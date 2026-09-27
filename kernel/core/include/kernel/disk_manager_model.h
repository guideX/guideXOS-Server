#ifndef KERNEL_DISK_MANAGER_MODEL_H
#define KERNEL_DISK_MANAGER_MODEL_H

#include "kernel/storage_manager.h"

namespace kernel {
namespace storage {

// Read-only extents derived only from a structurally validated MBR/GPT model.
// End LBAs are inclusive, matching PartitionEntry and GPT conventions.
static const uint16_t MAX_UNALLOCATED_REGIONS = MAX_PARSED_PARTITIONS + 1;

struct UnallocatedRegion {
    uint64_t startLba;
    uint64_t endLba;
    uint64_t sectorCount;
    uint64_t capacityBytes;
    uint32_t alignmentSectors;
    bool startAlignedTo1MiB;
    bool insideUsableRange;
};

enum DiskManagerAction : uint8_t {
    DISK_MANAGER_ACTION_REFRESH = 0,
    DISK_MANAGER_ACTION_PROPERTIES,
    DISK_MANAGER_ACTION_DIAGNOSTICS,
    DISK_MANAGER_ACTION_INITIALIZE,
    DISK_MANAGER_ACTION_COUNT,
};

inline bool disk_manager_action_is_write(DiskManagerAction action)
{
    return action == DISK_MANAGER_ACTION_INITIALIZE;
}

inline bool disk_manager_action_enabled(DiskManagerAction action,
                                        bool hasSelection, DiskState state,
                                        bool initializeAvailable,
                                        bool dialogClosed)
{
    if (!dialogClosed) return false;
    switch (action) {
        case DISK_MANAGER_ACTION_REFRESH: return true;
        case DISK_MANAGER_ACTION_PROPERTIES:
        case DISK_MANAGER_ACTION_DIAGNOSTICS: return hasSelection;
        case DISK_MANAGER_ACTION_INITIALIZE:
            return hasSelection && state == DISK_STATE_NOT_INITIALIZED &&
                   initializeAvailable;
        default: return false;
    }
}

inline bool disk_manager_text_equal(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    while (*left && *right && *left == *right) { ++left; ++right; }
    return *left == *right;
}

// Registry generation changes for unrelated registrations too. For UI
// selection continuity, retain a disk only when its non-reused registration
// incarnation and descriptive geometry still match; destructive operations
// continue to use target_identities_equal(), which also checks generation.
inline bool disk_manager_same_disk_incarnation(const TargetIdentity& left,
                                                const TargetIdentity& right)
{
    return left.registrationId != 0 &&
        left.registrationId == right.registrationId &&
        left.globalIndex == right.globalIndex &&
        left.transport == right.transport &&
        left.driverIndex == right.driverIndex &&
        left.totalLogicalSectors == right.totalLogicalSectors &&
        left.logicalSectorSize == right.logicalSectorSize &&
        disk_manager_text_equal(left.name, right.name) &&
        disk_manager_text_equal(left.model, right.model) &&
        disk_manager_text_equal(left.serial, right.serial);
}

inline bool disk_manager_same_partition(const PartitionEntry& left,
                                        const PartitionEntry& right)
{
    if (left.isGpt != right.isGpt) return false;
    if (left.isGpt) {
        bool nonzero = false;
        for (uint8_t i = 0; i < 16; ++i) {
            if (left.uniqueGuid[i] != right.uniqueGuid[i]) return false;
            if (left.uniqueGuid[i] != 0) nonzero = true;
        }
        return nonzero;
    }
    return left.startLba == right.startLba &&
        left.endLba == right.endLba &&
        left.mbrType == right.mbrType;
}

inline const char* disk_manager_state_summary(DiskState state,
                                               bool primaryGptValid,
                                               bool backupGptValid)
{
    switch (state) {
        case DISK_STATE_NOT_INITIALIZED: return "Not Initialized";
        case DISK_STATE_UNREADABLE: return "Unreadable";
        case DISK_STATE_VALID_MBR: return "Online | MBR";
        case DISK_STATE_VALID_GPT: return "Online | GPT";
        case DISK_STATE_GPT_DEGRADED:
            if (primaryGptValid && !backupGptValid)
                return "GPT | Primary valid, backup invalid";
            if (!primaryGptValid && backupGptValid)
                return "GPT | Primary invalid, backup valid";
            return "GPT | Copies disagree or protective MBR is invalid";
        case DISK_STATE_INVALID_PARTITION_TABLE: return "Invalid Partition Table";
        case DISK_STATE_UNSUPPORTED_PARTITION_SCHEME:
            return "Unsupported Partition Scheme";
        default: return "Unknown";
    }
}

inline const char* disk_manager_mount_summary(MountSafety safety)
{
    switch (safety) {
        case DEVICE_ROOT_BACKING: return "Root backing device";
        case DEVICE_MOUNTED: return "Mounted on device";
        case DEVICE_UNMOUNTED: return "Unmounted";
        default: return "Mount relationship unknown";
    }
}

inline const char* disk_manager_boot_summary(BootSafety safety)
{
    switch (safety) {
        case BOOT_DEVICE_IS_TARGET: return "Boot device; protected";
        case BOOT_DEVICE_DEFINITELY_NOT_TARGET: return "Definitely not boot device";
        default: return "Boot identity unknown";
    }
}

inline void disk_manager_visible_range(uint16_t total, uint16_t offset,
                                       uint16_t capacity, uint16_t& first,
                                       uint16_t& count)
{
    first = offset > total ? total : offset;
    const uint16_t remaining = static_cast<uint16_t>(total - first);
    count = capacity < remaining ? capacity : remaining;
}

inline bool disk_manager_same_region(const UnallocatedRegion& left,
                                     const UnallocatedRegion& right)
{
    return left.startLba == right.startLba && left.endLba == right.endLba;
}

// Compute every bounded gap within the table's usable range. Invalid tables,
// overlapping entries, out-of-range entries, and unsupported extended MBR
// layouts produce no free-space claims. GPT metadata outside its usable range
// is reserved and is never returned as unallocated space.
inline bool compute_unallocated_regions(const PartitionTableModel& table,
                                         uint64_t totalSectors,
                                         uint32_t sectorSize,
                                         UnallocatedRegion* output,
                                         uint16_t outputCapacity,
                                         uint16_t& outputCount)
{
    outputCount = 0;
    if (!output || totalSectors < 2 || sectorSize < 512 ||
        sectorSize > 4096 || (sectorSize & (sectorSize - 1)) != 0 ||
        table.partitionCount > MAX_PARSED_PARTITIONS ||
        table.hybridMbr || table.extendedPartitionsPresent)
        return false;

    const bool gpt = table.scheme == PARTITION_SCHEME_GPT &&
        (table.state == DISK_STATE_VALID_GPT ||
         table.state == DISK_STATE_GPT_DEGRADED);
    const bool mbr = table.scheme == PARTITION_SCHEME_MBR &&
        table.state == DISK_STATE_VALID_MBR;
    if (!gpt && !mbr) return false;
    if (gpt && table.primaryGptValid && table.backupGptValid &&
        !table.gptCopiesAgree) return false;

    const uint64_t first = gpt ? table.firstUsableLba : 1;
    const uint64_t last = gpt ? table.lastUsableLba : totalSectors - 1;
    if (first > last || last >= totalSectors) return false;

    uint16_t order[MAX_PARSED_PARTITIONS];
    for (uint16_t i = 0; i < table.partitionCount; ++i) {
        const PartitionEntry& part = table.partitions[i];
        if (part.sectorCount == 0 || part.startLba > part.endLba ||
            part.endLba >= totalSectors || part.startLba < first ||
            part.endLba > last ||
            part.sectorCount != part.endLba - part.startLba + 1)
            return false;
        order[i] = i;
        uint16_t at = i;
        while (at > 0 && table.partitions[order[at - 1]].startLba >
                              part.startLba) {
            order[at] = order[at - 1];
            --at;
        }
        order[at] = i;
    }

    const uint64_t alignmentSectors = (1024u * 1024u) / sectorSize;
    if (alignmentSectors == 0) return false;
    uint64_t cursor = first;
    for (uint16_t i = 0; i <= table.partitionCount; ++i) {
        const bool trailing = i == table.partitionCount;
        const PartitionEntry* part = trailing ? nullptr :
            &table.partitions[order[i]];
        if (part && cursor > part->startLba) return false;
        const uint64_t gapEnd = part ? part->startLba - 1 : last;
        if (cursor <= gapEnd) {
            if (outputCount >= outputCapacity) {
                outputCount = 0;
                return false;
            }
            UnallocatedRegion& gap = output[outputCount++];
            gap.startLba = cursor;
            gap.endLba = gapEnd;
            gap.sectorCount = gapEnd - cursor + 1;
            if (gap.sectorCount > UINT64_MAX / sectorSize) {
                outputCount = 0;
                return false;
            }
            gap.capacityBytes = gap.sectorCount * sectorSize;
            gap.alignmentSectors = static_cast<uint32_t>(alignmentSectors);
            gap.startAlignedTo1MiB = (cursor % alignmentSectors) == 0;
            gap.insideUsableRange = true;
        }
        if (part) {
            if (part->endLba == UINT64_MAX) return false;
            cursor = part->endLba + 1;
        }
    }
    return true;
}

} // namespace storage
} // namespace kernel

#endif
