#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RX37_SEED UINT32_C(0x52583337)
#define RX37_CASES ((size_t)4096u)
#define RX37_CAPTURE_CAPACITY ((size_t)8192u)
#define RX37_WRITER_PAYLOAD_LIMIT ((size_t)119u)

typedef struct Rx37Destination {
    RexxVM *replace_vm;
    RexxVMOutputCapability metadata;
    unsigned char input[RX37_CAPTURE_CAPACITY];
    unsigned char formatted[RX37_CAPTURE_CAPACITY];
    size_t input_length;
    size_t formatted_length;
    size_t column;
    size_t writer_calls;
    size_t callback_chunks;
    RexxVMOutputCapabilityReadStatus read_status;
    uint32_t resize_width;
    int fail_next_write;
    int fail_partially;
    size_t partial_failure_bytes;
    int resize_during_read;
    int replace_during_read;
    int teardown_during_read;
} Rx37Destination;

static size_t assertions;
static size_t direct_assertions;
static size_t property_assertions;
static size_t property_cases;
static size_t failures;

static void check(int condition, const char *description, int property)
{
    ++assertions;
    if (property) ++property_assertions;
    else ++direct_assertions;
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL: %s\n", description);
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

static int append_byte(unsigned char *bytes, size_t capacity,
                       size_t *length, unsigned char value)
{
    if (bytes == NULL || length == NULL || *length >= capacity) return 0;
    bytes[(*length)++] = value;
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

static void destination_reset_output(Rx37Destination *destination)
{
    destination->input_length = 0u;
    destination->formatted_length = 0u;
    destination->column = 0u;
    destination->writer_calls = 0u;
    destination->callback_chunks = 0u;
}

static int destination_write(void *user_data, const char *bytes, size_t length)
{
    Rx37Destination *destination = (Rx37Destination *)user_data;
    size_t offset = 0u;
    int interrupted = 0;
    if (destination == NULL || (bytes == NULL && length != 0u)) return 0;
    ++destination->writer_calls;
    if (destination->fail_next_write) {
        destination->fail_next_write = 0;
        return 0;
    }
    if (destination->fail_partially) {
        destination->fail_partially = 0;
        if (destination->partial_failure_bytes < length)
            length = destination->partial_failure_bytes;
        interrupted = 1;
    }
    if (length > sizeof(destination->input) - destination->input_length)
        return 0;
    if (length != 0u)
        memcpy(destination->input + destination->input_length, bytes, length);
    destination->input_length += length;

    /* Model the destination's streaming policy while respecting the same
     * 119-byte callback processing bound used by the kernel adapter. The
     * chunk loop carries the column state across chunks. */
    while (offset < length) {
        size_t chunk = length - offset;
        size_t index;
        if (chunk > RX37_WRITER_PAYLOAD_LIMIT)
            chunk = RX37_WRITER_PAYLOAD_LIMIT;
        ++destination->callback_chunks;
        for (index = 0u; index < chunk; ++index) {
            unsigned char value = (unsigned char)bytes[offset + index];
            int wraps = destination->metadata.state ==
                            REXX_VM_OUTPUT_CAPABILITY_KNOWN &&
                        (destination->metadata.wrap_policy ==
                             REXX_VM_OUTPUT_WRAP_DESTINATION_WIDTH ||
                         destination->metadata.wrap_policy ==
                             REXX_VM_OUTPUT_WRAP_RECORD_BOUNDARY ||
                         destination->metadata.wrap_policy ==
                             REXX_VM_OUTPUT_WRAP_STORAGE_LIMIT);
            if (value == (unsigned char)'\n') {
                if (!append_byte(destination->formatted,
                                 sizeof(destination->formatted),
                                 &destination->formatted_length, value))
                    return 0;
                destination->column = 0u;
                continue;
            }
            if (wraps && destination->column >=
                             destination->metadata.logical_line_width) {
                if (!append_byte(destination->formatted,
                                 sizeof(destination->formatted),
                                 &destination->formatted_length,
                                 (unsigned char)'\n'))
                    return 0;
                destination->column = 0u;
            }
            if (!append_byte(destination->formatted,
                             sizeof(destination->formatted),
                             &destination->formatted_length, value))
                return 0;
            if (wraps) ++destination->column;
        }
        offset += chunk;
    }
    return interrupted ? 0 : 1;
}

static int replacement_write(void *user_data, const char *bytes, size_t length)
{
    (void)user_data;
    (void)bytes;
    (void)length;
    return 1;
}

static RexxVMOutputCapabilityReadStatus destination_read_capability(
    void *user_data, RexxVMOutputCapability *capability)
{
    Rx37Destination *destination = (Rx37Destination *)user_data;
    if (destination == NULL || capability == NULL)
        return REXX_VM_OUTPUT_CAPABILITY_READ_FAILURE;
    if (destination->replace_during_read) {
        destination->replace_during_read = 0;
        rexx_vm_set_output_writer(destination->replace_vm,
                                  replacement_write, NULL);
    }
    if (destination->teardown_during_read) {
        destination->teardown_during_read = 0;
        rexx_vm_free(destination->replace_vm);
    }
    *capability = destination->metadata;
    if (destination->resize_during_read) {
        destination->resize_during_read = 0;
        ++destination->metadata.destination_generation;
        destination->metadata.logical_line_width = destination->resize_width;
        capability->state = REXX_VM_OUTPUT_CAPABILITY_STALE;
        capability->width_unit = REXX_VM_OUTPUT_WIDTH_NONE;
        capability->wrap_policy = REXX_VM_OUTPUT_WRAP_UNKNOWN;
        capability->logical_line_width = 0u;
        capability->record_length = 0u;
    }
    return destination->read_status;
}

static void destination_make_fixed_columns(Rx37Destination *destination,
                                          uint32_t width,
                                          uint64_t identity,
                                          uint64_t generation)
{
    memset(&destination->metadata, 0, sizeof(destination->metadata));
    destination->metadata.struct_size =
        (uint32_t)sizeof(destination->metadata);
    destination->metadata.state = REXX_VM_OUTPUT_CAPABILITY_KNOWN;
    destination->metadata.destination_kind =
        REXX_VM_OUTPUT_DESTINATION_INTERACTIVE;
    destination->metadata.width_unit =
        REXX_VM_OUTPUT_WIDTH_CHARACTER_COLUMNS;
    destination->metadata.wrap_policy =
        REXX_VM_OUTPUT_WRAP_DESTINATION_WIDTH;
    destination->metadata.logical_line_width = width;
    destination->metadata.destination_identity = identity;
    destination->metadata.destination_generation = generation;
    destination->read_status = REXX_VM_OUTPUT_CAPABILITY_READ_OK;
}

static int destination_install(RexxVM *vm, Rx37Destination *destination)
{
    rexx_vm_set_output_writer(vm, destination_write, destination);
    return rexx_vm_set_output_capability_reader(
               vm, destination_read_capability) == REXX_VM_STATUS_OK;
}

static int execute_source(RexxVM *vm, const char *source)
{
    RexxBytecodeChunk *chunk = NULL;
    RexxVMStatus status;
    int compiled = compile_source(source, &chunk);
    if (!compiled) return 0;
    status = rexx_vm_execute(vm, chunk);
    rexx_bytecode_chunk_free(chunk);
    return status == REXX_VM_STATUS_OK;
}

/* Independent oracle: its width is a test vector input, not copied from the
 * provider snapshot or the destination writer's stored metadata. */
static int reference_format(const unsigned char *input, size_t input_length,
                            size_t width, unsigned char *output,
                            size_t output_capacity, size_t *output_length)
{
    size_t input_index;
    size_t output_index = 0u;
    size_t column = 0u;
    if ((input == NULL && input_length != 0u) || output == NULL ||
        output_length == NULL || width == 0u)
        return 0;
    for (input_index = 0u; input_index < input_length; ++input_index) {
        unsigned char value = input[input_index];
        if (value == (unsigned char)'\n') {
            if (!append_byte(output, output_capacity, &output_index, value))
                return 0;
            column = 0u;
        } else {
            if (column == width) {
                if (!append_byte(output, output_capacity, &output_index,
                                 (unsigned char)'\n'))
                    return 0;
                column = 0u;
            }
            if (!append_byte(output, output_capacity, &output_index, value))
                return 0;
            ++column;
        }
    }
    *output_length = output_index;
    return 1;
}

static int run_say_literal(RexxVM *vm, const unsigned char *payload,
                           size_t payload_length)
{
    char source[RX37_CAPTURE_CAPACITY];
    size_t index;
    size_t offset = 0u;
    static const char prefix[] = "SAY '";
    static const char suffix[] = "'\n";
    if (payload_length > sizeof(source) - sizeof(prefix) - sizeof(suffix))
        return 0;
    memcpy(source + offset, prefix, sizeof(prefix) - 1u);
    offset += sizeof(prefix) - 1u;
    for (index = 0u; index < payload_length; ++index) {
        unsigned char value = payload[index];
        if (value == (unsigned char)'\'') return 0;
        source[offset++] = (char)value;
    }
    memcpy(source + offset, suffix, sizeof(suffix) - 1u);
    offset += sizeof(suffix) - 1u;
    source[offset] = '\0';
    return execute_source(vm, source);
}

static void query_expect(RexxVM *vm,
                         RexxVMOutputCapabilityQueryStatus expected,
                         const char *description)
{
    RexxVMOutputCapability capability;
    RexxVMOutputCapabilityQueryStatus actual =
        rexx_vm_get_output_capability(vm, &capability);
    check(actual == expected, description, 0);
}

static void test_query_states_and_validation(void)
{
    RexxVM vm;
    RexxVMOutputCapability capability;
    Rx37Destination destination;
    memset(&destination, 0, sizeof(destination));
    check(rexx_vm_init(&vm, 16u) == REXX_VM_STATUS_OK,
          "VM initializes for capability query state checks", 0);
    query_expect(&vm, REXX_VM_OUTPUT_CAPABILITY_QUERY_NO_PROVIDER,
                 "missing writer and capability provider are distinct");

    destination_make_fixed_columns(&destination, 8u, 1u, 1u);
    check(destination_install(&vm, &destination),
          "fixed-column output provider installs on one VM", 0);
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.state == REXX_VM_OUTPUT_CAPABILITY_KNOWN &&
              capability.width_unit ==
                  REXX_VM_OUTPUT_WIDTH_CHARACTER_COLUMNS &&
              capability.logical_line_width == 8u,
          "known columns are copied by value with an explicit unit", 0);

    destination.metadata.state =
        REXX_VM_OUTPUT_CAPABILITY_NO_MEANINGFUL_WIDTH;
    destination.metadata.width_unit = REXX_VM_OUTPUT_WIDTH_NONE;
    destination.metadata.wrap_policy = REXX_VM_OUTPUT_WRAP_NONE;
    destination.metadata.logical_line_width = 0u;
    query_expect(&vm, REXX_VM_OUTPUT_CAPABILITY_QUERY_OK,
                 "raw redirected output may explicitly have no width");
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.state ==
                  REXX_VM_OUTPUT_CAPABILITY_NO_MEANINGFUL_WIDTH,
          "no meaningful width is distinct from missing destination", 0);

    destination.metadata.state =
        REXX_VM_OUTPUT_CAPABILITY_TEMPORARILY_UNKNOWN;
    destination.metadata.destination_kind =
        REXX_VM_OUTPUT_DESTINATION_INTERACTIVE;
    destination.metadata.wrap_policy = REXX_VM_OUTPUT_WRAP_UNKNOWN;
    query_expect(&vm, REXX_VM_OUTPUT_CAPABILITY_QUERY_OK,
                 "temporarily unknown width is reported as a state");
    destination.metadata.state =
        REXX_VM_OUTPUT_CAPABILITY_STALE;
    query_expect(&vm, REXX_VM_OUTPUT_CAPABILITY_QUERY_OK,
                 "provider can explicitly reject a stale destination view");

    destination.metadata = (RexxVMOutputCapability){0};
    destination.metadata.struct_size =
        (uint32_t)sizeof(destination.metadata);
    destination.metadata.state =
        REXX_VM_OUTPUT_CAPABILITY_DESTINATION_NOT_INSTALLED;
    destination.metadata.wrap_policy = REXX_VM_OUTPUT_WRAP_UNKNOWN;
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.state ==
                  REXX_VM_OUTPUT_CAPABILITY_DESTINATION_NOT_INSTALLED,
          "uninstalled destination is distinct from an absent reader", 0);

    destination_make_fixed_columns(&destination, 8u, 2u, 1u);
    destination.metadata.width_unit = (RexxVMOutputWidthUnit)99;
    query_expect(&vm, REXX_VM_OUTPUT_CAPABILITY_QUERY_INVALID_METADATA,
                 "unsupported width unit is rejected without conversion");
    destination_make_fixed_columns(&destination, 0u, 2u, 1u);
    query_expect(&vm, REXX_VM_OUTPUT_CAPABILITY_QUERY_INVALID_METADATA,
                 "zero known width cannot substitute for missing metadata");
    destination_make_fixed_columns(
        &destination, REXX_VM_OUTPUT_CAPABILITY_MAX_WIDTH + 1u, 2u, 1u);
    query_expect(&vm, REXX_VM_OUTPUT_CAPABILITY_QUERY_INVALID_METADATA,
                 "oversized width is rejected by the bounded contract");

    destination.metadata = (RexxVMOutputCapability){0};
    destination.metadata.struct_size =
        (uint32_t)sizeof(destination.metadata);
    destination.metadata.state = REXX_VM_OUTPUT_CAPABILITY_KNOWN;
    destination.metadata.destination_kind =
        REXX_VM_OUTPUT_DESTINATION_RECORD;
    destination.metadata.width_unit = REXX_VM_OUTPUT_WIDTH_RECORD_BYTES;
    destination.metadata.wrap_policy =
        REXX_VM_OUTPUT_WRAP_RECORD_BOUNDARY;
    destination.metadata.logical_line_width = 17u;
    destination.metadata.record_length = 16u;
    destination.metadata.destination_identity = 3u;
    destination.metadata.destination_generation = 1u;
    query_expect(&vm, REXX_VM_OUTPUT_CAPABILITY_QUERY_INVALID_METADATA,
                 "record length must equal the declared record-byte width");

    destination.read_status = REXX_VM_OUTPUT_CAPABILITY_READ_FAILURE;
    query_expect(&vm, REXX_VM_OUTPUT_CAPABILITY_QUERY_PROVIDER_FAILURE,
                 "provider failure has a distinct query status");
    destination.read_status = REXX_VM_OUTPUT_CAPABILITY_READ_UNSUPPORTED;
    query_expect(&vm, REXX_VM_OUTPUT_CAPABILITY_QUERY_UNSUPPORTED,
                 "unsupported capability query has a distinct status");

    destination.read_status = REXX_VM_OUTPUT_CAPABILITY_READ_OK;
    destination_make_fixed_columns(&destination, 8u, 2u, 1u);
    destination.resize_during_read = 1;
    destination.resize_width = 12u;
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.state == REXX_VM_OUTPUT_CAPABILITY_STALE &&
              capability.logical_line_width == 0u,
          "resize during observation returns an explicitly stale snapshot", 0);

    rexx_vm_test_allocator_fail_on(1u);
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              rexx_vm_test_allocator_attempts() == 0u,
          "capability query performs no VM allocation", 0);
    rexx_vm_test_allocator_disable();

    destination.teardown_during_read = 1;
    destination.replace_vm = &vm;
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK && vm.initialized,
          "VM teardown is rejected while a provider callback is active", 0);
    rexx_vm_free(&vm);
    check(!vm.initialized,
          "VM teardown succeeds after provider observation returns", 0);
}

static void test_provider_replacement_and_vm_isolation(void)
{
    RexxVM vm_a;
    RexxVM vm_b;
    RexxVMOutputCapability capability;
    Rx37Destination destination_a;
    Rx37Destination destination_b;

    memset(&destination_a, 0, sizeof(destination_a));
    memset(&destination_b, 0, sizeof(destination_b));
    destination_make_fixed_columns(&destination_a, 6u, 10u, 1u);
    destination_make_fixed_columns(&destination_b, 10u, 20u, 4u);
    check(rexx_vm_init(&vm_a, 16u) == REXX_VM_STATUS_OK &&
              rexx_vm_init(&vm_b, 16u) == REXX_VM_STATUS_OK,
          "two VMs initialize for output-provider isolation", 0);
    check(destination_install(&vm_a, &destination_a) &&
              destination_install(&vm_b, &destination_b),
          "each VM installs its own writer and width provider", 0);
    check(rexx_vm_get_output_capability(&vm_a, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.logical_line_width == 6u &&
              capability.destination_identity == 10u,
          "VM A observes only its six-column destination", 0);
    check(rexx_vm_get_output_capability(&vm_b, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.logical_line_width == 10u &&
              capability.destination_identity == 20u,
          "VM B observes only its ten-column destination", 0);

    destination_a.replace_vm = &vm_a;
    destination_a.replace_during_read = 1;
    check(rexx_vm_get_output_capability(&vm_a, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_STALE &&
              capability.state == REXX_VM_OUTPUT_CAPABILITY_STALE,
          "writer replacement during observation invalidates that snapshot", 0);
    query_expect(&vm_a, REXX_VM_OUTPUT_CAPABILITY_QUERY_NO_PROVIDER,
                 "replacement does not retain the previous reader callback");
    check(destination_install(&vm_a, &destination_a),
          "replacement destination can install a fresh reader", 0);
    check(rexx_vm_get_output_capability(&vm_a, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.destination_identity == 10u,
          "new observation sees only the replacement generation", 0);

    rexx_vm_free(&vm_a);
    rexx_vm_free(&vm_b);
}

static void test_destination_kinds_and_width_units(void)
{
    RexxVM vm;
    RexxVMOutputCapability capability;
    Rx37Destination destination;
    unsigned char payload[96];
    unsigned char oracle[160];
    size_t oracle_length = 0u;
    memset(&destination, 0, sizeof(destination));
    check(rexx_vm_init(&vm, 16u) == REXX_VM_STATUS_OK,
          "VM initializes for destination kind checks", 0);
    destination_make_fixed_columns(&destination, 8u, 1u, 1u);
    destination.metadata.destination_kind =
        REXX_VM_OUTPUT_DESTINATION_REDIRECTED;
    destination.metadata.state =
        REXX_VM_OUTPUT_CAPABILITY_NO_MEANINGFUL_WIDTH;
    destination.metadata.width_unit = REXX_VM_OUTPUT_WIDTH_NONE;
    destination.metadata.wrap_policy = REXX_VM_OUTPUT_WRAP_NONE;
    destination.metadata.logical_line_width = 0u;
    check(destination_install(&vm, &destination) &&
              rexx_vm_get_output_capability(&vm, &capability) ==
                  REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.destination_kind ==
                  REXX_VM_OUTPUT_DESTINATION_REDIRECTED &&
              capability.state ==
                  REXX_VM_OUTPUT_CAPABILITY_NO_MEANINGFUL_WIDTH,
          "raw redirected bytes report no inferred terminal width", 0);

    destination.metadata.destination_kind =
        REXX_VM_OUTPUT_DESTINATION_INTERACTIVE;
    destination.metadata.state = REXX_VM_OUTPUT_CAPABILITY_KNOWN;
    destination.metadata.width_unit =
        REXX_VM_OUTPUT_WIDTH_CHARACTER_COLUMNS;
    destination.metadata.wrap_policy =
        REXX_VM_OUTPUT_WRAP_DESTINATION_WIDTH;
    destination.metadata.logical_line_width = 8u;
    destination.metadata.destination_identity = 2u;
    ++destination.metadata.destination_generation;
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.destination_kind ==
                  REXX_VM_OUTPUT_DESTINATION_INTERACTIVE &&
              capability.logical_line_width == 8u &&
              capability.destination_generation == 2u,
          "redirect-to-interactive destination swap publishes a new generation", 0);
    destination.metadata.destination_kind =
        REXX_VM_OUTPUT_DESTINATION_REDIRECTED;
    destination.metadata.state =
        REXX_VM_OUTPUT_CAPABILITY_NO_MEANINGFUL_WIDTH;
    destination.metadata.width_unit = REXX_VM_OUTPUT_WIDTH_NONE;
    destination.metadata.wrap_policy = REXX_VM_OUTPUT_WRAP_NONE;
    destination.metadata.logical_line_width = 0u;
    destination.metadata.destination_identity = 3u;
    ++destination.metadata.destination_generation;
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.destination_kind ==
                  REXX_VM_OUTPUT_DESTINATION_REDIRECTED &&
              capability.state ==
                  REXX_VM_OUTPUT_CAPABILITY_NO_MEANINGFUL_WIDTH &&
              capability.destination_generation == 3u,
          "interactive-to-raw-redirection swap clears stale column metadata", 0);

    destination.metadata.state = REXX_VM_OUTPUT_CAPABILITY_KNOWN;
    destination.metadata.destination_kind =
        REXX_VM_OUTPUT_DESTINATION_BACKGROUND;
    destination.metadata.width_unit = REXX_VM_OUTPUT_WIDTH_RECORD_BYTES;
    destination.metadata.wrap_policy =
        REXX_VM_OUTPUT_WRAP_RECORD_BOUNDARY;
    destination.metadata.logical_line_width = 43u;
    destination.metadata.record_length = 43u;
    destination.metadata.destination_generation = 2u;
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.destination_kind ==
                  REXX_VM_OUTPUT_DESTINATION_BACKGROUND &&
              capability.width_unit == REXX_VM_OUTPUT_WIDTH_RECORD_BYTES &&
              capability.record_length == 43u,
          "background record policy exposes its configured record-byte width", 0);
    memset(payload, 'B', 50u);
    destination_reset_output(&destination);
    check(run_say_literal(&vm, payload, 50u) &&
              reference_format(destination.input, destination.input_length,
                              43u, oracle, sizeof(oracle), &oracle_length) &&
              destination.formatted_length == oracle_length &&
              memcmp(destination.formatted, oracle, oracle_length) == 0,
          "background output follows its configured record-byte boundary", 0);

    destination.metadata.destination_kind =
        REXX_VM_OUTPUT_DESTINATION_RECORD;
    destination.metadata.destination_identity = 2u;
    destination.metadata.destination_generation = 3u;
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.destination_kind ==
                  REXX_VM_OUTPUT_DESTINATION_RECORD,
          "record-oriented output retains record-byte units", 0);
    memset(payload, 'Q', 86u);
    destination_reset_output(&destination);
    check(run_say_literal(&vm, payload, 86u) &&
              reference_format(destination.input, destination.input_length,
                              43u, oracle, sizeof(oracle), &oracle_length) &&
              destination.formatted_length == oracle_length &&
              memcmp(destination.formatted, oracle, oracle_length) == 0,
          "record-oriented SAY boundaries match the declared record length", 0);

    destination.metadata.destination_kind =
        REXX_VM_OUTPUT_DESTINATION_INTERACTIVE;
    destination.metadata.destination_identity = 3u;
    destination.metadata.destination_generation = 4u;
    destination.metadata.width_unit = REXX_VM_OUTPUT_WIDTH_PIXELS;
    destination.metadata.wrap_policy =
        REXX_VM_OUTPUT_WRAP_DESTINATION_WIDTH;
    destination.metadata.logical_line_width = 640u;
    destination.metadata.record_length = 0u;
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.width_unit == REXX_VM_OUTPUT_WIDTH_PIXELS,
          "pixel width remains explicitly pixel-valued", 0);
    destination.metadata.destination_generation = 4u;
    destination.metadata.destination_kind =
        REXX_VM_OUTPUT_DESTINATION_INTERACTIVE;
    destination.metadata.width_unit = REXX_VM_OUTPUT_WIDTH_PIXELS;
    destination.metadata.logical_line_width = 800u;
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.width_unit == REXX_VM_OUTPUT_WIDTH_PIXELS &&
              capability.logical_line_width == 800u &&
              capability.destination_generation == 4u,
          "font or renderer reconfiguration publishes pixels without column conversion", 0);
    destination.metadata.state =
        REXX_VM_OUTPUT_CAPABILITY_NO_MEANINGFUL_WIDTH;
    destination.metadata.width_unit = REXX_VM_OUTPUT_WIDTH_NONE;
    destination.metadata.wrap_policy = REXX_VM_OUTPUT_WRAP_NONE;
    destination.metadata.logical_line_width = 0u;
    destination.metadata.destination_generation = 5u;
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.state ==
                  REXX_VM_OUTPUT_CAPABILITY_NO_MEANINGFUL_WIDTH &&
              capability.destination_generation == 5u,
          "renderer reconfiguration may revoke width without leaving a stale value", 0);
    rexx_vm_free(&vm);
}

static void test_say_formatting_and_resize(void)
{
    static const size_t lengths[] = {
        0u, 1u, 7u, 8u, 9u, 16u, 17u, 35u, 119u, 120u, 238u, 377u
    };
    RexxVM vm;
    RexxVMOutputCapability capability;
    Rx37Destination destination;
    unsigned char payload[512];
    unsigned char oracle[RX37_CAPTURE_CAPACITY];
    size_t oracle_length;
    size_t index;
    size_t before_resize_length;
    size_t before_input_length;
    unsigned char before_resize[RX37_CAPTURE_CAPACITY];
    memset(&destination, 0, sizeof(destination));
    destination_make_fixed_columns(&destination, 8u, 31u, 1u);
    check(rexx_vm_init(&vm, 32u) == REXX_VM_STATUS_OK &&
              destination_install(&vm, &destination),
          "resizable fixed-column destination installs", 0);
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.logical_line_width == 8u &&
              capability.destination_generation == 1u,
          "initial resize capability has its starting generation", 0);

    for (index = 0u; index < sizeof(lengths) / sizeof(lengths[0]); ++index) {
        size_t length = lengths[index];
        memset(payload, 'A', length);
        destination_reset_output(&destination);
        check(run_say_literal(&vm, payload, length),
              "SAY executes for a deterministic width/chunk boundary", 0);
        check(destination.input_length == length + 1u &&
                  destination.input[length] == (unsigned char)'\n',
              "host writer receives value bytes plus the explicit SAY LF", 0);
        check(reference_format(destination.input, destination.input_length,
                               8u, oracle, sizeof(oracle), &oracle_length) &&
                  destination.formatted_length == oracle_length &&
                  memcmp(destination.formatted, oracle, oracle_length) == 0,
              "actual mock wrapping matches the independent width oracle", 0);
        if (length > RX37_WRITER_PAYLOAD_LIMIT)
            check(destination.callback_chunks > 1u,
                  "119-byte callback chunks do not reset logical wrap state", 0);
        if (length == 119u || length == 238u || length == 377u)
            check(destination.callback_chunks > 1u,
                  "large SAY values cross the existing storage chunk size", 0);
    }

    destination_reset_output(&destination);
    check(execute_source(&vm, "SAY 'A'||X2C('0D0A')||'B'\n"),
          "explicit CR/LF SAY value executes", 0);
    check(destination.input_length == 5u &&
              destination.input[0] == 'A' && destination.input[1] == 0x0du &&
              destination.input[2] == 0x0au && destination.input[3] == 'B' &&
              destination.input[4] == 0x0au,
          "explicit CR/LF bytes and the SAY LF are preserved separately", 0);
    check(destination.formatted_length == 5u &&
              destination.formatted[0] == 'A' &&
              destination.formatted[1] == 0x0du &&
              destination.formatted[2] == 0x0au &&
              destination.formatted[3] == 'B' &&
              destination.formatted[4] == 0x0au,
          "explicit newline and blank-line behavior remains distinct from wrap", 0);

    destination_reset_output(&destination);
    check(execute_source(&vm, "SAY 'A'||X2C('0A0A')||'B'\n"),
          "consecutive explicit LFs execute", 0);
    check(destination.input_length == 5u &&
              destination.formatted_length == 5u &&
              memcmp(destination.input, "A\n\nB\n", 5u) == 0 &&
              memcmp(destination.formatted, "A\n\nB\n", 5u) == 0,
          "explicit blank lines do not count as automatic wrapping", 0);

    destination_reset_output(&destination);
    check(execute_source(&vm, "SAY X2C('0080FF')\n"),
          "binary and high-bit SAY value executes", 0);
    check(destination.input_length == 4u &&
              destination.input[0] == 0u && destination.input[1] == 0x80u &&
              destination.input[2] == 0xffu && destination.input[3] == 0x0au,
          "NUL and high-bit bytes survive the per-VM output writer", 0);

    destination_reset_output(&destination);
    memset(payload, 'R', 23u);
    check(run_say_literal(&vm, payload, 23u),
          "pre-resize output is captured", 0);
    before_resize_length = destination.formatted_length;
    before_input_length = destination.input_length;
    memcpy(before_resize, destination.formatted, before_resize_length);
    destination.resize_width = 12u;
    destination.metadata.logical_line_width = 12u;
    ++destination.metadata.destination_generation;
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.logical_line_width == 12u &&
              capability.destination_generation == 2u,
          "host expansion increments generation and publishes the new width", 0);
    check(before_resize_length <= destination.formatted_length &&
              memcmp(before_resize, destination.formatted,
                     before_resize_length) == 0,
          "resize does not retroactively rewrite historical output", 0);
    memset(payload, 'C', 13u);
    check(run_say_literal(&vm, payload, 13u),
          "post-expansion SAY executes", 0);
    check(before_resize_length <= destination.formatted_length &&
              memcmp(before_resize, destination.formatted,
                     before_resize_length) == 0 &&
              reference_format(destination.input + before_input_length,
                               destination.input_length - before_input_length,
                               12u, oracle, sizeof(oracle), &oracle_length) &&
              destination.formatted_length ==
                  before_resize_length + oracle_length &&
              memcmp(destination.formatted + before_resize_length, oracle,
                     oracle_length) == 0,
          "expanded width formats new output and keeps history unchanged", 0);

    before_resize_length = destination.formatted_length;
    before_input_length = destination.input_length;
    memcpy(before_resize, destination.formatted, before_resize_length);
    destination.resize_width = 5u;
    destination.metadata.logical_line_width = 5u;
    ++destination.metadata.destination_generation;
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.logical_line_width == 5u &&
              capability.destination_generation == 3u,
          "host contraction publishes the current width and generation", 0);
    memset(payload, 'D', 6u);
    check(run_say_literal(&vm, payload, 6u),
          "post-contraction SAY executes", 0);
    check(before_resize_length <= destination.formatted_length &&
              memcmp(before_resize, destination.formatted,
                     before_resize_length) == 0 &&
              reference_format(destination.input + before_input_length,
                               destination.input_length - before_input_length,
                               5u, oracle, sizeof(oracle), &oracle_length) &&
              destination.formatted_length ==
                  before_resize_length + oracle_length &&
              memcmp(destination.formatted + before_resize_length, oracle,
                     oracle_length) == 0,
          "contracted width formats new output and keeps history unchanged", 0);
    rexx_vm_free(&vm);
}

static void test_output_failure_recovery_and_routines(void)
{
    RexxVM vm;
    RexxVMOutputCapability capability;
    Rx37Destination destination;
    RexxBytecodeChunk *chunk = NULL;
    static const char routine_source[] =
        "CALL INNER\n"
        "INTERPRET \"SAY 'I'\"\n"
        "RETURN\n"
        "INNER:\n"
        "SAY 'R'\n"
        "RETURN\n";
    memset(&destination, 0, sizeof(destination));
    destination_make_fixed_columns(&destination, 4u, 40u, 1u);
    check(rexx_vm_init(&vm, 32u) == REXX_VM_STATUS_OK &&
              destination_install(&vm, &destination),
          "VM installs the provider used during routine lifetime checks", 0);
    check(compile_source(routine_source, &chunk),
          "internal routine and INTERPRET program compiles", 0);
    if (chunk != NULL) {
        check(rexx_vm_execute(&vm, chunk) == REXX_VM_STATUS_OK,
              "SAY remains operational through internal calls and INTERPRET", 0);
        rexx_bytecode_chunk_free(chunk);
    }
    check(destination.input_length == 4u &&
              memcmp(destination.input, "R\nI\n", 4u) == 0,
          "routine and INTERPRET output reach the same bound destination", 0);

    destination.fail_next_write = 1;
    check(!execute_source(&vm, "SAY 'F'\n") &&
              vm.status == REXX_VM_STATUS_IO_ERROR,
          "writer failure propagates as a REXX I/O error", 0);
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.logical_line_width == 4u,
          "writer failure does not poison the read-only capability", 0);
    destination_reset_output(&destination);
    destination.fail_partially = 1;
    destination.partial_failure_bytes = 2u;
    check(!execute_source(&vm, "SAY 'PARTIAL'\n") &&
              vm.status == REXX_VM_STATUS_IO_ERROR &&
              destination.input_length == 2u &&
              memcmp(destination.input, "PA", 2u) == 0,
          "interrupted output preserves only the writer-accepted prefix", 0);
    destination_reset_output(&destination);
    check(execute_source(&vm, "SAY 'OK'\n") &&
              destination.input_length == 3u &&
              memcmp(destination.input, "OK\n", 3u) == 0,
          "writer recovers for subsequent SAY output", 0);
    rexx_vm_free(&vm);
}

static void test_fixed_seed_independent_properties(void)
{
    RexxVM vm;
    RexxVMOutputCapability capability;
    Rx37Destination destination;
    unsigned char payload[512];
    unsigned char oracle[RX37_CAPTURE_CAPACITY];
    uint32_t random_state = RX37_SEED;
    size_t oracle_length;
    size_t case_index;
    memset(&destination, 0, sizeof(destination));
    destination_make_fixed_columns(&destination, 23u, 51u, 1u);
    check(rexx_vm_init(&vm, 32u) == REXX_VM_STATUS_OK &&
              destination_install(&vm, &destination),
          "VM and deterministic property destination initialize", 0);
    check(rexx_vm_get_output_capability(&vm, &capability) ==
              REXX_VM_OUTPUT_CAPABILITY_QUERY_OK &&
              capability.logical_line_width == 23u,
          "property suite separately checks declared width", 0);
    for (case_index = 0u; case_index < RX37_CASES; ++case_index) {
        size_t length = (size_t)(next_random(&random_state) % 512u);
        size_t index;
        for (index = 0u; index < length; ++index)
            payload[index] = (unsigned char)('A' +
                (next_random(&random_state) % 26u));
        destination_reset_output(&destination);
        if (!run_say_literal(&vm, payload, length) ||
            destination.input_length != length + 1u ||
            destination.input[length] != (unsigned char)'\n' ||
            !reference_format(destination.input, destination.input_length,
                              23u, oracle, sizeof(oracle), &oracle_length) ||
            destination.formatted_length != oracle_length ||
            memcmp(destination.formatted, oracle, oracle_length) != 0) {
            check(0, "fixed-seed output matches independent reference model", 1);
            break;
        }
        ++property_cases;
        check(1, "fixed-seed output matches independent reference model", 1);
    }
    rexx_vm_free(&vm);
}

int main(void)
{
    test_query_states_and_validation();
    test_provider_replacement_and_vm_isolation();
    test_destination_kinds_and_width_units();
    test_say_formatting_and_resize();
    test_output_failure_recovery_and_routines();
    test_fixed_seed_independent_properties();
    rexx_vm_test_allocator_disable();
    printf("RX37 output capability assertions: %lu total; %lu direct; "
           "%lu property (%lu cases); %lu failures; seed=0x%08lx\n",
           (unsigned long)assertions,
           (unsigned long)direct_assertions,
           (unsigned long)property_assertions,
           (unsigned long)property_cases,
           (unsigned long)failures,
           (unsigned long)RX37_SEED);
    return failures == 0u && property_cases == RX37_CASES ? 0 : 1;
}
