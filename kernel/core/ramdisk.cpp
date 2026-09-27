// RAM Disk Block Device Driver — Implementation
//
// Provides in-memory block device storage using a static memory pool.
// Integrates with the block device layer for uniform access.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/ramdisk.h"
#include "include/kernel/block_device.h"

#if (defined(__GNUC__) || defined(__clang__)) && !defined(KERNEL_STORAGE_TEST)
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
static uint64_t s_nextInstanceId = 0;

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
#if (defined(__GNUC__) || defined(__clang__)) && !defined(KERNEL_STORAGE_TEST)
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

    
    if (lba > disk.sectorCount || static_cast<uint64_t>(count) > disk.sectorCount - lba) {
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

    if (disk.readOnly) {
        return block::BLOCK_ERR_UNSUPPORTED;
    }
    
    if (lba > disk.sectorCount || static_cast<uint64_t>(count) > disk.sectorCount - lba) {
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

static block::Status flush_writes(uint8_t driverIndex)
{
    if (driverIndex >= MAX_RAMDISKS || !s_disks[driverIndex].active)
        return block::BLOCK_ERR_INVALID;
    // RAM-disk writes are synchronous and have no separate write cache. This
    // completes the flush API without claiming nonvolatile durability.
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

#if (defined(__GNUC__) || defined(__clang__)) && !defined(KERNEL_STORAGE_TEST)
    serial::puts("[RAMDISK] Initialized with ");
    serial::put_hex32(RAMDISK_POOL_SIZE / 1024);
    serial::puts(" KB pool\n");
#endif
}

static uint8_t find_free_disk_slot()
{
    for (uint8_t i = 0; i < MAX_RAMDISKS; ++i)
        if (!s_disks[i].active) return i;
    return 0xFF;
}

static void set_disk_name(RamDisk& disk, uint8_t index, const char* name,
                          const char* prefix)
{
    if (name && strlen(name) > 0) {
        strcopy(disk.name, name, sizeof(disk.name));
    } else {
        strcopy(disk.name, prefix, sizeof(disk.name));
        char numBuf[8];
        int_to_str(index, numBuf, sizeof(numBuf));
        const size_t nameLen = strlen(disk.name);
        strcopy(disk.name + nameLen, numBuf, sizeof(disk.name) - nameLen);
    }
}

static uint8_t register_ramdisk_block(uint8_t index)
{
    RamDisk& disk = s_disks[index];
    block::BlockDevice bdev;
    memzero(&bdev, sizeof(bdev));
    bdev.active = true;
    bdev.type = block::BDEV_RAMDISK;
    bdev.driverIndex = index;
    bdev.totalSectors = disk.sectorCount;
    bdev.sectorSize = disk.sectorSize;
    strcopy(bdev.name, disk.name, sizeof(bdev.name));
    bdev.readFn = read_sectors;
    bdev.writeFn = disk.readOnly ? nullptr : write_sectors;
    bdev.flushFn = flush_writes;
    bdev.flushSemanticsKnown = true;
    return block::register_device(bdev);
}

static uint8_t finish_ramdisk_create(uint8_t index)
{
    RamDisk& disk = s_disks[index];
    ++s_nextInstanceId;
    if (s_nextInstanceId == 0) ++s_nextInstanceId;
    disk.instanceId = s_nextInstanceId;
    disk.blockDeviceIndex = register_ramdisk_block(index);
    if (disk.blockDeviceIndex == 0xFF) {
        disk.active = false;
        disk.data = nullptr;
        disk.ownsHeapMemory = false;
        return 0xFF;
    }
    ++s_diskCount;
    return index;
}

uint8_t create(size_t sizeBytes, const char* name)
{
    if (!s_initialized) init();
    if (sizeBytes == 0 || sizeBytes > static_cast<size_t>(-1) - (RAMDISK_SECTOR_SIZE - 1))
        return 0xFF;
    const uint8_t index = find_free_disk_slot();
    if (index == 0xFF) return 0xFF;
    const uint64_t sectorCount =
        (sizeBytes + RAMDISK_SECTOR_SIZE - 1) / RAMDISK_SECTOR_SIZE;
    const size_t allocSize = static_cast<size_t>(sectorCount * RAMDISK_SECTOR_SIZE);
    void* memory = pool_alloc(allocSize);
    if (!memory) return 0xFF;
    memzero(memory, allocSize);

    RamDisk& disk = s_disks[index];
    memzero(&disk, sizeof(disk));
    disk.active = true;
    disk.data = static_cast<uint8_t*>(memory);
    disk.sectorCount = sectorCount;
    disk.sectorSize = RAMDISK_SECTOR_SIZE;
    disk.ownsMemory = true;
    disk.readOnly = false;
    disk.blockDeviceIndex = 0xFF;
    set_disk_name(disk, index, name, "ram");
    return finish_ramdisk_create(index);
}

uint8_t create_at(void* memory, size_t sizeBytes, const char* name)
{
    if (!s_initialized) init();
    if (!memory || sizeBytes < RAMDISK_SECTOR_SIZE) return 0xFF;
    const uint8_t index = find_free_disk_slot();
    if (index == 0xFF) return 0xFF;

    RamDisk& disk = s_disks[index];
    memzero(&disk, sizeof(disk));
    disk.active = true;
    disk.data = static_cast<uint8_t*>(memory);
    disk.sectorCount = sizeBytes / RAMDISK_SECTOR_SIZE;
    disk.sectorSize = RAMDISK_SECTOR_SIZE;
    disk.ownsMemory = false;
    disk.readOnly = false;
    disk.blockDeviceIndex = 0xFF;
    set_disk_name(disk, index, name, "ram");
    return finish_ramdisk_create(index);
}

uint8_t create_readonly_at(const void* memory, size_t sizeBytes, const char* name)
{
    if (!s_initialized) init();
    if (!memory || sizeBytes < RAMDISK_SECTOR_SIZE) return 0xFF;
    const uint8_t index = find_free_disk_slot();
    if (index == 0xFF) return 0xFF;

    RamDisk& disk = s_disks[index];
    memzero(&disk, sizeof(disk));
    disk.active = true;
    disk.data = const_cast<uint8_t*>(static_cast<const uint8_t*>(memory));
    disk.sectorCount = sizeBytes / RAMDISK_SECTOR_SIZE;
    disk.sectorSize = RAMDISK_SECTOR_SIZE;
    disk.ownsMemory = false;
    disk.readOnly = true;
    disk.blockDeviceIndex = 0xFF;
    set_disk_name(disk, index, name, "ramimg");
    return finish_ramdisk_create(index);
}

uint8_t create_readonly_owned(uint8_t* memory, size_t sizeBytes, const char* name)
{
    if (!s_initialized) init();
    if (!memory || sizeBytes < RAMDISK_SECTOR_SIZE) return 0xFF;
    const uint8_t index = find_free_disk_slot();
    if (index == 0xFF) return 0xFF;

    RamDisk& disk = s_disks[index];
    memzero(&disk, sizeof(disk));
    disk.active = true;
    disk.data = memory;
    disk.sectorCount = sizeBytes / RAMDISK_SECTOR_SIZE;
    disk.sectorSize = RAMDISK_SECTOR_SIZE;
    disk.ownsMemory = true;
    disk.ownsHeapMemory = true;
    disk.readOnly = true;
    disk.blockDeviceIndex = 0xFF;
    set_disk_name(disk, index, name, "ramimg");
    return finish_ramdisk_create(index);
}

void destroy(uint8_t index)
{
    if (index >= MAX_RAMDISKS) return;
    if (!s_disks[index].active) return;
    
    RamDisk& disk = s_disks[index];
    if (disk.blockDeviceIndex != 0xFF) {
        const block::BlockDevice* registered = block::get_device(disk.blockDeviceIndex);
        if (registered && registered->type == block::BDEV_RAMDISK &&
            registered->driverIndex == index)
            block::unregister_device(disk.blockDeviceIndex);
    }
    if (disk.ownsHeapMemory && disk.data) delete[] disk.data;
    disk.active = false;
    disk.data = nullptr;
    disk.blockDeviceIndex = 0xFF;
    disk.ownsHeapMemory = false;
    // Pool-backed disks use a bump allocator, so their memory stays reserved.
    if (s_diskCount > 0) --s_diskCount;

#if (defined(__GNUC__) || defined(__clang__)) && !defined(KERNEL_STORAGE_TEST)
    serial::puts("[RAMDISK] Destroyed disk ");
    serial::put_hex32(index);
    serial::putc('\n');
#endif
}

void clear(uint8_t index)
{
    if (index >= MAX_RAMDISKS) return;
    if (!s_disks[index].active) return;
    if (s_disks[index].readOnly || !s_disks[index].data) return;
    
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

bool validate_attachment_identity(uint8_t index, uint8_t blockDeviceIndex,
                                  uint64_t instanceId)
{
    if (instanceId == 0) return false;
    const RamDisk* disk = get_disk(index);
    if (!disk || disk->instanceId != instanceId ||
        disk->blockDeviceIndex != blockDeviceIndex) return false;
    const block::BlockDevice* device = block::get_device(blockDeviceIndex);
    return device && device->type == block::BDEV_RAMDISK &&
           device->driverIndex == index;
}

void* get_data(uint8_t index)
{
    if (index >= MAX_RAMDISKS) return nullptr;
    if (!s_disks[index].active) return nullptr;
    return s_disks[index].data;
}

} // namespace ramdisk
} // namespace kernel
