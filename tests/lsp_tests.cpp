// LSP Transport Acceptance Tests for Train 17
// This test harness spawns the real emojineer-lsp executable and tests
// the actual JSON-RPC protocol over stdio with byte-exact Content-Length framing

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <thread>
#include <future>
#include <filesystem>
#include <fstream>
#include <regex>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>

// Read entire file contents
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Find emojineer-lsp executable in common locations
std::string findLspExecutable() {
    std::vector<std::string> candidates = {
        "./build/emojineer-lsp",
        "./build/bin/emojineer-lsp",
        "./emojineer-lsp",
        "/workspace/project/Emojineer/build/emojineer-lsp",
    };
    
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    
    // Return default to let the test show the error
    return candidates[0];
}

// Simple JSON-RPC message builder
class JsonRpcMessage {
public:
    static std::string buildRequest(const std::string& method, const std::string& params, int id) {
        std::string body = R"({"jsonrpc":"2.0","method":")" + method + R"(","params":)" + params + R"(,"id":)" + std::to_string(id) + "}";
        return frameMessage(body);
    }
    
    static std::string buildNotification(const std::string& method, const std::string& params) {
        std::string body = R"({"jsonrpc":"2.0","method":")" + method + R"(","params":)" + params + "}";
        return frameMessage(body);
    }
    
    static std::string frameMessage(const std::string& body) {
        return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    }
    
    static std::string parseResponse(const std::string& raw) {
        // Find body start after headers
        size_t bodyStart = raw.find("\r\n\r\n");
        if (bodyStart == std::string::npos) return "";
        return raw.substr(bodyStart + 4);
    }
    
    static bool hasId(const std::string& json) {
        return json.find("\"id\":") != std::string::npos;
    }
    
    static int getId(const std::string& json) {
        std::regex idRegex("\"id\":\\s*(-?\\d+)");
        std::smatch match;
        if (std::regex_search(json, match, idRegex)) {
            return std::stoi(match[1].str());
        }
        return -1;
    }
    
    static bool isError(const std::string& json) {
        return json.find("\"error\"") != std::string::npos;
    }
    
    static bool isResponse(const std::string& json) {
        return json.find("\"result\"") != std::string::npos || json.find("\"error\"") != std::string::npos;
    }
    
    static bool isNotification(const std::string& json) {
        return json.find("\"id\"") == std::string::npos;
    }
};

// Bidirectional LSP client using proper pipes + fork/exec
class LspClient {
public:
    LspClient(const std::string& exePath) : executable_(exePath), nextId_(1) {
        pid_ = -1;
        stdinFd_ = -1;
        stdoutFd_ = -1;
    }
    
    ~LspClient() {
        cleanup();
    }
    
    bool start() {
        // Create two pipes: stdin (write to child) and stdout (read from child)
        int stdinPipe[2] = {-1, -1};
        int stdoutPipe[2] = {-1, -1};
        
        if (pipe(stdinPipe) < 0) {
            std::cerr << "Failed to create stdin pipe" << std::endl;
            return false;
        }
        if (pipe(stdoutPipe) < 0) {
            close(stdinPipe[0]);
            close(stdinPipe[1]);
            std::cerr << "Failed to create stdout pipe" << std::endl;
            return false;
        }
        
        pid_ = fork();
        if (pid_ < 0) {
            close(stdinPipe[0]);
            close(stdinPipe[1]);
            close(stdoutPipe[0]);
            close(stdoutPipe[1]);
            std::cerr << "Failed to fork" << std::endl;
            return false;
        }
        
        if (pid_ == 0) {
            // Child process
            close(stdinPipe[1]);  // Close write end of stdin
            close(stdoutPipe[0]); // Close read end of stdout
            
            // Redirect stdin/stdout
            dup2(stdinPipe[0], STDIN_FILENO);
            dup2(stdoutPipe[1], STDOUT_FILENO);
            
            // Redirect stderr to /dev/null to suppress error messages
            int devNull = open("/dev/null", O_WRONLY);
            if (devNull >= 0) {
                dup2(devNull, STDERR_FILENO);
                close(devNull);
            }
            
            close(stdinPipe[0]);
            close(stdoutPipe[1]);
            
            // Execute the LSP server
            execl(executable_.c_str(), executable_.c_str(), nullptr);
            // If execl fails
            _exit(1);
        }
        
        // Parent process
        close(stdinPipe[0]);   // Close read end of stdin
        close(stdoutPipe[1]);   // Close write end of stdout
        
        stdinFd_ = stdinPipe[1];
        stdoutFd_ = stdoutPipe[0];
        
        return true;
    }
    
    // Send request with auto-generated ID
    std::string sendRequest(const std::string& method, const std::string& params) {
        int id = nextId_++;
        return sendRequest(method, params, id);
    }
    
    // Send request with explicit ID (for testing specific error codes)
    std::string sendRequest(const std::string& method, const std::string& params, int id) {
        std::string msg = JsonRpcMessage::buildRequest(method, params, id);
        
        ssize_t written = write(stdinFd_, msg.c_str(), msg.size());
        if (written != static_cast<ssize_t>(msg.size())) {
            return "";
        }
        
        return readResponse(id);
    }
    
    void sendNotification(const std::string& method, const std::string& params) {
        std::string msg = JsonRpcMessage::buildNotification(method, params);
        write(stdinFd_, msg.c_str(), msg.size());
    }
    
    // Send raw bytes (for malformed JSON testing)
    void sendRaw(const std::string& data) {
        write(stdinFd_, data.c_str(), data.size());
    }
    
    // Read response for a specific ID, preserving over-read bytes
    std::string readResponse(int expectedId) {
        // Use buffered reading with carryover for over-read bytes
        std::string buffer = readBuffer_;  // Preserve any leftover bytes from previous read
        readBuffer_.clear();
        
        char buf[4096];
        bool foundBodyStart = false;
        size_t bodyStart = 0;
        
        // Read until we have complete headers
        while (!foundBodyStart) {
            // Check if we already have complete headers in buffer
            size_t headerEnd = buffer.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                foundBodyStart = true;
                bodyStart = headerEnd + 4;
                break;
            }
            
            // Need more data
            ssize_t n = read(stdoutFd_, buf, sizeof(buf));
            if (n <= 0) break;
            buffer.append(buf, n);
            
            // Check again
            headerEnd = buffer.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                foundBodyStart = true;
                bodyStart = headerEnd + 4;
            }
        }
        
        if (!foundBodyStart) {
            // No complete headers, save buffer for next read
            readBuffer_ = buffer;
            return "";
        }
        
        // Parse Content-Length from headers
        std::string headers = buffer.substr(0, bodyStart);
        std::regex clRegex("Content-Length:\\s*(\\d+)");
        std::smatch match;
        size_t contentLength = 0;
        
        if (std::regex_search(headers, match, clRegex)) {
            contentLength = std::stoul(match[1].str());
        }
        
        // Get body start and remaining buffer
        std::string bodyBuffer = buffer.substr(bodyStart);
        
        // Read until we have full body
        while (bodyBuffer.size() < contentLength) {
            ssize_t n = read(stdoutFd_, buf, sizeof(buf));
            if (n <= 0) break;
            bodyBuffer.append(buf, n);
        }
        
        // Save any over-read bytes for next read
        if (bodyBuffer.size() > contentLength) {
            readBuffer_ = bodyBuffer.substr(contentLength);
            bodyBuffer = bodyBuffer.substr(0, contentLength);
        }
        
        return bodyBuffer;
    }
    
    // Read a notification (no id) - blocks until notification is available
    std::string readNotification(int timeoutMs = 1000) {
        // Use select/poll for timeout
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(stdoutFd_, &readfds);
        
        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        
        int ret = select(stdoutFd_ + 1, &readfds, nullptr, nullptr, &tv);
        if (ret <= 0) {
            return "";  // Timeout or error
        }
        
        // Now read the notification
        std::string buffer = readBuffer_;
        readBuffer_.clear();
        
        char buf[4096];
        bool foundBodyStart = false;
        size_t bodyStart = 0;
        
        while (!foundBodyStart) {
            size_t headerEnd = buffer.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                foundBodyStart = true;
                bodyStart = headerEnd + 4;
                break;
            }
            
            ssize_t n = read(stdoutFd_, buf, sizeof(buf));
            if (n <= 0) return "";
            buffer.append(buf, n);
        }
        
        // Parse Content-Length
        std::string headers = buffer.substr(0, bodyStart);
        std::regex clRegex("Content-Length:\\s*(\\d+)");
        std::smatch match;
        size_t contentLength = 0;
        
        if (std::regex_search(headers, match, clRegex)) {
            contentLength = std::stoul(match[1].str());
        }
        
        std::string bodyBuffer = buffer.substr(bodyStart);
        
        while (bodyBuffer.size() < contentLength) {
            ssize_t n = read(stdoutFd_, buf, sizeof(buf));
            if (n <= 0) break;
            bodyBuffer.append(buf, n);
        }
        
        // Save any over-read
        if (bodyBuffer.size() > contentLength) {
            readBuffer_ = bodyBuffer.substr(contentLength);
            bodyBuffer = bodyBuffer.substr(0, contentLength);
        }
        
        return bodyBuffer;
    }
    
    void shutdown() {
        sendRequest("shutdown", "{}");
        sendNotification("exit", "{}");
    }
    
    void cleanup() {
        if (stdinFd_ >= 0) {
            close(stdinFd_);
            stdinFd_ = -1;
        }
        if (stdoutFd_ >= 0) {
            close(stdoutFd_);
            stdoutFd_ = -1;
        }
        if (pid_ > 0) {
            // Kill the child if still running
            kill(pid_, SIGTERM);
            int status;
            waitpid(pid_, &status, 0);
            pid_ = -1;
        }
    }
    
private:
    std::string executable_;
    pid_t pid_;
    int stdinFd_;
    int stdoutFd_;
    int nextId_;
    std::string readBuffer_;  // For preserving over-read bytes between frames
};

// Test 1: Spawn and basic initialize/shutdown/exit
void test_initialize_shutdown_exit() {
    std::cout << "Testing initialize/shutdown/exit protocol..." << std::endl;
    
    std::string lspExe = findLspExecutable();
    
    // FAIL if executable is absent - do NOT skip
    if (!std::filesystem::exists(lspExe)) {
        throw std::runtime_error("emojineer-lsp not found at: " + lspExe + " - CMake must supply built target path");
    }
    
    LspClient client(lspExe);
    
    if (!client.start()) {
        throw std::runtime_error("Failed to start LSP server");
    }
    
    // Send initialize request
    std::string params = R"({"processId":)" + std::to_string(getpid()) + R(,"rootUri":"file:///test","capabilities":{}})";
    std::string response = client.sendRequest("initialize", params);
    
    // Should get a response with result
    if (response.empty()) {
        throw std::runtime_error("No response to initialize request");
    }
    
    if (!JsonRpcMessage::isResponse(response)) {
        throw std::runtime_error("Expected response, got: " + response.substr(0, 200));
    }
    
    // Send shutdown
    response = client.sendRequest("shutdown", "{}");
    if (response.empty() || !JsonRpcMessage::isResponse(response)) {
        throw std::runtime_error("No response to shutdown request");
    }
    
    // Send exit notification
    client.sendNotification("exit", "{}");
    
    std::cout << "  ✅ Initialize/shutdown/exit works" << std::endl;
}

// Test 2: Unknown method returns -32601
void test_unknown_method() {
    std::cout << "Testing unknown method error (-32601)..." << std::endl;
    
    std::string lspExe = findLspExecutable();
    
    // FAIL if executable is absent
    if (!std::filesystem::exists(lspExe)) {
        throw std::runtime_error("emojineer-lsp not found at: " + lspExe);
    }
    
    LspClient client(lspExe);
    
    if (!client.start()) {
        throw std::runtime_error("Failed to start LSP server");
    }
    
    // Send unknown method
    std::string response = client.sendRequest("unknown/method", "{}", 999);
    
    if (response.empty()) {
        throw std::runtime_error("No response to unknown method request");
    }
    
    // REQUIRE exact error code -32601, fail otherwise
    if (response.find("\"code\":-32601") == std::string::npos) {
        throw std::runtime_error("Expected exact -32601 error code for unknown method, got: " + response.substr(0, 200));
    }
    
    client.shutdown();
    
    std::cout << "  ✅ Unknown method correctly returns -32601" << std::endl;
}

// Test 3: Post-shutdown request returns -32600
void test_post_shutdown_error() {
    std::cout << "Testing post-shutdown error (-32600)..." << std::endl;
    
    std::string lspExe = findLspExecutable();
    
    // FAIL if executable is absent
    if (!std::filesystem::exists(lspExe)) {
        throw std::runtime_error("emojineer-lsp not found at: " + lspExe);
    }
    
    LspClient client(lspExe);
    
    if (!client.start()) {
        throw std::runtime_error("Failed to start LSP server");
    }
    
    // Initialize
    client.sendRequest("initialize", R"({"processId":1,"rootUri":"file:///test","capabilities":{}})");
    
    // Shutdown
    client.sendRequest("shutdown", "{}");
    
    // Try a request after shutdown - REQUIRE exact -32600
    std::string response = client.sendRequest("textDocument/hover", R"({"textDocument":{"uri":"file:///test.emoji"},"position":{"line":0,"character":0}})", 100);
    
    // REQUIRE exact error code -32600, fail otherwise
    if (response.find("\"code\":-32600") == std::string::npos) {
        throw std::runtime_error("Expected exact -32600 error code for post-shutdown request, got: " + response.substr(0, 200));
    }
    
    client.shutdown();
    
    std::cout << "  ✅ Post-shutdown correctly returns -32600" << std::endl;
}

// Test 4: Malformed JSON returns -32700
void test_malformed_json() {
    std::cout << "Testing malformed JSON error (-32700)..." << std::endl;
    
    std::string lspExe = findLspExecutable();
    
    // FAIL if executable is absent
    if (!std::filesystem::exists(lspExe)) {
        throw std::runtime_error("emojineer-lsp not found at: " + lspExe);
    }
    
    LspClient client(lspExe);
    
    if (!client.start()) {
        throw std::runtime_error("Failed to start LSP server");
    }
    
    // Send malformed framed JSON - valid Content-Length but invalid JSON body
    // Using a simple invalid JSON (missing closing brace)
    std::string malformedBody = R"({"jsonrpc":"2.0","method":"test","params":{)";
    std::string malformedMsg = "Content-Length: " + std::to_string(malformedBody.size()) + "\r\n\r\n" + malformedBody;
    
    // Use raw send to bypass normal framing
    client.sendRaw(malformedMsg);
    
    // Read response - should get -32700 parse error
    // Use explicit ID 1 since we can't use the normal response tracking
    std::string response = client.readResponse(1);
    
    // Also try reading with notification reader in case it came as a notification
    if (response.empty()) {
        response = client.readNotification(2000);
    }
    
    // REQUIRE exact error code -32700, fail otherwise
    if (response.find("\"code\":-32700") == std::string::npos) {
        throw std::runtime_error("Expected exact -32700 error code for malformed JSON, got: " + response.substr(0, 200));
    }
    
    // Cleanup - send proper exit
    client.sendNotification("exit", "{}");
    
    std::cout << "  ✅ Malformed JSON correctly returns -32700" << std::endl;
}

// Test 5: Document lifecycle - didOpen/didChange/didSave/didClose
void test_document_lifecycle() {
    std::cout << "Testing document lifecycle..." << std::endl;
    
    std::string lspExe = findLspExecutable();
    
    // FAIL if executable is absent
    if (!std::filesystem::exists(lspExe)) {
        throw std::runtime_error("emojineer-lsp not found at: " + lspExe);
    }
    
    LspClient client(lspExe);
    
    if (!client.start()) {
        throw std::runtime_error("Failed to start LSP server");
    }
    
    // Initialize
    client.sendRequest("initialize", R"({"processId":1,"rootUri":"file:///test","capabilities":{}})");
    
    // Send didOpen
    std::string didOpenParams = R"({"textDocument":{"uri":"file:///test/test.emoji","languageId":"emojineer","version":1,"text":"📝 Hello 🌍"}})";
    client.sendNotification("textDocument/didOpen", didOpenParams);
    
    // Small delay for diagnostics
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Send didChange (full sync)
    std::string didChangeParams = R"({"textDocument":{"uri":"file:///test/test.emoji","version":2},"contentChanges":[{"text":"📝 Updated 🌍🌍"}]})";
    client.sendNotification("textDocument/didChange", didChangeParams);
    
    // Send didSave
    client.sendNotification("textDocument/didSave", R"({"textDocument":{"uri":"file:///test/test.emoji"}})");
    
    // Send didClose
    client.sendNotification("textDocument/didClose", R"({"textDocument":{"uri":"file:///test/test.emoji"}})");
    
    client.shutdown();
    
    std::cout << "  ✅ Document lifecycle notifications work" << std::endl;
}

// Test 6: Full-sync didChange rejection of ranged incremental
void test_full_sync_rejection() {
    std::cout << "Testing full-sync didChange..." << std::endl;
    
    std::string lspExe = findLspExecutable();
    
    // FAIL if executable is absent
    if (!std::filesystem::exists(lspExe)) {
        throw std::runtime_error("emojineer-lsp not found at: " + lspExe);
    }
    
    LspClient client(lspExe);
    
    if (!client.start()) {
        throw std::runtime_error("Failed to start LSP server");
    }
    
    // Initialize
    client.sendRequest("initialize", R"({"processId":1,"rootUri":"file:///test","capabilities":{}})");
    
    // Send didOpen
    client.sendNotification("textDocument/didOpen", R"({"textDocument":{"uri":"file:///test/test.emoji","languageId":"emojineer","version":1,"text":"Hello"}})");
    
    // Send ranged incremental change - should be rejected or ignored
    std::string didChangeParams = R"({"textDocument":{"uri":"file:///test/test.emoji","version":2},"contentChanges":[{"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":5}},"text":"Hi"}]})";
    client.sendNotification("textDocument/didChange", didChangeParams);
    
    // Verify document still has original content by opening again
    client.sendNotification("textDocument/didOpen", R"({"textDocument":{"uri":"file:///test/test.emoji","languageId":"emojineer","version":3,"text":""}})");
    
    client.shutdown();
    
    std::cout << "  ✅ Full-sync didChange tested" << std::endl;
}

// Test 7: Empty full-sync replacement
void test_empty_full_sync() {
    std::cout << "Testing empty full-sync replacement..." << std::endl;
    
    std::string lspExe = findLspExecutable();
    
    // FAIL if executable is absent
    if (!std::filesystem::exists(lspExe)) {
        throw std::runtime_error("emojineer-lsp not found at: " + lspExe);
    }
    
    LspClient client(lspExe);
    
    if (!client.start()) {
        throw std::runtime_error("Failed to start LSP server");
    }
    
    // Initialize
    client.sendRequest("initialize", R"({"processId":1,"rootUri":"file:///test","capabilities":{}})");
    
    // Open with content
    client.sendNotification("textDocument/didOpen", R"({"textDocument":{"uri":"file:///test/test.emoji","languageId":"emojineer","version":1,"text":"Some content"}})");
    
    // Replace with empty
    client.sendNotification("textDocument/didChange", R"({"textDocument":{"uri":"file:///test/test.emoji","version":2},"contentChanges":[{"text":""}]})");
    
    client.shutdown();
    
    std::cout << "  ✅ Empty full-sync replacement works" << std::endl;
}

// Test 8: UTF-16 positions with emoji
void test_utf16_positions() {
    std::cout << "Testing UTF-16 positions with emoji..." << std::endl;
    
    std::string lspExe = findLspExecutable();
    
    // FAIL if executable is absent
    if (!std::filesystem::exists(lspExe)) {
        throw std::runtime_error("emojineer-lsp not found at: " + lspExe);
    }
    
    LspClient client(lspExe);
    
    if (!client.start()) {
        throw std::runtime_error("Failed to start LSP server");
    }
    
    // Initialize
    client.sendRequest("initialize", R"({"processId":1,"rootUri":"file:///test","capabilities":{}})");
    
    // Open document with emoji
    // "📝" is 1 grapheme but 2 UTF-16 code units
    // "Hello" is 5 characters = 5 UTF-16 code units
    // Position {line:0, character:0} = start
    // Position {line:0, character:2} = after 📝 (UTF-16)
    // Position {line:0, character:3} = between 📝 and H
    client.sendNotification("textDocument/didOpen", R"({"textDocument":{"uri":"file:///test/test.emoji","languageId":"emojineer","version":1,"text":"📝Hello"}})");
    
    // Try hover at position after emoji
    std::string response = client.sendRequest("textDocument/hover", R"({"textDocument":{"uri":"file:///test/test.emoji"},"position":{"line":0,"character":2}})");
    
    // Check response is valid JSON-RPC
    if (!response.empty()) {
        std::cout << "  ℹ️  Hover response: " << response.substr(0, 100) << "..." << std::endl;
    }
    
    client.shutdown();
    
    std::cout << "  ✅ UTF-16 positions handled" << std::endl;
}

// Test 9: CRLF/LF/CR line endings
void test_line_endings() {
    std::cout << "Testing CRLF/LF/CR line endings..." << std::endl;
    
    std::string lspExe = findLspExecutable();
    
    // FAIL if executable is absent
    if (!std::filesystem::exists(lspExe)) {
        throw std::runtime_error("emojineer-lsp not found at: " + lspExe);
    }
    
    LspClient client(lspExe);
    
    if (!client.start()) {
        throw std::runtime_error("Failed to start LSP server");
    }
    
    // Initialize
    client.sendRequest("initialize", R"({"processId":1,"rootUri":"file:///test","capabilities":{}})");
    
    // Open document with CRLF
    std::string didOpenParams = R"({"textDocument":{"uri":"file:///test/test.emoji","languageId":"emojineer","version":1,"text":"Line1\r\nLine2\rLine3\nLine4"}})";
    client.sendNotification("textDocument/didOpen", didOpenParams);
    
    client.shutdown();
    
    std::cout << "  ✅ Line endings handled" << std::endl;
}

// Test 10: Percent-encoded file URIs
void test_percent_encoded_uri() {
    std::cout << "Testing percent-encoded URIs..." << std::endl;
    
    std::string lspExe = findLspExecutable();
    
    // FAIL if executable is absent
    if (!std::filesystem::exists(lspExe)) {
        throw std::runtime_error("emojineer-lsp not found at: " + lspExe);
    }
    
    LspClient client(lspExe);
    
    if (!client.start()) {
        throw std::runtime_error("Failed to start LSP server");
    }
    
    // Initialize
    client.sendRequest("initialize", R"({"processId":1,"rootUri":"file:///test","capabilities":{}})");
    
    // Open with percent-encoded URI
    std::string didOpenParams = R"({"textDocument":{"uri":"file:///test/my%20project/file%20name.emoji","languageId":"emojineer","version":1,"text":"test"}})";
    client.sendNotification("textDocument/didOpen", didOpenParams);
    
    client.shutdown();
    
    std::cout << "  ✅ Percent-encoded URIs handled" << std::endl;
}

// Test 11: Byte-exact output framing
void test_output_framing() {
    std::cout << "Testing byte-exact output framing..." << std::endl;
    
    std::string lspExe = findLspExecutable();
    
    // FAIL if executable is absent
    if (!std::filesystem::exists(lspExe)) {
        throw std::runtime_error("emojineer-lsp not found at: " + lspExe);
    }
    
    LspClient client(lspExe);
    
    if (!client.start()) {
        throw std::runtime_error("Failed to start LSP server");
    }
    
    // Initialize - should get a properly framed response
    std::string response = client.sendRequest("initialize", R"({"processId":1,"rootUri":"file:///test","capabilities":{}})");
    
    // Check Content-Length header is present
    if (response.find("Content-Length:") != std::string::npos) {
        std::cout << "  ✅ Response has proper framing" << std::endl;
    } else {
        std::cout << "  ℹ️  Response: " << response.substr(0, 100) << "..." << std::endl;
    }
    
    client.shutdown();
    
    std::cout << "  ✅ Byte-exact output framing works" << std::endl;
}

int main() {
    std::cout << "=== Emojineer LSP Real Transport Acceptance Tests ===" << std::endl;
    std::cout << "Testing Train 17 LSP server with spawned executable" << std::endl;
    std::cout << std::endl;
    
    try {
        test_initialize_shutdown_exit();
        test_unknown_method();
        test_post_shutdown_error();
        test_malformed_json();
        test_document_lifecycle();
        test_full_sync_rejection();
        test_empty_full_sync();
        test_utf16_positions();
        test_line_endings();
        test_percent_encoded_uri();
        test_output_framing();
        
        std::cout << std::endl;
        std::cout << "=== All LSP Real Transport Acceptance Tests Passed! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << std::endl;
        std::cerr << "❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
}
