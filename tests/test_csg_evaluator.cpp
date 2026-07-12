#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "lang/Lexer.h"
#include "lang/Parser.h"
#include "csg/CsgEvaluator.h"
#include <filesystem>

using namespace chisel::lang;
using namespace chisel::csg;
using Catch::Approx;

#ifndef CHISELCAD_TEST_FIXTURE_DIR
#  error "CHISELCAD_TEST_FIXTURE_DIR must be defined by the build (see CMakeLists.txt)"
#endif

static std::filesystem::path fixture(const std::string& relPath) {
    return std::filesystem::path(CHISELCAD_TEST_FIXTURE_DIR) / relPath;
}

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

static CsgScene evaluateWithBaseDir(std::string_view src, const std::filesystem::path& baseDir) {
    CsgEvaluator ev;
    ev.baseDir = baseDir;
    return ev.evaluate(parse(src));
}

static const CsgLeaf& asLeaf(const CsgNodePtr& n) {
    return std::get<CsgLeaf>(*n);
}
static const CsgBoolean& asBool(const CsgNodePtr& n) {
    return std::get<CsgBoolean>(*n);
}
static const CsgOffset& asOffset(const CsgNodePtr& n) {
    return std::get<CsgOffset>(*n);
}
static const CsgProjection& asProjection(const CsgNodePtr& n) {
    return std::get<CsgProjection>(*n);
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
// cube()/cylinder()/sphere() argument-form handling (issue #31)
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:cube bare scalar shorthand", "[csg]") {
    auto s = evaluate("cube(5);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.params.at("x") == Approx(5.0));
    REQUIRE(leaf.params.at("y") == Approx(5.0));
    REQUIRE(leaf.params.at("z") == Approx(5.0));
}

TEST_CASE("CsgEval:cube named size vector", "[csg]") {
    auto s = evaluate("cube(size=[10,20,30]);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.params.at("x") == Approx(10.0));
    REQUIRE(leaf.params.at("y") == Approx(20.0));
    REQUIRE(leaf.params.at("z") == Approx(30.0));
}

TEST_CASE("CsgEval:cube named size scalar", "[csg]") {
    auto s = evaluate("cube(size=4);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.params.at("x") == Approx(4.0));
    REQUIRE(leaf.params.at("y") == Approx(4.0));
    REQUIRE(leaf.params.at("z") == Approx(4.0));
}

TEST_CASE("CsgEval:cube positional vector variable", "[csg]") {
    auto s = evaluate("v = [1,2,3]; cube(v);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.params.at("x") == Approx(1.0));
    REQUIRE(leaf.params.at("y") == Approx(2.0));
    REQUIRE(leaf.params.at("z") == Approx(3.0));
}

TEST_CASE("CsgEval:cylinder bare positional height", "[csg]") {
    auto s = evaluate("cylinder(10);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.params.at("h") == Approx(10.0));
}

TEST_CASE("CsgEval:cylinder positional height and radius", "[csg]") {
    auto s = evaluate("cylinder(10, 5);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.params.at("h") == Approx(10.0));
    REQUIRE(leaf.params.at("r") == Approx(5.0));
}

TEST_CASE("CsgEval:cylinder diameter forms", "[csg]") {
    auto s1 = evaluate("cylinder(h=10, d=6);");
    REQUIRE(asLeaf(s1.roots[0]).params.at("r") == Approx(3.0));

    auto s2 = evaluate("cylinder(h=10, d1=8, d2=2);");
    const auto& leaf2 = asLeaf(s2.roots[0]);
    REQUIRE(leaf2.params.at("r1") == Approx(4.0));
    REQUIRE(leaf2.params.at("r2") == Approx(1.0));
}

TEST_CASE("CsgEval:sphere diameter form", "[csg]") {
    auto s = evaluate("sphere(d=10);");
    REQUIRE(asLeaf(s.roots[0]).params.at("r") == Approx(5.0));
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
// multmatrix() folds the given 4x4 rows straight into the leaf transform
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:multmatrix translation column", "[csg]") {
    auto s = evaluate(
        "multmatrix([[1,0,0,5],[0,1,0,7],[0,0,1,-2],[0,0,0,1]]) cube([1,1,1]);"
    );
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.transform[3][0] == Approx(5.0f));
    REQUIRE(leaf.transform[3][1] == Approx(7.0f));
    REQUIRE(leaf.transform[3][2] == Approx(-2.0f));
    REQUIRE(leaf.transform[0][0] == Approx(1.0f));
    REQUIRE(leaf.transform[1][1] == Approx(1.0f));
    REQUIRE(leaf.transform[2][2] == Approx(1.0f));
}

TEST_CASE("CsgEval:multmatrix arbitrary linear map", "[csg]") {
    // Row 0 = [2,0,0,0] doubles x; row 1 = [0,0,1,0] swaps y<-z's basis vector.
    auto s = evaluate(
        "multmatrix([[2,0,0,0],[0,0,1,0],[0,1,0,0],[0,0,0,1]]) sphere(r=1);"
    );
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.transform[0][0] == Approx(2.0f)); // column 0, row 0
    REQUIRE(leaf.transform[1][2] == Approx(1.0f)); // column 1, row 2
    REQUIRE(leaf.transform[2][1] == Approx(1.0f)); // column 2, row 1
}

TEST_CASE("CsgEval:multmatrix composes with outer translate", "[csg]") {
    auto s = evaluate(
        "translate([10,0,0]) multmatrix([[1,0,0,1],[0,1,0,0],[0,0,1,0],[0,0,0,1]]) cube([1,1,1]);"
    );
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.transform[3][0] == Approx(11.0f));
}

// ---------------------------------------------------------------------------
// render() groups children under an implicit union, no transform of its own
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:render single child passes through untouched", "[csg]") {
    auto s = evaluate("render() cube([2,2,2]);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.kind == CsgLeaf::Kind::Cube);
    REQUIRE(leaf.transform[0][0] == Approx(1.0f));
}

TEST_CASE("CsgEval:render multiple children implicitly unions", "[csg]") {
    auto s = evaluate("render() { cube([2,2,2]); sphere(r=1); }");
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Union);
    REQUIRE(b.children.size() == 2);
}

TEST_CASE("CsgEval:render(convexity=...) ignores the hint", "[csg]") {
    auto s = evaluate("render(convexity = 6) sphere(r=3);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.kind == CsgLeaf::Kind::Sphere);
}

// ---------------------------------------------------------------------------
// color() sets an inherited tint, overridable by a nested color()
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:color by name tints a leaf", "[csg]") {
    auto s = evaluate("color(\"red\") cube([1,1,1]);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.color.has);
    REQUIRE(leaf.color.value.r == Approx(1.0f));
    REQUIRE(leaf.color.value.g == Approx(0.0f));
    REQUIRE(leaf.color.value.b == Approx(0.0f));
    REQUIRE(leaf.color.value.a == Approx(1.0f));
}

TEST_CASE("CsgEval:color by hex string", "[csg]") {
    auto s = evaluate("color(\"#00ff00\") sphere(r=1);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.color.has);
    REQUIRE(leaf.color.value.r == Approx(0.0f));
    REQUIRE(leaf.color.value.g == Approx(1.0f));
    REQUIRE(leaf.color.value.b == Approx(0.0f));
}

TEST_CASE("CsgEval:color by vector with alpha component", "[csg]") {
    auto s = evaluate("color([0,0,1,0.25]) cube([1,1,1]);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.color.has);
    REQUIRE(leaf.color.value.b == Approx(1.0f));
    REQUIRE(leaf.color.value.a == Approx(0.25f));
}

TEST_CASE("CsgEval:color positional alpha overrides vector alpha", "[csg]") {
    auto s = evaluate("color([1,1,1,1], 0.5) cube([1,1,1]);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.color.value.a == Approx(0.5f));
}

TEST_CASE("CsgEval:color propagates through nested transforms", "[csg]") {
    auto s = evaluate("color(\"blue\") translate([1,0,0]) cube([1,1,1]);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.color.has);
    REQUIRE(leaf.color.value.b == Approx(1.0f));
    REQUIRE(leaf.transform[3][0] == Approx(1.0f));
}

TEST_CASE("CsgEval:color propagates to every child of a group", "[csg]") {
    auto s = evaluate("color(\"green\") { cube([1,1,1]); sphere(r=1); }");
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Union);
    REQUIRE(b.color.has);
    for (const auto& child : b.children) {
        const auto& leaf = asLeaf(child);
        REQUIRE(leaf.color.has);
        REQUIRE(leaf.color.value.g == Approx(0.5f));
    }
}

TEST_CASE("CsgEval:nested color overrides its own subtree only", "[csg]") {
    auto s = evaluate("color(\"red\") { cube([1,1,1]); color(\"blue\") sphere(r=1); }");
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.children.size() == 2);
    const auto& cubeLeaf = asLeaf(b.children[0]);
    REQUIRE(cubeLeaf.color.value.r == Approx(1.0f));
    const auto& sphereLeaf = asLeaf(b.children[1]);
    REQUIRE(sphereLeaf.color.value.b == Approx(1.0f));
    REQUIRE(sphereLeaf.color.value.r == Approx(0.0f));
}

TEST_CASE("CsgEval:no color() leaves color unset", "[csg]") {
    auto s = evaluate("cube([1,1,1]);");
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE_FALSE(leaf.color.has);
}

// ---------------------------------------------------------------------------
// offset() resolves r/delta/chamfer to doubles and evaluates children
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:offset with rounded radius", "[csg]") {
    auto s = evaluate("offset(r=2) circle(r=5);");
    const auto& o = asOffset(s.roots[0]);
    REQUIRE(o.params.at("r") == Approx(2.0));
    REQUIRE(o.params.count("delta") == 0);
    REQUIRE(o.children.size() == 1);
    const auto& child = asLeaf(o.children[0]);
    REQUIRE(child.kind == CsgLeaf::Kind::Circle2D);
}

TEST_CASE("CsgEval:offset with delta and chamfer resolves chamfer to 0/1", "[csg]") {
    auto s = evaluate("offset(delta=1, chamfer=true) square([10,10]);");
    const auto& o = asOffset(s.roots[0]);
    REQUIRE(o.params.at("delta")   == Approx(1.0));
    REQUIRE(o.params.at("chamfer") == Approx(1.0));
}

TEST_CASE("CsgEval:offset children evaluated in local space, transform stored outer", "[csg]") {
    auto s = evaluate("translate([10,0,0]) offset(r=1) circle(r=5);");
    const auto& o = asOffset(s.roots[0]);
    // Outer translate is stored on the offset node itself...
    REQUIRE(o.transform[3][0] == Approx(10.0f));
    // ...not baked into the child leaf's transform.
    const auto& child = asLeaf(o.children[0]);
    REQUIRE(child.transform[3][0] == Approx(0.0f));
}

TEST_CASE("CsgEval:offset wraps multiple children into a union", "[csg]") {
    auto s = evaluate("offset(r=-1) { square([5,5]); circle(r=3); }");
    const auto& o = asOffset(s.roots[0]);
    REQUIRE(o.params.at("r") == Approx(-1.0));
    REQUIRE(o.children.size() == 2);
}

TEST_CASE("CsgEval:offset propagates inherited color to children", "[csg]") {
    auto s = evaluate("color(\"red\") offset(r=1) circle(r=5);");
    const auto& o = asOffset(s.roots[0]);
    REQUIRE(o.color.has);
    REQUIRE(o.color.value.r == Approx(1.0f));
    const auto& child = asLeaf(o.children[0]);
    REQUIRE(child.color.has);
    REQUIRE(child.color.value.r == Approx(1.0f));
}

// ---------------------------------------------------------------------------
// projection() resolves "cut" and evaluates 3-D children in local space
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:projection defaults cut to false", "[csg]") {
    auto s = evaluate("projection() cube([2,2,2]);");
    const auto& p = asProjection(s.roots[0]);
    REQUIRE_FALSE(p.cut);
    REQUIRE(p.children.size() == 1);
    const auto& child = asLeaf(p.children[0]);
    REQUIRE(child.kind == CsgLeaf::Kind::Cube);
}

TEST_CASE("CsgEval:projection with cut=true resolves cut to true", "[csg]") {
    auto s = evaluate("projection(cut = true) sphere(r=5);");
    const auto& p = asProjection(s.roots[0]);
    REQUIRE(p.cut);
}

TEST_CASE("CsgEval:projection children evaluated in local space, transform stored outer", "[csg]") {
    auto s = evaluate("translate([10,0,0]) projection() sphere(r=5);");
    const auto& p = asProjection(s.roots[0]);
    // Outer translate is stored on the projection node itself...
    REQUIRE(p.transform[3][0] == Approx(10.0f));
    // ...not baked into the 3-D child leaf's transform.
    const auto& child = asLeaf(p.children[0]);
    REQUIRE(child.transform[3][0] == Approx(0.0f));
}

TEST_CASE("CsgEval:projection wraps multiple children", "[csg]") {
    auto s = evaluate("projection() { cube([2,2,2]); sphere(r=1); }");
    const auto& p = asProjection(s.roots[0]);
    REQUIRE(p.children.size() == 2);
}

TEST_CASE("CsgEval:projection propagates inherited color to children", "[csg]") {
    auto s = evaluate("color(\"blue\") projection() cube([2,2,2]);");
    const auto& p = asProjection(s.roots[0]);
    REQUIRE(p.color.has);
    REQUIRE(p.color.value.b == Approx(1.0f));
    const auto& child = asLeaf(p.children[0]);
    REQUIRE(child.color.has);
    REQUIRE(child.color.value.b == Approx(1.0f));
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

TEST_CASE("CsgEval:hull stores outer transform", "[csg]") {
    auto s = evaluate("translate([5,0,0]) hull() { sphere(r=1); cube([2,2,2]); }");
    REQUIRE(s.roots.size() == 1);
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Hull);
    REQUIRE(b.children.size() == 2);
    // Outer translate must be on the node, not baked into children
    REQUIRE(b.transform[3][0] == Approx(5.0f));
    const auto& child = asLeaf(b.children[0]);
    REQUIRE(child.transform[3][0] == Approx(0.0f)); // local space — no x offset
}

TEST_CASE("CsgEval:minkowski stores outer transform", "[csg]") {
    auto s = evaluate("translate([0,-35,0]) minkowski() { cube([10,10,10]); sphere(r=1); }");
    REQUIRE(s.roots.size() == 1);
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Minkowski);
    // Outer translate on node, children at origin
    REQUIRE(b.transform[3][1] == Approx(-35.0f));
    const auto& child = asLeaf(b.children[0]);
    REQUIRE(child.transform[3][1] == Approx(0.0f)); // local space — no y offset
}

// ---------------------------------------------------------------------------
// if / else
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:if true yields then geometry", "[csg]") {
    auto s = evaluate("if (1) sphere(r=5);");
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Sphere);
}

TEST_CASE("CsgEval:if false yields no geometry", "[csg]") {
    auto s = evaluate("if (0) sphere(r=5);");
    REQUIRE(s.roots.empty());
}

TEST_CASE("CsgEval:if false else yields else geometry", "[csg]") {
    auto s = evaluate("if (0) sphere(r=5); else cube([3,3,3]);");
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Cube);
}

TEST_CASE("CsgEval:if true else skips else geometry", "[csg]") {
    auto s = evaluate("if (1) sphere(r=5); else cube([3,3,3]);");
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Sphere);
}

TEST_CASE("CsgEval:if condition from expression", "[csg]") {
    auto s = evaluate("if (3 > 2) sphere(r=4);");
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Sphere);
}

TEST_CASE("CsgEval:if condition from variable", "[csg]") {
    auto s = evaluate("show = 1; if (show) cube([5,5,5]);");
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Cube);
}

TEST_CASE("CsgEval:if multiple then children wrapped in union", "[csg]") {
    auto s = evaluate("if (1) { sphere(r=1); cube([2,2,2]); }");
    REQUIRE(s.roots.size() == 1);
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Union);
    REQUIRE(b.children.size() == 2);
}

TEST_CASE("CsgEval:if inherits outer transform", "[csg]") {
    auto s = evaluate("translate([7,0,0]) if (1) sphere(r=1);");
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).transform[3][0] == Approx(7.0f));
}

// ---------------------------------------------------------------------------
// for loops
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:for range produces one child per step", "[csg]") {
    // [0:2] → i=0,1,2 — three spheres wrapped in a union
    auto s = evaluate("for (i = [0:2]) sphere(r=1);");
    REQUIRE(s.roots.size() == 1);
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Union);
    REQUIRE(b.children.size() == 3);
}

TEST_CASE("CsgEval:for range with step", "[csg]") {
    // [0:2:6] → i=0,2,4,6 — four children
    auto s = evaluate("for (i = [0:2:6]) sphere(r=1);");
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.children.size() == 4);
}

TEST_CASE("CsgEval:for list produces one child per value", "[csg]") {
    auto s = evaluate("for (v = [1, 5, 9]) cube([v, v, v]);");
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Union);
    REQUIRE(b.children.size() == 3);
}

TEST_CASE("CsgEval:for single iteration returns leaf directly", "[csg]") {
    // [3:3] → i=3 only — no wrapping union needed
    auto s = evaluate("for (i = [3:3]) sphere(r=2);");
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Sphere);
}

TEST_CASE("CsgEval:for uses loop variable in child", "[csg]") {
    // translate([i,0,0]) — each sphere should sit at x = i
    auto s = evaluate("for (i = [0:2]) translate([i*5, 0, 0]) sphere(r=1);");
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.children.size() == 3);
    REQUIRE(asLeaf(b.children[0]).transform[3][0] == Approx(0.0f));
    REQUIRE(asLeaf(b.children[1]).transform[3][0] == Approx(5.0f));
    REQUIRE(asLeaf(b.children[2]).transform[3][0] == Approx(10.0f));
}

TEST_CASE("CsgEval:for restores outer variable after loop", "[csg]") {
    // 'i' is set before the loop; the for loop should restore it afterward
    auto s = evaluate("i = 99; for (i = [0:1]) sphere(r=1); cube([i,i,i]);");
    // cube gets i=99 (restored), for produces 2 spheres
    REQUIRE(s.roots.size() == 2);
    REQUIRE(asLeaf(s.roots[1]).params.at("x") == Approx(99.0));
}

TEST_CASE("CsgEval:for empty range yields no geometry", "[csg]") {
    // [5:3] with positive step — no iterations
    auto s = evaluate("for (i = [5:3]) sphere(r=1);");
    REQUIRE(s.roots.empty());
}

TEST_CASE("CsgEval:for over bracketed point-list literal iterates per-point, not flattened", "[csg]") {
    // Regression test: a bracketed list literal whose own elements are
    // vectors — the common point-list idiom — must yield one iteration per
    // point (2), not one per flattened scalar (6).
    auto s = evaluate("for (p = [[1,2,3], [4,5,6]]) translate(p) sphere(r=1);");
    REQUIRE(s.roots.size() == 1);
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.children.size() == 2);
    REQUIRE(asLeaf(b.children[0]).transform[3][0] == Approx(1.0f));
    REQUIRE(asLeaf(b.children[0]).transform[3][1] == Approx(2.0f));
    REQUIRE(asLeaf(b.children[0]).transform[3][2] == Approx(3.0f));
    REQUIRE(asLeaf(b.children[1]).transform[3][0] == Approx(4.0f));
    REQUIRE(asLeaf(b.children[1]).transform[3][1] == Approx(5.0f));
    REQUIRE(asLeaf(b.children[1]).transform[3][2] == Approx(6.0f));
}

TEST_CASE("CsgEval:for over variable holding a point list still expands per-point", "[csg]") {
    // Same point-list idiom via a variable (expression form) — must keep
    // working the same way it did before the bracketed-list fix.
    auto s = evaluate("pts = [[1,2,3], [4,5,6]]; for (p = pts) translate(p) sphere(r=1);");
    REQUIRE(s.roots.size() == 1);
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.children.size() == 2);
    REQUIRE(asLeaf(b.children[1]).transform[3][0] == Approx(4.0f));
}

// ---------------------------------------------------------------------------
// User-defined modules
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:simple module call produces geometry", "[csg]") {
    auto s = evaluate(
        "module ball(r) { sphere(r=r); }"
        "ball(5);"
    );
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Sphere);
    REQUIRE(asLeaf(s.roots[0]).params.at("r") == Approx(5.0));
}

TEST_CASE("CsgEval:module call with named args", "[csg]") {
    auto s = evaluate(
        "module pill(r, h) { cylinder(r=r, h=h); }"
        "pill(h=10, r=3);"
    );
    REQUIRE(s.roots.size() == 1);
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.kind == CsgLeaf::Kind::Cylinder);
    REQUIRE(leaf.params.at("r") == Approx(3.0));
    REQUIRE(leaf.params.at("h") == Approx(10.0));
}

TEST_CASE("CsgEval:module call with default param", "[csg]") {
    auto s = evaluate(
        "module disk(r, h = 2) { cylinder(r=r, h=h); }"
        "disk(r=6);"
    );
    REQUIRE(s.roots.size() == 1);
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.params.at("r") == Approx(6.0));
    REQUIRE(leaf.params.at("h") == Approx(2.0));
}

TEST_CASE("CsgEval:module with multi-primitive body wraps in union", "[csg]") {
    auto s = evaluate(
        "module combo() { sphere(r=1); cube([2,2,2]); }"
        "combo();"
    );
    REQUIRE(s.roots.size() == 1);
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.op == CsgBoolean::Op::Union);
    REQUIRE(b.children.size() == 2);
}

TEST_CASE("CsgEval:module call restores caller env", "[csg]") {
    // 'r' exists in caller scope; module should not clobber it
    auto s = evaluate(
        "module ball(r) { sphere(r=r); }"
        "r = 99;"
        "ball(3);"
        "sphere(r=r);"
    );
    REQUIRE(s.roots.size() == 2);
    REQUIRE(asLeaf(s.roots[0]).params.at("r") == Approx(3.0));
    REQUIRE(asLeaf(s.roots[1]).params.at("r") == Approx(99.0));
}

TEST_CASE("CsgEval:unbound module parameter is undef, not the caller's/global same-named variable", "[csg][bugfix]") {
    auto s = evaluate(
        "r = 42;"
        "module ball(r) { sphere(r=r); }"
        "ball();"
    );
    REQUIRE(s.roots.size() == 1);
    // undef coerces to 0, not the enclosing global r = 42.
    REQUIRE(asLeaf(s.roots[0]).params.at("r") == Approx(0.0));
}

TEST_CASE("CsgEval:children() evaluates caller-authored geometry in the caller's scope, not the callee's params", "[csg][bugfix]") {
    auto s = evaluate(
        "r = 100;"
        "module container(r) { children(); }"
        "container(5) sphere(r=r);"
    );
    REQUIRE(s.roots.size() == 1);
    // sphere(r=r) was written at the call site, so r must resolve to the
    // caller's r = 100, not container's own parameter r = 5.
    REQUIRE(asLeaf(s.roots[0]).params.at("r") == Approx(100.0));
}

TEST_CASE("CsgEval:undefined module call yields no geometry", "[csg]") {
    // unknown_module() is not defined — should silently produce nothing
    Lexer  lexer("unknown_module(5);");
    auto   tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto   result = parser.parse();
    // Parser produces a ModuleCallNode (structural), no errors
    CsgEvaluator ev;
    auto s = ev.evaluate(result);
    REQUIRE(s.roots.empty());
}

TEST_CASE("CsgEval:module call inherits outer transform", "[csg]") {
    auto s = evaluate(
        "module dot() { sphere(r=1); }"
        "translate([4, 0, 0]) dot();"
    );
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).transform[3][0] == Approx(4.0f));
}

TEST_CASE("CsgEval:module called multiple times", "[csg]") {
    auto s = evaluate(
        "module dot(r) { sphere(r=r); }"
        "dot(1); dot(2); dot(3);"
    );
    REQUIRE(s.roots.size() == 3);
    REQUIRE(asLeaf(s.roots[0]).params.at("r") == Approx(1.0));
    REQUIRE(asLeaf(s.roots[1]).params.at("r") == Approx(2.0));
    REQUIRE(asLeaf(s.roots[2]).params.at("r") == Approx(3.0));
}

// ===========================================================================
// Tier C — Module System Completeness
// ===========================================================================

// ---------------------------------------------------------------------------
// echo()
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:echo captures message", "[csg][tier-c]") {
    auto s = evaluate("echo(\"hello\");");
    REQUIRE(s.echoMessages.size() == 1);
    REQUIRE(s.echoMessages[0].find("hello") != std::string::npos);
}

TEST_CASE("CsgEval:echo multiple args", "[csg][tier-c]") {
    auto s = evaluate("echo(\"x=\", 42);");
    REQUIRE(s.echoMessages.size() == 1);
    REQUIRE(s.echoMessages[0].find("42") != std::string::npos);
}

TEST_CASE("CsgEval:echo formats number", "[csg][tier-c]") {
    auto s = evaluate("echo(3.14);");
    REQUIRE(!s.echoMessages.empty());
    REQUIRE(s.echoMessages[0].find("3.14") != std::string::npos);
}

TEST_CASE("CsgEval:echo in for loop", "[csg][tier-c]") {
    auto s = evaluate("for (i = [1:3]) echo(i);");
    REQUIRE(s.echoMessages.size() == 3);
}

TEST_CASE("CsgEval:echo does not produce geometry", "[csg][tier-c]") {
    auto s = evaluate("echo(\"no geo\"); cube([1,1,1]);");
    REQUIRE(s.roots.size() == 1); // only the cube
    REQUIRE(s.echoMessages.size() == 1);
}

// ---------------------------------------------------------------------------
// assert()
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:assert pass produces no error", "[csg][tier-c]") {
    auto s = evaluate("assert(1 > 0); cube([1,1,1]);");
    REQUIRE(s.evalDiags.empty());
    REQUIRE(s.roots.size() == 1);
}

TEST_CASE("CsgEval:assert fail records error", "[csg][tier-c]") {
    auto s = evaluate("assert(1 < 0);");
    REQUIRE(!s.evalDiags.empty());
    REQUIRE(s.evalDiags[0].level == chisel::lang::DiagLevel::Error);
}

TEST_CASE("CsgEval:assert fail with message", "[csg][tier-c]") {
    auto s = evaluate("assert(false, \"bad value\");");
    REQUIRE(!s.evalDiags.empty());
    REQUIRE(s.evalDiags[0].message.find("bad value") != std::string::npos);
}

TEST_CASE("CsgEval:assert pass inside module", "[csg][tier-c]") {
    auto s = evaluate(
        "module sc(n) { assert(n > 0); cube([n, n, n]); }"
        "sc(5);"
    );
    REQUIRE(s.evalDiags.empty());
    REQUIRE(s.roots.size() == 1);
}

TEST_CASE("CsgEval:assert fail inside module", "[csg][tier-c]") {
    auto s = evaluate(
        "module sc(n) { assert(n > 0, \"n must be positive\"); }"
        "sc(-1);"
    );
    REQUIRE(!s.evalDiags.empty());
    REQUIRE(s.evalDiags[0].message.find("n must be positive") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Module recursion-depth guard (#30) — a module with no base case (or mutual
// recursion) must not overflow the native call stack.
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:unbounded module recursion is capped, not a stack-overflow crash", "[csg][bugfix]") {
    auto s = evaluate(
        "module r() { r(); }"
        "r();"
    );
    REQUIRE_FALSE(s.evalDiags.empty());
    REQUIRE(s.evalDiags[0].level == chisel::lang::DiagLevel::Error);
}

TEST_CASE("CsgEval:mutually recursive modules with no base case are capped", "[csg][bugfix]") {
    auto s = evaluate(
        "module a() { b(); }"
        "module b() { a(); }"
        "a();"
    );
    REQUIRE_FALSE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:legitimate bounded module recursion still produces geometry", "[csg][bugfix]") {
    // Depth well under the cap; the guard must not interfere with normal use.
    auto s = evaluate(
        "module nest(n) { if (n > 0) { translate([1,0,0]) nest(n-1); } else { cube(1); } }"
        "nest(50);"
    );
    REQUIRE(s.evalDiags.empty());
    REQUIRE(s.roots.size() == 1);
}

// ---------------------------------------------------------------------------
// children() / $children
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:children basic passthrough", "[csg][tier-c]") {
    auto s = evaluate(
        "module wrap() { children(); }"
        "wrap() cube([5,5,5]);"
    );
    REQUIRE(s.roots.size() == 1);
    REQUIRE(std::holds_alternative<CsgLeaf>(*s.roots[0]));
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Cube);
}

TEST_CASE("CsgEval:children with transform", "[csg][tier-c]") {
    auto s = evaluate(
        "module lifted() { translate([0,0,10]) children(); }"
        "lifted() cube([1,1,1]);"
    );
    REQUIRE(s.roots.size() == 1);
    // The cube should have a z-translation of 10
    REQUIRE(asLeaf(s.roots[0]).transform[3][2] == Approx(10.0f));
}

TEST_CASE("CsgEval:children multiple", "[csg][tier-c]") {
    auto s = evaluate(
        "module wrap() { children(); }"
        "wrap() { sphere(r=1); cube([2,2,2]); }"
    );
    // Both children unioned into one boolean, so we get one root (a boolean)
    REQUIRE(s.roots.size() == 1);
    REQUIRE(std::holds_alternative<CsgBoolean>(*s.roots[0]));
    REQUIRE(asBool(s.roots[0]).children.size() == 2);
}

TEST_CASE("CsgEval:children indexed", "[csg][tier-c]") {
    auto s = evaluate(
        "module first_only() { children(0); }"
        "first_only() { sphere(r=3); cube([1,1,1]); }"
    );
    REQUIRE(s.roots.size() == 1);
    REQUIRE(std::holds_alternative<CsgLeaf>(*s.roots[0]));
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Sphere);
}

TEST_CASE("CsgEval:children index out of range returns nothing", "[csg][tier-c]") {
    auto s = evaluate(
        "module bad() { children(99); }"
        "bad() cube([1,1,1]);"
    );
    REQUIRE(s.roots.empty());
}

TEST_CASE("CsgEval:children outside module returns nothing", "[csg][tier-c]") {
    // children() called at top level — not inside a module body
    auto s = evaluate("children();");
    REQUIRE(s.roots.empty());
}

TEST_CASE("CsgEval:children repeated in for loop", "[csg][tier-c]") {
    auto s = evaluate(
        "module repeat3() {"
        "    for (i = [0:2])"
        "        translate([i * 10, 0, 0]) children();"
        "}"
        "repeat3() sphere(r=2);"
    );
    // 3 translated copies of sphere, unioned by for → one CsgBoolean with 3 children
    REQUIRE(s.roots.size() == 1);
    const auto& b = asBool(s.roots[0]);
    REQUIRE(b.children.size() == 3);
}

TEST_CASE("CsgEval:$children count is correct", "[csg][tier-c]") {
    // Module uses $children to decide geometry
    auto s = evaluate(
        "module maybe(n) {"
        "    if ($children == n) { cube([1,1,1]); }"
        "}"
        "maybe(2) { sphere(r=1); sphere(r=2); }"
    );
    // $children == 2 is true, so cube is produced
    REQUIRE(s.roots.size() == 1);
    REQUIRE(std::holds_alternative<CsgLeaf>(*s.roots[0]));
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Cube);
}

TEST_CASE("CsgEval:$children zero when no children", "[csg][tier-c]") {
    auto s = evaluate(
        "module maybe() {"
        "    if ($children == 0) { cube([1,1,1]); }"
        "}"
        "maybe();"
    );
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Cube);
}

// ---------------------------------------------------------------------------
// Recursive functions (Tier C — confirmed already working via Tier A impl)
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:recursive function drives geometry", "[csg][tier-c]") {
    auto s = evaluate(
        "function fib(n) = n <= 1 ? n : fib(n-1) + fib(n-2);"
        "sphere(r = fib(6));"  // fib(6) = 8
    );
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).params.at("r") == Approx(8.0));
}

// ---------------------------------------------------------------------------
// Tier E: import()
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:import() produces a Mesh leaf from an absolute path", "[csg][tier-e]") {
    std::string src = "import(\"" + fixture("import/triangle.stl").string() + "\");";
    auto s = evaluate(src);
    REQUIRE(s.roots.size() == 1);
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.kind == CsgLeaf::Kind::Mesh);
    REQUIRE(leaf.meshPositions.size() == 3);
    REQUIRE(leaf.meshIndices.size() == 3);
    REQUIRE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:import() resolves a relative path against baseDir", "[csg][tier-e]") {
    auto s = evaluateWithBaseDir("import(\"triangle.stl\");", fixture("import"));
    REQUIRE(s.roots.size() == 1);
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.kind == CsgLeaf::Kind::Mesh);
    REQUIRE(leaf.meshPositions.size() == 3);
    REQUIRE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:import() honors an outer transform and color", "[csg][tier-e]") {
    std::string src = "color(\"red\") translate([1,2,3]) import(\"" +
                       fixture("import/triangle.stl").string() + "\");";
    auto s = evaluate(src);
    REQUIRE(s.roots.size() == 1);
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.kind == CsgLeaf::Kind::Mesh);
    REQUIRE(leaf.color.has);
    REQUIRE(leaf.transform[3][0] == Approx(1.0)); // translation column
    REQUIRE(leaf.transform[3][1] == Approx(2.0));
    REQUIRE(leaf.transform[3][2] == Approx(3.0));
}

TEST_CASE("CsgEval:import() with file= named argument", "[csg][tier-e]") {
    std::string src = "import(file=\"" + fixture("import/triangle.stl").string() + "\");";
    auto s = evaluate(src);
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Mesh);
}

TEST_CASE("CsgEval:import() missing file reports a diagnostic, not a crash", "[csg][tier-e]") {
    auto s = evaluate("import(\"does_not_exist.stl\");");
    REQUIRE(s.roots.empty());
    REQUIRE_FALSE(s.evalDiags.empty());
    REQUIRE(s.evalDiags[0].level == chisel::lang::DiagLevel::Error);
}

TEST_CASE("CsgEval:import() unsupported format reports a diagnostic", "[csg][tier-e]") {
    auto s = evaluate("import(\"model.obj\");");
    REQUIRE(s.roots.empty());
    REQUIRE_FALSE(s.evalDiags.empty());
    REQUIRE(s.evalDiags[0].message.find("unsupported") != std::string::npos);
}

TEST_CASE("CsgEval:import() with no arguments reports a diagnostic", "[csg][tier-e]") {
    auto s = evaluate("import();");
    REQUIRE(s.roots.empty());
    REQUIRE_FALSE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:import() recognizes a file literally named '.stl'", "[csg][tier-e]") {
    // std::filesystem::path::extension() treats ".stl" (leading dot, no
    // other dot) as having *no* extension (the "dotfile" rule) — the
    // matching in evalImport() must not rely on extension() alone.
    std::string src = "import(\"" + fixture("import/.stl").string() + "\");";
    auto s = evaluate(src);
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Mesh);
    REQUIRE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:import() positional argument wins over file= regardless of order", "[csg][tier-e]") {
    const std::string realPath = fixture("import/triangle.stl").string();

    // Named argument appears first in the source, positional second — the
    // positional must still win (order-independent precedence).
    auto s1 = evaluate("import(file=\"does_not_exist.stl\", \"" + realPath + "\");");
    REQUIRE(s1.roots.size() == 1);
    REQUIRE(s1.evalDiags.empty());

    // Positional first, named second — positional wins here too.
    auto s2 = evaluate("import(\"" + realPath + "\", file=\"does_not_exist.stl\");");
    REQUIRE(s2.roots.size() == 1);
    REQUIRE(s2.evalDiags.empty());
}

// ---------------------------------------------------------------------------
// Tier E: surface()
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:surface() produces a Mesh leaf from an absolute path", "[csg][tier-e]") {
    std::string src = "surface(\"" + fixture("surface/simple.dat").string() + "\");";
    auto s = evaluate(src);
    REQUIRE(s.roots.size() == 1);
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.kind == CsgLeaf::Kind::Mesh);
    REQUIRE(leaf.meshPositions.size() == 18);
    REQUIRE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:surface() resolves a relative path against baseDir", "[csg][tier-e]") {
    auto s = evaluateWithBaseDir("surface(\"simple.dat\");", fixture("surface"));
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Mesh);
    REQUIRE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:surface() with file=, center=, and invert= named arguments", "[csg][tier-e]") {
    std::string src = "surface(file=\"" + fixture("surface/simple.dat").string() +
                       "\", center=true, invert=true);";
    auto s = evaluate(src);
    REQUIRE(s.roots.size() == 1);
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.kind == CsgLeaf::Kind::Mesh);
    // Center cell (grid index 4): was the peak (z=5), inverted -> 0, then
    // centered around the [0,5] span -> shifted by -2.5.
    REQUIRE(leaf.meshPositions[4].z == Approx(-2.5));
}

TEST_CASE("CsgEval:surface() honors an outer transform and color", "[csg][tier-e]") {
    std::string src = "color(\"red\") translate([1,2,3]) surface(\"" +
                       fixture("surface/simple.dat").string() + "\");";
    auto s = evaluate(src);
    REQUIRE(s.roots.size() == 1);
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.color.has);
    REQUIRE(leaf.transform[3][0] == Approx(1.0));
    REQUIRE(leaf.transform[3][1] == Approx(2.0));
    REQUIRE(leaf.transform[3][2] == Approx(3.0));
}

TEST_CASE("CsgEval:surface() missing file reports a diagnostic, not a crash", "[csg][tier-e]") {
    auto s = evaluate("surface(\"does_not_exist.dat\");");
    REQUIRE(s.roots.empty());
    REQUIRE_FALSE(s.evalDiags.empty());
    REQUIRE(s.evalDiags[0].level == chisel::lang::DiagLevel::Error);
}

TEST_CASE("CsgEval:surface() with no arguments reports a diagnostic", "[csg][tier-e]") {
    auto s = evaluate("surface();");
    REQUIRE(s.roots.empty());
    REQUIRE_FALSE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:surface() inconsistent row length reports a diagnostic", "[csg][tier-e]") {
    std::string src = "surface(\"" + fixture("surface/inconsistent.dat").string() + "\");";
    auto s = evaluate(src);
    REQUIRE(s.roots.empty());
    REQUIRE_FALSE(s.evalDiags.empty());
}

// ---------------------------------------------------------------------------
// Tier E: text()
// ---------------------------------------------------------------------------
TEST_CASE("CsgEval:text() produces a Polygon2D leaf using the bundled default font", "[csg][tier-e]") {
    auto s = evaluate("text(\"A\");");
    REQUIRE(s.roots.size() == 1);
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.kind == CsgLeaf::Kind::Polygon2D);
    REQUIRE_FALSE(leaf.polyPoints.empty());
    REQUIRE_FALSE(leaf.polyPaths.empty());
    REQUIRE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:text() with named text= argument", "[csg][tier-e]") {
    auto s = evaluate("text(text=\"A\");");
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Polygon2D);
    REQUIRE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:text() honors an outer transform and color", "[csg][tier-e]") {
    auto s = evaluate("color(\"red\") translate([1,2,3]) text(\"A\");");
    REQUIRE(s.roots.size() == 1);
    const auto& leaf = asLeaf(s.roots[0]);
    REQUIRE(leaf.color.has);
    REQUIRE(leaf.transform[3][0] == Approx(1.0));
    REQUIRE(leaf.transform[3][1] == Approx(2.0));
    REQUIRE(leaf.transform[3][2] == Approx(3.0));
}

TEST_CASE("CsgEval:text() with a custom font resolves relative to baseDir", "[csg][tier-e]") {
    auto s = evaluateWithBaseDir("text(\"A\", font=\"custom.ttf\");", fixture("text"));
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Polygon2D);
    REQUIRE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:text() with a custom font at an absolute path", "[csg][tier-e]") {
    std::string src = "text(\"A\", font=\"" + fixture("text/custom.ttf").string() + "\");";
    auto s = evaluate(src);
    REQUIRE(s.roots.size() == 1);
    REQUIRE(asLeaf(s.roots[0]).kind == CsgLeaf::Kind::Polygon2D);
    REQUIRE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:text() empty string produces no root and no diagnostic", "[csg][tier-e]") {
    auto s = evaluate("text(\"\");");
    REQUIRE(s.roots.empty());
    REQUIRE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:text() with no arguments reports a diagnostic", "[csg][tier-e]") {
    auto s = evaluate("text();");
    REQUIRE(s.roots.empty());
    REQUIRE_FALSE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:text() non-string argument reports a diagnostic", "[csg][tier-e]") {
    auto s = evaluate("text(5);");
    REQUIRE(s.roots.empty());
    REQUIRE_FALSE(s.evalDiags.empty());
}

TEST_CASE("CsgEval:text() missing font file reports a diagnostic, not a crash", "[csg][tier-e]") {
    auto s = evaluate("text(\"A\", font=\"does_not_exist.ttf\");");
    REQUIRE(s.roots.empty());
    REQUIRE_FALSE(s.evalDiags.empty());
    REQUIRE(s.evalDiags[0].level == chisel::lang::DiagLevel::Error);
}

TEST_CASE("CsgEval:text() ignores direction/language/script for forward compatibility", "[csg][tier-e]") {
    auto s = evaluate("text(\"A\", direction=\"ltr\", language=\"en\", script=\"latin\");");
    REQUIRE(s.roots.size() == 1);
    REQUIRE(s.evalDiags.empty());
}
