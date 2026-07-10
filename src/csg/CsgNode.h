#pragma once
#include "lang/Diagnostic.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace chisel::csg {

// ---------------------------------------------------------------------------
// Forward declarations needed for the recursive variant
// ---------------------------------------------------------------------------
struct CsgBoolean;
struct CsgExtrusion;
struct CsgOffset;

// ---------------------------------------------------------------------------
// ColorAttr — the color() attribute in effect at a given point in the tree,
// accumulated top-down exactly like the transform matrix (nested color()
// overrides its ancestor's for its own subtree). `has` is false when no
// color() has been applied — the renderer falls back to its default tint.
// ---------------------------------------------------------------------------
struct ColorAttr {
    bool has = false;
    glm::vec4 value{1.0f, 1.0f, 1.0f, 1.0f};
};

// ---------------------------------------------------------------------------
// CsgLeaf — a primitive with its fully-accumulated world transform.
// The transform encodes every translate/rotate/scale/mirror applied above
// this node in the AST; the MeshEvaluator applies it after tessellation.
// ---------------------------------------------------------------------------
struct CsgLeaf {
    enum class Kind { Cube, Sphere, Cylinder, Square2D, Circle2D, Polygon2D };

    Kind kind = Kind::Cube;
    // Named params carried forward from the parser ("r", "h", "r1", "r2",
    // "$fn", "$fs", "$fa", "x"/"y"/"z" for cube size, "_pos0" for sphere(5),
    // "sx"/"sy" for square, etc.)
    std::unordered_map<std::string, double> params;
    bool center = false;
    glm::mat4 transform{1.0f}; // accumulated model-to-world matrix
    ColorAttr color;           // accumulated color() tint, if any

    // Polygon2D only — resolved contour points and optional path indices
    std::vector<glm::vec2>           polyPoints;
    std::vector<std::vector<int>>    polyPaths;
};

// ---------------------------------------------------------------------------
// CsgNode — the CSG IR variant
// ---------------------------------------------------------------------------
using CsgNode    = std::variant<CsgLeaf, CsgBoolean, CsgExtrusion, CsgOffset>;
using CsgNodePtr = std::shared_ptr<CsgNode>;

struct CsgBoolean {
    enum class Op { Union, Difference, Intersection, Hull, Minkowski };

    Op op = Op::Union;
    std::vector<CsgNodePtr> children;
    // Hull and Minkowski are not equivariant under per-child translation:
    // MinkowskiSum(T(A),T(B)) = 2T + sum(shapes), not T + sum.
    // So their children are evaluated in local space and this matrix is
    // applied once to the final result. Identity for Union/Difference/Intersection.
    glm::mat4 transform{1.0f};
    ColorAttr color; // accumulated color() tint in effect at this node
};

// ---------------------------------------------------------------------------
// CsgExtrusion — linear_extrude / rotate_extrude in the CSG IR.
// Children are 2-D CsgLeafs (Square2D / Circle2D / Polygon2D) or CsgBooleans
// of 2-D leaves; MeshEvaluator converts them to CrossSections.
// ---------------------------------------------------------------------------
struct CsgExtrusion {
    enum class Kind { Linear, Rotate };

    Kind kind = Kind::Linear;
    // Resolved numeric params: "height", "twist", "scale_x", "scale_y",
    // "angle", "$fn", "center", "_pos0"
    std::unordered_map<std::string, double> params;
    std::vector<CsgNodePtr> children;
    glm::mat4 transform{1.0f}; // outer 3-D transform applied to the final solid
    ColorAttr color;           // accumulated color() tint in effect at this node
};

// ---------------------------------------------------------------------------
// CsgOffset — offset(r=...) / offset(delta=..., chamfer=...) in the CSG IR.
// Children are 2-D CsgLeaf/CsgBoolean/CsgOffset nodes; MeshEvaluator builds
// their CrossSection, offsets it, then applies `transform`. Offsetting is
// not equivariant under arbitrary per-child transforms (like Hull/
// Minkowski), so children are evaluated in local space and the accumulated
// transform is applied once to the offset result.
// ---------------------------------------------------------------------------
struct CsgOffset {
    // Resolved numeric params: "r" (rounded offset radius, mutually
    // exclusive with delta) or "delta" (straight-edge offset distance),
    // "chamfer" (0/1, only meaningful with delta), "$fn" (segment override
    // for the rounded case).
    std::unordered_map<std::string, double> params;
    std::vector<CsgNodePtr> children;
    glm::mat4 transform{1.0f};
    ColorAttr color;
};

// ---------------------------------------------------------------------------
// Factory helpers
// ---------------------------------------------------------------------------
inline CsgNodePtr makeLeaf(CsgLeaf leaf) {
    return std::make_shared<CsgNode>(std::move(leaf));
}
inline CsgNodePtr makeBoolean(CsgBoolean b) {
    return std::make_shared<CsgNode>(std::move(b));
}
inline CsgNodePtr makeExtrusion(CsgExtrusion e) {
    return std::make_shared<CsgNode>(std::move(e));
}
inline CsgNodePtr makeOffset(CsgOffset o) {
    return std::make_shared<CsgNode>(std::move(o));
}

// The ColorAttr in effect at the top of this node's subtree (whichever
// color() most closely wraps it), regardless of concrete node kind.
inline const ColorAttr& nodeColor(const CsgNode& node) {
    return std::visit([](const auto& n) -> const ColorAttr& { return n.color; }, node);
}

// ---------------------------------------------------------------------------
// CsgScene — output of CsgEvaluator
// ---------------------------------------------------------------------------
struct CsgScene {
    std::vector<CsgNodePtr>     roots;
    double globalFn = 0.0;
    double globalFs = 2.0;
    double globalFa = 12.0;
    std::vector<std::string>    echoMessages; // echo() output (one entry per call)
    std::vector<lang::Diagnostic> evalDiags; // assert() failures and other runtime errors
};

} // namespace chisel::csg
