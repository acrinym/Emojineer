#pragma once
#include "emojineer/bytecode.hpp"
#include "emojineer/cer.hpp"
#include "emojineer/debug_types.hpp"
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

// Forward declaration - VM defined in vm.hpp
class VM;

// Source resolver - maps deterministic module identity to available source content
// Keeps checkout-absolute roots out of bytecode/debug identities
class SourceResolver {
public:
    SourceResolver() = default;
    
    // Add a source search path (e.g., project root, package directories)
    void add_search_path(const std::filesystem::path& path);
    
    // Set the base path for resolving relative paths
    void set_base_path(const std::filesystem::path& base);
    
    // Resolve a deterministic module identity to an actual file path
    // Returns empty path if not found
    std::filesystem::path resolve(const std::string& deterministic_identity) const;
    
    // Get source text from deterministic identity
    std::optional<std::string> get_source_text(const std::string& identity,
                                                std::uint32_t start_line,
                                                std::uint32_t end_line) const;
    
    // Register source content directly (for materialized packages)
    void register_source(const std::string& identity, std::string content);

private:
    std::vector<std::filesystem::path> search_paths_;
    std::filesystem::path base_path_;
    std::unordered_map<std::string, std::string> registered_sources_;  // identity -> content
};

// Breakpoint binding status for diagnostics
enum class BreakpointStatus {
    Bound,       // Successfully bound to a source location
    Unbound,     // No matching source location found
    Stale,       // Source file has changed since compilation
    SourceDrift // Source path no longer accessible
};

// Detailed breakpoint info with binding status
struct BreakpointInfo {
    std::size_t id;
    BreakpointLocation location;
    BreakpointStatus status;
    std::optional<std::size_t> bound_ip;  // IP if bound
    std::string diagnostics;  // Human-readable status info
};

// Debug controller that implements VMDebugControl to drive the production VM
// This replaces the old DebugVM which duplicated VM execution semantics
class DebugController : public VMDebugControl, public DebuggerControl {
public:
    using DebugCallback = std::function<void(const DebugSnapshot&)>;
    
    DebugController(VM& vm);
    ~DebugController() override;
    
    // Set debugger callback for pause events
    void set_debug_callback(DebugCallback callback);
    
    // Set source resolver for mapping identities to source
    void set_source_resolver(std::shared_ptr<SourceResolver> resolver);
    
    // DebuggerControl implementation
    std::size_t set_breakpoint(const BreakpointLocation& location) override;
    bool remove_breakpoint(std::size_t id) override;
    bool enable_breakpoint(std::size_t id, bool enabled) override;
    std::vector<BreakpointLocation> get_breakpoints() const override;
    void continue_execution() override;
    void pause_execution() override;
    void step_into() override;
    void step_over() override;
    void step_out() override;
    std::optional<DebugSnapshot> get_snapshot() const override;
    std::optional<Value> evaluate_expression(const std::string& expr) override;
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
    
    // Get detailed breakpoint info with binding status
    std::vector<BreakpointInfo> get_breakpoint_info() const;
    
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
    std::shared_ptr<SourceResolver> source_resolver_;
    std::vector<BreakpointLocation> breakpoints_;
    std::unordered_map<std::string, std::vector<std::size_t>> breakpoint_ids_by_source_;
    std::unordered_map<std::size_t, std::size_t> breakpoint_id_by_ip_;
    const Chunk* current_chunk_{nullptr};
    
    // Execution control state
    enum class DebugRunMode { Running, SteppingInto, SteppingOver, SteppingOut, Paused };
    // Start in Paused mode so user can set breakpoints before execution
    DebugRunMode run_mode_{DebugRunMode::Paused};
    std::size_t step_out_frame_depth_{0};
    std::size_t step_over_start_ip_{0};
    std::size_t step_into_start_ip_{0};
    bool step_into_first_{true};
    
    // Step state for source-boundary stepping (mutable for const methods)
    mutable std::optional<SourcePosition> step_start_position_;
    mutable std::size_t step_start_depth_{0};
    mutable bool step_initialized_{false};
    
    // Start paused so user can set breakpoints before execution begins
    bool paused_{true};
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
    std::vector<BreakpointLocation> get_breakpoints() const;
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
