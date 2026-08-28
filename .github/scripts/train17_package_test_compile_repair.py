from pathlib import Path

p = Path("tests/package_tests.cpp")
text = p.read_text()
replacements = [
    (
        'auto mylib_manifest = emojineer::load_project_manifest(mylib_path / "emojineer.toml");\n    std::string mylib_hash = emojineer::compute_registry_package_hash(mylib_path, mylib_manifest);',
        'auto loaded_mylib_manifest = emojineer::load_project_manifest(mylib_path / "emojineer.toml");\n    std::string mylib_hash = emojineer::compute_registry_package_hash(mylib_path, loaded_mylib_manifest);',
        "mylib loaded manifest fixture name",
    ),
    (
        'auto transitive_manifest = emojineer::load_project_manifest(transitive_path / "emojineer.toml");\n    std::string transitive_hash = emojineer::compute_registry_package_hash(transitive_path, transitive_manifest);',
        'auto loaded_transitive_manifest = emojineer::load_project_manifest(transitive_path / "emojineer.toml");\n    std::string transitive_hash = emojineer::compute_registry_package_hash(transitive_path, loaded_transitive_manifest);',
        "transitive loaded manifest fixture name",
    ),
]
for old, new, label in replacements:
    if old in text:
        if text.count(old) != 1:
            raise SystemExit(f"{label}: expected one legacy occurrence")
        text = text.replace(old, new, 1)
        print(f"applied: {label}")
    elif new in text:
        print(f"already applied: {label}")
    else:
        raise SystemExit(f"{label}: expected fixture block not found")
p.write_text(text)
