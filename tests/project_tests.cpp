#include "emojineer/project.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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
        {{"zeta", std::filesystem::path("../zeta")},
         {"alpha", std::filesystem::path("../alpha")}}
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
    require(lock.find("lock_version = 2") != std::string::npos,
            "dependency-aware lockfile should use lock format v2");
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
        {{"bad", std::filesystem::absolute("bad")}}
    };
    bool rejected = false;
    try {
        (void)emojineer::canonical_manifest_text(manifest);
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("relative") != std::string::npos;
    }
    require(rejected, "dependency paths must stay relative and checkout-portable");
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
        std::cout << "✅ project workflow tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
