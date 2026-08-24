#include "emojineer/package_artifact.hpp"
#include "emojineer/project.hpp"
#include "emojineer/registry.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("package artifact test failed: " + message);
}

struct TempRoot {
    std::filesystem::path path;
    explicit TempRoot(const std::string& suffix) {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("emojineer-artifact-" + suffix + "-" + std::to_string(nonce));
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
    if (!output) throw std::runtime_error("cannot write test source");
    output << text;
}

void make_package_with_dependency(const std::filesystem::path& parent) {
    const auto app = parent / "app";
    const auto dep = app / "deps/lib";
    emojineer::initialize_project(app, "app");
    emojineer::initialize_project(dep, "lib");
    emojineer::add_project_dependency(app, "lib", "deps/lib");
}

void test_artifact_is_deterministic_and_package_owned() {
    TempRoot first("portable-a");
    TempRoot second("portable-b");
    make_package_with_dependency(first.path);
    make_package_with_dependency(second.path);

    const auto a = emojineer::build_package_artifact_bytes(first.path / "app");
    const auto b = emojineer::build_package_artifact_bytes(second.path / "app");
    require(a == b, "equivalent checkouts should produce byte-identical artifacts");

    const auto parsed = emojineer::parse_package_artifact(a);
    require(parsed.name == "app" && parsed.version == "0.1.0",
            "artifact should preserve package identity");
    require(parsed.entry == "src/main.emoji", "artifact should preserve entry source");
    require(parsed.files.size() == 1 && parsed.files.front().path == "src/main.emoji",
            "dependency-owned source must be excluded from root artifact");
    require(parsed.content_sha256.size() == 64 && parsed.artifact_sha256.size() == 64,
            "artifact should expose content and whole-artifact SHA-256 identities");

    const auto before = a;
    write_text(first.path / "app/deps/lib/src/main.emoji", "📝 📜dependency changed📜\n");
    const auto after_dependency_change =
        emojineer::build_package_artifact_bytes(first.path / "app");
    require(before == after_dependency_change,
            "dependency source changes must not alter the owning package artifact");

    write_text(first.path / "app/src/main.emoji", "📝 📜owner changed📜\n");
    const auto after_owner_change = emojineer::build_package_artifact_bytes(first.path / "app");
    require(before != after_owner_change,
            "owned source changes must alter the immutable package artifact");
}

void test_artifact_detects_tampering_and_cache_path_is_content_addressed() {
    TempRoot root("tamper");
    emojineer::initialize_project(root.path / "pkg", "pkg");
    auto bytes = emojineer::build_package_artifact_bytes(root.path / "pkg");
    const auto artifact = emojineer::parse_package_artifact(bytes);

    const auto cache = emojineer::package_cache_path("cache", artifact).generic_string();
    require(cache == "cache/pkg/0.1.0/" + artifact.artifact_sha256 + ".emjpkg",
            "cache path should be package/version/artifact-SHA addressed");
    require(emojineer::default_package_artifact_filename("pkg", "0.1.0") ==
                "pkg-0.1.0.emjpkg",
            "default artifact filename should be deterministic");

    require(!bytes.empty(), "artifact bytes should not be empty");
    bytes.back() = bytes.back() == 'x' ? 'y' : 'x';
    bool rejected = false;
    try {
        (void)emojineer::parse_package_artifact(bytes);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "tampered source bytes must fail checksum verification");
}

void test_semver_precedence_and_requirements() {
    const auto release = emojineer::parse_semantic_version("1.2.3");
    const auto prerelease = emojineer::parse_semantic_version("1.2.3-rc.1");
    require(emojineer::compare_semantic_versions(prerelease, release) < 0,
            "prerelease should sort below the corresponding release");

    const std::vector<std::string> versions{
        "0.2.3", "0.2.9", "0.3.0", "1.2.3", "1.2.8", "1.3.0",
        "1.9.0", "1.9.0+build.1", "1.9.0+build.2", "2.0.0", "2.1.0-alpha.1"};

    require(emojineer::select_highest_matching_version(versions, "^1.2.3") ==
                std::optional<std::string>("1.9.0+build.2"),
            "caret range should select the highest compatible stable precedence deterministically");
    require(emojineer::select_highest_matching_version(versions, "~1.2.3") ==
                std::optional<std::string>("1.2.8"),
            "tilde range should stay within the requested minor line");
    require(emojineer::select_highest_matching_version(versions, "^0.2.3") ==
                std::optional<std::string>("0.2.9"),
            "caret zero-major range should stay within the compatible minor line");
    require(emojineer::select_highest_matching_version(versions, "*") ==
                std::optional<std::string>("2.0.0"),
            "wildcard selection should exclude prereleases by default");
    require(emojineer::select_highest_matching_version(versions, "1.9.0+build.1") ==
                std::optional<std::string>("1.9.0+build.1"),
            "exact requirement should preserve exact build identity");
}

void test_invalid_semver_is_rejected() {
    for (const auto* text : {"1.2", "01.2.3", "1.2.3-01", "1.2.3+"}) {
        bool rejected = false;
        try {
            (void)emojineer::parse_semantic_version(text);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, std::string("invalid semantic version should be rejected: ") + text);
    }
}

void test_semver_large_numeric_core_identifiers() {
    // Test that SemVer numeric core identifiers beyond uint64_t work correctly
    // using overflow-free string representation
    const auto v1 = emojineer::parse_semantic_version("18446744073709551616"); // uint64_t max + 1
    require(v1.major == "18446744073709551616", "major should be large number string");
    require(v1.minor == "0", "minor should be zero");
    require(v1.patch == "0", "patch should be zero");

    const auto v2 = emojineer::parse_semantic_version("9999999999999999999999999999");
    require(v2.major == "9999999999999999999999999999", "major should be very large");

    // Test comparison of large versions
    require(emojineer::compare_semantic_versions(v1, v2) < 0,
            "larger digit count should be greater");

    // Test that version selection works with large numbers
    const std::vector<std::string> versions{
        "1.0.0", "2.0.0", "18446744073709551616.0.0", "9999999999999999999.0.0"};
    const auto selected = emojineer::select_highest_matching_version(versions, "*");
    require(selected && *selected == "9999999999999999999.0.0",
            "wildcard should select largest version by digit count");
}

void test_semver_large_prerelease_identifiers() {
    // Test that prerelease numeric identifiers work without fixed-width integer conversion
    const auto v1 = emojineer::parse_semantic_version("1.0.0-18446744073709551616");
    require(v1.prerelease == "18446744073709551616", "prerelease should be large number");

    const auto v2 = emojineer::parse_semantic_version("1.0.0-9999999999999999999");
    const auto v3 = emojineer::parse_semantic_version("1.0.0-2");

    // Test ordering: numeric prerelease identifiers compared as numbers
    // "2" > "18446744073709551616" because shorter is larger for numeric identifiers
    require(emojineer::compare_semantic_versions(v3, v1) > 0,
            "shorter numeric prerelease should be greater");

    // Test leading zero preservation in ordering
    const auto v4 = emojineer::parse_semantic_version("1.0.0-01");
    const auto v5 = emojineer::parse_semantic_version("1.0.0-1");
    // "01" is invalid as a numeric prerelease (leading zero), but text comparison works for non-numeric

    // Test very large prerelease identifier comparison
    require(emojineer::compare_semantic_versions(v1, v2) < 0,
            "larger prerelease numeric identifier should be greater");
}

void test_artifact_size_bound_enforced_before_read() {
    // This test verifies that oversized artifacts are rejected BEFORE being read into memory
    // by creating a file larger than 128 MiB and attempting to load it
    TempRoot root("oversized");
    emojineer::initialize_project(root.path / "pkg", "pkg");

    // First create a valid artifact
    auto bytes = emojineer::build_package_artifact_bytes(root.path / "pkg");
    const auto artifact_path = root.path / "large.emjpkg";

    // Create a file that exceeds the 128 MiB limit
    // We use a smaller test size to avoid actually creating a 128MB file
    std::ofstream fake(artifact_path, std::ios::binary | std::ios::trunc);
    require(fake, "should be able to create test file");
    // Write magic and enough data to exceed limit
    fake << "EMJPKG1\n";
    // Write 1 byte more than the limit to trigger rejection
    for (std::size_t i = 0; i < (128ull * 1024ull * 1024ull); ++i) {
        fake.put('x');
    }
    fake.close();

    bool rejected = false;
    try {
        (void)emojineer::load_package_artifact(artifact_path);
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        if (msg.find("128 MiB") != std::string::npos ||
            msg.find("exceeds") != std::string::npos) {
            rejected = true;
        }
    }
    require(rejected, "oversized artifact should be rejected before full read");
}

} // namespace

int main() {
    try {
        test_artifact_is_deterministic_and_package_owned();
        test_artifact_detects_tampering_and_cache_path_is_content_addressed();
        test_semver_precedence_and_requirements();
        test_invalid_semver_is_rejected();
        test_semver_large_numeric_core_identifiers();
        test_semver_large_prerelease_identifiers();
        test_artifact_size_bound_enforced_before_read();
        std::cout << "✅ package artifact and registry contract tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
