#pragma once
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace chisel::io {

// ---------------------------------------------------------------------------
// RawOffMesh — triangle-soup OFF data with no GPU/render dependency, safe to
// include from anywhere (including the lightweight chiselcad_tests target),
// mirroring RawStlMesh's shape minus per-triangle normals (OFF's shared
// vertex indices already give CsgEvaluator's import() everything it uses —
// see evalImport(), which never reads RawStlMesh::normals either).
// ---------------------------------------------------------------------------
struct RawOffMesh {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    std::string error; // empty = success
};

// Reads an ASCII OFF (Object File Format) mesh: a header line naming the
// variant (any single token ending in "OFF" — "OFF", "COFF", "NOFF", "4OFF",
// "STOFF", ... — is accepted; this loader always reads a vertex's first 3
// numbers on its line as x/y/z and ignores anything trailing on that same
// line, which is exactly where COFF's per-vertex color / NOFF's per-vertex
// normal / etc. live, so all these variants parse the same way), followed by
// "<numVertices> <numFaces> <numEdges>" (numEdges is read and discarded —
// OFF doesn't actually need it), then that many vertex lines and face lines.
// The header and counts may be split across two lines or share one line
// (both "OFF\n8 6 12\n..." and "OFF 8 6 12\n..." are accepted).
//
// Each face line is "<n> <i0> <i1> ... <i(n-1)> [ignored trailing color
// values]"; n-gon faces (n > 3) are fan-triangulated from their first
// vertex — OFF faces are assumed planar/convex, the same assumption
// polyhedron() makes (see CsgEvaluator::evalPolyhedron). '#' starts a line
// comment anywhere in the file, and blank lines are skipped, matching
// SurfaceLoader's .dat tolerance.
RawOffMesh loadOffMesh(const std::filesystem::path& path);

} // namespace chisel::io
