#pragma once
#include "Token.h"
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace chisel::lang {

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
struct NumberLit;
struct BoolLit;
struct UndefLit;
struct VectorLit;
struct VarRef;
struct BinaryExpr;
struct UnaryExpr;
struct TernaryExpr;
struct IndexExpr;
struct LetExpr;
struct FunctionCall;

using ExprNode = std::variant<NumberLit, BoolLit, UndefLit, VectorLit, VarRef,
                               BinaryExpr, UnaryExpr, TernaryExpr, IndexExpr,
                               LetExpr, FunctionCall>;
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

struct UndefLit {
    SourceLoc loc;
};

struct VectorLit {
    std::vector<ExprPtr> elements;
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
        Add, Sub, Mul, Div, Mod,
        Eq,  Ne,
        Lt,  Le,  Gt,  Ge,
        And, Or
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

// condition ? then : else_
struct TernaryExpr {
    ExprPtr   condition;
    ExprPtr   then;
    ExprPtr   else_;
    SourceLoc loc;
};

// target[index]
struct IndexExpr {
    ExprPtr   target;
    ExprPtr   index;
    SourceLoc loc;
};

// let(x = expr, ...) body_expr
struct LetExpr {
    std::vector<std::pair<std::string, ExprPtr>> bindings;
    ExprPtr   body;
    SourceLoc loc;
};

// One argument in a function call — named or positional
struct FunctionArg {
    std::string name;  // empty = positional
    ExprPtr     value;
};

struct FunctionCall {
    std::string              name;
    std::vector<FunctionArg> args;
    SourceLoc                loc;
};

} // namespace chisel::lang
