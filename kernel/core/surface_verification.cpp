#include "include/kernel/surface_verification.h"

namespace kernel {
namespace storage {
namespace {

alignas(4096) static uint8_t s_surfaceReadBuffer[
    SURFACE_VERIFICATION_MAX_BATCH_BYTES];

static void copy_text(char* destination, size_t capacity, const char* source)
{
    if (!destination || capacity == 0) return;
    size_t index = 0;
    if (source) {
        while (source[index] && index + 1 < capacity) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

static void saturating_add(uint64_t& value, uint64_t amount)
{
    value = value > UINT64_MAX - amount ? UINT64_MAX : value + amount;
}

static uint8_t ratio_percent(uint64_t numerator, uint64_t denominator)
{
    if (denominator == 0 || numerator == 0) return 0;
    if (numerator >= denominator) return 100;

    // Compute floor(numerator * 100 / denominator) without overflowing the
    // 64-bit arithmetic used by this freestanding kernel.
    const uint64_t quotient = denominator / 100u;
    const uint64_t remainder = denominator % 100u;
    uint8_t percent = 0;
    for (uint16_t candidate = 1; candidate <= 100; ++candidate) {
        const uint64_t threshold = quotient * candidate +
            (remainder * candidate + 99u) / 100u;
        if (numerator < threshold) break;
        percent = static_cast<uint8_t>(candidate);
    }
    return percent;
}

static void update_progress(SurfaceVerificationResult& result)
{
    if (result.readableSectors > result.totalSectors)
        result.readableSectors = result.totalSectors;
    if (result.attemptedSectors > result.totalSectors)
        result.attemptedSectors = result.totalSectors;
    result.bytesRead = result.readableSectors *
        static_cast<uint64_t>(result.logicalSectorSize);
    result.unverifiedSectors = result.totalSectors - result.readableSectors;
    result.unattemptedSectors = result.totalSectors - result.attemptedSectors;
    result.progressPercent = ratio_percent(result.readableSectors,
                                             result.totalSectors);
}

static void set_diagnostic(SurfaceVerificationResult& result,
                           const char* message)
{
    copy_text(result.diagnostic, sizeof(result.diagnostic), message);
}

static SurfaceVerificationReason map_identity_failure(
    const TargetIdentity& target, RevalidationStatus status)
{
    switch (status) {
        case TARGET_VALID:
            return SURFACE_VERIFICATION_REASON_NONE;
        case TARGET_DEVICE_MISSING:
            return SURFACE_VERIFICATION_REASON_DEVICE_REMOVED;
        case TARGET_REGISTRY_CHANGED: {
            // mark_device_offline() leaves an exact pinned tombstone. This
            // distinguishes it from a registration that no longer exists.
            if (block::registration_is_present(target.globalIndex,
                                               target.registrationId)) {
                block::BlockDevice current = {};
                if (!block::copy_device(target.globalIndex, current))
                    return SURFACE_VERIFICATION_REASON_DEVICE_REMOVED;
            }
            return SURFACE_VERIFICATION_REASON_REGISTRATION_INVALIDATED;
        }
        case TARGET_IDENTITY_MISMATCH:
        default:
            return SURFACE_VERIFICATION_REASON_TARGET_IDENTITY_CHANGED;
    }
}

static SurfaceVerificationReason validate_live_target(
    const SurfaceVerificationJob& job, DeviceCapabilities& capabilities)
{
    const TargetIdentity& target = job.result.targetIdentity;
    if (!storage_operation_lease_is_current(job.lease) ||
        !job.lease.targetPinned ||
        job.lease.pinnedIndex != target.globalIndex ||
        job.lease.pinnedRegistrationId != target.registrationId)
        return SURFACE_VERIFICATION_REASON_OWNERSHIP_INVALID;

    const RevalidationStatus identity = revalidate_target_identity(target);
    if (identity != TARGET_VALID)
        return map_identity_failure(target, identity);

    if (!query_device_capabilities(target.globalIndex, capabilities))
        return SURFACE_VERIFICATION_REASON_DEVICE_REMOVED;
    if (!capabilities.geometryValid || !capabilities.capacityValid ||
        capabilities.totalLogicalSectors != target.totalLogicalSectors ||
        capabilities.logicalSectorSize != target.logicalSectorSize ||
        !valid_geometry(capabilities.totalLogicalSectors,
                        capabilities.logicalSectorSize))
        return SURFACE_VERIFICATION_REASON_UNSUPPORTED_GEOMETRY;
    if (!capabilities.readable)
        return SURFACE_VERIFICATION_REASON_READ_UNAVAILABLE;
    if (capabilities.maxTransferBytes != 0 &&
        capabilities.maxTransferBytes < capabilities.logicalSectorSize)
        return SURFACE_VERIFICATION_REASON_UNSUPPORTED_GEOMETRY;
    if (capabilities.requiredBufferAlignment > 4096u ||
        (capabilities.requiredBufferAlignment > 1u &&
         reinterpret_cast<uintptr_t>(s_surfaceReadBuffer) %
             capabilities.requiredBufferAlignment != 0u))
        return SURFACE_VERIFICATION_REASON_UNSUPPORTED_GEOMETRY;

    const RevalidationStatus finalIdentity = revalidate_target_identity(target);
    if (finalIdentity != TARGET_VALID)
        return map_identity_failure(target, finalIdentity);
    return SURFACE_VERIFICATION_REASON_NONE;
}

static uint32_t calculate_batch_sectors(
    const DeviceCapabilities& capabilities)
{
    if (capabilities.transport == block::BDEV_ATA_PIO) return 1u;

    uint32_t transferLimit = SURFACE_VERIFICATION_MAX_BATCH_BYTES;
    if (capabilities.maxTransferBytes != 0 &&
        capabilities.maxTransferBytes < transferLimit)
        transferLimit = capabilities.maxTransferBytes;
    return transferLimit / capabilities.logicalSectorSize;
}

static void capture_block_failure(SurfaceVerificationResult& result,
                                  block::Status status, uint64_t lba,
                                  uint32_t count)
{
    result.blockStatusValid = true;
    result.blockStatus = status;
    result.failureRangeValid = true;
    result.failedLba = lba;
    result.failedSectorCount = count;
    result.blockDiagnostic = {};

    block::OperationDiagnostic diagnostic = {};
    if (block::last_operation_diagnostic(diagnostic) && diagnostic.valid &&
        diagnostic.globalIndex == result.targetIdentity.globalIndex &&
        diagnostic.registrationId == result.targetIdentity.registrationId &&
        diagnostic.operation == block::OPERATION_READ &&
        diagnostic.requestedLba == lba &&
        diagnostic.requestedSectors == count && diagnostic.status == status) {
        result.blockDiagnostic = diagnostic;
    }
}

static bool release_ownership(SurfaceVerificationJob& job)
{
    if (!job.result.ownershipAcquired || job.ownershipReleaseAttempted)
        return job.result.ownershipReleased;

    job.ownershipReleaseAttempted = true;
    job.result.ownershipReleased = release_storage_operation(job.lease);
    return job.result.ownershipReleased;
}

static void finish_job(SurfaceVerificationJob& job,
                       SurfaceVerificationState state,
                       SurfaceVerificationReason reason,
                       const char* message)
{
    job.result.state = state;
    job.result.reason = reason;
    job.stepActive = false;
    set_diagnostic(job.result, message);
    update_progress(job.result);

    if (job.result.ownershipAcquired && !release_ownership(job)) {
        job.result.state = SURFACE_VERIFICATION_FAILED;
        job.result.reason = SURFACE_VERIFICATION_REASON_OWNERSHIP_INVALID;
        set_diagnostic(job.result,
            "The scan ended, but storage-operation ownership could not be released.");
    }
}

static SurfaceVerificationReason classify_read_failure(
    const SurfaceVerificationJob& job, block::Status status)
{
    if (status == block::BLOCK_ERR_NO_MEDIA)
        return SURFACE_VERIFICATION_REASON_DEVICE_REMOVED;
    if (!storage_operation_lease_is_current(job.lease))
        return SURFACE_VERIFICATION_REASON_OWNERSHIP_INVALID;

    const RevalidationStatus identity =
        revalidate_target_identity(job.result.targetIdentity);
    if (identity != TARGET_VALID)
        return map_identity_failure(job.result.targetIdentity, identity);

    if (status == block::BLOCK_ERR_IO || status == block::BLOCK_ERR_TIMEOUT ||
        status == block::BLOCK_ERR_NOT_READY ||
        status == block::BLOCK_ERR_UNSUPPORTED)
        return SURFACE_VERIFICATION_REASON_READ_FAILURE;
    return SURFACE_VERIFICATION_REASON_INFRASTRUCTURE_FAILURE;
}

} // namespace

SurfaceVerificationState begin_surface_verification_job(
    SurfaceVerificationJob& job, const SurfaceVerificationRequest& request)
{
    if (job.result.state == SURFACE_VERIFICATION_RUNNING)
        return job.result.state;

    job = {};
    job.request = request;
    job.result.state = SURFACE_VERIFICATION_NOT_ADMITTED;
    job.result.targetIdentity = request.targetSnapshot;
    job.result.startLba = request.startLba;
    job.result.totalSectors = request.sectorCount;
    job.result.lastSuccessfulLba = UINT64_MAX;
    job.result.failedLba = UINT64_MAX;
    job.result.blockStatus = block::BLOCK_ERR_INVALID;

    if (request.targetSnapshot.registrationId == 0 ||
        request.targetSnapshot.registryGeneration == 0 ||
        request.targetSnapshot.registryGeneration == UINT64_MAX ||
        request.sectorCount == 0 ||
        !checked_lba_range(request.targetSnapshot.totalLogicalSectors,
                           request.startLba, request.sectorCount)) {
        job.result.reason = SURFACE_VERIFICATION_REASON_INVALID_REQUEST;
        set_diagnostic(job.result,
            "The selected device registration or scan range is invalid.");
        return job.result.state;
    }
    if (request.targetSnapshot.registryGeneration !=
        block::registry_generation()) {
        job.result.reason = SURFACE_VERIFICATION_REASON_REGISTRATION_INVALIDATED;
        set_diagnostic(job.result,
            surface_verification_reason_name(job.result.reason));
        return job.result.state;
    }

    uint64_t selectedBytes = 0;
    if (!valid_geometry(request.sectorCount,
                        request.targetSnapshot.logicalSectorSize,
                        &selectedBytes)) {
        job.result.reason = SURFACE_VERIFICATION_REASON_UNSUPPORTED_GEOMETRY;
        set_diagnostic(job.result,
            surface_verification_reason_name(job.result.reason));
        return job.result.state;
    }
    job.result.totalBytes = selectedBytes;

    const StorageOperationLockStatus lock =
        try_acquire_storage_operation(job.lease);
    if (lock != STORAGE_OPERATION_LOCK_ACQUIRED) {
        job.result.reason = lock == STORAGE_OPERATION_LOCK_BUSY
            ? SURFACE_VERIFICATION_REASON_OPERATION_BUSY
            : SURFACE_VERIFICATION_REASON_OWNERSHIP_INVALID;
        set_diagnostic(job.result,
            surface_verification_reason_name(job.result.reason));
        return job.result.state;
    }
    job.result.ownershipAcquired = true;
    job.result.operationId = job.lease.ownerToken;

    if (!pin_storage_operation_target(job.lease, request.targetSnapshot)) {
        const RevalidationStatus identity =
            revalidate_target_identity(job.result.targetIdentity);
        finish_job(job, SURFACE_VERIFICATION_NOT_ADMITTED,
            identity == TARGET_VALID
                ? SURFACE_VERIFICATION_REASON_OWNERSHIP_INVALID
                : map_identity_failure(job.result.targetIdentity, identity),
            "The exact device registration could not be pinned.");
        return job.result.state;
    }

    DeviceCapabilities capabilities = {};
    SurfaceVerificationReason reason =
        validate_live_target(job, capabilities);
    if (reason == SURFACE_VERIFICATION_REASON_NONE) {
        job.maxBatchSectors = calculate_batch_sectors(capabilities);
        if (job.maxBatchSectors == 0)
            reason = SURFACE_VERIFICATION_REASON_UNSUPPORTED_GEOMETRY;
    }
    if (reason == SURFACE_VERIFICATION_REASON_NONE &&
        !valid_geometry(request.targetSnapshot.totalLogicalSectors,
                        capabilities.logicalSectorSize))
        reason = SURFACE_VERIFICATION_REASON_UNSUPPORTED_GEOMETRY;
    if (reason != SURFACE_VERIFICATION_REASON_NONE) {
        finish_job(job, SURFACE_VERIFICATION_NOT_ADMITTED, reason,
                   surface_verification_reason_name(reason));
        return job.result.state;
    }

    job.result.logicalSectorSize = capabilities.logicalSectorSize;
    job.nextLba = request.startLba;
    job.result.state = SURFACE_VERIFICATION_RUNNING;
    job.result.reason = SURFACE_VERIFICATION_REASON_NONE;
    set_diagnostic(job.result,
        "Read-only surface verification admitted; no scan reads have completed.");
    update_progress(job.result);
    return job.result.state;
}

SurfaceVerificationState step_surface_verification_job(
    SurfaceVerificationJob& job)
{
    if (job.result.state != SURFACE_VERIFICATION_RUNNING || job.stepActive)
        return job.result.state;

    job.stepActive = true;
    if (job.result.stopRequested) {
        finish_job(job, SURFACE_VERIFICATION_STOPPED,
            SURFACE_VERIFICATION_REASON_STOP_REQUESTED,
            "Scan stopped; remaining sectors are unverified.");
        return job.result.state;
    }

    DeviceCapabilities capabilities = {};
    const SurfaceVerificationReason liveReason =
        validate_live_target(job, capabilities);
    if (liveReason != SURFACE_VERIFICATION_REASON_NONE) {
        finish_job(job, SURFACE_VERIFICATION_FAILED, liveReason,
                   surface_verification_reason_name(liveReason));
        return job.result.state;
    }

    if (job.nextLba < job.result.startLba ||
        job.nextLba - job.result.startLba >= job.result.totalSectors ||
        job.maxBatchSectors == 0) {
        finish_job(job, SURFACE_VERIFICATION_FAILED,
            SURFACE_VERIFICATION_REASON_INFRASTRUCTURE_FAILURE,
            "The next read range is outside the admitted scan extent.");
        return job.result.state;
    }

    const uint64_t remaining = job.result.totalSectors -
        (job.nextLba - job.result.startLba);
    const uint32_t count = static_cast<uint32_t>(remaining <
        job.maxBatchSectors ? remaining : job.maxBatchSectors);
    if (count == 0 || count >
            SURFACE_VERIFICATION_MAX_BATCH_BYTES /
                capabilities.logicalSectorSize ||
        !checked_lba_range(job.result.targetIdentity.totalLogicalSectors,
                           job.nextLba, count)) {
        finish_job(job, SURFACE_VERIFICATION_FAILED,
            SURFACE_VERIFICATION_REASON_INFRASTRUCTURE_FAILURE,
            "The next bounded read request is outside the target geometry.");
        return job.result.state;
    }

    const uint32_t byteCount = count * capabilities.logicalSectorSize;
    if (byteCount == 0 || byteCount > sizeof(s_surfaceReadBuffer)) {
        finish_job(job, SURFACE_VERIFICATION_FAILED,
            SURFACE_VERIFICATION_REASON_INFRASTRUCTURE_FAILURE,
            "The next read request exceeds the bounded scan buffer.");
        return job.result.state;
    }

    const uint64_t lba = job.nextLba;
    saturating_add(job.result.readRequests, 1u);
    saturating_add(job.result.attemptedSectors, count);
    const block::Status status = read_sectors_safe(
        job.result.targetIdentity.globalIndex, lba, count,
        s_surfaceReadBuffer, byteCount);
    if (status != block::BLOCK_OK) {
        capture_block_failure(job.result, status, lba, count);
        const SurfaceVerificationReason failure =
            classify_read_failure(job, status);
        const char* message = failure == SURFACE_VERIFICATION_REASON_READ_FAILURE
            ? "A read request failed; its requested range is not individually localized."
            : surface_verification_reason_name(failure);
        finish_job(job, SURFACE_VERIFICATION_FAILED, failure, message);
        return job.result.state;
    }

    saturating_add(job.result.readableSectors, count);
    job.result.lastSuccessfulLba = lba + count - 1u;
    job.nextLba += count;
    update_progress(job.result);

    if (!storage_operation_lease_is_current(job.lease)) {
        finish_job(job, SURFACE_VERIFICATION_FAILED,
            SURFACE_VERIFICATION_REASON_OWNERSHIP_INVALID,
            surface_verification_reason_name(
                SURFACE_VERIFICATION_REASON_OWNERSHIP_INVALID));
        return job.result.state;
    }

    const RevalidationStatus identity =
        revalidate_target_identity(job.result.targetIdentity);
    if (identity != TARGET_VALID) {
        const SurfaceVerificationReason reason = map_identity_failure(
            job.result.targetIdentity, identity);
        finish_job(job, SURFACE_VERIFICATION_FAILED, reason,
                   surface_verification_reason_name(reason));
        return job.result.state;
    }

    if (job.result.readableSectors == job.result.totalSectors) {
        job.result.allSelectedSectorsReadable = true;
        job.result.progressPercent = 100u;
        finish_job(job, SURFACE_VERIFICATION_COMPLETED,
            SURFACE_VERIFICATION_REASON_NONE,
            "All selected sectors were readable during this scan.");
        return job.result.state;
    }

    if (job.result.stopRequested) {
        finish_job(job, SURFACE_VERIFICATION_STOPPED,
            SURFACE_VERIFICATION_REASON_STOP_REQUESTED,
            "Scan stopped; remaining sectors are unverified.");
        return job.result.state;
    }

    job.stepActive = false;
    return job.result.state;
}

bool request_surface_verification_stop(SurfaceVerificationJob& job)
{
    if (job.result.state != SURFACE_VERIFICATION_RUNNING) return false;
    job.result.stopRequested = true;
    return true;
}

const char* surface_verification_state_name(SurfaceVerificationState state)
{
    switch (state) {
        case SURFACE_VERIFICATION_NOT_ADMITTED: return "Not admitted";
        case SURFACE_VERIFICATION_RUNNING: return "Running";
        case SURFACE_VERIFICATION_COMPLETED: return "Completed";
        case SURFACE_VERIFICATION_STOPPED: return "Stopped";
        case SURFACE_VERIFICATION_FAILED: return "Failed";
        default: return "Unknown";
    }
}

const char* surface_verification_reason_name(
    SurfaceVerificationReason reason)
{
    switch (reason) {
        case SURFACE_VERIFICATION_REASON_NONE: return "None";
        case SURFACE_VERIFICATION_REASON_INVALID_REQUEST: return "Invalid request";
        case SURFACE_VERIFICATION_REASON_OPERATION_BUSY: return "Another storage operation is active";
        case SURFACE_VERIFICATION_REASON_OWNERSHIP_INVALID: return "Storage operation ownership is invalid";
        case SURFACE_VERIFICATION_REASON_DEVICE_REMOVED: return "Device removed or offline";
        case SURFACE_VERIFICATION_REASON_REGISTRATION_INVALIDATED: return "Device registration was invalidated";
        case SURFACE_VERIFICATION_REASON_TARGET_IDENTITY_CHANGED: return "Target identity changed";
        case SURFACE_VERIFICATION_REASON_UNSUPPORTED_GEOMETRY: return "Unsupported sector geometry or transfer alignment";
        case SURFACE_VERIFICATION_REASON_READ_UNAVAILABLE: return "Device does not support reads";
        case SURFACE_VERIFICATION_REASON_READ_FAILURE: return "A read request failed";
        case SURFACE_VERIFICATION_REASON_INFRASTRUCTURE_FAILURE: return "Scan infrastructure failed";
        case SURFACE_VERIFICATION_REASON_STOP_REQUESTED: return "Stop requested";
        default: return "Unknown";
    }
}

} // namespace storage
} // namespace kernel
