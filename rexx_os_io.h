#ifndef GUIDEXOS_REXX_OS_IO_H
#define GUIDEXOS_REXX_OS_IO_H

/*
 * Operating-system boundary for the native guideXOS REXX interpreter.
 *
 * Kernel builds use the C-linkage VFS primitives declared below. Hosted
 * builds use the C standard library unless an embedding application installs
 * its own VFS operations. Install custom operations once during startup and
 * leave them unchanged while REXX is executing.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REXX_OS_MAX_SOURCE_BYTES ((size_t)16777216u)

typedef enum RexxOSIOStatus {
    REXX_OS_IO_OK = 0,
    REXX_OS_IO_INVALID_ARGUMENT,
    REXX_OS_IO_NOT_FOUND,
    REXX_OS_IO_OPEN_FAILED,
    REXX_OS_IO_READ_FAILED,
    REXX_OS_IO_TOO_LARGE,
    REXX_OS_IO_OUT_OF_MEMORY,
    REXX_OS_IO_BACKEND_UNAVAILABLE,
    REXX_OS_IO_CLOSE_FAILED
} RexxOSIOStatus;

/*
 * VFS operations supplied by an embedding kernel or shell. The callbacks
 * return REXX_OS_IO_OK on success. A successful read may return fewer bytes
 * than requested; zero bytes before the advertised file size is an error.
 */
typedef struct RexxOSVFSOperations {
    RexxOSIOStatus (*open)(void *context,
                           const char *path,
                           void **out_handle);
    RexxOSIOStatus (*size)(void *context,
                           void *handle,
                           uint64_t *out_size);
    RexxOSIOStatus (*read)(void *context,
                           void *handle,
                           void *buffer,
                           uint32_t requested,
                           uint32_t *out_read);
    RexxOSIOStatus (*close)(void *context, void *handle);
} RexxOSVFSOperations;

/*
 * Install custom VFS operations. Passing NULL restores the platform default:
 * native guideXOS VFS in GXOS_BARE_METAL builds, hosted stdio otherwise.
 */
RexxOSIOStatus rexx_os_set_vfs_operations(
    const RexxOSVFSOperations *operations,
    void *context);

/*
 * Status-returning loader. On failure, *out_buffer is NULL and *out_size is
 * zero. On success, the allocation is NUL terminated and *out_size excludes
 * that terminator. Release it with rexx_os_free_file().
 */
RexxOSIOStatus rexx_os_read_file_ex(const char *filepath,
                                    char **out_buffer,
                                    size_t *out_size);

/* Convenience API requested by the interpreter integration. NULL means an
 * error; use rexx_os_read_file_ex() when the precise error code is needed. */
char *rexx_os_read_file(const char *filepath, size_t *out_size);
void rexx_os_free_file(char *buffer);
const char *rexx_os_io_status_name(RexxOSIOStatus status);

/*
 * ADDRESS dispatch hook. The environment and command are borrowed,
 * length-delimited slices valid only for the duration of the call.
 * Passing NULL to the registration restores the safe RC 127 stub.
 */
typedef int32_t (*RexxOSAddressDispatcher)(void *user_data,
                                          const char *environment,
                                          size_t environment_length,
                                          const char *command,
                                          size_t command_length);

void rexx_os_set_address_dispatcher(RexxOSAddressDispatcher dispatcher,
                                    void *user_data);
int32_t rexx_os_dispatch_address(const char *environment,
                                 size_t environment_length,
                                 const char *command,
                                 size_t command_length);

/* Adapter with the signature consumed by RexxVM's extended ADDRESS hook. */
int32_t rexx_os_vm_address_dispatcher(void *user_data,
                                      const char *environment,
                                      size_t environment_length,
                                      const char *command,
                                      size_t command_length);

/* Shell-callable REXX entry point; its definition is in rexx_cli.c. */
int guide_rexx_main(int argc, char **argv);

/* C ABI used by rexx_os_io.c to reach kernel::vfs from a C translation unit. */
#if defined(GXOS_BARE_METAL)
int32_t guidexos_rexx_vfs_open(const char *path, uint8_t *out_handle);
int64_t guidexos_rexx_vfs_size(uint8_t handle);
int32_t guidexos_rexx_vfs_read(uint8_t handle,
                               void *buffer,
                               uint32_t requested);
int32_t guidexos_rexx_vfs_close(uint8_t handle);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GUIDEXOS_REXX_OS_IO_H */
