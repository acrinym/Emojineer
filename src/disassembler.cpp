#include "emojineer/disassembler.hpp"
#include "emojineer/capability.hpp"
#include "emojineer/interop.hpp"
#include "emojineer/intrinsic.hpp"

#include <iomanip>
#include <ostream>

namespace emojineer {

void disassemble(const Chunk& chunk, std::ostream& out) {
    out << "== required capabilities ==\n"
        << capability_mask_string(chunk.required_capabilities) << '\n';

    out << "== EMJBC constants ==\n";
    for (std::size_t i = 0; i < chunk.constants.size(); ++i) {
        out << '[' << i << "] " << value_to_string(chunk.constants[i]) << '\n';
    }

    out << "== functions ==\n";
    if (chunk.functions.empty()) out << "(none)\n";
    for (std::size_t i = 0; i < chunk.functions.size(); ++i) {
        const auto& fn = chunk.functions[i];
        out << '[' << i << "] " << fn.name
            << " entry=" << fn.entry
            << " arity=" << fn.arity
            << " locals=" << fn.local_count << '\n';
    }

    out << "== interop imports ==\n";
    if (chunk.interop_imports.empty()) out << "(none)\n";
    for (std::size_t i = 0; i < chunk.interop_imports.size(); ++i) {
        const auto& import = chunk.interop_imports[i];
        out << '[' << i << "] " << import.internal_name << " -> " << import.external_name << " (";
        for (std::size_t p = 0; p < import.signature.parameters.size(); ++p) { if (p) out << ","; out << interop_type_name(import.signature.parameters[p]); }
        out << ") -> " << interop_type_name(import.signature.result) << " capabilities=" << capability_mask_string(import.required_capabilities) << '\n';
    }
    out << "== interop exports ==\n";
    if (chunk.interop_exports.empty()) out << "(none)\n";
    for (std::size_t i = 0; i < chunk.interop_exports.size(); ++i) {
        const auto& export_info = chunk.interop_exports[i];
        out << '[' << i << "] " << export_info.external_name << " <- " << chunk.functions.at(export_info.function_index).name << " (";
        for (std::size_t p = 0; p < export_info.signature.parameters.size(); ++p) { if (p) out << ","; out << interop_type_name(export_info.signature.parameters[p]); }
        out << ") -> " << interop_type_name(export_info.signature.result) << '\n';
    }

    out << "== instructions ==\n";
    for (std::size_t i = 0; i < chunk.code.size(); ++i) {
        const auto& ins = chunk.code[i];
        out << std::setw(6) << i << "  "
            << std::left << std::setw(16) << opcode_name(ins.op) << std::right
            << " operand=" << ins.operand;
        if (ins.op == OpCode::HostCall) {
            if (const auto facility = native_facility_from_operand(ins.operand)) out << " native=" << native_facility_name(*facility);
        } else if (ins.op == OpCode::InteropCall && ins.operand >= 0 && static_cast<std::size_t>(ins.operand) < chunk.interop_imports.size()) {
            out << " interop=" << chunk.interop_imports[static_cast<std::size_t>(ins.operand)].external_name;
        } else if (ins.op == OpCode::IntrinsicCall) {
            if (const auto intrinsic = intrinsic_from_operand(ins.operand)) out << " intrinsic=" << intrinsic_name(*intrinsic);
        }
        out << " line=" << ins.line << '\n';
    }
}

} // namespace emojineer
