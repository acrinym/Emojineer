#include "emojineer/project.hpp"
#include "emojineer/package_artifact.hpp"
#include "emojineer/hash.hpp"
#include "emojineer/registry_transport.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("project test failed: " + message);
}

std::filesystem::path temp_root(const std::string& suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("emojineer-" + suffix + "-" + std::to_string(nonce));
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write project test file");
    output << text;
}

void refresh_project_test_lock_manifest_hash(const std::filesystem::path& root) {
    const auto lock_path = root / "emojineer.lock";
    if (!std::filesystem::exists(lock_path)) return;
    const auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    const auto manifest_hash = emojineer::project_manifest_hash(manifest);
    std::ifstream input(lock_path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read project test lock");
    std::string lock_text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::string prefix = "manifest_hash = \"";
    const auto begin = lock_text.find(prefix);
    if (begin == std::string::npos) throw std::runtime_error("project test lock is missing manifest_hash");
    const auto value_begin = begin + prefix.size();
    const auto value_end = lock_text.find('\"', value_begin);
    if (value_end == std::string::npos) throw std::runtime_error("project test lock has malformed manifest_hash");
    lock_text.replace(value_begin, value_end - value_begin, manifest_hash);
    std::ofstream output(lock_path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot rewrite project test lock");
    output << lock_text;
}

bool has_diagnostic(const std::vector<emojineer::ProjectDiagnostic>& diagnostics,
                    const std::string& needle) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

void test_initialize_load_lock_and_check() {
    const auto root = temp_root("project");
    std::filesystem::remove_all(root);
    emojineer::initialize_project(root, "demo_project");

    const auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    require(manifest.name == "demo_project", "initialized project name should round-trip");
    require(manifest.version == "0.1.0", "initialized version should be 0.1.0");
    require(manifest.entry.generic_string() == "src/main.emoji", "default entry should be src/main.emoji");
    require(manifest.dependencies.empty(), "new projects should start without dependencies");
    require(std::filesystem::exists(root / manifest.entry), "entry source should be created");

    emojineer::write_project_lock(root, manifest);
    require(std::filesystem::exists(root / "emojineer.lock"), "lockfile should be created");
    require(emojineer::check_project(root).empty(), "fresh initialized project should validate cleanly");

    std::filesystem::remove_all(root);
}

void test_lock_detects_manifest_drift() {
    const auto root = temp_root("stale-lock");
    std::filesystem::remove_all(root);
    emojineer::initialize_project(root, "drift_demo");
    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    emojineer::write_project_lock(root, manifest);

    manifest.version = "0.2.0";
    write_text(root / "emojineer.toml", emojineer::canonical_manifest_text(manifest));

    const auto diagnostics = emojineer::check_project(root);
    require(has_diagnostic(diagnostics, "stale"), "manifest changes should make the lockfile stale");
    std::filesystem::remove_all(root);
}

void test_strict_manifest_keys() {
    const auto root = temp_root("strict-manifest");
    std::filesystem::create_directories(root);
    write_text(root / "emojineer.toml",
               "[package]\n"
               "name = \"demo\"\n"
               "version = \"0.1.0\"\n"
               "entry = \"src/main.emoji\"\n"
               "mystery = \"nope\"\n");

    bool rejected = false;
    try {
        (void)emojineer::load_project_manifest(root / "emojineer.toml");
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("unknown") != std::string::npos;
    }
    require(rejected, "unknown manifest keys should be rejected instead of silently ignored");
    std::filesystem::remove_all(root);
}

void test_dependency_manifest_is_canonical_and_deterministic() {
    emojineer::ProjectManifest manifest{
        "hash_demo",
        "1.2.3",
        std::filesystem::path("src/main.emoji"),
        {},  // registries
        {   // dependencies
            {"zeta", emojineer::DependencyKind::Path, std::filesystem::path("../zeta"), {}, {}},
            {"alpha", emojineer::DependencyKind::Path, std::filesystem::path("../alpha"), {}, {}}
        }
    };
    const auto text = emojineer::canonical_manifest_text(manifest);
    const auto alpha = text.find("alpha = \"../alpha\"");
    const auto zeta = text.find("zeta = \"../zeta\"");
    require(text.find("[dependencies]") != std::string::npos,
            "canonical manifest should emit a dependencies section");
    require(alpha != std::string::npos && zeta != std::string::npos && alpha < zeta,
            "canonical dependency entries should be sorted by package name");

    const auto first = emojineer::project_manifest_hash(manifest);
    std::swap(manifest.dependencies[0], manifest.dependencies[1]);
    const auto second = emojineer::project_manifest_hash(manifest);
    require(first == second && first.size() == 16,
            "manifest identity should be independent of dependency insertion order");
}

void test_add_remove_and_dependency_lock_drift() {
    const auto root = temp_root("dependency-lock");
    const auto dep = root / "deps/mathkit";
    std::filesystem::remove_all(root);
    emojineer::initialize_project(root, "app");
    emojineer::initialize_project(dep, "mathkit");

    emojineer::add_project_dependency(root, "mathkit", "deps/mathkit");
    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    require(manifest.dependencies.size() == 1,
            "emji add foundation should persist the dependency declaration");
    require(manifest.dependencies.front().name == "mathkit" &&
                manifest.dependencies.front().path.generic_string() == "deps/mathkit",
            "dependency name and relative path should round-trip");

    const auto lock = emojineer::canonical_project_lock(root, manifest);
    require(lock.find("lock_version = 3") != std::string::npos,
            "dependency-aware lockfile should use lock format v3");
    require(lock.find("name = \"mathkit\"") != std::string::npos &&
                lock.find("content_sha256 = \"") != std::string::npos,
            "lockfile should pin dependency package identity and SHA-256 content");
    require(emojineer::check_project(root).empty(),
            "project with a fresh local dependency lock should validate cleanly");

    write_text(dep / "src/main.emoji", "📝 📜dependency changed📜\n");
    auto diagnostics = emojineer::check_project(root);
    require(has_diagnostic(diagnostics, "stale"),
            "dependency source changes should make the root lock stale");

    manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    emojineer::write_project_lock(root, manifest);
    require(emojineer::check_project(root).empty(),
            "refreshing lock should accept the new dependency content hash");

    emojineer::remove_project_dependency(root, "mathkit");
    manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    require(manifest.dependencies.empty(), "remove should delete the dependency declaration");
    require(emojineer::check_project(root).empty(),
            "remove should also refresh the deterministic lockfile");

    std::filesystem::remove_all(root);
}

void test_absolute_dependency_path_rejected() {
    emojineer::ProjectManifest manifest{
        "demo",
        "0.1.0",
        std::filesystem::path("src/main.emoji"),
        {},  // registries
        {{"bad", emojineer::DependencyKind::Path, std::filesystem::absolute("bad"), {}, {}}}
    };
    bool rejected = false;
    try {
        (void)emojineer::canonical_manifest_text(manifest);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("relative") != std::string::npos;
    }
    require(rejected, "dependency paths must stay relative and checkout-portable");
}

// Regression test: path dependency in registry package must be rejected in online mode
void test_registry_path_dependency_rejected_online() {
    const auto root = temp_root("reg-path-dep-online");
    const auto registry_root = root / "registry";
    const auto package_root = root / "mylib";
    const auto local_dep = package_root / "local-dep";
    const auto app_root = root / "app";
    const auto store_root = app_root / ".emojineer" / "packages";
    std::filesystem::remove_all(root);

    emojineer::initialize_file_registry(registry_root, "project-tests.local");
    const auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());

    emojineer::initialize_project(package_root, "mylib");
    emojineer::initialize_project(local_dep, "local-dep");
    auto package_manifest = emojineer::load_project_manifest(package_root / "emojineer.toml");
    package_manifest.version = "1.0.0";
    write_text(package_root / "emojineer.toml", emojineer::canonical_manifest_text(package_manifest));
    emojineer::add_project_dependency(package_root, "local-dep", "local-dep");
    (void)emojineer::publish_package_to_registry(package_root, endpoint);

    emojineer::initialize_project(app_root, "app");
    write_text(app_root / "emojineer.toml",
               "[package]\n"
               "name = \"app\"\n"
               "version = \"0.1.0\"\n"
               "entry = \"src/main.emoji\"\n"
               "\n[registries]\n"
               "origin = \"" + registry_root.string() + "\"\n"
               "\n[dependencies]\n"
               "mylib = \"registry:origin:^1.0.0\"\n");
    const auto manifest = emojineer::load_project_manifest(app_root / "emojineer.toml");

    bool rejected = false;
    std::string error_msg;
    try {
        (void)emojineer::resolve_registry_dependencies(manifest, store_root, app_root, false);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("path dependency") != std::string::npos;
        error_msg = e.what();
    }
    require(rejected,
            "registry package with path dependency should be rejected in ONLINE mode, got: " + error_msg);

    std::filesystem::remove_all(root);
}

// Regression test: path dependency in registry package must be rejected in offline mode
void test_registry_path_dependency_rejected_offline() {
    const auto root = temp_root("reg-path-dep-offline");
    const auto store_root = root / ".emojineer" / "packages";
    const auto endpoint = emojineer::parse_registry_endpoint("https://registry.example.com");
    std::filesystem::remove_all(root);

    emojineer::initialize_project(root, "app");
    
    // Create a local path dependency
    const auto local_dep = root / "local-dep";
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
    source_out << "📝 greeting = \"Hello\" 📤\n";
    source_out.close();
    
    // Create lock file with the registry dependency
    std::ofstream lock_out(root / "emojineer.lock");
    lock_out << "lock_version = 3\n";
    lock_out << "manifest_hash = \"abc123def456\"\n";
    lock_out << "\n";
    lock_out << "[[registry]]\n";
    lock_out << "alias = \"origin\"\n";
    lock_out << "id = \"origin-id\"\n";
    lock_out << "endpoint = \"" << endpoint.canonical << "\"\n";
    lock_out << "\n";
    lock_out << "[[dependency]]\n";
    lock_out << "source = \"registry\"\n";
    lock_out << "name = \"mylib\"\n";
    lock_out << "version = \"1.0.0\"\n";
    lock_out << "registry = \"origin\"\n";
    lock_out << "registry_id = \"origin-id\"\n";
    lock_out << "registry_endpoint = \"" << endpoint.canonical << "\"\n";
    lock_out << "requirement = \"^1.0.0\"\n";
    lock_out << "artifact_sha256 = \"abc123\"\n";
    lock_out << "store_path = \"" << pkg_path.generic_string() << "\"\n";
    lock_out << "content_sha256 = \"def456\"\n";
    lock_out.close();
    
    // Update manifest to include registry dependency
    std::ofstream manifest_file(root / "emojineer.toml", std::ios::app);
    manifest_file << "\n[registries]\n";
    manifest_file << "origin = \"" << endpoint.canonical << "\"\n";
    manifest_file << "\n[dependencies]\n";
    manifest_file << "mylib = \"registry:origin:^1.0.0\"\n";
    manifest_file.close();
    
    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    refresh_project_test_lock_manifest_hash(root);
    
    // This should throw because registry package has path dependency - OFFLINE mode (offline=true)
    bool rejected = false;
    std::string error_msg;
    try {
        auto resolved = emojineer::resolve_registry_dependencies(manifest, store_root, root, true);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("path dependency") != std::string::npos;
        error_msg = e.what();
    }
    require(rejected, "registry package with path dependency should be rejected in OFFLINE mode, got: " + error_msg);
    
    std::filesystem::remove_all(root);
}

// Regression test: corrupted offline materialization must fail, not disappear from graph
void test_corrupted_offline_materialization_fails() {
    const auto root = temp_root("corrupt-offline");
    const auto store_root = root / ".emojineer" / "packages";
    std::filesystem::remove_all(root);

    emojineer::initialize_project(root, "app");
    
    // Create a registry package with corrupted manifest
    std::filesystem::create_directories(store_root / "origin" / "mylib" / "1.0.0" / "abc123");
    auto pkg_path = store_root / "origin" / "mylib" / "1.0.0" / "abc123";
    
    // Write corrupted manifest - missing required fields
    std::ofstream manifest_out(pkg_path / "emojineer.toml");
    manifest_out << "[package]\n";
    // Missing name and version - this is corrupted
    manifest_out << "entry = \"src/main.emoji\"\n";
    manifest_out.close();
    
    std::filesystem::create_directories(pkg_path / "src");
    std::ofstream source_out(pkg_path / "src" / "main.emoji");
    source_out << "📝 greeting = \"Hello\" 📤\n";
    source_out.close();
    
    // Create lock file with the registry dependency pointing to corrupted package
    std::ofstream lock_out(root / "emojineer.lock");
    lock_out << "lock_version = 3\n";
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
    lock_out << "content_sha256 = \"def456\"\n";
    lock_out.close();
    
    // Update manifest to include registry dependency
    std::ofstream manifest_file(root / "emojineer.toml", std::ios::app);
    manifest_file << "\n[registries]\n";
    manifest_file << "origin = \"https://registry.example.com\"\n";
    manifest_file << "\n[dependencies]\n";
    manifest_file << "mylib = \"registry:origin:^1.0.0\"\n";
    manifest_file.close();
    
    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    refresh_project_test_lock_manifest_hash(root);
    
    // This should throw because the materialized manifest is corrupted - OFFLINE mode
    bool failed = false;
    std::string error_msg;
    try {
        auto resolved = emojineer::resolve_registry_dependencies(manifest, store_root, root, true);
    } catch (const std::runtime_error& e) {
        failed = true;
        error_msg = e.what();
    }
    require(failed, "corrupted offline materialization should fail, not disappear from graph, got: " + error_msg);
    require(error_msg == "manifest [package] requires name, version, and entry",
            "corrupted materialization must fail for the intended manifest corruption, got: " + error_msg);
    
    std::filesystem::remove_all(root);
}

// Regression test: after sync_project, check_project should not report stale lock
// This verifies that sync_project correctly updates the lock file to match the manifest
void test_sync_project_check_project_no_stale_lock() {
    const auto root = temp_root("sync-check-no-stale");
    std::filesystem::remove_all(root);

    emojineer::initialize_project(root, "app");
    
    // Create an initial lock file
    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    emojineer::write_project_lock(root, manifest);
    
    // Verify initial state - no stale lock
    auto initial_diagnostics = emojineer::check_project(root);
    require(!has_diagnostic(initial_diagnostics, "stale"), 
            "initial project should not have stale lock");
    
    // Sync the project (this writes a fresh lock file)
    // Note: This test verifies the lock is not stale after sync
    // In a real scenario with registry deps, sync would fetch and materialize
    emojineer::sync_project(root, true);  // offline mode
    
    // Immediately check project - should not have stale lock
    auto diagnostics = emojineer::check_project(root);
    require(!has_diagnostic(diagnostics, "stale"), 
            "after sync_project, check_project should not report stale lock");
    
    std::filesystem::remove_all(root);
}


void test_verify_or_repair_materialization_restores_sources() {
    const auto root = temp_root("repair-materialization");
    const auto source_root = root / "artifact-source";
    const auto cache_root = root / "cache";
    std::filesystem::remove_all(root);
    emojineer::initialize_project(source_root, "mylib");

    const auto bytes = emojineer::build_package_artifact_bytes(source_root);
    const auto artifact = emojineer::parse_package_artifact(bytes);
    const std::string endpoint = "https://registry.example.com";
    const std::string registry_id = "origin-id";
    const std::string registry_cache_key =
        emojineer::sha256_hex(endpoint + "\n" + registry_id).substr(0, 32);
    const auto cache_path = emojineer::package_cache_path(
        cache_root / "registries" / registry_cache_key, artifact);
    std::filesystem::create_directories(cache_path.parent_path());
    write_text(cache_path, bytes);

    const auto store_root = emojineer::package_store_root(root);
    const auto package_path = store_root / "origin" / artifact.name / artifact.version /
                              artifact.artifact_sha256;
    std::filesystem::create_directories(package_path / "src");
    write_text(package_path / "emojineer.toml", artifact.manifest);
    write_text(package_path / artifact.entry, "📝 📜corrupt📜\n");
    require(!emojineer::is_materialized_package_valid(package_path, artifact.content_sha256),
            "fixture must begin corrupt");

    emojineer::ProjectLock lock;
    lock.version = "3";
    emojineer::LockDependency dependency;
    dependency.source = emojineer::LockSourceKind::Registry;
    dependency.name = artifact.name;
    dependency.version = artifact.version;
    dependency.registry_alias = "origin";
    dependency.registry_id = registry_id;
    dependency.registry_endpoint = endpoint;
    dependency.requirement = "^0.1.0";
    dependency.artifact_sha256 = artifact.artifact_sha256;
    dependency.content_sha256 = artifact.content_sha256;
    dependency.store_path = package_path.generic_string();
    lock.dependencies.push_back(dependency);

    emojineer::verify_or_repair_materialization(root, lock, cache_root);
    require(emojineer::is_materialized_package_valid(package_path, artifact.content_sha256),
            "repair must restore exact materialized content");
    require(std::filesystem::is_regular_file(package_path / artifact.entry),
            "repair must restore artifact source files, not only the manifest");

    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    try {
        test_initialize_load_lock_and_check();
        test_lock_detects_manifest_drift();
        test_strict_manifest_keys();
        test_dependency_manifest_is_canonical_and_deterministic();
        test_add_remove_and_dependency_lock_drift();
        test_absolute_dependency_path_rejected();
        test_registry_path_dependency_rejected_online();
        test_registry_path_dependency_rejected_offline();
        test_corrupted_offline_materialization_fails();
        test_sync_project_check_project_no_stale_lock();
        test_verify_or_repair_materialization_restores_sources();
        std::cout << "✅ project workflow tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
