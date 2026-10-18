#include "../rexx_builtin.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_compiler.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <string.h>

typedef struct Rx13Case {
    const char *name;
    const char *arguments[5]; /* NULL represents an omitted positional value. */
    size_t argument_count;
    const char *expected;
} Rx13Case;

typedef struct Rx13ErrorCase {
    const char *name;
    const char *arguments[6];
    size_t argument_count;
    RexxVMStatus expected_status;
} Rx13ErrorCase;

static size_t assertions;
static char output[8192];
static size_t output_length;

static int check(int condition, const char *description)
{
    ++assertions;
    if (!condition) fprintf(stderr, "FAIL: %s\n", description);
    return condition ? 0 : 1;
}

static void clear_values(RexxValue *values, size_t count)
{
    size_t index;
    for (index = 0u; index < count; ++index) rexx_value_clear(&values[index]);
}

static int run_case(const Rx13Case *test)
{
    RexxValue arguments[5] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT,
        REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxValue result = REXX_VALUE_INIT;
    size_t index;
    size_t expected_length = strlen(test->expected);
    RexxVMStatus status = REXX_VM_STATUS_OK;
    int failed = 0;
    char description[160];

    for (index = 0u; index < test->argument_count; ++index) {
        if (test->arguments[index] != NULL) {
            status = rexx_value_set_string(&arguments[index],
                                           test->arguments[index],
                                           strlen(test->arguments[index]));
            if (status != REXX_VM_STATUS_OK) {
                failed += check(0, "RX13 test argument allocation succeeds");
                clear_values(arguments, 5u);
                return failed;
            }
        }
    }
    status = rexx_builtin_dispatch(test->name, strlen(test->name),
                                   test->argument_count == 0u ? NULL : arguments,
                                   test->argument_count, &result);
    (void)snprintf(description, sizeof(description), "%s returns successfully",
                   test->name);
    failed += check(status == REXX_VM_STATUS_OK, description);
    (void)snprintf(description, sizeof(description), "%s returns a string of exact length",
                   test->name);
    failed += check(status == REXX_VM_STATUS_OK &&
                    result.type == REXX_VALUE_STRING &&
                    result.as.string.length == expected_length,
                    description);
    (void)snprintf(description, sizeof(description), "%s preserves exact output bytes",
                   test->name);
    failed += check(status == REXX_VM_STATUS_OK &&
                    result.type == REXX_VALUE_STRING &&
                    result.as.string.length == expected_length &&
                    memcmp(result.as.string.bytes, test->expected,
                           expected_length) == 0,
                    description);
    if (status != REXX_VM_STATUS_OK || result.type != REXX_VALUE_STRING ||
        result.as.string.length != expected_length ||
        memcmp(result.as.string.bytes, test->expected, expected_length) != 0) {
        fprintf(stderr, "  %s expected length=%zu [%s], status=%s\n",
                test->name, expected_length, test->expected,
                rexx_vm_status_name(status));
    }
    clear_values(arguments, 5u);
    rexx_value_clear(&result);
    return failed;
}

static int run_error_case(const Rx13ErrorCase *test)
{
    RexxValue arguments[6] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT,
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxValue result = REXX_VALUE_INIT;
    size_t index;
    RexxVMStatus status = REXX_VM_STATUS_OK;
    int failed;
    char description[160];

    for (index = 0u; index < test->argument_count; ++index) {
        if (test->arguments[index] != NULL) {
            status = rexx_value_set_string(&arguments[index],
                                           test->arguments[index],
                                           strlen(test->arguments[index]));
            if (status != REXX_VM_STATUS_OK) {
                failed = check(0, "RX13 error argument allocation succeeds");
                clear_values(arguments, 6u);
                return failed;
            }
        }
    }
    status = rexx_builtin_dispatch(test->name, strlen(test->name),
                                   test->argument_count == 0u ? NULL : arguments,
                                   test->argument_count, &result);
    (void)snprintf(description, sizeof(description),
                   "%s rejects malformed or oversized input safely", test->name);
    failed = check(status == test->expected_status &&
                   result.type == REXX_VALUE_UNSET, description);
    clear_values(arguments, 6u);
    rexx_value_clear(&result);
    return failed;
}

static int test_maximum_output_length(void)
{
    RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    size_t index;
    int all_padding = 1;
    int failed;

    status = rexx_value_set_string(&arguments[0], "", 0u);
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_value_set_string(&arguments[1], "16777216", 8u);
    }
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_builtin_dispatch("LEFT", 4u, arguments, 2u, &result);
    }
    if (status == REXX_VM_STATUS_OK && result.type == REXX_VALUE_STRING &&
        result.as.string.length == 16777216u) {
        for (index = 0u; index < result.as.string.length; ++index) {
            if (result.as.string.bytes[index] != ' ') {
                all_padding = 0;
                break;
            }
        }
    } else {
        all_padding = 0;
    }
    failed = check(all_padding,
                   "RX13 accepts the exact 16 MiB result boundary");
    clear_values(arguments, 2u);
    rexx_value_clear(&result);
    return failed;
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
    if (length > sizeof(output) - output_length) return -1;
    memcpy(output + output_length, bytes, length);
    output_length += length;
    return 1;
}

static int run_program(const char *source, const char *expected_output,
                       const char *description)
{
    RexxVM vm;
    RexxBytecodeChunk *chunk = compile_source(source);
    RexxVMStatus status;
    int failed;
    if (chunk == NULL) return check(0, "RX13 integration source compiles");
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return check(0, "RX13 integration VM initializes");
    }
    output_length = 0u;
    memset(output, 0, sizeof(output));
    rexx_vm_set_output_writer(&vm, capture_output, NULL);
    status = rexx_vm_execute(&vm, chunk);
    failed = check(status == REXX_VM_STATUS_OK &&
                   output_length == strlen(expected_output) &&
                   memcmp(output, expected_output, output_length) == 0,
                   description);
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    return failed;
}

static int run_program_status(const char *source, RexxVMStatus expected_status,
                              const char *description)
{
    RexxVM vm;
    RexxBytecodeChunk *chunk = compile_source(source);
    RexxVMStatus status;
    int failed;
    if (chunk == NULL) return check(0, "RX13 error source compiles");
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return check(0, "RX13 error VM initializes");
    }
    status = rexx_vm_execute(&vm, chunk);
    failed = check(status == expected_status, description);
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    return failed;
}

#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
static int test_allocation_failure(void)
{
    static const Rx13Case allocation_cases[] = {
        { "LEFT", { "x", "3", "." }, 3u, "x.." },
        { "RIGHT", { "x", "3", "." }, 3u, "..x" },
        { "INSERT", { "x", "ab", "1" }, 3u, "axb" },
        { "OVERLAY", { "x", "ab", "1" }, 3u, "xb" },
        { "DELSTR", { "abc", "2", "1" }, 3u, "ac" },
        { "DELWORD", { "a b", "1", "1" }, 3u, "b" },
        { "SUBWORD", { "a b", "2" }, 2u, "b" },
        { "WORDINDEX", { "a b", "2" }, 2u, "3" },
        { "WORDLENGTH", { "a b", "2" }, 2u, "1" },
        { "LASTPOS", { "a", "aba" }, 2u, "3" }
    };
    const size_t initial_live = rexx_vm_test_allocator_live_blocks();
    size_t case_index;
    int failed = 0;

    for (case_index = 0u;
         case_index < sizeof(allocation_cases) / sizeof(allocation_cases[0]);
         ++case_index) {
        const Rx13Case *test = &allocation_cases[case_index];
        RexxValue arguments[5] = {
            REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT,
            REXX_VALUE_INIT, REXX_VALUE_INIT
        };
        RexxValue result = REXX_VALUE_INIT;
        size_t index;
        RexxVMStatus status = REXX_VM_STATUS_OK;
        for (index = 0u; index < test->argument_count; ++index) {
            if (test->arguments[index] != NULL) {
                status = rexx_value_set_string(&arguments[index],
                                               test->arguments[index],
                                               strlen(test->arguments[index]));
            }
        }
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX13 allocation test arguments are created");
        rexx_vm_test_allocator_fail_on(1u);
        status = rexx_builtin_dispatch(test->name, strlen(test->name),
                                       arguments, test->argument_count, &result);
        rexx_vm_test_allocator_disable();
        failed += check(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
                        result.type == REXX_VALUE_UNSET &&
                        arguments[0].type == REXX_VALUE_STRING,
                        "RX13 output allocation failure leaves inputs intact");
        status = rexx_builtin_dispatch(test->name, strlen(test->name),
                                       arguments, test->argument_count, &result);
        failed += check(status == REXX_VM_STATUS_OK &&
                        result.type == REXX_VALUE_STRING &&
                        result.as.string.length == strlen(test->expected) &&
                        memcmp(result.as.string.bytes, test->expected,
                               strlen(test->expected)) == 0,
                        "RX13 BIF succeeds after allocation failure is disabled");
        clear_values(arguments, 5u);
        rexx_value_clear(&result);
    }
    failed += check(rexx_vm_test_allocator_live_blocks() == initial_live,
                    "RX13 allocation failures release all owned buffers");
    return failed;
}
#endif

#define RX13_CASE(name, a0, a1, a2, a3, a4, count, value) \
    { name, { a0, a1, a2, a3, a4 }, count, value }

int main(void)
{
    static const Rx13Case cases[] = {
        RX13_CASE("LEFT", "AB", "4", NULL, NULL, NULL, 2u, "AB  "),
        RX13_CASE("left", "AB", "4", ".", NULL, NULL, 3u, "AB.."),
        RX13_CASE("LEFT", "ABCDE", "3", NULL, NULL, NULL, 2u, "ABC"),
        RX13_CASE("LEFT", "", "3", NULL, NULL, NULL, 2u, "   "),
        RX13_CASE("LEFT", "ABC", "0", NULL, NULL, NULL, 2u, ""),
        RX13_CASE("LEFT", "", "0", ".", NULL, NULL, 3u, ""),
        RX13_CASE("LEFT", "ABC", "3", ".", NULL, NULL, 3u, "ABC"),
        RX13_CASE("LEFT", "AB", "4", NULL, NULL, NULL, 3u, "AB  "),
        RX13_CASE("LEFT", "A  ", "3", NULL, NULL, NULL, 2u, "A  "),
        RX13_CASE("LEFT", "AB", "2.0", NULL, NULL, NULL, 2u, "AB"),
        RX13_CASE("RIGHT", "AB", "4", NULL, NULL, NULL, 2u, "  AB"),
        RX13_CASE("right", "AB", "4", ".", NULL, NULL, 3u, "..AB"),
        RX13_CASE("RIGHT", "ABCDE", "3", NULL, NULL, NULL, 2u, "CDE"),
        RX13_CASE("RIGHT", "", "3", NULL, NULL, NULL, 2u, "   "),
        RX13_CASE("RIGHT", "ABC", "0", NULL, NULL, NULL, 2u, ""),
        RX13_CASE("RIGHT", "", "0", ".", NULL, NULL, 3u, ""),
        RX13_CASE("RIGHT", "ABC", "3", ".", NULL, NULL, 3u, "ABC"),
        RX13_CASE("RIGHT", "AB", "4", NULL, NULL, NULL, 3u, "  AB"),
        RX13_CASE("RIGHT", "  A", "3", NULL, NULL, NULL, 2u, "  A"),
        RX13_CASE("RIGHT", "AB", "2.0", NULL, NULL, NULL, 2u, "AB"),

        RX13_CASE("INSERT", "XX", "abcd", "2", NULL, NULL, 3u, "abXXcd"),
        RX13_CASE("INSERT", "123", "abc", NULL, NULL, NULL, 2u, "123abc"),
        RX13_CASE("INSERT", "123", "abc", "5", "6", "+", 5u, "abc++123+++"),
        RX13_CASE("INSERT", "123", "abc", NULL, "5", "-", 5u, "123--abc"),
        RX13_CASE("INSERT", "X", "abcd", "0", NULL, NULL, 3u, "Xabcd"),
        RX13_CASE("INSERT", "X", "abcd", "4", NULL, NULL, 3u, "abcdX"),
        RX13_CASE("INSERT", "XY", "abcd", "1", "1", NULL, 4u, "aXbcd"),
        RX13_CASE("INSERT", "X", "abcd", "1", "3", ".", 5u, "aX..bcd"),
        RX13_CASE("INSERT", "X", "abcd", "1", "0", NULL, 4u, "abcd"),
        RX13_CASE("INSERT", "", "abc", "2", NULL, NULL, 3u, "abc"),
        RX13_CASE("INSERT", "X", "abc", "5", "1", ".", 5u, "abc..X"),
        RX13_CASE("INSERT", "X", "", "2", "2", "+", 5u, "++X+"),
        RX13_CASE("INSERT", "XY", "abc", "3", "1", NULL, 5u, "abcX"),
        RX13_CASE("INSERT", "X", "abc", NULL, "3", NULL, 4u, "X  abc"),
        RX13_CASE("INSERT", "XY", "abc", "2.0", "1.0", NULL, 4u, "abXc"),

        RX13_CASE("OVERLAY", "XX", "abcd", "2", NULL, NULL, 3u, "aXXd"),
        RX13_CASE("OVERLAY", "qq", "abcd", NULL, NULL, NULL, 2u, "qqcd"),
        RX13_CASE("OVERLAY", "123", "abc", "5", "6", "+", 5u, "abc+123+++"),
        RX13_CASE("OVERLAY", "X", "abc", "4", NULL, NULL, 3u, "abcX"),
        RX13_CASE("OVERLAY", "Z", "abcdef", "5", "1", NULL, 4u, "abcdZf"),
        RX13_CASE("OVERLAY", "X", "abcdef", "2", "4", NULL, 4u, "aX   f"),
        RX13_CASE("OVERLAY", "X", "abcd", "2", "0", NULL, 4u, "abcd"),
        RX13_CASE("OVERLAY", "", "abcd", "1", "0", NULL, 4u, "abcd"),
        RX13_CASE("OVERLAY", "", "abc", "5", "0", "+", 5u, "abc+"),
        RX13_CASE("OVERLAY", "XY", "abcd", "2", "1", NULL, 4u, "aXcd"),
        RX13_CASE("OVERLAY", "XY", "abcd", "2", "3", ".", 5u, "aXY."),
        RX13_CASE("OVERLAY", "", "", NULL, NULL, NULL, 2u, ""),
        RX13_CASE("OVERLAY", "X", "abc", NULL, "2.0", NULL, 4u, "X c"),
        RX13_CASE("OVERLAY", "X", "abc", "1.0", NULL, NULL, 3u, "Xbc"),

        RX13_CASE("DELSTR", "abcdef", "3", "2", NULL, NULL, 3u, "abef"),
        RX13_CASE("DELSTR", "abcdef", "3", NULL, NULL, NULL, 2u, "ab"),
        RX13_CASE("DELSTR", "abcdef", "3", "0", NULL, NULL, 3u, "abcdef"),
        RX13_CASE("DELSTR", "abcdef", "7", "2", NULL, NULL, 3u, "abcdef"),
        RX13_CASE("DELSTR", "abcdef", "6", "1", NULL, NULL, 3u, "abcde"),
        RX13_CASE("DELSTR", "", "1", NULL, NULL, NULL, 2u, ""),
        RX13_CASE("DELSTR", "abcdef", "3", "999999999", NULL, NULL, 3u, "ab"),
        RX13_CASE("DELSTR", "abc", "1", "4294967295", NULL, NULL, 3u, ""),
        RX13_CASE("DELSTR", "abcdef", "1", "99", NULL, NULL, 3u, ""),
        RX13_CASE("DELSTR", "x", "1", "1", NULL, NULL, 3u, ""),
        RX13_CASE("DELSTR", "abc", "2", NULL, NULL, NULL, 3u, "a"),
        RX13_CASE("DELSTR", "abc", "2.0", "1.0", NULL, NULL, 3u, "ac"),
        RX13_CASE("DELSTR", "A\tB", "2", "1", NULL, NULL, 3u, "AB"),

        RX13_CASE("DELWORD", "Now is the  time", "2", "2", NULL, NULL, 3u, "Now time"),
        RX13_CASE("DELWORD", "Now is   the time", "3", "1", NULL, NULL, 3u, "Now is   time"),
        RX13_CASE("DELWORD", "Now is the time ", "3", NULL, NULL, NULL, 2u, "Now is "),
        RX13_CASE("DELWORD", "one two", "3", NULL, NULL, NULL, 2u, "one two"),
        RX13_CASE("DELWORD", "one two", "1", "0", NULL, NULL, 3u, "one two"),
        RX13_CASE("DELWORD", "  a   b", "1", "1", NULL, NULL, 3u, "  b"),
        RX13_CASE("DELWORD", "a   b", "2", NULL, NULL, NULL, 2u, "a   "),
        RX13_CASE("DELWORD", "a\t\tb  c", "2", "1", NULL, NULL, 3u, "a\t\tc"),
        RX13_CASE("DELWORD", "   \t  ", "1", "2", NULL, NULL, 3u, "   \t  "),
        RX13_CASE("DELWORD", "", "1", NULL, NULL, NULL, 2u, ""),
        RX13_CASE("DELWORD", "a  b   c", "2", "1", NULL, NULL, 3u, "a  c"),
        RX13_CASE("DELWORD", "a b c", "2", "99", NULL, NULL, 3u, "a "),
        RX13_CASE("DELWORD", "a b", "1", "2.0", NULL, NULL, 3u, ""),

        RX13_CASE("SUBWORD", "Now is the  time", "2", "2", NULL, NULL, 3u, "is the"),
        RX13_CASE("SUBWORD", "Now is the  time", "3", NULL, NULL, NULL, 2u, "the  time"),
        RX13_CASE("SUBWORD", "  one   two three  ", "2", "2", NULL, NULL, 3u, "two three"),
        RX13_CASE("SUBWORD", " one\t two  three ", "2", "2", NULL, NULL, 3u, "two  three"),
        RX13_CASE("SUBWORD", "one two", "3", NULL, NULL, NULL, 2u, ""),
        RX13_CASE("SUBWORD", "one two", "1", "0", NULL, NULL, 3u, ""),
        RX13_CASE("SUBWORD", "one two", "1", NULL, NULL, NULL, 2u, "one two"),
        RX13_CASE("SUBWORD", "", "1", NULL, NULL, NULL, 2u, ""),
        RX13_CASE("SUBWORD", "   \t  ", "1", "1", NULL, NULL, 3u, ""),
        RX13_CASE("SUBWORD", "  alpha   beta ", "1", "1", NULL, NULL, 3u, "alpha"),
        RX13_CASE("SUBWORD", "a  b   c", "2", "2", NULL, NULL, 3u, "b   c"),
        RX13_CASE("SUBWORD", "a\tb\nc", "2", "2", NULL, NULL, 3u, "b\nc"),

        RX13_CASE("WORDINDEX", "Now is the time", "3", NULL, NULL, NULL, 2u, "8"),
        RX13_CASE("WORDINDEX", "Now is the time", "6", NULL, NULL, NULL, 2u, "0"),
        RX13_CASE("WORDINDEX", "  Now   is", "1", NULL, NULL, NULL, 2u, "3"),
        RX13_CASE("WORDINDEX", "\tone\ttwo", "2", NULL, NULL, NULL, 2u, "6"),
        RX13_CASE("WORDINDEX", "", "1", NULL, NULL, NULL, 2u, "0"),
        RX13_CASE("WORDINDEX", "one two", "1", NULL, NULL, NULL, 2u, "1"),
        RX13_CASE("WORDINDEX", "one two", "2.0", NULL, NULL, NULL, 2u, "5"),
        RX13_CASE("WORDINDEX", "one two", "999999", NULL, NULL, NULL, 2u, "0"),

        RX13_CASE("WORDLENGTH", "Now is the time", "2", NULL, NULL, NULL, 2u, "2"),
        RX13_CASE("WORDLENGTH", "Now comes the time", "2", NULL, NULL, NULL, 2u, "5"),
        RX13_CASE("WORDLENGTH", "Now is the time", "6", NULL, NULL, NULL, 2u, "0"),
        RX13_CASE("WORDLENGTH", "  longer   x", "1", NULL, NULL, NULL, 2u, "6"),
        RX13_CASE("WORDLENGTH", "\tone\ttwo", "2", NULL, NULL, NULL, 2u, "3"),
        RX13_CASE("WORDLENGTH", "", "1", NULL, NULL, NULL, 2u, "0"),
        RX13_CASE("WORDLENGTH", "x", "1", NULL, NULL, NULL, 2u, "1"),
        RX13_CASE("WORDLENGTH", "one two", "2.0", NULL, NULL, NULL, 2u, "3"),

        RX13_CASE("LASTPOS", "ab", "abxxab", NULL, NULL, NULL, 2u, "5"),
        RX13_CASE("LASTPOS", "", "abc", NULL, NULL, NULL, 2u, "0"),
        RX13_CASE("LASTPOS", "x", "abc", NULL, NULL, NULL, 2u, "0"),
        RX13_CASE("LASTPOS", "ab", "abxxab", "99", NULL, NULL, 3u, "5"),
        RX13_CASE("LASTPOS", "ab", "abxxab", "5", NULL, NULL, 3u, "1"),
        RX13_CASE("LASTPOS", "ab", "abxxab", "2", NULL, NULL, 3u, "1"),
        RX13_CASE("LASTPOS", "ab", "abxxab", "1", NULL, NULL, 3u, "0"),
        RX13_CASE("LASTPOS", "aa", "aaaa", NULL, NULL, NULL, 2u, "3"),
        RX13_CASE("LASTPOS", "aa", "aaaa", "3", NULL, NULL, 3u, "2"),
        RX13_CASE("LASTPOS", "aa", "aaaa", "2", NULL, NULL, 3u, "1"),
        RX13_CASE("LASTPOS", "A", "aA", NULL, NULL, NULL, 2u, "2"),
        RX13_CASE("LASTPOS", "a", "", NULL, NULL, NULL, 2u, "0"),
        RX13_CASE("LASTPOS", "ab", "ab", "2.0", NULL, NULL, 3u, "1")
    };
    static const Rx13ErrorCase errors[] = {
        { "LEFT", { "x" }, 1u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "LEFT", { "x", "-1" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "LEFT", { "x", "1.5" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "LEFT", { "x", "16777217" }, 2u, REXX_VM_STATUS_RESOURCE_LIMIT },
        { "LEFT", { "x", "1", ".." }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "RIGHT", { "x", "2", "" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "RIGHT", { "x", "-2" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "RIGHT", { "x", "2", "ab" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "INSERT", { "x", "abc", "-1" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "INSERT", { "x", "abc", "1", "1.5" }, 4u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "INSERT", { "x", "abc", "1", "1", "" }, 5u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "INSERT", { "", "", "16777217", "0" }, 4u, REXX_VM_STATUS_RESOURCE_LIMIT },
        { "INSERT", { "x", "", "4294967295" }, 3u, REXX_VM_STATUS_RESOURCE_LIMIT },
        { "LASTPOS", { "a", "a", "4294967296" }, 3u, REXX_VM_STATUS_RESOURCE_LIMIT },
        { "INSERT", { "x", "abc", "1", "1", ".", "extra" }, 6u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "OVERLAY", { "x", "abc", "0" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "OVERLAY", { "x", "abc", "1", "-1" }, 4u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "OVERLAY", { "x", "abc", "1", "2", ".." }, 5u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "OVERLAY", { "x", "abc", "1", "16777217" }, 4u, REXX_VM_STATUS_RESOURCE_LIMIT },
        { "DELSTR", { "abc", "0" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "DELSTR", { "abc", "1", "-1" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "DELSTR", { "abc", "2.5" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "DELWORD", { "a b", "0" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "DELWORD", { "a b", "1", "-1" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "SUBWORD", { "a b", "0" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "SUBWORD", { "a b", "1", "1.2" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "WORDINDEX", { "a b", "0" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "WORDLENGTH", { "a b", "-1" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "LASTPOS", { "a", "aba", "0" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "LASTPOS", { "a", "aba", "2.1" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "LASTPOS", { "", "aba", "0" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "LASTPOS", { "a", "aba", "1", "extra" }, 4u, REXX_VM_STATUS_INVALID_FUNCTION_CALL }
    };
    static const char integrated[] =
        "message = 'guideXOS REXX scripting'\n"
        "say left(message,8)\n"
        "say right('42',5,'0')\n"
        "say insert(' native','REXX scripting',4)\n"
        "say overlay('ABC','12345',2)\n"
        "say delstr('abcdef',3,2)\n"
        "say delword('one two three',2,1)\n"
        "say subword('one two  three',2)\n"
        "say wordindex(message,3)\n"
        "say wordlength(message,2)\n"
        "say lastpos('a','banana')\n"
        "exit 0\n";
    static const char interpret_source[] =
        "code = 'say left(\"q\",3,\".\"); say subword(\"one  two\",2)'\n"
        "interpret code\n"
        "exit 0\n";
    static const char shadow_source[] =
        "say left('builtin',2)\n"
        "say 'LEFT'('AB',3,'.')\n"
        "exit 0\n"
        "left: procedure\n"
        "  return 'shadow:'||arg(1)\n";
    static const char syntax_recovery_source[] =
        "signal on syntax name trapped\n"
        "value = right('x',-1)\n"
        "say 'unreachable'\n"
        "exit 1\n"
        "trapped:\n"
        "  say 'TRAPPED'\n"
        "  signal off syntax\n"
        "  say left('R',2,'.')\n"
        "  exit 0\n";
    static const char oversize_source[] =
        "say left('x',16777217)\n"
        "exit 0\n";
    size_t index;
    size_t failed = 0u;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
        failed += (size_t)run_case(&cases[index]);
    for (index = 0u; index < sizeof(errors) / sizeof(errors[0]); ++index)
        failed += (size_t)run_error_case(&errors[index]);
    failed += (size_t)test_maximum_output_length();

    failed += (size_t)run_program(
        integrated,
        "guideXOS\n00042\nREXX native scripting\n1ABC5\nabef\none three\n"
        "two  three\n15\n4\n6\n",
        "all ten RX13 functions integrate in one top-level program");
    failed += (size_t)run_program(interpret_source, "q..\ntwo\n",
                                  "RX13 built-ins execute through INTERPRET");
    failed += (size_t)run_program(shadow_source, "shadow:builtin\nAB.\n",
                                  "internal functions shadow BIFs and quoted names reach the BIF");
    failed += (size_t)run_program(syntax_recovery_source, "TRAPPED\nR.\n",
                                  "invalid BIF arguments are catchable and the VM recovers");
    failed += (size_t)run_program_status(
        oversize_source, REXX_VM_STATUS_RESOURCE_LIMIT,
        "oversized LEFT output is rejected by the VM resource boundary");
#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
    failed += (size_t)test_allocation_failure();
#endif

    failed += (size_t)check(assertions >= 250u,
                            "RX13 suite reaches 250 function-level assertions");
    printf("RX13 string-editing assertions: %zu; failures: %zu; cases: %zu; errors: %zu\n",
           assertions, failed, sizeof(cases) / sizeof(cases[0]),
           sizeof(errors) / sizeof(errors[0]));
    return failed == 0u ? 0 : 1;
}

#undef RX13_CASE
