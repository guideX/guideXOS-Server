#ifndef KERNEL_DISK_MANAGER_SURFACE_VERIFICATION_UI_H
#define KERNEL_DISK_MANAGER_SURFACE_VERIFICATION_UI_H

#include "kernel/surface_verification.h"
#include "kernel/widget_activation.h"

namespace kernel {
namespace apps {
namespace disk_manager_surface_verification_ui {

// The Stop Scan control reuses Disk Manager's existing modal Cancel button.
// Widget creation order in DiskManagerApp assigns it ID 12.
static const int STOP_SCAN_WIDGET_ID = 12;

enum VerificationScope : uint8_t {
    VERIFY_ENTIRE_DISK = 0,
    VERIFY_PARTITION,
};

enum AdmissionBlockReason : uint8_t {
    ADMISSION_READY = 0,
    ADMISSION_SELECTION_REQUIRED,
    ADMISSION_IDENTITY_INVALID,
    ADMISSION_GEOMETRY_INVALID,
    ADMISSION_READ_UNAVAILABLE,
    ADMISSION_PARTITION_TABLE_INVALID,
    ADMISSION_PARTITION_INVALID,
    ADMISSION_OPERATION_BUSY,
};

inline const char* admission_block_reason_text(AdmissionBlockReason reason)
{
    switch (reason) {
        case ADMISSION_READY: return "Ready for explicit read-only verification";
        case ADMISSION_SELECTION_REQUIRED:
            return "Select a parent disk row or a validated partition row";
        case ADMISSION_IDENTITY_INVALID:
            return "The exact device registration is unavailable or changed";
        case ADMISSION_GEOMETRY_INVALID:
            return "The device capacity or logical-sector geometry is invalid";
        case ADMISSION_READ_UNAVAILABLE:
            return "The selected device does not support reads";
        case ADMISSION_PARTITION_TABLE_INVALID:
            return "The selected partition map is invalid or unsupported";
        case ADMISSION_PARTITION_INVALID:
            return "The selected partition bounds are invalid or stale";
        case ADMISSION_OPERATION_BUSY:
            return "Another storage operation owns the device operation lease";
        default: return "Verification is unavailable";
    }
}

struct VerificationTarget {
    VerificationScope scope;
    storage::SurfaceVerificationRequest request;
    storage::PartitionEntry partition;
    storage::PartitionTableModel partitionTable;
    bool partitionTableCaptured;
};

inline bool text_equal(const char* left, const char* right)
{
    if (!left || !right) return false;
    size_t i = 0;
    while (left[i] && right[i]) {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return left[i] == '\0' && right[i] == '\0';
}

inline bool partition_entries_equal(const storage::PartitionEntry& a,
                                    const storage::PartitionEntry& b)
{
    if (a.partitionNumber != b.partitionNumber || a.mbrType != b.mbrType ||
        a.isGpt != b.isGpt || a.bootable != b.bootable ||
        a.startLba != b.startLba || a.endLba != b.endLba ||
        a.sectorCount != b.sectorCount || a.attributes != b.attributes ||
        !text_equal(a.name, b.name)) return false;
    for (uint8_t i = 0; i < 16; ++i)
        if (a.typeGuid[i] != b.typeGuid[i] ||
            a.uniqueGuid[i] != b.uniqueGuid[i]) return false;
    return true;
}

inline bool partition_tables_equal(const storage::PartitionTableModel& a,
                                   const storage::PartitionTableModel& b)
{
    if (a.scheme != b.scheme || a.state != b.state || a.error != b.error ||
        a.partitionCount != b.partitionCount ||
        a.tableEntryCount != b.tableEntryCount ||
        a.gptEntrySize != b.gptEntrySize ||
        a.gptEntryArraySectors != b.gptEntryArraySectors ||
        a.primaryGptHeaderCrc32 != b.primaryGptHeaderCrc32 ||
        a.backupGptHeaderCrc32 != b.backupGptHeaderCrc32 ||
        a.primaryGptEntryArrayCrc32 != b.primaryGptEntryArrayCrc32 ||
        a.backupGptEntryArrayCrc32 != b.backupGptEntryArrayCrc32 ||
        a.primaryGptEntryArrayLba != b.primaryGptEntryArrayLba ||
        a.backupGptEntryArrayLba != b.backupGptEntryArrayLba ||
        a.firstUsableLba != b.firstUsableLba ||
        a.lastUsableLba != b.lastUsableLba ||
        a.protectiveMbr != b.protectiveMbr ||
        a.hybridMbr != b.hybridMbr ||
        a.extendedPartitionsPresent != b.extendedPartitionsPresent ||
        a.primaryGptValid != b.primaryGptValid ||
        a.backupGptValid != b.backupGptValid ||
        a.gptCopiesAgree != b.gptCopiesAgree ||
        a.gptCopiesConflict != b.gptCopiesConflict ||
        a.protectiveMbrValid != b.protectiveMbrValid ||
        a.primaryGptCopyState != b.primaryGptCopyState ||
        a.backupGptCopyState != b.backupGptCopyState ||
        a.primaryGptError != b.primaryGptError ||
        a.backupGptError != b.backupGptError ||
        a.mbrDiskSignature != b.mbrDiskSignature) return false;
    for (uint8_t i = 0; i < 16; ++i)
        if (a.primaryDiskGuid[i] != b.primaryDiskGuid[i] ||
            a.backupDiskGuid[i] != b.backupDiskGuid[i]) return false;
    for (uint16_t i = 0; i < a.partitionCount; ++i)
        if (!partition_entries_equal(a.partitions[i], b.partitions[i]))
            return false;
    return true;
}

inline bool live_target_matches_capabilities(
    const storage::TargetIdentity& target,
    const storage::DeviceCapabilities& capabilities)
{
    return target.registrationId != 0 && target.registryGeneration != 0 &&
        target.registryGeneration != UINT64_MAX &&
        target.globalIndex == capabilities.globalIndex &&
        target.transport == capabilities.transport &&
        target.driverIndex == capabilities.driverIndex &&
        target.totalLogicalSectors == capabilities.totalLogicalSectors &&
        target.logicalSectorSize == capabilities.logicalSectorSize;
}

inline AdmissionBlockReason build_entire_disk_target(
    bool parentDiskRowSelected, bool identityValid, bool operationIdle,
    const storage::TargetIdentity& target,
    const storage::DeviceCapabilities& capabilities,
    VerificationTarget& output)
{
    output = {};
    if (!parentDiskRowSelected) return ADMISSION_SELECTION_REQUIRED;
    if (!identityValid || !live_target_matches_capabilities(target, capabilities))
        return ADMISSION_IDENTITY_INVALID;
    if (!capabilities.geometryValid || !capabilities.capacityValid ||
        capabilities.totalLogicalSectors == 0 ||
        !storage::valid_geometry(capabilities.totalLogicalSectors,
                                 capabilities.logicalSectorSize, nullptr))
        return ADMISSION_GEOMETRY_INVALID;
    if (!capabilities.readable) return ADMISSION_READ_UNAVAILABLE;
    if (!operationIdle) return ADMISSION_OPERATION_BUSY;

    output.scope = VERIFY_ENTIRE_DISK;
    output.request.targetSnapshot = target;
    output.request.startLba = 0;
    output.request.sectorCount = capabilities.totalLogicalSectors;
    return ADMISSION_READY;
}

inline bool valid_partition_table(const storage::PartitionTableModel& table)
{
    if (table.state == storage::DISK_STATE_VALID_GPT)
        return table.scheme == storage::PARTITION_SCHEME_GPT &&
            table.primaryGptValid && table.backupGptValid &&
            table.gptCopiesAgree && !table.gptCopiesConflict &&
            !table.hybridMbr;
    if (table.state == storage::DISK_STATE_VALID_MBR)
        return table.scheme == storage::PARTITION_SCHEME_MBR &&
            !table.hybridMbr && !table.extendedPartitionsPresent;
    return false;
}

inline AdmissionBlockReason build_partition_target(
    bool selectedPartitionRow, bool identityValid, bool operationIdle,
    const storage::TargetIdentity& target,
    const storage::DeviceCapabilities& capabilities,
    const storage::PartitionTableModel& table,
    const storage::PartitionEntry& selectedSnapshot,
    VerificationTarget& output)
{
    output = {};
    if (!selectedPartitionRow) return ADMISSION_SELECTION_REQUIRED;
    if (!identityValid || !live_target_matches_capabilities(target, capabilities))
        return ADMISSION_IDENTITY_INVALID;
    if (!capabilities.geometryValid || !capabilities.capacityValid ||
        capabilities.totalLogicalSectors == 0 ||
        !storage::valid_geometry(capabilities.totalLogicalSectors,
                                 capabilities.logicalSectorSize, nullptr))
        return ADMISSION_GEOMETRY_INVALID;
    if (!capabilities.readable) return ADMISSION_READ_UNAVAILABLE;
    if (!valid_partition_table(table)) return ADMISSION_PARTITION_TABLE_INVALID;
    if (!operationIdle) return ADMISSION_OPERATION_BUSY;

    const storage::PartitionEntry* selected = nullptr;
    for (uint16_t i = 0; i < table.partitionCount; ++i) {
        if (table.partitions[i].partitionNumber !=
                selectedSnapshot.partitionNumber) continue;
        if (selected) return ADMISSION_PARTITION_INVALID;
        selected = &table.partitions[i];
    }
    if (!selected || !partition_entries_equal(*selected, selectedSnapshot) ||
        selected->partitionNumber == 0 || selected->startLba == 0 ||
        selected->sectorCount == 0 || selected->startLba > selected->endLba ||
        selected->endLba >= capabilities.totalLogicalSectors ||
        selected->endLba - selected->startLba == UINT64_MAX ||
        selected->endLba - selected->startLba + 1u != selected->sectorCount ||
        !storage::checked_lba_range(capabilities.totalLogicalSectors,
                                    selected->startLba, selected->sectorCount) ||
        !storage::valid_geometry(selected->sectorCount,
                                 capabilities.logicalSectorSize, nullptr))
        return ADMISSION_PARTITION_INVALID;

    output.scope = VERIFY_PARTITION;
    output.request.targetSnapshot = target;
    output.request.startLba = selected->startLba;
    output.request.sectorCount = selected->sectorCount;
    output.partition = *selected;
    output.partitionTable = table;
    output.partitionTableCaptured = true;
    return ADMISSION_READY;
}

inline bool target_binding_matches_selection(
    const VerificationTarget& target, bool parentDiskRowSelected,
    bool partitionRowSelected, const storage::TargetIdentity& selectedTarget,
    const storage::PartitionEntry* selectedPartition)
{
    if (!storage::target_identities_equal(target.request.targetSnapshot,
                                          selectedTarget)) return false;
    if (target.scope == VERIFY_ENTIRE_DISK)
        return parentDiskRowSelected && !partitionRowSelected &&
            target.request.startLba == 0 &&
            target.request.sectorCount ==
                target.request.targetSnapshot.totalLogicalSectors;
    return target.scope == VERIFY_PARTITION && !parentDiskRowSelected &&
        partitionRowSelected && selectedPartition &&
        target.partitionTableCaptured &&
        partition_entries_equal(target.partition, *selectedPartition) &&
        target.request.startLba == target.partition.startLba &&
        target.request.sectorCount == target.partition.sectorCount;
}

inline bool verification_job_running(const storage::SurfaceVerificationJob& job)
{
    return job.result.state == storage::SURFACE_VERIFICATION_RUNNING;
}

inline bool request_stop_before_close(storage::SurfaceVerificationJob& job)
{
    if (!verification_job_running(job)) return true;
    storage::request_surface_verification_stop(job);
    return false;
}

inline int progress_value(const storage::SurfaceVerificationResult& result)
{
    if (result.state == storage::SURFACE_VERIFICATION_COMPLETED &&
        result.allSelectedSectorsReadable) return 100;
    return result.progressPercent >= 100 ? 99 : result.progressPercent;
}

inline const char* scope_name(VerificationScope scope)
{
    return scope == VERIFY_ENTIRE_DISK ? "Entire disk" : "Partition";
}

inline const char* terminal_message(
    const storage::SurfaceVerificationResult& result)
{
    if (result.state == storage::SURFACE_VERIFICATION_COMPLETED &&
        result.allSelectedSectorsReadable)
        return "All selected sectors were readable during this scan.";
    if (result.state == storage::SURFACE_VERIFICATION_STOPPED)
        return "Verification stopped — remaining sectors were not checked.";
    if (result.reason == storage::SURFACE_VERIFICATION_REASON_DEVICE_REMOVED)
        return "Verification stopped because the selected device is no longer available.";
    if (result.reason == storage::SURFACE_VERIFICATION_REASON_REGISTRATION_INVALIDATED ||
        result.reason == storage::SURFACE_VERIFICATION_REASON_TARGET_IDENTITY_CHANGED)
        return "Verification could not continue because the original device identity changed.";
    if (result.reason == storage::SURFACE_VERIFICATION_REASON_READ_FAILURE)
        return "Verification encountered a read error.";
    return result.state == storage::SURFACE_VERIFICATION_RUNNING
        ? "Verification is running." : "Verification did not complete.";
}

inline bool running_tab_navigation_enabled(bool regularRunningDialog,
                                           bool wholeEraseRunning,
                                           bool verificationRunning)
{
    return regularRunningDialog || wholeEraseRunning || verificationRunning;
}

inline bool running_tab_focuses_stop_button(bool wipeRunning,
                                            bool wholeEraseRunning,
                                            bool formatScanCancelable,
                                            bool verificationRunning)
{
    return wipeRunning || wholeEraseRunning || formatScanCancelable ||
        verificationRunning;
}

inline uint32_t stop_button_background(bool focused)
{
    return focused ? 0xFF34533D : 0xFF3D6349;
}

inline int active_action_count(const app::KernelWindow* window,
                               const int* candidates, int candidateCount)
{
    if (!window || !candidates || candidateCount <= 0) return 0;
    int count = 0;
    for (int i = 0; i < candidateCount; ++i) {
        for (int w = 0; w < window->widgetCount; ++w) {
            const app::Widget& widget = window->widgets[w];
            if (widget.id == candidates[i] &&
                widget.type == app::WidgetType::Button && widget.visible &&
                widget.enabled) {
                ++count;
                break;
            }
        }
    }
    return count;
}

inline int active_action_widget_id(const app::KernelWindow* window,
                                   const int* candidates, int candidateCount,
                                   int focusIndex)
{
    if (!window || !candidates || focusIndex < 0) return -1;
    int active = 0;
    for (int i = 0; i < candidateCount; ++i) {
        for (int w = 0; w < window->widgetCount; ++w) {
            const app::Widget& widget = window->widgets[w];
            if (widget.id != candidates[i] ||
                widget.type != app::WidgetType::Button || !widget.visible ||
                !widget.enabled) continue;
            if (active++ == focusIndex) return widget.id;
            break;
        }
    }
    return -1;
}

inline int move_active_action_focus(const app::KernelWindow* window,
                                    const int* candidates, int candidateCount,
                                    int focusIndex, int delta)
{
    const int count = active_action_count(window, candidates, candidateCount);
    if (count == 0) return 0;
    int next = focusIndex + delta;
    while (next < 0) next += count;
    return next % count;
}

} // namespace disk_manager_surface_verification_ui
} // namespace apps
} // namespace kernel

#endif
