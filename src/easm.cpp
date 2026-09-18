#include "emojineer/easm.hpp"
#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace emojineer { namespace {
constexpr std::size_t MaxBuffers = 1024;
constexpr std::size_t MaxImports = 10000;
constexpr std::size_t MaxFunctions = 10000;
constexpr std::size_t MaxExports = 10000;
constexpr std::size_t MaxRegisters = 4096;
constexpr std::size_t MaxInstructions = 1'000'000;
constexpr std::uint64_t MaxBufferBytes = 16ull * 1024 * 1024;
constexpr std::uint64_t MaxTotalMemoryBytes = 64ull * 1024 * 1024;
constexpr double MinI64InclusiveAsF64 = -0x1p63;
constexpr double MaxI64ExclusiveAsF64 = 0x1p63;

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r");
    return value.substr(first, last - first + 1);
}
std::vector<std::string> words(const std::string& line) {
    std::istringstream in(line); std::vector<std::string> out; std::string word;
    while (in >> word) out.push_back(word);
    return out;
}
[[noreturn]] void parse_error(std::size_t line, const std::string& message) {
    throw std::runtime_error("EASM line " + std::to_string(line) + ": " + message);
}
EasmScalarType parse_scalar(std::string_view text) {
    if (text == "i64") return EasmScalarType::I64;
    if (text == "f64") return EasmScalarType::F64;
    if (text == "bool") return EasmScalarType::Bool;
    throw std::runtime_error("unknown EASM scalar type '" + std::string(text) + "'");
}
EasmBufferType parse_buffer(std::string_view text) {
    if (text == "u8") return EasmBufferType::U8;
    if (text == "i64") return EasmBufferType::I64;
    if (text == "f64") return EasmBufferType::F64;
    throw std::runtime_error("unknown EASM buffer type '" + std::string(text) + "'");
}
std::int32_t parse_reg(std::string_view text) {
    if (text.size() < 2 || text.front() != 'r') throw std::runtime_error("expected register rN");
    std::int64_t raw = 0;
    const auto first = text.data() + 1;
    const auto last = text.data() + text.size();
    const auto [end, error] = std::from_chars(first, last, raw, 10);
    if (error != std::errc{} || end != last || raw < 0 || raw > std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("invalid register '" + std::string(text) + "'");
    return static_cast<std::int32_t>(raw);
}
std::int64_t parse_i64_literal(std::string_view text) {
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::runtime_error("invalid i64 literal '" + std::string(text) + "'");
    return value;
}
std::uint32_t parse_u32_literal(std::string_view text, std::string_view label) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (error != std::errc{} || end != text.data() + text.size() || value > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error(std::string(label) + " is not a valid u32 value");
    return static_cast<std::uint32_t>(value);
}
double parse_f64_literal(std::string_view text) {
    double value = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
    if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value))
        throw std::runtime_error("invalid finite f64 literal '" + std::string(text) + "'");
    return value;
}
std::string render_f64_literal(double value) {
    char buffer[64];
    const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                             std::chars_format::general,
                                             std::numeric_limits<double>::max_digits10);
    if (error != std::errc{}) throw std::runtime_error("failed to render EASM f64 literal");
    return std::string(buffer, end);
}
std::vector<EasmScalarType> parse_type_list(std::string text) {
    text = trim(std::move(text));
    if (text.size() < 2 || text.front() != '(' || text.back() != ')')
        throw std::runtime_error("expected parenthesized type list");
    text = trim(text.substr(1, text.size() - 2)); std::vector<EasmScalarType> out;
    if (text.empty()) return out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto comma = text.find(',', start); const auto end = comma == std::string::npos ? text.size() : comma;
        out.push_back(parse_scalar(trim(text.substr(start, end - start))));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}
struct ParsedSignature {
    std::vector<EasmScalarType> parameters;
    EasmScalarType result{EasmScalarType::Bool};
};
ParsedSignature parse_signature_tail(std::string_view tail) {
    const auto open = tail.find('('); const auto close = tail.find(')', open == std::string_view::npos ? 0 : open);
    const auto arrow = tail.find("->", close == std::string_view::npos ? 0 : close);
    if (open == std::string_view::npos || close == std::string_view::npos || arrow == std::string_view::npos || arrow < close)
        throw std::runtime_error("expected signature '(...) -> type'");
    if (!trim(std::string(tail.substr(close + 1, arrow - close - 1))).empty())
        throw std::runtime_error("unexpected content between EASM parameter list and result arrow");
    ParsedSignature out;
    out.parameters = parse_type_list(std::string(tail.substr(open, close - open + 1)));
    const auto result_text = trim(std::string(tail.substr(arrow + 2)));
    if (result_text.empty()) throw std::runtime_error("missing result type");
    out.result = parse_scalar(result_text);
    return out;
}
std::uint64_t buffer_element_size(EasmBufferType type) {
    switch (type) {
        case EasmBufferType::U8: return 1;
        case EasmBufferType::I64: case EasmBufferType::F64: return 8;
    }
    return 0;
}
InteropType interop_type(EasmScalarType type) {
    switch (type) {
        case EasmScalarType::I64: case EasmScalarType::F64: return InteropType::Number;
        case EasmScalarType::Bool: return InteropType::Bool;
    }
    throw std::runtime_error("invalid EASM scalar type");
}
std::string signature_text(const std::vector<EasmScalarType>& parameters, EasmScalarType result) {
    std::string out = "(";
    for (std::size_t i = 0; i < parameters.size(); ++i) { if (i) out += ','; out += easm_scalar_type_name(parameters[i]); }
    out += ") -> "; out += easm_scalar_type_name(result); return out;
}
EasmScalar scalar_from_value(const Value& value, EasmScalarType expected) {
    if (expected == EasmScalarType::I64) {
        if (const auto* integer = std::get_if<std::int64_t>(&value)) return *integer;
        if (const auto* number = std::get_if<double>(&value)) {
            if (std::isfinite(*number) && std::floor(*number) == *number &&
                *number >= MinI64InclusiveAsF64 && *number < MaxI64ExclusiveAsF64)
                return static_cast<std::int64_t>(*number);
        }
        throw std::runtime_error("EASM i64 ABI parameter requires a whole in-range number");
    }
    if (expected == EasmScalarType::F64) {
        if (const auto* number = std::get_if<double>(&value)) {
            if (!std::isfinite(*number)) throw std::runtime_error("EASM f64 rejects non-finite values");
            return *number;
        }
        if (const auto* integer = std::get_if<std::int64_t>(&value)) {
            const double converted = static_cast<double>(*integer);
            if (converted < MinI64InclusiveAsF64 || converted >= MaxI64ExclusiveAsF64 ||
                static_cast<std::int64_t>(converted) != *integer)
                throw std::runtime_error("EASM f64 conversion would lose integer precision");
            return converted;
        }
        throw std::runtime_error("EASM f64 ABI parameter requires a number");
    }
    if (const auto* boolean = std::get_if<bool>(&value)) return *boolean;
    throw std::runtime_error("EASM bool ABI parameter requires a boolean");
}
Value value_from_scalar(const EasmScalar& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) return *integer;
    if (const auto* number = std::get_if<double>(&value)) return *number;
    return std::get<bool>(value);
}
void require_scalar_type(const EasmScalar& value, EasmScalarType expected) {
    const bool ok = (expected == EasmScalarType::I64 && std::holds_alternative<std::int64_t>(value)) ||
                    (expected == EasmScalarType::F64 && std::holds_alternative<double>(value)) ||
                    (expected == EasmScalarType::Bool && std::holds_alternative<bool>(value));
    if (!ok) throw std::runtime_error("EASM scalar type mismatch");
}
std::size_t checked_index(const EasmScalar& value, std::size_t bound) {
    const auto* integer = std::get_if<std::int64_t>(&value);
    if (!integer || *integer < 0 || static_cast<std::uint64_t>(*integer) >= bound)
        throw std::runtime_error("EASM buffer index out of range");
    return static_cast<std::size_t>(*integer);
}
} // namespace
std::string easm_scalar_type_name(EasmScalarType type) {
    switch (type) { case EasmScalarType::I64:return "i64"; case EasmScalarType::F64:return "f64"; case EasmScalarType::Bool:return "bool"; }
    return "?";
}
std::string easm_buffer_type_name(EasmBufferType type) {
    switch (type) { case EasmBufferType::U8:return "u8"; case EasmBufferType::I64:return "i64"; case EasmBufferType::F64:return "f64"; }
    return "?";
}
std::string easm_opcode_name(EasmOp op) {
    switch (op) {
        case EasmOp::ConstI64:return "i64.const"; case EasmOp::ConstF64:return "f64.const"; case EasmOp::ConstBool:return "bool.const"; case EasmOp::Move:return "move";
        case EasmOp::AddI64:return "i64.add"; case EasmOp::SubI64:return "i64.sub"; case EasmOp::MulI64:return "i64.mul";
        case EasmOp::AddF64:return "f64.add"; case EasmOp::SubF64:return "f64.sub"; case EasmOp::MulF64:return "f64.mul"; case EasmOp::DivF64:return "f64.div";
        case EasmOp::EqI64:return "i64.eq"; case EasmOp::LtI64:return "i64.lt"; case EasmOp::EqF64:return "f64.eq"; case EasmOp::LtF64:return "f64.lt";
        case EasmOp::LoadU8:return "buffer.load.u8"; case EasmOp::StoreU8:return "buffer.store.u8";
        case EasmOp::LoadI64:return "buffer.load.i64"; case EasmOp::StoreI64:return "buffer.store.i64";
        case EasmOp::LoadF64:return "buffer.load.f64"; case EasmOp::StoreF64:return "buffer.store.f64";
        case EasmOp::CallImport:return "call"; case EasmOp::Jump:return "jump"; case EasmOp::JumpIfFalse:return "jump_if_false"; case EasmOp::Return:return "return";
    }
    return "?";
}
InteropSignature easm_interop_signature(const std::vector<EasmScalarType>& parameters, EasmScalarType result) {
    InteropSignature signature; signature.result = interop_type(result);
    for (const auto type : parameters) signature.parameters.push_back(interop_type(type));
    return signature;
}
EasmProgram parse_easm(std::string_view source) {
    EasmProgram program;
    std::unordered_map<std::string, std::uint32_t> buffers, imports, functions;
    std::istringstream input{std::string(source)}; std::string raw; std::size_t line_no = 0;
    bool saw_header = false; EasmFunction* current = nullptr;
    std::unordered_map<std::string, std::uint32_t> labels;
    std::vector<std::pair<std::size_t, std::string>> pending_jumps;

    auto finish_function = [&] {
        if (!current) return;
        for (const auto& [instruction_index, label] : pending_jumps) {
            const auto found = labels.find(label);
            if (found == labels.end()) parse_error(line_no, "unknown label '" + label + "'");
            current->code.at(instruction_index).operand = found->second;
        }
        current = nullptr; labels.clear(); pending_jumps.clear();
    };

    while (std::getline(input, raw)) {
        ++line_no;
        std::string line = trim(std::move(raw));
        if (line.empty() || line.front() == '#') continue;
        if (!saw_header) {
            if (line != "EASM1") parse_error(line_no, "first statement must be EASM1");
            saw_header = true; continue;
        }
        try {
            if (current) {
                if (line == "end") { finish_function(); continue; }
                const auto w = words(line); if (w.empty()) continue;
                if (w[0] == "label") {
                    if (w.size() != 2) throw std::runtime_error("label requires one name");
                    if (!labels.emplace(w[1], static_cast<std::uint32_t>(current->code.size())).second)
                        throw std::runtime_error("duplicate label '" + w[1] + "'");
                    continue;
                }
                if (current->code.size() >= MaxInstructions)
                    throw std::runtime_error("EASM instruction count exceeds safety limit");
                EasmInstruction ins;
                auto need = [&](std::size_t n) { if (w.size() != n) throw std::runtime_error(w[0] + " has wrong operand count"); };
                auto binary = [&](EasmOp op) { need(4); ins.op=op; ins.dst=parse_reg(w[1]); ins.a=parse_reg(w[2]); ins.b=parse_reg(w[3]); };
                if (w[0] == "i64.const") { need(3); ins.op=EasmOp::ConstI64; ins.dst=parse_reg(w[1]); ins.imm_i64=parse_i64_literal(w[2]); }
                else if (w[0] == "f64.const") { need(3); ins.op=EasmOp::ConstF64; ins.dst=parse_reg(w[1]); ins.imm_f64=parse_f64_literal(w[2]); }
                else if (w[0] == "bool.const") { need(3); ins.op=EasmOp::ConstBool; ins.dst=parse_reg(w[1]); if(w[2]=="true") ins.imm_i64=1; else if(w[2]=="false") ins.imm_i64=0; else throw std::runtime_error("bool.const requires true or false"); }
                else if (w[0] == "move") { need(3); ins.op=EasmOp::Move; ins.dst=parse_reg(w[1]); ins.a=parse_reg(w[2]); }
                else if (w[0] == "i64.add") binary(EasmOp::AddI64);
                else if (w[0] == "i64.sub") binary(EasmOp::SubI64);
                else if (w[0] == "i64.mul") binary(EasmOp::MulI64);
                else if (w[0] == "f64.add") binary(EasmOp::AddF64);
                else if (w[0] == "f64.sub") binary(EasmOp::SubF64);
                else if (w[0] == "f64.mul") binary(EasmOp::MulF64);
                else if (w[0] == "f64.div") binary(EasmOp::DivF64);
                else if (w[0] == "i64.eq") binary(EasmOp::EqI64);
                else if (w[0] == "i64.lt") binary(EasmOp::LtI64);
                else if (w[0] == "f64.eq") binary(EasmOp::EqF64);
                else if (w[0] == "f64.lt") binary(EasmOp::LtF64);
                else if (w[0].rfind("buffer.load.",0)==0) {
                    need(4); ins.dst=parse_reg(w[1]); ins.a=parse_reg(w[3]);
                    const auto found=buffers.find(w[2]); if(found==buffers.end()) throw std::runtime_error("unknown buffer '"+w[2]+"'"); ins.operand=found->second;
                    if(w[0]=="buffer.load.u8") ins.op=EasmOp::LoadU8; else if(w[0]=="buffer.load.i64") ins.op=EasmOp::LoadI64; else if(w[0]=="buffer.load.f64") ins.op=EasmOp::LoadF64; else throw std::runtime_error("unknown buffer load opcode");
                }
                else if (w[0].rfind("buffer.store.",0)==0) {
                    need(4); ins.a=parse_reg(w[2]); ins.b=parse_reg(w[3]);
                    const auto found=buffers.find(w[1]); if(found==buffers.end()) throw std::runtime_error("unknown buffer '"+w[1]+"'"); ins.operand=found->second;
                    if(w[0]=="buffer.store.u8") ins.op=EasmOp::StoreU8; else if(w[0]=="buffer.store.i64") ins.op=EasmOp::StoreI64; else if(w[0]=="buffer.store.f64") ins.op=EasmOp::StoreF64; else throw std::runtime_error("unknown buffer store opcode");
                }
                else if (w[0] == "call") {
                    if (w.size() < 3) throw std::runtime_error("call requires destination and import name");
                    ins.op=EasmOp::CallImport; ins.dst=parse_reg(w[1]);
                    const auto found=imports.find(w[2]); if(found==imports.end()) throw std::runtime_error("unknown import '"+w[2]+"'"); ins.operand=found->second;
                    for(std::size_t n=3;n<w.size();++n) ins.args.push_back(parse_reg(w[n]));
                }
                else if (w[0] == "jump") { need(2); ins.op=EasmOp::Jump; pending_jumps.emplace_back(current->code.size(),w[1]); }
                else if (w[0] == "jump_if_false") { need(3); ins.op=EasmOp::JumpIfFalse; ins.a=parse_reg(w[1]); pending_jumps.emplace_back(current->code.size(),w[2]); }
                else if (w[0] == "return") { need(2); ins.op=EasmOp::Return; ins.a=parse_reg(w[1]); }
                else throw std::runtime_error("unknown instruction '" + w[0] + "'");
                current->code.push_back(std::move(ins)); continue;
            }

            const auto w = words(line); if (w.empty()) continue;
            if (w[0] == "buffer") {
                if(program.buffers.size() >= MaxBuffers) throw std::runtime_error("EASM buffer table exceeds safety limit");
                if(w.size()!=4) throw std::runtime_error("buffer syntax: buffer <name> <u8|i64|f64> <elements>");
                if(buffers.contains(w[1])) throw std::runtime_error("duplicate buffer '"+w[1]+"'");
                const auto count=parse_u32_literal(w[3], "buffer element count");
                buffers[w[1]]=static_cast<std::uint32_t>(program.buffers.size()); program.buffers.push_back({w[1],parse_buffer(w[2]),count}); continue;
            }
            if (w[0] == "import") {
                if(program.imports.size() >= MaxImports) throw std::runtime_error("EASM import table exceeds safety limit");
                if(w.size()<6) throw std::runtime_error("import syntax: import <name> <external> <caps> (<types>) -> <type>");
                if(imports.contains(w[1])) throw std::runtime_error("duplicate import '"+w[1]+"'");
                const auto open=line.find('('); if(open==std::string::npos) throw std::runtime_error("import missing signature");
                const auto prefix=words(trim(line.substr(0,open))); if(prefix.size()!=4) throw std::runtime_error("invalid import prefix");
                auto sig=parse_signature_tail(line.substr(open)); validate_interop_external_name(prefix[2]);
                EasmImport entry; entry.internal_name=prefix[1]; entry.external_name=prefix[2]; entry.required_capabilities=parse_interop_capability_spec(prefix[3]); entry.parameters=std::move(sig.parameters); entry.result=sig.result;
                imports[entry.internal_name]=static_cast<std::uint32_t>(program.imports.size()); program.imports.push_back(std::move(entry)); continue;
            }
            if (w[0] == "func") {
                if(program.functions.size() >= MaxFunctions) throw std::runtime_error("EASM function table exceeds safety limit");
                const auto open=line.find('('); const auto regs_pos=line.find(" regs ");
                if(open==std::string::npos||regs_pos==std::string::npos||regs_pos<=open) throw std::runtime_error("func syntax: func <name> (<params>) -> <type> regs (<registers>)");
                const auto prefix=words(trim(line.substr(0,open))); if(prefix.size()!=2) throw std::runtime_error("invalid func prefix");
                if(functions.contains(prefix[1])) throw std::runtime_error("duplicate function '"+prefix[1]+"'");
                auto sig=parse_signature_tail(line.substr(open,regs_pos-open)); auto regs=parse_type_list(trim(line.substr(regs_pos+6)));
                EasmFunction function; function.name=prefix[1]; function.parameters=std::move(sig.parameters); function.result=sig.result; function.registers=std::move(regs);
                functions[function.name]=static_cast<std::uint32_t>(program.functions.size()); program.functions.push_back(std::move(function)); current=&program.functions.back(); labels.clear(); pending_jumps.clear(); continue;
            }
            if (w[0] == "export") {
                if(program.exports.size() >= MaxExports) throw std::runtime_error("EASM export table exceeds safety limit");
                if(w.size()!=3) throw std::runtime_error("export syntax: export <external> <function>");
                validate_interop_external_name(w[1]); const auto found=functions.find(w[2]); if(found==functions.end()) throw std::runtime_error("unknown export function '"+w[2]+"'");
                program.exports.push_back({w[1],found->second}); continue;
            }
            throw std::runtime_error("unknown top-level statement '"+w[0]+"'");
        } catch(const std::exception& error) { parse_error(line_no,error.what()); }
    }
    if(current) parse_error(line_no,"unterminated function; expected end");
    if(!saw_header) throw std::runtime_error("empty EASM source");
    CapabilityMask inferred=0; for(const auto& function:program.functions) for(const auto& ins:function.code) if(ins.op==EasmOp::CallImport && ins.operand<program.imports.size()) inferred|=program.imports[ins.operand].required_capabilities;
    program.required_capabilities=inferred; verify_easm_program(program); return program;
}
void verify_easm_program(const EasmProgram& program) {
    if(program.buffers.size()>MaxBuffers||program.imports.size()>MaxImports||program.functions.size()>MaxFunctions||program.exports.size()>MaxExports)
        throw std::runtime_error("EASM table exceeds safety limit");
    auto valid_scalar=[](EasmScalarType t){return t==EasmScalarType::I64||t==EasmScalarType::F64||t==EasmScalarType::Bool;};
    auto valid_buffer=[](EasmBufferType t){return t==EasmBufferType::U8||t==EasmBufferType::I64||t==EasmBufferType::F64;};
    std::unordered_set<std::string> names; std::uint64_t total_bytes=0;
    for(const auto& buffer:program.buffers){
        if(buffer.name.empty()||!names.insert("b:"+buffer.name).second) throw std::runtime_error("invalid or duplicate EASM buffer name");
        if(!valid_buffer(buffer.type)) throw std::runtime_error("invalid EASM buffer type");
        if(buffer.elements==0) throw std::runtime_error("EASM buffers must contain at least one element");
        const auto bytes=buffer_element_size(buffer.type)*static_cast<std::uint64_t>(buffer.elements);
        if(bytes>MaxBufferBytes||total_bytes>MaxTotalMemoryBytes-bytes)
            throw std::runtime_error("EASM memory exceeds safety limit");
        total_bytes+=bytes;
    }
    std::unordered_set<std::string> external_imports;
    for(const auto& import:program.imports){
        if(import.internal_name.empty()||!names.insert("i:"+import.internal_name).second) throw std::runtime_error("invalid or duplicate EASM import name");
        validate_interop_external_name(import.external_name); if(!external_imports.insert(import.external_name).second) throw std::runtime_error("duplicate EASM external import name");
        validate_capability_mask(import.required_capabilities); if(!valid_scalar(import.result)) throw std::runtime_error("invalid EASM import result type");
        if(import.parameters.size()>MaxRegisters)
            throw std::runtime_error("EASM import arity exceeds safety limit");
        for(auto type:import.parameters)
            if(!valid_scalar(type)) throw std::runtime_error("invalid EASM import parameter type");
    }
    std::unordered_set<std::string> function_names; CapabilityMask inferred=0;
    for(const auto& function:program.functions){
        if(function.name.empty()||!function_names.insert(function.name).second) throw std::runtime_error("invalid or duplicate EASM function name");
        if(function.registers.empty()||function.registers.size()>MaxRegisters||function.parameters.size()>function.registers.size()) throw std::runtime_error("invalid EASM register file");
        if(function.code.empty()||function.code.size()>MaxInstructions) throw std::runtime_error("invalid EASM instruction count");
        if(!valid_scalar(function.result)) throw std::runtime_error("invalid EASM function result type");
        for(std::size_t i=0;i<function.registers.size();++i) if(!valid_scalar(function.registers[i])) throw std::runtime_error("invalid EASM register type");
        for(std::size_t i=0;i<function.parameters.size();++i) if(function.parameters[i]!=function.registers[i]) throw std::runtime_error("EASM parameter registers must occupy matching leading register slots");
        auto reg=[&](std::int32_t r)->EasmScalarType{if(r<0||static_cast<std::size_t>(r)>=function.registers.size()) throw std::runtime_error("EASM instruction references invalid register"); return function.registers[static_cast<std::size_t>(r)];};
        auto expect=[&](std::int32_t r,EasmScalarType t){if(reg(r)!=t) throw std::runtime_error("EASM instruction register type mismatch");};
        bool has_return=false;
        for(const auto& ins:function.code){
            switch(ins.op){
                case EasmOp::ConstI64: expect(ins.dst,EasmScalarType::I64); break; case EasmOp::ConstF64: expect(ins.dst,EasmScalarType::F64); if(!std::isfinite(ins.imm_f64)) throw std::runtime_error("non-finite EASM constant"); break; case EasmOp::ConstBool: expect(ins.dst,EasmScalarType::Bool); if(ins.imm_i64<0||ins.imm_i64>1) throw std::runtime_error("invalid EASM bool constant"); break;
                case EasmOp::Move: if(reg(ins.dst)!=reg(ins.a)) throw std::runtime_error("EASM move type mismatch"); break;
                case EasmOp::AddI64: case EasmOp::SubI64: case EasmOp::MulI64: expect(ins.dst,EasmScalarType::I64); expect(ins.a,EasmScalarType::I64); expect(ins.b,EasmScalarType::I64); break;
                case EasmOp::AddF64: case EasmOp::SubF64: case EasmOp::MulF64: case EasmOp::DivF64: expect(ins.dst,EasmScalarType::F64); expect(ins.a,EasmScalarType::F64); expect(ins.b,EasmScalarType::F64); break;
                case EasmOp::EqI64: case EasmOp::LtI64: expect(ins.dst,EasmScalarType::Bool); expect(ins.a,EasmScalarType::I64); expect(ins.b,EasmScalarType::I64); break;
                case EasmOp::EqF64: case EasmOp::LtF64: expect(ins.dst,EasmScalarType::Bool); expect(ins.a,EasmScalarType::F64); expect(ins.b,EasmScalarType::F64); break;
                case EasmOp::LoadU8: case EasmOp::StoreU8: case EasmOp::LoadI64: case EasmOp::StoreI64: case EasmOp::LoadF64: case EasmOp::StoreF64: {
                    if(ins.operand>=program.buffers.size())
                        throw std::runtime_error("EASM instruction references invalid buffer");
                    const auto type=program.buffers[ins.operand].type;
                    expect(ins.a,EasmScalarType::I64);
                    if(ins.op==EasmOp::LoadU8){if(type!=EasmBufferType::U8)throw std::runtime_error("EASM buffer opcode/type mismatch");expect(ins.dst,EasmScalarType::I64);} else if(ins.op==EasmOp::StoreU8){if(type!=EasmBufferType::U8)throw std::runtime_error("EASM buffer opcode/type mismatch");expect(ins.b,EasmScalarType::I64);}
                    else if(ins.op==EasmOp::LoadI64){if(type!=EasmBufferType::I64)throw std::runtime_error("EASM buffer opcode/type mismatch");expect(ins.dst,EasmScalarType::I64);} else if(ins.op==EasmOp::StoreI64){if(type!=EasmBufferType::I64)throw std::runtime_error("EASM buffer opcode/type mismatch");expect(ins.b,EasmScalarType::I64);}
                    else if(ins.op==EasmOp::LoadF64){if(type!=EasmBufferType::F64)throw std::runtime_error("EASM buffer opcode/type mismatch");expect(ins.dst,EasmScalarType::F64);}
                    else {if(type!=EasmBufferType::F64)throw std::runtime_error("EASM buffer opcode/type mismatch");expect(ins.b,EasmScalarType::F64);}
                    break;
                }
                case EasmOp::CallImport: {
                    if(ins.operand>=program.imports.size())
                        throw std::runtime_error("EASM call references invalid import");
                    const auto& import=program.imports[ins.operand];
                    inferred|=import.required_capabilities;
                    if(ins.args.size()!=import.parameters.size())
                        throw std::runtime_error("EASM import call arity mismatch");
                    if(reg(ins.dst)!=import.result)
                        throw std::runtime_error("EASM import result register type mismatch");
                    for(std::size_t n=0;n<ins.args.size();++n)
                        if(reg(ins.args[n])!=import.parameters[n]) throw std::runtime_error("EASM import argument register type mismatch");
                    break;
                }
                case EasmOp::Jump: if(ins.operand>=function.code.size()) throw std::runtime_error("EASM jump target out of range"); break;
                case EasmOp::JumpIfFalse: expect(ins.a,EasmScalarType::Bool); if(ins.operand>=function.code.size()) throw std::runtime_error("EASM jump target out of range"); break;
                case EasmOp::Return: if(reg(ins.a)!=function.result) throw std::runtime_error("EASM return register type mismatch"); has_return=true; break;
                default: throw std::runtime_error("invalid EASM opcode");
            }
        }
        if(!has_return) throw std::runtime_error("EASM function has no return instruction");
    }
    if(program.required_capabilities!=inferred) throw std::runtime_error("EASM capability metadata does not match import calls");
    validate_capability_mask(program.required_capabilities);
    std::unordered_set<std::string> export_names;
    for(const auto& export_info:program.exports){validate_interop_external_name(export_info.external_name);if(!export_names.insert(export_info.external_name).second)throw std::runtime_error("duplicate EASM external export name");if(export_info.function_index>=program.functions.size())throw std::runtime_error("EASM export references invalid function");}
}
std::string render_easm(const EasmProgram& program) {
    verify_easm_program(program); std::ostringstream out; out.imbue(std::locale::classic()); out << "EASM1\n";
    for(const auto& buffer:program.buffers) out << "buffer "<<buffer.name<<' '<<easm_buffer_type_name(buffer.type)<<' '<<buffer.elements<<'\n';
    for(const auto& import:program.imports) {
        auto caps=capability_mask_string(import.required_capabilities);
        caps.erase(std::remove(caps.begin(),caps.end(),' '),caps.end());
        out << "import "<<import.internal_name<<' '<<import.external_name<<' '<<caps<<' '<<signature_text(import.parameters,import.result)<<'\n';
    }
    auto reg=[](std::int32_t n){return "r"+std::to_string(n);};
    for(const auto& function:program.functions){
        out << "func "<<function.name<<' '<<signature_text(function.parameters,function.result)<<" regs (";
        for(std::size_t n=0;n<function.registers.size();++n){if(n)out<<',';out<<easm_scalar_type_name(function.registers[n]);} out << ")\n";
        std::unordered_map<std::uint32_t,std::string> labels; for(const auto& ins:function.code) if(ins.op==EasmOp::Jump||ins.op==EasmOp::JumpIfFalse) labels.emplace(ins.operand,"L"+std::to_string(ins.operand));
        for(std::size_t ip=0;ip<function.code.size();++ip){
            if(auto it=labels.find(static_cast<std::uint32_t>(ip));it!=labels.end())
                out<<"  label "<<it->second<<'\n';
            const auto& ins=function.code[ip];
            out<<"  ";
            switch(ins.op){
                case EasmOp::ConstI64: out<<"i64.const "<<reg(ins.dst)<<' '<<ins.imm_i64; break; case EasmOp::ConstF64: out<<"f64.const "<<reg(ins.dst)<<' '<<render_f64_literal(ins.imm_f64); break; case EasmOp::ConstBool: out<<"bool.const "<<reg(ins.dst)<<' '<<(ins.imm_i64?"true":"false"); break; case EasmOp::Move: out<<"move "<<reg(ins.dst)<<' '<<reg(ins.a); break;
                case EasmOp::AddI64: case EasmOp::SubI64: case EasmOp::MulI64: case EasmOp::AddF64: case EasmOp::SubF64: case EasmOp::MulF64: case EasmOp::DivF64: case EasmOp::EqI64: case EasmOp::LtI64: case EasmOp::EqF64: case EasmOp::LtF64: out<<easm_opcode_name(ins.op)<<' '<<reg(ins.dst)<<' '<<reg(ins.a)<<' '<<reg(ins.b); break;
                case EasmOp::LoadU8: case EasmOp::LoadI64: case EasmOp::LoadF64: out<<easm_opcode_name(ins.op)<<' '<<reg(ins.dst)<<' '<<program.buffers.at(ins.operand).name<<' '<<reg(ins.a); break;
                case EasmOp::StoreU8: case EasmOp::StoreI64: case EasmOp::StoreF64: out<<easm_opcode_name(ins.op)<<' '<<program.buffers.at(ins.operand).name<<' '<<reg(ins.a)<<' '<<reg(ins.b); break;
                case EasmOp::CallImport: out<<"call "<<reg(ins.dst)<<' '<<program.imports.at(ins.operand).internal_name; for(auto arg:ins.args) out<<' '<<reg(arg); break;
                case EasmOp::Jump: out<<"jump "<<labels.at(ins.operand); break;
                case EasmOp::JumpIfFalse: out<<"jump_if_false "<<reg(ins.a)<<' '<<labels.at(ins.operand); break;
                case EasmOp::Return: out<<"return "<<reg(ins.a); break;
            }
            out<<'\n';
        }
        out<<"end\n";
    }
    for(const auto& export_info:program.exports) out<<"export "<<export_info.external_name<<' '<<program.functions.at(export_info.function_index).name<<'\n';
    return out.str();
}

CapabilityMask easm_export_capabilities(const EasmProgram& program, std::string_view external_name) {
    verify_easm_program(program); const EasmFunction* function=nullptr;
    for(const auto& export_info:program.exports) if(export_info.external_name==external_name){function=&program.functions.at(export_info.function_index);break;}
    if(!function) throw std::runtime_error("unknown EASM export '"+std::string(external_name)+"'");
    CapabilityMask mask=0; for(const auto& ins:function->code) if(ins.op==EasmOp::CallImport) mask|=program.imports.at(ins.operand).required_capabilities; return mask;
}
EasmVM::EasmVM(ExecutionPolicy policy, const InteropRegistry* interop, std::uint64_t fuel)
    : policy_(std::move(policy)), interop_registry_(interop), fuel_(fuel) {
    validate_execution_policy(policy_); if(fuel_==0) throw std::runtime_error("EASM fuel must be positive");
}
void EasmVM::reset_memory(const EasmProgram& program) {
    verify_easm_program(program); memory_.clear(); memory_.reserve(program.buffers.size());
    for(const auto& buffer:program.buffers){
        switch(buffer.type){case EasmBufferType::U8:memory_.emplace_back(std::vector<std::uint8_t>(buffer.elements));break;case EasmBufferType::I64:memory_.emplace_back(std::vector<std::int64_t>(buffer.elements));break;case EasmBufferType::F64:memory_.emplace_back(std::vector<double>(buffer.elements));break;}
    }
    memory_program_=program;
}
void EasmVM::ensure_memory(const EasmProgram& program) {
    if(!memory_program_ || *memory_program_ != program) reset_memory(program);
}
void EasmVM::preflight_function(const EasmProgram& program, const EasmFunction& function) const {
    CapabilityMask required=0; std::vector<bool> checked(program.imports.size(),false);
    for(const auto& ins:function.code) if(ins.op==EasmOp::CallImport){required|=program.imports.at(ins.operand).required_capabilities;checked[ins.operand]=true;}
    require_execution_capabilities(required,policy_);
    for(std::size_t n=0;n<checked.size();++n) if(checked[n]){
        const auto& import=program.imports[n]; if(!interop_registry_) throw std::runtime_error("EASM interop adapter registry is required for '"+import.external_name+"'");
        const auto* binding=interop_registry_->find(import.external_name); if(!binding) throw std::runtime_error("no EASM interop adapter bound for '"+import.external_name+"'");
        if(binding->required_capabilities!=import.required_capabilities) throw std::runtime_error("EASM interop adapter capability contract mismatch for '"+import.external_name+"'");
        if(policy_.mode==ExecutionMode::Deterministic&&!binding->deterministic) throw std::runtime_error("EASM interop adapter is not deterministic: '"+import.external_name+"'");
    }
}
EasmScalar EasmVM::run_function(const EasmProgram& program, const EasmFunction& function,
                                const std::vector<EasmScalar>& arguments) {
    if(arguments.size()!=function.parameters.size()) throw std::runtime_error("EASM export argument count mismatch");
    preflight_function(program,function); ensure_memory(program);
    std::vector<std::optional<EasmScalar>> regs(function.registers.size());
    for(std::size_t n=0;n<arguments.size();++n){require_scalar_type(arguments[n],function.parameters[n]);regs[n]=arguments[n];}
    auto get=[&](std::int32_t r)->const EasmScalar&{if(r<0||static_cast<std::size_t>(r)>=regs.size()||!regs[static_cast<std::size_t>(r)])throw std::runtime_error("EASM read of uninitialized register");return *regs[static_cast<std::size_t>(r)];};
    auto put=[&](std::int32_t r,EasmScalar value){require_scalar_type(value,function.registers.at(static_cast<std::size_t>(r)));regs.at(static_cast<std::size_t>(r))=std::move(value);};
    auto i64=[&](std::int32_t r){return std::get<std::int64_t>(get(r));}; auto f64=[&](std::int32_t r){return std::get<double>(get(r));}; auto boolean=[&](std::int32_t r){return std::get<bool>(get(r));};
    std::uint64_t remaining=fuel_; std::size_t ip=0;
    while(ip<function.code.size()){
        if(remaining==0) throw std::runtime_error("EASM execution fuel exhausted");
        --remaining;
        const auto ins=function.code[ip++];
        switch(ins.op){
            case EasmOp::ConstI64:put(ins.dst,ins.imm_i64);break; case EasmOp::ConstF64:put(ins.dst,ins.imm_f64);break; case EasmOp::ConstBool:put(ins.dst,ins.imm_i64!=0);break; case EasmOp::Move:put(ins.dst,get(ins.a));break;
            case EasmOp::AddI64:{auto a=i64(ins.a),b=i64(ins.b);if((b>0&&a>INT64_MAX-b)||(b<0&&a<INT64_MIN-b))throw std::runtime_error("EASM i64.add overflow");put(ins.dst,a+b);break;}
            case EasmOp::SubI64:{auto a=i64(ins.a),b=i64(ins.b);if((b<0&&a>INT64_MAX+b)||(b>0&&a<INT64_MIN+b))throw std::runtime_error("EASM i64.sub overflow");put(ins.dst,a-b);break;}
            case EasmOp::MulI64:{auto a=i64(ins.a),b=i64(ins.b);if(a!=0&&b!=0){if((a==-1&&b==INT64_MIN)||(b==-1&&a==INT64_MIN))throw std::runtime_error("EASM i64.mul overflow");if(a>0?(b>0?a>INT64_MAX/b:b<INT64_MIN/a):(b>0?a<INT64_MIN/b:a<INT64_MAX/b))throw std::runtime_error("EASM i64.mul overflow");}put(ins.dst,a*b);break;}
            case EasmOp::AddF64:{const auto v=f64(ins.a)+f64(ins.b);if(!std::isfinite(v))throw std::runtime_error("EASM f64.add produced non-finite result");put(ins.dst,v);break;}
            case EasmOp::SubF64:{const auto v=f64(ins.a)-f64(ins.b);if(!std::isfinite(v))throw std::runtime_error("EASM f64.sub produced non-finite result");put(ins.dst,v);break;}
            case EasmOp::MulF64:{const auto v=f64(ins.a)*f64(ins.b);if(!std::isfinite(v))throw std::runtime_error("EASM f64.mul produced non-finite result");put(ins.dst,v);break;}
            case EasmOp::DivF64:{const auto d=f64(ins.b);if(d==0.0)throw std::runtime_error("EASM f64.div by zero");const auto v=f64(ins.a)/d;if(!std::isfinite(v))throw std::runtime_error("EASM f64.div produced non-finite result");put(ins.dst,v);break;}
            case EasmOp::EqI64:put(ins.dst,i64(ins.a)==i64(ins.b));break; case EasmOp::LtI64:put(ins.dst,i64(ins.a)<i64(ins.b));break;
            case EasmOp::EqF64:put(ins.dst,f64(ins.a)==f64(ins.b));break; case EasmOp::LtF64:put(ins.dst,f64(ins.a)<f64(ins.b));break;
            case EasmOp::LoadU8:{auto& data=std::get<std::vector<std::uint8_t>>(memory_.at(ins.operand));put(ins.dst,static_cast<std::int64_t>(data.at(checked_index(get(ins.a),data.size()))));break;}
            case EasmOp::StoreU8:{auto& data=std::get<std::vector<std::uint8_t>>(memory_.at(ins.operand));const auto value=i64(ins.b);if(value<0||value>255)throw std::runtime_error("EASM u8 store value out of range");data.at(checked_index(get(ins.a),data.size()))=static_cast<std::uint8_t>(value);break;}
            case EasmOp::LoadI64:{auto& data=std::get<std::vector<std::int64_t>>(memory_.at(ins.operand));put(ins.dst,data.at(checked_index(get(ins.a),data.size())));break;}
            case EasmOp::StoreI64:{auto& data=std::get<std::vector<std::int64_t>>(memory_.at(ins.operand));data.at(checked_index(get(ins.a),data.size()))=i64(ins.b);break;}
            case EasmOp::LoadF64:{auto& data=std::get<std::vector<double>>(memory_.at(ins.operand));put(ins.dst,data.at(checked_index(get(ins.a),data.size())));break;}
            case EasmOp::StoreF64:{auto& data=std::get<std::vector<double>>(memory_.at(ins.operand));const auto value=f64(ins.b);if(!std::isfinite(value))throw std::runtime_error("EASM f64 store rejects non-finite value");data.at(checked_index(get(ins.a),data.size()))=value;break;}
            case EasmOp::CallImport:{
                const auto& import=program.imports.at(ins.operand);const auto* binding=interop_registry_->find(import.external_name);std::vector<Value> values;values.reserve(ins.args.size());for(auto r:ins.args)values.push_back(value_from_scalar(get(r)));
                try{auto signature=easm_interop_signature(import.parameters,import.result);auto response=binding->invoke(encode_interop_request(signature,values));auto value=decode_interop_response(signature.result,response);put(ins.dst,scalar_from_value(value,import.result));}catch(const std::exception& error){throw std::runtime_error("EASM import '"+import.external_name+"': "+error.what());}break;
            }
            case EasmOp::Jump:ip=ins.operand;break; case EasmOp::JumpIfFalse:if(!boolean(ins.a))ip=ins.operand;break;
            case EasmOp::Return:{const auto result=get(ins.a);require_scalar_type(result,function.result);return result;}
        }
    }
    throw std::runtime_error("EASM function terminated without return");
}
EasmScalar EasmVM::invoke_export(const EasmProgram& program, std::string_view external_name,
                                 const std::vector<EasmScalar>& arguments) {
    verify_easm_program(program);
    for(const auto& export_info:program.exports) if(export_info.external_name==external_name)
        return run_function(program,program.functions.at(export_info.function_index),arguments);
    throw std::runtime_error("unknown EASM export '"+std::string(external_name)+"'");
}

InteropBytes EasmVM::invoke_export_abi(const EasmProgram& program, std::string_view external_name,
                                       std::span<const std::uint8_t> request) {
    try {
        verify_easm_program(program);
        const EasmFunction* function=nullptr;
        for(const auto& export_info:program.exports) if(export_info.external_name==external_name){function=&program.functions.at(export_info.function_index);break;}
        if(!function) throw std::runtime_error("unknown EASM export '"+std::string(external_name)+"'");
        const auto signature=easm_interop_signature(function->parameters,function->result);auto values=decode_interop_request(signature,request);std::vector<EasmScalar> args;args.reserve(values.size());
        for(std::size_t n=0;n<values.size();++n) args.push_back(scalar_from_value(values[n],function->parameters[n]));
        return encode_interop_success(signature.result,value_from_scalar(invoke_export(program,external_name,args)));
    } catch(const std::exception& error) {
        try{return encode_interop_failure(error.what());}catch(const std::exception&){return encode_interop_failure("EASM invocation failed");}
    }
}

bool EasmVM::export_is_deterministic(const EasmProgram& program, std::string_view external_name) const {
    verify_easm_program(program);
    const EasmFunction* function=nullptr;
    for(const auto& export_info:program.exports)
        if(export_info.external_name==external_name){function=&program.functions.at(export_info.function_index);break;}
    if(!function) throw std::runtime_error("unknown EASM export '"+std::string(external_name)+"'");
    for(const auto& ins:function->code) if(ins.op==EasmOp::CallImport){
        if(!interop_registry_) return false;
        const auto& import=program.imports.at(ins.operand);
        const auto* binding=interop_registry_->find(import.external_name);
        if(!binding||binding->required_capabilities!=import.required_capabilities||!binding->deterministic) return false;
    }
    return true;
}

void bind_easm_export(InteropRegistry& registry, std::string binding_external_name,
                      std::shared_ptr<EasmVM> vm, EasmProgram program,
                      std::string export_external_name) {
    if(!vm) throw std::runtime_error("EASM export binding requires a VM");
    auto owned_program=std::make_shared<EasmProgram>(std::move(program));
    const auto required=easm_export_capabilities(*owned_program,export_external_name);
    const bool deterministic=vm->export_is_deterministic(*owned_program,export_external_name);
    registry.bind(std::move(binding_external_name),required,deterministic,
        [vm=std::move(vm),program=std::move(owned_program),name=std::move(export_external_name)](std::span<const std::uint8_t> request){return vm->invoke_export_abi(*program,name,request);});
}

} // namespace emojineer
