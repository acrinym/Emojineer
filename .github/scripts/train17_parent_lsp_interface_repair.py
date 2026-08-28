from pathlib import Path

hpp = Path("include/emojineer/lsp.hpp")
h = hpp.read_text()
old_anchor = '''struct OpenDocument {\n    std::string uri;\n    std::string path;\n    int version{0};\n    std::string text;\n    std::vector<Diagnostic> diagnostics;\n};\n\n'''
new_anchor = old_anchor + '''struct DiagnosticResult {\n    std::string primaryUri;\n    std::unordered_map<std::string, std::vector<Diagnostic>> diagnosticsByUri;\n};\n\n'''
if "struct DiagnosticResult" not in h:
    if old_anchor not in h:
        raise SystemExit("lsp interface: OpenDocument anchor not found")
    h = h.replace(old_anchor, new_anchor, 1)
if "std::vector<Diagnostic> diagnoseDocumentWithCompile(const OpenDocument& doc);" in h:
    h = h.replace(
        "std::vector<Diagnostic> diagnoseDocumentWithCompile(const OpenDocument& doc);",
        "DiagnosticResult diagnoseDocumentWithCompile(const OpenDocument& doc);",
        1,
    )
elif "DiagnosticResult diagnoseDocumentWithCompile(const OpenDocument& doc);" not in h:
    raise SystemExit("lsp interface: diagnose declaration not found")
hpp.write_text(h)

cpp = Path("src/lsp.cpp")
c = cpp.read_text()
local = '''// Result type for diagnoseDocumentWithCompile - groups diagnostics by URI\nstruct DiagnosticResult {\n    std::string primaryUri;  // URI to publish primary diagnostics under (entry document)\n    std::map<std::string, std::vector<Diagnostic>> diagnosticsByUri;  // URI -> diagnostics\n};\n\n'''
if local in c:
    c = c.replace(local, "", 1)
elif c.count("struct DiagnosticResult"):
    raise SystemExit("lsp interface: unexpected local DiagnosticResult shape")
cpp.write_text(c)
print("applied: parent LSP DiagnosticResult interface authority")
