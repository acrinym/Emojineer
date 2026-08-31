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

# JSON may legally escape solidus. Do not make protocol tests depend on one spelling.
method_pattern = re.compile(
    r'(?P<expr>[A-Za-z_][A-Za-z0-9_]*)\.find\("textDocument/publishDiagnostics"\) != std::string::npos'
)
text, method_count = method_pattern.subn(
    lambda m: (
        f'{m.group("expr")}.find("textDocument") != std::string::npos &&\n'
        f'           {m.group("expr")}.find("publishDiagnostics") != std::string::npos'
    ),
    text,
)
if 'find("textDocument/publishDiagnostics")' in text:
    raise SystemExit("remaining brittle publishDiagnostics JSON slash assertion")

uri_pattern = re.compile(
    r'(?P<expr>[A-Za-z_][A-Za-z0-9_]*)\.find\("file:///test/main\.emoji"\) != std::string::npos'
)
text, uri_count = uri_pattern.subn(
    lambda m: (
        f'({m.group("expr")}.find("file:///test/main.emoji") != std::string::npos ||\n'
        f'            {m.group("expr")}.find("file:\\\/\\\/\\\/test\\\/main.emoji") != std::string::npos)'
    ),
    text,
)
if '.find("file:///test/main.emoji") != std::string::npos' in text:
    raise SystemExit("remaining brittle file URI solidus assertion")

# Rewrite the source ONLY inside the strict supplementary-diagnostic E2E.
function_start = text.find("void test_e2e_real_supplementary_emoji_diagnostics()")
if function_start == -1:
    raise SystemExit("supplementary diagnostic E2E function missing")
function_end = text.find("#endif // test_e2e_real_supplementary_emoji_diagnostics", function_start)
if function_end == -1:
    raise SystemExit("supplementary diagnostic E2E end marker missing")
function = text[function_start:function_end]
old_source = '"text":"🐍 🍎 🔢 🟰 42"'
new_source = '"text":"🐍 🍎 🔢 🟰 42 @"'
if new_source not in function:
    if old_source not in function:
        raise SystemExit("strict supplementary diagnostic source fixture missing")
    function = function.replace(old_source, new_source, 1)

new_range_block = '''    // The invalid '@' follows four supplementary-plane emoji. In UTF-16 its exact
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

    // Clean shutdown'''
if "Diagnostic must start at UTF-16 character 15" not in function:
    range_pattern = re.compile(
        r'    // The diagnostic should cover the whole expression, with positions accounting for\n.*?    // Clean shutdown',
        re.DOTALL,
    )
    function, range_count = range_pattern.subn(new_range_block, function, count=1)
    if range_count != 1:
        raise SystemExit(f"strict supplementary diagnostic range block matches: {range_count}")
text = text[:function_start] + function + text[function_end:]

# Keep generated edits patch-clean.
had_final_newline = text.endswith("\n")
text = "\n".join(line.rstrip() for line in text.splitlines())
if had_final_newline:
    text += "\n"
path.write_text(text)
print(f"repaired: typed capability model, {method_count} method assertion(s), {uri_count} URI assertion(s), and targeted supplementary diagnostic E2E")
