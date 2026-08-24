#include "emojineer/package.hpp"

#include "emojineer/hash.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace emojineer {
namespace {

enum class VisitState { Visiting, Done };

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open '" + path.string() + "'");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool within(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto r = root.begin();
    auto p = path.begin();
    for (; r != root.end(); ++r, ++p) {
        if (p == path.end() || *r != *p) return false;
    }
    return true;
}

void append_u64(std::string& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xffu));
    }
}

void append_field(std::string& out, std::string_view value) {
    append_u64(out, static_cast<std::uint64_t>(value.size()));
    out.append(value.data(), value.size());
}

bool under_dependency(const std::filesystem::path& path,
                      const std::vector<std::filesystem::path>& dependency_roots) {
    for (const auto& dependency : dependency_roots) {
        if (within(dependency, path)) return true;
    }
    return false;
}

std::string package_hash(const std::filesystem::path& root,
                         const ProjectManifest& manifest,
                         const std::vector<std::filesystem::path>& dependency_roots) {
    std::vector<std::pair<std::string, std::string>> sources;
    std::filesystem::recursive_directory_iterator it(root), end;
    for (; it != end; ++it) {
        const auto path = it->path();
        if (it->is_directory()) {
            std::error_code ec;
            const auto canonical = std::filesystem::canonical(path, ec);
            if (!ec && path != root && under_dependency(canonical, dependency_roots)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file() || path.extension() != ".emoji") continue;
        const auto canonical = std::filesystem::canonical(path);
        if (!within(root, canonical)) {
            throw std::runtime_error("package '" + manifest.name +
                                     "' contains a source symlink that escapes its root");
        }
        if (under_dependency(canonical, dependency_roots)) continue;
        sources.emplace_back(std::filesystem::relative(canonical, root).generic_string(),
                             read_text(canonical));
    }
    std::sort(sources.begin(), sources.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });

    std::string framed;
    append_field(framed, "EMOJINEER-PACKAGE-v1");
    append_field(framed, canonical_manifest_text(manifest));
    for (const auto& [path, source] : sources) {
        append_field(framed, path);
        append_field(framed, source);
    }
    return sha256_hex(framed);
}

class Resolver {
public:
    PackageGraph resolve(const std::filesystem::path& raw_root,
                         const ProjectManifest& root_manifest) {
        const auto root = std::filesystem::canonical(raw_root);
        std::vector<std::string> stack;
        visit(root, root_manifest, stack);
        std::sort(packages_.begin(), packages_.end(),
                  [](const ResolvedPackage& left, const ResolvedPackage& right) {
                      return left.name < right.name;
                  });
        return {root_manifest.name, std::move(packages_)};
    }

private:
    [[noreturn]] void cycle(const std::string& name,
                            const std::vector<std::string>& stack) const {
        std::ostringstream out;
        out << "cyclic package dependency: ";
        auto first = std::find(stack.begin(), stack.end(), name);
        if (first == stack.end()) first = stack.begin();
        bool separator = false;
        for (auto current = first; current != stack.end(); ++current) {
            if (separator) out << " -> ";
            separator = true;
            out << *current;
        }
        if (separator) out << " -> ";
        out << name;
        throw std::runtime_error(out.str());
    }

    std::filesystem::path dependency_root(const std::filesystem::path& owner_root,
                                          const ProjectDependency& dependency,
                                          const std::string& owner_name) const {
        const auto candidate = owner_root / dependency.path;
        if (!std::filesystem::exists(candidate)) {
            throw std::runtime_error("package '" + owner_name + "' dependency '" +
                                     dependency.name + "' path does not exist: " +
                                     dependency.path.generic_string());
        }
        if (!std::filesystem::is_directory(candidate)) {
            throw std::runtime_error("package '" + owner_name + "' dependency '" +
                                     dependency.name + "' path is not a directory");
        }
        const auto canonical = std::filesystem::canonical(candidate);
        if (!std::filesystem::is_regular_file(canonical / "emojineer.toml")) {
            throw std::runtime_error("package '" + owner_name + "' dependency '" +
                                     dependency.name + "' has no emojineer.toml");
        }
        return canonical;
    }

    void visit(const std::filesystem::path& root,
               const ProjectManifest& manifest,
               std::vector<std::string>& stack) {
        const std::string root_key = root.generic_string();
        if (auto state = states_.find(root_key); state != states_.end()) {
            if (state->second == VisitState::Done) return;
            cycle(manifest.name, stack);
        }

        if (auto existing = name_roots_.find(manifest.name); existing != name_roots_.end() &&
            existing->second != root_key) {
            throw std::runtime_error("package name '" + manifest.name +
                                     "' resolves to multiple local roots");
        }
        name_roots_[manifest.name] = root_key;
        states_[root_key] = VisitState::Visiting;
        stack.push_back(manifest.name);

        std::vector<std::string> dependency_names;
        dependency_names.reserve(manifest.dependencies.size());

        for (const auto& dependency : manifest.dependencies) {
            const auto dep_root = dependency_root(root, dependency, manifest.name);
            const auto dep_manifest = load_project_manifest(dep_root / "emojineer.toml");
            if (dep_manifest.name != dependency.name) {
                throw std::runtime_error("package '" + manifest.name + "' dependency key '" +
                                         dependency.name + "' points to package '" +
                                         dep_manifest.name + "'");
            }
            dependency_names.push_back(dependency.name);
            visit(dep_root, dep_manifest, stack);
        }
        std::sort(dependency_names.begin(), dependency_names.end());

        // All descendants have been visited at this point. Exclude every resolved package
        // root nested beneath this package, not merely its direct dependency directories.
        // This keeps content hashes ownership-correct even when a transitive path dependency
        // is a sibling of its parent but still physically inside an ancestor package root.
        std::vector<std::filesystem::path> descendant_roots;
        for (const auto& resolved : packages_) {
            if (resolved.root != root && within(root, resolved.root)) {
                descendant_roots.push_back(resolved.root);
            }
        }

        packages_.push_back({manifest.name,
                             manifest.version,
                             root,
                             manifest.entry,
                             dependency_names,
                             package_hash(root, manifest, descendant_roots)});
        stack.pop_back();
        states_[root_key] = VisitState::Done;
    }

    std::unordered_map<std::string, VisitState> states_;
    std::unordered_map<std::string, std::string> name_roots_;
    std::vector<ResolvedPackage> packages_;
};

} // namespace

const ResolvedPackage* PackageGraph::find(const std::string& name) const {
    auto it = std::lower_bound(packages.begin(), packages.end(), name,
                               [](const ResolvedPackage& package, const std::string& key) {
                                   return package.name < key;
                               });
    if (it == packages.end() || it->name != name) return nullptr;
    return &*it;
}

PackageGraph resolve_package_graph(const std::filesystem::path& root) {
    return resolve_package_graph(root, load_project_manifest(root / "emojineer.toml"));
}

PackageGraph resolve_package_graph(const std::filesystem::path& root,
                                   const ProjectManifest& root_manifest) {
    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        throw std::runtime_error("package root is not a directory: " + root.string());
    }
    Resolver resolver;
    return resolver.resolve(root, root_manifest);
}

} // namespace emojineer
