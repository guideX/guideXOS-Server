// guideXOS REXX shell command bridge.
//
// The shell has a fixed-size command buffer, but this module still validates
// the input length and all argument bounds. A single allocation contains both
// argv and its NUL-terminated token storage, so every successful allocation
// has one clear release point after the interpreter returns.

#include "rexx_shell_cmd.h"

#include <stddef.h>
#include <stdlib.h>

// The REXX CLI is implemented in C. GCC/Clang weak linkage keeps kernel
// configurations that have not yet linked the C interpreter objects usable;
// when present, the command always calls this exact entry point.
#if defined(__GNUC__) || defined(__clang__)
extern "C" int guide_rexx_main(int argc, char** argv) __attribute__((weak));
#else
extern "C" int guide_rexx_main(int argc, char** argv);
#endif

namespace kernel {
namespace shell {
namespace {

static const size_t kMaxCommandLineBytes = 256u;
static const int kMaxArgumentCount = 16;

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
    return guide_rexx_main != NULL;
#else
    return true;
#endif
}

int shell_cmd_rexx(int argc, char** argv) {
    if (argc < 1 || argc > kMaxArgumentCount || argv == NULL || argv[0] == NULL ||
        !rexx_command_name(argv[0])) {
        return 2;
    }
    for (int index = 0; index < argc; ++index) {
        if (argv[index] == NULL) return 2;
    }
    if (!rexx_cli_available()) return 127;

    return guide_rexx_main(argc, argv);
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
