#include "TextLoader.h"

#include "NaiveLtrShaper.h"
#include "StbFontBackend.h"

#include <cmath>

namespace chisel::io {

namespace {

// Proportional to the font's own em size, so curve smoothness stays
// consistent across fonts with different unitsPerEm (1000 vs. 2048 are
// both common) rather than looking finer or coarser depending on the font.
float defaultFlatnessFontUnits(int unitsPerEmValue) {
    return static_cast<float>(unitsPerEmValue) / 1000.0f;
}

} // namespace

RawTextOutline loadTextOutline(const std::filesystem::path& fontPath, const std::string& text,
                               double size, const std::string& halign, const std::string& valign,
                               double spacing, double fnOverride) {
    RawTextOutline out;
    if (text.empty())
        return out;

    LoadedFont font = loadFont(fontPath, out.error);
    if (!font.valid)
        return out;

    ShapedText shaped = shapeLtr(font, text, static_cast<float>(spacing));
    if (shaped.glyphs.empty())
        return out;

    int upm = unitsPerEm(font);
    float scale = (upm > 0) ? static_cast<float>(size) / static_cast<float>(upm) : 1.0f;
    int fixedSegments = (fnOverride > 0.0) ? static_cast<int>(std::lround(fnOverride)) : 0;
    float flatness = defaultFlatnessFontUnits(upm);

    float textWidth = shaped.totalAdvance * scale;
    float xOffset = 0.0f;
    if (halign == "center")
        xOffset = -textWidth / 2.0f;
    else if (halign == "right")
        xOffset = -textWidth;

    FontVMetrics vm = fontVMetrics(font);
    float yOffset = 0.0f;
    if (valign == "top")
        yOffset = -static_cast<float>(vm.ascent) * scale;
    else if (valign == "bottom")
        yOffset = -static_cast<float>(vm.descent) * scale;
    else if (valign == "center")
        yOffset = -static_cast<float>(vm.ascent + vm.descent) * 0.5f * scale;
    // "baseline" (default, and any unrecognized value): no shift.

    for (const ShapedGlyph& g : shaped.glyphs) {
        GlyphOutline glyph = glyphOutline(font, g.glyphIndex, flatness, fixedSegments);
        for (const GlyphContour& contour : glyph.contours) {
            if (contour.size() < 3)
                continue; // degenerate — not a fillable contour

            std::vector<int> path;
            path.reserve(contour.size());
            for (const glm::vec2& local : contour) {
                path.push_back(static_cast<int>(out.points.size()));
                out.points.emplace_back((g.penX + local.x) * scale + xOffset,
                                        local.y * scale + yOffset);
            }
            out.paths.push_back(std::move(path));
        }
    }

    return out;
}

} // namespace chisel::io
