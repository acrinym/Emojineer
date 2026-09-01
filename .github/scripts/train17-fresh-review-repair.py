from pathlib import Path
import re


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        raise SystemExit(f"missing replacement anchor: {label}")
    return text.replace(old, new, 1)


# 1) JSON-RPC request identity: presence of `id`, including explicit null,
# distinguishes a request from a notification.
lsp_path = Path("src/lsp.cpp")
lsp = lsp_path.read_text()
old = '''    const JsonValue& params = getJsonObject(message, "params");
    const JsonValue& idVal = getJsonObject(message, "id");

    // Support both integer and string request IDs
    std::optional<int> intId;
    std::optional<std::string> stringId;
    bool hasInvalidId = false;
    if (!idVal.isNull()) {
        if (idVal.isNumber()) {
            intId = static_cast<int>(getJsonNumber(idVal));
        } else if (idVal.isString()) {
            stringId = idVal.get<std::string>();
        } else {
            // Invalid ID type - mark it to return error with null id
            hasInvalidId = true;
        }
    }
'''
new = '''    const JsonValue& params = getJsonObject(message, "params");
    const auto idIt = obj->find("id");
    const bool hasId = idIt != obj->end();

    // JSON-RPC distinguishes a request from a notification by the presence
    // of the id member. An explicit null id is still a request and must get
    // a response carrying id:null.
    std::optional<int> intId;
    std::optional<std::string> stringId;
    bool hasInvalidId = false;
    if (hasId && !idIt->second.isNull()) {
        const JsonValue& idVal = idIt->second;
        if (idVal.isNumber()) {
            intId = static_cast<int>(getJsonNumber(idVal));
        } else if (idVal.isString()) {
            stringId = idVal.get<std::string>();
        } else {
            // Invalid ID type - mark it to return error with null id.
            hasInvalidId = true;
        }
    }
'''
lsp = replace_once(lsp, old, new, "JSON-RPC id member presence")
lsp = replace_once(
    lsp,
    '''    // Handle the response - prefer string IDs if present
    if (stringId || intId) {''',
    '''    // Handle a request whenever an id member is present, including id:null.
    // Empty intId/stringId optionals deliberately serialize as a null id.
    if (hasId) {''',
    "JSON-RPC request branch",
)
lsp_path.write_text(lsp)


# 2) Package-import failures caused by an import statement retain the
# importer's source path, identity, line, and requested token.
module_path = Path("src/module.cpp")
module = module_path.read_text()
module = replace_once(
    module,
    '''        require_owned_path(importer.package_name, canonical, context);
        return canonical;''',
    '''        try {
            require_owned_path(importer.package_name, canonical, context);
        } catch (const std::runtime_error& error) {
            throw SourceLocationException(error.what(), importer.path, importer.identity,
                                          spec.line, 1, spec.requested);
        }
        return canonical;''',
    "local import ownership source context",
)

start = module.find("    ResolvedSourceImport resolve_package_import")
end = module.find("    std::string visit_standard", start)
if start < 0 or end < 0:
    raise SystemExit("resolve_package_import region not found")
region = module[start:end]
region = replace_once(
    region,
    '''    ResolvedSourceImport resolve_package_import(const ModuleUnit& importer,
                                                const ImportSpec& spec) const {
''',
    '''    ResolvedSourceImport resolve_package_import(const ModuleUnit& importer,
                                                const ImportSpec& spec) const {
        const auto fail = [&](const std::string& message) -> void {
            throw SourceLocationException(message, importer.path, importer.identity,
                                          spec.line, 1, spec.requested);
        };
''',
    "package import failure helper",
)

# Convert user/import-caused runtime failures to typed source failures. Internal
# graph-invariant failures intentionally remain std::runtime_error.
pattern = re.compile(r"throw std::runtime_error\((.*?)\);", re.S)
parts = []
last = 0
converted = 0
for match in pattern.finditer(region):
    expr = match.group(1)
    source_owned = (
        "importer.identity" in expr
        or "spec.requested" in expr
        or "pkg: import" in expr
    )
    if not source_owned:
        continue
    parts.append(region[last:match.start()])
    parts.append("fail(" + expr + ");")
    last = match.end()
    converted += 1
if converted == 0:
    raise SystemExit("no package import failures converted")
parts.append(region[last:])
region = "".join(parts)
module = module[:start] + region + module[end:]
module_path.write_text(module)


# 3) Spawned-server harness: loop partial writes and retain coalesced frame
# carry-over in a persistent receive buffer.
test_path = Path("tests/lsp_tests.cpp")
tests = test_path.read_text()
if "#include <cerrno>" not in tests:
    tests = tests.replace("#include <chrono>\n", "#include <chrono>\n#include <cerrno>\n#include <cctype>\n", 1)

tests = replace_once(
    tests,
    '''    bool isRunning_;

    LspServerProcess() : pid_(-1), stdinFd_(-1), stdoutFd_(-1), isRunning_(false) {}''',
    '''    bool isRunning_;
    std::string receiveBuffer_;

    LspServerProcess()
        : pid_(-1), stdinFd_(-1), stdoutFd_(-1), isRunning_(false), receiveBuffer_() {}''',
    "receive buffer member",
)
tests = replace_once(
    tests,
    '''        isRunning_ = true;
        return true;''',
    '''        receiveBuffer_.clear();
        isRunning_ = true;
        return true;''',
    "start buffer reset",
)
tests = replace_once(
    tests,
    '''        isRunning_ = false;
    }

    // Send a framed JSON-RPC message''',
    '''        isRunning_ = false;
        receiveBuffer_.clear();
    }

    // Send a framed JSON-RPC message''',
    "stop buffer reset",
)

send_start = tests.find("    bool sendMessage(const std::string& jsonBody) {")
read_marker = tests.find("    // Read a framed JSON-RPC response", send_start)
if send_start < 0 or read_marker < 0:
    raise SystemExit("sendMessage region not found")
new_send = r'''    bool sendMessage(const std::string& jsonBody) {
        if (!isRunning_ || stdinFd_ < 0) return false;

        std::string framed = "Content-Length: " + std::to_string(jsonBody.size()) + "\r\n\r\n" + jsonBody;
        std::size_t offset = 0;
        while (offset < framed.size()) {
            const ssize_t written = write(stdinFd_, framed.data() + offset, framed.size() - offset);
            if (written > 0) {
                offset += static_cast<std::size_t>(written);
                continue;
            }
            if (written < 0 && errno == EINTR) continue;
            return false;
        }
        return true;
    }

    std::optional<std::string> popBufferedFrame() {
        const std::size_t headerEnd = receiveBuffer_.find("\r\n\r\n");
        if (headerEnd == std::string::npos) return std::nullopt;

        const std::string headers = receiveBuffer_.substr(0, headerEnd);
        const std::string marker = "Content-Length:";
        const std::size_t clPos = headers.find(marker);
        if (clPos == std::string::npos) return std::nullopt;

        std::size_t pos = clPos + marker.size();
        while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) ++pos;
        const std::size_t digitsStart = pos;
        std::size_t contentLength = 0;
        while (pos < headers.size() && std::isdigit(static_cast<unsigned char>(headers[pos]))) {
            contentLength = contentLength * 10 + static_cast<std::size_t>(headers[pos] - '0');
            ++pos;
        }
        if (pos == digitsStart) return std::nullopt;

        const std::size_t bodyStart = headerEnd + 4;
        const std::size_t frameSize = bodyStart + contentLength;
        if (receiveBuffer_.size() < frameSize) return std::nullopt;

        std::string frame = receiveBuffer_.substr(0, frameSize);
        receiveBuffer_.erase(0, frameSize);
        return frame;
    }

'''
tests = tests[:send_start] + new_send + tests[read_marker:]

read_start = tests.find("    std::string readResponse(int timeoutMs = 5000) {")
read_end = tests.find("    // Extract JSON body from a framed response", read_start)
if read_start < 0 or read_end < 0:
    raise SystemExit("readResponse region not found")
new_read = r'''    std::string readResponse(int timeoutMs = 5000) {
        if (!isRunning_ || stdoutFd_ < 0) return "";

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (true) {
            if (auto frame = popBufferedFrame()) return *frame;
            if (std::chrono::steady_clock::now() >= deadline) return "";

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(stdoutFd_, &readfds);
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 10000;

            const int ret = select(stdoutFd_ + 1, &readfds, nullptr, nullptr, &tv);
            if (ret < 0) {
                if (errno == EINTR) continue;
                return "";
            }
            if (ret == 0) continue;

            char buf[4096];
            const ssize_t n = read(stdoutFd_, buf, sizeof(buf));
            if (n > 0) {
                receiveBuffer_.append(buf, static_cast<std::size_t>(n));
                continue;
            }
            if (n == 0) return "";
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return "";
        }
    }

'''
tests = tests[:read_start] + new_read + tests[read_end:]

if "void test_lsp_harness_coalesced_frames()" not in tests:
    main_pos = tests.find("int main() {")
    if main_pos < 0:
        raise SystemExit("LSP test main not found")
    extra_tests = r'''
#if EMOJINEER_HAVE_POSIX_PROCESS
void test_lsp_harness_coalesced_frames() {
    LspServerProcess harness;
    const std::string firstBody = R"({"jsonrpc":"2.0","id":1,"result":null})";
    const std::string secondBody = R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{"diagnostics":[]}})";
    const std::string first = "Content-Length: " + std::to_string(firstBody.size()) + "\r\n\r\n" + firstBody;
    const std::string second = "Content-Length: " + std::to_string(secondBody.size()) + "\r\n\r\n" + secondBody;
    harness.receiveBuffer_ = first + second;

    auto a = harness.popBufferedFrame();
    assert(a && *a == first);
    assert(harness.receiveBuffer_ == second && "second coalesced frame must remain buffered");
    auto b = harness.popBufferedFrame();
    assert(b && *b == second);
    assert(harness.receiveBuffer_.empty());
}

void test_e2e_real_explicit_null_id_request() {
    std::cout << "Testing real LSP explicit null request id..." << std::endl;
    LspServerProcess server;
    assert(server.start("./emojineer-lsp"));

    assert(server.sendMessage(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///test","processId":12345}})"));
    assert(!server.readResponse().empty());
    assert(server.sendMessage(R"({"jsonrpc":"2.0","method":"initialized","params":{}})"));

    assert(server.sendMessage(R"({"jsonrpc":"2.0","id":null,"method":"shutdown","params":null})"));
    const std::string response = server.readResponse();
    assert(!response.empty() && "explicit id:null request must receive a response");
    const std::size_t bodyStart = response.find("\r\n\r\n");
    assert(bodyStart != std::string::npos);
    const std::string body = response.substr(bodyStart + 4);
    assert(body.find("\"result\"") != std::string::npos);
    assert(body.find("\"id\":null") != std::string::npos && "response must preserve explicit null id");

    assert(server.sendMessage(R"({"jsonrpc":"2.0","method":"exit","params":null})"));
    usleep(100000);
    server.stop();
}
#endif

'''
    tests = tests[:main_pos] + extra_tests + tests[main_pos:]

tests = replace_once(
    tests,
    '''#if EMOJINEER_HAVE_POSIX_PROCESS
    std::cout << "\\n=== Real Framed E2E Tests (spawning server) ===" << std::endl;
    test_e2e_real_initialize();''',
    '''#if EMOJINEER_HAVE_POSIX_PROCESS
    std::cout << "\\n=== Real Framed E2E Tests (spawning server) ===" << std::endl;
    test_lsp_harness_coalesced_frames();
    test_e2e_real_explicit_null_id_request();
    test_e2e_real_initialize();''',
    "new real E2E calls",
)
test_path.write_text(tests)


# 4) Regression: a package-authority error originating in an imported local
# module must preserve that imported module as the source owner.
module_test_path = Path("tests/module_tests.cpp")
module_tests = module_test_path.read_text()
if '#include "emojineer/source_diagnostic.hpp"' not in module_tests:
    module_tests = module_tests.replace(
        '#include "emojineer/project.hpp"\n',
        '#include "emojineer/project.hpp"\n#include "emojineer/source_diagnostic.hpp"\n',
        1,
    )
if "void test_package_import_error_preserves_importer_source()" not in module_tests:
    marker = "\n} // namespace\n\nint main()"
    pos = module_tests.find(marker)
    if pos < 0:
        raise SystemExit("module test namespace marker not found")
    regression = r'''

void test_package_import_error_preserves_importer_source() {
    TempRoot root("package-import-source-owner");
    emojineer::initialize_project(root.path, "owner_app");
    const auto manifest = emojineer::load_project_manifest(root.path / "emojineer.toml");
    const auto entry = root.path / manifest.entry;
    const auto imported = entry.parent_path() / "child.emoji";

    write_source(imported,
                 "🧩 🌲\n"
                 "🔗 📜pkg:missing/lib.emoji📜\n");
    write_source(entry,
                 "🧩 🚀\n"
                 "🔗 📜child.emoji📜\n");

    bool caught = false;
    try {
        (void)emojineer::compile_file(entry, {}, root.path);
    } catch (const emojineer::SourceLocationException& error) {
        caught = true;
        require(error.sourcePath == imported,
                "package import failure must belong to imported module path");
        require(error.sourceIdentity.find("child.emoji") != std::string::npos,
                "package import failure must preserve imported module identity");
        require(error.line == 2, "package import failure must preserve import line");
        require(error.tokenLexeme == "pkg:missing/lib.emoji",
                "package import failure must preserve requested import token");
        require(std::string(error.what()).find("does not declare direct dependency 'missing'") != std::string::npos,
                "regression must exercise package authority, not parsing");
    }
    require(caught, "package import authority failure must be a SourceLocationException");
}
'''
    module_tests = module_tests[:pos] + regression + module_tests[pos:]
    module_tests = module_tests.replace(
        "        test_dependency_initialization_once_and_project_check();",
        "        test_dependency_initialization_once_and_project_check();\n        test_package_import_error_preserves_importer_source();",
        1,
    )
module_test_path.write_text(module_tests)


# Normalize trailing whitespace in touched product/test files.
for path in (lsp_path, module_path, test_path, module_test_path):
    text = path.read_text()
    newline = text.endswith("\n")
    text = "\n".join(line.rstrip() for line in text.splitlines())
    if newline:
        text += "\n"
    path.write_text(text)

print(f"fresh review repair complete; converted {converted} package import failures")
