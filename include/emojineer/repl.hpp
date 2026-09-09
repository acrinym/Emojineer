#pragma once

#include "emojineer/cer.hpp"
#include "emojineer/capability.hpp"

#include <iosfwd>

namespace emojineer {

/// Run the buffered interactive REPL over the sovereign compiler and production VM.
///
/// The supplied ExecutionPolicy is reused for every `:run` execution, so the REPL
/// cannot become an alternate authority path around normal VM capability preflight.
/// The input stream is intentionally shared with executed programs for `📥`.
int run_repl(std::istream& input, std::ostream& output, std::ostream& errors,
             CustomEmojiRegistry registry = {}, ExecutionPolicy policy = {});

} // namespace emojineer
