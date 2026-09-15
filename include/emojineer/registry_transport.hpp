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

/// Concrete transport family supported by the registry tooling layer.
enum class RegistryTransportKind {
    File,
    Https,
};

/// Registry endpoint state used by transport operations.
///
/// parse_registry_endpoint() produces canonical validated instances; transport
/// callers should use that parser rather than constructing unvalidated endpoints.
struct RegistryEndpoint {
    RegistryTransportKind kind = RegistryTransportKind::File;
    std::string canonical;
    std::filesystem::path file_root;
};

/// Immutable package-version identity fields recorded by EMJREGPKG1.
struct RegistryVersionRecord {
    std::string version;
    std::string content_sha256;
    std::string artifact_sha256;
};

/// In-memory representation of one EMJREGPKG1 package index.
struct RegistryPackageIndex {
    std::string registry_id;
    std::string package_name;
    std::vector<RegistryVersionRecord> versions;
};

/// Result of credential-free publication into a local/file registry.
struct RegistryPublishResult {
    RegistryVersionRecord record;
    std::filesystem::path artifact_path;
    bool already_present = false;
};

/// Verified artifact and cache provenance returned by registry fetch.
struct RegistryFetchResult {
    RegistryVersionRecord record;
    PackageArtifact artifact;
    std::filesystem::path cache_path;
    bool cache_hit = false;
};

/// External publication authority supplied by the invoking process.
///
/// Secrets are never serialized into manifests, lockfiles, package artifacts,
/// receipts, or source files.
struct RegistryPublishCredential {
    std::string token;
    std::string namespace_id;
};

/// In-memory fields of an authenticated-publication receipt.
///
/// Shape validation is performed by parse/render paths; binding these fields to
/// a specific publication attempt requires verify_publication_receipt().
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

/// Parse bounded publication-receipt JSON with an exact allowed field set.
///
/// Duplicate, unknown, missing, non-string, malformed, and trailing JSON content
/// is rejected. Input key order and insignificant JSON whitespace are accepted;
/// canonical field ordering is a property of render_publication_receipt().
PublicationReceipt parse_publication_receipt(std::string_view text);

/// Serialize a publication receipt in deterministic canonical field order without credentials.
std::string render_publication_receipt(const PublicationReceipt& receipt);

/// Prove that a returned receipt describes the exact attempted publication.
void verify_publication_receipt(const PublicationReceipt& receipt,
                                const std::string& expected_registry_id,
                                const std::string& expected_package_name,
                                const std::string& expected_version,
                                const std::string& expected_content_sha256,
                                const std::string& expected_artifact_sha256);

/// Parse a local/file or HTTPS endpoint into canonical transport state.
///
/// Plain HTTP and unsafe endpoint forms are rejected rather than upgraded.
RegistryEndpoint parse_registry_endpoint(std::string_view text);

/// Return the platform-specific default registry cache root used when no override is supplied.
std::filesystem::path default_registry_cache_root();

/// Initialize a credential-free file registry with an EMJREGISTRY1 identity.
void initialize_file_registry(const std::filesystem::path& root,
                              const std::string& registry_id);

/// Read and validate the registry identity descriptor for an endpoint.
std::string registry_identity(const RegistryEndpoint& endpoint);

/// Read one bounded registry resource through the shared strict transport seam.
///
/// HTTPS reads inherit the registry transport's TLS, no-redirect, timeout, and
/// response-size policy. This is package-manager/tooling authority, not VM authority.
std::string read_registry_resource(const RegistryEndpoint& endpoint,
                                   std::string_view resource,
                                   std::size_t max_bytes);

/// Load and validate one package's immutable EMJREGPKG1 version index.
RegistryPackageIndex load_registry_package_index(const RegistryEndpoint& endpoint,
                                                 const std::string& package_name);

/// Serialize a package index in deterministic canonical order.
std::string render_registry_package_index(const RegistryPackageIndex& index);

/// Render user-facing version rows from a validated package index.
std::string render_registry_versions(const RegistryPackageIndex& index);

/// Return the publication bearer token from process environment when present.
///
/// The optional value is process input only and is never persisted by this API.
std::optional<std::string> credential_from_environment();

/// Validate bearer-token and namespace syntax before constructing credentials.
RegistryPublishCredential parse_credential(std::string_view token,
                                           std::string_view namespace_id);

/// Publish an immutable package into a local/file registry.
///
/// Exact republishing is idempotent; conflicting content under the same package
/// version is rejected rather than replaced.
RegistryPublishResult publish_package_to_registry(
    const std::filesystem::path& package_root,
    const RegistryEndpoint& endpoint);

/// Perform authenticated immutable HTTPS publication and verify its receipt.
PublicationReceipt publish_package_to_https_registry(
    const std::filesystem::path& package_root,
    const RegistryEndpoint& endpoint,
    const RegistryPublishCredential& credential);

/// Validate receipt shape through canonical rendering, then persist it atomically.
///
/// Cross-checking the receipt against a specific publication attempt is the caller's
/// responsibility via verify_publication_receipt() or publish_with_transport().
void save_receipt_file(const std::filesystem::path& path,
                       const PublicationReceipt& receipt);

/// Resolve, fetch, verify, and cache one package satisfying a SemVer requirement.
///
/// Cache hits are re-verified; corrupt entries are discarded and repaired from
/// the registry before a PackageArtifact is returned.
RegistryFetchResult fetch_registry_package(
    const RegistryEndpoint& endpoint,
    const std::string& package_name,
    std::string_view requirement,
    const std::filesystem::path& cache_root = {});

/// Report whether this build can perform strict HTTPS registry transport.
bool https_registry_transport_available();

/// Versioned authenticated-publication protocol seams used by production and
/// the interoperability fixture. They remain below the language/runtime authority boundary.
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

/// Encoded publication request value passed to an exchange implementation.
struct HttpRequest {
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

/// HTTP response value consumed by bounded publication-receipt validation.
struct HttpResponse {
    long status = 0;
    std::string content_type;
    std::string body;
};

/// Registry-identity lookup seam; production still uses strict registry transport.
using IdentityLookup = std::function<std::string(const RegistryEndpoint&)>;

/// HTTP exchange seam used to exercise the exact encoded protocol in tests.
using Exchange = std::function<HttpResponse(const HttpRequest&)>;

/// Encode, exchange, and validate one authenticated publication transaction.
///
/// Credential/header construction, immutable artifact identity, registry identity,
/// response status/content type, and receipt identity are all verified here so the
/// interoperability fixture cannot create a fake success path around production logic.
PublicationReceipt publish_with_transport(
    const std::filesystem::path& package_root,
    const RegistryEndpoint& endpoint,
    const RegistryPublishCredential& credential,
    const IdentityLookup& identity_lookup,
    const Exchange& exchange);

} // namespace publication_protocol

} // namespace emojineer
