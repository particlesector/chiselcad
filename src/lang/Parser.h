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
    void parseInclude(ParseResult& result);

    // ---- geometry nodes ---------------------------------------------------
    AstNodePtr parseNode();
    // The dispatch previously done directly by parseNode(); parseNode() now
    // just consumes a leading # % ! * modifier run (if any) around this.
    AstNodePtr parseNodeInner();
    AstNodePtr parsePrimitive(TokenKind k);
    AstNodePtr parseBoolean(TokenKind k);
    AstNodePtr parseTransform(TokenKind k);
    AstNodePtr parseRender();
    AstNodePtr parseColor();
    AstNodePtr parseIf();
    AstNodePtr parseFor();
    AstNodePtr parseModuleCall();
    AstNodePtr parseExtrusion(TokenKind k);
    AstNodePtr parseOffset();
    AstNodePtr parseProjection();
    AstNodePtr parseAssignNode(); // local assignment inside any block: name = expr;

    // ---- module / function definitions ------------------------------------
    void parseModuleDef(ParseResult& result);
    void parseFunctionDef(ParseResult& result);

    // ---- let statement ---------------------------------------------------
    AstNodePtr parseLetNode();

    // ---- expressions (Pratt parser) --------------------------------------
    ExprPtr parseExpr(int minPrec = 0);
    ExprPtr parseUnary();
    ExprPtr parsePostfix();     // handles postfix [idx] after primary
    ExprPtr parsePrimary();
    ExprPtr parseLetExpr();
    ExprPtr parseFunctionLit();             // function(params) expr — a function-literal value
    VectorElem parseVectorElem();           // one list element: expr, or `each expr`
    ExprPtr parseListComp(SourceLoc loc);   // [for (var = source) body]
    ListCompBodyPtr parseListCompBody();    // body clause: expr / each expr / if (..) body [else body]

    // ---- argument helpers ------------------------------------------------
    void parseParamList(std::unordered_map<std::string, ExprPtr>& params,
                        bool& center);
    void parseExtrusionParams(std::unordered_map<std::string, ExprPtr>& params);

    // ---- child body ------------------------------------------------------
    std::vector<AstNodePtr> parseBody();
    // Statements up to (not including) the closing '}' — shared by parseBody's
    // brace form and parseModuleDef, which also requires a brace block.
    std::vector<AstNodePtr> parseBraceBlock();

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
