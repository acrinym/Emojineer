#include "emojineer/source_tools.hpp"

#include "emojineer/lexer.hpp"
#include "emojineer/token.hpp"
#include "emojineer/unicode.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace emojineer {
namespace {

std::string normalize_newlines(std::string source) {
    std::string out;
    out.reserve(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\r') {
            if (i + 1 < source.size() && source[i + 1] == '\n') ++i;
            out.push_back('\n');
        } else {
            out.push_back(source[i]);
        }
    }
    return out;
}

std::vector<std::string> split_lines(const std::string& source) {
    if (source.empty()) return {""};
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < source.size()) {
        const std::size_t end = source.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(source.substr(start));
            break;
        }
        lines.push_back(source.substr(start, end - start));
        start = end + 1;
    }
    if (lines.empty()) lines.emplace_back();
    return lines;
}

bool is_ascii_indent(char c) {
    return c == ' ' || c == '\t';
}

std::string trim_ascii_edges(const std::string& line) {
    std::size_t begin = 0;
    while (begin < line.size() && is_ascii_indent(line[begin])) ++begin;
    std::size_t end = line.size();
    while (end > begin && is_ascii_indent(line[end - 1])) --end;
    return line.substr(begin, end - begin);
}

std::vector<bool> string_sensitive_lines(const std::vector<std::string>& lines) {
    const std::string fence = canonicalize_token("📜");
    const std::string comment = canonicalize_token("💭");
    std::vector<bool> protected_line(lines.size(), false);
    bool in_string = false;

    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const bool started_in_string = in_string;
        for (const Grapheme& grapheme : segment_graphemes(lines[line_index])) {
            if (grapheme.canonical == fence) {
                in_string = !in_string;
                continue;
            }
            if (!in_string && grapheme.canonical == comment) break;
        }
        protected_line[line_index] = started_in_string || in_string;
    }
    return protected_line;
}

bool opens_block(TokenKind kind) {
    return kind == TokenKind::If || kind == TokenKind::While || kind == TokenKind::Function;
}

bool closes_before_line(TokenKind kind) {
    return kind == TokenKind::Else || kind == TokenKind::End;
}

bool reopens_after_line(TokenKind kind) {
    return kind == TokenKind::Else;
}

std::unordered_map<std::size_t, TokenKind> first_tokens_by_line(
    const std::string& source, CustomEmojiRegistry registry) {
    std::unordered_map<std::size_t, TokenKind> first;
    Lexer lexer(source, std::move(registry));
    for (const Token& token : lexer.tokenize()) {
        if (token.kind == TokenKind::Eof || token.kind == TokenKind::Newline) continue;
        first.try_emplace(token.line, token.kind);
    }
    return first;
}

} // namespace

std::string format_source(const std::string& raw_source, CustomEmojiRegistry registry) {
    const bool had_final_newline = !raw_source.empty() &&
                                   (raw_source.back() == '\n' || raw_source.back() == '\r');
    const std::string source = normalize_newlines(raw_source);
    const auto first_tokens = first_tokens_by_line(source, std::move(registry));
    const auto lines = split_lines(source);
    const auto protected_lines = string_sensitive_lines(lines);

    std::ostringstream out;
    std::size_t depth = 0;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::size_t line_number = i + 1;
        const auto token_it = first_tokens.find(line_number);
        const bool has_token = token_it != first_tokens.end();
        const TokenKind first_kind = has_token ? token_it->second : TokenKind::Eof;

        if (!protected_lines[i] && has_token && closes_before_line(first_kind) && depth > 0) {
            --depth;
        }

        if (protected_lines[i]) {
            out << lines[i];
        } else {
            const std::string body = trim_ascii_edges(lines[i]);
            if (!body.empty()) out << std::string(depth * 4, ' ') << body;
        }

        if (i + 1 < lines.size() || had_final_newline) out << '\n';

        if (!protected_lines[i] && has_token &&
            (opens_block(first_kind) || reopens_after_line(first_kind))) {
            ++depth;
        }
    }

    return out.str();
}

std::vector<StyleDiagnostic> diagnose_source_style(const std::string& source,
                                                   CustomEmojiRegistry registry) {
    std::vector<StyleDiagnostic> diagnostics;

    const std::size_t first_cr = source.find('\r');
    if (first_cr != std::string::npos) {
        const std::size_t line = 1 + static_cast<std::size_t>(
            std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(first_cr), '\n'));
        diagnostics.push_back({line, "source uses noncanonical CR/CRLF line endings; use LF"});
    }

    const std::string normalized = normalize_newlines(source);
    const std::string formatted = format_source(source, std::move(registry));
    const auto original_lines = split_lines(normalized);
    const auto formatted_lines = split_lines(normalize_newlines(formatted));
    const std::size_t count = std::max(original_lines.size(), formatted_lines.size());

    for (std::size_t i = 0; i < count; ++i) {
        const std::string original = i < original_lines.size() ? original_lines[i] : std::string{};
        const std::string expected = i < formatted_lines.size() ? formatted_lines[i] : std::string{};
        if (original != expected) {
            diagnostics.push_back({i + 1, "source line differs from canonical Emojineer formatting"});
        }
    }

    if (!source.empty() && source.back() != '\n' && source.back() != '\r') {
        diagnostics.push_back({original_lines.size(), "source file should end with a newline"});
    }

    return diagnostics;
}

} // namespace emojineer
