#pragma once
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

// ---------------------------------------------------------------------------
// AstNode — the top-level variant
// All nodes are heap-allocated via unique_ptr so the tree is
// easy to move/own and the variant stays small.
// ---------------------------------------------------------------------------
using AstNode    = std::variant<PrimitiveNode, BooleanNode, TransformNode>;
using AstNodePtr = std::unique_ptr<AstNode>;

// ---------------------------------------------------------------------------
// PrimitiveNode — cube / sphere / cylinder
// ---------------------------------------------------------------------------
struct PrimitiveNode {
    enum class Kind { Cube, Sphere, Cylinder };

    Kind kind;
    // Named params: "r", "h", "r1", "r2", "center", "$fn", "$fs", "$fa", etc.
    std::unordered_map<std::string, double> params;
    // center flag stored separately for clarity
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
    double x = 0.0, y = 0.0, z = 0.0; // the [x, y, z] vector argument
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
// ParseResult — the output of a successful parse
// ---------------------------------------------------------------------------
struct ParseResult {
    std::vector<AstNodePtr> roots; // top-level statements
    double globalFn = 0.0;         // $fn if set at file scope (0 = unset)
    double globalFs = 2.0;         // $fs default
    double globalFa = 12.0;        // $fa default
};

} // namespace chisel::lang
