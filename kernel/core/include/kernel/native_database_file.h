#ifndef KERNEL_NATIVE_DATABASE_FILE_H
#define KERNEL_NATIVE_DATABASE_FILE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t GxDbFileHandle;

typedef enum GxDbFileStatus {
    GX_DB_FILE_OK = 0,
    GX_DB_FILE_NOT_FOUND,
    GX_DB_FILE_ALREADY_EXISTS,
    GX_DB_FILE_INVALID_ARGUMENT,
    GX_DB_FILE_INVALID_NAME,
    GX_DB_FILE_STALE_HANDLE,
    GX_DB_FILE_IS_DIRECTORY,
    GX_DB_FILE_READ_ONLY,
    GX_DB_FILE_NO_SPACE,
    GX_DB_FILE_NOT_SUPPORTED,
    GX_DB_FILE_RESOURCE_LIMIT,
    GX_DB_FILE_BUSY,
    GX_DB_FILE_CORRUPT,
    GX_DB_FILE_SHORT_READ,
    GX_DB_FILE_SHORT_WRITE,
    GX_DB_FILE_IO_TIMEOUT,
    GX_DB_FILE_IO,
} GxDbFileStatus;

typedef enum GxDbFileOpenMode {
    GX_DB_OPEN_READ_ONLY = 0,
    GX_DB_OPEN_READ_WRITE = 1,
    GX_DB_OPEN_CREATE = 2,
} GxDbFileOpenMode;

typedef enum GxDbNativeFileType {
    GX_DB_NATIVE_FILE_UNKNOWN = 0,
    GX_DB_NATIVE_FILE_REGULAR = 1,
    GX_DB_NATIVE_FILE_DIRECTORY = 2,
} GxDbNativeFileType;

// Narrow VFS operations table. Production defaults call the kernel VFS; the
// table also lets the hosted contract harness exercise this exact bridge.
typedef struct GxDbNativeFileOps {
    int32_t (*open)(const char* path, uint16_t flags);
    int32_t (*close)(uint8_t handle);
    int32_t (*read)(uint8_t handle, void* buffer, uint32_t length);
    int32_t (*write)(uint8_t handle, const void* buffer, uint32_t length);
    int32_t (*seek)(uint8_t handle, int64_t offset, uint8_t origin);
    int64_t (*tell)(uint8_t handle);
    int64_t (*file_size)(uint8_t handle);
    int32_t (*flush)(uint8_t handle);
    int32_t (*truncate_shrink)(uint8_t handle, uint64_t size);
    int32_t (*stat)(const char* path, uint32_t* type, uint64_t* size);
    int32_t (*unlink)(const char* path);
} GxDbNativeFileOps;

#define GX_DB_FILE_INVALID_HANDLE ((GxDbFileHandle)0xFFFFFFFFu)

// Bind null to restore the production guideXOS VFS operation table.
void gx_db_file_bind_ops(const GxDbNativeFileOps* ops);

// Maps `/DIR/NAME.gxdb` to `/DIR/NAME.GDB` and the matching `.gxwal` to
// `.GWL`. Parent components are 1-8 ASCII letters/digits/underscores; the
// database stem has the same constraint. Parent/mount spelling is preserved
// for VFS routing; FAT short-name lookup is case-insensitive.
GxDbFileStatus gx_db_file_map_path(const char* logicalPath, char* outPath,
                                   size_t outPathSize);

GxDbFileStatus gx_db_file_open(const char* logicalPath, GxDbFileOpenMode mode,
                               uint8_t truncateExisting, GxDbFileHandle* outHandle);
GxDbFileStatus gx_db_file_get_size(GxDbFileHandle handle, uint64_t* outSize);
GxDbFileStatus gx_db_file_read_at(GxDbFileHandle handle, uint64_t offset,
                                 void* buffer, size_t length);
GxDbFileStatus gx_db_file_write_at(GxDbFileHandle handle, uint64_t offset,
                                  const void* buffer, size_t length);
GxDbFileStatus gx_db_file_truncate_to(GxDbFileHandle handle, uint64_t size);
GxDbFileStatus gx_db_file_flush(GxDbFileHandle handle);
GxDbFileStatus gx_db_file_close(GxDbFileHandle handle);
GxDbFileStatus gx_db_file_exists(const char* logicalPath, uint8_t* outExists);
GxDbFileStatus gx_db_file_remove(const char* logicalPath);

#ifdef __cplusplus
}
#endif

#endif
