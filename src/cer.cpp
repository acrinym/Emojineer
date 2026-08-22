#include "emojineer/cer.hpp"

#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <unordered_map>

namespace emojineer {
namespace {

class JsonCursor {
public:
    explicit JsonCursor(const std::string& text) : text_(text) {}

    void ws() { while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_; }
    bool consume(char c) { ws(); if (pos_ < text_.size() && text_[pos_] == c) { ++pos_; return true; } return false; }
    void expect(char c, const char* what) { if (!consume(c)) fail(std::string("expected ") + what); }

    std::string string() {
        ws();
        if (pos_ >= text_.size() || text_[pos_] != '"') fail("expected JSON string");
        ++pos_;
        std::string out;
        while (pos_ < text_.size()) {
            unsigned char ch = static_cast<unsigned char>(text_[pos_++]);
            if (ch == '"') return out;
            if (ch == '\\') {
                if (pos_ >= text_.size()) fail("truncated JSON escape");
                char e = text_[pos_++];
                switch (e) {
                    case '"': out.push_back('"'); break; case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break; case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break; case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break; case 't': out.push_back('\t'); break;
                    default: fail("CER JSON supports UTF-8 directly; unsupported escape");
                }
            } else {
                out.push_back(static_cast<char>(ch));
            }
        }
        fail("unterminated JSON string");
    }

    bool end() { ws(); return pos_ == text_.size(); }
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("CER JSON byte " + std::to_string(pos_) + ": " + message);
    }
private:
    const std::string& text_;
    std::size_t pos_{0};
};

using Obj = std::unordered_map<std::string, std::string>;

std::vector<Obj> parse_registry_json(const std::string& text) {
    JsonCursor c(text);
    c.expect('{', "'{'");
    std::vector<Obj> result;
    bool saw_tokens = false;
    bool first_root = true;
    while (!c.consume('}')) {
        if (!first_root) c.expect(',', "','");
        first_root = false;
        std::string key = c.string();
        c.expect(':', "':'");
        if (key != "tokens") c.fail("only root key 'tokens' is supported");
        if (saw_tokens) c.fail("duplicate 'tokens' key");
        saw_tokens = true;
        c.expect('[', "'['");
        bool first_item = true;
        while (!c.consume(']')) {
            if (!first_item) c.expect(',', "','");
            first_item = false;
            c.expect('{', "'{'");
            Obj obj;
            bool first_field = true;
            while (!c.consume('}')) {
                if (!first_field) c.expect(',', "','");
                first_field = false;
                std::string field = c.string();
                c.expect(':', "':'");
                std::string value = c.string();
                if (!obj.emplace(field, value).second) c.fail("duplicate token field '" + field + "'");
            }
            result.push_back(std::move(obj));
        }
    }
    if (!saw_tokens) c.fail("missing root 'tokens' array");
    if (!c.end()) c.fail("trailing content");
    return result;
}

std::string require_field(const Obj& o, const std::string& key) {
    auto it = o.find(key);
    if (it == o.end() || it->second.empty()) throw std::runtime_error("CER token missing non-empty '" + key + "'");
    return it->second;
}

std::string sequence_key(const std::vector<std::string>& seq) {
    std::string key;
    for (const auto& s : seq) { key += s; key.push_back('\x1f'); }
    return key;
}

} // namespace

std::uint32_t semantic_token_id(const std::vector<std::string>& seq) {
    std::uint32_t h = 2166136261u;
    for (const auto& part : seq) {
        for (unsigned char b : part) { h ^= b; h *= 16777619u; }
        h ^= 0x1fu; h *= 16777619u;
    }
    return h;
}

TokenKind token_kind_from_name(const std::string& n) {
    static const std::unordered_map<std::string, TokenKind> map = {
        {"Var",TokenKind::Var},{"Assign",TokenKind::Assign},{"Print",TokenKind::Print},
        {"If",TokenKind::If},{"Else",TokenKind::Else},{"While",TokenKind::While},{"End",TokenKind::End},
        {"Input",TokenKind::Input},{"True",TokenKind::True},{"False",TokenKind::False},
        {"TypeNumber",TokenKind::TypeNumber},{"TypeString",TokenKind::TypeString},{"TypeBool",TokenKind::TypeBool},
        {"Function",TokenKind::Function},{"Return",TokenKind::Return},{"Add",TokenKind::Add},
        {"Subtract",TokenKind::Subtract},{"Multiply",TokenKind::Multiply},{"Divide",TokenKind::Divide},
        {"Modulo",TokenKind::Modulo},{"Equal",TokenKind::Equal},{"Less",TokenKind::Less},{"Greater",TokenKind::Greater},
        {"Not",TokenKind::Not},{"GroupStart",TokenKind::GroupStart},{"GroupEnd",TokenKind::GroupEnd},
        {"Array",TokenKind::Array},{"Index",TokenKind::Index},{"Length",TokenKind::Length},
        {"Append",TokenKind::Append},{"SetIndex",TokenKind::SetIndex}
    };
    auto it = map.find(n);
    if (it == map.end()) throw std::runtime_error("CER maps_to references unknown core token kind '" + n + "'");
    return it->second;
}

CustomEmojiRegistry::CustomEmojiRegistry() {
    auto core = [this](const char* glyph, TokenKind kind, const char* description) {
        add_definition(glyph, kind, "", description, false);
    };
    core("🐍",TokenKind::Var,"declare variable"); core("✏️",TokenKind::Assign,"assign variable");
    core("📝",TokenKind::Print,"print value"); core("🤔",TokenKind::If,"begin if block");
    core("🙅",TokenKind::Else,"begin else block"); core("🔁",TokenKind::While,"begin while loop");
    core("🏁",TokenKind::End,"end block"); core("📥",TokenKind::Input,"read one line of input");
    core("✅",TokenKind::True,"boolean true"); core("❌",TokenKind::False,"boolean false");
    core("🔢",TokenKind::TypeNumber,"number type"); core("🔤",TokenKind::TypeString,"text type");
    core("🎯",TokenKind::TypeBool,"boolean type"); core("🛠️",TokenKind::Function,"define function");
    core("📦",TokenKind::Return,"return from function"); core("➕",TokenKind::Add,"addition or text concatenation");
    core("➖",TokenKind::Subtract,"subtraction or numeric negation"); core("✖️",TokenKind::Multiply,"multiplication");
    core("➗",TokenKind::Divide,"division"); core("🪄",TokenKind::Modulo,"remainder");
    core("🟰",TokenKind::Equal,"assignment separator or equality comparison"); core("🔽",TokenKind::Less,"less-than comparison");
    core("🔼",TokenKind::Greater,"greater-than comparison"); core("🚫",TokenKind::Not,"boolean negation");
    core("🫴",TokenKind::GroupStart,"begin grouped expression or argument list"); core("🤲",TokenKind::GroupEnd,"end grouped expression or argument list");
    core("📚",TokenKind::Array,"array type or array literal"); core("🔎",TokenKind::Index,"read array element by index");
    core("📏",TokenKind::Length,"array or text length"); core("📎",TokenKind::Append,"return array with appended value");
    core("🧷",TokenKind::SetIndex,"return array with element replaced");
}

void CustomEmojiRegistry::add_definition(std::string glyph, TokenKind kind, std::string alias,
                                         std::string description, bool custom) {
    auto gs = segment_graphemes(glyph);
    if (gs.empty()) throw std::runtime_error("CER glyph cannot be empty");
    std::vector<std::string> seq;
    seq.reserve(gs.size());
    for (const auto& g : gs) {
        if (!is_emoji_grapheme(g.canonical)) throw std::runtime_error("CER glyph sequences must contain only emoji graphemes");
        seq.push_back(g.canonical);
    }
    std::string key = sequence_key(seq);
    if (exact_.contains(key)) throw std::runtime_error("CER canonical token collision for '" + glyph + "'");
    if (!alias.empty() && aliases_.contains(alias)) throw std::runtime_error("CER alias collision for '" + alias + "'");
    std::uint32_t id = semantic_token_id(seq);
    if (ids_.contains(id)) throw std::runtime_error("CER semantic token ID collision for '" + glyph + "'");
    std::size_t index = definitions_.size();
    definitions_.push_back({kind,std::move(glyph),std::move(seq),std::move(alias),std::move(description),id,custom});
    exact_[key] = index;
    if (!definitions_.back().alias.empty()) aliases_[definitions_.back().alias] = index;
    ids_[id] = index;
    if (definitions_.back().sequence.size() > max_sequence_) max_sequence_ = definitions_.back().sequence.size();
}

void CustomEmojiRegistry::load_json(const std::string& json, const std::string& origin) {
    try {
        for (const auto& o : parse_registry_json(json)) {
            for (const auto& [k,_] : o) {
                if (k != "glyph" && k != "alias" && k != "description" && k != "maps_to")
                    throw std::runtime_error("unknown CER token field '" + k + "'");
            }
            add_definition(require_field(o,"glyph"), token_kind_from_name(require_field(o,"maps_to")),
                           require_field(o,"alias"), require_field(o,"description"), true);
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("CER '" + origin + "': " + e.what());
    }
}

void CustomEmojiRegistry::load_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open CER '" + path + "'");
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    load_json(text, path);
}

const SemanticTokenDefinition* CustomEmojiRegistry::match(const std::vector<Grapheme>& gs,
                                                           std::size_t start, std::size_t& consumed) const {
    consumed = 0;
    const SemanticTokenDefinition* best = nullptr;
    const std::size_t limit = std::min(max_sequence_, gs.size() - start);
    std::vector<std::string> seq;
    for (std::size_t n = 1; n <= limit; ++n) {
        if (gs[start+n-1].display == "\n") break;
        seq.push_back(gs[start+n-1].canonical);
        auto it = exact_.find(sequence_key(seq));
        if (it != exact_.end()) { best = &definitions_[it->second]; consumed = n; }
    }
    return best;
}

const SemanticTokenDefinition* CustomEmojiRegistry::find_exact(const std::string& canonical) const {
    std::vector<std::string> seq{canonical};
    auto it = exact_.find(sequence_key(seq));
    return it == exact_.end() ? nullptr : &definitions_[it->second];
}

std::string CustomEmojiRegistry::describe(TokenKind kind) const {
    for (const auto& d : definitions_) if (!d.custom && d.kind == kind) return d.description;
    return token_kind_name(kind);
}

} // namespace emojineer
