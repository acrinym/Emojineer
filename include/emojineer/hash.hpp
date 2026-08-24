#pragma once

#include <string>
#include <string_view>

namespace emojineer {

std::string sha256_hex(std::string_view data);

} // namespace emojineer
