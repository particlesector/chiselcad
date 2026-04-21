#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "lang/Lexer.h"
#include "lang/Parser.h"
#include "csg/CsgEvaluator.h"

using namespace chisel::lang;
using namespace chisel::csg;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------
static ParseResult parse(std::string_view src) {
    Lexer  lexer(src);
    auto   tokens = lexer.tokenize();
    REQUIRE_FALSE(lexer.hasErrors());
    Parser parser(std::move(tokens));
    auto   result = parser.parse();
    REQUIRE_FALSE(parser.hasErrors());
    return result;
}

static CsgScene evaluate(std::string_view src) {
    CsgEvaluator ev;
    return ev.evaluate(parse(src));
}

static const CsgLeaf& asLeaf(const CsgNodePtr& n) {
    return std::get<CsgLeaf>(*n);
}
static const CsgBoolean& asBool(const CsgNodePtr& n) {
    return std::get<CsgBoolean>(*n);
}

// ---------------------------------------------------------------------------
// Global params forwarding
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:global $fn forwarded to scene", "[csg]") {
    auto s = evaluate("$fn = 48; cube([1,1,1]);");
    REQUIRE(s.globalFn == Approx(48.0));
    REQUIRE(s.roots.size() == 1);
}

// ---------------------------------------------------------------------------
// Primitives produce CsgLeaf nodes
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:cube produces CsgLeaf", "[csg]") {
    auto s = evaluate("cube([10, 20, 30]);");
    REQUIRE(s.roots.size() == 1);
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.kind == CsgLeaf::Kind::Cube);
    REQUIRE(leaf.params.at("x") == Approx(10.0));
    REQUIRE(leaf.params.at("y") == Approx(20.0));
    REQUIRE(leaf.params.at("z") == Approx(30.0));
    REQUIRE(leaf.center == false);
}

TEST_CASE("CsgEval:cube centered flag preserved", "[csg]") {
    auto s = evaluate("cube([5,5,5], center=true);");
    REQUIRE(asLeaf(s.roots[0]).center == true);
}

TEST_CASE("CsgEval:sphere produces CsgLeaf", "[csg]") {
    auto s = evaluate("sphere(r=7);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.kind == CsgLeaf::Kind::Sphere);
    REQUIRE(leaf.params.at("r") == Approx(7.0));
}

TEST_CASE("CsgEval:cylinder produces CsgLeaf", "[csg]") {
    auto s = evaluate("cylinder(h=12, r1=4, r2=1);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.kind == CsgLeaf::Kind::Cylinder);
    REQUIRE(leaf.params.at("h")  == Approx(12.0));
    REQUIRE(leaf.params.at("r1") == Approx(4.0));
    REQUIRE(leaf.params.at("r2") == Approx(1.0));
}

// ---------------------------------------------------------------------------
// Identity transform on un-transformed primitives
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:un-transformed leaf has identity matrix", "[csg]") {
    auto s = evaluate("sphere(r=3);");
    const auto& leaf = asLeaf(s.roots[0]);
    const glm::mat4 I{1.0f};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            REQUIRE(leaf.transform[c][r] == Approx(I[c][r]).margin(1e-5));
}

// ---------------------------------------------------------------------------
// Translate folds into the leaf transform
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:translate folds into leaf", "[csg]") {
    auto s = evaluate("translate([3, 7, -2]) sphere(r=1);");
    REQUIRE(s.roots.size() == 1);
    const auto& leaf = asLeaf(s.roots[0]);
    // Column 3 of the matrix is the translation vector
    REQUIRE(leaf.transform[3][0] == Approx(3.0f));
    REQUIRE(leaf.transform[3][1] == Approx(7.0f));
    REQUIRE(leaf.transform[3][2] == Approx(-2.0f));
    // Diagonal should be 1 (no scale)
    REQUIRE(leaf.transform[0][0] == Approx(1.0f));
    REQUIRE(leaf.transform[1][1] == Approx(1.0f));
    REQUIRE(leaf.transform[2][2] == Approx(1.0f));
}

// ---------------------------------------------------------------------------
// Nested translates accumulate
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:nested translates accumulate", "[csg]") {
    // translate([1,0,0]) translate([0,2,0]) cube → leaf at [1,2,0]
    auto s = evaluate("translate([1,0,0]) translate([0,2,0]) cube([5,5,5]);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.transform[3][0] == Approx(1.0f));
    REQUIRE(leaf.transform[3][1] == Approx(2.0f));
    REQUIRE(leaf.transform[3][2] == Approx(0.0f));
}

// ---------------------------------------------------------------------------
// Scale folds into the leaf transform
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:scale folds into leaf", "[csg]") {
    auto s = evaluate("scale([2, 3, 4]) cube([1,1,1]);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.transform[0][0] == Approx(2.0f));
    REQUIRE(leaf.transform[1][1] == Approx(3.0f));
    REQUIRE(leaf.transform[2][2] == Approx(4.0f));
}

// ---------------------------------------------------------------------------
// Mirror [1,0,0] reflects x — matrix should negate x column
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:mirror [1,0,0] negates x column", "[csg]") {
    auto s = evaluate("mirror([1, 0, 0]) cube([1,1,1]);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.transform[0][0] == Approx(-1.0f));
    REQUIRE(leaf.transform[1][1] == Approx( 1.0f));
    REQUIRE(leaf.transform[2][2] == Approx( 1.0f));
}

// ---------------------------------------------------------------------------
// Mirror [0,0,0] is identity (OpenSCAD edge case)
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:mirror [0,0,0] is identity", "[csg]") {
    auto s = evaluate("mirror([0, 0, 0]) sphere(r=1);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.transform[0][0] == Approx(1.0f));
    REQUIRE(leaf.transform[1][1] == Approx(1.0f));
    REQUIRE(leaf.transform[2][2] == Approx(1.0f));
}

// ---------------------------------------------------------------------------
// Booleans preserve tree structure
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:union produces CsgBoolean", "[csg]") {
    auto s = evaluate("union() { cube([5,5,5]); sphere(r=3); }");
    REQUIRE(s.roots.size() == 1);
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Union);
    REQUIRE(b.children.size() == 2);
    REQUIRE(asLeaf(b.children[0]).kind == CsgLeaf::Kind::Cube);
    REQUIRE(asLeaf(b.children[1]).kind == CsgLeaf::Kind::Sphere);
}

TEST_CASE("CsgEval:difference produces CsgBoolean", "[csg]") {
    auto s = evaluate("difference() { cube([10,10,10]); sphere(r=6); }");
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Difference);
    REQUIRE(b.children.size() == 2);
}

TEST_CASE("CsgEval:intersection produces CsgBoolean", "[csg]") {
    auto s = evaluate("intersection() { sphere(r=8); cube([12,12,12], center=true); }");
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Intersection);
}

TEST_CASE("CsgEval:empty union has no children", "[csg]") {
    auto s = evaluate("union() {};");
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.children.empty());
}

// ---------------------------------------------------------------------------
// Transform wrapping a boolean passes the matrix to all leaf descendants
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:translate wrapping union reaches all leaves", "[csg]") {
    auto s = evaluate(
        "translate([5, 0, 0])"
        "  union() { cube([1,1,1]); sphere(r=1); }"
    );
    // translate + union → CsgBoolean (union), children are leaves at x=5
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Union);
    REQUIRE(b.children.size() == 2);
    REQUIRE(asLeaf(b.children[0]).transform[3][0] == Approx(5.0f));
    REQUIRE(asLeaf(b.children[1]).transform[3][0] == Approx(5.0f));
}

// ---------------------------------------------------------------------------
// Multiple roots
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:multiple top-level roots", "[csg]") {
    auto s = evaluate("cube([1,1,1]); sphere(r=1);");
    REQUIRE(s.roots.size() == 2);
}

// ---------------------------------------------------------------------------
// hull() and minkowski()
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:hull produces CsgBoolean", "[csg]") {
    auto s = evaluate("hull() { sphere(r=1); cube([2,2,2]); }");
    REQUIRE(s.roots.size() == 1);
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Hull);
    REQUIRE(b.children.size() == 2);
}

TEST_CASE("CsgEval:minkowski produces CsgBoolean", "[csg]") {
    auto s = evaluate("minkowski() { cube([10,10,10]); sphere(r=1); }");
    REQUIRE(s.roots.size() == 1);
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Minkowski);
    REQUIRE(b.children.size() == 2);
}

TEST_CASE("CsgEval:hull with transform wrapper", "[csg]") {
    auto s = evaluate("translate([5,0,0]) hull() { sphere(r=1); cube([2,2,2]); }");
    REQUIRE(s.roots.size() == 1);
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Hull);
    REQUIRE(b.children.size() == 2);
}
