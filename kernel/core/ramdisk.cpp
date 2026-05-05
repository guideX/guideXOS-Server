// RAM Disk Block Device Driver — Implementation
//
// Provides in-memory block device storage using a static memory pool.
// Integrates with the block device layer for uniform access.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/ramdisk.h"
#include "include/kernel/block_device.h"

#if defined(__GNUC__) || defined(__clang__)
#include "include/kernel/serial_debug.h"
#endif

namespace kernel {
namespace ramdisk {

// ================================================================
// Internal state
// ================================================================

static RamDisk s_disks[MAX_RAMDISKS];
static uint8_t s_diskCount = 0;
static bool    s_initialized = false;

// Static memory pool for RAM disk allocation
// Aligned to 4KB for potential DMA compatibility
alignas(4096) static uint8_t s_memoryPool[RAMDISK_POOL_SIZE];
static size_t s_poolUsed = 0;

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

static void memcopy(void* dst, const void* src, size_t len)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < len; ++i) {
        d[i] = s[i];
    }
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

static size_t strlen(const char* s)
{
    size_t len = 0;
    if (s) {
        while (s[len]) ++len;
    }
    return len;
}

// Simple integer to string for naming
static void int_to_str(uint32_t num, char* buf, size_t bufLen)
{
    if (bufLen == 0) return;
    
    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    
    char temp[12];
    int i = 0;
    while (num > 0 && i < 11) {
        temp[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    size_t j = 0;
    while (i > 0 && j < bufLen - 1) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
}

// Allocate from the static pool
static void* pool_alloc(size_t size)
{
    // Align to sector size
    size = (size + RAMDISK_SECTOR_SIZE - 1) & ~(RAMDISK_SECTOR_SIZE - 1);
    
    if (s_poolUsed + size > RAMDISK_POOL_SIZE) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[RAMDISK] ERROR: Out of pool memory\n");
#endif
        return nullptr;
    }
    
    void* ptr = s_memoryPool + s_poolUsed;
    s_poolUsed += size;
    return ptr;
}

// ================================================================
// Block device callbacks
// ================================================================

block::Status read_sectors(uint8_t driverIndex,
                           uint64_t lba,
                           uint32_t count,
                           void* buffer)
{
    if (driverIndex >= MAX_RAMDISKS) {
        return block::BLOCK_ERR_INVALID;
    }
    
    RamDisk& disk = s_disks[driverIndex];
    if (!disk.active || !disk.data) {
        return block::BLOCK_ERR_INVALID;
    }

    if (disk.readOnly) {
        return block::BLOCK_ERR_UNSUPPORTED;
    }
    
    if (lba + count > disk.sectorCount) {
        return block::BLOCK_ERR_INVALID;
    }
    
    if (!buffer || count == 0) {
        return block::BLOCK_ERR_INVALID;
    }
    
    size_t offset = static_cast<size_t>(lba * disk.sectorSize);
    size_t bytes = static_cast<size_t>(count * disk.sectorSize);
    
    memcopy(buffer, disk.data + offset, bytes);
    
    return block::BLOCK_OK;
}

block::Status write_sectors(uint8_t driverIndex,
                            uint64_t lba,
                            uint32_t count,
                            const void* buffer)
{
    if (driverIndex >= MAX_RAMDISKS) {
        return block::BLOCK_ERR_INVALID;
    }
    
    RamDisk& disk = s_disks[driverIndex];
    if (!disk.active || !disk.data) {
        return block::BLOCK_ERR_INVALID;
    }
    
    if (lba + count > disk.sectorCount) {
        return block::BLOCK_ERR_INVALID;
    }
    
    if (!buffer || count == 0) {
        return block::BLOCK_ERR_INVALID;
    }
    
    size_t offset = static_cast<size_t>(lba * disk.sectorSize);
    size_t bytes = static_cast<size_t>(count * disk.sectorSize);
    
    memcopy(disk.data + offset, buffer, bytes);
    
    return block::BLOCK_OK;
}

// ================================================================
// Public API implementation
// ================================================================

void init()
{
    if (s_initialized) return;
    
    memzero(s_disks, sizeof(s_disks));
    s_diskCount = 0;
    s_poolUsed = 0;
    s_initialized = true;
    
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[RAMDISK] Initialized with ");
    serial::put_hex32(RAMDISK_POOL_SIZE / 1024);
    serial::puts(" KB pool\n");
#endif
}

uint8_t create(size_t sizeBytes, const char* name)
{
    if (!s_initialized) {
        init();
    }
    
    // Find free slot
    uint8_t index = 0xFF;
    for (uint8_t i = 0; i < MAX_RAMDISKS; ++i) {
        if (!s_disks[i].active) {
            index = i;
            break;
        }
    }
    
    if (index == 0xFF) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[RAMDISK] ERROR: No free slots\n");
#endif
        return 0xFF;
    }
    
    // Calculate sector count
    uint64_t sectorCount = (sizeBytes + RAMDISK_SECTOR_SIZE - 1) / RAMDISK_SECTOR_SIZE;
    size_t allocSize = static_cast<size_t>(sectorCount * RAMDISK_SECTOR_SIZE);
    
    // Allocate memory
    void* memory = pool_alloc(allocSize);
    if (!memory) {
        return 0xFF;
    }
    
    // Zero-initialize
    memzero(memory, allocSize);
    
    // Initialize descriptor
    RamDisk& disk = s_disks[index];
    disk.active = true;
    disk.data = static_cast<uint8_t*>(memory);
    disk.sectorCount = sectorCount;
    disk.sectorSize = RAMDISK_SECTOR_SIZE;
    disk.ownsMemory = true;
    disk.readOnly = false;
    
    // Generate name
    if (name && strlen(name) > 0) {
        strcopy(disk.name, name, sizeof(disk.name));
    } else {
        strcopy(disk.name, "ram", sizeof(disk.name));
        char numBuf[8];
        int_to_str(index, numBuf, sizeof(numBuf));
        size_t nameLen = strlen(disk.name);
        strcopy(disk.name + nameLen, numBuf, sizeof(disk.name) - nameLen);
    }
    
    ++s_diskCount;
    
    // Register with block device layer
    block::BlockDevice bdev;
    bdev.active = true;
    bdev.type = block::BDEV_USB_MASS; // Reuse type since there's no BDEV_RAMDISK
    bdev.driverIndex = index;
    bdev.totalSectors = sectorCount;
    bdev.sectorSize = RAMDISK_SECTOR_SIZE;
    strcopy(bdev.name, disk.name, sizeof(bdev.name));
    bdev.readFn = read_sectors;
    bdev.writeFn = write_sectors;
    
    block::register_device(bdev);
    
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[RAMDISK] Created '");
    serial::puts(disk.name);
    serial::puts("' with ");
    serial::put_hex32(static_cast<uint32_t>(sectorCount));
    serial::puts(" sectors (");
    serial::put_hex32(static_cast<uint32_t>(allocSize / 1024));
    serial::puts(" KB)\n");
#endif
    
    return index;
}

uint8_t create_at(void* memory, size_t sizeBytes, const char* name)
{
    if (!s_initialized) {
        init();
    }
    
    if (!memory || sizeBytes < RAMDISK_SECTOR_SIZE) {
        return 0xFF;
    }
    
    // Find free slot
    uint8_t index = 0xFF;
    for (uint8_t i = 0; i < MAX_RAMDISKS; ++i) {
        if (!s_disks[i].active) {
            index = i;
            break;
        }
    }
    
    if (index == 0xFF) {
        return 0xFF;
    }
    
    uint64_t sectorCount = sizeBytes / RAMDISK_SECTOR_SIZE;
    
    // Initialize descriptor
    RamDisk& disk = s_disks[index];
    disk.active = true;
    disk.data = static_cast<uint8_t*>(memory);
    disk.sectorCount = sectorCount;
    disk.sectorSize = RAMDISK_SECTOR_SIZE;
    disk.ownsMemory = false;  // Caller owns the memory
    disk.readOnly = false;
    
    // Generate name
    if (name && strlen(name) > 0) {
        strcopy(disk.name, name, sizeof(disk.name));
    } else {
        strcopy(disk.name, "ram", sizeof(disk.name));
        char numBuf[8];
        int_to_str(index, numBuf, sizeof(numBuf));
        size_t nameLen = strlen(disk.name);
        strcopy(disk.name + nameLen, numBuf, sizeof(disk.name) - nameLen);
    }
    
    ++s_diskCount;
    
    // Register with block device layer
    block::BlockDevice bdev;
    bdev.active = true;
    bdev.type = block::BDEV_USB_MASS;
    bdev.driverIndex = index;
    bdev.totalSectors = sectorCount;
    bdev.sectorSize = RAMDISK_SECTOR_SIZE;
    strcopy(bdev.name, disk.name, sizeof(bdev.name));
    bdev.readFn = read_sectors;
    bdev.writeFn = write_sectors;
    
    block::register_device(bdev);
    
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[RAMDISK] Created '");
    serial::puts(disk.name);
    serial::puts("' at address 0x");
    serial::put_hex32(reinterpret_cast<uintptr_t>(memory) >> 32);
    serial::put_hex32(reinterpret_cast<uintptr_t>(memory) & 0xFFFFFFFF);
    serial::puts(" (");
    serial::put_hex32(static_cast<uint32_t>(sectorCount));
    serial::puts(" sectors)\n");
#endif
    
    return index;
}

uint8_t create_readonly_at(const void* memory, size_t sizeBytes, const char* name)
{
    if (!s_initialized) {
        init();
    }

    if (!memory || sizeBytes < RAMDISK_SECTOR_SIZE) {
        return 0xFF;
    }

    uint8_t index = 0xFF;
    for (uint8_t i = 0; i < MAX_RAMDISKS; ++i) {
        if (!s_disks[i].active) {
            index = i;
            break;
        }
    }

    if (index == 0xFF) {
        return 0xFF;
    }

    uint64_t sectorCount = sizeBytes / RAMDISK_SECTOR_SIZE;

    RamDisk& disk = s_disks[index];
    disk.active = true;
    disk.data = const_cast<uint8_t*>(static_cast<const uint8_t*>(memory));
    disk.sectorCount = sectorCount;
    disk.sectorSize = RAMDISK_SECTOR_SIZE;
    disk.ownsMemory = false;
    disk.readOnly = true;

    if (name && strlen(name) > 0) {
        strcopy(disk.name, name, sizeof(disk.name));
    } else {
        strcopy(disk.name, "ramimg", sizeof(disk.name));
        char numBuf[8];
        int_to_str(index, numBuf, sizeof(numBuf));
        size_t nameLen = strlen(disk.name);
        strcopy(disk.name + nameLen, numBuf, sizeof(disk.name) - nameLen);
    }

    ++s_diskCount;

    block::BlockDevice bdev;
    bdev.active = true;
    bdev.type = block::BDEV_USB_MASS;
    bdev.driverIndex = index;
    bdev.totalSectors = sectorCount;
    bdev.sectorSize = RAMDISK_SECTOR_SIZE;
    strcopy(bdev.name, disk.name, sizeof(bdev.name));
    bdev.readFn = read_sectors;
    bdev.writeFn = nullptr;

    block::register_device(bdev);

#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[RAMDISK] Attached read-only image '");
    serial::puts(disk.name);
    serial::puts("' with ");
    serial::put_hex32(static_cast<uint32_t>(sectorCount));
    serial::puts(" sectors\n");
#endif

    return index;
}

void destroy(uint8_t index)
{
    if (index >= MAX_RAMDISKS) return;
    if (!s_disks[index].active) return;
    
    // Note: We don't free pool memory (simple bump allocator)
    // In a real kernel, we'd have proper memory management
    
    s_disks[index].active = false;
    s_disks[index].data = nullptr;
    
    if (s_diskCount > 0) {
        --s_diskCount;
    }
    
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[RAMDISK] Destroyed disk ");
    serial::put_hex32(index);
    serial::putc('\n');
#endif
}

void clear(uint8_t index)
{
    if (index >= MAX_RAMDISKS) return;
    if (!s_disks[index].active) return;
    if (!s_disks[index].data) return;
    
    size_t bytes = static_cast<size_t>(s_disks[index].sectorCount * 
                                       s_disks[index].sectorSize);
    memzero(s_disks[index].data, bytes);
}

uint8_t disk_count()
{
    return s_diskCount;
}

const RamDisk* get_disk(uint8_t index)
{
    if (index >= MAX_RAMDISKS) return nullptr;
    if (!s_disks[index].active) return nullptr;
    return &s_disks[index];
}

void* get_data(uint8_t index)
{
    if (index >= MAX_RAMDISKS) return nullptr;
    if (!s_disks[index].active) return nullptr;
    return s_disks[index].data;
}

} // namespace ramdisk
} // namespace kernel
