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

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_devices, sizeof(s_devices));
    s_deviceCount = 0;
}

uint8_t register_device(const BlockDevice& dev)
{
    for (uint8_t i = 0; i < MAX_BLOCK_DEVICES; ++i) {
        if (!s_devices[i].active) {
            memcopy(&s_devices[i], &dev, sizeof(BlockDevice));
            s_devices[i].active = true;
            ++s_deviceCount;
            return i;
        }
    }
    return 0xFF;
}

void unregister_device(uint8_t index)
{
    if (index >= MAX_BLOCK_DEVICES) return;
    if (!s_devices[index].active) return;
    s_devices[index].active = false;
    if (s_deviceCount > 0) --s_deviceCount;
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

Status read_sectors(uint8_t devIndex,
                    uint64_t lba,
                    uint32_t count,
                    void* buffer)
{
    if (devIndex >= MAX_BLOCK_DEVICES) return BLOCK_ERR_INVALID;
    if (!s_devices[devIndex].active)   return BLOCK_ERR_INVALID;
    if (!s_devices[devIndex].readFn)   return BLOCK_ERR_UNSUPPORTED;
    if (!buffer || count == 0)         return BLOCK_ERR_INVALID;

    return s_devices[devIndex].readFn(
        s_devices[devIndex].driverIndex, lba, count, buffer);
}

Status write_sectors(uint8_t devIndex,
                     uint64_t lba,
                     uint32_t count,
                     const void* buffer)
{
    if (devIndex >= MAX_BLOCK_DEVICES)  return BLOCK_ERR_INVALID;
    if (!s_devices[devIndex].active)    return BLOCK_ERR_INVALID;
    if (!s_devices[devIndex].writeFn)   return BLOCK_ERR_UNSUPPORTED;
    if (!buffer || count == 0)          return BLOCK_ERR_INVALID;

    return s_devices[devIndex].writeFn(
        s_devices[devIndex].driverIndex, lba, count, buffer);
}

} // namespace block
} // namespace kernel
