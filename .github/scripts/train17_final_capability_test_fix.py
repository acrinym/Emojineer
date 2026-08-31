from pathlib import Path
import re

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

# JSON permits escaping solidus as `\/`. The production serializer deliberately
# does that, so tests must verify the semantic method name without requiring one
# particular serialized spelling. Normalize every response-variable assertion.
pattern = re.compile(
    r'(?P<expr>[A-Za-z_][A-Za-z0-9_]*)\.find\("textDocument/publishDiagnostics"\) != std::string::npos'
)
text, count = pattern.subn(
    lambda m: (
        f'{m.group("expr")}.find("textDocument") != std::string::npos &&\n'
        f'           {m.group("expr")}.find("publishDiagnostics") != std::string::npos'
    ),
    text,
)

if 'find("textDocument/publishDiagnostics")' in text:
    raise SystemExit("remaining brittle publishDiagnostics JSON slash assertion")

# Keep generated edits patch-clean even when the replaced source had spaces
# before its original continuation/newline.
had_final_newline = text.endswith("\n")
text = "\n".join(line.rstrip() for line in text.splitlines())
if had_final_newline:
    text += "\n"

path.write_text(text)
print(f"repaired: typed capability model and {count} publishDiagnostics assertion(s)")
