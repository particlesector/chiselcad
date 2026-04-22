#include "CsgEvaluator.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace chisel::csg {

using namespace chisel::lang;

static constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------
CsgScene CsgEvaluator::evaluate(const ParseResult& result) {
    Interpreter defaultInterp;
    defaultInterp.loadAssignments(result);
    return evaluate(result, defaultInterp);
}

CsgScene CsgEvaluator::evaluate(const ParseResult& result, Interpreter& interp) {
    m_interp = &interp;

    CsgScene scene;
    scene.globalFn = result.globalFn;
    scene.globalFs = result.globalFs;
    scene.globalFa = result.globalFa;

    const glm::mat4 identity{1.0f};
    for (const auto& root : result.roots) {
        if (auto node = evalNode(*root, identity))
            scene.roots.push_back(std::move(node));
    }

    m_interp = nullptr;
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
        else if constexpr (std::is_same_v<T, IfNode>)
            return evalIf(n, xform);
        return nullptr;
    }, node);
}

// ---------------------------------------------------------------------------
// Primitive — resolve ExprPtr params, bake the transform into the leaf
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalPrimitive(const PrimitiveNode& p, const glm::mat4& xform) {
    CsgLeaf leaf;
    switch (p.kind) {
    case PrimitiveNode::Kind::Cube:     leaf.kind = CsgLeaf::Kind::Cube;     break;
    case PrimitiveNode::Kind::Sphere:   leaf.kind = CsgLeaf::Kind::Sphere;   break;
    case PrimitiveNode::Kind::Cylinder: leaf.kind = CsgLeaf::Kind::Cylinder; break;
    }

    // Resolve every expression param to a concrete double
    for (const auto& [name, exprPtr] : p.params)
        leaf.params[name] = m_interp->evalNumber(*exprPtr);

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
    case BooleanNode::Op::Hull:         bnode.op = CsgBoolean::Op::Hull;         break;
    case BooleanNode::Op::Minkowski:    bnode.op = CsgBoolean::Op::Minkowski;    break;
    }

    const bool isLocalSpaceOp = (bnode.op == CsgBoolean::Op::Hull ||
                                  bnode.op == CsgBoolean::Op::Minkowski);
    const glm::mat4 childXform = isLocalSpaceOp ? glm::mat4{1.0f} : xform;
    if (isLocalSpaceOp) bnode.transform = xform;

    for (const auto& child : b.children) {
        if (auto c = evalNode(*child, childXform))
            bnode.children.push_back(std::move(c));
    }
    return makeBoolean(std::move(bnode));
}

// ---------------------------------------------------------------------------
// Transform — resolve the vec expression, multiply into the accumulated matrix
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

    CsgBoolean u;
    u.op       = CsgBoolean::Op::Union;
    u.children = std::move(children);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// Build the local transform matrix — evaluates the vec3 expression.
// Rotation order: Z first, then Y, then X (OpenSCAD convention).
// ---------------------------------------------------------------------------
glm::mat4 CsgEvaluator::makeMatrix(const TransformNode& t) const {
    auto vec = m_interp->evalVec3(*t.vec);
    const double vx = vec[0], vy = vec[1], vz = vec[2];

    glm::mat4 m{1.0f};
    switch (t.kind) {

    case TransformNode::Kind::Translate:
        m = glm::translate(m, glm::vec3(
            static_cast<float>(vx),
            static_cast<float>(vy),
            static_cast<float>(vz)));
        break;

    case TransformNode::Kind::Rotate: {
        float rx = static_cast<float>(vx * kDeg2Rad);
        float ry = static_cast<float>(vy * kDeg2Rad);
        float rz = static_cast<float>(vz * kDeg2Rad);
        m = glm::rotate(m, rx, glm::vec3(1.0f, 0.0f, 0.0f));
        m = glm::rotate(m, ry, glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::rotate(m, rz, glm::vec3(0.0f, 0.0f, 1.0f));
        break;
    }

    case TransformNode::Kind::Scale:
        m = glm::scale(m, glm::vec3(
            static_cast<float>(vx),
            static_cast<float>(vy),
            static_cast<float>(vz)));
        break;

    case TransformNode::Kind::Mirror: {
        glm::vec3 n(static_cast<float>(vx),
                    static_cast<float>(vy),
                    static_cast<float>(vz));
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

// ---------------------------------------------------------------------------
// if/else — evaluate condition, walk the live branch
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalIf(const IfNode& node, const glm::mat4& xform) {
    const bool cond = bool(m_interp->evaluate(*node.condition));
    const auto& branch = cond ? node.thenChildren : node.elseChildren;

    std::vector<CsgNodePtr> children;
    children.reserve(branch.size());
    for (const auto& child : branch) {
        if (auto c = evalNode(*child, xform))
            children.push_back(std::move(c));
    }

    if (children.empty())     return nullptr;
    if (children.size() == 1) return children[0];

    CsgBoolean u;
    u.op       = CsgBoolean::Op::Union;
    u.children = std::move(children);
    return makeBoolean(std::move(u));
}

} // namespace chisel::csg
