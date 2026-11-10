#pragma once

#include "SourceLocation.hpp"

#include <string>

namespace mathcompiler {

enum class TokenType {
    Number,
    Identifier,
    Function,
    Plus,
    Minus,
    Multiply,
    Divide,
    Power,
    Modulo,
    UnaryMinus,
    UnaryPlus,
    Assign,
    LeftParen,
    RightParen,
    Comma,
    Semicolon,
    Newline,
    Invalid,
    EndOfInput
};

[[nodiscard]] inline const char* tokenTypeName(TokenType type) noexcept {
    switch (type) {
        case TokenType::Number:     return "Number";
        case TokenType::Identifier: return "Identifier";
        case TokenType::Function:   return "Function";
        case TokenType::Plus:       return "Plus";
        case TokenType::Minus:      return "Minus";
        case TokenType::Multiply:   return "Multiply";
        case TokenType::Divide:     return "Divide";
        case TokenType::Power:      return "Power";
        case TokenType::Modulo:     return "Modulo";
        case TokenType::UnaryMinus: return "UnaryMinus";
        case TokenType::UnaryPlus:  return "UnaryPlus";
        case TokenType::Assign:     return "Assign";
        case TokenType::LeftParen:  return "LeftParen";
        case TokenType::RightParen: return "RightParen";
        case TokenType::Comma:      return "Comma";
        case TokenType::Semicolon:  return "Semicolon";
        case TokenType::Newline:    return "Newline";
        case TokenType::Invalid:    return "Invalid";
        case TokenType::EndOfInput: return "EndOfInput";
    }
    return "Unknown";
}

struct Token {
    TokenType type = TokenType::EndOfInput;
    std::string lexeme;
    double numericValue = 0.0;
    SourceSpan span{};

    [[nodiscard]] bool isOperator() const noexcept {
        switch (type) {
            case TokenType::Plus:
            case TokenType::Minus:
            case TokenType::Multiply:
            case TokenType::Divide:
            case TokenType::Power:
            case TokenType::Modulo:
            case TokenType::UnaryMinus:
            case TokenType::UnaryPlus:
            case TokenType::Assign:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] bool isBinaryOperator() const noexcept {
        switch (type) {
            case TokenType::Plus:
            case TokenType::Minus:
            case TokenType::Multiply:
            case TokenType::Divide:
            case TokenType::Power:
            case TokenType::Modulo:
            case TokenType::Assign:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] bool isUnaryOperator() const noexcept {
        return type == TokenType::UnaryMinus || type == TokenType::UnaryPlus;
    }

    /// Operator precedence: '=' (0) < '+,-' (1) < '*,/,%' (2) < 'u-,u+' (3) < '^' (4).
    [[nodiscard]] int precedence() const noexcept {
        switch (type) {
            case TokenType::Assign:     return 0;
            case TokenType::Plus:
            case TokenType::Minus:      return 1;
            case TokenType::Multiply:
            case TokenType::Divide:
            case TokenType::Modulo:     return 2;
            case TokenType::UnaryMinus:
            case TokenType::UnaryPlus:  return 3;
            case TokenType::Power:      return 4;
            default:                    return -1;
        }
    }

    [[nodiscard]] bool isRightAssociative() const noexcept {
        return type == TokenType::Power ||
               type == TokenType::UnaryMinus ||
               type == TokenType::UnaryPlus ||
               type == TokenType::Assign;
    }

    [[nodiscard]] std::string displayLexeme() const {
        if (type == TokenType::UnaryMinus) return "u-";
        if (type == TokenType::UnaryPlus)  return "u+";
        if (type == TokenType::Newline)    return "\\n";
        return lexeme;
    }
};

}  // namespace mathcompiler
