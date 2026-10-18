#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_compiler.h"
#include "../rexx_builtin.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <string.h>

typedef struct Rx15Bytes {
    const unsigned char *bytes;
    size_t length;
} Rx15Bytes;

typedef struct Rx15Case {
    const char *name;
    Rx15Bytes arguments[2];
    size_t argument_count;
    Rx15Bytes expected;
    RexxVMStatus expected_status;
} Rx15Case;

static size_t assertions;
static size_t cases_run;
static size_t errors_run;
static size_t allocations_run;
static char captured_output[8192];
static size_t captured_length;

#define RX15_TEXT(value) \
    { (const unsigned char *)(value), sizeof(value) - 1u }
#define RX15_RAW(value) \
    { (const unsigned char *)(value), sizeof(value) }

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

static int dispatch_case(const Rx15Case *test)
{
    RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    size_t index;
    RexxVMStatus status = REXX_VM_STATUS_OK;
    int failed = 0;
    char description[160];

    for (index = 0u; index < test->argument_count; ++index) {
        status = rexx_value_set_string(
            &arguments[index], (const char *)test->arguments[index].bytes,
            test->arguments[index].length);
        if (status != REXX_VM_STATUS_OK) {
            failed += check(0, "RX15 input allocation succeeds");
            clear_values(arguments, 2u);
            return failed;
        }
    }
    status = rexx_builtin_dispatch(test->name, strlen(test->name), arguments,
                                   test->argument_count, &result);
    (void)snprintf(description, sizeof(description), "%s status matches", test->name);
    failed += check(status == test->expected_status, description);
    if (test->expected_status == REXX_VM_STATUS_OK) {
        (void)snprintf(description, sizeof(description), "%s exact byte length", test->name);
        failed += check(result.type == REXX_VALUE_STRING &&
                        result.as.string.length == test->expected.length,
                        description);
        (void)snprintf(description, sizeof(description), "%s exact output bytes", test->name);
        failed += check(result.type == REXX_VALUE_STRING &&
                        result.as.string.length == test->expected.length &&
                        memcmp(result.as.string.bytes, test->expected.bytes,
                               test->expected.length) == 0,
                        description);
    } else {
        (void)snprintf(description, sizeof(description), "%s clears partial output", test->name);
        failed += check(result.type == REXX_VALUE_UNSET, description);
    }
    ++cases_run;
    if (test->expected_status != REXX_VM_STATUS_OK) ++errors_run;
    if (failed != 0) {
        fprintf(stderr, "  %s expected status=%s length=%zu; got status=%s type=%d length=%zu\n",
                test->name, rexx_vm_status_name(test->expected_status),
                test->expected.length, rexx_vm_status_name(status), (int)result.type,
                result.type == REXX_VALUE_STRING ? result.as.string.length : 0u);
    }
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
    if (chunk == NULL) return check(0, "RX15 integration source compiles");
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return check(0, "RX15 integration VM initializes");
    }
    captured_length = 0u;
    memset(captured_output, 0, sizeof(captured_output));
    rexx_vm_set_output_writer(&vm, capture_output, NULL);
    status = rexx_vm_execute(&vm, chunk);
    failed = check(status == REXX_VM_STATUS_OK &&
                   captured_length == strlen(expected) &&
                   memcmp(captured_output, expected, captured_length) == 0,
                   description);
    if (failed != 0)
        fprintf(stderr, "  integration status=%s, output length=%zu (expected %zu)\n",
                rexx_vm_status_name(status), captured_length, strlen(expected));
    if (failed != 0)
        fprintf(stderr, "  actual output: [%.*s]\n", (int)captured_length,
                captured_output);
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    return failed;
}

static int run_program_status(const char *source, RexxVMStatus expected,
                              const char *description)
{
    RexxVM vm;
    RexxBytecodeChunk *chunk = compile_source(source);
    RexxVMStatus status;
    int failed;
    if (chunk == NULL) return check(0, "RX15 error source compiles");
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return check(0, "RX15 error VM initializes");
    }
    status = rexx_vm_execute(&vm, chunk);
    failed = check(status == expected, description);
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    return failed;
}

static int test_numeric_context(void)
{
    RexxVM vm;
    RexxValue argument = REXX_VALUE_INIT;
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    int failed = 0;
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK)
        return check(0, "RX15 numeric context VM initializes");
    vm.numeric_digits = 2u;
    vm.numeric_fuzz = 1u;
    (void)rexx_value_set_string(&argument, "100", 3u);
    status = rexx_builtin_dispatch_with_vm(&vm, "X2D", 3u, &argument, 1u,
                                          &result);
    failed += check(status == REXX_VM_STATUS_NUMERIC_ERROR &&
                    result.type == REXX_VALUE_UNSET,
                    "X2D rejects results exceeding NUMERIC DIGITS");
    failed += check(vm.numeric_digits == 2u && vm.numeric_fuzz == 1u,
                    "failed conversions preserve DIGITS and FUZZ settings");
    vm.numeric_digits = 3u;
    status = rexx_builtin_dispatch_with_vm(&vm, "X2D", 3u, &argument, 1u,
                                          &result);
    failed += check(status == REXX_VM_STATUS_OK &&
                    result.type == REXX_VALUE_STRING &&
                    result.as.string.length == 3u &&
                    memcmp(result.as.string.bytes, "256", 3u) == 0,
                    "X2D recovers after insufficient precision");
    failed += check(vm.numeric_digits == 3u && vm.numeric_fuzz == 1u,
                    "successful conversions preserve numeric settings");
    rexx_value_clear(&argument);
    rexx_value_clear(&result);
    rexx_vm_free(&vm);
    return failed;
}

static int test_wide_numeric_roundtrips(void)
{
    static const unsigned char two_to_64[] = {
        0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    RexxVM vm;
    RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    int failed = 0;
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK)
        return check(0, "RX15 wide numeric VM initializes");
    vm.numeric_digits = 20u;
    (void)rexx_value_set_string(&arguments[0], "18446744073709551616", 20u);
    status = rexx_builtin_dispatch_with_vm(&vm, "D2X", 3u, arguments, 1u,
                                          &result);
    failed += check(status == REXX_VM_STATUS_OK &&
                    result.type == REXX_VALUE_STRING &&
                    result.as.string.length == 17u &&
                    memcmp(result.as.string.bytes, "10000000000000000", 17u) == 0,
                    "D2X handles integers wider than uint64 without narrowing");
    rexx_value_clear(&result);
    (void)rexx_value_set_string(&arguments[0], "10000000000000000", 17u);
    status = rexx_builtin_dispatch_with_vm(&vm, "X2D", 3u, arguments, 1u,
                                          &result);
    failed += check(status == REXX_VM_STATUS_OK &&
                    result.type == REXX_VALUE_STRING &&
                    result.as.string.length == 20u &&
                    memcmp(result.as.string.bytes, "18446744073709551616", 20u) == 0,
                    "X2D returns exact decimal beyond native word size");
    rexx_value_clear(&result);
    (void)rexx_value_set_string(&arguments[0], "18446744073709551616", 20u);
    status = rexx_builtin_dispatch_with_vm(&vm, "D2C", 3u, arguments, 1u,
                                          &result);
    failed += check(status == REXX_VM_STATUS_OK &&
                    result.type == REXX_VALUE_STRING &&
                    result.as.string.length == sizeof(two_to_64) &&
                    memcmp(result.as.string.bytes, two_to_64,
                           sizeof(two_to_64)) == 0,
                    "D2C emits wide byte sequences in most-significant-first order");
    rexx_value_clear(&result);
    (void)rexx_value_set_string(&arguments[0], (const char *)two_to_64,
                                sizeof(two_to_64));
    status = rexx_builtin_dispatch_with_vm(&vm, "C2D", 3u, arguments, 1u,
                                          &result);
    failed += check(status == REXX_VM_STATUS_OK &&
                    result.type == REXX_VALUE_STRING &&
                    result.as.string.length == 20u &&
                    memcmp(result.as.string.bytes, "18446744073709551616", 20u) == 0,
                    "C2D accumulates multibyte integers without narrowing");
    failed += check(vm.numeric_digits == 20u,
                    "wide conversions leave the caller numeric setting intact");
    clear_values(arguments, 2u);
    rexx_value_clear(&result);
    rexx_vm_free(&vm);
    return failed;
}

#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
static int test_allocation_failure(void)
{
    static const unsigned char raw_81[] = { 0x81u };
    static const unsigned char d2c_expected[] = { 0x00u, 0x81u };
    static const Rx15Case cases[] = {
        { "C2D", { RX15_RAW(raw_81), { NULL, 0u } }, 1u,
          RX15_TEXT("129"), REXX_VM_STATUS_OK },
        { "C2X", { RX15_TEXT("ABC"), { NULL, 0u } }, 1u,
          RX15_TEXT("414243"), REXX_VM_STATUS_OK },
        { "D2C", { RX15_TEXT("129"), RX15_TEXT("2") }, 2u,
          RX15_RAW(d2c_expected), REXX_VM_STATUS_OK },
        { "D2X", { RX15_TEXT("-127"), RX15_TEXT("4") }, 2u,
          RX15_TEXT("FF81"), REXX_VM_STATUS_OK },
        { "X2C", { RX15_TEXT("F"), { NULL, 0u } }, 1u,
          { (const unsigned char *)"\x0f", 1u }, REXX_VM_STATUS_OK },
        { "X2D", { RX15_TEXT("FF"), RX15_TEXT("2") }, 2u,
          RX15_TEXT("-1"), REXX_VM_STATUS_OK },
        { "B2X", { RX15_TEXT("10111"), { NULL, 0u } }, 1u,
          RX15_TEXT("17"), REXX_VM_STATUS_OK },
        { "X2B", { RX15_TEXT("C3"), { NULL, 0u } }, 1u,
          RX15_TEXT("11000011"), REXX_VM_STATUS_OK }
    };
    const size_t initial_live = rexx_vm_test_allocator_live_blocks();
    size_t case_index;
    int failed = 0;
    for (case_index = 0u; case_index < sizeof(cases) / sizeof(cases[0]);
         ++case_index) {
        const Rx15Case *test = &cases[case_index];
        RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
        RexxValue result = REXX_VALUE_INIT;
        size_t index;
        RexxVMStatus status = REXX_VM_STATUS_OK;
        for (index = 0u; index < test->argument_count; ++index)
            status = rexx_value_set_string(
                &arguments[index], (const char *)test->arguments[index].bytes,
                test->arguments[index].length);
        failed += check(status == REXX_VM_STATUS_OK,
                        "RX15 allocation-test inputs are created");
        rexx_vm_test_allocator_fail_on(1u);
        status = rexx_builtin_dispatch(test->name, strlen(test->name), arguments,
                                       test->argument_count, &result);
        rexx_vm_test_allocator_disable();
        failed += check(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
                        result.type == REXX_VALUE_UNSET &&
                        arguments[0].type == REXX_VALUE_STRING,
                        "RX15 allocation failure clears output and preserves input");
        rexx_vm_test_allocator_fail_on(1u);
        status = rexx_builtin_dispatch(test->name, strlen(test->name), arguments,
                                       test->argument_count, &result);
        rexx_vm_test_allocator_disable();
        failed += check(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
                        result.type == REXX_VALUE_UNSET,
                        "RX15 repeated allocation failure remains recoverable");
        status = rexx_builtin_dispatch(test->name, strlen(test->name), arguments,
                                       test->argument_count, &result);
        failed += check(status == REXX_VM_STATUS_OK &&
                        result.type == REXX_VALUE_STRING &&
                        result.as.string.length == test->expected.length &&
                        memcmp(result.as.string.bytes, test->expected.bytes,
                               test->expected.length) == 0,
                        "RX15 BIF succeeds after injected allocation failure");
        ++allocations_run;
        clear_values(arguments, 2u);
        rexx_value_clear(&result);
    }
    failed += check(rexx_vm_test_allocator_live_blocks() == initial_live,
                    "RX15 allocation failures release all buffers");
    return failed;
}
#endif

int main(void)
{
    static const unsigned char byte_81[] = { 0x81u };
    static const unsigned char bytes_00ff80[] = { 0x00u, 0xffu, 0x80u, 0x1fu };
    static const unsigned char bytes_ff81[] = { 0xffu, 0x81u };
    static const unsigned char bytes_00_81[] = { 0x00u, 0x81u };
    static const unsigned char bytes_ff4[] = { 0xffu, 0xffu, 0xffu, 0xffu };
    static const unsigned char bytes_0f[] = { 0x0fu };
    static const unsigned char bytes_01d8[] = { 0x01u, 0xd8u };
    static const Rx15Case cases[] = {
        { "C2X", { RX15_TEXT("ABC"), { NULL, 0u } }, 1u,
          RX15_TEXT("414243"), REXX_VM_STATUS_OK },
        { "C2X", { RX15_RAW(bytes_00ff80), { NULL, 0u } }, 1u,
          RX15_TEXT("00FF801F"), REXX_VM_STATUS_OK },
        { "C2X", { RX15_TEXT(""), { NULL, 0u } }, 1u,
          RX15_TEXT(""), REXX_VM_STATUS_OK },
        { "C2D", { RX15_RAW(byte_81), { NULL, 0u } }, 1u,
          RX15_TEXT("129"), REXX_VM_STATUS_OK },
        { "C2D", { RX15_RAW(byte_81), RX15_TEXT("1") }, 2u,
          RX15_TEXT("-127"), REXX_VM_STATUS_OK },
        { "C2D", { RX15_RAW(byte_81), RX15_TEXT("2") }, 2u,
          RX15_TEXT("129"), REXX_VM_STATUS_OK },
        { "C2D", { RX15_TEXT(""), { NULL, 0u } }, 1u,
          RX15_TEXT("0"), REXX_VM_STATUS_OK },
        { "C2D", { RX15_RAW(bytes_00_81), RX15_TEXT("0") }, 2u,
          RX15_TEXT("0"), REXX_VM_STATUS_OK },
        { "C2D", { RX15_RAW(bytes_ff81), RX15_TEXT("1") }, 2u,
          RX15_TEXT("-127"), REXX_VM_STATUS_OK },
        { "C2D", { RX15_RAW(bytes_00ff80), RX15_TEXT("0") }, 2u,
          RX15_TEXT("0"), REXX_VM_STATUS_OK },
        { "D2X", { RX15_TEXT("129"), { NULL, 0u } }, 1u,
          RX15_TEXT("81"), REXX_VM_STATUS_OK },
        { "D2X", { RX15_TEXT("129"), RX15_TEXT("4") }, 2u,
          RX15_TEXT("0081"), REXX_VM_STATUS_OK },
        { "D2X", { RX15_TEXT("129"), RX15_TEXT("1") }, 2u,
          RX15_TEXT("1"), REXX_VM_STATUS_OK },
        { "D2X", { RX15_TEXT("-127"), RX15_TEXT("2") }, 2u,
          RX15_TEXT("81"), REXX_VM_STATUS_OK },
        { "D2X", { RX15_TEXT("-127"), RX15_TEXT("4") }, 2u,
          RX15_TEXT("FF81"), REXX_VM_STATUS_OK },
        { "D2X", { RX15_TEXT("12"), RX15_TEXT("0") }, 2u,
          RX15_TEXT(""), REXX_VM_STATUS_OK },
        { "D2X", { RX15_TEXT("257"), RX15_TEXT("2") }, 2u,
          RX15_TEXT("01"), REXX_VM_STATUS_OK },
        { "D2X", { RX15_TEXT("+00129.000"), RX15_TEXT("4.0") }, 2u,
          RX15_TEXT("0081"), REXX_VM_STATUS_OK },
        { "D2X", { RX15_TEXT("1.29E2"), { NULL, 0u } }, 1u,
          RX15_TEXT("81"), REXX_VM_STATUS_OK },
        { "D2C", { RX15_TEXT("129"), RX15_TEXT("2") }, 2u,
          RX15_RAW(bytes_00_81), REXX_VM_STATUS_OK },
        { "D2C", { RX15_TEXT("-1"), RX15_TEXT("4") }, 2u,
          RX15_RAW(bytes_ff4), REXX_VM_STATUS_OK },
        { "D2C", { RX15_TEXT("-127"), RX15_TEXT("2") }, 2u,
          RX15_RAW(bytes_ff81), REXX_VM_STATUS_OK },
        { "D2C", { RX15_TEXT("129"), RX15_TEXT("1") }, 2u,
          { (const unsigned char *)"\x81", 1u }, REXX_VM_STATUS_OK },
        { "D2C", { RX15_TEXT("129.0"), { NULL, 0u } }, 1u,
          { (const unsigned char *)"\x81", 1u }, REXX_VM_STATUS_OK },
        { "D2C", { RX15_TEXT("0"), RX15_TEXT("0") }, 2u,
          RX15_TEXT(""), REXX_VM_STATUS_OK },
        { "X2C", { RX15_TEXT("414243"), { NULL, 0u } }, 1u,
          RX15_TEXT("ABC"), REXX_VM_STATUS_OK },
        { "X2C", { RX15_TEXT("F"), { NULL, 0u } }, 1u,
          RX15_RAW(bytes_0f), REXX_VM_STATUS_OK },
        { "X2C", { RX15_TEXT("1 d8"), { NULL, 0u } }, 1u,
          RX15_RAW(bytes_01d8), REXX_VM_STATUS_OK },
        { "X2C", { RX15_TEXT(""), { NULL, 0u } }, 1u,
          RX15_TEXT(""), REXX_VM_STATUS_OK },
        { "X2D", { RX15_TEXT("FF"), { NULL, 0u } }, 1u,
          RX15_TEXT("255"), REXX_VM_STATUS_OK },
        { "X2D", { RX15_TEXT("FF"), RX15_TEXT("2") }, 2u,
          RX15_TEXT("-1"), REXX_VM_STATUS_OK },
        { "X2D", { RX15_TEXT("FF"), RX15_TEXT("4") }, 2u,
          RX15_TEXT("255"), REXX_VM_STATUS_OK },
        { "X2D", { RX15_TEXT("FF"), RX15_TEXT("0") }, 2u,
          RX15_TEXT("0"), REXX_VM_STATUS_OK },
        { "X2D", { RX15_TEXT("F081"), RX15_TEXT("3") }, 2u,
          RX15_TEXT("129"), REXX_VM_STATUS_OK },
        { "X2D", { RX15_TEXT("F"), RX15_TEXT("1") }, 2u,
          RX15_TEXT("-1"), REXX_VM_STATUS_OK },
        { "X2D", { RX15_TEXT("F"), RX15_TEXT("2") }, 2u,
          RX15_TEXT("15"), REXX_VM_STATUS_OK },
        { "X2D", { RX15_TEXT(""), { NULL, 0u } }, 1u,
          RX15_TEXT("0"), REXX_VM_STATUS_OK },
        { "B2X", { RX15_TEXT("11000011"), { NULL, 0u } }, 1u,
          RX15_TEXT("C3"), REXX_VM_STATUS_OK },
        { "B2X", { RX15_TEXT("10111"), { NULL, 0u } }, 1u,
          RX15_TEXT("17"), REXX_VM_STATUS_OK },
        { "B2X", { RX15_TEXT("1 1111 0000"), { NULL, 0u } }, 1u,
          RX15_TEXT("1F0"), REXX_VM_STATUS_OK },
        { "B2X", { RX15_TEXT(""), { NULL, 0u } }, 1u,
          RX15_TEXT(""), REXX_VM_STATUS_OK },
        { "X2B", { RX15_TEXT("C3"), { NULL, 0u } }, 1u,
          RX15_TEXT("11000011"), REXX_VM_STATUS_OK },
        { "X2B", { RX15_TEXT("7"), { NULL, 0u } }, 1u,
          RX15_TEXT("0111"), REXX_VM_STATUS_OK },
        { "X2B", { RX15_TEXT("a1 0f"), { NULL, 0u } }, 1u,
          RX15_TEXT("1010000100001111"), REXX_VM_STATUS_OK },
        { "X2B", { RX15_TEXT(""), { NULL, 0u } }, 1u,
          RX15_TEXT(""), REXX_VM_STATUS_OK }
    };
    static const Rx15Case errors[] = {
        { "D2X", { RX15_TEXT("-1"), { NULL, 0u } }, 1u,
          { NULL, 0u }, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "D2C", { RX15_TEXT("12.5"), RX15_TEXT("2") }, 2u,
          { NULL, 0u }, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "D2X", { RX15_TEXT("bad"), { NULL, 0u } }, 1u,
          { NULL, 0u }, REXX_VM_STATUS_NUMERIC_ERROR },
        { "X2C", { RX15_TEXT("G1"), { NULL, 0u } }, 1u,
          { NULL, 0u }, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "X2C", { RX15_TEXT("A B"), { NULL, 0u } }, 1u,
          { NULL, 0u }, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "X2D", { RX15_TEXT("F Z"), { NULL, 0u } }, 1u,
          { NULL, 0u }, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "B2X", { RX15_TEXT("1020"), { NULL, 0u } }, 1u,
          { NULL, 0u }, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "B2X", { RX15_TEXT("1 0 0000"), { NULL, 0u } }, 1u,
          { NULL, 0u }, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "D2C", { RX15_TEXT("1"), RX15_TEXT("-1") }, 2u,
          { NULL, 0u }, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "D2X", { RX15_TEXT("1"), RX15_TEXT("1.5") }, 2u,
          { NULL, 0u }, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "D2C", { RX15_TEXT("1"), RX15_TEXT("16777217") }, 2u,
          { NULL, 0u }, REXX_VM_STATUS_RESOURCE_LIMIT },
        { "C2D", { RX15_RAW(byte_81), RX15_TEXT("16777217") }, 2u,
          { NULL, 0u }, REXX_VM_STATUS_RESOURCE_LIMIT },
        { "X2D", { RX15_TEXT("FFFFFFFFFF"), { NULL, 0u } }, 1u,
          { NULL, 0u }, REXX_VM_STATUS_NUMERIC_ERROR },
        { "B2X", { RX15_TEXT(" 1010"), { NULL, 0u } }, 1u,
          { NULL, 0u }, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "X2B", { RX15_TEXT("A  "), { NULL, 0u } }, 1u,
          { NULL, 0u }, REXX_VM_STATUS_INVALID_FUNCTION_CALL }
    };
    static const char integrated[] =
        "say c2x('ABC') c2x(X'00FF') c2d(X'81') c2d(X'81',1) c2d(X'81',2)\n"
        "say d2x(129) d2x(129,4) d2x(-127,2) d2x(-127,4) d2x(12,0)\n"
        "say c2x(d2c(129,2)) c2x(d2c(-1,4))\n"
        "say c2x(x2c('414243')) c2x(x2c('f')) x2d('ff') x2d('ff',2) x2d('ff',4)\n"
        "say b2x('11000011') b2x('10111') b2x('1 1111 0000') x2b('c3') x2b('7')\n"
        "numeric digits 20\n"
        "say d2x('18446744073709551616') x2d(d2x('18446744073709551616')) digits()\n";
    static const char recovery[] =
        "signal on syntax name trapped\n"
        "say x2d('GG')\n"
        "exit 99\n"
        "trapped:\n"
        "signal off syntax\n"
        "say 'RX15_RECOVERED' c2x(d2c(255,1))\n"
        "exit 0\n";
    size_t index;
    int failed = 0;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
        failed += dispatch_case(&cases[index]);
    for (index = 0u; index < sizeof(errors) / sizeof(errors[0]); ++index)
        failed += dispatch_case(&errors[index]);
    failed += test_numeric_context();
    failed += test_wide_numeric_roundtrips();
    failed += run_program(
        integrated,
        "414243 00FF 129 -127 129\n81 0081 81 FF81\n0081 FFFFFFFF\n414243 0F 255 -1 255\nC3 17 1F0 11000011 0111\n"
        "10000000000000000 18446744073709551616 20\n",
        "RX15 eight conversion BIFs compose and preserve exact bytes");
    failed += run_program(
        recovery, "RX15_RECOVERED FF\n",
        "RX15 syntax recovery permits a later conversion");
    failed += run_program_status(
        "numeric digits 2\nsay x2d('100')\n",
        REXX_VM_STATUS_NUMERIC_ERROR,
        "RX15 numeric conversions enforce NUMERIC DIGITS");
#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
    failed += test_allocation_failure();
#endif
    failed += check(cases_run >= 50u, "RX15 direct corpus covers at least 50 calls");
    printf("RX15 conversion assertions: %zu; failures: %d; cases: %zu; errors: %zu; allocation functions: %zu\n",
           assertions, failed, cases_run, errors_run, allocations_run);
    return failed == 0 ? 0 : 1;
}
