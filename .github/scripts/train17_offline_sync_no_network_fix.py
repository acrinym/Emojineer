from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)

project_path = Path("src/project.cpp")
project = project_path.read_text()
project = replace_once(
    project,
    '''                    const auto expected_endpoint = parse_registry_endpoint(owner_registry->endpoint);\n                    const auto expected_registry_id = registry_identity(expected_endpoint);\n\n''',
    '''                    // Canonicalizing the endpoint is local/string-only. Do NOT call\n                    // registry_identity() here: file/HTTPS identity discovery is network/authority I/O\n                    // and ordinary offline resolution must never contact the registry.\n                    const auto expected_endpoint = parse_registry_endpoint(owner_registry->endpoint);\n\n''',
    "project offline identity I/O removal")
project = replace_once(
    project,
    '''                            lock_dep.requirement && *lock_dep.requirement == dep.requirement &&\n                            lock_dep.registry_id && *lock_dep.registry_id == expected_registry_id &&\n                            lock_dep.registry_endpoint && *lock_dep.registry_endpoint == expected_endpoint.canonical) {\n''',
    '''                            lock_dep.requirement && *lock_dep.requirement == dep.requirement &&\n                            lock_dep.registry_endpoint && *lock_dep.registry_endpoint == expected_endpoint.canonical) {\n''',
    "project owner endpoint match")
project_path.write_text(project)

package_path = Path("src/package.cpp")
package = package_path.read_text()
package = replace_once(
    package,
    '''                    const auto expected_endpoint = parse_registry_endpoint(owner_registry->endpoint);\n                    const auto expected_registry_id = registry_identity(expected_endpoint);\n                    if (!lock_dep->registry_id || *lock_dep->registry_id != expected_registry_id ||\n                        !lock_dep->registry_endpoint ||\n                        *lock_dep->registry_endpoint != expected_endpoint.canonical) {\n''',
    '''                    // Owner binding is the canonical endpoint declared by this package.\n                    // The persisted registry ID remains validated against the lock's own\n                    // [[registry]] authority record; never rediscover it from the network offline.\n                    const auto expected_endpoint = parse_registry_endpoint(owner_registry->endpoint);\n                    if (!lock_dep->registry_endpoint ||\n                        *lock_dep->registry_endpoint != expected_endpoint.canonical) {\n''',
    "PackageGraph network-free owner authority")
package_path.write_text(package)
print("applied: offline owner-authority validation remains network-free")
