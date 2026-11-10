#include "CompilerEngine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace mathcompiler {

namespace {

using Clock = std::chrono::steady_clock;

double elapsedMicros(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::micro>(end - start).count();
}

DiagnosticSnapshot makeDiagnosticSnapshot(CompilerException& ex, std::string_view source) {
    ex.setSourceText(std::string(source));
    return DiagnosticSnapshot{
        ex.phase(),
        ex.errorCode(),
        ex.detailMessage(),
        ex.hint(),
        ex.span(),
        ex.formatCaretBlock(),
        ex.formatReport(),
        ex.callStack()};
}

std::string formatAsFraction(double value, int maxDenomPower) {
    if (!std::isfinite(value)) {
        return "NaN";
    }
    if (std::fabs(value) < 1e-12) {
        return "0";
    }
    if (std::fabs(value) > 1e9) {
        std::ostringstream oss;
        oss << std::scientific << std::setprecision(6) << value;
        return oss.str();
    }

    const bool negative = value < 0.0;
    const double target = std::fabs(value);

    const double nearestInt = std::round(target);
    if (std::fabs(target - nearestInt) < 1e-11) {
        return (negative ? "-" : "") + std::to_string(static_cast<long long>(std::llround(nearestInt)));
    }

    long long maxDenom = 1;
    const int clampedPow = std::clamp(maxDenomPower, 2, 6);
    for (int i = 0; i < clampedPow; ++i) {
        maxDenom *= 10;
    }

    long long h0 = 0;
    long long h1 = 1;
    long long k0 = 1;
    long long k1 = 0;
    double x = target;

    for (int iter = 0; iter < 64; ++iter) {
        const long long a = static_cast<long long>(std::floor(x));
        const long long h2 = a * h1 + h0;
        const long long k2 = a * k1 + k0;
        if (k2 > maxDenom) {
            break;
        }
        h0 = h1;
        h1 = h2;
        k0 = k1;
        k1 = k2;

        const double approx = static_cast<double>(h1) / static_cast<double>(k1);
        if (std::fabs(target - approx) < 1e-10) {
            break;
        }
        const double frac = x - static_cast<double>(a);
        if (frac < 1e-12) {
            break;
        }
        x = 1.0 / frac;
    }

    if (k1 == 0) {
        return (negative ? "-" : "") + std::to_string(static_cast<long long>(std::llround(target)));
    }

    const double approx = static_cast<double>(h1) / static_cast<double>(k1);
    const bool exact = std::fabs(target - approx) < 1e-9;

    std::ostringstream oss;
    if (!exact) {
        oss << "~";
    }
    if (negative) {
        oss << "-";
    }
    oss << h1;
    if (k1 != 1) {
        oss << "/" << k1;
    }
    return oss.str();
}

bool isPureSideEffectFreeConstant(const ASTNode& node) {
    return node.kind() == ASTNodeKind::Number;
}

bool hasPureRhs(const ASTNode& node) {
    switch (node.kind()) {
        case ASTNodeKind::Number:
            return true;
        case ASTNodeKind::Variable:
            return false;
        case ASTNodeKind::UnaryOp:
            return hasPureRhs(static_cast<const UnaryOpNode&>(node).operand());
        case ASTNodeKind::BinaryOp: {
            const auto& b = static_cast<const BinaryOpNode&>(node);
            if (b.op() == BinaryOpKind::Div || b.op() == BinaryOpKind::Mod) {
                return false;
            }
            return hasPureRhs(b.left()) && hasPureRhs(b.right());
        }
        default:
            return false;
    }
}

}  // namespace

CompilationResult CompilerEngine::compileAndEvaluate(std::string_view source, bool enableOptimization) {
    SymbolTable workingSymbols = symbols_;
    CompilationResult result = compileAndEvaluateWithSymbols(source, workingSymbols, enableOptimization);
    if (result.success) {
        symbols_ = std::move(workingSymbols);
    }
    return result;
}

CompilationResult CompilerEngine::compileAndEvaluateWithSymbols(std::string_view source,
                                                                SymbolTable& externalSymbols,
                                                                bool enableOptimization) const {
    const auto totalStart = Clock::now();

    CompilationResult result;
    result.source = std::string(source);

    try {
        const auto lexStart = Clock::now();
        result.allTokens = lexer_.tokenize(source);
        const auto lexEnd = Clock::now();
        result.profile.lexUs = elapsedMicros(lexStart, lexEnd);

        const auto parseStart = Clock::now();
        auto slices = Parser::splitStatements(result.allTokens);
        if (slices.empty()) {
            const auto parseEnd = Clock::now();
            result.profile.parseUs = elapsedMicros(parseStart, parseEnd);
            result.profile.totalUs = elapsedMicros(totalStart, parseEnd);
            result.success = true;
            result.emptyInput = true;
            return result;
        }

        std::vector<ParsedStatement> parsedStmts;
        parsedStmts.reserve(slices.size());
        for (const auto& slice : slices) {
            parsedStmts.push_back(parser_.parseStatement(slice));
        }
        const auto parseEnd = Clock::now();
        result.profile.parseUs = elapsedMicros(parseStart, parseEnd);

        // Optimize each statement AST
        const auto optStart = Clock::now();
        result.statements.reserve(parsedStmts.size());
        for (std::size_t idx = 0; idx < parsedStmts.size(); ++idx) {
            StatementArtifact artifact;
            artifact.statementIndex = idx + 1;
            artifact.infixTokens = std::move(parsedStmts[idx].infixTokens);
            artifact.rpnTokens = std::move(parsedStmts[idx].rpnTokens);
            artifact.shuntingYardSteps = std::move(parsedStmts[idx].shuntingYardSteps);
            artifact.rawAst = std::move(parsedStmts[idx].ast);
            artifact.rawNodeCount = artifact.rawAst->nodeCount();
            artifact.hasSymbolicDiff = SymbolicDiff::containsDiffCall(*artifact.rawAst);

            if (enableOptimization || artifact.hasSymbolicDiff) {
                OptimizationResult opt = ASTOptimizer::optimize(*artifact.rawAst);
                artifact.optimizedAst = std::move(opt.optimizedRoot);
                artifact.optimizedNodeCount = opt.optimizedNodeCount;
                artifact.foldsPerformed = opt.foldsPerformed;
                artifact.optimizationLog = std::move(opt.passLog);
            } else {
                artifact.optimizedAst = artifact.rawAst->clone();
                artifact.optimizedNodeCount = artifact.rawNodeCount;
                artifact.foldsPerformed = 0;
            }

            if (artifact.hasSymbolicDiff) {
                artifact.symbolicForm = artifact.optimizedAst->toInfixString();
            } else if (artifact.optimizedAst->kind() == ASTNodeKind::FunctionDef) {
                artifact.symbolicForm = artifact.optimizedAst->toInfixString();
            }

            result.statements.push_back(std::move(artifact));
        }

        // Program-level dead-code elimination pass across statements
        if (enableOptimization && result.statements.size() > 1) {
            for (std::size_t i = 0; i + 1 < result.statements.size(); ++i) {
                auto& cur = result.statements[i];
                const ASTNode& curNode = *cur.optimizedAst;

                if (isPureSideEffectFreeConstant(curNode)) {
                    cur.eliminatedAsDeadCode = true;
                    ++cur.foldsPerformed;
                    std::string msg = "Dead-code elimination: removed unused pure expression at statement #" +
                                      std::to_string(cur.statementIndex);
                    cur.optimizationLog.push_back(msg);
                    result.deadCodeLog.push_back(std::move(msg));
                    continue;
                }

                if (curNode.kind() == ASTNodeKind::Assign) {
                    const auto& assignCur = static_cast<const AssignNode&>(curNode);
                    const ASTNode& nextNode = *result.statements[i + 1].optimizedAst;
                    if (nextNode.kind() == ASTNodeKind::Assign && hasPureRhs(assignCur.rhs())) {
                        const auto& assignNext = static_cast<const AssignNode&>(nextNode);
                        if (assignCur.targetName() == assignNext.targetName() &&
                            !SymbolicDiff::referencesVariable(assignNext.rhs(), assignCur.targetName())) {
                            cur.eliminatedAsDeadCode = true;
                            ++cur.foldsPerformed;
                            std::string msg = "Dead-store elimination: removed overwritten assignment '" +
                                              assignCur.targetName() + " = ...' at statement #" +
                                              std::to_string(cur.statementIndex);
                            cur.optimizationLog.push_back(msg);
                            result.deadCodeLog.push_back(std::move(msg));
                        }
                    }
                }
            }
        }
        const auto optEnd = Clock::now();
        result.profile.optimizeUs = elapsedMicros(optStart, optEnd);

        // Compile all live statements to Bytecode
        const auto compileStart = Clock::now();
        for (auto& artifact : result.statements) {
            if (!artifact.eliminatedAsDeadCode) {
                artifact.bytecode = BytecodeCompiler::compile(*artifact.optimizedAst);
            }
        }
        const auto compileEnd = Clock::now();
        result.profile.bytecodeCompileUs = elapsedMicros(compileStart, compileEnd);

        // Execute on Stack Bytecode VM and benchmark against AST-Walker
        SymbolTable vmSymbols = externalSymbols;
        SymbolTable astSymbols = externalSymbols;

        for (std::size_t idx = 0; idx < result.statements.size(); ++idx) {
            auto& artifact = result.statements[idx];
            if (artifact.eliminatedAsDeadCode) {
                continue;
            }

            const bool isLast = (idx + 1 == result.statements.size());
            const bool isFuncDef = (artifact.optimizedAst->kind() == ASTNodeKind::FunctionDef);

            try {
                const auto vmStart = Clock::now();
                artifact.vmValue = vm_.execute(artifact.bytecode, vmSymbols);
                const auto vmEnd = Clock::now();
                result.profile.vmExecUs += elapsedMicros(vmStart, vmEnd);

                const auto astStart = Clock::now();
                artifact.evalOutput = evaluator_.evaluate(*artifact.optimizedAst, astSymbols);
                const auto astEnd = Clock::now();
                result.profile.astWalkExecUs += elapsedMicros(astStart, astEnd);

                if (isFuncDef) {
                    result.hasNumericValue = false;
                    result.symbolicResult = artifact.symbolicForm;
                    result.statusNote = "Function '" + artifact.evalOutput.assignedSymbol + "' registered.";
                } else {
                    result.hasNumericValue = true;
                    result.finalValue = artifact.evalOutput.value;
                    result.vmFinalValue = artifact.vmValue;
                    if (artifact.hasSymbolicDiff) {
                        result.symbolicResult = artifact.symbolicForm;
                    } else {
                        result.symbolicResult.clear();
                    }
                    result.statusNote.clear();
                }
            } catch (const UndeclaredVariableException&) {
                if (isLast && artifact.hasSymbolicDiff) {
                    // Pure symbolic derivative without a numeric variable binding still succeeds symbolically.
                    result.hasNumericValue = false;
                    result.symbolicResult = artifact.symbolicForm;
                    result.statusNote = "Symbolic derivative ready (assign variable to evaluate numerically).";
                } else {
                    throw;
                }
            } catch (CompilerException& ex) {
                if (result.statements.size() > 1) {
                    ex.addFrame("Statement #" + std::to_string(artifact.statementIndex), parsedStmts[idx].span);
                }
                throw;
            }
        }

        externalSymbols = std::move(vmSymbols);
        result.success = true;
    } catch (CompilerException& ex) {
        result.success = false;
        result.hasNumericValue = false;
        result.error = makeDiagnosticSnapshot(ex, source);
    }

    result.profile.totalUs = elapsedMicros(totalStart, Clock::now());
    return result;
}

std::string CompilerEngine::formatResult(double value, NumberFormatMode mode, int precision) {
    const int clampedPrecision = std::clamp(precision, 0, 15);
    const double cleanVal = (value == 0.0) ? 0.0 : value;

    std::ostringstream oss;
    switch (mode) {
        case NumberFormatMode::Auto:
            oss << std::setprecision(std::max(1, clampedPrecision)) << cleanVal;
            break;
        case NumberFormatMode::Fixed:
            oss << std::fixed << std::setprecision(clampedPrecision) << cleanVal;
            break;
        case NumberFormatMode::Scientific:
            oss << std::scientific << std::setprecision(clampedPrecision) << cleanVal;
            break;
        case NumberFormatMode::Fraction:
            return formatAsFraction(cleanVal, clampedPrecision);
    }
    return oss.str();
}

}  // namespace mathcompiler
