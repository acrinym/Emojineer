#pragma once
#include "emojineer/bytecode.hpp"
#include "emojineer/capability.hpp"
#include "emojineer/debug_types.hpp"
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

namespace emojineer {

/// Production Emojineer virtual machine and the single opcode implementation.
///
/// Source execution, serialized bytecode, REPL, and debugger execution all use
/// this VM. The ExecutionPolicy is validated at construction and a chunk's full
/// verifier-bound capability mask is preflighted before instruction zero.
class VM {
public:
    /// Bind host I/O, instruction fuel, and explicit runtime authority.
    ///
    /// The default policy grants no Train 20 host capabilities.
    VM(std::istream& input, std::ostream& output, std::uint64_t fuel = 1'000'000,
       ExecutionPolicy policy = {});

    /// Start a new chunk or resume a paused execution of the same chunk.
    ///
    /// New chunks are verified and capability-preflighted before instruction zero.
    /// Resuming the same paused chunk continues its already-preflighted VM state.
    void execute(const Chunk& chunk);

    /// Attach the debugger control hook used by the production execution loop.
    void set_debug_control(VMDebugControl* debug) { debug_control_ = debug; }

    /// Return the currently attached debugger hook, or nullptr when undebugged.
    VMDebugControl* debug_control() const { return debug_control_; }

    /// Report whether debugger control has paused instruction execution.
    bool is_debug_paused() const { return debug_paused_; }

    /// Update debugger pause state without changing program values.
    void set_debug_paused(bool p) { debug_paused_ = p; }

    /// Return the current instruction pointer for debugger/tooling inspection.
    std::size_t current_ip() const { return ip_; }

    /// Return the chunk associated with the current or most recent execution.
    ///
    /// The pointer remains available after normal completion for debugger/tooling
    /// inspection and is nullptr only before a chunk has been initialized.
    const Chunk* current_chunk() const { return current_chunk_; }

    /// Resolve the current instruction to deterministic source provenance.
    std::optional<SourcePosition> get_current_source_position() const;

    /// Snapshot call frames for read-only debugger inspection.
    std::vector<DebugFrame> get_call_stack() const;

    /// Snapshot global values without exposing mutable VM storage.
    std::unordered_map<std::string, Value> get_globals() const;

    /// Report whether the current execution has reached terminal state.
    bool is_execution_finished() const { return execution_finished_; }

private:
    struct CallFrame {
        std::size_t return_ip{0};
        std::size_t stack_base{0};
        std::size_t function_index{0};
        std::vector<Value> locals;
    };

    /// Reset execution state only after bytecode verification and authority preflight.
    void initialize_execution(const Chunk& chunk);

    /// Execute the production opcode loop until halt, pause, or runtime failure.
    void run_execution_loop();

    /// Pop one operand or raise a source-line runtime error on stack underflow.
    Value pop(std::uint32_t line);

    /// Inspect the top operand without consuming it.
    const Value& peek(std::uint32_t line) const;

    /// Pop and type-check a strict boolean operand.
    bool pop_bool(std::uint32_t line);

    /// Pop and normalize an Emojineer numeric operand.
    double pop_number(std::uint32_t line);

    /// Pop a checked VM support integer operand.
    std::int64_t pop_int64(std::uint32_t line);

    /// Read a string constant after bounds and variant validation.
    std::string constant_string(const Chunk& chunk, std::int32_t index, std::uint32_t line) const;

    /// Execute one already-preflighted HostCall with defense-in-depth grant checks.
    void execute_host_call(std::int32_t operand, std::uint32_t line);

    /// Advance the deterministic VM-local PRNG state used only in deterministic mode.
    std::uint64_t next_deterministic_random();

    /// Return the active call frame or raise a runtime error when none exists.
    CallFrame& frame(std::uint32_t line);
    const CallFrame& frame(std::uint32_t line) const;

    /// Throw a source-line-aware runtime failure; this function never returns.
    [[noreturn]] void runtime_error(std::uint32_t line, const std::string& message) const;

    std::istream& input_;
    std::ostream& output_;
    std::uint64_t fuel_;
    std::uint64_t remaining_fuel_;
    ExecutionPolicy policy_;
    std::uint64_t deterministic_random_state_{0};
    std::int64_t deterministic_clock_ms_{0};
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
