#include "emojineer/lsp.hpp"
#include "emojineer/ast.hpp"
#include "emojineer/compiler.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/module.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/project.hpp"
#include "emojineer/source_tools.hpp"
#include "emojineer/stdlib.hpp"
#include "emojineer/unicode.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace emojineer {
namespace lsp {

// JSON Value access helpers
static JsonValue getJsonObject(const JsonValue& val, const std::string& key) {
    auto* obj = val.getPtr<std::unordered_map<std::string, JsonValue>>();
    if (obj) {
        auto it = obj->find(key);
        if (it != obj->end()) return it->second;
    }
    return JsonValue(nullptr);
}

static std::string getJsonString(const JsonValue& val) {
    auto* str = val.getPtr<std::string>();
    return str ? *str : "";
}

static double getJsonNumber(const JsonValue& val) {
    auto* num = val.getPtr<double>();
    return num ? *num : 0.0;
}

static bool getJsonBool(const JsonValue& val) {
    auto* b = val.getPtr<bool>();
    return b ? *b : false;
}

static std::vector<JsonValue> getJsonArray(const JsonValue& val) {
    auto* arr = val.getPtr<std::vector<JsonValue>>();
    return arr ? *arr : std::vector<JsonValue>{};
}

namespace {

// Forward declarations for parsing functions
JsonValue parseJsonValue(const std::string& json, std::size_t& pos);

// JSON parsing utilities
void skipWhitespace(const std::string& json, std::size_t& pos) {
    while (pos < json.size() && std::isspace(json[pos])) pos++;
}

// Helper to decode a surrogate pair
static char32_t decodeSurrogatePair(char16_t high, char16_t low) {
    return 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00);
}

std::string parseJsonString(const std::string& json, std::size_t& pos) {
    if (json[pos] != '"') throw std::runtime_error("expected string");
    pos++;
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            switch (json[pos]) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case '/': result += '/'; break;
                case 'u':
                    // Handle \uXXXX escapes
                    if (pos + 4 < json.size()) {
                        std::string hex = json.substr(pos + 1, 4);
                        char32_t codePoint = static_cast<char32_t>(std::stoul(hex, nullptr, 16));
                        pos += 4;
                        
                        // Check for surrogate pair
                        if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                            // High surrogate, expect \uXXXX low surrogate
                            if (pos + 6 < json.size() && json[pos + 1] == '\\' && json[pos + 2] == 'u') {
                                std::string lowHex = json.substr(pos + 3, 4);
                                char32_t lowSurrogate = static_cast<char32_t>(std::stoul(lowHex, nullptr, 16));
                                if (lowSurrogate >= 0xDC00 && lowSurrogate <= 0xDFFF) {
                                    codePoint = decodeSurrogatePair(static_cast<char16_t>(codePoint), static_cast<char16_t>(lowSurrogate));
                                    pos += 6;
                                }
                            }
                        }
                        
                        // Encode as UTF-8
                        if (codePoint < 0x80) {
                            result += static_cast<char>(codePoint);
                        } else if (codePoint < 0x800) {
                            result += static_cast<char>(0xC0 | (codePoint >> 6));
                            result += static_cast<char>(0x80 | (codePoint & 0x3F));
                        } else if (codePoint < 0x10000) {
                            result += static_cast<char>(0xE0 | (codePoint >> 12));
                            result += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (codePoint & 0x3F));
                        } else {
                            result += static_cast<char>(0xF0 | (codePoint >> 18));
                            result += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
                            result += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (codePoint & 0x3F));
                        }
                    }
                    break;
                default: result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        pos++;
    }
    if (pos >= json.size()) throw std::runtime_error("unterminated string");
    pos++;
    return result;
}

JsonValue parseJsonObject(const std::string& json, std::size_t& pos) {
    if (json[pos] != '{') throw std::runtime_error("expected object");
    pos++;
    auto obj = json::makeObject();
    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == '}') {
        pos++;
        return obj;
    }
    bool expectCommaOrEnd = false;
    while (pos < json.size()) {
        skipWhitespace(json, pos);
        
        if (expectCommaOrEnd) {
            if (json[pos] == '}') {
                pos++;
                break;
            }
            if (json[pos] == ',') {
                pos++;
                expectCommaOrEnd = false;
            } else {
                throw std::runtime_error("expected ',' or '}'");
            }
            skipWhitespace(json, pos);
        }
        
        // Parse key
        std::string key = parseJsonString(json, pos);
        skipWhitespace(json, pos);
        
        // Expect colon
        if (pos >= json.size() || json[pos] != ':') {
            throw std::runtime_error("expected ':' after object key");
        }
        pos++;
        skipWhitespace(json, pos);
        
        // Parse value
        JsonValue value = parseJsonValue(json, pos);
        json::objectSet(obj, key, value);
        
        expectCommaOrEnd = true;
    }
    return obj;
}

JsonValue parseJsonArray(const std::string& json, std::size_t& pos) {
    if (json[pos] != '[') throw std::runtime_error("expected array");
    pos++;
    auto arr = json::makeArray();
    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == ']') {
        pos++;
        return arr;
    }
    while (pos < json.size()) {
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
}

JsonValue parseJsonValue(const std::string& json, std::size_t& pos) {
    skipWhitespace(json, pos);
    if (pos >= json.size()) throw std::runtime_error("unexpected end of input");
    
    switch (json[pos]) {
        case '{': return parseJsonObject(json, pos);
        case '[': return parseJsonArray(json, pos);
        case '"': return parseJsonString(json, pos);
        case 't':
            if (json.substr(pos, 4) == "true") { pos += 4; return JsonValue(true); }
            throw std::runtime_error("invalid token");
        case 'f':
            if (json.substr(pos, 5) == "false") { pos += 5; return JsonValue(false); }
            throw std::runtime_error("invalid token");
        case 'n':
            if (json.substr(pos, 4) == "null") { pos += 4; return JsonValue(nullptr); }
            throw std::runtime_error("invalid token");
        default:
            if (json[pos] == '-' || std::isdigit(json[pos])) {
                std::string numStr;
                while (pos < json.size() && (std::isdigit(json[pos]) || json[pos] == '.' || 
                                             json[pos] == '-' || json[pos] == '+' || 
                                             json[pos] == 'e' || json[pos] == 'E')) {
                    numStr += json[pos++];
                }
                return JsonValue(std::stod(numStr));
            }
            throw std::runtime_error("invalid token");
    }
}

JsonValue parseJson(const std::string& json) {
    std::size_t pos = 0;
    return parseJsonValue(json, pos);
}

// JSON serialization
void jsonValueToString(const JsonValue& value, std::ostringstream& out);

// Helper to escape a Unicode code point as \uXXXX
static void encodeUnicodeEscape(std::ostringstream& out, char32_t cp) {
    if (cp < 0x10000) {
        // BMP character - encode as \uXXXX
        char16_t code = static_cast<char16_t>(cp);
        out << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(code);
    } else {
        // Supplementary plane - encode as surrogate pair
        char16_t high = static_cast<char16_t>((cp - 0x10000) >> 10) + 0xD800;
        char16_t low = static_cast<char16_t>((cp - 0x10000) & 0x3FF) + 0xDC00;
        out << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(high);
        out << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(low);
    }
}

// Helper to escape a JSON string value
static void escapeJsonString(const std::string& s, std::ostringstream& out) {
    for (char c : s) {
        unsigned char uc = static_cast<unsigned char>(c);
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '/': out << "\\/"; break;
            default:
                // Escape control characters (0x00-0x1F) and non-ASCII as \uXXXX
                if (uc < 0x20 || uc >= 0x80) {
                    encodeUnicodeEscape(out, static_cast<char32_t>(uc));
                } else {
                    out << c;
                }
                break;
        }
    }
}

void jsonObjectToString(const std::unordered_map<std::string, JsonValue>& obj, std::ostringstream& out) {
    out << '{';
    bool first = true;
    for (const auto& [key, val] : obj) {
        if (!first) out << ',';
        first = false;
        out << '"';
        escapeJsonString(key, out);
        out << "\":";
        jsonValueToString(val, out);
    }
    out << '}';
}

void jsonArrayToString(const std::vector<JsonValue>& arr, std::ostringstream& out) {
    out << '[';
    for (size_t i = 0; i < arr.size(); i++) {
        if (i > 0) out << ',';
        jsonValueToString(arr[i], out);
    }
    out << ']';
}

void jsonValueToString(const JsonValue& value, std::ostringstream& out) {
    if (value.isNull()) {
        out << "null";
    } else if (value.isBool()) {
        out << (value.get<bool>() ? "true" : "false");
    } else if (value.isNumber()) {
        out << std::fixed << std::setprecision(6) << value.get<double>();
    } else if (value.isString()) {
        out << '"';
        escapeJsonString(value.get<std::string>(), out);
        out << '"';
    } else if (value.isArray()) {
        jsonArrayToString(value.get<std::vector<JsonValue>>(), out);
    } else if (value.isObject()) {
        jsonObjectToString(value.get<std::unordered_map<std::string, JsonValue>>(), out);
    }
}

std::string toJson(const JsonValue& value) {
    std::ostringstream out;
    jsonValueToString(value, out);
    return out.str();
}

}  // namespace

LanguageServer::LanguageServer() = default;
LanguageServer::~LanguageServer() = default;

// Percent-decode a string (URI component decoding)
static std::string percentDecode(const std::string& s) {
    std::string result;
    for (std::size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            // Decode percent-encoded character
            std::string hex = s.substr(i + 1, 2);
            try {
                char c = static_cast<char>(std::stoul(hex, nullptr, 16));
                result += c;
                i += 2;
            } catch (...) {
                result += s[i];
            }
        } else if (s[i] == '+') {
            // URL query string encoding: + represents space
            result += ' ';
        } else {
            result += s[i];
        }
    }
    return result;
}

// Percent-encode a string (URI component encoding)
static std::string percentEncode(const std::string& s) {
    std::string result;
    for (unsigned char c : s) {
        // Characters that don't need encoding: A-Z a-z 0-9 - _ . ~ 
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else {
            // Percent-encode
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            result += hex;
        }
    }
    return result;
}

std::string LanguageServer::uriToPath(const std::string& uri) const {
    if (uri.rfind("file://", 0) == 0) {
        std::string path = uri.substr(7);
        
        // Decode percent-encoded characters
        path = percentDecode(path);
        
        // Handle Windows paths (e.g., /C:/Users/... or C:/Users/...)
        if (path.size() >= 3 && path[0] == '/' && path[2] == ':') {
            path = path.substr(1); // Remove leading slash from /C:/
        } else if (path.size() >= 2 && path[1] == ':') {
            // Already has drive letter like C:\
            // Keep as-is
        }
        
        return path;
    }
    // Only accept file:// URIs - reject other URI schemes for security
    return "";
}

std::string LanguageServer::pathToUri(const std::filesystem::path& path) const {
    std::string pathStr = path.string();
    
    // Convert backslashes to forward slashes for URI
    std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
    
    // Percent-encode special characters
    pathStr = percentEncode(pathStr);
    
    // On Windows, add leading slash if not present (e.g., C:/... -> /C:/...)
    if (pathStr.size() >= 2 && pathStr[1] == ':') {
        pathStr = "/" + pathStr;
    }
    
    return "file://" + pathStr;
}

std::optional<OpenDocument> LanguageServer::getDocument(const std::string& uri) const {
    auto it = openDocuments_.find(uri);
    if (it != openDocuments_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void LanguageServer::openDocument(const std::string& uri, const std::string& text, int version) {
    OpenDocument doc;
    doc.uri = uri;
    doc.path = uriToPath(uri);
    doc.text = text;
    doc.version = version;
    doc.diagnostics = diagnoseDocument(doc);
    openDocuments_[uri] = doc;
    publishDiagnostics(uri, doc.diagnostics);
}

void LanguageServer::updateDocument(const std::string& uri, const std::string& text, int version) {
    auto it = openDocuments_.find(uri);
    if (it != openDocuments_.end()) {
        it->second.text = text;
        it->second.version = version;
        it->second.diagnostics = diagnoseDocument(it->second);
        publishDiagnostics(uri, it->second.diagnostics);
    }
}

void LanguageServer::saveDocument(const std::string& uri) {
    auto it = openDocuments_.find(uri);
    if (it != openDocuments_.end()) {
        it->second.diagnostics = diagnoseDocument(it->second);
        publishDiagnostics(uri, it->second.diagnostics);
    }
}

void LanguageServer::closeDocument(const std::string& uri) {
    openDocuments_.erase(uri);
}

// Helper to count UTF-16 code units for a UTF-8 code point
static std::uint32_t countUtf16Units(unsigned char byte) {
    if ((byte & 0x80) == 0) return 1;       // ASCII
    if ((byte & 0xE0) == 0xC0) return 1;    // 2-byte sequence, 1 UTF-16 unit
    if ((byte & 0xF0) == 0xE0) return 1;    // 3-byte sequence, 1 UTF-16 unit (BMP)
    if ((byte & 0xF8) == 0xF0) return 2;    // 4-byte sequence, 2 UTF-16 units (surrogate pair)
    return 1;
}

// Convert UTF-8 byte index to UTF-16 position (line, column)
Position LanguageServer::utf8ToUtf16(const std::string& text, std::size_t utf8Offset) const {
    Position pos;
    std::size_t utf8Pos = 0;
    std::uint32_t utf16Col = 0;
    
    // Clamp offset to text length
    utf8Offset = std::min(utf8Offset, text.size());
    
    while (utf8Pos < utf8Offset) {
        unsigned char byte = static_cast<unsigned char>(text[utf8Pos]);
        
        if (byte == '\n') {
            pos.line++;
            utf16Col = 0;
        } else {
            // Count UTF-16 code units for this character
            utf16Col += countUtf16Units(byte);
            
            // Skip continuation bytes
            if ((byte & 0xC0) == 0x80) {
                // Continuation byte, already counted
            } else {
                // Determine sequence length
                int seqLen = 0;
                if ((byte & 0x80) == 0) seqLen = 1;
                else if ((byte & 0xE0) == 0xC0) seqLen = 2;
                else if ((byte & 0xF0) == 0xE0) seqLen = 3;
                else if ((byte & 0xF8) == 0xF0) seqLen = 4;
                
                // For supplementary plane (4-byte sequences), count as 2 UTF-16 units
                if (seqLen == 4) {
                    utf16Col++; // Add extra unit for surrogate pair
                }
                
                // Skip continuation bytes
                for (int i = 1; i < seqLen && utf8Pos + i < utf8Offset; i++) {
                    // continuation bytes don't add to utf8Pos separately
                }
            }
        }
        utf8Pos++;
    }
    
    pos.character = utf16Col;
    return pos;
}

// Convert UTF-16 position to UTF-8 byte offset
std::size_t LanguageServer::utf16ToUtf8(const std::string& text, std::uint32_t line, std::uint32_t utf16Col) const {
    std::uint32_t currentLine = 0;
    std::uint32_t currentCol = 0;
    std::size_t utf8Offset = 0;
    
    while (utf8Offset < text.size()) {
        unsigned char byte = static_cast<unsigned char>(text[utf8Offset]);
        
        if (byte == '\n') {
            if (currentLine == line) {
                // At end of target line
                return utf8Offset;
            }
            currentLine++;
            currentCol = 0;
            utf8Offset++;
            continue;
        }
        
        if (currentLine == line) {
            // Count UTF-16 code units
            std::uint32_t units = countUtf16Units(byte);
            
            // For 4-byte sequences (supplementary plane), count as 2 units
            if ((byte & 0xF8) == 0xF0) {
                units = 2;
            }
            
            if (currentCol + units > utf16Col) {
                // Found the position
                return utf8Offset;
            }
            currentCol += units;
        }
        
        // Determine sequence length and skip
        int seqLen = 1;
        if ((byte & 0x80) == 0) seqLen = 1;
        else if ((byte & 0xE0) == 0xC0) seqLen = 2;
        else if ((byte & 0xF0) == 0xE0) seqLen = 3;
        else if ((byte & 0xF8) == 0xF0) seqLen = 4;
        
        utf8Offset += seqLen;
    }
    
    return utf8Offset;
}

std::vector<Diagnostic> LanguageServer::diagnoseDocument(const OpenDocument& doc) {
    std::vector<Diagnostic> diagnostics;
    
    if (doc.text.empty()) return diagnostics;
    
    try {
        Lexer lexer(doc.text, registry_);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens));
        auto program = parser.parse();
        Compiler compiler;
        compiler.compile(program);
    } catch (const std::exception& e) {
        std::string errorMsg = e.what();
        std::size_t line = 1;
        
        std::size_t linePos = errorMsg.find("line ");
        if (linePos != std::string::npos) {
            try {
                line = std::stoul(errorMsg.substr(linePos + 5));
            } catch (...) {}
        }
        
        Position startPos;
        startPos.line = static_cast<std::uint32_t>(line - 1);
        startPos.character = 0;
        
        Position endPos = startPos;
        endPos.character = 80;
        
        Diagnostic diag;
        diag.range = {startPos, endPos};
        diag.severity = 1;
        diag.message = errorMsg;
        diag.source = "emojineer";
        diagnostics.push_back(diag);
    }
    
    return diagnostics;
}

void LanguageServer::publishDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics) {
    auto params = json::makeObject();
    json::objectSet(params, "uri", JsonValue(uri));
    
    auto diagJson = json::makeArray();
    for (const auto& d : diagnostics) {
        auto diag = json::makeObject();
        
        auto range = json::makeObject();
        auto start = json::makeObject();
        json::objectSet(start, "line", JsonValue(static_cast<double>(d.range.start.line)));
        json::objectSet(start, "character", JsonValue(static_cast<double>(d.range.start.character)));
        auto end = json::makeObject();
        json::objectSet(end, "line", JsonValue(static_cast<double>(d.range.end.line)));
        json::objectSet(end, "character", JsonValue(static_cast<double>(d.range.end.character)));
        json::objectSet(range, "start", start);
        json::objectSet(range, "end", end);
        json::objectSet(diag, "range", range);
        
        json::objectSet(diag, "severity", JsonValue(static_cast<double>(d.severity)));
        json::objectSet(diag, "message", JsonValue(d.message));
        if (d.source) json::objectSet(diag, "source", JsonValue(*d.source));
        
        json::arrayPushBack(diagJson, diag);
    }
    json::objectSet(params, "diagnostics", diagJson);
    
    // Send proper LSP notification through stdout with Content-Length framing
    auto notificationObj = json::makeObject();
    json::objectSet(notificationObj, "jsonrpc", JsonValue(std::string("2.0")));
    json::objectSet(notificationObj, "method", JsonValue(std::string("textDocument/publishDiagnostics")));
    json::objectSet(notificationObj, "params", params);
    std::string jsonBody = toJson(notificationObj);
    std::ostringstream framed;
    framed << "Content-Length: " << jsonBody.size() << "\r\n\r\n" << jsonBody;
    std::cout << framed.str();
    std::cout.flush();
}

std::optional<ProjectManifest> LanguageServer::getProjectManifest(const std::filesystem::path& path) const {
    if (!workspaceRoot_) return std::nullopt;
    auto manifestPath = *workspaceRoot_ / "emojineer.toml";
    if (std::filesystem::exists(manifestPath)) {
        return load_project_manifest(manifestPath);
    }
    return std::nullopt;
}

std::optional<ProjectLock> LanguageServer::getProjectLock(const std::filesystem::path& path) const {
    if (!workspaceRoot_) return std::nullopt;
    auto lockPath = *workspaceRoot_ / "emojineer.lock";
    if (std::filesystem::exists(lockPath)) {
        return load_project_lock(lockPath);
    }
    return std::nullopt;
}

void LanguageServer::discoverWorkspace(const std::filesystem::path& root) {
    workspaceRoot_ = root;
    manifest_ = getProjectManifest(root);
    if (manifest_) {
        lock_ = getProjectLock(root);
        if (lock_) {
            packageStore_ = PackageStore{package_store_root(root)};
        }
    }
}

JsonValue LanguageServer::handleInitialize(const JsonValue& params) {
    initialized_ = true;
    
    const auto* paramsObj = params.getPtr<std::unordered_map<std::string, JsonValue>>();
    if (paramsObj) {
        auto rootUriIt = paramsObj->find("rootUri");
        if (rootUriIt != paramsObj->end()) {
            std::string rootUri = getJsonString(rootUriIt->second);
            if (!rootUri.empty()) {
                auto path = uriToPath(rootUri);
                if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
                    discoverWorkspace(path);
                }
            }
        }
    }
    
    auto result = json::makeObject();
    auto caps = json::makeObject();
    json::objectSet(caps, "textDocumentSync", JsonValue(1));
    json::objectSet(caps, "hoverProvider", JsonValue(true));
    
    auto compOpts = json::makeObject();
    json::objectSet(compOpts, "resolveProvider", JsonValue(false));
    json::objectSet(caps, "completionProvider", compOpts);
    
    json::objectSet(caps, "definitionProvider", JsonValue(true));
    json::objectSet(caps, "referencesProvider", JsonValue(true));
    json::objectSet(caps, "documentSymbolProvider", JsonValue(true));
    json::objectSet(caps, "workspaceSymbolProvider", JsonValue(true));
    json::objectSet(caps, "documentFormattingProvider", JsonValue(true));
    json::objectSet(caps, "documentRangeFormattingProvider", JsonValue(true));
    
    json::objectSet(result, "capabilities", caps);
    
    auto serverInfo = json::makeObject();
    json::objectSet(serverInfo, "name", JsonValue("emojineer-lsp"));
    json::objectSet(serverInfo, "version", JsonValue("0.17.0"));
    json::objectSet(result, "serverInfo", serverInfo);
    
    return result;
}

JsonValue LanguageServer::handleShutdown(const JsonValue& params) {
    shutdown_ = true;
    return JsonValue(nullptr);
}

void LanguageServer::handleExit(const JsonValue& params) {
    std::exit(shutdown_ ? 0 : 1);
}

void LanguageServer::handleDidOpenTextDocument(const JsonValue& params) {
    auto paramsObj = getJsonObject(params, "textDocument");
    if (paramsObj.isNull()) return;
    
    std::string uri = getJsonObject(paramsObj, "uri").getPtr<std::string>() ? 
                      *getJsonObject(paramsObj, "uri").getPtr<std::string>() : "";
    double version = getJsonNumber(getJsonObject(paramsObj, "version"));
    std::string text = getJsonObject(paramsObj, "text").getPtr<std::string>() ?
                       *getJsonObject(paramsObj, "text").getPtr<std::string>() : "";
    
    if (!uri.empty()) {
        openDocument(uri, text, static_cast<int>(version));
    }
}

void LanguageServer::handleDidChangeTextDocument(const JsonValue& params) {
    auto paramsObj = getJsonObject(params, "textDocument");
    if (paramsObj.isNull()) return;
    
    std::string uri = getJsonObject(paramsObj, "uri").getPtr<std::string>() ?
                      *getJsonObject(paramsObj, "uri").getPtr<std::string>() : "";
    double version = getJsonNumber(getJsonObject(paramsObj, "version"));
    
    auto changes = getJsonObject(params, "contentChanges");
    auto arr = getJsonArray(changes);
    if (!arr.empty()) {
        std::string text = getJsonString(arr[arr.size() - 1]);
        if (!uri.empty()) {
            updateDocument(uri, text, static_cast<int>(version));
        }
    }
}

void LanguageServer::handleDidSaveTextDocument(const JsonValue& params) {
    auto paramsObj = getJsonObject(params, "textDocument");
    if (paramsObj.isNull()) return;
    
    std::string uri = getJsonObject(paramsObj, "uri").getPtr<std::string>() ?
                      *getJsonObject(paramsObj, "uri").getPtr<std::string>() : "";
    if (!uri.empty()) {
        saveDocument(uri);
    }
}

void LanguageServer::handleDidCloseTextDocument(const JsonValue& params) {
    auto paramsObj = getJsonObject(params, "textDocument");
    if (paramsObj.isNull()) return;
    
    std::string uri = getJsonObject(paramsObj, "uri").getPtr<std::string>() ?
                      *getJsonObject(paramsObj, "uri").getPtr<std::string>() : "";
    if (!uri.empty()) {
        closeDocument(uri);
    }
}

void LanguageServer::handleInitialized(const JsonValue& params) {}

JsonValue LanguageServer::handleHover(const JsonValue& params) {
    // Extract URI from textDocument
    auto textDoc = getJsonObject(params, "textDocument");
    std::string uri = getJsonString(getJsonObject(textDoc, "uri"));
    
    auto doc = getDocument(uri);
    if (!doc) return JsonValue(nullptr);
    
    auto posObj = getJsonObject(params, "position");
    std::uint32_t line = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "line")));
    std::uint32_t char_ = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "character")));
    
    auto hover = getHover(doc->uri, Position{line, char_});
    if (!hover) return JsonValue(nullptr);
    
    auto result = json::makeObject();
    if (hover->contents) {
        auto contents = json::makeObject();
        json::objectSet(contents, "kind", JsonValue("markdown"));
        json::objectSet(contents, "value", JsonValue(hover->contents->value));
        json::objectSet(result, "contents", contents);
    }
    return result;
}

std::optional<Hover> LanguageServer::getHover(const std::string& uri, const Position& pos) {
    auto doc = getDocument(uri);
    if (!doc) return std::nullopt;
    
    try {
        Lexer lexer(doc->text, registry_);
        auto tokens = lexer.tokenize();
        
        for (const auto& token : tokens) {
            if (token.kind == TokenKind::Eof) continue;
            
            if (token.line == pos.line + 1) {
                Hover hover;
                hover.contents = MarkupContent{"markdown", ""};
                
                const auto& defs = registry_.definitions();
                for (const auto& def : defs) {
                    if (def.kind == token.kind) {
                        hover.contents->value = "**" + def.alias + "**\n\n" + def.description;
                        return hover;
                    }
                }
                
                if (token.kind == TokenKind::Identifier) {
                    hover.contents->value = "Identifier: `" + token.lexeme + "`";
                    return hover;
                }
                
                hover.contents->value = "Token: " + token_kind_name(token.kind);
                return hover;
            }
        }
    } catch (...) {}
    
    return std::nullopt;
}

JsonValue LanguageServer::handleCompletion(const JsonValue& params) {
    auto posObj = getJsonObject(params, "position");
    std::uint32_t line = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "line")));
    std::uint32_t char_ = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "character")));
    
    auto completions = getCompletions("", Position{line, char_});
    
    auto result = json::makeObject();
    json::objectSet(result, "isIncomplete", JsonValue(false));
    
    auto items = json::makeArray();
    for (const auto& item : completions) {
        auto itemJson = json::makeObject();
        json::objectSet(itemJson, "label", JsonValue(item.label));
        // Use numeric kind for LSP CompletionItemKind
        if (item.kind) json::objectSet(itemJson, "kind", JsonValue(static_cast<double>(*item.kind)));
        if (item.detail) json::objectSet(itemJson, "detail", JsonValue(*item.detail));
        if (item.documentation) json::objectSet(itemJson, "documentation", JsonValue(*item.documentation));
        json::arrayPushBack(items, itemJson);
    }
    json::objectSet(result, "items", items);
    
    return result;
}

std::vector<CompletionItem> LanguageServer::getCompletions(const std::string& uri, const Position& pos) {
    std::vector<CompletionItem> completions;
    
    const std::vector<std::pair<std::string, std::string>> keywords = {
        {"🧑‍💻", "variable declaration"},
        {"📤", "export statement"},
        {"🔗", "import statement"},
        {"🧩", "module declaration"},
        {"📦", "return statement"},
        {"🤔", "if statement"},
        {"🙅", "else statement"},
        {"🔁", "while loop"},
        {"🛠️", "function declaration"},
        {"📥", "input expression"},
        {"📝", "print statement"},
        {"🔢", "number type"},
        {"✅", "boolean true"},
        {"❌", "boolean false"},
    };
    
    for (const auto& [emoji, desc] : keywords) {
        CompletionItem item;
        item.label = emoji;
        item.detail = desc;
        completions.push_back(item);
    }
    
    for (const auto& def : registry_.definitions()) {
        if (!def.custom) {
            CompletionItem item;
            item.label = def.alias;
            item.detail = def.description;
            completions.push_back(item);
        }
    }
    
    for (const auto& module : standard_modules()) {
        CompletionItem item;
        item.label = std::string(module.specifier);
        item.detail = std::string(module.description);
        completions.push_back(item);
    }
    
    return completions;
}

JsonValue LanguageServer::handleDefinition(const JsonValue& params) {
    auto posObj = getJsonObject(params, "position");
    std::uint32_t line = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "line")));
    std::uint32_t char_ = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "character")));
    
    auto defs = findDefinitions("", Position{line, char_});
    
    auto result = json::makeArray();
    for (const auto& def : defs) {
        auto loc = json::makeObject();
        json::objectSet(loc, "uri", JsonValue(def.uri));
        
        auto range = json::makeObject();
        auto start = json::makeObject();
        json::objectSet(start, "line", JsonValue(static_cast<double>(def.range.start.line)));
        json::objectSet(start, "character", JsonValue(static_cast<double>(def.range.start.character)));
        auto end = json::makeObject();
        json::objectSet(end, "line", JsonValue(static_cast<double>(def.range.end.line)));
        json::objectSet(end, "character", JsonValue(static_cast<double>(def.range.end.character)));
        json::objectSet(range, "start", start);
        json::objectSet(range, "end", end);
        json::objectSet(loc, "range", range);
        
        json::arrayPushBack(result, loc);
    }
    
    return result;
}

std::vector<SymbolLocation> LanguageServer::findDefinitions(const std::string& uri, const Position& pos) {
    return {};
}

std::vector<SymbolLocation> LanguageServer::findReferences(const std::string& uri, const Position& pos) {
    return {};
}

JsonValue LanguageServer::handleDocumentSymbol(const JsonValue& params) {
    return json::makeArray();
}

std::vector<DocumentSymbol> LanguageServer::getDocumentSymbols(const std::string& uri) {
    return {};
}

JsonValue LanguageServer::handleWorkspaceSymbol(const JsonValue& params) {
    return json::makeArray();
}

std::vector<SymbolInformation> LanguageServer::getWorkspaceSymbols(const std::string& query) {
    return {};
}

JsonValue LanguageServer::handleFormatting(const JsonValue& params) {
    return json::makeArray();
}

JsonValue LanguageServer::handleRangeFormatting(const JsonValue& params) {
    return handleFormatting(params);
}

JsonValue LanguageServer::handleRequest(const std::string& method, const JsonValue& params) {
    if (method == "initialize") return handleInitialize(params);
    if (method == "shutdown") return handleShutdown(params);
    if (method == "textDocument/hover") return handleHover(params);
    if (method == "textDocument/completion") return handleCompletion(params);
    if (method == "textDocument/definition") return handleDefinition(params);
    if (method == "textDocument/references") return handleReferences(params);
    if (method == "textDocument/documentSymbol") return handleDocumentSymbol(params);
    if (method == "workspace/symbol") return handleWorkspaceSymbol(params);
    if (method == "textDocument/formatting") return handleFormatting(params);
    if (method == "textDocument/rangeFormatting") return handleRangeFormatting(params);
    return JsonValue(nullptr);
}

void LanguageServer::handleNotification(const std::string& method, const JsonValue& params) {
    if (method == "initialized") handleInitialized(params);
    else if (method == "textDocument/didOpen") handleDidOpenTextDocument(params);
    else if (method == "textDocument/didChange") handleDidChangeTextDocument(params);
    else if (method == "textDocument/didSave") handleDidSaveTextDocument(params);
    else if (method == "textDocument/didClose") handleDidCloseTextDocument(params);
    else if (method == "exit") handleExit(params);
}

void LanguageServer::handleMessage(const JsonValue& message) {
    auto* obj = message.getPtr<std::unordered_map<std::string, JsonValue>>();
    if (!obj) return;
    
    auto methodIt = obj->find("method");
    if (methodIt == obj->end()) return;
    
    std::string method = getJsonString(methodIt->second);
    const JsonValue& params = getJsonObject(message, "params");
    const JsonValue& idVal = getJsonObject(message, "id");
    
    // Support both integer and string request IDs
    std::optional<int> intId;
    std::optional<std::string> stringId;
    if (!idVal.isNull()) {
        if (idVal.isNumber()) {
            intId = static_cast<int>(getJsonNumber(idVal));
        } else if (idVal.isString()) {
            stringId = idVal.get<std::string>();
        }
    }
    
    // Handle the response - prefer string IDs if present
    if (stringId || intId) {
        try {
            auto result = handleRequest(method, params);
            if (stringId) {
                // Use string ID format
                std::cout << formatJsonRpcResponseStringId(result, *stringId) << "\n";
            } else {
                std::cout << formatJsonRpcResponse(result, intId) << "\n";
            }
        } catch (const std::exception& e) {
            if (stringId) {
                std::cout << formatJsonRpcErrorStringId(-32603, e.what(), *stringId) << "\n";
            } else {
                std::cout << formatJsonRpcError(-32603, e.what(), intId) << "\n";
            }
        }
    } else {
        handleNotification(method, params);
    }
}

int LanguageServer::run() {
    std::string buffer;
    std::string line;
    
    while (std::getline(std::cin, line)) {
        buffer += line;
        buffer += '\n';
        
        if (buffer.find("Content-Length:") != std::string::npos) {
            size_t headerEnd = buffer.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                size_t contentLengthStart = buffer.find("Content-Length:") + 16;
                size_t contentLengthEnd = buffer.find("\r\n", contentLengthStart);
                int contentLength = std::stoi(buffer.substr(contentLengthStart, contentLengthEnd - contentLengthStart));
                
                size_t bodyStart = headerEnd + 4;
                if (buffer.size() >= bodyStart + contentLength) {
                    std::string body = buffer.substr(bodyStart, contentLength);
                    buffer = buffer.substr(bodyStart + contentLength);
                    
                    try {
                        auto json = parseJson(body);
                        handleMessage(json);
                    } catch (const std::exception& e) {
                        std::cerr << "LSP error: " << e.what() << std::endl;
                    }
                }
            }
        } else {
            try {
                auto json = parseJson(buffer);
                buffer.clear();
                handleMessage(json);
            } catch (...) {}
        }
    }
    
    return 0;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open '" + path.string() + "'");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot write '" + path.string() + "'");
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string formatJsonRpc(const std::string& method, const JsonValue& params, std::optional<int> id) {
    auto obj = json::makeObject();
    json::objectSet(obj, "jsonrpc", JsonValue(std::string("2.0")));
    json::objectSet(obj, "method", JsonValue(method));
    json::objectSet(obj, "params", params);
    if (id) json::objectSet(obj, "id", JsonValue(static_cast<double>(*id)));
    return toJson(obj);
}

std::string formatJsonRpcResponse(const JsonValue& result, std::optional<int> id) {
    auto obj = json::makeObject();
    json::objectSet(obj, "jsonrpc", JsonValue(std::string("2.0")));
    json::objectSet(obj, "result", result);
    if (id) json::objectSet(obj, "id", JsonValue(static_cast<double>(*id)));
    
    std::string body = toJson(obj);
    std::ostringstream out;
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    return out.str();
}

std::string formatJsonRpcError(int code, const std::string& message, std::optional<int> id) {
    auto obj = json::makeObject();
    json::objectSet(obj, "jsonrpc", JsonValue(std::string("2.0")));
    
    auto error = json::makeObject();
    json::objectSet(error, "code", JsonValue(static_cast<double>(code)));
    json::objectSet(error, "message", JsonValue(message));
    json::objectSet(obj, "error", error);
    
    if (id) json::objectSet(obj, "id", JsonValue(static_cast<double>(*id)));
    
    std::string body = toJson(obj);
    std::ostringstream out;
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    return out.str();
}

// String ID variants for JSON-RPC 2.0
std::string formatJsonRpcResponseStringId(const JsonValue& result, const std::string& id) {
    auto obj = json::makeObject();
    json::objectSet(obj, "jsonrpc", JsonValue(std::string("2.0")));
    json::objectSet(obj, "result", result);
    json::objectSet(obj, "id", JsonValue(id));
    
    std::string body = toJson(obj);
    std::ostringstream out;
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    return out.str();
}

std::string formatJsonRpcErrorStringId(int code, const std::string& message, const std::string& id) {
    auto obj = json::makeObject();
    json::objectSet(obj, "jsonrpc", JsonValue(std::string("2.0")));
    
    auto error = json::makeObject();
    json::objectSet(error, "code", JsonValue(static_cast<double>(code)));
    json::objectSet(error, "message", JsonValue(message));
    json::objectSet(obj, "error", error);
    
    json::objectSet(obj, "id", JsonValue(id));
    
    std::string body = toJson(obj);
    std::ostringstream out;
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    return out.str();
}

std::string formatNotification(const std::string& method, const JsonValue& params) {
    return formatJsonRpc(method, params, std::nullopt);
}

}  // namespace lsp
}  // namespace emojineer
