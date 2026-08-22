#include "emojineer/source_tools.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("source-tools test failed: " + message);
}

void test_block_indentation_and_comments() {
    const std::string source =
        "🤔 ✅\n"
        "📝 📜yes📜\n"
        "💭 keep this comment\n"
        "🙅\n"
        "📝 📜no📜\n"
        "🏁\n";
    const std::string expected =
        "🤔 ✅\n"
        "    📝 📜yes📜\n"
        "    💭 keep this comment\n"
        "🙅\n"
        "    📝 📜no📜\n"
        "🏁\n";
    require(emojineer::format_source(source) == expected,
            "formatter should apply four-space block indentation without dropping comments");
}

void test_multiline_string_is_preserved() {
    const std::string source =
        "📝 📜hello   \n"
        "  keep   me\n"
        "world📜\n";
    require(emojineer::format_source(source) == source,
            "formatter must not rewrite whitespace inside multiline strings");
}

void test_diagnostics_follow_formatter() {
    const std::string unformatted =
        "🤔 ✅\n"
        "📝 📜yes📜\n"
        "🏁";
    const auto diagnostics = emojineer::diagnose_source_style(unformatted);
    require(!diagnostics.empty(), "lint diagnostics should detect noncanonical formatting");

    const std::string formatted = emojineer::format_source(unformatted) + "\n";
    require(emojineer::diagnose_source_style(formatted).empty(),
            "canonically formatted source with final newline should be clean");
}

void test_crlf_normalization() {
    const std::string source = "📝 📜hello📜\r\n";
    require(emojineer::format_source(source) == "📝 📜hello📜\n",
            "formatter should normalize CRLF to LF");
}

} // namespace

int main() {
    try {
        test_block_indentation_and_comments();
        test_multiline_string_is_preserved();
        test_diagnostics_follow_formatter();
        test_crlf_normalization();
        std::cout << "✅ source tooling tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
