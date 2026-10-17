#ifndef GUIDEXOS_REXX_PARSER_H
#define GUIDEXOS_REXX_PARSER_H

/*
 * Recursive-descent parser and owned AST for the native guideXOS REXX
 * implementation. AST token fields are non-owning slices into the source
 * buffer used by the lexer; keep that buffer alive for as long as the AST is
 * inspected. AST nodes themselves are allocated by this module and released
 * with rexx_parser_free_ast(). The grammar covers labels, assignments,
 * command expressions, ADDRESS, PARSE, SAY/CALL/RETURN/EXIT/NOP/LEAVE/ITERATE,
 * and the IF, DO, and SELECT control forms; other lexer keywords currently
 * produce a syntax error because they do not yet have dedicated AST forms.
 */

#include "rexx_lexer.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RexxParserStatus {
    REXX_PARSER_STATUS_OK = 0,
    REXX_PARSER_STATUS_INVALID_ARGUMENT,
    REXX_PARSER_STATUS_LEXER_ERROR,
    REXX_PARSER_STATUS_SYNTAX_ERROR,
    REXX_PARSER_STATUS_OUT_OF_MEMORY,
    REXX_PARSER_STATUS_RESOURCE_LIMIT
} RexxParserStatus;

typedef struct RexxParserError {
    RexxParserStatus status;
    RexxToken token;          /* Offending token and its original coordinates. */
    const char *message;      /* Static text; never allocated by the parser. */
} RexxParserError;

typedef enum RexxAstExpressionKind {
    REXX_AST_EXPR_LITERAL = 0,
    REXX_AST_EXPR_SYMBOL,
    REXX_AST_EXPR_GROUP,
    REXX_AST_EXPR_UNARY,
    REXX_AST_EXPR_BINARY,
    REXX_AST_EXPR_FUNCTION_CALL
} RexxAstExpressionKind;

/* REXX operator precedence is represented by the parser's tree shape. */
typedef enum RexxAstOperator {
    REXX_AST_OP_NONE = 0,
    REXX_AST_OP_POSITIVE,
    REXX_AST_OP_NEGATIVE,
    REXX_AST_OP_LOGICAL_NOT,
    REXX_AST_OP_POWER,
    REXX_AST_OP_MULTIPLY,
    REXX_AST_OP_DIVIDE,
    REXX_AST_OP_INTEGER_DIVIDE,
    REXX_AST_OP_REMAINDER,
    REXX_AST_OP_ADD,
    REXX_AST_OP_SUBTRACT,
    REXX_AST_OP_CONCAT,
    REXX_AST_OP_CONCAT_BLANK,
    REXX_AST_OP_CONCAT_ABUTTAL,
    REXX_AST_OP_EQUAL,
    REXX_AST_OP_STRICT_EQUAL,
    REXX_AST_OP_NOT_EQUAL,
    REXX_AST_OP_STRICT_NOT_EQUAL,
    REXX_AST_OP_GREATER,
    REXX_AST_OP_LESS,
    REXX_AST_OP_GREATER_EQUAL,
    REXX_AST_OP_LESS_EQUAL,
    REXX_AST_OP_NOT_GREATER,
    REXX_AST_OP_NOT_LESS,
    REXX_AST_OP_STRICT_GREATER,
    REXX_AST_OP_STRICT_LESS,
    REXX_AST_OP_STRICT_GREATER_EQUAL,
    REXX_AST_OP_STRICT_LESS_EQUAL,
    REXX_AST_OP_STRICT_NOT_GREATER,
    REXX_AST_OP_STRICT_NOT_LESS,
    REXX_AST_OP_LOGICAL_AND,
    REXX_AST_OP_LOGICAL_OR,
    REXX_AST_OP_LOGICAL_XOR
} RexxAstOperator;

typedef struct RexxAstExpression RexxAstExpression;
typedef struct RexxAstArgument RexxAstArgument;
typedef struct RexxAstClause RexxAstClause;
typedef struct RexxAstWhen RexxAstWhen;
typedef struct RexxAstParseTemplatePart RexxAstParseTemplatePart;
typedef struct RexxAstParseTemplate RexxAstParseTemplate;
typedef struct RexxAstName RexxAstName;

struct RexxAstName {
    RexxToken token;
    int subsidiary;           /* PROCEDURE EXPOSE (name) only. */
    RexxAstName *next;
};

struct RexxAstArgument {
    RexxAstExpression *expression; /* NULL represents an omitted positional arg. */
    RexxAstArgument *next;
};

struct RexxAstExpression {
    RexxAstExpressionKind kind;
    RexxToken token;          /* Literal, symbol, function name, or operator. */
    RexxAstOperator operation;
    RexxAstExpression *left;
    RexxAstExpression *right;
    RexxAstArgument *arguments; /* Function call arguments, in source order. */
};

typedef enum RexxAstClauseKind {
    REXX_AST_CLAUSE_LABEL = 0,
    REXX_AST_CLAUSE_ASSIGNMENT,
    REXX_AST_CLAUSE_SAY,
    REXX_AST_CLAUSE_CALL,
    REXX_AST_CLAUSE_RETURN,
    REXX_AST_CLAUSE_EXIT,
    REXX_AST_CLAUSE_NOP,
    REXX_AST_CLAUSE_LEAVE,
    REXX_AST_CLAUSE_ITERATE,
    REXX_AST_CLAUSE_COMMAND,
    REXX_AST_CLAUSE_ADDRESS,
    REXX_AST_CLAUSE_IF,
    REXX_AST_CLAUSE_DO,
    REXX_AST_CLAUSE_SELECT,
    REXX_AST_CLAUSE_NUMERIC_DIGITS,
    REXX_AST_CLAUSE_NUMERIC_FUZZ,
    REXX_AST_CLAUSE_NUMERIC_FORM,
    REXX_AST_CLAUSE_PARSE,
    REXX_AST_CLAUSE_PROCEDURE,
    REXX_AST_CLAUSE_DROP,
    REXX_AST_CLAUSE_SIGNAL,
    REXX_AST_CLAUSE_CONDITION
} RexxAstClauseKind;

typedef enum RexxAstConditionAction {
    REXX_AST_CONDITION_SIGNAL = 0,
    REXX_AST_CONDITION_CALL
} RexxAstConditionAction;

typedef struct RexxAstCondition {
    RexxAstConditionAction action;
    RexxToken condition;
    RexxToken handler;
    int has_handler;
    int enable;
} RexxAstCondition;

typedef enum RexxAstSignalKind {
    REXX_AST_SIGNAL_LABEL = 0, /* Unquoted constant symbol; case-insensitive. */
    REXX_AST_SIGNAL_EXPRESSION /* VALUE form or omitted-VALUE expression. */
} RexxAstSignalKind;

typedef enum RexxAstParseSourceKind {
    REXX_AST_PARSE_ARG = 0,
    REXX_AST_PARSE_VAR,
    REXX_AST_PARSE_VALUE
} RexxAstParseSourceKind;

typedef enum RexxAstParsePartKind {
    REXX_AST_PARSE_VARIABLE = 0,
    REXX_AST_PARSE_PLACEHOLDER,
    REXX_AST_PARSE_STRING_PATTERN,
    REXX_AST_PARSE_POSITION_PATTERN
} RexxAstParsePartKind;

typedef enum RexxAstParsePositionKind {
    REXX_AST_PARSE_POSITION_ABSOLUTE = 0,
    REXX_AST_PARSE_POSITION_FORWARD,
    REXX_AST_PARSE_POSITION_BACKWARD
} RexxAstParsePositionKind;

/* Pattern tokens are quoted literals or variable names in parentheses. */
struct RexxAstParseTemplatePart {
    RexxAstParsePartKind kind;
    RexxAstParsePositionKind position_kind;
    uint32_t position;
    int dynamic;
    RexxToken token;
    RexxAstParseTemplatePart *next;
};

struct RexxAstParseTemplate {
    RexxAstParseTemplatePart *parts;
    RexxAstParseTemplate *next;
};

typedef struct RexxAstParse {
    RexxAstParseSourceKind source_kind;
    RexxToken source_variable;
    RexxAstExpression *source_expression;
    RexxAstParseTemplate *templates;
    int uppercase;
} RexxAstParse;

typedef enum RexxAstDoMode {
    /* A plain DO group executes once; WHILE/UNTIL can still qualify it. */
    REXX_AST_DO_BLOCK = 0,
    REXX_AST_DO_FOREVER,
    REXX_AST_DO_COUNTED,
    REXX_AST_DO_CONTROLLED
} RexxAstDoMode;

typedef struct RexxAstDo {
    RexxAstDoMode mode;
    RexxToken control_variable;
    int has_control_variable;
    RexxAstExpression *start;
    RexxAstExpression *to;
    RexxAstExpression *by;
    RexxAstExpression *for_count;
    RexxAstExpression *while_condition;
    RexxAstExpression *until_condition;
    int has_to;
    int has_by;
    int has_for;
    int has_while;
    int has_until;
    RexxAstClause *body;
    RexxToken end_name;
    int has_end_name;
} RexxAstDo;

struct RexxAstWhen {
    RexxAstExpression *condition;
    RexxAstClause *body;
    RexxAstWhen *next;
};

typedef struct RexxAstSelect {
    RexxAstWhen *when_clauses;
    RexxAstClause *otherwise_clause;
} RexxAstSelect;

struct RexxAstClause {
    RexxAstClauseKind kind;
    RexxToken token;          /* Keyword/name at the start of this clause. */
    RexxAstClause *next;
    union {
        struct {
            RexxToken variable;
            RexxAstExpression *value;
        } assignment;
        struct {
            RexxAstExpression *expression; /* NULL is permitted for SAY. */
            RexxToken address_environment; /* ADDRESS only; source-backed. */
            int has_address_environment;
            uint32_t numeric_form; /* 0=SCIENTIFIC, 1=ENGINEERING for FORM. */
        } simple;
        struct {
            RexxAstExpression *call;
        } call;
        struct {
            RexxToken label;
        } label;
        struct {
            RexxAstExpression *condition;
            RexxAstClause *then_clause;
            RexxAstClause *else_clause;
        } if_clause;
        RexxAstDo do_clause;
        RexxAstSelect select_clause;
        RexxAstParse parse_clause;
        struct {
            RexxAstName *names;
            int subsidiary;
        } procedure;
        struct {
            RexxAstSignalKind signal_kind;
            RexxToken label;
            RexxAstExpression *expression;
        } signal_clause;
        RexxAstCondition condition_clause;
        struct {
            RexxAstName *names;
        } drop;
    } as;
};

typedef struct RexxAstProgram {
    RexxAstClause *clauses;
} RexxAstProgram;

/*
 * Parser state is public so callers can allocate it without a separate
 * constructor. Initialize it with an already initialized lexer; parsing
 * consumes that lexer's remaining token stream.
 */
typedef struct RexxParser {
    RexxLexer *lexer;
    RexxToken current;
    RexxToken lookahead;
    RexxToken previous;
    RexxParserError error;
    RexxParserStatus status;
    size_t nesting_depth;
    size_t expression_depth;
    size_t expression_nodes;
    size_t allocated_nodes;
    void *allocation_list;    /* Private list used to reclaim failed parses. */
    int initialized;
    int parsed;
} RexxParser;

/* Limits bound recursion and memory use for untrusted source text. */
#define REXX_PARSER_MAX_NESTING ((size_t)128u)
#define REXX_PARSER_MAX_EXPRESSION_NODES ((size_t)512u)
#define REXX_PARSER_MAX_AST_NODES ((size_t)65536u)

#if defined(REXX_PARSER_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
void rexx_parser_test_allocator_fail_on(size_t allocation_attempt);
void rexx_parser_test_allocator_disable(void);
size_t rexx_parser_test_allocator_attempts(void);
size_t rexx_parser_test_allocator_live_blocks(void);
#endif

RexxParserStatus rexx_parser_init(RexxParser *parser, RexxLexer *lexer);

/*
 * Parses the remaining input into a program. On failure, *program is NULL and
 * parser->error contains the first diagnostic. The parser may be used once.
 */
RexxParserStatus rexx_parser_parse(RexxParser *parser,
                                   RexxAstProgram **program);

/* Releases a complete or partially built AST. NULL is accepted. */
void rexx_parser_free_ast(RexxAstProgram *program);

/* Returns the parser's current diagnostic, or NULL when no error occurred. */
const RexxParserError *rexx_parser_get_error(const RexxParser *parser);

#ifdef __cplusplus
}
#endif

#endif /* GUIDEXOS_REXX_PARSER_H */
