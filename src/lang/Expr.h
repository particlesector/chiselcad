#pragma once
#include "Token.h"
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace chisel::lang {

// ---------------------------------------------------------------------------
// Forward declarations — allows ExprPtr to be used inside node structs
// before ExprNode is fully defined (same pattern as AST.h).
// ---------------------------------------------------------------------------
struct NumberLit;
struct BoolLit;
struct VectorLit;
struct VarRef;
struct BinaryExpr;
struct UnaryExpr;
struct FunctionCall;

using ExprNode = std::variant<NumberLit, BoolLit, VectorLit, VarRef,
                               BinaryExpr, UnaryExpr, FunctionCall>;
using ExprPtr  = std::unique_ptr<ExprNode>;

template<typename T>
inline ExprPtr makeExpr(T node) {
    return std::make_unique<ExprNode>(std::move(node));
}

// ---------------------------------------------------------------------------
// Leaf nodes
// ---------------------------------------------------------------------------
struct NumberLit {
    double    value = 0.0;
    SourceLoc loc;
};

struct BoolLit {
    bool      value = false;
    SourceLoc loc;
};

struct VectorLit {
    std::vector<ExprPtr> elements; // each element is an expression
    SourceLoc loc;
};

struct VarRef {
    std::string name;
    SourceLoc   loc;
};

// ---------------------------------------------------------------------------
// Composite nodes
// ---------------------------------------------------------------------------
struct BinaryExpr {
    enum class Op {
        Add, Sub, Mul, Div, Mod,    // arithmetic
        Eq,  Ne,                     // equality
        Lt,  Le,  Gt,  Ge,          // comparison
        And, Or                      // logical
    };
    Op        op;
    ExprPtr   left;
    ExprPtr   right;
    SourceLoc loc;
};

struct UnaryExpr {
    enum class Op { Neg, Not };
    Op        op;
    ExprPtr   operand;
    SourceLoc loc;
};

struct FunctionCall {
    std::string          name;
    std::vector<ExprPtr> args; // positional arguments
    SourceLoc            loc;
};

} // namespace chisel::lang
