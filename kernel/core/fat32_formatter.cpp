#include "include/kernel/fat32_formatter.h"
#include "include/kernel/block_device.h"
#include "include/kernel/pit.h"
#if !defined(KERNEL_STORAGE_TEST)
#include "include/kernel/virtio_rng.h"
#endif
#if defined(GXOS_DM23_SCAN_DIAGNOSTICS) && \
    (defined(__GNUC__) || defined(__clang__))
#include "include/kernel/serial_debug.h"
#elif defined(GXOS_DM27_QEMU_QUICK_REFORMAT_PROOF)
#include "include/kernel/serial_debug.h"
#endif

namespace kernel {
namespace storage {

namespace {

static const uint32_t kFat32ReservedSectors = 32;
static const uint32_t kFat32Count = 2;
static const uint32_t kFsInfoSector = 1;
static const uint32_t kBackupBootSector = 6;
static const uint32_t kBackupFsInfoSector = 7;
static const uint32_t kZeroScanBufferBytes =
    FAT32_FORMAT_SCAN_BUFFER_MAX_BYTES;

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
// Independent read-back scratch; keeping it static avoids adding several
// logical-sector-sized arrays to the kernel boot stack.
alignas(4096) static uint8_t s_verifySector[MAX_LOGICAL_SECTOR_SIZE];
// One shared scan buffer is operation-owned scratch. FAT32 probe/format calls
// hold the global storage-operation lease, so scans cannot overlap or reuse
// this buffer concurrently. It never grows with partition size.
alignas(4096) static uint8_t s_zeroScanBuffer[kZeroScanBufferBytes];
static Fat32RollbackRecord s_rollbackRecords[FAT32_FORMAT_MAX_ROLLBACK_ENTRIES];
alignas(4096) static PartitionTableModel s_partitionTable;
static uint32_t s_rollbackRecordCount;
static bool s_quickReadAccountingActive;
static uint32_t s_quickReadRequests;

static void account_quick_read_request()
{
    if (s_quickReadAccountingActive && s_quickReadRequests != UINT32_MAX)
        ++s_quickReadRequests;
}

struct QuickReadAccounting {
    Fat32FormatResult& result;
    QuickReadAccounting(Fat32FormatResult& value) : result(value)
    {
        s_quickReadRequests = 0;
        s_quickReadAccountingActive = true;
    }
    ~QuickReadAccounting()
    {
        result.reformatReadRequests = s_quickReadRequests;
        s_quickReadAccountingActive = false;
    }
};

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

static uint64_t read_u64(const uint8_t* input)
{
    return static_cast<uint64_t>(read_u32(input)) |
        (static_cast<uint64_t>(read_u32(input + 4)) << 32);
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

static void write_u64(uint8_t* output, uint64_t value)
{
    write_u32(output, static_cast<uint32_t>(value));
    write_u32(output + 4, static_cast<uint32_t>(value >> 32));
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
    if (!fat32_format_sector_size_supported(caps.logicalSectorSize))
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
    // The reusable scratch sector holds one complete logical block up to
    // 4096 bytes. A four-sector prefix fits for 512-byte media, while 4Kn
    // must be read as one complete logical sector.
    const uint32_t probeCount = sectorSize == 4096u ? 1u :
        (sectors < 4 ? static_cast<uint32_t>(sectors) : 4u);
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

struct BlankVerificationToken {
    bool valid;
    TargetIdentity target;
    PartitionEntry partition;
    Fat32FormatGeometry geometry;
    uint64_t operationGeneration;
    uint64_t sectorsVerified;
};

// The cooperative formatter is intentionally single-job. Its unforgeable
// KnownZero evidence stays private to this translation unit and is bound to
// the operation owner until commit or cancellation.
static BlankVerificationToken s_jobBlankToken;

static bool buffer_is_zero(const uint8_t* bytes, size_t length,
                           size_t& firstNonzeroOffset)
{
    firstNonzeroOffset = length;
    size_t offset = 0;
    // Check eight bytes per branch. The grouped byte loads avoid alignment
    // and strict-aliasing assumptions while keeping an early exit at the
    // first nonzero word-sized group.
    for (; length - offset >= sizeof(uint64_t); offset += sizeof(uint64_t)) {
        const uint8_t combined = static_cast<uint8_t>(bytes[offset] |
            bytes[offset + 1] | bytes[offset + 2] | bytes[offset + 3] |
            bytes[offset + 4] | bytes[offset + 5] | bytes[offset + 6] |
            bytes[offset + 7]);
        if (combined != 0) {
            for (size_t i = 0; i < sizeof(uint64_t); ++i) {
                if (bytes[offset + i] != 0) {
                    firstNonzeroOffset = offset + i;
                    return false;
                }
            }
        }
    }
    for (; offset < length; ++offset) {
        if (bytes[offset] != 0) {
            firstNonzeroOffset = offset;
            return false;
        }
    }
    return true;
}

static bool same_scan_target(const TargetIdentity& left,
                             const TargetIdentity& right)
{
    return left.registryGeneration == right.registryGeneration &&
        left.registrationId == right.registrationId &&
        left.globalIndex == right.globalIndex &&
        left.transport == right.transport &&
        left.driverIndex == right.driverIndex &&
        left.totalLogicalSectors == right.totalLogicalSectors &&
        left.logicalSectorSize == right.logicalSectorSize;
}

static bool same_scan_partition(const PartitionEntry& left,
                                const PartitionEntry& right)
{
    if (!same_partition_extent(left, right) ||
        left.partitionNumber != right.partitionNumber ||
        left.mbrType != right.mbrType) return false;
    if (left.isGpt)
        return bytes_equal(left.uniqueGuid, right.uniqueGuid, 16) &&
            bytes_equal(left.typeGuid, right.typeGuid, 16);
    return true;
}

static bool same_scan_geometry(const Fat32FormatGeometry& left,
                               const Fat32FormatGeometry& right)
{
    return left.bytesPerSector == right.bytesPerSector &&
        left.sectorsPerCluster == right.sectorsPerCluster &&
        left.clusterSizeBytes == right.clusterSizeBytes &&
        left.reservedSectorCount == right.reservedSectorCount &&
        left.fatCount == right.fatCount &&
        left.fatSizeSectors == right.fatSizeSectors &&
        left.firstFatSector == right.firstFatSector &&
        left.secondFatSector == right.secondFatSector &&
        left.firstDataSector == right.firstDataSector &&
        left.rootCluster == right.rootCluster &&
        left.clusterCount == right.clusterCount &&
        left.freeClusterCount == right.freeClusterCount &&
        left.totalSectors == right.totalSectors &&
        left.fsInfoSector == right.fsInfoSector &&
        left.backupFsInfoSector == right.backupFsInfoSector &&
        left.backupBootSector == right.backupBootSector &&
        // The volume ID is generated after the blank scan and is not part of
        // the scanned on-media geometry.
        left.hiddenSectors == right.hiddenSectors;
}

static bool blank_token_matches(const BlankVerificationToken& token,
                                const Fat32FormatRequest& request,
                                const PartitionEntry& partition,
                                const Fat32FormatGeometry& geometry,
                                const StorageOperationLease& lease)
{
    return token.valid && storage_operation_lease_is_current(lease) &&
        token.operationGeneration == lease.ownerToken &&
        same_scan_target(token.target, request.targetSnapshot) &&
        same_scan_partition(token.partition, partition) &&
        same_scan_geometry(token.geometry, geometry) &&
        token.sectorsVerified == partition.sectorCount;
}

static void finish_scan_metrics(Fat32FormatResult& result,
                                uint64_t startTicks)
{
#if defined(KERNEL_STORAGE_TEST)
    const uint64_t now = startTicks;
#else
    const uint64_t now = kernel::pit::ticks();
#endif
    result.scanElapsedTicks = now >= startTicks ? now - startTicks : 0;
}

static uint64_t scan_clock_ticks()
{
#if defined(KERNEL_STORAGE_TEST)
    return 0;
#else
    return kernel::pit::ticks();
#endif
}

#if defined(GXOS_DM23_SCAN_DIAGNOSTICS) && \
    (defined(__GNUC__) || defined(__clang__))
static const char* scan_transport_name(block::DeviceType type)
{
    switch (type) {
        case block::BDEV_ATA_PIO: return "ATA";
        case block::BDEV_AHCI: return "AHCI";
        case block::BDEV_USB_MASS: return "USB";
        case block::BDEV_NVME: return "NVMe";
        case block::BDEV_RAMDISK: return "RAM";
        default: return "unknown";
    }
}

static void report_scan_progress(const Fat32FormatResult& result,
                                 const TargetIdentity& target)
{
    serial::puts("[DM23-SCAN] transport=");
    serial::puts(scan_transport_name(target.transport));
    serial::puts(" device=");
    serial::puts(target.name);
    serial::puts(" model=");
    serial::puts(target.model);
    serial::puts(" serial=");
    serial::puts(target.serial);
    serial::puts(" lba=");
    serial::put_hex64(result.scanCurrentLba);
    serial::puts(" relative=");
    serial::put_hex64(result.scanRelativeLba);
    serial::puts(" percent=");
    serial::put_hex32(result.scanProgressPercent);
    serial::puts(" bytes=");
    serial::put_hex64(result.scanBytesRead);
    serial::puts(" requests=");
    serial::put_hex32(result.scanReadRequests);
    serial::puts(" minBytes=");
    serial::put_hex32(result.scanSmallestRequestBytes);
    serial::puts(" maxBytes=");
    serial::put_hex32(result.scanLargestRequestBytes);
    serial::puts(" zeroSectors=");
    serial::put_hex64(result.scanZeroVerifiedSectors);
    serial::puts(" coverage=");
    serial::puts(result.scanCoverageComplete ? "complete" : "incomplete");
    serial::puts(" nonzeroRel=");
    serial::put_hex64(result.scanFirstNonzeroRelativeLba);
    serial::puts(" nonzeroByte=");
    serial::put_hex32(result.scanFirstNonzeroByteOffset);
    serial::puts(" elapsedTicks=");
    serial::put_hex64(result.scanElapsedTicks);
    serial::puts(" avgRequestBytes=");
    serial::put_hex32(result.scanReadRequests
        ? static_cast<uint32_t>(result.scanBytesRead /
                                result.scanReadRequests) : 0u);
    const uint64_t milliMiBPerSecond = result.scanElapsedTicks
        ? (result.scanBytesRead * 100000u) /
            (result.scanElapsedTicks * 1048576u) : 0u;
    serial::puts(" milliMiBps=");
    serial::put_hex64(milliMiBPerSecond);
    serial::putc('\n');
}
#endif

static Fat32FormatStatus scan_partition_for_clean_state(
    const Fat32FormatRequest& request, uint32_t sectorSize,
    const Fat32FormatGeometry& geometry,
    const StorageOperationLease& lease,
    Fat32ExistingState& existingState, Fat32FormatResult& result,
    BlankVerificationToken& token)
{
    clear_bytes(&token, sizeof(token));
    const uint64_t startTicks = scan_clock_ticks();
    existingState = classify_existing_prefix(request, sectorSize);
    if (existingState == FAT32_EXISTING_UNREADABLE) {
        finish_scan_metrics(result, startTicks);
        return FAT32_FORMAT_READ_UNAVAILABLE;
    }
    if (existingState == FAT32_EXISTING_RECOGNIZED_FILESYSTEM) {
        finish_scan_metrics(result, startTicks);
        return FAT32_FORMAT_FILESYSTEM_ALREADY_RECOGNIZED;
    }

    const uint64_t total = request.partitionSnapshot.sectorCount;
    uint64_t relative = 0;
    DeviceCapabilities caps;
    if (!query_device_capabilities(request.targetSnapshot.globalIndex, caps)) {
        finish_scan_metrics(result, startTicks);
        return FAT32_FORMAT_DEVICE_MISSING;
    }
    const uint32_t maxSectors = fat32_scan_batch_sectors(
        sectorSize, caps.maxTransferBytes);
    if (maxSectors == 0) {
        finish_scan_metrics(result, startTicks);
        return FAT32_FORMAT_READ_UNAVAILABLE;
    }
    const uint64_t totalBytes = total * static_cast<uint64_t>(sectorSize);
    uint8_t lastReportedPercent = 0;
    while (relative < total) {
        if (!storage_operation_lease_is_current(lease) ||
            !request.targetSnapshot.registrationId ||
            revalidate_target_identity(request.targetSnapshot) != TARGET_VALID) {
            finish_scan_metrics(result, startTicks);
            return FAT32_FORMAT_IDENTITY_CHANGED;
        }
        const uint32_t count = static_cast<uint32_t>(
            total - relative < maxSectors ? total - relative : maxSectors);
        uint64_t lba = 0;
        if (!add_u64(request.partitionSnapshot.startLba, relative, lba) ||
            !checked_lba_range(caps.totalLogicalSectors, lba, count)) {
            finish_scan_metrics(result, startTicks);
            return FAT32_FORMAT_INVALID_GEOMETRY;
        }
        const uint32_t bytes = count * sectorSize;
        ++result.scanReadRequests;
        if (result.scanSmallestRequestBytes == 0 ||
            bytes < result.scanSmallestRequestBytes)
            result.scanSmallestRequestBytes = bytes;
        if (bytes > result.scanLargestRequestBytes)
            result.scanLargestRequestBytes = bytes;
        result.scanCurrentLba = lba;
        result.scanRelativeLba = relative;
        if (read_sectors_safe(request.targetSnapshot.globalIndex, lba, count,
                s_zeroScanBuffer, bytes) != block::BLOCK_OK) {
            existingState = FAT32_EXISTING_UNREADABLE;
            (void)capture_current_io_result(result);
            finish_scan_metrics(result, startTicks);
            return FAT32_FORMAT_READ_UNAVAILABLE;
        }
        result.scanBytesRead += bytes;
        size_t firstNonzeroOffset = 0;
        if (!buffer_is_zero(s_zeroScanBuffer, bytes, firstNonzeroOffset)) {
            const uint64_t nonzeroRelativeLba = relative +
                firstNonzeroOffset / sectorSize;
            result.scanFirstNonzeroRelativeLba = nonzeroRelativeLba;
            result.scanFirstNonzeroByteOffset = static_cast<uint32_t>(
                firstNonzeroOffset % sectorSize);
            result.scanCurrentLba = request.partitionSnapshot.startLba +
                nonzeroRelativeLba;
            result.scanRelativeLba = nonzeroRelativeLba;
            result.scanProgressPercent = totalBytes == 0 ? 100u :
                static_cast<uint8_t>((result.scanBytesRead * 100u) /
                                     totalBytes);
            existingState = FAT32_EXISTING_AMBIGUOUS_DATA;
            set_diagnostic(result,
                "Blank scan found non-zero data; no formatter writes were made.");
            finish_scan_metrics(result, startTicks);
#if defined(GXOS_DM23_SCAN_DIAGNOSTICS) && \
    (defined(__GNUC__) || defined(__clang__))
            report_scan_progress(result, request.targetSnapshot);
#endif
            return FAT32_FORMAT_AMBIGUOUS_EXISTING_DATA;
        }
        if (relative > UINT64_MAX - count ||
            result.scanZeroVerifiedSectors > UINT64_MAX - count) {
            finish_scan_metrics(result, startTicks);
            return FAT32_FORMAT_INVALID_GEOMETRY;
        }
        relative += count;
        result.scanZeroVerifiedSectors += count;
        result.scanRelativeLba = relative;
        result.scanCurrentLba = request.partitionSnapshot.startLba + relative;
        result.scanProgressPercent = totalBytes == 0 ? 100u :
            static_cast<uint8_t>((result.scanBytesRead * 100u) / totalBytes);
#if defined(GXOS_DM23_SCAN_DIAGNOSTICS) && \
    (defined(__GNUC__) || defined(__clang__))
        if (result.scanProgressPercent >=
            static_cast<uint8_t>(lastReportedPercent + 10u)) {
            lastReportedPercent = static_cast<uint8_t>(
                (result.scanProgressPercent / 10u) * 10u);
            report_scan_progress(result, request.targetSnapshot);
        }
#else
        (void)lastReportedPercent;
#endif
    }
    if (relative != total || result.scanZeroVerifiedSectors != total) {
        finish_scan_metrics(result, startTicks);
        return FAT32_FORMAT_INVALID_GEOMETRY;
    }
    result.scanCoverageComplete = true;
    result.scanProgressPercent = 100u;
    result.scanRelativeLba = total;
    result.scanCurrentLba = request.partitionSnapshot.startLba + total;
    existingState = FAT32_EXISTING_CLEAN;
    token.valid = true;
    token.target = request.targetSnapshot;
    token.partition = request.partitionSnapshot;
    token.geometry = geometry;
    token.operationGeneration = lease.ownerToken;
    token.sectorsVerified = result.scanZeroVerifiedSectors;
    finish_scan_metrics(result, startTicks);
#if defined(GXOS_DM23_SCAN_DIAGNOSTICS) && \
    (defined(__GNUC__) || defined(__clang__))
    report_scan_progress(result, request.targetSnapshot);
#endif
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
    account_quick_read_request();
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
                              const char label[11], uint8_t* sector,
                              uint32_t sectorSize)
{
    clear_bytes(sector, sectorSize);
    sector[0] = 0xEB;
    sector[1] = 0x58;
    sector[2] = 0x90;
    copy_bytes(sector + 3, "GUIDEXOS ", 8);
    write_u16(sector + 11,
              static_cast<uint16_t>(geometry.bytesPerSector));
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
                         uint8_t* sector, uint32_t sectorSize)
{
    clear_bytes(sector, sectorSize);
    write_u32(sector + 0, 0x41615252u);
    write_u32(sector + 484, 0x61417272u);
    write_u32(sector + 488, geometry.freeClusterCount);
    write_u32(sector + 492, 3u);
    write_u32(sector + 508, 0xAA550000u);
}

static void build_fat_sector(const Fat32FormatGeometry& geometry,
                             uint8_t* sector, uint32_t sectorSize)
{
    clear_bytes(sector, sectorSize);
    write_u32(sector + 0, 0x0FFFFFF8u);
    write_u32(sector + 4, 0x0FFFFFFFu);
    write_u32(sector + 8, 0x0FFFFFFFu); // Root cluster 2 is allocated to EOC.
    (void)geometry;
}

static void build_root_sector(const char label[11], bool haveLabel,
                              uint8_t* sector, uint32_t sectorSize)
{
    clear_bytes(sector, sectorSize);
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
    const uint32_t sectorSize = planned.bytesPerSector;
    if (!fat32_format_sector_size_supported(sectorSize) ||
        sectorSize > sizeof(s_verifySector) ||
        read_partition_sector(target, partition, planned, 0,
            s_verifySector) != block::BLOCK_OK) return false;

    // Parse the written BPB independently and derive region bounds from disk.
    const uint8_t* primary = s_verifySector;
    const uint32_t bps = read_u16(primary + 11);
    const uint32_t spc = primary[13];
    const uint32_t reserved = read_u16(primary + 14);
    const uint32_t fats = primary[16];
    const uint32_t total = read_u32(primary + 32);
    const uint32_t fatSectors = read_u32(primary + 36);
    const uint32_t rootCluster = read_u32(primary + 44);
    uint64_t fatRegionSectors = 0, firstData = 0;
    if (bps != sectorSize || !fat32_format_sector_size_supported(bps) ||
        spc == 0 || spc > FAT32_FORMAT_MAX_CLUSTER_BYTES / bps ||
        (spc & (spc - 1u)) != 0 || reserved == 0 || fats != 2 ||
        read_u16(primary + 17) != 0 || read_u16(primary + 19) != 0 ||
        read_u16(primary + 22) != 0 || total == 0 ||
        total > partition.sectorCount || fatSectors == 0 || rootCluster < 2 ||
        read_u16(primary + 40) != 0 || read_u16(primary + 42) != 0 ||
        read_u16(primary + 48) != planned.fsInfoSector ||
        read_u16(primary + 50) != planned.backupBootSector ||
        primary[66] != 0x29 || read_u32(primary + 67) == 0 ||
        read_u32(primary + 28) != planned.hiddenSectors ||
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
    const uint32_t diskVolumeId = read_u32(primary + 67);
    uint64_t fatBytes = 0, fatCapacityBytes = 0;
    if (clusters < FAT32_FORMAT_MIN_CLUSTERS ||
        clusters > FAT32_FORMAT_MAX_CLUSTERS ||
        !mul_u64(static_cast<uint64_t>(clusters) + 2u, 4u, fatBytes) ||
        !mul_u64(fatSectors, bps, fatCapacityBytes) ||
        fatBytes > fatCapacityBytes ||
        !bytes_zero(primary + 512, bps - 512u)) return false;

    if (read_partition_sector(target, partition, planned,
            planned.backupBootSector, s_ioSector) != block::BLOCK_OK ||
        !bytes_equal(primary, s_ioSector, bps)) return false;

    // FSInfo retains the standard offsets within the logical sector. Reuse
    // the two bounded static buffers, comparing all bytes in each copy.
    if (read_partition_sector(target, partition, planned,
            planned.fsInfoSector, s_verifySector) != block::BLOCK_OK ||
        read_partition_sector(target, partition, planned,
            planned.backupFsInfoSector, s_ioSector) != block::BLOCK_OK ||
        read_u32(s_verifySector) != 0x41615252u ||
        read_u32(s_verifySector + 484) != 0x61417272u ||
        read_u32(s_verifySector + 488) != clusters - 1 ||
        read_u32(s_verifySector + 492) != 3u ||
        read_u32(s_verifySector + 508) != 0xAA550000u ||
        !bytes_equal(s_verifySector, s_ioSector, bps) ||
        !bytes_zero(s_verifySector + 512, bps - 512u)) return false;

    if (diskVolumeId != planned.volumeId) return false;

    if (!relative_range_valid(planned, reserved, 1) ||
        !relative_range_valid(planned,
            static_cast<uint64_t>(reserved) + fatSectors, 1) ||
        read_partition_sector(target, partition, planned, reserved,
            s_verifySector) != block::BLOCK_OK ||
        read_partition_sector(target, partition, planned,
            static_cast<uint64_t>(reserved) + fatSectors,
            s_ioSector) != block::BLOCK_OK ||
        read_u32(s_verifySector) != 0x0FFFFFF8u ||
        read_u32(s_verifySector + 4) != 0x0FFFFFFFu ||
        read_u32(s_verifySector + 8) != 0x0FFFFFFFu ||
        !bytes_zero(s_verifySector + 12, bps - 12) ||
        !bytes_equal(s_verifySector, s_ioSector, bps)) return false;
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
    return true;
}

static bool rescan_partition(const Fat32FormatRequest& request,
                             const PartitionEntry& expected)
{
#if defined(KERNEL_STORAGE_TEST)
    if (request.testForceRescanFailure) return false;
#endif
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

// DM26's interrupted-format token is deliberately small and stored in an
// otherwise unused reserved sector. It binds retry to the persistent
// partition-table identity; registration incarnations are revalidated by the
// operation lease on every invocation instead of being persisted here.
static const uint32_t kQuickMarkerFirstSector = 8u;
static const uint32_t kQuickMarkerLastSector = 31u;
static const uint32_t kQuickMarkerLegacyVersion = 1u;
static const uint32_t kQuickMarkerVersion = 2u;
static const uint32_t kQuickMarkerChecksumOffset = 120u;
static const uint32_t kQuickMarkerTableFingerprintOffset = 120u;
static const uint32_t kQuickMarkerStateOffset = 124u;
static const uint32_t kQuickMarkerV2ChecksumOffset = 128u;
static const uint32_t kQuickMarkerStatePrepared = 1u;
static const uint32_t kQuickMarkerStatePointOfNoReturn = 2u;
static const char kQuickMarkerMagic[8] = {'G','X','D','M','2','6','R','F'};

#if defined(GXOS_DM27_QEMU_QUICK_REFORMAT_PROOF)
static void quick_reformat_proof_stage(const char* stage)
{
    serial::puts("[DM28-QRF] stage=");
    serial::puts(stage);
    serial::putc('\n');
}
#endif

#if defined(GXOS_DM28_QEMU_REFORMAT_INTERRUPT_PROOF)
static void quick_reformat_interrupt_pause()
{
    // The proof host waits for the stage marker, removes the emulated USB disk,
    // and waits for QEMU's DEVICE_DELETED acknowledgement during this bounded
    // window. Production builds do not include this pause.
    const uint64_t start = pit::ticks();
    while (pit::ticks() - start < 500u)
        __asm__ __volatile__("pause");
}
#endif

struct QuickReformatSource {
    bool retry;
    uint32_t markerSector;
    uint32_t markerVersion;
    uint32_t markerState;
    uint32_t tableFingerprint;
    uint32_t oldBackupBootSector;
    uint32_t oldVolumeId;
    uint32_t oldFsInfoSector;
    uint8_t oldLabel[11];
};

enum QuickWriteKind {
    QUICK_WRITE_RESERVED,
    QUICK_WRITE_FAT1_ZERO,
    QUICK_WRITE_FAT2_ZERO,
    QUICK_WRITE_FAT1_INIT,
    QUICK_WRITE_FAT2_INIT,
    QUICK_WRITE_ROOT,
};

static uint32_t quick_marker_checksum(const uint8_t* bytes, uint32_t count)
{
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < count; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool quick_table_fingerprint(const Fat32FormatRequest& request,
    uint32_t sectorSize, uint32_t& fingerprint)
{
    fingerprint = 0;
    if (!parse_partition_table(request.targetSnapshot.globalIndex,
            s_partitionTable)) return false;
    if (request.partitionScheme == PARTITION_SCHEME_GPT) {
        if (s_partitionTable.state != DISK_STATE_VALID_GPT ||
            !s_partitionTable.primaryGptValid ||
            !s_partitionTable.backupGptValid ||
            !s_partitionTable.gptCopiesAgree ||
            s_partitionTable.primaryGptEntryArrayCrc32 == 0 ||
            s_partitionTable.primaryGptEntryArrayCrc32 !=
                s_partitionTable.backupGptEntryArrayCrc32) return false;
        fingerprint = s_partitionTable.primaryGptEntryArrayCrc32;
        return true;
    }
    if (request.partitionScheme != PARTITION_SCHEME_MBR ||
        sectorSize > sizeof(s_ioSector) ||
        block::read_sectors(request.targetSnapshot.globalIndex, 0, 1,
            s_ioSector) != block::BLOCK_OK) return false;
    fingerprint = crc32(s_ioSector, sectorSize);
    return fingerprint != 0;
}

static bool quick_marker_identity_matches(const uint8_t* marker,
    const Fat32FormatRequest& request, const PartitionEntry& partition,
    uint32_t sectorSize, uint32_t markerSector)
{
    if (!marker || !bytes_equal(marker, reinterpret_cast<const uint8_t*>(
            kQuickMarkerMagic), sizeof(kQuickMarkerMagic)) ||
        read_u32(marker + 12) != static_cast<uint32_t>(request.partitionScheme) ||
        read_u64(marker + 16) != partition.startLba ||
        read_u64(marker + 24) != partition.sectorCount ||
        read_u32(marker + 32) != sectorSize ||
        read_u32(marker + 36) != partition.partitionNumber ||
        read_u32(marker + 116) != markerSector)
        return false;
    const uint32_t version = read_u32(marker + 8);
    if (version == kQuickMarkerLegacyVersion) {
        if (read_u32(marker + kQuickMarkerChecksumOffset) !=
                quick_marker_checksum(marker, kQuickMarkerChecksumOffset))
            return false;
    } else if (version == kQuickMarkerVersion) {
        uint32_t currentFingerprint = 0;
        if (!quick_table_fingerprint(request, sectorSize,
                currentFingerprint) ||
            read_u32(marker + kQuickMarkerTableFingerprintOffset) !=
                currentFingerprint ||
            (read_u32(marker + kQuickMarkerStateOffset) !=
                 kQuickMarkerStatePrepared &&
             read_u32(marker + kQuickMarkerStateOffset) !=
                 kQuickMarkerStatePointOfNoReturn) ||
            read_u32(marker + kQuickMarkerV2ChecksumOffset) !=
                quick_marker_checksum(marker, kQuickMarkerV2ChecksumOffset))
            return false;
    } else {
        return false;
    }
    if (request.partitionScheme == PARTITION_SCHEME_GPT)
        return bytes_equal(marker + 48, partition.uniqueGuid, 16) &&
            bytes_equal(marker + 64, partition.typeGuid, 16) &&
            bytes_equal(marker + 80, request.gptDiskGuid, 16);
    return read_u32(marker + 40) == partition.mbrType &&
        read_u32(marker + 44) == request.mbrDiskSignature &&
        bytes_zero(marker + 48, 48);
}

static void build_quick_marker(uint8_t* sector, uint32_t sectorSize,
    const Fat32FormatRequest& request, const PartitionEntry& partition,
    uint32_t markerSector, const QuickReformatSource& source,
    uint32_t tableFingerprint, uint32_t markerState)
{
    clear_bytes(sector, sectorSize);
    copy_bytes(sector, kQuickMarkerMagic, sizeof(kQuickMarkerMagic));
    write_u32(sector + 8, kQuickMarkerVersion);
    write_u32(sector + 12, static_cast<uint32_t>(request.partitionScheme));
    write_u64(sector + 16, partition.startLba);
    write_u64(sector + 24, partition.sectorCount);
    write_u32(sector + 32, sectorSize);
    write_u32(sector + 36, partition.partitionNumber);
    if (request.partitionScheme == PARTITION_SCHEME_GPT) {
        copy_bytes(sector + 48, partition.uniqueGuid, 16);
        copy_bytes(sector + 64, partition.typeGuid, 16);
        copy_bytes(sector + 80, request.gptDiskGuid, 16);
    } else {
        write_u32(sector + 40, partition.mbrType);
        write_u32(sector + 44, request.mbrDiskSignature);
    }
    write_u32(sector + 96, source.oldBackupBootSector);
    write_u32(sector + 100, source.oldVolumeId);
    copy_bytes(sector + 104, source.oldLabel, sizeof(source.oldLabel));
    write_u32(sector + 116, markerSector);
    write_u32(sector + kQuickMarkerTableFingerprintOffset, tableFingerprint);
    write_u32(sector + kQuickMarkerStateOffset, markerState);
    write_u32(sector + kQuickMarkerV2ChecksumOffset,
        quick_marker_checksum(sector, kQuickMarkerV2ChecksumOffset));
}

static bool read_quick_marker(const Fat32FormatRequest& request,
    const PartitionEntry& partition, const Fat32FormatGeometry& geometry,
    QuickReformatSource& source)
{
    for (uint32_t sector = kQuickMarkerFirstSector;
         sector <= kQuickMarkerLastSector; ++sector) {
        if (read_partition_sector(request.targetSnapshot, partition, geometry,
                sector, s_ioSector) != block::BLOCK_OK) continue;
        if (!quick_marker_identity_matches(s_ioSector, request, partition,
                geometry.bytesPerSector, sector)) continue;
        source.retry = true;
        source.markerSector = sector;
        source.markerVersion = read_u32(s_ioSector + 8);
        source.markerState = source.markerVersion == kQuickMarkerVersion
            ? read_u32(s_ioSector + kQuickMarkerStateOffset)
            : kQuickMarkerStatePrepared;
        source.tableFingerprint = source.markerVersion ==
                kQuickMarkerVersion
            ? read_u32(s_ioSector + kQuickMarkerTableFingerprintOffset) : 0;
        source.oldBackupBootSector = read_u32(s_ioSector + 96);
        source.oldVolumeId = read_u32(s_ioSector + 100);
        copy_bytes(source.oldLabel, s_ioSector + 104,
                   sizeof(source.oldLabel));
        source.oldFsInfoSector = 0;
        return true;
    }
    return false;
}

static bool valid_old_fat32_primary(const uint8_t* boot,
    uint32_t sectorSize, const PartitionEntry& partition,
    uint32_t& backupBootSector, uint32_t& fsInfoSector,
    uint32_t& volumeId, uint8_t label[11])
{
    if (!boot || read_u16(boot + 11) != sectorSize ||
        !fat32_format_sector_size_supported(sectorSize) ||
        boot[510] != 0x55 || boot[511] != 0xAA ||
        !ascii_eq(boot + 82, "FAT32   ", 8)) return false;
    const uint32_t spc = boot[13];
    const uint32_t reserved = read_u16(boot + 14);
    const uint32_t fats = boot[16];
    const uint32_t total = read_u32(boot + 32);
    const uint32_t fatSectors = read_u32(boot + 36);
    const uint32_t rootCluster = read_u32(boot + 44);
    const uint64_t clustersFirstData = static_cast<uint64_t>(reserved) +
        2ull * fatSectors;
    if (spc == 0 || (spc & (spc - 1u)) != 0 ||
        static_cast<uint64_t>(spc) * sectorSize > FAT32_FORMAT_MAX_CLUSTER_BYTES ||
        reserved < 32u || fats != 2u || read_u16(boot + 17) != 0 ||
        read_u16(boot + 19) != 0 || read_u16(boot + 22) != 0 ||
        read_u16(boot + 40) != 0 || read_u16(boot + 42) != 0 ||
        total == 0 || total > partition.sectorCount || fatSectors == 0 ||
        clustersFirstData >= total || rootCluster < 2 ||
        rootCluster - 2u >= (total - clustersFirstData) / spc)
        return false;
    const uint32_t clusters = (total - clustersFirstData) / spc;
    const uint64_t fatEntries = static_cast<uint64_t>(fatSectors) *
        sectorSize / 4u;
    if (clusters < FAT32_FORMAT_MIN_CLUSTERS ||
        clusters > FAT32_FORMAT_MAX_CLUSTERS || fatEntries < clusters + 2u)
        return false;
    backupBootSector = read_u16(boot + 50);
    fsInfoSector = read_u16(boot + 48);
    // DM26 writes the new geometry's 32-sector reserved region. Requiring the
    // old backup to lie there prevents invalidation from touching old ordinary
    // data that becomes a data sector in the new layout.
    if (backupBootSector == 0 || backupBootSector >= 32u ||
        backupBootSector >= reserved || backupBootSector == 0xFFFFu ||
        (fsInfoSector != 0 && fsInfoSector != 0xFFFFu &&
         fsInfoSector >= reserved)) return false;
    volumeId = boot[66] == 0x29 ? read_u32(boot + 67) : 0;
    copy_bytes(label, boot + 71, 11);
    return true;
}

static bool old_boot_copies_match(const uint8_t* primary,
                                  const uint8_t* backup)
{
    static const uint8_t bpbRanges[][2] = {
        {11, 25}, {28, 52}, {64, 90}
    };
    if (!primary || !backup || backup[510] != 0x55 || backup[511] != 0xAA)
        return false;
    for (uint32_t r = 0; r < sizeof(bpbRanges) / sizeof(bpbRanges[0]); ++r)
        if (!bytes_equal(primary + bpbRanges[r][0],
                backup + bpbRanges[r][0], bpbRanges[r][1] - bpbRanges[r][0]))
            return false;
    return true;
}

static Fat32FormatStatus inspect_quick_reformat_source(
    const Fat32FormatRequest& request, const PartitionCheck& check,
    QuickReformatSource& source)
{
    clear_bytes(&source, sizeof(source));
    const uint32_t sectorSize = check.geometry.bytesPerSector;
    if (read_partition_sector(request.targetSnapshot, check.currentPartition,
            check.geometry, 0, s_verifySector) != block::BLOCK_OK)
        return FAT32_FORMAT_READ_UNAVAILABLE;
    uint32_t backupBoot = 0, fsInfo = 0, volumeId = 0;
    uint8_t label[11];
    const bool primaryValid = valid_old_fat32_primary(s_verifySector,
        sectorSize, check.currentPartition, backupBoot, fsInfo, volumeId,
        label);
    bool copiesMatch = false;
    if (primaryValid && read_partition_sector(request.targetSnapshot,
            check.currentPartition, check.geometry, backupBoot,
            s_ioSector) == block::BLOCK_OK)
        copiesMatch = old_boot_copies_match(s_verifySector, s_ioSector);
    if (primaryValid && copiesMatch) {
        source.oldBackupBootSector = backupBoot;
        source.oldFsInfoSector = fsInfo;
        source.oldVolumeId = volumeId;
        copy_bytes(source.oldLabel, label, sizeof(source.oldLabel));
        return FAT32_FORMAT_READY;
    }
    if (!read_quick_marker(request, check.currentPartition, check.geometry,
            source)) return FAT32_FORMAT_REFORMAT_TARGET_UNSUPPORTED;
    if (source.oldBackupBootSector == 0 ||
        source.oldBackupBootSector >= 32u ||
        source.oldBackupBootSector >= check.currentPartition.sectorCount ||
        source.oldBackupBootSector == source.markerSector)
        return FAT32_FORMAT_REFORMAT_MARKER_FAILED;
    return FAT32_FORMAT_READY;
}

static bool quick_write_range(const Fat32FormatRequest& request,
    const PartitionCheck& check, const StorageOperationLease& lease,
    uint64_t relative, uint32_t sectors, const uint8_t* buffer,
    QuickWriteKind kind, Fat32FormatResult& result)
{
    const uint32_t sectorSize = check.geometry.bytesPerSector;
    if (!buffer || !relative_range_valid(check.geometry, relative, sectors) ||
        !rollback_context_valid(request, request.targetSnapshot,
            check.currentPartition, check.geometry, lease)) {
        result.status = FAT32_FORMAT_PARTITION_IDENTITY_CHANGED;
        return false;
    }
    uint64_t absolute = 0;
    if (!add_u64(check.currentPartition.startLba, relative, absolute) ||
        !checked_lba_range(request.targetSnapshot.totalLogicalSectors,
                           absolute, sectors)) {
        result.status = FAT32_FORMAT_INVALID_GEOMETRY;
        return false;
    }
    ++result.reformatWriteRequests;
    const block::Status writeStatus = write_sectors_safe(
        request.targetSnapshot.globalIndex, absolute, sectors, buffer,
        buffer == s_zeroScanBuffer ? sizeof(s_zeroScanBuffer)
                                   : sizeof(s_ioSector));
    if (writeStatus != block::BLOCK_OK) {
        result.status = FAT32_FORMAT_METADATA_WRITE_FAILED;
        if (capture_current_io_result(result)) {
            const bool submitted = result.failedBlockDiagnostic.callbackInvoked;
            const bool mayHaveReached = submitted &&
                (!result.failedBlockDiagnostic.transportDiagnostic.valid ||
                 result.failedBlockDiagnostic.transportDiagnostic.dataSectorsTransferred != 0);
            result.writeAttempted = result.writeAttempted || submitted;
            result.writeMayHaveReachedMedia = result.writeMayHaveReachedMedia ||
                mayHaveReached;
            if (submitted && result.reformatState ==
                    FAT32_REFORMAT_BEFORE_DESTRUCTIVE_COMMIT)
                result.reformatState = FAT32_REFORMAT_IN_PROGRESS;
        }
        result.failedBeforeWrite = !result.writeMayHaveReachedMedia;
        return false;
    }
    const uint64_t bytes = static_cast<uint64_t>(sectors) * sectorSize;
    result.writeAttempted = true;
    result.writeMayHaveReachedMedia = true;
    result.failedBeforeWrite = false;
    result.sectorsWritten += sectors;
    result.reformatBytesWritten += bytes;
    if (result.reformatState == FAT32_REFORMAT_BEFORE_DESTRUCTIVE_COMMIT)
        result.reformatState = FAT32_REFORMAT_IN_PROGRESS;
    switch (kind) {
        case QUICK_WRITE_FAT1_ZERO: result.fat1BytesCleared += bytes; break;
        case QUICK_WRITE_FAT2_ZERO: result.fat2BytesCleared += bytes; break;
        case QUICK_WRITE_ROOT: result.rootClusterBytesWritten += bytes; break;
        case QUICK_WRITE_RESERVED: result.reservedBytesWritten += bytes; break;
        default: break;
    }
    return true;
}

static bool quick_zero_range(const Fat32FormatRequest& request,
    const PartitionCheck& check, const StorageOperationLease& lease,
    uint64_t relative, uint32_t sectors, QuickWriteKind kind,
    uint32_t maxBatchSectors, Fat32FormatResult& result)
{
    uint32_t left = sectors;
    uint64_t cursor = relative;
    while (left != 0) {
        const uint32_t batch = left < maxBatchSectors ? left : maxBatchSectors;
        if (!quick_write_range(request, check, lease, cursor, batch,
                s_zeroScanBuffer, kind, result)) return false;
        cursor += batch;
        left -= batch;
    }
    return true;
}

static bool quick_verify_fats(const Fat32FormatRequest& request,
    const PartitionCheck& check, const StorageOperationLease& lease,
    uint32_t maxBatchSectors)
{
    const uint32_t bps = check.geometry.bytesPerSector;
    const uint64_t starts[2] = {check.geometry.firstFatSector,
                                check.geometry.secondFatSector};
    for (uint32_t copy = 0; copy < 2; ++copy) {
        uint32_t left = check.geometry.fatSizeSectors;
        uint64_t cursor = starts[copy];
        uint32_t fatSector = 0;
        while (left != 0) {
            const uint32_t batch = left < maxBatchSectors ? left : maxBatchSectors;
            uint64_t absolute = 0;
            account_quick_read_request();
            if (!rollback_context_valid(request, request.targetSnapshot,
                    check.currentPartition, check.geometry, lease) ||
                !add_u64(check.currentPartition.startLba, cursor, absolute) ||
                block::read_sectors_checked(request.targetSnapshot.globalIndex,
                    absolute, batch, s_zeroScanBuffer,
                    FAT32_FORMAT_SCAN_BUFFER_MAX_BYTES) != block::BLOCK_OK)
                return false;
            const uint64_t bytes64 = static_cast<uint64_t>(batch) * bps;
            if (bytes64 > FAT32_FORMAT_SCAN_BUFFER_MAX_BYTES) return false;
            const size_t bytes = static_cast<size_t>(bytes64);
            if (fatSector == 0) {
                if (read_u32(s_zeroScanBuffer) != 0x0FFFFFF8u ||
                    read_u32(s_zeroScanBuffer + 4) != 0x0FFFFFFFu ||
                    read_u32(s_zeroScanBuffer + 8) != 0x0FFFFFFFu ||
                    !bytes_zero(s_zeroScanBuffer + 12, bytes - 12u))
                    return false;
            } else if (!bytes_zero(s_zeroScanBuffer, bytes)) {
                return false;
            }
            fatSector += batch;
            cursor += batch;
            left -= batch;
        }
    }
    return true;
}

static bool quick_verify_reserved_and_root(const Fat32FormatRequest& request,
    const PartitionCheck& check, const StorageOperationLease& lease,
    const char label[11], uint32_t markerSector)
{
    const uint32_t bps = check.geometry.bytesPerSector;
    for (uint32_t sector = 1; sector < check.geometry.reservedSectorCount;
         ++sector) {
        if (sector == check.geometry.fsInfoSector ||
            sector == check.geometry.backupBootSector ||
            sector == check.geometry.backupFsInfoSector ||
            sector == markerSector) continue;
        if (!rollback_context_valid(request, request.targetSnapshot,
                check.currentPartition, check.geometry, lease) ||
            read_partition_sector(request.targetSnapshot,
                check.currentPartition, check.geometry, sector,
                s_verifySector) != block::BLOCK_OK ||
            !bytes_zero(s_verifySector, bps)) return false;
    }
    uint64_t rootSector = 0;
    if (!cluster_relative_range(check.geometry, check.geometry.rootCluster,
                                rootSector)) return false;
    const bool haveLabel = label[0] != ' ';
    for (uint32_t i = 0; i < check.geometry.sectorsPerCluster; ++i) {
        uint64_t relative = 0;
        if (!add_u64(rootSector, i, relative) ||
            read_partition_sector(request.targetSnapshot,
                check.currentPartition, check.geometry, relative,
                s_verifySector) != block::BLOCK_OK) return false;
        if (i == 0 && haveLabel) {
            if (!bytes_equal(s_verifySector,
                    reinterpret_cast<const uint8_t*>(label), 11) ||
                s_verifySector[11] != 0x08 ||
                !bytes_zero(s_verifySector + 12, bps - 12u)) return false;
        } else if (!bytes_zero(s_verifySector, bps)) return false;
    }
    return true;
}

static bool quick_verify_prepublication(const Fat32FormatRequest& request,
    const PartitionCheck& check, const StorageOperationLease& lease,
    const char label[11], uint32_t markerSector, uint32_t maxBatchSectors)
{
    const uint32_t bps = check.geometry.bytesPerSector;
    build_boot_sector(check.geometry, label, s_verifySector, bps);
    if (read_partition_sector(request.targetSnapshot, check.currentPartition,
            check.geometry, check.geometry.backupBootSector, s_ioSector) !=
            block::BLOCK_OK || !bytes_equal(s_verifySector, s_ioSector, bps))
        return false;
    build_fsinfo(check.geometry, s_verifySector, bps);
    if (read_partition_sector(request.targetSnapshot, check.currentPartition,
            check.geometry, check.geometry.fsInfoSector, s_ioSector) !=
            block::BLOCK_OK || !bytes_equal(s_verifySector, s_ioSector, bps) ||
        read_partition_sector(request.targetSnapshot, check.currentPartition,
            check.geometry, check.geometry.backupFsInfoSector, s_ioSector) !=
            block::BLOCK_OK || !bytes_equal(s_verifySector, s_ioSector, bps))
        return false;
    return quick_verify_fats(request, check, lease, maxBatchSectors) &&
        quick_verify_reserved_and_root(request, check, lease, label,
                                       markerSector);
}

static Fat32FormatStatus quick_reformat_preflight_locked(
    const Fat32FormatRequest& request, Fat32FormatResult& result,
    PartitionCheck& check, QuickReformatSource& source)
{
    const Fat32FormatStatus status = validate_request_and_partition(
        request, result, check);
    if (status != FAT32_FORMAT_READY) return status;
    const Fat32FormatStatus sourceStatus = inspect_quick_reformat_source(
        request, check, source);
    if (sourceStatus != FAT32_FORMAT_READY) return sourceStatus;
    result.existingState = source.retry
        ? FAT32_EXISTING_INTERRUPTED_REFORMAT
        : FAT32_EXISTING_RECOGNIZED_FILESYSTEM;
    result.reformatRetry = source.retry;
    result.oldVolumeId = source.oldVolumeId;
    uint32_t labelLength = 11;
    while (labelLength != 0 && source.oldLabel[labelLength - 1] == ' ')
        --labelLength;
    for (uint32_t i = 0; i < labelLength; ++i)
        result.oldVolumeLabel[i] = static_cast<char>(source.oldLabel[i]);
    result.oldVolumeLabel[labelLength] = '\0';
    result.reformatState = FAT32_REFORMAT_BEFORE_DESTRUCTIVE_COMMIT;
    return FAT32_FORMAT_READY;
}

static Fat32FormatStatus execute_quick_reformat_locked(
    const Fat32FormatRequest& request, Fat32FormatResult& result,
    StorageOperationLease& lease)
{
    QuickReadAccounting readAccounting(result);
    const uint64_t startTicks = scan_clock_ticks();
    PartitionCheck check = {};
    QuickReformatSource source = {};
    result.stage = FAT32_FORMAT_STAGE_REVALIDATING_PARTITION;
    Fat32FormatStatus status = quick_reformat_preflight_locked(
        request, result, check, source);
    if (status != FAT32_FORMAT_READY) return status;
    char label[11];
    status = normalize_fat32_volume_label(request.volumeLabel, label);
    if (status != FAT32_FORMAT_READY) return status;
    uint32_t volumeId = 0;
    if (!make_volume_id(request, volumeId)) return FAT32_FORMAT_VOLUME_ID_UNAVAILABLE;
    if (volumeId == source.oldVolumeId) {
        volumeId ^= 0xA5A5A5A5u;
        if (volumeId == 0) volumeId = 1;
    }
    check.geometry.volumeId = volumeId;
    result.geometry = check.geometry;

    // Revalidate the exact disk registration, table identity, mount/root/boot
    // state, bounds, and current FAT32-or-marker state immediately before the
    // first destructive write.
    PartitionCheck finalCheck = {};
    QuickReformatSource finalSource = {};
    status = quick_reformat_preflight_locked(request, result, finalCheck,
                                              finalSource);
    if (status != FAT32_FORMAT_READY ||
        !storage_operation_lease_is_current(lease) ||
        !same_partition_extent(check.currentPartition,
                               finalCheck.currentPartition) ||
        !same_scan_geometry(check.geometry, finalCheck.geometry))
        return status == FAT32_FORMAT_READY
            ? FAT32_FORMAT_PARTITION_IDENTITY_CHANGED : status;
    check = finalCheck;
    source = finalSource;
    check.geometry.volumeId = volumeId;
    result.geometry = check.geometry;

    const DeviceCapabilities& caps = check.capabilities;
    uint32_t tableFingerprint = source.tableFingerprint;
    if (source.retry && source.markerVersion == kQuickMarkerLegacyVersion &&
        !quick_table_fingerprint(request, check.geometry.bytesPerSector,
            tableFingerprint))
        return FAT32_FORMAT_REFORMAT_MARKER_FAILED;
    const uint32_t maxBatchSectors = fat32_scan_batch_sectors(
        check.geometry.bytesPerSector, caps.maxTransferBytes);
    if (maxBatchSectors == 0) return FAT32_FORMAT_INVALID_GEOMETRY;
    clear_bytes(s_zeroScanBuffer, sizeof(s_zeroScanBuffer));
    if (!begin_storage_operation_execution(lease))
        return FAT32_FORMAT_OPERATION_OWNERSHIP_INVALID;

    bool ok = true;
    bool primaryPublicationAttempted = false;
    uint32_t markerSector = source.markerSector;
    if (!source.retry) {
        markerSector = kQuickMarkerLastSector;
        while (markerSector >= kQuickMarkerFirstSector &&
               (markerSector == source.oldBackupBootSector ||
                markerSector == source.oldFsInfoSector)) --markerSector;
        if (markerSector < kQuickMarkerFirstSector) {
            result.status = FAT32_FORMAT_REFORMAT_MARKER_FAILED;
            ok = false;
        }
        if (ok && !quick_table_fingerprint(request,
                check.geometry.bytesPerSector, tableFingerprint)) {
            result.status = FAT32_FORMAT_REFORMAT_MARKER_FAILED;
            ok = false;
        }
    }

    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_BACKUP_METADATA;
        result.lastStage = result.stage;
#if defined(GXOS_DM27_QEMU_QUICK_REFORMAT_PROOF)
        quick_reformat_proof_stage("QRF_INVALIDATION_BEGIN");
#endif
        build_quick_marker(s_ioSector, check.geometry.bytesPerSector,
            request, check.currentPartition, markerSector, source,
            tableFingerprint, kQuickMarkerStatePrepared);
        ok = quick_write_range(request, check, lease, markerSector, 1,
            s_ioSector, QUICK_WRITE_RESERVED, result);
    }
    // Invalidate the previous backup first and primary second. Both changes
    // are flushed and independently reread before either FAT copy is cleared.
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_BACKUP_METADATA;
        result.lastStage = result.stage;
        if (read_partition_sector(request.targetSnapshot,
                check.currentPartition, check.geometry,
                source.oldBackupBootSector, s_ioSector) != block::BLOCK_OK) {
            result.status = FAT32_FORMAT_READ_UNAVAILABLE;
            ok = false;
        } else {
            s_ioSector[510] = 0;
            s_ioSector[511] = 0;
            ok = quick_write_range(request, check, lease,
                source.oldBackupBootSector, 1, s_ioSector,
                QUICK_WRITE_RESERVED, result);
        }
    }
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_BOOT_SECTOR;
        result.lastStage = result.stage;
        if (read_partition_sector(request.targetSnapshot,
                check.currentPartition, check.geometry, 0,
                s_ioSector) != block::BLOCK_OK) {
            result.status = FAT32_FORMAT_READ_UNAVAILABLE;
            ok = false;
        } else {
            s_ioSector[510] = 0;
            s_ioSector[511] = 0;
            ok = quick_write_range(request, check, lease, 0, 1, s_ioSector,
                QUICK_WRITE_RESERVED, result);
        }
    }
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_FLUSH;
        result.lastStage = result.stage;
        result.flushAttempted = true;
        ++result.flushAttempts;
        const block::FlushReport flush =
            block::flush_with_result(request.targetSnapshot.globalIndex);
        result.flushOutcome = flush.outcome;
        result.flushStatus = flush.status;
        if (!trusted_flush(flush)) {
            result.failedOperation = block::OPERATION_FLUSH;
            (void)capture_current_io_result(result);
            result.status = flush.semanticsKnown ? FAT32_FORMAT_FLUSH_FAILED
                                                  : FAT32_FORMAT_FLUSH_UNAVAILABLE;
            ok = false;
        } else {
            result.reformatInvalidationFlushPassed = true;
            result.finalProbeState = FAT32_FINAL_PROBE_UNFORMATTED;
            if (read_partition_sector(request.targetSnapshot,
                    check.currentPartition, check.geometry, 0,
                    s_verifySector) != block::BLOCK_OK ||
                s_verifySector[510] == 0x55 || s_verifySector[511] == 0xAA ||
                read_partition_sector(request.targetSnapshot,
                    check.currentPartition, check.geometry,
                    source.oldBackupBootSector, s_verifySector) != block::BLOCK_OK ||
                s_verifySector[510] == 0x55 || s_verifySector[511] == 0xAA ||
                read_partition_sector(request.targetSnapshot,
                    check.currentPartition, check.geometry, markerSector,
                    s_verifySector) != block::BLOCK_OK ||
                !quick_marker_identity_matches(s_verifySector, request,
                    check.currentPartition, check.geometry.bytesPerSector,
                    markerSector)) {
                result.status = FAT32_FORMAT_VERIFICATION_FAILED;
                ok = false;
            }
        }
    }
    // Persist an explicit point-of-no-return state only after both old boot
    // copies are invalid and that invalidation has reached durable media.
    if (ok) {
        build_quick_marker(s_ioSector, check.geometry.bytesPerSector,
            request, check.currentPartition, markerSector, source,
            tableFingerprint, kQuickMarkerStatePointOfNoReturn);
        ok = quick_write_range(request, check, lease, markerSector, 1,
            s_ioSector, QUICK_WRITE_RESERVED, result);
    }
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_FLUSH;
        result.lastStage = result.stage;
        result.flushAttempted = true;
        ++result.flushAttempts;
        const block::FlushReport markerFlush =
            block::flush_with_result(request.targetSnapshot.globalIndex);
        result.flushOutcome = markerFlush.outcome;
        result.flushStatus = markerFlush.status;
        if (!trusted_flush(markerFlush)) {
            result.failedOperation = block::OPERATION_FLUSH;
            (void)capture_current_io_result(result);
            result.status = markerFlush.semanticsKnown
                ? FAT32_FORMAT_FLUSH_FAILED : FAT32_FORMAT_FLUSH_UNAVAILABLE;
            ok = false;
        }
    }
    if (ok && (read_partition_sector(request.targetSnapshot,
            check.currentPartition, check.geometry, markerSector,
            s_verifySector) != block::BLOCK_OK ||
        !quick_marker_identity_matches(s_verifySector, request,
            check.currentPartition, check.geometry.bytesPerSector,
            markerSector) ||
        read_u32(s_verifySector + kQuickMarkerStateOffset) !=
            kQuickMarkerStatePointOfNoReturn)) {
        result.failedOperation = block::OPERATION_READ;
        (void)capture_current_io_result(result);
        result.status = FAT32_FORMAT_VERIFICATION_FAILED;
        ok = false;
    }
#if defined(GXOS_DM27_QEMU_QUICK_REFORMAT_PROOF)
    if (ok) quick_reformat_proof_stage("QRF_INVALIDATION_DURABLE");
#endif

    // Clear the complete new reserved region except the primary publication
    // sector and the retry marker. The marker remains until the new primary
    // and all supporting structures have been durably verified.
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_BACKUP_METADATA;
        result.lastStage = result.stage;
        if (markerSector > 1)
            ok = quick_zero_range(request, check, lease, 1,
                markerSector - 1u, QUICK_WRITE_RESERVED, maxBatchSectors, result);
        const uint32_t afterMarker = markerSector + 1u;
        if (ok && afterMarker < check.geometry.reservedSectorCount)
            ok = quick_zero_range(request, check, lease, afterMarker,
                check.geometry.reservedSectorCount - afterMarker,
                QUICK_WRITE_RESERVED, maxBatchSectors, result);
    }
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_FAT;
        result.lastStage = result.stage;
#if defined(GXOS_DM27_QEMU_QUICK_REFORMAT_PROOF)
        quick_reformat_proof_stage("QRF_FAT1_BEGIN");
#endif
#if defined(GXOS_DM28_QEMU_REFORMAT_INTERRUPT_PROOF)
        quick_reformat_interrupt_pause();
#endif
        ok = quick_zero_range(request, check, lease,
            check.geometry.firstFatSector, check.geometry.fatSizeSectors,
            QUICK_WRITE_FAT1_ZERO, maxBatchSectors, result);
    }
#if defined(GXOS_DM27_QEMU_QUICK_REFORMAT_PROOF)
    if (ok) quick_reformat_proof_stage("QRF_FAT1_COMPLETE");
#endif
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_FAT;
        result.lastStage = result.stage;
#if defined(GXOS_DM27_QEMU_QUICK_REFORMAT_PROOF)
        quick_reformat_proof_stage("QRF_FAT2_BEGIN");
#endif
        ok = quick_zero_range(request, check, lease,
            check.geometry.secondFatSector, check.geometry.fatSizeSectors,
            QUICK_WRITE_FAT2_ZERO, maxBatchSectors, result);
    }
#if defined(GXOS_DM27_QEMU_QUICK_REFORMAT_PROOF)
    if (ok) quick_reformat_proof_stage("QRF_FAT2_COMPLETE");
#endif
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_FAT;
        result.lastStage = result.stage;
        build_fat_sector(check.geometry, s_ioSector,
                         check.geometry.bytesPerSector);
        ok = quick_write_range(request, check, lease,
            check.geometry.firstFatSector, 1, s_ioSector,
            QUICK_WRITE_FAT1_INIT, result);
        if (ok) ok = quick_write_range(request, check, lease,
            check.geometry.secondFatSector, 1, s_ioSector,
            QUICK_WRITE_FAT2_INIT, result);
    }
    uint64_t rootSector = 0;
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_ROOT;
        result.lastStage = result.stage;
        ok = cluster_relative_range(check.geometry,
            check.geometry.rootCluster, rootSector) &&
            quick_zero_range(request, check, lease, rootSector,
                check.geometry.sectorsPerCluster, QUICK_WRITE_ROOT,
                maxBatchSectors, result);
        if (!ok && result.status == FAT32_FORMAT_INVALID_REQUEST)
            result.status = FAT32_FORMAT_INVALID_GEOMETRY;
        if (ok && label[0] != ' ') {
            build_root_sector(label, true, s_ioSector,
                              check.geometry.bytesPerSector);
            ok = quick_write_range(request, check, lease, rootSector, 1,
                s_ioSector, QUICK_WRITE_ROOT, result);
        }
    }
#if defined(GXOS_DM27_QEMU_QUICK_REFORMAT_PROOF)
    if (ok) quick_reformat_proof_stage("QRF_ROOT_COMPLETE");
#endif
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_FSINFO;
        result.lastStage = result.stage;
        build_fsinfo(check.geometry, s_ioSector,
                     check.geometry.bytesPerSector);
        ok = quick_write_range(request, check, lease,
            check.geometry.fsInfoSector, 1, s_ioSector,
            QUICK_WRITE_RESERVED, result);
        if (ok) ok = quick_write_range(request, check, lease,
            check.geometry.backupFsInfoSector, 1, s_ioSector,
            QUICK_WRITE_RESERVED, result);
    }
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_BACKUP_METADATA;
        result.lastStage = result.stage;
        build_boot_sector(check.geometry, label, s_ioSector,
                          check.geometry.bytesPerSector);
        ok = quick_write_range(request, check, lease,
            check.geometry.backupBootSector, 1, s_ioSector,
            QUICK_WRITE_RESERVED, result);
    }
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_FLUSH;
        result.lastStage = result.stage;
        result.flushAttempted = true;
        ++result.flushAttempts;
        const block::FlushReport flush =
            block::flush_with_result(request.targetSnapshot.globalIndex);
        result.flushOutcome = flush.outcome;
        result.flushStatus = flush.status;
        if (!trusted_flush(flush)) {
            result.failedOperation = block::OPERATION_FLUSH;
            (void)capture_current_io_result(result);
            result.status = flush.semanticsKnown ? FAT32_FORMAT_FLUSH_FAILED
                                                  : FAT32_FORMAT_FLUSH_UNAVAILABLE;
            ok = false;
        }
    }
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_VERIFY;
        result.lastStage = result.stage;
        ok = quick_verify_prepublication(request, check, lease, label,
            markerSector, maxBatchSectors);
        if (!ok) {
            result.failedOperation = block::OPERATION_READ;
            (void)capture_current_io_result(result);
            result.status = FAT32_FORMAT_VERIFICATION_FAILED;
        }
    }
    if (ok) {
        // The primary BPB is the final filesystem-authority publication.
        result.stage = FAT32_FORMAT_STAGE_WRITE_BOOT_SECTOR;
        result.lastStage = result.stage;
#if defined(GXOS_DM27_QEMU_QUICK_REFORMAT_PROOF)
        quick_reformat_proof_stage("QRF_PUBLICATION_BEGIN");
#endif
        build_boot_sector(check.geometry, label, s_ioSector,
                          check.geometry.bytesPerSector);
        primaryPublicationAttempted = true;
        ok = quick_write_range(request, check, lease, 0, 1, s_ioSector,
            QUICK_WRITE_RESERVED, result);
#if defined(GXOS_DM27_QEMU_QUICK_REFORMAT_PROOF)
        if (ok) quick_reformat_proof_stage("QRF_PRIMARY_WRITTEN");
#endif
    }
    if (ok) {
        result.reformatState = FAT32_REFORMAT_NEW_FILESYSTEM_WRITTEN_NOT_DURABLE;
        result.stage = FAT32_FORMAT_STAGE_FLUSH;
        result.lastStage = result.stage;
        result.flushAttempted = true;
        ++result.flushAttempts;
        const block::FlushReport flush =
            block::flush_with_result(request.targetSnapshot.globalIndex);
        result.flushOutcome = flush.outcome;
        result.flushStatus = flush.status;
        result.persistenceTrusted = trusted_flush(flush);
#if defined(GXOS_DM27_QEMU_QUICK_REFORMAT_PROOF)
        if (result.persistenceTrusted)
            quick_reformat_proof_stage("QRF_FINAL_FLUSH_COMPLETE");
#endif
        if (!result.persistenceTrusted) {
            result.failedOperation = block::OPERATION_FLUSH;
            (void)capture_current_io_result(result);
            result.status = flush.semanticsKnown ? FAT32_FORMAT_FLUSH_FAILED
                                                  : FAT32_FORMAT_FLUSH_UNAVAILABLE;
            ok = false;
        }
    }
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_VERIFY;
        result.lastStage = result.stage;
        if (!verify_fat32_structure(request.targetSnapshot,
                check.currentPartition, check.geometry, label) ||
            !quick_verify_fats(request, check, lease, maxBatchSectors)) {
            result.failedOperation = block::OPERATION_READ;
            (void)capture_current_io_result(result);
            result.status = FAT32_FORMAT_VERIFICATION_FAILED;
            ok = false;
        } else {
            result.verificationPassed = true;
            result.finalProbeState = FAT32_FINAL_PROBE_FAT32;
        }
    }
    if (ok) {
        // Remove the retry marker only after the fresh filesystem is durable.
        clear_bytes(s_ioSector, check.geometry.bytesPerSector);
        ok = quick_write_range(request, check, lease, markerSector, 1,
            s_ioSector, QUICK_WRITE_RESERVED, result);
    }
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_FLUSH;
        result.lastStage = result.stage;
        result.flushAttempted = true;
        ++result.flushAttempts;
        const block::FlushReport flush =
            block::flush_with_result(request.targetSnapshot.globalIndex);
        result.flushOutcome = flush.outcome;
        result.flushStatus = flush.status;
        result.persistenceTrusted = trusted_flush(flush);
        if (!result.persistenceTrusted) {
            result.failedOperation = block::OPERATION_FLUSH;
            (void)capture_current_io_result(result);
            result.status = flush.semanticsKnown ? FAT32_FORMAT_FLUSH_FAILED
                                                  : FAT32_FORMAT_FLUSH_UNAVAILABLE;
            ok = false;
        }
    }
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_VERIFY;
        result.lastStage = result.stage;
        ok = verify_fat32_structure(request.targetSnapshot,
                check.currentPartition, check.geometry, label) &&
            quick_verify_fats(request, check, lease, maxBatchSectors) &&
            quick_verify_reserved_and_root(request, check, lease, label,
                check.geometry.reservedSectorCount) &&
            read_partition_sector(request.targetSnapshot,
                check.currentPartition, check.geometry, markerSector,
                s_verifySector) == block::BLOCK_OK &&
            bytes_zero(s_verifySector, check.geometry.bytesPerSector);
        if (!ok) {
            result.failedOperation = block::OPERATION_READ;
            (void)capture_current_io_result(result);
            result.status = FAT32_FORMAT_VERIFICATION_FAILED;
        }
    }
    if (ok) {
        result.stage = FAT32_FORMAT_STAGE_RESCAN;
        result.lastStage = result.stage;
        ok = rescan_partition(request, check.currentPartition) &&
            revalidate_target_identity(request.targetSnapshot) == TARGET_VALID;
        if (!ok) {
            result.failedOperation = block::OPERATION_READ;
            (void)capture_current_io_result(result);
            result.status = FAT32_FORMAT_RESCAN_FAILED;
        }
    }

    result.reformatElapsedTicks = scan_clock_ticks() - startTicks;
    if (!ok) {
        if (result.status == FAT32_FORMAT_INVALID_REQUEST)
            result.status = FAT32_FORMAT_METADATA_WRITE_FAILED;
        result.failureStatus = result.status;
        result.firstFailedStage = result.firstFailedStage ==
            FAT32_FORMAT_STAGE_IDLE ? result.stage : result.firstFailedStage;
        result.stage = FAT32_FORMAT_STAGE_FAILED;
        result.failedBeforeWrite = !result.writeMayHaveReachedMedia;
        if (result.failureStatus == FAT32_FORMAT_RESCAN_FAILED &&
            result.verificationPassed && result.persistenceTrusted &&
            result.reformatInvalidationFlushPassed) {
            // The new filesystem and marker cleanup have both been flushed
            // and reread successfully. A failed in-memory rescan is a refresh
            // problem; keep that distinct from a failed reformat.
            result.status = FAT32_FORMAT_RESCAN_FAILED;
            result.finalProbeState = FAT32_FINAL_PROBE_FAT32;
            result.reformatState =
                FAT32_REFORMAT_DURABLE_BUT_REFRESH_FAILED;
            set_diagnostic(result,
                "The new FAT32 filesystem is durable and verified, but the runtime rescan failed. Refresh storage state or restart to rediscover it.");
            complete_storage_operation_execution(lease);
            return result.status;
        }
        if (result.reformatState >= FAT32_REFORMAT_IN_PROGRESS) {
            result.status = FAT32_FORMAT_REFORMAT_INCOMPLETE;
            result.finalProbeState = result.verificationPassed
                ? FAT32_FINAL_PROBE_FAT32
                : (result.reformatInvalidationFlushPassed &&
                   !primaryPublicationAttempted
                    ? FAT32_FINAL_PROBE_UNFORMATTED
                    : FAT32_FINAL_PROBE_UNKNOWN);
            set_diagnostic(result,
                "Quick Reformat did not complete. The previous filesystem may no longer be usable; retry formatting after fresh preflight.");
        } else {
            set_diagnostic(result, fat32_format_status_name(result.status));
        }
        complete_storage_operation_execution(lease);
        return result.status;
    }
    result.status = FAT32_FORMAT_SUCCESS;
    result.failureStatus = FAT32_FORMAT_SUCCESS;
    result.stage = FAT32_FORMAT_STAGE_COMPLETED;
    result.lastStage = FAT32_FORMAT_STAGE_COMPLETED;
    result.failedBeforeWrite = false;
    result.verificationPassed = true;
    result.finalProbeState = FAT32_FINAL_PROBE_FAT32;
    result.reformatState = FAT32_REFORMAT_DURABLE;
    set_diagnostic(result,
        "Quick Reformat completed, flushed, reread, verified, and rescanned. Ordinary file-data sectors were not erased.");
    complete_storage_operation_execution(lease);
    return result.status;
}

static Fat32FormatStatus execute_locked(const Fat32FormatRequest& request,
                                        Fat32FormatResult& result,
                                        StorageOperationLease& lease,
                                        const BlankVerificationToken* scannedToken = nullptr)
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
    BlankVerificationToken blankToken = {};
    if (scannedToken) {
        blankToken = *scannedToken;
        if (!blank_token_matches(blankToken, request,
                check.currentPartition, check.geometry, lease))
            return FAT32_FORMAT_OPERATION_OWNERSHIP_INVALID;
    } else {
        status = scan_partition_for_clean_state(request,
            check.capabilities.logicalSectorSize, check.geometry, lease,
            result.existingState, result, blankToken);
        if (status != FAT32_FORMAT_READY) return status;
    }

    // The long scan held the exact target pin and exclusive storage lease.
    // Re-run destructive preflight immediately afterward, before metadata
    // reads, Flush, or any formatter write; this also rechecks mount and boot.
    PartitionCheck postScanCheck = {};
    status = validate_request_and_partition(request, result, postScanCheck);
    if (status != FAT32_FORMAT_READY ||
        !storage_operation_lease_is_current(lease) ||
        !blank_token_matches(blankToken, request,
            postScanCheck.currentPartition, postScanCheck.geometry, lease) ||
        !same_partition_extent(check.currentPartition,
                               postScanCheck.currentPartition) ||
        !same_scan_geometry(check.geometry, postScanCheck.geometry))
        return status == FAT32_FORMAT_READY
            ? FAT32_FORMAT_PARTITION_IDENTITY_CHANGED : status;
    check = postScanCheck;
    blankToken.valid = false; // One-use authorization is consumed here.

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
    const uint32_t sectorSize = check.geometry.bytesPerSector;
    build_fat_sector(check.geometry, s_ioSector, sectorSize);
    bool writeOk = write_partition_sector(request, request.targetSnapshot,
        check.currentPartition, check.geometry,
        check.geometry.firstFatSector, s_ioSector, lease, result);
    if (writeOk) {
        writeOk = write_partition_sector(request, request.targetSnapshot,
            check.currentPartition, check.geometry,
            check.geometry.secondFatSector, s_ioSector, lease, result);
    }
    uint64_t rootSectorLba = 0;
    if (writeOk && haveLabel) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_ROOT;
        result.lastStage = result.stage;
        writeOk = cluster_relative_range(check.geometry,
            check.geometry.rootCluster, rootSectorLba);
        if (writeOk) {
            build_root_sector(label, haveLabel, s_ioSector, sectorSize);
            writeOk = write_partition_sector(request,
                request.targetSnapshot, check.currentPartition,
                check.geometry, rootSectorLba, s_ioSector, lease, result);
        }
    }

    if (writeOk) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_BACKUP_METADATA;
        result.lastStage = result.stage;
        build_boot_sector(check.geometry, label, s_ioSector, sectorSize);
        writeOk = write_partition_sector(request, request.targetSnapshot,
            check.currentPartition, check.geometry,
            check.geometry.backupBootSector, s_ioSector, lease, result);
    }
    if (writeOk) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_FSINFO;
        result.lastStage = result.stage;
        build_fsinfo(check.geometry, s_ioSector, sectorSize);
        writeOk = write_partition_sector(request, request.targetSnapshot,
            check.currentPartition, check.geometry,
            check.geometry.backupFsInfoSector, s_ioSector, lease, result);
    }
    if (writeOk) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_FSINFO;
        result.lastStage = result.stage;
        writeOk = write_partition_sector(request, request.targetSnapshot,
            check.currentPartition, check.geometry,
            check.geometry.fsInfoSector, s_ioSector, lease, result);
    }
    // Publish FAT32 only after the supporting structures exist.
    if (writeOk) {
        result.stage = FAT32_FORMAT_STAGE_WRITE_BOOT_SECTOR;
        result.lastStage = result.stage;
        build_boot_sector(check.geometry, label, s_ioSector, sectorSize);
        writeOk = write_partition_sector(request, request.targetSnapshot,
            check.currentPartition, check.geometry, 0, s_ioSector, lease,
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
                                         Fat32FormatResult& result,
                                         const StorageOperationLease& lease)
{
    PartitionCheck check = {};
    result.stage = FAT32_FORMAT_STAGE_REVALIDATING_PARTITION;
    Fat32FormatStatus status = validate_request_and_partition(request, result,
                                                               check);
    if (status != FAT32_FORMAT_READY) return status;
    result.stage = FAT32_FORMAT_STAGE_CALCULATING_LAYOUT;
    BlankVerificationToken blankToken = {};
    status = scan_partition_for_clean_state(request,
        check.capabilities.logicalSectorSize, check.geometry, lease,
        result.existingState, result, blankToken);
    if (status != FAT32_FORMAT_READY) return status;
    // A probe is only a transient observation; it cannot publish durable
    // KnownZero state to another request or operation.
    blankToken.valid = false;
    result.status = FAT32_FORMAT_READY;
    result.finalProbeState = FAT32_FINAL_PROBE_UNFORMATTED;
    set_diagnostic(result, "Partition is clean and eligible for FAT32 formatting.");
    return FAT32_FORMAT_READY;
}

static Fat32FormatStatus run_quick_reformat_with_lease(
    const Fat32FormatRequest& request, Fat32FormatResult& result, bool format)
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
    Fat32FormatStatus status;
    if (format) {
        status = execute_quick_reformat_locked(request, result, lease);
    } else {
        PartitionCheck check = {};
        QuickReformatSource source = {};
        status = quick_reformat_preflight_locked(request, result, check,
                                                  source);
    }
    if (!format || (!result.writeAttempted &&
            result.stage != FAT32_FORMAT_STAGE_COMPLETED &&
            result.stage != FAT32_FORMAT_STAGE_STATE_UNCERTAIN))
        release_storage_operation(lease);
    if (status != FAT32_FORMAT_READY && status != FAT32_FORMAT_SUCCESS &&
        status != FAT32_FORMAT_REFORMAT_INCOMPLETE &&
        result.reformatState != FAT32_REFORMAT_DURABLE_BUT_REFRESH_FAILED &&
        result.stage != FAT32_FORMAT_STAGE_STATE_UNCERTAIN) {
        result.status = status;
        if (result.stage != FAT32_FORMAT_STAGE_FAILED)
            mark_stage_failed(result);
        set_diagnostic(result, fat32_format_status_name(status));
    }
    return status;
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
        : run_probe_locked(request, result, lease);
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
        if (status != FAT32_FORMAT_AMBIGUOUS_EXISTING_DATA ||
            result.diagnostic[0] == '\0')
            set_diagnostic(result, fat32_format_status_name(status));
    }
    return status;
}

} // namespace

static void finish_job_failure(Fat32FormatJob& job, Fat32FormatStatus status)
{
    if (s_jobBlankToken.operationGeneration == job.lease.ownerToken)
        s_jobBlankToken.valid = false;
    job.result.status = status;
    if (job.result.stage != FAT32_FORMAT_STAGE_STATE_UNCERTAIN) {
        if (job.result.stage != FAT32_FORMAT_STAGE_FAILED)
            mark_stage_failed(job.result);
        job.result.status = status;
        if (job.result.diagnostic[0] == '\0')
            set_diagnostic(job.result, fat32_format_status_name(status));
    }
    job.state = FAT32_FORMAT_JOB_FAILED;
    if (job.lease.ownerToken != 0 && !job.result.writeAttempted)
        release_storage_operation(job.lease);
}

static bool cancel_job_now(Fat32FormatJob& job)
{
    if (job.commitStarted || job.state == FAT32_FORMAT_JOB_COMPLETED ||
        job.state == FAT32_FORMAT_JOB_FAILED ||
        job.state == FAT32_FORMAT_JOB_CANCELED ||
        job.state == FAT32_FORMAT_JOB_IDLE)
        return false;
    job.cancelRequested = true;
    if (job.stepActive) return true;
    if (s_jobBlankToken.operationGeneration == job.lease.ownerToken)
        s_jobBlankToken.valid = false;
    job.result.status = FAT32_FORMAT_CANCELED;
    job.result.failureStatus = FAT32_FORMAT_CANCELED;
    job.result.failedBeforeWrite = true;
    job.result.stage = FAT32_FORMAT_STAGE_FAILED;
    job.result.lastStage = FAT32_FORMAT_STAGE_FAILED;
    set_diagnostic(job.result,
        "Canceled before FAT32 metadata publication; no formatter writes were made.");
    if (job.lease.ownerToken != 0) release_storage_operation(job.lease);
    job.state = FAT32_FORMAT_JOB_CANCELED;
    return true;
}

Fat32FormatJobState begin_fat32_format_job(
    Fat32FormatJob& job, const Fat32FormatRequest& request)
{
    if (job.state == FAT32_FORMAT_JOB_SCANNING ||
        job.state == FAT32_FORMAT_JOB_READY_TO_COMMIT)
        return job.state;
    clear_bytes(&job, sizeof(job));
    job.request = request;
    reset_result(job.result);
    job.result.targetIdentity = request.targetSnapshot;
    job.result.partition = request.partitionSnapshot;
    job.result.stage = FAT32_FORMAT_STAGE_VALIDATING;

    const StorageOperationLockStatus lock =
        try_acquire_storage_operation(job.lease);
    if (lock != STORAGE_OPERATION_LOCK_ACQUIRED) {
        finish_job_failure(job, lock == STORAGE_OPERATION_LOCK_BUSY
            ? FAT32_FORMAT_OPERATION_BUSY
            : FAT32_FORMAT_OPERATION_OWNERSHIP_INVALID);
        return job.state;
    }
    clear_bytes(&s_jobBlankToken, sizeof(s_jobBlankToken));
    job.result.stage = FAT32_FORMAT_STAGE_PIN_TARGET;
    if (!pin_storage_operation_target(job.lease, job.request.targetSnapshot)) {
        const Fat32FormatStatus failure = map_identity(revalidate_target_identity(
            job.request.targetSnapshot));
        finish_job_failure(job, failure == FAT32_FORMAT_SUCCESS
            ? FAT32_FORMAT_IDENTITY_CHANGED : failure);
        return job.state;
    }

    PartitionCheck check = {};
    job.result.stage = FAT32_FORMAT_STAGE_REVALIDATING_PARTITION;
    Fat32FormatStatus status = validate_request_and_partition(
        job.request, job.result, check);
    if (status != FAT32_FORMAT_READY) {
        finish_job_failure(job, status);
        return job.state;
    }
    job.geometry = check.geometry;
    job.result.stage = FAT32_FORMAT_STAGE_CALCULATING_LAYOUT;
    job.result.existingState = classify_existing_prefix(job.request,
        check.capabilities.logicalSectorSize);
    if (job.result.existingState == FAT32_EXISTING_UNREADABLE) {
        finish_job_failure(job, FAT32_FORMAT_READ_UNAVAILABLE);
        return job.state;
    }
    if (job.result.existingState == FAT32_EXISTING_RECOGNIZED_FILESYSTEM) {
        finish_job_failure(job, FAT32_FORMAT_FILESYSTEM_ALREADY_RECOGNIZED);
        return job.state;
    }
    job.scanMaxSectors = fat32_scan_batch_sectors(
        check.capabilities.logicalSectorSize, check.capabilities.maxTransferBytes);
    if (job.scanMaxSectors == 0) {
        finish_job_failure(job, FAT32_FORMAT_READ_UNAVAILABLE);
        return job.state;
    }
    job.scanStartTicks = scan_clock_ticks();
    job.result.stage = FAT32_FORMAT_STAGE_VALIDATING;
    job.result.status = FAT32_FORMAT_READY;
    job.state = FAT32_FORMAT_JOB_SCANNING;
    return job.state;
}

Fat32FormatJobState step_fat32_format_job(Fat32FormatJob& job)
{
    if (job.state == FAT32_FORMAT_JOB_SCANNING) {
        if (job.cancelRequested) {
            job.stepActive = false;
            cancel_job_now(job);
            return job.state;
        }
        job.stepActive = true;
        if (!storage_operation_lease_is_current(job.lease) ||
            revalidate_target_identity(job.request.targetSnapshot) != TARGET_VALID) {
            job.stepActive = false;
            finish_job_failure(job, FAT32_FORMAT_IDENTITY_CHANGED);
            return job.state;
        }
        DeviceCapabilities caps = {};
        if (!query_device_capabilities(job.request.targetSnapshot.globalIndex,
                                       caps)) {
            job.stepActive = false;
            finish_job_failure(job, FAT32_FORMAT_DEVICE_MISSING);
            return job.state;
        }
        const uint64_t total = job.request.partitionSnapshot.sectorCount;
        if (job.scanRelativeLba >= total) {
            job.stepActive = false;
            finish_job_failure(job, FAT32_FORMAT_INVALID_GEOMETRY);
            return job.state;
        }
        const uint32_t count = static_cast<uint32_t>(
            total - job.scanRelativeLba < job.scanMaxSectors
                ? total - job.scanRelativeLba : job.scanMaxSectors);
        uint64_t lba = 0;
        if (!add_u64(job.request.partitionSnapshot.startLba,
                     job.scanRelativeLba, lba) ||
            !checked_lba_range(caps.totalLogicalSectors, lba, count)) {
            job.stepActive = false;
            finish_job_failure(job, FAT32_FORMAT_INVALID_GEOMETRY);
            return job.state;
        }
        const uint32_t sectorSize = caps.logicalSectorSize;
        const uint32_t bytes = count * sectorSize;
        ++job.result.scanReadRequests;
        if (job.result.scanSmallestRequestBytes == 0 ||
            bytes < job.result.scanSmallestRequestBytes)
            job.result.scanSmallestRequestBytes = bytes;
        if (bytes > job.result.scanLargestRequestBytes)
            job.result.scanLargestRequestBytes = bytes;
        job.result.scanCurrentLba = lba;
        job.result.scanRelativeLba = job.scanRelativeLba;
        if (read_sectors_safe(job.request.targetSnapshot.globalIndex, lba,
                count, s_zeroScanBuffer, bytes) != block::BLOCK_OK) {
            (void)capture_current_io_result(job.result);
            finish_scan_metrics(job.result, job.scanStartTicks);
            job.stepActive = false;
            finish_job_failure(job, FAT32_FORMAT_READ_UNAVAILABLE);
            return job.state;
        }
        job.result.scanBytesRead += bytes;
        size_t firstNonzeroOffset = 0;
        if (!buffer_is_zero(s_zeroScanBuffer, bytes, firstNonzeroOffset)) {
            job.result.scanFirstNonzeroRelativeLba = job.scanRelativeLba +
                firstNonzeroOffset / sectorSize;
            job.result.scanFirstNonzeroByteOffset = static_cast<uint32_t>(
                firstNonzeroOffset % sectorSize);
            job.result.scanCurrentLba = job.request.partitionSnapshot.startLba +
                job.result.scanFirstNonzeroRelativeLba;
            job.result.scanRelativeLba = job.result.scanFirstNonzeroRelativeLba;
            const uint64_t totalBytes = total *
                static_cast<uint64_t>(sectorSize);
            job.result.scanProgressPercent = totalBytes == 0 ? 0u :
                static_cast<uint8_t>((job.result.scanBytesRead * 100u) /
                                     totalBytes);
            job.result.existingState = FAT32_EXISTING_AMBIGUOUS_DATA;
            set_diagnostic(job.result,
                "Blank scan found non-zero data; no formatter writes were made.");
            finish_scan_metrics(job.result, job.scanStartTicks);
            job.stepActive = false;
            finish_job_failure(job, FAT32_FORMAT_AMBIGUOUS_EXISTING_DATA);
            return job.state;
        }
        if (job.cancelRequested) {
            finish_scan_metrics(job.result, job.scanStartTicks);
            job.stepActive = false;
            cancel_job_now(job);
            return job.state;
        }
        job.scanRelativeLba += count;
        job.result.scanZeroVerifiedSectors += count;
        job.result.scanRelativeLba = job.scanRelativeLba;
        job.result.scanCurrentLba = job.request.partitionSnapshot.startLba +
            job.scanRelativeLba;
        const uint64_t totalBytes = total * static_cast<uint64_t>(sectorSize);
        job.result.scanProgressPercent = totalBytes == 0 ? 100u :
            static_cast<uint8_t>((job.result.scanBytesRead * 100u) / totalBytes);
        job.stepActive = false;
        if (job.scanRelativeLba == total) {
            job.result.scanCoverageComplete = true;
            job.result.scanProgressPercent = 100u;
            job.result.existingState = FAT32_EXISTING_CLEAN;
            finish_scan_metrics(job.result, job.scanStartTicks);
            s_jobBlankToken.valid = true;
            s_jobBlankToken.target = job.request.targetSnapshot;
            s_jobBlankToken.partition = job.request.partitionSnapshot;
            s_jobBlankToken.geometry = job.geometry;
            s_jobBlankToken.operationGeneration = job.lease.ownerToken;
            s_jobBlankToken.sectorsVerified = job.result.scanZeroVerifiedSectors;
            job.state = FAT32_FORMAT_JOB_READY_TO_COMMIT;
        }
        return job.state;
    }

    if (job.state != FAT32_FORMAT_JOB_READY_TO_COMMIT) return job.state;
    if (job.cancelRequested) {
        cancel_job_now(job);
        return job.state;
    }
    if (!s_jobBlankToken.valid ||
        !storage_operation_lease_is_current(job.lease) ||
        revalidate_target_identity(job.request.targetSnapshot) != TARGET_VALID) {
        finish_job_failure(job, FAT32_FORMAT_IDENTITY_CHANGED);
        return job.state;
    }
    job.commitStarted = true;
    const BlankVerificationToken token = s_jobBlankToken;
    s_jobBlankToken.valid = false;
    const Fat32FormatStatus status = execute_locked(job.request, job.result,
        job.lease, &token);
    if (job.lease.ownerToken != 0 && !job.result.writeAttempted &&
        job.result.stage != FAT32_FORMAT_STAGE_COMPLETED &&
        job.result.stage != FAT32_FORMAT_STAGE_STATE_UNCERTAIN)
        release_storage_operation(job.lease);
    if (status != FAT32_FORMAT_READY && status != FAT32_FORMAT_SUCCESS &&
        job.result.stage != FAT32_FORMAT_STAGE_STATE_UNCERTAIN) {
        job.result.status = status;
        if (job.result.stage != FAT32_FORMAT_STAGE_FAILED)
            mark_stage_failed(job.result);
        if (status != FAT32_FORMAT_AMBIGUOUS_EXISTING_DATA ||
            job.result.diagnostic[0] == '\0')
            set_diagnostic(job.result, fat32_format_status_name(status));
    }
    job.state = status == FAT32_FORMAT_SUCCESS
        ? FAT32_FORMAT_JOB_COMPLETED : FAT32_FORMAT_JOB_FAILED;
    return job.state;
}

bool cancel_fat32_format_job(Fat32FormatJob& job)
{
    return cancel_job_now(job);
}

const char* fat32_format_job_state_name(Fat32FormatJobState state)
{
    switch (state) {
        case FAT32_FORMAT_JOB_IDLE: return "Idle";
        case FAT32_FORMAT_JOB_SCANNING: return "Checking blank media";
        case FAT32_FORMAT_JOB_READY_TO_COMMIT: return "Creating FAT32 metadata";
        case FAT32_FORMAT_JOB_COMPLETED: return "Completed";
        case FAT32_FORMAT_JOB_FAILED: return "Failed";
        case FAT32_FORMAT_JOB_CANCELED: return "Canceled";
        default: return "Unknown";
    }
}

uint32_t fat32_scan_batch_sectors(uint32_t logicalSectorSize,
                                  uint32_t maxTransferBytes)
{
    if (logicalSectorSize < MIN_LOGICAL_SECTOR_SIZE ||
        logicalSectorSize > MAX_LOGICAL_SECTOR_SIZE ||
        (logicalSectorSize & (logicalSectorSize - 1u)) != 0)
        return 0;
    uint32_t sectors = FAT32_FORMAT_SCAN_BUFFER_MAX_BYTES /
        logicalSectorSize;
    if (maxTransferBytes != 0) {
        const uint32_t transferSectors = maxTransferBytes / logicalSectorSize;
        if (transferSectors == 0) return 0;
        if (sectors > transferSectors) sectors = transferSectors;
    }
    return sectors;
}

Fat32FormatStatus calculate_fat32_format_geometry(
    uint64_t partitionStartLba, uint64_t partitionSectorCount,
    uint32_t logicalSectorSize, Fat32FormatGeometry& result)
{
    clear_bytes(&result, sizeof(result));
    if (!fat32_format_sector_size_supported(logicalSectorSize))
        return FAT32_FORMAT_UNSUPPORTED_SECTOR_SIZE;
    if (partitionSectorCount == 0) return FAT32_FORMAT_TOO_SMALL;
    if (partitionSectorCount > UINT32_MAX || partitionStartLba > UINT32_MAX)
        return FAT32_FORMAT_LAYOUT_OVERFLOW;

    uint32_t sectorsPerCluster =
        FAT32_FORMAT_MAX_CLUSTER_BYTES / logicalSectorSize;
    if (sectorsPerCluster > FAT32_FORMAT_MAX_SECTORS_PER_CLUSTER)
        sectorsPerCluster = FAT32_FORMAT_MAX_SECTORS_PER_CLUSTER;
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
    uint64_t clusterBytes = 0;
    if (!add_u64(partitionStartLba, partitionSectorCount, absoluteEnd) ||
        absoluteEnd == 0 ||
        !mul_u64(sectorsPerCluster, logicalSectorSize, clusterBytes) ||
        clusterBytes > UINT32_MAX) return FAT32_FORMAT_LAYOUT_OVERFLOW;

    result.bytesPerSector = logicalSectorSize;
    result.sectorsPerCluster = sectorsPerCluster;
    result.clusterSizeBytes = static_cast<uint32_t>(clusterBytes);
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
    result.hiddenSectors = static_cast<uint32_t>(partitionStartLba);
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

Fat32FormatStatus probe_fat32_quick_reformat_partition(
    const Fat32FormatRequest& request, Fat32FormatResult& result)
{
    reset_result(result);
    result.targetIdentity = request.targetSnapshot;
    result.partition = request.partitionSnapshot;
    result.stage = FAT32_FORMAT_STAGE_VALIDATING;
    return run_quick_reformat_with_lease(request, result, false);
}

Fat32FormatStatus quick_reformat_fat32_partition(
    const Fat32FormatRequest& request, Fat32FormatResult& result)
{
    reset_result(result);
    result.targetIdentity = request.targetSnapshot;
    result.partition = request.partitionSnapshot;
    result.stage = FAT32_FORMAT_STAGE_VALIDATING;
    return run_quick_reformat_with_lease(request, result, true);
}

const char* fat32_format_status_name(Fat32FormatStatus status)
{
    switch (status) {
        case FAT32_FORMAT_READY: return "Ready to format FAT32";
        case FAT32_FORMAT_SUCCESS: return "FAT32 formatted and verified";
        case FAT32_FORMAT_CANCELED: return "Formatting canceled before metadata publication";
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
        case FAT32_FORMAT_UNSUPPORTED_SECTOR_SIZE: return "FAT32 formatting supports 512-byte and 4096-byte logical sectors";
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
        case FAT32_FORMAT_REFORMAT_TARGET_UNSUPPORTED: return "Quick Reformat supports guideXOS FAT32 or a marked interrupted reformat";
        case FAT32_FORMAT_REFORMAT_MARKER_FAILED: return "Interrupted-reformat marker is invalid or does not match this partition";
        case FAT32_FORMAT_REFORMAT_INCOMPLETE: return "Quick Reformat did not complete; the filesystem may be incomplete";
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
        case FAT32_EXISTING_INTERRUPTED_REFORMAT: return "Interrupted FAT32 reformat";
        case FAT32_EXISTING_UNKNOWN: default: return "Unknown";
    }
}

const char* fat32_reformat_state_name(Fat32ReformatState state)
{
    switch (state) {
        case FAT32_REFORMAT_BEFORE_DESTRUCTIVE_COMMIT:
            return "BeforeDestructiveCommit";
        case FAT32_REFORMAT_IN_PROGRESS: return "ReformatInProgress";
        case FAT32_REFORMAT_NEW_FILESYSTEM_WRITTEN_NOT_DURABLE:
            return "NewFilesystemWrittenButNotDurable";
        case FAT32_REFORMAT_DURABLE: return "Durable";
        case FAT32_REFORMAT_DURABLE_BUT_REFRESH_FAILED:
            return "DurableButRefreshFailed";
        default: return "Unknown";
    }
}

} // namespace storage
} // namespace kernel
