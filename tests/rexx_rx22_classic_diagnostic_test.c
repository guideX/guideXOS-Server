#include "../rexx_builtin.h"
#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t assertions;
static size_t failures;

#define CHECK(condition, label) do { \
    ++assertions; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "[FAIL] %s (line %d)\n", label, __LINE__); \
    } \
} while (0)

static const char *const expected_ibm_error_text[47] = {
    "Program is unreadable",
    "Program interrupted",
    "Machine storage exhausted",
    "Unmatched \"/*\" or quote",
    "WHEN or OTHERWISE expected",
    "Unexpected THEN or ELSE",
    "Unexpected WHEN or OTHERWISE",
    "Unexpected or unmatched END",
    "Control stack full",
    "Clause too long",
    "Invalid character in program",
    "Incomplete DO/SELECT/IF",
    "Invalid hexadecimal or binary string",
    "Label not found",
    "Unexpected PROCEDURE",
    "THEN expected",
    "String or symbol expected",
    "Symbol expected",
    "Invalid data at end of clause",
    "Invalid character string",
    "Invalid SBCS/DBCS mixed string",
    "Invalid TRACE request",
    "Invalid subkeyword found",
    "Invalid whole number",
    "Invalid DO syntax",
    "Invalid LEAVE or ITERATE",
    "Environment name too long",
    "Name or string > 250 characters",
    "Name starts with number or \".\"",
    "Invalid use of stem",
    "Invalid expression result",
    "Logical value not 0 or 1",
    "Invalid expression",
    "Unmatched \"(\" in expression",
    "Unexpected \",\" or \")\"",
    "Invalid template or pattern",
    "Evaluation stack overflow",
    "Incorrect call to routine",
    "Bad arithmetic conversion",
    "Arithmetic overflow/underflow",
    "Routine not found",
    "Function did not return data",
    "No data specified on function RETURN",
    "Invalid variable reference",
    "Unexpected label",
    "Failure in system service",
    "Language processor failure"
};

static RexxVMStatus compile_bytes(const char *source, size_t source_length,
                                  RexxBytecodeChunk **chunk_out)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxCompilerError error;
    RexxParserStatus parser_status;
    RexxCompilerStatus compiler_status;

    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    memset(&error, 0, sizeof(error));
    *chunk_out = NULL;
    if (rexx_lexer_init(&lexer, source, source_length) !=
        REXX_LEXER_STATUS_OK)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    if (rexx_parser_init(&parser, &lexer) != REXX_PARSER_STATUS_OK)
        return REXX_VM_STATUS_INVALID_ARGUMENT;
    parser_status = rexx_parser_parse(&parser, &program);
    if (parser_status != REXX_PARSER_STATUS_OK) {
        rexx_parser_free_ast(program);
        if (parser_status == REXX_PARSER_STATUS_OUT_OF_MEMORY)
            return REXX_VM_STATUS_OUT_OF_MEMORY;
        if (parser_status == REXX_PARSER_STATUS_RESOURCE_LIMIT)
            return REXX_VM_STATUS_RESOURCE_LIMIT;
        return REXX_VM_STATUS_DYNAMIC_SYNTAX;
    }
    compiler_status = rexx_compiler_compile(program, chunk_out, &error);
    rexx_parser_free_ast(program);
    if (compiler_status == REXX_COMPILER_STATUS_OK)
        return REXX_VM_STATUS_OK;
    if (compiler_status == REXX_COMPILER_STATUS_OUT_OF_MEMORY)
        return REXX_VM_STATUS_OUT_OF_MEMORY;
    if (compiler_status == REXX_COMPILER_STATUS_RESOURCE_LIMIT)
        return REXX_VM_STATUS_RESOURCE_LIMIT;
    return REXX_VM_STATUS_INVALID_ARGUMENT;
}

static int bytes_equal(const RexxValue *value, const char *expected,
                       size_t expected_length)
{
    return value != NULL && value->type == REXX_VALUE_STRING &&
           value->as.string.bytes != NULL &&
           value->as.string.length == expected_length &&
           (expected_length == 0u ||
            memcmp(value->as.string.bytes, expected, expected_length) == 0);
}

static int string_equal(const RexxValue *value, const char *expected)
{
    return bytes_equal(value, expected, strlen(expected));
}

static const RexxValue *find_local(const RexxVM *vm, const char *name)
{
    size_t index;
    size_t length = strlen(name);
    if (vm == NULL) return NULL;
    for (index = 0u; index < vm->local_count; ++index) {
        if (vm->locals[index].name_length == length &&
            memcmp(vm->locals[index].name, name, length) == 0)
            return &vm->locals[index].value;
    }
    return NULL;
}

static RexxVMStatus call_builtin(const char *name,
                                const RexxValue *arguments,
                                size_t argument_count,
                                RexxVM *vm,
                                RexxValue *result)
{
    return rexx_builtin_dispatch_with_vm(
        vm, name, strlen(name), arguments, argument_count, result);
}

static void test_reverse_examples_and_binary_safety(void)
{
    RexxValue input = REXX_VALUE_INIT;
    RexxValue result = REXX_VALUE_INIT;
    static const unsigned char binary_a[] = { 0x00u, 0xffu, 0x80u };
    static const unsigned char binary_a_expected[] = { 0x80u, 0xffu, 0x00u };
    static const unsigned char binary_b[] = { 0x41u, 0x42u, 0x00u };
    static const unsigned char binary_b_expected[] = { 0x00u, 0x42u, 0x41u };
    static const unsigned char empty[] = { 0u };

    CHECK(rexx_value_set_string(&input, "ABC", 3u) == REXX_VM_STATUS_OK &&
              call_builtin("REVERSE", &input, 1u, NULL, &result) ==
                  REXX_VM_STATUS_OK &&
              string_equal(&result, "CBA"),
          "REVERSE('ABC') produces CBA");
    rexx_value_clear(&result);
    CHECK(rexx_value_set_string(&input, (const char *)empty, 0u) == REXX_VM_STATUS_OK &&
              call_builtin("REVERSE", &input, 1u, NULL, &result) ==
                  REXX_VM_STATUS_OK &&
              result.type == REXX_VALUE_STRING &&
              result.as.string.bytes != NULL &&
              result.as.string.length == 0u,
          "empty reversal returns an allocated empty string");
    rexx_value_clear(&result);
    CHECK(rexx_value_set_string(&input, "A", 1u) == REXX_VM_STATUS_OK &&
              call_builtin("REVERSE", &input, 1u, NULL, &result) ==
                  REXX_VM_STATUS_OK &&
              string_equal(&result, "A"),
          "one-byte reversal is unchanged");
    rexx_value_clear(&result);
    CHECK(rexx_value_set_string(&input, "ABCD", 4u) == REXX_VM_STATUS_OK &&
              call_builtin("REVERSE", &input, 1u, NULL, &result) ==
                  REXX_VM_STATUS_OK &&
              string_equal(&result, "DCBA"),
          "even-length reversal is correct");
    rexx_value_clear(&result);
    CHECK(rexx_value_set_string(&input, "XYZ ", 4u) == REXX_VM_STATUS_OK &&
              call_builtin("REVERSE", &input, 1u, NULL, &result) ==
                  REXX_VM_STATUS_OK &&
              string_equal(&result, " ZYX"),
          "trailing spaces are reversed as ordinary bytes");
    rexx_value_clear(&result);
    CHECK(rexx_value_set_string(&input, (const char *)binary_a,
                                sizeof(binary_a)) == REXX_VM_STATUS_OK &&
              call_builtin("REVERSE", &input, 1u, NULL, &result) ==
                  REXX_VM_STATUS_OK &&
              bytes_equal(&result, (const char *)binary_a_expected,
                          sizeof(binary_a_expected)),
          "binary reversal preserves high-bit bytes and embedded NUL");
    rexx_value_clear(&result);
    CHECK(rexx_value_set_string(&input, (const char *)binary_b,
                                sizeof(binary_b)) == REXX_VM_STATUS_OK &&
              call_builtin("REVERSE", &input, 1u, NULL, &result) ==
                  REXX_VM_STATUS_OK &&
              bytes_equal(&result, (const char *)binary_b_expected,
                          sizeof(binary_b_expected)),
          "binary reversal preserves an embedded NUL at the output front");
    CHECK(bytes_equal(&input, (const char *)binary_b, sizeof(binary_b)),
          "REVERSE does not modify the source value");
    CHECK(call_builtin("REVERSE", NULL, 0u, NULL, &result) ==
              REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "REVERSE rejects a missing required argument");
    CHECK(call_builtin("REVERSE", &input, 2u, NULL, &result) ==
              REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "REVERSE rejects extra arguments");

    rexx_value_clear(&input);
    rexx_value_clear(&result);
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

static void test_reverse_properties(void)
{
    enum { MAX_PROPERTY_LENGTH = 4096, PROPERTY_CASES = 500 };
    uint32_t random_state = 0x52583232u;
    size_t sample;

    for (sample = 0u; sample < PROPERTY_CASES; ++sample) {
        unsigned char input_bytes[MAX_PROPERTY_LENGTH];
        unsigned char oracle[MAX_PROPERTY_LENGTH];
        unsigned char counts_before[256] = { 0u };
        unsigned char counts_after[256] = { 0u };
        RexxValue input = REXX_VALUE_INIT;
        RexxValue reversed = REXX_VALUE_INIT;
        RexxValue restored = REXX_VALUE_INIT;
        size_t length = (size_t)(next_random(&random_state) %
                                 (MAX_PROPERTY_LENGTH + 1u));
        size_t index;
        RexxVMStatus status;

        for (index = 0u; index < length; ++index) {
            input_bytes[index] =
                (unsigned char)(next_random(&random_state) >> 24);
            oracle[length - index - 1u] = input_bytes[index];
            ++counts_before[input_bytes[index]];
        }
        status = rexx_value_set_string(&input, (const char *)input_bytes,
                                       length);
        if (status == REXX_VM_STATUS_OK)
            status = call_builtin("REVERSE", &input, 1u, NULL, &reversed);
        CHECK(status == REXX_VM_STATUS_OK &&
                  bytes_equal(&reversed, (const char *)oracle, length),
              "fixed-seed byte reversal matches the independent oracle");
        CHECK(input.as.string.length == length &&
                  (length == 0u ||
                   memcmp(input.as.string.bytes, input_bytes, length) == 0),
              "fixed-seed reversal leaves its input unchanged");
        if (reversed.type == REXX_VALUE_STRING) {
            for (index = 0u; index < length; ++index)
                ++counts_after[(unsigned char)reversed.as.string.bytes[index]];
        }
        CHECK(memcmp(counts_before, counts_after,
                     sizeof(counts_before)) == 0,
              "fixed-seed reversal preserves the byte multiset");
        status = call_builtin("REVERSE", &reversed, 1u, NULL, &restored);
        CHECK(status == REXX_VM_STATUS_OK &&
                  bytes_equal(&restored, (const char *)input_bytes, length),
              "reversing twice restores the original byte sequence");

        rexx_value_clear(&restored);
        rexx_value_clear(&reversed);
        rexx_value_clear(&input);
    }
}

static void test_reverse_boundaries_and_allocation_recovery(void)
{
    const size_t maximum = REXX_VM_MAX_PARSE_SOURCE_LENGTH;
    unsigned char *source = (unsigned char *)malloc(maximum + 1u);
    RexxValue input = REXX_VALUE_INIT;
    RexxValue reversed = REXX_VALUE_INIT;
    RexxValue restored = REXX_VALUE_INIT;
    size_t index;
    RexxVMStatus status;

    CHECK(source != NULL, "maximum-size input buffer allocated");
    if (source == NULL) return;
    for (index = 0u; index < maximum + 1u; ++index)
        source[index] = (unsigned char)((index * 31u + 7u) & 0xffu);

    CHECK(rexx_value_set_string(&input, (const char *)source, maximum) ==
              REXX_VM_STATUS_OK,
          "runtime maximum-size string is accepted");
    status = call_builtin("REVERSE", &input, 1u, NULL, &reversed);
    CHECK(status == REXX_VM_STATUS_OK &&
              reversed.as.string.length == maximum &&
              (unsigned char)reversed.as.string.bytes[0] ==
                  source[maximum - 1u] &&
              (unsigned char)reversed.as.string.bytes[maximum - 1u] ==
                  source[0],
          "REVERSE operates at the 16 MiB runtime boundary");
    status = call_builtin("REVERSE", &reversed, 1u, NULL, &restored);
    CHECK(status == REXX_VM_STATUS_OK &&
              restored.as.string.length == maximum &&
              memcmp(restored.as.string.bytes, source, maximum) == 0,
          "double reversal restores the maximum-size input");

    rexx_value_clear(&input);
    rexx_value_clear(&reversed);
    rexx_value_clear(&restored);

    input.type = REXX_VALUE_STRING;
    input.as.string.bytes = (char *)source;
    input.as.string.length = maximum + 1u;
    CHECK(call_builtin("REVERSE", &input, 1u, NULL, &reversed) ==
              REXX_VM_STATUS_RESOURCE_LIMIT,
          "REVERSE rejects an output larger than the runtime string limit");
    input.type = REXX_VALUE_UNSET;
    input.as.string.bytes = NULL;
    input.as.string.length = 0u;
    rexx_value_clear(&reversed);
    free(source);

    CHECK(rexx_value_set_string(&input, "failure", 7u) ==
              REXX_VM_STATUS_OK,
          "allocation-failure input prepared before fault injection");
    rexx_vm_test_allocator_fail_on(1u);
    status = call_builtin("REVERSE", &input, 1u, NULL, &reversed);
    CHECK(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
              rexx_vm_test_allocator_attempts() == 1u,
          "REVERSE reports an injected result allocation failure");
    rexx_vm_test_allocator_disable();
    status = call_builtin("REVERSE", &input, 1u, NULL, &reversed);
    CHECK(status == REXX_VM_STATUS_OK && string_equal(&reversed, "eruliaf"),
          "REVERSE succeeds after allocation failure recovery");
    rexx_value_clear(&input);
    rexx_value_clear(&reversed);

    CHECK(rexx_value_set_string(&input, "", 0u) == REXX_VM_STATUS_OK,
          "empty allocation-failure input prepared");
    rexx_vm_test_allocator_fail_on(1u);
    status = call_builtin("REVERSE", &input, 1u, NULL, &reversed);
    CHECK(status == REXX_VM_STATUS_OUT_OF_MEMORY,
          "zero-length reversal still reports allocation failure");
    rexx_vm_test_allocator_disable();
    status = call_builtin("REVERSE", &input, 1u, NULL, &reversed);
    CHECK(status == REXX_VM_STATUS_OK && reversed.as.string.bytes != NULL &&
              reversed.as.string.length == 0u,
          "zero-length reversal recovers with a valid empty string");
    rexx_vm_test_allocator_disable();
    rexx_value_clear(&input);
    rexx_value_clear(&reversed);
}

static void test_errortext_complete_mapping(void)
{
    RexxValue argument = REXX_VALUE_INIT;
    RexxValue result = REXX_VALUE_INIT;
    size_t number;

    for (number = 0u; number <= 99u; ++number) {
        const char *expected = number >= 3u && number <= 49u
            ? expected_ibm_error_text[number - 3u] : "";
        RexxVMStatus status =
            rexx_value_set_number(&argument, (double)number);
        if (status == REXX_VM_STATUS_OK)
            status = call_builtin("ERRORTEXT", &argument, 1u, NULL, &result);
        CHECK(status == REXX_VM_STATUS_OK && string_equal(&result, expected),
              "ERRORTEXT returns the independent IBM mapping for 0..99");
        rexx_value_clear(&result);
        status = call_builtin("ERRORTEXT", &argument, 1u, NULL, &result);
        CHECK(status == REXX_VM_STATUS_OK && string_equal(&result, expected),
              "ERRORTEXT mapping is stable across repeated lookup");
        rexx_value_clear(&result);
    }

    CHECK(rexx_value_set_number(&argument, 16.0) == REXX_VM_STATUS_OK &&
              call_builtin("ERRORTEXT", &argument, 1u, NULL, &result) ==
                  REXX_VM_STATUS_OK &&
              string_equal(&result, "Label not found"),
          "IBM ERRORTEXT(16) reference case");
    rexx_value_clear(&result);
    CHECK(rexx_value_set_number(&argument, 60.0) == REXX_VM_STATUS_OK &&
              call_builtin("ERRORTEXT", &argument, 1u, NULL, &result) ==
                  REXX_VM_STATUS_OK &&
              result.as.string.length == 0u,
          "IBM ERRORTEXT(60) reference case is empty");

    CHECK(rexx_value_set_number(&argument, -1.0) == REXX_VM_STATUS_OK &&
              call_builtin("ERRORTEXT", &argument, 1u, NULL, &result) !=
                  REXX_VM_STATUS_OK,
          "ERRORTEXT rejects negative codes");
    CHECK(rexx_value_set_number(&argument, 100.0) == REXX_VM_STATUS_OK &&
              call_builtin("ERRORTEXT", &argument, 1u, NULL, &result) ==
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "ERRORTEXT rejects codes above 99");
    CHECK(rexx_value_set_number(&argument, 1.5) == REXX_VM_STATUS_OK &&
              call_builtin("ERRORTEXT", &argument, 1u, NULL, &result) !=
                  REXX_VM_STATUS_OK,
          "ERRORTEXT rejects non-whole-number codes");
    CHECK(rexx_value_set_string(&argument, "not a number", 12u) ==
              REXX_VM_STATUS_OK &&
              call_builtin("ERRORTEXT", &argument, 1u, NULL, &result) ==
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "ERRORTEXT rejects nonnumeric values");
    CHECK(call_builtin("ERRORTEXT", NULL, 0u, NULL, &result) ==
              REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "ERRORTEXT enforces its required argument");
    CHECK(call_builtin("ERRORTEXT", &argument, 2u, NULL, &result) ==
              REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "ERRORTEXT rejects extra arguments");

    CHECK(rexx_value_set_number(&argument, 16.0) == REXX_VM_STATUS_OK,
          "ERRORTEXT allocation-failure input prepared");
    rexx_vm_test_allocator_fail_on(1u);
    CHECK(call_builtin("ERRORTEXT", &argument, 1u, NULL, &result) ==
              REXX_VM_STATUS_OUT_OF_MEMORY,
          "ERRORTEXT reports a message allocation failure");
    rexx_vm_test_allocator_disable();
    CHECK(call_builtin("ERRORTEXT", &argument, 1u, NULL, &result) ==
              REXX_VM_STATUS_OK && string_equal(&result, "Label not found"),
          "ERRORTEXT recovers after message allocation failure");
    rexx_value_clear(&result);

    rexx_value_clear(&argument);
    rexx_value_clear(&result);
}

static void test_sourceline_source_ownership_and_boundaries(void)
{
    static const unsigned char source[] =
        "count = SOURCELINE()\n"
        "first = SOURCELINE(1)\n"
        "blank = SOURCELINE(4)\n"
        "\n"
        "last = SOURCELINE(6)\n"
        "/* \xff */";
    const size_t source_length = sizeof(source) - 1u;
    char *caller_buffer = (char *)malloc(source_length);
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm;
    RexxValue argument = REXX_VALUE_INIT;
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    const RexxValue *value;

    memset(&vm, 0, sizeof(vm));
    CHECK(caller_buffer != NULL, "temporary caller source allocated");
    if (caller_buffer == NULL) return;
    memcpy(caller_buffer, source, source_length);
    status = compile_bytes(caller_buffer, source_length, &chunk);
    free(caller_buffer);
    CHECK(status == REXX_VM_STATUS_OK && chunk != NULL &&
              chunk->source_line_count == 6u,
          "compiled chunk owns six source lines after caller releases buffer");
    if (status != REXX_VM_STATUS_OK || chunk == NULL) return;
    CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "SOURCELINE VM initialized");
    CHECK(rexx_vm_execute(&vm, chunk) == REXX_VM_STATUS_OK,
          "source inspection script executes");
    value = find_local(&vm, "COUNT");
    CHECK(string_equal(value, "6"),
          "SOURCELINE() counts the final unterminated source line");
    value = find_local(&vm, "FIRST");
    CHECK(string_equal(value, "count = SOURCELINE()"),
          "SOURCELINE(1) preserves original line text");
    value = find_local(&vm, "BLANK");
    CHECK(value != NULL && value->type == REXX_VALUE_STRING &&
              value->as.string.length == 0u,
          "SOURCELINE preserves an empty source line");
    value = find_local(&vm, "LAST");
    CHECK(bytes_equal(value, "/* \xff */", 7u),
          "SOURCELINE preserves comments and high-bit source bytes");

    vm.active_chunk = chunk;
    CHECK(rexx_builtin_dispatch_with_vm(&vm, "SOURCELINE", 10u, NULL, 0u,
                                        &result) == REXX_VM_STATUS_OK &&
              string_equal(&result, "6"),
          "SOURCELINE context lookup is bound to the active VM chunk");
    rexx_value_clear(&result);
    CHECK(rexx_value_set_number(&argument, 0.0) == REXX_VM_STATUS_OK &&
              call_builtin("SOURCELINE", &argument, 1u, &vm, &result) ==
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "SOURCELINE rejects zero line numbers");
    CHECK(rexx_value_set_number(&argument, 7.0) == REXX_VM_STATUS_OK &&
              call_builtin("SOURCELINE", &argument, 1u, &vm, &result) ==
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "SOURCELINE rejects line numbers past the source count");
    CHECK(rexx_value_set_number(&argument, 1.5) == REXX_VM_STATUS_OK &&
              call_builtin("SOURCELINE", &argument, 1u, &vm, &result) !=
                  REXX_VM_STATUS_OK,
          "SOURCELINE rejects non-whole-number line requests");
    CHECK(rexx_value_set_number(&argument, -1.0) == REXX_VM_STATUS_OK &&
              call_builtin("SOURCELINE", &argument, 1u, &vm, &result) !=
                  REXX_VM_STATUS_OK,
          "SOURCELINE rejects negative line requests");
    CHECK(rexx_value_set_string(&argument, "bad", 3u) ==
              REXX_VM_STATUS_OK &&
              call_builtin("SOURCELINE", &argument, 1u, &vm, &result) !=
                  REXX_VM_STATUS_OK,
          "SOURCELINE rejects nonnumeric line requests");
    CHECK(call_builtin("SOURCELINE", NULL, 0u, &vm, &result) ==
              REXX_VM_STATUS_OK &&
              string_equal(&result, "6"),
          "SOURCELINE permits omission of n");
    CHECK(call_builtin("SOURCELINE", &argument, 2u, &vm, &result) ==
              REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "SOURCELINE rejects extra arguments");

    CHECK(rexx_value_set_number(&argument, 1.0) == REXX_VM_STATUS_OK,
          "SOURCELINE allocation-failure input prepared");
    rexx_vm_test_allocator_fail_on(1u);
    CHECK(call_builtin("SOURCELINE", &argument, 1u, &vm, &result) ==
              REXX_VM_STATUS_OUT_OF_MEMORY,
          "SOURCELINE reports a returned-line allocation failure");
    rexx_vm_test_allocator_disable();
    CHECK(call_builtin("SOURCELINE", &argument, 1u, &vm, &result) ==
              REXX_VM_STATUS_OK &&
              string_equal(&result, "count = SOURCELINE()"),
          "SOURCELINE recovers after returned-line allocation failure");
    rexx_value_clear(&result);

    rexx_value_clear(&argument);
    rexx_value_clear(&result);
    rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);

    {
        static const char crlf_source[] =
            "count = SOURCELINE()\r\n"
            "second = SOURCELINE(2)  \r\n";
        RexxBytecodeChunk *crlf_chunk = NULL;
        RexxVM crlf_vm;
        memset(&crlf_vm, 0, sizeof(crlf_vm));
        status = compile_bytes(crlf_source, sizeof(crlf_source) - 1u,
                               &crlf_chunk);
        CHECK(status == REXX_VM_STATUS_OK && crlf_chunk != NULL &&
                  crlf_chunk->source_line_count == 2u,
              "CRLF and trailing-newline source has two lines");
        if (crlf_chunk != NULL) {
            CHECK(rexx_vm_init(&crlf_vm, 0u) == REXX_VM_STATUS_OK &&
                      rexx_vm_execute(&crlf_vm, crlf_chunk) ==
                          REXX_VM_STATUS_OK,
                  "CRLF source executes");
            value = find_local(&crlf_vm, "SECOND");
            CHECK(string_equal(value, "second = SOURCELINE(2)  "),
                  "CRLF is removed and line trailing spaces are preserved");
            rexx_vm_free(&crlf_vm);
            rexx_bytecode_chunk_free(crlf_chunk);
        }
    }

    {
        static const char trailing_cr_source[] = "line = SOURCELINE(1)\r";
        RexxBytecodeChunk *trailing_cr_chunk = NULL;
        RexxVM source_vm;
        RexxValue line_number = REXX_VALUE_INIT;
        RexxValue line_value = REXX_VALUE_INIT;
        memset(&source_vm, 0, sizeof(source_vm));
        status = compile_bytes(trailing_cr_source,
                               sizeof(trailing_cr_source) - 1u,
                               &trailing_cr_chunk);
        CHECK(status == REXX_VM_STATUS_OK && trailing_cr_chunk != NULL,
              "source with a final lone CR compiles");
        if (trailing_cr_chunk != NULL) {
            source_vm.active_chunk = trailing_cr_chunk;
            CHECK(rexx_value_set_number(&line_number, 1.0) ==
                      REXX_VM_STATUS_OK &&
                      call_builtin("SOURCELINE", &line_number, 1u,
                                   &source_vm, &line_value) ==
                          REXX_VM_STATUS_OK &&
                      bytes_equal(&line_value, trailing_cr_source,
                                  sizeof(trailing_cr_source) - 1u),
                  "SOURCELINE preserves a final CR that is not part of CRLF");
            rexx_bytecode_chunk_free(trailing_cr_chunk);
        }
        rexx_value_clear(&line_number);
        rexx_value_clear(&line_value);
    }

    {
        static const char quoted_binary_source[] =
            "line = SOURCELINE(2)\n"
            "value = '00FF80'X";
        RexxBytecodeChunk *quoted_binary_chunk = NULL;
        RexxVM quoted_binary_vm;
        memset(&quoted_binary_vm, 0, sizeof(quoted_binary_vm));
        status = compile_bytes(quoted_binary_source,
                               sizeof(quoted_binary_source) - 1u,
                               &quoted_binary_chunk);
        CHECK(status == REXX_VM_STATUS_OK && quoted_binary_chunk != NULL &&
                  quoted_binary_chunk->source_line_count == 2u,
              "source containing a quoted binary literal compiles");
        if (quoted_binary_chunk != NULL) {
            CHECK(rexx_vm_init(&quoted_binary_vm, 0u) == REXX_VM_STATUS_OK &&
                      rexx_vm_execute(&quoted_binary_vm, quoted_binary_chunk) ==
                          REXX_VM_STATUS_OK,
                  "quoted binary literal source program executes");
            value = find_local(&quoted_binary_vm, "LINE");
            CHECK(string_equal(value, "value = '00FF80'X"),
                  "SOURCELINE preserves quoted binary literal source text");
            rexx_vm_free(&quoted_binary_vm);
            rexx_bytecode_chunk_free(quoted_binary_chunk);
        }
    }

    {
        RexxBytecodeChunk *empty_chunk = NULL;
        RexxBytecodeChunk *newline_chunk = NULL;
        CHECK(compile_bytes("", 0u, &empty_chunk) == REXX_VM_STATUS_OK &&
                  empty_chunk != NULL && empty_chunk->source_line_count == 0u,
              "empty program has zero source lines");
        CHECK(compile_bytes("NOP\n", 4u, &newline_chunk) ==
                  REXX_VM_STATUS_OK &&
                  newline_chunk != NULL &&
                  newline_chunk->source_line_count == 1u,
              "a final newline terminates rather than adds a source line");
        rexx_bytecode_chunk_free(empty_chunk);
        rexx_bytecode_chunk_free(newline_chunk);
    }
}

static void test_sourceline_interpret_routines_and_unavailable_source(void)
{
    static const char source[] =
        "top = SOURCELINE(1)\n"
        "fromRoutine = GETLINE()\n"
        "interpret \"dynamicCount=SOURCELINE(); dynamicLine=SOURCELINE(1)\"\n"
        "exit\n"
        "getline:\n"
        "return SOURCELINE(1)";
    RexxBytecodeChunk *chunk = NULL;
    const size_t source_length = sizeof(source) - 1u;
    RexxVM vm;
    const RexxValue *value;
    RexxVMStatus status;

    memset(&vm, 0, sizeof(vm));
    status = compile_bytes(source, source_length, &chunk);
    CHECK(status == REXX_VM_STATUS_OK && chunk != NULL,
          "routine and INTERPRET source compiles");
    if (status == REXX_VM_STATUS_OK && chunk != NULL) {
        CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK &&
                  rexx_vm_execute(&vm, chunk) == REXX_VM_STATUS_OK,
              "nested source contexts execute");
        value = find_local(&vm, "TOP");
        CHECK(string_equal(value, "top = SOURCELINE(1)"),
              "top-level source context returns its own text");
        value = find_local(&vm, "FROMROUTINE");
        CHECK(string_equal(value, "top = SOURCELINE(1)"),
              "internal routines inherit their program source");
        value = find_local(&vm, "DYNAMICCOUNT");
        CHECK(string_equal(value, "1"),
              "INTERPRET uses its active source line count");
        value = find_local(&vm, "DYNAMICLINE");
        CHECK(string_equal(value,
              "dynamicCount=SOURCELINE(); dynamicLine=SOURCELINE(1)"),
              "INTERPRET retrieves its own exact source text");
        rexx_vm_free(&vm);

        {
            char *saved_source = chunk->source_text;
            vm.initialized = 0;
            chunk->source_text = NULL;
            chunk->source_length = 0u;
            chunk->source_line_count = 0u;
            CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
                  "source-unavailable VM initialized");
            vm.active_chunk = chunk;
            {
                RexxValue unavailable = REXX_VALUE_INIT;
                RexxValue line = REXX_VALUE_INIT;
                CHECK(call_builtin("SOURCELINE", NULL, 0u, &vm, &unavailable) ==
                          REXX_VM_STATUS_OK &&
                          string_equal(&unavailable, "0"),
                      "source-unavailable line count is deterministic");
                CHECK(rexx_value_set_number(&line, 1.0) == REXX_VM_STATUS_OK &&
                          call_builtin("SOURCELINE", &line, 1u, &vm,
                                       &unavailable) == REXX_VM_STATUS_OK &&
                          unavailable.as.string.length == 0u,
                      "unavailable requested source line returns the null string");
                rexx_value_clear(&line);
                rexx_value_clear(&unavailable);
            }
            rexx_vm_free(&vm);
            chunk->source_text = saved_source;
            chunk->source_length = source_length;
            chunk->source_line_count = 6u;
        }
        rexx_bytecode_chunk_free(chunk);
    }
}

static void test_source_copy_allocation_failure_recovery(void)
{
    static const char source[] = "answer = SOURCELINE()";
    RexxBytecodeChunk *chunk = NULL;
    RexxVM vm;
    RexxVMStatus status;

    rexx_compiler_test_allocator_fail_on(2u);
    status = compile_bytes(source, sizeof(source) - 1u, &chunk);
    CHECK(status == REXX_VM_STATUS_OUT_OF_MEMORY && chunk == NULL &&
              rexx_compiler_test_allocator_attempts() == 2u &&
              rexx_compiler_test_allocator_live_blocks() == 0u,
          "source snapshot allocation failure cleans the partial chunk");
    rexx_compiler_test_allocator_disable();

    status = compile_bytes(source, sizeof(source) - 1u, &chunk);
    CHECK(status == REXX_VM_STATUS_OK && chunk != NULL,
          "source compilation recovers after allocation failure");
    if (status == REXX_VM_STATUS_OK && chunk != NULL) {
        memset(&vm, 0, sizeof(vm));
        CHECK(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK &&
                  rexx_vm_execute(&vm, chunk) == REXX_VM_STATUS_OK &&
                  string_equal(find_local(&vm, "ANSWER"), "1"),
              "interpreter executes successfully after source allocation failure");
        rexx_vm_free(&vm);
        rexx_bytecode_chunk_free(chunk);
    }
    CHECK(rexx_compiler_test_allocator_live_blocks() == 0u,
          "compiler source snapshot allocations are fully reclaimed");
}

static void test_runtime_recovery_and_vm_isolation(void)
{
    static const char invalid_source[] = "bad = ERRORTEXT(100)";
    static const char recovery_source[] =
        "good = REVERSE('AB')\n"
        "message = ERRORTEXT(16)\n"
        "lineCount = SOURCELINE()";
    static const char first_source[] = "name = SOURCELINE(1)";
    static const char second_source[] = "other = SOURCELINE(1)";
    RexxBytecodeChunk *invalid_chunk = NULL;
    RexxBytecodeChunk *recovery_chunk = NULL;
    RexxBytecodeChunk *first_chunk = NULL;
    RexxBytecodeChunk *second_chunk = NULL;
    RexxVM first_vm;
    RexxVM second_vm;
    const RexxValue *value;

    memset(&first_vm, 0, sizeof(first_vm));
    memset(&second_vm, 0, sizeof(second_vm));
    CHECK(compile_bytes(invalid_source, sizeof(invalid_source) - 1u,
                        &invalid_chunk) == REXX_VM_STATUS_OK &&
              compile_bytes(recovery_source, sizeof(recovery_source) - 1u,
                            &recovery_chunk) == REXX_VM_STATUS_OK,
          "invalid-argument and recovery programs compile");
    if (invalid_chunk != NULL && recovery_chunk != NULL) {
        CHECK(rexx_vm_init(&first_vm, 0u) == REXX_VM_STATUS_OK,
              "recovery VM initialized");
        CHECK(rexx_vm_execute(&first_vm, invalid_chunk) ==
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL,
              "malformed ERRORTEXT argument is rejected at runtime");
        CHECK(rexx_vm_execute(&first_vm, recovery_chunk) ==
                  REXX_VM_STATUS_OK,
              "same interpreter remains responsive after malformed argument");
        value = find_local(&first_vm, "GOOD");
        CHECK(string_equal(value, "BA"),
              "REVERSE works after invalid ERRORTEXT recovery");
        value = find_local(&first_vm, "MESSAGE");
        CHECK(string_equal(value, "Label not found"),
              "ERRORTEXT works after invalid argument recovery");
        value = find_local(&first_vm, "LINECOUNT");
        CHECK(string_equal(value, "3"),
              "active source changes with the next executed chunk");
    }

    CHECK(compile_bytes(first_source, sizeof(first_source) - 1u,
                        &first_chunk) == REXX_VM_STATUS_OK &&
              compile_bytes(second_source, sizeof(second_source) - 1u,
                            &second_chunk) == REXX_VM_STATUS_OK,
          "independent source programs compile");
    if (first_chunk != NULL && second_chunk != NULL) {
        rexx_vm_free(&first_vm);
        rexx_vm_free(&second_vm);
        CHECK(rexx_vm_init(&first_vm, 0u) == REXX_VM_STATUS_OK &&
                  rexx_vm_init(&second_vm, 0u) == REXX_VM_STATUS_OK,
              "two interpreter instances initialize");
        CHECK(rexx_vm_execute(&first_vm, first_chunk) == REXX_VM_STATUS_OK &&
                  rexx_vm_execute(&second_vm, second_chunk) ==
                      REXX_VM_STATUS_OK,
              "two interpreter instances execute independently");
        CHECK(string_equal(find_local(&first_vm, "NAME"),
                           "name = SOURCELINE(1)") &&
                  string_equal(find_local(&second_vm, "OTHER"),
                               "other = SOURCELINE(1)"),
              "source text does not leak between interpreter instances");
    }

    rexx_vm_free(&first_vm);
    rexx_vm_free(&second_vm);
    rexx_bytecode_chunk_free(invalid_chunk);
    rexx_bytecode_chunk_free(recovery_chunk);
    rexx_bytecode_chunk_free(first_chunk);
    rexx_bytecode_chunk_free(second_chunk);
}

int main(void)
{
    test_reverse_examples_and_binary_safety();
    test_reverse_properties();
    test_reverse_boundaries_and_allocation_recovery();
    test_errortext_complete_mapping();
    test_sourceline_source_ownership_and_boundaries();
    test_sourceline_interpret_routines_and_unavailable_source();
    test_source_copy_allocation_failure_recovery();
    test_runtime_recovery_and_vm_isolation();

    rexx_vm_test_allocator_disable();
    rexx_compiler_test_allocator_disable();
    printf("RX22 classic/diagnostic built-ins: %lu assertions, %lu failures\n",
           (unsigned long)assertions, (unsigned long)failures);
    return failures == 0u ? 0 : 1;
}
