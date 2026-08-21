#pragma once

#include "emojineer/token.hpp"

#include <string>
#include <vector>

namespace emojineer {

class Lexer {
public:
    explicit Lexer(std::string source);
    std::vector<Token> tokenize() const;
    std::string explain() const;

private:
    std::string source_;
    TokenRegistry registry_;
};

} // namespace emojineer
