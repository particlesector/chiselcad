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
