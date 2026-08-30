from pathlib import Path
import re

lsp_path = Path("src/lsp.cpp")
lsp = lsp_path.read_text()

start = lsp.find("void LanguageServer::publishDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics) {")
end = lsp.find("std::optional<ProjectManifest> LanguageServer::getProjectManifest", start)
if start < 0 or end < 0:
    raise SystemExit("publishDiagnostics replacement anchors not found")

replacement = r'''void LanguageServer::publishDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics) {
    auto params = json::makeObject();
    json::objectSet(params, "uri", JsonValue(uri));

    // Diagnostic::range is already expressed in canonical LSP UTF-16 coordinates.
    // Preserve it byte-for-byte instead of interpreting its character offsets as
    // grapheme columns a second time. This is also required for diagnostics owned
    // by imported sources that are not open as editor overlays.
    auto diagJson = json::makeArray();
    for (const auto& d : diagnostics) {
        auto diag = json::makeObject();

        auto range = json::makeObject();
        auto startPosition = json::makeObject();
        json::objectSet(startPosition, "line", JsonValue(static_cast<double>(d.range.start.line)));
        json::objectSet(startPosition, "character", JsonValue(static_cast<double>(d.range.start.character)));
        auto endPosition = json::makeObject();
        json::objectSet(endPosition, "line", JsonValue(static_cast<double>(d.range.end.line)));
        json::objectSet(endPosition, "character", JsonValue(static_cast<double>(d.range.end.character)));
        json::objectSet(range, "start", startPosition);
        json::objectSet(range, "end", endPosition);
        json::objectSet(diag, "range", range);

        json::objectSet(diag, "severity", JsonValue(static_cast<double>(d.severity)));
        json::objectSet(diag, "message", JsonValue(d.message));
        if (d.source) json::objectSet(diag, "source", JsonValue(*d.source));

        json::arrayPushBack(diagJson, diag);
    }
    json::objectSet(params, "diagnostics", diagJson);

    auto notificationObj = json::makeObject();
    json::objectSet(notificationObj, "jsonrpc", JsonValue(std::string("2.0")));
    json::objectSet(notificationObj, "method", JsonValue(std::string("textDocument/publishDiagnostics")));
    json::objectSet(notificationObj, "params", params);
    std::string jsonBody = toJson(notificationObj);
    std::ostringstream framed;
    framed << "Content-Length: " << jsonBody.size() << "\r\n\r\n" << jsonBody;
    std::cout << framed.str();
    std::cout.flush();
}

'''

lsp = lsp[:start] + replacement + lsp[end:]

if "graphemeColumnToUtf16Column(startLineStr, d.range.start.character)" in lsp:
    raise SystemExit("stale diagnostic range double-conversion remains")

lsp_path.write_text(lsp)

test_path = Path("tests/lsp_tests.cpp")
test = test_path.read_text()
if "test_publish_diagnostics_preserves_canonical_utf16_ranges" not in test:
    main_match = re.search(r"\nint\s+main\s*\([^)]*\)\s*\{", test)
    if not main_match:
        raise SystemExit("test main() anchor not found")

    regression = r'''
// Regression: Diagnostic ranges produced by tokenToRange are already LSP UTF-16
// coordinates and must survive publication unchanged, including for an imported
// source URI that is not open as an editor overlay.
void test_publish_diagnostics_preserves_canonical_utf16_ranges() {
    LanguageServer server;
    const std::string source = "🍎 abc";

    Token token;
    token.kind = TokenKind::Identifier;
    token.line = 1;
    token.column = 3; // Grapheme column after 🍎 + space.
    token.lexeme = "abc";
    token.canonical = "abc";

    Diagnostic diagnostic;
    diagnostic.range = server.tokenToRange(source, token);
    diagnostic.severity = 1;
    diagnostic.message = "utf16 range regression";
    diagnostic.source = "emojineer";

    // 🍎 occupies two UTF-16 code units, then the space occupies one.
    assert(diagnostic.range.start.line == 0);
    assert(diagnostic.range.start.character == 3);
    assert(diagnostic.range.end.line == 0);
    assert(diagnostic.range.end.character == 6);

    std::ostringstream captured;
    auto* original = std::cout.rdbuf(captured.rdbuf());
    server.publishDiagnostics("file:///imported-not-open.emoji", {diagnostic});
    std::cout.rdbuf(original);

    const std::string framed = captured.str();
    assert(framed.find("Content-Length:") == 0);
    assert(framed.find("textDocument/publishDiagnostics") != std::string::npos);
    assert(framed.find("\"character\":3") != std::string::npos &&
           "published diagnostic must preserve UTF-16 start character");
    assert(framed.find("\"character\":6") != std::string::npos &&
           "published diagnostic must preserve UTF-16 end character");
}

'''
    test = test[:main_match.start() + 1] + regression + test[main_match.start() + 1:]

    main_match = re.search(r"int\s+main\s*\([^)]*\)\s*\{", test)
    if not main_match:
        raise SystemExit("test main() disappeared after insertion")
    brace_end = main_match.end()
    test = test[:brace_end] + "\n    test_publish_diagnostics_preserves_canonical_utf16_ranges();" + test[brace_end:]

test_path.write_text(test)
print("repaired: canonical UTF-16 diagnostic ranges are published without double conversion")
