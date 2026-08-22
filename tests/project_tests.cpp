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

void test_initialize_load_lock_and_check() {
    const auto root = temp_root("project");
    std::filesystem::remove_all(root);
    emojineer::initialize_project(root, "demo_project");

    const auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    require(manifest.name == "demo_project", "initialized project name should round-trip");
    require(manifest.version == "0.1.0", "initialized version should be 0.1.0");
    require(manifest.entry.generic_string() == "src/main.emoji", "default entry should be src/main.emoji");
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
    std::ofstream output(root / "emojineer.toml", std::ios::binary | std::ios::trunc);
    output << emojineer::canonical_manifest_text(manifest);
    output.close();

    const auto diagnostics = emojineer::check_project(root);
    bool found_stale = false;
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.message.find("stale") != std::string::npos) found_stale = true;
    }
    require(found_stale, "manifest changes should make the lockfile stale");
    std::filesystem::remove_all(root);
}

void test_strict_manifest_keys() {
    const auto root = temp_root("strict-manifest");
    std::filesystem::create_directories(root);
    std::ofstream output(root / "emojineer.toml", std::ios::binary);
    output << "[package]\n"
              "name = \"demo\"\n"
              "version = \"0.1.0\"\n"
              "entry = \"src/main.emoji\"\n"
              "mystery = \"nope\"\n";
    output.close();

    bool rejected = false;
    try {
        (void)emojineer::load_project_manifest(root / "emojineer.toml");
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("unknown") != std::string::npos;
    }
    require(rejected, "unknown manifest keys should be rejected instead of silently ignored");
    std::filesystem::remove_all(root);
}

void test_manifest_hash_is_deterministic() {
    emojineer::ProjectManifest manifest{"hash_demo", "1.2.3", std::filesystem::path("src/main.emoji")};
    const auto first = emojineer::project_manifest_hash(manifest);
    const auto second = emojineer::project_manifest_hash(manifest);
    require(first == second && first.size() == 16, "manifest hash should be stable 64-bit hex");
}

} // namespace

int main() {
    try {
        test_initialize_load_lock_and_check();
        test_lock_detects_manifest_drift();
        test_strict_manifest_keys();
        test_manifest_hash_is_deterministic();
        std::cout << "✅ project workflow tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
