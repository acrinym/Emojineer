#include "emojineer/package.hpp"
#include "emojineer/package_report.hpp"
#include "emojineer/project.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("package report test failed: " + message);
}

struct TempRoot {
    std::filesystem::path path;
    explicit TempRoot(const std::string& suffix) {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("emojineer-package-report-" + suffix + "-" + std::to_string(nonce));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempRoot() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void make_chain(const std::filesystem::path& root) {
    const auto app = root / "app";
    const auto b = app / "deps/b";
    const auto c = app / "deps/c";
    emojineer::initialize_project(app, "app");
    emojineer::initialize_project(b, "b");
    emojineer::initialize_project(c, "c");
    emojineer::add_project_dependency(b, "c", "../c");
    emojineer::add_project_dependency(app, "b", "deps/b");
}

void test_report_relations_paths_and_hashes() {
    TempRoot root("relations");
    make_chain(root.path);
    const auto app = root.path / "app";

    const auto report = emojineer::build_package_graph_report(emojineer::resolve_package_graph(app));
    require(report.root_name == "app", "root package name should be preserved");
    require(report.packages.size() == 3, "chain should report exactly three packages");

    const auto* app_row = report.find("app");
    const auto* b_row = report.find("b");
    const auto* c_row = report.find("c");
    require(app_row && b_row && c_row, "all packages should be addressable in the report");
    require(app_row->relation == emojineer::PackageRelation::Root,
            "root package should be classified as root");
    require(b_row->relation == emojineer::PackageRelation::Direct,
            "root dependency should be classified as direct");
    require(c_row->relation == emojineer::PackageRelation::Transitive,
            "dependency-of-dependency should be classified as transitive");
    require(app_row->path == ".", "root package path should be checkout-relative dot");
    require(b_row->path == "deps/b", "direct package path should be checkout-relative");
    require(c_row->path == "deps/c", "transitive package path should be checkout-relative");
    require(app_row->content_sha256.size() == 64 && b_row->content_sha256.size() == 64 &&
                c_row->content_sha256.size() == 64,
            "every package should expose its full SHA-256 content identity");

    const auto tree = emojineer::render_package_tree(report, true);
    require(tree.find("app@0.1.0 [root] path=. entry=src/main.emoji sha256=") != std::string::npos,
            "human tree should identify the root package and hash");
    require(tree.find("b@0.1.0 [direct] path=deps/b") != std::string::npos,
            "human tree should label direct dependencies");
    require(tree.find("c@0.1.0 [transitive] path=deps/c") != std::string::npos,
            "human tree should label transitive dependencies");
    require(tree.find(app.string()) == std::string::npos,
            "human tree must not leak the absolute checkout path");
}

void make_shared_graph(const std::filesystem::path& root) {
    const auto app = root / "app";
    const auto b = app / "deps/b";
    const auto c = app / "deps/c";
    const auto d = app / "deps/d";
    emojineer::initialize_project(app, "app");
    emojineer::initialize_project(b, "b");
    emojineer::initialize_project(c, "c");
    emojineer::initialize_project(d, "d");
    emojineer::add_project_dependency(b, "c", "../c");
    emojineer::add_project_dependency(d, "c", "../c");
    emojineer::add_project_dependency(app, "b", "deps/b");
    emojineer::add_project_dependency(app, "d", "deps/d");
}

void test_shared_dag_tree_is_finite_and_deterministic() {
    TempRoot root("shared");
    make_shared_graph(root.path);
    const auto report = emojineer::build_package_graph_report(
        emojineer::resolve_package_graph(root.path / "app"));
    const auto first = emojineer::render_package_tree(report, false);
    const auto second = emojineer::render_package_tree(report, false);
    require(first == second, "human graph rendering should be deterministic");
    require(first.find("c@0.1.0 [transitive]") != std::string::npos,
            "shared transitive package should be visible");
    require(first.find("(shared)") != std::string::npos,
            "second expansion of a shared dependency should be marked and stopped");
}

void test_json_is_checkout_portable_and_deterministic() {
    TempRoot first("portable-a");
    TempRoot second("portable-b");
    make_chain(first.path);
    make_chain(second.path);

    const auto a = emojineer::render_package_graph_json(
        emojineer::build_package_graph_report(
            emojineer::resolve_package_graph(first.path / "app")));
    const auto b = emojineer::render_package_graph_json(
        emojineer::build_package_graph_report(
            emojineer::resolve_package_graph(second.path / "app")));

    require(a == b, "machine-readable package graph should be identical across checkout roots");
    require(a.find("\"schema\": \"emojineer.package-graph.v1\"") != std::string::npos,
            "JSON should carry a stable schema identifier");
    require(a.find("\"relation\": \"direct\"") != std::string::npos,
            "JSON should expose direct/transitive relation metadata");
    require(a.find("\"content_sha256\": \"") != std::string::npos,
            "JSON should expose full package content identities");
    require(a.find(first.path.string()) == std::string::npos &&
                a.find(second.path.string()) == std::string::npos,
            "JSON must not encode absolute checkout roots");
}

void test_sibling_dependency_uses_portable_parent_path() {
    TempRoot root("sibling");
    const auto app = root.path / "app";
    const auto lib = root.path / "lib";
    emojineer::initialize_project(app, "app");
    emojineer::initialize_project(lib, "lib");
    emojineer::add_project_dependency(app, "lib", "../lib");

    const auto report = emojineer::build_package_graph_report(emojineer::resolve_package_graph(app));
    const auto* lib_row = report.find("lib");
    require(lib_row && lib_row->path == "../lib",
            "dependency outside the root directory should retain a portable relative path");
    require(!std::filesystem::path(lib_row->path).is_absolute(),
            "reported dependency path must never be absolute");
}

} // namespace

int main() {
    try {
        test_report_relations_paths_and_hashes();
        test_shared_dag_tree_is_finite_and_deterministic();
        test_json_is_checkout_portable_and_deterministic();
        test_sibling_dependency_uses_portable_parent_path();
        std::cout << "✅ package report tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
