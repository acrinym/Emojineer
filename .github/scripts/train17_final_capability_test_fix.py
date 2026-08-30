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
]

for old, new in replacements:
    if new in text:
        continue
    if old not in text:
        raise SystemExit(f"capability test anchor missing: {old}")
    text = text.replace(old, new, 1)

path.write_text(text)
print("repaired: capability tests use typed LSP sync/completion model")
