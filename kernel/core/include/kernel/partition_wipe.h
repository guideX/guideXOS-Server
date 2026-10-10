#ifndef KERNEL_PARTITION_WIPE_H
#define KERNEL_PARTITION_WIPE_H

#include "kernel/disk_initialization.h"

namespace kernel {
namespace storage {

// DM34's first operation is deliberately partition-only. The partition map,
// rather than filesystem geometry or a caller-provided whole-device extent,
// is the authority for the write range.
struct PartitionWipeRequest {
    TargetIdentity targetSnapshot;
    PartitionScheme partitionScheme;
    PartitionEntry partitionSnapshot;
    uint8_t gptDiskGuid[16];
    uint32_t mbrDiskSignature;
    uint64_t expectedRegistryGeneration;
};

enum PartitionWipeState : uint8_t {
    PARTITION_WIPE_NOT_ADMITTED = 0,
    PARTITION_WIPE_RUNNING,
    PARTITION_WIPE_STOPPED,
    PARTITION_WIPE_FAILED,
    PARTITION_WIPE_COMPLETED,
};

enum PartitionWipeReason : uint8_t {
    PARTITION_WIPE_REASON_NONE = 0,
    PARTITION_WIPE_REASON_INVALID_REQUEST,
    PARTITION_WIPE_REASON_OPERATION_BUSY,
    PARTITION_WIPE_REASON_OWNERSHIP_INVALID,
    PARTITION_WIPE_REASON_DEVICE_MISSING,
    PARTITION_WIPE_REASON_IDENTITY_CHANGED,
    PARTITION_WIPE_REASON_PARTITION_CHANGED,
    PARTITION_WIPE_REASON_INVALID_GEOMETRY,
    PARTITION_WIPE_REASON_UNSUPPORTED_TRANSPORT,
    PARTITION_WIPE_REASON_READ_UNAVAILABLE,
    PARTITION_WIPE_REASON_WRITE_UNAVAILABLE,
    PARTITION_WIPE_REASON_READ_ONLY,
    PARTITION_WIPE_REASON_DURABILITY_UNKNOWN,
    PARTITION_WIPE_REASON_FLUSH_UNAVAILABLE,
    PARTITION_WIPE_REASON_MOUNTED,
    PARTITION_WIPE_REASON_ROOT_BACKING,
    PARTITION_WIPE_REASON_BOOT_BACKING,
    PARTITION_WIPE_REASON_BOOT_IDENTITY_UNKNOWN,
    PARTITION_WIPE_REASON_PROTECTED_SYSTEM_PARTITION,
    PARTITION_WIPE_REASON_WRITE_FAILED,
    PARTITION_WIPE_REASON_FLUSH_FAILED,
    PARTITION_WIPE_REASON_STOP_REQUESTED,
};

struct PartitionWipeResult {
    PartitionWipeState state;
    PartitionWipeReason reason;
    TargetIdentity targetIdentity;
    PartitionScheme partitionScheme;
    PartitionEntry partition;
    uint64_t operationId;
    uint64_t totalSectors;
    uint64_t totalBytes;
    // Successful bytes form one contiguous prefix of the selected partition.
    uint64_t sectorsWritten;
    uint64_t bytesWritten;
    uint64_t completedRangeStartLba;
    uint64_t completedRangeSectors;
    uint64_t failedLba;
    uint32_t failedSectorCount;
    uint32_t logicalSectorSize;
    uint8_t progressPercent;
    bool stopRequested;
    bool writeAttempted;
    // A failed write callback may have modified media without reporting a
    // complete sector count, so this is intentionally a may-have-changed flag.
    bool mediaMayHaveChanged;
    bool blockStatusValid;
    block::Status blockStatus;
    block::OperationDiagnostic blockDiagnostic;
    bool flushAttempted;
    block::FlushOutcome flushOutcome;
    block::Status flushStatus;
    bool durabilityConfirmed;
    bool ownershipAcquired;
    bool ownershipReleased;
    char diagnostic[128];
};

struct PartitionWipeJob {
    PartitionWipeRequest request;
    PartitionWipeResult result;
    StorageOperationLease lease;
    uint64_t nextRelativeSector;
    uint32_t maxBatchSectors;
    bool stepActive;
    bool ownershipReleaseAttempted;
};

// begin() validates and snapshots the exact device incarnation and parsed
// partition, acquires the shared destructive-operation lease, and pins that
// registration. Each step writes at most one bounded zero-filled batch.
PartitionWipeState begin_partition_wipe_job(
    PartitionWipeJob& job, const PartitionWipeRequest& request);
PartitionWipeState step_partition_wipe_job(PartitionWipeJob& job);
bool request_partition_wipe_stop(PartitionWipeJob& job);
const char* partition_wipe_state_name(PartitionWipeState state);
const char* partition_wipe_reason_name(PartitionWipeReason reason);

} // namespace storage
} // namespace kernel

#endif
