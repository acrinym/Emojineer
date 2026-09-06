#pragma once

#include "emojineer/registry_transport.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace emojineer {

struct RegistryDiscoveryRecord {
    std::string package_name;
    std::string version;
    std::string content_sha256;
    std::string artifact_sha256;
    std::string entry;
    std::vector<std::string> dependencies;
};

struct RegistryDiscoveryIndex {
    std::string registry_id;
    std::vector<RegistryDiscoveryRecord> records;
};

RegistryDiscoveryIndex parse_registry_discovery_index(std::string_view text);
std::string render_registry_discovery_index(const RegistryDiscoveryIndex& index);
RegistryDiscoveryIndex load_registry_discovery(const RegistryEndpoint& endpoint);

std::vector<RegistryDiscoveryRecord> search_registry_discovery(
    const RegistryDiscoveryIndex& index,
    std::string_view query,
    bool include_prerelease = false);

std::optional<RegistryDiscoveryRecord> select_registry_discovery_package(
    const RegistryDiscoveryIndex& index,
    const std::string& package_name,
    bool include_prerelease = false);

std::vector<RegistryDiscoveryRecord> reverse_registry_dependencies(
    const RegistryDiscoveryIndex& index,
    const std::string& package_name,
    bool include_prerelease = false);

std::string render_registry_search(const RegistryDiscoveryIndex& index,
                                   std::string_view query,
                                   bool include_prerelease = false);
std::string render_registry_search_json(const RegistryDiscoveryIndex& index,
                                        std::string_view query,
                                        bool include_prerelease = false);
std::string render_registry_package_info(const RegistryDiscoveryIndex& index,
                                         const std::string& package_name,
                                         bool include_prerelease = false);
std::string render_registry_package_info_json(const RegistryDiscoveryIndex& index,
                                              const std::string& package_name,
                                              bool include_prerelease = false);
std::string render_registry_dependents(const RegistryDiscoveryIndex& index,
                                       const std::string& package_name,
                                       bool include_prerelease = false);
std::string render_registry_dependents_json(const RegistryDiscoveryIndex& index,
                                            const std::string& package_name,
                                            bool include_prerelease = false);

} // namespace emojineer
