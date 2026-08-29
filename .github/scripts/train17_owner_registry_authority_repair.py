from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text()
    if new in text:
        print(f"already applied: {label}")
        return
    if text.count(old) != 1:
        raise SystemExit(f"{label}: expected exactly one source match, got {text.count(old)}")
    p.write_text(text.replace(old, new, 1))
    print(f"applied: {label}")


replace_once(
    "src/project.cpp",
    '''                // Recursively resolve embedded dependencies
                // Create a synthetic manifest with the parent project's registries and embedded package's dependencies
                // This is needed because the embedded artifact doesn't include registry definitions
                ProjectManifest synthetic_manifest;
                synthetic_manifest.dependencies = embedded_manifest.dependencies;
                // Use parent manifest's registries for resolving transitive deps
                synthetic_manifest.registries = manifest.registries;
''',
    '''                // Recursively resolve embedded dependencies using the owning package's
                // embedded registry authority. Registry aliases are package-local coordinates:
                // a child package may bind `origin` to a different endpoint than the root app.
                ProjectManifest synthetic_manifest;
                synthetic_manifest.dependencies = embedded_manifest.dependencies;
                synthetic_manifest.registries = embedded_manifest.registries;
''',
    "transitive registry resolution uses owning package registries",
)

p = Path("tests/acceptance_journey_tests.cpp")
text = p.read_text()
text = text.replace(
'''    const auto registry_root = temp_root("registry-dep");
    std::filesystem::create_directories(registry_root);
    emojineer::initialize_file_registry(registry_root, "emojineer.test");
    const auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());
''',
'''    // Root and child deliberately reuse alias `origin` for different authorities.
    // lib-b is published in root_registry; only child_registry contains transitive lib-a.
    const auto root_registry = temp_root("registry-dep-root");
    const auto child_registry = temp_root("registry-dep-child");
    std::filesystem::create_directories(root_registry);
    std::filesystem::create_directories(child_registry);
    emojineer::initialize_file_registry(root_registry, "emojineer.root.test");
    emojineer::initialize_file_registry(child_registry, "emojineer.child.test");
    const auto root_endpoint = emojineer::parse_registry_endpoint(root_registry.string());
    const auto child_endpoint = emojineer::parse_registry_endpoint(child_registry.string());
''', 1)
text = text.replace(
'const auto published_a = emojineer::publish_package_to_registry(lib_a_root, endpoint);',
'const auto published_a = emojineer::publish_package_to_registry(lib_a_root, child_endpoint);', 1)
text = text.replace(
'"\\n[registries]\\norigin = \\\"" + registry_root.string() + "\\\"\\n"',
'"\\n[registries]\\norigin = \\\"" + child_registry.string() + "\\\"\\n"', 1)
text = text.replace(
'const auto published_b = emojineer::publish_package_to_registry(lib_b_root, endpoint);',
'const auto published_b = emojineer::publish_package_to_registry(lib_b_root, root_endpoint);', 1)
text = text.replace(
'require(!emojineer::load_registry_package_index(endpoint, "lib-b").versions.empty(),',
'require(!emojineer::load_registry_package_index(root_endpoint, "lib-b").versions.empty(),', 1)
text = text.replace(
'app_root, "lib-b", "^1.0.0", registry_root.string(), "origin");',
'app_root, "lib-b", "^1.0.0", root_registry.string(), "origin");', 1)
text = text.replace(
'''    require(app_lock.registries.size() == 1 && app_lock.registries.front().alias == "origin",
            "lock must retain the concrete registry authority");
''',
'''    bool saw_root_authority = false;
    bool saw_child_authority = false;
    for (const auto& registry : app_lock.registries) {
        if (registry.alias != "origin") continue;
        if (registry.endpoint == root_endpoint.canonical) saw_root_authority = true;
        if (registry.endpoint == child_endpoint.canonical) saw_child_authority = true;
    }
    require(saw_root_authority && saw_child_authority,
            "lock must retain both package-local authorities even when both aliases are origin");
''', 1)
text = text.replace(
'''    std::filesystem::remove_all(registry_root);
''',
'''    // Both network/file authorities are gone: all following package operations are offline.
    std::filesystem::remove_all(root_registry);
    std::filesystem::remove_all(child_registry);
''', 1)
text = text.replace(
'''    std::filesystem::remove_all(lib_a_root);
    std::filesystem::remove_all(lib_b_root);
    std::filesystem::remove_all(app_root);
    std::cout << "  ✅ Registry artifact, offline direct dependency, and transitive ownership proven\\n";
''',
'''    std::filesystem::remove_all(lib_a_root);
    std::filesystem::remove_all(lib_b_root);
    std::filesystem::remove_all(app_root);
    std::cout << "  ✅ Package-local registry authority, offline ownership, and tamper integrity proven\\n";
''', 1)
if 'const auto child_endpoint = emojineer::parse_registry_endpoint(child_registry.string());' not in text:
    raise SystemExit("two-registry acceptance rewrite did not apply")
p.write_text(text)
print("applied: two-registry same-alias owner-authority acceptance journey")
