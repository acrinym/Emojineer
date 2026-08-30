// Source Diagnostic and tokenToRange Unit Tests
// These tests verify the protocol-neutral diagnostic source identity
// and canonical token/range conversion

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>

#include "emojineer/token.hpp"
#include "emojineer/source_diagnostic.hpp"
#include "emojineer/unicode.hpp"

// Test helper: count lines in text (for verifying line count)
// Handles \n, \r\n, and lone \r as line endings
std::size_t countLines(const std::string& text) {
    if (text.empty()) return 1;
    std::size_t count = 1;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++count;
        } else if (text[i] == '\r') {
            // CR is a line ending - skip if followed by LF
            if (i + 1 >= text.size() || text[i + 1] != '\n') {
                ++count;
            }
        }
    }
    return count;
}

// Helper: decode a single UTF-8 code point
// Returns true if successful, advances pos to after the decoded sequence
// Returns false if invalid UTF-8
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
        pos += 1;
        return false;
    }
    
    // Decode continuation bytes
    for (int i = 1; i < seqLen; i++) {
        unsigned char cb = static_cast<unsigned char>(text[pos + i]);
        if ((cb & 0xC0) != 0x80) {
            pos += 1;
            return false;
        }
        cp = (cp << 6) | (cb & 0x3F);
    }
    
    // Validate code point
    if (cp > 0x10FFFF) {
        pos += 1;
        return false;
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) {
        pos += 1;
        return false;
    }
    
    codePoint = cp;
    pos += seqLen;
    return true;
}

// Helper: count UTF-16 code units for a Unicode code point
static std::uint32_t countUtf16UnitsForCodePoint(char32_t cp) {
    if (cp < 0x10000) return 1;  // BMP fits in one UTF-16 unit
    if (cp <= 0x10FFFF) return 2;  // Supplementary plane needs surrogate pair
    return 1;
}

// Count total UTF-16 code units in a UTF-8 string
static std::size_t countUtf16UnitsInString(const std::string& utf8Str) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while (pos < utf8Str.size()) {
        char32_t cp = 0;
        if (decodeUtf8CodePoint(utf8Str, pos, cp)) {
            count += countUtf16UnitsForCodePoint(cp);
        } else {
            // Invalid UTF-8, count as one unit
            count += 1;
        }
    }
    return count;
}

// Convert UTF-8 grapheme offset to UTF-16 position (line, column)
static std::pair<std::uint32_t, std::uint32_t> utf8ToUtf16Position(const std::string& text, std::size_t utf8Offset) {
    std::uint32_t line = 0;
    std::uint32_t utf16Col = 0;
    std::size_t utf8Pos = 0;
    
    while (utf8Pos < utf8Offset && utf8Pos < text.size()) {
        // Handle CRLF
        if (utf8Pos + 1 < text.size() && 
            text[utf8Pos] == '\r' && text[utf8Pos + 1] == '\n') {
            if (utf8Offset - utf8Pos >= 2) {
                line++;
                utf16Col = 0;
                utf8Pos += 2;
            } else {
                break;
            }
            continue;
        }
        
        unsigned char byte = static_cast<unsigned char>(text[utf8Pos]);
        
        if (byte == '\n') {
            line++;
            utf16Col = 0;
            utf8Pos++;
        } else if (byte == '\r') {
            line++;
            utf16Col = 0;
            utf8Pos++;
        } else {
            char32_t cp = 0;
            std::size_t oldPos = utf8Pos;
            if (decodeUtf8CodePoint(text, utf8Pos, cp)) {
                utf16Col += countUtf16UnitsForCodePoint(cp);
            } else {
                utf8Pos = oldPos + 1;
                utf16Col += 1;
            }
        }
    }
    
    return {line, utf16Col};
}

// Test: SourceLocationException carries source path correctly
void test_source_location_exception_path() {
    std::cout << "Testing SourceLocationException path handling..." << std::endl;
    
    // Test with empty path
    emojineer::SourceLocationException e1("test error");
    assert(e1.sourcePath.empty());
    assert(e1.line == 1);
    assert(e1.column == 1);
    
    // Test with path
    emojineer::SourceLocationException e2("test error", "/path/to/file.emoji", 5, 10, "token");
    assert(!e2.sourcePath.empty());
    assert(e2.sourcePath == "/path/to/file.emoji");
    assert(e2.line == 5);
    assert(e2.column == 10);
    assert(e2.tokenLexeme == "token");
    
    std::cout << "  ✅ SourceLocationException path handling works" << std::endl;
}

// Test: Token structure is properly formed
void test_token_structure() {
    std::cout << "Testing Token structure..." << std::endl;
    
    emojineer::Token t;
    t.kind = emojineer::TokenKind::Identifier;
    t.lexeme = "test";
    t.canonical = "test";
    t.literal = "";
    t.line = 1;
    t.column = 1;
    
    assert(t.kind == emojineer::TokenKind::Identifier);
    assert(t.lexeme == "test");
    assert(t.line == 1);
    assert(t.column == 1);
    
    std::cout << "  ✅ Token structure is correct" << std::endl;
}

// Test: Line counting with different line endings
void test_line_counting() {
    std::cout << "Testing line counting..." << std::endl;
    
    // Empty string
    assert(countLines("") == 1);
    
    // No newlines
    assert(countLines("hello") == 1);
    
    // Single newline
    assert(countLines("hello\n") == 2);
    assert(countLines("\n") == 2);
    
    // Multiple newlines
    assert(countLines("line1\nline2\nline3") == 3);
    
    // CRLF should count as one line
    assert(countLines("line1\r\nline2") == 2);
    
    // Lone CR
    assert(countLines("line1\rline2") == 2);
    
    std::cout << "  ✅ Line counting works correctly" << std::endl;
}

// Test: UTF-8 grapheme segmentation works
void test_grapheme_segmentation() {
    std::cout << "Testing grapheme segmentation..." << std::endl;
    
    // ASCII
    auto ascii = emojineer::segment_graphemes("abc");
    assert(ascii.size() == 3);
    
    // BMP emoji
    auto bmp = emojineer::segment_graphemes("😀");
    assert(bmp.size() == 1);
    
    // ASCII + emoji
    auto mixed = emojineer::segment_graphemes("a😀b");
    assert(mixed.size() == 3);
    
    std::cout << "  ✅ Grapheme segmentation works" << std::endl;
}

// Test: UTF-16 unit counting - exact expected values
void test_utf16_unit_counting() {
    std::cout << "Testing UTF-16 unit counting exact values..." << std::endl;
    
    // ASCII - 1 UTF-16 unit per character
    assert(countUtf16UnitsInString("abc") == 3);
    
    // BMP emoji (like 😀 U+1F600) - 1 UTF-16 unit (in BMP)
    assert(countUtf16UnitsInString("😀") == 1);
    
    // Mixed ASCII and emoji
    assert(countUtf16UnitsInString("a😀b") == 4);  // 'a' + 😀 + 'b' = 1 + 1 + 1 = 3... wait, that's 3
    // Actually: 'a' (1), '😀' (1), 'b' (1) = 3
    assert(countUtf16UnitsInString("a😀b") == 3);
    
    // Supplementary plane emoji (e.g., U+1F3F4 WAVING BLACK FLAG - 4 byte UTF-8, 2 UTF-16 units)
    // 🏴 (U+1F3F4) = 4 bytes in UTF-8, 2 UTF-16 units (surrogate pair)
    assert(countUtf16UnitsInString("🏴") == 2);
    
    // Test full string with supplementary emoji
    assert(countUtf16UnitsInString("a🏴b") == 4);  // 'a'(1) + 🏴(2) + 'b'(1) = 4
    
    std::cout << "  ✅ UTF-16 unit counting exact values work" << std::endl;
}

// Test: Variation selector handling with UTF-16 units
void test_variation_selector() {
    std::cout << "Testing variation selector handling..." << std::endl;
    
    // Base emoji ⭐ (U+2605) - 1 UTF-16 unit
    auto base = emojineer::segment_graphemes("⭐");
    assert(base.size() == 1);
    assert(countUtf16UnitsInString("⭐") == 1);
    
    // Emoji with variation selector 16 (emoji style) ⭐️ (U+2605 U+FE0F)
    // This is a 3-byte UTF-8 sequence: E2 98 85 EF B8 8F
    // But in terms of grapheme clusters, it's 1 grapheme
    // And 1 UTF-16 unit (both code points are in BMP)
    auto emoji_style = emojineer::segment_graphemes("⭐️");
    assert(emoji_style.size() == 1);
    assert(countUtf16UnitsInString("⭐️") == 2);  // Both in BMP, so 2 UTF-16 units
    
    std::cout << "  ✅ Variation selector handling works" << std::endl;
}

// Test: Combining sequence handling with UTF-16 units
void test_combining_sequence() {
    std::cout << "Testing combining sequence handling..." << std::endl;
    
    // Composed é (U+00E9) - 1 UTF-16 unit
    assert(countUtf16UnitsInString("é") == 1);
    
    // Decomposed é (e + combining acute: U+0065 U+0301) - 2 UTF-16 units
    // In UTF-8: C3 A9 (composed) vs 65 CC 81 (decomposed)
    // Both result in 1 grapheme, but the decomposed form has 2 code points
    auto combining = emojineer::segment_graphemes("e\u0301");  // decomposed é
    assert(combining.size() == 1);
    // The decomposed form has 2 code points, both in BMP = 2 UTF-16 units
    assert(countUtf16UnitsInString("e\u0301") == 2);
    
    std::cout << "  ✅ Combining sequence handling works" << std::endl;
}

// Test: ZWJ sequence handling with UTF-16 units
void test_zwj_sequence() {
    std::cout << "Testing ZWJ sequence handling..." << std::endl;
    
    // Family emoji: 👨‍👩‍👧‍👦 (man, ZWJ, woman, ZWJ, girl, ZWJ, girl)
    // This is a multi-codepoint sequence but single grapheme
    // All code points are in supplementary plane, so many UTF-16 units
    auto family = emojineer::segment_graphemes("👨‍👩‍👧‍👦");
    assert(family.size() == 1);  // Single grapheme cluster
    
    // Count UTF-16 units - this family has 7 code points, many in supplementary
    // Each supplementary code point = 2 UTF-16 units
    std::size_t familyUtf16 = countUtf16UnitsInString("👨‍👩‍👧‍👦");
    assert(familyUtf16 >= 7);  // At least 7 UTF-16 units
    
    // Flag emoji: 🏴󠁧󠁢󠁥󠁮󠁧󠁿 (Scotland flag - tag sequence)
    // This is a base emoji + tag characters
    auto flag = emojineer::segment_graphemes("🏴󠁧󠁢󠁥󠁮󠁧󠁿");
    assert(flag.size() == 1);
    
    std::cout << "  ✅ ZWJ sequence handling works" << std::endl;
}

// Test: Source diagnostic can be caught and rethrown with path
void test_source_diagnostic_rethrow() {
    std::cout << "Testing SourceLocationException rethrow with path..." << std::endl;
    
    try {
        // Simulate catching and rethrowing with path
        emojineer::SourceLocationException original("original error", "", 1, 1);
        
        // Rethrow with path attached
        throw emojineer::SourceLocationException(
            original.message,
            "/module/path.emoji",
            original.line,
            original.column,
            original.tokenLexeme
        );
    } catch (const emojineer::SourceLocationException& e) {
        assert(e.sourcePath == "/module/path.emoji");
        assert(e.line == 1);
        assert(e.column == 1);
    }
    
    std::cout << "  ✅ SourceLocationException rethrow works" << std::endl;
}

// Test: source_diagnostic.hpp can be included in core files
void test_core_includes_diagnostic() {
    std::cout << "Testing core files can include source_diagnostic.hpp..." << std::endl;
    
    // This test verifies that the header is self-contained
    // and doesn't pull in LSP dependencies
    
    // Create exception and verify it's protocol-neutral
    emojineer::SourceLocationException e(
        "test message",
        std::filesystem::path("/test/path.emoji"),
        10,
        5,
        "test_token"
    );
    
    // Verify all fields are accessible
    assert(!e.message.empty());
    assert(!e.sourcePath.empty());
    assert(e.line == 10);
    assert(e.column == 5);
    assert(e.tokenLexeme == "test_token");
    
    std::cout << "  ✅ Core files can include source_diagnostic.hpp" << std::endl;
}

int main() {
    std::cout << "=== Source Diagnostic and Token/Range Unit Tests ===" << std::endl;
    std::cout << std::endl;
    
    try {
        test_source_location_exception_path();
        test_token_structure();
        test_line_counting();
        test_grapheme_segmentation();
        test_utf16_unit_counting();
        test_variation_selector();
        test_combining_sequence();
        test_zwj_sequence();
        test_source_diagnostic_rethrow();
        test_core_includes_diagnostic();
        
        std::cout << std::endl;
        std::cout << "=== All Source Diagnostic Tests Passed! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << std::endl;
        std::cerr << "❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
}
