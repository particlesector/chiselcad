#pragma once
#include "CsgNode.h"
#include <manifold/manifold.h>

namespace chisel::csg {

// ---------------------------------------------------------------------------
// PrimitiveGen — tessellates a CsgLeaf into a Manifold mesh.
//
// All parameters are read from the leaf's params map using the same names
// the parser stores: "x"/"y"/"z" for cube size, "r"/"r1"/"r2"/"h" for
// cylinder, "r" or "_pos0" for sphere.  Per-node $fn/$fs/$fa overrides
// are also respected via the params map.
// ---------------------------------------------------------------------------
struct PrimitiveGen {
    // Global resolution settings (from ParseResult / CsgScene)
    double globalFn = 0.0;
    double globalFs = 2.0;
    double globalFa = 12.0;

    // Generate the untransformed Manifold for a leaf.
    // The caller (MeshEvaluator) applies leaf.transform afterwards.
    manifold::Manifold generate(const CsgLeaf& leaf) const;

    // Compute how many circular segments to use for a feature of radius r,
    // respecting $fn / $fs / $fa in the same way OpenSCAD does.
    int resolveSegments(double r, double fnOverride) const;

private:
    static double getParam(const std::unordered_map<std::string, double>& p,
                           const std::string& key,
                           double def = 0.0);
};

} // namespace chisel::csg
