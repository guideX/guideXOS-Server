// Block Device Abstraction Layer implementation.
//
// Maintains the bounded registry and dispatches checked sector I/O through
// transport callbacks.

#include "include/kernel/block_device.h"
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace kernel {
namespace block {

static BlockDevice s_devices[MAX_BLOCK_DEVICES];
static uint16_t s_devicePins[MAX_BLOCK_DEVICES];
static uint8_t s_deviceCount = 0;
static uint64_t s_registryGeneration = 0;
static uint64_t s_nextRegistrationId = 0;
static volatile uint32_t s_registryLock = 0;

static void lock_registry()
{
#if defined(_MSC_VER)
    while (_InterlockedCompareExchange(
        reinterpret_cast<volatile long*>(&s_registryLock), 1, 0) != 0) {}
#else
    while (__sync_lock_test_and_set(&s_registryLock, 1) != 0) {}
#endif
}

static void unlock_registry()
{
#if defined(_MSC_VER)
    _InterlockedExchange(reinterpret_cast<volatile long*>(&s_registryLock), 0);
#else
    __sync_lock_release(&s_registryLock);
#endif
}

static void memzero(void* dst, size_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) p[i] = 0;
}

static void memcopy(void* dst, const void* src, size_t len)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < len; ++i) d[i] = s[i];
}

static void bump_generation_locked()
{
    // Never wrap into a value an old snapshot may still hold. Storage
    // revalidation fails closed after saturation at UINT64_MAX.
    if (s_registryGeneration != UINT64_MAX) ++s_registryGeneration;
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

static bool copy_and_pin(uint8_t index, BlockDevice& out)
{
    if (index >= MAX_BLOCK_DEVICES) return false;
    lock_registry();
    if (!s_devices[index].active || s_devices[index].forcedOffline ||
        s_devicePins[index] == UINT16_MAX) {
        unlock_registry();
        return false;
    }
    ++s_devicePins[index];
    memcopy(&out, &s_devices[index], sizeof(BlockDevice));
    unlock_registry();
    return true;
}

static Status read_pinned(const BlockDevice& dev, uint64_t lba, uint32_t count,
                          void* buffer, size_t bufferBytes, bool sizeChecked)
{
    if (!dev.readFn) return BLOCK_ERR_UNSUPPORTED;
    if (!valid_sector_size(dev.sectorSize) ||
        !valid_transfer_buffer(dev, count, buffer) ||
        lba > dev.totalSectors ||
        static_cast<uint64_t>(count) > dev.totalSectors - lba)
        return BLOCK_ERR_INVALID;
    const uint64_t requiredBytes = static_cast<uint64_t>(count) * dev.sectorSize;
    if (sizeChecked && requiredBytes > static_cast<uint64_t>(bufferBytes))
        return BLOCK_ERR_INVALID;
    return dev.readFn(dev.driverIndex, lba, count, buffer);
}

static Status write_pinned(const BlockDevice& dev, uint64_t lba,
                           uint32_t count, const void* buffer,
                           size_t bufferBytes, bool sizeChecked)
{
    if (!dev.writeFn) return BLOCK_ERR_UNSUPPORTED;
    if (!valid_sector_size(dev.sectorSize) ||
        !valid_transfer_buffer(dev, count, buffer) ||
        lba > dev.totalSectors ||
        static_cast<uint64_t>(count) > dev.totalSectors - lba)
        return BLOCK_ERR_INVALID;
    const uint64_t requiredBytes = static_cast<uint64_t>(count) * dev.sectorSize;
    if (sizeChecked && requiredBytes > static_cast<uint64_t>(bufferBytes))
        return BLOCK_ERR_INVALID;
    return dev.writeFn(dev.driverIndex, lba, count, buffer);
}

static void remove_locked(uint8_t index)
{
    if (!s_devices[index].active) return;
    memzero(&s_devices[index], sizeof(BlockDevice));
    if (s_deviceCount > 0) --s_deviceCount;
    bump_generation_locked();
}

void init()
{
    lock_registry();
    s_deviceCount = 0;
    for (uint8_t i = 0; i < MAX_BLOCK_DEVICES; ++i) {
        if (s_devicePins[i] != 0) {
            // Reinitialization during an operation must fail closed. Keep the
            // pinned identity as an offline tombstone until its owner releases.
            if (s_devices[i].active) {
                if (!s_devices[i].forcedOffline) s_devices[i].forcedOffline = true;
                ++s_deviceCount;
            }
        } else {
            memzero(&s_devices[i], sizeof(BlockDevice));
        }
    }
    bump_generation_locked();
    unlock_registry();
}

uint8_t register_device(const BlockDevice& dev)
{
    lock_registry();
    if (s_nextRegistrationId == UINT64_MAX) {
        unlock_registry();
        return 0xFF;
    }
    for (uint8_t i = 0; i < MAX_BLOCK_DEVICES; ++i) {
        if (!s_devices[i].active) {
            memcopy(&s_devices[i], &dev, sizeof(BlockDevice));
            s_devices[i].active = true;
            s_devices[i].forcedOffline = false;
            s_devices[i].registrationId = ++s_nextRegistrationId;
            ++s_deviceCount;
            bump_generation_locked();
            unlock_registry();
            return i;
        }
    }
    unlock_registry();
    return 0xFF;
}

bool copy_device(uint8_t index, BlockDevice& out)
{
    if (index >= MAX_BLOCK_DEVICES) return false;
    lock_registry();
    const bool valid = s_devices[index].active && !s_devices[index].forcedOffline;
    if (valid) memcopy(&out, &s_devices[index], sizeof(BlockDevice));
    unlock_registry();
    return valid;
}

bool unregister_device(uint8_t index)
{
    if (index >= MAX_BLOCK_DEVICES) return false;
    lock_registry();
    if (!s_devices[index].active || s_devicePins[index] != 0) {
        unlock_registry();
        return false;
    }
    remove_locked(index);
    unlock_registry();
    return true;
}

bool registration_is_present(uint8_t index, uint64_t registrationId)
{
    if (index >= MAX_BLOCK_DEVICES || registrationId == 0) return false;
    lock_registry();
    const bool present = s_devices[index].active &&
        s_devices[index].registrationId == registrationId;
    unlock_registry();
    return present;
}

bool unregister_device_if_matches(uint8_t index, uint64_t registrationId)
{
    if (index >= MAX_BLOCK_DEVICES || registrationId == 0) return false;
    lock_registry();
    if (!s_devices[index].active ||
        s_devices[index].registrationId != registrationId ||
        s_devicePins[index] != 0) {
        unlock_registry();
        return false;
    }
    remove_locked(index);
    unlock_registry();
    return true;
}

bool mark_device_offline(uint8_t index, uint64_t registrationId)
{
    if (index >= MAX_BLOCK_DEVICES || registrationId == 0) return false;
    lock_registry();
    BlockDevice& dev = s_devices[index];
    if (!dev.active || dev.registrationId != registrationId) {
        unlock_registry();
        return false;
    }
    if (!dev.forcedOffline) {
        if (s_devicePins[index] == 0) {
            remove_locked(index);
        } else {
            dev.forcedOffline = true;
            bump_generation_locked();
        }
    }
    unlock_registry();
    return true;
}

bool pin_device(uint8_t index, uint64_t registrationId)
{
    if (index >= MAX_BLOCK_DEVICES || registrationId == 0) return false;
    lock_registry();
    BlockDevice& dev = s_devices[index];
    const bool valid = dev.active && !dev.forcedOffline &&
        dev.registrationId == registrationId &&
        s_devicePins[index] != UINT16_MAX;
    if (valid) ++s_devicePins[index];
    unlock_registry();
    return valid;
}

void unpin_device(uint8_t index, uint64_t registrationId)
{
    if (index >= MAX_BLOCK_DEVICES || registrationId == 0) return;
    lock_registry();
    BlockDevice& dev = s_devices[index];
    if (dev.active && dev.registrationId == registrationId &&
        s_devicePins[index] != 0) {
        --s_devicePins[index];
        if (s_devicePins[index] == 0 && dev.forcedOffline)
            remove_locked(index);
    }
    unlock_registry();
}

uint8_t device_count()
{
    lock_registry();
    const uint8_t count = s_deviceCount;
    unlock_registry();
    return count;
}

const BlockDevice* get_device(uint8_t index)
{
    if (index >= MAX_BLOCK_DEVICES) return nullptr;
    lock_registry();
    const BlockDevice* result = s_devices[index].active &&
        !s_devices[index].forcedOffline ? &s_devices[index] : nullptr;
    unlock_registry();
    return result;
}

uint64_t registry_generation()
{
    lock_registry();
    const uint64_t generation = s_registryGeneration;
    unlock_registry();
    return generation;
}

Status read_sectors(uint8_t devIndex, uint64_t lba, uint32_t count,
                    void* buffer)
{
    BlockDevice dev;
    if (!copy_and_pin(devIndex, dev)) return BLOCK_ERR_NO_MEDIA;
    const Status status = read_pinned(dev, lba, count, buffer, 0, false);
    if (status == BLOCK_ERR_NO_MEDIA)
        mark_device_offline(devIndex, dev.registrationId);
    unpin_device(devIndex, dev.registrationId);
    return status;
}

Status read_sectors_checked(uint8_t devIndex, uint64_t lba, uint32_t count,
                            void* buffer, size_t bufferBytes)
{
    BlockDevice dev;
    if (!copy_and_pin(devIndex, dev)) return BLOCK_ERR_NO_MEDIA;
    const Status status = read_pinned(dev, lba, count, buffer,
                                      bufferBytes, true);
    if (status == BLOCK_ERR_NO_MEDIA)
        mark_device_offline(devIndex, dev.registrationId);
    unpin_device(devIndex, dev.registrationId);
    return status;
}

Status write_sectors(uint8_t devIndex, uint64_t lba, uint32_t count,
                     const void* buffer)
{
    BlockDevice dev;
    if (!copy_and_pin(devIndex, dev)) return BLOCK_ERR_NO_MEDIA;
    const Status status = write_pinned(dev, lba, count, buffer, 0, false);
    if (status == BLOCK_ERR_NO_MEDIA)
        mark_device_offline(devIndex, dev.registrationId);
    unpin_device(devIndex, dev.registrationId);
    return status;
}

Status write_sectors_checked(uint8_t devIndex, uint64_t lba, uint32_t count,
                             const void* buffer, size_t bufferBytes)
{
    BlockDevice dev;
    if (!copy_and_pin(devIndex, dev)) return BLOCK_ERR_NO_MEDIA;
    const Status status = write_pinned(dev, lba, count, buffer,
                                       bufferBytes, true);
    if (status == BLOCK_ERR_NO_MEDIA)
        mark_device_offline(devIndex, dev.registrationId);
    unpin_device(devIndex, dev.registrationId);
    return status;
}

FlushReport flush_with_result(uint8_t devIndex)
{
    FlushReport report = { FLUSH_OUTCOME_INVALID, BLOCK_ERR_INVALID, false };
    BlockDevice dev;
    if (!copy_and_pin(devIndex, dev)) {
        report.status = BLOCK_ERR_NO_MEDIA;
        return report;
    }
    if (dev.flushFn) {
        report.status = dev.flushFn(dev.driverIndex);
        report.semanticsKnown = dev.flushSemanticsKnown &&
                                !dev.writeCompletionDurable;
        report.outcome = report.status == BLOCK_OK
            ? FLUSH_OUTCOME_SUPPORTED_SUCCEEDED : FLUSH_OUTCOME_FAILED;
    } else if (dev.writeCompletionDurable && !dev.flushSemanticsKnown) {
        report.outcome = FLUSH_OUTCOME_SYNCHRONOUS_DURABLE;
        report.status = BLOCK_OK;
        report.semanticsKnown = true;
    } else {
        report.outcome = FLUSH_OUTCOME_UNSUPPORTED_UNKNOWN;
        report.status = BLOCK_ERR_UNSUPPORTED;
    }
    if (report.status == BLOCK_ERR_NO_MEDIA)
        mark_device_offline(devIndex, dev.registrationId);
    unpin_device(devIndex, dev.registrationId);
    return report;
}

Status flush(uint8_t devIndex)
{
    return flush_with_result(devIndex).status;
}

} // namespace block
} // namespace kernel
