#include "emojineer/intrinsic.hpp"
#include "emojineer/unicode.hpp"

#include <array>
#include <stdexcept>

namespace emojineer {
namespace {

constexpr std::array<Intrinsic, 14> IntrinsicOrder{
    Intrinsic::RecordCreate, Intrinsic::RecordType, Intrinsic::RecordKeys,
    Intrinsic::ResultOk, Intrinsic::ResultError, Intrinsic::ResultIsOk,
    Intrinsic::ResultPayload, Intrinsic::Utf8Encode, Intrinsic::Utf8Decode,
    Intrinsic::HexEncode, Intrinsic::HexDecode,
    Intrinsic::Base64Encode, Intrinsic::Base64Decode, Intrinsic::ProgramArguments,
};

} // namespace

std::string intrinsic_name(Intrinsic value) {
    switch (value) {
        case Intrinsic::RecordCreate: return "record.create";
        case Intrinsic::RecordType: return "record.type";
        case Intrinsic::RecordKeys: return "record.keys";
        case Intrinsic::ResultOk: return "result.ok";
        case Intrinsic::ResultError: return "result.error";
        case Intrinsic::ResultIsOk: return "result.is-ok";
        case Intrinsic::ResultPayload: return "result.payload";
        case Intrinsic::Utf8Encode: return "encoding.utf8-encode";
        case Intrinsic::Utf8Decode: return "encoding.utf8-decode";
        case Intrinsic::HexEncode: return "encoding.hex-encode";
        case Intrinsic::HexDecode: return "encoding.hex-decode";
        case Intrinsic::Base64Encode: return "encoding.base64-encode";
        case Intrinsic::Base64Decode: return "encoding.base64-decode";
        case Intrinsic::ProgramArguments: return "runtime.arguments";
    }
    throw std::runtime_error("unknown intrinsic");
}

std::string intrinsic_glyph(Intrinsic value) {
    switch (value) {
        case Intrinsic::RecordCreate: return "🗃️";
        case Intrinsic::RecordType: return "🏷️";
        case Intrinsic::RecordKeys: return "🗝️";
        case Intrinsic::ResultOk: return "🟢";
        case Intrinsic::ResultError: return "🔴";
        case Intrinsic::ResultIsOk: return "👌";
        case Intrinsic::ResultPayload: return "🎁";
        case Intrinsic::Utf8Encode: return "🧬";
        case Intrinsic::Utf8Decode: return "🗣️";
        case Intrinsic::HexEncode: return "🔡";
        case Intrinsic::HexDecode: return "🔣";
        case Intrinsic::Base64Encode: return "📨";
        case Intrinsic::Base64Decode: return "📩";
        case Intrinsic::ProgramArguments: return "🧳";
    }
    throw std::runtime_error("unknown intrinsic");
}

std::size_t intrinsic_arity(Intrinsic value) {
    switch (value) {
        case Intrinsic::RecordCreate: return 2;
        case Intrinsic::RecordType:
        case Intrinsic::RecordKeys:
        case Intrinsic::ResultOk:
        case Intrinsic::ResultError:
        case Intrinsic::ResultIsOk:
        case Intrinsic::ResultPayload:
        case Intrinsic::Utf8Encode:
        case Intrinsic::Utf8Decode:
        case Intrinsic::HexEncode:
        case Intrinsic::HexDecode:
        case Intrinsic::Base64Encode:
        case Intrinsic::Base64Decode:
            return 1;
        case Intrinsic::ProgramArguments:
            return 0;
    }
    throw std::runtime_error("unknown intrinsic");
}

std::optional<Intrinsic> intrinsic_from_identifier(std::string_view identifier) {
    for (const auto value : IntrinsicOrder) {
        if (canonicalize_token(intrinsic_glyph(value)) == identifier) return value;
    }
    return std::nullopt;
}

std::optional<Intrinsic> intrinsic_from_operand(std::int32_t operand) {
    if (operand < static_cast<std::int32_t>(Intrinsic::RecordCreate) ||
        operand > static_cast<std::int32_t>(Intrinsic::ProgramArguments)) {
        return std::nullopt;
    }
    return static_cast<Intrinsic>(operand);
}

} // namespace emojineer
