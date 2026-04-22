#pragma once
#include "AST.h"
#include "Diagnostic.h"
#include "Expr.h"
#include "Token.h"
#include <span>
#include <vector>

namespace chisel::lang {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens, std::string filePath = "");

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
    void parseAssignment(ParseResult& result);

    // ---- geometry nodes ---------------------------------------------------
    AstNodePtr parseNode();
    AstNodePtr parsePrimitive(TokenKind k);
    AstNodePtr parseBoolean(TokenKind k);
    AstNodePtr parseTransform(TokenKind k);
    AstNodePtr parseIf();
    AstNodePtr parseFor();

    // ---- expressions (Pratt parser) --------------------------------------
    ExprPtr parseExpr(int minPrec = 0);
    ExprPtr parseUnary();
    ExprPtr parsePrimary();
    ExprPtr parseVecExpr(); // parse [x, y, z] → VectorLit ExprPtr

    // ---- argument helpers ------------------------------------------------
    void parseParamList(std::unordered_map<std::string, ExprPtr>& params,
                        bool& center);

    // ---- child body ------------------------------------------------------
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
