#pragma once

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>

namespace mathcompiler {

/// Represents a contiguous region in the input source string (1-based line and column indices).
struct SourceSpan {
    std::size_t line = 1;
    std::size_t column = 1;
    std::size_t offset = 0;
    std::size_t length = 1;

    /// Returns the 1-based inclusive end column on the starting line.
    [[nodiscard]] constexpr std::size_t endColumn() const noexcept {
        return length > 0 ? (column + length - 1) : column;
    }

    /// Merges two spans into a combined span covering both endpoints.
    [[nodiscard]] SourceSpan merge(const SourceSpan& other) const noexcept {
        if (offset <= other.offset) {
            const std::size_t endOffset = std::max(offset + length, other.offset + other.length);
            return SourceSpan{line, column, offset, endOffset - offset};
        }
        const std::size_t endOffset = std::max(offset + length, other.offset + other.length);
        return SourceSpan{other.line, other.column, other.offset, endOffset - other.offset};
    }

    [[nodiscard]] bool operator==(const SourceSpan& other) const noexcept = default;
};

/// Extracts the exact 1-based line text (without trailing CR/LF) from `source`.
[[nodiscard]] inline std::string extractLineFromSource(std::string_view source, std::size_t targetLine) {
    if (targetLine == 0) {
        return {};
    }

    std::size_t currentLine = 1;
    std::size_t lineStart = 0;
    for (std::size_t i = 0; i <= source.size(); ++i) {
        if (i == source.size() || source[i] == '\n') {
            if (currentLine == targetLine) {
                std::size_t len = i - lineStart;
                if (len > 0 && source[lineStart + len - 1] == '\r') {
                    --len;
                }
                return std::string(source.substr(lineStart, len));
            }
            ++currentLine;
            lineStart = i + 1;
        }
    }
    return {};
}

/// Builds a two-line caret underline diagram pointing at `span` inside `source`.
[[nodiscard]] inline std::string formatCaretDiagnostic(std::string_view source, const SourceSpan& span) {
    const std::string lineText = extractLineFromSource(source, span.line);
    if (lineText.empty() && source.empty()) {
        return "  <empty input>\n  ^";
    }

    std::string sanitizedLine;
    sanitizedLine.reserve(lineText.size());
    for (char ch : lineText) {
        sanitizedLine.push_back(ch == '\t' ? ' ' : ch);
    }

    const std::size_t caretOffset = (span.column > 0) ? (span.column - 1) : 0;
    const std::size_t clampedOffset = std::min(caretOffset, sanitizedLine.size());
    const std::size_t underlineLen = std::max<std::size_t>(1, span.length);

    std::ostringstream oss;
    oss << "  " << sanitizedLine << "\n  ";
    for (std::size_t i = 0; i < clampedOffset; ++i) {
        oss << ' ';
    }
    oss << '^';
    for (std::size_t i = 1; i < underlineLen; ++i) {
        oss << '~';
    }
    return oss.str();
}

}  // namespace mathcompiler
