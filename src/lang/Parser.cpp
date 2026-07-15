#include "Parser.h"
#include <cassert>
#include <cmath>
#include <stdexcept>

namespace chisel::lang {

// A token can be used as a named-parameter name (e.g. `scale=`) if it's an
// identifier or a keyword scanned as one (both carry non-empty `.text`).
// Number/String literals also carry non-empty `.text` but must NOT be
// accepted as param names — `cube(3=5)` should be a syntax error, not a
// named param called "3".
static bool isParamNameToken(const Token& t) {
    return !t.text.empty() && t.kind != TokenKind::Number && t.kind != TokenKind::String &&
           t.kind != TokenKind::SpecialVar;
}

// A CSG modifier character (# % ! *) — only meaningful at statement-start,
// where Percent/Star/Bang double as this instead of their expression-operator
// meaning. Shared by parseNode() (consumes the run) and parseStatement()'s
// assignment lookahead (needs to see past it without consuming yet).
static bool isModifierToken(TokenKind k) {
    return k == TokenKind::Hash || k == TokenKind::Percent ||
           k == TokenKind::Star || k == TokenKind::Bang;
}

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

    // Function definition: function name(params) = expr;
    if (check(TokenKind::Function)) {
        parseFunctionDef(result);
        return;
    }

    // File inclusion: include <path>; / use <path>;
    if (check(TokenKind::Include) || check(TokenKind::Use)) {
        parseInclude(result);
        return;
    }

    // Variable assignment: ident = expr;  (no '(' follows the ident)
    if (check(TokenKind::Ident) && peek(1).kind == TokenKind::Equals) {
        parseAssignment(result);
        return;
    }

    // A leading run of CSG modifier characters (# % ! *) followed by an
    // assignment (`#x = 2;`) isn't valid OpenSCAD — modifiers only apply to
    // module instantiations — but falling through to parseNode() below would
    // still *perform* the assignment, just via result.roots (evaluated
    // in-place during the CSG tree walk) instead of result.assignments
    // (hoisted by Interpreter::loadAssignments() before any geometry runs).
    // That silently breaks "last assignment wins" ordering against a plain
    // top-level assignment of the same variable elsewhere in the file, so
    // detect it here and route it through the normal hoisted path instead,
    // with a diagnostic that the modifier itself has no effect.
    if (isModifierToken(peek().kind)) {
        size_t look = 0;
        while (isModifierToken(peek(static_cast<int>(look)).kind)) ++look;
        if (peek(static_cast<int>(look)).kind == TokenKind::Ident &&
            peek(static_cast<int>(look) + 1).kind == TokenKind::Equals) {
            SourceLoc modLoc = peek().loc;
            for (size_t i = 0; i < look; ++i) advance();
            addError("CSG modifier characters ('#', '%', '!', '*') cannot prefix a variable assignment", modLoc);
            parseAssignment(result);
            return;
        }
    }

    // Geometry node
    auto node = parseNode();
    if (node) {
        result.roots.push_back(std::move(node));
    } else if (!atEnd()) {
        addError("unexpected token '" + peek().text + "' at statement position", peek().loc);
        advance(); // skip unrecognised token
    }
    match(TokenKind::Semicolon);
}

void Parser::parseSpecialVarAssignment(ParseResult& result) {
    const Token& var = advance(); // $fn / $fs / $fa
    expect(TokenKind::Equals, "expected '=' after special variable");
    auto expr = parseExpr();
    match(TokenKind::Semicolon);

    // A literal also resolves immediately into globalFn/*Set, so
    // SourceLoader's cross-include merge (mergeGlobalQuality) can see it
    // without needing an Interpreter. This is a secondary/legacy view,
    // though: if the same special var is assigned more than once in one
    // file (e.g. `$fn = quality*4; $fn = 8;`), only the *last* literal
    // assignment before this point is reflected here — it is NOT
    // necessarily the last assignment overall (a later non-literal one
    // could follow it, or a non-literal one could precede a later literal).
    // CsgEvaluator ignores this field whenever result.assignments (below)
    // has a value for the same name, since that always reflects the true
    // last-assignment-wins order.
    if (auto* lit = std::get_if<NumberLit>(expr.get())) {
        if      (var.text == "$fn") { result.globalFn = lit->value; result.globalFnSet = true; }
        else if (var.text == "$fs") { result.globalFs = lit->value; result.globalFsSet = true; }
        else if (var.text == "$fa") { result.globalFa = lit->value; result.globalFaSet = true; }
    }

    // Always record the assignment in file order too — exactly like a
    // normal variable — so the Interpreter (which evaluates
    // result.assignments in order during loadAssignments()) gives correct
    // "last assignment wins" semantics when $fn/$fs/$fa is reassigned more
    // than once with a mix of literal and non-literal expressions. Without
    // this, a literal assignment followed by a non-literal one (or vice
    // versa) could have the wrong one win depending on which path
    // CsgEvaluator happened to read from.
    result.assignments.push_back({var.text, std::move(expr), var.loc});
}

void Parser::parseAssignment(ParseResult& result) {
    const Token& name_tok = advance(); // identifier
    advance();                         // consume '='
    auto value = parseExpr();
    match(TokenKind::Semicolon);
    result.assignments.push_back({name_tok.text, std::move(value), name_tok.loc});
}

// ---------------------------------------------------------------------------
// Local variable assignment inside a block: name = expr;
// Reached via parseNode(), so it's usable anywhere a geometry statement is —
// module/for/if/boolean/transform/... bodies — not just at file scope. Like
// the other parseNode() cases, the trailing ';' is left for the caller's
// body-parsing loop to consume (see parseBraceBlock()/parseBody()).
// ---------------------------------------------------------------------------
AstNodePtr Parser::parseAssignNode() {
    const Token& name_tok = advance(); // identifier
    advance();                         // consume '='
    auto value = parseExpr();
    return makeAssign(AssignStmt{name_tok.text, std::move(value), name_tok.loc});
}

// ---------------------------------------------------------------------------
// include <path>; / use <path>; — no file I/O here (Parser is file-agnostic);
// this just records the directive for SourceLoader to resolve recursively.
// ---------------------------------------------------------------------------
void Parser::parseInclude(ParseResult& result) {
    const Token& kw = advance(); // 'include' or 'use'
    IncludeStmt inc;
    inc.kind = (kw.kind == TokenKind::Include) ? IncludeStmt::Kind::Include
                                                : IncludeStmt::Kind::Use;
    inc.loc  = kw.loc;

    if (check(TokenKind::AngledPath)) {
        inc.path = advance().text;
    } else {
        addError("expected '<path>' after 'include'/'use'", peek().loc);
    }
    match(TokenKind::Semicolon); // OpenSCAD doesn't require one; tolerate it if present

    // SourceLoader needs to know where in *this file's own* statement stream
    // the directive sat so it can splice the target's content in at the
    // right position instead of always appending it at the end.
    inc.rootsIndex    = result.roots.size();
    inc.assignIndex   = result.assignments.size();
    inc.moduleIndex   = result.moduleDefs.size();
    inc.functionIndex = result.functionDefs.size();

    result.includes.push_back(std::move(inc));
}

// ---------------------------------------------------------------------------
// Node dispatch
// ---------------------------------------------------------------------------
// A statement may be prefixed by any run of CSG modifier characters
// (# % ! *, in any order/repetition — OpenSCAD allows stacking them, e.g.
// `#!cube();`). None of the four are valid at statement-start any other
// way, so this is unambiguous with their operator meanings inside
// expressions (parseExpr() never calls parseNode()).
AstNodePtr Parser::parseNode() {
    uint8_t mods = ModNone;
    SourceLoc modLoc;
    while (isModifierToken(peek().kind)) {
        if (mods == ModNone) modLoc = peek().loc;
        switch (peek().kind) {
        case TokenKind::Hash:    mods |= ModHighlight;  break;
        case TokenKind::Percent: mods |= ModBackground; break;
        case TokenKind::Star:    mods |= ModDisable;    break;
        case TokenKind::Bang:    mods |= ModRoot;        break;
        default: break; // unreachable — isModifierToken() only allows the four above
        }
        advance();
    }

    AstNodePtr node = parseNodeInner();
    if (node && mods != ModNone) {
        // A block-scoped assignment (`union() { #x = 2; } `) reaches here via
        // the Ident+Equals case in parseNodeInner()'s switch — modifiers on
        // an assignment aren't valid OpenSCAD (they only apply to module
        // instantiations), so diagnose it and leave the assignment itself
        // untouched rather than silently tagging it with a meaningless flag.
        // (A *top-level* modifier-prefixed assignment never reaches this
        // function at all — parseStatement() intercepts it earlier so it
        // stays on the normal hoisted result.assignments path.)
        if (std::holds_alternative<AssignStmt>(*node))
            addError("CSG modifier characters ('#', '%', '!', '*') cannot prefix a variable assignment", modLoc);
        else
            setAstModifiers(*node, mods);
    }
    return node;
}

AstNodePtr Parser::parseNodeInner() {
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

    case TokenKind::Offset:
        return parseOffset();

    case TokenKind::Projection:
        return parseProjection();

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
    case TokenKind::Multmatrix:
        return parseTransform(k);

    case TokenKind::Render:
        return parseRender();

    case TokenKind::Color:
        return parseColor();

    case TokenKind::If:
        return parseIf();

    case TokenKind::For:
        return parseFor();

    case TokenKind::Let:
        return parseLetNode();

    case TokenKind::Ident:
        // Could be a module call: name(args) { ... }
        if (peek(1).kind == TokenKind::LParen)
            return parseModuleCall();
        // Local variable assignment: name = expr; — valid as a statement in
        // any block (module/for/if/... body), not just at file scope.
        if (peek(1).kind == TokenKind::Equals)
            return parseAssignNode();
        return nullptr;

    case TokenKind::Include:
    case TokenKind::Use:
        // include/use are only resolved at file scope (Parser::parseStatement's
        // top-level loop, not here) since SourceLoader splices whole files in
        // relative to top-level statement position. Without this case, a
        // directive written inside a block would fall to `default` below and
        // synchronize() would eat it with no diagnostic at all — report it
        // explicitly instead of silently discarding the whole line.
        addError("include/use is only supported at file scope, not inside a block", peek().loc);
        advance(); // 'include' or 'use'
        if (check(TokenKind::AngledPath)) advance();
        // Deliberately leave a trailing ';' unconsumed: parseBody's caller
        // already treats a bare ';' after a null node as "end of statement"
        // and advances past it cleanly. Consuming it here instead would
        // remove the one delimiter synchronize() looks for, causing it to
        // eat the *next* statement too when there's no ';' immediately after.
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
    case TokenKind::Translate:   node.kind = TransformNode::Kind::Translate; break;
    case TokenKind::Rotate:      node.kind = TransformNode::Kind::Rotate;    break;
    case TokenKind::Scale:       node.kind = TransformNode::Kind::Scale;     break;
    case TokenKind::Mirror:      node.kind = TransformNode::Kind::Mirror;    break;
    case TokenKind::Multmatrix:  node.kind = TransformNode::Kind::Matrix;    break;
    default: break;
    }

    expect(TokenKind::LParen, "expected '(' after transform name");
    node.vec = parseExpr(); // [x,y,z] literal, 4x4 matrix literal, or any expression yielding one
    expect(TokenKind::RParen, "expected ')' after transform argument");

    node.children = parseBody();
    return makeTransform(std::move(node));
}

// ---------------------------------------------------------------------------
// render() — groups children with no transform of its own. convexity (and
// any other named args) are preview-only hints in OpenSCAD; ChiselCAD always
// fully evaluates, so they're parsed and discarded.
// ---------------------------------------------------------------------------
AstNodePtr Parser::parseRender() {
    const Token& kw = advance();
    TransformNode node;
    node.loc  = kw.loc;
    node.kind = TransformNode::Kind::Identity;

    expect(TokenKind::LParen, "expected '(' after 'render'");
    std::unordered_map<std::string, ExprPtr> discardedParams;
    bool unusedCenter = false;
    parseParamList(discardedParams, unusedCenter);
    expect(TokenKind::RParen, "expected ')' after 'render' arguments");

    node.children = parseBody();
    return makeTransform(std::move(node));
}

// ---------------------------------------------------------------------------
// color(c) / color(c, alpha) / color(c=..., alpha=...) — sets an inherited
// tint for its children. The first positional (or "c=") argument is the
// color value; a second positional (or "alpha=") argument overrides alpha.
// ---------------------------------------------------------------------------
AstNodePtr Parser::parseColor() {
    const Token& kw = advance();
    ColorNode node;
    node.loc = kw.loc;

    expect(TokenKind::LParen, "expected '(' after 'color'");

    bool sawPositional = false;
    while (!check(TokenKind::RParen) && !atEnd()) {
        const size_t prevPos = m_pos; // guard against zero-progress infinite loops

        // Special variable override: $fn = expr — color() has no use for
        // special vars, but accept and discard them (consistent with
        // parseParamList/parseExtrusionParams) rather than erroring.
        if (check(TokenKind::SpecialVar)) {
            advance();
            expect(TokenKind::Equals, "expected '=' after special variable");
            parseExpr();
            match(TokenKind::Comma);
            continue;
        }

        if (peek(1).kind == TokenKind::Equals && isParamNameToken(peek())) {
            std::string name = peek().text;
            advance(); // name
            advance(); // =
            if (name == "alpha") node.alphaExpr = parseExpr();
            else                 node.colorExpr = parseExpr(); // c=...
        } else if (!sawPositional) {
            node.colorExpr = parseExpr();
            sawPositional  = true;
        } else {
            node.alphaExpr = parseExpr();
        }

        match(TokenKind::Comma);
        if (m_pos == prevPos) break; // no token consumed — stop to avoid infinite loop
    }
    expect(TokenKind::RParen, "expected ')' after 'color' arguments");

    node.children = parseBody();
    return makeColorNode(std::move(node));
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

    if (check(TokenKind::LBracket)) {
        advance(); // consume '['

        // Parse first expression — determines range vs list form
        auto first = parseExpr();

        if (check(TokenKind::Colon)) {
            // Range form: [start : end] or [start : step : end]
            advance(); // consume ':'
            auto second = parseExpr();
            if (check(TokenKind::Colon)) {
                advance(); // consume ':'
                auto third = parseExpr();
                node.range.isRange = true;
                node.range.start   = std::move(first);
                node.range.step    = std::move(second);
                node.range.end     = std::move(third);
            } else {
                node.range.isRange = true;
                node.range.start   = std::move(first);
                node.range.end     = std::move(second);
            }
        } else {
            // List form: [first, ...]
            node.range.isRange = false;
            node.range.isBracketedList = true;
            node.range.list.push_back(std::move(first));
            while (match(TokenKind::Comma)) {
                if (check(TokenKind::RBracket)) break;
                node.range.list.push_back(parseExpr());
            }
        }
        expect(TokenKind::RBracket, "expected ']' after range/list");
    } else {
        // Expression form: for (var = expr) — expr must evaluate to a vector
        node.range.isRange = false;
        node.range.list.push_back(parseExpr());
    }

    expect(TokenKind::RParen, "expected ')' after for header");

    node.children = parseBody();
    return makeFor(std::move(node));
}

// ---------------------------------------------------------------------------
// parseParamList — named and positional params → ExprPtr map + center flag
// ---------------------------------------------------------------------------
void Parser::parseParamList(std::unordered_map<std::string, ExprPtr>& params,
                             bool& center) {
    center = false;
    int posIdx = 0;

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

        // Named param: any token (Ident or keyword like 'scale') followed by '='
        if (peek(1).kind == TokenKind::Equals && isParamNameToken(peek())) {
            std::string name = peek().text;
            advance(); // name token
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

        // Positional number/expression — indexed _pos0, _pos1, ... so that
        // multiple positional args don't collide into a single key.
        if (!check(TokenKind::RParen)) {
            params["_pos" + std::to_string(posIdx++)] = parseExpr();
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

    // Ternary operator — lowest precedence, only at top level (minPrec == 0)
    if (minPrec == 0 && check(TokenKind::Question)) {
        const Token& q = advance(); // consume '?'
        auto thenExpr = parseExpr(0);
        expect(TokenKind::Colon, "expected ':' in ternary expression");
        auto elseExpr = parseExpr(0);
        TernaryExpr t;
        t.condition = std::move(lhs);
        t.then      = std::move(thenExpr);
        t.else_     = std::move(elseExpr);
        t.loc       = q.loc;
        lhs = makeExpr(std::move(t));
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
    return parsePostfix();
}

// Postfix operators applied to any primary: expr[index]
ExprPtr Parser::parsePostfix() {
    auto expr = parsePrimary();
    while (check(TokenKind::LBracket)) {
        SourceLoc loc = advance().loc; // consume '['
        auto idx = parseExpr();
        expect(TokenKind::RBracket, "expected ']' after index");
        IndexExpr ie;
        ie.target = std::move(expr);
        ie.index  = std::move(idx);
        ie.loc    = loc;
        expr = makeExpr(std::move(ie));
    }
    return expr;
}

ExprPtr Parser::parsePrimary() {
    // Number literal
    if (check(TokenKind::Number)) {
        const Token& tok = advance();
        return makeExpr(NumberLit{tok.numberValue(), tok.loc});
    }
    // String literal
    if (check(TokenKind::String)) {
        const Token& tok = advance();
        return makeExpr(StringLit{tok.text, tok.loc});
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
    // undef literal
    if (check(TokenKind::Undef)) {
        SourceLoc loc = advance().loc;
        return makeExpr(UndefLit{loc});
    }
    // let expression: let(x = expr, ...) body
    if (check(TokenKind::Let)) {
        return parseLetExpr();
    }
    // Function literal: function(params) expr — a first-class function value.
    // Only reachable here (inside expression parsing); a bare `function` at
    // statement level is instead a named `function foo(...) = expr;`
    // definition, handled by parseFunctionDef before expression parsing ever
    // starts.
    if (check(TokenKind::Function)) {
        return parseFunctionLit();
    }
    // Parenthesised expression
    if (match(TokenKind::LParen)) {
        auto expr = parseExpr();
        expect(TokenKind::RParen, "expected ')'");
        return expr;
    }
    // Vector literal [e0, e1, ...], range literal [start:end]/[start:step:end],
    // or list comprehension [for (var = source) body].
    if (check(TokenKind::LBracket)) {
        SourceLoc loc = advance().loc; // consume [

        if (check(TokenKind::RBracket)) { // empty list: []
            advance();
            VectorLit vlit;
            vlit.loc = loc;
            return makeExpr(std::move(vlit));
        }

        if (check(TokenKind::For))
            return parseListComp(loc);

        // A leading `each` can only start a list element, not a range
        // (`[each x : y]` isn't meaningful), so only try the range form
        // when the first element wasn't `each`.
        VectorElem firstElem = parseVectorElem();

        if (!firstElem.isEach && check(TokenKind::Colon)) {
            // Range literal — same grammar as a `for` header's range form,
            // but usable as a general expression (see RangeLit in Expr.h).
            advance(); // consume ':'
            auto second = parseExpr();
            RangeLit range;
            range.loc = loc;
            if (check(TokenKind::Colon)) {
                advance(); // consume ':'
                range.start = std::move(firstElem.value);
                range.step  = std::move(second);
                range.end   = parseExpr();
            } else {
                range.start = std::move(firstElem.value);
                range.end   = std::move(second);
            }
            expect(TokenKind::RBracket, "expected ']' after range");
            return makeExpr(std::move(range));
        }

        VectorLit vlit;
        vlit.loc = loc;
        vlit.elements.push_back(std::move(firstElem));
        while (match(TokenKind::Comma)) {
            if (check(TokenKind::RBracket)) break;
            vlit.elements.push_back(parseVectorElem());
        }
        expect(TokenKind::RBracket, "expected ']'");
        return makeExpr(std::move(vlit));
    }
    // Special variable reference: $fn, $fs, $fa, $children, etc.
    if (check(TokenKind::SpecialVar)) {
        const Token& tok = advance();
        return makeExpr(VarRef{tok.text, tok.loc});
    }
    // Identifier — variable reference or function call
    if (check(TokenKind::Ident)) {
        const Token& name_tok = advance();
        if (match(TokenKind::LParen)) {
            // Function call: name(arg, name=arg, ...)
            FunctionCall fc;
            fc.name = name_tok.text;
            fc.loc  = name_tok.loc;
            while (!check(TokenKind::RParen) && !atEnd()) {
                FunctionArg arg;
                // Named arg: ident = expr
                if (check(TokenKind::Ident) && peek(1).kind == TokenKind::Equals) {
                    arg.name = advance().text; // consume ident
                    advance();                 // consume '='
                }
                arg.value = parseExpr();
                fc.args.push_back(std::move(arg));
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
    def.body = parseBraceBlock();
    expect(TokenKind::RBrace, "expected '}' to close module body");

    result.moduleDefs.push_back(std::move(def));
}

// ---------------------------------------------------------------------------
// function definition — function name(params) = expr;
// ---------------------------------------------------------------------------
void Parser::parseFunctionDef(ParseResult& result) {
    const Token& kw = advance(); // consume 'function'
    FunctionDef def;
    def.loc  = kw.loc;
    def.name = expect(TokenKind::Ident, "expected function name").text;

    expect(TokenKind::LParen, "expected '(' after function name");
    while (!check(TokenKind::RParen) && !atEnd()) {
        FunctionParam param;
        param.name = expect(TokenKind::Ident, "expected parameter name").text;
        if (match(TokenKind::Equals))
            param.defaultVal = parseExpr();
        def.params.push_back(std::move(param));
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RParen, "expected ')'");
    expect(TokenKind::Equals, "expected '=' in function definition");
    def.body = parseExpr();
    match(TokenKind::Semicolon);

    result.functionDefs.push_back(std::move(def));
}

// ---------------------------------------------------------------------------
// function literal — function(params) expr, usable as a general expression
// (assigned to a variable, passed as an argument, returned from another
// function). Same parameter-list grammar as parseFunctionDef, minus the name
// and the `=`.
// ---------------------------------------------------------------------------
ExprPtr Parser::parseFunctionLit() {
    const Token& kw = advance(); // consume 'function'
    FunctionLit lit;
    lit.loc = kw.loc;

    expect(TokenKind::LParen, "expected '(' after 'function'");
    while (!check(TokenKind::RParen) && !atEnd()) {
        FunctionLitParam param;
        param.name = expect(TokenKind::Ident, "expected parameter name").text;
        if (match(TokenKind::Equals))
            param.defaultVal = parseExpr();
        lit.params.push_back(std::move(param));
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RParen, "expected ')' after parameter list");
    lit.body = parseExpr();

    return makeExpr(std::move(lit));
}

// ---------------------------------------------------------------------------
// let statement — let(x = expr, ...) { children }
// ---------------------------------------------------------------------------
AstNodePtr Parser::parseLetNode() {
    const Token& kw = advance(); // consume 'let'
    LetNode node;
    node.loc = kw.loc;

    expect(TokenKind::LParen, "expected '(' after 'let'");
    while (!check(TokenKind::RParen) && !atEnd()) {
        std::string name = expect(TokenKind::Ident, "expected variable name").text;
        expect(TokenKind::Equals, "expected '='");
        auto val = parseExpr();
        node.bindings.push_back({std::move(name), std::move(val)});
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RParen, "expected ')'");

    node.children = parseBody();
    return makeLetNode(std::move(node));
}

// ---------------------------------------------------------------------------
// let expression — let(x = expr, ...) body_expr
// ---------------------------------------------------------------------------
ExprPtr Parser::parseLetExpr() {
    const Token& kw = advance(); // consume 'let'
    LetExpr expr;
    expr.loc = kw.loc;

    expect(TokenKind::LParen, "expected '(' after 'let'");
    while (!check(TokenKind::RParen) && !atEnd()) {
        std::string name = expect(TokenKind::Ident, "expected variable name").text;
        expect(TokenKind::Equals, "expected '='");
        auto val = parseExpr();
        expr.bindings.push_back({std::move(name), std::move(val)});
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RParen, "expected ')'");

    expr.body = parseExpr();
    return makeExpr(std::move(expr));
}

// ---------------------------------------------------------------------------
// parseVectorElem — one element of a vector/list literal: `expr` or
// `each expr` (the latter flattens expr's own elements into the list).
// ---------------------------------------------------------------------------
VectorElem Parser::parseVectorElem() {
    if (check(TokenKind::Each)) {
        advance(); // consume 'each'
        return VectorElem{parseExpr(), true};
    }
    return VectorElem{parseExpr(), false};
}

// ---------------------------------------------------------------------------
// List comprehension — [for (var = source) body]. The leading '[' and the
// 'for' keyword have already been confirmed present (not yet consumed for
// 'for') by the caller; this owns everything through the closing ']'.
// ---------------------------------------------------------------------------
ExprPtr Parser::parseListComp(SourceLoc loc) {
    advance(); // consume 'for'
    expect(TokenKind::LParen, "expected '(' after 'for'");
    std::string var = expect(TokenKind::Ident, "expected loop variable").text;
    expect(TokenKind::Equals, "expected '=' after loop variable");
    auto source = parseExpr();
    expect(TokenKind::RParen, "expected ')' after for-clause");

    ListCompExpr comp;
    comp.var    = std::move(var);
    comp.source = std::move(source);
    comp.loc    = loc;
    comp.body   = parseListCompBody();

    expect(TokenKind::RBracket, "expected ']' after list comprehension");
    return makeExpr(std::move(comp));
}

// ---------------------------------------------------------------------------
// parseListCompBody — one body clause of a list comprehension:
//   expr | each expr | if (cond) body [else body]
// Recursive so if/else/each can nest (e.g. `if (cond) each a else b`).
// ---------------------------------------------------------------------------
ListCompBodyPtr Parser::parseListCompBody() {
    auto body = std::make_unique<ListCompBody>();

    if (check(TokenKind::If)) {
        advance(); // consume 'if'
        expect(TokenKind::LParen, "expected '(' after 'if'");
        body->kind      = ListCompBody::Kind::If;
        body->condition = parseExpr();
        expect(TokenKind::RParen, "expected ')' after if-condition");
        body->thenBody = parseListCompBody();
        if (check(TokenKind::Else)) {
            advance(); // consume 'else'
            body->elseBody = parseListCompBody();
        }
        return body;
    }

    if (check(TokenKind::Each)) {
        advance(); // consume 'each'
        body->kind = ListCompBody::Kind::Each;
        body->expr = parseExpr();
        return body;
    }

    body->kind = ListCompBody::Kind::Expr;
    body->expr = parseExpr();
    return body;
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
    int posIdx = 0;
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
        // Accept any token (Ident or keyword like 'scale') when followed by '='
        if (peek(1).kind == TokenKind::Equals && isParamNameToken(peek())) {
            std::string name = advance().text;
            advance(); // consume '='
            params[name] = parseExpr();
            match(TokenKind::Comma);
            continue;
        }
        // Positional scalar (height for linear_extrude) — indexed to avoid
        // multiple positional args colliding into a single key.
        if (!check(TokenKind::RParen)) {
            params["_pos" + std::to_string(posIdx++)] = parseExpr();
            match(TokenKind::Comma);
        }
        if (m_pos == prevPos) break;
    }
}

// ---------------------------------------------------------------------------
// offset(r=...) / offset(delta=..., chamfer=...) — grows/shrinks 2-D
// children. Params share parseExtrusionParams' generic key=value parsing
// (kept as raw ExprPtr; CsgEvaluator resolves r/delta/chamfer/$fn).
// ---------------------------------------------------------------------------
AstNodePtr Parser::parseOffset() {
    const Token& kw = advance();
    OffsetNode node;
    node.loc = kw.loc;

    expect(TokenKind::LParen, "expected '(' after 'offset'");
    parseExtrusionParams(node.params);
    expect(TokenKind::RParen, "expected ')' after 'offset' arguments");

    node.children = parseBody();
    return makeOffset(std::move(node));
}

// ---------------------------------------------------------------------------
// projection(cut = false) — projects 3-D children onto the XY plane.
// Params share parseExtrusionParams' generic key=value parsing.
// ---------------------------------------------------------------------------
AstNodePtr Parser::parseProjection() {
    const Token& kw = advance();
    ProjectionNode node;
    node.loc = kw.loc;

    expect(TokenKind::LParen, "expected '(' after 'projection'");
    parseExtrusionParams(node.params);
    expect(TokenKind::RParen, "expected ')' after 'projection' arguments");

    node.children = parseBody();
    return makeProjection(std::move(node));
}

// ---------------------------------------------------------------------------
// parseBraceBlock: statements up to (not including) the closing '}' — each
// is either a geometry node or a local assignment (both come back from
// parseNode(), which now handles `name = expr;` as well as geometry).
// ---------------------------------------------------------------------------
std::vector<AstNodePtr> Parser::parseBraceBlock() {
    std::vector<AstNodePtr> children;
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
    return children;
}

// ---------------------------------------------------------------------------
// parseBody: { children* } or single child node
// ---------------------------------------------------------------------------
std::vector<AstNodePtr> Parser::parseBody() {
    std::vector<AstNodePtr> children;

    if (match(TokenKind::LBrace)) {
        children = parseBraceBlock();
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
