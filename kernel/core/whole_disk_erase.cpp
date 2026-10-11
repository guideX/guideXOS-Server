#include "kernel/whole_disk_erase.h"
#include "kernel/usb_storage.h"

namespace kernel {
namespace storage {
namespace {

static const uint32_t ERASE_BATCH_BUFFER_BYTES = 1024u * 1024u;
alignas(4096) static uint8_t s_zeroBatch[ERASE_BATCH_BUFFER_BYTES];
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

static uint8_t lower_ascii(uint8_t value)
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<uint8_t>(value + ('a' - 'A')) : value;
}

static bool name_contains(const char* name, const char* needle)
{
    if (!name || !needle || !needle[0]) return false;
    for (size_t i = 0; name[i]; ++i) {
        size_t j = 0;
        while (needle[j] && name[i + j] &&
               lower_ascii(static_cast<uint8_t>(name[i + j])) ==
                   lower_ascii(static_cast<uint8_t>(needle[j]))) ++j;
        if (!needle[j]) return true;
    }
    return false;
}

static bool contains_system_label(const PartitionEntry& partition)
{
    return name_contains(partition.name, "boot") ||
           name_contains(partition.name, "system") ||
           name_contains(partition.name, "recovery") ||
           name_contains(partition.name, "windows") ||
           name_contains(partition.name, "efi") ||
           name_contains(partition.name, "operating system");
}

static bool protected_gpt_type(const PartitionEntry& partition)
{
    // EFI System, Microsoft Reserved, Windows Recovery, BIOS Boot, and Linux
    // root types are treated as protected even on a removable inactive disk.
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
    static const uint8_t linuxRoot[16] = {
        0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
        0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};
    return bytes_equal(partition.typeGuid, efiSystem, 16) ||
        bytes_equal(partition.typeGuid, microsoftReserved, 16) ||
        bytes_equal(partition.typeGuid, windowsRecovery, 16) ||
        bytes_equal(partition.typeGuid, biosBoot, 16) ||
        bytes_equal(partition.typeGuid, linuxRoot, 16);
}

static bool known_data_partition(const PartitionEntry& partition)
{
    if (partition.isGpt) {
        // The initial GPT allowlist is deliberately narrow. Generic Microsoft
        // Basic Data can contain an inactive Windows system image, so it stays
        // ambiguous until a future policy explicitly addresses that case.
        static const uint8_t linuxHome[16] = {
            0xE1, 0xC7, 0x3A, 0x93, 0xB4, 0x2E, 0x13, 0x4F,
            0xB8, 0x44, 0x0E, 0x14, 0xE2, 0xAE, 0xF9, 0x15};
        return bytes_equal(partition.typeGuid, linuxHome, 16);
    }
    // Initial MBR allowlist: the two FAT32 data types. NTFS, Linux, EFI,
    // extended, and unknown IDs need a future explicit policy.
    return partition.mbrType == 0x0Bu || partition.mbrType == 0x0Cu;
}

static bool valid_partition_geometry(const PartitionEntry& partition,
                                     uint64_t totalSectors)
{
    return partition.partitionNumber != 0 && partition.sectorCount != 0 &&
        partition.startLba <= partition.endLba &&
        partition.endLba < totalSectors &&
        partition.endLba - partition.startLba != UINT64_MAX &&
        partition.endLba - partition.startLba + 1u == partition.sectorCount &&
        checked_lba_range(totalSectors, partition.startLba,
                          partition.sectorCount);
}

static WholeDiskEraseReason classify_partition_map(
    const DeviceCapabilities& caps, WholeDiskMediaClassification& classification)
{
    classification = WHOLE_DISK_MEDIA_CLASS_AMBIGUOUS;
    if (!parse_partition_table(caps.globalIndex, s_partitionTable) ||
        (s_partitionTable.state != DISK_STATE_VALID_MBR &&
         s_partitionTable.state != DISK_STATE_VALID_GPT) ||
        (s_partitionTable.scheme != PARTITION_SCHEME_MBR &&
         s_partitionTable.scheme != PARTITION_SCHEME_GPT) ||
        s_partitionTable.partitionCount == 0 || s_partitionTable.hybridMbr ||
        s_partitionTable.extendedPartitionsPresent ||
        (s_partitionTable.scheme == PARTITION_SCHEME_GPT &&
         (!s_partitionTable.primaryGptValid ||
          !s_partitionTable.backupGptValid || !s_partitionTable.gptCopiesAgree ||
          s_partitionTable.gptCopiesConflict ||
          !s_partitionTable.protectiveMbrValid)))
        return WHOLE_DISK_ERASE_REASON_PARTITION_MAP_INVALID;

    for (uint16_t i = 0; i < s_partitionTable.partitionCount; ++i) {
        const PartitionEntry& partition = s_partitionTable.partitions[i];
        if (!valid_partition_geometry(partition, caps.totalLogicalSectors))
            return WHOLE_DISK_ERASE_REASON_PARTITION_MAP_INVALID;
        if ((!partition.isGpt && partition.bootable) ||
            (partition.isGpt && (protected_gpt_type(partition) ||
                                 (partition.attributes & 1u) != 0))) {
            classification = WHOLE_DISK_MEDIA_CLASS_PROTECTED_BOOTABLE;
            return WHOLE_DISK_ERASE_REASON_BOOTABLE_MEDIA_PROTECTED;
        }
        if (contains_system_label(partition) ||
            !known_data_partition(partition)) {
            classification = WHOLE_DISK_MEDIA_CLASS_AMBIGUOUS;
            return WHOLE_DISK_ERASE_REASON_BOOT_MEDIA_AMBIGUOUS;
        }
    }
    classification = WHOLE_DISK_MEDIA_CLASS_DATA_LAYOUT;
    return WHOLE_DISK_ERASE_REASON_NONE;
}

static void layout_hash_byte(uint64_t& first, uint64_t& second,
                             uint8_t value)
{
    first = (first ^ value) * 1099511628211ull;
    second = (second ^ static_cast<uint8_t>(value ^ 0xA7u)) *
        14029467366897019727ull;
}

static void layout_hash_u64(uint64_t& first, uint64_t& second,
                            uint64_t value, uint8_t byteCount)
{
    for (uint8_t i = 0; i < byteCount; ++i) {
        layout_hash_byte(first, second, static_cast<uint8_t>(value));
        value >>= 8;
    }
}

static void layout_hash_text(uint64_t& first, uint64_t& second,
                             const char* value, size_t capacity)
{
    size_t length = 0;
    while (length < capacity && value[length]) {
        layout_hash_byte(first, second,
            static_cast<uint8_t>(value[length]));
        ++length;
    }
    layout_hash_byte(first, second, 0);
}

static void fingerprint_partition_map(WholeDiskEraseProbe& probe)
{
    uint64_t first = 14695981039346656037ull;
    uint64_t second = 7809847782465536322ull;
    layout_hash_u64(first, second, s_partitionTable.scheme, 1);
    layout_hash_u64(first, second, s_partitionTable.state, 1);
    layout_hash_u64(first, second, s_partitionTable.partitionCount, 2);
    layout_hash_u64(first, second, s_partitionTable.tableEntryCount, 2);
    layout_hash_u64(first, second, s_partitionTable.gptEntrySize, 4);
    layout_hash_u64(first, second, s_partitionTable.gptEntryArraySectors, 4);
    layout_hash_u64(first, second, s_partitionTable.primaryGptHeaderCrc32, 4);
    layout_hash_u64(first, second, s_partitionTable.backupGptHeaderCrc32, 4);
    layout_hash_u64(first, second,
        s_partitionTable.primaryGptEntryArrayCrc32, 4);
    layout_hash_u64(first, second,
        s_partitionTable.backupGptEntryArrayCrc32, 4);
    layout_hash_u64(first, second,
        s_partitionTable.primaryGptEntryArrayLba, 8);
    layout_hash_u64(first, second,
        s_partitionTable.backupGptEntryArrayLba, 8);
    layout_hash_u64(first, second, s_partitionTable.firstUsableLba, 8);
    layout_hash_u64(first, second, s_partitionTable.lastUsableLba, 8);
    layout_hash_u64(first, second, s_partitionTable.protectiveMbr, 1);
    layout_hash_u64(first, second, s_partitionTable.hybridMbr, 1);
    layout_hash_u64(first, second,
        s_partitionTable.extendedPartitionsPresent, 1);
    layout_hash_u64(first, second, s_partitionTable.primaryGptValid, 1);
    layout_hash_u64(first, second, s_partitionTable.backupGptValid, 1);
    layout_hash_u64(first, second, s_partitionTable.gptCopiesAgree, 1);
    layout_hash_u64(first, second, s_partitionTable.gptCopiesConflict, 1);
    layout_hash_u64(first, second, s_partitionTable.protectiveMbrValid, 1);
    layout_hash_u64(first, second, s_partitionTable.primaryGptCopyState, 1);
    layout_hash_u64(first, second, s_partitionTable.backupGptCopyState, 1);
    layout_hash_u64(first, second, s_partitionTable.primaryGptError, 1);
    layout_hash_u64(first, second, s_partitionTable.backupGptError, 1);
    for (uint8_t i = 0; i < 16; ++i) {
        layout_hash_byte(first, second, s_partitionTable.primaryDiskGuid[i]);
        layout_hash_byte(first, second, s_partitionTable.backupDiskGuid[i]);
    }
    layout_hash_u64(first, second, s_partitionTable.mbrDiskSignature, 4);
    for (uint16_t i = 0; i < s_partitionTable.partitionCount; ++i) {
        const PartitionEntry& part = s_partitionTable.partitions[i];
        layout_hash_u64(first, second, part.partitionNumber, 2);
        layout_hash_u64(first, second, part.mbrType, 1);
        layout_hash_u64(first, second, part.isGpt, 1);
        layout_hash_u64(first, second, part.bootable, 1);
        for (uint8_t j = 0; j < 16; ++j) {
            layout_hash_byte(first, second, part.typeGuid[j]);
            layout_hash_byte(first, second, part.uniqueGuid[j]);
        }
        layout_hash_u64(first, second, part.startLba, 8);
        layout_hash_u64(first, second, part.endLba, 8);
        layout_hash_u64(first, second, part.sectorCount, 8);
        layout_hash_u64(first, second, part.attributes, 8);
        layout_hash_text(first, second, part.name, sizeof(part.name));
    }
    probe.partitionScheme = s_partitionTable.scheme;
    probe.partitionCount = s_partitionTable.partitionCount;
    probe.layoutFingerprint[0] = first;
    probe.layoutFingerprint[1] = second;
}

static WholeDiskEraseReason validate_live_target(
    const TargetIdentity& target, const StorageOperationLease* lease,
    const uint8_t* zeroBuffer, DeviceCapabilities& caps)
{
    if (lease) {
        if (!storage_operation_lease_is_current(*lease))
            return WHOLE_DISK_ERASE_REASON_OWNERSHIP_INVALID;
        if (!lease->targetPinned || lease->pinnedIndex != target.globalIndex ||
            lease->pinnedRegistrationId != target.registrationId)
            return WHOLE_DISK_ERASE_REASON_OWNERSHIP_INVALID;
    } else if (storage_operation_active()) {
        return WHOLE_DISK_ERASE_REASON_OPERATION_BUSY;
    }

    const RevalidationStatus identity = revalidate_target_identity(target);
    if (identity == TARGET_DEVICE_MISSING)
        return WHOLE_DISK_ERASE_REASON_DEVICE_MISSING;
    if (identity != TARGET_VALID)
        return WHOLE_DISK_ERASE_REASON_IDENTITY_CHANGED;

    if (!query_device_capabilities(target.globalIndex, caps))
        return WHOLE_DISK_ERASE_REASON_DEVICE_MISSING;
    if (!caps.geometryValid || !caps.capacityValid ||
        caps.totalLogicalSectors != target.totalLogicalSectors ||
        caps.logicalSectorSize != target.logicalSectorSize ||
        target.totalLogicalSectors == 0 ||
        !checked_lba_range(target.totalLogicalSectors, 0,
                           target.totalLogicalSectors))
        return WHOLE_DISK_ERASE_REASON_INVALID_GEOMETRY;
    if (!caps.readable) return WHOLE_DISK_ERASE_REASON_READ_UNAVAILABLE;
    if (caps.transport != block::BDEV_ATA_PIO &&
        caps.transport != block::BDEV_AHCI &&
        caps.transport != block::BDEV_USB_MASS)
        return WHOLE_DISK_ERASE_REASON_UNSUPPORTED_TRANSPORT;
    if (!caps.writable) return WHOLE_DISK_ERASE_REASON_READ_ONLY;
    if (caps.persistence != PERSISTENCE_FLUSH_REQUIRED ||
        !caps.flushSupported || !caps.flushSemanticsKnown)
        return caps.flushSupported
            ? WHOLE_DISK_ERASE_REASON_DURABILITY_UNKNOWN
            : WHOLE_DISK_ERASE_REASON_FLUSH_UNAVAILABLE;
    if (caps.transport == block::BDEV_USB_MASS &&
        caps.usbSyncCacheState != usb_storage::SYNC_CACHE_SUCCEEDED)
        return WHOLE_DISK_ERASE_REASON_DURABILITY_UNKNOWN;
    if (caps.requiredBufferAlignment > 4096u ||
        (caps.requiredBufferAlignment > 1u &&
         (reinterpret_cast<uintptr_t>(zeroBuffer) %
              caps.requiredBufferAlignment) != 0))
        return WHOLE_DISK_ERASE_REASON_INVALID_GEOMETRY;

    block::BlockDevice device = {};
    if (!block::copy_device(target.globalIndex, device) ||
        device.registrationId != target.registrationId ||
        device.forcedOffline)
        return WHOLE_DISK_ERASE_REASON_DEVICE_MISSING;
    if (device.type == block::BDEV_NVME)
        return WHOLE_DISK_ERASE_REASON_UNSUPPORTED_TRANSPORT;

    const RevalidationStatus finalIdentity = revalidate_target_identity(target);
    if (finalIdentity == TARGET_DEVICE_MISSING)
        return WHOLE_DISK_ERASE_REASON_DEVICE_MISSING;
    if (finalIdentity != TARGET_VALID)
        return WHOLE_DISK_ERASE_REASON_IDENTITY_CHANGED;
    return WHOLE_DISK_ERASE_REASON_NONE;
}

static void update_progress(WholeDiskEraseResult& result)
{
    if (result.totalSectors == 0 || result.logicalSectorSize == 0) {
        result.bytesWritten = 0;
        result.progressPercent = 0;
        return;
    }
    if (result.sectorsWritten > result.totalSectors)
        result.sectorsWritten = result.totalSectors;
    result.bytesWritten = result.sectorsWritten *
        static_cast<uint64_t>(result.logicalSectorSize);
    if (result.sectorsWritten == result.totalSectors) {
        result.progressPercent = result.state == WHOLE_DISK_ERASE_COMPLETED
            ? 100u : 99u;
        return;
    }
    result.progressPercent = static_cast<uint8_t>(
        (result.sectorsWritten * 100u) / result.totalSectors);
}

static void capture_block_failure(WholeDiskEraseResult& result,
                                  block::Status status, uint64_t lba,
                                  uint32_t count)
{
    result.blockStatusValid = true;
    result.blockStatus = status;
    result.failedLba = lba;
    result.failedSectorCount = count;
    result.blockDiagnostic = {};
    block::OperationDiagnostic diagnostic = {};
    if (block::last_operation_diagnostic(diagnostic) && diagnostic.valid &&
        diagnostic.registrationId == result.targetIdentity.registrationId &&
        diagnostic.operation == block::OPERATION_WRITE &&
        diagnostic.requestedLba == lba &&
        diagnostic.requestedSectors == count) {
        result.blockDiagnostic = diagnostic;
        const uint32_t completed =
            diagnostic.transportDiagnostic.completedSectors;
        if (completed <= count) {
            result.sectorsWritten += completed;
            result.failedLba = lba + completed;
            result.failedSectorCount = count - completed;
        }
    }
    update_progress(result);
}

static bool release_ownership(WholeDiskEraseJob& job)
{
    if (job.ownershipReleaseAttempted) return job.result.ownershipReleased;
    if (job.lease.ownerToken == 0) return false;
    job.ownershipReleaseAttempted = true;
    job.result.ownershipReleased = release_storage_operation(job.lease);
    return job.result.ownershipReleased;
}

static void set_terminal(WholeDiskEraseJob& job, WholeDiskEraseState state,
                         WholeDiskEraseReason reason, const char* message)
{
    job.result.phase = WHOLE_DISK_ERASE_PHASE_TERMINAL;
    job.result.state = state;
    job.result.reason = reason;
    job.result.partitionMapMayBeDestroyed = job.result.writeAttempted;
    copy_text(job.result.diagnostic, sizeof(job.result.diagnostic), message);
    update_progress(job.result);
    if (job.lease.ownerToken != 0 && !release_ownership(job)) {
        job.result.state = WHOLE_DISK_ERASE_FAILED;
        job.result.reason = WHOLE_DISK_ERASE_REASON_OWNERSHIP_INVALID;
        copy_text(job.result.diagnostic, sizeof(job.result.diagnostic),
            "Erase ended, but storage-operation ownership release failed.");
        update_progress(job.result);
    }
}

static bool finish_flush(WholeDiskEraseJob& job,
                         WholeDiskEraseReason& reason)
{
    job.result.phase = WHOLE_DISK_ERASE_PHASE_FLUSH;
    DeviceCapabilities caps = {};
    reason = validate_live_target(job.result.targetIdentity, &job.lease,
                                  s_zeroBatch, caps);
    if (reason != WHOLE_DISK_ERASE_REASON_NONE) return false;

    job.result.flushAttempted = true;
    const block::FlushReport report = block::flush_with_result(
        job.result.targetIdentity.globalIndex);
    job.result.flushOutcome = report.outcome;
    job.result.flushStatus = report.status;
    job.result.durabilityConfirmed = report.status == block::BLOCK_OK &&
        report.semanticsKnown &&
        report.outcome == block::FLUSH_OUTCOME_SUPPORTED_SUCCEEDED;
    if (!job.result.durabilityConfirmed) {
        reason = report.status == block::BLOCK_ERR_UNSUPPORTED ||
                 !report.semanticsKnown
            ? WHOLE_DISK_ERASE_REASON_DURABILITY_UNKNOWN
            : WHOLE_DISK_ERASE_REASON_FLUSH_FAILED;
        return false;
    }
    reason = WHOLE_DISK_ERASE_REASON_NONE;
    return true;
}

static void settle_stopped(WholeDiskEraseJob& job)
{
    job.result.stopRequested = true;
    if (job.result.writeAttempted) {
        WholeDiskEraseReason reason = WHOLE_DISK_ERASE_REASON_NONE;
        if (!finish_flush(job, reason)) {
            set_terminal(job, WHOLE_DISK_ERASE_FAILED, reason,
                "Erase stopped after writes began, but durability could not be confirmed. The disk may be partially erased and unusable.");
            return;
        }
    }
    set_terminal(job, WHOLE_DISK_ERASE_STOPPED,
        WHOLE_DISK_ERASE_REASON_STOP_REQUESTED,
        job.result.writeAttempted
            ? "Erase stopped after partial overwrite. The partition map and data may be destroyed; the disk may be unusable."
            : "Erase stopped before any overwrite write completed.");
}

static void settle_failed_write(WholeDiskEraseJob& job,
                                block::Status status, uint64_t lba,
                                uint32_t count)
{
    capture_block_failure(job.result, status, lba, count);
    WholeDiskEraseReason reason = status == block::BLOCK_ERR_NO_MEDIA
        ? WHOLE_DISK_ERASE_REASON_DEVICE_MISSING
        : WHOLE_DISK_ERASE_REASON_WRITE_FAILED;
    if (status != block::BLOCK_ERR_NO_MEDIA) {
        DeviceCapabilities caps = {};
        const WholeDiskEraseReason live = validate_live_target(
            job.result.targetIdentity, &job.lease, s_zeroBatch, caps);
        if (live == WHOLE_DISK_ERASE_REASON_NONE) {
            WholeDiskEraseReason flushReason = WHOLE_DISK_ERASE_REASON_NONE;
            (void)finish_flush(job, flushReason);
            reason = WHOLE_DISK_ERASE_REASON_WRITE_FAILED;
        } else {
            reason = live;
        }
    }
    set_terminal(job, WHOLE_DISK_ERASE_FAILED, reason,
        job.result.writeAttempted
            ? "Erase failed after writes began. The disk may be partially erased, its partition map may be destroyed, and the device may be unusable."
            : "Erase failed before any overwrite write completed.");
}

static WholeDiskEraseReason validate_admission_policy(
    const TargetIdentity& target, const StorageOperationLease* lease,
    DeviceCapabilities& caps, WholeDiskMediaClassification& classification)
{
    WholeDiskEraseReason reason = validate_live_target(
        target, lease, s_zeroBatch, caps);
    if (reason != WHOLE_DISK_ERASE_REASON_NONE) return reason;

    const MountProtection mount = query_mount_protection(target);
    if (mount.safety == DEVICE_ROOT_BACKING)
        return WHOLE_DISK_ERASE_REASON_ROOT_BACKING;
    if (mount.safety == DEVICE_MOUNTED)
        return WHOLE_DISK_ERASE_REASON_MOUNTED_CHILD;
    if (mount.safety != DEVICE_UNMOUNTED)
        return WHOLE_DISK_ERASE_REASON_MOUNTED_CHILD;

    const BootProtection boot = query_boot_protection(target);
    if (boot.safety == BOOT_DEVICE_IS_TARGET)
        return WHOLE_DISK_ERASE_REASON_BOOT_BACKING;
    if (boot.safety != BOOT_DEVICE_DEFINITELY_NOT_TARGET)
        return WHOLE_DISK_ERASE_REASON_BOOT_IDENTITY_UNKNOWN;

    reason = classify_partition_map(caps, classification);
    const WholeDiskEraseReason finalLive = validate_live_target(
        target, lease, s_zeroBatch, caps);
    return finalLive != WHOLE_DISK_ERASE_REASON_NONE ? finalLive : reason;
}

} // namespace

WholeDiskEraseReason probe_whole_disk_erase_target(
    const TargetIdentity& target, WholeDiskEraseProbe& probe)
{
    probe = {};
    probe.targetIdentity = target;
    probe.reason = WHOLE_DISK_ERASE_REASON_INVALID_REQUEST;
    probe.mediaClassification = WHOLE_DISK_MEDIA_CLASS_UNKNOWN;
    if (target.registrationId == 0 || target.registryGeneration == 0 ||
        target.registryGeneration == UINT64_MAX ||
        target.registryGeneration != block::registry_generation())
        return probe.reason;
    if (storage_operation_active()) {
        probe.reason = WHOLE_DISK_ERASE_REASON_OPERATION_BUSY;
        return probe.reason;
    }

    DeviceCapabilities caps = {};
    WholeDiskMediaClassification classification =
        WHOLE_DISK_MEDIA_CLASS_UNKNOWN;
    probe.reason = validate_admission_policy(target, nullptr, caps,
                                             classification);
    probe.capabilities = caps;
    probe.mediaClassification = classification;
    if (probe.reason == WHOLE_DISK_ERASE_REASON_NONE) {
        fingerprint_partition_map(probe);
        // A competing operation may have acquired ownership while the
        // read-only partition-table validation was running.
        if (storage_operation_active())
            probe.reason = WHOLE_DISK_ERASE_REASON_OPERATION_BUSY;
    }
    return probe.reason;
}

WholeDiskEraseState begin_whole_disk_erase_job(
    WholeDiskEraseJob& job, const WholeDiskEraseRequest& request)
{
    if (job.result.state == WHOLE_DISK_ERASE_RUNNING)
        return job.result.state;
    job = {};
    job.request = request;
    job.result.state = WHOLE_DISK_ERASE_NOT_ADMITTED;
    job.result.phase = WHOLE_DISK_ERASE_PHASE_ADMISSION;
    job.result.targetIdentity = request.targetSnapshot;
    job.result.startLba = 0;
    job.result.failedLba = UINT64_MAX;
    job.result.blockStatus = block::BLOCK_ERR_INVALID;
    job.result.flushStatus = block::BLOCK_ERR_INVALID;

    if (request.scope != WHOLE_DISK_ERASE_ALL_ADDRESSABLE_SECTORS ||
        request.targetSnapshot.registrationId == 0 ||
        request.expectedRegistryGeneration == 0 ||
        request.expectedRegistryGeneration !=
            request.targetSnapshot.registryGeneration ||
        request.targetSnapshot.registryGeneration == UINT64_MAX ||
        request.targetSnapshot.registryGeneration !=
            block::registry_generation()) {
        job.result.reason = WHOLE_DISK_ERASE_REASON_INVALID_REQUEST;
        job.result.phase = WHOLE_DISK_ERASE_PHASE_TERMINAL;
        copy_text(job.result.diagnostic, sizeof(job.result.diagnostic),
            "An explicit whole-device erase request and live disk selection are required.");
        return job.result.state;
    }

    const StorageOperationLockStatus lock =
        try_acquire_storage_operation(job.lease);
    if (lock != STORAGE_OPERATION_LOCK_ACQUIRED) {
        job.result.reason = lock == STORAGE_OPERATION_LOCK_BUSY
            ? WHOLE_DISK_ERASE_REASON_OPERATION_BUSY
            : WHOLE_DISK_ERASE_REASON_OWNERSHIP_INVALID;
        job.result.phase = WHOLE_DISK_ERASE_PHASE_TERMINAL;
        copy_text(job.result.diagnostic, sizeof(job.result.diagnostic),
            whole_disk_erase_reason_name(job.result.reason));
        return job.result.state;
    }
    job.result.ownershipAcquired = true;
    job.result.operationId = job.lease.ownerToken;
    if (!pin_storage_operation_target(job.lease, request.targetSnapshot)) {
        const RevalidationStatus identity =
            revalidate_target_identity(request.targetSnapshot);
        set_terminal(job, WHOLE_DISK_ERASE_NOT_ADMITTED,
            identity == TARGET_DEVICE_MISSING
                ? WHOLE_DISK_ERASE_REASON_DEVICE_MISSING
                : WHOLE_DISK_ERASE_REASON_IDENTITY_CHANGED,
            "The exact device registration could not be pinned.");
        return job.result.state;
    }

    DeviceCapabilities caps = {};
    WholeDiskMediaClassification classification =
        WHOLE_DISK_MEDIA_CLASS_UNKNOWN;
    WholeDiskEraseReason reason = validate_admission_policy(
        request.targetSnapshot, &job.lease, caps, classification);
    job.result.mediaClassification = classification;
    if (reason != WHOLE_DISK_ERASE_REASON_NONE) {
        job.result.reason = reason;
        set_terminal(job, WHOLE_DISK_ERASE_NOT_ADMITTED, reason,
            whole_disk_erase_reason_name(reason));
        return job.result.state;
    }

    uint64_t totalBytes = 0;
    if (!valid_geometry(caps.totalLogicalSectors, caps.logicalSectorSize,
                        &totalBytes)) {
        job.result.reason = WHOLE_DISK_ERASE_REASON_INVALID_GEOMETRY;
        set_terminal(job, WHOLE_DISK_ERASE_NOT_ADMITTED,
            job.result.reason, whole_disk_erase_reason_name(job.result.reason));
        return job.result.state;
    }
    const uint32_t transferLimit = caps.maxTransferBytes == 0 ||
        caps.maxTransferBytes > ERASE_BATCH_BUFFER_BYTES
        ? ERASE_BATCH_BUFFER_BYTES : caps.maxTransferBytes;
    const uint32_t maxSectors = transferLimit / caps.logicalSectorSize;
    if (maxSectors == 0) {
        job.result.reason = WHOLE_DISK_ERASE_REASON_INVALID_GEOMETRY;
        set_terminal(job, WHOLE_DISK_ERASE_NOT_ADMITTED,
            job.result.reason, whole_disk_erase_reason_name(job.result.reason));
        return job.result.state;
    }

    for (size_t i = 0; i < sizeof(s_zeroBatch); ++i) s_zeroBatch[i] = 0;
    job.maxBatchSectors = maxSectors;
    job.result.logicalSectorSize = caps.logicalSectorSize;
    job.result.totalSectors = caps.totalLogicalSectors;
    job.result.totalBytes = totalBytes;
    job.result.state = WHOLE_DISK_ERASE_RUNNING;
    job.result.reason = WHOLE_DISK_ERASE_REASON_NONE;
    job.result.phase = WHOLE_DISK_ERASE_PHASE_OVERWRITE;
    copy_text(job.result.diagnostic, sizeof(job.result.diagnostic),
        "Whole-device zero-overwrite admitted; no write batch has completed.");
    return job.result.state;
}

WholeDiskEraseState step_whole_disk_erase_job(WholeDiskEraseJob& job)
{
    if (job.result.state != WHOLE_DISK_ERASE_RUNNING || job.stepActive)
        return job.result.state;
    job.stepActive = true;
    if (job.result.stopRequested && job.nextLba < job.result.totalSectors) {
        job.stepActive = false;
        settle_stopped(job);
        return job.result.state;
    }

    DeviceCapabilities caps = {};
    WholeDiskEraseReason reason = validate_live_target(
        job.result.targetIdentity, &job.lease, s_zeroBatch, caps);
    if (reason != WHOLE_DISK_ERASE_REASON_NONE) {
        job.stepActive = false;
        set_terminal(job, WHOLE_DISK_ERASE_FAILED, reason,
            job.result.writeAttempted
                ? "The exact target became unavailable after writes began; the disk may be partially erased."
                : "The exact target or its full-device geometry is no longer valid.");
        return job.result.state;
    }

    // Before the first write, repeat all admission-only checks. After the
    // first attempt, LBA 0 may intentionally be invalid and must not be parsed.
    if (!job.result.writeAttempted) {
        WholeDiskMediaClassification classification =
            WHOLE_DISK_MEDIA_CLASS_UNKNOWN;
        reason = validate_admission_policy(job.result.targetIdentity,
            &job.lease, caps, classification);
        job.result.mediaClassification = classification;
        if (reason != WHOLE_DISK_ERASE_REASON_NONE) {
            job.stepActive = false;
            set_terminal(job, WHOLE_DISK_ERASE_FAILED, reason,
                whole_disk_erase_reason_name(reason));
            return job.result.state;
        }
    }

    if (job.nextLba == job.result.totalSectors) {
        job.stepActive = false;
        reason = WHOLE_DISK_ERASE_REASON_NONE;
        if (!finish_flush(job, reason)) {
            set_terminal(job, WHOLE_DISK_ERASE_FAILED, reason,
                "Every addressable sector was written, but required durability was not confirmed.");
        } else {
            set_terminal(job, WHOLE_DISK_ERASE_COMPLETED,
                WHOLE_DISK_ERASE_REASON_NONE,
                "Every addressable logical sector was overwritten with zeros and required Flush succeeded.");
        }
        return job.result.state;
    }

    const uint64_t remaining = job.result.totalSectors - job.nextLba;
    const uint32_t count = static_cast<uint32_t>(remaining <
        job.maxBatchSectors ? remaining : job.maxBatchSectors);
    if (count == 0 || count > UINT32_MAX / caps.logicalSectorSize ||
        !checked_lba_range(job.result.totalSectors, job.nextLba, count)) {
        job.stepActive = false;
        set_terminal(job, WHOLE_DISK_ERASE_FAILED,
            WHOLE_DISK_ERASE_REASON_INVALID_GEOMETRY,
            "The next bounded whole-device range is outside the immutable capacity.");
        return job.result.state;
    }
    const uint32_t bytes = count * caps.logicalSectorSize;
    if (bytes == 0 || bytes > sizeof(s_zeroBatch)) {
        job.stepActive = false;
        set_terminal(job, WHOLE_DISK_ERASE_FAILED,
            WHOLE_DISK_ERASE_REASON_INVALID_GEOMETRY,
            "The next whole-device batch has invalid byte geometry.");
        return job.result.state;
    }

    const uint64_t lba = job.nextLba;
    job.result.writeAttempted = true;
    job.result.mediaMayHaveChanged = true;
    job.result.partitionMapMayBeDestroyed = true;
    job.result.phase = WHOLE_DISK_ERASE_PHASE_OVERWRITE;
    const block::Status status = write_sectors_safe(
        job.result.targetIdentity.globalIndex, lba, count, s_zeroBatch, bytes);
    if (status != block::BLOCK_OK) {
        job.stepActive = false;
        settle_failed_write(job, status, lba, count);
        return job.result.state;
    }

    job.result.sectorsWritten += count;
    job.nextLba += count;
    update_progress(job.result);
    job.stepActive = false;

    if (job.result.stopRequested) settle_stopped(job);
    return job.result.state;
}

bool request_whole_disk_erase_stop(WholeDiskEraseJob& job)
{
    if (job.result.state != WHOLE_DISK_ERASE_RUNNING) return false;
    job.result.stopRequested = true;
    return true;
}

const char* whole_disk_erase_state_name(WholeDiskEraseState state)
{
    switch (state) {
        case WHOLE_DISK_ERASE_NOT_ADMITTED: return "Not admitted";
        case WHOLE_DISK_ERASE_RUNNING: return "Running";
        case WHOLE_DISK_ERASE_STOPPED: return "Stopped";
        case WHOLE_DISK_ERASE_FAILED: return "Failed";
        case WHOLE_DISK_ERASE_COMPLETED: return "Completed";
        default: return "Unknown";
    }
}

const char* whole_disk_erase_reason_name(WholeDiskEraseReason reason)
{
    switch (reason) {
        case WHOLE_DISK_ERASE_REASON_NONE: return "No error";
        case WHOLE_DISK_ERASE_REASON_INVALID_REQUEST: return "An explicit whole-device request and live disk selection are required";
        case WHOLE_DISK_ERASE_REASON_OPERATION_BUSY: return "Another destructive storage operation is active";
        case WHOLE_DISK_ERASE_REASON_OWNERSHIP_INVALID: return "Whole-device operation ownership is invalid";
        case WHOLE_DISK_ERASE_REASON_DEVICE_MISSING: return "The exact target device is unavailable";
        case WHOLE_DISK_ERASE_REASON_IDENTITY_CHANGED: return "The exact device registration or identity changed";
        case WHOLE_DISK_ERASE_REASON_INVALID_GEOMETRY: return "The device capacity or logical-sector geometry is invalid";
        case WHOLE_DISK_ERASE_REASON_UNSUPPORTED_TRANSPORT: return "This storage transport is not qualified for whole-device overwriting";
        case WHOLE_DISK_ERASE_REASON_READ_UNAVAILABLE: return "Device reads required for admission are unavailable";
        case WHOLE_DISK_ERASE_REASON_WRITE_UNAVAILABLE: return "The device does not support writes";
        case WHOLE_DISK_ERASE_REASON_READ_ONLY: return "The device is read-only";
        case WHOLE_DISK_ERASE_REASON_DURABILITY_UNKNOWN: return "Write durability or USB Synchronize Cache support is not established";
        case WHOLE_DISK_ERASE_REASON_FLUSH_UNAVAILABLE: return "A qualified Flush operation is unavailable";
        case WHOLE_DISK_ERASE_REASON_MOUNTED_CHILD: return "At least one partition on this device is mounted or has ambiguous VFS ownership";
        case WHOLE_DISK_ERASE_REASON_ROOT_BACKING: return "This device backs the running root volume";
        case WHOLE_DISK_ERASE_REASON_BOOT_BACKING: return "This device backs the running boot source";
        case WHOLE_DISK_ERASE_REASON_BOOT_IDENTITY_UNKNOWN: return "Boot-device provenance is unknown";
        case WHOLE_DISK_ERASE_REASON_PARTITION_MAP_INVALID: return "The partition map is missing, unsupported, inconsistent, or ambiguous";
        case WHOLE_DISK_ERASE_REASON_BOOTABLE_MEDIA_PROTECTED: return "Bootable or protected system metadata was found on this device";
        case WHOLE_DISK_ERASE_REASON_BOOT_MEDIA_AMBIGUOUS: return "Partition metadata does not establish an ordinary data disk";
        case WHOLE_DISK_ERASE_REASON_WRITE_FAILED: return "A bounded overwrite write failed";
        case WHOLE_DISK_ERASE_REASON_FLUSH_FAILED: return "Required Flush failed";
        case WHOLE_DISK_ERASE_REASON_STOP_REQUESTED: return "Stop requested";
        default: return "Unknown whole-device erase reason";
    }
}

} // namespace storage
} // namespace kernel
