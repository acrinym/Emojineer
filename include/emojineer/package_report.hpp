#pragma once

#include "emojineer/package.hpp"

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
    std::string path;
    std::string entry;
    std::vector<std::string> dependencies;
    std::string content_sha256;
};

struct PackageGraphReport {
    std::string root_name;
    std::vector<PackageReportRow> packages;

    const PackageReportRow* find(const std::string& name) const;
};

std::string package_relation_name(PackageRelation relation);
PackageGraphReport build_package_graph_report(const PackageGraph& graph);
std::string render_package_tree(const PackageGraphReport& report, bool include_hashes = false);
std::string render_package_graph_json(const PackageGraphReport& report);

} // namespace emojineer
