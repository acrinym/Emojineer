#pragma once

#include "emojineer/cer.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace emojineer {

struct StyleDiagnostic {
    std::size_t line{0};
    std::string message;
};

std::string format_source(const std::string& source, CustomEmojiRegistry registry = {});
std::vector<StyleDiagnostic> diagnose_source_style(const std::string& source,
                                                   CustomEmojiRegistry registry = {});

} // namespace emojineer
