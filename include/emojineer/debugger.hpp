#pragma once
#include "emojineer/bytecode.hpp"
#include "emojineer/cer.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace emojineer {

// Source position for debugging
struct SourcePosition {
    std::string source_path;    // Deterministic module identity (no absolute roots)
    std::uint32_t line;         // 1-based line number
    std::uint32_t column;       // 1-based column number
    
    // Constructors for compatibility
    SourcePosition() = default;
    SourcePosition(const SourceLocation& loc) 
        : source_path(loc.source_path), line(loc.line), column(loc.column) {}
    SourcePosition(const std::string& path, std::uint32_t l, std::uint32_t c = 1)
        : source_path(path), line(l), column(c) {}
    
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
    std::unordered_map<std::string, Value> globals;
};

// Debug state snapshot (read-only, no mutation)
struct DebugSnapshot {
    std::vector<DebugFrame> call_stack;
    std::size_t current_frame;
    SourcePosition current_position;
    std::string reason;  // Why we're paused (breakpoint, step, etc.)
};

// Debug control interface - protocol-neutral core for later DAP/editor adapter
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

// VM with debugger support
class DebugVM {
public:
    using DebugCallback = std::function<void(const DebugSnapshot&)>;
    
    DebugVM(std::istream& input, std::ostream& output, std::uint64_t fuel = 1'000'000);
    
    // Set debugger callback
    void set_debug_callback(DebugCallback callback);
    
    // Execute with debugger
    void execute(const Chunk& chunk);
    
    // Debug control
    void add_breakpoint(const BreakpointLocation& bp);
    void remove_breakpoint(std::size_t id);
    void set_breakpoints(const std::vector<BreakpointLocation>& bps);
    void rebuild_breakpoint_index();
    
    // Execution control
    void continue_run();
    void step_into();
    void step_over();
    void step_out();
    void pause();
    
    // Inspection (read-only)
    std::optional<DebugSnapshot> get_debug_snapshot() const;
    std::vector<SourcePosition> get_breakable_positions() const;
    
    // Source information
    std::optional<std::string> get_source_text(const std::string& path, 
                                               std::uint32_t start_line,
                                               std::uint32_t end_line) const;
    std::optional<SourcePosition> get_source_position(std::size_t ip) const;
    
    // Check if at breakpoint
    bool is_at_breakpoint() const;
    
    // Get current source position
    std::optional<SourcePosition> current_position() const;
    
    // Check if execution is complete
    bool is_finished() const { return finished_; }
    
    // Get current instruction pointer
    std::size_t current_ip() const { return ip_; }

private:
    // CallFrame for DebugVM (similar to VM::CallFrame)
    struct CallFrame {
        std::size_t return_ip{0};
        std::size_t stack_base{0};
        std::size_t function_index{0};
        std::vector<Value> locals;
    };
    
    void check_breakpoint();
    void notify_debug_event(DebugEventType type, const std::string& reason);
    
    // VM-like execution state (but controlled)
    std::istream& input_;
    std::ostream& output_;
    std::uint64_t fuel_;
    std::vector<Value> stack_;
    std::vector<CallFrame> frames_;
    std::unordered_map<std::string, Value> globals_;
    
    // Debug state
    std::size_t ip_{0};
    const Chunk* current_chunk_{nullptr};
    std::vector<BreakpointLocation> breakpoints_;
    std::unordered_map<std::size_t, std::size_t> breakpoint_id_by_ip_;  // ip -> breakpoint index
    std::set<std::size_t> hit_breakpoints_;
    
    // Execution control state
    enum class DebugRunMode { Running, SteppingInto, SteppingOver, SteppingOut, Paused, Finished };
    DebugRunMode run_mode_{DebugRunMode::Running};
    std::size_t step_out_frame_depth_{0};
    std::size_t step_over_start_ip_{0};
    
    bool finished_{false};
    DebugCallback debug_callback_;
    
    // Source mapping cache
    std::unordered_map<std::size_t, SourceLocation> ip_to_source_;
    
    // Frame tracking
    std::vector<SourceLocation> call_stack_positions_;
    
    void execute_instruction();
    void call_function(std::size_t function_index);
    void return_from_function();
    void wait_for_continue();
    
    Value pop(std::uint32_t line);
    const Value& peek(std::uint32_t line) const;
    bool pop_bool(std::uint32_t line);
    double pop_number(std::uint32_t line);
    std::int64_t pop_int64(std::uint32_t line);
    std::string constant_string(std::int32_t idx, std::uint32_t line) const;
    CallFrame& frame(std::uint32_t line);
    const CallFrame& frame(std::uint32_t line) const;
    [[noreturn]] void runtime_error(const std::string& message) const;
};

// Helper to create deterministic source path (no absolute roots)
std::string make_deterministic_source_path(const std::filesystem::path& source_file,
                                           const std::filesystem::path& module_root);

// Render value for debugger display (deterministic)
std::string debug_render_value(const Value& value);

// Run interactive debug session
int run_debug_session(const std::filesystem::path& source_file,
                     std::istream& input,
                     std::ostream& output,
                     std::ostream& error,
                     const CustomEmojiRegistry& registry);

} // namespace emojineer
