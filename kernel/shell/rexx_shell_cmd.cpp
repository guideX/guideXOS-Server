// guideXOS REXX shell command bridge.
//
// The shell has a fixed-size command buffer, but this module still validates
// the input length and all argument bounds. A single allocation contains both
// argv and its NUL-terminated token storage, so every successful allocation
// has one clear release point after the interpreter returns.

#include "rexx_shell_cmd.h"
#include "kernel/shell.h"
#include "kernel/pit.h"
#include "kernel/time.h"
#include "../../rexx_cli.h"

#include <stddef.h>
#include <stdlib.h>

// The REXX CLI is implemented in C. GCC/Clang weak linkage keeps kernel
// configurations that have not yet linked the C interpreter objects usable;
// when present, the command always calls this exact entry point.
#if defined(__GNUC__) || defined(__clang__)
extern "C" int guide_rexx_main_with_output(
    int argc, char** argv, const RexxOutputHost* host) __attribute__((weak));
extern "C" int guide_rexx_main_with_services(
    int argc, char** argv, const RexxOutputHost* host,
    RexxVMClockReader clock_reader, void* clock_user_data) __attribute__((weak));
#else
extern "C" int guide_rexx_main_with_output(
    int argc, char** argv, const RexxOutputHost* host);
extern "C" int guide_rexx_main_with_services(
    int argc, char** argv, const RexxOutputHost* host,
    RexxVMClockReader clock_reader, void* clock_user_data);
#endif

namespace kernel {
namespace shell {
namespace {

static const size_t kMaxCommandLineBytes = 256u;
static const int kMaxArgumentCount = 16;

#if defined(GXOS_REXX_RX34_FIXTURE)
struct Rx34FixtureStream {
    const char* name;
    size_t name_length;
    const unsigned char* bytes;
    size_t length;
    size_t position;
    uint64_t generation;
    bool seekable;
    bool fail_read_once;
};

struct Rx34FixtureContext {
    Rx34FixtureStream streams[5];
};

static const unsigned char kRx34DefaultBytes[] = { 'D', '0' };
static const unsigned char kRx34SampleBytes[] = { 'A', 'B', 'C', 'D', 'E', 'F' };
static const unsigned char kRx34BinaryBytes[] = { 'A', 0x00, 0x80, 0xff, 'B' };
static const unsigned char kRx34NoSeekBytes[] = { 'N', 'S' };
static const unsigned char kRx34FaultBytes[] = { 'F', 'A' };

static bool rx34_name_equals(const unsigned char* name, size_t name_length,
                             const char* expected, size_t expected_length) {
    if (name_length != expected_length) return false;
    for (size_t index = 0; index < name_length; ++index) {
        if (name[index] != static_cast<unsigned char>(expected[index]))
            return false;
    }
    return true;
}

static Rx34FixtureStream* rx34_fixture_identity(void* user_data,
                                                const void* identity) {
    Rx34FixtureContext* context = static_cast<Rx34FixtureContext*>(user_data);
    if (context == NULL || identity == NULL) return NULL;
    for (size_t index = 0; index < 5u; ++index)
        if (identity == &context->streams[index])
            return &context->streams[index];
    return NULL;
}

static RexxVMByteStreamResolveStatus rx34_fixture_resolve(
    void* user_data, int use_default, const unsigned char* name,
    size_t name_length, RexxVMByteStreamInfo* info) {
    Rx34FixtureContext* context = static_cast<Rx34FixtureContext*>(user_data);
    Rx34FixtureStream* stream = NULL;
    if (context == NULL || info == NULL || (name == NULL && name_length != 0u))
        return REXX_VM_BYTE_STREAM_RESOLVE_ERROR;
    if (use_default) {
        stream = &context->streams[0];
    } else {
        for (size_t index = 0; index < 5u; ++index) {
            Rx34FixtureStream* candidate = &context->streams[index];
            if (rx34_name_equals(name, name_length, candidate->name,
                                 candidate->name_length)) {
                stream = candidate;
                break;
            }
        }
    }
    if (stream == NULL) return REXX_VM_BYTE_STREAM_RESOLVE_UNKNOWN;
    info->identity = stream;
    info->stable_identity = reinterpret_cast<const unsigned char*>(stream->name);
    info->stable_identity_length = stream->name_length;
    info->identity_generation = stream->generation;
    info->capabilities = REXX_VM_BYTE_STREAM_CAP_READABLE;
    if (stream->seekable) info->capabilities |= REXX_VM_BYTE_STREAM_CAP_SEEKABLE;
    info->ownership = REXX_VM_BYTE_STREAM_IDENTITY_BORROWED;
    return REXX_VM_BYTE_STREAM_RESOLVE_OK;
}

static RexxVMByteStreamReadStatus rx34_fixture_read(
    void* user_data, const void* identity, size_t requested,
    RexxVMByteStreamRead* result) {
    Rx34FixtureStream* stream = rx34_fixture_identity(user_data, identity);
    if (stream == NULL || result == NULL)
        return REXX_VM_BYTE_STREAM_READ_ERROR;
    if (stream->fail_read_once) {
        stream->fail_read_once = false;
        return REXX_VM_BYTE_STREAM_READ_ERROR;
    }
    if (stream->position >= stream->length)
        return REXX_VM_BYTE_STREAM_READ_EOF;
    size_t available = stream->length - stream->position;
    size_t count = requested < available ? requested : available;
    result->bytes = stream->bytes + stream->position;
    result->length = count;
    result->ownership = REXX_VM_INPUT_OWNERSHIP_BORROWED;
    stream->position += count;
    result->eof_after_read = stream->position == stream->length ? 1u : 0u;
    return REXX_VM_BYTE_STREAM_READ_OK;
}

static RexxVMByteStreamSeekStatus rx34_fixture_seek(
    void* user_data, const void* identity, uint64_t one_based_position) {
    Rx34FixtureStream* stream = rx34_fixture_identity(user_data, identity);
    if (stream == NULL) return REXX_VM_BYTE_STREAM_SEEK_ERROR;
    if (!stream->seekable) return REXX_VM_BYTE_STREAM_SEEK_UNSUPPORTED;
    if (one_based_position == 0u ||
        one_based_position > static_cast<uint64_t>(stream->length) + 1u)
        return REXX_VM_BYTE_STREAM_SEEK_INVALID_POSITION;
    stream->position = static_cast<size_t>(one_based_position - 1u);
    return REXX_VM_BYTE_STREAM_SEEK_OK;
}

static RexxVMByteStreamProvider rx34_fixture_provider() {
    RexxVMByteStreamProvider provider = {};
    provider.resolve = rx34_fixture_resolve;
    provider.read = rx34_fixture_read;
    provider.seek = rx34_fixture_seek;
    return provider;
}

static void rx34_fixture_init(Rx34FixtureContext* context) {
    if (context == NULL) return;
    context->streams[0] = { "default", 7u, kRx34DefaultBytes,
                            sizeof(kRx34DefaultBytes), 0u, 1u, true, false };
    context->streams[1] = { "rx34sample", 10u, kRx34SampleBytes,
                            sizeof(kRx34SampleBytes), 0u, 1u, true, false };
    context->streams[2] = { "rx34binary", 10u, kRx34BinaryBytes,
                            sizeof(kRx34BinaryBytes), 0u, 1u, true, false };
    context->streams[3] = { "noseek", 6u, kRx34NoSeekBytes,
                            sizeof(kRx34NoSeekBytes), 0u, 1u, false, false };
    context->streams[4] = { "fault", 5u, kRx34FaultBytes,
                            sizeof(kRx34FaultBytes), 0u, 1u, true, true };
}
#endif

static int shell_output_writer(void*, RexxOutputStream,
                               const char* bytes, size_t length) {
    return kernel::shell::write_rexx_output(bytes, length) ? 1 : 0;
}

static int shell_output_flush(void*, RexxOutputStream) {
    return 1;
}

static RexxVMClockReadStatus shell_rexx_clock_reader(
    void* user_data, RexxVMClockReading* reading) {
    (void)user_data;
    if (reading == NULL) return REXX_VM_CLOCK_READ_INVALID;
    *reading = RexxVMClockReading{};

#if defined(GXOS_BARE_METAL)
    kernel::time::ClockReading clock{};
    kernel::time::get_clock_reading(clock);
    if (clock.raw_available && clock.resolution_known) {
        reading->capabilities |= REXX_CLOCK_CAP_WALL_CIVIL;
        reading->wall_resolution_nanoseconds = clock.resolution_nanoseconds;
        const kernel::time::DateTime* rexx_calendar = &clock.raw_calendar;
        if (clock.local_available) {
            // REXX currently formats local wall time only. The kernel service
            // must supply that capability before the provider enables it.
            reading->wall_interpretation = REXX_CLOCK_WALL_LOCAL_CIVIL;
            rexx_calendar = &clock.local_calendar;
        } else {
            // Raw RTC fields remain observable to the provider but cannot
            // satisfy a current DATE/TIME wall-clock request.
            reading->wall_interpretation = REXX_CLOCK_WALL_UNSPECIFIED;
        }
        reading->year = rexx_calendar->year;
        reading->month = rexx_calendar->month;
        reading->day = rexx_calendar->day;
        reading->hour = rexx_calendar->hour;
        reading->minute = rexx_calendar->minute;
        reading->second = rexx_calendar->second;
        reading->fractional_nanoseconds = rexx_calendar->nanosecond;
    }

    if (clock.monotonic_available) {
        reading->capabilities |= REXX_CLOCK_CAP_MONOTONIC;
        reading->monotonic_nanoseconds = clock.monotonic_nanoseconds;
        reading->monotonic_resolution_nanoseconds =
            clock.monotonic_resolution_nanoseconds;
    }

    return reading->capabilities != 0u ? REXX_VM_CLOCK_READ_OK
                                       : REXX_VM_CLOCK_READ_UNAVAILABLE;
#else
    return REXX_VM_CLOCK_READ_UNAVAILABLE;
#endif
}

static bool is_shell_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static bool text_equals(const char* left, const char* right) {
    if (left == NULL || right == NULL) return false;
    while (*left != '\0' && *right != '\0') {
        if (*left != *right) return false;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static bool rexx_command_name(const char* value) {
    return text_equals(value, "rexx");
}

static RexxShellTokenizeStatus tokenize_command_line(const char* input,
                                                      int* argc_out,
                                                      char*** argv_out) {
    size_t input_length = 0u;
    size_t pointer_bytes;
    size_t storage_bytes;
    size_t allocation_bytes;
    size_t read_pos = 0u;
    size_t write_pos = 0u;
    int argc = 0;
    char** argv;
    char* storage;

    if (argc_out == NULL || argv_out == NULL) {
        return RexxShellTokenizeStatus::InvalidInput;
    }
    *argc_out = 0;
    *argv_out = NULL;

    if (input == NULL) return RexxShellTokenizeStatus::InvalidInput;

    // Accept at most 255 bytes, matching the kernel shell's 256-byte buffer
    // including its terminating NUL. A line without a NUL in that bound is
    // rejected before any memory is allocated or read past the limit.
    while (input_length < kMaxCommandLineBytes && input[input_length] != '\0') {
        ++input_length;
    }
    if (input_length == kMaxCommandLineBytes) {
        return RexxShellTokenizeStatus::InvalidInput;
    }

    pointer_bytes = (size_t)(kMaxArgumentCount + 1) * sizeof(char*);
    storage_bytes = input_length + 1u;
    if (storage_bytes > (size_t)-1 - pointer_bytes) {
        return RexxShellTokenizeStatus::OutOfMemory;
    }
    allocation_bytes = pointer_bytes + storage_bytes;

    argv = (char**)malloc(allocation_bytes);
    if (argv == NULL) return RexxShellTokenizeStatus::OutOfMemory;
    storage = (char*)(argv + kMaxArgumentCount + 1);

    while (read_pos < input_length) {
        char quote = '\0';

        while (read_pos < input_length && is_shell_space(input[read_pos])) {
            ++read_pos;
        }
        if (read_pos == input_length) break;

        if (argc >= kMaxArgumentCount) {
            free(argv);
            return RexxShellTokenizeStatus::InvalidInput;
        }

        // Record the start even for an empty quoted argument, which is a valid
        // shell token and must not disappear during tokenization.
        argv[argc++] = storage + write_pos;

        while (read_pos < input_length) {
            const char current = input[read_pos];

            if (quote != '\0') {
                if (current == quote) {
                    quote = '\0';
                    ++read_pos;
                    continue;
                }
                // Backslash escapes the following character in double quotes;
                // single-quoted text remains literal by design. Preserve a
                // backslash before ordinary letters so Windows-style paths
                // such as C:\system\apps\monitor.rx remain intact.
                if (quote == '"' && current == '\\') {
                    if (read_pos + 1u < input_length &&
                        (input[read_pos + 1u] == '"' ||
                         input[read_pos + 1u] == '\\')) {
                        ++read_pos;
                        storage[write_pos++] = input[read_pos++];
                    } else {
                        storage[write_pos++] = current;
                        ++read_pos;
                    }
                    continue;
                }
                storage[write_pos++] = current;
                ++read_pos;
                continue;
            }

            if (is_shell_space(current)) break;
            if (current == '\'' || current == '"') {
                quote = current;
                ++read_pos;
                continue;
            }
            if (current == '\\') {
                if (read_pos + 1u < input_length &&
                    (is_shell_space(input[read_pos + 1u]) ||
                     input[read_pos + 1u] == '\\' ||
                     input[read_pos + 1u] == '\'' ||
                     input[read_pos + 1u] == '"')) {
                    ++read_pos;
                    storage[write_pos++] = input[read_pos++];
                } else {
                    storage[write_pos++] = current;
                    ++read_pos;
                }
                continue;
            }

            storage[write_pos++] = current;
            ++read_pos;
        }

        if (quote != '\0') {
            free(argv);
            return RexxShellTokenizeStatus::InvalidInput;
        }
        storage[write_pos++] = '\0';
    }

    argv[argc] = NULL;
    if (argc == 0 || !rexx_command_name(argv[0])) {
        free(argv);
        return RexxShellTokenizeStatus::InvalidInput;
    }

    *argc_out = argc;
    *argv_out = argv;
    return RexxShellTokenizeStatus::Success;
}

} // namespace

bool rexx_cli_available() {
#if defined(__GNUC__) || defined(__clang__)
    return guide_rexx_main_with_services != NULL ||
           guide_rexx_main_with_output != NULL;
#else
    return true;
#endif
}

int shell_cmd_rexx(int argc, char** argv) {
    char resolved_path[256];
    char* resolved_argv[kMaxArgumentCount + 1];
    int script_index = 1;
    int index;
    RexxOutputHost host;
#if defined(GXOS_REXX_RX34_FIXTURE)
    Rx34FixtureContext fixture_context = {};
    RexxVMByteStreamProvider fixture_provider = rx34_fixture_provider();
#endif

    if (argc < 1 || argc > kMaxArgumentCount || argv == NULL || argv[0] == NULL ||
        !rexx_command_name(argv[0])) {
        return 2;
    }
    for (int index = 0; index < argc; ++index) {
        if (argv[index] == NULL) return 2;
    }
    if (!rexx_cli_available()) return 127;

    for (index = 0; index < argc; ++index) resolved_argv[index] = argv[index];
    resolved_argv[argc] = NULL;
    if (script_index < argc && text_equals(resolved_argv[script_index], "--")) {
        ++script_index;
    }
    if (script_index < argc && resolved_argv[script_index][0] != '-') {
        if (!kernel::shell::resolve_rexx_script_path(resolved_argv[script_index],
                                                    resolved_path,
                                                    sizeof(resolved_path))) {
            static const char error[] = "rexx: invalid or oversized script path\n";
            (void)shell_output_writer(NULL, REXX_OUTPUT_STDERR,
                                      error, sizeof(error) - 1u);
            return 2;
        }
        resolved_argv[script_index] = resolved_path;
    }

    host.write = shell_output_writer;
    host.flush = shell_output_flush;
    host.context = NULL;
    host.max_stdout_bytes = 65536u;
    host.max_stderr_bytes = 4096u;
    host.read_capability = NULL;
#if defined(GXOS_REXX_RX34_FIXTURE)
    rx34_fixture_init(&fixture_context);
    return guide_rexx_main_with_stream_provider(
        argc, resolved_argv, &host, shell_rexx_clock_reader, NULL,
        &fixture_provider, &fixture_context);
#endif
#if defined(__GNUC__) || defined(__clang__)
    if (guide_rexx_main_with_services != NULL) {
        return guide_rexx_main_with_services(
            argc, resolved_argv, &host, shell_rexx_clock_reader, NULL);
    }
#else
    return guide_rexx_main_with_services(
        argc, resolved_argv, &host, shell_rexx_clock_reader, NULL);
#endif
    return guide_rexx_main_with_output(argc, resolved_argv, &host);
}

RexxShellTokenizeStatus shell_cmd_rexx_line(const char* command_line,
                                           int* exit_code) {
    int argc = 0;
    char** argv = NULL;
    RexxShellTokenizeStatus tokenize_status;

    if (exit_code == NULL) return RexxShellTokenizeStatus::InvalidInput;
    *exit_code = 2;

    tokenize_status = tokenize_command_line(command_line, &argc, &argv);
    if (tokenize_status != RexxShellTokenizeStatus::Success) {
        return tokenize_status;
    }

    // argv and all its token strings share this allocation. Release it after
    // the synchronous CLI call, regardless of the command's return status.
    *exit_code = shell_cmd_rexx(argc, argv);
    free(argv);
    return RexxShellTokenizeStatus::Success;
}

} // namespace shell
} // namespace kernel
