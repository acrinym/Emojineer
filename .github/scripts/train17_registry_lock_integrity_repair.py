from pathlib import Path


def replace_once(path, old, new, label):
    p = Path(path)
    text = p.read_text()
    if new in text:
        print(f"already applied: {label}")
        return
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}")
    p.write_text(text.replace(old, new, 1))
    print(f"applied: {label}")


replace_once(
    "src/project.cpp",
    '''            if (dep.artifact_sha256) out << "artifact_sha256 = \\\"" << *dep.artifact_sha256 << "\\\"\\n";
            if (dep.store_path) out << "store_path = \\\"" << *dep.store_path << "\\\"\\n";''',
    '''            if (dep.artifact_sha256) out << "artifact_sha256 = \\\"" << *dep.artifact_sha256 << "\\\"\\n";
            if (dep.content_sha256) out << "content_sha256 = \\\"" << *dep.content_sha256 << "\\\"\\n";
            if (dep.store_path) out << "store_path = \\\"" << *dep.store_path << "\\\"\\n";''',
    "canonical registry lock persists content_sha256",
)

replace_once(
    "src/package.cpp",
    '''            if (is_lock_stale(root, root_manifest, lock_storage)) {
                throw std::runtime_error("emojineer.lock is stale; run 'emji sync'");
            }
            lock = &lock_storage;''',
    '''            if (is_lock_stale(root, root_manifest, lock_storage)) {
                throw std::runtime_error("emojineer.lock is stale; run 'emji sync'");
            }
            if (offline) {
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
            lock = &lock_storage;''',
    "offline registry lock requires valid content hash",
)

p = Path("tests/acceptance_journey_tests.cpp")
text = p.read_text()
old_loop = '''    bool saw_b = false;
    bool saw_a = false;
    for (const auto& dep : app_lock.dependencies) {
        if (dep.name == "lib-b") {
            saw_b = dep.source == emojineer::LockSourceKind::Registry && dep.store_path.has_value();
        } else if (dep.name == "lib-a") {
            saw_a = dep.source == emojineer::LockSourceKind::Registry && dep.store_path.has_value();
        }
    }
    require(saw_b && saw_a,
            "lock must materialize direct lib-b and transitive lib-a as registry packages");'''
new_loop = '''    bool saw_b = false;
    bool saw_a = false;
    std::filesystem::path lib_b_store;
    for (const auto& dep : app_lock.dependencies) {
        if (dep.name == "lib-b") {
            saw_b = dep.source == emojineer::LockSourceKind::Registry &&
                    dep.store_path.has_value() && dep.content_sha256.has_value() &&
                    dep.content_sha256->size() == 64;
            if (dep.store_path) lib_b_store = *dep.store_path;
        } else if (dep.name == "lib-a") {
            saw_a = dep.source == emojineer::LockSourceKind::Registry &&
                    dep.store_path.has_value() && dep.content_sha256.has_value() &&
                    dep.content_sha256->size() == 64;
        }
    }
    require(saw_b && saw_a,
            "sync-produced lock must retain store paths and content hashes for direct/transitive registry packages");'''
if new_loop not in text:
    if text.count(old_loop) != 1:
        raise SystemExit(f"acceptance lock assertion: expected one block, got {text.count(old_loop)}")
    text = text.replace(old_loop, new_loop, 1)
    print("applied: sync-produced lock asserts registry content hashes")
else:
    print("already applied: sync-produced lock asserts registry content hashes")

old_tail = '''    require(transitive_rejected,
            "root must reject ambient transitive lib-a with the direct-ownership diagnostic");

    std::filesystem::remove_all(lib_a_root);'''
new_tail = '''    require(transitive_rejected,
            "root must reject ambient transitive lib-a with the direct-ownership diagnostic");

    // Tamper with a package materialized by sync. The registry is already unavailable, so
    // both graph resolution and compile_file must enforce the lock's persisted content hash.
    require(!lib_b_store.empty(), "sync must expose lib-b materialized store path");
    write_text(lib_b_store / "src/main.emoji",
        "🧩 🌲\\n🔗 📜pkg:lib-a/src/main.emoji📜\\n"
        "🛠️ 🍏 🫴 🤲\\n📦 🌟\\n🏁\\n📤 🍏\\n📝 tampered-after-sync\\n");

    bool graph_tamper_rejected = false;
    try {
        (void)emojineer::resolve_package_graph(
            app_root, app_manifest, emojineer::package_store_root(app_root), true);
    } catch (const std::runtime_error& error) {
        graph_tamper_rejected = std::string(error.what()).find("content SHA256 mismatch") != std::string::npos;
    }
    require(graph_tamper_rejected,
            "offline graph must reject content tampering using sync-produced registry lock hash");

    write_text(app_root / "src/main.emoji",
        "🧩 🚀\\n🔗 📜pkg:lib-b/src/main.emoji📜\\n📝 🍏 🫴 🤲\\n");
    bool compile_tamper_rejected = false;
    try {
        (void)emojineer::compile_file(app_root / "src/main.emoji", {}, app_root);
    } catch (const std::runtime_error& error) {
        compile_tamper_rejected = std::string(error.what()).find("content SHA256 mismatch") != std::string::npos;
    }
    require(compile_tamper_rejected,
            "compile_file must reject content tampering using sync-produced registry lock hash");

    std::filesystem::remove_all(lib_a_root);'''
if new_tail not in text:
    if text.count(old_tail) != 1:
        raise SystemExit(f"acceptance tamper regression: expected one tail block, got {text.count(old_tail)}")
    text = text.replace(old_tail, new_tail, 1)
    print("applied: sync-produced lock tamper regression")
else:
    print("already applied: sync-produced lock tamper regression")
p.write_text(text)
