#pragma once

#include "SourceLocation.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace mathcompiler {

enum class UnaryOpKind {
    Negate,
    Plus
};

enum class BinaryOpKind {
    Add,
    Sub,
    Mul,
    Div,
    Pow,
    Mod
};

[[nodiscard]] inline const char* unaryOpSymbol(UnaryOpKind op) noexcept {
    switch (op) {
        case UnaryOpKind::Negate: return "-";
        case UnaryOpKind::Plus:   return "+";
    }
    return "?";
}

[[nodiscard]] inline const char* binaryOpSymbol(BinaryOpKind op) noexcept {
    switch (op) {
        case BinaryOpKind::Add: return "+";
        case BinaryOpKind::Sub: return "-";
        case BinaryOpKind::Mul: return "*";
        case BinaryOpKind::Div: return "/";
        case BinaryOpKind::Pow: return "^";
        case BinaryOpKind::Mod: return "%";
    }
    return "?";
}

/// Evaluates a unary operator, throwing RuntimeEvalException on overflow/NaN.
[[nodiscard]] double applyUnaryOp(UnaryOpKind op, double operand, const SourceSpan& span);

/// Evaluates a binary operator, throwing DivisionByZeroException, MathDomainException, or RuntimeEvalException on error.
[[nodiscard]] double applyBinaryOp(BinaryOpKind op, double lhs, double rhs, const SourceSpan& span);

/// Evaluates a standard built-in `<cmath>` function with arity, domain, and overflow checks.
[[nodiscard]] double applyMathFunction(std::string_view name,
                                         const std::vector<double>& args,
                                         const SourceSpan& span);

/// Formats a double concisely without trailing redundant zeros for labels and diagnostics.
[[nodiscard]] std::string formatCompactNumber(double value);

}  // namespace mathcompiler
