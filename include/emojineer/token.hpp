#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>
namespace emojineer {
enum class TokenKind { Eof,Newline,Number,String,Identifier, Var,Assign,Print,If,Else,While,End,Input,True,False,TypeNumber,TypeString,TypeBool, Function,Return, Add,Subtract,Multiply,Divide,Modulo,Equal,Less,Greater,Not, GroupStart,GroupEnd };
struct Token {TokenKind kind{TokenKind::Eof};std::string lexeme;std::string canonical;std::string literal;std::size_t line{1};std::size_t column{1};};
struct TokenDefinition {TokenKind kind;std::string description;};
class TokenRegistry {public:TokenRegistry();const TokenDefinition* find(const std::string& canonical)const;std::string describe(TokenKind kind)const;private:std::unordered_map<std::string,TokenDefinition> definitions_;};
std::string token_kind_name(TokenKind kind);
}
