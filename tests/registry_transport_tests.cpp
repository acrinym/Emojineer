#include "emojineer/project.hpp"
#include "emojineer/registry_transport.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("registry transport test failed: " + message);
}

struct TempRoot {
    std::filesystem::path path;
    explicit TempRoot(const std::string& suffix) {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("emojineer-registry-" + suffix + "-" + std::to_string(nonce));
        std::filesystem::remove_all(path);
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
    if (!output) throw std::runtime_error("cannot write registry test file");
    output << text;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read registry test file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void set_version(const std::filesystem::path& package_root, const std::string& version) {
    auto manifest = emojineer::load_project_manifest(package_root / "emojineer.toml");
    manifest.version = version;
    write_text(package_root / "emojineer.toml", emojineer::canonical_manifest_text(manifest));
}

void test_endpoint_contract_and_registry_identity() {
    TempRoot root("endpoint");
    emojineer::initialize_file_registry(root.path / "registry", "test.registry");
    const auto endpoint = emojineer::parse_registry_endpoint((root.path / "registry").string());
    require(endpoint.kind == emojineer::RegistryTransportKind::File,
            "plain path should become file registry endpoint");
    require(endpoint.canonical.starts_with("file://"),
            "file endpoint should have canonical file identity");
    require(emojineer::registry_identity(endpoint) == "test.registry",
            "registry descriptor should preserve identity");

    emojineer::initialize_file_registry(root.path / "registry", "test.registry");
    bool conflict = false;
    try {
        emojineer::initialize_file_registry(root.path / "registry", "other.registry");
    } catch (const std::runtime_error&) {
        conflict = true;
    }
    require(conflict, "registry identity should be immutable after initialization");

    bool rejected_http = false;
    try {
        (void)emojineer::parse_registry_endpoint("http://example.test/packages");
    } catch (const std::runtime_error&) {
        rejected_http = true;
    }
    require(rejected_http, "network registry endpoints should require HTTPS");

    const auto https = emojineer::parse_registry_endpoint("https://EXAMPLE.TEST/api/");
    require(https.kind == emojineer::RegistryTransportKind::Https,
            "HTTPS endpoint should select HTTPS transport");
    require(https.canonical == "https://example.test/api",
            "HTTPS endpoint should normalize host case and trailing slash");
}

void test_publish_is_immutable_and_idempotent() {
    TempRoot root("publish");
    const auto registry_root = root.path / "registry";
    const auto package_root = root.path / "package";
    emojineer::initialize_file_registry(registry_root, "local.dev");
    emojineer::initialize_project(package_root, "spark");
    set_version(package_root, "1.2.0");

    const auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());
    const auto first = emojineer::publish_package_to_registry(package_root, endpoint);
    require(!first.already_present, "first publication should create immutable artifact");
    require(std::filesystem::is_regular_file(first.artifact_path),
            "publication should store artifact by SHA-256");

    const auto second = emojineer::publish_package_to_registry(package_root, endpoint);
    require(second.already_present, "identical re-publication should be idempotent");
    require(second.record.artifact_sha256 == first.record.artifact_sha256,
            "idempotent publication should keep artifact identity");

    write_text(package_root / "src/main.emoji", "📝 📜changed without version bump📜\n");
    bool immutable_conflict = false;
    try {
        (void)emojineer::publish_package_to_registry(package_root, endpoint);
    } catch (const std::runtime_error& error) {
        immutable_conflict = std::string(error.what()).find("immutable registry version conflict") != std::string::npos;
    }
    require(immutable_conflict,
            "same package version must not be republished with different content");
}

void test_versions_selection_verified_fetch_and_cache_repair() {
    TempRoot root("fetch");
    const auto registry_root = root.path / "registry";
    const auto package_root = root.path / "package";
    const auto cache_root = root.path / "cache";
    emojineer::initialize_file_registry(registry_root, "selection.dev");
    emojineer::initialize_project(package_root, "signal");
    const auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());

    set_version(package_root, "1.2.0");
    write_text(package_root / "src/main.emoji", "📝 📜one-two📜\n");
    (void)emojineer::publish_package_to_registry(package_root, endpoint);

    set_version(package_root, "1.5.0");
    write_text(package_root / "src/main.emoji", "📝 📜one-five📜\n");
    const auto latest = emojineer::publish_package_to_registry(package_root, endpoint);

    set_version(package_root, "2.0.0-alpha.1");
    write_text(package_root / "src/main.emoji", "📝 📜two alpha📜\n");
    (void)emojineer::publish_package_to_registry(package_root, endpoint);

    const auto index = emojineer::load_registry_package_index(endpoint, "signal");
    require(index.registry_id == "selection.dev" && index.package_name == "signal",
            "package index should bind registry and package identity");
    require(index.versions.size() == 3, "package index should expose all immutable versions");
    const auto rendered = emojineer::render_registry_versions(index);
    require(rendered.find("1.2.0") != std::string::npos &&
                rendered.find("1.5.0") != std::string::npos &&
                rendered.find("2.0.0-alpha.1") != std::string::npos,
            "version listing should expose registry versions");

    const auto fetched = emojineer::fetch_registry_package(endpoint, "signal", "^1.2.0", cache_root);
    require(fetched.record.version == "1.5.0",
            "fetch should select highest compatible stable version");
    require(!fetched.cache_hit, "first fetch should populate cache");
    require(fetched.artifact.artifact_sha256 == latest.record.artifact_sha256,
            "fetched artifact must match index artifact identity");
    require(std::filesystem::is_regular_file(fetched.cache_path),
            "verified fetch should admit artifact into content-addressed cache");

    const auto cached = emojineer::fetch_registry_package(endpoint, "signal", "^1.2.0", cache_root);
    require(cached.cache_hit, "second fetch should reuse verified cache entry");
    require(cached.cache_path == fetched.cache_path,
            "cache reuse should preserve content-addressed path");

    write_text(fetched.cache_path, "corrupt cache");
    const auto repaired = emojineer::fetch_registry_package(endpoint, "signal", "^1.2.0", cache_root);
    require(!repaired.cache_hit, "corrupt cache entry should be discarded and re-fetched");
    require(repaired.artifact.artifact_sha256 == latest.record.artifact_sha256,
            "cache repair should restore selected immutable artifact");
}

void test_registry_artifact_and_index_tampering_is_rejected() {
    TempRoot root("tamper");
    const auto registry_root = root.path / "registry";
    const auto package_root = root.path / "package";
    emojineer::initialize_file_registry(registry_root, "tamper.dev");
    emojineer::initialize_project(package_root, "guarded");
    set_version(package_root, "3.0.0");
    const auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());
    const auto published = emojineer::publish_package_to_registry(package_root, endpoint);

    auto bytes = read_text(published.artifact_path);
    require(!bytes.empty(), "published artifact should contain bytes");
    bytes.back() = bytes.back() == 'x' ? 'y' : 'x';
    write_text(published.artifact_path, bytes);

    bool artifact_rejected = false;
    try {
        (void)emojineer::fetch_registry_package(endpoint, "guarded", "3.0.0", root.path / "fresh-cache");
    } catch (const std::runtime_error&) {
        artifact_rejected = true;
    }
    require(artifact_rejected, "tampered registry artifact must fail before cache admission");

    const auto index_path = registry_root / "v1/packages/guarded.index";
    auto index_text = read_text(index_path);
    const auto position = index_text.find("registry=tamper.dev");
    require(position != std::string::npos, "test index should contain registry identity");
    index_text.replace(position, std::string("registry=tamper.dev").size(), "registry=wrong.dev");
    write_text(index_path, index_text);

    bool identity_rejected = false;
    try {
        (void)emojineer::load_registry_package_index(endpoint, "guarded");
    } catch (const std::runtime_error&) {
        identity_rejected = true;
    }
    require(identity_rejected, "package index from another registry identity must be rejected");
}

void test_missing_version_reports_requirement() {
    TempRoot root("missing");
    const auto registry_root = root.path / "registry";
    const auto package_root = root.path / "package";
    emojineer::initialize_file_registry(registry_root, "missing.dev");
    emojineer::initialize_project(package_root, "onlyone");
    set_version(package_root, "1.0.0");
    const auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());
    (void)emojineer::publish_package_to_registry(package_root, endpoint);

    bool rejected = false;
    try {
        (void)emojineer::fetch_registry_package(endpoint, "onlyone", "^2.0.0", root.path / "cache");
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        rejected = message.find("onlyone") != std::string::npos && message.find("^2.0.0") != std::string::npos;
    }
    require(rejected, "missing selection diagnostic should name package and requirement");
}

} // namespace

int main() {
    try {
        test_endpoint_contract_and_registry_identity();
        test_publish_is_immutable_and_idempotent();
        test_versions_selection_verified_fetch_and_cache_repair();
        test_registry_artifact_and_index_tampering_is_rejected();
        test_missing_version_reports_requirement();
        std::cout << "registry transport tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
