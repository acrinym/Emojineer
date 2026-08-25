#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace emojineer {

struct PackageArtifactFile {
    std::string path;
    std::string sha256;
    std::string source;
};

struct PackageArtifact {
    std::string name;
    std::string version;
    std::string entry;
    std::string manifest;
    std::string content_sha256;
    std::string artifact_sha256;
    std::vector<PackageArtifactFile> files;
};

std::string build_package_artifact_bytes(const std::filesystem::path& package_root);
PackageArtifact parse_package_artifact(std::string_view bytes);
PackageArtifact load_package_artifact(const std::filesystem::path& artifact_path);
void write_package_artifact(const std::filesystem::path& package_root,
                            const std::filesystem::path& artifact_path);
std::string default_package_artifact_filename(const std::string& name,
                                              const std::string& version);
std::filesystem::path package_cache_path(const std::filesystem::path& cache_root,
                                         const PackageArtifact& artifact);
std::string render_package_artifact_summary(const PackageArtifact& artifact);

} // namespace emojineer
