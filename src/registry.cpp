#include "emojineer/registry.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace emojineer {
namespace {

void validate_numeric_identifier(std::string_view text, const char* label) {
    if (text.empty()) throw std::runtime_error(std::string("semantic version ") + label + " is empty");
    for (unsigned char c : text) {
        if (!std::isdigit(c)) {
            throw std::runtime_error(std::string("semantic version ") + label + " must be decimal digits");
        }
    }
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

// Compare two numeric identifier strings as numbers without overflow.
// According to SemVer: "When compared, numeric identifiers always have lower precedence
// than non-numeric identifiers. Numeric identifiers always compare as integers with
// infinite precision."
//
// This implements numeric comparison: longer length means larger value (ignoring leading zeros).
// If equal length, use lexicographic comparison.
// Examples:
//   "2" < "10" because 2 < 10 numerically (1 digit < 2 digits)
//   "10" < "100" because 10 < 100 numerically (2 digits < 3 digits)
//   "2" == "2" (same value)
//   "2" > "02" (if "02" were allowed - it's not in valid SemVer)
int compare_numeric_identifiers(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        // Longer string = larger number (assuming no leading zeros, which is validated elsewhere)
        return left.size() < right.size() ? -1 : 1;
    }
    if (left == right) return 0;
    // Same length: lexicographic compare gives numeric compare
    return left < right ? -1 : 1;
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
            // Use overflow-free string-based numeric comparison
            const int cmp = compare_numeric_identifiers(lparts[i], rparts[i]);
            if (cmp != 0) return cmp;
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
    // For caret ranges: ^1.2.3 allows 1.x.x but not 2.0.0
    // Special handling for base major = 0 (^0.x.y has different semantics)
    const int major_cmp = compare_numeric_identifiers(base.major, "0");
    if (major_cmp > 0) {
        // base.major > 0: allow anything with same major
        return candidate.major == base.major;
    }
    // base.major == 0
    const int minor_cmp = compare_numeric_identifiers(base.minor, "0");
    if (minor_cmp > 0) {
        // base.major == 0, base.minor > 0: allow 0.x.y where x matches
        return candidate.major == "0" && candidate.minor == base.minor;
    }
    // base.major == 0, base.minor == 0: allow 0.0.x where patch matches
    return candidate.major == "0" && candidate.minor == "0" && candidate.patch == base.patch;
}

bool below_tilde_upper(const SemanticVersion& base, const SemanticVersion& candidate) {
    // For tilde ranges: ~1.2.3 allows 1.2.x but not 1.3.0
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
    validate_numeric_identifier(major_text, "major");
    validate_numeric_identifier(minor_text, "minor");
    validate_numeric_identifier(patch_text, "patch");
    version.major = std::string(major_text);
    version.minor = std::string(minor_text);
    version.patch = std::string(patch_text);
    return version;
}

int compare_semantic_versions(const SemanticVersion& left, const SemanticVersion& right) {
    const int major_cmp = compare_numeric_identifiers(left.major, right.major);
    if (major_cmp != 0) return major_cmp;
    const int minor_cmp = compare_numeric_identifiers(left.minor, right.minor);
    if (minor_cmp != 0) return minor_cmp;
    const int patch_cmp = compare_numeric_identifiers(left.patch, right.patch);
    if (patch_cmp != 0) return patch_cmp;
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
        return candidate.text == requirement.base.text;
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
