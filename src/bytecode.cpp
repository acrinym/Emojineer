#include "emojineer/bytecode.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <iterator>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace emojineer {
namespace {

constexpr char Magic[] = {'E','M','J','B','C'};
constexpr std::uint16_t Version = 1;
constexpr std::uint32_t MaxConstants = 1'000'000;
constexpr std::uint32_t MaxInstructions = 10'000'000;
constexpr std::uint32_t MaxStringBytes = 64 * 1024 * 1024;

void write_u8(std::ostream& out, std::uint8_t value) {
    out.put(static_cast<char>(value));
    if (!out) throw std::runtime_error("failed to write bytecode");
}

void write_u16_le(std::ostream& out, std::uint16_t value) {
    write_u8(out, static_cast<std::uint8_t>(value & 0xFFu));
    write_u8(out, static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
}

void write_u32_le(std::ostream& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        write_u8(out, static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

void write_u64_le(std::ostream& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        write_u8(out, static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

std::uint8_t read_u8(std::istream& in) {
    const int value = in.get();
    if (value == std::char_traits<char>::eof()) throw std::runtime_error("truncated bytecode");
    return static_cast<std::uint8_t>(value);
}

std::uint16_t read_u16_le(std::istream& in) {
    return static_cast<std::uint16_t>(read_u8(in)) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(read_u8(in)) << 8u);
}

std::uint32_t read_u32_le(std::istream& in) {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(read_u8(in)) << shift;
    }
    return value;
}

std::uint64_t read_u64_le(std::istream& in) {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(read_u8(in)) << shift;
    }
    return value;
}

void write_string(std::ostream& out, const std::string& value) {
    if (value.size() > MaxStringBytes) throw std::runtime_error("string too large for bytecode");
    write_u32_le(out, static_cast<std::uint32_t>(value.size()));
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!out) throw std::runtime_error("failed to write bytecode string");
}

std::string read_string(std::istream& in) {
    const std::uint32_t size = read_u32_le(in);
    if (size > MaxStringBytes) throw std::runtime_error("bytecode string exceeds safety limit");
    std::string value(size, '\0');
    in.read(value.data(), static_cast<std::streamsize>(size));
    if (!in) throw std::runtime_error("truncated bytecode string");
    return value;
}

} // namespace

std::int32_t Chunk::add_constant(Value value) {
    if (constants.size() >= MaxConstants) throw std::runtime_error("too many constants");
    constants.push_back(std::move(value));
    return static_cast<std::int32_t>(constants.size() - 1);
}

void write_bytecode(const Chunk& chunk, std::ostream& out) {
    if (chunk.constants.size() > MaxConstants) throw std::runtime_error("too many constants");
    if (chunk.code.size() > MaxInstructions) throw std::runtime_error("too many instructions");

    out.write(Magic, sizeof(Magic));
    if (!out) throw std::runtime_error("failed to write bytecode header");
    write_u16_le(out, Version);
    write_u32_le(out, static_cast<std::uint32_t>(chunk.constants.size()));

    for (const Value& value : chunk.constants) {
        if (const auto* number = std::get_if<double>(&value)) {
            static_assert(sizeof(double) == sizeof(std::uint64_t));
            write_u8(out, 1);
            write_u64_le(out, std::bit_cast<std::uint64_t>(*number));
        } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
            write_u8(out, 4);
            write_u64_le(out, static_cast<std::uint64_t>(*integer));
        } else if (const auto* boolean = std::get_if<bool>(&value)) {
            write_u8(out, 2);
            write_u8(out, *boolean ? 1 : 0);
        } else if (const auto* string = std::get_if<std::string>(&value)) {
            write_u8(out, 3);
            write_string(out, *string);
        }
    }

    write_u32_le(out, static_cast<std::uint32_t>(chunk.code.size()));
    for (const Instruction& instruction : chunk.code) {
        write_u8(out, static_cast<std::uint8_t>(instruction.op));
        write_u32_le(out, std::bit_cast<std::uint32_t>(instruction.operand));
        write_u32_le(out, instruction.line);
    }
}

Chunk read_bytecode(std::istream& in) {
    char magic[sizeof(Magic)]{};
    in.read(magic, sizeof(magic));
    if (!in || !std::equal(std::begin(magic), std::end(magic), std::begin(Magic))) {
        throw std::runtime_error("not an Emojineer bytecode file");
    }

    const std::uint16_t version = read_u16_le(in);
    if (version != Version) {
        throw std::runtime_error("unsupported Emojineer bytecode version " + std::to_string(version));
    }

    Chunk chunk;
    const std::uint32_t constant_count = read_u32_le(in);
    if (constant_count > MaxConstants) throw std::runtime_error("bytecode constant pool exceeds safety limit");
    chunk.constants.reserve(constant_count);
    for (std::uint32_t i = 0; i < constant_count; ++i) {
        switch (read_u8(in)) {
            case 1: chunk.constants.emplace_back(std::bit_cast<double>(read_u64_le(in))); break;
            case 2: {
                const std::uint8_t raw = read_u8(in);
                if (raw > 1) throw std::runtime_error("invalid boolean constant in bytecode");
                chunk.constants.emplace_back(raw != 0);
                break;
            }
            case 3: chunk.constants.emplace_back(read_string(in)); break;
            case 4: chunk.constants.emplace_back(static_cast<std::int64_t>(read_u64_le(in))); break;
            default: throw std::runtime_error("invalid constant tag in bytecode");
        }
    }

    const std::uint32_t code_count = read_u32_le(in);
    if (code_count > MaxInstructions) throw std::runtime_error("bytecode instruction stream exceeds safety limit");
    chunk.code.reserve(code_count);
    for (std::uint32_t i = 0; i < code_count; ++i) {
        const std::uint8_t raw_op = read_u8(in);
        if (raw_op > static_cast<std::uint8_t>(OpCode::Halt)) throw std::runtime_error("invalid opcode in bytecode");
        chunk.code.push_back({
            static_cast<OpCode>(raw_op),
            std::bit_cast<std::int32_t>(read_u32_le(in)),
            read_u32_le(in)
        });
    }
    return chunk;
}

std::string opcode_name(OpCode op) {
    switch (op) {
        case OpCode::Constant: return "Constant";
        case OpCode::LoadGlobal: return "LoadGlobal";
        case OpCode::StoreGlobal: return "StoreGlobal";
        case OpCode::AssertNumber: return "AssertNumber";
        case OpCode::AssertString: return "AssertString";
        case OpCode::AssertBool: return "AssertBool";
        case OpCode::Add: return "Add";
        case OpCode::Subtract: return "Subtract";
        case OpCode::Multiply: return "Multiply";
        case OpCode::Divide: return "Divide";
        case OpCode::Modulo: return "Modulo";
        case OpCode::AddInt: return "AddInt";
        case OpCode::SubtractInt: return "SubtractInt";
        case OpCode::MultiplyInt: return "MultiplyInt";
        case OpCode::Equal: return "Equal";
        case OpCode::Less: return "Less";
        case OpCode::Greater: return "Greater";
        case OpCode::Negate: return "Negate";
        case OpCode::Not: return "Not";
        case OpCode::ReadLine: return "ReadLine";
        case OpCode::Print: return "Print";
        case OpCode::JumpIfFalse: return "JumpIfFalse";
        case OpCode::Jump: return "Jump";
        case OpCode::Halt: return "Halt";
    }
    return "Unknown";
}

std::string value_to_string(const Value& value) {
    if (const auto* number = std::get_if<double>(&value)) {
        std::ostringstream out;
        if (std::isfinite(*number) && std::floor(*number) == *number) {
            out << std::fixed << std::setprecision(0) << *number;
        } else {
            out << std::setprecision(15) << *number;
        }
        return out.str();
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*integer);
    }
    if (const auto* boolean = std::get_if<bool>(&value)) return *boolean ? "✅" : "❌";
    return std::get<std::string>(value);
}

} // namespace emojineer
