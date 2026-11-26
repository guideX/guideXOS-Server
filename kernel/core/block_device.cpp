// Block Device Abstraction Layer — Implementation
//
// Maintains a table of block devices registered by transport drivers
// (ATA, AHCI, NVMe, USB Mass Storage) and dispatches sector I/O
// through the function pointers stored in each descriptor.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/block_device.h"

#if defined(GXOS_RAMDISK_STAGE_FINGERPRINTS) && (defined(__GNUC__) || defined(__clang__))
#include "include/kernel/serial_debug.h"
#endif

namespace kernel {
namespace block {

// ================================================================
// Internal state
// ================================================================

static BlockDevice s_devices[MAX_BLOCK_DEVICES];
static uint8_t     s_deviceCount = 0;
static uint64_t    s_deviceGenerations[MAX_BLOCK_DEVICES] = {};

#if defined(GXOS_RAMDISK_STAGE_FINGERPRINTS) && (defined(__GNUC__) || defined(__clang__))
static bool s_wallimgSector0ReadFingerprintEmitted = false;

static bool block_name_is_wallimg(const char* name)
{
    static const char expected[] = "wallimg";
    if (!name) return false;
    for (size_t i = 0; i < sizeof(expected); ++i) {
        if (name[i] != expected[i]) return false;
    }
    return true;
}

static uint64_t block_sector0_fnv1a64(const void* data)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 14695981039346656037ull;
    for (uint32_t i = 0; i < 512; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}
#endif

static void advanceGeneration(uint8_t index)
{
    ++s_deviceGenerations[index];
    if (s_deviceGenerations[index] == 0) ++s_deviceGenerations[index];
}

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
#if defined(GXOS_RAMDISK_STAGE_FINGERPRINTS) && (defined(__GNUC__) || defined(__clang__))
    s_wallimgSector0ReadFingerprintEmitted = false;
#endif
    for (uint8_t i = 0; i < MAX_BLOCK_DEVICES; ++i) {
        if (s_devices[i].active) advanceGeneration(i);
    }
    memzero(s_devices, sizeof(s_devices));
    s_deviceCount = 0;
}

uint8_t register_device(const BlockDevice& dev)
{
    for (uint8_t i = 0; i < MAX_BLOCK_DEVICES; ++i) {
        if (!s_devices[i].active) {
            memcopy(&s_devices[i], &dev, sizeof(BlockDevice));
            s_devices[i].active = true;
            advanceGeneration(i);
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
    advanceGeneration(index);
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

uint64_t device_generation(uint8_t index)
{
    if (index >= MAX_BLOCK_DEVICES || !s_devices[index].active) return 0;
    return s_deviceGenerations[index];
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
    if (lba > s_devices[devIndex].totalSectors ||
        static_cast<uint64_t>(count) > s_devices[devIndex].totalSectors - lba) {
        return BLOCK_ERR_INVALID;
    }

    const BlockDevice& device = s_devices[devIndex];
    const Status status = device.readFn(device.driverIndex, lba, count, buffer);
#if defined(GXOS_RAMDISK_STAGE_FINGERPRINTS) && (defined(__GNUC__) || defined(__clang__))
    if (!s_wallimgSector0ReadFingerprintEmitted &&
        device.type == BDEV_RAMDISK && block_name_is_wallimg(device.name) &&
        lba == 0 && count != 0) {
        serial::puts("S22R28R3_STAGE_FP stage=block-read device=wallimg lba=0 bytes=0x00000200 status=0x");
        serial::put_hex8(static_cast<uint8_t>(status));
        if (status == BLOCK_OK && device.sectorSize >= 512 && count >= 1) {
            serial::puts(" fnv1a64=0x");
            serial::put_hex64(block_sector0_fnv1a64(buffer));
        } else {
            serial::puts(" fnv1a64=unavailable");
        }
        serial::putc('\n');
        s_wallimgSector0ReadFingerprintEmitted = true;
    }
#endif
    return status;
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
    if (lba > s_devices[devIndex].totalSectors ||
        static_cast<uint64_t>(count) > s_devices[devIndex].totalSectors - lba) {
        return BLOCK_ERR_INVALID;
    }

    return s_devices[devIndex].writeFn(
        s_devices[devIndex].driverIndex, lba, count, buffer);
}

Status flush(uint8_t devIndex)
{
    if (devIndex >= MAX_BLOCK_DEVICES) return BLOCK_ERR_INVALID;
    if (!s_devices[devIndex].active) return BLOCK_ERR_INVALID;
    if (!s_devices[devIndex].flushFn) return BLOCK_OK;
    return s_devices[devIndex].flushFn(s_devices[devIndex].driverIndex);
}

} // namespace block
} // namespace kernel
