#include "emojineer/package_artifact.hpp"

#include "emojineer/hash.hpp"
#include "emojineer/package.hpp"
#include "emojineer/project.hpp"
#include "emojineer/registry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace emojineer {
namespace {

constexpr std::string_view artifact_magic = "EMJPKG1\n";
constexpr std::uint64_t max_artifact_bytes = 128ull * 1024ull * 1024ull;
constexpr std::uint64_t max_field_bytes = 64ull * 1024ull * 1024ull;
constexpr std::uint64_t max_package_files = 100000ull;

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open '" + path.string() + "'");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& path, std::string_view data) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write '" + path.string() + "'");
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!output) throw std::runtime_error("failed while writing '" + path.string() + "'");
}

bool within(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto r = root.begin();
    auto p = path.begin();
    for (; r != root.end(); ++r, ++p) {
        if (p == path.end() || *r != *p) return false;
    }
    return true;
}

bool under_any(const std::filesystem::path& path,
               const std::vector<std::filesystem::path>& roots) {
    for (const auto& root : roots) {
        if (within(root, path)) return true;
    }
    return false;
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

std::string content_identity(std::string_view canonical_manifest,
                             const std::vector<PackageArtifactFile>& files) {
    std::string framed;
    append_field(framed, "EMOJINEER-PACKAGE-v1");
    append_field(framed, canonical_manifest);
    for (const auto& file : files) {
        append_field(framed, file.path);
        append_field(framed, file.source);
    }
    return sha256_hex(framed);
}

bool valid_hex_sha256(std::string_view value) {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isdigit(c) || (c >= 'a' && c <= 'f');
    });
}

void validate_name(std::string_view name) {
    if (name.empty()) throw std::runtime_error("package artifact name cannot be empty");
    for (unsigned char c : name) {
        if (!(std::isalnum(c) || c == '-' || c == '_')) {
            throw std::runtime_error("package artifact name contains a non-portable character");
        }
    }
}

void validate_portable_source_path(std::string_view text, const char* label) {
    if (text.empty()) throw std::runtime_error(std::string("package artifact ") + label + " cannot be empty");
    if (text.front() == '/' || text.find('\\') != std::string_view::npos ||
        text.find(':') != std::string_view::npos) {
        throw std::runtime_error(std::string("package artifact ") + label + " is not a portable relative path");
    }
    const std::filesystem::path path{std::string(text)};
    if (path.is_absolute() || path.generic_string() != text) {
        throw std::runtime_error(std::string("package artifact ") + label + " is not canonical");
    }
    for (const auto& component : path) {
        if (component == ".." || component == ".") {
            throw std::runtime_error(std::string("package artifact ") + label + " may not contain traversal components");
        }
    }
    if (path.extension() != ".emoji") {
        throw std::runtime_error(std::string("package artifact ") + label + " must be a .emoji path");
    }
}

void validate_dependency_path(std::string_view text, const std::string& name) {
    if (text.empty() || text.front() == '/' || text.find('\\') != std::string_view::npos ||
        text.find(':') != std::string_view::npos || text.find('"') != std::string_view::npos) {
        throw std::runtime_error("package artifact dependency '" + name + "' has a non-portable path");
    }
    const std::filesystem::path path{std::string(text)};
    if (path.is_absolute() || path.generic_string() != text) {
        throw std::runtime_error("package artifact dependency '" + name + "' path is not canonical");
    }
}

std::string metadata_prefix(const std::string& name,
                            const std::string& version,
                            const std::string& entry) {
    return "[package]\nname = \"" + name + "\"\nversion = \"" + version +
           "\"\nentry = \"" + entry + "\"\n";
}

void validate_canonical_manifest(const PackageArtifact& artifact) {
    const auto prefix = metadata_prefix(artifact.name, artifact.version, artifact.entry);
    if (!artifact.manifest.starts_with(prefix)) {
        throw std::runtime_error("package artifact metadata does not match its canonical manifest");
    }
    std::string_view rest{artifact.manifest};
    rest.remove_prefix(prefix.size());
    if (rest.empty()) return;
    constexpr std::string_view dependency_header = "\n[dependencies]\n";
    if (!rest.starts_with(dependency_header)) {
        throw std::runtime_error("package artifact manifest is not canonical");
    }
    rest.remove_prefix(dependency_header.size());
    if (rest.empty()) throw std::runtime_error("package artifact manifest has an empty dependencies section");

    std::string previous_name;
    std::set<std::string> names;
    while (!rest.empty()) {
        const auto newline = rest.find('\n');
        if (newline == std::string_view::npos) {
            throw std::runtime_error("package artifact manifest must end with a newline");
        }
        const auto line = rest.substr(0, newline);
        rest.remove_prefix(newline + 1);
        const auto separator = line.find(" = \"");
        if (separator == std::string_view::npos || line.size() < separator + 5 || line.back() != '"') {
            throw std::runtime_error("package artifact dependency manifest line is not canonical");
        }
        const std::string name(line.substr(0, separator));
        const std::string path(line.substr(separator + 4, line.size() - separator - 5));
        validate_name(name);
        validate_dependency_path(path, name);
        if (name == artifact.name) throw std::runtime_error("package artifact may not depend on itself");
        if (!names.insert(name).second || (!previous_name.empty() && name <= previous_name)) {
            throw std::runtime_error("package artifact dependency manifest is not canonically ordered");
        }
        previous_name = name;
    }
}

std::vector<PackageArtifactFile> collect_owned_sources(const std::filesystem::path& root,
                                                       const PackageGraph& graph) {
    std::vector<std::filesystem::path> nested_roots;
    for (const auto& package : graph.packages) {
        if (package.root != root && within(root, package.root)) nested_roots.push_back(package.root);
    }

    std::vector<PackageArtifactFile> files;
    std::filesystem::recursive_directory_iterator it(root), end;
    for (; it != end; ++it) {
        const auto path = it->path();
        if (it->is_directory()) {
            std::error_code error;
            const auto canonical = std::filesystem::canonical(path, error);
            if (!error && path != root && under_any(canonical, nested_roots)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file() || path.extension() != ".emoji") continue;
        const auto canonical = std::filesystem::canonical(path);
        if (!within(root, canonical)) {
            throw std::runtime_error("package contains a source symlink that escapes its root");
        }
        if (under_any(canonical, nested_roots)) continue;
        const auto relative = std::filesystem::relative(canonical, root).generic_string();
        validate_portable_source_path(relative, "source path");
        const auto source = read_file(canonical);
        files.push_back({relative, sha256_hex(source), source});
    }
    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        return left.path < right.path;
    });
    return files;
}

class Reader {
public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}

    std::uint64_t u64(const char* label) {
        if (remaining() < 8) throw std::runtime_error(std::string("truncated package artifact while reading ") + label);
        std::uint64_t value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes_[offset_++])) << shift;
        }
        return value;
    }

    std::string field(const char* label, std::uint64_t limit = max_field_bytes) {
        const auto length = u64(label);
        if (length > limit || length > remaining()) {
            throw std::runtime_error(std::string("invalid package artifact field length for ") + label);
        }
        const auto start = offset_;
        offset_ += static_cast<std::size_t>(length);
        return std::string(bytes_.substr(start, static_cast<std::size_t>(length)));
    }

    std::size_t remaining() const { return bytes_.size() - offset_; }

private:
    std::string_view bytes_;
    std::size_t offset_ = 0;
};

} // namespace

std::string build_package_artifact_bytes(const std::filesystem::path& raw_root) {
    const auto root = std::filesystem::canonical(raw_root);
    const auto manifest = load_project_manifest(root / "emojineer.toml");
    (void)parse_semantic_version(manifest.version);
    const auto graph = resolve_package_graph(root, manifest);
    const auto* resolved = graph.find(manifest.name);
    if (!resolved) throw std::runtime_error("package graph is missing artifact root package");

    auto files = collect_owned_sources(root, graph);
    const auto entry = manifest.entry.generic_string();
    if (std::none_of(files.begin(), files.end(), [&](const auto& file) { return file.path == entry; })) {
        throw std::runtime_error("package artifact entry source is not package-owned or does not exist: " + entry);
    }
    const auto canonical_manifest = canonical_manifest_text(manifest);
    const auto computed_content = content_identity(canonical_manifest, files);
    if (computed_content != resolved->content_sha256) {
        throw std::runtime_error("package artifact content identity diverges from PackageGraph");
    }

    std::string bytes(artifact_magic);
    append_field(bytes, manifest.name);
    append_field(bytes, manifest.version);
    append_field(bytes, entry);
    append_field(bytes, computed_content);
    append_field(bytes, canonical_manifest);
    append_u64(bytes, static_cast<std::uint64_t>(files.size()));
    for (const auto& file : files) {
        append_field(bytes, file.path);
        append_field(bytes, file.sha256);
        append_field(bytes, file.source);
    }
    if (bytes.size() > max_artifact_bytes) {
        throw std::runtime_error("package artifact exceeds 128 MiB format limit");
    }
    return bytes;
}

PackageArtifact parse_package_artifact(std::string_view bytes) {
    if (bytes.size() > max_artifact_bytes) {
        throw std::runtime_error("package artifact exceeds 128 MiB format limit");
    }
    if (!bytes.starts_with(artifact_magic)) throw std::runtime_error("invalid package artifact magic");

    Reader reader(bytes.substr(artifact_magic.size()));
    PackageArtifact artifact;
    artifact.name = reader.field("package name", 1024);
    artifact.version = reader.field("package version", 1024);
    artifact.entry = reader.field("package entry", 64 * 1024);
    artifact.content_sha256 = reader.field("content SHA-256", 64);
    artifact.manifest = reader.field("canonical manifest", 4 * 1024 * 1024);
    const auto file_count = reader.u64("file count");
    if (file_count > max_package_files) throw std::runtime_error("package artifact file count exceeds format limit");

    validate_name(artifact.name);
    (void)parse_semantic_version(artifact.version);
    validate_portable_source_path(artifact.entry, "entry");
    if (!valid_hex_sha256(artifact.content_sha256)) {
        throw std::runtime_error("package artifact content SHA-256 is malformed");
    }
    validate_canonical_manifest(artifact);

    artifact.files.reserve(static_cast<std::size_t>(file_count));
    std::string previous_path;
    bool have_entry = false;
    for (std::uint64_t i = 0; i < file_count; ++i) {
        PackageArtifactFile file;
        file.path = reader.field("source path", 64 * 1024);
        file.sha256 = reader.field("source SHA-256", 64);
        file.source = reader.field("source contents");
        validate_portable_source_path(file.path, "source path");
        if (!previous_path.empty() && file.path <= previous_path) {
            throw std::runtime_error("package artifact source paths are not in canonical order");
        }
        previous_path = file.path;
        if (!valid_hex_sha256(file.sha256) || sha256_hex(file.source) != file.sha256) {
            throw std::runtime_error("package artifact source checksum mismatch for '" + file.path + "'");
        }
        if (file.path == artifact.entry) have_entry = true;
        artifact.files.push_back(std::move(file));
    }
    if (!have_entry) throw std::runtime_error("package artifact does not contain its declared entry source");
    if (reader.remaining() != 0) throw std::runtime_error("package artifact contains trailing bytes");

    if (content_identity(artifact.manifest, artifact.files) != artifact.content_sha256) {
        throw std::runtime_error("package artifact content identity mismatch");
    }
    artifact.artifact_sha256 = sha256_hex(bytes);
    return artifact;
}

PackageArtifact load_package_artifact(const std::filesystem::path& artifact_path) {
    return parse_package_artifact(read_file(artifact_path));
}

void write_package_artifact(const std::filesystem::path& package_root,
                            const std::filesystem::path& artifact_path) {
    if (artifact_path.extension() != ".emjpkg") {
        throw std::runtime_error("package artifact output must use the .emjpkg extension");
    }
    const auto bytes = build_package_artifact_bytes(package_root);
    (void)parse_package_artifact(bytes);
    write_file(artifact_path, bytes);
}

std::string default_package_artifact_filename(const std::string& name,
                                              const std::string& version) {
    validate_name(name);
    (void)parse_semantic_version(version);
    return name + "-" + version + ".emjpkg";
}

std::filesystem::path package_cache_path(const std::filesystem::path& cache_root,
                                         const PackageArtifact& artifact) {
    validate_name(artifact.name);
    (void)parse_semantic_version(artifact.version);
    if (!valid_hex_sha256(artifact.artifact_sha256)) {
        throw std::runtime_error("package artifact SHA-256 is malformed");
    }
    return cache_root / artifact.name / artifact.version / (artifact.artifact_sha256 + ".emjpkg");
}

std::string render_package_artifact_summary(const PackageArtifact& artifact) {
    std::ostringstream out;
    out << "name: " << artifact.name << '\n'
        << "version: " << artifact.version << '\n'
        << "entry: " << artifact.entry << '\n'
        << "files: " << artifact.files.size() << '\n'
        << "content-sha256: " << artifact.content_sha256 << '\n'
        << "artifact-sha256: " << artifact.artifact_sha256 << '\n';
    return out.str();
}

} // namespace emojineer
