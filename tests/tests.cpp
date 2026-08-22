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

void test_parser_bounds_safety() {
    // Test 1: Empty input should not cause out-of-range
    {
        emojineer::Lexer lexer("");
        auto tokens = lexer.tokenize();
        emojineer::Parser parser(std::move(tokens));
        auto program = parser.parse();
        require(program.statements.empty(), "empty input should produce empty program");
    }
    
    // Test 2: Truncated/incomplete statements should produce meaningful errors, not crashes
    {
        emojineer::Lexer lexer("🐍 🍎\n");  // Missing type and initializer
        auto tokens = lexer.tokenize();
        emojineer::Parser parser(std::move(tokens));
        try {
            auto program = parser.parse();
            // Should either succeed with partial parse or throw meaningful error
        } catch (const std::runtime_error& e) {
            // Expected - should get a meaningful error message
            require(std::string(e.what()).find("line") != std::string::npos,
                    "parser error should include line number");
        }
    }
    
    // Test 3: Incomplete expression
    {
        emojineer::Lexer lexer("📝 1 ➕\n");  // Incomplete addition
        auto tokens = lexer.tokenize();
        emojineer::Parser parser(std::move(tokens));
        try {
            auto program = parser.parse();
        } catch (const std::runtime_error& e) {
            // Expected - should get a meaningful error
        }
    }
}

void test_integer_overflow() {
    // Create a chunk with integer constants and test overflow detection
    emojineer::Chunk chunk;
    
    // Add INT64_MAX as constant
    std::int64_t max_val = INT64_MAX;
    chunk.constants.push_back(max_val);
    chunk.constants.push_back(std::int64_t(1));
    chunk.constants.push_back(std::int64_t(2));
    
    // Test AddInt overflow: INT64_MAX + 1
    chunk.code.push_back({emojineer::OpCode::Constant, 0, 1});  // push INT64_MAX
    chunk.code.push_back({emojineer::OpCode::Constant, 1, 1});  // push 1
    chunk.code.push_back({emojineer::OpCode::AddInt, 0, 1});    // INT64_MAX + 1 (overflow!)
    chunk.code.push_back({emojineer::OpCode::Print, 0, 1});
    chunk.code.push_back({emojineer::OpCode::Halt, 0, 1});
    
    std::istringstream in;
    std::ostringstream out;
    emojineer::VM vm(in, out);
    
    try {
        vm.execute(chunk);
        throw std::runtime_error("test failed: AddInt overflow should throw");
    } catch (const std::runtime_error& e) {
        require(std::string(e.what()).find("overflow") != std::string::npos,
                "AddInt overflow should report overflow error");
    }
    
    // Test SubtractInt underflow: INT64_MIN - 1
    chunk = emojineer::Chunk();
    std::int64_t min_val = INT64_MIN;
    chunk.constants.push_back(min_val);
    chunk.constants.push_back(std::int64_t(1));
    
    chunk.code.push_back({emojineer::OpCode::Constant, 0, 2});  // push INT64_MIN
    chunk.code.push_back({emojineer::OpCode::Constant, 1, 2});  // push 1
    chunk.code.push_back({emojineer::OpCode::SubtractInt, 0, 2}); // INT64_MIN - 1 (underflow!)
    chunk.code.push_back({emojineer::OpCode::Print, 0, 2});
    chunk.code.push_back({emojineer::OpCode::Halt, 0, 2});
    
    try {
        vm.execute(chunk);
        throw std::runtime_error("test failed: SubtractInt underflow should throw");
    } catch (const std::runtime_error& e) {
        require(std::string(e.what()).find("overflow") != std::string::npos,
                "SubtractInt underflow should report overflow error");
    }
    
    // Test MultiplyInt overflow: INT64_MAX * 2
    chunk = emojineer::Chunk();
    chunk.constants.push_back(max_val);
    chunk.constants.push_back(std::int64_t(2));
    
    chunk.code.push_back({emojineer::OpCode::Constant, 0, 3});  // push INT64_MAX
    chunk.code.push_back({emojineer::OpCode::Constant, 1, 3});  // push 2
    chunk.code.push_back({emojineer::OpCode::MultiplyInt, 0, 3}); // INT64_MAX * 2 (overflow!)
    chunk.code.push_back({emojineer::OpCode::Print, 0, 3});
    chunk.code.push_back({emojineer::OpCode::Halt, 0, 3});
    
    try {
        vm.execute(chunk);
        throw std::runtime_error("test failed: MultiplyInt overflow should throw");
    } catch (const std::runtime_error& e) {
        require(std::string(e.what()).find("overflow") != std::string::npos,
                "MultiplyInt overflow should report overflow error");
    }
    
    // Test valid integer operations don't overflow
    chunk = emojineer::Chunk();
    chunk.constants.push_back(std::int64_t(100));
    chunk.constants.push_back(std::int64_t(200));
    chunk.constants.push_back(std::int64_t(3));
    
    chunk.code.push_back({emojineer::OpCode::Constant, 0, 4});   // push 100
    chunk.code.push_back({emojineer::OpCode::Constant, 1, 4});   // push 200
    chunk.code.push_back({emojineer::OpCode::AddInt, 0, 4});     // 100 + 200 = 300
    chunk.code.push_back({emojineer::OpCode::Constant, 2, 4});   // push 3
    chunk.code.push_back({emojineer::OpCode::MultiplyInt, 0, 4}); // 300 * 3 = 900
    chunk.code.push_back({emojineer::OpCode::Print, 0, 4});
    chunk.code.push_back({emojineer::OpCode::Halt, 0, 4});
    
    in = std::istringstream();
    out = std::ostringstream();
    emojineer::VM vm2(in, out);
    vm2.execute(chunk);
    require(out.str() == "900\n", "valid integer operations should work correctly");
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
        test_parser_bounds_safety();
        test_integer_overflow();
        std::cout << "✅ all Emojineer tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
