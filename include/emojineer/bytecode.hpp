#pragma once
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <variant>
#include <unordered_map>
#include <vector>
namespace emojineer {
struct ArrayValue;
using ArrayPtr=std::shared_ptr<ArrayValue>;
using Value=std::variant<std::int64_t,double,bool,std::string,ArrayPtr>;
struct ArrayValue{std::vector<Value> elements;};
enum class OpCode:std::uint8_t{Constant,LoadGlobal,StoreGlobal,LoadLocal,StoreLocal,AssertNumber,AssertString,AssertBool,Add,Subtract,Multiply,Divide,Modulo,AddInt,SubtractInt,MultiplyInt,Equal,Less,Greater,Negate,Not,ReadLine,Print,JumpIfFalse,Jump,Call,Return,Halt,AssertArray,MakeArray,Index,Length,Append,SetIndex,HostCall,InteropCall};
struct Instruction{OpCode op{OpCode::Halt};std::int32_t operand{0};std::uint32_t line{0};};
struct FunctionInfo{std::string name;std::uint32_t entry{0};std::uint32_t arity{0};std::uint32_t local_count{0};std::vector<std::string> parameter_names;std::vector<std::string> local_names;};

enum class InteropType : std::uint8_t { Number = 1, String = 2, Bool = 3, Array = 4 };
struct InteropSignature { std::vector<InteropType> parameters; InteropType result{InteropType::Bool}; };
struct InteropImportInfo {
    std::string internal_name;
    std::string external_name;
    InteropSignature signature;
    std::uint32_t required_capabilities{0};
};
struct InteropExportInfo {
    std::string external_name;
    std::uint32_t function_index{0};
    InteropSignature signature;
};

// Source position for deterministic bytecode mapping (no absolute roots)
// EMJBC v6: includes source range (start/end) and function context
struct SourceLocation{
    std::string source_path;     // Deterministic module identity
    std::uint32_t line{1};       // 1-based line number (start line)
    std::uint32_t column{1};    // 1-based column number (start column)
    std::uint32_t end_line{1};   // 1-based end line number
    std::uint32_t end_column{1}; // 1-based end column number
    std::string function_name;   // Function context (empty for module-level code)
};

/// Complete in-memory bytecode unit consumed by verification, serialization, and the VM.
///
/// Chunk is intentionally constructible before verification; write_bytecode(),
/// read_bytecode(), and VM execution enforce structural validity at their boundaries.
struct Chunk{
    std::vector<Value> constants;
    std::vector<FunctionInfo> functions;
    std::vector<Instruction> code;
    std::vector<SourceLocation> source_map;  // Indexed by instruction pointer
    // EMJBC v7: source identity -> SHA-256 of the exact source text used to compile.
    // This is one digest per source, not one digest per instruction.
    std::unordered_map<std::string, std::string> source_hashes;
    // EMJBC v8+: exact host capability contract inferred from host/interop calls.
    std::uint32_t required_capabilities{0};
    // EMJBC v9: verifier-visible typed host/WASM ABI surfaces.
    std::vector<InteropImportInfo> interop_imports;
    std::vector<InteropExportInfo> interop_exports;

    /// Append a serializable constant and return its stable pool index.
    ///
    /// Rejects arrays and pool growth beyond the bytecode safety bound.
    std::int32_t add_constant(Value value);
};

/// Verify a chunk and serialize it using the current EMJBC writer version.
///
/// Serialization never bypasses structural verification; malformed capability,
/// source-map, provenance, function, or instruction metadata is rejected first.
void write_bytecode(const Chunk& chunk,std::ostream& out);

/// Parse bounded EMJBC v1-v9 input and verify the resulting chunk before return.
Chunk read_bytecode(std::istream& in);

/// Validate structural safety independently of source parsing.
///
/// In v9-era chunks this includes recomputing the exact capability union from
/// HostCall and InteropCall instructions and requiring it to equal required_capabilities.
void verify_bytecode(const Chunk& chunk);

/// Return the stable diagnostic/disassembly name for an opcode.
std::string opcode_name(OpCode op);

/// Render one runtime value deterministically for language-visible output/tooling.
std::string value_to_string(const Value& value);

/// Compare runtime values using Emojineer's structural value semantics.
bool values_equal(const Value& left,const Value& right);
}
