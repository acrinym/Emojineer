from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text()
    if new in text:
        print(f"already applied: {label}")
        return
    if text.count(old) != 1:
        raise SystemExit(f"{label}: expected exactly one match, got {text.count(old)}")
    p.write_text(text.replace(old, new, 1))
    print(f"applied: {label}")


# 1) Preserve every registry authority needed by direct AND transitive registry packages.
p = Path("src/project.cpp")
text = p.read_text()
old = '''    // Resolve package graph for path dependencies
    auto canonical_root = std::filesystem::canonical(root);
    auto graph = resolve_package_graph(canonical_root, manifest);
'''
new = '''    // Registry aliases are lock-wide authority records. Preserve authorities discovered
    // through transitive registry packages as well as root-declared registries.
    for (const auto& resolved : resolved_registry_deps) {
        auto existing = std::find_if(lock.registries.begin(), lock.registries.end(),
                                     [&](const LockRegistry& registry) {
                                         return registry.alias == resolved.registry_alias;
                                     });
        if (existing == lock.registries.end()) {
            lock.registries.push_back({resolved.registry_alias,
                                       resolved.registry_id,
                                       resolved.registry_endpoint});
        } else if (existing->id != resolved.registry_id ||
                   existing->endpoint != resolved.registry_endpoint) {
            throw std::runtime_error("registry alias '" + resolved.registry_alias +
                                     "' resolves to conflicting registry authorities");
        }
    }

    // Resolve package graph for path dependencies
    auto canonical_root = std::filesystem::canonical(root);
    auto graph = resolve_package_graph(canonical_root, manifest);
'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit("transitive registry authority preservation anchor missing")
    text = text.replace(old, new, 1)
    print("applied: preserve transitive registry authority records")

# 2) Parse lock_version instead of silently defaulting to v3; reject invalid source kinds.
text = text.replace('''    ProjectLock lock;
    lock.version = "3";  // Default to version 3
''', '''    ProjectLock lock;
    lock.version.clear();
''', 1)

old_loop = '''        if (line == "[[registry]]") {
            lock.registries.push_back({});
            current_reg = &lock.registries.back();
            current_dep = nullptr;
        } else if (line == "[[dependency]]") {
'''
new_loop = '''        if (!current_reg && !current_dep && line.rfind("lock_version", 0) == 0) {
            const auto eq = line.find('=');
            if (eq == std::string::npos || !lock.version.empty()) {
                throw std::runtime_error("malformed or duplicate lock_version");
            }
            lock.version = trim(line.substr(eq + 1));
            if (lock.version.empty()) throw std::runtime_error("lock_version cannot be empty");
        } else if (line == "[[registry]]") {
            lock.registries.push_back({});
            current_reg = &lock.registries.back();
            current_dep = nullptr;
        } else if (line == "[[dependency]]") {
'''
if new_loop not in text:
    if text.count(old_loop) != 1:
        raise SystemExit("lock_version parser anchor missing")
    text = text.replace(old_loop, new_loop, 1)
    print("applied: parse explicit lock_version")

old_source = '''                if (key == "source") {
                    current_dep->source = (value == "registry") ? LockSourceKind::Registry : LockSourceKind::Path;
                } else if (key == "name") current_dep->name = value;
'''
new_source = '''                if (key == "source") {
                    if (value == "registry") current_dep->source = LockSourceKind::Registry;
                    else if (value == "path") current_dep->source = LockSourceKind::Path;
                    else throw std::runtime_error("unsupported lock dependency source '" + value + "'");
                } else if (key == "name") current_dep->name = value;
'''
if new_source not in text:
    if text.count(old_source) != 1:
        raise SystemExit("lock source parser anchor missing")
    text = text.replace(old_source, new_source, 1)
    print("applied: reject unknown lock dependency source")
p.write_text(text)

# 3) Central offline registry-lock metadata authority in package.cpp.
p = Path("src/package.cpp")
text = p.read_text()
anchor = '''std::string registry_package_hash(const std::filesystem::path& root,
                                 const ProjectManifest& manifest) {
'''
helper = '''bool valid_sha256_hex(const std::optional<std::string>& value) {
    if (!value || value->size() != 64) return false;
    return std::all_of(value->begin(), value->end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

const LockRegistry* find_lock_registry(const ProjectLock& lock, const std::string& alias) {
    const LockRegistry* found = nullptr;
    for (const auto& registry : lock.registries) {
        if (registry.alias != alias) continue;
        if (found) throw std::runtime_error("offline lock contains duplicate registry alias '" + alias + "'");
        found = &registry;
    }
    return found;
}

void validate_offline_registry_lock(const ProjectManifest& root_manifest,
                                    const ProjectLock& lock) {
    if (lock.version != "3") {
        throw std::runtime_error("offline package resolution requires lock_version 3");
    }

    std::unordered_map<std::string, const LockDependency*> dependencies;
    for (const auto& locked : lock.dependencies) {
        if (locked.name.empty() || locked.version.empty()) {
            throw std::runtime_error("offline lock contains dependency with missing name or version");
        }
        if (!dependencies.emplace(locked.name, &locked).second) {
            throw std::runtime_error("offline lock contains duplicate dependency '" + locked.name + "'");
        }
        if (locked.source != LockSourceKind::Registry) continue;
        if (!locked.registry_alias || locked.registry_alias->empty() ||
            !locked.registry_id || locked.registry_id->empty() ||
            !locked.registry_endpoint || locked.registry_endpoint->empty() ||
            !locked.requirement || locked.requirement->empty() ||
            !locked.store_path || locked.store_path->empty()) {
            throw std::runtime_error("offline registry dependency '" + locked.name +
                                     "' is missing required provenance metadata");
        }
        if (!valid_sha256_hex(locked.artifact_sha256)) {
            throw std::runtime_error("offline registry dependency '" + locked.name +
                                     "' requires valid artifact_sha256");
        }
        if (!valid_sha256_hex(locked.content_sha256)) {
            throw std::runtime_error("offline registry dependency '" + locked.name +
                                     "' requires valid content_sha256");
        }
        const auto* registry = find_lock_registry(lock, *locked.registry_alias);
        if (!registry) {
            throw std::runtime_error("offline registry dependency '" + locked.name +
                                     "' references undeclared registry '" + *locked.registry_alias + "'");
        }
        if (registry->id.empty() || registry->endpoint.empty() ||
            registry->id != *locked.registry_id || registry->endpoint != *locked.registry_endpoint) {
            throw std::runtime_error("offline registry dependency '" + locked.name +
                                     "' registry authority does not match lock registry record");
        }
    }

    for (const auto& dependency : root_manifest.dependencies) {
        if (dependency.kind != DependencyKind::Registry) continue;
        const auto found = dependencies.find(dependency.name);
        if (found == dependencies.end() || found->second->source != LockSourceKind::Registry) {
            throw std::runtime_error("offline root registry dependency '" + dependency.name +
                                     "' has no registry lock entry");
        }
        const auto& locked = *found->second;
        if (!locked.registry_alias || *locked.registry_alias != dependency.registry_alias ||
            !locked.requirement || *locked.requirement != dependency.requirement) {
            throw std::runtime_error("offline root registry dependency '" + dependency.name +
                                     "' lock provenance does not match manifest declaration");
        }
    }
}

'''
if helper.strip() not in text:
    if text.count(anchor) != 1:
        raise SystemExit("package metadata validator anchor missing")
    text = text.replace(anchor, helper + anchor, 1)
    print("applied: central offline registry lock metadata validator")

old_check = '''            if (offline) {
                const auto is_hex = [](char c) {
                    return (c >= '0' && c <= '9') ||
                           (c >= 'a' && c <= 'f') ||
                           (c >= 'A' && c <= 'F');
                };
                for (const auto& locked_dependency : lock_storage.dependencies) {
                    if (locked_dependency.source != LockSourceKind::Registry) continue;
                    if (!locked_dependency.content_sha256 ||
                        locked_dependency.content_sha256->size() != 64 ||
                        !std::all_of(locked_dependency.content_sha256->begin(),
                                     locked_dependency.content_sha256->end(), is_hex)) {
                        throw std::runtime_error(
                            "offline package resolution requires valid content_sha256 for registry dependency '" +
                            locked_dependency.name + "'");
                    }
                }
            }
'''
new_check = '''            if (offline) {
                validate_offline_registry_lock(root_manifest, lock_storage);
            }
'''
if new_check not in text:
    if text.count(old_check) != 1:
        raise SystemExit("offline metadata validation replacement anchor missing")
    text = text.replace(old_check, new_check, 1)
    print("applied: enforce full offline registry lock metadata authority")

# 4) Match canonical dependency-edge data and per-owner registry coordinate declarations.
old_manifest_ok = '''                // Verify manifest version matches locked version
                if (dep_manifest.version != lock_dep->version) {
                    if (offline_) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' version mismatch: locked " + lock_dep->version +
                                                 " but manifest has " + dep_manifest.version);
                    }
                    dependency_names.push_back(dependency.name);
                    continue;
                }
                
                // Registry package is valid
'''
new_manifest_ok = '''                // Verify manifest version matches locked version
                if (dep_manifest.version != lock_dep->version) {
                    if (offline_) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' version mismatch: locked " + lock_dep->version +
                                                 " but manifest has " + dep_manifest.version);
                    }
                    dependency_names.push_back(dependency.name);
                    continue;
                }

                if (offline_) {
                    if (!lock_dep->registry_alias || *lock_dep->registry_alias != dependency.registry_alias ||
                        !lock_dep->requirement || *lock_dep->requirement != dependency.requirement) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' lock coordinate does not match owner manifest");
                    }
                    std::vector<std::string> manifest_edges;
                    manifest_edges.reserve(dep_manifest.dependencies.size());
                    for (const auto& nested : dep_manifest.dependencies) manifest_edges.push_back(nested.name);
                    std::sort(manifest_edges.begin(), manifest_edges.end());
                    auto lock_edges = lock_dep->dependencies;
                    std::sort(lock_edges.begin(), lock_edges.end());
                    if (manifest_edges != lock_edges) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' lock dependency edges do not match materialized manifest");
                    }
                }
                
                // Registry package is valid
'''
if new_manifest_ok not in text:
    if text.count(old_manifest_ok) != 1:
        raise SystemExit("registry edge validation anchor missing")
    text = text.replace(old_manifest_ok, new_manifest_ok, 1)
    print("applied: validate owner coordinates and dependency edges")
p.write_text(text)

# 5) Repair the legacy acceptance journey so it proves actual package ownership semantics.
p = Path("tests/acceptance_journey_tests.cpp")
text = p.read_text()
old_sources = '''    write_text(lib_a_root / "src/lib-a.emoji", 
        "📝 Library A\\n"
        "export const a_value = 42\\n");
'''
new_sources = '''    write_text(lib_a_root / "src/lib-a.emoji",
        "🧩 🌊\\n"
        "🐍 🌟 🔢 🟰 9\\n"
        "📤 🌟\\n");
'''
if new_sources not in text:
    if text.count(old_sources) != 1:
        raise SystemExit("legacy lib-a source fixture anchor missing")
    text = text.replace(old_sources, new_sources, 1)

old_b = '''    write_text(lib_b_root / "src/lib-b.emoji", 
        "📝 Library B depends on A\\n"
        "🔗 📜\\\"lib-a\\\"📜\\n"
        "export const b_value = 1\\n");
'''
new_b = '''    write_text(lib_b_root / "src/lib-b.emoji",
        "🧩 🌲\\n"
        "🔗 📜pkg:lib-a/src/lib-a.emoji📜\\n"
        "🛠️ 🍏 🫴 🤲\\n"
        "📦 🌟\\n"
        "🏁\\n"
        "📤 🍏\\n");
'''
if new_b not in text:
    if text.count(old_b) != 1:
        raise SystemExit("legacy lib-b source fixture anchor missing")
    text = text.replace(old_b, new_b, 1)

old_main = '''    write_text(app_root / "src/main.emoji",
        "🧩 🚀\\n"           // Module declaration
        "🐍 🌟 🟰 42\\n"    // Variable declaration: 🐍 = variable, 🌟 = name, 🟰 = assignment
        "📤 🌟\\n");         // Export 🌟
'''
new_main = '''    write_text(app_root / "src/main.emoji",
        "🧩 🚀\\n"
        "🔗 📜pkg:lib-b/src/lib-b.emoji📜\\n"
        "📝 🍏 🫴 🤲\\n");
'''
if new_main not in text:
    if text.count(old_main) != 1:
        raise SystemExit("legacy direct package compile fixture anchor missing")
    text = text.replace(old_main, new_main, 1)

old_bad = '''    write_text(app_root / "src/bad_import.emoji",
        "import { a_value } from \\\"lib-a\\\"\\n"  // Direct import of transitive dep should fail
        "export const bad = a_value\\n");
    
    bool import_rejected = false;
    try {
        auto chunk = emojineer::compile_file(app_root / "src/bad_import.emoji", {}, app_root);
        // If this succeeds, the test should fail
        require(false, "direct import of transitive lib-a should be rejected");
    } catch (const std::exception& e) {
        // Expected - import of transitive dependency should be rejected
        import_rejected = true;
    }
    require(import_rejected, "direct import of transitive dependency lib-a should be rejected in offline mode");
'''
new_bad = '''    write_text(app_root / "src/bad_import.emoji",
        "🧩 🚀\\n"
        "🔗 📜pkg:lib-a/src/lib-a.emoji📜\\n");
    
    bool import_rejected = false;
    try {
        (void)emojineer::compile_file(app_root / "src/bad_import.emoji", {}, app_root);
    } catch (const std::runtime_error& error) {
        import_rejected = std::string(error.what()).find(
            "does not declare direct dependency 'lib-a'") != std::string::npos;
    }
    require(import_rejected,
            "direct transitive lib-a import must fail with direct-dependency ownership diagnostic");
'''
if new_bad not in text:
    if text.count(old_bad) != 1:
        raise SystemExit("legacy false-positive transitive fixture anchor missing")
    text = text.replace(old_bad, new_bad, 1)
print("applied: legacy acceptance now proves direct package compile + ownership diagnostic")

# 6) Add a real sync-produced malformed-lock matrix. Every mutation must fail graph AND compile.
fn = r'''
void test_offline_registry_lock_metadata_rejected() {
    std::cout << "Test: malformed registry lock metadata is rejected before offline module loading...\n";
    const auto registry_root = temp_root("registry-lock-metadata");
    std::filesystem::create_directories(registry_root);
    emojineer::initialize_file_registry(registry_root, "emojineer.test");
    const auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());

    const auto lib_root = temp_root("lock-metadata-lib");
    std::filesystem::create_directories(lib_root / "src");
    write_text(lib_root / "emojineer.toml",
        "[package]\nname = \"locked-lib\"\nversion = \"1.0.0\"\nentry = \"src/main.emoji\"\n");
    write_text(lib_root / "src/main.emoji", "🧩 🌊\n🐍 🌟 🔢 🟰 9\n📤 🌟\n");
    (void)emojineer::publish_package_to_registry(lib_root, endpoint);

    const auto app_root = temp_root("lock-metadata-app");
    emojineer::initialize_project(app_root, "app");
    emojineer::add_project_registry_dependency(app_root, "locked-lib", "^1.0.0",
                                               registry_root.string(), "origin");
    write_text(app_root / "src/main.emoji",
        "🧩 🚀\n🔗 📜pkg:locked-lib/src/main.emoji📜\n📝 🍏 🫴 🤲\n");
    emojineer::sync_project(app_root, false);

    const auto manifest = emojineer::load_project_manifest(app_root / "emojineer.toml");
    const auto good_lock = emojineer::load_project_lock(app_root / "emojineer.lock");
    const auto good_text = emojineer::read_text_standalone(app_root / "emojineer.lock");
    const auto store_root = emojineer::package_store_root(app_root);
    const auto* locked = [&]() -> const emojineer::LockDependency* {
        for (const auto& dep : good_lock.dependencies) if (dep.name == "locked-lib") return &dep;
        return nullptr;
    }();
    require(locked && locked->registry_alias && locked->registry_id && locked->registry_endpoint &&
            locked->requirement && locked->artifact_sha256 && locked->content_sha256,
            "sync-produced registry lock must contain complete metadata before mutation");

    auto replace_once = [](std::string text, const std::string& old_value,
                           const std::string& new_value) {
        const auto pos = text.find(old_value);
        if (pos == std::string::npos) throw std::runtime_error("acceptance mutation anchor missing: " + old_value);
        text.replace(pos, old_value.size(), new_value);
        return text;
    };
    auto erase_once = [](std::string text, const std::string& value) {
        const auto pos = text.find(value);
        if (pos == std::string::npos) throw std::runtime_error("acceptance erase anchor missing: " + value);
        text.erase(pos, value.size());
        return text;
    };

    std::vector<std::pair<std::string, std::string>> cases;
    cases.push_back({"lock version", replace_once(good_text, "lock_version = 3", "lock_version = 2")});
    cases.push_back({"missing registry alias", erase_once(good_text, "registry = \"" + *locked->registry_alias + "\"\n")});
    cases.push_back({"undeclared registry alias", replace_once(good_text,
        "registry = \"" + *locked->registry_alias + "\"",
        "registry = \"missing-authority\"")});
    cases.push_back({"missing registry id", erase_once(good_text, "registry_id = \"" + *locked->registry_id + "\"\n")});
    cases.push_back({"mismatched registry endpoint", replace_once(good_text,
        "registry_endpoint = \"" + *locked->registry_endpoint + "\"",
        "registry_endpoint = \"file:///wrong-registry\"")});
    cases.push_back({"empty requirement", replace_once(good_text,
        "requirement = \"" + *locked->requirement + "\"", "requirement = \"\"")});
    cases.push_back({"invalid artifact sha", replace_once(good_text,
        "artifact_sha256 = \"" + *locked->artifact_sha256 + "\"", "artifact_sha256 = \"xyz\"")});
    cases.push_back({"missing content sha", erase_once(good_text,
        "content_sha256 = \"" + *locked->content_sha256 + "\"\n")});
    cases.push_back({"wrong dependency edges", replace_once(good_text,
        "dependencies = \"\"", "dependencies = \"ghost\"")});

    std::filesystem::remove_all(registry_root);
    for (const auto& [label, mutated] : cases) {
        write_text(app_root / "emojineer.lock", mutated);
        bool graph_rejected = false;
        try {
            (void)emojineer::resolve_package_graph(app_root, manifest, store_root, true);
        } catch (const std::runtime_error&) {
            graph_rejected = true;
        }
        require(graph_rejected, label + " must be rejected by offline package graph");

        bool compile_rejected = false;
        try {
            (void)emojineer::compile_file(app_root / "src/main.emoji", {}, app_root);
        } catch (const std::runtime_error&) {
            compile_rejected = true;
        }
        require(compile_rejected, label + " must be rejected by compile_file before module loading");
    }

    write_text(app_root / "emojineer.lock", good_text);
    (void)emojineer::resolve_package_graph(app_root, manifest, store_root, true);
    (void)emojineer::compile_file(app_root / "src/main.emoji", {}, app_root);

    std::filesystem::remove_all(lib_root);
    std::filesystem::remove_all(app_root);
    std::cout << "  ✅ Malformed registry lock metadata rejected by graph and compile authority\n";
}

'''
end_anchor = '} // anonymous namespace\n\nint main() {'
if 'void test_offline_registry_lock_metadata_rejected()' not in text:
    if text.count(end_anchor) != 1:
        raise SystemExit("acceptance namespace-end anchor missing")
    text = text.replace(end_anchor, fn + end_anchor, 1)
    print("applied: malformed registry lock metadata acceptance matrix")

call_anchor = '        test_sync_lock_offline_compile_roundtrip();\n'
call_new = call_anchor + '        test_offline_registry_lock_metadata_rejected();\n'
if call_new not in text:
    if text.count(call_anchor) != 1:
        raise SystemExit("acceptance call anchor missing")
    text = text.replace(call_anchor, call_new, 1)
p.write_text(text)
