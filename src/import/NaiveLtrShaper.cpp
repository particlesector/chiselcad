#include "NaiveLtrShaper.h"

namespace chisel::io {

namespace {

// Decodes one UTF-8 codepoint starting at `i`, advancing `i` past it.
// Invalid/truncated sequences yield U+FFFD and advance by a single byte,
// so a corrupt string degrades gracefully instead of throwing/crashing.
uint32_t decodeUtf8At(const std::string& s, size_t& i) {
    unsigned char b0 = static_cast<unsigned char>(s[i]);
    auto continuation = [&](size_t idx) {
        return idx < s.size() && (static_cast<unsigned char>(s[idx]) & 0xC0) == 0x80;
    };

    if (b0 < 0x80) {
        ++i;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && continuation(i + 1)) {
        uint32_t cp =
            (static_cast<uint32_t>(b0 & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        i += 2;
        return cp;
    }
    if ((b0 & 0xF0) == 0xE0 && continuation(i + 1) && continuation(i + 2)) {
        uint32_t cp = (static_cast<uint32_t>(b0 & 0x0F) << 12) |
                      (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        i += 3;
        return cp;
    }
    if ((b0 & 0xF8) == 0xF0 && continuation(i + 1) && continuation(i + 2) && continuation(i + 3)) {
        uint32_t cp = (static_cast<uint32_t>(b0 & 0x07) << 18) |
                      (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
                      (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        i += 4;
        return cp;
    }
    ++i;
    return 0xFFFD;
}

} // namespace

ShapedText shapeLtr(const LoadedFont& font, const std::string& utf8Text, float spacing) {
    ShapedText result;

    uint32_t prevGlyph = 0;
    bool havePrev = false;
    float pen = 0.0f;

    size_t i = 0;
    while (i < utf8Text.size()) {
        uint32_t cp = decodeUtf8At(utf8Text, i);
        uint32_t glyph = glyphIndexForCodepoint(font, cp);

        if (havePrev)
            pen += static_cast<float>(glyphKernAdvance(font, prevGlyph, glyph));

        result.glyphs.push_back({glyph, pen});
        pen += static_cast<float>(glyphAdvanceWidth(font, glyph)) * spacing;

        prevGlyph = glyph;
        havePrev = true;
    }

    result.totalAdvance = pen;
    return result;
}

} // namespace chisel::io
