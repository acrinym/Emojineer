from pathlib import Path

path = Path('src/module.cpp')
text = path.read_text(encoding='utf-8')

stamp = r'''
// Recursively stamp deterministic module identity onto source ranges for debugger metadata.
void stamp_expr(ast::Expr& expr, const std::string& identity) {
    expr.source.module_identity = identity;
    if (auto* unary = dynamic_cast<ast::UnaryExpr*>(&expr)) {
        stamp_expr(*unary->right, identity);
    } else if (auto* binary = dynamic_cast<ast::BinaryExpr*>(&expr)) {
        stamp_expr(*binary->left, identity);
        stamp_expr(*binary->right, identity);
    } else if (auto* call = dynamic_cast<ast::CallExpr*>(&expr)) {
        for (auto& arg : call->arguments) stamp_expr(*arg, identity);
    } else if (auto* array = dynamic_cast<ast::ArrayExpr*>(&expr)) {
        for (auto& element : array->elements) stamp_expr(*element, identity);
    } else if (auto* index = dynamic_cast<ast::IndexExpr*>(&expr)) {
        stamp_expr(*index->collection, identity);
        stamp_expr(*index->index, identity);
    } else if (auto* length = dynamic_cast<ast::LengthExpr*>(&expr)) {
        stamp_expr(*length->value, identity);
    } else if (auto* append = dynamic_cast<ast::AppendExpr*>(&expr)) {
        stamp_expr(*append->collection, identity);
        stamp_expr(*append->value, identity);
    } else if (auto* set = dynamic_cast<ast::SetIndexExpr*>(&expr)) {
        stamp_expr(*set->collection, identity);
        stamp_expr(*set->index, identity);
        stamp_expr(*set->value, identity);
    }
}

void stamp_stmt(ast::Stmt& stmt, const std::string& identity) {
    stmt.source.module_identity = identity;
    if (auto* var = dynamic_cast<ast::VarDecl*>(&stmt)) {
        if (var->initializer) stamp_expr(*var->initializer, identity);
    } else if (auto* assignment = dynamic_cast<ast::Assignment*>(&stmt)) {
        stamp_expr(*assignment->value, identity);
    } else if (auto* print = dynamic_cast<ast::PrintStmt*>(&stmt)) {
        stamp_expr(*print->expression, identity);
    } else if (auto* ret = dynamic_cast<ast::ReturnStmt*>(&stmt)) {
        stamp_expr(*ret->expression, identity);
    } else if (auto* branch = dynamic_cast<ast::IfStmt*>(&stmt)) {
        stamp_expr(*branch->condition, identity);
        for (auto& child : branch->then_branch) stamp_stmt(*child, identity);
        for (auto& child : branch->else_branch) stamp_stmt(*child, identity);
    } else if (auto* loop = dynamic_cast<ast::WhileStmt*>(&stmt)) {
        stamp_expr(*loop->condition, identity);
        for (auto& child : loop->body) stamp_stmt(*child, identity);
    } else if (auto* fn = dynamic_cast<ast::FunctionDecl*>(&stmt)) {
        for (auto& child : fn->body) stamp_stmt(*child, identity);
    }
}
'''

anchor = '\nvoid rewrite_stmt(ast::Stmt& stmt, ModuleUnit& unit,\n'
if stamp.strip() not in text:
    if text.count(anchor) != 1:
        raise SystemExit(f'expected one rewrite_stmt anchor, found {text.count(anchor)}')
    text = text.replace(anchor, '\n' + stamp + anchor, 1)

old = '                rewrite_stmt(*stmt, unit, nullptr);\n                linked.statements.push_back(std::move(stmt));'
new = '                rewrite_stmt(*stmt, unit, nullptr);\n                stamp_stmt(*stmt, identity);\n                linked.statements.push_back(std::move(stmt));'
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f'expected one linked rewrite anchor, found {text.count(old)}')
    text = text.replace(old, new, 1)

old = '        Compiler compiler;\n        return compiler.compile(linked);'
new = '        Compiler compiler;\n        compiler.set_source_path(entry_id);\n        return compiler.compile(linked);'
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f'expected one linked compiler anchor, found {text.count(old)}')
    text = text.replace(old, new, 1)

old = '    if (!has_module_syntax(entry_program)) {\n        Compiler compiler;\n        return compiler.compile(entry_program);\n    }'
new = '    if (!has_module_syntax(entry_program)) {\n        Compiler compiler;\n        compiler.set_source_path(identity);\n        return compiler.compile(entry_program);\n    }'
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f'expected one non-module compiler anchor, found {text.count(old)}')
    text = text.replace(old, new, 1)

path.write_text(text, encoding='utf-8')
print('Train 18 module identity reconciliation applied exactly.')
