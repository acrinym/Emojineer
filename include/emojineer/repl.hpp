#pragma once

#include "emojineer/cer.hpp"
#include "emojineer/capability.hpp"

#include <iosfwd>

namespace emojineer {

int run_repl(std::istream& input, std::ostream& output, std::ostream& errors,
             CustomEmojiRegistry registry = {}, ExecutionPolicy policy = {});

} // namespace emojineer
