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
    return interp.evalNumber(*vlit.elements[static_cast<std::size_t>(i)].value);
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
static const ColorNode& asColor(const AstNodePtr& n) {
    return std::get<ColorNode>(*n);
}
static const AssignStmt& asAssign(const AstNodePtr& n) {
    return std::get<AssignStmt>(*n);
}

// ---------------------------------------------------------------------------
// Global special variables
// ---------------------------------------------------------------------------
TEST_CASE("Parser:global $fn", "[parser]") {
    auto r = parse("$fn = 48;");
    REQUIRE(r.globalFn == 48.0);
    REQUIRE(r.roots.empty());
}

TEST_CASE("Parser:non-literal $fn is deferred as a regular assignment, not discarded", "[parser]") {
    auto r = parse("$fn = 16*2;");
    // Not resolved at parse time (no variable environment available here)...
    REQUIRE_FALSE(r.globalFnSet);
    // ...but also not silently dropped: it's recorded for the Interpreter,
    // same as any other variable assignment.
    REQUIRE(r.assignments.size() == 1);
    REQUIRE(r.assignments[0].name == "$fn");
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

TEST_CASE("Parser:multmatrix", "[parser]") {
    auto r = parse(
        "multmatrix([[1,0,0,5],[0,1,0,0],[0,0,1,0],[0,0,0,1]]) cube([2,2,2]);"
    );
    auto& t = asTrans(r.roots[0]);
    REQUIRE(t.kind == TransformNode::Kind::Matrix);
    REQUIRE(t.children.size() == 1);

    Interpreter interp;
    Value m = interp.evaluate(*t.vec);
    REQUIRE(m.isVector());
    REQUIRE(m.asVec().size() == 4);
    REQUIRE(m.asVec()[0].asVec()[3].asNumber() == Approx(5.0));
}

TEST_CASE("Parser:render", "[parser]") {
    auto r = parse("render() { cube([3,3,3]); sphere(r=2); }");
    auto& t = asTrans(r.roots[0]);
    REQUIRE(t.kind == TransformNode::Kind::Identity);
    REQUIRE(t.children.size() == 2);
}

TEST_CASE("Parser:render with convexity arg", "[parser]") {
    // convexity is a preview-only hint in OpenSCAD; parsed and discarded.
    auto r = parse("render(convexity = 4) sphere(r=2);");
    auto& t = asTrans(r.roots[0]);
    REQUIRE(t.kind == TransformNode::Kind::Identity);
    REQUIRE(t.children.size() == 1);
}

TEST_CASE("Parser:color with string name", "[parser]") {
    auto r = parse("color(\"red\") cube([1,1,1]);");
    auto& c = asColor(r.roots[0]);
    REQUIRE(c.colorExpr != nullptr);
    REQUIRE(c.alphaExpr == nullptr);
    REQUIRE(c.children.size() == 1);

    Interpreter interp;
    Value v = interp.evaluate(*c.colorExpr);
    REQUIRE(v.isString());
    REQUIRE(v.asString() == "red");
}

TEST_CASE("Parser:color with vector and positional alpha", "[parser]") {
    auto r = parse("color([1,0,0], 0.5) sphere(r=2);");
    auto& c = asColor(r.roots[0]);
    REQUIRE(c.colorExpr != nullptr);
    REQUIRE(c.alphaExpr != nullptr);

    Interpreter interp;
    Value v = interp.evaluate(*c.colorExpr);
    REQUIRE(v.isVector());
    REQUIRE(v.asVec().size() == 3);
    REQUIRE(interp.evalNumber(*c.alphaExpr) == Approx(0.5));
}

TEST_CASE("Parser:color with named c and alpha args", "[parser]") {
    auto r = parse("color(alpha = 0.25, c = [0,1,0,1]) { cube([1,1,1]); sphere(r=1); }");
    auto& c = asColor(r.roots[0]);
    REQUIRE(c.colorExpr != nullptr);
    REQUIRE(c.alphaExpr != nullptr);
    REQUIRE(c.children.size() == 2);

    Interpreter interp;
    REQUIRE(interp.evalNumber(*c.alphaExpr) == Approx(0.25));
}

TEST_CASE("Parser:color accepts and discards a special-var override", "[parser]") {
    // color() has no use for $fn, but a stray override must be consumed
    // gracefully (consistent with parseParamList/parseExtrusionParams)
    // rather than becoming a parse error.
    auto r = parse("color($fn=8, \"red\") cube([1,1,1]);");
    auto& c = asColor(r.roots[0]);
    REQUIRE(c.colorExpr != nullptr);

    Interpreter interp;
    Value v = interp.evaluate(*c.colorExpr);
    REQUIRE(v.isString());
    REQUIRE(v.asString() == "red");
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
// General range-literal expressions (usable outside `for`)
// ---------------------------------------------------------------------------
TEST_CASE("Parser:range literal [start:end] outside for is a RangeLit assignment", "[parser][bugfix]") {
    auto r = parse("x = [0:5];");
    REQUIRE(r.assignments.size() == 1);
    const auto& range = std::get<RangeLit>(*r.assignments[0].value);
    REQUIRE(range.step == nullptr);
    Interpreter interp;
    REQUIRE(interp.evalNumber(*range.start) == Approx(0.0));
    REQUIRE(interp.evalNumber(*range.end) == Approx(5.0));
}

TEST_CASE("Parser:range literal [start:step:end] outside for is a RangeLit assignment", "[parser][bugfix]") {
    auto r = parse("x = [0:2:10];");
    REQUIRE(r.assignments.size() == 1);
    const auto& range = std::get<RangeLit>(*r.assignments[0].value);
    REQUIRE(range.step != nullptr);
    Interpreter interp;
    REQUIRE(interp.evalNumber(*range.step) == Approx(2.0));
    REQUIRE(interp.evalNumber(*range.end) == Approx(10.0));
}

TEST_CASE("Parser:a plain vector literal is still a VectorLit, not a range", "[parser]") {
    auto r = parse("x = [1, 2, 3];");
    REQUIRE(r.assignments.size() == 1);
    const auto& vlit = std::get<VectorLit>(*r.assignments[0].value);
    REQUIRE(vlit.elements.size() == 3);
}

TEST_CASE("Parser:an empty vector literal still parses", "[parser]") {
    auto r = parse("x = [];");
    REQUIRE(r.assignments.size() == 1);
    const auto& vlit = std::get<VectorLit>(*r.assignments[0].value);
    REQUIRE(vlit.elements.empty());
}

TEST_CASE("Parser:range literal as a function argument", "[parser]") {
    auto r = parse("echo([0:3]);");
    REQUIRE(r.roots.size() == 1);
    const auto& call = std::get<ModuleCallNode>(*r.roots[0]);
    REQUIRE(call.args.size() == 1);
    REQUIRE(std::holds_alternative<RangeLit>(*call.args[0].value));
}

// ---------------------------------------------------------------------------
// Function literals: function(params) expr
// ---------------------------------------------------------------------------
TEST_CASE("Parser:function literal assigned to a variable", "[parser][v36]") {
    auto r = parse("f = function(x) x * 2;");
    REQUIRE(r.assignments.size() == 1);
    const auto& lit = std::get<FunctionLit>(*r.assignments[0].value);
    REQUIRE(lit.params.size() == 1);
    REQUIRE(lit.params[0].name == "x");
    REQUIRE(lit.params[0].defaultVal == nullptr);
    REQUIRE(std::holds_alternative<BinaryExpr>(*lit.body));
}

TEST_CASE("Parser:function literal with multiple params and a default value", "[parser][v36]") {
    auto r = parse("f = function(x, y=10) x + y;");
    const auto& lit = std::get<FunctionLit>(*r.assignments[0].value);
    REQUIRE(lit.params.size() == 2);
    REQUIRE(lit.params[0].name == "x");
    REQUIRE(lit.params[0].defaultVal == nullptr);
    REQUIRE(lit.params[1].name == "y");
    REQUIRE(lit.params[1].defaultVal != nullptr);
}

TEST_CASE("Parser:function literal passed directly as a call argument", "[parser][v36]") {
    auto r = parse("y = apply(function(x) x + 1, 10);");
    REQUIRE(r.assignments.size() == 1);
    const auto& call = std::get<FunctionCall>(*r.assignments[0].value);
    REQUIRE(call.args.size() == 2);
    REQUIRE(std::holds_alternative<FunctionLit>(*call.args[0].value));
}

TEST_CASE("Parser:named function definition is unaffected by function-literal parsing", "[parser][v36]") {
    // Statement-level `function name(...) = expr;` must still work exactly
    // as before now that `function` also starts an expression-level literal.
    auto r = parse("function double(x) = x * 2;");
    REQUIRE(r.functionDefs.size() == 1);
    REQUIRE(r.functionDefs[0].name == "double");
}

// ---------------------------------------------------------------------------
// List comprehensions and `each`
// ---------------------------------------------------------------------------
TEST_CASE("Parser:basic list comprehension", "[parser]") {
    auto r = parse("x = [for (i = [0:3]) i * 2];");
    REQUIRE(r.assignments.size() == 1);
    const auto& comp = std::get<ListCompExpr>(*r.assignments[0].value);
    REQUIRE(comp.var == "i");
    REQUIRE(std::holds_alternative<RangeLit>(*comp.source));
    REQUIRE(comp.body->kind == ListCompBody::Kind::Expr);
}

TEST_CASE("Parser:list comprehension with if filter", "[parser]") {
    auto r = parse("x = [for (i = [0:5]) if (i % 2 == 0) i];");
    REQUIRE(r.assignments.size() == 1);
    const auto& comp = std::get<ListCompExpr>(*r.assignments[0].value);
    REQUIRE(comp.body->kind == ListCompBody::Kind::If);
    REQUIRE(comp.body->thenBody->kind == ListCompBody::Kind::Expr);
    REQUIRE(comp.body->elseBody == nullptr);
}

TEST_CASE("Parser:list comprehension with if/else", "[parser]") {
    auto r = parse("x = [for (i = [0:5]) if (i % 2 == 0) i else -i];");
    const auto& comp = std::get<ListCompExpr>(*r.assignments[0].value);
    REQUIRE(comp.body->kind == ListCompBody::Kind::If);
    REQUIRE(comp.body->elseBody != nullptr);
    REQUIRE(comp.body->elseBody->kind == ListCompBody::Kind::Expr);
}

TEST_CASE("Parser:list comprehension with each in the body", "[parser]") {
    auto r = parse("x = [for (i = [0:2]) each [i, i]];");
    const auto& comp = std::get<ListCompExpr>(*r.assignments[0].value);
    REQUIRE(comp.body->kind == ListCompBody::Kind::Each);
}

TEST_CASE("Parser:each as a plain list element", "[parser]") {
    auto r = parse("x = [each [1, 2], 3];");
    REQUIRE(r.assignments.size() == 1);
    const auto& vlit = std::get<VectorLit>(*r.assignments[0].value);
    REQUIRE(vlit.elements.size() == 2);
    REQUIRE(vlit.elements[0].isEach == true);
    REQUIRE(vlit.elements[1].isEach == false);
}

TEST_CASE("Parser:a plain vector element defaults to isEach = false", "[parser]") {
    auto r = parse("x = [1, 2, 3];");
    const auto& vlit = std::get<VectorLit>(*r.assignments[0].value);
    for (const auto& elem : vlit.elements)
        REQUIRE(elem.isEach == false);
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

TEST_CASE("Parser:module-local variable assignment is kept, not discarded", "[parser][bugfix]") {
    auto r = parse("module box(r) { d = r * 2; cube(d); }");
    REQUIRE(r.moduleDefs.size() == 1);
    const auto& body = r.moduleDefs[0].body;
    REQUIRE(body.size() == 2);
    REQUIRE(asAssign(body[0]).name == "d");
    Interpreter interp;
    interp.setVar("r", Value::fromNumber(3.0));
    REQUIRE(interp.evalNumber(*asAssign(body[0]).value) == Approx(6.0));
    REQUIRE(asPrim(body[1]).kind == PrimitiveNode::Kind::Cube);
}

TEST_CASE("Parser:local variable assignment inside a for-body is kept", "[parser][bugfix]") {
    auto r = parse("for (i = [0:2]) { x = i * 2; cube(x); }");
    REQUIRE(r.roots.size() == 1);
    const auto& forBody = std::get<ForNode>(*r.roots[0]).children;
    REQUIRE(forBody.size() == 2);
    REQUIRE(asAssign(forBody[0]).name == "x");
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

// ---------------------------------------------------------------------------
// Tier 4 — 2-D primitives and extrusions
// ---------------------------------------------------------------------------
static const ExtrusionNode& asExtrude(const AstNodePtr& n) {
    return std::get<ExtrusionNode>(*n);
}

TEST_CASE("Parser:square positional vector", "[parser]") {
    auto r = parse("square([10, 5]);");
    REQUIRE(r.roots.size() == 1);
    const auto& p = asPrim(r.roots[0]);
    REQUIRE(p.kind == PrimitiveNode::Kind::Square2D);
    // Positional [10,5] → x=10, y=5
    REQUIRE(paramVal(p, "x") == Approx(10.0));
    REQUIRE(paramVal(p, "y") == Approx(5.0));
}

TEST_CASE("Parser:square named size scalar", "[parser]") {
    auto r = parse("square(size=8);");
    REQUIRE(r.roots.size() == 1);
    const auto& p = asPrim(r.roots[0]);
    REQUIRE(p.kind == PrimitiveNode::Kind::Square2D);
    REQUIRE(p.params.count("size") == 1);
}

TEST_CASE("Parser:circle named r", "[parser]") {
    auto r = parse("circle(r=5);");
    REQUIRE(r.roots.size() == 1);
    const auto& p = asPrim(r.roots[0]);
    REQUIRE(p.kind == PrimitiveNode::Kind::Circle2D);
    REQUIRE(paramVal(p, "r") == Approx(5.0));
}

TEST_CASE("Parser:circle positional radius", "[parser]") {
    auto r = parse("circle(3);");
    const auto& p = asPrim(r.roots[0]);
    REQUIRE(p.kind == PrimitiveNode::Kind::Circle2D);
    REQUIRE(paramVal(p, "_pos0") == Approx(3.0));
}

TEST_CASE("Parser:multiple positional args indexed distinctly", "[parser]") {
    auto r = parse("cylinder(10, 5);");
    const auto& p = asPrim(r.roots[0]);
    REQUIRE(p.kind == PrimitiveNode::Kind::Cylinder);
    REQUIRE(paramVal(p, "_pos0") == Approx(10.0));
    REQUIRE(paramVal(p, "_pos1") == Approx(5.0));
}

TEST_CASE("Parser:polygon with points", "[parser]") {
    auto r = parse("polygon(points=[[0,0],[10,0],[5,8]]);");
    REQUIRE(r.roots.size() == 1);
    const auto& p = asPrim(r.roots[0]);
    REQUIRE(p.kind == PrimitiveNode::Kind::Polygon2D);
    REQUIRE(p.params.count("points") == 1);
}

TEST_CASE("Parser:linear_extrude with height", "[parser]") {
    auto r = parse("linear_extrude(height=10) { circle(r=5); }");
    REQUIRE(r.roots.size() == 1);
    const auto& e = asExtrude(r.roots[0]);
    REQUIRE(e.kind == ExtrusionNode::Kind::Linear);
    REQUIRE(e.params.count("height") == 1);
    REQUIRE(e.children.size() == 1);
}

TEST_CASE("Parser:linear_extrude positional height", "[parser]") {
    auto r = parse("linear_extrude(15) square([4,4]);");
    const auto& e = asExtrude(r.roots[0]);
    REQUIRE(e.kind == ExtrusionNode::Kind::Linear);
    REQUIRE(e.params.count("_pos0") == 1);
    REQUIRE(e.children.size() == 1);
}

TEST_CASE("Parser:rotate_extrude with angle", "[parser]") {
    auto r = parse("rotate_extrude(angle=180) circle(r=3);");
    REQUIRE(r.roots.size() == 1);
    const auto& e = asExtrude(r.roots[0]);
    REQUIRE(e.kind == ExtrusionNode::Kind::Rotate);
    REQUIRE(e.params.count("angle") == 1);
    REQUIRE(e.children.size() == 1);
}

TEST_CASE("Parser:linear_extrude with twist and center", "[parser]") {
    auto r = parse("linear_extrude(height=20, twist=90, center=true) square([5,5]);");
    const auto& e = asExtrude(r.roots[0]);
    REQUIRE(e.params.count("height") == 1);
    REQUIRE(e.params.count("twist")  == 1);
    REQUIRE(e.params.count("center") == 1);
}

// ---------------------------------------------------------------------------
// offset()
// ---------------------------------------------------------------------------
static const OffsetNode& asOffset(const AstNodePtr& n) {
    return std::get<OffsetNode>(*n);
}

TEST_CASE("Parser:offset with rounded radius", "[parser]") {
    auto r = parse("offset(r=2) circle(r=5);");
    REQUIRE(r.roots.size() == 1);
    const auto& o = asOffset(r.roots[0]);
    REQUIRE(o.params.count("r") == 1);
    REQUIRE(o.children.size() == 1);

    Interpreter interp;
    REQUIRE(interp.evalNumber(*o.params.at("r")) == Approx(2.0));
}

TEST_CASE("Parser:offset with delta and chamfer", "[parser]") {
    auto r = parse("offset(delta=1, chamfer=true) square([10,10]);");
    const auto& o = asOffset(r.roots[0]);
    REQUIRE(o.params.count("delta")   == 1);
    REQUIRE(o.params.count("chamfer") == 1);

    Interpreter interp;
    REQUIRE(interp.evalNumber(*o.params.at("delta")) == Approx(1.0));
    Value cv = interp.evaluate(*o.params.at("chamfer"));
    REQUIRE(bool(cv) == true);
}

TEST_CASE("Parser:offset wraps multiple children", "[parser]") {
    auto r = parse("offset(r=-1) { square([5,5]); circle(r=3); }");
    const auto& o = asOffset(r.roots[0]);
    REQUIRE(o.children.size() == 2);
}

// ---------------------------------------------------------------------------
// projection()
// ---------------------------------------------------------------------------
static const ProjectionNode& asProjection(const AstNodePtr& n) {
    return std::get<ProjectionNode>(*n);
}

TEST_CASE("Parser:projection defaults with no args", "[parser]") {
    auto r = parse("projection() cube([2,2,2]);");
    REQUIRE(r.roots.size() == 1);
    const auto& p = asProjection(r.roots[0]);
    REQUIRE(p.params.count("cut") == 0);
    REQUIRE(p.children.size() == 1);
}

TEST_CASE("Parser:projection with cut=true", "[parser]") {
    auto r = parse("projection(cut = true) sphere(r=5);");
    const auto& p = asProjection(r.roots[0]);
    REQUIRE(p.params.count("cut") == 1);

    Interpreter interp;
    Value cv = interp.evaluate(*p.params.at("cut"));
    REQUIRE(bool(cv) == true);
}

TEST_CASE("Parser:projection wraps multiple children", "[parser]") {
    auto r = parse("projection() { cube([2,2,2]); sphere(r=1); }");
    const auto& p = asProjection(r.roots[0]);
    REQUIRE(p.children.size() == 2);
}

// ---------------------------------------------------------------------------
// include<>/use<> — the Parser only records these; SourceLoader (tested
// separately in test_source_loader.cpp) resolves them against the filesystem.
// ---------------------------------------------------------------------------
TEST_CASE("Parser:include records path and kind", "[parser][tier-e]") {
    auto r = parse("include <shapes.scad>\ncube(1);");
    REQUIRE(r.includes.size() == 1);
    REQUIRE(r.includes[0].kind == IncludeStmt::Kind::Include);
    REQUIRE(r.includes[0].path == "shapes.scad");
    // The geometry statement after it still parses normally.
    REQUIRE(r.roots.size() == 1);
}

TEST_CASE("Parser:use records path and kind", "[parser][tier-e]") {
    auto r = parse("use <lib.scad>");
    REQUIRE(r.includes.size() == 1);
    REQUIRE(r.includes[0].kind == IncludeStmt::Kind::Use);
    REQUIRE(r.includes[0].path == "lib.scad");
}

TEST_CASE("Parser:multiple include/use directives keep source order", "[parser][tier-e]") {
    auto r = parse("include <a.scad>\nuse <b.scad>\ninclude <c.scad>");
    REQUIRE(r.includes.size() == 3);
    REQUIRE(r.includes[0].path == "a.scad");
    REQUIRE(r.includes[1].path == "b.scad");
    REQUIRE(r.includes[2].path == "c.scad");
    REQUIRE(r.includes[0].kind == IncludeStmt::Kind::Include);
    REQUIRE(r.includes[1].kind == IncludeStmt::Kind::Use);
    REQUIRE(r.includes[2].kind == IncludeStmt::Kind::Include);
}

TEST_CASE("Parser:include missing path is a parse error", "[parser][tier-e]") {
    Lexer lexer("include ;");
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    parser.parse();
    REQUIRE(parser.hasErrors());
}

TEST_CASE("Parser:include records position for splicing", "[parser][tier-e]") {
    auto r = parse("x = 1;\ninclude <lib.scad>\ny = 2;");
    REQUIRE(r.includes.size() == 1);
    // One assignment (x=1) existed before the include; the second (y=2)
    // comes after — SourceLoader relies on this to splice at the right spot.
    REQUIRE(r.includes[0].assignIndex == 1);
}

TEST_CASE("Parser:include inside a block is a parse error, not silently dropped", "[parser][tier-e]") {
    Lexer lexer("union() { include <bar.scad>; cube(1); }");
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto r = parser.parse();
    REQUIRE(parser.hasErrors());
    // The rest of the block still parses: the enclosing union() with its cube().
    REQUIRE(r.roots.size() == 1);
    const auto& u = std::get<BooleanNode>(*r.roots[0]);
    REQUIRE(u.children.size() == 1);
}

// ---------------------------------------------------------------------------
// Diagnostics for otherwise-silent parse gaps
// ---------------------------------------------------------------------------
TEST_CASE("Parser:unrecognized top-level statement reports an error", "[parser]") {
    // A bare identifier not followed by '(' or '=' is not a valid statement
    // and must not be silently skipped.
    Lexer lexer("y;");
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto r = parser.parse();
    REQUIRE(parser.hasErrors());
    REQUIRE(r.roots.empty());
}

TEST_CASE("Parser:numeric token as a named-param name is a parse error", "[parser]") {
    // `3=5` must not be silently accepted as a named param keyed "3".
    Lexer lexer("cube(3=5);");
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    parser.parse();
    REQUIRE(parser.hasErrors());
}

TEST_CASE("Parser:keyword-named param still parses as a named param", "[parser]") {
    // Regression guard: the numeric-token fix must not break keyword-named
    // params like 'scale=' (a keyword token, not Number/String).
    auto r = parse("linear_extrude(height=5, scale=2) square(1);");
    REQUIRE(r.roots.size() == 1);
}

// ---------------------------------------------------------------------------
// v3 Phase 2: CSG modifier characters (# % ! *)
// ---------------------------------------------------------------------------
TEST_CASE("Parser:each modifier character tags the following primitive", "[parser]") {
    REQUIRE((asPrim(parse("#cube(1);").roots[0]).modifiers & ModHighlight) != 0);
    REQUIRE((asPrim(parse("%cube(1);").roots[0]).modifiers & ModBackground) != 0);
    REQUIRE((asPrim(parse("*cube(1);").roots[0]).modifiers & ModDisable) != 0);
    REQUIRE((asPrim(parse("!cube(1);").roots[0]).modifiers & ModRoot) != 0);
}

TEST_CASE("Parser:no modifier characters means modifiers is ModNone", "[parser]") {
    REQUIRE(asPrim(parse("cube(1);").roots[0]).modifiers == ModNone);
}

TEST_CASE("Parser:stacked modifier characters combine into one bitmask", "[parser]") {
    auto mods = asPrim(parse("#!cube(1);").roots[0]).modifiers;
    REQUIRE((mods & ModHighlight) != 0);
    REQUIRE((mods & ModRoot) != 0);
    REQUIRE((mods & ModBackground) == 0);
    REQUIRE((mods & ModDisable) == 0);
}

TEST_CASE("Parser:modifier character tags a boolean/group node", "[parser]") {
    auto r = parse("%union() { cube(1); sphere(1); }");
    REQUIRE((asBool(r.roots[0]).modifiers & ModBackground) != 0);
}

TEST_CASE("Parser:modifier character tags a transform node", "[parser]") {
    auto r = parse("#translate([1,0,0]) cube(1);");
    REQUIRE((asTrans(r.roots[0]).modifiers & ModHighlight) != 0);
}

TEST_CASE("Parser:modifier character works on a statement nested in a block", "[parser]") {
    auto r = parse("union() { *cube(1); sphere(1); }");
    const auto& u = asBool(r.roots[0]);
    REQUIRE(u.children.size() == 2);
    REQUIRE((asPrim(u.children[0]).modifiers & ModDisable) != 0);
    REQUIRE(asPrim(u.children[1]).modifiers == ModNone);
}

TEST_CASE("Parser:percent/star/bang still work as expression operators", "[parser]") {
    // These characters only mean CSG modifiers at statement-start;
    // parseNode() never fires mid-expression, so ordinary arithmetic/
    // logical-not usage inside a param expression must be unaffected.
    auto r = parse("x = 5 % 2;\ny = 3 * 4;\nz = !true;");
    REQUIRE(r.assignments.size() == 3);
}

// ---------------------------------------------------------------------------
// Regression (PR #69 review): a modifier-prefixed *assignment* must not
// silently reroute through result.roots — it has to stay on the same
// hoisted result.assignments path as a plain top-level assignment, since
// mixing the two paths broke "last assignment wins" ordering. OpenSCAD's
// grammar doesn't allow modifiers on assignments at all, so this is also a
// parse error.
// ---------------------------------------------------------------------------
TEST_CASE("Parser:modifier before a top-level assignment is a parse error and still hoists", "[parser]") {
    Lexer lexer("#x = 2;");
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto r = parser.parse();
    REQUIRE(parser.hasErrors());
    // Must land in result.assignments (hoisted), not result.roots.
    REQUIRE(r.roots.empty());
    REQUIRE(r.assignments.size() == 1);
    REQUIRE(r.assignments[0].name == "x");
}

TEST_CASE("Parser:modifier-prefixed and plain top-level assignments hoist in file order", "[parser]") {
    // Regression for the exact scenario from the review comment: without the
    // fix, `#x = 2;` took a different (non-hoisted) path than `x = 1;`, so
    // the interpreter saw x=2 last instead of x=1 (file-order last-write).
    Lexer lexer("#x = 2;\nx = 1;\ncube(x);");
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto r = parser.parse();
    REQUIRE(parser.hasErrors()); // the '#' itself is still diagnosed
    REQUIRE(r.assignments.size() == 2);
    REQUIRE(r.roots.size() == 1); // just the cube — no phantom assignment root

    Interpreter interp;
    interp.loadAssignments(r);
    REQUIRE(interp.getVar("x").asNumber() == Approx(1.0)); // last one in file order wins
}

TEST_CASE("Parser:modifier before a nested (block-scoped) assignment is a parse error", "[parser]") {
    Lexer lexer("union() { #x = 2; sphere(1); }");
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto r = parser.parse();
    REQUIRE(parser.hasErrors());
    REQUIRE(r.roots.size() == 1);
    const auto& u = asBool(r.roots[0]);
    REQUIRE(u.children.size() == 2);
    REQUIRE(asAssign(u.children[0]).name == "x");
    REQUIRE(asAssign(u.children[0]).modifiers == ModNone); // rejected, not silently tagged
}
