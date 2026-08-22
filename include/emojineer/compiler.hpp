#pragma once

#include "emojineer/ast.hpp"
#include "emojineer/bytecode.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace emojineer {

class Compiler {
public:
    Chunk compile(const ast::Program& program);

private:
    void compile_stmt(const ast::Stmt& stmt);
    void compile_expr(const ast::Expr& expr);
    void compile_block(const std::vector<ast::StmtPtr>& block);
    void emit(OpCode op, std::int32_t operand, std::size_t line);
    std::int32_t name_constant(const std::string& name);
    void emit_type_assert(std::optional<ast::DeclaredType> type, std::size_t line);

    Chunk chunk_;
    std::unordered_map<std::string, std::optional<ast::DeclaredType>> declared_types_;
};

} // namespace emojineer
