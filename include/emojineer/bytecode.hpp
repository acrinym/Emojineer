#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>
#include <variant>
#include <vector>
namespace emojineer {
using Value=std::variant<std::int64_t,double,bool,std::string>;
enum class OpCode:std::uint8_t{Constant,LoadGlobal,StoreGlobal,LoadLocal,StoreLocal,AssertNumber,AssertString,AssertBool,Add,Subtract,Multiply,Divide,Modulo,AddInt,SubtractInt,MultiplyInt,Equal,Less,Greater,Negate,Not,ReadLine,Print,JumpIfFalse,Jump,Call,Return,Halt};
struct Instruction{OpCode op{OpCode::Halt};std::int32_t operand{0};std::uint32_t line{0};};
struct FunctionInfo{std::string name;std::uint32_t entry{0};std::uint32_t arity{0};std::uint32_t local_count{0};};
struct Chunk{std::vector<Value> constants;std::vector<FunctionInfo> functions;std::vector<Instruction> code;std::int32_t add_constant(Value value);};
void write_bytecode(const Chunk& chunk,std::ostream& out);Chunk read_bytecode(std::istream& in);void verify_bytecode(const Chunk& chunk);std::string opcode_name(OpCode op);std::string value_to_string(const Value& value);
}
