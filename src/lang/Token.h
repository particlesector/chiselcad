#pragma once
#include <cstdint>
#include <string>

namespace chisel::lang {

// ---------------------------------------------------------------------------
// SourceLoc — zero-based line/column + byte offset into the source string
// ---------------------------------------------------------------------------
struct SourceLoc {
    uint32_t line = 0;   // 0-based
    uint32_t col = 0;    // 0-based
    uint32_t offset = 0; // byte offset from start of source
    // Index into the file table a Lexer/Parser pair was constructed with
    // (see SourceLoader.h's LoadedSource::files) — identifies which physical
    // file this location is in once include<>/use<> have spliced multiple
    // files' AST nodes into one merged ParseResult. 0 is both "unset" and
    // (by construction — SourceLoader assigns the root file id 0 first) the
    // root file, so a default-constructed SourceLoc still resolves sensibly.
    uint32_t fileId = 0;

    bool operator==(const SourceLoc&) const = default;
};

// ---------------------------------------------------------------------------
// TokenKind
// ---------------------------------------------------------------------------
enum class TokenKind : uint8_t {
    // Literals
    Number, // 3.14  .5  1e3
    String, // "hello"
    True,   // true
    False,  // false

    // Identifiers / special variables
    Ident,      // any unrecognised identifier
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
    Multmatrix,
    Render,
    Color,

    // Control flow / definitions
    If,       // if
    Else,     // else
    For,      // for
    Each,     // each
    Module,   // module
    Function, // function
    Let,      // let

    // File inclusion
    Include,    // include
    Use,        // use
    AngledPath, // <path/to/file.scad> — raw text scanned right after Include/Use

    // Literals
    Undef, // undef

    // 2-D primitives
    Square,
    Circle,
    Polygon,
    // Extrusion operations
    LinearExtrude,
    RotateExtrude,
    // 2-D → 2-D operations
    Offset,
    // 3-D → 2-D operations
    Projection,

    // Range separator
    Colon, // :

    // Arithmetic operators
    Plus,    // +
    Minus,   // -
    Star,    // *
    Slash,   // /
    Percent, // %

    // CSG modifier prefix — only '#' needs its own token; '%'/'*'/'!' above
    // double as this when the Parser finds them in statement-start position
    // (see Modifier in AST.h).
    Hash, // #

    // Comparison / equality
    EqualEqual,   // ==
    BangEqual,    // !=
    Less,         // <
    LessEqual,    // <=
    Greater,      // >
    GreaterEqual, // >=

    // Logical / ternary
    Bang,     // !
    AmpAmp,   // &&
    PipePipe, // ||
    Question, // ?

    // Punctuation
    LParen,    // (
    RParen,    // )
    LBrace,    // {
    RBrace,    // }
    LBracket,  // [
    RBracket,  // ]
    Comma,     // ,
    Semicolon, // ;
    Equals,    // =

    // End of file
    Eof,
};

// ---------------------------------------------------------------------------
// Token
// ---------------------------------------------------------------------------
struct Token {
    TokenKind kind;
    SourceLoc loc;
    std::string text; // raw source text of the token (number digits, ident name, etc.)

    // Convenience: return numeric value for Number tokens
    double numberValue() const { return std::stod(text); }
};

} // namespace chisel::lang
