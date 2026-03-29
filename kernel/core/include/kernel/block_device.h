// Block Device Abstraction Layer
//
// Provides a unified interface for all block-level storage:
//   - ATA / SATA (PIO & AHCI)
//   - NVMe
//   - USB Mass Storage (via usb_storage driver)
//
// Higher-level filesystem drivers (FAT32, ext4, UFS, …) use this
// abstraction to perform sector I/O without knowing the transport.
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_BLOCK_DEVICE_H
#define KERNEL_BLOCK_DEVICE_H

#include "kernel/types.h"

namespace kernel {
namespace block {

// ================================================================
// Block device transport types
// ================================================================

enum DeviceType : uint8_t {
    BDEV_NONE       = 0,
    BDEV_ATA_PIO    = 1,    // Legacy ATA (PIO mode via port I/O)
    BDEV_AHCI       = 2,    // SATA / AHCI (DMA, MMIO)
    BDEV_NVME       = 3,    // NVMe (PCIe, MMIO)
    BDEV_USB_MASS   = 4,    // USB Mass Storage (Bulk-Only)
};

// ================================================================
// Status codes
// ================================================================

enum Status : uint8_t {
    BLOCK_OK            = 0,
    BLOCK_ERR_IO        = 1,    // general I/O error
    BLOCK_ERR_TIMEOUT   = 2,
    BLOCK_ERR_NO_MEDIA  = 3,
    BLOCK_ERR_NOT_READY = 4,
    BLOCK_ERR_INVALID   = 5,    // bad parameter
    BLOCK_ERR_UNSUPPORTED = 6,
};

// ================================================================
// Read / write callbacks (set by each transport driver)
// ================================================================

// Read  'count' sectors starting at LBA into 'buffer'.
// Write 'count' sectors starting at LBA from 'buffer'.
typedef Status (*ReadSectorsFn)(uint8_t devIndex,
                                uint64_t lba,
                                uint32_t count,
                                void* buffer);
typedef Status (*WriteSectorsFn)(uint8_t devIndex,
                                 uint64_t lba,
                                 uint32_t count,
                                 const void* buffer);

// ================================================================
// Block device descriptor
// ================================================================

struct BlockDevice {
    bool          active;
    DeviceType    type;
    uint8_t       driverIndex;    // index within the transport driver
    uint64_t      totalSectors;
    uint32_t      sectorSize;     // typically 512 or 4096
    char          name[32];       // human-readable, e.g. "ata0", "nvme0n1"
    ReadSectorsFn  readFn;
    WriteSectorsFn writeFn;
};

static const uint8_t MAX_BLOCK_DEVICES = 16;

// ================================================================
// Public API
// ================================================================

// Initialise the block device layer (call once at boot).
void init();

// Register a new block device.  Returns the global device index,
// or 0xFF on failure.
uint8_t register_device(const BlockDevice& dev);

// Unregister a block device by global index.
void unregister_device(uint8_t index);

// Return the number of active block devices.
uint8_t device_count();

// Return a device descriptor by global index (nullptr if invalid).
const BlockDevice* get_device(uint8_t index);

// ----------------------------------------------------------------
// Sector I/O (delegates to the transport callbacks)
// ----------------------------------------------------------------

Status read_sectors(uint8_t devIndex,
                    uint64_t lba,
                    uint32_t count,
                    void* buffer);

Status write_sectors(uint8_t devIndex,
                     uint64_t lba,
                     uint32_t count,
                     const void* buffer);

} // namespace block
} // namespace kernel

#endif // KERNEL_BLOCK_DEVICE_H
