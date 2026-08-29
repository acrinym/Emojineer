from pathlib import Path

# Runs after the two lock-hardening scripts.

# Upgrade legacy handcrafted lock fixtures with dependency edges derived from the actual
# materialized manifests, rather than leaving stale placeholder edge lists.
p = Path("tests/package_tests.cpp")
text = p.read_text()
old = '''    lock_text.replace(value_begin, value_end - value_begin, manifest_hash);
    std::ofstream output(lock_path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot rewrite package test lock");
    output << lock_text;
}'''
new = '''    lock_text.replace(value_begin, value_end - value_begin, manifest_hash);
    {
        std::ofstream output(lock_path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot rewrite package test lock");
        output << lock_text;
    }

    auto lock = emojineer::load_project_lock(lock_path);
    lock.version = "3";
    lock.manifest_hash = manifest_hash;
    for (auto& dependency : lock.dependencies) {
        if (dependency.source != emojineer::LockSourceKind::Registry || !dependency.store_path) continue;
        const auto dep_manifest_path = std::filesystem::path(*dependency.store_path) / "emojineer.toml";
        if (!std::filesystem::exists(dep_manifest_path)) continue;
        try {
            const auto dep_manifest = emojineer::load_project_manifest(dep_manifest_path);
            dependency.dependencies.clear();
            for (const auto& nested : dep_manifest.dependencies) {
                dependency.dependencies.push_back(nested.name);
            }
        } catch (const std::exception&) {
            // Corrupt-manifest regression fixtures intentionally fail later at the production seam.
        }
    }
    {
        std::ofstream output(lock_path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot canonicalize package test lock");
        output << emojineer::canonical_lock_text(lock);
    }
}'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f"legacy lock edge refresh tail: expected 1 match, got {text.count(old)}")
    text = text.replace(old, new, 1)
    print("applied: legacy package lock edges derive from materialized manifests")
p.write_text(text)

# The malformed-lock matrix's healthy control should prove a valid direct import, not call an
# unrelated symbol that the tiny fixture does not export.
p = Path("tests/acceptance_journey_tests.cpp")
text = p.read_text()
old = '''    write_text(app_root / "src/main.emoji",
        "🧩 🚀\\n🔗 📜pkg:locked-lib/src/main.emoji📜\\n📝 🍏 🫴 🤲\\n");'''
new = '''    write_text(app_root / "src/main.emoji",
        "🧩 🚀\\n🔗 📜pkg:locked-lib/src/main.emoji📜\\n");'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f"healthy malformed-lock control source: expected 1 match, got {text.count(old)}")
    text = text.replace(old, new, 1)
    print("applied: healthy malformed-lock control proves direct package import")
p.write_text(text)
