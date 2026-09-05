from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one anchor, found {count}')
    return text.replace(old, new, 1)


# Reconcile Train 18 module source identities onto the exact current Train 17 module linker.
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
        raise SystemExit(f'module stamp insertion: expected one anchor, found {text.count(anchor)}')
    text = text.replace(anchor, '\n' + stamp + anchor, 1)

text = replace_once(
    text,
    '                rewrite_stmt(*stmt, unit, nullptr);\n                linked.statements.push_back(std::move(stmt));',
    '                rewrite_stmt(*stmt, unit, nullptr);\n                stamp_stmt(*stmt, identity);\n                linked.statements.push_back(std::move(stmt));',
    'module linked statement stamp')
text = replace_once(
    text,
    '        Compiler compiler;\n        return compiler.compile(linked);',
    '        Compiler compiler;\n        compiler.set_source_path(entry_id);\n        return compiler.compile(linked);',
    'module linked compiler identity')
text = replace_once(
    text,
    '    if (!has_module_syntax(entry_program)) {\n        Compiler compiler;\n        return compiler.compile(entry_program);\n    }',
    '    if (!has_module_syntax(entry_program)) {\n        Compiler compiler;\n        compiler.set_source_path(identity);\n        return compiler.compile(entry_program);\n    }',
    'module plain compiler identity')
path.write_text(text, encoding='utf-8')


# A breakpoint is a source-level stop, not one stop per bytecode instruction on the same line.
# Resuming from a breakpoint must execute through the just-hit IP exactly once while preserving
# every other explicit breakpoint, including breakpoints encountered during stepping.
hpp = Path('include/emojineer/debugger.hpp')
text = hpp.read_text(encoding='utf-8')
text = replace_once(
    text,
    '    bool finished_{false};\n    std::string pause_reason_;',
    '    bool finished_{false};\n    std::string pause_reason_;\n    std::optional<std::size_t> resume_breakpoint_ip_;',
    'debugger resume state')
hpp.write_text(text, encoding='utf-8')

cpp = Path('src/debugger.cpp')
text = cpp.read_text(encoding='utf-8')
resume_line = '    if (paused_ && pause_reason_ == "breakpoint hit") resume_breakpoint_ip_ = vm_.current_ip();\n'
for signature in (
    'void DebugController::continue_execution() {\n',
    'void DebugController::step_into() {\n',
    'void DebugController::step_over() {\n',
    'void DebugController::step_out() {\n',
):
    if signature + resume_line not in text:
        if text.count(signature) != 1:
            raise SystemExit(f'{signature.strip()}: expected one function')
        text = text.replace(signature, signature + resume_line, 1)

text = replace_once(
    text,
    '    std::size_t ip = vm_.current_ip();\n    auto it = breakpoint_id_by_ip_.find(ip);',
    '    std::size_t ip = vm_.current_ip();\n    if (resume_breakpoint_ip_ && *resume_breakpoint_ip_ == ip) return false;\n    auto it = breakpoint_id_by_ip_.find(ip);',
    'breakpoint one-shot resume guard')

text = replace_once(
    text,
    '        current_chunk_ = chunk;\n        rebuild_breakpoint_index();\n    }\n    \n    // Check if we should pause',
    '        current_chunk_ = chunk;\n        rebuild_breakpoint_index();\n    }\n\n    // Reaching the post-instruction hook proves the one skipped instruction made progress.\n    resume_breakpoint_ip_.reset();\n    \n    // Check if we should pause',
    'clear resume guard after progress')

text = replace_once(
    text,
    '                breakpoint_id_by_ip_[ip] = i;\n            }',
    '                breakpoint_id_by_ip_[ip] = i;\n                break;\n            }',
    'bind source breakpoint to first executable IP')

cpp.write_text(text, encoding='utf-8')
print('Train 18 exact module reconciliation and breakpoint resume repair applied.')
