/*
 * Standalone REXX integration driver for guideXOS.
 *
 * Build this file with the four in-tree REXX modules, for example:
 *   cc -std=c11 -Wall -Wextra -pedantic rexx_main.c rexx_lexer.c \
 *      rexx_parser.c rexx_compiler.c rexx_vm.c rexx_builtin.c -lm -o rexx_test
 *
 * With no arguments the executable runs the built-in integration suite.
 * `--eval` executes one source string through the same public pipeline. No
 * external runtime or third-party library is required.
 */

#include "rexx_lexer.h"
#include "rexx_parser.h"
#include "rexx_compiler.h"
#include "rexx_vm.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum RexxExpectedValueKind {
    REXX_EXPECT_NO_VALUE = 0,
    REXX_EXPECT_LOCAL_NUMBER,
    REXX_EXPECT_LOCAL_STRING,
    REXX_EXPECT_STEM_STRING
} RexxExpectedValueKind;

/* State belongs to one evaluation, keeping dispatch and result checks local. */
typedef struct RexxTestAudit {
    size_t command_calls;
    const char *expected_command;
    int saw_expected_command;
    RexxExpectedValueKind expected_value_kind;
    const char *expected_name;
    const char *expected_stem;
    const char *expected_tail;
    const char *expected_string;
    double expected_number;
} RexxTestAudit;

/*
 * This deliberately harmless dispatcher never launches a host shell. It
 * prints the command so ADDRESS routing is visible and always returns RC 0.
 */
static int32_t rexx_test_command_dispatcher(void *user_data,
                                           const char *command,
                                           size_t command_length)
{
    RexxTestAudit *audit = (RexxTestAudit *)user_data;

    if (audit != NULL) {
        ++audit->command_calls;
        if (audit->expected_command != NULL &&
            command_length == strlen(audit->expected_command) &&
            memcmp(command, audit->expected_command, command_length) == 0) {
            audit->saw_expected_command = 1;
        }
    }

    (void)fputs("[ADDRESS] ", stdout);
    if (command_length != 0u) {
        (void)fwrite(command, 1u, command_length, stdout);
    }
    (void)fputc('\n', stdout);
    return 0;
}

/*
 * Internal form of the public API lets the built-in suite audit dispatch
 * without global mutable state. All module-owned resources are released from
 * one cleanup path, including when initialization or execution fails.
 */
static int rexx_verify_expected_value(const RexxVM *vm,
                                      const RexxTestAudit *audit)
{
    const RexxValue *value = NULL;
    size_t index;

    if (vm == NULL || audit == NULL ||
        audit->expected_value_kind == REXX_EXPECT_NO_VALUE) {
        return 1;
    }

    if (audit->expected_value_kind == REXX_EXPECT_LOCAL_NUMBER ||
        audit->expected_value_kind == REXX_EXPECT_LOCAL_STRING) {
        for (index = 0; index < vm->local_count; ++index) {
            const RexxVMEntry *entry = &vm->locals[index];
            if (entry->name_length == strlen(audit->expected_name) &&
                memcmp(entry->name, audit->expected_name,
                       entry->name_length) == 0) {
                value = &entry->value;
                break;
            }
        }
    } else if (audit->expected_value_kind == REXX_EXPECT_STEM_STRING) {
        for (index = 0; index < vm->stem_count; ++index) {
            const RexxVMStemEntry *entry = &vm->stems[index];
            if (entry->stem_length == strlen(audit->expected_stem) &&
                entry->tail_length == strlen(audit->expected_tail) &&
                memcmp(entry->stem, audit->expected_stem,
                       entry->stem_length) == 0 &&
                memcmp(entry->tail, audit->expected_tail,
                       entry->tail_length) == 0) {
                value = &entry->value;
                break;
            }
        }
    }

    if (audit->expected_value_kind == REXX_EXPECT_LOCAL_NUMBER &&
        value != NULL && value->type == REXX_VALUE_NUMBER &&
        value->as.number == audit->expected_number) {
        return 1;
    }
    if ((audit->expected_value_kind == REXX_EXPECT_LOCAL_STRING ||
         audit->expected_value_kind == REXX_EXPECT_STEM_STRING) &&
        value != NULL && value->type == REXX_VALUE_STRING &&
        value->as.string.length == strlen(audit->expected_string) &&
        memcmp(value->as.string.bytes, audit->expected_string,
               value->as.string.length) == 0) {
        return 1;
    }

    (void)fprintf(stderr, "REXX test expectation failed for %s\n",
                  audit->expected_name != NULL ? audit->expected_name
                                               : "stem variable");
    return 0;
}

static int rexx_eval_source_with_audit(const char *source_code,
                                       RexxTestAudit *audit)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxCompilerError compiler_error;
    RexxVM vm;
    RexxLexerStatus lexer_status;
    RexxParserStatus parser_status;
    RexxCompilerStatus compiler_status;
    RexxVMStatus vm_status;
    int result = 1;

    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    memset(&vm, 0, sizeof(vm));
    memset(&compiler_error, 0, sizeof(compiler_error));

    if (source_code == NULL) {
        (void)fputs("REXX input error: source_code is NULL\n", stderr);
        goto cleanup;
    }

    /* The lexer borrows the source; it stays alive through AST compilation. */
    lexer_status = rexx_lexer_init(&lexer, source_code, strlen(source_code));
    if (lexer_status != REXX_LEXER_STATUS_OK) {
        (void)fprintf(stderr, "REXX lexer initialization failed (%d)\n",
                      (int)lexer_status);
        goto cleanup;
    }

    parser_status = rexx_parser_init(&parser, &lexer);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        const RexxParserError *error = rexx_parser_get_error(&parser);
        if (error != NULL) {
            (void)fprintf(stderr,
                          "REXX parser initialization failed at %lu:%lu: %s\n",
                          (unsigned long)error->token.line,
                          (unsigned long)error->token.column,
                          error->message != NULL ? error->message : "unknown error");
        } else {
            (void)fprintf(stderr, "REXX parser initialization failed (%d)\n",
                          (int)parser_status);
        }
        goto cleanup;
    }

    parser_status = rexx_parser_parse(&parser, &program);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        const RexxParserError *error = rexx_parser_get_error(&parser);
        if (error != NULL) {
            (void)fprintf(stderr, "REXX parse error at %lu:%lu: %s\n",
                          (unsigned long)error->token.line,
                          (unsigned long)error->token.column,
                          error->message != NULL ? error->message : "unknown error");
        } else {
            (void)fprintf(stderr, "REXX parse failed (%d)\n", (int)parser_status);
        }
        goto cleanup;
    }

    compiler_status = rexx_compiler_compile(program, &chunk, &compiler_error);
    if (compiler_status != REXX_COMPILER_STATUS_OK) {
        (void)fprintf(stderr,
                      "REXX compile error at %lu:%lu: %s (%s)\n",
                      (unsigned long)compiler_error.token.line,
                      (unsigned long)compiler_error.token.column,
                      compiler_error.message != NULL
                          ? compiler_error.message : "unknown error",
                      rexx_compiler_status_name(compiler_status));
        goto cleanup;
    }

    vm_status = rexx_vm_init(&vm, 0u);
    if (vm_status != REXX_VM_STATUS_OK) {
        (void)fprintf(stderr, "REXX VM initialization failed: %s\n",
                      rexx_vm_status_name(vm_status));
        goto cleanup;
    }
    rexx_vm_set_command_dispatcher(&vm, rexx_test_command_dispatcher, audit);

    vm_status = rexx_vm_execute(&vm, chunk);
    if (vm_status != REXX_VM_STATUS_OK) {
        const RexxVMError *error = &vm.error;
        (void)fprintf(stderr, "REXX execution failed at instruction %lu: %s\n",
                      (unsigned long)error->pc,
                      error->message != NULL ? error->message
                                              : rexx_vm_status_name(vm_status));
        goto cleanup;
    }

    if (!rexx_verify_expected_value(&vm, audit)) {
        goto cleanup;
    }

    result = 0;

cleanup:
    /* Safe for zero-initialized, partially initialized, and live instances. */
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    rexx_parser_free_ast(program);
    return result;
}

/* Public embedding entry point. Zero means the complete pipeline succeeded. */
int rexx_eval_source(const char *source_code)
{
    return rexx_eval_source_with_audit(source_code, NULL);
}

static int rexx_run_test(const char *name,
                         const char *source_code,
                         RexxTestAudit *audit)
{
    int result;

    (void)printf("[TEST] %s\n", name);
    result = rexx_eval_source_with_audit(source_code, audit);

    if (result == 0 && audit != NULL && audit->expected_command != NULL &&
        (audit->command_calls != 1u || !audit->saw_expected_command)) {
        (void)fprintf(stderr,
                      "ADDRESS test did not dispatch the expected command "
                      "exactly once (calls=%lu)\n",
                      (unsigned long)audit->command_calls);
        result = 1;
    }

    if (result == 0) {
        (void)printf("[PASS] %s\n\n", name);
    } else {
        (void)printf("[FAIL] %s\n\n", name);
    }
    return result;
}

static int rexx_run_test_suite(void)
{
    static const char arithmetic_source[] =
        "a = 5 + 3 * 2\n"
        "say a\n";
    static const char stem_source[] =
        "server.1 = \"active\"; say server.1\n";
    static const char control_flow_source[] =
        "a = 8\n"
        "if a > 5 then do; result = \"Greater\"; say \"Greater\"; end\n";
    static const char address_source[] =
        "address KERNEL \"sys_status\"\n";
    RexxTestAudit arithmetic_audit;
    RexxTestAudit stem_audit;
    RexxTestAudit control_audit;
    RexxTestAudit address_audit;
    int failures = 0;

    memset(&arithmetic_audit, 0, sizeof(arithmetic_audit));
    arithmetic_audit.expected_value_kind = REXX_EXPECT_LOCAL_NUMBER;
    arithmetic_audit.expected_name = "A";
    arithmetic_audit.expected_number = 11.0;

    memset(&stem_audit, 0, sizeof(stem_audit));
    stem_audit.expected_value_kind = REXX_EXPECT_STEM_STRING;
    stem_audit.expected_stem = "SERVER.";
    stem_audit.expected_tail = "1";
    stem_audit.expected_string = "active";

    memset(&control_audit, 0, sizeof(control_audit));
    control_audit.expected_value_kind = REXX_EXPECT_LOCAL_STRING;
    control_audit.expected_name = "RESULT";
    control_audit.expected_string = "Greater";

    memset(&address_audit, 0, sizeof(address_audit));
    address_audit.expected_command = "sys_status";

    failures += rexx_run_test("arithmetic and assignment",
                              arithmetic_source, &arithmetic_audit);
    failures += rexx_run_test("stem variable", stem_source, &stem_audit);
    failures += rexx_run_test("IF and DO control flow",
                              control_flow_source, &control_audit);
    failures += rexx_run_test("ADDRESS dispatcher", address_source,
                              &address_audit);

    if (failures == 0) {
        (void)puts("All REXX integration tests passed.");
        return 0;
    }
    (void)fprintf(stderr, "%d REXX integration test(s) failed.\n", failures);
    return 1;
}

static void rexx_print_usage(const char *program_name)
{
    (void)fprintf(stderr,
                  "Usage: %s [--eval SOURCE]\n"
                  "With no arguments, run the built-in integration suite.\n",
                  program_name != NULL ? program_name : "rexx_test");
}

int main(int argc, char **argv)
{
    if (argc == 1) {
        return rexx_run_test_suite();
    }
    if (argc == 2 &&
        (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        rexx_print_usage(argv[0]);
        return 0;
    }
    if (argc == 3 &&
        (strcmp(argv[1], "--eval") == 0 || strcmp(argv[1], "-e") == 0)) {
        return rexx_eval_source(argv[2]);
    }

    rexx_print_usage(argc > 0 ? argv[0] : NULL);
    return 2;
}
