#pragma once
#include "RawPolygon2D.h"

#include <filesystem>

namespace chisel::io {

// Reads an SVG file's shape elements (rect, circle, ellipse, polygon, and
// path) and returns one closed 2-D contour per closed shape found:
//   - rect / circle / ellipse / polygon are always closed by definition
//     (rect's rx/ry corner rounding is not modeled — corners are sharp)
//   - path: each subpath that ends in a Z/z (closepath) command becomes one
//     contour; open subpaths are skipped, matching DxfLoader's "closed
//     contours only" policy so the two 2-D import() formats behave the same
//     way. Supported path commands: M/m, L/l, H/h, V/v, C/c (cubic
//     Bezier), Q/q (quadratic Bezier), Z/z — each flattened to straight
//     segments (curves via a fixed subdivision count, see
//     kCurveSegments in SvgLoader.cpp). S/s, T/t (smooth-curve shorthands)
//     and A/a (elliptical arcs) are recognized just enough to stay
//     positioned correctly (their parameters are consumed so later commands
//     don't desync) but are drawn as a straight line to their endpoint
//     rather than the true curve/arc — a scope limitation, not a crash.
//   - line / polyline: always open (or, for polyline, only accidentally
//     closed if its own last point repeats its first) — skipped, same
//     reasoning as DXF's bare LINE entities.
//
// `transform` attributes, `<use>`/`<g>` grouping, `viewBox`/`style`-based
// geometry, and units other than bare numbers (px/mm/etc suffixes) are not
// interpreted — this is a straight-line/curve *shape* importer for simple,
// unnested SVGs (e.g. laser-cut/profile exports), not a general SVG
// renderer. A file with no closed shapes at all is reported as an error
// rather than silently producing empty geometry.
RawPolygon2D loadSvgPaths(const std::filesystem::path& path);

} // namespace chisel::io
