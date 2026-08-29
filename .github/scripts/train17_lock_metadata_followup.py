from pathlib import Path

# This runs after train17_lock_metadata_hardening.py and refines two assumptions
# exposed by the first full-suite qualification.

# Canonical lock registry endpoints must use the same canonical endpoint identity as resolved deps.
p = Path("src/project.cpp")
text = p.read_text()
old = '''        lock.registries.push_back({
            reg.alias,
            registry_identity(endpoint),
            reg.endpoint
        });'''
new = '''        lock.registries.push_back({
            reg.alias,
            registry_identity(endpoint),
            endpoint.canonical
        });'''
count = text.count(old)
if count:
    text = text.replace(old, new)
    print(f"applied: canonicalized {count} root registry lock endpoint site(s)")
else:
    print("already applied: canonical root registry lock endpoints")

old = '''        auto existing = std::find_if(lock.registries.begin(), lock.registries.end(),
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
        }'''
new = '''        auto existing = std::find_if(lock.registries.begin(), lock.registries.end(),
                                     [&](const LockRegistry& registry) {
                                         return registry.alias == resolved.registry_alias &&
                                                registry.id == resolved.registry_id &&
                                                registry.endpoint == resolved.registry_endpoint;
                                     });
        if (existing == lock.registries.end()) {
            lock.registries.push_back({resolved.registry_alias,
                                       resolved.registry_id,
                                       resolved.registry_endpoint});
        }'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f"transitive registry identity block: expected 1 match, got {text.count(old)}")
    text = text.replace(old, new, 1)
    print("applied: registry aliases are package-scoped; preserve unique authority triples")

old = '''    std::sort(sorted_regs.begin(), sorted_regs.end(),
              [](const LockRegistry& a, const LockRegistry& b) {
                  return a.alias < b.alias;
              });'''
new = '''    std::sort(sorted_regs.begin(), sorted_regs.end(),
              [](const LockRegistry& a, const LockRegistry& b) {
                  if (a.alias != b.alias) return a.alias < b.alias;
                  if (a.id != b.id) return a.id < b.id;
                  return a.endpoint < b.endpoint;
              });'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit("registry canonical sort block not found")
    text = text.replace(old, new, 1)
    print("applied: deterministic registry authority triple ordering")
p.write_text(text)

# Offline lookup matches the dependency's full authority tuple, not globally ambiguous alias alone.
p = Path("src/package.cpp")
text = p.read_text()
old = '''const LockRegistry* find_lock_registry(const ProjectLock& lock, const std::string& alias) {
    const LockRegistry* found = nullptr;
    for (const auto& registry : lock.registries) {
        if (registry.alias != alias) continue;
        if (found) throw std::runtime_error("offline lock contains duplicate registry alias '" + alias + "'");
        found = &registry;
    }
    return found;
}'''
new = '''const LockRegistry* find_lock_registry(const ProjectLock& lock,
                                       const std::string& alias,
                                       const std::string& id,
                                       const std::string& endpoint) {
    const LockRegistry* found = nullptr;
    for (const auto& registry : lock.registries) {
        if (registry.alias != alias || registry.id != id || registry.endpoint != endpoint) continue;
        if (found) {
            throw std::runtime_error("offline lock contains duplicate registry authority for alias '" + alias + "'");
        }
        found = &registry;
    }
    return found;
}'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit("full registry authority lookup block not found")
    text = text.replace(old, new, 1)

old_call = 'const auto* registry = find_lock_registry(lock, *locked.registry_alias);'
new_call = '''const auto* registry = find_lock_registry(lock, *locked.registry_alias,
                                                  *locked.registry_id,
                                                  *locked.registry_endpoint);'''
if new_call not in text:
    if text.count(old_call) != 1:
        raise SystemExit("registry authority lookup call not found")
    text = text.replace(old_call, new_call, 1)

old_check = '''        if (registry->id.empty() || registry->endpoint.empty() ||
            registry->id != *locked.registry_id || registry->endpoint != *locked.registry_endpoint) {
            throw std::runtime_error("offline registry dependency '" + locked.name +
                                     "' registry authority does not match lock registry record");
        }'''
new_check = '''        if (registry->id.empty() || registry->endpoint.empty()) {
            throw std::runtime_error("offline registry dependency '" + locked.name +
                                     "' registry authority record is incomplete");
        }'''
if new_check not in text:
    if text.count(old_check) != 1:
        raise SystemExit("registry authority post-check block not found")
    text = text.replace(old_check, new_check, 1)
p.write_text(text)
print("applied: full registry authority tuple validation")

# Legacy package regression fixtures predate canonical lock v3 spelling and used placeholder
# artifact hashes. Normalize those fixtures in their existing manifest-hash refresh helper so
# strict validation exercises each test's intended lower-level behavior.
p = Path("tests/package_tests.cpp")
text = p.read_text()
old = '''    std::string lock_text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::string prefix = "manifest_hash = \\\"";'''
new = '''    std::string lock_text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (lock_text.find("lock_version = 3") == std::string::npos) {
        const std::string legacy = "version = \\\"3\\\"\\n";
        const auto legacy_pos = lock_text.find(legacy);
        if (legacy_pos != std::string::npos) lock_text.replace(legacy_pos, legacy.size(), "lock_version = 3\\n");
        else lock_text.insert(0, "lock_version = 3\\n");
    }
    const std::string artifact_prefix = "artifact_sha256 = \\\"";
    std::size_t artifact_pos = 0;
    while ((artifact_pos = lock_text.find(artifact_prefix, artifact_pos)) != std::string::npos) {
        const auto value_begin = artifact_pos + artifact_prefix.size();
        const auto value_end = lock_text.find('\\\"', value_begin);
        if (value_end == std::string::npos) throw std::runtime_error("package test lock has malformed artifact_sha256");
        const auto value = lock_text.substr(value_begin, value_end - value_begin);
        if (value.size() != 64) {
            const auto normalized = emojineer::sha256_hex(value);
            lock_text.replace(value_begin, value.size(), normalized);
            artifact_pos = value_begin + normalized.size();
        } else {
            artifact_pos = value_end + 1;
        }
    }
    const std::string prefix = "manifest_hash = \\\"";'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit("package fixture normalizer anchor missing")
    text = text.replace(old, new, 1)
    print("applied: legacy package fixtures normalized to canonical lock v3 + SHA256 shape")
p.write_text(text)
