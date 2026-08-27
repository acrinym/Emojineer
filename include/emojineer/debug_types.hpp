#pragma once
#include "emojineer/bytecode.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace emojineer {

// Forward declaration - VM is defined in vm.hpp
class VM;

// Source position for debugging (v6 format - includes range and function context)
struct SourcePosition {
    std::string source_path;    // Deterministic module identity (no absolute roots)
    std::uint32_t line;         // 1-based line number (start line)
    std::uint32_t column;       // 1-based column number (start column)
    std::uint32_t end_line;     // 1-based end line number
    std::uint32_t end_column;   // 1-based end column number
    std::string function_name;  // Function context (empty for module-level code)
    
    // Constructors for compatibility
    SourcePosition() = default;
    SourcePosition(const SourceLocation& loc) 
        : source_path(loc.source_path), line(loc.line), column(loc.column),
          end_line(loc.end_line), end_column(loc.end_column), function_name(loc.function_name) {}
    SourcePosition(const std::string& path, std::uint32_t l, std::uint32_t c = 1)
        : source_path(path), line(l), column(c), end_line(l), end_column(c), function_name("") {}
    
    bool operator<(const SourcePosition& other) const {
        if (source_path != other.source_path) return source_path < other.source_path;
        if (line != other.line) return line < other.line;
        return column < other.column;
    }
    
    bool operator==(const SourcePosition& other) const {
        return source_path == other.source_path && line == other.line && column == other.column;
    }
};

// Breakpoint location
struct BreakpointLocation {
    SourcePosition source_position;
    bool enabled{true};
    std::string condition;  // Optional condition for conditional breakpoints
};

// Debug event types
enum class DebugEventType {
    BreakpointHit,
    StepComplete,
    Pause,
    Exception,
    ProgramExit
};

// Debug frame information
struct DebugFrame {
    std::size_t frame_index;           // 0 = innermost
    std::string function_name;
    SourcePosition source_position;
    std::vector<Value> locals;
    std::vector<Value> parameters;
    std::vector<std::string> local_names;      // Names for locals (same order as locals vector)
    std::vector<std::string> parameter_names; // Names for parameters (same order as parameters vector)
    std::unordered_map<std::string, Value> globals;
    // Named locals/parameters for debugger inspection (slot -> name mapping)
    std::unordered_map<std::string, Value> named_locals;   // All locals by name
    std::unordered_map<std::string, Value> named_parameters; // Parameters by name
};

// Debug state snapshot (read-only, no mutation)
struct DebugSnapshot {
    std::vector<DebugFrame> call_stack;
    std::size_t current_frame;
    SourcePosition current_position;
    std::string reason;  // Why we're paused (breakpoint, step, etc.)
};

// Debugger control interface - protocol-neutral core for later DAP/editor adapter
class DebuggerControl {
public:
    virtual ~DebuggerControl() = default;
    
    // Breakpoint management
    virtual std::size_t set_breakpoint(const BreakpointLocation& location) = 0;
    virtual bool remove_breakpoint(std::size_t id) = 0;
    virtual bool enable_breakpoint(std::size_t id, bool enabled) = 0;
    virtual std::vector<BreakpointLocation> get_breakpoints() const = 0;
    
    // Execution control
    virtual void continue_execution() = 0;
    virtual void pause_execution() = 0;
    virtual void step_into() = 0;
    virtual void step_over() = 0;
    virtual void step_out() = 0;
    
    // Inspection (read-only, no state mutation)
    virtual std::optional<DebugSnapshot> get_snapshot() const = 0;
    virtual std::optional<Value> evaluate_expression(const std::string& expr) = 0;
    
    // Source information
    virtual std::optional<std::string> get_source_text(const std::string& path, 
                                                        std::uint32_t start_line,
                                                        std::uint32_t end_line) const = 0;
    virtual std::optional<SourcePosition> get_source_position(std::size_t ip) const = 0;
};

// Debugger control interface - implemented by VM to allow external debugger control
// This is in debug_types.hpp to break the include cycle between vm.hpp and debugger.hpp
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
    
    // Check if debugger should pause BEFORE executing an instruction
    // Returns true if execution should NOT proceed (paused, breakpoint hit, or step complete)
    virtual bool should_pause_before_execution() const = 0;
};

} // namespace emojineer
