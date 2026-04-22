#include "Interpreter.h"
#include <cmath>

static constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;

namespace chisel::lang {

// ---------------------------------------------------------------------------
// Environment loading
// ---------------------------------------------------------------------------
void Interpreter::loadAssignments(const ParseResult& result) {
    for (const auto& stmt : result.assignments) {
        m_env[stmt.name] = evaluate(*stmt.value);
    }
}

// ---------------------------------------------------------------------------
// evaluate — dispatch on ExprNode variant
// ---------------------------------------------------------------------------
Value Interpreter::evaluate(const ExprNode& expr) const {
    return std::visit([&](const auto& node) -> Value {
        using T = std::decay_t<decltype(node)>;

        // ---- Literals ----
        if constexpr (std::is_same_v<T, NumberLit>) {
            return Value::fromNumber(node.value);
        }
        else if constexpr (std::is_same_v<T, BoolLit>) {
            return Value::fromBool(node.value);
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
            return Value::undef(); // undefined variable → undef
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

            // Logical / mixed-type fallback
            auto valEq = [](const Value& a, const Value& b) -> bool {
                if (a.tag != b.tag) return false;
                if (a.isNumber()) return a.asNumber() == b.asNumber();
                if (a.isBool())   return a.asBool()   == b.asBool();
                return false; // undef/vector/string: never equal here
            };
            switch (node.op) {
            case BinaryExpr::Op::And: return Value::fromBool(bool(lv) && bool(rv));
            case BinaryExpr::Op::Or:  return Value::fromBool(bool(lv) || bool(rv));
            case BinaryExpr::Op::Eq:  return Value::fromBool(valEq(lv, rv));
            case BinaryExpr::Op::Ne:  return Value::fromBool(!valEq(lv, rv));
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
                return Value::undef();
            case UnaryExpr::Op::Not:
                return Value::fromBool(!bool(v));
            }
        }

        // ---- Function call ----
        else if constexpr (std::is_same_v<T, FunctionCall>) {
            std::vector<Value> args;
            args.reserve(node.args.size());
            for (const auto& a : node.args)
                args.push_back(evaluate(*a));
            return callBuiltin(node.name, args);
        }

        return Value::undef();
    }, expr);
}

// ---------------------------------------------------------------------------
// evalNumber — evaluate and coerce to double
// ---------------------------------------------------------------------------
double Interpreter::evalNumber(const ExprNode& expr) const {
    Value v = evaluate(expr);
    if (v.isNumber()) return v.asNumber();
    if (v.isBool())   return v.asBool() ? 1.0 : 0.0;
    return 0.0;
}

// ---------------------------------------------------------------------------
// evalVec3 — evaluate a VectorLit and return first three elements as doubles
// ---------------------------------------------------------------------------
std::array<double, 3> Interpreter::evalVec3(const ExprNode& expr) const {
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
// Built-in functions (V2a subset — math functions added in V2c)
// ---------------------------------------------------------------------------
Value Interpreter::callBuiltin(const std::string& name,
                                const std::vector<Value>& args) const {
    auto num = [&](std::size_t i) {
        return (i < args.size() && args[i].isNumber()) ? args[i].asNumber() : 0.0;
    };

    if (name == "abs")   return args.size() >= 1 ? Value::fromNumber(std::abs(num(0)))  : Value::undef();
    if (name == "sqrt")  return args.size() >= 1 ? Value::fromNumber(std::sqrt(num(0))) : Value::undef();
    if (name == "sin")   return args.size() >= 1 ? Value::fromNumber(std::sin(num(0) * kDeg2Rad)) : Value::undef();
    if (name == "cos")   return args.size() >= 1 ? Value::fromNumber(std::cos(num(0) * kDeg2Rad)) : Value::undef();
    if (name == "tan")   return args.size() >= 1 ? Value::fromNumber(std::tan(num(0) * kDeg2Rad)) : Value::undef();
    if (name == "pow")   return args.size() >= 2 ? Value::fromNumber(std::pow(num(0), num(1))) : Value::undef();
    if (name == "min")   return args.size() >= 2 ? Value::fromNumber(std::min(num(0), num(1))) : Value::undef();
    if (name == "max")   return args.size() >= 2 ? Value::fromNumber(std::max(num(0), num(1))) : Value::undef();
    if (name == "floor") return args.size() >= 1 ? Value::fromNumber(std::floor(num(0))) : Value::undef();
    if (name == "ceil")  return args.size() >= 1 ? Value::fromNumber(std::ceil(num(0)))  : Value::undef();
    if (name == "round") return args.size() >= 1 ? Value::fromNumber(std::round(num(0))) : Value::undef();
    if (name == "log")   return args.size() >= 1 ? Value::fromNumber(std::log(num(0)))   : Value::undef();
    if (name == "exp")   return args.size() >= 1 ? Value::fromNumber(std::exp(num(0)))   : Value::undef();
    if (name == "len")   return (args.size() >= 1 && args[0].isVector())
                                ? Value::fromNumber(static_cast<double>(args[0].asVec().size()))
                                : Value::undef();

    return Value::undef(); // unknown function
}

} // namespace chisel::lang
