#include "Lexer.h"
#include <cassert>
#include <cctype>
#include <unordered_map>

namespace chisel::lang {

// ---------------------------------------------------------------------------
// Keyword table — maps source text to TokenKind
// ---------------------------------------------------------------------------
static const std::unordered_map<std::string_view, TokenKind> kKeywords = {
    {"true",         TokenKind::True},
    {"false",        TokenKind::False},
    {"cube",         TokenKind::Cube},
    {"sphere",       TokenKind::Sphere},
    {"cylinder",     TokenKind::Cylinder},
    {"union",        TokenKind::Union},
    {"difference",   TokenKind::Difference},
    {"intersection", TokenKind::Intersection},
    {"hull",         TokenKind::Hull},
    {"minkowski",    TokenKind::Minkowski},
    {"translate",    TokenKind::Translate},
    {"rotate",       TokenKind::Rotate},
    {"scale",        TokenKind::Scale},
    {"mirror",       TokenKind::Mirror},
    {"if",             TokenKind::If},
    {"else",           TokenKind::Else},
    {"for",            TokenKind::For},
    {"module",         TokenKind::Module},
    {"square",         TokenKind::Square},
    {"circle",         TokenKind::Circle},
    {"polygon",        TokenKind::Polygon},
    {"linear_extrude", TokenKind::LinearExtrude},
    {"rotate_extrude", TokenKind::RotateExtrude},
    {"undef",          TokenKind::Undef},
    {"function",       TokenKind::Function},
    {"let",            TokenKind::Let},
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Lexer::Lexer(std::string_view source, std::string filePath)
    : m_source(source), m_filePath(std::move(filePath)) {}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    tokens.reserve(256);

    while (!atEnd()) {
        // Skip whitespace
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r') {
            advance();
            continue;
        }
        if (c == '\n') {
            advance();
            continue;
        }

        // Skip comments
        if (c == '/' && peek(1) == '/') {
            skipLineComment();
            continue;
        }
        if (c == '/' && peek(1) == '*') {
            skipBlockComment();
            continue;
        }

        uint32_t startOffset = static_cast<uint32_t>(m_pos);

        // Numbers: digits or leading dot (e.g. .5)
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
            tokens.push_back(scanNumber(startOffset));
            continue;
        }

        // Special variables: $fn $fs $fa
        if (c == '$') {
            tokens.push_back(scanSpecialVar(startOffset));
            continue;
        }

        // Identifiers and keywords
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(scanIdentOrKeyword(startOffset));
            continue;
        }

        // Punctuation and operators
        advance();
        switch (c) {
        case '(':  tokens.push_back(makeToken(TokenKind::LParen,    startOffset)); break;
        case ')':  tokens.push_back(makeToken(TokenKind::RParen,    startOffset)); break;
        case '{':  tokens.push_back(makeToken(TokenKind::LBrace,    startOffset)); break;
        case '}':  tokens.push_back(makeToken(TokenKind::RBrace,    startOffset)); break;
        case '[':  tokens.push_back(makeToken(TokenKind::LBracket,  startOffset)); break;
        case ']':  tokens.push_back(makeToken(TokenKind::RBracket,  startOffset)); break;
        case ',':  tokens.push_back(makeToken(TokenKind::Comma,     startOffset)); break;
        case ';':  tokens.push_back(makeToken(TokenKind::Semicolon, startOffset)); break;
        case ':':  tokens.push_back(makeToken(TokenKind::Colon,     startOffset)); break;
        case '+':  tokens.push_back(makeToken(TokenKind::Plus,      startOffset)); break;
        case '-':  tokens.push_back(makeToken(TokenKind::Minus,     startOffset)); break;
        case '*':  tokens.push_back(makeToken(TokenKind::Star,      startOffset)); break;
        case '/':  tokens.push_back(makeToken(TokenKind::Slash,     startOffset)); break;
        case '%':  tokens.push_back(makeToken(TokenKind::Percent,   startOffset)); break;
        case '!':
            if (match('=')) tokens.push_back(makeToken(TokenKind::BangEqual,    startOffset));
            else            tokens.push_back(makeToken(TokenKind::Bang,         startOffset));
            break;
        case '<':
            if (match('=')) tokens.push_back(makeToken(TokenKind::LessEqual,    startOffset));
            else            tokens.push_back(makeToken(TokenKind::Less,         startOffset));
            break;
        case '>':
            if (match('=')) tokens.push_back(makeToken(TokenKind::GreaterEqual, startOffset));
            else            tokens.push_back(makeToken(TokenKind::Greater,      startOffset));
            break;
        case '=':
            if (match('=')) tokens.push_back(makeToken(TokenKind::EqualEqual,   startOffset));
            else            tokens.push_back(makeToken(TokenKind::Equals,       startOffset));
            break;
        case '?':  tokens.push_back(makeToken(TokenKind::Question, startOffset)); break;
        case '&':
            if (match('&')) tokens.push_back(makeToken(TokenKind::AmpAmp,      startOffset));
            else addError("expected '&&'", makeToken(TokenKind::Eof, startOffset).loc);
            break;
        case '|':
            if (match('|')) tokens.push_back(makeToken(TokenKind::PipePipe,    startOffset));
            else addError("expected '||'", makeToken(TokenKind::Eof, startOffset).loc);
            break;
        default:
            addError(std::string("unexpected character '") + c + "'",
                     makeToken(TokenKind::Eof, startOffset).loc);
            break;
        }
    }

    tokens.push_back(makeToken(TokenKind::Eof, static_cast<uint32_t>(m_pos)));
    return tokens;
}

bool Lexer::hasErrors() const {
    for (const auto& d : m_diags)
        if (d.level == DiagLevel::Error) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Source navigation
// ---------------------------------------------------------------------------
char Lexer::peek(int offset) const {
    size_t idx = m_pos + static_cast<size_t>(offset);
    if (idx >= m_source.size()) return '\0';
    return m_source[idx];
}

char Lexer::advance() {
    char c = m_source[m_pos++];
    if (c == '\n') { ++m_line; m_col = 0; }
    else           { ++m_col; }
    return c;
}

bool Lexer::match(char expected) {
    if (atEnd() || m_source[m_pos] != expected) return false;
    advance();
    return true;
}

// ---------------------------------------------------------------------------
// Token factories
// ---------------------------------------------------------------------------
Token Lexer::makeToken(TokenKind kind, uint32_t startOffset) const {
    // Recompute the column at startOffset by walking back — simple approach
    // for small tokens; the loc stored is the start of the token.
    Token t;
    t.kind = kind;
    t.loc.offset = startOffset;
    // Line and col at startOffset: we track current position; for start we
    // need to account for chars consumed since startOffset.
    // We store current line/col which reflects the state *after* consuming.
    // For the token start we subtract the chars consumed in this token.
    // Simpler: store current m_line/m_col BEFORE advancing in each scanner.
    // Here we use a best-effort: the loc reflects where the token begins,
    // captured before advancing in each scan path.
    t.loc.line = m_line;
    t.loc.col  = (m_col >= (m_pos - startOffset)) ? m_col - static_cast<uint32_t>(m_pos - startOffset) : 0;
    return t;
}

Token Lexer::makeToken(TokenKind kind, uint32_t startOffset, std::string text) const {
    Token t = makeToken(kind, startOffset);
    t.text  = std::move(text);
    return t;
}

// ---------------------------------------------------------------------------
// Scanners
// ---------------------------------------------------------------------------
Token Lexer::scanNumber(uint32_t startOffset) {
    // Capture line/col at start
    uint32_t startLine = m_line;
    uint32_t startCol  = m_col;

    // Integer part
    while (std::isdigit(static_cast<unsigned char>(peek()))) advance();

    // Fractional part
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        advance(); // consume '.'
        while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }

    // Exponent
    if (peek() == 'e' || peek() == 'E') {
        advance();
        if (peek() == '+' || peek() == '-') advance();
        while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }

    Token t;
    t.kind       = TokenKind::Number;
    t.loc.offset = startOffset;
    t.loc.line   = startLine;
    t.loc.col    = startCol;
    t.text       = std::string(m_source.substr(startOffset, m_pos - startOffset));
    return t;
}

Token Lexer::scanIdentOrKeyword(uint32_t startOffset) {
    uint32_t startLine = m_line;
    uint32_t startCol  = m_col;

    while (!atEnd() &&
           (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_'))
        advance();

    std::string_view word = m_source.substr(startOffset, m_pos - startOffset);

    TokenKind kind = TokenKind::Ident;
    auto it = kKeywords.find(word);
    if (it != kKeywords.end()) kind = it->second;

    Token t;
    t.kind       = kind;
    t.loc.offset = startOffset;
    t.loc.line   = startLine;
    t.loc.col    = startCol;
    // Store text for identifiers; keywords don't need it but it's harmless
    t.text       = std::string(word);
    return t;
}

Token Lexer::scanSpecialVar(uint32_t startOffset) {
    uint32_t startLine = m_line;
    uint32_t startCol  = m_col;

    advance(); // consume '$'
    while (!atEnd() &&
           (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_'))
        advance();

    Token t;
    t.kind       = TokenKind::SpecialVar;
    t.loc.offset = startOffset;
    t.loc.line   = startLine;
    t.loc.col    = startCol;
    t.text       = std::string(m_source.substr(startOffset, m_pos - startOffset));
    return t;
}

void Lexer::skipLineComment() {
    // consume '//'
    advance(); advance();
    while (!atEnd() && peek() != '\n') advance();
}

void Lexer::skipBlockComment() {
    // consume '/*'
    advance(); advance();
    while (!atEnd()) {
        if (peek() == '*' && peek(1) == '/') {
            advance(); advance();
            return;
        }
        advance();
    }
    // Unterminated block comment — not a fatal error, just note it
    SourceLoc loc;
    loc.line = m_line; loc.col = m_col; loc.offset = static_cast<uint32_t>(m_pos);
    addError("unterminated block comment", loc);
}

void Lexer::addError(const std::string& msg, SourceLoc loc) {
    m_diags.push_back({DiagLevel::Error, msg, loc, m_filePath});
}

} // namespace chisel::lang
