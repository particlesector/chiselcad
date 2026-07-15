#pragma once
#include "AST.h"
#include "Expr.h"
#include "Value.h"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

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

    // Expands a range's [start:step:end] bounds into the concrete sequence
    // of values it denotes — shared by for-loops (both the `for (v =
    // [a:b:c])` literal form and `for (v = expr)` when expr is a Range
    // value) and list comprehensions. Empty if step is 0. Capped at
    // kMaxRangeCount so a runaway range (e.g. a typo'd step) can't exhaust
    // memory the way an unbounded for-loop already can't (see ForNode's own
    // matching cap in CsgEvaluator.cpp).
    static constexpr int kMaxRangeCount = 10000;
    std::vector<Value> expandRange(double start, double step, double end) const;

    // Expands a value into the sequence of per-iteration values a for-loop
    // or list-comprehension for-clause visits for it: a Range expands via
    // expandRange(); a Vector iterates its own elements (`for (p = pts)`
    // visits each element of pts); anything else is a single iteration of
    // that value unchanged. Shared by CsgEvaluator::evalFor's expression
    // form and ListCompExpr below.
    std::vector<Value> iterationValues(const Value& v) const;

    // Environment snapshot/restore for scoping.
    std::unordered_map<std::string, Value> snapshotEnv() const { return m_env; }
    void restoreEnv(std::unordered_map<std::string, Value> env) { m_env = std::move(env); }

    // Module-call-name stack backing parent_module()/$parent_modules. Module
    // calls are evaluated by CsgEvaluator, not here, so CsgEvaluator pushes/
    // pops around each user-module call (mirroring how it already sets
    // $children) rather than this class tracking module calls itself.
    void pushModuleName(std::string name) { m_moduleNameStack.push_back(std::move(name)); }
    void popModuleName() { m_moduleNameStack.pop_back(); }

private:
    std::unordered_map<std::string, Value>             m_env;
    std::unordered_map<std::string, const FunctionDef*> m_funcDefs;
    std::vector<std::string>                           m_moduleNameStack;

    // Guards against unbounded recursion in user-defined functions (e.g. a
    // missing base case) blowing the native call stack. Silent cap, no
    // diagnostic, matching the existing `for`-loop kMaxIter convention.
    //
    // Kept conservative rather than generous: each recursive evaluate() call
    // carries several locals (snapshotEnv's unordered_map copy, arg vectors,
    // Value copies), and the smallest stack we need to fit under is MSVC's
    // default 1 MiB thread stack (Windows CI build has no custom /STACK).
    // Empirically, an unguarded 1 MiB-stack GCC Release build overflows
    // between depth 800-1000; this cap stays well under that with margin to
    // spare for MSVC's likely-larger per-frame footprint.
    static constexpr int kMaxCallDepth = 200;
    int m_callDepth = 0;

    // Guards against a nested list comprehension's element count multiplying
    // out of control — each individual range is already capped at
    // kMaxRangeCount, but that cap is per-range, so
    // `[for (i=[0:9999]) [for (j=[0:9999]) i+j]]` would otherwise allocate
    // ~1e8 Values despite neither range exceeding its own cap. m_listCompBudget
    // is shared across an entire (possibly nested) comprehension expression —
    // reset only when the outermost one starts (m_listCompDepth == 0) — and
    // decremented per element actually produced by collectListCompBody, so
    // the total across all nesting levels of one expression is bounded.
    static constexpr long long kMaxListCompElements = 1'000'000;
    long long m_listCompBudget = 0;
    int       m_listCompDepth  = 0;

    Value callBuiltin(const std::string& name,
                      const std::vector<Value>& args) const;

    // Appends v's per-iteration values (see iterationValues()) onto out —
    // used by `each` in both a plain list literal and a list-comprehension
    // body.
    void flattenAppend(std::vector<Value>& out, const Value& v) const;

    // Evaluates one list-comprehension body clause (see ListCompBody in
    // Expr.h), appending whatever it contributes onto out (subject to
    // m_listCompBudget). Recursive since if/else/each clauses can nest.
    void collectListCompBody(const ListCompBody& body, std::vector<Value>& out);
};

} // namespace chisel::lang
