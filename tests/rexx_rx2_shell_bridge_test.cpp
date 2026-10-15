#include "../kernel/shell/rexx_shell_cmd.h"
#include "../kernel/core/include/kernel/shell.h"
#include "../rexx_cli.h"

extern "C" int printf(const char* format, ...);

static int assertions;
static bool fail_path_resolution;
static char routed_output[256];
static size_t routed_length;
static char observed_arguments[4][128];
static int observed_argc;
static bool observed_host_limits;

#define CHECK(condition, name) do { \
    ++assertions; \
    if (!(condition)) { \
        printf("[FAIL] %s (line %d)\n", name, __LINE__); \
        return 1; \
    } \
} while (0)

static size_t test_length(const char* text)
{
    size_t length = 0u;
    while (text != nullptr && text[length] != '\0') ++length;
    return length;
}

static bool test_equals(const char* left, const char* right)
{
    size_t index = 0u;
    if (left == nullptr || right == nullptr) return left == right;
    while (left[index] != '\0' && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

static bool test_contains(const char* text, const char* needle)
{
    const size_t needle_length = test_length(needle);
    size_t offset = 0u;
    if (needle_length == 0u) return true;
    while (text != nullptr && text[offset] != '\0') {
        size_t index = 0u;
        while (index < needle_length && text[offset + index] == needle[index])
            ++index;
        if (index == needle_length) return true;
        ++offset;
    }
    return false;
}

namespace kernel {
namespace shell {

bool write_rexx_output(const char* bytes, size_t length)
{
    if (bytes == nullptr && length != 0u) return false;
    if (length > sizeof(routed_output) - routed_length - 1u) return false;
    for (size_t index = 0u; index < length; ++index)
        routed_output[routed_length + index] = bytes[index];
    routed_length += length;
    routed_output[routed_length] = '\0';
    return true;
}

bool resolve_rexx_script_path(const char* path,
                              char* output,
                              size_t output_size)
{
    static const char prefix[] = "/cwd/";
    const size_t path_length = test_length(path);
    const size_t prefix_length = sizeof(prefix) - 1u;
    if (fail_path_resolution || path == nullptr || output == nullptr ||
        output_size <= prefix_length + path_length) return false;
    for (size_t index = 0u; index < prefix_length; ++index)
        output[index] = prefix[index];
    for (size_t index = 0u; index <= path_length; ++index)
        output[prefix_length + index] = path[index];
    return true;
}

} // namespace shell
} // namespace kernel

extern "C" int guide_rexx_main_with_output(int argc,
                                            char** argv,
                                            const RexxOutputHost* host)
{
    observed_argc = argc;
    for (int index = 0; index < 4; ++index) {
        observed_arguments[index][0] = '\0';
        if (index < argc && argv[index] != nullptr) {
            size_t length = test_length(argv[index]);
            size_t count = length < sizeof(observed_arguments[index]) - 1u
                               ? length : sizeof(observed_arguments[index]) - 1u;
            for (size_t byte = 0u; byte < count; ++byte)
                observed_arguments[index][byte] = argv[index][byte];
            observed_arguments[index][count] = '\0';
        }
    }
    observed_host_limits = host != nullptr && host->write != nullptr &&
                           host->max_stdout_bytes == 65536u &&
                           host->max_stderr_bytes == 4096u;
    if (host != nullptr && host->write != nullptr) {
        static const char stdout_text[] = "visible-output\n";
        static const char stderr_text[] = "diagnostic\n";
        if (!host->write(host->context, REXX_OUTPUT_STDOUT,
                         stdout_text, sizeof(stdout_text) - 1u) ||
            !host->write(host->context, REXX_OUTPUT_STDERR,
                         stderr_text, sizeof(stderr_text) - 1u)) return 99;
    }
    return 7;
}

int main()
{
    int exit_code = -1;
    kernel::shell::RexxShellTokenizeStatus status;

    status = kernel::shell::shell_cmd_rexx_line(
        "rexx \"hello world.rx\"", &exit_code);
    CHECK(status == kernel::shell::RexxShellTokenizeStatus::Success && exit_code == 7,
          "quoted script argument returns CLI status");
    CHECK(observed_argc == 2 &&
          test_equals(observed_arguments[1], "/cwd/hello world.rx"),
          "quoted script path is resolved without splitting");
    CHECK(observed_host_limits &&
          test_equals(routed_output, "visible-output\ndiagnostic\n"),
          "shell supplies bounded output host for both streams");

    routed_length = 0u;
    routed_output[0] = '\0';
    status = kernel::shell::shell_cmd_rexx_line(
        "rexx -- \"hello world.rx\"", &exit_code);
    CHECK(status == kernel::shell::RexxShellTokenizeStatus::Success && exit_code == 7,
          "-- path form returns CLI status");
    CHECK(observed_argc == 3 &&
          test_equals(observed_arguments[1], "--") &&
          test_equals(observed_arguments[2], "/cwd/hello world.rx"),
          "-- and quoted path reach CLI in order");

    fail_path_resolution = true;
    routed_length = 0u;
    routed_output[0] = '\0';
    status = kernel::shell::shell_cmd_rexx_line("rexx missing.rx", &exit_code);
    CHECK(status == kernel::shell::RexxShellTokenizeStatus::Success && exit_code == 2,
          "invalid shell path returns usage status");
    CHECK(test_contains(routed_output, "invalid or oversized script path"),
          "invalid shell path has a visible diagnostic");

    printf("RX2 shell bridge assertions passed: %d\n", assertions);
    return 0;
}
