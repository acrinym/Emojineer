#include "emojineer/debugger.hpp"
#include "emojineer/vm.hpp"
#include "emojineer/module.hpp"
#include "emojineer/unicode.hpp"
#include "emojineer/compiler.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/parser.hpp"
#include <climits>
#include <cmath>
#include <fstream>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace emojineer {

// Helper to create deterministic source path (no absolute roots)
std::string make_deterministic_source_path(const std::filesystem::path& source_file,
                                           const std::filesystem::path& module_root) {
    // Use relative path to avoid embedding absolute checkout roots
    return std::filesystem::relative(source_file, module_root).generic_string();
}

// Render value for debugger display (deterministic)
std::string debug_render_value(const Value& value) {
    return value_to_string(value);
}

// SourceResolver implementation
void SourceResolver::add_search_path(const std::filesystem::path& path) {
    search_paths_.push_back(path);
}

void SourceResolver::set_base_path(const std::filesystem::path& base) {
    base_path_ = base;
}

std::filesystem::path SourceResolver::resolve(const std::string& deterministic_identity) const {
    // First check registered sources (for materialized packages)
    if (registered_sources_.find(deterministic_identity) != registered_sources_.end()) {
        // Return empty path to indicate we have content but no file
        return std::filesystem::path{};
    }
    
    // Try the identity as a direct path first
    std::filesystem::path p(deterministic_identity);
    if (p.is_absolute()) {
        // Don't use absolute paths - they contain checkout roots
        return std::filesystem::path{};
    }
    
    // Try as relative to base path
    if (!base_path_.empty()) {
        std::filesystem::path candidate = base_path_ / p;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    
    // Try search paths
    for (const auto& search_path : search_paths_) {
        std::filesystem::path candidate = search_path / p;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    
    // Try current directory
    if (std::filesystem::exists(p)) {
        return std::filesystem::absolute(p);
    }
    
    return std::filesystem::path{};
}

std::optional<std::string> SourceResolver::get_source_text(const std::string& identity,
                                                            std::uint32_t start_line,
                                                            std::uint32_t end_line) const {
    // First check registered sources (for materialized packages)
    auto it = registered_sources_.find(identity);
    if (it != registered_sources_.end()) {
        // Parse the registered source content and extract requested lines
        std::istringstream iss(it->second);
        std::string result;
        std::string line;
        std::uint32_t current_line = 0;
        while (std::getline(iss, line)) {
            ++current_line;
            if (current_line >= start_line && current_line <= end_line) {
                result += std::to_string(current_line) + ": " + line + "\n";
            }
            if (current_line > end_line) break;
        }
        if (result.empty()) {
            return std::nullopt;
        }
        return result;
    }
    
    // Try to resolve and read from file
    std::filesystem::path path = resolve(identity);
    if (path.empty()) {
        return std::nullopt;
    }
    
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    
    std::string result;
    std::string line;
    std::uint32_t current_line = 0;
    while (std::getline(file, line)) {
        ++current_line;
        if (current_line >= start_line && current_line <= end_line) {
            result += std::to_string(current_line) + ": " + line + "\n";
        }
        if (current_line > end_line) break;
    }
    
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
}

void SourceResolver::register_source(const std::string& identity, std::string content) {
    registered_sources_[identity] = std::move(content);
}

// DebugController implementation
DebugController::DebugController(VM& vm) : vm_(vm) {}

DebugController::~DebugController() = default;

void DebugController::set_debug_callback(DebugCallback callback) {
    debug_callback_ = std::move(callback);
}

void DebugController::set_source_resolver(std::shared_ptr<SourceResolver> resolver) {
    source_resolver_ = std::move(resolver);
}

std::size_t DebugController::set_breakpoint(const BreakpointLocation& location) {
    std::size_t id = breakpoints_.size();
    breakpoints_.push_back(location);
    
    // Index by source path for efficient lookup
    breakpoint_ids_by_source_[location.source_position.source_path].push_back(id);
    
    rebuild_breakpoint_index();
    return id;
}

bool DebugController::remove_breakpoint(std::size_t id) {
    if (id >= breakpoints_.size()) return false;
    
    const auto& bp = breakpoints_[id];
    auto& ids = breakpoint_ids_by_source_[bp.source_position.source_path];
    for (auto it = ids.begin(); it != ids.end(); ++it) {
        if (*it == id) {
            ids.erase(it);
            break;
        }
    }
    
    breakpoints_.erase(breakpoints_.begin() + id);
    rebuild_breakpoint_index();
    return true;
}

bool DebugController::enable_breakpoint(std::size_t id, bool enabled) {
    if (id >= breakpoints_.size()) return false;
    breakpoints_[id].enabled = enabled;
    rebuild_breakpoint_index();
    return true;
}

std::vector<BreakpointLocation> DebugController::get_breakpoints() const {
    return breakpoints_;
}

void DebugController::continue_execution() {
    run_mode_ = DebugRunMode::Running;
    paused_ = false;
    vm_.set_debug_paused(false);
}

void DebugController::pause_execution() {
    run_mode_ = DebugRunMode::Paused;
    paused_ = true;
    vm_.set_debug_paused(true);
}

void DebugController::step_into() {
    run_mode_ = DebugRunMode::SteppingInto;
    step_into_start_ip_ = vm_.current_ip();
    // Initialize step state for source-boundary stepping
    step_start_position_ = vm_.get_current_source_position();
    step_start_depth_ = vm_.get_call_stack().size();
    step_initialized_ = false;  // Will be set on first debug_hook call
    paused_ = false;
    vm_.set_debug_paused(false);
}

void DebugController::step_over() {
    run_mode_ = DebugRunMode::SteppingOver;
    step_over_start_ip_ = vm_.current_ip();
    // Initialize step state for source-boundary stepping
    step_start_position_ = vm_.get_current_source_position();
    step_start_depth_ = vm_.get_call_stack().size();
    step_initialized_ = false;  // Will be set on first debug_hook call
    paused_ = false;
    vm_.set_debug_paused(false);
}

void DebugController::step_out() {
    run_mode_ = DebugRunMode::SteppingOut;
    step_out_frame_depth_ = vm_.get_call_stack().size();
    // Initialize step state - target depth is one less than current
    step_start_depth_ = step_out_frame_depth_ > 0 ? step_out_frame_depth_ - 1 : 0;
    step_initialized_ = false;
    paused_ = false;
    vm_.set_debug_paused(false);
}

std::optional<DebugSnapshot> DebugController::get_snapshot() const {
    if (!paused_ && run_mode_ != DebugRunMode::Paused) {
        return std::nullopt;
    }
    
    DebugSnapshot snapshot;
    snapshot.call_stack = vm_.get_call_stack();
    // Clamp selected frame to valid range
    snapshot.current_frame = std::min(selected_frame_, snapshot.call_stack.empty() ? 0 : snapshot.call_stack.size() - 1);
    
    // Use selected frame's source_position, not the innermost position
    if (snapshot.current_frame < snapshot.call_stack.size()) {
        snapshot.current_position = snapshot.call_stack[snapshot.current_frame].source_position;
    } else {
        snapshot.current_position = vm_.get_current_source_position().value_or(SourcePosition());
    }
    snapshot.reason = pause_reason_;
    
    return snapshot;
}

bool DebugController::select_frame(std::size_t frame_index) {
    auto frames = vm_.get_call_stack();
    if (frames.empty()) {
        return false;
    }
    if (frame_index >= frames.size()) {
        return false;  // Report invalid selection deterministically
    }
    selected_frame_ = frame_index;
    return true;
}

std::optional<Value> DebugController::evaluate_expression(const std::string& expr) {
    // Read-only identifier/value inspection without mutating VM state or consuming input
    // Supports: locals, parameters, globals, arrays, and text values
    // Only searches the selected frame as per contract - user selects a frame
    
    if (expr.empty()) {
        return std::nullopt;
    }
    
    // Get the current frame's locals, parameters, and globals
    auto frames = vm_.get_call_stack();
    if (frames.empty()) {
        return std::nullopt;
    }
    
    // Clamp selected frame to valid range
    std::size_t frame_idx = std::min(selected_frame_, frames.size() - 1);
    
    // Trim whitespace from expression
    auto trim = [](const std::string& s) -> std::string {
        std::size_t start = 0;
        while (start < s.size() && std::isspace(s[start])) ++start;
        std::size_t end = s.size();
        while (end > start && std::isspace(s[end - 1])) --end;
        return s.substr(start, end - start);
    };
    
    std::string identifier = trim(expr);
    
    // Check the selected frame specifically for locals and parameters
    // Selected-frame inspection must be scoped to the selected frame
    if (frame_idx < frames.size()) {
        const auto& frame = frames[frame_idx];
        
        // Use named_parameters map directly (populated by VM from FunctionInfo)
        auto param_it = frame.named_parameters.find(identifier);
        if (param_it != frame.named_parameters.end()) {
            return param_it->second;
        }
        
        // Use named_locals map directly (populated by VM from FunctionInfo)
        auto local_it = frame.named_locals.find(identifier);
        if (local_it != frame.named_locals.end()) {
            return local_it->second;
        }
    }
    
    // Globals are shared and accessible from any frame - check VM globals directly
    const auto& globals = vm_.get_globals();
    auto it = globals.find(identifier);
    if (it != globals.end()) {
        return it->second;
    }
    
    return std::nullopt;
}

std::optional<std::string> DebugController::get_source_text(const std::string& path, 
                                                           std::uint32_t start_line,
                                                           std::uint32_t end_line) const {
    // Use source resolver if available for deterministic identity resolution
    if (source_resolver_) {
        return source_resolver_->get_source_text(path, start_line, end_line);
    }
    
    // Fallback: try to read from the source file (legacy behavior)
    // This doesn't handle deterministic identities properly but maintains compatibility
    std::filesystem::path source_path(path);
    if (!std::filesystem::exists(source_path)) {
        return std::nullopt;
    }
    
    std::ifstream file(source_path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    
    std::string result;
    std::string line;
    std::uint32_t current_line = 0;
    while (std::getline(file, line)) {
        ++current_line;
        if (current_line >= start_line && current_line <= end_line) {
            result += std::to_string(current_line) + ": " + line + "\n";
        }
        if (current_line > end_line) break;
    }
    
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
}

std::optional<SourcePosition> DebugController::get_source_position(std::size_t ip) const {
    const Chunk* chunk = vm_.current_chunk();
    if (!chunk || ip >= chunk->source_map.size()) {
        return std::nullopt;
    }
    const auto& src = chunk->source_map[ip];
    return SourcePosition(src.source_path, src.line, src.column);
}

std::vector<SourcePosition> DebugController::get_breakable_positions() const {
    std::vector<SourcePosition> positions;
    const Chunk* chunk = vm_.current_chunk();
    if (!chunk) return positions;
    
    for (std::size_t ip = 0; ip < chunk->source_map.size(); ++ip) {
        const auto& src = chunk->source_map[ip];
        positions.push_back(SourcePosition(src.source_path, src.line, src.column));
    }
    return positions;
}

bool DebugController::debug_hook() {
    // Rebuild breakpoint index if chunk changed
    const Chunk* chunk = vm_.current_chunk();
    if (chunk != current_chunk_) {
        current_chunk_ = chunk;
        rebuild_breakpoint_index();
    }
    
    // Check if we should pause
    if (run_mode_ == DebugRunMode::Paused) {
        return false;
    }
    
    // Check for breakpoint hit
    if (is_breakpoint_hit()) {
        paused_ = true;
        pause_reason_ = "breakpoint hit";
        notify_debug_event(DebugEventType::BreakpointHit, pause_reason_);
        return false;
    }
    
    // Check for step mode
    if (should_pause_for_step()) {
        paused_ = true;
        
        if (run_mode_ == DebugRunMode::SteppingInto) {
            pause_reason_ = "stepped into";
        } else if (run_mode_ == DebugRunMode::SteppingOver) {
            pause_reason_ = "stepped over";
        } else if (run_mode_ == DebugRunMode::SteppingOut) {
            pause_reason_ = "stepped out";
        }
        
        notify_debug_event(DebugEventType::StepComplete, pause_reason_);
        return false;
    }
    
    return true;
}

std::size_t DebugController::current_ip() const {
    return vm_.current_ip();
}

std::optional<SourcePosition> DebugController::get_current_source_position() const {
    return vm_.get_current_source_position();
}

std::vector<DebugFrame> DebugController::get_call_stack() const {
    return vm_.get_call_stack();
}

std::unordered_map<std::string, Value> DebugController::get_globals() const {
    return vm_.get_globals();
}

bool DebugController::is_breakpoint_hit() const {
    // Check breakpoints in all modes - user breakpoints should be honored even during stepping
    // However, we don't want to re-hit the same breakpoint immediately - that's handled by step semantics
    
    std::size_t ip = vm_.current_ip();
    auto it = breakpoint_id_by_ip_.find(ip);
    if (it == breakpoint_id_by_ip_.end()) return false;
    
    std::size_t bp_id = it->second;
    if (bp_id >= breakpoints_.size()) return false;
    
    // Only honor enabled breakpoints
    if (!breakpoints_[bp_id].enabled) return false;
    
    // If we're stepping, we need to ensure we don't immediately re-hit the same breakpoint
    // The step logic in should_pause_for_step will handle the pause after the instruction executes
    // Here we just check if there's a breakpoint at the current location
    return true;
}

bool DebugController::should_pause_for_step() const {
    if (run_mode_ == DebugRunMode::Running || run_mode_ == DebugRunMode::Paused) {
        return false;
    }
    
    const Chunk* chunk = vm_.current_chunk();
    if (!chunk) return false;
    
    std::size_t current_ip = vm_.current_ip();
    std::size_t current_depth = vm_.get_call_stack().size();
    
    // Get current source position
    std::optional<SourcePosition> current_pos = vm_.get_current_source_position();
    if (!current_pos) return false;
    
    if (run_mode_ == DebugRunMode::SteppingInto) {
        // Step into: stop at the next source location (including function calls)
        
        // First call - initialize state
        if (!step_initialized_) {
            step_start_position_ = current_pos;
            step_start_depth_ = current_depth;
            step_initialized_ = true;
            return false; // Don't pause on first call
        }
        
        // Check if we've moved to a different source position
        if (step_start_position_ && 
            (current_pos->source_path != step_start_position_->source_path || 
             current_pos->line != step_start_position_->line)) {
            return true;
        }
        
        // Check if we've entered a new frame (function call)
        if (current_depth > step_start_depth_) {
            return true;
        }
        
        return false;
    }
    
    if (run_mode_ == DebugRunMode::SteppingOver) {
        // Step over: execute calls without stopping inside them
        // Stop at the next source location at the same or shallower depth
        
        // First call - initialize state
        if (!step_initialized_) {
            step_start_position_ = current_pos;
            step_start_depth_ = current_depth;
            step_initialized_ = true;
            return false;
        }
        
        // If we've gone to a deeper depth, we're inside a called function - continue
        if (current_depth > step_start_depth_) {
            return false;
        }
        
        // If we're at the same or shallower depth and moved to a new source position, stop
        if (current_depth <= step_start_depth_) {
            if (step_start_position_ &&
                (current_pos->source_path != step_start_position_->source_path || 
                 current_pos->line != step_start_position_->line)) {
                return true;
            }
        }
        
        return false;
    }
    
    if (run_mode_ == DebugRunMode::SteppingOut) {
        // Step out: stop only after returning to a caller (shallower depth)
        // Don't pause until we've actually returned
        
        // First call - initialize state
        if (!step_initialized_) {
            step_start_depth_ = current_depth > 0 ? current_depth - 1 : 0;
            step_initialized_ = true;
            return false;
        }
        
        // Stop when we've returned to the target depth (or shallower)
        if (current_depth <= step_start_depth_) {
            return true;
        }
        
        return false;
    }
    
    return false;
}

std::size_t DebugController::get_step_out_target_depth() const {
    return step_out_frame_depth_;
}

std::size_t DebugController::get_step_over_target_ip() const {
    return step_over_start_ip_;
}

bool DebugController::should_pause_before_execution() const {
    // If already paused, stay paused
    if (paused_) {
        return true;
    }
    
    // If in paused mode, pause before executing
    if (run_mode_ == DebugRunMode::Paused) {
        return true;
    }
    
    // Check for breakpoint hit - but don't re-hit the same breakpoint immediately
    // after a step operation (handled by step semantics)
    if (is_breakpoint_hit()) {
        return true;
    }
    
    // Check if we should pause for step (but only after the instruction has executed,
    // so this is handled by debug_hook() which is called post-instruction)
    // This method is for PRE-execution checks only
    
    return false;
}

void DebugController::rebuild_breakpoint_index() {
    breakpoint_id_by_ip_.clear();
    
    const Chunk* chunk = vm_.current_chunk();
    if (!chunk) return;
    
    for (std::size_t i = 0; i < breakpoints_.size(); ++i) {
        const auto& bp = breakpoints_[i];
        
        // Find matching source position in source map
        for (std::size_t ip = 0; ip < chunk->source_map.size(); ++ip) {
            const auto& src = chunk->source_map[ip];
            if (src.source_path == bp.source_position.source_path &&
                src.line == bp.source_position.line) {
                breakpoint_id_by_ip_[ip] = i;
            }
        }
    }
}

std::vector<BreakpointInfo> DebugController::get_breakpoint_info() const {
    std::vector<BreakpointInfo> infos;
    
    const Chunk* chunk = vm_.current_chunk();
    
    for (std::size_t i = 0; i < breakpoints_.size(); ++i) {
        const auto& bp = breakpoints_[i];
        BreakpointInfo info;
        info.id = i;
        info.location = bp;
        
        // Check if breakpoint can bind to current chunk
        if (!chunk) {
            info.status = BreakpointStatus::Unbound;
            info.diagnostics = "No chunk loaded";
            infos.push_back(info);
            continue;
        }
        
        // Try to find matching source position
        std::optional<std::size_t> bound_ip;
        for (std::size_t ip = 0; ip < chunk->source_map.size(); ++ip) {
            const auto& src = chunk->source_map[ip];
            if (src.source_path == bp.source_position.source_path &&
                src.line == bp.source_position.line) {
                bound_ip = ip;
                break;
            }
        }
        
        if (bound_ip) {
            info.status = BreakpointStatus::Bound;
            info.bound_ip = bound_ip;
            info.diagnostics = "Bound to IP " + std::to_string(*bound_ip);
        } else {
            // Check if source exists
            bool source_exists = false;
            if (source_resolver_) {
                source_exists = !source_resolver_->resolve(bp.source_position.source_path).empty();
            } else {
                std::filesystem::path p(bp.source_position.source_path);
                source_exists = std::filesystem::exists(p);
            }
            
            if (source_exists) {
                info.status = BreakpointStatus::SourceDrift;
                info.diagnostics = "Source file exists but no matching line in current bytecode";
            } else {
                info.status = BreakpointStatus::Unbound;
                info.diagnostics = "No matching source location found in current chunk";
            }
        }
        
        infos.push_back(info);
    }
    
    return infos;
}

void DebugController::notify_debug_event(DebugEventType type, const std::string& reason) {
    if (debug_callback_) {
        DebugSnapshot snapshot;
        snapshot.call_stack = vm_.get_call_stack();
        snapshot.current_frame = 0;
        snapshot.current_position = vm_.get_current_source_position().value_or(SourcePosition());
        snapshot.reason = reason;
        
        debug_callback_(snapshot);
    }
}

std::optional<SourcePosition> DebugController::find_breakpoint_position(const BreakpointLocation& bp) const {
    const Chunk* chunk = vm_.current_chunk();
    if (!chunk) return std::nullopt;
    
    for (std::size_t ip = 0; ip < chunk->source_map.size(); ++ip) {
        const auto& src = chunk->source_map[ip];
        if (src.source_path == bp.source_position.source_path &&
            src.line == bp.source_position.line) {
            return SourcePosition(src.source_path, src.line, src.column);
        }
    }
    return std::nullopt;
}

// DebugVM implementation - wraps VM with DebugController for backward compatibility
DebugVM::DebugVM(std::istream& input, std::ostream& output, std::uint64_t fuel)
    : vm_(std::make_unique<VM>(input, output, fuel)),
      controller_(std::make_unique<DebugController>(*vm_)) {
    vm_->set_debug_control(controller_.get());
}

DebugVM::~DebugVM() = default;

void DebugVM::set_debug_callback(DebugCallback callback) {
    controller_->set_debug_callback(std::move(callback));
}

void DebugVM::execute(const Chunk& chunk) {
    // Breakpoint index will be rebuilt lazily in debug_hook when chunk is set
    finished_ = false;
    try {
        vm_->execute(chunk);
    } catch (...) {
        finished_ = true;
        throw;
    }
    // Only mark finished if VM execution actually completed (not paused)
    if (!vm_->is_debug_paused()) {
        finished_ = true;
    }
}

void DebugVM::add_breakpoint(const BreakpointLocation& bp) {
    controller_->set_breakpoint(bp);
}

void DebugVM::remove_breakpoint(std::size_t id) {
    controller_->remove_breakpoint(id);
}

void DebugVM::set_breakpoints(const std::vector<BreakpointLocation>& bps) {
    for (const auto& bp : bps) {
        controller_->set_breakpoint(bp);
    }
}

std::vector<BreakpointLocation> DebugVM::get_breakpoints() const {
    return controller_->get_breakpoints();
}

void DebugVM::rebuild_breakpoint_index() {
    // Handled internally by DebugController
}

void DebugVM::continue_run() {
    controller_->continue_execution();
}

void DebugVM::step_into() {
    controller_->step_into();
}

void DebugVM::step_over() {
    controller_->step_over();
}

void DebugVM::step_out() {
    controller_->step_out();
}

void DebugVM::pause() {
    controller_->pause_execution();
}

std::optional<DebugSnapshot> DebugVM::get_debug_snapshot() const {
    return controller_->get_snapshot();
}

std::vector<SourcePosition> DebugVM::get_breakable_positions() const {
    return controller_->get_breakable_positions();
}

std::optional<std::string> DebugVM::get_source_text(const std::string& path, 
                                                    std::uint32_t start_line,
                                                    std::uint32_t end_line) const {
    return controller_->get_source_text(path, start_line, end_line);
}

std::optional<SourcePosition> DebugVM::get_source_position(std::size_t ip) const {
    return controller_->get_source_position(ip);
}

bool DebugVM::is_at_breakpoint() const {
    return controller_->is_paused();
}

std::optional<SourcePosition> DebugVM::current_position() const {
    return controller_->get_current_source_position();
}

std::size_t DebugVM::current_ip() const {
    return vm_->current_ip();
}

void DebugVM::select_frame(std::size_t frame_index) {
    controller_->select_frame(frame_index);
}

std::size_t DebugVM::selected_frame() const {
    return controller_->selected_frame();
}

std::optional<Value> DebugVM::evaluate_expression(const std::string& expr) {
    return controller_->evaluate_expression(expr);
}

// Interactive debug session implementation
namespace {
std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        result.push_back(trim(item));
    }
    return result;
}
} // anonymous namespace

int run_debug_session(const std::filesystem::path& source_file,
                     std::istream& input,
                     std::ostream& output,
                     std::ostream& error,
                     const CustomEmojiRegistry& registry) {
    // Compile the source file
    emojineer::Chunk chunk;
    try {
        chunk = emojineer::compile_file(source_file, registry);
    } catch (const std::exception& e) {
        error << "Debug session failed to compile: " << e.what() << "\n";
        return 1;
    }
    
    // Create DebugVM with the compiled chunk using pointer to allow recreation
    std::unique_ptr<emojineer::DebugVM> debug_vm = std::make_unique<emojineer::DebugVM>(input, output);
    
    // Set up debug callback for pause events
    debug_vm->set_debug_callback([&output](const emojineer::DebugSnapshot& snapshot) {
        output << "\n" << snapshot.reason << " at ";
        output << snapshot.current_position.source_path << ":";
        output << snapshot.current_position.line << "\n";
    });
    
    // Don't execute immediately - debugger starts in paused mode so user can set breakpoints first
    
    output << "Emojineer debugger (type 'help' for commands)\n";
    output << "Loaded: " << source_file.string() << "\n";
    output << "Set breakpoints with 'break <file:line>', then 'continue' to run\n";
    
    bool running = true;
    
    while (running) {
        // Show current position
        auto pos = debug_vm->current_position();
        if (pos) {
            output << "(" << pos->source_path << ":" << pos->line << ") ";
        }
        output << "> ";
        output.flush();
        
        std::string line;
        if (!std::getline(input, line)) {
            break;
        }
        
        line = trim(line);
        if (line.empty()) continue;
        
        auto parts = split(line, ' ');
        auto cmd = parts[0];
        
        if (cmd == "quit" || cmd == "q" || cmd == "exit") {
            running = false;
        }
        else if (cmd == "help" || cmd == "h") {
            output << "Commands:\n";
            output << "  break <file:line>  - Set breakpoint\n";
            output << "  continue, c         - Continue execution\n";
            output << "  step, s            - Step into\n";
            output << "  next, n            - Step over\n";
            output << "  out, o             - Step out\n";
            output << "  backtrace, bt      - Show call stack\n";
            output << "  frame, f          - Show current frame\n";
            output << "  locals             - Show local variables\n";
            output << "  globals            - Show global variables\n";
            output << "  print, p <expr>   - Evaluate expression\n";
            output << "  list, l [n]       - List source lines\n";
            output << "  run                - Restart debug session\n";
            output << "  help, h            - Show this help\n";
            output << "  quit, q            - Exit debugger\n";
        }
        else if (cmd == "continue" || cmd == "c") {
            debug_vm->continue_run();
            debug_vm->execute(chunk);
            auto pos = debug_vm->current_position();
            if (pos) {
                output << "Continued to " << pos->source_path << ":" << pos->line << "\n";
            }
        }
        else if (cmd == "step" || cmd == "s") {
            debug_vm->step_into();
            debug_vm->execute(chunk);
            auto pos = debug_vm->current_position();
            if (pos) {
                output << "Stepped to " << pos->source_path << ":" << pos->line << "\n";
            }
        }
        else if (cmd == "next" || cmd == "n") {
            debug_vm->step_over();
            debug_vm->execute(chunk);
            auto pos = debug_vm->current_position();
            if (pos) {
                output << "Stepped over to " << pos->source_path << ":" << pos->line << "\n";
            }
        }
        else if (cmd == "out" || cmd == "o") {
            debug_vm->step_out();
            debug_vm->execute(chunk);
            auto pos = debug_vm->current_position();
            if (pos) {
                output << "Stepped out to " << pos->source_path << ":" << pos->line << "\n";
            }
        }
        else if (cmd == "break" || cmd == "b") {
            if (parts.size() < 2) {
                output << "Usage: break <file:line>\n";
                continue;
            }
            auto arg = parts[1];
            auto colon = arg.find(':');
            if (colon == std::string::npos) {
                output << "Usage: break <file:line>\n";
                continue;
            }
            std::string file = arg.substr(0, colon);
            std::string line_str = arg.substr(colon + 1);
            try {
                std::uint32_t line_num = std::stoul(line_str);
                emojineer::BreakpointLocation bp;
                bp.source_position.source_path = file;
                bp.source_position.line = line_num;
                debug_vm->add_breakpoint(bp);
                output << "Breakpoint set at " << file << ":" << line_num << "\n";
            } catch (...) {
                output << "Invalid line number\n";
            }
        }
        else if (cmd == "backtrace" || cmd == "bt") {
            auto snapshot = debug_vm->get_debug_snapshot();
            if (!snapshot) {
                output << "Not paused\n";
                continue;
            }
            output << "Call stack:\n";
            for (std::size_t i = 0; i < snapshot->call_stack.size(); ++i) {
                const auto& frame = snapshot->call_stack[i];
                output << "#" << i << " " << frame.function_name << " at ";
                output << frame.source_position.source_path << ":" << frame.source_position.line << "\n";
            }
        }
        else if (cmd == "frame" || cmd == "f") {
            auto snapshot = debug_vm->get_debug_snapshot();
            if (!snapshot) {
                output << "Not paused\n";
                continue;
            }
            // Accept optional frame index - use controller's frame selection
            if (parts.size() >= 2) {
                try {
                    std::size_t new_frame = std::stoul(parts[1]);
                    if (new_frame >= snapshot->call_stack.size()) {
                        output << "Frame #" << new_frame << " out of range (max is " 
                               << (snapshot->call_stack.size() - 1) << ")\n";
                        continue;
                    }
                    debug_vm->select_frame(new_frame);
                } catch (...) {
                    output << "Invalid frame number\n";
                    continue;
                }
            }
            // Use controller's selected frame (which clamps automatically)
            std::size_t current_frame = debug_vm->selected_frame();
            const auto& frame = snapshot->call_stack[current_frame];
            output << "Frame #" << current_frame << ": " << frame.function_name << "\n";
            output << "  at " << frame.source_position.source_path << ":" << frame.source_position.line << "\n";
            // Show parameters and locals for this frame
            if (!frame.named_parameters.empty()) {
                output << "  Parameters:\n";
                for (const auto& p : frame.named_parameters) {
                    output << "    " << p.first << " = " << emojineer::debug_render_value(p.second) << "\n";
                }
            }
            if (!frame.named_locals.empty()) {
                output << "  Locals:\n";
                for (const auto& l : frame.named_locals) {
                    output << "    " << l.first << " = " << emojineer::debug_render_value(l.second) << "\n";
                }
            }
        }
        else if (cmd == "locals") {
            auto snapshot = debug_vm->get_debug_snapshot();
            if (!snapshot) {
                output << "Not paused\n";
                continue;
            }
            // Use controller's selected frame
            std::size_t current_frame = debug_vm->selected_frame();
            if (current_frame >= snapshot->call_stack.size()) {
                current_frame = 0;
                debug_vm->select_frame(0);
            }
            const auto& frame = snapshot->call_stack[current_frame];
            output << "Locals in frame #" << current_frame << ":\n";
            // Show named parameters with real names
            if (!frame.named_parameters.empty()) {
                output << "Parameters:\n";
                for (const auto& p : frame.named_parameters) {
                    output << "  " << p.first << " = " << emojineer::debug_render_value(p.second) << "\n";
                }
            }
            // Show named locals with real names (excluding parameters which are already shown)
            if (!frame.named_locals.empty()) {
                output << "Locals:\n";
                for (const auto& l : frame.named_locals) {
                    // Skip parameters (they're in named_parameters too)
                    if (frame.named_parameters.find(l.first) == frame.named_parameters.end()) {
                        output << "  " << l.first << " = " << emojineer::debug_render_value(l.second) << "\n";
                    }
                }
            }
            if (frame.named_parameters.empty() && frame.named_locals.empty()) {
                output << "  (no locals or parameters)\n";
            }
        }
        else if (cmd == "globals") {
            auto snapshot = debug_vm->get_debug_snapshot();
            if (!snapshot) {
                output << "Not paused\n";
                continue;
            }
            // Use controller's selected frame
            std::size_t current_frame = debug_vm->selected_frame();
            if (current_frame >= snapshot->call_stack.size()) {
                current_frame = 0;
            }
            const auto& frame = snapshot->call_stack[current_frame];
            output << "Globals in frame #" << current_frame << ":\n";
            for (const auto& g : frame.globals) {
                output << "  " << g.first << " = " << emojineer::debug_render_value(g.second) << "\n";
            }
            if (frame.globals.empty()) {
                output << "  (no globals)\n";
            }
        }
        else if (cmd == "print" || cmd == "p") {
            if (parts.size() < 2) {
                output << "Usage: print <expression>\n";
                continue;
            }
            auto snapshot = debug_vm->get_debug_snapshot();
            if (!snapshot) {
                output << "Not paused\n";
                continue;
            }
            // Use controller's evaluate_expression which uses selected frame
            std::string identifier = parts[1];
            auto value = debug_vm->evaluate_expression(identifier);
            if (value) {
                output << identifier << " = " << emojineer::debug_render_value(*value) << "\n";
            } else {
                output << "Variable '" << identifier << "' not found in selected frame\n";
            }
        }
        else if (cmd == "list" || cmd == "l") {
            auto pos = debug_vm->current_position();
            if (!pos) {
                output << "No current position\n";
                continue;
            }
            std::uint32_t start = 1;
            if (parts.size() >= 2) {
                try {
                    start = std::stoul(parts[1]);
                } catch (...) {}
            }
            std::uint32_t end = start + 10;
            auto source_text = debug_vm->get_source_text(pos->source_path, start, end);
            if (source_text) {
                output << *source_text << "\n";
            } else {
                output << "Could not read source\n";
            }
        }
        else if (cmd == "run") {
            // Restart the debug session but preserve breakpoints
            auto breakpoints = debug_vm->get_breakpoints();
            debug_vm = std::make_unique<emojineer::DebugVM>(input, output);
            debug_vm->set_debug_callback([&output](const emojineer::DebugSnapshot& snapshot) {
                output << "\n" << snapshot.reason << " at ";
                output << snapshot.current_position.source_path << ":";
                output << snapshot.current_position.line << "\n";
            });
            // Restore breakpoints
            debug_vm->set_breakpoints(breakpoints);
            output << "Restarted debug session (breakpoints preserved)\n";
        }
        else {
            output << "Unknown command: " << cmd << " (type 'help' for commands)\n";
        }
        
        if (debug_vm->is_finished()) {
            output << "Program finished\n";
            break;
        }
    }
    
    output << "Exiting debugger\n";
    return 0;
}

} // namespace emojineer
