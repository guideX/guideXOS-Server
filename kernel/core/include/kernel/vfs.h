// Kernel Virtual File System (VFS) Layer
//
// Provides a unified interface for all filesystems:
//   - Mount point management
//   - Path resolution and normalization
//   - File and directory operations
//   - File handle management
//
// Supports multiple filesystem types:
//   - FAT32/exFAT (via fs_fat)
//   - ext2/ext4 (via fs_ext4)
//   - UFS (via fs_ufs)
//   - Future: ISO9660, NTFS, etc.
//
// Architecture-independent — works on all platforms.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include "kernel/types.h"
#include "kernel/block_device.h"

namespace kernel {
namespace vfs {

// ================================================================
// Constants
// ================================================================

static const size_t VFS_MAX_PATH = 256;
static const size_t VFS_MAX_FILENAME = 128;
static const size_t VFS_MAX_MOUNTS = 8;
static const size_t VFS_MAX_OPEN_FILES = 32;

// ================================================================
// Filesystem types
// ================================================================

enum FSType : uint8_t {
    FS_TYPE_NONE    = 0,
    FS_TYPE_FAT32   = 1,
    FS_TYPE_EXFAT   = 2,
    FS_TYPE_EXT2    = 3,
    FS_TYPE_EXT4    = 4,
    FS_TYPE_UFS     = 5,
    FS_TYPE_ISO9660 = 6,
    FS_TYPE_RAMDISK = 7,   // Simple flat file storage
};

// ================================================================
// File types
// ================================================================

enum FileType : uint8_t {
    FILE_TYPE_UNKNOWN   = 0,
    FILE_TYPE_REGULAR   = 1,
    FILE_TYPE_DIRECTORY = 2,
    FILE_TYPE_SYMLINK   = 3,
    FILE_TYPE_DEVICE    = 4,
};

// ================================================================
// Open mode flags
// ================================================================

enum OpenFlags : uint16_t {
    OPEN_READ      = 0x0001,
    OPEN_WRITE     = 0x0002,
    OPEN_APPEND    = 0x0004,
    OPEN_CREATE    = 0x0008,
    OPEN_TRUNCATE  = 0x0010,
    OPEN_EXCL      = 0x0020,   // Fail if exists (with CREATE)
    OPEN_RDWR      = OPEN_READ | OPEN_WRITE,
};

// ================================================================
// Seek origin
// ================================================================

enum SeekOrigin : uint8_t {
    SEEK_SET = 0,   // From beginning
    SEEK_CUR = 1,   // From current position
    SEEK_END = 2,   // From end
};

// ================================================================
// Status codes
// ================================================================

enum Status : int8_t {
    VFS_OK            =  0,
    VFS_ERR_NOT_FOUND = -1,
    VFS_ERR_EXISTS    = -2,
    VFS_ERR_NOT_DIR   = -3,
    VFS_ERR_IS_DIR    = -4,
    VFS_ERR_NOT_EMPTY = -5,
    VFS_ERR_NO_SPACE  = -6,
    VFS_ERR_READ_ONLY = -7,
    VFS_ERR_INVALID   = -8,
    VFS_ERR_IO        = -9,
    VFS_ERR_NOT_MOUNT = -10,
    VFS_ERR_BUSY      = -11,
    VFS_ERR_TOO_MANY  = -12,
    VFS_ERR_NOT_SUPPORTED = -13,
};

// ================================================================
// File/directory information
// ================================================================

struct FileInfo {
    char     name[VFS_MAX_FILENAME];
    FileType type;
    uint64_t size;
    uint32_t permissions;      // Unix-style mode bits
    uint16_t createDate;       // DOS date format
    uint16_t createTime;       // DOS time format
    uint16_t modifyDate;
    uint16_t modifyTime;
    uint16_t accessDate;
    uint16_t accessTime;
};

// ================================================================
// Directory entry (for iteration)
// ================================================================

struct DirEntry {
    char     name[VFS_MAX_FILENAME];
    FileType type;
    uint64_t size;
    bool     isHidden;
    bool     isSystem;
    bool     isReadOnly;
};

// ================================================================
// Mount point descriptor
// ================================================================

struct MountPoint {
    bool     active;
    char     path[VFS_MAX_PATH];      // Mount path (e.g., "/", "/mnt/usb")
    FSType   fsType;
    uint8_t  blockDevIndex;           // Block device index
    uint8_t  fsVolumeIndex;           // FS-specific volume index
    bool     readOnly;
};

// ================================================================
// File handle descriptor
// ================================================================

struct FileHandle {
    bool     open;
    uint8_t  mountIndex;              // Which mount point
    uint16_t flags;                   // Open flags
    uint64_t position;                // Current read/write position
    uint64_t size;                    // File size (cached)
    uint8_t  fsFileHandle;            // FS-specific handle
    char     path[VFS_MAX_PATH];      // Full path (for debugging)
};

// ================================================================
// Directory iterator
// ================================================================

struct DirIterator {
    bool     active;
    uint8_t  mountIndex;
    char     path[VFS_MAX_PATH];
    uint32_t index;                   // Current entry index
};

// ================================================================
// Public API — Initialization
// ================================================================

// Initialize the VFS layer (call once at boot).
void init();

// ================================================================
// Public API — Mount Management
// ================================================================

// Auto-detect and mount a filesystem on a block device.
// Returns mount index, or 0xFF on failure.
uint8_t mount(const char* path, uint8_t blockDevIndex);

// Mount with explicit filesystem type.
uint8_t mount_type(const char* path, uint8_t blockDevIndex, FSType fsType);

// Unmount a filesystem.
Status unmount(const char* path);

// Get mount point info by path.
const MountPoint* get_mount(const char* path);

// Get mount point info by index.
const MountPoint* get_mount_by_index(uint8_t index);

// Return number of active mounts.
uint8_t mount_count();

// ================================================================
// Public API — Path Operations
// ================================================================

// Normalize a path (resolve ., .., multiple slashes).
void normalize_path(const char* input, char* output, size_t outputSize);

// Get the parent directory of a path.
void parent_path(const char* path, char* output, size_t outputSize);

// Get the filename from a path.
const char* basename(const char* path);

// Check if a path is absolute (starts with /).
bool is_absolute(const char* path);

// Join two path components.
void join_path(const char* base, const char* name, char* output, size_t outputSize);

// ================================================================
// Public API — File Operations
// ================================================================

// Open a file.  Returns file handle index, or 0xFF on failure.
uint8_t open(const char* path, uint16_t flags);

// Close a file.
Status close(uint8_t handle);

// Read from a file.  Returns bytes read, or negative on error.
int32_t read(uint8_t handle, void* buffer, uint32_t size);

// Write to a file.  Returns bytes written, or negative on error.
int32_t write(uint8_t handle, const void* buffer, uint32_t size);

// Seek within a file.
Status seek(uint8_t handle, int64_t offset, SeekOrigin origin);

// Get current file position.
int64_t tell(uint8_t handle);

// Get file size.
int64_t file_size(uint8_t handle);

// Flush file buffers.
Status flush(uint8_t handle);

// Get file handle info.
const FileHandle* get_handle(uint8_t handle);

// ================================================================
// Public API — Directory Operations
// ================================================================

// Open a directory for iteration.
// Returns iterator index, or 0xFF on failure.
uint8_t opendir(const char* path);

// Read next directory entry.
// Returns true if entry was read, false if end of directory.
bool readdir(uint8_t iterator, DirEntry* entry);

// Close directory iterator.
void closedir(uint8_t iterator);

// Create a directory.
Status mkdir(const char* path);

// Remove an empty directory.
Status rmdir(const char* path);

// ================================================================
// Public API — File/Directory Management
// ================================================================

// Check if a path exists.
bool exists(const char* path);

// Get file/directory info.
Status stat(const char* path, FileInfo* info);

// Delete a file.
Status unlink(const char* path);

// Rename/move a file or directory.
Status rename(const char* oldPath, const char* newPath);

// ================================================================
// Public API — High-Level Convenience Functions
// ================================================================

// Read entire file into buffer.
// Returns bytes read, or negative on error.
int32_t read_file(const char* path, void* buffer, uint32_t maxSize);

// Write entire buffer to file (creates or truncates).
// Returns bytes written, or negative on error.
int32_t write_file(const char* path, const void* buffer, uint32_t size);

// Append data to a file.
int32_t append_file(const char* path, const void* buffer, uint32_t size);

// ================================================================
// Public API — Filesystem Information
// ================================================================

// Get total space on a mount point (in bytes).
uint64_t total_space(const char* mountPath);

// Get free space on a mount point (in bytes).
uint64_t free_space(const char* mountPath);

// Get filesystem type name as string.
const char* fs_type_name(FSType type);

} // namespace vfs
} // namespace kernel

#endif // KERNEL_VFS_H
