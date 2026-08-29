from pathlib import Path

p = Path("tests/acceptance_journey_tests.cpp")
text = p.read_text()

# Repair only the cleanup portions touched by the preceding transformation.
versions_start = text.find("void test_publish_library_versions() {")
versions_end = text.find("void test_publish_library_with_dependency() {", versions_start)
if versions_start == -1 or versions_end == -1:
    raise SystemExit("publish-library test boundaries missing")
versions = text[versions_start:versions_end]
versions = versions.replace(
    "    std::filesystem::remove_all(root_registry);\n    std::filesystem::remove_all(child_registry);\n",
    "    std::filesystem::remove_all(registry_root);\n",
)
text = text[:versions_start] + versions + text[versions_end:]

chain_start = text.find("void test_publish_library_with_dependency() {")
chain_end = text.find("void test_add_remote_dependency() {", chain_start)
if chain_start == -1 or chain_end == -1:
    raise SystemExit("registry-chain test boundaries missing")
chain = text[chain_start:chain_end]
chain = chain.replace(
    "    std::filesystem::remove_all(registry_root);\n",
    "    std::filesystem::remove_all(root_registry);\n    std::filesystem::remove_all(child_registry);\n",
)
if "std::filesystem::remove_all(registry_root);" in chain:
    raise SystemExit("old registry_root cleanup remains in two-registry chain")
if chain.count("std::filesystem::remove_all(root_registry);") != 1 or chain.count("std::filesystem::remove_all(child_registry);") != 1:
    raise SystemExit("two-registry cleanup is not exactly once per authority")
text = text[:chain_start] + chain + text[chain_end:]

p.write_text(text)
print("applied: scoped two-registry cleanup fixture")
