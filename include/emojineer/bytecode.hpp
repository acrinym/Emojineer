#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <variant>
#include <vector>

namespace emojineer {

using Value = std::variant<double, bool, std::string>;

enum class OpCode : std::uint8_t {
    Constant,
    LoadGlobal,
    StoreGlobal,
    AssertNumber,
    AssertString,
    AssertBool,
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Equal,
    Less,
    Greater,
    Negate,
    Not,
    ReadLine,
    Print,
    JumpIfFalse,
    Jump,
    Halt,
};

struct Instruction {
    OpCode op{OpCode::Halt};
    std::int32_t operand{0};
    std::uint32_t line{0};
};

struct Chunk {
    std::vector<Value> constants;
    std::vector<Instruction> code;

    std::int32_t add_constant(Value value);
};

void write_bytecode(const Chunk& chunk, std::ostream& out);
Chunk read_bytecode(std::istream& in);
std::string opcode_name(OpCode op);
std::string value_to_string(const Value& value);

} // namespace emojineer
