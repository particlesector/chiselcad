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
    defaultInterp.loadFunctions(result);
    return evaluate(result, defaultInterp);
}

CsgScene CsgEvaluator::evaluate(const ParseResult& result, Interpreter& interp) {
    m_interp = &interp;

    // Index module definitions by name for O(1) lookup during calls
    m_moduleDefs.clear();
    for (const auto& def : result.moduleDefs)
        m_moduleDefs[def.name] = &def;

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
    m_moduleDefs.clear();
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
        else if constexpr (std::is_same_v<T, ForNode>)
            return evalFor(n, xform);
        else if constexpr (std::is_same_v<T, ModuleCallNode>)
            return evalModuleCall(n, xform);
        else if constexpr (std::is_same_v<T, ExtrusionNode>)
            return evalExtrusion(n, xform);
        else if constexpr (std::is_same_v<T, LetNode>)
            return evalLet(n, xform);
        return nullptr;
    }, node);
}

// ---------------------------------------------------------------------------
// Primitive — resolve ExprPtr params, bake the transform into the leaf
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalPrimitive(const PrimitiveNode& p, const glm::mat4& xform) {
    CsgLeaf leaf;
    leaf.center    = p.center;
    leaf.transform = xform;

    switch (p.kind) {
    // ---- 3-D primitives: resolve all params as scalars --------------------
    case PrimitiveNode::Kind::Cube:
        leaf.kind = CsgLeaf::Kind::Cube;
        for (const auto& [name, exprPtr] : p.params)
            leaf.params[name] = m_interp->evalNumber(*exprPtr);
        break;

    case PrimitiveNode::Kind::Sphere:
        leaf.kind = CsgLeaf::Kind::Sphere;
        for (const auto& [name, exprPtr] : p.params)
            leaf.params[name] = m_interp->evalNumber(*exprPtr);
        break;

    case PrimitiveNode::Kind::Cylinder:
        leaf.kind = CsgLeaf::Kind::Cylinder;
        for (const auto& [name, exprPtr] : p.params)
            leaf.params[name] = m_interp->evalNumber(*exprPtr);
        break;

    // ---- square([w,h]) / square(s) / square(size=[w,h]) ------------------
    case PrimitiveNode::Kind::Square2D: {
        leaf.kind = CsgLeaf::Kind::Square2D;
        // Resolve scalar params ($fn, etc.) — skip "size" which may be a vector
        for (const auto& [name, exprPtr] : p.params) {
            if (name == "size") continue;
            leaf.params[name] = m_interp->evalNumber(*exprPtr);
        }
        // "size" overrides x/y if present
        if (p.params.count("size")) {
            Value sv = m_interp->evaluate(*p.params.at("size"));
            if (sv.isNumber()) {
                leaf.params["sx"] = sv.asNumber();
                leaf.params["sy"] = sv.asNumber();
            } else if (sv.isVector() && sv.asVec().size() >= 2) {
                leaf.params["sx"] = sv.asVec()[0].asNumber();
                leaf.params["sy"] = sv.asVec()[1].asNumber();
            }
        }
        // If sx/sy not set yet, fall back to positional vector "x","y" or scalar "_pos0"
        if (!leaf.params.count("sx")) {
            double sx = leaf.params.count("x") ? leaf.params["x"] : 1.0;
            double sy = leaf.params.count("y") ? leaf.params["y"] : 1.0;
            if (leaf.params.count("_pos0")) {
                double s = leaf.params["_pos0"];
                sx = sy = s;
            }
            leaf.params["sx"] = sx;
            leaf.params["sy"] = sy;
        }
        break;
    }

    // ---- circle(r) / circle(d) --------------------------------------------
    case PrimitiveNode::Kind::Circle2D:
        leaf.kind = CsgLeaf::Kind::Circle2D;
        for (const auto& [name, exprPtr] : p.params)
            leaf.params[name] = m_interp->evalNumber(*exprPtr);
        // diameter → radius
        if (!leaf.params.count("r") && leaf.params.count("d"))
            leaf.params["r"] = leaf.params["d"] * 0.5;
        break;

    // ---- polygon(points=[[x,y],...], paths=[[i,j,...],...]) ---------------
    case PrimitiveNode::Kind::Polygon2D: {
        leaf.kind = CsgLeaf::Kind::Polygon2D;
        if (p.params.count("points")) {
            Value pts = m_interp->evaluate(*p.params.at("points"));
            if (pts.isVector()) {
                leaf.polyPoints.reserve(pts.asVec().size());
                for (const auto& pt : pts.asVec()) {
                    if (pt.isVector() && pt.asVec().size() >= 2) {
                        leaf.polyPoints.push_back({
                            static_cast<float>(pt.asVec()[0].asNumber()),
                            static_cast<float>(pt.asVec()[1].asNumber())
                        });
                    }
                }
            }
        }
        if (p.params.count("paths")) {
            Value paths = m_interp->evaluate(*p.params.at("paths"));
            if (paths.isVector()) {
                for (const auto& path : paths.asVec()) {
                    if (path.isVector()) {
                        std::vector<int> indices;
                        indices.reserve(path.asVec().size());
                        for (const auto& idx : path.asVec())
                            indices.push_back(static_cast<int>(idx.asNumber()));
                        leaf.polyPaths.push_back(std::move(indices));
                    }
                }
            }
        }
        break;
    }
    }

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
    Value rotVal = m_interp->evaluate(*t.vec); // evaluate once, share result
    auto vec = [&]() -> std::array<double, 3> {
        if (rotVal.isVector()) {
            std::array<double, 3> r = {0.0, 0.0, 0.0};
            for (std::size_t i = 0; i < 3 && i < rotVal.asVec().size(); ++i)
                if (rotVal.asVec()[i].isNumber()) r[i] = rotVal.asVec()[i].asNumber();
            return r;
        }
        if (rotVal.isNumber())
            return {0.0, 0.0, rotVal.asNumber()}; // scalar → Z axis
        return {0.0, 0.0, 0.0};
    }();
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
        // scalar rotate(angle) → Z-axis; vector → XYZ Euler
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

// ---------------------------------------------------------------------------
// for — iterate range or list, union all child geometry
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalFor(const ForNode& node, const glm::mat4& xform) {
    // Build the sequence of iteration values
    std::vector<Value> values;
    static constexpr int kMaxIter = 10000;

    if (node.range.isRange) {
        double start = m_interp->evalNumber(*node.range.start);
        double end   = m_interp->evalNumber(*node.range.end);
        double step  = node.range.step
                       ? m_interp->evalNumber(*node.range.step)
                       : 1.0;
        if (step == 0.0) return nullptr;
        if (step > 0.0)
            for (double v = start; v <= end + 1e-10 && (int)values.size() < kMaxIter; v += step)
                values.push_back(Value::fromNumber(v));
        else
            for (double v = start; v >= end - 1e-10 && (int)values.size() < kMaxIter; v += step)
                values.push_back(Value::fromNumber(v));
    } else {
        // List form — evaluate each element; if one evaluates to a Vector,
        // expand it so that `for (pt = pts)` iterates over pts' elements.
        for (const auto& e : node.range.list) {
            Value v = m_interp->evaluate(*e);
            if (v.isVector())
                for (const auto& elem : v.asVec()) values.push_back(elem);
            else
                values.push_back(std::move(v));
        }
    }

    // Save the loop variable's current binding, iterate, then restore
    const Value saved = m_interp->getVar(node.var);
    std::vector<CsgNodePtr> all;
    for (const Value& v : values) {
        m_interp->setVar(node.var, v);
        for (const auto& child : node.children) {
            if (auto c = evalNode(*child, xform))
                all.push_back(std::move(c));
        }
    }
    m_interp->setVar(node.var, saved);

    if (all.empty())     return nullptr;
    if (all.size() == 1) return all[0];

    CsgBoolean u;
    u.op       = CsgBoolean::Op::Union;
    u.children = std::move(all);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// Module call — bind args, evaluate body, restore environment
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalModuleCall(const ModuleCallNode& call, const glm::mat4& xform) {
    auto it = m_moduleDefs.find(call.name);
    if (it == m_moduleDefs.end()) return nullptr; // undefined module

    const ModuleDef& def = *it->second;

    // Snapshot the interpreter env so we can restore it after the call
    auto savedEnv = m_interp->snapshotEnv();

    // Bind positional and named arguments to module parameters
    std::size_t posIdx = 0;
    for (const auto& arg : call.args) {
        if (arg.name.empty()) {
            if (posIdx < def.params.size())
                m_interp->setVar(def.params[posIdx].name,
                                 m_interp->evaluate(*arg.value));
            ++posIdx;
        } else {
            m_interp->setVar(arg.name, m_interp->evaluate(*arg.value));
        }
    }

    // Fill in defaults for parameters not supplied
    for (std::size_t i = 0; i < def.params.size(); ++i) {
        const auto& param = def.params[i];
        bool alreadyBound = (i < posIdx);
        if (!alreadyBound) {
            for (const auto& arg : call.args)
                if (arg.name == param.name) { alreadyBound = true; break; }
        }
        if (!alreadyBound && param.defaultVal)
            m_interp->setVar(param.name, m_interp->evaluate(*param.defaultVal));
    }

    // Evaluate the module body and collect geometry
    std::vector<CsgNodePtr> all;
    for (const auto& child : def.body) {
        if (auto c = evalNode(*child, xform))
            all.push_back(std::move(c));
    }

    // Restore the caller's environment
    m_interp->restoreEnv(std::move(savedEnv));

    if (all.empty())     return nullptr;
    if (all.size() == 1) return all[0];

    CsgBoolean u;
    u.op       = CsgBoolean::Op::Union;
    u.children = std::move(all);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// Extrusion — build a CsgExtrusion from an ExtrusionNode
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalExtrusion(const ExtrusionNode& e, const glm::mat4& xform) {
    CsgExtrusion ext;
    ext.kind      = (e.kind == ExtrusionNode::Kind::Linear) ? CsgExtrusion::Kind::Linear
                                                             : CsgExtrusion::Kind::Rotate;
    ext.transform = xform;

    // Resolve numeric params; treat "scale" and "center" specially
    for (const auto& [name, exprPtr] : e.params) {
        if (name == "scale") {
            Value sv = m_interp->evaluate(*exprPtr);
            if (sv.isNumber()) {
                ext.params["scale_x"] = sv.asNumber();
                ext.params["scale_y"] = sv.asNumber();
            } else if (sv.isVector() && sv.asVec().size() >= 2) {
                ext.params["scale_x"] = sv.asVec()[0].asNumber();
                ext.params["scale_y"] = sv.asVec()[1].asNumber();
            }
        } else if (name == "center") {
            Value cv = m_interp->evaluate(*exprPtr);
            ext.params["center"] = bool(cv) ? 1.0 : 0.0;
        } else {
            ext.params[name] = m_interp->evalNumber(*exprPtr);
        }
    }

    // Evaluate 2-D children in local space (identity xform)
    // The outer xform is stored in ext.transform and applied to the final solid.
    const glm::mat4 identity{1.0f};
    for (const auto& child : e.children) {
        if (auto c = evalNode(*child, identity))
            ext.children.push_back(std::move(c));
    }

    return makeExtrusion(std::move(ext));
}

// ---------------------------------------------------------------------------
// let — bind variables in scope, evaluate children, restore
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalLet(const LetNode& node, const glm::mat4& xform) {
    auto savedEnv = m_interp->snapshotEnv();
    for (const auto& [name, valExpr] : node.bindings)
        m_interp->setVar(name, m_interp->evaluate(*valExpr));

    std::vector<CsgNodePtr> all;
    for (const auto& child : node.children) {
        if (auto c = evalNode(*child, xform))
            all.push_back(std::move(c));
    }
    m_interp->restoreEnv(std::move(savedEnv));

    if (all.empty())     return nullptr;
    if (all.size() == 1) return std::move(all[0]);

    CsgBoolean u;
    u.op       = CsgBoolean::Op::Union;
    u.children = std::move(all);
    return makeBoolean(std::move(u));
}

} // namespace chisel::csg
