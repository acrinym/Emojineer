#pragma once

#include "emojineer/token.hpp"
#include "emojineer/unicode.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace emojineer {

struct SemanticTokenDefinition {
    TokenKind kind{TokenKind::Identifier};
    std::string glyph;
    std::vector<std::string> sequence;
    std::string alias;
    std::string description;
    std::uint32_t semantic_id{0};
    bool custom{false};
};

class CustomEmojiRegistry {
public:
    CustomEmojiRegistry();

    void load_json(const std::string& json, const std::string& origin = "<memory>");
    void load_file(const std::string& path);

    const SemanticTokenDefinition* match(const std::vector<Grapheme>& graphemes,
                                         std::size_t start,
                                         std::size_t& consumed) const;
    const SemanticTokenDefinition* find_exact(const std::string& canonical) const;
    std::string describe(TokenKind kind) const;

    const std::vector<SemanticTokenDefinition>& definitions() const { return definitions_; }

private:
    void add_definition(std::string glyph, TokenKind kind, std::string alias,
                        std::string description, bool custom);

    std::vector<SemanticTokenDefinition> definitions_;
    std::unordered_map<std::string, std::size_t> exact_;
    std::unordered_map<std::string, std::size_t> aliases_;
    std::unordered_map<std::uint32_t, std::size_t> ids_;
    std::size_t max_sequence_{1};
};

std::uint32_t semantic_token_id(const std::vector<std::string>& canonical_sequence);
TokenKind token_kind_from_name(const std::string& name);

} // namespace emojineer
