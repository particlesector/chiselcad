#include "PrimitiveGen.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace chisel::csg {

// ---------------------------------------------------------------------------
// UV sphere matching OpenSCAD's tessellation exactly.
//
// Algorithm from OpenSCAD src/core/primitives.cc:
//   nRings = (fn + 1) / 2          — latitude rings (no explicit pole vertices)
//   phi_i  = 180 * (i + 0.5) / nRings  — ring latitude in degrees (0.5 offset
//                                         keeps rings away from the true poles)
//   theta_j = 360 * j / nLon       — longitude in degrees
//
// Face layout (as polygons before triangulation):
//   Top cap:    ring 0 vertices as a single polygon
//   Quad strips: between adjacent rings, nLon quads each
//   Bottom cap: ring nRings-1 vertices reversed (so normal faces outward)
//
// Vertex index: i * nLon + j
// ---------------------------------------------------------------------------
static manifold::Manifold makeUVSphere(float r, int fn) {
    const int nLon   = fn;
    const int nRings = (fn + 1) / 2;

    constexpr double kPi = 3.14159265358979323846;
    const int nVerts = nRings * nLon;

    manifold::MeshGL mesh;
    mesh.numProp = 3;
    mesh.vertProperties.reserve(static_cast<size_t>(nVerts) * 3);

    for (int i = 0; i < nRings; ++i) {
        double phi = kPi * (i + 0.5) / nRings;   // radians, 0..π
        float z  = r * static_cast<float>(std::cos(phi));
        float rr = r * static_cast<float>(std::sin(phi));
        for (int j = 0; j < nLon; ++j) {
            double theta = 2.0 * kPi * j / nLon;
            mesh.vertProperties.push_back(rr * static_cast<float>(std::cos(theta)));
            mesh.vertProperties.push_back(rr * static_cast<float>(std::sin(theta)));
            mesh.vertProperties.push_back(z);
        }
    }

    // Estimate triangle count:
    //   2*(nLon-2) cap triangles + (nRings-1)*nLon*2 strip triangles
    const size_t estTris = static_cast<size_t>(2*(nLon-2) + (nRings-1)*nLon*2);
    mesh.triVerts.reserve(estTris * 3);

    auto vi = [&](int ring, int lon) -> uint32_t {
        return static_cast<uint32_t>(ring * nLon + ((lon % nLon + nLon) % nLon));
    };

    // Top cap — ring 0, vertices in forward order, fan from vertex 0
    for (int j = 1; j < nLon - 1; ++j) {
        mesh.triVerts.push_back(vi(0, 0));
        mesh.triVerts.push_back(vi(0, j));
        mesh.triVerts.push_back(vi(0, j + 1));
    }

    // Quad strips — between ring i and ring i+1
    // OpenSCAD quad winding: {ring_i[j+1], ring_i[j], ring_{i+1}[j], ring_{i+1}[j+1]}
    // Split into two triangles maintaining outward normals:
    //   T1: (ring_i[j+1], ring_i[j],   ring_{i+1}[j])
    //   T2: (ring_i[j+1], ring_{i+1}[j], ring_{i+1}[j+1])
    for (int i = 0; i < nRings - 1; ++i) {
        for (int j = 0; j < nLon; ++j) {
            uint32_t a = vi(i,     j);
            uint32_t b = vi(i,     j + 1);
            uint32_t c = vi(i + 1, j);
            uint32_t d = vi(i + 1, j + 1);
            // T1
            mesh.triVerts.push_back(b);
            mesh.triVerts.push_back(a);
            mesh.triVerts.push_back(c);
            // T2
            mesh.triVerts.push_back(b);
            mesh.triVerts.push_back(c);
            mesh.triVerts.push_back(d);
        }
    }

    // Bottom cap — ring nRings-1, vertices in REVERSE order (outward normal -z),
    // fan from vertex nRings*nLon-1 (= ring[nRings-1][nLon-1])
    const int lastRing = nRings - 1;
    for (int j = 1; j < nLon - 1; ++j) {
        mesh.triVerts.push_back(vi(lastRing, nLon - 1));
        mesh.triVerts.push_back(vi(lastRing, nLon - 1 - j));
        mesh.triVerts.push_back(vi(lastRing, nLon - 2 - j));
    }

    return manifold::Manifold(mesh);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
double PrimitiveGen::getParam(const std::unordered_map<std::string, double>& p,
                               const std::string& key,
                               double def) {
    auto it = p.find(key);
    return (it != p.end()) ? it->second : def;
}

// Matches OpenSCAD's fn/fs/fa segment formula.
// fn > 0 → use fn directly.
// Otherwise: ceil(max(min(360/fa, 2π·r/fs), 5))
//
// faOverride/fsOverride are this leaf's own $fa=/$fs= arguments (0.0 meaning
// "not given here" — matching fnOverride's existing convention), each
// falling back independently to the global $fa/$fs when absent. Before this
// fix, only a per-node $fn override was honored; $fa/$fs overrides were
// silently ignored in favor of the global values, so e.g.
// `sphere(5, $fa=40, $fs=0.3)` rendered at the *global* default resolution
// instead of the coarser one it explicitly asked for — confirmed against
// real OpenSCAD via volumetric corpus comparison (docs/roadmap.md v3.9).
int PrimitiveGen::resolveSegments(double r, double fnOverride, double faOverride,
                                   double fsOverride) const {
    double fn = (fnOverride > 0.0) ? fnOverride : globalFn;
    if (fn > 0.0) {
        // A non-finite $fn (e.g. `$fn = 1/0`) can't be meaningfully rounded
        // to a segment count: casting an out-of-range double to int is
        // undefined behavior, and empirically this platform's cast yields
        // INT_MAX (not INT_MIN, as `std::max(3, ...)` alone would need to
        // safely clamp), silently building a huge/degenerate mesh instead
        // of erroring. Real OpenSCAD's own equivalent clamps to the minimum
        // of 3 sides here — confirmed against a live export:
        // `cylinder($fn=1/0)` renders as a 3-sided prism (5 facets total).
        if (!std::isfinite(fn))
            return 3;
        return std::max(3, static_cast<int>(std::round(fn)));
    }

    double fa = (faOverride > 0.0) ? faOverride : globalFa;
    double fs = (fsOverride > 0.0) ? fsOverride : globalFs;
    double byAngle = (fa > 0.0) ? 360.0 / fa : 360.0;
    double bySize  = (fs > 0.0 && r > 0.0) ? (2.0 * 3.14159265358979323846 * r / fs) : byAngle;
    int segs = static_cast<int>(std::ceil(std::min(byAngle, bySize)));
    return std::max(segs, 5);
}

// ---------------------------------------------------------------------------
// Generate
// ---------------------------------------------------------------------------
manifold::Manifold PrimitiveGen::generate(const CsgLeaf& leaf) const {
    const auto& p = leaf.params;

    switch (leaf.kind) {
    // ------------------------------------------------------------------
    // Cube — params: "x", "y", "z" (from positional vector) or "size"
    // ------------------------------------------------------------------
    case CsgLeaf::Kind::Cube: {
        double x = getParam(p, "x", 1.0);
        double y = getParam(p, "y", 1.0);
        double z = getParam(p, "z", 1.0);
        // A non-finite dimension (e.g. `cube(1/0)`) isn't a shape Manifold
        // can construct — real OpenSCAD rejects it outright and renders no
        // geometry at all (confirmed live: `cube(1/0)` exports an empty
        // STL, no warning). Without this check, an infinite param sometimes
        // reaches Manifold::Cube() and comes back with a non-empty but
        // garbage/degenerate mesh instead (docs/roadmap.md, issue #88).
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return {};
        return manifold::Manifold::Cube(
            {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
            leaf.center);
    }

    // ------------------------------------------------------------------
    // Sphere — params: "r" (named) or "_pos0" (positional)
    // ------------------------------------------------------------------
    case CsgLeaf::Kind::Sphere: {
        double r = getParam(p, "r", getParam(p, "_pos0", 1.0));
        // See the Cube case above: a non-finite radius (`sphere(1/0)`)
        // renders as nothing in real OpenSCAD, not a degenerate mesh.
        if (!std::isfinite(r))
            return {};
        double fnOvr = getParam(p, "$fn", 0.0);
        double faOvr = getParam(p, "$fa", 0.0);
        double fsOvr = getParam(p, "$fs", 0.0);
        int segs = resolveSegments(r, fnOvr, faOvr, fsOvr);
        if (useManifoldSphere)
            return manifold::Manifold::Sphere(static_cast<float>(r), segs);
        return makeUVSphere(static_cast<float>(r), segs);
    }

    // ------------------------------------------------------------------
    // Cylinder — params: "h", "r" (uniform) or "r1"/"r2" (cone), center
    // ------------------------------------------------------------------
    case CsgLeaf::Kind::Cylinder: {
        double h  = getParam(p, "h",  1.0);
        double r  = getParam(p, "r", -1.0);  // -1 = not set
        // r1/r2 each fall back to the plain r= if given, else independently
        // default to 1.0 — r2 does NOT mirror r1 (confirmed against real
        // OpenSCAD: cylinder(h=5, r1=5) tapers from r1=5 down to r2=1, not a
        // uniform r=5 cylinder — see docs/roadmap.md v3.9).
        //
        // When *both* r and r1 (or r and r2) are given — real OpenSCAD warns
        // "Cylinder parameters ambiguous" but still renders something, not
        // nothing — an explicitly-given r1/r2 wins for its own slot and `r`
        // only fills in the *other*, unspecified slot; `r` does not blanket-
        // override an explicit r1/r2 the way this used to unconditionally do
        // (confirmed against real OpenSCAD: `cylinder(h=5, r=5, r1=0,
        // center=true)` renders the same frustum volume as `cylinder(h=5,
        // r1=5, r2=0)`, i.e. r1 stays 0 — not a uniform r=5 cylinder, which
        // is what checking only `r >= 0.0` here used to produce regardless
        // of an explicit r1/r2 — issue #88's cylinder-tests.scad corpus
        // mismatch).
        double r1 = p.count("r1") ? p.at("r1") : ((r >= 0.0) ? r : 1.0);
        double r2 = p.count("r2") ? p.at("r2") : ((r >= 0.0) ? r : 1.0);
        // See the Cube case above: a non-finite height/radius (e.g.
        // `cylinder(h=10, r1=1, r2=1/0)`) renders as nothing in real
        // OpenSCAD. Without this check, an infinite r2 in particular used
        // to reach Manifold::Cylinder() and silently come back as a
        // degenerate cone collapsed to a point instead of empty/erroring —
        // a real (not just cosmetic) volume bug (issue #88).
        if (!std::isfinite(h) || !std::isfinite(r1) || !std::isfinite(r2))
            return {};
        double fnOvr = getParam(p, "$fn", 0.0);
        double faOvr = getParam(p, "$fa", 0.0);
        double fsOvr = getParam(p, "$fs", 0.0);
        int segs = resolveSegments(std::max(r1, r2), fnOvr, faOvr, fsOvr);
        return manifold::Manifold::Cylinder(
            static_cast<float>(h),
            static_cast<float>(r1),
            static_cast<float>(r2),
            segs,
            leaf.center);
    }

    case CsgLeaf::Kind::Square2D:
    case CsgLeaf::Kind::Circle2D:
    case CsgLeaf::Kind::Polygon2D:
        return {}; // 2-D only — use generateCrossSection() instead

    // ------------------------------------------------------------------
    // Mesh (import()/surface()) / Polyhedron (polyhedron()) — already-
    // resolved triangle mesh from CsgEvaluator (may or may not be vertex-
    // welded — see CsgNode.h). import()/StlLoader in particular hands back
    // triangle-soup data: a duplicated position per triangle vertex, since
    // STL itself has no shared-vertex indexing; polyhedron()'s fan-
    // triangulated faces share indices already but aren't guaranteed to be
    // manifold-clean either. Manifold's boolean/transform ops need genuine
    // manifold topology (shared indices along every interior edge), so
    // MeshGL::Merge() is called below to weld coincident vertices before
    // construction — without it, a soup mesh almost always fails Manifold's
    // manifoldness check and every op on it silently propagates
    // empty/garbage geometry (see MeshEvaluator::checkStatus for the other
    // half of this fix: surfacing that failure as a Diagnostic instead of
    // leaving it silent).
    // ------------------------------------------------------------------
    case CsgLeaf::Kind::Mesh:
    case CsgLeaf::Kind::Polyhedron: {
        static_assert(sizeof(glm::vec3) == 3 * sizeof(float),
                      "glm::vec3 must be a tightly-packed 3-float struct for the bulk copy below");
        manifold::MeshGL mesh;
        mesh.numProp = 3;
        // Bulk copy rather than a per-component push_back loop — imported
        // meshes can be tens of thousands of vertices, unlike the procedural
        // primitives above.
        mesh.vertProperties.resize(leaf.meshPositions.size() * 3);
        if (!leaf.meshPositions.empty())
            std::memcpy(mesh.vertProperties.data(), leaf.meshPositions.data(),
                        leaf.meshPositions.size() * sizeof(glm::vec3));

        // Defensive bounds check: today's loaders (StlLoader/SurfaceLoader)
        // always emit self-consistent indices, but this is the last point
        // before an out-of-range index would be handed to Manifold's MeshGL
        // constructor as raw, unchecked vertex offsets. Drop (rather than
        // clamp) any triangle referencing an out-of-range vertex, since
        // clamping would silently splice in an unrelated vertex instead of
        // just omitting the bad triangle.
        const auto numVerts = static_cast<uint32_t>(leaf.meshPositions.size());
        mesh.triVerts.reserve(leaf.meshIndices.size());
        std::size_t droppedTris = 0;
        for (std::size_t i = 0; i + 2 < leaf.meshIndices.size(); i += 3) {
            uint32_t a = leaf.meshIndices[i], b = leaf.meshIndices[i + 1], c = leaf.meshIndices[i + 2];
            if (a < numVerts && b < numVerts && c < numVerts) {
                mesh.triVerts.push_back(a);
                mesh.triVerts.push_back(b);
                mesh.triVerts.push_back(c);
            } else {
                ++droppedTris;
            }
        }
        // Summarized once per leaf rather than logged per-triangle, so a
        // badly corrupted mesh doesn't flood the log.
        if (droppedTris > 0)
            spdlog::debug("[mesh] dropped {} triangle(s) referencing an out-of-range "
                          "vertex index (mesh has {} vertices)", droppedTris, numVerts);

        // Best-effort weld: merges vertices that are coincident (within
        // tolerance) along open edges, populating mergeFromVert/mergeToVert
        // so the Manifold constructor below builds real shared-vertex
        // topology instead of treating every triangle as disconnected.
        // A harmless no-op if the mesh is already indexed/shared (e.g.
        // SurfaceLoader's output).
        mesh.Merge();

        return manifold::Manifold(mesh);
    }
    }

    return {};
}

// ---------------------------------------------------------------------------
// generateCrossSection — 2-D leaf → Manifold CrossSection
// ---------------------------------------------------------------------------
manifold::CrossSection PrimitiveGen::generateCrossSection(const CsgLeaf& leaf) const {
    const auto& p = leaf.params;

    switch (leaf.kind) {
    case CsgLeaf::Kind::Square2D: {
        double sx = getParam(p, "sx", 1.0);
        double sy = getParam(p, "sy", 1.0);
        return manifold::CrossSection::Square(
            {static_cast<float>(sx), static_cast<float>(sy)}, leaf.center);
    }

    case CsgLeaf::Kind::Circle2D: {
        double r    = getParam(p, "r", getParam(p, "_pos0", 1.0));
        double fnOvr = getParam(p, "$fn", 0.0);
        double faOvr = getParam(p, "$fa", 0.0);
        double fsOvr = getParam(p, "$fs", 0.0);
        int    segs  = resolveSegments(r, fnOvr, faOvr, fsOvr);
        return manifold::CrossSection::Circle(static_cast<float>(r), segs);
    }

    case CsgLeaf::Kind::Polygon2D: {
        manifold::Polygons polys;
        if (!leaf.polyPaths.empty()) {
            for (const auto& path : leaf.polyPaths) {
                manifold::SimplePolygon contour;
                contour.reserve(path.size());
                for (int idx : path) {
                    if (idx >= 0 && static_cast<size_t>(idx) < leaf.polyPoints.size())
                        contour.push_back({leaf.polyPoints[idx].x,
                                           leaf.polyPoints[idx].y});
                }
                if (!contour.empty()) polys.push_back(std::move(contour));
            }
        } else {
            manifold::SimplePolygon contour;
            contour.reserve(leaf.polyPoints.size());
            for (const auto& pt : leaf.polyPoints)
                contour.push_back({pt.x, pt.y});
            if (!contour.empty()) polys.push_back(std::move(contour));
        }
        // EvenOdd matches OpenSCAD's polygon fill rule: nested same-winding
        // contours create holes (inner area has even crossing count → outside).
        return polys.empty() ? manifold::CrossSection{}
                             : manifold::CrossSection(polys, manifold::CrossSection::FillRule::EvenOdd);
    }

    default:
        return {}; // 3-D primitive — no 2-D representation
    }
}

} // namespace chisel::csg
