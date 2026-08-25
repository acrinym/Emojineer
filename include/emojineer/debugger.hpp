#pragma once
#include "emojineer/bytecode.hpp"
#include "emojineer/cer.hpp"
#include "emojineer/vm.hpp"
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

// Debug controller that implements VMDebugControl to drive the production VM
// This replaces the old DebugVM which duplicated VM execution semantics
class DebugController : public VMDebugControl {
public:
    using DebugCallback = std::function<void(const DebugSnapshot&)>;
    
    DebugController(VM& vm);
    ~DebugController() override;
    
    // Set debugger callback for pause events
    void set_debug_callback(DebugCallback callback);
    
    // Breakpoint management
    std::size_t set_breakpoint(const BreakpointLocation& location) override;
    bool remove_breakpoint(std::size_t id) override;
    bool enable_breakpoint(std::size_t id, bool enabled) override;
    std::vector<BreakpointLocation> get_breakpoints() const override;
    
    // Execution control
    void continue_execution() override;
    void pause_execution() override;
    void step_into() override;
    void step_over() override;
    void step_out() override;
    
    // Inspection (read-only, no state mutation)
    std::optional<DebugSnapshot> get_snapshot() const override;
    std::optional<Value> evaluate_expression(const std::string& expr) override;
    
    // Source information
    std::optional<std::string> get_source_text(const std::string& path, 
                                                std::uint32_t start_line,
                                                std::uint32_t end_line) const override;
    std::optional<SourcePosition> get_source_position(std::size_t ip) const override;
    
    // Check if execution is paused
    bool is_paused() const { return paused_; }
    
    // Check if execution is complete
    bool is_finished() const { return finished_; }
    
    // Get breakable positions from current chunk
    std::vector<SourcePosition> get_breakable_positions() const;
    
    // VMDebugControl implementation
    bool debug_hook() override;
    std::size_t current_ip() const override;
    std::optional<SourcePosition> get_current_source_position() const override;
    std::vector<DebugFrame> get_call_stack() const override;
    std::unordered_map<std::string, Value> get_globals() const override;
    bool is_breakpoint_hit() const override;
    bool should_pause_for_step() const override;
    std::size_t get_step_out_target_depth() const override;
    std::size_t get_step_over_target_ip() const override;
    
private:
    void rebuild_breakpoint_index();
    void notify_debug_event(DebugEventType type, const std::string& reason);
    std::optional<SourcePosition> find_breakpoint_position(const BreakpointLocation& bp) const;
    
    VM& vm_;
    DebugCallback debug_callback_;
    std::vector<BreakpointLocation> breakpoints_;
    std::unordered_map<std::string, std::vector<std::size_t>> breakpoint_ids_by_source_;
    std::unordered_map<std::size_t, std::size_t> breakpoint_id_by_ip_;
    
    // Execution control state
    enum class DebugRunMode { Running, SteppingInto, SteppingOver, SteppingOut, Paused };
    DebugRunMode run_mode_{DebugRunMode::Running};
    std::size_t step_out_frame_depth_{0};
    std::size_t step_over_start_ip_{0};
    
    bool paused_{false};
    bool finished_{false};
    std::string pause_reason_;
};

// Legacy DebugVM wrapper for backward compatibility - uses DebugController to drive real VM
// DEPRECATED: New code should use DebugController directly with VM
class DebugVM {
public:
    using DebugCallback = std::function<void(const DebugSnapshot&)>;
    
    DebugVM(std::istream& input, std::ostream& output, std::uint64_t fuel = 1'000'000);
    ~DebugVM();
    
    // Set debugger callback
    void set_debug_callback(DebugCallback callback);
    
    // Execute with debugger
    void execute(const Chunk& chunk);
    
    // Debug control - delegates to DebugController
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
    bool is_finished() const { return controller_ && controller_->is_finished(); }
    
    // Get current instruction pointer
    std::size_t current_ip() const;

private:
    std::unique_ptr<VM> vm_;
    std::unique_ptr<DebugController> controller_;
    bool finished_{false};
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
