#pragma once

#include "AST.hpp"
#include "SymbolTable.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace mathcompiler {

struct EvalStep {
    std::size_t stepIndex = 0;
    int depth = 0;
    std::string operation;
    double value = 0.0;
    SourceSpan span{};
};

struct EvaluationOutput {
    double value = 0.0;
    std::vector<EvalStep> trace;
    bool wasAssignment = false;
    bool wasFunctionDef = false;
    std::string assignedSymbol;
    std::string symbolicExpression;
};

/// Recursive AST-walking evaluator with step trace, user function invocation, and stack unwinding.
class Evaluator {
public:
    static constexpr std::size_t kMaxCallDepth = 32;

    [[nodiscard]] EvaluationOutput evaluate(
        const ASTNode& root,
        SymbolTable& symbols,
        const std::unordered_map<std::string, double>* locals = nullptr,
        std::size_t callDepth = 0) const;
};

}  // namespace mathcompiler
