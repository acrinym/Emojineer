#pragma once

#include "emojineer/project.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace emojineer {

struct ResolvedPackage {
    std::string name;
    std::string version;
    std::filesystem::path root;
    std::filesystem::path entry;
    std::vector<std::string> dependencies;
    std::string content_sha256;
};

struct PackageGraph {
    std::string root_name;
    std::vector<ResolvedPackage> packages;

    const ResolvedPackage* find(const std::string& name) const;
};

PackageGraph resolve_package_graph(const std::filesystem::path& root);
PackageGraph resolve_package_graph(const std::filesystem::path& root,
                                   const ProjectManifest& root_manifest);

} // namespace emojineer
