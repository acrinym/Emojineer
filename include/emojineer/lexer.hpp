#pragma once
#include "emojineer/cer.hpp"
#include "emojineer/token.hpp"
#include <string>
#include <vector>
namespace emojineer {
class Lexer {
public:
    explicit Lexer(std::string source);
    Lexer(std::string source, CustomEmojiRegistry registry);
    std::vector<Token> tokenize() const;
    std::string explain() const;
private:
    std::string source_;
    CustomEmojiRegistry registry_;
};
}
