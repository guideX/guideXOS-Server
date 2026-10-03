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
static storage::PartitionTableModel s_partitionTableScratch;
// Filesystem detection reads one complete device logical sector. Reuse this
// bounded scratch instead of putting a 4 KiB sector on the kernel stack.
alignas(4096) static uint8_t s_detectSectorBuffer[
    storage::MAX_LOGICAL_SECTOR_SIZE];
static Status mount_io_status(const MountPoint* mount);
static Status map_block_status(block::Status status);

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
    // A destructive operation pins an exact disk across long scans and
    // revalidates mount state before writing. Prevent a new VFS view from
    // appearing on any disk while that exclusive operation is active.
    if (storage::storage_operation_active())
        return partition_mount_result(PARTITION_MOUNT_OPERATION_BUSY);

    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0)
            return partition_mount_result(PARTITION_MOUNT_PATH_OCCUPIED);
    }
    uint8_t mountSlot = 0xFF;
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!s_mounts[i].active) { mountSlot = i; break; }
    }
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
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        const MountPoint& existing = s_mounts[i];
        if (existing.active && existing.partitionMount &&
            block::same_partition_identity(existing.partitionIdentity, identity)) {
            block::release_partition_view(viewHandle);
            return partition_mount_result(PARTITION_MOUNT_ALREADY_MOUNTED);
        }
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

    MountPoint& mount = s_mounts[mountSlot];
    memzero(&mount, sizeof(mount));
    mount.active = true;
    strcopy(mount.path, path, sizeof(mount.path));
    mount.fsType = FS_TYPE_FAT32;
    mount.blockDevIndex = blockDevIndex;
    mount.parentRegistrationId = identity.parentRegistrationId;
    mount.fsVolumeIndex = fsVolume;
    mount.readOnly = partitionEndpoint.readOnly ||
        !block::endpoint_supports_durable_writes(partitionEndpoint);
    mount.alias = false;
    mount.partitionMount = true;
    mount.partitionIdentity = identity;
    mount.partitionView = viewHandle;
    ++s_mountCount;
    return partition_mount_result(PARTITION_MOUNT_OK, mountSlot);
}

uint8_t mount_type(const char* path, uint8_t blockDevIndex, FSType fsType)
{
    if (storage::storage_operation_active()) return 0xFF;
    if (!s_initialized) {
        init();
    }
    
    if (!path || strlen(path) == 0) {
        return 0xFF;
    }
    block::BlockDevice geometry = {};
    if (!block::copy_device(blockDevIndex, geometry) ||
        (geometry.sectorSize != 512 &&
         !(fsType == FS_TYPE_FAT32 && geometry.sectorSize == 4096)))
        return 0xFF;
    
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
    MountPoint& mp = s_mounts[index];
    memzero(&mp, sizeof(mp));
    mp.active = true;
    strcopy(mp.path, path, sizeof(mp.path));
    mp.fsType = fsType;
    mp.blockDevIndex = blockDevIndex;
    mp.parentRegistrationId = partitionMount
        ? mountIdentity.parentRegistrationId : geometry.registrationId;
    mp.fsVolumeIndex = fsVolume;
    mp.readOnly = partitionMount ? partitionReadOnly : !geometry.writeFn;
    mp.alias = false;
    mp.sourcePrefix[0] = '\0';
    mp.partitionMount = partitionMount;
    if (partitionMount) {
        mp.partitionIdentity = mountIdentity;
        mp.partitionView = mountView;
    }
    
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
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i)
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0)
            return true;
    return false;
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

bool mount_identity_valid(uint8_t mountIndex)
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

Status mount_backing_status(uint8_t mountIndex)
{
    const MountPoint* mount = get_mount_by_index(mountIndex);
    if (!mount || !mount->active) return VFS_ERR_NOT_MOUNT;
    const Status ioStatus = mount_io_status(mount);
    if (ioStatus != VFS_OK) return ioStatus;
    if (mount->partitionMount && !mount_identity_valid(mountIndex))
        return VFS_ERR_MOUNT_STALE;
    return VFS_OK;
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

    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0) {
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS] ERROR: Alias path already mounted\n");
#endif
            return 0xFF;
        }
    }

    MountPoint* sourceMount = find_mount_for_path(sourcePath);
    if (!sourceMount) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] ERROR: No source mount point for alias path\n");
#endif
        return 0xFF;
    }

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
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!s_mounts[i].active) {
            index = i;
            break;
        }
    }

    if (index == 0xFF) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[VFS] ERROR: No free mount slots for alias\n");
#endif
        return 0xFF;
    }
    if (mount_io_status(sourceMount) != VFS_OK) return 0xFF;
    if (sourceMount->partitionMount &&
        !block::retain_partition_view(sourceMount->partitionView))
        return 0xFF;

    MountPoint& mp = s_mounts[index];
    memzero(&mp, sizeof(mp));
    mp.active = true;
    strcopy(mp.path, path, sizeof(mp.path));
    mp.fsType = sourceMount->fsType;
    mp.blockDevIndex = sourceMount->blockDevIndex;
    mp.parentRegistrationId = sourceMount->parentRegistrationId;
    mp.fsVolumeIndex = sourceMount->fsVolumeIndex;
    mp.readOnly = sourceMount->readOnly;
    mp.alias = true;
    mp.partitionMount = sourceMount->partitionMount;
    mp.partitionIdentity = sourceMount->partitionIdentity;
    mp.partitionView = sourceMount->partitionView;
    strcopy(mp.sourcePrefix, sourceRelative, sizeof(mp.sourcePrefix));

    ++s_mountCount;

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
    if (strcmp(path, "/") == 0) return VFS_ERR_BUSY;
    
    for (uint8_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (s_mounts[i].active && strcmp(s_mounts[i].path, path) == 0) {
            // Check for open files on this mount
            for (uint8_t j = 0; j < VFS_MAX_OPEN_FILES; ++j) {
                if (s_files[j].open && s_files[j].mountIndex == i) {
                    return VFS_ERR_BUSY;
                }
            }
            for (uint8_t j = 0; j < VFS_MAX_OPEN_FILES; ++j) {
                if (s_dirs[j].active && s_dirs[j].mountIndex == i)
                    return VFS_ERR_BUSY;
            }
            if (!s_mounts[i].alias) {
                for (uint8_t j = 0; j < VFS_MAX_MOUNTS; ++j) {
                    if (s_mounts[j].active && s_mounts[j].alias &&
                        s_mounts[j].blockDevIndex == s_mounts[i].blockDevIndex &&
                        s_mounts[j].fsVolumeIndex == s_mounts[i].fsVolumeIndex)
                        return VFS_ERR_BUSY;
                }
            }

            Status cleanupStatus = mount_backing_status(i);
            if (cleanupStatus == VFS_OK && !s_mounts[i].alias &&
                !s_mounts[i].readOnly &&
                (s_mounts[i].fsType == FS_TYPE_FAT32 ||
                 s_mounts[i].fsType == FS_TYPE_EXFAT)) {
                block::Status blockStatus = block::BLOCK_OK;
                if (!fs_fat::flush(s_mounts[i].fsVolumeIndex, &blockStatus)) {
                    const Status flushStatus = map_block_status(blockStatus);
                    cleanupStatus = mount_io_status(&s_mounts[i]);
                    if (cleanupStatus == VFS_OK) return flushStatus;
                }
            }
            
            // Unmount the filesystem
            if (!s_mounts[i].alias) {
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
            }
            if (s_mounts[i].partitionMount)
                block::release_partition_view(s_mounts[i].partitionView);
            memzero(&s_mounts[i], sizeof(s_mounts[i]));
            if (s_mountCount > 0) --s_mountCount;
            
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[VFS] Unmounted '");
            serial::puts(path);
            serial::puts("'\n");
#endif
            
            return cleanupStatus;
        }
    }
    
    return VFS_ERR_NOT_MOUNT;
}

const MountPoint* get_mount(const char* path)
{
    return find_mount_for_path(path);
}

uint8_t mount_index_for_path(const char* path)
{
    MountPoint* mount = find_mount_for_path(path);
    return mount ? static_cast<uint8_t>(mount - s_mounts) : 0xFF;
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

#if defined(KERNEL_STORAGE_TEST)
void test_set_mount(uint8_t index, bool active, uint8_t deviceIndex,
                    const char* path)
{
    if (index >= VFS_MAX_MOUNTS) return;
    if (s_mounts[index].active && s_mountCount) --s_mountCount;
    memzero(&s_mounts[index], sizeof(s_mounts[index]));
    s_mounts[index].active = active;
    s_mounts[index].blockDevIndex = deviceIndex;
    block::BlockDevice device = {};
    if (active && block::copy_device(deviceIndex, device))
        s_mounts[index].parentRegistrationId = device.registrationId;
    if (path) strcopy(s_mounts[index].path, path,
                      sizeof(s_mounts[index].path));
    if (active && s_mountCount < VFS_MAX_MOUNTS) ++s_mountCount;
    s_initialized = true;
}

void test_clear_mounts()
{
    for (size_t i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!s_mounts[i].active) continue;
        if (!s_mounts[i].alias) {
            if (s_mounts[i].fsType == FS_TYPE_FAT32 ||
                s_mounts[i].fsType == FS_TYPE_EXFAT)
                fs_fat::unmount(s_mounts[i].fsVolumeIndex);
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
    if (mount_io_status(mount) != VFS_OK) return 0xFF;
    
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
                    return 0xFF;
                }
                // Check if it's a directory when we want a file
                if (entry.isDir && (flags & OPEN_WRITE)) {
                    // Cannot open directory for writing
#if defined(__GNUC__) || defined(__clang__)
                    serial::puts("[VFS] ERROR: Cannot open directory for writing\n");
#endif
                    return 0xFF;
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
                        return 0xFF;
                    }
                    if (!fs_fat::lookup_path(mount->fsVolumeIndex, relPath, &entry)) {
                        return 0xFF;
                    }
                } else {
#if defined(__GNUC__) || defined(__clang__)
                    serial::puts("[VFS] ERROR: File not found: ");
                    serial::puts(path);
                    serial::puts("\n");
#endif
                    return 0xFF;
                }
            }

            // Open the existing or newly-created file.
            if (entry.isDir && (flags & OPEN_WRITE)) return 0xFF;
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
    Status flushStatus = mount_io_status(&s_mounts[fh.mountIndex]);
    // Close via filesystem driver
    MountPoint* mount = &s_mounts[fh.mountIndex];

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
    
    fh.open = false;
    return flushStatus;
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

    MountPoint* mount = &s_mounts[fh.mountIndex];
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
    MountPoint* mount = &s_mounts[s_files[handle].mountIndex];
    const Status backingStatus = mount_io_status(mount);
    if (backingStatus != VFS_OK) return backingStatus;
    if (mount->fsType != FS_TYPE_FAT32) return VFS_OK;
    block::Status blockStatus = block::BLOCK_OK;
    return fs_fat::flush(mount->fsVolumeIndex, &blockStatus)
        ? VFS_OK : map_block_status(blockStatus);
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
    if (mount_io_status(mount) != VFS_OK) return 0xFF;
    
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
    char resolvedPath[VFS_MAX_PATH];
    const char* relPath = resolve_relative_path(path, mount, resolvedPath, sizeof(resolvedPath));
    
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
    bool hasEntry = false;
    return readdir_detailed(iterator, entry, hasEntry) == VFS_OK && hasEntry;
}

Status readdir_detailed(uint8_t iterator, DirEntry* entry, bool& hasEntry)
{
    hasEntry = false;
    if (iterator >= VFS_MAX_OPEN_FILES) return VFS_ERR_INVALID;
    if (!s_dirs[iterator].active) return VFS_ERR_INVALID;
    if (!entry) return VFS_ERR_INVALID;

    DirIterator& di = s_dirs[iterator];
    MountPoint* mount = &s_mounts[di.mountIndex];
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

void closedir(uint8_t iterator)
{
    if (iterator >= VFS_MAX_OPEN_FILES) return;
    if (s_dirs[iterator].active) {
        MountPoint& mount = s_mounts[s_dirs[iterator].mountIndex];
        if (mount.active && (mount.fsType == FS_TYPE_FAT32 ||
                             mount.fsType == FS_TYPE_EXFAT)) {
            fs_fat::close_dir(mount.fsVolumeIndex);
        }
    }
    s_dirs[iterator].active = false;
}

Status mkdir(const char* path)
{
    if (!path) return VFS_ERR_INVALID;

    FileInfo existing{};
    if (stat(path, &existing) == VFS_OK) return VFS_ERR_EXISTS;

    char parentPath[VFS_MAX_PATH];
    parent_path(path, parentPath, sizeof(parentPath));
    FileInfo parentInfo{};
    Status parentStatus = stat(parentPath, &parentInfo);
    if (parentStatus != VFS_OK) return parentStatus;
    if (parentInfo.type != FILE_TYPE_DIRECTORY) return VFS_ERR_NOT_DIR;

    MountPoint* mount = find_mount_for_path(path);
    if (!mount) return VFS_ERR_NOT_MOUNT;
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

    MountPoint* mount = find_mount_for_path(path);
    if (!mount) return VFS_ERR_NOT_MOUNT;
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
    
    MountPoint* mount = find_mount_for_path(path);
    if (!mount) return VFS_ERR_NOT_MOUNT;
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

    MountPoint* mount = find_mount_for_path(path);
    if (!mount) return VFS_ERR_NOT_MOUNT;
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

    MountPoint* oldMount = find_mount_for_path(oldPath);
    MountPoint* newMount = find_mount_for_path(newPath);
    if (!oldMount || !newMount) return VFS_ERR_NOT_MOUNT;
    if (oldMount != newMount) return VFS_ERR_INVALID;
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
    uint8_t handle = open(path, OPEN_READ);
    if (handle == 0xFF) return VFS_ERR_NOT_FOUND;
    
    int32_t bytesRead = read(handle, buffer, maxSize);
    close(handle);
    
    return bytesRead;
}

int32_t write_file(const char* path, const void* buffer, uint32_t size)
{
    if (!path || (!buffer && size != 0)) return VFS_ERR_INVALID;

    MountPoint* mount = find_mount_for_path(path);
    if (!mount) return VFS_ERR_NOT_MOUNT;
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

    MountPoint* mount = find_mount_for_path(path);
    if (!mount) return VFS_ERR_NOT_MOUNT;
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
