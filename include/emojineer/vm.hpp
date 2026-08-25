#pragma once
#include "emojineer/bytecode.hpp"
#include "emojineer/debugger.hpp"
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>
namespace emojineer {

// Debugger control interface - implemented by VM to allow external debugger control
class VMDebugControl {
public:
    virtual ~VMDebugControl() = default;
    
    // Called at safe points during execution (before each instruction)
    // Returns true to continue execution, false to pause
    virtual bool debug_hook() = 0;
    
    // Get current instruction pointer
    virtual std::size_t current_ip() const = 0;
    
    // Get current source position from source map
    virtual std::optional<SourcePosition> get_current_source_position() const = 0;
    
    // Get read-only view of call stack
    virtual std::vector<DebugFrame> get_call_stack() const = 0;
    
    // Get read-only view of globals
    virtual std::unordered_map<std::string, Value> get_globals() const = 0;
    
    // Check if a breakpoint is set at current position
    virtual bool is_breakpoint_hit() const = 0;
    
    // Check if should pause due to step mode
    virtual bool should_pause_for_step() const = 0;
    
    // Get step out target frame depth
    virtual std::size_t get_step_out_target_depth() const = 0;
    
    // Get step over target IP
    virtual std::size_t get_step_over_target_ip() const = 0;
};

// VM with integrated debugger support - single opcode implementation
class VM{public:VM(std::istream& input,std::ostream& output,std::uint64_t fuel=1'000'000);void execute(const Chunk& chunk);void set_debug_control(VMDebugControl* debug){debug_control_=debug;}VMDebugControl* debug_control()const{return debug_control_;}bool is_debug_paused()const{return debug_paused_;}void set_debug_paused(bool p){debug_paused_=p;}std::size_t current_ip()const{return ip_;}const Chunk* current_chunk()const{return current_chunk_;}std::optional<SourcePosition> get_current_source_position()const;std::vector<DebugFrame> get_call_stack()const;std::unordered_map<std::string,Value> get_globals()const;private:struct CallFrame{std::size_t return_ip{0};std::size_t stack_base{0};std::size_t function_index{0};std::vector<Value> locals;};Value pop(std::uint32_t line);const Value& peek(std::uint32_t line)const;bool pop_bool(std::uint32_t line);double pop_number(std::uint32_t line);std::int64_t pop_int64(std::uint32_t line);std::string constant_string(const Chunk& chunk,std::int32_t index,std::uint32_t line)const;CallFrame& frame(std::uint32_t line);const CallFrame& frame(std::uint32_t line)const;[[noreturn]]void runtime_error(std::uint32_t line,const std::string& message)const;std::istream& input_;std::ostream& output_;std::uint64_t fuel_;std::vector<Value> stack_;std::vector<CallFrame> frames_;std::unordered_map<std::string,Value> globals_;VMDebugControl* debug_control_{nullptr};bool debug_paused_{false};std::size_t ip_{0};const Chunk* current_chunk_{nullptr};
};
