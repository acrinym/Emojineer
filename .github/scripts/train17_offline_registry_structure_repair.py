from pathlib import Path

p = Path("src/project.cpp")
text = p.read_text()

old = '''                            if (std::filesystem::exists(resolved_dep.store_path / "emojineer.toml")) {
                                auto embedded_manifest = load_project_manifest(resolved_dep.store_path / "emojineer.toml");
                                resolved_dep.dependencies = embedded_manifest.dependencies;
                            }
                            
                            resolved[key] = resolved_dep;'''
new = '''                            if (std::filesystem::exists(resolved_dep.store_path / "emojineer.toml")) {
                                auto embedded_manifest = load_project_manifest(resolved_dep.store_path / "emojineer.toml");
                                resolved_dep.dependencies = embedded_manifest.dependencies;
                                for (const auto& embedded_dep : embedded_manifest.dependencies) {
                                    if (embedded_dep.kind == DependencyKind::Path) {
                                        resolving.erase(dep.name);
                                        throw std::runtime_error("registry package '" + resolved_dep.name + "'@'" + resolved_dep.version +
                                                                 "' contains path dependency '" + embedded_dep.name + "' which cannot be resolved by consumers");
                                    }
                                }
                            }
                            
                            resolved[key] = resolved_dep;'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f"offline registry structural validation: expected one block, got {text.count(old)}")
    text = text.replace(old, new, 1)
    print("applied: offline registry packages reject path dependencies")
else:
    print("already applied: offline registry packages reject path dependencies")

old_catch = '''                } catch (...) {
                    // Lock load failed, will try online resolution
                }
            }
            if (!resolved_from_lock) {'''
new_catch = '''                } catch (...) {
                    // Offline resolution is sovereign: malformed lock/materialization and
                    // structural package failures are terminal, never an implicit online fallback.
                    resolving.erase(dep.name);
                    throw;
                }
            }
            if (!resolved_from_lock) {'''
if new_catch not in text:
    if text.count(old_catch) != 1:
        raise SystemExit(f"offline registry failure propagation: expected one catch, got {text.count(old_catch)}")
    text = text.replace(old_catch, new_catch, 1)
    print("applied: offline lock/materialization failures propagate")
else:
    print("already applied: offline lock/materialization failures propagate")

p.write_text(text)
