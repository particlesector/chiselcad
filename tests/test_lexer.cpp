#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "lang/Lexer.h"
#include "lang/Token.h"

using Catch::Approx;

using namespace chisel::lang;

// Helper: lex source and return tokens (excluding final Eof)
static std::vector<Token> lex(std::string_view src) {
    Lexer lexer(src);
    auto tokens = lexer.tokenize();
    REQUIRE_FALSE(lexer.hasErrors());
    // Drop the trailing Eof for most tests
    if (!tokens.empty() && tokens.back().kind == TokenKind::Eof)
        tokens.pop_back();
    return tokens;
}

// Helper: lex and return the kinds only
static std::vector<TokenKind> kinds(std::string_view src) {
    auto tokens = lex(src);
    std::vector<TokenKind> ks;
    ks.reserve(tokens.size());
    for (auto& t : tokens) ks.push_back(t.kind);
    return ks;
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------
TEST_CASE("Lexer:primitive keywords", "[lexer]") {
    REQUIRE(kinds("cube")     == std::vector{TokenKind::Cube});
    REQUIRE(kinds("sphere")   == std::vector{TokenKind::Sphere});
    REQUIRE(kinds("cylinder") == std::vector{TokenKind::Cylinder});
}

// ---------------------------------------------------------------------------
// Boolean keywords
// ---------------------------------------------------------------------------
TEST_CASE("Lexer:boolean keywords", "[lexer]") {
    REQUIRE(kinds("union")        == std::vector{TokenKind::Union});
    REQUIRE(kinds("difference")   == std::vector{TokenKind::Difference});
    REQUIRE(kinds("intersection") == std::vector{TokenKind::Intersection});
}

TEST_CASE("Lexer:hull and minkowski keywords", "[lexer]") {
    REQUIRE(kinds("hull")      == std::vector{TokenKind::Hull});
    REQUIRE(kinds("minkowski") == std::vector{TokenKind::Minkowski});
}

// ---------------------------------------------------------------------------
// Transform keywords
// ---------------------------------------------------------------------------
TEST_CASE("Lexer:transform keywords", "[lexer]") {
    REQUIRE(kinds("translate") == std::vector{TokenKind::Translate});
    REQUIRE(kinds("rotate")    == std::vector{TokenKind::Rotate});
    REQUIRE(kinds("scale")     == std::vector{TokenKind::Scale});
    REQUIRE(kinds("mirror")    == std::vector{TokenKind::Mirror});
}

// ---------------------------------------------------------------------------
// Boolean literals
// ---------------------------------------------------------------------------
TEST_CASE("Lexer:boolean literals", "[lexer]") {
    REQUIRE(kinds("true")  == std::vector{TokenKind::True});
    REQUIRE(kinds("false") == std::vector{TokenKind::False});
}

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------
TEST_CASE("Lexer:integer", "[lexer]") {
    auto t = lex("48");
    REQUIRE(t.size() == 1);
    REQUIRE(t[0].kind == TokenKind::Number);
    REQUIRE(t[0].numberValue() == 48.0);
}

TEST_CASE("Lexer:float", "[lexer]") {
    auto t = lex("3.14");
    REQUIRE(t[0].kind == TokenKind::Number);
    REQUIRE(t[0].numberValue() == Approx(3.14));
}

TEST_CASE("Lexer:leading dot float", "[lexer]") {
    auto t = lex(".5");
    REQUIRE(t[0].kind == TokenKind::Number);
    REQUIRE(t[0].numberValue() == Approx(0.5));
}

TEST_CASE("Lexer:negative number", "[lexer]") {
    auto t = lex("-1");
    REQUIRE(t[0].kind == TokenKind::Number);
    REQUIRE(t[0].numberValue() == -1.0);
}

TEST_CASE("Lexer:negative float in vector context", "[lexer]") {
    // As seen in rotate([0, -30, 0])
    auto t = lex("[0, -30, 0]");
    REQUIRE(t[1].kind == TokenKind::Number); // 0
    REQUIRE(t[3].kind == TokenKind::Number); // -30
    REQUIRE(t[3].numberValue() == -30.0);
}

// ---------------------------------------------------------------------------
// Special variables
// ---------------------------------------------------------------------------
TEST_CASE("Lexer:special variables", "[lexer]") {
    auto fn = lex("$fn");
    REQUIRE(fn[0].kind == TokenKind::SpecialVar);
    REQUIRE(fn[0].text == "$fn");

    auto fs = lex("$fs");
    REQUIRE(fs[0].kind == TokenKind::SpecialVar);
    REQUIRE(fs[0].text == "$fs");

    auto fa = lex("$fa");
    REQUIRE(fa[0].kind == TokenKind::SpecialVar);
    REQUIRE(fa[0].text == "$fa");
}

TEST_CASE("Lexer:global $fn assignment", "[lexer]") {
    // $fn = 48;
    auto t = lex("$fn = 48;");
    REQUIRE(t[0].kind == TokenKind::SpecialVar);
    REQUIRE(t[1].kind == TokenKind::Equals);
    REQUIRE(t[2].kind == TokenKind::Number);
    REQUIRE(t[2].numberValue() == 48.0);
    REQUIRE(t[3].kind == TokenKind::Semicolon);
}

// ---------------------------------------------------------------------------
// Punctuation
// ---------------------------------------------------------------------------
TEST_CASE("Lexer:punctuation", "[lexer]") {
    auto t = lex("(){},[];=");
    REQUIRE(t[0].kind == TokenKind::LParen);
    REQUIRE(t[1].kind == TokenKind::RParen);
    REQUIRE(t[2].kind == TokenKind::LBrace);
    REQUIRE(t[3].kind == TokenKind::RBrace);
    REQUIRE(t[4].kind == TokenKind::Comma);
    REQUIRE(t[5].kind == TokenKind::LBracket);
    REQUIRE(t[6].kind == TokenKind::RBracket);
    REQUIRE(t[7].kind == TokenKind::Semicolon);
    REQUIRE(t[8].kind == TokenKind::Equals);
}

// ---------------------------------------------------------------------------
// Comments
// ---------------------------------------------------------------------------
TEST_CASE("Lexer:line comment skipped", "[lexer]") {
    REQUIRE(kinds("// this is a comment\ncube").back() == TokenKind::Cube);
    REQUIRE(kinds("cube // trailing comment") == std::vector{TokenKind::Cube});
}

TEST_CASE("Lexer:block comment skipped", "[lexer]") {
    REQUIRE(kinds("/* comment */ cube") == std::vector{TokenKind::Cube});
    // "cu" and "be" are separate idents — the comment is stripped mid-token
    REQUIRE(kinds("cu/* mid */be") == (std::vector{TokenKind::Ident, TokenKind::Ident}));
}

// ---------------------------------------------------------------------------
// Identifiers (unrecognised words)
// ---------------------------------------------------------------------------
TEST_CASE("Lexer:unknown identifier", "[lexer]") {
    auto t = lex("center");
    REQUIRE(t[0].kind == TokenKind::Ident);
    REQUIRE(t[0].text == "center");
}

// ---------------------------------------------------------------------------
// Source locations
// ---------------------------------------------------------------------------
TEST_CASE("Lexer:source location line tracking", "[lexer]") {
    auto t = lex("cube\nsphere");
    REQUIRE(t[0].loc.line == 0);
    REQUIRE(t[1].loc.line == 1);
}

TEST_CASE("Lexer:source location column tracking", "[lexer]") {
    auto t = lex("cube sphere");
    REQUIRE(t[0].loc.col == 0);
    REQUIRE(t[1].loc.col == 5);
}

// ---------------------------------------------------------------------------
// Real snippet from chiselcad_test.scad
// ---------------------------------------------------------------------------
TEST_CASE("Lexer:translate + cube call", "[lexer]") {
    // translate([0, 0, 0]) cube([10, 10, 10]);
    auto t = lex("translate([0, 0, 0])\n    cube([10, 10, 10]);");
    REQUIRE(t[0].kind == TokenKind::Translate);
    REQUIRE(t[1].kind == TokenKind::LParen);
    REQUIRE(t[2].kind == TokenKind::LBracket);
    REQUIRE(t[3].kind == TokenKind::Number);
    REQUIRE(t[3].numberValue() == 0.0);
}

TEST_CASE("Lexer:difference block header", "[lexer]") {
    // difference() {
    REQUIRE(kinds("difference() {") == std::vector{
        TokenKind::Difference,
        TokenKind::LParen,
        TokenKind::RParen,
        TokenKind::LBrace,
    });
}

TEST_CASE("Lexer:cylinder with named params", "[lexer]") {
    // cylinder(h = 10, r = 5, center = true)
    auto t = lex("cylinder(h = 10, r = 5, center = true)");
    REQUIRE(t[0].kind == TokenKind::Cylinder);
    REQUIRE(t[2].kind == TokenKind::Ident);   // h
    REQUIRE(t[2].text == "h");
    REQUIRE(t[3].kind == TokenKind::Equals);
    REQUIRE(t[4].kind == TokenKind::Number);
    REQUIRE(t[4].numberValue() == 10.0);
}

TEST_CASE("Lexer:$fn per-primitive override", "[lexer]") {
    // sphere(r = 5, $fn = 8)
    // indices: 0=sphere 1=( 2=r 3== 4=5 5=, 6=$fn 7== 8=8 9=)
    auto t = lex("sphere(r = 5, $fn = 8)");
    REQUIRE(t[0].kind == TokenKind::Sphere);
    REQUIRE(t[6].kind == TokenKind::SpecialVar);
    REQUIRE(t[6].text == "$fn");
    REQUIRE(t[8].numberValue() == 8.0);
}

TEST_CASE("Lexer:empty union (edge case from test file)", "[lexer]") {
    // union() {};
    REQUIRE(kinds("union() {};") == std::vector{
        TokenKind::Union,
        TokenKind::LParen,
        TokenKind::RParen,
        TokenKind::LBrace,
        TokenKind::RBrace,
        TokenKind::Semicolon,
    });
}

TEST_CASE("Lexer:Eof token is always last", "[lexer]") {
    Lexer lexer("cube");
    auto tokens = lexer.tokenize();
    REQUIRE(tokens.back().kind == TokenKind::Eof);
}
