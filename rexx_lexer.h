#ifndef GUIDEXOS_REXX_LEXER_H
#define GUIDEXOS_REXX_LEXER_H

/*
 * Small, allocation-free lexer interface for the native REXX implementation.
 * Token lexemes are non-owning slices into the source buffer supplied to the
 * lexer. The caller must keep that buffer alive while it uses the tokens.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RexxTokenType {
    TOKEN_ERROR = 0,
    TOKEN_EOF,
    TOKEN_NEWLINE,
    TOKEN_SEMICOLON,
    TOKEN_SYMBOL,
    TOKEN_STRING,
    TOKEN_HEX_STRING,
    TOKEN_BINARY_STRING,
    TOKEN_NUMBER,

    /* REXX instruction and clause keywords. Matching is ASCII case-insensitive. */
    TOKEN_KEYWORD_ADDRESS,
    TOKEN_KEYWORD_ARG,
    TOKEN_KEYWORD_CALL,
    TOKEN_KEYWORD_DO,
    TOKEN_KEYWORD_DROP,
    TOKEN_KEYWORD_ELSE,
    TOKEN_KEYWORD_END,
    TOKEN_KEYWORD_EXIT,
    TOKEN_KEYWORD_EXPOSE,
    TOKEN_KEYWORD_IF,
    TOKEN_KEYWORD_INTERPRET,
    TOKEN_KEYWORD_ITERATE,
    TOKEN_KEYWORD_LEAVE,
    TOKEN_KEYWORD_NOP,
    TOKEN_KEYWORD_NUMERIC,
    TOKEN_KEYWORD_OPTIONS,
    TOKEN_KEYWORD_OTHERWISE,
    TOKEN_KEYWORD_PARSE,
    TOKEN_KEYWORD_PROCEDURE,
    TOKEN_KEYWORD_PULL,
    TOKEN_KEYWORD_PUSH,
    TOKEN_KEYWORD_QUEUE,
    TOKEN_KEYWORD_RETURN,
    TOKEN_KEYWORD_SAY,
    TOKEN_KEYWORD_SELECT,
    TOKEN_KEYWORD_SIGNAL,
    TOKEN_KEYWORD_THEN,
    TOKEN_KEYWORD_TRACE,
    TOKEN_KEYWORD_WHEN,

    /* Common DO-clause words; parsers may also treat them as ordinary symbols. */
    TOKEN_KEYWORD_BY,
    TOKEN_KEYWORD_FOR,
    TOKEN_KEYWORD_FOREVER,
    TOKEN_KEYWORD_TO,
    TOKEN_KEYWORD_UNTIL,
    TOKEN_KEYWORD_WHILE,

    TOKEN_ASSIGN,
    TOKEN_OPERATOR,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_DOT
} RexxTokenType;

typedef enum RexxLexerError {
    REXX_LEXER_ERROR_NONE = 0,
    REXX_LEXER_ERROR_INVALID_ARGUMENT,
    REXX_LEXER_ERROR_INVALID_STATE,
    REXX_LEXER_ERROR_UNEXPECTED_CHARACTER,
    REXX_LEXER_ERROR_UNTERMINATED_STRING,
    REXX_LEXER_ERROR_UNTERMINATED_COMMENT,
    REXX_LEXER_ERROR_INVALID_HEX_STRING,
    REXX_LEXER_ERROR_INVALID_BINARY_STRING,
    REXX_LEXER_ERROR_ENCODED_STRING_TOO_LONG,
    REXX_LEXER_ERROR_COMMENT_NESTING_OVERFLOW
} RexxLexerError;

/* Encoded literals have a deterministic, bounded packed size. */
#define REXX_LEXER_MAX_ENCODED_BYTES ((size_t)250u)

typedef enum RexxLexerStatus {
    REXX_LEXER_STATUS_OK = 0,
    REXX_LEXER_STATUS_INVALID_ARGUMENT
} RexxLexerStatus;

typedef struct RexxToken {
    RexxTokenType type;
    const char *lexeme;
    size_t length;
    size_t line;       /* One-based source line. */
    size_t column;     /* One-based byte column; tabs count as one byte. */
    RexxLexerError error;
} RexxToken;

typedef struct RexxLexer {
    const char *source;
    size_t length;
    size_t offset;
    size_t line;
    size_t column;

    /* Block comments can nest and may span lines. */
    size_t comment_depth;
    size_t comment_start_offset;
    size_t comment_start_line;
    size_t comment_start_column;

    int initialized;
    RexxLexerStatus status;
} RexxLexer;

/*
 * Initializes a lexer over a length-delimited source buffer. A NULL source is
 * accepted only for an empty input. The buffer is never copied or modified.
 */
RexxLexerStatus rexx_lexer_init(RexxLexer *lexer,
                                const char *source,
                                size_t length);

/* Returns the next token by value. EOF is returned repeatedly after the end. */
RexxToken rexx_lexer_next_token(RexxLexer *lexer);

/* Human-readable, static names suitable for diagnostics. */
const char *rexx_token_type_name(RexxTokenType type);
const char *rexx_lexer_error_message(RexxLexerError error);

/*
 * Compares a token's complete lexeme with a NUL-terminated ASCII string,
 * ignoring ASCII letter case. Returns 1 for equal, otherwise 0.
 */
int rexx_token_equals_ascii_case_insensitive(const RexxToken *token,
                                             const char *text);

/*
 * Copies a token lexeme to destination while uppercasing ASCII a-z. If
 * capacity is nonzero, output is always NUL-terminated. The return value is
 * the required output length excluding the terminator, whether or not the
 * destination had enough space. A NULL destination is allowed only with
 * capacity zero.
 */
size_t rexx_token_copy_uppercase(const RexxToken *token,
                                 char *destination,
                                 size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* GUIDEXOS_REXX_LEXER_H */
