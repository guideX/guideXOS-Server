#ifndef KERNEL_WHOLE_DISK_ERASE_H
#define KERNEL_WHOLE_DISK_ERASE_H

#include "kernel/disk_initialization.h"

namespace kernel {
namespace storage {

// A caller must name this explicit scope. There is deliberately no arbitrary
// LBA/count parameter: this API always means LBA 0 through device capacity-1.
enum WholeDiskEraseScope : uint8_t {
    WHOLE_DISK_ERASE_SCOPE_UNSPECIFIED = 0,
    WHOLE_DISK_ERASE_ALL_ADDRESSABLE_SECTORS,
};

struct WholeDiskEraseRequest {
    TargetIdentity targetSnapshot;
    uint64_t expectedRegistryGeneration;
    WholeDiskEraseScope scope;
};

enum WholeDiskEraseState : uint8_t {
    WHOLE_DISK_ERASE_NOT_ADMITTED = 0,
    WHOLE_DISK_ERASE_RUNNING,
    WHOLE_DISK_ERASE_STOPPED,
    WHOLE_DISK_ERASE_FAILED,
    WHOLE_DISK_ERASE_COMPLETED,
};

enum WholeDiskEraseReason : uint8_t {
    WHOLE_DISK_ERASE_REASON_NONE = 0,
    WHOLE_DISK_ERASE_REASON_INVALID_REQUEST,
    WHOLE_DISK_ERASE_REASON_OPERATION_BUSY,
    WHOLE_DISK_ERASE_REASON_OWNERSHIP_INVALID,
    WHOLE_DISK_ERASE_REASON_DEVICE_MISSING,
    WHOLE_DISK_ERASE_REASON_IDENTITY_CHANGED,
    WHOLE_DISK_ERASE_REASON_INVALID_GEOMETRY,
    WHOLE_DISK_ERASE_REASON_UNSUPPORTED_TRANSPORT,
    WHOLE_DISK_ERASE_REASON_READ_UNAVAILABLE,
    WHOLE_DISK_ERASE_REASON_WRITE_UNAVAILABLE,
    WHOLE_DISK_ERASE_REASON_READ_ONLY,
    WHOLE_DISK_ERASE_REASON_DURABILITY_UNKNOWN,
    WHOLE_DISK_ERASE_REASON_FLUSH_UNAVAILABLE,
    WHOLE_DISK_ERASE_REASON_MOUNTED_CHILD,
    WHOLE_DISK_ERASE_REASON_ROOT_BACKING,
    WHOLE_DISK_ERASE_REASON_BOOT_BACKING,
    WHOLE_DISK_ERASE_REASON_BOOT_IDENTITY_UNKNOWN,
    WHOLE_DISK_ERASE_REASON_PARTITION_MAP_INVALID,
    WHOLE_DISK_ERASE_REASON_BOOTABLE_MEDIA_PROTECTED,
    WHOLE_DISK_ERASE_REASON_BOOT_MEDIA_AMBIGUOUS,
    WHOLE_DISK_ERASE_REASON_WRITE_FAILED,
    WHOLE_DISK_ERASE_REASON_FLUSH_FAILED,
    WHOLE_DISK_ERASE_REASON_STOP_REQUESTED,
};

enum WholeDiskErasePhase : uint8_t {
    WHOLE_DISK_ERASE_PHASE_ADMISSION = 0,
    WHOLE_DISK_ERASE_PHASE_OVERWRITE,
    WHOLE_DISK_ERASE_PHASE_FLUSH,
    WHOLE_DISK_ERASE_PHASE_TERMINAL,
};

enum WholeDiskMediaClassification : uint8_t {
    WHOLE_DISK_MEDIA_CLASS_UNKNOWN = 0,
    WHOLE_DISK_MEDIA_CLASS_DATA_LAYOUT,
    WHOLE_DISK_MEDIA_CLASS_PROTECTED_BOOTABLE,
    WHOLE_DISK_MEDIA_CLASS_AMBIGUOUS,
};

// Read-only admission snapshot for UI presentation and target-bound
// confirmation. This uses the same production eligibility checks as begin;
// it does not acquire ownership or issue writes.
struct WholeDiskEraseProbe {
    TargetIdentity targetIdentity;
    DeviceCapabilities capabilities;
    WholeDiskEraseReason reason;
    WholeDiskMediaClassification mediaClassification;
    PartitionScheme partitionScheme;
    uint16_t partitionCount;
    uint64_t layoutFingerprint[2];
};

struct WholeDiskEraseResult {
    WholeDiskEraseState state;
    WholeDiskEraseReason reason;
    WholeDiskErasePhase phase;
    WholeDiskMediaClassification mediaClassification;
    TargetIdentity targetIdentity;
    uint64_t operationId;
    uint64_t startLba;
    uint64_t totalSectors;
    uint64_t totalBytes;
    uint64_t sectorsWritten;
    uint64_t bytesWritten;
    uint64_t failedLba;
    uint32_t failedSectorCount;
    uint32_t logicalSectorSize;
    uint8_t progressPercent;
    bool stopRequested;
    bool writeAttempted;
    bool mediaMayHaveChanged;
    bool blockStatusValid;
    block::Status blockStatus;
    block::OperationDiagnostic blockDiagnostic;
    bool flushAttempted;
    block::FlushOutcome flushOutcome;
    block::Status flushStatus;
    bool durabilityConfirmed;
    bool partitionMapMayBeDestroyed;
    bool ownershipAcquired;
    bool ownershipReleased;
    char diagnostic[160];
};

struct WholeDiskEraseJob {
    WholeDiskEraseRequest request;
    WholeDiskEraseResult result;
    StorageOperationLease lease;
    uint64_t nextLba;
    uint32_t maxBatchSectors;
    bool stepActive;
    bool ownershipReleaseAttempted;
};

// Admission validates the complete partition map and system-storage policy,
// then pins the exact registration and full-device geometry. Each step writes
// at most one bounded batch. Once admitted, steps intentionally do not
// revalidate the partition map because LBA 0 and GPT metadata are in scope.
WholeDiskEraseState begin_whole_disk_erase_job(
    WholeDiskEraseJob& job, const WholeDiskEraseRequest& request);
WholeDiskEraseReason probe_whole_disk_erase_target(
    const TargetIdentity& target, WholeDiskEraseProbe& probe);
WholeDiskEraseState step_whole_disk_erase_job(WholeDiskEraseJob& job);
bool request_whole_disk_erase_stop(WholeDiskEraseJob& job);
const char* whole_disk_erase_state_name(WholeDiskEraseState state);
const char* whole_disk_erase_reason_name(WholeDiskEraseReason reason);

} // namespace storage
} // namespace kernel

#endif
