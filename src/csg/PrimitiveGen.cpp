#include "PrimitiveGen.h"
#include <algorithm>
#include <cmath>
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
int PrimitiveGen::resolveSegments(double r, double fnOverride) const {
    double fn = (fnOverride > 0.0) ? fnOverride : globalFn;
    if (fn > 0.0)
        return std::max(3, static_cast<int>(std::round(fn)));

    double byAngle = (globalFa > 0.0) ? 360.0 / globalFa : 360.0;
    double bySize  = (globalFs > 0.0 && r > 0.0) ? (2.0 * 3.14159265358979323846 * r / globalFs) : byAngle;
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
        return manifold::Manifold::Cube(
            {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
            leaf.center);
    }

    // ------------------------------------------------------------------
    // Sphere — params: "r" (named) or "_pos0" (positional)
    // ------------------------------------------------------------------
    case CsgLeaf::Kind::Sphere: {
        double r = getParam(p, "r", getParam(p, "_pos0", 1.0));
        double fnOvr = getParam(p, "$fn", 0.0);
        int segs = resolveSegments(r, fnOvr);
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
        double r1 = (r >= 0.0) ? r : getParam(p, "r1", 1.0);
        double r2 = (r >= 0.0) ? r : getParam(p, "r2", r1);
        double fnOvr = getParam(p, "$fn", 0.0);
        int segs = resolveSegments(std::max(r1, r2), fnOvr);
        return manifold::Manifold::Cylinder(
            static_cast<float>(h),
            static_cast<float>(r1),
            static_cast<float>(r2),
            segs,
            leaf.center);
    }
    }

    return {}; // unreachable
}

} // namespace chisel::csg
