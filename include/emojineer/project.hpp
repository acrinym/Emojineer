#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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

ProjectManifest load_project_manifest(const std::filesystem::path& manifest_path);
void initialize_project(const std::filesystem::path& root, const std::string& name);
std::vector<ProjectDiagnostic> check_project(const std::filesystem::path& root);
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

} // namespace emojineer
