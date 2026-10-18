#include "rexx_lexer.h"
#include "rexx_parser.h"
#include "rexx_compiler.h"
#include "rexx_vm.h"

#include <stdio.h>
#include <string.h>

static size_t assertions;
static char captured_output[4096];
static size_t captured_length;

static int check(int condition, const char *description)
{
    ++assertions;
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", description);
    return 1;
}

static RexxBytecodeChunk *compile_source(const char *source)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxCompilerError error;

    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    if (rexx_lexer_init(&lexer, source, strlen(source)) != REXX_LEXER_STATUS_OK)
        return NULL;
    if (rexx_parser_init(&parser, &lexer) != REXX_PARSER_STATUS_OK ||
        rexx_parser_parse(&parser, &program) != REXX_PARSER_STATUS_OK) {
        rexx_parser_free_ast(program);
        return NULL;
    }
    if (rexx_compiler_compile(program, &chunk, &error) !=
        REXX_COMPILER_STATUS_OK) {
        rexx_parser_free_ast(program);
        return NULL;
    }
    rexx_parser_free_ast(program);
    return chunk;
}

static int32_t capture_output(void *user_data, const char *bytes, size_t length)
{
    (void)user_data;
    if (length > sizeof(captured_output) - captured_length) return -1;
    memcpy(captured_output + captured_length, bytes, length);
    captured_length += length;
    return 1;
}

static int run_program(const char *source, const char *expected,
                       const char *description)
{
    RexxVM vm;
    RexxBytecodeChunk *chunk = compile_source(source);
    RexxVMStatus status;
    int failed;
    if (chunk == NULL) return check(0, "RX16 integration source compiles");
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return check(0, "RX16 integration VM initializes");
    }
    captured_length = 0u;
    memset(captured_output, 0, sizeof(captured_output));
    rexx_vm_set_output_writer(&vm, capture_output, NULL);
    status = rexx_vm_execute(&vm, chunk);
    failed = check(status == REXX_VM_STATUS_OK &&
                   captured_length == strlen(expected) &&
                   memcmp(captured_output, expected, captured_length) == 0,
                   description);
    if (failed != 0) {
        fprintf(stderr, "  execution status=%s, output length=%zu (expected %zu)\n",
                rexx_vm_status_name(status), captured_length, strlen(expected));
        fprintf(stderr, "  actual output: [%.*s]\n", (int)captured_length,
                captured_output);
    }
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    return failed;
}

static int expect_tokens(const char *source,
                         const RexxTokenType *types,
                         const RexxLexerError *errors,
                         size_t expected_count,
                         const char *description)
{
    RexxLexer lexer;
    size_t index;
    int failed = 0;

    memset(&lexer, 0, sizeof(lexer));
    if (rexx_lexer_init(&lexer, source, strlen(source)) != REXX_LEXER_STATUS_OK)
        return check(0, "RX16 lexer initializes");
    for (index = 0u; index < expected_count; ++index) {
        RexxToken token = rexx_lexer_next_token(&lexer);
        char message[160];
        (void)snprintf(message, sizeof(message), "%s (token %zu type)",
                       description, index);
        failed += check(token.type == types[index], message);
        if (errors != NULL) {
            (void)snprintf(message, sizeof(message), "%s (token %zu error)",
                           description, index);
            failed += check(token.error == errors[index], message);
        }
    }
    return failed;
}

static int test_lexer_boundaries(void)
{
    static const RexxTokenType boundary_types[] = {
        TOKEN_HEX_STRING, TOKEN_BINARY_STRING,
        TOKEN_STRING, TOKEN_SYMBOL,
        TOKEN_STRING, TOKEN_SYMBOL,
        TOKEN_STRING, TOKEN_SYMBOL,
        TOKEN_STRING, TOKEN_SYMBOL,
        TOKEN_EOF
    };
    static const RexxTokenType comment_types[] = {
        TOKEN_HEX_STRING, TOKEN_HEX_STRING, TOKEN_EOF
    };
    static const RexxTokenType function_types[] = {
        TOKEN_STRING, TOKEN_LEFT_PAREN, TOKEN_STRING, TOKEN_RIGHT_PAREN,
        TOKEN_HEX_STRING, TOKEN_LEFT_PAREN, TOKEN_NUMBER, TOKEN_RIGHT_PAREN,
        TOKEN_EOF
    };
    static const RexxTokenType invalid_hex_types[] = { TOKEN_ERROR, TOKEN_EOF };
    static const RexxTokenType invalid_binary_types[] = { TOKEN_ERROR, TOKEN_EOF };
    static const RexxLexerError invalid_hex_errors[] = {
        REXX_LEXER_ERROR_INVALID_HEX_STRING, REXX_LEXER_ERROR_NONE
    };
    static const RexxLexerError invalid_binary_errors[] = {
        REXX_LEXER_ERROR_INVALID_BINARY_STRING, REXX_LEXER_ERROR_NONE
    };
    static const RexxTokenType unterminated_types[] = {
        TOKEN_KEYWORD_SAY, TOKEN_ERROR, TOKEN_NEWLINE, TOKEN_EOF
    };
    static const RexxLexerError unterminated_errors[] = {
        REXX_LEXER_ERROR_NONE, REXX_LEXER_ERROR_UNTERMINATED_STRING,
        REXX_LEXER_ERROR_NONE, REXX_LEXER_ERROR_NONE
    };
    static const char boundaries[] =
        "'41'X '10'B '43' X '44'XY '45'BETA '46'X1";
    static const char comments[] = "'41'X/*adjacent*/'42'X";
    static const char functions[] = "'C2X'('A') '41'X(2)";
    static const char unterminated[] = "say '41\n";
    int failed = 0;

    failed += expect_tokens(boundaries, boundary_types, NULL,
                            sizeof(boundary_types) / sizeof(boundary_types[0]),
                            "suffix boundary preserves following symbols");
    failed += expect_tokens(comments, comment_types, NULL,
                            sizeof(comment_types) / sizeof(comment_types[0]),
                            "comments adjacent to suffix literals");
    failed += expect_tokens(functions, function_types, NULL,
                            sizeof(function_types) / sizeof(function_types[0]),
                            "quoted function call and hex literal grouping stay distinct");
    failed += expect_tokens("'4G'X", invalid_hex_types, invalid_hex_errors, 2u,
                            "invalid hexadecimal digit is a lexical error");
    failed += expect_tokens("'4 1'X", invalid_hex_types, invalid_hex_errors, 2u,
                            "hex grouping must end at a byte boundary");
    failed += expect_tokens("' 41'X", invalid_hex_types, invalid_hex_errors, 2u,
                            "hex leading blank is rejected");
    failed += expect_tokens("'41 'X", invalid_hex_types, invalid_hex_errors, 2u,
                            "hex trailing blank is rejected");
    failed += expect_tokens("'1 2'X", invalid_hex_types, invalid_hex_errors, 2u,
                            "odd first hex group does not relax later boundaries");
    failed += expect_tokens("'102'B", invalid_binary_types, invalid_binary_errors, 2u,
                            "invalid binary digit is a lexical error");
    failed += expect_tokens("'10 101'B", invalid_binary_types,
                            invalid_binary_errors, 2u,
                            "binary grouping must end at a nibble boundary");
    failed += expect_tokens("' 1010'B", invalid_binary_types,
                            invalid_binary_errors, 2u,
                            "binary leading blank is rejected");
    failed += expect_tokens("'1010 'B", invalid_binary_types,
                            invalid_binary_errors, 2u,
                            "binary trailing blank is rejected");
    failed += expect_tokens("'4''1'X", invalid_hex_types,
                            invalid_hex_errors, 2u,
                            "doubled quotes are parsed and rejected as invalid hex digits");
    failed += expect_tokens(unterminated, unterminated_types,
                            unterminated_errors, 4u,
                            "unterminated string leaves the line ending for recovery");
    {
        RexxLexer lexer;
        RexxToken token;
        static const char spaced_suffix[] = "'41' X";
        int ok = rexx_lexer_init(&lexer, spaced_suffix,
                                 sizeof(spaced_suffix) - 1u) ==
                 REXX_LEXER_STATUS_OK;
        failed += check(ok, "whitespace-before-suffix lexer initializes");
        if (ok) {
            token = rexx_lexer_next_token(&lexer);
            failed += check(token.type == TOKEN_STRING,
                            "whitespace before X keeps a normal string");
            token = rexx_lexer_next_token(&lexer);
            failed += check(token.type == TOKEN_SYMBOL &&
                            rexx_token_equals_ascii_case_insensitive(&token, "X"),
                            "whitespace-separated X remains a symbol");
        }
    }
    return failed;
}

static int test_size_limit(void)
{
    char maximum_hex[504];
    char oversized_hex[506];
    char maximum_binary[2004];
    char oversized_binary[2005];
    RexxLexer lexer;
    RexxToken token;
    int failed = 0;
    size_t index;

    maximum_hex[0] = '\'';
    for (index = 1u; index <= 500u; ++index) maximum_hex[index] = 'A';
    maximum_hex[501] = '\'';
    maximum_hex[502] = 'X';
    maximum_hex[503] = '\0';
    failed += check(rexx_lexer_init(&lexer, maximum_hex, 503u) ==
                    REXX_LEXER_STATUS_OK,
                    "maximum-size hex lexer initializes");
    token = rexx_lexer_next_token(&lexer);
    failed += check(token.type == TOKEN_HEX_STRING,
                    "250-byte hexadecimal literal is accepted");

    oversized_hex[0] = '\'';
    for (index = 1u; index <= 502u; ++index) oversized_hex[index] = 'A';
    oversized_hex[503] = '\'';
    oversized_hex[504] = 'X';
    oversized_hex[505] = '\0';
    failed += check(rexx_lexer_init(&lexer, oversized_hex, 505u) ==
                    REXX_LEXER_STATUS_OK,
                    "oversized hex lexer initializes");
    token = rexx_lexer_next_token(&lexer);
    failed += check(token.type == TOKEN_ERROR &&
                    token.error == REXX_LEXER_ERROR_ENCODED_STRING_TOO_LONG,
                    "hex literal above packed-size limit is rejected");

    maximum_binary[0] = '\'';
    for (index = 1u; index <= 2000u; ++index) maximum_binary[index] = '0';
    maximum_binary[2001] = '\'';
    maximum_binary[2002] = 'B';
    maximum_binary[2003] = '\0';
    failed += check(rexx_lexer_init(&lexer, maximum_binary, 2003u) ==
                    REXX_LEXER_STATUS_OK,
                    "maximum-size binary lexer initializes");
    token = rexx_lexer_next_token(&lexer);
    failed += check(token.type == TOKEN_BINARY_STRING,
                    "250-byte binary literal is accepted");

    oversized_binary[0] = '\'';
    for (index = 1u; index <= 2001u; ++index) oversized_binary[index] = '0';
    oversized_binary[2002] = '\'';
    oversized_binary[2003] = 'B';
    oversized_binary[2004] = '\0';
    failed += check(rexx_lexer_init(&lexer, oversized_binary, 2004u) ==
                    REXX_LEXER_STATUS_OK,
                    "oversized binary lexer initializes");
    token = rexx_lexer_next_token(&lexer);
    failed += check(token.type == TOKEN_ERROR &&
                    token.error == REXX_LEXER_ERROR_ENCODED_STRING_TOO_LONG,
                    "binary literal above packed-size limit is rejected");
    return failed;
}

static int test_parser_error_location_and_reuse(void)
{
    static const char malformed[] = "say 'GG'X\n";
    static const char valid[] = "say c2x('00FF80'X)\n";
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxParserStatus status;
    RexxBytecodeChunk *chunk;
    const RexxParserError *error;
    int failed = 0;

    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    failed += check(rexx_lexer_init(&lexer, malformed,
                                    sizeof(malformed) - 1u) ==
                    REXX_LEXER_STATUS_OK,
                    "malformed RX16 parser source initializes");
    if (lexer.initialized) {
        failed += check(rexx_parser_init(&parser, &lexer) ==
                        REXX_PARSER_STATUS_OK,
                        "malformed RX16 parser initializes");
        status = rexx_parser_parse(&parser, &program);
        error = rexx_parser_get_error(&parser);
        failed += check(status == REXX_PARSER_STATUS_LEXER_ERROR &&
                        error != NULL && error->token.line == 1u &&
                        error->token.column == 5u,
                        "invalid literal reports the exact source location");
        rexx_parser_free_ast(program);
    }

    chunk = compile_source(valid);
    failed += check(chunk != NULL,
                    "a fresh parser recovers after a previous syntax failure");
    rexx_bytecode_chunk_free(chunk);
    return failed;
}

#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL) && \
    defined(REXX_PARSER_TEST_ALLOCATOR) && defined(REXX_COMPILER_TEST_ALLOCATOR)
static int test_allocation_failure_recovery(void)
{
    static const char source[] = "say c2x('00FF80'X)\n";
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxCompilerError compiler_error;
    RexxVM vm;
    RexxVMStatus vm_status;
    RexxParserStatus parser_status;
    RexxCompilerStatus compiler_status;
    size_t parser_blocks = rexx_parser_test_allocator_live_blocks();
    size_t compiler_blocks = rexx_compiler_test_allocator_live_blocks();
    int failed = 0;

    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    failed += check(rexx_lexer_init(&lexer, source, sizeof(source) - 1u) ==
                    REXX_LEXER_STATUS_OK,
                    "allocation-failure source initializes");
    if (lexer.initialized) {
        failed += check(rexx_parser_init(&parser, &lexer) ==
                        REXX_PARSER_STATUS_OK,
                        "allocation-failure parser initializes");
        rexx_parser_test_allocator_fail_on(1u);
        parser_status = rexx_parser_parse(&parser, &program);
        rexx_parser_test_allocator_disable();
        failed += check(parser_status == REXX_PARSER_STATUS_OUT_OF_MEMORY &&
                        program == NULL,
                        "literal parse reports injected allocation failure");
        failed += check(rexx_parser_test_allocator_live_blocks() == parser_blocks,
                        "failed literal parse releases partial parser allocations");
    }

    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    (void)rexx_lexer_init(&lexer, source, sizeof(source) - 1u);
    (void)rexx_parser_init(&parser, &lexer);
    parser_status = rexx_parser_parse(&parser, &program);
    failed += check(parser_status == REXX_PARSER_STATUS_OK && program != NULL,
                    "literal AST builds before compiler failure injection");
    if (program != NULL) {
        rexx_compiler_test_allocator_fail_on(1u);
        compiler_status = rexx_compiler_compile(program, &chunk,
                                                &compiler_error);
        rexx_compiler_test_allocator_disable();
        failed += check(compiler_status == REXX_COMPILER_STATUS_OUT_OF_MEMORY &&
                        chunk == NULL,
                        "literal compilation reports injected allocation failure");
        failed += check(rexx_compiler_test_allocator_live_blocks() == compiler_blocks,
                        "failed literal compilation releases partial compiler allocations");
        rexx_parser_free_ast(program);
    }

    chunk = compile_source(source);
    failed += check(chunk != NULL, "literal recompiles after allocation failures");
    if (chunk != NULL && rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK) {
        rexx_vm_set_output_writer(&vm, capture_output, NULL);
        captured_length = 0u;
        rexx_vm_test_allocator_fail_on(1u);
        vm_status = rexx_vm_execute(&vm, chunk);
        rexx_vm_test_allocator_disable();
        failed += check(vm_status == REXX_VM_STATUS_OUT_OF_MEMORY,
                        "literal decode reports injected allocation failure");
        captured_length = 0u;
        vm_status = rexx_vm_execute(&vm, chunk);
        failed += check(vm_status == REXX_VM_STATUS_OK &&
                        captured_length == 7u &&
                        memcmp(captured_output, "00FF80\n", 7u) == 0,
                        "VM executes the same literal after allocation recovery");
        rexx_vm_free(&vm);
    }
    rexx_bytecode_chunk_free(chunk);
    return failed;
}
#endif

int main(void)
{
    static const char hex_source[] =
        "say c2x('414243'X) c2x('414243'x) c2x(\"41 42 43\"X) c2x('F'X) c2x('00FF80'X)\n";
    static const char binary_source[] =
        "say c2x('01000001'B) c2x('1'B) c2x('101 1101'B) c2x('11110000'b)\n";
    static const char empty_source[] = "say length(''X) length(''B)\n";
    static const char roundtrip_source[] =
        "say x2b(c2x('0001FF80'X))\n";
    static const char concat_source[] =
        "say c2x('41'X || '42'X) c2x('41'X '42'X)\n";
    static const char expression_source[] =
        "value = '00FF80'X\n"
        "if value = '00FF80'X then say 'MATCH'\n"
        "if value \\= '00FF00'X then say 'UNEQUAL'\n"
        "say c2x(value)\n"
        "say 'C2X'('A')\n"
        "say c2x('41'X(2))\n"
        "code = \"say c2x('41'X)\"\n"
        "interpret code\n";
    static const char quote_source[] = "say 'It''s' \"A\"\"B\"\n";
    static const char recovery_source[] = "say c2x('00FF80'X)\n";
    int failed = 0;

    failed += test_lexer_boundaries();
    failed += test_size_limit();
    failed += test_parser_error_location_and_reuse();
    failed += run_program(hex_source,
                          "414243 414243 414243 0F 00FF80\n",
                          "hex suffix literals preserve bytes and classic odd-nibble padding");
    failed += run_program(binary_source, "41 01 5D F0\n",
                          "binary suffix literals use nibble grouping and leading zero padding");
    failed += run_program(empty_source, "0 0\n",
                          "empty hex and binary literals produce empty strings");
    failed += run_program(roundtrip_source,
                          "00000000000000011111111110000000\n",
                          "NUL and high-bit bytes survive conversion round trips");
    failed += run_program(concat_source, "4142 412042\n",
                          "explicit and implicit concatenation remain unchanged");
    failed += run_program(expression_source,
                          "MATCH\nUNEQUAL\n00FF80\n41\n4132\n41\n",
                          "literals work in assignments, comparisons, calls, grouping and INTERPRET");
    failed += run_program(quote_source, "It's A\"B\n",
                          "ordinary strings retain doubled-quote behavior");
    failed += run_program(recovery_source, "00FF80\n",
                          "a valid literal executes after prior parser errors");
#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL) && \
    defined(REXX_PARSER_TEST_ALLOCATOR) && defined(REXX_COMPILER_TEST_ALLOCATOR)
    failed += test_allocation_failure_recovery();
#endif
    printf("RX16 literal assertions: %zu; failures: %d\n", assertions, failed);
    return failed == 0 ? 0 : 1;
}
