#pragma once

#include <string_view>

#ifndef EMOJINEER_VERSION
#define EMOJINEER_VERSION "0.23.0"
#endif

namespace emojineer {

inline constexpr std::string_view version = EMOJINEER_VERSION;

} // namespace emojineer
