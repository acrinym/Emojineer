from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


project_path = Path("src/project.cpp")
project = project_path.read_text()

old = '''                try {
                    auto lock = load_project_lock(lock_path);
                    for (const auto& lock_dep : lock.dependencies) {
                        if (lock_dep.name == dep.name && lock_dep.source == LockSourceKind::Registry) {
                            // Found in lock - construct resolved dependency from lock data
'''
new = '''                try {
                    auto lock = load_project_lock(lock_path);

                    // Registry aliases are package-local coordinates. Resolve the authority
                    // from the CURRENT owning manifest before accepting a lock entry.
                    const auto owner_registry = std::find_if(
                        manifest.registries.begin(), manifest.registries.end(),
                        [&](const ProjectRegistry& registry) {
                            return registry.alias == dep.registry_alias;
                        });
                    if (owner_registry == manifest.registries.end()) {
                        throw std::runtime_error("offline registry dependency '" + dep.name +
                                                 "' references unknown owner registry '" +
                                                 dep.registry_alias + "'");
                    }
                    const auto expected_endpoint = parse_registry_endpoint(owner_registry->endpoint);
                    const auto expected_registry_id = registry_identity(expected_endpoint);

                    for (const auto& lock_dep : lock.dependencies) {
                        if (lock_dep.name == dep.name &&
                            lock_dep.source == LockSourceKind::Registry &&
                            lock_dep.registry_alias && *lock_dep.registry_alias == dep.registry_alias &&
                            lock_dep.requirement && *lock_dep.requirement == dep.requirement &&
                            lock_dep.registry_id && *lock_dep.registry_id == expected_registry_id &&
                            lock_dep.registry_endpoint && *lock_dep.registry_endpoint == expected_endpoint.canonical) {
                            // Found the exact owner-scoped coordinate in the lock.
'''
project = replace_once(project, old, new, "project offline lock coordinate")

old = '''                            // Recursively resolve dependencies from lock
                            ProjectManifest synthetic_manifest;
                            synthetic_manifest.dependencies = resolved_dep.dependencies;
                            auto nested = resolve_registry_dependencies_impl(synthetic_manifest, store_root, project_root, offline, resolved, resolving);
'''
new = '''                            // Recursively resolve dependencies from lock using the materialized
                            // package's own registry bindings, not the root application's aliases.
                            ProjectManifest synthetic_manifest;
                            synthetic_manifest.dependencies = resolved_dep.dependencies;
                            if (std::filesystem::exists(resolved_dep.store_path / "emojineer.toml")) {
                                const auto embedded_manifest = load_project_manifest(
                                    resolved_dep.store_path / "emojineer.toml");
                                synthetic_manifest.registries = embedded_manifest.registries;
                            }
                            auto nested = resolve_registry_dependencies_impl(synthetic_manifest, store_root, project_root, offline, resolved, resolving);
'''
project = replace_once(project, old, new, "project offline nested owner registries")

old = '''    // Resolve registry dependencies
    std::unordered_map<std::string, ResolvedRegistryDependency> resolved;
'''
new = '''    // Offline sync may not be a weaker authority than offline compile/LSP.
    // Validate the existing lock and the complete materialized package graph BEFORE
    // resolving or rewriting anything. A malformed/stale/wrong-authority lock fails here.
    if (offline) {
        (void)resolve_package_graph(root, manifest, store_root, true);
    }

    // Resolve registry dependencies
    std::unordered_map<std::string, ResolvedRegistryDependency> resolved;
'''
project = replace_once(project, old, new, "offline sync front-door validation")
project_path.write_text(project)
print("applied: sync_project offline front-door and owner-scoped lock authority")


package_path = Path("src/package.cpp")
package = package_path.read_text()
old = '''                if (offline_) {
                    if (!lock_dep->registry_alias || *lock_dep->registry_alias != dependency.registry_alias ||
                        !lock_dep->requirement || *lock_dep->requirement != dependency.requirement) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' lock coordinate does not match owner manifest");
                    }
                    std::vector<std::string> manifest_edges;
'''
new = '''                if (offline_) {
                    if (!lock_dep->registry_alias || *lock_dep->registry_alias != dependency.registry_alias ||
                        !lock_dep->requirement || *lock_dep->requirement != dependency.requirement) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' lock coordinate does not match owner manifest");
                    }

                    // Alias alone is not an authority: two packages may both call different
                    // registries `origin`. Bind the locked dependency to the registry declared
                    // by THIS owning package and require canonical endpoint + identity equality.
                    const auto owner_registry = std::find_if(
                        manifest.registries.begin(), manifest.registries.end(),
                        [&](const ProjectRegistry& registry) {
                            return registry.alias == dependency.registry_alias;
                        });
                    if (owner_registry == manifest.registries.end()) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' owner manifest has no registry '" +
                                                 dependency.registry_alias + "'");
                    }
                    const auto expected_endpoint = parse_registry_endpoint(owner_registry->endpoint);
                    const auto expected_registry_id = registry_identity(expected_endpoint);
                    if (!lock_dep->registry_id || *lock_dep->registry_id != expected_registry_id ||
                        !lock_dep->registry_endpoint ||
                        *lock_dep->registry_endpoint != expected_endpoint.canonical) {
                        throw std::runtime_error("offline mode: registry dependency '" + dependency.name +
                                                 "' lock authority does not match owner manifest");
                    }

                    std::vector<std::string> manifest_edges;
'''
package = replace_once(package, old, new, "package graph owner authority")
package_path.write_text(package)
print("applied: PackageGraph enforces owner-scoped registry endpoint and identity")


test_path = Path("tests/acceptance_journey_tests.cpp")
test = test_path.read_text()
old = '''void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write test file");
    output << text;
}
'''
new = '''void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write test file");
    output << text;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read test file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
'''
test = replace_once(test, old, new, "acceptance read_text helper")

old = '''    require(saw_b && saw_a,
            "sync-produced lock must retain store paths and content hashes for direct/transitive registry packages");
    const auto offline_graph = emojineer::resolve_package_graph(
        app_root, app_manifest, emojineer::package_store_root(app_root), true);
    require(offline_graph.find("lib-b") != nullptr && offline_graph.find("lib-a") != nullptr,
            "offline graph must contain direct lib-b and its owned transitive lib-a");

    std::filesystem::remove_all(root_registry);
    std::filesystem::remove_all(child_registry);
'''
new = '''    require(saw_b && saw_a,
            "sync-produced lock must retain store paths and content hashes for direct/transitive registry packages");

    // Corrupt only the transitive authority: lib-a belongs to child_registry, but make
    // its lock metadata claim the root application's `origin`. Both authorities remain
    // declared in the lock, so only owner-scoped validation can reject this.
    const auto valid_lock_text = read_text(app_root / "emojineer.lock");
    auto wrong_authority_lock = app_lock;
    std::string root_registry_id;
    for (const auto& registry : app_lock.registries) {
        if (registry.alias == "origin" && registry.endpoint == root_endpoint.canonical) {
            root_registry_id = registry.id;
            break;
        }
    }
    require(!root_registry_id.empty(), "root registry identity must be present in lock");
    bool rewrote_transitive_authority = false;
    for (auto& dep : wrong_authority_lock.dependencies) {
        if (dep.name != "lib-a") continue;
        dep.registry_alias = "origin";
        dep.registry_id = root_registry_id;
        dep.registry_endpoint = root_endpoint.canonical;
        rewrote_transitive_authority = true;
        break;
    }
    require(rewrote_transitive_authority, "test must locate transitive lib-a lock entry");
    write_text(app_root / "emojineer.lock",
               emojineer::canonical_lock_text(wrong_authority_lock));
    const auto corrupt_lock_text = read_text(app_root / "emojineer.lock");

    std::filesystem::remove_all(root_registry);
    std::filesystem::remove_all(child_registry);

    bool offline_sync_rejected_wrong_authority = false;
    try {
        emojineer::sync_project(app_root, true);
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        offline_sync_rejected_wrong_authority =
            message.find("authority") != std::string::npos ||
            message.find("coordinate") != std::string::npos ||
            message.find("registry") != std::string::npos;
    }
    require(offline_sync_rejected_wrong_authority,
            "offline sync must reject transitive lock provenance bound to the root authority");
    require(read_text(app_root / "emojineer.lock") == corrupt_lock_text,
            "rejected offline sync must not rewrite the malformed lock");

    // Restore the valid pre-corruption lock. The registries stay unavailable for every
    // remaining operation, proving the accepted path is completely materialized/offline.
    write_text(app_root / "emojineer.lock", valid_lock_text);
    const auto offline_graph = emojineer::resolve_package_graph(
        app_root, app_manifest, emojineer::package_store_root(app_root), true);
    require(offline_graph.find("lib-b") != nullptr && offline_graph.find("lib-a") != nullptr,
            "offline graph must contain direct lib-b and its owned transitive lib-a");
'''
test = replace_once(test, old, new, "acceptance offline sync wrong authority regression")
test_path.write_text(test)
print("applied: offline sync wrong-authority/no-rewrite acceptance regression")
