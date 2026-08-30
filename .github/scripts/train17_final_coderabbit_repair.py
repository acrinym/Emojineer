from pathlib import Path
import re


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        raise SystemExit(f"missing replacement anchor: {label}")
    return text.replace(old, new, 1)


def replace_region(text: str, start: str, end: str, transform, label: str) -> str:
    a = text.find(start)
    if a < 0:
        raise SystemExit(f"missing region start: {label}")
    b = text.find(end, a)
    if b < 0:
        raise SystemExit(f"missing region end: {label}")
    region = text[a:b]
    updated = transform(region)
    return text[:a] + updated + text[b:]


# 1) Keep the public capability model aligned with the LSP wire types.
lsp_hpp_path = Path("include/emojineer/lsp.hpp")
lsp_hpp = lsp_hpp_path.read_text()
lsp_hpp = replace_once(
    lsp_hpp,
    '''struct ServerCapabilities {
    std::optional<bool> textDocumentSync;
    std::optional<bool> hoverProvider;
    std::optional<bool> completionProvider;
    std::optional<bool> definitionProvider;
    std::optional<bool> referencesProvider;
    std::optional<bool> documentSymbolProvider;
    std::optional<bool> workspaceSymbolProvider;
    std::optional<bool> documentFormattingProvider;
    std::optional<bool> documentRangeFormattingProvider;
};
''',
    '''enum class TextDocumentSyncKind {
    None = 0,
    Full = 1,
    Incremental = 2,
};

struct CompletionOptions {
    bool resolveProvider{false};
};

struct ServerCapabilities {
    std::optional<TextDocumentSyncKind> textDocumentSync;
    std::optional<bool> hoverProvider;
    std::optional<CompletionOptions> completionProvider;
    std::optional<bool> definitionProvider;
    std::optional<bool> referencesProvider;
    std::optional<bool> documentSymbolProvider;
    std::optional<bool> workspaceSymbolProvider;
    std::optional<bool> documentFormattingProvider;
    std::optional<bool> documentRangeFormattingProvider;
};
''',
    "LSP capability model",
)
lsp_hpp_path.write_text(lsp_hpp)


# 2) Preserve legacy std::runtime_error catch compatibility while carrying typed locations.
sd_path = Path("include/emojineer/source_diagnostic.hpp")
sd = sd_path.read_text()
sd = replace_once(
    sd,
    "struct SourceLocationException : public std::exception {",
    "struct SourceLocationException : public std::runtime_error {",
    "SourceLocationException base",
)
sd = replace_once(
    sd,
    '''        : message(msg), sourcePath(std::move(path)), line(ln), column(col), tokenLexeme(lexeme) {}''',
    '''        : std::runtime_error(msg), message(msg), sourcePath(std::move(path)), line(ln), column(col), tokenLexeme(lexeme) {}''',
    "SourceLocationException constructor one",
)
sd = replace_once(
    sd,
    '''        : message(msg), sourcePath(std::move(path)), sourceIdentity(std::move(identity)),
          line(ln), column(col), tokenLexeme(lexeme) {}''',
    '''        : std::runtime_error(msg), message(msg), sourcePath(std::move(path)), sourceIdentity(std::move(identity)),
          line(ln), column(col), tokenLexeme(lexeme) {}''',
    "SourceLocationException constructor two",
)
sd_path.write_text(sd)


# 3) Reject positions beyond EOL instead of silently clamping them.
lsp_path = Path("src/lsp.cpp")
lsp = lsp_path.read_text()

def repair_utf16_to_utf8(region: str) -> str:
    region = region.replace(
        '''            if (currentLine == line) {
                // At end of target line after CRLF
                return utf8Offset;
            }''',
        '''            if (currentLine == line) {
                // Only the exact end-of-line UTF-16 position is valid.
                return currentCol == utf16Col ? std::optional<std::size_t>(utf8Offset) : std::nullopt;
            }''')
    region = region.replace(
        '''            if (currentLine == line) {
                // At end of target line
                return utf8Offset;
            }''',
        '''            if (currentLine == line) {
                // Only the exact end-of-line UTF-16 position is valid.
                return currentCol == utf16Col ? std::optional<std::size_t>(utf8Offset) : std::nullopt;
            }''')
    region = region.replace(
        '''            if (currentLine == line) {
                return utf8Offset;
            }''',
        '''            if (currentLine == line) {
                return currentCol == utf16Col ? std::optional<std::size_t>(utf8Offset) : std::nullopt;
            }''')
    region = region.replace(
        "if (currentLine == line && currentCol <= utf16Col)",
        "if (currentLine == line && currentCol == utf16Col)")
    if "currentCol <= utf16Col" in region:
        raise SystemExit("utf16ToUtf8 still clamps beyond EOL")
    return region

lsp = replace_region(
    lsp,
    "std::optional<std::size_t> LanguageServer::utf16ToUtf8",
    "// Convert grapheme column to UTF-16 column in a line",
    repair_utf16_to_utf8,
    "utf16ToUtf8",
)

def repair_grapheme_col(region: str) -> str:
    region = region.replace("if (currentUtf16 <= utf16Col)", "if (currentUtf16 == utf16Col)")
    if "currentUtf16 <= utf16Col" in region:
        raise SystemExit("utf16ColumnToGraphemeColumn still clamps beyond EOL")
    return region

lsp = replace_region(
    lsp,
    "std::optional<std::size_t> utf16ColumnToGraphemeColumn",
    "// Find the byte offset in a line for a given UTF-16 column",
    repair_grapheme_col,
    "utf16ColumnToGraphemeColumn",
)

def repair_line_offset(region: str) -> str:
    region = region.replace("    bool inSurrogatePair = false;\n", "")
    region = region.replace("if (currentUtf16 <= utf16Col)", "if (currentUtf16 == utf16Col)")
    if "currentUtf16 <= utf16Col" in region:
        raise SystemExit("utf16ColumnToLineOffset still clamps beyond EOL")
    return region

lsp = replace_region(
    lsp,
    "std::optional<std::size_t> utf16ColumnToLineOffset",
    "// Find the byte offset for a UTF-16 position in full text",
    repair_line_offset,
    "utf16ColumnToLineOffset",
)
lsp_path.write_text(lsp)


# 4) Attribute module-linker typed failures to the actual source module.
module_path = Path("src/module.cpp")
module = module_path.read_text()

def repair_nested(region: str) -> str:
    region = replace_once(
        region,
        '''void reject_nested_module_syntax(const std::vector<ast::StmtPtr>& block,
                                 const std::string& identity) {''',
        '''void reject_nested_module_syntax(const std::vector<ast::StmtPtr>& block,
                                 const std::filesystem::path& source_path,
                                 const std::string& identity) {''',
        "nested syntax signature",
    )
    region = replace_once(
        region,
        '''            throw SourceLocationException("🧩, 🔗, and 📤 are top-level only", 
                                             {}, stmt->line, 1);''',
        '''            throw SourceLocationException("🧩, 🔗, and 📤 are top-level only",
                                             source_path, identity, stmt->line, 1);''',
        "nested syntax typed source",
    )
    region = region.replace("reject_nested_module_syntax(branch->then_branch, identity);",
                            "reject_nested_module_syntax(branch->then_branch, source_path, identity);")
    region = region.replace("reject_nested_module_syntax(branch->else_branch, identity);",
                            "reject_nested_module_syntax(branch->else_branch, source_path, identity);")
    region = region.replace("reject_nested_module_syntax(loop->body, identity);",
                            "reject_nested_module_syntax(loop->body, source_path, identity);")
    region = region.replace("reject_nested_module_syntax(fn->body, identity);",
                            "reject_nested_module_syntax(fn->body, source_path, identity);")
    return region

module = replace_region(
    module,
    "void reject_nested_module_syntax",
    "void collect_module_globals",
    repair_nested,
    "nested module syntax",
)
module = module.replace("reject_nested_module_syntax(branch->then_branch, unit.identity);",
                        "reject_nested_module_syntax(branch->then_branch, unit.path, unit.identity);")
module = module.replace("reject_nested_module_syntax(branch->else_branch, unit.identity);",
                        "reject_nested_module_syntax(branch->else_branch, unit.path, unit.identity);")
module = module.replace("reject_nested_module_syntax(loop->body, unit.identity);",
                        "reject_nested_module_syntax(loop->body, unit.path, unit.identity);")
module = module.replace("reject_nested_module_syntax(fn->body, unit.identity);",
                        "reject_nested_module_syntax(fn->body, unit.path, unit.identity);")

def repair_local_import(region: str) -> str:
    region = region.replace("                                             {}, spec.line, 1",
                            "                                             importer.path, importer.identity, spec.line, 1")
    if "{}, spec.line, 1" in region:
        raise SystemExit("local import typed source ownership still missing")
    return region

module = replace_region(
    module,
    "std::filesystem::path resolve_local_import",
    "ResolvedSourceImport resolve_package_import",
    repair_local_import,
    "local import typed source",
)
module_path.write_text(module)


# 5) Never treat package-manager state as package-owned source.
package_path = Path("src/package.cpp")
package = package_path.read_text()

def prune_package_state(region: str) -> str:
    anchor = '''        if (it->is_directory()) {
            std::error_code ec;'''
    replacement = '''        if (it->is_directory()) {
            if (path == root / ".emojineer") {
                it.disable_recursion_pending();
                continue;
            }
            std::error_code ec;'''
    return replace_once(region, anchor, replacement, "package hash .emojineer prune")

package = replace_region(
    package,
    "std::string package_hash",
    "// Compute hash for a registry package",
    prune_package_state,
    "package_hash",
)
package_path.write_text(package)

artifact_path = Path("src/package_artifact.cpp")
artifact_cpp = artifact_path.read_text()

def prune_artifact_state(region: str) -> str:
    anchor = '''        if (it->is_directory()) {
            std::error_code error;'''
    replacement = '''        if (it->is_directory()) {
            if (path == root / ".emojineer") {
                it.disable_recursion_pending();
                continue;
            }
            std::error_code error;'''
    return replace_once(region, anchor, replacement, "artifact .emojineer prune")

artifact_cpp = replace_region(
    artifact_cpp,
    "std::vector<PackageArtifactFile> collect_owned_sources",
    "class Reader",
    prune_artifact_state,
    "collect_owned_sources",
)
artifact_path.write_text(artifact_cpp)


# 6) Reuse a current lock/materialized store when producing canonical lock text;
# only absent/stale state may enter online package-manager resolution.
project_path = Path("src/project.cpp")
project = project_path.read_text()
old_lock_body = '''    // Resolve registry dependencies for canonical lock production
    auto store_root = package_store_root(root);
    std::unordered_map<std::string, ResolvedRegistryDependency> resolved;
    std::unordered_set<std::string> resolving;
    auto resolved_deps = resolve_registry_dependencies_impl(manifest, store_root, root, false, resolved, resolving);
'''
new_lock_body = '''    // Reuse a current lock/materialized store without contacting registry authority.
    // Only an absent or stale lock enters online package-manager resolution.
    auto store_root = package_store_root(root);
    bool offline = false;
    const auto lock_path = root / "emojineer.lock";
    if (std::filesystem::exists(lock_path)) {
        try {
            const auto existing_lock = load_project_lock(lock_path);
            offline = !is_lock_stale(root, manifest, existing_lock);
        } catch (...) {
            // A malformed lock is not reusable; canonical lock production may replace it.
            offline = false;
        }
    }
    std::unordered_map<std::string, ResolvedRegistryDependency> resolved;
    std::unordered_set<std::string> resolving;
    auto resolved_deps = resolve_registry_dependencies_impl(
        manifest, store_root, root, offline, resolved, resolving);
'''
project = replace_once(project, old_lock_body, new_lock_body, "canonical lock offline reuse")

# Persist dependency edges discovered from the embedded registry manifest into
# the objects already stored in both the resolved map and result vector.
project = replace_once(
    project,
    '''                // Populate dependencies from embedded manifest for lock serialization
                resolved_dep.dependencies = embedded_manifest.dependencies;
                
                // Structural validation:''',
    '''                // Populate dependencies from embedded manifest for lock serialization.
                resolved_dep.dependencies = embedded_manifest.dependencies;
                resolved[key] = resolved_dep;
                result.back() = resolved_dep;
                
                // Structural validation:''',
    "online resolved dependency edge persistence",
)

# Make materialization replacement deterministic and able to replace corrupt state.
project = replace_once(
    project,
    '''    // Create staging directory
    auto staging = pkg_path.parent_path() / ".staging";
    std::filesystem::create_directories(staging);''',
    '''    // Create a clean artifact-specific staging directory.
    auto staging = pkg_path.parent_path() / (".staging-" + artifact_sha256);
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);''',
    "materialization staging",
)
project = replace_once(
    project,
    '''    // Atomically move staging to final location
    std::filesystem::rename(staging, pkg_path);''',
    '''    // Replace corrupt/missing materialization with the fully written staging tree.
    if (std::filesystem::exists(pkg_path)) {
        std::filesystem::remove_all(pkg_path);
    }
    std::filesystem::rename(staging, pkg_path);''',
    "materialization replacement",
)

# Repair from the actual content-addressed registry cache, restore all artifact
# sources, and verify the repaired package before returning.
old_repair = '''                    auto cache_path = cache_root.empty() 
                        ? default_registry_cache_root() / dep.name / dep.version / *dep.artifact_sha256
                        : cache_root / dep.name / dep.version / *dep.artifact_sha256;
                    
                    if (std::filesystem::exists(cache_path)) {
                        // Re-materialize from cache
                        auto artifact = load_package_artifact(cache_path);
                        materialize_package(store_root,
                                           registry_key(dep.registry_alias ? *dep.registry_alias : "origin"),
                                           dep.name,
                                           dep.version,
                                           *dep.artifact_sha256,
                                           {},
                                           artifact.manifest);'''
new_repair = '''                    if (!dep.registry_endpoint || !dep.registry_id) {
                        throw std::runtime_error("cannot repair package " + dep.name + "@" + dep.version +
                                                 " - lock is missing registry authority");
                    }
                    PackageArtifact expected;
                    expected.name = dep.name;
                    expected.version = dep.version;
                    expected.artifact_sha256 = *dep.artifact_sha256;
                    const auto registry_cache_key = sha256_hex(
                        *dep.registry_endpoint + "\\n" + *dep.registry_id).substr(0, 32);
                    const auto cache_base = (cache_root.empty() ? default_registry_cache_root() : cache_root) /
                                            "registries" / registry_cache_key;
                    auto cache_path = package_cache_path(cache_base, expected);
                    
                    if (std::filesystem::exists(cache_path)) {
                        auto artifact = load_package_artifact(cache_path);
                        if (artifact.name != dep.name || artifact.version != dep.version ||
                            artifact.artifact_sha256 != *dep.artifact_sha256 ||
                            artifact.content_sha256 != *dep.content_sha256) {
                            throw std::runtime_error("cached repair artifact identity mismatch for " +
                                                     dep.name + "@" + dep.version);
                        }
                        std::vector<std::pair<std::string, std::string>> files;
                        files.reserve(artifact.files.size());
                        for (const auto& file : artifact.files) {
                            files.emplace_back(file.path, file.source);
                        }
                        materialize_package(store_root,
                                           registry_key(dep.registry_alias ? *dep.registry_alias : "origin"),
                                           dep.name,
                                           dep.version,
                                           *dep.artifact_sha256,
                                           files,
                                           artifact.manifest);
                        if (!is_materialized_package_valid(pkg_path, *dep.content_sha256)) {
                            throw std::runtime_error("repaired package failed content verification: " +
                                                     dep.name + "@" + dep.version);
                        }'''
project = replace_once(project, old_repair, new_repair, "materialization repair from cache")
project_path.write_text(project)


# 7) Strengthen tests so Release false positives cannot hide these exact defects.
source_test_path = Path("tests/source_diagnostic_tests.cpp")
source_test = source_test_path.read_text()
source_test = replace_once(
    source_test,
    '''    // BMP emoji (like 😀 U+1F600) - 1 UTF-16 unit (in BMP)
    assert(countUtf16UnitsInString("😀") == 1);
    
    // Mixed ASCII and emoji
    assert(countUtf16UnitsInString("a😀b") == 4);  // 'a' + 😀 + 'b' = 1 + 1 + 1 = 3... wait, that's 3
    // Actually: 'a' (1), '😀' (1), 'b' (1) = 3
    assert(countUtf16UnitsInString("a😀b") == 3);''',
    '''    // U+1F600 is supplementary and therefore occupies a UTF-16 surrogate pair.
    assert(countUtf16UnitsInString("😀") == 2);
    
    // Mixed ASCII and supplementary emoji: 1 + 2 + 1 = 4 UTF-16 units.
    assert(countUtf16UnitsInString("a😀b") == 4);''',
    "source diagnostic UTF-16 expectations",
)
source_test_path.write_text(source_test)

project_test_path = Path("tests/project_tests.cpp")
project_test = project_test_path.read_text()
project_test = replace_once(
    project_test,
    '#include "emojineer/project.hpp"\n',
    '#include "emojineer/project.hpp"\n#include "emojineer/package_artifact.hpp"\n#include "emojineer/hash.hpp"\n',
    "project test artifact includes",
)
project_test = project_test.replace('lock_out << "lock_version = \\"3\\"\\n";',
                                    'lock_out << "lock_version = 3\\n";')
project_test = project_test.replace('lock_out << "registry_alias = \\"origin\\"\\n";',
                                    'lock_out << "registry = \\"origin\\"\\n";')
project_test = replace_once(
    project_test,
    '''    require(failed, "corrupted offline materialization should fail, not disappear from graph, got: " + error_msg);''',
    '''    require(failed, "corrupted offline materialization should fail, not disappear from graph, got: " + error_msg);
    require(error_msg == "manifest [package] requires name, version, and entry",
            "corrupted materialization must fail for the intended manifest corruption, got: " + error_msg);''',
    "corrupt materialization exact failure",
)

if "test_verify_or_repair_materialization_restores_sources" not in project_test:
    marker = "\n} // namespace\n\nint main() {"
    if marker not in project_test:
        raise SystemExit("project test namespace/main marker missing")
    regression = r'''

void test_verify_or_repair_materialization_restores_sources() {
    const auto root = temp_root("repair-materialization");
    const auto source_root = root / "artifact-source";
    const auto cache_root = root / "cache";
    std::filesystem::remove_all(root);
    emojineer::initialize_project(source_root, "mylib");

    const auto bytes = emojineer::build_package_artifact_bytes(source_root);
    const auto artifact = emojineer::parse_package_artifact(bytes);
    const std::string endpoint = "https://registry.example.com";
    const std::string registry_id = "origin-id";
    const std::string registry_cache_key =
        emojineer::sha256_hex(endpoint + "\n" + registry_id).substr(0, 32);
    const auto cache_path = emojineer::package_cache_path(
        cache_root / "registries" / registry_cache_key, artifact);
    std::filesystem::create_directories(cache_path.parent_path());
    write_text(cache_path, bytes);

    const auto store_root = emojineer::package_store_root(root);
    const auto package_path = store_root / "origin" / artifact.name / artifact.version /
                              artifact.artifact_sha256;
    std::filesystem::create_directories(package_path / "src");
    write_text(package_path / "emojineer.toml", artifact.manifest);
    write_text(package_path / artifact.entry, "📝 📜corrupt📜\n");
    require(!emojineer::is_materialized_package_valid(package_path, artifact.content_sha256),
            "fixture must begin corrupt");

    emojineer::ProjectLock lock;
    lock.version = "3";
    emojineer::LockDependency dependency;
    dependency.source = emojineer::LockSourceKind::Registry;
    dependency.name = artifact.name;
    dependency.version = artifact.version;
    dependency.registry_alias = "origin";
    dependency.registry_id = registry_id;
    dependency.registry_endpoint = endpoint;
    dependency.requirement = "^0.1.0";
    dependency.artifact_sha256 = artifact.artifact_sha256;
    dependency.content_sha256 = artifact.content_sha256;
    dependency.store_path = package_path.generic_string();
    lock.dependencies.push_back(dependency);

    emojineer::verify_or_repair_materialization(root, lock, cache_root);
    require(emojineer::is_materialized_package_valid(package_path, artifact.content_sha256),
            "repair must restore exact materialized content");
    require(std::filesystem::is_regular_file(package_path / artifact.entry),
            "repair must restore artifact source files, not only the manifest");

    std::filesystem::remove_all(root);
}
'''
    project_test = project_test.replace(marker, regression + marker, 1)
    project_test = project_test.replace(
        "        test_sync_project_check_project_no_stale_lock();",
        "        test_sync_project_check_project_no_stale_lock();\n        test_verify_or_repair_materialization_restores_sources();",
        1,
    )
project_test_path.write_text(project_test)

artifact_test_path = Path("tests/package_artifact_tests.cpp")
artifact_test = artifact_test_path.read_text()
artifact_test = replace_once(
    artifact_test,
    '''    const auto before = a;
    write_text(first.path / "app/deps/lib/src/main.emoji", "📝 📜dependency changed📜\\n");''',
    '''    const auto before = a;
    write_text(first.path / "app/.emojineer/packages/injected/0.1.0/hash/src/hidden.emoji",
               "📝 📜package manager state📜\\n");
    const auto after_package_state = emojineer::build_package_artifact_bytes(first.path / "app");
    require(before == after_package_state,
            ".emojineer package-manager state must never affect package hash/artifact ownership");

    write_text(first.path / "app/deps/lib/src/main.emoji", "📝 📜dependency changed📜\\n");''',
    "artifact package-state exclusion regression",
)
artifact_test_path.write_text(artifact_test)

lsp_test_path = Path("tests/lsp_tests.cpp")
lsp_test = lsp_test_path.read_text()
if "test_utf16_rejects_positions_beyond_line_end" not in lsp_test:
    main_match = re.search(r"\nint\s+main\s*\([^)]*\)\s*\{", lsp_test)
    if not main_match:
        raise SystemExit("lsp test main marker missing")
    regression = r'''

void test_utf16_rejects_positions_beyond_line_end() {
    LanguageServer server;

    auto ascii_end = server.utf16ToUtf8("a", 0, 1);
    assert(ascii_end && *ascii_end == 1);
    assert(!server.utf16ToUtf8("a", 0, 2) &&
           "UTF-16 columns beyond ASCII EOL must be rejected");

    const std::string supplementary = "🍎";
    auto emoji_end = server.utf16ToUtf8(supplementary, 0, 2);
    assert(emoji_end && *emoji_end == supplementary.size());
    assert(!server.utf16ToUtf8(supplementary, 0, 1) &&
           "mid-surrogate positions must remain invalid");
    assert(!server.utf16ToUtf8(supplementary, 0, 3) &&
           "UTF-16 columns beyond supplementary EOL must be rejected");

    const std::string crlf = "a\r\nb";
    auto first_line_end = server.utf16ToUtf8(crlf, 0, 1);
    assert(first_line_end && *first_line_end == 1);
    assert(!server.utf16ToUtf8(crlf, 0, 2) &&
           "columns beyond a CRLF-terminated line must be rejected");
}
'''
    insert_at = main_match.start() + 1
    lsp_test = lsp_test[:insert_at] + regression + lsp_test[insert_at:]
    main_match = re.search(r"int\s+main\s*\([^)]*\)\s*\{", lsp_test)
    lsp_test = lsp_test[:main_match.end()] + "\n    test_utf16_rejects_positions_beyond_line_end();" + lsp_test[main_match.end():]
lsp_test_path.write_text(lsp_test)

print("repaired: final Train 17 exact-current findings and Debug-visible regressions")
