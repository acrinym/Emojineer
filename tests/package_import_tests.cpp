#include "emojineer/bytecode.hpp"
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
#include <utility>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("package import test failed: " + message);
}

struct TempRoot {
    std::filesystem::path path;
    explicit TempRoot(const std::string& suffix) {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("emojineer-package-imports-" + suffix + "-" + std::to_string(nonce));
        std::filesystem::create_directories(path);
    }
    ~TempRoot() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write test file");
    output << text;
}

using Dependency = std::pair<std::string, std::filesystem::path>;

void write_manifest(const std::filesystem::path& root,
                    const std::string& name,
                    const std::vector<Dependency>& dependencies = {},
                    const std::string& entry = "src/main.emoji") {
    std::ostringstream manifest;
    manifest << "[package]\n"
             << "name = \"" << name << "\"\n"
             << "version = \"0.1.0\"\n"
             << "entry = \"" << entry << "\"\n";
    if (!dependencies.empty()) {
        manifest << "\n[dependencies]\n";
        for (const auto& [dependency_name, dependency_path] : dependencies) {
            manifest << dependency_name << " = \""
                     << dependency_path.generic_string() << "\"\n";
        }
    }
    write_text(root / "emojineer.toml", manifest.str());
}

std::string execute(const emojineer::Chunk& chunk) {
    std::istringstream input;
    std::ostringstream output;
    emojineer::VM vm(input, output);
    vm.execute(chunk);
    return output.str();
}

std::string bytecode_bytes(const emojineer::Chunk& chunk) {
    std::ostringstream output(std::ios::binary);
    emojineer::write_bytecode(chunk, output);
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
    throw std::runtime_error("package import test failed: expected error containing '" + needle + "'");
}

void write_mathkit(const std::filesystem::path& root) {
    write_manifest(root, "mathkit");
    write_text(root / "src/main.emoji",
               "🧩 🧮\n"
               "🛠️ 🧠 🫴 🍎 🤲\n"
               "📦 🍎 ➕ 1\n"
               "🏁\n"
               "📤 🧠\n");
}

void test_direct_declared_package_import_and_project_check() {
    TempRoot root("direct");
    const auto app = root.path / "app";
    const auto mathkit = app / "deps/mathkit";
    write_mathkit(mathkit);
    write_manifest(app, "app", {{"mathkit", "deps/mathkit"}});
    write_text(app / "src/main.emoji",
               "🧩 🚀\n"
               "🔗 📜pkg:mathkit/src/main.emoji📜\n"
               "📝 🧠 🫴 5 🤲\n");

    const auto chunk = emojineer::compile_file(app / "src/main.emoji", {}, app);
    require(execute(chunk) == "6\n", "declared pkg: import should link and execute");

    const auto diagnostics = emojineer::check_project(app);
    require(diagnostics.empty(), "emji check path should accept package-aware source graph");
}

void test_transitive_dependency_is_not_ambient_and_local_paths_cannot_cross_packages() {
    TempRoot root("transitive");
    const auto app = root.path / "app";
    const auto b = app / "deps/b";
    const auto c = b / "vendor/c";

    write_manifest(c, "c");
    write_text(c / "src/main.emoji",
               "🧩 🌊\n"
               "🐍 🌟 🔢 🟰 9\n"
               "📤 🌟\n");
    write_manifest(b, "b", {{"c", "vendor/c"}});
    write_text(b / "src/main.emoji",
               "🧩 🌲\n"
               "🔗 📜pkg:c/src/main.emoji📜\n"
               "🛠️ 🍏 🫴 🤲\n"
               "📦 🌟\n"
               "🏁\n"
               "📤 🍏\n");
    write_manifest(app, "app", {{"b", "deps/b"}});

    write_text(app / "src/main.emoji",
               "🧩 🚀\n"
               "🔗 📜pkg:c/src/main.emoji📜\n");
    expect_error([&] { (void)emojineer::compile_file(app / "src/main.emoji", {}, app); },
                 "does not declare direct dependency 'c'");

    write_text(app / "src/main.emoji",
               "🧩 🚀\n"
               "🔗 📜../deps/b/src/main.emoji📜\n");
    expect_error([&] { (void)emojineer::compile_file(app / "src/main.emoji", {}, app); },
                 "crosses a package boundary");

    write_text(app / "src/main.emoji",
               "🧩 🚀\n"
               "🔗 📜pkg:b/vendor/c/src/main.emoji📜\n");
    expect_error([&] { (void)emojineer::compile_file(app / "src/main.emoji", {}, app); },
                 "targets nested package 'c'");

    write_manifest(app, "app", {{"b", "deps/b"}, {"c", "deps/b/vendor/c"}});
    write_text(app / "src/main.emoji",
               "🧩 🚀\n"
               "🔗 📜pkg:c/src/main.emoji📜\n"
               "📝 🌟\n");
    require(execute(emojineer::compile_file(app / "src/main.emoji", {}, app)) == "9\n",
            "independently declared nested package should be importable by its own coordinate");
}

void test_missing_and_malformed_package_coordinates() {
    TempRoot root("missing");
    const auto app = root.path / "app";
    const auto mathkit = app / "deps/mathkit";
    write_mathkit(mathkit);
    write_manifest(app, "app", {{"mathkit", "deps/mathkit"}});

    write_text(app / "src/main.emoji", "🧩 🚀\n🔗 📜pkg:mathkit📜\n");
    expect_error([&] { (void)emojineer::compile_file(app / "src/main.emoji", {}, app); },
                 "pkg:<dependency>/<module>.emoji");

    write_text(app / "src/main.emoji",
               "🧩 🚀\n🔗 📜pkg:mathkit/src/missing.emoji📜\n");
    expect_error([&] { (void)emojineer::compile_file(app / "src/main.emoji", {}, app); },
                 "does not exist");

    TempRoot loose("loose");
    write_text(loose.path / "main.emoji",
               "🧩 🚀\n🔗 📜pkg:mathkit/src/main.emoji📜\n");
    expect_error([&] { (void)emojineer::compile_file(loose.path / "main.emoji", {}, loose.path); },
                 "require an enclosing emojineer.toml package graph");
}

void test_module_and_package_cycles_have_distinct_diagnostics() {
    TempRoot module_root("module-cycle");
    const auto app = module_root.path / "app";
    const auto b = app / "deps/b";
    write_manifest(b, "b", {}, "src/a.emoji");
    write_text(b / "src/a.emoji", "🧩 🌲\n🔗 📜b.emoji📜\n");
    write_text(b / "src/b.emoji", "🧩 🌊\n🔗 📜a.emoji📜\n");
    write_manifest(app, "app", {{"b", "deps/b"}});
    write_text(app / "src/main.emoji", "🧩 🚀\n🔗 📜pkg:b/src/a.emoji📜\n");
    const auto module_error = expect_error(
        [&] { (void)emojineer::compile_file(app / "src/main.emoji", {}, app); },
        "cyclic module import");
    require(module_error.find("pkg:b/") != std::string::npos,
            "cross-package module cycle should use package-qualified identities");

    TempRoot package_root("package-cycle");
    const auto cycle_app = package_root.path / "app";
    const auto cycle_b = cycle_app / "deps/b";
    write_manifest(cycle_app, "app", {{"b", "deps/b"}});
    write_text(cycle_app / "src/main.emoji", "🧩 🚀\n🔗 📜pkg:b/src/main.emoji📜\n");
    write_manifest(cycle_b, "b", {{"app", "../.."}});
    write_text(cycle_b / "src/main.emoji", "🧩 🌲\n");
    expect_error([&] { (void)emojineer::compile_file(cycle_app / "src/main.emoji", {}, cycle_app); },
                 "cyclic package dependency");
}

void test_duplicate_module_names_across_packages_are_rejected() {
    TempRoot root("duplicate-module-name");
    const auto app = root.path / "app";
    const auto a = app / "deps/a";
    const auto b = app / "deps/b";
    write_manifest(a, "a");
    write_manifest(b, "b");
    write_text(a / "src/main.emoji", "🧩 🌲\n");
    write_text(b / "src/main.emoji", "🧩 🌲\n");
    write_manifest(app, "app", {{"a", "deps/a"}, {"b", "deps/b"}});
    write_text(app / "src/main.emoji",
               "🧩 🚀\n"
               "🔗 📜pkg:a/src/main.emoji📜\n"
               "🔗 📜pkg:b/src/main.emoji📜\n");
    expect_error([&] { (void)emojineer::compile_file(app / "src/main.emoji", {}, app); },
                 "duplicate 🧩 module name");
}

void write_portable_graph(const std::filesystem::path& root) {
    const auto app = root / "app";
    const auto lib = app / "deps/lib";
    write_manifest(lib, "lib");
    write_text(lib / "src/main.emoji",
               "🧩 🌲\n"
               "🛠️ 🍏 🫴 🍎 🤲\n"
               "📦 🍎 ➕ 2\n"
               "🏁\n"
               "📤 🍏\n");
    write_manifest(app, "app", {{"lib", "deps/lib"}});
    write_text(app / "src/main.emoji",
               "🧩 🚀\n"
               "🔗 📜pkg:lib/src/main.emoji📜\n"
               "📝 🍏 🫴 3 🤲\n");
}

void test_package_identities_are_checkout_portable() {
    TempRoot first("portable-a");
    TempRoot second("portable-b");
    write_portable_graph(first.path);
    write_portable_graph(second.path);

    const auto first_app = first.path / "app";
    const auto second_app = second.path / "app";
    const auto a = emojineer::compile_file(first_app / "src/main.emoji", {}, first_app);
    const auto b = emojineer::compile_file(second_app / "src/main.emoji", {}, second_app);
    require(bytecode_bytes(a) == bytecode_bytes(b),
            "pkg: module identities must not depend on absolute checkout location");
    require(execute(a) == "5\n", "portable package graph should execute normally");
}

} // namespace

int main() {
    try {
        test_direct_declared_package_import_and_project_check();
        test_transitive_dependency_is_not_ambient_and_local_paths_cannot_cross_packages();
        test_missing_and_malformed_package_coordinates();
        test_module_and_package_cycles_have_distinct_diagnostics();
        test_duplicate_module_names_across_packages_are_rejected();
        test_package_identities_are_checkout_portable();
        std::cout << "✅ package import tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
