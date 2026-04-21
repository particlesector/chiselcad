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
    // Guarantee at least one Eof token so peek() is always safe
    if (m_tokens.empty() || m_tokens.back().kind != TokenKind::Eof)
        m_tokens.push_back({TokenKind::Eof, {}, ""});
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
ParseResult Parser::parse() {
    ParseResult result;
    // defaults
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
    if (idx >= m_tokens.size()) return m_tokens.back(); // Eof
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
    // return current token without advancing so caller can see what's there
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

    // Any node-producing statement
    auto node = parseNode();
    if (node) {
        result.roots.push_back(std::move(node));
    } else {
        // Nothing was recognised — skip one token to avoid infinite loop
        if (!atEnd()) advance();
    }
    // Optional trailing semicolon at the top level
    match(TokenKind::Semicolon);
}

void Parser::parseSpecialVarAssignment(ParseResult& result) {
    const Token& var = advance(); // consume $fn / $fs / $fa
    expect(TokenKind::Equals, "expected '=' after special variable");
    double val = parseNumber();
    match(TokenKind::Semicolon);

    if      (var.text == "$fn") result.globalFn = val;
    else if (var.text == "$fs") result.globalFs = val;
    else if (var.text == "$fa") result.globalFa = val;
    // unknown special var — silently ignore
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
        return parsePrimitive(k);

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
    default: break;
    }

    // Parse argument list
    expect(TokenKind::LParen, "expected '(' after primitive name");
    node.params = parseParamList(node.center);
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

    // () — empty param list required by OpenSCAD syntax
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
    parseVec3(node.x, node.y, node.z);
    expect(TokenKind::RParen, "expected ')' after transform vector");

    node.children = parseBody();
    return makeTransform(std::move(node));
}

// ---------------------------------------------------------------------------
// parseVec3: consume [x, y, z]
// ---------------------------------------------------------------------------
void Parser::parseVec3(double& x, double& y, double& z) {
    x = y = z = 0.0;
    if (!match(TokenKind::LBracket)) {
        addError("expected '[' for vector argument", peek().loc);
        return;
    }
    // Parse up to 3 comma-separated numbers
    double* components[3] = {&x, &y, &z};
    for (int i = 0; i < 3; ++i) {
        if (check(TokenKind::RBracket)) break;
        *components[i] = parseNumber();
        if (i < 2 && !match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBracket, "expected ']' after vector");
}

// ---------------------------------------------------------------------------
// parseParamList: (name = value, ...) — handles both named and positional
// For cube:     ([x,y,z]) or ([x,y,z], center=true) or (size=[x,y,z])
// For sphere:   (r=5) or (5)
// For cylinder: (h=10, r=5) or (h=10, r1=3, r2=1, center=true)
// Also handles per-node $fn/$fs/$fa overrides.
// ---------------------------------------------------------------------------
std::unordered_map<std::string, double> Parser::parseParamList(bool& center) {
    std::unordered_map<std::string, double> params;
    center = false;

    while (!check(TokenKind::RParen) && !atEnd()) {
        // Special variable override: $fn = 64
        if (check(TokenKind::SpecialVar)) {
            std::string name = peek().text;
            advance();
            expect(TokenKind::Equals, "expected '=' after special variable");
            params[name] = parseNumber();
            match(TokenKind::Comma);
            continue;
        }

        // Named param: ident = value
        if (check(TokenKind::Ident) &&
            peek(1).kind == TokenKind::Equals) {
            std::string name = peek().text;
            advance(); // consume ident
            advance(); // consume '='

            if (name == "center") {
                // center = true / false
                if (check(TokenKind::True))  { advance(); center = true;  }
                else if (check(TokenKind::False)) { advance(); center = false; }
                else { center = (parseNumber() != 0.0); }
            } else {
                params[name] = parseNumber();
            }
            match(TokenKind::Comma);
            continue;
        }

        // Positional vector [x, y, z] — treat as "size" for cube
        if (check(TokenKind::LBracket)) {
            double x, y, z;
            parseVec3(x, y, z);
            params["x"] = x; params["y"] = y; params["z"] = z;
            match(TokenKind::Comma);
            continue;
        }

        // Positional number — treat as first unnamed param (e.g. sphere(5))
        if (check(TokenKind::Number)) {
            params["_pos0"] = parseNumber();
            match(TokenKind::Comma);
            continue;
        }

        // Unrecognised — stop to avoid spinning
        break;
    }
    return params;
}

// ---------------------------------------------------------------------------
// parseNumber: consume a Number token (or negative number)
// ---------------------------------------------------------------------------
double Parser::parseNumber() {
    if (check(TokenKind::Number)) {
        double val = peek().numberValue();
        advance();
        return val;
    }
    addError("expected a number", peek().loc);
    return 0.0;
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
                advance(); // empty statement
            } else if (!check(TokenKind::RBrace)) {
                synchronize();
            }
        }
        expect(TokenKind::RBrace, "expected '}' to close block");
        match(TokenKind::Semicolon); // optional trailing ;
    } else {
        // Single child node (no braces)
        auto child = parseNode();
        if (child) children.push_back(std::move(child));
        match(TokenKind::Semicolon);
    }

    return children;
}

// ---------------------------------------------------------------------------
// Error recovery: skip to the next '}' or ';'
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
