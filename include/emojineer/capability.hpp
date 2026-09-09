#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace emojineer {

enum class Capability : std::uint32_t {
    Filesystem = 1u << 0,
    Network = 1u << 1,
    Process = 1u << 2,
    Clock = 1u << 3,
    Random = 1u << 4,
    Host = 1u << 5,
};

using CapabilityMask = std::uint32_t;

constexpr CapabilityMask capability_mask(Capability capability) {
    return static_cast<CapabilityMask>(capability);
}

constexpr CapabilityMask all_capabilities_mask() {
    return capability_mask(Capability::Filesystem) |
           capability_mask(Capability::Network) |
           capability_mask(Capability::Process) |
           capability_mask(Capability::Clock) |
           capability_mask(Capability::Random) |
           capability_mask(Capability::Host);
}

enum class ExecutionMode {
    Normal,
    Sandbox,
    Deterministic,
};

struct ExecutionPolicy {
    CapabilityMask grants{0};
    ExecutionMode mode{ExecutionMode::Normal};
    std::uint64_t deterministic_seed{0x454d4f4a494e4545ULL};
    std::int64_t deterministic_clock_ms{0};
};

enum class NativeFacility : std::int32_t {
    FilesystemReadText = 0,
    NetworkGet = 1,
    ProcessRun = 2,
    ClockMillis = 3,
    RandomInt = 4,
    HostEnvironment = 5,
};

std::optional<Capability> parse_capability(std::string_view name);
std::string capability_name(Capability capability);
std::vector<Capability> capabilities_in_mask(CapabilityMask mask);
std::string capability_mask_string(CapabilityMask mask);
void validate_capability_mask(CapabilityMask mask);
void validate_execution_policy(const ExecutionPolicy& policy);
void require_execution_capabilities(CapabilityMask required, const ExecutionPolicy& policy);

Capability native_facility_capability(NativeFacility facility);
std::string native_facility_name(NativeFacility facility);
std::string native_facility_glyph(NativeFacility facility);
std::size_t native_facility_arity(NativeFacility facility);
std::optional<NativeFacility> native_facility_from_identifier(std::string_view canonical_identifier);
std::optional<NativeFacility> native_facility_from_operand(std::int32_t operand);

} // namespace emojineer
