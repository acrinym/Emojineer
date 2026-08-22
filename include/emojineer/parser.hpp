#pragma once
#include "emojineer/ast.hpp"
#include "emojineer/token.hpp"
#include <vector>
namespace emojineer {
class Parser{public:explicit Parser(std::vector<Token> tokens);ast::Program parse();private:
ast::StmtPtr statement();ast::StmtPtr module_declaration();ast::StmtPtr import_statement();ast::StmtPtr export_statement();ast::StmtPtr var_declaration();ast::StmtPtr assignment();ast::StmtPtr print_statement();ast::StmtPtr return_statement();ast::StmtPtr function_declaration();ast::StmtPtr if_statement();ast::StmtPtr while_statement();std::vector<ast::StmtPtr> block_until(TokenKind first,TokenKind second=TokenKind::Eof);
ast::ExprPtr expression();ast::ExprPtr equality();ast::ExprPtr comparison();ast::ExprPtr term();ast::ExprPtr factor();ast::ExprPtr unary();ast::ExprPtr primary();
bool match(TokenKind kind);bool check(TokenKind kind)const;const Token& advance();const Token& previous()const;const Token& peek()const;const Token& consume(TokenKind kind,const std::string& message);void consume_line_end();void skip_newlines();[[noreturn]]void error(const Token& token,const std::string& message)const;
std::vector<Token> tokens_;std::size_t current_{0};};
}
