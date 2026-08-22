#include "emojineer/lexer.hpp"
#include "emojineer/unicode.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace emojineer {
namespace {

bool is_ascii_digit_grapheme(const Grapheme& g) {
    return g.display.size() == 1 && std::isdigit(static_cast<unsigned char>(g.display[0]));
}

bool is_ascii_space(const Grapheme& g) {
    return g.display == " " || g.display == "\t" || g.display == "\r";
}

std::string normalize_newlines(std::string source) {
    std::string out;
    out.reserve(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\r') {
            if (i + 1 < source.size() && source[i + 1] == '\n') ++i;
            out.push_back('\n');
        } else out.push_back(source[i]);
    }
    return out;
}

} // namespace

TokenRegistry::TokenRegistry() {
    auto add = [&](const std::string& glyph, TokenKind kind, const std::string& description) {
        definitions_.emplace(canonicalize_token(glyph), TokenDefinition{kind, description});
    };
    add("🐍", TokenKind::Var, "declare variable");
    add("✏️", TokenKind::Assign, "assign variable");
    add("📝", TokenKind::Print, "print value");
    add("🤔", TokenKind::If, "begin if block");
    add("🙅", TokenKind::Else, "begin else block");
    add("🔁", TokenKind::While, "begin while loop");
    add("🏁", TokenKind::End, "end block");
    add("📥", TokenKind::Input, "read one line of input");
    add("✅", TokenKind::True, "boolean true");
    add("❌", TokenKind::False, "boolean false");
    add("🔢", TokenKind::TypeNumber, "number type");
    add("🔤", TokenKind::TypeString, "text type");
    add("🎯", TokenKind::TypeBool, "boolean type");
    add("➕", TokenKind::Add, "addition or text concatenation");
    add("➖", TokenKind::Subtract, "subtraction or numeric negation");
    add("✖️", TokenKind::Multiply, "multiplication");
    add("➗", TokenKind::Divide, "division");
    add("🪄", TokenKind::Modulo, "remainder");
    add("🟰", TokenKind::Equal, "assignment separator or equality comparison");
    add("🔽", TokenKind::Less, "less-than comparison");
    add("🔼", TokenKind::Greater, "greater-than comparison");
    add("🚫", TokenKind::Not, "boolean negation");
    add("🫴", TokenKind::GroupStart, "begin grouped expression");
    add("🤲", TokenKind::GroupEnd, "end grouped expression");
}

const TokenDefinition* TokenRegistry::find(const std::string& canonical) const {
    auto it = definitions_.find(canonical);
    return it == definitions_.end() ? nullptr : &it->second;
}

std::string TokenRegistry::describe(TokenKind kind) const {
    for (const auto& [_, definition] : definitions_) if (definition.kind == kind) return definition.description;
    switch (kind) {
        case TokenKind::Number: return "numeric literal";
        case TokenKind::String: return "text literal";
        case TokenKind::Identifier: return "emoji identifier";
        case TokenKind::Newline: return "end of source line";
        case TokenKind::Eof: return "end of file";
        default: return token_kind_name(kind);
    }
}

std::string token_kind_name(TokenKind kind) {
    switch (kind) {
        case TokenKind::Eof: return "Eof"; case TokenKind::Newline: return "Newline";
        case TokenKind::Number: return "Number"; case TokenKind::String: return "String";
        case TokenKind::Identifier: return "Identifier"; case TokenKind::Var: return "Var";
        case TokenKind::Assign: return "Assign"; case TokenKind::Print: return "Print";
        case TokenKind::If: return "If"; case TokenKind::Else: return "Else";
        case TokenKind::While: return "While"; case TokenKind::End: return "End";
        case TokenKind::Input: return "Input"; case TokenKind::True: return "True";
        case TokenKind::False: return "False"; case TokenKind::TypeNumber: return "TypeNumber";
        case TokenKind::TypeString: return "TypeString"; case TokenKind::TypeBool: return "TypeBool";
        case TokenKind::Add: return "Add"; case TokenKind::Subtract: return "Subtract";
        case TokenKind::Multiply: return "Multiply"; case TokenKind::Divide: return "Divide";
        case TokenKind::Modulo: return "Modulo"; case TokenKind::Equal: return "Equal";
        case TokenKind::Less: return "Less"; case TokenKind::Greater: return "Greater";
        case TokenKind::Not: return "Not"; case TokenKind::GroupStart: return "GroupStart";
        case TokenKind::GroupEnd: return "GroupEnd";
    }
    return "Unknown";
}

Lexer::Lexer(std::string source) : source_(normalize_newlines(std::move(source))) {}

std::vector<Token> Lexer::tokenize() const {
    const auto graphemes = segment_graphemes(source_);
    const std::string string_fence = canonicalize_token("📜");
    const std::string comment = canonicalize_token("💭");
    std::vector<Token> tokens;
    std::size_t line = 1, column = 1;

    for (std::size_t i = 0; i < graphemes.size();) {
        const Grapheme& g = graphemes[i];
        if (g.display == "\n") {
            tokens.push_back({TokenKind::Newline, "\n", "\n", "", line, column});
            ++line; column = 1; ++i; continue;
        }
        if (is_ascii_space(g)) { ++column; ++i; continue; }
        if (g.canonical == comment) {
            while (i < graphemes.size() && graphemes[i].display != "\n") { ++i; ++column; }
            continue;
        }
        if (g.canonical == string_fence) {
            const std::size_t start_line = line, start_col = column;
            std::string literal, lexeme = g.display;
            ++i; ++column; bool closed = false;
            while (i < graphemes.size()) {
                const Grapheme& part = graphemes[i];
                if (part.canonical == string_fence) {
                    lexeme += part.display; ++i; ++column; closed = true; break;
                }
                if (part.display == "\n") { ++line; column = 1; } else ++column;
                literal += part.display; lexeme += part.display; ++i;
            }
            if (!closed) {
                std::ostringstream msg; msg << "line " << start_line << ", column " << start_col << ": unterminated 📜 string literal";
                throw std::runtime_error(msg.str());
            }
            tokens.push_back({TokenKind::String, lexeme, string_fence, literal, start_line, start_col});
            continue;
        }
        if (is_ascii_digit_grapheme(g)) {
            const std::size_t start_col = column; bool seen_dot = false; std::string number;
            while (i < graphemes.size()) {
                const Grapheme& part = graphemes[i];
                if (is_ascii_digit_grapheme(part)) { number += part.display; ++i; ++column; continue; }
                if (part.display == "." && !seen_dot) { seen_dot = true; number += '.'; ++i; ++column; continue; }
                break;
            }
            if (!number.empty() && number.back() == '.') throw std::runtime_error("line " + std::to_string(line) + ": number cannot end with '.'");
            tokens.push_back({TokenKind::Number, number, number, number, line, start_col});
            continue;
        }
        if (const TokenDefinition* def = registry_.find(g.canonical)) {
            tokens.push_back({def->kind, g.display, g.canonical, "", line, column}); ++column; ++i; continue;
        }
        if (is_emoji_grapheme(g.canonical)) {
            tokens.push_back({TokenKind::Identifier, g.display, g.canonical, g.canonical, line, column}); ++column; ++i; continue;
        }
        std::ostringstream msg;
        msg << "line " << line << ", column " << column << ": unexpected grapheme '" << g.display << "' (" << codepoints_hex(g.display) << ")";
        throw std::runtime_error(msg.str());
    }
    tokens.push_back({TokenKind::Eof, "", "", "", line, column});
    return tokens;
}

std::string Lexer::explain() const {
    std::ostringstream out;
    for (const Token& token : tokenize()) {
        if (token.kind == TokenKind::Eof || token.kind == TokenKind::Newline) continue;
        out << token.line << ':' << token.column << "  " << token.lexeme << "  →  " << registry_.describe(token.kind);
        if (token.kind == TokenKind::Identifier) out << " [canonical " << codepoints_hex(token.canonical) << ']';
        else if (token.kind == TokenKind::Number || token.kind == TokenKind::String) out << " = " << token.literal;
        out << '\n';
    }
    return out.str();
}

} // namespace emojineer
