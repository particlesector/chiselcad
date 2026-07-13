#include "MeshEvaluator.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace chisel::csg {

// ---------------------------------------------------------------------------
// Cache key helpers
// ---------------------------------------------------------------------------
static std::string fmtFloat(double v) {
    // %.17g is round-trip-safe for a double (17 significant digits is the
    // maximum needed to uniquely recover any double from its decimal text)
    // — %.6g previously used here could give two distinct values the same
    // cache key, causing MeshCache to return the wrong cached mesh.
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

// Order-sensitive hash combine (boost-style), used below to fold arbitrarily
// large geometry data (polygon points, imported mesh vertices) into a fixed-
// size digest for the cache key — spelling every coordinate out as text
// would make the key computation itself slow to recompute on every rebuild
// for large imported meshes.
static void hashCombine(std::size_t& seed, std::size_t v) {
    seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

static std::string fmtHash(std::size_t h) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%zx", h);
    return buf;
}

// Serialize a leaf into a deterministic string key.
// The key encodes kind, all params (sorted), center, and the 16 matrix
// elements so that any change to geometry or placement is detected. It also
// folds in every input from `gen` that generate()/generateCrossSection()
// can consult besides the leaf itself (global $fn/$fs/$fa, which affect
// resolveSegments() for Sphere/Cylinder when no per-leaf override is given,
// and useManifoldSphere, which picks between two different sphere
// tessellators) — required now that MeshCache is hoisted to persist across
// rebuilds (see MeshBuilder): a leaf with identical params can legitimately
// need a different mesh across two builds if one of these globals changed,
// and without them in the key a persistent cache would return a stale mesh.
static std::string leafKey(const CsgLeaf& leaf, const PrimitiveGen& gen) {
    std::string k;
    k.reserve(256);

    k += gen.useManifoldSphere ? "ms1:" : "ms0:";
    k += fmtFloat(gen.globalFn); k += ':';
    k += fmtFloat(gen.globalFs); k += ':';
    k += fmtFloat(gen.globalFa); k += ':';

    switch (leaf.kind) {
    case CsgLeaf::Kind::Cube:      k += "cube:";      break;
    case CsgLeaf::Kind::Sphere:    k += "sphere:";    break;
    case CsgLeaf::Kind::Cylinder:  k += "cylinder:";  break;
    case CsgLeaf::Kind::Square2D:  k += "square2d:";  break;
    case CsgLeaf::Kind::Circle2D:  k += "circle2d:";  break;

    // Polygon2D/Mesh carry their actual geometry outside `params` (in
    // polyPoints/polyPaths or meshPositions/meshIndices), so — unlike the
    // parametric primitives above — the key must fold that data in too, or
    // e.g. two different polygon()s / two different imported files with the
    // same transform would collide in MeshCache and silently swap geometry.
    case CsgLeaf::Kind::Polygon2D: {
        k += "polygon2d:";
        std::size_t h = 0;
        for (const auto& pt : leaf.polyPoints) {
            hashCombine(h, std::hash<float>{}(pt.x));
            hashCombine(h, std::hash<float>{}(pt.y));
        }
        for (const auto& path : leaf.polyPaths)
            for (int idx : path)
                hashCombine(h, std::hash<int>{}(idx));
        k += fmtHash(h);
        k += ':';
        break;
    }

    case CsgLeaf::Kind::Mesh: {
        k += "mesh:";
        std::size_t h = 0;
        for (const auto& pos : leaf.meshPositions) {
            hashCombine(h, std::hash<float>{}(pos.x));
            hashCombine(h, std::hash<float>{}(pos.y));
            hashCombine(h, std::hash<float>{}(pos.z));
        }
        for (uint32_t idx : leaf.meshIndices)
            hashCombine(h, std::hash<uint32_t>{}(idx));
        k += fmtHash(h);
        k += ':';
        break;
    }
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

// Apply the 2-D portion (rotation/scale/shear in XY, plus XY translation)
// of a 4x4 world transform to a CrossSection. A no-op when transform is
// identity, so callers can apply this unconditionally.
static manifold::CrossSection apply2DTransform(manifold::CrossSection cs,
                                                const glm::mat4& transform) {
    if (transform == glm::mat4{1.0f}) return cs;
    manifold::mat2x3 m2;
    m2[0][0] = transform[0][0]; m2[0][1] = transform[0][1];
    m2[1][0] = transform[1][0]; m2[1][1] = transform[1][1];
    m2[2][0] = transform[3][0]; m2[2][1] = transform[3][1];
    return cs.Transform(m2);
}

// Human-readable name for a Manifold::Error status, used in diagnostics.
static const char* manifoldErrorName(manifold::Manifold::Error e) {
    using E = manifold::Manifold::Error;
    switch (e) {
    case E::NoError:                      return "NoError";
    case E::NonFiniteVertex:              return "NonFiniteVertex";
    case E::NotManifold:                  return "NotManifold";
    case E::VertexOutOfBounds:            return "VertexOutOfBounds";
    case E::PropertiesWrongLength:        return "PropertiesWrongLength";
    case E::MissingPositionProperties:    return "MissingPositionProperties";
    case E::MergeVectorsDifferentLengths: return "MergeVectorsDifferentLengths";
    case E::MergeIndexOutOfBounds:        return "MergeIndexOutOfBounds";
    case E::TransformWrongLength:         return "TransformWrongLength";
    case E::RunIndexWrongLength:          return "RunIndexWrongLength";
    case E::FaceIDWrongLength:            return "FaceIDWrongLength";
    case E::InvalidConstruction:          return "InvalidConstruction";
    default:                              return "unknown error";
    }
}

static const char* leafKindName(CsgLeaf::Kind k) {
    switch (k) {
    case CsgLeaf::Kind::Cube:      return "cube()";
    case CsgLeaf::Kind::Sphere:    return "sphere()";
    case CsgLeaf::Kind::Cylinder:  return "cylinder()";
    case CsgLeaf::Kind::Square2D:  return "square()";
    case CsgLeaf::Kind::Circle2D:  return "circle()";
    case CsgLeaf::Kind::Polygon2D: return "polygon()";
    case CsgLeaf::Kind::Mesh:      return "import()/surface()";
    }
    return "leaf";
}

static const char* booleanOpName(CsgBoolean::Op op) {
    switch (op) {
    case CsgBoolean::Op::Union:        return "union()";
    case CsgBoolean::Op::Difference:   return "difference()";
    case CsgBoolean::Op::Intersection: return "intersection()";
    case CsgBoolean::Op::Hull:         return "hull()";
    case CsgBoolean::Op::Minkowski:    return "minkowski()";
    }
    return "boolean op";
}

// ---------------------------------------------------------------------------
// MeshEvaluator
// ---------------------------------------------------------------------------
MeshEvaluator::MeshEvaluator(MeshCache& cache)
    : m_cache(cache) {}

manifold::Manifold MeshEvaluator::checkStatus(manifold::Manifold m, const std::string& context) {
    auto status = m.Status();
    if (status != manifold::Manifold::Error::NoError) {
        chisel::lang::Diagnostic d;
        d.level   = chisel::lang::DiagLevel::Error;
        d.message = context + ": invalid geometry (" + manifoldErrorName(status) +
                    "); result may be empty or degenerate";
        m_diags.push_back(std::move(d));
    }
    return m;
}

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
        else if constexpr (std::is_same_v<T, CsgExtrusion>)
            return evalExtrusion(n, gen);
        else
            return {}; // CsgOffset/CsgProjection: 2-D only, no 3-D representation unless extruded
    }, node);
}

manifold::Manifold MeshEvaluator::evalLeaf(const CsgLeaf& leaf, const PrimitiveGen& gen) {
    const std::string key = leafKey(leaf, gen);
    manifold::Manifold mesh = m_cache.getOrCompute(key, [&]() {
        manifold::Manifold mesh = gen.generate(leaf);
        // Apply the accumulated transform (mat4 → mat4x3 affine)
        if (leaf.transform != glm::mat4{1.0f})
            mesh = mesh.Transform(toAffine(leaf.transform));
        return mesh;
    });
    // Checked on every call (not just on a cache miss) so a status problem
    // is reported for every build that includes this leaf, not just the
    // first one that happened to compute it.
    return checkStatus(std::move(mesh), leafKindName(leaf.kind));
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
        return checkStatus(result.Transform(toAffine(b.transform)), booleanOpName(b.op));
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

    return checkStatus(std::move(result), booleanOpName(b.op));
}

// ---------------------------------------------------------------------------
// getChildCrossSection — recursively build a CrossSection from a 2-D subtree.
// Handles CsgLeaf (2-D kinds), CsgBoolean (union/difference/intersection of
// 2-D children), CsgOffset (grow/shrink), and CsgProjection (3-D → 2-D).
// 3-D leaves produce an empty CrossSection.
// ---------------------------------------------------------------------------
manifold::CrossSection MeshEvaluator::getChildCrossSection(const CsgNode& node,
                                                            const PrimitiveGen& gen) {
    return std::visit([&](const auto& n) -> manifold::CrossSection {
        using T = std::decay_t<decltype(n)>;

        if constexpr (std::is_same_v<T, CsgLeaf>) {
            // Apply the 2-D portion of the accumulated transform so that
            // translate/rotate applied to 2-D children is respected.
            return apply2DTransform(gen.generateCrossSection(n), n.transform);
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
        } else if constexpr (std::is_same_v<T, CsgOffset>) {
            manifold::CrossSection cs;
            for (const auto& child : n.children)
                cs = cs + getChildCrossSection(*child, gen);

            auto getP = [&](const std::string& k, double def) -> double {
                auto it = n.params.find(k);
                return (it != n.params.end()) ? it->second : def;
            };

            if (n.params.count("r")) {
                double r     = getP("r", 0.0);
                double fnOvr = getP("$fn", 0.0);
                int    segs  = gen.resolveSegments(std::abs(r), fnOvr);
                cs = cs.Offset(r, manifold::CrossSection::JoinType::Round, 2.0, segs);
            } else if (n.params.count("delta")) {
                double delta   = getP("delta", 0.0);
                bool   chamfer = getP("chamfer", 0.0) != 0.0;
                cs = cs.Offset(delta, chamfer ? manifold::CrossSection::JoinType::Bevel
                                              : manifold::CrossSection::JoinType::Miter);
            }

            return apply2DTransform(cs, n.transform);
        } else if constexpr (std::is_same_v<T, CsgProjection>) {
            if (n.children.empty()) return {};

            // Union all 3-D children (each already carries its own local
            // transform from CsgEvaluator) into one solid, then flatten it.
            manifold::Manifold solid = evalNode(*n.children[0], gen);
            for (std::size_t i = 1; i < n.children.size(); ++i)
                solid = solid + evalNode(*n.children[i], gen);

            manifold::Polygons polys = n.cut ? solid.Slice(0.0) : solid.Project();
            auto cs = polys.empty() ? manifold::CrossSection{}
                                    : manifold::CrossSection(polys, manifold::CrossSection::FillRule::EvenOdd);
            return apply2DTransform(cs, n.transform);
        } else {
            // Nested extrusion (e.g. linear_extrude() rotate_extrude() ...)
            // is not currently supported. Flag it explicitly rather than
            // silently returning empty geometry, so the caller sees a
            // Diagnostic instead of unexplained missing output.
            chisel::lang::Diagnostic d;
            d.level   = chisel::lang::DiagLevel::Error;
            d.message = "nested extrusion (a linear_extrude()/rotate_extrude() "
                        "inside another extrude) is not supported; its geometry "
                        "was skipped";
            m_diags.push_back(std::move(d));
            return {};
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
        // Divisions are only needed for twist (to smoothly interpolate the
        // rotation). A plain scale taper works correctly with 0 divisions.
        int    nDivs  = (twist != 0.0)
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

        manifold::Polygons polys = cs.ToPolygons();

        // OpenSCAD requires the 2-D profile not cross the Y axis (Manifold's
        // X axis here) — revolving a profile that straddles the rotation
        // axis produces self-intersecting/degenerate geometry. Check before
        // handing off to Revolve() rather than passing bad input through
        // silently.
        constexpr double kAxisEps = 1e-4;
        bool crossesAxis = false;
        for (const auto& poly : polys) {
            for (const auto& pt : poly) {
                if (pt.x < -kAxisEps) { crossesAxis = true; break; }
            }
            if (crossesAxis) break;
        }

        if (crossesAxis) {
            chisel::lang::Diagnostic d;
            d.level   = chisel::lang::DiagLevel::Error;
            d.message = "rotate_extrude(): profile crosses the rotation axis "
                        "(all points must satisfy x >= 0); geometry skipped";
            m_diags.push_back(std::move(d));
            return {};
        }

        result = manifold::Manifold::Revolve(
            polys,
            segs,
            static_cast<float>(angle));
    }

    // Apply the outer 3-D world transform
    if (e.transform != glm::mat4{1.0f})
        result = result.Transform(toAffine(e.transform));

    return checkStatus(std::move(result),
                        e.kind == CsgExtrusion::Kind::Linear ? "linear_extrude()" : "rotate_extrude()");
}

} // namespace chisel::csg
