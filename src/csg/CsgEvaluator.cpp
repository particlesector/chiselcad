#include "CsgEvaluator.h"

#include "import/AmfLoader.h"
#include "import/DxfLoader.h"
#include "import/OffLoader.h"
#include "import/StlLoader.h"
#include "import/SurfaceLoader.h"
#include "import/SvgLoader.h"
#include "import/TextLoader.h"
#include "import/ThreeMfLoader.h"
#include "util/PathSuffix.h"
#include "util/PathUtf8.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

#ifndef CHISELCAD_RESOURCE_DIR
#error "CHISELCAD_RESOURCE_DIR must be defined by the build (see CMakeLists.txt)"
#endif

namespace chisel::csg {

using namespace chisel::lang;

static constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;

// Bundled fallback used by text() when its `font` argument is omitted/empty.
// Not baseDir-relative — this is a build-time resource path, not a
// .scad-file-relative user path (see resolveFilePathArg()).
static std::filesystem::path defaultFontPath() {
    return std::filesystem::path(CHISELCAD_RESOURCE_DIR) / "fonts" / "Roboto-Regular.ttf";
}

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
    m_aborted = false;
    for (const auto& def : result.moduleDefs)
        m_moduleDefs[def.name] = &def;

    CsgScene scene;
    m_scene = &scene;

    // $fn/$fs/$fa: every assignment (literal or not) is pushed onto
    // result.assignments in file order, same as any other variable, and
    // loadAssignments() (called by callers before reaching this function)
    // has already evaluated them in order into interp's env — so reading it
    // back here gives correct "last assignment wins" semantics even when a
    // script mixes literal and non-literal reassignments of the same
    // special var (e.g. `$fn = quality*4; $fn = 8;`). result.globalFn is
    // only a fallback for the (common) case where the var was never
    // assigned at all, or only Parser-visible cross-include merging
    // (SourceLoader::mergeGlobalQuality) applies.
    auto resolveGlobal = [&](const char* name, double parsedVal) {
        Value v = interp.getVar(name);
        return v.isNumber() ? v.asNumber() : parsedVal;
    };
    scene.globalFn = resolveGlobal("$fn", result.globalFn);
    scene.globalFs = resolveGlobal("$fs", result.globalFs);
    scene.globalFa = resolveGlobal("$fa", result.globalFa);

    // Make special variables readable in expression context
    interp.setVar("$fn", Value::fromNumber(scene.globalFn));
    interp.setVar("$fs", Value::fromNumber(scene.globalFs));
    interp.setVar("$fa", Value::fromNumber(scene.globalFa));

    const glm::mat4 identity{1.0f};
    const ColorAttr noColor{};
    for (const auto& root : result.roots) {
        if (auto node = evalNode(*root, identity, noColor))
            scene.roots.push_back(std::move(node));
    }

    // '!' (root/show-only): if any node anywhere in the tree carried it,
    // discard everything else — the marked subtree(s) become the entire
    // output, exactly as if the file contained only them. Background roots
    // (from '%' elsewhere in the file) are discarded too, matching
    // OpenSCAD: '!' means *only* this, full stop.
    if (!m_rootOnlyNodes.empty()) {
        scene.roots = m_rootOnlyNodes;
        scene.backgroundRoots.clear();
    }

    m_interp = nullptr;
    m_scene = nullptr;
    m_moduleDefs.clear();
    m_childrenStack.clear();
    m_rootOnlyNodes.clear();
    return scene;
}

// ---------------------------------------------------------------------------
// Node dispatch
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalNode(const AstNode& node, const glm::mat4& xform,
                                  const ColorAttr& color) {
    // A failed assert() aborts the rest of the script (OpenSCAD semantics):
    // every remaining statement anywhere in the tree — siblings, later
    // module-body statements, later for-loop iterations, etc. — funnels
    // through this function, so bailing out here halts all of them without
    // needing a check in each individual loop.
    if (m_aborted)
        return nullptr;

    // '*' (disable): the subtree is skipped entirely — not evaluated at all,
    // so nested echo()/assert() or expensive module calls inside it never
    // run, matching OpenSCAD's "as if this statement wasn't here" semantics.
    const uint8_t mods = astModifiers(node);
    if (mods & ModDisable)
        return nullptr;

    CsgNodePtr result = std::visit(
        [&](const auto& n) -> CsgNodePtr {
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
            else if constexpr (std::is_same_v<T, AssignStmt>) {
                // Local assignment as a block statement: mutates the current
                // scope in place (the enclosing evalXxx call is responsible for
                // snapshot/restore around its whole child list, so this doesn't
                // leak past the block it's written in). Produces no geometry.
                m_interp->assignVar(n.name, *n.value);
                return nullptr;
            }
            return nullptr;
        },
        node);

    if (!result || mods == ModNone)
        return result;

    // '#' (highlight/debug): force a highlight tint on this node's own
    // subtree, exactly like an implicit color() wrapper — and like a nested
    // (non-root) color(), it only becomes visible once this node ends up as
    // (or inside) a CsgScene root, since Manifold's boolean merge discards
    // per-child color identity below that level (see ColorAttr's comment).
    if (mods & ModHighlight)
        std::visit([](auto& n) { n.color = ColorAttr{true, kHighlightColor}; }, *result);

    // '!' (root/show-only): stash this fully-evaluated subtree; evaluate()
    // swaps it in for the whole scene once the entire tree has been walked.
    if (mods & ModRoot)
        m_rootOnlyNodes.push_back(result);

    // '%' (background): excluded from whatever boolean/group it's nested
    // in — returning nullptr here means the parent's own children/geometry
    // loop simply doesn't add it — and instead rendered as its own
    // independent, tinted reference root.
    if (mods & ModBackground) {
        std::visit([](auto& n) { n.color = ColorAttr{true, kBackgroundColor}; }, *result);
        if (m_scene)
            m_scene->backgroundRoots.push_back(result);
        return nullptr;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Primitive — resolve ExprPtr params, bake the transform into the leaf
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalPrimitive(const PrimitiveNode& p, const glm::mat4& xform,
                                       const ColorAttr& color) {
    CsgLeaf leaf;
    leaf.center = p.center;
    leaf.transform = xform;
    leaf.color = color;

    switch (p.kind) {
    // ---- cube([w,h,d]) / cube(s) / cube(size=[w,h,d]) / cube(size=s) ------
    case PrimitiveNode::Kind::Cube: {
        leaf.kind = CsgLeaf::Kind::Cube;
        // Resolve scalar params ($fn, etc.) — skip "size"/"_pos0", which may
        // be vectors, so they aren't coerced to 0 by the blanket evalNumber.
        for (const auto& [name, exprPtr] : p.params) {
            if (name == "size" || name == "_pos0")
                continue;
            leaf.params[name] = m_interp->evalNumber(*exprPtr);
        }
        // Named "size=" takes priority; otherwise a bare positional arg
        // (cube(5) or cube(v) where v is a vector variable) is "size".
        const ExprNode* sizeExpr = p.params.count("size")    ? p.params.at("size").get()
                                   : p.params.count("_pos0") ? p.params.at("_pos0").get()
                                                             : nullptr;
        if (sizeExpr) {
            Value sv = m_interp->evaluate(*sizeExpr);
            if (sv.isNumber()) {
                leaf.params["x"] = leaf.params["y"] = leaf.params["z"] = sv.asNumber();
            } else if (sv.isVector()) {
                const auto& vec = sv.asVec();
                if (vec.size() >= 1 && vec[0].isNumber())
                    leaf.params["x"] = vec[0].asNumber();
                if (vec.size() >= 2 && vec[1].isNumber())
                    leaf.params["y"] = vec[1].asNumber();
                if (vec.size() >= 3 && vec[2].isNumber())
                    leaf.params["z"] = vec[2].asNumber();
            }
        }
        break;
    }

    // ---- sphere(r) / sphere(d) ---------------------------------------------
    case PrimitiveNode::Kind::Sphere:
        leaf.kind = CsgLeaf::Kind::Sphere;
        for (const auto& [name, exprPtr] : p.params)
            leaf.params[name] = m_interp->evalNumber(*exprPtr);
        // diameter → radius
        if (!leaf.params.count("r") && leaf.params.count("d"))
            leaf.params["r"] = leaf.params["d"] * 0.5;
        break;

    // ---- cylinder(h, r) / cylinder(h=, r=/r1=/r2=/d=/d1=/d2=) --------------
    case PrimitiveNode::Kind::Cylinder:
        leaf.kind = CsgLeaf::Kind::Cylinder;
        for (const auto& [name, exprPtr] : p.params)
            leaf.params[name] = m_interp->evalNumber(*exprPtr);
        // Bare positional args: cylinder(h) / cylinder(h, r)
        if (!leaf.params.count("h") && leaf.params.count("_pos0"))
            leaf.params["h"] = leaf.params["_pos0"];
        if (!leaf.params.count("r") && leaf.params.count("_pos1"))
            leaf.params["r"] = leaf.params["_pos1"];
        // diameter → radius conversions
        if (!leaf.params.count("r") && leaf.params.count("d"))
            leaf.params["r"] = leaf.params["d"] * 0.5;
        if (!leaf.params.count("r1") && leaf.params.count("d1"))
            leaf.params["r1"] = leaf.params["d1"] * 0.5;
        if (!leaf.params.count("r2") && leaf.params.count("d2"))
            leaf.params["r2"] = leaf.params["d2"] * 0.5;
        break;

    // ---- square([w,h]) / square(s) / square(size=[w,h]) ------------------
    case PrimitiveNode::Kind::Square2D: {
        leaf.kind = CsgLeaf::Kind::Square2D;
        // Resolve scalar params ($fn, etc.) — skip "size" which may be a vector
        for (const auto& [name, exprPtr] : p.params) {
            if (name == "size")
                continue;
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
                        leaf.polyPoints.push_back({static_cast<float>(pt.asVec()[0].asNumber()),
                                                   static_cast<float>(pt.asVec()[1].asNumber())});
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
CsgNodePtr CsgEvaluator::evalBoolean(const BooleanNode& b, const glm::mat4& xform,
                                     const ColorAttr& color) {
    CsgBoolean bnode;
    bnode.color = color;
    switch (b.op) {
    case BooleanNode::Op::Union:
        bnode.op = CsgBoolean::Op::Union;
        break;
    case BooleanNode::Op::Difference:
        bnode.op = CsgBoolean::Op::Difference;
        break;
    case BooleanNode::Op::Intersection:
        bnode.op = CsgBoolean::Op::Intersection;
        break;
    case BooleanNode::Op::Hull:
        bnode.op = CsgBoolean::Op::Hull;
        break;
    case BooleanNode::Op::Minkowski:
        bnode.op = CsgBoolean::Op::Minkowski;
        break;
    }

    const bool isLocalSpaceOp =
        (bnode.op == CsgBoolean::Op::Hull || bnode.op == CsgBoolean::Op::Minkowski);
    const glm::mat4 childXform = isLocalSpaceOp ? glm::mat4{1.0f} : xform;
    if (isLocalSpaceOp)
        bnode.transform = xform;

    // Local assignments among the children (see AssignStmt in evalNode) are
    // scoped to this block: save/restore around the whole child list so they
    // don't leak into whatever follows this node in the enclosing scope.
    auto savedEnv = m_interp->snapshotEnv();
    for (const auto& child : b.children) {
        if (auto c = evalNode(*child, childXform, color))
            bnode.children.push_back(std::move(c));
    }
    m_interp->restoreEnv(std::move(savedEnv));
    return makeBoolean(std::move(bnode));
}

// ---------------------------------------------------------------------------
// Transform — resolve the vec expression, multiply into the accumulated matrix
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalTransform(const TransformNode& t, const glm::mat4& xform,
                                       const ColorAttr& color) {
    const glm::mat4 combined = xform * makeMatrix(t);

    std::vector<CsgNodePtr> children;
    children.reserve(t.children.size());
    auto savedEnv = m_interp->snapshotEnv();
    for (const auto& child : t.children) {
        if (auto c = evalNode(*child, combined, color))
            children.push_back(std::move(c));
    }
    m_interp->restoreEnv(std::move(savedEnv));

    if (children.empty())
        return nullptr;
    if (children.size() == 1)
        return children[0];

    CsgBoolean u;
    u.op = CsgBoolean::Op::Union;
    u.color = color;
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
                if (!rows[row].isVector())
                    continue;
                const auto& cols = rows[row].asVec();
                for (std::size_t col = 0; col < 4 && col < cols.size(); ++col)
                    if (cols[col].isNumber())
                        m[static_cast<int>(col)][static_cast<int>(row)] =
                            static_cast<float>(cols[col].asNumber());
            }
        }
        return m;
    }

    Value rotVal = m_interp->evaluate(*t.vec); // evaluate once, share result
    auto vec = [&]() -> std::array<double, 3> {
        if (rotVal.isVector()) {
            std::array<double, 3> r = {0.0, 0.0, 0.0};
            for (std::size_t i = 0; i < 3 && i < rotVal.asVec().size(); ++i)
                if (rotVal.asVec()[i].isNumber())
                    r[i] = rotVal.asVec()[i].asNumber();
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
        m = glm::translate(
            m, glm::vec3(static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz)));
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
        m = glm::scale(
            m, glm::vec3(static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz)));
        break;

    case TransformNode::Kind::Mirror: {
        glm::vec3 n(static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz));
        float len2 = glm::dot(n, n);
        if (len2 > 1e-12f) {
            n /= std::sqrt(len2);
            m[0][0] = 1.0f - 2.0f * n.x * n.x;
            m[1][0] = -2.0f * n.x * n.y;
            m[2][0] = -2.0f * n.x * n.z;
            m[0][1] = -2.0f * n.y * n.x;
            m[1][1] = 1.0f - 2.0f * n.y * n.y;
            m[2][1] = -2.0f * n.y * n.z;
            m[0][2] = -2.0f * n.z * n.x;
            m[1][2] = -2.0f * n.z * n.y;
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
CsgNodePtr CsgEvaluator::evalIf(const IfNode& node, const glm::mat4& xform,
                                const ColorAttr& color) {
    const bool cond = bool(m_interp->evaluate(*node.condition));
    const auto& branch = cond ? node.thenChildren : node.elseChildren;

    std::vector<CsgNodePtr> children;
    children.reserve(branch.size());
    auto savedEnv = m_interp->snapshotEnv();
    for (const auto& child : branch) {
        if (auto c = evalNode(*child, xform, color))
            children.push_back(std::move(c));
    }
    m_interp->restoreEnv(std::move(savedEnv));

    if (children.empty())
        return nullptr;
    if (children.size() == 1)
        return children[0];

    CsgBoolean u;
    u.op = CsgBoolean::Op::Union;
    u.color = color;
    u.children = std::move(children);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// for — iterate range or list, union all child geometry
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalFor(const ForNode& node, const glm::mat4& xform,
                                 const ColorAttr& color) {
    // Build the sequence of iteration values
    std::vector<Value> values;

    if (node.range.isRange) {
        double start = m_interp->evalNumber(*node.range.start);
        double end = m_interp->evalNumber(*node.range.end);
        double step = node.range.step ? m_interp->evalNumber(*node.range.step) : 1.0;
        values = m_interp->expandRange(start, step, end);
    } else if (node.range.isBracketedList) {
        // Bracketed list literal `[a, b, c]` — each element is its own loop
        // value, even if it evaluates to a vector (e.g. a point list
        // `[[1,2,3], [4,5,6]]` must yield two vector iterations, not six
        // scalars).
        for (const auto& e : node.range.list)
            values.push_back(m_interp->evaluate(*e));
    } else {
        // Expression form `for (v = expr)` — expr must evaluate to a vector
        // or a range (e.g. a variable holding one, or a general range
        // literal used directly: `for (i = someRange)`); expand either so
        // `for (pt = pts)` iterates over pts' elements.
        for (const auto& e : node.range.list) {
            Value v = m_interp->evaluate(*e);
            for (auto& elem : m_interp->iterationValues(v))
                values.push_back(std::move(elem));
        }
    }

    // Each iteration gets a fresh copy of the enclosing scope: the loop
    // variable and any local assignment inside the body must not leak into
    // the next iteration or survive past the loop (matching OpenSCAD's
    // per-iteration block scoping), so restore the pre-loop snapshot before
    // each iteration rather than just saving/restoring node.var alone.
    auto savedEnv = m_interp->snapshotEnv();
    std::vector<CsgNodePtr> all;
    for (const Value& v : values) {
        m_interp->restoreEnv(savedEnv);
        m_interp->setVar(node.var, v);
        for (const auto& child : node.children) {
            if (auto c = evalNode(*child, xform, color))
                all.push_back(std::move(c));
        }
    }
    m_interp->restoreEnv(std::move(savedEnv));

    if (all.empty())
        return nullptr;
    if (all.size() == 1)
        return all[0];

    CsgBoolean u;
    u.op = CsgBoolean::Op::Union;
    u.color = color;
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
    if (v.isBool())
        return v.asBool() ? "true" : "false";
    if (v.isString())
        return "\"" + v.asString() + "\"";
    if (v.isUndef())
        return "undef";
    if (v.isVector()) {
        std::string s = "[";
        for (std::size_t i = 0; i < v.asVec().size(); ++i) {
            if (i > 0)
                s += ", ";
            s += formatValue(v.asVec()[i]);
        }
        s += "]";
        return s;
    }
    if (v.isRange()) {
        return "[" + formatValue(Value::fromNumber(v.rangeStart)) + ":" +
               formatValue(Value::fromNumber(v.rangeStep)) + ":" +
               formatValue(Value::fromNumber(v.rangeEnd)) + "]";
    }
    if (v.isFunction()) {
        std::string s = "function(";
        if (v.closure && v.closure->def) {
            const auto& params = v.closure->def->params;
            for (std::size_t i = 0; i < params.size(); ++i) {
                if (i > 0) s += ", ";
                s += params[i].name;
            }
        }
        s += ")";
        return s;
    }
    return "undef";
}

// ---------------------------------------------------------------------------
// Module call — bind args, evaluate body, restore environment
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalModuleCall(const ModuleCallNode& call, const glm::mat4& xform,
                                        const ColorAttr& color) {
    // ---- Built-in: children() ----
    if (call.name == "children")
        return evalChildren(call, xform, color);

    // ---- Built-in: echo(...) ----
    if (call.name == "echo") {
        if (m_scene) {
            std::string msg = "ECHO:";
            bool first = true;
            for (const auto& arg : call.args) {
                Value v = m_interp->evaluate(*arg.value);
                msg += first ? " " : ", ";
                first = false;
                if (!arg.name.empty()) msg += arg.name + " = ";
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
                d.loc = call.loc;
                d.filePath = resolveFilePath(call.loc.fileId);
                if (call.args.size() >= 2) {
                    Value msgVal = m_interp->evaluate(*call.args[1].value);
                    d.message = "assert failed: " + formatValue(msgVal);
                } else {
                    d.message = "assert failed";
                }
                m_scene->evalDiags.push_back(std::move(d));
                m_aborted = true;
            }
        }
        return nullptr;
    }

    // ---- Built-in: import(file) ----
    if (call.name == "import")
        return evalImport(call, xform, color);

    // ---- Built-in: surface(file, center=, invert=, convexity=) ----
    if (call.name == "surface")
        return evalSurface(call, xform, color);

    // ---- Built-in: text(t, size=, font=, halign=, valign=, spacing=, $fn=) ----
    if (call.name == "text")
        return evalText(call, xform, color);

    // ---- Built-in: polyhedron(points=, faces=) ----
    if (call.name == "polyhedron")
        return evalPolyhedron(call, xform, color);

    // ---- Built-in: resize(newsize=, auto=) { children } ----
    if (call.name == "resize")
        return evalResize(call, xform, color);

    auto it = m_moduleDefs.find(call.name);
    if (it == m_moduleDefs.end())
        return nullptr; // undefined module

    if (m_moduleDepth >= kMaxModuleDepth) {
        if (m_scene) {
            lang::Diagnostic d;
            d.level = lang::DiagLevel::Error;
            d.loc = call.loc;
            d.filePath = resolveFilePath(call.loc.fileId);
            d.message = "module recursion limit exceeded calling '" + call.name + "'";
            m_scene->evalDiags.push_back(std::move(d));
        }
        return nullptr;
    }

    const ModuleDef& def = *it->second;

    // Snapshot the interpreter env so we can restore it after the call
    auto savedEnv = m_interp->snapshotEnv();

    // Bind positional and named arguments to module parameters
    std::size_t posIdx = 0;
    for (const auto& arg : call.args) {
        if (arg.name.empty()) {
            if (posIdx < def.params.size())
                m_interp->setVar(def.params[posIdx].name, m_interp->evaluate(*arg.value));
            ++posIdx;
        } else {
            m_interp->setVar(arg.name, m_interp->evaluate(*arg.value));
        }
    }

    // Fill in defaults for parameters not supplied; explicitly bind unbound
    // ones to undef rather than leaving whatever value already occupied that
    // name in the caller's/enclosing scope.
    for (std::size_t i = 0; i < def.params.size(); ++i) {
        const auto& param = def.params[i];
        bool alreadyBound = (i < posIdx);
        if (!alreadyBound) {
            for (const auto& arg : call.args)
                if (arg.name == param.name) {
                    alreadyBound = true;
                    break;
                }
        }
        if (!alreadyBound) {
            if (param.defaultVal)
                m_interp->setVar(param.name, m_interp->evaluate(*param.defaultVal));
            else
                m_interp->setVar(param.name, Value::undef());
        }
    }

    // Expose $children count and push the children context for children() access
    m_interp->setVar("$children", Value::fromNumber(static_cast<double>(call.children.size())));
    m_childrenStack.push_back({&call.children, &savedEnv});
    m_interp->pushModuleName(call.name);

    // Evaluate the module body and collect geometry
    std::vector<CsgNodePtr> all;
    ++m_moduleDepth;
    for (const auto& child : def.body) {
        if (auto c = evalNode(*child, xform, color))
            all.push_back(std::move(c));
    }
    --m_moduleDepth;

    m_interp->popModuleName();
    m_childrenStack.pop_back();
    // Restore the caller's environment
    m_interp->restoreEnv(std::move(savedEnv));

    if (all.empty())
        return nullptr;
    if (all.size() == 1)
        return all[0];

    CsgBoolean u;
    u.op = CsgBoolean::Op::Union;
    u.color = color;
    u.children = std::move(all);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// children() — evaluate the active module's children with correct nesting.
// Pops the stack before evaluating so any children() calls *inside* a child
// node see the grandparent module's children (correct OpenSCAD semantics).
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalChildren(const ModuleCallNode& call, const glm::mat4& xform,
                                      const ColorAttr& color) {
    if (m_childrenStack.empty())
        return nullptr;

    ChildrenFrame frame = m_childrenStack.back();
    const auto* activeChildren = frame.children;
    if (!activeChildren || activeChildren->empty())
        return nullptr;

    // Pop current frame so nested children() calls see the parent context
    m_childrenStack.pop_back();

    // children(i)'s index expression is written in the *callee* module body,
    // so evaluate it in the callee's (still-active) scope, before switching
    // to the caller's scope for the child AST nodes themselves.
    //
    // The index argument can be a plain number (children(i)), a vector of
    // numbers (children([0,2])), or a range (children([0:2])) — collect
    // whichever form into a flat list of indices to evaluate below.
    std::vector<int> indices;
    if (!call.args.empty()) {
        Value idxVal = m_interp->evaluate(*call.args[0].value);
        if (idxVal.isNumber()) {
            indices.push_back(static_cast<int>(idxVal.asNumber()));
        } else if (idxVal.isVector()) {
            for (const auto& e : idxVal.asVec())
                if (e.isNumber()) indices.push_back(static_cast<int>(e.asNumber()));
        } else if (idxVal.isRange()) {
            for (const auto& v : m_interp->expandRange(idxVal.rangeStart, idxVal.rangeStep, idxVal.rangeEnd))
                if (v.isNumber()) indices.push_back(static_cast<int>(v.asNumber()));
        }
    }

    // Children AST nodes were written at the call site and must be evaluated
    // against the caller's variable bindings, not the callee module's own
    // (still-active) parameter bindings. Swap to the caller's env for the
    // duration of this evaluation, then restore the callee's env afterward.
    auto calleeEnv = m_interp->snapshotEnv();
    m_interp->restoreEnv(*frame.callerEnv);

    std::vector<CsgNodePtr> all;

    if (call.args.empty()) {
        // children() — evaluate all children
        for (const auto& child : *activeChildren) {
            if (auto c = evalNode(*child, xform, color))
                all.push_back(std::move(c));
        }
    } else {
        // children(i) / children([...]) / children([a:b]) — evaluate each
        // requested index that's actually in range; out-of-range indices are
        // skipped rather than aborting the whole call.
        for (int idx : indices) {
            if (idx < 0 || idx >= static_cast<int>(activeChildren->size())) continue;
            if (auto c = evalNode(*(*activeChildren)[static_cast<std::size_t>(idx)], xform, color))
                all.push_back(std::move(c));
        }
    }

    // Restore the callee's environment now that caller-scope evaluation is done
    m_interp->restoreEnv(std::move(calleeEnv));

    // Restore the frame
    m_childrenStack.push_back(std::move(frame));

    if (all.empty())
        return nullptr;
    if (all.size() == 1)
        return std::move(all[0]);

    CsgBoolean u;
    u.op = CsgBoolean::Op::Union;
    u.color = color;
    u.children = std::move(all);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// Extrusion — build a CsgExtrusion from an ExtrusionNode
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalExtrusion(const ExtrusionNode& e, const glm::mat4& xform,
                                       const ColorAttr& color) {
    CsgExtrusion ext;
    ext.kind = (e.kind == ExtrusionNode::Kind::Linear) ? CsgExtrusion::Kind::Linear
                                                       : CsgExtrusion::Kind::Rotate;
    ext.transform = xform;
    ext.color = color;

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
    auto savedEnv = m_interp->snapshotEnv();
    for (const auto& child : e.children) {
        if (auto c = evalNode(*child, identity, color))
            ext.children.push_back(std::move(c));
    }
    m_interp->restoreEnv(std::move(savedEnv));

    return makeExtrusion(std::move(ext));
}

// ---------------------------------------------------------------------------
// offset() — resolve r/delta/chamfer/$fn to doubles; children are evaluated
// in local space (like extrusion/hull/minkowski) since offsetting isn't
// equivariant under arbitrary per-child transforms.
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalOffset(const OffsetNode& o, const glm::mat4& xform,
                                    const ColorAttr& color) {
    CsgOffset off;
    off.transform = xform;
    off.color = color;

    for (const auto& [name, exprPtr] : o.params) {
        if (name == "chamfer") {
            Value cv = m_interp->evaluate(*exprPtr);
            off.params["chamfer"] = bool(cv) ? 1.0 : 0.0;
        } else {
            off.params[name] = m_interp->evalNumber(*exprPtr);
        }
    }

    const glm::mat4 identity{1.0f};
    auto savedEnv = m_interp->snapshotEnv();
    for (const auto& child : o.children) {
        if (auto c = evalNode(*child, identity, color))
            off.children.push_back(std::move(c));
    }
    m_interp->restoreEnv(std::move(savedEnv));

    return makeOffset(std::move(off));
}

// ---------------------------------------------------------------------------
// projection() — resolve "cut" to a bool; children are 3-D geometry
// evaluated in local space (their own translate/rotate/scale still apply,
// but not whatever's wrapping the projection() call itself — that outer
// transform is stored on the node and applied to the 2-D result instead,
// same treatment as offset()/extrusion).
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalProjection(const ProjectionNode& p, const glm::mat4& xform,
                                        const ColorAttr& color) {
    CsgProjection proj;
    proj.transform = xform;
    proj.color = color;

    if (auto it = p.params.find("cut"); it != p.params.end())
        proj.cut = bool(m_interp->evaluate(*it->second));

    const glm::mat4 identity{1.0f};
    auto savedEnv = m_interp->snapshotEnv();
    for (const auto& child : p.children) {
        if (auto c = evalNode(*child, identity, color))
            proj.children.push_back(std::move(c));
    }
    m_interp->restoreEnv(std::move(savedEnv));

    return makeProjection(std::move(proj));
}

// ---------------------------------------------------------------------------
// Shared helpers for the file-loading builtins (import()/surface()). File IO
// happens eagerly in evalImport()/evalSurface() below (not deferred to
// PrimitiveGen) specifically so a missing file or unreadable/malformed data
// can surface as a Diagnostic instead of silently producing empty geometry;
// see CsgLeaf::meshPositions in CsgNode.h.
// ---------------------------------------------------------------------------
void CsgEvaluator::reportEvalError(const lang::SourceLoc& loc, std::string msg) {
    if (m_scene) {
        lang::Diagnostic d;
        d.level = lang::DiagLevel::Error;
        d.loc = loc;
        d.filePath = resolveFilePath(loc.fileId);
        d.message = std::move(msg);
        m_scene->evalDiags.push_back(std::move(d));
    }
}

std::string CsgEvaluator::resolveFilePath(uint32_t fileId) const {
    if (fileTable && fileId < fileTable->size())
        return (*fileTable)[fileId];
    return {};
}

// Shared by import()/surface(): resolves the file-path argument (first
// positional argument wins if given at all, regardless of where among
// call.args it appears; file=/filename= is only a fallback for when no
// positional argument was given — a deliberate, order-independent
// precedence, since "whichever of a positional/named pair happens to come
// first in the source" would silently pick a different file depending on
// argument order alone), evaluates it, checks it's a string, and resolves
// it (if relative) against the directory of the file containing the call,
// falling back to baseDir. Reports a Diagnostic and returns std::nullopt on
// any failure.
std::optional<std::filesystem::path>
CsgEvaluator::resolveFilePathArg(const ModuleCallNode& call, std::string_view builtinName) {
    const lang::ExprNode* positional = nullptr;
    const lang::ExprNode* named = nullptr;
    for (const auto& arg : call.args) {
        if (arg.name.empty()) {
            if (!positional)
                positional = arg.value.get();
        } else if (arg.name == "file" || arg.name == "filename")
            named = arg.value.get();
    }
    const lang::ExprNode* pathNode = positional ? positional : named;
    if (!pathNode) {
        reportEvalError(call.loc, std::string(builtinName) + "() requires a file path");
        return std::nullopt;
    }

    Value pathVal = m_interp->evaluate(*pathNode);
    if (!pathVal.isString()) {
        reportEvalError(call.loc, std::string(builtinName) + "(): file path must be a string");
        return std::nullopt;
    }

    std::filesystem::path filePath = chisel::util::utf8ToPath(pathVal.asString());
    if (filePath.is_relative()) {
        // Prefer the directory of the file that actually contains this call
        // (via fileTable) over baseDir (always the root file's directory) —
        // matters when the call is reached through include/use, since
        // OpenSCAD resolves a relative import()/surface() path relative to
        // the file it's written in, not the root file being built.
        std::string callerFile = resolveFilePath(call.loc.fileId);
        std::filesystem::path dir =
            callerFile.empty() ? baseDir : std::filesystem::path(callerFile).parent_path();
        filePath = dir / filePath;
    }
    return filePath;
}

// import(file) / import(file="...") [, layer="..."] — loads external
// geometry into a Mesh or Polygon2D leaf. Dispatched by filename suffix
// (case-insensitive, via chisel::util::hasSuffixCI — not
// std::filesystem::path::extension(), which treats a file literally named
// e.g. ".stl" as having *no* extension per the standard's "dotfile" rule).
// .stl/.off/.3mf/.amf are 3-D triangle-soup formats -> Mesh; .dxf/.svg are
// 2-D outline formats -> Polygon2D, matching OpenSCAD's own format-determines-dimension
// behavior for import(). `layer=` is DXF-specific (see DxfLoader.h) and
// ignored for every other format, matching OpenSCAD (SVG import() has no
// layer concept).
CsgNodePtr CsgEvaluator::evalImport(const ModuleCallNode& call, const glm::mat4& xform,
                                    const ColorAttr& color) {
    auto filePath = resolveFilePathArg(call, "import");
    if (!filePath)
        return nullptr;

    if (chisel::util::hasSuffixCI(*filePath, ".dxf") ||
        chisel::util::hasSuffixCI(*filePath, ".svg")) {
        std::string layer;
        for (const auto& arg : call.args) {
            if (arg.name == "layer") {
                Value v = m_interp->evaluate(*arg.value);
                if (v.isString())
                    layer = v.asString();
            }
        }

        chisel::io::RawPolygon2D poly = chisel::util::hasSuffixCI(*filePath, ".dxf")
                                            ? chisel::io::loadDxfPaths(*filePath, layer)
                                            : chisel::io::loadSvgPaths(*filePath);
        if (!poly.error.empty()) {
            reportEvalError(call.loc, "import(): " + poly.error);
            return nullptr;
        }

        CsgLeaf leaf;
        leaf.kind = CsgLeaf::Kind::Polygon2D;
        leaf.transform = xform;
        leaf.color = color;
        leaf.polyPoints = std::move(poly.points);
        leaf.polyPaths = std::move(poly.paths);
        return makeLeaf(std::move(leaf));
    }

    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;

    if (chisel::util::hasSuffixCI(*filePath, ".stl")) {
        chisel::io::RawStlMesh mesh = chisel::io::loadStlRaw(*filePath);
        if (!mesh.error.empty()) {
            reportEvalError(call.loc, "import(): " + mesh.error);
            return nullptr;
        }
        positions = std::move(mesh.positions);
        indices = std::move(mesh.indices);
    } else if (chisel::util::hasSuffixCI(*filePath, ".off")) {
        chisel::io::RawOffMesh mesh = chisel::io::loadOffMesh(*filePath);
        if (!mesh.error.empty()) {
            reportEvalError(call.loc, "import(): " + mesh.error);
            return nullptr;
        }
        positions = std::move(mesh.positions);
        indices = std::move(mesh.indices);
    } else if (chisel::util::hasSuffixCI(*filePath, ".3mf")) {
        chisel::io::RawThreeMfMesh mesh = chisel::io::loadThreeMfMesh(*filePath);
        if (!mesh.error.empty()) {
            reportEvalError(call.loc, "import(): " + mesh.error);
            return nullptr;
        }
        positions = std::move(mesh.positions);
        indices = std::move(mesh.indices);
    } else if (chisel::util::hasSuffixCI(*filePath, ".amf")) {
        chisel::io::RawAmfMesh mesh = chisel::io::loadAmfMesh(*filePath);
        if (!mesh.error.empty()) {
            reportEvalError(call.loc, "import(): " + mesh.error);
            return nullptr;
        }
        positions = std::move(mesh.positions);
        indices = std::move(mesh.indices);
    } else {
        reportEvalError(
            call.loc,
            "import(): unsupported file format '" + filePath->extension().string() +
                "' — only .stl, .off, .3mf, .amf, .dxf, and .svg are currently supported");
        return nullptr;
    }

    CsgLeaf leaf;
    leaf.kind = CsgLeaf::Kind::Mesh;
    leaf.transform = xform;
    leaf.color = color;
    leaf.meshPositions = std::move(positions);
    leaf.meshIndices = std::move(indices);
    return makeLeaf(std::move(leaf));
}

// ---------------------------------------------------------------------------
// surface(file [, center=false] [, invert=false] [, convexity=]) — loads a
// text heightmap (.dat) into a Mesh leaf, exactly like import(); the actual
// grid parsing and solid triangulation lives in SurfaceLoader.h so this stays
// a thin orchestration layer (argument resolution + diagnostics), matching
// evalImport() above. `convexity` is a preview-only hint (like render()'s),
// parsed for compatibility and discarded.
//
// Unlike a user-defined module's arguments, center/invert are only matched
// by name here (not positionally) — real-world surface() calls essentially
// always name them, and adding full positional-parameter-list binding for
// one builtin isn't worth the complexity.
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalSurface(const ModuleCallNode& call, const glm::mat4& xform,
                                     const ColorAttr& color) {
    auto filePath = resolveFilePathArg(call, "surface");
    if (!filePath)
        return nullptr;

    bool center = false;
    bool invert = false;
    for (const auto& arg : call.args) {
        if (arg.name == "center")
            center = bool(m_interp->evaluate(*arg.value));
        else if (arg.name == "invert")
            invert = bool(m_interp->evaluate(*arg.value));
        // "file"/"filename" already consumed by resolveFilePathArg() above;
        // "convexity" and any other named arg: ignored.
    }

    chisel::io::RawSurfaceMesh mesh = chisel::io::loadSurfaceMesh(*filePath, center, invert);
    if (!mesh.error.empty()) {
        reportEvalError(call.loc, "surface(): " + mesh.error);
        return nullptr;
    }

    CsgLeaf leaf;
    leaf.kind = CsgLeaf::Kind::Mesh;
    leaf.transform = xform;
    leaf.color = color;
    leaf.meshPositions = std::move(mesh.positions);
    leaf.meshIndices = std::move(mesh.indices);
    return makeLeaf(std::move(leaf));
}

// ---------------------------------------------------------------------------
// text(t [, size=10] [, font=] [, halign="left"] [, valign="baseline"]
//      [, spacing=1] [, $fn=]) — builds a Polygon2D leaf from shaped glyph
//      outlines, exactly like polygon(): one path per glyph contour; the
//      EvenOdd fill rule PrimitiveGen.cpp already uses for Polygon2D turns
//      nested contours (e.g. the counter of an "O") into holes
//      automatically, so no new CsgLeaf::Kind was needed. Font parsing
//      happens eagerly here (not deferred to PrimitiveGen), same rationale
//      as import()/surface() — see the comment above reportEvalError().
//
// `t` is matched positionally or via `text=` (that's OpenSCAD's actual
// keyword name for this parameter, unlike file/filename for import()/
// surface()). `font` defaults to the bundled Roboto (defaultFontPath())
// when omitted/empty; otherwise resolved exactly like import()/surface()
// paths (relative to baseDir). `direction`/`language`/`script` are
// accepted and discarded, like surface()'s `convexity` — see
// TextLoader.h for the full scope writeup (no ligatures/bidi/
// complex-script shaping in this first cut). `font`, if relative, resolves
// against the directory of the file containing this text() call (like
// import()/surface() — see resolveFilePathArg()), falling back to baseDir.
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalText(const ModuleCallNode& call, const glm::mat4& xform,
                                  const ColorAttr& color) {
    const lang::ExprNode* positional = nullptr;
    const lang::ExprNode* named = nullptr;
    for (const auto& arg : call.args) {
        if (arg.name.empty()) {
            if (!positional)
                positional = arg.value.get();
        } else if (arg.name == "text")
            named = arg.value.get();
    }
    const lang::ExprNode* textNode = positional ? positional : named;
    if (!textNode) {
        reportEvalError(call.loc, "text() requires a string");
        return nullptr;
    }

    Value textVal = m_interp->evaluate(*textNode);
    if (!textVal.isString()) {
        reportEvalError(call.loc, "text(): argument must be a string");
        return nullptr;
    }

    double size = 10.0, spacing = 1.0, fnOvr = 0.0;
    std::string font, halign = "left", valign = "baseline";
    for (const auto& arg : call.args) {
        if (arg.name == "size")
            size = m_interp->evalNumber(*arg.value);
        else if (arg.name == "spacing")
            spacing = m_interp->evalNumber(*arg.value);
        else if (arg.name == "$fn")
            fnOvr = m_interp->evalNumber(*arg.value);
        else if (arg.name == "font") {
            Value v = m_interp->evaluate(*arg.value);
            if (v.isString())
                font = v.asString();
        } else if (arg.name == "halign") {
            Value v = m_interp->evaluate(*arg.value);
            if (v.isString())
                halign = v.asString();
        } else if (arg.name == "valign") {
            Value v = m_interp->evaluate(*arg.value);
            if (v.isString())
                valign = v.asString();
        }
        // "text"/positional already consumed above; "direction"/"language"/
        // "script": accepted, discarded (see class comment above).
    }

    std::filesystem::path fontPath;
    if (font.empty()) {
        fontPath = defaultFontPath();
    } else {
        fontPath = chisel::util::utf8ToPath(font);
        if (fontPath.is_relative()) {
            std::string callerFile = resolveFilePath(call.loc.fileId);
            std::filesystem::path dir =
                callerFile.empty() ? baseDir : std::filesystem::path(callerFile).parent_path();
            fontPath = dir / fontPath;
        }
    }

    chisel::io::RawTextOutline outline = chisel::io::loadTextOutline(
        fontPath, textVal.asString(), size, halign, valign, spacing, fnOvr);
    if (!outline.error.empty()) {
        reportEvalError(call.loc, "text(): " + outline.error);
        return nullptr;
    }
    if (outline.points.empty())
        return nullptr; // e.g. text("") — no geometry, not an error

    CsgLeaf leaf;
    leaf.kind = CsgLeaf::Kind::Polygon2D;
    leaf.transform = xform;
    leaf.color = color;
    leaf.polyPoints = std::move(outline.points);
    leaf.polyPaths = std::move(outline.paths);
    return makeLeaf(std::move(leaf));
}

// ---------------------------------------------------------------------------
// polyhedron(points=[[x,y,z],...], faces=[[i,j,k,...],...]) — builds a Mesh-
// shaped leaf directly from caller-supplied vertices/faces, fan-
// triangulating each (assumed planar/convex) face; PrimitiveGen's existing
// Mesh/Polyhedron case (index-bounds checking + MeshGL::Merge() weld, see
// PrimitiveGen.cpp) handles the rest unchanged, same as import()/surface().
// `triangles=` is accepted as an alias for `faces=` (OpenSCAD's older name
// for the same argument). `convexity=` is accepted and discarded, like
// surface()'s.
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalPolyhedron(const ModuleCallNode& call, const glm::mat4& xform,
                                        const ColorAttr& color) {
    const lang::ExprNode* pointsExpr = nullptr;
    const lang::ExprNode* facesExpr = nullptr;
    for (const auto& arg : call.args) {
        if (arg.name == "points")
            pointsExpr = arg.value.get();
        else if (arg.name == "faces" || arg.name == "triangles")
            facesExpr = arg.value.get();
        else if (arg.name.empty()) {
            if (!pointsExpr)
                pointsExpr = arg.value.get();
            else if (!facesExpr)
                facesExpr = arg.value.get();
        }
    }
    if (!pointsExpr || !facesExpr) {
        reportEvalError(call.loc, "polyhedron() requires points= and faces=");
        return nullptr;
    }

    Value pointsVal = m_interp->evaluate(*pointsExpr);
    if (!pointsVal.isVector()) {
        reportEvalError(call.loc, "polyhedron(): points must be a vector of [x,y,z] points");
        return nullptr;
    }
    std::vector<glm::vec3> positions;
    positions.reserve(pointsVal.asVec().size());
    for (const auto& pt : pointsVal.asVec()) {
        if (!pt.isVector() || pt.asVec().size() < 3) {
            reportEvalError(call.loc, "polyhedron(): each point must be an [x,y,z] vector");
            return nullptr;
        }
        const auto& v = pt.asVec();
        positions.push_back({static_cast<float>(v[0].asNumber()),
                             static_cast<float>(v[1].asNumber()),
                             static_cast<float>(v[2].asNumber())});
    }

    Value facesVal = m_interp->evaluate(*facesExpr);
    if (!facesVal.isVector()) {
        reportEvalError(call.loc, "polyhedron(): faces must be a vector of index lists");
        return nullptr;
    }

    const std::size_t numPoints = positions.size();
    std::vector<uint32_t> indices;
    for (const auto& face : facesVal.asVec()) {
        if (!face.isVector() || face.asVec().size() < 3) {
            reportEvalError(call.loc, "polyhedron(): each face must list at least 3 point indices");
            return nullptr;
        }
        const auto& fv = face.asVec();
        std::vector<uint32_t> faceIdx;
        faceIdx.reserve(fv.size());
        for (const auto& idxVal : fv) {
            if (!idxVal.isNumber()) {
                reportEvalError(call.loc, "polyhedron(): face indices must be numbers");
                return nullptr;
            }
            long long i = static_cast<long long>(idxVal.asNumber());
            if (i < 0 || static_cast<std::size_t>(i) >= numPoints) {
                reportEvalError(call.loc,
                                "polyhedron(): face references an out-of-range point index");
                return nullptr;
            }
            faceIdx.push_back(static_cast<uint32_t>(i));
        }
        // Fan-triangulate from the face's first vertex — OpenSCAD faces are
        // assumed planar/convex, matching OpenSCAD's own triangulation for
        // that common case.
        for (std::size_t i = 1; i + 1 < faceIdx.size(); ++i) {
            indices.push_back(faceIdx[0]);
            indices.push_back(faceIdx[i]);
            indices.push_back(faceIdx[i + 1]);
        }
    }

    CsgLeaf leaf;
    leaf.kind = CsgLeaf::Kind::Polyhedron;
    leaf.transform = xform;
    leaf.color = color;
    leaf.meshPositions = std::move(positions);
    leaf.meshIndices = std::move(indices);
    return makeLeaf(std::move(leaf));
}

// ---------------------------------------------------------------------------
// resize(newsize=[x,y,z] [, auto=false|[bx,by,bz]]) { children } — unlike
// scale() (a pure AST-time matrix fold), resize's scale factor depends on
// the tessellated bounding box of its children, which only exists once
// MeshEvaluator has an actual Manifold to measure — so this only resolves
// newsize/auto here and defers the bbox-driven scale computation to
// MeshEvaluator::evalResize(). Children are evaluated in local space and the
// outer transform stored separately, same treatment as offset()/
// projection()/hull()/minkowski().
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalResize(const ModuleCallNode& call, const glm::mat4& xform,
                                    const ColorAttr& color) {
    CsgResize r;
    r.transform = xform;
    r.color = color;

    const lang::ExprNode* newsizeExpr = nullptr;
    const lang::ExprNode* autoExpr = nullptr;
    for (const auto& arg : call.args) {
        if (arg.name == "newsize")
            newsizeExpr = arg.value.get();
        else if (arg.name == "auto")
            autoExpr = arg.value.get();
        else if (arg.name.empty()) {
            if (!newsizeExpr)
                newsizeExpr = arg.value.get();
            else if (!autoExpr)
                autoExpr = arg.value.get();
        }
    }

    if (newsizeExpr) {
        Value nv = m_interp->evaluate(*newsizeExpr);
        if (nv.isVector()) {
            const auto& v = nv.asVec();
            if (v.size() >= 1 && v[0].isNumber())
                r.newX = v[0].asNumber();
            if (v.size() >= 2 && v[1].isNumber())
                r.newY = v[1].asNumber();
            if (v.size() >= 3 && v[2].isNumber())
                r.newZ = v[2].asNumber();
        }
    }
    if (autoExpr) {
        Value av = m_interp->evaluate(*autoExpr);
        if (av.isVector()) {
            const auto& v = av.asVec();
            if (v.size() >= 1)
                r.autoX = bool(v[0]);
            if (v.size() >= 2)
                r.autoY = bool(v[1]);
            if (v.size() >= 3)
                r.autoZ = bool(v[2]);
        } else {
            r.autoX = r.autoY = r.autoZ = bool(av);
        }
    }

    const glm::mat4 identity{1.0f};
    auto savedEnv = m_interp->snapshotEnv();
    for (const auto& child : call.children) {
        if (auto c = evalNode(*child, identity, color))
            r.children.push_back(std::move(c));
    }
    m_interp->restoreEnv(std::move(savedEnv));

    return makeResize(std::move(r));
}

// ---------------------------------------------------------------------------
// let — bind variables in scope, evaluate children, restore
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalLet(const LetNode& node, const glm::mat4& xform,
                                 const ColorAttr& color) {
    auto savedEnv = m_interp->snapshotEnv();
    for (const auto& [name, valExpr] : node.bindings)
        m_interp->assignVar(name, *valExpr);

    std::vector<CsgNodePtr> all;
    for (const auto& child : node.children) {
        if (auto c = evalNode(*child, xform, color))
            all.push_back(std::move(c));
    }
    m_interp->restoreEnv(std::move(savedEnv));

    if (all.empty())
        return nullptr;
    if (all.size() == 1)
        return std::move(all[0]);

    CsgBoolean u;
    u.op = CsgBoolean::Op::Union;
    u.color = color;
    u.children = std::move(all);
    return makeBoolean(std::move(u));
}

// ---------------------------------------------------------------------------
// color() — resolves its (optional) color/alpha expressions into a tint
// that overrides the inherited ColorAttr for this subtree only. A nested
// color() further inside always wins over this one for its own children.
// ---------------------------------------------------------------------------
CsgNodePtr CsgEvaluator::evalColor(const ColorNode& node, const glm::mat4& xform,
                                   const ColorAttr& color) {
    ColorAttr cur = color;

    if (node.colorExpr) {
        Value cv = m_interp->evaluate(*node.colorExpr);
        glm::vec4 rgba;
        if (resolveColor(cv, rgba)) {
            cur.has = true;
            cur.value = rgba;
        }
    }
    if (node.alphaExpr) {
        cur.has = true;
        cur.value.a = static_cast<float>(m_interp->evalNumber(*node.alphaExpr));
    }

    std::vector<CsgNodePtr> children;
    children.reserve(node.children.size());
    auto savedEnv = m_interp->snapshotEnv();
    for (const auto& child : node.children) {
        if (auto c = evalNode(*child, xform, cur))
            children.push_back(std::move(c));
    }
    m_interp->restoreEnv(std::move(savedEnv));

    if (children.empty())
        return nullptr;
    if (children.size() == 1)
        return children[0];

    CsgBoolean u;
    u.op = CsgBoolean::Op::Union;
    u.color = cur;
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
        if (v.size() < 3)
            return false;
        out = glm::vec4(static_cast<float>(v[0].asNumber()), static_cast<float>(v[1].asNumber()),
                        static_cast<float>(v[2].asNumber()),
                        v.size() >= 4 ? static_cast<float>(v[3].asNumber()) : 1.0f);
        return true;
    }
    if (!c.isString())
        return false;
    const std::string& s = c.asString();

    if (!s.empty() && s[0] == '#') {
        auto hexDigit = [](char ch) -> int {
            if (ch >= '0' && ch <= '9')
                return ch - '0';
            if (ch >= 'a' && ch <= 'f')
                return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F')
                return ch - 'A' + 10;
            return -1;
        };
        auto byteAt = [&](std::size_t i) -> int {
            int hi = hexDigit(s[i]), lo = hexDigit(s[i + 1]);
            return (hi < 0 || lo < 0) ? -1 : (hi << 4) | lo;
        };
        if (s.size() != 7 && s.size() != 9)
            return false;
        int r = byteAt(1), g = byteAt(3), b = byteAt(5);
        int a = (s.size() == 9) ? byteAt(7) : 255;
        if (r < 0 || g < 0 || b < 0 || a < 0)
            return false;
        out = glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
        return true;
    }

    static const std::unordered_map<std::string, glm::vec3> kNamedColors = {
        {"black", {0.00f, 0.00f, 0.00f}},     {"white", {1.00f, 1.00f, 1.00f}},
        {"red", {1.00f, 0.00f, 0.00f}},       {"lime", {0.00f, 1.00f, 0.00f}},
        {"green", {0.00f, 0.50f, 0.00f}},     {"blue", {0.00f, 0.00f, 1.00f}},
        {"yellow", {1.00f, 1.00f, 0.00f}},    {"cyan", {0.00f, 1.00f, 1.00f}},
        {"magenta", {1.00f, 0.00f, 1.00f}},   {"orange", {1.00f, 0.65f, 0.00f}},
        {"purple", {0.50f, 0.00f, 0.50f}},    {"pink", {1.00f, 0.75f, 0.80f}},
        {"gray", {0.50f, 0.50f, 0.50f}},      {"grey", {0.50f, 0.50f, 0.50f}},
        {"silver", {0.75f, 0.75f, 0.75f}},    {"gold", {1.00f, 0.84f, 0.00f}},
        {"brown", {0.65f, 0.16f, 0.16f}},     {"navy", {0.00f, 0.00f, 0.50f}},
        {"teal", {0.00f, 0.50f, 0.50f}},      {"maroon", {0.50f, 0.00f, 0.00f}},
        {"olive", {0.50f, 0.50f, 0.00f}},     {"indigo", {0.29f, 0.00f, 0.51f}},
        {"violet", {0.93f, 0.51f, 0.93f}},    {"coral", {1.00f, 0.50f, 0.31f}},
        {"salmon", {0.98f, 0.50f, 0.45f}},    {"khaki", {0.94f, 0.90f, 0.55f}},
        {"plum", {0.87f, 0.63f, 0.87f}},      {"orchid", {0.85f, 0.44f, 0.84f}},
        {"turquoise", {0.25f, 0.88f, 0.82f}}, {"tan", {0.82f, 0.71f, 0.55f}},
        {"beige", {0.96f, 0.96f, 0.86f}},     {"ivory", {1.00f, 1.00f, 0.94f}},
        {"crimson", {0.86f, 0.08f, 0.24f}},   {"chocolate", {0.82f, 0.41f, 0.12f}},
        {"lavender", {0.90f, 0.90f, 0.98f}},  {"skyblue", {0.53f, 0.81f, 0.92f}},
        {"lightblue", {0.68f, 0.85f, 0.90f}}, {"darkgray", {0.66f, 0.66f, 0.66f}},
        {"darkgrey", {0.66f, 0.66f, 0.66f}},  {"lightgray", {0.83f, 0.83f, 0.83f}},
        {"lightgrey", {0.83f, 0.83f, 0.83f}},
    };

    std::string lower;
    lower.reserve(s.size());
    for (char ch : s)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    auto it = kNamedColors.find(lower);
    if (it == kNamedColors.end())
        return false;
    out = glm::vec4(it->second, 1.0f);
    return true;
}

} // namespace chisel::csg
