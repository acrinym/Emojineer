#include "emojineer/bytecode.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <istream>
#include <iterator>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
namespace emojineer { namespace {
constexpr char Magic[]={'E','M','J','B','C'};constexpr std::uint16_t CurrentVersion=7;constexpr std::uint32_t MaxConstants=1'000'000,MaxFunctions=100'000,MaxInstructions=10'000'000,MaxStringBytes=64*1024*1024,MaxSourceFiles=1'000'000;
void write_u8(std::ostream&o,std::uint8_t v){o.put(static_cast<char>(v));if(!o)throw std::runtime_error("failed to write bytecode");}void write_u16(std::ostream&o,std::uint16_t v){write_u8(o,v&255);write_u8(o,(v>>8)&255);}void write_u32(std::ostream&o,std::uint32_t v){for(unsigned s=0;s<32;s+=8)write_u8(o,(v>>s)&255);}void write_u64(std::ostream&o,std::uint64_t v){for(unsigned s=0;s<64;s+=8)write_u8(o,(v>>s)&255);}std::uint8_t read_u8(std::istream&i){int v=i.get();if(v==std::char_traits<char>::eof())throw std::runtime_error("truncated bytecode");return static_cast<std::uint8_t>(v);}std::uint16_t read_u16(std::istream&i){return static_cast<std::uint16_t>(read_u8(i))|static_cast<std::uint16_t>(read_u8(i)<<8);}std::uint32_t read_u32(std::istream&i){std::uint32_t v=0;for(unsigned s=0;s<32;s+=8)v|=static_cast<std::uint32_t>(read_u8(i))<<s;return v;}std::uint64_t read_u64(std::istream&i){std::uint64_t v=0;for(unsigned s=0;s<64;s+=8)v|=static_cast<std::uint64_t>(read_u8(i))<<s;return v;}
void write_string(std::ostream&o,const std::string&v){if(v.size()>MaxStringBytes)throw std::runtime_error("string too large for bytecode");write_u32(o,static_cast<std::uint32_t>(v.size()));o.write(v.data(),static_cast<std::streamsize>(v.size()));if(!o)throw std::runtime_error("failed to write bytecode string");}std::string read_string(std::istream&i){auto n=read_u32(i);if(n>MaxStringBytes)throw std::runtime_error("bytecode string exceeds safety limit");std::string v(n,'\0');i.read(v.data(),static_cast<std::streamsize>(n));if(!i)throw std::runtime_error("truncated bytecode string");return v;}
OpCode decode_v1(std::uint8_t raw){switch(raw){case 0:return OpCode::Constant;case 1:return OpCode::LoadGlobal;case 2:return OpCode::StoreGlobal;case 3:return OpCode::AssertNumber;case 4:return OpCode::AssertString;case 5:return OpCode::AssertBool;case 6:return OpCode::Add;case 7:return OpCode::Subtract;case 8:return OpCode::Multiply;case 9:return OpCode::Divide;case 10:return OpCode::Modulo;case 11:return OpCode::AddInt;case 12:return OpCode::SubtractInt;case 13:return OpCode::MultiplyInt;case 14:return OpCode::Equal;case 15:return OpCode::Less;case 16:return OpCode::Greater;case 17:return OpCode::Negate;case 18:return OpCode::Not;case 19:return OpCode::ReadLine;case 20:return OpCode::Print;case 21:return OpCode::JumpIfFalse;case 22:return OpCode::Jump;case 23:return OpCode::Halt;default:throw std::runtime_error("invalid opcode in v1 bytecode");}}
bool portable_source_identity(const std::string& identity) {
    if (identity.empty()) return true;
    if (std::filesystem::path(identity).is_absolute()) return false;
    if (identity.size() >= 3 && std::isalpha(static_cast<unsigned char>(identity[0])) &&
        identity[1] == ':' && (identity[2] == '/' || identity[2] == '\\')) return false;
    return true;
}
bool valid_sha256(const std::string& hash) {
    if (hash.size() != 64) return false;
    return std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}
std::string array_to_string(const ArrayPtr&a){if(!a)return"<null-array>";std::ostringstream o;o<<'[';for(std::size_t i=0;i<a->elements.size();++i){if(i)o<<", ";o<<value_to_string(a->elements[i]);}o<<']';return o.str();}
} // namespace
std::int32_t Chunk::add_constant(Value value){if(constants.size()>=MaxConstants)throw std::runtime_error("too many constants");if(std::holds_alternative<ArrayPtr>(value))throw std::runtime_error("arrays cannot be stored in the bytecode constant pool");constants.push_back(std::move(value));return static_cast<std::int32_t>(constants.size()-1);}
void verify_bytecode(const Chunk& c) {
    if (c.constants.size() > MaxConstants || c.functions.size() > MaxFunctions || c.code.size() > MaxInstructions)
        throw std::runtime_error("bytecode exceeds safety limit");
    for (const auto& value : c.constants)
        if (std::holds_alternative<ArrayPtr>(value))
            throw std::runtime_error("array found in bytecode constant pool");

    std::unordered_set<std::string> names;
    for (const auto& f : c.functions) {
        if (f.entry >= c.code.size()) throw std::runtime_error("function entry is outside bytecode");
        if (f.arity > f.local_count) throw std::runtime_error("function arity exceeds local count");
        if (!f.parameter_names.empty() && f.parameter_names.size() != f.arity)
            throw std::runtime_error("function parameter-name metadata does not match arity");
        if (!f.local_names.empty() && f.local_names.size() != f.local_count)
            throw std::runtime_error("function local-name metadata does not match local count");
        if (!names.insert(f.name).second) throw std::runtime_error("duplicate function metadata");
    }

    for (const auto& ins : c.code) {
        auto valid_const = [&] { return ins.operand >= 0 && static_cast<std::size_t>(ins.operand) < c.constants.size(); };
        switch (ins.op) {
            case OpCode::Constant:
                if (!valid_const()) throw std::runtime_error("invalid constant operand");
                break;
            case OpCode::LoadGlobal:
            case OpCode::StoreGlobal:
                if (!valid_const() || !std::holds_alternative<std::string>(c.constants[static_cast<std::size_t>(ins.operand)]))
                    throw std::runtime_error("global operand must reference a string constant");
                break;
            case OpCode::LoadLocal:
            case OpCode::StoreLocal:
                if (ins.operand < 0) throw std::runtime_error("negative local slot");
                break;
            case OpCode::Jump:
            case OpCode::JumpIfFalse:
                if (ins.operand < 0 || static_cast<std::size_t>(ins.operand) > c.code.size())
                    throw std::runtime_error("invalid jump target");
                break;
            case OpCode::Call:
                if (ins.operand < 0 || static_cast<std::size_t>(ins.operand) >= c.functions.size())
                    throw std::runtime_error("invalid function index");
                break;
            case OpCode::MakeArray:
                if (ins.operand < 0) throw std::runtime_error("negative array element count");
                break;
            default:
                break;
        }
    }

    if (!c.source_map.empty()) {
        if (c.source_map.size() != c.code.size())
            throw std::runtime_error("source map cardinality does not match instruction stream");
        for (const auto& src : c.source_map) {
            if (!portable_source_identity(src.source_path))
                throw std::runtime_error("source map contains an absolute checkout-specific identity");
            if (src.line == 0 || src.column == 0 || src.end_line == 0 || src.end_column == 0)
                throw std::runtime_error("source map positions must be 1-based");
            if (src.end_line < src.line || (src.end_line == src.line && src.end_column < src.column))
                throw std::runtime_error("source map contains a reversed source range");
        }
    }

    if (c.source_hashes.size() > MaxSourceFiles)
        throw std::runtime_error("source hash table exceeds safety limit");
    for (const auto& [identity, hash] : c.source_hashes) {
        if (identity.empty() || !portable_source_identity(identity))
            throw std::runtime_error("source hash table contains an invalid deterministic identity");
        if (!valid_sha256(hash)) throw std::runtime_error("source hash table contains an invalid SHA-256 digest");
    }
}
void write_bytecode(const Chunk& c, std::ostream& o) {
    verify_bytecode(c);
    o.write(Magic, sizeof(Magic));
    if (!o) throw std::runtime_error("failed to write bytecode header");
    write_u16(o, CurrentVersion);
    write_u32(o, static_cast<std::uint32_t>(c.constants.size()));
    for (const auto& v : c.constants) {
        if (auto* n = std::get_if<double>(&v)) { write_u8(o, 1); write_u64(o, std::bit_cast<std::uint64_t>(*n)); }
        else if (auto* integer = std::get_if<std::int64_t>(&v)) { write_u8(o, 4); write_u64(o, static_cast<std::uint64_t>(*integer)); }
        else if (auto* b = std::get_if<bool>(&v)) { write_u8(o, 2); write_u8(o, *b ? 1 : 0); }
        else if (auto* s = std::get_if<std::string>(&v)) { write_u8(o, 3); write_string(o, *s); }
        else throw std::runtime_error("unsupported constant value in bytecode");
    }
    write_u32(o, static_cast<std::uint32_t>(c.functions.size()));
    for (const auto& f : c.functions) {
        write_string(o, f.name); write_u32(o, f.entry); write_u32(o, f.arity); write_u32(o, f.local_count);
        write_u32(o, static_cast<std::uint32_t>(f.parameter_names.size()));
        for (const auto& name : f.parameter_names) write_string(o, name);
        write_u32(o, static_cast<std::uint32_t>(f.local_names.size()));
        for (const auto& name : f.local_names) write_string(o, name);
    }
    write_u32(o, static_cast<std::uint32_t>(c.code.size()));
    for (const auto& ins : c.code) {
        write_u8(o, static_cast<std::uint8_t>(ins.op));
        write_u32(o, std::bit_cast<std::uint32_t>(ins.operand));
        write_u32(o, ins.line);
    }
    // v6+ source map: deterministic source identity, exact range, and function context.
    write_u32(o, static_cast<std::uint32_t>(c.source_map.size()));
    for (const auto& src : c.source_map) {
        write_string(o, src.source_path); write_u32(o, src.line); write_u32(o, src.column);
        write_u32(o, src.end_line); write_u32(o, src.end_column); write_string(o, src.function_name);
    }
    // v7 provenance table. Sort identities so bytecode output is deterministic.
    std::vector<std::pair<std::string, std::string>> hashes(c.source_hashes.begin(), c.source_hashes.end());
    std::sort(hashes.begin(), hashes.end());
    write_u32(o, static_cast<std::uint32_t>(hashes.size()));
    for (const auto& [identity, hash] : hashes) { write_string(o, identity); write_string(o, hash); }
}
Chunk read_bytecode(std::istream& i) {
    char magic[sizeof(Magic)]{};
    i.read(magic, sizeof(magic));
    if (!i || !std::equal(std::begin(magic), std::end(magic), std::begin(Magic)))
        throw std::runtime_error("not an Emojineer bytecode file");
    const auto version = read_u16(i);
    if (version < 1 || version > CurrentVersion)
        throw std::runtime_error("unsupported Emojineer bytecode version " + std::to_string(version));

    Chunk c;
    const auto nc = read_u32(i);
    if (nc > MaxConstants) throw std::runtime_error("bytecode constant pool exceeds safety limit");
    for (std::uint32_t k = 0; k < nc; ++k) {
        switch (read_u8(i)) {
            case 1: c.constants.emplace_back(std::bit_cast<double>(read_u64(i))); break;
            case 2: { auto b = read_u8(i); if (b > 1) throw std::runtime_error("invalid boolean constant in bytecode"); c.constants.emplace_back(b != 0); break; }
            case 3: c.constants.emplace_back(read_string(i)); break;
            case 4: c.constants.emplace_back(static_cast<std::int64_t>(read_u64(i))); break;
            default: throw std::runtime_error("invalid constant tag in bytecode");
        }
    }

    if (version >= 2) {
        const auto nf = read_u32(i);
        if (nf > MaxFunctions) throw std::runtime_error("bytecode function table exceeds safety limit");
        for (std::uint32_t k = 0; k < nf; ++k) {
            FunctionInfo f;
            f.name = read_string(i); f.entry = read_u32(i); f.arity = read_u32(i); f.local_count = read_u32(i);
            if (f.arity > f.local_count) throw std::runtime_error("function arity exceeds local count");
            if (version >= 5) {
                const auto np = read_u32(i);
                if (np > f.arity) throw std::runtime_error("parameter-name metadata exceeds function arity");
                for (std::uint32_t p = 0; p < np; ++p) f.parameter_names.push_back(read_string(i));
                const auto nl = read_u32(i);
                if (nl > f.local_count) throw std::runtime_error("local-name metadata exceeds function local count");
                for (std::uint32_t l = 0; l < nl; ++l) f.local_names.push_back(read_string(i));
            }
            c.functions.push_back(std::move(f));
        }
    }

    const auto ni = read_u32(i);
    if (ni > MaxInstructions) throw std::runtime_error("bytecode instruction stream exceeds safety limit");
    for (std::uint32_t k = 0; k < ni; ++k) {
        const auto raw = read_u8(i);
        OpCode op;
        if (version == 1) op = decode_v1(raw);
        else {
            const auto max = version == 2 ? static_cast<std::uint8_t>(OpCode::Halt) : static_cast<std::uint8_t>(OpCode::SetIndex);
            if (raw > max) throw std::runtime_error("invalid opcode in bytecode");
            op = static_cast<OpCode>(raw);
        }
        c.code.push_back({op, std::bit_cast<std::int32_t>(read_u32(i)), read_u32(i)});
    }

    // v6 has exact ranges/context; v4-v5 degrade deterministically to point locations.
    if (version >= 6) {
        const auto ns = read_u32(i);
        if (ns > MaxInstructions || ns != ni) throw std::runtime_error("source map count does not match instruction stream");
        for (std::uint32_t k = 0; k < ns; ++k) {
            SourceLocation src;
            src.source_path = read_string(i); src.line = read_u32(i); src.column = read_u32(i);
            src.end_line = read_u32(i); src.end_column = read_u32(i); src.function_name = read_string(i);
            c.source_map.push_back(std::move(src));
        }
    } else if (version >= 4) {
        const auto ns = read_u32(i);
        if (ns > MaxInstructions || ns != ni) throw std::runtime_error("source map count does not match instruction stream");
        for (std::uint32_t k = 0; k < ns; ++k) {
            SourceLocation src;
            src.source_path = read_string(i); src.line = read_u32(i); src.column = read_u32(i);
            src.end_line = src.line; src.end_column = src.column;
            c.source_map.push_back(std::move(src));
        }
    }

    if (version >= 7) {
        const auto nh = read_u32(i);
        if (nh > MaxSourceFiles) throw std::runtime_error("source hash table exceeds safety limit");
        for (std::uint32_t k = 0; k < nh; ++k) {
            auto identity = read_string(i);
            auto hash = read_string(i);
            if (!c.source_hashes.emplace(std::move(identity), std::move(hash)).second)
                throw std::runtime_error("duplicate source hash identity in bytecode");
        }
    }

    verify_bytecode(c);
    return c;
}
std::string opcode_name(OpCode op){switch(op){case OpCode::Constant:return"Constant";case OpCode::LoadGlobal:return"LoadGlobal";case OpCode::StoreGlobal:return"StoreGlobal";case OpCode::LoadLocal:return"LoadLocal";case OpCode::StoreLocal:return"StoreLocal";case OpCode::AssertNumber:return"AssertNumber";case OpCode::AssertString:return"AssertString";case OpCode::AssertBool:return"AssertBool";case OpCode::Add:return"Add";case OpCode::Subtract:return"Subtract";case OpCode::Multiply:return"Multiply";case OpCode::Divide:return"Divide";case OpCode::Modulo:return"Modulo";case OpCode::AddInt:return"AddInt";case OpCode::SubtractInt:return"SubtractInt";case OpCode::MultiplyInt:return"MultiplyInt";case OpCode::Equal:return"Equal";case OpCode::Less:return"Less";case OpCode::Greater:return"Greater";case OpCode::Negate:return"Negate";case OpCode::Not:return"Not";case OpCode::ReadLine:return"ReadLine";case OpCode::Print:return"Print";case OpCode::JumpIfFalse:return"JumpIfFalse";case OpCode::Jump:return"Jump";case OpCode::Call:return"Call";case OpCode::Return:return"Return";case OpCode::Halt:return"Halt";case OpCode::AssertArray:return"AssertArray";case OpCode::MakeArray:return"MakeArray";case OpCode::Index:return"Index";case OpCode::Length:return"Length";case OpCode::Append:return"Append";case OpCode::SetIndex:return"SetIndex";}return"Unknown";}
std::string value_to_string(const Value&v){if(auto*n=std::get_if<double>(&v)){std::ostringstream o;if(std::isfinite(*n)&&std::floor(*n)==*n)o<<std::fixed<<std::setprecision(0)<<*n;else o<<std::setprecision(15)<<*n;return o.str();}if(auto*i=std::get_if<std::int64_t>(&v))return std::to_string(*i);if(auto*b=std::get_if<bool>(&v))return*b?"✅":"❌";if(auto*s=std::get_if<std::string>(&v))return*s;return array_to_string(std::get<ArrayPtr>(v));}
bool values_equal(const Value&a,const Value&b){if(a.index()!=b.index())return false;if(auto*x=std::get_if<std::int64_t>(&a))return*x==std::get<std::int64_t>(b);if(auto*x=std::get_if<double>(&a))return*x==std::get<double>(b);if(auto*x=std::get_if<bool>(&a))return*x==std::get<bool>(b);if(auto*x=std::get_if<std::string>(&a))return*x==std::get<std::string>(b);auto x=std::get<ArrayPtr>(a),y=std::get<ArrayPtr>(b);if(!x||!y)return x==y;if(x->elements.size()!=y->elements.size())return false;for(std::size_t i=0;i<x->elements.size();++i)if(!values_equal(x->elements[i],y->elements[i]))return false;return true;}
} // namespace emojineer
