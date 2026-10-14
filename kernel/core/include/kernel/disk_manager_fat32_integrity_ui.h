#ifndef KERNEL_DISK_MANAGER_FAT32_INTEGRITY_UI_H
#define KERNEL_DISK_MANAGER_FAT32_INTEGRITY_UI_H

#include "kernel/disk_manager_surface_verification_ui.h"
#include "kernel/fat32_integrity.h"

namespace kernel {
namespace apps {
namespace disk_manager_fat32_integrity_ui {

enum AdmissionBlockReason : uint8_t {
    ADMISSION_READY = 0,
    ADMISSION_SELECTION_REQUIRED,
    ADMISSION_UNSUPPORTED_PARTITION,
    ADMISSION_IDENTITY_INVALID,
    ADMISSION_GEOMETRY_INVALID,
    ADMISSION_READ_UNAVAILABLE,
    ADMISSION_PARTITION_TABLE_INVALID,
    ADMISSION_PARTITION_INVALID,
    ADMISSION_MOUNTED_TARGET,
    ADMISSION_OPERATION_BUSY,
};

enum WidgetSlot : uint8_t {
    WIDGET_CHECK_FAT32 = 0,
    WIDGET_START_CHECK,
    WIDGET_STOP_CHECK,
    WIDGET_PREVIOUS_FINDING,
    WIDGET_NEXT_FINDING,
    WIDGET_CLOSE,
};

// Verified from DiskManagerApp::init() add order: all widgets receive their
// zero-based widgetCount as the ID when they are created.
static const int CHECK_FAT32_FILESYSTEM_WIDGET_ID = 20;
static const int START_CHECK_WIDGET_ID = 11;
static const int STOP_CHECK_WIDGET_ID = 12;
static const int PREVIOUS_FINDING_WIDGET_ID = 21;
static const int NEXT_FINDING_WIDGET_ID = 22;
static const int CLOSE_DIALOG_WIDGET_ID = 12;

inline bool widget_ids_match(int checkFat32Id, int startCheckId,
                             int stopCheckId, int previousFindingId,
                             int nextFindingId, int closeDialogId)
{
    return checkFat32Id == CHECK_FAT32_FILESYSTEM_WIDGET_ID &&
        startCheckId == START_CHECK_WIDGET_ID &&
        stopCheckId == STOP_CHECK_WIDGET_ID &&
        previousFindingId == PREVIOUS_FINDING_WIDGET_ID &&
        nextFindingId == NEXT_FINDING_WIDGET_ID &&
        closeDialogId == CLOSE_DIALOG_WIDGET_ID;
}

struct CapturedTarget {
    storage::Fat32IntegrityRequest request;
    storage::PartitionTableModel partitionTable;
    bool partitionTableCaptured;
};

inline bool is_supported_partition_identity(
    const storage::PartitionEntry& partition)
{
    if (partition.isGpt) {
        // Microsoft Basic Data GUID in the byte order retained by the GPT
        // parser. The boot sector is deliberately not consulted here.
        static const uint8_t basicDataGuid[16] = {
            0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
            0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
        };
        for (uint8_t i = 0; i < 16; ++i)
            if (partition.typeGuid[i] != basicDataGuid[i]) return false;
        return true;
    }
    return partition.mbrType == 0x0B || partition.mbrType == 0x0C ||
        partition.mbrType == 0x1B || partition.mbrType == 0x1C;
}

inline const char* admission_reason_text(AdmissionBlockReason reason)
{
    switch (reason) {
        case ADMISSION_READY: return "Ready for read-only FAT32 checking";
        case ADMISSION_SELECTION_REQUIRED:
            return "Select a supported FAT32 partition row";
        case ADMISSION_UNSUPPORTED_PARTITION:
            return "The selected partition type is not a supported FAT32 candidate";
        case ADMISSION_IDENTITY_INVALID:
            return "The original device registration is unavailable or changed";
        case ADMISSION_GEOMETRY_INVALID:
            return "The device or partition sector geometry is unsupported";
        case ADMISSION_READ_UNAVAILABLE:
            return "The selected device does not support checked reads";
        case ADMISSION_PARTITION_TABLE_INVALID:
            return "The partition table is invalid or unsupported";
        case ADMISSION_PARTITION_INVALID:
            return "The selected partition bounds or identity are invalid";
        case ADMISSION_MOUNTED_TARGET:
            return "A volume on this device is mounted; unmount it explicitly first";
        case ADMISSION_OPERATION_BUSY:
            return "Another storage operation currently owns the device";
        default: return "FAT32 checking is unavailable";
    }
}

inline AdmissionBlockReason build_target(
    bool selectedPartitionRow, bool identityValid, bool operationIdle,
    bool unmounted, const storage::TargetIdentity& target,
    const storage::DeviceCapabilities& capabilities,
    const storage::PartitionTableModel& table,
    const storage::PartitionEntry& selectedPartition,
    CapturedTarget& output)
{
    output = {};
    if (!selectedPartitionRow) return ADMISSION_SELECTION_REQUIRED;
    if (!is_supported_partition_identity(selectedPartition))
        return ADMISSION_UNSUPPORTED_PARTITION;
    if (!identityValid ||
        !disk_manager_surface_verification_ui::live_target_matches_capabilities(
            target, capabilities)) return ADMISSION_IDENTITY_INVALID;
    if (!capabilities.geometryValid || !capabilities.capacityValid ||
        !storage::fat32_format_sector_size_supported(
            capabilities.logicalSectorSize) ||
        !storage::valid_geometry(capabilities.totalLogicalSectors,
            capabilities.logicalSectorSize, nullptr))
        return ADMISSION_GEOMETRY_INVALID;
    if (!capabilities.readable) return ADMISSION_READ_UNAVAILABLE;
    if (!disk_manager_surface_verification_ui::valid_partition_table(table))
        return ADMISSION_PARTITION_TABLE_INVALID;
    if (operationIdle == false) return ADMISSION_OPERATION_BUSY;
    if (!unmounted) return ADMISSION_MOUNTED_TARGET;

    const storage::PartitionEntry* selected = nullptr;
    for (uint16_t i = 0; i < table.partitionCount; ++i) {
        if (table.partitions[i].partitionNumber !=
                selectedPartition.partitionNumber) continue;
        if (selected) return ADMISSION_PARTITION_INVALID;
        selected = &table.partitions[i];
    }
    if (!selected ||
        !disk_manager_surface_verification_ui::partition_entries_equal(
            *selected, selectedPartition) ||
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

    storage::Fat32IntegrityRequest request = {};
    if (!storage::capture_fat32_integrity_request(
            target, table, *selected, request))
        return ADMISSION_PARTITION_INVALID;
    output.request = request;
    output.partitionTable = table;
    output.partitionTableCaptured = true;
    return ADMISSION_READY;
}

inline bool captured_target_matches_live(
    const CapturedTarget& captured, const CapturedTarget& live)
{
    if (!(captured.partitionTableCaptured && live.partitionTableCaptured &&
        storage::target_identities_equal(captured.request.targetSnapshot,
            live.request.targetSnapshot) &&
        captured.request.expectedRegistryGeneration ==
            live.request.expectedRegistryGeneration &&
        captured.request.partitionScheme == live.request.partitionScheme &&
        captured.request.mbrDiskSignature == live.request.mbrDiskSignature &&
        disk_manager_surface_verification_ui::partition_tables_equal(
            captured.partitionTable, live.partitionTable) &&
        disk_manager_surface_verification_ui::partition_entries_equal(
            captured.request.partitionSnapshot,
            live.request.partitionSnapshot))) return false;
    for (uint8_t i = 0; i < 16; ++i)
        if (captured.request.gptDiskGuid[i] != live.request.gptDiskGuid[i])
            return false;
    return true;
}

inline bool job_running(const storage::Fat32IntegrityJob& job)
{
    return job.result.state == storage::FAT32_INTEGRITY_RUNNING;
}

inline bool request_stop_before_close(storage::Fat32IntegrityJob& job)
{
    if (!job_running(job)) return true;
    storage::request_fat32_integrity_stop(job);
    return false;
}

inline const char* stage_name(const storage::Fat32IntegrityJob& job)
{
    switch (job.step) {
        case storage::FAT32_INTEGRITY_STEP_VALIDATE_CAPTURE:
            return "Validating captured target";
        case storage::FAT32_INTEGRITY_STEP_PRIMARY_BOOT:
        case storage::FAT32_INTEGRITY_STEP_BACKUP_BOOT:
        case storage::FAT32_INTEGRITY_STEP_FSINFO:
            return "FAT32 metadata";
        case storage::FAT32_INTEGRITY_STEP_FAT_RESERVED:
        case storage::FAT32_INTEGRITY_STEP_FAT_COMPARE_REFERENCE:
        case storage::FAT32_INTEGRITY_STEP_FAT_COMPARE_OTHER:
            return "FAT allocation";
        case storage::FAT32_INTEGRITY_STEP_STRUCTURE:
            switch (job.structuralStep) {
                case storage::FAT32_INTEGRITY_STRUCTURE_WALK_CLUSTER:
                case storage::FAT32_INTEGRITY_STRUCTURE_ADVANCE_CHAIN:
                case storage::FAT32_INTEGRITY_STRUCTURE_START_FILE:
                    return "Cluster-chain verification";
                case storage::FAT32_INTEGRITY_STRUCTURE_READ_DIRECTORY_SECTOR:
                case storage::FAT32_INTEGRITY_STRUCTURE_START_DIRECTORY:
                    return "Directory traversal";
                case storage::FAT32_INTEGRITY_STRUCTURE_SCAN_ORPHANS:
                    return "Allocation consistency";
                default: return "Structural verification";
            }
        case storage::FAT32_INTEGRITY_STEP_FINISHED:
            return "Finalization";
        default: return "Preparing check";
    }
}

inline const char* finding_category_name(
    storage::Fat32IntegrityFindingCategory category)
{
    using namespace storage;
    switch (category) {
        case FAT32_INTEGRITY_FINDING_BOOT_SIGNATURE: return "Boot signature";
        case FAT32_INTEGRITY_FINDING_MALFORMED_BPB: return "Malformed BPB";
        case FAT32_INTEGRITY_FINDING_BACKUP_BOOT_MISMATCH: return "Backup boot mismatch";
        case FAT32_INTEGRITY_FINDING_FSINFO_SIGNATURE: return "FSInfo signature";
        case FAT32_INTEGRITY_FINDING_FSINFO_HINT_UNVERIFIED: return "FSInfo hint";
        case FAT32_INTEGRITY_FINDING_FAT_RESERVED_ENTRY: return "FAT reserved entry";
        case FAT32_INTEGRITY_FINDING_FAT_COPY_MISMATCH: return "FAT copy mismatch";
        case FAT32_INTEGRITY_FINDING_READ_FAILURE: return "Read failure";
        case FAT32_INTEGRITY_FINDING_INVALID_CLUSTER_REFERENCE: return "Invalid cluster reference";
        case FAT32_INTEGRITY_FINDING_BAD_CLUSTER_IN_CHAIN: return "Bad cluster in chain";
        case FAT32_INTEGRITY_FINDING_CHAIN_CYCLE: return "Cluster-chain loop";
        case FAT32_INTEGRITY_FINDING_CROSS_LINKED_CLUSTER: return "Cross-linked cluster";
        case FAT32_INTEGRITY_FINDING_FILE_SIZE_MISMATCH: return "File-size/allocation mismatch";
        case FAT32_INTEGRITY_FINDING_ORPHANED_ALLOCATION: return "Orphaned allocation";
        case FAT32_INTEGRITY_FINDING_DIRECTORY_LOOP: return "Directory loop";
        case FAT32_INTEGRITY_FINDING_INVALID_DIRECTORY_REFERENCE: return "Invalid directory reference";
        case FAT32_INTEGRITY_FINDING_INVALID_DIRECTORY_ENTRY: return "Invalid directory entry";
        case FAT32_INTEGRITY_FINDING_LFN_CHECKSUM: return "VFAT checksum";
        case FAT32_INTEGRITY_FINDING_LFN_SEQUENCE: return "VFAT sequence";
        case FAT32_INTEGRITY_FINDING_ORPHANED_LFN: return "Orphaned VFAT entry";
        default: return "Unknown checker finding";
    }
}

inline const char* structure_name(storage::Fat32IntegrityStructure structure)
{
    using namespace storage;
    switch (structure) {
        case FAT32_INTEGRITY_STRUCTURE_PRIMARY_BOOT: return "Primary boot sector";
        case FAT32_INTEGRITY_STRUCTURE_BACKUP_BOOT: return "Backup boot sector";
        case FAT32_INTEGRITY_STRUCTURE_FSINFO: return "FSInfo";
        case FAT32_INTEGRITY_STRUCTURE_FAT: return "FAT";
        case FAT32_INTEGRITY_STRUCTURE_DATA_REGION: return "Data region";
        case FAT32_INTEGRITY_STRUCTURE_DIRECTORY: return "Directory";
        default: return "Unknown structure";
    }
}

inline const char* severity_name(storage::Fat32IntegrityFindingSeverity severity)
{
    return severity == storage::FAT32_INTEGRITY_ADVISORY
        ? "Advisory" : "Structural inconsistency";
}

inline const char* terminal_message(const storage::Fat32IntegrityResult& result)
{
    using namespace storage;
    if (result.state == FAT32_INTEGRITY_PASSED &&
        result.structuralAnalysisComplete && result.orphanAnalysisComplete)
        return "No FAT32 structural inconsistencies were detected in the checks completed.";
    if (result.state == FAT32_INTEGRITY_INCONSISTENCY)
        return "Inconsistencies were found. No repairs were performed.";
    if (result.state == FAT32_INTEGRITY_STOPPED)
        return "Check stopped; unchecked structures remain unverified.";
    if (result.reason == FAT32_INTEGRITY_REASON_DEVICE_REMOVED)
        return "The original target was removed; this check is terminal.";
    if (result.reason == FAT32_INTEGRITY_REASON_REGISTRATION_INVALIDATED ||
        result.reason == FAT32_INTEGRITY_REASON_TARGET_IDENTITY_CHANGED)
        return "The original target identity was invalidated; no replacement was checked.";
    if (result.reason == FAT32_INTEGRITY_REASON_READ_FAILURE)
        return "A checked read failed; the storage device is not thereby diagnosed as defective.";
    if (result.reason == FAT32_INTEGRITY_REASON_RESOURCE_LIMIT ||
        result.reason == FAT32_INTEGRITY_REASON_UNSUPPORTED_SCALE)
        return "The checker reached a coverage or resource limit; the result is incomplete.";
    if (result.reason == FAT32_INTEGRITY_REASON_MOUNTED_TARGET)
        return "The target device has a mounted volume; no check was started.";
    if (result.state == FAT32_INTEGRITY_INVALID_GEOMETRY)
        return "The selected partition geometry is invalid or unsupported.";
    if (result.state == FAT32_INTEGRITY_INCOMPLETE)
        return "The check did not complete; unverified structures remain.";
    if (result.state == FAT32_INTEGRITY_NOT_ADMITTED)
        return "The selected target was not admitted for checking.";
    return result.state == FAT32_INTEGRITY_RUNNING
        ? "FAT32 structural verification is running." :
          "The check did not complete.";
}

inline const char* findings_overflow_message(bool additionalFindingsMayExist)
{
    return additionalFindingsMayExist
        ? "Additional inconsistencies were detected but could not be individually displayed."
        : "";
}

} // namespace disk_manager_fat32_integrity_ui
} // namespace apps
} // namespace kernel

#endif
