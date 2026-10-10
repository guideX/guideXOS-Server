#ifndef KERNEL_DISK_MANAGER_WIPE_UI_H
#define KERNEL_DISK_MANAGER_WIPE_UI_H

#include "kernel/partition_wipe.h"

namespace kernel {
namespace apps {
namespace disk_manager_wipe_ui {

enum WipeActionBlockReason : uint8_t {
    WIPE_ACTION_READY = 0,
    WIPE_ACTION_PARTITION_NOT_SELECTED,
    WIPE_ACTION_IDENTITY_INVALID,
    WIPE_ACTION_TABLE_INVALID,
    WIPE_ACTION_GEOMETRY_INVALID,
    WIPE_ACTION_NVME_UNQUALIFIED,
    WIPE_ACTION_TRANSPORT_UNSUPPORTED,
    WIPE_ACTION_DEVICE_OFFLINE,
    WIPE_ACTION_READ_UNAVAILABLE,
    WIPE_ACTION_READ_ONLY,
    WIPE_ACTION_DURABILITY_UNKNOWN,
    WIPE_ACTION_MOUNTED,
    WIPE_ACTION_PROTECTED,
    WIPE_ACTION_OPERATION_BUSY,
};

struct WipeActionEligibility {
    bool partitionSelected;
    bool identityValid;
    bool validPartitionTable;
    bool validPartitionGeometry;
    block::DeviceType transport;
    bool deviceOnline;
    bool readable;
    bool writable;
    bool durabilityQualified;
    bool unmounted;
    bool bootSafe;
    bool operationIdle;
};

inline WipeActionBlockReason wipe_action_block_reason(
    const WipeActionEligibility& eligibility)
{
    if (!eligibility.partitionSelected)
        return WIPE_ACTION_PARTITION_NOT_SELECTED;
    if (!eligibility.identityValid) return WIPE_ACTION_IDENTITY_INVALID;
    if (!eligibility.validPartitionTable) return WIPE_ACTION_TABLE_INVALID;
    if (!eligibility.validPartitionGeometry)
        return WIPE_ACTION_GEOMETRY_INVALID;
    if (eligibility.transport == block::BDEV_NVME)
        return WIPE_ACTION_NVME_UNQUALIFIED;
    if (eligibility.transport != block::BDEV_ATA_PIO &&
        eligibility.transport != block::BDEV_AHCI &&
        eligibility.transport != block::BDEV_USB_MASS)
        return WIPE_ACTION_TRANSPORT_UNSUPPORTED;
    if (!eligibility.deviceOnline) return WIPE_ACTION_DEVICE_OFFLINE;
    if (!eligibility.readable) return WIPE_ACTION_READ_UNAVAILABLE;
    if (!eligibility.writable) return WIPE_ACTION_READ_ONLY;
    if (!eligibility.durabilityQualified)
        return WIPE_ACTION_DURABILITY_UNKNOWN;
    if (!eligibility.unmounted) return WIPE_ACTION_MOUNTED;
    if (!eligibility.bootSafe) return WIPE_ACTION_PROTECTED;
    if (!eligibility.operationIdle) return WIPE_ACTION_OPERATION_BUSY;
    return WIPE_ACTION_READY;
}

inline const char* wipe_action_block_reason_text(WipeActionBlockReason reason)
{
    switch (reason) {
        case WIPE_ACTION_READY: return "Eligible selected partition";
        case WIPE_ACTION_PARTITION_NOT_SELECTED:
            return "Select a partition; a disk row cannot be wiped";
        case WIPE_ACTION_IDENTITY_INVALID:
            return "The exact device registration is unavailable";
        case WIPE_ACTION_TABLE_INVALID:
            return "The partition table is invalid or unsupported";
        case WIPE_ACTION_GEOMETRY_INVALID:
            return "The selected partition bounds are invalid";
        case WIPE_ACTION_NVME_UNQUALIFIED:
            return "NVMe partition writes are not qualified";
        case WIPE_ACTION_TRANSPORT_UNSUPPORTED:
            return "This storage transport is not qualified for wiping";
        case WIPE_ACTION_DEVICE_OFFLINE:
            return "The selected device is offline";
        case WIPE_ACTION_READ_UNAVAILABLE:
            return "The selected device cannot be safely revalidated";
        case WIPE_ACTION_READ_ONLY:
            return "The selected device is read only";
        case WIPE_ACTION_DURABILITY_UNKNOWN:
            return "Required write durability is not established";
        case WIPE_ACTION_MOUNTED:
            return "Unmount the selected storage before wiping";
        case WIPE_ACTION_PROTECTED:
            return "Boot, root, or protected system storage cannot be wiped";
        case WIPE_ACTION_OPERATION_BUSY:
            return "Another destructive storage operation is active";
        default: return "Partition wipe is unavailable";
    }
}

inline bool wipe_confirmation_matches(const char* text)
{
    return text && text[0] == 'W' && text[1] == 'I' && text[2] == 'P' &&
        text[3] == 'E' && text[4] == '\0';
}

inline bool wipe_partition_snapshot_matches(
    const storage::PartitionEntry& current,
    const storage::PartitionEntry& captured)
{
    if (current.partitionNumber != captured.partitionNumber ||
        current.isGpt != captured.isGpt ||
        current.startLba != captured.startLba ||
        current.endLba != captured.endLba ||
        current.sectorCount != captured.sectorCount ||
        current.mbrType != captured.mbrType ||
        current.bootable != captured.bootable ||
        current.attributes != captured.attributes) return false;
    for (uint8_t i = 0; i < 16; ++i) {
        if (current.typeGuid[i] != captured.typeGuid[i] ||
            current.uniqueGuid[i] != captured.uniqueGuid[i]) return false;
    }
    return true;
}

inline void bind_wipe_request(storage::PartitionWipeRequest& request,
                              const storage::TargetIdentity& target,
                              storage::PartitionScheme scheme,
                              const storage::PartitionEntry& partition,
                              const uint8_t* gptDiskGuid,
                              uint32_t mbrDiskSignature)
{
    request = {};
    request.targetSnapshot = target;
    request.partitionScheme = scheme;
    request.partitionSnapshot = partition;
    if (scheme == storage::PARTITION_SCHEME_GPT && gptDiskGuid) {
        for (uint8_t i = 0; i < 16; ++i)
            request.gptDiskGuid[i] = gptDiskGuid[i];
    } else if (scheme == storage::PARTITION_SCHEME_MBR) {
        request.mbrDiskSignature = mbrDiskSignature;
    }
    request.expectedRegistryGeneration = target.registryGeneration;
}

inline bool wipe_confirmation_binding_matches(
    const storage::PartitionWipeRequest& request,
    const storage::TargetIdentity& currentTarget,
    storage::PartitionScheme currentScheme,
    const storage::PartitionEntry& currentPartition)
{
    return storage::target_identities_equal(request.targetSnapshot,
                                             currentTarget) &&
        request.partitionScheme == currentScheme &&
        wipe_partition_snapshot_matches(currentPartition,
                                        request.partitionSnapshot);
}

inline bool wipe_partition_is_protected(
    const storage::PartitionEntry& partition)
{
    if (partition.bootable) return true;
    if (!partition.isGpt) return false;
    static const uint8_t protectedTypes[][16] = {
        {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
         0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B},
        {0x16, 0xE3, 0xC9, 0xE3, 0x5C, 0x0B, 0xB8, 0x4D,
         0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE},
        {0xA4, 0xBB, 0x94, 0xDE, 0xD1, 0x06, 0x40, 0x4D,
         0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC},
        {0x48, 0x61, 0x68, 0x21, 0x49, 0x64, 0x6F, 0x6E,
         0x74, 0x4E, 0x65, 0x65, 0x64, 0x45, 0x46, 0x49},
    };
    for (uint8_t type = 0; type < 4; ++type) {
        bool matches = true;
        for (uint8_t i = 0; i < 16; ++i)
            if (partition.typeGuid[i] != protectedTypes[type][i]) {
                matches = false;
                break;
            }
        if (matches) return true;
    }
    return false;
}

inline bool wipe_confirmation_can_start(bool targetStillValid,
                                        WipeActionBlockReason reason,
                                        const char* text)
{
    return targetStillValid && reason == WIPE_ACTION_READY &&
        wipe_confirmation_matches(text);
}

inline const char* wipe_terminal_message(storage::PartitionWipeState state,
                                         uint64_t sectorsWritten)
{
    switch (state) {
        case storage::PARTITION_WIPE_COMPLETED:
            return "Selected partition overwritten with zeros.";
        case storage::PARTITION_WIPE_STOPPED:
            return sectorsWritten == 0
                ? "Wipe stopped before any overwrite. No sectors were written."
                : "Wipe stopped after overwriting part of the partition. Existing data may be lost.";
        case storage::PARTITION_WIPE_FAILED:
            return "Wipe failed. The selected partition may be partially overwritten.";
        default:
            return "Partition wipe is still in progress.";
    }
}

inline bool wipe_job_running(const storage::PartitionWipeJob& job)
{
    return job.result.state == storage::PARTITION_WIPE_RUNNING;
}

inline bool wipe_close_request_allowed(const storage::PartitionWipeJob& job)
{
    return !wipe_job_running(job);
}

inline bool request_wipe_stop_before_close(storage::PartitionWipeJob& job)
{
    if (wipe_close_request_allowed(job)) return true;
    storage::request_partition_wipe_stop(job);
    return false;
}

inline int wipe_progress_value(const storage::PartitionWipeResult& result)
{
    return result.progressPercent;
}

} // namespace disk_manager_wipe_ui
} // namespace apps
} // namespace kernel

#endif
