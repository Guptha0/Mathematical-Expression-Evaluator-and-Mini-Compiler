#include "Evaluator.hpp"

#include "CompilerException.hpp"
#include "FunctionRegistry.hpp"
#include "MathOps.hpp"

#include <cmath>
#include <sstream>

namespace mathcompiler {

namespace {

double evalNodeRecursive(const ASTNode& node,
                         SymbolTable& symbols,
                         const std::unordered_map<std::string, double>* locals,
                         std::vector<EvalStep>& trace,
                         int depth,
                         std::size_t callDepth) {
    if (callDepth > Evaluator::kMaxCallDepth) {
        throw RuntimeEvalException(
            "E_STACK_OVERFLOW",
            "Maximum function call recursion depth exceeded",
            node.span(),
            "Check for cyclic user function definitions (e.g. f(x) = f(x)).");
    }

    switch (node.kind()) {
        case ASTNodeKind::Number: {
            const auto& num = static_cast<const NumberNode&>(node);
            std::string op = num.isFoldedConstant()
                                 ? ("Folded constant " + formatCompactNumber(num.value()))
                                 : ("Literal " + formatCompactNumber(num.value()));
            trace.push_back(EvalStep{trace.size() + 1, depth, std::move(op), num.value(), num.span()});
            return num.value();
        }

        case ASTNodeKind::Variable: {
            const auto& var = static_cast<const VariableNode&>(node);
            if (locals) {
                auto it = locals->find(var.name());
                if (it != locals->end()) {
                    trace.push_back(EvalStep{
                        trace.size() + 1, depth, "Param '" + var.name() + "'", it->second, var.span()});
                    return it->second;
                }
            }
            auto val = symbols.get(var.name());
            if (!val.has_value()) {
                throw UndeclaredVariableException(var.name(), var.span());
            }
            trace.push_back(EvalStep{
                trace.size() + 1, depth, "Load '" + var.name() + "'", *val, var.span()});
            return *val;
        }

        case ASTNodeKind::UnaryOp: {
            const auto& u = static_cast<const UnaryOpNode&>(node);
            try {
                const double operandVal =
                    evalNodeRecursive(u.operand(), symbols, locals, trace, depth + 1, callDepth);
                const double res = applyUnaryOp(u.op(), operandVal, u.span());
                trace.push_back(EvalStep{
                    trace.size() + 1,
                    depth,
                    std::string("Unary ") + unaryOpSymbol(u.op()) + "(" + formatCompactNumber(operandVal) + ")",
                    res,
                    u.span()});
                return res;
            } catch (CompilerException& ex) {
                ex.addFrame(std::string("Evaluate UnaryOp '") + unaryOpSymbol(u.op()) + "'", u.span());
                throw;
            }
        }

        case ASTNodeKind::BinaryOp: {
            const auto& b = static_cast<const BinaryOpNode&>(node);
            try {
                const double lhs =
                    evalNodeRecursive(b.left(), symbols, locals, trace, depth + 1, callDepth);
                const double rhs =
                    evalNodeRecursive(b.right(), symbols, locals, trace, depth + 1, callDepth);
                const double res = applyBinaryOp(b.op(), lhs, rhs, b.span());
                trace.push_back(EvalStep{
                    trace.size() + 1,
                    depth,
                    formatCompactNumber(lhs) + " " + binaryOpSymbol(b.op()) + " " + formatCompactNumber(rhs),
                    res,
                    b.span()});
                return res;
            } catch (CompilerException& ex) {
                ex.addFrame(std::string("Evaluate BinaryOp '") + binaryOpSymbol(b.op()) + "'", b.span());
                throw;
            }
        }

        case ASTNodeKind::FunctionCall: {
            const auto& f = static_cast<const FunctionCallNode&>(node);
            try {
                if (f.funcName() == "diff") {
                    if (f.args().size() != 2) {
                        throw ArityMismatchException("diff", 2, f.args().size(), f.span());
                    }
                    if (f.args()[1]->kind() != ASTNodeKind::Variable) {
                        throw SemanticException(
                            "E_SYMBOLIC_DIFF",
                            "Second argument of diff(expr, var) must be a variable name",
                            f.args()[1]->span());
                    }
                    const auto& varName = static_cast<const VariableNode&>(*f.args()[1]).name();
                    auto derivAst = SymbolicDiff::differentiate(*f.args()[0], varName);
                    const double res =
                        evalNodeRecursive(*derivAst, symbols, locals, trace, depth + 1, callDepth);
                    trace.push_back(EvalStep{
                        trace.size() + 1,
                        depth,
                        "Symbolic diff -> " + derivAst->toInfixString(),
                        res,
                        f.span()});
                    return res;
                }

                if (f.funcName() == "ndiff") {
                    if (f.args().size() != 3) {
                        throw ArityMismatchException("ndiff", 3, f.args().size(), f.span());
                    }
                    if (f.args()[1]->kind() != ASTNodeKind::Variable) {
                        throw SemanticException(
                            "E_SYMBOLIC_DIFF",
                            "Second argument of ndiff(expr, var, at) must be a variable name",
                            f.args()[1]->span());
                    }
                    const auto& varName = static_cast<const VariableNode&>(*f.args()[1]).name();
                    const double x0 =
                        evalNodeRecursive(*f.args()[2], symbols, locals, trace, depth + 1, callDepth);
                    const double h = 1e-6 * std::max(1.0, std::fabs(x0));

                    std::unordered_map<std::string, double> framePlus = locals ? *locals : std::unordered_map<std::string, double>{};
                    framePlus[varName] = x0 + h;
                    std::unordered_map<std::string, double> frameMinus = locals ? *locals : std::unordered_map<std::string, double>{};
                    frameMinus[varName] = x0 - h;

                    std::vector<EvalStep> scratchTrace;
                    const double fPlus =
                        evalNodeRecursive(*f.args()[0], symbols, &framePlus, scratchTrace, depth + 1, callDepth + 1);
                    const double fMinus =
                        evalNodeRecursive(*f.args()[0], symbols, &frameMinus, scratchTrace, depth + 1, callDepth + 1);
                    double deriv = (fPlus - fMinus) / (2.0 * h);
                    if (!std::isfinite(deriv)) {
                        throw MathDomainException("Numerical derivative diverged at evaluation point", f.span());
                    }
                    if (deriv == 0.0) deriv = 0.0;
                    trace.push_back(EvalStep{
                        trace.size() + 1,
                        depth,
                        "ndiff(..., " + varName + ", " + formatCompactNumber(x0) + ")",
                        deriv,
                        f.span()});
                    return deriv;
                }

                std::vector<double> argVals;
                argVals.reserve(f.args().size());
                for (const auto& arg : f.args()) {
                    argVals.push_back(
                        evalNodeRecursive(*arg, symbols, locals, trace, depth + 1, callDepth));
                }

                double res = 0.0;
                if (FunctionRegistry::isBuiltIn(f.funcName())) {
                    res = applyMathFunction(f.funcName(), argVals, f.span());
                } else {
                    auto userFn = symbols.getFunction(f.funcName());
                    if (!userFn.has_value()) {
                        throw UnknownFunctionException(f.funcName(), f.span());
                    }
                    if (argVals.size() != userFn->params.size()) {
                        throw ArityMismatchException(
                            f.funcName(), userFn->params.size(), argVals.size(), f.span());
                    }
                    std::unordered_map<std::string, double> fnLocals;
                    fnLocals.reserve(userFn->params.size());
                    for (std::size_t i = 0; i < userFn->params.size(); ++i) {
                        fnLocals[userFn->params[i]] = argVals[i];
                    }
                    res = evalNodeRecursive(
                        *userFn->bodyAst, symbols, &fnLocals, trace, depth + 1, callDepth + 1);
                }

                std::ostringstream op;
                op << f.funcName() << "(";
                for (std::size_t i = 0; i < argVals.size(); ++i) {
                    if (i > 0) op << ", ";
                    op << formatCompactNumber(argVals[i]);
                }
                op << ")";
                trace.push_back(EvalStep{trace.size() + 1, depth, op.str(), res, f.span()});
                return res;
            } catch (CompilerException& ex) {
                ex.addFrame("Evaluate Function '" + f.funcName() + "()'", f.span());
                throw;
            }
        }

        case ASTNodeKind::Assign: {
            const auto& a = static_cast<const AssignNode&>(node);
            try {
                const double val =
                    evalNodeRecursive(a.rhs(), symbols, locals, trace, depth + 1, callDepth);
                if (!symbols.set(a.targetName(), val)) {
                    throw SemanticException(
                        "E_RESERVED_IDENTIFIER",
                        "Cannot assign to '" + a.targetName() + "'",
                        a.targetSpan());
                }
                trace.push_back(EvalStep{
                    trace.size() + 1,
                    depth,
                    "Assign " + a.targetName() + " = " + formatCompactNumber(val),
                    val,
                    a.span()});
                return val;
            } catch (CompilerException& ex) {
                ex.addFrame("Evaluate Assignment '" + a.targetName() + " = ...'", a.span());
                throw;
            }
        }

        case ASTNodeKind::FunctionDef: {
            const auto& fd = static_cast<const FunctionDefNode&>(node);
            if (!symbols.setFunction(fd.funcName(), fd.params(), fd.body().clone())) {
                throw SemanticException(
                    "E_RESERVED_IDENTIFIER",
                    "Cannot redefine built-in function '" + fd.funcName() + "()'",
                    fd.funcSpan());
            }
            trace.push_back(EvalStep{
                trace.size() + 1,
                depth,
                "Define " + fd.label() + " " + fd.body().toInfixString(),
                0.0,
                fd.span()});
            return 0.0;
        }
    }
    return 0.0;
}

}  // namespace

EvaluationOutput Evaluator::evaluate(const ASTNode& root,
                                     SymbolTable& symbols,
                                     const std::unordered_map<std::string, double>* locals,
                                     std::size_t callDepth) const {
    EvaluationOutput out;
    if (root.kind() == ASTNodeKind::Assign) {
        out.wasAssignment = true;
        out.assignedSymbol = static_cast<const AssignNode&>(root).targetName();
    } else if (root.kind() == ASTNodeKind::FunctionDef) {
        out.wasFunctionDef = true;
        out.assignedSymbol = static_cast<const FunctionDefNode&>(root).funcName();
        out.symbolicExpression = static_cast<const FunctionDefNode&>(root).toInfixString();
    }
    out.value = evalNodeRecursive(root, symbols, locals, out.trace, 0, callDepth);
    return out;
}

}  // namespace mathcompiler
