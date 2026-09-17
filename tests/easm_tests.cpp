#include "emojineer/compiler.hpp"
#include "emojineer/easm.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/vm.hpp"
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error("EASM test failed: "+message);}
template<class F>std::string expect_error(F&& fn,const std::string& needle){try{fn();}catch(const std::exception& error){std::string message=error.what();require(message.find(needle)!=std::string::npos,"expected '"+needle+"', got '"+message+"'");return message;}throw std::runtime_error("EASM test failed: expected error containing '"+needle+"'");}
emojineer::Chunk compile_high(const std::string& source){emojineer::Lexer lexer(source);emojineer::Parser parser(lexer.tokenize());emojineer::Compiler compiler;return compiler.compile(parser.parse());}

const std::string memory_program=R"(EASM1
buffer cells i64 4
func main () -> i64 regs (i64,i64,i64)
  i64.const r0 1
  i64.const r1 41
  buffer.store.i64 cells r0 r1
  buffer.load.i64 r2 cells r0
  return r2
end
export main main
)";

const std::string double_program=R"(EASM1
func double (i64) -> i64 regs (i64,i64)
  i64.const r1 2
  i64.mul r1 r0 r1
  return r1
end
export easm.double double
)";
void test_parse_render_and_memory(){
 auto program=emojineer::parse_easm(memory_program);require(program.buffers.size()==1,"one typed buffer");require(program.exports.size()==1,"one export");
 emojineer::EasmVM vm;auto result=vm.invoke_export(program,"main",{});require(std::get<std::int64_t>(result)==41,"typed i64 buffer store/load works");
 auto canonical=emojineer::render_easm(program);auto reparsed=emojineer::parse_easm(canonical);require(emojineer::render_easm(reparsed)==canonical,"canonical EASM round-trips");
}

void test_branch_and_fuel(){
 auto program=emojineer::parse_easm(R"(EASM1
func choose (i64) -> i64 regs (i64,i64,bool)
  i64.const r1 10
  i64.lt r2 r0 r1
  jump_if_false r2 ge
  return r1
  label ge
  return r0
end
export choose choose
)");emojineer::EasmVM vm;require(std::get<std::int64_t>(vm.invoke_export(program,"choose",{std::int64_t{3}}))==10,"branch true path");require(std::get<std::int64_t>(vm.invoke_export(program,"choose",{std::int64_t{12}}))==12,"branch false path");
 auto loop=emojineer::parse_easm(R"(EASM1
func spin () -> i64 regs (i64)
  label again
  jump again
  i64.const r0 0
  return r0
end
export spin spin
)");emojineer::EasmVM bounded({},nullptr,50);expect_error([&]{(void)bounded.invoke_export(loop,"spin",{});},"fuel exhausted");
}
void test_verifier_and_runtime_bounds(){
 expect_error([&]{(void)emojineer::parse_easm(R"(EASM1
buffer huge f64 3000000
func main () -> i64 regs (i64)
 i64.const r0 0
 return r0
end
export main main
)");},"memory exceeds safety limit");
 expect_error([&]{(void)emojineer::parse_easm(R"(EASM1
func bad () -> i64 regs (i64,f64)
 f64.const r1 1
 i64.add r0 r0 r1
 return r0
end
export bad bad
)");},"register type mismatch");
 expect_error([&]{(void)emojineer::parse_easm(R"(EASM1
func bad () -> i64 regs (i64)
 jump nowhere
 return r0
end
export bad bad
)");},"unknown label");
 auto bounds=emojineer::parse_easm(R"(EASM1
buffer bytes u8 2
func main () -> i64 regs (i64,i64)
 i64.const r0 2
 buffer.load.u8 r1 bytes r0
 return r1
end
export main main
)");emojineer::EasmVM vm;expect_error([&]{(void)vm.invoke_export(bounds,"main",{});},"index out of range");
}
void test_parser_cardinality_and_enum_rejection(){
 std::string too_many_buffers="EASM1\n";
 for(std::size_t i=0;i<1025;++i) too_many_buffers+="buffer b"+std::to_string(i)+" u8 1\n";
 expect_error([&]{(void)emojineer::parse_easm(too_many_buffers);},"EASM buffer table exceeds safety limit");

 std::string too_many_instructions="EASM1\nfunc huge () -> i64 regs (i64)\n";
 too_many_instructions.reserve(18'000'000);
 for(std::size_t i=0;i<1'000'001;++i) too_many_instructions+="i64.const r0 0\n";
 too_many_instructions+="end\n";
 expect_error([&]{(void)emojineer::parse_easm(too_many_instructions);},"EASM instruction count exceeds safety limit");

 emojineer::EasmProgram bad_buffer;
 bad_buffer.buffers.push_back({"bad",static_cast<emojineer::EasmBufferType>(0xff),1});
 expect_error([&]{emojineer::verify_easm_program(bad_buffer);},"invalid EASM buffer type");

 emojineer::EasmProgram bad_opcode;
 emojineer::EasmFunction function;function.name="bad";function.result=emojineer::EasmScalarType::I64;function.registers={emojineer::EasmScalarType::I64};
 emojineer::EasmInstruction bogus;bogus.op=static_cast<emojineer::EasmOp>(0xff);
 emojineer::EasmInstruction ret;ret.op=emojineer::EasmOp::Return;ret.a=0;
 function.code={bogus,ret};bad_opcode.functions.push_back(std::move(function));
 expect_error([&]{emojineer::verify_easm_program(bad_opcode);},"invalid EASM opcode");
}
void test_import_capability_preflight(){
 auto program=emojineer::parse_easm(R"(EASM1
import remote fixture.low network (i64) -> i64
func call_remote (i64) -> i64 regs (i64,i64)
 call r1 remote r0
 return r1
end
export easm.remote call_remote
)");
 int calls=0;emojineer::InteropRegistry inner;inner.bind("fixture.low",emojineer::capability_mask(emojineer::Capability::Network),true,[&](auto request){++calls;auto sig=emojineer::easm_interop_signature({emojineer::EasmScalarType::I64},emojineer::EasmScalarType::I64);auto args=emojineer::decode_interop_request(sig,request);return emojineer::encode_interop_success(emojineer::InteropType::Number,args[0]);});
 emojineer::EasmVM denied({},&inner);expect_error([&]{(void)denied.invoke_export(program,"easm.remote",{std::int64_t{7}});},"network");require(calls==0,"capability denial occurs before adapter effects");
 emojineer::ExecutionPolicy policy;policy.grants=emojineer::capability_mask(emojineer::Capability::Network);emojineer::EasmVM granted(policy,&inner);auto value=granted.invoke_export(program,"easm.remote",{std::int64_t{7}});require(std::get<std::int64_t>(value)==7&&calls==1,"explicit capability grant permits import");
}

void test_determinism_is_derived(){
 auto program=emojineer::parse_easm(R"(EASM1
import entropy fixture.entropy none (i64) -> i64
func f (i64) -> i64 regs (i64,i64)
 call r1 entropy r0
 return r1
end
export easm.f f
)");
 emojineer::InteropRegistry nondet;nondet.bind("fixture.entropy",0,false,[&](auto request){auto sig=emojineer::easm_interop_signature({emojineer::EasmScalarType::I64},emojineer::EasmScalarType::I64);auto args=emojineer::decode_interop_request(sig,request);return emojineer::encode_interop_success(emojineer::InteropType::Number,args[0]);});emojineer::EasmVM no({},&nondet);require(!no.export_is_deterministic(program,"easm.f"),"nondeterministic import propagates to export");
 emojineer::InteropRegistry det;det.bind("fixture.entropy",0,true,[&](auto request){auto sig=emojineer::easm_interop_signature({emojineer::EasmScalarType::I64},emojineer::EasmScalarType::I64);auto args=emojineer::decode_interop_request(sig,request);return emojineer::encode_interop_success(emojineer::InteropType::Number,args[0]);});emojineer::EasmVM yes({},&det);require(yes.export_is_deterministic(program,"easm.f"),"deterministic import preserves export eligibility");
}
void test_train21_high_level_bridge(){
 auto low_vm=std::make_shared<emojineer::EasmVM>();emojineer::InteropRegistry outer;
 {auto low=emojineer::parse_easm(double_program);emojineer::bind_easm_export(outer,"easm.double",low_vm,low,"easm.double");}
 auto high=compile_high(R"(🔌 🧮 📜easm.double📜 📜none📜 🔢 🫴 🔢 🤲
📝 🧮 🫴 21 🤲
)");std::istringstream input;std::ostringstream output;emojineer::VM high_vm(input,output,1'000'000,{},&outer);high_vm.execute(high);require(output.str()=="42\n","high-level Emojineer calls EASM through Train 21 ABI");
 require(outer.find("easm.double")&&outer.find("easm.double")->deterministic,"pure EASM binding advertises deterministic eligibility");
}

void test_bridge_capability_contract(){
 auto low=emojineer::parse_easm(R"(EASM1
import net fixture.net network (i64) -> i64
func f (i64) -> i64 regs (i64,i64)
 call r1 net r0
 return r1
end
export easm.net f
)");emojineer::InteropRegistry inner;inner.bind("fixture.net",emojineer::capability_mask(emojineer::Capability::Network),true,[&](auto request){auto sig=emojineer::easm_interop_signature({emojineer::EasmScalarType::I64},emojineer::EasmScalarType::I64);auto args=emojineer::decode_interop_request(sig,request);return emojineer::encode_interop_success(emojineer::InteropType::Number,args[0]);});
 emojineer::ExecutionPolicy policy;policy.grants=emojineer::capability_mask(emojineer::Capability::Network);auto low_vm=std::make_shared<emojineer::EasmVM>(policy,&inner);emojineer::InteropRegistry outer;emojineer::bind_easm_export(outer,"easm.net",low_vm,low,"easm.net");require(outer.find("easm.net")->required_capabilities==policy.grants,"EASM export capability union becomes Train 21 adapter contract");
 auto dishonest=compile_high(R"(🔌 🧮 📜easm.net📜 📜none📜 🔢 🫴 🔢 🤲
📝 🧮 🫴 1 🤲
)");std::istringstream in;std::ostringstream out;emojineer::VM high(in,out,1'000'000,{},&outer);expect_error([&]{high.execute(dishonest);},"capability contract mismatch");require(out.str().empty(),"mismatched low-level authority is rejected before high-level effects");
}
void test_direct_and_abi_equivalence(){
 auto program=emojineer::parse_easm(double_program);emojineer::EasmVM vm;auto direct=vm.invoke_export(program,"easm.double",{std::int64_t{9}});require(std::get<std::int64_t>(direct)==18,"direct EASM invocation");
 auto signature=emojineer::easm_interop_signature({emojineer::EasmScalarType::I64},emojineer::EasmScalarType::I64);auto request=emojineer::encode_interop_request(signature,{std::int64_t{9}});auto response=vm.invoke_export_abi(program,"easm.double",request);auto value=emojineer::decode_interop_response(emojineer::InteropType::Number,response);require(std::get<std::int64_t>(value)==18,"EMJABI1 path matches direct EASM invocation");
 auto bad_request=emojineer::encode_interop_request(signature,{2.5});auto bad_response=vm.invoke_export_abi(program,"easm.double",bad_request);expect_error([&]{(void)emojineer::decode_interop_response(emojineer::InteropType::Number,bad_response);},"whole in-range number");
 auto high_request=emojineer::encode_interop_request(signature,{9223372036854775808.0});auto high_response=vm.invoke_export_abi(program,"easm.double",high_request);expect_error([&]{(void)emojineer::decode_interop_response(emojineer::InteropType::Number,high_response);},"whole in-range number");

 auto f64_program=emojineer::parse_easm(R"(EASM1
func identity (f64) -> f64 regs (f64)
 return r0
end
export easm.identity identity
)");
 auto f64_signature=emojineer::easm_interop_signature({emojineer::EasmScalarType::F64},emojineer::EasmScalarType::F64);
 auto min_request=emojineer::encode_interop_request(f64_signature,{std::int64_t{-9223372036854775807LL-1}});auto min_response=vm.invoke_export_abi(f64_program,"easm.identity",min_request);auto min_value=emojineer::decode_interop_response(emojineer::InteropType::Number,min_response);require(std::get<double>(min_value)==-9223372036854775808.0,"INT64_MIN converts exactly to EASM f64");
 auto max_request=emojineer::encode_interop_request(f64_signature,{std::int64_t{9223372036854775807LL}});auto max_response=vm.invoke_export_abi(f64_program,"easm.identity",max_request);expect_error([&]{(void)emojineer::decode_interop_response(emojineer::InteropType::Number,max_response);},"lose integer precision");
}

void test_strict_numeric_tokens_and_f64_roundtrip(){
 expect_error([&]{(void)emojineer::parse_easm("EASM1\nfunc bad () -> i64 regs (i64)\n i64.const r0 12oops\n return r0\nend\nexport bad bad\n");},"invalid i64 literal");
 expect_error([&]{(void)emojineer::parse_easm("EASM1\nfunc bad () -> f64 regs (f64)\n f64.const r0 1.5oops\n return r0\nend\nexport bad bad\n");},"invalid finite f64 literal");
 expect_error([&]{(void)emojineer::parse_easm("EASM1\nbuffer bad i64 4oops\n");},"not a valid u32 value");
 expect_error([&]{(void)emojineer::parse_easm("EASM1\nfunc bad (i64) garbage -> i64 regs (i64)\n return r0\nend\nexport bad bad\n");},"unexpected content between EASM parameter list and result arrow");
 auto program=emojineer::parse_easm("EASM1\nfunc precise () -> f64 regs (f64)\n f64.const r0 0.12345678901234566\n return r0\nend\nexport precise precise\n");
 emojineer::EasmVM vm;const auto before=std::get<double>(vm.invoke_export(program,"precise",{}));
 const auto canonical=emojineer::render_easm(program);auto reparsed=emojineer::parse_easm(canonical);emojineer::EasmVM vm2;const auto after=std::get<double>(vm2.invoke_export(reparsed,"precise",{}));require(before==after,"canonical f64 rendering preserves binary64 exactly");
 auto hash_name=emojineer::parse_easm("EASM1\n# whole-line comment\nimport x fixture#name none (i64) -> i64\nfunc main () -> i64 regs (i64)\n i64.const r0 0\n return r0\nend\nexport main main\n");const auto hash_canonical=emojineer::render_easm(hash_name);require(emojineer::render_easm(emojineer::parse_easm(hash_canonical))==hash_canonical,"# in a valid adapter name survives canonical round-trip");
}

void test_memory_state_tracks_program_definition(){
 auto program=emojineer::parse_easm(R"(EASM1
buffer cell i64 1
func main () -> i64 regs (i64,i64,i64)
 i64.const r0 0
 i64.const r1 1
 buffer.load.i64 r2 cell r0
 i64.add r2 r2 r1
 buffer.store.i64 cell r0 r2
 return r2
end
export main main
)");
 emojineer::EasmVM vm;require(std::get<std::int64_t>(vm.invoke_export(program,"main",{}))==1,"unchanged program starts with zeroed memory");require(std::get<std::int64_t>(vm.invoke_export(program,"main",{}))==2,"unchanged program preserves EASM memory");
 auto replacement=emojineer::parse_easm(R"(EASM1
buffer cell i64 1
func main () -> i64 regs (i64,i64,i64)
 i64.const r0 0
 buffer.load.i64 r1 cell r0
 return r1
end
export main main
)");program.functions[0]=replacement.functions[0];require(std::get<std::int64_t>(vm.invoke_export(program,"main",{}))==0,"changed program definition resets stale EASM memory");
}

void test_integer_overflow(){
 auto add=emojineer::parse_easm(R"(EASM1
func add (i64,i64) -> i64 regs (i64,i64,i64)
 i64.add r2 r0 r1
 return r2
end
export add add
)");emojineer::EasmVM vm;expect_error([&]{(void)vm.invoke_export(add,"add",{std::int64_t{9223372036854775807LL},std::int64_t{1}});},"overflow");
 auto mul=emojineer::parse_easm(R"(EASM1
func mul (i64,i64) -> i64 regs (i64,i64,i64)
 i64.mul r2 r0 r1
 return r2
end
export mul mul
)");require(std::get<std::int64_t>(vm.invoke_export(mul,"mul",{std::int64_t{-3},std::int64_t{-4}}))==12,"negative by negative i64 multiplication");require(std::get<std::int64_t>(vm.invoke_export(mul,"mul",{std::int64_t{-3},std::int64_t{4}}))==-12,"negative by positive i64 multiplication");require(std::get<std::int64_t>(vm.invoke_export(mul,"mul",{std::int64_t{3},std::int64_t{-4}}))==-12,"positive by negative i64 multiplication");expect_error([&]{(void)vm.invoke_export(mul,"mul",{std::int64_t{-9223372036854775807LL-1},std::int64_t{-1}});},"overflow");
}

}
int main(){
 try{
  test_parse_render_and_memory();test_branch_and_fuel();test_verifier_and_runtime_bounds();test_parser_cardinality_and_enum_rejection();test_import_capability_preflight();test_determinism_is_derived();test_train21_high_level_bridge();test_bridge_capability_contract();test_direct_and_abi_equivalence();test_strict_numeric_tokens_and_f64_roundtrip();test_memory_state_tracks_program_definition();test_integer_overflow();
  std::cout<<"all Train 22 EASM tests passed\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
