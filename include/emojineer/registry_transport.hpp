#pragma once

#include "emojineer/package_artifact.hpp"

#include <filesystem>
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

RegistryEndpoint parse_registry_endpoint(std::string_view text);
std::filesystem::path default_registry_cache_root();

void initialize_file_registry(const std::filesystem::path& root,
                              const std::string& registry_id);
std::string registry_identity(const RegistryEndpoint& endpoint);
RegistryPackageIndex load_registry_package_index(const RegistryEndpoint& endpoint,
                                                 const std::string& package_name);
std::string render_registry_package_index(const RegistryPackageIndex& index);
std::string render_registry_versions(const RegistryPackageIndex& index);

RegistryPublishResult publish_package_to_registry(
    const std::filesystem::path& package_root,
    const RegistryEndpoint& endpoint);
RegistryFetchResult fetch_registry_package(
    const RegistryEndpoint& endpoint,
    const std::string& package_name,
    std::string_view requirement,
    const std::filesystem::path& cache_root = {});

bool https_registry_transport_available();

} // namespace emojineer
