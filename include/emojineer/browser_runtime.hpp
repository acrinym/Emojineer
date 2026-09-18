#pragma once

#include "emojineer/capability.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace emojineer {

struct BrowserRunResult {
    bool ok{false};
    std::string stdout_text;
    std::string diagnostic;
    CapabilityMask required_capabilities{0};
};

/// Compile and execute one in-memory Emojineer source unit through the production VM.
/// Browser execution is permanently sandboxed: no native host capabilities are granted.
BrowserRunResult run_browser_source(std::string_view source,
                                    const std::vector<std::string>& program_arguments = {});

} // namespace emojineer
