#pragma once

#include "AST.hpp"
#include "Token.hpp"

#include <memory>
#include <string>
#include <vector>

namespace mathcompiler {

/// Snapshot of one Shunting-Yard step for educational visualization & debugging.
struct ShuntingYardStep {
    std::size_t stepIndex = 0;
    Token currentToken;
    std::string action;
    std::vector<std::string> operatorStack;  // bottom -> top
    std::vector<std::string> outputQueue;    // left -> right
};

struct ParsedStatement {
    std::vector<Token> infixTokens;
    std::vector<Token> rpnTokens;
    std::vector<ShuntingYardStep> shuntingYardSteps;
    std::unique_ptr<ASTNode> ast;
    SourceSpan span{};
};

/// Infix-to-Postfix (Shunting-Yard) parser and AST builder supporting user-defined functions.
class Parser {
public:
    /// Splits a full token stream into logical statements separated by `;` or balanced newlines.
    [[nodiscard]] static std::vector<std::vector<Token>> splitStatements(const std::vector<Token>& tokens);

    /// Parses a single statement's token slice into RPN tokens, step snapshots, and an AST.
    [[nodiscard]] ParsedStatement parseStatement(const std::vector<Token>& statementTokens) const;

    /// Convenience helper: splits `tokens` into statements and parses each into RPN + AST.
    [[nodiscard]] std::vector<ParsedStatement> parseProgram(const std::vector<Token>& tokens) const;
};

}  // namespace mathcompiler
