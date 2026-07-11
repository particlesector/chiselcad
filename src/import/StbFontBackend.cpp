#include "StbFontBackend.h"

#include <cmath>
#include <fstream>

namespace chisel::io {

namespace {

// Recursive de Casteljau subdivision, splitting until the control point's
// deviation from the chord is within `flatnessSq` (squared, to avoid a
// sqrt per test) or a depth cap is hit (guards against numerically
// degenerate curves rather than any expected well-formed glyph data).
constexpr int kMaxSubdivisionDepth = 10;

void flattenQuadratic(glm::vec2 p0, glm::vec2 c, glm::vec2 p1, float flatnessSq, int fixedSegments,
                      std::vector<glm::vec2>& out, int depth = 0) {
    if (fixedSegments > 0) {
        for (int i = 1; i <= fixedSegments; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(fixedSegments);
            float mt = 1.0f - t;
            out.push_back(mt * mt * p0 + 2.0f * mt * t * c + t * t * p1);
        }
        return;
    }

    glm::vec2 chord = p1 - p0;
    float lenSq = glm::dot(chord, chord);
    float distSq;
    if (lenSq < 1e-9f) {
        distSq = glm::dot(c - p0, c - p0);
    } else {
        float t = glm::clamp(glm::dot(c - p0, chord) / lenSq, 0.0f, 1.0f);
        glm::vec2 proj = p0 + t * chord;
        distSq = glm::dot(c - proj, c - proj);
    }

    if (distSq <= flatnessSq || depth >= kMaxSubdivisionDepth) {
        out.push_back(p1);
        return;
    }

    glm::vec2 p01 = 0.5f * (p0 + c);
    glm::vec2 p12 = 0.5f * (c + p1);
    glm::vec2 p012 = 0.5f * (p01 + p12);
    flattenQuadratic(p0, p01, p012, flatnessSq, 0, out, depth + 1);
    flattenQuadratic(p012, p12, p1, flatnessSq, 0, out, depth + 1);
}

void flattenCubic(glm::vec2 p0, glm::vec2 c1, glm::vec2 c2, glm::vec2 p1, float flatnessSq,
                  int fixedSegments, std::vector<glm::vec2>& out, int depth = 0) {
    if (fixedSegments > 0) {
        for (int i = 1; i <= fixedSegments; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(fixedSegments);
            float mt = 1.0f - t;
            out.push_back(mt * mt * mt * p0 + 3.0f * mt * mt * t * c1 + 3.0f * mt * t * t * c2 +
                          t * t * t * p1);
        }
        return;
    }

    glm::vec2 chord = p1 - p0;
    float lenSq = glm::dot(chord, chord);
    auto distToChordSq = [&](glm::vec2 pt) {
        if (lenSq < 1e-9f)
            return glm::dot(pt - p0, pt - p0);
        float t = glm::clamp(glm::dot(pt - p0, chord) / lenSq, 0.0f, 1.0f);
        glm::vec2 proj = p0 + t * chord;
        return glm::dot(pt - proj, pt - proj);
    };

    if ((std::max)(distToChordSq(c1), distToChordSq(c2)) <= flatnessSq ||
        depth >= kMaxSubdivisionDepth) {
        out.push_back(p1);
        return;
    }

    glm::vec2 p01 = 0.5f * (p0 + c1);
    glm::vec2 p12 = 0.5f * (c1 + c2);
    glm::vec2 p23 = 0.5f * (c2 + p1);
    glm::vec2 p012 = 0.5f * (p01 + p12);
    glm::vec2 p123 = 0.5f * (p12 + p23);
    glm::vec2 p0123 = 0.5f * (p012 + p123);
    flattenCubic(p0, p01, p012, p0123, flatnessSq, 0, out, depth + 1);
    flattenCubic(p0123, p123, p23, p1, flatnessSq, 0, out, depth + 1);
}

} // namespace

LoadedFont loadFont(const std::filesystem::path& path, std::string& error) {
    LoadedFont out;

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        error = "Cannot open file: " + path.string();
        return out;
    }
    auto size = f.tellg();
    if (size <= 0) {
        error = "Font file is empty: " + path.string();
        return out;
    }
    f.seekg(0);
    out.fileData.resize(static_cast<size_t>(size));
    if (!f.read(reinterpret_cast<char*>(out.fileData.data()), size)) {
        error = "Failed to read file: " + path.string();
        out.fileData.clear();
        return out;
    }

    int offset = stbtt_GetFontOffsetForIndex(out.fileData.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&out.info, out.fileData.data(), offset)) {
        error = "Not a recognized TrueType/OpenType font: " + path.string();
        out.fileData.clear();
        return out;
    }

    out.valid = true;
    return out;
}

int unitsPerEm(const LoadedFont& font) {
    float scale = stbtt_ScaleForMappingEmToPixels(&font.info, 1.0f);
    return scale > 0.0f ? static_cast<int>(std::round(1.0f / scale)) : 1000;
}

FontVMetrics fontVMetrics(const LoadedFont& font) {
    FontVMetrics m;
    stbtt_GetFontVMetrics(&font.info, &m.ascent, &m.descent, &m.lineGap);
    return m;
}

uint32_t glyphIndexForCodepoint(const LoadedFont& font, uint32_t codepoint) {
    return static_cast<uint32_t>(stbtt_FindGlyphIndex(&font.info, static_cast<int>(codepoint)));
}

GlyphOutline glyphOutline(const LoadedFont& font, uint32_t glyphIndex, float flatnessFontUnits,
                          int fixedSegments) {
    GlyphOutline out;

    stbtt_vertex* verts = nullptr;
    int numVerts = stbtt_GetGlyphShape(&font.info, static_cast<int>(glyphIndex), &verts);
    if (numVerts <= 0 || !verts)
        return out;

    float flatnessSq = flatnessFontUnits * flatnessFontUnits;
    GlyphContour current;
    glm::vec2 pen{0.0f, 0.0f};

    for (int i = 0; i < numVerts; ++i) {
        const stbtt_vertex& v = verts[i];
        glm::vec2 to{static_cast<float>(v.x), static_cast<float>(v.y)};
        switch (v.type) {
        case STBTT_vmove:
            if (!current.empty())
                out.contours.push_back(std::move(current));
            current.clear();
            current.push_back(to);
            pen = to;
            break;
        case STBTT_vline:
            current.push_back(to);
            pen = to;
            break;
        case STBTT_vcurve: {
            glm::vec2 ctrl{static_cast<float>(v.cx), static_cast<float>(v.cy)};
            flattenQuadratic(pen, ctrl, to, flatnessSq, fixedSegments, current);
            pen = to;
            break;
        }
        case STBTT_vcubic: {
            glm::vec2 c1{static_cast<float>(v.cx), static_cast<float>(v.cy)};
            glm::vec2 c2{static_cast<float>(v.cx1), static_cast<float>(v.cy1)};
            flattenCubic(pen, c1, c2, to, flatnessSq, fixedSegments, current);
            pen = to;
            break;
        }
        default:
            break;
        }
    }
    if (!current.empty())
        out.contours.push_back(std::move(current));

    stbtt_FreeShape(&font.info, verts);
    return out;
}

int glyphAdvanceWidth(const LoadedFont& font, uint32_t glyphIndex) {
    int advance = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&font.info, static_cast<int>(glyphIndex), &advance, &lsb);
    return advance;
}

int glyphKernAdvance(const LoadedFont& font, uint32_t glyphIndex1, uint32_t glyphIndex2) {
    return stbtt_GetGlyphKernAdvance(&font.info, static_cast<int>(glyphIndex1),
                                     static_cast<int>(glyphIndex2));
}

} // namespace chisel::io
