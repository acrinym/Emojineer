#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

namespace emojineer {

// Source location exception - carries typed source position info for lexer/parser/compiler.
// This is a protocol-neutral error type owned by the core (lexer, parser, compiler, module).
// LSP may include this header to access source diagnostics.
struct SourceLocationException : public std::exception {
    std::string message;
    std::filesystem::path sourcePath;  // The source file that owns the error
    std::size_t line;      // 1-based grapheme line
    std::size_t column;    // 1-based grapheme column
    std::string tokenLexeme;  // Optional: the token that caused the error
    
    SourceLocationException(const std::string& msg,
                           std::filesystem::path path = {},
                           std::size_t ln = 1,
                           std::size_t col = 1,
                           const std::string& lexeme = {})
        : message(msg), sourcePath(std::move(path)), line(ln), column(col), tokenLexeme(lexeme) {}
    
    const char* what() const noexcept override {
        return message.c_str();
    }
};

} // namespace emojineer
