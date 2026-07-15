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
struct StringLit;
struct VectorLit;
struct VarRef;
struct BinaryExpr;
struct UnaryExpr;
struct TernaryExpr;
struct IndexExpr;
struct LetExpr;
struct FunctionCall;
struct RangeLit;
struct ListCompExpr;
struct FunctionLit;

using ExprNode = std::variant<NumberLit, BoolLit, UndefLit, StringLit, VectorLit, VarRef,
                               BinaryExpr, UnaryExpr, TernaryExpr, IndexExpr,
                               LetExpr, FunctionCall, RangeLit, ListCompExpr, FunctionLit>;
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

struct StringLit {
    std::string value;
    SourceLoc   loc;
};

// One element of a vector/list literal: a plain expression, or `each expr`
// which flattens expr's elements into the surrounding list instead of
// nesting expr itself as a single element (e.g. `[each [1,2], 3] == [1,2,3]`).
struct VectorElem {
    ExprPtr value;
    bool    isEach = false;
};

struct VectorLit {
    std::vector<VectorElem> elements;
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

// ---------------------------------------------------------------------------
// Range literal — [start:end] or [start:step:end], usable as a general
// expression (assigned to a variable, passed as an argument, echoed, used as
// the source of a for loop or list comprehension), not just inside a `for`
// header. step is nullptr when omitted (defaults to 1 at evaluation time).
// ---------------------------------------------------------------------------
struct RangeLit {
    ExprPtr   start;
    ExprPtr   step; // nullptr means step of 1
    ExprPtr   end;
    SourceLoc loc;
};

// ---------------------------------------------------------------------------
// List comprehension — [for (var = source) body].
//
// `body` is recursive so `if (cond) body [else body]` and `each expr` can
// nest inside the for-clause (mirroring OpenSCAD's actual comprehension
// grammar): a plain expression contributes one element per iteration; an
// `each` sub-expression flattens its own vector/range into the result
// instead of nesting it; an `if [else]` picks which nested body (if any)
// runs for that iteration. Multi-variable/C-style `for(init;cond;next)` and
// nested `for` clauses inside the body aren't supported — out of scope for
// this pass.
// ---------------------------------------------------------------------------
struct ListCompBody;
using ListCompBodyPtr = std::unique_ptr<ListCompBody>;

struct ListCompBody {
    enum class Kind { Expr, Each, If };
    Kind kind = Kind::Expr;

    ExprPtr expr; // Kind::Expr: the element; Kind::Each: the list/range to flatten in

    ExprPtr         condition; // Kind::If
    ListCompBodyPtr thenBody;  // Kind::If
    ListCompBodyPtr elseBody;  // Kind::If — nullptr when there's no else clause
};

struct ListCompExpr {
    std::string     var;
    ExprPtr         source; // range or list to iterate
    ListCompBodyPtr body;
    SourceLoc       loc;
};

// ---------------------------------------------------------------------------
// Function literal — `function(params) expr`, OpenSCAD 2019.05+.
//
// Unlike FunctionDef (AST.h) — a named `function foo(x) = expr;` declaration
// dispatched by name through FunctionCall — a FunctionLit is itself a
// first-class expression: it evaluates to a closure Value (see Value::Tag::
// Function) that can be assigned to a variable, stored in a list, passed as
// an argument, and returned from another function, then invoked later via
// ordinary call syntax (`f(...)`) wherever `f` names a variable holding one.
// ---------------------------------------------------------------------------
struct FunctionLitParam {
    std::string name;
    ExprPtr     defaultVal; // nullptr = no default (required)
};

struct FunctionLit {
    std::vector<FunctionLitParam> params;
    ExprPtr                       body;
    SourceLoc                     loc;
};

} // namespace chisel::lang
