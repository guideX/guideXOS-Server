#ifndef KERNEL_PARTITION_OPERATIONS_H
#define KERNEL_PARTITION_OPERATIONS_H

#include "kernel/disk_manager_model.h"
#include "kernel/disk_initialization.h"

namespace kernel {
namespace storage {

enum CreatePartitionType : uint8_t {
    CREATE_PARTITION_GPT_BASIC_DATA = 0,
    CREATE_PARTITION_MBR_FAT32_LBA,
};

enum CreatePartitionStatus : uint8_t {
    CREATE_PARTITION_READY = 0,
    CREATE_PARTITION_SUCCESS,
    CREATE_PARTITION_OPERATION_BUSY,
    CREATE_PARTITION_INVALID_REQUEST,
    CREATE_PARTITION_DEVICE_MISSING,
    CREATE_PARTITION_REGISTRY_CHANGED,
    CREATE_PARTITION_IDENTITY_CHANGED,
    CREATE_PARTITION_INVALID_GEOMETRY,
    CREATE_PARTITION_READ_UNAVAILABLE,
    CREATE_PARTITION_WRITE_UNAVAILABLE,
    CREATE_PARTITION_READ_ONLY,
    CREATE_PARTITION_DURABILITY_UNKNOWN,
    CREATE_PARTITION_FLUSH_UNAVAILABLE,
    CREATE_PARTITION_MOUNTED,
    CREATE_PARTITION_ROOT_BACKING,
    CREATE_PARTITION_BOOT_BACKING,
    CREATE_PARTITION_BOOT_IDENTITY_UNKNOWN,
    CREATE_PARTITION_INVALID_TABLE,
    CREATE_PARTITION_GPT_DEGRADED,
    CREATE_PARTITION_UNSUPPORTED_SCHEME,
    CREATE_PARTITION_STALE_REGION,
    CREATE_PARTITION_NO_GPT_ENTRY,
    CREATE_PARTITION_NO_MBR_ENTRY,
    CREATE_PARTITION_NO_ALIGNED_SPACE,
    CREATE_PARTITION_TOO_SMALL,
    CREATE_PARTITION_TOO_LARGE,
    CREATE_PARTITION_UNSUPPORTED_TYPE,
    CREATE_PARTITION_GUID_UNAVAILABLE,
    CREATE_PARTITION_GUID_COLLISION,
    CREATE_PARTITION_OPERATION_OWNERSHIP_INVALID,
    CREATE_PARTITION_IO_FAILED,
    CREATE_PARTITION_FLUSH_FAILED,
    CREATE_PARTITION_VERIFICATION_FAILED,
    CREATE_PARTITION_RESCAN_FAILED,
    CREATE_PARTITION_ROLLBACK_FAILED,
};

enum CreatePartitionStage : uint8_t {
    CREATE_PARTITION_STAGE_IDLE = 0,
    CREATE_PARTITION_STAGE_VALIDATING,
    CREATE_PARTITION_STAGE_REVALIDATING_FREE_SPACE,
    CREATE_PARTITION_STAGE_SNAPSHOTTING,
    CREATE_PARTITION_STAGE_WRITING_GPT,
    CREATE_PARTITION_STAGE_WRITING_MBR,
    CREATE_PARTITION_STAGE_FLUSHING,
    CREATE_PARTITION_STAGE_VERIFYING,
    CREATE_PARTITION_STAGE_RESCANNING,
    CREATE_PARTITION_STAGE_COMPLETED,
    CREATE_PARTITION_STAGE_FAILED,
    CREATE_PARTITION_STAGE_STATE_UNCERTAIN,
    CREATE_PARTITION_STAGE_ACQUIRE_LEASE,
    CREATE_PARTITION_STAGE_PIN_TARGET,
    CREATE_PARTITION_STAGE_PREFLIGHT,
    CREATE_PARTITION_STAGE_SNAPSHOT_TABLE,
    CREATE_PARTITION_STAGE_PREPARE_METADATA,
    CREATE_PARTITION_STAGE_WRITE_BACKUP_GPT,
    CREATE_PARTITION_STAGE_WRITE_PRIMARY_GPT,
    CREATE_PARTITION_STAGE_FLUSH,
    CREATE_PARTITION_STAGE_VERIFY,
    CREATE_PARTITION_STAGE_RESCAN,
    CREATE_PARTITION_STAGE_ROLLBACK_WRITE,
    CREATE_PARTITION_STAGE_ROLLBACK_FLUSH,
    CREATE_PARTITION_STAGE_ROLLBACK_VERIFY,
};

enum CreatePartitionWriteStage : uint32_t {
    CREATE_PARTITION_WRITE_BACKUP_ARRAY = 1u << 0,
    CREATE_PARTITION_WRITE_BACKUP_HEADER = 1u << 1,
    CREATE_PARTITION_WRITE_PRIMARY_ARRAY = 1u << 2,
    CREATE_PARTITION_WRITE_PRIMARY_HEADER = 1u << 3,
    CREATE_PARTITION_WRITE_MBR_ENTRY = 1u << 4,
};

static const uint64_t CREATE_PARTITION_ALIGNMENT_BYTES = 1024ull * 1024ull;
static const uint64_t CREATE_PARTITION_MINIMUM_BYTES = 1024ull * 1024ull;
static const uint8_t GPT_TYPE_BASIC_DATA_GUID[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};
static const uint8_t MBR_TYPE_FAT32_LBA = 0x0C;

struct CreatePartitionRequest {
    TargetIdentity targetSnapshot;
    PartitionScheme requestedScheme;
    UnallocatedRegion selectedRegion;
    uint64_t requestedSizeBytes;
    bool useMaximumSize;
    CreatePartitionType partitionType;
    char gptName[64];
    uint64_t expectedRegistryGeneration;
#if defined(KERNEL_STORAGE_TEST)
    bool testGuidProvided;
    uint8_t testUniqueGuid[16];
#endif
};

struct CreatePartitionProbe {
    CreatePartitionStatus status;
    DiskState detectedState;
    uint64_t firstAlignedLba;
    uint64_t maximumSectorCount;
    uint64_t maximumBytes;
    uint16_t freeGptEntries;
    uint8_t freeMbrEntries;
};

struct CreatePartitionResult {
    CreatePartitionStatus status;
    CreatePartitionStatus failureStatus;
    CreatePartitionStage stage;
    CreatePartitionStage lastStage;
    CreatePartitionStage firstFailedStage;
    TargetIdentity targetIdentity;
    PartitionScheme requestedScheme;
    PartitionEntry createdPartition;
    uint16_t partitionTableSlot;
    uint32_t writeStagesCompleted;
    bool writeAttempted;
    uint32_t writesCompleted;
    bool writeMayHaveReachedMedia;
    bool failedBeforeWrite;
    bool blockStatusValid;
    block::Status blockStatus;
    block::OperationKind failedOperation;
    block::OperationDiagnostic failedBlockDiagnostic;
    bool flushAttempted;
    uint32_t flushAttempts;
    block::FlushOutcome flushOutcome;
    block::Status flushStatus;
    bool verificationPassed;
    DiskState finalDetectedState;
    uint16_t finalPartitionCount;
    uint16_t finalUnallocatedRegionCount;
    bool rollbackAttempted;
    bool rollbackSucceeded;
    CreatePartitionStage rollbackStage;
    bool rollbackWriteAttempted;
    block::Status rollbackWriteStatus;
    bool rollbackFlushAttempted;
    block::FlushOutcome rollbackFlushOutcome;
    block::Status rollbackFlushStatus;
    bool rollbackVerificationPassed;
    bool finalStateUncertain;
    char diagnostic[128];
};

// Short-lived authoritative eligibility check for a selected, exact gap.
// The UI uses this only to decide whether to expose its contextual action.
CreatePartitionStatus probe_create_partition(
    const TargetIdentity& target, PartitionScheme scheme,
    const UnallocatedRegion& selectedRegion, CreatePartitionProbe& result);

// One confirmed operation: acquires the shared storage lease, pins/revalidates
// the target, reparses and rederives the exact gap, snapshots metadata, writes,
// flushes, verifies through the normal parser, rescans, and releases the lease.
CreatePartitionStatus create_partition(
    const CreatePartitionRequest& request, CreatePartitionResult& result);

const char* create_partition_status_name(CreatePartitionStatus status);
const char* create_partition_stage_name(CreatePartitionStage stage);

enum DeletePartitionStatus : uint8_t {
    DELETE_PARTITION_READY_FOR_CONFIRMATION = 0,
    DELETE_PARTITION_SUCCESS,
    DELETE_PARTITION_OPERATION_BUSY,
    DELETE_PARTITION_INVALID_REQUEST,
    DELETE_PARTITION_DEVICE_MISSING,
    DELETE_PARTITION_REGISTRY_CHANGED,
    DELETE_PARTITION_IDENTITY_CHANGED,
    DELETE_PARTITION_INVALID_GEOMETRY,
    DELETE_PARTITION_READ_UNAVAILABLE,
    DELETE_PARTITION_WRITE_UNAVAILABLE,
    DELETE_PARTITION_READ_ONLY,
    DELETE_PARTITION_DURABILITY_UNKNOWN,
    DELETE_PARTITION_FLUSH_UNAVAILABLE,
    DELETE_PARTITION_MOUNTED,
    DELETE_PARTITION_MOUNT_STATE_UNKNOWN,
    DELETE_PARTITION_ROOT_BACKING,
    DELETE_PARTITION_BOOT_BACKING,
    DELETE_PARTITION_BOOT_IDENTITY_UNKNOWN,
    DELETE_PARTITION_INVALID_TABLE,
    DELETE_PARTITION_GPT_DEGRADED,
    DELETE_PARTITION_UNSUPPORTED_SCHEME,
    DELETE_PARTITION_STALE_SELECTION,
    DELETE_PARTITION_OPERATION_OWNERSHIP_INVALID,
    DELETE_PARTITION_IO_FAILED,
    DELETE_PARTITION_FLUSH_FAILED,
    DELETE_PARTITION_VERIFICATION_FAILED,
    DELETE_PARTITION_RESCAN_FAILED,
    DELETE_PARTITION_ROLLBACK_FAILED,
};

enum DeletePartitionStage : uint8_t {
    DELETE_PARTITION_STAGE_IDLE = 0,
    DELETE_PARTITION_STAGE_ACQUIRE_LEASE,
    DELETE_PARTITION_STAGE_PIN_TARGET,
    DELETE_PARTITION_STAGE_PREFLIGHT,
    DELETE_PARTITION_STAGE_WAITING_FOR_CONFIRMATION,
    DELETE_PARTITION_STAGE_REVALIDATING,
    DELETE_PARTITION_STAGE_SNAPSHOT,
    DELETE_PARTITION_STAGE_PREPARE_METADATA,
    DELETE_PARTITION_STAGE_WRITE_BACKUP_GPT,
    DELETE_PARTITION_STAGE_VERIFY_BACKUP_GPT,
    DELETE_PARTITION_STAGE_WRITE_PRIMARY_GPT,
    DELETE_PARTITION_STAGE_WRITE_MBR,
    DELETE_PARTITION_STAGE_FLUSH,
    DELETE_PARTITION_STAGE_VERIFY,
    DELETE_PARTITION_STAGE_RESCAN,
    DELETE_PARTITION_STAGE_ROLLBACK_WRITE,
    DELETE_PARTITION_STAGE_ROLLBACK_FLUSH,
    DELETE_PARTITION_STAGE_ROLLBACK_VERIFY,
    DELETE_PARTITION_STAGE_COMPLETED,
    DELETE_PARTITION_STAGE_FAILED,
    DELETE_PARTITION_STAGE_STATE_UNCERTAIN,
};

enum DeletePartitionWriteStage : uint32_t {
    DELETE_PARTITION_WRITE_BACKUP_ARRAY = 1u << 0,
    DELETE_PARTITION_WRITE_BACKUP_HEADER = 1u << 1,
    DELETE_PARTITION_WRITE_PRIMARY_ARRAY = 1u << 2,
    DELETE_PARTITION_WRITE_PRIMARY_HEADER = 1u << 3,
    DELETE_PARTITION_WRITE_MBR_ENTRY = 1u << 4,
};

struct DeletePartitionRequest {
    TargetIdentity targetSnapshot;
    PartitionScheme partitionScheme;
    PartitionEntry partitionSnapshot;
    uint64_t expectedRegistryGeneration;
};

struct DeletePartitionPlan {
    StorageOperationLease lease;
    TargetIdentity targetSnapshot;
    PartitionScheme partitionScheme;
    PartitionEntry partitionSnapshot;
    uint64_t expectedRegistryGeneration;
    DiskState parserState;
    uint16_t partitionCount;
    uint16_t tableEntryCount;
    uint32_t gptEntrySize;
    uint32_t gptEntryArraySectors;
    uint64_t primaryGptEntryArrayLba;
    uint64_t backupGptEntryArrayLba;
    uint64_t firstUsableLba;
    uint64_t lastUsableLba;
    uint32_t primaryGptHeaderCrc32;
    uint32_t backupGptHeaderCrc32;
    uint32_t primaryGptEntryArrayCrc32;
    uint32_t backupGptEntryArrayCrc32;
    uint32_t mbrTableCrc32;
    uint32_t mbrDiskSignature;
    uint8_t primaryDiskGuid[16];
    uint8_t backupDiskGuid[16];
    bool confirmationReady;
};

static const uint8_t DELETE_PARTITION_MAX_WRITE_RANGES = 16;

struct DeletePartitionWriteRange {
    uint64_t startLba;
    uint32_t sectorCount;
};

struct DeletePartitionResult {
    DeletePartitionStatus status;
    DeletePartitionStatus failureStatus;
    DeletePartitionStage stage;
    DeletePartitionStage lastStage;
    DeletePartitionStage firstFailedStage;
    TargetIdentity targetIdentity;
    PartitionScheme partitionScheme;
    PartitionEntry deletedPartition;
    uint32_t logicalSectorsRead;
    uint32_t logicalSectorsWritten;
    DeletePartitionWriteRange writeRanges[DELETE_PARTITION_MAX_WRITE_RANGES];
    uint8_t writeRangeCount;
    uint32_t writeStagesCompleted;
    bool writeMayHaveReachedMedia;
    bool failedBeforeWrite;
    bool blockStatusValid;
    block::Status blockStatus;
    block::OperationKind failedOperation;
    block::OperationDiagnostic failedBlockDiagnostic;
    bool flushAttempted;
    uint32_t flushAttempts;
    block::FlushOutcome flushOutcome;
    block::Status flushStatus;
    bool verificationPassed;
    DiskState finalDetectedState;
    uint16_t finalPartitionCount;
    uint16_t finalUnallocatedRegionCount;
    bool rollbackAttempted;
    bool rollbackSucceeded;
    bool rollbackWriteAttempted;
    block::Status rollbackWriteStatus;
    bool rollbackFlushAttempted;
    block::FlushOutcome rollbackFlushOutcome;
    block::Status rollbackFlushStatus;
    bool rollbackVerificationPassed;
    bool finalStateUncertain;
    char diagnostic[128];
};

DeletePartitionStatus probe_delete_partition(
    const TargetIdentity& target, PartitionScheme scheme,
    const PartitionEntry& partition, DeletePartitionResult& result);
DeletePartitionStatus prepare_delete_partition(
    const DeletePartitionRequest& request, DeletePartitionPlan& plan,
    DeletePartitionResult& result);
DeletePartitionStatus execute_delete_partition(
    DeletePartitionPlan& plan, DeletePartitionResult& result);
bool cancel_delete_partition(DeletePartitionPlan& plan);
const char* delete_partition_status_name(DeletePartitionStatus status);
const char* delete_partition_stage_name(DeletePartitionStage stage);

} // namespace storage
} // namespace kernel

#endif
