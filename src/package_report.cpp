#include "emojineer/package_report.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace emojineer {
namespace {

std::string portable_relative_path(const std::filesystem::path& root,
                                   const std::filesystem::path& package_root,
                                   const std::string& package_name) {
    if (root == package_root) return ".";
    std::error_code error;
    const auto relative = std::filesystem::relative(package_root, root, error);
    if (error || relative.empty() || relative.is_absolute()) {
        throw std::runtime_error("cannot render checkout-portable path for package '" +
                                 package_name + "'");
    }
    const auto text = relative.generic_string();
    if (text.empty()) {
        throw std::runtime_error("cannot render checkout-portable path for package '" +
                                 package_name + "'");
    }
    return text;
}

std::string json_string(std::string_view text) {
    std::ostringstream out;
    out << '"';
    static constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : text) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) {
                out << "\\u00" << hex[(c >> 4) & 0x0f] << hex[c & 0x0f];
            } else {
                out << static_cast<char>(c);
            }
        }
    }
    out << '"';
    return out.str();
}

void append_tree_node(std::ostringstream& out,
                      const PackageGraphReport& report,
                      const PackageReportRow& row,
                      const std::string& prefix,
                      const std::string& connector,
                      bool include_hashes,
                      std::set<std::string>& expanded) {
    out << prefix << connector << row.name << '@' << row.version
        << " [" << package_relation_name(row.relation) << ']'
        << " [" << dependency_kind_name(row.source_kind) << ']';
    
    // Show source-specific info
    if (row.source_kind == DependencyKind::Registry) {
        if (row.registry_alias) {
            out << " registry=" << *row.registry_alias;
        }
    } else {
        out << " path=" << row.path;
    }
    out << " entry=" << row.entry;
    if (include_hashes) out << " sha256=" << row.content_sha256;

    const bool already_expanded = !expanded.insert(row.name).second;
    if (already_expanded) {
        out << " (shared)\n";
        return;
    }
    out << '\n';

    for (std::size_t i = 0; i < row.dependencies.size(); ++i) {
        const auto* child = report.find(row.dependencies[i]);
        if (!child) {
            throw std::runtime_error("package report references missing dependency '" +
                                     row.dependencies[i] + "'");
        }
        const bool last = i + 1 == row.dependencies.size();
        append_tree_node(out,
                         report,
                         *child,
                         prefix + (connector.empty() ? std::string{} :
                                   (connector == "`- " ? "   " : "|  ")),
                         last ? "`- " : "+- ",
                         include_hashes,
                         expanded);
    }
}

} // namespace

const PackageReportRow* PackageGraphReport::find(const std::string& name) const {
    auto it = std::lower_bound(packages.begin(), packages.end(), name,
                               [](const PackageReportRow& row, const std::string& key) {
                                   return row.name < key;
                               });
    if (it == packages.end() || it->name != name) return nullptr;
    return &*it;
}

std::string package_relation_name(PackageRelation relation) {
    switch (relation) {
    case PackageRelation::Root: return "root";
    case PackageRelation::Direct: return "direct";
    case PackageRelation::Transitive: return "transitive";
    }
    throw std::runtime_error("unknown package relation");
}

std::string dependency_kind_name(DependencyKind kind) {
    switch (kind) {
    case DependencyKind::Path: return "path";
    case DependencyKind::Registry: return "registry";
    }
    throw std::runtime_error("unknown dependency kind");
}

PackageGraphReport build_package_graph_report(const PackageGraph& graph) {
    const auto* root = graph.find(graph.root_name);
    if (!root) throw std::runtime_error("package graph is missing its root package");

    std::set<std::string> direct(root->dependencies.begin(), root->dependencies.end());
    PackageGraphReport report;
    report.root_name = graph.root_name;
    report.packages.reserve(graph.packages.size());

    for (const auto& package : graph.packages) {
        PackageRelation relation = PackageRelation::Transitive;
        if (package.name == graph.root_name) {
            relation = PackageRelation::Root;
        } else if (direct.contains(package.name)) {
            relation = PackageRelation::Direct;
        }

        PackageReportRow row;
        row.name = package.name;
        row.version = package.version;
        row.relation = relation;
        row.source_kind = package.source_kind;
        row.path = portable_relative_path(root->root, package.root, package.name);
        row.entry = package.entry.generic_string();
        row.dependencies = package.dependencies;
        row.content_sha256 = package.content_sha256;
        
        // Copy registry-specific fields if present
        if (package.registry_alias) {
            row.registry_alias = package.registry_alias;
        }
        if (package.registry_endpoint) {
            row.registry_endpoint = package.registry_endpoint;
        }
        
        report.packages.push_back(std::move(row));
    }

    std::sort(report.packages.begin(), report.packages.end(),
              [](const PackageReportRow& left, const PackageReportRow& right) {
                  return left.name < right.name;
              });
    return report;
}

std::string render_package_tree(const PackageGraphReport& report, bool include_hashes) {
    const auto* root = report.find(report.root_name);
    if (!root) throw std::runtime_error("package report is missing its root package");
    std::ostringstream out;
    std::set<std::string> expanded;
    append_tree_node(out, report, *root, {}, {}, include_hashes, expanded);
    return out.str();
}

std::string render_package_graph_json(const PackageGraphReport& report) {
    std::ostringstream out;
    out << "{\n"
        << "  \"schema\": \"emojineer.package-graph.v1\",\n"
        << "  \"root\": " << json_string(report.root_name) << ",\n"
        << "  \"packages\": [\n";

    for (std::size_t i = 0; i < report.packages.size(); ++i) {
        const auto& package = report.packages[i];
        out << "    {\n"
            << "      \"name\": " << json_string(package.name) << ",\n"
            << "      \"version\": " << json_string(package.version) << ",\n"
            << "      \"relation\": " << json_string(package_relation_name(package.relation)) << ",\n"
            << "      \"path\": " << json_string(package.path) << ",\n"
            << "      \"entry\": " << json_string(package.entry) << ",\n"
            << "      \"content_sha256\": " << json_string(package.content_sha256) << ",\n"
            << "      \"dependencies\": [";
        for (std::size_t dependency = 0; dependency < package.dependencies.size(); ++dependency) {
            if (dependency != 0) out << ", ";
            out << json_string(package.dependencies[dependency]);
        }
        out << "]\n"
            << "    }" << (i + 1 == report.packages.size() ? "\n" : ",\n");
    }

    out << "  ]\n}\n";
    return out.str();
}

} // namespace emojineer
