#pragma once
#include "RawPolygon2D.h"

#include <filesystem>
#include <string>

namespace chisel::io {

// Reads an ASCII DXF file's ENTITIES section and returns one closed 2-D
// contour per closed entity found:
//   - LWPOLYLINE / POLYLINE with the "closed" flag set (group 70, bit 1)
//   - CIRCLE (tessellated into a fixed-resolution polygon — see
//     kCircleSegments in DxfLoader.cpp; there's no $fn to inherit from the
//     calling .scad file through import(), so this uses one fixed value,
//     same simplification surface()/text() already make in other spots)
//
// Bare LINE/ARC entities and open (non-closed) LWPOLYLINE/POLYLINE entities
// are skipped — this loader does NOT perform DXF's general open-edge-graph
// reconstruction (joining disconnected line/arc segments into polygons by
// shared endpoints), unlike full OpenSCAD/CAD software. It covers the
// common case of a DXF authored as one or more already-closed outlines
// (e.g. laser-cut/profile exports), not arbitrary line-segment soups. A
// file with no closed entities at all is reported as an error rather than
// silently producing empty geometry.
//
// `layer`, if non-empty, restricts to entities on that DXF layer (group
// code 8) — matching OpenSCAD's import(..., layer="...") for DXF; empty
// (the default) imports every layer.
RawPolygon2D loadDxfPaths(const std::filesystem::path& path, const std::string& layer = "");

} // namespace chisel::io
