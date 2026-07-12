#pragma once
#include "Expr.h"
#include "Token.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace chisel::lang {

// Forward declarations
struct PrimitiveNode;
struct BooleanNode;
struct TransformNode;
struct IfNode;
struct ForNode;
struct ModuleCallNode;
struct ExtrusionNode;
struct LetNode;
struct ColorNode;
struct OffsetNode;
struct ProjectionNode;

// ---------------------------------------------------------------------------
// AstNode — the top-level variant
// ---------------------------------------------------------------------------
using AstNode    = std::variant<PrimitiveNode, BooleanNode, TransformNode, IfNode, ForNode, ModuleCallNode, ExtrusionNode, LetNode, ColorNode, OffsetNode, ProjectionNode>;
using AstNodePtr = std::unique_ptr<AstNode>;

// ---------------------------------------------------------------------------
// PrimitiveNode — cube / sphere / cylinder
// ---------------------------------------------------------------------------
struct PrimitiveNode {
    enum class Kind { Cube, Sphere, Cylinder, Square2D, Circle2D, Polygon2D };

    Kind kind;
    // Named params: "r", "h", "r1", "r2", "$fn", "$fs", "$fa", etc.
    // Values are expression trees; the Interpreter resolves them to doubles.
    std::unordered_map<std::string, ExprPtr> params;
    // center is always a boolean literal in practice — kept as bool.
    bool center = false;
    SourceLoc loc;
};

// ---------------------------------------------------------------------------
// BooleanNode — union / difference / intersection
// ---------------------------------------------------------------------------
struct BooleanNode {
    enum class Op { Union, Difference, Intersection, Hull, Minkowski };

    Op op;
    std::vector<AstNodePtr> children;
    SourceLoc loc;
};

// ---------------------------------------------------------------------------
// TransformNode — translate / rotate / scale / mirror
// ---------------------------------------------------------------------------
struct TransformNode {
    // Matrix — multmatrix(m): m is a 4x4 (or 3x4/3x3, auto-padded) nested
    // vector expression, evaluated and applied directly as the local matrix.
    // Identity — render(): no transform of its own, just groups children
    // (render()'s convexity hint is a preview-only concept OpenSCAD uses;
    // ChiselCAD always fully evaluates, so it has nothing to do here).
    enum class Kind { Translate, Rotate, Scale, Mirror, Matrix, Identity };

    Kind kind;
    // [x, y, z] vector argument — stored as a VectorLit expression so
    // that variable references like translate([dx, 0, 0]) work.
    // Unused (nullptr) for Kind::Identity.
    ExprPtr vec;
    std::vector<AstNodePtr> children;
    SourceLoc loc;
};

// ---------------------------------------------------------------------------
// Helpers for constructing heap nodes
// ---------------------------------------------------------------------------
inline AstNodePtr makePrimitive(PrimitiveNode n) {
    return std::make_unique<AstNode>(std::move(n));
}
inline AstNodePtr makeBoolean(BooleanNode n) {
    return std::make_unique<AstNode>(std::move(n));
}
inline AstNodePtr makeTransform(TransformNode n) {
    return std::make_unique<AstNode>(std::move(n));
}

// ---------------------------------------------------------------------------
// IfNode — if (condition) { ... } else { ... }
// ---------------------------------------------------------------------------
struct IfNode {
    ExprPtr                  condition;
    std::vector<AstNodePtr>  thenChildren;
    std::vector<AstNodePtr>  elseChildren; // empty when no else branch
    SourceLoc                loc;
};

inline AstNodePtr makeIf(IfNode n) {
    return std::make_unique<AstNode>(std::move(n));
}

// ---------------------------------------------------------------------------
// ForNode — for (var = [start:step:end]) { ... }
//           for (var = [start:end])      { ... }   (step defaults to 1)
//           for (var = [v0, v1, v2, ...]) { ... }  (explicit list)
// ---------------------------------------------------------------------------
struct ForRange {
    bool             isRange = true;  // true: start/step/end; false: list
    ExprPtr          start;           // range form — required
    ExprPtr          step;            // range form — nullptr means step of 1
    ExprPtr          end;             // range form — required
    std::vector<ExprPtr> list;        // list form
    // true when `list` came from a bracketed literal `[a, b, c]` (each entry
    // is its own loop value, even if it evaluates to a vector); false when it
    // came from `for (v = expr)` (single expr, expanded if it's a vector).
    bool             isBracketedList = false;
};

struct ForNode {
    std::string             var;
    ForRange                range;
    std::vector<AstNodePtr> children;
    SourceLoc               loc;
};

inline AstNodePtr makeFor(ForNode n) {
    return std::make_unique<AstNode>(std::move(n));
}

// ---------------------------------------------------------------------------
// AssignStmt — a variable assignment at file or block scope: x = expr;
// ---------------------------------------------------------------------------
struct AssignStmt {
    std::string name;
    ExprPtr     value;
    SourceLoc   loc;
};

// ---------------------------------------------------------------------------
// ModuleParam — one formal parameter in a module definition
// ---------------------------------------------------------------------------
struct ModuleParam {
    std::string name;
    ExprPtr     defaultVal; // nullptr means no default (required)
};

// ---------------------------------------------------------------------------
// ModuleDef — a user-defined module: module name(params) { body }
// ---------------------------------------------------------------------------
struct ModuleDef {
    std::string              name;
    std::vector<ModuleParam> params;
    std::vector<AstNodePtr>  body;
    SourceLoc                loc;
};

// ---------------------------------------------------------------------------
// ModuleArg — one actual argument in a module call
// ---------------------------------------------------------------------------
struct ModuleArg {
    std::string name;  // empty string = positional argument
    ExprPtr     value;
};

// ---------------------------------------------------------------------------
// ModuleCallNode — a call to a user-defined module: name(args) { children }
// ---------------------------------------------------------------------------
struct ModuleCallNode {
    std::string              name;
    std::vector<ModuleArg>   args;
    std::vector<AstNodePtr>  children; // body passed as children (future use)
    SourceLoc                loc;
};

inline AstNodePtr makeModuleCall(ModuleCallNode n) {
    return std::make_unique<AstNode>(std::move(n));
}

// ---------------------------------------------------------------------------
// ExtrusionNode — linear_extrude / rotate_extrude
// Wraps 2-D children and extrudes them into a 3-D solid.
// ---------------------------------------------------------------------------
struct ExtrusionNode {
    enum class Kind { Linear, Rotate };

    Kind kind;
    // Named params stored as expressions (height, twist, scale, angle, $fn …)
    std::unordered_map<std::string, ExprPtr> params;
    std::vector<AstNodePtr> children; // 2-D geometry
    SourceLoc loc;
};

inline AstNodePtr makeExtrusion(ExtrusionNode n) {
    return std::make_unique<AstNode>(std::move(n));
}

// ---------------------------------------------------------------------------
// FunctionParam — one formal parameter in a function definition
// ---------------------------------------------------------------------------
struct FunctionParam {
    std::string name;
    ExprPtr     defaultVal; // nullptr means no default (required)
};

// ---------------------------------------------------------------------------
// FunctionDef — user-defined function: function name(params) = expr;
// ---------------------------------------------------------------------------
struct FunctionDef {
    std::string               name;
    std::vector<FunctionParam> params;
    ExprPtr                   body; // expression — not a geometry block
    SourceLoc                 loc;
};

// ---------------------------------------------------------------------------
// LetNode — statement-level let: let(x = expr) { children }
// Creates a scoped variable binding for child geometry.
// ---------------------------------------------------------------------------
struct LetNode {
    std::vector<std::pair<std::string, ExprPtr>> bindings;
    std::vector<AstNodePtr> children;
    SourceLoc loc;
};

inline AstNodePtr makeLetNode(LetNode n) {
    return std::make_unique<AstNode>(std::move(n));
}

// ---------------------------------------------------------------------------
// ColorNode — color(c [, alpha]) { children }
// Sets an inherited tint for its subtree, like an attribute alongside the
// transform accumulator. c may be a color name / "#rrggbb"(aa) hex string,
// or an [r,g,b]/[r,g,b,a] vector (components in 0..1); alpha, if given
// (positionally or as alpha=...), overrides the alpha component of c.
// ---------------------------------------------------------------------------
struct ColorNode {
    ExprPtr colorExpr; // nullptr if omitted
    ExprPtr alphaExpr; // nullptr if no explicit alpha override
    std::vector<AstNodePtr> children;
    SourceLoc loc;
};

inline AstNodePtr makeColorNode(ColorNode n) {
    return std::make_unique<AstNode>(std::move(n));
}

// ---------------------------------------------------------------------------
// OffsetNode — offset(r=...) / offset(delta=..., chamfer=...)
// Grows or shrinks 2-D children by a distance, producing a new 2-D shape:
// r gives rounded corners, delta gives straight corners (mitered, or
// beveled when chamfer=true). Params are kept as raw ExprPtr like
// ExtrusionNode's; CsgEvaluator resolves them to concrete doubles.
// ---------------------------------------------------------------------------
struct OffsetNode {
    std::unordered_map<std::string, ExprPtr> params;
    std::vector<AstNodePtr> children; // 2-D geometry
    SourceLoc loc;
};

inline AstNodePtr makeOffset(OffsetNode n) {
    return std::make_unique<AstNode>(std::move(n));
}

// ---------------------------------------------------------------------------
// ProjectionNode — projection(cut = false) { ... }
// Projects 3-D children onto the XY plane: cut=true slices at z=0 (a true
// cross-section through the solid); cut=false (default) computes the full
// silhouette/shadow. Params kept as raw ExprPtr like OffsetNode's chamfer;
// CsgEvaluator resolves "cut" to a bool.
// ---------------------------------------------------------------------------
struct ProjectionNode {
    std::unordered_map<std::string, ExprPtr> params;
    std::vector<AstNodePtr> children; // 3-D geometry
    SourceLoc loc;
};

inline AstNodePtr makeProjection(ProjectionNode n) {
    return std::make_unique<AstNode>(std::move(n));
}

// ---------------------------------------------------------------------------
// IncludeStmt — a file-scope `include <path>;` or `use <path>;` directive.
// The Parser only records these (it does no file I/O); SourceLoader resolves
// them recursively, relative to the directory of the file that contains the
// directive, and splices the target file's ParseResult into this one —
// `Include` merges everything (roots/assignments/moduleDefs/functionDefs),
// `Use` merges only moduleDefs/functionDefs, matching OpenSCAD semantics.
// ---------------------------------------------------------------------------
struct IncludeStmt {
    enum class Kind { Include, Use };

    Kind        kind;
    std::string path; // raw text between '<' and '>', unresolved
    SourceLoc   loc;

    // Snapshot of each destination vector's size (in the *same file's*
    // ParseResult, before splicing) at the point this directive was parsed,
    // so SourceLoader can insert the target file's content at the right
    // textual position instead of always appending it at the end.
    size_t rootsIndex    = 0;
    size_t assignIndex   = 0;
    size_t moduleIndex   = 0;
    size_t functionIndex = 0;
};

// ---------------------------------------------------------------------------
// ParseResult — the output of a successful parse
// ---------------------------------------------------------------------------
struct ParseResult {
    std::vector<AstNodePtr>  roots;        // geometry-producing top-level nodes
    std::vector<AssignStmt>  assignments;  // variable assignments (x = expr;)
    std::vector<ModuleDef>   moduleDefs;   // user-defined module definitions
    std::vector<FunctionDef> functionDefs; // user-defined function definitions
    std::vector<IncludeStmt> includes;     // include<>/use<> directives, in source order
    double globalFn = 0.0;                // $fn if set at file scope (0 = unset)
    double globalFs = 2.0;                // $fs default
    double globalFa = 12.0;              // $fa default
    // Whether $fn/$fs/$fa were explicitly written in this file, as opposed
    // to sitting at the language default — lets SourceLoader tell "the
    // includer explicitly chose the default value" apart from "the includer
    // never set it" when deciding whether an included file's setting applies.
    bool globalFnSet = false;
    bool globalFsSet = false;
    bool globalFaSet = false;
};

} // namespace chisel::lang
