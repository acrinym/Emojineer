#pragma once

#include "emojineer/project.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace emojineer {

struct ResolvedPackage {
    std::string name;
    std::string version;
    DependencyKind source_kind;  // path or registry
    std::filesystem::path root;
    std::filesystem::path entry;
    std::vector<std::string> dependencies;
    std::string content_sha256;
    // Registry-specific fields (only for registry packages)
    std::optional<std::string> registry_alias;
    std::optional<std::string> registry_id;
    std::optional<std::string> registry_endpoint;
    std::optional<std::string> requirement;
    std::optional<std::string> artifact_sha256;
    std::optional<std::string> store_path;
};

struct PackageGraph {
    std::string root_name;
    std::vector<ResolvedPackage> packages;

    const ResolvedPackage* find(const std::string& name) const;
};

PackageGraph resolve_package_graph(const std::filesystem::path& root);
PackageGraph resolve_package_graph(const std::filesystem::path& root,
                                   const ProjectManifest& root_manifest);
PackageGraph resolve_package_graph(const std::filesystem::path& root,
                                   const ProjectManifest& root_manifest,
                                   const std::filesystem::path& store_root,
                                   bool offline = false);

// Compute the canonical content hash for a registry package.
// This uses the production authority (EMOJINEER-PACKAGE-v1 framing) to compute
// the hash that will be used in lock files for content integrity verification.
std::string compute_registry_package_hash(const std::filesystem::path& package_root,
                                          const ProjectManifest& manifest);

} // namespace emojineer
