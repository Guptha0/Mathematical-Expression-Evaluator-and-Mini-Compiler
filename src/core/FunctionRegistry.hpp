#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace mathcompiler {

struct FunctionSpec {
    std::string_view name;
    std::size_t arity;
    bool isSpecialForm;
    std::string_view signature;
    std::string_view description;
};

/// Single source of truth for built-in mathematical and symbolic calculus functions.
class FunctionRegistry {
public:
    static constexpr std::array<FunctionSpec, 12> kBuiltInFunctions = {{
        {"sin",   1, false, "sin(x)",          "Sine of x (radians)"},
        {"cos",   1, false, "cos(x)",          "Cosine of x (radians)"},
        {"tan",   1, false, "tan(x)",          "Tangent of x (radians)"},
        {"sqrt",  1, false, "sqrt(x)",         "Principal square root (x >= 0)"},
        {"exp",   1, false, "exp(x)",          "Exponential e^x"},
        {"ln",    1, false, "ln(x)",           "Natural logarithm (x > 0)"},
        {"log",   1, false, "log(x)",          "Natural logarithm (x > 0)"},
        {"log10", 1, false, "log10(x)",        "Base-10 logarithm (x > 0)"},
        {"pow",   2, false, "pow(a, b)",       "Power a^b"},
        {"abs",   1, false, "abs(x)",          "Absolute value |x|"},
        {"diff",  2, true,  "diff(expr, var)", "Symbolic derivative d(expr)/d(var)"},
        {"ndiff", 3, true,  "ndiff(expr, var, x)", "Numerical central-difference derivative at x"},
    }};

    [[nodiscard]] static std::optional<FunctionSpec> find(std::string_view name) noexcept {
        for (const auto& spec : kBuiltInFunctions) {
            if (spec.name == name) {
                return spec;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static bool isBuiltIn(std::string_view name) noexcept {
        return find(name).has_value();
    }
};

}  // namespace mathcompiler
