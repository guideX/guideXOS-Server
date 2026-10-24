#include "../rexx_builtin.h"
#include "../rexx_compiler.h"
#include "../rexx_lexer.h"
#include "../rexx_parser.h"
#include "../rexx_vm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX33_PROPERTY_SEED UINT32_C(0x52583333)
#define RX34_PROPERTY_SEED UINT32_C(0x52583434)
#define RX33_PROPERTY_CASES ((size_t)4096u)
#define RX34_PROPERTY_CASES ((size_t)4096u)
#define RX33_CAPTURE_CAPACITY ((size_t)8192u)
#define RX33_STREAM_CAPACITY ((size_t)32u)

static const unsigned char rx33_sample_bytes[] = "ABCDEF";
static const unsigned char rx33_mixed_bytes[] = "MiXeD Input";
static const unsigned char rx33_binary_bytes[] = {
    0x41u, 0x00u, 0x80u, 0xffu, 0x42u, 0x0au, 0x43u
};
static const unsigned char rx33_default_bytes[] = "DEFAULT";
static const unsigned char rx33_other_bytes[] = "WXYZ";
static const unsigned char rx33_noseek_bytes[] = "NS";
static const unsigned char rx33_crlf_bytes[] = { 'A', '\r', '\n', 'B' };

typedef struct Rx33Stream {
    const unsigned char *name;
    size_t name_length;
    const unsigned char *bytes;
    size_t length;
    size_t position;
    int seekable;
} Rx33Stream;

typedef enum Rx33ReadFault {
    RX33_READ_FAULT_NONE = 0,
    RX33_READ_FAULT_ERROR,
    RX33_READ_FAULT_UNAVAILABLE,
    RX33_READ_FAULT_PENDING,
    RX33_READ_FAULT_UNSUPPORTED,
    RX33_READ_FAULT_NULL_SPAN,
    RX33_READ_FAULT_OVERSIZED_LENGTH,
    RX33_READ_FAULT_BAD_OWNERSHIP,
    RX33_READ_FAULT_SHORT_WITHOUT_EOF,
    RX33_READ_FAULT_BAD_EOF_FLAG,
    RX33_READ_FAULT_EOF_WITH_DATA
} Rx33ReadFault;

typedef struct Rx33Provider {
    Rx33Stream streams[RX33_STREAM_CAPACITY];
    size_t stream_count;
    int default_stream;
    unsigned char repeated_bytes[256];
    size_t resolve_calls;
    size_t read_calls;
    size_t seek_calls;
    size_t last_stream_index;
    int last_lookup_was_default;
    Rx33ReadFault next_read_fault;
    RexxVMByteStreamResolveStatus next_resolve_status;
    int next_resolve_invalid_metadata;
    int next_resolve_oversized_identity;
    int next_seek_unsupported;
    int next_seek_error;
    RexxVM *mutation_vm;
    int mutate_on_resolve;
    int free_vm_on_read;
    RexxVMStatus mutation_status;
    size_t context_release_calls;
} Rx33Provider;

typedef struct Rx33Capture {
    unsigned char bytes[RX33_CAPTURE_CAPACITY];
    size_t length;
} Rx33Capture;

static size_t assertions;
static size_t failures;
static size_t direct_assertions;
static size_t property_cases;
static size_t property_assertions;

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

static RexxVMStatus text_value(RexxValue *value, const char *text)
{
    return rexx_value_set_string(value, text, strlen(text));
}

static RexxVMStatus call_charin(RexxVM *vm, const RexxValue *arguments,
                                size_t argument_count, RexxValue *result)
{
    return rexx_builtin_dispatch_with_vm(vm, "CHARIN", 6u, arguments,
                                         argument_count, result);
}

static void add_stream(Rx33Provider *provider, const char *name,
                       const unsigned char *bytes, size_t length,
                       int seekable)
{
    Rx33Stream *stream;
    if (provider == NULL || provider->stream_count >= RX33_STREAM_CAPACITY)
        return;
    stream = &provider->streams[provider->stream_count++];
    stream->name = (const unsigned char *)name;
    stream->name_length = strlen(name);
    stream->bytes = bytes;
    stream->length = length;
    stream->position = 0u;
    stream->seekable = seekable;
}

static void provider_init(Rx33Provider *provider)
{
    size_t index;
    uint32_t seed = RX33_PROPERTY_SEED;
    memset(provider, 0, sizeof(*provider));
    provider->default_stream = 0;
    provider->last_stream_index = SIZE_MAX;
    add_stream(provider, "default", rx33_default_bytes,
               sizeof(rx33_default_bytes) - 1u, 1);
    add_stream(provider, "sample", rx33_sample_bytes,
               sizeof(rx33_sample_bytes) - 1u, 1);
    add_stream(provider, "mixed", rx33_mixed_bytes,
               sizeof(rx33_mixed_bytes) - 1u, 1);
    add_stream(provider, "binary", rx33_binary_bytes,
               sizeof(rx33_binary_bytes), 1);
    add_stream(provider, "empty", NULL, 0u, 1);
    add_stream(provider, "other", rx33_other_bytes,
               sizeof(rx33_other_bytes) - 1u, 1);
    add_stream(provider, "noseek", rx33_noseek_bytes,
               sizeof(rx33_noseek_bytes) - 1u, 0);
    add_stream(provider, "crlf", rx33_crlf_bytes,
               sizeof(rx33_crlf_bytes), 1);
    for (index = 0u; index < sizeof(provider->repeated_bytes); ++index) {
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        provider->repeated_bytes[index] = (unsigned char)(seed >> 24);
    }
    provider->streams[provider->stream_count].name =
        (const unsigned char *)"repeated";
    provider->streams[provider->stream_count].name_length = 8u;
    provider->streams[provider->stream_count].bytes = provider->repeated_bytes;
    provider->streams[provider->stream_count].length =
        sizeof(provider->repeated_bytes);
    provider->streams[provider->stream_count].position = 0u;
    provider->streams[provider->stream_count].seekable = 1;
    ++provider->stream_count;
}

static size_t provider_stream_index(const Rx33Provider *provider,
                                    const void *identity)
{
    size_t index;
    if (provider == NULL) return SIZE_MAX;
    for (index = 0u; index < provider->stream_count; ++index)
        if (identity == &provider->streams[index]) return index;
    return SIZE_MAX;
}

static RexxVMByteStreamResolveStatus provider_resolve(
    void *user_data, int use_default, const unsigned char *name,
    size_t name_length, RexxVMByteStreamInfo *stream)
{
    Rx33Provider *provider = (Rx33Provider *)user_data;
    size_t index;

    if (provider == NULL || stream == NULL)
        return REXX_VM_BYTE_STREAM_RESOLVE_ERROR;
    ++provider->resolve_calls;
    provider->last_lookup_was_default = use_default;
    provider->last_stream_index = SIZE_MAX;
    if (provider->mutate_on_resolve && provider->mutation_vm != NULL) {
        provider->mutation_status = rexx_vm_set_byte_stream_provider(
            provider->mutation_vm, NULL, NULL);
        provider->mutate_on_resolve = 0;
    }
    if (provider->next_resolve_invalid_metadata) {
        provider->next_resolve_invalid_metadata = 0;
        stream->identity = NULL;
        stream->stable_identity = (const unsigned char *)"badmeta";
        stream->stable_identity_length = 7u;
        stream->identity_generation = UINT64_C(1);
        stream->capabilities = REXX_VM_BYTE_STREAM_CAP_READABLE;
        stream->ownership = REXX_VM_BYTE_STREAM_IDENTITY_BORROWED;
        return REXX_VM_BYTE_STREAM_RESOLVE_OK;
    }
    if (provider->next_resolve_status != REXX_VM_BYTE_STREAM_RESOLVE_OK) {
        RexxVMByteStreamResolveStatus status = provider->next_resolve_status;
        provider->next_resolve_status = REXX_VM_BYTE_STREAM_RESOLVE_OK;
        return status;
    }
    if (use_default) {
        if (provider->default_stream < 0 ||
            (size_t)provider->default_stream >= provider->stream_count)
            return REXX_VM_BYTE_STREAM_RESOLVE_UNKNOWN;
        index = (size_t)provider->default_stream;
    } else {
        if (name == NULL && name_length != 0u)
            return REXX_VM_BYTE_STREAM_RESOLVE_ERROR;
        for (index = 0u; index < provider->stream_count; ++index) {
            Rx33Stream *candidate = &provider->streams[index];
            if (candidate->name_length == name_length &&
                (name_length == 0u ||
                 memcmp(candidate->name, name, name_length) == 0))
                break;
        }
        if (index == provider->stream_count)
            return REXX_VM_BYTE_STREAM_RESOLVE_UNKNOWN;
    }
    provider->last_stream_index = index;
    stream->identity = &provider->streams[index];
    stream->stable_identity = provider->streams[index].name;
    stream->stable_identity_length = provider->streams[index].name_length;
    stream->identity_generation = UINT64_C(1);
    if (provider->next_resolve_oversized_identity) {
        provider->next_resolve_oversized_identity = 0;
        stream->stable_identity_length =
            REXX_VM_BYTE_STREAM_IDENTITY_MAX_BYTES + 1u;
    }
    stream->capabilities = REXX_VM_BYTE_STREAM_CAP_READABLE;
    if (provider->streams[index].seekable)
        stream->capabilities |= REXX_VM_BYTE_STREAM_CAP_SEEKABLE;
    stream->ownership = REXX_VM_BYTE_STREAM_IDENTITY_BORROWED;
    return REXX_VM_BYTE_STREAM_RESOLVE_OK;
}

static RexxVMByteStreamSeekStatus provider_seek(void *user_data,
                                               const void *identity,
                                               uint64_t one_based_position)
{
    Rx33Provider *provider = (Rx33Provider *)user_data;
    size_t index = provider_stream_index(provider, identity);
    Rx33Stream *stream;
    if (provider == NULL || index == SIZE_MAX)
        return REXX_VM_BYTE_STREAM_SEEK_ERROR;
    ++provider->seek_calls;
    stream = &provider->streams[index];
    if (!stream->seekable) return REXX_VM_BYTE_STREAM_SEEK_UNSUPPORTED;
    if (provider->next_seek_unsupported) {
        provider->next_seek_unsupported = 0;
        return REXX_VM_BYTE_STREAM_SEEK_UNSUPPORTED;
    }
    if (provider->next_seek_error) {
        provider->next_seek_error = 0;
        return REXX_VM_BYTE_STREAM_SEEK_ERROR;
    }
    if (one_based_position == 0u ||
        one_based_position > (uint64_t)stream->length + UINT64_C(1))
        return REXX_VM_BYTE_STREAM_SEEK_ERROR;
    stream->position = (size_t)(one_based_position - UINT64_C(1));
    return REXX_VM_BYTE_STREAM_SEEK_OK;
}

static RexxVMByteStreamReadStatus provider_read(
    void *user_data, const void *identity, size_t requested,
    RexxVMByteStreamRead *read_result)
{
    Rx33Provider *provider = (Rx33Provider *)user_data;
    size_t index = provider_stream_index(provider, identity);
    Rx33Stream *stream;
    size_t available, count;
    Rx33ReadFault fault;

    if (provider == NULL || index == SIZE_MAX || read_result == NULL)
        return REXX_VM_BYTE_STREAM_READ_ERROR;
    ++provider->read_calls;
    if (provider->free_vm_on_read && provider->mutation_vm != NULL) {
        rexx_vm_free(provider->mutation_vm);
        provider->free_vm_on_read = 0;
    }
    fault = provider->next_read_fault;
    provider->next_read_fault = RX33_READ_FAULT_NONE;
    switch (fault) {
    case RX33_READ_FAULT_ERROR:
        return REXX_VM_BYTE_STREAM_READ_ERROR;
    case RX33_READ_FAULT_UNAVAILABLE:
        return REXX_VM_BYTE_STREAM_READ_UNAVAILABLE;
    case RX33_READ_FAULT_PENDING:
        return REXX_VM_BYTE_STREAM_READ_PENDING;
    case RX33_READ_FAULT_UNSUPPORTED:
        return REXX_VM_BYTE_STREAM_READ_UNSUPPORTED;
    default:
        break;
    }
    stream = &provider->streams[index];
    if (stream->position >= stream->length)
        return REXX_VM_BYTE_STREAM_READ_EOF;
    available = stream->length - stream->position;
    count = available < requested ? available : requested;
    read_result->bytes = stream->bytes + stream->position;
    read_result->length = count;
    read_result->ownership = REXX_VM_INPUT_OWNERSHIP_BORROWED;
    read_result->eof_after_read = (uint8_t)(count == available ? 1u : 0u);
    switch (fault) {
    case RX33_READ_FAULT_NULL_SPAN:
        read_result->bytes = NULL;
        break;
    case RX33_READ_FAULT_OVERSIZED_LENGTH:
        read_result->length = requested + 1u;
        break;
    case RX33_READ_FAULT_BAD_OWNERSHIP:
        read_result->ownership = REXX_VM_INPUT_OWNERSHIP_UNSPECIFIED;
        break;
    case RX33_READ_FAULT_SHORT_WITHOUT_EOF:
        if (read_result->length > 1u) read_result->length = 1u;
        read_result->eof_after_read = 0u;
        stream->position += read_result->length;
        return REXX_VM_BYTE_STREAM_READ_OK;
    case RX33_READ_FAULT_BAD_EOF_FLAG:
        read_result->eof_after_read = 2u;
        break;
    case RX33_READ_FAULT_EOF_WITH_DATA:
        return REXX_VM_BYTE_STREAM_READ_EOF;
    default:
        stream->position += count;
        return REXX_VM_BYTE_STREAM_READ_OK;
    }
    return REXX_VM_BYTE_STREAM_READ_OK;
}

static const RexxVMByteStreamProvider rx33_provider_callbacks = {
    provider_resolve, provider_read, provider_seek
};

static int capture_write(void *user_data, const char *bytes, size_t length)
{
    Rx33Capture *capture = (Rx33Capture *)user_data;
    if (capture == NULL || (bytes == NULL && length != 0u) ||
        length > sizeof(capture->bytes) - capture->length)
        return 0;
    if (length != 0u)
        memcpy(capture->bytes + capture->length, bytes, length);
    capture->length += length;
    return 1;
}

static RexxVMInputReadStatus line_provider_probe(void *user_data,
                                                 RexxVMInputLine *line)
{
    size_t *calls = (size_t *)user_data;
    (void)line;
    if (calls != NULL) ++*calls;
    return REXX_VM_INPUT_READ_ERROR;
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

static RexxVMStatus prepare_named_call(RexxValue args[3], const char *name,
                                       const char *start,
                                       const char *length, size_t argc)
{
    RexxVMStatus status;
    memset(args, 0, 3u * sizeof(*args));
    if (argc > 0u && name != NULL) {
        status = text_value(&args[0], name);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    if (argc > 1u && start != NULL) {
        status = text_value(&args[1], start);
        if (status != REXX_VM_STATUS_OK) return status;
    }
    if (argc > 2u && length != NULL)
        return text_value(&args[2], length);
    return REXX_VM_STATUS_OK;
}

static void clear_args(RexxValue args[3])
{
    size_t index;
    for (index = 0u; index < 3u; ++index) rexx_value_clear(&args[index]);
}

static void test_signature_positions_and_bytes(void)
{
    Rx33Provider provider;
    RexxVM vm;
    RexxValue args[3] = { REXX_VALUE_INIT, REXX_VALUE_INIT,
                          REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    size_t before_reads;
    static const unsigned char expected_crlf[] = { 'A', '\r', '\n', 'B' };

    provider_init(&provider);
    memset(&vm, 0, sizeof(vm));
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "RX33 signature VM initializes");
    if (!vm.initialized) return;
    check(rexx_vm_set_byte_stream_provider(
              &vm, &rx33_provider_callbacks, &provider) == REXX_VM_STATUS_OK,
          "per-VM memory byte-stream provider installs");

    status = call_charin(&vm, NULL, 0u, &result);
    check(status == REXX_VM_STATUS_OK &&
              string_is(&result, "D", 1u) &&
              provider.last_lookup_was_default,
          "CHARIN() selects the configured default and reads one byte");
    rexx_value_clear(&result);

    check(prepare_named_call(args, "sample", NULL, "3", 3u) ==
              REXX_VM_STATUS_OK,
          "named length-three call arguments are prepared");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "ABC", 3u),
          "CHARIN(name,,length) reads exact initial bytes");
    rexx_value_clear(&result);
    clear_args(args);

    check(prepare_named_call(args, "sample", NULL, "2", 3u) ==
              REXX_VM_STATUS_OK,
          "second sequential read arguments are prepared");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "DE", 2u),
          "consecutive CHARIN calls continue at the provider-owned cursor");
    rexx_value_clear(&result);
    clear_args(args);

    check(prepare_named_call(args, "sample", "2", "2", 3u) ==
              REXX_VM_STATUS_OK,
          "explicit position call arguments are prepared");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "BC", 2u),
          "explicit start is a one-based byte position");
    rexx_value_clear(&result);
    clear_args(args);

    check(prepare_named_call(args, "sample", "1", "0", 3u) ==
              REXX_VM_STATUS_OK,
          "zero-length reposition arguments are prepared");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "", 0u) &&
              provider.streams[1].position == 0u,
          "zero length repositions without consuming bytes");
    rexx_value_clear(&result);
    clear_args(args);

    check(prepare_named_call(args, "sample", NULL, "2", 3u) ==
              REXX_VM_STATUS_OK,
          "post-seek sequential arguments are prepared");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "AB", 2u),
          "zero-length explicit position changes the next read position");
    rexx_value_clear(&result);
    clear_args(args);

    check(prepare_named_call(args, "sample", "5", NULL, 2u) ==
              REXX_VM_STATUS_OK,
          "omitted length with explicit start arguments are prepared");
    status = call_charin(&vm, args, 2u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "E", 1u),
          "CHARIN(name,start) treats start as position and defaults length to one");
    rexx_value_clear(&result);
    clear_args(args);

    check(prepare_named_call(args, "sample", "6", "1", 3u) ==
              REXX_VM_STATUS_OK,
          "last-byte read arguments are prepared");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "F", 1u),
          "a larger valid start reads the last byte");
    rexx_value_clear(&result);
    clear_args(args);

    check(prepare_named_call(args, "sample", "1", "0", 3u) ==
              REXX_VM_STATUS_OK,
          "zero-read cursor reset arguments are prepared");
    (void)call_charin(&vm, args, 3u, &result);
    rexx_value_clear(&result);
    clear_args(args);
    before_reads = provider.read_calls;
    check(prepare_named_call(args, "sample", NULL, "0", 3u) ==
              REXX_VM_STATUS_OK,
          "sequential zero-length call arguments are prepared");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "", 0u) &&
              provider.read_calls == before_reads &&
              provider.streams[1].position == 0u,
          "zero-length sequential request resolves but does not read or advance");
    rexx_value_clear(&result);
    clear_args(args);

    check(prepare_named_call(args, NULL, "2", "3", 3u) ==
              REXX_VM_STATUS_OK,
          "omitted name with explicit start and length arguments are prepared");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "EFA", 3u) &&
              provider.last_lookup_was_default,
          "CHARIN(,start,length) preserves the omitted name position");
    rexx_value_clear(&result);
    clear_args(args);

    check(prepare_named_call(args, "mixed", NULL, NULL, 1u) ==
              REXX_VM_STATUS_OK,
          "mixed-case default-length arguments are prepared");
    status = call_charin(&vm, args, 1u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "M", 1u),
          "named stream with omitted length reads one byte");
    rexx_value_clear(&result);
    clear_args(args);

    check(prepare_named_call(args, "binary", NULL, "7", 3u) ==
              REXX_VM_STATUS_OK,
          "binary read arguments are prepared");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_OK &&
              string_is(&result, rx33_binary_bytes,
                        sizeof(rx33_binary_bytes)),
          "CHARIN preserves NUL, high-bit, LF, and all explicit byte lengths");
    rexx_value_clear(&result);
    clear_args(args);

    check(prepare_named_call(args, "crlf", NULL, "4", 3u) ==
              REXX_VM_STATUS_OK,
          "CR/LF read arguments are prepared");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_OK &&
              string_is(&result, expected_crlf, sizeof(expected_crlf)),
          "CHARIN preserves carriage return and line feed bytes");
    rexx_value_clear(&result);
    clear_args(args);

    check(prepare_named_call(args, "empty", NULL, "0", 3u) ==
              REXX_VM_STATUS_OK,
          "empty zero-read arguments are prepared");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "", 0u),
          "zero-length read on an empty stream is a valid empty result");
    rexx_value_clear(&result);
    clear_args(args);
    check(prepare_named_call(args, "empty", NULL, NULL, 1u) ==
              REXX_VM_STATUS_OK,
          "empty EOF arguments are prepared");
    status = call_charin(&vm, args, 1u, &result);
    check(status == REXX_VM_STATUS_BYTE_STREAM_END_OF_FILE &&
              result.type == REXX_VALUE_UNSET,
          "EOF before any byte is returned distinctly from an empty successful read");
    clear_args(args);

    check(prepare_named_call(args, "sample", "5", "9", 3u) ==
              REXX_VM_STATUS_OK,
          "short-read arguments are prepared");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "EF", 2u) &&
              provider.streams[1].position == 6u,
          "EOF after partial data returns the exact short byte string");
    rexx_value_clear(&result);
    clear_args(args);
    check(prepare_named_call(args, "sample", NULL, "1", 3u) ==
              REXX_VM_STATUS_OK,
          "following EOF arguments are prepared");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_BYTE_STREAM_END_OF_FILE,
          "next read after a short EOF reports end of stream");
    clear_args(args);

    check(rexx_vm_set_byte_stream_provider(&vm, NULL, NULL) ==
              REXX_VM_STATUS_OK,
          "byte-stream provider clears while VM is quiescent");
    status = call_charin(&vm, NULL, 0u, &result);
    check(status == REXX_VM_STATUS_BYTE_STREAM_UNAVAILABLE,
          "unconfigured CHARIN fails closed without fabricated input");
    check(rexx_vm_status_name(status) != NULL,
          "unconfigured provider failure has a stable diagnostic");
    rexx_value_clear(&result);
    clear_args(args);
    rexx_vm_free(&vm);
}

static void test_validation_and_stream_selection(void)
{
    Rx33Provider provider;
    RexxVM vm;
    RexxValue args[3] = { REXX_VALUE_INIT, REXX_VALUE_INIT,
                          REXX_VALUE_INIT };
    RexxValue extra_args[4] = { REXX_VALUE_INIT, REXX_VALUE_INIT,
                                REXX_VALUE_INIT, REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    RexxVMStatus status;
    size_t resolve_calls;
    size_t read_calls;
    size_t cursor;

    provider_init(&provider);
    memset(&vm, 0, sizeof(vm));
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "RX33 validation VM initializes");
    if (!vm.initialized) return;
    check(rexx_vm_set_byte_stream_provider(
              &vm, &rx33_provider_callbacks, &provider) == REXX_VM_STATUS_OK,
          "validation VM installs the test provider");
    resolve_calls = provider.resolve_calls;
    read_calls = provider.read_calls;
    cursor = provider.streams[1].position;

#define CHECK_BAD_ARGUMENT(start_text, length_text, expected_status, label) \
    do { \
        check(prepare_named_call(args, "sample", start_text, length_text, \
                                 3u) == REXX_VM_STATUS_OK, \
              label " arguments prepare"); \
        status = call_charin(&vm, args, 3u, &result); \
        check(status == (expected_status) && result.type == REXX_VALUE_UNSET, \
              label " is rejected before provider reads"); \
        clear_args(args); \
    } while (0)

    CHECK_BAD_ARGUMENT("0", "1", REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                       "start zero");
    CHECK_BAD_ARGUMENT("-1", "1", REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                       "negative start");
    CHECK_BAD_ARGUMENT("1.5", "1", REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                       "fractional start");
    CHECK_BAD_ARGUMENT("not-a-number", "1",
                       REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                       "nonnumeric start");
    CHECK_BAD_ARGUMENT(NULL, "-1", REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                       "negative length");
    CHECK_BAD_ARGUMENT(NULL, "0.25", REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                       "fractional length");
    CHECK_BAD_ARGUMENT(NULL, "16777217", REXX_VM_STATUS_RESOURCE_LIMIT,
                       "length above the 16 MiB REXX string cap");
#undef CHECK_BAD_ARGUMENT

    check(prepare_named_call(args, "sample", "4294967296", "1", 3u) ==
              REXX_VM_STATUS_OK,
          "oversized start arguments prepare");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_RESOURCE_LIMIT,
          "start beyond the exact 32-bit supported position range is rejected");
    clear_args(args);

    check(text_value(&extra_args[0], "sample") == REXX_VM_STATUS_OK &&
              text_value(&extra_args[1], "1") == REXX_VM_STATUS_OK &&
              text_value(&extra_args[2], "1") == REXX_VM_STATUS_OK &&
              text_value(&extra_args[3], "extra") == REXX_VM_STATUS_OK,
          "extra-argument validation values prepare");
    check(call_charin(&vm, extra_args, 4u, &result) ==
              REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "CHARIN rejects extra positional arguments");
    rexx_value_clear(&extra_args[0]);
    rexx_value_clear(&extra_args[1]);
    rexx_value_clear(&extra_args[2]);
    rexx_value_clear(&extra_args[3]);
    check(prepare_named_call(args, "", NULL, "1", 3u) ==
              REXX_VM_STATUS_OK,
          "explicit empty stream name arguments prepare");
    check(call_charin(&vm, args, 3u, &result) ==
              REXX_VM_STATUS_BYTE_STREAM_UNKNOWN &&
              provider.last_lookup_was_default == 0,
          "explicit empty name is distinct from omitted default and unknown");
    clear_args(args);
    check(prepare_named_call(args, "missing", NULL, "1", 3u) ==
              REXX_VM_STATUS_OK,
          "unknown named stream arguments prepare");
    check(call_charin(&vm, args, 3u, &result) ==
              REXX_VM_STATUS_BYTE_STREAM_UNKNOWN,
          "unregistered names fail deterministically without path access");
    clear_args(args);
    check(provider.resolve_calls == resolve_calls + 2u &&
              provider.read_calls == read_calls &&
              provider.streams[1].position == cursor,
          "invalid positions and unknown identities do not consume stream bytes");

    provider.default_stream = -1;
    check(call_charin(&vm, NULL, 0u, &result) ==
              REXX_VM_STATUS_BYTE_STREAM_UNKNOWN,
          "provider without a default stream rejects omitted name");
    provider.default_stream = 0;

    rexx_value_clear(&result);
    rexx_vm_free(&vm);
}

static void test_nonseekable_provider_errors_and_metadata(void)
{
    Rx33Provider provider;
    RexxVM vm;
    RexxValue args[3] = { REXX_VALUE_INIT, REXX_VALUE_INIT,
                          REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    RexxVMByteStreamProvider no_seeker = rx33_provider_callbacks;
    RexxVMStatus status;
    size_t read_calls;
    size_t seek_calls;
    size_t cursor;

    provider_init(&provider);
    memset(&vm, 0, sizeof(vm));
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "RX33 provider error VM initializes");
    if (!vm.initialized) return;
    check(rexx_vm_set_byte_stream_provider(
              &vm, &rx33_provider_callbacks, &provider) == REXX_VM_STATUS_OK,
          "provider error VM installs stream callbacks");

    read_calls = provider.read_calls;
    seek_calls = provider.seek_calls;
    cursor = provider.streams[6].position;
    check(prepare_named_call(args, "noseek", "2", "1", 3u) ==
              REXX_VM_STATUS_OK,
          "nonseekable explicit-position arguments prepare");
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_BYTE_STREAM_UNSUPPORTED_OPERATION &&
              provider.read_calls == read_calls &&
              provider.seek_calls == seek_calls &&
              provider.streams[6].position == cursor,
          "nonseekable explicit start is rejected without read or cursor movement");
    clear_args(args);

    no_seeker.seek = NULL;
    check(rexx_vm_set_byte_stream_provider(&vm, &no_seeker, &provider) ==
              REXX_VM_STATUS_OK,
          "provider replacement succeeds while quiescent");
    check(prepare_named_call(args, "sample", "1", "0", 3u) ==
              REXX_VM_STATUS_OK,
          "seekable metadata without callback arguments prepare");
    check(call_charin(&vm, args, 3u, &result) ==
              REXX_VM_STATUS_BYTE_STREAM_RESULT_INVALID,
          "provider cannot advertise seeking without a seek callback");
    clear_args(args);
    check(rexx_vm_set_byte_stream_provider(
              &vm, &rx33_provider_callbacks, &provider) == REXX_VM_STATUS_OK,
          "complete provider is restored");

    provider.next_seek_unsupported = 1;
    check(prepare_named_call(args, "sample", "2", "0", 3u) ==
              REXX_VM_STATUS_OK,
          "unsupported seek callback arguments prepare");
    check(call_charin(&vm, args, 3u, &result) ==
              REXX_VM_STATUS_BYTE_STREAM_UNSUPPORTED_OPERATION &&
              provider.streams[1].position == 0u,
          "provider-declared seek rejection never reports successful repositioning");
    clear_args(args);
    provider.next_seek_error = 1;
    check(prepare_named_call(args, "sample", "2", "0", 3u) ==
              REXX_VM_STATUS_OK,
          "seek provider-error arguments prepare");
    check(call_charin(&vm, args, 3u, &result) ==
              REXX_VM_STATUS_BYTE_STREAM_PROVIDER_ERROR &&
              provider.streams[1].position == 0u,
          "failed seek leaves the provider-owned cursor unchanged");
    clear_args(args);

    provider.next_resolve_status = REXX_VM_BYTE_STREAM_RESOLVE_UNAVAILABLE;
    check(prepare_named_call(args, "sample", NULL, "1", 3u) ==
              REXX_VM_STATUS_OK,
          "unavailable resolution arguments prepare");
    check(call_charin(&vm, args, 3u, &result) ==
              REXX_VM_STATUS_BYTE_STREAM_NOT_READY,
          "temporarily unavailable resolution stays distinct");
    clear_args(args);
    provider.next_resolve_status = REXX_VM_BYTE_STREAM_RESOLVE_ERROR;
    check(prepare_named_call(args, "sample", NULL, "1", 3u) ==
              REXX_VM_STATUS_OK,
          "resolution failure arguments prepare");
    check(call_charin(&vm, args, 3u, &result) ==
              REXX_VM_STATUS_BYTE_STREAM_PROVIDER_ERROR,
          "provider resolution errors are propagated");
    clear_args(args);
    provider.next_resolve_invalid_metadata = 1;
    check(prepare_named_call(args, "sample", NULL, "1", 3u) ==
              REXX_VM_STATUS_OK,
          "invalid resolution metadata arguments prepare");
    check(call_charin(&vm, args, 3u, &result) ==
              REXX_VM_STATUS_BYTE_STREAM_RESULT_INVALID,
          "null stream identity is rejected as invalid metadata");
    clear_args(args);

    provider.next_read_fault = RX33_READ_FAULT_ERROR;
    check(prepare_named_call(args, "sample", NULL, "1", 3u) ==
              REXX_VM_STATUS_OK,
          "read-error arguments prepare");
    cursor = provider.streams[1].position;
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_BYTE_STREAM_PROVIDER_ERROR &&
              result.type == REXX_VALUE_UNSET &&
              provider.streams[1].position == cursor,
          "read failure returns no uninitialized data and preserves cursor");
    clear_args(args);

    provider.next_read_fault = RX33_READ_FAULT_UNAVAILABLE;
    check(prepare_named_call(args, "sample", NULL, "1", 3u) ==
              REXX_VM_STATUS_OK,
          "read-unavailable arguments prepare");
    check(call_charin(&vm, args, 3u, &result) ==
              REXX_VM_STATUS_BYTE_STREAM_NOT_READY,
          "temporarily unavailable read is not turned into EOF");
    clear_args(args);
    provider.next_read_fault = RX33_READ_FAULT_PENDING;
    check(prepare_named_call(args, "sample", NULL, "1", 3u) ==
              REXX_VM_STATUS_OK,
          "pending-read arguments prepare");
    check(call_charin(&vm, args, 3u, &result) ==
              REXX_VM_STATUS_BYTE_STREAM_PENDING_UNSUPPORTED,
          "pending reads fail immediately without waiting or busy looping");
    clear_args(args);
    provider.next_read_fault = RX33_READ_FAULT_UNSUPPORTED;
    check(prepare_named_call(args, "sample", NULL, "1", 3u) ==
              REXX_VM_STATUS_OK,
          "unsupported-read arguments prepare");
    check(call_charin(&vm, args, 3u, &result) ==
              REXX_VM_STATUS_BYTE_STREAM_UNSUPPORTED_OPERATION,
          "provider unsupported read operation remains distinct");
    clear_args(args);

    provider.next_read_fault = RX33_READ_FAULT_SHORT_WITHOUT_EOF;
    check(prepare_named_call(args, "sample", NULL, "2", 3u) ==
              REXX_VM_STATUS_OK,
          "short successful read arguments prepare");
    cursor = provider.streams[1].position;
    status = call_charin(&vm, args, 3u, &result);
    check(status == REXX_VM_STATUS_OK && string_is(&result, "A", 1u) &&
              provider.streams[1].position == cursor + 1u,
          "a provider may return an immediate short read without EOF");
    rexx_value_clear(&result);
    clear_args(args);

#define CHECK_INVALID_READ(fault, label) \
    do { \
        provider.next_read_fault = (fault); \
        cursor = provider.streams[1].position; \
        check(prepare_named_call(args, "sample", NULL, "2", 3u) == \
                  REXX_VM_STATUS_OK, label " arguments prepare"); \
        check(call_charin(&vm, args, 3u, &result) == \
                  REXX_VM_STATUS_BYTE_STREAM_RESULT_INVALID && \
                  result.type == REXX_VALUE_UNSET && \
                  provider.streams[1].position == cursor, \
              label " is rejected without cursor drift"); \
        clear_args(args); \
    } while (0)
    CHECK_INVALID_READ(RX33_READ_FAULT_NULL_SPAN,
                       "nonzero length with null data pointer");
    CHECK_INVALID_READ(RX33_READ_FAULT_OVERSIZED_LENGTH,
                       "provider length above requested bytes");
    CHECK_INVALID_READ(RX33_READ_FAULT_BAD_OWNERSHIP,
                       "invalid borrowed-buffer ownership");
    CHECK_INVALID_READ(RX33_READ_FAULT_BAD_EOF_FLAG,
                       "invalid EOF metadata");
    CHECK_INVALID_READ(RX33_READ_FAULT_EOF_WITH_DATA,
                       "EOF status with inconsistent byte metadata");
#undef CHECK_INVALID_READ

    rexx_value_clear(&result);
    rexx_vm_free(&vm);
}

static void test_queue_line_provider_and_vm_lifecycle(void)
{
    Rx33Provider provider;
    RexxVM vm;
    RexxValue args[3] = { REXX_VALUE_INIT, REXX_VALUE_INIT,
                          REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    RexxDataStackItem item;
    RexxDataStackStatus stack_status;
    uint32_t before_count = 0u, after_count = 0u;
    size_t line_provider_calls = 0u;
    static const unsigned char queued[] = "queued line";

    provider_init(&provider);
    memset(&vm, 0, sizeof(vm));
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "RX33 queue-isolation VM initializes");
    if (!vm.initialized) return;
    check(rexx_vm_set_byte_stream_provider(
              &vm, &rx33_provider_callbacks, &provider) == REXX_VM_STATUS_OK,
          "queue-isolation VM installs byte stream");
    check(rexx_vm_set_input_reader(&vm, line_provider_probe,
                                   &line_provider_calls) ==
              REXX_VM_STATUS_OK,
          "RX31 line provider installs independently from byte streams");
    check(rexx_data_stack_push(&vm.data_stack, queued, sizeof(queued) - 1u) ==
              REXX_DATA_STACK_OK,
          "RX30 queue receives a complete independent line");
    (void)rexx_data_stack_get_count(&vm.data_stack, &before_count);
    check(prepare_named_call(args, "sample", NULL, "2", 3u) ==
              REXX_VM_STATUS_OK,
          "queue-independence CHARIN arguments prepare");
    check(call_charin(&vm, args, 3u, &result) == REXX_VM_STATUS_OK &&
              string_is(&result, "AB", 2u),
          "CHARIN reads only its named byte stream");
    (void)rexx_data_stack_get_count(&vm.data_stack, &after_count);
    check(before_count == after_count && after_count == 1u &&
              line_provider_calls == 0u,
          "CHARIN leaves QUEUED and RX31 line-input provider untouched");
    clear_args(args);
    rexx_value_clear(&result);

    stack_status = rexx_data_stack_take_head(&vm.data_stack, &item);
    check(stack_status == REXX_DATA_STACK_OK &&
              item.length == sizeof(queued) - 1u &&
              memcmp(item.bytes, queued, item.length) == 0,
          "queued REXX line remains intact after CHARIN");
    rexx_data_stack_item_clear(&item);

    provider.mutation_vm = &vm;
    provider.mutate_on_resolve = 1;
    check(prepare_named_call(args, "sample", NULL, "1", 3u) ==
              REXX_VM_STATUS_OK,
          "provider replacement guard arguments prepare");
    check(call_charin(&vm, args, 3u, &result) == REXX_VM_STATUS_OK &&
              provider.mutation_status == REXX_VM_STATUS_INVALID_ARGUMENT,
          "provider replacement is rejected during an active callback");
    clear_args(args);
    rexx_value_clear(&result);

    provider.free_vm_on_read = 1;
    check(prepare_named_call(args, "sample", NULL, "1", 3u) ==
              REXX_VM_STATUS_OK,
          "provider teardown guard arguments prepare");
    check(call_charin(&vm, args, 3u, &result) == REXX_VM_STATUS_OK &&
              vm.initialized,
          "VM teardown from a provider callback is safely refused");
    clear_args(args);
    rexx_value_clear(&result);

    check(rexx_vm_set_byte_stream_provider(&vm, NULL, NULL) ==
              REXX_VM_STATUS_OK,
          "host clears the callback before releasing its provider context");
    ++provider.context_release_calls;
    check(provider.context_release_calls == 1u,
          "provider context release remains host-owned");
    rexx_vm_free(&vm);
    check(!vm.initialized,
          "VM teardown succeeds after provider callbacks are cleared");
}

static void test_allocation_failure_and_recovery(void)
{
    Rx33Provider provider;
    RexxVM vm;
    RexxValue args[3] = { REXX_VALUE_INIT, REXX_VALUE_INIT,
                          REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;
    size_t resolutions;
    size_t reads;

    provider_init(&provider);
    memset(&vm, 0, sizeof(vm));
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "RX33 allocation VM initializes");
    if (!vm.initialized) return;
    check(rexx_vm_set_byte_stream_provider(
              &vm, &rx33_provider_callbacks, &provider) == REXX_VM_STATUS_OK,
          "allocation VM installs memory provider");
    check(prepare_named_call(args, "sample", NULL, "1", 3u) ==
              REXX_VM_STATUS_OK,
          "allocation-failure read arguments prepare");
    resolutions = provider.resolve_calls;
    reads = provider.read_calls;
    rexx_vm_test_allocator_fail_on(1u);
    check(call_charin(&vm, args, 3u, &result) ==
              REXX_VM_STATUS_OUT_OF_MEMORY,
          "result allocation failure is explicit");
    rexx_vm_test_allocator_disable();
    check(provider.resolve_calls == resolutions &&
              provider.read_calls == reads &&
              provider.streams[1].position == 0u,
          "allocation failure occurs before resolve, seek, or byte consumption");
    check(call_charin(&vm, args, 3u, &result) == REXX_VM_STATUS_OK &&
              string_is(&result, "A", 1u),
          "successful CHARIN recovers after allocation failure");
    rexx_value_clear(&result);
    check(text_value(&args[2], "2") == REXX_VM_STATUS_OK,
          "copy-failure read length prepares");
    reads = provider.read_calls;
    rexx_vm_test_byte_stream_copy_fail_on(1u);
    check(call_charin(&vm, args, 3u, &result) ==
              REXX_VM_STATUS_OUT_OF_MEMORY &&
              result.type == REXX_VALUE_UNSET,
          "injected borrowed-byte copy failure returns no partial result");
    check(rexx_vm_test_byte_stream_copy_attempts() == 1u &&
              provider.read_calls == reads + 1u &&
              provider.streams[1].position == 3u,
          "copy failure records one provider-owned cursor advance and no double advance");
    rexx_vm_test_byte_stream_copy_disable();
    clear_args(args);
    check(prepare_named_call(args, "sample", NULL, "1", 3u) ==
              REXX_VM_STATUS_OK,
          "copy-failure recovery read arguments prepare");
    check(call_charin(&vm, args, 3u, &result) == REXX_VM_STATUS_OK &&
              string_is(&result, "D", 1u),
          "execution recovers at the byte after the provider-completed read");
    clear_args(args);
    rexx_value_clear(&result);
    rexx_vm_free(&vm);
    check(rexx_vm_test_allocator_live_blocks() == 0u,
          "RX33 VM allocations are released at teardown");
}

static uint32_t property_next(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static size_t model_stream_index(const Rx33Provider *provider, const char *name)
{
    size_t index;
    size_t length = strlen(name);
    for (index = 0u; index < provider->stream_count; ++index)
        if (provider->streams[index].name_length == length &&
            memcmp(provider->streams[index].name, name, length) == 0)
            return index;
    return SIZE_MAX;
}

static void test_fixed_seed_reference_model(void)
{
    Rx33Provider provider;
    RexxVM vm;
    uint32_t seed = RX33_PROPERTY_SEED;
    size_t model_positions[RX33_STREAM_CAPACITY] = { 0u };
    size_t index;

    provider_init(&provider);
    memset(&vm, 0, sizeof(vm));
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "RX33 fixed-seed model VM initializes");
    if (!vm.initialized) return;
    check(rexx_vm_set_byte_stream_provider(
              &vm, &rx33_provider_callbacks, &provider) == REXX_VM_STATUS_OK,
          "RX33 reference-model provider installs");

    for (index = 0u; index < RX33_PROPERTY_CASES; ++index) {
        static const char *const names[] = { "repeated", "other", "sample" };
        Rx33Provider *oracle_provider = &provider;
        RexxValue args[3] = { REXX_VALUE_INIT, REXX_VALUE_INIT,
                              REXX_VALUE_INIT };
        RexxValue result = REXX_VALUE_INIT;
        uint32_t random = property_next(&seed);
        size_t selected = (size_t)(random % 3u);
        size_t stream_index = model_stream_index(oracle_provider,
                                                 names[selected]);
        Rx33Stream *fixture = &provider.streams[stream_index];
        size_t requested = (size_t)((random >> 5) % 9u);
        int explicit_position = ((random >> 12) & 1u) != 0u;
        int inject_error = ((random >> 13) % 31u) == 0u && requested != 0u;
        size_t expected_position = model_positions[stream_index];
        size_t expected_length = 0u;
        RexxVMStatus expected_status = REXX_VM_STATUS_OK;
        RexxVMStatus status;
        unsigned char expected[16];

        if (explicit_position) {
            size_t one_based = (size_t)((random >> 16) %
                                        (fixture->length + 1u)) + 1u;
            char start_text[16];
            (void)snprintf(start_text, sizeof(start_text), "%lu",
                           (unsigned long)one_based);
            if (prepare_named_call(args, names[selected], start_text, NULL,
                                   2u) != REXX_VM_STATUS_OK) {
                check(0, "property explicit-position arguments prepare");
                break;
            }
            expected_position = one_based - 1u;
        } else if (prepare_named_call(args, names[selected], NULL, NULL, 1u) !=
                   REXX_VM_STATUS_OK) {
            check(0, "property sequential arguments prepare");
            break;
        }
        if (explicit_position || requested != 1u || inject_error) {
            char length_text[16];
            (void)snprintf(length_text, sizeof(length_text), "%lu",
                           (unsigned long)requested);
            if (text_value(&args[2], length_text) != REXX_VM_STATUS_OK) {
                check(0, "property length arguments prepare");
                clear_args(args);
                rexx_value_clear(&result);
                break;
            }
        }
        if (inject_error) provider.next_read_fault = RX33_READ_FAULT_ERROR;
        if (explicit_position) model_positions[stream_index] = expected_position;
        if (requested == 0u) {
            expected_length = 0u;
        } else if (inject_error) {
            expected_status = REXX_VM_STATUS_BYTE_STREAM_PROVIDER_ERROR;
        } else if (expected_position >= fixture->length) {
            expected_status = REXX_VM_STATUS_BYTE_STREAM_END_OF_FILE;
        } else {
            size_t available = fixture->length - expected_position;
            expected_length = requested < available ? requested : available;
            memcpy(expected, fixture->bytes + expected_position,
                   expected_length);
            model_positions[stream_index] = expected_position + expected_length;
        }
        status = call_charin(&vm, args, 3u, &result);
        ++property_cases;
        ++property_assertions;
        check(status == expected_status,
              "property operation status matches independent model");
        ++property_assertions;
        if (expected_status == REXX_VM_STATUS_OK)
            check(string_is(&result, expected, expected_length),
                  "property result bytes and length match independent model");
        else
            check(result.type == REXX_VALUE_UNSET,
                  "property failure returns no partial or uninitialized value");
        ++property_assertions;
        check(provider.streams[stream_index].position ==
                  model_positions[stream_index] &&
                  provider.last_stream_index == stream_index,
              "property cursor and resolved identity match independent model");
        if (provider.streams[0].position != model_positions[0] ||
            provider.streams[1].position != model_positions[1] ||
            provider.streams[5].position != model_positions[5]) {
            ++property_assertions;
            check(0, "all untouched streams preserve independent cursors");
        }
        clear_args(args);
        rexx_value_clear(&result);
    }
    rexx_vm_free(&vm);
}

static void test_language_routines_interpret_and_vm_isolation(void)
{
    static const char source[] =
        "say c2x(charin('sample',,1))\n"
        "interpret \"say c2x(charin('sample',,1))\"\n"
        "call nestedread\n"
        "say c2x(charin('sample',,1))\n"
        "exit 0\n"
        "nestedread:\n"
        "  say c2x(charin('sample',,1))\n"
        "  return\n";
    static const char expected[] = "41\n42\n43\n44\n";
    Rx33Provider provider;
    RexxVM vm;
    RexxBytecodeChunk *chunk = NULL;
    Rx33Capture capture;
    RexxVMStatus status;
    Rx33Provider provider_a, provider_b;
    RexxVM vm_a, vm_b;
    RexxValue arg_a[3] = { REXX_VALUE_INIT, REXX_VALUE_INIT,
                           REXX_VALUE_INIT };
    RexxValue arg_b[3] = { REXX_VALUE_INIT, REXX_VALUE_INIT,
                           REXX_VALUE_INIT };
    RexxValue result = REXX_VALUE_INIT;

    provider_init(&provider);
    memset(&vm, 0, sizeof(vm));
    memset(&capture, 0, sizeof(capture));
    check(compile_source(source, &chunk),
          "CHARIN routine and INTERPRET program compiles");
    if (chunk != NULL) {
        check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
              "routine and INTERPRET VM initializes");
        if (vm.initialized) {
            rexx_vm_set_output_writer(&vm, capture_write, &capture);
            rexx_vm_set_byte_stream_provider(
                &vm, &rx33_provider_callbacks, &provider);
            status = rexx_vm_execute(&vm, chunk);
            check(status == REXX_VM_STATUS_OK &&
                      capture.length == sizeof(expected) - 1u &&
                      memcmp(capture.bytes, expected, sizeof(expected) - 1u) == 0,
                  "stream position is shared through routine and INTERPRET execution");
            rexx_vm_free(&vm);
        }
        rexx_bytecode_chunk_free(chunk);
    }

    {
        static const char fail_closed_source[] =
            "signal on syntax name charinError\n"
            "say charin()\n"
            "say 'UNREACHABLE'\n"
            "exit 1\n"
            "charinError:\n"
            "say 'RECOVERED'\n"
            "exit 0\n";
        static const char fail_closed_expected[] = "RECOVERED\n";
        RexxVM recovery_vm;
        RexxBytecodeChunk *recovery_chunk = NULL;
        Rx33Capture recovery_capture;
        memset(&recovery_vm, 0, sizeof(recovery_vm));
        memset(&recovery_capture, 0, sizeof(recovery_capture));
        check(compile_source(fail_closed_source, &recovery_chunk),
              "unconfigured CHARIN syntax-recovery program compiles");
        if (recovery_chunk != NULL) {
            check(rexx_vm_init(&recovery_vm, 0u) == REXX_VM_STATUS_OK,
                  "unconfigured CHARIN recovery VM initializes");
            if (recovery_vm.initialized) {
                rexx_vm_set_output_writer(&recovery_vm, capture_write,
                                          &recovery_capture);
                status = rexx_vm_execute(&recovery_vm, recovery_chunk);
                check(status == REXX_VM_STATUS_OK &&
                          recovery_capture.length ==
                              sizeof(fail_closed_expected) - 1u &&
                          memcmp(recovery_capture.bytes,
                                 fail_closed_expected,
                                 sizeof(fail_closed_expected) - 1u) == 0,
                      "unconfigured CHARIN fails closed through SYNTAX and recovers");
                rexx_vm_free(&recovery_vm);
            }
            rexx_bytecode_chunk_free(recovery_chunk);
        }
    }

    provider_init(&provider_a);
    provider_init(&provider_b);
    provider_a.streams[1].bytes = (const unsigned char *)"AA";
    provider_a.streams[1].length = 2u;
    provider_b.streams[1].bytes = (const unsigned char *)"BB";
    provider_b.streams[1].length = 2u;
    memset(&vm_a, 0, sizeof(vm_a));
    memset(&vm_b, 0, sizeof(vm_b));
    check(rexx_vm_init(&vm_a, 0u) == REXX_VM_STATUS_OK &&
              rexx_vm_init(&vm_b, 0u) == REXX_VM_STATUS_OK,
          "independent byte-stream VMs initialize");
    if (vm_a.initialized && vm_b.initialized) {
        rexx_vm_set_byte_stream_provider(&vm_a, &rx33_provider_callbacks,
                                         &provider_a);
        rexx_vm_set_byte_stream_provider(&vm_b, &rx33_provider_callbacks,
                                         &provider_b);
        prepare_named_call(arg_a, "sample", NULL, "1", 3u);
        prepare_named_call(arg_b, "sample", NULL, "1", 3u);
        check(call_charin(&vm_a, arg_a, 3u, &result) == REXX_VM_STATUS_OK &&
                  string_is(&result, "A", 1u),
              "VM A reads only its installed provider context");
        rexx_value_clear(&result);
        check(call_charin(&vm_b, arg_b, 3u, &result) == REXX_VM_STATUS_OK &&
                  string_is(&result, "B", 1u),
              "VM B maintains an independent cursor and context");
        rexx_value_clear(&result);
        check(provider_a.streams[1].position == 1u &&
                  provider_b.streams[1].position == 1u,
              "provider replacement and names do not share cross-VM position");
    }
    clear_args(arg_a);
    clear_args(arg_b);
    rexx_value_clear(&result);
    rexx_vm_free(&vm_a);
    rexx_vm_free(&vm_b);
}

static void test_vm_creation_and_teardown_cycles(void)
{
    size_t cycle;
    for (cycle = 0u; cycle < 32u; ++cycle) {
        Rx33Provider provider;
        RexxVM vm;
        RexxValue result = REXX_VALUE_INIT;
        size_t index;
        provider_init(&provider);
        memset(&vm, 0, sizeof(vm));
        check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
              "lifecycle property VM initializes");
        if (!vm.initialized) continue;
        check(rexx_vm_set_byte_stream_provider(
                  &vm, &rx33_provider_callbacks, &provider) ==
                  REXX_VM_STATUS_OK,
              "lifecycle provider installs");
        for (index = 0u; index < 4u; ++index) {
            RexxValue args[3] = { REXX_VALUE_INIT, REXX_VALUE_INIT,
                                  REXX_VALUE_INIT };
            prepare_named_call(args, "sample", NULL, "1", 3u);
            check(call_charin(&vm, args, 3u, &result) == REXX_VM_STATUS_OK,
                  "lifecycle provider reads before teardown");
            clear_args(args);
            rexx_value_clear(&result);
        }
        check(rexx_vm_set_byte_stream_provider(&vm, NULL, NULL) ==
                  REXX_VM_STATUS_OK,
              "lifecycle provider clears before context release");
        rexx_vm_free(&vm);
        check(!vm.initialized,
              "lifecycle teardown leaves no installed provider");
    }
}

#if defined(REXX_RX34_TEST)
static RexxVMStatus rx34_call_named(RexxVM *vm, const char *name,
                                    const char *start, const char *length,
                                    RexxValue *result)
{
    RexxValue args[3] = { REXX_VALUE_INIT, REXX_VALUE_INIT,
                          REXX_VALUE_INIT };
    RexxVMStatus status = prepare_named_call(args, name, start, length, 3u);
    if (status == REXX_VM_STATUS_OK)
        status = call_charin(vm, args, 3u, result);
    clear_args(args);
    return status;
}

static int rx34_query(RexxVM *vm, const char *identity,
                      RexxVMByteStreamConditionStatus *status)
{
    return rexx_vm_query_byte_stream_status(
               vm, (const unsigned char *)identity, strlen(identity), status) ==
           REXX_VM_STATUS_OK;
}

static void rx34_check_status(RexxVM *vm, const char *identity,
                              RexxVMByteStreamConditionState state,
                              RexxVMByteStreamErrorClass error_class,
                              RexxVMByteStreamOperation operation,
                              RexxVMStatus last_status,
                              size_t requested, size_t returned,
                              int eof_after_read, const char *description)
{
    RexxVMByteStreamConditionStatus snapshot;
    int queried = rx34_query(vm, identity, &snapshot);
    check(queried && snapshot.valid && snapshot.state == state &&
              snapshot.error_class == error_class &&
              snapshot.last_operation == operation &&
              snapshot.last_status == last_status &&
              snapshot.requested_length == requested &&
              snapshot.returned_length == returned &&
              snapshot.eof_after_last_read == (uint8_t)(eof_after_read != 0) &&
              snapshot.identity_length == strlen(identity) &&
              memcmp(snapshot.identity, identity, strlen(identity)) == 0,
          description);
}

static void test_rx34_status_transitions_and_observation(void)
{
    Rx33Provider provider, replacement;
    RexxVM vm;
    RexxValue result = REXX_VALUE_INIT;
    RexxVMByteStreamConditionStatus snapshot, before_query;
    size_t reads, seeks, resolves;
    uint64_t original_generation;

    provider_init(&provider);
    replacement = provider;
    memset(&vm, 0, sizeof(vm));
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "RX34 status VM initializes");
    if (!vm.initialized) return;
    check(rexx_vm_set_byte_stream_provider(
              &vm, &rx33_provider_callbacks, &provider) == REXX_VM_STATUS_OK,
          "RX34 status provider installs");

    reads = provider.read_calls;
    seeks = provider.seek_calls;
    resolves = provider.resolve_calls;
    check(rx34_query(&vm, "sample", &snapshot) && !snapshot.valid &&
              snapshot.state == REXX_VM_BYTE_STREAM_CONDITION_UNKNOWN &&
              provider.resolve_calls == resolves &&
              provider.read_calls == reads && provider.seek_calls == seeks,
          "unknown status query is deterministic and performs no provider call");

    check(rx34_call_named(&vm, "sample", NULL, "0", &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, NULL, 0u),
          "zero-length read resolves without fabricating input");
    rx34_check_status(&vm, "sample", REXX_VM_BYTE_STREAM_CONDITION_RESOLVED,
                      REXX_VM_BYTE_STREAM_ERROR_NONE,
                      REXX_VM_BYTE_STREAM_OPERATION_ZERO_LENGTH_READ,
                      REXX_VM_STATUS_OK, 0u, 0u, 0,
                      "zero-length read remains resolved rather than ready");
    check(provider.streams[1].position == 0u && provider.read_calls == reads &&
              provider.seek_calls == seeks,
          "zero-length read without start does not move the provider cursor");

    check(rx34_call_named(&vm, "sample", "1", "3", &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, "ABC", 3u),
          "full read returns the requested bytes");
    original_generation = 0u;
    check(rx34_query(&vm, "sample", &snapshot) && snapshot.valid &&
              snapshot.state == REXX_VM_BYTE_STREAM_CONDITION_READY &&
              snapshot.error_class == REXX_VM_BYTE_STREAM_ERROR_NONE &&
              snapshot.last_operation == REXX_VM_BYTE_STREAM_OPERATION_READ &&
              snapshot.requested_length == 3u &&
              snapshot.returned_length == 3u &&
              snapshot.last_status == REXX_VM_STATUS_OK &&
              provider.streams[1].position == 3u,
          "first full read publishes ready state and exact cursor advance");
    original_generation = snapshot.provider_generation;
    check(rx34_query(&vm, "sample", &before_query) &&
              before_query.state == REXX_VM_BYTE_STREAM_CONDITION_READY,
          "recovered stream status is captured before reading another stream");
    reads = provider.read_calls;
    check(rx34_query(&vm, "sample", &snapshot) &&
              memcmp(&snapshot, &before_query, sizeof(snapshot)) == 0 &&
              provider.read_calls == reads &&
              provider.streams[1].position == 3u,
          "repeated status observation changes neither state nor cursor");

    check(rx34_call_named(&vm, "sample", NULL, "2", &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, "DE", 2u),
          "repeated successful read advances sequentially");
    rx34_check_status(&vm, "sample", REXX_VM_BYTE_STREAM_CONDITION_READY,
                      REXX_VM_BYTE_STREAM_ERROR_NONE,
                      REXX_VM_BYTE_STREAM_OPERATION_READ,
                      REXX_VM_STATUS_OK, 2u, 2u, 0,
                      "repeated full read records its own exact result");

    check(rx34_call_named(&vm, "sample", NULL, "5", &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, "F", 1u) &&
              provider.streams[1].position == 6u,
          "short read retains its useful byte and actual cursor advance");
    rx34_check_status(&vm, "sample", REXX_VM_BYTE_STREAM_CONDITION_ERROR,
                      REXX_VM_BYTE_STREAM_ERROR_SHORT_READ,
                      REXX_VM_BYTE_STREAM_OPERATION_READ,
                      REXX_VM_STATUS_OK, 5u, 1u, 1,
                      "short read is an observable stream error with returned bytes");
    rexx_value_clear(&result);

    check(rx34_call_named(&vm, "sample", NULL, "1", &result) ==
                  REXX_VM_STATUS_BYTE_STREAM_END_OF_FILE &&
              result.type == REXX_VALUE_UNSET &&
              provider.streams[1].position == 6u,
          "read after end returns EOF without changing the cursor");
    rx34_check_status(&vm, "sample", REXX_VM_BYTE_STREAM_CONDITION_ERROR,
                      REXX_VM_BYTE_STREAM_ERROR_END_OF_STREAM,
                      REXX_VM_BYTE_STREAM_OPERATION_READ,
                      REXX_VM_STATUS_BYTE_STREAM_END_OF_FILE, 1u, 0u, 1,
                      "EOF before data is distinguished from a successful empty read");

    check(rx34_call_named(&vm, "sample", "1", "0", &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, NULL, 0u) &&
              provider.streams[1].position == 0u,
          "explicit zero-length call repositions to the first byte");
    rx34_check_status(&vm, "sample", REXX_VM_BYTE_STREAM_CONDITION_READY,
                      REXX_VM_BYTE_STREAM_ERROR_NONE,
                      REXX_VM_BYTE_STREAM_OPERATION_SEEK,
                      REXX_VM_STATUS_OK, 0u, 0u, 0,
                      "successful seek is observable and is the documented error recovery event");
    check(rx34_call_named(&vm, "sample", NULL, "2", &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, "AB", 2u) &&
              provider.streams[1].position == 2u,
          "read recovers after EOF and follows the successful reposition");
    rx34_check_status(&vm, "sample", REXX_VM_BYTE_STREAM_CONDITION_READY,
                      REXX_VM_BYTE_STREAM_ERROR_NONE,
                      REXX_VM_BYTE_STREAM_OPERATION_READ,
                      REXX_VM_STATUS_OK, 2u, 2u, 0,
                      "successful recovery read records the current ready state");

    check(rx34_query(&vm, "sample", &before_query) &&
              before_query.state == REXX_VM_BYTE_STREAM_CONDITION_READY &&
              before_query.requested_length == 2u &&
              before_query.returned_length == 2u,
          "recovered stream status is captured before reading another stream");
    check(rx34_call_named(&vm, "other", NULL, "1", &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, "W", 1u),
          "second stream reads independently");
    {
        RexxVMByteStreamConditionStatus other_snapshot;
        int sample_unchanged = rx34_query(&vm, "sample", &snapshot) &&
            snapshot.valid == before_query.valid &&
            snapshot.state == before_query.state &&
            snapshot.last_operation == before_query.last_operation &&
            snapshot.error_class == before_query.error_class &&
            snapshot.last_status == before_query.last_status &&
            snapshot.diagnostic_code == before_query.diagnostic_code &&
            snapshot.requested_length == before_query.requested_length &&
            snapshot.returned_length == before_query.returned_length &&
            snapshot.eof_after_last_read == before_query.eof_after_last_read &&
            snapshot.provider_generation == before_query.provider_generation &&
            snapshot.identity_generation == before_query.identity_generation &&
            snapshot.identity_length == before_query.identity_length &&
            memcmp(snapshot.identity, before_query.identity,
                   snapshot.identity_length) == 0;
        check(sample_unchanged,
              "reading another stream leaves the first stream status unchanged");
        check(rx34_query(&vm, "other", &other_snapshot) &&
                  other_snapshot.valid &&
                  other_snapshot.state == REXX_VM_BYTE_STREAM_CONDITION_READY,
              "other stream receives an independent status entry");
        check(provider.streams[1].position == 2u &&
                  provider.streams[5].position == 1u,
              "each stream retains its own provider cursor");
    }

    check(rexx_vm_set_byte_stream_provider(
              &vm, &rx33_provider_callbacks, &replacement) ==
                  REXX_VM_STATUS_OK &&
              rx34_query(&vm, "sample", &snapshot) && !snapshot.valid &&
              snapshot.state == REXX_VM_BYTE_STREAM_CONDITION_UNKNOWN,
          "provider replacement clears old generation condition records");
    check(rx34_call_named(&vm, "sample", NULL, "1", &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, "A", 1u) &&
              rx34_query(&vm, "sample", &snapshot) && snapshot.valid &&
              snapshot.provider_generation != original_generation &&
              snapshot.state == REXX_VM_BYTE_STREAM_CONDITION_READY,
          "replacement stream status is attributed only to its new generation");

    rexx_value_clear(&result);
    rexx_vm_free(&vm);
    check(!vm.initialized && vm.byte_stream_status_count == 0u,
          "VM teardown releases all inline status records");
}

static void test_rx34_callback_errors_and_argument_boundaries(void)
{
    Rx33Provider provider;
    RexxVM vm, no_provider_vm;
    RexxValue result = REXX_VALUE_INIT;
    RexxVMByteStreamConditionStatus snapshot;
    size_t cursor, resolve_calls, reads;
    unsigned char oversized[REXX_VM_BYTE_STREAM_IDENTITY_MAX_BYTES + 1u];

    provider_init(&provider);
    memset(&vm, 0, sizeof(vm));
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK &&
              rexx_vm_set_byte_stream_provider(
                  &vm, &rx33_provider_callbacks, &provider) ==
                  REXX_VM_STATUS_OK,
          "RX34 callback-error VM and provider initialize");
    if (!vm.initialized) return;

    cursor = provider.streams[1].position;
    provider.next_seek_error = 1;
    check(rx34_call_named(&vm, "sample", "2", "0", &result) ==
                  REXX_VM_STATUS_BYTE_STREAM_PROVIDER_ERROR &&
              provider.streams[1].position == cursor,
          "failed seek preserves provider cursor and reports failure");
    rx34_check_status(&vm, "sample", REXX_VM_BYTE_STREAM_CONDITION_ERROR,
                      REXX_VM_BYTE_STREAM_ERROR_PROVIDER,
                      REXX_VM_BYTE_STREAM_OPERATION_SEEK,
                      REXX_VM_STATUS_BYTE_STREAM_PROVIDER_ERROR, 0u, 0u, 0,
                      "failed seek is retained as provider error");

    provider.next_seek_unsupported = 1;
    check(rx34_call_named(&vm, "sample", "2", "0", &result) ==
                  REXX_VM_STATUS_BYTE_STREAM_UNSUPPORTED_OPERATION &&
              provider.streams[1].position == cursor,
          "unsupported seek has no cursor effect");
    rx34_check_status(&vm, "sample", REXX_VM_BYTE_STREAM_CONDITION_ERROR,
                      REXX_VM_BYTE_STREAM_ERROR_UNSUPPORTED_OPERATION,
                      REXX_VM_BYTE_STREAM_OPERATION_SEEK,
                      REXX_VM_STATUS_BYTE_STREAM_UNSUPPORTED_OPERATION,
                      0u, 0u, 0,
                      "unsupported seek capability is recorded on its stream");

    provider.next_read_fault = RX33_READ_FAULT_UNAVAILABLE;
    check(rx34_call_named(&vm, "sample", NULL, "1", &result) ==
                  REXX_VM_STATUS_BYTE_STREAM_NOT_READY,
          "unavailable provider input remains distinct from EOF");
    rx34_check_status(&vm, "sample", REXX_VM_BYTE_STREAM_CONDITION_ERROR,
                      REXX_VM_BYTE_STREAM_ERROR_NOT_READY,
                      REXX_VM_BYTE_STREAM_OPERATION_READ,
                      REXX_VM_STATUS_BYTE_STREAM_NOT_READY, 1u, 0u, 0,
                      "NOTREADY callback status is retained without raising NOTREADY condition");

    provider.next_read_fault = RX33_READ_FAULT_PENDING;
    check(rx34_call_named(&vm, "sample", NULL, "1", &result) ==
                  REXX_VM_STATUS_BYTE_STREAM_PENDING_UNSUPPORTED,
          "pending input is rejected immediately");
    rx34_check_status(&vm, "sample", REXX_VM_BYTE_STREAM_CONDITION_ERROR,
                      REXX_VM_BYTE_STREAM_ERROR_PENDING_UNSUPPORTED,
                      REXX_VM_BYTE_STREAM_OPERATION_READ,
                      REXX_VM_STATUS_BYTE_STREAM_PENDING_UNSUPPORTED,
                      1u, 0u, 0,
                      "pending callback maps to its bounded error class");

    provider.next_read_fault = RX33_READ_FAULT_UNSUPPORTED;
    check(rx34_call_named(&vm, "sample", NULL, "1", &result) ==
                  REXX_VM_STATUS_BYTE_STREAM_UNSUPPORTED_OPERATION,
          "unsupported read capability remains a distinct immediate failure");
    rx34_check_status(&vm, "sample", REXX_VM_BYTE_STREAM_CONDITION_ERROR,
                      REXX_VM_BYTE_STREAM_ERROR_UNSUPPORTED_OPERATION,
                      REXX_VM_BYTE_STREAM_OPERATION_READ,
                      REXX_VM_STATUS_BYTE_STREAM_UNSUPPORTED_OPERATION,
                      1u, 0u, 0,
                      "unsupported read is queryable on the resolved stream");

    provider.next_read_fault = RX33_READ_FAULT_ERROR;
    check(rx34_call_named(&vm, "sample", NULL, "1", &result) ==
                  REXX_VM_STATUS_BYTE_STREAM_PROVIDER_ERROR,
          "provider read error is not confused with EOF or NOTREADY");
    rx34_check_status(&vm, "sample", REXX_VM_BYTE_STREAM_CONDITION_ERROR,
                      REXX_VM_BYTE_STREAM_ERROR_PROVIDER,
                      REXX_VM_BYTE_STREAM_OPERATION_READ,
                      REXX_VM_STATUS_BYTE_STREAM_PROVIDER_ERROR, 1u, 0u, 0,
                      "provider callback failure is retained with exact VM status");

    provider.next_resolve_invalid_metadata = 1;
    check(rx34_call_named(&vm, "sample", NULL, "1", &result) ==
                  REXX_VM_STATUS_BYTE_STREAM_RESULT_INVALID,
          "invalid callback metadata remains a CHARIN failure");
    rx34_check_status(&vm, "badmeta", REXX_VM_BYTE_STREAM_CONDITION_ERROR,
                      REXX_VM_BYTE_STREAM_ERROR_INVALID_METADATA,
                      REXX_VM_BYTE_STREAM_OPERATION_RESOLVE,
                      REXX_VM_STATUS_BYTE_STREAM_RESULT_INVALID, 1u, 0u, 0,
                      "valid stable key retains invalid callback metadata without a borrowed handle");

    provider.next_resolve_oversized_identity = 1;
    reads = provider.read_calls;
    check(rx34_call_named(&vm, "sample", NULL, "1", &result) ==
                  REXX_VM_STATUS_RESOURCE_LIMIT &&
              provider.read_calls == reads,
          "overlong provider identity fails before seek or read");
    check(rexx_vm_query_byte_stream_status(
              &vm, oversized, sizeof(oversized), &snapshot) ==
                  REXX_VM_STATUS_RESOURCE_LIMIT,
          "status query rejects identity larger than its fixed output bound");
    check(rexx_vm_query_byte_stream_status(&vm, NULL, 1u, &snapshot) ==
              REXX_VM_STATUS_INVALID_ARGUMENT,
          "status query rejects an absent nonempty identity span");

    resolve_calls = provider.resolve_calls;
    {
        RexxValue too_many[4] = { REXX_VALUE_INIT, REXX_VALUE_INIT,
                                  REXX_VALUE_INIT, REXX_VALUE_INIT };
        check(call_charin(&vm, too_many, 4u, &result) ==
                  REXX_VM_STATUS_INVALID_FUNCTION_CALL &&
                  provider.resolve_calls == resolve_calls,
              "invalid function arguments do not fabricate a stream operation");
        check(rx34_query(&vm, "never-resolved", &snapshot) &&
                  !snapshot.valid,
              "invalid arguments leave the unobserved stream unknown");
    }

    memset(&no_provider_vm, 0, sizeof(no_provider_vm));
    check(rexx_vm_init(&no_provider_vm, 0u) == REXX_VM_STATUS_OK,
          "RX34 no-provider VM initializes");
    if (no_provider_vm.initialized) {
        check(rexx_vm_charin(&no_provider_vm, 0, (const unsigned char *)"sample",
                             6u, 0, 0u, 1u, &result) ==
                  REXX_VM_STATUS_BYTE_STREAM_UNAVAILABLE &&
                  rx34_query(&no_provider_vm, "sample", &snapshot) &&
                  !snapshot.valid,
              "provider-unavailable failure stays fail-closed and query remains unknown");
        rexx_vm_free(&no_provider_vm);
    }
    rexx_value_clear(&result);
    rexx_vm_free(&vm);
}

static void test_rx34_capacity_and_vm_isolation(void)
{
    Rx33Provider provider, provider_b;
    RexxVM vm, vm_b;
    RexxValue result = REXX_VALUE_INIT;
    RexxVMByteStreamConditionStatus snapshot;
    char names[REXX_VM_BYTE_STREAM_STATUS_CAPACITY + 1u][16];
    size_t index, reads_before;

    provider_init(&provider);
    for (index = 0u; index < REXX_VM_BYTE_STREAM_STATUS_CAPACITY + 1u; ++index) {
        (void)snprintf(names[index], sizeof(names[index]), "slot%02lu",
                       (unsigned long)index);
        add_stream(&provider, names[index], (const unsigned char *)"Z", 1u, 1);
    }
    memset(&vm, 0, sizeof(vm));
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK &&
              rexx_vm_set_byte_stream_provider(
                  &vm, &rx33_provider_callbacks, &provider) ==
                  REXX_VM_STATUS_OK,
          "RX34 bounded-capacity VM initializes");
    if (!vm.initialized) return;

    check(rx34_call_named(&vm, names[0], NULL, "2", &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, "Z", 1u),
          "capacity test retains a live short-read error in its first slot");
    for (index = 1u; index < REXX_VM_BYTE_STREAM_STATUS_CAPACITY; ++index) {
        check(rx34_call_named(&vm, names[index], NULL, "1", &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, "Z", 1u),
              "capacity test fills the next bounded status slot");
        rexx_value_clear(&result);
    }
    check(vm.byte_stream_status_count == REXX_VM_BYTE_STREAM_STATUS_CAPACITY,
          "tracked stream capacity is exactly the documented fixed bound");
    reads_before = provider.read_calls;
    check(rx34_call_named(&vm, names[REXX_VM_BYTE_STREAM_STATUS_CAPACITY],
                          NULL, "1", &result) ==
                  REXX_VM_STATUS_RESOURCE_LIMIT &&
              provider.read_calls == reads_before &&
              provider.streams[provider.stream_count - 1u].position == 0u,
          "capacity exhaustion rejects new data consumption deterministically");
    rx34_check_status(&vm, names[0], REXX_VM_BYTE_STREAM_CONDITION_ERROR,
                      REXX_VM_BYTE_STREAM_ERROR_SHORT_READ,
                      REXX_VM_BYTE_STREAM_OPERATION_READ,
                      REXX_VM_STATUS_OK, 2u, 1u, 1,
                      "capacity exhaustion never evicts an unresolved error");
    check(rx34_query(&vm, names[REXX_VM_BYTE_STREAM_STATUS_CAPACITY],
                     &snapshot) && !snapshot.valid,
          "denied stream remains unknown because its operation never began");

    provider_init(&provider_b);
    memset(&vm_b, 0, sizeof(vm_b));
    check(rexx_vm_init(&vm_b, 0u) == REXX_VM_STATUS_OK &&
              rexx_vm_set_byte_stream_provider(
                  &vm_b, &rx33_provider_callbacks, &provider_b) ==
                  REXX_VM_STATUS_OK,
          "second RX34 VM initializes with an independent provider");
    if (vm_b.initialized) {
        check(rx34_query(&vm_b, names[0], &snapshot) && !snapshot.valid,
              "one VM cannot observe another VM's stream-state table");
        check(rx34_call_named(&vm_b, "sample", NULL, "2", &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, "AB", 2u),
              "second VM reads its own stream cursor");
        check(rx34_query(&vm, "sample", &snapshot) && !snapshot.valid,
              "second VM operation does not create state in the first VM");
        rexx_vm_free(&vm_b);
    }
    rexx_value_clear(&result);
    rexx_vm_free(&vm);
}

typedef struct Rx34ReferenceStatus {
    int valid;
    RexxVMByteStreamConditionState state;
    RexxVMByteStreamOperation operation;
    RexxVMByteStreamErrorClass error;
    RexxVMStatus last_status;
    uint32_t diagnostic;
    size_t requested;
    size_t returned;
    int eof_after_read;
    uint64_t generation;
    size_t position;
} Rx34ReferenceStatus;

static void rx34_reference_resolve(Rx34ReferenceStatus *model,
                                   uint64_t generation)
{
    size_t position;
    if (model->valid) return;
    position = model->position;
    memset(model, 0, sizeof(*model));
    model->position = position;
    model->valid = 1;
    model->state = REXX_VM_BYTE_STREAM_CONDITION_RESOLVED;
    model->operation = REXX_VM_BYTE_STREAM_OPERATION_RESOLVE;
    model->last_status = REXX_VM_STATUS_OK;
    model->generation = generation;
}

static void rx34_reference_success(Rx34ReferenceStatus *model,
                                   RexxVMByteStreamOperation operation,
                                   size_t requested, size_t returned,
                                   int eof_after_read)
{
    model->operation = operation;
    model->last_status = REXX_VM_STATUS_OK;
    model->requested = requested;
    model->returned = returned;
    model->eof_after_read = eof_after_read;
    model->state = REXX_VM_BYTE_STREAM_CONDITION_READY;
    model->error = REXX_VM_BYTE_STREAM_ERROR_NONE;
    model->diagnostic = 0u;
}

static void rx34_reference_zero_without_seek(Rx34ReferenceStatus *model)
{
    model->operation = REXX_VM_BYTE_STREAM_OPERATION_ZERO_LENGTH_READ;
    model->last_status = REXX_VM_STATUS_OK;
    model->requested = 0u;
    model->returned = 0u;
    model->eof_after_read = 0;
}

static void rx34_reference_error(Rx34ReferenceStatus *model,
                                 RexxVMByteStreamOperation operation,
                                 RexxVMByteStreamErrorClass error,
                                 RexxVMStatus status, size_t requested,
                                 size_t returned, int eof_after_read)
{
    model->state = REXX_VM_BYTE_STREAM_CONDITION_ERROR;
    model->operation = operation;
    model->error = error;
    model->last_status = status;
    model->diagnostic = error == REXX_VM_BYTE_STREAM_ERROR_SHORT_READ
                            ? (uint32_t)error : (uint32_t)status;
    model->requested = requested;
    model->returned = returned;
    model->eof_after_read = eof_after_read;
}

static void rx34_check_reference(RexxVM *vm,
                                 const Rx34ReferenceStatus *model,
                                 size_t operation_index)
{
    static const unsigned char identity[] = "sample";
    RexxVMByteStreamConditionStatus actual;
    int query_ok = rexx_vm_query_byte_stream_status(
        vm, identity, sizeof(identity) - 1u, &actual) == REXX_VM_STATUS_OK;
    ++property_assertions;
    check(query_ok && actual.valid == (uint8_t)model->valid &&
              actual.state == model->state &&
              actual.last_operation == model->operation &&
              actual.error_class == model->error &&
              actual.last_status == model->last_status &&
              actual.diagnostic_code == model->diagnostic &&
              actual.requested_length == model->requested &&
              actual.returned_length == model->returned &&
              actual.eof_after_last_read == (uint8_t)model->eof_after_read &&
              actual.provider_generation == model->generation &&
              actual.identity_length == (model->valid
                                              ? sizeof(identity) - 1u : 0u) &&
              (!model->valid ||
               memcmp(actual.identity, identity, sizeof(identity) - 1u) == 0),
          "RX34 model status, identity, generation, and error reset match");
    ++property_assertions;
    check(vm->byte_stream_status_count == (size_t)model->valid,
          "RX34 model status table occupancy remains bounded and exact");
    ++property_assertions;
    check(vm->byte_stream_statuses[0].valid == (uint8_t)model->valid &&
              model->position == ((Rx33Provider *)
                  vm->byte_stream_provider_user_data)->streams[1].position,
          "RX34 model cursor equals provider-owned position");
    (void)operation_index;
}

static void test_rx34_deterministic_status_model(void)
{
    Rx33Provider provider;
    RexxVM vm;
    RexxValue result = REXX_VALUE_INIT;
    Rx34ReferenceStatus model;
    uint32_t seed = RX34_PROPERTY_SEED;
    size_t index;

    provider_init(&provider);
    memset(&vm, 0, sizeof(vm));
    memset(&model, 0, sizeof(model));
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK &&
              rexx_vm_set_byte_stream_provider(
                  &vm, &rx33_provider_callbacks, &provider) ==
                  REXX_VM_STATUS_OK,
          "RX34 deterministic model VM and provider initialize");
    if (!vm.initialized) return;

    for (index = 0u; index < RX34_PROPERTY_CASES; ++index) {
        uint32_t random = property_next(&seed);
        size_t action = index < 10u ? index : (size_t)(random % 10u);
        size_t requested = 1u + (size_t)((random >> 8) % 7u);
        size_t before_position = provider.streams[1].position;
        size_t expected_length = 0u;
        unsigned char expected[8] = { 0u };
        RexxVMStatus operation_status = REXX_VM_STATUS_OK;
        int has_result = 0;


        if (action == 6u) {
            RexxVMByteStreamConditionStatus snapshot;
            size_t calls = provider.resolve_calls + provider.read_calls +
                           provider.seek_calls;
            rx34_check_reference(&vm, &model, index);
            ++property_assertions;
            check(rexx_vm_query_byte_stream_status(
                      &vm, (const unsigned char *)"sample", 6u, &snapshot) ==
                          REXX_VM_STATUS_OK &&
                      provider.resolve_calls + provider.read_calls +
                              provider.seek_calls == calls &&
                      provider.streams[1].position == before_position,
                  "RX34 model query has no provider or cursor side effect");
        } else if (action == 7u) {
            size_t cursor_position = model.position;
            check(rexx_vm_set_byte_stream_provider(
                      &vm, &rx33_provider_callbacks, &provider) ==
                      REXX_VM_STATUS_OK,
                  "RX34 model provider replacement is accepted while quiescent");
            memset(&model, 0, sizeof(model));
            model.position = cursor_position;
            rx34_check_reference(&vm, &model, index);
        } else if (action == 8u) {
            size_t cursor_position = model.position;
            rexx_vm_free(&vm);
            check(!vm.initialized,
                  "RX34 model teardown invalidates all prior state");
            check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK &&
                      rexx_vm_set_byte_stream_provider(
                          &vm, &rx33_provider_callbacks, &provider) ==
                          REXX_VM_STATUS_OK,
                  "RX34 model VM reinitializes with the same host provider");
            memset(&model, 0, sizeof(model));
            model.position = cursor_position;
            rx34_check_reference(&vm, &model, index);
        } else {
            char start_text[16];
            char length_text[16];
            const char *start = NULL;
            size_t requested_for_call = requested;
            int inject_failure = action == 5u;
            rx34_reference_resolve(&model, vm.byte_stream_provider_generation);
            if (action == 1u || action == 4u || action == 9u) {
                size_t position = action == 1u ? 1u :
                                  action == 4u ? 7u : 1u;
                (void)snprintf(start_text, sizeof(start_text), "%lu",
                               (unsigned long)position);
                start = start_text;
                if (action == 1u) requested_for_call = 7u;
                else if (action == 4u) requested_for_call = 1u;
                else requested_for_call = 1u;
                model.position = position - 1u;
                /* The seek occurs before CHARIN reads or returns an empty
                 * string, and its state is visible unless a later read does. */
                rx34_reference_success(
                    &model, REXX_VM_BYTE_STREAM_OPERATION_SEEK,
                    requested_for_call, 0u, 0);
            } else if (action == 2u) {
                size_t position = (size_t)((random >> 16) % 7u) + 1u;
                (void)snprintf(start_text, sizeof(start_text), "%lu",
                               (unsigned long)position);
                start = start_text;
                requested_for_call = 0u;
                model.position = position - 1u;
                rx34_reference_success(&model,
                                       REXX_VM_BYTE_STREAM_OPERATION_SEEK,
                                       0u, 0u, 0);
            } else if (action == 3u) {
                requested_for_call = 0u;
            }
            (void)snprintf(length_text, sizeof(length_text), "%lu",
                           (unsigned long)requested_for_call);
            if (inject_failure) provider.next_read_fault = RX33_READ_FAULT_ERROR;
            if (action == 4u) {
                operation_status = REXX_VM_STATUS_BYTE_STREAM_END_OF_FILE;
                rx34_reference_error(
                    &model, REXX_VM_BYTE_STREAM_OPERATION_READ,
                    REXX_VM_BYTE_STREAM_ERROR_END_OF_STREAM, operation_status,
                    1u, 0u, 1);
            } else if (inject_failure) {
                operation_status = REXX_VM_STATUS_BYTE_STREAM_PROVIDER_ERROR;
                rx34_reference_error(
                    &model, REXX_VM_BYTE_STREAM_OPERATION_READ,
                    REXX_VM_BYTE_STREAM_ERROR_PROVIDER, operation_status,
                    requested_for_call, 0u, 0);
            } else if (requested_for_call == 0u) {
                if (start != NULL)
                    rx34_reference_success(
                        &model, REXX_VM_BYTE_STREAM_OPERATION_SEEK,
                        0u, 0u, 0);
                else
                    rx34_reference_zero_without_seek(&model);
            } else if (model.position >= sizeof(rx33_sample_bytes) - 1u) {
                operation_status = REXX_VM_STATUS_BYTE_STREAM_END_OF_FILE;
                rx34_reference_error(
                    &model, REXX_VM_BYTE_STREAM_OPERATION_READ,
                    REXX_VM_BYTE_STREAM_ERROR_END_OF_STREAM, operation_status,
                    requested_for_call, 0u, 1);
            } else {
                size_t available = sizeof(rx33_sample_bytes) - 1u -
                                   model.position;
                expected_length = requested_for_call < available
                                      ? requested_for_call : available;
                memcpy(expected, rx33_sample_bytes + model.position,
                       expected_length);
                model.position += expected_length;
                has_result = 1;
                if (expected_length < requested_for_call) {
                    rx34_reference_error(
                        &model, REXX_VM_BYTE_STREAM_OPERATION_READ,
                        REXX_VM_BYTE_STREAM_ERROR_SHORT_READ,
                        REXX_VM_STATUS_OK, requested_for_call,
                        expected_length,
                        model.position == sizeof(rx33_sample_bytes) - 1u);
                } else {
                    rx34_reference_success(
                        &model, REXX_VM_BYTE_STREAM_OPERATION_READ,
                        requested_for_call, expected_length,
                        model.position == sizeof(rx33_sample_bytes) - 1u);
                }
            }
            ++property_cases;
            ++property_assertions;
            check(rx34_call_named(&vm, "sample", start, length_text,
                                  &result) == operation_status,
                  "RX34 generated operation status matches independent model");
            ++property_assertions;
            if (operation_status == REXX_VM_STATUS_OK)
                check(has_result ? string_is(&result, expected, expected_length)
                                 : string_is(&result, NULL, 0u),
                      "RX34 generated bytes and length match independent model");
            else
                check(result.type == REXX_VALUE_UNSET,
                      "RX34 generated failure exposes no invalid result bytes");
            if (start != NULL && requested_for_call != 0u)
                before_position = strtoull(start_text, NULL, 10) - 1u;
            ++property_assertions;
            check(provider.streams[1].position == model.position &&
                      provider.streams[1].position <=
                          sizeof(rx33_sample_bytes) - 1u,
                  "RX34 generated cursor effects match provider contract");
            rx34_check_reference(&vm, &model, index);
            rexx_value_clear(&result);
        }
        if (action == 6u || action == 7u || action == 8u)
            rx34_check_reference(&vm, &model, index);
    }
    rexx_value_clear(&result);
    rexx_vm_free(&vm);
}
#endif

int main(void)
{
    size_t assertion_mark;
    test_signature_positions_and_bytes();
    test_validation_and_stream_selection();
    test_nonseekable_provider_errors_and_metadata();
    test_queue_line_provider_and_vm_lifecycle();
    test_allocation_failure_and_recovery();
    test_language_routines_interpret_and_vm_isolation();
    test_vm_creation_and_teardown_cycles();
    assertion_mark = assertions;
    test_fixed_seed_reference_model();
#if defined(REXX_RX34_TEST)
    test_rx34_deterministic_status_model();
#endif
    property_assertions = assertions - assertion_mark;
#if defined(REXX_RX34_TEST)
    test_rx34_status_transitions_and_observation();
    test_rx34_callback_errors_and_argument_boundaries();
    test_rx34_capacity_and_vm_isolation();
#endif
    direct_assertions = assertions - property_assertions;
#if defined(REXX_RX34_TEST)
    printf("RX34 CHARIN seed=0x%08X status_capacity=%lu identity_limit=%lu direct_assertions=%lu property_cases=%lu property_assertions=%lu failures=%lu\n",
           (unsigned int)RX34_PROPERTY_SEED,
           (unsigned long)REXX_VM_BYTE_STREAM_STATUS_CAPACITY,
           (unsigned long)REXX_VM_BYTE_STREAM_IDENTITY_MAX_BYTES,
           (unsigned long)direct_assertions,
           (unsigned long)property_cases,
           (unsigned long)property_assertions,
           (unsigned long)failures);
#else
    printf("RX33 CHARIN seed=0x%08X direct_assertions=%lu property_cases=%lu property_assertions=%lu lifecycle_cycles=32 failures=%lu\n",
           (unsigned int)RX33_PROPERTY_SEED,
           (unsigned long)direct_assertions,
           (unsigned long)property_cases,
           (unsigned long)property_assertions,
           (unsigned long)failures);
#endif
    return failures == 0u ? 0 : 1;
}
