#pragma once
#include "emojineer/bytecode.hpp"
#include "emojineer/debug_types.hpp"
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

namespace emojineer {

// VM with integrated debugger support - single opcode implementation
class VM {
public:
    VM(std::istream& input, std::ostream& output, std::uint64_t fuel = 1'000'000);
    void execute(const Chunk& chunk);
    void set_debug_control(VMDebugControl* debug) { debug_control_ = debug; }
    VMDebugControl* debug_control() const { return debug_control_; }
    bool is_debug_paused() const { return debug_paused_; }
    void set_debug_paused(bool p) { debug_paused_ = p; }
    std::size_t current_ip() const { return ip_; }
    const Chunk* current_chunk() const { return current_chunk_; }
    std::optional<SourcePosition> get_current_source_position() const;
    std::vector<DebugFrame> get_call_stack() const;
    std::unordered_map<std::string, Value> get_globals() const;
    bool is_execution_finished() const { return execution_finished_; }

private:
    struct CallFrame {
        std::size_t return_ip{0};
        std::size_t stack_base{0};
        std::size_t function_index{0};
        std::vector<Value> locals;
    };

    void initialize_execution(const Chunk& chunk);
    void run_execution_loop();
    Value pop(std::uint32_t line);
    const Value& peek(std::uint32_t line) const;
    bool pop_bool(std::uint32_t line);
    double pop_number(std::uint32_t line);
    std::int64_t pop_int64(std::uint32_t line);
    std::string constant_string(const Chunk& chunk, std::int32_t index, std::uint32_t line) const;
    CallFrame& frame(std::uint32_t line);
    const CallFrame& frame(std::uint32_t line) const;
    [[noreturn]] void runtime_error(std::uint32_t line, const std::string& message) const;

    std::istream& input_;
    std::ostream& output_;
    std::uint64_t fuel_;
    std::uint64_t remaining_fuel_;
    std::vector<Value> stack_;
    std::vector<CallFrame> frames_;
    std::unordered_map<std::string, Value> globals_;
    VMDebugControl* debug_control_{nullptr};
    bool debug_paused_{false};
    bool execution_finished_{false};
    bool initial_execution_{true};
    std::size_t ip_{0};
    const Chunk* current_chunk_{nullptr};
};

} // namespace emojineer
