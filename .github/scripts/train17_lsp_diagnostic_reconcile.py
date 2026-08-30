from pathlib import Path
import subprocess

PARENT = "fb992e03733ac757fe4f3f102b899ab1b5193877"


def git_show(path: str) -> str:
    return subprocess.check_output(["git", "show", f"{PARENT}:{path}"], text=True)


def region(text: str, start: str, end: str) -> str:
    a = text.index(start)
    b = text.index(end, a)
    return text[a:b]


def replace_region(text: str, start: str, end: str, replacement: str) -> str:
    a = text.index(start)
    b = text.index(end, a)
    return text[:a] + replacement + text[b:]


def clean_ws(text: str) -> str:
    had_newline = text.endswith("\n")
    cleaned = "\n".join(line.rstrip() for line in text.splitlines())
    return cleaned + ("\n" if had_newline else "")


parent_h = git_show("include/emojineer/lsp.hpp")
cur_h_path = Path("include/emojineer/lsp.hpp")
cur_h = cur_h_path.read_text()

if "struct DiagnosticResult" not in cur_h:
    insert_at = cur_h.index("// Symbol location for definitions/references")
    diag_struct = region(parent_h, "struct DiagnosticResult", "// Symbol location for definitions/references")
    cur_h = cur_h[:insert_at] + diag_struct + cur_h[insert_at:]

if "using SourceLocationException = ::emojineer::SourceLocationException;" not in cur_h:
    marker = "// LSP server main class"
    alias = region(parent_h, "// Backward compatibility alias", marker)
    cur_h = cur_h.replace(marker, alias + marker, 1)

if "Range tokenToRange(" not in cur_h:
    marker = "    // Diagnostics\n"
    decl = region(parent_h, "    // Canonical token-to-range conversion", marker)
    cur_h = cur_h.replace(marker, decl + marker, 1)

old_diag = "    // Diagnostics\n    std::vector<Diagnostic> diagnoseDocument(const OpenDocument& doc);\n    void publishDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics);\n"
new_diag = "    // Diagnostics\n    std::vector<Diagnostic> diagnoseDocument(const OpenDocument& doc);\n    DiagnosticResult diagnoseDocumentWithCompile(const OpenDocument& doc);\n    void publishDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics);\n\n    // Overlay-first source provider for compile-based diagnostics.\n    ::emojineer::SourceProvider createSourceProvider() const;\n"
if old_diag not in cur_h:
    raise SystemExit("header diagnostic declaration anchor mismatch")
cur_h = cur_h.replace(old_diag, new_diag, 1)
cur_h_path.write_text(clean_ws(cur_h))

parent_cpp = git_show("src/lsp.cpp")
cur_cpp_path = Path("src/lsp.cpp")
cur_cpp = cur_cpp_path.read_text()

# Reuse the current, already-qualified document lifecycle that publishes grouped imported diagnostics.
parent_lifecycle = region(parent_cpp, "void LanguageServer::openDocument", "void LanguageServer::saveDocument")
cur_cpp = replace_region(cur_cpp, "void LanguageServer::openDocument", "void LanguageServer::saveDocument", parent_lifecycle)

# Parent tokenToRange requires a small UTF-16 counting helper absent from the restored branch.
if "static std::size_t countUtf16Units(" not in cur_cpp:
    helper = region(parent_cpp, "// Count total UTF-16 code units", "// Convert UTF-8 byte index to UTF-16 position")
    marker = "// Convert UTF-8 byte index to UTF-16 position"
    cur_cpp = cur_cpp.replace(marker, helper + marker, 1)

# Insert canonical token->LSP range conversion immediately before diagnostics.
if "Range LanguageServer::tokenToRange(" not in cur_cpp:
    token_region = region(parent_cpp, "Range LanguageServer::tokenToRange", "std::vector<Diagnostic> LanguageServer::diagnoseDocument")
    marker = "std::vector<Diagnostic> LanguageServer::diagnoseDocument"
    cur_cpp = cur_cpp.replace(marker, token_region + marker, 1)

# Replace the obsolete diagnostic implementation with the current typed/grouped pipeline.
parent_diag = region(parent_cpp, "std::vector<Diagnostic> LanguageServer::diagnoseDocument", "void LanguageServer::publishDiagnostics")
cur_cpp = replace_region(cur_cpp, "std::vector<Diagnostic> LanguageServer::diagnoseDocument", "void LanguageServer::publishDiagnostics", parent_diag)

# Range formatting is not qualified in Train 17. Keep full-document formatting only.
cur_cpp = cur_cpp.replace('json::objectSet(caps, "documentRangeFormattingProvider", JsonValue(true));',
                          'json::objectSet(caps, "documentRangeFormattingProvider", JsonValue(false));')

cur_cpp_path.write_text(clean_ws(cur_cpp))
print("reconciled current typed diagnostics, grouped source URIs, canonical token ranges, and range-format capability")
