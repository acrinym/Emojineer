from pathlib import Path

path = Path("tests/lsp_tests.cpp")
text = path.read_text()
needle = '''        // Create registry package using materialize_package API (not hand-authored)\n        auto storeRoot = rootPath / ".emojineer" / "packages";\n        \n        // Fixed: 🧩 🧮 (valid identifier), 🧠 (not starting with Add token), 🍇 (valid identifier), bare 🏁\n'''
replacement = '''        // Registry package fixture content. The store root is declared once below,\n        // immediately before materialization.\n        \n        // Fixed: 🧩 🧮 (valid identifier), 🧠 (not starting with Add token), 🍇 (valid identifier), bare 🏁\n'''
if text.count(needle) != 1:
    raise SystemExit(f"duplicate storeRoot fixture anchor mismatch: {text.count(needle)}")
path.write_text(text.replace(needle, replacement, 1))
print("fixed: LspTestWorkspace declares registry storeRoot exactly once")
