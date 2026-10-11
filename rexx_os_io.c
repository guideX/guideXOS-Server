/*
 * C11 operating-system I/O boundary for guideXOS REXX.
 *
 * The loader owns the VFS handle and its result buffer for the complete
 * operation. All failure paths close an opened handle and free partial data.
 * Kernel builds call the small C ABI shim implemented alongside the kernel
 * VFS; hosted builds retain a standard-C file fallback for standalone use.
 */

#include "rexx_os_io.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if !defined(GXOS_BARE_METAL)
#include <errno.h>
#include <stdio.h>
#endif

static RexxOSVFSOperations s_custom_vfs_operations;
static void *s_custom_vfs_context;
static int s_has_custom_vfs_operations;

static RexxOSAddressDispatcher s_address_dispatcher;
static void *s_address_dispatcher_user_data;

#if defined(GXOS_BARE_METAL)
typedef struct RexxNativeVFSHandle {
    uint8_t handle;
} RexxNativeVFSHandle;

static RexxOSIOStatus rexx_os_native_open(void *context,
                                         const char *path,
                                         void **out_handle)
{
    RexxNativeVFSHandle *native_handle;
    uint8_t vfs_handle = UINT8_MAX;
    int32_t status;

    (void)context;
    if (path == NULL || out_handle == NULL) {
        return REXX_OS_IO_INVALID_ARGUMENT;
    }
    *out_handle = NULL;

    status = guidexos_rexx_vfs_open(path, &vfs_handle);
    if (status != 0) {
        return status == -1 ? REXX_OS_IO_NOT_FOUND : REXX_OS_IO_OPEN_FAILED;
    }

    native_handle = (RexxNativeVFSHandle *)malloc(sizeof(*native_handle));
    if (native_handle == NULL) {
        (void)guidexos_rexx_vfs_close(vfs_handle);
        return REXX_OS_IO_OUT_OF_MEMORY;
    }
    native_handle->handle = vfs_handle;
    *out_handle = native_handle;
    return REXX_OS_IO_OK;
}

static RexxOSIOStatus rexx_os_native_size(void *context,
                                          void *handle,
                                          uint64_t *out_size)
{
    RexxNativeVFSHandle *native_handle = (RexxNativeVFSHandle *)handle;
    int64_t size;

    (void)context;
    if (native_handle == NULL || out_size == NULL) {
        return REXX_OS_IO_INVALID_ARGUMENT;
    }
    size = guidexos_rexx_vfs_size(native_handle->handle);
    if (size < 0) {
        return REXX_OS_IO_READ_FAILED;
    }
    *out_size = (uint64_t)size;
    return REXX_OS_IO_OK;
}

static RexxOSIOStatus rexx_os_native_read(void *context,
                                         void *handle,
                                         void *buffer,
                                         uint32_t requested,
                                         uint32_t *out_read)
{
    RexxNativeVFSHandle *native_handle = (RexxNativeVFSHandle *)handle;
    int32_t count;

    (void)context;
    if (native_handle == NULL || buffer == NULL || out_read == NULL ||
        requested == 0u) {
        return REXX_OS_IO_INVALID_ARGUMENT;
    }
    *out_read = 0u;
    count = guidexos_rexx_vfs_read(native_handle->handle, buffer, requested);
    if (count < 0 || (uint32_t)count > requested) {
        return REXX_OS_IO_READ_FAILED;
    }
    *out_read = (uint32_t)count;
    return REXX_OS_IO_OK;
}

static RexxOSIOStatus rexx_os_native_close(void *context, void *handle)
{
    RexxNativeVFSHandle *native_handle = (RexxNativeVFSHandle *)handle;
    int32_t status;

    (void)context;
    if (native_handle == NULL) {
        return REXX_OS_IO_INVALID_ARGUMENT;
    }
    status = guidexos_rexx_vfs_close(native_handle->handle);
    free(native_handle);
    return status == 0 ? REXX_OS_IO_OK : REXX_OS_IO_CLOSE_FAILED;
}

static const RexxOSVFSOperations *rexx_os_default_vfs_operations(void)
{
    static const RexxOSVFSOperations operations = {
        rexx_os_native_open,
        rexx_os_native_size,
        rexx_os_native_read,
        rexx_os_native_close
    };

    return &operations;
}
#else
static RexxOSIOStatus rexx_os_host_open(void *context,
                                        const char *path,
                                        void **out_handle)
{
    FILE *file;

    (void)context;
    if (path == NULL || out_handle == NULL) {
        return REXX_OS_IO_INVALID_ARGUMENT;
    }
    *out_handle = NULL;
    errno = 0;
    file = fopen(path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? REXX_OS_IO_NOT_FOUND
                               : REXX_OS_IO_OPEN_FAILED;
    }
    *out_handle = file;
    return REXX_OS_IO_OK;
}

static RexxOSIOStatus rexx_os_host_size(void *context,
                                        void *handle,
                                        uint64_t *out_size)
{
    FILE *file = (FILE *)handle;
    long end_position;

    (void)context;
    if (file == NULL || out_size == NULL) {
        return REXX_OS_IO_INVALID_ARGUMENT;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        return REXX_OS_IO_READ_FAILED;
    }
    end_position = ftell(file);
    if (end_position < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        return REXX_OS_IO_READ_FAILED;
    }
    *out_size = (uint64_t)(unsigned long)end_position;
    return REXX_OS_IO_OK;
}

static RexxOSIOStatus rexx_os_host_read(void *context,
                                        void *handle,
                                        void *buffer,
                                        uint32_t requested,
                                        uint32_t *out_read)
{
    FILE *file = (FILE *)handle;
    size_t count;

    (void)context;
    if (file == NULL || buffer == NULL || out_read == NULL ||
        requested == 0u) {
        return REXX_OS_IO_INVALID_ARGUMENT;
    }
    *out_read = 0u;
    count = fread(buffer, 1u, (size_t)requested, file);
    if (ferror(file)) {
        return REXX_OS_IO_READ_FAILED;
    }
    *out_read = (uint32_t)count;
    return REXX_OS_IO_OK;
}

static RexxOSIOStatus rexx_os_host_close(void *context, void *handle)
{
    (void)context;
    if (handle == NULL) {
        return REXX_OS_IO_INVALID_ARGUMENT;
    }
    return fclose((FILE *)handle) == 0 ? REXX_OS_IO_OK
                                      : REXX_OS_IO_CLOSE_FAILED;
}

static const RexxOSVFSOperations *rexx_os_default_vfs_operations(void)
{
    static const RexxOSVFSOperations operations = {
        rexx_os_host_open,
        rexx_os_host_size,
        rexx_os_host_read,
        rexx_os_host_close
    };

    return &operations;
}
#endif

RexxOSIOStatus rexx_os_set_vfs_operations(
    const RexxOSVFSOperations *operations,
    void *context)
{
    if (operations == NULL) {
        memset(&s_custom_vfs_operations, 0, sizeof(s_custom_vfs_operations));
        s_custom_vfs_context = NULL;
        s_has_custom_vfs_operations = 0;
        return REXX_OS_IO_OK;
    }

    if (operations->open == NULL || operations->size == NULL ||
        operations->read == NULL || operations->close == NULL) {
        return REXX_OS_IO_INVALID_ARGUMENT;
    }

    s_custom_vfs_operations = *operations;
    s_custom_vfs_context = context;
    s_has_custom_vfs_operations = 1;
    return REXX_OS_IO_OK;
}

RexxOSIOStatus rexx_os_read_file_ex(const char *filepath,
                                    char **out_buffer,
                                    size_t *out_size)
{
    const RexxOSVFSOperations *operations;
    void *context;
    void *handle = NULL;
    char *buffer = NULL;
    uint64_t file_size = 0u;
    size_t offset = 0u;
    RexxOSIOStatus status;
    RexxOSIOStatus close_status;

    if (out_buffer != NULL) {
        *out_buffer = NULL;
    }
    if (out_size != NULL) {
        *out_size = 0u;
    }
    if (out_buffer == NULL || out_size == NULL) {
        return REXX_OS_IO_INVALID_ARGUMENT;
    }
    if (filepath == NULL || filepath[0] == '\0') {
        return REXX_OS_IO_INVALID_ARGUMENT;
    }

    operations = s_has_custom_vfs_operations
                     ? &s_custom_vfs_operations
                     : rexx_os_default_vfs_operations();
    context = s_has_custom_vfs_operations ? s_custom_vfs_context : NULL;

    status = operations->open(context, filepath, &handle);
    if (status != REXX_OS_IO_OK) {
        if (handle != NULL) {
            (void)operations->close(context, handle);
        }
        return status;
    }
    if (handle == NULL) {
        return REXX_OS_IO_OPEN_FAILED;
    }

    status = operations->size(context, handle, &file_size);
    if (status != REXX_OS_IO_OK) {
        goto cleanup;
    }
    if (file_size > (uint64_t)REXX_OS_MAX_SOURCE_BYTES ||
        file_size >= (uint64_t)SIZE_MAX) {
        status = REXX_OS_IO_TOO_LARGE;
        goto cleanup;
    }

    buffer = (char *)malloc((size_t)file_size + 1u);
    if (buffer == NULL) {
        status = REXX_OS_IO_OUT_OF_MEMORY;
        goto cleanup;
    }

    while ((uint64_t)offset < file_size) {
        uint64_t remaining = file_size - (uint64_t)offset;
        uint32_t requested = remaining > (uint64_t)UINT32_MAX
                                 ? UINT32_MAX : (uint32_t)remaining;
        uint32_t bytes_read = 0u;

        status = operations->read(context, handle, buffer + offset,
                                  requested, &bytes_read);
        if (status != REXX_OS_IO_OK) {
            goto cleanup;
        }
        if (bytes_read == 0u || bytes_read > requested) {
            status = REXX_OS_IO_READ_FAILED;
            goto cleanup;
        }
        offset += (size_t)bytes_read;
    }
    buffer[(size_t)file_size] = '\0';

cleanup:
    close_status = operations->close(context, handle);
    if (status == REXX_OS_IO_OK && close_status != REXX_OS_IO_OK) {
        status = REXX_OS_IO_CLOSE_FAILED;
    }
    if (status != REXX_OS_IO_OK) {
        free(buffer);
        return status;
    }

    *out_buffer = buffer;
    *out_size = (size_t)file_size;
    return REXX_OS_IO_OK;
}

char *rexx_os_read_file(const char *filepath, size_t *out_size)
{
    char *buffer = NULL;
    size_t size = 0u;

    if (out_size != NULL) {
        *out_size = 0u;
    }
    if (rexx_os_read_file_ex(filepath, &buffer, &size) != REXX_OS_IO_OK) {
        return NULL;
    }
    if (out_size != NULL) {
        *out_size = size;
    }
    return buffer;
}

void rexx_os_free_file(char *buffer)
{
    free(buffer);
}

const char *rexx_os_io_status_name(RexxOSIOStatus status)
{
    switch (status) {
    case REXX_OS_IO_OK: return "ok";
    case REXX_OS_IO_INVALID_ARGUMENT: return "invalid argument";
    case REXX_OS_IO_NOT_FOUND: return "file not found";
    case REXX_OS_IO_OPEN_FAILED: return "cannot open file";
    case REXX_OS_IO_READ_FAILED: return "cannot read file";
    case REXX_OS_IO_TOO_LARGE: return "file exceeds the 16 MiB limit";
    case REXX_OS_IO_OUT_OF_MEMORY: return "out of memory";
    case REXX_OS_IO_BACKEND_UNAVAILABLE: return "VFS backend unavailable";
    case REXX_OS_IO_CLOSE_FAILED: return "cannot close file after reading";
    default: return "unknown I/O error";
    }
}

void rexx_os_set_address_dispatcher(RexxOSAddressDispatcher dispatcher,
                                    void *user_data)
{
    s_address_dispatcher = dispatcher;
    s_address_dispatcher_user_data = dispatcher != NULL ? user_data : NULL;
}

int32_t rexx_os_dispatch_address(const char *environment,
                                 size_t environment_length,
                                 const char *command,
                                 size_t command_length)
{
    if ((environment == NULL && environment_length != 0u) ||
        (command == NULL && command_length != 0u)) {
        return INT32_C(127);
    }
    if (s_address_dispatcher == NULL) {
        return INT32_C(127);
    }
    return s_address_dispatcher(s_address_dispatcher_user_data,
                               environment, environment_length,
                               command, command_length);
}

int32_t rexx_os_vm_address_dispatcher(void *user_data,
                                      const char *environment,
                                      size_t environment_length,
                                      const char *command,
                                      size_t command_length)
{
    (void)user_data;
    return rexx_os_dispatch_address(environment, environment_length,
                                    command, command_length);
}
