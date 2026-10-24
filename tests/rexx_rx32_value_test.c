#include "../rexx_builtin.h"
#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX32_PROPERTY_SEED UINT32_C(0x52583332)
#define RX32_PROPERTY_CASES ((size_t)4096u)
#define RX32_CAPTURE_CAPACITY ((size_t)4096u)
#define RX32_MAX_NAME_LENGTH ((size_t)65535u)

static size_t assertions;
static size_t failures;
static size_t property_cases;
static size_t direct_assertions;
static size_t property_assertions;

typedef struct Rx32Capture {
    unsigned char bytes[RX32_CAPTURE_CAPACITY];
    size_t length;
} Rx32Capture;

static void check(int condition, const char *description)
{
    ++assertions;
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL: %s\n", description);
}

static int string_is(const RexxValue *value, const void *expected,
                     size_t expected_length)
{
    return value != NULL && value->type == REXX_VALUE_STRING &&
           value->as.string.length == expected_length &&
           (expected_length == 0u ||
            memcmp(value->as.string.bytes, expected, expected_length) == 0);
}

static RexxVMStatus call_value(RexxVM *vm, const RexxValue *arguments,
                              size_t argument_count, RexxValue *result)
{
    return rexx_builtin_dispatch_with_vm(vm, "VALUE", 5u, arguments,
                                         argument_count, result);
}

static RexxVMStatus text_value(RexxValue *value, const char *text)
{
    return rexx_value_set_string(value, text, strlen(text));
}

static int capture_write(void *user_data, const char *bytes, size_t length)
{
    Rx32Capture *capture = (Rx32Capture *)user_data;
    if (capture == NULL || (bytes == NULL && length != 0u) ||
        length > sizeof(capture->bytes) - capture->length)
        return 0;
    if (length != 0u)
        memcpy(capture->bytes + capture->length, bytes, length);
    capture->length += length;
    return 1;
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

static void test_direct_lookup_assignment_and_validation(void)
{
    static const unsigned char binary_value[] = { 'A', 0u, 0x80u, 0xffu };
    static const char invalid_high_bit_name[] = { 'A', (char)0x80 };
    RexxVM vm;
    RexxValue arguments[3] = { REXX_VALUE_INIT, REXX_VALUE_INIT,
                               REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    RexxValue binary = REXX_VALUE_INIT;
    RexxValue big_name = REXX_VALUE_INIT;
    RexxValue too_big_name = REXX_VALUE_INIT;
    char *name_buffer = NULL;
    char *too_big_buffer = NULL;
    RexxVMStatus status;

    memset(&vm, 0, sizeof(vm));
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "direct VALUE VM initializes");
    if (!vm.initialized) return;
    vm.numeric_digits = 4u;

    check(text_value(&arguments[0], "MiSsInG") == REXX_VM_STATUS_OK,
          "missing variable name argument is prepared");
    status = call_value(&vm, arguments, 1u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "MISSING", 7u),
          "uninitialized VALUE returns the uppercase default without NOVALUE");
    rexx_value_clear(&result);

    check(text_value(&arguments[0], "alpha") == REXX_VM_STATUS_OK &&
              text_value(&arguments[1], "before") == REXX_VM_STATUS_OK &&
              rexx_vm_set_local(&vm, "ALPHA", 5u, &arguments[1]) ==
                  REXX_VM_STATUS_OK,
          "initialized variable is installed in the VM scope");
    status = call_value(&vm, arguments, 1u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "before", 6u),
          "VALUE reads the current value case insensitively");
    rexx_value_clear(&result);

    check(text_value(&arguments[1], "after") == REXX_VM_STATUS_OK,
          "replacement value is prepared");
    status = call_value(&vm, arguments, 2u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "before", 6u),
          "VALUE assignment returns the value from before the update");
    rexx_value_clear(&result);
    status = call_value(&vm, arguments, 1u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "after", 5u),
          "VALUE assignment updates the active variable");
    rexx_value_clear(&result);

    check(rexx_value_set_string(&arguments[1], "", 0u) == REXX_VM_STATUS_OK,
          "empty replacement value is prepared");
    status = call_value(&vm, arguments, 2u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "after", 5u),
          "explicit empty string is an assignment, distinct from omission");
    rexx_value_clear(&result);
    status = call_value(&vm, arguments, 1u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "", 0u),
          "empty assignment leaves the variable empty");
    rexx_value_clear(&result);

    check(text_value(&arguments[0], "I") == REXX_VM_STATUS_OK &&
              text_value(&arguments[1], "3") == REXX_VM_STATUS_OK &&
              rexx_vm_set_local(&vm, "I", 1u, &arguments[1]) ==
                  REXX_VM_STATUS_OK &&
              text_value(&arguments[1], "third") == REXX_VM_STATUS_OK &&
              rexx_vm_set_local(&vm, "LIST.3", 6u, &arguments[1]) ==
                  REXX_VM_STATUS_OK &&
              text_value(&arguments[0], "list.i") == REXX_VM_STATUS_OK,
          "compound VALUE fixture is prepared");
    status = call_value(&vm, arguments, 1u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "third", 5u),
          "VALUE substitutes a symbolic compound tail in the active scope");
    rexx_value_clear(&result);

    check(rexx_value_set_string(&binary, (const char *)binary_value,
                                sizeof(binary_value)) == REXX_VM_STATUS_OK &&
              rexx_vm_set_local(&vm, "BIN", 3u, &binary) ==
                  REXX_VM_STATUS_OK &&
              text_value(&arguments[0], "bin") == REXX_VM_STATUS_OK,
          "binary variable value is prepared");
    status = call_value(&vm, arguments, 1u, &result);
    check(status == REXX_VM_STATUS_OK &&
              string_is(&result, binary_value, sizeof(binary_value)),
          "VALUE preserves embedded NUL and high-bit bytes by explicit length");
    rexx_value_clear(&result);

    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);
    check(call_value(&vm, NULL, 0u, &result) ==
              REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "VALUE rejects a missing required argument");
    check(text_value(&arguments[0], "ALPHA") == REXX_VM_STATUS_OK,
          "argument for excess-count test is prepared");
    check(call_value(&vm, arguments, 3u, &result) ==
              REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "VALUE rejects the IBM CMS selector argument as out of scope");
    check(call_value(NULL, arguments, 1u, &result) ==
              REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "standalone dispatch rejects VALUE without a VM variable pool");

    rexx_value_clear(&arguments[0]);
    check(rexx_value_set_string(&arguments[0], "", 0u) ==
              REXX_VM_STATUS_OK &&
              call_value(&vm, arguments, 1u, &result) ==
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "VALUE rejects an empty dynamic symbol name");
    rexx_value_clear(&arguments[0]);
    check(text_value(&arguments[0], "2BAD") == REXX_VM_STATUS_OK &&
              call_value(&vm, arguments, 1u, &result) ==
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "VALUE rejects a symbol beginning with a digit");
    rexx_value_clear(&arguments[0]);
    check(rexx_value_set_string(&arguments[0], invalid_high_bit_name,
                                sizeof(invalid_high_bit_name)) ==
              REXX_VM_STATUS_OK &&
              call_value(&vm, arguments, 1u, &result) ==
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "VALUE rejects non-symbol high-bit name bytes");
    rexx_value_clear(&arguments[0]);

    name_buffer = (char *)malloc(RX32_MAX_NAME_LENGTH);
    too_big_buffer = (char *)malloc(RX32_MAX_NAME_LENGTH + 1u);
    check(name_buffer != NULL && too_big_buffer != NULL,
          "name-limit test buffers allocate");
    if (name_buffer != NULL && too_big_buffer != NULL) {
        memset(name_buffer, 'A', RX32_MAX_NAME_LENGTH);
        memset(too_big_buffer, 'B', RX32_MAX_NAME_LENGTH + 1u);
        check(rexx_value_set_string(&big_name, name_buffer,
                                    RX32_MAX_NAME_LENGTH) ==
                  REXX_VM_STATUS_OK,
              "maximum 65,535-byte symbol name is accepted as a value");
        status = call_value(&vm, &big_name, 1u, &result);
        check(status == REXX_VM_STATUS_OK &&
                  string_is(&result, name_buffer, RX32_MAX_NAME_LENGTH),
              "maximum symbol name resolves within the bounded name limit");
        rexx_value_clear(&result);
        check(rexx_value_set_string(&too_big_name, too_big_buffer,
                                    RX32_MAX_NAME_LENGTH + 1u) ==
                  REXX_VM_STATUS_OK &&
                  call_value(&vm, &too_big_name, 1u, &result) ==
                      REXX_VM_STATUS_RESOURCE_LIMIT,
              "symbol name one byte beyond 65,535 is rejected as a resource limit");
    }
    free(name_buffer);
    free(too_big_buffer);

    check(vm.numeric_digits == 4u,
          "VALUE does not modify the active numeric environment");
    rexx_value_clear(&result);
    rexx_value_clear(&binary);
    rexx_value_clear(&big_name);
    rexx_value_clear(&too_big_name);
    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);
    rexx_value_clear(&arguments[2]);
    rexx_vm_free(&vm);
}

static uint32_t property_next(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void test_fixed_seed_property_model(void)
{
    RexxVM vm;
    RexxValue name = REXX_VALUE_INIT;
    RexxValue replacement = REXX_VALUE_INIT;
    RexxValue result = REXX_VALUE_INIT;
    uint32_t seed = RX32_PROPERTY_SEED;
    size_t index;
    char name_buffer[24];
    char lower_name[24];
    char expected[32];
    char updated[32];
    RexxVMStatus status;

    memset(&vm, 0, sizeof(vm));
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "property-model VM initializes");
    if (!vm.initialized) return;
    for (index = 0u; index < RX32_PROPERTY_CASES; ++index) {
        uint32_t value_bits = property_next(&seed);
        uint32_t replacement_bits = property_next(&seed);
        int name_length = snprintf(name_buffer, sizeof(name_buffer),
                                   "P%04u", (unsigned int)index);
        int value_length = snprintf(expected, sizeof(expected),
                                    "V%08X", (unsigned int)value_bits);
        int updated_length = snprintf(updated, sizeof(updated),
                                      "U%08X", (unsigned int)replacement_bits);
        size_t char_index;
        if (name_length <= 0 || value_length <= 0 || updated_length <= 0) {
            check(0, "property-model formatting remains bounded");
            break;
        }
        for (char_index = 0u; char_index < (size_t)name_length; ++char_index)
            lower_name[char_index] = (char)(name_buffer[char_index] >= 'A' &&
                name_buffer[char_index] <= 'Z'
                ? name_buffer[char_index] + ('a' - 'A')
                : name_buffer[char_index]);
        lower_name[name_length] = '\0';

        check(text_value(&replacement, expected) == REXX_VM_STATUS_OK &&
                  rexx_vm_set_local(&vm, name_buffer,
                                    (size_t)name_length, &replacement) ==
                      REXX_VM_STATUS_OK &&
                  text_value(&name, lower_name) == REXX_VM_STATUS_OK,
              "property case seeds VM value and independently held model value");
        status = call_value(&vm, &name, 1u, &result);
        check(status == REXX_VM_STATUS_OK &&
                  string_is(&result, expected, (size_t)value_length),
              "property lookup matches the independent bounded map oracle");
        rexx_value_clear(&result);

        check(text_value(&replacement, updated) == REXX_VM_STATUS_OK,
              "property replacement is prepared");
        status = call_value(&vm, (RexxValue[]){ name, replacement }, 2u,
                            &result);
        check(status == REXX_VM_STATUS_OK &&
                  string_is(&result, expected, (size_t)value_length),
              "property assignment returns the model's old value");
        rexx_value_clear(&result);
        status = call_value(&vm, &name, 1u, &result);
        check(status == REXX_VM_STATUS_OK &&
                  string_is(&result, updated, (size_t)updated_length),
              "property assignment stores the model's replacement value");
        rexx_value_clear(&result);
        ++property_cases;
    }
    rexx_value_clear(&name);
    rexx_value_clear(&replacement);
    rexx_value_clear(&result);
    rexx_vm_free(&vm);
}

static void test_full_runtime_string_boundary(void)
{
    const size_t maximum = REXX_VM_MAX_PARSE_SOURCE_LENGTH;
    unsigned char *bytes = NULL;
    RexxVM vm;
    RexxValue stored = REXX_VALUE_INIT;
    RexxValue name = REXX_VALUE_INIT;
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;

    memset(&vm, 0, sizeof(vm));
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "runtime-string boundary VM initializes");
    if (!vm.initialized) return;
    bytes = (unsigned char *)malloc(maximum);
    check(bytes != NULL, "16 MiB runtime-string boundary buffer allocates");
    if (bytes == NULL) goto cleanup;
    memset(bytes, 'Z', maximum);
    bytes[0] = 0u;
    bytes[maximum - 1u] = 0xffu;
    check(rexx_value_set_string(&stored, (const char *)bytes, maximum) ==
              REXX_VM_STATUS_OK &&
              rexx_vm_set_local(&vm, "MAXVALUE", 8u, &stored) ==
                  REXX_VM_STATUS_OK &&
              text_value(&name, "MAXVALUE") == REXX_VM_STATUS_OK,
          "exact 16 MiB variable value is stored");
    status = call_value(&vm, &name, 1u, &result);
    check(status == REXX_VM_STATUS_OK &&
              string_is(&result, bytes, maximum),
          "VALUE returns the entire 16 MiB explicit-length value");
    rexx_value_clear(&result);

cleanup:
    free(bytes);
    rexx_value_clear(&stored);
    rexx_value_clear(&name);
    rexx_value_clear(&result);
    rexx_vm_free(&vm);
}

static void test_allocation_failure_and_recovery(void)
{
    RexxVM vm;
    RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    RexxValue expected = REXX_VALUE_INIT;
    RexxVMStatus status;
    size_t fail_at;
    int reached_success = 0;

    memset(&vm, 0, sizeof(vm));
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "allocation-failure VM initializes");
    if (!vm.initialized) return;
    check(text_value(&expected, "stable") == REXX_VM_STATUS_OK &&
              rexx_vm_set_local(&vm, "FAILVAR", 7u, &expected) ==
                  REXX_VM_STATUS_OK &&
              text_value(&arguments[0], "FAILVAR") == REXX_VM_STATUS_OK &&
              text_value(&arguments[1], "replacement") == REXX_VM_STATUS_OK,
          "allocation-failure assignment fixture is installed");

    for (fail_at = 1u; fail_at <= 8u; ++fail_at) {
        rexx_vm_test_allocator_fail_on(fail_at);
        status = call_value(&vm, arguments, 2u, &result);
        rexx_vm_test_allocator_disable();
        if (status == REXX_VM_STATUS_OK) {
            reached_success = 1;
            rexx_value_clear(&result);
            break;
        }
        check(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
                  result.type == REXX_VALUE_UNSET,
              "each injected allocation failure returns a clean error result");
        status = call_value(&vm, arguments, 1u, &result);
        check(status == REXX_VM_STATUS_OK &&
                  string_is(&result, "stable", 6u),
              "failed VALUE assignment leaves the original value intact");
        rexx_value_clear(&result);
        check(rexx_vm_set_local(&vm, "FAILVAR", 7u, &expected) ==
                  REXX_VM_STATUS_OK,
              "allocation-failure iteration restores the baseline value");
    }
    check(reached_success,
          "injected allocation sequence reaches a successful VALUE assignment");
    status = call_value(&vm, arguments, 1u, &result);
    check(status == REXX_VM_STATUS_OK &&
              string_is(&result, "replacement", 11u),
          "interpreter variable access recovers after allocation failures");

    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);
    rexx_value_clear(&result);
    rexx_value_clear(&expected);
    rexx_vm_free(&vm);
}

static void test_language_scopes_and_error_recovery(void)
{
    static const char source[] =
        "signal on novalue name noValueHandler\n"
        "alpha = 'before'\n"
        "name = 'alpha'\n"
        "say value(name)\n"
        "say value('missing')\n"
        "say value('missing','created')\n"
        "say value('missing')\n"
        "idx = '2'\n"
        "items.2 = 'two'\n"
        "say value('items.idx')\n"
        "say value('items.idx','changed')\n"
        "say items.2\n"
        "shared = 'caller'\n"
        "call updateShared\n"
        "say shared\n"
        "localResult = localScope()\n"
        "say localResult\n"
        "signal on syntax name syntaxHandler\n"
        "bad = value('')\n"
        "say 'UNREACHABLE'\n"
        "exit 0\n"
        "updateShared: procedure expose shared\n"
        "old = value('shared','inner')\n"
        "return\n"
        "localScope: procedure\n"
        "local = 'local-value'\n"
        "old = value('local','local-new')\n"
        "return old || ':' || local\n"
        "noValueHandler:\n"
        "say 'NOVALUE_RAISED'\n"
        "exit 8\n"
        "syntaxHandler:\n"
        "signal off syntax\n"
        "say 'INVALID_NAME_RECOVERED'\n"
        "say value('alpha')\n"
        "exit 0\n";
    static const char expected[] =
        "before\nMISSING\nMISSING\ncreated\ntwo\ntwo\nchanged\n"
        "inner\nlocal-value:local-new\nINVALID_NAME_RECOVERED\nbefore\n";
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm;
    RexxVMStatus status;
    Rx32Capture capture;

    memset(&vm, 0, sizeof(vm));
    memset(&capture, 0, sizeof(capture));
    check(compile_source(source, &chunk),
          "VALUE nested-scope and error-recovery fixture compiles");
    if (chunk == NULL) return;
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "language-scope VM initializes");
    if (vm.initialized) {
        rexx_vm_set_output_writer(&vm, capture_write, &capture);
        status = rexx_vm_execute(&vm, chunk);
        check(status == REXX_VM_STATUS_OK,
              "VALUE language fixture executes through NOVALUE and SYNTAX traps");
        check(capture.length == sizeof(expected) - 1u &&
                  memcmp(capture.bytes, expected, sizeof(expected) - 1u) == 0,
              "VALUE honors compound tails, exposed scopes, local scopes, and recovery");
        if (capture.length != sizeof(expected) - 1u ||
            memcmp(capture.bytes, expected, sizeof(expected) - 1u) != 0)
            fprintf(stderr, "  VALUE output (%lu bytes): '%.*s'\n",
                    (unsigned long)capture.length, (int)capture.length,
                    capture.bytes);
        rexx_vm_free(&vm);
    }
    rexx_bytecode_chunk_free(chunk);
}

static void test_vm_isolation(void)
{
    RexxVM vm_a;
    RexxVM vm_b;
    RexxValue value_a = REXX_VALUE_INIT;
    RexxValue value_b = REXX_VALUE_INIT;
    RexxValue name = REXX_VALUE_INIT;
    RexxValue result = REXX_VALUE_INIT;

    memset(&vm_a, 0, sizeof(vm_a));
    memset(&vm_b, 0, sizeof(vm_b));
    check(rexx_vm_init(&vm_a, 0u) == REXX_VM_STATUS_OK &&
              rexx_vm_init(&vm_b, 0u) == REXX_VM_STATUS_OK,
          "independent VALUE VMs initialize");
    if (!vm_a.initialized || !vm_b.initialized) goto cleanup;
    check(text_value(&value_a, "A") == REXX_VM_STATUS_OK &&
              text_value(&value_b, "B") == REXX_VM_STATUS_OK &&
              rexx_vm_set_local(&vm_a, "SHARED", 6u, &value_a) ==
                  REXX_VM_STATUS_OK &&
              rexx_vm_set_local(&vm_b, "SHARED", 6u, &value_b) ==
                  REXX_VM_STATUS_OK &&
              text_value(&name, "shared") == REXX_VM_STATUS_OK,
          "VMs receive different values for the same variable name");
    check(call_value(&vm_a, &name, 1u, &result) == REXX_VM_STATUS_OK &&
              string_is(&result, "A", 1u),
          "VM A VALUE sees only VM A storage");
    rexx_value_clear(&result);
    check(call_value(&vm_b, &name, 1u, &result) == REXX_VM_STATUS_OK &&
              string_is(&result, "B", 1u),
          "VM B VALUE sees only VM B storage");

cleanup:
    rexx_value_clear(&value_a);
    rexx_value_clear(&value_b);
    rexx_value_clear(&name);
    rexx_value_clear(&result);
    rexx_vm_free(&vm_a);
    rexx_vm_free(&vm_b);
}

int main(void)
{
    size_t assertion_mark;

    test_direct_lookup_assignment_and_validation();
    direct_assertions = assertions;
    assertion_mark = assertions;
    test_fixed_seed_property_model();
    property_assertions = assertions - assertion_mark;
    test_full_runtime_string_boundary();
    test_allocation_failure_and_recovery();
    test_language_scopes_and_error_recovery();
    test_vm_isolation();

    printf("RX32 VALUE seed=0x%08X direct_assertions=%lu property_cases=%lu property_assertions=%lu boundary_groups=4 total_assertions=%lu failures=%lu\n",
           (unsigned int)RX32_PROPERTY_SEED,
           (unsigned long)direct_assertions,
           (unsigned long)property_cases,
           (unsigned long)property_assertions,
           (unsigned long)assertions,
           (unsigned long)failures);
    return failures == 0u ? 0 : 1;
}
