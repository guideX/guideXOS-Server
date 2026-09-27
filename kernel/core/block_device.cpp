// Block Device Abstraction Layer — Implementation
//
// Maintains a table of block devices registered by transport drivers
// (ATA, AHCI, NVMe, USB Mass Storage) and dispatches sector I/O
// through the function pointers stored in each descriptor.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/block_device.h"

namespace kernel {
namespace block {

// ================================================================
// Internal state
// ================================================================

static BlockDevice s_devices[MAX_BLOCK_DEVICES];
static uint8_t     s_deviceCount = 0;
static uint64_t    s_registryGeneration = 0;

// ================================================================
// Helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

static void memcopy(void* dst, const void* src, uint32_t len)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (uint32_t i = 0; i < len; ++i) d[i] = s[i];
}

static void bump_generation()
{
    ++s_registryGeneration;
    if (s_registryGeneration == 0) ++s_registryGeneration;
}

static bool valid_sector_size(uint32_t size)
{
    return size >= 512 && size <= 4096 && (size & (size - 1)) == 0;
}

static bool valid_transfer_buffer(const BlockDevice& dev, uint32_t count,
                                 const void* buffer)
{
    if (!buffer || count == 0 || !valid_sector_size(dev.sectorSize)) return false;
    const uint64_t bytes = static_cast<uint64_t>(count) * dev.sectorSize;
    if (dev.maxTransferBytes != 0 && bytes > dev.maxTransferBytes) return false;
    return dev.requiredBufferAlignment <= 1 ||
           (reinterpret_cast<uintptr_t>(buffer) % dev.requiredBufferAlignment) == 0;
}

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_devices, sizeof(s_devices));
    s_deviceCount = 0;
    bump_generation();
}

uint8_t register_device(const BlockDevice& dev)
{
    for (uint8_t i = 0; i < MAX_BLOCK_DEVICES; ++i) {
        if (!s_devices[i].active) {
            memcopy(&s_devices[i], &dev, sizeof(BlockDevice));
            s_devices[i].active = true;
            ++s_deviceCount;
            bump_generation();
            return i;
        }
    }
    return 0xFF;
}

void unregister_device(uint8_t index)
{
    if (index >= MAX_BLOCK_DEVICES) return;
    if (!s_devices[index].active) return;
    memzero(&s_devices[index], sizeof(BlockDevice));
    if (s_deviceCount > 0) --s_deviceCount;
    bump_generation();
}

uint8_t device_count()
{
    return s_deviceCount;
}

const BlockDevice* get_device(uint8_t index)
{
    if (index >= MAX_BLOCK_DEVICES) return nullptr;
    if (!s_devices[index].active) return nullptr;
    return &s_devices[index];
}

uint64_t registry_generation()
{
    return s_registryGeneration;
}

Status read_sectors(uint8_t devIndex,
                    uint64_t lba,
                    uint32_t count,
                    void* buffer)
{
    if (devIndex >= MAX_BLOCK_DEVICES) return BLOCK_ERR_INVALID;
    if (!s_devices[devIndex].active)   return BLOCK_ERR_INVALID;
    if (!s_devices[devIndex].readFn)   return BLOCK_ERR_UNSUPPORTED;
    if (!valid_sector_size(s_devices[devIndex].sectorSize)) return BLOCK_ERR_INVALID;
    if (!valid_transfer_buffer(s_devices[devIndex], count, buffer))
        return BLOCK_ERR_INVALID;
    if (lba > s_devices[devIndex].totalSectors ||
        static_cast<uint64_t>(count) > s_devices[devIndex].totalSectors - lba) {
        return BLOCK_ERR_INVALID;
    }

    return s_devices[devIndex].readFn(
        s_devices[devIndex].driverIndex, lba, count, buffer);
}

Status read_sectors_checked(uint8_t devIndex, uint64_t lba, uint32_t count,
                            void* buffer, size_t bufferBytes)
{
    const BlockDevice* dev = get_device(devIndex);
    if (!dev || !buffer || count == 0 || !valid_sector_size(dev->sectorSize))
        return BLOCK_ERR_INVALID;
    const uint64_t bytes = static_cast<uint64_t>(count) * dev->sectorSize;
    if (bytes > static_cast<uint64_t>(bufferBytes)) return BLOCK_ERR_INVALID;
    return read_sectors(devIndex, lba, count, buffer);
}

Status write_sectors(uint8_t devIndex,
                     uint64_t lba,
                     uint32_t count,
                     const void* buffer)
{
    if (devIndex >= MAX_BLOCK_DEVICES)  return BLOCK_ERR_INVALID;
    if (!s_devices[devIndex].active)    return BLOCK_ERR_INVALID;
    if (!s_devices[devIndex].writeFn)   return BLOCK_ERR_UNSUPPORTED;
    if (!valid_sector_size(s_devices[devIndex].sectorSize)) return BLOCK_ERR_INVALID;
    if (!valid_transfer_buffer(s_devices[devIndex], count, buffer))
        return BLOCK_ERR_INVALID;
    if (lba > s_devices[devIndex].totalSectors ||
        static_cast<uint64_t>(count) > s_devices[devIndex].totalSectors - lba) {
        return BLOCK_ERR_INVALID;
    }

    return s_devices[devIndex].writeFn(
        s_devices[devIndex].driverIndex, lba, count, buffer);
}

Status write_sectors_checked(uint8_t devIndex, uint64_t lba, uint32_t count,
                             const void* buffer, size_t bufferBytes)
{
    const BlockDevice* dev = get_device(devIndex);
    if (!dev || !buffer || count == 0 || !valid_sector_size(dev->sectorSize))
        return BLOCK_ERR_INVALID;
    const uint64_t bytes = static_cast<uint64_t>(count) * dev->sectorSize;
    if (bytes > static_cast<uint64_t>(bufferBytes)) return BLOCK_ERR_INVALID;
    return write_sectors(devIndex, lba, count, buffer);
}

FlushReport flush_with_result(uint8_t devIndex)
{
    FlushReport report;
    report.outcome = FLUSH_OUTCOME_INVALID;
    report.status = BLOCK_ERR_INVALID;
    report.semanticsKnown = false;
    if (devIndex >= MAX_BLOCK_DEVICES || !s_devices[devIndex].active)
        return report;

    const BlockDevice& dev = s_devices[devIndex];
    if (dev.flushFn) {
        report.status = dev.flushFn(dev.driverIndex);
        report.semanticsKnown = dev.flushSemanticsKnown;
        report.outcome = report.status == BLOCK_OK
            ? FLUSH_OUTCOME_SUPPORTED_SUCCEEDED : FLUSH_OUTCOME_FAILED;
        return report;
    }
    if (dev.writeCompletionDurable) {
        report.outcome = FLUSH_OUTCOME_SYNCHRONOUS_DURABLE;
        report.status = BLOCK_OK;
        report.semanticsKnown = true;
        return report;
    }
    report.outcome = FLUSH_OUTCOME_UNSUPPORTED_UNKNOWN;
    report.status = BLOCK_ERR_UNSUPPORTED;
    return report;
}

Status flush(uint8_t devIndex)
{
    const FlushReport report = flush_with_result(devIndex);
    return report.status;
}

} // namespace block
} // namespace kernel
