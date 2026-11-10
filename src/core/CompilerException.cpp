#include "CompilerException.hpp"
#include "FunctionRegistry.hpp"

#include <sstream>
#include <utility>

namespace mathcompiler {

std::string errorPhaseToString(ErrorPhase phase) {
    switch (phase) {
        case ErrorPhase::Lexical:  return "Lexical";
        case ErrorPhase::Syntax:   return "Syntax";
        case ErrorPhase::Semantic: return "Semantic";
        case ErrorPhase::Runtime:  return "Runtime";
    }
    return "Unknown";
}

namespace {

std::string buildWhatString(ErrorPhase phase,
                            const std::string& errorCode,
                            const std::string& message,
                            const SourceSpan& span) {
    std::ostringstream oss;
    oss << "[" << errorPhaseToString(phase) << ":" << errorCode << "] "
        << "(Line " << span.line << ", Col " << span.column << "): " << message;
    return oss.str();
}

}  // namespace

CompilerException::CompilerException(ErrorPhase phase,
                                     std::string errorCode,
                                     std::string message,
                                     SourceSpan span,
                                     std::string hint,
                                     std::string sourceText)
    : std::runtime_error(buildWhatString(phase, errorCode, message, span)),
      phase_(phase),
      errorCode_(std::move(errorCode)),
      message_(std::move(message)),
      span_(span),
      hint_(std::move(hint)),
      sourceText_(std::move(sourceText)) {}

void CompilerException::setSourceText(std::string sourceText) {
    sourceText_ = std::move(sourceText);
}

void CompilerException::addFrame(std::string operation, SourceSpan frameSpan) {
    if (callStack_.size() < 16) {
        callStack_.push_back(DiagnosticFrame{std::move(operation), frameSpan});
    }
}

std::string CompilerException::formatCaretBlock() const {
    return formatCaretDiagnostic(sourceText_, span_);
}

std::string CompilerException::formatReport() const {
    std::ostringstream oss;
    oss << "[" << errorPhaseToString(phase_) << "Error / " << errorCode_ << "] "
        << "Line " << span_.line << ", Col " << span_.column << ": " << message_ << "\n";

    if (!sourceText_.empty()) {
        oss << formatCaretBlock() << "\n";
    }

    if (!hint_.empty()) {
        oss << "  Hint: " << hint_ << "\n";
    }

    if (!callStack_.empty()) {
        oss << "  Unwind Trace:\n";
        for (std::size_t i = 0; i < callStack_.size(); ++i) {
            const auto& frame = callStack_[i];
            oss << "    #" << i << " " << frame.operation
                << " (Line " << frame.span.line << ", Col " << frame.span.column << ")\n";
        }
    }
    return oss.str();
}

LexicalException::LexicalException(std::string errorCode,
                                   std::string message,
                                   SourceSpan span,
                                   std::string hint,
                                   std::string sourceText)
    : CompilerException(ErrorPhase::Lexical,
                        std::move(errorCode),
                        std::move(message),
                        span,
                        std::move(hint),
                        std::move(sourceText)) {}

SyntaxException::SyntaxException(std::string errorCode,
                                 std::string message,
                                 SourceSpan span,
                                 std::string hint,
                                 std::string sourceText)
    : CompilerException(ErrorPhase::Syntax,
                        std::move(errorCode),
                        std::move(message),
                        span,
                        std::move(hint),
                        std::move(sourceText)) {}

UnbalancedParenthesesException::UnbalancedParenthesesException(std::string message,
                                                               SourceSpan span,
                                                               std::string hint,
                                                               std::string sourceText)
    : SyntaxException("E_UNBALANCED_PARENS",
                      std::move(message),
                      span,
                      std::move(hint),
                      std::move(sourceText)) {}

SemanticException::SemanticException(std::string errorCode,
                                     std::string message,
                                     SourceSpan span,
                                     std::string hint,
                                     std::string sourceText)
    : CompilerException(ErrorPhase::Semantic,
                        std::move(errorCode),
                        std::move(message),
                        span,
                        std::move(hint),
                        std::move(sourceText)) {}

UndeclaredVariableException::UndeclaredVariableException(const std::string& variableName,
                                                         SourceSpan span,
                                                         std::string sourceText)
    : SemanticException("E_UNDECLARED_VAR",
                        "Undeclared variable '" + variableName + "'",
                        span,
                        FunctionRegistry::isBuiltIn(variableName)
                            ? ("'" + variableName + "' is a built-in function — did you forget parentheses, e.g. '" + variableName + "(x)'?")
                            : ("Define '" + variableName + " = <value>' or add it in the Variables panel."),
                        std::move(sourceText)) {}

UnknownFunctionException::UnknownFunctionException(const std::string& functionName,
                                                   SourceSpan span,
                                                   std::string sourceText)
    : SemanticException("E_UNKNOWN_FUNC",
                        "Unknown mathematical function '" + functionName + "()'",
                        span,
                        "Supported built-ins: sin, cos, tan, sqrt, exp, ln, log, log10, pow, abs, diff, ndiff — or define your own like f(x) = x^2.",
                        std::move(sourceText)) {}

ArityMismatchException::ArityMismatchException(const std::string& functionName,
                                               std::size_t expectedArgs,
                                               std::size_t actualArgs,
                                               SourceSpan span,
                                               std::string sourceText)
    : SemanticException("E_ARITY_MISMATCH",
                        "Function '" + functionName + "()' expects " +
                            std::to_string(expectedArgs) + " argument(s), but got " +
                            std::to_string(actualArgs),
                        span,
                        "Check comma-separated arguments inside '" + functionName + "(...)'.",
                        std::move(sourceText)) {}

RuntimeEvalException::RuntimeEvalException(std::string errorCode,
                                           std::string message,
                                           SourceSpan span,
                                           std::string hint,
                                           std::string sourceText)
    : CompilerException(ErrorPhase::Runtime,
                        std::move(errorCode),
                        std::move(message),
                        span,
                        std::move(hint),
                        std::move(sourceText)) {}

DivisionByZeroException::DivisionByZeroException(std::string message,
                                                 SourceSpan span,
                                                 std::string sourceText)
    : RuntimeEvalException("E_DIV_ZERO",
                           std::move(message),
                           span,
                           "Ensure the divisor evaluates to a non-zero value.",
                           std::move(sourceText)) {}

MathDomainException::MathDomainException(std::string message,
                                         SourceSpan span,
                                         std::string hint,
                                         std::string sourceText)
    : RuntimeEvalException("E_MATH_DOMAIN",
                           std::move(message),
                           span,
                           std::move(hint),
                           std::move(sourceText)) {}

}  // namespace mathcompiler
