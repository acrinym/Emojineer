#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace emojineer {

struct ProjectDependency {
    std::string name;
    std::filesystem::path path;
};

struct ProjectManifest {
    std::string name;
    std::string version;
    std::filesystem::path entry;
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
void remove_project_dependency(const std::filesystem::path& root,
                               const std::string& name);

} // namespace emojineer
