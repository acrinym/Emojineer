#pragma once

#include "emojineer/cer.hpp"
#include "emojineer/module.hpp"
#include "emojineer/project.hpp"
#include "emojineer/source_diagnostic.hpp"

#include <any>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace emojineer {

// Forward declarations for ast
namespace ast {
struct Program;
}

namespace lsp {

// Forward declarations
struct Range;
struct Position;

// LSP JSON-RPC types - define Position first
struct Position {
    std::uint32_t line{0};
    std::uint32_t character{0};
};

struct Range {
    Position start;
    Position end;
};

// JsonValue - using std::any for recursive types

struct JsonValue {
    std::any value;
    
    JsonValue() = default;
    JsonValue(std::nullptr_t v) : value(v) {}
    JsonValue(bool v) : value(v) {}
    JsonValue(double v) : value(v) {}
    JsonValue(int v) : value(static_cast<double>(v)) {}  // Integers stored as double
    JsonValue(unsigned int v) : value(static_cast<double>(v)) {}  // Unsigned integers
    JsonValue(const std::string& v) : value(v) {}
    JsonValue(const char* v) : value(std::string(v)) {}
    
    template<typename T>
    JsonValue(const std::vector<T>& v) : value(v) {}
    
    template<typename T>
    JsonValue(const std::unordered_map<std::string, T>& v) : value(v) {}
    
    bool isNull() const { return value.type() == typeid(nullptr); }
    bool isBool() const { return value.type() == typeid(bool); }
    bool isNumber() const { return value.type() == typeid(double); }
    bool isString() const { return value.type() == typeid(std::string); }
    bool isArray() const { return value.type() == typeid(std::vector<JsonValue>); }
    bool isObject() const { return value.type() == typeid(std::unordered_map<std::string, JsonValue>); }
    
    template<typename T>
    T get() const { return std::any_cast<T>(value); }
    
    template<typename T>
    const T* getPtr() const { return std::any_cast<T>(&value); }
    
    template<typename T>
    T* getPtr() { return std::any_cast<T>(&value); }
};

// Helper functions for JsonValue
namespace json {
    inline JsonValue makeArray() {
        return JsonValue(std::vector<JsonValue>{});
    }
    
    inline JsonValue makeObject() {
        return JsonValue(std::unordered_map<std::string, JsonValue>{});
    }
    
    inline void arrayPushBack(JsonValue& arr, const JsonValue& item) {
        auto* vec = arr.getPtr<std::vector<JsonValue>>();
        if (vec) vec->push_back(item);
    }
    
    inline void objectSet(JsonValue& obj, const std::string& key, const JsonValue& item) {
        auto* map = obj.getPtr<std::unordered_map<std::string, JsonValue>>();
        if (map) (*map)[key] = item;
    }
}

struct TextDocumentPositionParams {
    std::string uri;
    Position position;
};

struct Location {
    std::string uri;
    Range range;
};

struct TextDocumentItem {
    std::string uri;
    std::string languageId;
    int version{0};
    std::string text;
};

struct TextDocumentIdentifier {
    std::string uri;
};

struct VersionedTextDocumentIdentifier {
    std::string uri;
    int version{0};
};

struct TextEdit {
    Range range;
    std::string newText;
};

struct TextDocumentEdit {
    VersionedTextDocumentIdentifier textDocument;
    std::vector<TextEdit> edits;
};

struct Diagnostic {
    Range range;
    int severity{0};  // 1=Error, 2=Warning, 3=Info, 4=Hint (LSP enum)
    std::string message;
    std::optional<std::string> source;
};

struct MarkupContent {
    std::string kind;  // "markdown" or "plaintext"
    std::string value;
};

struct Hover {
    std::optional<MarkupContent> contents;
    std::optional<Range> range;
};

// LSP CompletionItemKind enum (numeric)
enum class CompletionItemKind : int {
    Text = 1,
    Method = 2,
    Function = 3,
    Constructor = 4,
    Field = 5,
    Variable = 6,
    Class = 7,
    Interface = 8,
    Module = 9,
    Property = 10,
    Unit = 11,
    Value = 12,
    Enum = 13,
    Keyword = 14,
    Snippet = 15,
    Color = 16,
    File = 17,
    Reference = 18,
    Folder = 19,
    EnumMember = 20,
    Constant = 21,
    Struct = 22,
    Event = 23,
    Operator = 24,
    TypeParameter = 25
};

// LSP SymbolKind enum (numeric)
enum class SymbolKind : int {
    File = 1,
    Module = 2,
    Namespace = 3,
    Package = 4,
    Class = 5,
    Method = 6,
    Property = 7,
    Field = 8,
    Constructor = 9,
    Function = 10,
    Variable = 11,
    Constant = 12,
    String = 13,
    Number = 14,
    Boolean = 15,
    Array = 16,
    Object = 17,
    Key = 18,
    Null = 19,
    EnumMember = 20,
    Struct = 21,
    Event = 22,
    Operator = 23,
    TypeParameter = 24
};

struct CompletionItem {
    std::string label;
    std::optional<int> kind;  // Numeric LSP CompletionItemKind
    std::optional<std::string> detail;
    std::optional<std::string> documentation;
    std::optional<std::string> insertText;
    std::optional<std::string> filterText;
};

struct CompletionList {
    bool isIncomplete{false};
    std::vector<CompletionItem> items;
};

struct SymbolInformation {
    std::string name;
    std::optional<int> kind;  // Numeric LSP SymbolKind
    Location location;
    std::optional<std::string> containerName;
};

struct DocumentSymbol {
    std::string name;
    std::optional<int> kind;  // Numeric LSP SymbolKind
    Range range;
    Range selectionRange;
    std::optional<std::string> detail;
    std::vector<DocumentSymbol> children;
};

struct ReferenceContext {
    bool includeDeclaration{false};
};

struct ParameterInformation {
    std::optional<std::string> label;
    std::optional<std::string> documentation;
};

struct SignatureInformation {
    std::optional<std::string> label;
    std::optional<std::string> documentation;
    std::optional<std::vector<ParameterInformation>> parameters;
};

struct SignatureHelp {
    std::vector<SignatureInformation> signatures;
    int activeSignature{0};
    int activeParameter{0};
};

struct ServerCapabilities {
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

struct InitializeResult {
    std::string protocolVersion{"3.17.0"};
    ServerCapabilities capabilities;
};

// In-memory document overlay
struct OpenDocument {
    std::string uri;
    std::string path;
    int version{0};
    std::string text;
    std::vector<Diagnostic> diagnostics;
};

struct DiagnosticResult {
    std::string primaryUri;
    std::unordered_map<std::string, std::vector<Diagnostic>> diagnosticsByUri;
};

// Symbol location for definitions/references
struct SymbolLocation {
    std::string uri;
    Range range;
    std::string name;
    std::string symbolKind;  // "function", "variable", "module", etc.
};

// Backward compatibility alias - SourceLocationException is now in source_diagnostic.hpp
// This allows existing code that uses lsp::SourceLocationException to continue working
using SourceLocationException = ::emojineer::SourceLocationException;

// LSP server main class
class LanguageServer {
public:
    LanguageServer();
    ~LanguageServer();

    // Run the LSP server (reads from stdin, writes to stdout)
    int run();

private:
    // JSON-RPC message handling
    void handleMessage(const JsonValue& message);
    JsonValue handleRequest(const std::string& method, const JsonValue& params);
    void handleNotification(const std::string& method, const JsonValue& params);

    // LSP method handlers
    JsonValue handleInitialize(const JsonValue& params);
    JsonValue handleShutdown(const JsonValue& params);
    void handleExit(const JsonValue& params);
    void handleDidOpenTextDocument(const JsonValue& params);
    void handleDidChangeTextDocument(const JsonValue& params);
    void handleDidSaveTextDocument(const JsonValue& params);
    void handleDidCloseTextDocument(const JsonValue& params);
    void handleInitialized(const JsonValue& params);

    // Feature handlers
    JsonValue handleHover(const JsonValue& params);
    JsonValue handleCompletion(const JsonValue& params);
    JsonValue handleDefinition(const JsonValue& params);
    JsonValue handleReferences(const JsonValue& params);
    JsonValue handleDocumentSymbol(const JsonValue& params);
    JsonValue handleWorkspaceSymbol(const JsonValue& params);
    JsonValue handleFormatting(const JsonValue& params);
    JsonValue handleRangeFormatting(const JsonValue& params);

    // Document management
    void openDocument(const std::string& uri, const std::string& text, int version);
    void updateDocument(const std::string& uri, const std::string& text, int version);
    void saveDocument(const std::string& uri);
    void closeDocument(const std::string& uri);
    std::optional<OpenDocument> getDocument(const std::string& uri) const;
    std::string uriToPath(const std::string& uri) const;
    std::string pathToUri(const std::filesystem::path& path) const;
    
    // Source access (hybrid: overlay first, then filesystem)
    std::optional<std::string> getSource(const std::string& uri) const;

    // Position conversion (UTF-16 <-> UTF-8/grapheme)
    Position utf8ToUtf16(const std::string& text, std::size_t utf8Offset) const;
    std::optional<std::size_t> utf16ToUtf8(const std::string& text, std::uint32_t line, std::uint32_t utf16Col) const;

    // Canonical token-to-range conversion: converts a Token's 1-based grapheme
    // line/column + lexeme into an exact LSP UTF-16 Range against original source.
    // This is the ONE authoritative way to convert token positions to LSP ranges.
    Range tokenToRange(const std::string& sourceText, const Token& token) const;

    // Diagnostics
    std::vector<Diagnostic> diagnoseDocument(const OpenDocument& doc);
    DiagnosticResult diagnoseDocumentWithCompile(const OpenDocument& doc);
    void publishDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics);
    
    // Create a SourceProvider that checks open documents first
    // Uses ::emojineer::SourceProvider (the callback type from module.hpp)
    ::emojineer::SourceProvider createSourceProvider() const;

    // Workspace management
    void discoverWorkspace(const std::filesystem::path& root);
    std::optional<ProjectManifest> getProjectManifest(const std::filesystem::path& path) const;
    std::optional<ProjectLock> getProjectLock(const std::filesystem::path& path) const;

    // Symbol resolution
    std::vector<SymbolLocation> findDefinitions(const std::string& uri, const Position& pos);
    std::vector<SymbolLocation> findReferences(const std::string& uri, const Position& pos);
    std::vector<DocumentSymbol> getDocumentSymbols(const std::string& uri);
    std::vector<SymbolInformation> getWorkspaceSymbols(const std::string& query);

    // Completion items
    std::vector<CompletionItem> getCompletions(const std::string& uri, const Position& pos);

    // Hover information
    std::optional<Hover> getHover(const std::string& uri, const Position& pos);

    // State
    bool initialized_{false};
    bool shutdown_{false};
    std::optional<InitializeResult> serverInfo_;
    
    // Document overlays
    std::unordered_map<std::string, OpenDocument> openDocuments_;
    
    // Workspace state
    std::optional<std::filesystem::path> workspaceRoot_;
    std::optional<ProjectManifest> manifest_;
    std::optional<ProjectLock> lock_;
    std::optional<PackageStore> packageStore_;
    CustomEmojiRegistry registry_;

    // Cached data
    std::unordered_map<std::string, ast::Program> parsedPrograms_;
    std::unordered_map<std::string, std::vector<Diagnostic>> diagnosticsCache_;
};

// Utility functions
std::string readFile(const std::filesystem::path& path);
void writeFile(const std::filesystem::path& path, const std::string& content);
// parseJson and toJson are internal implementation details

std::string formatJsonRpc(const std::string& method, const JsonValue& params, std::optional<int> id = std::nullopt);
std::string formatJsonRpcResponse(const JsonValue& result, std::optional<int> id = std::nullopt);
std::string formatJsonRpcError(int code, const std::string& message, std::optional<int> id = std::nullopt);
std::string formatJsonRpcResponseStringId(const JsonValue& result, const std::string& id);
std::string formatJsonRpcErrorStringId(int code, const std::string& message, const std::string& id);
std::string formatNotification(const std::string& method, const JsonValue& params);

}  // namespace lsp
}  // namespace emojineer
