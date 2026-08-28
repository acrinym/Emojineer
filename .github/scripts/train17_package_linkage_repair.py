from pathlib import Path

p = Path("src/package.cpp")
text = p.read_text()
old_wrapper = '''// Public API: compute registry package hash using production authority\nstd::string compute_registry_package_hash(const std::filesystem::path& package_root,\n                                         const ProjectManifest& manifest) {\n    return registry_package_hash(package_root, manifest);\n}\n\n'''
public_wrapper = '''std::string compute_registry_package_hash(const std::filesystem::path& package_root,\n                                          const ProjectManifest& manifest) {\n    return registry_package_hash(package_root, manifest);\n}\n\n'''
anchor = '''} // namespace\n\nconst ResolvedPackage* PackageGraph::find'''
repaired_anchor = '''} // namespace\n\n''' + public_wrapper + '''const ResolvedPackage* PackageGraph::find'''

if old_wrapper in text:
    text = text.replace(old_wrapper, "", 1)
    if anchor not in text:
        raise SystemExit("registry hash linkage: namespace-exit anchor not found")
    text = text.replace(anchor, repaired_anchor, 1)
    p.write_text(text)
    print("applied: registry package hash public linkage")
elif public_wrapper in text and text.index(public_wrapper) > text.index("} // namespace"):
    print("already applied: registry package hash public linkage")
else:
    raise SystemExit("registry hash linkage: expected wrapper form not found")
