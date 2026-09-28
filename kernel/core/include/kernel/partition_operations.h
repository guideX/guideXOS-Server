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

} // namespace storage
} // namespace kernel

#endif
