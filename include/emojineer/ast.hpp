#pragma once
#include "emojineer/token.hpp"
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
namespace emojineer::ast {
using LiteralValue=std::variant<double,bool,std::string>;
enum class DeclaredType{Number,String,Bool};
struct Expr{virtual ~Expr()=default;std::size_t line{1};};using ExprPtr=std::unique_ptr<Expr>;
struct LiteralExpr final:Expr{LiteralValue value;};
struct VariableExpr final:Expr{std::string name;};
struct InputExpr final:Expr{};
struct UnaryExpr final:Expr{TokenKind op;ExprPtr right;};
struct BinaryExpr final:Expr{ExprPtr left;TokenKind op;ExprPtr right;};
struct CallExpr final:Expr{std::string callee;std::vector<ExprPtr> arguments;};
struct Stmt{virtual ~Stmt()=default;std::size_t line{1};};using StmtPtr=std::unique_ptr<Stmt>;
struct VarDecl final:Stmt{std::string name;std::optional<DeclaredType> declared_type;ExprPtr initializer;};
struct Assignment final:Stmt{std::string name;ExprPtr value;};
struct PrintStmt final:Stmt{ExprPtr expression;};
struct ReturnStmt final:Stmt{ExprPtr expression;};
struct IfStmt final:Stmt{ExprPtr condition;std::vector<StmtPtr> then_branch;std::vector<StmtPtr> else_branch;};
struct WhileStmt final:Stmt{ExprPtr condition;std::vector<StmtPtr> body;};
struct FunctionDecl final:Stmt{std::string name;std::vector<std::string> parameters;std::vector<StmtPtr> body;};
struct Program{std::vector<StmtPtr> statements;};
}
