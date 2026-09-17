#include "emojineer/interop.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cctype>
#include <limits>
#include <stdexcept>
namespace emojineer { namespace {
constexpr std::size_t MaxPayload=1024*1024,MaxString=256*1024,MaxDepth=64,MaxElements=100'000;
constexpr char RequestMagic[]={'E','M','J','A','B','I','1','Q'};
constexpr char ResponseMagic[]={'E','M','J','A','B','I','1','R'};
enum class ValueTag:std::uint8_t{Integer=1,Number=2,Bool=3,String=4,Array=5};
struct Writer{
    InteropBytes bytes;
    void room(std::size_t n){if(n>MaxPayload||bytes.size()>MaxPayload-n)throw std::runtime_error("interop ABI payload exceeds 1 MiB limit");}
    void u8(std::uint8_t v){room(1);bytes.push_back(v);} void u32(std::uint32_t v){room(4);for(unsigned s=0;s<32;s+=8)bytes.push_back(static_cast<std::uint8_t>((v>>s)&255));}
    void u64(std::uint64_t v){room(8);for(unsigned s=0;s<64;s+=8)bytes.push_back(static_cast<std::uint8_t>((v>>s)&255));}
    void raw(const char*p,std::size_t n){room(n);bytes.insert(bytes.end(),p,p+n);} void text(std::string_view s){if(s.size()>MaxString)throw std::runtime_error("interop ABI text exceeds 256 KiB limit");u32(static_cast<std::uint32_t>(s.size()));raw(s.data(),s.size());}
};
struct Reader{
    std::span<const std::uint8_t> bytes;std::size_t pos{0};explicit Reader(std::span<const std::uint8_t>b):bytes(b){if(b.size()>MaxPayload)throw std::runtime_error("interop ABI payload exceeds 1 MiB limit");}
    void need(std::size_t n){if(pos>bytes.size()||n>bytes.size()-pos)throw std::runtime_error("truncated interop ABI payload");}
    std::uint8_t u8(){need(1);return bytes[pos++];} std::uint32_t u32(){need(4);std::uint32_t v=0;for(unsigned s=0;s<32;s+=8)v|=static_cast<std::uint32_t>(bytes[pos++])<<s;return v;}
    std::uint64_t u64(){need(8);std::uint64_t v=0;for(unsigned s=0;s<64;s+=8)v|=static_cast<std::uint64_t>(bytes[pos++])<<s;return v;}
    std::string text(){auto n=u32();if(n>MaxString)throw std::runtime_error("interop ABI text exceeds 256 KiB limit");need(n);std::string s(reinterpret_cast<const char*>(bytes.data()+pos),n);pos+=n;return s;}
    void magic(const char*p,std::size_t n){need(n);if(!std::equal(p,p+n,bytes.begin()+static_cast<std::ptrdiff_t>(pos)))throw std::runtime_error("invalid interop ABI envelope");pos+=n;} void end(){if(pos!=bytes.size())throw std::runtime_error("trailing interop ABI payload");}
};
void encode_value(Writer&w,const Value&v,std::size_t depth,std::size_t&elements){
    if(depth>MaxDepth)throw std::runtime_error("interop ABI nesting exceeds 64 levels");
    if(++elements>MaxElements)throw std::runtime_error("interop ABI aggregate exceeds 100000 values");
    if(auto p=std::get_if<std::int64_t>(&v)){w.u8(static_cast<std::uint8_t>(ValueTag::Integer));w.u64(std::bit_cast<std::uint64_t>(*p));return;}
    if(auto p=std::get_if<double>(&v)){if(!std::isfinite(*p))throw std::runtime_error("interop ABI rejects non-finite numbers");w.u8(static_cast<std::uint8_t>(ValueTag::Number));w.u64(std::bit_cast<std::uint64_t>(*p));return;}
    if(auto p=std::get_if<bool>(&v)){w.u8(static_cast<std::uint8_t>(ValueTag::Bool));w.u8(*p?1:0);return;} if(auto p=std::get_if<std::string>(&v)){w.u8(static_cast<std::uint8_t>(ValueTag::String));w.text(*p);return;}
    auto a=std::get<ArrayPtr>(v);if(!a)throw std::runtime_error("interop ABI rejects null arrays");if(a->elements.size()>MaxElements)throw std::runtime_error("interop ABI array exceeds element limit");w.u8(static_cast<std::uint8_t>(ValueTag::Array));w.u32(static_cast<std::uint32_t>(a->elements.size()));for(const auto&e:a->elements)encode_value(w,e,depth+1,elements);
}
Value decode_value(Reader&r,std::size_t depth,std::size_t&elements){
    if(depth>MaxDepth)throw std::runtime_error("interop ABI nesting exceeds 64 levels");
    if(++elements>MaxElements)throw std::runtime_error("interop ABI aggregate exceeds 100000 values");
    switch(static_cast<ValueTag>(r.u8())){case ValueTag::Integer:return std::bit_cast<std::int64_t>(r.u64());case ValueTag::Number:{double d=std::bit_cast<double>(r.u64());if(!std::isfinite(d))throw std::runtime_error("interop ABI rejects non-finite numbers");return d;}case ValueTag::Bool:{auto b=r.u8();if(b>1)throw std::runtime_error("invalid interop ABI boolean");return b!=0;}case ValueTag::String:return r.text();case ValueTag::Array:{auto n=r.u32();if(n>MaxElements)throw std::runtime_error("interop ABI array exceeds element limit");auto a=std::make_shared<ArrayValue>();a->elements.reserve(n);for(std::uint32_t i=0;i<n;++i)a->elements.push_back(decode_value(r,depth+1,elements));return a;}}throw std::runtime_error("invalid interop ABI value tag");
}
bool type_matches(const Value&v,InteropType t){switch(t){case InteropType::Number:return std::holds_alternative<std::int64_t>(v)||std::holds_alternative<double>(v);case InteropType::String:return std::holds_alternative<std::string>(v);case InteropType::Bool:return std::holds_alternative<bool>(v);case InteropType::Array:{auto p=std::get_if<ArrayPtr>(&v);return p&&*p;}}return false;}
std::string trim(std::string_view s){std::size_t a=0,b=s.size();while(a<b&&std::isspace(static_cast<unsigned char>(s[a])))++a;while(b>a&&std::isspace(static_cast<unsigned char>(s[b-1])))--b;return std::string(s.substr(a,b-a));}
} // namespace
void InteropRegistry::bind(std::string name,CapabilityMask caps,bool deterministic,InteropAdapter adapter){validate_interop_external_name(name);validate_capability_mask(caps);if(!adapter)throw std::runtime_error("interop adapter callback cannot be empty");if(!bindings_.emplace(std::move(name),InteropAdapterBinding{caps,deterministic,std::move(adapter)}).second)throw std::runtime_error("duplicate interop adapter binding");}
const InteropAdapterBinding* InteropRegistry::find(std::string_view name)const{auto it=bindings_.find(std::string(name));return it==bindings_.end()?nullptr:&it->second;}
std::string interop_type_name(InteropType t){switch(t){case InteropType::Number:return"number";case InteropType::String:return"text";case InteropType::Bool:return"bool";case InteropType::Array:return"array";}throw std::runtime_error("invalid interop type");}
void validate_interop_external_name(std::string_view name){if(name.empty()||name.size()>256)throw std::runtime_error("interop external name must contain 1..256 bytes");for(unsigned char c:name)if(c<0x21||c>0x7e)throw std::runtime_error("interop external name must use printable ASCII without spaces");}
CapabilityMask parse_interop_capability_spec(std::string_view raw){auto spec=trim(raw);if(spec=="none")return 0;if(spec=="all")return all_capabilities_mask();if(spec.empty())throw std::runtime_error("interop capability spec cannot be empty");CapabilityMask mask=0;std::size_t start=0;while(start<=spec.size()){auto comma=spec.find(',',start);auto item=trim(std::string_view(spec).substr(start,comma==std::string::npos?std::string::npos:comma-start));if(item.empty())throw std::runtime_error("interop capability spec contains an empty name");auto cap=parse_capability(item);if(!cap)throw std::runtime_error("unknown interop capability '"+item+"'");mask|=capability_mask(*cap);if(comma==std::string::npos)break;start=comma+1;}return mask;}
void validate_interop_value(const Value&v,InteropType t){if(!type_matches(v,t))throw std::runtime_error("interop value requires "+interop_type_name(t));}
InteropBytes encode_interop_request(const InteropSignature&sig,const std::vector<Value>&args){if(args.size()!=sig.parameters.size())throw std::runtime_error("interop argument count does not match signature");Writer w;w.raw(RequestMagic,sizeof(RequestMagic));w.u32(static_cast<std::uint32_t>(args.size()));std::size_t count=0;for(std::size_t i=0;i<args.size();++i){validate_interop_value(args[i],sig.parameters[i]);encode_value(w,args[i],0,count);}return std::move(w.bytes);}
std::vector<Value> decode_interop_request(const InteropSignature&sig,std::span<const std::uint8_t>bytes){Reader r(bytes);r.magic(RequestMagic,sizeof(RequestMagic));auto n=r.u32();if(n!=sig.parameters.size())throw std::runtime_error("interop request argument count does not match signature");std::vector<Value> out;out.reserve(n);std::size_t count=0;for(std::uint32_t i=0;i<n;++i){auto v=decode_value(r,0,count);validate_interop_value(v,sig.parameters[i]);out.push_back(std::move(v));}r.end();return out;}
InteropBytes encode_interop_success(InteropType t,const Value&v){validate_interop_value(v,t);Writer w;w.raw(ResponseMagic,sizeof(ResponseMagic));w.u8(0);std::size_t count=0;encode_value(w,v,0,count);return std::move(w.bytes);}
InteropBytes encode_interop_failure(std::string_view message){Writer w;w.raw(ResponseMagic,sizeof(ResponseMagic));w.u8(1);w.text(message);return std::move(w.bytes);}
Value decode_interop_response(InteropType t,std::span<const std::uint8_t>bytes){Reader r(bytes);r.magic(ResponseMagic,sizeof(ResponseMagic));auto status=r.u8();if(status==1){auto message=r.text();r.end();throw std::runtime_error("adapter failure: "+message);}if(status!=0)throw std::runtime_error("invalid interop ABI response status");std::size_t count=0;auto value=decode_value(r,0,count);validate_interop_value(value,t);r.end();return value;}
} // namespace emojineer
