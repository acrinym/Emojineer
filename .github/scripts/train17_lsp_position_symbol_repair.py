from pathlib import Path
import re


def clean_ws(text: str) -> str:
    had_newline = text.endswith("\n")
    cleaned = "\n".join(line.rstrip() for line in text.splitlines())
    return cleaned + ("\n" if had_newline else "")


def function_region(text: str, start: str, end: str):
    a = text.index(start)
    b = text.index(end, a)
    return a, b, text[a:b]


def replace_function_region(text: str, start: str, end: str, transform):
    a, b, region = function_region(text, start, end)
    return text[:a] + transform(region) + text[b:]


def reject_invalid_utf16(region: str, empty_result: str) -> str:
    marker = "auto graphemeCol = utf16ColumnToGraphemeColumn(lineStr, utf16Char);"
    if marker not in region:
        raise SystemExit(f"missing UTF-16 conversion marker in {region.splitlines()[0]}")
    if "if (!graphemeCol)" not in region:
        region = region.replace(
            marker,
            marker + f"\n    if (!graphemeCol) return {empty_result};",
            1,
        )
    region = region.replace(
        "internalPos.character = graphemeCol.value_or(utf16Char);",
        "internalPos.character = *graphemeCol;",
        1,
    )
    if "graphemeCol.value_or(utf16Char)" in region:
        raise SystemExit(f"stale UTF-16 fallback remains in {region.splitlines()[0]}")
    return region


path = Path("src/lsp.cpp")
text = path.read_text()

# Semantic request positions arrive as UTF-16; the internal semantic model uses
# grapheme columns. Reject positions that land inside a surrogate pair.
text = replace_function_region(
    text,
    "JsonValue LanguageServer::handleHover",
    "std::optional<Hover> LanguageServer::getHover",
    lambda r: reject_invalid_utf16(r, "JsonValue(nullptr)"),
)
text = replace_function_region(
    text,
    "JsonValue LanguageServer::handleCompletion",
    "std::vector<CompletionItem> LanguageServer::getCompletions",
    lambda r: reject_invalid_utf16(r, "JsonValue(json::makeArray())"),
)
text = replace_function_region(
    text,
    "JsonValue LanguageServer::handleDefinition",
    "JsonValue LanguageServer::handleReferences",
    lambda r: reject_invalid_utf16(r, "JsonValue(json::makeArray())"),
)
text = replace_function_region(
    text,
    "JsonValue LanguageServer::handleReferences",
    "JsonValue LanguageServer::handleDocumentSymbol",
    lambda r: reject_invalid_utf16(r, "JsonValue(json::makeArray())"),
)

# PackageGraph contains the complete resolved graph. It is path/edge authority,
# not permission to expose transitives ambiently to the workspace root.
helper_marker = "std::vector<SymbolLocation> LanguageServer::findDefinitions"
helper_code = r'''static bool pathHasPrefix(const std::filesystem::path& candidate,
                          const std::filesystem::path& root) {
    auto c = candidate.lexically_normal();
    auto r = root.lexically_normal();
    auto ci = c.begin();
    for (auto ri = r.begin(); ri != r.end(); ++ri, ++ci) {
        if (ci == c.end() || *ci != *ri) return false;
    }
    return true;
}

static const ResolvedPackage* packageOwningPath(const PackageGraph& graph,
                                                const std::filesystem::path& candidate) {
    const ResolvedPackage* owner = nullptr;
    std::size_t bestLength = 0;
    for (const auto& pkg : graph.packages) {
        if (pkg.root.empty()) continue;
        auto normalizedRoot = pkg.root.lexically_normal();
        if (pathHasPrefix(candidate, normalizedRoot)) {
            auto length = normalizedRoot.native().size();
            if (!owner || length > bestLength) {
                owner = &pkg;
                bestLength = length;
            }
        }
    }
    return owner;
}

static std::set<std::filesystem::path> visibleSourceRoots(
    const std::filesystem::path& workspaceRoot,
    const std::optional<ProjectManifest>& manifest,
    const PackageGraph& graph,
    const std::filesystem::path& requesterPath) {

    std::set<std::filesystem::path> roots;
    if (const auto* owner = packageOwningPath(graph, requesterPath)) {
        roots.insert(owner->root.lexically_normal());
        for (const auto& dependencyName : owner->dependencies) {
            if (const auto* dependency = graph.find(dependencyName)) {
                if (!dependency->root.empty()) {
                    roots.insert(dependency->root.lexically_normal());
                }
            }
        }
        return roots;
    }

    roots.insert(workspaceRoot.lexically_normal());
    if (manifest) {
        for (const auto& dependency : manifest->dependencies) {
            if (const auto* resolved = graph.find(dependency.name)) {
                if (!resolved->root.empty()) {
                    roots.insert(resolved->root.lexically_normal());
                }
            }
        }
    }
    return roots;
}

static std::set<std::string> visibleDependencyNames(
    const std::optional<ProjectManifest>& manifest,
    const PackageGraph& graph,
    const std::filesystem::path& requesterPath) {

    std::set<std::string> names;
    if (const auto* owner = packageOwningPath(graph, requesterPath)) {
        names.insert(owner->dependencies.begin(), owner->dependencies.end());
        return names;
    }

    if (manifest) {
        for (const auto& dependency : manifest->dependencies) {
            names.insert(dependency.name);
        }
    }
    return names;
}

static bool sourcePathIsVisible(
    const std::filesystem::path& candidate,
    const std::filesystem::path& workspaceRoot,
    const PackageGraph& graph,
    const std::set<std::filesystem::path>& visibleRoots) {

    if (const auto* owner = packageOwningPath(graph, candidate)) {
        return visibleRoots.count(owner->root.lexically_normal()) != 0;
    }

    if (!pathHasPrefix(candidate, workspaceRoot)) return false;
    auto relative = candidate.lexically_normal().lexically_relative(workspaceRoot.lexically_normal());
    if (!relative.empty()) {
        auto first = relative.begin();
        if (first != relative.end() && first->string() == ".emojineer") return false;
    }
    return true;
}

'''
if "static std::set<std::filesystem::path> visibleSourceRoots(" not in text:
    text = text.replace(helper_marker, helper_code + helper_marker, 1)

# Prevent already-open package files from bypassing the same requester boundary.
open_doc_guard_old = "        if (otherUri == uri) continue;  // Skip current document\n"
open_doc_guard_new = r'''        if (otherUri == uri) continue;  // Skip current document
        if (workspaceRoot_ && packageGraph_) {
            auto visibleRoots = visibleSourceRoots(
                *workspaceRoot_, manifest_, *packageGraph_, std::filesystem::path(uriToPath(uri)));
            if (!sourcePathIsVisible(std::filesystem::path(uriToPath(otherUri)),
                                     *workspaceRoot_, *packageGraph_, visibleRoots)) {
                continue;
            }
        }
'''
count = text.count(open_doc_guard_old)
if count:
    text = text.replace(open_doc_guard_old, open_doc_guard_new)
elif text.count("auto visibleRoots = visibleSourceRoots(") < 3:
    raise SystemExit("open-document visibility guard anchor mismatch")


def scope_authorized_paths(region: str, requester_expr: str) -> str:
    pattern = re.compile(
        r"        std::set<std::filesystem::path> authorizedPaths;\n"
        r".*?"
        r"        for \(const auto& authorizedPath : authorizedPaths\) \{",
        re.S,
    )
    replacement = (
        "        auto authorizedPaths = visibleSourceRoots(\n"
        f"            root, manifest_, *packageGraph_, {requester_expr});\n\n"
        "        for (const auto& authorizedPath : authorizedPaths) {"
    )
    new_region, n = pattern.subn(replacement, region, count=1)
    if n != 1:
        if "auto authorizedPaths = visibleSourceRoots(" in region:
            return region
        raise SystemExit(f"authorized source universe anchor mismatch in {region.splitlines()[0]}")
    return new_region


text = replace_function_region(
    text,
    "std::vector<SymbolLocation> LanguageServer::findDefinitions",
    "// Helper to find references in expressions - forward declaration",
    lambda r: scope_authorized_paths(r, "std::filesystem::path(uriToPath(uri))"),
)
text = replace_function_region(
    text,
    "std::vector<SymbolLocation> LanguageServer::findReferences",
    "JsonValue LanguageServer::handleCompletion",
    lambda r: scope_authorized_paths(r, "std::filesystem::path(uriToPath(uri))"),
)
text = replace_function_region(
    text,
    "std::vector<CompletionItem> LanguageServer::getCompletions",
    "JsonValue LanguageServer::handleDefinition",
    lambda r: scope_authorized_paths(r, "std::filesystem::path(uriToPath(uri))"),
)
text = replace_function_region(
    text,
    "std::vector<SymbolInformation> LanguageServer::getWorkspaceSymbols",
    "JsonValue LanguageServer::handleFormatting",
    lambda r: scope_authorized_paths(r, "root"),
)


def scope_completion_packages(region: str) -> str:
    start = region.find("    // Add direct dependencies from PackageGraph")
    end = region.find("    // Add user-defined symbols from current document", start)
    if start < 0 or end < 0:
        if "auto dependencyNames = visibleDependencyNames(" in region:
            return region
        raise SystemExit("completion dependency block anchors missing")
    replacement = r'''    // Add only dependencies visible from the requesting package/root.
    if (packageGraph_ && workspaceRoot_) {
        auto dependencyNames = visibleDependencyNames(
            manifest_, *packageGraph_, std::filesystem::path(uriToPath(uri)));
        for (const auto& dependencyName : dependencyNames) {
            const auto* pkg = packageGraph_->find(dependencyName);
            if (!pkg || !matchesPrefix(pkg->name)) continue;

            CompletionItem item;
            item.label = pkg->name;
            if (pkg->source_kind == DependencyKind::Path) {
                item.detail = "path package";
            } else {
                item.detail = "registry package";
            }
            item.kind = static_cast<int>(CompletionItemKind::Module);
            completions.push_back(item);
        }
    }

'''
    return region[:start] + replacement + region[end:]


def fix_completion_prefix(region: str) -> str:
    start = region.find("    // Get the prefix at the current position for filtering")
    end = region.find("    // Helper to filter by prefix", start)
    if start < 0 or end < 0:
        if "prefixGraphemes" in region:
            return region
        raise SystemExit("completion prefix block anchors missing")
    replacement = r'''    // Get the lexical prefix at the current grapheme position.
    auto doc = getDocument(uri);
    std::string prefix;
    if (doc) {
        std::string lineText = getLine(doc->text, pos.line);
        auto lineGraphemes = segment_graphemes(lineText);
        if (pos.character <= lineGraphemes.size()) {
            std::string beforeCursor;
            for (std::size_t i = 0; i < pos.character; ++i) {
                beforeCursor += lineGraphemes[i].display;
            }

            auto prefixGraphemes = segment_graphemes(beforeCursor);
            std::size_t begin = prefixGraphemes.size();
            while (begin > 0) {
                const auto& g = prefixGraphemes[begin - 1].display;
                bool delimiter = g.empty();
                if (!delimiter) {
                    delimiter = std::all_of(g.begin(), g.end(), [](unsigned char ch) {
                        return std::isspace(ch) != 0;
                    });
                }
                if (!delimiter && g.size() == 1) {
                    const char ch = g[0];
                    delimiter = ch == '(' || ch == ')' || ch == '[' || ch == ']' ||
                                ch == '{' || ch == '}' || ch == ',' || ch == ':' ||
                                ch == ';' || ch == '=' || ch == '+' || ch == '-' ||
                                ch == '*' || ch == '/' || ch == '<' || ch == '>';
                }
                if (delimiter) break;
                --begin;
            }
            for (std::size_t i = begin; i < prefixGraphemes.size(); ++i) {
                prefix += prefixGraphemes[i].display;
            }
        }
    }

'''
    return region[:start] + replacement + region[end:]


def completion_transform(region: str) -> str:
    return scope_completion_packages(fix_completion_prefix(region))


text = replace_function_region(
    text,
    "std::vector<CompletionItem> LanguageServer::getCompletions",
    "JsonValue LanguageServer::handleDefinition",
    completion_transform,
)

# Canonical document-symbol ranges come directly from tokenToRange (UTF-16).
new_symbols = r'''std::vector<DocumentSymbol> LanguageServer::getDocumentSymbols(const std::string& uri) {
    std::vector<DocumentSymbol> symbols;

    auto doc = getDocument(uri);
    if (!doc) return symbols;

    auto programOpt = getOrParseProgram(uri);
    auto tokensOpt = getTokens(uri);
    if (!programOpt || !tokensOpt) return symbols;
    const auto& program = programOpt->get();
    const auto& tokens = tokensOpt->get();

    auto declarationRange = [&](std::size_t line, const std::string& name) -> std::optional<Range> {
        for (const auto& token : tokens) {
            if (token.line == line && token.kind == TokenKind::Identifier &&
                (token.lexeme == name || token.canonical == name)) {
                return tokenToRange(doc->text, token);
            }
        }
        return std::nullopt;
    };

    for (const auto& stmt : program.statements) {
        if (auto* funcDecl = dynamic_cast<const ast::FunctionDecl*>(stmt.get())) {
            auto exact = declarationRange(stmt->line, funcDecl->name);
            if (!exact) continue;
            DocumentSymbol sym;
            sym.name = funcDecl->name;
            sym.kind = static_cast<int>(SymbolKind::Function);
            sym.range = *exact;
            sym.selectionRange = *exact;
            sym.detail = "function";
            symbols.push_back(std::move(sym));
        } else if (auto* varDecl = dynamic_cast<const ast::VarDecl*>(stmt.get())) {
            auto exact = declarationRange(stmt->line, varDecl->name);
            if (!exact) continue;
            DocumentSymbol sym;
            sym.name = varDecl->name;
            sym.kind = static_cast<int>(SymbolKind::Variable);
            sym.range = *exact;
            sym.selectionRange = *exact;
            symbols.push_back(std::move(sym));
        } else if (auto* modDecl = dynamic_cast<const ast::ModuleDecl*>(stmt.get())) {
            auto exact = declarationRange(stmt->line, modDecl->name);
            if (!exact) continue;
            DocumentSymbol sym;
            sym.name = modDecl->name;
            sym.kind = static_cast<int>(SymbolKind::Module);
            sym.range = *exact;
            sym.selectionRange = *exact;
            sym.detail = "module";
            symbols.push_back(std::move(sym));
        }
    }

    return symbols;
}

'''
a = text.index("std::vector<DocumentSymbol> LanguageServer::getDocumentSymbols")
b = text.index("JsonValue LanguageServer::handleWorkspaceSymbol", a)
text = text[:a] + new_symbols + text[b:]


def fix_workspace_token_ranges(region: str) -> str:
    # The workspace-symbol response is serialized directly, so these ranges
    # must already be UTF-16 LSP ranges.
    pattern = re.compile(
        r"                                                    Range r;\n"
        r"                                                    r\.start = \{.*?\};\n"
        r"                                                    auto gs = segment_graphemes\(token\.lexeme\);\n"
        r"                                                    r\.end = \{.*?\};\n"
        r"                                                    info\.location = \{moduleUri, r\};",
        re.S,
    )
    region, n = pattern.subn(
        "                                                    info.location = {moduleUri, tokenToRange(source, token)};",
        region,
    )
    if n == 0 and "tokenToRange(source, token)" not in region:
        raise SystemExit("workspace symbol token-range anchors missing")
    return region


text = replace_function_region(
    text,
    "std::vector<SymbolInformation> LanguageServer::getWorkspaceSymbols",
    "JsonValue LanguageServer::handleFormatting",
    fix_workspace_token_ranges,
)


def guard_workspace_open_docs(region: str) -> str:
    anchor = "    for (const auto& [uri, doc] : openDocuments_) {\n"
    if anchor in region and "workspaceVisibleRoots" not in region:
        replacement = r'''    std::set<std::filesystem::path> workspaceVisibleRoots;
    if (workspaceRoot_ && packageGraph_) {
        workspaceVisibleRoots = visibleSourceRoots(
            *workspaceRoot_, manifest_, *packageGraph_, *workspaceRoot_);
    }

    for (const auto& [uri, doc] : openDocuments_) {
        if (workspaceRoot_ && packageGraph_ &&
            !sourcePathIsVisible(std::filesystem::path(uriToPath(uri)),
                                 *workspaceRoot_, *packageGraph_, workspaceVisibleRoots)) {
            continue;
        }
'''
        region = region.replace(anchor, replacement, 1)
    return region


text = replace_function_region(
    text,
    "std::vector<SymbolInformation> LanguageServer::getWorkspaceSymbols",
    "JsonValue LanguageServer::handleFormatting",
    guard_workspace_open_docs,
)

# Range formatting is deliberately not a Train 17 surface.
text = text.replace(
    '    if (method == "textDocument/rangeFormatting") return handleRangeFormatting(params);\n',
    "",
)

if "graphemeCol.value_or(utf16Char)" in text:
    raise SystemExit("stale semantic UTF-16 fallback remains")
if 'if (method == "textDocument/rangeFormatting")' in text:
    raise SystemExit("rangeFormatting dispatch remains")
if "constexpr std::uint32_t estimatedColumn = 2;" in text:
    raise SystemExit("approximate document symbol range remains")

path.write_text(clean_ws(text))
print("repaired: exact UTF-16 positions/symbols, requester-scoped PackageGraph visibility, completion prefix, rangeFormatting dispatch")
