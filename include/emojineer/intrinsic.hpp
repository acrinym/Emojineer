#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace emojineer {

enum class Intrinsic : std::int32_t {
    RecordCreate = 0,
    RecordType = 1,
    RecordKeys = 2,
    ResultOk = 3,
    ResultError = 4,
    ResultIsOk = 5,
    ResultPayload = 6,
    Utf8Encode = 7,
    Utf8Decode = 8,
    HexEncode = 9,
    HexDecode = 10,
    Base64Encode = 11,
    Base64Decode = 12,
    ProgramArguments = 13,
};

std::string intrinsic_name(Intrinsic intrinsic);
std::string intrinsic_glyph(Intrinsic intrinsic);
std::size_t intrinsic_arity(Intrinsic intrinsic);
std::optional<Intrinsic> intrinsic_from_identifier(std::string_view canonical_identifier);
std::optional<Intrinsic> intrinsic_from_operand(std::int32_t operand);

} // namespace emojineer
