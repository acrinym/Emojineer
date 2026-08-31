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

# The old supplementary-plane E2E fixture used a valid declaration and then
# expected a diagnostic anyway. Make the final grapheme deliberately invalid so
# the test proves exact UTF-16 range conversion after four supplementary emoji.
old_source = '"text":"🐍 🍎 🔢 🟰 42"'
new_source = '"text":"🐍 🍎 🔢 🟰 42 @"'
if new_source not in text:
    if old_source not in text:
        raise SystemExit("supplementary diagnostic fixture anchor missing")
    text = text.replace(old_source, new_source, 1)

old_range_comment = '''    // The diagnostic should cover the whole expression, with positions accounting for
    // the 2 UTF-16 units per supplementary-plane emoji
    assert(body.find("\\\"range\\\"") != std::string::npos &&
           "Diagnostic must have range");

    // The start character should be 0 (beginning of document)
    // The end character should account for all the supplementary emojis
    // Grapheme count: 10 (🐍, space, 🍎, space, 🔢, space, 🟰, space, 4, 2)
    // UTF-16 count: 14 (🐍=2, space=1, 🍎=2, space=1, 🔢=2, space=1, 🟰=2, space=1, 4=1, 2=1 = 14)
    // We verify the positions are in UTF-16, not byte offsets or grapheme columns
'''
new_range_comment = '''    // The invalid '@' follows four supplementary-plane emoji. In UTF-16 its exact
    // range is [15, 16): four emoji contribute 8 code units; five spaces plus two
    // digits before '@' contribute the remaining 7. This proves the server is not
    // reporting a byte offset or grapheme column.
    assert(body.find("\\\"range\\\"") != std::string::npos &&
           "Diagnostic must have range");
    assert(body.find("\\\"character\\\":15") != std::string::npos &&
           "Diagnostic must start at UTF-16 character 15");
    assert(body.find("\\\"character\\\":16") != std::string::npos &&
           "Diagnostic must end at UTF-16 character 16");
    assert(body.find("unexpected grapheme") != std::string::npos &&
           "Diagnostic must come from the intentionally invalid grapheme");
'''
if new_range_comment not in text:
    if old_range_comment not in text:
        raise SystemExit("supplementary diagnostic range assertion block missing")
    text = text.replace(old_range_comment, new_range_comment, 1)

# Keep generated edits patch-clean even when the replaced source had spaces
# before its original continuation/newline.
had_final_newline = text.endswith("\n")
text = "\n".join(line.rstrip() for line in text.splitlines())
if had_final_newline:
    text += "\n"

path.write_text(text)
print(f"repaired: typed capability model, {count} publishDiagnostics assertion(s), and exact supplementary diagnostic E2E")
