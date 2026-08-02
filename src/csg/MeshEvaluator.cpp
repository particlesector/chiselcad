#include "MeshEvaluator.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
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

    // Mesh (import()/surface()) and Polyhedron (polyhedron()) carry their
    // actual geometry outside `params` too — same rationale as Polygon2D
    // above, just distinct label prefixes so two builtins can't collide.
    case CsgLeaf::Kind::Mesh:
    case CsgLeaf::Kind::Polyhedron: {
        k += (leaf.kind == CsgLeaf::Kind::Mesh) ? "mesh:" : "polyhedron:";
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
    case CsgLeaf::Kind::Polygon2D:  return "polygon()";
    case CsgLeaf::Kind::Mesh:       return "import()/surface()";
    case CsgLeaf::Kind::Polyhedron: return "polyhedron()";
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

    // Background ('%') roots are appended after the "real" ones — MeshBuilder
    // uses scene.roots.size() as the boundary to exclude them from volume/
    // surface-area stats and STL export while still meshing/rendering them.
    std::vector<manifold::Manifold> result;
    result.reserve(scene.roots.size() + scene.backgroundRoots.size());
    for (const auto& root : scene.roots)
        result.push_back(evalNode(*root, gen));
    for (const auto& root : scene.backgroundRoots)
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
        else if constexpr (std::is_same_v<T, CsgResize>)
            return evalResize(n, gen);
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
        } else if constexpr (std::is_same_v<T, CsgExtrusion>) {
            // Nested extrusion (e.g. linear_extrude() rotate_extrude()
            // square(); ...) — realize the inner extrusion to its actual
            // 3-D solid (evalExtrusion() already bakes that node's own
            // transform into it), then flatten it to a silhouette exactly
            // like CsgProjection's cut=false case above, so the outer
            // extrude re-extrudes a well-defined 2-D shape instead of the
            // geometry being silently dropped.
            manifold::Manifold inner = evalExtrusion(n, gen);
            manifold::Polygons polys = inner.Project();
            return polys.empty() ? manifold::CrossSection{}
                                 : manifold::CrossSection(polys, manifold::CrossSection::FillRule::EvenOdd);
        } else {
            // CsgResize: 3-D only, no 2-D representation. Flag it explicitly
            // rather than silently returning empty geometry.
            chisel::lang::Diagnostic d;
            d.level   = chisel::lang::DiagLevel::Error;
            d.message = "resize() has no 2-D representation and can't be used "
                        "inside an extrude/offset/projection; its geometry was "
                        "skipped";
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
        // OpenSCAD's default linear_extrude() height is 100, not 1 —
        // confirmed against real OpenSCAD's STL output for
        // linear_extrude(v=[3,2,5]) square([10,10]) (no height given):
        // volume 10000, i.e. a 10x10 profile times height 100 (docs/
        // roadmap.md v3.9).
        double height = getP("height", getP("h", getP("_pos0", 100.0)));
        double twist  = -getP("twist",  0.0); // OpenSCAD uses left-hand rule; Manifold uses right-hand
        float  scaleX = static_cast<float>(getP("scale_x", 1.0));
        float  scaleY = static_cast<float>(getP("scale_y", 1.0));
        double fnOvr  = getP("$fn",   0.0);
        double slices = getP("slices", 0.0);
        // Divisions are only needed for twist (to smoothly interpolate the
        // rotation). A plain scale taper works correctly with 0 divisions.
        // An explicit slices=... always wins over the $fn-derived default —
        // it's OpenSCAD's dedicated knob for this and should be honored even
        // when the caller also set $fn for an unrelated reason (e.g. a
        // sibling circle() in the same file).
        int nDivs = 0;
        if (slices > 0.0)
            nDivs = std::max(1, static_cast<int>(slices));
        else if (twist != 0.0)
            nDivs = std::max(1, static_cast<int>(fnOvr > 0 ? fnOvr : 10));
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
        // rotate_extrude — angle resolution matches real OpenSCAD's own
        // (RotateExtrudeNode::instantiate/rotateextrude.cc, verified against
        // a live 2021.01 binary): a missing, non-finite (NaN/+-Inf), or
        // out-of-(-360,360] angle all fall back to a full 360° revolution.
        // Confirmed against rotate_extrude-angle.scad's corpus cases, which
        // exercise exactly these edges (unspecified, 0/0, 1/0, -1/0, 360,
        // -360, 1000, -1000).
        double angle = getP("angle", 360.0);
        if (!std::isfinite(angle) || angle <= -360.0 || angle > 360.0)
            angle = 360.0;

        // angle=0 is a distinct case from "unspecified"/"out of range" —
        // real OpenSCAD's rotatePolygon() returns no geometry at all for it
        // (not a degenerate zero-volume sweep), matching
        // rotate_extrude-angle.scad's own "// show nothing" comment on its
        // angle=0 case.
        if (angle == 0.0) return {};

        double fnOvr = getP("$fn", 0.0);

        manifold::Polygons polys = cs.ToPolygons();

        // Real OpenSCAD only rejects a profile that *straddles* the
        // rotation axis (points strictly on both sides) — a profile lying
        // entirely on the -X side is valid, it just revolves mirrored back
        // onto +X (confirmed against rotate_extrude-tests.scad's "Object in
        // negative X" case, translate([-20,0]) square(10)). This used to
        // reject *any* point with x<0, including profiles entirely on the
        // negative side.
        // minX/maxX are deliberately seeded at 0.0, not the true extremes —
        // matching real OpenSCAD's own rotatePolygon() (GeometryEvaluator.cc
        // in the 2021.01 oracle this was verified against), which does the
        // same (`double min_x = 0; double max_x = 0;` before the same
        // fmin/fmax scan). This isn't an oversight there: it means a profile
        // that never touches the axis gets its segment-count radius (below)
        // measured as the axis-to-far-edge distance, not the profile's own
        // width — confirmed against a live 2021.01 binary, which uses 24
        // segments (not the "true extent"-implied 16) for
        // rotate_extrude(a=-45) applied to a profile spanning x=[16,26] (a
        // width of 10, but 26 measured from the axis).
        constexpr double kAxisEps = 1e-4;
        double minX = 0.0, maxX = 0.0;
        for (const auto& poly : polys) {
            for (const auto& pt : poly) {
                minX = std::min(minX, static_cast<double>(pt.x));
                maxX = std::max(maxX, static_cast<double>(pt.x));
            }
        }

        if (minX < -kAxisEps && maxX > kAxisEps) {
            chisel::lang::Diagnostic d;
            d.level   = chisel::lang::DiagLevel::Error;
            d.message = "rotate_extrude(): profile crosses the rotation axis "
                        "(all points must have the same X sign); geometry skipped";
            m_diags.push_back(std::move(d));
            return {};
        }

        // Manifold's own Revolve() only keeps x>=0 geometry (it silently
        // clips away anything with x<0 rather than mirroring it), so a
        // profile entirely on the -X side must be mirrored onto +X before
        // handing it off. Revolving the mirrored profile through the same
        // angle and then rotating the result 180° about Z reproduces the
        // original sweep: (x*cos(a), x*sin(a)) for x<0 is identical to
        // ((-x)*cos(a+180), (-x)*sin(a+180)) for -x>0. Negating x alone
        // mirrors (and thus reverses the winding of) each polygon, so the
        // vertex order is reversed too, to keep the outward-facing
        // convention Revolve() expects — without this the mirrored solid
        // comes out inside-out (negative volume).
        bool mirrored = minX < -kAxisEps;
        if (mirrored) {
            for (auto& poly : polys) {
                for (auto& pt : poly)
                    pt.x = -pt.x;
                std::reverse(poly.begin(), poly.end());
            }
        }
        // Segment count uses (0-seeded) maxX-minX as the radius proxy,
        // matching real OpenSCAD's Calc::get_fragments_from_r(max_x-min_x,
        // ...) exactly — not a fixed stand-in radius (this used to be a
        // hardcoded 10.0). Computed from the pre-mirror min/max: negating
        // every x negates and swaps min/max, so this span is the same
        // either way — no need to branch on `mirrored` here.
        double radius = maxX - minX;
        int    segs   = gen.resolveSegments(radius, fnOvr);
        // Real OpenSCAD scales that full-circle count down for a partial
        // sweep (floor, matching OpenSCAD's own minimum of 1) rather than
        // tessellating the whole arc at full-circle density — passing the
        // full-circle count straight through to Revolve() (as this used to)
        // over-tessellates any angle<360 sweep relative to real OpenSCAD's
        // actual output. The floor is 3, not 1: Manifold::Revolve() only
        // honors an explicit circularSegments > 2, silently falling back to
        // its own internal auto-quality segment count (built from a default
        // it knows nothing about our angle/profile) for 0/1/2 — which
        // produced degenerate/empty geometry here for a small-enough angle,
        // not just an imprecise tessellation.
        segs = std::max(3, static_cast<int>(segs * std::fabs(angle) / 360.0));

        // Manifold::Revolve() winds its side faces correctly for a
        // positive sweep but comes out inside-out (negative volume) for a
        // literal negative revolveDegrees, so always sweep by the positive
        // magnitude and mirror the *result* across the XZ plane (Y -> -Y)
        // for an originally-negative angle instead: revolving a profile
        // point through angle -A gives (x*cos(-A), x*sin(-A), y) =
        // (x*cos(A), -x*sin(A), y), i.e. exactly the +A sweep with Y
        // negated. Manifold's Mirror() (unlike Revolve()) is a generic
        // transform and keeps winding correct on its own.
        result = manifold::Manifold::Revolve(
            polys,
            segs,
            static_cast<float>(std::fabs(angle)));
        if (angle < 0.0)
            result = result.Mirror({0.0, 1.0, 0.0});

        if (mirrored)
            result = result.Rotate(0.0, 0.0, 180.0);
    }

    // Apply the outer 3-D world transform
    if (e.transform != glm::mat4{1.0f})
        result = result.Transform(toAffine(e.transform));

    return checkStatus(std::move(result),
                        e.kind == CsgExtrusion::Kind::Linear ? "linear_extrude()" : "rotate_extrude()");
}

// ---------------------------------------------------------------------------
// evalResize — union the children, measure their bounding box, and scale to
// match newsize (see CsgResize's doc comment in CsgNode.h for why this can't
// be resolved at CsgEvaluator time the way scale() is).
// ---------------------------------------------------------------------------
manifold::Manifold MeshEvaluator::evalResize(const CsgResize& r, const PrimitiveGen& gen) {
    if (r.children.empty()) return {};

    manifold::Manifold result = evalNode(*r.children[0], gen);
    for (std::size_t i = 1; i < r.children.size(); ++i)
        result = result + evalNode(*r.children[i], gen);

    manifold::Box box = result.BoundingBox();
    const double extentX = static_cast<double>(box.max.x - box.min.x);
    const double extentY = static_cast<double>(box.max.y - box.min.y);
    const double extentZ = static_cast<double>(box.max.z - box.min.z);

    // An axis is "explicitly resized" when newsize for it is non-zero and
    // the current extent is non-degenerate; anything else defaults to an
    // unchanged (1.0) scale unless auto= kicks in below.
    auto explicitScale = [](double newsize, double extent) -> std::optional<double> {
        if (newsize == 0.0 || extent <= 1e-9) return std::nullopt;
        return newsize / extent;
    };
    std::optional<double> sxExplicit = explicitScale(r.newX, extentX);
    std::optional<double> syExplicit = explicitScale(r.newY, extentY);
    std::optional<double> szExplicit = explicitScale(r.newZ, extentZ);

    // OpenSCAD's auto=: an axis with newsize 0 and auto=true scales by the
    // largest explicit scale factor among the OTHER axes, so the shape
    // grows/shrinks proportionally along that axis instead of staying put;
    // falls back to 1 (unchanged) if no axis was explicitly resized.
    double maxExplicit = 1.0;
    bool haveExplicit = false;
    for (auto s : {sxExplicit, syExplicit, szExplicit}) {
        if (!s) continue;
        maxExplicit = haveExplicit ? std::max(maxExplicit, *s) : *s;
        haveExplicit = true;
    }

    auto resolveAxis = [&](std::optional<double> explicitS, bool autoFlag) -> double {
        if (explicitS) return *explicitS;
        return autoFlag ? maxExplicit : 1.0;
    };

    const double sx = resolveAxis(sxExplicit, r.autoX);
    const double sy = resolveAxis(syExplicit, r.autoY);
    const double sz = resolveAxis(szExplicit, r.autoZ);

    result = result.Scale({static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz)});

    if (r.transform != glm::mat4{1.0f})
        result = result.Transform(toAffine(r.transform));

    return checkStatus(std::move(result), "resize()");
}

} // namespace chisel::csg
