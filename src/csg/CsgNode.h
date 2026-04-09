#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace chisel::csg {

// ---------------------------------------------------------------------------
// Forward declaration needed for the recursive variant
// ---------------------------------------------------------------------------
struct CsgBoolean;

// ---------------------------------------------------------------------------
// CsgLeaf — a primitive with its fully-accumulated world transform.
// The transform encodes every translate/rotate/scale/mirror applied above
// this node in the AST; the MeshEvaluator applies it after tessellation.
// ---------------------------------------------------------------------------
struct CsgLeaf {
    enum class Kind { Cube, Sphere, Cylinder };

    Kind kind = Kind::Cube;
    // Named params carried forward from the parser ("r", "h", "r1", "r2",
    // "$fn", "$fs", "$fa", "x"/"y"/"z" for cube size, "_pos0" for sphere(5)).
    std::unordered_map<std::string, double> params;
    bool center = false;
    glm::mat4 transform{1.0f}; // accumulated model-to-world matrix
};

// ---------------------------------------------------------------------------
// CsgNode — the CSG IR variant
// ---------------------------------------------------------------------------
using CsgNode    = std::variant<CsgLeaf, CsgBoolean>;
using CsgNodePtr = std::shared_ptr<CsgNode>;

struct CsgBoolean {
    enum class Op { Union, Difference, Intersection };

    Op op = Op::Union;
    std::vector<CsgNodePtr> children;
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

// ---------------------------------------------------------------------------
// CsgScene — output of CsgEvaluator
// ---------------------------------------------------------------------------
struct CsgScene {
    std::vector<CsgNodePtr> roots;
    double globalFn = 0.0;
    double globalFs = 2.0;
    double globalFa = 12.0;
};

} // namespace chisel::csg
