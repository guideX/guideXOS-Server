#include "../rexx_builtin.h"
#include "../rexx_vm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RX34_PROVIDER_STREAM_CAPACITY ((size_t)20u)
#define RX34_PROPERTY_CASES ((size_t)4096u)
#define RX34_LIFECYCLE_CYCLES ((size_t)32u)
#define RX34_PROPERTY_SEED UINT32_C(0x52583434)

static const unsigned char default_bytes[] = { 'D', '0' };
static const unsigned char sample_bytes[] = {
    'A', 0x00u, 0x80u, 0xffu, 'B', 'C'
};
static const unsigned char other_bytes[] = { 'X', 'Y', 'Z' };
static const unsigned char no_seek_bytes[] = { 'N', 'S' };
static const unsigned char fault_bytes[] = { 'E', 'R', 'R' };
static const unsigned char empty_bytes[] = { 0u };

typedef enum Rx34ResolveFault {
    RX34_RESOLVE_FAULT_NONE = 0,
    RX34_RESOLVE_FAULT_UNAVAILABLE,
    RX34_RESOLVE_FAULT_ERROR,
    RX34_RESOLVE_FAULT_BAD_METADATA
} Rx34ResolveFault;

typedef struct Rx34Stream {
    unsigned char name[12];
    size_t name_length;
    const unsigned char *bytes;
    size_t length;
    size_t position;
    uint64_t generation;
    int seekable;
    int fail_read_once;
    int unavailable_once;
} Rx34Stream;

typedef struct Rx34Provider {
    Rx34Stream streams[RX34_PROVIDER_STREAM_CAPACITY];
    unsigned char property_bytes[64];
    size_t stream_count;
    int default_stream;
    size_t resolve_calls;
    size_t read_calls;
    size_t seek_calls;
    Rx34ResolveFault next_resolve_fault;
    int next_seek_error;
} Rx34Provider;

static size_t assertions;
static size_t failures;
static size_t direct_assertions;
static size_t property_assertions;
static size_t property_cases;

static void check(int condition, const char *description, int property)
{
    ++assertions;
    if (property) ++property_assertions;
    else ++direct_assertions;
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

static int string_contains(const RexxValue *value, const char *text)
{
    size_t text_length;
    size_t index;
    if (value == NULL || value->type != REXX_VALUE_STRING || text == NULL)
        return 0;
    text_length = strlen(text);
    if (text_length > value->as.string.length) return 0;
    for (index = 0u; index + text_length <= value->as.string.length; ++index)
        if (memcmp(value->as.string.bytes + index, text, text_length) == 0)
            return 1;
    return 0;
}

static RexxVMStatus set_text(RexxValue *value, const char *text)
{
    return rexx_value_set_string(value, text, strlen(text));
}

static size_t uint_text(size_t value, char *output)
{
    char reversed[3u * sizeof(size_t) + 1u];
    size_t count = 0u;
    size_t index;
    do {
        reversed[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    for (index = 0u; index < count; ++index)
        output[index] = reversed[count - index - 1u];
    output[count] = '\0';
    return count;
}

static Rx34Stream *add_stream(Rx34Provider *provider, const char *name,
                              const unsigned char *bytes, size_t length,
                              int seekable)
{
    Rx34Stream *stream;
    size_t name_length;
    if (provider == NULL || provider->stream_count >=
                                RX34_PROVIDER_STREAM_CAPACITY ||
        name == NULL)
        return NULL;
    name_length = strlen(name);
    if (name_length >= sizeof(provider->streams[0].name)) return NULL;
    stream = &provider->streams[provider->stream_count++];
    memset(stream, 0, sizeof(*stream));
    if (name_length != 0u) memcpy(stream->name, name, name_length);
    stream->name_length = name_length;
    stream->bytes = bytes;
    stream->length = length;
    stream->generation = UINT64_C(1);
    stream->seekable = seekable;
    return stream;
}

static void provider_init(Rx34Provider *provider)
{
    size_t index;
    uint32_t seed = RX34_PROPERTY_SEED;
    memset(provider, 0, sizeof(*provider));
    provider->default_stream = 0;
    (void)add_stream(provider, "default", default_bytes,
                     sizeof(default_bytes), 1);
    (void)add_stream(provider, "sample", sample_bytes,
                     sizeof(sample_bytes), 1);
    (void)add_stream(provider, "other", other_bytes,
                     sizeof(other_bytes), 1);
    (void)add_stream(provider, "noseek", no_seek_bytes,
                     sizeof(no_seek_bytes), 0);
    (void)add_stream(provider, "fault", fault_bytes,
                     sizeof(fault_bytes), 1);
    (void)add_stream(provider, "empty", empty_bytes, 0u, 1);
    (void)add_stream(provider, "", other_bytes, sizeof(other_bytes), 1);

    {
        for (index = 0u; index < sizeof(provider->property_bytes); ++index) {
            seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
            provider->property_bytes[index] = (unsigned char)(seed >> 24);
        }
        (void)add_stream(provider, "property", provider->property_bytes,
                         sizeof(provider->property_bytes), 1);
    }
}

static size_t provider_stream_index(const Rx34Provider *provider,
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
    size_t name_length, RexxVMByteStreamInfo *info)
{
    Rx34Provider *provider = (Rx34Provider *)user_data;
    size_t index;
    if (provider == NULL || info == NULL)
        return REXX_VM_BYTE_STREAM_RESOLVE_ERROR;
    ++provider->resolve_calls;
    if (provider->next_resolve_fault != RX34_RESOLVE_FAULT_NONE) {
        Rx34ResolveFault fault = provider->next_resolve_fault;
        provider->next_resolve_fault = RX34_RESOLVE_FAULT_NONE;
        if (fault == RX34_RESOLVE_FAULT_UNAVAILABLE)
            return REXX_VM_BYTE_STREAM_RESOLVE_UNAVAILABLE;
        if (fault == RX34_RESOLVE_FAULT_ERROR)
            return REXX_VM_BYTE_STREAM_RESOLVE_ERROR;
        if (fault == RX34_RESOLVE_FAULT_BAD_METADATA) {
            info->identity = &provider->streams[0];
            info->stable_identity = provider->streams[0].name;
            info->stable_identity_length = provider->streams[0].name_length;
            info->identity_generation = 0u;
            info->capabilities = REXX_VM_BYTE_STREAM_CAP_READABLE;
            info->ownership = REXX_VM_BYTE_STREAM_IDENTITY_BORROWED;
            return REXX_VM_BYTE_STREAM_RESOLVE_OK;
        }
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
            const Rx34Stream *candidate = &provider->streams[index];
            if (candidate->name_length == name_length &&
                (name_length == 0u ||
                 memcmp(candidate->name, name, name_length) == 0))
                break;
        }
        if (index == provider->stream_count)
            return REXX_VM_BYTE_STREAM_RESOLVE_UNKNOWN;
    }
    info->identity = &provider->streams[index];
    info->stable_identity = provider->streams[index].name;
    info->stable_identity_length = provider->streams[index].name_length;
    info->identity_generation = provider->streams[index].generation;
    info->capabilities = REXX_VM_BYTE_STREAM_CAP_READABLE;
    if (provider->streams[index].seekable)
        info->capabilities |= REXX_VM_BYTE_STREAM_CAP_SEEKABLE;
    info->ownership = REXX_VM_BYTE_STREAM_IDENTITY_BORROWED;
    return REXX_VM_BYTE_STREAM_RESOLVE_OK;
}

static RexxVMByteStreamSeekStatus provider_seek(
    void *user_data, const void *identity, uint64_t one_based_position)
{
    Rx34Provider *provider = (Rx34Provider *)user_data;
    size_t index = provider_stream_index(provider, identity);
    Rx34Stream *stream;
    if (provider == NULL || index == SIZE_MAX)
        return REXX_VM_BYTE_STREAM_SEEK_ERROR;
    ++provider->seek_calls;
    stream = &provider->streams[index];
    if (!stream->seekable) return REXX_VM_BYTE_STREAM_SEEK_UNSUPPORTED;
    if (provider->next_seek_error) {
        provider->next_seek_error = 0;
        return REXX_VM_BYTE_STREAM_SEEK_ERROR;
    }
    if (one_based_position == 0u ||
        one_based_position > (uint64_t)stream->length + UINT64_C(1))
        return REXX_VM_BYTE_STREAM_SEEK_INVALID_POSITION;
    stream->position = (size_t)(one_based_position - UINT64_C(1));
    return REXX_VM_BYTE_STREAM_SEEK_OK;
}

static RexxVMByteStreamReadStatus provider_read(
    void *user_data, const void *identity, size_t requested,
    RexxVMByteStreamRead *read_result)
{
    Rx34Provider *provider = (Rx34Provider *)user_data;
    size_t index = provider_stream_index(provider, identity);
    Rx34Stream *stream;
    size_t available;
    size_t count;
    if (provider == NULL || index == SIZE_MAX || read_result == NULL)
        return REXX_VM_BYTE_STREAM_READ_ERROR;
    ++provider->read_calls;
    stream = &provider->streams[index];
    if (stream->fail_read_once) {
        stream->fail_read_once = 0;
        return REXX_VM_BYTE_STREAM_READ_ERROR;
    }
    if (stream->unavailable_once) {
        stream->unavailable_once = 0;
        return REXX_VM_BYTE_STREAM_READ_UNAVAILABLE;
    }
    if (stream->position >= stream->length)
        return REXX_VM_BYTE_STREAM_READ_EOF;
    available = stream->length - stream->position;
    count = available < requested ? available : requested;
    read_result->bytes = stream->bytes + stream->position;
    read_result->length = count;
    read_result->ownership = REXX_VM_INPUT_OWNERSHIP_BORROWED;
    read_result->eof_after_read = (uint8_t)(count == available);
    stream->position += count;
    return REXX_VM_BYTE_STREAM_READ_OK;
}

static const RexxVMByteStreamProvider provider_callbacks = {
    provider_resolve, provider_read, provider_seek
};

static RexxVMStatus call_charin(RexxVM *vm, const char *name,
                                int include_name, const char *start,
                                const char *length, RexxValue *result)
{
    RexxValue arguments[3] = { REXX_VALUE_INIT, REXX_VALUE_INIT,
                               REXX_VALUE_INIT };
    size_t argument_count = 0u;
    RexxVMStatus status;
    if (include_name) {
        status = set_text(&arguments[0], name != NULL ? name : "");
        if (status != REXX_VM_STATUS_OK) return status;
        argument_count = 1u;
    }
    if (start != NULL) {
        status = set_text(&arguments[1], start);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
        argument_count = 2u;
    }
    if (length != NULL) {
        status = set_text(&arguments[2], length);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
        argument_count = 3u;
    }
    status = rexx_builtin_dispatch_with_vm(vm, "CHARIN", 6u, arguments,
                                          argument_count, result);
cleanup:
    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);
    rexx_value_clear(&arguments[2]);
    return status;
}

static RexxVMStatus call_stream(RexxVM *vm, const char *name,
                                int include_name, const char *operation,
                                RexxValue *result)
{
    RexxValue arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    size_t argument_count = 0u;
    RexxVMStatus status;
    if (include_name) {
        status = set_text(&arguments[0], name != NULL ? name : "");
        if (status != REXX_VM_STATUS_OK) return status;
        argument_count = 1u;
    }
    if (operation != NULL) {
        status = set_text(&arguments[1], operation);
        if (status != REXX_VM_STATUS_OK) goto cleanup;
        argument_count = 2u;
    }
    status = rexx_builtin_dispatch_with_vm(vm, "STREAM", 6u, arguments,
                                          argument_count, result);
cleanup:
    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);
    return status;
}

static Rx34Stream *find_stream(Rx34Provider *provider, const char *name)
{
    size_t index;
    size_t length = strlen(name);
    for (index = 0u; index < provider->stream_count; ++index)
        if (provider->streams[index].name_length == length &&
            (length == 0u ||
             memcmp(provider->streams[index].name, name, length) == 0))
            return &provider->streams[index];
    return NULL;
}

static int init_vm(RexxVM *vm, Rx34Provider *provider)
{
    if (rexx_vm_init(vm, 0u) != REXX_VM_STATUS_OK) return 0;
    return rexx_vm_set_byte_stream_provider(
               vm, &provider_callbacks, provider) == REXX_VM_STATUS_OK;
}

static void test_reads_states_and_recovery(void)
{
    RexxVM vm = { 0 };
    Rx34Provider provider;
    RexxValue result = REXX_VALUE_INIT;
    size_t reads;
    size_t seeks;
    size_t cursor;

    provider_init(&provider);
    check(init_vm(&vm, &provider), "VM/provider initialization", 0);
    check(call_stream(&vm, "sample", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "UNRESOLVED", 10u),
          "registered stream starts unresolved before input", 0);
    check(call_charin(&vm, NULL, 0, NULL, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, default_bytes, 1u),
          "omitted name reads one byte from the provider default", 0);
    check(provider.streams[0].position == 1u,
          "default cursor advances by the returned byte count", 0);
    check(call_charin(&vm, NULL, 0, NULL, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, default_bytes + 1u, 1u),
          "omitted name continues the default stream cursor", 0);

    check(call_charin(&vm, "sample", 1, NULL, "2", &result) ==
              REXX_VM_STATUS_OK && string_is(&result, sample_bytes, 2u),
          "named multi-byte read preserves an embedded NUL", 0);
    check(call_stream(&vm, "sample", 1, "D", &result) ==
              REXX_VM_STATUS_OK && string_contains(&result, "READY") &&
              string_contains(&result, "requested=2 returned=2"),
          "full read is observable as ready with exact byte counts", 0);

    check(call_charin(&vm, "sample", 1, "5", NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "B", 1u),
          "CHARIN(name,5) reads one byte at one-based position five", 0);
    check(provider.streams[1].position == 5u,
          "explicit start is applied once and cursor continues after byte five", 0);
    check(call_charin(&vm, "sample", 1, NULL, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "C", 1u),
          "omitted length defaults to one and continues the cursor", 0);
    check(call_stream(&vm, "sample", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "EXHAUSTED", 9u),
          "exact final-byte read is successful and records exhaustion", 0);

    check(call_charin(&vm, "sample", 1, NULL, "1", &result) ==
              REXX_VM_STATUS_BYTE_STREAM_END_OF_FILE &&
              result.type == REXX_VALUE_UNSET,
          "read after exhaustion takes the distinct EOF error path", 0);
    check(call_stream(&vm, "sample", 1, "D", &result) ==
              REXX_VM_STATUS_OK && string_contains(&result, "EOF") &&
              string_contains(&result, "requested=1 returned=0"),
          "EOF observation stays distinct from a provider error", 0);

    reads = provider.read_calls;
    check(call_charin(&vm, "sample", 1, "3", "0", &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "", 0u),
          "zero-length explicit start repositions without reading", 0);
    check(provider.streams[1].position == 2u && provider.read_calls == reads,
          "zero-length positioning changes only the provider cursor", 0);
    check(call_stream(&vm, "sample", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "READY", 5u),
          "successful explicit seek recovers the prior EOF state", 0);
    check(call_charin(&vm, "sample", 1, NULL, NULL, &result) ==
              REXX_VM_STATUS_OK && result.as.string.length == 1u &&
              (unsigned char)result.as.string.bytes[0] == 0x80u,
          "high-bit stream bytes survive a seek and read", 0);

    check(call_charin(&vm, "sample", 1, "6", "2", &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "C", 1u),
          "short read returns all available bytes", 0);
    check(call_stream(&vm, "sample", 1, "D", &result) ==
              REXX_VM_STATUS_OK &&
              string_contains(&result, "SHORT_READ requested=2 returned=1 eof=1"),
          "short read is retained with exact request/return metadata", 0);
    check(call_charin(&vm, "sample", 1, NULL, "0", &result) ==
              REXX_VM_STATUS_OK && result.as.string.length == 0u,
          "zero length without a start returns empty", 0);
    check(call_stream(&vm, "sample", 1, "D", &result) ==
              REXX_VM_STATUS_OK &&
              string_is(&result, "SHORT_READ", 10u),
          "zero length refreshes detail without clearing the short-read condition", 0);
    check(call_charin(&vm, "sample", 1, NULL, NULL, &result) ==
              REXX_VM_STATUS_BYTE_STREAM_END_OF_FILE,
          "later exhaustion observation replaces short-read detail with EOF", 0);
    check(call_charin(&vm, "sample", 1, "1", "1", &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "A", 1u),
          "a valid reposition and successful read recover from EOF", 0);

    check(call_stream(&vm, "other", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "UNRESOLVED", 10u),
          "status is isolated by provider stream identity", 0);
    find_stream(&provider, "fault")->fail_read_once = 1;
    check(call_charin(&vm, "fault", 1, NULL, "1", &result) ==
              REXX_VM_STATUS_BYTE_STREAM_PROVIDER_ERROR,
          "provider I/O failure remains distinct from EOF", 0);
    check(call_stream(&vm, "fault", 1, "D", &result) ==
              REXX_VM_STATUS_OK && string_contains(&result, "ERROR"),
          "provider failure is observable", 0);
    check(call_charin(&vm, "fault", 1, NULL, "1", &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "E", 1u),
          "later successful read recovers a provider error", 0);
    check(call_stream(&vm, "fault", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "READY", 5u),
          "successful recovery replaces the earlier error state", 0);

    find_stream(&provider, "sample")->position = 0u;
    check(call_charin(&vm, "sample", 1, "5", "1", &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "B", 1u),
          "reposition establishes a ready state before invalid seek", 0);
    reads = provider.read_calls;
    seeks = provider.seek_calls;
    cursor = find_stream(&provider, "sample")->position;
    check(call_charin(&vm, "sample", 1, "99", "1", &result) ==
              REXX_VM_STATUS_INVALID_FUNCTION_CALL,
          "provider rejects an out-of-range position as invalid", 0);
    check(provider.read_calls == reads && provider.seek_calls == seeks + 1u &&
              find_stream(&provider, "sample")->position == cursor,
          "invalid seek does not read, move cursor, or corrupt prior state", 0);
    check(call_stream(&vm, "sample", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "READY", 5u),
          "valid prior state survives invalid positioning", 0);
    check(call_charin(&vm, "noseek", 1, "1", "1", &result) ==
              REXX_VM_STATUS_BYTE_STREAM_UNSUPPORTED_OPERATION &&
              find_stream(&provider, "noseek")->position == 0u,
          "nonseekable positioning fails before cursor movement", 0);

    find_stream(&provider, "sample")->fail_read_once = 1;
    check(call_charin(&vm, "sample", 1, "1", "2", &result) ==
              REXX_VM_STATUS_BYTE_STREAM_PROVIDER_ERROR,
          "read fault injection reaches the provider callback", 0);
    check(call_stream(&vm, "sample", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "ERROR", 5u),
          "read failure replaces the prior state with provider error", 0);
    check(call_charin(&vm, "sample", 1, NULL, "0", &result) ==
              REXX_VM_STATUS_OK,
          "zero-length no-position read after error still succeeds", 0);
    check(call_stream(&vm, "sample", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "ERROR", 5u),
          "zero-length read does not clear provider error", 0);
    check(call_charin(&vm, "sample", 1, "1", "1", &result) ==
              REXX_VM_STATUS_OK,
          "explicit seek and successful read recover provider state", 0);

    find_stream(&provider, "sample")->unavailable_once = 1;
    check(call_charin(&vm, "sample", 1, NULL, "1", &result) ==
              REXX_VM_STATUS_BYTE_STREAM_NOT_READY,
          "provider not-ready response is not converted to EOF", 0);
    check(call_stream(&vm, "sample", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "NOTREADY", 8u),
          "not-ready state is observable", 0);

    rexx_value_clear(&result);
    rexx_vm_free(&vm);
    check(!vm.initialized && vm.byte_stream_status_count == 0u,
          "VM teardown releases the entire status table", 0);
    rexx_value_clear(&result);
}

static void test_unknown_unavailable_metadata_and_observation(void)
{
    RexxVM vm = { 0 };
    Rx34Provider provider;
    RexxValue result = REXX_VALUE_INIT;
    RexxValue query_arguments[2] = { REXX_VALUE_INIT, REXX_VALUE_INIT };
    size_t read_calls;
    size_t seek_calls;
    size_t resolve_calls;

    provider_init(&provider);
    check(rexx_vm_init(&vm, 0u) == REXX_VM_STATUS_OK,
          "unconfigured VM initialization", 0);
    check(call_stream(&vm, NULL, 0, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "UNAVAILABLE", 11u),
          "status query exposes unavailable provider without fabricating input", 0);
    check(call_charin(&vm, NULL, 0, NULL, NULL, &result) ==
              REXX_VM_STATUS_BYTE_STREAM_UNAVAILABLE,
          "production-style no-provider CHARIN stays fail-closed", 0);
    check(rexx_vm_set_byte_stream_provider(&vm, &provider_callbacks,
                                           &provider) ==
              REXX_VM_STATUS_OK,
          "test provider installation", 0);
    check(call_stream(&vm, "missing", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "UNKNOWN", 7u),
          "unknown name is distinct from unresolved and unavailable", 0);
    check(call_stream(&vm, "", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "UNRESOLVED", 10u),
          "explicit empty name remains an explicit registered lookup", 0);
    check(call_stream(&vm, NULL, 0, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "UNRESOLVED", 10u),
          "omitted status name selects the provider default", 0);

    provider.next_resolve_fault = RX34_RESOLVE_FAULT_UNAVAILABLE;
    check(call_stream(&vm, "sample", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "NOTREADY", 8u),
          "temporarily unavailable resolution is observable as NOTREADY", 0);
    provider.next_resolve_fault = RX34_RESOLVE_FAULT_ERROR;
    check(call_stream(&vm, "sample", 1, "D", &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "ERROR", 5u),
          "resolver provider error is observable through STREAM detail", 0);
    provider.next_resolve_fault = RX34_RESOLVE_FAULT_BAD_METADATA;
    check(call_stream(&vm, "sample", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "INVALID", 7u),
          "malformed provider identity generation is reported as invalid", 0);

    read_calls = provider.read_calls;
    seek_calls = provider.seek_calls;
    resolve_calls = provider.resolve_calls;
    check(set_text(&query_arguments[0], "sample") == REXX_VM_STATUS_OK &&
              set_text(&query_arguments[1], "D") == REXX_VM_STATUS_OK,
          "status-query arguments allocated before failure injection", 0);
    rexx_vm_test_allocator_fail_on(1u);
    check(rexx_builtin_dispatch_with_vm(&vm, "STREAM", 6u,
                                        query_arguments, 2u, &result) ==
              REXX_VM_STATUS_OUT_OF_MEMORY,
          "status observation output allocation failure is reported", 0);
    rexx_vm_test_allocator_disable();
    check(provider.resolve_calls == resolve_calls + 1u &&
              provider.read_calls == read_calls &&
              provider.seek_calls == seek_calls,
          "failed observation resolves only and does not read or seek", 0);
    rexx_value_clear(&query_arguments[0]);
    rexx_value_clear(&query_arguments[1]);

    rexx_value_clear(&result);
    rexx_vm_free(&vm);
}

static void test_copy_failure_boundary(void)
{
    RexxVM vm = { 0 };
    Rx34Provider provider;
    RexxValue result = REXX_VALUE_INIT;

    provider_init(&provider);
    check(init_vm(&vm, &provider), "copy-boundary VM/provider initialization", 0);
    rexx_vm_test_byte_stream_copy_fail_on(1u);
    check(call_charin(&vm, "sample", 1, "6", "2", &result) ==
              REXX_VM_STATUS_OUT_OF_MEMORY &&
              result.type == REXX_VALUE_UNSET,
          "injected result-copy refusal occurs after provider read", 0);
    rexx_vm_test_byte_stream_copy_disable();
    check(find_stream(&provider, "sample")->position == sizeof(sample_bytes),
          "provider cursor advances before injected VM-side copy refusal", 0);
    check(call_stream(&vm, "sample", 1, "D", &result) ==
              REXX_VM_STATUS_OK &&
              string_contains(&result, "SHORT_READ requested=2 returned=1 eof=1"),
          "status records the completed short read rather than copy failure", 0);
    check(call_charin(&vm, "sample", 1, NULL, "1", &result) ==
              REXX_VM_STATUS_BYTE_STREAM_END_OF_FILE,
          "next read resumes after provider-consumed bytes and sees EOF", 0);
    rexx_value_clear(&result);
    rexx_vm_free(&vm);
}

static void test_generation_replacement_and_vm_isolation(void)
{
    RexxVM vm_a = { 0 };
    RexxVM vm_b = { 0 };
    Rx34Provider provider_a;
    Rx34Provider provider_b;
    RexxValue result = REXX_VALUE_INIT;
    static const unsigned char bytes_b[] = { 'B' };
    Rx34Stream *stream_a;

    provider_init(&provider_a);
    provider_init(&provider_b);
    stream_a = find_stream(&provider_a, "sample");
    stream_a->fail_read_once = 1;
    provider_b.streams[1].bytes = bytes_b;
    provider_b.streams[1].length = sizeof(bytes_b);
    check(init_vm(&vm_a, &provider_a) && init_vm(&vm_b, &provider_b),
          "two independent VMs initialize", 0);
    check(call_charin(&vm_a, "sample", 1, NULL, "1", &result) ==
              REXX_VM_STATUS_BYTE_STREAM_PROVIDER_ERROR,
          "first VM records provider error for a shared name", 0);
    check(call_stream(&vm_b, "sample", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "UNRESOLVED", 10u),
          "same-name stream in another VM has independent state", 0);
    check(call_charin(&vm_b, "sample", 1, NULL, "1", &result) ==
              REXX_VM_STATUS_OK && string_is(&result, bytes_b, 1u),
          "second VM reads its own provider data", 0);

    stream_a->generation = UINT64_C(2);
    check(call_stream(&vm_a, "sample", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "UNRESOLVED", 10u),
          "reused identity with a new generation cannot inherit stale status", 0);
    check(call_charin(&vm_a, "sample", 1, NULL, "1", &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "A", 1u),
          "new identity generation starts a fresh cursor observation", 0);

    check(call_charin(&vm_a, "sample", 1, "1", "1", &result) ==
              REXX_VM_STATUS_OK,
          "old provider records status before replacement", 0);
    provider_init(&provider_b);
    provider_b.streams[1].bytes = bytes_b;
    provider_b.streams[1].length = sizeof(bytes_b);
    check(rexx_vm_set_byte_stream_provider(&vm_a, &provider_callbacks,
                                           &provider_b) ==
              REXX_VM_STATUS_OK,
          "quiescent provider replacement succeeds", 0);
    check(call_stream(&vm_a, "sample", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "UNRESOLVED", 10u),
          "provider replacement clears every prior status", 0);
    check(call_charin(&vm_a, "sample", 1, NULL, "1", &result) ==
              REXX_VM_STATUS_OK && string_is(&result, bytes_b, 1u),
          "replacement provider data is used immediately", 0);

    rexx_value_clear(&result);
    rexx_vm_free(&vm_a);
    rexx_vm_free(&vm_b);
}

static uint32_t property_next(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void test_fixed_seed_properties(void)
{
    RexxVM vm = { 0 };
    Rx34Provider provider;
    Rx34Stream *stream;
    RexxValue result = REXX_VALUE_INIT;
    uint32_t random_state = RX34_PROPERTY_SEED;
    size_t case_index;

    provider_init(&provider);
    check(init_vm(&vm, &provider), "property VM/provider initialization", 1);
    stream = find_stream(&provider, "property");
    check(stream != NULL, "fixed-seed property stream registered", 1);
    for (case_index = 0u; case_index < RX34_PROPERTY_CASES; ++case_index) {
        size_t start;
        size_t requested;
        size_t available;
        size_t expected_length;
        size_t expected_position;
        char start_text[3u * sizeof(size_t) + 1u];
        char length_text[3u * sizeof(size_t) + 1u];
        const char *expected_state;
        RexxVMStatus status;
        uint32_t next = property_next(&random_state);

        start = (size_t)(next % (uint32_t)(stream->length + 1u)) + 1u;
        requested = (size_t)(property_next(&random_state) % UINT32_C(8)) + 1u;
        available = stream->length - (start - 1u);
        expected_length = available < requested ? available : requested;
        expected_position = start - 1u + expected_length;
        (void)uint_text(start, start_text);
        (void)uint_text(requested, length_text);

        status = call_charin(&vm, "property", 1, start_text, length_text,
                             &result);
        check(status == (expected_length == 0u
                             ? REXX_VM_STATUS_BYTE_STREAM_END_OF_FILE
                             : REXX_VM_STATUS_OK),
              "property CHARIN status matches bounded stream model", 1);
        check(expected_length == 0u
                  ? result.type == REXX_VALUE_UNSET
                  : string_is(&result, stream->bytes + start - 1u,
                              expected_length),
              "property CHARIN bytes match deterministic fixture", 1);
        check(stream->position == expected_position,
              "property cursor advances once by returned byte count", 1);
        expected_state = expected_length == 0u ? "EOF"
                        : expected_length < requested ? "SHORT_READ"
                        : expected_position == stream->length ? "EXHAUSTED"
                                                             : "READY";
        check(call_stream(&vm, "property", 1, "D", &result) ==
                  REXX_VM_STATUS_OK && string_contains(&result, expected_state) &&
                  string_contains(&result, "requested=") &&
                  string_contains(&result, "returned="),
              "property STREAM state matches read and EOF model", 1);
        ++property_cases;
    }
    check(property_cases == RX34_PROPERTY_CASES,
          "all fixed-seed property cases executed", 1);
    rexx_value_clear(&result);
    rexx_vm_free(&vm);
}

static void test_capacity_and_lifecycle(void)
{
    RexxVM vm = { 0 };
    Rx34Provider provider;
    RexxValue result = REXX_VALUE_INIT;
    static const unsigned char one_byte[] = { 'Q' };
    size_t index;
    size_t read_calls;

    provider_init(&provider);
    provider.stream_count = 0u;
    provider.default_stream = 0;
    for (index = 0u; index < REXX_VM_BYTE_STREAM_STATUS_CAPACITY + 1u;
         ++index) {
        char name[8];
        name[0] = 's';
        name[1] = (char)('0' + (index / 10u));
        name[2] = (char)('0' + (index % 10u));
        name[3] = '\0';
        (void)add_stream(&provider, name, one_byte, sizeof(one_byte), 1);
    }
    check(init_vm(&vm, &provider), "capacity VM/provider initialization", 0);
    for (index = 0u; index < REXX_VM_BYTE_STREAM_STATUS_CAPACITY; ++index) {
        char name[8];
        name[0] = 's';
        name[1] = (char)('0' + (index / 10u));
        name[2] = (char)('0' + (index % 10u));
        name[3] = '\0';
        check(call_charin(&vm, name, 1, NULL, NULL, &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, one_byte, 1u),
              "status capacity accepts each bounded identity", 0);
    }
    read_calls = provider.read_calls;
    check(call_charin(&vm, "s16", 1, NULL, NULL, &result) ==
              REXX_VM_STATUS_RESOURCE_LIMIT,
          "status capacity exhaustion fails deterministically", 0);
    check(provider.read_calls == read_calls && provider.streams[16].position == 0u,
          "capacity exhaustion occurs before the provider consumes bytes", 0);
    check(call_stream(&vm, "s16", 1, NULL, &result) ==
              REXX_VM_STATUS_OK && string_is(&result, "UNRESOLVED", 10u),
          "status query remains available when no table slot can be allocated", 0);
    rexx_value_clear(&result);
    rexx_vm_free(&vm);

    for (index = 0u; index < RX34_LIFECYCLE_CYCLES; ++index) {
        Rx34Provider cycle_provider;
        Rx34Stream *cycle_stream;
        memset(&vm, 0, sizeof(vm));
        provider_init(&cycle_provider);
        cycle_stream = find_stream(&cycle_provider, "sample");
        check(init_vm(&vm, &cycle_provider),
              "lifecycle cycle VM/provider initialization", 0);
        check(call_charin(&vm, "sample", 1, NULL, "2", &result) ==
                  REXX_VM_STATUS_OK && result.as.string.length == 2u,
              "lifecycle cycle records a bounded state", 0);
        check(call_stream(&vm, "sample", 1, NULL, &result) ==
                  REXX_VM_STATUS_OK && string_is(&result, "READY", 5u),
              "lifecycle cycle observes the current status", 0);
        rexx_value_clear(&result);
        rexx_vm_free(&vm);
        check(!vm.initialized && vm.byte_stream_status_count == 0u &&
                  cycle_stream->position == 2u,
              "lifecycle teardown clears VM state but not provider-owned cursor", 0);
    }
}

int main(void)
{
    test_reads_states_and_recovery();
    test_unknown_unavailable_metadata_and_observation();
    test_copy_failure_boundary();
    test_generation_replacement_and_vm_isolation();
    test_fixed_seed_properties();
    test_capacity_and_lifecycle();
    printf("RX34 byte-stream state assertions: %lu (%lu direct, %lu property); "
           "property_cases=%lu seed=0x%08lx lifecycle_cycles=%lu failures=%lu\n",
           (unsigned long)assertions, (unsigned long)direct_assertions,
           (unsigned long)property_assertions, (unsigned long)property_cases,
           (unsigned long)RX34_PROPERTY_SEED,
           (unsigned long)RX34_LIFECYCLE_CYCLES, (unsigned long)failures);
    return failures == 0u ? 0 : 1;
}
