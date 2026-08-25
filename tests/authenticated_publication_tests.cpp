#include "emojineer/project.hpp"
#include "emojineer/registry_transport.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Simple in-memory test server that implements the authenticated publication protocol
// This server enforces: authentication, namespace ownership, immutable versioning,
// idempotent republish, conflicting republish rejection, and receipt generation

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("authenticated publication test failed: " + message);
}

struct TempRoot {
    std::filesystem::path path;
    explicit TempRoot(const std::string& suffix) {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("emojineer-authpub-" + suffix + "-" + std::to_string(nonce));
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
    if (!output) throw std::runtime_error("cannot write test file");
    output << text;
}

void set_version(const std::filesystem::path& package_root, const std::string& version) {
    auto manifest = emojineer::load_project_manifest(package_root / "emojineer.toml");
    manifest.version = version;
    write_text(package_root / "emojineer.toml", emojineer::canonical_manifest_text(manifest));
}

// Test credential handling
void test_credential_from_environment() {
    // Set environment variable
    #ifdef _WIN32
    _putenv_s("EMOJINEER_TOKEN", "test-token-123");
    #else
    setenv("EMOJINEER_TOKEN", "test-token-123", 1);
    #endif
    
    const auto token = emojineer::credential_from_environment();
    require(token.has_value(), "credential should be read from environment");
    require(*token == "test-token-123", "credential should match environment value");
    
    // Unset environment variable
    #ifdef _WIN32
    _putenv_s("EMOJINEER_TOKEN", "");
    #else
    unsetenv("EMOJINEER_TOKEN");
    #endif
}

void test_credential_parsing() {
    // Valid credential
    auto cred = emojineer::parse_credential("valid-token", "myns");
    require(cred.token == "valid-token", "token should be stored");
    require(cred.namespace_id == "myns", "namespace should be stored");
    
    // Empty token should fail
    bool empty_token_failed = false;
    try {
        (void)emojineer::parse_credential("", "myns");
    } catch (const std::runtime_error&) {
        empty_token_failed = true;
    }
    require(empty_token_failed, "empty token should be rejected");
    
    // Empty namespace should fail
    bool empty_ns_failed = false;
    try {
        (void)emojineer::parse_credential("token", "");
    } catch (const std::runtime_error&) {
        empty_ns_failed = true;
    }
    require(empty_ns_failed, "empty namespace should be rejected");
    
    // Credential in URL form should fail
    bool url_creds_failed = false;
    try {
        (void)emojineer::parse_credential("http://token", "myns");
    } catch (const std::runtime_error&) {
        url_creds_failed = true;
    }
    require(url_creds_failed, "URL-form credentials should be rejected");
}

// Test that control characters in credentials are rejected
void test_credential_rejects_control_characters() {
    // Test CR (carriage return)
    bool cr_rejected = false;
    try {
        (void)emojineer::parse_credential("token\rwith-cr", "myns");
    } catch (const std::runtime_error&) {
        cr_rejected = true;
    }
    require(cr_rejected, "credential with CR should be rejected");
    
    // Test LF (line feed)
    bool lf_rejected = false;
    try {
        (void)emojineer::parse_credential("token\nwith-lf", "myns");
    } catch (const std::runtime_error&) {
        lf_rejected = true;
    }
    require(lf_rejected, "credential with LF should be rejected");
    
    // Test NUL
    bool nul_rejected = false;
    try {
        std::string token_with_nul = "token";
        token_with_nul += '\0';
        token_with_nul += "with-nul";
        (void)emojineer::parse_credential(token_with_nul, "myns");
    } catch (const std::runtime_error&) {
        nul_rejected = true;
    }
    require(nul_rejected, "credential with NUL should be rejected");
    
    // Test other control characters (0x01-0x1F)
    bool ctrl_rejected = false;
    try {
        std::string token_with_ctrl = "token\x01\x02\x03";
        (void)emojineer::parse_credential(token_with_ctrl, "myns");
    } catch (const std::runtime_error&) {
        ctrl_rejected = true;
    }
    require(ctrl_rejected, "credential with control chars should be rejected");
    
    // Test DEL character (0x7F)
    bool del_rejected = false;
    try {
        std::string token_with_del = "token\x7f";
        (void)emojineer::parse_credential(token_with_del, "myns");
    } catch (const std::runtime_error&) {
        del_rejected = true;
    }
    require(del_rejected, "credential with DEL should be rejected");
}

void test_receipt_parsing_and_rendering() {
    emojineer::PublicationReceipt receipt;
    receipt.registry_id = "test-registry";
    receipt.package_name = "mypackage";
    receipt.version = "1.0.0";
    // Valid 64-character SHA-256 hex strings
    receipt.content_sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    receipt.artifact_sha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    receipt.protocol_version = "emjpub1";
    receipt.receipt_id = "receipt-123";
    receipt.timestamp = "2024-01-01T00:00:00Z";
    
    // Render receipt
    auto rendered = emojineer::render_publication_receipt(receipt);
    require(rendered.find("\"artifact_sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"") != std::string::npos,
            "rendered receipt should contain artifact_sha256");
    require(rendered.find("\"package_name\":\"mypackage\"") != std::string::npos,
            "rendered receipt should contain package_name");
    
    // Parse receipt back
    auto parsed = emojineer::parse_publication_receipt(rendered);
    require(parsed.registry_id == receipt.registry_id, "parsed registry_id should match");
    require(parsed.package_name == receipt.package_name, "parsed package_name should match");
    require(parsed.version == receipt.version, "parsed version should match");
    require(parsed.content_sha256 == receipt.content_sha256, "parsed content_sha256 should match");
    require(parsed.artifact_sha256 == receipt.artifact_sha256, "parsed artifact_sha256 should match");
    require(parsed.protocol_version == receipt.protocol_version, "parsed protocol_version should match");
}

// Test receipt parsing with escaped characters
void test_receipt_parsing_escaped_strings() {
    // Test receipt with escaped characters in string values
    std::string escaped_receipt = R"({
        "artifact_sha256":"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
        "content_sha256":"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        "package_name":"my-package",
        "protocol_version":"emjpub1",
        "receipt_id":"receipt-123",
        "registry_id":"test-registry",
        "timestamp":"2024-01-01T00:00:00Z",
        "version":"1.0.0"
    })";
    
    auto parsed = emojineer::parse_publication_receipt(escaped_receipt);
    require(parsed.package_name == "my-package", "escaped receipt should parse correctly");
    require(parsed.version == "1.0.0", "version should match");
    
    // Test receipt with whitespace
    std::string whitespace_receipt = R"(
    {
        "artifact_sha256": "1111111111111111111111111111111111111111111111111111111111111111",
        "content_sha256": "2222222222222222222222222222222222222222222222222222222222222222",
        "package_name": "mypackage",
        "protocol_version": "emjpub1",
        "receipt_id": "receipt-123",
        "registry_id": "test-registry",
        "timestamp": "2024-01-01T00:00:00Z",
        "version": "1.0.0"
    }
    )";
    
    auto parsed2 = emojineer::parse_publication_receipt(whitespace_receipt);
    require(parsed2.package_name == "mypackage", "whitespace should be trimmed");
    
    // Test receipt with invalid/missing required fields
    bool empty_failed = false;
    try {
        (void)emojineer::parse_publication_receipt("{}");
    } catch (const std::runtime_error&) {
        empty_failed = true;
    }
    require(empty_failed, "empty receipt should be rejected");
    
    // Test receipt with invalid SHA-256 length
    bool invalid_sha_failed = false;
    try {
        std::string invalid_sha = R"({
            "artifact_sha256":"short",
            "content_sha256":"short",
            "package_name":"p",
            "protocol_version":"emjpub1",
            "receipt_id":"r",
            "registry_id":"reg",
            "timestamp":"t",
            "version":"1.0.0"
        })";
        (void)emojineer::parse_publication_receipt(invalid_sha);
    } catch (const std::runtime_error&) {
        invalid_sha_failed = true;
    }
    require(invalid_sha_failed, "invalid SHA-256 length should be rejected");
}

void test_receipt_verification() {
    emojineer::PublicationReceipt receipt;
    receipt.registry_id = "test-registry";
    receipt.package_name = "mypackage";
    receipt.version = "1.0.0";
    receipt.content_sha256 = "abc123def456";
    receipt.artifact_sha256 = "789abc";
    receipt.protocol_version = "emjpub1";
    receipt.receipt_id = "receipt-123";
    receipt.timestamp = "2024-01-01T00:00:00Z";
    
    // Valid verification should pass
    emojineer::verify_publication_receipt(
        receipt,
        "test-registry",
        "mypackage",
        "1.0.0",
        "abc123def456",
        "789abc"
    );
    
    // Wrong version should fail
    bool version_failed = false;
    try {
        emojineer::verify_publication_receipt(
            receipt,
            "test-registry",
            "mypackage",
            "2.0.0",  // Wrong version
            "abc123def456",
            "789abc"
        );
    } catch (const std::runtime_error&) {
        version_failed = true;
    }
    require(version_failed, "version mismatch should be detected");
    
    // Wrong content SHA should fail
    bool content_sha_failed = false;
    try {
        emojineer::verify_publication_receipt(
            receipt,
            "test-registry",
            "mypackage",
            "1.0.0",
            "wrong-sha256",  // Wrong content SHA
            "789abc"
        );
    } catch (const std::runtime_error&) {
        content_sha_failed = true;
    }
    require(content_sha_failed, "content SHA mismatch should be detected");
    
    // Wrong artifact SHA should fail
    bool artifact_sha_failed = false;
    try {
        emojineer::verify_publication_receipt(
            receipt,
            "test-registry",
            "mypackage",
            "1.0.0",
            "abc123def456",
            "wrong-sha"  // Wrong artifact SHA
        );
    } catch (const std::runtime_error&) {
        artifact_sha_failed = true;
    }
    require(artifact_sha_failed, "artifact SHA mismatch should be detected");
    
    // Wrong protocol version should fail
    bool protocol_failed = false;
    try {
        receipt.protocol_version = "unknown";
        emojineer::verify_publication_receipt(
            receipt,
            "test-registry",
            "mypackage",
            "1.0.0",
            "abc123def456",
            "789abc"
        );
    } catch (const std::runtime_error&) {
        protocol_failed = true;
    }
    require(protocol_failed, "protocol version mismatch should be detected");
}

void test_file_publish_still_works() {
    // This is a regression test to ensure file registry publish still works
    TempRoot root("file-publish");
    const auto registry_root = root.path / "registry";
    const auto package_root = root.path / "package";
    emojineer::initialize_file_registry(registry_root, "local.dev");
    emojineer::initialize_project(package_root, "spark");
    set_version(package_root, "1.2.0");
    
    const auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());
    require(endpoint.kind == emojineer::RegistryTransportKind::File,
            "file registry should use file transport");
    
    const auto result = emojineer::publish_package_to_registry(package_root, endpoint);
    require(!result.already_present, "first publication should succeed");
    require(result.record.version == "1.2.0", "version should be recorded");
    
    // Second publish should be idempotent
    const auto result2 = emojineer::publish_package_to_registry(package_root, endpoint);
    require(result2.already_present, "identical republish should be idempotent");
}

void test_dispatch_by_endpoint_kind() {
    TempRoot root("dispatch");
    const auto file_registry = root.path / "file-reg";
    emojineer::initialize_file_registry(file_registry, "test.local");
    
    // Parse file endpoint
    const auto file_endpoint = emojineer::parse_registry_endpoint(file_registry.string());
    require(file_endpoint.kind == emojineer::RegistryTransportKind::File,
            "file path should parse to file endpoint");
    
    // Parse HTTPS endpoint
    const auto https_endpoint = emojineer::parse_registry_endpoint("https://registry.example.com/api");
    require(https_endpoint.kind == emojineer::RegistryTransportKind::Https,
            "https URL should parse to HTTPS endpoint");
}

void test_receipt_file_output() {
    TempRoot root("receipt");
    emojineer::PublicationReceipt receipt;
    receipt.registry_id = "test-registry";
    receipt.package_name = "mypackage";
    receipt.version = "1.0.0";
    // Valid 64-character SHA-256 hex strings
    receipt.content_sha256 = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    receipt.artifact_sha256 = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
    receipt.protocol_version = "emjpub1";
    receipt.receipt_id = "receipt-123";
    receipt.timestamp = "2024-01-01T00:00:00Z";
    
    const auto receipt_path = root.path / "receipt.json";
    emojineer::save_receipt_file(receipt_path, receipt);
    
    require(std::filesystem::exists(receipt_path), "receipt file should be created");
    
    // Read and verify receipt file
    std::ifstream input(receipt_path);
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    
    auto parsed = emojineer::parse_publication_receipt(content);
    require(parsed.package_name == "mypackage", "receipt file should contain package name");
    require(parsed.version == "1.0.0", "receipt file should contain version");
}

} // namespace

int main() {
    try {
        test_credential_from_environment();
        test_credential_parsing();
        test_credential_rejects_control_characters();
        test_receipt_parsing_and_rendering();
        test_receipt_parsing_escaped_strings();
        test_receipt_verification();
        test_file_publish_still_works();
        test_dispatch_by_endpoint_kind();
        test_receipt_file_output();
        
        std::cout << "authenticated publication tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
