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
// ---------------------------------------------------------------------------
class Interpreter {
public:
    // Populate the environment from variable assignments.
    void loadAssignments(const ParseResult& result);

    // Register user-defined functions (non-owning pointers into result).
    // result must outlive this interpreter instance.
    void loadFunctions(const ParseResult& result);

    // Evaluate an expression to a Value.
    Value evaluate(const ExprNode& expr);

    // Convenience: evaluate and coerce to double (undef → 0.0).
    double evalNumber(const ExprNode& expr);

    // Evaluate a VectorLit and return the first three elements as doubles.
    std::array<double, 3> evalVec3(const ExprNode& expr);

    // For-loop / module call variable binding.
    Value       getVar(const std::string& name) const;
    void        setVar(const std::string& name, Value val);

    // Environment snapshot/restore for scoping.
    std::unordered_map<std::string, Value> snapshotEnv() const { return m_env; }
    void restoreEnv(std::unordered_map<std::string, Value> env) { m_env = std::move(env); }

private:
    std::unordered_map<std::string, Value>             m_env;
    std::unordered_map<std::string, const FunctionDef*> m_funcDefs;

    Value callBuiltin(const std::string& name,
                      const std::vector<Value>& args) const;
};

} // namespace chisel::lang
