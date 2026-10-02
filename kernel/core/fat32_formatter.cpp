#include "include/kernel/fat32_formatter.h"
#include "include/kernel/block_device.h"
#if !defined(KERNEL_STORAGE_TEST)
#include "include/kernel/virtio_rng.h"
#endif

namespace kernel {
namespace storage {

namespace {

static const uint32_t kFat32ReservedSectors = 32;
static const uint32_t kFat32Count = 2;
static const uint32_t kFsInfoSector = 1;
static const uint32_t kBackupBootSector = 6;
static const uint32_t kBackupFsInfoSector = 7;
static const uint64_t kZeroScanBufferBytes = 64u * 1024u;

enum Fat32RollbackPriorState : uint8_t {
    FAT32_ROLLBACK_KNOWN_ZERO = 1,
};

struct Fat32RollbackRecord {
    uint32_t relativeSector;
    Fat32RollbackPriorState priorState;
    uint8_t reserved[3];
};
static_assert(sizeof(Fat32RollbackRecord) ==
              FAT32_FORMAT_ROLLBACK_RECORD_BYTES,
              "FAT32 rollback record size is part of the memory bound");

struct PartitionCheck {
    DeviceCapabilities capabilities;
    PartitionEntry currentPartition;
    Fat32FormatGeometry geometry;
};

alignas(4096) static uint8_t s_ioSector[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_zeroScanBuffer[kZeroScanBufferBytes];
static Fat32RollbackRecord s_rollbackRecords[FAT32_FORMAT_MAX_ROLLBACK_ENTRIES];
alignas(4096) static PartitionTableModel s_partitionTable;
static uint32_t s_rollbackRecordCount;

static void clear_bytes(void* destination, size_t bytes)
{
    uint8_t* output = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < bytes; ++i) output[i] = 0;
}

static void copy_bytes(void* destination, const void* source, size_t bytes)
{
    uint8_t* output = static_cast<uint8_t*>(destination);
    const uint8_t* input = static_cast<const uint8_t*>(source);
    for (size_t i = 0; i < bytes; ++i) output[i] = input[i];
}

static bool bytes_equal(const uint8_t* left, const uint8_t* right,
                        size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i)
        if (left[i] != right[i]) return false;
    return true;
}

static bool bytes_zero(const uint8_t* bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if (bytes[i] != 0) return false;
    return true;
}

static uint16_t read_u16(const uint8_t* input)
{
    return static_cast<uint16_t>(input[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(input[1]) << 8);
}

static uint32_t read_u32(const uint8_t* input)
{
    return static_cast<uint32_t>(input[0]) |
        (static_cast<uint32_t>(input[1]) << 8) |
        (static_cast<uint32_t>(input[2]) << 16) |
        (static_cast<uint32_t>(input[3]) << 24);
}

static void write_u16(uint8_t* output, uint16_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
}

static void write_u32(uint8_t* output, uint32_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
    output[2] = static_cast<uint8_t>(value >> 16);
    output[3] = static_cast<uint8_t>(value >> 24);
}

static void set_diagnostic(Fat32FormatResult& result, const char* message)
{
    size_t i = 0;
    if (message) {
        while (message[i] && i + 1 < sizeof(result.diagnostic)) {
            result.diagnostic[i] = message[i];
            ++i;
        }
    }
    result.diagnostic[i] = '\0';
}

static void reset_result(Fat32FormatResult& result)
{
    clear_bytes(&result, sizeof(result));
    result.status = FAT32_FORMAT_INVALID_REQUEST;
    result.failureStatus = FAT32_FORMAT_INVALID_REQUEST;
    result.stage = FAT32_FORMAT_STAGE_IDLE;
    result.existingState = FAT32_EXISTING_UNKNOWN;
    result.finalProbeState = FAT32_FINAL_PROBE_UNKNOWN;
    result.flushOutcome = block::FLUSH_OUTCOME_INVALID;
    result.flushStatus = block::BLOCK_ERR_INVALID;
    result.rollbackWriteStatus = block::BLOCK_ERR_INVALID;
    result.rollbackFlushOutcome = block::FLUSH_OUTCOME_INVALID;
    result.rollbackFlushStatus = block::BLOCK_ERR_INVALID;
    result.failedBeforeWrite = true;
}

static void mark_stage_failed(Fat32FormatResult& result)
{
    result.lastStage = result.stage;
    if (result.firstFailedStage == FAT32_FORMAT_STAGE_IDLE)
        result.firstFailedStage = result.stage;
    result.failureStatus = result.status;
    result.failedBeforeWrite = !result.writeMayHaveReachedMedia;
    result.stage = FAT32_FORMAT_STAGE_FAILED;
}

static bool capture_current_io_result(Fat32FormatResult& result)
{
    if (!block::last_operation_diagnostic(result.failedBlockDiagnostic))
        return false;
    result.blockStatusValid = true;
    result.blockStatus = result.failedBlockDiagnostic.status;
    result.failedOperation = result.failedBlockDiagnostic.operation;
    return true;
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

static bool nonzero_guid(const uint8_t guid[16])
{
    return guid && !bytes_zero(guid, 16);
}

static bool trusted_flush(const block::FlushReport& report)
{
    return report.status == block::BLOCK_OK && report.semanticsKnown &&
        (report.outcome == block::FLUSH_OUTCOME_SUPPORTED_SUCCEEDED ||
         report.outcome == block::FLUSH_OUTCOME_SYNCHRONOUS_DURABLE);
}

static bool target_range_valid(const TargetIdentity& target,
                               uint64_t startLba, uint64_t sectorCount)
{
    DeviceCapabilities caps;
    return query_device_capabilities(target.globalIndex, caps) &&
        caps.capacityValid && checked_lba_range(caps.totalLogicalSectors,
                                                 startLba, sectorCount);
}

static bool same_partition_extent(const PartitionEntry& left,
                                  const PartitionEntry& right)
{
    return left.isGpt == right.isGpt && left.startLba == right.startLba &&
        left.endLba == right.endLba && left.sectorCount == right.sectorCount;
}

static bool find_and_validate_partition(const Fat32FormatRequest& request,
                                        const DeviceCapabilities& caps,
                                        PartitionTableModel& table,
                                        PartitionEntry& current)
{
    if (!parse_partition_table(request.targetSnapshot.globalIndex, table))
        return false;
    if (request.partitionScheme == PARTITION_SCHEME_GPT) {
        if (table.state != DISK_STATE_VALID_GPT || !table.primaryGptValid ||
            !table.backupGptValid || !table.gptCopiesAgree ||
            !nonzero_guid(request.gptDiskGuid) ||
            !bytes_equal(request.gptDiskGuid, table.primaryDiskGuid, 16) ||
            !bytes_equal(table.primaryDiskGuid, table.backupDiskGuid, 16) ||
            !request.partitionSnapshot.isGpt ||
            !nonzero_guid(request.partitionSnapshot.uniqueGuid)) return false;
    } else if (request.partitionScheme == PARTITION_SCHEME_MBR) {
        if (table.state != DISK_STATE_VALID_MBR || table.hybridMbr ||
            table.extendedPartitionsPresent || table.protectiveMbr ||
            request.partitionSnapshot.isGpt ||
            table.mbrDiskSignature != request.mbrDiskSignature) return false;
    } else {
        return false;
    }

    uint16_t matches = 0;
    for (uint16_t i = 0; i < table.partitionCount; ++i) {
        const PartitionEntry& candidate = table.partitions[i];
        bool identityMatch = false;
        if (request.partitionScheme == PARTITION_SCHEME_GPT) {
            identityMatch = candidate.isGpt &&
                bytes_equal(candidate.uniqueGuid,
                            request.partitionSnapshot.uniqueGuid, 16);
            if (identityMatch && !bytes_equal(candidate.typeGuid,
                    request.partitionSnapshot.typeGuid, 16)) return false;
        } else {
            identityMatch = !candidate.isGpt &&
                candidate.partitionNumber ==
                    request.partitionSnapshot.partitionNumber &&
                candidate.mbrType == request.partitionSnapshot.mbrType &&
                candidate.startLba == request.partitionSnapshot.startLba &&
                candidate.sectorCount == request.partitionSnapshot.sectorCount;
        }
        if (!identityMatch) continue;
        ++matches;
        current = candidate;
    }
    if (matches == 0) return false;
    if (matches != 1 || !same_partition_extent(current,
            request.partitionSnapshot) || current.sectorCount == 0 ||
        current.endLba < current.startLba ||
        current.endLba - current.startLba != current.sectorCount - 1 ||
        !checked_lba_range(caps.totalLogicalSectors, current.startLba,
                           current.sectorCount)) return false;

    if (request.partitionScheme == PARTITION_SCHEME_GPT) {
        if (current.startLba < table.firstUsableLba ||
            current.endLba > table.lastUsableLba) return false;
    } else if (current.startLba == 0) {
        return false; // LBA 0 contains the MBR and is never format-authorized.
    }

    for (uint16_t i = 0; i < table.partitionCount; ++i) {
        const PartitionEntry& other = table.partitions[i];
        const bool sameIdentity = request.partitionScheme == PARTITION_SCHEME_GPT
            ? other.isGpt && bytes_equal(other.uniqueGuid,
                request.partitionSnapshot.uniqueGuid, 16)
            : !other.isGpt && other.partitionNumber ==
                request.partitionSnapshot.partitionNumber &&
              other.mbrType == request.partitionSnapshot.mbrType &&
              other.startLba == request.partitionSnapshot.startLba &&
              other.sectorCount == request.partitionSnapshot.sectorCount;
        if (sameIdentity) continue;
        if (current.startLba <= other.endLba && other.startLba <= current.endLba)
            return false;
    }
    return true;
}

static Fat32FormatStatus map_identity(RevalidationStatus status)
{
    switch (status) {
        case TARGET_REGISTRY_CHANGED: return FAT32_FORMAT_REGISTRY_CHANGED;
        case TARGET_DEVICE_MISSING: return FAT32_FORMAT_DEVICE_MISSING;
        case TARGET_IDENTITY_MISMATCH: return FAT32_FORMAT_IDENTITY_CHANGED;
        case TARGET_VALID: default: return FAT32_FORMAT_SUCCESS;
    }
}

static Fat32FormatStatus map_safety_issues(uint32_t issues)
{
    if (issues & SAFETY_ISSUE_REGISTRY_CHANGED)
        return FAT32_FORMAT_REGISTRY_CHANGED;
    if (issues & SAFETY_ISSUE_DEVICE_MISSING)
        return FAT32_FORMAT_DEVICE_MISSING;
    if (issues & SAFETY_ISSUE_IDENTITY_MISMATCH)
        return FAT32_FORMAT_IDENTITY_CHANGED;
    if (issues & SAFETY_ISSUE_UNKNOWN_GEOMETRY)
        return FAT32_FORMAT_INVALID_GEOMETRY;
    if (issues & SAFETY_ISSUE_UNREADABLE)
        return FAT32_FORMAT_READ_UNAVAILABLE;
    if (issues & SAFETY_ISSUE_READ_ONLY)
        return FAT32_FORMAT_READ_ONLY;
    if (issues & SAFETY_ISSUE_DURABILITY_UNKNOWN)
        return FAT32_FORMAT_DURABILITY_UNKNOWN;
    if (issues & SAFETY_ISSUE_ROOT_BACKING)
        return FAT32_FORMAT_ROOT_BACKING;
    if (issues & SAFETY_ISSUE_MOUNTED)
        return FAT32_FORMAT_MOUNTED;
    if (issues & SAFETY_ISSUE_BOOT_BACKING)
        return FAT32_FORMAT_BOOT_BACKING;
    if (issues & SAFETY_ISSUE_BOOT_IDENTITY_UNKNOWN)
        return FAT32_FORMAT_BOOT_IDENTITY_UNKNOWN;
    return FAT32_FORMAT_INVALID_TABLE;
}

static Fat32FormatStatus validate_request_and_partition(
    const Fat32FormatRequest& request, Fat32FormatResult& result,
    PartitionCheck& check)
{
    if (request.targetSnapshot.registrationId == 0 ||
        request.expectedRegistryGeneration == 0 ||
        request.expectedRegistryGeneration !=
            request.targetSnapshot.registryGeneration ||
        request.partitionSnapshot.partitionNumber == 0 ||
        request.partitionSnapshot.sectorCount == 0 ||
        (request.partitionScheme != PARTITION_SCHEME_GPT &&
         request.partitionScheme != PARTITION_SCHEME_MBR) ||
        request.partitionSnapshot.isGpt !=
            (request.partitionScheme == PARTITION_SCHEME_GPT))
        return FAT32_FORMAT_INVALID_REQUEST;

    char normalizedLabel[11];
    const Fat32FormatStatus labelStatus = normalize_fat32_volume_label(
        request.volumeLabel, normalizedLabel);
    if (labelStatus != FAT32_FORMAT_READY) return labelStatus;

    const RevalidationStatus identity =
        revalidate_target_identity(request.targetSnapshot);
    if (identity != TARGET_VALID) return map_identity(identity);

    DeviceCapabilities& caps = check.capabilities;
    if (!query_device_capabilities(request.targetSnapshot.globalIndex, caps))
        return FAT32_FORMAT_DEVICE_MISSING;
    if (!caps.geometryValid || !caps.capacityValid)
        return FAT32_FORMAT_INVALID_GEOMETRY;
    if (!caps.readable) return FAT32_FORMAT_READ_UNAVAILABLE;
    if (!caps.writable) return FAT32_FORMAT_READ_ONLY;
    if (caps.logicalSectorSize != FAT32_FORMAT_SECTOR_SIZE)
        return FAT32_FORMAT_UNSUPPORTED_SECTOR_SIZE;

    SafetyRequest safetyRequest = {};
    safetyRequest.requireWritable = true;
    safetyRequest.requireDurableWrites = true;
    safetyRequest.requireKnownPartitionState = true;
    safetyRequest.allowNotInitialized = false;
    SafetyValidation safety = {};
    if (!validate_destructive_target(request.targetSnapshot, safetyRequest,
                                     safety))
        return map_safety_issues(safety.issues);

    const BootProtection boot = query_boot_protection(request.targetSnapshot);
    if (boot.safety == BOOT_DEVICE_IS_TARGET)
        return FAT32_FORMAT_BOOT_BACKING;
    if (boot.safety != BOOT_DEVICE_DEFINITELY_NOT_TARGET)
        return FAT32_FORMAT_BOOT_IDENTITY_UNKNOWN;

    if (caps.persistence != PERSISTENCE_SYNCHRONOUS_DURABLE &&
        !(caps.persistence == PERSISTENCE_FLUSH_REQUIRED &&
          caps.flushSupported && caps.flushSemanticsKnown))
        return FAT32_FORMAT_DURABILITY_UNKNOWN;
    if (caps.persistence == PERSISTENCE_FLUSH_REQUIRED && !caps.flushSupported)
        return FAT32_FORMAT_FLUSH_UNAVAILABLE;

    if (revalidate_target_identity(request.targetSnapshot) != TARGET_VALID)
        return map_identity(revalidate_target_identity(request.targetSnapshot));
    if (!find_and_validate_partition(request, caps, s_partitionTable,
                                     check.currentPartition)) {
        // Differentiate disappearance from malformed or changed table state.
        if (!parse_partition_table(request.targetSnapshot.globalIndex,
                                   s_partitionTable) ||
            (s_partitionTable.state != DISK_STATE_VALID_GPT &&
             s_partitionTable.state != DISK_STATE_VALID_MBR))
            return FAT32_FORMAT_INVALID_TABLE;
        bool found = false;
        for (uint16_t i = 0; i < s_partitionTable.partitionCount; ++i) {
            const PartitionEntry& candidate = s_partitionTable.partitions[i];
            if (request.partitionScheme == PARTITION_SCHEME_GPT &&
                candidate.isGpt && bytes_equal(candidate.uniqueGuid,
                    request.partitionSnapshot.uniqueGuid, 16)) found = true;
            if (request.partitionScheme == PARTITION_SCHEME_MBR &&
                candidate.partitionNumber ==
                    request.partitionSnapshot.partitionNumber) found = true;
        }
        return found ? FAT32_FORMAT_PARTITION_IDENTITY_CHANGED
                     : FAT32_FORMAT_PARTITION_DISAPPEARED;
    }
    if (!target_range_valid(request.targetSnapshot,
            check.currentPartition.startLba,
            check.currentPartition.sectorCount))
        return FAT32_FORMAT_INVALID_GEOMETRY;

    const Fat32FormatStatus geometryStatus =
        calculate_fat32_format_geometry(check.currentPartition.startLba,
            check.currentPartition.sectorCount, caps.logicalSectorSize,
            check.geometry);
    if (geometryStatus != FAT32_FORMAT_READY) return geometryStatus;
    result.targetIdentity = request.targetSnapshot;
    result.partition = check.currentPartition;
    result.geometry = check.geometry;
    return FAT32_FORMAT_READY;
}

static bool ascii_eq(const uint8_t* bytes, const char* text, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if (bytes[i] != static_cast<uint8_t>(text[i])) return false;
    return true;
}

static Fat32ExistingState classify_existing_prefix(
    const Fat32FormatRequest& request, uint32_t sectorSize)
{
    const uint64_t start = request.partitionSnapshot.startLba;
    const uint64_t sectors = request.partitionSnapshot.sectorCount;
    const uint32_t probeCount = sectors < 4 ? static_cast<uint32_t>(sectors) : 4;
    if (probeCount == 0 || block::read_sectors_checked(
            request.targetSnapshot.globalIndex, start, probeCount, s_ioSector,
            sizeof(s_ioSector)) != block::BLOCK_OK)
        return FAT32_EXISTING_UNREADABLE;
    const uint8_t* boot = s_ioSector;
    if (ascii_eq(boot + 3, "EXFAT   ", 8))
        return FAT32_EXISTING_RECOGNIZED_FILESYSTEM;
    if (ascii_eq(boot + 3, "NTFS    ", 8))
        return FAT32_EXISTING_RECOGNIZED_FILESYSTEM;
    const uint16_t bps = read_u16(boot + 11);
    if (boot[510] == 0x55 && boot[511] == 0xAA && bps == sectorSize &&
        (ascii_eq(boot + 82, "FAT32   ", 8) ||
         ascii_eq(boot + 54, "FAT12   ", 8) ||
         ascii_eq(boot + 54, "FAT16   ", 8)))
        return FAT32_EXISTING_RECOGNIZED_FILESYSTEM;
    // ext superblock magic is at byte 1080 for a 512-byte logical device.
    if (probeCount >= 3 && read_u16(s_ioSector + 1024 + 56) == 0xEF53)
        return FAT32_EXISTING_RECOGNIZED_FILESYSTEM;
    return FAT32_EXISTING_UNKNOWN;
}

static bool buffer_is_zero(const uint8_t* bytes, size_t length)
{
    return bytes_zero(bytes, length);
}

static Fat32FormatStatus scan_partition_for_clean_state(
    const Fat32FormatRequest& request, uint32_t sectorSize,
    Fat32ExistingState& existingState)
{
    existingState = classify_existing_prefix(request, sectorSize);
    if (existingState == FAT32_EXISTING_UNREADABLE)
        return FAT32_FORMAT_READ_UNAVAILABLE;
    if (existingState == FAT32_EXISTING_RECOGNIZED_FILESYSTEM)
        return FAT32_FORMAT_FILESYSTEM_ALREADY_RECOGNIZED;

    const uint64_t total = request.partitionSnapshot.sectorCount;
    uint64_t relative = 0;
    bool allZero = true;
    while (relative < total) {
        uint64_t maxSectors = kZeroScanBufferBytes / sectorSize;
        DeviceCapabilities caps;
        if (!query_device_capabilities(request.targetSnapshot.globalIndex,
                                       caps))
            return FAT32_FORMAT_DEVICE_MISSING;
        if (caps.maxTransferBytes != 0) {
            const uint64_t transferSectors = caps.maxTransferBytes / sectorSize;
            if (transferSectors == 0) return FAT32_FORMAT_READ_UNAVAILABLE;
            if (maxSectors > transferSectors) maxSectors = transferSectors;
        }
        if (maxSectors == 0) return FAT32_FORMAT_READ_UNAVAILABLE;
        const uint32_t count = static_cast<uint32_t>(
            total - relative < maxSectors ? total - relative : maxSectors);
        uint64_t lba = 0;
        if (!add_u64(request.partitionSnapshot.startLba, relative, lba) ||
            !checked_lba_range(caps.totalLogicalSectors, lba, count))
            return FAT32_FORMAT_INVALID_GEOMETRY;
        if (read_sectors_safe(request.targetSnapshot.globalIndex, lba, count,
                s_zeroScanBuffer,
                static_cast<size_t>(count) * sectorSize) != block::BLOCK_OK) {
            existingState = FAT32_EXISTING_UNREADABLE;
            return FAT32_FORMAT_READ_UNAVAILABLE;
        }
        const size_t bytes = static_cast<size_t>(count) * sectorSize;
        if (!buffer_is_zero(s_zeroScanBuffer, bytes)) {
            allZero = false;
            break;
        }
        relative += count;
    }
    if (!allZero) {
        existingState = FAT32_EXISTING_AMBIGUOUS_DATA;
        return FAT32_FORMAT_AMBIGUOUS_EXISTING_DATA;
    }
    existingState = FAT32_EXISTING_CLEAN;
    return FAT32_FORMAT_READY;
}

static bool relative_range_valid(const Fat32FormatGeometry& geometry,
                                 uint64_t relative, uint32_t sectors)
{
    return sectors != 0 && relative < geometry.totalSectors &&
        sectors <= static_cast<uint64_t>(geometry.totalSectors) - relative;
}

static bool rollback_context_valid(const Fat32FormatRequest& request,
                                   const TargetIdentity& target,
                                   const PartitionEntry& partition,
                                   const Fat32FormatGeometry& geometry,
                                   const StorageOperationLease& lease);

static block::Status read_partition_sector(const TargetIdentity& target,
                                           const PartitionEntry& partition,
                                           const Fat32FormatGeometry& geometry,
                                           uint64_t relative,
                                           uint8_t* output)
{
    if (!output || !relative_range_valid(geometry, relative, 1) ||
        relative >= partition.sectorCount) return block::BLOCK_ERR_INVALID;
    uint64_t absolute = 0;
    if (!add_u64(partition.startLba, relative, absolute) ||
        !checked_lba_range(target.totalLogicalSectors, absolute, 1))
        return block::BLOCK_ERR_INVALID;
    return read_logical_sector(target.globalIndex, absolute, output,
                               MAX_LOGICAL_SECTOR_SIZE);
}

static bool write_partition_sector(const Fat32FormatRequest& request,
                                   const TargetIdentity& target,
                                   const PartitionEntry& partition,
                                   const Fat32FormatGeometry& geometry,
                                   uint64_t relative, const uint8_t* sector,
                                   const StorageOperationLease& lease,
                                   Fat32FormatResult& result)
{
    result.lastStage = result.stage;
    if (!sector || !relative_range_valid(geometry, relative, 1) ||
        relative >= partition.sectorCount) {
        result.status = FAT32_FORMAT_INVALID_GEOMETRY;
        return false;
    }
    const RevalidationStatus identity = revalidate_target_identity(target);
    if (identity != TARGET_VALID) {
        result.status = map_identity(identity);
        return false;
    }
    if (!rollback_context_valid(request, target, partition, geometry, lease)) {
        result.status = FAT32_FORMAT_PARTITION_IDENTITY_CHANGED;
        return false;
    }
    uint64_t absolute = 0;
    if (!add_u64(partition.startLba, relative, absolute) ||
        !checked_lba_range(target.totalLogicalSectors, absolute, 1)) {
        result.status = FAT32_FORMAT_INVALID_GEOMETRY;
        return false;
    }
    const block::Status status = write_sectors_safe(target.globalIndex,
        absolute, 1, sector, geometry.bytesPerSector);
    if (status != block::BLOCK_OK) {
        const bool haveIo = capture_current_io_result(result);
        const bool thisWriteAttempted = haveIo &&
            result.failedBlockDiagnostic.callbackInvoked;
        const bool thisWriteMayHaveReachedMedia = thisWriteAttempted &&
            (!result.failedBlockDiagnostic.transportDiagnostic.valid ||
             result.failedBlockDiagnostic.transportDiagnostic.dataSectorsTransferred != 0);
        result.writeAttempted = result.writeAttempted || thisWriteAttempted;
        result.firstFailedStage = result.firstFailedStage == FAT32_FORMAT_STAGE_IDLE
            ? result.stage : result.firstFailedStage;
        result.writeMayHaveReachedMedia = result.writeMayHaveReachedMedia ||
            thisWriteMayHaveReachedMedia;
        result.failedBeforeWrite = !result.writeMayHaveReachedMedia;
        if (result.status == FAT32_FORMAT_INVALID_REQUEST)
            result.status = FAT32_FORMAT_METADATA_WRITE_FAILED;
        return false;
    }
    result.writeAttempted = true;
    result.writeMayHaveReachedMedia = true;
    result.failedBeforeWrite = false;
    result.failedOperation = block::OPERATION_NONE;
    ++result.sectorsWritten;
    return true;
}

static void build_boot_sector(const Fat32FormatGeometry& geometry,
                              const char label[11], uint8_t sector[512])
{
    clear_bytes(sector, 512);
    sector[0] = 0xEB;
    sector[1] = 0x58;
    sector[2] = 0x90;
    copy_bytes(sector + 3, "GUIDEXOS ", 8);
    write_u16(sector + 11, 512);
    sector[13] = static_cast<uint8_t>(geometry.sectorsPerCluster);
    write_u16(sector + 14,
              static_cast<uint16_t>(geometry.reservedSectorCount));
    sector[16] = static_cast<uint8_t>(geometry.fatCount);
    write_u16(sector + 17, 0);
    write_u16(sector + 19, 0);
    sector[21] = 0xF8;
    write_u16(sector + 22, 0);
    write_u16(sector + 24, 63);
    write_u16(sector + 26, 255);
    write_u32(sector + 28, geometry.hiddenSectors);
    write_u32(sector + 32, geometry.totalSectors);
    write_u32(sector + 36, geometry.fatSizeSectors);
    write_u16(sector + 40, 0); // FAT mirroring enabled; two copies agree.
    write_u16(sector + 42, 0);
    write_u32(sector + 44, geometry.rootCluster);
    write_u16(sector + 48,
              static_cast<uint16_t>(geometry.fsInfoSector));
    write_u16(sector + 50,
              static_cast<uint16_t>(geometry.backupBootSector));
    sector[64] = 0x80;
    sector[65] = 0;
    sector[66] = 0x29;
    write_u32(sector + 67, geometry.volumeId);
    copy_bytes(sector + 71, label, 11);
    copy_bytes(sector + 82, "FAT32   ", 8);
    sector[510] = 0x55;
    sector[511] = 0xAA;
}

static void build_fsinfo(const Fat32FormatGeometry& geometry,
                         uint8_t sector[512])
{
    clear_bytes(sector, 512);
    write_u32(sector + 0, 0x41615252u);
    write_u32(sector + 484, 0x61417272u);
    write_u32(sector + 488, geometry.freeClusterCount);
    write_u32(sector + 492, 3u);
    write_u32(sector + 508, 0xAA550000u);
}

static void build_fat_sector(const Fat32FormatGeometry& geometry,
                             uint8_t sector[512])
{
    clear_bytes(sector, 512);
    write_u32(sector + 0, 0x0FFFFFF8u);
    write_u32(sector + 4, 0x0FFFFFFFu);
    write_u32(sector + 8, 0x0FFFFFFFu); // Root cluster 2 is allocated to EOC.
    (void)geometry;
}

static void build_root_sector(const char label[11], bool haveLabel,
                              uint8_t sector[512])
{
    clear_bytes(sector, 512);
    if (!haveLabel) return;
    copy_bytes(sector, label, 11);
    sector[11] = 0x08; // Volume-label entry; the following 0x00 ends the root.
}

static bool cluster_relative_range(const Fat32FormatGeometry& geometry,
                                   uint32_t cluster, uint64_t& firstSector)
{
    if (cluster < 2 ||
        static_cast<uint64_t>(cluster - 2u) >= geometry.clusterCount)
        return false;
    uint64_t clusterOffset = 0;
    uint64_t end = 0;
    if (!mul_u64(static_cast<uint64_t>(cluster - 2u),
                 geometry.sectorsPerCluster, clusterOffset) ||
        !add_u64(geometry.firstDataSector, clusterOffset, firstSector) ||
        !add_u64(firstSector, geometry.sectorsPerCluster, end) ||
        end > geometry.totalSectors) return false;
    return true;
}

static bool add_known_zero_record(const TargetIdentity& target,
                                  const PartitionEntry& partition,
                                  const Fat32FormatGeometry& geometry,
                                  uint64_t relativeSector)
{
    if (relativeSector > UINT32_MAX ||
        !relative_range_valid(geometry, relativeSector, 1) ||
        relativeSector >= partition.sectorCount) return false;
    for (uint32_t i = 0; i < s_rollbackRecordCount; ++i)
        if (s_rollbackRecords[i].relativeSector == relativeSector) return true;
    if (s_rollbackRecordCount >= FAT32_FORMAT_MAX_ROLLBACK_ENTRIES ||
        read_partition_sector(target, partition, geometry, relativeSector,
            s_ioSector) != block::BLOCK_OK ||
        !bytes_zero(s_ioSector, geometry.bytesPerSector)) return false;
    Fat32RollbackRecord& record = s_rollbackRecords[s_rollbackRecordCount++];
    record.relativeSector = static_cast<uint32_t>(relativeSector);
    record.priorState = FAT32_ROLLBACK_KNOWN_ZERO;
    clear_bytes(record.reserved, sizeof(record.reserved));
    return true;
}

// The full-partition zero scan is the authority for this representation.
// Recheck every planned write sector and retain only its LBA plus KnownZero;
// no sector payload or FAT-sized prefix is copied into rollback memory.
static bool capture_known_zero_rollback_set(
    const TargetIdentity& target, const PartitionEntry& partition,
    const Fat32FormatGeometry& geometry, bool haveLabel)
{
    clear_bytes(s_rollbackRecords, sizeof(s_rollbackRecords));
    s_rollbackRecordCount = 0;
    uint64_t rootSector = 0;
    if (!cluster_relative_range(geometry, geometry.rootCluster, rootSector) ||
        !add_known_zero_record(target, partition, geometry,
            geometry.firstFatSector) ||
        !add_known_zero_record(target, partition, geometry,
            geometry.secondFatSector) ||
        (haveLabel && !add_known_zero_record(target, partition, geometry,
            rootSector)) ||
        !add_known_zero_record(target, partition, geometry,
            geometry.backupBootSector) ||
        !add_known_zero_record(target, partition, geometry,
            geometry.backupFsInfoSector) ||
        !add_known_zero_record(target, partition, geometry,
            geometry.fsInfoSector) ||
        !add_known_zero_record(target, partition, geometry, 0))
        return false;
    return s_rollbackRecordCount != 0;
}

static bool rollback_context_valid(const Fat32FormatRequest& request,
                                   const TargetIdentity& target,
                                   const PartitionEntry& partition,
                                   const Fat32FormatGeometry& geometry,
                                   const StorageOperationLease& lease)
{
    if (!storage_operation_lease_is_current(lease) ||
        revalidate_target_identity(target) != TARGET_VALID) return false;
    DeviceCapabilities caps;
    if (!query_device_capabilities(target.globalIndex, caps) ||
        caps.totalLogicalSectors != target.totalLogicalSectors ||
        caps.logicalSectorSize != geometry.bytesPerSector ||
        !target_range_valid(target, partition.startLba,
                            partition.sectorCount)) return false;
    PartitionEntry current = {};
    return find_and_validate_partition(request, caps, s_partitionTable,
                                       current) &&
        same_partition_extent(current, partition);
}

static bool rollback_set_is_zero(const TargetIdentity& target,
                                 const PartitionEntry& partition,
                                 const Fat32FormatGeometry& geometry)
{
    if (s_rollbackRecordCount == 0) return false;
    for (uint32_t i = 0; i < s_rollbackRecordCount; ++i) {
        const Fat32RollbackRecord& record = s_rollbackRecords[i];
        if (record.priorState != FAT32_ROLLBACK_KNOWN_ZERO ||
            read_partition_sector(target, partition, geometry,
                record.relativeSector, s_ioSector) != block::BLOCK_OK ||
            !bytes_zero(s_ioSector, geometry.bytesPerSector)) return false;
    }
    return true;
}

static bool restore_known_zero_rollback_set(
    const Fat32FormatRequest& request, const TargetIdentity& target,
    const PartitionEntry& partition, const Fat32FormatGeometry& geometry,
    const StorageOperationLease& lease, Fat32FormatResult& result)
{
    if (!result.writeMayHaveReachedMedia || s_rollbackRecordCount == 0 ||
        s_rollbackRecordCount > FAT32_FORMAT_MAX_ROLLBACK_ENTRIES ||
        !rollback_context_valid(request, target, partition, geometry, lease))
        return false;
    DeviceCapabilities caps;
    if (!query_device_capabilities(target.globalIndex, caps) ||
        (caps.persistence != PERSISTENCE_SYNCHRONOUS_DURABLE &&
         !(caps.persistence == PERSISTENCE_FLUSH_REQUIRED &&
           caps.flushSupported && caps.flushSemanticsKnown))) return false;

    result.rollbackAttempted = true;
    result.rollbackEntryCount = s_rollbackRecordCount;
    result.rollbackRecordBytes = s_rollbackRecordCount *
        FAT32_FORMAT_ROLLBACK_RECORD_BYTES;
    bool restored = true;
    result.rollbackWriteStatus = block::BLOCK_OK;
    clear_bytes(s_ioSector, geometry.bytesPerSector);
    for (uint32_t i = 0; i < s_rollbackRecordCount; ++i) {
        const Fat32RollbackRecord& record = s_rollbackRecords[i];
        result.rollbackStage = FAT32_FORMAT_STAGE_ROLLBACK_WRITE;
        if (record.priorState != FAT32_ROLLBACK_KNOWN_ZERO ||
            !rollback_context_valid(request, target, partition, geometry,
                                     lease)) {
            restored = false;
            result.rollbackWriteStatus = block::BLOCK_ERR_NO_MEDIA;
            break;
        }
        uint64_t absolute = 0;
        if (!add_u64(partition.startLba, record.relativeSector, absolute) ||
            !checked_lba_range(target.totalLogicalSectors, absolute, 1) ||
            !relative_range_valid(geometry, record.relativeSector, 1)) {
            restored = false;
            result.rollbackWriteStatus = block::BLOCK_ERR_INVALID;
            break;
        }
        result.rollbackWriteAttempted = true;
        result.rollbackWriteStatus = write_sectors_safe(target.globalIndex,
            absolute, 1, s_ioSector, geometry.bytesPerSector);
        if (result.rollbackWriteStatus != block::BLOCK_OK) {
            restored = false;
            break;
        }
        ++result.rollbackSectorsWritten;
    }
    if (!restored || !rollback_context_valid(request, target, partition,
                                              geometry, lease)) return false;
    result.rollbackStage = FAT32_FORMAT_STAGE_ROLLBACK_FLUSH;
    result.rollbackFlushAttempted = true;
    const block::FlushReport flush =
        block::flush_with_result(target.globalIndex);
    result.rollbackFlushOutcome = flush.outcome;
    result.rollbackFlushStatus = flush.status;
    if (!trusted_flush(flush) ||
        !rollback_context_valid(request, target, partition, geometry, lease))
        return false;
    result.rollbackStage = FAT32_FORMAT_STAGE_ROLLBACK_VERIFY;
    if (!rollback_set_is_zero(target, partition, geometry)) return false;
    result.rollbackVerificationPassed = true;
    return true;
}

static bool verify_fat32_structure(const TargetIdentity& target,
                                   const PartitionEntry& partition,
                                   const Fat32FormatGeometry& planned,
                                   const char expectedLabel[11])
{
    uint8_t primary[512];
    uint8_t backup[512];
    uint8_t fsinfo[512];
    uint8_t backupFsinfo[512];
    uint64_t absolute = 0;
    if (read_partition_sector(target, partition, planned, 0, primary) !=
            block::BLOCK_OK ||
        read_partition_sector(target, partition, planned,
            planned.backupBootSector, backup) != block::BLOCK_OK ||
        read_partition_sector(target, partition, planned,
            planned.fsInfoSector, fsinfo) != block::BLOCK_OK ||
        read_partition_sector(target, partition, planned,
            planned.backupFsInfoSector, backupFsinfo) != block::BLOCK_OK)
        return false;

    // Parse the written BPB independently and derive region bounds from disk.
    const uint32_t bps = read_u16(primary + 11);
    const uint32_t spc = primary[13];
    const uint32_t reserved = read_u16(primary + 14);
    const uint32_t fats = primary[16];
    const uint32_t total = read_u32(primary + 32);
    const uint32_t fatSectors = read_u32(primary + 36);
    const uint32_t rootCluster = read_u32(primary + 44);
    uint64_t fatRegionSectors = 0, firstData = 0;
    if (bps != FAT32_FORMAT_SECTOR_SIZE || spc == 0 ||
        spc > FAT32_FORMAT_MAX_SECTORS_PER_CLUSTER ||
        (spc & (spc - 1u)) != 0 || reserved == 0 || fats != 2 ||
        read_u16(primary + 17) != 0 || read_u16(primary + 19) != 0 ||
        read_u16(primary + 22) != 0 || total == 0 ||
        total > partition.sectorCount || fatSectors == 0 || rootCluster < 2 ||
        read_u16(primary + 40) != 0 || read_u16(primary + 42) != 0 ||
        read_u16(primary + 48) != planned.fsInfoSector ||
        read_u16(primary + 50) != planned.backupBootSector ||
        primary[66] != 0x29 || read_u32(primary + 67) == 0 ||
        primary[510] != 0x55 || primary[511] != 0xAA ||
        !ascii_eq(primary + 82, "FAT32   ", 8) ||
        !bytes_equal(primary + 71, reinterpret_cast<const uint8_t*>(expectedLabel), 11) ||
        !mul_u64(fats, fatSectors, fatRegionSectors) ||
        !add_u64(reserved, fatRegionSectors, firstData) ||
        firstData >= total || rootCluster - 2 >=
            (total - firstData) / spc ||
        firstData + static_cast<uint64_t>(spc) > total ||
        firstData + static_cast<uint64_t>(spc) > partition.sectorCount ||
        !checked_lba_range(target.totalLogicalSectors,
            partition.startLba, total)) return false;

    const uint64_t clusterCount64 = (total - firstData) / spc;
    const uint32_t clusters = static_cast<uint32_t>(clusterCount64);
    uint64_t fatBytes = 0, fatCapacityBytes = 0;
    if (clusters < FAT32_FORMAT_MIN_CLUSTERS ||
        clusters > FAT32_FORMAT_MAX_CLUSTERS ||
        !mul_u64(static_cast<uint64_t>(clusters) + 2u, 4u, fatBytes) ||
        !mul_u64(fatSectors, bps, fatCapacityBytes) ||
        fatBytes > fatCapacityBytes ||
        !bytes_equal(primary, backup, sizeof(primary))) return false;
    if (read_u32(fsinfo) != 0x41615252u ||
        read_u32(fsinfo + 484) != 0x61417272u ||
        read_u32(fsinfo + 488) != clusters - 1 ||
        read_u32(fsinfo + 492) != 3u ||
        read_u32(fsinfo + 508) != 0xAA550000u ||
        !bytes_equal(fsinfo, backupFsinfo, sizeof(fsinfo))) return false;

    const uint32_t diskVolumeId = read_u32(primary + 67);
    if (diskVolumeId != planned.volumeId) return false;

    uint8_t firstFatSector[512];
    uint8_t secondFatSector[512];
    if (!relative_range_valid(planned, reserved, 1) ||
        !relative_range_valid(planned,
            static_cast<uint64_t>(reserved) + fatSectors, 1) ||
        read_partition_sector(target, partition, planned, reserved,
            firstFatSector) != block::BLOCK_OK ||
        read_partition_sector(target, partition, planned,
            static_cast<uint64_t>(reserved) + fatSectors,
            secondFatSector) != block::BLOCK_OK ||
        read_u32(firstFatSector) != 0x0FFFFFF8u ||
        read_u32(firstFatSector + 4) != 0x0FFFFFFFu ||
        read_u32(firstFatSector + 8) != 0x0FFFFFFFu ||
        !bytes_zero(firstFatSector + 12, bps - 12) ||
        !bytes_equal(firstFatSector, secondFatSector, bps)) return false;
    // The full-volume blank scan established all untouched FAT sectors as
    // zero. Read-back therefore checks the initialized FAT sector in each
    // mirror, avoiding verification work proportional to FAT capacity.
    uint64_t rootSector = 0;
    if (!cluster_relative_range(planned, rootCluster, rootSector)) return false;
    for (uint32_t i = 0; i < spc; ++i) {
        uint64_t sector = 0;
        if (!add_u64(rootSector, i, sector) ||
            read_partition_sector(target, partition, planned, sector,
                s_ioSector) != block::BLOCK_OK) return false;
        if (i == 0 && expectedLabel[0] != ' ') {
            if (!bytes_equal(s_ioSector,
                    reinterpret_cast<const uint8_t*>(expectedLabel), 11) ||
                s_ioSector[11] != 0x08 ||
                !bytes_zero(s_ioSector + 12, bps - 12)) return false;
        } else if (!bytes_zero(s_ioSector, bps)) {
            return false;
        }
    }
    // Independently confirm the BPB's advertised FAT layout equals the
    // pre-write checked plan and stays in the chosen partition.
    if (bps != planned.bytesPerSector || spc != planned.sectorsPerCluster ||
        reserved != planned.reservedSectorCount || fats != planned.fatCount ||
        fatSectors != planned.fatSizeSectors || total != planned.totalSectors ||
        rootCluster != planned.rootCluster || firstData != planned.firstDataSector)
        return false;
    (void)absolute;
    return true;
}

static bool rescan_partition(const Fat32FormatRequest& request,
                             const PartitionEntry& expected)
{
    if (!parse_partition_table(request.targetSnapshot.globalIndex,
                               s_partitionTable)) return false;
    const DiskState expectedState = request.partitionScheme == PARTITION_SCHEME_GPT
        ? DISK_STATE_VALID_GPT : DISK_STATE_VALID_MBR;
    if (s_partitionTable.state != expectedState) return false;
    DeviceCapabilities caps;
    if (!query_device_capabilities(request.targetSnapshot.globalIndex, caps))
        return false;
    PartitionEntry current = {};
    Fat32FormatRequest refreshed = request;
    if (!find_and_validate_partition(refreshed, caps, s_partitionTable, current))
        return false;
    return same_partition_extent(current, expected);
}

static bool secure_random(void* output, size_t length)
{
#if defined(KERNEL_STORAGE_TEST)
    (void)output;
    (void)length;
    return false;
#else
    return kernel::virtio::rng::fill(output, length);
#endif
}

static bool make_volume_id(const Fat32FormatRequest& request,
                           uint32_t& volumeId)
{
#if defined(KERNEL_STORAGE_TEST)
    if (request.testVolumeIdProvided) {
        volumeId = request.testVolumeId;
        return volumeId != 0;
    }
#else
    (void)request;
#endif
    uint8_t bytes[4];
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        if (!secure_random(bytes, sizeof(bytes))) return false;
        volumeId = read_u32(bytes);
        if (volumeId != 0) return true;
    }
    return false;
}

static Fat32FormatStatus execute_locked(const Fat32FormatRequest& request,
                                        Fat32FormatResult& result,
                                        StorageOperationLease& lease)
{
    PartitionCheck check = {};
    result.stage = FAT32_FORMAT_STAGE_REVALIDATING_PARTITION;
    result.lastStage = result.stage;
    Fat32FormatStatus status = validate_request_and_partition(request, result,
                                                               check);
    if (status != FAT32_FORMAT_READY) return status;
    result.stage = FAT32_FORMAT_STAGE_CALCULATING_LAYOUT;
    result.lastStage = result.stage;
    clear_bytes(s_rollbackRecords, sizeof(s_rollbackRecords));
    s_rollbackRecordCount = 0;
    status = scan_partition_for_clean_state(request,
        check.capabilities.logicalSectorSize, result.existingState);
    if (status != FAT32_FORMAT_READY) return status;

    char label[11];
    status = normalize_fat32_volume_label(request.volumeLabel, label);
    if (status != FAT32_FORMAT_READY) return status;
    uint32_t volumeId = 0;
    if (!make_volume_id(request, volumeId))
        return FAT32_FORMAT_VOLUME_ID_UNAVAILABLE;
    check.geometry.volumeId = volumeId;
    result.geometry = check.geometry;
    const bool haveLabel = label[0] != ' ';

    result.stage = FAT32_FORMAT_STAGE_SNAPSHOT_FILESYSTEM_METADATA;
    result.lastStage = result.stage;
    if (!capture_known_zero_rollback_set(request.targetSnapshot,
            check.currentPartition, check.geometry, haveLabel)) {
        result.firstFailedStage = result.stage;
        (void)capture_current_io_result(result);
        return FAT32_FORMAT_SNAPSHOT_FAILED;
    }
    result.rollbackEntryCount = s_rollbackRecordCount;
    result.rollbackRecordBytes = s_rollbackRecordCount *
        FAT32_FORMAT_ROLLBACK_RECORD_BYTES;

    result.stage = FAT32_FORMAT_STAGE_SNAPSHOT_FILESYSTEM_METADATA;
    result.lastStage = result.stage;
    if (!rollback_context_valid(request, request.targetSnapshot,
            check.currentPartition, check.geometry, lease))
        return FAT32_FORMAT_PARTITION_IDENTITY_CHANGED;
    if (!rollback_set_is_zero(request.targetSnapshot,
            check.currentPartition, check.geometry))
        return FAT32_FORMAT_SNAPSHOT_FAILED;
    result.stage = FAT32_FORMAT_STAGE_FLUSH;
    result.lastStage = result.stage;
    result.flushAttempted = true;
    ++result.flushAttempts;
    const block::FlushReport preWriteFlush =
        block::flush_with_result(request.targetSnapshot.globalIndex);
    result.flushOutcome = preWriteFlush.outcome;
    result.flushStatus = preWriteFlush.status;
    if (!trusted_flush(preWriteFlush)) {
        result.failedOperation = block::OPERATION_FLUSH;
        (void)capture_current_io_result(result);
        return preWriteFlush.semanticsKnown
            ? FAT32_FORMAT_FLUSH_FAILED : FAT32_FORMAT_FLUSH_UNAVAILABLE;
    }
    if (!begin_storage_operation_execution(lease))
        return FAT32_FORMAT_OPERATION_OWNERSHIP_INVALID;

    result.stage = FAT32_FORMAT_STAGE_WRITE_FAT;
    result.lastStage = result.stage;
    result.status = FAT32_FORMAT_INVALID_REQUEST;
    uint8_t fatSector[512];
    uint8_t fsinfoSector[512];
    uint8_t bootSector[512];
    uint8_t rootSector[512];
    build_fat_sector(check.geometry, fatSector);
    bool writeOk = write_partition_sector(request, request.targetSnapshot,
        check.currentPartition, check.geometry,
        check.geometry.firstFatSector, fatSector, lease, result);
    if (writeOk) {
        writeOk = write_partition_sector(request, request.targetSnapshot,
            check.currentPartition, check.geometry,
            check.geometry.secondFatSector, fatSector, lease, result);
    }
    build_root_sector(label, haveLabel, rootSector);
    uint64_t rootSectorLba = 0;
    if (writeOk && haveLabel) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_ROOT;
        result.lastStage = result.stage;
        writeOk = cluster_relative_range(check.geometry,
            check.geometry.rootCluster, rootSectorLba);
    }
    if (writeOk && haveLabel) {
        writeOk = write_partition_sector(request, request.targetSnapshot,
            check.currentPartition, check.geometry,
            rootSectorLba, rootSector, lease, result);
    }

    build_fsinfo(check.geometry, fsinfoSector);
    build_boot_sector(check.geometry, label, bootSector);
    if (writeOk) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_BACKUP_METADATA;
        result.lastStage = result.stage;
        writeOk = write_partition_sector(request, request.targetSnapshot,
        check.currentPartition, check.geometry,
        check.geometry.backupBootSector, bootSector, lease, result);
    }
    if (writeOk) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_FSINFO;
        result.lastStage = result.stage;
        writeOk = write_partition_sector(request, request.targetSnapshot,
        check.currentPartition, check.geometry,
        check.geometry.backupFsInfoSector, fsinfoSector, lease, result);
    }
    if (writeOk) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_FSINFO;
        result.lastStage = result.stage;
        writeOk = write_partition_sector(request, request.targetSnapshot,
        check.currentPartition, check.geometry,
        check.geometry.fsInfoSector, fsinfoSector, lease, result);
    }
    // Publish FAT32 only after the supporting structures exist.
    if (writeOk) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_BOOT_SECTOR;
        result.lastStage = result.stage;
        writeOk = write_partition_sector(request, request.targetSnapshot,
            check.currentPartition, check.geometry, 0, bootSector, lease,
            result);
    }
    if (!writeOk) {
        status = result.status == FAT32_FORMAT_INVALID_REQUEST
            ? FAT32_FORMAT_METADATA_WRITE_FAILED : result.status;
        goto failed;
    }

    result.stage = FAT32_FORMAT_STAGE_FLUSH;
    result.lastStage = result.stage;
    result.flushAttempted = true;
    ++result.flushAttempts;
    {
        const block::FlushReport flush =
            block::flush_with_result(request.targetSnapshot.globalIndex);
        result.flushOutcome = flush.outcome;
        result.flushStatus = flush.status;
        result.persistenceTrusted = trusted_flush(flush);
        if (!result.persistenceTrusted) {
            result.failedOperation = block::OPERATION_FLUSH;
            (void)capture_current_io_result(result);
            status = flush.semanticsKnown ? FAT32_FORMAT_FLUSH_FAILED
                                          : FAT32_FORMAT_FLUSH_UNAVAILABLE;
            goto failed;
        }
    }

    result.stage = FAT32_FORMAT_STAGE_VERIFY;
    result.lastStage = result.stage;
    if (!verify_fat32_structure(request.targetSnapshot,
            check.currentPartition, check.geometry, label)) {
        result.failedOperation = block::OPERATION_READ;
        (void)capture_current_io_result(result);
        status = FAT32_FORMAT_VERIFICATION_FAILED;
        goto failed;
    }
    result.verificationPassed = true;
    result.finalProbeState = FAT32_FINAL_PROBE_FAT32;

    result.stage = FAT32_FORMAT_STAGE_RESCAN;
    result.lastStage = result.stage;
    if (!rescan_partition(request, check.currentPartition) ||
        revalidate_target_identity(request.targetSnapshot) != TARGET_VALID) {
        result.failedOperation = block::OPERATION_READ;
        (void)capture_current_io_result(result);
        status = FAT32_FORMAT_RESCAN_FAILED;
        goto failed;
    }
    result.status = FAT32_FORMAT_SUCCESS;
    result.failureStatus = FAT32_FORMAT_SUCCESS;
    result.stage = FAT32_FORMAT_STAGE_COMPLETED;
    result.lastStage = FAT32_FORMAT_STAGE_COMPLETED;
    result.failedBeforeWrite = false;
    result.finalProbeState = FAT32_FINAL_PROBE_FAT32;
    set_diagnostic(result,
        "FAT32 metadata formatted and verified. The partition remains unmounted.");
    complete_storage_operation_execution(lease);
    clear_bytes(s_rollbackRecords, sizeof(s_rollbackRecords));
    s_rollbackRecordCount = 0;
    return result.status;

failed:
    result.lastStage = result.stage;
    if (result.firstFailedStage == FAT32_FORMAT_STAGE_IDLE)
        result.firstFailedStage = result.lastStage;
    result.failureStatus = status;
    result.status = status;
    result.stage = FAT32_FORMAT_STAGE_FAILED;
    set_diagnostic(result, fat32_format_status_name(status));
    result.failedBeforeWrite = !result.writeMayHaveReachedMedia;
    if (result.writeMayHaveReachedMedia) {
        result.rollbackSucceeded = restore_known_zero_rollback_set(request,
            request.targetSnapshot, check.currentPartition, check.geometry,
            lease, result);
        if (!result.rollbackSucceeded) {
            result.status = FAT32_FORMAT_ROLLBACK_FAILED;
            result.stage = FAT32_FORMAT_STAGE_STATE_UNCERTAIN;
    result.finalStateUncertain = true;
            result.finalProbeState = FAT32_FINAL_PROBE_UNKNOWN;
            set_diagnostic(result,
                "Filesystem state uncertain; rollback could not be verified.");
    } else {
        result.finalProbeState = FAT32_FINAL_PROBE_UNFORMATTED;
    }
    } else if (rollback_context_valid(request, request.targetSnapshot,
                   check.currentPartition, check.geometry, lease) &&
               rollback_set_is_zero(request.targetSnapshot,
                   check.currentPartition, check.geometry)) {
        result.finalProbeState = FAT32_FINAL_PROBE_UNFORMATTED;
    }
    complete_storage_operation_execution(lease);
    clear_bytes(s_rollbackRecords, sizeof(s_rollbackRecords));
    s_rollbackRecordCount = 0;
    return result.status;
}

static Fat32FormatStatus run_probe_locked(const Fat32FormatRequest& request,
                                         Fat32FormatResult& result)
{
    PartitionCheck check = {};
    result.stage = FAT32_FORMAT_STAGE_REVALIDATING_PARTITION;
    Fat32FormatStatus status = validate_request_and_partition(request, result,
                                                               check);
    if (status != FAT32_FORMAT_READY) return status;
    result.stage = FAT32_FORMAT_STAGE_CALCULATING_LAYOUT;
    status = scan_partition_for_clean_state(request,
        check.capabilities.logicalSectorSize, result.existingState);
    if (status != FAT32_FORMAT_READY) return status;
    result.status = FAT32_FORMAT_READY;
    result.finalProbeState = FAT32_FINAL_PROBE_UNFORMATTED;
    set_diagnostic(result, "Partition is clean and eligible for FAT32 formatting.");
    return FAT32_FORMAT_READY;
}

static Fat32FormatStatus run_with_lease(const Fat32FormatRequest& request,
                                        Fat32FormatResult& result,
                                        bool format)
{
    StorageOperationLease lease = {};
    result.stage = FAT32_FORMAT_STAGE_ACQUIRE_LEASE;
    const StorageOperationLockStatus lock = try_acquire_storage_operation(lease);
    if (lock != STORAGE_OPERATION_LOCK_ACQUIRED) {
        result.status = lock == STORAGE_OPERATION_LOCK_BUSY
            ? FAT32_FORMAT_OPERATION_BUSY
            : FAT32_FORMAT_OPERATION_OWNERSHIP_INVALID;
        mark_stage_failed(result);
        set_diagnostic(result, fat32_format_status_name(result.status));
        return result.status;
    }
    result.stage = FAT32_FORMAT_STAGE_PIN_TARGET;
    if (!pin_storage_operation_target(lease, request.targetSnapshot)) {
        result.status = map_identity(
            revalidate_target_identity(request.targetSnapshot));
        if (result.status == FAT32_FORMAT_SUCCESS)
            result.status = FAT32_FORMAT_IDENTITY_CHANGED;
        mark_stage_failed(result);
        set_diagnostic(result, fat32_format_status_name(result.status));
        release_storage_operation(lease);
        return result.status;
    }
    const Fat32FormatStatus status = format
        ? execute_locked(request, result, lease)
        : run_probe_locked(request, result);
    if (format && !result.writeAttempted &&
        result.stage != FAT32_FORMAT_STAGE_COMPLETED &&
        result.stage != FAT32_FORMAT_STAGE_STATE_UNCERTAIN)
        release_storage_operation(lease);
    else if (!format)
        release_storage_operation(lease);
    if (status != FAT32_FORMAT_READY && status != FAT32_FORMAT_SUCCESS &&
        result.stage != FAT32_FORMAT_STAGE_STATE_UNCERTAIN) {
        result.status = status;
        if (result.stage != FAT32_FORMAT_STAGE_FAILED)
            mark_stage_failed(result);
        set_diagnostic(result, fat32_format_status_name(status));
    }
    return status;
}

} // namespace

Fat32FormatStatus calculate_fat32_format_geometry(
    uint64_t partitionStartLba, uint64_t partitionSectorCount,
    uint32_t logicalSectorSize, Fat32FormatGeometry& result)
{
    clear_bytes(&result, sizeof(result));
    if (logicalSectorSize != FAT32_FORMAT_SECTOR_SIZE)
        return FAT32_FORMAT_UNSUPPORTED_SECTOR_SIZE;
    if (partitionSectorCount == 0) return FAT32_FORMAT_TOO_SMALL;
    if (partitionSectorCount > UINT32_MAX)
        return FAT32_FORMAT_LAYOUT_OVERFLOW;

    uint32_t sectorsPerCluster = FAT32_FORMAT_MAX_SECTORS_PER_CLUSTER;
    uint64_t fatSectors = 0;
    uint64_t clusterCount = 0;
    uint64_t firstData = 0;
    bool chosen = false;
    // Keep the cluster size at or below 32 KiB for FAT32 interoperability.
    // Prefer the largest supported cluster that still leaves enough data
    // clusters to be FAT32. FAT size and rollback memory do not choose this
    // geometry.
    for (;;) {
        fatSectors = 1;
        bool converged = false;
        bool candidateTooSmall = false;
        for (uint8_t iteration = 0; iteration < 64; ++iteration) {
            uint64_t fatRegions = 0;
            if (!mul_u64(kFat32Count, fatSectors, fatRegions) ||
                !add_u64(kFat32ReservedSectors, fatRegions, firstData))
                return FAT32_FORMAT_LAYOUT_OVERFLOW;
            if (firstData >= partitionSectorCount) {
                candidateTooSmall = true;
                break;
            }
            clusterCount = (partitionSectorCount - firstData) /
                sectorsPerCluster;
            uint64_t requiredFatBytes = 0;
            if (!mul_u64(clusterCount + 2u, 4u, requiredFatBytes) ||
                requiredFatBytes > UINT64_MAX - (logicalSectorSize - 1u))
                return FAT32_FORMAT_LAYOUT_OVERFLOW;
            const uint64_t neededFatSectors = (requiredFatBytes +
                logicalSectorSize - 1u) / logicalSectorSize;
            if (neededFatSectors == fatSectors) {
                converged = true;
                break;
            }
            if (neededFatSectors == 0 || neededFatSectors > UINT32_MAX)
                return FAT32_FORMAT_LAYOUT_OVERFLOW;
            fatSectors = neededFatSectors;
        }
        if (candidateTooSmall) {
            if (sectorsPerCluster == 1) break;
            sectorsPerCluster >>= 1;
            continue;
        }
        if (!converged || fatSectors > UINT32_MAX)
            return FAT32_FORMAT_LAYOUT_OVERFLOW;

        uint64_t totalFatSectors = 0;
        if (!mul_u64(kFat32Count, fatSectors, totalFatSectors) ||
            !add_u64(kFat32ReservedSectors, totalFatSectors, firstData) ||
            firstData >= partitionSectorCount)
            return FAT32_FORMAT_TOO_SMALL;
        clusterCount = (partitionSectorCount - firstData) /
            sectorsPerCluster;
        if (clusterCount > FAT32_FORMAT_MAX_CLUSTERS)
            return FAT32_FORMAT_LAYOUT_OVERFLOW;
        if (clusterCount >= FAT32_FORMAT_MIN_CLUSTERS) {
            chosen = true;
            break;
        }
        if (sectorsPerCluster == 1) break;
        sectorsPerCluster >>= 1;
    }
    if (!chosen) return FAT32_FORMAT_TOO_SMALL;

    uint64_t fatBytes = 0, fatCapacityBytes = 0, fatEntriesAvailable = 0;
    if (!mul_u64(clusterCount + 2u, 4u, fatBytes) ||
        !mul_u64(fatSectors, logicalSectorSize, fatCapacityBytes))
        return FAT32_FORMAT_LAYOUT_OVERFLOW;
    fatEntriesAvailable = fatCapacityBytes / 4u;
    if (fatBytes > fatCapacityBytes ||
        fatEntriesAvailable < clusterCount + 2u)
        return FAT32_FORMAT_LAYOUT_OVERFLOW;
    uint64_t rootEnd = 0;
    if (!add_u64(firstData, sectorsPerCluster, rootEnd) ||
        rootEnd > partitionSectorCount)
        return FAT32_FORMAT_LAYOUT_OVERFLOW;
    uint64_t absoluteEnd = 0;
    if (!add_u64(partitionStartLba, partitionSectorCount, absoluteEnd) ||
        absoluteEnd == 0) return FAT32_FORMAT_LAYOUT_OVERFLOW;

    result.bytesPerSector = logicalSectorSize;
    result.sectorsPerCluster = sectorsPerCluster;
    result.clusterSizeBytes = sectorsPerCluster * logicalSectorSize;
    result.reservedSectorCount = kFat32ReservedSectors;
    result.fatCount = kFat32Count;
    result.fatSizeSectors = static_cast<uint32_t>(fatSectors);
    result.firstFatSector = kFat32ReservedSectors;
    uint64_t secondFatSector = 0;
    if (!add_u64(kFat32ReservedSectors, fatSectors, secondFatSector) ||
        secondFatSector > UINT32_MAX) return FAT32_FORMAT_LAYOUT_OVERFLOW;
    result.secondFatSector = static_cast<uint32_t>(secondFatSector);
    result.firstDataSector = static_cast<uint32_t>(firstData);
    result.rootCluster = 2;
    result.clusterCount = static_cast<uint32_t>(clusterCount);
    result.freeClusterCount = result.clusterCount - 1;
    result.totalSectors = static_cast<uint32_t>(partitionSectorCount);
    result.fsInfoSector = kFsInfoSector;
    result.backupFsInfoSector = kBackupFsInfoSector;
    result.backupBootSector = kBackupBootSector;
    result.hiddenSectors = partitionStartLba <= UINT32_MAX
        ? static_cast<uint32_t>(partitionStartLba) : 0;
    return FAT32_FORMAT_READY;
}

Fat32FormatStatus normalize_fat32_volume_label(const char* input,
                                              char normalized[11])
{
    if (!input || !normalized) return FAT32_FORMAT_LABEL_INVALID;
    clear_bytes(normalized, 11);
    size_t length = 0;
    while (length < 12 && input[length] != '\0') ++length;
    if (length == 12 || length > 11) return FAT32_FORMAT_LABEL_INVALID;
    static const char allowedPunctuation[] = "$%'-_@~`!(){}^#&";
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 'a' + 'A');
        bool allowed = c == ' ' || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9');
        for (size_t punctuation = 0;
             !allowed && punctuation + 1 < sizeof(allowedPunctuation);
             ++punctuation)
            allowed = c == static_cast<unsigned char>(allowedPunctuation[punctuation]);
        if (!allowed || (c == ' ' && (i == 0 || i + 1 == length)))
            return FAT32_FORMAT_LABEL_INVALID;
        normalized[i] = static_cast<char>(c);
    }
    for (size_t i = length; i < 11; ++i) normalized[i] = ' ';
    return FAT32_FORMAT_READY;
}

Fat32FormatStatus probe_fat32_format_partition(
    const Fat32FormatRequest& request, Fat32FormatResult& result)
{
    reset_result(result);
    result.targetIdentity = request.targetSnapshot;
    result.partition = request.partitionSnapshot;
    result.stage = FAT32_FORMAT_STAGE_VALIDATING;
    return run_with_lease(request, result, false);
}

Fat32FormatStatus format_fat32_partition(
    const Fat32FormatRequest& request, Fat32FormatResult& result)
{
    reset_result(result);
    result.targetIdentity = request.targetSnapshot;
    result.partition = request.partitionSnapshot;
    result.stage = FAT32_FORMAT_STAGE_VALIDATING;
    return run_with_lease(request, result, true);
}

const char* fat32_format_status_name(Fat32FormatStatus status)
{
    switch (status) {
        case FAT32_FORMAT_READY: return "Ready to format FAT32";
        case FAT32_FORMAT_SUCCESS: return "FAT32 formatted and verified";
        case FAT32_FORMAT_OPERATION_BUSY: return "Another storage operation is active";
        case FAT32_FORMAT_INVALID_REQUEST: return "The format request is invalid";
        case FAT32_FORMAT_DEVICE_MISSING: return "The disk is no longer present";
        case FAT32_FORMAT_REGISTRY_CHANGED: return "Disk registry changed; refresh and try again";
        case FAT32_FORMAT_IDENTITY_CHANGED: return "Disk identity changed; refresh and try again";
        case FAT32_FORMAT_PARTITION_DISAPPEARED: return "The selected partition no longer exists";
        case FAT32_FORMAT_PARTITION_IDENTITY_CHANGED: return "Partition identity or bounds changed; refresh and try again";
        case FAT32_FORMAT_INVALID_TABLE: return "The partition table is invalid or unsupported";
        case FAT32_FORMAT_INVALID_GEOMETRY: return "Disk or partition geometry is invalid";
        case FAT32_FORMAT_READ_UNAVAILABLE: return "Partition reads are unavailable";
        case FAT32_FORMAT_WRITE_UNAVAILABLE: return "Durable sector writes are unavailable";
        case FAT32_FORMAT_READ_ONLY: return "The disk is read-only";
        case FAT32_FORMAT_DURABILITY_UNKNOWN: return "Durable writes are not proven for this disk";
        case FAT32_FORMAT_FLUSH_UNAVAILABLE: return "A trusted flush is unavailable";
        case FAT32_FORMAT_FLUSH_FAILED: return "Disk flush failed";
        case FAT32_FORMAT_MOUNTED: return "The disk is mounted and protected";
        case FAT32_FORMAT_ROOT_BACKING: return "The disk backs the root filesystem and is protected";
        case FAT32_FORMAT_BOOT_BACKING: return "The disk is the boot device and is protected";
        case FAT32_FORMAT_BOOT_IDENTITY_UNKNOWN: return "Boot-device identity is unknown";
        case FAT32_FORMAT_UNSUPPORTED_SECTOR_SIZE: return "FAT32 formatting currently requires 512-byte logical sectors";
        case FAT32_FORMAT_TOO_SMALL: return "Partition is too small for the supported FAT32 layout.";
        case FAT32_FORMAT_LAYOUT_OVERFLOW: return "FAT32 layout arithmetic exceeded a supported bound";
        case FAT32_FORMAT_FILESYSTEM_ALREADY_RECOGNIZED: return "A recognized filesystem already exists on this partition";
        case FAT32_FORMAT_AMBIGUOUS_EXISTING_DATA: return "Partition contains non-zero data of unknown type; formatting is blocked";
        case FAT32_FORMAT_LABEL_INVALID: return "Volume label must use supported FAT characters and be at most 11 characters";
        case FAT32_FORMAT_VOLUME_ID_UNAVAILABLE: return "A non-zero volume ID could not be generated";
        case FAT32_FORMAT_ROLLBACK_BUFFER_LIMIT: return "Legacy rollback-cap status; this formatter no longer returns it";
        case FAT32_FORMAT_SNAPSHOT_FAILED: return "Partition metadata snapshot failed";
        case FAT32_FORMAT_OPERATION_OWNERSHIP_INVALID: return "Storage operation lease is no longer valid";
        case FAT32_FORMAT_METADATA_WRITE_FAILED: return "FAT32 metadata write failed";
        case FAT32_FORMAT_VERIFICATION_FAILED: return "FAT32 structure verification failed";
        case FAT32_FORMAT_RESCAN_FAILED: return "The partition table could not be verified after formatting";
        case FAT32_FORMAT_ROLLBACK_FAILED: return "Filesystem state uncertain; rollback could not be verified";
        default: return "Unknown FAT32 format status";
    }
}

const char* fat32_format_stage_name(Fat32FormatStage stage)
{
    switch (stage) {
        case FAT32_FORMAT_STAGE_SNAPSHOT_FILESYSTEM_METADATA: return "SnapshotFilesystemMetadata";
        case FAT32_FORMAT_STAGE_WRITE_FAT: return "WriteFAT";
        case FAT32_FORMAT_STAGE_WRITE_ROOT: return "WriteRoot";
        case FAT32_FORMAT_STAGE_WRITE_BACKUP_METADATA: return "WriteBackupMetadata";
        case FAT32_FORMAT_STAGE_WRITE_FSINFO: return "WriteFSInfo";
        case FAT32_FORMAT_STAGE_WRITE_BOOT_SECTOR: return "WriteBootSector";
        case FAT32_FORMAT_STAGE_FLUSH: return "Flush";
        case FAT32_FORMAT_STAGE_VERIFY: return "Verify";
        case FAT32_FORMAT_STAGE_RESCAN: return "Rescan";
        case FAT32_FORMAT_STAGE_ROLLBACK_WRITE: return "RollbackWrite";
        case FAT32_FORMAT_STAGE_ROLLBACK_FLUSH: return "RollbackFlush";
        case FAT32_FORMAT_STAGE_ROLLBACK_VERIFY: return "RollbackVerify";
        case FAT32_FORMAT_STAGE_ACQUIRE_LEASE: return "AcquireLease";
        case FAT32_FORMAT_STAGE_PIN_TARGET: return "PinTarget";
        case FAT32_FORMAT_STAGE_IDLE: return "Idle";
        case FAT32_FORMAT_STAGE_VALIDATING: return "Validating";
        case FAT32_FORMAT_STAGE_REVALIDATING_PARTITION: return "Revalidating partition";
        case FAT32_FORMAT_STAGE_CALCULATING_LAYOUT: return "Calculating FAT32 layout";
        case FAT32_FORMAT_STAGE_SNAPSHOTTING: return "Snapshotting metadata";
        case FAT32_FORMAT_STAGE_WRITING_METADATA: return "Writing filesystem metadata";
        case FAT32_FORMAT_STAGE_FLUSHING: return "Flushing";
        case FAT32_FORMAT_STAGE_VERIFYING: return "Verifying";
        case FAT32_FORMAT_STAGE_RESCANNING: return "Rescanning";
        case FAT32_FORMAT_STAGE_COMPLETED: return "Completed";
        case FAT32_FORMAT_STAGE_FAILED: return "Failed";
        case FAT32_FORMAT_STAGE_STATE_UNCERTAIN: return "State uncertain";
        default: return "Unknown stage";
    }
}

const char* fat32_existing_state_name(Fat32ExistingState state)
{
    switch (state) {
        case FAT32_EXISTING_CLEAN: return "Unformatted";
        case FAT32_EXISTING_RECOGNIZED_FILESYSTEM: return "Recognized filesystem";
        case FAT32_EXISTING_AMBIGUOUS_DATA: return "Unknown non-zero data";
        case FAT32_EXISTING_UNREADABLE: return "Unreadable";
        case FAT32_EXISTING_UNKNOWN: default: return "Unknown";
    }
}

} // namespace storage
} // namespace kernel
