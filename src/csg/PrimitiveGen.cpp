#include "PrimitiveGen.h"
#include <algorithm>
#include <cmath>

namespace chisel::csg {

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
    double bySize  = (globalFs > 0.0 && r > 0.0) ? (2.0 * M_PI * r / globalFs) : byAngle;
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
        return manifold::Manifold::Sphere(static_cast<float>(r), segs);
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
