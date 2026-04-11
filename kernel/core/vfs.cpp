// Kernel Virtual File System (VFS) Layer — Implementation
//
// Provides unified filesystem access by delegating to the appropriate
// filesystem driver (FAT32, ext4, UFS) based on mount points.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/vfs.h"
#include "include/kernel/block_device.h"
#include "include/kernel/fs_fat.h"
#include "include/kernel/fs_ext4.h"

#if defined(__GNUC__) || defined(__clang__)
#include "include/kernel/serial_debug.h"
#endif

namespace kernel {
namespace vfs {

// ================================================================
// Internal state
// ================================================================

static MountPoint   s_mounts[VFS_MAX_MOUNTS];
static FileHandle   s_files[VFS_MAX_OPEN_FILES];
static DirIterator  s_dirs[VFS_MAX_OPEN_FILES];
static uint8_t      s_mountCount = 0;
static bool         s_initialized = false;

// ================================================================
// Helper functions
// ================================================================

static void memzero(void* dst, size_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0;
    }
}

// May be used for future file copying operations
#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static void memcopy(void* dst, const void* src, size_t len)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
}

static size_t strlen(const char* s)
{
    size_t len = 0;
    if (s) {
        while (s[len]) ++len;
    }
    return len;
}

static void strcopy(char* dst, const char* src, size_t maxLen)
{
    size_t i = 0;
    if (src) {
        while (src[i] && i < maxLen - 1) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static int strcmp(const char* s1, const char* s2)
{
    while (*s1 && (*s1 == *s2)) {
        ++s1;
        ++s2;
    }
    return static_cast<unsigned char>(*s1) - static_cast<unsigned char>(*s2);
}

static int strncmp(const char* s1, const char* s2, size_t n)
{
    while (n > 0 && *s1 && (*s1 == *s2)) {
        ++s1;
        ++s2;
        --n;
    }
    if (n == 0) return 0;
    return static_cast<unsigned char>(*s1) - static_cast<unsigned char>(*s2);
}

// Find the mount point that best matches a path
static MountPoint* find_mount_for_path(const char* path)
{
    if (!path) return nullptr;
    
    MountPoint* bestMatch = nullptr;
    size_t bestLen = 0;
    
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!s_mounts[i].active) continue;
        
        size_t mountLen = strlen(s_mounts[i].path);
        
        // Check if path starts with mount path
        if (strncmp(path, s_mounts[i].path, mountLen) == 0) {
            // Ensure we match at a path boundary
            if (path[mountLen] == '\0' || path[mountLen] == '/' || 
                mountLen == 1) {  // Root mount "/"
                if (mountLen > bestLen) {
                    bestMatch = &s_mounts[i];
                    bestLen = mountLen;
                }
            }
        }
    }
    
    return bestMatch;
}

// Get the path relative to a mount point
static const char* get_relative_path(const char* fullPath, const MountPoint* mount)
{
    if (!fullPath || !mount) return nullptr;
    
    size_t mountLen = strlen(mount->path);
    
    // Handle root mount
    if (mountLen == 1 && mount->path[0] == '/') {
        return fullPath;
    }
    
    // Skip the mount prefix
    const char* relative = fullPath + mountLen;
    
    // Ensure it starts with / or is empty
    if (*relative == '/') {
        return relative;
    } else if (*relative == '\0') {
        return "/";
    }
    
    return relative;
}

// Detect filesystem type from block device
static FSType detect_fs_type(uint8_t blockDevIndex)
{
    // Buffer large enough for UFS superblock detection (magic at offset 0x55C)
    uint8_t buffer[2048];
    
    // Read first sector
    if (block::read_sectors(blockDevIndex, 0, 1, buffer) != block::BLOCK_OK) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] detect_fs: Failed to read sector 0\n");
#endif
        return FS_TYPE_NONE;
    }
    
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[VFS] detect_fs: Read sector 0 OK\n");
    serial::puts("[VFS]   Bytes 510-511: 0x");
    serial::put_hex8(buffer[510]);
    serial::puts(" 0x");
    serial::put_hex8(buffer[511]);
    serial::puts("\n");
#endif
    
    // Check for boot sector signature (0x55AA)
    if (buffer[510] == 0x55 && buffer[511] == 0xAA) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS]   Found 0x55AA boot signature\n");
#endif
        
        // Check for FAT32 specific fields at offset 82
        if (buffer[82] == 'F' && buffer[83] == 'A' && buffer[84] == 'T' &&
            buffer[85] == '3' && buffer[86] == '2') {
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS]   Found FAT32 string at offset 82\n");
#endif
            return FS_TYPE_FAT32;
        }
        
        // Check for exFAT
        if (buffer[3] == 'E' && buffer[4] == 'X' && buffer[5] == 'F' &&
            buffer[6] == 'A' && buffer[7] == 'T') {
            return FS_TYPE_EXFAT;
        }
        
        // Check for FAT signature at offset 54 (FAT12/16) 
        if (buffer[54] == 'F' && buffer[55] == 'A' && buffer[56] == 'T') {
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS]   Found FAT string at offset 54\n");
#endif
            return FS_TYPE_FAT32;  // Treat as FAT32 for now
        }
        
        // Check for FAT32 at offset 82 with different format
        if (buffer[82] == 'F' && buffer[83] == 'A' && buffer[84] == 'T') {
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS]   Found FAT at offset 82 (partial)\n");
#endif
            return FS_TYPE_FAT32;
        }
        
        // Check BPB fields to identify FAT32 even without string
        // FAT32 has sectorsPerFAT16 = 0 (offset 22-23) and sectorsPerFAT32 > 0 (offset 36-39)
        uint16_t sectorsPerFAT16 = *reinterpret_cast<uint16_t*>(&buffer[22]);
        uint32_t sectorsPerFAT32 = *reinterpret_cast<uint32_t*>(&buffer[36]);
        uint16_t bytesPerSector = *reinterpret_cast<uint16_t*>(&buffer[11]);
        
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS]   BPB: bps=0x");
        serial::put_hex16(bytesPerSector);
        serial::puts(" spf16=0x");
        serial::put_hex16(sectorsPerFAT16);
        serial::puts(" spf32=0x");
        serial::put_hex32(sectorsPerFAT32);
        serial::puts("\n");
#endif
        
        if (bytesPerSector == 512 && sectorsPerFAT16 == 0 && sectorsPerFAT32 > 0) {
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS]   Detected FAT32 from BPB fields\n");
#endif
            return FS_TYPE_FAT32;
        }
        
        // Could be MBR with partition table - check first partition entry
        uint8_t partType = buffer[446 + 4];
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS]   MBR partition type: 0x");
        serial::put_hex8(partType);
        serial::puts("\n");
#endif
        
        if (partType == 0x0B || partType == 0x0C) {
            // FAT32 partition - get start LBA
            uint32_t startLBA = *reinterpret_cast<uint32_t*>(&buffer[446 + 8]);
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS]   FAT32 partition at LBA 0x");
            serial::put_hex32(startLBA);
            serial::puts(" - partitions not supported\n");
#endif
            return FS_TYPE_NONE;
        }
    }
    
    // Check for ext2/ext4 (superblock at offset 1024)
    if (block::read_sectors(blockDevIndex, 2, 1, buffer) == block::BLOCK_OK) {
        // ext2/ext4 magic number at offset 56 in superblock
        if (buffer[56] == 0x53 && buffer[57] == 0xEF) {
            // Check for ext4 features
            uint32_t incompat = *reinterpret_cast<uint32_t*>(&buffer[96]);
            if (incompat & 0x40) {  // EXTENTS feature
                return FS_TYPE_EXT4;
            }
            return FS_TYPE_EXT2;
        }
    }
    
    // Check for UFS (various magic locations)
    // UFS superblock is at offset 8192 (sector 16), magic at offset 0x55C within superblock
    // Need to read 4 sectors (2048 bytes) to reach the magic location
    if (block::read_sectors(blockDevIndex, 16, 4, buffer) == block::BLOCK_OK) {
        uint32_t magic = *reinterpret_cast<uint32_t*>(&buffer[0x55C]);
        if (magic == 0x00011954 || magic == 0x54190100) {  // UFS1/UFS2
            return FS_TYPE_UFS;
        }
    }
    
    return FS_TYPE_NONE;
}

// ================================================================
// Public API — Initialization
// ================================================================

void init()
{
    if (s_initialized) return;
    
    memzero(s_mounts, sizeof(s_mounts));
    memzero(s_files, sizeof(s_files));
    memzero(s_dirs, sizeof(s_dirs));
    s_mountCount = 0;
    s_initialized = true;
    
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[VFS] Initialized\n");
#endif
}

// ================================================================
// Public API — Mount Management
// ================================================================

uint8_t mount(const char* path, uint8_t blockDevIndex)
{
    FSType fsType = detect_fs_type(blockDevIndex);
    if (fsType == FS_TYPE_NONE) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] ERROR: Could not detect filesystem type\n");
#endif
        return 0xFF;
    }
    
    return mount_type(path, blockDevIndex, fsType);
}

uint8_t mount_type(const char* path, uint8_t blockDevIndex, FSType fsType)
{
    if (!s_initialized) {
        init();
    }
    
    if (!path || strlen(path) == 0) {
        return 0xFF;
    }
    
    // Check if path is already mounted
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0) {
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS] ERROR: Path already mounted\n");
#endif
            return 0xFF;
        }
    }
    
    // Find free mount slot
    uint8_t index = 0xFF;
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!s_mounts[i].active) {
            index = i;
            break;
        }
    }
    
    if (index == 0xFF) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] ERROR: No free mount slots\n");
#endif
        return 0xFF;
    }
    
    // Mount the filesystem
    uint8_t fsVolume = 0xFF;
    
    switch (fsType) {
        case FS_TYPE_FAT32:
        case FS_TYPE_EXFAT:
            fsVolume = fs_fat::mount(blockDevIndex);
            break;
            
        case FS_TYPE_EXT2:
        case FS_TYPE_EXT4:
            fsVolume = fs_ext4::mount(blockDevIndex);
            break;
            
        case FS_TYPE_UFS:
            // fsVolume = fs_ufs::mount(blockDevIndex);
            // UFS mount not implemented in this version
            break;
            
        default:
            break;
    }
    
    if (fsVolume == 0xFF) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] ERROR: Filesystem mount failed\n");
#endif
        return 0xFF;
    }
    
    // Initialize mount point
    MountPoint& mp = s_mounts[index];
    mp.active = true;
    strcopy(mp.path, path, sizeof(mp.path));
    mp.fsType = fsType;
    mp.blockDevIndex = blockDevIndex;
    mp.fsVolumeIndex = fsVolume;
    mp.readOnly = false;
    
    ++s_mountCount;
    
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[VFS] Mounted ");
    serial::puts(fs_type_name(fsType));
    serial::puts(" at '");
    serial::puts(path);
    serial::puts("'\n");
#endif
    
    return index;
}

Status unmount(const char* path)
{
    if (!path) return VFS_ERR_INVALID;
    
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0) {
            // Check for open files on this mount
            for (uint8_t j = 0; j < VFS_MAX_OPEN_FILES; ++j) {
                if (s_files[j].open && s_files[j].mountIndex == i) {
                    return VFS_ERR_BUSY;
                }
            }
            
            // Unmount the filesystem
            switch (s_mounts[i].fsType) {
                case FS_TYPE_FAT32:
                case FS_TYPE_EXFAT:
                    fs_fat::unmount(s_mounts[i].fsVolumeIndex);
                    break;
                    
                case FS_TYPE_EXT2:
                case FS_TYPE_EXT4:
                    fs_ext4::unmount(s_mounts[i].fsVolumeIndex);
                    break;
                    
                default:
                    break;
            }
            
            s_mounts[i].active = false;
            if (s_mountCount > 0) --s_mountCount;
            
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS] Unmounted '");
            serial::puts(path);
            serial::puts("'\n");
#endif
            
            return VFS_OK;
        }
    }
    
    return VFS_ERR_NOT_MOUNT;
}

const MountPoint* get_mount(const char* path)
{
    return find_mount_for_path(path);
}

const MountPoint* get_mount_by_index(uint8_t index)
{
    if (index >= VFS_MAX_MOUNTS) return nullptr;
    if (!s_mounts[index].active) return nullptr;
    return &s_mounts[index];
}

uint8_t mount_count()
{
    return s_mountCount;
}

// ================================================================
// Public API — Path Operations
// ================================================================

void normalize_path(const char* input, char* output, size_t outputSize)
{
    if (!input || !output || outputSize == 0) return;
    
    size_t outIdx = 0;
    size_t inIdx = 0;
    size_t inputLen = strlen(input);
    
    // Handle empty path
    if (inputLen == 0) {
        output[0] = '/';
        output[1] = '\0';
        return;
    }
    
    // Process each character
    while (inIdx < inputLen && outIdx < outputSize - 1) {
        char c = input[inIdx];
        
        // Skip multiple slashes
        if (c == '/' && outIdx > 0 && output[outIdx - 1] == '/') {
            ++inIdx;
            continue;
        }
        
        // Handle . and ..
        if (c == '.' && (inIdx == 0 || input[inIdx - 1] == '/')) {
            // Check for ..
            if (inIdx + 1 < inputLen && input[inIdx + 1] == '.') {
                if (inIdx + 2 >= inputLen || input[inIdx + 2] == '/') {
                    // Go up one directory
                    if (outIdx > 1) {
                        --outIdx;  // Remove trailing slash
                        while (outIdx > 0 && output[outIdx - 1] != '/') {
                            --outIdx;
                        }
                    }
                    inIdx += 2;
                    if (inIdx < inputLen && input[inIdx] == '/') ++inIdx;
                    continue;
                }
            }
            // Check for single .
            else if (inIdx + 1 >= inputLen || input[inIdx + 1] == '/') {
                ++inIdx;
                if (inIdx < inputLen && input[inIdx] == '/') ++inIdx;
                continue;
            }
        }
        
        output[outIdx++] = c;
        ++inIdx;
    }
    
    // Ensure path starts with /
    if (outIdx == 0 || output[0] != '/') {
        // Shift everything right and add /
        if (outIdx < outputSize - 1) {
            for (size_t i = outIdx; i > 0; --i) {
                output[i] = output[i - 1];
            }
            output[0] = '/';
            ++outIdx;
        }
    }
    
    // Remove trailing slash (except for root)
    if (outIdx > 1 && output[outIdx - 1] == '/') {
        --outIdx;
    }
    
    output[outIdx] = '\0';
}

void parent_path(const char* path, char* output, size_t outputSize)
{
    if (!path || !output || outputSize == 0) return;
    
    size_t pathLen = strlen(path);
    
    // Handle root
    if (pathLen <= 1) {
        output[0] = '/';
        output[1] = '\0';
        return;
    }
    
    // Find last slash (excluding trailing)
    size_t lastSlash = pathLen - 1;
    if (path[lastSlash] == '/') --lastSlash;
    
    while (lastSlash > 0 && path[lastSlash] != '/') {
        --lastSlash;
    }
    
    // Copy up to last slash
    size_t copyLen = (lastSlash == 0) ? 1 : lastSlash;
    if (copyLen >= outputSize) copyLen = outputSize - 1;
    
    for (size_t i = 0; i < copyLen; ++i) {
        output[i] = path[i];
    }
    output[copyLen] = '\0';
}

const char* basename(const char* path)
{
    if (!path) return nullptr;
    
    size_t pathLen = strlen(path);
    if (pathLen == 0) return path;
    
    // Find last slash
    size_t lastSlash = pathLen;
    while (lastSlash > 0 && path[lastSlash - 1] != '/') {
        --lastSlash;
    }
    
    return path + lastSlash;
}

bool is_absolute(const char* path)
{
    return path && path[0] == '/';
}

void join_path(const char* base, const char* name, char* output, size_t outputSize)
{
    if (!output || outputSize == 0) return;
    
    size_t outIdx = 0;
    
    // Copy base
    if (base) {
        while (*base && outIdx < outputSize - 1) {
            output[outIdx++] = *base++;
        }
    }
    
    // Add separator if needed
    if (outIdx > 0 && output[outIdx - 1] != '/' && outIdx < outputSize - 1) {
        output[outIdx++] = '/';
    }
    
    // Copy name (skip leading slash)
    if (name) {
        if (*name == '/') ++name;
        while (*name && outIdx < outputSize - 1) {
            output[outIdx++] = *name++;
        }
    }
    
    output[outIdx] = '\0';
}

// ================================================================
// Public API — File Operations
// ================================================================

uint8_t open(const char* path, uint16_t flags)
{
    if (!path) return 0xFF;
    
    MountPoint* mount = find_mount_for_path(path);
    if (!mount) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] ERROR: No mount point for path: ");
        serial::puts(path);
        serial::puts("\n");
#endif
        return 0xFF;
    }
    
    // Find free file handle
    uint8_t handle = 0xFF;
    for (uint8_t i = 0; i < VFS_MAX_OPEN_FILES; ++i) {
        if (!s_files[i].open) {
            handle = i;
            break;
        }
    }
    
    if (handle == 0xFF) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] ERROR: No free file handles\n");
#endif
        return 0xFF;
    }
    
    // Get path relative to mount point
    const char* relPath = get_relative_path(path, mount);
    
    // Open via filesystem driver
    uint8_t fsHandle = 0xFF;
    uint64_t fileSize = 0;
    bool found = false;
    
    switch (mount->fsType) {
        case FS_TYPE_FAT32:
        case FS_TYPE_EXFAT: {
            // Use lookup_path to find the file
            fs_fat::DirEntry entry;
            if (fs_fat::lookup_path(mount->fsVolumeIndex, relPath, &entry)) {
                // Check if it's a directory when we want a file
                if (entry.isDir && (flags & OPEN_WRITE)) {
                    // Cannot open directory for writing
#if defined(__GNUC__) || defined(__clang__)
                    serial::puts("[VFS] ERROR: Cannot open directory for writing\n");
#endif
                    return 0xFF;
                }
                
                // Open the file
                fsHandle = fs_fat::open_file(mount->fsVolumeIndex,
                                             entry.firstCluster,
                                             entry.fileSize,
                                             entry.attr);
                if (fsHandle != 0xFF) {
                    fileSize = entry.fileSize;
                    found = true;
#if defined(__GNUC__) || defined(__clang__)
                    serial::puts("[VFS] Opened: ");
                    serial::puts(path);
                    serial::puts("\n");
#endif
                }
            } else {
                // File not found - check if we should create it
                if (flags & OPEN_CREATE) {
                    // TODO: File creation not yet implemented
#if defined(__GNUC__) || defined(__clang__)
                    serial::puts("[VFS] ERROR: File creation not implemented\n");
#endif
                    return 0xFF;
                }
#if defined(__GNUC__) || defined(__clang__)
                serial::puts("[VFS] ERROR: File not found: ");
                serial::puts(path);
                serial::puts("\n");
#endif
                return 0xFF;
            }
            break;
        }
            
        case FS_TYPE_EXT2:
        case FS_TYPE_EXT4: {
            // ext4 path lookup - simplified for now
            // TODO: Implement ext4::lookup_path similar to FAT
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS] ext4 path lookup not fully implemented\n");
#endif
            return 0xFF;
        }
            
        default:
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS] ERROR: Unsupported filesystem type\n");
#endif
            return 0xFF;
    }
    
    if (!found) {
        return 0xFF;
    }
    
    // Initialize handle
    FileHandle& fh = s_files[handle];
    fh.open = true;
    fh.mountIndex = static_cast<uint8_t>(mount - s_mounts);
    fh.flags = flags;
    fh.position = 0;
    fh.size = fileSize;
    fh.fsFileHandle = fsHandle;
    strcopy(fh.path, path, sizeof(fh.path));
    
    return handle;
}

Status close(uint8_t handle)
{
    if (handle >= VFS_MAX_OPEN_FILES) return VFS_ERR_INVALID;
    if (!s_files[handle].open) return VFS_ERR_INVALID;
    
    FileHandle& fh = s_files[handle];
    
    // Close via filesystem driver
    MountPoint* mount = &s_mounts[fh.mountIndex];
    
    switch (mount->fsType) {
        case FS_TYPE_FAT32:
        case FS_TYPE_EXFAT:
            if (fh.fsFileHandle != 0xFF) {
                fs_fat::close_file(fh.fsFileHandle);
            }
            break;
            
        default:
            break;
    }
    
    fh.open = false;
    return VFS_OK;
}

int32_t read(uint8_t handle, void* buffer, uint32_t size)
{
    if (handle >= VFS_MAX_OPEN_FILES) return VFS_ERR_INVALID;
    if (!s_files[handle].open) return VFS_ERR_INVALID;
    if (!buffer || size == 0) return VFS_ERR_INVALID;
    
    FileHandle& fh = s_files[handle];
    
    if (!(fh.flags & OPEN_READ)) {
        return VFS_ERR_INVALID;
    }
    
    MountPoint* mount = &s_mounts[fh.mountIndex];
    int32_t bytesRead = 0;
    
    switch (mount->fsType) {
        case FS_TYPE_FAT32:
        case FS_TYPE_EXFAT:
            if (fh.fsFileHandle != 0xFF) {
                bytesRead = static_cast<int32_t>(
                    fs_fat::read_file(fh.fsFileHandle, buffer, size));
            }
            break;
            
        default:
            break;
    }
    
    if (bytesRead > 0) {
        fh.position += static_cast<uint64_t>(bytesRead);
    }
    
    return bytesRead;
}

int32_t write(uint8_t handle, const void* buffer, uint32_t size)
{
    if (handle >= VFS_MAX_OPEN_FILES) return VFS_ERR_INVALID;
    if (!s_files[handle].open) return VFS_ERR_INVALID;
    if (!buffer || size == 0) return VFS_ERR_INVALID;
    
    FileHandle& fh = s_files[handle];
    
    if (!(fh.flags & OPEN_WRITE)) {
        return VFS_ERR_INVALID;
    }
    
    MountPoint* mount = &s_mounts[fh.mountIndex];
    
    if (mount->readOnly) {
        return VFS_ERR_READ_ONLY;
    }
    
    // Write operations depend on filesystem support
    // Currently FAT and ext4 have limited write support
    
    return VFS_ERR_NOT_SUPPORTED;
}

Status seek(uint8_t handle, int64_t offset, SeekOrigin origin)
{
    if (handle >= VFS_MAX_OPEN_FILES) return VFS_ERR_INVALID;
    if (!s_files[handle].open) return VFS_ERR_INVALID;
    
    FileHandle& fh = s_files[handle];
    int64_t newPos = 0;
    
    switch (origin) {
        case SEEK_SET:
            newPos = offset;
            break;
        case SEEK_CUR:
            newPos = static_cast<int64_t>(fh.position) + offset;
            break;
        case SEEK_END:
            newPos = static_cast<int64_t>(fh.size) + offset;
            break;
        default:
            return VFS_ERR_INVALID;
    }
    
    if (newPos < 0) {
        newPos = 0;
    }
    
    fh.position = static_cast<uint64_t>(newPos);
    return VFS_OK;
}

int64_t tell(uint8_t handle)
{
    if (handle >= VFS_MAX_OPEN_FILES) return -1;
    if (!s_files[handle].open) return -1;
    return static_cast<int64_t>(s_files[handle].position);
}

int64_t file_size(uint8_t handle)
{
    if (handle >= VFS_MAX_OPEN_FILES) return -1;
    if (!s_files[handle].open) return -1;
    return static_cast<int64_t>(s_files[handle].size);
}

Status flush(uint8_t handle)
{
    if (handle >= VFS_MAX_OPEN_FILES) return VFS_ERR_INVALID;
    if (!s_files[handle].open) return VFS_ERR_INVALID;
    // No buffering currently implemented
    return VFS_OK;
}

const FileHandle* get_handle(uint8_t handle)
{
    if (handle >= VFS_MAX_OPEN_FILES) return nullptr;
    if (!s_files[handle].open) return nullptr;
    return &s_files[handle];
}

// ================================================================
// Public API — Directory Operations
// ================================================================

uint8_t opendir(const char* path)
{
    if (!path) return 0xFF;
    
    MountPoint* mount = find_mount_for_path(path);
    if (!mount) return 0xFF;
    
    // Find free iterator
    uint8_t iter = 0xFF;
    for (uint8_t i = 0; i < VFS_MAX_OPEN_FILES; ++i) {
        if (!s_dirs[i].active) {
            iter = i;
            break;
        }
    }
    
    if (iter == 0xFF) return 0xFF;
    
    // Initialize iterator
    DirIterator& di = s_dirs[iter];
    di.active = true;
    di.mountIndex = static_cast<uint8_t>(mount - s_mounts);
    strcopy(di.path, path, sizeof(di.path));
    di.index = 0;
    
    // Get path relative to mount point
    const char* relPath = get_relative_path(path, mount);
    
    switch (mount->fsType) {
        case FS_TYPE_FAT32:
        case FS_TYPE_EXFAT: {
            // If path is root or empty, open root directory
            if (!relPath || relPath[0] == '\0' || 
                (relPath[0] == '/' && relPath[1] == '\0')) {
                if (!fs_fat::open_root_dir(mount->fsVolumeIndex)) {
                    di.active = false;
                    return 0xFF;
                }
            } else {
                // Lookup the directory by path
                fs_fat::DirEntry dirEntry;
                if (!fs_fat::lookup_path(mount->fsVolumeIndex, relPath, &dirEntry)) {
                    di.active = false;
                    return 0xFF;  // Directory not found
                }
                
                // Must be a directory
                if (!dirEntry.isDir) {
                    di.active = false;
                    return 0xFF;  // Not a directory
                }
                
                // Open the directory by its cluster
                if (!fs_fat::open_dir(mount->fsVolumeIndex, dirEntry.firstCluster)) {
                    di.active = false;
                    return 0xFF;
                }
            }
            break;
        }
            
        case FS_TYPE_EXT2:
        case FS_TYPE_EXT4:
            // ext4 subdirectory support not yet implemented
            if (!fs_ext4::open_root_dir(mount->fsVolumeIndex)) {
                di.active = false;
                return 0xFF;
            }
            break;
            
        default:
            di.active = false;
            return 0xFF;
    }
    
    return iter;
}

bool readdir(uint8_t iterator, DirEntry* entry)
{
    if (iterator >= VFS_MAX_OPEN_FILES) return false;
    if (!s_dirs[iterator].active) return false;
    if (!entry) return false;
    
    DirIterator& di = s_dirs[iterator];
    MountPoint* mount = &s_mounts[di.mountIndex];
    
    switch (mount->fsType) {
        case FS_TYPE_FAT32:
        case FS_TYPE_EXFAT: {
            fs_fat::DirEntry fatEntry;
            if (!fs_fat::read_dir(mount->fsVolumeIndex, &fatEntry)) {
                return false;
            }
            
            strcopy(entry->name, fatEntry.name, sizeof(entry->name));
            entry->type = fatEntry.isDir ? FILE_TYPE_DIRECTORY : FILE_TYPE_REGULAR;
            entry->size = fatEntry.fileSize;
            entry->isHidden = (fatEntry.attr & 0x02) != 0;
            entry->isSystem = (fatEntry.attr & 0x04) != 0;
            entry->isReadOnly = (fatEntry.attr & 0x01) != 0;
            ++di.index;
            return true;
        }
        
        case FS_TYPE_EXT2:
        case FS_TYPE_EXT4: {
            fs_ext4::Ext4DirEntry extEntry;
            if (!fs_ext4::read_dir(mount->fsVolumeIndex, &extEntry)) {
                return false;
            }
            
            strcopy(entry->name, extEntry.name, sizeof(entry->name));
            entry->type = extEntry.isDir ? FILE_TYPE_DIRECTORY : FILE_TYPE_REGULAR;
            entry->size = 0; // ext4 DirEntry does not include size
            entry->isHidden = (entry->name[0] == '.');
            entry->isSystem = false;
            entry->isReadOnly = false;
            ++di.index;
            return true;
        }
        
        default:
            return false;
    }
}

void closedir(uint8_t iterator)
{
    if (iterator >= VFS_MAX_OPEN_FILES) return;
    s_dirs[iterator].active = false;
}

Status mkdir(const char* path)
{
    (void)path;
    // Directory creation not yet implemented
    return VFS_ERR_NOT_SUPPORTED;
}

Status rmdir(const char* path)
{
    (void)path;
    // Directory removal not yet implemented
    return VFS_ERR_NOT_SUPPORTED;
}

// ================================================================
// Public API — File/Directory Management
// ================================================================

bool exists(const char* path)
{
    FileInfo info;
    return stat(path, &info) == VFS_OK;
}

Status stat(const char* path, FileInfo* info)
{
    if (!path || !info) return VFS_ERR_INVALID;
    
    MountPoint* mount = find_mount_for_path(path);
    if (!mount) return VFS_ERR_NOT_MOUNT;
    
    // Get path relative to mount point
    const char* relPath = get_relative_path(path, mount);
    
    // Zero-initialize the output
    memzero(info, sizeof(FileInfo));
    
    switch (mount->fsType) {
        case FS_TYPE_FAT32:
        case FS_TYPE_EXFAT: {
            fs_fat::DirEntry entry;
            if (!fs_fat::lookup_path(mount->fsVolumeIndex, relPath, &entry)) {
                return VFS_ERR_NOT_FOUND;
            }
            
            // Fill in FileInfo from DirEntry
            strcopy(info->name, entry.name, sizeof(info->name));
            info->type = entry.isDir ? FILE_TYPE_DIRECTORY : FILE_TYPE_REGULAR;
            info->size = entry.fileSize;
            info->permissions = 0755;  // Default permissions for FAT
            if (entry.attr & 0x01) info->permissions &= ~0222;  // Read-only
            info->createDate = entry.crtDate;
            info->createTime = entry.crtTime;
            info->modifyDate = entry.wrtDate;
            info->modifyTime = entry.wrtTime;
            info->accessDate = entry.wrtDate;  // FAT doesn't store access time accurately
            info->accessTime = entry.wrtTime;
            
            return VFS_OK;
        }
        
        case FS_TYPE_EXT2:
        case FS_TYPE_EXT4: {
            // TODO: Implement ext4 stat
            return VFS_ERR_NOT_SUPPORTED;
        }
        
        default:
            return VFS_ERR_NOT_SUPPORTED;
    }
}

Status unlink(const char* path)
{
    (void)path;
    return VFS_ERR_NOT_SUPPORTED;
}

Status rename(const char* oldPath, const char* newPath)
{
    (void)oldPath;
    (void)newPath;
    return VFS_ERR_NOT_SUPPORTED;
}

// ================================================================
// Public API — High-Level Convenience Functions
// ================================================================

int32_t read_file(const char* path, void* buffer, uint32_t maxSize)
{
    uint8_t handle = open(path, OPEN_READ);
    if (handle == 0xFF) return VFS_ERR_NOT_FOUND;
    
    int32_t bytesRead = read(handle, buffer, maxSize);
    close(handle);
    
    return bytesRead;
}

int32_t write_file(const char* path, const void* buffer, uint32_t size)
{
    uint8_t handle = open(path, OPEN_WRITE | OPEN_CREATE | OPEN_TRUNCATE);
    if (handle == 0xFF) return VFS_ERR_INVALID;
    
    int32_t bytesWritten = write(handle, buffer, size);
    close(handle);
    
    return bytesWritten;
}

int32_t append_file(const char* path, const void* buffer, uint32_t size)
{
    uint8_t handle = open(path, OPEN_WRITE | OPEN_APPEND | OPEN_CREATE);
    if (handle == 0xFF) return VFS_ERR_INVALID;
    
    int32_t bytesWritten = write(handle, buffer, size);
    close(handle);
    
    return bytesWritten;
}

// ================================================================
// Public API — Filesystem Information
// ================================================================

uint64_t total_space(const char* mountPath)
{
    MountPoint* mount = find_mount_for_path(mountPath);
    if (!mount) return 0;
    
    const block::BlockDevice* dev = block::get_device(mount->blockDevIndex);
    if (!dev) return 0;
    
    return dev->totalSectors * dev->sectorSize;
}

uint64_t free_space(const char* mountPath)
{
    MountPoint* mount = find_mount_for_path(mountPath);
    if (!mount) return 0;
    
    // Would need to query filesystem for actual free space
    // For now, return 0 (unknown)
    return 0;
}

const char* fs_type_name(FSType type)
{
    switch (type) {
        case FS_TYPE_FAT32:   return "FAT32";
        case FS_TYPE_EXFAT:   return "exFAT";
        case FS_TYPE_EXT2:    return "ext2";
        case FS_TYPE_EXT4:    return "ext4";
        case FS_TYPE_UFS:     return "UFS";
        case FS_TYPE_ISO9660: return "ISO9660";
        case FS_TYPE_RAMDISK: return "RamDisk";
        default:              return "Unknown";
    }
}

} // namespace vfs
} // namespace kernel
