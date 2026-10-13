#ifndef KERNEL_FAT32_INTEGRITY_H
#define KERNEL_FAT32_INTEGRITY_H

#include "kernel/disk_initialization.h"

namespace kernel {
namespace storage {

// FAT copies are compared in resumable chunks. The bound is independent of
// the volume size and fits the largest supported logical-sector geometry.
static const uint32_t FAT32_INTEGRITY_COMPARE_BUFFER_BYTES = 32u * 1024u;
static const uint8_t FAT32_INTEGRITY_MAX_FINDINGS = 32;

struct Fat32IntegrityRequest {
    TargetIdentity targetSnapshot;
    PartitionScheme partitionScheme;
    PartitionEntry partitionSnapshot;
    // Stable parent-table identity, captured with the selected partition.
    uint8_t gptDiskGuid[16];
    uint32_t mbrDiskSignature;
    uint64_t expectedRegistryGeneration;
};

struct Fat32IntegrityGeometry {
    uint32_t bytesPerSector;
    uint32_t sectorsPerCluster;
    uint32_t reservedSectorCount;
    uint32_t fatCount;
    uint32_t fatSizeSectors;
    uint32_t totalSectors;
    uint32_t rootCluster;
    uint32_t clusterCount;
    uint32_t fsInfoSector;
    uint32_t backupBootSector;
    uint32_t extFlags;
    uint32_t activeFat;
    uint64_t firstFatSector;
    uint64_t firstDataSector;
    uint64_t dataSectorCount;
    uint64_t fatBytes;
    uint64_t rootDirectorySector;
    bool fatMirroringEnabled;
};

enum Fat32IntegrityState : uint8_t {
    FAT32_INTEGRITY_NOT_ADMITTED = 0,
    FAT32_INTEGRITY_RUNNING,
    FAT32_INTEGRITY_PASSED,
    FAT32_INTEGRITY_INCONSISTENCY,
    FAT32_INTEGRITY_STOPPED,
    FAT32_INTEGRITY_READ_ERROR,
    FAT32_INTEGRITY_INVALID_GEOMETRY,
    FAT32_INTEGRITY_TARGET_INVALIDATED,
    FAT32_INTEGRITY_INCOMPLETE,
};

enum Fat32IntegrityReason : uint8_t {
    FAT32_INTEGRITY_REASON_NONE = 0,
    FAT32_INTEGRITY_REASON_INVALID_REQUEST,
    FAT32_INTEGRITY_REASON_OPERATION_BUSY,
    FAT32_INTEGRITY_REASON_MOUNTED_TARGET,
    FAT32_INTEGRITY_REASON_OWNERSHIP_INVALID,
    FAT32_INTEGRITY_REASON_DEVICE_REMOVED,
    FAT32_INTEGRITY_REASON_REGISTRATION_INVALIDATED,
    FAT32_INTEGRITY_REASON_TARGET_IDENTITY_CHANGED,
    FAT32_INTEGRITY_REASON_PARTITION_IDENTITY_INVALID,
    FAT32_INTEGRITY_REASON_UNSUPPORTED_SECTOR_SIZE,
    FAT32_INTEGRITY_REASON_INVALID_GEOMETRY,
    FAT32_INTEGRITY_REASON_READ_UNAVAILABLE,
    FAT32_INTEGRITY_REASON_READ_FAILURE,
    FAT32_INTEGRITY_REASON_STOP_REQUESTED,
    FAT32_INTEGRITY_REASON_INTERNAL_BOUNDS_FAILURE,
};

enum Fat32IntegrityFindingCategory : uint8_t {
    FAT32_INTEGRITY_FINDING_BOOT_SIGNATURE = 0,
    FAT32_INTEGRITY_FINDING_MALFORMED_BPB,
    FAT32_INTEGRITY_FINDING_BACKUP_BOOT_MISMATCH,
    FAT32_INTEGRITY_FINDING_FSINFO_SIGNATURE,
    FAT32_INTEGRITY_FINDING_FSINFO_HINT_UNVERIFIED,
    FAT32_INTEGRITY_FINDING_FAT_RESERVED_ENTRY,
    FAT32_INTEGRITY_FINDING_FAT_COPY_MISMATCH,
    FAT32_INTEGRITY_FINDING_READ_FAILURE,
};

enum Fat32IntegrityStructure : uint8_t {
    FAT32_INTEGRITY_STRUCTURE_PRIMARY_BOOT = 0,
    FAT32_INTEGRITY_STRUCTURE_BACKUP_BOOT,
    FAT32_INTEGRITY_STRUCTURE_FSINFO,
    FAT32_INTEGRITY_STRUCTURE_FAT,
    FAT32_INTEGRITY_STRUCTURE_DATA_REGION,
};

enum Fat32IntegrityFindingSeverity : uint8_t {
    FAT32_INTEGRITY_ADVISORY = 0,
    FAT32_INTEGRITY_STRUCTURAL_INCONSISTENCY,
};

struct Fat32IntegrityFinding {
    Fat32IntegrityFindingCategory category;
    Fat32IntegrityStructure structure;
    Fat32IntegrityFindingSeverity severity;
    uint64_t lba;
    uint32_t sectorCount;
    uint32_t byteOffset;
    uint64_t expected;
    uint64_t observed;
    bool expectedObservedValid;
};

struct Fat32IntegrityResult {
    Fat32IntegrityState state;
    Fat32IntegrityReason reason;
    TargetIdentity targetIdentity;
    PartitionScheme partitionScheme;
    PartitionEntry partition;
    uint64_t partitionStartLba;
    uint64_t partitionSectorCount;
    uint64_t operationId;
    Fat32IntegrityGeometry geometry;
    Fat32IntegrityFinding findings[FAT32_INTEGRITY_MAX_FINDINGS];
    uint8_t findingCount;
    bool additionalFindingsMayExist;
    bool structuralInconsistencyFound;
    bool stopRequested;
    bool ownershipAcquired;
    bool ownershipReleased;
    bool blockStatusValid;
    block::Status blockStatus;
    block::OperationDiagnostic blockDiagnostic;
    uint64_t failedLba;
    uint32_t failedSectorCount;
    uint64_t metadataReadRequests;
    uint64_t bytesRead;
    uint64_t fatBytesCompared;
    uint64_t fatBytesToCompare;
    uint8_t progressPercent;
    char diagnostic[160];
};

enum Fat32IntegrityStep : uint8_t {
    FAT32_INTEGRITY_STEP_VALIDATE_CAPTURE = 0,
    FAT32_INTEGRITY_STEP_PRIMARY_BOOT,
    FAT32_INTEGRITY_STEP_BACKUP_BOOT,
    FAT32_INTEGRITY_STEP_FSINFO,
    FAT32_INTEGRITY_STEP_FAT_RESERVED,
    FAT32_INTEGRITY_STEP_FAT_COMPARE_REFERENCE,
    FAT32_INTEGRITY_STEP_FAT_COMPARE_OTHER,
    FAT32_INTEGRITY_STEP_FINISHED,
};

struct Fat32IntegrityJob {
    Fat32IntegrityRequest request;
    Fat32IntegrityResult result;
    StorageOperationLease lease;
    Fat32IntegrityStep step;
    uint64_t compareByteOffset;
    uint32_t compareChunkBytes;
    uint32_t compareChunkSectors;
    uint32_t maxTransferBytes;
    uint8_t reservedFatIndex;
    uint8_t compareReferenceFat;
    uint8_t compareOtherFat;
    bool stepActive;
    bool ownershipReleaseAttempted;
};

// Bind a selected entry to its already parsed, valid table snapshot. This
// helper performs no I/O; the table and target snapshots must come from the
// caller's current inventory/selection transaction.
bool capture_fat32_integrity_request(
    const TargetIdentity& target, const PartitionTableModel& table,
    const PartitionEntry& partition, Fat32IntegrityRequest& request);

Fat32IntegrityState begin_fat32_integrity_job(
    Fat32IntegrityJob& job, const Fat32IntegrityRequest& request);
Fat32IntegrityState step_fat32_integrity_job(Fat32IntegrityJob& job);
bool request_fat32_integrity_stop(Fat32IntegrityJob& job);

// Checked cluster mapping shared by the metadata verifier and future bounded
// chain/directory walkers. The result is relative to the FAT32 volume.
bool fat32_integrity_cluster_sector(
    const Fat32IntegrityGeometry& geometry, uint32_t cluster,
    uint64_t& volumeRelativeSector);

const char* fat32_integrity_state_name(Fat32IntegrityState state);
const char* fat32_integrity_reason_name(Fat32IntegrityReason reason);

} // namespace storage
} // namespace kernel

#endif
