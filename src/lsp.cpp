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
#include <typeinfo>

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
                case 'u':
                    // Handle \uXXXX escapes - must have exactly 4 hex digits
                    if (pos + 4 >= json.size()) {
                        throw std::runtime_error("incomplete \\u escape");
                    }
                    // Validate all 4 characters are hex digits
                    for (int i = 1; i <= 4; i++) {
                        char c = json[pos + i];
                        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                            throw std::runtime_error("invalid \\u escape: expected 4 hex digits");
                        }
                    }
                    {
                        std::string hex = json.substr(pos + 1, 4);
                        char32_t codePoint = static_cast<char32_t>(std::stoul(hex, nullptr, 16));
                        pos += 4;
                        
                        // First check for surrogate PAIR: if high surrogate, look for low surrogate
                        if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                            // High surrogate (0xD800-0xDBFF), expect \uXXXX low surrogate
                            if (pos + 6 < json.size() && json[pos] == '\\' && json[pos + 1] == 'u') {
                                // Validate low surrogate hex digits
                                for (int i = 2; i <= 5; i++) {
                                    char c = json[pos + i];
                                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                                        throw std::runtime_error("invalid low surrogate escape");
                                    }
                                }
                                std::string lowHex = json.substr(pos + 2, 4);
                                char32_t lowSurrogate = static_cast<char32_t>(std::stoul(lowHex, nullptr, 16));
                                if (lowSurrogate >= 0xDC00 && lowSurrogate <= 0xDFFF) {
                                    // Valid pair - decode to actual code point
                                    codePoint = decodeSurrogatePair(static_cast<char16_t>(codePoint), static_cast<char16_t>(lowSurrogate));
                                    pos += 6;
                                } else {
                                    throw std::runtime_error("high surrogate without valid low surrogate (DC00-DFFF)");
                                }
                            } else {
                                throw std::runtime_error("unpaired high surrogate without following \\uXXXX");
                            }
                        } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
                            // Low surrogate without preceding high surrogate - invalid
                            throw std::runtime_error("unpaired low surrogate");
                        } else if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
                            // This shouldn't be reached due to above checks, but safety net
                            throw std::runtime_error("invalid Unicode surrogate");
                        }
                        
                        // Validate final code point is in valid Unicode range
                        if (codePoint > 0x10FFFF) {
                            throw std::runtime_error("invalid Unicode code point > U+10FFFF");
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
    JsonValue result = parseJsonValue(json, pos);
    
    // Check for trailing content - reject JSON with data after the parsed value
    skipWhitespace(json, pos);
    if (pos < json.size()) {
        throw std::runtime_error("trailing content after JSON value");
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

// Helper to escape a JSON string value
// Properly handles UTF-8: decodes code points before escaping
static void escapeJsonString(const std::string& s, std::ostringstream& out) {
    std::size_t i = 0;
    while (i < s.size()) {
        unsigned char uc = static_cast<unsigned char>(s[i]);
        
        // Handle ASCII control characters
        if (uc < 0x20) {
            switch (uc) {
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                case '\b': out << "\\b"; break;
                case '\f': out << "\\f"; break;
                default: encodeUnicodeEscape(out, uc); break;
            }
            i++;
            continue;
        }
        
        // Handle ASCII printable characters (0x20-0x7E)
        if (uc < 0x80) {
            switch (uc) {
                case '"': out << "\\\""; break;
                case '\\': out << "\\\\"; break;
                case '/': out << "\\/"; break;
                default: out << static_cast<char>(uc); break;
            }
            i++;
            continue;
        }
        
        // Handle multi-byte UTF-8 sequences
        // Decode the UTF-8 code point
        char32_t codePoint = 0;
        std::size_t seqLen = 0;
        
        if ((uc & 0xE0) == 0xC0) {
            // 2-byte sequence
            if (i + 1 >= s.size()) {
                // Invalid: truncated sequence, escape as byte
                encodeUnicodeEscape(out, uc);
                i++;
                continue;
            }
            unsigned char uc2 = static_cast<unsigned char>(s[i + 1]);
            if ((uc2 & 0xC0) != 0x80) {
                // Invalid continuation byte
                encodeUnicodeEscape(out, uc);
                i++;
                continue;
            }
            codePoint = ((uc & 0x1F) << 6) | (uc2 & 0x3F);
            seqLen = 2;
        } else if ((uc & 0xF0) == 0xE0) {
            // 3-byte sequence
            if (i + 2 >= s.size()) {
                encodeUnicodeEscape(out, uc);
                i++;
                continue;
            }
            unsigned char uc2 = static_cast<unsigned char>(s[i + 1]);
            unsigned char uc3 = static_cast<unsigned char>(s[i + 2]);
            if ((uc2 & 0xC0) != 0x80 || (uc3 & 0xC0) != 0x80) {
                encodeUnicodeEscape(out, uc);
                i++;
                continue;
            }
            codePoint = ((uc & 0x0F) << 12) | ((uc2 & 0x3F) << 6) | (uc3 & 0x3F);
            seqLen = 3;
        } else if ((uc & 0xF8) == 0xF0) {
            // 4-byte sequence (supplementary plane)
            if (i + 3 >= s.size()) {
                encodeUnicodeEscape(out, uc);
                i++;
                continue;
            }
            unsigned char uc2 = static_cast<unsigned char>(s[i + 1]);
            unsigned char uc3 = static_cast<unsigned char>(s[i + 2]);
            unsigned char uc4 = static_cast<unsigned char>(s[i + 3]);
            if ((uc2 & 0xC0) != 0x80 || (uc3 & 0xC0) != 0x80 || (uc4 & 0xC0) != 0x80) {
                encodeUnicodeEscape(out, uc);
                i++;
                continue;
            }
            codePoint = ((uc & 0x07) << 18) | ((uc2 & 0x3F) << 12) | ((uc3 & 0x3F) << 6) | (uc4 & 0x3F);
            seqLen = 4;
        } else {
            // Invalid lead byte, escape as byte
            encodeUnicodeEscape(out, uc);
            i++;
            continue;
        }
        
        // Validate code point is valid Unicode scalar
        if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
            // Invalid Unicode, escape as bytes
            for (std::size_t j = 0; j < seqLen && i + j < s.size(); j++) {
                encodeUnicodeEscape(out, static_cast<char32_t>(static_cast<unsigned char>(s[i + j])));
            }
            i += seqLen;
            continue;
        }
        
        // Valid code point - encode properly
        encodeUnicodeEscape(out, codePoint);
        i += seqLen;
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

std::optional<std::string> LanguageServer::getSource(const std::string& uri) const {
    // First check overlay documents
    auto it = openDocuments_.find(uri);
    if (it != openDocuments_.end()) {
        return it->second.text;
    }
    
    // Fall back to filesystem
    if (workspaceRoot_) {
        std::filesystem::path filePath = uriToPath(uri);
        if (!filePath.empty() && std::filesystem::exists(filePath)) {
            try {
                return readFile(filePath);
            } catch (...) {
                return std::nullopt;
            }
        }
    }
    
    return std::nullopt;
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
        doc.diagnostics = diagnoseDocumentWithCompile(doc);
    } else {
        doc.diagnostics = diagnoseDocument(doc);
    }
    
    // Update the stored document with diagnostics
    openDocuments_[uri] = doc;
    publishDiagnostics(uri, doc.diagnostics);
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
            it->second.diagnostics = diagnoseDocumentWithCompile(it->second);
        } else {
            it->second.diagnostics = diagnoseDocument(it->second);
        }
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
    // Invalidate caches for this document
    parsedPrograms_.erase(uri);
    diagnosticsCache_.erase(uri);
    openDocuments_.erase(uri);
}

// Helper: validated UTF-8 decoder - decodes a single code point from UTF-8
// Returns true if successful, advances pos to after the decoded sequence
// Returns false if invalid UTF-8, advances pos by 1 byte
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
        pos += 1;
        return false;
    }
    
    // Check we have enough bytes
    if (pos + seqLen > text.size()) {
        // Incomplete sequence
        pos += 1;
        return false;
    }
    
    // Decode continuation bytes
    for (int i = 1; i < seqLen; i++) {
        unsigned char cb = static_cast<unsigned char>(text[pos + i]);
        if ((cb & 0xC0) != 0x80) {
            // Not a continuation byte - invalid UTF-8
            pos += 1;
            return false;
        }
        cp = (cp << 6) | (cb & 0x3F);
    }
    
    // Validate code point
    if (cp > 0x10FFFF) {
        // Out of Unicode range
        pos += 1;
        return false;
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) {
        // Surrogates invalid in UTF-8
        pos += 1;
        return false;
    }
    
    // Reject overlong encodings - each sequence length has a minimum code point
    if (seqLen == 2 && cp < 0x80) {
        pos += 1;
        return false;   // 2-byte must encode >= U+0080
    }
    if (seqLen == 3 && cp < 0x800) {
        pos += 1;
        return false;  // 3-byte must encode >= U+0800
    }
    if (seqLen == 4 && cp < 0x10000) {
        pos += 1;
        return false; // 4-byte must encode >= U+10000
    }
    
    codePoint = cp;
    pos += seqLen;
    return true;
}

// Helper: count UTF-16 code units for a Unicode code point
// This counts based on the actual code point value, not the UTF-8 encoding
static std::uint32_t countUtf16UnitsForCodePoint(char32_t cp) {
    if (cp < 0x10000) return 1;  // BMP fits in one UTF-16 unit
    if (cp <= 0x10FFFF) return 2;  // Supplementary plane needs surrogate pair
    return 1;  // Invalid, but return something reasonable
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
        // Check if remaining bytes fit in the current offset
        std::size_t remaining = utf8Offset - utf8Pos;
        
        // Handle CRLF: check for \r\n sequence
        if (utf8Pos + 1 < text.size() && 
            text[utf8Pos] == '\r' && text[utf8Pos + 1] == '\n') {
            // Check if CRLF fits within the requested offset
            if (remaining >= 2) {
                pos.line++;
                utf16Col = 0;
                utf8Pos += 2;
            } else {
                // Partial CRLF - stop at offset without counting
                break;
            }
            continue;
        }
        
        unsigned char byte = static_cast<unsigned char>(text[utf8Pos]);
        
        if (byte == '\n') {
            if (remaining >= 1) {
                pos.line++;
                utf16Col = 0;
                utf8Pos++;
            } else {
                break;
            }
        } else if (byte == '\r') {
            // CR not followed by LF - treat as line ending
            if (remaining >= 1) {
                pos.line++;
                utf16Col = 0;
                utf8Pos++;
            } else {
                break;
            }
        } else {
            // Try to decode a valid UTF-8 code point starting from current position
            char32_t codePoint = 0;
            std::size_t oldPos = utf8Pos;
            
            if (decodeUtf8CodePoint(text, utf8Pos, codePoint)) {
                // Successfully decoded - count UTF-16 units for this scalar value
                std::size_t seqLen = utf8Pos - oldPos;
                if (seqLen <= remaining) {
                    // The full sequence fits within the offset
                    utf16Col += countUtf16UnitsForCodePoint(codePoint);
                } else {
                    // The offset falls in the middle of this multi-byte sequence
                    // Report position at the START of this character
                    utf8Pos = oldPos;  // Reset to start of character
                    break;  // Exit loop - we've found our position
                }
            } else {
                // Invalid UTF-8 - treat as single byte
                utf8Pos = oldPos + 1;
                if (utf8Pos <= utf8Offset) {
                    utf16Col += 1;
                } else {
                    utf8Pos = oldPos;
                    break;
                }
            }
        }
    }
    
    pos.character = utf16Col;
    return pos;
}

// Convert UTF-16 position to UTF-8 byte offset
// Returns std::nullopt for invalid positions (e.g., mid-surrogate)
// Valid UTF-16 positions are 0-based code unit indices.
std::optional<std::size_t> LanguageServer::utf16ToUtf8(const std::string& text, std::uint32_t line, std::uint32_t utf16Col) const {
    std::uint32_t currentLine = 0;
    std::uint32_t currentCol = 0;
    std::size_t utf8Offset = 0;
    
    // First, count total lines to validate line number
    // Line endings: \r\n (windows), \n (unix), \r (old mac)
    std::uint32_t totalLines = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            totalLines++;
            i += 2;
        } else if (text[i] == '\n') {
            totalLines++;
            i++;
        } else if (text[i] == '\r') {
            totalLines++;
            i++;
        } else {
            i++;
        }
    }
    totalLines++;  // Account for last line
    
    // Reject out-of-range line number
    if (line >= totalLines) {
        return std::nullopt;
    }
    
    while (utf8Offset < text.size()) {
        // Handle CRLF: check for \r\n sequence
        if (utf8Offset + 1 < text.size() && 
            text[utf8Offset] == '\r' && text[utf8Offset + 1] == '\n') {
            if (currentLine == line) {
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
                return utf8Offset;
            }
            currentLine++;
            currentCol = 0;
            utf8Offset++;
            continue;
        } else if (byte == '\r') {
            if (currentLine == line) {
                return utf8Offset;
            }
            currentLine++;
            currentCol = 0;
            utf8Offset++;
            continue;
        }
        
        if (currentLine == line) {
            // Decode UTF-8 code point to get its UTF-16 length
            char32_t codePoint = 0;
            std::size_t oldPos = utf8Offset;
            if (decodeUtf8CodePoint(text, utf8Offset, codePoint)) {
                std::uint32_t units = countUtf16UnitsForCodePoint(codePoint);
                
                // For characters needing 2 UTF-16 units (surrogate pairs), 
                // reject positions in the middle (e.g., position 1 of a 2-unit character)
                if (units == 2) {
                    if (currentCol < utf16Col && utf16Col < currentCol + units) {
                        return std::nullopt;  // Invalid: in middle of surrogate pair
                    }
                }
                
                // Check if we've reached or passed the target column
                if (currentCol + units <= utf16Col) {
                    if (currentCol + units == utf16Col) {
                        // Position is exactly at end of this character
                        return utf8Offset;
                    }
                    currentCol += units;
                } else {
                    // Position falls within this character
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
            // Not on target line, just skip characters
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
        
    } catch (const std::exception& e) {
        std::string errorMsg = e.what();
        std::size_t line = 1;
        std::size_t column = 1;
        
        // Try to extract line and column from error message
        // Error messages typically contain "at line X" or "line X, column Y"
        std::size_t linePos = errorMsg.find("line ");
        if (linePos != std::string::npos) {
            try {
                std::string numStr = errorMsg.substr(linePos + 5);
                // Find end of number
                std::size_t endPos = numStr.find_first_not_of("0123456789");
                if (endPos != std::string::npos) {
                    line = std::stoul(numStr.substr(0, endPos));
                } else {
                    line = std::stoul(numStr);
                }
            } catch (...) {}
        }
        
        // Try to find column info
        std::size_t colPos = errorMsg.find("column ");
        if (colPos != std::string::npos) {
            try {
                std::string numStr = errorMsg.substr(colPos + 7);
                std::size_t endPos = numStr.find_first_not_of("0123456789");
                if (endPos != std::string::npos) {
                    column = std::stoul(numStr.substr(0, endPos));
                } else {
                    column = std::stoul(numStr);
                }
            } catch (...) {}
        }
        
        // Try to extract the token that caused the error from the message
        // This helps provide better range information
        Position startPos;
        startPos.line = static_cast<std::uint32_t>(line > 0 ? line - 1 : 0);
        startPos.character = static_cast<std::uint32_t>(column > 0 ? column - 1 : 0);
        
        // Try to find a token at or near this position for better range
        try {
            Lexer lexer(doc.text, registry_);
            auto tokens = lexer.tokenize();
            for (const auto& token : tokens) {
                if (token.line == line) {
                    // Found a token on the error line - use its position
                    startPos.character = static_cast<std::uint32_t>(token.column > 0 ? token.column - 1 : 0);
                    // Estimate end based on lexeme length
                    break;
                }
            }
        } catch (...) {}
        
        // Estimate end position - use a reasonable default or token length
        Position endPos = startPos;
        endPos.character = startPos.character + 10; // Default span of 10 characters
        
        Diagnostic diag;
        diag.range = {startPos, endPos};
        diag.severity = 1;  // Error
        diag.message = errorMsg;
        diag.source = "emojineer";
        diagnostics.push_back(diag);
    }
    
    return diagnostics;
}

SourceProvider LanguageServer::createSourceProvider() const {
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

std::vector<Diagnostic> LanguageServer::diagnoseDocumentWithCompile(const OpenDocument& doc) {
    std::vector<Diagnostic> diagnostics;
    
    if (doc.text.empty()) return diagnostics;
    
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
            
        } catch (const std::exception& e) {
            std::string errorMsg = e.what();
            std::size_t line = 1;
            std::size_t column = 1;
            
            // Try to extract line and column from error message
            std::size_t linePos = errorMsg.find("line ");
            if (linePos != std::string::npos) {
                try {
                    std::string numStr = errorMsg.substr(linePos + 5);
                    std::size_t endPos = numStr.find_first_not_of("0123456789");
                    if (endPos != std::string::npos) {
                        line = std::stoul(numStr.substr(0, endPos));
                    } else {
                        line = std::stoul(numStr);
                    }
                } catch (...) {}
            }
            
            // Try to find column info
            std::size_t colPos = errorMsg.find("column ");
            if (colPos != std::string::npos) {
                try {
                    std::string numStr = errorMsg.substr(colPos + 7);
                    std::size_t endPos = numStr.find_first_not_of("0123456789");
                    if (endPos != std::string::npos) {
                        column = std::stoul(numStr.substr(0, endPos));
                    } else {
                        column = std::stoul(numStr);
                    }
                } catch (...) {}
            }
            
            // Try to find token/emoji info
            std::size_t emojiPos = errorMsg.find('\'');
            std::string tokenName;
            if (emojiPos != std::string::npos) {
                auto endEmojiPos = errorMsg.find('\'', emojiPos + 1);
                if (endEmojiPos != std::string::npos) {
                    tokenName = errorMsg.substr(emojiPos + 1, endEmojiPos - emojiPos - 1);
                }
            }
            
            Position startPos;
            startPos.line = static_cast<std::uint32_t>(line > 0 ? line - 1 : 0);
            startPos.character = static_cast<std::uint32_t>(column > 0 ? column - 1 : 0);
            
            // Estimate end position
            Position endPos = startPos;
            endPos.character = startPos.character + (tokenName.empty() ? 10 : tokenName.length());
            
            Diagnostic diag;
            diag.range = {startPos, endPos};
            diag.severity = 1;  // Error
            diag.message = errorMsg;
            diag.source = "emojineer";
            diagnostics.push_back(diag);
        }
    } else {
        // No workspace root - fall back to simple diagnostics
        return diagnoseDocument(doc);
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
        // Get the final change event - server advertises full sync, reject ranged incremental
        const JsonValue& lastChange = arr[arr.size() - 1];
        
        // Check if this is a ranged (incremental) change - reject it for full sync
        auto rangeObj = getJsonObject(lastChange, "range");
        if (!rangeObj.isNull()) {
            // Server advertises full sync - reject incremental changes
            return;
        }
        
        // Get the text field from the change object
        auto textObj = getJsonObject(lastChange, "text");
        std::string text = getJsonString(textObj);
        
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
    std::uint32_t utf16Char = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "character")));
    
    // Convert UTF-16 position to UTF-8 byte offset
    auto utf8OffsetOpt = utf16ToUtf8(doc->text, line, utf16Char);
    std::size_t utf8Offset = utf8OffsetOpt.value_or(0);
    
    // Find the column (byte offset within the line) by scanning from line start
    std::size_t lineStart = 0;
    std::size_t currentLineNum = 0;
    for (std::size_t i = 0; i < doc->text.size(); ++i) {
        if (currentLineNum == line) {
            lineStart = i;
            break;
        }
        // Handle CRLF: check for \r\n sequence
        if (i + 1 < doc->text.size() && doc->text[i] == '\r' && doc->text[i + 1] == '\n') {
            currentLineNum++;
            i++; // Skip the \n as well
        } else if (doc->text[i] == '\n') {
            currentLineNum++;
        } else if (doc->text[i] == '\r') {
            currentLineNum++;
        }
    }
    
    // Column is the byte offset from the start of the line
    std::uint32_t column = 0;
    if (utf8Offset > lineStart) {
        column = static_cast<std::uint32_t>(utf8Offset - lineStart);
    }
    
    auto hover = getHover(doc->uri, Position{line, column});
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
        
        // Find the token at the exact requested position
        // Convert LSP UTF-16 position to UTF-8 offset for comparison
        auto utf8Offset = utf16ToUtf8(doc->text, pos.line, pos.character);
        if (!utf8Offset) return std::nullopt;
        
        for (const auto& token : tokens) {
            if (token.kind == TokenKind::Eof) continue;
            
            // Check if this token contains the requested position
            // token.line is 1-indexed, token.column is 1-indexed
            // We need to check if the position falls within this token's range
            if (token.line == pos.line + 1) {
                // Get token's UTF-8 start position
                auto tokenStartPos = utf8ToUtf16(doc->text, token.byte_offset);
                auto tokenEndPos = utf8ToUtf16(doc->text, token.byte_offset + token.lexeme.size());
                
                // Check if requested position is within this token's range
                // Position is inclusive at start, exclusive at end
                if (pos.character >= tokenStartPos.character && 
                    pos.character < tokenEndPos.character) {
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

JsonValue LanguageServer::handleCompletion(const JsonValue& params) {
    // Extract the text document URI from params
    auto textDoc = getJsonObject(params, "textDocument");
    std::string uri = getJsonString(textDoc);
    
    auto posObj = getJsonObject(params, "position");
    std::uint32_t line = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "line")));
    std::uint32_t char_ = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "character")));
    
    auto completions = getCompletions(uri, Position{line, char_});
    
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
    
    // Language keywords
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
    
    // Core emoji (non-custom) from CER
    for (const auto& def : registry_.definitions()) {
        if (!def.custom) {
            CompletionItem item;
            item.label = def.alias;
            item.detail = def.description;
            completions.push_back(item);
        }
    }
    
    // Custom CER tokens
    for (const auto& def : registry_.definitions()) {
        if (def.custom) {
            CompletionItem item;
            item.label = def.alias;
            item.detail = "Custom CER: " + def.description;
            completions.push_back(item);
        }
    }
    
    // Standard library modules
    for (const auto& module : standard_modules()) {
        CompletionItem item;
        item.label = std::string(module.specifier);
        item.detail = std::string(module.description);
        completions.push_back(item);
    }
    
    // Get the document for user symbols and local context
    auto doc = getDocument(uri);
    if (doc) {
        try {
            // Lex to find user-defined symbols in current document
            Lexer lexer(doc->text, registry_);
            auto tokens = lexer.tokenize();
            
            std::unordered_set<std::string> seen;
            
            // Collect user-defined identifiers
            for (const auto& token : tokens) {
                if (token.kind == TokenKind::Identifier) {
                    if (seen.insert(token.lexeme).second) {
                        CompletionItem item;
                        item.label = token.lexeme;
                        item.detail = "user symbol";
                        completions.push_back(item);
                    }
                }
            }
            
            // Parse to find function declarations
            Parser parser(std::move(tokens));
            auto program = parser.parse();
            
            for (const auto& stmt : program.statements) {
                if (auto* func = dynamic_cast<ast::FunctionDecl*>(stmt.get())) {
                    if (seen.insert(func->name).second) {
                        CompletionItem item;
                        item.label = func->name;
                        item.detail = "function";
                        completions.push_back(item);
                    }
                } else if (auto* var = dynamic_cast<ast::VarDecl*>(stmt.get())) {
                    if (seen.insert(var->name).second) {
                        CompletionItem item;
                        item.label = var->name;
                        item.detail = "variable";
                        completions.push_back(item);
                    }
                }
            }
        } catch (...) {}
    }
    
    // If we have workspace, add local modules and direct packages
    if (workspaceRoot_) {
        // Local .emoji modules in workspace
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(*workspaceRoot_)) {
                if (entry.is_regular_file() && entry.path().extension() == ".emoji") {
                    std::string moduleName = entry.path().filename().stem().string();
                    // Only add modules that are not hidden or in hidden directories
                    if (!moduleName.empty() && moduleName[0] != '.') {
                        CompletionItem item;
                        item.label = "./" + entry.path().lexically_relative(*workspaceRoot_).string();
                        item.detail = "local module";
                        completions.push_back(item);
                    }
                }
            }
        } catch (...) {}
        
        // Direct path packages (from manifest if available)
        if (manifest_) {
            for (const auto& dep : manifest_->dependencies) {
                CompletionItem item;
                item.label = "pkg:" + dep.name + "/";
                item.detail = "direct package";
                completions.push_back(item);
            }
        }
        
        // Materialized direct registry packages from lock file
        if (lock_) {
            for (const auto& pkg : lock_->packages) {
                CompletionItem item;
                item.label = "pkg:" + pkg.name + "/";
                item.detail = "materialized package";
                completions.push_back(item);
            }
        }
    }
    
    return completions;
}

JsonValue LanguageServer::handleDefinition(const JsonValue& params) {
    // Extract the text document URI
    auto textDoc = getJsonObject(params, "textDocument");
    std::string uri = getJsonString(textDoc);
    
    auto doc = getDocument(uri);
    if (!doc) return json::makeArray();
    
    auto posObj = getJsonObject(params, "position");
    std::uint32_t line = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "line")));
    std::uint32_t utf16Char = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "character")));
    
    // Convert UTF-16 position to UTF-8 byte offset
    auto utf8OffsetOpt = utf16ToUtf8(doc->text, line, utf16Char);
    std::size_t utf8Offset = utf8OffsetOpt.value_or(0);
    
    // Find the column (byte offset within the line) by scanning from line start
    std::size_t lineStart = 0;
    std::size_t currentLineNum = 0;
    for (std::size_t i = 0; i < doc->text.size(); ++i) {
        if (currentLineNum == line) {
            lineStart = i;
            break;
        }
        // Handle CRLF: check for \r\n sequence
        if (i + 1 < doc->text.size() && doc->text[i] == '\r' && doc->text[i + 1] == '\n') {
            currentLineNum++;
            i++; // Skip the \n as well
        } else if (doc->text[i] == '\n') {
            currentLineNum++;
        } else if (doc->text[i] == '\r') {
            currentLineNum++;
        }
    }
    
    std::uint32_t column = 0;
    if (utf8Offset > lineStart) {
        column = static_cast<std::uint32_t>(utf8Offset - lineStart);
    }
    
    auto defs = findDefinitions(uri, Position{line, column});
    
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
    std::vector<SymbolLocation> results;
    
    auto doc = getDocument(uri);
    if (!doc) return results;
    
    try {
        Lexer lexer(doc->text, registry_);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens));
        auto program = parser.parse();
        
        // Find the token at the given position
        const Token* tokenAtPos = nullptr;
        for (const auto& token : tokens) {
            if (token.kind == TokenKind::Eof) continue;
            
            // Check if this token is at the requested position
            // Position is 0-indexed in LSP
            if (token.line == pos.line + 1) {
                // Simple column matching - this is approximate
                if (token.column <= pos.character + 1 && 
                    token.column + token.lexeme.size() >= pos.character + 1) {
                    tokenAtPos = &token;
                    break;
                }
            }
        }
        
        if (!tokenAtPos) return results;
        
        // Now search for the definition of this identifier
        // In Emojineer, definitions come before uses, so we search backwards in the program
        for (const auto& stmt : program.statements) {
            if (!stmt) continue;
            
            const std::type_info& typeInfo = typeid(*stmt);
            
            // Check for variable declarations
            if (typeInfo == typeid(ast::VarDecl)) {
                auto* varDecl = dynamic_cast<ast::VarDecl*>(stmt.get());
                if (varDecl && varDecl->name == tokenAtPos->lexeme) {
                    SymbolLocation loc;
                    loc.uri = uri;
                    loc.name = varDecl->name;
                    loc.symbolKind = "variable";
                    loc.range.start.line = static_cast<std::uint32_t>(stmt->line - 1);
                    loc.range.start.character = 0;
                    loc.range.end.line = static_cast<std::uint32_t>(stmt->line - 1);
                    loc.range.end.character = static_cast<std::uint32_t>(varDecl->name.length());
                    results.push_back(loc);
                    return results;
                }
            }
            
            // Check for function declarations
            if (typeInfo == typeid(ast::FunctionDecl)) {
                auto* funcDecl = dynamic_cast<ast::FunctionDecl*>(stmt.get());
                if (funcDecl && funcDecl->name == tokenAtPos->lexeme) {
                    SymbolLocation loc;
                    loc.uri = uri;
                    loc.name = funcDecl->name;
                    loc.symbolKind = "function";
                    loc.range.start.line = static_cast<std::uint32_t>(stmt->line - 1);
                    loc.range.start.character = 0;
                    loc.range.end.line = static_cast<std::uint32_t>(stmt->line - 1);
                    loc.range.end.character = static_cast<std::uint32_t>(funcDecl->name.length());
                    results.push_back(loc);
                    return results;
                }
            }
        }
        
    } catch (const std::exception&) {
        // Return empty results on error
    }
    
    return results;
}

std::vector<SymbolLocation> LanguageServer::findReferences(const std::string& uri, const Position& pos) {
    std::vector<SymbolLocation> results;
    
    // First find what symbol is at the position
    auto defs = findDefinitions(uri, pos);
    if (defs.empty()) return results;
    
    const std::string& symbolName = defs[0].name;
    
    auto doc = getDocument(uri);
    if (!doc) return results;
    
    try {
        Lexer lexer(doc->text, registry_);
        auto tokens = lexer.tokenize();
        
        // Find all uses of this symbol
        for (const auto& token : tokens) {
            if (token.kind == TokenKind::Eof) continue;
            
            // Look for identifier tokens that match our symbol name
            if (token.kind == TokenKind::Identifier && token.lexeme == symbolName) {
                // Skip the definition itself (we'll have already found it via findDefinitions)
                bool isDefinition = false;
                for (const auto& def : defs) {
                    if (def.range.start.line == token.line - 1) {
                        isDefinition = true;
                        break;
                    }
                }
                
                if (!isDefinition) {
                    SymbolLocation loc;
                    loc.uri = uri;
                    loc.name = token.lexeme;
                    loc.symbolKind = defs[0].symbolKind;
                    loc.range.start.line = static_cast<std::uint32_t>(token.line - 1);
                    loc.range.start.character = static_cast<std::uint32_t>(token.column > 0 ? token.column - 1 : 0);
                    loc.range.end.line = static_cast<std::uint32_t>(token.line - 1);
                    loc.range.end.character = loc.range.start.character + static_cast<std::uint32_t>(token.lexeme.length());
                    results.push_back(loc);
                }
            }
        }
        
    } catch (const std::exception&) {
        // Return what we have on error
    }
    
    return results;
}

JsonValue LanguageServer::handleReferences(const JsonValue& params) {
    // Extract the text document URI
    auto textDoc = getJsonObject(params, "textDocument");
    std::string uri = getJsonString(textDoc);
    
    auto doc = getDocument(uri);
    if (!doc) return json::makeArray();
    
    auto posObj = getJsonObject(params, "position");
    std::uint32_t line = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "line")));
    std::uint32_t utf16Char = static_cast<std::uint32_t>(getJsonNumber(getJsonObject(posObj, "character")));
    
    // Convert UTF-16 position to UTF-8 byte offset
    auto utf8OffsetOpt = utf16ToUtf8(doc->text, line, utf16Char);
    std::size_t utf8Offset = utf8OffsetOpt.value_or(0);
    
    // Find the column (byte offset within the line) by scanning from line start
    std::size_t lineStart = 0;
    std::size_t currentLineNum = 0;
    for (std::size_t i = 0; i < doc->text.size(); ++i) {
        if (currentLineNum == line) {
            lineStart = i;
            break;
        }
        // Handle CRLF: check for \r\n sequence
        if (i + 1 < doc->text.size() && doc->text[i] == '\r' && doc->text[i + 1] == '\n') {
            currentLineNum++;
            i++; // Skip the \n as well
        } else if (doc->text[i] == '\n') {
            currentLineNum++;
        } else if (doc->text[i] == '\r') {
            currentLineNum++;
        }
    }
    
    std::uint32_t column = 0;
    if (utf8Offset > lineStart) {
        column = static_cast<std::uint32_t>(utf8Offset - lineStart);
    }
    
    auto refs = findReferences(uri, Position{line, column});
    
    auto result = json::makeArray();
    for (const auto& ref : refs) {
        auto loc = json::makeObject();
        json::objectSet(loc, "uri", JsonValue(ref.uri));
        
        auto range = json::makeObject();
        auto start = json::makeObject();
        json::objectSet(start, "line", JsonValue(static_cast<double>(ref.range.start.line)));
        json::objectSet(start, "character", JsonValue(static_cast<double>(ref.range.start.character)));
        auto end = json::makeObject();
        json::objectSet(end, "line", JsonValue(static_cast<double>(ref.range.end.line)));
        json::objectSet(end, "character", JsonValue(static_cast<double>(ref.range.end.character)));
        json::objectSet(range, "start", start);
        json::objectSet(range, "end", end);
        json::objectSet(loc, "range", range);
        
        json::arrayPushBack(result, loc);
    }
    
    return result;
}

JsonValue LanguageServer::handleDocumentSymbol(const JsonValue& params) {
    // Extract the text document URI
    auto textDoc = getJsonObject(params, "textDocument");
    std::string uri = getJsonString(textDoc);
    
    auto symbols = getDocumentSymbols(uri);
    
    // Convert DocumentSymbol to JSON
    auto result = json::makeArray();
    for (const auto& sym : symbols) {
        auto symJson = json::makeObject();
        json::objectSet(symJson, "name", JsonValue(sym.name));
        if (sym.kind) json::objectSet(symJson, "kind", JsonValue(static_cast<double>(*sym.kind)));
        
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
        
        auto selectionRange = json::makeObject();
        auto selStart = json::makeObject();
        json::objectSet(selStart, "line", JsonValue(static_cast<double>(sym.selectionRange.start.line)));
        json::objectSet(selStart, "character", JsonValue(static_cast<double>(sym.selectionRange.start.character)));
        auto selEnd = json::makeObject();
        json::objectSet(selEnd, "line", JsonValue(static_cast<double>(sym.selectionRange.end.line)));
        json::objectSet(selEnd, "character", JsonValue(static_cast<double>(sym.selectionRange.end.character)));
        json::objectSet(selectionRange, "start", selStart);
        json::objectSet(selectionRange, "end", selEnd);
        json::objectSet(symJson, "selectionRange", selectionRange);
        
        if (sym.detail) json::objectSet(symJson, "detail", JsonValue(*sym.detail));
        
        // Handle children recursively
        if (!sym.children.empty()) {
            auto children = json::makeArray();
            for (const auto& child : sym.children) {
                // Recursively convert child symbols - simplified for now
                auto childJson = json::makeObject();
                json::objectSet(childJson, "name", JsonValue(child.name));
                json::arrayPushBack(children, childJson);
            }
            json::objectSet(symJson, "children", children);
        }
        
        json::arrayPushBack(result, symJson);
    }
    
    return result;
}

std::vector<DocumentSymbol> LanguageServer::getDocumentSymbols(const std::string& uri) {
    std::vector<DocumentSymbol> symbols;
    
    auto doc = getDocument(uri);
    if (!doc) return symbols;
    
    try {
        Lexer lexer(doc->text, registry_);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens));
        auto program = parser.parse();
        
        // Extract symbols from the AST with actual emoji names
        for (const auto& stmt : program.statements) {
            if (!stmt) continue;
            
            // Use the statement's line number for position
            auto line = stmt->line;
            
            DocumentSymbol sym;
            sym.range.start.line = static_cast<std::uint32_t>(line - 1);
            sym.range.start.character = 0;
            sym.range.end.line = static_cast<std::uint32_t>(line - 1);
            sym.range.end.character = 80; // Approximate
            sym.selectionRange = sym.range;
            
            // Use dynamic_cast to get actual names
            if (auto* funcDecl = dynamic_cast<ast::FunctionDecl*>(stmt.get())) {
                sym.name = funcDecl->name;
                sym.kind = static_cast<int>(SymbolKind::Function);
                sym.detail = "function declaration";
            } else if (auto* varDecl = dynamic_cast<ast::VarDecl*>(stmt.get())) {
                sym.name = varDecl->name;
                sym.kind = static_cast<int>(SymbolKind::Variable);
                sym.detail = "variable declaration";
            } else if (auto* modDecl = dynamic_cast<ast::ModuleDecl*>(stmt.get())) {
                sym.name = modDecl->name;
                sym.kind = static_cast<int>(SymbolKind::Module);
                sym.detail = "module declaration";
            } else if (auto* importStmt = dynamic_cast<ast::ImportStmt*>(stmt.get())) {
                sym.name = importStmt->specifier;
                sym.kind = static_cast<int>(SymbolKind::Module);
                sym.detail = "import statement";
            } else if (auto* exportStmt = dynamic_cast<ast::ExportStmt*>(stmt.get())) {
                sym.name = exportStmt->specifier;
                sym.kind = static_cast<int>(SymbolKind::Module);
                sym.detail = "export statement";
            } else {
                // Skip other statement types
                continue;
            }
            
            symbols.push_back(sym);
        }
        
    } catch (const std::exception&) {
        // Return whatever symbols we found before the error
    }
    
    return symbols;
}

JsonValue LanguageServer::handleWorkspaceSymbol(const JsonValue& params) {
    std::string query = getJsonString(getJsonObject(params, "query"));
    
    auto symbols = getWorkspaceSymbols(query);
    
    auto result = json::makeArray();
    for (const auto& sym : symbols) {
        auto symJson = json::makeObject();
        json::objectSet(symJson, "name", JsonValue(sym.name));
        if (sym.kind) json::objectSet(symJson, "kind", JsonValue(static_cast<double>(*sym.kind)));
        
        auto location = json::makeObject();
        json::objectSet(location, "uri", JsonValue(sym.location.uri));
        
        auto range = json::makeObject();
        auto start = json::makeObject();
        json::objectSet(start, "line", JsonValue(static_cast<double>(sym.location.range.start.line)));
        json::objectSet(start, "character", JsonValue(static_cast<double>(sym.location.range.start.character)));
        auto end = json::makeObject();
        json::objectSet(end, "line", JsonValue(static_cast<double>(sym.location.range.end.line)));
        json::objectSet(end, "character", JsonValue(static_cast<double>(sym.location.range.end.character)));
        json::objectSet(range, "start", start);
        json::objectSet(range, "end", end);
        json::objectSet(location, "range", range);
        
        json::objectSet(symJson, "location", location);
        if (sym.containerName.has_value()) {
            json::objectSet(symJson, "containerName", JsonValue(*sym.containerName));
        }
        
        json::arrayPushBack(result, symJson);
    }
    
    return result;
}

std::vector<SymbolInformation> LanguageServer::getWorkspaceSymbols(const std::string& query) {
    std::vector<SymbolInformation> symbols;
    
    if (!workspaceRoot_) return symbols;
    
    // Search through open documents first
    for (const auto& [uri, doc] : openDocuments_) {
        auto docSymbols = getDocumentSymbols(uri);
        for (const auto& docSym : docSymbols) {
            // Filter by query if provided
            if (query.empty() || docSym.name.find(query) != std::string::npos) {
                SymbolInformation info;
                info.name = docSym.name;
                info.kind = docSym.kind;
                info.location.uri = uri;
                info.location.range = docSym.range;
                symbols.push_back(info);
            }
        }
    }
    
    // Also search through local module files in the workspace
    if (workspaceRoot_ && std::filesystem::exists(*workspaceRoot_)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(*workspaceRoot_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".emoji") {
                std::string filePath = entry.path().string();
                
                // Try to read and parse the file
                try {
                    std::ifstream file(filePath);
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    std::string source = buffer.str();
                    
                    // Parse and extract symbols
                    Lexer lexer(source, registry_);
                    auto tokens = lexer.tokenize();
                    Parser parser(std::move(tokens));
                    auto program = parser.parse();
                    
                    for (const auto& stmt : program.statements) {
                        if (!stmt) continue;
                        
                        SymbolInformation info;
                        info.location.uri = pathToUri(entry.path());
                        info.location.range.start.line = static_cast<std::uint32_t>(stmt->line - 1);
                        info.location.range.start.character = 0;
                        info.location.range.end.line = static_cast<std::uint32_t>(stmt->line - 1);
                        info.location.range.end.character = 80;
                        
                        // Use dynamic_cast to get actual names
                        if (auto* funcDecl = dynamic_cast<ast::FunctionDecl*>(stmt.get())) {
                            info.name = funcDecl->name;
                            info.kind = static_cast<int>(SymbolKind::Function);
                        } else if (auto* varDecl = dynamic_cast<ast::VarDecl*>(stmt.get())) {
                            info.name = varDecl->name;
                            info.kind = static_cast<int>(SymbolKind::Variable);
                        } else if (auto* modDecl = dynamic_cast<ast::ModuleDecl*>(stmt.get())) {
                            info.name = modDecl->name;
                            info.kind = static_cast<int>(SymbolKind::Module);
                        } else if (auto* importStmt = dynamic_cast<ast::ImportStmt*>(stmt.get())) {
                            info.name = importStmt->specifier;
                            info.kind = static_cast<int>(SymbolKind::Module);
                        } else if (auto* exportStmt = dynamic_cast<ast::ExportStmt*>(stmt.get())) {
                            info.name = exportStmt->specifier;
                            info.kind = static_cast<int>(SymbolKind::Module);
                        } else {
                            continue;
                        }
                        
                        // Filter by query if provided
                        if (query.empty() || info.name.find(query) != std::string::npos) {
                            symbols.push_back(info);
                        }
                    }
                } catch (const std::exception&) {
                    // Skip files that can't be parsed
                }
            }
        }
    }
    
    return symbols;
}

JsonValue LanguageServer::handleFormatting(const JsonValue& params) {
    // Extract the text document
    auto textDoc = getJsonObject(params, "textDocument");
    std::string uri = getJsonString(textDoc);
    
    auto doc = getDocument(uri);
    if (!doc) return json::makeArray();
    
    try {
        // Use the canonical formatter
        std::string formatted = emojineer::format_source(doc->text, registry_);
        
        // Create a TextEdit for the full document
        auto result = json::makeArray();
        auto edit = json::makeObject();
        
        // Full range from start to actual end of document
        auto range = json::makeObject();
        auto start = json::makeObject();
        json::objectSet(start, "line", JsonValue(0.0));
        json::objectSet(start, "character", JsonValue(0.0));
        
        auto end = json::makeObject();
        
        // Calculate actual end position: find last line and its length
        std::size_t lastLine = 0;
        std::size_t lastLineStart = 0;
        for (std::size_t i = 0; i < doc->text.size(); i++) {
            if (doc->text[i] == '\n') {
                lastLine++;
                lastLineStart = i + 1;
            }
        }
        
        // Calculate character position at end (UTF-16 column)
        // If document is empty, end is at 0,0
        if (doc->text.empty()) {
            json::objectSet(end, "line", JsonValue(0.0));
            json::objectSet(end, "character", JsonValue(0.0));
        } else {
            json::objectSet(end, "line", JsonValue(static_cast<double>(lastLine)));
            // Calculate UTF-16 position at end of document
            std::string lastLineContent = doc->text.substr(lastLineStart);
            Position endPos = utf8ToUtf16(doc->text, doc->text.size());
            json::objectSet(end, "character", JsonValue(static_cast<double>(endPos.character)));
        }
        
        json::objectSet(range, "start", start);
        json::objectSet(range, "end", end);
        json::objectSet(edit, "range", range);
        json::objectSet(edit, "newText", JsonValue(formatted));
        
        json::arrayPushBack(result, edit);
        return result;
    } catch (const std::exception& e) {
        // Return empty array on error
        return json::makeArray();
    }
}

JsonValue LanguageServer::handleRangeFormatting(const JsonValue& params) {
    // Extract the text document and range
    auto textDoc = getJsonObject(params, "textDocument");
    std::string uri = getJsonString(textDoc);
    
    auto doc = getDocument(uri);
    if (!doc) return json::makeArray();
    
    // Check if range is provided
    auto rangeObj = getJsonObject(params, "range");
    if (rangeObj.isNull()) {
        // No range provided, fall back to full document formatting
        return handleFormatting(params);
    }
    
    // Currently, we only support full document formatting
    // Range formatting would require the formatter to support partial formatting
    // Since the canonical formatter works on the full document, we fall back to full doc
    // TODO: Implement true range formatting using formatter's internal APIs
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
        // Check for invalid request: requests after shutdown (except exit)
        if (shutdown_ && method != "exit") {
            if (stringId) {
                std::cout << formatJsonRpcErrorStringId(-32600, "Invalid request: cannot handle requests after shutdown", *stringId);
            } else {
                std::cout << formatJsonRpcError(-32600, "Invalid request: cannot handle requests after shutdown", intId);
            }
            std::cout.flush();
            return;
        }
        
        // Check for unknown method - return method not found error
        bool knownMethod = (method == "initialize" || method == "shutdown" ||
                          method == "textDocument/hover" || method == "textDocument/completion" ||
                          method == "textDocument/definition" || method == "textDocument/references" ||
                          method == "textDocument/documentSymbol" || method == "workspace/symbol" ||
                          method == "textDocument/formatting" || method == "textDocument/rangeFormatting");
        
        if (!knownMethod) {
            if (stringId) {
                std::cout << formatJsonRpcErrorStringId(-32601, "Method not found", *stringId);
            } else {
                std::cout << formatJsonRpcError(-32601, "Method not found", intId);
            }
            std::cout.flush();
            return;
        }
        
        try {
            auto result = handleRequest(method, params);
            if (stringId) {
                // Use string ID format
                std::cout << formatJsonRpcResponseStringId(result, *stringId);
            } else {
                std::cout << formatJsonRpcResponse(result, intId);
            }
            std::cout.flush();
        } catch (const std::exception& e) {
            if (stringId) {
                std::cout << formatJsonRpcErrorStringId(-32603, e.what(), *stringId);
            } else {
                std::cout << formatJsonRpcError(-32603, e.what(), intId);
            }
            std::cout.flush();
        }
    } else {
        handleNotification(method, params);
    }
}

// Helper function to send a JSON-RPC error response for malformed requests
static void sendMalformedJsonResponse(const std::string& errorMsg) {
    auto obj = json::makeObject();
    json::objectSet(obj, "jsonrpc", JsonValue(std::string("2.0")));
    
    auto error = json::makeObject();
    json::objectSet(error, "code", JsonValue(static_cast<double>(-32700)));  // Parse error
    json::objectSet(error, "message", JsonValue(errorMsg));
    json::objectSet(obj, "error", error);
    
    std::string body = toJson(obj);
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
}

int LanguageServer::run() {
    std::string buffer;
    
    // Set stdin to binary mode to read exact bytes
    std::cin >> std::noskipws;
    
    while (true) {
        // Read headers
        std::string headers;
        char ch;
        
        // Read until we find the end of headers (blank line)
        while (std::cin.get(ch)) {
            headers += ch;
            if (headers.size() >= 4 && 
                headers.substr(headers.size() - 4) == "\r\n\r\n") {
                break;
            }
        }
        
        if (headers.empty() && std::cin.eof()) {
            break;
        }
        
        // Parse Content-Length
        size_t contentLength = 0;
        size_t pos = headers.find("Content-Length:");
        if (pos != std::string::npos) {
            pos += 15;  // Length of "Content-Length:"
            // Skip whitespace
            while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) pos++;
            // Read digits
            size_t endPos = pos;
            while (endPos < headers.size() && headers[endPos] >= '0' && headers[endPos] <= '9') {
                endPos++;
            }
            if (endPos > pos) {
                contentLength = std::stoul(headers.substr(pos, endPos - pos));
            }
        }
        
        if (contentLength == 0) {
            // Try to parse as a raw JSON message (for testing without headers)
            if (!buffer.empty()) {
                try {
                    auto json = parseJson(buffer);
                    handleMessage(json);
                } catch (const std::exception& e) {
                    sendMalformedJsonResponse(e.what());
                }
                buffer.clear();
            }
            continue;
        }
        
        // Read exactly contentLength bytes
        std::string body;
        body.reserve(contentLength);
        while (body.size() < contentLength) {
            if (!std::cin.get(ch)) {
                break;
            }
            body += ch;
        }
        
        // Process the message
        try {
            auto json = parseJson(body);
            handleMessage(json);
            std::cout.flush();
        } catch (const std::exception& e) {
            sendMalformedJsonResponse(e.what());
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
