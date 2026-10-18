#include "../rexx_builtin.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_compiler.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <string.h>

typedef struct Rx14Case {
    const char *name;
    const char *arguments[5];
    size_t argument_count;
    const char *expected;
} Rx14Case;

typedef struct Rx14ErrorCase {
    const char *name;
    const char *arguments[6];
    size_t argument_count;
    RexxVMStatus expected_status;
} Rx14ErrorCase;

static size_t assertions;
static char output[32768];
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

static int run_case(const Rx14Case *test)
{
    RexxValue arguments[5] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT,
        REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxValue result = REXX_VALUE_INIT;
    size_t index, expected_length = strlen(test->expected);
    RexxVMStatus status = REXX_VM_STATUS_OK;
    int failed = 0;
    char description[192];

    for (index = 0u; index < test->argument_count; ++index) {
        if (test->arguments[index] != NULL) {
            status = rexx_value_set_string(&arguments[index],
                                           test->arguments[index],
                                           strlen(test->arguments[index]));
            if (status != REXX_VM_STATUS_OK) {
                failed += check(0, "RX14 argument allocation succeeds");
                clear_values(arguments, 5u);
                return failed;
            }
        }
    }
    status = rexx_builtin_dispatch(
        test->name, strlen(test->name),
        test->argument_count == 0u ? NULL : arguments,
        test->argument_count, &result);
    (void)snprintf(description, sizeof(description), "%s returns successfully",
                   test->name);
    failed += check(status == REXX_VM_STATUS_OK, description);
    (void)snprintf(description, sizeof(description),
                   "%s returns a string of exact length", test->name);
    failed += check(status == REXX_VM_STATUS_OK &&
                    result.type == REXX_VALUE_STRING &&
                    result.as.string.length == expected_length, description);
    (void)snprintf(description, sizeof(description),
                   "%s preserves exact output bytes", test->name);
    failed += check(status == REXX_VM_STATUS_OK &&
                    result.type == REXX_VALUE_STRING &&
                    result.as.string.length == expected_length &&
                    memcmp(result.as.string.bytes, test->expected,
                           expected_length) == 0, description);
    if (status != REXX_VM_STATUS_OK || result.type != REXX_VALUE_STRING ||
        result.as.string.length != expected_length ||
        memcmp(result.as.string.bytes, test->expected, expected_length) != 0)
        fprintf(stderr, "  %s expected length=%zu [%s], got length=%zu [%.*s], status=%s\n",
                test->name, expected_length, test->expected,
                result.type == REXX_VALUE_STRING ? result.as.string.length : 0u,
                result.type == REXX_VALUE_STRING
                    ? (int)result.as.string.length : 0,
                result.type == REXX_VALUE_STRING ? result.as.string.bytes : "",
                rexx_vm_status_name(status));
    clear_values(arguments, 5u);
    rexx_value_clear(&result);
    return failed;
}

static int run_error_case(const Rx14ErrorCase *test)
{
    RexxValue arguments[6] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT,
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxValue result = REXX_VALUE_INIT;
    size_t index;
    RexxVMStatus status = REXX_VM_STATUS_OK;
    int failed;
    char description[192];

    for (index = 0u; index < test->argument_count; ++index) {
        if (test->arguments[index] != NULL) {
            status = rexx_value_set_string(&arguments[index],
                                           test->arguments[index],
                                           strlen(test->arguments[index]));
            if (status != REXX_VM_STATUS_OK) {
                failed = check(0, "RX14 error arguments allocate");
                clear_values(arguments, 6u);
                return failed;
            }
        }
    }
    status = rexx_builtin_dispatch(test->name, strlen(test->name),
                                   test->argument_count == 0u ? NULL : arguments,
                                   test->argument_count, &result);
    (void)snprintf(description, sizeof(description),
                   "%s rejects malformed or oversized input safely",
                   test->name);
    failed = check(status == test->expected_status &&
                   result.type == REXX_VALUE_UNSET, description);
    if (status != test->expected_status)
        fprintf(stderr, "  %s got %s, expected %s\n", test->name,
                rexx_vm_status_name(status),
                rexx_vm_status_name(test->expected_status));
    clear_values(arguments, 6u);
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
    if (chunk == NULL) return check(0, "RX14 integration source compiles");
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return check(0, "RX14 integration VM initializes");
    }
    output_length = 0u;
    memset(output, 0, sizeof(output));
    rexx_vm_set_output_writer(&vm, capture_output, NULL);
    status = rexx_vm_execute(&vm, chunk);
    failed = check(status == REXX_VM_STATUS_OK &&
                   output_length == strlen(expected_output) &&
                   memcmp(output, expected_output, output_length) == 0,
                   description);
    if (failed != 0)
        fprintf(stderr, "  expected [%s], got [%.*s], status=%s\n",
                expected_output, (int)output_length, output,
                rexx_vm_status_name(status));
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
    if (chunk == NULL) return check(0, "RX14 error source compiles");
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return check(0, "RX14 error VM initializes");
    }
    status = rexx_vm_execute(&vm, chunk);
    failed = check(status == expected_status, description);
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    return failed;
}

static int run_program_reuse_after_failure(void)
{
    static const char bad_source[] = "say abs('not a number')\n";
    static const char good_source[] = "say abs('-7.5')\n";
    RexxVM vm;
    RexxBytecodeChunk *bad_chunk = compile_source(bad_source);
    RexxBytecodeChunk *good_chunk = compile_source(good_source);
    RexxVMStatus bad_status, good_status;
    int failed = 0;

    if (bad_chunk == NULL || good_chunk == NULL) {
        rexx_bytecode_chunk_free(bad_chunk);
        rexx_bytecode_chunk_free(good_chunk);
        return check(0, "RX14 reuse sources compile");
    }
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(bad_chunk);
        rexx_bytecode_chunk_free(good_chunk);
        return check(0, "RX14 reusable VM initializes");
    }
    output_length = 0u;
    memset(output, 0, sizeof(output));
    rexx_vm_set_output_writer(&vm, capture_output, NULL);
    bad_status = rexx_vm_execute(&vm, bad_chunk);
    failed += check(bad_status == REXX_VM_STATUS_NUMERIC_ERROR,
                    "invalid numeric BIF fails without a trapped handler");
    output_length = 0u;
    good_status = rexx_vm_execute(&vm, good_chunk);
    failed += check(good_status == REXX_VM_STATUS_OK,
                    "the same VM executes a new chunk after numeric failure");
    failed += check(output_length == 4u && memcmp(output, "7.5\n", 4u) == 0,
                    "VM reuse after failure produces the exact next result");
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(bad_chunk);
    rexx_bytecode_chunk_free(good_chunk);
    return failed;
}

static int test_xrange_wrap_bytes(void)
{
    RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    size_t index;
    int failed = 0;
    static const unsigned char expected[] = {
        0xfeu, 0xffu, 0x00u, 0x01u, 0x02u
    };
    RexxVMStatus status = rexx_value_set_string(&arguments[0], "\xfe", 1u);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_string(&arguments[1], "\x02", 1u);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_dispatch("XRANGE", 6u, arguments, 2u, &result);
    failed += check(status == REXX_VM_STATUS_OK,
                    "XRANGE wraps from byte FF to byte 00");
    failed += check(status == REXX_VM_STATUS_OK &&
                    result.type == REXX_VALUE_STRING &&
                    result.as.string.length == sizeof(expected),
                    "XRANGE wrapped result has exact byte length");
    for (index = 0u; index < sizeof(expected); ++index)
        failed += check(status == REXX_VM_STATUS_OK &&
                        (unsigned char)result.as.string.bytes[index] == expected[index],
                        "XRANGE wrapped result preserves each byte");
    clear_values(arguments, 2u);
    rexx_value_clear(&result);
    return failed;
}

static int test_maximum_argument_count(void)
{
    RexxValue arguments[21] = { REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    size_t index;
    int failed = 0;
    RexxVMStatus status = REXX_VM_STATUS_OK;
    for (index = 0u; index < 21u; ++index) {
        char value[8];
        size_t length;
        int written = snprintf(value, sizeof(value), "%zu", index);
        if (written < 0 || (size_t)written >= sizeof(value)) {
            status = REXX_VM_STATUS_INVALID_ARGUMENT;
            break;
        }
        length = (size_t)written;
        status = rexx_value_set_string(&arguments[index], value, length);
        if (status != REXX_VM_STATUS_OK) break;
    }
    failed += check(status == REXX_VM_STATUS_OK,
                    "MAX boundary arguments are allocated");
    if (status == REXX_VM_STATUS_OK)
        status = rexx_builtin_dispatch("MAX", 3u, arguments, 20u, &result);
    failed += check(status == REXX_VM_STATUS_OK &&
                    result.type == REXX_VALUE_STRING &&
                    result.as.string.length == 2u &&
                    memcmp(result.as.string.bytes, "19", 2u) == 0,
                    "MAX accepts twenty positional arguments");
    status = rexx_builtin_dispatch("MAX", 3u, arguments, 21u, &result);
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                    result.type == REXX_VALUE_UNSET,
                    "MAX rejects more than twenty positional arguments");
    clear_values(arguments, 21u);
    rexx_value_clear(&result);
    return failed;
}

#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
static int test_allocation_failure(void)
{
    static const Rx14Case cases[] = {
        { "ABS", { "-123.40" }, 1u, "123.40" },
        { "SIGN", { "-0.01" }, 1u, "-1" },
        { "MAX", { "-4", "8" }, 2u, "8" },
        { "MIN", { "-4", "8" }, 2u, "-4" },
        { "TRUNC", { "12.349", "2" }, 2u, "12.34" },
        { "FORMAT", { "12.349", "5", "2" }, 3u, "   12.35" },
        { "DATATYPE", { "12.3", "N" }, 2u, "1" },
        { "SYMBOL", { "thing" }, 1u, "LIT" },
        { "XRANGE", { "a", "c" }, 2u, "abc" }
    };
    const size_t initial_live = rexx_vm_test_allocator_live_blocks();
    size_t case_index;
    int failed = 0;

    for (case_index = 0u; case_index < sizeof(cases) / sizeof(cases[0]);
         ++case_index) {
        const Rx14Case *test = &cases[case_index];
        RexxValue arguments[5] = {
            REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT,
            REXX_VALUE_INIT, REXX_VALUE_INIT
        };
        RexxValue result = REXX_VALUE_INIT;
        size_t index;
        RexxVMStatus status = REXX_VM_STATUS_OK;
        for (index = 0u; index < test->argument_count; ++index) {
            if (test->arguments[index] != NULL)
                status = rexx_value_set_string(&arguments[index],
                                               test->arguments[index],
                                               strlen(test->arguments[index]));
        }
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX14 allocation arguments are created");
        rexx_vm_test_allocator_fail_on(1u);
        status = rexx_builtin_dispatch(test->name, strlen(test->name),
                                       arguments, test->argument_count, &result);
        rexx_vm_test_allocator_disable();
        failed += check(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
                        result.type == REXX_VALUE_UNSET &&
                        arguments[0].type == REXX_VALUE_STRING,
                        "RX14 output allocation failure leaves inputs intact");
        status = rexx_builtin_dispatch(test->name, strlen(test->name),
                                       arguments, test->argument_count, &result);
        failed += check(status == REXX_VM_STATUS_OK &&
                        result.type == REXX_VALUE_STRING &&
                        result.as.string.length == strlen(test->expected) &&
                        memcmp(result.as.string.bytes, test->expected,
                               strlen(test->expected)) == 0,
                        "RX14 BIF recovers after allocation failure");
        clear_values(arguments, 5u);
        rexx_value_clear(&result);
    }
    failed += check(rexx_vm_test_allocator_live_blocks() == initial_live,
                    "RX14 allocation failures release owned buffers");
    return failed;
}
#endif

#define RX14_CASE(name, a0, a1, a2, a3, a4, count, value) \
    { name, { a0, a1, a2, a3, a4 }, count, value }

int main(void)
{
    static const Rx14Case cases[] = {
        RX14_CASE("ABS", "-0", NULL, NULL, NULL, NULL, 1u, "0"),
        RX14_CASE("ABS", "-42.5", NULL, NULL, NULL, NULL, 1u, "42.5"),
        RX14_CASE("abs", "-0012.3400", NULL, NULL, NULL, NULL, 1u, "12.3400"),
        RX14_CASE("ABS", "+3.5", NULL, NULL, NULL, NULL, 1u, "3.5"),
        RX14_CASE("SIGN", "0", NULL, NULL, NULL, NULL, 1u, "0"),
        RX14_CASE("SIGN", "-0.0001", NULL, NULL, NULL, NULL, 1u, "-1"),
        RX14_CASE("SIGN", ".001", NULL, NULL, NULL, NULL, 1u, "1"),
        RX14_CASE("SIGN", "+0", NULL, NULL, NULL, NULL, 1u, "0"),
        RX14_CASE("MAX", "-2.5", "-2.49", NULL, NULL, NULL, 2u, "-2.49"),
        RX14_CASE("MAX", "2", "10", "3", "9", NULL, 4u, "10"),
        RX14_CASE("MAX", "1", "2", "3", "4", "5", 5u, "5"),
        RX14_CASE("MIN", "-2.5", "-2.49", NULL, NULL, NULL, 2u, "-2.5"),
        RX14_CASE("MIN", ".01", "0.001", "1", NULL, NULL, 3u, "0.001"),
        RX14_CASE("MIN", "10", "2", "3", "9", NULL, 4u, "2"),
        RX14_CASE("MAX", "1", "2", "3", "4", "5", 5u, "5"),
        RX14_CASE("MAX", "1.000", "1", NULL, NULL, NULL, 2u, "1.000"),
        RX14_CASE("MAX", "-0.1", "-0.01", NULL, NULL, NULL, 2u, "-0.01"),
        RX14_CASE("MAX", "1.2", "1.19", NULL, NULL, NULL, 2u, "1.2"),
        RX14_CASE("MIN", "0.1", "0.01", NULL, NULL, NULL, 2u, "0.01"),
        RX14_CASE("MIN", "100", "99.9", NULL, NULL, NULL, 2u, "99.9"),
        RX14_CASE("TRUNC", "123.456", NULL, NULL, NULL, NULL, 1u, "123"),
        RX14_CASE("TRUNC", "123.456", "2", NULL, NULL, NULL, 2u, "123.45"),
        RX14_CASE("TRUNC", "123.456", "0", NULL, NULL, NULL, 2u, "123"),
        RX14_CASE("TRUNC", "-123.456", "2", NULL, NULL, NULL, 2u, "-123.45"),
        RX14_CASE("TRUNC", ".456", "4", NULL, NULL, NULL, 2u, "0.4560"),
        RX14_CASE("TRUNC", "1200", "2", NULL, NULL, NULL, 2u, "1200.00"),
        RX14_CASE("TRUNC", "-0.01", "0", NULL, NULL, NULL, 2u, "0"),
        RX14_CASE("TRUNC", "1E3", "1", NULL, NULL, NULL, 2u, "1000.0"),
        RX14_CASE("TRUNC", "-9.999", "2", NULL, NULL, NULL, 2u, "-9.99"),
        RX14_CASE("TRUNC", "0", "3", NULL, NULL, NULL, 2u, "0.000"),
        RX14_CASE("TRUNC", "99.999", "1", NULL, NULL, NULL, 2u, "99.9"),
        RX14_CASE("FORMAT", "1.234", NULL, NULL, NULL, NULL, 1u, "1.234"),
        RX14_CASE("FORMAT", "1.234", "6", NULL, NULL, NULL, 2u, "     1.234"),
        RX14_CASE("FORMAT", "1.236", "5", "2", NULL, NULL, 3u, "    1.24"),
        RX14_CASE("FORMAT", "1.73", "4", "0", NULL, NULL, 3u, "   2"),
        RX14_CASE("FORMAT", "-1.236", "6", "2", NULL, NULL, 3u, "    -1.24"),
        RX14_CASE("FORMAT", "12", "5", "3", NULL, NULL, 3u, "   12.000"),
        RX14_CASE("FORMAT", "1.2", NULL, "4", NULL, NULL, 3u, "1.2000"),
        RX14_CASE("FORMAT", "1234", NULL, NULL, "0", NULL, 4u, "1234"),
        RX14_CASE("FORMAT", "0.00123", NULL, NULL, NULL, "2", 5u, "1.23E-3"),
        RX14_CASE("FORMAT", "1234567", NULL, NULL, NULL, "3", 5u, "1.234567E+6"),
        RX14_CASE("FORMAT", "0", "4", "2", "3", "0", 5u, "   0.00"),
        RX14_CASE("FORMAT", "1.23", NULL, NULL, "3", "0", 5u, "1.23     "),
        RX14_CASE("FORMAT", "999.9", NULL, "0", NULL, "1", 5u, "1E+3"),
        RX14_CASE("FORMAT", "0.00012", NULL, NULL, "4", "2", 5u, "1.2E-0004"),
        RX14_CASE("FORMAT", "-12", "4", "0", "0", "0", 5u, " -12"),
        RX14_CASE("FORMAT", "12.345", "4", "2", NULL, NULL, 3u, "  12.35"),
        RX14_CASE("DATATYPE", "12.3", NULL, NULL, NULL, NULL, 1u, "NUM"),
        RX14_CASE("DATATYPE", " 12.3 ", "n", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "abc", NULL, NULL, NULL, NULL, 1u, "CHAR"),
        RX14_CASE("DATATYPE", "Ab9", "A", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "10101", "B", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "10 01", "B", NULL, NULL, NULL, 2u, "0"),
        RX14_CASE("DATATYPE", "abc", "L", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "aBc", "M", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "ABC", "U", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "a1", "U", NULL, NULL, NULL, 2u, "0"),
        RX14_CASE("DATATYPE", "aBc_9", "S", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "A..B", "S", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "2name", "S", NULL, NULL, NULL, 2u, "0"),
        RX14_CASE("DATATYPE", "0A FF", "X", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "A F", "X", NULL, NULL, NULL, 2u, "0"),
        RX14_CASE("DATATYPE", "", "X", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "12", "W", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "12.0", "W", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "12.01", "W", NULL, NULL, NULL, 2u, "0"),
        RX14_CASE("DATATYPE", "-12", "W", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "12.000000001", "W", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "A1", "M", NULL, NULL, NULL, 2u, "0"),
        RX14_CASE("DATATYPE", "9", "A", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "", "A", NULL, NULL, NULL, 2u, "0"),
        RX14_CASE("DATATYPE", "AB", "L", NULL, NULL, NULL, 2u, "0"),
        RX14_CASE("DATATYPE", "0a", "X", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("DATATYPE", "0 A", "X", NULL, NULL, NULL, 2u, "0"),
        RX14_CASE("DATATYPE", "123", "N", NULL, NULL, NULL, 2u, "1"),
        RX14_CASE("SYMBOL", "missing", NULL, NULL, NULL, NULL, 1u, "LIT"),
        RX14_CASE("SYMBOL", "12.3", NULL, NULL, NULL, NULL, 1u, "LIT"),
        RX14_CASE("SYMBOL", "not a symbol", NULL, NULL, NULL, NULL, 1u, "BAD"),
        RX14_CASE("SYMBOL", "", NULL, NULL, NULL, NULL, 1u, "BAD"),
        RX14_CASE("SYMBOL", "?name_2", NULL, NULL, NULL, NULL, 1u, "LIT"),
        RX14_CASE("SYMBOL", "9name", NULL, NULL, NULL, NULL, 1u, "BAD"),
        RX14_CASE("SYMBOL", "a.b", NULL, NULL, NULL, NULL, 1u, "LIT"),
        RX14_CASE("SYMBOL", "#name", NULL, NULL, NULL, NULL, 1u, "LIT"),
        RX14_CASE("SYMBOL", " 12 ", NULL, NULL, NULL, NULL, 1u, "BAD"),
        RX14_CASE("XRANGE", "a", "c", NULL, NULL, NULL, 2u, "abc"),
        RX14_CASE("XRANGE", "x", "z", NULL, NULL, NULL, 2u, "xyz"),
        RX14_CASE("XRANGE", "A", "A", NULL, NULL, NULL, 2u, "A"),
        RX14_CASE("XRANGE", "0", "2", NULL, NULL, NULL, 2u, "012"),
    };
    static const Rx14ErrorCase errors[] = {
        { "ABS", { "twelve" }, 1u, REXX_VM_STATUS_NUMERIC_ERROR },
        { "SIGN", { "" }, 1u, REXX_VM_STATUS_NUMERIC_ERROR },
        { "MAX", { "1", "x" }, 2u, REXX_VM_STATUS_NUMERIC_ERROR },
        { "MIN", { "1" }, 1u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "TRUNC", { "1", "-1" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "TRUNC", { "1", "1.5" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "FORMAT", { "1", "0" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "FORMAT", { "1", "1", "16777217" }, 3u, REXX_VM_STATUS_RESOURCE_LIMIT },
        { "FORMAT", { "1", "1", "0", "10" }, 4u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "FORMAT", { "1E100", "2", "0", "2", "0" }, 5u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "DATATYPE", { "abc", "" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "DATATYPE", { "abc", "Q" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "SYMBOL", { "a", "extra" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "XRANGE", { "ab" }, 1u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "XRANGE", { NULL, "cd" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "XRANGE", { "a", "b", "c" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL }
    };
    static const char integrated[] =
        "numeric digits 4\n"
        "say abs('-001.20') sign('-9') max('1.2','1.19') min('9','-2')\n"
        "say trunc('12.345',2) format('12.345',6,2)\n"
        "say datatype('12.0','W') datatype('00 FF','X') datatype('word','S')\n"
        "x = 7\n"
        "say symbol('x') symbol('y') symbol('7') symbol('bad name')\n"
        "say xrange('a','e')\n"
        "exit 0\n";
    static const char interpret_source[] =
        "numeric digits 3\n"
        "source = \"say format('12345',,2,,3)\"\n"
        "interpret source\n"
        "say trunc('9.876',1)\n";
    static const char nested_interpret_source[] =
        "outer = \"inner='say abs(-7)'; interpret inner\"\n"
        "interpret outer\n";
    static const char fuzz_source[] =
        "numeric digits 5\n"
        "numeric fuzz 2\n"
        "say max('1.0001','1.0002') min('1.0001','1.0002')\n";
    static const char stem_default_source[] =
        "stem. = 'default'\n"
        "say symbol('stem.other')\n";
    static const char expose_source[] =
        "x = 10\n"
        "call check\n"
        "exit 0\n"
        "check: procedure expose x\n"
        "say symbol('x') symbol('missing')\n";
    static const char engineering_source[] =
        "numeric form engineering\n"
        "say format('1234567',,2,2,3)\n"
        "say format('0.000123',,,3,2)\n";
    static const char syntax_recovery_source[] =
        "signal on syntax name trapped\n"
        "say format('1',0)\n"
        "say 'unreachable'\n"
        "exit 1\n"
        "trapped:\n"
        "  say 'TRAPPED'\n"
        "  signal off syntax\n"
        "  say abs('-2')\n"
        "  exit 0\n";
    size_t index, failed = 0u;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
        failed += (size_t)run_case(&cases[index]);
    for (index = 0u; index < sizeof(errors) / sizeof(errors[0]); ++index)
        failed += (size_t)run_error_case(&errors[index]);
    failed += (size_t)test_xrange_wrap_bytes();
    failed += (size_t)test_maximum_argument_count();
    failed += (size_t)run_program(
        integrated,
        "1.20 -1 1.2 -2\n12.35     12.35\n1 1 1\nVAR LIT LIT BAD\nabcde\n",
        "RX14 functions preserve decimal precision and compose in the VM");
    failed += (size_t)run_program(interpret_source, "1.23E+4\n9.8\n",
                                  "RX14 functions run through INTERPRET");
    failed += (size_t)run_program(nested_interpret_source, "7\n",
                                  "RX14 functions run through nested INTERPRET");
    failed += (size_t)run_program(fuzz_source, "1.0002 1.0001\n",
                                  "MAX/MIN retain exact ordering under NUMERIC FUZZ");
    failed += (size_t)run_program(stem_default_source, "VAR\n",
                                  "SYMBOL sees initialized stem defaults");
    failed += (size_t)run_program(expose_source, "VAR LIT\n",
                                  "SYMBOL sees PROCEDURE EXPOSE generations");
    failed += (size_t)run_program(
        engineering_source, "1.23E+06\n123E-006\n",
        "FORMAT follows NUMERIC FORM SCIENTIFIC and ENGINEERING");
    failed += (size_t)run_program(
        syntax_recovery_source, "TRAPPED\n2\n",
        "invalid numeric format calls are catchable and VM recovers");
    failed += (size_t)run_program_status(
        "say max('1', 'not numeric')\n",
        REXX_VM_STATUS_NUMERIC_ERROR,
        "MAX rejects a nonnumeric argument before returning a result");
    failed += (size_t)run_program(
        "signal on syntax name trapped\nsay abs('bad')\nexit 91\n"
        "trapped:\nsignal off syntax\nsay 'RX14_NUMERIC_SYNTAX_RECOVERED'\n",
        "RX14_NUMERIC_SYNTAX_RECOVERED\n",
        "invalid numeric operands raise catchable SYNTAX");
    failed += (size_t)run_program_status(
        "signal on syntax name trapped\nsay trunc('1',16777217)\n"
        "exit 91\ntrapped:\nsay 'UNREACHABLE'\n",
        REXX_VM_STATUS_RESOURCE_LIMIT,
        "resource-limit failures remain terminal under SYNTAX trapping");
    failed += (size_t)run_program_reuse_after_failure();
#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
    failed += (size_t)test_allocation_failure();
#endif
    failed += (size_t)check(assertions >= 300u,
                            "RX14 suite reaches 300 function-level assertions");
    printf("RX14 numeric/inspection assertions: %zu; failures: %zu; cases: %zu; errors: %zu\n",
           assertions, failed, sizeof(cases) / sizeof(cases[0]),
           sizeof(errors) / sizeof(errors[0]));
    return failed == 0u ? 0 : 1;
}

#undef RX14_CASE
