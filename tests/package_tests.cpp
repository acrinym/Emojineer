#include "emojineer/hash.hpp"
#include "emojineer/package.hpp"
#include "emojineer/project.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("package test failed: " + message);
}

std::filesystem::path temp_root(const std::string& suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("emojineer-package-" + suffix + "-" + std::to_string(nonce));
}

void write_source(const std::filesystem::path& path, const std::string& source) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write test source");
    output << source;
}

void test_sha256_vectors() {
    require(emojineer::sha256_hex("") ==
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "SHA-256 empty-string vector should match");
    require(emojineer::sha256_hex("abc") ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 abc vector should match");
}

void test_recursive_graph_and_content_ownership() {
    const auto root = temp_root("graph");
    const auto b = root / "deps/b";
    const auto c = root / "deps/c";
    std::filesystem::remove_all(root);

    emojineer::initialize_project(root, "app");
    emojineer::initialize_project(b, "b");
    emojineer::initialize_project(c, "c");

    emojineer::add_project_dependency(b, "c", "../c");
    emojineer::add_project_dependency(root, "b", "deps/b");

    const auto first = emojineer::resolve_package_graph(root);
    require(first.packages.size() == 3, "recursive graph should contain app, b, and c exactly once");
    require(first.find("app") != nullptr, "root package should be addressable by name");
    require(first.find("b") != nullptr && first.find("c") != nullptr,
            "transitive packages should be addressable by name");
    require(first.find("b")->dependencies.size() == 1 &&
                first.find("b")->dependencies.front() == "c",
            "resolved direct dependency names should be recorded");

    const std::string app_hash = first.find("app")->content_sha256;
    const std::string b_hash = first.find("b")->content_sha256;
    const std::string c_hash = first.find("c")->content_sha256;

    write_source(c / "src/main.emoji", "📝 📜changed dependency source📜\n");
    const auto second = emojineer::resolve_package_graph(root);
    require(second.find("c")->content_sha256 != c_hash,
            "changing c source should change c content hash");
    require(second.find("b")->content_sha256 == b_hash,
            "dependency source should not be folded into its parent package hash");
    require(second.find("app")->content_sha256 == app_hash,
            "transitive dependency source inside app root should not pollute app package hash");

    std::filesystem::remove_all(root);
}

void test_independent_nested_package_is_excluded_from_container_hash() {
    const auto root = temp_root("nested-independent");
    const auto b = root / "deps/b";
    const auto c = b / "vendor/c";
    std::filesystem::remove_all(root);

    emojineer::initialize_project(root, "app");
    emojineer::initialize_project(b, "b");
    emojineer::initialize_project(c, "c");

    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    manifest.dependencies = {{"b", std::filesystem::path("deps/b")},
                             {"c", std::filesystem::path("deps/b/vendor/c")}};

    const auto first = emojineer::resolve_package_graph(root, manifest);
    require(first.find("b") != nullptr && first.find("c") != nullptr,
            "root should resolve both independently declared packages");
    require(first.find("b")->dependencies.empty(),
            "container package b must not implicitly own or depend on nested c");

    const std::string app_hash = first.find("app")->content_sha256;
    const std::string b_hash = first.find("b")->content_sha256;
    const std::string c_hash = first.find("c")->content_sha256;

    write_source(c / "src/main.emoji", "📝 📜independent nested package changed📜\n");
    const auto second = emojineer::resolve_package_graph(root, manifest);
    require(second.find("c")->content_sha256 != c_hash,
            "nested c source change should change c content hash");
    require(second.find("b")->content_sha256 == b_hash,
            "physically nested c source must not pollute container b hash when b does not depend on c");
    require(second.find("app")->content_sha256 == app_hash,
            "nested dependency source must remain excluded from root app hash");

    std::filesystem::remove_all(root);
}

void test_dependency_cycle_rejected_before_manifest_write() {
    const auto root = temp_root("cycle");
    const auto b = root / "deps/b";
    std::filesystem::remove_all(root);

    emojineer::initialize_project(root, "app");
    emojineer::initialize_project(b, "b");
    emojineer::add_project_dependency(b, "app", "../..");

    bool rejected = false;
    try {
        emojineer::add_project_dependency(root, "b", "deps/b");
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("cyclic package dependency") != std::string::npos;
    }
    require(rejected, "cyclic path dependencies should be rejected");

    const auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    require(manifest.dependencies.empty(),
            "failed add must not write the candidate dependency into the root manifest");

    std::filesystem::remove_all(root);
}

void test_dependency_name_must_match_target_package() {
    const auto root = temp_root("name-mismatch");
    const auto dep = root / "dep";
    std::filesystem::remove_all(root);

    emojineer::initialize_project(root, "app");
    emojineer::initialize_project(dep, "actual_name");

    bool rejected = false;
    try {
        emojineer::add_project_dependency(root, "claimed_name", "dep");
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("points to package 'actual_name'") != std::string::npos;
    }
    require(rejected, "dependency key should match the target package name");

    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    try {
        test_sha256_vectors();
        test_recursive_graph_and_content_ownership();
        test_independent_nested_package_is_excluded_from_container_hash();
        test_dependency_cycle_rejected_before_manifest_write();
        test_dependency_name_must_match_target_package();
        std::cout << "✅ package dependency tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
