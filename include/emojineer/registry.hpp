#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace emojineer {

struct SemanticVersion {
    std::uint64_t major = 0;
    std::uint64_t minor = 0;
    std::uint64_t patch = 0;
    std::string prerelease;
    std::string build;
    std::string text;
};

enum class VersionRequirementKind {
    Any,
    Exact,
    Caret,
    Tilde,
};

struct VersionRequirement {
    VersionRequirementKind kind = VersionRequirementKind::Any;
    SemanticVersion base;
    std::string text;
};

SemanticVersion parse_semantic_version(std::string_view text);
int compare_semantic_versions(const SemanticVersion& left, const SemanticVersion& right);
VersionRequirement parse_version_requirement(std::string_view text);
bool version_requirement_matches(const VersionRequirement& requirement,
                                 const SemanticVersion& candidate);
std::optional<std::string> select_highest_matching_version(
    const std::vector<std::string>& available_versions,
    std::string_view requirement);

} // namespace emojineer
