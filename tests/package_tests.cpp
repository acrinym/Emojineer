#include "emojineer/hash.hpp"
#include "emojineer/package.hpp"
#include "emojineer/project.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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

void refresh_lock_manifest_hash(const std::filesystem::path& root) {
    const auto lock_path = root / "emojineer.lock";
    if (!std::filesystem::exists(lock_path)) return;
    const auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    const auto manifest_hash = emojineer::project_manifest_hash(manifest);
    std::ifstream input(lock_path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read package test lock");
    std::string lock_text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (lock_text.find("lock_version = 3") == std::string::npos) {
        const std::string legacy = "version = \"3\"\n";
        const auto legacy_pos = lock_text.find(legacy);
        if (legacy_pos != std::string::npos) lock_text.replace(legacy_pos, legacy.size(), "lock_version = 3\n");
        else lock_text.insert(0, "lock_version = 3\n");
    }
    const std::string artifact_prefix = "artifact_sha256 = \"";
    std::size_t artifact_pos = 0;
    while ((artifact_pos = lock_text.find(artifact_prefix, artifact_pos)) != std::string::npos) {
        const auto value_begin = artifact_pos + artifact_prefix.size();
        const auto value_end = lock_text.find('\"', value_begin);
        if (value_end == std::string::npos) throw std::runtime_error("package test lock has malformed artifact_sha256");
        const auto value = lock_text.substr(value_begin, value_end - value_begin);
        if (value.size() != 64) {
            const auto normalized = emojineer::sha256_hex(value);
            lock_text.replace(value_begin, value.size(), normalized);
            artifact_pos = value_begin + normalized.size();
        } else {
            artifact_pos = value_end + 1;
        }
    }
    const std::string prefix = "manifest_hash = \"";
    const auto begin = lock_text.find(prefix);
    if (begin == std::string::npos) throw std::runtime_error("package test lock is missing manifest_hash");
    const auto value_begin = begin + prefix.size();
    const auto value_end = lock_text.find('\"', value_begin);
    if (value_end == std::string::npos) throw std::runtime_error("package test lock has malformed manifest_hash");
    lock_text.replace(value_begin, value_end - value_begin, manifest_hash);
    {
        std::ofstream output(lock_path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot rewrite package test lock");
        output << lock_text;
    }

    auto lock = emojineer::load_project_lock(lock_path);
    lock.version = "3";
    lock.manifest_hash = manifest_hash;
    for (auto& dependency : lock.dependencies) {
        if (dependency.source != emojineer::LockSourceKind::Registry || !dependency.store_path) continue;
        const auto dep_manifest_path = std::filesystem::path(*dependency.store_path) / "emojineer.toml";
        if (!std::filesystem::exists(dep_manifest_path)) continue;
        try {
            const auto dep_manifest = emojineer::load_project_manifest(dep_manifest_path);
            dependency.dependencies.clear();
            for (const auto& nested : dep_manifest.dependencies) {
                dependency.dependencies.push_back(nested.name);
            }
        } catch (const std::exception&) {
            // Corrupt-manifest regression fixtures intentionally fail later at the production seam.
        }
    }
    {
        std::ofstream output(lock_path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot canonicalize package test lock");
        output << emojineer::canonical_lock_text(lock);
    }
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
    refresh_lock_manifest_hash(root);
    manifest.dependencies = {
        {"b", emojineer::DependencyKind::Path, std::filesystem::path("deps/b"), {}, {}},
        {"c", emojineer::DependencyKind::Path, std::filesystem::path("deps/b/vendor/c"), {}, {}}
    };

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

void test_registry_dependency_offline_resolution() {
    const auto root = temp_root("registry-offline");
    const auto store_root = root / ".emojineer" / "packages";
    std::filesystem::remove_all(root);

    // Create project with a registry dependency
    emojineer::initialize_project(root, "app");
    
    // Create materialized package store
    std::filesystem::create_directories(store_root / "origin" / "mylib" / "1.0.0" / "abc123");
    auto pkg_path = store_root / "origin" / "mylib" / "1.0.0" / "abc123";
    
    // Create the materialized package manifest
    std::ofstream manifest_out(pkg_path / "emojineer.toml");
    manifest_out << "[package]\n";
    manifest_out << "name = \"mylib\"\n";
    manifest_out << "version = \"1.0.0\"\n";
    manifest_out << "entry = \"src/main.emoji\"\n";
    manifest_out.close();
    
    // Create source directory
    std::filesystem::create_directories(pkg_path / "src");
    std::ofstream source_out(pkg_path / "src" / "main.emoji");
    source_out << "📝 🌟\n";
    source_out.close();
    
    // Compute the expected hash for this package using production authority
    auto pkg_manifest = emojineer::load_project_manifest(pkg_path / "emojineer.toml");
    std::string computed_hash = emojineer::compute_registry_package_hash(pkg_path, pkg_manifest);
    
    // Create lock file with the registry dependency
    std::ofstream lock_out(root / "emojineer.lock");
    lock_out << "version = \"3\"\n";
    lock_out << "manifest_hash = \"abc123def456\"\n";
    lock_out << "\n";
    lock_out << "[[registry]]\n";
    lock_out << "alias = \"origin\"\n";
    lock_out << "id = \"origin-id\"\n";
    lock_out << "endpoint = \"https://registry.example.com\"\n";
    lock_out << "\n";
    lock_out << "[[dependency]]\n";
    lock_out << "source = \"registry\"\n";
    lock_out << "name = \"mylib\"\n";
    lock_out << "version = \"1.0.0\"\n";
    lock_out << "registry = \"origin\"\n";
    lock_out << "registry_id = \"origin-id\"\n";
    lock_out << "registry_endpoint = \"https://registry.example.com\"\n";
    lock_out << "requirement = \"^1.0.0\"\n";
    lock_out << "artifact_sha256 = \"abc123\"\n";
    lock_out << "store_path = \"" << pkg_path.generic_string() << "\"\n";
    lock_out << "content_sha256 = \"" << computed_hash << "\"\n";
    lock_out.close();
    
    // Create manifest with registry dependency manually
    std::ofstream manifest_file(root / "emojineer.toml");
    manifest_file << "[package]\n";
    manifest_file << "name = \"app\"\n";
    manifest_file << "version = \"0.1.0\"\n";
    manifest_file << "entry = \"src/main.emoji\"\n";
    manifest_file << "\n";
    manifest_file << "[registries]\n";
    manifest_file << "origin = \"https://registry.example.com\"\n";
    manifest_file << "\n";
    manifest_file << "[dependencies]\n";
    manifest_file << "mylib = \"registry:origin:^1.0.0\"\n";
    manifest_file.close();
    
    // Load manifest and call resolve_package_graph with offline=true
    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    refresh_lock_manifest_hash(root);
    auto graph = emojineer::resolve_package_graph(root, manifest, store_root, true);
    
    // Verify the registry package is properly resolved
    require(graph.packages.size() == 2, "graph should contain app and mylib");
    require(graph.find("app") != nullptr, "root package should be addressable");
    require(graph.find("mylib") != nullptr, "registry package should be resolved");
    
    const auto* mylib = graph.find("mylib");
    require(mylib->source_kind == emojineer::DependencyKind::Registry, 
            "mylib should be resolved as registry package");
    require(mylib->version == "1.0.0", "mylib version should match");
    require(mylib->root == pkg_path, "mylib root should be the materialized store path");
    require(mylib->registry_alias == "origin", "mylib registry alias should be set");
    require(mylib->registry_id == "origin-id", "mylib registry id should be set");
    require(mylib->store_path == pkg_path, "mylib store_path should be set");
    
    // Verify app depends on mylib
    const auto* app = graph.find("app");
    require(app->dependencies.size() == 1, "app should have one dependency");
    require(app->dependencies[0] == "mylib", "app should depend on mylib");
    
    std::filesystem::remove_all(root);
}

void test_registry_dependency_offline_without_lock() {
    const auto root = temp_root("registry-no-lock");
    const auto store_root = root / ".emojineer" / "packages";
    std::filesystem::remove_all(root);

    // Create project with a registry dependency but no lock file
    emojineer::initialize_project(root, "app");
    
    // Create manifest with registry dependency manually
    std::ofstream manifest_file(root / "emojineer.toml");
    manifest_file << "[package]\n";
    manifest_file << "name = \"app\"\n";
    manifest_file << "version = \"0.1.0\"\n";
    manifest_file << "entry = \"src/main.emoji\"\n";
    manifest_file << "\n";
    manifest_file << "[registries]\n";
    manifest_file << "origin = \"https://registry.example.com\"\n";
    manifest_file << "\n";
    manifest_file << "[dependencies]\n";
    manifest_file << "mylib = \"registry:origin:^1.0.0\"\n";
    manifest_file.close();
    
    // Load manifest and call resolve_package_graph with offline=true but no lock
    // In offline mode without a lock entry, this should throw an exception
    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    refresh_lock_manifest_hash(root);
    bool caught_error = false;
    std::string error_msg;
    try {
        auto graph = emojineer::resolve_package_graph(root, manifest, store_root, true);
    } catch (const std::exception& e) {
        caught_error = true;
        error_msg = e.what();
    }
    
    require(caught_error, "offline mode should fail without lock entry");
    require(error_msg.find("offline") != std::string::npos, "error should identify offline resolution");
    require(error_msg.find("lock") != std::string::npos,
            "error should identify the missing lock requirement");
    
    std::filesystem::remove_all(root);
}

void test_registry_package_unique_names_no_duplicates() {
    // Test that registry packages are not duplicated in the graph
    const auto root = temp_root("registry-unique");
    const auto store_root = root / ".emojineer" / "packages";
    std::filesystem::remove_all(root);

    // Create project with a registry dependency
    emojineer::initialize_project(root, "app");
    
    // Create materialized package store
    std::filesystem::create_directories(store_root / "origin" / "mylib" / "1.0.0" / "abc123");
    auto pkg_path = store_root / "origin" / "mylib" / "1.0.0" / "abc123";
    
    // Create the materialized package manifest
    std::ofstream manifest_out(pkg_path / "emojineer.toml");
    manifest_out << "[package]\n";
    manifest_out << "name = \"mylib\"\n";
    manifest_out << "version = \"1.0.0\"\n";
    manifest_out << "entry = \"src/main.emoji\"\n";
    manifest_out.close();
    
    // Create source directory
    std::filesystem::create_directories(pkg_path / "src");
    std::ofstream source_out(pkg_path / "src" / "main.emoji");
    source_out << "📝 🍎 🫴 🤲\n";
    source_out.close();
    
    // Compute the expected hash for this package using production authority
    auto pkg_manifest = emojineer::load_project_manifest(pkg_path / "emojineer.toml");
    std::string computed_hash = emojineer::compute_registry_package_hash(pkg_path, pkg_manifest);
    
    // Create lock file with the registry dependency
    std::ofstream lock_out(root / "emojineer.lock");
    lock_out << "version = \"3\"\n";
    lock_out << "manifest_hash = \"abc123def456\"\n";
    lock_out << "\n";
    lock_out << "[[registry]]\n";
    lock_out << "alias = \"origin\"\n";
    lock_out << "id = \"origin-id\"\n";
    lock_out << "endpoint = \"https://registry.example.com\"\n";
    lock_out << "\n";
    lock_out << "[[dependency]]\n";
    lock_out << "source = \"registry\"\n";
    lock_out << "name = \"mylib\"\n";
    lock_out << "version = \"1.0.0\"\n";
    lock_out << "registry = \"origin\"\n";
    lock_out << "registry_id = \"origin-id\"\n";
    lock_out << "registry_endpoint = \"https://registry.example.com\"\n";
    lock_out << "requirement = \"^1.0.0\"\n";
    lock_out << "artifact_sha256 = \"abc123\"\n";
    lock_out << "store_path = \"" << pkg_path.generic_string() << "\"\n";
    lock_out << "content_sha256 = \"" << computed_hash << "\"\n";
    lock_out.close();
    
    // Create manifest with registry dependency
    std::ofstream manifest_file(root / "emojineer.toml");
    manifest_file << "[package]\n";
    manifest_file << "name = \"app\"\n";
    manifest_file << "version = \"0.1.0\"\n";
    manifest_file << "entry = \"src/main.emoji\"\n";
    manifest_file << "\n";
    manifest_file << "[registries]\n";
    manifest_file << "origin = \"https://registry.example.com\"\n";
    manifest_file << "\n";
    manifest_file << "[dependencies]\n";
    manifest_file << "mylib = \"registry:origin:^1.0.0\"\n";
    manifest_file.close();
    
    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    refresh_lock_manifest_hash(root);
    auto graph = emojineer::resolve_package_graph(root, manifest, store_root, true);
    
    // Verify unique package names - no duplicates
    require(graph.packages.size() == 2, "graph should contain exactly 2 packages (no duplicates)");
    
    // Count occurrences of each package name
    std::map<std::string, int> name_counts;
    for (const auto& pkg : graph.packages) {
        name_counts[pkg.name]++;
    }
    for (const auto& [name, count] : name_counts) {
        require(count == 1, "package '" + name + "' should appear exactly once, got " + std::to_string(count));
    }
    
    std::filesystem::remove_all(root);
}

void test_registry_package_source_kind_preserved() {
    // Test that registry packages maintain their Registry source kind (not converted to Path)
    const auto root = temp_root("registry-source-kind");
    const auto store_root = root / ".emojineer" / "packages";
    std::filesystem::remove_all(root);

    emojineer::initialize_project(root, "app");
    
    // Create materialized package store
    std::filesystem::create_directories(store_root / "origin" / "mylib" / "1.0.0" / "abc123");
    auto pkg_path = store_root / "origin" / "mylib" / "1.0.0" / "abc123";
    
    std::ofstream manifest_out(pkg_path / "emojineer.toml");
    manifest_out << "[package]\n";
    manifest_out << "name = \"mylib\"\n";
    manifest_out << "version = \"1.0.0\"\n";
    manifest_out << "entry = \"src/main.emoji\"\n";
    manifest_out.close();
    
    std::filesystem::create_directories(pkg_path / "src");
    std::ofstream source_out(pkg_path / "src" / "main.emoji");
    source_out << "📝 📜shared-init📜\n";
    source_out.close();
    
    // Compute the expected hash for this package using production authority
    auto pkg_manifest = emojineer::load_project_manifest(pkg_path / "emojineer.toml");
    std::string computed_hash = emojineer::compute_registry_package_hash(pkg_path, pkg_manifest);
    
    // Create lock file
    std::ofstream lock_out(root / "emojineer.lock");
    lock_out << "version = \"3\"\n";
    lock_out << "manifest_hash = \"abc123def456\"\n";
    lock_out << "\n";
    lock_out << "[[registry]]\n";
    lock_out << "alias = \"origin\"\n";
    lock_out << "id = \"origin-id\"\n";
    lock_out << "endpoint = \"https://registry.example.com\"\n";
    lock_out << "\n";
    lock_out << "[[dependency]]\n";
    lock_out << "source = \"registry\"\n";
    lock_out << "name = \"mylib\"\n";
    lock_out << "version = \"1.0.0\"\n";
    lock_out << "registry = \"origin\"\n";
    lock_out << "registry_id = \"origin-id\"\n";
    lock_out << "registry_endpoint = \"https://registry.example.com\"\n";
    lock_out << "requirement = \"^1.0.0\"\n";
    lock_out << "artifact_sha256 = \"abc123\"\n";
    lock_out << "store_path = \"" << pkg_path.generic_string() << "\"\n";
    lock_out << "content_sha256 = \"" << computed_hash << "\"\n";
    lock_out.close();
    
    std::ofstream manifest_file(root / "emojineer.toml");
    manifest_file << "[package]\n";
    manifest_file << "name = \"app\"\n";
    manifest_file << "version = \"0.1.0\"\n";
    manifest_file << "entry = \"src/main.emoji\"\n";
    manifest_file << "\n";
    manifest_file << "[registries]\n";
    manifest_file << "origin = \"https://registry.example.com\"\n";
    manifest_file << "\n";
    manifest_file << "[dependencies]\n";
    manifest_file << "mylib = \"registry:origin:^1.0.0\"\n";
    manifest_file.close();
    
    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    refresh_lock_manifest_hash(root);
    auto graph = emojineer::resolve_package_graph(root, manifest, store_root, true);
    
    // Verify mylib is Registry, not Path
    const auto* mylib = graph.find("mylib");
    require(mylib != nullptr, "mylib should be in the graph");
    require(mylib->source_kind == emojineer::DependencyKind::Registry, 
            "mylib should be Registry, not Path");
    
    // Verify app is Path (root is always path)
    const auto* app = graph.find("app");
    require(app != nullptr, "app should be in the graph");
    require(app->source_kind == emojineer::DependencyKind::Path, 
            "app (root) should be Path");
    
    std::filesystem::remove_all(root);
}

void test_transitive_registry_child_preserves_source_kind() {
    // Test that transitive registry dependencies also maintain Registry source kind
    const auto root = temp_root("transitive-registry");
    const auto store_root = root / ".emojineer" / "packages";
    std::filesystem::remove_all(root);

    emojineer::initialize_project(root, "app");
    
    // Create materialized package store with transitive deps
    std::filesystem::create_directories(store_root / "origin" / "mylib" / "1.0.0" / "abc123");
    auto mylib_path = store_root / "origin" / "mylib" / "1.0.0" / "abc123";
    
    std::ofstream mylib_manifest(mylib_path / "emojineer.toml");
    mylib_manifest << "[package]\n";
    mylib_manifest << "name = \"mylib\"\n";
    mylib_manifest << "version = \"1.0.0\"\n";
    mylib_manifest << "entry = \"src/main.emoji\"\n";
    // mylib depends on transitive-lib
    mylib_manifest << "\n[registries]\n";
    mylib_manifest << "origin = \"https://registry.example.com\"\n";
    mylib_manifest << "\n[dependencies]\n";
    mylib_manifest << "transitive-lib = \"registry:origin:^2.0.0\"\n";
    mylib_manifest.close();
    
    std::filesystem::create_directories(mylib_path / "src");
    std::ofstream mylib_source(mylib_path / "src" / "main.emoji");
    mylib_source << "📝 🧠 🫴 🤲\n";
    mylib_source.close();
    
    // Create transitive library in store
    std::filesystem::create_directories(store_root / "origin" / "transitive-lib" / "2.0.0" / "xyz789");
    auto transitive_path = store_root / "origin" / "transitive-lib" / "2.0.0" / "xyz789";
    
    std::ofstream transitive_manifest(transitive_path / "emojineer.toml");
    transitive_manifest << "[package]\n";
    transitive_manifest << "name = \"transitive-lib\"\n";
    transitive_manifest << "version = \"2.0.0\"\n";
    transitive_manifest << "entry = \"src/main.emoji\"\n";
    transitive_manifest.close();
    
    std::filesystem::create_directories(transitive_path / "src");
    std::ofstream transitive_source(transitive_path / "src" / "main.emoji");
    transitive_source << "📝 🌑\n";
    transitive_source.close();
    
    // Compute the expected hashes for both packages using production authority
    auto loaded_mylib_manifest = emojineer::load_project_manifest(mylib_path / "emojineer.toml");
    std::string mylib_hash = emojineer::compute_registry_package_hash(mylib_path, loaded_mylib_manifest);
    
    auto loaded_transitive_manifest = emojineer::load_project_manifest(transitive_path / "emojineer.toml");
    std::string transitive_hash = emojineer::compute_registry_package_hash(transitive_path, loaded_transitive_manifest);
    
    // Create lock file with both direct and transitive deps
    std::ofstream lock_out(root / "emojineer.lock");
    lock_out << "version = \"3\"\n";
    lock_out << "manifest_hash = \"abc123def456\"\n";
    lock_out << "\n";
    lock_out << "[[registry]]\n";
    lock_out << "alias = \"origin\"\n";
    lock_out << "id = \"origin-id\"\n";
    lock_out << "endpoint = \"https://registry.example.com\"\n";
    lock_out << "\n";
    lock_out << "[[dependency]]\n";
    lock_out << "source = \"registry\"\n";
    lock_out << "name = \"mylib\"\n";
    lock_out << "version = \"1.0.0\"\n";
    lock_out << "registry = \"origin\"\n";
    lock_out << "registry_id = \"origin-id\"\n";
    lock_out << "registry_endpoint = \"https://registry.example.com\"\n";
    lock_out << "requirement = \"^1.0.0\"\n";
    lock_out << "artifact_sha256 = \"abc123\"\n";
    lock_out << "store_path = \"" << mylib_path.generic_string() << "\"\n";
    lock_out << "content_sha256 = \"" << mylib_hash << "\"\n";
    lock_out << "\n";
    lock_out << "[[dependency]]\n";
    lock_out << "source = \"registry\"\n";
    lock_out << "name = \"transitive-lib\"\n";
    lock_out << "version = \"2.0.0\"\n";
    lock_out << "registry = \"origin\"\n";
    lock_out << "registry_id = \"origin-id\"\n";
    lock_out << "registry_endpoint = \"https://registry.example.com\"\n";
    lock_out << "requirement = \"^2.0.0\"\n";
    lock_out << "artifact_sha256 = \"xyz789\"\n";
    lock_out << "store_path = \"" << transitive_path.generic_string() << "\"\n";
    lock_out << "content_sha256 = \"" << transitive_hash << "\"\n";
    lock_out.close();
    
    std::ofstream manifest_file(root / "emojineer.toml");
    manifest_file << "[package]\n";
    manifest_file << "name = \"app\"\n";
    manifest_file << "version = \"0.1.0\"\n";
    manifest_file << "entry = \"src/main.emoji\"\n";
    manifest_file << "\n";
    manifest_file << "[registries]\n";
    manifest_file << "origin = \"https://registry.example.com\"\n";
    manifest_file << "\n";
    manifest_file << "[dependencies]\n";
    manifest_file << "mylib = \"registry:origin:^1.0.0\"\n";
    manifest_file.close();
    
    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    refresh_lock_manifest_hash(root);
    auto graph = emojineer::resolve_package_graph(root, manifest, store_root, true);
    
    // Verify all packages exist and have correct source kinds
    require(graph.packages.size() == 3, "graph should contain app, mylib, and transitive-lib");
    
    const auto* app_pkg = graph.find("app");
    require(app_pkg != nullptr && app_pkg->source_kind == emojineer::DependencyKind::Path,
            "app should be Path");
    
    const auto* mylib_pkg = graph.find("mylib");
    require(mylib_pkg != nullptr && mylib_pkg->source_kind == emojineer::DependencyKind::Registry,
            "mylib should be Registry");
    
    const auto* transitive_pkg = graph.find("transitive-lib");
    require(transitive_pkg != nullptr && transitive_pkg->source_kind == emojineer::DependencyKind::Registry,
            "transitive-lib should be Registry (not converted to Path)");
    
    std::filesystem::remove_all(root);
}

void test_registry_package_rejects_path_dependency() {
    // Test that registry packages cannot have path dependencies
    const auto root = temp_root("registry-no-path-dep");
    const auto store_root = root / ".emojineer" / "packages";
    const auto local_dep = root / "local-dep";
    std::filesystem::remove_all(root);

    emojineer::initialize_project(root, "app");
    emojineer::initialize_project(local_dep, "local-dep");
    
    // Create materialized registry package that has a path dependency
    std::filesystem::create_directories(store_root / "origin" / "mylib" / "1.0.0" / "abc123");
    auto pkg_path = store_root / "origin" / "mylib" / "1.0.0" / "abc123";
    
    std::ofstream manifest_out(pkg_path / "emojineer.toml");
    manifest_out << "[package]\n";
    manifest_out << "name = \"mylib\"\n";
    manifest_out << "version = \"1.0.0\"\n";
    manifest_out << "entry = \"src/main.emoji\"\n";
    // mylib incorrectly has a path dependency - this should be rejected
    manifest_out << "\n[dependencies]\n";
    manifest_out << "local-dep = \"../local-dep\"\n";
    manifest_out.close();
    
    std::filesystem::create_directories(pkg_path / "src");
    std::ofstream source_out(pkg_path / "src" / "main.emoji");
    source_out << "📝 🌟\n";
    source_out.close();
    
    // Compute the expected hash for this package using production authority
    auto pkg_manifest = emojineer::load_project_manifest(pkg_path / "emojineer.toml");
    std::string computed_hash = emojineer::compute_registry_package_hash(pkg_path, pkg_manifest);
    
    // Create lock file
    std::ofstream lock_out(root / "emojineer.lock");
    lock_out << "version = \"3\"\n";
    lock_out << "manifest_hash = \"abc123def456\"\n";
    lock_out << "\n";
    lock_out << "[[registry]]\n";
    lock_out << "alias = \"origin\"\n";
    lock_out << "id = \"origin-id\"\n";
    lock_out << "endpoint = \"https://registry.example.com\"\n";
    lock_out << "\n";
    lock_out << "[[dependency]]\n";
    lock_out << "source = \"registry\"\n";
    lock_out << "name = \"mylib\"\n";
    lock_out << "version = \"1.0.0\"\n";
    lock_out << "registry = \"origin\"\n";
    lock_out << "registry_id = \"origin-id\"\n";
    lock_out << "registry_endpoint = \"https://registry.example.com\"\n";
    lock_out << "requirement = \"^1.0.0\"\n";
    lock_out << "artifact_sha256 = \"abc123\"\n";
    lock_out << "store_path = \"" << pkg_path.generic_string() << "\"\n";
    lock_out << "content_sha256 = \"" << computed_hash << "\"\n";
    lock_out.close();
    
    std::ofstream manifest_file(root / "emojineer.toml");
    manifest_file << "[package]\n";
    manifest_file << "name = \"app\"\n";
    manifest_file << "version = \"0.1.0\"\n";
    manifest_file << "entry = \"src/main.emoji\"\n";
    manifest_file << "\n";
    manifest_file << "[registries]\n";
    manifest_file << "origin = \"https://registry.example.com\"\n";
    manifest_file << "\n";
    manifest_file << "[dependencies]\n";
    manifest_file << "mylib = \"registry:origin:^1.0.0\"\n";
    manifest_file.close();
    
    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    refresh_lock_manifest_hash(root);
    
    // This should throw because registry package has path dependency
    bool rejected = false;
    std::string error_msg;
    try {
        auto graph = emojineer::resolve_package_graph(root, manifest, store_root, true);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("cannot have path dependency") != std::string::npos;
        error_msg = e.what();
    }
    require(rejected, "registry package with path dependency should be rejected, got: " + error_msg);
    
    std::filesystem::remove_all(root);
}

void test_registry_package_content_integrity_valid_hash_succeeds() {
    // Test that valid content SHA256 succeeds - packages with correct hash load successfully
    const auto root = temp_root("integrity-valid");
    const auto store_root = root / ".emojineer" / "packages";
    std::filesystem::remove_all(root);

    emojineer::initialize_project(root, "app");
    
    // Create materialized package store
    std::filesystem::create_directories(store_root / "origin" / "validpkg" / "1.0.0" / "abc123");
    auto pkg_path = store_root / "origin" / "validpkg" / "1.0.0" / "abc123";
    
    std::ofstream manifest_out(pkg_path / "emojineer.toml");
    manifest_out << "[package]\n";
    manifest_out << "name = \"validpkg\"\n";
    manifest_out << "version = \"1.0.0\"\n";
    manifest_out << "entry = \"src/main.emoji\"\n";
    manifest_out.close();
    
    std::filesystem::create_directories(pkg_path / "src");
    std::ofstream source_out(pkg_path / "src" / "main.emoji");
    source_out << "📝 🌟\n";
    source_out.close();
    
    // Compute the expected hash for this package using production authority
    auto pkg_manifest = emojineer::load_project_manifest(pkg_path / "emojineer.toml");
    std::string computed_hash = emojineer::compute_registry_package_hash(pkg_path, pkg_manifest);
    
    // Create lock file with correct content hash
    std::ofstream lock_out(root / "emojineer.lock");
    lock_out << "version = \"3\"\n";
    lock_out << "manifest_hash = \"abc123def456\"\n";
    lock_out << "\n";
    lock_out << "[[registry]]\n";
    lock_out << "alias = \"origin\"\n";
    lock_out << "id = \"origin-id\"\n";
    lock_out << "endpoint = \"https://registry.example.com\"\n";
    lock_out << "\n";
    lock_out << "[[dependency]]\n";
    lock_out << "source = \"registry\"\n";
    lock_out << "name = \"validpkg\"\n";
    lock_out << "version = \"1.0.0\"\n";
    lock_out << "registry = \"origin\"\n";
    lock_out << "registry_id = \"origin-id\"\n";
    lock_out << "registry_endpoint = \"https://registry.example.com\"\n";
    lock_out << "requirement = \"^1.0.0\"\n";
    lock_out << "artifact_sha256 = \"abc123\"\n";
    lock_out << "store_path = \"" << pkg_path.generic_string() << "\"\n";
    lock_out << "content_sha256 = \"" << computed_hash << "\"\n";
    lock_out.close();
    
    std::ofstream manifest_file(root / "emojineer.toml");
    manifest_file << "[package]\n";
    manifest_file << "name = \"app\"\n";
    manifest_file << "version = \"0.1.0\"\n";
    manifest_file << "entry = \"src/main.emoji\"\n";
    manifest_file << "\n";
    manifest_file << "[registries]\n";
    manifest_file << "origin = \"https://registry.example.com\"\n";
    manifest_file << "\n";
    manifest_file << "[dependencies]\n";
    manifest_file << "validpkg = \"registry:origin:^1.0.0\"\n";
    manifest_file.close();
    
    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    refresh_lock_manifest_hash(root);
    
    // This should succeed because the hash matches
    auto graph = emojineer::resolve_package_graph(root, manifest, store_root, true);
    
    require(graph.packages.size() == 2, "graph should contain app and validpkg");
    require(graph.find("validpkg") != nullptr, "validpkg should be in the graph");
    
    std::filesystem::remove_all(root);
}

void test_registry_package_content_integrity_hash_mismatch_rejected() {
    // Test that content SHA256 mismatch is rejected - mutate source after lock creation
    const auto root = temp_root("integrity-mismatch");
    const auto store_root = root / ".emojineer" / "packages";
    std::filesystem::remove_all(root);

    emojineer::initialize_project(root, "app");
    
    // Create materialized package store
    std::filesystem::create_directories(store_root / "origin" / "mutatepkg" / "1.0.0" / "abc123");
    auto pkg_path = store_root / "origin" / "mutatepkg" / "1.0.0" / "abc123";
    
    std::ofstream manifest_out(pkg_path / "emojineer.toml");
    manifest_out << "[package]\n";
    manifest_out << "name = \"mutatepkg\"\n";
    manifest_out << "version = \"1.0.0\"\n";
    manifest_out << "entry = \"src/main.emoji\"\n";
    manifest_out.close();
    
    std::filesystem::create_directories(pkg_path / "src");
    std::ofstream source_out(pkg_path / "src" / "main.emoji");
    source_out << "📝 🌟\n";
    source_out.close();
    
    // Compute the CORRECT hash for the original content using production authority
    auto pkg_manifest = emojineer::load_project_manifest(pkg_path / "emojineer.toml");
    std::string correct_hash = emojineer::compute_registry_package_hash(pkg_path, pkg_manifest);
    
    // Create lock file with CORRECT hash for original content
    std::ofstream lock_out(root / "emojineer.lock");
    lock_out << "version = \"3\"\n";
    lock_out << "manifest_hash = \"abc123def456\"\n";
    lock_out << "\n";
    lock_out << "[[registry]]\n";
    lock_out << "alias = \"origin\"\n";
    lock_out << "id = \"origin-id\"\n";
    lock_out << "endpoint = \"https://registry.example.com\"\n";
    lock_out << "\n";
    lock_out << "[[dependency]]\n";
    lock_out << "source = \"registry\"\n";
    lock_out << "name = \"mutatepkg\"\n";
    lock_out << "version = \"1.0.0\"\n";
    lock_out << "registry = \"origin\"\n";
    lock_out << "registry_id = \"origin-id\"\n";
    lock_out << "registry_endpoint = \"https://registry.example.com\"\n";
    lock_out << "requirement = \"^1.0.0\"\n";
    lock_out << "artifact_sha256 = \"abc123\"\n";
    lock_out << "store_path = \"" << pkg_path.generic_string() << "\"\n";
    // This is the correct hash for "📝 🌟\n" source, computed via production authority
    lock_out << "content_sha256 = \"" << correct_hash << "\"\n";
    lock_out.close();
    
    // Now MUTATE the source file after lock creation - change to different content
    // This changes the content, making the stored hash no longer valid
    std::ofstream mutated_source(pkg_path / "src" / "main.emoji", std::ios::trunc);
    mutated_source << "📝 🍎 🫴 🤲\n";
    mutated_source.close();
    
    std::ofstream manifest_file(root / "emojineer.toml");
    manifest_file << "[package]\n";
    manifest_file << "name = \"app\"\n";
    manifest_file << "version = \"0.1.0\"\n";
    manifest_file << "entry = \"src/main.emoji\"\n";
    manifest_file << "\n";
    manifest_file << "[registries]\n";
    manifest_file << "origin = \"https://registry.example.com\"\n";
    manifest_file << "\n";
    manifest_file << "[dependencies]\n";
    manifest_file << "mutatepkg = \"registry:origin:^1.0.0\"\n";
    manifest_file.close();
    
    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    refresh_lock_manifest_hash(root);
    
    // This should throw because the content hash no longer matches
    bool rejected = false;
    std::string error_msg;
    try {
        auto graph = emojineer::resolve_package_graph(root, manifest, store_root, true);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("content SHA256 mismatch") != std::string::npos;
        error_msg = e.what();
    }
    require(rejected, "mutated content should be rejected due to hash mismatch, got: " + error_msg);
    
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
        test_registry_dependency_offline_resolution();
        test_registry_dependency_offline_without_lock();
        test_registry_package_unique_names_no_duplicates();
        test_registry_package_source_kind_preserved();
        test_transitive_registry_child_preserves_source_kind();
        test_registry_package_rejects_path_dependency();
        test_registry_package_content_integrity_valid_hash_succeeds();
        test_registry_package_content_integrity_hash_mismatch_rejected();
        std::cout << "✅ package dependency tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
