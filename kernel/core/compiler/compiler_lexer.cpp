//
// Bounded lexer for the bare-metal compiler bootstrap.
//

#include "compiler_lexer.h"

namespace kernel {
namespace compiler {
namespace {

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_identifier_start(char c)
{
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static bool is_identifier_part(char c)
{
    return is_identifier_start(c) || is_digit(c);
}

static void advance(const char* source,
                    uint32_t length,
                    uint32_t* index,
                    uint32_t* line,
                    uint32_t* column)
{
    if (!source || !index || *index >= length) return;

    const char c = source[*index];
    ++(*index);
    if (c == '\r') {
        if (*index < length && source[*index] == '\n') ++(*index);
        ++(*line);
        *column = 1;
    } else if (c == '\n') {
        ++(*line);
        *column = 1;
    } else {
        ++(*column);
    }
}

static bool add_token(Token* tokens,
                      uint32_t capacity,
                      uint32_t* count,
                      TokenKind kind,
                      SourceLocation location,
                      uint32_t length,
                      Diagnostics& diagnostics)
{
    if (!tokens || !count || *count >= capacity) {
        diagnostics.error(location, "token limit exceeded", "token");
        return false;
    }

    tokens[*count].kind = kind;
    tokens[*count].location = location;
    tokens[*count].length = length;
    ++(*count);
    return true;
}

} // namespace

const char* token_kind_name(TokenKind kind)
{
    switch (kind) {
        case TokenKind::EndOfFile: return "end-of-file";
        case TokenKind::Identifier: return "identifier";
        case TokenKind::Integer: return "integer-literal";
        case TokenKind::Star: return "'*'";
        case TokenKind::LeftParen: return "'('";
        case TokenKind::RightParen: return "')'";
        case TokenKind::LeftBrace: return "'{'";
        case TokenKind::RightBrace: return "'}'";
        case TokenKind::Semicolon: return "';'";
        case TokenKind::Minus: return "'-'";
        case TokenKind::Plus: return "'+'";
        default: return "unknown";
    }
}

bool lex_source(const char* source,
                uint32_t sourceLength,
                Token* tokens,
                uint32_t tokenCapacity,
                uint32_t* tokenCount,
                Diagnostics& diagnostics)
{
    if (!source || !tokens || !tokenCount || tokenCapacity == 0) {
        SourceLocation location = {0, 1, 1};
        diagnostics.error(location, "invalid lexer buffer", "lexer");
        return false;
    }

    *tokenCount = 0;
    uint32_t index = 0;
    uint32_t line = 1;
    uint32_t column = 1;

    while (index < sourceLength) {
        const char c = source[index];

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
            advance(source, sourceLength, &index, &line, &column);
            continue;
        }

        if (c == '/' && index + 1 < sourceLength && source[index + 1] == '/') {
            advance(source, sourceLength, &index, &line, &column);
            advance(source, sourceLength, &index, &line, &column);
            while (index < sourceLength && source[index] != '\r' && source[index] != '\n') {
                advance(source, sourceLength, &index, &line, &column);
            }
            continue;
        }

        if (c == '/' && index + 1 < sourceLength && source[index + 1] == '*') {
            const SourceLocation commentLocation = {index, line, column};
            advance(source, sourceLength, &index, &line, &column);
            advance(source, sourceLength, &index, &line, &column);
            bool closed = false;
            while (index < sourceLength) {
                if (source[index] == '*' && index + 1 < sourceLength && source[index + 1] == '/') {
                    advance(source, sourceLength, &index, &line, &column);
                    advance(source, sourceLength, &index, &line, &column);
                    closed = true;
                    break;
                }
                advance(source, sourceLength, &index, &line, &column);
            }
            if (!closed) {
                diagnostics.error(commentLocation, "unterminated block comment", "comment");
                return false;
            }
            continue;
        }

        const SourceLocation location = {index, line, column};
        if (is_identifier_start(c)) {
            const uint32_t start = index;
            while (index < sourceLength && is_identifier_part(source[index])) {
                advance(source, sourceLength, &index, &line, &column);
            }
            if (!add_token(tokens, tokenCapacity, tokenCount, TokenKind::Identifier,
                           location, index - start, diagnostics)) return false;
            continue;
        }

        if (is_digit(c)) {
            const uint32_t start = index;
            while (index < sourceLength && is_digit(source[index])) {
                advance(source, sourceLength, &index, &line, &column);
            }
            if (index < sourceLength && is_identifier_start(source[index])) {
                while (index < sourceLength && is_identifier_part(source[index])) {
                    advance(source, sourceLength, &index, &line, &column);
                }
                diagnostics.error(location, "invalid integer literal", "integer-literal");
                return false;
            }
            if (!add_token(tokens, tokenCapacity, tokenCount, TokenKind::Integer,
                           location, index - start, diagnostics)) return false;
            continue;
        }

        TokenKind kind;
        switch (c) {
            case '*': kind = TokenKind::Star; break;
            case '(': kind = TokenKind::LeftParen; break;
            case ')': kind = TokenKind::RightParen; break;
            case '{': kind = TokenKind::LeftBrace; break;
            case '}': kind = TokenKind::RightBrace; break;
            case ';': kind = TokenKind::Semicolon; break;
            case '-': kind = TokenKind::Minus; break;
            case '+': kind = TokenKind::Plus; break;
            default:
                diagnostics.error(location, "unexpected character", "character");
                advance(source, sourceLength, &index, &line, &column);
                return false;
        }

        advance(source, sourceLength, &index, &line, &column);
        if (!add_token(tokens, tokenCapacity, tokenCount, kind, location, 1, diagnostics)) return false;
    }

    const SourceLocation endLocation = {sourceLength, line, column};
    if (!add_token(tokens, tokenCapacity, tokenCount, TokenKind::EndOfFile,
                   endLocation, 0, diagnostics)) return false;
    return !diagnostics.has_error();
}

} // namespace compiler
} // namespace kernel
