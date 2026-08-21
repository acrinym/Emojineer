#include "emojineer/bytecode.hpp"
#include "emojineer/compiler.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/vm.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("test failed: " + message);
}

emojineer::Chunk compile(const std::string& source) {
    emojineer::Lexer lexer(source);
    emojineer::Parser parser(lexer.tokenize());
    emojineer::Compiler compiler;
    return compiler.compile(parser.parse());
}

std::string run(const std::string& source, const std::string& input = "") {
    auto chunk = compile(source);
    std::istringstream in(input);
    std::ostringstream out;
    emojineer::VM vm(in, out);
    vm.execute(chunk);
    return out.str();
}

void test_variation_selector_identity() {
    emojineer::Lexer text_style("📝 2 ✖ 3\n");
    emojineer::Lexer emoji_style("📝 2 ✖️ 3\n");
    auto a = text_style.tokenize();
    auto b = emoji_style.tokenize();
    require(a.size() == b.size(), "variation-selector forms should tokenize equally");
    for (std::size_t i = 0; i < a.size(); ++i) {
        require(a[i].kind == b[i].kind, "variation selector must not change token kind");
        require(a[i].canonical == b[i].canonical, "variation selector must not change canonical token identity");
    }
}

void test_modifier_identifiers_are_distinct() {
    const std::string source =
        "🐍 👍🏻 🔢 🟰 1\n"
        "🐍 👍🏿 🔢 🟰 2\n"
        "📝 👍🏻 ➕ 👍🏿\n";
    require(run(source) == "3\n", "skin-tone modifiers must preserve distinct identifier identity");
}

void test_arithmetic_and_types() {
    const std::string source =
        "🐍 🍎 🔢 🟰 2\n"
        "🐍 🍐 🔢 🟰 3\n"
        "📝 🍎 ➕ 🍐 ✖️ 4\n";
    require(run(source) == "14\n", "operator precedence or typed variables broken");
}

void test_if_else_and_loop() {
    const std::string source =
        "🐍 🍎 🔢 🟰 3\n"
        "🔁 🍎 🔼 0\n"
        "📝 🍎\n"
        "✏️ 🍎 🟰 🍎 ➖ 1\n"
        "🏁\n"
        "🤔 🍎 🟰 0\n"
        "📝 📜done📜\n"
        "🙅\n"
        "📝 📜bad📜\n"
        "🏁\n";
    require(run(source) == "3\n2\n1\ndone\n", "control flow broken");
}

void test_input_and_text() {
    const std::string source =
        "🐍 👤 🔤 🟰 📥\n"
        "📝 📜Hello, 📜 ➕ 👤\n";
    require(run(source, "Ada\n") == "Hello, Ada\n", "input/text concatenation broken");
}

void test_bytecode_roundtrip() {
    auto original = compile("📝 📜roundtrip📜\n");
    std::stringstream bytes(std::ios::in | std::ios::out | std::ios::binary);
    emojineer::write_bytecode(original, bytes);
    bytes.seekg(0);
    auto restored = emojineer::read_bytecode(bytes);
    std::istringstream in;
    std::ostringstream out;
    emojineer::VM vm(in, out);
    vm.execute(restored);
    require(out.str() == "roundtrip\n", "bytecode roundtrip broken");
}

void test_type_error() {
    try {
        (void)run("🐍 🍎 🔢 🟰 📜nope📜\n");
        throw std::runtime_error("test failed: type mismatch should throw");
    } catch (const std::runtime_error& e) {
        require(std::string(e.what()).find("🔢") != std::string::npos,
                "type mismatch should explain required type");
    }
}

} // namespace

int main() {
    try {
        test_variation_selector_identity();
        test_modifier_identifiers_are_distinct();
        test_arithmetic_and_types();
        test_if_else_and_loop();
        test_input_and_text();
        test_bytecode_roundtrip();
        test_type_error();
        std::cout << "✅ all Emojineer tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
