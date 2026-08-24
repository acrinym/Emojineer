#include "emojineer/registry.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace emojineer {
namespace {

std::uint64_t parse_number(std::string_view text, const char* label) {
    if (text.empty()) throw std::runtime_error(std::string("semantic version ") + label + " is empty");
    std::uint64_t value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        throw std::runtime_error(std::string("semantic version ") + label + " must be decimal digits");
    }
    return value;
}

bool valid_identifier_list(std::string_view text) {
    if (text.empty()) return false;
    bool component_has_char = false;
    for (unsigned char c : text) {
        if (c == '.') {
            if (!component_has_char) return false;
            component_has_char = false;
            continue;
        }
        if (!(std::isalnum(c) || c == '-')) return false;
        component_has_char = true;
    }
    return component_has_char;
}

bool all_digits(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
}

std::vector<std::string_view> split_identifiers(std::string_view text) {
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto dot = text.find('.', start);
        if (dot == std::string_view::npos) {
            result.push_back(text.substr(start));
            break;
        }
        result.push_back(text.substr(start, dot - start));
        start = dot + 1;
    }
    return result;
}

int compare_prerelease(std::string_view left, std::string_view right) {
    if (left.empty() && right.empty()) return 0;
    if (left.empty()) return 1;
    if (right.empty()) return -1;

    const auto lparts = split_identifiers(left);
    const auto rparts = split_identifiers(right);
    const auto count = std::min(lparts.size(), rparts.size());
    for (std::size_t i = 0; i < count; ++i) {
        const bool ln = all_digits(lparts[i]);
        const bool rn = all_digits(rparts[i]);
        if (ln && rn) {
            const auto lv = parse_number(lparts[i], "prerelease identifier");
            const auto rv = parse_number(rparts[i], "prerelease identifier");
            if (lv < rv) return -1;
            if (lv > rv) return 1;
            continue;
        }
        if (ln != rn) return ln ? -1 : 1;
        if (lparts[i] < rparts[i]) return -1;
        if (lparts[i] > rparts[i]) return 1;
    }
    if (lparts.size() < rparts.size()) return -1;
    if (lparts.size() > rparts.size()) return 1;
    return 0;
}

bool same_core(const SemanticVersion& left, const SemanticVersion& right) {
    return left.major == right.major && left.minor == right.minor && left.patch == right.patch;
}

bool below_caret_upper(const SemanticVersion& base, const SemanticVersion& candidate) {
    if (base.major > 0) return candidate.major == base.major;
    if (base.minor > 0) return candidate.major == 0 && candidate.minor == base.minor;
    return candidate.major == 0 && candidate.minor == 0 && candidate.patch == base.patch;
}

bool below_tilde_upper(const SemanticVersion& base, const SemanticVersion& candidate) {
    return candidate.major == base.major && candidate.minor == base.minor;
}

} // namespace

SemanticVersion parse_semantic_version(std::string_view text) {
    if (text.empty()) throw std::runtime_error("semantic version cannot be empty");

    SemanticVersion version;
    version.text = std::string(text);

    std::string_view core = text;
    const auto plus = core.find('+');
    if (plus != std::string_view::npos) {
        version.build = std::string(core.substr(plus + 1));
        if (!valid_identifier_list(version.build)) {
            throw std::runtime_error("semantic version build metadata is invalid");
        }
        core = core.substr(0, plus);
    }

    const auto dash = core.find('-');
    if (dash != std::string_view::npos) {
        version.prerelease = std::string(core.substr(dash + 1));
        if (!valid_identifier_list(version.prerelease)) {
            throw std::runtime_error("semantic version prerelease is invalid");
        }
        for (const auto part : split_identifiers(version.prerelease)) {
            if (all_digits(part) && part.size() > 1 && part.front() == '0') {
                throw std::runtime_error("semantic version numeric prerelease identifiers may not have leading zeroes");
            }
        }
        core = core.substr(0, dash);
    }

    const auto first = core.find('.');
    const auto second = first == std::string_view::npos ? std::string_view::npos : core.find('.', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos || core.find('.', second + 1) != std::string_view::npos) {
        throw std::runtime_error("semantic version must have major.minor.patch");
    }
    const auto major_text = core.substr(0, first);
    const auto minor_text = core.substr(first + 1, second - first - 1);
    const auto patch_text = core.substr(second + 1);
    for (const auto part : {major_text, minor_text, patch_text}) {
        if (part.size() > 1 && part.front() == '0') {
            throw std::runtime_error("semantic version numeric components may not have leading zeroes");
        }
    }
    version.major = parse_number(major_text, "major");
    version.minor = parse_number(minor_text, "minor");
    version.patch = parse_number(patch_text, "patch");
    return version;
}

int compare_semantic_versions(const SemanticVersion& left, const SemanticVersion& right) {
    if (left.major != right.major) return left.major < right.major ? -1 : 1;
    if (left.minor != right.minor) return left.minor < right.minor ? -1 : 1;
    if (left.patch != right.patch) return left.patch < right.patch ? -1 : 1;
    return compare_prerelease(left.prerelease, right.prerelease);
}

VersionRequirement parse_version_requirement(std::string_view text) {
    VersionRequirement requirement;
    requirement.text = std::string(text);
    if (text == "*") {
        requirement.kind = VersionRequirementKind::Any;
        return requirement;
    }
    if (text.empty()) throw std::runtime_error("version requirement cannot be empty");
    if (text.front() == '^') {
        requirement.kind = VersionRequirementKind::Caret;
        requirement.base = parse_semantic_version(text.substr(1));
        return requirement;
    }
    if (text.front() == '~') {
        requirement.kind = VersionRequirementKind::Tilde;
        requirement.base = parse_semantic_version(text.substr(1));
        return requirement;
    }
    requirement.kind = VersionRequirementKind::Exact;
    requirement.base = parse_semantic_version(text);
    return requirement;
}

bool version_requirement_matches(const VersionRequirement& requirement,
                                 const SemanticVersion& candidate) {
    if (requirement.kind == VersionRequirementKind::Any) {
        return candidate.prerelease.empty();
    }
    const int lower = compare_semantic_versions(candidate, requirement.base);
    if (requirement.kind == VersionRequirementKind::Exact) {
        return lower == 0;
    }

    if (!candidate.prerelease.empty()) {
        if (requirement.base.prerelease.empty() || !same_core(candidate, requirement.base)) return false;
    }
    if (lower < 0) return false;
    if (requirement.kind == VersionRequirementKind::Caret) {
        return below_caret_upper(requirement.base, candidate);
    }
    return below_tilde_upper(requirement.base, candidate);
}

std::optional<std::string> select_highest_matching_version(
    const std::vector<std::string>& available_versions,
    std::string_view requirement_text) {
    const auto requirement = parse_version_requirement(requirement_text);
    std::optional<SemanticVersion> selected;
    for (const auto& text : available_versions) {
        const auto candidate = parse_semantic_version(text);
        if (!version_requirement_matches(requirement, candidate)) continue;
        if (!selected) {
            selected = candidate;
            continue;
        }
        const int precedence = compare_semantic_versions(candidate, *selected);
        if (precedence > 0 || (precedence == 0 && candidate.text > selected->text)) {
            selected = candidate;
        }
    }
    if (!selected) return std::nullopt;
    return selected->text;
}

} // namespace emojineer
