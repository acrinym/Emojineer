from pathlib import Path

path = Path("tests/lsp_tests.cpp")
text = path.read_text()
start = text.find("void test_e2e_real_explicit_null_id_request() {")
end = text.find("}\n#endif", start)
if start < 0 or end < 0:
    raise SystemExit("explicit null-id regression function not found")
end += 2
replacement = r'''void test_e2e_real_explicit_null_id_request() {
    std::cout << "Testing real LSP explicit null request id..." << std::endl;
    LspServerProcess server;
    const bool started = server.start("./emojineer-lsp");
    assert(started);
    if (!started) return;

    const bool initSent = server.sendMessage(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///test","processId":12345}})");
    assert(initSent);
    const std::string initResponse = server.readResponse();
    assert(!initResponse.empty());

    const bool initializedSent = server.sendMessage(R"({"jsonrpc":"2.0","method":"initialized","params":{}})");
    assert(initializedSent);

    const bool shutdownSent = server.sendMessage(R"({"jsonrpc":"2.0","id":null,"method":"shutdown","params":null})");
    assert(shutdownSent);
    const std::string response = server.readResponse();
    assert(!response.empty() && "explicit id:null request must receive a response");
    if (response.empty()) {
        server.stop();
        return;
    }
    const std::size_t bodyStart = response.find("\r\n\r\n");
    assert(bodyStart != std::string::npos);
    if (bodyStart == std::string::npos) {
        server.stop();
        return;
    }
    const std::string body = response.substr(bodyStart + 4);
    assert(body.find("\"result\"") != std::string::npos);
    assert(body.find("\"id\":null") != std::string::npos && "response must preserve explicit null id");

    const bool exitSent = server.sendMessage(R"({"jsonrpc":"2.0","method":"exit","params":null})");
    assert(exitSent);
    usleep(100000);
    server.stop();
}
'''
text = text[:start] + replacement + text[end:]
path.write_text(text)
print("release-safe null-id regression repaired")
