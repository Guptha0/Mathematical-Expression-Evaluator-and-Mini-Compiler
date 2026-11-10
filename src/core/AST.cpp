#include "AST.hpp"

#include "CompilerException.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mathcompiler {

namespace {

int binaryPrecedence(BinaryOpKind op) noexcept {
    switch (op) {
        case BinaryOpKind::Add:
        case BinaryOpKind::Sub: return 1;
        case BinaryOpKind::Mul:
        case BinaryOpKind::Div:
        case BinaryOpKind::Mod: return 2;
        case BinaryOpKind::Pow: return 4;
    }
    return 0;
}

bool needsRightParens(BinaryOpKind parentOp, BinaryOpKind childOp) noexcept {
    const int pPrec = binaryPrecedence(parentOp);
    const int cPrec = binaryPrecedence(childOp);
    if (cPrec < pPrec) return true;
    if (cPrec > pPrec) return false;
    if (parentOp == BinaryOpKind::Sub || parentOp == BinaryOpKind::Div ||
        parentOp == BinaryOpKind::Mod) {
        return true;
    }
    if (parentOp == BinaryOpKind::Pow && childOp == BinaryOpKind::Pow) {
        return false;
    }
    return false;
}

const NumberNode* asNumber(const ASTNode* node) noexcept {
    return node && node->kind() == ASTNodeKind::Number
               ? static_cast<const NumberNode*>(node)
               : nullptr;
}

bool isZero(const ASTNode* node) noexcept {
    const auto* n = asNumber(node);
    return n && n->value() == 0.0;
}

bool isOne(const ASTNode* node) noexcept {
    const auto* n = asNumber(node);
    return n && n->value() == 1.0;
}

std::unique_ptr<ASTNode> makeNum(double val, SourceSpan span, bool folded = true) {
    return std::make_unique<NumberNode>(val == 0.0 ? 0.0 : val, span, folded);
}

std::unique_ptr<ASTNode> makeBin(BinaryOpKind op,
                                 std::unique_ptr<ASTNode> lhs,
                                 std::unique_ptr<ASTNode> rhs,
                                 SourceSpan span) {
    return std::make_unique<BinaryOpNode>(op, std::move(lhs), std::move(rhs), span);
}

std::unique_ptr<ASTNode> makeCall(std::string name,
                                  std::unique_ptr<ASTNode> arg,
                                  SourceSpan span) {
    std::vector<std::unique_ptr<ASTNode>> args;
    args.push_back(std::move(arg));
    return std::make_unique<FunctionCallNode>(std::move(name), std::move(args), span);
}

std::unique_ptr<ASTNode> simplifyImpl(const ASTNode& node) {
    switch (node.kind()) {
        case ASTNodeKind::Number:
        case ASTNodeKind::Variable:
            return node.clone();

        case ASTNodeKind::UnaryOp: {
            const auto& u = static_cast<const UnaryOpNode&>(node);
            auto inner = simplifyImpl(u.operand());
            if (u.op() == UnaryOpKind::Plus) {
                return inner;
            }
            if (const auto* n = asNumber(inner.get())) {
                return makeNum(-n->value(), u.span());
            }
            if (inner->kind() == ASTNodeKind::UnaryOp &&
                static_cast<const UnaryOpNode&>(*inner).op() == UnaryOpKind::Negate) {
                return static_cast<const UnaryOpNode&>(*inner).operand().clone();
            }
            return std::make_unique<UnaryOpNode>(u.op(), std::move(inner), u.span());
        }

        case ASTNodeKind::BinaryOp: {
            const auto& b = static_cast<const BinaryOpNode&>(node);
            auto lhs = simplifyImpl(b.left());
            auto rhs = simplifyImpl(b.right());

            const auto* ln = asNumber(lhs.get());
            const auto* rn = asNumber(rhs.get());
            if (ln && rn) {
                try {
                    return makeNum(applyBinaryOp(b.op(), ln->value(), rn->value(), b.span()), b.span());
                } catch (const CompilerException&) {
                }
            }

            if (b.op() == BinaryOpKind::Add) {
                if (isZero(lhs.get())) return rhs;
                if (isZero(rhs.get())) return lhs;
                if (rhs->kind() == ASTNodeKind::UnaryOp &&
                    static_cast<const UnaryOpNode&>(*rhs).op() == UnaryOpKind::Negate) {
                    return simplifyImpl(BinaryOpNode(
                        BinaryOpKind::Sub,
                        std::move(lhs),
                        static_cast<const UnaryOpNode&>(*rhs).operand().clone(),
                        b.span()));
                }
            } else if (b.op() == BinaryOpKind::Sub) {
                if (isZero(rhs.get())) return lhs;
                if (isZero(lhs.get())) {
                    return simplifyImpl(UnaryOpNode(UnaryOpKind::Negate, std::move(rhs), b.span()));
                }
            } else if (b.op() == BinaryOpKind::Mul) {
                if (isZero(lhs.get()) || isZero(rhs.get())) return makeNum(0.0, b.span());
                if (isOne(lhs.get())) return rhs;
                if (isOne(rhs.get())) return lhs;
                if (ln && ln->value() == -1.0) {
                    return simplifyImpl(UnaryOpNode(UnaryOpKind::Negate, std::move(rhs), b.span()));
                }
                if (rn && rn->value() == -1.0) {
                    return simplifyImpl(UnaryOpNode(UnaryOpKind::Negate, std::move(lhs), b.span()));
                }
                // Associative constant folding: c1 * (c2 * u) -> (c1 * c2) * u
                if (ln && rhs->kind() == ASTNodeKind::BinaryOp) {
                    const auto& rb = static_cast<const BinaryOpNode&>(*rhs);
                    if (rb.op() == BinaryOpKind::Mul) {
                        if (const auto* rln = asNumber(&rb.left())) {
                            return simplifyImpl(BinaryOpNode(
                                BinaryOpKind::Mul,
                                makeNum(ln->value() * rln->value(), b.span()),
                                rb.right().clone(),
                                b.span()));
                        }
                    }
                }
                if (rn && lhs->kind() == ASTNodeKind::BinaryOp) {
                    const auto& lb = static_cast<const BinaryOpNode&>(*lhs);
                    if (lb.op() == BinaryOpKind::Mul) {
                        if (const auto* lln = asNumber(&lb.left())) {
                            return simplifyImpl(BinaryOpNode(
                                BinaryOpKind::Mul,
                                makeNum(rn->value() * lln->value(), b.span()),
                                lb.right().clone(),
                                b.span()));
                        }
                    }
                }
                // Canonicalize `u * c` as `c * u` when u is non-numeric for cleaner calculus forms.
                if (rn && !ln) {
                    return std::make_unique<BinaryOpNode>(BinaryOpKind::Mul, std::move(rhs), std::move(lhs), b.span());
                }
            } else if (b.op() == BinaryOpKind::Div) {
                if (isZero(lhs.get()) && (!rn || rn->value() != 0.0)) return makeNum(0.0, b.span());
                if (isOne(rhs.get())) return lhs;
            } else if (b.op() == BinaryOpKind::Pow) {
                if (isZero(rhs.get())) return makeNum(1.0, b.span());
                if (isOne(rhs.get())) return lhs;
                if (isOne(lhs.get())) return makeNum(1.0, b.span());
            }

            return makeBin(b.op(), std::move(lhs), std::move(rhs), b.span());
        }

        case ASTNodeKind::FunctionCall: {
            const auto& f = static_cast<const FunctionCallNode&>(node);
            std::vector<std::unique_ptr<ASTNode>> args;
            std::vector<double> constVals;
            bool allConst = true;
            for (const auto& a : f.args()) {
                auto sa = simplifyImpl(*a);
                if (const auto* n = asNumber(sa.get())) {
                    constVals.push_back(n->value());
                } else {
                    allConst = false;
                }
                args.push_back(std::move(sa));
            }
            if (allConst && f.funcName() != "diff" && f.funcName() != "ndiff") {
                try {
                    return makeNum(applyMathFunction(f.funcName(), constVals, f.span()), f.span());
                } catch (const CompilerException&) {
                }
            }
            return std::make_unique<FunctionCallNode>(f.funcName(), std::move(args), f.span());
        }

        case ASTNodeKind::Assign: {
            const auto& a = static_cast<const AssignNode&>(node);
            return std::make_unique<AssignNode>(
                a.targetName(), a.targetSpan(), simplifyImpl(a.rhs()), a.span());
        }

        case ASTNodeKind::FunctionDef: {
            const auto& fd = static_cast<const FunctionDefNode&>(node);
            return std::make_unique<FunctionDefNode>(
                fd.funcName(), fd.funcSpan(), fd.params(), simplifyImpl(fd.body()), fd.span());
        }
    }
    return node.clone();
}

std::unique_ptr<ASTNode> diffRaw(const ASTNode& expr, std::string_view var);

std::unique_ptr<ASTNode> diffPower(const ASTNode& base,
                                   const ASTNode& exp,
                                   std::string_view var,
                                   SourceSpan span) {
    const bool baseHasVar = SymbolicDiff::referencesVariable(base, var);
    const bool expHasVar = SymbolicDiff::referencesVariable(exp, var);

    if (!baseHasVar && !expHasVar) {
        return makeNum(0.0, span);
    }

    if (!expHasVar) {
        // Power rule: exp * base^(exp - 1) * d(base)
        auto expMinusOne = makeBin(BinaryOpKind::Sub, exp.clone(), makeNum(1.0, span), span);
        auto powTerm = makeBin(BinaryOpKind::Pow, base.clone(), std::move(expMinusOne), span);
        auto dBase = diffRaw(base, var);
        return makeBin(
            BinaryOpKind::Mul,
            makeBin(BinaryOpKind::Mul, exp.clone(), std::move(powTerm), span),
            std::move(dBase),
            span);
    }

    if (!baseHasVar) {
        // Exponential rule: a^v * ln(a) * dv
        auto powTerm = makeBin(BinaryOpKind::Pow, base.clone(), exp.clone(), span);
        auto lnBase = makeCall("ln", base.clone(), span);
        auto dExp = diffRaw(exp, var);
        return makeBin(
            BinaryOpKind::Mul,
            makeBin(BinaryOpKind::Mul, std::move(powTerm), std::move(lnBase), span),
            std::move(dExp),
            span);
    }

    // Generalized power rule: u^v * (v' * ln(u) + v * u' / u)
    auto powTerm = makeBin(BinaryOpKind::Pow, base.clone(), exp.clone(), span);
    auto term1 = makeBin(BinaryOpKind::Mul, diffRaw(exp, var), makeCall("ln", base.clone(), span), span);
    auto term2 = makeBin(
        BinaryOpKind::Div,
        makeBin(BinaryOpKind::Mul, exp.clone(), diffRaw(base, var), span),
        base.clone(),
        span);
    return makeBin(BinaryOpKind::Mul, std::move(powTerm), makeBin(BinaryOpKind::Add, std::move(term1), std::move(term2), span), span);
}

std::unique_ptr<ASTNode> diffRaw(const ASTNode& expr, std::string_view var) {
    const SourceSpan span = expr.span();
    switch (expr.kind()) {
        case ASTNodeKind::Number:
            return makeNum(0.0, span);

        case ASTNodeKind::Variable: {
            const auto& v = static_cast<const VariableNode&>(expr);
            return makeNum(v.name() == var ? 1.0 : 0.0, span);
        }

        case ASTNodeKind::UnaryOp: {
            const auto& u = static_cast<const UnaryOpNode&>(expr);
            auto dOp = diffRaw(u.operand(), var);
            if (u.op() == UnaryOpKind::Plus) return dOp;
            return std::make_unique<UnaryOpNode>(UnaryOpKind::Negate, std::move(dOp), span);
        }

        case ASTNodeKind::BinaryOp: {
            const auto& b = static_cast<const BinaryOpNode&>(expr);
            switch (b.op()) {
                case BinaryOpKind::Add:
                    return makeBin(BinaryOpKind::Add, diffRaw(b.left(), var), diffRaw(b.right(), var), span);
                case BinaryOpKind::Sub:
                    return makeBin(BinaryOpKind::Sub, diffRaw(b.left(), var), diffRaw(b.right(), var), span);
                case BinaryOpKind::Mul: {
                    auto t1 = makeBin(BinaryOpKind::Mul, diffRaw(b.left(), var), b.right().clone(), span);
                    auto t2 = makeBin(BinaryOpKind::Mul, b.left().clone(), diffRaw(b.right(), var), span);
                    return makeBin(BinaryOpKind::Add, std::move(t1), std::move(t2), span);
                }
                case BinaryOpKind::Div: {
                    auto num1 = makeBin(BinaryOpKind::Mul, diffRaw(b.left(), var), b.right().clone(), span);
                    auto num2 = makeBin(BinaryOpKind::Mul, b.left().clone(), diffRaw(b.right(), var), span);
                    auto num = makeBin(BinaryOpKind::Sub, std::move(num1), std::move(num2), span);
                    auto den = makeBin(BinaryOpKind::Pow, b.right().clone(), makeNum(2.0, span), span);
                    return makeBin(BinaryOpKind::Div, std::move(num), std::move(den), span);
                }
                case BinaryOpKind::Pow:
                    return diffPower(b.left(), b.right(), var, span);
                case BinaryOpKind::Mod:
                    throw SemanticException(
                        "E_SYMBOLIC_DIFF",
                        "Modulo operator '%' is not symbolically differentiable",
                        span,
                        "Use ndiff() for numerical finite-difference derivatives.");
            }
            break;
        }

        case ASTNodeKind::FunctionCall: {
            const auto& f = static_cast<const FunctionCallNode&>(expr);
            if (f.funcName() == "diff") {
                if (f.args().size() != 2 || f.args()[1]->kind() != ASTNodeKind::Variable) {
                    throw SemanticException(
                        "E_SYMBOLIC_DIFF",
                        "diff(expr, var) requires a variable identifier as its second argument",
                        span);
                }
                const auto& innerVar = static_cast<const VariableNode&>(*f.args()[1]).name();
                auto lowered = SymbolicDiff::differentiate(*f.args()[0], innerVar);
                return diffRaw(*lowered, var);
            }
            if (f.funcName() == "pow" && f.args().size() == 2) {
                return diffPower(*f.args()[0], *f.args()[1], var, span);
            }
            if (f.args().size() != 1) {
                throw SemanticException(
                    "E_SYMBOLIC_DIFF",
                    "Cannot symbolically differentiate '" + f.funcName() + "()'",
                    span);
            }

            const ASTNode& u = *f.args()[0];
            auto du = diffRaw(u, var);

            if (f.funcName() == "sin") {
                return makeBin(BinaryOpKind::Mul, makeCall("cos", u.clone(), span), std::move(du), span);
            }
            if (f.funcName() == "cos") {
                auto negSin = std::make_unique<UnaryOpNode>(UnaryOpKind::Negate, makeCall("sin", u.clone(), span), span);
                return makeBin(BinaryOpKind::Mul, std::move(negSin), std::move(du), span);
            }
            if (f.funcName() == "tan") {
                auto cosSq = makeBin(BinaryOpKind::Pow, makeCall("cos", u.clone(), span), makeNum(2.0, span), span);
                return makeBin(BinaryOpKind::Div, std::move(du), std::move(cosSq), span);
            }
            if (f.funcName() == "exp") {
                return makeBin(BinaryOpKind::Mul, makeCall("exp", u.clone(), span), std::move(du), span);
            }
            if (f.funcName() == "ln" || f.funcName() == "log") {
                return makeBin(BinaryOpKind::Div, std::move(du), u.clone(), span);
            }
            if (f.funcName() == "log10") {
                auto denom = makeBin(BinaryOpKind::Mul, u.clone(), makeCall("ln", makeNum(10.0, span), span), span);
                return makeBin(BinaryOpKind::Div, std::move(du), std::move(denom), span);
            }
            if (f.funcName() == "sqrt") {
                auto denom = makeBin(BinaryOpKind::Mul, makeNum(2.0, span), makeCall("sqrt", u.clone(), span), span);
                return makeBin(BinaryOpKind::Div, std::move(du), std::move(denom), span);
            }
            if (f.funcName() == "abs") {
                auto sgn = makeBin(BinaryOpKind::Div, u.clone(), makeCall("abs", u.clone(), span), span);
                return makeBin(BinaryOpKind::Mul, std::move(sgn), std::move(du), span);
            }
            throw SemanticException(
                "E_SYMBOLIC_DIFF",
                "Cannot symbolically differentiate function '" + f.funcName() + "()'",
                span,
                "Symbolic differentiation supports +, -, *, /, ^, sin, cos, tan, exp, ln, log, log10, sqrt, pow, abs.");
        }

        case ASTNodeKind::Assign:
        case ASTNodeKind::FunctionDef:
            throw SemanticException(
                "E_SYMBOLIC_DIFF",
                "Cannot differentiate an assignment or function definition directly",
                span);
    }
    return makeNum(0.0, span);
}

std::unique_ptr<ASTNode> optimizeNode(const ASTNode& node, OptimizationResult& stats) {
    switch (node.kind()) {
        case ASTNodeKind::Number:
        case ASTNodeKind::Variable:
            return node.clone();

        case ASTNodeKind::UnaryOp: {
            const auto& u = static_cast<const UnaryOpNode&>(node);
            auto optOperand = optimizeNode(u.operand(), stats);

            if (u.op() == UnaryOpKind::Plus) {
                ++stats.foldsPerformed;
                stats.passLog.push_back("Dead-code / identity: +(" + optOperand->label() + ") -> " + optOperand->label());
                return optOperand;
            }

            if (const auto* num = asNumber(optOperand.get())) {
                try {
                    const double folded = applyUnaryOp(u.op(), num->value(), u.span());
                    ++stats.foldsPerformed;
                    stats.passLog.push_back("Constant fold: -(" + num->label() + ") -> " + formatCompactNumber(folded));
                    return std::make_unique<NumberNode>(folded, u.span(), true);
                } catch (const CompilerException&) {
                }
            }

            if (optOperand->kind() == ASTNodeKind::UnaryOp) {
                const auto& inner = static_cast<const UnaryOpNode&>(*optOperand);
                if (u.op() == UnaryOpKind::Negate && inner.op() == UnaryOpKind::Negate) {
                    ++stats.foldsPerformed;
                    stats.passLog.push_back("Double negation: --(" + inner.operand().label() + ") -> " + inner.operand().label());
                    return inner.operand().clone();
                }
            }

            return std::make_unique<UnaryOpNode>(u.op(), std::move(optOperand), u.span());
        }

        case ASTNodeKind::BinaryOp: {
            const auto& b = static_cast<const BinaryOpNode&>(node);
            auto optLeft = optimizeNode(b.left(), stats);
            auto optRight = optimizeNode(b.right(), stats);

            const auto* lhsNum = asNumber(optLeft.get());
            const auto* rhsNum = asNumber(optRight.get());

            if (lhsNum && rhsNum) {
                try {
                    const double folded = applyBinaryOp(b.op(), lhsNum->value(), rhsNum->value(), b.span());
                    ++stats.foldsPerformed;
                    stats.passLog.push_back("Constant fold: " + lhsNum->label() + " " +
                                            binaryOpSymbol(b.op()) + " " + rhsNum->label() +
                                            " -> " + formatCompactNumber(folded));
                    return std::make_unique<NumberNode>(folded, b.span(), true);
                } catch (const CompilerException&) {
                }
            }

            if (b.op() == BinaryOpKind::Add) {
                if (rhsNum && rhsNum->value() == 0.0) {
                    ++stats.foldsPerformed;
                    stats.passLog.push_back("Identity elimination: " + optLeft->label() + " + 0 -> " + optLeft->label());
                    return optLeft;
                }
                if (lhsNum && lhsNum->value() == 0.0) {
                    ++stats.foldsPerformed;
                    stats.passLog.push_back("Identity elimination: 0 + " + optRight->label() + " -> " + optRight->label());
                    return optRight;
                }
            } else if (b.op() == BinaryOpKind::Sub) {
                if (rhsNum && rhsNum->value() == 0.0) {
                    ++stats.foldsPerformed;
                    stats.passLog.push_back("Identity elimination: " + optLeft->label() + " - 0 -> " + optLeft->label());
                    return optLeft;
                }
            } else if (b.op() == BinaryOpKind::Mul) {
                if (rhsNum && rhsNum->value() == 1.0) {
                    ++stats.foldsPerformed;
                    stats.passLog.push_back("Identity elimination: " + optLeft->label() + " * 1 -> " + optLeft->label());
                    return optLeft;
                }
                if (lhsNum && lhsNum->value() == 1.0) {
                    ++stats.foldsPerformed;
                    stats.passLog.push_back("Identity elimination: 1 * " + optRight->label() + " -> " + optRight->label());
                    return optRight;
                }
            } else if (b.op() == BinaryOpKind::Div) {
                if (rhsNum && rhsNum->value() == 1.0) {
                    ++stats.foldsPerformed;
                    stats.passLog.push_back("Identity elimination: " + optLeft->label() + " / 1 -> " + optLeft->label());
                    return optLeft;
                }
            } else if (b.op() == BinaryOpKind::Pow) {
                if (rhsNum && rhsNum->value() == 1.0) {
                    ++stats.foldsPerformed;
                    stats.passLog.push_back("Identity elimination: " + optLeft->label() + " ^ 1 -> " + optLeft->label());
                    return optLeft;
                }
            }

            return std::make_unique<BinaryOpNode>(b.op(), std::move(optLeft), std::move(optRight), b.span());
        }

        case ASTNodeKind::FunctionCall: {
            const auto& f = static_cast<const FunctionCallNode&>(node);

            if (f.funcName() == "diff") {
                if (f.args().size() != 2) {
                    throw ArityMismatchException("diff", 2, f.args().size(), f.span());
                }
                if (f.args()[1]->kind() != ASTNodeKind::Variable) {
                    throw SemanticException(
                        "E_SYMBOLIC_DIFF",
                        "Second argument of diff(expr, var) must be a variable name",
                        f.args()[1]->span(),
                        "Example: diff(x^2 + 2*x, x)");
                }
                const auto& varName = static_cast<const VariableNode&>(*f.args()[1]).name();
                auto lowered = SymbolicDiff::differentiate(*f.args()[0], varName);
                ++stats.foldsPerformed;
                stats.passLog.push_back("Symbolic diff: diff(..., " + varName + ") -> " + lowered->toInfixString());
                return lowered;
            }

            std::vector<std::unique_ptr<ASTNode>> optArgs;
            optArgs.reserve(f.args().size());
            std::vector<double> constValues;
            constValues.reserve(f.args().size());
            bool allConst = true;

            for (const auto& arg : f.args()) {
                auto optArg = optimizeNode(*arg, stats);
                if (const auto* num = asNumber(optArg.get())) {
                    constValues.push_back(num->value());
                } else {
                    allConst = false;
                }
                optArgs.push_back(std::move(optArg));
            }

            if (allConst && f.funcName() != "ndiff") {
                try {
                    const double folded = applyMathFunction(f.funcName(), constValues, f.span());
                    ++stats.foldsPerformed;
                    std::ostringstream log;
                    log << "Constant fold: " << f.funcName() << "(";
                    for (std::size_t i = 0; i < constValues.size(); ++i) {
                        if (i > 0) log << ", ";
                        log << formatCompactNumber(constValues[i]);
                    }
                    log << ") -> " << formatCompactNumber(folded);
                    stats.passLog.push_back(log.str());
                    return std::make_unique<NumberNode>(folded, f.span(), true);
                } catch (const CompilerException&) {
                }
            }

            return std::make_unique<FunctionCallNode>(f.funcName(), std::move(optArgs), f.span());
        }

        case ASTNodeKind::Assign: {
            const auto& a = static_cast<const AssignNode&>(node);
            return std::make_unique<AssignNode>(
                a.targetName(), a.targetSpan(), optimizeNode(a.rhs(), stats), a.span());
        }

        case ASTNodeKind::FunctionDef: {
            const auto& fd = static_cast<const FunctionDefNode&>(node);
            return std::make_unique<FunctionDefNode>(
                fd.funcName(), fd.funcSpan(), fd.params(), optimizeNode(fd.body(), stats), fd.span());
        }
    }

    return node.clone();
}

}  // namespace

std::size_t ASTNode::nodeCount() const {
    std::size_t total = 1;
    for (const ASTNode* child : childNodes()) {
        total += child->nodeCount();
    }
    return total;
}

std::size_t ASTNode::depth() const {
    std::size_t maxChildDepth = 0;
    for (const ASTNode* child : childNodes()) {
        maxChildDepth = std::max(maxChildDepth, child->depth());
    }
    return 1 + maxChildDepth;
}

std::string ASTNode::toTreeString(int indent) const {
    std::ostringstream oss;
    oss << std::string(static_cast<std::size_t>(indent) * 2, ' ')
        << kindName() << "(" << label() << ")";
    if (isFoldedConstant()) {
        oss << " [folded]";
    }
    oss << "\n";
    for (const ASTNode* child : childNodes()) {
        oss << child->toTreeString(indent + 1);
    }
    return oss.str();
}

std::string ASTNode::toInfixString(int parentPrecedence, bool isRightChild) const {
    (void)parentPrecedence;
    (void)isRightChild;
    switch (kind()) {
        case ASTNodeKind::Number:
            return static_cast<const NumberNode&>(*this).label();

        case ASTNodeKind::Variable:
            return static_cast<const VariableNode&>(*this).name();

        case ASTNodeKind::UnaryOp: {
            const auto& u = static_cast<const UnaryOpNode&>(*this);
            std::string inner = u.operand().toInfixString();
            if (u.operand().kind() == ASTNodeKind::BinaryOp) {
                const int innerPrec = binaryPrecedence(static_cast<const BinaryOpNode&>(u.operand()).op());
                if (innerPrec <= 3) {
                    inner = "(" + inner + ")";
                }
            } else if (u.operand().kind() == ASTNodeKind::UnaryOp) {
                inner = "(" + inner + ")";
            }
            return std::string(unaryOpSymbol(u.op())) + inner;
        }

        case ASTNodeKind::BinaryOp: {
            const auto& b = static_cast<const BinaryOpNode&>(*this);
            const int myPrec = binaryPrecedence(b.op());

            std::string leftStr = b.left().toInfixString();
            if (b.left().kind() == ASTNodeKind::BinaryOp) {
                const int lPrec = binaryPrecedence(static_cast<const BinaryOpNode&>(b.left()).op());
                if (lPrec < myPrec || (lPrec == myPrec && b.op() == BinaryOpKind::Pow)) {
                    leftStr = "(" + leftStr + ")";
                }
            } else if (b.left().kind() == ASTNodeKind::UnaryOp && b.op() == BinaryOpKind::Pow) {
                leftStr = "(" + leftStr + ")";
            }

            std::string rightStr = b.right().toInfixString();
            if (b.right().kind() == ASTNodeKind::BinaryOp) {
                const auto& rb = static_cast<const BinaryOpNode&>(b.right());
                if (needsRightParens(b.op(), rb.op())) {
                    rightStr = "(" + rightStr + ")";
                }
            } else if (b.right().kind() == ASTNodeKind::UnaryOp && myPrec >= 2) {
                // Keep e.g. `2 * (-x)` clear if needed? Actually `2 * -x` is valid, or keep clean!
            }

            return leftStr + " " + binaryOpSymbol(b.op()) + " " + rightStr;
        }

        case ASTNodeKind::FunctionCall: {
            const auto& f = static_cast<const FunctionCallNode&>(*this);
            std::ostringstream oss;
            oss << f.funcName() << "(";
            for (std::size_t i = 0; i < f.args().size(); ++i) {
                if (i > 0) oss << ", ";
                oss << f.args()[i]->toInfixString(-1, false);
            }
            oss << ")";
            return oss.str();
        }

        case ASTNodeKind::Assign: {
            const auto& a = static_cast<const AssignNode&>(*this);
            return a.targetName() + " = " + a.rhs().toInfixString(-1, false);
        }

        case ASTNodeKind::FunctionDef: {
            const auto& fd = static_cast<const FunctionDefNode&>(*this);
            std::ostringstream oss;
            oss << fd.funcName() << "(";
            for (std::size_t i = 0; i < fd.params().size(); ++i) {
                if (i > 0) oss << ", ";
                oss << fd.params()[i];
            }
            oss << ") = " << fd.body().toInfixString(-1, false);
            return oss.str();
        }
    }
    return {};
}

std::unique_ptr<ASTNode> NumberNode::clone() const {
    return std::make_unique<NumberNode>(value_, span_, folded_);
}

std::unique_ptr<ASTNode> VariableNode::clone() const {
    return std::make_unique<VariableNode>(name_, span_);
}

std::string UnaryOpNode::label() const {
    return op_ == UnaryOpKind::Negate ? "u-" : "u+";
}

std::vector<const ASTNode*> UnaryOpNode::childNodes() const {
    return {operand_.get()};
}

std::unique_ptr<ASTNode> UnaryOpNode::clone() const {
    return std::make_unique<UnaryOpNode>(op_, operand_->clone(), span_);
}

std::vector<const ASTNode*> BinaryOpNode::childNodes() const {
    return {left_.get(), right_.get()};
}

std::unique_ptr<ASTNode> BinaryOpNode::clone() const {
    return std::make_unique<BinaryOpNode>(op_, left_->clone(), right_->clone(), span_);
}

std::vector<const ASTNode*> FunctionCallNode::childNodes() const {
    std::vector<const ASTNode*> out;
    out.reserve(args_.size());
    for (const auto& arg : args_) {
        out.push_back(arg.get());
    }
    return out;
}

std::unique_ptr<ASTNode> FunctionCallNode::clone() const {
    std::vector<std::unique_ptr<ASTNode>> clonedArgs;
    clonedArgs.reserve(args_.size());
    for (const auto& arg : args_) {
        clonedArgs.push_back(arg->clone());
    }
    return std::make_unique<FunctionCallNode>(funcName_, std::move(clonedArgs), span_);
}

std::vector<const ASTNode*> AssignNode::childNodes() const {
    return {rhs_.get()};
}

std::unique_ptr<ASTNode> AssignNode::clone() const {
    return std::make_unique<AssignNode>(targetName_, targetSpan_, rhs_->clone(), span_);
}

std::string FunctionDefNode::label() const {
    std::ostringstream oss;
    oss << funcName_ << "(";
    for (std::size_t i = 0; i < params_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << params_[i];
    }
    oss << ") =";
    return oss.str();
}

std::vector<const ASTNode*> FunctionDefNode::childNodes() const {
    return {body_.get()};
}

std::unique_ptr<ASTNode> FunctionDefNode::clone() const {
    return std::make_unique<FunctionDefNode>(funcName_, funcSpan_, params_, body_->clone(), span_);
}

bool SymbolicDiff::referencesVariable(const ASTNode& node, std::string_view varName) {
    if (node.kind() == ASTNodeKind::Variable) {
        return static_cast<const VariableNode&>(node).name() == varName;
    }
    for (const ASTNode* child : node.childNodes()) {
        if (referencesVariable(*child, varName)) {
            return true;
        }
    }
    return false;
}

bool SymbolicDiff::containsDiffCall(const ASTNode& node) {
    if (node.kind() == ASTNodeKind::FunctionCall &&
        static_cast<const FunctionCallNode&>(node).funcName() == "diff") {
        return true;
    }
    for (const ASTNode* child : node.childNodes()) {
        if (containsDiffCall(*child)) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<ASTNode> SymbolicDiff::simplify(const ASTNode& expr) {
    auto cur = simplifyImpl(expr);
    // Second pass resolves newly exposed constants/identities (e.g. `2 * x^(2 - 1)` -> `2 * x`).
    return simplifyImpl(*cur);
}

std::unique_ptr<ASTNode> SymbolicDiff::differentiate(const ASTNode& expr, std::string_view variable) {
    auto raw = diffRaw(expr, variable);
    return simplify(*raw);
}

OptimizationResult ASTOptimizer::optimize(const ASTNode& root) {
    OptimizationResult result;
    result.rawNodeCount = root.nodeCount();
    result.optimizedRoot = optimizeNode(root, result);
    result.optimizedNodeCount = result.optimizedRoot ? result.optimizedRoot->nodeCount() : 0;
    return result;
}

}  // namespace mathcompiler
