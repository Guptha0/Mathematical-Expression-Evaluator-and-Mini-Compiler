#include "Lexer.hpp"

#include "CompilerException.hpp"

#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <system_error>

namespace mathcompiler {

namespace {

bool isAsciiDigit(char ch) noexcept {
    return ch >= '0' && ch <= '9';
}

bool isHexDigit(char ch) noexcept {
    return isAsciiDigit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

bool isBinaryDigit(char ch) noexcept {
    return ch == '0' || ch == '1';
}

bool isIdentStart(char ch) noexcept {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

bool isIdentContinue(char ch) noexcept {
    return isIdentStart(ch) || isAsciiDigit(ch);
}

int hexDigitValue(char ch) noexcept {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    return 10 + (ch - 'A');
}

class Scanner {
public:
    explicit Scanner(std::string_view source) : source_(source) {}

    std::vector<Token> run(bool throwOnError) {
        std::vector<Token> tokens;
        tokens.reserve(source_.size() / 2 + 4);

        while (!atEnd()) {
            const char ch = peek();

            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\v' || ch == '\f') {
                advance();
                continue;
            }

            if (ch == '\n') {
                const SourceSpan span{line_, column_, pos_, 1};
                advance();
                if (tokens.empty() || tokens.back().type != TokenType::Newline) {
                    tokens.push_back(Token{TokenType::Newline, "\\n", 0.0, span});
                }
                continue;
            }

            if (isAsciiDigit(ch) || (ch == '.' && pos_ + 1 < source_.size() && isAsciiDigit(source_[pos_ + 1]))) {
                auto tok = scanNumber(throwOnError);
                if (tok.has_value()) {
                    tokens.push_back(std::move(*tok));
                }
                continue;
            }

            if (isIdentStart(ch)) {
                tokens.push_back(scanIdentifier());
                continue;
            }

            const SourceSpan span{line_, column_, pos_, 1};
            TokenType singleType = TokenType::EndOfInput;
            switch (ch) {
                case '+': singleType = TokenType::Plus; break;
                case '-': singleType = TokenType::Minus; break;
                case '*': singleType = TokenType::Multiply; break;
                case '/': singleType = TokenType::Divide; break;
                case '^': singleType = TokenType::Power; break;
                case '%': singleType = TokenType::Modulo; break;
                case '=': singleType = TokenType::Assign; break;
                case '(': singleType = TokenType::LeftParen; break;
                case ')': singleType = TokenType::RightParen; break;
                case ',': singleType = TokenType::Comma; break;
                case ';': singleType = TokenType::Semicolon; break;
                default: break;
            }

            if (singleType != TokenType::EndOfInput) {
                std::string lex(1, ch);
                advance();
                tokens.push_back(Token{singleType, std::move(lex), 0.0, span});
                continue;
            }

            if (throwOnError) {
                std::string msg = "Unexpected character '";
                if (static_cast<unsigned char>(ch) < 32 || static_cast<unsigned char>(ch) == 127) {
                    msg += "<control>";
                } else {
                    msg.push_back(ch);
                }
                msg += "'";
                throw LexicalException(
                    "E_UNEXPECTED_CHAR",
                    std::move(msg),
                    span,
                    "Allowed tokens: numbers (dec/hex/bin), identifiers, +, -, *, /, ^, %, =, (, ), comma, ;",
                    std::string(source_));
            }
            std::string badLex(1, ch);
            advance();
            tokens.push_back(Token{TokenType::Invalid, std::move(badLex), 0.0, span});
        }

        tokens.push_back(Token{TokenType::EndOfInput, "", 0.0, SourceSpan{line_, column_, pos_, 1}});
        return tokens;
    }

private:
    [[nodiscard]] bool atEnd() const noexcept {
        return pos_ >= source_.size();
    }

    [[nodiscard]] char peek(std::size_t lookahead = 0) const noexcept {
        const std::size_t idx = pos_ + lookahead;
        return idx < source_.size() ? source_[idx] : '\0';
    }

    void advance() noexcept {
        if (atEnd()) return;
        const char ch = source_[pos_++];
        if (ch == '\n') {
            ++line_;
            column_ = 1;
        } else if (ch != '\r') {
            ++column_;
        }
    }

    std::optional<Token> scanRadixInteger(bool throwOnError,
                                          bool isHex,
                                          std::size_t startOffset,
                                          std::size_t startLine,
                                          std::size_t startCol) {
        advance();  // '0'
        advance();  // 'x'/'X' or 'b'/'B'

        const std::size_t digitsStart = pos_;
        double value = 0.0;
        const double base = isHex ? 16.0 : 2.0;

        while (!atEnd() && (isHex ? isHexDigit(peek()) : isBinaryDigit(peek()))) {
            value = value * base + static_cast<double>(hexDigitValue(peek()));
            advance();
        }

        if (pos_ == digitsStart) {
            if (throwOnError) {
                const SourceSpan span{startLine, startCol, startOffset, pos_ - startOffset};
                throw LexicalException(
                    isHex ? "E_BAD_HEX" : "E_BAD_BINARY",
                    isHex ? "Malformed hexadecimal literal: expected hex digits after '0x'"
                          : "Malformed binary literal: expected '0' or '1' after '0b'",
                    span,
                    isHex ? "Example: 0xFF, 0x1A" : "Example: 0b1010",
                    std::string(source_));
            }
            const std::size_t len = pos_ - startOffset;
            return Token{TokenType::Invalid, std::string(source_.substr(startOffset, len)), 0.0,
                         SourceSpan{startLine, startCol, startOffset, len}};
        }

        // Reject invalid trailing digits immediately following binary/hex literals (e.g. `0b102`, `0x1G`).
        if (!atEnd() && (isHex ? (isIdentStart(peek()) || peek() == '.') : (isAsciiDigit(peek()) || isIdentStart(peek()) || peek() == '.'))) {
            if (throwOnError) {
                const SourceSpan badCharSpan{line_, column_, pos_, 1};
                throw LexicalException(
                    isHex ? "E_BAD_HEX" : "E_BAD_BINARY",
                    std::string(isHex ? "Invalid character '" : "Invalid digit '") + peek() +
                        (isHex ? "' in hexadecimal literal" : "' in binary literal"),
                    badCharSpan,
                    isHex ? "Hex literals accept 0-9 and A-F." : "Binary literals accept only 0 and 1.",
                    std::string(source_));
            }
            while (!atEnd() && (isIdentContinue(peek()) || peek() == '.')) {
                advance();
            }
            const std::size_t len = pos_ - startOffset;
            return Token{TokenType::Invalid, std::string(source_.substr(startOffset, len)), 0.0,
                         SourceSpan{startLine, startCol, startOffset, len}};
        }

        const std::size_t len = pos_ - startOffset;
        const SourceSpan span{startLine, startCol, startOffset, len};
        if (!std::isfinite(value)) {
            if (throwOnError) {
                throw LexicalException("E_NUM_OVERFLOW", "Radix literal overflows double precision range", span);
            }
            return Token{TokenType::Invalid, std::string(source_.substr(startOffset, len)), 0.0, span};
        }

        return Token{TokenType::Number, std::string(source_.substr(startOffset, len)), value, span};
    }

    std::optional<Token> scanNumber(bool throwOnError) {
        const std::size_t startOffset = pos_;
        const std::size_t startLine = line_;
        const std::size_t startCol = column_;

        if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
            return scanRadixInteger(throwOnError, true, startOffset, startLine, startCol);
        }
        if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
            return scanRadixInteger(throwOnError, false, startOffset, startLine, startCol);
        }

        while (!atEnd() && isAsciiDigit(peek())) {
            advance();
        }

        if (!atEnd() && peek() == '.') {
            advance();
            while (!atEnd() && isAsciiDigit(peek())) {
                advance();
            }
            if (!atEnd() && peek() == '.') {
                if (throwOnError) {
                    const SourceSpan badDot{line_, column_, pos_, 1};
                    throw LexicalException(
                        "E_BAD_NUMBER",
                        "Malformed numeric literal: unexpected extra decimal point '.'",
                        badDot,
                        "A floating-point number may contain at most one decimal point.",
                        std::string(source_));
                }
                while (!atEnd() && (isAsciiDigit(peek()) || peek() == '.')) {
                    advance();
                }
                const std::size_t badLen = pos_ - startOffset;
                return Token{TokenType::Invalid, std::string(source_.substr(startOffset, badLen)), 0.0,
                             SourceSpan{startLine, startCol, startOffset, badLen}};
            }
        }

        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            const char next1 = peek(1);
            const char next2 = peek(2);
            if ((next1 == '+' || next1 == '-') && isAsciiDigit(next2)) {
                advance();
                advance();
                while (!atEnd() && isAsciiDigit(peek())) {
                    advance();
                }
            } else if (isAsciiDigit(next1)) {
                advance();
                while (!atEnd() && isAsciiDigit(peek())) {
                    advance();
                }
            } else if ((next1 == '+' || next1 == '-') && !isAsciiDigit(next2)) {
                if (throwOnError) {
                    const SourceSpan badExp{line_, column_, pos_, 2};
                    throw LexicalException(
                        "E_BAD_EXPONENT",
                        "Incomplete scientific notation exponent after '" +
                            std::string(1, peek()) + std::string(1, next1) + "'",
                        badExp,
                        "Provide exponent digits (e.g. '1.5e-3') or insert '*' if multiplying by 'e'.",
                        std::string(source_));
                }
                advance();
                advance();
                const std::size_t badLen = pos_ - startOffset;
                return Token{TokenType::Invalid, std::string(source_.substr(startOffset, badLen)), 0.0,
                             SourceSpan{startLine, startCol, startOffset, badLen}};
            }
        }

        const std::size_t len = pos_ - startOffset;
        const std::string_view lexView = source_.substr(startOffset, len);
        const SourceSpan span{startLine, startCol, startOffset, len};

        double parsedValue = 0.0;
        const char* first = lexView.data();
        const char* last = first + lexView.size();
        auto [ptr, ec] = std::from_chars(first, last, parsedValue);

        if (ec == std::errc::result_out_of_range) {
            if (throwOnError) {
                throw LexicalException(
                    "E_NUM_OVERFLOW",
                    "Numeric literal '" + std::string(lexView) + "' overflows double precision range",
                    span,
                    "Use a smaller magnitude within IEEE-754 double limits (~1.79e308).",
                    std::string(source_));
            }
            return Token{TokenType::Invalid, std::string(lexView), 0.0, span};
        }
        if (ec != std::errc{} || ptr != last) {
            if (throwOnError) {
                throw LexicalException(
                    "E_BAD_NUMBER",
                    "Failed to parse numeric literal '" + std::string(lexView) + "'",
                    span,
                    {},
                    std::string(source_));
            }
            return Token{TokenType::Invalid, std::string(lexView), 0.0, span};
        }

        return Token{TokenType::Number, std::string(lexView), parsedValue, span};
    }

    Token scanIdentifier() {
        const std::size_t startOffset = pos_;
        const std::size_t startLine = line_;
        const std::size_t startCol = column_;

        while (!atEnd() && isIdentContinue(peek())) {
            advance();
        }

        const std::size_t len = pos_ - startOffset;
        std::string lexeme(source_.substr(startOffset, len));
        const SourceSpan span{startLine, startCol, startOffset, len};
        return Token{TokenType::Identifier, std::move(lexeme), 0.0, span};
    }

    std::string_view source_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
};

}  // namespace

std::vector<Token> Lexer::tokenize(std::string_view source) const {
    Scanner scanner(source);
    return scanner.run(true);
}

std::vector<Token> Lexer::tokenizeForHighlighting(std::string_view source) const noexcept {
    try {
        Scanner scanner(source);
        return scanner.run(false);
    } catch (...) {
        return {};
    }
}

}  // namespace mathcompiler
