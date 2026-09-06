#include "emojineer/registry_discovery.hpp"

#include "emojineer/package_artifact.hpp"
#include "emojineer/registry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace emojineer {
namespace {

constexpr std::string_view discovery_magic = "EMJREGDISC1\n";
constexpr std::size_t max_discovery_bytes = 16ull * 1024ull * 1024ull;
constexpr std::size_t max_discovery_records = 250000;

bool valid_portable_name(std::string_view value) {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_';
    });
}

void validate_package_name(std::string_view value) {
    if (!valid_portable_name(value)) {
        throw std::runtime_error("discovery package name may contain only ASCII letters, digits, '-' and '_'");
    }
}

bool valid_sha256(std::string_view value) {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isdigit(c) || (c >= 'a' && c <= 'f');
    });
}

bool has_control(std::string_view text) {
    return std::any_of(text.begin(), text.end(), [](unsigned char c) {
        return c < 0x20 || c == 0x7f;
    });
}

void validate_entry(std::string_view text) {
    if (text.empty() || has_control(text) || text.front() == '/' ||
        text.find('\\') != std::string_view::npos) {
        throw std::runtime_error("discovery entry must be a portable relative path without control characters");
    }
    const std::filesystem::path path{std::string(text)};
    if (path.is_absolute() || path.generic_string() != text || path.extension() != ".emoji") {
        throw std::runtime_error("discovery entry must be a canonical relative .emoji path");
    }
    for (const auto& part : path) {
        if (part == "." || part == "..") {
            throw std::runtime_error("discovery entry may not contain traversal components");
        }
    }
}

void validate_discovery_record(const RegistryDiscoveryRecord& record) {
    validate_package_name(record.package_name);
    (void)parse_semantic_version(record.version);
    if (!valid_sha256(record.content_sha256) || !valid_sha256(record.artifact_sha256)) {
        throw std::runtime_error("discovery record contains malformed SHA-256 identity");
    }
    validate_entry(record.entry);
    std::string previous;
    for (const auto& dependency : record.dependencies) {
        validate_package_name(dependency);
        if (dependency == record.package_name) {
            throw std::runtime_error("discovery record may not declare itself as a dependency");
        }
        if (!previous.empty() && dependency <= previous) {
            throw std::runtime_error("discovery dependencies are not canonically ordered");
        }
        previous = dependency;
    }
}

std::vector<std::string> artifact_dependencies(const PackageArtifact& artifact) {
    constexpr std::string_view marker = "\n[dependencies]\n";
    const auto position = artifact.manifest.find(marker);
    if (position == std::string::npos) return {};

    std::vector<std::string> dependencies;
    std::string_view rest{artifact.manifest};
    rest.remove_prefix(position + marker.size());
    while (!rest.empty()) {
        const auto newline = rest.find('\n');
        if (newline == std::string_view::npos) {
            throw std::runtime_error("verified package manifest unexpectedly lacks a final LF");
        }
        const auto line = rest.substr(0, newline);
        rest.remove_prefix(newline + 1);
        if (line.empty()) {
            throw std::runtime_error("verified package manifest unexpectedly contains a blank dependency record");
        }
        const auto separator = line.find(" = \"");
        if (separator == std::string_view::npos) {
            throw std::runtime_error("verified package manifest dependency record is not canonical");
        }
        std::string name(line.substr(0, separator));
        validate_package_name(name);
        dependencies.push_back(std::move(name));
    }
    if (!std::is_sorted(dependencies.begin(), dependencies.end()) ||
        std::adjacent_find(dependencies.begin(), dependencies.end()) != dependencies.end()) {
        throw std::runtime_error("verified package manifest dependencies are not canonical");
    }
    return dependencies;
}

RegistryDiscoveryRecord record_from_artifact(const PackageArtifact& artifact,
                                              const RegistryVersionRecord& expected) {
    if (artifact.version != expected.version ||
        artifact.content_sha256 != expected.content_sha256 ||
        artifact.artifact_sha256 != expected.artifact_sha256) {
        throw std::runtime_error("registry discovery artifact identity does not match package index");
    }
    RegistryDiscoveryRecord record{
        artifact.name,
        artifact.version,
        artifact.content_sha256,
        artifact.artifact_sha256,
        artifact.entry,
        artifact_dependencies(artifact),
    };
    validate_discovery_record(record);
    return record;
}

std::string lower_ascii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
        return static_cast<char>(c);
    });
    return text;
}

std::vector<std::string> query_tokens(std::string_view query) {
    if (query.empty()) throw std::runtime_error("registry search query cannot be empty");
    if (has_control(query)) throw std::runtime_error("registry search query may not contain control characters");
    if (query == "*") return {};

    std::istringstream input(lower_ascii(std::string(query)));
    std::vector<std::string> tokens;
    std::string token;
    while (input >> token) tokens.push_back(token);
    if (tokens.empty()) throw std::runtime_error("registry search query cannot be blank");
    return tokens;
}

bool is_prerelease(const RegistryDiscoveryRecord& record) {
    return !parse_semantic_version(record.version).prerelease.empty();
}

std::vector<RegistryDiscoveryRecord> selected_records(const RegistryDiscoveryIndex& index,
                                                      bool include_prerelease) {
    std::map<std::string, RegistryDiscoveryRecord> selected;
    for (const auto& record : index.records) {
        validate_discovery_record(record);
        if (!include_prerelease && is_prerelease(record)) continue;
        auto found = selected.find(record.package_name);
        if (found == selected.end()) {
            selected.emplace(record.package_name, record);
            continue;
        }
        const auto candidate = parse_semantic_version(record.version);
        const auto current = parse_semantic_version(found->second.version);
        const int order = compare_semantic_versions(candidate, current);
        if (order > 0 || (order == 0 && record.version > found->second.version)) {
            found->second = record;
        }
    }

    std::vector<RegistryDiscoveryRecord> out;
    out.reserve(selected.size());
    for (const auto& [name, record] : selected) {
        (void)name;
        out.push_back(record);
    }
    return out;
}

bool record_matches(const RegistryDiscoveryRecord& record,
                    const std::vector<std::string>& tokens) {
    if (tokens.empty()) return true;
    std::string haystack = lower_ascii(record.package_name + "\n" + record.entry);
    for (const auto& dependency : record.dependencies) {
        haystack.push_back('\n');
        haystack += lower_ascii(dependency);
    }
    for (const auto& token : tokens) {
        if (haystack.find(token) == std::string::npos) return false;
    }
    return true;
}

std::string json_escape(std::string_view text) {
    std::ostringstream out;
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
                    static constexpr char hex[] = "0123456789abcdef";
                    out << "\\u00" << hex[(c >> 4) & 0xf] << hex[c & 0xf];
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

void render_record_json(std::ostringstream& out, const RegistryDiscoveryRecord& record) {
    out << "{\"package\":\"" << json_escape(record.package_name)
        << "\",\"version\":\"" << json_escape(record.version)
        << "\",\"stability\":\"" << (is_prerelease(record) ? "prerelease" : "stable")
        << "\",\"entry\":\"" << json_escape(record.entry)
        << "\",\"content_sha256\":\"" << record.content_sha256
        << "\",\"artifact_sha256\":\"" << record.artifact_sha256
        << "\",\"dependencies\":[";
    for (std::size_t i = 0; i < record.dependencies.size(); ++i) {
        if (i != 0) out << ',';
        out << '\"' << json_escape(record.dependencies[i]) << '\"';
    }
    out << "]}";
}

std::vector<RegistryDiscoveryRecord> package_versions(const RegistryDiscoveryIndex& index,
                                                       const std::string& package_name) {
    validate_package_name(package_name);
    std::vector<RegistryDiscoveryRecord> versions;
    for (const auto& record : index.records) {
        if (record.package_name == package_name) versions.push_back(record);
    }
    std::sort(versions.begin(), versions.end(), [](const auto& left, const auto& right) {
        const int order = compare_semantic_versions(parse_semantic_version(left.version),
                                                    parse_semantic_version(right.version));
        if (order != 0) return order > 0;
        return left.version > right.version;
    });
    return versions;
}

std::array<std::string, 6> parse_record_fields(std::string_view line) {
    if (!line.starts_with("record=")) {
        throw std::runtime_error("registry discovery index contains an unknown record");
    }
    line.remove_prefix(7);
    std::array<std::string, 6> fields;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i + 1 == fields.size()) {
            if (line.find('\t') != std::string_view::npos) {
                throw std::runtime_error("registry discovery record contains too many fields");
            }
            fields[i] = std::string(line);
            break;
        }
        const auto tab = line.find('\t');
        if (tab == std::string_view::npos) {
            throw std::runtime_error("registry discovery record contains too few fields");
        }
        fields[i] = std::string(line.substr(0, tab));
        line.remove_prefix(tab + 1);
    }
    return fields;
}

std::vector<std::string> parse_dependencies(std::string_view text) {
    if (text.empty()) return {};
    std::vector<std::string> dependencies;
    while (true) {
        const auto comma = text.find(',');
        const auto item = comma == std::string_view::npos ? text : text.substr(0, comma);
        if (item.empty()) throw std::runtime_error("registry discovery dependency list contains an empty name");
        dependencies.emplace_back(item);
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
    }
    return dependencies;
}

RegistryDiscoveryIndex load_file_registry_discovery(const RegistryEndpoint& endpoint,
                                                     const std::string& identity) {
    RegistryDiscoveryIndex index{identity, {}};
    const auto packages_root = endpoint.file_root / "v1/packages";
    if (!std::filesystem::exists(packages_root)) return index;

    std::vector<std::string> package_names;
    for (const auto& entry : std::filesystem::directory_iterator(packages_root)) {
        if (entry.path().extension() != ".index") continue;
        if (entry.is_symlink()) {
            throw std::runtime_error("registry discovery package index may not be a symlink");
        }
        if (!entry.is_regular_file()) continue;
        const auto package = entry.path().stem().string();
        validate_package_name(package);
        package_names.push_back(package);
        if (package_names.size() > max_discovery_records) {
            throw std::runtime_error("registry discovery package count exceeds format limit");
        }
    }
    std::sort(package_names.begin(), package_names.end());
    package_names.erase(std::unique(package_names.begin(), package_names.end()), package_names.end());

    for (const auto& package : package_names) {
        const auto package_index = load_registry_package_index(endpoint, package);
        for (const auto& version : package_index.versions) {
            const auto artifact_path = endpoint.file_root / "v1/artifacts/sha256" /
                (version.artifact_sha256 + ".emjpkg");
            if (std::filesystem::is_symlink(artifact_path)) {
                throw std::runtime_error("registry discovery artifact may not be a symlink");
            }
            const auto artifact = load_package_artifact(artifact_path);
            if (artifact.name != package) {
                throw std::runtime_error("registry discovery artifact package name does not match package index");
            }
            index.records.push_back(record_from_artifact(artifact, version));
            if (index.records.size() > max_discovery_records) {
                throw std::runtime_error("registry discovery record count exceeds format limit");
            }
        }
    }
    return index;
}

} // namespace

RegistryDiscoveryIndex parse_registry_discovery_index(std::string_view text) {
    if (text.size() > max_discovery_bytes) {
        throw std::runtime_error("registry discovery response exceeds format size limit");
    }
    if (!text.starts_with(discovery_magic)) {
        throw std::runtime_error("invalid registry discovery-index magic");
    }
    text.remove_prefix(discovery_magic.size());
    if (text.empty() || !text.ends_with('\n')) {
        throw std::runtime_error("registry discovery index must end with LF");
    }

    RegistryDiscoveryIndex index;
    std::istringstream input{std::string(text)};
    std::string line;
    bool have_registry = false;
    std::string previous_key;
    std::set<std::string> identities;
    while (std::getline(input, line)) {
        if (line.empty()) throw std::runtime_error("registry discovery index contains a blank record");
        if (line.ends_with('\r')) throw std::runtime_error("registry discovery index must use LF line endings");
        if (!have_registry) {
            if (!line.starts_with("registry=") || line.size() == 9) {
                throw std::runtime_error("registry discovery index is missing registry id");
            }
            index.registry_id = line.substr(9);
            if (!std::all_of(index.registry_id.begin(), index.registry_id.end(), [](unsigned char c) {
                    return std::isalnum(c) || c == '-' || c == '_' || c == '.';
                })) {
                throw std::runtime_error("registry discovery index contains invalid registry id");
            }
            have_registry = true;
            continue;
        }

        const auto fields = parse_record_fields(line);
        RegistryDiscoveryRecord record{
            fields[0], fields[1], fields[2], fields[3], fields[4], parse_dependencies(fields[5])};
        validate_discovery_record(record);
        const std::string key = record.package_name + "\t" + record.version;
        if (!previous_key.empty() && key <= previous_key) {
            throw std::runtime_error("registry discovery records are not in canonical order");
        }
        if (!identities.insert(key).second) {
            throw std::runtime_error("registry discovery index contains duplicate package version");
        }
        previous_key = key;
        index.records.push_back(std::move(record));
        if (index.records.size() > max_discovery_records) {
            throw std::runtime_error("registry discovery record count exceeds format limit");
        }
    }
    if (!have_registry) throw std::runtime_error("registry discovery index is incomplete");
    return index;
}

std::string render_registry_discovery_index(const RegistryDiscoveryIndex& raw_index) {
    if (raw_index.registry_id.empty() ||
        !std::all_of(raw_index.registry_id.begin(), raw_index.registry_id.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '-' || c == '_' || c == '.';
        })) {
        throw std::runtime_error("registry discovery index contains invalid registry id");
    }
    RegistryDiscoveryIndex index = raw_index;
    for (const auto& record : index.records) validate_discovery_record(record);
    std::sort(index.records.begin(), index.records.end(), [](const auto& left, const auto& right) {
        if (left.package_name != right.package_name) return left.package_name < right.package_name;
        return left.version < right.version;
    });
    for (std::size_t i = 1; i < index.records.size(); ++i) {
        if (index.records[i - 1].package_name == index.records[i].package_name &&
            index.records[i - 1].version == index.records[i].version) {
            throw std::runtime_error("registry discovery index contains duplicate package version");
        }
    }

    std::ostringstream out;
    out << discovery_magic << "registry=" << index.registry_id << '\n';
    for (const auto& record : index.records) {
        out << "record=" << record.package_name << '\t'
            << record.version << '\t'
            << record.content_sha256 << '\t'
            << record.artifact_sha256 << '\t'
            << record.entry << '\t';
        for (std::size_t i = 0; i < record.dependencies.size(); ++i) {
            if (i != 0) out << ',';
            out << record.dependencies[i];
        }
        out << '\n';
    }
    const auto rendered = out.str();
    if (rendered.size() > max_discovery_bytes) {
        throw std::runtime_error("registry discovery response exceeds format size limit");
    }
    return rendered;
}

RegistryDiscoveryIndex load_registry_discovery(const RegistryEndpoint& endpoint) {
    const auto identity = registry_identity(endpoint);
    if (endpoint.kind == RegistryTransportKind::File) {
        auto index = load_file_registry_discovery(endpoint, identity);
        return parse_registry_discovery_index(render_registry_discovery_index(index));
    }
    auto index = parse_registry_discovery_index(
        read_registry_resource(endpoint, "v1/discovery.index", max_discovery_bytes));
    if (index.registry_id != identity) {
        throw std::runtime_error("registry discovery index identity does not match registry descriptor");
    }
    return index;
}

std::vector<RegistryDiscoveryRecord> search_registry_discovery(
    const RegistryDiscoveryIndex& index,
    std::string_view query,
    bool include_prerelease) {
    const auto tokens = query_tokens(query);
    auto candidates = selected_records(index, include_prerelease);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const auto& record) {
        return !record_matches(record, tokens);
    }), candidates.end());
    return candidates;
}

std::optional<RegistryDiscoveryRecord> select_registry_discovery_package(
    const RegistryDiscoveryIndex& index,
    const std::string& package_name,
    bool include_prerelease) {
    validate_package_name(package_name);
    for (const auto& record : selected_records(index, include_prerelease)) {
        if (record.package_name == package_name) return record;
    }
    return std::nullopt;
}

std::vector<RegistryDiscoveryRecord> reverse_registry_dependencies(
    const RegistryDiscoveryIndex& index,
    const std::string& package_name,
    bool include_prerelease) {
    validate_package_name(package_name);
    std::vector<RegistryDiscoveryRecord> dependents;
    for (const auto& record : selected_records(index, include_prerelease)) {
        if (std::binary_search(record.dependencies.begin(), record.dependencies.end(), package_name)) {
            dependents.push_back(record);
        }
    }
    return dependents;
}

std::string render_registry_search(const RegistryDiscoveryIndex& index,
                                   std::string_view query,
                                   bool include_prerelease) {
    const auto results = search_registry_discovery(index, query, include_prerelease);
    std::ostringstream out;
    out << "registry: " << index.registry_id << '\n'
        << "query: " << query << '\n'
        << "releases: " << (include_prerelease ? "stable+prerelease" : "stable") << '\n';
    if (results.empty()) {
        out << "  (no matching packages)\n";
        return out.str();
    }
    for (const auto& record : results) {
        out << "  " << record.package_name << "  " << record.version
            << "  " << (is_prerelease(record) ? "prerelease" : "stable")
            << "  entry=" << record.entry;
        if (!record.dependencies.empty()) {
            out << "  deps=";
            for (std::size_t i = 0; i < record.dependencies.size(); ++i) {
                if (i != 0) out << ',';
                out << record.dependencies[i];
            }
        }
        out << '\n';
    }
    return out.str();
}

std::string render_registry_search_json(const RegistryDiscoveryIndex& index,
                                        std::string_view query,
                                        bool include_prerelease) {
    const auto results = search_registry_discovery(index, query, include_prerelease);
    std::ostringstream out;
    out << "{\"schema\":\"emojineer.registry-search.v1\",\"registry_id\":\""
        << json_escape(index.registry_id) << "\",\"query\":\"" << json_escape(query)
        << "\",\"include_prerelease\":" << (include_prerelease ? "true" : "false")
        << ",\"packages\":[";
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (i != 0) out << ',';
        render_record_json(out, results[i]);
    }
    out << "]}\n";
    return out.str();
}

std::string render_registry_package_info(const RegistryDiscoveryIndex& index,
                                         const std::string& package_name,
                                         bool include_prerelease) {
    const auto selected = select_registry_discovery_package(index, package_name, include_prerelease);
    const auto versions = package_versions(index, package_name);
    std::ostringstream out;
    out << "registry: " << index.registry_id << '\n'
        << "package: " << package_name << '\n'
        << "releases: " << (include_prerelease ? "stable+prerelease" : "stable") << '\n';
    if (!selected) {
        out << "selected: (none)\n";
    } else {
        out << "selected: " << selected->version << '\n'
            << "stability: " << (is_prerelease(*selected) ? "prerelease" : "stable") << '\n'
            << "entry: " << selected->entry << '\n'
            << "content-sha256: " << selected->content_sha256 << '\n'
            << "artifact-sha256: " << selected->artifact_sha256 << '\n'
            << "dependencies:";
        if (selected->dependencies.empty()) out << " (none)\n";
        else {
            out << '\n';
            for (const auto& dependency : selected->dependencies) out << "  " << dependency << '\n';
        }
    }
    out << "versions:";
    if (versions.empty()) out << " (none)\n";
    else {
        out << '\n';
        for (const auto& version : versions) {
            out << "  " << version.version << "  " << (is_prerelease(version) ? "prerelease" : "stable") << '\n';
        }
    }
    return out.str();
}

std::string render_registry_package_info_json(const RegistryDiscoveryIndex& index,
                                              const std::string& package_name,
                                              bool include_prerelease) {
    const auto selected = select_registry_discovery_package(index, package_name, include_prerelease);
    const auto versions = package_versions(index, package_name);
    std::ostringstream out;
    out << "{\"schema\":\"emojineer.registry-package-info.v1\",\"registry_id\":\""
        << json_escape(index.registry_id) << "\",\"package\":\"" << json_escape(package_name)
        << "\",\"include_prerelease\":" << (include_prerelease ? "true" : "false")
        << ",\"selected\":";
    if (selected) render_record_json(out, *selected);
    else out << "null";
    out << ",\"versions\":[";
    for (std::size_t i = 0; i < versions.size(); ++i) {
        if (i != 0) out << ',';
        out << "{\"version\":\"" << json_escape(versions[i].version)
            << "\",\"stability\":\"" << (is_prerelease(versions[i]) ? "prerelease" : "stable") << "\"}";
    }
    out << "]}\n";
    return out.str();
}

std::string render_registry_dependents(const RegistryDiscoveryIndex& index,
                                       const std::string& package_name,
                                       bool include_prerelease) {
    const auto results = reverse_registry_dependencies(index, package_name, include_prerelease);
    std::ostringstream out;
    out << "registry: " << index.registry_id << '\n'
        << "dependency: " << package_name << '\n'
        << "releases: " << (include_prerelease ? "stable+prerelease" : "stable") << '\n';
    if (results.empty()) {
        out << "  (no direct dependents)\n";
        return out.str();
    }
    for (const auto& record : results) {
        out << "  " << record.package_name << "  " << record.version
            << "  " << (is_prerelease(record) ? "prerelease" : "stable") << '\n';
    }
    return out.str();
}

std::string render_registry_dependents_json(const RegistryDiscoveryIndex& index,
                                            const std::string& package_name,
                                            bool include_prerelease) {
    const auto results = reverse_registry_dependencies(index, package_name, include_prerelease);
    std::ostringstream out;
    out << "{\"schema\":\"emojineer.registry-dependents.v1\",\"registry_id\":\""
        << json_escape(index.registry_id) << "\",\"dependency\":\"" << json_escape(package_name)
        << "\",\"include_prerelease\":" << (include_prerelease ? "true" : "false")
        << ",\"packages\":[";
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (i != 0) out << ',';
        render_record_json(out, results[i]);
    }
    out << "]}\n";
    return out.str();
}

} // namespace emojineer
