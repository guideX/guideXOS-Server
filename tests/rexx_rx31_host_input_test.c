#include "../rexx_builtin.h"
#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX31_CAPTURE_CAPACITY ((size_t)262144u)
#define RX31_PROPERTY_SEED UINT32_C(0x52583331)
#define RX31_PROPERTY_OPERATIONS ((size_t)256u)
#define RX31_MODEL_CAPACITY ((size_t)512u)
#define RX31_LONG_LINE_BYTES ((size_t)65536u)

static size_t assertions;
static size_t failures;
static size_t property_operations;

typedef struct Rx31Capture {
    unsigned char *bytes;
    size_t length;
    size_t capacity;
} Rx31Capture;

typedef struct Rx31InputEntry {
    RexxVMInputReadStatus status;
    const unsigned char *bytes;
    size_t length;
    RexxVMInputOwnership ownership;
} Rx31InputEntry;

typedef struct Rx31Provider {
    Rx31InputEntry entries[4];
    size_t entry_count;
    size_t next_entry;
    size_t calls;
    int allocate_owned;
    int fail_provider_allocation;
    unsigned char *owned_bytes;
    RexxVM *mutation_vm;
    RexxVMStatus mutation_status;
} Rx31Provider;

typedef struct Rx31StreamProvider {
    uint32_t next_value;
    size_t calls;
    unsigned char bytes[24];
    size_t length;
} Rx31StreamProvider;

static void check(int condition, const char *description)
{
    ++assertions;
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL: %s\n", description);
}

static int capture_write(void *user_data, const char *bytes, size_t length)
{
    Rx31Capture *capture = (Rx31Capture *)user_data;
    if (capture == NULL || (bytes == NULL && length != 0u) ||
        length > capture->capacity - capture->length)
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

    if (chunk_out == NULL || source == NULL) return 0;
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

static RexxVMInputReadStatus provider_read(void *user_data,
                                          RexxVMInputLine *line)
{
    Rx31Provider *provider = (Rx31Provider *)user_data;
    Rx31InputEntry *entry;

    if (provider == NULL || line == NULL) return REXX_VM_INPUT_READ_ERROR;
    ++provider->calls;
    if (provider->mutation_vm != NULL) {
        provider->mutation_status = rexx_vm_set_input_reader(
            provider->mutation_vm, NULL, NULL);
    }
    if (provider->fail_provider_allocation) {
        provider->fail_provider_allocation = 0;
        return REXX_VM_INPUT_READ_ERROR;
    }
    if (provider->next_entry >= provider->entry_count)
        return REXX_VM_INPUT_READ_END_OF_INPUT;

    entry = &provider->entries[provider->next_entry++];
    if (entry->status != REXX_VM_INPUT_READ_OK)
        return entry->status;

    line->length = entry->length;
    line->ownership = entry->ownership;
    if (provider->allocate_owned) {
        free(provider->owned_bytes);
        provider->owned_bytes = (unsigned char *)malloc(
            entry->length != 0u ? entry->length : 1u);
        if (provider->owned_bytes == NULL)
            return REXX_VM_INPUT_READ_ERROR;
        if (entry->length != 0u)
            memcpy(provider->owned_bytes, entry->bytes, entry->length);
        line->bytes = provider->owned_bytes;
    } else {
        line->bytes = entry->bytes;
    }
    return REXX_VM_INPUT_READ_OK;
}

static RexxVMInputReadStatus stream_read(void *user_data,
                                         RexxVMInputLine *line)
{
    Rx31StreamProvider *provider = (Rx31StreamProvider *)user_data;
    int length;

    if (provider == NULL || line == NULL) return REXX_VM_INPUT_READ_ERROR;
    length = snprintf((char *)provider->bytes, sizeof(provider->bytes),
                      "Host%03u", (unsigned int)provider->next_value++);
    if (length < 0 || (size_t)length >= sizeof(provider->bytes))
        return REXX_VM_INPUT_READ_ERROR;
    provider->length = (size_t)length;
    ++provider->calls;
    line->bytes = provider->bytes;
    line->length = provider->length;
    line->ownership = REXX_VM_INPUT_OWNERSHIP_BORROWED;
    return REXX_VM_INPUT_READ_OK;
}

static void add_input(Rx31Provider *provider,
                      RexxVMInputReadStatus status,
                      const void *bytes,
                      size_t length)
{
    Rx31InputEntry *entry;
    if (provider->entry_count >= sizeof(provider->entries) /
                                     sizeof(provider->entries[0])) {
        check(0, "provider fixture has bounded input entries");
        return;
    }
    entry = &provider->entries[provider->entry_count++];
    entry->status = status;
    entry->bytes = (const unsigned char *)bytes;
    entry->length = length;
    entry->ownership = REXX_VM_INPUT_OWNERSHIP_BORROWED;
}

static int execute_case(const char *source,
                        Rx31Provider *provider,
                        const unsigned char *expected,
                        size_t expected_length,
                        RexxVMStatus expected_status,
                        const char *description)
{
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm;
    RexxVMStatus status;
    Rx31Capture capture;
    unsigned char capture_bytes[RX31_CAPTURE_CAPACITY];
    int compiled;

    memset(&vm, 0, sizeof(vm));
    memset(&capture, 0, sizeof(capture));
    capture.bytes = capture_bytes;
    capture.capacity = sizeof(capture_bytes);
    compiled = compile_source(source, &chunk);
    check(compiled, "RX31 provider fixture compiles");
    if (!compiled) return 0;
    status = rexx_vm_init(&vm, 0u);
    check(status == REXX_VM_STATUS_OK, "RX31 provider VM initializes");
    if (status != REXX_VM_STATUS_OK) goto cleanup;
    rexx_vm_set_output_writer(&vm, capture_write, &capture);
    if (provider != NULL) {
        status = rexx_vm_set_input_reader(&vm, provider_read, provider);
        check(status == REXX_VM_STATUS_OK, "RX31 provider installs per VM");
    }
    status = rexx_vm_execute(&vm, chunk);
    check(status == expected_status, description);
    if (status != expected_status) {
        fprintf(stderr, "  expected=%s actual=%s message=%s\n",
                rexx_vm_status_name(expected_status),
                rexx_vm_status_name(status),
                vm.error.message != NULL ? vm.error.message : "(none)");
    }
    if (expected_status == REXX_VM_STATUS_OK && expected != NULL) {
        int output_matches = capture.length == expected_length &&
            (expected_length == 0u ||
             memcmp(capture.bytes, expected, expected_length) == 0);
        check(output_matches, description);
        if (!output_matches) {
            fprintf(stderr, "  output length=%lu expected=%lu actual=",
                    (unsigned long)capture.length,
                    (unsigned long)expected_length);
            for (size_t index = 0u; index < capture.length && index < 64u;
                 ++index)
                fprintf(stderr, "%02x", capture.bytes[index]);
            fprintf(stderr, " expected=");
            for (size_t index = 0u; index < expected_length && index < 64u;
                 ++index)
                fprintf(stderr, "%02x", expected[index]);
            fputc('\n', stderr);
        }
    }
    rexx_vm_free(&vm);
    if (provider != NULL) {
        free(provider->owned_bytes);
        provider->owned_bytes = NULL;
    }

cleanup:
    rexx_bytecode_chunk_free(chunk);
    return status == expected_status;
}

static void test_case_semantics_and_binary_safety(void)
{
    static const char pull_source[] = "pull a\nsay a\n";
    static const unsigned char pull_expected[] = "MIXED INPUT\n";
    static const char parse_source[] = "parse pull b\nsay b\n";
    static const unsigned char parse_expected[] = "MiXeD Input\n";
    static const char upper_source[] = "parse upper pull b\nsay b\n";
    static const unsigned char upper_expected[] = "MIXED INPUT\n";
    static const char empty_source[] =
        "parse pull a\nsay a\nsay queued()\n";
    static const unsigned char empty_expected[] = "\n0\n";
    static const char spaced_source[] = "parse pull first last\nsay first\nsay last\n";
    static const unsigned char spaced_expected[] = "lead\nmiddle trail  \n";
    static const unsigned char mixed_bytes[] = "MiXeD Input";
    static const unsigned char spaced_bytes[] = "  lead middle trail  ";
    static const unsigned char binary_bytes[] = { 'a', 0, 'b', 0xe9, 'c' };
    static const unsigned char binary_expected[] = { 'A', 0, 'B', 0xe9, 'C', '\n' };
    static const char binary_source[] = "pull a\nsay a\n";
    Rx31Provider provider;

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_OK, mixed_bytes,
              sizeof(mixed_bytes) - 1u);
    execute_case(pull_source, &provider, pull_expected,
                 sizeof(pull_expected) - 1u, REXX_VM_STATUS_OK,
                 "PULL obtains provider input and uppercases ASCII bytes");

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_OK, mixed_bytes,
              sizeof(mixed_bytes) - 1u);
    execute_case(parse_source, &provider, parse_expected,
                 sizeof(parse_expected) - 1u, REXX_VM_STATUS_OK,
                 "PARSE PULL obtains provider input and preserves case");

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_OK, mixed_bytes,
              sizeof(mixed_bytes) - 1u);
    execute_case(upper_source, &provider, upper_expected,
                 sizeof(upper_expected) - 1u, REXX_VM_STATUS_OK,
                 "PARSE UPPER PULL applies the existing uppercase semantics");

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_OK, NULL, 0u);
    execute_case(empty_source, &provider, empty_expected,
                 sizeof(empty_expected) - 1u, REXX_VM_STATUS_OK,
                 "a successful zero-length provider line is valid input");

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_OK, spaced_bytes,
              sizeof(spaced_bytes) - 1u);
    execute_case(spaced_source, &provider, spaced_expected,
                 sizeof(spaced_expected) - 1u, REXX_VM_STATUS_OK,
                 "provider input keeps the existing whitespace template behavior");

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_OK, binary_bytes,
              sizeof(binary_bytes));
    execute_case(binary_source, &provider, binary_expected,
                 sizeof(binary_expected), REXX_VM_STATUS_OK,
                 "provider input preserves embedded NUL and high-bit bytes");
}

static void test_queue_priority_and_provider_statuses(void)
{
    static const char priority_source[] =
        "queue 'Queued first'\n"
        "parse pull a\n"
        "say a\n"
        "say queued()\n";
    static const unsigned char priority_expected[] = "Queued first\n0\n";
    static const char empty_pull[] = "pull\n";
    static const unsigned char unused[] = "unused";
    Rx31Provider provider;

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_OK, unused,
              sizeof(unused) - 1u);
    execute_case(priority_source, &provider, priority_expected,
                 sizeof(priority_expected) - 1u, REXX_VM_STATUS_OK,
                 "queued data has priority and QUEUED excludes provider input");
    check(provider.calls == 0u,
          "queue-first PULL does not invoke the installed host provider");

    memset(&provider, 0, sizeof(provider));
    execute_case(empty_pull, NULL, NULL, 0u,
                 REXX_VM_STATUS_INPUT_UNAVAILABLE,
                 "no provider retains RX30 unavailable-input status");

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_UNAVAILABLE, NULL, 0u);
    execute_case(empty_pull, &provider, NULL, 0u,
                 REXX_VM_STATUS_INPUT_NOT_READY,
                 "provider no-data status differs from missing provider");

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_END_OF_INPUT, NULL, 0u);
    execute_case(empty_pull, &provider, NULL, 0u,
                 REXX_VM_STATUS_INPUT_END_OF_FILE,
                 "provider EOF remains distinct from unavailable input");

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_ERROR, NULL, 0u);
    execute_case(empty_pull, &provider, NULL, 0u,
                 REXX_VM_STATUS_INPUT_PROVIDER_ERROR,
                 "provider failure maps to a deterministic VM error");

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_PENDING, NULL, 0u);
    execute_case(empty_pull, &provider, NULL, 0u,
                 REXX_VM_STATUS_INPUT_PENDING_UNSUPPORTED,
                 "unsupported pending input fails immediately");

    check(strcmp(rexx_vm_status_name(REXX_VM_STATUS_INPUT_END_OF_FILE),
                 "unknown VM status") != 0 &&
              strcmp(rexx_vm_status_name(REXX_VM_STATUS_INPUT_RESULT_INVALID),
                     "unknown VM status") != 0,
          "all RX31 input statuses have stable readable names");
}

static void test_validation_limits_and_provider_ownership(void)
{
    static const char pull_source[] = "parse pull a\n";
    static const unsigned char valid[] = "borrowed";
    Rx31Provider provider;
    unsigned char *long_line;
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm;
    RexxVMStatus status;

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_OK, NULL, 3u);
    execute_case(pull_source, &provider, NULL, 0u,
                 REXX_VM_STATUS_INPUT_RESULT_INVALID,
                 "NULL with a nonzero provider length is rejected");

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_OK, valid,
              sizeof(valid) - 1u);
    provider.entries[0].ownership = (RexxVMInputOwnership)99;
    execute_case(pull_source, &provider, NULL, 0u,
                 REXX_VM_STATUS_INPUT_RESULT_INVALID,
                 "unsupported ownership metadata is rejected");

    long_line = (unsigned char *)malloc(
        REXX_VM_MAX_PARSE_SOURCE_LENGTH + 1u);
    check(long_line != NULL, "bounded long-line fixture allocates");
    if (long_line == NULL) return;
    memset(long_line, 'x', REXX_VM_MAX_PARSE_SOURCE_LENGTH + 1u);

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_OK, long_line,
              RX31_LONG_LINE_BYTES);
    provider.allocate_owned = 1;
    execute_case("parse pull a\nsay length(a)\n", &provider,
                 (const unsigned char *)"65536\n", 6u,
                 REXX_VM_STATUS_OK,
                 "a long bounded provider line is copied and parsed");
    check(provider.calls == 1u,
          "provider-owned allocation remains valid through VM copy");

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_OK, long_line,
              REXX_VM_MAX_PARSE_SOURCE_LENGTH);
    execute_case("pull\n", &provider, NULL, 0u, REXX_VM_STATUS_OK,
                 "the maximum runtime-sized line is accepted and discarded");

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_OK, long_line,
              REXX_VM_MAX_PARSE_SOURCE_LENGTH + 1u);
    execute_case(pull_source, &provider, NULL, 0u,
                 REXX_VM_STATUS_RESOURCE_LIMIT,
                 "oversized provider input is rejected without truncation");
    free(long_line);

    memset(&provider, 0, sizeof(provider));
    provider.fail_provider_allocation = 1;
    provider.allocate_owned = 1;
    add_input(&provider, REXX_VM_INPUT_READ_OK, valid,
              sizeof(valid) - 1u);
    check(compile_source("parse pull a\nsay a\n", &chunk),
          "provider-allocation recovery fixture compiles");
    if (chunk != NULL) {
        Rx31Capture capture;
        unsigned char capture_bytes[RX31_CAPTURE_CAPACITY];
        memset(&capture, 0, sizeof(capture));
        capture.bytes = capture_bytes;
        capture.capacity = sizeof(capture_bytes);
        memset(&vm, 0, sizeof(vm));
        status = rexx_vm_init(&vm, 0u);
        check(status == REXX_VM_STATUS_OK,
              "provider-allocation recovery VM initializes");
        if (status == REXX_VM_STATUS_OK) {
            rexx_vm_set_output_writer(&vm, capture_write, &capture);
            check(rexx_vm_set_input_reader(&vm, provider_read, &provider) ==
                      REXX_VM_STATUS_OK,
                  "provider-allocation recovery callback installs");
            status = rexx_vm_execute(&vm, chunk);
            check(status == REXX_VM_STATUS_INPUT_PROVIDER_ERROR,
                  "provider-owned allocation failure maps to provider error");
            status = rexx_vm_execute(&vm, chunk);
            check(status == REXX_VM_STATUS_OK && provider.calls == 2u &&
                      capture.length == sizeof(valid) &&
                      memcmp(capture.bytes, valid, sizeof(valid) - 1u) == 0 &&
                      capture.bytes[capture.length - 1u] == '\n',
                  "provider-owned allocation failure recovers on next read");
            rexx_vm_free(&vm);
            check(provider.owned_bytes != NULL,
                  "provider retains its last allocation across VM teardown");
            free(provider.owned_bytes);
            provider.owned_bytes = NULL;
        }
        rexx_bytecode_chunk_free(chunk);
        chunk = NULL;
    }

    /* The borrowed span is never freed by the VM; the fixture owns it until
     * after VM teardown, including on error paths. */
    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_OK, valid,
              sizeof(valid) - 1u);
    provider.allocate_owned = 1;
    check(compile_source(pull_source, &chunk),
          "ownership teardown fixture compiles");
    if (chunk == NULL) return;
    memset(&vm, 0, sizeof(vm));
    status = rexx_vm_init(&vm, 0u);
    check(status == REXX_VM_STATUS_OK, "ownership teardown VM initializes");
    if (status == REXX_VM_STATUS_OK) {
        check(rexx_vm_set_input_reader(&vm, provider_read, &provider) ==
                  REXX_VM_STATUS_OK,
              "provider with owned context installs");
        status = rexx_vm_execute(&vm, chunk);
        check(status == REXX_VM_STATUS_OK && provider.owned_bytes != NULL,
              "VM copied provider-owned bytes during PULL");
        rexx_vm_free(&vm);
        check(provider.owned_bytes != NULL,
              "VM teardown does not free provider-owned context memory");
        free(provider.owned_bytes);
        provider.owned_bytes = NULL;
    }
    rexx_bytecode_chunk_free(chunk);
}

static void test_vm_isolation_nested_interpret_and_lifetime(void)
{
    static const char pull_source[] = "parse pull value\nsay value\n";
    static const char nested_source[] =
        "call inner\n"
        "interpret 'parse pull dynamicValue; say dynamicValue'\n"
        "exit 0\n"
        "inner:\n"
        "parse pull nestedValue\n"
        "say nestedValue\n"
        "return\n";
    static const unsigned char a_line[] = "Provider A";
    static const unsigned char b_line[] = "Provider B";
    static const unsigned char b_next_line[] = "Provider B next";
    static const unsigned char a_expected[] = "Provider A\n";
    static const unsigned char b_expected[] = "Provider B\n";
    static const unsigned char b_next_expected[] = "Provider B next\n";
    static const unsigned char nested_expected[] =
        "Nested line\nInterpret line\n";
    Rx31Provider provider_a;
    Rx31Provider provider_b;
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm_a;
    RexxVM vm_b;
    RexxVMStatus status_a;
    RexxVMStatus status_b;
    Rx31Capture capture_a;
    Rx31Capture capture_b;
    unsigned char *bytes_a = (unsigned char *)malloc(RX31_CAPTURE_CAPACITY);
    unsigned char *bytes_b = (unsigned char *)malloc(RX31_CAPTURE_CAPACITY);

    check(bytes_a != NULL && bytes_b != NULL,
          "independent-VM output captures allocate");
    if (bytes_a == NULL || bytes_b == NULL) goto cleanup_buffers;
    memset(&provider_a, 0, sizeof(provider_a));
    memset(&provider_b, 0, sizeof(provider_b));
    add_input(&provider_a, REXX_VM_INPUT_READ_OK, a_line,
              sizeof(a_line) - 1u);
    add_input(&provider_b, REXX_VM_INPUT_READ_OK, b_line,
              sizeof(b_line) - 1u);
    add_input(&provider_b, REXX_VM_INPUT_READ_OK, b_next_line,
              sizeof(b_next_line) - 1u);
    memset(&vm_a, 0, sizeof(vm_a));
    memset(&vm_b, 0, sizeof(vm_b));
    memset(&capture_a, 0, sizeof(capture_a));
    memset(&capture_b, 0, sizeof(capture_b));
    capture_a.bytes = bytes_a;
    capture_a.capacity = RX31_CAPTURE_CAPACITY;
    capture_b.bytes = bytes_b;
    capture_b.capacity = RX31_CAPTURE_CAPACITY;
    check(compile_source(pull_source, &chunk),
          "isolation program compiles");
    if (chunk == NULL) goto cleanup_buffers;
    status_a = rexx_vm_init(&vm_a, 0u);
    status_b = rexx_vm_init(&vm_b, 0u);
    check(status_a == REXX_VM_STATUS_OK && status_b == REXX_VM_STATUS_OK,
          "two independent input VMs initialize");
    if (status_a != REXX_VM_STATUS_OK || status_b != REXX_VM_STATUS_OK)
        goto cleanup_vms;
    rexx_vm_set_output_writer(&vm_a, capture_write, &capture_a);
    rexx_vm_set_output_writer(&vm_b, capture_write, &capture_b);
    check(rexx_vm_set_input_reader(&vm_a, provider_read, &provider_a) ==
              REXX_VM_STATUS_OK &&
              rexx_vm_set_input_reader(&vm_b, provider_read, &provider_b) ==
              REXX_VM_STATUS_OK,
          "each VM receives an independent provider context");
    status_a = rexx_vm_execute(&vm_a, chunk);
    status_b = rexx_vm_execute(&vm_b, chunk);
    check(status_a == REXX_VM_STATUS_OK && status_b == REXX_VM_STATUS_OK,
          "isolated providers execute their own programs");
    check(capture_a.length == sizeof(a_expected) - 1u &&
              memcmp(capture_a.bytes, a_expected,
                     sizeof(a_expected) - 1u) == 0 &&
              capture_b.length == sizeof(b_expected) - 1u &&
              memcmp(capture_b.bytes, b_expected,
                     sizeof(b_expected) - 1u) == 0,
          "provider A bytes never appear in VM B and vice versa");
    check(provider_a.calls == 1u && provider_b.calls == 1u,
          "independent VM callbacks have independent call counts");
    check(rexx_data_stack_queue(&vm_a.data_stack, "Queue A", 7u) ==
              REXX_DATA_STACK_OK,
          "queue A remains local to VM A");
    capture_b.length = 0u;
    status_b = rexx_vm_execute(&vm_b, chunk);
    check(status_b == REXX_VM_STATUS_OK && provider_b.calls == 2u &&
              capture_b.length == sizeof(b_next_expected) - 1u &&
              memcmp(capture_b.bytes, b_next_expected,
                     sizeof(b_next_expected) - 1u) == 0,
          "queue A does not alter provider B input or execution");

    capture_a.length = 0u;
    status_a = rexx_vm_execute(&vm_a, chunk);
    check(status_a == REXX_VM_STATUS_OK && provider_a.calls == 1u &&
              capture_a.length == sizeof("Queue A\n") - 1u &&
              memcmp(capture_a.bytes, "Queue A\n",
                     sizeof("Queue A\n") - 1u) == 0,
          "VM A consumes its own queued bytes before its provider");

    rexx_bytecode_chunk_free(chunk);
    chunk = NULL;
    memset(&provider_a, 0, sizeof(provider_a));
    provider_a.allocate_owned = 1;
    add_input(&provider_a, REXX_VM_INPUT_READ_OK,
              (const unsigned char *)"Nested line", 11u);
    add_input(&provider_a, REXX_VM_INPUT_READ_OK,
              (const unsigned char *)"Interpret line", 14u);
    check(compile_source(nested_source, &chunk),
          "nested routine and INTERPRET program compiles");
    if (chunk != NULL) {
        capture_a.length = 0u;
        check(rexx_vm_set_input_reader(&vm_a, provider_read, &provider_a) ==
                  REXX_VM_STATUS_OK,
              "provider can be replaced between executions");
        status_a = rexx_vm_execute(&vm_a, chunk);
        check(status_a == REXX_VM_STATUS_OK && provider_a.calls == 2u,
              "nested routines and INTERPRET share the active VM provider");
        if (status_a != REXX_VM_STATUS_OK || provider_a.calls != 2u) {
            fprintf(stderr, "  nested status=%s calls=%lu message=%s\n",
                    rexx_vm_status_name(status_a),
                    (unsigned long)provider_a.calls,
                    vm_a.error.message != NULL ? vm_a.error.message : "(none)");
        }
        check(capture_a.length == sizeof(nested_expected) - 1u &&
                  memcmp(capture_a.bytes, nested_expected,
                         sizeof(nested_expected) - 1u) == 0,
              "nested and dynamic PULL use provider lines in order");
        if (capture_a.length != sizeof(nested_expected) - 1u ||
            memcmp(capture_a.bytes, nested_expected,
                   sizeof(nested_expected) - 1u) != 0)
            fprintf(stderr, "  nested output length=%lu output='%.*s'\n",
                    (unsigned long)capture_a.length,
                    (int)capture_a.length, capture_a.bytes);
        rexx_bytecode_chunk_free(chunk);
        chunk = NULL;
    }

    check(rexx_vm_set_input_reader(&vm_a, NULL, NULL) ==
              REXX_VM_STATUS_OK,
          "provider is removed safely while VM is quiescent");
    check(compile_source("pull\n", &chunk),
          "post-removal PULL program compiles");
    if (chunk != NULL) {
        check(rexx_vm_execute(&vm_a, chunk) ==
                  REXX_VM_STATUS_INPUT_UNAVAILABLE,
              "cleared provider returns the documented unavailable status");
    }

cleanup_vms:
    rexx_vm_free(&vm_a);
    rexx_vm_free(&vm_b);
    rexx_bytecode_chunk_free(chunk);
cleanup_buffers:
    free(bytes_a);
    free(bytes_b);
}

static void test_reentrant_replacement_and_error_recovery(void)
{
    static const char source[] = "parse pull value\nsay value\n";
    static const unsigned char ok_line[] = "Recovered";
    static const unsigned char recovered_output[] = "Recovered\n";
    Rx31Provider provider;
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm;
    RexxVMStatus status;
    Rx31Capture capture;
    unsigned char capture_bytes[RX31_CAPTURE_CAPACITY];

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_ERROR, NULL, 0u);
    add_input(&provider, REXX_VM_INPUT_READ_OK, ok_line,
              sizeof(ok_line) - 1u);
    memset(&vm, 0, sizeof(vm));
    memset(&capture, 0, sizeof(capture));
    capture.bytes = capture_bytes;
    capture.capacity = sizeof(capture_bytes);
    check(compile_source(source, &chunk), "recovery program compiles");
    if (chunk == NULL) return;
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "recovery VM initializes");
    rexx_vm_set_output_writer(&vm, capture_write, &capture);
    provider.mutation_vm = &vm;
    check(rexx_vm_set_input_reader(&vm, provider_read, &provider) ==
              REXX_VM_STATUS_OK,
          "recovery provider installs");
    status = rexx_vm_execute(&vm, chunk);
    check(status == REXX_VM_STATUS_INPUT_PROVIDER_ERROR,
          "provider error is propagated without fabricating a line");
    check(provider.mutation_status == REXX_VM_STATUS_INVALID_ARGUMENT,
          "provider replacement during execution is rejected");
    capture.length = 0u;
    provider.mutation_vm = NULL;
    status = rexx_vm_execute(&vm, chunk);
    check(status == REXX_VM_STATUS_OK && provider.calls == 2u,
          "VM and provider recover on the next program execution");
    check(capture.length == sizeof(recovered_output) - 1u &&
              memcmp(capture.bytes, recovered_output,
                     sizeof(recovered_output) - 1u) == 0,
          "post-error provider line parses through the same template engine");
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
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

static int append_text(char *buffer, size_t capacity, size_t *length,
                       const char *format, ...)
{
    va_list args;
    int written;
    if (buffer == NULL || length == NULL || *length >= capacity) return 0;
    va_start(args, format);
    written = vsnprintf(buffer + *length, capacity - *length, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= capacity - *length) return 0;
    *length += (size_t)written;
    return 1;
}

static void model_insert(char model[][16], size_t *count,
                         const char *value, int at_head)
{
    size_t index;
    if (at_head) {
        for (index = *count; index > 0u; --index)
            memcpy(model[index], model[index - 1u], sizeof(model[index]));
        ++*count;
        snprintf(model[0], sizeof(model[0]), "%s", value);
    } else {
        snprintf(model[*count], sizeof(model[*count]), "%s", value);
        ++*count;
    }
}

static void test_seeded_queue_provider_model(void)
{
    char source[32768];
    char expected[16384];
    char model[RX31_MODEL_CAPACITY][16];
    size_t source_length = 0u;
    size_t expected_length = 0u;
    size_t model_count = 0u;
    uint32_t state = RX31_PROPERTY_SEED;
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm;
    RexxVMStatus status;
    Rx31Capture capture;
    Rx31StreamProvider provider;
    unsigned char capture_bytes[RX31_CAPTURE_CAPACITY];
    size_t operation;

    memset(source, 0, sizeof(source));
    memset(expected, 0, sizeof(expected));
    memset(model, 0, sizeof(model));
    memset(&vm, 0, sizeof(vm));
    memset(&capture, 0, sizeof(capture));
    memset(&provider, 0, sizeof(provider));
    capture.bytes = capture_bytes;
    capture.capacity = sizeof(capture_bytes);
    for (operation = 0u; operation < RX31_PROPERTY_OPERATIONS; ++operation) {
        uint32_t choice = next_random(&state) % 3u;
        if (choice == 0u && model_count < RX31_MODEL_CAPACITY - 1u) {
            char value[16];
            snprintf(value, sizeof(value), "Q%03lu",
                     (unsigned long)operation);
            if (!append_text(source, sizeof(source), &source_length,
                             "queue '%s'\n", value)) break;
            model_insert(model, &model_count, value, 0);
        } else if (choice == 1u && model_count < RX31_MODEL_CAPACITY - 1u) {
            char value[16];
            snprintf(value, sizeof(value), "P%03lu",
                     (unsigned long)operation);
            if (!append_text(source, sizeof(source), &source_length,
                             "push '%s'\n", value)) break;
            model_insert(model, &model_count, value, 1);
        } else {
            char value[16];
            if (!append_text(source, sizeof(source), &source_length,
                             "parse pull item\nsay item\nsay queued()\n"))
                break;
            if (model_count != 0u) {
                snprintf(value, sizeof(value), "%s", model[0]);
                for (size_t index = 1u; index < model_count; ++index)
                    memcpy(model[index - 1u], model[index],
                           sizeof(model[index - 1u]));
                --model_count;
            } else {
                snprintf(value, sizeof(value), "Host%03u",
                         (unsigned int)provider.next_value++);
                /* stream_read increments next_value after formatting. */
                --provider.next_value;
            }
            if (!append_text(expected, sizeof(expected), &expected_length,
                             "%s\n%lu\n", value,
                             (unsigned long)model_count))
                break;
        }
        ++property_operations;
    }
    check(operation == RX31_PROPERTY_OPERATIONS,
          "fixed-seed queue/provider script stays within documented bounds");
    if (operation != RX31_PROPERTY_OPERATIONS) return;
    check(compile_source(source, &chunk),
          "fixed-seed queue/provider program compiles");
    if (chunk == NULL) return;
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "fixed-seed queue/provider VM initializes");
    rexx_vm_set_output_writer(&vm, capture_write, &capture);
    check(rexx_vm_set_input_reader(&vm, stream_read, &provider) ==
              REXX_VM_STATUS_OK,
          "fixed-seed provider installs");
    status = rexx_vm_execute(&vm, chunk);
    check(status == REXX_VM_STATUS_OK,
          "fixed-seed mixed queue/provider corpus executes");
    check(capture.length == expected_length &&
              memcmp(capture.bytes, expected, expected_length) == 0,
          "fixed-seed output matches independent queue/provider model");
    check(provider.calls <= RX31_PROPERTY_OPERATIONS,
          "provider is only called for empty-queue PULL operations");
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
}

static void test_vm_allocation_failure_recovery(void)
{
#if defined(REXX_VM_TEST_ALLOCATOR)
    static const char source[] = "parse pull item\nsay item\n";
    static const unsigned char first[] = "first";
    static const unsigned char recovered[] = "recovered";
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm;
    RexxVMStatus status;
    Rx31Provider provider;
    Rx31Capture capture;
    unsigned char capture_bytes[RX31_CAPTURE_CAPACITY];
    size_t allocation_count;
    size_t fail_at;

    rexx_vm_test_allocator_disable();
    check(compile_source(source, &chunk),
          "allocation-failure program compiles before VM fault injection");
    if (chunk == NULL) return;

    memset(&provider, 0, sizeof(provider));
    add_input(&provider, REXX_VM_INPUT_READ_OK, first, sizeof(first) - 1u);
    memset(&vm, 0, sizeof(vm));
    memset(&capture, 0, sizeof(capture));
    capture.bytes = capture_bytes;
    capture.capacity = sizeof(capture_bytes);
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "allocation-count VM initializes");
    rexx_vm_set_output_writer(&vm, capture_write, &capture);
    rexx_vm_set_input_reader(&vm, provider_read, &provider);
    rexx_vm_test_allocator_fail_on(SIZE_MAX);
    status = rexx_vm_execute(&vm, chunk);
    allocation_count = rexx_vm_test_allocator_attempts();
    check(status == REXX_VM_STATUS_OK && allocation_count > 0u,
          "successful provider parse exposes VM allocation stages");
    rexx_vm_free(&vm);
    check(rexx_vm_test_allocator_live_blocks() == 0u,
          "successful provider parse releases all VM-owned allocations");

    for (fail_at = 1u; fail_at <= allocation_count; ++fail_at) {
        memset(&provider, 0, sizeof(provider));
        add_input(&provider, REXX_VM_INPUT_READ_OK,
                  first, sizeof(first) - 1u);
        add_input(&provider, REXX_VM_INPUT_READ_OK,
                  recovered, sizeof(recovered) - 1u);
        memset(&vm, 0, sizeof(vm));
        memset(&capture, 0, sizeof(capture));
        capture.bytes = capture_bytes;
        capture.capacity = sizeof(capture_bytes);
        check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
              "fault-injection VM initializes");
        rexx_vm_set_output_writer(&vm, capture_write, &capture);
        rexx_vm_set_input_reader(&vm, provider_read, &provider);
        rexx_vm_test_allocator_fail_on(fail_at);
        status = rexx_vm_execute(&vm, chunk);
        rexx_vm_test_allocator_disable();
        check(status == REXX_VM_STATUS_OUT_OF_MEMORY && provider.calls == 1u,
              "each provider parse allocation failure consumes one acquired line");
        capture.length = 0u;
        status = rexx_vm_execute(&vm, chunk);
        check(status == REXX_VM_STATUS_OK && provider.calls == 2u &&
                  capture.length == sizeof(recovered) &&
                  memcmp(capture.bytes, recovered,
                         sizeof(recovered) - 1u) == 0 &&
                  capture.bytes[capture.length - 1u] == '\n',
              "VM recovers with the next provider line after allocation failure");
        rexx_vm_free(&vm);
        check(rexx_vm_test_allocator_live_blocks() == 0u,
              "allocation failure and recovery leave no VM allocation leaks");
    }
    rexx_vm_test_allocator_disable();
    rexx_bytecode_chunk_free(chunk);
#else
    check(0, "RX31 allocation-failure test requires REXX_VM_TEST_ALLOCATOR");
#endif
}

int main(void)
{
    rexx_vm_test_allocator_disable();
    test_case_semantics_and_binary_safety();
    test_queue_priority_and_provider_statuses();
    test_validation_limits_and_provider_ownership();
    test_vm_isolation_nested_interpret_and_lifetime();
    test_reentrant_replacement_and_error_recovery();
    test_seeded_queue_provider_model();
    test_vm_allocation_failure_recovery();
    rexx_vm_test_allocator_disable();
    printf("RX31 host input: seed=0x%08x operations=%lu assertions=%lu failures=%lu\n",
           (unsigned int)RX31_PROPERTY_SEED,
           (unsigned long)property_operations,
           (unsigned long)assertions,
           (unsigned long)failures);
    return failures == 0u ? 0 : 1;
}
