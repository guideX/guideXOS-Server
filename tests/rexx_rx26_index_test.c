#include "../rexx_builtin.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_compiler.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX26_CORPUS_CASES 12000u
#define RX26_MAX_HAYSTACK 384u
#define RX26_MAX_NEEDLE 32u
#define RX26_LARGE_STRING ((size_t)16777216u)

static size_t assertions;
static size_t failures;
static char captured_output[64];
static size_t captured_length;

static void check(int condition, const char *description)
{
    ++assertions;
    if (!condition) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", description);
    }
}

static int result_is_size(const RexxValue *result, size_t expected)
{
    char expected_text[3u * sizeof(size_t) + 1u];
    int written = snprintf(expected_text, sizeof(expected_text), "%zu", expected);
    return written > 0 && (size_t)written < sizeof(expected_text) &&
           result->type == REXX_VALUE_STRING &&
           result->as.string.length == (size_t)written &&
           memcmp(result->as.string.bytes, expected_text, (size_t)written) == 0;
}

static void clear_values(RexxValue *values, size_t count)
{
    size_t index;
    for (index = 0u; index < count; ++index) rexx_value_clear(&values[index]);
}

static int call_text(const char *name,
                     const char *first,
                     const char *second,
                     const char *third,
                     size_t argument_count,
                     RexxVMStatus expected_status,
                     size_t expected_result)
{
    RexxValue arguments[4] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxValue result = REXX_VALUE_INIT;
    const char *texts[4] = { first, second, third, "extra" };
    size_t index;
    RexxVMStatus status = REXX_VM_STATUS_OK;
    int unchanged = 1;

    for (index = 0u; index < argument_count && index < 4u; ++index) {
        if (texts[index] != NULL) {
            status = rexx_value_set_string(&arguments[index], texts[index],
                                           strlen(texts[index]));
            if (status != REXX_VM_STATUS_OK) {
                check(0, "text argument allocation succeeds");
                clear_values(arguments, 4u);
                return 1;
            }
        }
    }
    status = rexx_builtin_dispatch(name, strlen(name), arguments,
                                   argument_count, &result);
    check(status == expected_status,
          "INDEX/POS argument status matches the established validation contract");
    if (status != expected_status) {
        fprintf(stderr,
                "  %s status expected=%s actual=%s args=[%s,%s,%s] count=%zu\n",
                name, rexx_vm_status_name(expected_status),
                rexx_vm_status_name(status), first != NULL ? first : "<unset>",
                second != NULL ? second : "<unset>",
                third != NULL ? third : "<unset>", argument_count);
    }
    if (status == REXX_VM_STATUS_OK) {
        check(result_is_size(&result, expected_result),
              "INDEX/POS returns the expected one-based byte position");
    } else {
        check(result.type == REXX_VALUE_UNSET,
              "rejected INDEX/POS call has no partially constructed result");
    }
    for (index = 0u; index < argument_count && index < 4u; ++index) {
        if (texts[index] == NULL) {
            unchanged = unchanged && arguments[index].type == REXX_VALUE_UNSET;
        } else {
            size_t length = strlen(texts[index]);
            unchanged = unchanged && arguments[index].type == REXX_VALUE_STRING &&
                        arguments[index].as.string.length == length &&
                        memcmp(arguments[index].as.string.bytes, texts[index],
                               length) == 0;
        }
    }
    check(unchanged, "INDEX/POS leaves input arguments unchanged");
    clear_values(arguments, 4u);
    rexx_value_clear(&result);
    return status == expected_status ? 0 : 1;
}

static int call_binary(const char *name,
                       const unsigned char *first,
                       size_t first_length,
                       const unsigned char *second,
                       size_t second_length,
                       const char *start_text,
                       size_t argument_count,
                       RexxVMStatus expected_status,
                       size_t expected_result)
{
    RexxValue arguments[3] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    int unchanged;

    status = rexx_value_set_string(&arguments[0], (const char *)first,
                                   first_length);
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_value_set_string(&arguments[1], (const char *)second,
                                       second_length);
    }
    if (status == REXX_VM_STATUS_OK && argument_count >= 3u &&
        start_text != NULL) {
        status = rexx_value_set_string(&arguments[2], start_text,
                                       strlen(start_text));
    }
    if (status != REXX_VM_STATUS_OK) {
        check(0, "binary argument allocation succeeds");
        clear_values(arguments, 3u);
        return 1;
    }

    status = rexx_builtin_dispatch(name, strlen(name), arguments,
                                   argument_count, &result);
    check(status == expected_status,
          "binary INDEX/POS call has expected status");
    if (status == REXX_VM_STATUS_OK) {
        check(result_is_size(&result, expected_result),
              "binary INDEX/POS call has expected byte position");
    } else {
        check(result.type == REXX_VALUE_UNSET,
              "failed binary INDEX/POS call has no partial result");
    }
    unchanged = arguments[0].type == REXX_VALUE_STRING &&
                arguments[0].as.string.length == first_length &&
                (first_length == 0u ||
                 memcmp(arguments[0].as.string.bytes, first, first_length) == 0) &&
                arguments[1].type == REXX_VALUE_STRING &&
                arguments[1].as.string.length == second_length &&
                (second_length == 0u ||
                 memcmp(arguments[1].as.string.bytes, second, second_length) == 0);
    if (argument_count >= 3u && start_text != NULL) {
        size_t start_length = strlen(start_text);
        unchanged = unchanged && arguments[2].type == REXX_VALUE_STRING &&
                    arguments[2].as.string.length == start_length &&
                    memcmp(arguments[2].as.string.bytes, start_text,
                           start_length) == 0;
    }
    check(unchanged, "binary INDEX/POS leaves borrowed input bytes unchanged");
    clear_values(arguments, 3u);
    rexx_value_clear(&result);
    return status == expected_status ? 0 : 1;
}

static size_t independent_find(const unsigned char *haystack,
                               size_t haystack_length,
                               const unsigned char *needle,
                               size_t needle_length,
                               size_t start)
{
    size_t position;
    size_t last_start;

    if (start == 0u || start > haystack_length || needle_length == 0u ||
        needle_length > haystack_length - (start - 1u)) {
        return 0u;
    }
    last_start = haystack_length - needle_length;
    for (position = start - 1u; position <= last_start; ++position) {
        size_t offset;
        for (offset = 0u; offset < needle_length; ++offset) {
            if (haystack[position + offset] != needle[offset]) break;
        }
        if (offset == needle_length) return position + 1u;
        if (position == last_start) break;
    }
    return 0u;
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static int call_raw_search(const char *name,
                           const unsigned char *haystack,
                           size_t haystack_length,
                           const unsigned char *needle,
                           size_t needle_length,
                           size_t start,
                           size_t expected)
{
    RexxValue arguments[3] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxValue result = REXX_VALUE_INIT;
    char start_text[32];
    size_t first_length;
    size_t second_length;
    const unsigned char *first;
    const unsigned char *second;
    RexxVMStatus status;
    int inputs_unchanged;
    int passed;

    if (strcmp(name, "INDEX") == 0) {
        first = haystack;
        first_length = haystack_length;
        second = needle;
        second_length = needle_length;
    } else {
        first = needle;
        first_length = needle_length;
        second = haystack;
        second_length = haystack_length;
    }
    if (snprintf(start_text, sizeof(start_text), "%zu", start) <= 0) {
        check(0, "corpus start formats");
        return 1;
    }
    status = rexx_value_set_string(&arguments[0], (const char *)first,
                                   first_length);
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_value_set_string(&arguments[1], (const char *)second,
                                       second_length);
    }
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_value_set_string(&arguments[2], start_text,
                                       strlen(start_text));
    }
    if (status != REXX_VM_STATUS_OK) {
        check(0, "generated binary argument allocation succeeds");
        clear_values(arguments, 3u);
        return 1;
    }
    status = rexx_builtin_dispatch(name, strlen(name), arguments, 3u, &result);
    passed = status == REXX_VM_STATUS_OK && result_is_size(&result, expected);
    check(passed,
          "generated result matches independent byte-search oracle");
    inputs_unchanged = arguments[0].as.string.length == first_length &&
                       arguments[1].as.string.length == second_length &&
                       (first_length == 0u ||
                        memcmp(arguments[0].as.string.bytes, first,
                               first_length) == 0) &&
                       (second_length == 0u ||
                        memcmp(arguments[1].as.string.bytes, second,
                               second_length) == 0);
    check(inputs_unchanged, "generated search preserves input bytes");
    clear_values(arguments, 3u);
    rexx_value_clear(&result);
    return passed ? 0 : 1;
}

static int test_corpus(void)
{
    unsigned char haystack[RX26_MAX_HAYSTACK];
    unsigned char needle[RX26_MAX_NEEDLE];
    uint32_t state = UINT32_C(0x52583236);
    size_t case_index;

    for (case_index = 0u; case_index < RX26_CORPUS_CASES; ++case_index) {
        size_t haystack_length = (size_t)(next_random(&state) %
                                          (RX26_MAX_HAYSTACK + 1u));
        size_t needle_length = (size_t)(next_random(&state) %
                                        (RX26_MAX_NEEDLE + 1u));
        size_t start;
        size_t index;
        size_t expected;
        size_t insertion;
        unsigned int mode = (unsigned int)(case_index % 5u);

        for (index = 0u; index < haystack_length; ++index)
            haystack[index] = (unsigned char)next_random(&state);
        for (index = 0u; index < needle_length; ++index)
            needle[index] = (unsigned char)next_random(&state);

        if (mode == 0u && needle_length != 0u &&
            needle_length <= haystack_length) {
            insertion = (size_t)(next_random(&state) %
                                 (haystack_length - needle_length + 1u));
            memcpy(haystack + insertion, needle, needle_length);
        } else if (mode == 1u && haystack_length >= 9u) {
            static const unsigned char overlap_haystack[] = "ababababa";
            static const unsigned char overlap_needle[] = "aba";
            memcpy(haystack, overlap_haystack, sizeof(overlap_haystack) - 1u);
            haystack_length = sizeof(overlap_haystack) - 1u;
            memcpy(needle, overlap_needle, sizeof(overlap_needle) - 1u);
            needle_length = sizeof(overlap_needle) - 1u;
        } else if (mode == 2u && haystack_length >= 6u) {
            static const unsigned char repeated[] = "aaaaaa";
            memcpy(haystack, repeated, sizeof(repeated) - 1u);
            haystack_length = sizeof(repeated) - 1u;
            needle[0] = 'a';
            needle[1] = 'a';
            needle[2] = 'a';
            needle_length = 3u;
        }

        if (mode == 3u) {
            start = haystack_length + 1u + (size_t)(next_random(&state) % 3u);
        } else if (mode == 4u && haystack_length != 0u) {
            start = haystack_length;
        } else {
            start = (size_t)(next_random(&state) %
                             (haystack_length + 4u)) + 1u;
        }
        expected = independent_find(haystack, haystack_length, needle,
                                    needle_length, start);
        (void)call_raw_search("INDEX", haystack, haystack_length, needle,
                              needle_length, start, expected);
        (void)call_raw_search("POS", haystack, haystack_length, needle,
                              needle_length, start, expected);
    }
    return 0;
}

static int capture_output(void *context, const char *bytes, size_t length)
{
    (void)context;
    if (length > sizeof(captured_output) - captured_length) return 0;
    memcpy(captured_output + captured_length, bytes, length);
    captured_length += length;
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
    memset(&error, 0, sizeof(error));
    if (rexx_lexer_init(&lexer, source, strlen(source)) !=
        REXX_LEXER_STATUS_OK) {
        return NULL;
    }
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

#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
static int test_allocation_failure_and_recovery(void)
{
    static const char source[] = "say index('abc','b')\nexit 0\n";
    RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    RexxBytecodeChunk *chunk;
    RexxVM vm;
    RexxVMStatus status;
    size_t initial_live = rexx_vm_test_allocator_live_blocks();
    int failed = 0;

    status = rexx_value_set_string(&arguments[0], "abc", 3u);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_string(&arguments[1], "b", 1u);
    check(status == REXX_VM_STATUS_OK,
          "allocation-failure input strings are ready before failpoint");
    if (status == REXX_VM_STATUS_OK) {
        rexx_vm_test_allocator_fail_on(1u);
        status = rexx_builtin_dispatch("INDEX", 5u, arguments, 2u, &result);
        rexx_vm_test_allocator_disable();
        check(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
              result.type == REXX_VALUE_UNSET,
              "INDEX result allocation failure leaves no partial result");
        check(arguments[0].as.string.length == 3u &&
              memcmp(arguments[0].as.string.bytes, "abc", 3u) == 0 &&
              arguments[1].as.string.length == 1u &&
              memcmp(arguments[1].as.string.bytes, "b", 1u) == 0,
              "INDEX result allocation failure leaves input strings unchanged");
        status = rexx_builtin_dispatch("INDEX", 5u, arguments, 2u, &result);
        check(status == REXX_VM_STATUS_OK && result_is_size(&result, 2u),
              "INDEX direct call succeeds after output allocation recovery");
        rexx_value_clear(&result);
    }
    clear_values(arguments, 2u);

    chunk = compile_source(source);
    check(chunk != NULL, "INDEX allocation-recovery script compiles");
    if (chunk == NULL) return 1;
    status = rexx_vm_init(&vm, 0u);
    check(status == REXX_VM_STATUS_OK, "INDEX recovery VM initializes");
    if (status != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return 1;
    }
    rexx_vm_set_output_writer(&vm, capture_output, NULL);
    {
        size_t failpoint;
        size_t recovered = 0u;
        for (failpoint = 1u; failpoint <= 32u; ++failpoint) {
            captured_length = 0u;
            memset(captured_output, 0, sizeof(captured_output));
            rexx_vm_test_allocator_fail_on(failpoint);
            status = rexx_vm_execute(&vm, chunk);
            rexx_vm_test_allocator_disable();
            if (status == REXX_VM_STATUS_OUT_OF_MEMORY) {
                ++recovered;
                captured_length = 0u;
                memset(captured_output, 0, sizeof(captured_output));
                status = rexx_vm_execute(&vm, chunk);
                check(status == REXX_VM_STATUS_OK && captured_length == 2u &&
                      memcmp(captured_output, "2\n", 2u) == 0,
                      "a valid INDEX script succeeds after VM allocation failure");
            } else {
                check(status == REXX_VM_STATUS_OK,
                      "VM allocation failpoint either fails cleanly or succeeds");
            }
        }
        check(recovered != 0u,
              "INDEX VM failpoint sweep includes allocation failures");
    }
    rexx_vm_test_allocator_disable();
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    check(rexx_vm_test_allocator_live_blocks() == initial_live,
          "INDEX allocation failures release all owned values");
    return failed;
}
#endif

static int test_large_buffer(void)
{
    unsigned char *haystack = (unsigned char *)malloc(RX26_LARGE_STRING);
    static const unsigned char tail[] = { 'T', 'A', 'I', 'L' };
    int failed = 0;

    if (haystack == NULL) {
        check(0, "16 MiB boundary buffer allocates in the hosted test");
        return 1;
    }
    memset(haystack, 0xCC, RX26_LARGE_STRING);
    memcpy(haystack + RX26_LARGE_STRING - sizeof(tail), tail, sizeof(tail));
    failed += call_binary("INDEX", haystack, RX26_LARGE_STRING,
                          tail, sizeof(tail), "16777213", 3u,
                          REXX_VM_STATUS_OK, 16777213u);
    failed += call_binary("INDEX", haystack, RX26_LARGE_STRING,
                          tail, sizeof(tail), "16777216", 3u,
                          REXX_VM_STATUS_OK, 0u);
    failed += call_binary("POS", tail, sizeof(tail),
                          haystack, RX26_LARGE_STRING, "16777213", 3u,
                          REXX_VM_STATUS_OK, 16777213u);
    free(haystack);
    return failed;
}

int main(void)
{
    static const unsigned char binary_haystack_1[] = { 0x00u, 0xFFu, 0x80u };
    static const unsigned char binary_needle_1[] = { 0xFFu };
    static const unsigned char binary_haystack_2[] = { 'A', 'B', 0x00u, 'A' };
    static const unsigned char binary_needle_2[] = { 0x00u, 'A' };
    static const unsigned char binary_haystack_3[] = { 0x00u, 0x00u, 0x00u };
    static const unsigned char binary_needle_3[] = { 0x00u, 0x00u };
    unsigned char all_bytes[256];
    unsigned char expected_end = 0xFFu;
    size_t index;

    /* IBM TSO/E / VM INDEX reference examples. */
    (void)call_text("INDEX", "abcdef", "cd", NULL, 2u, REXX_VM_STATUS_OK, 3u);
    (void)call_text("INDEX", "abcdef", "xd", NULL, 2u, REXX_VM_STATUS_OK, 0u);
    (void)call_text("INDEX", "abcdef", "bc", "3", 3u, REXX_VM_STATUS_OK, 0u);
    (void)call_text("INDEX", "abcabc", "bc", "3", 3u, REXX_VM_STATUS_OK, 5u);
    (void)call_text("INDEX", "abcabc", "bc", "6", 3u, REXX_VM_STATUS_OK, 0u);

    /* Independent expected-result vectors and edge positions. */
    (void)call_text("INDEX", "abc", "a", NULL, 2u, REXX_VM_STATUS_OK, 1u);
    (void)call_text("INDEX", "abc", "c", NULL, 2u, REXX_VM_STATUS_OK, 3u);
    (void)call_text("INDEX", "abc", "a", "2", 3u, REXX_VM_STATUS_OK, 0u);
    (void)call_text("INDEX", "ababa", "aba", NULL, 2u, REXX_VM_STATUS_OK, 1u);
    (void)call_text("INDEX", "ababa", "aba", "2", 3u, REXX_VM_STATUS_OK, 3u);
    (void)call_text("INDEX", "abc", "", NULL, 2u, REXX_VM_STATUS_OK, 0u);
    (void)call_text("INDEX", "", "a", NULL, 2u, REXX_VM_STATUS_OK, 0u);
    (void)call_text("INDEX", "", "", NULL, 2u, REXX_VM_STATUS_OK, 0u);
    (void)call_text("INDEX", "abc", "a", "99", 3u, REXX_VM_STATUS_OK, 0u);
    (void)call_text("INDEX", "abc", "a", "1", 3u, REXX_VM_STATUS_OK, 1u);
    (void)call_text("INDEX", "abc", "c", "3", 3u, REXX_VM_STATUS_OK, 3u);
    (void)call_text("INDEX", "abc", "c", "4", 3u, REXX_VM_STATUS_OK, 0u);
    (void)call_text("INDEX", "abc", "a", NULL, 3u, REXX_VM_STATUS_OK, 1u);

    /* Supported exact whole-number spellings and values larger than uint32. */
    (void)call_text("INDEX", "abc", "c", "+3", 3u, REXX_VM_STATUS_OK, 3u);
    (void)call_text("INDEX", "abc", "c", "3.0", 3u, REXX_VM_STATUS_OK, 3u);
    (void)call_text("INDEX", "abc", "c", "3E0", 3u, REXX_VM_STATUS_OK, 3u);
    (void)call_text("INDEX", "abc", "a", "4294967296", 3u,
                    REXX_VM_STATUS_OK, 0u);
    (void)call_text("INDEX", "abc", "a", "1234567890123456789012345", 3u,
                    REXX_VM_STATUS_OK, 0u);
    (void)call_text("INDEX", "abc", "a", "1E100", 3u,
                    REXX_VM_STATUS_OK, 0u);
    /* The large-start correction is shared, and therefore also regression
     * tested through portable POS. */
    (void)call_text("POS", "a", "abc", "4294967296", 3u,
                    REXX_VM_STATUS_OK, 0u);

    /* Invalid argument counts, missing required values, and bad starts. */
    (void)call_text("INDEX", NULL, "a", NULL, 2u,
                    REXX_VM_STATUS_INVALID_FUNCTION_CALL, 0u);
    (void)call_text("INDEX", "abc", NULL, NULL, 2u,
                    REXX_VM_STATUS_INVALID_FUNCTION_CALL, 0u);
    (void)call_text("INDEX", "abc", "a", "0", 3u,
                    REXX_VM_STATUS_INVALID_FUNCTION_CALL, 0u);
    (void)call_text("INDEX", "abc", "a", "-1", 3u,
                    REXX_VM_STATUS_INVALID_FUNCTION_CALL, 0u);
    (void)call_text("INDEX", "abc", "a", "1.5", 3u,
                    REXX_VM_STATUS_INVALID_FUNCTION_CALL, 0u);
    (void)call_text("INDEX", "abc", "a", "n/a", 3u,
                    REXX_VM_STATUS_NUMERIC_ERROR, 0u);
    (void)call_text("INDEX", "abc", "a", "", 3u,
                    REXX_VM_STATUS_NUMERIC_ERROR, 0u);
    (void)call_text("INDEX", "abc", "a", "2", 4u,
                    REXX_VM_STATUS_INVALID_FUNCTION_CALL, 0u);
    (void)call_text("INDEX", "abc", "a", NULL, 3u,
                    REXX_VM_STATUS_OK, 1u);

    (void)call_binary("INDEX", binary_haystack_1, sizeof(binary_haystack_1),
                      binary_needle_1, sizeof(binary_needle_1), NULL, 2u,
                      REXX_VM_STATUS_OK, 2u);
    (void)call_binary("INDEX", binary_haystack_2, sizeof(binary_haystack_2),
                      binary_needle_2, sizeof(binary_needle_2), NULL, 2u,
                      REXX_VM_STATUS_OK, 3u);
    (void)call_binary("INDEX", binary_haystack_3, sizeof(binary_haystack_3),
                      binary_needle_3, sizeof(binary_needle_3), "2", 3u,
                      REXX_VM_STATUS_OK, 2u);
    for (index = 0u; index < sizeof(all_bytes); ++index)
        all_bytes[index] = (unsigned char)index;
    (void)call_binary("INDEX", all_bytes, sizeof(all_bytes), &expected_end, 1u,
                      NULL, 2u, REXX_VM_STATUS_OK, 256u);
    (void)call_binary("POS", &expected_end, 1u, all_bytes, sizeof(all_bytes),
                      "256", 3u, REXX_VM_STATUS_OK, 256u);

    (void)test_large_buffer();
    (void)test_corpus();

#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
    (void)test_allocation_failure_and_recovery();
#endif

    printf("RX26 INDEX assertions: %zu; failures: %zu; deterministic equivalence cases: %u; seed: 0x%08X\n",
           assertions, failures, RX26_CORPUS_CASES, UINT32_C(0x52583236));
    return failures == 0u ? 0 : 1;
}
