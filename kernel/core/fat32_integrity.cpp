#include "include/kernel/fat32_integrity.h"
#include "include/kernel/fat32_formatter.h"
#include "include/kernel/vfs.h"

namespace kernel {
namespace storage {
namespace {

static const uint32_t kFat32MinimumClusters = 65525u;
static const uint32_t kFat32MaximumClusters = 0x0FFFFFEEu;
static const uint32_t kFat32MaximumClusterBytes = 32768u;

alignas(4096) static uint8_t s_primaryBoot[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_sector[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_compareReference[
    FAT32_INTEGRITY_COMPARE_BUFFER_BYTES];
alignas(4096) static uint8_t s_compareOther[
    FAT32_INTEGRITY_COMPARE_BUFFER_BYTES];

static uint16_t read_u16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t read_u32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t& result)
{
    if (left > UINT64_MAX - right) return false;
    result = left + right;
    return true;
}

static bool mul_u64(uint64_t left, uint64_t right, uint64_t& result)
{
    if (left != 0 && right > UINT64_MAX / left) return false;
    result = left * right;
    return true;
}

static bool bytes_equal(const uint8_t* left, const uint8_t* right,
                        size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if (left[i] != right[i]) return false;
    return true;
}

static void copy_text(char* destination, size_t capacity,
                      const char* source)
{
    if (!destination || capacity == 0) return;
    size_t i = 0;
    if (source) {
        while (source[i] && i + 1 < capacity) {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
}

static void set_diagnostic(Fat32IntegrityResult& result,
                           const char* message)
{
    copy_text(result.diagnostic, sizeof(result.diagnostic), message);
}

static uint8_t ratio_percent(uint64_t numerator, uint64_t denominator)
{
    if (denominator == 0 || numerator == 0) return 0;
    if (numerator >= denominator) return 100;
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

static void update_progress(Fat32IntegrityResult& result)
{
    if (result.state == FAT32_INTEGRITY_PASSED ||
        result.state == FAT32_INTEGRITY_INCONSISTENCY) {
        result.progressPercent = 100;
    } else if (result.fatBytesToCompare != 0) {
        result.progressPercent = static_cast<uint8_t>(10u +
            (ratio_percent(result.fatBytesCompared,
                           result.fatBytesToCompare) * 89u) / 100u);
    } else if (result.metadataReadRequests > 0) {
        result.progressPercent = 10;
    }
}

static bool guid_nonzero(const uint8_t guid[16])
{
    if (!guid) return false;
    for (uint8_t i = 0; i < 16; ++i)
        if (guid[i] != 0) return true;
    return false;
}

static bool same_partition_snapshot(const PartitionEntry& left,
                                    const PartitionEntry& right,
                                    PartitionScheme scheme)
{
    if (left.partitionNumber != right.partitionNumber ||
        left.isGpt != right.isGpt || left.mbrType != right.mbrType ||
        left.startLba != right.startLba || left.endLba != right.endLba ||
        left.sectorCount != right.sectorCount ||
        left.attributes != right.attributes) return false;
    if (scheme == PARTITION_SCHEME_GPT)
        return bytes_equal(left.typeGuid, right.typeGuid, 16) &&
            bytes_equal(left.uniqueGuid, right.uniqueGuid, 16);
    return scheme == PARTITION_SCHEME_MBR;
}

static bool valid_table_snapshot(const PartitionTableModel& table,
                                 const PartitionEntry& partition)
{
    const DiskState expectedState = table.scheme == PARTITION_SCHEME_GPT
        ? DISK_STATE_VALID_GPT : DISK_STATE_VALID_MBR;
    if (table.state != expectedState || table.partitionCount == 0 ||
        table.partitionCount > MAX_PARSED_PARTITIONS || table.hybridMbr ||
        table.extendedPartitionsPresent ||
        (table.scheme != PARTITION_SCHEME_GPT &&
         table.scheme != PARTITION_SCHEME_MBR)) return false;
    if (table.scheme == PARTITION_SCHEME_GPT &&
        (!table.primaryGptValid || !table.backupGptValid ||
         !table.gptCopiesAgree || table.gptCopiesConflict ||
         !guid_nonzero(table.primaryDiskGuid) ||
         !bytes_equal(table.primaryDiskGuid, table.backupDiskGuid, 16)))
        return false;
    if (partition.partitionNumber == 0 || partition.sectorCount == 0 ||
        partition.isGpt != (table.scheme == PARTITION_SCHEME_GPT) ||
        partition.startLba > partition.endLba ||
        partition.endLba - partition.startLba != partition.sectorCount - 1)
        return false;

    uint16_t matches = 0;
    for (uint16_t i = 0; i < table.partitionCount; ++i) {
        const PartitionEntry& candidate = table.partitions[i];
        if (same_partition_snapshot(candidate, partition, table.scheme))
            ++matches;
    }
    return matches == 1;
}

static bool valid_request_shape(const Fat32IntegrityRequest& request)
{
    const TargetIdentity& target = request.targetSnapshot;
    const PartitionEntry& partition = request.partitionSnapshot;
    if (target.registrationId == 0 || target.registryGeneration == 0 ||
        target.registryGeneration == UINT64_MAX ||
        request.expectedRegistryGeneration != target.registryGeneration ||
        (request.partitionScheme != PARTITION_SCHEME_GPT &&
         request.partitionScheme != PARTITION_SCHEME_MBR) ||
        partition.partitionNumber == 0 || partition.sectorCount == 0 ||
        partition.startLba == 0 || partition.startLba > partition.endLba ||
        partition.endLba - partition.startLba != partition.sectorCount - 1 ||
        partition.isGpt !=
            (request.partitionScheme == PARTITION_SCHEME_GPT) ||
        !checked_lba_range(target.totalLogicalSectors, partition.startLba,
                           partition.sectorCount)) return false;
    if (request.partitionScheme == PARTITION_SCHEME_GPT &&
        (!guid_nonzero(request.gptDiskGuid) ||
         !guid_nonzero(partition.uniqueGuid) ||
         !guid_nonzero(partition.typeGuid))) return false;
    return true;
}

static bool target_has_active_mount(const TargetIdentity& target)
{
    for (uint8_t i = 0; i < vfs::VFS_MAX_MOUNTS; ++i) {
        const vfs::MountPoint* mount = vfs::get_mount_by_index(i);
        if (mount && mount->active &&
            mount->blockDevIndex == target.globalIndex)
            return true;
    }
    return false;
}

static Fat32IntegrityReason map_identity_failure(
    const TargetIdentity& target, RevalidationStatus status)
{
    switch (status) {
        case TARGET_VALID:
            return FAT32_INTEGRITY_REASON_NONE;
        case TARGET_DEVICE_MISSING:
            return FAT32_INTEGRITY_REASON_DEVICE_REMOVED;
        case TARGET_REGISTRY_CHANGED:
            return block::registration_is_present(target.globalIndex,
                    target.registrationId)
                ? FAT32_INTEGRITY_REASON_REGISTRATION_INVALIDATED
                : FAT32_INTEGRITY_REASON_DEVICE_REMOVED;
        case TARGET_IDENTITY_MISMATCH:
        default:
            return FAT32_INTEGRITY_REASON_TARGET_IDENTITY_CHANGED;
    }
}

static Fat32IntegrityReason validate_live_target(
    const Fat32IntegrityJob& job, DeviceCapabilities& capabilities)
{
    const TargetIdentity& target = job.result.targetIdentity;
    if (!storage_operation_lease_is_current(job.lease) ||
        !job.lease.targetPinned ||
        job.lease.pinnedIndex != target.globalIndex ||
        job.lease.pinnedRegistrationId != target.registrationId)
        return FAT32_INTEGRITY_REASON_OWNERSHIP_INVALID;

    RevalidationStatus identity = revalidate_target_identity(target);
    if (identity != TARGET_VALID)
        return map_identity_failure(target, identity);
    if (!query_device_capabilities(target.globalIndex, capabilities))
        return FAT32_INTEGRITY_REASON_DEVICE_REMOVED;
    if (!capabilities.geometryValid || !capabilities.capacityValid ||
        capabilities.totalLogicalSectors != target.totalLogicalSectors ||
        capabilities.logicalSectorSize != target.logicalSectorSize ||
        !valid_geometry(capabilities.totalLogicalSectors,
                        capabilities.logicalSectorSize) ||
        !fat32_format_sector_size_supported(capabilities.logicalSectorSize))
        return FAT32_INTEGRITY_REASON_UNSUPPORTED_SECTOR_SIZE;
    if (!capabilities.readable)
        return FAT32_INTEGRITY_REASON_READ_UNAVAILABLE;
    if (capabilities.maxTransferBytes != 0 &&
        capabilities.maxTransferBytes < capabilities.logicalSectorSize)
        return FAT32_INTEGRITY_REASON_UNSUPPORTED_SECTOR_SIZE;
    if (capabilities.requiredBufferAlignment > 4096u ||
        (capabilities.requiredBufferAlignment > 1u &&
         reinterpret_cast<uintptr_t>(s_sector) %
             capabilities.requiredBufferAlignment != 0u) ||
        (capabilities.requiredBufferAlignment > 1u &&
         reinterpret_cast<uintptr_t>(s_compareReference) %
             capabilities.requiredBufferAlignment != 0u) ||
        (capabilities.requiredBufferAlignment > 1u &&
         reinterpret_cast<uintptr_t>(s_compareOther) %
             capabilities.requiredBufferAlignment != 0u))
        return FAT32_INTEGRITY_REASON_UNSUPPORTED_SECTOR_SIZE;

    identity = revalidate_target_identity(target);
    return identity == TARGET_VALID ? FAT32_INTEGRITY_REASON_NONE
        : map_identity_failure(target, identity);
}

static void record_finding(Fat32IntegrityJob& job,
                           Fat32IntegrityFindingCategory category,
                           Fat32IntegrityStructure structure,
                           Fat32IntegrityFindingSeverity severity,
                           uint64_t lba, uint32_t sectorCount,
                           uint32_t byteOffset, uint64_t expected,
                           uint64_t observed, bool valuesValid)
{
    if (severity == FAT32_INTEGRITY_STRUCTURAL_INCONSISTENCY)
        job.result.structuralInconsistencyFound = true;
    if (job.result.findingCount >= FAT32_INTEGRITY_MAX_FINDINGS) {
        job.result.additionalFindingsMayExist = true;
        return;
    }
    Fat32IntegrityFinding& finding =
        job.result.findings[job.result.findingCount++];
    finding.category = category;
    finding.structure = structure;
    finding.severity = severity;
    finding.lba = lba;
    finding.sectorCount = sectorCount;
    finding.byteOffset = byteOffset;
    finding.expected = expected;
    finding.observed = observed;
    finding.expectedObservedValid = valuesValid;
}

static bool absolute_partition_lba(const Fat32IntegrityJob& job,
                                   uint64_t relativeLba, uint32_t count,
                                   uint64_t& absoluteLba)
{
    if (!checked_lba_range(job.result.partitionSectorCount,
                           relativeLba, count) ||
        !add_u64(job.result.partitionStartLba, relativeLba, absoluteLba) ||
        !checked_lba_range(job.result.targetIdentity.totalLogicalSectors,
                           absoluteLba, count)) return false;
    return true;
}

static Fat32IntegrityState finish_job(Fat32IntegrityJob& job,
                                      Fat32IntegrityState state,
                                      Fat32IntegrityReason reason,
                                      const char* message)
{
    job.result.state = state;
    job.result.reason = reason;
    job.step = FAT32_INTEGRITY_STEP_FINISHED;
    job.stepActive = false;
    set_diagnostic(job.result, message);
    update_progress(job.result);

    if (job.result.ownershipAcquired && !job.ownershipReleaseAttempted) {
        job.ownershipReleaseAttempted = true;
        job.result.ownershipReleased = release_storage_operation(job.lease);
        if (!job.result.ownershipReleased) {
            job.result.state = FAT32_INTEGRITY_INCOMPLETE;
            job.result.reason = FAT32_INTEGRITY_REASON_OWNERSHIP_INVALID;
            set_diagnostic(job.result,
                "Verification ended, but storage-operation ownership could not be released.");
        }
    }
    return job.result.state;
}

static void capture_read_failure(Fat32IntegrityJob& job,
                                 block::Status status, uint64_t absoluteLba,
                                 uint32_t count,
                                 Fat32IntegrityStructure structure)
{
    job.result.blockStatusValid = true;
    job.result.blockStatus = status;
    job.result.failedLba = absoluteLba;
    job.result.failedSectorCount = count;
    block::OperationDiagnostic diagnostic = {};
    if (block::last_operation_diagnostic(diagnostic) && diagnostic.valid &&
        diagnostic.globalIndex == job.result.targetIdentity.globalIndex &&
        diagnostic.registrationId ==
            job.result.targetIdentity.registrationId &&
        diagnostic.operation == block::OPERATION_READ &&
        diagnostic.requestedLba == absoluteLba &&
        diagnostic.requestedSectors == count &&
        diagnostic.status == status)
        job.result.blockDiagnostic = diagnostic;
    record_finding(job, FAT32_INTEGRITY_FINDING_READ_FAILURE, structure,
        FAT32_INTEGRITY_ADVISORY, absoluteLba, count, 0,
        0, static_cast<uint8_t>(status), false);
}

static Fat32IntegrityState fail_read(Fat32IntegrityJob& job,
                                     block::Status status,
                                     uint64_t absoluteLba, uint32_t count,
                                     Fat32IntegrityStructure structure)
{
    capture_read_failure(job, status, absoluteLba, count, structure);
    if (status == block::BLOCK_ERR_NO_MEDIA)
        return finish_job(job, FAT32_INTEGRITY_TARGET_INVALIDATED,
            FAT32_INTEGRITY_REASON_DEVICE_REMOVED,
            "The selected registration was removed during verification.");
    const RevalidationStatus identity =
        revalidate_target_identity(job.result.targetIdentity);
    if (identity != TARGET_VALID) {
        const Fat32IntegrityReason reason = map_identity_failure(
            job.result.targetIdentity, identity);
        return finish_job(job, FAT32_INTEGRITY_TARGET_INVALIDATED, reason,
            fat32_integrity_reason_name(reason));
    }
    return finish_job(job, FAT32_INTEGRITY_READ_ERROR,
        FAT32_INTEGRITY_REASON_READ_FAILURE,
        "A metadata read failed; the requested range is not localized beyond this read request.");
}

static bool read_partition_range(Fat32IntegrityJob& job,
                                 uint64_t relativeLba, uint32_t count,
                                 void* buffer, size_t bufferBytes,
                                 Fat32IntegrityStructure structure,
                                 Fat32IntegrityState& terminal)
{
    uint64_t absoluteLba = 0;
    if (!buffer || count == 0 || !absolute_partition_lba(job, relativeLba,
            count, absoluteLba)) {
        terminal = finish_job(job, FAT32_INTEGRITY_INCOMPLETE,
            FAT32_INTEGRITY_REASON_INTERNAL_BOUNDS_FAILURE,
            "A metadata request failed the validated partition bounds check.");
        return false;
    }
    const uint64_t bytes = static_cast<uint64_t>(count) *
        job.result.targetIdentity.logicalSectorSize;
    if (bytes == 0 || bytes > bufferBytes ||
        (job.maxTransferBytes != 0 && bytes > job.maxTransferBytes)) {
        terminal = finish_job(job, FAT32_INTEGRITY_INCOMPLETE,
            FAT32_INTEGRITY_REASON_INTERNAL_BOUNDS_FAILURE,
            "A metadata request exceeded its fixed transfer bound.");
        return false;
    }
    DeviceCapabilities capabilities = {};
    const Fat32IntegrityReason liveReason =
        validate_live_target(job, capabilities);
    if (liveReason != FAT32_INTEGRITY_REASON_NONE) {
        const Fat32IntegrityState state =
            liveReason == FAT32_INTEGRITY_REASON_DEVICE_REMOVED ||
            liveReason == FAT32_INTEGRITY_REASON_REGISTRATION_INVALIDATED ||
            liveReason == FAT32_INTEGRITY_REASON_TARGET_IDENTITY_CHANGED
            ? FAT32_INTEGRITY_TARGET_INVALIDATED
            : FAT32_INTEGRITY_INCOMPLETE;
        terminal = finish_job(job, state, liveReason,
            fat32_integrity_reason_name(liveReason));
        return false;
    }
    ++job.result.metadataReadRequests;
    const block::Status status = read_sectors_safe(
        job.result.targetIdentity.globalIndex, absoluteLba, count, buffer,
        bufferBytes);
    if (status != block::BLOCK_OK) {
        terminal = fail_read(job, status, absoluteLba, count, structure);
        return false;
    }
    job.result.bytesRead += bytes;

    const RevalidationStatus identity =
        revalidate_target_identity(job.result.targetIdentity);
    if (identity != TARGET_VALID) {
        const Fat32IntegrityReason reason = map_identity_failure(
            job.result.targetIdentity, identity);
        terminal = finish_job(job, FAT32_INTEGRITY_TARGET_INVALIDATED,
            reason, fat32_integrity_reason_name(reason));
        return false;
    }
    return true;
}

static void record_bpb_issue(Fat32IntegrityJob& job, uint32_t offset,
                             uint64_t expected, uint64_t observed,
                             bool valuesValid = true)
{
    record_finding(job, FAT32_INTEGRITY_FINDING_MALFORMED_BPB,
        FAT32_INTEGRITY_STRUCTURE_PRIMARY_BOOT,
        FAT32_INTEGRITY_STRUCTURAL_INCONSISTENCY,
        job.result.partitionStartLba, 1, offset, expected, observed,
        valuesValid);
}

static bool parse_primary_geometry(Fat32IntegrityJob& job)
{
    const uint8_t* boot = s_primaryBoot;
    const uint32_t deviceSectorSize =
        job.result.targetIdentity.logicalSectorSize;
    bool valid = true;
    const uint16_t bytesPerSector = read_u16(boot + 11);
    const uint32_t sectorsPerCluster = boot[13];
    const uint32_t reserved = read_u16(boot + 14);
    const uint32_t fats = boot[16];
    const uint32_t rootEntries = read_u16(boot + 17);
    const uint32_t total16 = read_u16(boot + 19);
    const uint32_t media = boot[21];
    const uint32_t fat16 = read_u16(boot + 22);
    const uint32_t total = read_u32(boot + 32);
    const uint32_t fatSize = read_u32(boot + 36);
    const uint32_t extFlags = read_u16(boot + 40);
    const uint32_t fsVersion = read_u16(boot + 42);
    const uint32_t rootClusterRaw = read_u32(boot + 44);
    const uint32_t fsInfoSector = read_u16(boot + 48);
    const uint32_t backupBoot = read_u16(boot + 50);
    const uint32_t extendedSignature = boot[66];

    if (boot[510] != 0x55 || boot[511] != 0xAA)
        record_finding(job, FAT32_INTEGRITY_FINDING_BOOT_SIGNATURE,
            FAT32_INTEGRITY_STRUCTURE_PRIMARY_BOOT,
            FAT32_INTEGRITY_STRUCTURAL_INCONSISTENCY,
            job.result.partitionStartLba, 1, 510, 0xAA55u,
            static_cast<uint16_t>(boot[510]) |
                (static_cast<uint16_t>(boot[511]) << 8), true);

    if (!((boot[0] == 0xEB && boot[2] == 0x90) || boot[0] == 0xE9)) {
        record_bpb_issue(job, 0, 0xEBu, boot[0]);
        valid = false;
    }
    if (bytesPerSector != deviceSectorSize) {
        record_bpb_issue(job, 11, deviceSectorSize, bytesPerSector);
        valid = false;
    }
    if (sectorsPerCluster == 0 ||
        (sectorsPerCluster & (sectorsPerCluster - 1u)) != 0 ||
        static_cast<uint64_t>(sectorsPerCluster) * deviceSectorSize >
            kFat32MaximumClusterBytes) {
        record_bpb_issue(job, 13, 1, sectorsPerCluster);
        valid = false;
    }
    if (reserved == 0) {
        record_bpb_issue(job, 14, 1, reserved);
        valid = false;
    }
    if (fats == 0 || fats > 2) {
        record_bpb_issue(job, 16, 2, fats);
        valid = false;
    }
    if (rootEntries != 0) {
        record_bpb_issue(job, 17, 0, rootEntries);
        valid = false;
    }
    if (total16 != 0 || total == 0) {
        record_bpb_issue(job, 19, 0, total16);
        valid = false;
    }
    if (media < 0xF0u) {
        record_bpb_issue(job, 21, 0xF0u, media);
        valid = false;
    }
    if (fat16 != 0 || fatSize == 0) {
        record_bpb_issue(job, 22, 0, fat16);
        valid = false;
    }
    if ((extFlags & 0xFF70u) != 0 || fsVersion != 0) {
        record_bpb_issue(job, 40, 0, extFlags);
        valid = false;
    }
    if (extendedSignature != 0x28 && extendedSignature != 0x29) {
        record_bpb_issue(job, 66, 0x29, extendedSignature);
        valid = false;
    }
    if (total > job.result.partitionSectorCount) {
        record_bpb_issue(job, 32, job.result.partitionSectorCount, total);
        valid = false;
    }
    if (fsInfoSector == 0 || fsInfoSector == 0xFFFFu ||
        fsInfoSector >= reserved || fsInfoSector >= total) {
        record_bpb_issue(job, 48, reserved > 1 ? 1 : 0,
                         fsInfoSector);
        valid = false;
    }
    if (backupBoot == 0 || backupBoot == 0xFFFFu ||
        backupBoot >= reserved || backupBoot >= total ||
        backupBoot == fsInfoSector) {
        record_bpb_issue(job, 50, reserved > 1 ? 1 : 0, backupBoot);
        valid = false;
    }

    uint64_t fatRegionSectors = 0;
    uint64_t firstData = 0;
    uint64_t fatBytes = 0;
    uint64_t rootClusterOffset = 0;
    uint64_t rootSector = 0;
    uint64_t rootEnd = 0;
    uint64_t volumeEnd = 0;
    const uint32_t rootCluster = rootClusterRaw & 0x0FFFFFFFu;
    const uint32_t activeFat = extFlags & 0x000Fu;
    const bool mirroring = (extFlags & 0x0080u) == 0;
    if (fats != 0 && fatSize != 0 && reserved != 0 && total != 0) {
        if (!mul_u64(fats, fatSize, fatRegionSectors) ||
            !add_u64(reserved, fatRegionSectors, firstData) ||
            firstData >= total ||
            !mul_u64(fatSize, deviceSectorSize, fatBytes)) {
            record_bpb_issue(job, 36, 0, fatSize, false);
            valid = false;
        }
    }

    uint64_t clusterCount64 = 0;
    if (firstData < total && sectorsPerCluster != 0)
        clusterCount64 = (total - firstData) / sectorsPerCluster;
    if (clusterCount64 < kFat32MinimumClusters ||
        clusterCount64 > kFat32MaximumClusters) {
        record_bpb_issue(job, 13, kFat32MinimumClusters,
                         clusterCount64);
        valid = false;
    }
    if (fatBytes / 4u < clusterCount64 + 2u) {
        record_bpb_issue(job, 36, clusterCount64 + 2u,
                         fatBytes / 4u);
        valid = false;
    }
    if ((rootClusterRaw & 0xF0000000u) != 0 || rootCluster < 2 ||
        clusterCount64 < 1 ||
        static_cast<uint64_t>(rootCluster) >= clusterCount64 + 2u) {
        record_finding(job, FAT32_INTEGRITY_FINDING_MALFORMED_BPB,
            FAT32_INTEGRITY_STRUCTURE_DATA_REGION,
            FAT32_INTEGRITY_STRUCTURAL_INCONSISTENCY,
            job.result.partitionStartLba, 1, 44, clusterCount64 + 1u,
            rootClusterRaw, true);
        valid = false;
    }
    if (!mirroring && activeFat >= fats) {
        record_bpb_issue(job, 40, fats ? fats - 1u : 0u, activeFat);
        valid = false;
    }
    if (total != 0 &&
        (!add_u64(job.result.partitionStartLba, total, volumeEnd) ||
         volumeEnd > job.result.targetIdentity.totalLogicalSectors)) {
        record_bpb_issue(job, 32,
            job.result.targetIdentity.totalLogicalSectors -
                job.result.partitionStartLba,
            total);
        valid = false;
    }
    if (valid &&
        (!mul_u64(static_cast<uint64_t>(rootCluster - 2u),
                  sectorsPerCluster, rootClusterOffset) ||
         !add_u64(firstData, rootClusterOffset, rootSector) ||
         !add_u64(rootSector, sectorsPerCluster, rootEnd) ||
         rootEnd > total ||
         !checked_lba_range(job.result.partitionSectorCount, rootSector,
                            sectorsPerCluster))) {
        record_finding(job, FAT32_INTEGRITY_FINDING_MALFORMED_BPB,
            FAT32_INTEGRITY_STRUCTURE_DATA_REGION,
            FAT32_INTEGRITY_STRUCTURAL_INCONSISTENCY,
            job.result.partitionStartLba, 1, 44, total,
            rootClusterRaw, true);
        valid = false;
    }

    if (!valid) return false;
    Fat32IntegrityGeometry& geometry = job.result.geometry;
    geometry.bytesPerSector = bytesPerSector;
    geometry.sectorsPerCluster = sectorsPerCluster;
    geometry.reservedSectorCount = reserved;
    geometry.fatCount = fats;
    geometry.fatSizeSectors = fatSize;
    geometry.totalSectors = total;
    geometry.rootCluster = rootCluster;
    geometry.clusterCount = static_cast<uint32_t>(clusterCount64);
    geometry.fsInfoSector = fsInfoSector;
    geometry.backupBootSector = backupBoot;
    geometry.extFlags = extFlags;
    geometry.activeFat = mirroring ? 0u : activeFat;
    geometry.firstFatSector = reserved;
    geometry.firstDataSector = firstData;
    geometry.dataSectorCount = total - firstData;
    geometry.fatBytes = fatBytes;
    geometry.rootDirectorySector = rootSector;
    geometry.fatMirroringEnabled = mirroring;

    uint64_t backupRelativeEnd = 0;
    if (!add_u64(backupBoot, 1, backupRelativeEnd) ||
        backupRelativeEnd > total ||
        !checked_lba_range(job.result.partitionSectorCount, backupBoot, 1)) {
        record_bpb_issue(job, 50, total, backupBoot);
        return false;
    }
    return true;
}

static bool compare_backup_boot(Fat32IntegrityJob& job,
                                const uint8_t* backup)
{
    // BPB/extended BPB bytes and the standard boot signature are relevant;
    // executable boot code and OEM padding may legitimately differ.
    uint32_t mismatch = UINT32_MAX;
    for (uint32_t i = 11; i < 90; ++i) {
        if (s_primaryBoot[i] != backup[i]) {
            mismatch = i;
            break;
        }
    }
    if (mismatch == UINT32_MAX &&
        (s_primaryBoot[510] != backup[510] ||
         s_primaryBoot[511] != backup[511]))
        mismatch = s_primaryBoot[510] != backup[510] ? 510u : 511u;
    if (mismatch == UINT32_MAX) return true;
    record_finding(job, FAT32_INTEGRITY_FINDING_BACKUP_BOOT_MISMATCH,
        FAT32_INTEGRITY_STRUCTURE_BACKUP_BOOT,
        FAT32_INTEGRITY_STRUCTURAL_INCONSISTENCY,
        job.result.partitionStartLba + job.result.geometry.backupBootSector,
        1, mismatch, s_primaryBoot[mismatch], backup[mismatch], true);
    return false;
}

static bool validate_fsinfo(Fat32IntegrityJob& job)
{
    const uint32_t signatures[3] = {
        read_u32(s_sector), read_u32(s_sector + 484),
        read_u32(s_sector + 508)
    };
    const uint32_t expected[3] = {
        0x41615252u, 0x61417272u, 0xAA550000u
    };
    const uint32_t offsets[3] = {0, 484, 508};
    bool signaturesValid = true;
    for (uint8_t i = 0; i < 3; ++i) {
        if (signatures[i] == expected[i]) continue;
        signaturesValid = false;
        record_finding(job, FAT32_INTEGRITY_FINDING_FSINFO_SIGNATURE,
            FAT32_INTEGRITY_STRUCTURE_FSINFO,
            FAT32_INTEGRITY_STRUCTURAL_INCONSISTENCY,
            job.result.partitionStartLba +
                job.result.geometry.fsInfoSector,
            1, offsets[i], expected[i], signatures[i], true);
    }
    if (!signaturesValid) return false;

    const uint32_t freeCount = read_u32(s_sector + 488);
    const uint32_t nextFree = read_u32(s_sector + 492);
    if (freeCount != UINT32_MAX &&
        freeCount > job.result.geometry.clusterCount) {
        record_finding(job, FAT32_INTEGRITY_FINDING_FSINFO_HINT_UNVERIFIED,
            FAT32_INTEGRITY_STRUCTURE_FSINFO, FAT32_INTEGRITY_ADVISORY,
            job.result.partitionStartLba +
                job.result.geometry.fsInfoSector,
            1, 488, job.result.geometry.clusterCount, freeCount, true);
    }
    const uint64_t lastCluster =
        static_cast<uint64_t>(job.result.geometry.clusterCount) + 1u;
    if (nextFree != UINT32_MAX &&
        (nextFree < 2 || static_cast<uint64_t>(nextFree) > lastCluster)) {
        record_finding(job, FAT32_INTEGRITY_FINDING_FSINFO_HINT_UNVERIFIED,
            FAT32_INTEGRITY_STRUCTURE_FSINFO, FAT32_INTEGRITY_ADVISORY,
            job.result.partitionStartLba +
                job.result.geometry.fsInfoSector,
            1, 492, lastCluster, nextFree, true);
    }
    return true;
}

static bool fat_reserved_entries_plausible(Fat32IntegrityJob& job,
                                           uint8_t fatIndex)
{
    const uint32_t media = s_primaryBoot[21];
    const uint32_t entry0 = read_u32(s_sector);
    const uint32_t entry1 = read_u32(s_sector + 4);
    const bool entry0Valid =
        (entry0 & 0x0FFFFF00u) == 0x0FFFFF00u &&
        (entry0 & 0xFFu) == media;
    const bool entry1Valid =
        (entry1 & 0x0FFFFFFFu) >= 0x0FFFFFF8u;
    const uint64_t fatLba = job.result.partitionStartLba +
        job.result.geometry.firstFatSector +
        static_cast<uint64_t>(fatIndex) *
            job.result.geometry.fatSizeSectors;
    if (!entry0Valid)
        record_finding(job, FAT32_INTEGRITY_FINDING_FAT_RESERVED_ENTRY,
            FAT32_INTEGRITY_STRUCTURE_FAT,
            FAT32_INTEGRITY_STRUCTURAL_INCONSISTENCY,
            fatLba, 1, 0, 0x0FFFFF00u | media, entry0, true);
    if (!entry1Valid)
        record_finding(job, FAT32_INTEGRITY_FINDING_FAT_RESERVED_ENTRY,
            FAT32_INTEGRITY_STRUCTURE_FAT,
            FAT32_INTEGRITY_STRUCTURAL_INCONSISTENCY,
            fatLba, 1, 4, 0x0FFFFFF8u, entry1 & 0x0FFFFFFFu, true);
    return entry0Valid && entry1Valid;
}

static bool prepare_compare_chunk(Fat32IntegrityJob& job)
{
    const uint64_t remaining = job.result.geometry.fatBytes -
        job.compareByteOffset;
    uint64_t limit = FAT32_INTEGRITY_COMPARE_BUFFER_BYTES;
    if (job.maxTransferBytes != 0 && limit > job.maxTransferBytes)
        limit = job.maxTransferBytes;
    const uint32_t sectorSize = job.result.geometry.bytesPerSector;
    limit -= limit % sectorSize;
    if (limit == 0) return false;
    uint64_t bytes = remaining < limit ? remaining : limit;
    bytes -= bytes % sectorSize;
    if (bytes == 0 || bytes > UINT32_MAX) return false;
    job.compareChunkBytes = static_cast<uint32_t>(bytes);
    job.compareChunkSectors = job.compareChunkBytes / sectorSize;
    return job.compareChunkSectors != 0;
}

static bool read_fat_chunk(Fat32IntegrityJob& job, uint8_t fatIndex,
                           uint8_t* buffer, size_t capacity,
                           Fat32IntegrityState& terminal)
{
    uint64_t fatOffset = 0;
    uint64_t fatBase = 0;
    uint64_t relative = 0;
    if (!mul_u64(fatIndex, job.result.geometry.fatSizeSectors, fatOffset) ||
        !add_u64(job.result.geometry.firstFatSector, fatOffset, fatBase) ||
        !add_u64(fatBase, job.compareByteOffset /
            job.result.geometry.bytesPerSector, relative)) {
        terminal = finish_job(job, FAT32_INTEGRITY_INCOMPLETE,
            FAT32_INTEGRITY_REASON_INTERNAL_BOUNDS_FAILURE,
            "FAT comparison range arithmetic overflowed.");
        return false;
    }
    return read_partition_range(job, relative, job.compareChunkSectors,
        buffer, capacity, FAT32_INTEGRITY_STRUCTURE_FAT, terminal);
}

static void compare_fat_chunks(Fat32IntegrityJob& job)
{
    uint32_t mismatchOffset = UINT32_MAX;
    uint32_t mismatchCount = 0;
    for (uint32_t i = 0; i < job.compareChunkBytes; ++i) {
        if (s_compareReference[i] == s_compareOther[i]) continue;
        if (mismatchOffset == UINT32_MAX) mismatchOffset = i;
        ++mismatchCount;
    }
    if (mismatchOffset != UINT32_MAX) {
        const uint64_t relativeLba = job.result.geometry.firstFatSector +
            static_cast<uint64_t>(job.compareReferenceFat) *
                job.result.geometry.fatSizeSectors +
            job.compareByteOffset / job.result.geometry.bytesPerSector +
            mismatchOffset / job.result.geometry.bytesPerSector;
        const Fat32IntegrityFindingSeverity severity =
            job.result.geometry.fatMirroringEnabled
            ? FAT32_INTEGRITY_STRUCTURAL_INCONSISTENCY
            : FAT32_INTEGRITY_ADVISORY;
        record_finding(job, FAT32_INTEGRITY_FINDING_FAT_COPY_MISMATCH,
            FAT32_INTEGRITY_STRUCTURE_FAT, severity,
            job.result.partitionStartLba + relativeLba, 1,
            mismatchOffset % job.result.geometry.bytesPerSector,
            s_compareReference[mismatchOffset], s_compareOther[mismatchOffset],
            true);
        if (mismatchCount > 1)
            job.result.additionalFindingsMayExist = true;
    }
    job.compareByteOffset += job.compareChunkBytes;
    job.result.fatBytesCompared += job.compareChunkBytes;
    update_progress(job.result);
}

} // namespace

bool capture_fat32_integrity_request(
    const TargetIdentity& target, const PartitionTableModel& table,
    const PartitionEntry& partition, Fat32IntegrityRequest& request)
{
    request = {};
    if (target.registrationId == 0 || target.registryGeneration == 0 ||
        target.registryGeneration == UINT64_MAX ||
        !valid_table_snapshot(table, partition)) return false;
    Fat32IntegrityRequest captured = {};
    captured.targetSnapshot = target;
    captured.partitionScheme = table.scheme;
    captured.partitionSnapshot = partition;
    captured.expectedRegistryGeneration = target.registryGeneration;
    if (table.scheme == PARTITION_SCHEME_GPT) {
        for (uint8_t i = 0; i < 16; ++i)
            captured.gptDiskGuid[i] = table.primaryDiskGuid[i];
    } else {
        captured.mbrDiskSignature = table.mbrDiskSignature;
    }
    if (!valid_request_shape(captured)) return false;
    request = captured;
    return true;
}

Fat32IntegrityState begin_fat32_integrity_job(
    Fat32IntegrityJob& job, const Fat32IntegrityRequest& request)
{
    if (job.result.state == FAT32_INTEGRITY_RUNNING)
        return job.result.state;
    job = {};
    job.request = request;
    job.result.state = FAT32_INTEGRITY_NOT_ADMITTED;
    job.result.targetIdentity = request.targetSnapshot;
    job.result.partitionScheme = request.partitionScheme;
    job.result.partition = request.partitionSnapshot;
    job.result.partitionStartLba = request.partitionSnapshot.startLba;
    job.result.partitionSectorCount = request.partitionSnapshot.sectorCount;
    job.result.blockStatus = block::BLOCK_ERR_INVALID;
    job.result.failedLba = UINT64_MAX;
    job.step = FAT32_INTEGRITY_STEP_VALIDATE_CAPTURE;

    if (!valid_request_shape(request)) {
        job.result.reason = FAT32_INTEGRITY_REASON_INVALID_REQUEST;
        set_diagnostic(job.result,
            fat32_integrity_reason_name(job.result.reason));
        return job.result.state;
    }
    if (request.targetSnapshot.registryGeneration !=
        block::registry_generation()) {
        job.result.reason =
            FAT32_INTEGRITY_REASON_REGISTRATION_INVALIDATED;
        set_diagnostic(job.result,
            fat32_integrity_reason_name(job.result.reason));
        return job.result.state;
    }
    if (!fat32_format_sector_size_supported(
            request.targetSnapshot.logicalSectorSize)) {
        job.result.reason =
            FAT32_INTEGRITY_REASON_UNSUPPORTED_SECTOR_SIZE;
        set_diagnostic(job.result,
            fat32_integrity_reason_name(job.result.reason));
        return job.result.state;
    }
    DeviceCapabilities capabilities = {};
    if (!query_device_capabilities(request.targetSnapshot.globalIndex,
                                   capabilities) ||
        !capabilities.geometryValid || !capabilities.capacityValid ||
        !capabilities.readable ||
        capabilities.totalLogicalSectors !=
            request.targetSnapshot.totalLogicalSectors ||
        capabilities.logicalSectorSize !=
            request.targetSnapshot.logicalSectorSize ||
        !valid_geometry(capabilities.totalLogicalSectors,
                        capabilities.logicalSectorSize)) {
        job.result.reason = FAT32_INTEGRITY_REASON_READ_UNAVAILABLE;
        set_diagnostic(job.result,
            fat32_integrity_reason_name(job.result.reason));
        return job.result.state;
    }

    const StorageOperationLockStatus lock =
        try_acquire_storage_operation(job.lease);
    if (lock != STORAGE_OPERATION_LOCK_ACQUIRED) {
        job.result.reason = lock == STORAGE_OPERATION_LOCK_BUSY
            ? FAT32_INTEGRITY_REASON_OPERATION_BUSY
            : FAT32_INTEGRITY_REASON_OWNERSHIP_INVALID;
        set_diagnostic(job.result,
            fat32_integrity_reason_name(job.result.reason));
        return job.result.state;
    }
    job.result.ownershipAcquired = true;
    job.result.operationId = job.lease.ownerToken;
    if (!pin_storage_operation_target(job.lease,
                                      request.targetSnapshot)) {
        const RevalidationStatus identity =
            revalidate_target_identity(request.targetSnapshot);
        const Fat32IntegrityReason reason = identity == TARGET_VALID
            ? FAT32_INTEGRITY_REASON_OWNERSHIP_INVALID
            : map_identity_failure(request.targetSnapshot, identity);
        return finish_job(job, FAT32_INTEGRITY_NOT_ADMITTED, reason,
            "The exact selected device registration could not be pinned.");
    }
    const Fat32IntegrityReason liveReason =
        validate_live_target(job, capabilities);
    if (liveReason != FAT32_INTEGRITY_REASON_NONE) {
        const Fat32IntegrityState state =
            liveReason == FAT32_INTEGRITY_REASON_DEVICE_REMOVED ||
            liveReason == FAT32_INTEGRITY_REASON_REGISTRATION_INVALIDATED ||
            liveReason == FAT32_INTEGRITY_REASON_TARGET_IDENTITY_CHANGED
            ? FAT32_INTEGRITY_TARGET_INVALIDATED
            : FAT32_INTEGRITY_INCOMPLETE;
        return finish_job(job, state, liveReason,
            fat32_integrity_reason_name(liveReason));
    }
    if (target_has_active_mount(request.targetSnapshot))
        return finish_job(job, FAT32_INTEGRITY_NOT_ADMITTED,
            FAT32_INTEGRITY_REASON_MOUNTED_TARGET,
            "The selected device has an active VFS mount and is not a stable verification target.");

    job.maxTransferBytes = capabilities.maxTransferBytes;
    if (job.maxTransferBytes > FAT32_INTEGRITY_COMPARE_BUFFER_BYTES)
        job.maxTransferBytes = FAT32_INTEGRITY_COMPARE_BUFFER_BYTES;
    job.result.state = FAT32_INTEGRITY_RUNNING;
    job.result.reason = FAT32_INTEGRITY_REASON_NONE;
    set_diagnostic(job.result,
        "Read-only FAT32 metadata verification admitted; no filesystem-sector reads have completed.");
    update_progress(job.result);
    return job.result.state;
}

Fat32IntegrityState step_fat32_integrity_job(Fat32IntegrityJob& job)
{
    if (job.result.state != FAT32_INTEGRITY_RUNNING || job.stepActive)
        return job.result.state;
    job.stepActive = true;
    if (job.result.stopRequested)
        return finish_job(job, FAT32_INTEGRITY_STOPPED,
            FAT32_INTEGRITY_REASON_STOP_REQUESTED,
            "Verification stopped; remaining metadata is unverified.");

    DeviceCapabilities capabilities = {};
    const Fat32IntegrityReason liveReason =
        validate_live_target(job, capabilities);
    if (liveReason != FAT32_INTEGRITY_REASON_NONE) {
        const Fat32IntegrityState state =
            liveReason == FAT32_INTEGRITY_REASON_DEVICE_REMOVED ||
            liveReason == FAT32_INTEGRITY_REASON_REGISTRATION_INVALIDATED ||
            liveReason == FAT32_INTEGRITY_REASON_TARGET_IDENTITY_CHANGED
            ? FAT32_INTEGRITY_TARGET_INVALIDATED
            : FAT32_INTEGRITY_INCOMPLETE;
        return finish_job(job, state, liveReason,
            fat32_integrity_reason_name(liveReason));
    }

    Fat32IntegrityState terminal = FAT32_INTEGRITY_RUNNING;
    switch (job.step) {
        case FAT32_INTEGRITY_STEP_VALIDATE_CAPTURE:
            // The immutable request is the captured partition-table identity.
            // Re-resolving the table would read outside the selected partition.
            job.step = FAT32_INTEGRITY_STEP_PRIMARY_BOOT;
            job.stepActive = false;
            return job.result.state;

        case FAT32_INTEGRITY_STEP_PRIMARY_BOOT:
            if (!read_partition_range(job, 0, 1, s_primaryBoot,
                    sizeof(s_primaryBoot),
                    FAT32_INTEGRITY_STRUCTURE_PRIMARY_BOOT, terminal))
                return terminal;
            if (!parse_primary_geometry(job))
                return finish_job(job, FAT32_INTEGRITY_INVALID_GEOMETRY,
                    FAT32_INTEGRITY_REASON_INVALID_GEOMETRY,
                    "The primary BPB is malformed or its checked FAT32 geometry is unsupported.");
            job.result.fatBytesToCompare =
                job.result.geometry.fatCount > 1
                ? job.result.geometry.fatBytes : 0;
            job.compareReferenceFat =
                static_cast<uint8_t>(job.result.geometry.activeFat);
            job.compareOtherFat = job.compareReferenceFat == 0 ? 1 : 0;
            job.step = FAT32_INTEGRITY_STEP_BACKUP_BOOT;
            break;

        case FAT32_INTEGRITY_STEP_BACKUP_BOOT:
            if (!read_partition_range(job,
                    job.result.geometry.backupBootSector, 1, s_sector,
                    sizeof(s_sector), FAT32_INTEGRITY_STRUCTURE_BACKUP_BOOT,
                    terminal)) return terminal;
            (void)compare_backup_boot(job, s_sector);
            job.step = FAT32_INTEGRITY_STEP_FSINFO;
            break;

        case FAT32_INTEGRITY_STEP_FSINFO:
            if (!read_partition_range(job,
                    job.result.geometry.fsInfoSector, 1, s_sector,
                    sizeof(s_sector), FAT32_INTEGRITY_STRUCTURE_FSINFO,
                    terminal)) return terminal;
            (void)validate_fsinfo(job);
            job.reservedFatIndex = 0;
            job.step = FAT32_INTEGRITY_STEP_FAT_RESERVED;
            break;

        case FAT32_INTEGRITY_STEP_FAT_RESERVED: {
            uint64_t fatOffset = 0;
            uint64_t relative = 0;
            if (!mul_u64(job.reservedFatIndex,
                         job.result.geometry.fatSizeSectors, fatOffset) ||
                !add_u64(job.result.geometry.firstFatSector, fatOffset,
                         relative))
                return finish_job(job, FAT32_INTEGRITY_INCOMPLETE,
                    FAT32_INTEGRITY_REASON_INTERNAL_BOUNDS_FAILURE,
                    "FAT reserved-entry range arithmetic overflowed.");
            if (!read_partition_range(job, relative, 1, s_sector,
                    sizeof(s_sector), FAT32_INTEGRITY_STRUCTURE_FAT,
                    terminal)) return terminal;
            (void)fat_reserved_entries_plausible(job,
                job.reservedFatIndex);
            ++job.reservedFatIndex;
            if (job.reservedFatIndex >= job.result.geometry.fatCount) {
                if (job.result.geometry.fatCount > 1 &&
                    !prepare_compare_chunk(job))
                    return finish_job(job, FAT32_INTEGRITY_INCOMPLETE,
                        FAT32_INTEGRITY_REASON_INTERNAL_BOUNDS_FAILURE,
                        "The bounded FAT comparison could not form a sector-aligned chunk.");
                job.step = job.result.geometry.fatCount > 1
                    ? FAT32_INTEGRITY_STEP_FAT_COMPARE_REFERENCE
                    : FAT32_INTEGRITY_STEP_FINISHED;
            }
            break;
        }

        case FAT32_INTEGRITY_STEP_FAT_COMPARE_REFERENCE:
            if (!prepare_compare_chunk(job))
                return finish_job(job, FAT32_INTEGRITY_INCOMPLETE,
                    FAT32_INTEGRITY_REASON_INTERNAL_BOUNDS_FAILURE,
                    "The bounded FAT comparison could not form a sector-aligned chunk.");
            if (!read_fat_chunk(job, job.compareReferenceFat,
                    s_compareReference, sizeof(s_compareReference), terminal))
                return terminal;
            job.step = FAT32_INTEGRITY_STEP_FAT_COMPARE_OTHER;
            break;

        case FAT32_INTEGRITY_STEP_FAT_COMPARE_OTHER:
            if (!read_fat_chunk(job, job.compareOtherFat,
                    s_compareOther, sizeof(s_compareOther), terminal))
                return terminal;
            compare_fat_chunks(job);
            if (job.compareByteOffset >= job.result.geometry.fatBytes)
                job.step = FAT32_INTEGRITY_STEP_FINISHED;
            else
                job.step = FAT32_INTEGRITY_STEP_FAT_COMPARE_REFERENCE;
            break;

        case FAT32_INTEGRITY_STEP_FINISHED:
            return finish_job(job,
                job.result.structuralInconsistencyFound
                    ? FAT32_INTEGRITY_INCONSISTENCY
                    : FAT32_INTEGRITY_PASSED,
                FAT32_INTEGRITY_REASON_NONE,
                job.result.structuralInconsistencyFound
                    ? "Metadata verification completed with structural findings; no repair was attempted."
                    : "The bounded FAT32 metadata checks passed; directory and cluster-chain checks were not performed.");

        default:
            return finish_job(job, FAT32_INTEGRITY_INCOMPLETE,
                FAT32_INTEGRITY_REASON_INTERNAL_BOUNDS_FAILURE,
                "The verifier entered an unknown metadata-check state.");
    }
    job.stepActive = false;
    update_progress(job.result);
    return job.result.state;
}

bool request_fat32_integrity_stop(Fat32IntegrityJob& job)
{
    if (job.result.state != FAT32_INTEGRITY_RUNNING) return false;
    job.result.stopRequested = true;
    return true;
}

bool fat32_integrity_cluster_sector(
    const Fat32IntegrityGeometry& geometry, uint32_t cluster,
    uint64_t& volumeRelativeSector)
{
    volumeRelativeSector = 0;
    if (geometry.bytesPerSector == 0 || geometry.sectorsPerCluster == 0 ||
        geometry.firstDataSector >= geometry.totalSectors || cluster < 2 ||
        static_cast<uint64_t>(cluster) >=
            static_cast<uint64_t>(geometry.clusterCount) + 2u)
        return false;
    uint64_t clusterOffset = 0;
    uint64_t clusterSector = 0;
    uint64_t clusterEnd = 0;
    if (!mul_u64(static_cast<uint64_t>(cluster - 2u),
                 geometry.sectorsPerCluster, clusterOffset) ||
        !add_u64(geometry.firstDataSector, clusterOffset, clusterSector) ||
        !add_u64(clusterSector, geometry.sectorsPerCluster, clusterEnd) ||
        clusterEnd > geometry.totalSectors) return false;
    volumeRelativeSector = clusterSector;
    return true;
}

const char* fat32_integrity_state_name(Fat32IntegrityState state)
{
    switch (state) {
        case FAT32_INTEGRITY_NOT_ADMITTED: return "Not admitted";
        case FAT32_INTEGRITY_RUNNING: return "Running";
        case FAT32_INTEGRITY_PASSED: return "Metadata checks passed";
        case FAT32_INTEGRITY_INCONSISTENCY: return "Structural inconsistency detected";
        case FAT32_INTEGRITY_STOPPED: return "Stopped";
        case FAT32_INTEGRITY_READ_ERROR: return "Read error";
        case FAT32_INTEGRITY_INVALID_GEOMETRY: return "Unsupported or invalid geometry";
        case FAT32_INTEGRITY_TARGET_INVALIDATED: return "Target removed or invalidated";
        case FAT32_INTEGRITY_INCOMPLETE: return "Verification incomplete";
        default: return "Unknown";
    }
}

const char* fat32_integrity_reason_name(Fat32IntegrityReason reason)
{
    switch (reason) {
        case FAT32_INTEGRITY_REASON_NONE: return "No error";
        case FAT32_INTEGRITY_REASON_INVALID_REQUEST: return "Invalid selected partition request";
        case FAT32_INTEGRITY_REASON_OPERATION_BUSY: return "Another storage operation is active";
        case FAT32_INTEGRITY_REASON_MOUNTED_TARGET: return "Target device is mounted";
        case FAT32_INTEGRITY_REASON_OWNERSHIP_INVALID: return "Storage-operation ownership is invalid";
        case FAT32_INTEGRITY_REASON_DEVICE_REMOVED: return "Selected device was removed";
        case FAT32_INTEGRITY_REASON_REGISTRATION_INVALIDATED: return "Device registration was invalidated";
        case FAT32_INTEGRITY_REASON_TARGET_IDENTITY_CHANGED: return "Selected device identity changed";
        case FAT32_INTEGRITY_REASON_PARTITION_IDENTITY_INVALID: return "Partition-table identity is invalid";
        case FAT32_INTEGRITY_REASON_UNSUPPORTED_SECTOR_SIZE: return "Logical-sector size is unsupported";
        case FAT32_INTEGRITY_REASON_INVALID_GEOMETRY: return "FAT32 BPB geometry is invalid or unsupported";
        case FAT32_INTEGRITY_REASON_READ_UNAVAILABLE: return "Device does not provide validated reads";
        case FAT32_INTEGRITY_REASON_READ_FAILURE: return "A metadata read failed";
        case FAT32_INTEGRITY_REASON_STOP_REQUESTED: return "Stop requested";
        case FAT32_INTEGRITY_REASON_INTERNAL_BOUNDS_FAILURE: return "A checked metadata range was invalid";
        default: return "Unknown verifier reason";
    }
}

} // namespace storage
} // namespace kernel
