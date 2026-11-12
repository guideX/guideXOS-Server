#ifndef KERNEL_USER_DATA_VOLUME_POLICY_H
#define KERNEL_USER_DATA_VOLUME_POLICY_H

#include "kernel/block_device.h"

namespace kernel {
namespace user_data_volume {
namespace policy {

static const char kRequiredStableId[] = "GXOS-DATA-V1";

enum SelectionStatus : uint8_t {
    SELECT_OK = 0,
    SELECT_MISSING,
    SELECT_AMBIGUOUS,
    SELECT_SYSTEM_SOURCE,
    SELECT_UNSUPPORTED_DEVICE,
    SELECT_READ_ONLY,
    SELECT_NO_FLUSH,
    SELECT_INVALID_DEVICE,
};

struct Selection {
    SelectionStatus status;
    uint8_t deviceIndex;
};

inline bool stable_id_matches(const char* actual)
{
    if (!actual) return false;
    for (uint32_t index = 0; index < sizeof(kRequiredStableId); ++index) {
        if (actual[index] != kRequiredStableId[index]) return false;
        if (kRequiredStableId[index] == '\0') return true;
    }
    return false;
}

inline Selection select_device(const block::BlockDevice* const* devices,
                               uint8_t deviceCount,
                               uint8_t trustedSystemDevice)
{
    Selection result{SELECT_MISSING, 0xFFu};
    if (!devices) return result;

    for (uint8_t index = 0; index < deviceCount; ++index) {
        const block::BlockDevice* device = devices[index];
        if (!device || !device->active || !stable_id_matches(device->stableId)) continue;
        if (result.deviceIndex != 0xFFu) return Selection{SELECT_AMBIGUOUS, 0xFFu};
        result.deviceIndex = index;
    }

    if (result.deviceIndex == 0xFFu) return result;
    const block::BlockDevice* device = devices[result.deviceIndex];
    if (result.deviceIndex == trustedSystemDevice) {
        return Selection{SELECT_SYSTEM_SOURCE, result.deviceIndex};
    }
    if (device->type != block::BDEV_ATA_PIO) {
        return Selection{SELECT_UNSUPPORTED_DEVICE, result.deviceIndex};
    }
    if (!device->readFn || !device->totalSectors || device->sectorSize != 512u) {
        return Selection{SELECT_INVALID_DEVICE, result.deviceIndex};
    }
    if (!device->writeFn) return Selection{SELECT_READ_ONLY, result.deviceIndex};
    if (!device->flushFn) return Selection{SELECT_NO_FLUSH, result.deviceIndex};
    return Selection{SELECT_OK, result.deviceIndex};
}

} // namespace policy
} // namespace user_data_volume
} // namespace kernel

#endif // KERNEL_USER_DATA_VOLUME_POLICY_H
