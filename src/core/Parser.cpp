#include "Parser.hpp"

#include "CompilerException.hpp"
#include "FunctionRegistry.hpp"
#include "SymbolTable.hpp"

#include <sstream>
#include <unordered_set>
#include <utility>

namespace mathcompiler {

namespace {

enum class ExpectState {
    Operand,
    Operator
};

struct ParenFrame {
    bool isFunctionParen = false;
    std::string funcName;
    SourceSpan funcSpan{};
    std::size_t commaCount = 0;
    bool expectingFirstArg = true;
};

bool continuesExpressionAcrossNewline(TokenType prevType) noexcept {
    switch (prevType) {
        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::Multiply:
        case TokenType::Divide:
        case TokenType::Power:
        case TokenType::Modulo:
        case TokenType::UnaryMinus:
        case TokenType::UnaryPlus:
        case TokenType::Assign:
        case TokenType::LeftParen:
        case TokenType::Comma:
            return true;
        default:
            return false;
    }
}

std::vector<std::string> snapshotStack(const std::vector<Token>& stack) {
    std::vector<std::string> out;
    out.reserve(stack.size());
    for (const auto& t : stack) {
        out.push_back(t.displayLexeme());
    }
    return out;
}

void recordStep(std::vector<ShuntingYardStep>& steps,
                const Token& currentToken,
                std::string action,
                const std::vector<Token>& opStack,
                const std::vector<Token>& outputQueue) {
    steps.push_back(ShuntingYardStep{
        steps.size() + 1,
        currentToken,
        std::move(action),
        snapshotStack(opStack),
        snapshotStack(outputQueue)});
}

bool shouldPopOperator(const Token& stackTop, const Token& incoming) noexcept {
    if (stackTop.type == TokenType::LeftParen || stackTop.type == TokenType::Function) {
        return false;
    }
    const int topPrec = stackTop.precedence();
    const int inPrec = incoming.precedence();
    if (incoming.isRightAssociative()) {
        return topPrec > inPrec;
    }
    return topPrec >= inPrec;
}

BinaryOpKind toBinaryOp(TokenType type) noexcept {
    switch (type) {
        case TokenType::Plus:     return BinaryOpKind::Add;
        case TokenType::Minus:    return BinaryOpKind::Sub;
        case TokenType::Multiply: return BinaryOpKind::Mul;
        case TokenType::Divide:   return BinaryOpKind::Div;
        case TokenType::Power:    return BinaryOpKind::Pow;
        case TokenType::Modulo:   return BinaryOpKind::Mod;
        default:                  return BinaryOpKind::Add;
    }
}

std::unique_ptr<ASTNode> buildFunctionDef(const FunctionCallNode& lhsCall,
                                          std::unique_ptr<ASTNode> rhsBody,
                                          const SourceSpan& assignSpan) {
    const std::string& fnName = lhsCall.funcName();
    if (FunctionRegistry::isBuiltIn(fnName)) {
        throw SemanticException(
            "E_RESERVED_IDENTIFIER",
            "Cannot redefine built-in function '" + fnName + "()'",
            lhsCall.span(),
            "Choose a user function name such as 'f', 'g', or 'h'.");
    }

    std::vector<std::string> params;
    std::unordered_set<std::string> seen;
    params.reserve(lhsCall.args().size());

    for (const auto& arg : lhsCall.args()) {
        if (arg->kind() != ASTNodeKind::Variable) {
            throw SemanticException(
                "E_INVALID_PARAM",
                "Function parameter in '" + fnName + "(...)' definition must be a variable identifier",
                arg->span(),
                "Write function definitions as 'f(x) = x^2 + 1'.");
        }
        const std::string& pName = static_cast<const VariableNode&>(*arg).name();
        if (!SymbolTable::isAssignableIdentifier(pName)) {
            throw SemanticException(
                "E_RESERVED_IDENTIFIER",
                "Parameter name '" + pName + "' cannot shadow a reserved built-in function",
                arg->span());
        }
        if (!seen.insert(pName).second) {
            throw SemanticException(
                "E_DUPLICATE_PARAM",
                "Duplicate parameter name '" + pName + "' in function '" + fnName + "()'",
                arg->span());
        }
        params.push_back(pName);
    }

    const SourceSpan fullSpan = lhsCall.span().merge(rhsBody->span()).merge(assignSpan);
    return std::make_unique<FunctionDefNode>(
        fnName, lhsCall.span(), std::move(params), std::move(rhsBody), fullSpan);
}

std::unique_ptr<ASTNode> buildAstFromRpn(const std::vector<Token>& rpnTokens) {
    std::vector<std::unique_ptr<ASTNode>> stack;

    try {
        for (const auto& tok : rpnTokens) {
            if (tok.type == TokenType::Number) {
                stack.push_back(std::make_unique<NumberNode>(tok.numericValue, tok.span));
                continue;
            }
            if (tok.type == TokenType::Identifier) {
                stack.push_back(std::make_unique<VariableNode>(tok.lexeme, tok.span));
                continue;
            }
            if (tok.isUnaryOperator()) {
                if (stack.empty()) {
                    throw SyntaxException(
                        "E_MISSING_OPERAND",
                        "Missing operand for unary operator '" + tok.lexeme + "'",
                        tok.span);
                }
                auto operand = std::move(stack.back());
                stack.pop_back();
                const SourceSpan combined = tok.span.merge(operand->span());
                const UnaryOpKind opKind = tok.type == TokenType::UnaryMinus
                                               ? UnaryOpKind::Negate
                                               : UnaryOpKind::Plus;
                stack.push_back(std::make_unique<UnaryOpNode>(opKind, std::move(operand), combined));
                continue;
            }
            if (tok.type == TokenType::Function) {
                const std::size_t arity = static_cast<std::size_t>(tok.numericValue);
                if (auto spec = FunctionRegistry::find(tok.lexeme)) {
                    if (arity != spec->arity) {
                        throw ArityMismatchException(tok.lexeme, spec->arity, arity, tok.span);
                    }
                }
                if (stack.size() < arity) {
                    throw ArityMismatchException(tok.lexeme, arity, stack.size(), tok.span);
                }
                std::vector<std::unique_ptr<ASTNode>> args(arity);
                for (std::size_t i = arity; i > 0; --i) {
                    args[i - 1] = std::move(stack.back());
                    stack.pop_back();
                }
                SourceSpan combined = tok.span;
                for (const auto& arg : args) {
                    combined = combined.merge(arg->span());
                }
                stack.push_back(std::make_unique<FunctionCallNode>(tok.lexeme, std::move(args), combined));
                continue;
            }
            if (tok.type == TokenType::Assign) {
                if (stack.size() < 2) {
                    throw SyntaxException(
                        "E_MISSING_OPERAND",
                        "Assignment '=' requires both a target and an expression",
                        tok.span);
                }
                auto rhs = std::move(stack.back());
                stack.pop_back();
                auto lhs = std::move(stack.back());
                stack.pop_back();

                if (lhs->kind() == ASTNodeKind::FunctionCall) {
                    const auto& call = static_cast<const FunctionCallNode&>(*lhs);
                    stack.push_back(buildFunctionDef(call, std::move(rhs), tok.span));
                    continue;
                }

                if (lhs->kind() != ASTNodeKind::Variable) {
                    throw SemanticException(
                        "E_INVALID_ASSIGN",
                        "Left-hand side of '=' must be a variable identifier or function signature",
                        tok.span,
                        "Write 'x = 5' or 'f(x) = x^2 + 1'.");
                }
                const auto& varNode = static_cast<const VariableNode&>(*lhs);
                if (!SymbolTable::isAssignableIdentifier(varNode.name())) {
                    throw SemanticException(
                        "E_RESERVED_IDENTIFIER",
                        "Cannot assign to reserved built-in function '" + varNode.name() + "'",
                        varNode.span(),
                        "Choose a variable name other than a math function.");
                }
                const SourceSpan combined = lhs->span().merge(rhs->span());
                stack.push_back(std::make_unique<AssignNode>(
                    varNode.name(), varNode.span(), std::move(rhs), combined));
                continue;
            }
            if (tok.isBinaryOperator()) {
                if (stack.size() < 2) {
                    throw SyntaxException(
                        "E_MISSING_OPERAND",
                        "Binary operator '" + tok.lexeme + "' is missing an operand",
                        tok.span);
                }
                auto rhs = std::move(stack.back());
                stack.pop_back();
                auto lhs = std::move(stack.back());
                stack.pop_back();
                const SourceSpan combined = lhs->span().merge(rhs->span());
                stack.push_back(std::make_unique<BinaryOpNode>(
                    toBinaryOp(tok.type), std::move(lhs), std::move(rhs), combined));
                continue;
            }
        }
    } catch (CompilerException& ex) {
        ex.addFrame("Construct AST from RPN", ex.span());
        throw;
    }

    if (stack.size() != 1) {
        const SourceSpan errSpan = rpnTokens.empty() ? SourceSpan{} : rpnTokens.back().span;
        throw SyntaxException(
            "E_MALFORMED_EXPR",
            "Malformed expression: could not reduce RPN to a single AST root",
            errSpan);
    }
    return std::move(stack.back());
}

}  // namespace

std::vector<std::vector<Token>> Parser::splitStatements(const std::vector<Token>& tokens) {
    std::vector<std::vector<Token>> statements;
    std::vector<Token> current;
    int parenDepth = 0;

    for (const auto& tok : tokens) {
        if (tok.type == TokenType::EndOfInput) {
            break;
        }
        if (tok.type == TokenType::Semicolon) {
            if (!current.empty()) {
                statements.push_back(std::move(current));
                current.clear();
            }
            parenDepth = 0;
            continue;
        }
        if (tok.type == TokenType::Newline) {
            if (parenDepth == 0 && !current.empty() &&
                !continuesExpressionAcrossNewline(current.back().type)) {
                statements.push_back(std::move(current));
                current.clear();
            }
            continue;
        }

        if (tok.type == TokenType::LeftParen) {
            ++parenDepth;
        } else if (tok.type == TokenType::RightParen && parenDepth > 0) {
            --parenDepth;
        }
        current.push_back(tok);
    }

    if (!current.empty()) {
        statements.push_back(std::move(current));
    }
    return statements;
}

ParsedStatement Parser::parseStatement(const std::vector<Token>& statementTokens) const {
    std::vector<Token> input;
    input.reserve(statementTokens.size());
    for (const auto& tok : statementTokens) {
        if (tok.type != TokenType::Newline &&
            tok.type != TokenType::Semicolon &&
            tok.type != TokenType::EndOfInput) {
            input.push_back(tok);
        }
    }

    if (input.empty()) {
        throw SyntaxException("E_EMPTY_EXPR", "Expression cannot be empty", SourceSpan{1, 1, 0, 1});
    }

    ParsedStatement result;
    result.span = input.front().span.merge(input.back().span);

    std::vector<Token> opStack;
    std::vector<Token> outputQueue;
    std::vector<ParenFrame> parenStack;
    ExpectState expect = ExpectState::Operand;
    bool pendingFunctionParen = false;
    Token pendingFuncToken{};

    try {
        for (std::size_t i = 0; i < input.size(); ++i) {
            Token tok = input[i];
            const bool nextIsLeftParen = (i + 1 < input.size() && input[i + 1].type == TokenType::LeftParen);

            if (expect == ExpectState::Operand) {
                if (tok.type == TokenType::Number) {
                    if (!parenStack.empty()) {
                        parenStack.back().expectingFirstArg = false;
                    }
                    result.infixTokens.push_back(tok);
                    outputQueue.push_back(tok);
                    recordStep(result.shuntingYardSteps, tok,
                               "Push number '" + tok.lexeme + "' to Output Queue",
                               opStack, outputQueue);
                    expect = ExpectState::Operator;
                    continue;
                }

                if (tok.type == TokenType::Identifier) {
                    if (nextIsLeftParen) {
                        tok.type = TokenType::Function;
                        result.infixTokens.push_back(tok);
                        opStack.push_back(tok);
                        pendingFunctionParen = true;
                        pendingFuncToken = tok;
                        recordStep(result.shuntingYardSteps, tok,
                                   "Push function '" + tok.lexeme + "' onto Operator Stack",
                                   opStack, outputQueue);
                        continue;
                    }

                    if (!parenStack.empty()) {
                        parenStack.back().expectingFirstArg = false;
                    }
                    result.infixTokens.push_back(tok);
                    outputQueue.push_back(tok);
                    recordStep(result.shuntingYardSteps, tok,
                               "Push variable '" + tok.lexeme + "' to Output Queue",
                               opStack, outputQueue);
                    expect = ExpectState::Operator;
                    continue;
                }

                if (tok.type == TokenType::Minus || tok.type == TokenType::Plus) {
                    tok.type = (tok.type == TokenType::Minus) ? TokenType::UnaryMinus : TokenType::UnaryPlus;
                    result.infixTokens.push_back(tok);
                    opStack.push_back(tok);
                    recordStep(result.shuntingYardSteps, tok,
                               std::string("Push prefix unary '") + tok.displayLexeme() +
                                   "' (prec 3, right-assoc) onto Operator Stack",
                               opStack, outputQueue);
                    expect = ExpectState::Operand;
                    continue;
                }

                if (tok.type == TokenType::LeftParen) {
                    result.infixTokens.push_back(tok);
                    opStack.push_back(tok);
                    if (pendingFunctionParen) {
                        parenStack.push_back(ParenFrame{
                            true, pendingFuncToken.lexeme, pendingFuncToken.span, 0, true});
                        pendingFunctionParen = false;
                    } else {
                        if (!parenStack.empty()) {
                            parenStack.back().expectingFirstArg = false;
                        }
                        parenStack.push_back(ParenFrame{false, {}, tok.span, 0, true});
                    }
                    recordStep(result.shuntingYardSteps, tok,
                               "Push '(' onto Operator Stack",
                               opStack, outputQueue);
                    expect = ExpectState::Operand;
                    continue;
                }

                if (tok.type == TokenType::RightParen) {
                    if (parenStack.empty()) {
                        throw UnbalancedParenthesesException(
                            "Unmatched closing parenthesis ')'",
                            tok.span,
                            "Remove this ')' or add a matching '(' earlier in the expression.");
                    }
                    if (parenStack.back().isFunctionParen &&
                        parenStack.back().expectingFirstArg) {
                        auto spec = FunctionRegistry::find(parenStack.back().funcName);
                        if (spec.has_value()) {
                            throw ArityMismatchException(
                                parenStack.back().funcName, spec->arity, 0, tok.span);
                        }
                        throw SyntaxException(
                            "E_MISSING_ARGS",
                            "Function '" + parenStack.back().funcName + "()' requires at least one argument",
                            tok.span,
                            "Provide comma-separated argument(s) inside parentheses.");
                    }
                    if (parenStack.back().expectingFirstArg) {
                        throw SyntaxException(
                            "E_EMPTY_PARENS",
                            "Empty parentheses '()' are not allowed",
                            tok.span,
                            "Provide an expression inside the parentheses.");
                    }
                    if (i > 0 && input[i - 1].type == TokenType::Comma) {
                        throw SyntaxException(
                            "E_TRAILING_COMMA",
                            "Expected function argument after ',' before ')'",
                            tok.span,
                            "Provide an argument after the comma or remove the trailing comma.");
                    }
                    throw SyntaxException(
                        "E_UNEXPECTED_TOKEN",
                        "Unexpected ')' — expected an operand before ')'",
                        tok.span);
                }

                if (tok.isBinaryOperator()) {
                    std::string hint = (i > 0 && input[i - 1].isOperator())
                                           ? "Remove the extra operator or insert a parenthesized operand."
                                           : "Binary operators require a left-hand operand.";
                    throw SyntaxException(
                        "E_UNEXPECTED_OPERATOR",
                        "Unexpected operator '" + tok.lexeme + "' where an operand was expected",
                        tok.span,
                        std::move(hint));
                }

                if (tok.type == TokenType::Comma) {
                    throw SyntaxException(
                        "E_UNEXPECTED_COMMA",
                        "Unexpected ',' — expected an argument expression before comma",
                        tok.span);
                }
            } else {
                if (tok.isBinaryOperator()) {
                    result.infixTokens.push_back(tok);
                    std::ostringstream action;
                    int poppedCount = 0;
                    while (!opStack.empty() && shouldPopOperator(opStack.back(), tok)) {
                        outputQueue.push_back(opStack.back());
                        action << (poppedCount == 0 ? "Pop " : ", ") << "'" << opStack.back().displayLexeme() << "'";
                        opStack.pop_back();
                        ++poppedCount;
                    }
                    if (poppedCount > 0) {
                        action << " to Output; ";
                    }
                    opStack.push_back(tok);
                    action << "Push '" << tok.lexeme << "' (prec " << tok.precedence()
                           << (tok.isRightAssociative() ? ", right-assoc" : ", left-assoc")
                           << ") onto Operator Stack";
                    recordStep(result.shuntingYardSteps, tok, action.str(), opStack, outputQueue);
                    expect = ExpectState::Operand;
                    continue;
                }

                if (tok.type == TokenType::Comma) {
                    if (parenStack.empty() || !parenStack.back().isFunctionParen) {
                        throw SyntaxException(
                            "E_UNEXPECTED_COMMA",
                            "Unexpected ',' outside of function argument list",
                            tok.span,
                            "Commas separate arguments inside functions such as pow(a, b) or diff(x^2, x).");
                    }
                    result.infixTokens.push_back(tok);
                    while (!opStack.empty() && opStack.back().type != TokenType::LeftParen) {
                        outputQueue.push_back(opStack.back());
                        opStack.pop_back();
                    }
                    ++parenStack.back().commaCount;
                    recordStep(result.shuntingYardSteps, tok,
                               "Argument separator ',' — flushed sub-expression to Output Queue",
                               opStack, outputQueue);
                    expect = ExpectState::Operand;
                    continue;
                }

                if (tok.type == TokenType::RightParen) {
                    if (parenStack.empty()) {
                        throw UnbalancedParenthesesException(
                            "Unmatched closing parenthesis ')'",
                            tok.span,
                            "Remove this ')' or add a matching '(' earlier in the expression.");
                    }
                    result.infixTokens.push_back(tok);
                    while (!opStack.empty() && opStack.back().type != TokenType::LeftParen) {
                        outputQueue.push_back(opStack.back());
                        opStack.pop_back();
                    }
                    if (opStack.empty()) {
                        throw UnbalancedParenthesesException(
                            "Unmatched closing parenthesis ')'",
                            tok.span);
                    }
                    opStack.pop_back();

                    ParenFrame frame = parenStack.back();
                    parenStack.pop_back();

                    std::string action = "Match ')' and discard '('";
                    if (frame.isFunctionParen) {
                        if (!opStack.empty() && opStack.back().type == TokenType::Function) {
                            Token funcTok = opStack.back();
                            opStack.pop_back();
                            const std::size_t actualArgs = frame.commaCount + 1;
                            funcTok.numericValue = static_cast<double>(actualArgs);
                            if (auto spec = FunctionRegistry::find(funcTok.lexeme)) {
                                if (actualArgs != spec->arity) {
                                    throw ArityMismatchException(
                                        funcTok.lexeme, spec->arity, actualArgs, funcTok.span);
                                }
                            }
                            outputQueue.push_back(funcTok);
                            action += "; pop function '" + funcTok.lexeme + "' to Output Queue";
                        }
                    }

                    recordStep(result.shuntingYardSteps, tok, std::move(action), opStack, outputQueue);
                    expect = ExpectState::Operator;
                    continue;
                }

                throw SyntaxException(
                    "E_MISSING_OPERATOR",
                    "Expected an operator before '" + tok.lexeme + "'",
                    tok.span,
                    "Insert an operator (+, -, *, /, ^, %) before '" + tok.lexeme + "'.");
            }
        }

        if (expect == ExpectState::Operand) {
            const Token& lastTok = input.back();
            throw SyntaxException(
                "E_TRAILING_OPERATOR",
                "Unexpected end of expression after '" + lastTok.lexeme + "' — missing operand",
                lastTok.span,
                "Complete the expression with a number, variable, or parenthesized sub-expression.");
        }

        while (!opStack.empty()) {
            Token top = opStack.back();
            opStack.pop_back();
            if (top.type == TokenType::LeftParen) {
                throw UnbalancedParenthesesException(
                    "Unclosed opening parenthesis '(' — missing matching ')'",
                    top.span,
                    "Add a closing ')' at the end of the parenthesized expression.");
            }
            outputQueue.push_back(top);
            recordStep(result.shuntingYardSteps,
                       Token{TokenType::EndOfInput, "EOF", 0.0, input.back().span},
                       "End of input: pop '" + top.displayLexeme() + "' to Output Queue",
                       opStack, outputQueue);
        }
    } catch (CompilerException& ex) {
        ex.addFrame("Shunting-Yard Infix -> RPN", ex.span());
        throw;
    }

    result.rpnTokens = std::move(outputQueue);
    result.ast = buildAstFromRpn(result.rpnTokens);
    return result;
}

std::vector<ParsedStatement> Parser::parseProgram(const std::vector<Token>& tokens) const {
    auto slices = splitStatements(tokens);
    std::vector<ParsedStatement> parsed;
    parsed.reserve(slices.size());
    for (const auto& slice : slices) {
        parsed.push_back(parseStatement(slice));
    }
    return parsed;
}

}  // namespace mathcompiler
