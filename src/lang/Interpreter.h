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

    // Binds `name` in the current scope to the evaluated result of valueExpr
    // — used everywhere a real assignment happens (global `x = expr;`, local
    // block assignments, `let(x = expr, ...)` bindings), as opposed to
    // setVar's other callers (module/function parameter binding, for-loop
    // variables) which must NOT get the special case below. When valueExpr
    // is directly a function literal (`f = function(n) ... f(n-1) ...;`),
    // this also records the closure's own name (ClosureEnv::selfName) so
    // callClosure can bind it for the literal's body to call itself by that
    // name — mirroring OpenSCAD's function-literal recursion. Restricting
    // this to a literal directly on the right-hand side (checked via the AST
    // node type, not the runtime Value) keeps it safe: the closure was just
    // constructed by this same evaluate() call, so nothing else can be
    // aliasing it yet. A plain copy (`g = f;`) does not re-trigger this —
    // renaming a closure that's already shared with other bindings would
    // rebind `name` in their calls too. Note this stores only a name string,
    // not a Value referencing the closure itself: the latter would be a
    // shared_ptr<ClosureEnv> cycle back into its own vars map, which
    // shared_ptr can never collect.
    void assignVar(const std::string& name, const ExprNode& valueExpr);

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

    // Sets $vpr/$vpt/$vpd/$vpf (viewport rotation/translation/distance/
    // field-of-view), backing those special variables' VarRef fallback
    // below. Left at their OpenSCAD-matching defaults (see member
    // initializers) unless the caller has real render-camera state to plumb
    // in (MeshBuilder does this, deriving vpr from Camera's yaw/pitch — see
    // MeshBuilder::buildOne); headless evaluation (tests, CLI use) just
    // keeps the defaults.
    void setViewport(double vprX, double vprY, double vprZ,
                     double vptX, double vptY, double vptZ,
                     double vpd, double vpf) {
        m_vpr = {vprX, vprY, vprZ};
        m_vpt = {vptX, vptY, vptZ};
        m_vpd = vpd;
        m_vpf = vpf;
    }

private:
    std::unordered_map<std::string, Value>             m_env;
    std::unordered_map<std::string, const FunctionDef*> m_funcDefs;
    std::vector<std::string>                           m_moduleNameStack;

    // $vpr/$vpt/$vpd defaults match OpenSCAD's own defaults for a file with
    // no camera() statement and no GUI camera attached, so headless
    // evaluation reads sane numbers rather than undef.
    std::array<double, 3> m_vpr{55.0, 0.0, 25.0};
    std::array<double, 3> m_vpt{0.0, 0.0, 0.0};
    double                m_vpd = 140.0;
    double                m_vpf = 22.5;

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

    // Invokes a closure Value (see Value::Tag::Function): binds fnVal's
    // params against posArgs/namedArgs the same way a named FunctionDef call
    // does, but starting from the closure's *captured* environment rather
    // than the caller's — a function literal sees the scope where
    // `function(...) ...` was written, not the scope it's called from.
    // Takes fnVal by value, not by reference: the caller's reference is
    // typically an element inside m_env (e.g. a FunctionCall looks the
    // callee up via m_env.find), and this function reassigns m_env wholesale
    // as its first step (to switch to the closure's own captured scope) —
    // a reference into the old map would dangle the moment that happens.
    Value callClosure(Value fnVal,
                       const std::vector<Value>& posArgs,
                       const std::vector<std::pair<std::string, Value>>& namedArgs);

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
