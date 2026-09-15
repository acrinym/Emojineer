#pragma once
#include "emojineer/bytecode.hpp"
#include "emojineer/capability.hpp"
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
namespace emojineer {
using InteropBytes = std::vector<std::uint8_t>;
using InteropAdapter = std::function<InteropBytes(std::span<const std::uint8_t>)>;
struct InteropAdapterBinding {
    CapabilityMask required_capabilities{0};
    bool deterministic{false};
    InteropAdapter invoke;
};
class InteropRegistry {
public:
    void bind(std::string external_name, CapabilityMask required_capabilities,
              bool deterministic, InteropAdapter adapter);
    const InteropAdapterBinding* find(std::string_view external_name) const;
private:
    std::unordered_map<std::string, InteropAdapterBinding> bindings_;
};
std::string interop_type_name(InteropType type);
void validate_interop_external_name(std::string_view name);
CapabilityMask parse_interop_capability_spec(std::string_view spec);
void validate_interop_value(const Value& value, InteropType expected);
InteropBytes encode_interop_request(const InteropSignature& signature,
                                    const std::vector<Value>& arguments);
std::vector<Value> decode_interop_request(const InteropSignature& signature,
                                          std::span<const std::uint8_t> bytes);
InteropBytes encode_interop_success(InteropType result_type, const Value& value);
InteropBytes encode_interop_failure(std::string_view message);
Value decode_interop_response(InteropType result_type, std::span<const std::uint8_t> bytes);
} // namespace emojineer
