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

// DebugVM implementation
DebugVM::DebugVM(std::istream& input, std::ostream& output, std::uint64_t fuel)
    : input_(input), output_(output), fuel_(fuel) {}

void DebugVM::set_debug_callback(DebugCallback callback) {
    debug_callback_ = std::move(callback);
}

void DebugVM::set_breakpoints(const std::vector<BreakpointLocation>& bps) {
    breakpoints_ = bps;
    rebuild_breakpoint_index();
}

void DebugVM::add_breakpoint(const BreakpointLocation& bp) {
    breakpoints_.push_back(bp);
    rebuild_breakpoint_index();
}

void DebugVM::remove_breakpoint(std::size_t id) {
    if (id < breakpoints_.size()) {
        breakpoints_.erase(breakpoints_.begin() + id);
        rebuild_breakpoint_index();
    }
}

void DebugVM::rebuild_breakpoint_index() {
    breakpoint_id_by_ip_.clear();
    for (std::size_t i = 0; i < breakpoints_.size(); ++i) {
        // Find matching source position in source map
        for (std::size_t ip = 0; ip < current_chunk_->source_map.size(); ++ip) {
            const auto& src = current_chunk_->source_map[ip];
            if (src.source_path == breakpoints_[i].source_position.source_path &&
                src.line == breakpoints_[i].source_position.line) {
                breakpoint_id_by_ip_[ip] = i;
            }
        }
    }
}

void DebugVM::execute(const Chunk& c) {
    verify_bytecode(c);
    current_chunk_ = &c;
    stack_.clear();
    frames_.clear();
    globals_.clear();
    ip_ = 0;
    finished_ = false;
    call_stack_positions_.clear();
    
    std::uint64_t remaining = fuel_;
    
    // Set up initial source position
    if (!c.source_map.empty() && ip_ < c.source_map.size()) {
        call_stack_positions_.push_back(c.source_map[ip_]);
    }
    
    while (ip_ < c.code.size() && !finished_) {
        if (remaining == 0) {
            runtime_error("execution fuel exhausted (possible infinite loop)");
        }
        --remaining;
        
        // Check for breakpoint before executing instruction
        if (run_mode_ != DebugRunMode::Running) {
            check_breakpoint();
        }
        
        execute_instruction();
        
        // Handle step modes
        if (run_mode_ == DebugRunMode::SteppingInto) {
            run_mode_ = DebugRunMode::Paused;
            notify_debug_event(DebugEventType::StepComplete, "stepped into");
            wait_for_continue();
        } else if (run_mode_ == DebugRunMode::SteppingOver) {
            if (ip_ >= step_over_start_ip_) {
                run_mode_ = DebugRunMode::Paused;
                notify_debug_event(DebugEventType::StepComplete, "stepped over");
                wait_for_continue();
            }
        } else if (run_mode_ == DebugRunMode::SteppingOut) {
            if (frames_.size() <= step_out_frame_depth_) {
                run_mode_ = DebugRunMode::Paused;
                notify_debug_event(DebugEventType::StepComplete, "stepped out");
                wait_for_continue();
            }
        }
    }
    
    if (!frames_.empty()) {
        runtime_error("program ended inside a function");
    }
    if (!stack_.empty()) {
        runtime_error("program ended with values on stack");
    }
    
    notify_debug_event(DebugEventType::ProgramExit, "program completed");
}

void DebugVM::execute_instruction() {
    if (ip_ >= current_chunk_->code.size()) {
        runtime_error("instruction pointer out of bounds");
    }
    
    const Instruction ins = current_chunk_->code[ip_++];
    const auto line = ins.line;
    
    // Update source position
    if (ip_ < current_chunk_->source_map.size()) {
        call_stack_positions_.back() = current_chunk_->source_map[ip_ - 1];
    }
    
    switch (ins.op) {
        case OpCode::Constant:
            stack_.push_back(current_chunk_->constants.at(static_cast<std::size_t>(ins.operand)));
            break;
            
        case OpCode::LoadGlobal: {
            auto n = constant_string(ins.operand, line);
            auto it = globals_.find(n);
            if (it == globals_.end()) runtime_error("undefined emoji variable '" + n + "'");
            stack_.push_back(it->second);
            break;
        }
        
        case OpCode::StoreGlobal: {
            auto n = constant_string(ins.operand, line);
            globals_[n] = pop(line);
            break;
        }
        
        case OpCode::LoadLocal: {
            auto& f = frame(line);
            auto s = static_cast<std::size_t>(ins.operand);
            if (s >= f.locals.size()) runtime_error("invalid local slot");
            stack_.push_back(f.locals[s]);
            break;
        }
        
        case OpCode::StoreLocal: {
            auto& f = frame(line);
            auto s = static_cast<std::size_t>(ins.operand);
            if (s >= f.locals.size()) runtime_error("invalid local slot");
            f.locals[s] = pop(line);
            break;
        }
        
        case OpCode::AssertNumber:
            if (!std::holds_alternative<double>(peek(line)) && 
                !std::holds_alternative<std::int64_t>(peek(line))) {
                runtime_error("🔢 variable requires a number");
            }
            break;
            
        case OpCode::AssertString:
            if (!std::holds_alternative<std::string>(peek(line))) {
                runtime_error("🔤 variable requires text");
            }
            break;
            
        case OpCode::AssertBool:
            if (!std::holds_alternative<bool>(peek(line))) {
                runtime_error("🎯 variable requires ✅ or ❌");
            }
            break;
            
        case OpCode::AssertArray:
            if (!std::holds_alternative<ArrayPtr>(peek(line))) {
                runtime_error("📚 variable requires an array");
            }
            break;
            
        case OpCode::Add: {
            Value r = pop(line), l = pop(line);
            if (auto* ln = std::get_if<double>(&l)) {
                if (auto* rn = std::get_if<double>(&r)) {
                    stack_.emplace_back(*ln + *rn);
                } else runtime_error("➕ requires two numbers or two text values");
            } else if (auto* li = std::get_if<std::int64_t>(&l)) {
                if (auto* ri = std::get_if<std::int64_t>(&r)) {
                    // Use checked_add
                    auto result = *li + *ri;
                    if ((*li > 0 && *ri > 0 && result < 0) ||
                        (*li < 0 && *ri < 0 && result > 0)) {
                        runtime_error("integer overflow");
                    }
                    stack_.emplace_back(result);
                } else runtime_error("➕ requires two numbers or two text values");
            } else if (auto* ls = std::get_if<std::string>(&l)) {
                if (auto* rs = std::get_if<std::string>(&r)) {
                    stack_.emplace_back(*ls + *rs);
                } else runtime_error("➕ requires two numbers or two text values");
            } else runtime_error("➕ requires two numbers or two text values");
            break;
        }
        
        case OpCode::Subtract: {
            double r = pop_number(line), l = pop_number(line);
            stack_.emplace_back(l - r);
            break;
        }
        
        case OpCode::Multiply: {
            double r = pop_number(line), l = pop_number(line);
            stack_.emplace_back(l * r);
            break;
        }
        
        case OpCode::Divide: {
            double r = pop_number(line), l = pop_number(line);
            if (r == 0) runtime_error("division by zero");
            stack_.emplace_back(l / r);
            break;
        }
        
        case OpCode::Modulo: {
            double r = pop_number(line), l = pop_number(line);
            if (r == 0) runtime_error("modulo by zero");
            stack_.emplace_back(std::fmod(l, r));
            break;
        }
        
        case OpCode::AddInt: {
            auto r = pop_int64(line), l = pop_int64(line);
            auto result = l + r;
            if ((l > 0 && r > 0 && result < 0) || (l < 0 && r < 0 && result > 0)) {
                runtime_error("integer overflow");
            }
            stack_.emplace_back(result);
            break;
        }
        
        case OpCode::SubtractInt: {
            auto r = pop_int64(line), l = pop_int64(line);
            auto result = l - r;
            if ((l > 0 && r < 0 && result < 0) || (l < 0 && r > 0 && result > 0)) {
                runtime_error("integer overflow");
            }
            stack_.emplace_back(result);
            break;
        }
        
        case OpCode::MultiplyInt: {
            auto r = pop_int64(line), l = pop_int64(line);
            if (l == 0 || r == 0) {
                stack_.emplace_back(0);
            } else {
                auto result = l * r;
                if (result / l != r) runtime_error("integer overflow");
                stack_.emplace_back(result);
            }
            break;
        }
        
        case OpCode::Equal: {
            Value r = pop(line), l = pop(line);
            stack_.emplace_back(values_equal(l, r));
            break;
        }
        
        case OpCode::Less: {
            double r = pop_number(line), l = pop_number(line);
            stack_.emplace_back(l < r);
            break;
        }
        
        case OpCode::Greater: {
            double r = pop_number(line), l = pop_number(line);
            stack_.emplace_back(l > r);
            break;
        }
        
        case OpCode::Negate:
            stack_.emplace_back(-pop_number(line));
            break;
            
        case OpCode::Not:
            stack_.emplace_back(!pop_bool(line));
            break;
            
        case OpCode::ReadLine: {
            std::string v;
            if (!std::getline(input_, v)) {
                // Don't error - just return empty string for debugging
                stack_.emplace_back(std::string{});
            } else {
                stack_.emplace_back(std::move(v));
            }
            break;
        }
        
        case OpCode::Print:
            output_ << value_to_string(pop(line)) << '\n';
            break;
            
        case OpCode::JumpIfFalse: {
            bool cond = pop_bool(line);
            if (!cond) ip_ = static_cast<std::size_t>(ins.operand);
            break;
        }
        
        case OpCode::Jump:
            ip_ = static_cast<std::size_t>(ins.operand);
            break;
            
        case OpCode::Call:
            call_function(static_cast<std::size_t>(ins.operand));
            break;
            
        case OpCode::Return:
            return_from_function();
            break;
            
        case OpCode::MakeArray: {
            auto count = static_cast<std::size_t>(ins.operand);
            if (stack_.size() < count) runtime_error("not enough values for array literal");
            auto a = std::make_shared<ArrayValue>();
            a->elements.resize(count);
            for (std::size_t n = count; n > 0; --n) {
                a->elements[n - 1] = pop(line);
            }
            stack_.emplace_back(std::move(a));
            break;
        }
        
        case OpCode::Index: {
            Value iv = pop(line), cv = pop(line);
            auto* ap = std::get_if<ArrayPtr>(&cv);
            if (!ap || !*ap) runtime_error("🔎 requires an array");
            double raw;
            if (auto* d = std::get_if<double>(&iv)) raw = *d;
            else if (auto* i = std::get_if<std::int64_t>(&iv)) raw = static_cast<double>(*i);
            else runtime_error("🔎 index must be a whole number");
            if (!std::isfinite(raw) || std::floor(raw) != raw || raw < 0 || 
                raw >= static_cast<double>((*ap)->elements.size())) {
                runtime_error("🔎 array index out of range");
            }
            stack_.push_back((*ap)->elements[static_cast<std::size_t>(raw)]);
            break;
        }
        
        case OpCode::Length: {
            Value v = pop(line);
            if (auto* ap = std::get_if<ArrayPtr>(&v)) {
                if (!*ap) runtime_error("📏 received a null array");
                stack_.emplace_back(static_cast<double>((*ap)->elements.size()));
            } else if (auto* text = std::get_if<std::string>(&v)) {
                stack_.emplace_back(static_cast<double>(segment_graphemes(*text).size()));
            } else runtime_error("📏 requires an array or text value");
            break;
        }
        
        case OpCode::Append: {
            Value value = pop(line), cv = pop(line);
            auto* ap = std::get_if<ArrayPtr>(&cv);
            if (!ap || !*ap) runtime_error("📎 requires an array");
            auto next = std::make_shared<ArrayValue>(**ap);
            next->elements.push_back(std::move(value));
            stack_.emplace_back(std::move(next));
            break;
        }
        
        case OpCode::SetIndex: {
            Value value = pop(line), iv = pop(line), cv = pop(line);
            auto* ap = std::get_if<ArrayPtr>(&cv);
            if (!ap || !*ap) runtime_error("🧷 requires an array");
            double raw;
            if (auto* d = std::get_if<double>(&iv)) raw = *d;
            else if (auto* i = std::get_if<std::int64_t>(&iv)) raw = static_cast<double>(*i);
            else runtime_error("🧷 index must be a whole number");
            if (!std::isfinite(raw) || std::floor(raw) != raw || raw < 0 || 
                raw >= static_cast<double>((*ap)->elements.size())) {
                runtime_error("🧷 array index out of range");
            }
            auto next = std::make_shared<ArrayValue>(**ap);
            next->elements[static_cast<std::size_t>(raw)] = std::move(value);
            stack_.emplace_back(std::move(next));
            break;
        }
        
        case OpCode::Halt:
            finished_ = true;
            break;
    }
}

void DebugVM::call_function(std::size_t function_index) {
    const auto& fn = current_chunk_->functions[function_index];
    if (stack_.size() < fn.arity) {
        runtime_error("not enough call arguments on VM stack");
    }
    if (frames_.size() >= 4096) {
        runtime_error("maximum function call depth exceeded");
    }
    
    CallFrame f;
    f.return_ip = ip_;
    f.stack_base = stack_.size() - fn.arity;
    f.function_index = function_index;
    f.locals.resize(fn.local_count, Value{});
    
    // Copy parameters to locals
    for (std::size_t n = fn.arity; n > 0; --n) {
        f.locals[n - 1] = pop(current_chunk_->code[ip_].line);
    }
    
    frames_.push_back(std::move(f));
    ip_ = fn.entry;
    
    // Update call stack position
    if (ip_ < current_chunk_->source_map.size()) {
        call_stack_positions_.push_back(current_chunk_->source_map[ip_]);
    }
}

void DebugVM::return_from_function() {
    if (frames_.empty()) runtime_error("Return executed outside a function");
    
    Value result = pop(current_chunk_->code[ip_].line);
    CallFrame f = std::move(frames_.back());
    frames_.pop_back();
    
    if (stack_.size() != f.stack_base) {
        runtime_error("function returned with leaked operand stack values");
    }
    
    ip_ = f.return_ip;
    stack_.push_back(std::move(result));
    
    // Pop call stack position
    if (!call_stack_positions_.empty()) {
        call_stack_positions_.pop_back();
    }
}

void DebugVM::check_breakpoint() {
    // Check if current IP matches a breakpoint
    auto it = breakpoint_id_by_ip_.find(ip_);
    if (it != breakpoint_id_by_ip_.end()) {
        const auto& bp = breakpoints_[it->second];
        if (bp.enabled) {
            hit_breakpoints_.insert(it->second);
            run_mode_ = DebugRunMode::Paused;
            notify_debug_event(DebugEventType::BreakpointHit, 
                "breakpoint " + std::to_string(it->second) + " hit");
            wait_for_continue();
        }
    }
}

void DebugVM::notify_debug_event(DebugEventType type, const std::string& reason) {
    if (!debug_callback_) return;
    
    DebugSnapshot snapshot;
    snapshot.reason = reason;
    snapshot.current_frame = 0;
    
    // Build call stack
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        DebugFrame frame;
        frame.frame_index = i;
        
        const auto& fn = current_chunk_->functions[frames_[i].function_index];
        frame.function_name = fn.name;
        
        if (i < call_stack_positions_.size()) {
            frame.source_position = call_stack_positions_[i];
        }
        
        // Copy locals
        frame.locals = frames_[i].locals;
        
        // Parameters are first N locals based on arity
        frame.parameters = std::vector<Value>(frame.locals.begin(), 
            frame.locals.begin() + std::min(frame.locals.size(), static_cast<size_t>(fn.arity)));
        
        snapshot.call_stack.push_back(std::move(frame));
    }
    
    // Current position
    if (ip_ < current_chunk_->source_map.size()) {
        snapshot.current_position = current_chunk_->source_map[ip_];
    }
    
    debug_callback_(snapshot);
}

void DebugVM::wait_for_continue() {
    // This is a simplified version - in a real implementation,
    // this would block waiting for debugger commands
    // For now, we just continue execution
}

void DebugVM::continue_run() {
    run_mode_ = DebugRunMode::Running;
}

void DebugVM::step_into() {
    run_mode_ = DebugRunMode::SteppingInto;
}

void DebugVM::step_over() {
    run_mode_ = DebugRunMode::SteppingOver;
    step_over_start_ip_ = ip_ + 1;
}

void DebugVM::step_out() {
    run_mode_ = DebugRunMode::SteppingOut;
    step_out_frame_depth_ = frames_.size();
}

void DebugVM::pause() {
    run_mode_ = DebugRunMode::Paused;
    notify_debug_event(DebugEventType::Pause, "paused by user");
}

std::optional<DebugSnapshot> DebugVM::get_debug_snapshot() const {
    if (!current_chunk_) return std::nullopt;
    
    DebugSnapshot snapshot;
    snapshot.current_frame = 0;
    
    // Build call stack
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        DebugFrame frame;
        frame.frame_index = i;
        
        const auto& fn = current_chunk_->functions[frames_[i].function_index];
        frame.function_name = fn.name;
        
        if (i < call_stack_positions_.size()) {
            frame.source_position = call_stack_positions_[i];
        }
        
        // Copy locals
        frame.locals = frames_[i].locals;
        
        // Parameters
        frame.parameters = std::vector<Value>(frame.locals.begin(), 
            frame.locals.begin() + std::min(frame.locals.size(), static_cast<size_t>(fn.arity)));
        
        // Globals (only for the first frame)
        if (i == 0) {
            frame.globals = globals_;
        }
        
        snapshot.call_stack.push_back(std::move(frame));
    }
    
    // Current position
    if (ip_ < current_chunk_->source_map.size()) {
        snapshot.current_position = current_chunk_->source_map[ip_];
    }
    
    return snapshot;
}

std::vector<SourcePosition> DebugVM::get_breakable_positions() const {
    std::vector<SourcePosition> positions;
    if (!current_chunk_) return positions;
    
    for (std::size_t ip = 0; ip < current_chunk_->source_map.size(); ++ip) {
        positions.push_back(current_chunk_->source_map[ip]);
    }
    
    return positions;
}

bool DebugVM::is_at_breakpoint() const {
    return breakpoint_id_by_ip_.contains(ip_);
}

std::optional<SourcePosition> DebugVM::current_position() const {
    if (!current_chunk_ || ip_ >= current_chunk_->source_map.size()) {
        return std::nullopt;
    }
    return current_chunk_->source_map[ip_];
}

std::optional<std::string> DebugVM::get_source_text(const std::string& path, 
                                                     std::uint32_t start_line,
                                                     std::uint32_t end_line) const {
    // This would need filesystem access - return empty for now
    // In a real implementation, we'd read the source file
    return std::nullopt;
}

std::optional<SourcePosition> DebugVM::get_source_position(std::size_t ip) const {
    if (!current_chunk_ || ip >= current_chunk_->source_map.size()) {
        return std::nullopt;
    }
    return current_chunk_->source_map[ip];
}

Value DebugVM::pop(std::uint32_t line) {
    if (stack_.empty()) runtime_error("VM stack underflow");
    Value v = std::move(stack_.back());
    stack_.pop_back();
    return v;
}

const Value& DebugVM::peek(std::uint32_t line) const {
    if (stack_.empty()) runtime_error("VM stack underflow");
    return stack_.back();
}

bool DebugVM::pop_bool(std::uint32_t line) {
    Value v = pop(line);
    if (auto* b = std::get_if<bool>(&v)) return *b;
    runtime_error("condition requires ✅ or ❌");
}

double DebugVM::pop_number(std::uint32_t line) {
    Value v = pop(line);
    if (auto* n = std::get_if<double>(&v)) return *n;
    if (auto* i = std::get_if<std::int64_t>(&v)) return static_cast<double>(*i);
    runtime_error("numeric operation requires numbers");
}

std::int64_t DebugVM::pop_int64(std::uint32_t line) {
    Value v = pop(line);
    if (auto* i = std::get_if<std::int64_t>(&v)) return *i;
    runtime_error("integer operation requires integers");
}

std::string DebugVM::constant_string(std::int32_t idx, std::uint32_t line) const {
    if (idx < 0 || static_cast<std::size_t>(idx) >= current_chunk_->constants.size()) {
        runtime_error("invalid constant index");
    }
    auto* s = std::get_if<std::string>(&current_chunk_->constants[static_cast<std::size_t>(idx)]);
    if (!s) runtime_error("bytecode expected string constant");
    return *s;
}

DebugVM::CallFrame& DebugVM::frame(std::uint32_t line) {
    if (frames_.empty()) runtime_error("local variable access outside function");
    return frames_.back();
}

const DebugVM::CallFrame& DebugVM::frame(std::uint32_t line) const {
    if (frames_.empty()) runtime_error("local variable access outside function");
    return frames_.back();
}

[[noreturn]] void DebugVM::runtime_error(const std::string& m) const {
    throw std::runtime_error("runtime: " + m);
}

// Interactive debug session implementation
namespace {

std::string trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(*start)) start++;
    auto end = s.end();
    do { end--; } while (std::distance(start, end) > 0 && std::isspace(*end));
    return std::string(start, end + 1);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::istringstream stream(s);
    std::string item;
    while (std::getline(stream, item, delim)) {
        result.push_back(item);
    }
    return result;
}

} // anonymous namespace

int run_debug_session(const std::filesystem::path& source_file,
                     std::istream& input,
                     std::ostream& output,
                     std::ostream& error,
                     const CustomEmojiRegistry& registry) {
    output << "Emojineer Debugger v0.18\n";
    output << "Type 'help' for available commands\n\n";
    
    // Compile the source
    emojineer::Chunk chunk;
    try {
        chunk = emojineer::compile_file(source_file, registry);
    } catch (const std::exception& e) {
        error << "debug: compilation failed: " << e.what() << "\n";
        return 1;
    }
    
    // Create debugger VM
    emojineer::DebugVM debug_vm(input, output);
    debug_vm.set_breakpoints({});
    
    // Debug state
    std::vector<emojineer::BreakpointLocation> breakpoints;
    std::size_t next_breakpoint_id = 0;
    bool running = false;
    
    auto print_position = [&](const emojineer::SourcePosition& pos) {
        output << pos.source_path << ":" << pos.line << ":" << pos.column << "\n";
    };
    
    auto print_frame = [&](const emojineer::DebugFrame& frame, std::size_t idx) {
        output << "#" << idx << " " << frame.function_name << " at ";
        print_position(frame.source_position);
    };
    
    auto print_locals = [&](const emojineer::DebugFrame& frame) {
        if (frame.locals.empty()) {
            output << "  (no locals)\n";
            return;
        }
        for (std::size_t i = 0; i < frame.locals.size(); ++i) {
            output << "  local " << i << " = " << emojineer::debug_render_value(frame.locals[i]) << "\n";
        }
    };
    
    auto print_globals = [&](const std::unordered_map<std::string, emojineer::Value>& globals) {
        if (globals.empty()) {
            output << "  (no globals)\n";
            return;
        }
        for (const auto& [name, value] : globals) {
            output << "  " << name << " = " << emojineer::debug_render_value(value) << "\n";
        }
    };
    
    // Main debug loop
    std::string line;
    while (true) {
        if (!running) {
            output << "(emojineer debug) ";
        }
        
        if (!std::getline(input, line)) break;
        
        line = trim(line);
        if (line.empty()) continue;
        
        auto parts = split(line, ' ');
        auto cmd = parts[0];
        
        if (cmd == "quit" || cmd == "q" || cmd == "exit") {
            output << "Goodbye!\n";
            break;
        }
        
        if (cmd == "help" || cmd == "h") {
            output << "Available commands:\n";
            output << "  break <file:line>     - Set breakpoint at source location\n";
            output << "  delete <id>          - Delete breakpoint\n";
            output << "  list                 - List breakpoints\n";
            output << "  run                  - Start/continue execution\n";
            output << "  continue             - Continue execution (alias for run)\n";
            output << "  step                 - Step into\n";
            output << "  next                 - Step over\n";
            output << "  out                  - Step out\n";
            output << "  pause                - Pause execution\n";
            output << "  backtrace            - Show call stack (bt, where)\n";
            output << "  frame <n>            - Select frame\n";
            output << "  locals               - Show local variables\n";
            output << "  globals              - Show global variables\n";
            output << "  print <expr>         - Evaluate expression\n";
            output << "  source               - Show current source line\n";
            output << "  list <line>          - List source around line\n";
            output << "  help                 - Show this help\n";
            output << "  quit                 - Exit debugger\n";
            continue;
        }
        
        if (cmd == "break" || cmd == "b") {
            if (parts.size() < 2) {
                output << "Usage: break <file:line>\n";
                continue;
            }
            // Parse file:line format
            auto loc = parts[1];
            auto colon = loc.find(':');
            if (colon == std::string::npos) {
                output << "Usage: break <file:line> (e.g., break main.emoji:10)\n";
                continue;
            }
            try {
                auto file = loc.substr(0, colon);
                auto line_num = std::stoul(loc.substr(colon + 1));
                
                emojineer::BreakpointLocation bp;
                bp.source_position.source_path = file;
                bp.source_position.line = static_cast<std::uint32_t>(line_num);
                bp.source_position.column = 1;
                bp.enabled = true;
                
                auto id = next_breakpoint_id++;
                breakpoints.push_back(bp);
                debug_vm.set_breakpoints(breakpoints);
                
                output << "Breakpoint " << id << " set at " << file << ":" << line_num << "\n";
            } catch (const std::exception& e) {
                output << "Invalid breakpoint location: " << e.what() << "\n";
            }
            continue;
        }
        
        if (cmd == "delete" || cmd == "d") {
            if (parts.size() < 2) {
                output << "Usage: delete <id>\n";
                continue;
            }
            try {
                auto id = std::stoul(parts[1]);
                if (id < breakpoints.size()) {
                    breakpoints.erase(breakpoints.begin() + id);
                    debug_vm.set_breakpoints(breakpoints);
                    output << "Breakpoint " << id << " deleted\n";
                } else {
                    output << "No breakpoint " << id << "\n";
                }
            } catch (const std::exception& e) {
                output << "Invalid breakpoint id\n";
            }
            continue;
        }
        
        if (cmd == "list" || cmd == "l") {
            if (breakpoints.empty()) {
                output << "No breakpoints set\n";
            } else {
                output << "Breakpoints:\n";
                for (std::size_t i = 0; i < breakpoints.size(); ++i) {
                    const auto& bp = breakpoints[i];
                    output << "  " << i << ": " << bp.source_position.source_path 
                           << ":" << bp.source_position.line;
                    if (!bp.enabled) output << " (disabled)";
                    output << "\n";
                }
            }
            continue;
        }
        
        if (cmd == "run" || cmd == "continue" || cmd == "c") {
            if (running) {
                debug_vm.continue_run();
            }
            running = true;
            
            // Run until breakpoint or completion
            debug_vm.set_debug_callback([&](const emojineer::DebugSnapshot& snapshot) {
                output << "\n" << snapshot.reason << "\n";
                print_position(snapshot.current_position);
                
                if (!snapshot.call_stack.empty()) {
                    const auto& frame = snapshot.call_stack[0];
                    print_frame(frame, 0);
                }
            });
            
            try {
                debug_vm.execute(chunk);
            } catch (const std::exception& e) {
                error << "Execution error: " << e.what() << "\n";
            }
            
            running = false;
            continue;
        }
        
        if (cmd == "step" || cmd == "s") {
            if (!running) {
                debug_vm.step_into();
                running = true;
                
                debug_vm.set_debug_callback([&](const emojineer::DebugSnapshot& snapshot) {
                    output << "\n" << snapshot.reason << "\n";
                    print_position(snapshot.current_position);
                });
                
                try {
                    debug_vm.execute(chunk);
                } catch (const std::exception& e) {
                    error << "Execution error: " << e.what() << "\n";
                }
                
                running = false;
            } else {
                debug_vm.step_into();
            }
            continue;
        }
        
        if (cmd == "next" || cmd == "n") {
            if (!running) {
                debug_vm.step_over();
                running = true;
                
                debug_vm.set_debug_callback([&](const emojineer::DebugSnapshot& snapshot) {
                    output << "\n" << snapshot.reason << "\n";
                    print_position(snapshot.current_position);
                });
                
                try {
                    debug_vm.execute(chunk);
                } catch (const std::exception& e) {
                    error << "Execution error: " << e.what() << "\n";
                }
                
                running = false;
            } else {
                debug_vm.step_over();
            }
            continue;
        }
        
        if (cmd == "out" || cmd == "o") {
            if (!running) {
                debug_vm.step_out();
                running = true;
                
                debug_vm.set_debug_callback([&](const emojineer::DebugSnapshot& snapshot) {
                    output << "\n" << snapshot.reason << "\n";
                    print_position(snapshot.current_position);
                });
                
                try {
                    debug_vm.execute(chunk);
                } catch (const std::exception& e) {
                    error << "Execution error: " << e.what() << "\n";
                }
                
                running = false;
            } else {
                debug_vm.step_out();
            }
            continue;
        }
        
        if (cmd == "pause") {
            debug_vm.pause();
            auto pos = debug_vm.current_position();
            if (pos) {
                output << "Paused at ";
                print_position(*pos);
            }
            continue;
        }
        
        if (cmd == "backtrace" || cmd == "bt" || cmd == "where") {
            auto snapshot = debug_vm.get_debug_snapshot();
            if (!snapshot || snapshot->call_stack.empty()) {
                output << "No call stack (program not running)\n";
            } else {
                output << "Call stack:\n";
                for (std::size_t i = 0; i < snapshot->call_stack.size(); ++i) {
                    const auto& frame = snapshot->call_stack[i];
                    output << "#" << i << " " << frame.function_name << " at ";
                    print_position(frame.source_position);
                }
            }
            continue;
        }
        
        if (cmd == "frame" || cmd == "f") {
            auto snapshot = debug_vm.get_debug_snapshot();
            if (!snapshot || snapshot->call_stack.empty()) {
                output << "No call stack\n";
                continue;
            }
            
            std::size_t frame_idx = 0;
            if (parts.size() >= 2) {
                try {
                    frame_idx = std::stoul(parts[1]);
                } catch (...) {
                    output << "Invalid frame number\n";
                    continue;
                }
            }
            
            if (frame_idx >= snapshot->call_stack.size()) {
                output << "Frame " << frame_idx << " out of range\n";
                continue;
            }
            
            const auto& frame = snapshot->call_stack[frame_idx];
            print_frame(frame, frame_idx);
            continue;
        }
        
        if (cmd == "locals" || cmd == "vars") {
            auto snapshot = debug_vm.get_debug_snapshot();
            if (!snapshot || snapshot->call_stack.empty()) {
                output << "No call stack\n";
            } else {
                const auto& frame = snapshot->call_stack[0];
                output << "Locals:\n";
                print_locals(frame);
            }
            continue;
        }
        
        if (cmd == "globals") {
            auto snapshot = debug_vm.get_debug_snapshot();
            if (!snapshot || snapshot->call_stack.empty()) {
                output << "No call stack\n";
            } else {
                const auto& frame = snapshot->call_stack[0];
                output << "Globals:\n";
                print_globals(frame.globals);
            }
            continue;
        }
        
        if (cmd == "print" || cmd == "p") {
            if (parts.size() < 2) {
                output << "Usage: print <expression>\n";
                continue;
            }
            // For now, just print the expression literally
            // A full implementation would need an expression evaluator
            output << "(expression evaluation not yet implemented)\n";
            continue;
        }
        
        if (cmd == "source" || cmd == ".") {
            auto pos = debug_vm.current_position();
            if (pos) {
                output << "Current location: ";
                print_position(*pos);
            } else {
                output << "No source position (program not running)\n";
            }
            continue;
        }
        
        if (cmd == "list") {
            auto pos = debug_vm.current_position();
            if (!pos) {
                output << "No current source position\n";
                continue;
            }
            
            std::uint32_t center_line = pos->line;
            if (parts.size() >= 2) {
                try {
                    center_line = std::stoul(parts[1]);
                } catch (...) {}
            }
            
            output << "Source around " << pos->source_path << ":" << center_line << ":\n";
            output << "(source listing not yet implemented - use 'source' command for position)\n";
            continue;
        }
        
        output << "Unknown command: " << cmd << ". Type 'help' for available commands.\n";
    }
    
    return 0;
}

} // namespace emojineer
