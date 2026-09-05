#include "emojineer/project.hpp"
#include "emojineer/registry_discovery.hpp"
#include "emojineer/registry_transport.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("requirement failed: " + message);
}

template <typename Fn>
void require_throws(Fn&& fn, const std::string& message) {
    bool threw = false;
    try {
        fn();
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, message);
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write test file");
    out << text;
    if (!out) throw std::runtime_error("failed while writing test file");
}

void create_package(const std::filesystem::path& root,
                    const std::string& name,
                    const std::string& version,
                    const std::string& dependency = {},
                    const std::filesystem::path& dependency_path = {}) {
    emojineer::ProjectManifest manifest;
    manifest.name = name;
    manifest.version = version;
    manifest.entry = "src/main.emoji";
    if (!dependency.empty()) {
        manifest.dependencies.push_back(
            {dependency, emojineer::DependencyKind::Path, dependency_path, {}, {}});
    }
    write_text(root / "emojineer.toml", emojineer::canonical_manifest_text(manifest));
    write_text(root / "src/main.emoji", "📝 📜" + name + " " + version + "📜\n");
}

} // namespace

int main() {
    const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("emojineer-registry-discovery-" + std::to_string(nonce));
    try {
        const auto registry_root = root / "registry";
        emojineer::initialize_file_registry(registry_root, "discovery.test");
        const auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());

        const auto core = root / "core";
        create_package(core, "core", "1.0.0");
        (void)emojineer::publish_package_to_registry(core, endpoint);

        const auto consumer = root / "consumer";
        create_package(consumer, "consumer", "1.0.0", "core", "../core");
        (void)emojineer::publish_package_to_registry(consumer, endpoint);
        create_package(consumer, "consumer", "2.0.0-beta.1", "core", "../core");
        (void)emojineer::publish_package_to_registry(consumer, endpoint);

        const auto toolkit = root / "toolkit";
        create_package(toolkit, "toolkit", "1.4.0", "core", "../core");
        (void)emojineer::publish_package_to_registry(toolkit, endpoint);

        const auto edge = root / "edge";
        create_package(edge, "edge", "0.2.0-beta.2");
        (void)emojineer::publish_package_to_registry(edge, endpoint);

        const auto index = emojineer::load_registry_discovery(endpoint);
        require(index.registry_id == "discovery.test", "registry identity is preserved");
        require(index.records.size() == 5, "every immutable package version is represented");

        const auto wire = emojineer::render_registry_discovery_index(index);
        const auto reparsed = emojineer::parse_registry_discovery_index(wire);
        require(emojineer::render_registry_discovery_index(reparsed) == wire,
                "discovery wire format round-trips canonically");

        const auto stable_consumer = emojineer::select_registry_discovery_package(index, "consumer", false);
        require(stable_consumer.has_value(), "stable consumer is selectable");
        require(stable_consumer->version == "1.0.0", "stable selection excludes prerelease");

        const auto any_consumer = emojineer::select_registry_discovery_package(index, "consumer", true);
        require(any_consumer.has_value(), "consumer is selectable with prereleases");
        require(any_consumer->version == "2.0.0-beta.1", "include-prerelease selects highest SemVer");

        require(!emojineer::select_registry_discovery_package(index, "edge", false).has_value(),
                "prerelease-only package is absent from stable selection");
        require(emojineer::select_registry_discovery_package(index, "edge", true).has_value(),
                "prerelease-only package is discoverable explicitly");

        const auto dependency_keyword = emojineer::search_registry_discovery(index, "core", false);
        require(dependency_keyword.size() == 3,
                "keyword search matches package name and direct dependency metadata");
        require(dependency_keyword[0].package_name == "consumer" ||
                dependency_keyword[0].package_name == "core",
                "search results remain package-name sorted");

        const auto dependents = emojineer::reverse_registry_dependencies(index, "core", false);
        require(dependents.size() == 2, "reverse dependency query finds direct stable dependents");
        require(dependents[0].package_name == "consumer" && dependents[1].package_name == "toolkit",
                "reverse dependencies are deterministic");

        const auto dependents_with_pre = emojineer::reverse_registry_dependencies(index, "core", true);
        require(dependents_with_pre.size() == 2, "prerelease mode keeps one selected release per package");
        require(dependents_with_pre[0].version == "2.0.0-beta.1",
                "reverse dependency query observes selected prerelease version");

        const auto human = emojineer::render_registry_search(index, "core", false);
        require(human.find("consumer  1.0.0") != std::string::npos,
                "human search output contains selected package version");
        const auto json = emojineer::render_registry_search_json(index, "core", false);
        require(json.find("emojineer.registry-search.v1") != std::string::npos,
                "search JSON has deterministic schema identity");
        require(emojineer::render_registry_package_info_json(index, "consumer", true)
                    .find("2.0.0-beta.1") != std::string::npos,
                "package info JSON reports selected prerelease");
        require(emojineer::render_registry_dependents_json(index, "core", false)
                    .find("toolkit") != std::string::npos,
                "dependents JSON reports reverse dependency metadata");

        require_throws([&] {
            (void)emojineer::parse_registry_discovery_index("EMJREGDISC1\nregistry=bad id\n");
        }, "malformed registry identity is rejected");
        require_throws([&] {
            (void)emojineer::search_registry_discovery(index, "   ", false);
        }, "blank discovery query is rejected");

        std::filesystem::remove_all(root);
        std::cout << "registry discovery tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
