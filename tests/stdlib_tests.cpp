#include "emojineer/bytecode.hpp"
#include "emojineer/module.hpp"
#include "emojineer/stdlib.hpp"
#include "emojineer/vm.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("stdlib test failed: " + message);
}

struct TempRoot {
    std::filesystem::path path;
    explicit TempRoot(const std::string& suffix) {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("emojineer-stdlib-" + suffix + "-" + std::to_string(nonce));
        std::filesystem::create_directories(path);
    }
    ~TempRoot() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

void write_source(const std::filesystem::path& path, const std::string& source) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write stdlib test source");
    output << source;
}

std::string execute(const emojineer::Chunk& chunk) {
    std::istringstream input;
    std::ostringstream output;
    emojineer::VM vm(input, output);
    vm.execute(chunk);
    return output.str();
}

std::string bytecode_bytes(const emojineer::Chunk& chunk) {
    std::ostringstream out(std::ios::binary);
    emojineer::write_bytecode(chunk, out);
    return out.str();
}

template <class Fn>
void expect_error(Fn&& fn, const std::string& needle) {
    try {
        fn();
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()).find(needle) != std::string::npos,
                "expected error containing '" + needle + "', got '" + error.what() + "'");
        return;
    }
    throw std::runtime_error("stdlib test failed: expected error containing '" + needle + "'");
}

void test_catalog() {
    const auto modules = emojineer::standard_modules();
    require(modules.size() == 3, "initial catalog should contain three modules");
    require(modules[0].specifier == "std:math", "math module should be first and stable");
    require(modules[1].specifier == "std:arrays", "arrays module should be second and stable");
    require(modules[2].specifier == "std:text", "text module should be third and stable");
    for (const auto& module : modules) {
        require(emojineer::standard_module_source(module.specifier).has_value(),
                "every catalog entry must have source");
        require(!module.description.empty(), "every catalog entry must have a description");
    }
}

void test_math_module() {
    TempRoot root("math");
    write_source(root.path / "main.emoji",
                 "🧩 🚀\n"
                 "🔗 📜std:math📜\n"
                 "📝 🧭 🫴 ➖ 9 🤲\n"
                 "📝 🤏 🫴 7 2 🤲\n"
                 "📝 👐 🫴 7 2 🤲\n"
                 "📝 🎚️ 🫴 11 0 10 🤲\n"
                 "📝 🎚️ 🫴 ➖ 4 0 10 🤲\n");
    require(execute(emojineer::compile_file(root.path / "main.emoji", {}, root.path)) ==
                "9\n2\n7\n10\n0\n",
            "std:math helpers should execute through normal Emojineer calls");
}

void test_arrays_module() {
    TempRoot root("arrays");
    write_source(root.path / "main.emoji",
                 "🧩 🚀\n"
                 "🔗 📜std:arrays📜\n"
                 "🐍 🧺 📚 🟰 📚 🫴 3 1 2 🤲\n"
                 "📝 🧲 🫴 🧺 1 🤲\n"
                 "📝 🧲 🫴 🧺 9 🤲\n"
                 "📝 🧮 🫴 🧺 🤲\n"
                 "📝 🔃 🫴 🧺 🤲\n");
    require(execute(emojineer::compile_file(root.path / "main.emoji", {}, root.path)) ==
                "✅\n❌\n6\n[2, 1, 3]\n",
            "std:arrays helpers should execute through core collection semantics");
}

void test_text_module() {
    TempRoot root("text");
    write_source(root.path / "main.emoji",
                 "🧩 🚀\n"
                 "🔗 📜std:text📜\n"
                 "📝 🈳 🫴 📜📜 🤲\n"
                 "📝 🈳 🫴 📜hello📜 🤲\n"
                 "📝 🪢 🫴 📜hello 📜 📜world📜 🤲\n"
                 "📝 🔂 🫴 📜ha📜 3 🤲\n"
                 "📝 🔂 🫴 📜ha📜 2.5 🤲\n");
    require(execute(emojineer::compile_file(root.path / "main.emoji", {}, root.path)) ==
                "✅\n❌\nhello world\nhahaha\n\n",
            "std:text helpers should define deterministic empty/concat/repeat behavior");
}

void test_unknown_and_duplicate_standard_imports() {
    TempRoot root("errors");
    write_source(root.path / "unknown.emoji",
                 "🧩 🚀\n"
                 "🔗 📜std:nope📜\n");
    expect_error([&] { (void)emojineer::compile_file(root.path / "unknown.emoji", {}, root.path); },
                 "unknown standard module");

    write_source(root.path / "duplicate.emoji",
                 "🧩 🚀\n"
                 "🔗 📜std:math📜\n"
                 "🔗 📜std:math📜\n");
    expect_error([&] { (void)emojineer::compile_file(root.path / "duplicate.emoji", {}, root.path); },
                 "duplicate 🔗 import");
}

void test_standard_and_file_module_interoperate() {
    TempRoot root("interop");
    write_source(root.path / "helper.emoji",
                 "🧩 🌲\n"
                 "🔗 📜std:math📜\n"
                 "🛠️ 🌟 🫴 🍎 🤲\n"
                 "    📦 🧭 🫴 🍎 🤲\n"
                 "🏁\n"
                 "📤 🌟\n");
    write_source(root.path / "main.emoji",
                 "🧩 🚀\n"
                 "🔗 📜helper.emoji📜\n"
                 "📝 🌟 🫴 ➖ 12 🤲\n");
    require(execute(emojineer::compile_file(root.path / "main.emoji", {}, root.path)) == "12\n",
            "ordinary modules should be able to use standard-module exports");
}

void test_standard_module_bytecode_is_checkout_independent() {
    TempRoot first("deterministic-a");
    TempRoot second("deterministic-b");
    const std::string source =
        "🧩 🚀\n"
        "🔗 📜std:math📜\n"
        "📝 🎚️ 🫴 15 0 10 🤲\n";
    write_source(first.path / "main.emoji", source);
    write_source(second.path / "main.emoji", source);
    const auto a = emojineer::compile_file(first.path / "main.emoji", {}, first.path);
    const auto b = emojineer::compile_file(second.path / "main.emoji", {}, second.path);
    require(bytecode_bytes(a) == bytecode_bytes(b),
            "standard-module identity must not depend on checkout location");
    require(execute(a) == "10\n", "deterministic standard-module chunk should execute");
}

} // namespace

int main() {
    try {
        test_catalog();
        test_math_module();
        test_arrays_module();
        test_text_module();
        test_unknown_and_duplicate_standard_imports();
        test_standard_and_file_module_interoperate();
        test_standard_module_bytecode_is_checkout_independent();
        std::cout << "✅ standard library tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
