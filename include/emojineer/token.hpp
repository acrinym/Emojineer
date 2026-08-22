#pragma once

#include <cstddef>
#include <string>

namespace emojineer {

enum class TokenKind {
    Eof, Newline, Number, String, Identifier,
    Var, Assign, Print, If, Else, While, End, Input, True, False,
    Module, Import, Export,
    TypeNumber, TypeString, TypeBool, Function, Return,
    Add, Subtract, Multiply, Divide, Modulo, Equal, Less, Greater, Not,
    GroupStart, GroupEnd, Array, Index, Length, Append, SetIndex
};

struct Token {
    TokenKind kind{TokenKind::Eof};
    std::string lexeme;
    std::string canonical;
    std::string literal;
    std::size_t line{1};
    std::size_t column{1};
};

std::string token_kind_name(TokenKind kind);

} // namespace emojineer
