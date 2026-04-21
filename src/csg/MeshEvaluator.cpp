#include "MeshEvaluator.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace chisel::csg {

// ---------------------------------------------------------------------------
// Cache key helpers
// ---------------------------------------------------------------------------
static std::string fmtFloat(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

// Serialize a leaf into a deterministic string key.
// The key encodes kind, all params (sorted), center, and the 16 matrix
// elements so that any change to geometry or placement is detected.
static std::string leafKey(const CsgLeaf& leaf) {
    std::string k;
    k.reserve(256);

    switch (leaf.kind) {
    case CsgLeaf::Kind::Cube:     k += "cube:";     break;
    case CsgLeaf::Kind::Sphere:   k += "sphere:";   break;
    case CsgLeaf::Kind::Cylinder: k += "cylinder:"; break;
    }

    // Sort params for a stable key
    std::vector<std::pair<std::string, double>> sorted(leaf.params.begin(), leaf.params.end());
    std::sort(sorted.begin(), sorted.end());
    for (const auto& [name, val] : sorted) {
        k += name; k += '='; k += fmtFloat(val); k += ':';
    }

    k += leaf.center ? "c1:" : "c0:";

    // Transform — 4 columns, each 4 rows
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            k += fmtFloat(static_cast<double>(leaf.transform[col][row]));
            k += ',';
        }
    return k;
}

// ---------------------------------------------------------------------------
// toAffine: glm::mat4 → manifold::mat3x4
// Manifold::Transform expects manifold's own linalg type (mat3x4 = 3 rows,
// 4 columns, column-major), not glm::mat4x3. We copy element-by-element to
// avoid any type punning. The homogeneous bottom row is dropped.
// ---------------------------------------------------------------------------
static manifold::mat3x4 toAffine(const glm::mat4& m) {
    manifold::mat3x4 r;
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 3; ++row)
            r[col][row] = m[col][row];
    return r;
}

// ---------------------------------------------------------------------------
// MeshEvaluator
// ---------------------------------------------------------------------------
MeshEvaluator::MeshEvaluator(MeshCache& cache)
    : m_cache(cache) {}

manifold::Manifold MeshEvaluator::evaluate(const CsgScene& scene) {
    PrimitiveGen gen;
    gen.globalFn           = scene.globalFn;
    gen.globalFs           = scene.globalFs;
    gen.globalFa           = scene.globalFa;
    gen.useManifoldSphere  = useManifoldSphere;

    if (scene.roots.empty())
        return {};

    if (scene.roots.size() == 1)
        return evalNode(*scene.roots[0], gen);

    // Multiple roots → union (OpenSCAD semantics)
    manifold::Manifold result = evalNode(*scene.roots[0], gen);
    for (std::size_t i = 1; i < scene.roots.size(); ++i)
        result = result + evalNode(*scene.roots[i], gen);
    return result;
}

manifold::Manifold MeshEvaluator::evalNode(const CsgNode& node, const PrimitiveGen& gen) {
    return std::visit([&](const auto& n) -> manifold::Manifold {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, CsgLeaf>)
            return evalLeaf(n, gen);
        else
            return evalBoolean(n, gen);
    }, node);
}

manifold::Manifold MeshEvaluator::evalLeaf(const CsgLeaf& leaf, const PrimitiveGen& gen) {
    const std::string key = leafKey(leaf);
    return m_cache.getOrCompute(key, [&]() {
        manifold::Manifold mesh = gen.generate(leaf);
        // Apply the accumulated transform (mat4 → mat4x3 affine)
        if (leaf.transform != glm::mat4{1.0f})
            mesh = mesh.Transform(toAffine(leaf.transform));
        return mesh;
    });
}

manifold::Manifold MeshEvaluator::evalBoolean(const CsgBoolean& b, const PrimitiveGen& gen) {
    if (b.children.empty())
        return {};

    // Hull takes all children at once in local space, then applies the stored transform
    if (b.op == CsgBoolean::Op::Hull) {
        std::vector<manifold::Manifold> meshes;
        meshes.reserve(b.children.size());
        for (const auto& child : b.children)
            meshes.push_back(evalNode(*child, gen));
        auto result = manifold::Manifold::Hull(meshes);
        return result.Transform(toAffine(b.transform));
    }

    manifold::Manifold result = evalNode(*b.children[0], gen);

    for (std::size_t i = 1; i < b.children.size(); ++i) {
        manifold::Manifold child = evalNode(*b.children[i], gen);
        switch (b.op) {
        case CsgBoolean::Op::Union:        result = result + child;                 break;
        case CsgBoolean::Op::Difference:   result = result - child;                 break;
        case CsgBoolean::Op::Intersection: result = result ^ child;                 break;
        case CsgBoolean::Op::Minkowski:    result = result.MinkowskiSum(child);     break;
        case CsgBoolean::Op::Hull:         break; // handled above
        }
    }

    // Minkowski is also a local-space op — apply the stored outer transform once
    if (b.op == CsgBoolean::Op::Minkowski)
        result = result.Transform(toAffine(b.transform));

    return result;
}

} // namespace chisel::csg
