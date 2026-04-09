#pragma once
#include "CsgNode.h"
#include "MeshCache.h"
#include "PrimitiveGen.h"
#include <manifold/manifold.h>

namespace chisel::csg {

// ---------------------------------------------------------------------------
// MeshEvaluator — converts a CsgScene into a single Manifold by evaluating
// the CSG tree bottom-up using the Manifold boolean engine.
//
// Multiple root nodes are implicitly unioned (OpenSCAD semantics).
// Results for individual subtrees are cached in MeshCache to avoid
// recomputing unchanged geometry across reloads.
// ---------------------------------------------------------------------------
class MeshEvaluator {
public:
    explicit MeshEvaluator(MeshCache& cache);

    // Evaluate the full scene; returns the combined mesh.
    manifold::Manifold evaluate(const CsgScene& scene);

private:
    manifold::Manifold evalNode(const CsgNode& node, const PrimitiveGen& gen);
    manifold::Manifold evalLeaf(const CsgLeaf& leaf, const PrimitiveGen& gen);
    manifold::Manifold evalBoolean(const CsgBoolean& b, const PrimitiveGen& gen);

    MeshCache& m_cache;
};

} // namespace chisel::csg
