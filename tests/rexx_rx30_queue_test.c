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

#define RX30_CAPTURE_CAPACITY ((size_t)262144u)
#define RX30_PROPERTY_SEED UINT32_C(0x52583331)
#define RX30_PROPERTY_OPERATIONS ((size_t)1200u)
#define RX30_MODEL_CAPACITY ((size_t)64u)
#define RX30_MODEL_ITEM_BYTES ((size_t)8u)

static size_t assertions;
static size_t failures;
static size_t property_operations;

typedef struct Rx30Capture {
    unsigned char bytes[RX30_CAPTURE_CAPACITY];
    size_t length;
} Rx30Capture;

typedef struct Rx30ModelItem {
    unsigned char bytes[RX30_MODEL_ITEM_BYTES];
    size_t length;
} Rx30ModelItem;

static void check(int condition, const char *description)
{
    ++assertions;
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL: %s\n", description);
}

static int capture_write(void *user_data, const char *bytes, size_t length)
{
    Rx30Capture *capture = (Rx30Capture *)user_data;
    if (capture == NULL || (bytes == NULL && length != 0u) ||
        length > RX30_CAPTURE_CAPACITY - capture->length)
        return 0;
    if (length != 0u)
        memcpy(capture->bytes + capture->length, bytes, length);
    capture->length += length;
    return 1;
}

static int compile_source(const char *source, RexxBytecodeChunk **chunk_out,
                          RexxParserError *parser_error_out,
                          RexxCompilerError *compiler_error_out)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxParserError parser_error;
    RexxCompilerError compiler_error;
    RexxLexerStatus lexer_status;
    RexxParserStatus parser_status;
    RexxCompilerStatus compiler_status;

    if (chunk_out == NULL || source == NULL) return 0;
    *chunk_out = NULL;
    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    memset(&parser_error, 0, sizeof(parser_error));
    memset(&compiler_error, 0, sizeof(compiler_error));
    lexer_status = rexx_lexer_init(&lexer, source, strlen(source));
    if (lexer_status != REXX_LEXER_STATUS_OK) return 0;
    parser_status = rexx_parser_init(&parser, &lexer);
    if (parser_status != REXX_PARSER_STATUS_OK) return 0;
    parser_status = rexx_parser_parse(&parser, &program);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        if (parser_error_out != NULL) {
            const RexxParserError *error = rexx_parser_get_error(&parser);
            if (error != NULL) *parser_error_out = *error;
        }
        rexx_parser_free_ast(program);
        return 0;
    }
    compiler_status = rexx_compiler_compile(program, chunk_out,
                                             &compiler_error);
    rexx_parser_free_ast(program);
    if (compiler_status != REXX_COMPILER_STATUS_OK) {
        if (compiler_error_out != NULL) *compiler_error_out = compiler_error;
        rexx_bytecode_chunk_free(*chunk_out);
        *chunk_out = NULL;
        return 0;
    }
    return 1;
}

static int run_case(const char *source, const char *expected_output,
                    RexxVMStatus expected_status, const char *description)
{
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm;
    RexxVMStatus status;
    Rx30Capture capture;
    RexxParserError parser_error;
    RexxCompilerError compiler_error;
    size_t expected_length = expected_output != NULL
        ? strlen(expected_output) : 0u;
    int compiled;

    memset(&vm, 0, sizeof(vm));
    memset(&capture, 0, sizeof(capture));
    memset(&parser_error, 0, sizeof(parser_error));
    memset(&compiler_error, 0, sizeof(compiler_error));
    compiled = compile_source(source, &chunk, &parser_error,
                              &compiler_error);
    check(compiled, "RX30 program compiles");
    if (!compiled) {
        fprintf(stderr,
                "  case=%s parser=%d line=%lu column=%lu %s compiler=%d %s\n",
                description, (int)parser_error.status,
                (unsigned long)parser_error.token.line,
                (unsigned long)parser_error.token.column,
                parser_error.message != NULL ? parser_error.message : "",
                (int)compiler_error.status,
                compiler_error.message != NULL ? compiler_error.message : "");
        return 0;
    }
    status = rexx_vm_init(&vm, 0u);
    check(status == REXX_VM_STATUS_OK, "RX30 VM initializes");
    if (status != REXX_VM_STATUS_OK) {
        rexx_bytecode_chunk_free(chunk);
        return 0;
    }
    rexx_vm_set_output_writer(&vm, capture_write, &capture);
    status = rexx_vm_execute(&vm, chunk);
    check(status == expected_status, description);
    if (status != expected_status) {
        fprintf(stderr, "  expected=%s actual=%s message=%s\n",
                rexx_vm_status_name(expected_status),
                rexx_vm_status_name(status),
                vm.error.message != NULL ? vm.error.message : "(none)");
    }
    if (expected_status == REXX_VM_STATUS_OK && expected_output != NULL) {
        check(capture.length == expected_length &&
                  (expected_length == 0u ||
                   memcmp(capture.bytes, expected_output,
                          expected_length) == 0),
              "RX30 executable output matches expected bytes");
        if (capture.length != expected_length ||
            (expected_length != 0u &&
             memcmp(capture.bytes, expected_output,
                    expected_length) != 0)) {
            fprintf(stderr, "  expected output (%lu bytes): %s"
                    "  actual output (%lu bytes): %.*s\n",
                    (unsigned long)expected_length, expected_output,
                    (unsigned long)capture.length,
                    (int)capture.length, capture.bytes);
        }
    }
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    return status == expected_status;
}

static void test_required_examples(void)
{
    static const char basic[] =
        "say queued()\n"
        "queue 'Alpha'\n"
        "queue 'Beta'\n"
        "push 'Gamma'\n"
        "say queued()\n"
        "parse pull first\n"
        "say first\n"
        "pull second\n"
        "say second\n"
        "parse pull third\n"
        "say third\n"
        "say queued()\n";
    static const char basic_expected[] =
        "0\n3\nGamma\nALPHA\nBeta\n0\n";
    static const char case_and_empty[] =
        "queue 'MiXeD'\n"
        "parse pull a\n"
        "say a\n"
        "queue 'MiXeD'\n"
        "pull b\n"
        "say b\n"
        "push\n"
        "say queued()\n"
        "parse pull emptyLine\n"
        "say length(emptyLine)\n"
        "say queued()\n";
    static const char case_expected[] =
        "MiXeD\nMIXED\n1\n0\n0\n";

    (void)run_case(basic, basic_expected, REXX_VM_STATUS_OK,
                   "required FIFO/LIFO example executes");
    (void)run_case(case_and_empty, case_expected, REXX_VM_STATUS_OK,
                   "required case and empty-string example executes");
}

static void test_expressions_templates_and_binary(void)
{
    static const char source[] =
        "queue 'Com' || 'puted'\n"
        "push 3 + 4\n"
        "parse pull number\n"
        "say number\n"
        "parse pull computed\n"
        "say computed\n"
        "queue 'one two three'\n"
        "parse pull first . third\n"
        "say first||'/'||third\n"
        "queue 'A B C'\n"
        "parse pull x y\n"
        "say x||'/'||y\n"
        "queue '  MiXeD text  '\n"
        "parse pull wholeLine\n"
        "say '['||wholeLine||']'\n"
        "queue 'abcdef'\n"
        "parse pull before 4 after\n"
        "say before||'/'||after\n"
        "queue '00617AFF80'X\n"
        "parse pull raw\n"
        "say c2x(raw)\n"
        "queue '00617AFF80'X\n"
        "pull upperRaw\n"
        "say c2x(upperRaw)\n"
        "queue 'discard me'\n"
        "pull\n"
        "say queued()\n";
    static const char expected[] =
        "7\nComputed\none/three\nA/B C\n[  MiXeD text  ]\n"
        "abc/def\n00617AFF80\n00415AFF80\n0\n";

    (void)run_case(source, expected, REXX_VM_STATUS_OK,
                   "templates, positions, spaces, and binary queue values execute");
}

static void test_internal_interpret_and_lifecycle(void)
{
    static const char routines[] =
        "call rx30_outer\n"
        "say queued()\n"
        "parse pull first\n"
        "say first\n"
        "parse pull second\n"
        "say second\n"
        "interpret \"queue 'Dynamic'\"\n"
        "parse pull third\n"
        "say third\n"
        "exit 0\n"
        "rx30_outer:\n"
        "call rx30_inner\n"
        "queue 'Outer'\n"
        "return\n"
        "rx30_inner:\n"
        "push 'Inner'\n"
        "return\n";
    static const char routines_expected[] =
        "2\nInner\nOuter\nDynamic\n";
    static const char first_program[] = "queue 'Persisted'\n";
    static const char second_program[] =
        "say queued()\nparse pull line\nsay line\nsay queued()\n";
    static const char second_expected[] = "1\nPersisted\n0\n";
    RexxVM vm;
    RexxBytecodeChunk *first_chunk = NULL;
    RexxBytecodeChunk *second_chunk = NULL;
    Rx30Capture capture;
    RexxVMStatus status;
    int first_ok;
    int second_ok;

    (void)run_case(routines, routines_expected, REXX_VM_STATUS_OK,
                   "nested internal calls and INTERPRET share the VM data stack");

    memset(&vm, 0, sizeof(vm));
    memset(&capture, 0, sizeof(capture));
    first_ok = compile_source(first_program, &first_chunk, NULL, NULL);
    second_ok = compile_source(second_program, &second_chunk, NULL, NULL);
    check(first_ok && second_ok, "two lifecycle chunks compile");
    if (!first_ok || !second_ok) goto cleanup;
    status = rexx_vm_init(&vm, 0u);
    check(status == REXX_VM_STATUS_OK, "lifecycle VM initializes");
    if (status != REXX_VM_STATUS_OK) goto cleanup;
    rexx_vm_set_output_writer(&vm, capture_write, &capture);
    status = rexx_vm_execute(&vm, first_chunk);
    check(status == REXX_VM_STATUS_OK, "first program execution queues data");
    status = rexx_vm_execute(&vm, second_chunk);
    check(status == REXX_VM_STATUS_OK,
          "second program execution sees the same VM queue");
    check(capture.length == sizeof(second_expected) - 1u &&
              memcmp(capture.bytes, second_expected,
                     sizeof(second_expected) - 1u) == 0,
          "external stack persists across executions until VM teardown");

cleanup:
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(first_chunk);
    rexx_bytecode_chunk_free(second_chunk);
}

static void test_independent_vms_and_empty_input_policy(void)
{
    static const char insert[] = "queue 'OnlyThisVM'\n";
    static const char pull[] = "parse pull item\n";
    static const char empty_pull[] = "pull\n";
    RexxVM first;
    RexxVM second;
    RexxBytecodeChunk *insert_chunk = NULL;
    RexxBytecodeChunk *pull_chunk = NULL;
    RexxBytecodeChunk *empty_chunk = NULL;
    uint32_t first_count = UINT32_MAX;
    uint32_t second_count = UINT32_MAX;
    RexxVMStatus status;
    int compiled = compile_source(insert, &insert_chunk, NULL, NULL) &&
                   compile_source(pull, &pull_chunk, NULL, NULL) &&
                   compile_source(empty_pull, &empty_chunk, NULL, NULL);

    check(compiled, "VM isolation programs compile");
    if (!compiled) goto cleanup;
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    check(rexx_vm_init(&first, 0u) == REXX_VM_STATUS_OK,
          "first isolated VM initializes");
    check(rexx_vm_init(&second, 0u) == REXX_VM_STATUS_OK,
          "second isolated VM initializes");
    status = rexx_vm_execute(&first, insert_chunk);
    check(status == REXX_VM_STATUS_OK, "first VM queues its own entry");
    check(rexx_data_stack_get_count(&first.data_stack, &first_count) ==
              REXX_DATA_STACK_OK && first_count == 1u,
          "first VM reports one queued entry");
    check(rexx_data_stack_get_count(&second.data_stack, &second_count) ==
              REXX_DATA_STACK_OK && second_count == 0u,
          "second VM remains isolated with zero entries");
    status = rexx_vm_execute(&second, empty_chunk);
    check(status == REXX_VM_STATUS_INPUT_UNAVAILABLE &&
              second.error.message != NULL &&
              strstr(second.error.message, "no input provider") != NULL,
          "empty PULL fails immediately with explicit unavailable input");
    status = rexx_vm_execute(&first, pull_chunk);
    check(status == REXX_VM_STATUS_OK, "first VM can consume its own entry");
    check(rexx_data_stack_get_count(&first.data_stack, &first_count) ==
              REXX_DATA_STACK_OK && first_count == 0u,
          "first VM removal leaves its stack empty");
    rexx_vm_free(&first);
    rexx_vm_free(&second);

cleanup:
    rexx_bytecode_chunk_free(insert_chunk);
    rexx_bytecode_chunk_free(pull_chunk);
    rexx_bytecode_chunk_free(empty_chunk);
}

static void test_errors_and_parser_recovery(void)
{
    static const char trapped[] =
        "signal on syntax name recovered\n"
        "queue 1 / 0\n"
        "say 'unreachable'\n"
        "recovered:\n"
        "say queued()\n"
        "queue 'after-error'\n"
        "pull recoveredValue\n"
        "say recoveredValue\n";
    static const char trapped_expected[] = "0\nAFTER-ERROR\n";
    static const char malformed[] =
        "queue 'must remain'\n"
        "parse pull a (unterminated\n";
    static const char dynamic_bad[] =
        "queue 'must remain'\n"
        "interpret \"parse pull a (unterminated\"\n";
    static const char valid_recovery[] = "parse pull recoveredValue\nsay recoveredValue\n";
    static const char recovery_expected[] = "must remain\n";
    RexxParserError parser_error;
    RexxBytecodeChunk *bad_chunk = NULL;
    RexxBytecodeChunk *dynamic_chunk = NULL;
    RexxBytecodeChunk *recovery_chunk = NULL;
    RexxVM vm;
    RexxVMStatus status;
    Rx30Capture capture;
    uint32_t count = UINT32_MAX;

    (void)run_case(trapped, trapped_expected, REXX_VM_STATUS_OK,
                   "trapped expression failure leaves no phantom entry and recovers");

    memset(&parser_error, 0, sizeof(parser_error));
    check(!compile_source(malformed, &bad_chunk, &parser_error, NULL) &&
              parser_error.status == REXX_PARSER_STATUS_SYNTAX_ERROR &&
              parser_error.token.line == 2u,
          "malformed PARSE PULL template reports its original source line");
    rexx_bytecode_chunk_free(bad_chunk);

    memset(&vm, 0, sizeof(vm));
    memset(&capture, 0, sizeof(capture));
    check(compile_source(dynamic_bad, &dynamic_chunk, NULL, NULL),
          "INTERPRET malformed-template outer program compiles");
    check(compile_source(valid_recovery, &recovery_chunk, NULL, NULL),
          "valid parser recovery program compiles");
    if (dynamic_chunk == NULL || recovery_chunk == NULL) goto cleanup;
    status = rexx_vm_init(&vm, 0u);
    check(status == REXX_VM_STATUS_OK, "dynamic recovery VM initializes");
    if (status != REXX_VM_STATUS_OK) goto cleanup;
    rexx_vm_set_output_writer(&vm, capture_write, &capture);
    status = rexx_vm_execute(&vm, dynamic_chunk);
    check(status == REXX_VM_STATUS_DYNAMIC_SYNTAX,
          "malformed dynamic template raises a compile-time syntax failure");
    check(rexx_data_stack_get_count(&vm.data_stack, &count) ==
              REXX_DATA_STACK_OK && count == 1u,
          "dynamic compilation failure does not consume queued input");
    status = rexx_vm_execute(&vm, recovery_chunk);
    check(status == REXX_VM_STATUS_OK,
          "valid parsing succeeds after malformed INTERPRET recovery");
    check(capture.length == sizeof(recovery_expected) - 1u &&
              memcmp(capture.bytes, recovery_expected,
                     sizeof(recovery_expected) - 1u) == 0,
          "recovered PULL returns the retained queue element");
    if (capture.length != sizeof(recovery_expected) - 1u ||
        memcmp(capture.bytes, recovery_expected,
               sizeof(recovery_expected) - 1u) != 0) {
        fprintf(stderr, "  recovered output length=%lu status=%s\n",
                (unsigned long)capture.length, rexx_vm_status_name(status));
        fprintf(stderr, "  recovered output='%.*s' expected='%s'\n",
                (int)capture.length, capture.bytes, recovery_expected);
    }

cleanup:
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(dynamic_chunk);
    rexx_bytecode_chunk_free(recovery_chunk);
}

static void exercise_pull_failure_matrix(const RexxBytecodeChunk *pull_chunk,
                                         const RexxBytecodeChunk *recovery_chunk,
                                         const char *operation_name)
{
#if defined(REXX_VM_TEST_ALLOCATOR)
    RexxVM vm;
    RexxVMStatus status;
    uint32_t count = UINT32_MAX;
    size_t attempts_before;
    size_t allocation_count = 0u;
    size_t fail_at;

    rexx_vm_test_allocator_disable();
    memset(&vm, 0, sizeof(vm));
    status = rexx_vm_init(&vm, 0u);
    check(status == REXX_VM_STATUS_OK, "PULL probe VM initializes");
    if (status != REXX_VM_STATUS_OK) return;
    check(rexx_data_stack_queue(&vm.data_stack, "allocation", 10u) ==
              REXX_DATA_STACK_OK,
          "PULL probe preloads an owned input element");
    attempts_before = rexx_vm_test_allocator_attempts();
    status = rexx_vm_execute(&vm, pull_chunk);
    allocation_count = rexx_vm_test_allocator_attempts() - attempts_before;
    check(status == REXX_VM_STATUS_OK && allocation_count >= 2u,
          "successful PULL run exposes source-copy and parse/assignment allocation stages");
    rexx_vm_free(&vm);
    check(rexx_vm_test_allocator_live_blocks() == 0u,
          "successful PULL probe releases all VM allocations");

    for (fail_at = 1u; fail_at <= allocation_count; ++fail_at) {
        rexx_vm_test_allocator_disable();
        memset(&vm, 0, sizeof(vm));
        status = rexx_vm_init(&vm, 0u);
        check(status == REXX_VM_STATUS_OK, "PULL fault-point VM initializes");
        if (status != REXX_VM_STATUS_OK) break;
        check(rexx_data_stack_queue(&vm.data_stack, "allocation", 10u) ==
                  REXX_DATA_STACK_OK,
              "PULL fault-point VM receives queued input");
        rexx_vm_test_allocator_fail_on(fail_at);
        status = rexx_vm_execute(&vm, pull_chunk);
        rexx_vm_test_allocator_disable();
        check(status == REXX_VM_STATUS_OUT_OF_MEMORY,
              "each PULL transformation/parse/assignment allocation failure is reported");
        check(rexx_data_stack_get_count(&vm.data_stack, &count) ==
                  REXX_DATA_STACK_OK && count == 0u,
              "PULL consumes input before allocation/template work");
        status = rexx_vm_execute(&vm, recovery_chunk);
        check(status == REXX_VM_STATUS_OK,
              "VM executes QUEUE successfully after PULL failure");
        rexx_vm_free(&vm);
        check(rexx_vm_test_allocator_live_blocks() == 0u,
              "PULL failure and recovery release all VM blocks");
    }
    if (allocation_count < 2u)
        check(0, operation_name);
#else
    (void)pull_chunk;
    (void)recovery_chunk;
    check(0, operation_name);
#endif
}

static void test_failure_atomicity(void)
{
#if defined(REXX_VM_TEST_ALLOCATOR)
    static const char queue_source[] = "queue 'allocation'\n";
    static const char parse_pull_source[] = "parse pull pulledValue\n";
    static const char upper_pull_source[] = "pull pulledValue\n";
    static const char recovery_source[] = "queue 'recovered'\n";
    RexxBytecodeChunk *queue_chunk = NULL;
    RexxBytecodeChunk *parse_pull_chunk = NULL;
    RexxBytecodeChunk *upper_pull_chunk = NULL;
    RexxBytecodeChunk *recovery_chunk = NULL;
    RexxVM vm;
    RexxVMStatus status;
    uint32_t count = UINT32_MAX;
    size_t attempts_before;
    size_t allocation_count;
    size_t fail_at;
    int compiled = compile_source(queue_source, &queue_chunk, NULL, NULL) &&
                   compile_source(parse_pull_source, &parse_pull_chunk, NULL, NULL) &&
                   compile_source(upper_pull_source, &upper_pull_chunk, NULL, NULL) &&
                   compile_source(recovery_source, &recovery_chunk, NULL, NULL);

    check(compiled, "fault-injection programs compile");
    if (!compiled) goto cleanup_chunks;
    rexx_vm_test_allocator_disable();
    memset(&vm, 0, sizeof(vm));
    status = rexx_vm_init(&vm, 0u);
    check(status == REXX_VM_STATUS_OK, "queue-failure VM initializes");
    if (status != REXX_VM_STATUS_OK) goto cleanup_vm;
    attempts_before = rexx_vm_test_allocator_attempts();
    status = rexx_vm_execute(&vm, queue_chunk);
    allocation_count = rexx_vm_test_allocator_attempts() - attempts_before;
    check(status == REXX_VM_STATUS_OK && allocation_count >= 4u,
          "successful QUEUE run exposes conversion and node allocation stages");
    check(rexx_data_stack_get_count(&vm.data_stack, &count) ==
              REXX_DATA_STACK_OK && count == 1u,
          "successful allocation probe inserted one real item");
    rexx_vm_free(&vm);
    check(rexx_vm_test_allocator_live_blocks() == 0u,
          "successful allocation probe releases all VM allocations");

    for (fail_at = 1u; fail_at <= allocation_count; ++fail_at) {
        memset(&vm, 0, sizeof(vm));
        status = rexx_vm_init(&vm, 0u);
        check(status == REXX_VM_STATUS_OK,
              "queue fault-point VM initializes");
        if (status != REXX_VM_STATUS_OK) break;
        rexx_vm_test_allocator_fail_on(fail_at);
        status = rexx_vm_execute(&vm, queue_chunk);
        rexx_vm_test_allocator_disable();
        check(status == REXX_VM_STATUS_OUT_OF_MEMORY,
              "each QUEUE conversion/node allocation failure is reported");
        check(rexx_data_stack_get_count(&vm.data_stack, &count) ==
                  REXX_DATA_STACK_OK && count == 0u,
              "failed QUEUE insertion leaves no element or count drift");
        status = rexx_vm_execute(&vm, recovery_chunk);
        check(status == REXX_VM_STATUS_OK &&
                  rexx_data_stack_get_count(&vm.data_stack, &count) ==
                      REXX_DATA_STACK_OK && count == 1u,
              "QUEUE insertion succeeds after allocator recovery");
        rexx_vm_free(&vm);
        check(rexx_vm_test_allocator_live_blocks() == 0u,
              "QUEUE allocation failure and recovery release all blocks");
    }
    check(allocation_count >= 4u,
          "QUEUE fault matrix includes the final queue-node allocation");

    exercise_pull_failure_matrix(
        parse_pull_chunk, recovery_chunk,
        "PARSE PULL fault matrix reaches input-copy and parse allocation stages");
    exercise_pull_failure_matrix(
        upper_pull_chunk, recovery_chunk,
        "PULL fault matrix reaches uppercase input-copy and parse allocation stages");

cleanup_vm:
    rexx_vm_test_allocator_disable();
cleanup_chunks:
    rexx_bytecode_chunk_free(queue_chunk);
    rexx_bytecode_chunk_free(parse_pull_chunk);
    rexx_bytecode_chunk_free(upper_pull_chunk);
    rexx_bytecode_chunk_free(recovery_chunk);
#else
    check(0, "RX30 failure matrix requires REXX_VM_TEST_ALLOCATOR");
#endif
}

static int append_source(char *buffer, size_t capacity, size_t *length,
                         const char *format, ...)
{
    va_list arguments;
    int written;
    if (*length >= capacity) return 0;
    va_start(arguments, format);
    written = vsnprintf(buffer + *length, capacity - *length, format,
                        arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= capacity - *length) return 0;
    *length += (size_t)written;
    return 1;
}

static void append_expected_line(char *buffer, size_t capacity, size_t *length,
                                 const char *bytes, size_t byte_length)
{
    if (byte_length > capacity - *length - 1u) {
        check(0, "property expected-output buffer has capacity");
        return;
    }
    if (byte_length != 0u) memcpy(buffer + *length, bytes, byte_length);
    *length += byte_length;
    buffer[(*length)++] = '\n';
    buffer[*length] = '\0';
}

static uint32_t property_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void model_push(Rx30ModelItem *items, size_t *count,
                       const unsigned char *bytes, size_t length)
{
    size_t index = *count;
    while (index != 0u) {
        items[index] = items[index - 1u];
        --index;
    }
    if (length != 0u) memcpy(items[0].bytes, bytes, length);
    items[0].length = length;
    ++*count;
}

static void model_queue(Rx30ModelItem *items, size_t *count,
                        const unsigned char *bytes, size_t length)
{
    if (length != 0u) memcpy(items[*count].bytes, bytes, length);
    items[*count].length = length;
    ++*count;
}

static void model_take(Rx30ModelItem *items, size_t *count,
                       int uppercase, char *hex, size_t *hex_length)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t index;
    *hex_length = 0u;
    for (index = 0u; index < items[0].length; ++index) {
        unsigned char byte = items[0].bytes[index];
        if (uppercase && byte >= (unsigned char)'a' &&
            byte <= (unsigned char)'z')
            byte = (unsigned char)(byte - (unsigned char)'a' +
                                   (unsigned char)'A');
        hex[(*hex_length)++] = digits[byte >> 4];
        hex[(*hex_length)++] = digits[byte & 0x0fu];
    }
    hex[*hex_length] = '\0';
    for (index = 1u; index < *count; ++index) items[index - 1u] = items[index];
    --*count;
}

static int append_hex_literal(char *source, size_t capacity, size_t *length,
                              const unsigned char *bytes, size_t byte_length)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t index;
    if (!append_source(source, capacity, length, "queue '") ||
        byte_length > 8u) return 0;
    for (index = 0u; index < byte_length; ++index) {
        char pair[3];
        pair[0] = digits[bytes[index] >> 4];
        pair[1] = digits[bytes[index] & 0x0fu];
        pair[2] = '\0';
        if (!append_source(source, capacity, length, "%s", pair)) return 0;
    }
    return append_source(source, capacity, length, "'X\n");
}

static void test_deterministic_language_model(void)
{
    static const unsigned char routine_inner[] = { 'I' };
    static const unsigned char routine_outer[] = { 'R' };
    char source[RX30_CAPTURE_CAPACITY];
    char expected[RX30_CAPTURE_CAPACITY];
    size_t source_length = 0u;
    size_t expected_length = 0u;
    Rx30ModelItem model[RX30_MODEL_CAPACITY];
    size_t model_count = 0u;
    uint32_t state = RX30_PROPERTY_SEED;
    size_t operation;
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm;
    Rx30Capture capture;
    RexxVMStatus status;
    int source_ok = 1;

    memset(source, 0, sizeof(source));
    memset(expected, 0, sizeof(expected));
    memset(model, 0, sizeof(model));
    for (operation = 0u; operation < RX30_PROPERTY_OPERATIONS; ++operation) {
        uint32_t choice = property_random(&state) % 7u;
        unsigned char bytes[RX30_MODEL_ITEM_BYTES];
        size_t byte_length = property_random(&state) %
                             (RX30_MODEL_ITEM_BYTES + 1u);
        size_t index;
        char line[32];

        for (index = 0u; index < byte_length; ++index)
            bytes[index] = (unsigned char)property_random(&state);
        if (model_count >= RX30_MODEL_CAPACITY - 2u &&
            (choice == 0u || choice == 1u || choice == 4u || choice == 5u))
            choice = 2u;
        if (choice == 0u) {
            source_ok = append_hex_literal(source, sizeof(source),
                                           &source_length, bytes, byte_length);
            model_queue(model, &model_count, bytes, byte_length);
        } else if (choice == 1u) {
            source_ok = append_source(source, sizeof(source), &source_length,
                                      "push '");
            for (index = 0u; source_ok && index < byte_length; ++index)
                source_ok = append_source(
                    source, sizeof(source), &source_length, "%02X",
                    (unsigned int)bytes[index]);
            if (source_ok)
                source_ok = append_source(source, sizeof(source),
                                          &source_length, "'X\n");
            model_push(model, &model_count, bytes, byte_length);
        } else if (choice == 2u || choice == 3u) {
            int uppercase = choice == 3u;
            size_t hex_length = 0u;
            if (model_count == 0u) {
                source_ok = append_hex_literal(source, sizeof(source),
                                               &source_length, bytes,
                                               byte_length);
                model_queue(model, &model_count, bytes, byte_length);
            } else {
                source_ok = append_source(source, sizeof(source),
                    &source_length, uppercase
                        ? "pull rx30_value\n"
                        : "parse pull rx30_value\n");
                if (source_ok)
                    source_ok = append_source(source, sizeof(source),
                                              &source_length,
                                              "say c2x(rx30_value)\n");
                model_take(model, &model_count, uppercase, line, &hex_length);
                append_expected_line(expected, sizeof(expected),
                                     &expected_length, line, hex_length);
            }
        } else if (choice == 4u) {
            source_ok = append_source(source, sizeof(source), &source_length,
                                      "call rx30_outer\n");
            model_push(model, &model_count, routine_inner,
                       sizeof(routine_inner));
            model_queue(model, &model_count, routine_outer,
                        sizeof(routine_outer));
        } else if (choice == 5u) {
            source_ok = append_source(source, sizeof(source), &source_length,
                                      "interpret \"queue '");
            for (index = 0u; source_ok && index < byte_length; ++index)
                source_ok = append_source(
                    source, sizeof(source), &source_length, "%02X",
                    (unsigned int)bytes[index]);
            if (source_ok)
                source_ok = append_source(source, sizeof(source),
                                          &source_length, "'X\"\n");
            model_queue(model, &model_count, bytes, byte_length);
        } else {
            source_ok = 1;
        }
        if (!source_ok) {
            check(0, "generated deterministic REXX source fits its bound");
            return;
        }
        source_ok = append_source(source, sizeof(source), &source_length,
                                  "say queued()\n");
        if (!source_ok) {
            check(0, "generated count assertion fits the source bound");
            return;
        }
        snprintf(line, sizeof(line), "%lu", (unsigned long)model_count);
        append_expected_line(expected, sizeof(expected),
                             &expected_length, line, strlen(line));
        ++property_operations;
    }
    source_ok = append_source(source, sizeof(source), &source_length,
        "exit 0\n"
        "rx30_outer:\n"
        "call rx30_inner\n"
        "queue '52'X\n"
        "return\n"
        "rx30_inner:\n"
        "push '49'X\n"
        "return\n");
    check(source_ok, "generated routine and INTERPRET corpus completes");
    if (!source_ok) return;

    check(compile_source(source, &chunk, NULL, NULL),
          "fixed-seed executable queue property corpus compiles");
    if (chunk == NULL) return;
    memset(&vm, 0, sizeof(vm));
    memset(&capture, 0, sizeof(capture));
    status = rexx_vm_init(&vm, 0u);
    check(status == REXX_VM_STATUS_OK,
          "property-corpus VM initializes");
    if (status != REXX_VM_STATUS_OK) goto cleanup;
    rexx_vm_set_output_writer(&vm, capture_write, &capture);
    status = rexx_vm_execute(&vm, chunk);
    check(status == REXX_VM_STATUS_OK,
          "fixed-seed queue property corpus executes");
    check(capture.length == expected_length &&
              memcmp(capture.bytes, expected, expected_length) == 0,
          "fixed-seed output matches independent operation model");
    if (capture.length != expected_length ||
        memcmp(capture.bytes, expected, expected_length) != 0) {
        fprintf(stderr, "  property expected=%lu actual=%lu\n",
                (unsigned long)expected_length,
                (unsigned long)capture.length);
    }

cleanup:
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
}

static void test_direct_builtin_validation(void)
{
    RexxValue argument = REXX_VALUE_INIT;
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;

    check(rexx_value_set_string(&argument, "1", 1u) == REXX_VM_STATUS_OK,
          "QUEUED invalid-argument fixture allocates");
    status = rexx_builtin_dispatch_with_vm(NULL, "QUEUED", 6u,
                                          &argument, 1u, &result);
    check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "QUEUED accepts exactly zero arguments and requires an active VM");
    rexx_value_clear(&argument);
    rexx_value_clear(&result);
}

static void test_queue_push_ast_cleanup(void)
{
    static const char source[] =
        "queue 'tail' || ' value'\n"
        "push 1 + 2\n";
    RexxBytecodeChunk *chunk = NULL;
#if defined(REXX_PARSER_TEST_ALLOCATOR)
    size_t live_before = rexx_parser_test_allocator_live_blocks();
#endif
    check(compile_source(source, &chunk, NULL, NULL),
          "QUEUE and PUSH expressions compile for AST cleanup qualification");
    rexx_bytecode_chunk_free(chunk);
#if defined(REXX_PARSER_TEST_ALLOCATOR)
    check(rexx_parser_test_allocator_live_blocks() == live_before,
          "QUEUE and PUSH expression AST allocations are released");
#endif
}

int main(void)
{
    test_required_examples();
    test_expressions_templates_and_binary();
    test_internal_interpret_and_lifecycle();
    test_independent_vms_and_empty_input_policy();
    test_errors_and_parser_recovery();
    test_failure_atomicity();
    test_deterministic_language_model();
    test_direct_builtin_validation();
    test_queue_push_ast_cleanup();
    printf("RX30 queue language: property_seed=0x%08x property_operations=%lu assertions=%lu failures=%lu\n",
           (unsigned int)RX30_PROPERTY_SEED,
           (unsigned long)property_operations,
           (unsigned long)assertions,
           (unsigned long)failures);
    return failures == 0u ? 0 : 1;
}
