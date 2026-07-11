#pragma once
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace chisel::io {

// ---------------------------------------------------------------------------
// RawTextOutline — 2-D glyph contours for a single line of shaped text, in
// model units (already scaled by `size` and anchored per halign/valign) —
// no GPU/render dependency, safe to include from the lightweight
// chiselcad_tests target (mirrors RawSurfaceMesh/RawStlMesh).
//
// Scope of this first cut (see docs/roadmap.md for the full writeup):
//   - stb_truetype only (StbFontBackend.h) + naive left-to-right layout
//     (NaiveLtrShaper.h): no ligatures/contextual substitution, no bidi/
//     complex-script reordering, kerning only via the legacy 'kern' table
//     (many current fonts ship kerning via GPOS instead, which is silently
//     not applied). Both files are the seam a future FreeType+HarfBuzz
//     backend would replace — this file's signature wouldn't need to
//     change.
//   - single line only: embedded newlines are not treated specially (no
//     line-wrapping/stacking), matching OpenSCAD's text() — multi-line
//     text is multiple text() calls composed with translate().
//   - `direction`/`language`/`script` (OpenSCAD parameters for bidi/
//     complex-script control) are accepted by CsgEvaluator::evalText and
//     discarded, like surface()'s `convexity`.
// ---------------------------------------------------------------------------
struct RawTextOutline {
    std::vector<glm::vec2> points;
    std::vector<std::vector<int>> paths; // one closed contour per entry, indices into `points`
    std::string error;                   // empty = success
};

// Loads `fontPath`, shapes `text`, and returns its outline contours scaled
// so the font's em-square maps to `size` model units, anchored per
// `halign` ("left"/"center"/"right", default "left") and `valign`
// ("top"/"center"/"baseline"/"bottom", default "baseline"). `spacing`
// scales each glyph's advance width (1.0 = normal). `fnOverride` > 0 fixes
// the number of straight segments each glyph curve is split into; 0 uses
// adaptive tolerance-based flattening (see StbFontBackend.h). An empty
// `text` (or text with no drawable glyphs) yields an empty, error-free
// result.
RawTextOutline loadTextOutline(const std::filesystem::path& fontPath, const std::string& text,
                               double size, const std::string& halign, const std::string& valign,
                               double spacing, double fnOverride);

} // namespace chisel::io
