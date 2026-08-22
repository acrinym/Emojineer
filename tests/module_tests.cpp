#include "emojineer/bytecode.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/module.hpp"
#include "emojineer/project.hpp"
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
    if (!condition) throw std::runtime_error("module test failed: " + message);
}

struct TempRoot {
    std::filesystem::path path;
    explicit TempRoot(const std::string& suffix) {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("emojineer-modules-" + suffix + "-" + std::to_string(nonce));
        std::filesystem::create_directories(path);
    }
    ~TempRoot() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

void write_source(const std::filesystem::path& path, const std::string& source) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write test source");
    output << source;
}

std::string execute(const emojineer::Chunk& chunk) {
    std::istringstream input;
    std::ostringstream output;
    emojineer::VM vm(input, output);
    vm.execute(chunk);
    return output.str();
}

template <class Fn>
std::string expect_error(Fn&& fn, const std::string& needle) {
    try {
        fn();
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        require(message.find(needle) != std::string::npos,
                "expected error containing '" + needle + "', got '" + message + "'");
        return message;
    }
    throw std::runtime_error("module test failed: expected error containing '" + needle + "'");
}

void test_module_tokens() {
    emojineer::Lexer lexer("🧩 🚀\n🔗 📜lib.emoji📜\n📤 🌟\n");
    const auto tokens = lexer.tokenize();
    require(tokens[0].kind == emojineer::TokenKind::Module, "🧩 should be Module");
    require(tokens[3].kind == emojineer::TokenKind::Import, "🔗 should be Import");
    require(tokens[6].kind == emojineer::TokenKind::Export, "📤 should be Export");
}

void test_exported_function_and_global() {
    TempRoot root("exports");
    write_source(root.path / "math.emoji",
                 "🧩 🧮\n"
                 "🐍 🌟 🔢 🟰 7\n"
                 "🛠️ 🧠 🫴 🍎 🍐 🤲\n"
                 "📦 🍎 ➕ 🍐 ➕ 🌟\n"
                 "🏁\n"
                 "📤 🌟\n"
                 "📤 🧠\n");
    write_source(root.path / "main.emoji",
                 "🧩 🚀\n"
                 "🔗 📜math.emoji📜\n"
                 "📝 🧠 🫴 2 3 🤲\n"
                 "📝 🌟\n");
    const auto chunk = emojineer::compile_file(root.path / "main.emoji", {}, root.path);
    require(execute(chunk) == "12\n7\n", "imported public function/global should execute");
}

void test_imported_global_is_a_live_binding() {
    TempRoot root("mutable-export");
    write_source(root.path / "state.emoji",
                 "🧩 🌲\n"
                 "🐍 🌟 🔢 🟰 7\n"
                 "📤 🌟\n");
    write_source(root.path / "main.emoji",
                 "🧩 🚀\n"
                 "🔗 📜state.emoji📜\n"
                 "✏️ 🌟 🟰 9\n"
                 "📝 🌟\n");
    require(execute(emojineer::compile_file(root.path / "main.emoji", {}, root.path)) == "9\n",
            "assignment should target the exported global binding");
}

void test_private_symbols_and_module_local_isolation() {
    TempRoot root("privacy");
    write_source(root.path / "a.emoji",
                 "🧩 🌲\n"
                 "🐍 🌑 🔢 🟰 1\n"
                 "🛠️ 🍏 🫴 🤲\n"
                 "📦 🌑\n"
                 "🏁\n"
                 "📤 🍏\n");
    write_source(root.path / "b.emoji",
                 "🧩 🌊\n"
                 "🐍 🌑 🔢 🟰 2\n"
                 "🛠️ 🍊 🫴 🤲\n"
                 "📦 🌑\n"
                 "🏁\n"
                 "📤 🍊\n");
    write_source(root.path / "main.emoji",
                 "🧩 🚀\n"
                 "🔗 📜a.emoji📜\n"
                 "🔗 📜b.emoji📜\n"
                 "📝 🍏 🫴 🤲\n"
                 "📝 🍊 🫴 🤲\n");
    require(execute(emojineer::compile_file(root.path / "main.emoji", {}, root.path)) == "1\n2\n",
            "same private emoji name in two modules must remain isolated");

    write_source(root.path / "private-main.emoji",
                 "🧩 🚀\n"
                 "🔗 📜a.emoji📜\n"
                 "📝 🌑\n");
    expect_error([&] { (void)emojineer::compile_file(root.path / "private-main.emoji", {}, root.path); },
                 "non-exported emoji variable");
}

void test_duplicate_missing_and_late_imports() {
    TempRoot root("import-errors");
    write_source(root.path / "lib.emoji", "🧩 🌲\n");
    write_source(root.path / "duplicate.emoji",
                 "🧩 🚀\n"
                 "🔗 📜lib.emoji📜\n"
                 "🔗 📜./lib.emoji📜\n");
    expect_error([&] { (void)emojineer::compile_file(root.path / "duplicate.emoji", {}, root.path); },
                 "duplicate 🔗 import");

    write_source(root.path / "missing.emoji",
                 "🧩 🚀\n"
                 "🔗 📜does-not-exist.emoji📜\n");
    expect_error([&] { (void)emojineer::compile_file(root.path / "missing.emoji", {}, root.path); },
                 "does not exist");

    write_source(root.path / "late.emoji",
                 "🧩 🚀\n"
                 "📝 📜before📜\n"
                 "🔗 📜lib.emoji📜\n");
    expect_error([&] { (void)emojineer::compile_file(root.path / "late.emoji", {}, root.path); },
                 "must appear before");
}

void test_cycles_and_root_escape() {
    TempRoot root("cycle");
    write_source(root.path / "a.emoji", "🧩 🌲\n🔗 📜b.emoji📜\n");
    write_source(root.path / "b.emoji", "🧩 🌊\n🔗 📜a.emoji📜\n");
    const auto cycle = expect_error(
        [&] { (void)emojineer::compile_file(root.path / "a.emoji", {}, root.path); },
        "cyclic module import");
    require(cycle.find("a.emoji") != std::string::npos && cycle.find("b.emoji") != std::string::npos,
            "cycle diagnostic should name the import chain");

    TempRoot outer("escape");
    const auto project = outer.path / "project";
    std::filesystem::create_directories(project);
    write_source(outer.path / "outside.emoji", "🧩 🌋\n");
    write_source(project / "main.emoji", "🧩 🚀\n🔗 📜../outside.emoji📜\n");
    expect_error([&] { (void)emojineer::compile_file(project / "main.emoji", {}, project); },
                 "escapes the module root");
}

void test_collisions_and_declaration_rules() {
    TempRoot root("collisions");
    write_source(root.path / "a.emoji", "🧩 🌲\n🐍 🌟 🔢 🟰 1\n📤 🌟\n");
    write_source(root.path / "b.emoji", "🧩 🌊\n🐍 🌟 🔢 🟰 2\n📤 🌟\n");
    write_source(root.path / "main.emoji",
                 "🧩 🚀\n🔗 📜a.emoji📜\n🔗 📜b.emoji📜\n📝 🌟\n");
    expect_error([&] { (void)emojineer::compile_file(root.path / "main.emoji", {}, root.path); },
                 "multiple imports export the same emoji variable");

    write_source(root.path / "no-module.emoji", "🔗 📜a.emoji📜\n");
    expect_error([&] { (void)emojineer::compile_file(root.path / "no-module.emoji", {}, root.path); },
                 "must begin with 🧩");

    write_source(root.path / "bad-export.emoji", "🧩 🚀\n📤 🌟\n");
    expect_error([&] { (void)emojineer::compile_file(root.path / "bad-export.emoji", {}, root.path); },
                 "not declared");

    write_source(root.path / "name-a.emoji", "🧩 🌲\n");
    write_source(root.path / "name-b.emoji", "🧩 🌲\n");
    write_source(root.path / "name-main.emoji",
                 "🧩 🚀\n🔗 📜name-a.emoji📜\n🔗 📜name-b.emoji📜\n");
    expect_error([&] { (void)emojineer::compile_file(root.path / "name-main.emoji", {}, root.path); },
                 "duplicate 🧩 module name");
}

void write_deterministic_graph(const std::filesystem::path& root) {
    write_source(root / "lib/math.emoji",
                 "🧩 🧮\n"
                 "🛠️ 🧠 🫴 🍎 🤲\n"
                 "📦 🍎 ➕ 1\n"
                 "🏁\n"
                 "📤 🧠\n");
    write_source(root / "main.emoji",
                 "🧩 🚀\n"
                 "🔗 📜lib/math.emoji📜\n"
                 "📝 🧠 🫴 4 🤲\n");
}

std::string bytecode_bytes(const emojineer::Chunk& chunk) {
    std::ostringstream out(std::ios::binary);
    emojineer::write_bytecode(chunk, out);
    return out.str();
}

void test_deterministic_identity_and_bytecode_compatibility() {
    TempRoot first("deterministic-a");
    TempRoot second("deterministic-b");
    write_deterministic_graph(first.path);
    write_deterministic_graph(second.path);
    const auto a = emojineer::compile_file(first.path / "main.emoji", {}, first.path);
    const auto b = emojineer::compile_file(second.path / "main.emoji", {}, second.path);
    require(bytecode_bytes(a) == bytecode_bytes(b),
            "module identity must not depend on absolute checkout location");

    std::stringstream bytes(std::ios::in | std::ios::out | std::ios::binary);
    emojineer::write_bytecode(a, bytes);
    bytes.seekg(0);
    const auto roundtrip = emojineer::read_bytecode(bytes);
    require(execute(roundtrip) == "5\n", "module-linked EMJBC v3 should round-trip and execute");
}

void test_dependency_initialization_once_and_project_check() {
    TempRoot root("init-once");
    write_source(root.path / "shared.emoji",
                 "🧩 🌟\n"
                 "📝 📜shared-init📜\n"
                 "🛠️ 🧠 🫴 🤲\n"
                 "📦 1\n"
                 "🏁\n"
                 "📤 🧠\n");
    write_source(root.path / "left.emoji", "🧩 🌲\n🔗 📜shared.emoji📜\n");
    write_source(root.path / "right.emoji", "🧩 🌊\n🔗 📜shared.emoji📜\n");
    write_source(root.path / "main.emoji",
                 "🧩 🚀\n"
                 "🔗 📜left.emoji📜\n"
                 "🔗 📜right.emoji📜\n"
                 "🔗 📜shared.emoji📜\n"
                 "📝 🧠 🫴 🤲\n");
    require(execute(emojineer::compile_file(root.path / "main.emoji", {}, root.path)) ==
                "shared-init\n1\n",
            "diamond dependency should initialize once and remain directly visible only through exports");

    TempRoot project("project-check");
    emojineer::initialize_project(project.path, "module_project");
    auto manifest = emojineer::load_project_manifest(project.path / "emojineer.toml");
    write_source(project.path / manifest.entry,
                 "🧩 🚀\n🔗 📜missing.emoji📜\n");
    const auto diagnostics = emojineer::check_project(project.path);
    bool saw_source_graph = false;
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.message.find("source graph") != std::string::npos) saw_source_graph = true;
    }
    require(saw_source_graph, "emji project check should validate the module graph");
}

} // namespace

int main() {
    try {
        test_module_tokens();
        test_exported_function_and_global();
        test_imported_global_is_a_live_binding();
        test_private_symbols_and_module_local_isolation();
        test_duplicate_missing_and_late_imports();
        test_cycles_and_root_escape();
        test_collisions_and_declaration_rules();
        test_deterministic_identity_and_bytecode_compatibility();
        test_dependency_initialization_once_and_project_check();
        std::cout << "✅ module/import tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
