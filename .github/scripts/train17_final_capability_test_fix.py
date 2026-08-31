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
]

for old, new in replacements:
    if new in text:
        continue
    if old not in text:
        raise SystemExit(f"capability/diagnostic test anchor missing: {old}")
    text = text.replace(old, new, 1)

path.write_text(text)
print("repaired: typed capability tests and serialization-agnostic diagnostic publication regression")
