#include "emojineer/debugger.hpp"
#include "emojineer/module.hpp"
#include "emojineer/unicode.hpp"
#include <climits>
#include <cmath>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <sstream>

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

// DebugController implementation
DebugController::DebugController(VM& vm) : vm_(vm) {}

DebugController::~DebugController() = default;

void DebugController::set_debug_callback(DebugCallback callback) {
    debug_callback_ = std::move(callback);
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
    paused_ = false;
    vm_.set_debug_paused(false);
}

void DebugController::step_over() {
    run_mode_ = DebugRunMode::SteppingOver;
    step_over_start_ip_ = vm_.current_ip();
    paused_ = false;
    vm_.set_debug_paused(false);
}

void DebugController::step_out() {
    run_mode_ = DebugRunMode::SteppingOut;
    step_out_frame_depth_ = vm_.get_call_stack().size();
    paused_ = false;
    vm_.set_debug_paused(false);
}

std::optional<DebugSnapshot> DebugController::get_snapshot() const {
    if (!paused_ && run_mode_ != DebugRunMode::Paused) {
        return std::nullopt;
    }
    
    DebugSnapshot snapshot;
    snapshot.call_stack = vm_.get_call_stack();
    snapshot.current_frame = 0;
    snapshot.current_position = vm_.get_current_source_position().value_or(SourcePosition());
    snapshot.reason = pause_reason_;
    
    return snapshot;
}

std::optional<Value> DebugController::evaluate_expression(const std::string& expr) {
    // Expression evaluation requires a proper expression parser
    // For now, return nullopt - this is a placeholder
    (void)expr;
    return std::nullopt;
}

std::optional<std::string> DebugController::get_source_text(const std::string& path, 
                                                           std::uint32_t start_line,
                                                           std::uint32_t end_line) const {
    // This would need to read from the source file
    // For now, return nullopt - placeholder implementation
    (void)path;
    (void)start_line;
    (void)end_line;
    return std::nullopt;
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
    if (run_mode_ != DebugRunMode::Running) return false;
    
    std::size_t ip = vm_.current_ip();
    auto it = breakpoint_id_by_ip_.find(ip);
    if (it == breakpoint_id_by_ip_.end()) return false;
    
    std::size_t bp_id = it->second;
    if (bp_id >= breakpoints_.size()) return false;
    
    return breakpoints_[bp_id].enabled;
}

bool DebugController::should_pause_for_step() const {
    if (run_mode_ == DebugRunMode::Running || run_mode_ == DebugRunMode::Paused) {
        return false;
    }
    
    if (run_mode_ == DebugRunMode::SteppingInto) {
        return true;
    }
    
    if (run_mode_ == DebugRunMode::SteppingOver) {
        std::size_t current_ip = vm_.current_ip();
        return current_ip >= step_over_start_ip_;
    }
    
    if (run_mode_ == DebugRunMode::SteppingOut) {
        std::size_t current_depth = vm_.get_call_stack().size();
        return current_depth <= step_out_frame_depth_;
    }
    
    return false;
}

std::size_t DebugController::get_step_out_target_depth() const {
    return step_out_frame_depth_;
}

std::size_t DebugController::get_step_over_target_ip() const {
    return step_over_start_ip_;
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
    // Rebuild breakpoint index now that we have the chunk
    controller_->rebuild_breakpoint_index();
    
    finished_ = false;
    try {
        vm_->execute(chunk);
    } catch (...) {
        finished_ = true;
        throw;
    }
    finished_ = true;
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

// Interactive debug session implementation would go here
// For now, we'll provide a placeholder

int run_debug_session(const std::filesystem::path& source_file,
                     std::istream& input,
                     std::ostream& output,
                     std::ostream& error,
                     const CustomEmojiRegistry& registry) {
    (void)source_file;
    (void)input;
    (void)output;
    (void)error;
    (void)registry;
    
    // Placeholder - full implementation would:
    // 1. Compile the source file
    // 2. Create a DebugVM with the compiled chunk
    // 3. Run interactive command loop
    // 4. Handle all debugger commands
    
    error << "Interactive debug session not fully implemented\n";
    return 1;
}

} // namespace emojineer
