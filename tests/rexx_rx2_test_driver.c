#include "../rexx_cli.h"
#include "../rexx_numeric.h"
#include "../rexx_os_io.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct FakeVFS {
    const char *data;
    size_t length;
    size_t offset;
    size_t max_read;
    uint64_t size_override;
    RexxOSIOStatus open_status;
    RexxOSIOStatus size_status;
    RexxOSIOStatus read_status;
    RexxOSIOStatus close_status;
    int return_handle_on_open_failure;
    int zero_read;
    int override_size;
    unsigned open_calls;
    unsigned size_calls;
    unsigned read_calls;
    unsigned close_calls;
} FakeVFS;

typedef struct Capture {
    char stdout_bytes[4096];
    char stderr_bytes[4096];
    size_t stdout_length;
    size_t stderr_length;
    unsigned stdout_flushes;
    unsigned stderr_flushes;
    int fail_stdout;
} Capture;

static int test_count;

#define CHECK(condition, name) do { \
    ++test_count; \
    if (!(condition)) { \
        fprintf(stderr, "[FAIL] %s (line %d)\n", name, __LINE__); \
        return 0; \
    } \
} while (0)

static RexxOSIOStatus fake_open(void *context,
                               const char *path,
                               void **out_handle)
{
    FakeVFS *vfs = (FakeVFS *)context;
    (void)path;
    ++vfs->open_calls;
    if (vfs->open_status != REXX_OS_IO_OK) {
        *out_handle = vfs->return_handle_on_open_failure ? context : NULL;
        return vfs->open_status;
    }
    *out_handle = context;
    return REXX_OS_IO_OK;
}

static RexxOSIOStatus fake_size(void *context,
                               void *handle,
                               uint64_t *out_size)
{
    FakeVFS *vfs = (FakeVFS *)context;
    ++vfs->size_calls;
    if (handle != context || vfs->size_status != REXX_OS_IO_OK)
        return vfs->size_status != REXX_OS_IO_OK
                   ? vfs->size_status : REXX_OS_IO_INVALID_ARGUMENT;
    *out_size = vfs->override_size ? vfs->size_override : (uint64_t)vfs->length;
    return REXX_OS_IO_OK;
}

static RexxOSIOStatus fake_read(void *context,
                               void *handle,
                               void *buffer,
                               uint32_t requested,
                               uint32_t *out_read)
{
    FakeVFS *vfs = (FakeVFS *)context;
    size_t remaining;
    size_t count;
    ++vfs->read_calls;
    *out_read = 0u;
    if (handle != context) return REXX_OS_IO_INVALID_ARGUMENT;
    if (vfs->read_status != REXX_OS_IO_OK) return vfs->read_status;
    if (vfs->zero_read) return REXX_OS_IO_OK;
    remaining = vfs->length - vfs->offset;
    count = remaining < (size_t)requested ? remaining : (size_t)requested;
    if (vfs->max_read != 0u && count > vfs->max_read) count = vfs->max_read;
    if (count != 0u) memcpy(buffer, vfs->data + vfs->offset, count);
    vfs->offset += count;
    *out_read = (uint32_t)count;
    return REXX_OS_IO_OK;
}

static RexxOSIOStatus fake_close(void *context, void *handle)
{
    FakeVFS *vfs = (FakeVFS *)context;
    ++vfs->close_calls;
    if (handle != context) return REXX_OS_IO_INVALID_ARGUMENT;
    return vfs->close_status;
}

static const RexxOSVFSOperations fake_operations = {
    fake_open, fake_size, fake_read, fake_close
};

static RexxOSIOStatus install_vfs(FakeVFS *vfs)
{
    return rexx_os_set_vfs_operations(&fake_operations, vfs);
}

static int capture_write(void *context,
                         RexxOutputStream stream,
                         const char *bytes,
                         size_t length)
{
    Capture *capture = (Capture *)context;
    char *destination;
    size_t *used;
    size_t capacity;
    if (stream == REXX_OUTPUT_STDOUT) {
        if (capture->fail_stdout) return 0;
        destination = capture->stdout_bytes;
        used = &capture->stdout_length;
        capacity = sizeof(capture->stdout_bytes);
    } else {
        destination = capture->stderr_bytes;
        used = &capture->stderr_length;
        capacity = sizeof(capture->stderr_bytes);
    }
    if (length > capacity - *used - 1u) return 0;
    memcpy(destination + *used, bytes, length);
    *used += length;
    destination[*used] = '\0';
    return 1;
}

static int capture_flush(void *context, RexxOutputStream stream)
{
    Capture *capture = (Capture *)context;
    if (stream == REXX_OUTPUT_STDOUT) ++capture->stdout_flushes;
    else ++capture->stderr_flushes;
    return 1;
}

static RexxOutputHost capture_host(Capture *capture,
                                   size_t stdout_limit,
                                   size_t stderr_limit)
{
    RexxOutputHost host;
    host.write = capture_write;
    host.flush = capture_flush;
    host.context = capture;
    host.max_stdout_bytes = stdout_limit;
    host.max_stderr_bytes = stderr_limit;
    host.read_capability = NULL;
    return host;
}

static int test_file_load_partial_reads(void)
{
    static const char bytes[] = "partial-read-content";
    FakeVFS vfs = {0};
    char *loaded = NULL;
    size_t length = 0u;
    vfs.data = bytes;
    vfs.length = sizeof(bytes) - 1u;
    vfs.max_read = 3u;
    CHECK(install_vfs(&vfs) == REXX_OS_IO_OK, "install fake VFS");
    CHECK(rexx_os_read_file_ex("fixture.rx", &loaded, &length) == REXX_OS_IO_OK,
          "partial file read succeeds");
    CHECK(length == vfs.length && memcmp(loaded, bytes, length) == 0,
          "partial file bytes preserved");
    CHECK(loaded[length] == '\0', "file result is terminated");
    CHECK(vfs.read_calls > 1u && vfs.close_calls == 1u,
          "short reads continue and close once");
    rexx_os_free_file(loaded);

    memset(&vfs, 0, sizeof(vfs));
    vfs.data = "";
    vfs.length = 0u;
    loaded = NULL;
    length = 99u;
    CHECK(install_vfs(&vfs) == REXX_OS_IO_OK, "install empty-file VFS");
    CHECK(rexx_os_read_file_ex("empty.rx", &loaded, &length) == REXX_OS_IO_OK,
          "empty script loads successfully");
    CHECK(length == 0u && loaded != NULL && loaded[0] == '\0' &&
          vfs.read_calls == 0u && vfs.close_calls == 1u,
          "empty script has owned terminator and closes once");
    rexx_os_free_file(loaded);
    return 1;
}

static int test_file_load_failures(void)
{
    FakeVFS vfs = {0};
    char *loaded = (char *)(uintptr_t)1u;
    size_t length = 99u;

    vfs.open_status = REXX_OS_IO_NOT_FOUND;
    CHECK(install_vfs(&vfs) == REXX_OS_IO_OK, "install not-found VFS");
    CHECK(rexx_os_read_file_ex("missing.rx", &loaded, &length) == REXX_OS_IO_NOT_FOUND,
          "missing file status preserved");
    CHECK(loaded == NULL && length == 0u && vfs.close_calls == 0u,
          "failed open clears outputs without close");

    memset(&vfs, 0, sizeof(vfs));
    vfs.open_status = REXX_OS_IO_OPEN_FAILED;
    vfs.return_handle_on_open_failure = 1;
    CHECK(install_vfs(&vfs) == REXX_OS_IO_OK, "install failed-open VFS");
    CHECK(rexx_os_read_file_ex("bad-open.rx", &loaded, &length) == REXX_OS_IO_OPEN_FAILED,
          "open error status preserved");
    CHECK(vfs.close_calls == 1u, "handle returned with open failure is closed");

    memset(&vfs, 0, sizeof(vfs));
    vfs.data = "data";
    vfs.length = 4u;
    vfs.size_status = REXX_OS_IO_READ_FAILED;
    CHECK(install_vfs(&vfs) == REXX_OS_IO_OK, "install size-failure VFS");
    CHECK(rexx_os_read_file_ex("size.rx", &loaded, &length) == REXX_OS_IO_READ_FAILED,
          "size error status preserved");
    CHECK(vfs.close_calls == 1u && loaded == NULL && length == 0u,
          "size failure closes and clears outputs");

    memset(&vfs, 0, sizeof(vfs));
    vfs.data = "data";
    vfs.length = 4u;
    vfs.zero_read = 1;
    CHECK(install_vfs(&vfs) == REXX_OS_IO_OK, "install zero-read VFS");
    CHECK(rexx_os_read_file_ex("stalled.rx", &loaded, &length) == REXX_OS_IO_READ_FAILED,
          "zero progress read fails");
    CHECK(vfs.close_calls == 1u && loaded == NULL,
          "zero progress cleanup closes and clears outputs");

    memset(&vfs, 0, sizeof(vfs));
    vfs.data = "data";
    vfs.length = 4u;
    vfs.read_status = REXX_OS_IO_READ_FAILED;
    CHECK(install_vfs(&vfs) == REXX_OS_IO_OK, "install read-failure VFS");
    CHECK(rexx_os_read_file_ex("read-error.rx", &loaded, &length) == REXX_OS_IO_READ_FAILED,
          "read error status preserved");
    CHECK(vfs.close_calls == 1u && loaded == NULL,
          "read failure closes and clears outputs");

    memset(&vfs, 0, sizeof(vfs));
    vfs.override_size = 1;
    vfs.size_override = (uint64_t)REXX_OS_MAX_SOURCE_BYTES + 1u;
    CHECK(install_vfs(&vfs) == REXX_OS_IO_OK, "install oversized VFS");
    CHECK(rexx_os_read_file_ex("oversize.rx", &loaded, &length) == REXX_OS_IO_TOO_LARGE,
          "source size limit enforced before allocation");
    CHECK(vfs.read_calls == 0u && vfs.close_calls == 1u && loaded == NULL,
          "oversize path closes without reading");

    memset(&vfs, 0, sizeof(vfs));
    vfs.data = "data";
    vfs.length = 4u;
    vfs.close_status = REXX_OS_IO_CLOSE_FAILED;
    CHECK(install_vfs(&vfs) == REXX_OS_IO_OK, "install close-failure VFS");
    CHECK(rexx_os_read_file_ex("close-error.rx", &loaded, &length) == REXX_OS_IO_CLOSE_FAILED,
          "close error is reported");
    CHECK(vfs.close_calls == 1u && loaded == NULL && length == 0u,
          "close failure discards loaded data");
    CHECK(rexx_os_set_vfs_operations(NULL, NULL) == REXX_OS_IO_OK,
          "restore default VFS");
    return 1;
}

static int run_cli(FakeVFS *vfs,
                   Capture *capture,
                   size_t stdout_limit,
                   int argc)
{
    char *argv[] = {"rexx", "fixture.rx", NULL};
    RexxOutputHost host = capture_host(capture, stdout_limit, 1024u);
    if (install_vfs(vfs) != REXX_OS_IO_OK) return 99;
    return guide_rexx_main_with_output(argc, argv, &host);
}

static int test_cli_output_routing(void)
{
    static const char script[] = "say \"native\"\nsay 4 + 5\n";
    static const char syntax_error[] = "if then say \"bad\"\n";
    static const char nul_script[] = {'s','a','y',' ', '\0','x'};
    FakeVFS vfs = {0};
    Capture first = {0};
    Capture second = {0};
    Capture syntax = {0};
    Capture nul = {0};
    Capture limited = {0};
    Capture broken = {0};
    Capture budget = {0};
    int result;

    vfs.data = script;
    vfs.length = sizeof(script) - 1u;
    result = run_cli(&vfs, &first, 0u, 2);
    CHECK(result == 0, "CLI executes through VFS");
    CHECK(strcmp(first.stdout_bytes, "native\n9\n") == 0 &&
          first.stderr_length == 0u, "SAY routes to invocation stdout");
    CHECK(vfs.close_calls == 1u && first.stdout_flushes != 0u,
          "CLI closes script and flushes stdout");

    vfs = (FakeVFS){0};
    vfs.data = "say \"second\"\n";
    vfs.length = strlen(vfs.data);
    result = run_cli(&vfs, &second, 0u, 2);
    CHECK(result == 0 && strcmp(second.stdout_bytes, "second\n") == 0,
          "second invocation has independent output context");
    CHECK(strcmp(first.stdout_bytes, "native\n9\n") == 0,
          "later invocation does not alter prior output");

    vfs = (FakeVFS){0};
    vfs.data = syntax_error;
    vfs.length = sizeof(syntax_error) - 1u;
    result = run_cli(&vfs, &syntax, 0u, 2);
    CHECK(result == 1 && syntax.stdout_length == 0u,
          "syntax error returns failure without stdout");
    CHECK(strstr(syntax.stderr_bytes, "syntax error") != NULL &&
          vfs.close_calls == 1u, "diagnostic routes to stderr and closes VFS");

    vfs = (FakeVFS){0};
    vfs.data = nul_script;
    vfs.length = sizeof(nul_script);
    result = run_cli(&vfs, &nul, 0u, 2);
    CHECK(result == 1 && strstr(nul.stderr_bytes, "NUL byte") != NULL,
          "embedded NUL script is rejected");
    CHECK(vfs.close_calls == 1u && nul.stdout_length == 0u,
          "embedded NUL rejection closes without execution");

    vfs = (FakeVFS){0};
    vfs.data = "say \"long\"\n";
    vfs.length = strlen(vfs.data);
    result = run_cli(&vfs, &limited, 3u, 2);
    CHECK(result == 1 && limited.stdout_length == 0u,
          "stdout limit converts overflow to runtime failure");
    CHECK(strstr(limited.stderr_bytes, "runtime error") != NULL,
          "output limit failure reports diagnostic to stderr");

    vfs = (FakeVFS){0};
    vfs.data = "say \"broken\"\n";
    vfs.length = strlen(vfs.data);
    broken.fail_stdout = 1;
    result = run_cli(&vfs, &broken, 0u, 2);
    CHECK(result == 1 && broken.stderr_length != 0u && vfs.close_calls == 1u,
          "failed host writer returns error and still closes script");

    vfs = (FakeVFS){0};
    vfs.data = "do forever\nend\n";
    vfs.length = strlen(vfs.data);
    result = run_cli(&vfs, &budget, 0u, 2);
    CHECK(result == 1 &&
          strstr(budget.stderr_bytes, "instruction budget exhausted") != NULL,
          "CLI instruction budget stops a non-terminating loop");
    CHECK(vfs.close_calls == 1u,
          "instruction budget failure releases the source handle");

    CHECK(rexx_os_set_vfs_operations(NULL, NULL) == REXX_OS_IO_OK,
          "restore default VFS after CLI tests");
    return 1;
}

int main(void)
{
    if (!test_file_load_partial_reads() ||
        !test_file_load_failures() ||
        !test_cli_output_routing()) return 1;
    printf("RX2 hosted integration assertions passed: %d\n", test_count);
    return 0;
}
