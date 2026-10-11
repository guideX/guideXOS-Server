/*
 * Allocation-free ASCII lexer for the native guideXOS REXX implementation.
 * It deliberately leaves symbol spelling and literal contents in the source
 * buffer. Keyword recognition is case-insensitive, and callers can normalize
 * symbols with rexx_token_copy_uppercase when using them as variable names.
 */

#include "rexx_lexer.h"

static const char rexx_empty_source[] = "";

typedef struct RexxKeyword {
    const char *text;
    size_t length;
    RexxTokenType type;
} RexxKeyword;

#define REXX_KEYWORD(name, token_type) { name, sizeof(name) - 1u, token_type }

static const RexxKeyword rexx_keywords[] = {
    REXX_KEYWORD("ADDRESS", TOKEN_KEYWORD_ADDRESS),
    REXX_KEYWORD("ARG", TOKEN_KEYWORD_ARG),
    REXX_KEYWORD("CALL", TOKEN_KEYWORD_CALL),
    REXX_KEYWORD("DO", TOKEN_KEYWORD_DO),
    REXX_KEYWORD("DROP", TOKEN_KEYWORD_DROP),
    REXX_KEYWORD("ELSE", TOKEN_KEYWORD_ELSE),
    REXX_KEYWORD("END", TOKEN_KEYWORD_END),
    REXX_KEYWORD("EXIT", TOKEN_KEYWORD_EXIT),
    REXX_KEYWORD("EXPOSE", TOKEN_KEYWORD_EXPOSE),
    REXX_KEYWORD("IF", TOKEN_KEYWORD_IF),
    REXX_KEYWORD("INTERPRET", TOKEN_KEYWORD_INTERPRET),
    REXX_KEYWORD("ITERATE", TOKEN_KEYWORD_ITERATE),
    REXX_KEYWORD("LEAVE", TOKEN_KEYWORD_LEAVE),
    REXX_KEYWORD("NOP", TOKEN_KEYWORD_NOP),
    REXX_KEYWORD("NUMERIC", TOKEN_KEYWORD_NUMERIC),
    REXX_KEYWORD("OPTIONS", TOKEN_KEYWORD_OPTIONS),
    REXX_KEYWORD("OTHERWISE", TOKEN_KEYWORD_OTHERWISE),
    REXX_KEYWORD("PARSE", TOKEN_KEYWORD_PARSE),
    REXX_KEYWORD("PROCEDURE", TOKEN_KEYWORD_PROCEDURE),
    REXX_KEYWORD("PULL", TOKEN_KEYWORD_PULL),
    REXX_KEYWORD("PUSH", TOKEN_KEYWORD_PUSH),
    REXX_KEYWORD("QUEUE", TOKEN_KEYWORD_QUEUE),
    REXX_KEYWORD("RETURN", TOKEN_KEYWORD_RETURN),
    REXX_KEYWORD("SAY", TOKEN_KEYWORD_SAY),
    REXX_KEYWORD("SELECT", TOKEN_KEYWORD_SELECT),
    REXX_KEYWORD("SIGNAL", TOKEN_KEYWORD_SIGNAL),
    REXX_KEYWORD("THEN", TOKEN_KEYWORD_THEN),
    REXX_KEYWORD("TRACE", TOKEN_KEYWORD_TRACE),
    REXX_KEYWORD("WHEN", TOKEN_KEYWORD_WHEN),
    REXX_KEYWORD("BY", TOKEN_KEYWORD_BY),
    REXX_KEYWORD("FOR", TOKEN_KEYWORD_FOR),
    REXX_KEYWORD("FOREVER", TOKEN_KEYWORD_FOREVER),
    REXX_KEYWORD("TO", TOKEN_KEYWORD_TO),
    REXX_KEYWORD("UNTIL", TOKEN_KEYWORD_UNTIL),
    REXX_KEYWORD("WHILE", TOKEN_KEYWORD_WHILE)
};

#undef REXX_KEYWORD

static int rexx_ascii_is_alpha(unsigned char ch)
{
    return (ch >= (unsigned char)'A' && ch <= (unsigned char)'Z') ||
           (ch >= (unsigned char)'a' && ch <= (unsigned char)'z');
}

static int rexx_ascii_is_digit(unsigned char ch)
{
    return ch >= (unsigned char)'0' && ch <= (unsigned char)'9';
}

static unsigned char rexx_ascii_to_upper(unsigned char ch)
{
    if (ch >= (unsigned char)'a' && ch <= (unsigned char)'z') {
        return (unsigned char)(ch - (unsigned char)'a' + (unsigned char)'A');
    }
    return ch;
}

static unsigned char rexx_ascii_to_lower(unsigned char ch)
{
    if (ch >= (unsigned char)'A' && ch <= (unsigned char)'Z') {
        return (unsigned char)(ch - (unsigned char)'A' + (unsigned char)'a');
    }
    return ch;
}

static int rexx_ascii_is_symbol_start(unsigned char ch)
{
    return rexx_ascii_is_alpha(ch) ||
           ch == (unsigned char)'!' || ch == (unsigned char)'?' ||
           ch == (unsigned char)'_' || ch == (unsigned char)'@' ||
           ch == (unsigned char)'#' || ch == (unsigned char)'$';
}

static int rexx_ascii_is_symbol_part(unsigned char ch)
{
    return rexx_ascii_is_symbol_start(ch) || rexx_ascii_is_digit(ch) ||
           ch == (unsigned char)'.';
}

static int rexx_ascii_is_space(unsigned char ch)
{
    return ch == (unsigned char)' ' || ch == (unsigned char)'\t' ||
           ch == (unsigned char)'\v' || ch == (unsigned char)'\f';
}

static int rexx_is_newline_at(const RexxLexer *lexer, size_t offset)
{
    if (offset >= lexer->length) {
        return 0;
    }
    return lexer->source[offset] == '\r' || lexer->source[offset] == '\n';
}

static size_t rexx_consume_newline(RexxLexer *lexer)
{
    size_t consumed = 0;

    if (lexer->offset < lexer->length && lexer->source[lexer->offset] == '\r') {
        ++lexer->offset;
        ++consumed;
        if (lexer->offset < lexer->length && lexer->source[lexer->offset] == '\n') {
            ++lexer->offset;
            ++consumed;
        }
    } else if (lexer->offset < lexer->length && lexer->source[lexer->offset] == '\n') {
        ++lexer->offset;
        ++consumed;
    }

    if (consumed != 0) {
        ++lexer->line;
        lexer->column = 1;
    }
    return consumed;
}

static void rexx_consume_regular(RexxLexer *lexer)
{
    if (lexer->offset < lexer->length) {
        ++lexer->offset;
        ++lexer->column;
    }
}

static int rexx_has_pair(const RexxLexer *lexer, char first, char second)
{
    return lexer->offset + 1u < lexer->length &&
           lexer->source[lexer->offset] == first &&
           lexer->source[lexer->offset + 1u] == second;
}

static RexxToken rexx_make_token(const RexxLexer *lexer,
                                 RexxTokenType type,
                                 size_t start_offset,
                                 size_t start_line,
                                 size_t start_column,
                                 RexxLexerError error)
{
    RexxToken token;
    token.type = type;
    token.lexeme = lexer->source + start_offset;
    token.length = lexer->offset - start_offset;
    token.line = start_line;
    token.column = start_column;
    token.error = error;
    return token;
}

static RexxToken rexx_make_empty_error(RexxLexerError error)
{
    RexxToken token;
    token.type = TOKEN_ERROR;
    token.lexeme = NULL;
    token.length = 0;
    token.line = 1;
    token.column = 1;
    token.error = error;
    return token;
}

static int rexx_range_equals_ascii_case_insensitive(const char *source,
                                                     size_t length,
                                                     const char *text,
                                                     size_t text_length)
{
    size_t index;
    if (source == NULL || text == NULL || length != text_length) {
        return 0;
    }
    for (index = 0; index < length; ++index) {
        if (rexx_ascii_to_upper((unsigned char)source[index]) !=
            rexx_ascii_to_upper((unsigned char)text[index])) {
            return 0;
        }
    }
    return 1;
}

static RexxTokenType rexx_keyword_type(const char *source, size_t length)
{
    size_t index;
    const size_t keyword_count = sizeof(rexx_keywords) / sizeof(rexx_keywords[0]);

    for (index = 0; index < keyword_count; ++index) {
        if (rexx_range_equals_ascii_case_insensitive(source,
                                                    length,
                                                    rexx_keywords[index].text,
                                                    rexx_keywords[index].length)) {
            return rexx_keywords[index].type;
        }
    }
    return TOKEN_SYMBOL;
}

static RexxToken rexx_scan_string(RexxLexer *lexer,
                                 size_t start_offset,
                                 size_t start_line,
                                 size_t start_column)
{
    const char quote = lexer->source[lexer->offset];
    rexx_consume_regular(lexer); /* Opening quote. */

    while (lexer->offset < lexer->length) {
        char current = lexer->source[lexer->offset];

        if (current == quote) {
            if (rexx_has_pair(lexer, quote, quote)) {
                rexx_consume_regular(lexer);
                rexx_consume_regular(lexer);
                continue;
            }
            rexx_consume_regular(lexer); /* Closing quote. */
            return rexx_make_token(lexer, TOKEN_STRING, start_offset,
                                   start_line, start_column,
                                   REXX_LEXER_ERROR_NONE);
        }

        /* A physical line break ends an invalid literal; leave it for the next token. */
        if (rexx_is_newline_at(lexer, lexer->offset)) {
            return rexx_make_token(lexer, TOKEN_ERROR, start_offset,
                                   start_line, start_column,
                                   REXX_LEXER_ERROR_UNTERMINATED_STRING);
        }

        rexx_consume_regular(lexer);
    }

    return rexx_make_token(lexer, TOKEN_ERROR, start_offset,
                           start_line, start_column,
                           REXX_LEXER_ERROR_UNTERMINATED_STRING);
}

static int rexx_ascii_is_hex_digit(unsigned char ch)
{
    return rexx_ascii_is_digit(ch) ||
           (ch >= (unsigned char)'A' && ch <= (unsigned char)'F') ||
           (ch >= (unsigned char)'a' && ch <= (unsigned char)'f');
}

static RexxToken rexx_scan_encoded_string(RexxLexer *lexer,
                                         size_t start_offset,
                                         size_t start_line,
                                         size_t start_column,
                                         int is_hex)
{
    const char quote = lexer->source[lexer->offset + 1u];
    int invalid_payload = 0;
    RexxLexerError payload_error = is_hex
        ? REXX_LEXER_ERROR_INVALID_HEX_STRING
        : REXX_LEXER_ERROR_INVALID_BINARY_STRING;

    rexx_consume_regular(lexer); /* X or B prefix. */
    rexx_consume_regular(lexer); /* Opening quote. */

    while (lexer->offset < lexer->length) {
        unsigned char current = (unsigned char)lexer->source[lexer->offset];

        if (current == (unsigned char)quote) {
            rexx_consume_regular(lexer);
            return rexx_make_token(lexer,
                                   invalid_payload ? TOKEN_ERROR :
                                       (is_hex ? TOKEN_HEX_STRING : TOKEN_BINARY_STRING),
                                   start_offset,
                                   start_line,
                                   start_column,
                                   invalid_payload ? payload_error : REXX_LEXER_ERROR_NONE);
        }

        if (rexx_is_newline_at(lexer, lexer->offset)) {
            return rexx_make_token(lexer, TOKEN_ERROR, start_offset,
                                   start_line, start_column,
                                   REXX_LEXER_ERROR_UNTERMINATED_STRING);
        }

        if (rexx_ascii_is_space(current)) {
            rexx_consume_regular(lexer);
            continue;
        }

        if (is_hex ? rexx_ascii_is_hex_digit(current)
                   : (current == (unsigned char)'0' || current == (unsigned char)'1')) {
            rexx_consume_regular(lexer);
        } else {
            /* Consume to the closing quote before returning an error token. */
            invalid_payload = 1;
            rexx_consume_regular(lexer);
        }
    }

    return rexx_make_token(lexer, TOKEN_ERROR, start_offset,
                           start_line, start_column,
                           REXX_LEXER_ERROR_UNTERMINATED_STRING);
}

static RexxToken rexx_scan_number(RexxLexer *lexer,
                                  size_t start_offset,
                                  size_t start_line,
                                  size_t start_column)
{
    if (lexer->source[lexer->offset] == '.') {
        rexx_consume_regular(lexer);
        while (lexer->offset < lexer->length &&
               rexx_ascii_is_digit((unsigned char)lexer->source[lexer->offset])) {
            rexx_consume_regular(lexer);
        }
    } else {
        while (lexer->offset < lexer->length &&
               rexx_ascii_is_digit((unsigned char)lexer->source[lexer->offset])) {
            rexx_consume_regular(lexer);
        }
        if (lexer->offset < lexer->length && lexer->source[lexer->offset] == '.') {
            rexx_consume_regular(lexer);
            while (lexer->offset < lexer->length &&
                   rexx_ascii_is_digit((unsigned char)lexer->source[lexer->offset])) {
                rexx_consume_regular(lexer);
            }
        }
    }

    /* Only consume E and its optional sign when followed by at least one digit. */
    if (lexer->offset < lexer->length &&
        rexx_ascii_to_upper((unsigned char)lexer->source[lexer->offset]) == (unsigned char)'E') {
        size_t lookahead = lexer->offset + 1u;
        if (lookahead < lexer->length &&
            (lexer->source[lookahead] == '+' || lexer->source[lookahead] == '-')) {
            ++lookahead;
        }
        if (lookahead < lexer->length &&
            rexx_ascii_is_digit((unsigned char)lexer->source[lookahead])) {
            rexx_consume_regular(lexer); /* E */
            if (lexer->offset < lexer->length &&
                (lexer->source[lexer->offset] == '+' || lexer->source[lexer->offset] == '-')) {
                rexx_consume_regular(lexer);
            }
            while (lexer->offset < lexer->length &&
                   rexx_ascii_is_digit((unsigned char)lexer->source[lexer->offset])) {
                rexx_consume_regular(lexer);
            }
        }
    }

    return rexx_make_token(lexer, TOKEN_NUMBER, start_offset,
                           start_line, start_column,
                           REXX_LEXER_ERROR_NONE);
}

static int rexx_skip_comment_until_token(RexxLexer *lexer, RexxToken *token)
{
    while (lexer->comment_depth != 0) {
        if (lexer->offset >= lexer->length) {
            *token = rexx_make_token(lexer, TOKEN_ERROR,
                                     lexer->comment_start_offset,
                                     lexer->comment_start_line,
                                     lexer->comment_start_column,
                                     REXX_LEXER_ERROR_UNTERMINATED_COMMENT);
            lexer->comment_depth = 0;
            return 1;
        }

        if (rexx_is_newline_at(lexer, lexer->offset)) {
            size_t start_offset = lexer->offset;
            size_t start_line = lexer->line;
            size_t start_column = lexer->column;
            (void)rexx_consume_newline(lexer);
            *token = rexx_make_token(lexer, TOKEN_NEWLINE,
                                     start_offset, start_line, start_column,
                                     REXX_LEXER_ERROR_NONE);
            return 1;
        }

        if (rexx_has_pair(lexer, '/', '*')) {
            if (lexer->comment_depth == (size_t)-1) {
                size_t start_offset = lexer->offset;
                size_t start_line = lexer->line;
                size_t start_column = lexer->column;
                rexx_consume_regular(lexer);
                rexx_consume_regular(lexer);
                *token = rexx_make_token(lexer, TOKEN_ERROR,
                                         start_offset, start_line, start_column,
                                         REXX_LEXER_ERROR_COMMENT_NESTING_OVERFLOW);
                return 1;
            }
            ++lexer->comment_depth;
            rexx_consume_regular(lexer);
            rexx_consume_regular(lexer);
            continue;
        }

        if (rexx_has_pair(lexer, '*', '/')) {
            --lexer->comment_depth;
            rexx_consume_regular(lexer);
            rexx_consume_regular(lexer);
            continue;
        }

        rexx_consume_regular(lexer);
    }
    return 0;
}

static RexxToken rexx_scan_operator(RexxLexer *lexer,
                                    size_t start_offset,
                                    size_t start_line,
                                    size_t start_column)
{
    char current = lexer->source[lexer->offset];

    if (current == '=' && !rexx_has_pair(lexer, '=', '=')) {
        rexx_consume_regular(lexer);
        return rexx_make_token(lexer, TOKEN_ASSIGN, start_offset,
                               start_line, start_column,
                               REXX_LEXER_ERROR_NONE);
    }

    if (current == '\\') {
        rexx_consume_regular(lexer);
        if (lexer->offset < lexer->length && lexer->source[lexer->offset] == '=') {
            rexx_consume_regular(lexer);
            if (lexer->offset < lexer->length && lexer->source[lexer->offset] == '=') {
                rexx_consume_regular(lexer);
            }
        } else if (lexer->offset < lexer->length &&
                   (lexer->source[lexer->offset] == '<' || lexer->source[lexer->offset] == '>')) {
            rexx_consume_regular(lexer);
        }
        return rexx_make_token(lexer, TOKEN_OPERATOR, start_offset,
                               start_line, start_column,
                               REXX_LEXER_ERROR_NONE);
    }

    if (current == '=' || current == '/' || current == '*' || current == '>' ||
        current == '<' || current == '&' || current == '|') {
        char second = '\0';
        if (current == '=') second = '=';
        if (current == '/') second = '/';
        if (current == '*') second = '*';
        if (current == '>') {
            if (rexx_has_pair(lexer, '>', '=')) second = '=';
            else second = '<';
        }
        if (current == '<') {
            if (rexx_has_pair(lexer, '<', '=')) second = '=';
            else second = '>';
        }
        if (current == '&') second = '&';
        if (current == '|') second = '|';

        if (second != '\0' && rexx_has_pair(lexer, current, second)) {
            rexx_consume_regular(lexer);
            rexx_consume_regular(lexer);
            return rexx_make_token(lexer, TOKEN_OPERATOR, start_offset,
                                   start_line, start_column,
                                   REXX_LEXER_ERROR_NONE);
        }
    }

    if (current == '+' || current == '-' || current == '*' || current == '/' ||
        current == '%' || current == '>' || current == '<' || current == '&' ||
        current == '|' || current == '=') {
        rexx_consume_regular(lexer);
        return rexx_make_token(lexer, TOKEN_OPERATOR, start_offset,
                               start_line, start_column,
                               REXX_LEXER_ERROR_NONE);
    }

    return rexx_make_empty_error(REXX_LEXER_ERROR_UNEXPECTED_CHARACTER);
}

RexxLexerStatus rexx_lexer_init(RexxLexer *lexer,
                                const char *source,
                                size_t length)
{
    if (lexer == NULL) {
        return REXX_LEXER_STATUS_INVALID_ARGUMENT;
    }

    lexer->source = (source != NULL) ? source : rexx_empty_source;
    lexer->length = length;
    lexer->offset = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->comment_depth = 0;
    lexer->comment_start_offset = 0;
    lexer->comment_start_line = 1;
    lexer->comment_start_column = 1;
    lexer->initialized = (source != NULL || length == 0);
    lexer->status = lexer->initialized
        ? REXX_LEXER_STATUS_OK
        : REXX_LEXER_STATUS_INVALID_ARGUMENT;

    return lexer->status;
}

RexxToken rexx_lexer_next_token(RexxLexer *lexer)
{
    RexxToken token;

    if (lexer == NULL) {
        return rexx_make_empty_error(REXX_LEXER_ERROR_INVALID_STATE);
    }
    if (!lexer->initialized) {
        return rexx_make_empty_error(lexer->status == REXX_LEXER_STATUS_INVALID_ARGUMENT
                                         ? REXX_LEXER_ERROR_INVALID_ARGUMENT
                                         : REXX_LEXER_ERROR_INVALID_STATE);
    }
    if (lexer->source == NULL) {
        return rexx_make_empty_error(REXX_LEXER_ERROR_INVALID_STATE);
    }

    for (;;) {
        if (lexer->comment_depth != 0) {
            if (rexx_skip_comment_until_token(lexer, &token)) {
                return token;
            }
            continue;
        }

        if (lexer->offset >= lexer->length) {
            return rexx_make_token(lexer, TOKEN_EOF, lexer->offset,
                                   lexer->line, lexer->column,
                                   REXX_LEXER_ERROR_NONE);
        }

        if (rexx_ascii_is_space((unsigned char)lexer->source[lexer->offset])) {
            rexx_consume_regular(lexer);
            continue;
        }

        if (rexx_is_newline_at(lexer, lexer->offset)) {
            size_t start_offset = lexer->offset;
            size_t start_line = lexer->line;
            size_t start_column = lexer->column;
            (void)rexx_consume_newline(lexer);
            return rexx_make_token(lexer, TOKEN_NEWLINE,
                                   start_offset, start_line, start_column,
                                   REXX_LEXER_ERROR_NONE);
        }

        if (rexx_has_pair(lexer, '/', '*')) {
            lexer->comment_start_offset = lexer->offset;
            lexer->comment_start_line = lexer->line;
            lexer->comment_start_column = lexer->column;
            lexer->comment_depth = 1;
            rexx_consume_regular(lexer);
            rexx_consume_regular(lexer);
            continue;
        }

        break;
    }

    {
        size_t start_offset = lexer->offset;
        size_t start_line = lexer->line;
        size_t start_column = lexer->column;
        unsigned char current = (unsigned char)lexer->source[lexer->offset];

        if (current == '\'' || current == '"') {
            return rexx_scan_string(lexer, start_offset, start_line, start_column);
        }

        if ((current == 'x' || current == 'X' || current == 'b' || current == 'B') &&
            lexer->offset + 1u < lexer->length &&
            (lexer->source[lexer->offset + 1u] == '\'' ||
             lexer->source[lexer->offset + 1u] == '"')) {
            int is_hex = current == 'x' || current == 'X';
            return rexx_scan_encoded_string(lexer, start_offset, start_line,
                                            start_column, is_hex);
        }

        if (rexx_ascii_is_digit(current) ||
            (current == '.' && lexer->offset + 1u < lexer->length &&
             rexx_ascii_is_digit((unsigned char)lexer->source[lexer->offset + 1u]))) {
            return rexx_scan_number(lexer, start_offset, start_line, start_column);
        }

        if (rexx_ascii_is_symbol_start(current)) {
            rexx_consume_regular(lexer);
            while (lexer->offset < lexer->length &&
                   rexx_ascii_is_symbol_part((unsigned char)lexer->source[lexer->offset])) {
                rexx_consume_regular(lexer);
            }
            return rexx_make_token(lexer,
                                   rexx_keyword_type(lexer->source + start_offset,
                                                     lexer->offset - start_offset),
                                   start_offset,
                                   start_line,
                                   start_column,
                                   REXX_LEXER_ERROR_NONE);
        }

        switch (current) {
        case ';':
            rexx_consume_regular(lexer);
            return rexx_make_token(lexer, TOKEN_SEMICOLON, start_offset,
                                   start_line, start_column,
                                   REXX_LEXER_ERROR_NONE);
        case '(':
            rexx_consume_regular(lexer);
            return rexx_make_token(lexer, TOKEN_LEFT_PAREN, start_offset,
                                   start_line, start_column,
                                   REXX_LEXER_ERROR_NONE);
        case ')':
            rexx_consume_regular(lexer);
            return rexx_make_token(lexer, TOKEN_RIGHT_PAREN, start_offset,
                                   start_line, start_column,
                                   REXX_LEXER_ERROR_NONE);
        case ',':
            rexx_consume_regular(lexer);
            return rexx_make_token(lexer, TOKEN_COMMA, start_offset,
                                   start_line, start_column,
                                   REXX_LEXER_ERROR_NONE);
        case ':':
            rexx_consume_regular(lexer);
            return rexx_make_token(lexer, TOKEN_COLON, start_offset,
                                   start_line, start_column,
                                   REXX_LEXER_ERROR_NONE);
        case '.':
            rexx_consume_regular(lexer);
            return rexx_make_token(lexer, TOKEN_DOT, start_offset,
                                   start_line, start_column,
                                   REXX_LEXER_ERROR_NONE);
        default:
            break;
        }

        if (current == '+' || current == '-' || current == '*' || current == '/' ||
            current == '%' || current == '\\' || current == '>' || current == '<' ||
            current == '&' || current == '|' || current == '=') {
            return rexx_scan_operator(lexer, start_offset, start_line, start_column);
        }

        /* Consume one byte on every unexpected character so lexing can recover. */
        rexx_consume_regular(lexer);
        return rexx_make_token(lexer, TOKEN_ERROR, start_offset,
                               start_line, start_column,
                               REXX_LEXER_ERROR_UNEXPECTED_CHARACTER);
    }
}

const char *rexx_token_type_name(RexxTokenType type)
{
    switch (type) {
    case TOKEN_ERROR: return "error";
    case TOKEN_EOF: return "end of input";
    case TOKEN_NEWLINE: return "newline";
    case TOKEN_SEMICOLON: return "semicolon";
    case TOKEN_SYMBOL: return "symbol";
    case TOKEN_STRING: return "string";
    case TOKEN_HEX_STRING: return "hex string";
    case TOKEN_BINARY_STRING: return "binary string";
    case TOKEN_NUMBER: return "number";
    case TOKEN_KEYWORD_ADDRESS: return "ADDRESS";
    case TOKEN_KEYWORD_ARG: return "ARG";
    case TOKEN_KEYWORD_CALL: return "CALL";
    case TOKEN_KEYWORD_DO: return "DO";
    case TOKEN_KEYWORD_DROP: return "DROP";
    case TOKEN_KEYWORD_ELSE: return "ELSE";
    case TOKEN_KEYWORD_END: return "END";
    case TOKEN_KEYWORD_EXIT: return "EXIT";
    case TOKEN_KEYWORD_EXPOSE: return "EXPOSE";
    case TOKEN_KEYWORD_IF: return "IF";
    case TOKEN_KEYWORD_INTERPRET: return "INTERPRET";
    case TOKEN_KEYWORD_ITERATE: return "ITERATE";
    case TOKEN_KEYWORD_LEAVE: return "LEAVE";
    case TOKEN_KEYWORD_NOP: return "NOP";
    case TOKEN_KEYWORD_NUMERIC: return "NUMERIC";
    case TOKEN_KEYWORD_OPTIONS: return "OPTIONS";
    case TOKEN_KEYWORD_OTHERWISE: return "OTHERWISE";
    case TOKEN_KEYWORD_PARSE: return "PARSE";
    case TOKEN_KEYWORD_PROCEDURE: return "PROCEDURE";
    case TOKEN_KEYWORD_PULL: return "PULL";
    case TOKEN_KEYWORD_PUSH: return "PUSH";
    case TOKEN_KEYWORD_QUEUE: return "QUEUE";
    case TOKEN_KEYWORD_RETURN: return "RETURN";
    case TOKEN_KEYWORD_SAY: return "SAY";
    case TOKEN_KEYWORD_SELECT: return "SELECT";
    case TOKEN_KEYWORD_SIGNAL: return "SIGNAL";
    case TOKEN_KEYWORD_THEN: return "THEN";
    case TOKEN_KEYWORD_TRACE: return "TRACE";
    case TOKEN_KEYWORD_WHEN: return "WHEN";
    case TOKEN_KEYWORD_BY: return "BY";
    case TOKEN_KEYWORD_FOR: return "FOR";
    case TOKEN_KEYWORD_FOREVER: return "FOREVER";
    case TOKEN_KEYWORD_TO: return "TO";
    case TOKEN_KEYWORD_UNTIL: return "UNTIL";
    case TOKEN_KEYWORD_WHILE: return "WHILE";
    case TOKEN_ASSIGN: return "assignment/equality operator";
    case TOKEN_OPERATOR: return "operator";
    case TOKEN_LEFT_PAREN: return "(";
    case TOKEN_RIGHT_PAREN: return ")";
    case TOKEN_COMMA: return ",";
    case TOKEN_COLON: return ":";
    case TOKEN_DOT: return ".";
    default: return "unknown token";
    }
}

const char *rexx_lexer_error_message(RexxLexerError error)
{
    switch (error) {
    case REXX_LEXER_ERROR_NONE: return "no lexer error";
    case REXX_LEXER_ERROR_INVALID_ARGUMENT: return "invalid lexer argument";
    case REXX_LEXER_ERROR_INVALID_STATE: return "lexer is not initialized";
    case REXX_LEXER_ERROR_UNEXPECTED_CHARACTER: return "unexpected character";
    case REXX_LEXER_ERROR_UNTERMINATED_STRING: return "unterminated string literal";
    case REXX_LEXER_ERROR_UNTERMINATED_COMMENT: return "unterminated block comment";
    case REXX_LEXER_ERROR_INVALID_HEX_STRING: return "invalid hexadecimal string literal";
    case REXX_LEXER_ERROR_INVALID_BINARY_STRING: return "invalid binary string literal";
    case REXX_LEXER_ERROR_COMMENT_NESTING_OVERFLOW: return "block comment nesting is too deep";
    default: return "unknown lexer error";
    }
}

int rexx_token_equals_ascii_case_insensitive(const RexxToken *token,
                                             const char *text)
{
    size_t text_length = 0;

    if (token == NULL || text == NULL || token->lexeme == NULL) {
        return 0;
    }
    while (text[text_length] != '\0') {
        if (text_length == (size_t)-1) {
            return 0;
        }
        ++text_length;
    }
    return rexx_range_equals_ascii_case_insensitive(token->lexeme,
                                                    token->length,
                                                    text,
                                                    text_length);
}

size_t rexx_token_copy_uppercase(const RexxToken *token,
                                 char *destination,
                                 size_t capacity)
{
    size_t index;

    if (token == NULL || (token->length != 0 && token->lexeme == NULL) ||
        (destination == NULL && capacity != 0)) {
        if (destination != NULL && capacity != 0) {
            destination[0] = '\0';
        }
        return 0;
    }

    for (index = 0; index < token->length; ++index) {
        if (destination != NULL && capacity != 0 && index < capacity - 1u) {
            destination[index] = (char)rexx_ascii_to_upper((unsigned char)token->lexeme[index]);
        }
    }

    if (destination != NULL && capacity != 0) {
        size_t terminator = token->length < capacity - 1u
            ? token->length
            : capacity - 1u;
        destination[terminator] = '\0';
    }

    return token->length;
}
