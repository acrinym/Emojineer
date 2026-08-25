#pragma once

#include "emojineer/package_artifact.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace emojineer {

enum class RegistryTransportKind {
    File,
    Https,
};

struct RegistryEndpoint {
    RegistryTransportKind kind = RegistryTransportKind::File;
    std::string canonical;
    std::filesystem::path file_root;
};

struct RegistryVersionRecord {
    std::string version;
    std::string content_sha256;
    std::string artifact_sha256;
};

struct RegistryPackageIndex {
    std::string registry_id;
    std::string package_name;
    std::vector<RegistryVersionRecord> versions;
};

struct RegistryPublishResult {
    RegistryVersionRecord record;
    std::filesystem::path artifact_path;
    bool already_present = false;
};

struct RegistryFetchResult {
    RegistryVersionRecord record;
    PackageArtifact artifact;
    std::filesystem::path cache_path;
    bool cache_hit = false;
};

// Authenticated publication credential - never serialized to manifests/artifacts/receipts
struct RegistryPublishCredential {
    std::string token;
    std::string namespace_id;
};

// Publication receipt - deterministic machine-readable output
struct PublicationReceipt {
    std::string registry_id;
    std::string package_name;
    std::string version;
    std::string content_sha256;
    std::string artifact_sha256;
    std::string protocol_version;
    std::string receipt_id;
    std::string timestamp;
};

// Parse and render publication receipt (JSON format for machine readability)
PublicationReceipt parse_publication_receipt(std::string_view text);
std::string render_publication_receipt(const PublicationReceipt& receipt);

// Verify receipt against expected values before reporting success
void verify_publication_receipt(const PublicationReceipt& receipt,
                                 const std::string& expected_registry_id,
                                 const std::string& expected_package_name,
                                 const std::string& expected_version,
                                 const std::string& expected_content_sha256,
                                 const std::string& expected_artifact_sha256);

RegistryEndpoint parse_registry_endpoint(std::string_view text);
std::filesystem::path default_registry_cache_root();

void initialize_file_registry(const std::filesystem::path& root,
                              const std::string& registry_id);
std::string registry_identity(const RegistryEndpoint& endpoint);
RegistryPackageIndex load_registry_package_index(const RegistryEndpoint& endpoint,
                                                 const std::string& package_name);
std::string render_registry_package_index(const RegistryPackageIndex& index);
std::string render_registry_versions(const RegistryPackageIndex& index);

// Get credential from environment variable (EMOJINEER_TOKEN)
std::optional<std::string> credential_from_environment();

// Get credential from explicit CLI input
RegistryPublishCredential parse_credential(std::string_view token, std::string_view namespace_id);

// File registry publication (no credentials required)
RegistryPublishResult publish_package_to_registry(
    const std::filesystem::path& package_root,
    const RegistryEndpoint& endpoint);

// Authenticated HTTPS publication
// Requires credentials from CLI/env/credential-store, never serialized
// Returns publication receipt for verification
PublicationReceipt publish_package_to_https_registry(
    const std::filesystem::path& package_root,
    const RegistryEndpoint& endpoint,
    const RegistryPublishCredential& credential);

// Verify and save receipt to file if requested
void save_receipt_file(const std::filesystem::path& path, const PublicationReceipt& receipt);

RegistryFetchResult fetch_registry_package(
    const RegistryEndpoint& endpoint,
    const std::string& package_name,
    std::string_view requirement,
    const std::filesystem::path& cache_root = {});

bool https_registry_transport_available();

} // namespace emojineer
