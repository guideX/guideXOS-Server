// RAM Disk Block Device Driver
//
// Provides an in-memory block device for:
//   - Filesystem testing without hardware
//   - Temporary scratch storage
//   - Initial boot filesystem (initramfs-style)
//
// Integrates with the block device abstraction layer.
// Architecture-independent — works on all platforms.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_RAMDISK_H
#define KERNEL_RAMDISK_H

#include "kernel/types.h"
#include "kernel/block_device.h"

namespace kernel {
namespace ramdisk {

// ================================================================
// Constants
// ================================================================

// Default sector size (standard for compatibility)
static const uint32_t RAMDISK_SECTOR_SIZE = 512;

// Maximum number of RAM disks
static const uint8_t MAX_RAMDISKS = 4;

// Default RAM disk pool size (16 MB total for all RAM disks)
static const size_t RAMDISK_POOL_SIZE = 16 * 1024 * 1024;

// ================================================================
// RAM disk descriptor
// ================================================================

struct RamDisk {
    bool     active;
    uint8_t* data;              // Pointer to memory buffer
    uint64_t sectorCount;       // Number of sectors
    uint32_t sectorSize;        // Bytes per sector (usually 512)
    bool     ownsMemory;        // True if we allocated the memory
    char     name[32];          // Human-readable name (e.g., "ram0")
};

// ================================================================
// Public API
// ================================================================

// Initialize the RAM disk subsystem (allocates memory pool).
// Call once at boot before creating any RAM disks.
void init();

// Create a RAM disk with the specified size in bytes.
// Memory is allocated from the internal pool.
// Returns the RAM disk index, or 0xFF on failure.
uint8_t create(size_t sizeBytes, const char* name = nullptr);

// Create a RAM disk using a pre-allocated memory buffer.
// Useful for boot-time initramfs or memory-mapped regions.
// The caller is responsible for the memory lifetime.
// Returns the RAM disk index, or 0xFF on failure.
uint8_t create_at(void* memory, size_t sizeBytes, const char* name = nullptr);

// Destroy a RAM disk and free its memory (if owned).
void destroy(uint8_t index);

// Clear a RAM disk (zero all sectors).
void clear(uint8_t index);

// Return the number of active RAM disks.
uint8_t disk_count();

// Return RAM disk info by index (nullptr if invalid).
const RamDisk* get_disk(uint8_t index);

// Return the raw data pointer (for direct memory access).
// Use with caution — bypasses block device layer.
void* get_data(uint8_t index);

// ================================================================
// Block device callbacks (internal use)
// ================================================================

// These are registered with the block device layer.
block::Status read_sectors(uint8_t driverIndex,
                           uint64_t lba,
                           uint32_t count,
                           void* buffer);

block::Status write_sectors(uint8_t driverIndex,
                            uint64_t lba,
                            uint32_t count,
                            const void* buffer);

} // namespace ramdisk
} // namespace kernel

#endif // KERNEL_RAMDISK_H
