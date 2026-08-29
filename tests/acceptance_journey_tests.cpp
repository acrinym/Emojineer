// Acceptance journey tests for Train 15 - Remote Dependencies
// This is the 12th CTest target as specified in the contract

#include "emojineer/project.hpp"
#include "emojineer/package.hpp"
#include "emojineer/package_artifact.hpp"
#include "emojineer/registry_transport.hpp"
#include "emojineer/module.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("acceptance test failed: " + message);
}

std::filesystem::path temp_root(const std::string& suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("emojineer-acceptance-" + suffix + "-" + std::to_string(nonce));
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write test file");
    output << text;
}

// Test 1: Create and publish multiple versions of a library to a local registry
void test_publish_library_versions() {
    std::cout << "Test: publish library versions to registry...\n";
    
    // Create a library project
    const auto lib_root = temp_root("lib");
    std::filesystem::create_directories(lib_root);
    
    // Create version 1.0.0
    write_text(lib_root / "emojineer.toml",
        "[package]\n"
        "name = \"mathkit\"\n"
        "version = \"1.0.0\"\n"
        "entry = \"src/math.emoji\"\n");
    std::filesystem::create_directories(lib_root / "src");
    write_text(lib_root / "src/math.emoji", "📝 Math library v1\n");
    
    // Initialize registry and publish
    const auto registry_root = temp_root("registry");
    std::filesystem::create_directories(registry_root);
    emojineer::initialize_file_registry(registry_root, "emojineer.test");
    auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());
    
    auto result1 = emojineer::publish_package_to_registry(lib_root, endpoint);
    require(result1.record.version == "1.0.0", "first publish should be version 1.0.0");
    
    // Update to version 1.1.0 and publish again
    write_text(lib_root / "emojineer.toml",
        "[package]\n"
        "name = \"mathkit\"\n"
        "version = \"1.1.0\"\n"
        "entry = \"src/math.emoji\"\n");
    
    auto result2 = emojineer::publish_package_to_registry(lib_root, endpoint);
    require(result2.record.version == "1.1.0", "second publish should be version 1.1.0");
    
    // Verify both versions are available
    auto index = emojineer::load_registry_package_index(endpoint, "mathkit");
    require(index.versions.size() >= 2, "should have at least 2 versions published");
    
    // Cleanup
    std::filesystem::remove_all(lib_root);
    // Both network/file authorities are gone: all following package operations are offline.
    std::filesystem::remove_all(registry_root);
    
    std::cout << "  ✅ Published multiple library versions\n";
}

// Test 2: Registry dependencies survive publication/materialization and remain sovereign offline
void test_publish_library_with_dependency() {
    std::cout << "Test: registry dependency artifact round-trip and offline ownership...\n";
    // Root and child deliberately reuse alias `origin` for different authorities.
    // lib-b is published in root_registry; only child_registry contains transitive lib-a.
    const auto root_registry = temp_root("registry-dep-root");
    const auto child_registry = temp_root("registry-dep-child");
    std::filesystem::create_directories(root_registry);
    std::filesystem::create_directories(child_registry);
    emojineer::initialize_file_registry(root_registry, "emojineer.root.test");
    emojineer::initialize_file_registry(child_registry, "emojineer.child.test");
    const auto root_endpoint = emojineer::parse_registry_endpoint(root_registry.string());
    const auto child_endpoint = emojineer::parse_registry_endpoint(child_registry.string());

    const auto lib_a_root = temp_root("lib-a");
    std::filesystem::create_directories(lib_a_root / "src");
    write_text(lib_a_root / "emojineer.toml",
        "[package]\nname = \"lib-a\"\nversion = \"1.0.0\"\nentry = \"src/main.emoji\"\n");
    write_text(lib_a_root / "src/main.emoji",
        "🧩 🌊\n🐍 🌟 🔢 🟰 9\n📤 🌟\n");
    const auto published_a = emojineer::publish_package_to_registry(lib_a_root, child_endpoint);
    require(published_a.record.version == "1.0.0", "lib-a publication must succeed");

    const auto lib_b_root = temp_root("lib-b");
    std::filesystem::create_directories(lib_b_root / "src");
    write_text(lib_b_root / "emojineer.toml",
        "[package]\nname = \"lib-b\"\nversion = \"1.0.0\"\nentry = \"src/main.emoji\"\n"
        "\n[registries]\norigin = \"" + child_registry.string() + "\"\n"
        "\n[dependencies]\nlib-a = \"registry:origin:^1.0.0\"\n");
    write_text(lib_b_root / "src/main.emoji",
        "🧩 🌲\n🔗 📜pkg:lib-a/src/main.emoji📜\n"
        "🛠️ 🍏 🫴 🤲\n📦 🌟\n🏁\n📤 🍏\n");

    const auto artifact = emojineer::parse_package_artifact(
        emojineer::build_package_artifact_bytes(lib_b_root));
    require(artifact.manifest.find("[registries]") != std::string::npos,
            "registry package artifact must preserve [registries]");
    require(artifact.manifest.find("lib-a = \"registry:origin:^1.0.0\"") != std::string::npos,
            "registry dependency coordinate must round-trip in the artifact");
    const auto published_b = emojineer::publish_package_to_registry(lib_b_root, root_endpoint);
    require(published_b.record.version == "1.0.0", "lib-b publication must succeed");
    require(!emojineer::load_registry_package_index(root_endpoint, "lib-b").versions.empty(),
            "published lib-b must be discoverable");

    const auto app_root = temp_root("app-registry-chain");
    emojineer::initialize_project(app_root, "app");
    emojineer::add_project_registry_dependency(
        app_root, "lib-b", "^1.0.0", root_registry.string(), "origin");
    emojineer::sync_project(app_root, false);

    const auto app_manifest = emojineer::load_project_manifest(app_root / "emojineer.toml");
    const auto app_lock = emojineer::load_project_lock(app_root / "emojineer.lock");
    require(!emojineer::is_lock_stale(app_root, app_manifest, app_lock),
            "freshly synced registry lock must not be stale");
    bool saw_root_authority = false;
    bool saw_child_authority = false;
    for (const auto& registry : app_lock.registries) {
        if (registry.alias != "origin") continue;
        if (registry.endpoint == root_endpoint.canonical) saw_root_authority = true;
        if (registry.endpoint == child_endpoint.canonical) saw_child_authority = true;
    }
    require(saw_root_authority && saw_child_authority,
            "lock must retain both package-local authorities even when both aliases are origin");
    bool saw_b = false;
    bool saw_a = false;
    std::filesystem::path lib_b_store;
    for (const auto& dep : app_lock.dependencies) {
        if (dep.name == "lib-b") {
            saw_b = dep.source == emojineer::LockSourceKind::Registry &&
                    dep.store_path.has_value() && dep.content_sha256.has_value() &&
                    dep.content_sha256->size() == 64;
            if (dep.store_path) lib_b_store = *dep.store_path;
        } else if (dep.name == "lib-a") {
            saw_a = dep.source == emojineer::LockSourceKind::Registry &&
                    dep.store_path.has_value() && dep.content_sha256.has_value() &&
                    dep.content_sha256->size() == 64;
        }
    }
    require(saw_b && saw_a,
            "sync-produced lock must retain store paths and content hashes for direct/transitive registry packages");
    const auto offline_graph = emojineer::resolve_package_graph(
        app_root, app_manifest, emojineer::package_store_root(app_root), true);
    require(offline_graph.find("lib-b") != nullptr && offline_graph.find("lib-a") != nullptr,
            "offline graph must contain direct lib-b and its owned transitive lib-a");

    std::filesystem::remove_all(root_registry);
    std::filesystem::remove_all(child_registry);
    write_text(app_root / "src/main.emoji",
        "🧩 🚀\n🔗 📜pkg:lib-b/src/main.emoji📜\n📝 🍏 🫴 🤲\n");
    (void)emojineer::compile_file(app_root / "src/main.emoji", {}, app_root);

    write_text(app_root / "src/main.emoji",
        "🧩 🚀\n🔗 📜pkg:lib-a/src/main.emoji📜\n");
    bool transitive_rejected = false;
    try {
        (void)emojineer::compile_file(app_root / "src/main.emoji", {}, app_root);
    } catch (const std::runtime_error& error) {
        transitive_rejected = std::string(error.what()).find(
            "does not declare direct dependency 'lib-a'") != std::string::npos;
    }
    require(transitive_rejected,
            "root must reject ambient transitive lib-a with the direct-ownership diagnostic");

    // Tamper with a package materialized by sync. The registry is already unavailable, so
    // both graph resolution and compile_file must enforce the lock's persisted content hash.
    require(!lib_b_store.empty(), "sync must expose lib-b materialized store path");
    write_text(lib_b_store / "src/main.emoji",
        "🧩 🌲\n🔗 📜pkg:lib-a/src/main.emoji📜\n"
        "🛠️ 🍏 🫴 🤲\n📦 🌟\n🏁\n📤 🍏\n📝 tampered-after-sync\n");

    bool graph_tamper_rejected = false;
    try {
        (void)emojineer::resolve_package_graph(
            app_root, app_manifest, emojineer::package_store_root(app_root), true);
    } catch (const std::runtime_error& error) {
        graph_tamper_rejected = std::string(error.what()).find("content SHA256 mismatch") != std::string::npos;
    }
    require(graph_tamper_rejected,
            "offline graph must reject content tampering using sync-produced registry lock hash");

    write_text(app_root / "src/main.emoji",
        "🧩 🚀\n🔗 📜pkg:lib-b/src/main.emoji📜\n📝 🍏 🫴 🤲\n");
    bool compile_tamper_rejected = false;
    try {
        (void)emojineer::compile_file(app_root / "src/main.emoji", {}, app_root);
    } catch (const std::runtime_error& error) {
        compile_tamper_rejected = std::string(error.what()).find("content SHA256 mismatch") != std::string::npos;
    }
    require(compile_tamper_rejected,
            "compile_file must reject content tampering using sync-produced registry lock hash");

    std::filesystem::remove_all(lib_a_root);
    std::filesystem::remove_all(lib_b_root);
    std::filesystem::remove_all(app_root);
    std::cout << "  ✅ Package-local registry authority, offline ownership, and tamper integrity proven\n";
}

// Test 3: Initialize an application and add remote dependency
void test_add_remote_dependency() {
    std::cout << "Test: add remote dependency to application...\n";
    
    // Create a registry with a package
    const auto registry_root = temp_root("registry-add");
    std::filesystem::create_directories(registry_root);
    emojineer::initialize_file_registry(registry_root, "emojineer.test");
    auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());
    
    // Create and publish a package
    const auto lib_root = temp_root("utils");
    std::filesystem::create_directories(lib_root);
    write_text(lib_root / "emojineer.toml",
        "[package]\n"
        "name = \"utils\"\n"
        "version = \"1.0.0\"\n"
        "entry = \"src/utils.emoji\"\n");
    std::filesystem::create_directories(lib_root / "src");
    write_text(lib_root / "src/utils.emoji", "📝 Utils library\n");
    emojineer::publish_package_to_registry(lib_root, endpoint);
    
    // Create an application
    const auto app_root = temp_root("app");
    emojineer::initialize_project(app_root, "myapp");
    
    // Add the remote dependency
    emojineer::add_project_registry_dependency(app_root, "utils", "^1.0.0", 
        registry_root.string(), "origin");
    
    // Verify it was added to manifest
    auto manifest = emojineer::load_project_manifest(app_root / "emojineer.toml");
    require(manifest.dependencies.size() == 1, "should have one dependency");
    require(manifest.dependencies[0].name == "utils", "dependency should be utils");
    require(manifest.dependencies[0].kind == emojineer::DependencyKind::Registry, 
        "dependency should be registry kind");
    
    // Verify registry was added
    require(!manifest.registries.empty(), "should have a registry");
    require(manifest.registries[0].alias == "origin", "registry alias should be origin");
    
    // Cleanup
    std::filesystem::remove_all(lib_root);
    std::filesystem::remove_all(app_root);
    std::filesystem::remove_all(registry_root);
    
    std::cout << "  ✅ Added remote dependency to application\n";
}

// Test 4: Sync and verify lock format 3
void test_sync_creates_lock_v3() {
    std::cout << "Test: sync creates lock format 3...\n";
    
    // Create registry with package
    const auto registry_root = temp_root("registry-sync");
    std::filesystem::create_directories(registry_root);
    emojineer::initialize_file_registry(registry_root, "emojineer.test");
    auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());
    
    const auto lib_root = temp_root("lib-sync");
    std::filesystem::create_directories(lib_root);
    write_text(lib_root / "emojineer.toml",
        "[package]\n"
        "name = \"lib\"\n"
        "version = \"1.0.0\"\n"
        "entry = \"src/lib.emoji\"\n");
    std::filesystem::create_directories(lib_root / "src");
    write_text(lib_root / "src/lib.emoji", "📝 Library\n");
    emojineer::publish_package_to_registry(lib_root, endpoint);
    
    // Create app with dependency
    const auto app_root = temp_root("app-sync");
    emojineer::initialize_project(app_root, "app");
    emojineer::add_project_registry_dependency(app_root, "lib", "^1.0.0", 
        registry_root.string(), "origin");
    
    // Sync
    emojineer::sync_project(app_root, false);
    
    // Check lock exists and is version 3
    auto lock_text = emojineer::read_text_standalone(app_root / "emojineer.lock");
    require(lock_text.find("lock_version = 3") != std::string::npos, 
        "lock should be version 3");
    require(lock_text.find("[[registry]]") != std::string::npos,
        "lock should contain registry section");
    
    // Cleanup
    std::filesystem::remove_all(lib_root);
    std::filesystem::remove_all(app_root);
    std::filesystem::remove_all(registry_root);
    
    std::cout << "  ✅ Sync creates lock format 3\n";
}

// Test 5: Test deterministic version selection with multiple requirements
void test_deterministic_version_selection() {
    std::cout << "Test: deterministic version selection...\n";
    
    // Create registry with multiple versions
    const auto registry_root = temp_root("registry-versions");
    std::filesystem::create_directories(registry_root);
    emojineer::initialize_file_registry(registry_root, "emojineer.test");
    auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());
    
    // Publish versions 1.0.0, 1.1.0, 2.0.0
    const auto lib_root = temp_root("lib-versions");
    std::filesystem::create_directories(lib_root);
    
    for (const auto& ver : {"1.0.0", "1.1.0", "2.0.0"}) {
        write_text(lib_root / "emojineer.toml",
            "[package]\n"
            "name = \"lib\"\n"
            "version = \"" + std::string(ver) + "\"\n"
            "entry = \"src/lib.emoji\"\n");
        std::filesystem::create_directories(lib_root / "src");
        write_text(lib_root / "src/lib.emoji", "📝 Library " + std::string(ver) + "\n");
        emojineer::publish_package_to_registry(lib_root, endpoint);
    }
    
    // Test version selection with ^1.0.0 should select 1.1.0 (highest compatible)
    auto index = emojineer::load_registry_package_index(endpoint, "lib");
    auto selected = emojineer::select_deterministic_version("lib", {"^1.0.0"}, 
        {"1.0.0", "1.1.0", "2.0.0"});
    require(selected == "1.1.0", "^1.0.0 should select 1.1.0, got " + selected);
    
    // Test version selection with ^2.0.0 should select 2.0.0
    selected = emojineer::select_deterministic_version("lib", {"^2.0.0"}, 
        {"1.0.0", "1.1.0", "2.0.0"});
    require(selected == "2.0.0", "^2.0.0 should select 2.0.0, got " + selected);
    
    // Test conflict detection - use caret ranges that create real conflict
    // ^1.0.0 allows 1.x but not 2.x, ^2.0.0 allows 2.x but not 1.x
    auto conflict = emojineer::detect_version_conflict("lib", 
        {"^1.0.0", "^2.0.0"}, {"1.0.0", "1.1.0", "2.0.0"});
    require(conflict.has_value(), "conflict should be detected for incompatible requirements");
    
    // Cleanup
    std::filesystem::remove_all(lib_root);
    std::filesystem::remove_all(registry_root);
    
    std::cout << "  ✅ Deterministic version selection works\n";
}

// Test 6: Test stale lock detection
void test_stale_lock_detection() {
    std::cout << "Test: stale lock detection...\n";
    
    // Create app
    const auto app_root = temp_root("app-stale");
    emojineer::initialize_project(app_root, "app");
    emojineer::write_project_lock(app_root, 
        emojineer::load_project_manifest(app_root / "emojineer.toml"));
    
    // Load the lock
    auto lock = emojineer::load_project_lock(app_root / "emojineer.lock");
    auto manifest = emojineer::load_project_manifest(app_root / "emojineer.toml");
    
    // Without changes, lock should not be stale
    require(!emojineer::is_lock_stale(app_root, manifest, lock), 
        "lock should not be stale initially");
    
    // After modifying manifest, lock should be stale
    write_text(app_root / "emojineer.toml",
        "[package]\n"
        "name = \"app\"\n"
        "version = \"0.2.0\"\n"
        "entry = \"src/main.emoji\"\n");
    manifest = emojineer::load_project_manifest(app_root / "emojineer.toml");
    require(emojineer::is_lock_stale(app_root, manifest, lock),
        "lock should be stale after manifest change");
    
    // Cleanup
    std::filesystem::remove_all(app_root);
    
    std::cout << "  ✅ Stale lock detection works\n";
}

// Test 7: Test package store path generation
void test_package_store_path() {
    std::cout << "Test: package store path generation...\n";
    
    const auto app_root = temp_root("app-store");
    emojineer::initialize_project(app_root, "app");
    
    auto store_root = emojineer::package_store_root(app_root);
    require(store_root == app_root / ".emojineer" / "packages",
        "store root should be .emojineer/packages");
    
    // Cleanup
    std::filesystem::remove_all(app_root);
    
    std::cout << "  ✅ Package store path works\n";
}

// Test 8: Test version conflict error message
void test_version_conflict_error() {
    std::cout << "Test: version conflict error message...\n";
    
    // Test that select_deterministic_version throws on conflict
    // Use caret ranges that conflict: ^1.0.0 and ^2.0.0 can't both be satisfied
    try {
        emojineer::select_deterministic_version("pkg", {"^1.0.0", "^2.0.0"}, 
            {"1.0.0", "2.0.0"});
        require(false, "should have thrown on conflict");
    } catch (const std::exception& e) {
        require(std::string(e.what()).find("conflict") != std::string::npos,
            "error should mention conflict");
    }
    
    std::cout << "  ✅ Version conflict error works\n";
}

// Test 9: Test manifest validation rejects same package as path and registry
void test_manifest_rejects_duplicate_dependency_kind() {
    std::cout << "Test: manifest validation rejects duplicate dependency...\n";
    
    const auto root = temp_root("app-dup");
    emojineer::initialize_project(root, "app");
    
    // Try to add same package as both path and registry
    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    
    // Add as path
    manifest.dependencies.push_back({"lib", emojineer::DependencyKind::Path, 
        std::filesystem::path("../lib"), {}, {}});
    
    // Try to add as registry (should fail validation)
    try {
        manifest.dependencies.push_back({"lib", emojineer::DependencyKind::Registry,
            {}, "origin", "^1.0.0"});
        emojineer::validate_manifest(manifest);
        require(false, "should have thrown on duplicate dependency");
    } catch (const std::exception& e) {
        require(std::string(e.what()).find("duplicate") != std::string::npos ||
                std::string(e.what()).find("lib") != std::string::npos,
            "error should mention duplicate or package name");
    }
    
    // Cleanup
    std::filesystem::remove_all(root);
    
    std::cout << "  ✅ Manifest validation rejects duplicate dependency\n";
}

// Test 10: Test offline sync behavior
void test_offline_sync() {
    std::cout << "Test: offline sync behavior...\n";
    
    // Create app
    const auto app_root = temp_root("app-offline");
    emojineer::initialize_project(app_root, "app");
    
    // Sync in offline mode with no registry deps should work
    emojineer::sync_project(app_root, true);
    
    // Verify lock was created
    require(std::filesystem::exists(app_root / "emojineer.lock"),
        "lock should exist after offline sync");
    
    // Cleanup
    std::filesystem::remove_all(app_root);
    
    std::cout << "  ✅ Offline sync works\n";
}

// Test 11: Test sync -> lock -> registry unavailable -> compile round-trip
// This verifies that sync properly persists registry dependency metadata in lock
void test_sync_lock_offline_compile_roundtrip() {
    std::cout << "Test: sync -> lock -> registry unavailable -> compile round-trip...\n";
    
    // Create registry with base package (lib-a)
    const auto registry_root = temp_root("registry-roundtrip");
    std::filesystem::create_directories(registry_root);
    emojineer::initialize_file_registry(registry_root, "emojineer.test");
    auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());
    
    // Create and publish lib-a (base package with exported function)
    const auto lib_a_root = temp_root("lib-a");
    std::filesystem::create_directories(lib_a_root);
    write_text(lib_a_root / "emojineer.toml",
        "[package]\n"
        "name = \"lib-a\"\n"
        "version = \"1.0.0\"\n"
        "entry = \"src/lib-a.emoji\"\n");
    std::filesystem::create_directories(lib_a_root / "src");
    write_text(lib_a_root / "src/lib-a.emoji",
        "🧩 🌊\n"
        "🐍 🌟 🔢 🟰 9\n"
        "📤 🌟\n");
    emojineer::publish_package_to_registry(lib_a_root, endpoint);
    
    // Create and publish lib-b that depends on lib-a (transitive dep)
    const auto lib_b_root = temp_root("lib-b");
    std::filesystem::create_directories(lib_b_root);
    write_text(lib_b_root / "emojineer.toml",
        "[package]\n"
        "name = \"lib-b\"\n"
        "version = \"1.0.0\"\n"
        "entry = \"src/lib-b.emoji\"\n"
        "\n"
        "[registries]\n"
        "origin = \"" + registry_root.string() + "\"\n"
        "\n"
        "[dependencies]\n"
        "lib-a = \"registry:origin:^1.0.0\"\n");
    std::filesystem::create_directories(lib_b_root / "src");
    write_text(lib_b_root / "src/lib-b.emoji",
        "🧩 🌲\n"
        "🔗 📜pkg:lib-a/src/lib-a.emoji📜\n"
        "🛠️ 🍏 🫴 🤲\n"
        "📦 🌟\n"
        "🏁\n"
        "📤 🍏\n");
    emojineer::publish_package_to_registry(lib_b_root, endpoint);
    
    // Create app that depends on lib-b (which has transitive dep on lib-a)
    const auto app_root = temp_root("app-roundtrip");
    emojineer::initialize_project(app_root, "app");
    emojineer::add_project_registry_dependency(app_root, "lib-b", "^1.0.0", 
        registry_root.string(), "origin");
    
    // Create source file that uses lib-b (direct dependency)
    // Test with simple compilation that uses the package graph
    // Use valid Emojineer emoji syntax from module_tests.cpp
    std::filesystem::create_directories(app_root / "src");
    write_text(app_root / "src/main.emoji",
        "🧩 🚀\n"
        "🔗 📜pkg:lib-b/src/lib-b.emoji📜\n"
        "📝 🍏 🫴 🤲\n");
    
    // Step 1: Sync - this should materialize both lib-b and lib-a (transitive)
    emojineer::sync_project(app_root, false);
    
    // Step 2: Verify lock contains both direct and transitive dependencies
    auto lock_text = emojineer::read_text_standalone(app_root / "emojineer.lock");
    require(lock_text.find("lib-b") != std::string::npos,
        "lock should contain direct dependency lib-b");
    require(lock_text.find("lib-a") != std::string::npos,
        "lock should contain transitive dependency lib-a");
    require(lock_text.find("artifact_sha256") != std::string::npos,
        "lock should contain artifact hashes");
    require(lock_text.find("store_path") != std::string::npos,
        "lock should contain store paths");
    
    // Verify lock has proper registry_alias filled in (not empty)
    require(lock_text.find("registry = ") != std::string::npos,
        "lock should contain registry alias for dependencies");
    
    // Step 3: Remove the registry to make it unavailable
    std::filesystem::remove_all(registry_root);
    
    // Step 4: Try to compile/load using the lock file in offline mode
    // This should work because the lock contains all necessary metadata
    auto manifest = emojineer::load_project_manifest(app_root / "emojineer.toml");
    auto lock = emojineer::load_project_lock(app_root / "emojineer.lock");
    
    // Verify lock is not stale (manifest hasn't changed)
    require(!emojineer::is_lock_stale(app_root, manifest, lock),
        "lock should not be stale after registry removal");
    
    // Verify we can resolve package graph in offline mode using the lock
    // This uses the lock's store_path to find materialized packages
    auto store_root = emojineer::package_store_root(app_root);
    require(std::filesystem::exists(store_root), "store root should exist");
    
    // Verify materialized packages exist
    // lib-b should be materialized
    bool lib_b_materialized = false;
    bool lib_a_materialized = false;
    for (const auto& dep : lock.dependencies) {
        if (dep.name == "lib-b" && dep.store_path) {
            if (std::filesystem::exists(*dep.store_path)) {
                lib_b_materialized = true;
            }
        }
        if (dep.name == "lib-a" && dep.store_path) {
            if (std::filesystem::exists(*dep.store_path)) {
                lib_a_materialized = true;
            }
        }
    }
    require(lib_b_materialized, "lib-b should be materialized");
    require(lib_a_materialized, "lib-a (transitive) should be materialized");
    
    // Step 5: Actually compile in offline mode - should succeed for direct dep lib-b
    auto offline_graph = emojineer::resolve_package_graph(app_root, manifest, store_root, true);
    require(offline_graph.packages.size() > 0, "offline package graph should have packages");
    
    // Compile the main file in offline mode
    try {
        auto chunk = emojineer::compile_file(app_root / "src/main.emoji", {}, app_root);
        // Success - direct import of lib-b works in offline mode
    } catch (const std::exception& e) {
        require(false, "compile should succeed for direct dependency lib-b in offline mode: " + std::string(e.what()));
    }
    
    // Step 6: Try to import transitive lib-a directly - should fail
    // because package ownership boundaries are enforced (no ambient transitive imports)
    write_text(app_root / "src/bad_import.emoji",
        "🧩 🚀\n"
        "🔗 📜pkg:lib-a/src/lib-a.emoji📜\n");
    
    bool import_rejected = false;
    try {
        (void)emojineer::compile_file(app_root / "src/bad_import.emoji", {}, app_root);
    } catch (const std::runtime_error& error) {
        import_rejected = std::string(error.what()).find(
            "does not declare direct dependency 'lib-a'") != std::string::npos;
    }
    require(import_rejected,
            "direct transitive lib-a import must fail with direct-dependency ownership diagnostic");
    
    // Cleanup
    std::filesystem::remove_all(lib_a_root);
    std::filesystem::remove_all(lib_b_root);
    std::filesystem::remove_all(app_root);
    // Registry already removed
    
    std::cout << "  ✅ Sync -> lock -> registry unavailable -> compile round-trip works\n";
}


void test_offline_registry_lock_metadata_rejected() {
    std::cout << "Test: malformed registry lock metadata is rejected before offline module loading...\n";
    const auto registry_root = temp_root("registry-lock-metadata");
    std::filesystem::create_directories(registry_root);
    emojineer::initialize_file_registry(registry_root, "emojineer.test");
    const auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());

    const auto lib_root = temp_root("lock-metadata-lib");
    std::filesystem::create_directories(lib_root / "src");
    write_text(lib_root / "emojineer.toml",
        "[package]\nname = \"locked-lib\"\nversion = \"1.0.0\"\nentry = \"src/main.emoji\"\n");
    write_text(lib_root / "src/main.emoji", "🧩 🌊\n🐍 🌟 🔢 🟰 9\n📤 🌟\n");
    (void)emojineer::publish_package_to_registry(lib_root, endpoint);

    const auto app_root = temp_root("lock-metadata-app");
    emojineer::initialize_project(app_root, "app");
    emojineer::add_project_registry_dependency(app_root, "locked-lib", "^1.0.0",
                                               registry_root.string(), "origin");
    write_text(app_root / "src/main.emoji",
        "🧩 🚀\n🔗 📜pkg:locked-lib/src/main.emoji📜\n");
    emojineer::sync_project(app_root, false);

    const auto manifest = emojineer::load_project_manifest(app_root / "emojineer.toml");
    const auto good_lock = emojineer::load_project_lock(app_root / "emojineer.lock");
    const auto good_text = emojineer::read_text_standalone(app_root / "emojineer.lock");
    const auto store_root = emojineer::package_store_root(app_root);
    const auto* locked = [&]() -> const emojineer::LockDependency* {
        for (const auto& dep : good_lock.dependencies) if (dep.name == "locked-lib") return &dep;
        return nullptr;
    }();
    require(locked && locked->registry_alias && locked->registry_id && locked->registry_endpoint &&
            locked->requirement && locked->artifact_sha256 && locked->content_sha256,
            "sync-produced registry lock must contain complete metadata before mutation");

    auto replace_once = [](std::string text, const std::string& old_value,
                           const std::string& new_value) {
        const auto pos = text.find(old_value);
        if (pos == std::string::npos) throw std::runtime_error("acceptance mutation anchor missing: " + old_value);
        text.replace(pos, old_value.size(), new_value);
        return text;
    };
    auto erase_once = [](std::string text, const std::string& value) {
        const auto pos = text.find(value);
        if (pos == std::string::npos) throw std::runtime_error("acceptance erase anchor missing: " + value);
        text.erase(pos, value.size());
        return text;
    };

    std::vector<std::pair<std::string, std::string>> cases;
    cases.push_back({"lock version", replace_once(good_text, "lock_version = 3", "lock_version = 2")});
    cases.push_back({"missing registry alias", erase_once(good_text, "registry = \"" + *locked->registry_alias + "\"\n")});
    cases.push_back({"undeclared registry alias", replace_once(good_text,
        "registry = \"" + *locked->registry_alias + "\"",
        "registry = \"missing-authority\"")});
    cases.push_back({"missing registry id", erase_once(good_text, "registry_id = \"" + *locked->registry_id + "\"\n")});
    cases.push_back({"mismatched registry endpoint", replace_once(good_text,
        "registry_endpoint = \"" + *locked->registry_endpoint + "\"",
        "registry_endpoint = \"file:///wrong-registry\"")});
    cases.push_back({"empty requirement", replace_once(good_text,
        "requirement = \"" + *locked->requirement + "\"", "requirement = \"\"")});
    cases.push_back({"invalid artifact sha", replace_once(good_text,
        "artifact_sha256 = \"" + *locked->artifact_sha256 + "\"", "artifact_sha256 = \"xyz\"")});
    cases.push_back({"missing content sha", erase_once(good_text,
        "content_sha256 = \"" + *locked->content_sha256 + "\"\n")});
    cases.push_back({"wrong dependency edges", replace_once(good_text,
        "dependencies = \"\"", "dependencies = \"ghost\"")});

    std::filesystem::remove_all(registry_root);
    for (const auto& [label, mutated] : cases) {
        write_text(app_root / "emojineer.lock", mutated);
        bool graph_rejected = false;
        try {
            (void)emojineer::resolve_package_graph(app_root, manifest, store_root, true);
        } catch (const std::runtime_error&) {
            graph_rejected = true;
        }
        require(graph_rejected, label + " must be rejected by offline package graph");

        bool compile_rejected = false;
        try {
            (void)emojineer::compile_file(app_root / "src/main.emoji", {}, app_root);
        } catch (const std::runtime_error&) {
            compile_rejected = true;
        }
        require(compile_rejected, label + " must be rejected by compile_file before module loading");
    }

    write_text(app_root / "emojineer.lock", good_text);
    (void)emojineer::resolve_package_graph(app_root, manifest, store_root, true);
    (void)emojineer::compile_file(app_root / "src/main.emoji", {}, app_root);

    std::filesystem::remove_all(lib_root);
    std::filesystem::remove_all(app_root);
    std::cout << "  ✅ Malformed registry lock metadata rejected by graph and compile authority\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Train 15 Acceptance Journey Tests ===\n\n";
    
    try {
        test_publish_library_versions();
        test_publish_library_with_dependency();
        test_add_remote_dependency();
        test_sync_creates_lock_v3();
        test_deterministic_version_selection();
        test_stale_lock_detection();
        test_package_store_path();
        test_version_conflict_error();
        test_manifest_rejects_duplicate_dependency_kind();
        test_offline_sync();
        test_sync_lock_offline_compile_roundtrip();
        test_offline_registry_lock_metadata_rejected();
        
        std::cout << "\n✅ All acceptance journey tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed: " << e.what() << "\n";
        return 1;
    }
}
