#pragma once

#include "emojineer/package_artifact.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

// Publication authority is external process input. It is never serialized into
// manifests, lockfiles, package artifacts, receipts, or source files.
struct RegistryPublishCredential {
    std::string token;
    std::string namespace_id;
};

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

PublicationReceipt parse_publication_receipt(std::string_view text);
std::string render_publication_receipt(const PublicationReceipt& receipt);
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
std::string read_registry_resource(const RegistryEndpoint& endpoint,
                                   std::string_view resource,
                                   std::size_t max_bytes);
RegistryPackageIndex load_registry_package_index(const RegistryEndpoint& endpoint,
                                                 const std::string& package_name);
std::string render_registry_package_index(const RegistryPackageIndex& index);
std::string render_registry_versions(const RegistryPackageIndex& index);

std::optional<std::string> credential_from_environment();
RegistryPublishCredential parse_credential(std::string_view token,
                                           std::string_view namespace_id);

RegistryPublishResult publish_package_to_registry(
    const std::filesystem::path& package_root,
    const RegistryEndpoint& endpoint);

PublicationReceipt publish_package_to_https_registry(
    const std::filesystem::path& package_root,
    const RegistryEndpoint& endpoint,
    const RegistryPublishCredential& credential);

void save_receipt_file(const std::filesystem::path& path,
                       const PublicationReceipt& receipt);

RegistryFetchResult fetch_registry_package(
    const RegistryEndpoint& endpoint,
    const std::string& package_name,
    std::string_view requirement,
    const std::filesystem::path& cache_root = {});

bool https_registry_transport_available();

// The transport seam is below the language/runtime authority boundary.
// Production uses strict libcurl HTTPS; the interoperability CTest fixture
// consumes the same encoded request without adding a fake product success path.
namespace publication_protocol {

inline constexpr std::string_view version = "emjpub1";
inline constexpr std::string_view request_media_type =
    "application/vnd.emojineer.publish.v1+octet-stream";
inline constexpr std::string_view receipt_media_type =
    "application/vnd.emojineer.publish-receipt.v1+json";
inline constexpr std::size_t max_upload_bytes = 128ull * 1024ull * 1024ull;
inline constexpr std::size_t max_receipt_bytes = 16ull * 1024ull;
inline constexpr long connect_timeout_seconds = 10;
inline constexpr long upload_timeout_seconds = 300;
inline constexpr long header_timeout_seconds = 30;
inline constexpr long response_body_timeout_seconds = 300;

struct HttpRequest {
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

struct HttpResponse {
    long status = 0;
    std::string content_type;
    std::string body;
};

using IdentityLookup = std::function<std::string(const RegistryEndpoint&)>;
using Exchange = std::function<HttpResponse(const HttpRequest&)>;

PublicationReceipt publish_with_transport(
    const std::filesystem::path& package_root,
    const RegistryEndpoint& endpoint,
    const RegistryPublishCredential& credential,
    const IdentityLookup& identity_lookup,
    const Exchange& exchange);

} // namespace publication_protocol

} // namespace emojineer
