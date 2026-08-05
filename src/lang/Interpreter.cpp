#include "Interpreter.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>

static constexpr double kPi      = 3.14159265358979323846;
static constexpr double kDeg2Rad = kPi / 180.0;
static constexpr double kRad2Deg = 180.0 / kPi;

namespace chisel::lang {

// ---------------------------------------------------------------------------
// UTF-8 helpers backing len()/indexing/chr()/ord() on strings — OpenSCAD
// counts and indexes strings by Unicode codepoint, not by byte, so a
// std::string's own .size()/operator[] (byte-oriented) can't be used
// directly for those builtins. Malformed/truncated sequences decode as a
// single replacement codepoint (U+FFFD) and advance one byte, matching the
// same graceful-degradation choice made for text() shaping in
// NaiveLtrShaper.cpp (duplicated here rather than shared: that helper lives
// in src/import, which Interpreter has no dependency on).
// ---------------------------------------------------------------------------
namespace {

uint32_t decodeUtf8At(const std::string& s, std::size_t& i) {
    unsigned char b0 = static_cast<unsigned char>(s[i]);
    auto continuation = [&](std::size_t idx) {
        return idx < s.size() && (static_cast<unsigned char>(s[idx]) & 0xC0) == 0x80;
    };

    if (b0 < 0x80) {
        ++i;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && continuation(i + 1)) {
        uint32_t cp =
            (static_cast<uint32_t>(b0 & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        i += 2;
        return cp;
    }
    if ((b0 & 0xF0) == 0xE0 && continuation(i + 1) && continuation(i + 2)) {
        uint32_t cp = (static_cast<uint32_t>(b0 & 0x0F) << 12) |
                      (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        i += 3;
        return cp;
    }
    if ((b0 & 0xF8) == 0xF0 && continuation(i + 1) && continuation(i + 2) && continuation(i + 3)) {
        uint32_t cp = (static_cast<uint32_t>(b0 & 0x07) << 18) |
                      (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
                      (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        i += 4;
        return cp;
    }
    ++i;
    return 0xFFFD;
}

// Encodes a single codepoint back to its UTF-8 byte sequence.
std::string encodeUtf8(uint32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

// Number of Unicode codepoints in a UTF-8 string (OpenSCAD's len()/string
// indexing unit), not the byte count std::string::size() would give.
std::size_t utf8Length(const std::string& s) {
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        decodeUtf8At(s, i);
        ++count;
    }
    return count;
}

// Returns the codepointIdx-th character (as its own UTF-8-encoded
// substring), or an empty string if out of range.
std::string utf8CharAt(const std::string& s, std::size_t codepointIdx) {
    std::size_t i = 0;
    std::size_t idx = 0;
    while (i < s.size()) {
        std::size_t start = i;
        decodeUtf8At(s, i);
        if (idx == codepointIdx) return s.substr(start, i - start);
        ++idx;
    }
    return {};
}

} // namespace

// Textual representation used by str() for any argument that isn't itself a
// top-level string (str()'s caller special-cases that: a bare string arg is
// appended verbatim/unquoted, but a string nested inside a vector — handled
// here — is quoted, matching OpenSCAD: str("hi") == "hi" but
// str([1,"hi"]) == "[1, \"hi\"]"). Also backs echo()/assert() message
// formatting via CsgEvaluator::formatValue, which mirrors this same shape at
// the CSG-evaluator layer (duplicated rather than shared because Interpreter
// has no dependency on CsgEvaluator).
static std::string formatValueRec(const Value& v) {
    if (v.isNumber()) {
        double n = v.asNumber();
        if (std::isnan(n)) return "nan";
        if (std::isinf(n)) return n < 0 ? "-inf" : "inf";
        char buf[32];
        constexpr double kMaxExactInt = 9.007199254740992e15; // 2^53
        if (std::abs(n) < kMaxExactInt && n == static_cast<long long>(n))
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
        else
            std::snprintf(buf, sizeof(buf), "%g", n);
        return buf;
    }
    if (v.isBool())   return v.asBool() ? "true" : "false";
    if (v.isString()) return "\"" + v.asString() + "\"";
    if (v.isVector()) {
        std::string s = "[";
        for (std::size_t i = 0; i < v.asVec().size(); ++i) {
            if (i > 0) s += ", ";
            s += formatValueRec(v.asVec()[i]);
        }
        return s + "]";
    }
    if (v.isRange())
        return "[" + formatValueRec(Value::fromNumber(v.rangeStart)) + " : " +
               formatValueRec(Value::fromNumber(v.rangeStep)) + " : " +
               formatValueRec(Value::fromNumber(v.rangeEnd)) + "]";
    if (v.isFunction()) {
        std::string s = "function(";
        if (v.closure && v.closure->def) {
            const auto& params = v.closure->def->params;
            for (std::size_t i = 0; i < params.size(); ++i) {
                if (i > 0) s += ", ";
                s += params[i].name;
            }
        }
        return s + ")";
    }
    return "undef";
}

// Structural equality used by the `==`/`!=` fallback for non-numeric,
// non-bool types (string content, and element-wise vector comparison).
static bool valEqRec(const Value& a, const Value& b) {
    if (a.tag != b.tag) return false;
    if (a.isNumber()) return a.asNumber() == b.asNumber();
    if (a.isBool())   return a.asBool()   == b.asBool();
    if (a.isString()) return a.asString() == b.asString();
    if (a.isVector()) {
        const auto& av = a.asVec();
        const auto& bv = b.asVec();
        if (av.size() != bv.size()) return false;
        for (std::size_t i = 0; i < av.size(); ++i)
            if (!valEqRec(av[i], bv[i])) return false;
        return true;
    }
    if (a.isRange())
        return a.rangeStart == b.rangeStart && a.rangeStep == b.rangeStep && a.rangeEnd == b.rangeEnd;
    if (a.isFunction())
        return a.closure == b.closure; // identity: same closure instance
    return a.isUndef(); // both Undef, since tags already matched
}

// ---------------------------------------------------------------------------
// Environment loading
// ---------------------------------------------------------------------------
void Interpreter::loadAssignments(const ParseResult& result) {
    // Top-level redefinition is *not* plain sequential overwrite: a
    // variable reassigned more than once keeps its last RHS expression, but
    // that expression is evaluated "in place of" the first occurrence, not
    // where it's textually written — so it can't see any other variable's
    // own reassignment that comes later in the file, even though that
    // reassignment is textually between the first and last occurrence.
    // Verified against a live OpenSCAD 2021.01 binary via
    // value-reassignment-tests.scad/value-reassignment-tests2.scad:
    //   myval=2; i=2;      myval=i*2;  // => myval=undef (i isn't visible
    //                                  //    yet at myval's first-occurrence
    //                                  //    position), i=2
    //   myval=2; i=myval;  myval=3;    // => myval=3, i=3 (myval's *last*
    //                                  //    RHS "3" replaces "2" in place,
    //                                  //    so i=myval sees 3)
    // Implemented as: reduce to one (name, last RHS) pair per name, keeping
    // each name's first-occurrence position, then evaluate that reduced
    // list in position order.
    std::vector<const AssignStmt*> reduced;
    std::unordered_map<std::string, std::size_t> indexOf;
    for (const auto& stmt : result.assignments) {
        auto it = indexOf.find(stmt.name);
        if (it != indexOf.end())
            reduced[it->second] = &stmt;
        else {
            indexOf[stmt.name] = reduced.size();
            reduced.push_back(&stmt);
        }
    }
    for (const auto* stmt : reduced)
        assignVar(stmt->name, *stmt->value);
    m_globalEnv = m_env;
}

void Interpreter::loadFunctions(const ParseResult& result) {
    for (const auto& def : result.functionDefs)
        m_funcDefs[def.name] = &def;
}

std::unordered_map<std::string, Value> Interpreter::beginCallScope() {
    auto saved = m_env;
    std::unordered_map<std::string, Value> fresh = m_globalEnv;
    for (const auto& [k, v] : m_env)
        if (!k.empty() && k[0] == '$') fresh[k] = v; // dynamic scoping: most recent caller's $-value wins
    m_env = std::move(fresh);
    return saved;
}

void Interpreter::beginTailCallScope() {
    std::unordered_map<std::string, Value> fresh = m_globalEnv;
    for (const auto& [k, v] : m_env)
        if (!k.empty() && k[0] == '$') fresh[k] = v;
    m_env = std::move(fresh);
}

Value Interpreter::evalFunctionBody(const ExprNode& startBody) {
    const ExprNode* body = &startBody;

    for (int hop = 0; ; ++hop) {
        if (auto* t = std::get_if<TernaryExpr>(body)) {
            body = (bool(evaluate(*t->condition)) ? t->then : t->else_).get();
            continue;
        }
        if (auto* let = std::get_if<LetExpr>(body)) {
            for (const auto& [name, valExpr] : let->bindings)
                assignVar(name, *valExpr);
            body = let->body.get();
            continue;
        }
        if (auto* call = std::get_if<FunctionCall>(body)) {
            // A variable bound to a function-literal value takes priority
            // over a same-named `function` def (matches the ordinary
            // FunctionCall case in evaluate()) and isn't tail-hoppable here
            // — deferred to the terminal evaluate() call below instead.
            auto varIt = m_env.find(call->name);
            bool isClosureVar = varIt != m_env.end() && varIt->second.isFunction();
            auto fit = isClosureVar ? m_funcDefs.end() : m_funcDefs.find(call->name);
            if (fit == m_funcDefs.end()) break;

            if (hop >= kMaxTailHops) {
                m_recursionAborted    = true;
                m_recursionAbortedFnName = call->name;
                m_recursionAbortedLoc    = call->loc;
                // m_callStack.back() is this same tail-hopping call, already
                // captured above — see m_callStack's own comment.
                m_recursionAbortedStack.assign(m_callStack.rbegin() + 1, m_callStack.rend());
                return Value::undef();
            }

            std::vector<std::pair<std::string, Value>> orderedArgs;
            orderedArgs.reserve(call->args.size());
            for (const auto& arg : call->args)
                orderedArgs.push_back({arg.name, evaluate(*arg.value)});

            const FunctionDef& def = *fit->second;
            beginTailCallScope();
            bindOrderedArgs(def.params, orderedArgs);
            body = def.body.get();
            continue;
        }
        break;
    }

    return evaluate(*body);
}

// ---------------------------------------------------------------------------
// evaluate — dispatch on ExprNode variant
// ---------------------------------------------------------------------------
Value Interpreter::evaluate(const ExprNode& expr) {
    if (m_recursionAborted) return Value::undef();
    return std::visit([&](const auto& node) -> Value {
        using T = std::decay_t<decltype(node)>;

        // ---- Literals ----
        if constexpr (std::is_same_v<T, NumberLit>) {
            return Value::fromNumber(node.value);
        }
        else if constexpr (std::is_same_v<T, BoolLit>) {
            return Value::fromBool(node.value);
        }
        else if constexpr (std::is_same_v<T, UndefLit>) {
            return Value::undef();
        }
        else if constexpr (std::is_same_v<T, StringLit>) {
            return Value::fromString(node.value);
        }
        else if constexpr (std::is_same_v<T, VectorLit>) {
            std::vector<Value> elems;
            elems.reserve(node.elements.size());
            for (const auto& e : node.elements) {
                Value v = evaluate(*e.value);
                if (e.isEach) flattenAppend(elems, v);
                else          elems.push_back(std::move(v));
            }
            return Value::fromVec(std::move(elems));
        }

        // ---- Variable reference ----
        else if constexpr (std::is_same_v<T, VarRef>) {
            auto it = m_env.find(node.name);
            if (it != m_env.end()) return it->second;
            // Predefined constants/special vars — checked only once the
            // environment lookup misses, so a user assignment of the same
            // name (e.g. `PI = 3;`) still wins, matching OpenSCAD (these
            // aren't protected keywords, just default values).
            if (node.name == "PI")       return Value::fromNumber(kPi);
            if (node.name == "$preview") return Value::fromBool(true); // no separate export-render pass exists yet
            if (node.name == "$t")       return Value::fromNumber(0.0); // no animation/scrubbing yet (see roadmap v4)
            if (node.name == "$parent_modules")
                // The number of modules in the instantiation stack,
                // including the currently-running one (verified against a
                // live OpenSCAD 2021.01 binary via parent_module-tests.scad:
                // depth 2 inside a module called directly from another
                // module, not 1).
                return Value::fromNumber(static_cast<double>(m_moduleNameStack.size()));
            if (node.name == "$vpr")
                return Value::fromVec({Value::fromNumber(m_vpr[0]), Value::fromNumber(m_vpr[1]),
                                        Value::fromNumber(m_vpr[2])});
            if (node.name == "$vpt")
                return Value::fromVec({Value::fromNumber(m_vpt[0]), Value::fromNumber(m_vpt[1]),
                                        Value::fromNumber(m_vpt[2])});
            if (node.name == "$vpd")
                return Value::fromNumber(m_vpd);
            if (node.name == "$vpf")
                return Value::fromNumber(m_vpf);
            return Value::undef();
        }

        // ---- Binary expression ----
        else if constexpr (std::is_same_v<T, BinaryExpr>) {
            // && / || short-circuit: the right operand must not be evaluated
            // when the left operand already determines the result (this is
            // the only guard available in function bodies, which have no
            // `if` statement, so recursive functions rely on it to terminate).
            if (node.op == BinaryExpr::Op::And || node.op == BinaryExpr::Op::Or) {
                Value lv = evaluate(*node.left);
                bool lb = bool(lv);
                if (node.op == BinaryExpr::Op::And && !lb) return Value::fromBool(false);
                if (node.op == BinaryExpr::Op::Or  &&  lb) return Value::fromBool(true);
                return Value::fromBool(bool(evaluate(*node.right)));
            }

            Value lv = evaluate(*node.left);
            Value rv = evaluate(*node.right);

            // Arithmetic (number × number)
            if (lv.isNumber() && rv.isNumber()) {
                double l = lv.asNumber(), r = rv.asNumber();
                switch (node.op) {
                case BinaryExpr::Op::Add: return Value::fromNumber(l + r);
                case BinaryExpr::Op::Sub: return Value::fromNumber(l - r);
                case BinaryExpr::Op::Mul: return Value::fromNumber(l * r);
                // IEEE 754 division/fmod already produce the right result for
                // r == 0 (+/-inf or nan) and non-finite operands — matching
                // real OpenSCAD, which lets these propagate as nan/inf rather
                // than collapsing them to undef.
                case BinaryExpr::Op::Div: return Value::fromNumber(l / r);
                case BinaryExpr::Op::Mod: return Value::fromNumber(std::fmod(l, r));
                case BinaryExpr::Op::Eq:  return Value::fromBool(l == r);
                case BinaryExpr::Op::Ne:  return Value::fromBool(l != r);
                case BinaryExpr::Op::Lt:  return Value::fromBool(l <  r);
                case BinaryExpr::Op::Le:  return Value::fromBool(l <= r);
                case BinaryExpr::Op::Gt:  return Value::fromBool(l >  r);
                case BinaryExpr::Op::Ge:  return Value::fromBool(l >= r);
                case BinaryExpr::Op::And:
                case BinaryExpr::Op::Or:  break; // handled by the short-circuit branch above
                }
            }

            // Vector + vector component-wise
            if (lv.isVector() && rv.isVector() &&
                (node.op == BinaryExpr::Op::Add || node.op == BinaryExpr::Op::Sub)) {
                const auto& lv_ = lv.asVec();
                const auto& rv_ = rv.asVec();
                if (lv_.size() != rv_.size()) return Value::undef();
                std::vector<Value> out;
                out.reserve(lv_.size());
                for (std::size_t i = 0; i < lv_.size(); ++i) {
                    if (lv_[i].isNumber() && rv_[i].isNumber()) {
                        double l = lv_[i].asNumber(), r = rv_[i].asNumber();
                        out.push_back(Value::fromNumber(
                            node.op == BinaryExpr::Op::Add ? l + r : l - r));
                    } else {
                        out.push_back(Value::undef());
                    }
                }
                return Value::fromVec(std::move(out));
            }

            // Logical / mixed-type fallback (And/Or already handled above)
            switch (node.op) {
            case BinaryExpr::Op::Eq:  return Value::fromBool(valEqRec(lv, rv));
            case BinaryExpr::Op::Ne:  return Value::fromBool(!valEqRec(lv, rv));
            default: break;
            }
            return Value::undef();
        }

        // ---- Unary expression ----
        else if constexpr (std::is_same_v<T, UnaryExpr>) {
            Value v = evaluate(*node.operand);
            switch (node.op) {
            case UnaryExpr::Op::Neg:
                if (v.isNumber()) return Value::fromNumber(-v.asNumber());
                if (v.isVector()) {
                    std::vector<Value> out;
                    out.reserve(v.asVec().size());
                    for (const auto& e : v.asVec())
                        out.push_back(e.isNumber() ? Value::fromNumber(-e.asNumber()) : Value::undef());
                    return Value::fromVec(std::move(out));
                }
                return Value::undef();
            case UnaryExpr::Op::Not:
                return Value::fromBool(!bool(v));
            }
        }

        // ---- Ternary expression ----
        else if constexpr (std::is_same_v<T, TernaryExpr>) {
            return bool(evaluate(*node.condition))
                   ? evaluate(*node.then)
                   : evaluate(*node.else_);
        }

        // ---- Index expression: target[index] ----
        else if constexpr (std::is_same_v<T, IndexExpr>) {
            Value target = evaluate(*node.target);
            Value idx    = evaluate(*node.index);
            if (!idx.isNumber()) return Value::undef();
            double idxNum = idx.asNumber();
            if (!std::isfinite(idxNum) || idxNum < 0 ||
                idxNum > static_cast<double>(std::numeric_limits<int>::max()))
                return Value::undef();
            int i = static_cast<int>(idxNum);
            if (target.isVector()) {
                if (static_cast<std::size_t>(i) >= target.asVec().size())
                    return Value::undef();
                return target.asVec()[static_cast<std::size_t>(i)];
            }
            if (target.isString()) {
                std::string ch = utf8CharAt(target.asString(), static_cast<std::size_t>(i));
                if (ch.empty()) return Value::undef();
                return Value::fromString(std::move(ch));
            }
            if (target.isRange()) {
                auto vals = expandRange(target.rangeStart, target.rangeStep, target.rangeEnd);
                if (static_cast<std::size_t>(i) >= vals.size()) return Value::undef();
                return vals[static_cast<std::size_t>(i)];
            }
            return Value::undef();
        }

        // ---- Member expression: target.member (vector swizzling, range
        // .begin/.step/.end) ----
        else if constexpr (std::is_same_v<T, MemberExpr>) {
            Value target = evaluate(*node.target);
            if (target.isRange()) {
                if (node.member == "begin") return Value::fromNumber(target.rangeStart);
                if (node.member == "step")  return Value::fromNumber(target.rangeStep);
                if (node.member == "end")   return Value::fromNumber(target.rangeEnd);
                return Value::undef();
            }
            if (target.isVector()) {
                // x/y/z/w and rgba are two names for the same four slots —
                // matches OpenSCAD's vector-swizzling grammar.
                auto slotOf = [](char c) -> int {
                    switch (c) {
                    case 'x': case 'r': return 0;
                    case 'y': case 'g': return 1;
                    case 'z': case 'b': return 2;
                    case 'w': case 'a': return 3;
                    default:            return -1;
                    }
                };
                const auto& vec = target.asVec();
                if (node.member.size() == 1) {
                    int slot = slotOf(node.member[0]);
                    if (slot < 0 || static_cast<std::size_t>(slot) >= vec.size())
                        return Value::undef();
                    return vec[static_cast<std::size_t>(slot)];
                }
                // Multi-letter swizzle (.xyzw, .rgba, .xr, .rrrr, ...): one
                // output element per member letter, in the order written.
                std::vector<Value> out;
                out.reserve(node.member.size());
                for (char c : node.member) {
                    int slot = slotOf(c);
                    if (slot < 0 || static_cast<std::size_t>(slot) >= vec.size())
                        return Value::undef();
                    out.push_back(vec[static_cast<std::size_t>(slot)]);
                }
                return Value::fromVec(std::move(out));
            }
            return Value::undef();
        }

        // ---- Let expression: let(x = expr, ...) body ----
        else if constexpr (std::is_same_v<T, LetExpr>) {
            auto savedEnv = snapshotEnv();
            for (const auto& [name, valExpr] : node.bindings)
                assignVar(name, *valExpr);
            Value result = evaluate(*node.body);
            restoreEnv(std::move(savedEnv));
            return result;
        }

        // ---- Range literal: [start:end] / [start:step:end] ----
        else if constexpr (std::is_same_v<T, RangeLit>) {
            double start = evalNumber(*node.start);
            double step  = node.step ? evalNumber(*node.step) : 1.0;
            double end   = evalNumber(*node.end);
            return Value::fromRange(start, step, end);
        }

        // ---- List comprehension: [for (var = source) body] ----
        else if constexpr (std::is_same_v<T, ListCompExpr>) {
            Value sourceVal = evaluate(*node.source);
            auto  values    = iterationValues(sourceVal);

            // Only the outermost comprehension in a (possibly nested) chain
            // resets the shared element budget — a nested one reuses
            // whatever the outer one has left, so the total across all
            // nesting levels of this expression stays bounded even though
            // each range's own kMaxRangeCount cap is per-range, not joint.
            const bool isOutermost = (m_listCompDepth == 0);
            if (isOutermost) m_listCompBudget = kMaxListCompElements;
            ++m_listCompDepth;

            auto savedEnv = snapshotEnv();
            std::vector<Value> out;
            for (const Value& v : values) {
                if (m_listCompBudget <= 0) break;
                setVar(node.var, v);
                collectListCompBody(*node.body, out);
            }
            restoreEnv(std::move(savedEnv));

            --m_listCompDepth;
            return Value::fromVec(std::move(out));
        }

        // ---- Function literal: function(params) expr ----
        else if constexpr (std::is_same_v<T, FunctionLit>) {
            auto env  = std::make_shared<ClosureEnv>();
            env->def  = &node;
            env->vars = m_env; // capture the defining scope by value
            return Value::fromClosure(std::move(env));
        }

        // ---- Function call ----
        else if constexpr (std::is_same_v<T, FunctionCall>) {
            // Evaluate args in their original call-site textual order,
            // preserving each one's name (empty = positional) — needed so
            // binding can replay OpenSCAD's actual left-to-right
            // interleaving rule (see bindOrderedArgs in Interpreter.h)
            // instead of treating "named" and "positional" as two separate
            // passes.
            std::vector<std::pair<std::string, Value>> orderedArgs;
            orderedArgs.reserve(node.args.size());
            for (const auto& arg : node.args)
                orderedArgs.push_back({arg.name, evaluate(*arg.value)});

            // A variable bound to a function-literal value takes priority
            // over a same-named `function` definition or builtin — mirrors
            // OpenSCAD: once `f = function(x) x*2;` exists, `f(3)` calls
            // that closure even if a builtin or `function f(...)` also
            // exists under that name.
            auto varIt = m_env.find(node.name);
            if (varIt != m_env.end() && varIt->second.isFunction())
                return callClosure(varIt->second, orderedArgs, node.loc);

            // Try user-defined function first
            auto fit = m_funcDefs.find(node.name);
            if (fit != m_funcDefs.end()) {
                if (m_callDepth >= kMaxCallDepth) {
                    m_recursionAborted       = true;
                    m_recursionAbortedFnName = node.name;
                    m_recursionAbortedLoc    = node.loc;
                    // Nothing's been pushed for this (refused) call yet, so
                    // the whole current stack is "outer" frames.
                    m_recursionAbortedStack.assign(m_callStack.rbegin(), m_callStack.rend());
                    return Value::undef();
                }

                const FunctionDef& def = *fit->second;
                auto savedEnv = beginCallScope();

                bindOrderedArgs(def.params, orderedArgs);

                ++m_callDepth;
                m_callStack.push_back({node.name, node.loc});
                Value result = evalFunctionBody(*def.body);
                m_callStack.pop_back();
                --m_callDepth;
                restoreEnv(std::move(savedEnv));
                return result;
            }

            // Fall back to built-ins (positional args in order, then named
            // args in order — builtins have no declared parameter list to
            // bind named args against by position).
            std::vector<Value> allArgs;
            allArgs.reserve(orderedArgs.size());
            for (auto& [n, v] : orderedArgs) if (n.empty())  allArgs.push_back(v);
            for (auto& [n, v] : orderedArgs) if (!n.empty()) allArgs.push_back(v);
            return callBuiltin(node.name, allArgs);
        }

        // ---- Call on an arbitrary expression's result (currying/IIFE) ----
        else if constexpr (std::is_same_v<T, CallExpr>) {
            Value callee = evaluate(*node.callee);

            // Args are always evaluated, even for a non-function callee —
            // matching FunctionCall above, which evaluates orderedArgs
            // unconditionally before any dispatch decision. Keeps a
            // side-effecting arg (assert()/echo() inside an expression)
            // consistent regardless of what's being called.
            std::vector<std::pair<std::string, Value>> orderedArgs;
            orderedArgs.reserve(node.args.size());
            for (const auto& arg : node.args)
                orderedArgs.push_back({arg.name, evaluate(*arg.value)});

            if (!callee.isFunction()) return Value::undef();
            return callClosure(std::move(callee), orderedArgs, node.loc);
        }

        return Value::undef();
    }, expr);
}

// ---------------------------------------------------------------------------
// evalNumber — evaluate and coerce to double, for consumers that feed the
// result straight into geometry (CsgLeaf/extrusion params, range bounds):
// arithmetic can legitimately produce nan/inf as a real Number now (see
// evaluate()'s BinaryExpr::Div/Mod and callBuiltin()'s math functions,
// which match OpenSCAD's own IEEE 754 propagation), but those aren't safe
// to hand to Manifold or a range expansion, so this boundary — unlike
// evaluate() itself — folds them down to 0.0 same as any other non-number.
// ---------------------------------------------------------------------------
double Interpreter::evalNumber(const ExprNode& expr) {
    Value v = evaluate(expr);
    if (v.isNumber()) return std::isfinite(v.asNumber()) ? v.asNumber() : 0.0;
    if (v.isBool())   return v.asBool() ? 1.0 : 0.0;
    return 0.0;
}

// ---------------------------------------------------------------------------
// evalVec3 — evaluate and return first three elements as doubles
// ---------------------------------------------------------------------------
std::array<double, 3> Interpreter::evalVec3(const ExprNode& expr) {
    std::array<double, 3> result = {0.0, 0.0, 0.0};
    Value v = evaluate(expr);
    if (!v.isVector()) return result;
    for (std::size_t i = 0; i < 3 && i < v.asVec().size(); ++i) {
        const Value& elem = v.asVec()[i];
        if (elem.isNumber()) result[i] = elem.asNumber();
    }
    return result;
}

// ---------------------------------------------------------------------------
// expandRange — [start:step:end] -> the concrete sequence of values it
// denotes. Mirrors OpenSCAD's inclusive-endpoint, direction-aware stepping:
// step > 0 counts up while v <= end (with a small epsilon so a step that
// lands numerically just short of end due to float error still includes
// it); step < 0 counts down while v >= end. A step of exactly 0 denotes an
// empty range (not an infinite loop).
// ---------------------------------------------------------------------------
std::vector<Value> Interpreter::expandRange(double start, double step, double end) const {
    std::vector<Value> values;
    if (step == 0.0) return values;
    if (step > 0.0)
        for (double v = start; v <= end + 1e-10 && static_cast<int>(values.size()) < kMaxRangeCount; v += step)
            values.push_back(Value::fromNumber(v));
    else
        for (double v = start; v >= end - 1e-10 && static_cast<int>(values.size()) < kMaxRangeCount; v += step)
            values.push_back(Value::fromNumber(v));
    return values;
}

// ---------------------------------------------------------------------------
// iterationValues / flattenAppend — see Interpreter.h
// ---------------------------------------------------------------------------
std::vector<Value> Interpreter::iterationValues(const Value& v) const {
    if (v.isVector()) return v.asVec();
    if (v.isRange())  return expandRange(v.rangeStart, v.rangeStep, v.rangeEnd);
    return {v};
}

void Interpreter::flattenAppend(std::vector<Value>& out, const Value& v) const {
    for (auto& e : iterationValues(v)) out.push_back(std::move(e));
}

// ---------------------------------------------------------------------------
// collectListCompBody — one list-comprehension body clause; see
// ListCompBody in Expr.h for the grammar this mirrors.
// ---------------------------------------------------------------------------
void Interpreter::collectListCompBody(const ListCompBody& body, std::vector<Value>& out) {
    // Checked before evaluating body.expr at all (not just before pushing
    // the result) since a nested comprehension's expense is in evaluating
    // it, not in appending its already-computed result.
    if (m_listCompBudget <= 0) return;

    switch (body.kind) {
    case ListCompBody::Kind::Expr:
        out.push_back(evaluate(*body.expr));
        --m_listCompBudget;
        break;
    case ListCompBody::Kind::Each: {
        Value v = evaluate(*body.expr);
        for (auto& e : iterationValues(v)) {
            if (m_listCompBudget <= 0) break;
            out.push_back(std::move(e));
            --m_listCompBudget;
        }
        break;
    }
    case ListCompBody::Kind::If:
        if (bool(evaluate(*body.condition)))
            collectListCompBody(*body.thenBody, out);
        else if (body.elseBody)
            collectListCompBody(*body.elseBody, out);
        break;
    }
}

// ---------------------------------------------------------------------------
// getVar / setVar
// ---------------------------------------------------------------------------
Value Interpreter::getVar(const std::string& name) const {
    auto it = m_env.find(name);
    return it != m_env.end() ? it->second : Value::undef();
}

void Interpreter::setVar(const std::string& name, Value val) {
    m_env[name] = std::move(val);
}

void Interpreter::assignVar(const std::string& name, const ExprNode& valueExpr) {
    Value v = evaluate(valueExpr);
    if (v.isFunction() && v.closure && std::holds_alternative<FunctionLit>(valueExpr))
        v.closure->selfName = name; // let `name(...)` recurse from within its own body (see callClosure)
    m_env[name] = std::move(v);
}

// ---------------------------------------------------------------------------
// callClosure — invoke a Value::Tag::Function closure
// ---------------------------------------------------------------------------
Value Interpreter::callClosure(Value fnVal,
                                const std::vector<std::pair<std::string, Value>>& orderedArgs,
                                const SourceLoc& callLoc) {
    if (!fnVal.closure || !fnVal.closure->def) return Value::undef();
    std::string selfName =
        fnVal.closure->selfName.empty() ? "function" : fnVal.closure->selfName;
    if (m_callDepth >= kMaxCallDepth) {
        m_recursionAborted       = true;
        m_recursionAbortedFnName = selfName;
        m_recursionAbortedLoc    = callLoc;
        m_recursionAbortedStack.assign(m_callStack.rbegin(), m_callStack.rend());
        return Value::undef();
    }
    const FunctionLit& def = *fnVal.closure->def;

    auto savedEnv = snapshotEnv();
    m_env = fnVal.closure->vars; // lexical scope: where the literal was written, not where it's called
    // Rebind the closure's own name (if any) fresh for this call only — never
    // stored back into fnVal.closure->vars itself, which would recreate the
    // shared_ptr<ClosureEnv> cycle assignVar's comment warns about.
    if (!fnVal.closure->selfName.empty())
        m_env[fnVal.closure->selfName] = fnVal;

    bindOrderedArgs(def.params, orderedArgs);

    ++m_callDepth;
    m_callStack.push_back({selfName, callLoc});
    Value result = evaluate(*def.body);
    m_callStack.pop_back();
    --m_callDepth;
    restoreEnv(std::move(savedEnv));
    return result;
}

// ---------------------------------------------------------------------------
// Built-in functions
// ---------------------------------------------------------------------------
Value Interpreter::callBuiltin(const std::string& name,
                                const std::vector<Value>& args) const {
    auto num = [&](std::size_t i) -> double {
        return (i < args.size() && args[i].isNumber()) ? args[i].asNumber() : 0.0;
    };

    // ---- Math ----
    // A domain-erroring math result (e.g. sqrt(-1), ln(0)) is a legitimate
    // nan/inf Number, not undef — matching real OpenSCAD, which lets these
    // propagate as IEEE 754 values (confirmed against a live 2021.01 build:
    // sqrt(-1) => nan, ln(0) => -inf, etc.), not collapse to undef.
    if (name == "abs")   return args.size() >= 1 ? Value::fromNumber(std::abs(num(0)))  : Value::undef();
    if (name == "sqrt")  return args.size() >= 1 ? Value::fromNumber(std::sqrt(num(0))) : Value::undef();
    if (name == "pow")   return args.size() >= 2 ? Value::fromNumber(std::pow(num(0), num(1))) : Value::undef();
    if (name == "floor") return args.size() >= 1 ? Value::fromNumber(std::floor(num(0))) : Value::undef();
    if (name == "ceil")  return args.size() >= 1 ? Value::fromNumber(std::ceil(num(0)))  : Value::undef();
    if (name == "round") return args.size() >= 1 ? Value::fromNumber(std::round(num(0))) : Value::undef();
    if (name == "exp")   return args.size() >= 1 ? Value::fromNumber(std::exp(num(0)))  : Value::undef();
    // OpenSCAD's log() is base-10; natural log is the separate ln() builtin
    // (easy to get backwards since C's log() is natural log).
    if (name == "log")   return args.size() >= 1 ? Value::fromNumber(std::log10(num(0))): Value::undef();
    if (name == "ln")    return args.size() >= 1 ? Value::fromNumber(std::log(num(0)))  : Value::undef();
    if (name == "sign")  return args.size() >= 1
                               ? Value::fromNumber(num(0) > 0.0 ? 1.0 : num(0) < 0.0 ? -1.0 : 0.0)
                               : Value::undef();

    // ---- Trig (degrees) ----
    if (name == "sin")   return args.size() >= 1 ? Value::fromNumber(std::sin(num(0) * kDeg2Rad)) : Value::undef();
    if (name == "cos")   return args.size() >= 1 ? Value::fromNumber(std::cos(num(0) * kDeg2Rad)) : Value::undef();
    if (name == "tan")   return args.size() >= 1 ? Value::fromNumber(std::tan(num(0) * kDeg2Rad)) : Value::undef();
    if (name == "asin")  return args.size() >= 1 ? Value::fromNumber(std::asin(num(0)) * kRad2Deg) : Value::undef();
    if (name == "acos")  return args.size() >= 1 ? Value::fromNumber(std::acos(num(0)) * kRad2Deg) : Value::undef();
    if (name == "atan")  return args.size() >= 1 ? Value::fromNumber(std::atan(num(0)) * kRad2Deg) : Value::undef();
    if (name == "atan2") return args.size() >= 2 ? Value::fromNumber(std::atan2(num(0), num(1)) * kRad2Deg) : Value::undef();

    // ---- min/max — variadic ----
    if (name == "min" || name == "max") {
        if (args.empty()) return Value::undef();
        bool isMin = (name == "min");
        // If first arg is a vector, take min/max of its elements — but only
        // when it's the sole argument; a vector mixed with scalar args is a
        // likely authoring mistake, so treat it as an error rather than
        // silently discarding the vector.
        if (args.size() == 1 && args[0].isVector()) {
            const auto& v = args[0].asVec();
            if (v.empty() || !v[0].isNumber()) return Value::undef();
            double m = v[0].asNumber();
            for (std::size_t i = 1; i < v.size(); ++i) {
                if (!v[i].isNumber()) return Value::undef();
                double e = v[i].asNumber();
                m = isMin ? std::min(m, e) : std::max(m, e);
            }
            return Value::fromNumber(m);
        }
        if (args[0].isVector()) return Value::undef();
        if (!args[0].isNumber()) return Value::undef();
        double m = args[0].asNumber();
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (!args[i].isNumber()) return Value::undef();
            double v = args[i].asNumber();
            m = isMin ? std::min(m, v) : std::max(m, v);
        }
        return Value::fromNumber(m);
    }

    // ---- Vector ----
    if (name == "norm") {
        // Every element must be a number (nan/inf are numbers and
        // contribute normally — only a non-number element like a string,
        // list, or range makes the whole result undef).
        if (args.size() >= 1 && args[0].isVector()) {
            double sum = 0.0;
            for (const auto& e : args[0].asVec()) {
                if (!e.isNumber()) return Value::undef();
                sum += e.asNumber() * e.asNumber();
            }
            return Value::fromNumber(std::sqrt(sum));
        }
        return Value::undef();
    }
    if (name == "cross") {
        // Unlike most arithmetic, cross() requires every component to be a
        // *finite* number — nan/inf components make the whole result undef
        // rather than propagating, matching OpenSCAD.
        auto finiteComponents = [](const std::vector<Value>& v, std::size_t n) {
            for (std::size_t i = 0; i < n; ++i)
                if (!v[i].isNumber() || !std::isfinite(v[i].asNumber())) return false;
            return true;
        };
        if (args.size() >= 2 && args[0].isVector() && args[1].isVector()) {
            const auto& a = args[0].asVec();
            const auto& b = args[1].asVec();
            if (a.size() == 3 && b.size() == 3 && finiteComponents(a, 3) && finiteComponents(b, 3)) {
                double ax = a[0].asNumber(), ay = a[1].asNumber(), az = a[2].asNumber();
                double bx = b[0].asNumber(), by = b[1].asNumber(), bz = b[2].asNumber();
                return Value::fromVec({
                    Value::fromNumber(ay * bz - az * by),
                    Value::fromNumber(az * bx - ax * bz),
                    Value::fromNumber(ax * by - ay * bx)
                });
            }
            // 2D cross product: a scalar, ax*by - ay*bx.
            if (a.size() == 2 && b.size() == 2 && finiteComponents(a, 2) && finiteComponents(b, 2)) {
                double ax = a[0].asNumber(), ay = a[1].asNumber();
                double bx = b[0].asNumber(), by = b[1].asNumber();
                return Value::fromNumber(ax * by - ay * bx);
            }
        }
        return Value::undef();
    }

    // ---- List ----
    if (name == "len") {
        if (args.size() >= 1) {
            if (args[0].isVector()) return Value::fromNumber(static_cast<double>(args[0].asVec().size()));
            if (args[0].isString()) return Value::fromNumber(static_cast<double>(utf8Length(args[0].asString())));
            if (args[0].isRange())
                return Value::fromNumber(static_cast<double>(
                    expandRange(args[0].rangeStart, args[0].rangeStep, args[0].rangeEnd).size()));
        }
        return Value::undef();
    }
    if (name == "concat") {
        std::vector<Value> result;
        for (const auto& arg : args) {
            if (arg.isVector())
                for (const auto& elem : arg.asVec()) result.push_back(elem);
            else
                result.push_back(arg);
        }
        return Value::fromVec(std::move(result));
    }

    // ---- String ----
    if (name == "str") {
        std::string out;
        for (const auto& arg : args)
            // A bare string argument is appended unquoted; every other type
            // (including a string nested inside a vector/range) goes through
            // the quoting/bracketing formatter — see formatValueRec.
            out += arg.isString() ? arg.asString() : formatValueRec(arg);
        return Value::fromString(std::move(out));
    }
    if (name == "chr") {
        if (args.size() >= 1 && args[0].isNumber()) {
            double v = args[0].asNumber();
            if (std::isfinite(v) && v >= 0 && v <= 0x10FFFF)
                return Value::fromString(encodeUtf8(static_cast<uint32_t>(v)));
        }
        return Value::undef();
    }
    if (name == "ord") {
        if (args.size() >= 1 && args[0].isString() && !args[0].asString().empty()) {
            std::size_t i = 0;
            return Value::fromNumber(static_cast<double>(decodeUtf8At(args[0].asString(), i)));
        }
        return Value::undef();
    }

    // ---- rands(min, max, count [, seed]) ----
    if (name == "rands") {
        if (args.size() < 3) return Value::undef();
        double minVal = num(0);
        double maxVal = num(1);
        double countArg = num(2);
        if (!std::isfinite(countArg) || countArg <= 0 || minVal > maxVal)
            return Value::fromVec({});
        constexpr double kMaxCount = 1'000'000.0; // sane cap against runaway allocation
        int count = static_cast<int>(std::min(countArg, kMaxCount));
        std::mt19937_64 rng;
        if (args.size() >= 4 && args[3].isNumber())
            rng.seed(static_cast<uint64_t>(args[3].asNumber()));
        else
            rng.seed(std::random_device{}());
        std::uniform_real_distribution<double> dist(minVal, maxVal);
        std::vector<Value> result;
        result.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
            result.push_back(Value::fromNumber(dist(rng)));
        return Value::fromVec(std::move(result));
    }

    // ---- lookup(key, [[k0,v0],[k1,v1],...]) ----
    if (name == "lookup") {
        if (args.size() < 2 || !args[0].isNumber() || !args[1].isVector()) return Value::undef();
        double key = args[0].asNumber();
        const auto& table = args[1].asVec();
        if (table.empty()) return Value::undef();

        // Collect valid [key, val] pairs
        std::vector<std::pair<double, double>> pairs;
        for (const auto& entry : table) {
            if (entry.isVector() && entry.asVec().size() >= 2 &&
                entry.asVec()[0].isNumber() && entry.asVec()[1].isNumber())
                pairs.push_back({entry.asVec()[0].asNumber(), entry.asVec()[1].asNumber()});
        }
        if (pairs.empty()) return Value::undef();
        std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b){ return a.first < b.first; });

        if (key <= pairs.front().first) return Value::fromNumber(pairs.front().second);
        if (key >= pairs.back().first)  return Value::fromNumber(pairs.back().second);

        for (std::size_t i = 1; i < pairs.size(); ++i) {
            if (key <= pairs[i].first) {
                double t = (key - pairs[i-1].first) / (pairs[i].first - pairs[i-1].first);
                return Value::fromNumber(pairs[i-1].second + t * (pairs[i].second - pairs[i-1].second));
            }
        }
        return Value::fromNumber(pairs.back().second);
    }

    // ---- Type predicates ----
    if (name == "is_undef")  return args.size() >= 1 ? Value::fromBool(args[0].isUndef())  : Value::undef();
    if (name == "is_bool")   return args.size() >= 1 ? Value::fromBool(args[0].isBool())   : Value::undef();
    if (name == "is_num")    return args.size() >= 1 ? Value::fromBool(args[0].isNumber()) : Value::undef();
    if (name == "is_string") return args.size() >= 1 ? Value::fromBool(args[0].isString()) : Value::undef();
    if (name == "is_list")   return args.size() >= 1 ? Value::fromBool(args[0].isVector()) : Value::undef();
    if (name == "is_function") return args.size() >= 1 ? Value::fromBool(args[0].isFunction()) : Value::undef();

    // ---- version() / version_num() ----
    // ChiselCAD isn't OpenSCAD, so there's no real "version" to report; this
    // reports a fixed OpenSCAD-compatibility level instead, chosen to match
    // the newest feature this interpreter actually supports (general list
    // comprehensions/each, from OpenSCAD 2019.05) — real-world .scad files
    // commonly gate features with `if (version_num() >= 20190500)`, and this
    // makes them pick the modern branch. Don't bump this without confirming
    // the corresponding OpenSCAD-version feature is actually supported.
    if (name == "version")     return Value::fromVec({Value::fromNumber(2019),
                                                        Value::fromNumber(5),
                                                        Value::fromNumber(0)});
    if (name == "version_num") return Value::fromNumber(20190500);

    // ---- parent_module(idx) — name of the module idx levels up the current
    // module-call chain (0 = immediate caller). See m_moduleNameStack. ----
    if (name == "parent_module") {
        // idx=1 is the immediate caller of the currently-running module,
        // idx=2 its caller, etc. (idx=0 is the current module itself) —
        // verified against a live OpenSCAD 2021.01 binary via
        // parent_module-tests.scad, which 1-indexes from the caller, not
        // 0-indexes.
        if (args.size() >= 1 && args[0].isNumber()) {
            int idx = static_cast<int>(args[0].asNumber());
            int i   = static_cast<int>(m_moduleNameStack.size()) - 1 - idx;
            if (idx >= 0 && i >= 0)
                return Value::fromString(m_moduleNameStack[static_cast<std::size_t>(i)]);
        }
        return Value::undef();
    }

    // ---- search(match_value, string_or_vector, [num_returns_per_match=1], [index_col_num=0]) ----
    if (name == "search") {
        if (args.size() < 2) return Value::undef();
        const Value& target = args[1];
        if (!target.isString() && !target.isVector()) return Value::undef();

        int numReturns = 1;
        if (args.size() >= 3 && args[2].isNumber()) numReturns = static_cast<int>(args[2].asNumber());
        int indexCol = 0;
        if (args.size() >= 4 && args[3].isNumber()) indexCol = static_cast<int>(args[3].asNumber());

        std::size_t targetSize = target.isString() ? target.asString().size() : target.asVec().size();
        // needleIsVector: a vector needle (as opposed to a scalar/string
        // char) is compared against the *whole* row rather than a single
        // extracted column, even at the default index_col_num=0 — matching
        // OpenSCAD: search([[1,2]], [[0,1],[1,2],[2,3]]) finds [1,2] at
        // index 1 by comparing whole rows, not row[0].
        auto elementAt = [&](std::size_t i, bool needleIsVector) -> Value {
            if (target.isString())
                return Value::fromString(std::string(1, target.asString()[i]));
            const Value& e = target.asVec()[i];
            if (e.isVector()) {
                if (indexCol == -1 || needleIsVector) return e; // compare against the whole row
                if (indexCol >= 0 && static_cast<std::size_t>(indexCol) < e.asVec().size())
                    return e.asVec()[static_cast<std::size_t>(indexCol)];
                return Value::undef();
            }
            return e;
        };
        auto searchOne = [&](const Value& needle) -> std::vector<Value> {
            std::vector<Value> matches;
            bool needleIsVector = needle.isVector();
            for (std::size_t i = 0; i < targetSize; ++i) {
                if (valEqRec(needle, elementAt(i, needleIsVector))) {
                    matches.push_back(Value::fromNumber(static_cast<double>(i)));
                    if (numReturns > 0 && static_cast<int>(matches.size()) >= numReturns) break;
                }
            }
            return matches;
        };

        const Value& matchVal = args[0];
        // A vector match_value always searches once per element, decomposed
        // into the outer result — even a single-element vector (verified
        // against a live OpenSCAD 2021.01 binary: search([[9,9]], table) ==
        // [[]], not []). A string only decomposes per-character when it's
        // either more than one character, or num_returns_per_match != 1: a
        // single-char string under the *default* num_returns_per_match (1)
        // instead takes the same flat path as a bare scalar match_value
        // (search("e", "abcdabcd") == [], not [[]] — same binary, confirmed
        // against search("e", "abcdabcd", 0) == [[]] for the nested case).
        //
        // With the default num_returns_per_match=1, each decomposed element's
        // result is inlined directly into the outer vector — a found index,
        // or [] if that element had no match (matching OpenSCAD:
        // search("abe", table) == [0, 1, 8], search([1,3,1000], table) ==
        // [0, 1, []]). Any other num_returns_per_match (0 = all matches, or
        // >1) instead nests each element's matches in its own sub-vector
        // (search("abe", table, 0) == [[0,4,9,10], [1,5], [8]]).
        //
        // index_col_num == -1 is the exception: it means "compare match_value
        // whole against each entire row", specifically so a *vector*
        // match_value can be matched as one unit against table rows rather
        // than decomposed per-element — so the per-element decomposition
        // above must not kick in for it.
        bool decomposeString = matchVal.isString() &&
            (matchVal.asString().size() > 1 || numReturns != 1);
        bool decompose = indexCol != -1 && (decomposeString || matchVal.isVector());
        if (decompose) {
            std::vector<Value> outer;
            auto pushNeedle = [&](const Value& needle) {
                auto matches = searchOne(needle);
                if (numReturns == 1)
                    outer.push_back(matches.empty() ? Value::fromVec({}) : matches[0]);
                else
                    outer.push_back(Value::fromVec(std::move(matches)));
            };
            if (matchVal.isString()) {
                for (char c : matchVal.asString()) pushNeedle(Value::fromString(std::string(1, c)));
            } else {
                for (const auto& needle : matchVal.asVec()) pushNeedle(needle);
            }
            return Value::fromVec(std::move(outer));
        }
        return Value::fromVec(searchOne(matchVal));
    }

    return Value::undef(); // unknown function
}

} // namespace chisel::lang
