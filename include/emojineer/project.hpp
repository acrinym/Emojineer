#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "emojineer/registry_transport.hpp"

namespace emojineer {

struct ProjectRegistry {
    std::string alias;
    std::string endpoint;
};

enum class DependencyKind {
    Path,
    Registry,
};

struct ProjectDependency {
    std::string name;
    DependencyKind kind;
    std::filesystem::path path;  // Only for Path dependencies
    std::string registry_alias;  // Only for Registry dependencies
    std::string requirement;      // Only for Registry dependencies (SemVer)
};

struct ProjectManifest {
    std::string name;
    std::string version;
    std::filesystem::path entry;
    std::vector<ProjectRegistry> registries;
    std::vector<ProjectDependency> dependencies;
};

struct ProjectDiagnostic {
    std::string message;
};

// Lock format 3 structures
struct LockRegistry {
    std::string alias;
    std::string id;
    std::string endpoint;
};

enum class LockSourceKind {
    Path,
    Registry,
};

struct LockDependency {
    LockSourceKind source;
    std::string name;
    std::string version;
    // Path-specific fields
    std::optional<std::filesystem::path> path;
    std::optional<std::string> content_sha256;
    // Registry-specific fields
    std::optional<std::string> registry_alias;
    std::optional<std::string> registry_id;
    std::optional<std::string> registry_endpoint;
    std::optional<std::string> requirement;
    std::optional<std::string> artifact_sha256;
    std::optional<std::string> store_path;
    std::vector<std::string> dependencies;
};

struct ProjectLock {
    std::string version;  // Lock format version
    std::string manifest_hash;  // Hash of the manifest for drift detection
    std::vector<LockRegistry> registries;
    std::vector<LockDependency> dependencies;
};

struct PackageStore {
    std::filesystem::path root;
};

enum class SyncMode {
    Online,
    Offline,
};

ProjectManifest load_project_manifest(const std::filesystem::path& manifest_path);
void initialize_project(const std::filesystem::path& root, const std::string& name);
std::vector<ProjectDiagnostic> check_project(const std::filesystem::path& root);
void validate_manifest(const ProjectManifest& manifest);
std::string canonical_manifest_text(const ProjectManifest& manifest);
std::string project_manifest_hash(const ProjectManifest& manifest);
std::string canonical_project_lock(const std::filesystem::path& root,
                                   const ProjectManifest& manifest);
void write_project_lock(const std::filesystem::path& root, const ProjectManifest& manifest);
void add_project_dependency(const std::filesystem::path& root,
                            const std::string& name,
                            const std::filesystem::path& path);
void add_project_registry_dependency(const std::filesystem::path& root,
                                     const std::string& name,
                                     const std::string& requirement,
                                     const std::string& registry_endpoint,
                                     const std::string& registry_alias);
void remove_project_dependency(const std::filesystem::path& root,
                               const std::string& name);
void sync_project(const std::filesystem::path& root, bool offline = false);
void sync_project(const std::filesystem::path& root,
                 const std::filesystem::path& cache_root,
                 bool offline = false);
std::string get_registry_alias_for_dependency(const ProjectManifest& manifest,
                                              const std::string& dependency_name);
std::string get_registry_endpoint_for_alias(const ProjectManifest& manifest,
                                             const std::string& alias);

// Lock format 3 functions
ProjectLock load_project_lock(const std::filesystem::path& lock_path);
std::string canonical_lock_text(const ProjectLock& lock);
bool is_lock_stale(const std::filesystem::path& root, const ProjectManifest& manifest,
                   const ProjectLock& lock);
std::filesystem::path package_store_root(const std::filesystem::path& root);
void materialize_package(const std::filesystem::path& store_root,
                        const std::string& registry_key,
                        const std::string& package_name,
                        const std::string& version,
                        const std::string& artifact_sha256,
                        const std::vector<std::pair<std::string, std::string>>& files,
                        const std::string& manifest_content);
bool is_materialized_package_valid(const std::filesystem::path& package_path,
                                  const std::string& expected_sha256);
void verify_or_repair_materialization(const std::filesystem::path& root,
                                     const ProjectLock& lock,
                                     const std::filesystem::path& cache_root);

// Registry resolution
struct ResolvedRegistryDependency {
    std::string name;
    std::string version;
    std::string registry_alias;
    std::string registry_id;
    std::string registry_endpoint;
    std::string requirement;
    std::string content_sha256;
    std::string artifact_sha256;
    std::filesystem::path store_path;
    std::vector<ProjectDependency> dependencies;
};

std::vector<ResolvedRegistryDependency> resolve_registry_dependencies(
    const ProjectManifest& manifest,
    const std::filesystem::path& store_root,
    bool offline);

// Convert manifest to lock format 3
ProjectLock manifest_to_lock(const std::filesystem::path& root,
                            const ProjectManifest& manifest);

ResolvedRegistryDependency resolve_single_registry_dependency(
    const RegistryEndpoint& endpoint,
    const std::string& name,
    std::string_view requirement);

// Version conflict detection
struct VersionConflict {
    std::string package_name;
    std::vector<std::string> requirements;
    std::vector<std::string> available_versions;
};

std::optional<VersionConflict> detect_version_conflict(
    const std::string& package_name,
    const std::vector<std::string>& requirements,
    const std::vector<std::string>& available_versions);

std::string select_deterministic_version(
    const std::string& package_name,
    const std::vector<std::string>& requirements,
    const std::vector<std::string>& available_versions);

// Transactional add
void add_project_registry_dependency_transactional(const std::filesystem::path& root,
                                                  const std::string& name,
                                                  const std::string& requirement,
                                                  const std::string& registry_endpoint,
                                                  const std::string& registry_alias);

// Utility for tests
std::string read_text_standalone(const std::filesystem::path& path);

} // namespace emojineer
