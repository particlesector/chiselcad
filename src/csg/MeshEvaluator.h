#pragma once
#include "CsgNode.h"
#include "MeshCache.h"
#include "PrimitiveGen.h"
#include <manifold/manifold.h>
#include <vector>

namespace chisel::csg {

// ---------------------------------------------------------------------------
// MeshEvaluator — converts a CsgScene into Manifold meshes by evaluating
// the CSG tree bottom-up using the Manifold boolean engine.
//
// Each root node is evaluated independently and returned as a separate
// Manifold so that objects inside other objects remain visible (matching
// OpenSCAD preview semantics). The caller concatenates vertex buffers.
// Results for individual subtrees are cached in MeshCache to avoid
// recomputing unchanged geometry across reloads.
// ---------------------------------------------------------------------------
class MeshEvaluator {
public:
    explicit MeshEvaluator(MeshCache& cache);

    // Use Manifold's built-in sphere instead of the OpenSCAD-compatible UV sphere.
    bool useManifoldSphere = false;

    // Evaluate the full scene; returns one Manifold per root node.
    std::vector<manifold::Manifold> evaluate(const CsgScene& scene);

private:
    manifold::Manifold evalNode(const CsgNode& node, const PrimitiveGen& gen);
    manifold::Manifold evalLeaf(const CsgLeaf& leaf, const PrimitiveGen& gen);
    manifold::Manifold evalBoolean(const CsgBoolean& b, const PrimitiveGen& gen);

    MeshCache& m_cache;
};

} // namespace chisel::csg
