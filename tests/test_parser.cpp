#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "lang/Lexer.h"
#include "lang/Parser.h"
#include "lang/AST.h"
#include "lang/Interpreter.h"

using namespace chisel::lang;
using Catch::Approx;

// Evaluate a primitive param ExprPtr to a double using a default interpreter.
static double paramVal(const PrimitiveNode& p, const std::string& name) {
    Interpreter interp;
    return interp.evalNumber(*p.params.at(name));
}

// Evaluate component i of a TransformNode's vec ExprPtr.
static double vecComp(const TransformNode& t, int i) {
    Interpreter interp;
    const auto& vlit = std::get<VectorLit>(*t.vec);
    return interp.evalNumber(*vlit.elements[static_cast<std::size_t>(i)]);
}

// Helper: lex + parse, assert no errors, return result
static ParseResult parse(std::string_view src) {
    Lexer lexer(src);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(lexer.hasErrors());
    Parser parser(std::move(tokens));
    auto result = parser.parse();
    REQUIRE_FALSE(parser.hasErrors());
    return result;
}

// Typed node accessors
static const PrimitiveNode& asPrim(const AstNodePtr& n) {
    return std::get<PrimitiveNode>(*n);
}
static const BooleanNode& asBool(const AstNodePtr& n) {
    return std::get<BooleanNode>(*n);
}
static const TransformNode& asTrans(const AstNodePtr& n) {
    return std::get<TransformNode>(*n);
}

// ---------------------------------------------------------------------------
// Global special variables
// ---------------------------------------------------------------------------
TEST_CASE("Parser:global $fn", "[parser]") {
    auto r = parse("$fn = 48;");
    REQUIRE(r.globalFn == 48.0);
    REQUIRE(r.roots.empty());
}

TEST_CASE("Parser:global $fs and $fa", "[parser]") {
    auto r = parse("$fs = 1.0; $fa = 6.0;");
    REQUIRE(r.globalFs == Approx(1.0));
    REQUIRE(r.globalFa == Approx(6.0));
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------
TEST_CASE("Parser:cube with vector size", "[parser]") {
    auto r = parse("cube([10, 20, 30]);");
    REQUIRE(r.roots.size() == 1);
    auto& p = asPrim(r.roots[0]);
    REQUIRE(p.kind == PrimitiveNode::Kind::Cube);
    REQUIRE(paramVal(p, "x") == Approx(10.0));
    REQUIRE(paramVal(p, "y") == Approx(20.0));
    REQUIRE(paramVal(p, "z") == Approx(30.0));
    REQUIRE(p.center == false);
}

TEST_CASE("Parser:cube centered", "[parser]") {
    auto r = parse("cube([10, 10, 10], center = true);");
    REQUIRE(asPrim(r.roots[0]).center == true);
}

TEST_CASE("Parser:sphere with r", "[parser]") {
    auto r = parse("sphere(r = 5);");
    auto& p = asPrim(r.roots[0]);
    REQUIRE(p.kind == PrimitiveNode::Kind::Sphere);
    REQUIRE(paramVal(p, "r") == Approx(5.0));
}

TEST_CASE("Parser:sphere with $fn override", "[parser]") {
    auto r = parse("sphere(r = 5, $fn = 8);");
    auto& p = asPrim(r.roots[0]);
    REQUIRE(paramVal(p, "r")   == Approx(5.0));
    REQUIRE(paramVal(p, "$fn") == Approx(8.0));
}

TEST_CASE("Parser:cylinder h and r", "[parser]") {
    auto r = parse("cylinder(h = 10, r = 5);");
    auto& p = asPrim(r.roots[0]);
    REQUIRE(p.kind == PrimitiveNode::Kind::Cylinder);
    REQUIRE(paramVal(p, "h") == Approx(10.0));
    REQUIRE(paramVal(p, "r") == Approx(5.0));
}

TEST_CASE("Parser:cylinder r1 r2 cone", "[parser]") {
    auto r = parse("cylinder(h = 12, r1 = 7, r2 = 1);");
    auto& p = asPrim(r.roots[0]);
    REQUIRE(paramVal(p, "h")  == Approx(12.0));
    REQUIRE(paramVal(p, "r1") == Approx(7.0));
    REQUIRE(paramVal(p, "r2") == Approx(1.0));
}

TEST_CASE("Parser:cylinder centered", "[parser]") {
    auto r = parse("cylinder(h = 10, r = 5, center = true);");
    REQUIRE(asPrim(r.roots[0]).center == true);
}

// ---------------------------------------------------------------------------
// Booleans
// ---------------------------------------------------------------------------
TEST_CASE("Parser:union with two children", "[parser]") {
    auto r = parse("union() { cube([5,5,5]); sphere(r=3); }");
    REQUIRE(r.roots.size() == 1);
    auto& b = asBool(r.roots[0]);
    REQUIRE(b.op == BooleanNode::Op::Union);
    REQUIRE(b.children.size() == 2);
    REQUIRE(asPrim(b.children[0]).kind == PrimitiveNode::Kind::Cube);
    REQUIRE(asPrim(b.children[1]).kind == PrimitiveNode::Kind::Sphere);
}

TEST_CASE("Parser:difference", "[parser]") {
    auto r = parse("difference() { cube([10,10,10]); sphere(r=6); }");
    auto& b = asBool(r.roots[0]);
    REQUIRE(b.op == BooleanNode::Op::Difference);
    REQUIRE(b.children.size() == 2);
}

TEST_CASE("Parser:intersection", "[parser]") {
    auto r = parse("intersection() { sphere(r=9); cube([15,15,15], center=true); }");
    auto& b = asBool(r.roots[0]);
    REQUIRE(b.op == BooleanNode::Op::Intersection);
    REQUIRE(b.children.size() == 2);
}

TEST_CASE("Parser:hull with two children", "[parser]") {
    auto r = parse("hull() { sphere(r=1); cube([2,2,2]); }");
    REQUIRE(r.roots.size() == 1);
    auto& b = asBool(r.roots[0]);
    REQUIRE(b.op == BooleanNode::Op::Hull);
    REQUIRE(b.children.size() == 2);
}

TEST_CASE("Parser:minkowski", "[parser]") {
    auto r = parse("minkowski() { cube([10,10,10]); sphere(r=1); }");
    REQUIRE(r.roots.size() == 1);
    auto& b = asBool(r.roots[0]);
    REQUIRE(b.op == BooleanNode::Op::Minkowski);
    REQUIRE(b.children.size() == 2);
}

TEST_CASE("Parser:hull empty", "[parser]") {
    auto r = parse("hull() {}");
    REQUIRE(r.roots.size() == 1);
    auto& b = asBool(r.roots[0]);
    REQUIRE(b.op == BooleanNode::Op::Hull);
    REQUIRE(b.children.empty());
}

TEST_CASE("Parser:empty union", "[parser]") {
    // edge case from chiselcad_test.scad line 305
    auto r = parse("union() {};");
    auto& b = asBool(r.roots[0]);
    REQUIRE(b.op == BooleanNode::Op::Union);
    REQUIRE(b.children.empty());
}

// ---------------------------------------------------------------------------
// Transforms
// ---------------------------------------------------------------------------
TEST_CASE("Parser:translate", "[parser]") {
    auto r = parse("translate([1, 2, 3]) cube([5,5,5]);");
    REQUIRE(r.roots.size() == 1);
    auto& t = asTrans(r.roots[0]);
    REQUIRE(t.kind == TransformNode::Kind::Translate);
    REQUIRE(vecComp(t, 0) == Approx(1.0));
    REQUIRE(vecComp(t, 1) == Approx(2.0));
    REQUIRE(vecComp(t, 2) == Approx(3.0));
    REQUIRE(t.children.size() == 1);
}

TEST_CASE("Parser:rotate", "[parser]") {
    auto r = parse("rotate([45, 0, 0]) cube([8,8,8], center=true);");
    auto& t = asTrans(r.roots[0]);
    REQUIRE(t.kind == TransformNode::Kind::Rotate);
    REQUIRE(vecComp(t, 0) == Approx(45.0));
    REQUIRE(vecComp(t, 1) == Approx(0.0));
    REQUIRE(vecComp(t, 2) == Approx(0.0));
}

TEST_CASE("Parser:rotate with negative angle", "[parser]") {
    // rotate([0, -30, 0]) from chiselcad_test.scad
    auto r = parse("rotate([0, -30, 0]) cube([6,8,5]);");
    auto& t = asTrans(r.roots[0]);
    REQUIRE(vecComp(t, 1) == Approx(-30.0));
}

TEST_CASE("Parser:scale", "[parser]") {
    auto r = parse("scale([1.8, 1.8, 1.8]) sphere(r=3);");
    auto& t = asTrans(r.roots[0]);
    REQUIRE(t.kind == TransformNode::Kind::Scale);
    REQUIRE(vecComp(t, 0) == Approx(1.8));
}

TEST_CASE("Parser:mirror", "[parser]") {
    auto r = parse("mirror([1, 0, 0]) sphere(r=3);");
    auto& t = asTrans(r.roots[0]);
    REQUIRE(t.kind == TransformNode::Kind::Mirror);
    REQUIRE(vecComp(t, 0) == Approx(1.0));
    REQUIRE(vecComp(t, 1) == Approx(0.0));
    REQUIRE(vecComp(t, 2) == Approx(0.0));
}

TEST_CASE("Parser:mirror identity [0,0,0]", "[parser]") {
    // edge case from chiselcad_test.scad line 303
    auto r = parse("mirror([0, 0, 0]) union() {};");
    auto& t = asTrans(r.roots[0]);
    REQUIRE(vecComp(t, 0) == Approx(0.0));
    REQUIRE(vecComp(t, 1) == Approx(0.0));
    REQUIRE(vecComp(t, 2) == Approx(0.0));
}

// ---------------------------------------------------------------------------
// Nested operations
// ---------------------------------------------------------------------------
TEST_CASE("Parser:translate wrapping difference", "[parser]") {
    auto r = parse(
        "translate([0, 0, 0])"
        "  difference() {"
        "    cube([13, 13, 13], center=true);"
        "    sphere(r=7);"
        "  }"
    );
    auto& t = asTrans(r.roots[0]);
    REQUIRE(t.kind == TransformNode::Kind::Translate);
    REQUIRE(t.children.size() == 1);
    auto& b = asBool(t.children[0]);
    REQUIRE(b.op == BooleanNode::Op::Difference);
    REQUIRE(b.children.size() == 2);
}

TEST_CASE("Parser:union inside difference", "[parser]") {
    auto r = parse(
        "difference() {"
        "  union() { cube([14,10,8]); cylinder(h=6, r=3); }"
        "  cylinder(h=16, r=2);"
        "}"
    );
    auto& outer = asBool(r.roots[0]);
    REQUIRE(outer.op == BooleanNode::Op::Difference);
    auto& inner = asBool(outer.children[0]);
    REQUIRE(inner.op == BooleanNode::Op::Union);
    REQUIRE(inner.children.size() == 2);
}

// ---------------------------------------------------------------------------
// Multiple top-level statements (as in chiselcad_test.scad)
// ---------------------------------------------------------------------------
TEST_CASE("Parser:multiple root statements", "[parser]") {
    auto r = parse(
        "$fn = 48;"
        "translate([0,0,0]) cube([10,10,10]);"
        "translate([18,0,0]) cube([10,10,10], center=true);"
    );
    REQUIRE(r.globalFn == 48.0);
    REQUIRE(r.roots.size() == 2);
}

// ---------------------------------------------------------------------------
// if / else
// ---------------------------------------------------------------------------
static const IfNode& asIf(const AstNodePtr& n) {
    return std::get<IfNode>(*n);
}

TEST_CASE("Parser:if with single then child", "[parser]") {
    auto r = parse("if (1) sphere(r=3);");
    REQUIRE(r.roots.size() == 1);
    auto& n = asIf(r.roots[0]);
    REQUIRE(n.thenChildren.size() == 1);
    REQUIRE(n.elseChildren.empty());
    REQUIRE(asPrim(n.thenChildren[0]).kind == PrimitiveNode::Kind::Sphere);
}

TEST_CASE("Parser:if with brace body", "[parser]") {
    auto r = parse("if (1) { cube([5,5,5]); sphere(r=2); }");
    auto& n = asIf(r.roots[0]);
    REQUIRE(n.thenChildren.size() == 2);
    REQUIRE(n.elseChildren.empty());
}

TEST_CASE("Parser:if-else both branches", "[parser]") {
    auto r = parse("if (0) sphere(r=3); else cube([4,4,4]);");
    auto& n = asIf(r.roots[0]);
    REQUIRE(n.thenChildren.size() == 1);
    REQUIRE(n.elseChildren.size() == 1);
    REQUIRE(asPrim(n.thenChildren[0]).kind == PrimitiveNode::Kind::Sphere);
    REQUIRE(asPrim(n.elseChildren[0]).kind == PrimitiveNode::Kind::Cube);
}

TEST_CASE("Parser:if-else chained", "[parser]") {
    // else branch is itself an if — chaining works naturally
    auto r = parse("if (0) sphere(r=1); else if (1) cube([2,2,2]);");
    auto& outer = asIf(r.roots[0]);
    REQUIRE(outer.elseChildren.size() == 1);
    auto& inner = asIf(outer.elseChildren[0]);
    REQUIRE(inner.thenChildren.size() == 1);
}

TEST_CASE("Parser:if condition is expression", "[parser]") {
    auto r = parse("if (3 > 2) sphere(r=1);");
    REQUIRE(r.roots.size() == 1);
    REQUIRE(std::holds_alternative<IfNode>(*r.roots[0]));
}

// ---------------------------------------------------------------------------
// for loops
// ---------------------------------------------------------------------------
static const ForNode& asFor(const AstNodePtr& n) {
    return std::get<ForNode>(*n);
}

TEST_CASE("Parser:for range [start:end]", "[parser]") {
    auto r = parse("for (i = [0:4]) sphere(r=1);");
    REQUIRE(r.roots.size() == 1);
    auto& f = asFor(r.roots[0]);
    REQUIRE(f.var == "i");
    REQUIRE(f.range.isRange == true);
    REQUIRE(f.range.step == nullptr); // implicit step
    REQUIRE(f.children.size() == 1);
}

TEST_CASE("Parser:for range [start:step:end]", "[parser]") {
    auto r = parse("for (i = [0:2:8]) sphere(r=1);");
    auto& f = asFor(r.roots[0]);
    REQUIRE(f.range.isRange == true);
    REQUIRE(f.range.step != nullptr);
}

TEST_CASE("Parser:for list", "[parser]") {
    auto r = parse("for (v = [1, 3, 7]) sphere(r=1);");
    auto& f = asFor(r.roots[0]);
    REQUIRE(f.range.isRange == false);
    REQUIRE(f.range.list.size() == 3);
}

TEST_CASE("Parser:for with brace body", "[parser]") {
    auto r = parse("for (i = [0:2]) { cube([5,5,5]); sphere(r=2); }");
    auto& f = asFor(r.roots[0]);
    REQUIRE(f.children.size() == 2);
}

// ---------------------------------------------------------------------------
// Module definitions and calls
// ---------------------------------------------------------------------------
static const ModuleCallNode& asModuleCall(const AstNodePtr& n) {
    return std::get<ModuleCallNode>(*n);
}

TEST_CASE("Parser:module definition stored in moduleDefs", "[parser]") {
    auto r = parse("module my_box(w, h) { cube([w, h, h]); }");
    REQUIRE(r.roots.empty());
    REQUIRE(r.moduleDefs.size() == 1);
    REQUIRE(r.moduleDefs[0].name == "my_box");
    REQUIRE(r.moduleDefs[0].params.size() == 2);
    REQUIRE(r.moduleDefs[0].params[0].name == "w");
    REQUIRE(r.moduleDefs[0].params[1].name == "h");
    REQUIRE(r.moduleDefs[0].body.size() == 1);
}

TEST_CASE("Parser:module param with default value", "[parser]") {
    auto r = parse("module pill(r = 2, h = 5) { cylinder(r=r, h=h); }");
    REQUIRE(r.moduleDefs.size() == 1);
    REQUIRE(r.moduleDefs[0].params[0].name == "r");
    REQUIRE(r.moduleDefs[0].params[0].defaultVal != nullptr);
    REQUIRE(r.moduleDefs[0].params[1].name == "h");
}

TEST_CASE("Parser:module call positional args", "[parser]") {
    auto r = parse(
        "module box(w, h) { cube([w, h, h]); }"
        "box(10, 5);"
    );
    REQUIRE(r.moduleDefs.size() == 1);
    REQUIRE(r.roots.size() == 1);
    auto& c = asModuleCall(r.roots[0]);
    REQUIRE(c.name == "box");
    REQUIRE(c.args.size() == 2);
    REQUIRE(c.args[0].name.empty()); // positional
    REQUIRE(c.args[1].name.empty());
}

TEST_CASE("Parser:module call named args", "[parser]") {
    auto r = parse(
        "module pill(r, h) { cylinder(r=r, h=h); }"
        "pill(h = 10, r = 3);"
    );
    REQUIRE(r.roots.size() == 1);
    auto& c = asModuleCall(r.roots[0]);
    REQUIRE(c.args.size() == 2);
    REQUIRE(c.args[0].name == "h");
    REQUIRE(c.args[1].name == "r");
}

TEST_CASE("Parser:multiple module defs", "[parser]") {
    auto r = parse(
        "module a() { sphere(r=1); }"
        "module b() { cube([2,2,2]); }"
        "a(); b();"
    );
    REQUIRE(r.moduleDefs.size() == 2);
    REQUIRE(r.roots.size() == 2);
}

// ---------------------------------------------------------------------------
// Error recovery
// ---------------------------------------------------------------------------
TEST_CASE("Parser:recovers from missing paren", "[parser]") {
    Lexer lexer("cube [10,10,10];"); // missing ( before [
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto r = parser.parse();
    REQUIRE(parser.hasErrors());
    // Should still produce a node (partial parse)
    // The important thing is it doesn't crash or hang
}
