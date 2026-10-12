#ifndef KERNEL_SURFACE_VERIFICATION_H
#define KERNEL_SURFACE_VERIFICATION_H

#include "kernel/disk_initialization.h"

namespace kernel {
namespace storage {

// Each cooperative step issues at most this many bytes. A device-advertised
// transfer limit may reduce the batch further. ATA PIO is limited to one
// sector per step because its production callback issues one command per
// sector.
static const uint32_t SURFACE_VERIFICATION_MAX_BATCH_BYTES = 128u * 1024u;

struct SurfaceVerificationRequest {
    TargetIdentity targetSnapshot;
    uint64_t startLba;
    uint64_t sectorCount;
};

enum SurfaceVerificationState : uint8_t {
    SURFACE_VERIFICATION_NOT_ADMITTED = 0,
    SURFACE_VERIFICATION_RUNNING,
    SURFACE_VERIFICATION_COMPLETED,
    SURFACE_VERIFICATION_STOPPED,
    SURFACE_VERIFICATION_FAILED,
};

enum SurfaceVerificationReason : uint8_t {
    SURFACE_VERIFICATION_REASON_NONE = 0,
    SURFACE_VERIFICATION_REASON_INVALID_REQUEST,
    SURFACE_VERIFICATION_REASON_OPERATION_BUSY,
    SURFACE_VERIFICATION_REASON_OWNERSHIP_INVALID,
    SURFACE_VERIFICATION_REASON_DEVICE_REMOVED,
    SURFACE_VERIFICATION_REASON_REGISTRATION_INVALIDATED,
    SURFACE_VERIFICATION_REASON_TARGET_IDENTITY_CHANGED,
    SURFACE_VERIFICATION_REASON_UNSUPPORTED_GEOMETRY,
    SURFACE_VERIFICATION_REASON_READ_UNAVAILABLE,
    SURFACE_VERIFICATION_REASON_READ_FAILURE,
    SURFACE_VERIFICATION_REASON_INFRASTRUCTURE_FAILURE,
    SURFACE_VERIFICATION_REASON_STOP_REQUESTED,
};

struct SurfaceVerificationResult {
    SurfaceVerificationState state;
    SurfaceVerificationReason reason;
    TargetIdentity targetIdentity;
    uint64_t operationId;
    uint64_t startLba;
    uint64_t totalSectors;
    uint64_t totalBytes;
    // Readable sectors are complete successful requests in the contiguous
    // prefix beginning at startLba. Failed request ranges are not counted as
    // readable or individually localized.
    uint64_t readableSectors;
    uint64_t bytesRead;
    uint64_t attemptedSectors;
    uint64_t unverifiedSectors;
    uint64_t unattemptedSectors;
    uint64_t lastSuccessfulLba;
    uint64_t readRequests;
    uint64_t failedLba;
    uint32_t failedSectorCount;
    uint32_t logicalSectorSize;
    uint8_t progressPercent;
    bool stopRequested;
    bool failureRangeValid;
    bool allSelectedSectorsReadable;
    bool blockStatusValid;
    block::Status blockStatus;
    block::OperationDiagnostic blockDiagnostic;
    bool ownershipAcquired;
    bool ownershipReleased;
    char diagnostic[160];
};

struct SurfaceVerificationJob {
    SurfaceVerificationRequest request;
    SurfaceVerificationResult result;
    StorageOperationLease lease;
    uint64_t nextLba;
    uint32_t maxBatchSectors;
    bool stepActive;
    bool ownershipReleaseAttempted;
};

// begin() snapshots the exact target and caller-provided bounded extent,
// validates read capability/geometry, then acquires the shared storage
// operation lease and pins that registration. Each step performs at most one
// bounded read request. A read failure stops the scan at its first failed
// request; it is not retried or reported as a set of bad sectors.
SurfaceVerificationState begin_surface_verification_job(
    SurfaceVerificationJob& job, const SurfaceVerificationRequest& request);
SurfaceVerificationState step_surface_verification_job(
    SurfaceVerificationJob& job);
bool request_surface_verification_stop(SurfaceVerificationJob& job);
const char* surface_verification_state_name(SurfaceVerificationState state);
const char* surface_verification_reason_name(
    SurfaceVerificationReason reason);

} // namespace storage
} // namespace kernel

#endif
