#include "emojineer/parser.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace emojineer {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

ast::Program Parser::parse() {
    ast::Program program; skip_newlines();
    while (!check(TokenKind::Eof)) {
        if (check(TokenKind::Else) || check(TokenKind::End)) error(peek(), "unexpected block marker");
        program.statements.push_back(statement()); skip_newlines();
    }
    return program;
}

ast::StmtPtr Parser::statement() {
    if (check(TokenKind::Var)) return var_declaration();
    if (check(TokenKind::Assign)) return assignment();
    if (check(TokenKind::Print)) return print_statement();
    if (check(TokenKind::If)) return if_statement();
    if (check(TokenKind::While)) return while_statement();
    error(peek(), "expected a statement (🐍, ✏️, 📝, 🤔, or 🔁)");
}

ast::StmtPtr Parser::var_declaration() {
    const Token start = advance();
    const Token& name = consume(TokenKind::Identifier, "expected an emoji identifier after 🐍");
    std::optional<ast::DeclaredType> type;
    if (match(TokenKind::TypeNumber)) type = ast::DeclaredType::Number;
    else if (match(TokenKind::TypeString)) type = ast::DeclaredType::String;
    else if (match(TokenKind::TypeBool)) type = ast::DeclaredType::Bool;
    consume(TokenKind::Equal, "expected 🟰 in variable declaration");
    auto initializer = expression(); consume_line_end();
    auto stmt = std::make_unique<ast::VarDecl>(); stmt->line = start.line; stmt->name = name.canonical;
    stmt->declared_type = type; stmt->initializer = std::move(initializer); return stmt;
}

ast::StmtPtr Parser::assignment() {
    const Token start = advance(); const Token& name = consume(TokenKind::Identifier, "expected an emoji identifier after ✏️");
    consume(TokenKind::Equal, "expected 🟰 in assignment"); auto value = expression(); consume_line_end();
    auto stmt = std::make_unique<ast::Assignment>(); stmt->line = start.line; stmt->name = name.canonical; stmt->value = std::move(value); return stmt;
}

ast::StmtPtr Parser::print_statement() {
    const Token start = advance(); auto expr = expression(); consume_line_end();
    auto stmt = std::make_unique<ast::PrintStmt>(); stmt->line = start.line; stmt->expression = std::move(expr); return stmt;
}

ast::StmtPtr Parser::if_statement() {
    const Token start = advance(); auto condition = expression(); consume_line_end(); skip_newlines();
    auto then_branch = block_until(TokenKind::Else, TokenKind::End); std::vector<ast::StmtPtr> else_branch;
    if (match(TokenKind::Else)) { consume_line_end(); skip_newlines(); else_branch = block_until(TokenKind::End); }
    consume(TokenKind::End, "expected 🏁 to close 🤔 block"); consume_line_end();
    auto stmt = std::make_unique<ast::IfStmt>(); stmt->line = start.line; stmt->condition = std::move(condition);
    stmt->then_branch = std::move(then_branch); stmt->else_branch = std::move(else_branch); return stmt;
}

ast::StmtPtr Parser::while_statement() {
    const Token start = advance(); auto condition = expression(); consume_line_end(); skip_newlines();
    auto body = block_until(TokenKind::End); consume(TokenKind::End, "expected 🏁 to close 🔁 block"); consume_line_end();
    auto stmt = std::make_unique<ast::WhileStmt>(); stmt->line = start.line; stmt->condition = std::move(condition); stmt->body = std::move(body); return stmt;
}

std::vector<ast::StmtPtr> Parser::block_until(TokenKind first, TokenKind second) {
    std::vector<ast::StmtPtr> block; skip_newlines();
    while (!check(TokenKind::Eof) && !check(first) && !check(second)) { block.push_back(statement()); skip_newlines(); }
    if (check(TokenKind::Eof)) error(peek(), "unterminated block; expected 🏁");
    return block;
}

ast::ExprPtr Parser::expression() { return equality(); }
ast::ExprPtr Parser::equality() {
    auto expr = comparison();
    while (match(TokenKind::Equal)) { const Token op = previous(); auto right = comparison(); auto binary = std::make_unique<ast::BinaryExpr>(); binary->line = op.line; binary->left = std::move(expr); binary->op = op.kind; binary->right = std::move(right); expr = std::move(binary); }
    return expr;
}
ast::ExprPtr Parser::comparison() {
    auto expr = term();
    while (check(TokenKind::Less) || check(TokenKind::Greater)) { const Token op = advance(); auto right = term(); auto binary = std::make_unique<ast::BinaryExpr>(); binary->line = op.line; binary->left = std::move(expr); binary->op = op.kind; binary->right = std::move(right); expr = std::move(binary); }
    return expr;
}
ast::ExprPtr Parser::term() {
    auto expr = factor();
    while (check(TokenKind::Add) || check(TokenKind::Subtract)) { const Token op = advance(); auto right = factor(); auto binary = std::make_unique<ast::BinaryExpr>(); binary->line = op.line; binary->left = std::move(expr); binary->op = op.kind; binary->right = std::move(right); expr = std::move(binary); }
    return expr;
}
ast::ExprPtr Parser::factor() {
    auto expr = unary();
    while (check(TokenKind::Multiply) || check(TokenKind::Divide) || check(TokenKind::Modulo)) { const Token op = advance(); auto right = unary(); auto binary = std::make_unique<ast::BinaryExpr>(); binary->line = op.line; binary->left = std::move(expr); binary->op = op.kind; binary->right = std::move(right); expr = std::move(binary); }
    return expr;
}
ast::ExprPtr Parser::unary() {
    if (match(TokenKind::Subtract) || match(TokenKind::Not)) { const Token op = previous(); auto expr = std::make_unique<ast::UnaryExpr>(); expr->line = op.line; expr->op = op.kind; expr->right = unary(); return expr; }
    return primary();
}
ast::ExprPtr Parser::primary() {
    if (match(TokenKind::Number)) { auto expr = std::make_unique<ast::LiteralExpr>(); expr->line = previous().line; expr->value = std::stod(previous().literal); return expr; }
    if (match(TokenKind::String)) { auto expr = std::make_unique<ast::LiteralExpr>(); expr->line = previous().line; expr->value = previous().literal; return expr; }
    if (match(TokenKind::True) || match(TokenKind::False)) { auto expr = std::make_unique<ast::LiteralExpr>(); expr->line = previous().line; expr->value = previous().kind == TokenKind::True; return expr; }
    if (match(TokenKind::Identifier)) { auto expr = std::make_unique<ast::VariableExpr>(); expr->line = previous().line; expr->name = previous().canonical; return expr; }
    if (match(TokenKind::Input)) { auto expr = std::make_unique<ast::InputExpr>(); expr->line = previous().line; return expr; }
    if (match(TokenKind::GroupStart)) { auto expr = expression(); consume(TokenKind::GroupEnd, "expected 🤲 after grouped expression"); return expr; }
    error(peek(), "expected an expression");
}

// Helper to get a reference to a sentinel EOF token
static const Token& eof_token() {
    static Token eof{TokenKind::Eof, "", "", 0, 0};
    return eof;
}

bool Parser::match(TokenKind kind) { if (!check(kind)) return false; advance(); return true; }
bool Parser::check(TokenKind kind) const { return peek().kind == kind; }
const Token& Parser::advance() {
    if (!check(TokenKind::Eof)) {
        if (current_ < tokens_.size()) ++current_;
    }
    return previous();
}
const Token& Parser::previous() const {
    if (current_ == 0) return eof_token();
    if (current_ > tokens_.size()) return eof_token();
    return tokens_.at(current_ - 1);
}
const Token& Parser::peek() const {
    if (current_ >= tokens_.size()) return eof_token();
    return tokens_.at(current_);
}
const Token& Parser::consume(TokenKind kind, const std::string& message) { if (check(kind)) return advance(); error(peek(), message); }
void Parser::consume_line_end() { if (match(TokenKind::Newline) || check(TokenKind::Eof)) return; error(peek(), "expected end of line"); }
void Parser::skip_newlines() { while (match(TokenKind::Newline)) {} }
[[noreturn]] void Parser::error(const Token& token, const std::string& message) const {
    std::ostringstream out; out << "line " << token.line << ", column " << token.column << ": " << message;
    if (!token.lexeme.empty()) out << " near '" << token.lexeme << "'"; throw std::runtime_error(out.str());
}

} // namespace emojineer
