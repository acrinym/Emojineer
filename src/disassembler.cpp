#include "emojineer/disassembler.hpp"
#include "emojineer/capability.hpp"

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

    out << "== instructions ==\n";
    for (std::size_t i = 0; i < chunk.code.size(); ++i) {
        const auto& ins = chunk.code[i];
        out << std::setw(6) << i << "  "
            << std::left << std::setw(16) << opcode_name(ins.op) << std::right
            << " operand=" << ins.operand;
        if (ins.op == OpCode::HostCall) {
            if (const auto facility = native_facility_from_operand(ins.operand))
                out << " native=" << native_facility_name(*facility);
        }
        out << " line=" << ins.line << '\n';
    }
}

} // namespace emojineer
