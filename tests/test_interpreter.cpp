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
