#include "CsgEvaluator.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cctype>
#include <cmath>
#include <cstdio>

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
    m_scene = &scene;
    scene.globalFn = result.globalFn;
    scene.globalFs = result.globalFs;
    scene.globalFa = result.globalFa;

    // Make special variables readable in expression context
    interp.setVar("$fn", Value::fromNumber(result.globalFn));
    interp.setVar("$fs", Value::fromNumber(result.globalFs));
    interp.setVar("$fa", Value::fromNumber(result.globalFa));

    const glm::mat4 identity{1.0f};
    const ColorAttr  noColor{};
    for (const auto& root : result.roots) {
        if (auto node = evalNode(*root, identity, noColor))
            scene.roots.push_back(std::move(node));
    }

    m_interp = nullptr;
    m_scene  = nullptr;
    m_moduleDefs.clear();
    m_childrenStack.clear();
    return scene;
}

// ---------------------------------------------------------------------------
// Node dispatch
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalNode(const AstNode& node, const glm::mat4& xform, const ColorAttr& color) {
    return std::visit([&](const auto& n) -> CsgNodePtr {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, PrimitiveNode>)
            return evalPrimitive(n, xform, color);
        else if constexpr (std::is_same_v<T, BooleanNode>)
            return evalBoolean(n, xform, color);
        else if constexpr (std::is_same_v<T, TransformNode>)
            return evalTransform(n, xform, color);
        else if constexpr (std::is_same_v<T, IfNode>)
            return evalIf(n, xform, color);
        else if constexpr (std::is_same_v<T, ForNode>)
            return evalFor(n, xform, color);
        else if constexpr (std::is_same_v<T, ModuleCallNode>)
            return evalModuleCall(n, xform, color);
        else if constexpr (std::is_same_v<T, ExtrusionNode>)
            return evalExtrusion(n, xform, color);
        else if constexpr (std::is_same_v<T, LetNode>)
            return evalLet(n, xform, color);
        else if constexpr (std::is_same_v<T, ColorNode>)
            return evalColor(n, xform, color);
        else if constexpr (std::is_same_v<T, OffsetNode>)
            return evalOffset(n, xform, color);
        else if constexpr (std::is_same_v<T, ProjectionNode>)
            return evalProjection(n, xform, color);
        return nullptr;
    }, node);
}

// ---------------------------------------------------------------------------
// Primitive — resolve ExprPtr params, bake the transform into the leaf
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalPrimitive(const PrimitiveNode& p, const glm::mat4& xform, const ColorAttr& color) {
    CsgLeaf leaf;
    leaf.center    = p.center;
    leaf.transform = xform;
    leaf.color     = color;

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
CsgNodePtr CsgEvaluator::evalBoolean(const BooleanNode& b, const glm::mat4& xform, const ColorAttr& color) {
    CsgBoolean bnode;
    bnode.color = color;
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
        if (auto c = evalNode(*child, childXform, color))
            bnode.children.push_back(std::move(c));
    }
    return makeBoolean(std::move(bnode));
}

// ---------------------------------------------------------------------------
// Transform — resolve the vec expression, multiply into the accumulated matrix
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalTransform(const TransformNode& t, const glm::mat4& xform, const ColorAttr& color) {
    const glm::mat4 combined = xform * makeMatrix(t);

    std::vector<CsgNodePtr> children;
    children.reserve(t.children.size());
    for (const auto& child : t.children) {
        if (auto c = evalNode(*child, combined, color))
            children.push_back(std::move(c));
    }

    if (children.empty())     return nullptr;
    if (children.size() == 1) return children[0];

    CsgBoolean u;
    u.op       = CsgBoolean::Op::Union;
    u.color    = color;
    u.children = std::move(children);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// Build the local transform matrix — evaluates the vec3 expression.
// Rotation order: Z first, then Y, then X (OpenSCAD convention).
// ---------------------------------------------------------------------------
glm::mat4 CsgEvaluator::makeMatrix(const TransformNode& t) const {
    // render() has no argument of its own — just an identity matrix.
    if (t.kind == TransformNode::Kind::Identity)
        return glm::mat4{1.0f};

    // multmatrix(m) — m is a list of up to 4 rows of up to 4 numbers each
    // (OpenSCAD convention: missing trailing row defaults to [0,0,0,1],
    // missing trailing column in the top 3 rows defaults to 0).
    // Rows are read directly into the matrix; glm is column-major, so
    // element [row][col] of the OpenSCAD matrix goes to m[col][row].
    if (t.kind == TransformNode::Kind::Matrix) {
        Value matVal = m_interp->evaluate(*t.vec);
        glm::mat4 m{1.0f};
        if (matVal.isVector()) {
            const auto& rows = matVal.asVec();
            for (std::size_t row = 0; row < 4 && row < rows.size(); ++row) {
                if (!rows[row].isVector()) continue;
                const auto& cols = rows[row].asVec();
                for (std::size_t col = 0; col < 4 && col < cols.size(); ++col)
                    if (cols[col].isNumber())
                        m[static_cast<int>(col)][static_cast<int>(row)] = static_cast<float>(cols[col].asNumber());
            }
        }
        return m;
    }

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

    case TransformNode::Kind::Matrix:
    case TransformNode::Kind::Identity:
        break; // handled by early return above; unreachable
    }
    return m;
}

// ---------------------------------------------------------------------------
// if/else — evaluate condition, walk the live branch
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalIf(const IfNode& node, const glm::mat4& xform, const ColorAttr& color) {
    const bool cond = bool(m_interp->evaluate(*node.condition));
    const auto& branch = cond ? node.thenChildren : node.elseChildren;

    std::vector<CsgNodePtr> children;
    children.reserve(branch.size());
    for (const auto& child : branch) {
        if (auto c = evalNode(*child, xform, color))
            children.push_back(std::move(c));
    }

    if (children.empty())     return nullptr;
    if (children.size() == 1) return children[0];

    CsgBoolean u;
    u.op       = CsgBoolean::Op::Union;
    u.color    = color;
    u.children = std::move(children);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// for — iterate range or list, union all child geometry
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalFor(const ForNode& node, const glm::mat4& xform, const ColorAttr& color) {
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
            if (auto c = evalNode(*child, xform, color))
                all.push_back(std::move(c));
        }
    }
    m_interp->setVar(node.var, saved);

    if (all.empty())     return nullptr;
    if (all.size() == 1) return all[0];

    CsgBoolean u;
    u.op       = CsgBoolean::Op::Union;
    u.color    = color;
    u.children = std::move(all);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// formatValue — convert a Value to a human-readable string (for echo/assert)
// ---------------------------------------------------------------------------
std::string CsgEvaluator::formatValue(const Value& v) {
    if (v.isNumber()) {
        char buf[64];
        double n = v.asNumber();
        if (n == static_cast<double>(static_cast<long long>(n)) && n > -1e15 && n < 1e15)
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
        else
            std::snprintf(buf, sizeof(buf), "%g", n);
        return buf;
    }
    if (v.isBool())   return v.asBool() ? "true" : "false";
    if (v.isString()) return "\"" + v.asString() + "\"";
    if (v.isUndef())  return "undef";
    if (v.isVector()) {
        std::string s = "[";
        for (std::size_t i = 0; i < v.asVec().size(); ++i) {
            if (i > 0) s += ", ";
            s += formatValue(v.asVec()[i]);
        }
        s += "]";
        return s;
    }
    return "undef";
}

// ---------------------------------------------------------------------------
// Module call — bind args, evaluate body, restore environment
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalModuleCall(const ModuleCallNode& call, const glm::mat4& xform, const ColorAttr& color) {
    // ---- Built-in: children() ----
    if (call.name == "children") return evalChildren(call, xform, color);

    // ---- Built-in: echo(...) ----
    if (call.name == "echo") {
        if (m_scene) {
            std::string msg = "ECHO:";
            bool first = true;
            for (const auto& arg : call.args) {
                Value v = m_interp->evaluate(*arg.value);
                msg += first ? " " : ", ";
                first = false;
                msg += formatValue(v);
            }
            m_scene->echoMessages.push_back(std::move(msg));
        }
        return nullptr;
    }

    // ---- Built-in: assert(cond [, msg]) ----
    if (call.name == "assert") {
        if (!call.args.empty() && m_scene) {
            Value cond = m_interp->evaluate(*call.args[0].value);
            if (!bool(cond)) {
                lang::Diagnostic d;
                d.level = lang::DiagLevel::Error;
                d.loc   = call.loc;
                if (call.args.size() >= 2) {
                    Value msgVal = m_interp->evaluate(*call.args[1].value);
                    d.message = "assert failed: " + formatValue(msgVal);
                } else {
                    d.message = "assert failed";
                }
                m_scene->evalDiags.push_back(std::move(d));
            }
        }
        return nullptr;
    }

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

    // Expose $children count and push the children context for children() access
    m_interp->setVar("$children", Value::fromNumber(static_cast<double>(call.children.size())));
    m_childrenStack.push_back(&call.children);

    // Evaluate the module body and collect geometry
    std::vector<CsgNodePtr> all;
    for (const auto& child : def.body) {
        if (auto c = evalNode(*child, xform, color))
            all.push_back(std::move(c));
    }

    m_childrenStack.pop_back();
    // Restore the caller's environment
    m_interp->restoreEnv(std::move(savedEnv));

    if (all.empty())     return nullptr;
    if (all.size() == 1) return all[0];

    CsgBoolean u;
    u.op       = CsgBoolean::Op::Union;
    u.color    = color;
    u.children = std::move(all);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// children() — evaluate the active module's children with correct nesting.
// Pops the stack before evaluating so any children() calls *inside* a child
// node see the grandparent module's children (correct OpenSCAD semantics).
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalChildren(const ModuleCallNode& call, const glm::mat4& xform, const ColorAttr& color) {
    if (m_childrenStack.empty()) return nullptr;

    const auto* activeChildren = m_childrenStack.back();
    if (!activeChildren || activeChildren->empty()) return nullptr;

    // Pop current frame so nested children() calls see the parent context
    m_childrenStack.pop_back();

    std::vector<CsgNodePtr> all;

    if (call.args.empty()) {
        // children() — evaluate all children
        for (const auto& child : *activeChildren) {
            if (auto c = evalNode(*child, xform, color))
                all.push_back(std::move(c));
        }
    } else {
        // children(i) — evaluate the i-th child
        Value idxVal = m_interp->evaluate(*call.args[0].value);
        if (idxVal.isNumber()) {
            int idx = static_cast<int>(idxVal.asNumber());
            if (idx >= 0 && idx < static_cast<int>(activeChildren->size())) {
                if (auto c = evalNode(*(*activeChildren)[static_cast<std::size_t>(idx)], xform, color))
                    all.push_back(std::move(c));
            }
        }
    }

    // Restore the frame
    m_childrenStack.push_back(activeChildren);

    if (all.empty())     return nullptr;
    if (all.size() == 1) return std::move(all[0]);

    CsgBoolean u;
    u.op       = CsgBoolean::Op::Union;
    u.color    = color;
    u.children = std::move(all);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// Extrusion — build a CsgExtrusion from an ExtrusionNode
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalExtrusion(const ExtrusionNode& e, const glm::mat4& xform, const ColorAttr& color) {
    CsgExtrusion ext;
    ext.kind      = (e.kind == ExtrusionNode::Kind::Linear) ? CsgExtrusion::Kind::Linear
                                                             : CsgExtrusion::Kind::Rotate;
    ext.transform = xform;
    ext.color     = color;

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
        if (auto c = evalNode(*child, identity, color))
            ext.children.push_back(std::move(c));
    }

    return makeExtrusion(std::move(ext));
}

// ---------------------------------------------------------------------------
// offset() — resolve r/delta/chamfer/$fn to doubles; children are evaluated
// in local space (like extrusion/hull/minkowski) since offsetting isn't
// equivariant under arbitrary per-child transforms.
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalOffset(const OffsetNode& o, const glm::mat4& xform, const ColorAttr& color) {
    CsgOffset off;
    off.transform = xform;
    off.color     = color;

    for (const auto& [name, exprPtr] : o.params) {
        if (name == "chamfer") {
            Value cv = m_interp->evaluate(*exprPtr);
            off.params["chamfer"] = bool(cv) ? 1.0 : 0.0;
        } else {
            off.params[name] = m_interp->evalNumber(*exprPtr);
        }
    }

    const glm::mat4 identity{1.0f};
    for (const auto& child : o.children) {
        if (auto c = evalNode(*child, identity, color))
            off.children.push_back(std::move(c));
    }

    return makeOffset(std::move(off));
}

// ---------------------------------------------------------------------------
// projection() — resolve "cut" to a bool; children are 3-D geometry
// evaluated in local space (their own translate/rotate/scale still apply,
// but not whatever's wrapping the projection() call itself — that outer
// transform is stored on the node and applied to the 2-D result instead,
// same treatment as offset()/extrusion).
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalProjection(const ProjectionNode& p, const glm::mat4& xform, const ColorAttr& color) {
    CsgProjection proj;
    proj.transform = xform;
    proj.color     = color;

    if (auto it = p.params.find("cut"); it != p.params.end())
        proj.cut = bool(m_interp->evaluate(*it->second));

    const glm::mat4 identity{1.0f};
    for (const auto& child : p.children) {
        if (auto c = evalNode(*child, identity, color))
            proj.children.push_back(std::move(c));
    }

    return makeProjection(std::move(proj));
}

// ---------------------------------------------------------------------------
// let — bind variables in scope, evaluate children, restore
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalLet(const LetNode& node, const glm::mat4& xform, const ColorAttr& color) {
    auto savedEnv = m_interp->snapshotEnv();
    for (const auto& [name, valExpr] : node.bindings)
        m_interp->setVar(name, m_interp->evaluate(*valExpr));

    std::vector<CsgNodePtr> all;
    for (const auto& child : node.children) {
        if (auto c = evalNode(*child, xform, color))
            all.push_back(std::move(c));
    }
    m_interp->restoreEnv(std::move(savedEnv));

    if (all.empty())     return nullptr;
    if (all.size() == 1) return std::move(all[0]);

    CsgBoolean u;
    u.op       = CsgBoolean::Op::Union;
    u.color    = color;
    u.children = std::move(all);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// color() — resolves its (optional) color/alpha expressions into a tint
// that overrides the inherited ColorAttr for this subtree only. A nested
// color() further inside always wins over this one for its own children.
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalColor(const ColorNode& node, const glm::mat4& xform, const ColorAttr& color) {
    ColorAttr cur = color;

    if (node.colorExpr) {
        Value cv = m_interp->evaluate(*node.colorExpr);
        glm::vec4 rgba;
        if (resolveColor(cv, rgba)) {
            cur.has   = true;
            cur.value = rgba;
        }
    }
    if (node.alphaExpr) {
        cur.has     = true;
        cur.value.a = static_cast<float>(m_interp->evalNumber(*node.alphaExpr));
    }

    std::vector<CsgNodePtr> children;
    children.reserve(node.children.size());
    for (const auto& child : node.children) {
        if (auto c = evalNode(*child, xform, cur))
            children.push_back(std::move(c));
    }

    if (children.empty())     return nullptr;
    if (children.size() == 1) return children[0];

    CsgBoolean u;
    u.op       = CsgBoolean::Op::Union;
    u.color    = cur;
    u.children = std::move(children);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// resolveColor — turn a color() argument Value into an RGBA color.
// Accepts [r,g,b] / [r,g,b,a] vectors (components 0..1), "#rrggbb" /
// "#rrggbbaa" hex strings, and a set of common CSS/X11 color names.
// Returns false (leaving `out` untouched) for anything else.
// ---------------------------------------------------------------------------
bool CsgEvaluator::resolveColor(const Value& c, glm::vec4& out) const {
    if (c.isVector()) {
        const auto& v = c.asVec();
        if (v.size() < 3) return false;
        out = glm::vec4(
            static_cast<float>(v[0].asNumber()),
            static_cast<float>(v[1].asNumber()),
            static_cast<float>(v[2].asNumber()),
            v.size() >= 4 ? static_cast<float>(v[3].asNumber()) : 1.0f);
        return true;
    }
    if (!c.isString()) return false;
    const std::string& s = c.asString();

    if (!s.empty() && s[0] == '#') {
        auto hexDigit = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return -1;
        };
        auto byteAt = [&](std::size_t i) -> int {
            int hi = hexDigit(s[i]), lo = hexDigit(s[i + 1]);
            return (hi < 0 || lo < 0) ? -1 : (hi << 4) | lo;
        };
        if (s.size() != 7 && s.size() != 9) return false;
        int r = byteAt(1), g = byteAt(3), b = byteAt(5);
        int a = (s.size() == 9) ? byteAt(7) : 255;
        if (r < 0 || g < 0 || b < 0 || a < 0) return false;
        out = glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
        return true;
    }

    static const std::unordered_map<std::string, glm::vec3> kNamedColors = {
        {"black",      {0.00f, 0.00f, 0.00f}}, {"white",      {1.00f, 1.00f, 1.00f}},
        {"red",        {1.00f, 0.00f, 0.00f}}, {"lime",       {0.00f, 1.00f, 0.00f}},
        {"green",      {0.00f, 0.50f, 0.00f}}, {"blue",       {0.00f, 0.00f, 1.00f}},
        {"yellow",     {1.00f, 1.00f, 0.00f}}, {"cyan",       {0.00f, 1.00f, 1.00f}},
        {"magenta",    {1.00f, 0.00f, 1.00f}}, {"orange",     {1.00f, 0.65f, 0.00f}},
        {"purple",     {0.50f, 0.00f, 0.50f}}, {"pink",       {1.00f, 0.75f, 0.80f}},
        {"gray",       {0.50f, 0.50f, 0.50f}}, {"grey",       {0.50f, 0.50f, 0.50f}},
        {"silver",     {0.75f, 0.75f, 0.75f}}, {"gold",       {1.00f, 0.84f, 0.00f}},
        {"brown",      {0.65f, 0.16f, 0.16f}}, {"navy",       {0.00f, 0.00f, 0.50f}},
        {"teal",       {0.00f, 0.50f, 0.50f}}, {"maroon",     {0.50f, 0.00f, 0.00f}},
        {"olive",      {0.50f, 0.50f, 0.00f}}, {"indigo",     {0.29f, 0.00f, 0.51f}},
        {"violet",     {0.93f, 0.51f, 0.93f}}, {"coral",      {1.00f, 0.50f, 0.31f}},
        {"salmon",     {0.98f, 0.50f, 0.45f}}, {"khaki",      {0.94f, 0.90f, 0.55f}},
        {"plum",       {0.87f, 0.63f, 0.87f}}, {"orchid",     {0.85f, 0.44f, 0.84f}},
        {"turquoise",  {0.25f, 0.88f, 0.82f}}, {"tan",        {0.82f, 0.71f, 0.55f}},
        {"beige",      {0.96f, 0.96f, 0.86f}}, {"ivory",      {1.00f, 1.00f, 0.94f}},
        {"crimson",    {0.86f, 0.08f, 0.24f}}, {"chocolate",  {0.82f, 0.41f, 0.12f}},
        {"lavender",   {0.90f, 0.90f, 0.98f}}, {"skyblue",    {0.53f, 0.81f, 0.92f}},
        {"lightblue",  {0.68f, 0.85f, 0.90f}}, {"darkgray",   {0.66f, 0.66f, 0.66f}},
        {"darkgrey",   {0.66f, 0.66f, 0.66f}}, {"lightgray",  {0.83f, 0.83f, 0.83f}},
        {"lightgrey",  {0.83f, 0.83f, 0.83f}},
    };

    std::string lower;
    lower.reserve(s.size());
    for (char ch : s)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    auto it = kNamedColors.find(lower);
    if (it == kNamedColors.end()) return false;
    out = glm::vec4(it->second, 1.0f);
    return true;
}

} // namespace chisel::csg
