#pragma once
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace chisel::io {

// ---------------------------------------------------------------------------
// RawSurfaceMesh — a closed, watertight solid built from a heightmap: a top
// surface following the file's height values, vertical side walls, and a
// flat base — no GPU/render dependency (mirrors RawStlMesh), safe to include
// from the lightweight chiselcad_tests target.
// ---------------------------------------------------------------------------
struct RawSurfaceMesh {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t>  indices;
    std::string            error; // empty = success
};

// Reads a plain-text heightmap file (OpenSCAD's .dat format: one row of
// whitespace-separated height values per line, '#' starts a line comment,
// blank/comment-only lines are skipped, every row must have the same column
// count) and builds a solid: grid column c maps to x = c, grid row r maps to
// y = (numRows-1-r) — i.e. the *first* line in the file is the "far"
// (maximum Y) edge. The solid's base sits at z = min(0, minHeight) (not
// always exactly z=0) specifically so that heightmaps containing negative
// values don't fold the base up through the top surface and self-intersect.
//
// `invert` flips heights vertically about the grid's own maximum
// (h' = max - h, so peaks become valleys); `center` shifts the whole solid
// so its X/Y footprint AND its Z extent are centered on the origin.
RawSurfaceMesh loadSurfaceMesh(const std::filesystem::path& path, bool center, bool invert);

} // namespace chisel::io
