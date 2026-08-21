#include "emojineer/bytecode.hpp"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace emojineer {
namespace {
constexpr char Magic[] = {'E','M','J','B','C'};
constexpr std::uint16_t Version = 1;
template <typename T> void write_pod(std::ostream& out, T value) { static_assert(std::is_trivially_copyable_v<T>); out.write(reinterpret_cast<const char*>(&value), sizeof(T)); if (!out) throw std::runtime_error("failed to write bytecode"); }
template <typename T> T read_pod(std::istream& in) { static_assert(std::is_trivially_copyable_v<T>); T value{}; in.read(reinterpret_cast<char*>(&value), sizeof(T)); if (!in) throw std::runtime_error("truncated bytecode"); return value; }
void write_string(std::ostream& out, const std::string& value) { if (value.size() > UINT32_MAX) throw std::runtime_error("string too large for bytecode"); write_pod<std::uint32_t>(out, static_cast<std::uint32_t>(value.size())); out.write(value.data(), static_cast<std::streamsize>(value.size())); if (!out) throw std::runtime_error("failed to write bytecode string"); }
std::string read_string(std::istream& in) { const auto size = read_pod<std::uint32_t>(in); std::string value(size, '\0'); in.read(value.data(), static_cast<std::streamsize>(size)); if (!in) throw std::runtime_error("truncated bytecode string"); return value; }
} // namespace

std::int32_t Chunk::add_constant(Value value) { if (constants.size() >= static_cast<std::size_t>(INT32_MAX)) throw std::runtime_error("too many constants"); constants.push_back(std::move(value)); return static_cast<std::int32_t>(constants.size() - 1); }
void write_bytecode(const Chunk& chunk, std::ostream& out) {
    out.write(Magic, sizeof(Magic)); write_pod<std::uint16_t>(out, Version); write_pod<std::uint32_t>(out, static_cast<std::uint32_t>(chunk.constants.size()));
    for (const Value& value : chunk.constants) {
        if (const auto* n = std::get_if<double>(&value)) { write_pod<std::uint8_t>(out, 1); write_pod<double>(out, *n); }
        else if (const auto* b = std::get_if<bool>(&value)) { write_pod<std::uint8_t>(out, 2); write_pod<std::uint8_t>(out, *b ? 1 : 0); }
        else if (const auto* s = std::get_if<std::string>(&value)) { write_pod<std::uint8_t>(out, 3); write_string(out, *s); }
    }
    write_pod<std::uint32_t>(out, static_cast<std::uint32_t>(chunk.code.size()));
    for (const Instruction& i : chunk.code) { write_pod<std::uint8_t>(out, static_cast<std::uint8_t>(i.op)); write_pod<std::int32_t>(out, i.operand); write_pod<std::uint32_t>(out, i.line); }
}
Chunk read_bytecode(std::istream& in) {
    char magic[sizeof(Magic)]{}; in.read(magic, sizeof(magic)); if (!in || std::memcmp(magic, Magic, sizeof(Magic)) != 0) throw std::runtime_error("not an Emojineer bytecode file");
    const auto version = read_pod<std::uint16_t>(in); if (version != Version) throw std::runtime_error("unsupported Emojineer bytecode version " + std::to_string(version));
    Chunk chunk; const auto cc = read_pod<std::uint32_t>(in); chunk.constants.reserve(cc);
    for (std::uint32_t i = 0; i < cc; ++i) { const auto tag = read_pod<std::uint8_t>(in); switch (tag) { case 1: chunk.constants.emplace_back(read_pod<double>(in)); break; case 2: chunk.constants.emplace_back(read_pod<std::uint8_t>(in) != 0); break; case 3: chunk.constants.emplace_back(read_string(in)); break; default: throw std::runtime_error("invalid constant tag in bytecode"); } }
    const auto code_count = read_pod<std::uint32_t>(in); chunk.code.reserve(code_count);
    for (std::uint32_t i = 0; i < code_count; ++i) { const auto raw = read_pod<std::uint8_t>(in); if (raw > static_cast<std::uint8_t>(OpCode::Halt)) throw std::runtime_error("invalid opcode in bytecode"); chunk.code.push_back({static_cast<OpCode>(raw), read_pod<std::int32_t>(in), read_pod<std::uint32_t>(in)}); }
    return chunk;
}
std::string opcode_name(OpCode op) {
    switch (op) { case OpCode::Constant:return"Constant";case OpCode::LoadGlobal:return"LoadGlobal";case OpCode::StoreGlobal:return"StoreGlobal";case OpCode::AssertNumber:return"AssertNumber";case OpCode::AssertString:return"AssertString";case OpCode::AssertBool:return"AssertBool";case OpCode::Add:return"Add";case OpCode::Subtract:return"Subtract";case OpCode::Multiply:return"Multiply";case OpCode::Divide:return"Divide";case OpCode::Modulo:return"Modulo";case OpCode::Equal:return"Equal";case OpCode::Less:return"Less";case OpCode::Greater:return"Greater";case OpCode::Negate:return"Negate";case OpCode::Not:return"Not";case OpCode::ReadLine:return"ReadLine";case OpCode::Print:return"Print";case OpCode::JumpIfFalse:return"JumpIfFalse";case OpCode::Jump:return"Jump";case OpCode::Halt:return"Halt"; } return"Unknown";
}
std::string value_to_string(const Value& value) {
    if (const auto* n = std::get_if<double>(&value)) { std::ostringstream out; if (std::isfinite(*n) && std::floor(*n)==*n) out<<std::fixed<<std::setprecision(0)<<*n; else out<<std::setprecision(15)<<*n; return out.str(); }
    if (const auto* b = std::get_if<bool>(&value)) return *b ? "✅" : "❌";
    return std::get<std::string>(value);
}
} // namespace emojineer
