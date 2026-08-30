#include "emojineer/lsp.hpp"
#include "emojineer/ast.hpp"
#include "emojineer/compiler.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/module.hpp"
#include "emojineer/package.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/project.hpp"
#include "emojineer/source_tools.hpp"
#include "emojineer/stdlib.hpp"
#include "emojineer/unicode.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace emojineer {
namespace lsp {

// JSON-RPC error exception with specific error code
class JsonRpcError : public std::runtime_error {
public:
    JsonRpcError(int code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    int code() const { return code_; }

private:
    int code_;
};

// JSON-RPC error codes
constexpr int JSONRPC_INVALID_REQUEST = -32600;
constexpr int JSONRPC_METHOD_NOT_FOUND = -32601;
constexpr int JSONRPC_INTERNAL_ERROR = -32603;

// Helper: saturating cast from size_t to uint32_t to avoid narrowing warnings
// Clamps to UINT32_MAX if value exceeds range, since LSP uses uint32_t for positions
static constexpr std::uint32_t UINT32_MAX_VALUE = 0xFFFFFFFFu;

static std::uint32_t safeSizeToUint32(std::size_t value) {
    if (value > UINT32_MAX_VALUE) {
        return UINT32_MAX_VALUE;
    }
    return static_cast<std::uint32_t>(value);
}

// JSON Value access helpers
static JsonValue getJsonObject(const JsonValue& val, const std::string& key) {
    auto* obj = val.getPtr<std::map<std::string, JsonValue>>();
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
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) pos++;
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
                case 'u': {
                    // Handle \uXXXX escapes - validate hex and surrogate pairs
                    if (pos + 4 >= json.size()) {
                        throw std::runtime_error("incomplete \\u escape sequence");
                    }
                    // Validate that all 4 characters are valid hex
                    for (int i = 1; i <= 4; i++) {
                        char c = json[pos + i];
                        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                            throw std::runtime_error("invalid hex character in \\u escape");
                        }
                    }
                    std::string hex = json.substr(pos + 1, 4);
                    char32_t codePoint = static_cast<char32_t>(std::stoul(hex, nullptr, 16));
                    pos += 4;

                    // Check for surrogate pair
                    if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                        // High surrogate, expect \uXXXX low surrogate
                        if (pos + 6 < json.size() && json[pos + 1] == '\\' && json[pos + 2] == 'u') {
                            // Validate low surrogate hex
                            for (int i = 3; i <= 6; i++) {
                                char c = json[pos + i];
                                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                                    throw std::runtime_error("invalid hex character in \\u escape");
                                }
                            }
                            std::string lowHex = json.substr(pos + 3, 4);
                            char32_t lowSurrogate = static_cast<char32_t>(std::stoul(lowHex, nullptr, 16));
                            if (lowSurrogate >= 0xDC00 && lowSurrogate <= 0xDFFF) {
                                codePoint = decodeSurrogatePair(static_cast<char16_t>(codePoint), static_cast<char16_t>(lowSurrogate));
                                pos += 6;
                            } else {
                                throw std::runtime_error("invalid surrogate pair: low surrogate out of range");
                            }
                        } else {
                            throw std::runtime_error("invalid surrogate pair: missing low surrogate");
                        }
                    } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
                        // Low surrogate without high surrogate - invalid
                        throw std::runtime_error("invalid surrogate: low surrogate without high surrogate");
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
                    break;
                }
                default: throw std::runtime_error(std::string("unknown escape sequence: \\") + json[pos]);
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
                return obj;
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
    // If we exit the loop without finding }, it's an error
    throw std::runtime_error("unexpected end of input while parsing object: missing '}'");
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
        if (pos >= json.size()) {
            throw std::runtime_error("unexpected end of input while parsing array");
        }
        json::arrayPushBack(arr, parseJsonValue(json, pos));
        skipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == ']') {
            pos++;
            return arr;
        }
        if (pos >= json.size() || json[pos] != ',') {
            throw std::runtime_error("expected ',' or ']' in array");
        }
        pos++;  // Skip the comma
    }
    throw std::runtime_error("unexpected end of input while parsing array: missing ']'");
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
            if (json[pos] == '-' || std::isdigit(static_cast<unsigned char>(json[pos]))) {
                std::string numStr;
                while (pos < json.size() && (std::isdigit(static_cast<unsigned char>(json[pos])) || json[pos] == '.' ||
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
    skipWhitespace(json, pos);
    JsonValue result = parseJsonValue(json, pos);
    skipWhitespace(json, pos);
    if (pos < json.size()) {
        throw std::runtime_error("unexpected content after JSON value");
    }
    return result;
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

// Helper: decode a single UTF-8 code point, returning the code point and advancing the index.
// Returns true on success, false on invalid UTF-8.
static bool decodeUtf8CodePoint(const std::string& text, std::size_t& pos, char32_t& codePoint) {
    if (pos >= text.size()) return false;

    unsigned char byte = static_cast<unsigned char>(text[pos]);

    // Single byte (ASCII)
    if ((byte & 0x80) == 0) {
        codePoint = byte;
        pos += 1;
        return true;
    }

    // Determine sequence length from first byte
    int seqLen = 0;
    char32_t cp = 0;

    if ((byte & 0xE0) == 0xC0) {
        seqLen = 2;
        cp = byte & 0x1F;
    } else if ((byte & 0xF0) == 0xE0) {
        seqLen = 3;
        cp = byte & 0x0F;
    } else if ((byte & 0xF8) == 0xF0) {
        seqLen = 4;
        cp = byte & 0x07;
    } else {
        // Invalid leading byte
        return false;
    }

    // Check we have enough bytes
    if (pos + seqLen > text.size()) return false;

    // Decode continuation bytes
    for (int i = 1; i < seqLen; i++) {
        unsigned char cb = static_cast<unsigned char>(text[pos + i]);
        if ((cb & 0xC0) != 0x80) return false;  // Not a continuation byte
        cp = (cp << 6) | (cb & 0x3F);
    }

    // Validate code point
    if (cp > 0x10FFFF) return false;  // Out of Unicode range
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;  // Surrogates invalid in UTF-8

    // Reject overlong encodings - each sequence length has a minimum code point
    if (seqLen == 2 && cp < 0x80) return false;   // 2-byte must encode >= U+0080
    if (seqLen == 3 && cp < 0x800) return false;  // 3-byte must encode >= U+0800
    if (seqLen == 4 && cp < 0x10000) return false; // 4-byte must encode >= U+10000

    codePoint = cp;
    pos += seqLen;
    return true;
}

// Helper to escape a JSON string value - properly handles UTF-8
// Only escapes control characters and special characters; passes through
// valid UTF-8 including ASCII letters, numbers, and emoji
static void escapeJsonString(const std::string& s, std::ostringstream& out) {
    std::size_t pos = 0;
    while (pos < s.size()) {
        char32_t codePoint = 0;
        std::size_t oldPos = pos;

        if (decodeUtf8CodePoint(s, pos, codePoint)) {
            // Handle the Unicode code point
            switch (codePoint) {
                case '"': out << "\\\""; break;
                case '\\': out << "\\\\"; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                case '\b': out << "\\b"; break;
                case '\f': out << "\\f"; break;
                case '/': out << "\\/"; break;
                default:
                    // Only escape control characters (0x00-0x1F)
                    // Pass through all printable characters including ASCII letters, numbers, emoji
                    if (codePoint < 0x20) {
                        encodeUnicodeEscape(out, codePoint);
                    } else {
                        // Output as UTF-8 bytes directly
                        out << s.substr(oldPos, pos - oldPos);
                    }
                    break;
            }
        } else {
            // Invalid UTF-8 byte - treat as isolated byte
            unsigned char uc = static_cast<unsigned char>(s[oldPos]);
            if (uc < 0x20) {
                encodeUnicodeEscape(out, static_cast<char32_t>(uc));
            } else {
                out << static_cast<char>(uc);
            }
            pos = oldPos + 1;
        }
    }
}

void jsonObjectToString(const std::map<std::string, JsonValue>& obj, std::ostringstream& out) {
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
        // Use deterministic number formatting: no scientific notation,
        // no trailing zeros for integers, precise decimal for floats
        double num = value.get<double>();
        // Check if the number is effectively an integer
        if (num == std::floor(num) && std::abs(num) < 1e15) {
            // Output as integer (no decimal point)
            out << static_cast<long long>(num);
        } else {
            // Output with minimal precision needed for round-trip
            // Use 'g' with high precision for deterministic output
            std::ostringstream tmp;
            tmp << std::setprecision(15) << std::noshowpoint << num;
            std::string str = tmp.str();
            // Ensure it doesn't use scientific notation for deterministic output
            if (str.find('e') != std::string::npos || str.find('E') != std::string::npos) {
                // Re-format with explicit decimal
                out << std::fixed << std::setprecision(0) << num;
            } else {
                out << str;
            }
        }
    } else if (value.isString()) {
        out << '"';
        escapeJsonString(value.get<std::string>(), out);
        out << '"';
    } else if (value.isArray()) {
        jsonArrayToString(value.get<std::vector<JsonValue>>(), out);
    } else if (value.isObject()) {
        jsonObjectToString(value.get<std::map<std::string, JsonValue>>(), out);
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

// Percent-decode a string (RFC 3986 URI path decoding)
// Does NOT decode + to space - that's only for query strings
static std::string percentDecode(const std::string& s) {
    std::string result;
    for (std::size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            // Validate hex digits
            char h1 = s[i + 1];
            char h2 = s[i + 2];
            bool validHex = ((h1 >= '0' && h1 <= '9') || (h1 >= 'A' && h1 <= 'F') || (h1 >= 'a' && h1 <= 'f')) &&
                           ((h2 >= '0' && h2 <= '9') || (h2 >= 'A' && h2 <= 'F') || (h2 >= 'a' && h2 <= 'f'));
            if (validHex) {
                std::string hex = s.substr(i + 1, 2);
                char c = static_cast<char>(std::stoul(hex, nullptr, 16));
                result += c;
                i += 2;
            } else {
                // Invalid percent encoding - keep literal
                result += s[i];
            }
        } else {
            // In file URI paths, + is literal - only decode in query strings
            result += s[i];
        }
    }
    return result;
}

// Percent-encode only the characters that require encoding in a file URI path
// Per RFC 3986, file URI paths should preserve / and : (for drives)
static std::string percentEncodePath(const std::string& s) {
    std::string result;
    for (unsigned char c : s) {
        // In file URI path: don't encode A-Z a-z 0-9 / : - _ . ~
        // Encode everything else
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '/' || c == ':' ||
            c == '-' || c == '_' || c == '.' || c == '~') {
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

    // Percent-encode special characters (but not / or : for file URIs)
    pathStr = percentEncodePath(pathStr);

    // On Windows, add leading slash if not present (e.g., C:/... -> /C:/...)
    if (pathStr.size() >= 2 && pathStr[1] == ':') {
        pathStr = "/" + pathStr;
    }

    // Handle UNC paths (\\server\share) - encode as file:////server/share
    if (pathStr.size() >= 2 && pathStr[0] == '/' && pathStr[1] == '/') {
        // Already has leading slashes - keep them
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

    // Install the document FIRST (before diagnostics)
    openDocuments_[uri] = doc;

    // Invalidate parse caches for this document
    parsedPrograms_.erase(uri);
    diagnosticsCache_.erase(uri);

    // Use compile-based diagnostics if workspace is available
    if (workspaceRoot_) {
        auto result = diagnoseDocumentWithCompile(doc);
        // Store primary document diagnostics in the document
        auto it = result.diagnosticsByUri.find(uri);
        if (it != result.diagnosticsByUri.end()) {
            doc.diagnostics = std::move(it->second);
        }
        // Update the stored document with diagnostics
        openDocuments_[uri] = doc;
        // Publish diagnostics for all URIs
        for (auto& [diagUri, diags] : result.diagnosticsByUri) {
            publishDiagnostics(diagUri, diags);
        }
    } else {
        doc.diagnostics = diagnoseDocument(doc);
        // Update the stored document with diagnostics
        openDocuments_[uri] = doc;
        publishDiagnostics(uri, doc.diagnostics);
    }
}

void LanguageServer::updateDocument(const std::string& uri, const std::string& text, int version) {
    auto it = openDocuments_.find(uri);
    if (it != openDocuments_.end()) {
        // Update the document text first
        it->second.text = text;
        it->second.version = version;

        // Invalidate parse caches for this document
        parsedPrograms_.erase(uri);
        diagnosticsCache_.erase(uri);

        // Use compile-based diagnostics if workspace is available
        if (workspaceRoot_) {
            auto result = diagnoseDocumentWithCompile(it->second);
            // Store primary document diagnostics in the document
            auto resultIt = result.diagnosticsByUri.find(uri);
            if (resultIt != result.diagnosticsByUri.end()) {
                it->second.diagnostics = std::move(resultIt->second);
            }
            // Publish diagnostics for all URIs
            for (auto& [diagUri, diags] : result.diagnosticsByUri) {
                publishDiagnostics(diagUri, diags);
            }
        } else {
            it->second.diagnostics = diagnoseDocument(it->second);
            publishDiagnostics(uri, it->second.diagnostics);
        }
    }
}

void LanguageServer::saveDocument(const std::string& uri) {
    // Invalidate cached parsed program for this document (re-diagnose)
    parsedPrograms_.erase(uri);

    auto it = openDocuments_.find(uri);
    if (it != openDocuments_.end()) {
        it->second.diagnostics = diagnoseDocument(it->second);
        publishDiagnostics(uri, it->second.diagnostics);
    }
}

void LanguageServer::closeDocument(const std::string& uri) {
    // Invalidate cached parsed program for this document
    parsedPrograms_.erase(uri);
    openDocuments_.erase(uri);
}

// Helper: count UTF-16 code units for a Unicode code point
static std::uint32_t countUtf16UnitsForCodePoint(char32_t cp) {
    if (cp < 0x10000) return 1;  // BMP fits in one UTF-16 unit
    if (cp <= 0x10FFFF) return 2;  // Supplementary plane needs surrogate pair
    return 1;  // Invalid, but return something reasonable
}

// Count total UTF-16 code units in a UTF-8 string
static std::size_t countUtf16Units(const std::string& utf8Str) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while (pos < utf8Str.size()) {
        char32_t cp = 0;
        if (decodeUtf8CodePoint(utf8Str, pos, cp)) {
            count += countUtf16UnitsForCodePoint(cp);
        } else {
            // Invalid UTF-8, count as one unit
            count += 1;
            pos += 1;
        }
    }
    return count;
}

// Convert UTF-8 byte index to UTF-16 position (line, column)
// This properly handles UTF-8 sequences as atomic units and counts UTF-16
// code units per Unicode scalar value.
Position LanguageServer::utf8ToUtf16(const std::string& text, std::size_t utf8Offset) const {
    Position pos;
    std::size_t utf8Pos = 0;
    std::uint32_t utf16Col = 0;

    // Clamp offset to text length
    utf8Offset = std::min(utf8Offset, text.size());

    while (utf8Pos < utf8Offset) {
        // Handle CRLF: check for \r\n sequence
        if (utf8Pos + 1 < text.size() &&
            text[utf8Pos] == '\r' && text[utf8Pos + 1] == '\n') {
            // Check if CRLF fits within the requested offset
            if (utf8Pos + 2 <= utf8Offset) {
                pos.line++;
                utf16Col = 0;
                utf8Pos += 2;
            } else {
                // Partial CRLF - stop at offset without counting
                utf8Pos = utf8Offset;
            }
            continue;
        }

        unsigned char byte = static_cast<unsigned char>(text[utf8Pos]);

        if (byte == '\n') {
            pos.line++;
            utf16Col = 0;
            utf8Pos++;
        } else if (byte == '\r') {
            // CR not followed by LF - treat as line ending
            pos.line++;
            utf16Col = 0;
            utf8Pos++;
        } else {
            // Try to decode a UTF-8 code point directly
            // This is a validated decoder that handles all edge cases
            char32_t codePoint = 0;
            std::size_t oldPos = utf8Pos;
            if (decodeUtf8CodePoint(text, utf8Pos, codePoint)) {
                // Successfully decoded - count UTF-16 units for this scalar value
                utf16Col += countUtf16UnitsForCodePoint(codePoint);
            } else {
                // Invalid UTF-8 start byte or incomplete sequence - treat as single byte
                utf8Pos = oldPos + 1;
                utf16Col += 1;
            }
        }
    }

    pos.character = utf16Col;
    return pos;
}

// Compute the LSP position at the end of a UTF-8 document
// This properly handles LF, CRLF, and lone CR line endings
static Position computeEndPosition(const std::string& text) {
    if (text.empty()) {
        return {0, 0};
    }

    // Use utf8ToUtf16 with the full text length to get end position
    // This properly handles all line ending cases
    Position pos;
    std::size_t utf8Pos = 0;
    std::uint32_t utf16Col = 0;

    while (utf8Pos < text.size()) {
        // Handle CRLF: check for \r\n sequence
        if (utf8Pos + 1 < text.size() &&
            text[utf8Pos] == '\r' && text[utf8Pos + 1] == '\n') {
            pos.line++;
            utf16Col = 0;
            utf8Pos += 2;
            continue;
        }

        unsigned char byte = static_cast<unsigned char>(text[utf8Pos]);

        if (byte == '\n') {
            pos.line++;
            utf16Col = 0;
            utf8Pos++;
        } else if (byte == '\r') {
            // CR not followed by LF - treat as line ending
            pos.line++;
            utf16Col = 0;
            utf8Pos++;
        } else {
            // Try to decode a UTF-8 code point directly
            // This is a validated decoder that handles all edge cases
            char32_t codePoint = 0;
            std::size_t oldPos = utf8Pos;
            if (decodeUtf8CodePoint(text, utf8Pos, codePoint)) {
                // Successfully decoded - count UTF-16 units for this scalar value
                utf16Col += countUtf16UnitsForCodePoint(codePoint);
            } else {
                // Invalid UTF-8 start byte or incomplete sequence - treat as single byte
                utf8Pos = oldPos + 1;
                utf16Col += 1;
            }
        }
    }

    pos.character = utf16Col;
    return pos;
}

// Helper: encode a Unicode code point as UTF-8, returning bytes written
static std::size_t encodeUtf8CodePoint(char32_t cp, char* out) {
    if (cp < 0x80) {
        out[0] = static_cast<char>(cp);
        return 1;
    } else if (cp < 0x800) {
        out[0] = static_cast<char>(0xC0 | (cp >> 6));
        out[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (cp >> 12));
        out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    } else {
        out[0] = static_cast<char>(0xF0 | (cp >> 18));
        out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[3] = static_cast<char>(0x80 | (cp & 0x3F));
        return 4;
    }
}

// Convert UTF-16 position to UTF-8 byte offset
// Returns the byte offset. Valid UTF-16 positions are 0-based code unit indices.
// Positions in the middle of a surrogate pair (for supplementary characters) are INVALID
// and should return std::nullopt to indicate invalid position.
std::optional<std::size_t> LanguageServer::utf16ToUtf8(const std::string& text, std::uint32_t line, std::uint32_t utf16Col) const {
    std::uint32_t currentLine = 0;
    std::uint32_t currentCol = 0;
    std::size_t utf8Offset = 0;

    // First, count total lines to validate line number
    // Line endings: \r\n (windows), \n (unix), \r (old mac)
    // Count CRLF exactly once as a single line ending
    std::uint32_t totalLines = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            // CRLF - count as one line ending
            totalLines++;
            i += 2;
        } else if (text[i] == '\n') {
            // Unix line ending
            totalLines++;
            i++;
        } else if (text[i] == '\r') {
            // Old Mac line ending (\r not followed by \n)
            totalLines++;
            i++;
        } else {
            i++;
        }
    }
    // Account for last line without newline
    totalLines++;

    // Reject out-of-range line number
    if (line >= totalLines) {
        return std::nullopt;  // Invalid position
    }

    while (utf8Offset < text.size()) {
        // Handle CRLF: check for \r\n sequence
        if (utf8Offset + 1 < text.size() &&
            text[utf8Offset] == '\r' && text[utf8Offset + 1] == '\n') {
            if (currentLine == line) {
                // At end of target line after CRLF
                return utf8Offset;
            }
            currentLine++;
            currentCol = 0;
            utf8Offset += 2;
            continue;
        }

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
        } else if (byte == '\r') {
            // CR not followed by LF - treat as line ending
            if (currentLine == line) {
                return utf8Offset;
            }
            currentLine++;
            currentCol = 0;
            utf8Offset++;
            continue;
        }

        if (currentLine == line) {
            // Decode the UTF-8 code point to get its UTF-16 length
            char32_t codePoint = 0;
            std::size_t oldPos = utf8Offset;
            if (decodeUtf8CodePoint(text, utf8Offset, codePoint)) {
                std::uint32_t units = countUtf16UnitsForCodePoint(codePoint);

                // Check for invalid surrogate pair positions:
                // For characters needing 2 UTF-16 units (surrogate pairs), valid positions are:
                // - 0 to units-1: within the character (returns start of character)
                // - units: at the end of character (returns byte offset after character)
                // Positions in the middle (e.g., position 1 of a 2-unit character) are INVALID
                if (units == 2) {
                    // For surrogate pairs, check if position falls in the middle
                    if (currentCol < utf16Col && utf16Col < currentCol + units) {
                        // Position is in the middle of a surrogate pair - INVALID
                        return std::nullopt;
                    }
                }

                // Now handle the position
                if (currentCol + units <= utf16Col) {
                    // Position is after this character
                    if (currentCol + units == utf16Col) {
                        // Position is exactly at end of this character
                        return utf8Offset;
                    }
                    // Position is past this character
                    currentCol += units;
                } else {
                    // Position falls within this character (including start at currentCol)
                    return oldPos;
                }
            } else {
                // Invalid UTF-8, skip one byte
                utf8Offset = oldPos + 1;
                if (currentCol >= utf16Col) {
                    return oldPos;
                }
                currentCol += 1;
            }
        } else {
            // Skip this character entirely (not on target line)
            char32_t codePoint = 0;
            std::size_t oldPos = utf8Offset;
            if (!decodeUtf8CodePoint(text, utf8Offset, codePoint)) {
                utf8Offset = oldPos + 1;
            }
        }
    }

    // At end of text
    if (currentLine == line && currentCol <= utf16Col) {
        return utf8Offset;
    }

    return std::nullopt;
}

// Convert grapheme column to UTF-16 column in a line
std::uint32_t graphemeColumnToUtf16Column(const std::string& line, std::size_t graphemeIndex) {
    std::uint32_t utf16Col = 0;
    std::size_t graphemeCount = 0;
    std::size_t pos = 0;

    while (pos < line.size() && graphemeCount < graphemeIndex) {
        // Handle line endings
        if (line[pos] == '\n' || (line[pos] == '\r' && (pos + 1 >= line.size() || line[pos + 1] != '\n'))) {
            break;
        }

        // Decode UTF-8 code point
        char32_t codePoint = 0;
        std::size_t oldPos = pos;
        if (decodeUtf8CodePoint(line, pos, codePoint)) {
            utf16Col += countUtf16UnitsForCodePoint(codePoint);
            graphemeCount++;
        } else {
            // Invalid UTF-8, treat as single byte
            pos = oldPos + 1;
            utf16Col += 1;
            graphemeCount++;
        }
    }

    return utf16Col;
}

// Convert grapheme cluster index to UTF-16 column position
// This counts UTF-16 code units up to the specified grapheme index
std::uint32_t graphemeToUtf16Column(const std::string& text, std::size_t lineStart, std::size_t graphemeIndex) {
    std::uint32_t utf16Col = 0;
    std::size_t graphemeCount = 0;
    std::size_t pos = lineStart;

    while (pos < text.size() && graphemeCount < graphemeIndex) {
        // Handle line endings
        if (pos < text.size() - 1 && text[pos] == '\r' && text[pos + 1] == '\n') {
            // CRLF - end of line
            break;
        }
        if (text[pos] == '\n') {
            break;
        }
        if (text[pos] == '\r') {
            break;
        }

        // Decode UTF-8 code point
        char32_t codePoint = 0;
        std::size_t oldPos = pos;
        if (decodeUtf8CodePoint(text, pos, codePoint)) {
            utf16Col += countUtf16UnitsForCodePoint(codePoint);
            graphemeCount++;
        } else {
            // Invalid UTF-8, treat as single byte
            pos = oldPos + 1;
            utf16Col += 1;
            graphemeCount++;
        }
    }

    return utf16Col;
}

// Extract a specific line from text (0-indexed line number)
std::string getLine(const std::string& text, std::uint32_t lineNum) {
    if (text.empty() && lineNum == 0) {
        return "";
    }

    std::uint32_t currentLine = 0;
    std::size_t lineStart = 0;
    std::size_t i = 0;

    while (i < text.size()) {
        if (currentLine == lineNum) {
            // Found the line, find its end
            std::size_t lineEnd = i;
            while (lineEnd < text.size() && text[lineEnd] != '\n' && text[lineEnd] != '\r') {
                lineEnd++;
            }
            return text.substr(i, lineEnd - i);
        }

        // Handle line endings
        if (i < text.size() - 1 && text[i] == '\r' && text[i + 1] == '\n') {
            currentLine++;
            lineStart = i + 2;
            i += 2; // Skip the \r\n
        } else if (text[i] == '\n') {
            currentLine++;
            lineStart = i + 1;
            i++;
        } else if (text[i] == '\r') {
            currentLine++;
            lineStart = i + 1;
            i++;
        } else {
            i++;
        }
    }

    // If we reach here, check if lineNum is the last line
    // A text ending with newline has an additional empty line after it
    if (currentLine == lineNum) {
        return text.substr(lineStart);
    }
    // Also handle case where text ends with newline and we're asking for the line after
    if (currentLine + 1 == lineNum) {
        return "";
    }

    return "";
}

// Convert UTF-16 column to grapheme column in a line
// Returns std::nullopt if position falls within a surrogate pair (invalid)
std::optional<std::size_t> utf16ColumnToGraphemeColumn(const std::string& line, std::uint32_t utf16Col) {
    std::uint32_t currentUtf16 = 0;
    std::size_t graphemeCount = 0;
    std::size_t pos = 0;

    while (pos < line.size()) {
        // Handle line endings
        if (line[pos] == '\n' || (line[pos] == '\r' && (pos + 1 >= line.size() || line[pos + 1] != '\n'))) {
            break;
        }

        char32_t codePoint = 0;
        std::size_t oldPos = pos;
        if (!decodeUtf8CodePoint(line, pos, codePoint)) {
            // Invalid UTF-8, treat as single byte
            pos = oldPos + 1;
            if (currentUtf16 == utf16Col) {
                return graphemeCount;
            }
            currentUtf16++;
            graphemeCount++;
            continue;
        }

        std::uint32_t units = countUtf16UnitsForCodePoint(codePoint);

        // Check for invalid surrogate pair positions
        if (units == 2) {
            if (currentUtf16 == utf16Col) {
                return graphemeCount; // Valid: on first unit
            }
            if (currentUtf16 + 1 == utf16Col) {
                return std::nullopt; // Invalid: on second unit (low surrogate)
            }
            if (currentUtf16 < utf16Col && currentUtf16 + units > utf16Col) {
                return std::nullopt; // Invalid: in middle of surrogate pair
            }
        } else {
            if (currentUtf16 == utf16Col) {
                return graphemeCount;
            }
        }

        currentUtf16 += units;
        graphemeCount++;
    }

    // At end of line
    if (currentUtf16 <= utf16Col) {
        return graphemeCount;
    }

    return std::nullopt;
}

// Find the byte offset in a line for a given UTF-16 column
// Returns std::nullopt if position falls within a surrogate pair (invalid)
std::optional<std::size_t> utf16ColumnToLineOffset(const std::string& line, std::uint32_t utf16Col) {
    std::uint32_t currentUtf16 = 0;
    std::size_t pos = 0;
    bool inSurrogatePair = false;

    while (pos < line.size()) {
        // Handle line endings within the line check
        if (line[pos] == '\n' || (line[pos] == '\r' && (pos + 1 >= line.size() || line[pos + 1] != '\n'))) {
            // End of line
            if (currentUtf16 <= utf16Col) {
                return pos; // Return end of line
            }
            break;
        }

        char32_t codePoint = 0;
        std::size_t oldPos = pos;
        if (!decodeUtf8CodePoint(line, pos, codePoint)) {
            // Invalid UTF-8, skip one byte
            pos = oldPos + 1;
            if (currentUtf16 == utf16Col) {
                return pos;
            }
            currentUtf16++;
            continue;
        }

        std::uint32_t units = countUtf16UnitsForCodePoint(codePoint);

        // Check if we're trying to land in the middle of a surrogate pair
        if (units == 2) {
            if (currentUtf16 == utf16Col) {
                // Valid: landing on the first unit of surrogate pair
                return oldPos;
            } else if (currentUtf16 + 1 == utf16Col) {
                // Invalid: landing on the second unit (low surrogate)
                return std::nullopt;
            } else if (currentUtf16 < utf16Col && currentUtf16 + units > utf16Col) {
                // Would land in middle - invalid
                return std::nullopt;
            }
        } else {
            if (currentUtf16 == utf16Col) {
                return oldPos;
            } else if (currentUtf16 < utf16Col && currentUtf16 + units > utf16Col) {
                // Landing within this character
                return oldPos;
            }
        }

        currentUtf16 += units;
    }

    // At end of line
    if (currentUtf16 <= utf16Col) {
        return pos;
    }

    return std::nullopt;
}

// Find the byte offset for a UTF-16 position in full text
// Returns std::nullopt if position is invalid (inside surrogate pair)
std::optional<std::size_t> utf16PositionToUtf8Offset(const std::string& text, std::uint32_t line, std::uint32_t utf16Col) {
    // First, find the start of the target line
    std::uint32_t currentLine = 0;
    std::size_t lineStart = 0;

    for (std::size_t i = 0; i < text.size(); i++) {
        if (currentLine == line) {
            break;
        }
        if (i < text.size() - 1 && text[i] == '\r' && text[i + 1] == '\n') {
            currentLine++;
            lineStart = i + 2;
            i++; // Skip the \n
        } else if (text[i] == '\n') {
            currentLine++;
            lineStart = i + 1;
        } else if (text[i] == '\r') {
            currentLine++;
            lineStart = i + 1;
        }
    }

    if (currentLine != line) {
        // Line not found
        return std::nullopt;
    }

    // Now find the byte offset within that line
    // Find the end of the line
    std::size_t lineEnd = lineStart;
    while (lineEnd < text.size() && text[lineEnd] != '\n' && text[lineEnd] != '\r') {
        lineEnd++;
    }

    // Extract just this line
    std::string lineStr = text.substr(lineStart, lineEnd - lineStart);

    auto offset = utf16ColumnToLineOffset(lineStr, utf16Col);
    if (!offset) {
        return std::nullopt;
    }

    return lineStart + *offset;
}

// Get source provider for overlay support
SourceProvider LanguageServer::getSourceProvider() {
    return [this](const std::filesystem::path& path) -> std::optional<std::string> {
        // First check if this file has an open overlay
        std::string uri = pathToUri(path);
        auto doc = getDocument(uri);
        if (doc) {
            // Return the overlay text even if empty - this indicates an overlay exists
            // and prevents falling back to disk
            return doc->text;
        }
        return std::nullopt;
    };
}

// Get or parse a document's AST program
std::optional<std::reference_wrapper<ast::Program>> LanguageServer::getOrParseProgram(const std::string& uri) {
    auto doc = getDocument(uri);
    if (!doc) return std::nullopt;

    // Check cache first
    auto it = parsedPrograms_.find(uri);
    if (it != parsedPrograms_.end()) {
        return std::ref(*it->second);
    }

    // Parse the document
    try {
        CustomEmojiRegistry reg = registry_;
        Lexer lexer(doc->text, reg);
        auto tokens = lexer.tokenize();

        // Cache the tokens for accurate position information
        auto tokenResult = parsedTokens_.emplace(uri, std::move(tokens));
        const std::vector<Token>& cachedTokens = tokenResult.first->second;

        // Create a copy of tokens for the parser (parser takes ownership)
        std::vector<Token> parserTokens = cachedTokens;
        Parser parser(std::move(parserTokens));
        auto program = parser.parse();

        // Cache the parsed program - use unique_ptr to avoid copying unique_ptr members
        auto result = parsedPrograms_.emplace(uri, std::make_unique<ast::Program>(std::move(program)));
        return std::ref(*result.first->second);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::reference_wrapper<const std::vector<Token>>> LanguageServer::getTokens(const std::string& uri) {
    // First ensure we have tokens (by parsing the program which caches tokens)
    auto programOpt = getOrParseProgram(uri);
    if (!programOpt) return std::nullopt;

    // Now check the tokens cache
    auto it = parsedTokens_.find(uri);
    if (it != parsedTokens_.end()) {
        return std::ref(it->second);
    }
    return std::nullopt;
}

std::optional<std::uint32_t> LanguageServer::findIdentifierColumn(const ast::Stmt& stmt, const std::string& identifierName, const std::vector<Token>& tokens) {
    // Find the token that matches the identifier name on the same line as the statement
    std::size_t stmtLine = stmt.line;

    for (const auto& token : tokens) {
        if (token.line == stmtLine && token.kind == TokenKind::Identifier) {
            if (token.canonical == identifierName || token.lexeme == identifierName) {
                // Convert 1-based column to 0-based, using safe cast to avoid narrowing
                return safeSizeToUint32(token.column > 0 ? token.column - 1 : 0);
            }
        }
    }
    return std::nullopt;
}

// Helper to find a token at a specific line and use its bounds for diagnostic range
// Returns the token's start column and computes end column based on lexeme length
static std::pair<std::size_t, std::size_t> findTokenBoundsForLine(
    const std::vector<Token>& tokens, std::size_t targetLine) {
    std::size_t bestColumn = 0;
    std::size_t bestEndColumn = 0;
    std::size_t minColumn = std::numeric_limits<std::size_t>::max();

    // First pass: find the leftmost token on this line
    for (const auto& token : tokens) {
        if (token.line == targetLine) {
            if (token.column < minColumn) {
                minColumn = token.column;
                bestColumn = token.column;
                // Compute end column based on grapheme count (matching lexer column semantics)
                // Token::column is grapheme-based, so endColumn should also be grapheme-based
                // LSP ranges use [start, end) where end is exclusive, so we use token.column + gs.size()
                // to get the position after the last grapheme (no -1)
                auto gs = segment_graphemes(token.lexeme);
                bestEndColumn = token.column + gs.size();  // Exclusive end for LSP range
            }
        }
    }

    return {bestColumn, bestEndColumn};
}

// Helper to find the import token at a specific line for import ownership diagnostics
static std::pair<std::size_t, std::size_t> findImportTokenBoundsForLine(
    const std::vector<Token>& tokens, std::size_t targetLine) {
            // Look for the 🔗 token (Import) at the target line
    for (const auto& token : tokens) {
        if (token.line == targetLine && token.kind == TokenKind::Import) {
            // Found the import token, compute end column using grapheme count
            // (matching lexer column semantics)
            // LSP ranges use [start, end) where end is exclusive
            auto gs = segment_graphemes(token.lexeme);
            std::size_t endColumn = token.column + gs.size();  // Exclusive end for LSP range
            return {token.column, endColumn};
        }
    }
    // Fall back to finding any token at this line
    return findTokenBoundsForLine(tokens, targetLine);
}

// Helper to find the AST statement at a specific line
static const ast::Stmt* findStmtAtLine(const ast::Program& program, std::size_t targetLine) {
    for (const auto& stmt : program.statements) {
        if (stmt->line == targetLine) {
            return stmt.get();
        }
    }
    return nullptr;
}

Range LanguageServer::tokenToRange(const std::string& sourceText, const Token& token) const {
    Range range;

    // Handle EOF token - return empty range at document end
    if (token.kind == TokenKind::Eof) {
        // Return position at end of last line
        range.start.line = static_cast<std::uint32_t>(token.line - 1);
        range.start.character = 0;
        range.end = range.start;
        return range;
    }

    // Step 1: Find the start of the token's line in the original source
    // Handle LF, CRLF, and lone CR line endings
    std::size_t lineStart = 0;
    std::size_t currentLine = 1;

    for (std::size_t i = 0; i < sourceText.size(); ++i) {
        if (currentLine == token.line) {
            lineStart = i;
            break;
        }

        // Handle line endings
        if (sourceText[i] == '\r') {
            if (i + 1 < sourceText.size() && sourceText[i + 1] == '\n') {
                // CRLF - skip both characters
                ++currentLine;
                ++i; // Extra increment to skip \n
            } else {
                // Lone CR
                ++currentLine;
            }
        } else if (sourceText[i] == '\n') {
            ++currentLine;
        }
    }

    // If we didn't find the line, token is past end of text
    if (currentLine < token.line) {
        // Token line is beyond text - return position at last line end
        // Count the actual number of lines in the text
        std::size_t lineCount = 1;  // At minimum there's line 1
        for (std::size_t i = 0; i < sourceText.size(); ++i) {
            if (sourceText[i] == '\n') {
                ++lineCount;
            }
        }
        range.start.line = static_cast<std::uint32_t>(lineCount - 1);
        range.start.character = 0;
        range.end = range.start;
        return range;
    }

    // Step 2: Find the end of the token's line
    std::size_t lineEnd = lineStart;
    while (lineEnd < sourceText.size()) {
        if (sourceText[lineEnd] == '\n' || sourceText[lineEnd] == '\r') {
            break;
        }
        ++lineEnd;
    }

    // Extract the line text
    std::string lineText = sourceText.substr(lineStart, lineEnd - lineStart);

    // Step 3: Segment the line into graphemes and count UTF-16 units before token.column
    // token.column is 1-based grapheme column
    auto graphemes = segment_graphemes(lineText);

    std::uint32_t utf16Column = 0;
    std::size_t graphemeIndex = 0;

    for (const auto& g : graphemes) {
        if (graphemeIndex >= token.column - 1) {
            // Reached or passed the token's column
            break;
        }
        // Count UTF-16 units for this grapheme
        utf16Column += static_cast<std::uint32_t>(countUtf16Units(g.display));
        ++graphemeIndex;
    }

    range.start.line = static_cast<std::uint32_t>(token.line - 1);
    range.start.character = utf16Column;

    // Step 4: Compute end position by traversing the token's lexeme
    // We need to find where the token ends in the original source
    // Approach: start from the position we calculated and find the token's lexeme in the source

    // For multiline tokens (like strings), we need special handling
    if (token.lexeme.find('\n') != std::string::npos ||
        token.lexeme.find('\r') != std::string::npos) {
        // Multiline token - calculate end position by traversing the lexeme
        // Count lines and UTF-16 columns in the lexeme
        std::uint32_t endLine = range.start.line;
        std::uint32_t endColumn = range.start.character;

        auto lexemeGraphemes = segment_graphemes(token.lexeme);
        for (const auto& g : lexemeGraphemes) {
            if (g.display == "\n") {
                ++endLine;
                endColumn = 0;
            } else if (g.display == "\r") {
                // Lone CR - count as one line ending
                ++endLine;
                endColumn = 0;
            } else {
                endColumn += static_cast<std::uint32_t>(countUtf16Units(g.display));
            }
        }

        range.end.line = endLine;
        range.end.character = endColumn;
    } else {
        // Single-line token - find end column by adding UTF-16 units of lexeme
        range.end.character = range.start.character + static_cast<std::uint32_t>(countUtf16Units(token.lexeme));
        range.end.line = range.start.line;
    }

    return range;
}

std::vector<Diagnostic> LanguageServer::diagnoseDocument(const OpenDocument& doc) {
    std::vector<Diagnostic> diagnostics;

    if (doc.text.empty()) return diagnostics;

    try {
        // First check lexer-level issues
        Lexer lexer(doc.text, registry_);
        auto tokens = lexer.tokenize();

        // Check for lexer errors (unexpected characters, etc.)
        for (const auto& token : tokens) {
            if (token.kind == TokenKind::Eof) continue;

            // Check for unknown tokens or issues - token.lexeme being empty is sometimes an indicator
            // For now, we just validate tokenization works
        }

        // Parse the document
        Parser parser(std::move(tokens));
        auto program = parser.parse();

        // Compile to check semantic errors
        Compiler compiler;
        compiler.compile(program);

    } catch (const lsp::SourceLocationException& sle) {
        // Use typed source location for exact UTF-16 range
        Diagnostic diag;
        diag.severity = 1;  // Error
        diag.message = sle.message;
        diag.source = "emojineer";

        // Create a synthetic token from the exception info for canonical range conversion
        Token errorToken;
        errorToken.kind = TokenKind::Identifier;
        errorToken.line = sle.line;
        errorToken.column = sle.column;
        errorToken.lexeme = sle.tokenLexeme;

        // Use canonical helper to convert to UTF-16 range
        diag.range = tokenToRange(doc.text, errorToken);

        diagnostics.push_back(diag);
    } catch (const std::exception& e) {
        // Generic non-source exception - emit safe document-level diagnostic
        // without inventing coordinates from the message
        Diagnostic diag;
        diag.range = {{0, 0}, {0, 1}};  // Safe default: start of document
        diag.severity = 1;  // Error
        diag.message = e.what();
        diag.source = "emojineer";
        diagnostics.push_back(diag);
    }

    return diagnostics;
}

::emojineer::SourceProvider LanguageServer::createSourceProvider() const {
    // Create a source provider that checks open documents first before reading from disk
    return [this](const std::filesystem::path& path) -> std::optional<std::string> {
        // Convert path to URI and check if document is open
        std::string uri = pathToUri(path);
        auto it = openDocuments_.find(uri);
        if (it != openDocuments_.end()) {
            return it->second.text;
        }
        // Not in overlay, return nullopt to fall back to disk
        return std::nullopt;
    };
}

DiagnosticResult LanguageServer::diagnoseDocumentWithCompile(const OpenDocument& doc) {
    DiagnosticResult result;
    result.primaryUri = doc.uri;

    if (doc.text.empty()) return result;

    // If we have a workspace root, use compile_file with the source provider
    // to get proper module/package graph diagnostics
    if (workspaceRoot_) {
        try {
            auto sourceProvider = createSourceProvider();

            // Try to compile the document using the sovereign module system
            // This will catch module/import/package errors
            Chunk chunk = compile_file(doc.path, registry_, *workspaceRoot_, sourceProvider);

            // If we get here, compilation succeeded without errors
            // The chunk contains the compiled bytecode

        } catch (const lsp::SourceLocationException& sle) {
            // Use typed source location for exact UTF-16 range
            Diagnostic diag;
            diag.severity = 1;  // Error
            diag.message = sle.message;
            diag.source = "emojineer";

            // Determine the source text for the diagnostic
            // If sourcePath is set, the error is from an imported module
            std::string sourceText = doc.text;
            std::string targetUri = doc.uri;  // Default to entry document URI

            if (!sle.sourcePath.empty()) {
                // Error is from an imported module - get source from that path
                targetUri = pathToUri(sle.sourcePath);

                // Try to get the source from open documents first, then fall back to disk
                auto it = openDocuments_.find(targetUri);
                if (it != openDocuments_.end()) {
                    sourceText = it->second.text;
                } else if (std::filesystem::exists(sle.sourcePath)) {
                    // Fall back to reading from disk
                    try {
                        sourceText = readFile(sle.sourcePath);
                    } catch (...) {
                        // If we can't read the file, fall back to entry document
                        sourceText = doc.text;
                        targetUri = doc.uri;
                    }
                }
            }

            // Create a synthetic token from the exception info for canonical range conversion
            Token errorToken;
            errorToken.kind = TokenKind::Identifier;
            errorToken.line = sle.line;
            errorToken.column = sle.column;
            errorToken.lexeme = sle.tokenLexeme;

            // Use canonical helper to convert to UTF-16 range against the correct source
            diag.range = tokenToRange(sourceText, errorToken);

            // Add to the correct URI group
            result.diagnosticsByUri[targetUri].push_back(diag);

            // If this is an imported source error, also add empty diagnostics to entry
            // to trigger the notification (the actual error is under imported URI)
            if (targetUri != doc.uri) {
                // Primary URI gets empty diagnostics (error is under imported URI)
                result.diagnosticsByUri[doc.uri];
            } else {
                result.primaryUri = doc.uri;
            }
        } catch (const std::exception& e) {
            // Generic non-source exception - emit safe document-level diagnostic
            Diagnostic diag;
            diag.range = {{0, 0}, {0, 1}};  // Safe default: start of document
            diag.severity = 1;  // Error
            diag.message = e.what();
            diag.source = "emojineer";
            result.diagnosticsByUri[doc.uri].push_back(diag);
        }
    } else {
        // No workspace root - fall back to simple diagnostics
        auto diags = diagnoseDocument(doc);
        result.diagnosticsByUri[doc.uri] = std::move(diags);
    }

    return result;
}

void LanguageServer::publishDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics) {
    auto params = json::makeObject();
    json::objectSet(params, "uri", JsonValue(uri));

    // Get the document for position conversion
    auto doc = getDocument(uri);
    std::string docText;
    if (doc) {
        docText = doc->text;
    }

    auto diagJson = json::makeArray();
    for (const auto& d : diagnostics) {
        auto diag = json::makeObject();

        // Convert positions from internal (grapheme) to LSP (UTF-16)
        // Internal positions are 0-indexed lines, grapheme columns
        // LSP expects 0-indexed lines, UTF-16 columns

        std::string startLineStr = getLine(docText, d.range.start.line);
        std::uint32_t utf16StartChar = graphemeColumnToUtf16Column(startLineStr, d.range.start.character);

        std::string endLineStr = getLine(docText, d.range.end.line);
        std::uint32_t utf16EndChar = graphemeColumnToUtf16Column(endLineStr, d.range.end.character);

        // Diagnostic assertion: validate UTF-16 positions for supplementary-plane emojis
        // Check if the range spans a supplementary-plane emoji/CER token (code point >= U+10000)
        if (!startLineStr.empty() && d.range.start.line == d.range.end.line) {
            std::size_t graphemeIdx = 0;
            std::size_t pos = 0;
            bool foundSupplementaryInRange = false;
            std::uint32_t computedUtf16Start = 0;
            std::uint32_t computedUtf16End = 0;

            // Use < instead of <= to match graphemeColumnToUtf16Column semantics
            while (pos < startLineStr.size() && graphemeIdx < d.range.end.character) {
                char32_t codePoint = 0;
                std::size_t oldPos = pos;
                if (decodeUtf8CodePoint(startLineStr, pos, codePoint)) {
                    // Check if this grapheme is in the range [start, end)
                    if (graphemeIdx >= d.range.start.character && graphemeIdx < d.range.end.character) {
                        if (codePoint >= 0x10000) {
                            foundSupplementaryInRange = true;
                        }
                    }
                    if (graphemeIdx < d.range.start.character) {
                        computedUtf16Start += countUtf16UnitsForCodePoint(codePoint);
                    }
                    computedUtf16End += countUtf16UnitsForCodePoint(codePoint);
                    graphemeIdx++;
                } else {
                    pos = oldPos + 1;
                    graphemeIdx++;
                }
            }

            // Assert: for supplementary-plane tokens, computed UTF-16 must match published UTF-16
            if (foundSupplementaryInRange) {
                // The assertion validates exact UTF-16 positions, not just range presence
                assert(utf16StartChar == computedUtf16Start &&
                       utf16EndChar == computedUtf16End &&
                       "UTF-16 position assertion failed for supplementary-plane emoji/CER token");
            }
        }

        auto range = json::makeObject();
        auto start = json::makeObject();
        json::objectSet(start, "line", JsonValue(static_cast<double>(d.range.start.line)));
        json::objectSet(start, "character", JsonValue(static_cast<double>(utf16StartChar)));
        auto end = json::makeObject();
        json::objectSet(end, "line", JsonValue(static_cast<double>(d.range.end.line)));
        json::objectSet(end, "character", JsonValue(static_cast<double>(utf16EndChar)));
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
        // Initialize PackageGraph using canonical resolve_package_graph
        // This provides the authoritative source universe for definitions/references
        packageGraph_ = resolve_package_graph(root, *manifest_, package_store_root(root), true);
    }
}

JsonValue LanguageServer::handleInitialize(const JsonValue& params) {
    initialized_ = true;

    const auto* paramsObj = params.getPtr<std::map<std::string, JsonValue>>();
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
    json::objectSet(caps, "documentRangeFormattingProvider", JsonValue(false));

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
        // For full-sync mode, we take the last content change's full text.
        const auto& lastChange = arr[arr.size() - 1];

        // Check for range field - in full-sync mode, reject any event carrying range
        auto rangeField = getJsonObject(lastChange, "range");
        if (!rangeField.isNull()) {
            // Ranged incremental change - not supported in full-sync mode
            // Reject it entirely as per LSP spec
            return;
        }

        // Get the text field - always apply including empty text for full-sync
        auto textField = getJsonObject(lastChange, "text");
        std::string text = getJsonString(textField);

        if (!uri.empty()) {
            // Always apply the text field, including empty text for full document replacement
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
    std::uint32_t utf16Char = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "character")));

    // Convert UTF-16 position to grapheme position for lexer
    std::string lineStr = getLine(doc->text, line);
    auto graphemeCol = utf16ColumnToGraphemeColumn(lineStr, utf16Char);
    if (!graphemeCol) return JsonValue(nullptr);

    // Convert to 1-indexed for lexer (line is 0-indexed in LSP, column is UTF-16 units)
    Position internalPos;
    internalPos.line = line;  // LSP uses 0-indexed lines
    internalPos.character = *graphemeCol;  // Fallback to UTF-16 if conversion fails

    auto hover = getHover(doc->uri, internalPos);
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

        // Convert position: pos.character is already a grapheme column (0-indexed)
        // Token columns are 1-indexed grapheme columns
        std::size_t targetLine = pos.line + 1;
        std::size_t targetGraphemeCol = pos.character + 1;  // Convert to 1-indexed for lexer

        for (const auto& token : tokens) {
            if (token.kind == TokenKind::Eof) continue;

            // Check if position is within this token's range
            if (token.line == targetLine) {
                // token.column is 1-indexed GRAPHEME column
                // The token spans from token.column to token.column + grapheme_count - 1
                // Count graphemes in the lexeme
                auto gs = segment_graphemes(token.lexeme);
                std::size_t tokenGraphemeCount = gs.size();

                // Check if target grapheme column is within token span
                if (targetGraphemeCol >= token.column && targetGraphemeCol < token.column + tokenGraphemeCount) {
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
        }
    } catch (...) {}

    return std::nullopt;
}

// Helper: find the identifier at a given position in the document
// Helper: check if a Unicode code point can start an Emojineer identifier
static bool isIdentifierStart(char32_t cp) {
    // Emojineer identifiers can start with underscore or any Unicode letter/mark
    if (cp == '_') return true;
    // Unicode categories that can start an identifier: Lu, Ll, Lt, Lm, Lo, Nl, Mn, Mc
    // Simplified: check for letter or mark
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
           (cp >= 0x00C0 && cp <= 0x00FF) ||  // Latin Extended
           (cp >= 0x0100 && cp <= 0x024F) ||  // Latin Extended-B
           (cp >= 0x0370 && cp <= 0x03FF) ||  // Greek
           (cp >= 0x0400 && cp <= 0x04FF) ||  // Cyrillic
           (cp >= 0x1E00 && cp <= 0x1EFF) ||  // Latin Extended Additional
           (cp >= 0x1F300 && cp <= 0x1F9FF);  // Emoji (Emoji Symbols and Pictographs)
}

// Helper: check if a Unicode code point can continue an Emojineer identifier
static bool isIdentifierPart(char32_t cp) {
    if (isIdentifierStart(cp)) return true;
    // Unicode categories for identifier parts: Nd, Pc, Mn, Mc
    if (cp == '_') return true;
    // Digits
    if (cp >= '0' && cp <= '9') return true;
    return false;
}

// Helper: decode a UTF-8 sequence and return the code point
static std::pair<char32_t, std::size_t> decodeUtf8(const std::string& str, std::size_t pos) {
    if (pos >= str.size()) return {0, 0};
    unsigned char byte = static_cast<unsigned char>(str[pos]);

    char32_t cp = 0;
    std::size_t len = 1;

    if ((byte & 0x80) == 0) {
        // ASCII
        cp = byte;
    } else if ((byte & 0xE0) == 0xC0) {
        // 2-byte sequence
        cp = byte & 0x1F;
        len = 2;
    } else if ((byte & 0xF0) == 0xE0) {
        // 3-byte sequence
        cp = byte & 0x0F;
        len = 3;
    } else if ((byte & 0xF8) == 0xF0) {
        // 4-byte sequence
        cp = byte & 0x07;
        len = 4;
    } else {
        return {0, 0}; // Invalid
    }

    // Read continuation bytes
    for (std::size_t i = 1; i < len && pos + i < str.size(); i++) {
        unsigned char cont = static_cast<unsigned char>(str[pos + i]);
        if ((cont & 0xC0) != 0x80) return {0, 0}; // Invalid continuation
        cp = (cp << 6) | (cont & 0x3F);
    }

    return {cp, len};
}

static std::optional<std::string> findIdentifierAtPosition(const std::string& text, const Position& pos) {
    // pos.character is a grapheme column (0-indexed)
    // Convert to 1-indexed for easier comparison with grapheme segments
    std::size_t targetGraphemeCol = pos.character + 1;

    // Get the line at the position
    std::size_t lineStart = 0;
    std::size_t currentLine = 0;
    for (std::size_t i = 0; i < text.size(); i++) {
        if (currentLine == pos.line) {
            lineStart = i;
            break;
        }
        if (text[i] == '\n') currentLine++;
    }

    // Find the end of this line
    std::size_t lineEnd = lineStart;
    while (lineEnd < text.size() && text[lineEnd] != '\n' && text[lineEnd] != '\r') {
        lineEnd++;
    }

    // Extract the line
    std::string line = text.substr(lineStart, lineEnd - lineStart);

    // Segment the line into graphemes
    auto graphemes = segment_graphemes(line);

    // Check if position is within the line
    if (targetGraphemeCol == 0 || targetGraphemeCol > graphemes.size()) {
        return std::nullopt;
    }

    // Find the byte offset for the target grapheme column
    std::size_t byteOffset = 0;
    for (std::size_t g = 0; g < targetGraphemeCol - 1 && g < graphemes.size(); g++) {
        byteOffset += graphemes[g].display.size();
    }

    // Now find the identifier at this byte offset
    if (byteOffset >= line.size()) return std::nullopt;

    // Check if we're at a valid identifier start
    // Get the code point at byteOffset
    auto [startCp, startLen] = decodeUtf8(line, byteOffset);

    if (!isIdentifierStart(startCp)) {
        // Maybe we're in the middle of an identifier, try to go back
        std::size_t backPos = byteOffset;
        while (backPos > 0) {
            // Move back to find the start of current code point
            std::size_t prevPos = backPos;
            while (prevPos > 0 && (static_cast<unsigned char>(line[prevPos - 1]) & 0xC0) == 0x80) {
                prevPos--;
            }
            // If we didn't move back, break to avoid infinite loop
            if (prevPos >= backPos) break;
            auto [prevCp, _] = decodeUtf8(line, prevPos);
            if (!isIdentifierPart(prevCp)) break;
            backPos = prevPos;
        }
        byteOffset = backPos;
    }

    // Extract the identifier - collect consecutive identifier graphemes
    std::string identifier;
    std::size_t curPos = byteOffset;
    while (curPos < line.size()) {
        auto [cp, len] = decodeUtf8(line, curPos);
        if (cp == 0 || !isIdentifierPart(cp)) break;

        // Add the display form (grapheme) to identifier
        identifier += line.substr(curPos, len);
        curPos += len;
    }

    if (!identifier.empty()) {
        return identifier;
    }
    return std::nullopt;
}

static bool pathHasPrefix(const std::filesystem::path& candidate,
                          const std::filesystem::path& root) {
    auto c = candidate.lexically_normal();
    auto r = root.lexically_normal();
    auto ci = c.begin();
    for (auto ri = r.begin(); ri != r.end(); ++ri, ++ci) {
        if (ci == c.end() || *ci != *ri) return false;
    }
    return true;
}

static const ResolvedPackage* packageOwningPath(const PackageGraph& graph,
                                                const std::filesystem::path& candidate) {
    const ResolvedPackage* owner = nullptr;
    std::size_t bestLength = 0;
    for (const auto& pkg : graph.packages) {
        if (pkg.root.empty()) continue;
        auto normalizedRoot = pkg.root.lexically_normal();
        if (pathHasPrefix(candidate, normalizedRoot)) {
            auto length = normalizedRoot.native().size();
            if (!owner || length > bestLength) {
                owner = &pkg;
                bestLength = length;
            }
        }
    }
    return owner;
}

static std::set<std::filesystem::path> visibleSourceRoots(
    const std::filesystem::path& workspaceRoot,
    const std::optional<ProjectManifest>& manifest,
    const PackageGraph& graph,
    const std::filesystem::path& requesterPath) {

    std::set<std::filesystem::path> roots;
    if (const auto* owner = packageOwningPath(graph, requesterPath)) {
        roots.insert(owner->root.lexically_normal());
        for (const auto& dependencyName : owner->dependencies) {
            if (const auto* dependency = graph.find(dependencyName)) {
                if (!dependency->root.empty()) {
                    roots.insert(dependency->root.lexically_normal());
                }
            }
        }
        return roots;
    }

    roots.insert(workspaceRoot.lexically_normal());
    if (manifest) {
        for (const auto& dependency : manifest->dependencies) {
            if (const auto* resolved = graph.find(dependency.name)) {
                if (!resolved->root.empty()) {
                    roots.insert(resolved->root.lexically_normal());
                }
            }
        }
    }
    return roots;
}

static std::set<std::string> visibleDependencyNames(
    const std::optional<ProjectManifest>& manifest,
    const PackageGraph& graph,
    const std::filesystem::path& requesterPath) {

    std::set<std::string> names;
    if (const auto* owner = packageOwningPath(graph, requesterPath)) {
        names.insert(owner->dependencies.begin(), owner->dependencies.end());
        return names;
    }

    if (manifest) {
        for (const auto& dependency : manifest->dependencies) {
            names.insert(dependency.name);
        }
    }
    return names;
}

static bool sourcePathIsVisible(
    const std::filesystem::path& candidate,
    const std::filesystem::path& workspaceRoot,
    const PackageGraph& graph,
    const std::set<std::filesystem::path>& visibleRoots) {

    if (const auto* owner = packageOwningPath(graph, candidate)) {
        return visibleRoots.count(owner->root.lexically_normal()) != 0;
    }

    if (!pathHasPrefix(candidate, workspaceRoot)) return false;
    auto relative = candidate.lexically_normal().lexically_relative(workspaceRoot.lexically_normal());
    if (!relative.empty()) {
        auto first = relative.begin();
        if (first != relative.end() && first->string() == ".emojineer") return false;
    }
    return true;
}

std::vector<SymbolLocation> LanguageServer::findDefinitions(const std::string& uri, const Position& pos) {
    std::vector<SymbolLocation> results;

    auto doc = getDocument(uri);
    if (!doc) return results;

    // Find what identifier is at the cursor position
    auto identifier = findIdentifierAtPosition(doc->text, pos);
    if (!identifier) return results;

    // Get tokens for accurate position information
    auto tokensOpt = getTokens(uri);
    if (!tokensOpt) return results;
    const auto& tokens = tokensOpt->get();

    // Parse the document
    auto programOpt = getOrParseProgram(uri);
    if (!programOpt) return results;
    const auto& program = programOpt->get();

    // Helper to compute grapheme count from a string
    auto countGraphemes = [](const std::string& s) -> std::size_t {
        auto gs = segment_graphemes(s);
        return gs.size();
    };

    // Helper function to search for definitions in a program and add to results
    auto searchInProgram = [&](const std::string& searchUri, const ast::Program& searchProgram, const std::vector<Token>& searchTokens) {
        for (const auto& stmt : searchProgram.statements) {
            std::optional<std::uint32_t> actualColumn;

            // Check for function declarations
            if (auto* funcDecl = dynamic_cast<const ast::FunctionDecl*>(stmt.get())) {
                if (funcDecl->name == *identifier) {
                    actualColumn = findIdentifierColumn(*stmt, funcDecl->name, searchTokens);

                    SymbolLocation loc;
                    loc.uri = searchUri;
                    std::uint32_t startCol = actualColumn.value_or(2);
                    loc.range.start = {safeSizeToUint32(stmt->line > 0 ? stmt->line - 1 : 0), startCol};
                    // Use grapheme count instead of byte length, with safe cast
                    std::uint32_t nameEndCol = startCol + safeSizeToUint32(countGraphemes(funcDecl->name));
                    loc.range.end = {safeSizeToUint32(stmt->line > 0 ? stmt->line - 1 : 0), nameEndCol};
                    loc.name = funcDecl->name;
                    loc.symbolKind = "function";
                    results.push_back(loc);
                }
            }
            // Check for variable declarations
            else if (auto* varDecl = dynamic_cast<const ast::VarDecl*>(stmt.get())) {
                if (varDecl->name == *identifier) {
                    actualColumn = findIdentifierColumn(*stmt, varDecl->name, searchTokens);

                    SymbolLocation loc;
                    loc.uri = searchUri;
                    std::uint32_t startCol = actualColumn.value_or(2);
                    loc.range.start = {safeSizeToUint32(stmt->line > 0 ? stmt->line - 1 : 0), startCol};
                    // Use grapheme count instead of byte length, with safe cast
                    std::uint32_t nameEndCol = startCol + safeSizeToUint32(countGraphemes(varDecl->name));
                    loc.range.end = {safeSizeToUint32(stmt->line > 0 ? stmt->line - 1 : 0), nameEndCol};
                    loc.name = varDecl->name;
                    loc.symbolKind = "variable";
                    results.push_back(loc);
                }
            }
            // Check for module declarations
            else if (auto* modDecl = dynamic_cast<const ast::ModuleDecl*>(stmt.get())) {
                if (modDecl->name == *identifier) {
                    actualColumn = findIdentifierColumn(*stmt, modDecl->name, searchTokens);

                    SymbolLocation loc;
                    loc.uri = searchUri;
                    std::uint32_t startCol = actualColumn.value_or(2);
                    loc.range.start = {safeSizeToUint32(stmt->line > 0 ? stmt->line - 1 : 0), startCol};
                    // Use grapheme count instead of byte length, with safe cast
                    std::uint32_t nameEndCol = startCol + safeSizeToUint32(countGraphemes(modDecl->name));
                    loc.range.end = {safeSizeToUint32(stmt->line > 0 ? stmt->line - 1 : 0), nameEndCol};
                    loc.name = modDecl->name;
                    loc.symbolKind = "module";
                    results.push_back(loc);
                }
            }
        }
    };

    // Search in current document first
    searchInProgram(uri, program, tokens);

    // Search across other open documents (local modules)
    for (const auto& [otherUri, otherDoc] : openDocuments_) {
        if (otherUri == uri) continue;  // Skip current document
        if (workspaceRoot_ && packageGraph_) {
            auto visibleRoots = visibleSourceRoots(
                *workspaceRoot_, manifest_, *packageGraph_, std::filesystem::path(uriToPath(uri)));
            if (!sourcePathIsVisible(std::filesystem::path(uriToPath(otherUri)),
                                     *workspaceRoot_, *packageGraph_, visibleRoots)) {
                continue;
            }
        }
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
        const auto& otherTokens = otherTokensOpt->get();

        auto otherProgramOpt = getOrParseProgram(otherUri);
        if (!otherProgramOpt) continue;
        const auto& otherProgram = otherProgramOpt->get();

        searchInProgram(otherUri, otherProgram, otherTokens);
    }

    // Search in local modules from the filesystem (if workspace is available)
    // Use canonical PackageGraph for authorized source universe
    if (workspaceRoot_ && packageGraph_) {
        std::filesystem::path root = *workspaceRoot_;

        // Collect authorized package paths from PackageGraph
        // This includes root, local modules, path packages, and materialized registry packages
        auto authorizedPaths = visibleSourceRoots(
            root, manifest_, *packageGraph_, std::filesystem::path(uriToPath(uri)));

        for (const auto& authorizedPath : authorizedPaths) {
            if (!std::filesystem::exists(authorizedPath)) continue;

            // Skip .emojineer directory (contains package cache)
            if (authorizedPath.filename() == ".emojineer") continue;

            try {
                if (std::filesystem::is_directory(authorizedPath)) {
                    for (const auto& entry : std::filesystem::directory_iterator(authorizedPath)) {
                        if (entry.is_regular_file() &&
                            (entry.path().extension() == ".emj" || entry.path().extension() == ".emoji")) {
                            std::string moduleUri = pathToUri(entry.path());

                            // Skip if already searched
                            if (openDocuments_.count(moduleUri)) continue;

                            try {
                                std::string source = readFile(entry.path());
                                CustomEmojiRegistry reg = registry_;
                                Lexer lexer(source, reg);
                                auto modTokens = lexer.tokenize();
                                Parser parser(std::move(modTokens));
                                auto modProgram = parser.parse();

                                searchInProgram(moduleUri, modProgram, {});
                            } catch (...) {
                                // Skip files that can't be parsed
                            }
                        }
                    }
                } else if (authorizedPath.extension() == ".emj" || authorizedPath.extension() == ".emoji") {
                    // It's a file at root level
                    std::string moduleUri = pathToUri(authorizedPath);

                    if (!openDocuments_.count(moduleUri)) {
                        try {
                            std::string source = readFile(authorizedPath);
                            CustomEmojiRegistry reg = registry_;
                            Lexer lexer(source, reg);
                            auto modTokens = lexer.tokenize();
                            Parser parser(std::move(modTokens));
                            auto modProgram = parser.parse();

                            searchInProgram(moduleUri, modProgram, {});
                        } catch (...) {
                            // Skip files that can't be parsed
                        }
                    }
                }
            } catch (...) {
                // Skip paths we can't access
            }
        }
    }

    return results;
}

// Helper to find references in expressions - forward declaration
// Note: findReferencesInStmt is defined later in this file
void findReferencesInStmt(const ast::Stmt& stmt, const std::string& target,
                         const std::string& uri, std::vector<SymbolLocation>& results,
                         const std::vector<Token>& tokens, bool includeDeclaration);

// Helper to find references in expressions - uses tokens for accurate ranges
static void findReferencesInExpr(const ast::Expr& expr, const std::string& target,
                          const std::string& uri, std::vector<SymbolLocation>& results,
                          const std::vector<Token>& tokens) {
    if (auto* varExpr = dynamic_cast<const ast::VariableExpr*>(&expr)) {
        if (varExpr->name == target) {
            // Find the token for this variable reference
            for (const auto& token : tokens) {
                if (token.line == expr.line && token.kind == TokenKind::Identifier &&
                    (token.lexeme == target || token.canonical == target)) {
                    SymbolLocation loc;
                    loc.uri = uri;
                    // Use grapheme column from token (0-indexed for internal), with safe cast
                    loc.range.start = {safeSizeToUint32(expr.line > 0 ? expr.line - 1 : 0), safeSizeToUint32(token.column > 0 ? token.column - 1 : 0)};
                    // Compute end column using grapheme count
                    auto gs = segment_graphemes(token.lexeme);
                    loc.range.end = {safeSizeToUint32(expr.line > 0 ? expr.line - 1 : 0), safeSizeToUint32(token.column > 0 ? token.column - 1 : 0) + safeSizeToUint32(gs.size())};
                    loc.name = varExpr->name;
                    loc.symbolKind = "variable";
                    results.push_back(loc);
                    return; // Found the token, no need to continue
                }
            }
            // Fallback if token not found
            SymbolLocation loc;
            loc.uri = uri;
            loc.range.start = {safeSizeToUint32(expr.line > 0 ? expr.line - 1 : 0), 0};
            loc.range.end = {safeSizeToUint32(expr.line > 0 ? expr.line - 1 : 0), safeSizeToUint32(target.length())};
            loc.name = varExpr->name;
            loc.symbolKind = "variable";
            results.push_back(loc);
        }
    }
    else if (auto* callExpr = dynamic_cast<const ast::CallExpr*>(&expr)) {
        if (callExpr->callee == target) {
            // Find the token for this function call
            for (const auto& token : tokens) {
                if (token.line == expr.line && token.kind == TokenKind::Identifier &&
                    (token.lexeme == target || token.canonical == target)) {
                    SymbolLocation loc;
                    loc.uri = uri;
                    loc.range.start = {safeSizeToUint32(expr.line > 0 ? expr.line - 1 : 0), safeSizeToUint32(token.column > 0 ? token.column - 1 : 0)};
                    auto gs = segment_graphemes(token.lexeme);
                    loc.range.end = {safeSizeToUint32(expr.line > 0 ? expr.line - 1 : 0), safeSizeToUint32(token.column > 0 ? token.column - 1 : 0) + safeSizeToUint32(gs.size())};
                    loc.name = callExpr->callee;
                    loc.symbolKind = "function";
                    results.push_back(loc);
                    return;
                }
            }
            // Fallback
            SymbolLocation loc;
            loc.uri = uri;
            loc.range.start = {safeSizeToUint32(expr.line > 0 ? expr.line - 1 : 0), 0};
            loc.range.end = {safeSizeToUint32(expr.line > 0 ? expr.line - 1 : 0), safeSizeToUint32(target.length())};
            loc.name = callExpr->callee;
            loc.symbolKind = "function";
            results.push_back(loc);
        }
        // Recurse into arguments
        for (const auto& arg : callExpr->arguments) {
            findReferencesInExpr(*arg, target, uri, results, tokens);
        }
    }
    // Recurse into binary expressions
    else if (auto* binExpr = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
        findReferencesInExpr(*binExpr->left, target, uri, results, tokens);
        findReferencesInExpr(*binExpr->right, target, uri, results, tokens);
    }
    else if (auto* unaryExpr = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
        findReferencesInExpr(*unaryExpr->right, target, uri, results, tokens);
    }
}

// Helper to find references in statements
void findReferencesInStmt(const ast::Stmt& stmt, const std::string& target,
                         const std::string& uri, std::vector<SymbolLocation>& results,
                         const std::vector<Token>& tokens, bool includeDeclaration) {
    // Check if this is a definition of the target
    bool isDefinition = false;
    if (auto* funcDecl = dynamic_cast<const ast::FunctionDecl*>(&stmt)) {
        if (funcDecl->name == target) isDefinition = true;
    } else if (auto* varDecl = dynamic_cast<const ast::VarDecl*>(&stmt)) {
        if (varDecl->name == target) isDefinition = true;
    } else if (auto* modDecl = dynamic_cast<const ast::ModuleDecl*>(&stmt)) {
        if (modDecl->name == target) isDefinition = true;
    }

    // If this is a definition and we're not including declarations, skip
    if (isDefinition && !includeDeclaration) {
        // But still search for references within the definition body
    } else if (isDefinition && includeDeclaration) {
        // Add the definition as a reference
        for (const auto& token : tokens) {
            if (token.line == stmt.line && token.kind == TokenKind::Identifier &&
                (token.lexeme == target || token.canonical == target)) {
                SymbolLocation loc;
                loc.uri = uri;
                loc.range.start = {safeSizeToUint32(stmt.line > 0 ? stmt.line - 1 : 0), safeSizeToUint32(token.column > 0 ? token.column - 1 : 0)};
                auto gs = segment_graphemes(token.lexeme);
                loc.range.end = {safeSizeToUint32(stmt.line > 0 ? stmt.line - 1 : 0), safeSizeToUint32(token.column > 0 ? token.column - 1 : 0) + safeSizeToUint32(gs.size())};
                loc.name = target;
                loc.symbolKind = "variable";
                results.push_back(loc);
                break;
            }
        }
    }

    // Assignment - check the name
    if (auto* assign = dynamic_cast<const ast::Assignment*>(&stmt)) {
        if (assign->name == target) {
            // Find the token for this assignment target
            for (const auto& token : tokens) {
                if (token.line == stmt.line && token.kind == TokenKind::Identifier &&
                    (token.lexeme == target || token.canonical == target)) {
                    SymbolLocation loc;
                    loc.uri = uri;
                    loc.range.start = {safeSizeToUint32(stmt.line > 0 ? stmt.line - 1 : 0), safeSizeToUint32(token.column > 0 ? token.column - 1 : 0)};
                    auto gs = segment_graphemes(token.lexeme);
                    loc.range.end = {safeSizeToUint32(stmt.line > 0 ? stmt.line - 1 : 0), safeSizeToUint32(token.column > 0 ? token.column - 1 : 0) + safeSizeToUint32(gs.size())};
                    loc.name = assign->name;
                    loc.symbolKind = "variable";
                    results.push_back(loc);
                    break;
                }
            }
        }
        findReferencesInExpr(*assign->value, target, uri, results, tokens);
    }
    // Print/Return statements - check expression
    else if (auto* print = dynamic_cast<const ast::PrintStmt*>(&stmt)) {
        findReferencesInExpr(*print->expression, target, uri, results, tokens);
    }
    else if (auto* ret = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
        findReferencesInExpr(*ret->expression, target, uri, results, tokens);
    }
    // If statement
    else if (auto* ifStmt = dynamic_cast<const ast::IfStmt*>(&stmt)) {
        findReferencesInExpr(*ifStmt->condition, target, uri, results, tokens);
        for (const auto& s : ifStmt->then_branch) {
            if (s) findReferencesInStmt(*s, target, uri, results, tokens, includeDeclaration);
        }
        for (const auto& s : ifStmt->else_branch) {
            if (s) findReferencesInStmt(*s, target, uri, results, tokens, includeDeclaration);
        }
    }
    // While statement
    else if (auto* whileStmt = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
        findReferencesInExpr(*whileStmt->condition, target, uri, results, tokens);
        for (const auto& s : whileStmt->body) {
            if (s) findReferencesInStmt(*s, target, uri, results, tokens, includeDeclaration);
        }
    }
}

std::vector<SymbolLocation> LanguageServer::findReferences(const std::string& uri, const Position& pos, bool includeDeclaration) {
    std::vector<SymbolLocation> results;

    auto doc = getDocument(uri);
    if (!doc) return results;

    // Find what identifier is at the cursor position
    auto identifier = findIdentifierAtPosition(doc->text, pos);
    if (!identifier) return results;

    // Get tokens for accurate position information
    auto tokensOpt = getTokens(uri);
    if (!tokensOpt) return results;
    const auto& tokens = tokensOpt->get();

    // Parse the document
    auto programOpt = getOrParseProgram(uri);
    if (!programOpt) return results;
    const auto& program = programOpt->get();

    // Search for references to this identifier in current document
    for (const auto& stmt : program.statements) {
        findReferencesInStmt(*stmt, *identifier, uri, results, tokens, includeDeclaration);
    }

    // Search across other open documents (local modules)
    for (const auto& [otherUri, otherDoc] : openDocuments_) {
        if (otherUri == uri) continue;  // Skip current document
        if (workspaceRoot_ && packageGraph_) {
            auto visibleRoots = visibleSourceRoots(
                *workspaceRoot_, manifest_, *packageGraph_, std::filesystem::path(uriToPath(uri)));
            if (!sourcePathIsVisible(std::filesystem::path(uriToPath(otherUri)),
                                     *workspaceRoot_, *packageGraph_, visibleRoots)) {
                continue;
            }
        }
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
        const auto& otherTokens = otherTokensOpt->get();

        auto otherProgramOpt = getOrParseProgram(otherUri);
        if (!otherProgramOpt) continue;
        const auto& otherProgram = otherProgramOpt->get();

        for (const auto& stmt : otherProgram.statements) {
            findReferencesInStmt(*stmt, *identifier, otherUri, results, otherTokens, includeDeclaration);
        }
    }

    // Search in local modules from the filesystem (same logic as definitions)
    // Use canonical PackageGraph for authorized source universe
    if (workspaceRoot_ && packageGraph_) {
        std::filesystem::path root = *workspaceRoot_;

        // Collect authorized paths from PackageGraph
        auto authorizedPaths = visibleSourceRoots(
            root, manifest_, *packageGraph_, std::filesystem::path(uriToPath(uri)));

        for (const auto& authorizedPath : authorizedPaths) {
            if (!std::filesystem::exists(authorizedPath)) continue;
            if (authorizedPath.filename() == ".emojineer") continue;

            try {
                if (std::filesystem::is_directory(authorizedPath)) {
                    for (const auto& entry : std::filesystem::directory_iterator(authorizedPath)) {
                        if (entry.is_regular_file() &&
                            (entry.path().extension() == ".emj" || entry.path().extension() == ".emoji")) {
                            std::string moduleUri = pathToUri(entry.path());

                            if (openDocuments_.count(moduleUri)) continue;

                            try {
                                std::string source = readFile(entry.path());
                                CustomEmojiRegistry reg = registry_;
                                Lexer lexer(source, reg);
                                auto modTokens = lexer.tokenize();
                                Parser parser(std::move(modTokens));
                                auto modProgram = parser.parse();

                                for (const auto& stmt : modProgram.statements) {
                                    findReferencesInStmt(*stmt, *identifier, moduleUri, results, modTokens, includeDeclaration);
                                }
                            } catch (...) {}
                        }
                    }
                }
            } catch (...) {}
        }
    }

    return results;
}

JsonValue LanguageServer::handleCompletion(const JsonValue& params) {
    auto textDoc = getJsonObject(params, "textDocument");
    std::string uri = getJsonString(getJsonObject(textDoc, "uri"));

    auto doc = getDocument(uri);
    if (!doc) return JsonValue(json::makeArray());

    auto posObj = getJsonObject(params, "position");
    std::uint32_t line = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "line")));
    std::uint32_t utf16Char = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "character")));

    // Convert UTF-16 position to grapheme position for lexer
    std::string lineStr = getLine(doc->text, line);
    auto graphemeCol = utf16ColumnToGraphemeColumn(lineStr, utf16Char);
    if (!graphemeCol) return JsonValue(json::makeArray());

    // Convert to internal position
    Position internalPos;
    internalPos.line = line;
    internalPos.character = *graphemeCol;

    auto completions = getCompletions(uri, internalPos);

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

    // Get the lexical prefix at the current grapheme position.
    auto doc = getDocument(uri);
    std::string prefix;
    if (doc) {
        std::string lineText = getLine(doc->text, pos.line);
        auto lineGraphemes = segment_graphemes(lineText);
        if (pos.character <= lineGraphemes.size()) {
            std::string beforeCursor;
            for (std::size_t i = 0; i < pos.character; ++i) {
                beforeCursor += lineGraphemes[i].display;
            }

            auto prefixGraphemes = segment_graphemes(beforeCursor);
            std::size_t begin = prefixGraphemes.size();
            while (begin > 0) {
                const auto& g = prefixGraphemes[begin - 1].display;
                bool delimiter = g.empty();
                if (!delimiter) {
                    delimiter = std::all_of(g.begin(), g.end(), [](unsigned char ch) {
                        return std::isspace(ch) != 0;
                    });
                }
                if (!delimiter && g.size() == 1) {
                    const char ch = g[0];
                    delimiter = ch == '(' || ch == ')' || ch == '[' || ch == ']' ||
                                ch == '{' || ch == '}' || ch == ',' || ch == ':' ||
                                ch == ';' || ch == '=' || ch == '+' || ch == '-' ||
                                ch == '*' || ch == '/' || ch == '<' || ch == '>';
                }
                if (delimiter) break;
                --begin;
            }
            for (std::size_t i = begin; i < prefixGraphemes.size(); ++i) {
                prefix += prefixGraphemes[i].display;
            }
        }
    }

    // Helper to filter by prefix - use string_view for compatibility
    auto matchesPrefix = [&prefix](std::string_view s) {
        if (prefix.empty()) return true;
        return s.rfind(prefix, 0) == 0;
    };

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
        if (matchesPrefix(emoji)) {
            CompletionItem item;
            item.label = emoji;
            item.detail = desc;
            item.kind = static_cast<int>(CompletionItemKind::Keyword);
            completions.push_back(item);
        }
    }

    for (const auto& def : registry_.definitions()) {
        if (!def.custom && matchesPrefix(def.alias)) {
            CompletionItem item;
            item.label = def.alias;
            item.detail = def.description;
            item.kind = static_cast<int>(CompletionItemKind::Variable);
            completions.push_back(item);
        }
    }

    for (const auto& module : standard_modules()) {
        if (matchesPrefix(module.specifier)) {
            CompletionItem item;
            item.label = std::string(module.specifier);
            item.detail = std::string(module.description);
            item.kind = static_cast<int>(CompletionItemKind::Module);
            completions.push_back(item);
        }
    }

    // Add only dependencies visible from the requesting package/root.
    if (packageGraph_ && workspaceRoot_) {
        auto dependencyNames = visibleDependencyNames(
            manifest_, *packageGraph_, std::filesystem::path(uriToPath(uri)));
        for (const auto& dependencyName : dependencyNames) {
            const auto* pkg = packageGraph_->find(dependencyName);
            if (!pkg || !matchesPrefix(pkg->name)) continue;

            CompletionItem item;
            item.label = pkg->name;
            if (pkg->source_kind == DependencyKind::Path) {
                item.detail = "path package";
            } else {
                item.detail = "registry package";
            }
            item.kind = static_cast<int>(CompletionItemKind::Module);
            completions.push_back(item);
        }
    }

    // Add user-defined symbols from current document
    if (doc) {
        auto programOpt = getOrParseProgram(uri);
        if (programOpt) {
            const auto& program = programOpt->get();
            for (const auto& stmt : program.statements) {
                if (auto* funcDecl = dynamic_cast<const ast::FunctionDecl*>(stmt.get())) {
                    if (matchesPrefix(funcDecl->name)) {
                        CompletionItem item;
                        item.label = funcDecl->name;
                        item.detail = "function";
                        item.kind = static_cast<int>(CompletionItemKind::Function);
                        completions.push_back(item);
                    }
                }
                else if (auto* varDecl = dynamic_cast<const ast::VarDecl*>(stmt.get())) {
                    if (matchesPrefix(varDecl->name)) {
                        CompletionItem item;
                        item.label = varDecl->name;
                        item.detail = "variable";
                        item.kind = static_cast<int>(CompletionItemKind::Variable);
                        completions.push_back(item);
                    }
                }
                else if (auto* exportStmt = dynamic_cast<const ast::ExportStmt*>(stmt.get())) {
                    if (matchesPrefix(exportStmt->name)) {
                        CompletionItem item;
                        item.label = exportStmt->name;
                        item.detail = "exported symbol";
                        item.kind = static_cast<int>(CompletionItemKind::Variable);
                        completions.push_back(item);
                    }
                }
            }
        }
    }

    // Add symbols from other open documents (local modules)
    for (const auto& [otherUri, otherDoc] : openDocuments_) {
        if (otherUri == uri) continue;  // Skip current document
        if (workspaceRoot_ && packageGraph_) {
            auto visibleRoots = visibleSourceRoots(
                *workspaceRoot_, manifest_, *packageGraph_, std::filesystem::path(uriToPath(uri)));
            if (!sourcePathIsVisible(std::filesystem::path(uriToPath(otherUri)),
                                     *workspaceRoot_, *packageGraph_, visibleRoots)) {
                continue;
            }
        }
        if (workspaceRoot_ && packageGraph_) {
            auto visibleRoots = visibleSourceRoots(
                *workspaceRoot_, manifest_, *packageGraph_, std::filesystem::path(uriToPath(uri)));
            if (!sourcePathIsVisible(std::filesystem::path(uriToPath(otherUri)),
                                     *workspaceRoot_, *packageGraph_, visibleRoots)) {
                continue;
            }
        }

        auto otherProgramOpt = getOrParseProgram(otherUri);
        if (!otherProgramOpt) continue;
        const auto& otherProgram = otherProgramOpt->get();

        for (const auto& stmt : otherProgram.statements) {
            if (auto* funcDecl = dynamic_cast<const ast::FunctionDecl*>(stmt.get())) {
                if (matchesPrefix(funcDecl->name)) {
                    CompletionItem item;
                    item.label = funcDecl->name;
                    item.detail = "function (module)";
                    item.kind = static_cast<int>(CompletionItemKind::Function);
                    completions.push_back(item);
                }
            }
            else if (auto* varDecl = dynamic_cast<const ast::VarDecl*>(stmt.get())) {
                if (matchesPrefix(varDecl->name)) {
                    CompletionItem item;
                    item.label = varDecl->name;
                    item.detail = "variable (module)";
                    item.kind = static_cast<int>(CompletionItemKind::Variable);
                    completions.push_back(item);
                }
            }
            else if (auto* exportStmt = dynamic_cast<const ast::ExportStmt*>(stmt.get())) {
                if (matchesPrefix(exportStmt->name)) {
                    CompletionItem item;
                    item.label = exportStmt->name;
                    item.detail = "exported symbol (module)";
                    item.kind = static_cast<int>(CompletionItemKind::Variable);
                    completions.push_back(item);
                }
            }
            else if (auto* modDecl = dynamic_cast<const ast::ModuleDecl*>(stmt.get())) {
                if (matchesPrefix(modDecl->name)) {
                    CompletionItem item;
                    item.label = modDecl->name;
                    item.detail = "module";
                    item.kind = static_cast<int>(CompletionItemKind::Module);
                    completions.push_back(item);
                }
            }
        }
    }

    // Add symbols from authorized filesystem modules (same as definitions/references)
    // Use canonical PackageGraph for authorized source universe
    if (workspaceRoot_ && packageGraph_) {
        std::filesystem::path root = *workspaceRoot_;

        // Collect authorized paths from PackageGraph
        auto authorizedPaths = visibleSourceRoots(
            root, manifest_, *packageGraph_, std::filesystem::path(uriToPath(uri)));

        for (const auto& authorizedPath : authorizedPaths) {
            if (!std::filesystem::exists(authorizedPath)) continue;
            if (authorizedPath.filename() == ".emojineer") continue;

            try {
                if (std::filesystem::is_directory(authorizedPath)) {
                    for (const auto& entry : std::filesystem::directory_iterator(authorizedPath)) {
                        if (entry.is_regular_file() &&
                            (entry.path().extension() == ".emj" || entry.path().extension() == ".emoji")) {
                            std::string moduleUri = pathToUri(entry.path());

                            // Skip if already in open documents
                            if (openDocuments_.count(moduleUri)) continue;

                            try {
                                std::string source = readFile(entry.path());
                                CustomEmojiRegistry reg = registry_;
                                Lexer lexer(source, reg);
                                auto modTokens = lexer.tokenize();
                                Parser parser(std::move(modTokens));
                                auto modProgram = parser.parse();

                                for (const auto& stmt : modProgram.statements) {
                                    if (auto* funcDecl = dynamic_cast<const ast::FunctionDecl*>(stmt.get())) {
                                        if (matchesPrefix(funcDecl->name)) {
                                            CompletionItem item;
                                            item.label = funcDecl->name;
                                            item.detail = "function (" + entry.path().filename().string() + ")";
                                            item.kind = static_cast<int>(CompletionItemKind::Function);
                                            completions.push_back(item);
                                        }
                                    }
                                    else if (auto* varDecl = dynamic_cast<const ast::VarDecl*>(stmt.get())) {
                                        if (matchesPrefix(varDecl->name)) {
                                            CompletionItem item;
                                            item.label = varDecl->name;
                                            item.detail = "variable (" + entry.path().filename().string() + ")";
                                            item.kind = static_cast<int>(CompletionItemKind::Variable);
                                            completions.push_back(item);
                                        }
                                    }
                                    else if (auto* exportStmt = dynamic_cast<const ast::ExportStmt*>(stmt.get())) {
                                        if (matchesPrefix(exportStmt->name)) {
                                            CompletionItem item;
                                            item.label = exportStmt->name;
                                            item.detail = "exported (" + entry.path().filename().string() + ")";
                                            item.kind = static_cast<int>(CompletionItemKind::Variable);
                                            completions.push_back(item);
                                        }
                                    }
                                    else if (auto* modDecl = dynamic_cast<const ast::ModuleDecl*>(stmt.get())) {
                                        if (matchesPrefix(modDecl->name)) {
                                            CompletionItem item;
                                            item.label = modDecl->name;
                                            item.detail = "module (" + entry.path().filename().string() + ")";
                                            item.kind = static_cast<int>(CompletionItemKind::Module);
                                            completions.push_back(item);
                                        }
                                    }
                                }
                            } catch (...) {}
                        }
                    }
                }
            } catch (...) {}
        }
    }

    return completions;
}

JsonValue LanguageServer::handleDefinition(const JsonValue& params) {
    auto textDoc = getJsonObject(params, "textDocument");
    std::string uri = getJsonString(getJsonObject(textDoc, "uri"));

    auto doc = getDocument(uri);
    if (!doc) return JsonValue(json::makeArray());

    auto posObj = getJsonObject(params, "position");
    std::uint32_t line = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "line")));
    std::uint32_t utf16Char = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "character")));

    // Convert UTF-16 position to grapheme position for lexer
    std::string lineStr = getLine(doc->text, line);
    auto graphemeCol = utf16ColumnToGraphemeColumn(lineStr, utf16Char);
    if (!graphemeCol) return JsonValue(json::makeArray());

    Position internalPos;
    internalPos.line = line;
    internalPos.character = *graphemeCol;

    auto defs = findDefinitions(uri, internalPos);

    auto result = json::makeArray();
    for (const auto& def : defs) {
        auto loc = json::makeObject();
        json::objectSet(loc, "uri", JsonValue(def.uri));

        // Get the target document's text for proper position conversion
        // This supports cross-file navigation by using each target's own overlay/file
        auto targetDoc = getDocument(def.uri);
        std::string targetText;
        if (targetDoc) {
            targetText = targetDoc->text;
        } else {
            // Fallback to reading from disk if not in overlay
            try {
                auto path = uriToPath(def.uri);
                if (!path.empty()) {
                    targetText = readFile(std::filesystem::path(path));
                }
            } catch (...) {}
        }

        // Convert grapheme positions to UTF-16 for output
        std::string defLineStr = getLine(targetText.empty() ? doc->text : targetText, def.range.start.line);
        std::uint32_t utf16StartChar = graphemeColumnToUtf16Column(defLineStr, def.range.start.character);

        auto range = json::makeObject();
        auto start = json::makeObject();
        json::objectSet(start, "line", JsonValue(static_cast<double>(def.range.start.line)));
        json::objectSet(start, "character", JsonValue(static_cast<double>(utf16StartChar)));

        std::string defEndLineStr = getLine(targetText.empty() ? doc->text : targetText, def.range.end.line);
        std::uint32_t utf16EndChar = graphemeColumnToUtf16Column(defEndLineStr, def.range.end.character);

        auto end = json::makeObject();
        json::objectSet(end, "line", JsonValue(static_cast<double>(def.range.end.line)));
        json::objectSet(end, "character", JsonValue(static_cast<double>(utf16EndChar)));
        json::objectSet(range, "start", start);
        json::objectSet(range, "end", end);
        json::objectSet(loc, "range", range);

        json::arrayPushBack(result, loc);
    }

    return result;
}

JsonValue LanguageServer::handleReferences(const JsonValue& params) {
    // Extract context for includeDeclaration
    const auto* paramsObj = params.getPtr<std::map<std::string, JsonValue>>();
    bool includeDeclaration = true;
    if (paramsObj) {
        auto ctxIt = paramsObj->find("context");
        if (ctxIt != paramsObj->end()) {
            const JsonValue& ctx = ctxIt->second;
            const auto* ctxObj = ctx.getPtr<std::map<std::string, JsonValue>>();
            if (ctxObj) {
                auto declIt = ctxObj->find("includeDeclaration");
                if (declIt != ctxObj->end()) {
                    includeDeclaration = getJsonBool(declIt->second);
                }
            }
        }
    }

    auto textDoc = getJsonObject(params, "textDocument");
    std::string uri = getJsonString(getJsonObject(textDoc, "uri"));

    auto doc = getDocument(uri);
    if (!doc) return JsonValue(json::makeArray());

    auto posObj = getJsonObject(params, "position");
    std::uint32_t line = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "line")));
    std::uint32_t utf16Char = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "character")));

    // Convert UTF-16 position to grapheme position for lexer
    std::string lineStr = getLine(doc->text, line);
    auto graphemeCol = utf16ColumnToGraphemeColumn(lineStr, utf16Char);
    if (!graphemeCol) return JsonValue(json::makeArray());

    Position internalPos;
    internalPos.line = line;
    internalPos.character = *graphemeCol;

    auto refs = findReferences(uri, internalPos, includeDeclaration);

    auto result = json::makeArray();
    for (const auto& ref : refs) {
        auto loc = json::makeObject();
        json::objectSet(loc, "uri", JsonValue(ref.uri));

        // Get the target document's text for proper position conversion
        // This supports cross-file navigation by using each target's own overlay/file
        auto targetDoc = getDocument(ref.uri);
        std::string targetText;
        if (targetDoc) {
            targetText = targetDoc->text;
        } else {
            // Fallback to reading from disk if not in overlay
            try {
                auto path = uriToPath(ref.uri);
                if (!path.empty()) {
                    targetText = readFile(std::filesystem::path(path));
                }
            } catch (...) {}
        }

        // Convert grapheme positions to UTF-16 for output
        std::string refLineStr = getLine(targetText.empty() ? doc->text : targetText, ref.range.start.line);
        std::uint32_t utf16StartChar = graphemeColumnToUtf16Column(refLineStr, ref.range.start.character);

        auto range = json::makeObject();
        auto start = json::makeObject();
        json::objectSet(start, "line", JsonValue(static_cast<double>(ref.range.start.line)));
        json::objectSet(start, "character", JsonValue(static_cast<double>(utf16StartChar)));

        std::string refEndLineStr = getLine(targetText.empty() ? doc->text : targetText, ref.range.end.line);
        std::uint32_t utf16EndChar = graphemeColumnToUtf16Column(refEndLineStr, ref.range.end.character);

        auto end = json::makeObject();
        json::objectSet(end, "line", JsonValue(static_cast<double>(ref.range.end.line)));
        json::objectSet(end, "character", JsonValue(static_cast<double>(utf16EndChar)));
        json::objectSet(range, "start", start);
        json::objectSet(range, "end", end);
        json::objectSet(loc, "range", range);

        json::arrayPushBack(result, loc);
    }

    return result;
}

JsonValue LanguageServer::handleDocumentSymbol(const JsonValue& params) {
    auto textDoc = getJsonObject(params, "textDocument");
    std::string uri = getJsonString(getJsonObject(textDoc, "uri"));

    auto symbols = getDocumentSymbols(uri);

    auto result = json::makeArray();
    for (const auto& sym : symbols) {
        auto symJson = json::makeObject();
        json::objectSet(symJson, "name", JsonValue(sym.name));
        if (sym.kind) json::objectSet(symJson, "kind", JsonValue(static_cast<double>(*sym.kind)));

        // Range
        auto range = json::makeObject();
        auto start = json::makeObject();
        json::objectSet(start, "line", JsonValue(static_cast<double>(sym.range.start.line)));
        json::objectSet(start, "character", JsonValue(static_cast<double>(sym.range.start.character)));
        auto end = json::makeObject();
        json::objectSet(end, "line", JsonValue(static_cast<double>(sym.range.end.line)));
        json::objectSet(end, "character", JsonValue(static_cast<double>(sym.range.end.character)));
        json::objectSet(range, "start", start);
        json::objectSet(range, "end", end);
        json::objectSet(symJson, "range", range);

        // Selection range
        auto selRange = json::makeObject();
        auto selStart = json::makeObject();
        json::objectSet(selStart, "line", JsonValue(static_cast<double>(sym.selectionRange.start.line)));
        json::objectSet(selStart, "character", JsonValue(static_cast<double>(sym.selectionRange.start.character)));
        auto selEnd = json::makeObject();
        json::objectSet(selEnd, "line", JsonValue(static_cast<double>(sym.selectionRange.end.line)));
        json::objectSet(selEnd, "character", JsonValue(static_cast<double>(sym.selectionRange.end.character)));
        json::objectSet(selRange, "start", selStart);
        json::objectSet(selRange, "end", selEnd);
        json::objectSet(symJson, "selectionRange", selRange);

        if (sym.detail) json::objectSet(symJson, "detail", JsonValue(*sym.detail));

        json::arrayPushBack(result, symJson);
    }

    return result;
}

std::vector<DocumentSymbol> LanguageServer::getDocumentSymbols(const std::string& uri) {
    std::vector<DocumentSymbol> symbols;

    auto doc = getDocument(uri);
    if (!doc) return symbols;

    auto programOpt = getOrParseProgram(uri);
    auto tokensOpt = getTokens(uri);
    if (!programOpt || !tokensOpt) return symbols;
    const auto& program = programOpt->get();
    const auto& tokens = tokensOpt->get();

    auto declarationRange = [&](std::size_t line, const std::string& name) -> std::optional<Range> {
        for (const auto& token : tokens) {
            if (token.line == line && token.kind == TokenKind::Identifier &&
                (token.lexeme == name || token.canonical == name)) {
                return tokenToRange(doc->text, token);
            }
        }
        return std::nullopt;
    };

    for (const auto& stmt : program.statements) {
        if (auto* funcDecl = dynamic_cast<const ast::FunctionDecl*>(stmt.get())) {
            auto exact = declarationRange(stmt->line, funcDecl->name);
            if (!exact) continue;
            DocumentSymbol sym;
            sym.name = funcDecl->name;
            sym.kind = static_cast<int>(SymbolKind::Function);
            sym.range = *exact;
            sym.selectionRange = *exact;
            sym.detail = "function";
            symbols.push_back(std::move(sym));
        } else if (auto* varDecl = dynamic_cast<const ast::VarDecl*>(stmt.get())) {
            auto exact = declarationRange(stmt->line, varDecl->name);
            if (!exact) continue;
            DocumentSymbol sym;
            sym.name = varDecl->name;
            sym.kind = static_cast<int>(SymbolKind::Variable);
            sym.range = *exact;
            sym.selectionRange = *exact;
            symbols.push_back(std::move(sym));
        } else if (auto* modDecl = dynamic_cast<const ast::ModuleDecl*>(stmt.get())) {
            auto exact = declarationRange(stmt->line, modDecl->name);
            if (!exact) continue;
            DocumentSymbol sym;
            sym.name = modDecl->name;
            sym.kind = static_cast<int>(SymbolKind::Module);
            sym.range = *exact;
            sym.selectionRange = *exact;
            sym.detail = "module";
            symbols.push_back(std::move(sym));
        }
    }

    return symbols;
}

JsonValue LanguageServer::handleWorkspaceSymbol(const JsonValue& params) {
    auto queryObj = getJsonObject(params, "query");
    std::string query = getJsonString(queryObj);

    auto symbols = getWorkspaceSymbols(query);

    auto result = json::makeArray();
    for (const auto& sym : symbols) {
        auto symJson = json::makeObject();
        json::objectSet(symJson, "name", JsonValue(sym.name));
        if (sym.kind) json::objectSet(symJson, "kind", JsonValue(static_cast<double>(*sym.kind)));

        auto loc = json::makeObject();
        json::objectSet(loc, "uri", JsonValue(sym.location.uri));
        auto range = json::makeObject();
        auto start = json::makeObject();
        json::objectSet(start, "line", JsonValue(static_cast<double>(sym.location.range.start.line)));
        json::objectSet(start, "character", JsonValue(static_cast<double>(sym.location.range.start.character)));
        auto end = json::makeObject();
        json::objectSet(end, "line", JsonValue(static_cast<double>(sym.location.range.end.line)));
        json::objectSet(end, "character", JsonValue(static_cast<double>(sym.location.range.end.character)));
        json::objectSet(range, "start", start);
        json::objectSet(range, "end", end);
        json::objectSet(loc, "range", range);
        json::objectSet(symJson, "location", loc);

        if (sym.containerName) json::objectSet(symJson, "containerName", JsonValue(*sym.containerName));

        json::arrayPushBack(result, symJson);
    }

    return result;
}

std::vector<SymbolInformation> LanguageServer::getWorkspaceSymbols(const std::string& query) {
    std::vector<SymbolInformation> symbols;

    // Search across all open documents
    std::set<std::filesystem::path> workspaceVisibleRoots;
    if (workspaceRoot_ && packageGraph_) {
        workspaceVisibleRoots = visibleSourceRoots(
            *workspaceRoot_, manifest_, *packageGraph_, *workspaceRoot_);
    }

    for (const auto& [uri, doc] : openDocuments_) {
        if (workspaceRoot_ && packageGraph_ &&
            !sourcePathIsVisible(std::filesystem::path(uriToPath(uri)),
                                 *workspaceRoot_, *packageGraph_, workspaceVisibleRoots)) {
            continue;
        }
        auto docSymbols = getDocumentSymbols(uri);
        for (const auto& ds : docSymbols) {
            // Filter by query if provided
            if (query.empty() || ds.name.find(query) != std::string::npos) {
                SymbolInformation info;
                info.name = ds.name;
                info.kind = ds.kind;
                info.location = {uri, ds.range};
                symbols.push_back(info);
            }
        }
    }

    // Search in local modules from the filesystem (same logic as definitions/references)
    // Use canonical PackageGraph for authorized source universe
    if (workspaceRoot_ && packageGraph_) {
        std::filesystem::path root = *workspaceRoot_;

        // Collect authorized paths from PackageGraph
        auto authorizedPaths = visibleSourceRoots(
            root, manifest_, *packageGraph_, root);

        for (const auto& authorizedPath : authorizedPaths) {
            if (!std::filesystem::exists(authorizedPath)) continue;
            if (authorizedPath.filename() == ".emojineer") continue;

            try {
                if (std::filesystem::is_directory(authorizedPath)) {
                    for (const auto& entry : std::filesystem::directory_iterator(authorizedPath)) {
                        if (entry.is_regular_file() &&
                            (entry.path().extension() == ".emj" || entry.path().extension() == ".emoji")) {
                            std::string moduleUri = pathToUri(entry.path());

                            // Skip if already in open documents (already searched)
                            if (openDocuments_.count(moduleUri)) continue;

                            try {
                                std::string source = readFile(entry.path());
                                CustomEmojiRegistry reg = registry_;
                                Lexer lexer(source, reg);
                                auto modTokens = lexer.tokenize();
                                Parser parser(std::move(modTokens));
                                auto modProgram = parser.parse();

                                // Extract symbols from this program
                                for (const auto& stmt : modProgram.statements) {
                                    if (auto* funcDecl = dynamic_cast<const ast::FunctionDecl*>(stmt.get())) {
                                        if (query.empty() || funcDecl->name.find(query) != std::string::npos) {
                                            SymbolInformation info;
                                            info.name = funcDecl->name;
                                            info.kind = static_cast<int>(SymbolKind::Function);

                                            // Find token for range
                                            for (const auto& token : modTokens) {
                                                if (token.line == funcDecl->line && token.kind == TokenKind::Identifier &&
                                                    (token.lexeme == funcDecl->name || token.canonical == funcDecl->name)) {
                                                    info.location = {moduleUri, tokenToRange(source, token)};
                                                    break;
                                                }
                                            }
                                            symbols.push_back(info);
                                        }
                                    }
                                    else if (auto* varDecl = dynamic_cast<const ast::VarDecl*>(stmt.get())) {
                                        if (query.empty() || varDecl->name.find(query) != std::string::npos) {
                                            SymbolInformation info;
                                            info.name = varDecl->name;
                                            info.kind = static_cast<int>(SymbolKind::Variable);

                                            for (const auto& token : modTokens) {
                                                if (token.line == varDecl->line && token.kind == TokenKind::Identifier &&
                                                    (token.lexeme == varDecl->name || token.canonical == varDecl->name)) {
                                                    info.location = {moduleUri, tokenToRange(source, token)};
                                                    break;
                                                }
                                            }
                                            symbols.push_back(info);
                                        }
                                    }
                                    else if (auto* modDecl = dynamic_cast<const ast::ModuleDecl*>(stmt.get())) {
                                        if (query.empty() || modDecl->name.find(query) != std::string::npos) {
                                            SymbolInformation info;
                                            info.name = modDecl->name;
                                            info.kind = static_cast<int>(SymbolKind::Module);

                                            for (const auto& token : modTokens) {
                                                if (token.line == modDecl->line && token.kind == TokenKind::Identifier &&
                                                    (token.lexeme == modDecl->name || token.canonical == modDecl->name)) {
                                                    info.location = {moduleUri, tokenToRange(source, token)};
                                                    break;
                                                }
                                            }
                                            symbols.push_back(info);
                                        }
                                    }
                                    else if (auto* exportStmt = dynamic_cast<const ast::ExportStmt*>(stmt.get())) {
                                        if (query.empty() || exportStmt->name.find(query) != std::string::npos) {
                                            SymbolInformation info;
                                            info.name = exportStmt->name;
                                            info.kind = static_cast<int>(SymbolKind::Variable);

                                            for (const auto& token : modTokens) {
                                                if (token.line == exportStmt->line && token.kind == TokenKind::Identifier &&
                                                    (token.lexeme == exportStmt->name || token.canonical == exportStmt->name)) {
                                                    info.location = {moduleUri, tokenToRange(source, token)};
                                                    break;
                                                }
                                            }
                                            symbols.push_back(info);
                                        }
                                    }
                                }
                            } catch (...) {}
                        }
                    }
                }
            } catch (...) {}
        }
    }

    return symbols;
}

JsonValue LanguageServer::handleFormatting(const JsonValue& params) {
    auto textDoc = getJsonObject(params, "textDocument");
    std::string uri = getJsonString(getJsonObject(textDoc, "uri"));

    auto doc = getDocument(uri);
    if (!doc) return json::makeArray();

    // Use the source_tools formatter
    try {
        std::string formatted = format_source(doc->text, registry_);

        // If no changes, return empty array
        if (formatted == doc->text) {
            return json::makeArray();
        }

        auto result = json::makeArray();
        auto edit = json::makeObject();

        // Full document range - from {0, 0} to actual EOF
        auto range = json::makeObject();
        auto start = json::makeObject();
        json::objectSet(start, "line", JsonValue(0.0));
        json::objectSet(start, "character", JsonValue(0.0));

        // Compute true UTF-16 EOF position
        // Count lines and last line's UTF-16 length
        // Handle all line endings: LF, CRLF, CR
        std::size_t lineCount = 0;  // 0-indexed line number
        std::size_t lastLineUtf16Col = 0;
        std::size_t i = 0;
        while (i < doc->text.size()) {
            auto [cp, len] = decodeUtf8(doc->text, i);

            // Check for CRLF first
            if (cp == '\r' && i + len < doc->text.size()) {
                auto [nextCp, nextLen] = decodeUtf8(doc->text, i + len);
                if (nextCp == '\n') {
                    // CRLF - count as one line ending
                    lineCount++;
                    lastLineUtf16Col = 0;
                    i += len + nextLen;
                    continue;
                }
            }

            // Handle CR (not followed by LF)
            if (cp == '\r') {
                lineCount++;
                lastLineUtf16Col = 0;
                i += len;
                continue;
            }

            // Handle LF
            if (cp == '\n') {
                lineCount++;
                lastLineUtf16Col = 0;
                i += len;
                continue;
            }

            // Regular character - count UTF-16 units
            // Count UTF-16 units (BMP = 1, supplementary = 2)
            lastLineUtf16Col += (cp >= 0x10000) ? 2 : 1;
            i += len;
        }

        // Determine if document ends with a line terminator
        bool endsWithLineTerminator = false;
        if (!doc->text.empty()) {
            std::size_t lastPos = doc->text.size() - 1;
            // Check for CRLF at end
            if (doc->text[lastPos] == '\n') {
                endsWithLineTerminator = true;
                if (lastPos > 0 && doc->text[lastPos - 1] == '\r') {
                    // It's CRLF - already handled
                }
            } else if (doc->text[lastPos] == '\r') {
                endsWithLineTerminator = true;
            }
        }

        // If document ends with line terminator, the EOF is at the start of a new empty line
        // Otherwise, EOF is at the end of the last content line
        if (endsWithLineTerminator) {
            // Document ends with newline, EOF is at start of new line (after the terminator)
            // lineCount is now the index of the empty line after the last newline
            // lastLineUtf16Col is already 0 from the line reset
        } else if (!doc->text.empty()) {
            // Document doesn't end with newline, EOF is at end of last content
            // lineCount is already correct (0-indexed)
        } else {
            // Empty document - EOF is at {0, 0}
        }

        auto end = json::makeObject();
        json::objectSet(end, "line", JsonValue(static_cast<double>(lineCount)));
        json::objectSet(end, "character", JsonValue(static_cast<double>(lastLineUtf16Col)));

        json::objectSet(range, "start", start);
        json::objectSet(range, "end", end);
        json::objectSet(edit, "range", range);
        json::objectSet(edit, "newText", JsonValue(formatted));

        json::arrayPushBack(result, edit);
        return result;
    } catch (...) {
        return json::makeArray();
    }
}

// Helper: split text into lines (preserving line endings with each line)
static std::vector<std::string> splitIntoLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    std::size_t i = 0;

    while (i < text.size()) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                // CRLF
                lines.push_back(text.substr(start, i - start + 2));
                i += 2;
            } else {
                // CR
                lines.push_back(text.substr(start, i - start + 1));
                i++;
            }
            start = i;
        } else if (text[i] == '\n') {
            // LF
            lines.push_back(text.substr(start, i - start + 1));
            i++;
            start = i;
        } else {
            i++;
        }
    }

    // Add remaining content (last line without newline or empty string if ends with newline)
    if (start < text.size() || text.empty()) {
        lines.push_back(text.substr(start));
    }

    return lines;
}

JsonValue LanguageServer::handleRangeFormatting(const JsonValue& params) {
    auto textDoc = getJsonObject(params, "textDocument");
    std::string uri = getJsonString(getJsonObject(textDoc, "uri"));

    auto doc = getDocument(uri);
    if (!doc) return json::makeArray();

    // Extract the range from params
    auto rangeObj = getJsonObject(params, "range");
    if (rangeObj.isNull()) {
        // No range provided, fall back to full document formatting
        return handleFormatting(params);
    }

    // Parse range start
    auto startObj = getJsonObject(rangeObj, "start");
    std::uint32_t startLine = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(startObj, "line")));
    std::uint32_t startChar = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(startObj, "character")));

    // Parse range end
    auto endObj = getJsonObject(rangeObj, "end");
    std::uint32_t endLine = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(endObj, "line")));
    std::uint32_t endChar = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(endObj, "character")));

    try {
        // Get the text lines
        std::vector<std::string> lines = splitIntoLines(doc->text);

        if (startLine >= lines.size() || endLine >= lines.size()) {
            return json::makeArray();
        }

        // Convert UTF-16 positions to UTF-8 offsets using the FULL document
        // This fixes the bug where passing line index > 0 with a single-line slice
        // would incorrectly count lines within the slice
        auto startOffset = utf16ToUtf8(doc->text, startLine, startChar);
        auto endOffset = utf16ToUtf8(doc->text, endLine, endChar);

        if (!startOffset || !endOffset) {
            return json::makeArray();
        }

        // The offsets are already absolute since we used the full document
        std::size_t absoluteStart = *startOffset;
        std::size_t absoluteEnd = *endOffset;

        // Extract the range text
        std::string rangeText = doc->text.substr(absoluteStart, absoluteEnd - absoluteStart);

        // Format just the range text
        std::string formattedRange = format_source(rangeText, registry_);

        // If no changes, return empty array
        if (formattedRange == rangeText) {
            return json::makeArray();
        }

        auto result = json::makeArray();
        auto edit = json::makeObject();

        // Return the range exactly as specified in the request
        auto range = json::makeObject();
        auto start = json::makeObject();
        json::objectSet(start, "line", JsonValue(static_cast<double>(startLine)));
        json::objectSet(start, "character", JsonValue(static_cast<double>(startChar)));

        auto end = json::makeObject();
        json::objectSet(end, "line", JsonValue(static_cast<double>(endLine)));
        json::objectSet(end, "character", JsonValue(static_cast<double>(endChar)));

        json::objectSet(range, "start", start);
        json::objectSet(range, "end", end);
        json::objectSet(edit, "range", range);
        json::objectSet(edit, "newText", JsonValue(formattedRange));

        json::arrayPushBack(result, edit);
        return result;
    } catch (...) {
        return json::makeArray();
    }
}

JsonValue LanguageServer::handleRequest(const std::string& method, const JsonValue& params) {
    // After shutdown, reject all requests except exit with -32600 (Invalid request)
    if (shutdown_) {
        throw JsonRpcError(JSONRPC_INVALID_REQUEST, "Server is shut down");
    }

    if (method == "initialize") return handleInitialize(params);
    if (method == "shutdown") return handleShutdown(params);
    if (method == "textDocument/hover") return handleHover(params);
    if (method == "textDocument/completion") return handleCompletion(params);
    if (method == "textDocument/definition") return handleDefinition(params);
    if (method == "textDocument/references") return handleReferences(params);
    if (method == "textDocument/documentSymbol") return handleDocumentSymbol(params);
    if (method == "workspace/symbol") return handleWorkspaceSymbol(params);
    if (method == "textDocument/formatting") return handleFormatting(params);

    // Unknown method - return -32601 (Method not found)
    throw JsonRpcError(JSONRPC_METHOD_NOT_FOUND, "Method not found: " + method);
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
    // Validate that message is an object
    auto* obj = message.getPtr<std::map<std::string, JsonValue>>();
    if (!obj) {
        // Invalid request - not an object
        throw JsonRpcError(-32600, "Invalid JSON-RPC request: not an object");
    }

    // Check for "jsonrpc" version field - must be exactly "2.0"
    auto jsonrpcIt = obj->find("jsonrpc");
    if (jsonrpcIt == obj->end() || !jsonrpcIt->second.isString()) {
        throw JsonRpcError(-32600, "Invalid JSON-RPC request: missing or invalid jsonrpc field");
    }
    std::string jsonrpc = getJsonString(jsonrpcIt->second);
    if (jsonrpc != "2.0") {
        throw JsonRpcError(-32600, "Invalid JSON-RPC version: expected \"2.0\"");
    }

    // Check for "method" field - must be a string
    auto methodIt = obj->find("method");
    if (methodIt == obj->end()) {
        // Could be a response - check if there's a "result" or "error" field
        auto resultIt = obj->find("result");
        auto errorIt = obj->find("error");
        if (resultIt == obj->end() && errorIt == obj->end()) {
            throw JsonRpcError(-32600, "Invalid JSON-RPC request: missing method field and not a response");
        }
        // This is a response object, not a request - ignore it
        return;
    }

    if (!methodIt->second.isString()) {
        throw JsonRpcError(-32600, "Invalid JSON-RPC request: method must be a string");
    }
    std::string method = getJsonString(methodIt->second);
    const JsonValue& params = getJsonObject(message, "params");
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

    // If there's an invalid ID, it's a request (has id), so return error
    if (hasInvalidId) {
        // Return -32600 Invalid Request with null id
        throw JsonRpcError(-32600, "Invalid JSON-RPC request: id must be a string or number");
    }

    // Handle the response - prefer string IDs if present
    if (stringId || intId) {
        try {
            auto result = handleRequest(method, params);
            if (stringId) {
                // Use string ID format
                std::cout << formatJsonRpcResponseStringId(result, *stringId);
            } else {
                std::cout << formatJsonRpcResponse(result, intId);
            }
        } catch (const JsonRpcError& e) {
            // Custom JSON-RPC error with specific code
            if (stringId) {
                std::cout << formatJsonRpcErrorStringId(e.code(), e.what(), *stringId);
            } else {
                std::cout << formatJsonRpcError(e.code(), e.what(), intId);
            }
        } catch (const std::exception& e) {
            if (stringId) {
                std::cout << formatJsonRpcErrorStringId(-32603, e.what(), *stringId);
            } else {
                std::cout << formatJsonRpcError(-32603, e.what(), intId);
            }
        }
    } else {
        // This is a notification - handle but don't send response
        // Note: Exceptions in notifications are silently ignored per JSON-RPC spec
        try {
            handleNotification(method, params);
        } catch (...) {
            // Silently ignore notification errors per JSON-RPC spec
        }
    }
}

int LanguageServer::run() {
    // Persistent buffer for carry-over data between reads
    std::string buffer;

    while (true) {
        // Read headers until we find Content-Length
        std::string headers;

        // First, try to find complete headers in buffer
        size_t headerEnd = buffer.find("\r\n\r\n");
        size_t headerEndAlt = buffer.find("\n\n");  // Alternative: just \n\n

        bool hasCompleteHeaders = false;
        size_t headerBreakPos = 0;

        if (headerEnd != std::string::npos) {
            hasCompleteHeaders = true;
            headerBreakPos = headerEnd + 4;  // After \r\n\r\n
            headers = buffer.substr(0, headerEnd);
        } else if (headerEndAlt != std::string::npos) {
            hasCompleteHeaders = true;
            headerBreakPos = headerEndAlt + 2;  // After \n\n
            headers = buffer.substr(0, headerEndAlt);
        }

        // If we don't have complete headers, read more data
        while (!hasCompleteHeaders) {
            char ch;
            if (!std::cin.get(ch)) {
                // EOF or error
                if (std::cin.eof() && buffer.empty()) {
                    return 0;  // Clean exit
                }
                break;
            }
            buffer += ch;

            // Check for complete headers again
            headerEnd = buffer.find("\r\n\r\n");
            headerEndAlt = buffer.find("\n\n");

            if (headerEnd != std::string::npos) {
                hasCompleteHeaders = true;
                headerBreakPos = headerEnd + 4;
                headers = buffer.substr(0, headerEnd);
            } else if (headerEndAlt != std::string::npos) {
                hasCompleteHeaders = true;
                headerBreakPos = headerEndAlt + 2;
                headers = buffer.substr(0, headerEndAlt);
            }
        }

        if (!hasCompleteHeaders) {
            // Couldn't read complete headers
            break;
        }

        // Keep any excess data in buffer for next message
        buffer = buffer.substr(headerBreakPos);

        // Parse Content-Length header
        size_t contentLength = 0;
        {
            size_t pos = headers.find("Content-Length:");
            if (pos != std::string::npos) {
                pos += 15; // Length of "Content-Length:"
                // Skip whitespace
                while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) {
                    pos++;
                }
                // Parse number
                size_t end = pos;
                while (end < headers.size() && std::isdigit(static_cast<unsigned char>(headers[end]))) {
                    end++;
                }
                if (end > pos) {
                    contentLength = std::stoul(headers.substr(pos, end - pos));
                }
            }
        }

        if (contentLength == 0) {
            // No valid Content-Length, skip and continue
            continue;
        }

        // Ensure we have enough data in buffer for the body
        while (buffer.size() < contentLength) {
            char ch;
            if (!std::cin.get(ch)) {
                // EOF or error before complete body
                break;
            }
            buffer += ch;
        }

        if (buffer.size() < contentLength) {
            // Incomplete message, break
            break;
        }

        // Extract the body and keep any excess
        std::string body = buffer.substr(0, contentLength);
        buffer = buffer.substr(contentLength);  // Carry over any excess for next message

        // Process the message
        try {
            auto json = parseJson(body);
            handleMessage(json);
            // Flush stdout to ensure response is sent
            std::cout.flush();
        } catch (const JsonRpcError& e) {
            // JSON-RPC level error (e.g., -32600 invalid request) - send error response
            // Note: Don't send error for notifications (no id)
            std::cout << formatJsonRpcError(e.code(), e.what(), std::nullopt);
            std::cout.flush();
        } catch (const std::exception& e) {
            // Send proper JSON-RPC error response for parse errors
            // This follows JSON-RPC 2.0 spec for -32700 Parse error
            std::string errorResponse = formatJsonRpcError(-32700, e.what(), std::nullopt);
            std::cout << errorResponse;
            std::cout.flush();
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
