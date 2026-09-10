//
// Parser for the intentionally tiny compiler bootstrap grammar.
//

#include "compiler_parser.h"

namespace kernel {
namespace compiler {
namespace {

static bool text_equals(const char* source, const Token& token, const char* text)
{
    if (!source || !text) return false;
    uint32_t length = 0;
    while (text[length]) ++length;
    if (token.length != length) return false;
    for (uint32_t i = 0; i < length; ++i) {
        if (source[token.location.offset + i] != text[i]) return false;
    }
    return true;
}

static bool is_reserved_word(const char* source, const Token& token)
{
    return text_equals(source, token, "int") ||
           text_equals(source, token, "void") ||
           text_equals(source, token, "return");
}

static bool copy_string_literal(const char* source, const Token& token,
                                char* output, uint32_t capacity,
                                Diagnostics& diagnostics)
{
    if (!source || !output || capacity == 0 || token.kind != TokenKind::String || token.length < 2) return false;
    uint32_t written = 0;
    for (uint32_t i = 1; i + 1 < token.length; ++i) {
        char value = source[token.location.offset + i];
        if (value == '\\') {
            ++i;
            if (i + 1 >= token.length) return false;
            const char escaped = source[token.location.offset + i];
            if (escaped == 'n') value = '\n';
            else if (escaped == 'r') value = '\r';
            else if (escaped == 't') value = '\t';
            else if (escaped == '"' || escaped == '\\') value = escaped;
            else {
                diagnostics.error(token.location, "unsupported string escape", "string-literal");
                return false;
            }
        }
        if (written + 1 >= capacity || static_cast<unsigned char>(value) < 0x20) {
            diagnostics.error(token.location, "log message exceeds compiler limit", "string-literal");
            return false;
        }
        output[written++] = value;
    }
    output[written] = '\0';
    return true;
}

static bool expect_kind(const Token* tokens,
                        uint32_t tokenCount,
                        uint32_t* index,
                        TokenKind kind,
                        const char* message,
                        Diagnostics& diagnostics)
{
    if (!tokens || !index || *index >= tokenCount) return false;
    const Token& token = tokens[*index];
    if (token.kind != kind) {
        diagnostics.error(token.location, message, token_kind_name(token.kind));
        return false;
    }
    ++(*index);
    return true;
}

static bool expect_word(const char* source,
                        const Token* tokens,
                        uint32_t tokenCount,
                        uint32_t* index,
                        const char* word,
                        const char* message,
                        Diagnostics& diagnostics)
{
    if (!tokens || !index || *index >= tokenCount) return false;
    const Token& token = tokens[*index];
    if (token.kind != TokenKind::Identifier || !text_equals(source, token, word)) {
        diagnostics.error(token.location, message, token_kind_name(token.kind));
        return false;
    }
    ++(*index);
    return true;
}

static bool parse_integer(const char* source,
                          const Token& token,
                          bool negative,
                          int32_t* value,
                          Diagnostics& diagnostics)
{
    if (!source || !value) return false;

    const uint64_t limit = negative ? 2147483648ULL : 2147483647ULL;
    uint64_t accumulated = 0;
    for (uint32_t i = 0; i < token.length; ++i) {
        const uint32_t digit = static_cast<uint32_t>(source[token.location.offset + i] - '0');
        if (accumulated > (limit - digit) / 10ULL) {
            diagnostics.error(token.location, "integer literal outside signed 32-bit range", "integer-literal");
            return false;
        }
        accumulated = accumulated * 10ULL + digit;
    }

    if (!negative) {
        *value = static_cast<int32_t>(accumulated);
    } else if (accumulated == 2147483648ULL) {
        *value = static_cast<int32_t>(-2147483647 - 1);
    } else {
        *value = -static_cast<int32_t>(accumulated);
    }
    return true;
}

} // namespace

bool parse_function(const char* source,
                    const Token* tokens,
                    uint32_t tokenCount,
                    FunctionIR* output,
                    Diagnostics& diagnostics)
{
    if (!source || !tokens || !output || tokenCount == 0) {
        SourceLocation location = {0, 1, 1};
        diagnostics.error(location, "invalid parser input", "parser");
        return false;
    }

    uint32_t index = 0;
    if (!expect_word(source, tokens, tokenCount, &index, "int",
                     "expected return type 'int'", diagnostics)) return false;
    if (!expect_word(source, tokens, tokenCount, &index, "gx_main",
                     "expected function name 'gx_main'", diagnostics)) return false;
    if (!expect_kind(tokens, tokenCount, &index, TokenKind::LeftParen,
                     "expected '(' after function name", diagnostics)) return false;
    if (index >= tokenCount || tokens[index].kind != TokenKind::Identifier ||
        (!text_equals(source, tokens[index], "void") &&
         !text_equals(source, tokens[index], "gx_app_context"))) {
        const Token& token = tokens[index < tokenCount ? index : tokenCount - 1];
        diagnostics.error(token.location, "expected 'void' or 'gx_app_context' parameter type", token_kind_name(token.kind));
        return false;
    }
    ++index;
    if (!expect_kind(tokens, tokenCount, &index, TokenKind::Star,
                     "expected '*' in void-pointer parameter", diagnostics)) return false;

    if (index >= tokenCount || tokens[index].kind != TokenKind::Identifier ||
        is_reserved_word(source, tokens[index])) {
        const Token& token = tokens[index < tokenCount ? index : tokenCount - 1];
        diagnostics.error(token.location, "expected parameter identifier", token_kind_name(token.kind));
        return false;
    }
    ++index;

    if (!expect_kind(tokens, tokenCount, &index, TokenKind::RightParen,
                     "expected ')' after parameter", diagnostics)) return false;
    if (!expect_kind(tokens, tokenCount, &index, TokenKind::LeftBrace,
                     "expected '{' before function body", diagnostics)) return false;
    output->hasLogCall = false;
    output->logMessage[0] = '\0';
    output->logCount = 0;
    for (uint32_t i = 0; i < COMPILER_MAX_LOG_CALLS && index < tokenCount &&
         tokens[index].kind == TokenKind::Identifier && text_equals(source, tokens[index], "log"); ++i) {
        ++index;
        if (!expect_kind(tokens, tokenCount, &index, TokenKind::LeftParen,
                         "expected '(' after log", diagnostics)) return false;
        if (index >= tokenCount || tokens[index].kind != TokenKind::Identifier) {
            const Token& token = tokens[index < tokenCount ? index : tokenCount - 1];
            diagnostics.error(token.location, "expected context parameter in log call", token_kind_name(token.kind));
            return false;
        }
        ++index;
        if (!expect_kind(tokens, tokenCount, &index, TokenKind::Comma,
                         "expected ',' after log context", diagnostics)) return false;
        if (index >= tokenCount || !copy_string_literal(source, tokens[index],
                                                         output->logMessages[i],
                                                         sizeof(output->logMessages[i]), diagnostics)) {
            const Token& token = tokens[index < tokenCount ? index : tokenCount - 1];
            if (!diagnostics.has_error()) diagnostics.error(token.location, "expected log string literal", token_kind_name(token.kind));
            return false;
        }
        ++index;
        if (!expect_kind(tokens, tokenCount, &index, TokenKind::RightParen,
                         "expected ')' after log message", diagnostics) ||
            !expect_kind(tokens, tokenCount, &index, TokenKind::Semicolon,
                         "expected ';' after log call", diagnostics)) return false;
        ++output->logCount;
    }
    if (output->logCount != 0) {
        output->hasLogCall = true;
        for (uint32_t i = 0; i < COMPILER_LOG_MESSAGE_CAPACITY; ++i) {
            output->logMessage[i] = output->logMessages[0][i];
            if (output->logMessage[i] == '\0') break;
        }
    }
    if (!expect_word(source, tokens, tokenCount, &index, "return",
                     "expected 'return' in function body", diagnostics)) return false;

    bool negative = false;
    if (index < tokenCount && tokens[index].kind == TokenKind::Minus) {
        negative = true;
        ++index;
    } else if (index < tokenCount && tokens[index].kind == TokenKind::Plus) {
        diagnostics.error(tokens[index].location,
                          "expected integer literal after 'return'",
                          token_kind_name(tokens[index].kind));
        return false;
    }

    if (index >= tokenCount || tokens[index].kind != TokenKind::Integer) {
        const Token& token = tokens[index < tokenCount ? index : tokenCount - 1];
        diagnostics.error(token.location, "expected integer literal after 'return'",
                          token_kind_name(token.kind));
        return false;
    }

    const Token literal = tokens[index++];
    int32_t returnConstant = 0;
    if (!parse_integer(source, literal, negative, &returnConstant, diagnostics)) return false;

    if (!expect_kind(tokens, tokenCount, &index, TokenKind::Semicolon,
                     "expected ';' after return value", diagnostics)) return false;
    if (!expect_kind(tokens, tokenCount, &index, TokenKind::RightBrace,
                     "expected '}' after function body", diagnostics)) return false;

    if (index >= tokenCount || tokens[index].kind != TokenKind::EndOfFile) {
        const Token& token = tokens[index < tokenCount ? index : tokenCount - 1];
        diagnostics.error(token.location, "expected end of source after function",
                          token_kind_name(token.kind));
        return false;
    }

    output->name[0] = 'g';
    output->name[1] = 'x';
    output->name[2] = '_';
    output->name[3] = 'm';
    output->name[4] = 'a';
    output->name[5] = 'i';
    output->name[6] = 'n';
    output->name[7] = '\0';
    output->returnConstant = returnConstant;
    return !diagnostics.has_error();
}

} // namespace compiler
} // namespace kernel
