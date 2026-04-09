#include "CsgEvaluator.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace chisel::csg {

using namespace chisel::lang;

static constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
CsgScene CsgEvaluator::evaluate(const ParseResult& result) {
    CsgScene scene;
    scene.globalFn = result.globalFn;
    scene.globalFs = result.globalFs;
    scene.globalFa = result.globalFa;

    const glm::mat4 identity{1.0f};
    for (const auto& root : result.roots) {
        if (auto node = evalNode(*root, identity))
            scene.roots.push_back(std::move(node));
    }
    return scene;
}

// ---------------------------------------------------------------------------
// Node dispatch
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalNode(const AstNode& node, const glm::mat4& xform) {
    return std::visit([&](const auto& n) -> CsgNodePtr {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, PrimitiveNode>)
            return evalPrimitive(n, xform);
        else if constexpr (std::is_same_v<T, BooleanNode>)
            return evalBoolean(n, xform);
        else if constexpr (std::is_same_v<T, TransformNode>)
            return evalTransform(n, xform);
        return nullptr;
    }, node);
}

// ---------------------------------------------------------------------------
// Primitive — bake the accumulated transform into the leaf
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalPrimitive(const PrimitiveNode& p, const glm::mat4& xform) {
    CsgLeaf leaf;
    switch (p.kind) {
    case PrimitiveNode::Kind::Cube:     leaf.kind = CsgLeaf::Kind::Cube;     break;
    case PrimitiveNode::Kind::Sphere:   leaf.kind = CsgLeaf::Kind::Sphere;   break;
    case PrimitiveNode::Kind::Cylinder: leaf.kind = CsgLeaf::Kind::Cylinder; break;
    }
    leaf.params    = p.params;
    leaf.center    = p.center;
    leaf.transform = xform;
    return makeLeaf(std::move(leaf));
}

// ---------------------------------------------------------------------------
// Boolean — preserve the tree structure, pass xform down to children
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalBoolean(const BooleanNode& b, const glm::mat4& xform) {
    CsgBoolean bnode;
    switch (b.op) {
    case BooleanNode::Op::Union:        bnode.op = CsgBoolean::Op::Union;        break;
    case BooleanNode::Op::Difference:   bnode.op = CsgBoolean::Op::Difference;   break;
    case BooleanNode::Op::Intersection: bnode.op = CsgBoolean::Op::Intersection; break;
    }
    for (const auto& child : b.children) {
        if (auto c = evalNode(*child, xform))
            bnode.children.push_back(std::move(c));
    }
    return makeBoolean(std::move(bnode));
}

// ---------------------------------------------------------------------------
// Transform — multiply into the accumulated matrix, recurse into children.
// Transforms are transparent: they fold into leaf nodes rather than
// becoming tree nodes in the CSG IR.
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalTransform(const TransformNode& t, const glm::mat4& xform) {
    const glm::mat4 combined = xform * makeMatrix(t);

    std::vector<CsgNodePtr> children;
    children.reserve(t.children.size());
    for (const auto& child : t.children) {
        if (auto c = evalNode(*child, combined))
            children.push_back(std::move(c));
    }

    if (children.empty())     return nullptr;
    if (children.size() == 1) return children[0];

    // Multiple children under one transform → implicit union
    CsgBoolean u;
    u.op       = CsgBoolean::Op::Union;
    u.children = std::move(children);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// Build the local transform matrix for a TransformNode.
//
// Rotation order matches OpenSCAD: rotate([rx, ry, rz]) applies Z first,
// then Y, then X (extrinsic axes).  Matrix form: Rx * Ry * Rz.
// In GLM (column-major, M*v convention): build as rotate(X) * rotate(Y) *
// rotate(Z) so that Rz is applied first when multiplied against a column
// vector: (Rx*Ry*Rz)*p = Rx*(Ry*(Rz*p)).
// ---------------------------------------------------------------------------
glm::mat4 CsgEvaluator::makeMatrix(const TransformNode& t) {
    glm::mat4 m{1.0f};
    switch (t.kind) {

    case TransformNode::Kind::Translate:
        m = glm::translate(m, glm::vec3(
            static_cast<float>(t.x),
            static_cast<float>(t.y),
            static_cast<float>(t.z)));
        break;

    case TransformNode::Kind::Rotate: {
        float rx = static_cast<float>(t.x * kDeg2Rad);
        float ry = static_cast<float>(t.y * kDeg2Rad);
        float rz = static_cast<float>(t.z * kDeg2Rad);
        // GLM's rotate(M, angle, axis) = M * R(angle,axis)
        // We want final matrix = Rx*Ry*Rz:
        m = glm::rotate(m, rx, glm::vec3(1.0f, 0.0f, 0.0f)); // m = Rx
        m = glm::rotate(m, ry, glm::vec3(0.0f, 1.0f, 0.0f)); // m = Rx*Ry
        m = glm::rotate(m, rz, glm::vec3(0.0f, 0.0f, 1.0f)); // m = Rx*Ry*Rz
        break;
    }

    case TransformNode::Kind::Scale:
        m = glm::scale(m, glm::vec3(
            static_cast<float>(t.x),
            static_cast<float>(t.y),
            static_cast<float>(t.z)));
        break;

    case TransformNode::Kind::Mirror: {
        // Householder reflection: I - 2*(n⊗n) / (n·n)
        // If n == [0,0,0], OpenSCAD treats it as identity — leave m = I.
        glm::vec3 n(static_cast<float>(t.x),
                    static_cast<float>(t.y),
                    static_cast<float>(t.z));
        float len2 = glm::dot(n, n);
        if (len2 > 1e-12f) {
            n /= std::sqrt(len2);
            m[0][0] = 1.0f - 2.0f * n.x * n.x;
            m[1][0] =       -2.0f * n.x * n.y;
            m[2][0] =       -2.0f * n.x * n.z;
            m[0][1] =       -2.0f * n.y * n.x;
            m[1][1] = 1.0f - 2.0f * n.y * n.y;
            m[2][1] =       -2.0f * n.y * n.z;
            m[0][2] =       -2.0f * n.z * n.x;
            m[1][2] =       -2.0f * n.z * n.y;
            m[2][2] = 1.0f - 2.0f * n.z * n.z;
        }
        break;
    }
    }
    return m;
}

} // namespace chisel::csg
