#pragma once
#include "emojineer/token.hpp"
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
namespace emojineer::ast {
using LiteralValue=std::variant<double,bool,std::string>;
enum class DeclaredType{Number,String,Bool,Array};

// Source range for precise source mapping (EMJBC v6)
struct SourceRange {
    std::size_t line{1};       // 1-based start line
    std::size_t column{1};     // 1-based start column
    std::size_t end_line{1};   // 1-based end line
    std::size_t end_column{1}; // 1-based end column
    
    // Module identity for source mapping (set by ModuleLinker for linked code)
    std::string module_identity;  // Deterministic identity (no absolute paths)
};

struct Expr{
    virtual ~Expr()=default;
    std::size_t line{1};
    SourceRange source;  // Full source range for EMJBC v6
};using ExprPtr=std::unique_ptr<Expr>;
struct LiteralExpr final:Expr{LiteralValue value;};
struct VariableExpr final:Expr{std::string name;};
struct InputExpr final:Expr{};
struct UnaryExpr final:Expr{TokenKind op;ExprPtr right;};
struct BinaryExpr final:Expr{ExprPtr left;TokenKind op;ExprPtr right;};
struct CallExpr final:Expr{std::string callee;std::vector<ExprPtr> arguments;};
struct ArrayExpr final:Expr{std::vector<ExprPtr> elements;};
struct IndexExpr final:Expr{ExprPtr collection;ExprPtr index;};
struct LengthExpr final:Expr{ExprPtr value;};
struct AppendExpr final:Expr{ExprPtr collection;ExprPtr value;};
struct SetIndexExpr final:Expr{ExprPtr collection;ExprPtr index;ExprPtr value;};
struct Stmt{
    virtual ~Stmt()=default;
    std::size_t line{1};
    SourceRange source;  // Full source range for EMJBC v6
};using StmtPtr=std::unique_ptr<Stmt>;
struct VarDecl final:Stmt{std::string name;std::optional<DeclaredType> declared_type;ExprPtr initializer;};
struct Assignment final:Stmt{std::string name;ExprPtr value;};
struct PrintStmt final:Stmt{ExprPtr expression;};
struct ReturnStmt final:Stmt{ExprPtr expression;};
struct IfStmt final:Stmt{ExprPtr condition;std::vector<StmtPtr> then_branch;std::vector<StmtPtr> else_branch;};
struct WhileStmt final:Stmt{ExprPtr condition;std::vector<StmtPtr> body;};
struct FunctionDecl final:Stmt{std::string name;std::vector<std::string> parameters;std::vector<StmtPtr> body;};
struct ModuleDecl final:Stmt{std::string name;};
struct ImportStmt final:Stmt{std::string path;};
struct ExportStmt final:Stmt{std::string name;};
struct Program{std::vector<StmtPtr> statements;};
}
