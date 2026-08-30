from pathlib import Path

path = Path("src/lsp.cpp")
text = path.read_text()

replacements = [
    (
        "static std::optional<Range> exactIdentifierTokenRange(\n    const std::string& source,",
        "static std::optional<Range> exactIdentifierTokenRange(\n    const LanguageServer& server,\n    const std::string& source,",
    ),
    (
        "            return tokenToRange(source, token);\n        }\n    }\n    return std::nullopt;\n}\n\nstd::vector<SymbolLocation> LanguageServer::findDefinitions",
        "            return server.tokenToRange(source, token);\n        }\n    }\n    return std::nullopt;\n}\n\nstd::vector<SymbolLocation> LanguageServer::findDefinitions",
    ),
    (
        "auto exact = exactIdentifierTokenRange(\n                searchSource, searchTokens, stmt->line, declarationName);",
        "auto exact = exactIdentifierTokenRange(\n                *this, searchSource, searchTokens, stmt->line, declarationName);",
    ),
    (
        "static void collectExactReferences(\n    const std::string& uri,",
        "static void collectExactReferences(\n    const LanguageServer& server,\n    const std::string& uri,",
    ),
    (
        "        loc.range = tokenToRange(source, token);",
        "        loc.range = server.tokenToRange(source, token);",
    ),
    (
        "collectExactReferences(uri, doc->text, programOpt->get(), tokensOpt->get(),",
        "collectExactReferences(*this, uri, doc->text, programOpt->get(), tokensOpt->get(),",
    ),
    (
        "collectExactReferences(otherUri, otherDoc.text, otherProgramOpt->get(),",
        "collectExactReferences(*this, otherUri, otherDoc.text, otherProgramOpt->get(),",
    ),
    (
        "collectExactReferences(moduleUri, source, modProgram, modTokens,",
        "collectExactReferences(*this, moduleUri, source, modProgram, modTokens,",
    ),
]

for old, new in replacements:
    if new in text:
        continue
    if old not in text:
        raise SystemExit(f"compile-fix anchor missing: {old[:80]}")
    text = text.replace(old, new, 1)

segment = text[text.index("static std::optional<Range> exactIdentifierTokenRange"):text.index("JsonValue LanguageServer::handleCompletion")]
if "return tokenToRange(source, token)" in segment or "loc.range = tokenToRange(source, token)" in segment:
    raise SystemExit("unbound exact range helper call remains")

path.write_text(text)
print("repaired: exact semantic range helpers bound to LanguageServer::tokenToRange")
