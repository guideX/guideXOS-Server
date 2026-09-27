#ifndef KERNEL_DISK_INITIALIZATION_H
#define KERNEL_DISK_INITIALIZATION_H

#include "kernel/storage_manager.h"

namespace kernel {
namespace storage {

enum StorageOperationLockStatus : uint8_t {
    STORAGE_OPERATION_LOCK_ACQUIRED = 0,
    STORAGE_OPERATION_LOCK_BUSY,
    STORAGE_OPERATION_LOCK_INVALID_OWNER,
};

// A lease carries the explicit owner token for one storage operation. Stale
// copies cannot release a later operation after its token has changed.
struct StorageOperationLease {
    uint64_t ownerToken;
};

StorageOperationLockStatus try_acquire_storage_operation(
    StorageOperationLease& lease);
bool release_storage_operation(StorageOperationLease& lease);
bool begin_storage_operation_execution(const StorageOperationLease& lease);
bool complete_storage_operation_execution(StorageOperationLease& lease);
bool storage_operation_lease_is_current(const StorageOperationLease& lease);
bool storage_operation_active();

enum InitializeDiskStatus : uint8_t {
    INITIALIZE_DISK_READY_FOR_CONFIRMATION = 0,
    INITIALIZE_DISK_SUCCESS,
    INITIALIZE_DISK_OPERATION_BUSY,
    INITIALIZE_DISK_INVALID_REQUEST,
    INITIALIZE_DISK_DEVICE_MISSING,
    INITIALIZE_DISK_REGISTRY_CHANGED,
    INITIALIZE_DISK_IDENTITY_CHANGED,
    INITIALIZE_DISK_INVALID_GEOMETRY,
    INITIALIZE_DISK_READ_UNAVAILABLE,
    INITIALIZE_DISK_WRITE_UNAVAILABLE,
    INITIALIZE_DISK_READ_ONLY,
    INITIALIZE_DISK_DURABILITY_UNKNOWN,
    INITIALIZE_DISK_FLUSH_UNAVAILABLE,
    INITIALIZE_DISK_MOUNTED,
    INITIALIZE_DISK_ROOT_BACKING,
    INITIALIZE_DISK_BOOT_BACKING,
    INITIALIZE_DISK_BOOT_IDENTITY_UNKNOWN,
    INITIALIZE_DISK_NOT_RAW,
    INITIALIZE_DISK_UNSUPPORTED_SECTOR_SIZE,
    INITIALIZE_DISK_TOO_SMALL,
    INITIALIZE_DISK_MBR_CAPACITY_LIMIT,
    INITIALIZE_DISK_METADATA_NOT_CLEAR,
    INITIALIZE_DISK_ENTROPY_UNAVAILABLE,
    INITIALIZE_DISK_OPERATION_OWNERSHIP_INVALID,
    INITIALIZE_DISK_IO_FAILED,
    INITIALIZE_DISK_FLUSH_FAILED,
    INITIALIZE_DISK_VERIFICATION_FAILED,
    INITIALIZE_DISK_RESCAN_FAILED,
    INITIALIZE_DISK_ROLLBACK_FAILED,
};

enum InitializeDiskStage : uint8_t {
    INITIALIZE_STAGE_IDLE = 0,
    INITIALIZE_STAGE_VALIDATING,
    INITIALIZE_STAGE_WAITING_FOR_CONFIRMATION,
    INITIALIZE_STAGE_REVALIDATING,
    INITIALIZE_STAGE_WRITING_GPT_METADATA,
    INITIALIZE_STAGE_WRITING_MBR,
    INITIALIZE_STAGE_FLUSHING,
    INITIALIZE_STAGE_VERIFYING,
    INITIALIZE_STAGE_RESCANNING,
    INITIALIZE_STAGE_COMPLETED,
    INITIALIZE_STAGE_FAILED,
};

enum InitializeWriteStage : uint32_t {
    INITIALIZE_WRITE_BACKUP_ARRAY = 1u << 0,
    INITIALIZE_WRITE_BACKUP_HEADER = 1u << 1,
    INITIALIZE_WRITE_PRIMARY_ARRAY = 1u << 2,
    INITIALIZE_WRITE_PRIMARY_HEADER = 1u << 3,
    INITIALIZE_WRITE_PROTECTIVE_MBR = 1u << 4,
    INITIALIZE_WRITE_MBR = 1u << 5,
};

enum MbrSignaturePolicy : uint8_t {
    MBR_SIGNATURE_RANDOM_NONZERO = 0,
};

enum DiskGuidSource : uint8_t {
    DISK_GUID_SECURE_RANDOM = 0,
};

static const PartitionScheme DEFAULT_INITIALIZE_SCHEME = PARTITION_SCHEME_GPT;
static const uint32_t INITIALIZE_GPT_ENTRY_COUNT = 128;
static const uint32_t INITIALIZE_GPT_ENTRY_SIZE = 128;
static const uint32_t INITIALIZE_GPT_ARRAY_BYTES =
    INITIALIZE_GPT_ENTRY_COUNT * INITIALIZE_GPT_ENTRY_SIZE;

struct InitializeDiskRequest {
    TargetIdentity targetSnapshot;
    PartitionScheme requestedScheme;
    MbrSignaturePolicy mbrSignaturePolicy;
    DiskGuidSource diskGuidSource;
    uint64_t expectedRegistryGeneration;
#if defined(KERNEL_STORAGE_TEST)
    bool testGuidProvided;
    uint8_t testDiskGuid[16];
    bool testMbrSignatureProvided;
    uint32_t testMbrSignature;
#endif
};

struct InitializeTargetValidation {
    InitializeDiskStatus status;
    uint32_t destructiveIssues;
    DiskState detectedState;
    MountSafety mountSafety;
    BootSafety bootSafety;
    uint32_t logicalSectorSize;
    uint64_t totalLogicalSectors;
    uint64_t firstUsableLba;
    uint64_t lastUsableLba;
    uint32_t entryArraySectors;
};

struct InitializeDiskPlan {
    uint64_t ownerToken;
    TargetIdentity targetSnapshot;
    PartitionScheme requestedScheme;
    uint64_t expectedRegistryGeneration;
    uint64_t totalLogicalSectors;
    uint32_t logicalSectorSize;
    uint64_t firstUsableLba;
    uint64_t lastUsableLba;
    uint32_t entryArraySectors;
    uint8_t diskGuid[16];
    uint32_t mbrDiskSignature;
    bool confirmationReady;
};

struct InitializeDiskResult {
    InitializeDiskStatus status;
    InitializeDiskStage stage;
    TargetIdentity targetIdentity;
    PartitionScheme requestedScheme;
    uint32_t writeStagesCompleted;
    bool writeAttempted;
    block::FlushOutcome flushOutcome;
    block::Status flushStatus;
    bool verificationPassed;
    DiskState finalDetectedState;
    bool rollbackAttempted;
    bool rollbackSucceeded;
    bool finalStateUncertain;
    char diagnostic[128];
};

// The caller must own lease. This combines DM2 destructive-target validation
// with initialization-only raw-state, boot, geometry, and capacity rules.
InitializeDiskStatus validate_initialize_target(
    const TargetIdentity& target, PartitionScheme scheme,
    const StorageOperationLease& lease,
    InitializeTargetValidation& validation);

// Short-lived, read-only eligibility check for presentation. It acquires and
// releases the same operation lock; execution always repeats the check.
InitializeDiskStatus probe_initialize_target(
    const TargetIdentity& target, PartitionScheme scheme,
    InitializeTargetValidation& validation);

// Acquires the storage operation lock, validates the target, snapshots every
// metadata sector that would be overwritten, and returns a confirmation-ready
// plan while retaining the lease. On failure it releases the lease.
InitializeDiskStatus prepare_initialize_disk(
    const InitializeDiskRequest& request, InitializeDiskPlan& plan,
    InitializeDiskResult& result);

// Performs post-confirmation revalidation, writes, flush, normal-parser
// read-back verification, rescan, and unconditional lease release.
InitializeDiskStatus execute_initialize_disk(
    InitializeDiskPlan& plan, InitializeDiskResult& result);

// Cancels a confirmation-ready operation and releases its lease.
bool cancel_initialize_disk(InitializeDiskPlan& plan);

const char* initialize_disk_status_name(InitializeDiskStatus status);
const char* initialize_disk_stage_name(InitializeDiskStage stage);

} // namespace storage
} // namespace kernel

#endif
