#pragma once

#include "emojineer/registry_transport.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace emojineer {

/// Fields for one immutable package release exposed through registry discovery metadata.
///
/// The hashes are lookup metadata only: package admission still re-verifies the
/// canonical package index and immutable artifact through the registry transport.
struct RegistryDiscoveryRecord {
    std::string package_name;
    std::string version;
    std::string content_sha256;
    std::string artifact_sha256;
    std::string entry;
    std::vector<std::string> dependencies;
};

/// In-memory representation of an EMJREGDISC1 view for one registry identity.
///
/// Canonical ordering and field validity are enforced by parse/render/load APIs,
/// not by aggregate construction of this public value type.
struct RegistryDiscoveryIndex {
    std::string registry_id;
    std::vector<RegistryDiscoveryRecord> records;
};

/// Parse and validate canonical EMJREGDISC1 text.
///
/// Rejects malformed identities, package/version/hash fields, invalid entry
/// paths, invalid dependency names, duplicate/out-of-order records, and bounds
/// violations rather than silently normalizing untrusted registry input.
RegistryDiscoveryIndex parse_registry_discovery_index(std::string_view text);

/// Serialize a validated discovery index into deterministic EMJREGDISC1 form.
std::string render_registry_discovery_index(const RegistryDiscoveryIndex& index);

/// Load discovery metadata through the endpoint's real registry authority path.
///
/// File registries derive the view from package indexes plus verified artifacts;
/// HTTPS registries read the bounded v1/discovery.index resource through the
/// shared strict registry transport. This function does not grant VM authority.
RegistryDiscoveryIndex load_registry_discovery(const RegistryEndpoint& endpoint);

/// Search selected package releases using deterministic whitespace-token AND semantics.
///
/// Stable releases are selected by default. When include_prerelease is true,
/// the highest eligible SemVer, including prereleases, is selected per package.
std::vector<RegistryDiscoveryRecord> search_registry_discovery(
    const RegistryDiscoveryIndex& index,
    std::string_view query,
    bool include_prerelease = false);

/// Select one package's deterministic current release from the discovery view.
///
/// Returns nullopt when the package has no release eligible under the requested
/// stable/prerelease policy.
std::optional<RegistryDiscoveryRecord> select_registry_discovery_package(
    const RegistryDiscoveryIndex& index,
    const std::string& package_name,
    bool include_prerelease = false);

/// Return packages whose selected release directly depends on package_name.
std::vector<RegistryDiscoveryRecord> reverse_registry_dependencies(
    const RegistryDiscoveryIndex& index,
    const std::string& package_name,
    bool include_prerelease = false);

/// Render deterministic human-readable search results.
std::string render_registry_search(const RegistryDiscoveryIndex& index,
                                   std::string_view query,
                                   bool include_prerelease = false);

/// Render search results using the emojineer.registry-search.v1 JSON schema.
std::string render_registry_search_json(const RegistryDiscoveryIndex& index,
                                        std::string_view query,
                                        bool include_prerelease = false);

/// Render one package's selected release and discoverable version history.
std::string render_registry_package_info(const RegistryDiscoveryIndex& index,
                                         const std::string& package_name,
                                         bool include_prerelease = false);

/// Render package information using emojineer.registry-package-info.v1 JSON.
std::string render_registry_package_info_json(const RegistryDiscoveryIndex& index,
                                              const std::string& package_name,
                                              bool include_prerelease = false);

/// Render deterministic direct reverse dependencies for one package.
std::string render_registry_dependents(const RegistryDiscoveryIndex& index,
                                       const std::string& package_name,
                                       bool include_prerelease = false);

/// Render reverse dependencies using emojineer.registry-dependents.v1 JSON.
std::string render_registry_dependents_json(const RegistryDiscoveryIndex& index,
                                            const std::string& package_name,
                                            bool include_prerelease = false);

} // namespace emojineer
