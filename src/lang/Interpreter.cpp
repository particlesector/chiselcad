#include "Interpreter.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

static constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
static constexpr double kRad2Deg = 180.0 / 3.14159265358979323846;

namespace chisel::lang {

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
    return a.isUndef(); // both Undef, since tags already matched
}

// ---------------------------------------------------------------------------
// Environment loading
// ---------------------------------------------------------------------------
void Interpreter::loadAssignments(const ParseResult& result) {
    for (const auto& stmt : result.assignments)
        m_env[stmt.name] = evaluate(*stmt.value);
}

void Interpreter::loadFunctions(const ParseResult& result) {
    for (const auto& def : result.functionDefs)
        m_funcDefs[def.name] = &def;
}

// ---------------------------------------------------------------------------
// evaluate — dispatch on ExprNode variant
// ---------------------------------------------------------------------------
Value Interpreter::evaluate(const ExprNode& expr) {
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
            for (const auto& e : node.elements)
                elems.push_back(evaluate(*e));
            return Value::fromVec(std::move(elems));
        }

        // ---- Variable reference ----
        else if constexpr (std::is_same_v<T, VarRef>) {
            auto it = m_env.find(node.name);
            if (it != m_env.end()) return it->second;
            return Value::undef();
        }

        // ---- Binary expression ----
        else if constexpr (std::is_same_v<T, BinaryExpr>) {
            Value lv = evaluate(*node.left);
            Value rv = evaluate(*node.right);

            // Arithmetic (number × number)
            if (lv.isNumber() && rv.isNumber()) {
                double l = lv.asNumber(), r = rv.asNumber();
                switch (node.op) {
                case BinaryExpr::Op::Add: return Value::fromNumber(l + r);
                case BinaryExpr::Op::Sub: return Value::fromNumber(l - r);
                case BinaryExpr::Op::Mul: return Value::fromNumber(l * r);
                case BinaryExpr::Op::Div: return r != 0.0
                        ? Value::fromNumber(l / r) : Value::undef();
                case BinaryExpr::Op::Mod: return r != 0.0
                        ? Value::fromNumber(std::fmod(l, r)) : Value::undef();
                case BinaryExpr::Op::Eq:  return Value::fromBool(l == r);
                case BinaryExpr::Op::Ne:  return Value::fromBool(l != r);
                case BinaryExpr::Op::Lt:  return Value::fromBool(l <  r);
                case BinaryExpr::Op::Le:  return Value::fromBool(l <= r);
                case BinaryExpr::Op::Gt:  return Value::fromBool(l >  r);
                case BinaryExpr::Op::Ge:  return Value::fromBool(l >= r);
                case BinaryExpr::Op::And: return Value::fromBool(l != 0.0 && r != 0.0);
                case BinaryExpr::Op::Or:  return Value::fromBool(l != 0.0 || r != 0.0);
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

            // Logical / mixed-type fallback
            switch (node.op) {
            case BinaryExpr::Op::And: return Value::fromBool(bool(lv) && bool(rv));
            case BinaryExpr::Op::Or:  return Value::fromBool(bool(lv) || bool(rv));
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
                if (static_cast<std::size_t>(i) >= target.asString().size())
                    return Value::undef();
                return Value::fromString(std::string(1, target.asString()[static_cast<std::size_t>(i)]));
            }
            return Value::undef();
        }

        // ---- Let expression: let(x = expr, ...) body ----
        else if constexpr (std::is_same_v<T, LetExpr>) {
            auto savedEnv = snapshotEnv();
            for (const auto& [name, valExpr] : node.bindings)
                setVar(name, evaluate(*valExpr));
            Value result = evaluate(*node.body);
            restoreEnv(std::move(savedEnv));
            return result;
        }

        // ---- Function call ----
        else if constexpr (std::is_same_v<T, FunctionCall>) {
            // Collect positional and named argument values
            std::vector<Value>                        posArgs;
            std::vector<std::pair<std::string, Value>> namedArgs;
            for (const auto& arg : node.args) {
                Value v = evaluate(*arg.value);
                if (arg.name.empty())
                    posArgs.push_back(std::move(v));
                else
                    namedArgs.push_back({arg.name, std::move(v)});
            }

            // Try user-defined function first
            auto fit = m_funcDefs.find(node.name);
            if (fit != m_funcDefs.end()) {
                const FunctionDef& def = *fit->second;
                auto savedEnv = snapshotEnv();

                std::size_t posIdx = 0;
                for (const auto& param : def.params) {
                    // Check named args
                    bool bound = false;
                    for (const auto& [n, v] : namedArgs) {
                        if (n == param.name) { setVar(param.name, v); bound = true; break; }
                    }
                    if (!bound) {
                        if (posIdx < posArgs.size())
                            setVar(param.name, posArgs[posIdx++]);
                        else if (param.defaultVal)
                            setVar(param.name, evaluate(*param.defaultVal));
                        // else: unbound → undef (already the case in a fresh env)
                    }
                }

                Value result = evaluate(*def.body);
                restoreEnv(std::move(savedEnv));
                return result;
            }

            // Fall back to built-ins (positional args only)
            std::vector<Value> allArgs = std::move(posArgs);
            for (auto& [n, v] : namedArgs) allArgs.push_back(std::move(v));
            return callBuiltin(node.name, allArgs);
        }

        return Value::undef();
    }, expr);
}

// ---------------------------------------------------------------------------
// evalNumber — evaluate and coerce to double
// ---------------------------------------------------------------------------
double Interpreter::evalNumber(const ExprNode& expr) {
    Value v = evaluate(expr);
    if (v.isNumber()) return v.asNumber();
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
// getVar / setVar
// ---------------------------------------------------------------------------
Value Interpreter::getVar(const std::string& name) const {
    auto it = m_env.find(name);
    return it != m_env.end() ? it->second : Value::undef();
}

void Interpreter::setVar(const std::string& name, Value val) {
    m_env[name] = std::move(val);
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
    // Wrap a possibly domain-erroring math result: NaN/Inf becomes undef,
    // matching the Div/Mod convention of returning undef on error rather
    // than letting NaN/Inf silently poison downstream arithmetic.
    auto finite = [](double v) -> Value {
        return std::isfinite(v) ? Value::fromNumber(v) : Value::undef();
    };

    if (name == "abs")   return args.size() >= 1 ? Value::fromNumber(std::abs(num(0)))  : Value::undef();
    if (name == "sqrt")  return args.size() >= 1 ? finite(std::sqrt(num(0))) : Value::undef();
    if (name == "pow")   return args.size() >= 2 ? finite(std::pow(num(0), num(1))) : Value::undef();
    if (name == "floor") return args.size() >= 1 ? Value::fromNumber(std::floor(num(0))) : Value::undef();
    if (name == "ceil")  return args.size() >= 1 ? Value::fromNumber(std::ceil(num(0)))  : Value::undef();
    if (name == "round") return args.size() >= 1 ? Value::fromNumber(std::round(num(0))) : Value::undef();
    if (name == "exp")   return args.size() >= 1 ? finite(std::exp(num(0)))  : Value::undef();
    if (name == "log")   return args.size() >= 1 ? finite(std::log(num(0)))  : Value::undef();
    if (name == "log10") return args.size() >= 1 ? finite(std::log10(num(0))): Value::undef();
    if (name == "sign")  return args.size() >= 1
                               ? Value::fromNumber(num(0) > 0.0 ? 1.0 : num(0) < 0.0 ? -1.0 : 0.0)
                               : Value::undef();

    // ---- Trig (degrees) ----
    if (name == "sin")   return args.size() >= 1 ? Value::fromNumber(std::sin(num(0) * kDeg2Rad)) : Value::undef();
    if (name == "cos")   return args.size() >= 1 ? Value::fromNumber(std::cos(num(0) * kDeg2Rad)) : Value::undef();
    if (name == "tan")   return args.size() >= 1 ? Value::fromNumber(std::tan(num(0) * kDeg2Rad)) : Value::undef();
    if (name == "asin")  return args.size() >= 1 ? finite(std::asin(num(0)) * kRad2Deg) : Value::undef();
    if (name == "acos")  return args.size() >= 1 ? finite(std::acos(num(0)) * kRad2Deg) : Value::undef();
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
        if (args.size() >= 1 && args[0].isVector()) {
            double sum = 0.0;
            for (const auto& e : args[0].asVec())
                if (e.isNumber()) sum += e.asNumber() * e.asNumber();
            return Value::fromNumber(std::sqrt(sum));
        }
        return Value::undef();
    }
    if (name == "cross") {
        if (args.size() >= 2 && args[0].isVector() && args[1].isVector()) {
            const auto& a = args[0].asVec();
            const auto& b = args[1].asVec();
            if (a.size() >= 3 && b.size() >= 3 &&
                a[0].isNumber() && a[1].isNumber() && a[2].isNumber() &&
                b[0].isNumber() && b[1].isNumber() && b[2].isNumber()) {
                double ax = a[0].asNumber(), ay = a[1].asNumber(), az = a[2].asNumber();
                double bx = b[0].asNumber(), by = b[1].asNumber(), bz = b[2].asNumber();
                return Value::fromVec({
                    Value::fromNumber(ay * bz - az * by),
                    Value::fromNumber(az * bx - ax * bz),
                    Value::fromNumber(ax * by - ay * bx)
                });
            }
        }
        return Value::undef();
    }

    // ---- List ----
    if (name == "len") {
        if (args.size() >= 1) {
            if (args[0].isVector()) return Value::fromNumber(static_cast<double>(args[0].asVec().size()));
            if (args[0].isString()) return Value::fromNumber(static_cast<double>(args[0].asString().size()));
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
        for (const auto& arg : args) {
            if (arg.isNumber()) {
                double v = arg.asNumber();
                if (std::isnan(v)) {
                    out += "nan";
                } else if (std::isinf(v)) {
                    out += v < 0 ? "-inf" : "inf";
                } else {
                    char buf[32];
                    constexpr double kMaxExactInt = 9.007199254740992e15; // 2^53
                    if (std::abs(v) < kMaxExactInt && v == static_cast<long long>(v))
                        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
                    else
                        std::snprintf(buf, sizeof(buf), "%g", v);
                    out += buf;
                }
            } else if (arg.isBool())   { out += arg.asBool() ? "true" : "false"; }
            else if (arg.isString())   { out += arg.asString(); }
            else if (arg.isUndef())    { out += "undef"; }
        }
        return Value::fromString(std::move(out));
    }
    if (name == "chr") {
        if (args.size() >= 1 && args[0].isNumber()) {
            double v = args[0].asNumber();
            if (std::isfinite(v) && v >= 0 && v <= 127)
                return Value::fromString(std::string(1, static_cast<char>(static_cast<int>(v))));
        }
        return Value::undef();
    }
    if (name == "ord") {
        if (args.size() >= 1 && args[0].isString() && !args[0].asString().empty())
            return Value::fromNumber(static_cast<double>(
                static_cast<unsigned char>(args[0].asString()[0])));
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

    return Value::undef(); // unknown function
}

} // namespace chisel::lang
