#include "emojineer/compiler.hpp"

#include <stdexcept>

namespace emojineer {

Chunk Compiler::compile(const ast::Program& program) { chunk_ = {}; declared_types_.clear(); compile_block(program.statements); emit(OpCode::Halt, 0, 0); return chunk_; }
void Compiler::compile_block(const std::vector<ast::StmtPtr>& block) { for (const auto& stmt : block) compile_stmt(*stmt); }
void Compiler::compile_stmt(const ast::Stmt& stmt) {
    if (const auto* var = dynamic_cast<const ast::VarDecl*>(&stmt)) { compile_expr(*var->initializer); emit_type_assert(var->declared_type,var->line); emit(OpCode::StoreGlobal,name_constant(var->name),var->line); declared_types_[var->name]=var->declared_type; return; }
    if (const auto* a = dynamic_cast<const ast::Assignment*>(&stmt)) { compile_expr(*a->value); auto it=declared_types_.find(a->name); if(it!=declared_types_.end()) emit_type_assert(it->second,a->line); emit(OpCode::StoreGlobal,name_constant(a->name),a->line); return; }
    if (const auto* p = dynamic_cast<const ast::PrintStmt*>(&stmt)) { compile_expr(*p->expression); emit(OpCode::Print,0,p->line); return; }
    if (const auto* f = dynamic_cast<const ast::IfStmt*>(&stmt)) { compile_expr(*f->condition); const std::size_t false_jump=chunk_.code.size(); emit(OpCode::JumpIfFalse,0,f->line); compile_block(f->then_branch); if(!f->else_branch.empty()){const std::size_t end_jump=chunk_.code.size();emit(OpCode::Jump,0,f->line);chunk_.code[false_jump].operand=static_cast<std::int32_t>(chunk_.code.size());compile_block(f->else_branch);chunk_.code[end_jump].operand=static_cast<std::int32_t>(chunk_.code.size());}else chunk_.code[false_jump].operand=static_cast<std::int32_t>(chunk_.code.size()); return; }
    if (const auto* w = dynamic_cast<const ast::WhileStmt*>(&stmt)) { const std::size_t start=chunk_.code.size(); compile_expr(*w->condition); const std::size_t exit=chunk_.code.size(); emit(OpCode::JumpIfFalse,0,w->line); compile_block(w->body); emit(OpCode::Jump,static_cast<std::int32_t>(start),w->line); chunk_.code[exit].operand=static_cast<std::int32_t>(chunk_.code.size()); return; }
    throw std::runtime_error("compiler encountered unknown statement node");
}
void Compiler::compile_expr(const ast::Expr& expr) {
    if (const auto* l=dynamic_cast<const ast::LiteralExpr*>(&expr)) { std::visit([&](const auto& v){emit(OpCode::Constant,chunk_.add_constant(Value{v}),expr.line);},l->value); return; }
    if (const auto* v=dynamic_cast<const ast::VariableExpr*>(&expr)) { emit(OpCode::LoadGlobal,name_constant(v->name),expr.line); return; }
    if (dynamic_cast<const ast::InputExpr*>(&expr)) { emit(OpCode::ReadLine,0,expr.line); return; }
    if (const auto* u=dynamic_cast<const ast::UnaryExpr*>(&expr)) { compile_expr(*u->right); if(u->op==TokenKind::Subtract)emit(OpCode::Negate,0,expr.line);else if(u->op==TokenKind::Not)emit(OpCode::Not,0,expr.line);else throw std::runtime_error("unsupported unary operator"); return; }
    if (const auto* b=dynamic_cast<const ast::BinaryExpr*>(&expr)) { compile_expr(*b->left); compile_expr(*b->right); switch(b->op){case TokenKind::Add:emit(OpCode::Add,0,expr.line);break;case TokenKind::Subtract:emit(OpCode::Subtract,0,expr.line);break;case TokenKind::Multiply:emit(OpCode::Multiply,0,expr.line);break;case TokenKind::Divide:emit(OpCode::Divide,0,expr.line);break;case TokenKind::Modulo:emit(OpCode::Modulo,0,expr.line);break;case TokenKind::Equal:emit(OpCode::Equal,0,expr.line);break;case TokenKind::Less:emit(OpCode::Less,0,expr.line);break;case TokenKind::Greater:emit(OpCode::Greater,0,expr.line);break;default:throw std::runtime_error("unsupported binary operator");} return; }
    throw std::runtime_error("compiler encountered unknown expression node");
}
void Compiler::emit(OpCode op,std::int32_t operand,std::size_t line){chunk_.code.push_back({op,operand,static_cast<std::uint32_t>(line)});}
std::int32_t Compiler::name_constant(const std::string& name){return chunk_.add_constant(name);}
void Compiler::emit_type_assert(std::optional<ast::DeclaredType> type,std::size_t line){if(!type)return;switch(*type){case ast::DeclaredType::Number:emit(OpCode::AssertNumber,0,line);break;case ast::DeclaredType::String:emit(OpCode::AssertString,0,line);break;case ast::DeclaredType::Bool:emit(OpCode::AssertBool,0,line);break;}}

} // namespace emojineer
