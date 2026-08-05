#pragma once
#include "AST.h"
#include "Expr.h"
#include "Value.h"
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

    // Register a single function definition (non-owning pointer — def must
    // outlive this interpreter instance) — used for a *local* function
    // definition nested inside a module body (see LocalFunctionDefStmt in
    // AST.h), as opposed to loadFunctions()'s file-scope batch.
    void registerFunction(const FunctionDef& def) { m_funcDefs[def.name] = &def; }

    // Fine-grained lookup/restore for a single function-def entry, paired
    // with registerFunction() above: CsgEvaluator saves lookupFunctionDef's
    // result for just the handful of names a module body locally redefines
    // (not a full-map copy, since only those specific names are ever
    // touched), then calls setFunctionDef() to put each one back once the
    // module call returns.
    const FunctionDef* lookupFunctionDef(const std::string& name) const {
        auto it = m_funcDefs.find(name);
        return it != m_funcDefs.end() ? it->second : nullptr;
    }
    void setFunctionDef(const std::string& name, const FunctionDef* def) {
        if (def) m_funcDefs[name] = def;
        else     m_funcDefs.erase(name);
    }

    // Evaluate an expression to a Value.
    Value evaluate(const ExprNode& expr);

    // Set once a recursion guard (kMaxCallDepth or evalFunctionBody's
    // kMaxTailHops) trips — matches real OpenSCAD's fatal "Recursion
    // detected calling function 'X'" behavior (verified against a live
    // OpenSCAD 2021.01 binary and openscad/openscad's own
    // FunctionCall::evaluate), as opposed to every *other* Interpreter
    // failure mode, which degrades gracefully to undef and keeps going.
    // Once set, evaluate() itself short-circuits to undef immediately (see
    // its first line) rather than doing any further work.
    //
    // Interpreter has no notion of "abort the whole script" on its own —
    // that's CsgEvaluator's m_aborted, already used for a failed top-level
    // assert() — so the caller driving evaluation (CsgEvaluator) is
    // expected to check this after every expression it evaluates directly
    // and fold it into that same mechanism. See CsgEvaluator::
    // checkRecursionAbort().
    bool recursionAborted() const { return m_recursionAborted; }
    const std::string& recursionAbortedFunctionName() const { return m_recursionAbortedFnName; }
    SourceLoc recursionAbortedLoc() const { return m_recursionAbortedLoc; }

    // One entry of the call chain leading to a recursion-detected abort —
    // see m_callStack and recursionAbortedStack() below.
    struct CallFrame {
        std::string name;
        SourceLoc   loc;
    };

    // The call chain *above* the abort site, innermost first, captured at
    // the moment recursionAborted() was set — used to build OpenSCAD's
    // "TRACE: called by 'X' in file F, line L" lines (one per frame; the
    // abort site itself, matching recursionAbortedFunctionName()/
    // recursionAbortedLoc(), is the implicit first TRACE line and isn't
    // repeated in this list — see CsgEvaluator::checkRecursionAbort()).
    const std::vector<CallFrame>& recursionAbortedStack() const { return m_recursionAbortedStack; }

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

    // Begin a fresh call scope for a *named* module/function call (as
    // opposed to a closure call, which already gets this right via its own
    // captured ClosureEnv — see callClosure). Matches OpenSCAD: a callee
    // never sees the caller's ordinary local variables (module/function
    // parameters, let-bindings, etc.) — only its own parameters, top-level
    // globals, and $-prefixed special variables, which are dynamically
    // scoped and do inherit down the whole call chain (verified against a
    // live OpenSCAD 2021.01 binary via variable-scope-tests.scad: a plain
    // variable set in one module is undef in a module it calls, but a $fn
    // override passed at the outermost call is still visible many calls
    // deep). Swaps m_env to (global env) + (current $-vars) and returns the
    // caller's full env for restoreEnv() to reinstate afterward.
    //
    // Caveat: "global env" here means the env as of loadAssignments()'s
    // return, so this is exact for top-level module/function definitions
    // (the common case) but only an approximation for a definition nested
    // inside another module's body (see the new LocalModuleDefStmt/
    // LocalFunctionDefStmt in AST.h) — such a nested definition's true
    // lexical parent is its enclosing call's scope, not the file's global
    // scope, which this doesn't currently reconstruct.
    std::unordered_map<std::string, Value> beginCallScope();

    // Same env swap as beginCallScope(), for the tail-call trampoline in
    // evalFunctionBody(): each hop discards the previous hop's scope rather
    // than restoring it (only the *outermost* call's pre-call env, saved
    // once by beginCallScope() at the trampoline's entry point, ever gets
    // restored — see evalFunctionBody()), so there's no caller env worth
    // paying to copy and return here.
    void beginTailCallScope();

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
    // Snapshot of m_env taken at the end of loadAssignments() — the file's
    // top-level variables, before any call has added its own locals on top.
    // Backs beginCallScope()'s "globals are visible, callers' locals aren't"
    // rule.
    std::unordered_map<std::string, Value>             m_globalEnv;
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
    //
    // Re-measured for issue #83 (previous cap: 200) by driving unguarded
    // recursion (via a scratch copy of this file with kMaxCallDepth raised
    // to a no-op-large value) inside a pthread with an explicit 1 MiB stack,
    // GCC 13 -O3 -DNDEBUG, catching the resulting SIGSEGV to binary-search
    // the deepest surviving call. Two shapes were measured since per-call
    // frame cost varies a lot by body: a bare numeric recursion
    // (`f(n) = n<=0 ? 0 : f(n-1)`) survived to ~1180, while the heavier,
    // more representative "recursive list-building" idiom this issue calls
    // out (`f(n) = n<=0 ? [] : concat([n], f(n-1))`) only survived to ~680.
    // 300 keeps a >2x margin under that worse-case measurement — deeper
    // than the old 200, without eating into the safety margin the original
    // conservative choice was protecting — while leaving headroom for
    // MSVC's likely-larger per-frame footprint, which this pass could not
    // measure directly (no Windows toolchain available).
    //
    // This only needs to cover genuinely *non-tail* recursion now — see
    // evalFunctionBody()/kMaxTailHops below for tail calls, which OpenSCAD's
    // own tail-recursion-tests.scad (fetched directly from openscad/openscad
    // to confirm, since it isn't in this repo) needs at depths from 2,000 to
    // 50,000 that no native-stack cap could satisfy. Checked against that
    // same file: its one genuinely non-tail-recursive case (`f3a`, `a + f3a
    // (a - 1)` — the recursive call is an operand of `+`, not the whole tail
    // expression) only goes to depth 100, well inside this cap either way.
    static constexpr int kMaxCallDepth = 300;
    int m_callDepth = 0;

    // Bounds evalFunctionBody()'s tail-call trampoline — see its own comment
    // for why a tail hop needs a completely different (much larger) budget
    // than kMaxCallDepth above: each hop is a plain loop iteration, not a
    // new native stack frame, so it costs no stack regardless of how large
    // this is. Matches real OpenSCAD's own tail-call iteration cap exactly
    // (`recursion_depth`'s 1,000,000 in FunctionCall::evaluate, upstream
    // src/core/Expression.cc) rather than picking an arbitrary round number
    // — confirmed empirically fast enough in practice (a few hundred ms) to
    // still terminate an unconditionally-recursive tail call like
    // `function f() = f();` promptly.
    static constexpr int kMaxTailHops = 1'000'000;

    // Backing state for recursionAborted()/recursionAbortedFunctionName()/
    // recursionAbortedLoc() above — set at the two recursion-guard trip
    // sites (kMaxCallDepth in evaluate()'s FunctionCall case and callClosure(),
    // kMaxTailHops in evalFunctionBody()), never cleared automatically since
    // an Interpreter is constructed fresh per build/evaluation (see
    // HeadlessBuild.cpp/CsgEvaluator.cpp) rather than reused across scripts.
    bool        m_recursionAborted = false;
    std::string m_recursionAbortedFnName;
    SourceLoc   m_recursionAbortedLoc;
    std::vector<CallFrame> m_recursionAbortedStack;

    // Active named-call frames, outermost first (m_callStack.back() is the
    // innermost/most-recently-entered call) — pushed just before a named
    // function/closure call's body starts evaluating and popped right after
    // it returns, in evaluate()'s FunctionCall case and in callClosure().
    // Copied into m_recursionAbortedStack (innermost first, i.e. reversed)
    // at each of the three recursion-abort trip sites: as a whole, for the
    // ordinary evaluate()/callClosure() kMaxCallDepth checks (nothing's been
    // pushed yet for the call that's about to be refused); with its own
    // back() dropped, for evalFunctionBody()'s kMaxTailHops check (that
    // back() *is* the frame currently tail-hopping — already represented by
    // recursionAbortedFnName()/recursionAbortedLoc() — so including it too
    // would duplicate the trace's first line).
    std::vector<CallFrame> m_callStack;

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
    // params against orderedArgs the same way a named FunctionDef call does
    // (see bindOrderedArgs), but starting from the closure's *captured*
    // environment rather than the caller's — a function literal sees the
    // scope where `function(...) ...` was written, not the scope it's
    // called from. Takes fnVal by value, not by reference: the caller's
    // reference is typically an element inside m_env (e.g. a FunctionCall
    // looks the callee up via m_env.find), and this function reassigns
    // m_env wholesale as its first step (to switch to the closure's own
    // captured scope) — a reference into the old map would dangle the
    // moment that happens.
    //
    // callLoc is the *call* expression's own location (FunctionCall::loc or
    // CallExpr::loc at each of this function's two call sites) — deliberately
    // not fnVal.closure->def->loc (the closure literal's definition site):
    // a recursion abort's TRACE line for this frame must name where this
    // particular call was textually made, the same as the sibling
    // FunctionDef call path a few lines up in evaluate() (which pushes its
    // own node.loc, not the callee's def->loc either), not the one fixed
    // location every call of the same closure would otherwise share.
    Value callClosure(Value fnVal, const std::vector<std::pair<std::string, Value>>& orderedArgs,
                       const SourceLoc& callLoc);

    // Evaluates a *named* user function's body, trampolining through any
    // call in tail position instead of recursing — the fix for issue #83's
    // real gap: OpenSCAD's own tail-recursion-tests.scad expects tail-
    // recursive functions to reach depths (2,000-50,000) no reasonable
    // native-stack cap could ever survive, because real OpenSCAD doesn't
    // recurse for these either (see FunctionCall::evaluate's trampoline loop
    // in openscad/openscad's src/core/Expression.cc, confirmed by reading
    // it directly — this mirrors that design, not a guess).
    //
    // "Tail position" here means: the entire value of the function body,
    // reachable by unwrapping only the chosen branch of a ternary and the
    // body of a let (ChiselCAD's grammar has no assert()/echo()-as-
    // expression forms to unwrap, unlike upstream — a tail call wrapped in
    // one of those, e.g. tail-recursion-tests.scad's ftail_mixed, still
    // falls back to ordinary recursion, or fails to parse at all if
    // ChiselCAD doesn't support that form yet). Once unwrapping bottoms out
    // at a FunctionCall naming another (or the same) m_funcDefs entry, that
    // call is resolved and looped into directly: no new evaluate() stack
    // frame, so m_callDepth is never touched by a tail hop — only
    // kMaxTailHops bounds an unconditionally-recursive tail call (e.g.
    // `function f() = f();`) from looping forever. Anything else — a
    // variable bound to a function-literal value (which takes priority per
    // FunctionCall's own evaluate() case), a builtin, or any non-Ternary/
    // Let/FunctionCall expression shape — isn't tail-simplifiable, so the
    // loop stops and defers to one ordinary (real, kMaxCallDepth-guarded)
    // evaluate() call, matching this function's pre-trampoline behavior
    // exactly for every case that isn't a bare tail call.
    Value evalFunctionBody(const ExprNode& body);

    // Binds `params` in the current environment by replaying orderedArgs
    // (pairs of arg-name/value in original call-site textual order; an
    // empty name means positional) in that order — a positional arg
    // advances an unconditional left-to-right counter that does NOT skip
    // slots already targeted by a named arg, and a named arg binds directly
    // by name, so whichever (named or positional) comes later in the call
    // wins for that slot. This mirrors OpenSCAD's actual argument-binding
    // rule (confirmed against OpenSCAD's own arg-permutations.scad test).
    // A named arg that matches neither a declared parameter nor a
    // $-prefixed special variable is discarded, not bound — an unmatched
    // ordinary named arg must NOT leak into the callee's local scope, since
    // that scope is just the caller's env snapshot-and-restore, and setting
    // it there would shadow an unrelated same-named variable from the
    // enclosing scope for the duration of the call (e.g. `x = 100;
    // function f(a) = x; f(a=1, x=5)` must still see the outer `x = 100`,
    // not silently pick up `x=5`). $-prefixed names are the one deliberate
    // exception: those are genuinely dynamically-scoped special-variable
    // overrides, valid on any call regardless of whether the callee
    // declares a same-named parameter. Params untouched by any arg fall
    // back to their default expression, or undef.
    template <typename Param>
    void bindOrderedArgs(const std::vector<Param>& params,
                          const std::vector<std::pair<std::string, Value>>& orderedArgs) {
        auto isDeclaredParam = [&](const std::string& name) {
            for (const auto& p : params)
                if (p.name == name) return true;
            return false;
        };
        std::size_t posIdx = 0;
        std::unordered_set<std::string> namedBound;
        for (const auto& [name, value] : orderedArgs) {
            if (name.empty()) {
                if (posIdx < params.size())
                    setVar(params[posIdx].name, value);
                ++posIdx;
            } else if (name[0] == '$') {
                setVar(name, value);
            } else if (isDeclaredParam(name)) {
                setVar(name, value);
                namedBound.insert(name);
            }
            // else: unmatched ordinary named arg — discarded (see comment above).
        }
        for (std::size_t i = 0; i < params.size(); ++i) {
            const auto& param = params[i];
            bool bound = (i < posIdx) || namedBound.count(param.name) != 0;
            if (!bound) {
                if (param.defaultVal)
                    setVar(param.name, evaluate(*param.defaultVal));
                else
                    setVar(param.name, Value::undef());
            }
        }
    }

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
