#include "emojineer/package.hpp"

#include "emojineer/hash.hpp"
#include "emojineer/project.hpp"

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

// Compute hash for a registry package (materialized package without dependency roots)
bool valid_sha256_hex(const std::optional<std::string>& value) {
    if (!value || value->size() != 64) return false;
    return std::all_of(value->begin(), value->end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

const LockRegistry* find_lock_registry(const ProjectLock& lock,
                                       const std::string& alias,
                                       const std::string& id,
                                       const std::string& endpoint) {
    const LockRegistry* found = nullptr;
    for (const auto& registry : lock.registries) {
        if (registry.alias != alias || registry.id != id || registry.endpoint != endpoint) continue;
        if (found) {
            throw std::runtime_error("offline lock contains duplicate registry authority for alias '" + alias + "'");
        }
        found = &registry;
    }
    return found;
}

void validate_offline_registry_lock(const ProjectManifest& root_manifest,
                                    const ProjectLock& lock) {
    if (lock.version != "3") {
        throw std::runtime_error("offline package resolution requires lock_version 3");
    }

    std::unordered_map<std::string, const LockDependency*> dependencies;
    for (const auto& locked : lock.dependencies) {
        if (locked.name.empty() || locked.version.empty()) {
            throw std::runtime_error("offline lock contains dependency with missing name or version");
        }
        if (!dependencies.emplace(locked.name, &locked).second) {
            throw std::runtime_error("offline lock contains duplicate dependency '" + locked.name + "'");
        }
        if (locked.source != LockSourceKind::Registry) continue;
        if (!locked.registry_alias || locked.registry_alias->empty() ||
            !locked.registry_id || locked.registry_id->empty() ||
            !locked.registry_endpoint || locked.registry_endpoint->empty() ||
            !locked.requirement || locked.requirement->empty() ||
            !locked.store_path || locked.store_path->empty()) {
            throw std::runtime_error("offline registry dependency '" + locked.name +
                                     "' is missing required provenance metadata");
        }
        if (!valid_sha256_hex(locked.artifact_sha256)) {
            throw std::runtime_error("offline registry dependency '" + locked.name +
                                     "' requires valid artifact_sha256");
        }
        if (!valid_sha256_hex(locked.content_sha256)) {
            throw std::runtime_error("offline registry dependency '" + locked.name +
                                     "' requires valid content_sha256");
        }
        const auto* registry = find_lock_registry(lock, *locked.registry_alias,
                                                  *locked.registry_id,
                                                  *locked.registry_endpoint);
        if (!registry) {
            throw std::runtime_error("offline registry dependency '" + locked.name +
                                     "' references undeclared registry '" + *locked.registry_alias + "'");
        }
        if (registry->id.empty() || registry->endpoint.empty()) {
            throw std::runtime_error("offline registry dependency '" + locked.name +
                                     "' registry authority record is incomplete");
        }
    }

    for (const auto& dependency : root_manifest.dependencies) {
        if (dependency.kind != DependencyKind::Registry) continue;
        const auto found = dependencies.find(dependency.name);
        if (found == dependencies.end() || found->second->source != LockSourceKind::Registry) {
            throw std::runtime_error("offline root registry dependency '" + dependency.name +
                                     "' has no registry lock entry");
        }
        const auto& locked = *found->second;
        if (!locked.registry_alias || *locked.registry_alias != dependency.registry_alias ||
            !locked.requirement || *locked.requirement != dependency.requirement) {
            throw std::runtime_error("offline root registry dependency '" + dependency.name +
                                     "' lock provenance does not match manifest declaration");
        }
    }
}

std::string registry_package_hash(const std::filesystem::path& root,
                                 const ProjectManifest& manifest) {
    std::vector<std::pair<std::string, std::string>> sources;
    std::filesystem::recursive_directory_iterator it(root), end;
    for (; it != end; ++it) {
        const auto path = it->path();
        if (it->is_directory()) {
            continue;
        }
        if (!it->is_regular_file() || path.extension() != ".emoji") continue;
        const auto canonical = std::filesystem::canonical(path);
        if (!within(root, canonical)) {
            throw std::runtime_error("package '" + manifest.name +
                                     "' contains a source symlink that escapes its root");
        }
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
    Resolver(const ProjectLock* lock, const std::filesystem::path& store_root, bool offline)
        : lock_(lock), store_root_(store_root), offline_(offline) {}

    PackageGraph resolve(const std::filesystem::path& raw_root,
                         const ProjectManifest& root_manifest) {
        const auto root = std::filesystem::canonical(raw_root);
        std::vector<std::string> stack;
        // Root package is always a path package
        visit(root, root_manifest, stack, DependencyKind::Path);

        // Hashing is deliberately a second phase. DFS discovery order cannot define source
        // ownership: a package may be physically nested under another resolved package even
        // when the container does not declare it as a dependency. Every hash therefore sees
        // the complete discovered package-root set.
        for (auto& package : packages_) {
            // Only compute hash for path packages (not registry packages)
            if (package.source_kind == DependencyKind::Path) {
                std::vector<std::filesystem::path> nested_package_roots;
                for (const auto& candidate : packages_) {
                    if (candidate.root != package.root && within(package.root, candidate.root)) {
                        nested_package_roots.push_back(candidate.root);
                    }
                }
                const auto manifest = manifests_.find(package.root.generic_string());
                if (manifest == manifests_.end()) {
                    throw std::runtime_error("internal error: missing resolved package manifest");
                }
                package.content_sha256 = package_hash(package.root,
                                                      manifest->second,
                                                      nested_package_roots);
            }
        }

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

    const LockDependency* find_lock_dependency(const std::string& name) const {
        if (!lock_) return nullptr;
        for (const auto& dep : lock_->dependencies) {
            if (dep.name == name) return &dep;
        }
        return nullptr;
    }

    void visit(const std::filesystem::path& root,
               const ProjectManifest& manifest,
               std::vector<std::string>& stack,
               DependencyKind source_kind) {
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
        manifests_[root_key] = manifest;
        states_[root_key] = VisitState::Visiting;
        stack.push_back(manifest.name);

        std::vector<std::string> dependency_names;
        dependency_names.reserve(manifest.dependencies.size());

        // Registry packages cannot have path dependencies - they must only use registry deps
        if (source_kind == DependencyKind::Registry) {
            for (const auto& dependency : manifest.dependencies) {
                if (dependency.kind == DependencyKind::Path) {
                    throw std::runtime_error("registry package '" + manifest.name +
                                             "' cannot have path dependency '" + dependency.name +
                                             "'; registry packages must use only registry dependencies");
                }
            }
        }

        for (const auto& dependency : manifest.dependencies) {
            // Handle path dependencies (existing behavior)
            if (dependency.kind == DependencyKind::Path) {
                const auto dep_root = dependency_root(root, dependency, manifest.name);
                const auto dep_manifest = load_project_manifest(dep_root / "emojineer.toml");
                if (dep_manifest.name != dependency.name) {
                    throw std::runtime_error("package '" + manifest.name + "' dependency key '" +
                                             dependency.name + "' points to package '" +
                                             dep_manifest.name + "'");
                }
                dependency_names.push_back(dependency.name);
                // Path dependencies are always path packages
                visit(dep_root, dep_manifest, stack, DependencyKind::Path);
            } else if (dependency.kind == DependencyKind::Registry) {
                // Registry dependencies - resolve from lock if available
                const auto* lock_dep = find_lock_dependency(dependency.name);
                
                if (!lock_dep || !lock_dep->store_path) {
                    // No lock entry or store path - registry dependency not available
                    // In offline mode, this is an error - we cannot proceed without the lock entry
                    if (offline_) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' has no lock entry or store_path");
                    }
                    // In online mode without a lock, record the dependency name but don't add
                    // it as a resolved package. This allows the package graph to indicate the 
                    // dependency exists while the actual resolution will fail at module loading 
                    // time if the package is needed.
                    dependency_names.push_back(dependency.name);
                    continue;
                }
                
                // store_path is std::optional<std::string>, convert to path
                std::filesystem::path store_path(*lock_dep->store_path);
                
                // Validate store_path exists and is a directory
                if (!std::filesystem::exists(store_path)) {
                    if (offline_) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' store_path does not exist: " + store_path.string());
                    }
                    dependency_names.push_back(dependency.name);
                    continue;
                }
                
                if (!std::filesystem::is_directory(store_path)) {
                    if (offline_) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' store_path is not a directory: " + store_path.string());
                    }
                    dependency_names.push_back(dependency.name);
                    continue;
                }
                
                // Confine store_path beneath the effective project package store
                if (!store_root_.empty()) {
                    std::filesystem::path canonical_store_root;
                    std::filesystem::path canonical_store_path;
                    try {
                        canonical_store_root = std::filesystem::canonical(store_root_);
                        canonical_store_path = std::filesystem::canonical(store_path);
                    } catch (const std::filesystem::filesystem_error& e) {
                        // filesystem errors (path not found, permission denied, etc.) are critical in offline mode
                        if (offline_) {
                            throw std::runtime_error("offline mode: cannot canonicalize store paths for registry dependency '" +
                                                     dependency.name + "': " + e.what());
                        }
                        dependency_names.push_back(dependency.name);
                        continue;
                    } catch (const std::exception& e) {
                        // Other std:: exceptions are also critical in offline mode
                        if (offline_) {
                            throw std::runtime_error("offline mode: cannot canonicalize store paths for registry dependency '" +
                                                     dependency.name + "': " + e.what());
                        }
                        dependency_names.push_back(dependency.name);
                        continue;
                    }
                    
                    // Check if store_path is within store_root
                    if (!within(canonical_store_root, canonical_store_path)) {
                        if (offline_) {
                            throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                     "' store_path escapes project package store: " +
                                                     store_path.string());
                        }
                        dependency_names.push_back(dependency.name);
                        continue;
                    }
                }
                
                // Load and validate the materialized package manifest
                auto dep_manifest_path = store_path / "emojineer.toml";
                if (!std::filesystem::exists(dep_manifest_path)) {
                    if (offline_) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' has no emojineer.toml at: " + dep_manifest_path.string());
                    }
                    dependency_names.push_back(dependency.name);
                    continue;
                }
                
                ProjectManifest dep_manifest;
                try {
                    dep_manifest = load_project_manifest(dep_manifest_path);
                } catch (const std::exception& e) {
                    if (offline_) {
                        throw std::runtime_error("offline mode: failed to load manifest for registry dependency '" +
                                                 dependency.name + "': " + e.what());
                    }
                    dependency_names.push_back(dependency.name);
                    continue;
                }
                
                // Verify manifest name matches the dependency name
                if (dep_manifest.name != dependency.name) {
                    if (offline_) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' manifest name mismatch: expected '" + dependency.name +
                                                 "' but got '" + dep_manifest.name + "'");
                    }
                    dependency_names.push_back(dependency.name);
                    continue;
                }
                
                // Verify manifest version matches locked version
                if (dep_manifest.version != lock_dep->version) {
                    if (offline_) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' version mismatch: locked " + lock_dep->version +
                                                 " but manifest has " + dep_manifest.version);
                    }
                    dependency_names.push_back(dependency.name);
                    continue;
                }

                if (offline_) {
                    if (!lock_dep->registry_alias || *lock_dep->registry_alias != dependency.registry_alias ||
                        !lock_dep->requirement || *lock_dep->requirement != dependency.requirement) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' lock coordinate does not match owner manifest");
                    }
                    std::vector<std::string> manifest_edges;
                    manifest_edges.reserve(dep_manifest.dependencies.size());
                    for (const auto& nested : dep_manifest.dependencies) manifest_edges.push_back(nested.name);
                    std::sort(manifest_edges.begin(), manifest_edges.end());
                    auto lock_edges = lock_dep->dependencies;
                    std::sort(lock_edges.begin(), lock_edges.end());
                    if (manifest_edges != lock_edges) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' lock dependency edges do not match materialized manifest");
                    }
                }
                
                // Registry package is valid
                dependency_names.push_back(dependency.name);
                
                // Recursively visit the registry package to populate its dependencies
                // The visit function will register it with the correct source_kind (Registry)
                // Pass the lock dependency info via a map so the recursive visit can find it
                visit(store_path, dep_manifest, stack, DependencyKind::Registry);
            }
        }
        std::sort(dependency_names.begin(), dependency_names.end());

        // Check if this package was already registered (by name)
        // If so, update its metadata with what we learned during traversal
        bool already_registered = false;
        for (auto& pkg : packages_) {
            if (pkg.name == manifest.name) {
                // Update the existing package with correct metadata
                pkg.dependencies = dependency_names;
                
                // For registry packages, verify content integrity
                if (source_kind == DependencyKind::Registry) {
                    const auto* lock_dep = find_lock_dependency(manifest.name);
                    if (lock_dep) {
                        // Verify content SHA256 matches the materialized package
                        if (lock_dep->content_sha256) {
                            std::string computed_hash = registry_package_hash(root, manifest);
                            if (computed_hash != *lock_dep->content_sha256) {
                                throw std::runtime_error("registry package '" + manifest.name +
                                                         "' content SHA256 mismatch: expected " +
                                                         *lock_dep->content_sha256 + " but got " +
                                                         computed_hash);
                            }
                        }
                        pkg.root = root;
                        pkg.entry = manifest.entry;
                        pkg.content_sha256 = lock_dep->content_sha256.value_or("");
                        pkg.registry_alias = lock_dep->registry_alias;
                        pkg.registry_id = lock_dep->registry_id;
                        pkg.registry_endpoint = lock_dep->registry_endpoint;
                        pkg.requirement = lock_dep->requirement;
                        pkg.artifact_sha256 = lock_dep->artifact_sha256;
                        pkg.store_path = lock_dep->store_path;
                    }
                }
                already_registered = true;
                break;
            }
        }
        
        if (!already_registered) {
            // For registry packages, get metadata from lock
            ResolvedPackage new_pkg;
            new_pkg.name = manifest.name;
            new_pkg.version = manifest.version;
            new_pkg.source_kind = source_kind;
            new_pkg.root = root;
            new_pkg.entry = manifest.entry;
            new_pkg.dependencies = dependency_names;
            
            if (source_kind == DependencyKind::Registry) {
                const auto* lock_dep = find_lock_dependency(manifest.name);
                if (lock_dep) {
                    // Verify content SHA256 matches the materialized package
                    if (lock_dep->content_sha256) {
                        std::string computed_hash = registry_package_hash(root, manifest);
                        if (computed_hash != *lock_dep->content_sha256) {
                            throw std::runtime_error("registry package '" + manifest.name +
                                                     "' content SHA256 mismatch: expected " +
                                                     *lock_dep->content_sha256 + " but got " +
                                                     computed_hash);
                        }
                    }
                    new_pkg.content_sha256 = lock_dep->content_sha256.value_or("");
                    new_pkg.registry_alias = lock_dep->registry_alias;
                    new_pkg.registry_id = lock_dep->registry_id;
                    new_pkg.registry_endpoint = lock_dep->registry_endpoint;
                    new_pkg.requirement = lock_dep->requirement;
                    new_pkg.artifact_sha256 = lock_dep->artifact_sha256;
                    new_pkg.store_path = lock_dep->store_path;
                }
            }
            
            packages_.push_back(std::move(new_pkg));
        }
        stack.pop_back();
        states_[root_key] = VisitState::Done;
    }

    const ProjectLock* lock_;
    std::filesystem::path store_root_;
    bool offline_;
    std::unordered_map<std::string, VisitState> states_;
    std::unordered_map<std::string, std::string> name_roots_;
    std::unordered_map<std::string, ProjectManifest> manifests_;
    std::vector<ResolvedPackage> packages_;
};

} // namespace

std::string compute_registry_package_hash(const std::filesystem::path& package_root,
                                          const ProjectManifest& manifest) {
    return registry_package_hash(package_root, manifest);
}

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
    return resolve_package_graph(root, root_manifest, {}, false);
}

PackageGraph resolve_package_graph(const std::filesystem::path& root,
                                   const ProjectManifest& root_manifest,
                                   const std::filesystem::path& store_root,
                                   bool offline) {
    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        throw std::runtime_error("package root is not a directory: " + root.string());
    }
    
    // Load lock file for offline registry resolution
    const ProjectLock* lock = nullptr;
    ProjectLock lock_storage;
    std::filesystem::path effective_store_root = store_root;
    
    if (offline || !store_root.empty()) {
        const auto lock_path = root / "emojineer.lock";
        const bool root_has_registry_dependency = std::any_of(
            root_manifest.dependencies.begin(), root_manifest.dependencies.end(),
            [](const ProjectDependency& dependency) {
                return dependency.kind == DependencyKind::Registry;
            });

        if (!std::filesystem::exists(lock_path)) {
            if (offline && root_has_registry_dependency) {
                throw std::runtime_error(
                    "offline package resolution requires emojineer.lock for registry dependencies");
            }
        } else {
            try {
                lock_storage = load_project_lock(lock_path);
            } catch (const std::exception& error) {
                throw std::runtime_error(
                    std::string("cannot load emojineer.lock for package resolution: ") + error.what());
            }
            if (is_lock_stale(root, root_manifest, lock_storage)) {
                throw std::runtime_error("emojineer.lock is stale; run 'emji sync'");
            }
            if (offline) {
                validate_offline_registry_lock(root_manifest, lock_storage);
            }
            lock = &lock_storage;
            if (effective_store_root.empty()) {
                effective_store_root = package_store_root(root);
            }
        }
    }
    
    Resolver resolver(lock, effective_store_root, offline);
    return resolver.resolve(root, root_manifest);
}

} // namespace emojineer
