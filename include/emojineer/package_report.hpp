#pragma once

#include "emojineer/package.hpp"

#include <optional>
#include <string>
#include <vector>

namespace emojineer {

enum class PackageRelation {
    Root,
    Direct,
    Transitive,
};

struct PackageReportRow {
    std::string name;
    std::string version;
    PackageRelation relation = PackageRelation::Transitive;
    DependencyKind source_kind = DependencyKind::Path;  // path or registry
    std::string path;
    std::string entry;
    std::vector<std::string> dependencies;
    std::string content_sha256;
    // Registry-specific fields (only for registry packages)
    std::optional<std::string> registry_alias;
    std::optional<std::string> registry_endpoint;
    std::optional<std::string> selected_version;
};

struct PackageGraphReport {
    std::string root_name;
    std::vector<PackageReportRow> packages;

    const PackageReportRow* find(const std::string& name) const;
};

std::string package_relation_name(PackageRelation relation);
std::string dependency_kind_name(DependencyKind kind);
PackageGraphReport build_package_graph_report(const PackageGraph& graph);
std::string render_package_tree(const PackageGraphReport& report, bool include_hashes = false);
std::string render_package_graph_json(const PackageGraphReport& report);

} // namespace emojineer
