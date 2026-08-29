from pathlib import Path
import re


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


# Typed source diagnostics derive from std::exception, not std::runtime_error.
p = Path("tests/tests.cpp")
text = p.read_text()
old = 'catch(const std::runtime_error&e){require(std::string(e.what()).find("expects 1")!=std::string::npos,"arity error text");}'
new = 'catch(const std::exception&e){require(std::string(e.what()).find("expects 1")!=std::string::npos,"arity error text");}'
if new not in text:
    if text.count(old) != 1:
        raise SystemExit("arity diagnostic fixture: expected one catch site")
    p.write_text(text.replace(old, new, 1))
    print("applied: arity diagnostic catches typed source exception")
else:
    print("already applied: arity diagnostic catches typed source exception")

replace_once(
    "tests/module_tests.cpp",
    "    } catch (const std::runtime_error& error) {\n        const std::string message = error.what();",
    "    } catch (const std::exception& error) {\n        const std::string message = error.what();",
    "module expect_error catches typed source exception",
)

# JSON objects/arrays must not silently succeed when EOF arrives before their closing delimiter.
p = Path("src/lsp.cpp")
text = p.read_text()
obj_old = '''    bool expectCommaOrEnd = false;
    while (pos < json.size()) {
        skipWhitespace(json, pos);
        
        if (expectCommaOrEnd) {
            if (json[pos] == '}') {
                pos++;
                break;
            }'''
obj_new = '''    bool expectCommaOrEnd = false;
    bool closed = false;
    while (pos < json.size()) {
        skipWhitespace(json, pos);
        if (pos >= json.size()) break;
        
        if (expectCommaOrEnd) {
            if (json[pos] == '}') {
                pos++;
                closed = true;
                break;
            }'''
if obj_new not in text:
    if text.count(obj_old) != 1:
        raise SystemExit("JSON object delimiter authority: expected one parser block")
    text = text.replace(obj_old, obj_new, 1)
    obj_return = '''        expectCommaOrEnd = true;
    }
    return obj;
}

JsonValue parseJsonArray'''
    obj_return_new = '''        expectCommaOrEnd = true;
    }
    if (!closed) throw std::runtime_error("unterminated object");
    return obj;
}

JsonValue parseJsonArray'''
    if text.count(obj_return) != 1:
        raise SystemExit("JSON object delimiter authority: return block not found")
    text = text.replace(obj_return, obj_return_new, 1)
    print("applied: JSON object requires closing brace")
else:
    print("already applied: JSON object requires closing brace")

arr_old = '''    while (pos < json.size()) {
        skipWhitespace(json, pos);
        json::arrayPushBack(arr, parseJsonValue(json, pos));
        skipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == ']') {
            pos++;
            break;
        }
        if (pos < json.size() && json[pos] == ',') pos++;
    }
    return arr;
}'''
arr_new = '''    bool closed = false;
    while (pos < json.size()) {
        skipWhitespace(json, pos);
        if (pos >= json.size()) break;
        json::arrayPushBack(arr, parseJsonValue(json, pos));
        skipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == ']') {
            pos++;
            closed = true;
            break;
        }
        if (pos < json.size() && json[pos] == ',') pos++;
    }
    if (!closed) throw std::runtime_error("unterminated array");
    return arr;
}'''
if arr_new not in text:
    if text.count(arr_old) != 1:
        raise SystemExit("JSON array delimiter authority: expected one parser block")
    text = text.replace(arr_old, arr_new, 1)
    print("applied: JSON array requires closing bracket")
else:
    print("already applied: JSON array requires closing bracket")
p.write_text(text)

# Hand-authored registry-lock fixtures must carry the production manifest identity so the
# strict stale-lock gate tests the intended lower-level condition rather than a mystery hash.
p = Path("tests/package_tests.cpp")
text = p.read_text()
helper = '''
void refresh_lock_manifest_hash(const std::filesystem::path& root) {
    const auto lock_path = root / "emojineer.lock";
    if (!std::filesystem::exists(lock_path)) return;
    const auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    const auto manifest_hash = emojineer::project_manifest_hash(manifest);
    std::ifstream input(lock_path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read package test lock");
    std::string lock_text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::string prefix = "manifest_hash = \\\"";
    const auto begin = lock_text.find(prefix);
    if (begin == std::string::npos) throw std::runtime_error("package test lock is missing manifest_hash");
    const auto value_begin = begin + prefix.size();
    const auto value_end = lock_text.find('\\"', value_begin);
    if (value_end == std::string::npos) throw std::runtime_error("package test lock has malformed manifest_hash");
    lock_text.replace(value_begin, value_end - value_begin, manifest_hash);
    std::ofstream output(lock_path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot rewrite package test lock");
    output << lock_text;
}
'''
anchor = '''void write_source(const std::filesystem::path& path, const std::string& source) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write test source");
    output << source;
}
'''
if helper.strip() not in text:
    if text.count(anchor) != 1:
        raise SystemExit("package lock fixture helper anchor not found")
    text = text.replace(anchor, anchor + helper, 1)
    print("applied: package fixture production manifest-hash helper")
else:
    print("already applied: package fixture production manifest-hash helper")

# Refresh only at root-manifest load sites; no-lock fixtures remain no-lock because helper is a no-op.
needle = '    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");\n'
replacement = needle + '    refresh_lock_manifest_hash(root);\n'
if replacement not in text:
    count = text.count(needle)
    if count < 6:
        raise SystemExit(f"package lock fixture sites unexpectedly low: {count}")
    text = text.replace(needle, replacement)
    print(f"applied: refreshed manifest identity at {count} package fixture sites")
else:
    print("already applied: package fixture manifest identities")
p.write_text(text)

# Project regression fixtures also need current manifest identity for offline structural tests.
p = Path("tests/project_tests.cpp")
text = p.read_text()
if '#include "emojineer/registry_transport.hpp"' not in text:
    text = text.replace('#include "emojineer/project.hpp"\n', '#include "emojineer/project.hpp"\n#include "emojineer/registry_transport.hpp"\n', 1)
    print("applied: project test local-registry include")

project_helper = '''
void refresh_project_test_lock_manifest_hash(const std::filesystem::path& root) {
    const auto lock_path = root / "emojineer.lock";
    if (!std::filesystem::exists(lock_path)) return;
    const auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
    const auto manifest_hash = emojineer::project_manifest_hash(manifest);
    std::ifstream input(lock_path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read project test lock");
    std::string lock_text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::string prefix = "manifest_hash = \\\"";
    const auto begin = lock_text.find(prefix);
    if (begin == std::string::npos) throw std::runtime_error("project test lock is missing manifest_hash");
    const auto value_begin = begin + prefix.size();
    const auto value_end = lock_text.find('\\"', value_begin);
    if (value_end == std::string::npos) throw std::runtime_error("project test lock has malformed manifest_hash");
    lock_text.replace(value_begin, value_end - value_begin, manifest_hash);
    std::ofstream output(lock_path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot rewrite project test lock");
    output << lock_text;
}
'''
project_anchor = '''void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write project test file");
    output << text;
}
'''
if project_helper.strip() not in text:
    if text.count(project_anchor) != 1:
        raise SystemExit("project lock fixture helper anchor not found")
    text = text.replace(project_anchor, project_anchor + project_helper, 1)
    print("applied: project fixture production manifest-hash helper")

start = text.find('void test_registry_path_dependency_rejected_online() {')
end_marker = '// Regression test: path dependency in registry package must be rejected in offline mode'
end = text.find(end_marker, start)
if start == -1 or end == -1:
    raise SystemExit("online registry path-dependency test boundaries not found")
new_online = '''void test_registry_path_dependency_rejected_online() {
    const auto root = temp_root("reg-path-dep-online");
    const auto registry_root = root / "registry";
    const auto package_root = root / "mylib";
    const auto local_dep = package_root / "local-dep";
    const auto app_root = root / "app";
    const auto store_root = app_root / ".emojineer" / "packages";
    std::filesystem::remove_all(root);

    emojineer::initialize_file_registry(registry_root, "project-tests.local");
    const auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());

    emojineer::initialize_project(package_root, "mylib");
    emojineer::initialize_project(local_dep, "local-dep");
    auto package_manifest = emojineer::load_project_manifest(package_root / "emojineer.toml");
    package_manifest.version = "1.0.0";
    write_text(package_root / "emojineer.toml", emojineer::canonical_manifest_text(package_manifest));
    emojineer::add_project_dependency(package_root, "local-dep", "local-dep");
    (void)emojineer::publish_package_to_registry(package_root, endpoint);

    emojineer::initialize_project(app_root, "app");
    emojineer::add_project_registry_dependency(app_root, "mylib", "^1.0.0",
                                               registry_root.string(), "origin");
    const auto manifest = emojineer::load_project_manifest(app_root / "emojineer.toml");

    bool rejected = false;
    std::string error_msg;
    try {
        (void)emojineer::resolve_registry_dependencies(manifest, store_root, app_root, false);
    } catch (const std::runtime_error& e) {
        rejected = std::string(e.what()).find("path dependency") != std::string::npos;
        error_msg = e.what();
    }
    require(rejected,
            "registry package with path dependency should be rejected in ONLINE mode, got: " + error_msg);

    std::filesystem::remove_all(root);
}

'''
current_online = text[start:end]
if 'project-tests.local' not in current_online:
    text = text[:start] + new_online + text[end:]
    print("applied: online registry path-dependency test uses real file registry")
else:
    print("already applied: online registry path-dependency test uses real file registry")

# For remaining handcrafted offline locks, refresh the root manifest identity immediately after load.
needle = '    auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");\n'
replacement = needle + '    refresh_project_test_lock_manifest_hash(root);\n'
# Restrict replacement to the two legacy offline fixture functions by function slice.
for fn in ["test_registry_path_dependency_rejected_offline", "test_corrupted_offline_materialization_fails"]:
    fn_start = text.find(f"void {fn}() {{")
    if fn_start == -1:
        raise SystemExit(f"{fn}: function not found")
    next_fn = text.find("\nvoid ", fn_start + 6)
    if next_fn == -1:
        next_fn = text.find("\n} // namespace", fn_start)
    block = text[fn_start:next_fn]
    if replacement in block:
        continue
    if block.count(needle) != 1:
        raise SystemExit(f"{fn}: expected one root manifest load, got {block.count(needle)}")
    block = block.replace(needle, replacement, 1)
    text = text[:fn_start] + block + text[next_fn:]
    print(f"applied: {fn} current manifest identity")
p.write_text(text)
