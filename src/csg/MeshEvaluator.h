#pragma once
#include "CsgNode.h"
#include "MeshCache.h"
#include "PrimitiveGen.h"
#include "lang/Diagnostic.h"
#include <manifold/manifold.h>
#include <manifold/cross_section.h>
#include <vector>

namespace chisel::csg {

// ---------------------------------------------------------------------------
// MeshEvaluator — converts a CsgScene into Manifold meshes by evaluating
// the CSG tree bottom-up using the Manifold boolean engine.
//
// Each root node is evaluated independently and returned as a separate
// Manifold so that objects inside other objects remain visible (matching
// OpenSCAD preview semantics). The caller concatenates vertex buffers.
// Results for individual subtrees are cached in the caller-owned MeshCache
// (see MeshBuilder, which keeps one alive across rebuilds) to avoid
// recomputing unchanged geometry across reloads.
// ---------------------------------------------------------------------------
class MeshEvaluator {
public:
    explicit MeshEvaluator(MeshCache& cache);

    // Use Manifold's built-in sphere instead of the OpenSCAD-compatible UV sphere.
    bool useManifoldSphere = false;

    // Evaluate the full scene; returns one Manifold per root node.
    std::vector<manifold::Manifold> evaluate(const CsgScene& scene);

    // Diagnostics raised during mesh evaluation itself (as opposed to CSG
    // evaluation, which reports through CsgScene::evalDiags) — e.g. a
    // rotate_extrude() profile crossing the rotation axis, or a nested
    // extrusion that isn't supported. Populated by evaluate(); the caller
    // (MeshBuilder) forwards these into BuildResult::diags.
    const std::vector<chisel::lang::Diagnostic>& diagnostics() const { return m_diags; }

private:
    manifold::Manifold    evalNode(const CsgNode& node, const PrimitiveGen& gen);
    manifold::Manifold    evalLeaf(const CsgLeaf& leaf, const PrimitiveGen& gen);
    manifold::Manifold    evalBoolean(const CsgBoolean& b, const PrimitiveGen& gen);
    manifold::Manifold    evalExtrusion(const CsgExtrusion& e, const PrimitiveGen& gen);
    manifold::CrossSection getChildCrossSection(const CsgNode& node, const PrimitiveGen& gen);

    // Manifold's boolean/transform/extrude/construction operations don't
    // throw on invalid input (e.g. a non-manifold imported mesh) — they
    // silently propagate an empty/degenerate result. This is the only way
    // the caller learns something went wrong instead of getting unexplained
    // missing/broken geometry: if `m` isn't a valid manifold, records a
    // Diagnostic naming what produced it. `m` is returned unchanged either
    // way (a non-NoError status doesn't necessarily mean totally unusable
    // geometry, so this doesn't discard it — it just makes the failure visible).
    manifold::Manifold checkStatus(manifold::Manifold m, const std::string& context);

    MeshCache& m_cache;
    std::vector<chisel::lang::Diagnostic> m_diags;
};

} // namespace chisel::csg
