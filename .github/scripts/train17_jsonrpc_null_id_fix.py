from pathlib import Path
import re

lsp_path = Path("src/lsp.cpp")
lsp = lsp_path.read_text()

needle = '    if (id) json::objectSet(obj, "id", JsonValue(static_cast<double>(*id)));\n'
replacement = '''    if (id) {
        json::objectSet(obj, "id", JsonValue(static_cast<double>(*id)));
    } else {
        // JSON-RPC 2.0 requires an explicit null id when a response/error
        // cannot be correlated to a valid request id.
        json::objectSet(obj, "id", JsonValue(nullptr));
    }
'''

def repair_region(text: str, start_marker: str, end_marker: str, label: str) -> str:
    start = text.find(start_marker)
    end = text.find(end_marker, start + 1)
    if start < 0 or end < 0:
        raise SystemExit(f"missing {label} region")
    region = text[start:end]
    if replacement in region:
        return text
    if needle not in region:
        raise SystemExit(f"missing null-id anchor in {label}")
    region = region.replace(needle, replacement, 1)
    return text[:start] + region + text[end:]

lsp = repair_region(
    lsp,
    "std::string formatJsonRpcResponse(const JsonValue& result, std::optional<int> id)",
    "std::string formatJsonRpcError(int code, const std::string& message, std::optional<int> id)",
    "response formatter",
)
lsp = repair_region(
    lsp,
    "std::string formatJsonRpcError(int code, const std::string& message, std::optional<int> id)",
    "// String ID variants for JSON-RPC 2.0",
    "error formatter",
)

# Notifications/requests with no id must still omit the id member.
request_start = lsp.find("std::string formatJsonRpc(const std::string& method")
response_start = lsp.find("std::string formatJsonRpcResponse(", request_start)
request_region = lsp[request_start:response_start]
if 'JsonValue(nullptr)' in request_region:
    raise SystemExit("notification/request formatter incorrectly gained id:null")

lsp_path.write_text(lsp)

test_path = Path("tests/lsp_tests.cpp")
test = test_path.read_text()
name = "test_jsonrpc_null_id_contract"
if name not in test:
    main_match = re.search(r"\nint\s+main\s*\([^)]*\)\s*\{", test)
    if not main_match:
        raise SystemExit("test main() anchor not found")

    regression = r'''
// JSON-RPC 2.0 requires an explicit null id for a response/error when the
// request id cannot be identified. Notifications, by contrast, omit id.
void test_jsonrpc_null_id_contract() {
    const std::string error = formatJsonRpcError(-32700, "Parse error", std::nullopt);
    assert(error.find("Content-Length:") == 0);
    assert(error.find("\"id\":null") != std::string::npos &&
           "unidentifiable JSON-RPC errors must carry id:null");

    const std::string response = formatJsonRpcResponse(JsonValue(nullptr), std::nullopt);
    assert(response.find("\"id\":null") != std::string::npos &&
           "unidentifiable JSON-RPC responses must carry id:null");

    const std::string notification = formatNotification("exit", JsonValue(nullptr));
    assert(notification.find("\"id\"") == std::string::npos &&
           "JSON-RPC notifications must not gain an id member");
}

'''
    insert_at = main_match.start() + 1
    test = test[:insert_at] + regression + test[insert_at:]

    main_match = re.search(r"int\s+main\s*\([^)]*\)\s*\{", test)
    if not main_match:
        raise SystemExit("test main() disappeared")
    test = test[:main_match.end()] + f"\n    {name}();" + test[main_match.end():]

test_path.write_text(test)
print("repaired: JSON-RPC response/error formatters emit explicit id:null when id is unavailable")
