#pragma once

#include "Token.hpp"

#include <string_view>
#include <vector>

namespace mathcompiler {

/// Streaming lexical analyzer supporting decimal, scientific, hex (0x), binary (0b),
/// multi-character identifiers, operators, parentheses, and exact position tracking.
class Lexer {
public:
    /// Tokenizes the input source, throwing LexicalException on malformed literals or unknown characters.
    [[nodiscard]] std::vector<Token> tokenize(std::string_view source) const;

    /// Non-throwing tokenization for live UI syntax highlighting (preserves malformed spans as TokenType::Invalid).
    [[nodiscard]] std::vector<Token> tokenizeForHighlighting(std::string_view source) const noexcept;
};

}  // namespace mathcompiler
