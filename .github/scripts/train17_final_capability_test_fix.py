from pathlib import Path

path = Path("tests/lsp_tests.cpp")
text = path.read_text()

replacements = [
    (
        "    caps.textDocumentSync = true;",
        "    caps.textDocumentSync = TextDocumentSyncKind::Full;",
    ),
    (
        "    caps.completionProvider = true;",
        "    caps.completionProvider = CompletionOptions{false};",
    ),
    (
        "    assert(*caps.textDocumentSync == true);",
        "    assert(*caps.textDocumentSync == TextDocumentSyncKind::Full);",
    ),
    (
        "    assert(*caps.completionProvider == true);",
        "    assert(caps.completionProvider.has_value());\n    assert(!caps.completionProvider->resolveProvider);",
    ),
    (
        '    assert(framed.find("textDocument/publishDiagnostics") != std::string::npos);',
        '    assert(framed.find("textDocument") != std::string::npos &&\n           framed.find("publishDiagnostics") != std::string::npos &&\n           "published notification method must survive JSON serialization");',
    ),
    (
        '    assert(diagResponse.find("textDocument/publishDiagnostics") != std::string::npos &&\n           "Expected publishDiagnostics method");',
        '    assert(diagResponse.find("textDocument") != std::string::npos &&\n           diagResponse.find("publishDiagnostics") != std::string::npos &&\n           "Expected publishDiagnostics method");',
    ),
]

for old, new in replacements:
    if new in text:
        continue
    if old not in text:
        # Some anchors may already have been repaired by an earlier run; only fail for
        # the capability model anchors that must exist on the unqualified branch.
        if old.startswith("    caps.") or old.startswith("    assert(*caps"):
            raise SystemExit(f"capability test anchor missing: {old}")
        continue
    text = text.replace(old, new)

# Guard against any remaining assertion that requires an unescaped JSON slash spelling.
if 'find("textDocument/publishDiagnostics")' in text:
    raise SystemExit("remaining brittle publishDiagnostics JSON slash assertion")

path.write_text(text)
print("repaired: typed capability tests and serialization-agnostic publishDiagnostics assertions")
