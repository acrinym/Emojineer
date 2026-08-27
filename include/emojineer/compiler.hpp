#pragma once
#include "emojineer/ast.hpp"
#include "emojineer/bytecode.hpp"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
namespace emojineer {
class Compiler{
public:
    Chunk compile(const ast::Program& program);
    void set_source_path(const std::string& path) { source_path_ = path; }
private:
    void compile_stmt(const ast::Stmt& stmt);
    void compile_expr(const ast::Expr& expr);
    void compile_block(const std::vector<ast::StmtPtr>& block);
    void compile_function(const ast::FunctionDecl& fn,std::size_t index);
    void collect_locals(const std::vector<ast::StmtPtr>& block);
    void emit(OpCode op,std::int32_t operand,std::size_t line,std::size_t column = 1);
    void emit(OpCode op, std::int32_t operand, const ast::SourceRange* source);  // EMJBC v6: use AST source range
    std::int32_t name_constant(const std::string& name);
    void emit_type_assert(std::optional<ast::DeclaredType> type,std::size_t line);
    std::optional<std::int32_t> local_slot(const std::string& name)const;
    
    Chunk chunk_;
    std::string source_path_;  // Deterministic module identity (no absolute roots)
    std::unordered_map<std::string,std::optional<ast::DeclaredType>> declared_types_;
    std::unordered_map<std::string,std::size_t> function_indices_;
    std::unordered_map<std::string,std::int32_t> locals_;
    std::unordered_map<std::string,std::optional<ast::DeclaredType>> local_types_;
    bool in_function_{false};
    std::string current_function_name_;  // Current function context for source mapping
};
}
