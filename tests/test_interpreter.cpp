#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "lang/Interpreter.h"
#include "lang/Lexer.h"
#include "lang/Parser.h"

using namespace chisel::lang;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Parse a standalone expression (as dummy assignment _v = expr;) and evaluate.
static double evalNum(std::string_view src) {
    std::string full = "_v = ";
    full += src;
    full += ";";
    Lexer  lexer(full);
    auto   tokens = lexer.tokenize();
    REQUIRE_FALSE(lexer.hasErrors());
    Parser parser(std::move(tokens));
    auto   result = parser.parse();
    REQUIRE_FALSE(parser.hasErrors());
    REQUIRE(result.assignments.size() == 1);
    Interpreter interp;
    return interp.evalNumber(*result.assignments[0].value);
}

// Build an Interpreter with assignments loaded from src.
static Interpreter loadEnv(std::string_view src) {
    Lexer  lexer(src);
    auto   tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto   result = parser.parse();
    Interpreter interp;
    interp.loadAssignments(result);
    return interp;
}

// ---------------------------------------------------------------------------
// Literals and unary
// ---------------------------------------------------------------------------
TEST_CASE("Interp:integer literal", "[interp]") {
    REQUIRE(evalNum("42")  == Approx(42.0));
    REQUIRE(evalNum("0")   == Approx(0.0));
}

TEST_CASE("Interp:float literal", "[interp]") {
    REQUIRE(evalNum("3.14") == Approx(3.14));
    REQUIRE(evalNum(".5")   == Approx(0.5));
}

TEST_CASE("Interp:unary negate", "[interp]") {
    REQUIRE(evalNum("-7")  == Approx(-7.0));
    REQUIRE(evalNum("--3") == Approx(3.0));
}

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------
TEST_CASE("Interp:addition", "[interp]") {
    REQUIRE(evalNum("1 + 2")    == Approx(3.0));
    REQUIRE(evalNum("10 + 0.5") == Approx(10.5));
}

TEST_CASE("Interp:subtraction", "[interp]") {
    REQUIRE(evalNum("5 - 3") == Approx(2.0));
    REQUIRE(evalNum("0 - 1") == Approx(-1.0));
}

TEST_CASE("Interp:multiplication", "[interp]") {
    REQUIRE(evalNum("3 * 4")   == Approx(12.0));
    REQUIRE(evalNum("2.5 * 2") == Approx(5.0));
}

TEST_CASE("Interp:division", "[interp]") {
    REQUIRE(evalNum("10 / 4") == Approx(2.5));
    REQUIRE(evalNum("1 / 0")  == Approx(0.0)); // div-by-zero → undef → 0
}

TEST_CASE("Interp:modulo", "[interp]") {
    REQUIRE(evalNum("10 % 3") == Approx(1.0));
    REQUIRE(evalNum("7 % 7")  == Approx(0.0));
}

TEST_CASE("Interp:operator precedence", "[interp]") {
    REQUIRE(evalNum("2 + 3 * 4")   == Approx(14.0)); // * before +
    REQUIRE(evalNum("(2 + 3) * 4") == Approx(20.0));
    REQUIRE(evalNum("10 - 2 - 3")  == Approx(5.0));  // left-associative
    REQUIRE(evalNum("20 / 4 / 2")  == Approx(2.5));  // (20/4)/2
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------
TEST_CASE("Interp:comparison operators", "[interp]") {
    REQUIRE(evalNum("3 < 5")  == Approx(1.0));
    REQUIRE(evalNum("5 < 3")  == Approx(0.0));
    REQUIRE(evalNum("3 <= 3") == Approx(1.0));
    REQUIRE(evalNum("5 > 3")  == Approx(1.0));
    REQUIRE(evalNum("3 >= 4") == Approx(0.0));
    REQUIRE(evalNum("3 == 3") == Approx(1.0));
    REQUIRE(evalNum("3 != 4") == Approx(1.0));
    REQUIRE(evalNum("3 != 3") == Approx(0.0));
}

// ---------------------------------------------------------------------------
// Variables
// ---------------------------------------------------------------------------
TEST_CASE("Interp:variable reference", "[interp]") {
    auto interp = loadEnv("r = 5;");
    ExprNode e = VarRef{"r", {}};
    REQUIRE(interp.evalNumber(e) == Approx(5.0));
}

TEST_CASE("Interp:multiple variables", "[interp]") {
    auto interp = loadEnv("w = 10; h = 3;");
    ExprNode ew = VarRef{"w", {}};
    ExprNode eh = VarRef{"h", {}};
    REQUIRE(interp.evalNumber(ew) == Approx(10.0));
    REQUIRE(interp.evalNumber(eh) == Approx(3.0));
}

TEST_CASE("Interp:undefined variable returns 0", "[interp]") {
    Interpreter interp;
    ExprNode e = VarRef{"nothing", {}};
    REQUIRE(interp.evalNumber(e) == Approx(0.0));
}

// ---------------------------------------------------------------------------
// Built-in math functions
// ---------------------------------------------------------------------------
TEST_CASE("Interp:abs", "[interp]") {
    REQUIRE(evalNum("abs(-5)") == Approx(5.0));
    REQUIRE(evalNum("abs(3)")  == Approx(3.0));
}

TEST_CASE("Interp:sqrt", "[interp]") {
    REQUIRE(evalNum("sqrt(4)") == Approx(2.0));
    REQUIRE(evalNum("sqrt(9)") == Approx(3.0));
    REQUIRE(evalNum("sqrt(2)") == Approx(1.41421356).margin(1e-6));
}

TEST_CASE("Interp:trig in degrees", "[interp]") {
    REQUIRE(evalNum("sin(0)")  == Approx(0.0).margin(1e-9));
    REQUIRE(evalNum("sin(90)") == Approx(1.0).margin(1e-9));
    REQUIRE(evalNum("cos(0)")  == Approx(1.0).margin(1e-9));
    REQUIRE(evalNum("cos(90)") == Approx(0.0).margin(1e-9));
    REQUIRE(evalNum("tan(45)") == Approx(1.0).margin(1e-9));
}

TEST_CASE("Interp:pow", "[interp]") {
    REQUIRE(evalNum("pow(2, 10)") == Approx(1024.0));
    REQUIRE(evalNum("pow(3, 2)")  == Approx(9.0));
}

TEST_CASE("Interp:min max", "[interp]") {
    REQUIRE(evalNum("min(3, 7)") == Approx(3.0));
    REQUIRE(evalNum("max(3, 7)") == Approx(7.0));
    REQUIRE(evalNum("min(7, 3)") == Approx(3.0));
    REQUIRE(evalNum("max(7, 3)") == Approx(7.0));
}

TEST_CASE("Interp:floor ceil round", "[interp]") {
    REQUIRE(evalNum("floor(3.7)") == Approx(3.0));
    REQUIRE(evalNum("ceil(3.2)")  == Approx(4.0));
    REQUIRE(evalNum("round(3.5)") == Approx(4.0));
    REQUIRE(evalNum("round(3.4)") == Approx(3.0));
}

TEST_CASE("Interp:log exp", "[interp]") {
    REQUIRE(evalNum("exp(0)")      == Approx(1.0));
    REQUIRE(evalNum("log(1)")      == Approx(0.0));
    REQUIRE(evalNum("exp(log(5))") == Approx(5.0).margin(1e-9));
}

TEST_CASE("Interp:unknown function returns 0", "[interp]") {
    REQUIRE(evalNum("foobar(42)") == Approx(0.0));
}

// ---------------------------------------------------------------------------
// Integration: variable-driven primitive param (via CsgEvaluator path)
// ---------------------------------------------------------------------------
TEST_CASE("Interp:variable assignment chain", "[interp]") {
    // Parse script with variable assignments and check env values.
    auto interp = loadEnv("base = 10; factor = 2;");
    ExprNode eBase   = VarRef{"base",   {}};
    ExprNode eFactor = VarRef{"factor", {}};
    REQUIRE(interp.evalNumber(eBase)   == Approx(10.0));
    REQUIRE(interp.evalNumber(eFactor) == Approx(2.0));
}

// ---------------------------------------------------------------------------
// Tier A: undef literal
// ---------------------------------------------------------------------------
TEST_CASE("Interp:undef literal", "[interp][tier-a]") {
    std::string src = "_v = undef;";
    Lexer lexer(src); auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens)); auto result = parser.parse();
    Interpreter interp;
    Value v = interp.evaluate(*result.assignments[0].value);
    REQUIRE(v.isUndef());
    REQUIRE(interp.evalNumber(*result.assignments[0].value) == Approx(0.0));
}

// ---------------------------------------------------------------------------
// Tier A: ternary operator
// ---------------------------------------------------------------------------
TEST_CASE("Interp:ternary true branch", "[interp][tier-a]") {
    REQUIRE(evalNum("true  ? 1 : 2") == Approx(1.0));
    REQUIRE(evalNum("false ? 1 : 2") == Approx(2.0));
    REQUIRE(evalNum("1 > 0 ? 10 : 20") == Approx(10.0));
    REQUIRE(evalNum("1 < 0 ? 10 : 20") == Approx(20.0));
}

TEST_CASE("Interp:nested ternary", "[interp][tier-a]") {
    REQUIRE(evalNum("1 == 1 ? (2 == 2 ? 42 : 0) : 0") == Approx(42.0));
}

// ---------------------------------------------------------------------------
// Tier A: list indexing
// ---------------------------------------------------------------------------
TEST_CASE("Interp:list index", "[interp][tier-a]") {
    REQUIRE(evalNum("[10, 20, 30][0]") == Approx(10.0));
    REQUIRE(evalNum("[10, 20, 30][1]") == Approx(20.0));
    REQUIRE(evalNum("[10, 20, 30][2]") == Approx(30.0));
}

TEST_CASE("Interp:list index out of bounds returns 0", "[interp][tier-a]") {
    REQUIRE(evalNum("[1, 2][5]") == Approx(0.0)); // undef → 0
}

TEST_CASE("Interp:chained index", "[interp][tier-a]") {
    REQUIRE(evalNum("[[1,2],[3,4]][1][0]") == Approx(3.0));
}

// ---------------------------------------------------------------------------
// Tier A: let expression
// ---------------------------------------------------------------------------
TEST_CASE("Interp:let expression", "[interp][tier-a]") {
    REQUIRE(evalNum("let(x=5) x * 2") == Approx(10.0));
    REQUIRE(evalNum("let(x=3, y=4) x + y") == Approx(7.0));
}

TEST_CASE("Interp:let does not leak into outer scope", "[interp][tier-a]") {
    // after let expression, the outer scope is restored
    std::string src = "outer = 99; _v = let(outer = 1) outer;";
    Lexer lexer(src); auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens)); auto result = parser.parse();
    Interpreter interp;
    interp.loadAssignments(result);
    // After loading, outer should still be 99
    ExprNode eOuter = VarRef{"outer", {}};
    REQUIRE(interp.evalNumber(eOuter) == Approx(99.0));
}

// ---------------------------------------------------------------------------
// Tier A: user-defined functions
// ---------------------------------------------------------------------------
// m_funcDefs stores non-owning pointers into ParseResult, so result must
// outlive the Interpreter. Return both together.
struct InterpCtx {
    ParseResult  result;
    Interpreter  interp;
};

static InterpCtx loadEnvWithFuncs(std::string_view src) {
    Lexer  lexer(src);
    auto   tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    InterpCtx ctx;
    ctx.result = parser.parse();
    ctx.interp.loadAssignments(ctx.result);
    ctx.interp.loadFunctions(ctx.result);
    return ctx;
}

// Helper: build a FunctionCall expression with positional number args
static ExprNode makeCall(std::string name, std::vector<double> nums) {
    FunctionCall fc;
    fc.name = std::move(name);
    for (double v : nums) {
        FunctionArg arg;
        arg.value = makeExpr(NumberLit{v, {}});
        fc.args.push_back(std::move(arg));
    }
    return fc;
}

TEST_CASE("Interp:user function basic", "[interp][tier-a]") {
    auto ctx = loadEnvWithFuncs("function double(x) = x * 2;");
    ExprNode call = makeCall("double", {5.0});
    REQUIRE(ctx.interp.evalNumber(call) == Approx(10.0));
}

TEST_CASE("Interp:user function with default param", "[interp][tier-a]") {
    auto ctx = loadEnvWithFuncs("function inc(x, step=1) = x + step;");
    ExprNode call1 = makeCall("inc", {10.0});
    REQUIRE(ctx.interp.evalNumber(call1) == Approx(11.0));
    ExprNode call2 = makeCall("inc", {10.0, 5.0});
    REQUIRE(ctx.interp.evalNumber(call2) == Approx(15.0));
}

TEST_CASE("Interp:recursive user function", "[interp][tier-a]") {
    auto ctx = loadEnvWithFuncs("function fact(n) = n <= 1 ? 1 : n * fact(n - 1);");
    ExprNode call = makeCall("fact", {5.0});
    REQUIRE(ctx.interp.evalNumber(call) == Approx(120.0));
}

// ---------------------------------------------------------------------------
// && / || short-circuit (#28) and recursion-depth guard (#29)
// ---------------------------------------------------------------------------
TEST_CASE("Interp:&& short-circuits and does not evaluate right operand", "[interp][bugfix]") {
    // bad() has no base case; if `&&` evaluated both operands unconditionally,
    // this would recurse until the native stack overflows and crash the test.
    auto ctx = loadEnvWithFuncs(
        "function bad(x) = bad(x);\n"
        "function h(n) = (n > 0) && bad(n);");
    ExprNode call = makeCall("h", {-1.0});
    REQUIRE(bool(ctx.interp.evaluate(call)) == false);
}

TEST_CASE("Interp:|| short-circuits and does not evaluate right operand", "[interp][bugfix]") {
    auto ctx = loadEnvWithFuncs(
        "function bad(x) = bad(x);\n"
        "function h(n) = (n > 0) || bad(n);");
    ExprNode call = makeCall("h", {1.0});
    REQUIRE(bool(ctx.interp.evaluate(call)) == true);
}

TEST_CASE("Interp:|| short-circuit lets a guarded recursive function terminate", "[interp][bugfix]") {
    // Idiomatic recursion guard: functions have no `if`, so `n<=0 || f(n-1)`
    // relies entirely on `||` never evaluating its right side once n<=0.
    auto ctx = loadEnvWithFuncs("function f(n) = n <= 0 || f(n-1);");
    ExprNode call = makeCall("f", {3.0});
    REQUIRE(bool(ctx.interp.evaluate(call)) == true);
}

TEST_CASE("Interp:unbounded function recursion returns undef instead of crashing", "[interp][bugfix]") {
    auto ctx = loadEnvWithFuncs("function f(x) = f(x+1);");
    ExprNode call = makeCall("f", {0.0});
    Value r = ctx.interp.evaluate(call);
    REQUIRE(r.isUndef());
}

TEST_CASE("Interp:deep-but-bounded recursion still computes the correct result", "[interp][bugfix]") {
    // Depth 100 is comfortably under kMaxCallDepth (200); the guard must not
    // affect legitimate recursive functions at ordinary depths.
    auto ctx = loadEnvWithFuncs("function sum(n) = n <= 0 ? 0 : n + sum(n - 1);");
    ExprNode call = makeCall("sum", {100.0});
    REQUIRE(ctx.interp.evalNumber(call) == Approx(5050.0));
}

// ---------------------------------------------------------------------------
// Tier A: concat
// ---------------------------------------------------------------------------
TEST_CASE("Interp:concat", "[interp][tier-a]") {
    REQUIRE(evalNum("len(concat([1,2],[3,4]))") == Approx(4.0));
    REQUIRE(evalNum("concat([1,2],[3,4])[2]")   == Approx(3.0));
}

// ---------------------------------------------------------------------------
// Tier A: new math — inverse trig, norm, cross, sign
// ---------------------------------------------------------------------------
TEST_CASE("Interp:inverse trig", "[interp][tier-a]") {
    REQUIRE(evalNum("asin(1)") == Approx(90.0).margin(1e-9));
    REQUIRE(evalNum("acos(1)") == Approx(0.0).margin(1e-9));
    REQUIRE(evalNum("atan(1)") == Approx(45.0).margin(1e-9));
    REQUIRE(evalNum("atan2(1,1)") == Approx(45.0).margin(1e-9));
}

TEST_CASE("Interp:norm", "[interp][tier-a]") {
    REQUIRE(evalNum("norm([3,4])") == Approx(5.0).margin(1e-9));
    REQUIRE(evalNum("norm([0,0,1])") == Approx(1.0));
}

TEST_CASE("Interp:sign", "[interp][tier-a]") {
    REQUIRE(evalNum("sign(5)")  == Approx(1.0));
    REQUIRE(evalNum("sign(-3)") == Approx(-1.0));
    REQUIRE(evalNum("sign(0)")  == Approx(0.0));
}

TEST_CASE("Interp:cross product", "[interp][tier-a]") {
    REQUIRE(evalNum("cross([1,0,0],[0,1,0])[2]") == Approx(1.0));
    REQUIRE(evalNum("cross([1,0,0],[0,1,0])[0]") == Approx(0.0));
}

TEST_CASE("Interp:str function", "[interp][tier-a]") {
    REQUIRE(evalNum("len(str(42))")    == Approx(2.0));
    REQUIRE(evalNum("len(str(3.14))")  == Approx(4.0));
}

// ---------------------------------------------------------------------------
// Tier B: string literals
// ---------------------------------------------------------------------------
static Value evalVal(std::string_view src) {
    std::string full = "_v = ";
    full += src;
    full += ";";
    Lexer  lexer(full);
    auto   tokens = lexer.tokenize();
    REQUIRE_FALSE(lexer.hasErrors());
    Parser parser(std::move(tokens));
    auto   result = parser.parse();
    REQUIRE_FALSE(parser.hasErrors());
    REQUIRE(result.assignments.size() == 1);
    Interpreter interp;
    return interp.evaluate(*result.assignments[0].value);
}

TEST_CASE("Interp:string literal basic", "[interp][tier-b]") {
    Value v = evalVal("\"hello\"");
    REQUIRE(v.isString());
    REQUIRE(v.asString() == "hello");
}

TEST_CASE("Interp:string literal escape sequences", "[interp][tier-b]") {
    Value v = evalVal("\"a\\\"b\"");
    REQUIRE(v.isString());
    REQUIRE(v.asString() == "a\"b");
}

TEST_CASE("Interp:string literal empty", "[interp][tier-b]") {
    Value v = evalVal("\"\"");
    REQUIRE(v.isString());
    REQUIRE(v.asString().empty());
}

TEST_CASE("Interp:len on string literal", "[interp][tier-b]") {
    REQUIRE(evalNum("len(\"hello\")") == Approx(5.0));
    REQUIRE(evalNum("len(\"\")") == Approx(0.0));
}

TEST_CASE("Interp:str concat with string literal", "[interp][tier-b]") {
    Value v = evalVal("str(\"x=\", 5)");
    REQUIRE(v.isString());
    REQUIRE(v.asString() == "x=5");
}

TEST_CASE("Interp:index into string literal", "[interp][tier-b]") {
    Value v = evalVal("\"abc\"[1]");
    REQUIRE(v.isString());
    REQUIRE(v.asString() == "b");
}

TEST_CASE("Interp:chr and ord roundtrip", "[interp][tier-b]") {
    REQUIRE(evalNum("ord(\"A\")") == Approx(65.0));
    Value v = evalVal("chr(65)");
    REQUIRE(v.isString());
    REQUIRE(v.asString() == "A");
}

// ---------------------------------------------------------------------------
// Tier B: rands
// ---------------------------------------------------------------------------
TEST_CASE("Interp:rands count", "[interp][tier-b]") {
    REQUIRE(evalNum("len(rands(0, 1, 5))") == Approx(5.0));
    REQUIRE(evalNum("len(rands(0, 1, 10))") == Approx(10.0));
}

TEST_CASE("Interp:rands range", "[interp][tier-b]") {
    // With a fixed seed, values should stay in [min,max]
    REQUIRE(evalNum("rands(2, 5, 1, 42)[0]") >= 2.0);
    REQUIRE(evalNum("rands(2, 5, 1, 42)[0]") <= 5.0);
}

TEST_CASE("Interp:rands seeded deterministic", "[interp][tier-b]") {
    double r1 = evalNum("rands(0, 100, 3, 99)[0]");
    double r2 = evalNum("rands(0, 100, 3, 99)[0]");
    REQUIRE(r1 == Approx(r2));
}

TEST_CASE("Interp:rands empty count", "[interp][tier-b]") {
    REQUIRE(evalNum("len(rands(0, 1, 0))") == Approx(0.0));
}

// ---------------------------------------------------------------------------
// Tier B: lookup
// ---------------------------------------------------------------------------
TEST_CASE("Interp:lookup exact match", "[interp][tier-b]") {
    REQUIRE(evalNum("lookup(0, [[0,10],[1,20]])") == Approx(10.0));
    REQUIRE(evalNum("lookup(1, [[0,10],[1,20]])") == Approx(20.0));
}

TEST_CASE("Interp:lookup interpolation", "[interp][tier-b]") {
    REQUIRE(evalNum("lookup(0.5, [[0,0],[1,10]])") == Approx(5.0));
    REQUIRE(evalNum("lookup(0.25, [[0,0],[1,100]])") == Approx(25.0));
}

TEST_CASE("Interp:lookup clamp below", "[interp][tier-b]") {
    REQUIRE(evalNum("lookup(-1, [[0,5],[1,10]])") == Approx(5.0));
}

TEST_CASE("Interp:lookup clamp above", "[interp][tier-b]") {
    REQUIRE(evalNum("lookup(99, [[0,5],[1,10]])") == Approx(10.0));
}

TEST_CASE("Interp:lookup unsorted table", "[interp][tier-b]") {
    // lookup must sort by key
    REQUIRE(evalNum("lookup(0.5, [[1,10],[0,0]])") == Approx(5.0));
}

// ---------------------------------------------------------------------------
// String/vector equality (issue #34)
// ---------------------------------------------------------------------------
TEST_CASE("Interp:string equality", "[interp]") {
    REQUIRE(evalVal("\"abc\" == \"abc\"").asBool() == true);
    REQUIRE(evalVal("\"abc\" == \"xyz\"").asBool() == false);
    REQUIRE(evalVal("\"abc\" != \"xyz\"").asBool() == true);
}

TEST_CASE("Interp:vector equality", "[interp]") {
    REQUIRE(evalVal("[1,2] == [1,2]").asBool() == true);
    REQUIRE(evalVal("[1,2] == [1,3]").asBool() == false);
    REQUIRE(evalVal("[1,2] == [1,2,3]").asBool() == false); // length mismatch
    REQUIRE(evalVal("[[1,2],3] == [[1,2],3]").asBool() == true); // nested vectors
}

// ---------------------------------------------------------------------------
// Vector +/- with mismatched lengths returns undef (issue #45)
// ---------------------------------------------------------------------------
TEST_CASE("Interp:vector add/sub length mismatch is undef", "[interp]") {
    REQUIRE(evalVal("[1,2,3] + [1,2]").isUndef());
    REQUIRE(evalVal("[1,2,3] - [1,2]").isUndef());
    REQUIRE(evalNum("([1,2,3] + [1,2,3])[2]") == Approx(6.0));
}

// ---------------------------------------------------------------------------
// Math domain errors return undef instead of NaN/Inf (issue #43)
// ---------------------------------------------------------------------------
TEST_CASE("Interp:math domain errors return undef", "[interp]") {
    REQUIRE(evalVal("sqrt(-1)").isUndef());
    REQUIRE(evalVal("log(-1)").isUndef());
    REQUIRE(evalVal("log(0)").isUndef());
    REQUIRE(evalVal("asin(2)").isUndef());
    REQUIRE(evalVal("acos(2)").isUndef());
    REQUIRE(evalNum("sqrt(4)") == Approx(2.0));
}

// ---------------------------------------------------------------------------
// min/max no longer silently coerce bad args (issue #58)
// ---------------------------------------------------------------------------
TEST_CASE("Interp:min max reject non-numeric args", "[interp]") {
    REQUIRE(evalVal("max(3, \"abc\")").isUndef());
    REQUIRE(evalVal("min(3, \"abc\")").isUndef());
    REQUIRE(evalVal("max([1,2,3], 5)").isUndef()); // vector mixed with scalar
    REQUIRE(evalNum("max([1,2,3])") == Approx(3.0)); // sole vector arg still works
    REQUIRE(evalNum("min(3, 7)") == Approx(3.0));
}

// ---------------------------------------------------------------------------
// cross() validates element types like norm() (issue #57)
// ---------------------------------------------------------------------------
TEST_CASE("Interp:cross rejects non-numeric elements", "[interp]") {
    REQUIRE(evalVal("cross([\"a\",\"b\",\"c\"], [1,2,3])").isUndef());
    REQUIRE(evalNum("cross([1,0,0],[0,1,0])[2]") == Approx(1.0));
}

// ---------------------------------------------------------------------------
// Unchecked double->int casts no longer UB / leak "-nan" (issue #44)
// ---------------------------------------------------------------------------
TEST_CASE("Interp:index with huge index returns undef", "[interp]") {
    REQUIRE(evalVal("[1,2,3][1e20]").isUndef());
}

TEST_CASE("Interp:str never leaks nan text with a sign glitch", "[interp]") {
    // sqrt(-1) is now undef (see domain-error fix), so str() sees Undef,
    // not a raw NaN payload.
    REQUIRE(evalVal("str(sqrt(-1))").asString() == "undef");
}

TEST_CASE("Interp:chr rejects huge/non-finite codepoints", "[interp]") {
    REQUIRE(evalVal("chr(1e20)").isUndef());
}
