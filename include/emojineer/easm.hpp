#pragma once
#include "emojineer/capability.hpp"
#include "emojineer/interop.hpp"
#include <bit>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace emojineer {

enum class EasmScalarType : std::uint8_t { I64, F64, Bool };
enum class EasmBufferType : std::uint8_t { U8, I64, F64 };
enum class EasmOp : std::uint8_t {
    ConstI64, ConstF64, ConstBool, Move,
    AddI64, SubI64, MulI64, AddF64, SubF64, MulF64, DivF64,
    EqI64, LtI64, EqF64, LtF64,
    LoadU8, StoreU8, LoadI64, StoreI64, LoadF64, StoreF64,
    CallImport, Jump, JumpIfFalse, Return
};

using EasmScalar = std::variant<std::int64_t, double, bool>;

struct EasmBufferDecl {
    std::string name;
    EasmBufferType type{EasmBufferType::U8};
    std::uint32_t elements{0};
    bool operator==(const EasmBufferDecl&) const = default;
};

struct EasmImport {
    std::string internal_name;
    std::string external_name;
    std::vector<EasmScalarType> parameters;
    EasmScalarType result{EasmScalarType::Bool};
    CapabilityMask required_capabilities{0};
    bool operator==(const EasmImport&) const = default;
};
struct EasmInstruction {
    EasmOp op{EasmOp::Return};
    std::int32_t dst{-1};
    std::int32_t a{-1};
    std::int32_t b{-1};
    std::uint32_t operand{0};
    std::int64_t imm_i64{0};
    double imm_f64{0.0};
    std::vector<std::int32_t> args;
    bool operator==(const EasmInstruction& other) const {
        return op == other.op && dst == other.dst && a == other.a && b == other.b &&
               operand == other.operand && imm_i64 == other.imm_i64 &&
               std::bit_cast<std::uint64_t>(imm_f64) == std::bit_cast<std::uint64_t>(other.imm_f64) &&
               args == other.args;
    }
};

struct EasmFunction {
    std::string name;
    std::vector<EasmScalarType> parameters;
    EasmScalarType result{EasmScalarType::Bool};
    std::vector<EasmScalarType> registers;
    std::vector<EasmInstruction> code;
    bool operator==(const EasmFunction&) const = default;
};

struct EasmExport {
    std::string external_name;
    std::uint32_t function_index{0};
    bool operator==(const EasmExport&) const = default;
};

struct EasmProgram {
    std::vector<EasmBufferDecl> buffers;
    std::vector<EasmImport> imports;
    std::vector<EasmFunction> functions;
    std::vector<EasmExport> exports;
    CapabilityMask required_capabilities{0};
    bool operator==(const EasmProgram&) const = default;
};

std::string easm_scalar_type_name(EasmScalarType type);
std::string easm_buffer_type_name(EasmBufferType type);
std::string easm_opcode_name(EasmOp op);
InteropSignature easm_interop_signature(const std::vector<EasmScalarType>& parameters,
                                        EasmScalarType result);
EasmProgram parse_easm(std::string_view source);
void verify_easm_program(const EasmProgram& program);
std::string render_easm(const EasmProgram& program);
CapabilityMask easm_export_capabilities(const EasmProgram& program,
                                        std::string_view external_name);
class EasmVM {
public:
    EasmVM(ExecutionPolicy policy = {}, const InteropRegistry* interop = nullptr,
           std::uint64_t fuel = 1'000'000);

    EasmScalar invoke_export(const EasmProgram& program, std::string_view external_name,
                             const std::vector<EasmScalar>& arguments);
    InteropBytes invoke_export_abi(const EasmProgram& program, std::string_view external_name,
                                   std::span<const std::uint8_t> request);
    bool export_is_deterministic(const EasmProgram& program, std::string_view external_name) const;
    void reset_memory(const EasmProgram& program);

private:
    using BufferStorage = std::variant<std::vector<std::uint8_t>,
                                       std::vector<std::int64_t>,
                                       std::vector<double>>;
    void ensure_memory(const EasmProgram& program);
    void preflight_function(const EasmProgram& program, const EasmFunction& function) const;
    EasmScalar run_function(const EasmProgram& program, const EasmFunction& function,
                            const std::vector<EasmScalar>& arguments);

    ExecutionPolicy policy_;
    const InteropRegistry* interop_registry_{nullptr};
    std::uint64_t fuel_{1'000'000};
    std::optional<EasmProgram> memory_program_;
    std::vector<BufferStorage> memory_;
};

void bind_easm_export(InteropRegistry& registry, std::string binding_external_name,
                      std::shared_ptr<EasmVM> vm, EasmProgram program,
                      std::string export_external_name);

} // namespace emojineer
