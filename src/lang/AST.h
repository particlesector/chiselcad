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

// ---------------------------------------------------------------------------
// AstNode — the top-level variant
// All nodes are heap-allocated via unique_ptr so the tree is
// easy to move/own and the variant stays small.
// ---------------------------------------------------------------------------
using AstNode    = std::variant<PrimitiveNode, BooleanNode, TransformNode, IfNode, ForNode>;
using AstNodePtr = std::unique_ptr<AstNode>;

// ---------------------------------------------------------------------------
// PrimitiveNode — cube / sphere / cylinder
// ---------------------------------------------------------------------------
struct PrimitiveNode {
    enum class Kind { Cube, Sphere, Cylinder };

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
    enum class Kind { Translate, Rotate, Scale, Mirror };

    Kind kind;
    // [x, y, z] vector argument — stored as a VectorLit expression so
    // that variable references like translate([dx, 0, 0]) work.
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
// ParseResult — the output of a successful parse
// ---------------------------------------------------------------------------
struct ParseResult {
    std::vector<AstNodePtr>  roots;       // geometry-producing top-level nodes
    std::vector<AssignStmt>  assignments; // variable assignments (x = expr;)
    double globalFn = 0.0;               // $fn if set at file scope (0 = unset)
    double globalFs = 2.0;               // $fs default
    double globalFa = 12.0;             // $fa default
};

} // namespace chisel::lang
