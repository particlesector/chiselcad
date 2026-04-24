#include "Parser.h"
#include <cassert>
#include <cmath>
#include <stdexcept>

namespace chisel::lang {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Parser::Parser(std::vector<Token> tokens, std::string filePath)
    : m_tokens(std::move(tokens)), m_filePath(std::move(filePath)) {
    if (m_tokens.empty() || m_tokens.back().kind != TokenKind::Eof)
        m_tokens.push_back({TokenKind::Eof, {}, ""});
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
ParseResult Parser::parse() {
    ParseResult result;
    result.globalFs = 2.0;
    result.globalFa = 12.0;

    while (!atEnd()) {
        parseStatement(result);
    }
    return result;
}

bool Parser::hasErrors() const {
    for (const auto& d : m_diags)
        if (d.level == DiagLevel::Error) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Token navigation
// ---------------------------------------------------------------------------
const Token& Parser::peek(int offset) const {
    size_t idx = m_pos + static_cast<size_t>(offset);
    if (idx >= m_tokens.size()) return m_tokens.back();
    return m_tokens[idx];
}

const Token& Parser::advance() {
    if (!atEnd()) ++m_pos;
    return m_tokens[m_pos - 1];
}

bool Parser::match(TokenKind k) {
    if (check(k)) { advance(); return true; }
    return false;
}

const Token& Parser::expect(TokenKind k, const char* msg) {
    if (check(k)) return advance();
    addError(msg, peek().loc);
    return peek();
}

// ---------------------------------------------------------------------------
// Top-level statement dispatch
// ---------------------------------------------------------------------------
void Parser::parseStatement(ParseResult& result) {
    // Special variable assignment: $fn = 48;
    if (check(TokenKind::SpecialVar)) {
        parseSpecialVarAssignment(result);
        return;
    }

    // Module definition: module name(...) { ... }
    if (check(TokenKind::Module)) {
        parseModuleDef(result);
        return;
    }

    // Variable assignment: ident = expr;  (no '(' follows the ident)
    if (check(TokenKind::Ident) && peek(1).kind == TokenKind::Equals) {
        parseAssignment(result);
        return;
    }

    // Geometry node
    auto node = parseNode();
    if (node) {
        result.roots.push_back(std::move(node));
    } else {
        if (!atEnd()) advance(); // skip unrecognised token
    }
    match(TokenKind::Semicolon);
}

void Parser::parseSpecialVarAssignment(ParseResult& result) {
    const Token& var = advance(); // $fn / $fs / $fa
    expect(TokenKind::Equals, "expected '=' after special variable");
    // Special vars are always numeric literals in practice
    auto expr = parseExpr();
    match(TokenKind::Semicolon);

    // Evaluate immediately — special vars must be compile-time constants
    if (auto* lit = std::get_if<NumberLit>(expr.get())) {
        if      (var.text == "$fn") result.globalFn = lit->value;
        else if (var.text == "$fs") result.globalFs = lit->value;
        else if (var.text == "$fa") result.globalFa = lit->value;
    }
    // Non-literal $fn/$fs/$fa silently ignored for now (V2b will handle)
}

void Parser::parseAssignment(ParseResult& result) {
    const Token& name_tok = advance(); // identifier
    advance();                         // consume '='
    auto value = parseExpr();
    match(TokenKind::Semicolon);
    result.assignments.push_back({name_tok.text, std::move(value), name_tok.loc});
}

// ---------------------------------------------------------------------------
// Node dispatch
// ---------------------------------------------------------------------------
AstNodePtr Parser::parseNode() {
    TokenKind k = peek().kind;

    switch (k) {
    case TokenKind::Cube:
    case TokenKind::Sphere:
    case TokenKind::Cylinder:
    case TokenKind::Square:
    case TokenKind::Circle:
    case TokenKind::Polygon:
        return parsePrimitive(k);

    case TokenKind::LinearExtrude:
    case TokenKind::RotateExtrude:
        return parseExtrusion(k);

    case TokenKind::Union:
    case TokenKind::Difference:
    case TokenKind::Intersection:
    case TokenKind::Hull:
    case TokenKind::Minkowski:
        return parseBoolean(k);

    case TokenKind::Translate:
    case TokenKind::Rotate:
    case TokenKind::Scale:
    case TokenKind::Mirror:
        return parseTransform(k);

    case TokenKind::If:
        return parseIf();

    case TokenKind::For:
        return parseFor();

    case TokenKind::Ident:
        // Could be a module call: name(args) { ... }
        if (peek(1).kind == TokenKind::LParen)
            return parseModuleCall();
        return nullptr;

    default:
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------
AstNodePtr Parser::parsePrimitive(TokenKind k) {
    const Token& kw = advance();
    PrimitiveNode node;
    node.loc = kw.loc;

    switch (k) {
    case TokenKind::Cube:     node.kind = PrimitiveNode::Kind::Cube;     break;
    case TokenKind::Sphere:   node.kind = PrimitiveNode::Kind::Sphere;   break;
    case TokenKind::Cylinder: node.kind = PrimitiveNode::Kind::Cylinder; break;
    case TokenKind::Square:   node.kind = PrimitiveNode::Kind::Square2D; break;
    case TokenKind::Circle:   node.kind = PrimitiveNode::Kind::Circle2D; break;
    case TokenKind::Polygon:  node.kind = PrimitiveNode::Kind::Polygon2D; break;
    default: break;
    }

    expect(TokenKind::LParen, "expected '(' after primitive name");
    parseParamList(node.params, node.center);
    expect(TokenKind::RParen, "expected ')' after primitive arguments");

    return makePrimitive(std::move(node));
}

// ---------------------------------------------------------------------------
// Booleans
// ---------------------------------------------------------------------------
AstNodePtr Parser::parseBoolean(TokenKind k) {
    const Token& kw = advance();
    BooleanNode node;
    node.loc = kw.loc;

    switch (k) {
    case TokenKind::Union:        node.op = BooleanNode::Op::Union;        break;
    case TokenKind::Difference:   node.op = BooleanNode::Op::Difference;   break;
    case TokenKind::Intersection: node.op = BooleanNode::Op::Intersection; break;
    case TokenKind::Hull:         node.op = BooleanNode::Op::Hull;         break;
    case TokenKind::Minkowski:    node.op = BooleanNode::Op::Minkowski;    break;
    default: break;
    }

    expect(TokenKind::LParen,  "expected '(' after boolean operator");
    expect(TokenKind::RParen,  "expected ')' after boolean operator");

    node.children = parseBody();
    return makeBoolean(std::move(node));
}

// ---------------------------------------------------------------------------
// Transforms
// ---------------------------------------------------------------------------
AstNodePtr Parser::parseTransform(TokenKind k) {
    const Token& kw = advance();
    TransformNode node;
    node.loc = kw.loc;

    switch (k) {
    case TokenKind::Translate: node.kind = TransformNode::Kind::Translate; break;
    case TokenKind::Rotate:    node.kind = TransformNode::Kind::Rotate;    break;
    case TokenKind::Scale:     node.kind = TransformNode::Kind::Scale;     break;
    case TokenKind::Mirror:    node.kind = TransformNode::Kind::Mirror;    break;
    default: break;
    }

    expect(TokenKind::LParen, "expected '(' after transform name");
    node.vec = parseVecExpr();
    expect(TokenKind::RParen, "expected ')' after transform vector");

    node.children = parseBody();
    return makeTransform(std::move(node));
}

// ---------------------------------------------------------------------------
// if / else
// ---------------------------------------------------------------------------
AstNodePtr Parser::parseIf() {
    const Token& kw = advance(); // consume 'if'
    IfNode node;
    node.loc = kw.loc;

    expect(TokenKind::LParen, "expected '(' after 'if'");
    node.condition = parseExpr();
    expect(TokenKind::RParen, "expected ')' after condition");

    node.thenChildren = parseBody();

    if (check(TokenKind::Else)) {
        advance(); // consume 'else'
        node.elseChildren = parseBody();
    }

    return makeIf(std::move(node));
}

// ---------------------------------------------------------------------------
// for — for (var = [start:step:end]) or for (var = [v0, v1, ...])
// ---------------------------------------------------------------------------
AstNodePtr Parser::parseFor() {
    const Token& kw = advance(); // consume 'for'
    ForNode node;
    node.loc = kw.loc;

    expect(TokenKind::LParen, "expected '(' after 'for'");
    node.var = expect(TokenKind::Ident, "expected loop variable").text;
    expect(TokenKind::Equals, "expected '=' after loop variable");
    expect(TokenKind::LBracket, "expected '[' for range/list");

    // Parse first expression — determines range vs list form
    auto first = parseExpr();

    if (check(TokenKind::Colon)) {
        // Range form: [start : end] or [start : step : end]
        advance(); // consume ':'
        auto second = parseExpr();
        if (check(TokenKind::Colon)) {
            advance(); // consume ':'
            auto third = parseExpr();
            // [first:second:third] = [start:step:end]
            node.range.isRange = true;
            node.range.start   = std::move(first);
            node.range.step    = std::move(second);
            node.range.end     = std::move(third);
        } else {
            // [first:second] = [start:end], implicit step of 1
            node.range.isRange = true;
            node.range.start   = std::move(first);
            node.range.end     = std::move(second);
        }
    } else {
        // List form: [first, ...]
        node.range.isRange = false;
        node.range.list.push_back(std::move(first));
        while (match(TokenKind::Comma)) {
            if (check(TokenKind::RBracket)) break;
            node.range.list.push_back(parseExpr());
        }
    }

    expect(TokenKind::RBracket, "expected ']' after range/list");
    expect(TokenKind::RParen,   "expected ')' after for header");

    node.children = parseBody();
    return makeFor(std::move(node));
}

// ---------------------------------------------------------------------------
// parseVecExpr — parse a [x, y, z] literal into a VectorLit ExprPtr
// ---------------------------------------------------------------------------
ExprPtr Parser::parseVecExpr() {
    SourceLoc loc = peek().loc;
    if (!match(TokenKind::LBracket)) {
        addError("expected '[' for vector argument", peek().loc);
        VectorLit vlit;
        vlit.loc = loc;
        for (int i = 0; i < 3; ++i)
            vlit.elements.push_back(makeExpr(NumberLit{0.0, loc}));
        return makeExpr(std::move(vlit));
    }

    VectorLit vlit;
    vlit.loc = loc;
    for (int i = 0; i < 3; ++i) {
        if (check(TokenKind::RBracket)) {
            // Fewer than 3 components — fill rest with 0
            vlit.elements.push_back(makeExpr(NumberLit{0.0, peek().loc}));
            continue;
        }
        vlit.elements.push_back(parseExpr());
        if (i < 2) match(TokenKind::Comma);
    }
    expect(TokenKind::RBracket, "expected ']' after vector");
    return makeExpr(std::move(vlit));
}

// ---------------------------------------------------------------------------
// parseParamList — named and positional params → ExprPtr map + center flag
// ---------------------------------------------------------------------------
void Parser::parseParamList(std::unordered_map<std::string, ExprPtr>& params,
                             bool& center) {
    center = false;

    while (!check(TokenKind::RParen) && !atEnd()) {
        const size_t prevPos = m_pos; // guard against zero-progress infinite loops

        // Special variable override: $fn = expr
        if (check(TokenKind::SpecialVar)) {
            std::string name = peek().text;
            advance();
            expect(TokenKind::Equals, "expected '=' after special variable");
            params[name] = parseExpr();
            match(TokenKind::Comma);
            continue;
        }

        // Named param: ident = expr
        if (check(TokenKind::Ident) && peek(1).kind == TokenKind::Equals) {
            std::string name = peek().text;
            advance(); // ident
            advance(); // =

            if (name == "center") {
                if (check(TokenKind::True))       { advance(); center = true;  }
                else if (check(TokenKind::False)) { advance(); center = false; }
                else {
                    // Expression — evaluate it; treat non-zero as true
                    auto expr = parseExpr();
                    if (auto* lit = std::get_if<NumberLit>(expr.get()))
                        center = (lit->value != 0.0);
                    else if (auto* bl = std::get_if<BoolLit>(expr.get()))
                        center = bl->value;
                }
            } else {
                params[name] = parseExpr();
            }
            match(TokenKind::Comma);
            continue;
        }

        // Positional vector [x, y, z] — store as "x","y","z" params
        if (check(TokenKind::LBracket)) {
            advance(); // [
            for (int i = 0; i < 3 && !check(TokenKind::RBracket); ++i) {
                static const char* keys[] = {"x", "y", "z"};
                params[keys[i]] = parseExpr();
                if (i < 2) match(TokenKind::Comma);
            }
            expect(TokenKind::RBracket, "expected ']'");
            match(TokenKind::Comma);
            continue;
        }

        // Positional number/expression — treat as _pos0
        if (!check(TokenKind::RParen)) {
            params["_pos0"] = parseExpr();
            match(TokenKind::Comma);
        }

        if (m_pos == prevPos) break; // no token consumed — stop to avoid infinite loop
    }
}

// ---------------------------------------------------------------------------
// Expression parser (Pratt / precedence climbing)
// ---------------------------------------------------------------------------
static int infixPrec(TokenKind k) {
    switch (k) {
    case TokenKind::PipePipe:    return 1;
    case TokenKind::AmpAmp:      return 2;
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:   return 3;
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual: return 4;
    case TokenKind::Plus:
    case TokenKind::Minus:        return 5;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:      return 6;
    default: return -1;
    }
}

static BinaryExpr::Op tokenToBinaryOp(TokenKind k) {
    switch (k) {
    case TokenKind::Plus:         return BinaryExpr::Op::Add;
    case TokenKind::Minus:        return BinaryExpr::Op::Sub;
    case TokenKind::Star:         return BinaryExpr::Op::Mul;
    case TokenKind::Slash:        return BinaryExpr::Op::Div;
    case TokenKind::Percent:      return BinaryExpr::Op::Mod;
    case TokenKind::EqualEqual:   return BinaryExpr::Op::Eq;
    case TokenKind::BangEqual:    return BinaryExpr::Op::Ne;
    case TokenKind::Less:         return BinaryExpr::Op::Lt;
    case TokenKind::LessEqual:    return BinaryExpr::Op::Le;
    case TokenKind::Greater:      return BinaryExpr::Op::Gt;
    case TokenKind::GreaterEqual: return BinaryExpr::Op::Ge;
    case TokenKind::AmpAmp:       return BinaryExpr::Op::And;
    case TokenKind::PipePipe:     return BinaryExpr::Op::Or;
    default: return BinaryExpr::Op::Add; // unreachable
    }
}

ExprPtr Parser::parseExpr(int minPrec) {
    auto lhs = parseUnary();

    while (true) {
        int prec = infixPrec(peek().kind);
        if (prec < minPrec) break;

        const Token& op_tok = advance();
        SourceLoc    loc     = op_tok.loc;
        auto rhs = parseExpr(prec + 1); // left-associative

        BinaryExpr bin;
        bin.op    = tokenToBinaryOp(op_tok.kind);
        bin.left  = std::move(lhs);
        bin.right = std::move(rhs);
        bin.loc   = loc;
        lhs = makeExpr(std::move(bin));
    }
    return lhs;
}

ExprPtr Parser::parseUnary() {
    if (check(TokenKind::Minus)) {
        const Token& tok = advance();
        auto operand = parseUnary();
        return makeExpr(UnaryExpr{UnaryExpr::Op::Neg, std::move(operand), tok.loc});
    }
    if (check(TokenKind::Bang)) {
        const Token& tok = advance();
        auto operand = parseUnary();
        return makeExpr(UnaryExpr{UnaryExpr::Op::Not, std::move(operand), tok.loc});
    }
    return parsePrimary();
}

ExprPtr Parser::parsePrimary() {
    // Number literal
    if (check(TokenKind::Number)) {
        const Token& tok = advance();
        return makeExpr(NumberLit{tok.numberValue(), tok.loc});
    }
    // Bool literals
    if (check(TokenKind::True)) {
        SourceLoc loc = advance().loc;
        return makeExpr(BoolLit{true, loc});
    }
    if (check(TokenKind::False)) {
        SourceLoc loc = advance().loc;
        return makeExpr(BoolLit{false, loc});
    }
    // Parenthesised expression
    if (match(TokenKind::LParen)) {
        auto expr = parseExpr();
        expect(TokenKind::RParen, "expected ')'");
        return expr;
    }
    // Vector literal [e0, e1, ...]
    if (check(TokenKind::LBracket)) {
        SourceLoc loc = advance().loc; // consume [
        VectorLit vlit;
        vlit.loc = loc;
        while (!check(TokenKind::RBracket) && !atEnd()) {
            vlit.elements.push_back(parseExpr());
            if (!match(TokenKind::Comma)) break;
        }
        expect(TokenKind::RBracket, "expected ']'");
        return makeExpr(std::move(vlit));
    }
    // Identifier — variable reference or function call
    if (check(TokenKind::Ident)) {
        const Token& name_tok = advance();
        if (match(TokenKind::LParen)) {
            // Function call: name(arg, arg, ...)
            FunctionCall fc;
            fc.name = name_tok.text;
            fc.loc  = name_tok.loc;
            while (!check(TokenKind::RParen) && !atEnd()) {
                fc.args.push_back(parseExpr());
                if (!match(TokenKind::Comma)) break;
            }
            expect(TokenKind::RParen, "expected ')' after function arguments");
            return makeExpr(std::move(fc));
        }
        return makeExpr(VarRef{name_tok.text, name_tok.loc});
    }
    addError("expected expression", peek().loc);
    return makeExpr(NumberLit{0.0, peek().loc});
}

// ---------------------------------------------------------------------------
// module — module name(param, param = default, ...) { body }
// ---------------------------------------------------------------------------
void Parser::parseModuleDef(ParseResult& result) {
    const Token& kw = advance(); // consume 'module'
    ModuleDef def;
    def.loc  = kw.loc;
    def.name = expect(TokenKind::Ident, "expected module name").text;

    expect(TokenKind::LParen, "expected '(' after module name");
    while (!check(TokenKind::RParen) && !atEnd()) {
        ModuleParam param;
        param.name = expect(TokenKind::Ident, "expected parameter name").text;
        if (match(TokenKind::Equals))
            param.defaultVal = parseExpr();
        def.params.push_back(std::move(param));
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RParen, "expected ')' after parameter list");

    // Body must be a brace block for module definitions
    expect(TokenKind::LBrace, "expected '{' for module body");
    while (!check(TokenKind::RBrace) && !atEnd()) {
        auto child = parseNode();
        if (child) {
            def.body.push_back(std::move(child));
        } else if (check(TokenKind::Semicolon)) {
            advance();
        } else if (!check(TokenKind::RBrace)) {
            // Handle assignments inside module body
            if (check(TokenKind::Ident) && peek(1).kind == TokenKind::Equals) {
                // Variable assignment in module body — ignore for now (not scope-captured)
                advance(); advance(); parseExpr(); match(TokenKind::Semicolon);
            } else {
                synchronize();
            }
        }
    }
    expect(TokenKind::RBrace, "expected '}' to close module body");

    result.moduleDefs.push_back(std::move(def));
}

// ---------------------------------------------------------------------------
// module call — name(arg, name = arg, ...) { optional children }
// ---------------------------------------------------------------------------
AstNodePtr Parser::parseModuleCall() {
    const Token& name_tok = advance(); // consume identifier
    ModuleCallNode node;
    node.loc  = name_tok.loc;
    node.name = name_tok.text;

    expect(TokenKind::LParen, "expected '(' in module call");
    while (!check(TokenKind::RParen) && !atEnd()) {
        ModuleArg arg;
        // Named argument: ident = expr
        if (check(TokenKind::Ident) && peek(1).kind == TokenKind::Equals) {
            arg.name = advance().text; // ident
            advance();                 // =
        }
        arg.value = parseExpr();
        node.args.push_back(std::move(arg));
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RParen, "expected ')' after module arguments");

    // Optional children body (not yet used by the evaluator)
    if (check(TokenKind::LBrace) || (!check(TokenKind::Semicolon) && !atEnd() && peek().kind != TokenKind::RBrace)) {
        node.children = parseBody();
    } else {
        match(TokenKind::Semicolon);
    }

    return makeModuleCall(std::move(node));
}

// ---------------------------------------------------------------------------
// Extrusions — linear_extrude / rotate_extrude
// ---------------------------------------------------------------------------
AstNodePtr Parser::parseExtrusion(TokenKind k) {
    const Token& kw = advance();
    ExtrusionNode node;
    node.loc  = kw.loc;
    node.kind = (k == TokenKind::LinearExtrude) ? ExtrusionNode::Kind::Linear
                                                 : ExtrusionNode::Kind::Rotate;

    expect(TokenKind::LParen, "expected '(' after extrude keyword");
    parseExtrusionParams(node.params);
    expect(TokenKind::RParen, "expected ')' after extrude params");

    node.children = parseBody();
    return makeExtrusion(std::move(node));
}

// Parses key=value params for linear/rotate_extrude.  Unlike parseParamList,
// all values are kept as ExprPtr (including center and scale vectors).
void Parser::parseExtrusionParams(std::unordered_map<std::string, ExprPtr>& params) {
    while (!check(TokenKind::RParen) && !atEnd()) {
        const size_t prevPos = m_pos;

        if (check(TokenKind::SpecialVar)) {
            std::string name = peek().text;
            advance();
            expect(TokenKind::Equals, "expected '='");
            params[name] = parseExpr();
            match(TokenKind::Comma);
            continue;
        }
        if (check(TokenKind::Ident) && peek(1).kind == TokenKind::Equals) {
            std::string name = advance().text;
            advance(); // consume '='
            params[name] = parseExpr();
            match(TokenKind::Comma);
            continue;
        }
        // Positional scalar (height for linear_extrude)
        if (!check(TokenKind::RParen)) {
            params["_pos0"] = parseExpr();
            match(TokenKind::Comma);
        }
        if (m_pos == prevPos) break;
    }
}

// ---------------------------------------------------------------------------
// parseBody: { children* } or single child node
// ---------------------------------------------------------------------------
std::vector<AstNodePtr> Parser::parseBody() {
    std::vector<AstNodePtr> children;

    if (match(TokenKind::LBrace)) {
        while (!check(TokenKind::RBrace) && !atEnd()) {
            auto child = parseNode();
            if (child) {
                children.push_back(std::move(child));
            } else if (check(TokenKind::Semicolon)) {
                advance();
            } else if (!check(TokenKind::RBrace)) {
                synchronize();
            }
        }
        expect(TokenKind::RBrace, "expected '}' to close block");
        match(TokenKind::Semicolon);
    } else {
        auto child = parseNode();
        if (child) children.push_back(std::move(child));
        match(TokenKind::Semicolon);
    }

    return children;
}

// ---------------------------------------------------------------------------
// Error recovery
// ---------------------------------------------------------------------------
void Parser::synchronize() {
    while (!atEnd()) {
        TokenKind k = peek().kind;
        if (k == TokenKind::RBrace || k == TokenKind::Semicolon) return;
        advance();
    }
}

void Parser::addError(const std::string& msg, SourceLoc loc) {
    m_diags.push_back({DiagLevel::Error, msg, loc, m_filePath});
}

} // namespace chisel::lang
