#include "emojineer/project.hpp"
#include "emojineer/module.hpp"
#include "emojineer/package.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace emojineer {
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

void validate_dependency_path(const std::filesystem::path& path, const std::string& name) {
    if (path.empty() || path.is_absolute()) {
        throw std::runtime_error("dependency '" + name + "' path must be non-empty and relative");
    }
    const std::string generic = path.generic_string();
    if (generic.find('"') != std::string::npos || generic.find('\\') != std::string::npos) {
        throw std::runtime_error("dependency '" + name + "' path must use portable forward-slash syntax");
    }
}

void validate_manifest(const ProjectManifest& manifest) {
    validate_name(manifest.name);
    validate_version(manifest.version);
    validate_entry(manifest.entry);

    std::set<std::string> names;
    for (const auto& dependency : manifest.dependencies) {
        validate_name(dependency.name, "dependency name");
        validate_dependency_path(dependency.path, dependency.name);
        if (dependency.name == manifest.name) {
            throw std::runtime_error("package may not declare itself as dependency '" + dependency.name + "'");
        }
        if (!names.insert(dependency.name).second) {
            throw std::runtime_error("manifest contains duplicate dependency '" + dependency.name + "'");
        }
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

ProjectManifest load_project_manifest(const std::filesystem::path& manifest_path) {
    std::istringstream input(read_text(manifest_path));
    ProjectManifest manifest;
    enum class Section { None, Package, Dependencies };
    Section section = Section::None;
    bool have_name = false;
    bool have_version = false;
    bool have_entry = false;
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

        if (section == Section::Dependencies) {
            if (!dependency_names.insert(key).second) {
                throw std::runtime_error("manifest contains duplicate dependency '" + key + "'");
            }
            manifest.dependencies.push_back({key, std::filesystem::path(value)});
            continue;
        }

        throw std::runtime_error("manifest line " + std::to_string(line_number) +
                                 ": keys must appear inside [package] or [dependencies]");
    }

    if (!have_name || !have_version || !have_entry) {
        throw std::runtime_error("manifest [package] requires name, version, and entry");
    }
    validate_manifest(manifest);
    return manifest;
}

std::string canonical_manifest_text(const ProjectManifest& manifest) {
    validate_manifest(manifest);
    std::ostringstream out;
    out << "[package]\n"
        << "name = \"" << manifest.name << "\"\n"
        << "version = \"" << manifest.version << "\"\n"
        << "entry = \"" << manifest.entry.generic_string() << "\"\n";

    const auto dependencies = sorted_dependencies(manifest);
    if (!dependencies.empty()) {
        out << "\n[dependencies]\n";
        for (const auto& dependency : dependencies) {
            out << dependency.name << " = \"" << dependency.path.generic_string() << "\"\n";
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
    ProjectManifest manifest{name, "0.1.0", std::filesystem::path("src/main.emoji"), {}};
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
    const auto canonical_root = std::filesystem::canonical(root);
    const auto graph = resolve_package_graph(canonical_root, manifest);

    std::ostringstream out;
    out << "lock_version = 2\n"
        << "manifest_hash = \"" << project_manifest_hash(manifest) << "\"\n"
        << "package = \"" << manifest.name << "\"\n"
        << "version = \"" << manifest.version << "\"\n"
        << "entry = \"" << manifest.entry.generic_string() << "\"\n";

    std::size_t dependency_count = 0;
    for (const auto& package : graph.packages) {
        if (package.name != graph.root_name) ++dependency_count;
    }
    out << "dependency_count = " << dependency_count << "\n";

    for (const auto& package : graph.packages) {
        if (package.name == graph.root_name) continue;
        out << "\n[[dependency]]\n"
            << "name = \"" << package.name << "\"\n"
            << "version = \"" << package.version << "\"\n"
            << "path = \"" << relative_package_path(canonical_root, package.root) << "\"\n"
            << "content_sha256 = \"" << package.content_sha256 << "\"\n"
            << "dependencies = \"" << joined_dependencies(package.dependencies) << "\"\n";
    }
    return out.str();
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

    manifest.dependencies.push_back({name, path.lexically_normal()});
    validate_manifest(manifest);
    (void)resolve_package_graph(root, manifest);
    write_manifest(root, manifest);
    write_project_lock(root, manifest);
}

void remove_project_dependency(const std::filesystem::path& root,
                               const std::string& name) {
    auto manifest = load_project_manifest(root / "emojineer.toml");
    const auto before = manifest.dependencies.size();
    std::erase_if(manifest.dependencies,
                  [&](const ProjectDependency& dependency) { return dependency.name == name; });
    if (manifest.dependencies.size() == before) {
        throw std::runtime_error("dependency '" + name + "' is not declared");
    }

    validate_manifest(manifest);
    (void)resolve_package_graph(root, manifest);
    write_manifest(root, manifest);
    write_project_lock(root, manifest);
}

} // namespace emojineer
