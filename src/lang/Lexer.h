#pragma once
#include "Diagnostic.h"
#include "Token.h"
#include <string>
#include <string_view>
#include <vector>

namespace chisel::lang {

// Tokenises a complete OpenSCAD source string.
// All tokens are returned in one pass; diagnostics are accumulated and
// available after tokenize() returns. Lex errors do not throw.
class Lexer {
public:
    explicit Lexer(std::string_view source, std::string filePath = "");

    // Run the lexer; returns the full token stream including a final Eof token.
    std::vector<Token> tokenize();

    const DiagList& diagnostics() const { return m_diags; }
    bool hasErrors() const;

private:
    // ---- source navigation ------------------------------------------------
    char peek(int offset = 0) const;
    char advance();
    bool match(char expected);
    bool atEnd() const { return m_pos >= m_source.size(); }

    // ---- token factories --------------------------------------------------
    Token makeToken(TokenKind kind, uint32_t startOffset) const;
    Token makeToken(TokenKind kind, uint32_t startOffset, std::string text) const;

    // ---- scanners ---------------------------------------------------------
    Token scanNumber(uint32_t startOffset);
    Token scanIdentOrKeyword(uint32_t startOffset);
    Token scanSpecialVar(uint32_t startOffset);
    void  skipLineComment();
    void  skipBlockComment();

    void addError(const std::string& msg, SourceLoc loc);

    // ---- state ------------------------------------------------------------
    std::string_view m_source;
    std::string      m_filePath;
    size_t           m_pos  = 0;
    uint32_t         m_line = 0;
    uint32_t         m_col  = 0;
    DiagList         m_diags;
};

} // namespace chisel::lang
