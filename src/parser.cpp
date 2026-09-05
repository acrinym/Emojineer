#include "emojineer/parser.hpp"
#include "emojineer/source_diagnostic.hpp"
#include "emojineer/unicode.hpp"
#include <sstream>
#include <stdexcept>
#include <utility>

namespace emojineer {

static std::size_t token_grapheme_width(const Token& t) {
    return segment_graphemes(t.lexeme).size();
}

static void set_source_range(ast::SourceRange& dest, const Token& start, const Token& end) {
    dest.line = start.line;
    dest.column = start.column;
    dest.end_line = end.line;
    dest.end_column = end.column + token_grapheme_width(end);
}

Parser::Parser(std::vector<Token> tokens):tokens_(std::move(tokens)){}

ast::Program Parser::parse(){
    ast::Program program;
    skip_newlines();
    while(!check(TokenKind::Eof)){
        if(check(TokenKind::Else)||check(TokenKind::End))error(peek(),"unexpected block marker");
        program.statements.push_back(statement());
        skip_newlines();
    }
    return program;
}

ast::StmtPtr Parser::statement(){
    if(check(TokenKind::Module))return module_declaration();
    if(check(TokenKind::Import))return import_statement();
    if(check(TokenKind::Export))return export_statement();
    if(check(TokenKind::Var))return var_declaration();
    if(check(TokenKind::Assign))return assignment();
    if(check(TokenKind::Print))return print_statement();
    if(check(TokenKind::Return))return return_statement();
    if(check(TokenKind::Function))return function_declaration();
    if(check(TokenKind::If))return if_statement();
    if(check(TokenKind::While))return while_statement();
    error(peek(),"expected a statement (🧩, 🔗, 📤, 🐍, ✏️, 📝, 📦, 🛠️, 🤔, or 🔁)");
}

ast::StmtPtr Parser::module_declaration(){
    const Token start=advance();
    const Token name=consume(TokenKind::Identifier,"expected an emoji module name after 🧩");
    consume_line_end();
    auto s=std::make_unique<ast::ModuleDecl>();
    s->line=start.line;
    set_source_range(s->source, start, name);
    s->name=name.canonical;
    return s;
}

ast::StmtPtr Parser::import_statement(){
    const Token start=advance();
    const Token path=consume(TokenKind::String,"expected a 📜path.emoji📜 string after 🔗");
    consume_line_end();
    auto s=std::make_unique<ast::ImportStmt>();
    s->line=start.line;
    set_source_range(s->source, start, path);
    s->path=path.literal;
    return s;
}

ast::StmtPtr Parser::export_statement(){
    const Token start=advance();
    const Token name=consume(TokenKind::Identifier,"expected an emoji symbol after 📤");
    consume_line_end();
    auto s=std::make_unique<ast::ExportStmt>();
    s->line=start.line;
    set_source_range(s->source, start, name);
    s->name=name.canonical;
    return s;
}

ast::StmtPtr Parser::var_declaration(){
    const Token start=advance();
    const Token name=consume(TokenKind::Identifier,"expected an emoji identifier after 🐍");
    std::optional<ast::DeclaredType> type;
    if(match(TokenKind::TypeNumber))type=ast::DeclaredType::Number;
    else if(match(TokenKind::TypeString))type=ast::DeclaredType::String;
    else if(match(TokenKind::TypeBool))type=ast::DeclaredType::Bool;
    else if(match(TokenKind::Array))type=ast::DeclaredType::Array;
    consume(TokenKind::Equal,"expected 🟰 in variable declaration");
    auto init=expression();
    const Token end_token = previous();
    consume_line_end();
    auto s=std::make_unique<ast::VarDecl>();
    s->line=start.line;
    set_source_range(s->source, start, end_token);
    s->name=name.canonical;
    s->declared_type=type;
    s->initializer=std::move(init);
    return s;
}

ast::StmtPtr Parser::assignment(){
    const Token start=advance();
    const Token name=consume(TokenKind::Identifier,"expected an emoji identifier after ✏️");
    consume(TokenKind::Equal,"expected 🟰 in assignment");
    auto value=expression();
    const Token end_token = previous();
    consume_line_end();
    auto s=std::make_unique<ast::Assignment>();
    s->line=start.line;
    set_source_range(s->source, start, end_token);
    s->name=name.canonical;
    s->value=std::move(value);
    return s;
}

ast::StmtPtr Parser::print_statement(){
    const Token start=advance();
    auto expr=expression();
    const Token end_token = previous();
    consume_line_end();
    auto s=std::make_unique<ast::PrintStmt>();
    s->line=start.line;
    set_source_range(s->source, start, end_token);
    s->expression=std::move(expr);
    return s;
}

ast::StmtPtr Parser::return_statement(){
    const Token start=advance();
    auto expr=expression();
    const Token end_token = previous();
    consume_line_end();
    auto s=std::make_unique<ast::ReturnStmt>();
    s->line=start.line;
    set_source_range(s->source, start, end_token);
    s->expression=std::move(expr);
    return s;
}

ast::StmtPtr Parser::function_declaration(){
    const Token start=advance();
    const Token name=consume(TokenKind::Identifier,"expected function emoji after 🛠️");
    consume(TokenKind::GroupStart,"expected 🫴 before function parameters");
    std::vector<std::string> params;
    while(!check(TokenKind::GroupEnd)){
        if(check(TokenKind::Eof)||check(TokenKind::Newline))error(peek(),"expected 🤲 after function parameters");
        const Token p=consume(TokenKind::Identifier,"function parameters must be emoji identifiers");
        params.push_back(p.canonical);
    }
    const Token params_end = previous();
    (void)params_end;
    consume(TokenKind::GroupEnd,"expected 🤲 after function parameters");
    consume_line_end();
    skip_newlines();
    auto body=block_until(TokenKind::End);
    const Token end_marker = peek();
    consume(TokenKind::End,"expected 🏁 to close 🛠️ function");
    consume_line_end();
    auto s=std::make_unique<ast::FunctionDecl>();
    s->line=start.line;
    set_source_range(s->source, start, end_marker);
    s->name=name.canonical;
    s->parameters=std::move(params);
    s->body=std::move(body);
    return s;
}

ast::StmtPtr Parser::if_statement(){
    const Token start=advance();
    auto condition=expression();
    const Token cond_end = previous();
    (void)cond_end;
    consume_line_end();
    skip_newlines();
    auto then_branch=block_until(TokenKind::Else,TokenKind::End);
    std::vector<ast::StmtPtr> else_branch;
    Token else_token;
    if(match(TokenKind::Else)){
        else_token = previous();
        consume_line_end();
        skip_newlines();
        else_branch=block_until(TokenKind::End);
    }
    const Token end_marker = peek();
    consume(TokenKind::End,"expected 🏁 to close 🤔 block");
    consume_line_end();
    auto s=std::make_unique<ast::IfStmt>();
    s->line=start.line;
    if(else_token.kind != TokenKind::Eof){
        set_source_range(s->source, start, else_token);
    } else {
        set_source_range(s->source, start, end_marker);
    }
    s->condition=std::move(condition);
    s->then_branch=std::move(then_branch);
    s->else_branch=std::move(else_branch);
    return s;
}

ast::StmtPtr Parser::while_statement(){
    const Token start=advance();
    auto condition=expression();
    const Token cond_end = previous();
    (void)cond_end;
    consume_line_end();
    skip_newlines();
    auto body=block_until(TokenKind::End);
    const Token end_marker = peek();
    consume(TokenKind::End,"expected 🏁 to close 🔁 block");
    consume_line_end();
    auto s=std::make_unique<ast::WhileStmt>();
    s->line=start.line;
    set_source_range(s->source, start, end_marker);
    s->condition=std::move(condition);
    s->body=std::move(body);
    return s;
}

std::vector<ast::StmtPtr> Parser::block_until(TokenKind first,TokenKind second){
    std::vector<ast::StmtPtr> block;
    skip_newlines();
    while(!check(TokenKind::Eof)&&!check(first)&&!check(second)){
        block.push_back(statement());
        skip_newlines();
    }
    if(check(TokenKind::Eof))error(peek(),"unterminated block; expected 🏁");
    return block;
}

ast::ExprPtr Parser::expression(){return equality();}

ast::ExprPtr Parser::equality(){
    auto expr=comparison();
    while(match(TokenKind::Equal)){
        const Token op=previous();
        auto r=comparison();
        auto b=std::make_unique<ast::BinaryExpr>();
        b->line=op.line;
        b->source.line = expr->source.line;
        b->source.column = expr->source.column;
        b->source.end_line = r->source.end_line;
        b->source.end_column = r->source.end_column;
        b->left=std::move(expr);
        b->op=op.kind;
        b->right=std::move(r);
        expr=std::move(b);
    }
    return expr;
}

ast::ExprPtr Parser::comparison(){
    auto expr=term();
    while(check(TokenKind::Less)||check(TokenKind::Greater)){
        const Token op=advance();
        auto r=term();
        auto b=std::make_unique<ast::BinaryExpr>();
        b->line=op.line;
        b->source.line = expr->source.line;
        b->source.column = expr->source.column;
        b->source.end_line = r->source.end_line;
        b->source.end_column = r->source.end_column;
        b->left=std::move(expr);
        b->op=op.kind;
        b->right=std::move(r);
        expr=std::move(b);
    }
    return expr;
}

ast::ExprPtr Parser::term(){
    auto expr=factor();
    while(check(TokenKind::Add)||check(TokenKind::Subtract)){
        const Token op=advance();
        auto r=factor();
        auto b=std::make_unique<ast::BinaryExpr>();
        b->line=op.line;
        b->source.line = expr->source.line;
        b->source.column = expr->source.column;
        b->source.end_line = r->source.end_line;
        b->source.end_column = r->source.end_column;
        b->left=std::move(expr);
        b->op=op.kind;
        b->right=std::move(r);
        expr=std::move(b);
    }
    return expr;
}

ast::ExprPtr Parser::factor(){
    auto expr=unary();
    while(check(TokenKind::Multiply)||check(TokenKind::Divide)||check(TokenKind::Modulo)){
        const Token op=advance();
        auto r=unary();
        auto b=std::make_unique<ast::BinaryExpr>();
        b->line=op.line;
        b->source.line = expr->source.line;
        b->source.column = expr->source.column;
        b->source.end_line = r->source.end_line;
        b->source.end_column = r->source.end_column;
        b->left=std::move(expr);
        b->op=op.kind;
        b->right=std::move(r);
        expr=std::move(b);
    }
    return expr;
}

ast::ExprPtr Parser::unary(){
    if(match(TokenKind::Subtract)||match(TokenKind::Not)){
        const Token op=previous();
        auto e=std::make_unique<ast::UnaryExpr>();
        e->line=op.line;
        e->source.line = op.line;
        e->source.column = op.column;
        e->op=op.kind;
        e->right=unary();
        e->source.end_line = e->right->source.end_line;
        e->source.end_column = e->right->source.end_column;
        return e;
    }
    if(match(TokenKind::Length)){
        const Token op=previous();
        auto e=std::make_unique<ast::LengthExpr>();
        e->line=op.line;
        e->source.line = op.line;
        e->source.column = op.column;
        e->value=unary();
        e->source.end_line = e->value->source.end_line;
        e->source.end_column = e->value->source.end_column;
        return e;
    }
    return primary();
}

ast::ExprPtr Parser::primary(){
    if(match(TokenKind::Number)){
        auto e=std::make_unique<ast::LiteralExpr>();
        const Token t = previous();
        e->line=t.line;
        e->source.line = t.line;
        e->source.column = t.column;
        e->source.end_line = t.line;
        e->source.end_column = t.column + token_grapheme_width(t);
        e->value=std::stod(t.literal);
        return e;
    }
    if(match(TokenKind::String)){
        auto e=std::make_unique<ast::LiteralExpr>();
        const Token t = previous();
        e->line=t.line;
        e->source.line = t.line;
        e->source.column = t.column;
        e->source.end_line = t.line;
        e->source.end_column = t.column + token_grapheme_width(t);
        e->value=t.literal;
        return e;
    }
    if(match(TokenKind::True)||match(TokenKind::False)){
        auto e=std::make_unique<ast::LiteralExpr>();
        const Token t = previous();
        e->line=t.line;
        e->source.line = t.line;
        e->source.column = t.column;
        e->source.end_line = t.line;
        e->source.end_column = t.column + token_grapheme_width(t);
        e->value=t.kind==TokenKind::True;
        return e;
    }
    if(match(TokenKind::Array)){
        const Token op=previous();
        consume(TokenKind::GroupStart,"expected 🫴 after 📚 array literal");
        auto e=std::make_unique<ast::ArrayExpr>();
        e->line=op.line;
        e->source.line = op.line;
        e->source.column = op.column;
        while(!check(TokenKind::GroupEnd)){
            if(check(TokenKind::Eof)||check(TokenKind::Newline))error(peek(),"expected 🤲 after array elements");
            e->elements.push_back(expression());
        }
        consume(TokenKind::GroupEnd,"expected 🤲 after array elements");
        const Token close_bracket = previous();
        e->source.end_line = close_bracket.line;
        e->source.end_column = close_bracket.column + token_grapheme_width(close_bracket);
        return e;
    }
    if(match(TokenKind::Index)){
        const Token op=previous();
        consume(TokenKind::GroupStart,"expected 🫴 after 🔎");
        auto e=std::make_unique<ast::IndexExpr>();
        e->line=op.line;
        e->source.line = op.line;
        e->source.column = op.column;
        e->collection=expression();
        e->index=expression();
        consume(TokenKind::GroupEnd,"expected 🤲 after 🔎 array and index");
        const Token close_bracket = previous();
        e->source.end_line = close_bracket.line;
        e->source.end_column = close_bracket.column + token_grapheme_width(close_bracket);
        return e;
    }
    if(match(TokenKind::Append)){
        const Token op=previous();
        consume(TokenKind::GroupStart,"expected 🫴 after 📎");
        auto e=std::make_unique<ast::AppendExpr>();
        e->line=op.line;
        e->source.line = op.line;
        e->source.column = op.column;
        e->collection=expression();
        e->value=expression();
        consume(TokenKind::GroupEnd,"expected 🤲 after 📎 array and value");
        const Token close_bracket = previous();
        e->source.end_line = close_bracket.line;
        e->source.end_column = close_bracket.column + token_grapheme_width(close_bracket);
        return e;
    }
    if(match(TokenKind::SetIndex)){
        const Token op=previous();
        consume(TokenKind::GroupStart,"expected 🫴 after 🧷");
        auto e=std::make_unique<ast::SetIndexExpr>();
        e->line=op.line;
        e->source.line = op.line;
        e->source.column = op.column;
        e->collection=expression();
        e->index=expression();
        e->value=expression();
        consume(TokenKind::GroupEnd,"expected 🤲 after 🧷 array, index, and value");
        const Token close_bracket = previous();
        e->source.end_line = close_bracket.line;
        e->source.end_column = close_bracket.column + token_grapheme_width(close_bracket);
        return e;
    }
    if(match(TokenKind::Identifier)){
        const Token name=previous();
        if(match(TokenKind::GroupStart)){
            auto call=std::make_unique<ast::CallExpr>();
            call->line=name.line;
            call->source.line = name.line;
            call->source.column = name.column;
            call->callee=name.canonical;
            while(!check(TokenKind::GroupEnd)){
                if(check(TokenKind::Eof)||check(TokenKind::Newline))error(peek(),"expected 🤲 after function arguments");
                call->arguments.push_back(expression());
            }
            consume(TokenKind::GroupEnd,"expected 🤲 after function arguments");
            const Token close_paren = previous();
            call->source.end_line = close_paren.line;
            call->source.end_column = close_paren.column + token_grapheme_width(close_paren);
            return call;
        }
        auto e=std::make_unique<ast::VariableExpr>();
        e->line=name.line;
        e->source.line = name.line;
        e->source.column = name.column;
        e->source.end_line = name.line;
        e->source.end_column = name.column + token_grapheme_width(name);
        e->name=name.canonical;
        return e;
    }
    if(match(TokenKind::Input)){
        auto e=std::make_unique<ast::InputExpr>();
        const Token t = previous();
        e->line=t.line;
        e->source.line = t.line;
        e->source.column = t.column;
        e->source.end_line = t.line;
        e->source.end_column = t.column + token_grapheme_width(t);
        return e;
    }
    if(match(TokenKind::GroupStart)){
        auto e=expression();
        consume(TokenKind::GroupEnd,"expected 🤲 after grouped expression");
        const Token close_paren = previous();
        e->source.end_line = close_paren.line;
        e->source.end_column = close_paren.column + token_grapheme_width(close_paren);
        return e;
    }
    error(peek(),"expected an expression");
}

static const Token& eof_token(){
    static Token eof{TokenKind::Eof,"","","",0,0};
    return eof;
}

bool Parser::match(TokenKind kind){
    if(!check(kind))return false;
    advance();
    return true;
}

bool Parser::check(TokenKind kind)const{
    return peek().kind==kind;
}

const Token& Parser::advance(){
    if(!check(TokenKind::Eof)&&current_<tokens_.size())++current_;
    return previous();
}

const Token& Parser::previous()const{
    if(current_==0||current_>tokens_.size())return eof_token();
    return tokens_[current_-1];
}

const Token& Parser::peek()const{
    if(current_>=tokens_.size())return eof_token();
    return tokens_[current_];
}

const Token& Parser::consume(TokenKind kind,const std::string& message){
    if(check(kind))return advance();
    error(peek(),message);
}

void Parser::consume_line_end(){
    if(match(TokenKind::Newline)||check(TokenKind::Eof))return;
    error(peek(),"expected end of line");
}

void Parser::skip_newlines(){
    while(match(TokenKind::Newline)){}
}

[[noreturn]] void Parser::error(const Token& token,const std::string& message)const{
    std::ostringstream out;
    out<<message;
    if(!token.lexeme.empty())out<<" near '"<<token.lexeme<<"'";
    throw SourceLocationException(out.str(),{},token.line,token.column,token.lexeme);
}

}
