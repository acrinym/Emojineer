from pathlib import Path

path = Path("tests/project_tests.cpp")
text = path.read_text()
start = text.index("void test_registry_path_dependency_rejected_offline()")
end = text.index("// Regression test: corrupted offline materialization", start)
block = text[start:end]

needle = '''    const auto store_root = root / ".emojineer" / "packages";\n    std::filesystem::remove_all(root);\n'''
replacement = '''    const auto store_root = root / ".emojineer" / "packages";\n    const auto endpoint = emojineer::parse_registry_endpoint("https://registry.example.com");\n    std::filesystem::remove_all(root);\n'''
if block.count(needle) != 1:
    raise SystemExit("offline path fixture setup anchor mismatch")
block = block.replace(needle, replacement, 1)

needle = '''    lock_out << "endpoint = \\\"https://registry.example.com\\\"\\n";\n'''
replacement = '''    lock_out << "endpoint = \\\"" << endpoint.canonical << "\\\"\\n";\n'''
if block.count(needle) != 1:
    raise SystemExit(f"offline path fixture registry endpoint anchor mismatch: {block.count(needle)}")
block = block.replace(needle, replacement, 1)

needle = '''    lock_out << "registry_endpoint = \\\"https://registry.example.com\\\"\\n";\n'''
replacement = '''    lock_out << "registry_endpoint = \\\"" << endpoint.canonical << "\\\"\\n";\n'''
if block.count(needle) != 1:
    raise SystemExit("offline path fixture dependency endpoint anchor mismatch")
block = block.replace(needle, replacement, 1)

needle = '''    manifest_file << "origin = \\\"https://registry.example.com\\\"\\n";\n'''
replacement = '''    manifest_file << "origin = \\\"" << endpoint.canonical << "\\\"\\n";\n'''
if block.count(needle) != 1:
    raise SystemExit("offline path fixture manifest endpoint anchor mismatch")
block = block.replace(needle, replacement, 1)

text = text[:start] + block + text[end:]
path.write_text(text)
print("applied: offline path-dependency fixture uses canonical owner endpoint")
