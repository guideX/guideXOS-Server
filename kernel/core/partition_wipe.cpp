#include "kernel/partition_wipe.h"

namespace kernel {
namespace storage {
namespace {

static const uint32_t WIPE_BATCH_BUFFER_BYTES = 1024u * 1024u;
alignas(4096) static uint8_t s_zeroBatch[WIPE_BATCH_BUFFER_BYTES];
alignas(4096) static PartitionTableModel s_partitionTable;

static void copy_text(char* destination, size_t capacity, const char* source)
{
    if (!destination || capacity == 0) return;
    size_t i = 0;
    if (source) {
        while (i + 1 < capacity && source[i]) {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
}

static bool bytes_equal(const uint8_t* left, const uint8_t* right,
                        size_t count)
{
    if (!left || !right) return false;
    for (size_t i = 0; i < count; ++i)
        if (left[i] != right[i]) return false;
    return true;
}

static bool bytes_zero(const uint8_t* bytes, size_t count)
{
    if (!bytes) return count == 0;
    for (size_t i = 0; i < count; ++i)
        if (bytes[i] != 0) return false;
    return true;
}

static bool partition_entries_equal(const PartitionEntry& left,
                                    const PartitionEntry& right)
{
    return left.partitionNumber == right.partitionNumber &&
        left.mbrType == right.mbrType && left.isGpt == right.isGpt &&
        left.bootable == right.bootable && left.startLba == right.startLba &&
        left.endLba == right.endLba && left.sectorCount == right.sectorCount &&
        left.attributes == right.attributes &&
        bytes_equal(left.typeGuid, right.typeGuid, sizeof(left.typeGuid)) &&
        bytes_equal(left.uniqueGuid, right.uniqueGuid, sizeof(left.uniqueGuid));
}

static bool valid_partition_geometry(const PartitionEntry& partition,
                                     uint64_t deviceSectors)
{
    if (partition.partitionNumber == 0 || partition.sectorCount == 0 ||
        partition.startLba > partition.endLba ||
        partition.endLba >= deviceSectors ||
        partition.endLba - partition.startLba == UINT64_MAX ||
        partition.endLba - partition.startLba + 1u != partition.sectorCount ||
        !checked_lba_range(deviceSectors, partition.startLba,
                           partition.sectorCount)) return false;
    // A whole-device request is not a partition-wipe request, even if a
    // malformed caller tries to represent it as one.
    return partition.startLba != 0 ||
        partition.sectorCount != deviceSectors;
}

static bool protected_system_partition(const PartitionEntry& partition)
{
    if (partition.bootable) return true;
    if (!partition.isGpt) return false;
    // EFI System, Microsoft Reserved, Windows Recovery, and BIOS Boot GPT
    // types. GUID bytes use the on-disk EFI byte order used by the parser.
    static const uint8_t efiSystem[16] = {
        0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
        0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
    static const uint8_t microsoftReserved[16] = {
        0x16, 0xE3, 0xC9, 0xE3, 0x5C, 0x0B, 0xB8, 0x4D,
        0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE};
    static const uint8_t windowsRecovery[16] = {
        0xA4, 0xBB, 0x94, 0xDE, 0xD1, 0x06, 0x40, 0x4D,
        0xA1, 0x6A, 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC};
    static const uint8_t biosBoot[16] = {
        0x48, 0x61, 0x68, 0x21, 0x49, 0x64, 0x6F, 0x6E,
        0x74, 0x4E, 0x65, 0x65, 0x64, 0x45, 0x46, 0x49};
    return bytes_equal(partition.typeGuid, efiSystem, 16) ||
        bytes_equal(partition.typeGuid, microsoftReserved, 16) ||
        bytes_equal(partition.typeGuid, windowsRecovery, 16) ||
        bytes_equal(partition.typeGuid, biosBoot, 16);
}

static PartitionWipeReason validate_partition_map(
    const PartitionWipeRequest& request, const DeviceCapabilities& caps,
    PartitionEntry* currentPartition = nullptr)
{
    if (request.partitionScheme != PARTITION_SCHEME_MBR &&
        request.partitionScheme != PARTITION_SCHEME_GPT)
        return PARTITION_WIPE_REASON_INVALID_REQUEST;
    if (request.partitionSnapshot.isGpt !=
        (request.partitionScheme == PARTITION_SCHEME_GPT) ||
        !valid_partition_geometry(request.partitionSnapshot,
                                  caps.totalLogicalSectors))
        return PARTITION_WIPE_REASON_INVALID_GEOMETRY;
    if (!parse_partition_table(request.targetSnapshot.globalIndex,
                               s_partitionTable) ||
        s_partitionTable.scheme != request.partitionScheme ||
        s_partitionTable.state != (request.partitionScheme == PARTITION_SCHEME_GPT
            ? DISK_STATE_VALID_GPT : DISK_STATE_VALID_MBR) ||
        s_partitionTable.hybridMbr || s_partitionTable.extendedPartitionsPresent ||
        (request.partitionScheme == PARTITION_SCHEME_GPT &&
         (!s_partitionTable.primaryGptValid ||
          !s_partitionTable.backupGptValid || !s_partitionTable.gptCopiesAgree ||
          s_partitionTable.gptCopiesConflict)))
        return PARTITION_WIPE_REASON_PARTITION_CHANGED;

    if (request.partitionScheme == PARTITION_SCHEME_GPT) {
        if (bytes_zero(request.gptDiskGuid, sizeof(request.gptDiskGuid)) ||
            !bytes_equal(request.gptDiskGuid, s_partitionTable.primaryDiskGuid,
                         sizeof(request.gptDiskGuid)) ||
            !bytes_equal(request.gptDiskGuid, s_partitionTable.backupDiskGuid,
                         sizeof(request.gptDiskGuid)))
            return PARTITION_WIPE_REASON_PARTITION_CHANGED;
    } else if (request.mbrDiskSignature != s_partitionTable.mbrDiskSignature) {
        return PARTITION_WIPE_REASON_PARTITION_CHANGED;
    }

    const PartitionEntry* found = nullptr;
    for (uint16_t i = 0; i < s_partitionTable.partitionCount; ++i) {
        const PartitionEntry& candidate = s_partitionTable.partitions[i];
        if (candidate.partitionNumber !=
            request.partitionSnapshot.partitionNumber) continue;
        if (found) return PARTITION_WIPE_REASON_PARTITION_CHANGED;
        found = &candidate;
    }
    if (!found || !partition_entries_equal(*found,
                                           request.partitionSnapshot))
        return PARTITION_WIPE_REASON_PARTITION_CHANGED;
    if (!valid_partition_geometry(*found, caps.totalLogicalSectors))
        return PARTITION_WIPE_REASON_INVALID_GEOMETRY;
    if (protected_system_partition(*found))
        return PARTITION_WIPE_REASON_PROTECTED_SYSTEM_PARTITION;
    if (currentPartition) *currentPartition = *found;
    return PARTITION_WIPE_REASON_NONE;
}

static PartitionWipeReason validate_target(
    const PartitionWipeRequest& request, const StorageOperationLease& lease,
    DeviceCapabilities& caps)
{
    if (!storage_operation_lease_is_current(lease))
        return PARTITION_WIPE_REASON_OWNERSHIP_INVALID;
    const RevalidationStatus identity =
        revalidate_target_identity(request.targetSnapshot);
    if (identity == TARGET_DEVICE_MISSING)
        return PARTITION_WIPE_REASON_DEVICE_MISSING;
    if (identity != TARGET_VALID)
        return PARTITION_WIPE_REASON_IDENTITY_CHANGED;
    if (!lease.targetPinned ||
        lease.pinnedIndex != request.targetSnapshot.globalIndex ||
        lease.pinnedRegistrationId != request.targetSnapshot.registrationId)
        return PARTITION_WIPE_REASON_OWNERSHIP_INVALID;
    if (!query_device_capabilities(request.targetSnapshot.globalIndex, caps))
        return PARTITION_WIPE_REASON_DEVICE_MISSING;
    if (!caps.geometryValid || !caps.capacityValid)
        return PARTITION_WIPE_REASON_INVALID_GEOMETRY;
    if (!caps.readable) return PARTITION_WIPE_REASON_READ_UNAVAILABLE;
    if (caps.transport != block::BDEV_ATA_PIO &&
        caps.transport != block::BDEV_AHCI &&
        caps.transport != block::BDEV_USB_MASS)
        return PARTITION_WIPE_REASON_UNSUPPORTED_TRANSPORT;
    if (!caps.writable) return PARTITION_WIPE_REASON_READ_ONLY;
    if (caps.persistence != PERSISTENCE_SYNCHRONOUS_DURABLE &&
        !(caps.persistence == PERSISTENCE_FLUSH_REQUIRED &&
          caps.flushSupported && caps.flushSemanticsKnown))
        return PARTITION_WIPE_REASON_DURABILITY_UNKNOWN;
    if (caps.persistence == PERSISTENCE_FLUSH_REQUIRED && !caps.flushSupported)
        return PARTITION_WIPE_REASON_FLUSH_UNAVAILABLE;
    if (caps.requiredBufferAlignment > 4096u ||
        (caps.requiredBufferAlignment > 1u &&
         (reinterpret_cast<uintptr_t>(s_zeroBatch) %
              caps.requiredBufferAlignment) != 0))
        return PARTITION_WIPE_REASON_INVALID_GEOMETRY;

    block::BlockDevice device = {};
    if (!block::copy_device(request.targetSnapshot.globalIndex, device) ||
        device.registrationId != request.targetSnapshot.registrationId)
        return PARTITION_WIPE_REASON_DEVICE_MISSING;
    // NVMe remains explicitly excluded even in a proof build that supplies
    // write and Flush callbacks. The shared production gates stay at zero.
    if (device.type == block::BDEV_NVME)
        return PARTITION_WIPE_REASON_UNSUPPORTED_TRANSPORT;

    const RevalidationStatus afterCaps =
        revalidate_target_identity(request.targetSnapshot);
    if (afterCaps == TARGET_DEVICE_MISSING)
        return PARTITION_WIPE_REASON_DEVICE_MISSING;
    if (afterCaps != TARGET_VALID)
        return PARTITION_WIPE_REASON_IDENTITY_CHANGED;

    return PARTITION_WIPE_REASON_NONE;
}

static void update_progress(PartitionWipeResult& result)
{
    result.completedRangeStartLba = result.partition.startLba;
    result.completedRangeSectors = result.sectorsWritten;
    if (result.totalSectors == 0) {
        result.bytesWritten = 0;
        result.progressPercent = 0;
        return;
    }
    result.bytesWritten = result.sectorsWritten *
        static_cast<uint64_t>(result.logicalSectorSize);
    if (result.sectorsWritten >= result.totalSectors) {
        // A final write is not completion until the required durability check
        // has succeeded.
        result.progressPercent = result.state == PARTITION_WIPE_COMPLETED
            ? 100u : 99u;
        return;
    }
    const uint64_t hundredth = result.totalSectors / 100u;
    const uint64_t remainder = result.totalSectors % 100u;
    uint8_t percent = 0;
    for (uint8_t candidate = 1; candidate < 100u; ++candidate) {
        const uint64_t threshold = hundredth * candidate +
            (remainder * candidate + 99u) / 100u;
        if (result.sectorsWritten < threshold) break;
        percent = candidate;
    }
    result.progressPercent = percent;
}

static void capture_block_failure(PartitionWipeResult& result,
                                  block::Status status, uint64_t lba,
                                  uint32_t count)
{
    result.blockStatusValid = true;
    result.blockStatus = status;
    result.failedLba = lba;
    result.failedSectorCount = count;
    block::OperationDiagnostic diagnostic = {};
    if (!block::last_operation_diagnostic(diagnostic) || !diagnostic.valid ||
        diagnostic.operation != block::OPERATION_WRITE ||
        diagnostic.globalIndex != result.targetIdentity.globalIndex ||
        diagnostic.registrationId != result.targetIdentity.registrationId ||
        diagnostic.requestedLba != lba || diagnostic.requestedSectors != count ||
        diagnostic.status != status) return;
    result.blockDiagnostic = diagnostic;
    const uint32_t completed = diagnostic.transportDiagnostic.completedSectors;
    if (completed > 0 && completed <= count) {
        result.sectorsWritten += completed;
        result.failedLba = lba + completed;
        result.failedSectorCount = count - completed;
    }
    update_progress(result);
}

static bool release_ownership(PartitionWipeJob& job)
{
    if (job.ownershipReleaseAttempted) return job.result.ownershipReleased;
    if (job.lease.ownerToken == 0) return false;
    job.ownershipReleaseAttempted = true;
    const bool released = release_storage_operation(job.lease);
    job.result.ownershipReleased = released;
    return released;
}

static void set_terminal(PartitionWipeJob& job, PartitionWipeState state,
                         PartitionWipeReason reason, const char* message)
{
    job.result.reason = reason;
    job.result.state = state;
    job.result.stopRequested = job.result.stopRequested ||
        job.result.state == PARTITION_WIPE_STOPPED;
    copy_text(job.result.diagnostic, sizeof(job.result.diagnostic), message);
    update_progress(job.result);
    if (job.lease.ownerToken != 0 && !release_ownership(job)) {
        job.result.state = PARTITION_WIPE_FAILED;
        job.result.reason = PARTITION_WIPE_REASON_OWNERSHIP_INVALID;
        copy_text(job.result.diagnostic, sizeof(job.result.diagnostic),
            "Wipe ended, but storage-operation ownership release failed.");
        update_progress(job.result);
    }
}

static bool finish_flush(PartitionWipeJob& job, PartitionWipeReason& reason)
{
    DeviceCapabilities caps = {};
    reason = validate_target(job.request, job.lease, caps);
    if (reason != PARTITION_WIPE_REASON_NONE) return false;
    job.result.flushAttempted = true;
    const block::FlushReport report = block::flush_with_result(
        job.request.targetSnapshot.globalIndex);
    job.result.flushOutcome = report.outcome;
    job.result.flushStatus = report.status;
    job.result.durabilityConfirmed = report.status == block::BLOCK_OK &&
        report.semanticsKnown &&
        (report.outcome == block::FLUSH_OUTCOME_SUPPORTED_SUCCEEDED ||
         report.outcome == block::FLUSH_OUTCOME_SYNCHRONOUS_DURABLE);
    if (!job.result.durabilityConfirmed) {
        reason = report.status == block::BLOCK_ERR_UNSUPPORTED ||
                 !report.semanticsKnown
            ? PARTITION_WIPE_REASON_DURABILITY_UNKNOWN
            : PARTITION_WIPE_REASON_FLUSH_FAILED;
        return false;
    }
    reason = PARTITION_WIPE_REASON_NONE;
    return true;
}

static void settle_stopped(PartitionWipeJob& job)
{
    job.result.stopRequested = true;
    if (job.result.writeAttempted) {
        PartitionWipeReason reason = PARTITION_WIPE_REASON_NONE;
        if (!finish_flush(job, reason)) {
            set_terminal(job, PARTITION_WIPE_FAILED, reason,
                "Wipe stopped after writes, but written data could not be confirmed durable.");
            return;
        }
    }
    set_terminal(job, PARTITION_WIPE_STOPPED,
        PARTITION_WIPE_REASON_STOP_REQUESTED,
        job.result.mediaMayHaveChanged
            ? "Wipe stopped after partial overwrite; existing data may have been lost."
            : "Wipe stopped before any overwrite was attempted.");
}

static void settle_failed_write(PartitionWipeJob& job,
                                block::Status status, uint64_t lba,
                                uint32_t count)
{
    capture_block_failure(job.result, status, lba, count);
    PartitionWipeReason reason = PARTITION_WIPE_REASON_WRITE_FAILED;
    DeviceCapabilities caps = {};
    if (status != block::BLOCK_ERR_NO_MEDIA &&
        validate_target(job.request, job.lease, caps) ==
            PARTITION_WIPE_REASON_NONE) {
        PartitionWipeReason flushReason = PARTITION_WIPE_REASON_NONE;
        if (!finish_flush(job, flushReason) &&
            flushReason != PARTITION_WIPE_REASON_NONE) {
            // Keep the original write failure as the primary reason. The
            // separate flush fields preserve the durability failure detail.
            reason = PARTITION_WIPE_REASON_WRITE_FAILED;
        }
    }
    set_terminal(job, PARTITION_WIPE_FAILED, reason,
        job.result.mediaMayHaveChanged
            ? "Wipe failed after writes began; the partition may be partially overwritten."
            : "Wipe failed before any confirmed overwrite.");
}

} // namespace

PartitionWipeState begin_partition_wipe_job(
    PartitionWipeJob& job, const PartitionWipeRequest& request)
{
    if (job.result.state == PARTITION_WIPE_RUNNING) return job.result.state;
    job = {};
    job.request = request;
    job.result.state = PARTITION_WIPE_NOT_ADMITTED;
    job.result.targetIdentity = request.targetSnapshot;
    job.result.partitionScheme = request.partitionScheme;
    job.result.partition = request.partitionSnapshot;
    job.result.failedLba = UINT64_MAX;
    job.result.flushStatus = block::BLOCK_ERR_INVALID;
    if (request.targetSnapshot.registrationId == 0 ||
        request.expectedRegistryGeneration == 0 ||
        request.expectedRegistryGeneration !=
            request.targetSnapshot.registryGeneration ||
        request.targetSnapshot.registryGeneration !=
            block::registry_generation() ||
        request.partitionScheme == PARTITION_SCHEME_NONE_RAW ||
        (request.partitionScheme != PARTITION_SCHEME_MBR &&
         request.partitionScheme != PARTITION_SCHEME_GPT)) {
        job.result.reason = PARTITION_WIPE_REASON_INVALID_REQUEST;
        copy_text(job.result.diagnostic, sizeof(job.result.diagnostic),
                  "The selected partition request is invalid or stale.");
        return job.result.state;
    }

    const StorageOperationLockStatus lock =
        try_acquire_storage_operation(job.lease);
    if (lock != STORAGE_OPERATION_LOCK_ACQUIRED) {
        job.result.reason = lock == STORAGE_OPERATION_LOCK_BUSY
            ? PARTITION_WIPE_REASON_OPERATION_BUSY
            : PARTITION_WIPE_REASON_OWNERSHIP_INVALID;
        copy_text(job.result.diagnostic, sizeof(job.result.diagnostic),
                  partition_wipe_reason_name(job.result.reason));
        return job.result.state;
    }
    job.result.ownershipAcquired = true;
    job.result.operationId = job.lease.ownerToken;
    if (!pin_storage_operation_target(job.lease, request.targetSnapshot)) {
        const RevalidationStatus identity =
            revalidate_target_identity(request.targetSnapshot);
        set_terminal(job, PARTITION_WIPE_NOT_ADMITTED,
            identity == TARGET_DEVICE_MISSING
                ? PARTITION_WIPE_REASON_DEVICE_MISSING
                : PARTITION_WIPE_REASON_IDENTITY_CHANGED,
            "The exact target registration could not be pinned.");
        return job.result.state;
    }

    DeviceCapabilities caps = {};
    PartitionWipeReason reason = validate_target(request, job.lease, caps);
    if (reason == PARTITION_WIPE_REASON_NONE &&
        !request.targetSnapshot.registrationId)
        reason = PARTITION_WIPE_REASON_INVALID_REQUEST;
    if (reason == PARTITION_WIPE_REASON_NONE &&
        (request.expectedRegistryGeneration !=
             request.targetSnapshot.registryGeneration ||
         !valid_partition_geometry(request.partitionSnapshot,
                                   caps.totalLogicalSectors)))
        reason = PARTITION_WIPE_REASON_INVALID_GEOMETRY;
    if (reason == PARTITION_WIPE_REASON_NONE)
        reason = validate_partition_map(request, caps);
    if (reason == PARTITION_WIPE_REASON_NONE) {
        const MountProtection mount = query_partition_mount_protection(
            request.targetSnapshot, request.partitionScheme,
            request.partitionSnapshot);
        if (mount.safety == DEVICE_ROOT_BACKING)
            reason = PARTITION_WIPE_REASON_ROOT_BACKING;
        else if (mount.safety == DEVICE_MOUNTED)
            reason = PARTITION_WIPE_REASON_MOUNTED;
        else if (mount.safety != DEVICE_UNMOUNTED ||
                 !mount.partitionIdentityKnown)
            reason = PARTITION_WIPE_REASON_PARTITION_CHANGED;
    }
    if (reason == PARTITION_WIPE_REASON_NONE) {
        const BootProtection diskBoot = query_boot_protection(
            request.targetSnapshot);
        if (diskBoot.safety == BOOT_DEVICE_IS_TARGET)
            reason = PARTITION_WIPE_REASON_BOOT_BACKING;
        else if (diskBoot.safety != BOOT_DEVICE_DEFINITELY_NOT_TARGET)
            reason = PARTITION_WIPE_REASON_BOOT_IDENTITY_UNKNOWN;
    }
    if (reason == PARTITION_WIPE_REASON_NONE) {
        const BootProtection boot = query_partition_boot_protection(
            request.targetSnapshot, request.partitionScheme,
            request.partitionSnapshot, s_partitionTable);
        if (boot.safety == BOOT_DEVICE_IS_TARGET)
            reason = PARTITION_WIPE_REASON_BOOT_BACKING;
        else if (boot.safety != BOOT_DEVICE_DEFINITELY_NOT_TARGET)
            reason = PARTITION_WIPE_REASON_BOOT_IDENTITY_UNKNOWN;
    }
    if (reason != PARTITION_WIPE_REASON_NONE) {
        job.result.reason = reason;
        copy_text(job.result.diagnostic, sizeof(job.result.diagnostic),
                  partition_wipe_reason_name(reason));
        release_ownership(job);
        return job.result.state;
    }

    uint64_t totalBytes = 0;
    if (!valid_geometry(request.partitionSnapshot.sectorCount,
                        caps.logicalSectorSize, &totalBytes)) {
        job.result.reason = PARTITION_WIPE_REASON_INVALID_GEOMETRY;
        copy_text(job.result.diagnostic, sizeof(job.result.diagnostic),
                  partition_wipe_reason_name(job.result.reason));
        release_ownership(job);
        return job.result.state;
    }
    const uint32_t transferLimit = caps.maxTransferBytes == 0 ||
        caps.maxTransferBytes > WIPE_BATCH_BUFFER_BYTES
        ? WIPE_BATCH_BUFFER_BYTES : caps.maxTransferBytes;
    const uint32_t maxSectors = transferLimit / caps.logicalSectorSize;
    if (maxSectors == 0) {
        job.result.reason = PARTITION_WIPE_REASON_INVALID_GEOMETRY;
        copy_text(job.result.diagnostic, sizeof(job.result.diagnostic),
                  partition_wipe_reason_name(job.result.reason));
        release_ownership(job);
        return job.result.state;
    }

    for (size_t i = 0; i < sizeof(s_zeroBatch); ++i) s_zeroBatch[i] = 0;
    job.maxBatchSectors = maxSectors;
    job.result.logicalSectorSize = caps.logicalSectorSize;
    job.result.totalSectors = request.partitionSnapshot.sectorCount;
    job.result.totalBytes = totalBytes;
    job.result.state = PARTITION_WIPE_RUNNING;
    job.result.reason = PARTITION_WIPE_REASON_NONE;
    copy_text(job.result.diagnostic, sizeof(job.result.diagnostic),
              "Partition wipe admitted; no overwrite batch has completed yet.");
    return job.result.state;
}

PartitionWipeState step_partition_wipe_job(PartitionWipeJob& job)
{
    if (job.result.state != PARTITION_WIPE_RUNNING || job.stepActive)
        return job.result.state;
    job.stepActive = true;
    if (job.result.stopRequested) {
        job.stepActive = false;
        settle_stopped(job);
        return job.result.state;
    }

    DeviceCapabilities caps = {};
    PartitionWipeReason reason = validate_target(job.request, job.lease, caps);
    if (reason == PARTITION_WIPE_REASON_NONE)
        reason = validate_partition_map(job.request, caps);
    if (reason != PARTITION_WIPE_REASON_NONE) {
        job.stepActive = false;
        set_terminal(job, PARTITION_WIPE_FAILED, reason,
            job.result.mediaMayHaveChanged
                ? "Wipe stopped after target or partition invalidation; prior writes may have changed the media."
                : "Wipe stopped because the exact target or partition is no longer valid.");
        return job.result.state;
    }

    if (job.nextRelativeSector >= job.result.totalSectors) {
        job.stepActive = false;
        reason = PARTITION_WIPE_REASON_NONE;
        if (!finish_flush(job, reason)) {
            set_terminal(job, PARTITION_WIPE_FAILED, reason,
                "All partition sectors were written, but durability was not confirmed.");
        } else {
            set_terminal(job, PARTITION_WIPE_COMPLETED,
                PARTITION_WIPE_REASON_NONE,
                "Partition zero-overwrite completed and required durability checks passed.");
        }
        return job.result.state;
    }

    const uint64_t remaining = job.result.totalSectors -
        job.nextRelativeSector;
    const uint32_t count = static_cast<uint32_t>(remaining <
        job.maxBatchSectors ? remaining : job.maxBatchSectors);
    const uint32_t sectorSize = caps.logicalSectorSize;
    if (count == 0 || count > UINT32_MAX / sectorSize) {
        job.stepActive = false;
        set_terminal(job, PARTITION_WIPE_FAILED,
            PARTITION_WIPE_REASON_INVALID_GEOMETRY,
            "The next bounded overwrite range is invalid.");
        return job.result.state;
    }
    const uint32_t bytes = count * sectorSize;
    uint64_t lba = 0;
    if (job.request.partitionSnapshot.startLba >
            UINT64_MAX - job.nextRelativeSector) {
        job.stepActive = false;
        set_terminal(job, PARTITION_WIPE_FAILED,
            PARTITION_WIPE_REASON_INVALID_GEOMETRY,
            "The next overwrite LBA would overflow.");
        return job.result.state;
    }
    lba = job.request.partitionSnapshot.startLba + job.nextRelativeSector;
    if (!checked_lba_range(caps.totalLogicalSectors, lba, count) ||
        lba < job.request.partitionSnapshot.startLba ||
        count > job.result.totalSectors - job.nextRelativeSector) {
        job.stepActive = false;
        set_terminal(job, PARTITION_WIPE_FAILED,
            PARTITION_WIPE_REASON_INVALID_GEOMETRY,
            "The next overwrite range exceeds the selected partition.");
        return job.result.state;
    }

    job.result.writeAttempted = true;
    job.result.mediaMayHaveChanged = true;
    const block::Status status = write_sectors_safe(
        job.request.targetSnapshot.globalIndex, lba, count, s_zeroBatch, bytes);
    if (status != block::BLOCK_OK) {
        job.stepActive = false;
        settle_failed_write(job, status, lba, count);
        return job.result.state;
    }
    job.result.sectorsWritten += count;
    job.nextRelativeSector += count;
    update_progress(job.result);
    job.stepActive = false;

    if (job.nextRelativeSector == job.result.totalSectors) {
        reason = PARTITION_WIPE_REASON_NONE;
        if (!finish_flush(job, reason)) {
            set_terminal(job, PARTITION_WIPE_FAILED, reason,
                "All partition sectors were written, but durability was not confirmed.");
        } else {
            set_terminal(job, PARTITION_WIPE_COMPLETED,
                PARTITION_WIPE_REASON_NONE,
                "Partition zero-overwrite completed and required durability checks passed.");
        }
        return job.result.state;
    }
    if (job.result.stopRequested) settle_stopped(job);
    return job.result.state;
}

bool request_partition_wipe_stop(PartitionWipeJob& job)
{
    if (job.result.state != PARTITION_WIPE_RUNNING) return false;
    job.result.stopRequested = true;
    return true;
}

const char* partition_wipe_state_name(PartitionWipeState state)
{
    switch (state) {
        case PARTITION_WIPE_NOT_ADMITTED: return "Not admitted";
        case PARTITION_WIPE_RUNNING: return "Running";
        case PARTITION_WIPE_STOPPED: return "Stopped";
        case PARTITION_WIPE_FAILED: return "Failed";
        case PARTITION_WIPE_COMPLETED: return "Completed";
        default: return "Unknown";
    }
}

const char* partition_wipe_reason_name(PartitionWipeReason reason)
{
    switch (reason) {
        case PARTITION_WIPE_REASON_NONE: return "No error";
        case PARTITION_WIPE_REASON_INVALID_REQUEST: return "Invalid or stale partition request";
        case PARTITION_WIPE_REASON_OPERATION_BUSY: return "Another storage operation is active";
        case PARTITION_WIPE_REASON_OWNERSHIP_INVALID: return "Storage-operation ownership is invalid";
        case PARTITION_WIPE_REASON_DEVICE_MISSING: return "The selected device is unavailable";
        case PARTITION_WIPE_REASON_IDENTITY_CHANGED: return "The selected device identity changed";
        case PARTITION_WIPE_REASON_PARTITION_CHANGED: return "The selected partition map changed or is not authoritative";
        case PARTITION_WIPE_REASON_INVALID_GEOMETRY: return "The selected partition geometry is invalid";
        case PARTITION_WIPE_REASON_UNSUPPORTED_TRANSPORT: return "This storage transport is not qualified for wiping";
        case PARTITION_WIPE_REASON_READ_UNAVAILABLE: return "The selected device does not support reads required for partition validation";
        case PARTITION_WIPE_REASON_WRITE_UNAVAILABLE: return "The selected device does not support writes";
        case PARTITION_WIPE_REASON_READ_ONLY: return "The selected device is read-only";
        case PARTITION_WIPE_REASON_DURABILITY_UNKNOWN: return "Write durability is not established";
        case PARTITION_WIPE_REASON_FLUSH_UNAVAILABLE: return "A required Flush operation is unavailable";
        case PARTITION_WIPE_REASON_MOUNTED: return "The selected partition is mounted or busy";
        case PARTITION_WIPE_REASON_ROOT_BACKING: return "The selected partition backs the running system";
        case PARTITION_WIPE_REASON_BOOT_BACKING: return "The selected device or partition backs boot storage";
        case PARTITION_WIPE_REASON_BOOT_IDENTITY_UNKNOWN: return "Boot storage identity is unknown";
        case PARTITION_WIPE_REASON_PROTECTED_SYSTEM_PARTITION: return "The partition is marked as protected system storage";
        case PARTITION_WIPE_REASON_WRITE_FAILED: return "A zero-overwrite write failed";
        case PARTITION_WIPE_REASON_FLUSH_FAILED: return "The required Flush operation failed";
        case PARTITION_WIPE_REASON_STOP_REQUESTED: return "Stop requested";
        default: return "Unknown wipe failure";
    }
}

} // namespace storage
} // namespace kernel
