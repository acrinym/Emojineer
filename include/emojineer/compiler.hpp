#pragma once
#include "emojineer/ast.hpp"
#include "emojineer/bytecode.hpp"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
namespace emojineer {

/// Lowers one already-parsed/linked Emojineer AST into sovereign EMJBC.
///
/// Native Train 20 facilities are recognized here as reserved identifiers and
/// lower directly to HostCall instructions while contributing to the chunk's
/// verifier-bound required capability mask.
class Compiler{
public:
    /// Compile a complete program into a self-contained bytecode chunk.
    Chunk compile(const ast::Program& program);

    /// Set the deterministic source identity embedded in source-map metadata.
    ///
    /// Callers should provide linker-owned portable identities, never checkout-absolute roots.
    void set_source_path(const std::string& path) { source_path_ = path; }
private:
    /// Compile one statement in the current module/function context.
    void compile_stmt(const ast::Stmt& stmt);

    /// Compile one expression, including intrinsic HostCall lowering.
    void compile_expr(const ast::Expr& expr);

    /// Compile a statement block without creating a second runtime scope model.
    void compile_block(const std::vector<ast::StmtPtr>& block);

    /// Compile one declared function into its preallocated function-table slot.
    void compile_function(const ast::FunctionDecl& fn,std::size_t index);

    /// Discover function-local slots before bytecode emission begins.
    void collect_locals(const std::vector<ast::StmtPtr>& block);

    /// Emit an instruction with line/column source information.
    void emit(OpCode op,std::int32_t operand,std::size_t line,std::size_t column = 1);

    /// Emit an instruction from the AST's exact EMJBC v6 source range.
    void emit(OpCode op, std::int32_t operand, const ast::SourceRange* source);

    /// Append a global/function name string and return its constant-pool index.
    ///
    /// This helper does not deduplicate equal strings; each call uses Chunk::add_constant.
    std::int32_t name_constant(const std::string& name);

    /// Emit the runtime assertion required by an optional declaration type.
    void emit_type_assert(std::optional<ast::DeclaredType> type,std::size_t line);

    /// Resolve a current-function local name to its stable slot when present.
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
