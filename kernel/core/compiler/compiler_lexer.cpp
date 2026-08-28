//
// Bounded lexer for the bare-metal compiler bootstrap language.
//

#include "compiler_lexer.h"

namespace kernel {
namespace compiler {
namespace {

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static bool is_identifier_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_identifier_part(char c)
{
    return is_identifier_start(c) || is_digit(c);
}

static bool text_equals(const char* source, uint32_t offset, uint32_t length, const char* text)
{
    if (!source || !text) return false;
    uint32_t textLength = 0;
    while (text[textLength]) ++textLength;
    if (length != textLength) return false;
    for (uint32_t i = 0; i < length; ++i) if (source[offset + i] != text[i]) return false;
    return true;
}

static TokenKind identifier_kind(const char* source, uint32_t offset, uint32_t length)
{
    if (text_equals(source, offset, length, "int")) return TokenKind::KeywordInt;
    if (text_equals(source, offset, length, "gx_main")) return TokenKind::KeywordGxMain;
    if (text_equals(source, offset, length, "gx_app_context")) return TokenKind::KeywordGxAppContext;
    if (text_equals(source, offset, length, "void")) return TokenKind::KeywordVoid;
    if (text_equals(source, offset, length, "return")) return TokenKind::KeywordReturn;
    if (text_equals(source, offset, length, "log")) return TokenKind::KeywordLog;
    if (text_equals(source, offset, length, "if")) return TokenKind::KeywordIf;
    if (text_equals(source, offset, length, "else")) return TokenKind::KeywordElse;
    return TokenKind::Identifier;
}

static void advance(const char* source, uint32_t length, uint32_t* index,
                    uint32_t* line, uint32_t* column)
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

static bool add_token(Token* tokens, uint32_t capacity, uint32_t* count,
                      TokenKind kind, SourceLocation location, uint32_t length,
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
        case TokenKind::StringLiteral: return "string-literal";
        case TokenKind::KeywordInt: return "'int'";
        case TokenKind::KeywordGxMain: return "'gx_main'";
        case TokenKind::KeywordGxAppContext: return "'gx_app_context'";
        case TokenKind::KeywordVoid: return "'void'";
        case TokenKind::KeywordReturn: return "'return'";
        case TokenKind::KeywordLog: return "'log'";
        case TokenKind::KeywordIf: return "'if'";
        case TokenKind::KeywordElse: return "'else'";
        case TokenKind::Star: return "'*'";
        case TokenKind::LeftParen: return "'('";
        case TokenKind::RightParen: return "')'";
        case TokenKind::LeftBrace: return "'{'";
        case TokenKind::RightBrace: return "'}'";
        case TokenKind::Semicolon: return "';'";
        case TokenKind::Minus: return "'-'";
        case TokenKind::Plus: return "'+'";
        case TokenKind::Equal: return "'='";
        case TokenKind::EqualEqual: return "'=='";
        case TokenKind::NotEqual: return "'!='";
        case TokenKind::Less: return "'<'";
        case TokenKind::LessEqual: return "'<='";
        case TokenKind::Greater: return "'>'";
        case TokenKind::GreaterEqual: return "'>='";
        case TokenKind::Comma: return "','";
        default: return "unknown";
    }
}

bool lex_source(const char* source, uint32_t sourceLength, Token* tokens,
                uint32_t tokenCapacity, uint32_t* tokenCount,
                Diagnostics& diagnostics)
{
    if (!source || !tokens || !tokenCount || tokenCapacity == 0) {
        const SourceLocation location = {0, 1, 1};
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
            while (index < sourceLength && source[index] != '\r' && source[index] != '\n')
                advance(source, sourceLength, &index, &line, &column);
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
            while (index < sourceLength && is_identifier_part(source[index]))
                advance(source, sourceLength, &index, &line, &column);
            const uint32_t length = index - start;
            if (length > COMPILER_MAX_IDENTIFIER_BYTES) {
                diagnostics.error(location, "identifier exceeds 63-byte limit", "identifier");
                return false;
            }
            if (!add_token(tokens, tokenCapacity, tokenCount,
                           identifier_kind(source, start, length), location, length, diagnostics)) return false;
            continue;
        }

        if (is_digit(c)) {
            const uint32_t start = index;
            while (index < sourceLength && is_digit(source[index]))
                advance(source, sourceLength, &index, &line, &column);
            if (index < sourceLength && is_identifier_start(source[index])) {
                while (index < sourceLength && is_identifier_part(source[index]))
                    advance(source, sourceLength, &index, &line, &column);
                diagnostics.error(location, "invalid integer literal", "integer-literal");
                return false;
            }
            if (!add_token(tokens, tokenCapacity, tokenCount, TokenKind::Integer,
                           location, index - start, diagnostics)) return false;
            continue;
        }

        if (c == '"') {
            const SourceLocation stringLocation = location;
            const uint32_t start = index;
            advance(source, sourceLength, &index, &line, &column);
            bool closed = false;
            while (index < sourceLength) {
                const char value = source[index];
                if (value == '"') {
                    advance(source, sourceLength, &index, &line, &column);
                    closed = true;
                    break;
                }
                if (value == '\\') {
                    advance(source, sourceLength, &index, &line, &column);
                    if (index >= sourceLength || (source[index] != 'n' && source[index] != '\\' && source[index] != '"')) {
                        diagnostics.error(stringLocation, "unsupported string escape", "string-literal");
                        return false;
                    }
                    advance(source, sourceLength, &index, &line, &column);
                    continue;
                }
                if (value < 32 || value > 126) {
                    diagnostics.error(stringLocation, "string literal contains a non-printable character", "string-literal");
                    return false;
                }
                advance(source, sourceLength, &index, &line, &column);
            }
            if (!closed) {
                diagnostics.error(stringLocation, "unterminated string literal", "string-literal");
                return false;
            }
            if (index - start > COMPILER_MAX_STRING_LITERAL_BYTES * 2U + 2U) {
                diagnostics.error(stringLocation, "string literal exceeds 255-byte limit", "string-literal");
                return false;
            }
            if (!add_token(tokens, tokenCapacity, tokenCount, TokenKind::StringLiteral,
                           stringLocation, index - start, diagnostics)) return false;
            continue;
        }

        TokenKind kind;
        uint32_t tokenLength = 1;
        switch (c) {
            case '*': kind = TokenKind::Star; break;
            case '(': kind = TokenKind::LeftParen; break;
            case ')': kind = TokenKind::RightParen; break;
            case '{': kind = TokenKind::LeftBrace; break;
            case '}': kind = TokenKind::RightBrace; break;
            case ';': kind = TokenKind::Semicolon; break;
            case '-': kind = TokenKind::Minus; break;
            case '+': kind = TokenKind::Plus; break;
            case '=':
                if (index + 1 < sourceLength && source[index + 1] == '=') {
                    kind = TokenKind::EqualEqual; tokenLength = 2;
                } else kind = TokenKind::Equal;
                break;
            case '!':
                if (index + 1 < sourceLength && source[index + 1] == '=') {
                    kind = TokenKind::NotEqual; tokenLength = 2;
                } else {
                    diagnostics.error(location, "unexpected character", "character");
                    advance(source, sourceLength, &index, &line, &column);
                    return false;
                }
                break;
            case '<':
                if (index + 1 < sourceLength && source[index + 1] == '=') {
                    kind = TokenKind::LessEqual; tokenLength = 2;
                } else kind = TokenKind::Less;
                break;
            case '>':
                if (index + 1 < sourceLength && source[index + 1] == '=') {
                    kind = TokenKind::GreaterEqual; tokenLength = 2;
                } else kind = TokenKind::Greater;
                break;
            case ',': kind = TokenKind::Comma; break;
            default:
                diagnostics.error(location, "unexpected character", "character");
                advance(source, sourceLength, &index, &line, &column);
                return false;
        }
        for (uint32_t i = 0; i < tokenLength; ++i)
            advance(source, sourceLength, &index, &line, &column);
        if (!add_token(tokens, tokenCapacity, tokenCount, kind, location, tokenLength, diagnostics)) return false;
    }

    const SourceLocation endLocation = {sourceLength, line, column};
    if (!add_token(tokens, tokenCapacity, tokenCount, TokenKind::EndOfFile,
                   endLocation, 0, diagnostics)) return false;
    return !diagnostics.has_error();
}

} // namespace compiler
} // namespace kernel
