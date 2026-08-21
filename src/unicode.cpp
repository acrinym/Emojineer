#include "emojineer/unicode.hpp"

#include <unicode/brkiter.h>
#include <unicode/normalizer2.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>

#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace emojineer {
namespace {

icu::UnicodeString from_utf8(const std::string& text) {
    return icu::UnicodeString::fromUTF8(icu::StringPiece(text));
}

std::string to_utf8(const icu::UnicodeString& text) {
    std::string result;
    text.toUTF8String(result);
    return result;
}

icu::UnicodeString remove_variation_selectors(const icu::UnicodeString& input) {
    icu::UnicodeString out;
    for (int32_t i = 0; i < input.length();) {
        UChar32 cp = input.char32At(i);
        i += U16_LENGTH(cp);
        if (cp == 0xFE0E || cp == 0xFE0F) continue;
        out.append(cp);
    }
    return out;
}

} // namespace

std::string canonicalize_token(const std::string& utf8) {
    UErrorCode status = U_ZERO_ERROR;
    const icu::Normalizer2* nfc = icu::Normalizer2::getNFCInstance(status);
    if (U_FAILURE(status)) throw std::runtime_error("unable to initialize ICU NFC normalizer");
    icu::UnicodeString input = remove_variation_selectors(from_utf8(utf8));
    icu::UnicodeString normalized;
    nfc->normalize(input, normalized, status);
    if (U_FAILURE(status)) throw std::runtime_error("unable to normalize Unicode token");
    return to_utf8(normalized);
}

std::vector<Grapheme> segment_graphemes(const std::string& utf8) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::BreakIterator> breaker(icu::BreakIterator::createCharacterInstance(icu::Locale::getRoot(), status));
    if (U_FAILURE(status) || !breaker) throw std::runtime_error("unable to initialize ICU grapheme iterator");
    icu::UnicodeString text = from_utf8(utf8);
    breaker->setText(text);
    std::vector<Grapheme> result;
    int32_t start = breaker->first();
    for (int32_t end = breaker->next(); end != icu::BreakIterator::DONE; start = end, end = breaker->next()) {
        icu::UnicodeString piece = text.tempSubStringBetween(start, end);
        std::string display = to_utf8(piece);
        result.push_back({display, canonicalize_token(display)});
    }
    return result;
}

bool is_emoji_grapheme(const std::string& utf8) {
    icu::UnicodeString text = from_utf8(utf8);
    for (int32_t i = 0; i < text.length();) {
        UChar32 cp = text.char32At(i);
        i += U16_LENGTH(cp);
        if (u_hasBinaryProperty(cp, UCHAR_EMOJI) || u_hasBinaryProperty(cp, UCHAR_EXTENDED_PICTOGRAPHIC)) return true;
    }
    return false;
}

std::string codepoints_hex(const std::string& utf8) {
    icu::UnicodeString text = from_utf8(utf8);
    std::ostringstream out;
    bool first = true;
    for (int32_t i = 0; i < text.length();) {
        UChar32 cp = text.char32At(i);
        i += U16_LENGTH(cp);
        if (!first) out << ' ';
        first = false;
        out << "U+" << std::uppercase << std::hex << std::setw(cp <= 0xFFFF ? 4 : 6)
            << std::setfill('0') << static_cast<std::uint32_t>(cp);
    }
    return out.str();
}

} // namespace emojineer
