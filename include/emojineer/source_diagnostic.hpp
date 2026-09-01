#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace emojineer {

// Source location exception - carries typed source position info for lexer/parser/compiler.
// This is a protocol-neutral error type owned by the core (lexer, parser, compiler, module).
// LSP may include this header to access source diagnostics.
struct SourceLocationException : public std::runtime_error {
    std::string message;
    std::filesystem::path sourcePath;  // The actual source file path (filesystem path)
    std::string sourceIdentity;        // The module identity (e.g., "pkg:foo/src/main.emoji" or "std:io")
    std::size_t line;      // 1-based grapheme line
    std::size_t column;    // 1-based grapheme column
    std::string tokenLexeme;  // Optional: the token that caused the error

    SourceLocationException(const std::string& msg,
                           std::filesystem::path path = {},
                           std::size_t ln = 1,
                           std::size_t col = 1,
                           const std::string& lexeme = {})
        : std::runtime_error(msg), message(msg), sourcePath(std::move(path)), line(ln), column(col), tokenLexeme(lexeme) {}

    SourceLocationException(const std::string& msg,
                           std::filesystem::path path,
                           std::string identity,
                           std::size_t ln,
                           std::size_t col,
                           const std::string& lexeme = {})
        : std::runtime_error(msg), message(msg), sourcePath(std::move(path)), sourceIdentity(std::move(identity)),
          line(ln), column(col), tokenLexeme(lexeme) {}
};

} // namespace emojineer
