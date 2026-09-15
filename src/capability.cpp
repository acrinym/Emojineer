#include "emojineer/capability.hpp"

#include "emojineer/unicode.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace emojineer {
namespace {

constexpr std::array<Capability, 6> CapabilityOrder{
    Capability::Filesystem,
    Capability::Network,
    Capability::Process,
    Capability::Clock,
    Capability::Random,
    Capability::Host,
};

constexpr std::array<NativeFacility, 6> FacilityOrder{
    NativeFacility::FilesystemReadText,
    NativeFacility::NetworkGet,
    NativeFacility::ProcessRun,
    NativeFacility::ClockMillis,
    NativeFacility::RandomInt,
    NativeFacility::HostEnvironment,
};

std::string lower_ascii(std::string_view input) {
    std::string result(input);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

} // namespace

std::optional<Capability> parse_capability(std::string_view name) {
    const auto normalized = lower_ascii(name);
    if (normalized == "filesystem" || normalized == "fs") return Capability::Filesystem;
    if (normalized == "network" || normalized == "net") return Capability::Network;
    if (normalized == "process" || normalized == "proc") return Capability::Process;
    if (normalized == "clock" || normalized == "time") return Capability::Clock;
    if (normalized == "random" || normalized == "rng") return Capability::Random;
    if (normalized == "host" || normalized == "environment" || normalized == "env") return Capability::Host;
    return std::nullopt;
}

std::string capability_name(Capability capability) {
    switch (capability) {
        case Capability::Filesystem: return "filesystem";
        case Capability::Network: return "network";
        case Capability::Process: return "process";
        case Capability::Clock: return "clock";
        case Capability::Random: return "random";
        case Capability::Host: return "host";
    }
    throw std::runtime_error("unknown capability");
}

std::vector<Capability> capabilities_in_mask(CapabilityMask mask) {
    validate_capability_mask(mask);
    std::vector<Capability> result;
    for (const auto capability : CapabilityOrder) {
        if ((mask & capability_mask(capability)) != 0) result.push_back(capability);
    }
    return result;
}

std::string capability_mask_string(CapabilityMask mask) {
    validate_capability_mask(mask);
    if (mask == 0) return "none";
    std::string result;
    for (const auto capability : CapabilityOrder) {
        if ((mask & capability_mask(capability)) == 0) continue;
        if (!result.empty()) result += ", ";
        result += capability_name(capability);
    }
    return result;
}

void validate_capability_mask(CapabilityMask mask) {
    if ((mask & ~all_capabilities_mask()) != 0)
        throw std::runtime_error("capability mask contains unknown bits");
}

void validate_execution_policy(const ExecutionPolicy& policy) {
    validate_capability_mask(policy.grants);
    if (policy.mode == ExecutionMode::Sandbox && policy.grants != 0)
        throw std::runtime_error("sandbox mode cannot grant host capabilities");
    if (policy.mode == ExecutionMode::Deterministic) {
        constexpr CapabilityMask deterministic =
            capability_mask(Capability::Clock) | capability_mask(Capability::Random);
        if ((policy.grants & ~deterministic) != 0)
            throw std::runtime_error("deterministic mode permits only clock and random capabilities");
    }
}

void require_execution_capabilities(CapabilityMask required, const ExecutionPolicy& policy) {
    validate_capability_mask(required);
    validate_execution_policy(policy);
    const CapabilityMask missing = required & ~policy.grants;
    if (missing != 0) {
        throw std::runtime_error(
            "execution denied: missing capability grant(s): " + capability_mask_string(missing));
    }
}

Capability native_facility_capability(NativeFacility facility) {
    switch (facility) {
        case NativeFacility::FilesystemReadText: return Capability::Filesystem;
        case NativeFacility::NetworkGet: return Capability::Network;
        case NativeFacility::ProcessRun: return Capability::Process;
        case NativeFacility::ClockMillis: return Capability::Clock;
        case NativeFacility::RandomInt: return Capability::Random;
        case NativeFacility::HostEnvironment: return Capability::Host;
    }
    throw std::runtime_error("unknown native facility");
}

std::string native_facility_name(NativeFacility facility) {
    switch (facility) {
        case NativeFacility::FilesystemReadText: return "filesystem.read-text";
        case NativeFacility::NetworkGet: return "network.get";
        case NativeFacility::ProcessRun: return "process.run";
        case NativeFacility::ClockMillis: return "clock.millis";
        case NativeFacility::RandomInt: return "random.int";
        case NativeFacility::HostEnvironment: return "host.environment";
    }
    throw std::runtime_error("unknown native facility");
}

std::string native_facility_glyph(NativeFacility facility) {
    switch (facility) {
        case NativeFacility::FilesystemReadText: return "🗂️";
        case NativeFacility::NetworkGet: return "🌐";
        case NativeFacility::ProcessRun: return "⚙️";
        case NativeFacility::ClockMillis: return "🕰️";
        case NativeFacility::RandomInt: return "🎲";
        case NativeFacility::HostEnvironment: return "🖥️";
    }
    throw std::runtime_error("unknown native facility");
}

std::size_t native_facility_arity(NativeFacility facility) {
    switch (facility) {
        case NativeFacility::FilesystemReadText: return 1;
        case NativeFacility::NetworkGet: return 1;
        case NativeFacility::ProcessRun: return 1;
        case NativeFacility::ClockMillis: return 0;
        case NativeFacility::RandomInt: return 1;
        case NativeFacility::HostEnvironment: return 1;
    }
    throw std::runtime_error("unknown native facility");
}

std::optional<NativeFacility> native_facility_from_identifier(std::string_view canonical_identifier) {
    for (const auto facility : FacilityOrder) {
        if (canonicalize_token(native_facility_glyph(facility)) == canonical_identifier) return facility;
    }
    return std::nullopt;
}

std::optional<NativeFacility> native_facility_from_operand(std::int32_t operand) {
    if (operand < static_cast<std::int32_t>(NativeFacility::FilesystemReadText) ||
        operand > static_cast<std::int32_t>(NativeFacility::HostEnvironment)) return std::nullopt;
    return static_cast<NativeFacility>(operand);
}

} // namespace emojineer
