#pragma once

#include "emojineer/bytecode.hpp"

#include <iosfwd>

namespace emojineer {

void disassemble(const Chunk& chunk, std::ostream& out);

} // namespace emojineer
