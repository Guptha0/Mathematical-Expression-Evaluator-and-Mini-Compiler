#pragma once

#include "SourceLocation.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace mathcompiler {

enum class ErrorPhase {
    Lexical,
    Syntax,
    Semantic,
    Runtime
};

[[nodiscard]] std::string errorPhaseToString(ErrorPhase phase);

/// Represents a stack-unwinding diagnostic frame recorded as compiler/VM scopes unwind.
struct DiagnosticFrame {
    std::string operation;
    SourceSpan span;
};

/// Base exception class for all lexical, syntax, semantic, and runtime evaluation errors.
class CompilerException : public std::runtime_error {
public:
    CompilerException(ErrorPhase phase,
                      std::string errorCode,
                      std::string message,
                      SourceSpan span,
                      std::string hint = {},
                      std::string sourceText = {});

    [[nodiscard]] ErrorPhase phase() const noexcept { return phase_; }
    [[nodiscard]] const std::string& errorCode() const noexcept { return errorCode_; }
    [[nodiscard]] const std::string& detailMessage() const noexcept { return message_; }
    [[nodiscard]] const SourceSpan& span() const noexcept { return span_; }
    [[nodiscard]] const std::string& hint() const noexcept { return hint_; }
    [[nodiscard]] const std::string& sourceText() const noexcept { return sourceText_; }
    [[nodiscard]] const std::vector<DiagnosticFrame>& callStack() const noexcept { return callStack_; }

    void setSourceText(std::string sourceText);
    void addFrame(std::string operation, SourceSpan frameSpan);

    /// Produces a multi-line human-readable report including source caret and unwind trace.
    [[nodiscard]] std::string formatReport() const;

    /// Returns just the source line and caret pointer (`^~~~`) for console rendering.
    [[nodiscard]] std::string formatCaretBlock() const;

private:
    ErrorPhase phase_;
    std::string errorCode_;
    std::string message_;
    SourceSpan span_;
    std::string hint_;
    std::string sourceText_;
    std::vector<DiagnosticFrame> callStack_;
};

class LexicalException : public CompilerException {
public:
    LexicalException(std::string errorCode,
                     std::string message,
                     SourceSpan span,
                     std::string hint = {},
                     std::string sourceText = {});
};

class SyntaxException : public CompilerException {
public:
    SyntaxException(std::string errorCode,
                    std::string message,
                    SourceSpan span,
                    std::string hint = {},
                    std::string sourceText = {});
};

class UnbalancedParenthesesException : public SyntaxException {
public:
    UnbalancedParenthesesException(std::string message,
                                   SourceSpan span,
                                   std::string hint = {},
                                   std::string sourceText = {});
};

class SemanticException : public CompilerException {
public:
    SemanticException(std::string errorCode,
                      std::string message,
                      SourceSpan span,
                      std::string hint = {},
                      std::string sourceText = {});
};

class UndeclaredVariableException : public SemanticException {
public:
    UndeclaredVariableException(const std::string& variableName,
                                SourceSpan span,
                                std::string sourceText = {});
};

class UnknownFunctionException : public SemanticException {
public:
    UnknownFunctionException(const std::string& functionName,
                             SourceSpan span,
                             std::string sourceText = {});
};

class ArityMismatchException : public SemanticException {
public:
    ArityMismatchException(const std::string& functionName,
                           std::size_t expectedArgs,
                           std::size_t actualArgs,
                           SourceSpan span,
                           std::string sourceText = {});
};

class RuntimeEvalException : public CompilerException {
public:
    RuntimeEvalException(std::string errorCode,
                         std::string message,
                         SourceSpan span,
                         std::string hint = {},
                         std::string sourceText = {});
};

class DivisionByZeroException : public RuntimeEvalException {
public:
    DivisionByZeroException(std::string message,
                            SourceSpan span,
                            std::string sourceText = {});
};

class MathDomainException : public RuntimeEvalException {
public:
    MathDomainException(std::string message,
                        SourceSpan span,
                        std::string hint = {},
                        std::string sourceText = {});
};

}  // namespace mathcompiler
