#include "emojineer/project.hpp"
#include "emojineer/module.hpp"
#include "emojineer/package.hpp"
#include "emojineer/package_artifact.hpp"
#include "emojineer/registry_transport.hpp"
#include "emojineer/registry.hpp"
#include "emojineer/hash.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace emojineer {

// Forward declarations
struct ProjectManifest;
struct ResolvedRegistryDependency;
std::vector<ResolvedRegistryDependency> resolve_registry_dependencies_impl(
    const ProjectManifest& manifest,
    const std::filesystem::path& store_root,
    const std::filesystem::path& project_root,
    bool offline,
    std::unordered_map<std::string, ResolvedRegistryDependency>& resolved,
    std::unordered_set<std::string>& resolving);

namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open '" + path.string() + "'");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write '" + path.string() + "'");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("failed while writing '" + path.string() + "'");
}

std::string trim(std::string value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

std::string parse_quoted(const std::string& value, std::size_t line) {
    const std::string text = trim(value);
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') {
        throw std::runtime_error("manifest line " + std::to_string(line) + ": expected quoted string");
    }
    const std::string inner = text.substr(1, text.size() - 2);
    if (inner.find('"') != std::string::npos || inner.find('\\') != std::string::npos ||
        inner.find('\n') != std::string::npos || inner.find('\r') != std::string::npos) {
        throw std::runtime_error("manifest line " + std::to_string(line) +
                                 ": quotes, backslashes, and newlines are not allowed in values");
    }
    return inner;
}

void validate_name(const std::string& name, const std::string& what = "project name") {
    if (name.empty()) throw std::runtime_error(what + " cannot be empty");
    for (unsigned char c : name) {
        if (!(std::isalnum(c) || c == '-' || c == '_')) {
            throw std::runtime_error(what + " may contain only ASCII letters, digits, '-' and '_'");
        }
    }
}

void validate_version(const std::string& version) {
    static const std::regex pattern(R"(^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][A-Za-z0-9.-]+)?$)");
    if (!std::regex_match(version, pattern)) {
        throw std::runtime_error("project version must be semantic-version shaped (for example 0.1.0)");
    }
}

void validate_entry(const std::filesystem::path& entry) {
    if (entry.empty() || entry.is_absolute()) {
        throw std::runtime_error("project entry must be a non-empty relative path");
    }
    for (const auto& component : entry) {
        if (component == "..") throw std::runtime_error("project entry may not escape the project root");
    }
    const std::string generic = entry.generic_string();
    if (generic.find('"') != std::string::npos || generic.find('\\') != std::string::npos) {
        throw std::runtime_error("project entry must use portable forward-slash path syntax");
    }
    if (entry.extension() != ".emoji") {
        throw std::runtime_error("project entry must point to a .emoji source file");
    }
}

void validate_registry_alias(const std::string& alias, const std::string& what = "registry alias") {
    if (alias.empty()) throw std::runtime_error(what + " cannot be empty");
    for (unsigned char c : alias) {
        if (!(std::isalnum(c) || c == '-' || c == '_')) {
            throw std::runtime_error(what + " may contain only ASCII letters, digits, '-' and '_'");
        }
    }
}

void validate_dependency_path(const std::filesystem::path& path, const std::string& name) {
    if (path.empty() || path.is_absolute()) {
        throw std::runtime_error("dependency '" + name + "' path must be non-empty and relative");
    }
    const std::string generic = path.generic_string();
    if (generic.find('"') != std::string::npos || generic.find('\\') != std::string::npos) {
        throw std::runtime_error("dependency '" + name + "' path must use portable forward-slash syntax");
    }
}

std::uint64_t fnv1a64(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::vector<ProjectDependency> sorted_dependencies(const ProjectManifest& manifest) {
    auto dependencies = manifest.dependencies;
    std::sort(dependencies.begin(), dependencies.end(),
              [](const ProjectDependency& left, const ProjectDependency& right) {
                  return left.name < right.name;
              });
    return dependencies;
}

void write_manifest(const std::filesystem::path& root, const ProjectManifest& manifest) {
    write_text(root / "emojineer.toml", canonical_manifest_text(manifest));
}

std::string joined_dependencies(const std::vector<std::string>& dependencies) {
    std::ostringstream out;
    for (std::size_t i = 0; i < dependencies.size(); ++i) {
        if (i != 0) out << ',';
        out << dependencies[i];
    }
    return out.str();
}

std::string relative_package_path(const std::filesystem::path& root,
                                  const std::filesystem::path& package_root) {
    std::error_code error;
    auto relative = std::filesystem::relative(package_root, root, error);
    if (error || relative.empty()) {
        throw std::runtime_error("cannot express dependency package path relative to project root");
    }
    return relative.generic_string();
}

} // namespace

// Public read_text for tests
std::string read_text_standalone(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open '" + path.string() + "'");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// Public validate_manifest function
void validate_manifest(const ProjectManifest& manifest) {
    validate_name(manifest.name);
    validate_version(manifest.version);
    validate_entry(manifest.entry);

    // Validate registries
    std::set<std::string> registry_aliases;
    for (const auto& registry : manifest.registries) {
        validate_registry_alias(registry.alias, "registry alias");
        if (registry.endpoint.empty()) {
            throw std::runtime_error("registry '" + registry.alias + "' endpoint cannot be empty");
        }
        if (!registry_aliases.insert(registry.alias).second) {
            throw std::runtime_error("manifest contains duplicate registry '" + registry.alias + "'");
        }
    }

    // Validate dependencies
    std::set<std::string> names;
    for (const auto& dependency : manifest.dependencies) {
        validate_name(dependency.name, "dependency name");
        
        if (dependency.kind == DependencyKind::Path) {
            validate_dependency_path(dependency.path, dependency.name);
        } else if (dependency.kind == DependencyKind::Registry) {
            if (dependency.registry_alias.empty()) {
                throw std::runtime_error("dependency '" + dependency.name + "' registry alias cannot be empty");
            }
            if (dependency.requirement.empty()) {
                throw std::runtime_error("dependency '" + dependency.name + "' requirement cannot be empty");
            }
            if (registry_aliases.find(dependency.registry_alias) == registry_aliases.end()) {
                throw std::runtime_error("dependency '" + dependency.name + "' references unknown registry '" +
                                         dependency.registry_alias + "'");
            }
        }
        
        if (dependency.name == manifest.name) {
            throw std::runtime_error("package may not declare itself as dependency '" + dependency.name + "'");
        }
        if (!names.insert(dependency.name).second) {
            throw std::runtime_error("manifest contains duplicate dependency '" + dependency.name + "'");
        }
    }
}

ProjectManifest load_project_manifest(const std::filesystem::path& manifest_path) {
    std::istringstream input(read_text(manifest_path));
    ProjectManifest manifest;
    enum class Section { None, Package, Registries, Dependencies };
    Section section = Section::None;
    bool have_name = false;
    bool have_version = false;
    bool have_entry = false;
    std::set<std::string> registry_aliases;
    std::set<std::string> dependency_names;
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;
        const std::string text = trim(line);
        if (text.empty() || text.front() == '#') continue;
        if (text.front() == '[') {
            if (text == "[package]") {
                section = Section::Package;
                continue;
            }
            if (text == "[registries]") {
                section = Section::Registries;
                continue;
            }
            if (text == "[dependencies]") {
                section = Section::Dependencies;
                continue;
            }
            throw std::runtime_error("manifest line " + std::to_string(line_number) +
                                     ": unsupported section '" + text + "'");
        }
        const auto equal = text.find('=');
        if (equal == std::string::npos) {
            throw std::runtime_error("manifest line " + std::to_string(line_number) + ": expected key = value");
        }
        const std::string key = trim(text.substr(0, equal));
        const std::string value = parse_quoted(text.substr(equal + 1), line_number);

        if (section == Section::Package) {
            if (key == "name") {
                if (have_name) throw std::runtime_error("manifest contains duplicate package.name");
                manifest.name = value;
                have_name = true;
            } else if (key == "version") {
                if (have_version) throw std::runtime_error("manifest contains duplicate package.version");
                manifest.version = value;
                have_version = true;
            } else if (key == "entry") {
                if (have_entry) throw std::runtime_error("manifest contains duplicate package.entry");
                manifest.entry = std::filesystem::path(value);
                have_entry = true;
            } else {
                throw std::runtime_error("manifest line " + std::to_string(line_number) +
                                         ": unknown package key '" + key + "'");
            }
            continue;
        }

        if (section == Section::Registries) {
            if (!registry_aliases.insert(key).second) {
                throw std::runtime_error("manifest contains duplicate registry '" + key + "'");
            }
            manifest.registries.push_back({key, value});
            continue;
        }

        if (section == Section::Dependencies) {
            if (!dependency_names.insert(key).second) {
                throw std::runtime_error("manifest contains duplicate dependency '" + key + "'");
            }
            // Check if this is a registry dependency (starts with "registry:")
            if (value.substr(0, 9) == "registry:") {
                // Parse registry:<alias>:<requirement>
                std::string registry_spec = value.substr(9);  // Remove "registry:" prefix
                const auto colon_pos = registry_spec.find(':');
                if (colon_pos == std::string::npos) {
                    throw std::runtime_error("manifest line " + std::to_string(line_number) +
                                             ": registry dependency must have format registry:<alias>:<requirement>");
                }
                std::string reg_alias = registry_spec.substr(0, colon_pos);
                std::string requirement = registry_spec.substr(colon_pos + 1);
                manifest.dependencies.push_back({key, DependencyKind::Registry, {}, reg_alias, requirement});
            } else {
                // Path dependency (backward compatible)
                manifest.dependencies.push_back({key, DependencyKind::Path, std::filesystem::path(value), {}, {}});
            }
            continue;
        }

        throw std::runtime_error("manifest line " + std::to_string(line_number) +
                                 ": keys must appear inside [package], [registries], or [dependencies]");
    }

    if (!have_name || !have_version || !have_entry) {
        throw std::runtime_error("manifest [package] requires name, version, and entry");
    }
    validate_manifest(manifest);
    return manifest;
}

std::vector<ProjectRegistry> sorted_registries(const ProjectManifest& manifest) {
    auto registries = manifest.registries;
    std::sort(registries.begin(), registries.end(),
              [](const ProjectRegistry& left, const ProjectRegistry& right) {
                  return left.alias < right.alias;
              });
    return registries;
}

std::string canonical_manifest_text(const ProjectManifest& manifest, bool include_registries) {
    validate_manifest(manifest);
    std::ostringstream out;
    out << "[package]\n"
        << "name = \"" << manifest.name << "\"\n"
        << "version = \"" << manifest.version << "\"\n"
        << "entry = \"" << manifest.entry.generic_string() << "\"\n";

    // Output registries section (only for project manifests, not for package artifacts)
    if (include_registries) {
        const auto registries = sorted_registries(manifest);
        if (!registries.empty()) {
            out << "\n[registries]\n";
            for (const auto& registry : registries) {
                out << registry.alias << " = \"" << registry.endpoint << "\"\n";
            }
        }
    }

    // Output dependencies section
    const auto dependencies = sorted_dependencies(manifest);
    if (!dependencies.empty()) {
        out << "\n[dependencies]\n";
        for (const auto& dependency : dependencies) {
            if (dependency.kind == DependencyKind::Registry) {
                out << dependency.name << " = \"registry:" << dependency.registry_alias << ":"
                    << dependency.requirement << "\"\n";
            } else {
                out << dependency.name << " = \"" << dependency.path.generic_string() << "\"\n";
            }
        }
    }
    return out.str();
}

std::string project_manifest_hash(const ProjectManifest& manifest) {
    const std::uint64_t hash = fnv1a64(canonical_manifest_text(manifest));
    std::ostringstream out;
    out << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

void initialize_project(const std::filesystem::path& root, const std::string& name) {
    ProjectManifest manifest{name, "0.1.0", std::filesystem::path("src/main.emoji"), {}, {}};
    validate_manifest(manifest);
    const auto manifest_path = root / "emojineer.toml";
    if (std::filesystem::exists(manifest_path)) {
        throw std::runtime_error("project already contains emojineer.toml");
    }
    std::filesystem::create_directories(root / "src");
    write_text(manifest_path, canonical_manifest_text(manifest));
    const auto entry_path = root / manifest.entry;
    if (!std::filesystem::exists(entry_path)) {
        write_text(entry_path, "📝 📜Hello from Emojineer 🚀📜\n");
    }
}

std::string canonical_project_lock(const std::filesystem::path& root,
                                   const ProjectManifest& manifest) {
    validate_manifest(manifest);
    
    // Reuse a current lock/materialized store without contacting registry authority.
    // Only an absent or stale lock enters online package-manager resolution.
    auto store_root = package_store_root(root);
    bool offline = false;
    const auto lock_path = root / "emojineer.lock";
    if (std::filesystem::exists(lock_path)) {
        try {
            const auto existing_lock = load_project_lock(lock_path);
            offline = !is_lock_stale(root, manifest, existing_lock);
        } catch (...) {
            // A malformed lock is not reusable; canonical lock production may replace it.
            offline = false;
        }
    }
    std::unordered_map<std::string, ResolvedRegistryDependency> resolved;
    std::unordered_set<std::string> resolving;
    auto resolved_deps = resolve_registry_dependencies_impl(
        manifest, store_root, root, offline, resolved, resolving);
    
    // Convert to lock format 3 and output canonical text
    // Use the same producer as sync_project for consistent lock production
    auto lock = manifest_to_lock_with_resolved_deps(root, manifest, resolved_deps);
    return canonical_lock_text(lock);
}

std::vector<ProjectDiagnostic> check_project(const std::filesystem::path& root) {
    std::vector<ProjectDiagnostic> diagnostics;
    ProjectManifest manifest;
    try {
        manifest = load_project_manifest(root / "emojineer.toml");
    } catch (const std::exception& error) {
        diagnostics.push_back({error.what()});
        return diagnostics;
    }

    const auto entry_path = root / manifest.entry;
    if (!std::filesystem::exists(entry_path)) {
        diagnostics.push_back({"entry source does not exist: " + manifest.entry.generic_string()});
    } else if (!std::filesystem::is_regular_file(entry_path)) {
        diagnostics.push_back({"entry source is not a regular file: " + manifest.entry.generic_string()});
    } else {
        try {
            (void)compile_file(entry_path, {}, root);
        } catch (const std::exception& error) {
            diagnostics.push_back({std::string("source graph: ") + error.what()});
        }
    }

    std::string expected_lock;
    try {
        const auto graph = resolve_package_graph(root, manifest);
        for (const auto& package : graph.packages) {
            if (package.name == graph.root_name) continue;
            const auto dependency_entry = package.root / package.entry;
            if (!std::filesystem::is_regular_file(dependency_entry)) {
                diagnostics.push_back({"dependency '" + package.name + "' entry source does not exist: " +
                                       package.entry.generic_string()});
                continue;
            }
            try {
                (void)compile_file(dependency_entry, {}, package.root);
            } catch (const std::exception& error) {
                diagnostics.push_back({"dependency '" + package.name + "' source graph: " + error.what()});
            }
        }
        expected_lock = canonical_project_lock(root, manifest);
    } catch (const std::exception& error) {
        diagnostics.push_back({std::string("dependency graph: ") + error.what()});
    }

    const auto lock_path = root / "emojineer.lock";
    if (std::filesystem::exists(lock_path) && !expected_lock.empty()) {
        try {
            if (read_text(lock_path) != expected_lock) {
                diagnostics.push_back({"emojineer.lock is stale; run 'emji lock'"});
            }
        } catch (const std::exception& error) {
            diagnostics.push_back({std::string("cannot read lockfile: ") + error.what()});
        }
    }

    return diagnostics;
}

void write_project_lock(const std::filesystem::path& root, const ProjectManifest& manifest) {
    write_text(root / "emojineer.lock", canonical_project_lock(root, manifest));
}

void write_project_lock_with_resolved_deps(
    const std::filesystem::path& root,
    const ProjectManifest& manifest,
    const std::vector<ResolvedRegistryDependency>& resolved_registry_deps) {
    auto lock = manifest_to_lock_with_resolved_deps(root, manifest, resolved_registry_deps);
    write_text(root / "emojineer.lock", canonical_lock_text(lock));
}

void add_project_dependency(const std::filesystem::path& root,
                            const std::string& name,
                            const std::filesystem::path& path) {
    validate_name(name, "dependency name");
    validate_dependency_path(path, name);

    auto manifest = load_project_manifest(root / "emojineer.toml");
    for (const auto& dependency : manifest.dependencies) {
        if (dependency.name == name) {
            throw std::runtime_error("dependency '" + name + "' already exists");
        }
    }

    const auto dependency_root = root / path;
    const auto dependency_manifest = load_project_manifest(dependency_root / "emojineer.toml");
    if (dependency_manifest.name != name) {
        throw std::runtime_error("dependency key '" + name + "' points to package '" +
                                 dependency_manifest.name + "'");
    }

    manifest.dependencies.push_back({name, DependencyKind::Path, path.lexically_normal(), {}, {}});
    validate_manifest(manifest);
    (void)resolve_package_graph(root, manifest);
    write_manifest(root, manifest);
    write_project_lock(root, manifest);
}

void remove_project_dependency(const std::filesystem::path& root,
                               const std::string& name) {
    auto manifest = load_project_manifest(root / "emojineer.toml");
    const auto before = manifest.dependencies.size();
    manifest.dependencies.erase(
        std::remove_if(manifest.dependencies.begin(), manifest.dependencies.end(),
                      [&](const ProjectDependency& dependency) { return dependency.name == name; }),
        manifest.dependencies.end());
    if (manifest.dependencies.size() == before) {
        throw std::runtime_error("dependency '" + name + "' is not declared");
    }

    validate_manifest(manifest);
    (void)resolve_package_graph(root, manifest);
    write_manifest(root, manifest);
    write_project_lock(root, manifest);
}

std::string get_registry_alias_for_dependency(const ProjectManifest& manifest,
                                              const std::string& dependency_name) {
    for (const auto& dependency : manifest.dependencies) {
        if (dependency.name == dependency_name && dependency.kind == DependencyKind::Registry) {
            return dependency.registry_alias;
        }
    }
    throw std::runtime_error("dependency '" + dependency_name + "' not found or is not a registry dependency");
}

std::string get_registry_endpoint_for_alias(const ProjectManifest& manifest,
                                             const std::string& alias) {
    for (const auto& registry : manifest.registries) {
        if (registry.alias == alias) {
            return registry.endpoint;
        }
    }
    throw std::runtime_error("registry '" + alias + "' not found in manifest");
}

void add_project_registry_dependency(const std::filesystem::path& root,
                                     const std::string& name,
                                     const std::string& requirement,
                                     const std::string& registry_endpoint,
                                     const std::string& registry_alias) {
    auto manifest = load_project_manifest(root / "emojineer.toml");
    
    // Check if dependency already exists
    for (const auto& dependency : manifest.dependencies) {
        if (dependency.name == name) {
            throw std::runtime_error("dependency '" + name + "' already exists");
        }
    }
    
    // Find or add the registry
    std::string alias = registry_alias;
    if (alias.empty()) {
        alias = "origin";  // Default registry alias
    }
    
    // Check if registry already exists
    bool registry_found = false;
    for (const auto& registry : manifest.registries) {
        if (registry.alias == alias) {
            if (registry.endpoint != registry_endpoint) {
                throw std::runtime_error("registry '" + alias + "' already exists with different endpoint");
            }
            registry_found = true;
            break;
        }
    }
    
    // Add new registry if not found
    if (!registry_found) {
        manifest.registries.push_back({alias, registry_endpoint});
    }
    
    // Add the registry dependency
    manifest.dependencies.push_back({name, DependencyKind::Registry, {}, alias, requirement});
    
    validate_manifest(manifest);
    write_manifest(root, manifest);
    write_project_lock(root, manifest);
}

void sync_project(const std::filesystem::path& root, bool offline) {
    sync_project(root, {}, offline);
}

// Forward declaration for recursive resolution
std::vector<ResolvedRegistryDependency> resolve_registry_dependencies_impl(
    const ProjectManifest& manifest,
    const std::filesystem::path& store_root,
    const std::filesystem::path& project_root,
    bool offline,
    std::unordered_map<std::string, ResolvedRegistryDependency>& resolved,
    std::unordered_set<std::string>& resolving);

// Public resolve_registry_dependencies wrapper
std::vector<ResolvedRegistryDependency> resolve_registry_dependencies(
    const ProjectManifest& manifest,
    const std::filesystem::path& store_root,
    const std::filesystem::path& project_root,
    bool offline) {
    std::unordered_map<std::string, ResolvedRegistryDependency> resolved;
    std::unordered_set<std::string> resolving;
    return resolve_registry_dependencies_impl(manifest, store_root, project_root, offline, resolved, resolving);
}

// Convert manifest to lock format 3
ProjectLock manifest_to_lock(const std::filesystem::path& root,
                            const ProjectManifest& manifest) {
    ProjectLock lock;
    lock.version = "3";
    lock.manifest_hash = project_manifest_hash(manifest);
    
    // Add registries
    for (const auto& reg : manifest.registries) {
        auto endpoint = parse_registry_endpoint(reg.endpoint);
        lock.registries.push_back({
            reg.alias,
            registry_identity(endpoint),
            endpoint.canonical
        });
    }
    
    // Resolve package graph
    auto canonical_root = std::filesystem::canonical(root);
    auto graph = resolve_package_graph(canonical_root, manifest);
    
    // Add dependencies
    for (const auto& pkg : graph.packages) {
        if (pkg.name == graph.root_name) continue;
        
        LockDependency dep;
        dep.name = pkg.name;
        dep.version = pkg.version;
        dep.source = (pkg.source_kind == DependencyKind::Path) ? LockSourceKind::Path : LockSourceKind::Registry;
        dep.path = relative_package_path(canonical_root, pkg.root);
        dep.content_sha256 = pkg.content_sha256;
        
        if (pkg.source_kind == DependencyKind::Registry) {
            dep.registry_alias = pkg.registry_alias;
            dep.registry_id = pkg.registry_id;
            dep.registry_endpoint = pkg.registry_endpoint;
            dep.requirement = pkg.requirement;
            dep.artifact_sha256 = pkg.artifact_sha256;
            dep.store_path = pkg.store_path;
        }
        
        dep.dependencies = pkg.dependencies;
        lock.dependencies.push_back(dep);
    }
    
    return lock;
}

// Convert manifest to lock format 3 using pre-resolved registry dependencies
// This avoids re-fetching from registry during lock writing
ProjectLock manifest_to_lock_with_resolved_deps(
    const std::filesystem::path& root,
    const ProjectManifest& manifest,
    const std::vector<ResolvedRegistryDependency>& resolved_registry_deps) {
    ProjectLock lock;
    lock.version = "3";
    lock.manifest_hash = project_manifest_hash(manifest);
    
    // Add registries
    for (const auto& reg : manifest.registries) {
        auto endpoint = parse_registry_endpoint(reg.endpoint);
        lock.registries.push_back({
            reg.alias,
            registry_identity(endpoint),
            endpoint.canonical
        });
    }
    
    // Registry aliases are lock-wide authority records. Preserve authorities discovered
    // through transitive registry packages as well as root-declared registries.
    for (const auto& resolved : resolved_registry_deps) {
        auto existing = std::find_if(lock.registries.begin(), lock.registries.end(),
                                     [&](const LockRegistry& registry) {
                                         return registry.alias == resolved.registry_alias &&
                                                registry.id == resolved.registry_id &&
                                                registry.endpoint == resolved.registry_endpoint;
                                     });
        if (existing == lock.registries.end()) {
            lock.registries.push_back({resolved.registry_alias,
                                       resolved.registry_id,
                                       resolved.registry_endpoint});
        }
    }

    // Resolve package graph for path dependencies
    auto canonical_root = std::filesystem::canonical(root);
    auto graph = resolve_package_graph(canonical_root, manifest);
    
    // First add path dependencies from the graph
    for (const auto& pkg : graph.packages) {
        if (pkg.name == graph.root_name) continue;
        if (pkg.source_kind != DependencyKind::Path) continue;
        
        LockDependency dep;
        dep.name = pkg.name;
        dep.version = pkg.version;
        dep.source = LockSourceKind::Path;
        dep.path = relative_package_path(canonical_root, pkg.root);
        dep.content_sha256 = pkg.content_sha256;
        dep.dependencies = pkg.dependencies;
        lock.dependencies.push_back(dep);
    }
    
    // Then add ALL resolved registry dependencies (including transitive)
    // This ensures the lock has complete registry metadata for offline operation
    for (const auto& resolved : resolved_registry_deps) {
        // Skip if already added as a path dependency
        if (graph.find(resolved.name)) continue;
        
        LockDependency dep;
        dep.name = resolved.name;
        dep.version = resolved.version;
        dep.source = LockSourceKind::Registry;
        
        // Registry metadata
        dep.registry_alias = resolved.registry_alias;
        dep.registry_id = resolved.registry_id;
        dep.registry_endpoint = resolved.registry_endpoint;
        dep.requirement = resolved.requirement;
        dep.artifact_sha256 = resolved.artifact_sha256;
        dep.store_path = resolved.store_path;
        dep.content_sha256 = resolved.content_sha256;
        
        // Extract dependency names from the resolved dependency's dependencies
        for (const auto& nested_dep : resolved.dependencies) {
            dep.dependencies.push_back(nested_dep.name);
        }
        
        lock.dependencies.push_back(dep);
    }
    
    return lock;
}

// Helper to compute registry key from alias
std::string registry_key(const std::string& alias) {
    // Simple key derivation - replace non-alphanumeric with underscores
    std::string key;
    for (char c : alias) {
        if (std::isalnum(c) || c == '-' || c == '_') {
            key += c;
        } else {
            key += '_';
        }
    }
    return key;
}

// Resolve registry dependencies recursively
std::vector<ResolvedRegistryDependency> resolve_registry_dependencies_impl(
    const ProjectManifest& manifest,
    const std::filesystem::path& store_root,
    const std::filesystem::path& project_root,
    bool offline,
    std::unordered_map<std::string, ResolvedRegistryDependency>& resolved,
    std::unordered_set<std::string>& resolving) {
    
    std::vector<ResolvedRegistryDependency> result;
    
    for (const auto& dep : manifest.dependencies) {
        if (dep.kind != DependencyKind::Registry) continue;
        
        // Check if already resolved
        auto key = dep.name + "@" + dep.requirement;
        if (resolved.find(key) != resolved.end()) {
            result.push_back(resolved[key]);
            continue;
        }
        
        // Detect cycles
        if (resolving.find(dep.name) != resolving.end()) {
            throw std::runtime_error("cyclic registry dependency detected: " + dep.name);
        }
        resolving.insert(dep.name);
        
        bool resolved_from_lock = false;
        
        // In offline mode, try to load from existing lock file
        if (offline) {
            // Use project_root for lock file path
            auto lock_path = project_root / "emojineer.lock";
            if (std::filesystem::exists(lock_path)) {
                try {
                    auto lock = load_project_lock(lock_path);

                    // Registry aliases are package-local coordinates. Resolve the authority
                    // from the CURRENT owning manifest before accepting a lock entry.
                    const auto owner_registry = std::find_if(
                        manifest.registries.begin(), manifest.registries.end(),
                        [&](const ProjectRegistry& registry) {
                            return registry.alias == dep.registry_alias;
                        });
                    if (owner_registry == manifest.registries.end()) {
                        throw std::runtime_error("offline registry dependency '" + dep.name +
                                                 "' references unknown owner registry '" +
                                                 dep.registry_alias + "'");
                    }
                    // Canonicalizing the endpoint is local/string-only. Do NOT call
                    // registry_identity() here: file/HTTPS identity discovery is network/authority I/O
                    // and ordinary offline resolution must never contact the registry.
                    const auto expected_endpoint = parse_registry_endpoint(owner_registry->endpoint);

                    for (const auto& lock_dep : lock.dependencies) {
                        if (lock_dep.name == dep.name &&
                            lock_dep.source == LockSourceKind::Registry &&
                            lock_dep.registry_alias && *lock_dep.registry_alias == dep.registry_alias &&
                            lock_dep.requirement && *lock_dep.requirement == dep.requirement &&
                            lock_dep.registry_endpoint && *lock_dep.registry_endpoint == expected_endpoint.canonical) {
                            // Found the exact owner-scoped coordinate in the lock.
                            ResolvedRegistryDependency resolved_dep;
                            resolved_dep.name = lock_dep.name;
                            resolved_dep.version = lock_dep.version;
                            resolved_dep.registry_alias = lock_dep.registry_alias.value_or("");
                            resolved_dep.registry_id = lock_dep.registry_id.value_or("");
                            resolved_dep.registry_endpoint = lock_dep.registry_endpoint.value_or("");
                            resolved_dep.requirement = lock_dep.requirement.value_or("");
                            resolved_dep.artifact_sha256 = lock_dep.artifact_sha256.value_or("");
                            resolved_dep.content_sha256 = lock_dep.content_sha256.value_or("");
                            resolved_dep.store_path = lock_dep.store_path.value_or(std::filesystem::path());
                            
                            // Load dependencies from embedded manifest if available
                            // Corrupted manifest failures must propagate in BOTH online and offline modes
                            if (std::filesystem::exists(resolved_dep.store_path / "emojineer.toml")) {
                                auto embedded_manifest = load_project_manifest(resolved_dep.store_path / "emojineer.toml");
                                resolved_dep.dependencies = embedded_manifest.dependencies;
                                for (const auto& embedded_dep : embedded_manifest.dependencies) {
                                    if (embedded_dep.kind == DependencyKind::Path) {
                                        resolving.erase(dep.name);
                                        throw std::runtime_error("registry package '" + resolved_dep.name + "'@'" + resolved_dep.version +
                                                                 "' contains path dependency '" + embedded_dep.name + "' which cannot be resolved by consumers");
                                    }
                                }
                            }
                            
                            resolved[key] = resolved_dep;
                            result.push_back(resolved_dep);
                            
                            // Recursively resolve dependencies from lock using the materialized
                            // package's own registry bindings, not the root application's aliases.
                            ProjectManifest synthetic_manifest;
                            synthetic_manifest.dependencies = resolved_dep.dependencies;
                            if (std::filesystem::exists(resolved_dep.store_path / "emojineer.toml")) {
                                const auto embedded_manifest = load_project_manifest(
                                    resolved_dep.store_path / "emojineer.toml");
                                synthetic_manifest.registries = embedded_manifest.registries;
                            }
                            auto nested = resolve_registry_dependencies_impl(synthetic_manifest, store_root, project_root, offline, resolved, resolving);
                            result.insert(result.end(), nested.begin(), nested.end());
                            
                            resolved_from_lock = true;
                            break;
                        }
                    }
                } catch (...) {
                    // Offline resolution is sovereign: malformed lock/materialization and
                    // structural package failures are terminal, never an implicit online fallback.
                    resolving.erase(dep.name);
                    throw;
                }
            }
            if (!resolved_from_lock) {
                resolving.erase(dep.name);
                throw std::runtime_error("offline mode requires existing lock file for dependency " + dep.name);
            }
        }
        
        if (!resolved_from_lock) {
            // Online resolution path
            // Find the registry endpoint
            std::string endpoint_str;
            for (const auto& reg : manifest.registries) {
                if (reg.alias == dep.registry_alias) {
                    endpoint_str = reg.endpoint;
                    break;
                }
            }
            if (endpoint_str.empty()) {
                resolving.erase(dep.name);
                throw std::runtime_error("registry '" + dep.registry_alias + "' not found for dependency '" + dep.name + "'");
            }
            
            RegistryEndpoint endpoint = parse_registry_endpoint(endpoint_str);
            // Pass store_root and registry_alias to get canonical project store paths
            ResolvedRegistryDependency resolved_dep = resolve_single_registry_dependency(endpoint, dep.name, dep.requirement, store_root, dep.registry_alias);
            
            // Store the resolved dependency
            resolved[key] = resolved_dep;
            result.push_back(resolved_dep);
            
            // Recursively resolve the dependencies of this package
            // Parse the embedded manifest from the artifact (in memory, not from disk)
            try {
                auto artifact_filename = default_package_artifact_filename(resolved_dep.name, resolved_dep.version);
                auto artifact_path = resolved_dep.store_path / artifact_filename;
                auto artifact = load_package_artifact(artifact_path);
                
                // Parse embedded manifest from artifact's manifest field (in memory)
                std::istringstream embedded_stream(artifact.manifest);
                ProjectManifest embedded_manifest;
                enum class Section { None, Package, Registries, Dependencies };
                Section section = Section::None;
                bool have_name = false;
                bool have_version = false;
                bool have_entry = false;
                std::string line;
                std::size_t line_number = 0;
                while (std::getline(embedded_stream, line)) {
                    ++line_number;
                    const std::string text = trim(line);
                    if (text.empty() || text.front() == '#') continue;
                    if (text.front() == '[') {
                        if (text == "[package]") {
                            section = Section::Package;
                            continue;
                        }
                        if (text == "[registries]") {
                            section = Section::Registries;
                            continue;
                        }
                        if (text == "[dependencies]") {
                            section = Section::Dependencies;
                            continue;
                        }
                        section = Section::None;
                        continue;
                    }
                    const auto equal = text.find('=');
                    if (equal == std::string::npos) {
                        continue; // Skip invalid lines
                    }
                    const std::string key = trim(text.substr(0, equal));
                    const std::string value = parse_quoted(text.substr(equal + 1), line_number);

                    if (section == Section::Package) {
                        if (key == "name") {
                            embedded_manifest.name = value;
                        } else if (key == "version") {
                            embedded_manifest.version = value;
                        } else if (key == "entry") {
                            embedded_manifest.entry = std::filesystem::path(value);
                        }
                    } else if (section == Section::Registries) {
                        embedded_manifest.registries.push_back({key, value});
                    } else if (section == Section::Dependencies) {
                        // Parse dependency value format: registry:alias:requirement
                        // Non-registry: values are path dependencies
                        std::string registry_alias;
                        std::string requirement;
                        DependencyKind kind;
                        if (value.find("registry:") == 0) {
                            auto remainder = value.substr(9);
                            auto colon_pos = remainder.find(':');
                            if (colon_pos != std::string::npos) {
                                registry_alias = remainder.substr(0, colon_pos);
                                requirement = remainder.substr(colon_pos + 1);
                            } else {
                                registry_alias = remainder;
                                requirement = "*";
                            }
                            kind = DependencyKind::Registry;
                        } else {
                            // Non-registry: embedded dependency values are path dependencies
                            registry_alias = "";
                            requirement = "";
                            kind = DependencyKind::Path;
                        }
                        embedded_manifest.dependencies.push_back({key, kind, {}, registry_alias, requirement});
                    }
                }
                
                // Populate dependencies from embedded manifest for lock serialization.
                resolved_dep.dependencies = embedded_manifest.dependencies;
                resolved[key] = resolved_dep;
                result.back() = resolved_dep;
                
                // Structural validation: Check for path dependencies in registry packages - reject them
                // This MUST propagate in BOTH online and offline modes - do NOT move inside try/catch
                for (const auto& embedded_dep : embedded_manifest.dependencies) {
                    if (embedded_dep.kind == DependencyKind::Path) {
                        resolving.erase(dep.name);
                        throw std::runtime_error("registry package '" + resolved_dep.name + "'@'" + resolved_dep.version + 
                                                 "' contains path dependency '" + embedded_dep.name + "' which cannot be resolved by consumers");
                    }
                }
                
                // Recursively resolve embedded dependencies using the owning package's
                // embedded registry authority. Registry aliases are package-local coordinates:
                // a child package may bind `origin` to a different endpoint than the root app.
                ProjectManifest synthetic_manifest;
                synthetic_manifest.dependencies = embedded_manifest.dependencies;
                synthetic_manifest.registries = embedded_manifest.registries;
                
                auto nested = resolve_registry_dependencies_impl(synthetic_manifest, store_root, project_root, offline, resolved, resolving);
                result.insert(result.end(), nested.begin(), nested.end());
            } catch (...) {
                // Artifact loading failures must propagate in BOTH online and offline modes.
                // Structural validation failures, path-dependency rejection, corruption, hash mismatch,
                // malformed artifact/manifest, and declared locked dependency failures are all critical
                // and must NOT be silently swallowed in offline mode.
                throw;
            }
        }
        
        resolving.erase(dep.name);
    }
    
    return result;
}

void sync_project(const std::filesystem::path& root,
                 const std::filesystem::path& cache_root,
                 bool offline) {
    auto manifest = load_project_manifest(root / "emojineer.toml");
    auto store_root = package_store_root(root);
    
    // Create store root if needed
    if (!std::filesystem::exists(store_root)) {
        std::filesystem::create_directories(store_root);
    }
    
    // Offline sync may not be a weaker authority than offline compile/LSP.
    // Validate the existing lock and the complete materialized package graph BEFORE
    // resolving or rewriting anything. A malformed/stale/wrong-authority lock fails here.
    if (offline) {
        (void)resolve_package_graph(root, manifest, store_root, true);
    }

    // Resolve registry dependencies
    std::unordered_map<std::string, ResolvedRegistryDependency> resolved;
    std::unordered_set<std::string> resolving;
    auto resolved_deps = resolve_registry_dependencies_impl(manifest, store_root, root, offline, resolved, resolving);
    
    // Materialize packages
    for (const auto& dep : resolved_deps) {
        // Load and materialize the artifact
        try {
            auto artifact_path = dep.store_path / default_package_artifact_filename(dep.name, dep.version);
            if (std::filesystem::exists(artifact_path)) {
                auto artifact = load_package_artifact(artifact_path);
                
                // Verify artifact SHA-256 matches
                if (artifact.artifact_sha256 != dep.artifact_sha256) {
                    throw std::runtime_error("artifact SHA-256 mismatch for " + dep.name + "@" + dep.version);
                }
                
                // Check if already materialized - look for emojineer.toml inside
                auto pkg_path = store_root / registry_key(dep.registry_alias) / dep.name / dep.version / dep.artifact_sha256;
                auto manifest_path = pkg_path / "emojineer.toml";
                if (!std::filesystem::exists(manifest_path)) {
                    // Materialize the package
                    std::filesystem::create_directories(pkg_path);
                    
                    // Write manifest - preserve [dependencies] section for transitive dependency traversal
                    std::string manifest_text = artifact.manifest;
                    // Ensure we have at least a minimal [package] section
                    if (manifest_text.find("[package]") == std::string::npos) {
                        manifest_text = "[package]\nname = \"" + artifact.name + "\"\nversion = \"" + artifact.version + "\"\nentry = \"\"\n\n" + manifest_text;
                    }
                    write_text(manifest_path, manifest_text);
                    
                    // Write source files
                    for (const auto& file : artifact.files) {
                        auto file_path = pkg_path / file.path;
                        std::filesystem::create_directories(file_path.parent_path());
                        write_text(file_path, file.source);
                    }
                }
            }
        } catch (const std::exception& e) {
            // Materialization failures must propagate in BOTH online and offline modes.
            // Corruption, hash mismatch, malformed artifact/manifest failures are critical
            // and must NOT be silently swallowed in offline mode.
            throw;
        }
    }
    
    // Resolve the full package graph including materialized packages
    (void)resolve_package_graph(root, manifest);
    
    // Write lock format 3 using the pre-resolved registry dependencies
    // This ensures the lock contains all transitive registry dependencies
    // and their metadata (registry identity, exact version, artifact hashes, store paths)
    write_project_lock_with_resolved_deps(root, manifest, resolved_deps);
}

// Lock format 3 canonical text
std::string canonical_lock_text(const ProjectLock& lock) {
    std::ostringstream out;
    out << "lock_version = 3\n"
        << "manifest_hash = \"" << lock.manifest_hash << "\"\n";
    
    // Sort registries by alias for deterministic output
    std::vector<LockRegistry> sorted_regs = lock.registries;
    std::sort(sorted_regs.begin(), sorted_regs.end(),
              [](const LockRegistry& a, const LockRegistry& b) {
                  if (a.alias != b.alias) return a.alias < b.alias;
                  if (a.id != b.id) return a.id < b.id;
                  return a.endpoint < b.endpoint;
              });
    
    // Sort dependencies by name for deterministic output
    std::vector<LockDependency> sorted_deps = lock.dependencies;
    std::sort(sorted_deps.begin(), sorted_deps.end(),
              [](const LockDependency& a, const LockDependency& b) {
                  return a.name < b.name;
              });
    
    // Output registries (sorted by alias)
    for (const auto& reg : sorted_regs) {
        out << "\n[[registry]]\n"
            << "alias = \"" << reg.alias << "\"\n"
            << "id = \"" << reg.id << "\"\n"
            << "endpoint = \"" << reg.endpoint << "\"\n";
    }
    
    // Output dependencies (sorted by name for deterministic output)
    for (const auto& dep : sorted_deps) {
        out << "\n[[dependency]]\n"
            << "source = \"" << (dep.source == LockSourceKind::Path ? "path" : "registry") << "\"\n"
            << "name = \"" << dep.name << "\"\n"
            << "version = \"" << dep.version << "\"\n";
        
        if (dep.source == LockSourceKind::Path) {
            if (dep.path) {
                out << "path = \"" << dep.path->generic_string() << "\"\n";
            }
            if (dep.content_sha256) {
                out << "content_sha256 = \"" << *dep.content_sha256 << "\"\n";
            }
        } else {
            if (dep.registry_alias) out << "registry = \"" << *dep.registry_alias << "\"\n";
            if (dep.registry_id) out << "registry_id = \"" << *dep.registry_id << "\"\n";
            if (dep.registry_endpoint) out << "registry_endpoint = \"" << *dep.registry_endpoint << "\"\n";
            if (dep.requirement) out << "requirement = \"" << *dep.requirement << "\"\n";
            if (dep.artifact_sha256) out << "artifact_sha256 = \"" << *dep.artifact_sha256 << "\"\n";
            if (dep.content_sha256) out << "content_sha256 = \"" << *dep.content_sha256 << "\"\n";
            if (dep.store_path) out << "store_path = \"" << *dep.store_path << "\"\n";
        }
        
        out << "dependencies = \"" << joined_dependencies(dep.dependencies) << "\"\n";
    }
    
    return out.str();
}

ProjectLock load_project_lock(const std::filesystem::path& lock_path) {
    auto text = read_text(lock_path);
    ProjectLock lock;
    lock.version.clear();
    
    // Simple TOML-like parsing
    std::istringstream stream(text);
    std::string line;
    LockDependency* current_dep = nullptr;
    LockRegistry* current_reg = nullptr;
    
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        
        if (!current_reg && !current_dep && line.rfind("lock_version", 0) == 0) {
            const auto eq = line.find('=');
            if (eq == std::string::npos || !lock.version.empty()) {
                throw std::runtime_error("malformed or duplicate lock_version");
            }
            lock.version = trim(line.substr(eq + 1));
            if (lock.version.empty()) throw std::runtime_error("lock_version cannot be empty");
        } else if (line == "[[registry]]") {
            lock.registries.push_back({});
            current_reg = &lock.registries.back();
            current_dep = nullptr;
        } else if (line == "[[dependency]]") {
            lock.dependencies.push_back({});
            current_dep = &lock.dependencies.back();
            current_reg = nullptr;
        } else if (current_reg) {
            auto eq = line.find('=');
            if (eq != std::string::npos) {
                auto key = trim(line.substr(0, eq));
                auto value = trim(parse_quoted(line.substr(eq + 1), 1));
                if (key == "alias") current_reg->alias = value;
                else if (key == "id") current_reg->id = value;
                else if (key == "endpoint") current_reg->endpoint = value;
            }
        } else if (current_dep) {
            auto eq = line.find('=');
            if (eq != std::string::npos) {
                auto key = trim(line.substr(0, eq));
                auto value = trim(parse_quoted(line.substr(eq + 1), 1));
                if (key == "source") {
                    if (value == "registry") current_dep->source = LockSourceKind::Registry;
                    else if (value == "path") current_dep->source = LockSourceKind::Path;
                    else throw std::runtime_error("unsupported lock dependency source '" + value + "'");
                } else if (key == "name") current_dep->name = value;
                else if (key == "version") current_dep->version = value;
                else if (key == "path") current_dep->path = std::filesystem::path(value);
                else if (key == "content_sha256") current_dep->content_sha256 = value;
                else if (key == "registry") current_dep->registry_alias = value;
                else if (key == "registry_id") current_dep->registry_id = value;
                else if (key == "registry_endpoint") current_dep->registry_endpoint = value;
                else if (key == "requirement") current_dep->requirement = value;
                else if (key == "artifact_sha256") current_dep->artifact_sha256 = value;
                else if (key == "store_path") current_dep->store_path = std::filesystem::path(value);
                else if (key == "dependencies") {
                    // Parse comma-separated dependencies
                    std::string deps = value;
                    std::size_t start = 0;
                    while (start < deps.size()) {
                        auto comma = deps.find(',', start);
                        auto dep_name = trim(deps.substr(start, comma - start));
                        if (!dep_name.empty()) {
                            current_dep->dependencies.push_back(dep_name);
                        }
                        if (comma == std::string::npos) break;
                        start = comma + 1;
                    }
                }
            }
        } else if (line.find("manifest_hash") != std::string::npos) {
            auto eq = line.find('=');
            if (eq != std::string::npos) {
                lock.manifest_hash = trim(parse_quoted(line.substr(eq + 1), 1));
            }
        }
    }
    
    return lock;
}

bool is_lock_stale(const std::filesystem::path& root, const ProjectManifest& manifest,
                   const ProjectLock& lock) {
    auto current_hash = project_manifest_hash(manifest);
    if (current_hash != lock.manifest_hash) {
        return true;
    }
    
    // Check if all locked dependencies still exist
    for (const auto& dep : lock.dependencies) {
        if (dep.source == LockSourceKind::Path) {
            if (dep.path) {
                auto full_path = root / *dep.path;
                if (!std::filesystem::exists(full_path)) {
                    return true;
                }
            }
        } else {
            if (dep.store_path) {
                if (!std::filesystem::exists(*dep.store_path)) {
                    return true;
                }
            }
        }
    }
    
    return false;
}

std::filesystem::path package_store_root(const std::filesystem::path& root) {
    return root / ".emojineer" / "packages";
}

void materialize_package(const std::filesystem::path& store_root,
                        const std::string& registry_key,
                        const std::string& package_name,
                        const std::string& version,
                        const std::string& artifact_sha256,
                        const std::vector<std::pair<std::string, std::string>>& files,
                        const std::string& manifest_content) {
    auto pkg_path = store_root / registry_key / package_name / version / artifact_sha256;
    
    // Create a clean artifact-specific staging directory.
    auto staging = pkg_path.parent_path() / (".staging-" + artifact_sha256);
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    
    // Write manifest
    write_text(staging / "emojineer.toml", manifest_content);
    
    // Write source files
    for (const auto& [file_path, content] : files) {
        auto full_path = staging / file_path;
        std::filesystem::create_directories(full_path.parent_path());
        write_text(full_path, content);
    }
    
    // Replace corrupt/missing materialization with the fully written staging tree.
    if (std::filesystem::exists(pkg_path)) {
        std::filesystem::remove_all(pkg_path);
    }
    std::filesystem::rename(staging, pkg_path);
}

bool is_materialized_package_valid(const std::filesystem::path& package_path,
                                  const std::string& expected_sha256) {
    if (!std::filesystem::exists(package_path)) return false;
    if (!std::filesystem::is_directory(package_path)) return false;
    
    // Check that the package contains a valid manifest
    auto manifest_path = package_path / "emojineer.toml";
    if (!std::filesystem::exists(manifest_path)) return false;
    
    // Load the manifest to compute the content hash
    try {
        auto manifest = load_project_manifest(manifest_path);
        
        // Recompute the hash of materialized content and verify it matches expected
        // This ensures the package hasn't been corrupted or modified since materialization
        auto computed_sha256 = compute_registry_package_hash(package_path, manifest);
        if (computed_sha256 != expected_sha256) {
            return false;
        }
    } catch (...) {
        // If we can't load the manifest or compute the hash, consider it invalid
        return false;
    }
    
    return true;
}

void verify_or_repair_materialization(const std::filesystem::path& root,
                                     const ProjectLock& lock,
                                     const std::filesystem::path& cache_root) {
    auto store_root = package_store_root(root);
    
    for (const auto& dep : lock.dependencies) {
        if (dep.source != LockSourceKind::Registry) continue;
        if (!dep.store_path) continue;
        
        auto pkg_path = *dep.store_path;
        
        // Use content_sha256 to verify materialized package content
        // If content_sha256 is not available, skip verification
        if (dep.content_sha256) {
            if (!is_materialized_package_valid(pkg_path, *dep.content_sha256)) {
                // Package is corrupt or missing - try to repair from cache
                if (dep.artifact_sha256) {
                    if (!dep.registry_endpoint || !dep.registry_id) {
                        throw std::runtime_error("cannot repair package " + dep.name + "@" + dep.version +
                                                 " - lock is missing registry authority");
                    }
                    PackageArtifact expected;
                    expected.name = dep.name;
                    expected.version = dep.version;
                    expected.artifact_sha256 = *dep.artifact_sha256;
                    const auto registry_cache_key = sha256_hex(
                        *dep.registry_endpoint + "\n" + *dep.registry_id).substr(0, 32);
                    const auto cache_base = (cache_root.empty() ? default_registry_cache_root() : cache_root) /
                                            "registries" / registry_cache_key;
                    auto cache_path = package_cache_path(cache_base, expected);
                    
                    if (std::filesystem::exists(cache_path)) {
                        auto artifact = load_package_artifact(cache_path);
                        if (artifact.name != dep.name || artifact.version != dep.version ||
                            artifact.artifact_sha256 != *dep.artifact_sha256 ||
                            artifact.content_sha256 != *dep.content_sha256) {
                            throw std::runtime_error("cached repair artifact identity mismatch for " +
                                                     dep.name + "@" + dep.version);
                        }
                        std::vector<std::pair<std::string, std::string>> files;
                        files.reserve(artifact.files.size());
                        for (const auto& file : artifact.files) {
                            files.emplace_back(file.path, file.source);
                        }
                        materialize_package(store_root,
                                           registry_key(dep.registry_alias ? *dep.registry_alias : "origin"),
                                           dep.name,
                                           dep.version,
                                           *dep.artifact_sha256,
                                           files,
                                           artifact.manifest);
                        if (!is_materialized_package_valid(pkg_path, *dep.content_sha256)) {
                            throw std::runtime_error("repaired package failed content verification: " +
                                                     dep.name + "@" + dep.version);
                        }
                    } else {
                        throw std::runtime_error("cannot repair missing package " + dep.name + "@" + dep.version + 
                                                 " - run 'emji sync' to restore");
                    }
                } else {
                    throw std::runtime_error("cannot repair missing package " + dep.name + "@" + dep.version + 
                                             " - no artifact available");
                }
            }
        }
    }
}

// Single registry dependency resolution
ResolvedRegistryDependency resolve_single_registry_dependency(
    const RegistryEndpoint& endpoint,
    const std::string& name,
    std::string_view requirement,
    const std::filesystem::path& store_root,
    const std::string& registry_alias) {
    
    // Load package index
    auto index = load_registry_package_index(endpoint, name);
    
    // Collect available versions
    std::vector<std::string> available_versions;
    for (const auto& record : index.versions) {
        available_versions.push_back(record.version);
    }
    
    // Select deterministic version
    auto selected_version = select_deterministic_version(name, {std::string(requirement)}, available_versions);
    if (selected_version.empty()) {
        throw std::runtime_error("no matching version for " + name + "@" + std::string(requirement));
    }
    
    // Find the record
    RegistryVersionRecord record;
    for (const auto& r : index.versions) {
        if (r.version == selected_version) {
            record = r;
            break;
        }
    }
    
    // Fetch the package
    auto fetch_result = fetch_registry_package(endpoint, name, selected_version, {});
    
    // Determine store path using the provided store_root and registry_alias
    // This is the canonical project store path, not the cache path
    auto store_path = store_root / registry_key(registry_alias) / name / record.version / record.artifact_sha256;
    
    // Copy artifact from cache to store_path for materialization
    // The sync_project materialization code expects the artifact at store_path
    auto artifact_filename = default_package_artifact_filename(name, selected_version);
    auto dest_artifact_path = store_path / artifact_filename;
    if (!std::filesystem::exists(dest_artifact_path)) {
        std::filesystem::create_directories(store_path);
        std::filesystem::copy_file(fetch_result.cache_path, dest_artifact_path);
    }
    
    // Parse embedded manifest from artifact to get dependencies
    std::vector<ProjectDependency> embedded_deps;
    try {
        std::istringstream manifest_stream(fetch_result.artifact.manifest);
        ProjectManifest embedded_manifest;
        enum class Section { None, Package, Registries, Dependencies };
        Section section = Section::None;
        std::string line;
        while (std::getline(manifest_stream, line)) {
            const std::string text = trim(line);
            if (text.empty() || text.front() == '#') continue;
            if (text.front() == '[') {
                if (text == "[dependencies]") {
                    section = Section::Dependencies;
                } else {
                    section = Section::None;
                }
                continue;
            }
            if (section != Section::Dependencies) continue;
            const auto equal = text.find('=');
            if (equal == std::string::npos) continue;
            const std::string key = trim(text.substr(0, equal));
            const std::string value = parse_quoted(text.substr(equal + 1), 0);
            
            // Parse dependency value format: registry:alias:requirement
            // or just requirement (for direct deps without registry prefix)
            std::string registry_alias;
            std::string requirement;
            if (value.find("registry:") == 0) {
                // Format: registry:alias:requirement
                auto remainder = value.substr(9); // Remove "registry:"
                auto colon_pos = remainder.find(':');
                if (colon_pos != std::string::npos) {
                    registry_alias = remainder.substr(0, colon_pos);
                    requirement = remainder.substr(colon_pos + 1);
                } else {
                    registry_alias = remainder;
                    requirement = "*";
                }
            } else {
                // Direct dependency without registry prefix
                registry_alias = "";
                requirement = value;
            }
            embedded_deps.push_back({key, DependencyKind::Registry, {}, registry_alias, requirement});
        }
    } catch (...) {
        // If we can't parse the embedded manifest, that's okay - dependencies will be empty
    }
    
    return {
        name,
        record.version,
        registry_alias,  // Fill in the registry alias
        index.registry_id,
        endpoint.canonical,
        std::string(requirement),
        record.content_sha256,
        record.artifact_sha256,
        store_path,
        embedded_deps
    };
}

// Version conflict detection
std::optional<VersionConflict> detect_version_conflict(
    const std::string& package_name,
    const std::vector<std::string>& requirements,
    const std::vector<std::string>& available_versions) {
    
    // Find versions that satisfy all requirements
    std::vector<std::string> satisfying_versions;
    for (const auto& version : available_versions) {
        bool satisfies_all = true;
        for (const auto& req : requirements) {
            auto semver = parse_semantic_version(version);
            auto requirement = parse_version_requirement(req);
            if (!version_requirement_matches(requirement, semver)) {
                satisfies_all = false;
                break;
            }
        }
        if (satisfies_all) {
            satisfying_versions.push_back(version);
        }
    }
    
    if (satisfying_versions.empty()) {
        return VersionConflict{package_name, requirements, available_versions};
    }
    return std::nullopt;
}

std::string select_deterministic_version(
    const std::string& package_name,
    const std::vector<std::string>& requirements,
    const std::vector<std::string>& available_versions) {
    
    // First check for conflicts
    if (auto conflict = detect_version_conflict(package_name, requirements, available_versions)) {
        std::ostringstream out;
        out << "version conflict for " << package_name << ": requirements ";
        for (std::size_t i = 0; i < conflict->requirements.size(); ++i) {
            if (i > 0) out << ", ";
            out << conflict->requirements[i];
        }
        out << " cannot be satisfied by available versions";
        throw std::runtime_error(out.str());
    }
    
    // Select highest satisfying version
    std::optional<SemanticVersion> selected;
    for (const auto& version_text : available_versions) {
        bool satisfies_all = true;
        for (const auto& req : requirements) {
            auto semver = parse_semantic_version(version_text);
            auto requirement = parse_version_requirement(req);
            if (!version_requirement_matches(requirement, semver)) {
                satisfies_all = false;
                break;
            }
        }
        if (!satisfies_all) continue;
        
        auto semver = parse_semantic_version(version_text);
        if (!selected || compare_semantic_versions(semver, *selected) > 0) {
            selected = semver;
        }
    }
    
    if (!selected) return "";
    return selected->text;
}

// Transactional add implementation
void add_project_registry_dependency_transactional(const std::filesystem::path& root,
                                                  const std::string& name,
                                                  const std::string& requirement,
                                                  const std::string& registry_endpoint,
                                                  const std::string& registry_alias) {
    auto manifest_path = root / "emojineer.toml";
    auto lock_path = root / "emojineer.lock";
    
    // Read original state for rollback
    auto original_manifest = read_text(manifest_path);
    std::string original_lock;
    if (std::filesystem::exists(lock_path)) {
        original_lock = read_text(lock_path);
    }
    
    try {
        // Add the dependency
        add_project_registry_dependency(root, name, requirement, registry_endpoint, registry_alias);
        
        // Sync to verify the entire operation works
        sync_project(root, {}, false);
    } catch (const std::exception& e) {
        // Rollback: restore original manifest and lock
        write_text(manifest_path, original_manifest);
        if (!original_lock.empty()) {
            write_text(lock_path, original_lock);
        } else if (std::filesystem::exists(lock_path)) {
            std::filesystem::remove(lock_path);
        }
        throw;
    }
}

} // namespace emojineer
