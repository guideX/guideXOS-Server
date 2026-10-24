#include "../rexx_builtin.h"
#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RX35_PROPERTY_SEED UINT32_C(0x52583335)
#define RX35_PROPERTY_CASES ((size_t)4096u)
#define RX35_CAPTURE_CAPACITY ((size_t)1024u)

static size_t assertions;
static size_t direct_assertions;
static size_t property_assertions;
static size_t property_cases;
static size_t failures;

typedef struct Rx35Capture {
    char bytes[RX35_CAPTURE_CAPACITY];
    size_t length;
} Rx35Capture;

static void check(int condition, const char *description, int property)
{
    ++assertions;
    if (property) ++property_assertions;
    else ++direct_assertions;
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL: %s\n", description);
}

static int string_is(const RexxValue *value, const char *expected)
{
    size_t expected_length = strlen(expected);
    return value != NULL && value->type == REXX_VALUE_STRING &&
           value->as.string.length == expected_length &&
           (expected_length == 0u ||
            memcmp(value->as.string.bytes, expected, expected_length) == 0);
}

static int capture_output(void *user_data, const char *bytes, size_t length)
{
    Rx35Capture *capture = (Rx35Capture *)user_data;
    if (capture == NULL || (bytes == NULL && length != 0u) ||
        length > sizeof(capture->bytes) - capture->length)
        return 0;
    if (length != 0u)
        memcpy(capture->bytes + capture->length, bytes, length);
    capture->length += length;
    return 1;
}

static int capture_has(const Rx35Capture *capture, const char *text)
{
    size_t text_length;
    size_t index;
    if (capture == NULL || text == NULL) return 0;
    text_length = strlen(text);
    if (text_length > capture->length) return 0;
    for (index = 0u; index + text_length <= capture->length; ++index)
        if (memcmp(capture->bytes + index, text, text_length) == 0)
            return 1;
    return 0;
}

static int compile_source(const char *source, RexxBytecodeChunk **chunk_out)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxLexerStatus lexer_status;
    RexxParserStatus parser_status;
    RexxCompilerStatus compiler_status;

    if (source == NULL || chunk_out == NULL) return 0;
    *chunk_out = NULL;
    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    lexer_status = rexx_lexer_init(&lexer, source, strlen(source));
    if (lexer_status != REXX_LEXER_STATUS_OK) return 0;
    parser_status = rexx_parser_init(&parser, &lexer);
    if (parser_status != REXX_PARSER_STATUS_OK) return 0;
    parser_status = rexx_parser_parse(&parser, &program);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        rexx_parser_free_ast(program);
        return 0;
    }
    compiler_status = rexx_compiler_compile(program, chunk_out, NULL);
    rexx_parser_free_ast(program);
    if (compiler_status != REXX_COMPILER_STATUS_OK) {
        rexx_bytecode_chunk_free(*chunk_out);
        *chunk_out = NULL;
        return 0;
    }
    return 1;
}

static RexxVMStatus call_externals(RexxVM *vm, const char *name,
                                   const RexxValue *arguments,
                                   size_t argument_count, RexxValue *result)
{
    if (vm == NULL)
        return rexx_builtin_dispatch(name, strlen(name), arguments,
                                     argument_count, result);
    return rexx_builtin_dispatch_with_vm(vm, name, strlen(name), arguments,
                                        argument_count, result);
}

static void test_reference_vectors_and_argument_contract(void)
{
    static const char name[] = "EXTERNALS";
    RexxValue argument = REXX_VALUE_INIT;
    RexxValue omitted_arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    const RexxBuiltinDescriptor *descriptor =
        rexx_builtin_lookup(name, sizeof(name) - 1u);

    check(descriptor != NULL && descriptor->minimum_arguments == 0u &&
              descriptor->maximum_arguments == 0u,
          "EXTERNALS is registered as a zero-argument built-in", 0);
    check(call_externals(NULL, "EXTERNALS", NULL, 0u, &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, "0"),
          "IBM TSO/E reference result is exactly decimal zero", 0);
    check(call_externals(NULL, "eXtErNaLs", NULL, 0u, &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, "0"),
          "mixed-case built-in spelling returns the same reference value", 0);

    check(rexx_value_set_string(&argument, "", 0u) == REXX_VM_STATUS_OK,
          "explicit empty excess argument is prepared", 0);
    check(call_externals(NULL, "EXTERNALS", &argument, 1u, &result) ==
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
              result.type == REXX_VALUE_UNSET,
          "one explicit empty argument is rejected and clears the result", 0);
    check(call_externals(NULL, "EXTERNALS", omitted_arguments, 2u, &result) ==
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
              result.type == REXX_VALUE_UNSET,
          "two explicitly omitted slots are rejected as excess arguments", 0);
    check(call_externals(NULL, "EXTERNALS", NULL, 1u, &result) ==
                  REXX_VM_STATUS_INVALID_ARGUMENT,
          "a missing argument vector is rejected safely", 0);

    rexx_value_clear(&argument);
    rexx_value_clear(&result);
}

static void test_fixed_seed_properties_and_vm_isolation(void)
{
    RexxVM vm_a = { 0 };
    RexxVM vm_b = { 0 };
    RexxValue result = REXX_VALUE_INIT;
    uint32_t state = RX35_PROPERTY_SEED;
    size_t index;

    check(rexx_vm_init(&vm_a, 0u) == REXX_VM_STATUS_OK &&
              rexx_vm_init(&vm_b, 0u) == REXX_VM_STATUS_OK,
          "independent VMs initialize for property calls", 0);
    if (!vm_a.initialized || !vm_b.initialized) goto cleanup;

    for (index = 0u; index < RX35_PROPERTY_CASES; ++index) {
        char mixed_name[10];
        size_t byte_index;
        RexxVM *vm;
        RexxVMStatus status;

        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        for (byte_index = 0u; byte_index < sizeof(mixed_name) - 1u;
             ++byte_index) {
            char character = "EXTERNALS"[byte_index];
            if (((state >> (byte_index % 24u)) & 1u) != 0u &&
                character >= 'A' && character <= 'Z')
                character = (char)(character - 'A' + 'a');
            mixed_name[byte_index] = character;
        }
        mixed_name[sizeof(mixed_name) - 1u] = '\0';
        vm = (state & 1u) != 0u ? &vm_a : &vm_b;
        status = call_externals(vm, mixed_name, NULL, 0u, &result);
        check(status == REXX_VM_STATUS_OK && string_is(&result, "0"),
              "independent constant oracle predicts zero for every call", 1);
        rexx_value_clear(&result);
        ++property_cases;
    }

cleanup:
    rexx_value_clear(&result);
    rexx_vm_free(&vm_a);
    rexx_vm_free(&vm_b);
    check(property_cases == RX35_PROPERTY_CASES,
          "all fixed-seed property cases executed", 0);
}

static void test_allocation_failure_recovery(void)
{
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;

    rexx_vm_test_allocator_disable();
    check(rexx_value_set_string(&result, "stale", 5u) == REXX_VM_STATUS_OK,
          "preexisting result is ready before allocation failure", 0);
    rexx_vm_test_allocator_fail_on(1u);
    status = call_externals(NULL, "EXTERNALS", NULL, 0u, &result);
    rexx_vm_test_allocator_disable();
    check(status == REXX_VM_STATUS_OUT_OF_MEMORY,
          "result allocation failure propagates from EXTERNALS", 0);
    check(result.type == REXX_VALUE_UNSET,
          "rejected result is cleared after allocation failure", 0);
    check(call_externals(NULL, "EXTERNALS", NULL, 0u, &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, "0"),
          "interpreter allocation path recovers on the next call", 0);
    rexx_value_clear(&result);
    check(rexx_vm_test_allocator_live_blocks() == 0u,
          "allocation failure and recovery release all VM test blocks", 0);
    rexx_vm_test_allocator_disable();
}

static void test_language_context_routines_interpret_and_trap(void)
{
    static const char source[] =
        "say 'RX35_EXTERNALS_NORMAL=' || externals()\n"
        "say 'RX35_EXTERNALS_EMPTY_BUFFER=' || externals()\n"
        "numeric digits 1\n"
        "numeric fuzz 0\n"
        "numeric form engineering\n"
        "say 'RX35_EXTERNALS_NUMERIC=' || externals()\n"
        "say 'RX35_EXTERNALS_REPEAT=' || externals() || '/' || externals()\n"
        "queue 'RX35_STACK_ONLY'\n"
        "say 'RX35_EXTERNALS_STACK_BOUNDARY=' || externals() || '/' || queued()\n"
        "pull stack_only\n"
        "call rx35_probe\n"
        "interpret \"RX35_DYNAMIC = EXTERNALS()\"\n"
        "say 'RX35_EXTERNALS_INTERPRET=' || RX35_DYNAMIC\n"
        "signal on syntax name invalid_call\n"
        "ignored = externals('')\n"
        "say 'RX35_EXTERNALS_INVALID_UNCAUGHT'\n"
        "exit 9\n"
        "rx35_probe: procedure\n"
        "call rx35_leaf\n"
        "say 'RX35_EXTERNALS_ROUTINE=' || externals()\n"
        "return\n"
        "rx35_leaf: procedure\n"
        "say 'RX35_EXTERNALS_NESTED=' || externals()\n"
        "return\n"
        "invalid_call:\n"
        "signal off syntax\n"
        "say 'RX35_EXTERNALS_INVALID_CAUGHT'\n"
        "say 'RX35_EXTERNALS_RECOVERY=' || externals()\n"
        "exit\n";
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm = { 0 };
    Rx35Capture capture = { { 0 }, 0u };
    RexxVMStatus status;

    check(compile_source(source, &chunk),
          "language fixture compiles for routine/INTERPRET/trap coverage", 0);
    if (chunk == NULL) return;
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "language fixture VM initializes", 0);
    if (!vm.initialized) goto cleanup;
    rexx_vm_set_output_writer(&vm, capture_output, &capture);
    status = rexx_vm_execute(&vm, chunk);
    check(status == REXX_VM_STATUS_OK,
          "trapped invalid call leaves the interpreter responsive", 0);
    check(capture_has(&capture, "RX35_EXTERNALS_NORMAL=0") &&
              capture_has(&capture, "RX35_EXTERNALS_EMPTY_BUFFER=0") &&
              capture_has(&capture, "RX35_EXTERNALS_NUMERIC=0") &&
              capture_has(&capture, "RX35_EXTERNALS_REPEAT=0/0") &&
              capture_has(&capture, "RX35_EXTERNALS_STACK_BOUNDARY=0/1") &&
              capture_has(&capture, "RX35_EXTERNALS_ROUTINE=0") &&
              capture_has(&capture, "RX35_EXTERNALS_NESTED=0") &&
              capture_has(&capture, "RX35_EXTERNALS_INTERPRET=0") &&
              capture_has(&capture, "RX35_EXTERNALS_INVALID_CAUGHT") &&
              capture_has(&capture, "RX35_EXTERNALS_RECOVERY=0") &&
              !capture_has(&capture, "RX35_EXTERNALS_INVALID_UNCAUGHT"),
          "IBM vector, numeric independence, nested routine, INTERPRET, trap, and recovery markers match", 0);

cleanup:
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
}

int main(void)
{
    test_reference_vectors_and_argument_contract();
    test_fixed_seed_properties_and_vm_isolation();
    test_allocation_failure_recovery();
    test_language_context_routines_interpret_and_trap();
    printf("RX35 EXTERNALS seed=0x%08x direct_assertions=%lu property_cases=%lu property_assertions=%lu boundary_groups=4 total_assertions=%lu failures=%lu\n",
           (unsigned int)RX35_PROPERTY_SEED,
           (unsigned long)direct_assertions,
           (unsigned long)property_cases,
           (unsigned long)property_assertions,
           (unsigned long)assertions,
           (unsigned long)failures);
    rexx_vm_test_allocator_disable();
    return failures == 0u ? 0 : 1;
}
