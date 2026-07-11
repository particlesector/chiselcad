#pragma once
#include "StbFontBackend.h"

#include <cstdint>
#include <string>
#include <vector>

namespace chisel::io {

struct ShapedGlyph {
    uint32_t glyphIndex;
    float penX; // font units, this glyph's origin along the baseline
};

struct ShapedText {
    std::vector<ShapedGlyph> glyphs;
    float totalAdvance = 0.0f; // font units, pen position after the last glyph
};

// Decodes `utf8Text` as UTF-8 codepoints and lays them out left-to-right
// along the baseline: cmap lookup per codepoint (no ligatures/contextual
// substitution), legacy 'kern'-table kerning between adjacent pairs (see
// StbFontBackend.h's glyphKernAdvance caveat), each glyph's advance scaled
// by `spacing`. Malformed UTF-8 bytes are replaced with U+FFFD rather than
// rejected outright. This is the seam a future shaping engine (e.g.
// HarfBuzz, for ligatures/bidi/complex scripts) would replace — same
// output shape (a positioned glyph run), different implementation;
// TextLoader.cpp itself wouldn't need to change.
ShapedText shapeLtr(const LoadedFont& font, const std::string& utf8Text, float spacing);

} // namespace chisel::io
