/*
 * Recursive-descent parser for the native guideXOS REXX implementation.
 *
 * Tokens and AST token fields are non-owning slices into the original source.
 * Every AST node is owned by the tree and allocated with the C runtime; the
 * public free routine releases all nodes, including trees built before a
 * syntax or allocation failure.
 */

#include "rexx_parser.h"

#include <stdlib.h>
#include <string.h>

typedef struct RexxParserAllocation {
    struct RexxParserAllocation *next;
} RexxParserAllocation;

/* Context-sensitive expression stops for REXX control headers. */
#define REXX_STOP_THEN       (1u << 0)
#define REXX_STOP_ELSE       (1u << 1)
#define REXX_STOP_END        (1u << 2)
#define REXX_STOP_WHEN       (1u << 3)
#define REXX_STOP_OTHERWISE  (1u << 4)
#define REXX_STOP_TO         (1u << 5)
#define REXX_STOP_BY         (1u << 6)
#define REXX_STOP_FOR        (1u << 7)
#define REXX_STOP_WHILE      (1u << 8)
#define REXX_STOP_UNTIL      (1u << 9)

#define REXX_STOP_DO_MODIFIERS \
    (REXX_STOP_TO | REXX_STOP_BY | REXX_STOP_FOR | \
     REXX_STOP_WHILE | REXX_STOP_UNTIL)

static const char rexx_parser_message_expected_expression[] =
    "expected an expression";
static const char rexx_parser_message_expected_clause[] =
    "expected a REXX clause";
static const char rexx_parser_message_expected_separator[] =
    "expected a newline or semicolon between clauses";
static const char rexx_parser_message_expected_then[] =
    "expected THEN after the IF condition";
static const char rexx_parser_message_expected_end[] =
    "expected END to close the control block";
static const char rexx_parser_message_end_name_mismatch[] =
    "END name does not match the DO control variable";
static const char rexx_parser_message_expected_right_paren[] =
    "expected ')' to close the expression";
static const char rexx_parser_message_expected_call_name[] =
    "expected a function or routine name after CALL";
static const char rexx_parser_message_expected_do_separator[] =
    "expected a newline or semicolon after the DO header";
static const char rexx_parser_message_expected_select_separator[] =
    "expected a newline or semicolon after SELECT";
static const char rexx_parser_message_expected_when[] =
    "SELECT requires at least one WHEN clause";
static const char rexx_parser_message_expected_when_then[] =
    "expected THEN after the WHEN condition";
static const char rexx_parser_message_expected_instruction[] =
    "expected an instruction after THEN";
static const char rexx_parser_message_unexpected_token[] =
    "unexpected token";
static const char rexx_parser_message_duplicate_do_modifier[] =
    "duplicate DO loop modifier";
static const char rexx_parser_message_unexpected_do_modifier[] =
    "DO modifier is not valid for this loop form";
static const char rexx_parser_message_otherwise_order[] =
    "OTHERWISE must be the final SELECT branch";
static const char rexx_parser_message_missing_otherwise_instruction[] =
    "expected an instruction after OTHERWISE";
static const char rexx_parser_message_unsupported_numeric_setting[] =
    "NUMERIC FUZZ and FORM are not supported by this interpreter phase";
static const char rexx_parser_message_resource_limit[] =
    "parser resource limit exceeded";
static const char rexx_parser_message_out_of_memory[] =
    "out of memory while building the syntax tree";
static const char rexx_parser_message_invalid_lexer[] =
    "parser requires an initialized lexer";
static const char rexx_parser_message_already_parsed[] =
    "parser input has already been consumed";

static const char rexx_parser_message_allocation_overflow[] =
    "syntax tree allocation size overflow";

static int rexx_parser_is_keyword(RexxTokenType type)
{
    return type >= TOKEN_KEYWORD_ADDRESS && type <= TOKEN_KEYWORD_WHILE;
}

static void rexx_parser_set_error(RexxParser *parser,
                                  RexxParserStatus status,
                                  const RexxToken *token,
                                  const char *message)
{
    if (parser == NULL || parser->status != REXX_PARSER_STATUS_OK) {
        return;
    }

    parser->status = status;
    parser->error.status = status;
    parser->error.token = (token != NULL) ? *token : parser->current;
    parser->error.message = message;
}

static int rexx_parser_check_lexical_error(RexxParser *parser)
{
    const char *message;

    if (parser == NULL || parser->status != REXX_PARSER_STATUS_OK) {
        return 0;
    }
    if (parser->current.type != TOKEN_ERROR) {
        return 1;
    }

    message = rexx_lexer_error_message(parser->current.error);
    rexx_parser_set_error(parser, REXX_PARSER_STATUS_LEXER_ERROR,
                          &parser->current,
                          (message != NULL) ? message : "lexer error");
    return 0;
}

static int rexx_parser_advance(RexxParser *parser)
{
    if (parser == NULL || parser->status != REXX_PARSER_STATUS_OK) {
        return 0;
    }
    if (!rexx_parser_check_lexical_error(parser)) {
        return 0;
    }

    parser->previous = parser->current;
    parser->current = parser->lookahead;
    parser->lookahead = rexx_lexer_next_token(parser->lexer);
    return rexx_parser_check_lexical_error(parser);
}

static int rexx_parser_expect(RexxParser *parser,
                              RexxTokenType type,
                              const char *message)
{
    if (!rexx_parser_check_lexical_error(parser)) {
        return 0;
    }
    if (parser->current.type != type) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              (message != NULL) ? message
                                                : rexx_parser_message_unexpected_token);
        return 0;
    }
    return rexx_parser_advance(parser);
}

static int rexx_parser_enter_nesting(RexxParser *parser)
{
    if (parser->nesting_depth >= REXX_PARSER_MAX_NESTING) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_RESOURCE_LIMIT,
                              &parser->current,
                              rexx_parser_message_resource_limit);
        return 0;
    }
    ++parser->nesting_depth;
    return 1;
}

static void rexx_parser_leave_nesting(RexxParser *parser)
{
    if (parser->nesting_depth != 0u) {
        --parser->nesting_depth;
    }
}

static void *rexx_parser_allocate(RexxParser *parser, size_t size)
{
    RexxParserAllocation *allocation;

    if (parser == NULL || parser->status != REXX_PARSER_STATUS_OK) {
        return NULL;
    }
    if (parser->allocated_nodes >= REXX_PARSER_MAX_AST_NODES) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_RESOURCE_LIMIT,
                              &parser->current,
                              rexx_parser_message_resource_limit);
        return NULL;
    }

    if (size > (size_t)-1 - sizeof(*allocation)) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_RESOURCE_LIMIT,
                              &parser->current,
                              rexx_parser_message_allocation_overflow);
        return NULL;
    }
    allocation = (RexxParserAllocation *)calloc(1u,
                                                 sizeof(*allocation) + size);
    if (allocation == NULL) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_OUT_OF_MEMORY,
                              &parser->current,
                              rexx_parser_message_out_of_memory);
        return NULL;
    }
    allocation->next = (RexxParserAllocation *)parser->allocation_list;
    parser->allocation_list = allocation;
    ++parser->allocated_nodes;
    return (void *)(allocation + 1);
}

/* AST payloads are preceded by this module's private allocation header. */
static void rexx_parser_deallocate(void *memory)
{
    if (memory != NULL) {
        RexxParserAllocation *allocation =
            ((RexxParserAllocation *)memory) - 1;
        free(allocation);
    }
}

static void rexx_parser_discard_allocations(RexxParser *parser)
{
    RexxParserAllocation *allocation;

    if (parser == NULL) {
        return;
    }
    allocation = (RexxParserAllocation *)parser->allocation_list;
    parser->allocation_list = NULL;
    while (allocation != NULL) {
        RexxParserAllocation *next = allocation->next;
        free(allocation);
        allocation = next;
    }
}

static RexxAstExpression *rexx_parser_new_expression(
    RexxParser *parser,
    RexxAstExpressionKind kind,
    const RexxToken *token)
{
    RexxAstExpression *expression;

    if (parser->expression_nodes >= REXX_PARSER_MAX_EXPRESSION_NODES) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_RESOURCE_LIMIT,
                              (token != NULL) ? token : &parser->current,
                              rexx_parser_message_resource_limit);
        return NULL;
    }
    expression = (RexxAstExpression *)rexx_parser_allocate(
        parser, sizeof(*expression));
    if (expression == NULL) {
        return NULL;
    }
    ++parser->expression_nodes;
    expression->kind = kind;
    if (token != NULL) {
        expression->token = *token;
    }
    return expression;
}

static RexxAstClause *rexx_parser_new_clause(RexxParser *parser,
                                             RexxAstClauseKind kind,
                                             const RexxToken *token)
{
    RexxAstClause *clause = (RexxAstClause *)rexx_parser_allocate(
        parser, sizeof(*clause));

    if (clause != NULL) {
        clause->kind = kind;
        if (token != NULL) {
            clause->token = *token;
        }
    }
    return clause;
}

static RexxAstArgument *rexx_parser_new_argument(RexxParser *parser,
                                                  RexxAstExpression *expression)
{
    RexxAstArgument *argument = (RexxAstArgument *)rexx_parser_allocate(
        parser, sizeof(*argument));

    if (argument != NULL) {
        argument->expression = expression;
    }
    return argument;
}

static RexxAstWhen *rexx_parser_new_when(RexxParser *parser)
{
    return (RexxAstWhen *)rexx_parser_allocate(parser, sizeof(RexxAstWhen));
}

static int rexx_parser_is_separator(const RexxToken *token)
{
    return token != NULL &&
           (token->type == TOKEN_NEWLINE || token->type == TOKEN_SEMICOLON);
}

static size_t rexx_parser_skip_separators(RexxParser *parser)
{
    size_t count = 0u;

    while (parser->status == REXX_PARSER_STATUS_OK &&
           rexx_parser_is_separator(&parser->current)) {
        ++count;
        if (!rexx_parser_advance(parser)) {
            break;
        }
    }
    return count;
}

static size_t rexx_parser_skip_newlines(RexxParser *parser)
{
    size_t count = 0u;

    while (parser->status == REXX_PARSER_STATUS_OK &&
           parser->current.type == TOKEN_NEWLINE) {
        ++count;
        if (!rexx_parser_advance(parser)) {
            break;
        }
    }
    return count;
}

static int rexx_parser_token_is_stop(const RexxToken *token,
                                     unsigned int stop_mask)
{
    if (token == NULL) {
        return 1;
    }

    switch (token->type) {
    case TOKEN_EOF:
    case TOKEN_NEWLINE:
    case TOKEN_SEMICOLON:
    case TOKEN_RIGHT_PAREN:
    case TOKEN_COMMA:
        return 1;
    case TOKEN_KEYWORD_THEN:
        return (stop_mask & REXX_STOP_THEN) != 0u;
    case TOKEN_KEYWORD_ELSE:
        return (stop_mask & REXX_STOP_ELSE) != 0u;
    case TOKEN_KEYWORD_END:
        return (stop_mask & REXX_STOP_END) != 0u;
    case TOKEN_KEYWORD_WHEN:
        return (stop_mask & REXX_STOP_WHEN) != 0u;
    case TOKEN_KEYWORD_OTHERWISE:
        return (stop_mask & REXX_STOP_OTHERWISE) != 0u;
    case TOKEN_KEYWORD_TO:
        return (stop_mask & REXX_STOP_TO) != 0u;
    case TOKEN_KEYWORD_BY:
        return (stop_mask & REXX_STOP_BY) != 0u;
    case TOKEN_KEYWORD_FOR:
        return (stop_mask & REXX_STOP_FOR) != 0u;
    case TOKEN_KEYWORD_WHILE:
        return (stop_mask & REXX_STOP_WHILE) != 0u;
    case TOKEN_KEYWORD_UNTIL:
        return (stop_mask & REXX_STOP_UNTIL) != 0u;
    default:
        return 0;
    }
}

static int rexx_parser_can_start_term(const RexxToken *token)
{
    if (token == NULL) {
        return 0;
    }
    return token->type == TOKEN_SYMBOL || token->type == TOKEN_NUMBER ||
           token->type == TOKEN_STRING || token->type == TOKEN_HEX_STRING ||
           token->type == TOKEN_BINARY_STRING ||
           token->type == TOKEN_LEFT_PAREN ||
           rexx_parser_is_keyword(token->type);
}

static int rexx_parser_can_end_term(const RexxToken *token)
{
    if (token == NULL) {
        return 0;
    }
    return token->type == TOKEN_SYMBOL || token->type == TOKEN_NUMBER ||
           token->type == TOKEN_STRING || token->type == TOKEN_HEX_STRING ||
           token->type == TOKEN_BINARY_STRING ||
           token->type == TOKEN_RIGHT_PAREN ||
           rexx_parser_is_keyword(token->type);
}

static int rexx_parser_tokens_are_adjacent(const RexxToken *left,
                                           const RexxToken *right)
{
    if (left == NULL || right == NULL || left->lexeme == NULL ||
        right->lexeme == NULL) {
        return 0;
    }
    return left->lexeme + left->length == right->lexeme;
}

static unsigned char rexx_parser_ascii_upper(unsigned char ch)
{
    if (ch >= (unsigned char)'a' && ch <= (unsigned char)'z') {
        return (unsigned char)(ch - (unsigned char)'a' + (unsigned char)'A');
    }
    return ch;
}

static int rexx_parser_tokens_equal_case_insensitive(const RexxToken *left,
                                                      const RexxToken *right)
{
    size_t index;

    if (left == NULL || right == NULL || left->lexeme == NULL ||
        right->lexeme == NULL || left->length != right->length) {
        return 0;
    }
    for (index = 0u; index < left->length; ++index) {
        if (rexx_parser_ascii_upper((unsigned char)left->lexeme[index]) !=
            rexx_parser_ascii_upper((unsigned char)right->lexeme[index])) {
            return 0;
        }
    }
    return 1;
}

static int rexx_parser_gap_is_comment_only(const RexxParser *parser,
                                          const RexxToken *left,
                                          const RexxToken *right)
{
    const char *gap;
    size_t gap_length;
    size_t index = 0u;
    size_t comment_depth = 0u;
    int saw_comment = 0;

    if (parser == NULL || parser->lexer == NULL || left == NULL ||
        right == NULL || left->lexeme == NULL || right->lexeme == NULL) {
        return 0;
    }
    gap = left->lexeme + left->length;
    if (right->lexeme < gap || right->lexeme >
            parser->lexer->source + parser->lexer->length) {
        return 0;
    }
    gap_length = (size_t)(right->lexeme - gap);

    while (index < gap_length) {
        unsigned char ch = (unsigned char)gap[index];
        unsigned char next = (index + 1u < gap_length)
            ? (unsigned char)gap[index + 1u] : 0u;

        if (comment_depth != 0u) {
            if (ch == '/' && next == '*') {
                ++comment_depth;
                index += 2u;
            } else if (ch == '*' && next == '/') {
                --comment_depth;
                index += 2u;
            } else {
                ++index;
            }
            continue;
        }

        if (ch == '/' && next == '*') {
            saw_comment = 1;
            comment_depth = 1u;
            index += 2u;
        } else if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
                   ch == '\f' || ch == '\v') {
            /* A real blank outside the comment selects blank concatenation. */
            return 0;
        } else {
            return 0;
        }
    }
    return saw_comment && comment_depth == 0u;
}

static int rexx_parser_is_abuttal(const RexxParser *parser,
                                  const RexxToken *left,
                                  const RexxToken *right)
{
    return rexx_parser_tokens_are_adjacent(left, right) ||
           rexx_parser_gap_is_comment_only(parser, left, right);
}

/* Operator precedence follows the standard REXX ordering, high to low. */
static int rexx_parser_binary_operator(const RexxParser *parser,
                                       RexxAstOperator *operation,
                                       int *precedence,
                                       size_t *extra_tokens)
{
    const RexxToken *token;

    if (parser == NULL || operation == NULL || precedence == NULL ||
        extra_tokens == NULL) {
        return 0;
    }
    token = &parser->current;
    *extra_tokens = 0u;

    if (token->type == TOKEN_ASSIGN) {
        *operation = REXX_AST_OP_EQUAL;
        *precedence = 4;
        return 1;
    }
    if (token->type != TOKEN_OPERATOR || token->lexeme == NULL) {
        return 0;
    }

    /*
     * The established lexer emits a few three-character REXX comparison
     * operators as adjacent operator tokens. Rejoin only directly adjacent
     * slices here, leaving spaced operators as independent tokens.
     */
    if (parser->lookahead.lexeme != NULL &&
        rexx_parser_tokens_are_adjacent(token, &parser->lookahead)) {
        if (rexx_token_equals_ascii_case_insensitive(token, ">") &&
            rexx_token_equals_ascii_case_insensitive(&parser->lookahead, ">")) {
            *operation = REXX_AST_OP_STRICT_GREATER;
            *precedence = 4;
            *extra_tokens = 1u;
            return 1;
        }
        if (rexx_token_equals_ascii_case_insensitive(token, "<") &&
            rexx_token_equals_ascii_case_insensitive(&parser->lookahead, "<")) {
            *operation = REXX_AST_OP_STRICT_LESS;
            *precedence = 4;
            *extra_tokens = 1u;
            return 1;
        }
        if (rexx_token_equals_ascii_case_insensitive(token, ">") &&
            rexx_token_equals_ascii_case_insensitive(&parser->lookahead, ">=")) {
            *operation = REXX_AST_OP_STRICT_GREATER_EQUAL;
            *precedence = 4;
            *extra_tokens = 1u;
            return 1;
        }
        if (rexx_token_equals_ascii_case_insensitive(token, "<") &&
            rexx_token_equals_ascii_case_insensitive(&parser->lookahead, "<=")) {
            *operation = REXX_AST_OP_STRICT_LESS_EQUAL;
            *precedence = 4;
            *extra_tokens = 1u;
            return 1;
        }
        if (rexx_token_equals_ascii_case_insensitive(token, "\\>") &&
            rexx_token_equals_ascii_case_insensitive(&parser->lookahead, ">")) {
            *operation = REXX_AST_OP_STRICT_NOT_GREATER;
            *precedence = 4;
            *extra_tokens = 1u;
            return 1;
        }
        if (rexx_token_equals_ascii_case_insensitive(token, "\\<") &&
            rexx_token_equals_ascii_case_insensitive(&parser->lookahead, "<")) {
            *operation = REXX_AST_OP_STRICT_NOT_LESS;
            *precedence = 4;
            *extra_tokens = 1u;
            return 1;
        }
        if (rexx_token_equals_ascii_case_insensitive(token, "/") &&
            parser->lookahead.type == TOKEN_ASSIGN) {
            *operation = REXX_AST_OP_NOT_EQUAL;
            *precedence = 4;
            *extra_tokens = 1u;
            return 1;
        }
        if (rexx_token_equals_ascii_case_insensitive(token, "/") &&
            rexx_token_equals_ascii_case_insensitive(&parser->lookahead, "==")) {
            *operation = REXX_AST_OP_STRICT_NOT_EQUAL;
            *precedence = 4;
            *extra_tokens = 1u;
            return 1;
        }
    }

#define REXX_OPERATOR(text, ast_op, prec) \
    if (rexx_token_equals_ascii_case_insensitive(token, text)) { \
        *operation = ast_op; \
        *precedence = prec; \
        return 1; \
    }

    REXX_OPERATOR("**", REXX_AST_OP_POWER, 8)
    REXX_OPERATOR("*", REXX_AST_OP_MULTIPLY, 7)
    REXX_OPERATOR("/", REXX_AST_OP_DIVIDE, 7)
    REXX_OPERATOR("%", REXX_AST_OP_INTEGER_DIVIDE, 7)
    REXX_OPERATOR("//", REXX_AST_OP_REMAINDER, 7)
    REXX_OPERATOR("+", REXX_AST_OP_ADD, 6)
    REXX_OPERATOR("-", REXX_AST_OP_SUBTRACT, 6)
    REXX_OPERATOR("||", REXX_AST_OP_CONCAT, 5)
    REXX_OPERATOR("==", REXX_AST_OP_STRICT_EQUAL, 4)
    REXX_OPERATOR("\\==", REXX_AST_OP_STRICT_NOT_EQUAL, 4)
    REXX_OPERATOR("/=", REXX_AST_OP_NOT_EQUAL, 4)
    REXX_OPERATOR("\\=", REXX_AST_OP_NOT_EQUAL, 4)
    REXX_OPERATOR("<>", REXX_AST_OP_NOT_EQUAL, 4)
    REXX_OPERATOR("><", REXX_AST_OP_NOT_EQUAL, 4)
    REXX_OPERATOR(">=", REXX_AST_OP_GREATER_EQUAL, 4)
    REXX_OPERATOR("<=", REXX_AST_OP_LESS_EQUAL, 4)
    REXX_OPERATOR("\\>", REXX_AST_OP_NOT_GREATER, 4)
    REXX_OPERATOR("\\<", REXX_AST_OP_NOT_LESS, 4)
    REXX_OPERATOR(">", REXX_AST_OP_GREATER, 4)
    REXX_OPERATOR("<", REXX_AST_OP_LESS, 4)
    REXX_OPERATOR("&", REXX_AST_OP_LOGICAL_AND, 3)
    REXX_OPERATOR("|", REXX_AST_OP_LOGICAL_OR, 2)
    REXX_OPERATOR("&&", REXX_AST_OP_LOGICAL_XOR, 2)

#undef REXX_OPERATOR
    return 0;
}

static int rexx_parser_prefix_operator(const RexxToken *token,
                                       RexxAstOperator *operation)
{
    if (token == NULL || operation == NULL || token->type != TOKEN_OPERATOR) {
        return 0;
    }
    if (rexx_token_equals_ascii_case_insensitive(token, "+")) {
        *operation = REXX_AST_OP_POSITIVE;
        return 1;
    }
    if (rexx_token_equals_ascii_case_insensitive(token, "-")) {
        *operation = REXX_AST_OP_NEGATIVE;
        return 1;
    }
    if (rexx_token_equals_ascii_case_insensitive(token, "\\")) {
        *operation = REXX_AST_OP_LOGICAL_NOT;
        return 1;
    }
    return 0;
}

static int rexx_parser_is_name_token(const RexxToken *token)
{
    return token != NULL &&
           (token->type == TOKEN_SYMBOL || rexx_parser_is_keyword(token->type));
}

static RexxAstExpression *rexx_parser_parse_expression_internal(
    RexxParser *parser,
    int minimum_precedence,
    unsigned int stop_mask);

static RexxAstExpression *rexx_parser_parse_call_after_name(
    RexxParser *parser,
    RexxToken name,
    int has_parentheses)
{
    RexxAstExpression *call;
    RexxAstArgument *argument_tail = NULL;

    call = rexx_parser_new_expression(parser, REXX_AST_EXPR_FUNCTION_CALL,
                                      &name);
    if (call == NULL) {
        return NULL;
    }

    if (!has_parentheses) {
        return call;
    }
    if (!rexx_parser_expect(parser, TOKEN_LEFT_PAREN,
                            rexx_parser_message_unexpected_token)) {
        return NULL;
    }

    if (parser->current.type == TOKEN_RIGHT_PAREN) {
        if (!rexx_parser_advance(parser)) {
            return NULL;
        }
        return call;
    }

    for (;;) {
        RexxAstExpression *argument_expression = NULL;
        RexxAstArgument *argument;

        if (parser->current.type != TOKEN_COMMA &&
            parser->current.type != TOKEN_RIGHT_PAREN) {
            argument_expression = rexx_parser_parse_expression_internal(
                parser, 1, 0u);
            if (argument_expression == NULL) {
                return NULL;
            }
        }
        argument = rexx_parser_new_argument(parser, argument_expression);
        if (argument == NULL) {
            return NULL;
        }
        if (argument_tail == NULL) {
            call->arguments = argument;
        } else {
            argument_tail->next = argument;
        }
        argument_tail = argument;

        if (parser->current.type == TOKEN_COMMA) {
            if (!rexx_parser_advance(parser)) {
                return NULL;
            }
            continue;
        }
        if (!rexx_parser_expect(parser, TOKEN_RIGHT_PAREN,
                                rexx_parser_message_expected_right_paren)) {
            return NULL;
        }
        return call;
    }
}

static RexxAstExpression *rexx_parser_parse_primary(
    RexxParser *parser,
    unsigned int stop_mask)
{
    RexxToken token;
    RexxAstExpression *expression;
    RexxAstOperator operation;

    if (!rexx_parser_check_lexical_error(parser)) {
        return NULL;
    }

    if (rexx_parser_token_is_stop(&parser->current, stop_mask)) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_expected_expression);
        return NULL;
    }

    if (rexx_parser_prefix_operator(&parser->current, &operation)) {
        RexxAstExpression *operand;

        token = parser->current;
        if (!rexx_parser_advance(parser)) {
            return NULL;
        }
        /* Prefix operators bind more tightly than power in standard REXX. */
        operand = rexx_parser_parse_expression_internal(parser, 9, stop_mask);
        if (operand == NULL) {
            return NULL;
        }
        expression = rexx_parser_new_expression(parser, REXX_AST_EXPR_UNARY,
                                                &token);
        if (expression == NULL) {
            return NULL;
        }
        expression->operation = operation;
        expression->right = operand;
        return expression;
    }

    if (parser->current.type == TOKEN_LEFT_PAREN) {
        RexxAstExpression *inner;
        RexxAstExpression *group;

        token = parser->current;
        if (!rexx_parser_advance(parser)) {
            return NULL;
        }
        inner = rexx_parser_parse_expression_internal(parser, 1, stop_mask);
        if (inner == NULL) {
            return NULL;
        }
        if (!rexx_parser_expect(parser, TOKEN_RIGHT_PAREN,
                                rexx_parser_message_expected_right_paren)) {
            return NULL;
        }
        group = rexx_parser_new_expression(parser, REXX_AST_EXPR_GROUP, &token);
        if (group == NULL) {
            return NULL;
        }
        group->left = inner;
        return group;
    }

    if (parser->current.type == TOKEN_NUMBER ||
        parser->current.type == TOKEN_STRING ||
        parser->current.type == TOKEN_HEX_STRING ||
        parser->current.type == TOKEN_BINARY_STRING) {
        token = parser->current;
        if (!rexx_parser_advance(parser)) {
            return NULL;
        }
        return rexx_parser_new_expression(parser, REXX_AST_EXPR_LITERAL,
                                         &token);
    }

    if (rexx_parser_is_name_token(&parser->current)) {
        int is_adjacent_call;

        token = parser->current;
        is_adjacent_call = parser->lookahead.type == TOKEN_LEFT_PAREN &&
            rexx_parser_tokens_are_adjacent(&token, &parser->lookahead);
        if (!rexx_parser_advance(parser)) {
            return NULL;
        }
        if (is_adjacent_call) {
            return rexx_parser_parse_call_after_name(parser, token, 1);
        }
        return rexx_parser_new_expression(parser, REXX_AST_EXPR_SYMBOL, &token);
    }

    rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                          &parser->current,
                          rexx_parser_message_expected_expression);
    return NULL;
}

static RexxAstExpression *rexx_parser_parse_expression_core(
    RexxParser *parser,
    int minimum_precedence,
    unsigned int stop_mask)
{
    RexxAstExpression *left;

    left = rexx_parser_parse_primary(parser, stop_mask);
    if (left == NULL) {
        return NULL;
    }

    while (parser->status == REXX_PARSER_STATUS_OK) {
        RexxAstOperator operation;
        int precedence;
        int implicit_concat = 0;
        size_t extra_operator_tokens = 0u;
        RexxToken operator_token;
        RexxAstExpression *right;
        RexxAstExpression *binary;

        if (!rexx_parser_check_lexical_error(parser) ||
            rexx_parser_token_is_stop(&parser->current, stop_mask)) {
            break;
        }

        if (rexx_parser_binary_operator(parser, &operation, &precedence,
                                        &extra_operator_tokens)) {
            operator_token = parser->current;
        } else if (rexx_parser_can_start_term(&parser->current) &&
                   rexx_parser_can_end_term(&parser->previous)) {
            /*
             * REXX permits blank concatenation and abuttal. The lexer omits
             * whitespace tokens, so source-slice adjacency distinguishes
             * abuttal from the one-blank concatenation operator.
             */
            precedence = 5;
            implicit_concat = 1;
            operation = rexx_parser_is_abuttal(parser, &parser->previous,
                                               &parser->current)
                ? REXX_AST_OP_CONCAT_ABUTTAL
                : REXX_AST_OP_CONCAT_BLANK;
            operator_token = parser->current; /* Location for a synthetic op. */
        } else {
            break;
        }

        if (precedence < minimum_precedence) {
            break;
        }
        if (!implicit_concat) {
            size_t token_index;
            for (token_index = 0u;
                 token_index <= extra_operator_tokens;
                 ++token_index) {
                if (!rexx_parser_advance(parser)) {
                    return NULL;
                }
            }
        }

        /* All standard REXX binary operators associate from left to right. */
        right = rexx_parser_parse_expression_internal(parser,
                                                      precedence + 1,
                                                      stop_mask);
        if (right == NULL) {
            return NULL;
        }
        binary = rexx_parser_new_expression(parser, REXX_AST_EXPR_BINARY,
                                            &operator_token);
        if (binary == NULL) {
            return NULL;
        }
        binary->operation = operation;
        binary->left = left;
        binary->right = right;
        left = binary;
    }
    return left;
}

static RexxAstExpression *rexx_parser_parse_expression_internal(
    RexxParser *parser,
    int minimum_precedence,
    unsigned int stop_mask)
{
    RexxAstExpression *expression;
    int is_outer_expression;

    if (!rexx_parser_enter_nesting(parser)) {
        return NULL;
    }
    is_outer_expression = parser->expression_depth == 0u;
    if (is_outer_expression) {
        parser->expression_nodes = 0u;
    }
    ++parser->expression_depth;

    expression = rexx_parser_parse_expression_core(parser,
                                                   minimum_precedence,
                                                   stop_mask);

    if (parser->expression_depth != 0u) {
        --parser->expression_depth;
    }
    if (is_outer_expression) {
        parser->expression_nodes = 0u;
    }
    rexx_parser_leave_nesting(parser);
    return expression;
}

static RexxAstClause *rexx_parser_parse_clause(RexxParser *parser,
                                               unsigned int stop_mask);

static RexxAstClause *rexx_parser_parse_clause_list(
    RexxParser *parser,
    unsigned int stop_mask)
{
    RexxAstClause *head = NULL;
    RexxAstClause *tail = NULL;
    int have_clause = 0;

    for (;;) {
        RexxAstClause *clause;

        if (parser->status != REXX_PARSER_STATUS_OK ||
            !rexx_parser_check_lexical_error(parser)) {
            break;
        }

        (void)rexx_parser_skip_separators(parser);
        if (parser->status != REXX_PARSER_STATUS_OK) {
            break;
        }
        if (parser->current.type == TOKEN_EOF ||
            ((parser->current.type == TOKEN_KEYWORD_END) &&
             ((stop_mask & REXX_STOP_END) != 0u)) ||
            ((parser->current.type == TOKEN_KEYWORD_WHEN) &&
             ((stop_mask & REXX_STOP_WHEN) != 0u)) ||
            ((parser->current.type == TOKEN_KEYWORD_OTHERWISE) &&
             ((stop_mask & REXX_STOP_OTHERWISE) != 0u)) ||
            ((parser->current.type == TOKEN_KEYWORD_ELSE) &&
             ((stop_mask & REXX_STOP_ELSE) != 0u))) {
            break;
        }

        clause = rexx_parser_parse_clause(parser, stop_mask);
        if (clause == NULL) {
            break;
        }
        if (head == NULL) {
            head = clause;
        } else {
            tail->next = clause;
        }
        tail = clause;
        have_clause = 1;

        if (parser->status != REXX_PARSER_STATUS_OK) {
            break;
        }
        if (rexx_parser_is_separator(&parser->current)) {
            continue;
        }
        /* A label's colon is itself a clause boundary in REXX. */
        if (clause->kind == REXX_AST_CLAUSE_LABEL) {
            continue;
        }
        if (parser->current.type == TOKEN_EOF) {
            break;
        }
        if ((parser->current.type == TOKEN_KEYWORD_END &&
             (stop_mask & REXX_STOP_END) != 0u) ||
            (parser->current.type == TOKEN_KEYWORD_WHEN &&
             (stop_mask & REXX_STOP_WHEN) != 0u) ||
            (parser->current.type == TOKEN_KEYWORD_OTHERWISE &&
             (stop_mask & REXX_STOP_OTHERWISE) != 0u) ||
            (parser->current.type == TOKEN_KEYWORD_ELSE &&
             (stop_mask & REXX_STOP_ELSE) != 0u)) {
            /* Closing clauses must begin after a real clause boundary. */
            if (have_clause &&
                !rexx_parser_is_separator(&parser->previous)) {
                    rexx_parser_set_error(parser,
                                          REXX_PARSER_STATUS_SYNTAX_ERROR,
                                          &parser->current,
                                          rexx_parser_message_expected_separator);
                    break;
            }
            break;
        }

        /* IF can consume a newline while looking ahead for its ELSE clause. */
        if (rexx_parser_is_separator(&parser->previous)) {
            continue;
        }

        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_expected_separator);
        break;
    }
    return head;
}

static RexxAstClause *rexx_parser_parse_if(RexxParser *parser,
                                           unsigned int inherited_stops)
{
    RexxToken if_token = parser->current;
    RexxAstClause *clause = rexx_parser_new_clause(
        parser, REXX_AST_CLAUSE_IF, &if_token);
    unsigned int condition_stops = inherited_stops | REXX_STOP_THEN;

    if (clause == NULL || !rexx_parser_advance(parser)) {
        return NULL;
    }
    clause->as.if_clause.condition = rexx_parser_parse_expression_internal(
        parser, 1, condition_stops);
    if (clause->as.if_clause.condition == NULL) {
        return NULL;
    }
    if (parser->current.type != TOKEN_KEYWORD_THEN) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_expected_then);
        return NULL;
    }
    if (!rexx_parser_advance(parser)) {
        return NULL;
    }

    /* Newlines after THEN are allowed; a semicolon would be a null clause. */
    (void)rexx_parser_skip_newlines(parser);
    if (parser->current.type == TOKEN_SEMICOLON ||
        rexx_parser_token_is_stop(&parser->current,
                                  inherited_stops | REXX_STOP_ELSE)) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_expected_instruction);
        return NULL;
    }

    clause->as.if_clause.then_clause = rexx_parser_parse_clause(
        parser, inherited_stops | REXX_STOP_ELSE);
    if (clause->as.if_clause.then_clause == NULL) {
        return NULL;
    }

    (void)rexx_parser_skip_separators(parser);
    if (parser->current.type == TOKEN_KEYWORD_ELSE) {
        if (!rexx_parser_advance(parser)) {
            return NULL;
        }
        (void)rexx_parser_skip_newlines(parser);
        if (parser->current.type == TOKEN_SEMICOLON ||
            rexx_parser_token_is_stop(&parser->current, inherited_stops)) {
            rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                                  &parser->current,
                                  rexx_parser_message_expected_instruction);
            return NULL;
        }
        clause->as.if_clause.else_clause = rexx_parser_parse_clause(
            parser, inherited_stops);
        if (clause->as.if_clause.else_clause == NULL) {
            return NULL;
        }
    }
    return clause;
}

static int rexx_parser_parse_do_expression(RexxParser *parser,
                                            RexxAstExpression **destination,
                                            unsigned int stop_mask)
{
    *destination = rexx_parser_parse_expression_internal(parser, 1, stop_mask);
    return *destination != NULL;
}

static int rexx_parser_parse_do_modifier(RexxParser *parser,
                                          RexxAstDo *do_clause)
{
    int modifier;

    switch (parser->current.type) {
    case TOKEN_KEYWORD_TO: modifier = 1; break;
    case TOKEN_KEYWORD_BY: modifier = 2; break;
    case TOKEN_KEYWORD_FOR: modifier = 3; break;
    case TOKEN_KEYWORD_WHILE: modifier = 4; break;
    case TOKEN_KEYWORD_UNTIL: modifier = 5; break;
    default: return 0;
    }

    if ((modifier == 1 || modifier == 2 || modifier == 3) &&
        do_clause->mode != REXX_AST_DO_CONTROLLED) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_unexpected_do_modifier);
        return -1;
    }
    if ((modifier == 1 && do_clause->has_to) ||
        (modifier == 2 && do_clause->has_by) ||
        (modifier == 3 && do_clause->has_for) ||
        (modifier == 4 && do_clause->has_while) ||
        (modifier == 5 && do_clause->has_until)) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_duplicate_do_modifier);
        return -1;
    }

    if (!rexx_parser_advance(parser)) {
        return -1;
    }
    if (modifier == 1) {
        do_clause->has_to = 1;
        return rexx_parser_parse_do_expression(parser, &do_clause->to,
                                               REXX_STOP_DO_MODIFIERS)
            ? 1 : -1;
    }
    if (modifier == 2) {
        do_clause->has_by = 1;
        return rexx_parser_parse_do_expression(parser, &do_clause->by,
                                               REXX_STOP_DO_MODIFIERS)
            ? 1 : -1;
    }
    if (modifier == 3) {
        do_clause->has_for = 1;
        return rexx_parser_parse_do_expression(parser, &do_clause->for_count,
                                               REXX_STOP_DO_MODIFIERS)
            ? 1 : -1;
    }
    if (modifier == 4) {
        do_clause->has_while = 1;
        return rexx_parser_parse_do_expression(parser,
                                               &do_clause->while_condition,
                                               REXX_STOP_WHILE |
                                               REXX_STOP_UNTIL)
            ? 1 : -1;
    }

    do_clause->has_until = 1;
    return rexx_parser_parse_do_expression(parser,
                                           &do_clause->until_condition,
                                           REXX_STOP_WHILE |
                                           REXX_STOP_UNTIL)
        ? 1 : -1;
}

static RexxAstClause *rexx_parser_parse_do(RexxParser *parser,
                                           unsigned int inherited_stops)
{
    RexxToken do_token = parser->current;
    RexxAstClause *clause = rexx_parser_new_clause(
        parser, REXX_AST_CLAUSE_DO, &do_token);
    RexxAstDo *do_clause;
    int condition_started = 0;

    if (clause == NULL || !rexx_parser_enter_nesting(parser)) {
        return NULL;
    }
    do_clause = &clause->as.do_clause;
    do_clause->mode = REXX_AST_DO_BLOCK;
    if (!rexx_parser_advance(parser)) {
        rexx_parser_leave_nesting(parser);
        return NULL;
    }

    if (rexx_parser_is_name_token(&parser->current) &&
        parser->lookahead.type == TOKEN_ASSIGN) {
        do_clause->mode = REXX_AST_DO_CONTROLLED;
        do_clause->has_control_variable = 1;
        do_clause->control_variable = parser->current;
        if (!rexx_parser_advance(parser) ||
            !rexx_parser_expect(parser, TOKEN_ASSIGN,
                                rexx_parser_message_unexpected_token) ||
            !rexx_parser_parse_do_expression(parser, &do_clause->start,
                                             REXX_STOP_DO_MODIFIERS)) {
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
    } else if (parser->current.type == TOKEN_KEYWORD_FOREVER) {
        do_clause->mode = REXX_AST_DO_FOREVER;
        if (!rexx_parser_advance(parser)) {
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
    } else if (parser->current.type == TOKEN_KEYWORD_WHILE) {
        condition_started = 1;
        if (rexx_parser_parse_do_modifier(parser, do_clause) < 0) {
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
    } else if (parser->current.type == TOKEN_KEYWORD_UNTIL) {
        condition_started = 1;
        if (rexx_parser_parse_do_modifier(parser, do_clause) < 0) {
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
    } else if (!rexx_parser_is_separator(&parser->current) &&
               parser->current.type != TOKEN_EOF &&
               parser->current.type != TOKEN_KEYWORD_END) {
        do_clause->mode = REXX_AST_DO_COUNTED;
        if (!rexx_parser_parse_do_expression(parser, &do_clause->start,
                                             REXX_STOP_DO_MODIFIERS)) {
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
    }

    while (parser->status == REXX_PARSER_STATUS_OK &&
           (parser->current.type == TOKEN_KEYWORD_TO ||
            parser->current.type == TOKEN_KEYWORD_BY ||
            parser->current.type == TOKEN_KEYWORD_FOR ||
            parser->current.type == TOKEN_KEYWORD_WHILE ||
            parser->current.type == TOKEN_KEYWORD_UNTIL)) {
        int is_conditional = parser->current.type == TOKEN_KEYWORD_WHILE ||
                             parser->current.type == TOKEN_KEYWORD_UNTIL;
        int modifier_result;

        if (!is_conditional && condition_started) {
            rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                                  &parser->current,
                                  rexx_parser_message_unexpected_do_modifier);
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
        modifier_result = rexx_parser_parse_do_modifier(parser, do_clause);
        if (is_conditional) {
            condition_started = 1;
        }
        if (modifier_result <= 0) {
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
    }

    if (parser->status != REXX_PARSER_STATUS_OK) {
        rexx_parser_leave_nesting(parser);
        return NULL;
    }
    if (!rexx_parser_is_separator(&parser->current) &&
        parser->current.type != TOKEN_KEYWORD_END) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_expected_do_separator);
        rexx_parser_leave_nesting(parser);
        return NULL;
    }
    (void)rexx_parser_skip_separators(parser);

    do_clause->body = rexx_parser_parse_clause_list(
        parser, inherited_stops | REXX_STOP_END);
    if (parser->status != REXX_PARSER_STATUS_OK) {
        rexx_parser_leave_nesting(parser);
        return NULL;
    }
    if (parser->current.type != TOKEN_KEYWORD_END) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_expected_end);
        rexx_parser_leave_nesting(parser);
        return NULL;
    }
    if (!rexx_parser_advance(parser)) {
        rexx_parser_leave_nesting(parser);
        return NULL;
    }
    if (parser->current.type == TOKEN_SYMBOL) {
        if (!do_clause->has_control_variable ||
            !rexx_parser_tokens_equal_case_insensitive(
                &do_clause->control_variable, &parser->current)) {
            rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                                  &parser->current,
                                  rexx_parser_message_end_name_mismatch);
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
        do_clause->end_name = parser->current;
        do_clause->has_end_name = 1;
        if (!rexx_parser_advance(parser)) {
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
    }
    rexx_parser_leave_nesting(parser);
    return clause;
}

static RexxAstClause *rexx_parser_parse_select(RexxParser *parser,
                                               unsigned int inherited_stops)
{
    RexxToken select_token = parser->current;
    RexxAstClause *clause = rexx_parser_new_clause(
        parser, REXX_AST_CLAUSE_SELECT, &select_token);
    RexxAstWhen *when_tail = NULL;
    unsigned int body_stops = inherited_stops | REXX_STOP_END |
                              REXX_STOP_WHEN | REXX_STOP_OTHERWISE;
    int saw_when = 0;
    size_t separators;

    if (clause == NULL || !rexx_parser_enter_nesting(parser)) {
        return NULL;
    }
    if (!rexx_parser_advance(parser)) {
        rexx_parser_leave_nesting(parser);
        return NULL;
    }
    separators = rexx_parser_skip_separators(parser);
    if (separators == 0u && parser->current.type != TOKEN_KEYWORD_END) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_expected_select_separator);
        rexx_parser_leave_nesting(parser);
        return NULL;
    }

    while (parser->status == REXX_PARSER_STATUS_OK &&
           parser->current.type == TOKEN_KEYWORD_WHEN) {
        RexxAstWhen *when_clause;
        unsigned int condition_stops = body_stops | REXX_STOP_THEN;

        saw_when = 1;
        when_clause = rexx_parser_new_when(parser);
        if (when_clause == NULL) {
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
        if (when_tail == NULL) {
            clause->as.select_clause.when_clauses = when_clause;
        } else {
            when_tail->next = when_clause;
        }
        when_tail = when_clause;

        if (!rexx_parser_advance(parser)) {
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
        when_clause->condition = rexx_parser_parse_expression_internal(
            parser, 1, condition_stops);
        if (when_clause->condition == NULL) {
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
        if (parser->current.type != TOKEN_KEYWORD_THEN) {
            rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                                  &parser->current,
                                  rexx_parser_message_expected_when_then);
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
        if (!rexx_parser_advance(parser)) {
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
        (void)rexx_parser_skip_newlines(parser);
        if (parser->current.type == TOKEN_SEMICOLON ||
            rexx_parser_token_is_stop(&parser->current, body_stops)) {
            rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                                  &parser->current,
                                  rexx_parser_message_expected_instruction);
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
        when_clause->body = rexx_parser_parse_clause(parser, body_stops);
        if (when_clause->body == NULL) {
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
        separators = rexx_parser_skip_separators(parser);
        if ((parser->current.type == TOKEN_KEYWORD_WHEN ||
             parser->current.type == TOKEN_KEYWORD_OTHERWISE ||
             parser->current.type == TOKEN_KEYWORD_END) &&
            separators == 0u &&
            !rexx_parser_is_separator(&parser->previous)) {
            rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                                  &parser->current,
                                  rexx_parser_message_expected_separator);
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
        if (parser->current.type != TOKEN_KEYWORD_WHEN &&
            parser->current.type != TOKEN_KEYWORD_OTHERWISE &&
            parser->current.type != TOKEN_KEYWORD_END &&
            !rexx_parser_token_is_stop(&parser->current, inherited_stops) &&
            parser->current.type != TOKEN_EOF) {
            rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                                  &parser->current,
                                  rexx_parser_message_expected_separator);
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
    }

    if (!saw_when) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_expected_when);
        rexx_parser_leave_nesting(parser);
        return NULL;
    }

    if (parser->current.type == TOKEN_KEYWORD_OTHERWISE) {
        if (!rexx_parser_advance(parser)) {
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
        (void)rexx_parser_skip_newlines(parser);
        if (parser->current.type == TOKEN_SEMICOLON ||
            rexx_parser_token_is_stop(&parser->current, body_stops)) {
            rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                                  &parser->current,
                                  rexx_parser_message_missing_otherwise_instruction);
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
        clause->as.select_clause.otherwise_clause =
            rexx_parser_parse_clause(parser, inherited_stops | REXX_STOP_END);
        if (clause->as.select_clause.otherwise_clause == NULL) {
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
        (void)rexx_parser_skip_separators(parser);
        if (parser->current.type == TOKEN_KEYWORD_END &&
            !rexx_parser_is_separator(&parser->previous)) {
            rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                                  &parser->current,
                                  rexx_parser_message_expected_separator);
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
        if (parser->current.type == TOKEN_KEYWORD_WHEN ||
            parser->current.type == TOKEN_KEYWORD_OTHERWISE) {
            rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                                  &parser->current,
                                  rexx_parser_message_otherwise_order);
            rexx_parser_leave_nesting(parser);
            return NULL;
        }
    }

    if (parser->current.type != TOKEN_KEYWORD_END) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_expected_end);
        rexx_parser_leave_nesting(parser);
        return NULL;
    }
    if (!rexx_parser_advance(parser)) {
        rexx_parser_leave_nesting(parser);
        return NULL;
    }
    if (parser->current.type == TOKEN_SYMBOL) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_unexpected_token);
        rexx_parser_leave_nesting(parser);
        return NULL;
    }
    rexx_parser_leave_nesting(parser);
    return clause;
}

static RexxAstClause *rexx_parser_parse_symbol_clause(
    RexxParser *parser,
    unsigned int stop_mask)
{
    RexxToken token = parser->current;
    RexxAstClause *clause;

    if (parser->lookahead.type == TOKEN_COLON) {
        clause = rexx_parser_new_clause(parser, REXX_AST_CLAUSE_LABEL, &token);
        if (clause == NULL || !rexx_parser_advance(parser) ||
            !rexx_parser_expect(parser, TOKEN_COLON,
                                rexx_parser_message_unexpected_token)) {
            return NULL;
        }
        clause->as.label.label = token;
        return clause;
    }

    if (parser->lookahead.type == TOKEN_ASSIGN) {
        clause = rexx_parser_new_clause(parser, REXX_AST_CLAUSE_ASSIGNMENT,
                                        &token);
        if (clause == NULL || !rexx_parser_advance(parser) ||
            !rexx_parser_expect(parser, TOKEN_ASSIGN,
                                rexx_parser_message_unexpected_token)) {
            return NULL;
        }
        clause->as.assignment.variable = token;
        clause->as.assignment.value = rexx_parser_parse_expression_internal(
            parser, 1, stop_mask);
        if (clause->as.assignment.value == NULL) {
            return NULL;
        }
        return clause;
    }

    clause = rexx_parser_new_clause(parser, REXX_AST_CLAUSE_COMMAND, &token);
    if (clause == NULL) {
        return NULL;
    }
    clause->as.simple.expression = rexx_parser_parse_expression_internal(
        parser, 1, stop_mask);
    if (clause->as.simple.expression == NULL) {
        return NULL;
    }
    return clause;
}

static RexxAstClause *rexx_parser_parse_call_clause(
    RexxParser *parser,
    unsigned int stop_mask)
{
    RexxToken call_token = parser->current;
    RexxToken name;
    RexxAstClause *clause;
    int has_parentheses;

    (void)stop_mask;
    clause = rexx_parser_new_clause(parser, REXX_AST_CLAUSE_CALL, &call_token);
    if (clause == NULL || !rexx_parser_advance(parser)) {
        return NULL;
    }
    if (!rexx_parser_is_name_token(&parser->current)) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_expected_call_name);
        return NULL;
    }

    name = parser->current;
    has_parentheses = parser->lookahead.type == TOKEN_LEFT_PAREN &&
        rexx_parser_tokens_are_adjacent(&name, &parser->lookahead);
    if (!rexx_parser_advance(parser)) {
        return NULL;
    }
    clause->as.call.call = rexx_parser_parse_call_after_name(
        parser, name, has_parentheses);
    return clause->as.call.call != NULL ? clause : NULL;
}

static RexxAstClause *rexx_parser_parse_numeric_clause(
    RexxParser *parser,
    unsigned int stop_mask)
{
    RexxToken token = parser->current;
    RexxAstClause *clause;

    clause = rexx_parser_new_clause(parser, REXX_AST_CLAUSE_NUMERIC_DIGITS,
                                    &token);
    if (clause == NULL || !rexx_parser_advance(parser)) return NULL;
    if (parser->current.type != TOKEN_SYMBOL ||
        !rexx_token_equals_ascii_case_insensitive(&parser->current, "DIGITS")) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_unsupported_numeric_setting);
        return NULL;
    }
    if (!rexx_parser_advance(parser)) return NULL;
    if (!rexx_parser_token_is_stop(&parser->current, stop_mask)) {
        clause->as.simple.expression = rexx_parser_parse_expression_internal(
            parser, 1, stop_mask);
        if (clause->as.simple.expression == NULL) return NULL;
    }
    return clause;
}

static RexxAstClause *rexx_parser_parse_simple_keyword(
    RexxParser *parser,
    RexxAstClauseKind kind,
    unsigned int stop_mask)
{
    RexxToken token = parser->current;
    RexxAstClause *clause = rexx_parser_new_clause(parser, kind, &token);

    if (clause == NULL || !rexx_parser_advance(parser)) {
        return NULL;
    }
    if (kind == REXX_AST_CLAUSE_SAY ||
        kind == REXX_AST_CLAUSE_ADDRESS) {
        /* Keep an explicit ADDRESS environment separate from command text. */
        if (kind == REXX_AST_CLAUSE_ADDRESS &&
            !rexx_parser_token_is_stop(&parser->current, stop_mask) &&
            rexx_parser_is_name_token(&parser->current) &&
            (rexx_parser_token_is_stop(&parser->lookahead, stop_mask) ||
             (rexx_parser_can_start_term(&parser->lookahead) &&
              !rexx_parser_tokens_are_adjacent(&parser->current,
                                               &parser->lookahead)))) {
            clause->as.simple.address_environment = parser->current;
            clause->as.simple.has_address_environment = 1;
            if (!rexx_parser_advance(parser)) {
                return NULL;
            }
        }
        if (!rexx_parser_token_is_stop(&parser->current, stop_mask)) {
            clause->as.simple.expression =
                rexx_parser_parse_expression_internal(parser, 1, stop_mask);
            if (clause->as.simple.expression == NULL &&
                kind != REXX_AST_CLAUSE_ADDRESS) {
                return NULL;
            }
        }
    } else if (kind == REXX_AST_CLAUSE_RETURN ||
               kind == REXX_AST_CLAUSE_EXIT) {
        if (!rexx_parser_token_is_stop(&parser->current, stop_mask)) {
            clause->as.simple.expression =
                rexx_parser_parse_expression_internal(parser, 1, stop_mask);
            if (clause->as.simple.expression == NULL) {
                return NULL;
            }
        }
    }
    return clause;
}

static RexxAstClause *rexx_parser_parse_clause(RexxParser *parser,
                                               unsigned int stop_mask)
{
    RexxAstClause *clause = NULL;

    if (!rexx_parser_enter_nesting(parser)) {
        return NULL;
    }
    if (!rexx_parser_check_lexical_error(parser)) {
        rexx_parser_leave_nesting(parser);
        return NULL;
    }

    switch (parser->current.type) {
    case TOKEN_KEYWORD_NUMERIC:
        clause = rexx_parser_parse_numeric_clause(parser, stop_mask);
        break;
    case TOKEN_KEYWORD_ADDRESS:
        clause = rexx_parser_parse_simple_keyword(
            parser, REXX_AST_CLAUSE_ADDRESS, stop_mask);
        break;
    case TOKEN_SYMBOL:
        clause = rexx_parser_parse_symbol_clause(parser, stop_mask);
        break;
    case TOKEN_KEYWORD_IF:
        clause = rexx_parser_parse_if(parser, stop_mask);
        break;
    case TOKEN_KEYWORD_DO:
        clause = rexx_parser_parse_do(parser, stop_mask);
        break;
    case TOKEN_KEYWORD_SELECT:
        clause = rexx_parser_parse_select(parser, stop_mask);
        break;
    case TOKEN_KEYWORD_SAY:
        clause = rexx_parser_parse_simple_keyword(
            parser, REXX_AST_CLAUSE_SAY, stop_mask);
        break;
    case TOKEN_KEYWORD_CALL:
        clause = rexx_parser_parse_call_clause(parser, stop_mask);
        break;
    case TOKEN_KEYWORD_RETURN:
        clause = rexx_parser_parse_simple_keyword(
            parser, REXX_AST_CLAUSE_RETURN, stop_mask);
        break;
    case TOKEN_KEYWORD_EXIT:
        clause = rexx_parser_parse_simple_keyword(
            parser, REXX_AST_CLAUSE_EXIT, stop_mask);
        break;
    case TOKEN_KEYWORD_NOP:
        clause = rexx_parser_parse_simple_keyword(
            parser, REXX_AST_CLAUSE_NOP, stop_mask);
        break;
    case TOKEN_KEYWORD_LEAVE:
        clause = rexx_parser_parse_simple_keyword(
            parser, REXX_AST_CLAUSE_LEAVE, stop_mask);
        break;
    case TOKEN_KEYWORD_ITERATE:
        clause = rexx_parser_parse_simple_keyword(
            parser, REXX_AST_CLAUSE_ITERATE, stop_mask);
        break;
    default:
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                              &parser->current,
                              rexx_parser_message_expected_clause);
        break;
    }

    rexx_parser_leave_nesting(parser);
    return clause;
}

static void rexx_parser_free_expression(RexxAstExpression *expression);
static void rexx_parser_free_clause_list(RexxAstClause *clauses);

static void rexx_parser_free_arguments(RexxAstArgument *arguments)
{
    while (arguments != NULL) {
        RexxAstArgument *next = arguments->next;
        rexx_parser_free_expression(arguments->expression);
        rexx_parser_deallocate(arguments);
        arguments = next;
    }
}

static void rexx_parser_free_expression(RexxAstExpression *expression)
{
    if (expression == NULL) {
        return;
    }
    rexx_parser_free_expression(expression->left);
    rexx_parser_free_expression(expression->right);
    rexx_parser_free_arguments(expression->arguments);
    rexx_parser_deallocate(expression);
}

static void rexx_parser_free_when_list(RexxAstWhen *when_clauses)
{
    while (when_clauses != NULL) {
        RexxAstWhen *next = when_clauses->next;
        rexx_parser_free_expression(when_clauses->condition);
        rexx_parser_free_clause_list(when_clauses->body);
        rexx_parser_deallocate(when_clauses);
        when_clauses = next;
    }
}

static void rexx_parser_free_clause_list(RexxAstClause *clauses)
{
    while (clauses != NULL) {
        RexxAstClause *next = clauses->next;

        switch (clauses->kind) {
        case REXX_AST_CLAUSE_ASSIGNMENT:
            rexx_parser_free_expression(clauses->as.assignment.value);
            break;
        case REXX_AST_CLAUSE_SAY:
        case REXX_AST_CLAUSE_ADDRESS:
        case REXX_AST_CLAUSE_RETURN:
        case REXX_AST_CLAUSE_EXIT:
        case REXX_AST_CLAUSE_COMMAND:
        case REXX_AST_CLAUSE_NUMERIC_DIGITS:
            rexx_parser_free_expression(clauses->as.simple.expression);
            break;
        case REXX_AST_CLAUSE_CALL:
            rexx_parser_free_expression(clauses->as.call.call);
            break;
        case REXX_AST_CLAUSE_IF:
            rexx_parser_free_expression(clauses->as.if_clause.condition);
            rexx_parser_free_clause_list(clauses->as.if_clause.then_clause);
            rexx_parser_free_clause_list(clauses->as.if_clause.else_clause);
            break;
        case REXX_AST_CLAUSE_DO:
            rexx_parser_free_expression(clauses->as.do_clause.start);
            rexx_parser_free_expression(clauses->as.do_clause.to);
            rexx_parser_free_expression(clauses->as.do_clause.by);
            rexx_parser_free_expression(clauses->as.do_clause.for_count);
            rexx_parser_free_expression(clauses->as.do_clause.while_condition);
            rexx_parser_free_expression(clauses->as.do_clause.until_condition);
            rexx_parser_free_clause_list(clauses->as.do_clause.body);
            break;
        case REXX_AST_CLAUSE_SELECT:
            rexx_parser_free_when_list(
                clauses->as.select_clause.when_clauses);
            rexx_parser_free_clause_list(
                clauses->as.select_clause.otherwise_clause);
            break;
        case REXX_AST_CLAUSE_LABEL:
        case REXX_AST_CLAUSE_NOP:
        case REXX_AST_CLAUSE_LEAVE:
        case REXX_AST_CLAUSE_ITERATE:
        default:
            break;
        }
        rexx_parser_deallocate(clauses);
        clauses = next;
    }
}

RexxParserStatus rexx_parser_init(RexxParser *parser, RexxLexer *lexer)
{
    if (parser == NULL) {
        return REXX_PARSER_STATUS_INVALID_ARGUMENT;
    }
    memset(parser, 0, sizeof(*parser));
    parser->status = REXX_PARSER_STATUS_OK;
    parser->error.status = REXX_PARSER_STATUS_OK;
    if (lexer == NULL || !lexer->initialized || lexer->source == NULL ||
        lexer->status != REXX_LEXER_STATUS_OK || lexer->offset > lexer->length ||
        lexer->line == 0u || lexer->column == 0u) {
        parser->status = REXX_PARSER_STATUS_INVALID_ARGUMENT;
        parser->error.status = parser->status;
        parser->error.token.type = TOKEN_EOF;
        parser->error.token.line = 1u;
        parser->error.token.column = 1u;
        parser->error.message = rexx_parser_message_invalid_lexer;
        return parser->status;
    }

    parser->lexer = lexer;
    parser->current = rexx_lexer_next_token(lexer);
    parser->lookahead = rexx_lexer_next_token(lexer);
    parser->previous.type = TOKEN_EOF;
    parser->previous.line = parser->current.line;
    parser->previous.column = parser->current.column;
    parser->initialized = 1;
    return REXX_PARSER_STATUS_OK;
}

RexxParserStatus rexx_parser_parse(RexxParser *parser,
                                   RexxAstProgram **program)
{
    RexxAstProgram *ast;

    if (program != NULL) {
        *program = NULL;
    }
    if (parser == NULL || program == NULL || !parser->initialized ||
        parser->lexer == NULL) {
        return REXX_PARSER_STATUS_INVALID_ARGUMENT;
    }
    if (parser->parsed) {
        rexx_parser_set_error(parser, REXX_PARSER_STATUS_INVALID_ARGUMENT,
                              &parser->current,
                              rexx_parser_message_already_parsed);
        return parser->status;
    }
    parser->parsed = 1;

    if (!rexx_parser_check_lexical_error(parser)) {
        return parser->status;
    }
    ast = (RexxAstProgram *)rexx_parser_allocate(parser, sizeof(*ast));
    if (ast == NULL) {
        return parser->status;
    }
    ast->clauses = rexx_parser_parse_clause_list(parser, 0u);
    if (parser->status == REXX_PARSER_STATUS_OK &&
        parser->current.type != TOKEN_EOF) {
        if (!rexx_parser_check_lexical_error(parser)) {
            /* Preserve the lexer diagnostic. */
        } else {
            rexx_parser_set_error(parser, REXX_PARSER_STATUS_SYNTAX_ERROR,
                                  &parser->current,
                                  rexx_parser_message_unexpected_token);
        }
    }

    if (parser->status != REXX_PARSER_STATUS_OK) {
        /*
         * A failure can happen before a partially built child is attached to
         * its parent. Reclaim the private allocation list so no such node can
         * leak or require unsafe traversal of an incomplete tree.
         */
        rexx_parser_discard_allocations(parser);
        return parser->status;
    }
    /* Ownership transfers to the returned AST, which is freed structurally. */
    parser->allocation_list = NULL;
    *program = ast;
    return REXX_PARSER_STATUS_OK;
}

void rexx_parser_free_ast(RexxAstProgram *program)
{
    if (program == NULL) {
        return;
    }
    rexx_parser_free_clause_list(program->clauses);
    rexx_parser_deallocate(program);
}

const RexxParserError *rexx_parser_get_error(const RexxParser *parser)
{
    if (parser == NULL || parser->error.status == REXX_PARSER_STATUS_OK) {
        return NULL;
    }
    return &parser->error;
}
