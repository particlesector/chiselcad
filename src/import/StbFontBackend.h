#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#include <stb/stb_truetype.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace chisel::io {

// A single closed glyph contour, in font units — straight segments only;
// any quadratic/cubic curve from the source outline has already been
// flattened by glyphOutline() below.
using GlyphContour = std::vector<glm::vec2>;

struct GlyphOutline {
    std::vector<GlyphContour> contours; // empty for whitespace/undrawable glyphs
};

struct FontVMetrics {
    int ascent = 0;  // font units, positive (above baseline)
    int descent = 0; // font units, negative (below baseline) — stb_truetype convention
    int lineGap = 0;
};

// ---------------------------------------------------------------------------
// LoadedFont — an in-memory-parsed .ttf/.otf, backed by vendored stb_truetype
// (implementation compiled once in stb_truetype_impl.cpp). `info.data`
// points into `fileData`'s heap buffer; moving a LoadedFont is safe (a
// vector move preserves the buffer's address), copying is not (the copy's
// `info` would still point at the original's buffer) — so copying is
// disabled.
// ---------------------------------------------------------------------------
struct LoadedFont {
    std::vector<unsigned char> fileData;
    stbtt_fontinfo info{};
    bool valid = false;

    LoadedFont() = default;
    LoadedFont(const LoadedFont&) = delete;
    LoadedFont& operator=(const LoadedFont&) = delete;
    LoadedFont(LoadedFont&&) = default;
    LoadedFont& operator=(LoadedFont&&) = default;
};

// Reads and parses `path`. On failure, sets `error` and returns a LoadedFont
// with valid=false.
LoadedFont loadFont(const std::filesystem::path& path, std::string& error);

// The functions below are thin wrappers around one stbtt_* call each,
// isolated here — rather than called directly from TextLoader.cpp/
// NaiveLtrShaper.cpp — as the seam a future higher-fidelity backend (e.g.
// FreeType, for hinting/color-font/variable-font support) would replace:
// same shapes, different .cpp; callers wouldn't need to change.
int unitsPerEm(const LoadedFont& font);
FontVMetrics fontVMetrics(const LoadedFont& font);
uint32_t glyphIndexForCodepoint(const LoadedFont& font, uint32_t codepoint);

// Outline in font units; quadratic/cubic curves are flattened via recursive
// subdivision. If `fixedSegments` > 0, every curve is split into exactly
// that many straight segments (matches the intent, not the exact formula,
// of the $fn override PrimitiveGen::resolveSegments() uses for circles —
// glyph curves are typically much shorter arcs than a full circle, so a
// literal port of that formula isn't meaningful here). If `fixedSegments`
// is 0, curves are adaptively subdivided to `flatnessFontUnits` (same
// font-unit space as the outline, i.e. independent of whatever `size` the
// caller will later scale by) — a better default for text than a fixed
// count, since it decouples visual smoothness from glyph size.
GlyphOutline glyphOutline(const LoadedFont& font, uint32_t glyphIndex, float flatnessFontUnits,
                          int fixedSegments);

int glyphAdvanceWidth(const LoadedFont& font, uint32_t glyphIndex);

// Font-unit kerning adjustment between two adjacent glyphs; 0 if the font
// has no legacy 'kern' table entry for the pair. stb_truetype only reads
// 'kern', not the GPOS kerning most modern fonts actually ship — see
// TextLoader.h's top-level scope comment.
int glyphKernAdvance(const LoadedFont& font, uint32_t glyphIndex1, uint32_t glyphIndex2);

} // namespace chisel::io
