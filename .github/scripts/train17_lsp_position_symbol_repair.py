from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def replace_region(text: str, start: str, end: str, replacement: str) -> str:
    a = text.index(start)
    b = text.index(end, a)
    return text[:a] + replacement + text[b:]


def clean_ws(text: str) -> str:
    had_newline = text.endswith("\n")
    cleaned = "\n".join(line.rstrip() for line in text.splitlines())
    return cleaned + ("\n" if had_newline else "")


path = Path("src/lsp.cpp")
text = path.read_text()

# Invalid UTF-16 positions, including a low-surrogate position, are protocol-invalid
# locations for semantic queries. Do not reinterpret the UTF-16 column as a grapheme index.
text = replace_once(
    text,
    '''    auto graphemeCol = utf16ColumnToGraphemeColumn(lineStr, utf16Char);\n\n    // Convert to 1-indexed for lexer (line is 0-indexed in LSP, column is UTF-16 units)\n    Position internalPos;\n    internalPos.line = line;  // LSP uses 0-indexed lines\n    internalPos.character = graphemeCol.value_or(utf16Char);  // Fallback to UTF-16 if conversion fails\n''',
    '''    auto graphemeCol = utf16ColumnToGraphemeColumn(lineStr, utf16Char);\n    if (!graphemeCol) return JsonValue(nullptr);\n\n    Position internalPos;\n    internalPos.line = line;\n    internalPos.character = *graphemeCol;\n''',
    "hover invalid UTF-16 rejection")

text = replace_once(
    text,
    '''    auto graphemeCol = utf16ColumnToGraphemeColumn(lineStr, utf16Char);\n    \n    // Convert to internal position\n    Position internalPos;\n    internalPos.line = line;\n    internalPos.character = graphemeCol.value_or(utf16Char);\n    \n    auto completions = getCompletions(uri, internalPos);\n''',
    '''    auto graphemeCol = utf16ColumnToGraphemeColumn(lineStr, utf16Char);\n    if (!graphemeCol) return json::makeArray();\n\n    Position internalPos;\n    internalPos.line = line;\n    internalPos.character = *graphemeCol;\n\n    auto completions = getCompletions(uri, internalPos);\n''',
    "completion invalid UTF-16 rejection")

text = replace_once(
    text,
    '''    auto graphemeCol = utf16ColumnToGraphemeColumn(lineStr, utf16Char);\n    \n    Position internalPos;\n    internalPos.line = line;\n    internalPos.character = graphemeCol.value_or(utf16Char);\n    \n    auto defs = findDefinitions(uri, internalPos);\n''',
    '''    auto graphemeCol = utf16ColumnToGraphemeColumn(lineStr, utf16Char);\n    if (!graphemeCol) return json::makeArray();\n\n    Position internalPos;\n    internalPos.line = line;\n    internalPos.character = *graphemeCol;\n\n    auto defs = findDefinitions(uri, internalPos);\n''',
    "definition invalid UTF-16 rejection")

text = replace_once(
    text,
    '''    auto graphemeCol = utf16ColumnToGraphemeColumn(lineStr, utf16Char);\n    \n    Position internalPos;\n    internalPos.line = line;\n    internalPos.character = graphemeCol.value_or(utf16Char);\n    \n    auto refs = findReferences(uri, internalPos, includeDeclaration);\n''',
    '''    auto graphemeCol = utf16ColumnToGraphemeColumn(lineStr, utf16Char);\n    if (!graphemeCol) return json::makeArray();\n\n    Position internalPos;\n    internalPos.line = line;\n    internalPos.character = *graphemeCol;\n\n    auto refs = findReferences(uri, internalPos, includeDeclaration);\n''',
    "references invalid UTF-16 rejection")

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
text = replace_region(
    text,
    "std::vector<DocumentSymbol> LanguageServer::getDocumentSymbols",
    "JsonValue LanguageServer::handleWorkspaceSymbol",
    new_symbols)

# Range formatting is intentionally not a Train 17 capability. With the capability false,
# dispatching it anyway is contradictory and makes unqualified behavior observable.
text = replace_once(
    text,
    '    if (method == "textDocument/rangeFormatting") return handleRangeFormatting(params);\n',
    '',
    "rangeFormatting request dispatch removal")

path.write_text(clean_ws(text))
print("repaired: invalid UTF-16 rejection, exact document symbols, rangeFormatting dispatch")
