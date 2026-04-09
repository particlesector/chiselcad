#pragma once
#include "AST.h"
#include "Diagnostic.h"
#include "Token.h"
#include <span>
#include <vector>

namespace chisel::lang {

// Recursive descent parser for the ChiselCAD OpenSCAD subset.
// Consumes a token stream produced by Lexer and builds an AST.
// Errors are accumulated as diagnostics; parsing continues after errors
// by skipping to the next synchronisation point ('}' or ';').
class Parser {
public:
    explicit Parser(std::vector<Token> tokens, std::string filePath = "");

    // Run the parser. Returns the result even on partial parse.
    ParseResult parse();

    const DiagList& diagnostics() const { return m_diags; }
    bool hasErrors() const;

private:
    // ---- token navigation -------------------------------------------------
    const Token& peek(int offset = 0) const;
    const Token& advance();
    bool check(TokenKind k) const { return peek().kind == k; }
    bool match(TokenKind k);
    const Token& expect(TokenKind k, const char* msg);
    bool atEnd() const { return peek().kind == TokenKind::Eof; }

    // ---- top-level --------------------------------------------------------
    void parseStatement(ParseResult& result);
    void parseSpecialVarAssignment(ParseResult& result);

    // ---- expressions / nodes ---------------------------------------------
    AstNodePtr parseNode();
    AstNodePtr parsePrimitive(TokenKind k);
    AstNodePtr parseBoolean(TokenKind k);
    AstNodePtr parseTransform(TokenKind k);

    // ---- argument helpers ------------------------------------------------
    // Parse a [x, y, z] vector; fills out/out/out. Missing components = 0.
    void parseVec3(double& x, double& y, double& z);
    // Parse a named-parameter list: (name = value, ...) or positional for
    // primitives. Returns map of param name -> value.
    std::unordered_map<std::string, double> parseParamList(bool& center);
    double parseNumber(); // consume a Number token and return its value

    // ---- child body ------------------------------------------------------
    // Parse { stmt* } or a single child node
    std::vector<AstNodePtr> parseBody();

    // ---- error recovery --------------------------------------------------
    void synchronize();
    void addError(const std::string& msg, SourceLoc loc);

    // ---- state ------------------------------------------------------------
    std::vector<Token> m_tokens;
    std::string        m_filePath;
    size_t             m_pos = 0;
    DiagList           m_diags;
};

} // namespace chisel::lang
