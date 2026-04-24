#pragma once
#include <cstdint>
#include <string>

namespace chisel::lang {

// ---------------------------------------------------------------------------
// SourceLoc — zero-based line/column + byte offset into the source string
// ---------------------------------------------------------------------------
struct SourceLoc {
    uint32_t line   = 0; // 0-based
    uint32_t col    = 0; // 0-based
    uint32_t offset = 0; // byte offset from start of source

    bool operator==(const SourceLoc&) const = default;
};

// ---------------------------------------------------------------------------
// TokenKind
// ---------------------------------------------------------------------------
enum class TokenKind : uint8_t {
    // Literals
    Number,   // 3.14  .5  1e3
    True,     // true
    False,    // false

    // Identifiers / special variables
    Ident,    // any unrecognised identifier
    SpecialVar, // $fn  $fs  $fa

    // Primitives
    Cube,
    Sphere,
    Cylinder,

    // Booleans
    Union,
    Difference,
    Intersection,
    Hull,
    Minkowski,

    // Transforms
    Translate,
    Rotate,
    Scale,
    Mirror,

    // Control flow / definitions
    If,       // if
    Else,     // else
    For,      // for
    Module,   // module
    Function, // function
    Let,      // let

    // Literals
    Undef,    // undef

    // 2-D primitives
    Square, Circle, Polygon,
    // Extrusion operations
    LinearExtrude, RotateExtrude,

    // Range separator
    Colon, // :

    // Arithmetic operators
    Plus,         // +
    Minus,        // -
    Star,         // *
    Slash,        // /
    Percent,      // %

    // Comparison / equality
    EqualEqual,   // ==
    BangEqual,    // !=
    Less,         // <
    LessEqual,    // <=
    Greater,      // >
    GreaterEqual, // >=

    // Logical / ternary
    Bang,         // !
    AmpAmp,       // &&
    PipePipe,     // ||
    Question,     // ?

    // Punctuation
    LParen,   // (
    RParen,   // )
    LBrace,   // {
    RBrace,   // }
    LBracket, // [
    RBracket, // ]
    Comma,    // ,
    Semicolon,// ;
    Equals,   // =

    // End of file
    Eof,
};

// ---------------------------------------------------------------------------
// Token
// ---------------------------------------------------------------------------
struct Token {
    TokenKind   kind;
    SourceLoc   loc;
    std::string text; // raw source text of the token (number digits, ident name, etc.)

    // Convenience: return numeric value for Number tokens
    double numberValue() const { return std::stod(text); }
};

} // namespace chisel::lang
