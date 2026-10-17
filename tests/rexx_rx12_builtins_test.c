#include "../rexx_builtin.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_compiler.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <string.h>

typedef struct BuiltinCase {
    const char *name;
    const char *arguments[4]; /* NULL means an omitted positional argument. */
    size_t argument_count;
    const char *expected;
} BuiltinCase;

typedef struct ErrorCase {
    const char *name;
    const char *arguments[4];
    size_t argument_count;
    RexxVMStatus expected_status;
} ErrorCase;

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

static int run_builtin_case(const BuiltinCase *test)
{
    RexxValue arguments[4] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    RexxValue result = REXX_VALUE_INIT;
    size_t index;
    RexxVMStatus status = REXX_VM_STATUS_OK;
    int failed = 0;
    char description[160];

    for (index = 0u; index < test->argument_count; ++index) {
        if (test->arguments[index] != NULL) {
            status = rexx_value_set_string(&arguments[index],
                                           test->arguments[index],
                                           strlen(test->arguments[index]));
            if (status != REXX_VM_STATUS_OK) {
                failed += check(0, "test argument allocation succeeds");
                clear_values(arguments, 4u);
                return failed;
            }
        }
    }
    status = rexx_builtin_dispatch(test->name, strlen(test->name),
                                   test->argument_count == 0u ? NULL : arguments,
                                   test->argument_count, &result);
    (void)snprintf(description, sizeof(description), "%s returns expected bytes",
                   test->name);
    failed += check(status == REXX_VM_STATUS_OK &&
                    result.type == REXX_VALUE_STRING &&
                    result.as.string.length == strlen(test->expected) &&
                    memcmp(result.as.string.bytes, test->expected,
                           strlen(test->expected)) == 0,
                    description);
    if (status != REXX_VM_STATUS_OK || result.type != REXX_VALUE_STRING ||
        result.as.string.length != strlen(test->expected) ||
        memcmp(result.as.string.bytes, test->expected,
               strlen(test->expected)) != 0) {
        fprintf(stderr, "  %s expected [%s], status=%s, actual=[%.*s]\n",
                test->name, test->expected, rexx_vm_status_name(status),
                result.type == REXX_VALUE_STRING
                    ? (int)result.as.string.length : 0,
                result.type == REXX_VALUE_STRING
                    ? result.as.string.bytes : "");
    }
    clear_values(arguments, 4u);
    rexx_value_clear(&result);
    return failed;
}

static int run_error_case(const ErrorCase *test)
{
    RexxValue arguments[4] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
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
                failed = check(0, "error test argument allocation succeeds");
                clear_values(arguments, 4u);
                return failed;
            }
        }
    }
    status = rexx_builtin_dispatch(test->name, strlen(test->name),
                                   test->argument_count == 0u ? NULL : arguments,
                                   test->argument_count, &result);
    (void)snprintf(description, sizeof(description), "%s rejects malformed arguments",
                   test->name);
    failed = check(status == test->expected_status &&
                   result.type == REXX_VALUE_UNSET, description);
    clear_values(arguments, 4u);
    rexx_value_clear(&result);
    return failed;
}

static int test_binary_strings(void)
{
    const char bytes[] = { 'A', '\0', 'b' };
    RexxValue argument = REXX_VALUE_INIT;
    RexxValue result = REXX_VALUE_INIT;
    int failed = 0;
    RexxVMStatus status;

    status = rexx_value_set_string(&argument, bytes, sizeof(bytes));
    failed += check(status == REXX_VM_STATUS_OK,
                    "embedded-zero argument is created with its explicit length");
    status = rexx_builtin_dispatch("LENGTH", 6u, &argument, 1u, &result);
    failed += check(status == REXX_VM_STATUS_OK &&
                    result.as.string.length == 1u &&
                    memcmp(result.as.string.bytes, "3", 1u) == 0,
                    "LENGTH counts bytes after an embedded zero");
    rexx_value_clear(&result);

    status = rexx_builtin_dispatch("COMPARE", 7u,
                                   (RexxValue[]){ argument, argument }, 2u,
                                   &result);
    failed += check(status == REXX_VM_STATUS_OK &&
                    result.as.string.length == 1u &&
                    result.as.string.bytes[0] == '0',
                    "COMPARE compares the complete explicit byte range");
    rexx_value_clear(&result);
    rexx_value_clear(&argument);
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

static int run_program_with_invocation(const char *source,
                                       int has_argument_string,
                                       const char *argument_string,
                                       size_t argument_string_length,
                                       const char *expected_output,
                                       const char *description)
{
    RexxVM vm;
    RexxBytecodeChunk *chunk = compile_source(source);
    RexxVMStatus status;
    int failed = 0;

    if (chunk == NULL) return check(0, "RX12 integration source compiles");
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return check(0, "RX12 integration VM initializes");
    }
    status = REXX_VM_STATUS_OK;
    if (has_argument_string) {
        status = rexx_vm_set_argument_string(&vm, argument_string,
                                             argument_string_length);
    }
    output_length = 0u;
    memset(output, 0, sizeof(output));
    rexx_vm_set_output_writer(&vm, capture_output, NULL);
    if (status == REXX_VM_STATUS_OK) status = rexx_vm_execute(&vm, chunk);
    failed += check(status == REXX_VM_STATUS_OK &&
                    output_length == strlen(expected_output) &&
                    memcmp(output, expected_output, output_length) == 0,
                    description);
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    return failed;
}

static int run_program(const char *source, const char *expected_output,
                       const char *description)
{
    return run_program_with_invocation(source, 0, NULL, 0u,
                                       expected_output, description);
}

static int run_program_expect_status(const char *source,
                                     RexxVMStatus expected_status,
                                     const char *description)
{
    RexxVM vm;
    RexxBytecodeChunk *chunk = compile_source(source);
    RexxVMStatus status;
    int failed;
    if (chunk == NULL) return check(0, "RX12 error integration source compiles");
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return check(0, "RX12 error integration VM initializes");
    }
    status = rexx_vm_execute(&vm, chunk);
    failed = check(status == expected_status, description);
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    return failed;
}

static int test_arg_and_integration(void)
{
    static const char omitted_source[] =
        "call inspect 'alpha',,''\n"
        "exit 0\n"
        "inspect: procedure\n"
        "  say arg()\n"
        "  say arg(1)\n"
        "  say arg(2,'O')\n"
        "  say arg(2,'exists')\n"
        "  say arg(3,'O')\n"
        "  say arg(3,'E')\n"
        "  say arg(4)\n"
        "  code = 'say wordpos(substr(arg(1),1,5),\"alpha beta\")'\n"
        "  interpret code\n"
        "  return\n";
    static const char empty_call_source[] =
        "call inspect\n"
        "exit 0\n"
        "inspect: procedure\n"
        "  say arg()\n"
        "  say arg(1,'O')\n"
        "  return\n";
    static const char function_source[] =
        "say inspect('x',,'z')\n"
        "exit 0\n"
        "inspect: procedure\n"
        "  return arg()||':'||arg(1)||':'||arg(2,'O')||':'||arg(3)\n";
    static const char collision_source[] =
        "say length('abc')\n"
        "exit 0\n"
        "length: procedure\n"
        "  return arg(1)||'!'\n";
    static const char top_level_source[] =
        "say arg()\n"
        "say arg(1,'E')\n"
        "say arg(1,'O')\n"
        "say '['||arg(1)||']'\n";
    static const char invalid_option_source[] =
        "call inspect 'x'\n"
        "exit 0\n"
        "inspect: procedure\n"
        "  say arg(1,'Q')\n"
        "  return\n";
    static const char invalid_position_source[] =
        "call inspect 'x'\n"
        "exit 0\n"
        "inspect: procedure\n"
        "  say arg(0)\n"
        "  return\n";
    static const char omitted_position_source[] =
        "call inspect 'x'\n"
        "exit 0\n"
        "inspect: procedure\n"
        "  say arg(,'E')\n"
        "  return\n";
    int failed = 0;

    failed += run_program(omitted_source,
                          "3\nalpha\n1\n0\n0\n1\n\n1\n",
                          "ARG preserves holes, explicit empty values, and INTERPRET context");
    failed += run_program(empty_call_source, "0\n1\n",
                          "ARG reports zero positions and an omitted first argument");
    failed += run_program(function_source, "3:x:1:z\n",
                          "ARG works in an internal function frame");
    failed += run_program(collision_source, "abc!\n",
                          "an internal routine name continues to shadow a built-in");
    failed += run_program(top_level_source, "0\n0\n1\n[]\n",
                          "ARG distinguishes a top-level program with no argument string");
    failed += run_program_with_invocation(
        top_level_source, 1, "", 0u, "1\n1\n0\n[]\n",
        "ARG preserves an explicitly supplied empty top-level argument string");
    failed += run_program_with_invocation(
        top_level_source, 1, "one two", 7u, "1\n1\n0\n[one two]\n",
        "ARG treats the command tail as one top-level argument string");
    failed += run_program_expect_status(
        invalid_option_source, REXX_VM_STATUS_INVALID_FUNCTION_CALL,
        "ARG rejects option values outside the E/O family");
    failed += run_program_expect_status(
        invalid_position_source, REXX_VM_STATUS_INVALID_FUNCTION_CALL,
        "ARG rejects a zero argument position");
    failed += run_program_expect_status(
        omitted_position_source, REXX_VM_STATUS_INVALID_FUNCTION_CALL,
        "ARG rejects an omitted required position argument");
    return failed;
}

#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
static int test_allocation_failure(void)
{
    static const char *const names[] = { "COPIES", "SPACE", "TRANSLATE", "SUBSTR" };
    static const char *const outputs[] = { "xyxyxy", "a b", "ABC", "bc  " };
    const size_t initial_live = rexx_vm_test_allocator_live_blocks();
    size_t case_index;
    int failed = 0;

    for (case_index = 0u; case_index < sizeof(names) / sizeof(names[0]);
         ++case_index) {
        RexxValue arguments[4] = {
            REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
        };
        RexxValue result = REXX_VALUE_INIT;
        size_t count = 2u;
        RexxVMStatus status;
        if (case_index == 1u) {
            (void)rexx_value_set_string(&arguments[0], "a  b", 4u);
            (void)rexx_value_set_string(&arguments[1], "1", 1u);
            count = 2u;
        } else if (case_index == 2u) {
            (void)rexx_value_set_string(&arguments[0], "abc", 3u);
            count = 1u;
        } else if (case_index == 3u) {
            (void)rexx_value_set_string(&arguments[0], "abc", 3u);
            (void)rexx_value_set_string(&arguments[1], "2", 1u);
            (void)rexx_value_set_string(&arguments[2], "4", 1u);
            count = 3u;
        } else {
            (void)rexx_value_set_string(&arguments[0], "xy", 2u);
            (void)rexx_value_set_string(&arguments[1], "3", 1u);
        }

        rexx_vm_test_allocator_fail_on(1u);
        status = rexx_builtin_dispatch(names[case_index], strlen(names[case_index]),
                                       arguments, count, &result);
        rexx_vm_test_allocator_disable();
        failed += check(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
                        result.type == REXX_VALUE_UNSET &&
                        arguments[0].type == REXX_VALUE_STRING,
                        "built-in output allocation failure leaves inputs intact");
        status = rexx_builtin_dispatch(names[case_index], strlen(names[case_index]),
                                       arguments, count, &result);
        failed += check(status == REXX_VM_STATUS_OK &&
                        result.as.string.length == strlen(outputs[case_index]) &&
                        memcmp(result.as.string.bytes, outputs[case_index],
                               strlen(outputs[case_index])) == 0,
                        "built-in succeeds after allocation failure is disabled");
        clear_values(arguments, 4u);
        rexx_value_clear(&result);
    }
    failed += check(rexx_vm_test_allocator_live_blocks() == initial_live,
                    "built-in allocation failures release all owned buffers");
    return failed;
}

static int test_vm_recovery_after_allocation_failure(void)
{
    static const char source[] = "say copies('x',2)\nexit 0\n";
    RexxBytecodeChunk *chunk = compile_source(source);
    RexxVM vm;
    size_t failpoint;
    size_t recovered = 0u;
    int failed = 0;

    if (chunk == NULL) return check(0, "allocation recovery source compiles");
    if (rexx_vm_init(&vm, 0u) != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return check(0, "allocation recovery VM initializes");
    }
    rexx_vm_set_output_writer(&vm, capture_output, NULL);
    for (failpoint = 1u; failpoint <= 12u; ++failpoint) {
        RexxVMStatus status;
        output_length = 0u;
        memset(output, 0, sizeof(output));
        rexx_vm_test_allocator_fail_on(failpoint);
        status = rexx_vm_execute(&vm, chunk);
        rexx_vm_test_allocator_disable();
        if (status == REXX_VM_STATUS_OUT_OF_MEMORY) {
            ++recovered;
            output_length = 0u;
            memset(output, 0, sizeof(output));
            status = rexx_vm_execute(&vm, chunk);
            failed += check(status == REXX_VM_STATUS_OK &&
                            output_length == 3u &&
                            memcmp(output, "xx\n", 3u) == 0,
                            "VM executes the same BIF program after allocation failure");
        } else {
            failed += check(status == REXX_VM_STATUS_OK,
                            "allocation failpoint either fails cleanly or executes");
        }
    }
    failed += check(recovered != 0u,
                    "RX12 allocation failpoints exercise VM recovery");
    rexx_vm_test_allocator_disable();
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    return failed;
}
#endif

int main(void)
{
    static const BuiltinCase cases[] = {
        /* LENGTH: byte length, including all supported blank and control bytes. */
        { "length", { "hello" }, 1u, "5" },
        { "LENGTH", { "" }, 1u, "0" },
        { "LENGTH", { " " }, 1u, "1" },
        { "LENGTH", { "    " }, 1u, "4" },
        { "LENGTH", { "a b" }, 1u, "3" },
        { "LENGTH", { "a\tb" }, 1u, "3" },
        { "LENGTH", { "a\nb" }, 1u, "3" },
        { "LENGTH", { "a\vb" }, 1u, "3" },
        { "LENGTH", { "a\fb" }, 1u, "3" },
        { "LENGTH", { "a\rb" }, 1u, "3" },
        { "LENGTH", { "1234567890" }, 1u, "10" },
        { "LENGTH", { "punctuation!?" }, 1u, "13" },
        { "LENGTH", { "lower UPPER" }, 1u, "11" },
        { "LENGTH", { "one\t two" }, 1u, "8" },
        { "LENGTH", { "line\r\nbreak" }, 1u, "11" },
        { "LENGTH", { "   trailing " }, 1u, "12" },
        { "LENGTH", { "leading   " }, 1u, "10" },
        { "LENGTH", { "0000" }, 1u, "4" },
        { "LENGTH", { "a-b_c" }, 1u, "5" },
        { "LENGTH", { "x\t\r\ny" }, 1u, "5" },

        /* SUBSTR: omitted length, explicit zero, pad and out-of-range starts. */
        { "SUBSTR", { "abcdef", "1" }, 2u, "abcdef" },
        { "SUBSTR", { "abcdef", "2" }, 2u, "bcdef" },
        { "SUBSTR", { "abcdef", "6" }, 2u, "f" },
        { "SUBSTR", { "abcdef", "7" }, 2u, "" },
        { "SUBSTR", { "abcdef", "2", "3" }, 3u, "bcd" },
        { "SUBSTR", { "abcdef", "2", "0" }, 3u, "" },
        { "SUBSTR", { "abc", "2", "4" }, 3u, "bc  " },
        { "SUBSTR", { "abc", "2", "4", "." }, 4u, "bc.." },
        { "SUBSTR", { "abc", "4", "3", "." }, 4u, "..." },
        { "SUBSTR", { "abc", "5", "2", "x" }, 4u, "xx" },
        { "SUBSTR", { "abc", "3", "3" }, 3u, "c  " },
        { "SUBSTR", { "abc", "1", "1", NULL }, 4u, "a" },
        { "SUBSTR", { "abc", "2", NULL }, 3u, "bc" },
        { "SUBSTR", { "", "1", "3", "." }, 4u, "..." },
        { "SUBSTR", { "", "1" }, 2u, "" },
        { "SUBSTR", { "abc", "2", "1", "0" }, 4u, "b" },
        { "SUBSTR", { "12345", "3", "2" }, 3u, "34" },
        { "SUBSTR", { "abc", "3", "0", "." }, 4u, "" },
        { "SUBSTR", { "abc", "1", "5", " " }, 4u, "abc  " },
        { "SUBSTR", { "abc", "100", "2", "!" }, 4u, "!!" },

        /* POS: case-sensitive byte search, one-based position, empty needle. */
        { "POS", { "a", "abc" }, 2u, "1" },
        { "POS", { "bc", "abc" }, 2u, "2" },
        { "POS", { "bc", "abcabc" }, 2u, "2" },
        { "POS", { "bc", "abcabc", "3" }, 3u, "5" },
        { "POS", { "bc", "abcabc", "6" }, 3u, "0" },
        { "POS", { "x", "abc" }, 2u, "0" },
        { "POS", { "A", "aA" }, 2u, "2" },
        { "POS", { "A", "a" }, 2u, "0" },
        { "POS", { "", "abc" }, 2u, "0" },
        { "POS", { "", "", "1" }, 3u, "0" },
        { "POS", { "a", "" }, 2u, "0" },
        { "POS", { " ", "abc def" }, 2u, "4" },
        { "POS", { " ", "abc def", "5" }, 3u, "0" },
        { "POS", { "def", "abcdef", "4" }, 3u, "4" },
        { "POS", { "def", "abcdef", "5" }, 3u, "0" },
        { "POS", { "a", "a", "1" }, 3u, "1" },
        { "POS", { "a", "a", "2" }, 3u, "0" },
        { "POS", { "bc", "abcabc", NULL }, 3u, "2" },
        { "POS", { "x", "xyz" }, 2u, "1" },
        { "POS", { "yz", "xyz" }, 2u, "2" },

        /* WORDS: canonical ASCII REXX blank class and repeated separators. */
        { "WORDS", { "one two three" }, 1u, "3" },
        { "WORDS", { "" }, 1u, "0" },
        { "WORDS", { " " }, 1u, "0" },
        { "WORDS", { "      " }, 1u, "0" },
        { "WORDS", { " one" }, 1u, "1" },
        { "WORDS", { "one " }, 1u, "1" },
        { "WORDS", { "  one  two  " }, 1u, "2" },
        { "WORDS", { "one\ttwo" }, 1u, "2" },
        { "WORDS", { "one\ntwo" }, 1u, "2" },
        { "WORDS", { "one\vtwo" }, 1u, "2" },
        { "WORDS", { "one\ftwo" }, 1u, "2" },
        { "WORDS", { "one\rtwo" }, 1u, "2" },
        { "WORDS", { "one\t \r\ntwo\vthree" }, 1u, "3" },
        { "WORDS", { "single" }, 1u, "1" },
        { "WORDS", { "a  b   c    d" }, 1u, "4" },
        { "WORDS", { "a\tb\nc" }, 1u, "3" },
        { "WORDS", { "  a  b  c  " }, 1u, "3" },
        { "WORDS", { "one\ftwo\rthree" }, 1u, "3" },
        { "WORDS", { "  \t\r\n " }, 1u, "0" },
        { "WORDS", { "A B C D E" }, 1u, "5" },

        /* WORD: extraction preserves word bytes; missing words return null. */
        { "WORD", { "one two three", "1" }, 2u, "one" },
        { "WORD", { "one two three", "2" }, 2u, "two" },
        { "WORD", { "one two three", "3" }, 2u, "three" },
        { "WORD", { "one two three", "4" }, 2u, "" },
        { "WORD", { "", "1" }, 2u, "" },
        { "WORD", { "  alpha   beta ", "2" }, 2u, "beta" },
        { "WORD", { "\tone\ttwo", "1" }, 2u, "one" },
        { "WORD", { "one\ntwo", "2" }, 2u, "two" },
        { "WORD", { "word", "1" }, 2u, "word" },
        { "WORD", { "  x  y  z  ", "3" }, 2u, "z" },
        { "WORD", { "a b c d", "2" }, 2u, "b" },
        { "WORD", { "a b c d", "3" }, 2u, "c" },
        { "WORD", { "a b c d", "4" }, 2u, "d" },
        { "WORD", { "a b", "999999999999" }, 2u, "" },
        { "WORD", { "one two", "2.0" }, 2u, "two" },
        { "WORD", { "  long-word  end", "1" }, 2u, "long-word" },
        { "WORD", { "\fA\vB\rC", "2" }, 2u, "B" },
        { "WORD", { "same SAME", "2" }, 2u, "SAME" },
        { "WORD", { "same SAME", "1" }, 2u, "same" },
        { "WORD", { "a\t\tb", "2" }, 2u, "b" },

        /* WORDPOS: phrases are exact sequences of complete, case-sensitive words. */
        { "WORDPOS", { "is the", "now is the time" }, 2u, "2" },
        { "WORDPOS", { "the", "now is the time" }, 2u, "3" },
        { "WORDPOS", { "The", "now is the time" }, 2u, "0" },
        { "WORDPOS", { "is   the", "now is the time" }, 2u, "2" },
        { "WORDPOS", { "is the", "now is   the time" }, 2u, "2" },
        { "WORDPOS", { "is time", "now is the time" }, 2u, "0" },
        { "WORDPOS", { "be", "To be or not to be" }, 2u, "2" },
        { "WORDPOS", { "be", "To be or not to be", "3" }, 3u, "6" },
        { "WORDPOS", { "a a", "a a a" }, 2u, "1" },
        { "WORDPOS", { "a a", "a a a", "2" }, 3u, "2" },
        { "WORDPOS", { "a a", "a a a", "3" }, 3u, "0" },
        { "WORDPOS", { "cat", "concatenate cat" }, 2u, "2" },
        { "WORDPOS", { "cat", "concatenate" }, 2u, "0" },
        { "WORDPOS", { "the time", "the time then the time" }, 2u, "1" },
        { "WORDPOS", { "the time", "the time then the time", "2" }, 3u, "4" },
        { "WORDPOS", { "", "one two" }, 2u, "0" },
        { "WORDPOS", { "  \t  ", "one two" }, 2u, "0" },
        { "WORDPOS", { " one  two ", "zero one\t two end" }, 2u, "2" },
        { "WORDPOS", { "one", "ONE one" }, 2u, "2" },
        { "WORDPOS", { "x y", "x  y z" }, 2u, "1" },
        { "WORDPOS", { "x z", "x  y z" }, 2u, "0" },
        { "WORDPOS", { "word", "word word word", "2" }, 3u, "2" },
        { "WORDPOS", { "word", "word word word", "4" }, 3u, "0" },
        { "WORDPOS", { "a b c", "x a b c y" }, 2u, "2" },
        { "WORDPOS", { "b c", "x a b c y", "3" }, 3u, "3" },

        /* STRIP: options use their first character; supplied characters are exact bytes. */
        { "STRIP", { "  abc  " }, 1u, "abc" },
        { "STRIP", { "  abc  ", "L" }, 2u, "abc  " },
        { "STRIP", { "  abc  ", "T" }, 2u, "  abc" },
        { "STRIP", { "  abc  ", "B" }, 2u, "abc" },
        { "STRIP", { "  abc  ", "lEADING" }, 2u, "abc  " },
        { "STRIP", { "  abc  ", "trailing" }, 2u, "  abc" },
        { "STRIP", { "  abc  ", "both" }, 2u, "abc" },
        { "STRIP", { "xxabcxx", "B", "x" }, 3u, "abc" },
        { "STRIP", { "xxabcxx", "L", "x" }, 3u, "abcxx" },
        { "STRIP", { "xxabcxx", "T", "x" }, 3u, "xxabc" },
        { "STRIP", { "0012.700", NULL, "0" }, 3u, "12.7" },
        { "STRIP", { "000", NULL, "0" }, 3u, "" },
        { "STRIP", { "abc", NULL, "x" }, 3u, "abc" },
        { "STRIP", { "xxxabcxxx", "B", "x" }, 3u, "abc" },
        { "STRIP", { "  abc", "L" }, 2u, "abc" },
        { "STRIP", { "abc  ", "T" }, 2u, "abc" },
        { "STRIP", { "", "B" }, 2u, "" },
        { "STRIP", { "     ", "L" }, 2u, "" },
        { "STRIP", { "     ", "T" }, 2u, "" },
        { "STRIP", { "ababxaba", "B", "a" }, 3u, "babxab" },

        /* SPACE: trim delimiters and normalize each internal gap. */
        { "SPACE", { "abc def" }, 1u, "abc def" },
        { "SPACE", { "  abc   def  " }, 1u, "abc def" },
        { "SPACE", { "abc  def", "0" }, 2u, "abcdef" },
        { "SPACE", { "abc  def", "2" }, 2u, "abc  def" },
        { "SPACE", { "abc  def", "3" }, 2u, "abc   def" },
        { "SPACE", { "abc  def", "2", "+" }, 3u, "abc++def" },
        { "SPACE", { "  one   two three  ", "0" }, 2u, "onetwothree" },
        { "SPACE", { "one\ttwo" }, 1u, "one two" },
        { "SPACE", { "one\ntwo" }, 1u, "one two" },
        { "SPACE", { "one\vtwo" }, 1u, "one two" },
        { "SPACE", { "one\ftwo" }, 1u, "one two" },
        { "SPACE", { "one\rtwo" }, 1u, "one two" },
        { "SPACE", { "one\t \r\ntwo", "2", "." }, 3u, "one..two" },
        { "SPACE", { "", "2", "." }, 3u, "" },
        { "SPACE", { "   ", "1" }, 2u, "" },
        { "SPACE", { "solo", "4", "." }, 3u, "solo" },
        { "SPACE", { "  a  b  ", NULL, "+" }, 3u, "a+b" },
        { "SPACE", { "a  b", NULL }, 2u, "a b" },
        { "SPACE", { "a b c", "0", "." }, 3u, "abc" },
        { "SPACE", { "a  b  c", "2", "_" }, 3u, "a__b__c" },

        /* COPIES: count is nonnegative and the entire byte string repeats. */
        { "COPIES", { "abc", "3" }, 2u, "abcabcabc" },
        { "COPIES", { "abc", "0" }, 2u, "" },
        { "COPIES", { "abc", "1" }, 2u, "abc" },
        { "COPIES", { "x", "5" }, 2u, "xxxxx" },
        { "COPIES", { "xy", "2" }, 2u, "xyxy" },
        { "COPIES", { "", "4" }, 2u, "" },
        { "COPIES", { " ", "3" }, 2u, "   " },
        { "COPIES", { "REXX", "2" }, 2u, "REXXREXX" },
        { "COPIES", { "a b", "2" }, 2u, "a ba b" },
        { "COPIES", { "!", "1" }, 2u, "!" },
        { "COPIES", { "abcdef", "2" }, 2u, "abcdefabcdef" },
        { "COPIES", { "12", "4" }, 2u, "12121212" },
        { "COPIES", { "A", "8" }, 2u, "AAAAAAAA" },
        { "COPIES", { "ab", "0" }, 2u, "" },
        { "COPIES", { "q", "10" }, 2u, "qqqqqqqqqq" },
        { "COPIES", { "abc", "2.0" }, 2u, "abcabc" },
        { "COPIES", { "123", "3" }, 2u, "123123123" },
        { "COPIES", { "a-b", "2" }, 2u, "a-ba-b" },
        { "COPIES", { "Z", "4" }, 2u, "ZZZZ" },
        { "COPIES", { "  ", "2" }, 2u, "    " },

        /* TRANSLATE: omitted tables, explicit empty tables, and pad behavior. */
        { "TRANSLATE", { "abcdef" }, 1u, "ABCDEF" },
        { "TRANSLATE", { "Abc 123" }, 1u, "ABC 123" },
        { "TRANSLATE", { "abbc", "&", "b" }, 3u, "a&&c" },
        { "TRANSLATE", { "abcdef", "12", "ec" }, 3u, "ab2d1f" },
        { "TRANSLATE", { "abcdef", "12", "abcd", "." }, 4u, "12..ef" },
        { "TRANSLATE", { "abc", "xy", "ab" }, 3u, "xyc" },
        { "TRANSLATE", { "abc", "x", "ab", "." }, 4u, "x.c" },
        { "TRANSLATE", { "abc", "x", "ab" }, 3u, "x c" },
        { "TRANSLATE", { "abc", "", "abc" }, 3u, "   " },
        { "TRANSLATE", { "abc", "", "abc", "." }, 4u, "..." },
        { "TRANSLATE", { "abc", NULL, "abc" }, 3u, "   " },
        { "TRANSLATE", { "abc", "x" }, 2u, "   " },
        { "TRANSLATE", { "ABC", "xyz" }, 2u, "   " },
        { "TRANSLATE", { "abc", NULL, "" }, 3u, "abc" },
        { "TRANSLATE", { "abc", "xy", "" }, 3u, "abc" },
        { "TRANSLATE", { "abc", NULL, NULL, "." }, 4u, "..." },
        { "TRANSLATE", { "", "12", "ab", "." }, 4u, "" },
        { "TRANSLATE", { "aaab", "123", "aab" }, 3u, "1113" },
        { "TRANSLATE", { "xyz", "012", "xy" }, 3u, "01z" },
        { "TRANSLATE", { "aA", "1", "a" }, 3u, "1A" },

        /* COMPARE: padded bytewise equality and first mismatch position. */
        { "COMPARE", { "abc", "abc" }, 2u, "0" },
        { "COMPARE", { "abc", "ak" }, 2u, "2" },
        { "COMPARE", { "ab", "ab" }, 2u, "0" },
        { "COMPARE", { "ab ", "ab" }, 2u, "0" },
        { "COMPARE", { "ab", "ab " }, 2u, "0" },
        { "COMPARE", { "ab", "abc" }, 2u, "3" },
        { "COMPARE", { "abc", "ab" }, 2u, "3" },
        { "COMPARE", { "ab", "ab", "x" }, 3u, "0" },
        { "COMPARE", { "ab", "abx", "x" }, 3u, "0" },
        { "COMPARE", { "abx", "ab", "x" }, 3u, "0" },
        { "COMPARE", { "ab", "ab", "y" }, 3u, "0" },
        { "COMPARE", { "ab", "abc", "x" }, 3u, "3" },
        { "COMPARE", { "abc", "ab", "x" }, 3u, "3" },
        { "COMPARE", { "ab--", "ab", "-" }, 3u, "0" },
        { "COMPARE", { "ab-- ", "ab", "-" }, 3u, "5" },
        { "COMPARE", { "", "" }, 2u, "0" },
        { "COMPARE", { "", "x" }, 2u, "1" },
        { "COMPARE", { "x", "" }, 2u, "1" },
        { "COMPARE", { "A", "a" }, 2u, "1" },
        { "COMPARE", { "same", "same" }, 2u, "0" },

        /* VERIFY: membership, nonmembership, option prefixes and start. */
        { "VERIFY", { "AB3", "AB" }, 2u, "3" },
        { "VERIFY", { "AB", "AB" }, 2u, "0" },
        { "VERIFY", { "AB3", "0123456789", "M" }, 3u, "3" },
        { "VERIFY", { "AB3", "AB", "M" }, 3u, "1" },
        { "VERIFY", { "AB3", "AB", "N" }, 3u, "3" },
        { "VERIFY", { "AB3", "AB", "NOMATCH" }, 3u, "3" },
        { "VERIFY", { "AB3", "0123456789", "match" }, 3u, "3" },
        { "VERIFY", { "AB3", "AB", NULL, "2" }, 4u, "3" },
        { "VERIFY", { "AB3", "AB", "M", "2" }, 4u, "2" },
        { "VERIFY", { "AB3", "AB", "N", "1" }, 4u, "3" },
        { "VERIFY", { "AB3", "AB", "M", "1" }, 4u, "1" },
        { "VERIFY", { "", "AB" }, 2u, "0" },
        { "VERIFY", { "AB3", "" }, 2u, "1" },
        { "VERIFY", { "AB3", "", "M" }, 3u, "0" },
        { "VERIFY", { "AB3", "", NULL, "2" }, 4u, "2" },
        { "VERIFY", { "AB3", "", "M", "2" }, 4u, "0" },
        { "VERIFY", { "123", "0123456789" }, 2u, "0" },
        { "VERIFY", { "abc", "ABC" }, 2u, "1" },
        { "VERIFY", { "abc", "bc", "M" }, 3u, "2" },
        { "VERIFY", { "aabb", "ab" }, 2u, "0" },
    };
    static const ErrorCase errors[] = {
        { "LENGTH", { NULL }, 0u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "LENGTH", { NULL }, 1u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "LENGTH", { "a", "b" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "SUBSTR", { "abc" }, 1u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "SUBSTR", { "abc", NULL }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "SUBSTR", { "abc", "0" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "SUBSTR", { "abc", "-1" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "SUBSTR", { "abc", "1", "-1" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "SUBSTR", { "abc", "1", "1.5" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "SUBSTR", { "abc", "1", "1", "" }, 4u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "POS", { "a", "abc", "0" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "POS", { NULL, "abc" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "POS", { "a", "abc", "-1" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "POS", { "a", "abc", "1.5" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "WORD", { "a b", "0" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "WORD", { "a b", "-1" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "WORD", { "a b", "1.5" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "WORDPOS", { "a", "a b", "0" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "WORDPOS", { "a", "a b", "1.5" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "STRIP", { " abc ", "" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "STRIP", { " abc ", "X" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "STRIP", { " abc ", NULL, "xx" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "SPACE", { "a b", "-1" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "SPACE", { "a b", "1.5" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "SPACE", { "a b", "1", "" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "SPACE", { "a b", "1", "xx" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "COPIES", { "x", "-1" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "COPIES", { NULL, "1" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "COPIES", { "x", "2.5" }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "COPIES", { "x", "4294967296" }, 2u, REXX_VM_STATUS_RESOURCE_LIMIT },
        { "COPIES", { "x", "16777217" }, 2u, REXX_VM_STATUS_RESOURCE_LIMIT },
        { "TRANSLATE", { "x", NULL, NULL, "" }, 4u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "TRANSLATE", { "x", NULL, NULL, "xx" }, 4u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "COMPARE", { "x", "x", "" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "COMPARE", { "x", "x", "xx" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "VERIFY", { "x", "x", "Q" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "VERIFY", { "x", NULL }, 2u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "VERIFY", { "x", "x", "" }, 3u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "VERIFY", { "x", "x", NULL, "0" }, 4u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
        { "VERIFY", { "x", "x", NULL, "-1" }, 4u, REXX_VM_STATUS_INVALID_FUNCTION_CALL },
    };
    size_t index;
    size_t failed = 0u;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index)
        failed += (size_t)run_builtin_case(&cases[index]);
    for (index = 0u; index < sizeof(errors) / sizeof(errors[0]); ++index)
        failed += (size_t)run_error_case(&errors[index]);
    failed += (size_t)test_binary_strings();
    failed += (size_t)test_arg_and_integration();
#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
    failed += (size_t)test_allocation_failure();
    failed += (size_t)test_vm_recovery_after_allocation_failure();
#endif
    printf("RX12 built-in assertions: %zu; failures: %zu\n", assertions, failed);
    return failed == 0u ? 0 : 1;
}
