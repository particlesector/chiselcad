#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace chisel::io {

// Shared by DxfLoader/SvgLoader: a flat point pool plus one index list per
// closed contour, exactly matching CsgLeaf::Polygon2D's own
// polyPoints/polyPaths shape (see CsgNode.h) — the same shape
// RawTextOutline (TextLoader.h) already uses for text()'s glyph contours,
// so import()'s DXF/SVG cases can assign straight into a CsgLeaf the same
// way evalText() does, with no per-format conversion step.
struct RawPolygon2D {
    std::vector<glm::vec2> points;
    std::vector<std::vector<int>> paths; // one closed contour per entry, indices into `points`
    std::string error;                   // empty = success
};

} // namespace chisel::io
