#pragma once

#include <string>
#include <vector>

namespace emojineer {

struct Grapheme {
    std::string display;
    std::string canonical;
};

std::string canonicalize_token(const std::string& utf8);
std::vector<Grapheme> segment_graphemes(const std::string& utf8);
bool is_emoji_grapheme(const std::string& utf8);
std::string codepoints_hex(const std::string& utf8);

} // namespace emojineer
