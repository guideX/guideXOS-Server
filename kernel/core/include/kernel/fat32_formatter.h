#ifndef KERNEL_FAT32_FORMATTER_H
#define KERNEL_FAT32_FORMATTER_H

#include "kernel/disk_initialization.h"

namespace kernel {
namespace storage {

// The current VFS only probes and mounts filesystems on 512-byte logical
// sectors. Keep the formatter aligned with that end-to-end contract.
static const uint32_t FAT32_FORMAT_SECTOR_SIZE = 512;
static const uint32_t FAT32_FORMAT_ROLLBACK_LIMIT_BYTES = 1024u * 1024u;
static const uint32_t FAT32_FORMAT_MAX_CLUSTERS = 120000u;
static const uint32_t FAT32_FORMAT_MIN_CLUSTERS = 65525u;
static const uint32_t FAT32_FORMAT_MAX_SECTORS_PER_CLUSTER = 64u;

enum Fat32FormatStatus : uint8_t {
    FAT32_FORMAT_READY = 0,
    FAT32_FORMAT_SUCCESS,
    FAT32_FORMAT_OPERATION_BUSY,
    FAT32_FORMAT_INVALID_REQUEST,
    FAT32_FORMAT_DEVICE_MISSING,
    FAT32_FORMAT_REGISTRY_CHANGED,
    FAT32_FORMAT_IDENTITY_CHANGED,
    FAT32_FORMAT_PARTITION_DISAPPEARED,
    FAT32_FORMAT_PARTITION_IDENTITY_CHANGED,
    FAT32_FORMAT_INVALID_TABLE,
    FAT32_FORMAT_INVALID_GEOMETRY,
    FAT32_FORMAT_READ_UNAVAILABLE,
    FAT32_FORMAT_WRITE_UNAVAILABLE,
    FAT32_FORMAT_READ_ONLY,
    FAT32_FORMAT_DURABILITY_UNKNOWN,
    FAT32_FORMAT_FLUSH_UNAVAILABLE,
    FAT32_FORMAT_FLUSH_FAILED,
    FAT32_FORMAT_MOUNTED,
    FAT32_FORMAT_ROOT_BACKING,
    FAT32_FORMAT_BOOT_BACKING,
    FAT32_FORMAT_BOOT_IDENTITY_UNKNOWN,
    FAT32_FORMAT_UNSUPPORTED_SECTOR_SIZE,
    FAT32_FORMAT_TOO_SMALL,
    FAT32_FORMAT_LAYOUT_OVERFLOW,
    FAT32_FORMAT_FILESYSTEM_ALREADY_RECOGNIZED,
    FAT32_FORMAT_AMBIGUOUS_EXISTING_DATA,
    FAT32_FORMAT_LABEL_INVALID,
    FAT32_FORMAT_VOLUME_ID_UNAVAILABLE,
    FAT32_FORMAT_ROLLBACK_BUFFER_LIMIT,
    FAT32_FORMAT_SNAPSHOT_FAILED,
    FAT32_FORMAT_OPERATION_OWNERSHIP_INVALID,
    FAT32_FORMAT_METADATA_WRITE_FAILED,
    FAT32_FORMAT_VERIFICATION_FAILED,
    FAT32_FORMAT_RESCAN_FAILED,
    FAT32_FORMAT_ROLLBACK_FAILED,
};

enum Fat32FormatStage : uint8_t {
    FAT32_FORMAT_STAGE_IDLE = 0,
    FAT32_FORMAT_STAGE_VALIDATING,
    FAT32_FORMAT_STAGE_REVALIDATING_PARTITION,
    FAT32_FORMAT_STAGE_CALCULATING_LAYOUT,
    FAT32_FORMAT_STAGE_SNAPSHOTTING,
    FAT32_FORMAT_STAGE_WRITING_METADATA,
    FAT32_FORMAT_STAGE_FLUSHING,
    FAT32_FORMAT_STAGE_VERIFYING,
    FAT32_FORMAT_STAGE_RESCANNING,
    FAT32_FORMAT_STAGE_COMPLETED,
    FAT32_FORMAT_STAGE_FAILED,
    FAT32_FORMAT_STAGE_STATE_UNCERTAIN,
    FAT32_FORMAT_STAGE_SNAPSHOT_FILESYSTEM_METADATA,
    FAT32_FORMAT_STAGE_WRITE_FAT,
    FAT32_FORMAT_STAGE_WRITE_ROOT,
    FAT32_FORMAT_STAGE_WRITE_BACKUP_METADATA,
    FAT32_FORMAT_STAGE_WRITE_FSINFO,
    FAT32_FORMAT_STAGE_WRITE_BOOT_SECTOR,
    FAT32_FORMAT_STAGE_FLUSH,
    FAT32_FORMAT_STAGE_VERIFY,
    FAT32_FORMAT_STAGE_RESCAN,
    FAT32_FORMAT_STAGE_ROLLBACK_WRITE,
    FAT32_FORMAT_STAGE_ROLLBACK_FLUSH,
    FAT32_FORMAT_STAGE_ROLLBACK_VERIFY,
    FAT32_FORMAT_STAGE_ACQUIRE_LEASE,
    FAT32_FORMAT_STAGE_PIN_TARGET,
};

enum Fat32ExistingState : uint8_t {
    FAT32_EXISTING_UNKNOWN = 0,
    FAT32_EXISTING_CLEAN,
    FAT32_EXISTING_RECOGNIZED_FILESYSTEM,
    FAT32_EXISTING_AMBIGUOUS_DATA,
    FAT32_EXISTING_UNREADABLE,
};

enum Fat32FinalProbeState : uint8_t {
    FAT32_FINAL_PROBE_UNKNOWN = 0,
    FAT32_FINAL_PROBE_UNFORMATTED,
    FAT32_FINAL_PROBE_FAT32,
};

struct Fat32FormatGeometry {
    uint32_t bytesPerSector;
    uint32_t sectorsPerCluster;
    uint32_t clusterSizeBytes;
    uint32_t reservedSectorCount;
    uint32_t fatCount;
    uint32_t fatSizeSectors;
    uint32_t firstFatSector;
    uint32_t secondFatSector;
    uint32_t firstDataSector;
    uint32_t rootCluster;
    uint32_t clusterCount;
    uint32_t freeClusterCount;
    uint32_t totalSectors;
    uint32_t fsInfoSector;
    uint32_t backupFsInfoSector;
    uint32_t backupBootSector;
    uint32_t volumeId;
    uint32_t hiddenSectors;
    uint32_t rollbackSnapshotSectors;
    uint32_t rollbackSnapshotBytes;
};

struct Fat32FormatRequest {
    TargetIdentity targetSnapshot;
    PartitionScheme partitionScheme;
    PartitionEntry partitionSnapshot;
    // Stable disk identity for the selected table: GPT disk GUID or MBR
    // signature. It prevents a valid partition tuple on a replacement table
    // from being mistaken for the original selection.
    uint8_t gptDiskGuid[16];
    uint32_t mbrDiskSignature;
    char volumeLabel[12];
    uint64_t expectedRegistryGeneration;
#if defined(KERNEL_STORAGE_TEST)
    bool testVolumeIdProvided;
    uint32_t testVolumeId;
#endif
};

struct Fat32FormatResult {
    Fat32FormatStatus status;
    Fat32FormatStatus failureStatus;
    Fat32FormatStage stage;
    Fat32FormatStage lastStage;
    Fat32FormatStage firstFailedStage;
    Fat32ExistingState existingState;
    Fat32FinalProbeState finalProbeState;
    TargetIdentity targetIdentity;
    PartitionEntry partition;
    Fat32FormatGeometry geometry;
    uint64_t sectorsWritten;
    bool writeMayHaveReachedMedia;
    bool failedBeforeWrite;
    bool blockStatusValid;
    block::Status blockStatus;
    block::OperationKind failedOperation;
    block::OperationDiagnostic failedBlockDiagnostic;
    block::FlushOutcome flushOutcome;
    block::Status flushStatus;
    bool flushAttempted;
    uint32_t flushAttempts;
    bool persistenceTrusted;
    bool writeAttempted;
    bool verificationPassed;
    bool rollbackAttempted;
    bool rollbackSucceeded;
    Fat32FormatStage rollbackStage;
    bool rollbackWriteAttempted;
    uint32_t rollbackSectorsWritten;
    block::Status rollbackWriteStatus;
    bool rollbackFlushAttempted;
    block::FlushOutcome rollbackFlushOutcome;
    block::Status rollbackFlushStatus;
    bool rollbackVerificationPassed;
    bool finalStateUncertain;
    char diagnostic[160];
};

// Deterministic calculator and label normalizer are also used by tests and
// future storage clients; neither function performs device I/O.
Fat32FormatStatus calculate_fat32_format_geometry(
    uint64_t partitionStartLba, uint64_t partitionSectorCount,
    uint32_t logicalSectorSize, Fat32FormatGeometry& result);
Fat32FormatStatus normalize_fat32_volume_label(
    const char* input, char normalized[11]);

// Preflight includes target/table/partition identity, policy checks, and a
// conservative all-zero partition scan. It never writes.
Fat32FormatStatus probe_fat32_format_partition(
    const Fat32FormatRequest& request, Fat32FormatResult& result);

// Revalidates again under the shared operation lease and formats only the
// selected bounded partition extent. It deliberately does not mount it.
Fat32FormatStatus format_fat32_partition(
    const Fat32FormatRequest& request, Fat32FormatResult& result);

const char* fat32_format_status_name(Fat32FormatStatus status);
const char* fat32_format_stage_name(Fat32FormatStage stage);
const char* fat32_existing_state_name(Fat32ExistingState state);

} // namespace storage
} // namespace kernel

#endif
