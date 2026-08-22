#include "emojineer/project.hpp"
#include "emojineer/module.hpp"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <regex>
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

void validate_name(const std::string& name) {
    if (name.empty()) throw std::runtime_error("project name cannot be empty");
    for (unsigned char c : name) {
        if (!(std::isalnum(c) || c == '-' || c == '_')) {
            throw std::runtime_error("project name may contain only ASCII letters, digits, '-' and '_'");
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

void validate_manifest(const ProjectManifest& manifest) {
    validate_name(manifest.name);
    validate_version(manifest.version);
    validate_entry(manifest.entry);
}

std::uint64_t fnv1a64(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string lock_hash_from_text(const std::string& text) {
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        if (trim(line.substr(0, equal)) == "manifest_hash") {
            return parse_quoted(line.substr(equal + 1), 0);
        }
    }
    return {};
}

} // namespace

ProjectManifest load_project_manifest(const std::filesystem::path& manifest_path) {
    std::istringstream input(read_text(manifest_path));
    ProjectManifest manifest;
    bool in_package = false;
    bool have_name = false;
    bool have_version = false;
    bool have_entry = false;
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;
        const std::string text = trim(line);
        if (text.empty() || text.front() == '#') continue;
        if (text.front() == '[') {
            if (text == "[package]") {
                in_package = true;
                continue;
            }
            throw std::runtime_error("manifest line " + std::to_string(line_number) +
                                     ": unsupported section '" + text + "'");
        }
        if (!in_package) {
            throw std::runtime_error("manifest line " + std::to_string(line_number) +
                                     ": keys must appear inside [package]");
        }
        const auto equal = text.find('=');
        if (equal == std::string::npos) {
            throw std::runtime_error("manifest line " + std::to_string(line_number) + ": expected key = value");
        }
        const std::string key = trim(text.substr(0, equal));
        const std::string value = parse_quoted(text.substr(equal + 1), line_number);
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
    return out.str();
}

std::string project_manifest_hash(const ProjectManifest& manifest) {
    const std::uint64_t hash = fnv1a64(canonical_manifest_text(manifest));
    std::ostringstream out;
    out << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

void initialize_project(const std::filesystem::path& root, const std::string& name) {
    ProjectManifest manifest{name, "0.1.0", std::filesystem::path("src/main.emoji")};
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

    const auto lock_path = root / "emojineer.lock";
    if (std::filesystem::exists(lock_path)) {
        try {
            const std::string locked_hash = lock_hash_from_text(read_text(lock_path));
            if (locked_hash.empty()) {
                diagnostics.push_back({"emojineer.lock is missing manifest_hash"});
            } else if (locked_hash != project_manifest_hash(manifest)) {
                diagnostics.push_back({"emojineer.lock is stale; run 'emji lock'"});
            }
        } catch (const std::exception& error) {
            diagnostics.push_back({std::string("cannot read lockfile: ") + error.what()});
        }
    }

    return diagnostics;
}

void write_project_lock(const std::filesystem::path& root, const ProjectManifest& manifest) {
    validate_manifest(manifest);
    std::ostringstream out;
    out << "lock_version = 1\n"
        << "manifest_hash = \"" << project_manifest_hash(manifest) << "\"\n"
        << "package = \"" << manifest.name << "\"\n"
        << "version = \"" << manifest.version << "\"\n"
        << "entry = \"" << manifest.entry.generic_string() << "\"\n";
    write_text(root / "emojineer.lock", out.str());
}

} // namespace emojineer
