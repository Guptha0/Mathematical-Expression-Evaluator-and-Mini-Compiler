#include "MathOps.hpp"

#include "CompilerException.hpp"
#include "FunctionRegistry.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace mathcompiler {

namespace {

double ensureFinite(double value, std::string_view context, const SourceSpan& span) {
    if (std::isnan(value)) {
        throw MathDomainException(
            std::string(context) + " produced an undefined (NaN) result",
            span);
    }
    if (!std::isfinite(value)) {
        throw RuntimeEvalException(
            "E_OVERFLOW",
            std::string(context) + " overflowed IEEE-754 double precision range",
            span,
            "Try smaller operand magnitudes.");
    }
    return value == 0.0 ? 0.0 : value;
}

}  // namespace

std::string formatCompactNumber(double value) {
    if (value == 0.0) {
        return "0";
    }
    std::ostringstream oss;
    oss << std::setprecision(12) << value;
    return oss.str();
}

double applyUnaryOp(UnaryOpKind op, double operand, const SourceSpan& span) {
    switch (op) {
        case UnaryOpKind::Negate: {
            double result = -operand;
            return ensureFinite(result, "Unary negation", span);
        }
        case UnaryOpKind::Plus:
            return ensureFinite(operand, "Unary plus", span);
    }
    return operand;
}

double applyBinaryOp(BinaryOpKind op, double lhs, double rhs, const SourceSpan& span) {
    switch (op) {
        case BinaryOpKind::Add:
            return ensureFinite(lhs + rhs, "Addition", span);
        case BinaryOpKind::Sub:
            return ensureFinite(lhs - rhs, "Subtraction", span);
        case BinaryOpKind::Mul:
            return ensureFinite(lhs * rhs, "Multiplication", span);
        case BinaryOpKind::Div:
            if (rhs == 0.0) {
                throw DivisionByZeroException("Division by zero is undefined", span);
            }
            return ensureFinite(lhs / rhs, "Division", span);
        case BinaryOpKind::Mod:
            if (rhs == 0.0) {
                throw DivisionByZeroException("Modulo by zero is undefined", span);
            }
            return ensureFinite(std::fmod(lhs, rhs), "Modulo", span);
        case BinaryOpKind::Pow:
            if (lhs == 0.0 && rhs < 0.0) {
                throw DivisionByZeroException(
                    "Zero raised to a negative exponent results in division by zero",
                    span);
            }
            if (lhs < 0.0 && std::floor(rhs) != rhs) {
                throw MathDomainException(
                    "Negative base (" + formatCompactNumber(lhs) +
                        ") raised to non-integer exponent (" + formatCompactNumber(rhs) +
                        ") is undefined in real numbers",
                    span,
                    "Use an integer exponent or a non-negative base.");
            }
            return ensureFinite(std::pow(lhs, rhs), "Exponentiation", span);
    }
    return 0.0;
}

double applyMathFunction(std::string_view name,
                         const std::vector<double>& args,
                         const SourceSpan& span) {
    auto spec = FunctionRegistry::find(name);
    if (!spec.has_value()) {
        throw UnknownFunctionException(std::string(name), span);
    }
    if (args.size() != spec->arity) {
        throw ArityMismatchException(std::string(name), spec->arity, args.size(), span);
    }

    const double x = args[0];
    if (name == "sin") {
        return ensureFinite(std::sin(x), "sin()", span);
    }
    if (name == "cos") {
        return ensureFinite(std::cos(x), "cos()", span);
    }
    if (name == "tan") {
        double res = std::tan(x);
        if (!std::isfinite(res)) {
            throw MathDomainException(
                "tan() asymptote / undefined value",
                span,
                "Avoid odd multiples of pi/2.");
        }
        return res == 0.0 ? 0.0 : res;
    }
    if (name == "sqrt") {
        if (x < 0.0) {
            throw MathDomainException(
                "Domain error in sqrt(" + formatCompactNumber(x) + "): argument cannot be negative",
                span,
                "Pass a value >= 0 to sqrt().");
        }
        return ensureFinite(std::sqrt(x), "sqrt()", span);
    }
    if (name == "log" || name == "ln") {
        if (x <= 0.0) {
            throw MathDomainException(
                "Domain error in " + std::string(name) + "(" + formatCompactNumber(x) +
                    "): logarithm requires argument > 0",
                span,
                "Pass a strictly positive value (> 0).");
        }
        return ensureFinite(std::log(x), std::string(name) + "()", span);
    }
    if (name == "log10") {
        if (x <= 0.0) {
            throw MathDomainException(
                "Domain error in log10(" + formatCompactNumber(x) +
                    "): logarithm requires argument > 0",
                span,
                "Pass a strictly positive value (> 0).");
        }
        return ensureFinite(std::log10(x), "log10()", span);
    }
    if (name == "exp") {
        double res = std::exp(x);
        if (!std::isfinite(res)) {
            throw RuntimeEvalException(
                "E_OVERFLOW",
                "Overflow in exp(" + formatCompactNumber(x) + "): result exceeds double range",
                span,
                "Keep exp() argument <= ~709.78.");
        }
        return res;
    }
    if (name == "pow") {
        return applyBinaryOp(BinaryOpKind::Pow, args[0], args[1], span);
    }
    if (name == "abs") {
        return ensureFinite(std::fabs(x), "abs()", span);
    }

    throw UnknownFunctionException(std::string(name), span);
}

}  // namespace mathcompiler
