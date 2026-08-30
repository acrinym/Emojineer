#include "emojineer/module.hpp"

#include "emojineer/ast.hpp"
#include "emojineer/compiler.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/package.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/project.hpp"
#include "emojineer/source_diagnostic.hpp"
#include "emojineer/stdlib.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace emojineer {
namespace {

struct ImportSpec {
    std::string requested;
    std::size_t line{0};
    std::string identity;
};

struct ModuleUnit {
    std::filesystem::path path;
    std::filesystem::path package_root;
    std::string package_name;
    std::string identity;
    std::string module_name;
    ast::Program program;
    std::vector<ImportSpec> imports;
    std::unordered_set<std::string> exports;
    std::unordered_set<std::string> declared_globals;
    std::unordered_set<std::string> implicit_globals;
    std::unordered_set<std::string> functions;
    std::unordered_map<std::string, std::string> imported_globals;
    std::unordered_map<std::string, std::string> imported_functions;
};

struct ResolvedSourceImport {
    std::filesystem::path path;
    std::string package_name;
    std::string identity;
};

enum class VisitState { Visiting, Done };

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open '" + path.string() + "'");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

ast::Program parse_text(const std::string& source,
                        const CustomEmojiRegistry& registry,
                        const std::string& identity,
                        const std::filesystem::path& sourcePath = {}) {
    try {
        Lexer lexer(source, registry);
        Parser parser(lexer.tokenize());
        return parser.parse();
    } catch (const SourceLocationException& sle) {
        // Preserve typed source errors, attaching the sourceIdentity and real sourcePath separately.
        // sourcePath should be an actual filesystem path, identity is the module identity (e.g., "pkg:foo/src/main.emoji")
        if (sle.sourcePath.empty()) {
            throw SourceLocationException(sle.message, sourcePath, identity, sle.line, sle.column, sle.tokenLexeme);
        }
        throw;
    } catch (const std::exception& error) {
        throw std::runtime_error("module '" + identity + "': " + error.what());
    }
}

// Parse source from a path, optionally using a SourceProvider for in-memory overlays.
// If source_provider is provided and returns a value, use that instead of reading from disk.
ast::Program parse_source(const std::filesystem::path& path,
                          const CustomEmojiRegistry& registry,
                          const std::string& identity,
                          SourceProvider source_provider = {}) {
    // First check if the source provider has the content
    if (source_provider) {
        auto overlay = source_provider(path);
        if (overlay) {
            return parse_text(*overlay, registry, identity, path);
        }
    }
    // Fall back to reading from disk
    return parse_text(read_text(path), registry, identity, path);
}

bool has_module_syntax_stmt(const ast::Stmt& stmt) {
    if (dynamic_cast<const ast::ModuleDecl*>(&stmt) ||
        dynamic_cast<const ast::ImportStmt*>(&stmt) ||
        dynamic_cast<const ast::ExportStmt*>(&stmt)) return true;
    if (const auto* branch = dynamic_cast<const ast::IfStmt*>(&stmt)) {
        for (const auto& child : branch->then_branch) if (has_module_syntax_stmt(*child)) return true;
        for (const auto& child : branch->else_branch) if (has_module_syntax_stmt(*child)) return true;
    }
    if (const auto* loop = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
        for (const auto& child : loop->body) if (has_module_syntax_stmt(*child)) return true;
    }
    if (const auto* fn = dynamic_cast<const ast::FunctionDecl*>(&stmt)) {
        for (const auto& child : fn->body) if (has_module_syntax_stmt(*child)) return true;
    }
    return false;
}

bool has_module_syntax(const ast::Program& program) {
    for (const auto& stmt : program.statements) if (has_module_syntax_stmt(*stmt)) return true;
    return false;
}

std::filesystem::path discover_root(const std::filesystem::path& entry) {
    std::filesystem::path dir = entry.parent_path();
    while (!dir.empty()) {
        if (std::filesystem::exists(dir / "emojineer.toml")) return dir;
        const auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return entry.parent_path();
}

bool within(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto r = root.begin();
    auto p = path.begin();
    for (; r != root.end(); ++r, ++p) {
        if (p == path.end() || *r != *p) return false;
    }
    return true;
}

std::size_t path_depth(const std::filesystem::path& path) {
    return static_cast<std::size_t>(std::distance(path.begin(), path.end()));
}

std::string identity_for(const std::filesystem::path& root,
                         const std::filesystem::path& path) {
    return std::filesystem::relative(path, root).generic_string();
}

std::string internal_name(const ModuleUnit& unit, const std::string& source_name) {
    return "@module/" + unit.identity + "::" + source_name;
}

void reject_nested_module_syntax(const std::vector<ast::StmtPtr>& block,
                                 const std::string& identity) {
    for (const auto& stmt : block) {
        if (dynamic_cast<const ast::ModuleDecl*>(stmt.get()) ||
            dynamic_cast<const ast::ImportStmt*>(stmt.get()) ||
            dynamic_cast<const ast::ExportStmt*>(stmt.get())) {
            throw SourceLocationException("🧩, 🔗, and 📤 are top-level only", 
                                             {}, stmt->line, 1);
        }
        if (const auto* branch = dynamic_cast<const ast::IfStmt*>(stmt.get())) {
            reject_nested_module_syntax(branch->then_branch, identity);
            reject_nested_module_syntax(branch->else_branch, identity);
        } else if (const auto* loop = dynamic_cast<const ast::WhileStmt*>(stmt.get())) {
            reject_nested_module_syntax(loop->body, identity);
        } else if (const auto* fn = dynamic_cast<const ast::FunctionDecl*>(stmt.get())) {
            reject_nested_module_syntax(fn->body, identity);
        }
    }
}

void collect_module_globals(const std::vector<ast::StmtPtr>& block, ModuleUnit& unit) {
    for (const auto& stmt : block) {
        if (const auto* var = dynamic_cast<const ast::VarDecl*>(stmt.get())) {
            unit.declared_globals.insert(var->name);
        } else if (const auto* assignment = dynamic_cast<const ast::Assignment*>(stmt.get())) {
            unit.implicit_globals.insert(assignment->name);
        } else if (const auto* branch = dynamic_cast<const ast::IfStmt*>(stmt.get())) {
            collect_module_globals(branch->then_branch, unit);
            collect_module_globals(branch->else_branch, unit);
        } else if (const auto* loop = dynamic_cast<const ast::WhileStmt*>(stmt.get())) {
            collect_module_globals(loop->body, unit);
        }
    }
}

void collect_function_locals(const std::vector<ast::StmtPtr>& block,
                             std::unordered_set<std::string>& locals) {
    for (const auto& stmt : block) {
        if (const auto* var = dynamic_cast<const ast::VarDecl*>(stmt.get())) {
            locals.insert(var->name);
        } else if (const auto* branch = dynamic_cast<const ast::IfStmt*>(stmt.get())) {
            collect_function_locals(branch->then_branch, locals);
            collect_function_locals(branch->else_branch, locals);
        } else if (const auto* loop = dynamic_cast<const ast::WhileStmt*>(stmt.get())) {
            collect_function_locals(loop->body, locals);
        }
    }
}

void collect_function_implicit_globals(const std::vector<ast::StmtPtr>& block,
                                       const std::unordered_set<std::string>& locals,
                                       ModuleUnit& unit) {
    for (const auto& stmt : block) {
        if (const auto* assignment = dynamic_cast<const ast::Assignment*>(stmt.get())) {
            if (!locals.contains(assignment->name)) unit.implicit_globals.insert(assignment->name);
        } else if (const auto* branch = dynamic_cast<const ast::IfStmt*>(stmt.get())) {
            collect_function_implicit_globals(branch->then_branch, locals, unit);
            collect_function_implicit_globals(branch->else_branch, locals, unit);
        } else if (const auto* loop = dynamic_cast<const ast::WhileStmt*>(stmt.get())) {
            collect_function_implicit_globals(loop->body, locals, unit);
        }
    }
}

void analyze_unit(ModuleUnit& unit) {
    if (unit.program.statements.empty() ||
        !dynamic_cast<ast::ModuleDecl*>(unit.program.statements.front().get())) {
        throw std::runtime_error("module '" + unit.identity +
                                 "': multi-file source must begin with 🧩 <emoji-module-name>");
    }

    bool saw_module = false;
    bool saw_runtime = false;
    for (std::size_t i = 0; i < unit.program.statements.size(); ++i) {
        auto& stmt = unit.program.statements[i];
        if (auto* module = dynamic_cast<ast::ModuleDecl*>(stmt.get())) {
            if (i != 0 || saw_module) {
                throw std::runtime_error("module '" + unit.identity + "' line " +
                                         std::to_string(stmt->line) +
                                         ": exactly one 🧩 declaration is allowed and it must be first");
            }
            saw_module = true;
            unit.module_name = module->name;
            continue;
        }
        if (auto* import = dynamic_cast<ast::ImportStmt*>(stmt.get())) {
            if (saw_runtime) {
                throw std::runtime_error("module '" + unit.identity + "' line " +
                                         std::to_string(stmt->line) +
                                         ": 🔗 imports must appear before executable/declaration statements");
            }
            unit.imports.push_back({import->path, import->line, {}});
            continue;
        }
        if (auto* export_stmt = dynamic_cast<ast::ExportStmt*>(stmt.get())) {
            if (!unit.exports.insert(export_stmt->name).second) {
                throw std::runtime_error("module '" + unit.identity + "' line " +
                                         std::to_string(stmt->line) +
                                         ": duplicate 📤 export");
            }
            continue;
        }
        saw_runtime = true;
        if (auto* fn = dynamic_cast<ast::FunctionDecl*>(stmt.get())) {
            unit.functions.insert(fn->name);
            std::unordered_set<std::string> locals(fn->parameters.begin(), fn->parameters.end());
            collect_function_locals(fn->body, locals);
            collect_function_implicit_globals(fn->body, locals, unit);
        }
    }

    for (const auto& stmt : unit.program.statements) {
        if (const auto* branch = dynamic_cast<const ast::IfStmt*>(stmt.get())) {
            reject_nested_module_syntax(branch->then_branch, unit.identity);
            reject_nested_module_syntax(branch->else_branch, unit.identity);
        } else if (const auto* loop = dynamic_cast<const ast::WhileStmt*>(stmt.get())) {
            reject_nested_module_syntax(loop->body, unit.identity);
        } else if (const auto* fn = dynamic_cast<const ast::FunctionDecl*>(stmt.get())) {
            reject_nested_module_syntax(fn->body, unit.identity);
        }
    }
    collect_module_globals(unit.program.statements, unit);

    for (const auto& name : unit.exports) {
        if (!unit.declared_globals.contains(name) && !unit.functions.contains(name)) {
            throw std::runtime_error("module '" + unit.identity +
                                     "': 📤 exports a symbol that is not declared in this module");
        }
    }
}

void rewrite_expr(ast::Expr& expr, ModuleUnit& unit,
                  const std::unordered_set<std::string>* locals);

std::string resolve_global(const ModuleUnit& unit, const std::string& name,
                           std::size_t line, bool assignment) {
    if (unit.declared_globals.contains(name)) return internal_name(unit, name);
    if (auto it = unit.imported_globals.find(name); it != unit.imported_globals.end()) return it->second;
    if (unit.implicit_globals.contains(name)) return internal_name(unit, name);
    if (assignment) return internal_name(unit, name);
    throw std::runtime_error("module '" + unit.identity + "' line " + std::to_string(line) +
                             ": undefined or non-exported emoji variable '" + name + "'");
}

std::string resolve_function(const ModuleUnit& unit, const std::string& name,
                             std::size_t line) {
    if (unit.functions.contains(name)) return internal_name(unit, name);
    if (auto it = unit.imported_functions.find(name); it != unit.imported_functions.end()) return it->second;
    throw std::runtime_error("module '" + unit.identity + "' line " + std::to_string(line) +
                             ": undefined or non-exported emoji function '" + name + "'");
}

void rewrite_stmt(ast::Stmt& stmt, ModuleUnit& unit,
                  const std::unordered_set<std::string>* locals) {
    const bool in_function = locals != nullptr;
    if (auto* var = dynamic_cast<ast::VarDecl*>(&stmt)) {
        rewrite_expr(*var->initializer, unit, locals);
        if (!in_function) var->name = internal_name(unit, var->name);
        return;
    }
    if (auto* assignment = dynamic_cast<ast::Assignment*>(&stmt)) {
        rewrite_expr(*assignment->value, unit, locals);
        if (!(in_function && locals->contains(assignment->name))) {
            assignment->name = resolve_global(unit, assignment->name, assignment->line, true);
        }
        return;
    }
    if (auto* print = dynamic_cast<ast::PrintStmt*>(&stmt)) {
        rewrite_expr(*print->expression, unit, locals);
        return;
    }
    if (auto* ret = dynamic_cast<ast::ReturnStmt*>(&stmt)) {
        rewrite_expr(*ret->expression, unit, locals);
        return;
    }
    if (auto* branch = dynamic_cast<ast::IfStmt*>(&stmt)) {
        rewrite_expr(*branch->condition, unit, locals);
        for (auto& child : branch->then_branch) rewrite_stmt(*child, unit, locals);
        for (auto& child : branch->else_branch) rewrite_stmt(*child, unit, locals);
        return;
    }
    if (auto* loop = dynamic_cast<ast::WhileStmt*>(&stmt)) {
        rewrite_expr(*loop->condition, unit, locals);
        for (auto& child : loop->body) rewrite_stmt(*child, unit, locals);
        return;
    }
    if (auto* fn = dynamic_cast<ast::FunctionDecl*>(&stmt)) {
        std::unordered_set<std::string> fn_locals(fn->parameters.begin(), fn->parameters.end());
        collect_function_locals(fn->body, fn_locals);
        for (auto& child : fn->body) rewrite_stmt(*child, unit, &fn_locals);
        fn->name = internal_name(unit, fn->name);
        return;
    }
    if (dynamic_cast<ast::ModuleDecl*>(&stmt) || dynamic_cast<ast::ImportStmt*>(&stmt) ||
        dynamic_cast<ast::ExportStmt*>(&stmt)) {
        throw std::runtime_error("module linker encountered an unresolved module statement");
    }
}

void rewrite_expr(ast::Expr& expr, ModuleUnit& unit,
                  const std::unordered_set<std::string>* locals) {
    if (dynamic_cast<ast::LiteralExpr*>(&expr) || dynamic_cast<ast::InputExpr*>(&expr)) return;
    if (auto* variable = dynamic_cast<ast::VariableExpr*>(&expr)) {
        if (!(locals && locals->contains(variable->name))) {
            variable->name = resolve_global(unit, variable->name, variable->line, false);
        }
        return;
    }
    if (auto* unary = dynamic_cast<ast::UnaryExpr*>(&expr)) {
        rewrite_expr(*unary->right, unit, locals);
        return;
    }
    if (auto* binary = dynamic_cast<ast::BinaryExpr*>(&expr)) {
        rewrite_expr(*binary->left, unit, locals);
        rewrite_expr(*binary->right, unit, locals);
        return;
    }
    if (auto* call = dynamic_cast<ast::CallExpr*>(&expr)) {
        for (auto& arg : call->arguments) rewrite_expr(*arg, unit, locals);
        call->callee = resolve_function(unit, call->callee, call->line);
        return;
    }
    if (auto* array = dynamic_cast<ast::ArrayExpr*>(&expr)) {
        for (auto& element : array->elements) rewrite_expr(*element, unit, locals);
        return;
    }
    if (auto* index = dynamic_cast<ast::IndexExpr*>(&expr)) {
        rewrite_expr(*index->collection, unit, locals);
        rewrite_expr(*index->index, unit, locals);
        return;
    }
    if (auto* length = dynamic_cast<ast::LengthExpr*>(&expr)) {
        rewrite_expr(*length->value, unit, locals);
        return;
    }
    if (auto* append = dynamic_cast<ast::AppendExpr*>(&expr)) {
        rewrite_expr(*append->collection, unit, locals);
        rewrite_expr(*append->value, unit, locals);
        return;
    }
    if (auto* set = dynamic_cast<ast::SetIndexExpr*>(&expr)) {
        rewrite_expr(*set->collection, unit, locals);
        rewrite_expr(*set->index, unit, locals);
        rewrite_expr(*set->value, unit, locals);
        return;
    }
    throw std::runtime_error("module linker encountered unknown expression node");
}

class ModuleLinker {
public:
    ModuleLinker(std::filesystem::path root,
                 CustomEmojiRegistry registry,
                 std::optional<PackageGraph> package_graph,
                 SourceProvider source_provider = {})
        : root_(std::move(root)),
          registry_(std::move(registry)),
          package_graph_(std::move(package_graph)),
          source_provider_(std::move(source_provider)) {}

    Chunk compile(const std::filesystem::path& entry) {
        std::vector<std::string> stack;
        const std::string package_name = package_graph_ ? package_graph_->root_name : std::string{};
        const std::string entry_id = visit(entry, package_name, stack);
        (void)entry_id;
        build_import_bindings();

        ast::Program linked;
        for (const auto& identity : order_) {
            auto& unit = units_.at(identity);
            for (auto& stmt : unit.program.statements) {
                if (dynamic_cast<ast::ModuleDecl*>(stmt.get()) ||
                    dynamic_cast<ast::ImportStmt*>(stmt.get()) ||
                    dynamic_cast<ast::ExportStmt*>(stmt.get())) continue;
                rewrite_stmt(*stmt, unit, nullptr);
                linked.statements.push_back(std::move(stmt));
            }
        }
        Compiler compiler;
        return compiler.compile(linked);
    }

private:
    [[noreturn]] void cycle_error(const std::string& identity,
                                  const std::vector<std::string>& stack) const {
        std::ostringstream cycle;
        cycle << "cyclic module import: ";
        auto first = std::find(stack.begin(), stack.end(), identity);
        if (first == stack.end()) first = stack.begin();
        bool sep = false;
        for (auto it = first; it != stack.end(); ++it) {
            if (sep) cycle << " -> ";
            sep = true;
            cycle << *it;
        }
        if (sep) cycle << " -> ";
        cycle << identity;
        throw std::runtime_error(cycle.str());
    }

    void register_unit(ModuleUnit unit) {
        const std::string identity = unit.identity;
        if (auto existing = module_names_.find(unit.module_name); existing != module_names_.end()) {
            throw std::runtime_error("duplicate 🧩 module name declared by '" + existing->second +
                                     "' and '" + identity + "'");
        }
        module_names_[unit.module_name] = identity;
        units_.emplace(identity, std::move(unit));
    }

    const ResolvedPackage* package(const std::string& name) const {
        if (!package_graph_) return nullptr;
        return package_graph_->find(name);
    }

    const ResolvedPackage* owner_for_path(const std::filesystem::path& path) const {
        if (!package_graph_) return nullptr;
        const ResolvedPackage* owner = nullptr;
        std::size_t owner_depth = 0;
        for (const auto& candidate : package_graph_->packages) {
            if (!within(candidate.root, path)) continue;
            const auto depth = path_depth(candidate.root);
            if (!owner || depth > owner_depth) {
                owner = &candidate;
                owner_depth = depth;
            }
        }
        return owner;
    }

    const std::filesystem::path& package_root(const std::string& package_name) const {
        if (!package_graph_) return root_;
        const auto* resolved = package(package_name);
        if (!resolved) {
            throw std::runtime_error("internal error: module references unknown package '" +
                                     package_name + "'");
        }
        return resolved->root;
    }

    std::string module_identity(const std::string& package_name,
                                const std::filesystem::path& path) const {
        const auto& package_path = package_root(package_name);
        const auto relative = identity_for(package_path, path);
        if (!package_graph_ || package_name == package_graph_->root_name) return relative;
        return "pkg:" + package_name + "/" + relative;
    }

    void require_owned_path(const std::string& package_name,
                            const std::filesystem::path& path,
                            const std::string& context) const {
        const auto& expected_root = package_root(package_name);
        if (!within(expected_root, path)) {
            throw std::runtime_error(context + " escapes the " +
                                     (package_graph_ ? std::string("package root")
                                                     : std::string("module root")));
        }
        if (!package_graph_) return;
        const auto* owner = owner_for_path(path);
        if (!owner || owner->name != package_name) {
            throw std::runtime_error(context + " crosses a package boundary");
        }
    }

    std::filesystem::path resolve_local_import(const ModuleUnit& importer,
                                               const ImportSpec& spec) const {
        const std::filesystem::path requested(spec.requested);
        if (requested.empty() || requested.is_absolute()) {
            throw SourceLocationException("🔗 import path must be non-empty and relative",
                                             {}, spec.line, 1);
        }
        if (spec.requested.find('\\') != std::string::npos) {
            throw SourceLocationException("🔗 import paths must use portable forward slashes",
                                             {}, spec.line, 1);
        }
        if (requested.extension() != ".emoji") {
            throw SourceLocationException("🔗 import must target a .emoji source file, pkg:<dependency>/<module>.emoji, or std:<module>",
                                             {}, spec.line, 1);
        }
        const auto candidate = importer.path.parent_path() / requested;
        if (!std::filesystem::exists(candidate)) {
            throw SourceLocationException("imported module '" + spec.requested + "' does not exist",
                                             {}, spec.line, 1, spec.requested);
        }
        if (!std::filesystem::is_regular_file(candidate)) {
            throw SourceLocationException("imported module '" + spec.requested + "' is not a regular file",
                                             {}, spec.line, 1, spec.requested);
        }
        const auto canonical = std::filesystem::canonical(candidate);
        const std::string context = "module '" + importer.identity + "' line " +
                                    std::to_string(spec.line) + ": 🔗 import";
        require_owned_path(importer.package_name, canonical, context);
        return canonical;
    }

    ResolvedSourceImport resolve_package_import(const ModuleUnit& importer,
                                                const ImportSpec& spec) const {
        if (!package_graph_) {
            throw std::runtime_error("module '" + importer.identity + "' line " +
                                     std::to_string(spec.line) +
                                     ": pkg: imports require an enclosing emojineer.toml package graph");
        }
        if (spec.requested.find('\\') != std::string::npos) {
            throw std::runtime_error("module '" + importer.identity + "' line " +
                                     std::to_string(spec.line) +
                                     ": pkg: import paths must use portable forward slashes");
        }

        const std::string coordinate = spec.requested.substr(4);
        const auto slash = coordinate.find('/');
        if (slash == std::string::npos || slash == 0 || slash + 1 >= coordinate.size()) {
            throw std::runtime_error("module '" + importer.identity + "' line " +
                                     std::to_string(spec.line) +
                                     ": pkg: import must use pkg:<dependency>/<module>.emoji");
        }
        const std::string dependency_name = coordinate.substr(0, slash);
        const std::string module_path_text = coordinate.substr(slash + 1);
        const std::filesystem::path module_path(module_path_text);
        if (module_path.empty() || module_path.is_absolute() || module_path.extension() != ".emoji") {
            throw std::runtime_error("module '" + importer.identity + "' line " +
                                     std::to_string(spec.line) +
                                     ": pkg: import must target a relative .emoji source file");
        }

        const auto* importer_package = package(importer.package_name);
        if (!importer_package) {
            throw std::runtime_error("internal error: importer package is not in the resolved graph");
        }
        if (std::find(importer_package->dependencies.begin(), importer_package->dependencies.end(),
                      dependency_name) == importer_package->dependencies.end()) {
            throw std::runtime_error("module '" + importer.identity + "' line " +
                                     std::to_string(spec.line) + ": package '" +
                                     importer.package_name + "' does not declare direct dependency '" +
                                     dependency_name + "'");
        }

        const auto* dependency = package(dependency_name);
        if (!dependency) {
            throw std::runtime_error("internal error: declared dependency '" + dependency_name +
                                     "' is absent from the resolved package graph");
        }
        const auto candidate = dependency->root / module_path;
        if (!std::filesystem::exists(candidate)) {
            throw std::runtime_error("module '" + importer.identity + "' line " +
                                     std::to_string(spec.line) + ": package module '" +
                                     spec.requested + "' does not exist");
        }
        if (!std::filesystem::is_regular_file(candidate)) {
            throw std::runtime_error("module '" + importer.identity + "' line " +
                                     std::to_string(spec.line) + ": package module '" +
                                     spec.requested + "' is not a regular file");
        }
        const auto canonical = std::filesystem::canonical(candidate);
        if (!within(dependency->root, canonical)) {
            throw std::runtime_error("module '" + importer.identity + "' line " +
                                     std::to_string(spec.line) + ": pkg: import escapes dependency '" +
                                     dependency_name + "' root");
        }
        const auto* owner = owner_for_path(canonical);
        if (!owner || owner->name != dependency_name) {
            const std::string nested = owner ? owner->name : std::string("unknown");
            throw std::runtime_error("module '" + importer.identity + "' line " +
                                     std::to_string(spec.line) + ": pkg: import through '" +
                                     dependency_name + "' targets nested package '" + nested +
                                     "'; import that package through its own declared coordinate");
        }
        return {canonical, dependency_name, module_identity(dependency_name, canonical)};
    }

    std::string visit_standard(const std::string& specifier,
                               std::vector<std::string>& stack) {
        const auto source = standard_module_source(specifier);
        if (!source) throw std::runtime_error("unknown standard module '" + specifier + "'");
        const std::string identity = specifier;
        if (auto state = states_.find(identity); state != states_.end()) {
            if (state->second == VisitState::Done) return identity;
            cycle_error(identity, stack);
        }

        states_[identity] = VisitState::Visiting;
        stack.push_back(identity);

        ModuleUnit unit;
        unit.identity = identity;
        unit.program = parse_text(std::string(*source), registry_, identity);
        analyze_unit(unit);
        register_unit(std::move(unit));

        std::unordered_set<std::string> direct;
        const std::size_t import_count = units_.at(identity).imports.size();
        for (std::size_t index = 0; index < import_count; ++index) {
            const ImportSpec spec = units_.at(identity).imports[index];
            if (spec.requested.rfind("std:", 0) != 0) {
                throw std::runtime_error("standard module '" + identity + "' line " +
                                         std::to_string(spec.line) +
                                         ": standard modules may import only std:<module> sources");
            }
            if (!direct.insert(spec.requested).second) {
                throw std::runtime_error("standard module '" + identity + "' line " +
                                         std::to_string(spec.line) +
                                         ": duplicate 🔗 import of '" + spec.requested + "'");
            }
            units_.at(identity).imports[index].identity = visit_standard(spec.requested, stack);
        }

        stack.pop_back();
        states_[identity] = VisitState::Done;
        order_.push_back(identity);
        return identity;
    }

    std::string visit(const std::filesystem::path& path,
                      const std::string& package_name,
                      std::vector<std::string>& stack) {
        const auto canonical = std::filesystem::canonical(path);
        require_owned_path(package_name, canonical, "entry/import path");
        const std::string identity = module_identity(package_name, canonical);
        if (auto state = states_.find(identity); state != states_.end()) {
            if (state->second == VisitState::Done) return identity;
            cycle_error(identity, stack);
        }

        states_[identity] = VisitState::Visiting;
        stack.push_back(identity);

        ModuleUnit unit;
        unit.path = canonical;
        unit.package_root = package_root(package_name);
        unit.package_name = package_name;
        unit.identity = identity;
        unit.program = parse_source(canonical, registry_, identity, source_provider_);
        analyze_unit(unit);
        register_unit(std::move(unit));

        std::unordered_set<std::string> direct;
        const std::size_t import_count = units_.at(identity).imports.size();
        for (std::size_t index = 0; index < import_count; ++index) {
            const ImportSpec spec = units_.at(identity).imports[index];
            if (spec.requested.rfind("std:", 0) == 0) {
                if (!direct.insert(spec.requested).second) {
                    throw std::runtime_error("module '" + identity + "' line " +
                                             std::to_string(spec.line) +
                                             ": duplicate 🔗 import of '" + spec.requested + "'");
                }
                units_.at(identity).imports[index].identity = visit_standard(spec.requested, stack);
                continue;
            }

            ResolvedSourceImport resolved;
            if (spec.requested.rfind("pkg:", 0) == 0) {
                resolved = resolve_package_import(units_.at(identity), spec);
            } else {
                const auto dependency_path = resolve_local_import(units_.at(identity), spec);
                resolved = {dependency_path, package_name,
                            module_identity(package_name, dependency_path)};
            }

            if (!direct.insert(resolved.identity).second) {
                throw std::runtime_error("module '" + identity + "' line " +
                                         std::to_string(spec.line) +
                                         ": duplicate 🔗 import of '" + resolved.identity + "'");
            }
            units_.at(identity).imports[index].identity =
                visit(resolved.path, resolved.package_name, stack);
        }

        stack.pop_back();
        states_[identity] = VisitState::Done;
        order_.push_back(identity);
        return identity;
    }

    void build_import_bindings() {
        for (const auto& identity : order_) {
            auto& unit = units_.at(identity);
            for (const auto& spec : unit.imports) {
                const auto& dependency = units_.at(spec.identity);
                for (const auto& exported : dependency.exports) {
                    if (dependency.declared_globals.contains(exported)) {
                        if (unit.declared_globals.contains(exported)) {
                            throw std::runtime_error("module '" + unit.identity +
                                                     "': imported variable collides with a module-local declaration");
                        }
                        const auto [_, inserted] = unit.imported_globals.emplace(
                            exported, internal_name(dependency, exported));
                        if (!inserted) {
                            throw std::runtime_error("module '" + unit.identity +
                                                     "': multiple imports export the same emoji variable");
                        }
                    }
                    if (dependency.functions.contains(exported)) {
                        if (unit.functions.contains(exported)) {
                            throw std::runtime_error("module '" + unit.identity +
                                                     "': imported function collides with a module-local declaration");
                        }
                        const auto [_, inserted] = unit.imported_functions.emplace(
                            exported, internal_name(dependency, exported));
                        if (!inserted) {
                            throw std::runtime_error("module '" + unit.identity +
                                                     "': multiple imports export the same emoji function");
                        }
                    }
                }
            }
        }
    }

    std::filesystem::path root_;
    CustomEmojiRegistry registry_;
    std::optional<PackageGraph> package_graph_;
    SourceProvider source_provider_;
    std::unordered_map<std::string, ModuleUnit> units_;
    std::unordered_map<std::string, VisitState> states_;
    std::unordered_map<std::string, std::string> module_names_;
    std::vector<std::string> order_;
};

} // namespace

Chunk compile_file(const std::filesystem::path& raw_entry,
                   CustomEmojiRegistry registry,
                   std::filesystem::path raw_root,
                   SourceProvider source_provider) {
    if (!std::filesystem::exists(raw_entry)) {
        throw std::runtime_error("entry source does not exist: " + raw_entry.string());
    }
    if (!std::filesystem::is_regular_file(raw_entry)) {
        throw std::runtime_error("entry source is not a regular file: " + raw_entry.string());
    }
    const auto entry = std::filesystem::canonical(raw_entry);
    auto root = raw_root.empty() ? discover_root(entry) : std::move(raw_root);
    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        throw std::runtime_error("module root is not a directory: " + root.string());
    }
    root = std::filesystem::canonical(root);
    if (!within(root, entry)) throw std::runtime_error("entry source escapes the module root");

    const std::string identity = identity_for(root, entry);
    ast::Program entry_program = parse_source(entry, registry, identity, source_provider);
    if (!has_module_syntax(entry_program)) {
        Compiler compiler;
        return compiler.compile(entry_program);
    }

    std::optional<PackageGraph> package_graph;
    if (std::filesystem::is_regular_file(root / "emojineer.toml")) {
        const auto manifest = load_project_manifest(root / "emojineer.toml");
        package_graph = resolve_package_graph(root, manifest, package_store_root(root), true);
    }

    ModuleLinker linker(root, std::move(registry), std::move(package_graph), std::move(source_provider));
    return linker.compile(entry);
}

} // namespace emojineer
