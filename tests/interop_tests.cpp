#include "emojineer/bytecode.hpp"
#include "emojineer/capability.hpp"
#include "emojineer/compiler.hpp"
#include "emojineer/interop.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/module.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/vm.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
namespace {
void require(bool c,const std::string&m){if(!c)throw std::runtime_error("interop test failed: "+m);}
template<class F>std::string expect_error(F&&f,const std::string&needle){try{f();}catch(const std::exception&e){std::string m=e.what();require(m.find(needle)!=std::string::npos,"expected '"+needle+"', got '"+m+"'");return m;}throw std::runtime_error("interop test failed: expected error containing '"+needle+"'");}
emojineer::Chunk compile_text(const std::string&s){emojineer::Lexer l(s);emojineer::Parser p(l.tokenize());emojineer::Compiler c;return c.compile(p.parse());}
struct TempRoot{std::filesystem::path path;TempRoot(){path=std::filesystem::temp_directory_path()/ ("emojineer-interop-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(path);}~TempRoot(){std::error_code e;std::filesystem::remove_all(path,e);}};
void write_text(const std::filesystem::path&p,const std::string&s){std::ofstream o(p,std::ios::binary);if(!o)throw std::runtime_error("cannot write test source");o<<s;}

const std::string import_number = R"(🔌 🧮 📜fixture.double📜 📜none📜 🔢 🫴 🔢 🤲
📝 🧮 🫴 21 🤲
)";

void test_compile_and_adapter_success(){
 auto c=compile_text(import_number);require(c.interop_imports.size()==1,"one import metadata row");require(c.interop_exports.empty(),"no exports");require(c.required_capabilities==0,"pure adapter requires no host capability");
 std::size_t calls=0;for(const auto&i:c.code)if(i.op==emojineer::OpCode::InteropCall)++calls;require(calls==1,"call lowers to InteropCall");
 emojineer::InteropRegistry registry;registry.bind("fixture.double",0,true,[&](std::span<const std::uint8_t> bytes){auto args=emojineer::decode_interop_request(c.interop_imports[0].signature,bytes);require(args.size()==1,"adapter sees one argument");double n=std::get<double>(args[0]);return emojineer::encode_interop_success(emojineer::InteropType::Number,n*2);});
 std::istringstream in;std::ostringstream out;emojineer::VM vm(in,out,1'000'000,{},&registry);vm.execute(c);require(out.str()=="42\n","adapter result returns through VM");
}

void test_v9_roundtrip_and_verifier_binding(){
 auto c=compile_text(R"(🔌 🧮 📜fixture.net📜 📜network📜 🔢 🫴 🔢 🤲
📝 🧮 🫴 2 🤲
)");
 require(c.required_capabilities==emojineer::capability_mask(emojineer::Capability::Network),"interop call contributes capability mask");
 std::ostringstream encoded(std::ios::binary);emojineer::write_bytecode(c,encoded);auto bytes=encoded.str();require(static_cast<unsigned char>(bytes[5])==10&&static_cast<unsigned char>(bytes[6])==0,"current writer emits EMJBC v10 while preserving v9 interop tables");
 std::istringstream input(bytes,std::ios::binary);auto decoded=emojineer::read_bytecode(input);require(decoded.interop_imports.size()==1,"import survives roundtrip");require(decoded.interop_imports[0].external_name=="fixture.net","external name survives roundtrip");
 auto v9=bytes;v9[5]=9;v9[6]=0;std::istringstream v9_input(v9,std::ios::binary);auto decoded_v9=emojineer::read_bytecode(v9_input);require(decoded_v9.interop_imports.size()==1&&decoded_v9.interop_imports[0].external_name=="fixture.net","reader must preserve real EMJBC v9 interop compatibility");
 auto trailing=bytes+std::string("x",1);std::istringstream trailing_input(trailing,std::ios::binary);expect_error([&]{(void)emojineer::read_bytecode(trailing_input);},"trailing content");
 auto dishonest=c;dishonest.required_capabilities=0;expect_error([&]{emojineer::verify_bytecode(dishonest);},"does not match");
 auto invalid=c;for(auto&i:invalid.code)if(i.op==emojineer::OpCode::InteropCall){i.operand=999;break;}expect_error([&]{emojineer::verify_bytecode(invalid);},"invalid interop import operand");
}

void test_preflight_and_binding_contract(){
 auto c=compile_text(R"(🔌 🧮 📜fixture.net📜 📜network📜 🔢 🫴 🔢 🤲
📝 📜before📜
📝 🧮 🫴 3 🤲
)");
 int invoked=0;emojineer::InteropRegistry registry;registry.bind("fixture.net",emojineer::capability_mask(emojineer::Capability::Network),false,[&](auto bytes){++invoked;auto a=emojineer::decode_interop_request(c.interop_imports[0].signature,bytes);return emojineer::encode_interop_success(emojineer::InteropType::Number,a[0]);});
 std::istringstream in;std::ostringstream denied_out;emojineer::VM denied(in,denied_out,1'000'000,{},&registry);expect_error([&]{denied.execute(c);},"network");require(denied_out.str().empty()&&invoked==0,"denial occurs before bytecode and callback effects");
 emojineer::ExecutionPolicy policy;policy.grants=emojineer::capability_mask(emojineer::Capability::Network);std::ostringstream granted_out;emojineer::VM granted(in,granted_out,1'000'000,policy,&registry);granted.execute(c);require(granted_out.str()=="before\n3\n"&&invoked==1,"explicit grant permits adapter");
 auto pure=compile_text(import_number);emojineer::InteropRegistry mismatch;mismatch.bind("fixture.double",emojineer::capability_mask(emojineer::Capability::Network),false,[&](auto){return emojineer::encode_interop_success(emojineer::InteropType::Number,1.0);});std::ostringstream mismatch_out;emojineer::VM mismatch_vm(in,mismatch_out,1'000'000,{},&mismatch);expect_error([&]{mismatch_vm.execute(pure);},"capability contract mismatch");require(mismatch_out.str().empty(),"binding mismatch is preflighted");
}

void test_missing_and_deterministic_binding(){
 auto c=compile_text(import_number);std::istringstream in;std::ostringstream out;emojineer::VM missing(in,out);expect_error([&]{missing.execute(c);},"registry is required");require(out.str().empty(),"missing adapter fails before program output");
 emojineer::ExecutionPolicy deterministic;deterministic.mode=emojineer::ExecutionMode::Deterministic;emojineer::InteropRegistry nondet;nondet.bind("fixture.double",0,false,[&](auto){return emojineer::encode_interop_success(emojineer::InteropType::Number,42.0);});std::ostringstream no;emojineer::VM no_vm(in,no,1'000'000,deterministic,&nondet);expect_error([&]{no_vm.execute(c);},"not deterministic");
 emojineer::InteropRegistry det;det.bind("fixture.double",0,true,[&](auto bytes){auto a=emojineer::decode_interop_request(c.interop_imports[0].signature,bytes);return emojineer::encode_interop_success(emojineer::InteropType::Number,std::get<double>(a[0])*2);});std::ostringstream yes;emojineer::VM yes_vm(in,yes,1'000'000,deterministic,&det);yes_vm.execute(c);require(yes.str()=="42\n","deterministic adapter runs in deterministic mode");
}

void test_abi_roundtrip_and_bounds(){
 emojineer::InteropSignature sig{{emojineer::InteropType::Array},emojineer::InteropType::Array};auto inner=std::make_shared<emojineer::ArrayValue>();inner->elements={std::int64_t{7},std::int64_t{-9223372036854775807LL-1},2.5,true,std::string("hi")};auto outer=std::make_shared<emojineer::ArrayValue>();outer->elements={inner,std::string("tail")};std::vector<emojineer::Value> args{outer};auto request=emojineer::encode_interop_request(sig,args);auto decoded=emojineer::decode_interop_request(sig,request);require(emojineer::values_equal(decoded[0],args[0]),"request codec preserves nested values and signed numeric subtypes");auto response=emojineer::encode_interop_success(emojineer::InteropType::Array,decoded[0]);require(emojineer::values_equal(emojineer::decode_interop_response(emojineer::InteropType::Array,response),args[0]),"response codec is equivalent");
 auto failure=emojineer::encode_interop_failure("boom");expect_error([&]{(void)emojineer::decode_interop_response(emojineer::InteropType::Array,failure);},"boom");
 auto record=std::make_shared<emojineer::RecordValue>();record->type_name="AbiBoundary";record->fields["x"]=std::int64_t{1};auto compound=std::make_shared<emojineer::ArrayValue>();compound->elements.push_back(record);expect_error([&]{(void)emojineer::encode_interop_success(emojineer::InteropType::Array,compound);},"does not support this value type");
 emojineer::Value deep=std::make_shared<emojineer::ArrayValue>();for(int i=0;i<66;++i){auto next=std::make_shared<emojineer::ArrayValue>();next->elements.push_back(deep);deep=next;}expect_error([&]{(void)emojineer::encode_interop_success(emojineer::InteropType::Array,deep);},"nesting");
}

void test_adapter_failure(){
 auto c=compile_text(import_number);emojineer::InteropRegistry registry;registry.bind("fixture.double",0,true,[&](auto){return emojineer::encode_interop_failure("fixture exploded");});std::istringstream in;std::ostringstream out;emojineer::VM vm(in,out,1'000'000,{},&registry);expect_error([&]{vm.execute(c);},"fixture exploded");
}

void test_exports_and_abi(){
 auto c=compile_text(R"(📡 🚀 📜fixture.add_global📜 🔢 🫴 🔢 🤲
🐍 🌍 🔢 🟰 10
🛠️ 🚀 🫴 🍎 🤲
📦 🍎 ➕ 🌍
🏁
)");
 require(c.interop_exports.size()==1,"one export metadata row");std::istringstream in;std::ostringstream out;emojineer::VM vm(in,out);auto first=vm.invoke_export(c,"fixture.add_global",{5.0});require(std::get<double>(first)==15.0,"export runs initialized production VM function");auto second=vm.invoke_export(c,"fixture.add_global",{7.0});require(std::get<double>(second)==17.0,"export reuses initialized globals");
 auto request=emojineer::encode_interop_request(c.interop_exports[0].signature,{2.0});auto reply=vm.invoke_export_abi(c,"fixture.add_global",request);auto value=emojineer::decode_interop_response(emojineer::InteropType::Number,reply);require(std::get<double>(value)==12.0,"ABI export path matches direct invocation");expect_error([&]{(void)vm.invoke_export(c,"fixture.add_global",{std::string("bad")});},"requires number");
}

void test_oversized_adapter_failure_is_bounded(){
 auto c=compile_text(R"(🔌 🧮 📜fixture.fail📜 📜none📜 🔢 🫴 🔢 🤲
📡 🚀 📜fixture.export📜 🔢 🫴 🔢 🤲
🛠️ 🚀 🫴 🍎 🤲
📦 🧮 🫴 🍎 🤲
🏁
)");
 emojineer::InteropRegistry registry;registry.bind("fixture.fail",0,true,[&](auto){throw std::runtime_error(std::string(300*1024,'x'));return emojineer::InteropBytes{};});
 std::istringstream in;std::ostringstream out;emojineer::VM vm(in,out,1'000'000,{},&registry);
 auto request=emojineer::encode_interop_request(c.interop_exports[0].signature,{1.0});
 auto response=vm.invoke_export_abi(c,"fixture.export",request);require(response.size()<1024,"oversized adapter failure should collapse to a bounded ABI response");
 expect_error([&]{(void)emojineer::decode_interop_response(emojineer::InteropType::Number,response);},"interop invocation failed");
}

void test_compile_time_contract_errors(){
 expect_error([&]{(void)compile_text(R"(🔌 🧮 📜same.name📜 📜none📜 🔢 🫴 🔢 🤲
🔌 🧰 📜same.name📜 📜none📜 🔢 🫴 🔢 🤲
)");},"duplicate interop import external name");
 expect_error([&]{(void)compile_text(R"(📡 🚀 📜bad.export📜 🔢 🫴 🔢 🔢 🤲
🛠️ 🚀 🫴 🍎 🤲
📦 🍎
🏁
)");},"arity");
}

void test_linked_dependency_authority(){
 TempRoot root;write_text(root.path/"dep.emoji",R"(🧩 🌲
🔌 🧮 📜fixture.net📜 📜network📜 🔢 🫴 🔢 🤲
🛠️ 🧠 🫴 🍎 🤲
📦 🧮 🫴 🍎 🤲
🏁
📤 🧠
)");write_text(root.path/"main.emoji",R"(🧩 🚀
🔗 📜dep.emoji📜
📝 📜before📜
📝 🧠 🫴 1 🤲
)");auto c=emojineer::compile_file(root.path/"main.emoji",{},root.path);require(c.required_capabilities==emojineer::capability_mask(emojineer::Capability::Network),"dependency interop call contributes whole-program authority");std::istringstream in;std::ostringstream out;emojineer::VM vm(in,out);expect_error([&]{vm.execute(c);},"network");require(out.str().empty(),"dependency adapter authority is denied before main effects");
}
}
int main(){try{test_compile_and_adapter_success();test_v9_roundtrip_and_verifier_binding();test_preflight_and_binding_contract();test_missing_and_deterministic_binding();test_abi_roundtrip_and_bounds();test_adapter_failure();test_exports_and_abi();test_oversized_adapter_failure_is_bounded();test_compile_time_contract_errors();test_linked_dependency_authority();std::cout<<"all Train 21 interop tests passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
