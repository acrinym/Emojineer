#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace emojineer {

struct StandardModuleInfo {
    std::string_view specifier;
    std::string_view description;
};

std::optional<std::string_view> standard_module_source(std::string_view specifier);
std::vector<StandardModuleInfo> standard_modules();

} // namespace emojineer
