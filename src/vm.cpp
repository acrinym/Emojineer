#include "emojineer/vm.hpp"

#include <cmath>
#include <istream>
#include <ostream>
#include <stdexcept>

namespace emojineer {
VM::VM(std::istream& input,std::ostream& output,std::uint64_t fuel):input_(input),output_(output),fuel_(fuel){}
void VM::execute(const Chunk& chunk){stack_.clear();globals_.clear();std::size_t ip=0;while(ip<chunk.code.size()){if(fuel_==0)runtime_error(chunk.code[ip].line,"execution fuel exhausted (possible infinite loop)");--fuel_;const Instruction ins=chunk.code[ip++];const auto line=ins.line;switch(ins.op){
case OpCode::Constant:if(ins.operand<0||static_cast<std::size_t>(ins.operand)>=chunk.constants.size())runtime_error(line,"invalid constant index");stack_.push_back(chunk.constants[static_cast<std::size_t>(ins.operand)]);break;
case OpCode::LoadGlobal:{const std::string name=constant_string(chunk,ins.operand,line);auto it=globals_.find(name);if(it==globals_.end())runtime_error(line,"undefined emoji variable '"+name+"'");stack_.push_back(it->second);break;}
case OpCode::StoreGlobal:{const std::string name=constant_string(chunk,ins.operand,line);globals_[name]=pop(line);break;}
case OpCode::AssertNumber:if(!std::holds_alternative<double>(peek(line)))runtime_error(line,"🔢 variable requires a number");break;
case OpCode::AssertString:if(!std::holds_alternative<std::string>(peek(line)))runtime_error(line,"🔤 variable requires text");break;
case OpCode::AssertBool:if(!std::holds_alternative<bool>(peek(line)))runtime_error(line,"🎯 variable requires ✅ or ❌");break;
case OpCode::Add:{Value r=pop(line),l=pop(line);if(auto*ln=std::get_if<double>(&l)){if(auto*rn=std::get_if<double>(&r))stack_.emplace_back(*ln+*rn);else runtime_error(line,"➕ requires two numbers or two text values");}else if(auto*ls=std::get_if<std::string>(&l)){if(auto*rs=std::get_if<std::string>(&r))stack_.emplace_back(*ls+*rs);else runtime_error(line,"➕ requires two numbers or two text values");}else runtime_error(line,"➕ requires two numbers or two text values");break;}
case OpCode::Subtract:{double r=pop_number(line),l=pop_number(line);stack_.emplace_back(l-r);break;}case OpCode::Multiply:{double r=pop_number(line),l=pop_number(line);stack_.emplace_back(l*r);break;}case OpCode::Divide:{double r=pop_number(line),l=pop_number(line);if(r==0.0)runtime_error(line,"division by zero");stack_.emplace_back(l/r);break;}case OpCode::Modulo:{double r=pop_number(line),l=pop_number(line);if(r==0.0)runtime_error(line,"modulo by zero");stack_.emplace_back(std::fmod(l,r));break;}
case OpCode::Equal:{Value r=pop(line),l=pop(line);stack_.emplace_back(l==r);break;}case OpCode::Less:{double r=pop_number(line),l=pop_number(line);stack_.emplace_back(l<r);break;}case OpCode::Greater:{double r=pop_number(line),l=pop_number(line);stack_.emplace_back(l>r);break;}
case OpCode::Negate:stack_.emplace_back(-pop_number(line));break;case OpCode::Not:stack_.emplace_back(!pop_bool(line));break;
case OpCode::ReadLine:{std::string value;if(!std::getline(input_,value))runtime_error(line,"📥 could not read input");stack_.emplace_back(std::move(value));break;}
case OpCode::Print:output_<<value_to_string(pop(line))<<'\n';break;
case OpCode::JumpIfFalse:{bool condition=pop_bool(line);if(!condition){if(ins.operand<0||static_cast<std::size_t>(ins.operand)>chunk.code.size())runtime_error(line,"invalid jump target");ip=static_cast<std::size_t>(ins.operand);}break;}
case OpCode::Jump:if(ins.operand<0||static_cast<std::size_t>(ins.operand)>chunk.code.size())runtime_error(line,"invalid jump target");ip=static_cast<std::size_t>(ins.operand);break;
case OpCode::Halt:if(!stack_.empty())runtime_error(line,"VM halted with a non-empty stack");return;}}
runtime_error(0,"bytecode terminated without Halt");}
Value VM::pop(std::uint32_t line){if(stack_.empty())runtime_error(line,"VM stack underflow");Value v=std::move(stack_.back());stack_.pop_back();return v;}
const Value& VM::peek(std::uint32_t line)const{if(stack_.empty())runtime_error(line,"VM stack underflow");return stack_.back();}
bool VM::pop_bool(std::uint32_t line){Value v=pop(line);if(auto*b=std::get_if<bool>(&v))return*b;runtime_error(line,"condition requires ✅ or ❌");}
double VM::pop_number(std::uint32_t line){Value v=pop(line);if(auto*n=std::get_if<double>(&v))return*n;runtime_error(line,"numeric operation requires numbers");}
std::string VM::constant_string(const Chunk& chunk,std::int32_t index,std::uint32_t line)const{if(index<0||static_cast<std::size_t>(index)>=chunk.constants.size())runtime_error(line,"invalid constant index");const auto*value=std::get_if<std::string>(&chunk.constants[static_cast<std::size_t>(index)]);if(!value)runtime_error(line,"bytecode expected string constant");return*value;}
[[noreturn]] void VM::runtime_error(std::uint32_t line,const std::string& message)const{if(line!=0)throw std::runtime_error("runtime line "+std::to_string(line)+": "+message);throw std::runtime_error("runtime: "+message);}
} // namespace emojineer
