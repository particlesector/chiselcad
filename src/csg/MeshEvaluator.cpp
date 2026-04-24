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
    case CsgLeaf::Kind::Cube:      k += "cube:";      break;
    case CsgLeaf::Kind::Sphere:    k += "sphere:";    break;
    case CsgLeaf::Kind::Cylinder:  k += "cylinder:";  break;
    case CsgLeaf::Kind::Square2D:  k += "square2d:";  break;
    case CsgLeaf::Kind::Circle2D:  k += "circle2d:";  break;
    case CsgLeaf::Kind::Polygon2D: k += "polygon2d:"; break;
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

std::vector<manifold::Manifold> MeshEvaluator::evaluate(const CsgScene& scene) {
    PrimitiveGen gen;
    gen.globalFn           = scene.globalFn;
    gen.globalFs           = scene.globalFs;
    gen.globalFa           = scene.globalFa;
    gen.useManifoldSphere  = useManifoldSphere;

    std::vector<manifold::Manifold> result;
    result.reserve(scene.roots.size());
    for (const auto& root : scene.roots)
        result.push_back(evalNode(*root, gen));
    return result;
}

manifold::Manifold MeshEvaluator::evalNode(const CsgNode& node, const PrimitiveGen& gen) {
    return std::visit([&](const auto& n) -> manifold::Manifold {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, CsgLeaf>)
            return evalLeaf(n, gen);
        else if constexpr (std::is_same_v<T, CsgBoolean>)
            return evalBoolean(n, gen);
        else
            return evalExtrusion(n, gen);
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

// ---------------------------------------------------------------------------
// getChildCrossSection — recursively build a CrossSection from a 2-D subtree.
// Handles CsgLeaf (2-D kinds) and CsgBoolean (union/difference/intersection
// of 2-D children).  3-D leaves produce an empty CrossSection.
// ---------------------------------------------------------------------------
manifold::CrossSection MeshEvaluator::getChildCrossSection(const CsgNode& node,
                                                            const PrimitiveGen& gen) {
    return std::visit([&](const auto& n) -> manifold::CrossSection {
        using T = std::decay_t<decltype(n)>;

        if constexpr (std::is_same_v<T, CsgLeaf>) {
            auto cs = gen.generateCrossSection(n);
            // Apply the 2-D portion of the accumulated transform so that
            // translate/rotate applied to 2-D children is respected.
            if (n.transform != glm::mat4{1.0f}) {
                manifold::mat2x3 m2;
                m2[0][0] = n.transform[0][0]; m2[0][1] = n.transform[0][1];
                m2[1][0] = n.transform[1][0]; m2[1][1] = n.transform[1][1];
                m2[2][0] = n.transform[3][0]; m2[2][1] = n.transform[3][1];
                cs = cs.Transform(m2);
            }
            return cs;
        } else if constexpr (std::is_same_v<T, CsgBoolean>) {
            if (n.children.empty()) return {};
            auto result = getChildCrossSection(*n.children[0], gen);
            for (std::size_t i = 1; i < n.children.size(); ++i) {
                auto child = getChildCrossSection(*n.children[i], gen);
                switch (n.op) {
                case CsgBoolean::Op::Union:        result = result + child; break;
                case CsgBoolean::Op::Difference:   result = result - child; break;
                case CsgBoolean::Op::Intersection: result = result ^ child; break;
                default: result = result + child; break; // hull/minkowski → union
                }
            }
            return result;
        } else {
            return {}; // nested extrusion — not supported
        }
    }, node);
}

// ---------------------------------------------------------------------------
// evalExtrusion — convert a CsgExtrusion node to a 3-D Manifold
// ---------------------------------------------------------------------------
manifold::Manifold MeshEvaluator::evalExtrusion(const CsgExtrusion& e,
                                                  const PrimitiveGen& gen) {
    // Build the unified 2-D cross-section from all children
    manifold::CrossSection cs;
    for (const auto& child : e.children)
        cs = cs + getChildCrossSection(*child, gen);

    if (cs.IsEmpty()) return {};

    auto getP = [&](const std::string& k, double def) -> double {
        auto it = e.params.find(k);
        return (it != e.params.end()) ? it->second : def;
    };

    manifold::Manifold result;

    if (e.kind == CsgExtrusion::Kind::Linear) {
        double height = getP("height", getP("h", getP("_pos0", 1.0)));
        double twist  = -getP("twist",  0.0); // OpenSCAD uses left-hand rule; Manifold uses right-hand
        float  scaleX = static_cast<float>(getP("scale_x", 1.0));
        float  scaleY = static_cast<float>(getP("scale_y", 1.0));
        double fnOvr  = getP("$fn",   0.0);
        int    nDivs  = (twist != 0.0 || scaleX != 1.0f || scaleY != 1.0f)
                        ? std::max(1, static_cast<int>(fnOvr > 0 ? fnOvr : 10))
                        : 0;
        bool   center = (getP("center", 0.0) != 0.0);

        result = manifold::Manifold::Extrude(
            cs.ToPolygons(),
            static_cast<float>(height),
            nDivs,
            static_cast<float>(twist),
            {scaleX, scaleY});

        if (center)
            result = result.Translate({0.0f, 0.0f,
                                       -static_cast<float>(height) * 0.5f});
    } else {
        // rotate_extrude
        double angle = getP("angle", 360.0);
        double fnOvr = getP("$fn", 0.0);
        int    segs  = gen.resolveSegments(10.0, fnOvr); // 10 = proxy radius

        result = manifold::Manifold::Revolve(
            cs.ToPolygons(),
            segs,
            static_cast<float>(angle));
    }

    // Apply the outer 3-D world transform
    if (e.transform != glm::mat4{1.0f})
        result = result.Transform(toAffine(e.transform));

    return result;
}

} // namespace chisel::csg
