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
#include "include/kernel/partition_table.h"
#include "include/kernel/disk_initialization.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

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
static uint64_t     s_nextMountGeneration = 0;
static volatile uint32_t s_vfsMetadataLock = 0;
static uint16_t     s_mountAdmissionsInFlight = 0;
// Tokens are monotonic across unmounts and test resets so stale handles can
// never become valid again when a table slot is reused.
static HandleToken  s_nextHandleToken = 0x100u;
static storage::PartitionTableModel s_partitionTableScratch;
// Filesystem detection reads one complete device logical sector. Reuse this
// bounded scratch instead of putting a 4 KiB sector on the kernel stack.
alignas(4096) static uint8_t s_detectSectorBuffer[
    storage::MAX_LOGICAL_SECTOR_SIZE];
static Status mount_io_status(const MountPoint* mount);
static Status map_block_status(block::Status status);

// Protect only VFS registry and reference metadata. The lock is never held
// while a filesystem or block-device callback runs.
static void lock_vfs_metadata()
{
#if defined(_MSC_VER)
    while (_InterlockedCompareExchange(
        reinterpret_cast<volatile long*>(&s_vfsMetadataLock), 1, 0) != 0) {}
#else
    while (__sync_lock_test_and_set(&s_vfsMetadataLock, 1) != 0) {}
#endif
}

static void unlock_vfs_metadata()
{
#if defined(_MSC_VER)
    _InterlockedExchange(reinterpret_cast<volatile long*>(&s_vfsMetadataLock), 0);
#else
    __sync_lock_release(&s_vfsMetadataLock);
#endif
}

static bool initialize_mount_instance(MountPoint& mount)
{
    if (s_nextMountGeneration == UINT64_MAX) return false;
    mount.lifecycleState = MOUNT_LIFECYCLE_MOUNTED;
    mount.mountGeneration = ++s_nextMountGeneration;
    mount.activeOperations = 0;
    mount.openFileReferences = 0;
    mount.openDirectoryReferences = 0;
    return true;
}

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

static HandleToken allocate_handle_token()
{
    const HandleToken exhausted = ~static_cast<HandleToken>(0);
    if (s_nextHandleToken == INVALID_HANDLE || s_nextHandleToken == exhausted)
        return INVALID_HANDLE;
    return s_nextHandleToken++;
}

static uint8_t find_file_slot(HandleToken token)
{
    if (token == INVALID_HANDLE) return 0xFF;
    for (uint8_t i = 0; i < VFS_MAX_OPEN_FILES; ++i)
        if (s_files[i].open && s_files[i].token == token) return i;
    return 0xFF;
}

static uint8_t find_directory_slot(HandleToken token)
{
    if (token == INVALID_HANDLE) return 0xFF;
    for (uint8_t i = 0; i < VFS_MAX_OPEN_FILES; ++i)
        if (s_dirs[i].active && s_dirs[i].token == token) return i;
    return 0xFF;
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

void join_path(const char* base, const char* name, char* output, size_t outputSize);
static int strcmp(const char* s1, const char* s2);

static bool partition_entry_matches_identity(
    const storage::PartitionEntry& entry,
    const block::PartitionIdentity& identity)
{
    if (entry.partitionNumber != identity.partitionNumber ||
        entry.startLba != identity.startLba ||
        entry.endLba != identity.endLba ||
        entry.sectorCount != identity.sectorCount ||
        entry.isGpt != (identity.scheme == storage::PARTITION_SCHEME_GPT))
        return false;
    if (identity.scheme == storage::PARTITION_SCHEME_GPT) {
        for (size_t i = 0; i < sizeof(identity.uniqueGuid); ++i)
            if (entry.uniqueGuid[i] != identity.uniqueGuid[i]) return false;
        return true;
    }
    return identity.scheme == storage::PARTITION_SCHEME_MBR &&
           entry.mbrType == identity.mbrType;
}

static bool same_partition_entry(const storage::PartitionEntry& left,
                                 const storage::PartitionEntry& right)
{
    if (left.partitionNumber != right.partitionNumber ||
        left.isGpt != right.isGpt || left.mbrType != right.mbrType ||
        left.startLba != right.startLba || left.endLba != right.endLba ||
        left.sectorCount != right.sectorCount) return false;
    if (!left.isGpt) return true;
    for (size_t i = 0; i < sizeof(left.uniqueGuid); ++i)
        if (left.uniqueGuid[i] != right.uniqueGuid[i]) return false;
    return true;
}

static bool partition_path_is_normalized(const char* path)
{
    if (!path || path[0] != '/' || path[1] == '\0' ||
        strlen(path) >= VFS_MAX_PATH) return false;
    char normalized[VFS_MAX_PATH];
    normalize_path(path, normalized, sizeof(normalized));
    if (strcmp(path, normalized) != 0) return false;
    for (size_t i = 1; path[i];) {
        if (path[i] == '/') return false;
        size_t end = i;
        while (path[end] && path[end] != '/') {
            const char c = path[end];
            const bool safe = (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.';
            if (!safe) return false;
            ++end;
        }
        const size_t length = end - i;
        if (length == 0 || (length == 1 && path[i] == '.') ||
            (length == 2 && path[i] == '.' && path[i + 1] == '.')) return false;
        i = path[end] ? end + 1 : end;
    }
    return true;
}

static bool find_current_partition(uint8_t deviceIndex,
                                   uint16_t partitionNumber,
                                   storage::PartitionTableModel& table,
                                   storage::PartitionEntry& out)
{
    if (!storage::parse_partition_table(deviceIndex, table) ||
        (table.state != storage::DISK_STATE_VALID_MBR &&
         table.state != storage::DISK_STATE_VALID_GPT) ||
        table.hybridMbr || table.extendedPartitionsPresent ||
        (table.scheme != storage::PARTITION_SCHEME_MBR &&
         table.scheme != storage::PARTITION_SCHEME_GPT)) return false;
    for (uint16_t i = 0; i < table.partitionCount; ++i) {
        if (table.partitions[i].partitionNumber == partitionNumber) {
            out = table.partitions[i];
            return true;
        }
    }
    return false;
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

class MountOperationReference {
public:
    MountOperationReference()
        : mountIndex_(0xFF), mountGeneration_(0), fileSlot_(0xFF),
          directorySlot_(0xFF), handleToken_(INVALID_HANDLE), active_(false),
          mount_(nullptr) {}

    ~MountOperationReference() { release(); }

    Status begin_path(const char* path)
    {
        if (!path) return VFS_ERR_INVALID;
        lock_vfs_metadata();
        MountPoint* found = find_mount_for_path(path);
        if (!found) {
            unlock_vfs_metadata();
            return VFS_ERR_NOT_MOUNT;
        }
        return begin_mount_locked(found);
    }

    Status begin_index(uint8_t index)
    {
        if (index >= VFS_MAX_MOUNTS) return VFS_ERR_NOT_MOUNT;
        lock_vfs_metadata();
        if (!s_mounts[index].active) {
            unlock_vfs_metadata();
            return VFS_ERR_NOT_MOUNT;
        }
        return begin_mount_locked(&s_mounts[index]);
    }

    Status begin_file(HandleToken token, bool forClose = false)
    {
        lock_vfs_metadata();
        const uint8_t slot = find_file_slot(token);
        if (slot == 0xFF) {
            unlock_vfs_metadata();
            return VFS_ERR_INVALID;
        }
        FileHandle& file = s_files[slot];
        if (file.closing || file.activeOperations != 0 ||
            file.mountIndex >= VFS_MAX_MOUNTS) {
            unlock_vfs_metadata();
            return VFS_ERR_BUSY;
        }
        MountPoint& mount = s_mounts[file.mountIndex];
        if (!mount.active || mount.lifecycleState != MOUNT_LIFECYCLE_MOUNTED) {
            unlock_vfs_metadata();
            return VFS_ERR_BUSY;
        }
        if (mount.activeOperations == UINT16_MAX) {
            unlock_vfs_metadata();
            return VFS_ERR_BUSY;
        }
        ++mount.activeOperations;
        ++file.activeOperations;
        if (forClose) file.closing = true;
        mountIndex_ = file.mountIndex;
        mountGeneration_ = mount.mountGeneration;
        fileSlot_ = slot;
        handleToken_ = token;
        mount_ = &mount;
        active_ = true;
        unlock_vfs_metadata();
        return VFS_OK;
    }

    Status begin_directory(HandleToken token, bool forClose = false)
    {
        lock_vfs_metadata();
        const uint8_t slot = find_directory_slot(token);
        if (slot == 0xFF) {
            unlock_vfs_metadata();
            return VFS_ERR_INVALID;
        }
        DirIterator& directory = s_dirs[slot];
        if (directory.closing || directory.activeOperations != 0 ||
            directory.mountIndex >= VFS_MAX_MOUNTS) {
            unlock_vfs_metadata();
            return VFS_ERR_BUSY;
        }
        MountPoint& mount = s_mounts[directory.mountIndex];
        if (!mount.active || mount.lifecycleState != MOUNT_LIFECYCLE_MOUNTED) {
            unlock_vfs_metadata();
            return VFS_ERR_BUSY;
        }
        if (mount.activeOperations == UINT16_MAX) {
            unlock_vfs_metadata();
            return VFS_ERR_BUSY;
        }
        ++mount.activeOperations;
        ++directory.activeOperations;
        if (forClose) directory.closing = true;
        mountIndex_ = directory.mountIndex;
        mountGeneration_ = mount.mountGeneration;
        directorySlot_ = slot;
        handleToken_ = token;
        mount_ = &mount;
        active_ = true;
        unlock_vfs_metadata();
        return VFS_OK;
    }

    MountPoint* mount() const { return mount_; }
    uint8_t mount_index() const { return mountIndex_; }
    uint8_t file_slot() const { return fileSlot_; }
    uint8_t directory_slot() const { return directorySlot_; }

    void release()
    {
        if (!active_) return;
        lock_vfs_metadata();
        if (mountIndex_ < VFS_MAX_MOUNTS) {
            MountPoint& mount = s_mounts[mountIndex_];
            if (mount.active && mount.mountGeneration == mountGeneration_ &&
                mount.activeOperations != 0)
                --mount.activeOperations;
        }
        if (fileSlot_ < VFS_MAX_OPEN_FILES) {
            FileHandle& file = s_files[fileSlot_];
            if (file.token == handleToken_ && file.activeOperations != 0)
                --file.activeOperations;
        }
        if (directorySlot_ < VFS_MAX_OPEN_FILES) {
            DirIterator& directory = s_dirs[directorySlot_];
            if (directory.token == handleToken_ &&
                directory.activeOperations != 0)
                --directory.activeOperations;
        }
        unlock_vfs_metadata();
        active_ = false;
    }

private:
    Status begin_mount_locked(MountPoint* mount)
    {
        if (!mount->active ||
            mount->lifecycleState != MOUNT_LIFECYCLE_MOUNTED ||
            mount->activeOperations == UINT16_MAX) {
            unlock_vfs_metadata();
            return VFS_ERR_BUSY;
        }
        ++mount->activeOperations;
        mountIndex_ = static_cast<uint8_t>(mount - s_mounts);
        mountGeneration_ = mount->mountGeneration;
        mount_ = mount;
        active_ = true;
        unlock_vfs_metadata();
        return VFS_OK;
    }

    uint8_t mountIndex_;
    uint64_t mountGeneration_;
    uint8_t fileSlot_;
    uint8_t directorySlot_;
    HandleToken handleToken_;
    bool active_;
    MountPoint* mount_;
};

class MountAdmissionReference {
public:
    MountAdmissionReference() : active_(false) {}
    ~MountAdmissionReference()
    {
        if (!active_) return;
        lock_vfs_metadata();
        if (s_mountAdmissionsInFlight != 0) --s_mountAdmissionsInFlight;
        unlock_vfs_metadata();
    }

    bool begin(bool allowStorageOperation = false)
    {
        lock_vfs_metadata();
        if (s_mountAdmissionsInFlight != 0 ||
            (!allowStorageOperation && storage::storage_operation_active())) {
            unlock_vfs_metadata();
            return false;
        }
        s_mountAdmissionsInFlight = 1;
        active_ = true;
        unlock_vfs_metadata();
        return true;
    }

private:
    bool active_;
};

class FileSlotReservation {
public:
    FileSlotReservation() : slot_(0xFF), active_(false) {}
    ~FileSlotReservation() { cancel(); }

    bool reserve()
    {
        lock_vfs_metadata();
        for (uint8_t i = 0; i < VFS_MAX_OPEN_FILES; ++i) {
            if (!s_files[i].open && !s_files[i].opening) {
                memzero(&s_files[i], sizeof(s_files[i]));
                s_files[i].opening = true;
                slot_ = i;
                active_ = true;
                unlock_vfs_metadata();
                return true;
            }
        }
        unlock_vfs_metadata();
        return false;
    }

    bool commit(MountOperationReference& operation, const char* path,
                uint16_t flags, uint64_t size, uint8_t fsHandle,
                HandleToken& outToken)
    {
        outToken = INVALID_HANDLE;
        if (!active_ || slot_ >= VFS_MAX_OPEN_FILES || !operation.mount())
            return false;
        lock_vfs_metadata();
        MountPoint& mount = s_mounts[operation.mount_index()];
        FileHandle& file = s_files[slot_];
        const HandleToken token = allocate_handle_token();
        if (!file.opening || !mount.active ||
            mount.mountGeneration != operation.mount()->mountGeneration ||
            mount.lifecycleState != MOUNT_LIFECYCLE_MOUNTED ||
            mount.openFileReferences == UINT16_MAX ||
            token == INVALID_HANDLE) {
            unlock_vfs_metadata();
            return false;
        }
        file.opening = false;
        file.open = true;
        file.token = token;
        file.mountIndex = operation.mount_index();
        file.flags = flags;
        file.position = 0;
        file.size = size;
        file.fsFileHandle = fsHandle;
        strcopy(file.path, path, sizeof(file.path));
        ++mount.openFileReferences;
        outToken = token;
        active_ = false;
        unlock_vfs_metadata();
        return true;
    }

    uint8_t slot() const { return slot_; }

    void cancel()
    {
        if (!active_ || slot_ >= VFS_MAX_OPEN_FILES) return;
        lock_vfs_metadata();
        if (s_files[slot_].opening)
            memzero(&s_files[slot_], sizeof(s_files[slot_]));
        unlock_vfs_metadata();
        active_ = false;
    }

private:
    uint8_t slot_;
    bool active_;
};

class DirectorySlotReservation {
public:
    DirectorySlotReservation() : slot_(0xFF), active_(false) {}
    ~DirectorySlotReservation() { cancel(); }

    bool reserve()
    {
        lock_vfs_metadata();
        for (uint8_t i = 0; i < VFS_MAX_OPEN_FILES; ++i) {
            if (!s_dirs[i].active && !s_dirs[i].opening) {
                memzero(&s_dirs[i], sizeof(s_dirs[i]));
                s_dirs[i].opening = true;
                slot_ = i;
                active_ = true;
                unlock_vfs_metadata();
                return true;
            }
        }
        unlock_vfs_metadata();
        return false;
    }

    bool commit(MountOperationReference& operation, const char* path,
                HandleToken& outToken)
    {
        outToken = INVALID_HANDLE;
        if (!active_ || slot_ >= VFS_MAX_OPEN_FILES || !operation.mount())
            return false;
        lock_vfs_metadata();
        MountPoint& mount = s_mounts[operation.mount_index()];
        DirIterator& directory = s_dirs[slot_];
        const HandleToken token = allocate_handle_token();
        if (!directory.opening || !mount.active ||
            mount.mountGeneration != operation.mount()->mountGeneration ||
            mount.lifecycleState != MOUNT_LIFECYCLE_MOUNTED ||
            mount.openDirectoryReferences == UINT16_MAX ||
            token == INVALID_HANDLE) {
            unlock_vfs_metadata();
            return false;
        }
        directory.opening = false;
        directory.active = true;
        directory.token = token;
        directory.mountIndex = operation.mount_index();
        strcopy(directory.path, path, sizeof(directory.path));
        directory.index = 0;
        ++mount.openDirectoryReferences;
        outToken = token;
        active_ = false;
        unlock_vfs_metadata();
        return true;
    }

    uint8_t slot() const { return slot_; }

    void cancel()
    {
        if (!active_ || slot_ >= VFS_MAX_OPEN_FILES) return;
        lock_vfs_metadata();
        if (s_dirs[slot_].opening)
            memzero(&s_dirs[slot_], sizeof(s_dirs[slot_]));
        unlock_vfs_metadata();
        active_ = false;
    }

private:
    uint8_t slot_;
    bool active_;
};

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

static const char* block_status_name(block::Status status)
{
    switch (status) {
        case block::BLOCK_OK: return "BLOCK_OK";
        case block::BLOCK_ERR_IO: return "BLOCK_ERR_IO";
        case block::BLOCK_ERR_TIMEOUT: return "BLOCK_ERR_TIMEOUT";
        case block::BLOCK_ERR_NO_MEDIA: return "BLOCK_ERR_NO_MEDIA";
        case block::BLOCK_ERR_NOT_READY: return "BLOCK_ERR_NOT_READY";
        case block::BLOCK_ERR_INVALID: return "BLOCK_ERR_INVALID";
        case block::BLOCK_ERR_UNSUPPORTED: return "BLOCK_ERR_UNSUPPORTED";
        case block::BLOCK_ERR_READ_ONLY: return "BLOCK_ERR_READ_ONLY";
        case block::BLOCK_ERR_WRITE_UNCERTAIN: return "USB write completion is uncertain";
        case block::BLOCK_ERR_DURABILITY_UNVERIFIED: return "USB cache synchronization failed; durability could not be verified";
        default: return "BLOCK_STATUS_UNKNOWN";
    }
}

static Status map_fat_file_write_status(fs_fat::FileWriteStatus status)
{
    switch (status) {
        case fs_fat::FILE_WRITE_OK: return VFS_OK;
        case fs_fat::FILE_WRITE_NOT_FOUND: return VFS_ERR_NOT_FOUND;
        case fs_fat::FILE_WRITE_ALREADY_EXISTS: return VFS_ERR_EXISTS;
        case fs_fat::FILE_WRITE_INVALID_NAME:
        case fs_fat::FILE_WRITE_INVALID_ARGUMENT: return VFS_ERR_INVALID;
        case fs_fat::FILE_WRITE_NO_FREE_CLUSTER:
        case fs_fat::FILE_WRITE_NO_FREE_ENTRY:
        case fs_fat::FILE_WRITE_NO_SPACE: return VFS_ERR_NO_SPACE;
        case fs_fat::FILE_WRITE_READ_ONLY: return VFS_ERR_READ_ONLY;
        case fs_fat::FILE_WRITE_NOT_MOUNTED: return VFS_ERR_NOT_MOUNT;
        case fs_fat::FILE_WRITE_UNSUPPORTED_TYPE: return VFS_ERR_NOT_SUPPORTED;
        case fs_fat::FILE_WRITE_IO_ERROR:
            return map_block_status(fs_fat::last_io_status());
        case fs_fat::FILE_WRITE_IO_TIMEOUT: return VFS_ERR_IO_TIMEOUT;
        case fs_fat::FILE_WRITE_CORRUPT_CHAIN: return VFS_ERR_CORRUPT_CHAIN;
        case fs_fat::FILE_WRITE_NO_PROGRESS: return VFS_ERR_NO_PROGRESS;
        case fs_fat::FILE_WRITE_ALLOCATION_FAILED: return VFS_ERR_ALLOCATION_FAILED;
        default: return VFS_ERR_NOT_SUPPORTED;
    }
}

static bool has_source_prefix(const MountPoint* mount)
{
    return mount && mount->sourcePrefix[0] != '\0' &&
        !(mount->sourcePrefix[0] == '/' && mount->sourcePrefix[1] == '\0');
}

static const char* resolve_relative_path(const char* fullPath,
                                         const MountPoint* mount,
                                         char* resolvedPath,
                                         size_t resolvedPathSize)
{
    const char* relative = get_relative_path(fullPath, mount);
    if (!relative || !mount) return relative;
    if (!mount->alias || !has_source_prefix(mount)) return relative;
    if (!resolvedPath || resolvedPathSize == 0) return nullptr;

    if ((relative[0] == '/' && relative[1] == '\0') || relative[0] == '\0') {
        strcopy(resolvedPath, mount->sourcePrefix, resolvedPathSize);
        return resolvedPath;
    }

    join_path(mount->sourcePrefix, relative, resolvedPath, resolvedPathSize);
    return resolvedPath;
}

// Detect filesystem type from block device
static FSType detect_fs_type(uint8_t blockDevIndex)
{
    const block::BlockDevice* device = block::get_device(blockDevIndex);
    if (!device || (device->sectorSize != 512 &&
                    device->sectorSize != 4096)) return FS_TYPE_NONE;
    // One full logical block is needed for boot-sector and MBR probing. The
    // legacy ext/UFS probes below remain restricted to 512-byte devices.
    uint8_t* buffer = s_detectSectorBuffer;
    
    // Read first sector
    if (block::read_sectors_checked(blockDevIndex, 0, 1, buffer,
            storage::MAX_LOGICAL_SECTOR_SIZE) != block::BLOCK_OK) {
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
        
        const uint16_t bytesPerSector =
            *reinterpret_cast<uint16_t*>(&buffer[11]);
        const bool matchingFatGeometry =
            bytesPerSector == device->sectorSize;

        // FAT BPB fields stay at their standard offsets in a 4Kn block.
        if (matchingFatGeometry &&
            buffer[82] == 'F' && buffer[83] == 'A' && buffer[84] == 'T' &&
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
        if (matchingFatGeometry && buffer[54] == 'F' &&
            buffer[55] == 'A' && buffer[56] == 'T') {
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS]   Found FAT string at offset 54\n");
#endif
            return FS_TYPE_FAT32;  // Treat as FAT32 for now
        }
        
        // Check for FAT32 at offset 82 with different format
        if (matchingFatGeometry && buffer[82] == 'F' &&
            buffer[83] == 'A' && buffer[84] == 'T') {
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS]   Found FAT at offset 82 (partial)\n");
#endif
            return FS_TYPE_FAT32;
        }
        
        // Check BPB fields to identify FAT32 even without string
        // FAT32 has sectorsPerFAT16 = 0 (offset 22-23) and sectorsPerFAT32 > 0 (offset 36-39)
        uint16_t sectorsPerFAT16 = *reinterpret_cast<uint16_t*>(&buffer[22]);
        uint32_t sectorsPerFAT32 = *reinterpret_cast<uint32_t*>(&buffer[36]);
        
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS]   BPB: bps=0x");
        serial::put_hex16(bytesPerSector);
        serial::puts(" spf16=0x");
        serial::put_hex16(sectorsPerFAT16);
        serial::puts(" spf32=0x");
        serial::put_hex32(sectorsPerFAT32);
        serial::puts("\n");
#endif
        
        if (matchingFatGeometry && sectorsPerFAT16 == 0 &&
            sectorsPerFAT32 > 0) {
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
        
        if (partType == 0x01 || partType == 0x04 || partType == 0x06 ||
            partType == 0x0B || partType == 0x0C || partType == 0x0E) {
            // FAT-family partition - let the FAT driver probe the partition start.
            uint32_t startLBA = *reinterpret_cast<uint32_t*>(&buffer[446 + 8]);
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS]   FAT partition at LBA 0x");
            serial::put_hex32(startLBA);
            serial::puts(" - deferring to partition-aware FAT mount\n");
#endif
            return FS_TYPE_FAT32;
        }
    }
    
    if (device->sectorSize != 512) return FS_TYPE_NONE;

    // Check for ext2/ext4 (superblock at offset 1024).
    if (block::read_sectors_checked(blockDevIndex, 2, 1, buffer,
            storage::MAX_LOGICAL_SECTOR_SIZE) == block::BLOCK_OK) {
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
    if (block::read_sectors_checked(blockDevIndex, 16, 4, buffer,
            storage::MAX_LOGICAL_SECTOR_SIZE) == block::BLOCK_OK) {
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
    lock_vfs_metadata();
    if (s_initialized) {
        unlock_vfs_metadata();
        return;
    }
    memzero(s_mounts, sizeof(s_mounts));
    memzero(s_files, sizeof(s_files));
    memzero(s_dirs, sizeof(s_dirs));
    s_mountCount = 0;
    s_mountAdmissionsInFlight = 0;
    s_initialized = true;
    unlock_vfs_metadata();
    
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[VFS] Initialized\n");
#endif
}

// ================================================================
// Public API — Mount Management
// ================================================================

uint8_t mount(const char* path, uint8_t blockDevIndex)
{
    if (storage::storage_operation_active()) return 0xFF;
    FSType fsType = detect_fs_type(blockDevIndex);
    if (fsType == FS_TYPE_NONE) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] ERROR: Could not detect filesystem type\n");
#endif
        return 0xFF;
    }
    
    return mount_type(path, blockDevIndex, fsType);
}

uint8_t mount_partition(const char* path, uint8_t blockDevIndex, uint8_t partitionNumber)
{
    return mount_partition_detailed(path, blockDevIndex, partitionNumber).mountIndex;
}

static PartitionMountResult partition_mount_result(PartitionMountError error,
                                                    uint8_t mountIndex = 0xFF)
{
    PartitionMountResult result = { mountIndex, error };
    return result;
}

PartitionMountResult mount_partition_detailed(const char* path,
                                              uint8_t blockDevIndex,
                                              uint16_t partitionNumber,
                                              uint64_t expectedRegistrationId,
                                              const storage::PartitionEntry* expectedPartition)
{
    if (!s_initialized) init();
    if (!path || partitionNumber == 0)
        return partition_mount_result(PARTITION_MOUNT_INVALID_ARGUMENT);
    if (!partition_path_is_normalized(path))
        return partition_mount_result(PARTITION_MOUNT_BAD_PATH);
    MountAdmissionReference admission;
    if (!admission.begin())
        return partition_mount_result(PARTITION_MOUNT_OPERATION_BUSY);
    // A destructive operation pins an exact disk across long scans and
    // revalidates mount state before writing. Prevent a new VFS view from
    // appearing on any disk while that exclusive operation is active.
    if (storage::storage_operation_active())
        return partition_mount_result(PARTITION_MOUNT_OPERATION_BUSY);

    lock_vfs_metadata();
    bool pathAlreadyMounted = false;
    uint8_t mountSlot = 0xFF;
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0)
            pathAlreadyMounted = true;
        if (!s_mounts[i].active && mountSlot == 0xFF) mountSlot = i;
    }
    unlock_vfs_metadata();
    if (pathAlreadyMounted)
        return partition_mount_result(PARTITION_MOUNT_PATH_OCCUPIED);
    if (mountSlot == 0xFF)
        return partition_mount_result(PARTITION_MOUNT_VFS_REGISTRY_FULL);

    block::BlockEndpoint parentEndpoint = {};
    if (!block::make_device_endpoint(blockDevIndex, parentEndpoint))
        return partition_mount_result(PARTITION_MOUNT_DEVICE_UNAVAILABLE);
    if (expectedRegistrationId != 0 &&
        parentEndpoint.registrationId != expectedRegistrationId)
        return partition_mount_result(PARTITION_MOUNT_DEVICE_UNAVAILABLE);
    storage::PartitionTableModel& table = s_partitionTableScratch;
    if (!storage::parse_partition_table(blockDevIndex, table) ||
        (table.state != storage::DISK_STATE_VALID_MBR &&
         table.state != storage::DISK_STATE_VALID_GPT) ||
        table.hybridMbr || table.extendedPartitionsPresent ||
        (table.scheme != storage::PARTITION_SCHEME_MBR &&
         table.scheme != storage::PARTITION_SCHEME_GPT))
        return partition_mount_result(PARTITION_MOUNT_TABLE_INVALID);

    storage::PartitionEntry partition = {};
    bool found = false;
    for (uint16_t i = 0; i < table.partitionCount; ++i) {
        if (table.partitions[i].partitionNumber == partitionNumber) {
            partition = table.partitions[i];
            found = true;
            break;
        }
    }
    if (!found) return partition_mount_result(expectedPartition
        ? PARTITION_MOUNT_PARTITION_CHANGED
        : PARTITION_MOUNT_PARTITION_MISSING);
    if (expectedPartition && !same_partition_entry(partition, *expectedPartition))
        return partition_mount_result(PARTITION_MOUNT_PARTITION_CHANGED);
    if (!block::registration_is_present(blockDevIndex,
            parentEndpoint.registrationId))
        return partition_mount_result(PARTITION_MOUNT_DEVICE_UNAVAILABLE);

    block::PartitionViewHandle viewHandle = {};
    block::BlockEndpoint partitionEndpoint = {};
    block::PartitionIdentity identity = {};
    if (!block::create_partition_view(blockDevIndex, table.scheme, partition,
            viewHandle, partitionEndpoint, &identity)) {
        return partition_mount_result(block::registration_is_present(
                blockDevIndex, parentEndpoint.registrationId)
            ? PARTITION_MOUNT_VIEW_REGISTRY_FULL
            : PARTITION_MOUNT_DEVICE_UNAVAILABLE);
    }
    if (identity.parentRegistrationId != parentEndpoint.registrationId ||
        (expectedRegistrationId != 0 &&
         identity.parentRegistrationId != expectedRegistrationId)) {
        block::release_partition_view(viewHandle);
        return partition_mount_result(PARTITION_MOUNT_DEVICE_UNAVAILABLE);
    }
    bool partitionAlreadyMounted = false;
    lock_vfs_metadata();
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        const MountPoint& existing = s_mounts[i];
        if (existing.active && existing.partitionMount &&
            block::same_partition_identity(existing.partitionIdentity, identity))
            partitionAlreadyMounted = true;
    }
    unlock_vfs_metadata();
    if (partitionAlreadyMounted) {
        block::release_partition_view(viewHandle);
        return partition_mount_result(PARTITION_MOUNT_ALREADY_MOUNTED);
    }

    storage::PartitionEntry current = {};
    if (!block::registration_is_present(blockDevIndex,
            identity.parentRegistrationId) ||
        !find_current_partition(blockDevIndex, partitionNumber, table, current) ||
        !partition_entry_matches_identity(current, identity) ||
        (expectedPartition && !same_partition_entry(current, *expectedPartition))) {
        block::release_partition_view(viewHandle);
        return partition_mount_result(PARTITION_MOUNT_PARTITION_CHANGED);
    }

    const uint8_t fsVolume = fs_fat::mount_endpoint(partitionEndpoint);
    if (fsVolume == 0xFF) {
        block::release_partition_view(viewHandle);
        return partition_mount_result(PARTITION_MOUNT_FILESYSTEM_UNRECOGNIZED);
    }
    const fs_fat::FATVolume* fatVolume = fs_fat::get_volume(fsVolume);
    if (!fatVolume || fatVolume->type != fs_fat::FAT_TYPE_FAT32) {
        fs_fat::unmount(fsVolume);
        block::release_partition_view(viewHandle);
        return partition_mount_result(PARTITION_MOUNT_FILESYSTEM_UNSUPPORTED);
    }

    current = storage::PartitionEntry{};
    if (!block::registration_is_present(blockDevIndex,
            identity.parentRegistrationId) ||
        !find_current_partition(blockDevIndex, partitionNumber, table, current) ||
        !partition_entry_matches_identity(current, identity) ||
        (expectedPartition && !same_partition_entry(current, *expectedPartition))) {
        fs_fat::unmount(fsVolume);
        block::release_partition_view(viewHandle);
        return partition_mount_result(PARTITION_MOUNT_PARTITION_CHANGED);
    }
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0) {
            fs_fat::unmount(fsVolume);
            block::release_partition_view(viewHandle);
            return partition_mount_result(PARTITION_MOUNT_PATH_OCCUPIED);
        }
    }

    lock_vfs_metadata();
    bool pathOccupied = false;
    uint8_t commitSlot = 0xFF;
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0)
            pathOccupied = true;
        if (!s_mounts[i].active && commitSlot == 0xFF) commitSlot = i;
    }
    if (pathOccupied || commitSlot == 0xFF) {
        unlock_vfs_metadata();
        (void)fs_fat::unmount(fsVolume);
        block::release_partition_view(viewHandle);
        return partition_mount_result(pathOccupied
            ? PARTITION_MOUNT_PATH_OCCUPIED : PARTITION_MOUNT_VFS_REGISTRY_FULL);
    }
    mountSlot = commitSlot;
    MountPoint& mount = s_mounts[mountSlot];
    memzero(&mount, sizeof(mount));
    mount.active = true;
    if (!initialize_mount_instance(mount)) {
        memzero(&mount, sizeof(mount));
        unlock_vfs_metadata();
        (void)fs_fat::unmount(fsVolume);
        block::release_partition_view(viewHandle);
        return partition_mount_result(PARTITION_MOUNT_VFS_REGISTRY_FULL);
    }
    strcopy(mount.path, path, sizeof(mount.path));
    mount.fsType = FS_TYPE_FAT32;
    mount.blockDevIndex = blockDevIndex;
    mount.parentRegistrationId = identity.parentRegistrationId;
    mount.parentRegistryGeneration = identity.parentRegistryGeneration;
    mount.logicalSectorSize = partitionEndpoint.sectorSize;
    mount.fsVolumeIndex = fsVolume;
    mount.readOnly = partitionEndpoint.readOnly ||
        !block::endpoint_supports_durable_writes(partitionEndpoint);
    mount.alias = false;
    mount.partitionMount = true;
    mount.partitionIdentity = identity;
    mount.partitionView = viewHandle;
    ++s_mountCount;
    unlock_vfs_metadata();
    return partition_mount_result(PARTITION_MOUNT_OK, mountSlot);
}

uint8_t mount_type(const char* path, uint8_t blockDevIndex, FSType fsType)
{
    if (!s_initialized) {
        init();
    }
    MountAdmissionReference admission;
    if (!admission.begin() || storage::storage_operation_active()) return 0xFF;
    
    if (!path || strlen(path) == 0) {
        return 0xFF;
    }
    block::BlockDevice geometry = {};
    if (!block::copy_device(blockDevIndex, geometry) ||
        (geometry.sectorSize != 512 &&
         !(fsType == FS_TYPE_FAT32 && geometry.sectorSize == 4096)))
        return 0xFF;
    
    // Check if path is already mounted and reserve a candidate slot snapshot.
    bool pathAlreadyMounted = false;
    uint8_t index = 0xFF;
    lock_vfs_metadata();
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0) {
            pathAlreadyMounted = true;
        }
        if (!s_mounts[i].active && index == 0xFF) index = i;
    }
    unlock_vfs_metadata();
    if (pathAlreadyMounted) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] ERROR: Path already mounted\n");
#endif
        return 0xFF;
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
    if (geometry.sectorSize == 4096 && fsType == FS_TYPE_FAT32) {
        const fs_fat::FATVolume* mountedFat = fs_fat::get_volume(fsVolume);
        if (!mountedFat || mountedFat->type != fs_fat::FAT_TYPE_FAT32) {
            fs_fat::unmount(fsVolume);
            return 0xFF;
        }
    }
    if (!block::registration_is_present(blockDevIndex, geometry.registrationId)) {
        if (fsType == FS_TYPE_FAT32 || fsType == FS_TYPE_EXFAT)
            fs_fat::unmount(fsVolume);
        else if (fsType == FS_TYPE_EXT2 || fsType == FS_TYPE_EXT4)
            fs_ext4::unmount(fsVolume);
        return 0xFF;
    }

    const fs_fat::FATVolume* fatVolume = nullptr;
    block::PartitionViewHandle mountView = {};
    block::PartitionIdentity mountIdentity = {};
    bool partitionMount = false;
    bool partitionReadOnly = false;
    if (fsType == FS_TYPE_FAT32 || fsType == FS_TYPE_EXFAT) {
        fatVolume = fs_fat::get_volume(fsVolume);
        if (fatVolume && fatVolume->endpoint.kind ==
                block::ENDPOINT_PARTITION_VIEW &&
            fatVolume->partitionIdentity.valid) {
            mountView.slot = fatVolume->endpoint.viewSlot;
            mountView.generation = fatVolume->endpoint.viewGeneration;
            mountIdentity = fatVolume->partitionIdentity;
            if (mountIdentity.parentDeviceIndex != blockDevIndex ||
                mountIdentity.parentRegistrationId != geometry.registrationId ||
                !block::retain_partition_view(mountView)) {
                fs_fat::unmount(fsVolume);
                return 0xFF;
            }
            partitionMount = true;
            partitionReadOnly = fatVolume->endpoint.readOnly ||
                !block::endpoint_supports_durable_writes(fatVolume->endpoint);
        }
    }

    // Initialize mount point
    lock_vfs_metadata();
    bool pathOccupiedAtCommit = false;
    uint8_t commitIndex = 0xFF;
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0)
            pathOccupiedAtCommit = true;
        if (!s_mounts[i].active && commitIndex == 0xFF) commitIndex = i;
    }
    if (pathOccupiedAtCommit || commitIndex == 0xFF) {
        unlock_vfs_metadata();
        if (fsType == FS_TYPE_FAT32 || fsType == FS_TYPE_EXFAT)
            (void)fs_fat::unmount(fsVolume);
        else if (fsType == FS_TYPE_EXT2 || fsType == FS_TYPE_EXT4)
            fs_ext4::unmount(fsVolume);
        if (partitionMount) block::release_partition_view(mountView);
        return 0xFF;
    }
    index = commitIndex;
    MountPoint& mp = s_mounts[index];
    memzero(&mp, sizeof(mp));
    mp.active = true;
    if (!initialize_mount_instance(mp)) {
        memzero(&mp, sizeof(mp));
        unlock_vfs_metadata();
        if (fsType == FS_TYPE_FAT32 || fsType == FS_TYPE_EXFAT)
            (void)fs_fat::unmount(fsVolume);
        else if (fsType == FS_TYPE_EXT2 || fsType == FS_TYPE_EXT4)
            fs_ext4::unmount(fsVolume);
        if (partitionMount) block::release_partition_view(mountView);
        return 0xFF;
    }
    strcopy(mp.path, path, sizeof(mp.path));
    mp.fsType = fsType;
    mp.blockDevIndex = blockDevIndex;
    mp.parentRegistrationId = partitionMount
        ? mountIdentity.parentRegistrationId : geometry.registrationId;
    mp.parentRegistryGeneration = partitionMount
        ? mountIdentity.parentRegistryGeneration : block::registry_generation();
    mp.logicalSectorSize = geometry.sectorSize;
    mp.fsVolumeIndex = fsVolume;
    mp.readOnly = partitionMount ? partitionReadOnly : !geometry.writeFn;
    if (!partitionMount &&
        (fsType == FS_TYPE_FAT32 || fsType == FS_TYPE_EXFAT) && fatVolume)
        mp.readOnly = fatVolume->endpoint.readOnly ||
            !block::endpoint_supports_durable_writes(fatVolume->endpoint);
    mp.alias = false;
    mp.sourcePrefix[0] = '\0';
    mp.partitionMount = partitionMount;
    if (partitionMount) {
        mp.partitionIdentity = mountIdentity;
        mp.partitionView = mountView;
    }
    
    ++s_mountCount;
    unlock_vfs_metadata();
    
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[VFS] Mounted ");
    serial::puts(fs_type_name(fsType));
    serial::puts(" at '");
    serial::puts(path);
    serial::puts("'\n");
#endif
    
    return index;
}

static void append_decimal(uint64_t value, char* output, size_t capacity)
{
    if (!capacity) return;
    char reverse[24];
    size_t digits = 0;
    do {
        reverse[digits++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value && digits < sizeof(reverse));
    size_t at = 0;
    while (digits && at + 1 < capacity) output[at++] = reverse[--digits];
    output[at] = '\0';
}

static bool mount_path_in_use(const char* path)
{
    lock_vfs_metadata();
    bool inUse = false;
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i)
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0)
            inUse = true;
    unlock_vfs_metadata();
    return inUse;
}

bool propose_partition_mount_path(uint8_t blockDevIndex,
                                  uint16_t partitionNumber,
                                  char* outPath, size_t outPathSize)
{
    if (!outPath || outPathSize == 0 || partitionNumber == 0) return false;
    char base[VFS_MAX_PATH];
    char diskNumber[24], partNumber[24];
    append_decimal(blockDevIndex, diskNumber, sizeof(diskNumber));
    append_decimal(partitionNumber, partNumber, sizeof(partNumber));
    strcopy(base, "/mnt/disk", sizeof(base));
    const size_t baseLength = strlen(base);
    strcopy(base + baseLength, diskNumber, sizeof(base) - baseLength);
    size_t at = strlen(base);
    strcopy(base + at, "-part", sizeof(base) - at);
    at = strlen(base);
    strcopy(base + at, partNumber, sizeof(base) - at);

    for (uint8_t suffix = 1; suffix <= VFS_MAX_MOUNTS + 1; ++suffix) {
        char candidate[VFS_MAX_PATH];
        strcopy(candidate, base, sizeof(candidate));
        if (suffix > 1) {
            char suffixText[8];
            append_decimal(suffix, suffixText, sizeof(suffixText));
            at = strlen(candidate);
            strcopy(candidate + at, "-", sizeof(candidate) - at);
            at = strlen(candidate);
            strcopy(candidate + at, suffixText, sizeof(candidate) - at);
        }
        if (strlen(candidate) + 1 > outPathSize) return false;
        if (!partition_path_is_normalized(candidate)) return false;
        if (!mount_path_in_use(candidate)) {
            strcopy(outPath, candidate, outPathSize);
            return true;
        }
    }
    return false;
}

static bool mount_identity_valid_internal(uint8_t mountIndex)
{
    const MountPoint* mount = get_mount_by_index(mountIndex);
    if (!mount || mount->parentRegistrationId == 0 ||
        !block::registration_is_present(mount->blockDevIndex,
                                        mount->parentRegistrationId))
        return false;
    if (!mount->partitionMount) return true;
    if (!mount->partitionIdentity.valid ||
        !block::partition_view_parent_valid(mount->partitionView)) return false;
    storage::PartitionEntry current = {};
    if (!find_current_partition(mount->blockDevIndex,
            mount->partitionIdentity.partitionNumber,
            s_partitionTableScratch, current)) return false;
    return block::registration_is_present(mount->blockDevIndex,
               mount->parentRegistrationId) &&
        partition_entry_matches_identity(current, mount->partitionIdentity);
}

bool mount_identity_valid(uint8_t mountIndex)
{
    // Partition parsing uses the shared bounded table scratch. Join mount
    // admission so it cannot overlap another parse or a quiesced dismount.
    MountAdmissionReference admission;
    if (!admission.begin(true)) return false;
    MountOperationReference operation;
    return operation.begin_index(mountIndex) == VFS_OK &&
        mount_identity_valid_internal(mountIndex);
}

bool capture_mount_identity(uint8_t mountIndex, MountIdentity& out)
{
    out = {};
    if (mountIndex >= VFS_MAX_MOUNTS) return false;
    lock_vfs_metadata();
    const MountPoint& mount = s_mounts[mountIndex];
    if (!mount.active ||
        mount.lifecycleState != MOUNT_LIFECYCLE_MOUNTED ||
        mount.mountGeneration == 0 || mount.parentRegistrationId == 0 ||
        mount.logicalSectorSize == 0) {
        unlock_vfs_metadata();
        return false;
    }
    MountIdentity identity = {};
    identity.valid = true;
    identity.mountIndex = mountIndex;
    identity.mountGeneration = mount.mountGeneration;
    identity.blockDevIndex = mount.blockDevIndex;
    identity.registrationId = mount.parentRegistrationId;
    identity.registryGeneration = mount.parentRegistryGeneration;
    identity.logicalSectorSize = mount.logicalSectorSize;
    identity.partitionMount = mount.partitionMount;
    identity.partitionIdentity = mount.partitionIdentity;
    identity.partitionView = mount.partitionView;
    unlock_vfs_metadata();
    out = identity;
    return true;
}

bool copy_mount_snapshot(uint8_t mountIndex, MountPoint& out)
{
    out = {};
    if (mountIndex >= VFS_MAX_MOUNTS) return false;
    lock_vfs_metadata();
    const MountPoint& mount = s_mounts[mountIndex];
    if (!mount.active) {
        unlock_vfs_metadata();
        return false;
    }
    out = mount;
    unlock_vfs_metadata();
    return true;
}

static Status mount_io_status(const MountPoint* mount)
{
    if (!mount || !mount->active) return VFS_ERR_NOT_MOUNT;
    block::BlockDevice parent = {};
    if (!block::copy_device(mount->blockDevIndex, parent) ||
        parent.registrationId != mount->parentRegistrationId)
        return VFS_ERR_DEVICE_REMOVED;
    if (mount->partitionMount &&
        (!mount->partitionIdentity.valid ||
         !block::partition_view_parent_valid(mount->partitionView)))
        return VFS_ERR_MOUNT_STALE;
    return VFS_OK;
}

static bool mount_matches_identity_locked(const MountPoint& mount,
                                          const MountIdentity& identity)
{
    if (!identity.valid || !mount.active ||
        mount.mountGeneration != identity.mountGeneration ||
        mount.blockDevIndex != identity.blockDevIndex ||
        mount.parentRegistrationId != identity.registrationId ||
        mount.parentRegistryGeneration != identity.registryGeneration ||
        mount.logicalSectorSize != identity.logicalSectorSize ||
        mount.partitionMount != identity.partitionMount)
        return false;
    if (!identity.partitionMount) return true;
    return block::same_partition_identity(mount.partitionIdentity,
                                          identity.partitionIdentity) &&
        mount.partitionView.slot == identity.partitionView.slot &&
        mount.partitionView.generation == identity.partitionView.generation;
}

static bool protected_system_mount_path(const char* path)
{
    return path && (strcmp(path, "/") == 0 ||
        strcmp(path, "/boot") == 0 || strcmp(path, "/system") == 0);
}

class StorageOperationLeaseReference {
public:
    StorageOperationLeaseReference() : acquired_(false) { lease_ = {}; }
    ~StorageOperationLeaseReference()
    {
        if (acquired_) (void)storage::release_storage_operation(lease_);
    }

    storage::StorageOperationLockStatus acquire()
    {
        const storage::StorageOperationLockStatus status =
            storage::try_acquire_storage_operation(lease_);
        acquired_ = status == storage::STORAGE_OPERATION_LOCK_ACQUIRED;
        return status;
    }

    bool pin(const storage::TargetIdentity& target)
    {
        return acquired_ && storage::pin_storage_operation_target(lease_, target);
    }

    bool current() const
    {
        return acquired_ && storage::storage_operation_lease_is_current(lease_);
    }

private:
    storage::StorageOperationLease lease_;
    bool acquired_;
};

static Status mount_backing_status_internal(uint8_t mountIndex)
{
    const MountPoint* mount = get_mount_by_index(mountIndex);
    if (!mount || !mount->active) return VFS_ERR_NOT_MOUNT;
    const Status ioStatus = mount_io_status(mount);
    if (ioStatus != VFS_OK) return ioStatus;
    if (mount->partitionMount && !mount_identity_valid_internal(mountIndex))
        return VFS_ERR_MOUNT_STALE;
    return VFS_OK;
}

Status mount_backing_status(uint8_t mountIndex)
{
    MountAdmissionReference admission;
    if (!admission.begin(true)) return VFS_ERR_BUSY;
    MountOperationReference operation;
    const Status admitted = operation.begin_index(mountIndex);
    return admitted == VFS_OK
        ? mount_backing_status_internal(mountIndex) : admitted;
}

const char* partition_mount_error_name(PartitionMountError error)
{
    switch (error) {
        case PARTITION_MOUNT_OK: return "Mounted";
        case PARTITION_MOUNT_INVALID_ARGUMENT: return "Invalid mount request";
        case PARTITION_MOUNT_BAD_PATH: return "Invalid or non-normalized mount path";
        case PARTITION_MOUNT_PATH_OCCUPIED: return "Mount path is already in use";
        case PARTITION_MOUNT_DEVICE_UNAVAILABLE: return "Parent disk is unavailable";
        case PARTITION_MOUNT_TABLE_INVALID: return "Partition table is invalid or unsupported";
        case PARTITION_MOUNT_PARTITION_MISSING: return "Partition was not found";
        case PARTITION_MOUNT_PARTITION_CHANGED: return "Partition identity changed";
        case PARTITION_MOUNT_ALREADY_MOUNTED: return "Partition is already mounted";
        case PARTITION_MOUNT_VIEW_REGISTRY_FULL: return "Partition view registry is full";
        case PARTITION_MOUNT_VFS_REGISTRY_FULL: return "VFS mount table is full";
        case PARTITION_MOUNT_FILESYSTEM_UNRECOGNIZED:
            return "Filesystem is not recognized or is invalid";
        case PARTITION_MOUNT_FILESYSTEM_UNSUPPORTED:
            return "Filesystem is not supported for partition mounting";
        case PARTITION_MOUNT_OPERATION_BUSY:
            return "A storage operation is in progress";
        default: return "Unknown mount error";
    }
}

static Status map_block_status(block::Status status)
{
    switch (status) {
        case block::BLOCK_OK: return VFS_OK;
        case block::BLOCK_ERR_NO_MEDIA: return VFS_ERR_DEVICE_REMOVED;
        case block::BLOCK_ERR_NOT_READY: return VFS_ERR_NOT_READY;
        case block::BLOCK_ERR_TIMEOUT: return VFS_ERR_IO_TIMEOUT;
        case block::BLOCK_ERR_UNSUPPORTED: return VFS_ERR_NOT_SUPPORTED;
        case block::BLOCK_ERR_READ_ONLY: return VFS_ERR_READ_ONLY;
        case block::BLOCK_ERR_WRITE_UNCERTAIN: return VFS_ERR_WRITE_UNCERTAIN;
        case block::BLOCK_ERR_DURABILITY_UNVERIFIED: return VFS_ERR_DURABILITY_UNVERIFIED;
        case block::BLOCK_ERR_INVALID: return VFS_ERR_INVALID;
        case block::BLOCK_ERR_IO:
        default: return VFS_ERR_IO;
    }
}

uint8_t mount_alias(const char* path, const char* sourcePath)
{
    if (!s_initialized) {
        init();
    }

    if (!path || strlen(path) == 0 || !sourcePath || strlen(sourcePath) == 0) {
        return 0xFF;
    }
    if (storage::storage_operation_active()) return 0xFF;

    bool pathAlreadyMounted = false;
    lock_vfs_metadata();
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0) {
            pathAlreadyMounted = true;
        }
    }
    unlock_vfs_metadata();
    if (pathAlreadyMounted) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] ERROR: Alias path already mounted\n");
#endif
        return 0xFF;
    }

    MountOperationReference sourceReference;
    if (sourceReference.begin_path(sourcePath) != VFS_OK) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] ERROR: No source mount point for alias path\n");
#endif
        return 0xFF;
    }
    MountPoint* sourceMount = sourceReference.mount();

    FileInfo info{};
    if (stat(sourcePath, &info) != VFS_OK || info.type != FILE_TYPE_DIRECTORY) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] ERROR: Alias source path is not a directory\n");
#endif
        return 0xFF;
    }

    const char* sourceRelative = get_relative_path(sourcePath, sourceMount);
    if (!sourceRelative) {
        return 0xFF;
    }

    uint8_t index = 0xFF;
    if (mount_io_status(sourceMount) != VFS_OK) return 0xFF;
    if (sourceMount->partitionMount &&
        !block::retain_partition_view(sourceMount->partitionView))
        return 0xFF;

    lock_vfs_metadata();
    bool pathOccupied = false;
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0)
            pathOccupied = true;
        if (!s_mounts[i].active && index == 0xFF) index = i;
    }
    if (pathOccupied || index == 0xFF || !sourceMount->active ||
        sourceMount->lifecycleState != MOUNT_LIFECYCLE_MOUNTED ||
        s_nextMountGeneration == UINT64_MAX) {
        unlock_vfs_metadata();
        if (sourceMount->partitionMount)
            block::release_partition_view(sourceMount->partitionView);
        return 0xFF;
    }
    MountPoint& mp = s_mounts[index];
    memzero(&mp, sizeof(mp));
    mp.active = true;
    if (!initialize_mount_instance(mp)) {
        memzero(&mp, sizeof(mp));
        unlock_vfs_metadata();
        if (sourceMount->partitionMount)
            block::release_partition_view(sourceMount->partitionView);
        return 0xFF;
    }
    strcopy(mp.path, path, sizeof(mp.path));
    mp.fsType = sourceMount->fsType;
    mp.blockDevIndex = sourceMount->blockDevIndex;
    mp.parentRegistrationId = sourceMount->parentRegistrationId;
    mp.parentRegistryGeneration = sourceMount->parentRegistryGeneration;
    mp.logicalSectorSize = sourceMount->logicalSectorSize;
    mp.fsVolumeIndex = sourceMount->fsVolumeIndex;
    mp.readOnly = sourceMount->readOnly;
    mp.alias = true;
    mp.partitionMount = sourceMount->partitionMount;
    mp.partitionIdentity = sourceMount->partitionIdentity;
    mp.partitionView = sourceMount->partitionView;
    strcopy(mp.sourcePrefix, sourceRelative, sizeof(mp.sourcePrefix));

    ++s_mountCount;
    unlock_vfs_metadata();

#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[VFS] Mounted alias '");
    serial::puts(path);
    serial::puts("' -> '");
    serial::puts(sourcePath);
    serial::puts("'\n");
#endif

    return index;
}

Status unmount(const char* path)
{
    if (!path) return VFS_ERR_INVALID;
    MountIdentity identity = {};
    uint8_t index = 0xFF;
    lock_vfs_metadata();
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0) {
            index = i;
            break;
        }
    }
    const bool quiescing = index != 0xFF &&
        s_mounts[index].lifecycleState == MOUNT_LIFECYCLE_QUIESCING;
    unlock_vfs_metadata();
    if (index == 0xFF) return VFS_ERR_NOT_MOUNT;
    if (quiescing) return VFS_ERR_BUSY;
    if (protected_system_mount_path(path)) return VFS_ERR_BUSY;
    if (!capture_mount_identity(index, identity)) return VFS_ERR_BUSY;
    return dismount(identity);
}

Status dismount(const MountIdentity& identity)
{
    if (!identity.valid || identity.mountIndex >= VFS_MAX_MOUNTS ||
        identity.registrationId == 0 || identity.mountGeneration == 0 ||
        identity.logicalSectorSize == 0)
        return VFS_ERR_INVALID;

    MountPoint snapshot = {};
    lock_vfs_metadata();
    MountPoint& current = s_mounts[identity.mountIndex];
    if (!mount_matches_identity_locked(current, identity)) {
        unlock_vfs_metadata();
        return VFS_ERR_MOUNT_STALE;
    }
    if (current.lifecycleState != MOUNT_LIFECYCLE_MOUNTED) {
        unlock_vfs_metadata();
        return VFS_ERR_BUSY;
    }
    snapshot = current;
    unlock_vfs_metadata();

    if (protected_system_mount_path(snapshot.path)) return VFS_ERR_BUSY;

    block::BlockDevice device = {};
    const bool exactDeviceOnline = block::copy_device(
            identity.blockDevIndex, device) &&
        device.registrationId == identity.registrationId &&
        device.sectorSize == identity.logicalSectorSize;

    StorageOperationLeaseReference operationLease;
    storage::TargetIdentity target = {};
    if (exactDeviceOnline) {
        if (!storage::capture_target_identity(identity.blockDevIndex, target) ||
            target.registrationId != identity.registrationId ||
            target.logicalSectorSize != identity.logicalSectorSize)
            return VFS_ERR_DEVICE_REMOVED;
        const storage::BootProtection boot = storage::query_boot_protection(target);
        if (boot.safety != storage::BOOT_DEVICE_DEFINITELY_NOT_TARGET)
            return VFS_ERR_PROTECTED;
    }
    // Own the shared gate even for removal cleanup. This prevents a new mount
    // on a same-index replacement from racing release of the old FAT volume;
    // stale cleanup itself performs no device I/O and does not pin a new disk.
    if (operationLease.acquire() != storage::STORAGE_OPERATION_LOCK_ACQUIRED)
        return VFS_ERR_BUSY;
    if (exactDeviceOnline && !operationLease.pin(target))
        return VFS_ERR_DEVICE_REMOVED;

    // Quiescence is the admission boundary. Existing operations and persistent
    // handles are counted under the same lock used by open/readdir admission.
    lock_vfs_metadata();
    MountPoint& mount = s_mounts[identity.mountIndex];
    if (!mount_matches_identity_locked(mount, identity) ||
        mount.lifecycleState != MOUNT_LIFECYCLE_MOUNTED) {
        unlock_vfs_metadata();
        return VFS_ERR_MOUNT_STALE;
    }
    if (s_mountAdmissionsInFlight != 0) {
        unlock_vfs_metadata();
        return VFS_ERR_BUSY;
    }
    if (!mount.alias) {
        for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
            if (s_mounts[i].active && s_mounts[i].alias &&
                s_mounts[i].blockDevIndex == mount.blockDevIndex &&
                s_mounts[i].fsVolumeIndex == mount.fsVolumeIndex) {
                unlock_vfs_metadata();
                return VFS_ERR_BUSY;
            }
        }
    }
    mount.lifecycleState = MOUNT_LIFECYCLE_QUIESCING;
    bool hasOpenFiles = mount.openFileReferences != 0;
    bool hasOpenDirectories = mount.openDirectoryReferences != 0;
    for (uint8_t i = 0; i < VFS_MAX_OPEN_FILES; ++i) {
        if (s_files[i].open && s_files[i].mountIndex == identity.mountIndex)
            hasOpenFiles = true;
        if (s_dirs[i].active && s_dirs[i].mountIndex == identity.mountIndex)
            hasOpenDirectories = true;
    }
    if (mount.activeOperations != 0 || hasOpenFiles || hasOpenDirectories) {
        // No filesystem teardown or writes have occurred; reopening admission
        // is a proven rollback for a busy attempt.
        mount.lifecycleState = MOUNT_LIFECYCLE_MOUNTED;
        unlock_vfs_metadata();
        return VFS_ERR_BUSY;
    }
    unlock_vfs_metadata();

    Status cleanupStatus = exactDeviceOnline
        ? mount_backing_status_internal(identity.mountIndex)
        : VFS_ERR_DEVICE_REMOVED;
    if (cleanupStatus == VFS_OK && !snapshot.alias && !snapshot.readOnly &&
        (snapshot.fsType == FS_TYPE_FAT32 ||
         snapshot.fsType == FS_TYPE_EXFAT)) {
        block::Status blockStatus = block::BLOCK_OK;
        if (!fs_fat::flush(snapshot.fsVolumeIndex, &blockStatus))
            cleanupStatus = map_block_status(blockStatus);
    }

    if (cleanupStatus == VFS_OK && exactDeviceOnline &&
        !operationLease.current())
        cleanupStatus = VFS_ERR_DEVICE_REMOVED;

    if (cleanupStatus != VFS_OK && exactDeviceOnline) {
        const Status currentBacking =
            mount_backing_status_internal(identity.mountIndex);
        if (currentBacking == VFS_OK) {
            lock_vfs_metadata();
            MountPoint& rollback = s_mounts[identity.mountIndex];
            if (mount_matches_identity_locked(rollback, identity) &&
                rollback.lifecycleState == MOUNT_LIFECYCLE_QUIESCING &&
                rollback.activeOperations == 0 &&
                rollback.openFileReferences == 0 &&
                rollback.openDirectoryReferences == 0)
                rollback.lifecycleState = MOUNT_LIFECYCLE_MOUNTED;
            unlock_vfs_metadata();
            return cleanupStatus;
        }
        cleanupStatus = currentBacking;
    }

    bool filesystemDetached = true;
    if (!snapshot.alias) {
        switch (snapshot.fsType) {
            case FS_TYPE_FAT32:
            case FS_TYPE_EXFAT:
                filesystemDetached = fs_fat::unmount(snapshot.fsVolumeIndex);
                break;
            case FS_TYPE_EXT2:
            case FS_TYPE_EXT4:
                fs_ext4::unmount(snapshot.fsVolumeIndex);
                break;
            default:
                break;
        }
    }
    if (!filesystemDetached) {
        lock_vfs_metadata();
        MountPoint& rollback = s_mounts[identity.mountIndex];
        if (mount_matches_identity_locked(rollback, identity) &&
            rollback.lifecycleState == MOUNT_LIFECYCLE_QUIESCING)
            rollback.lifecycleState = MOUNT_LIFECYCLE_MOUNTED;
        unlock_vfs_metadata();
        return VFS_ERR_BUSY;
    }

    if (snapshot.partitionMount)
        block::release_partition_view(snapshot.partitionView);
    lock_vfs_metadata();
    MountPoint& detached = s_mounts[identity.mountIndex];
    if (!mount_matches_identity_locked(detached, identity) ||
        detached.lifecycleState != MOUNT_LIFECYCLE_QUIESCING ||
        detached.activeOperations != 0 ||
        detached.openFileReferences != 0 ||
        detached.openDirectoryReferences != 0) {
        unlock_vfs_metadata();
        // This is an internal invariant failure after filesystem teardown.
        // Keep the slot non-operational instead of publishing a false mount.
        return VFS_ERR_MOUNT_STALE;
    }
    memzero(&detached, sizeof(detached));
    if (s_mountCount != 0) --s_mountCount;
    unlock_vfs_metadata();

#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[VFS] Dismounted '");
    serial::puts(snapshot.path);
    serial::puts("'\n");
#endif
    return cleanupStatus;
}

const MountPoint* get_mount(const char* path)
{
    lock_vfs_metadata();
    MountPoint* mount = find_mount_for_path(path);
    unlock_vfs_metadata();
    return mount;
}

uint8_t mount_index_for_path(const char* path)
{
    lock_vfs_metadata();
    MountPoint* mount = find_mount_for_path(path);
    const uint8_t index = mount ? static_cast<uint8_t>(mount - s_mounts) : 0xFF;
    unlock_vfs_metadata();
    return index;
}

const MountPoint* get_mount_by_index(uint8_t index)
{
    if (index >= VFS_MAX_MOUNTS) return nullptr;
    lock_vfs_metadata();
    const MountPoint* mount = s_mounts[index].active ? &s_mounts[index] : nullptr;
    unlock_vfs_metadata();
    return mount;
}

uint8_t mount_count()
{
    lock_vfs_metadata();
    const uint8_t count = s_mountCount;
    unlock_vfs_metadata();
    return count;
}

#if defined(KERNEL_STORAGE_TEST)
void test_set_mount(uint8_t index, bool active, uint8_t deviceIndex,
                    const char* path)
{
    if (index >= VFS_MAX_MOUNTS) return;
    lock_vfs_metadata();
    if (s_mounts[index].active && s_mountCount) --s_mountCount;
    memzero(&s_mounts[index], sizeof(s_mounts[index]));
    s_mounts[index].active = active;
    s_mounts[index].blockDevIndex = deviceIndex;
    block::BlockDevice device = {};
    if (active && block::copy_device(deviceIndex, device)) {
        s_mounts[index].parentRegistrationId = device.registrationId;
        s_mounts[index].parentRegistryGeneration = block::registry_generation();
        s_mounts[index].logicalSectorSize = device.sectorSize;
    }
    if (active) (void)initialize_mount_instance(s_mounts[index]);
    if (path) strcopy(s_mounts[index].path, path,
                      sizeof(s_mounts[index].path));
    if (active && s_mountCount < VFS_MAX_MOUNTS) ++s_mountCount;
    s_initialized = true;
    unlock_vfs_metadata();
}

void test_clear_mounts()
{
    lock_vfs_metadata();
    for (size_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!s_mounts[i].active) continue;
        if (!s_mounts[i].alias) {
            if (s_mounts[i].fsType == FS_TYPE_FAT32 ||
                s_mounts[i].fsType == FS_TYPE_EXFAT)
                (void)fs_fat::unmount(s_mounts[i].fsVolumeIndex);
            else if (s_mounts[i].fsType == FS_TYPE_EXT2 ||
                     s_mounts[i].fsType == FS_TYPE_EXT4)
                fs_ext4::unmount(s_mounts[i].fsVolumeIndex);
        }
        if (s_mounts[i].partitionMount)
            block::release_partition_view(s_mounts[i].partitionView);
        memzero(&s_mounts[i], sizeof(s_mounts[i]));
    }
    memzero(s_files, sizeof(s_files));
    memzero(s_dirs, sizeof(s_dirs));
    s_mountCount = 0;
    s_initialized = true;
    unlock_vfs_metadata();
}
#endif

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

HandleToken open(const char* path, uint16_t flags)
{
    if (!path) return INVALID_HANDLE;
    MountOperationReference operation;
    if (operation.begin_path(path) != VFS_OK) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] ERROR: No mount point for path: ");
        serial::puts(path);
        serial::puts("\n");
#endif
        return INVALID_HANDLE;
    }
    MountPoint* mount = operation.mount();
    if (mount_io_status(mount) != VFS_OK) return INVALID_HANDLE;
    
    FileSlotReservation reservation;
    if (!reservation.reserve()) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] ERROR: No free file handles\n");
#endif
        return INVALID_HANDLE;
    }
    
    // Get path relative to mount point
    char resolvedPath[VFS_MAX_PATH];
    const char* relPath = resolve_relative_path(path, mount, resolvedPath, sizeof(resolvedPath));
    
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
                if ((flags & OPEN_CREATE) && (flags & OPEN_EXCL)) {
                    return INVALID_HANDLE;
                }
                // Check if it's a directory when we want a file
                if (entry.isDir && (flags & OPEN_WRITE)) {
                    // Cannot open directory for writing
#if defined(__GNUC__) || defined(__clang__)
                    serial::puts("[VFS] ERROR: Cannot open directory for writing\n");
#endif
                    return INVALID_HANDLE;
                }
                
            } else {
                // File not found - check if we should create it
                if (flags & OPEN_CREATE) {
                    block::Status blockStatus = block::BLOCK_OK;
                    const fs_fat::FileWriteStatus createStatus =
                        fs_fat::create_file_path_status(mount->fsVolumeIndex,
                                                        relPath, nullptr, 0,
                                                        &blockStatus);
                    if (createStatus != fs_fat::FILE_WRITE_OK) {
#if defined(__GNUC__) || defined(__clang__)
                        serial::puts("[VFS] ERROR: File creation failed status=");
                        serial::puts(fs_fat::file_write_status_name(createStatus));
                        serial::puts(" block=0x");
                        serial::put_hex8(static_cast<uint8_t>(blockStatus));
                        serial::puts("\n");
#endif
                        return INVALID_HANDLE;
                    }
                    if (!fs_fat::lookup_path(mount->fsVolumeIndex, relPath, &entry)) {
                        return INVALID_HANDLE;
                    }
                } else {
#if defined(__GNUC__) || defined(__clang__)
                    serial::puts("[VFS] ERROR: File not found: ");
                    serial::puts(path);
                    serial::puts("\n");
#endif
                    return INVALID_HANDLE;
                }
            }

            // Open the existing or newly-created file.
            if (entry.isDir && (flags & OPEN_WRITE)) return INVALID_HANDLE;
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
            break;
        }
            
        case FS_TYPE_EXT2:
        case FS_TYPE_EXT4: {
            // ext4 path lookup - simplified for now
            // TODO: Implement ext4::lookup_path similar to FAT
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS] ext4 path lookup not fully implemented\n");
#endif
            return INVALID_HANDLE;
        }
            
        default:
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS] ERROR: Unsupported filesystem type\n");
#endif
            return INVALID_HANDLE;
    }
    
    if (!found) {
        return INVALID_HANDLE;
    }

    HandleToken token = INVALID_HANDLE;
    if (!reservation.commit(operation, path, flags, fileSize, fsHandle, token)) {
        if (fsHandle != 0xFF) fs_fat::close_file(fsHandle);
        return INVALID_HANDLE;
    }
    return token;
}

Status close(HandleToken handle)
{
    MountOperationReference operation;
    const Status admitted = operation.begin_file(handle, true);
    if (admitted != VFS_OK) return admitted;
    FileHandle& fh = s_files[operation.file_slot()];
    MountPoint* mount = operation.mount();
    Status flushStatus = mount_io_status(mount);
    // Close via filesystem driver
    if (flushStatus == VFS_OK && (fh.flags & OPEN_WRITE) &&
        mount->fsType == FS_TYPE_FAT32) {
        block::Status blockStatus = block::BLOCK_OK;
        if (!fs_fat::flush(mount->fsVolumeIndex, &blockStatus)) {
            flushStatus = map_block_status(blockStatus);
        }
    }
    
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

    lock_vfs_metadata();
    if (fh.token == handle && fh.open) {
        fh.open = false;
        fh.closing = false;
        if (mount->active && mount->mountGeneration ==
                operation.mount()->mountGeneration &&
            mount->openFileReferences != 0)
            --mount->openFileReferences;
    }
    unlock_vfs_metadata();
    operation.release();
    return flushStatus;
}

int32_t read(HandleToken handle, void* buffer, uint32_t size)
{
    MountOperationReference operation;
    const Status admitted = operation.begin_file(handle);
    if (admitted != VFS_OK) return admitted;
    const uint8_t fileSlot = operation.file_slot();
    if (!buffer || size == 0) return VFS_ERR_INVALID;
    
    FileHandle& fh = s_files[fileSlot];
    
    if (!(fh.flags & OPEN_READ)) {
        return VFS_ERR_INVALID;
    }
    
    MountPoint* mount = operation.mount();
    const Status backingStatus = mount_io_status(mount);
    if (backingStatus != VFS_OK) return backingStatus;
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
    } else if (bytesRead == 0) {
        const block::Status ioStatus = fs_fat::last_io_status();
        if (ioStatus != block::BLOCK_OK) return map_block_status(ioStatus);
        const fs_fat::TraversalStatus traversal =
            fs_fat::last_traversal_status();
        if (traversal == fs_fat::TRAVERSAL_IO_ERROR)
            return map_block_status(ioStatus);
        if (traversal != fs_fat::TRAVERSAL_OK &&
            traversal != fs_fat::TRAVERSAL_END_OF_CHAIN &&
            traversal != fs_fat::TRAVERSAL_DIRECTORY_END)
            return VFS_ERR_CORRUPT_CHAIN;
    }
    
    return bytesRead;
}

int32_t write(HandleToken handle, const void* buffer, uint32_t size)
{
    MountOperationReference operation;
    const Status admitted = operation.begin_file(handle);
    if (admitted != VFS_OK) return admitted;
    const uint8_t fileSlot = operation.file_slot();
    if (!buffer || size == 0) return VFS_ERR_INVALID;
    
    FileHandle& fh = s_files[fileSlot];
    
    if (!(fh.flags & OPEN_WRITE)) {
        return VFS_ERR_INVALID;
    }
    
    MountPoint* mount = operation.mount();
    const Status backingStatus = mount_io_status(mount);
    if (backingStatus != VFS_OK) return backingStatus;
    
    if (mount->readOnly) {
        return VFS_ERR_READ_ONLY;
    }
    
    int32_t bytesWritten = VFS_ERR_NOT_SUPPORTED;

    switch (mount->fsType) {
        case FS_TYPE_FAT32:
            if (fh.fsFileHandle != 0xFF) {
                bytesWritten = static_cast<int32_t>(
                    fs_fat::write_file(fh.fsFileHandle, buffer, size));
            }
            break;

        default:
            return VFS_ERR_NOT_SUPPORTED;
    }

    if (bytesWritten > 0) {
        fh.position += static_cast<uint64_t>(bytesWritten);
        if (fh.position > fh.size) {
            if (fh.position > 0xFFFFFFFFull) return VFS_ERR_INVALID;
            char resolvedPath[VFS_MAX_PATH];
            const char* relPath = resolve_relative_path(fh.path, mount,
                                                         resolvedPath, sizeof(resolvedPath));
            block::Status blockStatus = block::BLOCK_OK;
            const fs_fat::FileWriteStatus sizeStatus =
                fs_fat::update_file_size_path_status(mount->fsVolumeIndex,
                                                     relPath,
                                                     static_cast<uint32_t>(fh.position),
                                                     &blockStatus);
            const Status mapped = map_fat_file_write_status(sizeStatus);
            if (mapped != VFS_OK) return mapped;
            fh.size = fh.position;
        }
    }
    if (bytesWritten == 0 && size != 0) {
        const block::Status ioStatus = fs_fat::last_io_status();
        if (ioStatus != block::BLOCK_OK) return map_block_status(ioStatus);
    }

    return bytesWritten;
}

Status seek(HandleToken handle, int64_t offset, SeekOrigin origin)
{
    MountOperationReference operation;
    const Status admitted = operation.begin_file(handle);
    if (admitted != VFS_OK) return admitted;
    const uint8_t fileSlot = operation.file_slot();
    
    FileHandle& fh = s_files[fileSlot];
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

    MountPoint* mount = operation.mount();
    const Status backingStatus = mount_io_status(mount);
    if (backingStatus != VFS_OK) return backingStatus;
    if (newPos > 0xFFFFFFFFll) return VFS_ERR_INVALID;
    switch (mount->fsType) {
        case FS_TYPE_FAT32:
        case FS_TYPE_EXFAT:
            if (fh.fsFileHandle == 0xFF ||
                !fs_fat::seek_file(fh.fsFileHandle, static_cast<uint32_t>(newPos))) {
                return VFS_ERR_IO;
            }
            break;
        default:
            return VFS_ERR_NOT_SUPPORTED;
    }

    fh.position = static_cast<uint64_t>(newPos);
    return VFS_OK;
}

int64_t tell(HandleToken handle)
{
    MountOperationReference operation;
    if (operation.begin_file(handle) != VFS_OK) return -1;
    return static_cast<int64_t>(s_files[operation.file_slot()].position);
}

int64_t file_size(HandleToken handle)
{
    MountOperationReference operation;
    if (operation.begin_file(handle) != VFS_OK) return -1;
    return static_cast<int64_t>(s_files[operation.file_slot()].size);
}

Status flush(HandleToken handle)
{
    MountOperationReference operation;
    const Status admitted = operation.begin_file(handle);
    if (admitted != VFS_OK) return admitted;
    MountPoint* mount = operation.mount();
    const Status backingStatus = mount_io_status(mount);
    if (backingStatus != VFS_OK) return backingStatus;
    if (mount->fsType != FS_TYPE_FAT32) return VFS_OK;
    block::Status blockStatus = block::BLOCK_OK;
    return fs_fat::flush(mount->fsVolumeIndex, &blockStatus)
        ? VFS_OK : map_block_status(blockStatus);
}

const FileHandle* get_handle(HandleToken handle)
{
    static FileHandle snapshot;
    lock_vfs_metadata();
    const uint8_t fileSlot = find_file_slot(handle);
    if (fileSlot == 0xFF) {
        unlock_vfs_metadata();
        return nullptr;
    }
    snapshot = s_files[fileSlot];
    unlock_vfs_metadata();
    return &snapshot;
}

// ================================================================
// Public API — Directory Operations
// ================================================================

HandleToken opendir(const char* path)
{
    if (!path) return INVALID_HANDLE;
    MountOperationReference operation;
    if (operation.begin_path(path) != VFS_OK) return INVALID_HANDLE;
    MountPoint* mount = operation.mount();
    if (mount_io_status(mount) != VFS_OK) return INVALID_HANDLE;
    DirectorySlotReservation reservation;
    if (!reservation.reserve()) return INVALID_HANDLE;
    
    // Get path relative to mount point
    char resolvedPath[VFS_MAX_PATH];
    const char* relPath = resolve_relative_path(path, mount, resolvedPath, sizeof(resolvedPath));
    
    switch (mount->fsType) {
        case FS_TYPE_FAT32:
        case FS_TYPE_EXFAT: {
            // If path is root or empty, open root directory
            if (!relPath || relPath[0] == '\0' || 
                (relPath[0] == '/' && relPath[1] == '\0')) {
                if (!fs_fat::open_root_dir(mount->fsVolumeIndex)) {
                    return INVALID_HANDLE;
                }
            } else {
                // Lookup the directory by path
                fs_fat::DirEntry dirEntry;
                if (!fs_fat::lookup_path(mount->fsVolumeIndex, relPath, &dirEntry)) {
                    return INVALID_HANDLE;  // Directory not found
                }
                
                // Must be a directory
                if (!dirEntry.isDir) {
                    return INVALID_HANDLE;  // Not a directory
                }
                
                // Open the directory by its cluster
                if (!fs_fat::open_dir(mount->fsVolumeIndex, dirEntry.firstCluster)) {
                    return INVALID_HANDLE;
                }
            }
            break;
        }
            
        case FS_TYPE_EXT2:
        case FS_TYPE_EXT4:
            // ext4 subdirectory support not yet implemented
            if (!fs_ext4::open_root_dir(mount->fsVolumeIndex)) {
                return 0xFF;
            }
            break;
            
        default:
            return INVALID_HANDLE;
    }

    HandleToken token = INVALID_HANDLE;
    if (!reservation.commit(operation, path, token)) {
        if (mount->fsType == FS_TYPE_FAT32 || mount->fsType == FS_TYPE_EXFAT)
            fs_fat::close_dir(mount->fsVolumeIndex);
        return INVALID_HANDLE;
    }
    return token;
}

bool readdir(HandleToken iterator, DirEntry* entry)
{
    bool hasEntry = false;
    return readdir_detailed(iterator, entry, hasEntry) == VFS_OK && hasEntry;
}

Status readdir_detailed(HandleToken iterator, DirEntry* entry, bool& hasEntry)
{
    hasEntry = false;
    MountOperationReference operation;
    const Status admitted = operation.begin_directory(iterator);
    if (admitted != VFS_OK) return admitted;
    if (!entry) return VFS_ERR_INVALID;

    DirIterator& di = s_dirs[operation.directory_slot()];
    MountPoint* mount = operation.mount();
    const Status backingStatus = mount_io_status(mount);
    if (backingStatus != VFS_OK) return backingStatus;
    
    switch (mount->fsType) {
        case FS_TYPE_FAT32:
        case FS_TYPE_EXFAT: {
            fs_fat::DirEntry fatEntry;
            if (!fs_fat::read_dir(mount->fsVolumeIndex, &fatEntry)) {
                const block::Status ioStatus = fs_fat::last_io_status();
                if (ioStatus != block::BLOCK_OK)
                    return map_block_status(ioStatus);
                const fs_fat::TraversalStatus traversal =
                    fs_fat::last_traversal_status();
                if (traversal == fs_fat::TRAVERSAL_OK ||
                    traversal == fs_fat::TRAVERSAL_END_OF_CHAIN ||
                    traversal == fs_fat::TRAVERSAL_DIRECTORY_END)
                    return VFS_OK;
                if (traversal == fs_fat::TRAVERSAL_IO_ERROR)
                    return map_block_status(ioStatus);
                return VFS_ERR_CORRUPT_DIRECTORY;
            }
            
            strcopy(entry->name, fatEntry.name, sizeof(entry->name));
            entry->type = fatEntry.isDir ? FILE_TYPE_DIRECTORY : FILE_TYPE_REGULAR;
            entry->size = fatEntry.fileSize;
            entry->isHidden = (fatEntry.attr & 0x02) != 0;
            entry->isSystem = (fatEntry.attr & 0x04) != 0;
            entry->isReadOnly = (fatEntry.attr & 0x01) != 0;
            ++di.index;
            hasEntry = true;
            return VFS_OK;
        }
        
        case FS_TYPE_EXT2:
        case FS_TYPE_EXT4: {
            fs_ext4::Ext4DirEntry extEntry;
            if (!fs_ext4::read_dir(mount->fsVolumeIndex, &extEntry)) {
                return VFS_OK;
            }
            
            strcopy(entry->name, extEntry.name, sizeof(entry->name));
            entry->type = extEntry.isDir ? FILE_TYPE_DIRECTORY : FILE_TYPE_REGULAR;
            entry->size = 0; // ext4 DirEntry does not include size
            entry->isHidden = (entry->name[0] == '.');
            entry->isSystem = false;
            entry->isReadOnly = false;
            ++di.index;
            hasEntry = true;
            return VFS_OK;
        }
        
        default:
            return VFS_ERR_NOT_SUPPORTED;
    }
}

Status closedir(HandleToken iterator)
{
    MountOperationReference operation;
    const Status admitted = operation.begin_directory(iterator, true);
    if (admitted != VFS_OK) return admitted;
    DirIterator& directory = s_dirs[operation.directory_slot()];
    MountPoint* mount = operation.mount();
    const Status backingStatus = mount_io_status(mount);
    if (mount->fsType == FS_TYPE_FAT32 || mount->fsType == FS_TYPE_EXFAT)
        fs_fat::close_dir(mount->fsVolumeIndex);
    lock_vfs_metadata();
    if (directory.token == iterator && directory.active) {
        directory.active = false;
        directory.closing = false;
        if (mount->active && mount->mountGeneration ==
                operation.mount()->mountGeneration &&
            mount->openDirectoryReferences != 0)
            --mount->openDirectoryReferences;
    }
    unlock_vfs_metadata();
    operation.release();
    return backingStatus;
}

Status mkdir(const char* path)
{
    if (!path) return VFS_ERR_INVALID;
    MountOperationReference operation;
    const Status admitted = operation.begin_path(path);
    if (admitted != VFS_OK) return admitted;

    FileInfo existing{};
    if (stat(path, &existing) == VFS_OK) return VFS_ERR_EXISTS;

    char parentPath[VFS_MAX_PATH];
    parent_path(path, parentPath, sizeof(parentPath));
    FileInfo parentInfo{};
    Status parentStatus = stat(parentPath, &parentInfo);
    if (parentStatus != VFS_OK) return parentStatus;
    if (parentInfo.type != FILE_TYPE_DIRECTORY) return VFS_ERR_NOT_DIR;

    MountPoint* mount = operation.mount();
    const Status backingStatus = mount_io_status(mount);
    if (backingStatus != VFS_OK) return backingStatus;
    if (mount->readOnly) return VFS_ERR_READ_ONLY;

    char resolvedPath[VFS_MAX_PATH];
    const char* relPath = resolve_relative_path(path, mount, resolvedPath, sizeof(resolvedPath));
    switch (mount->fsType) {
        case FS_TYPE_FAT32: {
            block::Status blockStatus = block::BLOCK_OK;
            const fs_fat::DirectoryCreateStatus fatStatus =
                fs_fat::create_directory_path_status(mount->fsVolumeIndex, relPath, &blockStatus);
            Status result = VFS_ERR_NOT_SUPPORTED;
            switch (fatStatus) {
                case fs_fat::DIRECTORY_CREATE_OK: result = VFS_OK; break;
                case fs_fat::DIRECTORY_CREATE_ALREADY_EXISTS: result = VFS_ERR_EXISTS; break;
                case fs_fat::DIRECTORY_CREATE_PARENT_NOT_FOUND: result = VFS_ERR_NOT_FOUND; break;
                case fs_fat::DIRECTORY_CREATE_INVALID_NAME:
                case fs_fat::DIRECTORY_CREATE_INVALID_ARGUMENT: result = VFS_ERR_INVALID; break;
                case fs_fat::DIRECTORY_CREATE_NO_FREE_CLUSTER:
                case fs_fat::DIRECTORY_CREATE_NO_FREE_ENTRY: result = VFS_ERR_NO_SPACE; break;
                case fs_fat::DIRECTORY_CREATE_NOT_MOUNTED: result = VFS_ERR_NOT_MOUNT; break;
                case fs_fat::DIRECTORY_CREATE_IO_ERROR:
                    result = map_block_status(blockStatus); break;
                default: result = VFS_ERR_NOT_SUPPORTED; break;
            }
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS_MKDIR] path=");
            serial::puts(path);
            serial::puts(" fs=");
            serial::puts(fs_type_name(mount->fsType));
            serial::puts(" fatStatus=");
            serial::puts(fs_fat::directory_create_status_name(fatStatus));
            serial::puts(" blockStatus=0x");
            serial::put_hex8(static_cast<uint8_t>(blockStatus));
            serial::puts("(");
            serial::puts(block_status_name(blockStatus));
            serial::puts(")");
            serial::puts(" vfsStatus=");
            serial::puts(status_name(result));
            serial::puts("\n");
#endif
            return result;
        }

        default:
            break;
    }

    return VFS_ERR_NOT_SUPPORTED;
}

static Status status_for_fat_delete(bool succeeded)
{
    if (succeeded) return VFS_OK;
    switch (fs_fat::last_delete_status()) {
        case fs_fat::DELETE_NOT_FOUND: return VFS_ERR_NOT_FOUND;
        case fs_fat::DELETE_WRONG_TYPE: return VFS_ERR_NOT_DIR;
        case fs_fat::DELETE_READ_ONLY: return VFS_ERR_READ_ONLY;
        case fs_fat::DELETE_DIRECTORY_NOT_EMPTY: return VFS_ERR_DIRECTORY_NOT_EMPTY;
        case fs_fat::DELETE_CORRUPT_DIRECTORY: return VFS_ERR_CORRUPT_DIRECTORY;
        case fs_fat::DELETE_CORRUPT_CHAIN: return VFS_ERR_CORRUPT_CHAIN;
        case fs_fat::DELETE_NOT_MOUNTED: return VFS_ERR_NOT_MOUNT;
        case fs_fat::DELETE_INVALID_ARGUMENT: return VFS_ERR_INVALID;
        case fs_fat::DELETE_IO_ERROR:
            return map_block_status(fs_fat::last_io_status());
        default: return VFS_ERR_IO;
    }
}

Status rmdir(const char* path)
{
    if (!path) return VFS_ERR_INVALID;

    MountOperationReference operation;
    const Status admitted = operation.begin_path(path);
    if (admitted != VFS_OK) return admitted;
    MountPoint* mount = operation.mount();
    const Status backingStatus = mount_io_status(mount);
    if (backingStatus != VFS_OK) return backingStatus;
    if (mount->readOnly) return VFS_ERR_READ_ONLY;

    char resolvedPath[VFS_MAX_PATH];
    const char* relPath = resolve_relative_path(path, mount, resolvedPath, sizeof(resolvedPath));
    switch (mount->fsType) {
        case FS_TYPE_FAT32:
            return status_for_fat_delete(
                fs_fat::delete_path(mount->fsVolumeIndex, relPath, true));

        default:
            break;
    }

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
    MountOperationReference operation;
    const Status admitted = operation.begin_path(path);
    if (admitted != VFS_OK) return admitted;
    MountPoint* mount = operation.mount();
    const Status backingStatus = mount_io_status(mount);
    if (backingStatus != VFS_OK) return backingStatus;
    
    // Get path relative to mount point
    char resolvedPath[VFS_MAX_PATH];
    const char* relPath = resolve_relative_path(path, mount, resolvedPath, sizeof(resolvedPath));
    
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
    if (!path) return VFS_ERR_INVALID;

    MountOperationReference operation;
    const Status admitted = operation.begin_path(path);
    if (admitted != VFS_OK) return admitted;
    MountPoint* mount = operation.mount();
    const Status backingStatus = mount_io_status(mount);
    if (backingStatus != VFS_OK) return backingStatus;
    if (mount->readOnly) return VFS_ERR_READ_ONLY;

    char resolvedPath[VFS_MAX_PATH];
    const char* relPath = resolve_relative_path(path, mount, resolvedPath, sizeof(resolvedPath));
    switch (mount->fsType) {
        case FS_TYPE_FAT32:
            return status_for_fat_delete(
                fs_fat::delete_path(mount->fsVolumeIndex, relPath, false));

        default:
            break;
    }

    return VFS_ERR_NOT_SUPPORTED;
}

Status rename(const char* oldPath, const char* newPath)
{
    if (!oldPath || !newPath) return VFS_ERR_INVALID;

    MountOperationReference oldOperation;
    MountOperationReference newOperation;
    const Status oldAdmission = oldOperation.begin_path(oldPath);
    if (oldAdmission != VFS_OK) return oldAdmission;
    const Status newAdmission = newOperation.begin_path(newPath);
    if (newAdmission != VFS_OK) return newAdmission;
    MountPoint* oldMount = oldOperation.mount();
    MountPoint* newMount = newOperation.mount();
    if (oldOperation.mount_index() != newOperation.mount_index())
        return VFS_ERR_INVALID;
    if (oldMount->readOnly) return VFS_ERR_READ_ONLY;

    char resolvedOldPath[VFS_MAX_PATH];
    char resolvedNewPath[VFS_MAX_PATH];
    const char* relOldPath = resolve_relative_path(oldPath, oldMount, resolvedOldPath, sizeof(resolvedOldPath));
    const char* relNewPath = resolve_relative_path(newPath, newMount, resolvedNewPath, sizeof(resolvedNewPath));
    switch (oldMount->fsType) {
        case FS_TYPE_FAT32:
            return fs_fat::rename_path(oldMount->fsVolumeIndex, relOldPath, relNewPath) ? VFS_OK : VFS_ERR_NOT_SUPPORTED;

        default:
            break;
    }

    return VFS_ERR_NOT_SUPPORTED;
}

// ================================================================
// Public API — High-Level Convenience Functions
// ================================================================

int32_t read_file(const char* path, void* buffer, uint32_t maxSize)
{
    const HandleToken handle = open(path, OPEN_READ);
    if (handle == INVALID_HANDLE) return VFS_ERR_NOT_FOUND;
    
    int32_t bytesRead = read(handle, buffer, maxSize);
    close(handle);
    
    return bytesRead;
}

int32_t write_file(const char* path, const void* buffer, uint32_t size)
{
    if (!path || (!buffer && size != 0)) return VFS_ERR_INVALID;

    MountOperationReference operation;
    const Status admitted = operation.begin_path(path);
    if (admitted != VFS_OK) return admitted;
    MountPoint* mount = operation.mount();
    const Status backingStatus = mount_io_status(mount);
    if (backingStatus != VFS_OK) return backingStatus;
    if (mount->readOnly) return VFS_ERR_READ_ONLY;

    char resolvedPath[VFS_MAX_PATH];
    const char* relPath = resolve_relative_path(path, mount, resolvedPath, sizeof(resolvedPath));
    switch (mount->fsType) {
        case FS_TYPE_FAT32: {
            block::Status blockStatus = block::BLOCK_OK;
            fs_fat::FileWriteStatus fatStatus = fs_fat::overwrite_path_status(
                mount->fsVolumeIndex, relPath, buffer, size, &blockStatus);
            const char* operation = "overwrite";
            if (fatStatus == fs_fat::FILE_WRITE_NOT_FOUND) {
                operation = "create";
                fatStatus = fs_fat::create_file_path_status(
                    mount->fsVolumeIndex, relPath, buffer, size, &blockStatus);
            }
            const Status result = map_fat_file_write_status(fatStatus);
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS_WRITE_FILE] path=");
            serial::puts(path);
            serial::puts(" operation=");
            serial::puts(operation);
            serial::puts(" bytes=");
            serial::put_hex64(size);
            serial::puts(" fatStatus=");
            serial::puts(fs_fat::file_write_status_name(fatStatus));
            serial::puts(" blockStatus=0x");
            serial::put_hex8(static_cast<uint8_t>(blockStatus));
            serial::puts(" vfsStatus=");
            serial::puts(status_name(result));
            serial::puts("\n");
#endif
            return result == VFS_OK ? static_cast<int32_t>(size) : result;
        }

        default:
            return VFS_ERR_NOT_SUPPORTED;
    }
}

int32_t create_file(const char* path, const void* buffer, uint32_t size)
{
    if (!path || (!buffer && size != 0)) return VFS_ERR_INVALID;

    MountOperationReference operation;
    const Status admitted = operation.begin_path(path);
    if (admitted != VFS_OK) return admitted;
    MountPoint* mount = operation.mount();
    const Status backingStatus = mount_io_status(mount);
    if (backingStatus != VFS_OK) return backingStatus;
    if (mount->readOnly) return VFS_ERR_READ_ONLY;

    char resolvedPath[VFS_MAX_PATH];
    const char* relPath = resolve_relative_path(path, mount, resolvedPath, sizeof(resolvedPath));
    switch (mount->fsType) {
        case FS_TYPE_FAT32: {
            block::Status blockStatus = block::BLOCK_OK;
            const fs_fat::FileWriteStatus fatStatus = fs_fat::create_file_path_status(
                mount->fsVolumeIndex, relPath, buffer, size, &blockStatus);
            const Status result = map_fat_file_write_status(fatStatus);
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS_CREATE_FILE] path=");
            serial::puts(path);
            serial::puts(" bytes=");
            serial::put_hex64(size);
            serial::puts(" fatStatus=");
            serial::puts(fs_fat::file_write_status_name(fatStatus));
            serial::puts(" blockStatus=0x");
            serial::put_hex8(static_cast<uint8_t>(blockStatus));
            serial::puts(" vfsStatus=");
            serial::puts(status_name(result));
            serial::puts("\n");
#endif
            return result == VFS_OK ? static_cast<int32_t>(size) : result;
        }

        default:
            return VFS_ERR_NOT_SUPPORTED;
    }
}

int32_t append_file(const char* path, const void* buffer, uint32_t size)
{
    const HandleToken handle = open(path, OPEN_WRITE | OPEN_APPEND | OPEN_CREATE);
    if (handle == INVALID_HANDLE) return VFS_ERR_INVALID;
    
    int32_t bytesWritten = write(handle, buffer, size);
    close(handle);
    
    return bytesWritten;
}

// ================================================================
// Public API — Filesystem Information
// ================================================================

uint64_t total_space(const char* mountPath)
{
    MountOperationReference operation;
    if (operation.begin_path(mountPath) != VFS_OK) return 0;
    MountPoint* mount = operation.mount();
    
    block::BlockDevice device = {};
    if (!block::copy_device(mount->blockDevIndex, device) ||
        device.registrationId != mount->parentRegistrationId)
        return 0;
    return device.totalSectors * device.sectorSize;
}

uint64_t free_space(const char* mountPath)
{
    MountOperationReference operation;
    if (operation.begin_path(mountPath) != VFS_OK) return 0;
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

const char* status_name(Status status)
{
    switch (status) {
        case VFS_OK: return "OK";
        case VFS_ERR_NOT_FOUND: return "Path not found";
        case VFS_ERR_EXISTS: return "Already exists";
        case VFS_ERR_NOT_DIR: return "Parent is not a directory";
        case VFS_ERR_IS_DIR: return "Path is a directory";
        case VFS_ERR_NOT_EMPTY: return "Directory is not empty";
        case VFS_ERR_NO_SPACE: return "No space left";
        case VFS_ERR_READ_ONLY: return "Media is read-only";
        case VFS_ERR_PROTECTED: return "Root or boot storage is protected";
        case VFS_ERR_WRITE_UNCERTAIN: return "USB write completion is uncertain";
        case VFS_ERR_DURABILITY_UNVERIFIED: return "USB write completed but durability could not be verified";
        case VFS_ERR_INVALID: return "Invalid filesystem operation";
        case VFS_ERR_IO: return "Filesystem I/O failed";
        case VFS_ERR_NOT_MOUNT: return "No mounted filesystem for path";
        case VFS_ERR_BUSY: return "Filesystem is busy";
        case VFS_ERR_TOO_MANY: return "Too many open filesystem objects";
        case VFS_ERR_NOT_SUPPORTED: return "Filesystem operation is unsupported";
        case VFS_ERR_IO_TIMEOUT: return "Storage I/O timed out";
        case VFS_ERR_CORRUPT_CHAIN: return "Filesystem chain is corrupt";
        case VFS_ERR_NO_PROGRESS: return "Filesystem operation made no progress";
        case VFS_ERR_ALLOCATION_FAILED: return "Filesystem allocation failed";
        case VFS_ERR_DIRECTORY_NOT_EMPTY: return "Directory is not empty";
        case VFS_ERR_RECURSION_LIMIT: return "Directory traversal limit reached";
        case VFS_ERR_ENTRY_LIMIT: return "Directory entry limit reached";
        case VFS_ERR_CORRUPT_DIRECTORY: return "Directory is corrupt";
        case VFS_ERR_INVALID_DESTINATION: return "Invalid destination";
        case VFS_ERR_ROLLBACK_FAILED: return "Filesystem rollback failed";
        case VFS_ERR_DEVICE_REMOVED:
            return "Device was removed or is no longer available";
        case VFS_ERR_NOT_READY: return "Device is not ready";
        case VFS_ERR_MOUNT_STALE:
            return "Mount is stale; parent identity changed";
        default: return "VFS_STATUS_UNKNOWN";
    }
}

} // namespace vfs
} // namespace kernel
