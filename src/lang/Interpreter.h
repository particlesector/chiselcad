#pragma once
#include "AST.h"
#include "Expr.h"
#include "Value.h"
#include <array>
#include <string>
#include <unordered_map>

namespace chisel::lang {

// ---------------------------------------------------------------------------
// Interpreter — evaluates ExprNode trees against a variable environment.
//
// Usage:
//   Interpreter interp;
//   interp.loadAssignments(parseResult);   // populate env from x = expr;
//   double r = interp.evalNumber(*expr);   // resolve a param expression
// ---------------------------------------------------------------------------
class Interpreter {
public:
    // Populate the environment by evaluating all AssignStmts in the result.
    void loadAssignments(const ParseResult& result);

    // Evaluate an expression to a Value.
    Value evaluate(const ExprNode& expr) const;

    // Convenience: evaluate and coerce to double (undef → 0.0).
    double evalNumber(const ExprNode& expr) const;

    // Evaluate a VectorLit and return the first three elements as doubles.
    // Missing elements default to 0.0.
    std::array<double, 3> evalVec3(const ExprNode& expr) const;

private:
    std::unordered_map<std::string, Value> m_env;

    Value callBuiltin(const std::string& name,
                      const std::vector<Value>& args) const;
};

} // namespace chisel::lang
