#pragma once

#include "AST.hpp"
#include "BytecodeVM.hpp"
#include "CompilerException.hpp"
#include "Evaluator.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "SymbolTable.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mathcompiler {

enum class NumberFormatMode {
    Auto,
    Fixed,
    Scientific,
    Fraction
};

struct ProfilerMetrics {
    double lexUs = 0.0;
    double parseUs = 0.0;
    double optimizeUs = 0.0;
    double bytecodeCompileUs = 0.0;
    double vmExecUs = 0.0;
    double astWalkExecUs = 0.0;
    double totalUs = 0.0;
};

struct DiagnosticSnapshot {
    ErrorPhase phase = ErrorPhase::Syntax;
    std::string errorCode;
    std::string message;
    std::string hint;
    SourceSpan span{};
    std::string caretBlock;
    std::string fullReport;
    std::vector<DiagnosticFrame> callStack;
};

struct StatementArtifact {
    std::size_t statementIndex = 0;
    std::vector<Token> infixTokens;
    std::vector<Token> rpnTokens;
    std::vector<ShuntingYardStep> shuntingYardSteps;
    std::unique_ptr<ASTNode> rawAst;
    std::unique_ptr<ASTNode> optimizedAst;
    std::size_t rawNodeCount = 0;
    std::size_t optimizedNodeCount = 0;
    std::size_t foldsPerformed = 0;
    std::vector<std::string> optimizationLog;
    BytecodeProgram bytecode;
    EvaluationOutput evalOutput;
    double vmValue = 0.0;
    bool eliminatedAsDeadCode = false;
    bool hasSymbolicDiff = false;
    std::string symbolicForm;
};

struct CompilationResult {
    bool success = false;
    bool emptyInput = false;
    bool hasNumericValue = false;
    std::string source;
    double finalValue = 0.0;
    double vmFinalValue = 0.0;
    std::string symbolicResult;
    std::string statusNote;
    ProfilerMetrics profile{};
    std::vector<Token> allTokens;
    std::vector<StatementArtifact> statements;
    std::vector<std::string> deadCodeLog;
    std::optional<DiagnosticSnapshot> error;
};

/// Facade coordinating Lexing, Shunting-Yard Parsing, AST Optimization & Symbolic Diff,
/// Bytecode Compilation, Stack VM Execution, AST-Walking Verification, and Microsecond Profiling.
class CompilerEngine {
public:
    CompilerEngine() = default;

    /// Compiles and executes `source`. Symbol table mutations commit atomically on success.
    [[nodiscard]] CompilationResult compileAndEvaluate(std::string_view source,
                                                       bool enableOptimization = true);

    /// Compiles and executes `source` directly against a caller-supplied SymbolTable.
    [[nodiscard]] CompilationResult compileAndEvaluateWithSymbols(std::string_view source,
                                                                  SymbolTable& externalSymbols,
                                                                  bool enableOptimization = true) const;

    [[nodiscard]] SymbolTable& symbolTable() noexcept { return symbols_; }
    [[nodiscard]] const SymbolTable& symbolTable() const noexcept { return symbols_; }

    /// Formats a computed numeric result using Auto, Fixed, Scientific, or continued-fraction mode.
    [[nodiscard]] static std::string formatResult(double value,
                                                  NumberFormatMode mode,
                                                  int precision = 6);

private:
    Lexer lexer_;
    Parser parser_;
    Evaluator evaluator_;
    BytecodeVM vm_;
    SymbolTable symbols_;
};

}  // namespace mathcompiler
