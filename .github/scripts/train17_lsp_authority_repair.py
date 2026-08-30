from pathlib import Path
import re


def replace_region(text: str, start: str, end: str, replacement: str) -> str:
    a = text.find(start)
    if a < 0:
        raise SystemExit(f"missing start marker: {start}")
    b = text.find(end, a)
    if b < 0:
        raise SystemExit(f"missing end marker: {end}")
    return text[:a] + replacement + text[b:]


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old in text:
        return text.replace(old, new, 1)
    if new in text:
        return text
    raise SystemExit(f"missing replacement anchor: {label}")


lsp_path = Path("src/lsp.cpp")
lsp = lsp_path.read_text()

exact_definitions = r'''static std::optional<Range> exactIdentifierTokenRange(
    const std::string& source,
    const std::vector<Token>& tokens,
    std::size_t astLine,
    const std::string& identifier) {
    for (const auto& token : tokens) {
        if (token.line == astLine && token.kind == TokenKind::Identifier &&
            (token.lexeme == identifier || token.canonical == identifier)) {
            return tokenToRange(source, token);
        }
    }
    return std::nullopt;
}

std::vector<SymbolLocation> LanguageServer::findDefinitions(const std::string& uri, const Position& pos) {
    std::vector<SymbolLocation> results;

    auto doc = getDocument(uri);
    if (!doc) return results;

    auto identifier = findIdentifierAtPosition(doc->text, pos);
    if (!identifier) return results;

    auto tokensOpt = getTokens(uri);
    if (!tokensOpt) return results;
    const auto& tokens = tokensOpt->get();

    auto programOpt = getOrParseProgram(uri);
    if (!programOpt) return results;
    const auto& program = programOpt->get();

    auto searchInProgram = [&](const std::string& searchUri,
                               const std::string& searchSource,
                               const ast::Program& searchProgram,
                               const std::vector<Token>& searchTokens) {
        for (const auto& stmt : searchProgram.statements) {
            std::string declarationName;
            std::string symbolKind;
            if (auto* funcDecl = dynamic_cast<const ast::FunctionDecl*>(stmt.get())) {
                declarationName = funcDecl->name;
                symbolKind = "function";
            } else if (auto* varDecl = dynamic_cast<const ast::VarDecl*>(stmt.get())) {
                declarationName = varDecl->name;
                symbolKind = "variable";
            } else if (auto* modDecl = dynamic_cast<const ast::ModuleDecl*>(stmt.get())) {
                declarationName = modDecl->name;
                symbolKind = "module";
            } else {
                continue;
            }

            if (declarationName != *identifier) continue;
            auto exact = exactIdentifierTokenRange(
                searchSource, searchTokens, stmt->line, declarationName);
            if (!exact) continue;

            SymbolLocation loc;
            loc.uri = searchUri;
            loc.range = *exact;
            loc.name = declarationName;
            loc.symbolKind = symbolKind;
            results.push_back(std::move(loc));
        }
    };

    searchInProgram(uri, doc->text, program, tokens);

    for (const auto& [otherUri, otherDoc] : openDocuments_) {
        if (otherUri == uri) continue;
        if (workspaceRoot_ && packageGraph_) {
            auto visibleRoots = visibleSourceRoots(
                *workspaceRoot_, manifest_, *packageGraph_, std::filesystem::path(uriToPath(uri)));
            if (!sourcePathIsVisible(std::filesystem::path(uriToPath(otherUri)),
                                     *workspaceRoot_, *packageGraph_, visibleRoots)) {
                continue;
            }
        }

        auto otherTokensOpt = getTokens(otherUri);
        if (!otherTokensOpt) continue;
        auto otherProgramOpt = getOrParseProgram(otherUri);
        if (!otherProgramOpt) continue;
        searchInProgram(otherUri, otherDoc.text, otherProgramOpt->get(), otherTokensOpt->get());
    }

    if (workspaceRoot_ && packageGraph_) {
        const std::filesystem::path root = *workspaceRoot_;
        auto authorizedPaths = visibleSourceRoots(
            root, manifest_, *packageGraph_, std::filesystem::path(uriToPath(uri)));

        auto searchFile = [&](const std::filesystem::path& path) {
            if (!std::filesystem::is_regular_file(path) ||
                (path.extension() != ".emj" && path.extension() != ".emoji")) {
                return;
            }
            const std::string moduleUri = pathToUri(path);
            if (openDocuments_.count(moduleUri)) return;
            try {
                std::string source = readFile(path);
                CustomEmojiRegistry reg = registry_;
                Lexer lexer(source, reg);
                auto modTokens = lexer.tokenize();
                auto parserTokens = modTokens;
                Parser parser(std::move(parserTokens));
                auto modProgram = parser.parse();
                searchInProgram(moduleUri, source, modProgram, modTokens);
            } catch (...) {
                // An unreadable or unparsable candidate is not a definition source.
            }
        };

        for (const auto& authorizedPath : authorizedPaths) {
            if (!std::filesystem::exists(authorizedPath)) continue;
            try {
                if (std::filesystem::is_directory(authorizedPath)) {
                    for (const auto& entry : std::filesystem::directory_iterator(authorizedPath)) {
                        searchFile(entry.path());
                    }
                } else {
                    searchFile(authorizedPath);
                }
            } catch (...) {
                // Keep semantic requests resilient to inaccessible authorized roots.
            }
        }
    }

    return results;
}

'''

lsp = replace_region(
    lsp,
    "std::vector<SymbolLocation> LanguageServer::findDefinitions",
    "// Helper to find references in expressions - forward declaration",
    exact_definitions,
)

exact_references = r'''static std::set<std::pair<std::size_t, std::size_t>> declarationTokenPositions(
    const ast::Program& program,
    const std::vector<Token>& tokens,
    const std::string& target) {
    std::set<std::pair<std::size_t, std::size_t>> positions;
    for (const auto& stmt : program.statements) {
        std::string declarationName;
        if (auto* funcDecl = dynamic_cast<const ast::FunctionDecl*>(stmt.get())) {
            declarationName = funcDecl->name;
        } else if (auto* varDecl = dynamic_cast<const ast::VarDecl*>(stmt.get())) {
            declarationName = varDecl->name;
        } else if (auto* modDecl = dynamic_cast<const ast::ModuleDecl*>(stmt.get())) {
            declarationName = modDecl->name;
        } else {
            continue;
        }
        if (declarationName != target) continue;
        for (const auto& token : tokens) {
            if (token.line == stmt->line && token.kind == TokenKind::Identifier &&
                (token.lexeme == target || token.canonical == target)) {
                positions.emplace(token.line, token.column);
                break;
            }
        }
    }
    return positions;
}

static void collectExactReferences(
    const std::string& uri,
    const std::string& source,
    const ast::Program& program,
    const std::vector<Token>& tokens,
    const std::string& target,
    bool includeDeclaration,
    std::vector<SymbolLocation>& results) {
    const auto declarations = declarationTokenPositions(program, tokens, target);
    for (const auto& token : tokens) {
        if (token.kind != TokenKind::Identifier ||
            (token.lexeme != target && token.canonical != target)) {
            continue;
        }
        const bool isDeclaration = declarations.count({token.line, token.column}) != 0;
        if (isDeclaration && !includeDeclaration) continue;

        SymbolLocation loc;
        loc.uri = uri;
        loc.range = tokenToRange(source, token);
        loc.name = target;
        loc.symbolKind = isDeclaration ? "declaration" : "reference";
        results.push_back(std::move(loc));
    }
}

std::vector<SymbolLocation> LanguageServer::findReferences(
    const std::string& uri,
    const Position& pos,
    bool includeDeclaration) {
    std::vector<SymbolLocation> results;

    auto doc = getDocument(uri);
    if (!doc) return results;
    auto identifier = findIdentifierAtPosition(doc->text, pos);
    if (!identifier) return results;

    auto tokensOpt = getTokens(uri);
    if (!tokensOpt) return results;
    auto programOpt = getOrParseProgram(uri);
    if (!programOpt) return results;
    collectExactReferences(uri, doc->text, programOpt->get(), tokensOpt->get(),
                           *identifier, includeDeclaration, results);

    for (const auto& [otherUri, otherDoc] : openDocuments_) {
        if (otherUri == uri) continue;
        if (workspaceRoot_ && packageGraph_) {
            auto visibleRoots = visibleSourceRoots(
                *workspaceRoot_, manifest_, *packageGraph_, std::filesystem::path(uriToPath(uri)));
            if (!sourcePathIsVisible(std::filesystem::path(uriToPath(otherUri)),
                                     *workspaceRoot_, *packageGraph_, visibleRoots)) {
                continue;
            }
        }
        auto otherTokensOpt = getTokens(otherUri);
        if (!otherTokensOpt) continue;
        auto otherProgramOpt = getOrParseProgram(otherUri);
        if (!otherProgramOpt) continue;
        collectExactReferences(otherUri, otherDoc.text, otherProgramOpt->get(),
                               otherTokensOpt->get(), *identifier, includeDeclaration, results);
    }

    if (workspaceRoot_ && packageGraph_) {
        const std::filesystem::path root = *workspaceRoot_;
        auto authorizedPaths = visibleSourceRoots(
            root, manifest_, *packageGraph_, std::filesystem::path(uriToPath(uri)));

        auto searchFile = [&](const std::filesystem::path& path) {
            if (!std::filesystem::is_regular_file(path) ||
                (path.extension() != ".emj" && path.extension() != ".emoji")) {
                return;
            }
            const std::string moduleUri = pathToUri(path);
            if (openDocuments_.count(moduleUri)) return;
            try {
                std::string source = readFile(path);
                CustomEmojiRegistry reg = registry_;
                Lexer lexer(source, reg);
                auto modTokens = lexer.tokenize();
                auto parserTokens = modTokens;
                Parser parser(std::move(parserTokens));
                auto modProgram = parser.parse();
                collectExactReferences(moduleUri, source, modProgram, modTokens,
                                       *identifier, includeDeclaration, results);
            } catch (...) {
                // Ignore unreadable or unparsable candidates.
            }
        };

        for (const auto& authorizedPath : authorizedPaths) {
            if (!std::filesystem::exists(authorizedPath)) continue;
            try {
                if (std::filesystem::is_directory(authorizedPath)) {
                    for (const auto& entry : std::filesystem::directory_iterator(authorizedPath)) {
                        searchFile(entry.path());
                    }
                } else {
                    searchFile(authorizedPath);
                }
            } catch (...) {
                // Keep references resilient to inaccessible authorized roots.
            }
        }
    }

    return results;
}

'''

lsp = replace_region(
    lsp,
    "// Helper to find references in expressions - forward declaration",
    "JsonValue LanguageServer::handleCompletion",
    exact_references,
)

if "token.column > 0 ? token.column - 1" in lsp[lsp.find("std::vector<SymbolLocation> LanguageServer::findDefinitions"):lsp.find("JsonValue LanguageServer::handleCompletion")]:
    raise SystemExit("grapheme-based semantic location reconstruction remains")
if "searchInProgram(moduleUri, modProgram, {})" in lsp:
    raise SystemExit("disk definition scan still discards tokens")

lsp_path.write_text(lsp)

# Strengthen the real child-process mixed-workspace acceptance. This fixture must
# be a valid offline root graph with two direct packages and one path transitive.
test_path = Path("tests/lsp_tests.cpp")
test = test_path.read_text()

test = replace_once(
    test,
    '#include "emojineer/project.hpp"\n',
    '#include "emojineer/project.hpp"\n#include "emojineer/registry_transport.hpp"\n',
    "registry transport include",
)

test = replace_once(
    test,
    '    void createProject() {\n        // Create emojineer.toml with path package dependency (using canonical format)\n',
    '    void createProject() {\n        const auto registryEndpoint = parse_registry_endpoint("https://emojineer.pkg.example.com");\n        // Create emojineer.toml with direct path + materialized registry dependencies.\n',
    "fixture registry endpoint",
)

old_root_manifest = '''        std::ofstream toml(rootPath / "emojineer.toml");
        toml << "[package]\\n"
             << "name = \\"testapp\\"\\n"
             << "version = \\"0.1.0\\"\\n"
             << "entry = \\"main.emoji\\"\\n"
             << "\\n"
             << "[dependencies]\\n"
             << "path-pkg = \\"./path-pkg\\"\\n";
        toml.close();
'''
new_root_manifest = '''        std::ofstream toml(rootPath / "emojineer.toml");
        toml << "[package]\\n"
             << "name = \\"testapp\\"\\n"
             << "version = \\"0.1.0\\"\\n"
             << "entry = \\"main.emoji\\"\\n"
             << "\\n"
             << "[registries]\\n"
             << "registry = \\"" << registryEndpoint.canonical << "\\"\\n"
             << "\\n"
             << "[dependencies]\\n"
             << "path-pkg = \\"./path-pkg\\"\\n"
             << "mathutil = \\"registry:registry:1.0.0\\"\\n";
        toml.close();
'''
test = replace_once(test, old_root_manifest, new_root_manifest, "root mixed manifest")

old_main = '''        main << "🧩 🚀\\n"  // root module declaration
             << "🔗 📜math.emoji📜\\n"  // import the math module
             << "🐍 🍎 🔢 🟰 42\\n"  // variable declaration
             << "📝 🧠 🫴 🍎 100 🤲\\n";  // print result of add(42, 100)
'''
new_main = '''        main << "🧩 🚀\\n"  // root module declaration
             << "🔗 📜math.emoji📜\\n"  // import the math module
             << "🐍 🍎 🔢 🟰 42\\n"  // variable declaration
             << "📝 🧠 🫴 🍎 100 🤲\\n"  // local-module function reference
             << "📝 🌟\\n"  // direct path-package symbol reference
             << "📝 🔮\\n"  // direct materialized-registry symbol reference
             << "📝 🧨\\n"; // forbidden transitive symbol reference
'''
test = replace_once(test, old_main, new_main, "mixed main package references")

old_path_manifest = '''        pathPkgToml << "[package]\\n"
                    << "name = \\"path-pkg\\"\\n"
                    << "version = \\"0.1.0\\"\\n"
                    << "entry = \\"lib.emoji\\"\\n";
'''
new_path_manifest = '''        pathPkgToml << "[package]\\n"
                    << "name = \\"path-pkg\\"\\n"
                    << "version = \\"0.1.0\\"\\n"
                    << "entry = \\"lib.emoji\\"\\n"
                    << "\\n[dependencies]\\n"
                    << "forbidden-transitive = \\"./transitive-pkg\\"\\n";
'''
test = replace_once(test, old_path_manifest, new_path_manifest, "path package transitive edge")

path_source_anchor = '''        pathPkgLib << "🧩 🧺\\n"  // package module
                   << "🐍 🌟 🔢 🟰 7\\n"  // exported constant
                   << "📤 🌟\\n";
        pathPkgLib.close();
'''
path_source_replacement = path_source_anchor + '''
        // A real transitive package: path-pkg may see it, but the workspace root may not.
        std::filesystem::create_directories(rootPath / "path-pkg" / "transitive-pkg");
        std::ofstream transitiveToml(rootPath / "path-pkg" / "transitive-pkg" / "emojineer.toml");
        transitiveToml << "[package]\\n"
                       << "name = \\"forbidden-transitive\\"\\n"
                       << "version = \\"0.1.0\\"\\n"
                       << "entry = \\"lib.emoji\\"\\n";
        transitiveToml.close();
        std::ofstream transitiveLib(rootPath / "path-pkg" / "transitive-pkg" / "lib.emoji");
        transitiveLib << "🧩 🧨\\n"
                      << "🐍 🧨 🔢 🟰 99\\n"
                      << "📤 🧨\\n";
        transitiveLib.close();
'''
test = replace_once(test, path_source_anchor, path_source_replacement, "transitive package fixture")

old_registry_source = '''        std::string regPkgLibContent = 
            "🧩 🧮\\n"  // module with valid identifier
            "🛠️ 🧠 🫴 🍎 🍐 🤲\\n"  // function with valid emoji name
            "🐍 🍇 🔢 🟰 🍎 ➕ 🍐\\n"  // variable with type
            "📦 🍇\\n"  // return the variable
            "🏁\\n"  // bare return
            "📤 🧠\\n";
'''
new_registry_source = '''        std::string regPkgLibContent =
            "🧩 🔭\\n"
            "🐍 🔮 🔢 🟰 11\\n"
            "📤 🔮\\n";
'''
test = replace_once(test, old_registry_source, new_registry_source, "unique registry package symbol")

lock_start = '        // Create emojineer.lock with canonical v3 lock schema\n'
lock_end = '        lock.close();\n'
la = test.find(lock_start)
if la < 0:
    raise SystemExit("missing mixed lock start")
lb = test.find(lock_end, la)
if lb < 0:
    raise SystemExit("missing mixed lock end")
lb += len(lock_end)
new_lock = r'''        // Create a valid v3 offline lock matching the root manifest and materialization.
        const auto rootManifest = load_project_manifest(rootPath / "emojineer.toml");
        const auto rootManifestHash = project_manifest_hash(rootManifest);
        const auto materializedPath = storeRoot / "registry" / "mathutil" / "1.0.0" / actualHash;
        std::ofstream lock(rootPath / "emojineer.lock");
        lock << "lock_version = 3\n"
             << "manifest_hash = \"" << rootManifestHash << "\"\n"
             << "\n"
             << "[[registry]]\n"
             << "alias = \"registry\"\n"
             << "id = \"lsp-test-registry\"\n"
             << "endpoint = \"" << registryEndpoint.canonical << "\"\n"
             << "\n"
             << "[[dependency]]\n"
             << "source = \"path\"\n"
             << "name = \"path-pkg\"\n"
             << "version = \"0.1.0\"\n"
             << "path = \"./path-pkg\"\n"
             << "dependencies = \"forbidden-transitive\"\n"
             << "\n"
             << "[[dependency]]\n"
             << "source = \"path\"\n"
             << "name = \"forbidden-transitive\"\n"
             << "version = \"0.1.0\"\n"
             << "path = \"./path-pkg/transitive-pkg\"\n"
             << "dependencies = \"\"\n"
             << "\n"
             << "[[dependency]]\n"
             << "source = \"registry\"\n"
             << "name = \"mathutil\"\n"
             << "version = \"1.0.0\"\n"
             << "registry = \"registry\"\n"
             << "registry_id = \"lsp-test-registry\"\n"
             << "registry_endpoint = \"" << registryEndpoint.canonical << "\"\n"
             << "requirement = \"1.0.0\"\n"
             << "artifact_sha256 = \"" << actualHash << "\"\n"
             << "content_sha256 = \"" << actualHash << "\"\n"
             << "store_path = \"" << materializedPath.generic_string() << "\"\n"
             << "dependencies = \"\"\n";
        lock.close();
'''
test = test[:la] + new_lock + test[lb:]

mixed_start = test.find("void test_e2e_real_mixed_workspace()")
mixed_end = test.find("#endif // EMOJINEER_HAVE_POSIX_PROCESS", mixed_start)
if mixed_start < 0 or mixed_end < 0:
    raise SystemExit("missing mixed-workspace test region")
mixed = test[mixed_start:mixed_end]

old_open_text = '"text":"🧩 🚀\\n🔗 📜math.emoji📜\\n🐍 🍎 🔢 🟰 42\\n📝 🧠 🫴 🍎 100 🤲\\n"'
new_open_text = '"text":"🧩 🚀\\n🔗 📜math.emoji📜\\n🐍 🍎 🔢 🟰 42\\n📝 🧠 🫴 🍎 100 🤲\\n📝 🌟\\n📝 🔮\\n📝 🧨\\n"'
if old_open_text in mixed:
    mixed = mixed.replace(old_open_text, new_open_text, 1)
elif new_open_text not in mixed:
    raise SystemExit("missing mixed didOpen source")

mixed = mixed.replace('"position":{"line":0,"character":1}', '"position":{"line":0,"character":0}', 1)
mixed = mixed.replace('"position":{"line":3,"character":2}', '"position":{"line":3,"character":3}', 2)
mixed = mixed.replace('"position":{"line":2,"character":2}', '"position":{"line":2,"character":3}', 1)

completion_anchor = '''    assert(body.find("\\\"result\\\":null") == std::string::npos);
    
    // Request hover on a symbol'''
completion_new = '''    assert(body.find("\\\"result\\\":null") == std::string::npos);
    assert(body.find("path-pkg") != std::string::npos &&
           "Root completion must expose its direct path dependency");
    assert(body.find("mathutil") != std::string::npos &&
           "Root completion must expose its direct materialized registry dependency");
    assert(body.find("forbidden-transitive") == std::string::npos &&
           "Root completion must not expose an undeclared transitive dependency");
    
    // Request hover on a symbol'''
if completion_anchor in mixed:
    mixed = mixed.replace(completion_anchor, completion_new, 1)
elif "Root completion must expose its direct path dependency" not in mixed:
    raise SystemExit("missing completion assertion anchor")

local_def_anchor = '''    assert(body.find("\\\"result\\\":null") == std::string::npos);
    
    // Request references
'''
package_definition_block = r'''    assert(body.find("\"result\":null") == std::string::npos);

    // Direct path-package definition must resolve into path-pkg.
    std::string pathDefReq = R"({"jsonrpc":"2.0","id":12,"method":"textDocument/definition","params":{"textDocument":{"uri":")" + mainUri + R"(},"position":{"line":4,"character":3}}})";
    server.sendMessage(pathDefReq);
    std::string pathDefResp = server.readResponse(3000);
    assert(!pathDefResp.empty());
    bodyStart = pathDefResp.find("\r\n\r\n") + 4;
    body = pathDefResp.substr(bodyStart);
    assert(body.find("path-pkg/lib.emoji") != std::string::npos &&
           "Root definition must resolve its direct path-package symbol");

    // Direct registry-package definition must resolve into the materialized store package.
    std::string registryDefReq = R"({"jsonrpc":"2.0","id":13,"method":"textDocument/definition","params":{"textDocument":{"uri":")" + mainUri + R"(},"position":{"line":5,"character":3}}})";
    server.sendMessage(registryDefReq);
    std::string registryDefResp = server.readResponse(3000);
    assert(!registryDefResp.empty());
    bodyStart = registryDefResp.find("\r\n\r\n") + 4;
    body = registryDefResp.substr(bodyStart);
    assert(body.find(".emojineer/packages/registry/mathutil/1.0.0") != std::string::npos &&
           "Root definition must resolve its direct materialized registry-package symbol");

    // The same root may not resolve a symbol that exists only in path-pkg's transitive dependency.
    std::string transitiveDefReq = R"({"jsonrpc":"2.0","id":14,"method":"textDocument/definition","params":{"textDocument":{"uri":")" + mainUri + R"(},"position":{"line":6,"character":3}}})";
    server.sendMessage(transitiveDefReq);
    std::string transitiveDefResp = server.readResponse(3000);
    assert(!transitiveDefResp.empty());
    bodyStart = transitiveDefResp.find("\r\n\r\n") + 4;
    body = transitiveDefResp.substr(bodyStart);
    assert(body.find("forbidden-transitive") == std::string::npos &&
           body.find("transitive-pkg") == std::string::npos &&
           "Root definition must reject undeclared transitive package symbols");

    // Request references
'''
if local_def_anchor in mixed:
    mixed = mixed.replace(local_def_anchor, package_definition_block, 1)
elif "Direct path-package definition must resolve" not in mixed:
    raise SystemExit("missing definition insertion anchor")

refs_anchor = '''    assert(body.find("\\\"result\\\":null") == std::string::npos);
    
    // Request document symbols
'''
refs_new = r'''    assert(body.find("\"result\":null") == std::string::npos);

    std::string pathRefsReq = R"({"jsonrpc":"2.0","id":19,"method":"textDocument/references","params":{"textDocument":{"uri":")" + mainUri + R"(},"position":{"line":4,"character":3},"context":{"includeDeclaration":true}}})";
    server.sendMessage(pathRefsReq);
    std::string pathRefsResp = server.readResponse(3000);
    assert(!pathRefsResp.empty());
    bodyStart = pathRefsResp.find("\r\n\r\n") + 4;
    body = pathRefsResp.substr(bodyStart);
    assert(body.find("path-pkg/lib.emoji") != std::string::npos &&
           body.find("main.emoji") != std::string::npos &&
           "References must cross the direct path-package boundary");

    std::string registryRefsReq = R"({"jsonrpc":"2.0","id":20,"method":"textDocument/references","params":{"textDocument":{"uri":")" + mainUri + R"(},"position":{"line":5,"character":3},"context":{"includeDeclaration":true}}})";
    server.sendMessage(registryRefsReq);
    std::string registryRefsResp = server.readResponse(3000);
    assert(!registryRefsResp.empty());
    bodyStart = registryRefsResp.find("\r\n\r\n") + 4;
    body = registryRefsResp.substr(bodyStart);
    assert(body.find(".emojineer/packages/registry/mathutil/1.0.0") != std::string::npos &&
           "References must cross the direct materialized registry-package boundary");

    std::string transitiveRefsReq = R"({"jsonrpc":"2.0","id":21,"method":"textDocument/references","params":{"textDocument":{"uri":")" + mainUri + R"(},"position":{"line":6,"character":3},"context":{"includeDeclaration":true}}})";
    server.sendMessage(transitiveRefsReq);
    std::string transitiveRefsResp = server.readResponse(3000);
    assert(!transitiveRefsResp.empty());
    bodyStart = transitiveRefsResp.find("\r\n\r\n") + 4;
    body = transitiveRefsResp.substr(bodyStart);
    assert(body.find("forbidden-transitive") == std::string::npos &&
           body.find("transitive-pkg") == std::string::npos &&
           "References from root must not cross into an undeclared transitive package");

    // Request document symbols
'''
if refs_anchor in mixed:
    mixed = mixed.replace(refs_anchor, refs_new, 1)
elif "References must cross the direct path-package boundary" not in mixed:
    raise SystemExit("missing reference insertion anchor")

workspace_anchor = '''    assert(body.find("\\\"result\\\":null") == std::string::npos);
    
    // Request formatting
'''
workspace_new = r'''    assert(body.find("\"result\":null") == std::string::npos);

    std::string pathWsReq = R"({"jsonrpc":"2.0","id":16,"method":"workspace/symbol","params":{"query":"🌟"}})";
    server.sendMessage(pathWsReq);
    std::string pathWsResp = server.readResponse(3000);
    assert(!pathWsResp.empty());
    bodyStart = pathWsResp.find("\r\n\r\n") + 4;
    body = pathWsResp.substr(bodyStart);
    assert(body.find("path-pkg/lib.emoji") != std::string::npos &&
           "Workspace symbols must include a direct path package");

    std::string registryWsReq = R"({"jsonrpc":"2.0","id":17,"method":"workspace/symbol","params":{"query":"🔮"}})";
    server.sendMessage(registryWsReq);
    std::string registryWsResp = server.readResponse(3000);
    assert(!registryWsResp.empty());
    bodyStart = registryWsResp.find("\r\n\r\n") + 4;
    body = registryWsResp.substr(bodyStart);
    assert(body.find(".emojineer/packages/registry/mathutil/1.0.0") != std::string::npos &&
           "Workspace symbols must include a direct materialized registry package");

    std::string transitiveWsReq = R"({"jsonrpc":"2.0","id":18,"method":"workspace/symbol","params":{"query":"🧨"}})";
    server.sendMessage(transitiveWsReq);
    std::string transitiveWsResp = server.readResponse(3000);
    assert(!transitiveWsResp.empty());
    bodyStart = transitiveWsResp.find("\r\n\r\n") + 4;
    body = transitiveWsResp.substr(bodyStart);
    assert(body.find("forbidden-transitive") == std::string::npos &&
           body.find("transitive-pkg") == std::string::npos &&
           "Workspace symbols must not leak an undeclared transitive package");

    // Request formatting
'''
if workspace_anchor in mixed:
    mixed = mixed.replace(workspace_anchor, workspace_new, 1)
elif "Workspace symbols must include a direct path package" not in mixed:
    raise SystemExit("missing workspace insertion anchor")

test = test[:mixed_start] + mixed + test[mixed_end:]

test_path.write_text(test)
print("repaired: exact UTF-16 definition/reference locations and real direct-vs-transitive LSP authority acceptance")
