#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace emojineer {

/// Coarse host authorities that may be granted to one VM execution.
///
/// Capability values are stable serialized bit positions in EMJBC v8. Adding a
/// new value therefore requires bytecode/verifier compatibility consideration.
enum class Capability : std::uint32_t {
    Filesystem = 1u << 0,
    Network = 1u << 1,
    Process = 1u << 2,
    Clock = 1u << 3,
    Random = 1u << 4,
    Host = 1u << 5,
};

/// Bitwise union of Capability values used by bytecode and execution policy.
using CapabilityMask = std::uint32_t;

/// Convert one capability into its stable serialized mask bit.
constexpr CapabilityMask capability_mask(Capability capability) {
    return static_cast<CapabilityMask>(capability);
}

/// Return the mask containing every capability understood by this build.
constexpr CapabilityMask all_capabilities_mask() {
    return capability_mask(Capability::Filesystem) |
           capability_mask(Capability::Network) |
           capability_mask(Capability::Process) |
           capability_mask(Capability::Clock) |
           capability_mask(Capability::Random) |
           capability_mask(Capability::Host);
}

/// Host-authority execution regime applied by the production VM.
enum class ExecutionMode {
    /// Execute with exactly the explicitly supplied grants.
    Normal,
    /// Hard zero-native-host-authority mode; nonzero grants are invalid.
    Sandbox,
    /// Reproducible mode allowing only virtual clock and random facilities.
    Deterministic,
};

/// Complete authority and deterministic-state policy for one VM execution.
///
/// The default-constructed policy intentionally grants nothing. REPL, debugger,
/// source execution, and serialized-bytecode execution all pass this same type
/// into the production VM rather than maintaining separate authority models.
struct ExecutionPolicy {
    CapabilityMask grants{0};
    ExecutionMode mode{ExecutionMode::Normal};
    std::uint64_t deterministic_seed{0x454d4f4a494e4545ULL};
    std::int64_t deterministic_clock_ms{0};
};

/// Stable EMJBC v8 operand identities for built-in native host facilities.
///
/// Numeric values are serialized by HostCall and must not be reordered.
enum class NativeFacility : std::int32_t {
    FilesystemReadText = 0,
    NetworkGet = 1,
    ProcessRun = 2,
    ClockMillis = 3,
    RandomInt = 4,
    HostEnvironment = 5,
};

/// Parse a CLI-facing capability name or supported short alias.
std::optional<Capability> parse_capability(std::string_view name);

/// Return the canonical lowercase name used in diagnostics and reports.
std::string capability_name(Capability capability);

/// Expand a validated mask into capabilities in canonical reporting order.
std::vector<Capability> capabilities_in_mask(CapabilityMask mask);

/// Render a mask deterministically, or "none" for an empty mask.
std::string capability_mask_string(CapabilityMask mask);

/// Reject mask bits not assigned to a capability understood by this build.
void validate_capability_mask(CapabilityMask mask);

/// Enforce cross-field execution-mode invariants before VM execution begins.
///
/// Sandbox requires zero grants. Deterministic mode permits only clock/random.
void validate_execution_policy(const ExecutionPolicy& policy);

/// Preflight a whole chunk's required authority against an execution policy.
///
/// Throws before instruction zero when any required capability is absent.
void require_execution_capabilities(CapabilityMask required, const ExecutionPolicy& policy);

/// Return the single capability required by a serialized native facility.
Capability native_facility_capability(NativeFacility facility);

/// Return the stable diagnostic name for a native facility.
std::string native_facility_name(NativeFacility facility);

/// Return the reserved emoji identifier used by source code for the facility.
std::string native_facility_glyph(NativeFacility facility);

/// Return the source-level argument count required by the facility.
std::size_t native_facility_arity(NativeFacility facility);

/// Recognize a compiler/linker identifier as one of the reserved facilities.
std::optional<NativeFacility> native_facility_from_identifier(std::string_view canonical_identifier);

/// Decode and bounds-check an EMJBC HostCall operand.
std::optional<NativeFacility> native_facility_from_operand(std::int32_t operand);

} // namespace emojineer
