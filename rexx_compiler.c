/*
 * Bytecode compiler for the parser-owned native guideXOS REXX AST.
 *
 * The compiler copies every token-backed pool entry, bounds recursion and
 * generated code, and keeps jump fixups local to their owning control form.
 * The bytecode VM can therefore outlive both the parser and source buffer.
 */

#include "rexx_compiler.h"

#include <stdlib.h>
#include <string.h>

typedef struct RexxIndexList {
    size_t *items;
    size_t count;
    size_t capacity;
} RexxIndexList;

typedef struct RexxLoopContext {
    RexxIndexList breaks;
    RexxIndexList continues;
} RexxLoopContext;

typedef struct RexxCompiler {
    RexxBytecodeChunk *chunk;
    RexxCompilerError *error;
    RexxCompilerStatus status;
    size_t instruction_capacity;
    size_t constant_capacity;
    size_t symbol_capacity;
    size_t visited_nodes;
    size_t expression_depth;
    size_t clause_depth;
    size_t loop_depth;
    RexxLoopContext loops[REXX_COMPILER_MAX_NESTING];
} RexxCompiler;

static const char rexx_compiler_message_invalid_argument[] =
    "compiler requires a valid program and output pointer";
static const char rexx_compiler_message_out_of_memory[] =
    "out of memory while building bytecode";
static const char rexx_compiler_message_resource_limit[] =
    "compiler resource limit exceeded";
static const char rexx_compiler_message_malformed_ast[] =
    "malformed or incomplete AST node";
static const char rexx_compiler_message_unsupported_expression[] =
    "expression form or operator is not supported by the bytecode compiler";
static const char rexx_compiler_message_unsupported_clause[] =
    "clause form is not supported by the bytecode compiler";
static const char rexx_compiler_message_bad_token[] =
    "AST token has no valid source text";
static const char rexx_compiler_message_bad_jump[] =
    "bytecode jump target is outside the supported range";
static const char rexx_compiler_message_bad_loop[] =
    "DO clause has inconsistent control fields";

static void rexx_compiler_fail(RexxCompiler *compiler,
                              RexxCompilerStatus status,
                              const RexxToken *token,
                              const char *message)
{
    if (compiler == NULL || compiler->status != REXX_COMPILER_STATUS_OK) {
        return;
    }

    compiler->status = status;
    if (compiler->error != NULL) {
        compiler->error->status = status;
        if (token != NULL) {
            compiler->error->token = *token;
        } else {
            memset(&compiler->error->token, 0,
                   sizeof(compiler->error->token));
        }
        compiler->error->message = message;
    }
}

static void rexx_compiler_fail_oom(RexxCompiler *compiler,
                                   const RexxToken *token)
{
    rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_OUT_OF_MEMORY, token,
                       rexx_compiler_message_out_of_memory);
}

static void rexx_compiler_fail_limit(RexxCompiler *compiler,
                                     const RexxToken *token)
{
    rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_RESOURCE_LIMIT, token,
                       rexx_compiler_message_resource_limit);
}

static int rexx_compiler_visit(RexxCompiler *compiler,
                               const RexxToken *token)
{
    if (compiler->visited_nodes >= REXX_COMPILER_MAX_POOL_ENTRIES) {
        rexx_compiler_fail_limit(compiler, token);
        return 0;
    }
    ++compiler->visited_nodes;
    return 1;
}

/* Overflow-checked geometric growth used by every compiler-owned array. */
static int rexx_compiler_reserve(void **array,
                                size_t element_size,
                                size_t *capacity,
                                size_t required,
                                size_t maximum)
{
    size_t next_capacity;
    void *grown;

    if (array == NULL || capacity == NULL || element_size == 0u ||
        required > maximum) {
        return 0;
    }
    if (required <= *capacity) {
        return 1;
    }

    next_capacity = (*capacity != 0u) ? *capacity : 8u;
    while (next_capacity < required) {
        if (next_capacity > maximum / 2u) {
            next_capacity = maximum;
            break;
        }
        next_capacity *= 2u;
    }
    if (next_capacity < required ||
        next_capacity > ((size_t)-1) / element_size) {
        return 0;
    }

    grown = realloc(*array, next_capacity * element_size);
    if (grown == NULL) {
        return 0;
    }
    *array = grown;
    *capacity = next_capacity;
    return 1;
}

static int rexx_compiler_token_is_valid(const RexxToken *token)
{
    return token != NULL && token->length != 0u && token->lexeme != NULL;
}

static int rexx_compiler_token_is_name(const RexxToken *token)
{
    return rexx_compiler_token_is_valid(token) &&
           (token->type == TOKEN_SYMBOL ||
            (token->type >= TOKEN_KEYWORD_ADDRESS &&
             token->type <= TOKEN_KEYWORD_WHILE));
}

static uint32_t rexx_compiler_emit(RexxCompiler *compiler,
                                  RexxOpcode opcode,
                                  uint8_t flags,
                                  uint32_t operand,
                                  uint32_t auxiliary,
                                  const RexxToken *token)
{
    RexxInstruction *instruction;
    size_t index;

    if (compiler->status != REXX_COMPILER_STATUS_OK) {
        return 0u;
    }
    if (compiler->chunk->instruction_count >=
        REXX_COMPILER_MAX_INSTRUCTIONS ||
        compiler->chunk->instruction_count > (size_t)UINT32_MAX) {
        rexx_compiler_fail_limit(compiler, token);
        return 0u;
    }

    index = compiler->chunk->instruction_count;
    if (!rexx_compiler_reserve(
            (void **)&compiler->chunk->instructions,
            sizeof(*compiler->chunk->instructions),
            &compiler->instruction_capacity,
            index + 1u,
            REXX_COMPILER_MAX_INSTRUCTIONS)) {
        if (index + 1u > REXX_COMPILER_MAX_INSTRUCTIONS) {
            rexx_compiler_fail_limit(compiler, token);
        } else {
            rexx_compiler_fail_oom(compiler, token);
        }
        return 0u;
    }

    instruction = &compiler->chunk->instructions[index];
    instruction->opcode = (uint8_t)opcode;
    instruction->flags = flags;
    instruction->reserved = 0u;
    instruction->operand = operand;
    instruction->auxiliary = auxiliary;
    compiler->chunk->instruction_count = index + 1u;
    return (uint32_t)index;
}

static int rexx_compiler_patch(RexxCompiler *compiler,
                               size_t instruction_index,
                               size_t target,
                               const RexxToken *token)
{
    if (instruction_index >= compiler->chunk->instruction_count ||
        target > (size_t)UINT32_MAX ||
        target > REXX_COMPILER_MAX_INSTRUCTIONS) {
        rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_RESOURCE_LIMIT,
                           token, rexx_compiler_message_bad_jump);
        return 0;
    }
    compiler->chunk->instructions[instruction_index].operand =
        (uint32_t)target;
    return 1;
}

static int rexx_compiler_index_list_push(RexxCompiler *compiler,
                                         RexxIndexList *list,
                                         size_t index,
                                         const RexxToken *token)
{
    if (list->count >= REXX_COMPILER_MAX_INSTRUCTIONS ||
        !rexx_compiler_reserve((void **)&list->items, sizeof(*list->items),
                               &list->capacity, list->count + 1u,
                               REXX_COMPILER_MAX_INSTRUCTIONS)) {
        if (list->count >= REXX_COMPILER_MAX_INSTRUCTIONS) {
            rexx_compiler_fail_limit(compiler, token);
        } else {
            rexx_compiler_fail_oom(compiler, token);
        }
        return 0;
    }
    list->items[list->count++] = index;
    return 1;
}

static void rexx_compiler_index_list_free(RexxIndexList *list)
{
    if (list != NULL) {
        free(list->items);
        list->items = NULL;
        list->count = 0u;
        list->capacity = 0u;
    }
}

static int rexx_compiler_token_has_dot(const RexxToken *token)
{
    size_t index;
    if (!rexx_compiler_token_is_valid(token)) {
        return 0;
    }
    for (index = 0u; index < token->length; ++index) {
        if (token->lexeme[index] == '.') {
            return 1;
        }
    }
    return 0;
}

static unsigned char rexx_compiler_ascii_upper(unsigned char ch)
{
    if (ch >= (unsigned char)'a' && ch <= (unsigned char)'z') {
        return (unsigned char)(ch - (unsigned char)'a' + (unsigned char)'A');
    }
    return ch;
}

static uint32_t rexx_compiler_intern_symbol(RexxCompiler *compiler,
                                           const RexxToken *token,
                                           RexxSymbolKind kind)
{
    RexxSymbol *symbols = compiler->chunk->symbols;
    size_t index;
    char *name;

    if (!rexx_compiler_token_is_name(token)) {
        rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                           token, rexx_compiler_message_bad_token);
        return 0u;
    }
    if (token->length > (size_t)UINT32_MAX) {
        rexx_compiler_fail_limit(compiler, token);
        return 0u;
    }

    for (index = 0u; index < compiler->chunk->symbol_count; ++index) {
        size_t offset;
        if (symbols[index].kind != kind || symbols[index].length != token->length) {
            continue;
        }
        for (offset = 0u; offset < token->length; ++offset) {
            if ((unsigned char)symbols[index].name[offset] !=
                rexx_compiler_ascii_upper((unsigned char)token->lexeme[offset])) {
                break;
            }
        }
        if (offset == token->length) {
            return (uint32_t)index;
        }
    }

    if (compiler->chunk->symbol_count >= REXX_COMPILER_MAX_POOL_ENTRIES ||
        compiler->chunk->symbol_count > (size_t)UINT32_MAX) {
        rexx_compiler_fail_limit(compiler, token);
        return 0u;
    }
    if (token->length == (size_t)-1) {
        rexx_compiler_fail_limit(compiler, token);
        return 0u;
    }
    name = (char *)malloc(token->length + 1u);
    if (name == NULL) {
        rexx_compiler_fail_oom(compiler, token);
        return 0u;
    }
    for (index = 0u; index < token->length; ++index) {
        name[index] = (char)rexx_compiler_ascii_upper(
            (unsigned char)token->lexeme[index]);
    }
    name[token->length] = '\0';

    index = compiler->chunk->symbol_count;
    if (!rexx_compiler_reserve((void **)&compiler->chunk->symbols,
                               sizeof(*compiler->chunk->symbols),
                               &compiler->symbol_capacity, index + 1u,
                               REXX_COMPILER_MAX_POOL_ENTRIES)) {
        free(name);
        if (index + 1u > REXX_COMPILER_MAX_POOL_ENTRIES) {
            rexx_compiler_fail_limit(compiler, token);
        } else {
            rexx_compiler_fail_oom(compiler, token);
        }
        return 0u;
    }
    symbols = compiler->chunk->symbols;
    symbols[index].name = name;
    symbols[index].length = token->length;
    symbols[index].kind = kind;
    compiler->chunk->symbol_count = index + 1u;
    return (uint32_t)index;
}

static uint32_t rexx_compiler_add_constant(RexxCompiler *compiler,
                                          const RexxToken *token)
{
    RexxConstantKind kind;
    RexxConstant *constants = compiler->chunk->constants;
    char *text;
    size_t index;

    if (!rexx_compiler_token_is_valid(token)) {
        rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                           token, rexx_compiler_message_bad_token);
        return 0u;
    }
    switch (token->type) {
    case TOKEN_NUMBER: kind = REXX_CONSTANT_NUMBER; break;
    case TOKEN_STRING: kind = REXX_CONSTANT_STRING; break;
    case TOKEN_HEX_STRING: kind = REXX_CONSTANT_HEX_STRING; break;
    case TOKEN_BINARY_STRING: kind = REXX_CONSTANT_BINARY_STRING; break;
    default:
        rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                           token, rexx_compiler_message_bad_token);
        return 0u;
    }

    for (index = 0u; index < compiler->chunk->constant_count; ++index) {
        if (constants[index].kind == kind &&
            constants[index].length == token->length &&
            memcmp(constants[index].text, token->lexeme, token->length) == 0) {
            return (uint32_t)index;
        }
    }
    if (compiler->chunk->constant_count >= REXX_COMPILER_MAX_POOL_ENTRIES ||
        compiler->chunk->constant_count > (size_t)UINT32_MAX ||
        token->length == (size_t)-1) {
        rexx_compiler_fail_limit(compiler, token);
        return 0u;
    }

    text = (char *)malloc(token->length + 1u);
    if (text == NULL) {
        rexx_compiler_fail_oom(compiler, token);
        return 0u;
    }
    memcpy(text, token->lexeme, token->length);
    text[token->length] = '\0';

    index = compiler->chunk->constant_count;
    if (!rexx_compiler_reserve((void **)&compiler->chunk->constants,
                               sizeof(*compiler->chunk->constants),
                               &compiler->constant_capacity, index + 1u,
                               REXX_COMPILER_MAX_POOL_ENTRIES)) {
        free(text);
        if (index + 1u > REXX_COMPILER_MAX_POOL_ENTRIES) {
            rexx_compiler_fail_limit(compiler, token);
        } else {
            rexx_compiler_fail_oom(compiler, token);
        }
        return 0u;
    }
    constants = compiler->chunk->constants;
    constants[index].kind = kind;
    constants[index].text = text;
    constants[index].length = token->length;
    compiler->chunk->constant_count = index + 1u;
    return (uint32_t)index;
}

static int rexx_compiler_emit_variable(RexxCompiler *compiler,
                                       const RexxToken *token,
                                       int store)
{
    int is_stem = rexx_compiler_token_has_dot(token);
    uint32_t symbol = rexx_compiler_intern_symbol(
        compiler, token, is_stem ? REXX_SYMBOL_STEM : REXX_SYMBOL_VARIABLE);

    if (compiler->status != REXX_COMPILER_STATUS_OK) {
        return 0;
    }
    (void)rexx_compiler_emit(
        compiler,
        is_stem ? (store ? OP_STORE_STEM : OP_LOAD_STEM)
                : (store ? OP_STORE_VAR : OP_LOAD_VAR),
        0u, symbol, 0u, token);
    return compiler->status == REXX_COMPILER_STATUS_OK;
}

static int rexx_compiler_map_operator(RexxAstOperator operation,
                                      RexxOpcode *opcode,
                                      int *arity)
{
    if (opcode == NULL || arity == NULL) {
        return 0;
    }
    *arity = 2;
    switch (operation) {
    case REXX_AST_OP_POSITIVE: *opcode = OP_POSITIVE; *arity = 1; return 1;
    case REXX_AST_OP_NEGATIVE: *opcode = OP_NEGATE; *arity = 1; return 1;
    case REXX_AST_OP_LOGICAL_NOT: *opcode = OP_LOGICAL_NOT; *arity = 1; return 1;
    case REXX_AST_OP_POWER: *opcode = OP_POWER; return 1;
    case REXX_AST_OP_MULTIPLY: *opcode = OP_MULTIPLY; return 1;
    case REXX_AST_OP_DIVIDE: *opcode = OP_DIVIDE; return 1;
    case REXX_AST_OP_INTEGER_DIVIDE: *opcode = OP_INTEGER_DIVIDE; return 1;
    case REXX_AST_OP_REMAINDER: *opcode = OP_REMAINDER; return 1;
    case REXX_AST_OP_ADD: *opcode = OP_ADD; return 1;
    case REXX_AST_OP_SUBTRACT: *opcode = OP_SUB; return 1;
    case REXX_AST_OP_CONCAT: *opcode = OP_CONCAT; return 1;
    case REXX_AST_OP_CONCAT_BLANK: *opcode = OP_CONCAT_BLANK; return 1;
    case REXX_AST_OP_CONCAT_ABUTTAL: *opcode = OP_CONCAT_ABUTTAL; return 1;
    case REXX_AST_OP_EQUAL: *opcode = OP_EQUAL; return 1;
    case REXX_AST_OP_STRICT_EQUAL: *opcode = OP_STRICT_EQUAL; return 1;
    case REXX_AST_OP_NOT_EQUAL: *opcode = OP_NOT_EQUAL; return 1;
    case REXX_AST_OP_STRICT_NOT_EQUAL: *opcode = OP_STRICT_NOT_EQUAL; return 1;
    case REXX_AST_OP_GREATER: *opcode = OP_GREATER; return 1;
    case REXX_AST_OP_LESS: *opcode = OP_LESS; return 1;
    case REXX_AST_OP_GREATER_EQUAL: *opcode = OP_GREATER_EQUAL; return 1;
    case REXX_AST_OP_LESS_EQUAL: *opcode = OP_LESS_EQUAL; return 1;
    case REXX_AST_OP_NOT_GREATER: *opcode = OP_NOT_GREATER; return 1;
    case REXX_AST_OP_NOT_LESS: *opcode = OP_NOT_LESS; return 1;
    case REXX_AST_OP_STRICT_GREATER: *opcode = OP_STRICT_GREATER; return 1;
    case REXX_AST_OP_STRICT_LESS: *opcode = OP_STRICT_LESS; return 1;
    case REXX_AST_OP_STRICT_GREATER_EQUAL: *opcode = OP_STRICT_GREATER_EQUAL; return 1;
    case REXX_AST_OP_STRICT_LESS_EQUAL: *opcode = OP_STRICT_LESS_EQUAL; return 1;
    case REXX_AST_OP_STRICT_NOT_GREATER: *opcode = OP_STRICT_NOT_GREATER; return 1;
    case REXX_AST_OP_STRICT_NOT_LESS: *opcode = OP_STRICT_NOT_LESS; return 1;
    case REXX_AST_OP_LOGICAL_AND: *opcode = OP_LOGICAL_AND; return 1;
    case REXX_AST_OP_LOGICAL_OR: *opcode = OP_LOGICAL_OR; return 1;
    case REXX_AST_OP_LOGICAL_XOR: *opcode = OP_LOGICAL_XOR; return 1;
    default: return 0;
    }
}

static int rexx_compiler_compile_expression(RexxCompiler *compiler,
                                            const RexxAstExpression *expression);

static int rexx_compiler_compile_arguments(RexxCompiler *compiler,
                                           const RexxAstArgument *arguments,
                                           uint32_t *argument_count)
{
    size_t count = 0u;
    const RexxAstArgument *argument;

    if (argument_count == NULL) {
        rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_INVALID_ARGUMENT,
                           NULL, rexx_compiler_message_invalid_argument);
        return 0;
    }
    for (argument = arguments; argument != NULL; argument = argument->next) {
        if (!rexx_compiler_visit(compiler, NULL)) {
            return 0;
        }
        if (argument->expression == NULL) {
            (void)rexx_compiler_emit(compiler, OP_LOAD_UNSET, 0u, 0u, 0u,
                                     NULL);
            if (compiler->status != REXX_COMPILER_STATUS_OK) {
                return 0;
            }
        } else if (!rexx_compiler_compile_expression(compiler,
                                                      argument->expression)) {
            if (compiler->status == REXX_COMPILER_STATUS_OK) {
                rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                                   NULL, rexx_compiler_message_malformed_ast);
            }
            return 0;
        }
        ++count;
    }
    if (count > (size_t)UINT32_MAX) {
        rexx_compiler_fail_limit(compiler, NULL);
        return 0;
    }
    *argument_count = (uint32_t)count;
    return 1;
}

static int rexx_compiler_compile_expression(RexxCompiler *compiler,
                                            const RexxAstExpression *expression)
{
    int result = 0;
    RexxOpcode opcode;
    int arity;

    if (compiler->status != REXX_COMPILER_STATUS_OK) {
        return 0;
    }
    if (expression == NULL) {
        rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                           NULL, rexx_compiler_message_malformed_ast);
        return 0;
    }
    if (!rexx_compiler_token_is_valid(&expression->token)) {
        rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                           &expression->token,
                           rexx_compiler_message_bad_token);
        return 0;
    }
    if (compiler->expression_depth >= REXX_COMPILER_MAX_NESTING ||
        !rexx_compiler_visit(compiler, &expression->token)) {
        if (compiler->status == REXX_COMPILER_STATUS_OK) {
            rexx_compiler_fail_limit(compiler, &expression->token);
        }
        return 0;
    }
    ++compiler->expression_depth;

    switch (expression->kind) {
    case REXX_AST_EXPR_LITERAL: {
        if (expression->left != NULL || expression->right != NULL ||
            expression->arguments != NULL) {
            rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                               &expression->token,
                               rexx_compiler_message_malformed_ast);
            break;
        }
        uint32_t constant = rexx_compiler_add_constant(compiler,
                                                       &expression->token);
        if (compiler->status == REXX_COMPILER_STATUS_OK) {
            (void)rexx_compiler_emit(compiler, OP_LOAD_CONST, 0u, constant,
                                     0u, &expression->token);
            result = compiler->status == REXX_COMPILER_STATUS_OK;
        }
        break;
    }
    case REXX_AST_EXPR_SYMBOL:
        if (expression->left != NULL || expression->right != NULL ||
            expression->arguments != NULL ||
            !rexx_compiler_token_is_name(&expression->token)) {
            rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                               &expression->token,
                               rexx_compiler_message_malformed_ast);
            break;
        }
        result = rexx_compiler_emit_variable(compiler, &expression->token, 0);
        break;
    case REXX_AST_EXPR_GROUP:
        if (expression->left == NULL || expression->right != NULL ||
            expression->arguments != NULL) {
            rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                               &expression->token,
                               rexx_compiler_message_malformed_ast);
        } else {
            result = rexx_compiler_compile_expression(compiler,
                                                       expression->left);
        }
        break;
    case REXX_AST_EXPR_UNARY:
        if (expression->right == NULL || expression->left != NULL ||
            expression->arguments != NULL ||
            !rexx_compiler_map_operator(expression->operation, &opcode,
                                        &arity) || arity != 1) {
            rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_UNSUPPORTED_AST,
                               &expression->token,
                               rexx_compiler_message_unsupported_expression);
            break;
        }
        if (rexx_compiler_compile_expression(compiler, expression->right)) {
            (void)rexx_compiler_emit(compiler, opcode, 0u, 0u, 0u,
                                     &expression->token);
            result = compiler->status == REXX_COMPILER_STATUS_OK;
        }
        break;
    case REXX_AST_EXPR_BINARY:
        if (expression->left == NULL || expression->right == NULL ||
            expression->arguments != NULL ||
            !rexx_compiler_map_operator(expression->operation, &opcode,
                                        &arity) || arity != 2) {
            rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_UNSUPPORTED_AST,
                               &expression->token,
                               rexx_compiler_message_unsupported_expression);
            break;
        }
        if (rexx_compiler_compile_expression(compiler, expression->left) &&
            rexx_compiler_compile_expression(compiler, expression->right)) {
            (void)rexx_compiler_emit(compiler, opcode, 0u, 0u, 0u,
                                     &expression->token);
            result = compiler->status == REXX_COMPILER_STATUS_OK;
        }
        break;
    case REXX_AST_EXPR_FUNCTION_CALL: {
        uint32_t function_symbol;
        uint32_t argument_count;
        if (expression->left != NULL || expression->right != NULL ||
            !rexx_compiler_token_is_name(&expression->token)) {
            rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                               &expression->token,
                               rexx_compiler_message_bad_token);
            break;
        }
        function_symbol = rexx_compiler_intern_symbol(
            compiler, &expression->token, REXX_SYMBOL_ROUTINE);
        if (compiler->status != REXX_COMPILER_STATUS_OK ||
            !rexx_compiler_compile_arguments(compiler, expression->arguments,
                                             &argument_count)) {
            break;
        }
        (void)rexx_compiler_emit(compiler, OP_CALL, 0u, function_symbol,
                                 argument_count, &expression->token);
        result = compiler->status == REXX_COMPILER_STATUS_OK;
        break;
    }
    default:
        rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_UNSUPPORTED_AST,
                           &expression->token,
                           rexx_compiler_message_unsupported_expression);
        break;
    }

    --compiler->expression_depth;
    return result && compiler->status == REXX_COMPILER_STATUS_OK;
}

static int rexx_compiler_compile_clause_list(RexxCompiler *compiler,
                                             const RexxAstClause *clauses);

static int rexx_compiler_compile_if(RexxCompiler *compiler,
                                    const RexxAstClause *clause)
{
    uint32_t false_jump;
    uint32_t end_jump;

    if (clause->as.if_clause.condition == NULL ||
        clause->as.if_clause.then_clause == NULL) {
        rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                           &clause->token, rexx_compiler_message_malformed_ast);
        return 0;
    }
    if (!rexx_compiler_compile_expression(
            compiler, clause->as.if_clause.condition)) {
        return 0;
    }
    false_jump = rexx_compiler_emit(compiler, OP_JUMP_IF_FALSE, 0u, 0u, 0u,
                                   &clause->token);
    if (compiler->status != REXX_COMPILER_STATUS_OK ||
        !rexx_compiler_compile_clause_list(
            compiler, clause->as.if_clause.then_clause)) {
        return 0;
    }
    if (clause->as.if_clause.else_clause != NULL) {
        end_jump = rexx_compiler_emit(compiler, OP_JUMP, 0u, 0u, 0u,
                                     &clause->token);
        if (compiler->status != REXX_COMPILER_STATUS_OK ||
            !rexx_compiler_patch(compiler, false_jump,
                                 compiler->chunk->instruction_count,
                                 &clause->token) ||
            !rexx_compiler_compile_clause_list(
                compiler, clause->as.if_clause.else_clause) ||
            !rexx_compiler_patch(compiler, end_jump,
                                 compiler->chunk->instruction_count,
                                 &clause->token)) {
            return 0;
        }
    } else if (!rexx_compiler_patch(compiler, false_jump,
                                    compiler->chunk->instruction_count,
                                    &clause->token)) {
        return 0;
    }
    return compiler->status == REXX_COMPILER_STATUS_OK;
}

static int rexx_compiler_compile_do(RexxCompiler *compiler,
                                    const RexxAstClause *clause)
{
    const RexxAstDo *loop = &clause->as.do_clause;
    RexxLoopContext *loop_context;
    uint32_t flags = 0u;
    uint32_t control_symbol = REXX_NO_SYMBOL;
    uint32_t test_jump = 0u;
    uint32_t while_jump = 0u;
    uint32_t until_jump = 0u;
    size_t top_target;
    size_t continue_target;
    size_t end_target;
    size_t index;
    int active = 0;
    int success = 0;

    if (compiler->loop_depth >= REXX_COMPILER_MAX_NESTING) {
        rexx_compiler_fail_limit(compiler, &clause->token);
        return 0;
    }
    if ((loop->has_control_variable != 0 && loop->has_control_variable != 1) ||
        (loop->has_to != 0 && loop->has_to != 1) ||
        (loop->has_by != 0 && loop->has_by != 1) ||
        (loop->has_for != 0 && loop->has_for != 1) ||
        (loop->has_while != 0 && loop->has_while != 1) ||
        (loop->has_until != 0 && loop->has_until != 1) ||
        (loop->mode == REXX_AST_DO_CONTROLLED &&
         (!loop->has_control_variable || loop->start == NULL)) ||
        (loop->mode != REXX_AST_DO_CONTROLLED &&
         loop->has_control_variable) ||
        (loop->mode == REXX_AST_DO_COUNTED && loop->start == NULL) ||
        ((loop->mode == REXX_AST_DO_BLOCK ||
          loop->mode == REXX_AST_DO_FOREVER) &&
         (loop->start != NULL || loop->has_to || loop->has_by || loop->has_for)) ||
        (loop->mode != REXX_AST_DO_BLOCK &&
         loop->mode != REXX_AST_DO_FOREVER &&
         loop->mode != REXX_AST_DO_COUNTED &&
         loop->mode != REXX_AST_DO_CONTROLLED) ||
        (loop->has_to && loop->to == NULL) ||
        (loop->has_by && loop->by == NULL) ||
        (loop->has_for && loop->for_count == NULL) ||
        (loop->has_while && loop->while_condition == NULL) ||
        (loop->has_until && loop->until_condition == NULL)) {
        rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                           &clause->token, rexx_compiler_message_bad_loop);
        return 0;
    }

    if (loop->mode == REXX_AST_DO_CONTROLLED) {
        flags |= REXX_DO_FLAG_CONTROLLED;
        control_symbol = rexx_compiler_intern_symbol(
            compiler, &loop->control_variable,
            rexx_compiler_token_has_dot(&loop->control_variable)
                ? REXX_SYMBOL_STEM : REXX_SYMBOL_VARIABLE);
    } else if (loop->mode == REXX_AST_DO_COUNTED) {
        flags |= REXX_DO_FLAG_COUNTED;
    }
    if (loop->has_to) flags |= REXX_DO_FLAG_HAS_TO;
    if (loop->has_by) flags |= REXX_DO_FLAG_HAS_BY;
    if (loop->has_for) flags |= REXX_DO_FLAG_HAS_FOR;
    if (compiler->status != REXX_COMPILER_STATUS_OK) {
        return 0;
    }

    if (loop->mode == REXX_AST_DO_COUNTED ||
        loop->mode == REXX_AST_DO_CONTROLLED) {
        if (!rexx_compiler_compile_expression(compiler, loop->start)) return 0;
        if (loop->has_to &&
            !rexx_compiler_compile_expression(compiler, loop->to)) return 0;
        if (loop->has_by &&
            !rexx_compiler_compile_expression(compiler, loop->by)) return 0;
        if (loop->has_for &&
            !rexx_compiler_compile_expression(compiler, loop->for_count)) return 0;
    }
    (void)rexx_compiler_emit(compiler, OP_DO_SETUP, 0u, control_symbol,
                             flags, &clause->token);
    if (compiler->status != REXX_COMPILER_STATUS_OK) return 0;

    loop_context = &compiler->loops[compiler->loop_depth++];
    memset(loop_context, 0, sizeof(*loop_context));
    active = 1;
    top_target = compiler->chunk->instruction_count;

    if (loop->mode == REXX_AST_DO_COUNTED ||
        loop->mode == REXX_AST_DO_CONTROLLED) {
        (void)rexx_compiler_emit(compiler, OP_DO_TEST, 0u, 0u, 0u,
                                 &clause->token);
        test_jump = rexx_compiler_emit(compiler, OP_JUMP_IF_FALSE, 0u,
                                       0u, 0u, &clause->token);
        if (compiler->status != REXX_COMPILER_STATUS_OK) goto cleanup;
    }
    if (loop->has_while) {
        if (!rexx_compiler_compile_expression(compiler,
                                              loop->while_condition)) {
            goto cleanup;
        }
        while_jump = rexx_compiler_emit(compiler, OP_JUMP_IF_FALSE, 0u,
                                        0u, 0u, &clause->token);
        if (compiler->status != REXX_COMPILER_STATUS_OK) goto cleanup;
    }
    if (loop->body != NULL &&
        !rexx_compiler_compile_clause_list(compiler, loop->body)) {
        goto cleanup;
    }

    if (loop->has_until) {
        continue_target = compiler->chunk->instruction_count;
        if (!rexx_compiler_compile_expression(compiler,
                                              loop->until_condition)) {
            goto cleanup;
        }
        until_jump = rexx_compiler_emit(compiler, OP_JUMP_IF_TRUE, 0u,
                                        0u, 0u, &clause->token);
        if (compiler->status != REXX_COMPILER_STATUS_OK) goto cleanup;
    } else if (loop->mode == REXX_AST_DO_COUNTED ||
               loop->mode == REXX_AST_DO_CONTROLLED) {
        continue_target = compiler->chunk->instruction_count;
    } else {
        continue_target = top_target;
    }

    if (loop->mode == REXX_AST_DO_COUNTED ||
        loop->mode == REXX_AST_DO_CONTROLLED) {
        (void)rexx_compiler_emit(compiler, OP_DO_NEXT, 0u, 0u, 0u,
                                 &clause->token);
        (void)rexx_compiler_emit(compiler, OP_JUMP, 0u,
                                 (uint32_t)top_target, 0u, &clause->token);
    } else if (loop->mode == REXX_AST_DO_FOREVER ||
               (loop->mode == REXX_AST_DO_BLOCK &&
                (loop->has_while || loop->has_until))) {
        (void)rexx_compiler_emit(compiler, OP_JUMP, 0u,
                                 (uint32_t)top_target, 0u, &clause->token);
    }
    if (compiler->status != REXX_COMPILER_STATUS_OK) goto cleanup;

    end_target = compiler->chunk->instruction_count;
    (void)rexx_compiler_emit(compiler, OP_DO_END, 0u, 0u, 0u,
                             &clause->token);
    if (compiler->status != REXX_COMPILER_STATUS_OK) goto cleanup;

    if ((loop->mode == REXX_AST_DO_COUNTED ||
         loop->mode == REXX_AST_DO_CONTROLLED) &&
        !rexx_compiler_patch(compiler, test_jump, end_target, &clause->token)) {
        goto cleanup;
    }
    if (loop->has_while &&
        !rexx_compiler_patch(compiler, while_jump, end_target, &clause->token)) {
        goto cleanup;
    }
    if (loop->has_until &&
        !rexx_compiler_patch(compiler, until_jump, end_target, &clause->token)) {
        goto cleanup;
    }
    for (index = 0u; index < loop_context->breaks.count; ++index) {
        if (!rexx_compiler_patch(compiler, loop_context->breaks.items[index],
                                 end_target, &clause->token)) goto cleanup;
    }
    for (index = 0u; index < loop_context->continues.count; ++index) {
        if (!rexx_compiler_patch(compiler,
                                 loop_context->continues.items[index],
                                 continue_target, &clause->token)) goto cleanup;
    }
    success = compiler->status == REXX_COMPILER_STATUS_OK;

cleanup:
    if (active) {
        rexx_compiler_index_list_free(&loop_context->breaks);
        rexx_compiler_index_list_free(&loop_context->continues);
        --compiler->loop_depth;
    }
    return success;
}

static int rexx_compiler_compile_select(RexxCompiler *compiler,
                                        const RexxAstClause *clause)
{
    const RexxAstWhen *when_clause;
    RexxIndexList end_jumps;
    memset(&end_jumps, 0, sizeof(end_jumps));

    for (when_clause = clause->as.select_clause.when_clauses;
         when_clause != NULL && compiler->status == REXX_COMPILER_STATUS_OK;
         when_clause = when_clause->next) {
        uint32_t false_jump;
        if (!rexx_compiler_visit(compiler, &clause->token)) goto cleanup;
        if (when_clause->condition == NULL || when_clause->body == NULL) {
            rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                               &clause->token,
                               rexx_compiler_message_malformed_ast);
            goto cleanup;
        }
        if (!rexx_compiler_compile_expression(compiler,
                                              when_clause->condition)) {
            goto cleanup;
        }
        false_jump = rexx_compiler_emit(compiler, OP_JUMP_IF_FALSE, 0u,
                                        0u, 0u, &clause->token);
        if (compiler->status != REXX_COMPILER_STATUS_OK ||
            !rexx_compiler_compile_clause_list(compiler, when_clause->body)) {
            goto cleanup;
        }
        if (!rexx_compiler_index_list_push(
                compiler, &end_jumps,
                rexx_compiler_emit(compiler, OP_JUMP, 0u, 0u, 0u,
                                   &clause->token),
                &clause->token)) {
            goto cleanup;
        }
        if (!rexx_compiler_patch(compiler, false_jump,
                                 compiler->chunk->instruction_count,
                                 &clause->token)) {
            goto cleanup;
        }
    }

    if (compiler->status != REXX_COMPILER_STATUS_OK) goto cleanup;
    if (clause->as.select_clause.when_clauses == NULL) {
        rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                           &clause->token, rexx_compiler_message_malformed_ast);
        goto cleanup;
    }
    if (clause->as.select_clause.otherwise_clause != NULL &&
        !rexx_compiler_compile_clause_list(
            compiler, clause->as.select_clause.otherwise_clause)) {
        goto cleanup;
    }
    for (size_t index = 0u; index < end_jumps.count; ++index) {
        if (!rexx_compiler_patch(compiler, end_jumps.items[index],
                                 compiler->chunk->instruction_count,
                                 &clause->token)) {
            goto cleanup;
        }
    }

cleanup:
    rexx_compiler_index_list_free(&end_jumps);
    return compiler->status == REXX_COMPILER_STATUS_OK;
}

static int rexx_compiler_compile_clause(RexxCompiler *compiler,
                                        const RexxAstClause *clause)
{
    uint32_t symbol;
    uint32_t jump;

    switch (clause->kind) {
    case REXX_AST_CLAUSE_LABEL:
        symbol = rexx_compiler_intern_symbol(compiler, &clause->as.label.label,
                                             REXX_SYMBOL_LABEL);
        if (compiler->status != REXX_COMPILER_STATUS_OK) return 0;
        (void)rexx_compiler_emit(compiler, OP_LABEL, 0u, symbol, 0u,
                                 &clause->token);
        break;
    case REXX_AST_CLAUSE_ASSIGNMENT:
        if (clause->as.assignment.value == NULL ||
            !rexx_compiler_compile_expression(
                compiler, clause->as.assignment.value)) return 0;
        if (!rexx_compiler_emit_variable(compiler,
                                         &clause->as.assignment.variable, 1)) {
            return 0;
        }
        break;
    case REXX_AST_CLAUSE_SAY:
        if (clause->as.simple.expression != NULL &&
            !rexx_compiler_compile_expression(
                compiler, clause->as.simple.expression)) return 0;
        (void)rexx_compiler_emit(compiler, OP_SAY,
                                 clause->as.simple.expression != NULL ? 1u : 0u,
                                 0u, 0u, &clause->token);
        break;
    case REXX_AST_CLAUSE_NUMERIC_DIGITS:
        if (clause->as.simple.expression != NULL &&
            !rexx_compiler_compile_expression(
                compiler, clause->as.simple.expression)) return 0;
        (void)rexx_compiler_emit(
            compiler, OP_NUMERIC_DIGITS,
            clause->as.simple.expression != NULL
                ? REXX_INSTRUCTION_HAS_VALUE : 0u,
            0u, 0u, &clause->token);
        break;
    case REXX_AST_CLAUSE_ADDRESS:
    {
        uint32_t environment_symbol = 0u;
        uint8_t flags = clause->as.simple.expression != NULL
                            ? REXX_INSTRUCTION_HAS_VALUE : 0u;

        if (clause->as.simple.has_address_environment) {
            environment_symbol = rexx_compiler_intern_symbol(
                compiler, &clause->as.simple.address_environment,
                REXX_SYMBOL_VARIABLE);
            if (compiler->status != REXX_COMPILER_STATUS_OK) return 0;
            flags |= REXX_INSTRUCTION_HAS_ENVIRONMENT;
        }
        if (clause->as.simple.expression != NULL &&
            !rexx_compiler_compile_expression(
                compiler, clause->as.simple.expression)) return 0;
        (void)rexx_compiler_emit(
            compiler, OP_ADDRESS, flags, environment_symbol, 0u,
            &clause->token);
        break;
    }
    case REXX_AST_CLAUSE_COMMAND:
        if (clause->as.simple.expression == NULL ||
            !rexx_compiler_compile_expression(
                compiler, clause->as.simple.expression)) return 0;
        (void)rexx_compiler_emit(compiler, OP_COMMAND, 0u, 0u, 0u,
                                 &clause->token);
        break;
    case REXX_AST_CLAUSE_CALL:
        if (clause->as.call.call == NULL ||
            !rexx_compiler_compile_expression(compiler,
                                              clause->as.call.call)) return 0;
        (void)rexx_compiler_emit(compiler, OP_POP, 0u, 0u, 0u,
                                 &clause->token);
        break;
    case REXX_AST_CLAUSE_RETURN:
    case REXX_AST_CLAUSE_EXIT:
        if (clause->as.simple.expression != NULL &&
            !rexx_compiler_compile_expression(
                compiler, clause->as.simple.expression)) return 0;
        (void)rexx_compiler_emit(
            compiler,
            clause->kind == REXX_AST_CLAUSE_RETURN ? OP_RETURN : OP_EXIT,
            clause->as.simple.expression != NULL ? 1u : 0u,
            0u, 0u, &clause->token);
        break;
    case REXX_AST_CLAUSE_NOP:
        (void)rexx_compiler_emit(compiler, OP_NOP, 0u, 0u, 0u,
                                 &clause->token);
        break;
    case REXX_AST_CLAUSE_LEAVE:
        if (compiler->loop_depth == 0u) {
            rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                               &clause->token,
                               rexx_compiler_message_malformed_ast);
            return 0;
        }
        jump = rexx_compiler_emit(compiler, OP_JUMP, 0u, 0u, 0u,
                                  &clause->token);
        return rexx_compiler_index_list_push(
            compiler, &compiler->loops[compiler->loop_depth - 1u].breaks,
            jump, &clause->token);
    case REXX_AST_CLAUSE_ITERATE:
        if (compiler->loop_depth == 0u) {
            rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                               &clause->token,
                               rexx_compiler_message_malformed_ast);
            return 0;
        }
        jump = rexx_compiler_emit(compiler, OP_JUMP, 0u, 0u, 0u,
                                  &clause->token);
        return rexx_compiler_index_list_push(
            compiler, &compiler->loops[compiler->loop_depth - 1u].continues,
            jump, &clause->token);
    case REXX_AST_CLAUSE_IF:
        return rexx_compiler_compile_if(compiler, clause);
    case REXX_AST_CLAUSE_DO:
        return rexx_compiler_compile_do(compiler, clause);
    case REXX_AST_CLAUSE_SELECT:
        return rexx_compiler_compile_select(compiler, clause);
    default:
        rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_UNSUPPORTED_AST,
                           &clause->token,
                           rexx_compiler_message_unsupported_clause);
        return 0;
    }
    return compiler->status == REXX_COMPILER_STATUS_OK;
}

static int rexx_compiler_compile_clause_list(RexxCompiler *compiler,
                                             const RexxAstClause *clauses)
{
    const RexxAstClause *clause;
    if (compiler->status != REXX_COMPILER_STATUS_OK) return 0;
    if (compiler->clause_depth >= REXX_COMPILER_MAX_NESTING) {
        rexx_compiler_fail_limit(compiler, clauses != NULL ? &clauses->token : NULL);
        return 0;
    }
    ++compiler->clause_depth;
    for (clause = clauses; clause != NULL; clause = clause->next) {
        if (!rexx_compiler_token_is_valid(&clause->token)) {
            rexx_compiler_fail(compiler, REXX_COMPILER_STATUS_MALFORMED_AST,
                               &clause->token,
                               rexx_compiler_message_bad_token);
            --compiler->clause_depth;
            return 0;
        }
        if (!rexx_compiler_visit(compiler, &clause->token) ||
            !rexx_compiler_compile_clause(compiler, clause)) {
            --compiler->clause_depth;
            return 0;
        }
    }
    --compiler->clause_depth;
    return compiler->status == REXX_COMPILER_STATUS_OK;
}

void rexx_bytecode_chunk_free(RexxBytecodeChunk *chunk)
{
    size_t index;
    if (chunk == NULL) return;
    for (index = 0u; index < chunk->constant_count; ++index) {
        free(chunk->constants[index].text);
    }
    for (index = 0u; index < chunk->symbol_count; ++index) {
        free(chunk->symbols[index].name);
    }
    free(chunk->constants);
    free(chunk->symbols);
    free(chunk->instructions);
    free(chunk);
}

RexxCompilerStatus rexx_compiler_compile(const RexxAstProgram *program,
                                         RexxBytecodeChunk **chunk,
                                         RexxCompilerError *error)
{
    RexxCompiler compiler;
    RexxBytecodeChunk *result;

    if (chunk == NULL) {
        if (error != NULL) {
            memset(error, 0, sizeof(*error));
            error->status = REXX_COMPILER_STATUS_INVALID_ARGUMENT;
            error->message = rexx_compiler_message_invalid_argument;
        }
        return REXX_COMPILER_STATUS_INVALID_ARGUMENT;
    }
    *chunk = NULL;
    if (error != NULL) memset(error, 0, sizeof(*error));
    if (program == NULL) {
        if (error != NULL) {
            error->status = REXX_COMPILER_STATUS_INVALID_ARGUMENT;
            error->message = rexx_compiler_message_invalid_argument;
        }
        return REXX_COMPILER_STATUS_INVALID_ARGUMENT;
    }

    result = (RexxBytecodeChunk *)calloc(1u, sizeof(*result));
    if (result == NULL) {
        if (error != NULL) {
            error->status = REXX_COMPILER_STATUS_OUT_OF_MEMORY;
            error->message = rexx_compiler_message_out_of_memory;
        }
        return REXX_COMPILER_STATUS_OUT_OF_MEMORY;
    }
    memset(&compiler, 0, sizeof(compiler));
    compiler.chunk = result;
    compiler.error = error;
    compiler.status = REXX_COMPILER_STATUS_OK;

    if (!rexx_compiler_compile_clause_list(&compiler, program->clauses) ||
        compiler.status != REXX_COMPILER_STATUS_OK) {
        size_t loop_index;
        for (loop_index = 0u; loop_index < compiler.loop_depth; ++loop_index) {
            rexx_compiler_index_list_free(&compiler.loops[loop_index].breaks);
            rexx_compiler_index_list_free(&compiler.loops[loop_index].continues);
        }
        rexx_bytecode_chunk_free(result);
        return compiler.status;
    }

    (void)rexx_compiler_emit(&compiler, OP_HALT, 0u, 0u, 0u, NULL);
    if (compiler.status != REXX_COMPILER_STATUS_OK) {
        rexx_bytecode_chunk_free(result);
        return compiler.status;
    }
    *chunk = result;
    return REXX_COMPILER_STATUS_OK;
}

const char *rexx_compiler_status_name(RexxCompilerStatus status)
{
    switch (status) {
    case REXX_COMPILER_STATUS_OK: return "ok";
    case REXX_COMPILER_STATUS_INVALID_ARGUMENT: return "invalid argument";
    case REXX_COMPILER_STATUS_OUT_OF_MEMORY: return "out of memory";
    case REXX_COMPILER_STATUS_RESOURCE_LIMIT: return "resource limit";
    case REXX_COMPILER_STATUS_UNSUPPORTED_AST: return "unsupported AST";
    case REXX_COMPILER_STATUS_MALFORMED_AST: return "malformed AST";
    default: return "unknown compiler status";
    }
}
