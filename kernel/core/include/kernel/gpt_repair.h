#ifndef KERNEL_GPT_REPAIR_H
#define KERNEL_GPT_REPAIR_H

#include "kernel/disk_initialization.h"

namespace kernel {
namespace storage {

enum GptRepairDirection : uint8_t {
    GPT_REPAIR_NONE = 0,
    GPT_REPAIR_PRIMARY_FROM_BACKUP,
    GPT_REPAIR_BACKUP_FROM_PRIMARY,
};

enum GptRepairStatus : uint8_t {
    GPT_REPAIR_READY_FOR_CONFIRMATION = 0,
    GPT_REPAIR_SUCCESS,
    GPT_REPAIR_OPERATION_BUSY,
    GPT_REPAIR_INVALID_REQUEST,
    GPT_REPAIR_DEVICE_MISSING,
    GPT_REPAIR_REGISTRY_CHANGED,
    GPT_REPAIR_IDENTITY_CHANGED,
    GPT_REPAIR_INVALID_GEOMETRY,
    GPT_REPAIR_READ_UNAVAILABLE,
    GPT_REPAIR_WRITE_UNAVAILABLE,
    GPT_REPAIR_READ_ONLY,
    GPT_REPAIR_DURABILITY_UNKNOWN,
    GPT_REPAIR_FLUSH_UNAVAILABLE,
    GPT_REPAIR_MOUNTED,
    GPT_REPAIR_ROOT_BACKING,
    GPT_REPAIR_MOUNT_STATE_UNKNOWN,
    GPT_REPAIR_BOOT_BACKING,
    GPT_REPAIR_BOOT_IDENTITY_UNKNOWN,
    GPT_REPAIR_GPT_HEALTHY,
    GPT_REPAIR_NO_AUTHORITATIVE_COPY,
    GPT_REPAIR_CONFLICT,
    GPT_REPAIR_GEOMETRY_MISMATCH,
    GPT_REPAIR_UNSUPPORTED_COPY,
    GPT_REPAIR_UNREADABLE_COPY,
    GPT_REPAIR_STALE_SNAPSHOT,
    GPT_REPAIR_OPERATION_OWNERSHIP_INVALID,
    GPT_REPAIR_IO_FAILED,
    GPT_REPAIR_FLUSH_FAILED,
    GPT_REPAIR_VERIFICATION_FAILED,
    GPT_REPAIR_RESCAN_FAILED,
};

enum GptRepairStage : uint8_t {
    GPT_REPAIR_STAGE_IDLE = 0,
    GPT_REPAIR_STAGE_ACQUIRE_LEASE,
    GPT_REPAIR_STAGE_PIN_TARGET,
    GPT_REPAIR_STAGE_PREFLIGHT,
    GPT_REPAIR_STAGE_WAITING_FOR_CONFIRMATION,
    GPT_REPAIR_STAGE_REVALIDATING,
    GPT_REPAIR_STAGE_READING_AUTHORITATIVE_COPY,
    GPT_REPAIR_STAGE_WRITING_ARRAY,
    GPT_REPAIR_STAGE_VERIFYING_ARRAY,
    GPT_REPAIR_STAGE_WRITING_HEADER,
    GPT_REPAIR_STAGE_FLUSH,
    GPT_REPAIR_STAGE_VERIFYING_BOTH_COPIES,
    GPT_REPAIR_STAGE_RESCANNING,
    GPT_REPAIR_STAGE_COMPLETED,
    GPT_REPAIR_STAGE_FAILED,
};

struct GptRepairRequest {
    TargetIdentity targetSnapshot;
    uint64_t expectedRegistryGeneration;
};

struct GptRepairPlan {
    StorageOperationLease lease;
    TargetIdentity targetSnapshot;
    uint64_t expectedRegistryGeneration;
    GptRepairDirection direction;
    GptCopyState damagedCopyState;
    uint64_t authoritativeFingerprint;
    uint64_t totalLogicalSectors;
    uint32_t logicalSectorSize;
    uint64_t authoritativeEntryArrayLba;
    uint64_t damagedEntryArrayLba;
    uint64_t authoritativeHeaderLba;
    uint64_t damagedHeaderLba;
    uint64_t firstUsableLba;
    uint64_t lastUsableLba;
    uint16_t partitionCount;
    uint32_t entryCount;
    uint32_t entrySize;
    uint32_t entryArraySectors;
    uint8_t diskGuid[16];
    bool confirmationReady;
};

struct GptRepairResult {
    GptRepairStatus status;
    GptRepairStatus failureStatus;
    GptRepairStage stage;
    GptRepairStage lastStage;
    GptRepairStage firstFailedStage;
    TargetIdentity targetIdentity;
    GptRepairDirection direction;
    GptCopyState damagedCopyState;
    uint64_t authoritativeFingerprint;
    uint16_t partitionCount;
    uint32_t logicalSectorsRead;
    uint32_t logicalSectorsWritten;
    uint64_t bytesWritten;
    uint32_t flushAttempts;
    block::FlushOutcome flushOutcome;
    block::Status flushStatus;
    bool writeAttempted;
    bool writeMayHaveReachedMedia;
    bool authoritativeCopyPreserved;
    bool partitionIdentitiesPreserved;
    bool protectiveMbrWasValid;
    bool protectiveMbrPreserved;
    bool partitionDataUntouched;
    bool finalVerificationPassed;
    bool finalStateUncertain;
    DiskState finalDetectedState;
    GptCopyState finalPrimaryState;
    GptCopyState finalBackupState;
    char diagnostic[128];
};

// Read-only eligibility and diagnosis. A degraded layout can remain visible
// when writable/persistence/mount/boot policy prevents repair.
GptRepairStatus probe_gpt_repair(const TargetIdentity& target,
                                 GptRepairResult& result);

// Captures a pinned, fingerprinted repair plan for a separate user
// confirmation. The shared destructive-operation lease remains held until
// execute_gpt_repair() or cancel_gpt_repair().
GptRepairStatus prepare_gpt_repair(const GptRepairRequest& request,
                                   GptRepairPlan& plan,
                                   GptRepairResult& result);
GptRepairStatus execute_gpt_repair(GptRepairPlan& plan,
                                   GptRepairResult& result);
bool cancel_gpt_repair(GptRepairPlan& plan);

const char* gpt_repair_status_name(GptRepairStatus status);
const char* gpt_repair_stage_name(GptRepairStage stage);
const char* gpt_copy_state_name(GptCopyState state);

} // namespace storage
} // namespace kernel

#endif
