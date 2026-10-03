#include "include/kernel/partition_operations.h"
#include "include/kernel/block_device.h"
#if !defined(KERNEL_STORAGE_TEST)
#include "include/kernel/virtio_rng.h"
#endif

namespace kernel {
namespace storage {

namespace {

static const uint32_t MBR_TABLE_OFFSET = 446;
static const uint32_t MBR_ENTRY_BYTES = 16;
static const uint32_t MBR_SIGNATURE_OFFSET = 510;
static const uint32_t GPT_HEADER_CRC_OFFSET = 16;
static const uint32_t GPT_ARRAY_CRC_OFFSET = 88;
static const uint32_t GPT_MIN_HEADER_SIZE = 92;

struct MetadataSnapshot {
    alignas(4096) uint8_t mbr[MAX_LOGICAL_SECTOR_SIZE];
    alignas(4096) uint8_t primaryHeader[MAX_LOGICAL_SECTOR_SIZE];
    alignas(4096) uint8_t primaryArray[GPT_MAX_ARRAY_BYTES];
    alignas(4096) uint8_t backupArray[GPT_MAX_ARRAY_BYTES];
    alignas(4096) uint8_t backupHeader[MAX_LOGICAL_SECTOR_SIZE];
    uint64_t primaryArrayLba;
    uint64_t backupArrayLba;
    uint32_t sectorSize;
    uint32_t arraySectors;
    uint32_t arrayBytes;
    uint32_t entrySize;
    uint16_t entryCount;
    bool isGpt;
    bool valid;
};

struct ValidatedTarget {
    CreatePartitionProbe probe;
    uint8_t slot;
    uint64_t startLba;
    uint64_t endLba;
    uint64_t sectorCount;
    uint16_t entryCount;
    uint32_t entrySize;
    uint32_t arraySectors;
    uint32_t arrayBytes;
    uint64_t primaryArrayLba;
    uint64_t backupArrayLba;
};

alignas(4096) static uint8_t s_ioSector[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_primaryScan[GPT_MAX_ARRAY_BYTES];
alignas(4096) static uint8_t s_backupScan[GPT_MAX_ARRAY_BYTES];
alignas(4096) static uint8_t s_newArray[GPT_MAX_ARRAY_BYTES];
alignas(4096) static uint8_t s_mbrCandidate[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_primaryHeader[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_backupHeader[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static PartitionTableModel s_currentTable;
alignas(4096) static PartitionTableModel s_originalTable;
alignas(4096) static PartitionTableModel s_verifiedTable;
static UnallocatedRegion s_regions[MAX_UNALLOCATED_REGIONS];
static MetadataSnapshot s_snapshot;

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

static void write_u64(uint8_t* output, uint64_t value)
{
    write_u32(output, static_cast<uint32_t>(value));
    write_u32(output + 4, static_cast<uint32_t>(value >> 32));
}

static void set_diagnostic(CreatePartitionResult& result,
                           const char* message)
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

static void reset_probe(CreatePartitionProbe& result)
{
    clear_bytes(&result, sizeof(result));
    result.status = CREATE_PARTITION_INVALID_REQUEST;
    result.detectedState = DISK_STATE_UNREADABLE;
}

static void reset_result(CreatePartitionResult& result)
{
    clear_bytes(&result, sizeof(result));
    result.status = CREATE_PARTITION_INVALID_REQUEST;
    result.failureStatus = CREATE_PARTITION_INVALID_REQUEST;
    result.failedBeforeWrite = true;
    result.stage = CREATE_PARTITION_STAGE_IDLE;
    result.flushOutcome = block::FLUSH_OUTCOME_INVALID;
    result.flushStatus = block::BLOCK_ERR_INVALID;
    result.rollbackWriteStatus = block::BLOCK_ERR_INVALID;
    result.rollbackFlushOutcome = block::FLUSH_OUTCOME_INVALID;
    result.rollbackFlushStatus = block::BLOCK_ERR_INVALID;
    result.finalDetectedState = DISK_STATE_UNREADABLE;
}

static void mark_create_failed_stage(CreatePartitionResult& result)
{
    result.lastStage = result.stage;
    if (result.firstFailedStage == CREATE_PARTITION_STAGE_IDLE)
        result.firstFailedStage = result.stage;
    result.failureStatus = result.status;
    result.failedBeforeWrite = !result.writeMayHaveReachedMedia;
    result.stage = CREATE_PARTITION_STAGE_FAILED;
}

static bool capture_current_io_result(CreatePartitionResult& result)
{
    if (!block::last_operation_diagnostic(result.failedBlockDiagnostic))
        return false;
    result.blockStatusValid = true;
    result.blockStatus = result.failedBlockDiagnostic.status;
    result.failedOperation = result.failedBlockDiagnostic.operation;
    return true;
}

static CreatePartitionStatus map_identity(RevalidationStatus status)
{
    switch (status) {
        case TARGET_REGISTRY_CHANGED: return CREATE_PARTITION_REGISTRY_CHANGED;
        case TARGET_DEVICE_MISSING: return CREATE_PARTITION_DEVICE_MISSING;
        case TARGET_IDENTITY_MISMATCH: return CREATE_PARTITION_IDENTITY_CHANGED;
        case TARGET_VALID: default: return CREATE_PARTITION_SUCCESS;
    }
}

static CreatePartitionStatus map_safety(uint32_t issues)
{
    if (issues & SAFETY_ISSUE_REGISTRY_CHANGED)
        return CREATE_PARTITION_REGISTRY_CHANGED;
    if (issues & SAFETY_ISSUE_DEVICE_MISSING)
        return CREATE_PARTITION_DEVICE_MISSING;
    if (issues & SAFETY_ISSUE_IDENTITY_MISMATCH)
        return CREATE_PARTITION_IDENTITY_CHANGED;
    if (issues & SAFETY_ISSUE_UNKNOWN_GEOMETRY)
        return CREATE_PARTITION_INVALID_GEOMETRY;
    if (issues & SAFETY_ISSUE_UNREADABLE)
        return CREATE_PARTITION_READ_UNAVAILABLE;
    if (issues & SAFETY_ISSUE_READ_ONLY)
        return CREATE_PARTITION_READ_ONLY;
    if (issues & SAFETY_ISSUE_DURABILITY_UNKNOWN)
        return CREATE_PARTITION_DURABILITY_UNKNOWN;
    if (issues & SAFETY_ISSUE_ROOT_BACKING)
        return CREATE_PARTITION_ROOT_BACKING;
    if (issues & SAFETY_ISSUE_MOUNTED)
        return CREATE_PARTITION_MOUNTED;
    if (issues & SAFETY_ISSUE_BOOT_BACKING)
        return CREATE_PARTITION_BOOT_BACKING;
    if (issues & SAFETY_ISSUE_BOOT_IDENTITY_UNKNOWN)
        return CREATE_PARTITION_BOOT_IDENTITY_UNKNOWN;
    return CREATE_PARTITION_INVALID_TABLE;
}

static bool trusted_flush(const block::FlushReport& report)
{
    return report.status == block::BLOCK_OK && report.semanticsKnown &&
        (report.outcome == block::FLUSH_OUTCOME_SUPPORTED_SUCCEEDED ||
         report.outcome == block::FLUSH_OUTCOME_SYNCHRONOUS_DURABLE);
}

static bool read_region(uint8_t device, uint64_t lba, uint32_t sectors,
                        uint32_t sectorSize, uint8_t* output)
{
    if (!output || sectors == 0) return false;
    DeviceCapabilities capabilities;
    if (!query_device_capabilities(device, capabilities) ||
        !checked_lba_range(capabilities.totalLogicalSectors, lba, sectors) ||
        static_cast<uint64_t>(sectors) * sectorSize > GPT_MAX_ARRAY_BYTES)
        return false;
    for (uint32_t i = 0; i < sectors; ++i) {
        if (read_logical_sector(device, lba + i, s_ioSector, sectorSize) !=
            block::BLOCK_OK) return false;
        copy_bytes(output + static_cast<size_t>(i) * sectorSize,
                   s_ioSector, sectorSize);
    }
    return true;
}

static bool compare_region(uint8_t device, uint64_t lba, uint32_t sectors,
                           uint32_t sectorSize, const uint8_t* expected)
{
    if (!expected || sectors == 0) return false;
    DeviceCapabilities capabilities;
    if (!query_device_capabilities(device, capabilities) ||
        !checked_lba_range(capabilities.totalLogicalSectors, lba, sectors))
        return false;
    for (uint32_t i = 0; i < sectors; ++i) {
        if (read_logical_sector(device, lba + i, s_ioSector, sectorSize) !=
                block::BLOCK_OK ||
            !bytes_equal(s_ioSector,
                expected + static_cast<size_t>(i) * sectorSize, sectorSize))
            return false;
    }
    return true;
}

static bool same_region(const UnallocatedRegion& a,
                        const UnallocatedRegion& b)
{
    return a.startLba == b.startLba && a.endLba == b.endLba &&
        a.sectorCount == b.sectorCount && a.capacityBytes == b.capacityBytes &&
        a.alignmentSectors == b.alignmentSectors &&
        a.startAlignedTo1MiB == b.startAlignedTo1MiB &&
        a.insideUsableRange == b.insideUsableRange;
}

static bool guid_equal(const uint8_t* a, const uint8_t* b)
{
    return bytes_equal(a, b, 16);
}

static bool guid_in_table(const uint8_t* guid,
                          const PartitionTableModel& table)
{
    if (guid_equal(guid, table.primaryDiskGuid) ||
        guid_equal(guid, table.backupDiskGuid)) return true;
    for (uint16_t i = 0; i < table.partitionCount; ++i)
        if (guid_equal(guid, table.partitions[i].uniqueGuid)) return true;
    return false;
}

static bool arrays_have_consistent_guids(const uint8_t* entries,
                                        uint16_t entryCount,
                                        uint32_t entrySize,
                                        const PartitionTableModel& table,
                                        uint16_t& freeCount,
                                        uint8_t& firstFree)
{
    freeCount = 0;
    firstFree = 0xFFu;
    uint16_t usedCount = 0;
    for (uint16_t slot = 0; slot < entryCount; ++slot) {
        const uint8_t* entry = entries + static_cast<size_t>(slot) * entrySize;
        if (bytes_zero(entry, 16)) {
            ++freeCount;
            if (firstFree == 0xFFu) firstFree = static_cast<uint8_t>(slot);
            continue;
        }
        ++usedCount;
        const uint8_t* unique = entry + 16;
        if (bytes_zero(unique, 16) || guid_equal(unique, table.primaryDiskGuid) ||
            guid_equal(unique, table.backupDiskGuid)) return false;
        for (uint16_t prior = 0; prior < slot; ++prior) {
            const uint8_t* priorEntry = entries +
                static_cast<size_t>(prior) * entrySize;
            if (!bytes_zero(priorEntry, 16) &&
                guid_equal(unique, priorEntry + 16)) return false;
        }
    }
    return usedCount == table.partitionCount;
}

static bool utf8_next(const uint8_t* text, size_t length, size_t& at,
                      uint32_t& codepoint)
{
    if (at >= length) return false;
    const uint8_t first = text[at++];
    if (first < 0x80) {
        codepoint = first;
        return true;
    }
    uint8_t needed = 0;
    uint32_t value = 0;
    uint32_t minimum = 0;
    if ((first & 0xE0) == 0xC0) { needed = 1; value = first & 0x1F; minimum = 0x80; }
    else if ((first & 0xF0) == 0xE0) { needed = 2; value = first & 0x0F; minimum = 0x800; }
    else if ((first & 0xF8) == 0xF0) { needed = 3; value = first & 0x07; minimum = 0x10000; }
    else { codepoint = 0xFFFD; return true; }
    if (length - at < needed) { at = length; codepoint = 0xFFFD; return true; }
    for (uint8_t i = 0; i < needed; ++i) {
        const uint8_t next = text[at];
        if ((next & 0xC0) != 0x80) { codepoint = 0xFFFD; return true; }
        ++at;
        value = (value << 6) | (next & 0x3F);
    }
    if (value < minimum || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF)) value = 0xFFFD;
    codepoint = value;
    return true;
}

static void encode_gpt_name(const char* input, uint8_t* entry)
{
    for (uint8_t i = 0; i < 36; ++i) write_u16(entry + 56 + i * 2, 0);
    size_t length = 0;
    while (length < 64 && input[length]) ++length;
    size_t at = 0;
    uint8_t units = 0;
    while (at < length && units < 36) {
        uint32_t cp = 0;
        if (!utf8_next(reinterpret_cast<const uint8_t*>(input), length, at, cp))
            break;
        if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) cp = 0xFFFD;
        if (cp <= 0xFFFF) {
            write_u16(entry + 56 + units * 2, static_cast<uint16_t>(cp));
            ++units;
        } else {
            if (units > 34) break;
            cp -= 0x10000;
            write_u16(entry + 56 + units * 2,
                static_cast<uint16_t>(0xD800 | (cp >> 10)));
            write_u16(entry + 56 + (units + 1) * 2,
                static_cast<uint16_t>(0xDC00 | (cp & 0x3FF)));
            units = static_cast<uint8_t>(units + 2);
        }
    }
}

static bool append_utf8(char* output, size_t capacity, size_t& used,
                        uint32_t codepoint)
{
    uint8_t encoded[4];
    uint8_t count = 0;
    if (codepoint <= 0x7F) {
        encoded[0] = static_cast<uint8_t>(codepoint); count = 1;
    } else if (codepoint <= 0x7FF) {
        encoded[0] = static_cast<uint8_t>(0xC0 | (codepoint >> 6));
        encoded[1] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F)); count = 2;
    } else if (codepoint <= 0xFFFF) {
        encoded[0] = static_cast<uint8_t>(0xE0 | (codepoint >> 12));
        encoded[1] = static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[2] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F)); count = 3;
    } else {
        encoded[0] = static_cast<uint8_t>(0xF0 | (codepoint >> 18));
        encoded[1] = static_cast<uint8_t>(0x80 | ((codepoint >> 12) & 0x3F));
        encoded[2] = static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[3] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F)); count = 4;
    }
    if (used + count >= capacity) return false;
    for (uint8_t i = 0; i < count; ++i) output[used++] = encoded[i];
    output[used] = '\0';
    return true;
}

static void normalized_gpt_name(const char* input, char output[64])
{
    output[0] = '\0';
    size_t length = 0;
    while (length < 64 && input[length]) ++length;
    size_t at = 0, used = 0;
    uint8_t utf16Units = 0;
    while (at < length && utf16Units < 36) {
        uint32_t cp = 0;
        if (!utf8_next(reinterpret_cast<const uint8_t*>(input), length, at, cp))
            break;
        if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) cp = 0xFFFD;
        const uint8_t units = cp > 0xFFFF ? 2 : 1;
        if (utf16Units + units > 36 || !append_utf8(output, 64, used, cp))
            break;
        utf16Units = static_cast<uint8_t>(utf16Units + units);
    }
}

static bool random_guid(uint8_t guid[16], const CreatePartitionRequest& request)
{
#if defined(KERNEL_STORAGE_TEST)
    if (!request.testGuidProvided) return false;
    copy_bytes(guid, request.testUniqueGuid, 16);
#else
    (void)request;
    if (!kernel::virtio::rng::fill(guid, 16)) return false;
#endif
    if (bytes_zero(guid, 16)) return false;
    // Set RFC 4122 version/variant bits in EFI GUID on-disk byte order.
    guid[7] = static_cast<uint8_t>((guid[7] & 0x0F) | 0x40);
    guid[8] = static_cast<uint8_t>((guid[8] & 0x3F) | 0x80);
    return !bytes_zero(guid, 16);
}

static bool locate_region(const UnallocatedRegion& selected,
                          uint16_t count, UnallocatedRegion& matched)
{
    for (uint16_t i = 0; i < count; ++i) {
        if (s_regions[i].startLba == selected.startLba &&
            s_regions[i].endLba == selected.endLba) {
            matched = s_regions[i];
            return same_region(selected, matched);
        }
    }
    return false;
}

static CreatePartitionStatus validate_target(
    const TargetIdentity& target, PartitionScheme scheme,
    const UnallocatedRegion& selected, const StorageOperationLease& lease,
    ValidatedTarget& validated)
{
    clear_bytes(&validated, sizeof(validated));
    reset_probe(validated.probe);
    if (!storage_operation_lease_is_current(lease))
        return validated.probe.status = CREATE_PARTITION_OPERATION_OWNERSHIP_INVALID;
    const RevalidationStatus identity = revalidate_target_identity(target);
    if (identity != TARGET_VALID)
        return validated.probe.status = map_identity(identity);

    DeviceCapabilities caps;
    if (!query_device_capabilities(target.globalIndex, caps))
        return validated.probe.status = CREATE_PARTITION_DEVICE_MISSING;
    if (!caps.geometryValid || !caps.capacityValid ||
        (caps.logicalSectorSize != 512 && caps.logicalSectorSize != 4096))
        return validated.probe.status = CREATE_PARTITION_INVALID_GEOMETRY;
    if (!caps.readable) return validated.probe.status = CREATE_PARTITION_READ_UNAVAILABLE;
    if (!caps.writable) return validated.probe.status = CREATE_PARTITION_READ_ONLY;
    if (caps.maxTransferBytes != 0 &&
        caps.maxTransferBytes < caps.logicalSectorSize)
        return validated.probe.status = CREATE_PARTITION_WRITE_UNAVAILABLE;
    if (caps.persistence == PERSISTENCE_FLUSH_REQUIRED) {
        if (!caps.flushSupported || !caps.flushSemanticsKnown)
            return validated.probe.status = CREATE_PARTITION_FLUSH_UNAVAILABLE;
    } else if (caps.persistence != PERSISTENCE_SYNCHRONOUS_DURABLE) {
        return validated.probe.status = CREATE_PARTITION_DURABILITY_UNKNOWN;
    }
    if (caps.requiredBufferAlignment > MAX_LOGICAL_SECTOR_SIZE ||
        (caps.requiredBufferAlignment > 1 &&
         (caps.requiredBufferAlignment &
          (caps.requiredBufferAlignment - 1)) != 0))
        return validated.probe.status = CREATE_PARTITION_INVALID_GEOMETRY;

    SafetyRequest safetyRequest = {};
    safetyRequest.requireWritable = true;
    safetyRequest.requireDurableWrites = true;
    // The parser state is checked below so degraded GPT receives its explicit
    // error while mount/root and identity protection remain common policy.
    safetyRequest.requireKnownPartitionState = false;
    safetyRequest.allowNotInitialized = false;
    SafetyValidation safety;
    if (!validate_destructive_target(target, safetyRequest, safety))
        return validated.probe.status = map_safety(safety.issues);

    const BootProtection boot = query_boot_protection(target);
    if (boot.safety == BOOT_DEVICE_IS_TARGET)
        return validated.probe.status = CREATE_PARTITION_BOOT_BACKING;
    if (boot.safety != BOOT_DEVICE_DEFINITELY_NOT_TARGET)
        return validated.probe.status = CREATE_PARTITION_BOOT_IDENTITY_UNKNOWN;

    if (!parse_partition_table(target.globalIndex, s_currentTable) ||
        s_currentTable.state == DISK_STATE_UNREADABLE)
        return validated.probe.status = CREATE_PARTITION_READ_UNAVAILABLE;
    validated.probe.detectedState = s_currentTable.state;
    if (s_currentTable.state == DISK_STATE_GPT_DEGRADED)
        return validated.probe.status = CREATE_PARTITION_GPT_DEGRADED;
    if (s_currentTable.state == DISK_STATE_INVALID_PARTITION_TABLE)
        return validated.probe.status = CREATE_PARTITION_INVALID_TABLE;
    if (s_currentTable.state == DISK_STATE_UNSUPPORTED_PARTITION_SCHEME ||
        s_currentTable.scheme == PARTITION_SCHEME_UNSUPPORTED)
        return validated.probe.status = CREATE_PARTITION_UNSUPPORTED_SCHEME;
    if ((scheme != PARTITION_SCHEME_GPT && scheme != PARTITION_SCHEME_MBR) ||
        s_currentTable.scheme != scheme)
        return validated.probe.status = CREATE_PARTITION_UNSUPPORTED_SCHEME;
    if ((scheme == PARTITION_SCHEME_GPT &&
         (s_currentTable.state != DISK_STATE_VALID_GPT ||
          !s_currentTable.primaryGptValid || !s_currentTable.backupGptValid ||
          !s_currentTable.gptCopiesAgree)) ||
        (scheme == PARTITION_SCHEME_MBR &&
         (s_currentTable.state != DISK_STATE_VALID_MBR ||
          s_currentTable.extendedPartitionsPresent || s_currentTable.hybridMbr)))
        return validated.probe.status = CREATE_PARTITION_INVALID_TABLE;

    uint16_t regionCount = 0;
    if (!compute_unallocated_regions(s_currentTable, caps.totalLogicalSectors,
            caps.logicalSectorSize, s_regions, MAX_UNALLOCATED_REGIONS,
            regionCount))
        return validated.probe.status = CREATE_PARTITION_INVALID_TABLE;
    UnallocatedRegion currentRegion = {};
    if (!locate_region(selected, regionCount, currentRegion))
        return validated.probe.status = CREATE_PARTITION_STALE_REGION;

    const uint64_t alignmentSectors = CREATE_PARTITION_ALIGNMENT_BYTES /
        caps.logicalSectorSize;
    if (alignmentSectors == 0)
        return validated.probe.status = CREATE_PARTITION_INVALID_GEOMETRY;
    const uint64_t remainder = currentRegion.startLba % alignmentSectors;
    uint64_t firstAligned = currentRegion.startLba;
    if (remainder != 0) {
        const uint64_t add = alignmentSectors - remainder;
        if (firstAligned > UINT64_MAX - add)
            return validated.probe.status = CREATE_PARTITION_NO_ALIGNED_SPACE;
        firstAligned += add;
    }
    uint64_t maxEnd = currentRegion.endLba;
    if (scheme == PARTITION_SCHEME_MBR) {
        if (firstAligned > 0xFFFFFFFFull)
            return validated.probe.status = CREATE_PARTITION_NO_ALIGNED_SPACE;
        if (maxEnd > 0xFFFFFFFFull) maxEnd = 0xFFFFFFFFull;
    }
    if (firstAligned > maxEnd)
        return validated.probe.status = CREATE_PARTITION_NO_ALIGNED_SPACE;
    const uint64_t maxSectors = maxEnd - firstAligned + 1;
    if (maxSectors > UINT64_MAX / caps.logicalSectorSize)
        return validated.probe.status = CREATE_PARTITION_INVALID_GEOMETRY;
    const uint64_t maxBytes = maxSectors * caps.logicalSectorSize;
    validated.probe.firstAlignedLba = firstAligned;
    validated.probe.maximumSectorCount = maxSectors;
    validated.probe.maximumBytes = maxBytes;

    validated.slot = 0xFFu;
    if (scheme == PARTITION_SCHEME_GPT) {
        const uint32_t entrySize = s_currentTable.gptEntrySize;
        const uint16_t entryCount = s_currentTable.tableEntryCount;
        const uint32_t arraySectors = s_currentTable.gptEntryArraySectors;
        if (entryCount == 0 || entryCount > MAX_PARSED_PARTITIONS ||
            entrySize < 128 || entrySize > GPT_MAX_ENTRY_SIZE ||
            (entrySize & 7u) != 0 || arraySectors == 0 ||
            s_currentTable.primaryGptEntryArrayLba == 0 ||
            s_currentTable.backupGptEntryArrayLba == 0)
            return validated.probe.status = CREATE_PARTITION_INVALID_TABLE;
        const uint64_t bytes64 = static_cast<uint64_t>(entryCount) * entrySize;
        const uint64_t sectorBytes = static_cast<uint64_t>(arraySectors) *
            caps.logicalSectorSize;
        if (bytes64 == 0 || bytes64 > GPT_MAX_ARRAY_BYTES ||
            sectorBytes > GPT_MAX_ARRAY_BYTES || bytes64 > sectorBytes)
            return validated.probe.status = CREATE_PARTITION_INVALID_TABLE;
        const uint32_t arrayBytes = static_cast<uint32_t>(bytes64);
        if (!read_region(target.globalIndex,
                s_currentTable.primaryGptEntryArrayLba, arraySectors,
                caps.logicalSectorSize, s_primaryScan) ||
            !read_region(target.globalIndex,
                s_currentTable.backupGptEntryArrayLba, arraySectors,
                caps.logicalSectorSize, s_backupScan))
            return validated.probe.status = CREATE_PARTITION_READ_UNAVAILABLE;
        if (!bytes_equal(s_primaryScan, s_backupScan,
                         static_cast<size_t>(sectorBytes)))
            return validated.probe.status = CREATE_PARTITION_GPT_DEGRADED;
        uint16_t freeCount = 0;
        uint8_t firstFree = 0xFFu;
        if (!arrays_have_consistent_guids(s_primaryScan, entryCount,
                entrySize, s_currentTable, freeCount, firstFree))
            return validated.probe.status = CREATE_PARTITION_INVALID_TABLE;
        if (freeCount == 0)
            return validated.probe.status = CREATE_PARTITION_NO_GPT_ENTRY;
        validated.probe.freeGptEntries = freeCount;
        validated.slot = firstFree;
        validated.entryCount = entryCount;
        validated.entrySize = entrySize;
        validated.arraySectors = arraySectors;
        validated.arrayBytes = arrayBytes;
        validated.primaryArrayLba = s_currentTable.primaryGptEntryArrayLba;
        validated.backupArrayLba = s_currentTable.backupGptEntryArrayLba;
    } else {
        if (!read_region(target.globalIndex, 0, 1, caps.logicalSectorSize,
                         s_ioSector))
            return validated.probe.status = CREATE_PARTITION_READ_UNAVAILABLE;
        uint8_t firstFree = 0xFFu;
        uint8_t freeCount = 0;
        for (uint8_t slot = 0; slot < 4; ++slot) {
            const uint8_t* entry = s_ioSector + MBR_TABLE_OFFSET +
                static_cast<uint32_t>(slot) * MBR_ENTRY_BYTES;
            if (entry[4] == 0 && entry[0] == 0 &&
                read_u32(entry + 8) == 0 && read_u32(entry + 12) == 0) {
                if (firstFree == 0xFFu) firstFree = slot;
                ++freeCount;
            }
        }
        if (freeCount == 0)
            return validated.probe.status = CREATE_PARTITION_NO_MBR_ENTRY;
        validated.probe.freeMbrEntries = freeCount;
        validated.slot = firstFree;
    }

    if (maxBytes < CREATE_PARTITION_MINIMUM_BYTES)
        return validated.probe.status = CREATE_PARTITION_TOO_SMALL;
    if (revalidate_target_identity(target) != TARGET_VALID)
        return validated.probe.status = map_identity(
            revalidate_target_identity(target));
    validated.probe.status = CREATE_PARTITION_READY;
    return validated.probe.status;
}

static bool capture_snapshot(const TargetIdentity& target,
                             const DeviceCapabilities& caps,
                             const ValidatedTarget& layout)
{
    clear_bytes(&s_snapshot, sizeof(s_snapshot));
    s_snapshot.sectorSize = caps.logicalSectorSize;
    s_snapshot.isGpt = layout.probe.detectedState == DISK_STATE_VALID_GPT;
    if (s_snapshot.isGpt) {
        s_snapshot.primaryArrayLba = layout.primaryArrayLba;
        s_snapshot.backupArrayLba = layout.backupArrayLba;
        s_snapshot.arraySectors = layout.arraySectors;
        s_snapshot.arrayBytes = layout.arrayBytes;
        s_snapshot.entrySize = layout.entrySize;
        s_snapshot.entryCount = layout.entryCount;
        if (!read_region(target.globalIndex, 1, 1, caps.logicalSectorSize,
                         s_snapshot.primaryHeader) ||
            !read_region(target.globalIndex, layout.primaryArrayLba,
                         layout.arraySectors, caps.logicalSectorSize,
                         s_snapshot.primaryArray) ||
            !read_region(target.globalIndex, layout.backupArrayLba,
                         layout.arraySectors, caps.logicalSectorSize,
                         s_snapshot.backupArray) ||
            !read_region(target.globalIndex, caps.totalLogicalSectors - 1, 1,
                         caps.logicalSectorSize, s_snapshot.backupHeader))
            return false;
        const uint64_t paddedBytes = static_cast<uint64_t>(layout.arraySectors) *
            caps.logicalSectorSize;
        if (!bytes_equal(s_snapshot.primaryArray, s_snapshot.backupArray,
                         static_cast<size_t>(paddedBytes))) return false;
    } else if (!read_region(target.globalIndex, 0, 1,
                            caps.logicalSectorSize, s_snapshot.mbr)) {
        return false;
    }
    s_snapshot.valid = true;
    return true;
}

static bool snapshot_matches(const TargetIdentity& target,
                             const DeviceCapabilities& caps)
{
    if (!s_snapshot.valid || s_snapshot.sectorSize != caps.logicalSectorSize)
        return false;
    if (!s_snapshot.isGpt)
        return compare_region(target.globalIndex, 0, 1,
            caps.logicalSectorSize, s_snapshot.mbr);
    const uint32_t sectors = s_snapshot.arraySectors;
    return compare_region(target.globalIndex, 1, 1, caps.logicalSectorSize,
                          s_snapshot.primaryHeader) &&
        compare_region(target.globalIndex, s_snapshot.primaryArrayLba, sectors,
                       caps.logicalSectorSize, s_snapshot.primaryArray) &&
        compare_region(target.globalIndex, s_snapshot.backupArrayLba, sectors,
                       caps.logicalSectorSize, s_snapshot.backupArray) &&
        compare_region(target.globalIndex, caps.totalLogicalSectors - 1, 1,
                       caps.logicalSectorSize, s_snapshot.backupHeader);
}

static bool modify_gpt_entry(const CreatePartitionRequest& request,
                             const ValidatedTarget& layout,
                             uint64_t startLba, uint64_t endLba,
                             uint8_t uniqueGuid[16])
{
    copy_bytes(s_newArray, s_snapshot.primaryArray,
        static_cast<size_t>(layout.arraySectors) * s_snapshot.sectorSize);
    uint8_t* entry = s_newArray +
        static_cast<size_t>(layout.slot) * layout.entrySize;
    clear_bytes(entry, layout.entrySize);
    copy_bytes(entry, GPT_TYPE_BASIC_DATA_GUID, 16);
    copy_bytes(entry + 16, uniqueGuid, 16);
    write_u64(entry + 32, startLba);
    write_u64(entry + 40, endLba);
    write_u64(entry + 48, 0);
    encode_gpt_name(request.gptName, entry);
    const uint32_t entriesCrc = crc32(s_newArray, layout.arrayBytes);

    copy_bytes(s_primaryHeader, s_snapshot.primaryHeader,
               s_snapshot.sectorSize);
    copy_bytes(s_backupHeader, s_snapshot.backupHeader,
               s_snapshot.sectorSize);
    const uint32_t primaryHeaderSize = read_u32(s_primaryHeader + 12);
    const uint32_t backupHeaderSize = read_u32(s_backupHeader + 12);
    if (primaryHeaderSize < GPT_MIN_HEADER_SIZE ||
        primaryHeaderSize > s_snapshot.sectorSize ||
        backupHeaderSize < GPT_MIN_HEADER_SIZE ||
        backupHeaderSize > s_snapshot.sectorSize) return false;
    write_u32(s_primaryHeader + GPT_ARRAY_CRC_OFFSET, entriesCrc);
    write_u32(s_primaryHeader + GPT_HEADER_CRC_OFFSET, 0);
    write_u32(s_primaryHeader + GPT_HEADER_CRC_OFFSET,
              crc32(s_primaryHeader, primaryHeaderSize));
    write_u32(s_backupHeader + GPT_ARRAY_CRC_OFFSET, entriesCrc);
    write_u32(s_backupHeader + GPT_HEADER_CRC_OFFSET, 0);
    write_u32(s_backupHeader + GPT_HEADER_CRC_OFFSET,
              crc32(s_backupHeader, backupHeaderSize));
    return true;
}

static bool write_one(const TargetIdentity& target, uint64_t lba,
                      const uint8_t* source, uint32_t sectorSize,
                      CreatePartitionResult& result)
{
    result.lastStage = result.stage;
    const RevalidationStatus identity = revalidate_target_identity(target);
    if (identity != TARGET_VALID) {
        result.status = map_identity(identity);
        return false;
    }
    copy_bytes(s_ioSector, source, sectorSize);
    const block::Status status = write_sectors_safe(target.globalIndex,
        lba, 1, s_ioSector, sectorSize);
    if (status != block::BLOCK_OK) {
        const bool haveIo = capture_current_io_result(result);
        result.writeAttempted = haveIo &&
            result.failedBlockDiagnostic.callbackInvoked;
        result.firstFailedStage = result.firstFailedStage == CREATE_PARTITION_STAGE_IDLE
            ? result.stage : result.firstFailedStage;
        result.writeMayHaveReachedMedia = result.writeAttempted &&
            (!result.failedBlockDiagnostic.transportDiagnostic.valid ||
             result.failedBlockDiagnostic.transportDiagnostic.dataSectorsTransferred != 0);
        result.failedBeforeWrite = !result.writeMayHaveReachedMedia;
        result.status = CREATE_PARTITION_IO_FAILED;
        return false;
    }
    result.writeAttempted = true;
    result.failedOperation = block::OPERATION_NONE;
    result.writeMayHaveReachedMedia = true;
    result.failedBeforeWrite = false;
    ++result.writesCompleted;
    return true;
}

static bool write_region(const TargetIdentity& target, uint64_t lba,
                         uint32_t sectors, uint32_t sectorSize,
                         const uint8_t* source,
                         CreatePartitionStage stage,
                         CreatePartitionWriteStage completedStage,
                         CreatePartitionResult& result)
{
    for (uint32_t i = 0; i < sectors; ++i) {
        result.stage = stage;
        if (!write_one(target, lba + i,
                source + static_cast<size_t>(i) * sectorSize,
                sectorSize, result)) return false;
    }
    result.writeStagesCompleted |= completedStage;
    return true;
}

static void modify_mbr_entry(const ValidatedTarget& layout,
                             uint64_t startLba, uint64_t sectorCount,
                             uint32_t sectorSize)
{
    copy_bytes(s_ioSector, s_snapshot.mbr, sectorSize);
    uint8_t* entry = s_ioSector + MBR_TABLE_OFFSET +
        static_cast<uint32_t>(layout.slot) * MBR_ENTRY_BYTES;
    clear_bytes(entry, MBR_ENTRY_BYTES);
    entry[0] = 0x00; // New primary partitions are always inactive.
    entry[1] = 0xFE; entry[2] = 0xFF; entry[3] = 0xFF;
    entry[4] = MBR_TYPE_FAT32_LBA;
    entry[5] = 0xFE; entry[6] = 0xFF; entry[7] = 0xFF;
    write_u32(entry + 8, static_cast<uint32_t>(startLba));
    write_u32(entry + 12, static_cast<uint32_t>(sectorCount));
    copy_bytes(s_mbrCandidate, s_ioSector, sectorSize);
}

static bool write_gpt(const TargetIdentity& target,
                      const ValidatedTarget& layout,
                      uint32_t sectorSize,
                      CreatePartitionResult& result)
{
    if (!write_region(target, layout.backupArrayLba, layout.arraySectors,
            sectorSize, s_newArray, CREATE_PARTITION_STAGE_WRITE_BACKUP_GPT,
            CREATE_PARTITION_WRITE_BACKUP_ARRAY, result) ||
        !write_region(target, target.totalLogicalSectors - 1, 1, sectorSize,
            s_backupHeader, CREATE_PARTITION_STAGE_WRITE_BACKUP_GPT,
            CREATE_PARTITION_WRITE_BACKUP_HEADER, result) ||
        !write_region(target, layout.primaryArrayLba, layout.arraySectors,
            sectorSize, s_newArray, CREATE_PARTITION_STAGE_WRITE_PRIMARY_GPT,
            CREATE_PARTITION_WRITE_PRIMARY_ARRAY, result) ||
        !write_region(target, 1, 1, sectorSize, s_primaryHeader,
            CREATE_PARTITION_STAGE_WRITE_PRIMARY_GPT,
            CREATE_PARTITION_WRITE_PRIMARY_HEADER, result)) return false;
    return true;
}

static bool write_mbr(const TargetIdentity& target,
                      const ValidatedTarget& layout,
                      uint64_t startLba, uint64_t sectorCount,
                      uint32_t sectorSize,
                      CreatePartitionResult& result)
{
    modify_mbr_entry(layout, startLba, sectorCount, sectorSize);
    // write_region stages each sector through s_ioSector, so preserve this
    // single-sector candidate in a separate aligned buffer.
    const bool ok = write_region(target, 0, 1, sectorSize, s_mbrCandidate,
        CREATE_PARTITION_STAGE_WRITING_MBR,
        CREATE_PARTITION_WRITE_MBR_ENTRY, result);
    return ok;
}

static bool entry_matches(const PartitionEntry& actual,
                          const PartitionEntry& expected)
{
    return actual.partitionNumber == expected.partitionNumber &&
        actual.isGpt == expected.isGpt && actual.mbrType == expected.mbrType &&
        actual.bootable == expected.bootable &&
        actual.startLba == expected.startLba &&
        actual.endLba == expected.endLba &&
        actual.sectorCount == expected.sectorCount &&
        actual.attributes == expected.attributes &&
        bytes_equal(actual.typeGuid, expected.typeGuid, 16) &&
        bytes_equal(actual.uniqueGuid, expected.uniqueGuid, 16) &&
        disk_manager_text_equal(actual.name, expected.name);
}

static bool old_entries_unchanged(const PartitionTableModel& before,
                                  const PartitionTableModel& after)
{
    for (uint16_t i = 0; i < before.partitionCount; ++i) {
        const PartitionEntry& oldEntry = before.partitions[i];
        bool found = false;
        for (uint16_t j = 0; j < after.partitionCount; ++j) {
            if (after.partitions[j].partitionNumber != oldEntry.partitionNumber)
                continue;
            if (!entry_matches(after.partitions[j], oldEntry)) return false;
            found = true;
            break;
        }
        if (!found) return false;
    }
    return true;
}

static bool locate_created(const CreatePartitionRequest& request,
                           const ValidatedTarget& layout,
                           uint64_t startLba, uint64_t endLba,
                           const uint8_t uniqueGuid[16],
                           PartitionTableModel& table,
                           PartitionEntry& found)
{
    if (!parse_partition_table(request.targetSnapshot.globalIndex, table))
        return false;
    if (request.requestedScheme == PARTITION_SCHEME_GPT) {
        if (table.state != DISK_STATE_VALID_GPT || !table.primaryGptValid ||
            !table.backupGptValid || !table.gptCopiesAgree ||
            table.partitionCount != s_originalTable.partitionCount + 1 ||
            table.tableEntryCount != s_originalTable.tableEntryCount ||
            table.gptEntrySize != s_originalTable.gptEntrySize ||
            table.gptEntryArraySectors != s_originalTable.gptEntryArraySectors ||
            table.primaryGptEntryArrayLba != s_originalTable.primaryGptEntryArrayLba ||
            table.backupGptEntryArrayLba != s_originalTable.backupGptEntryArrayLba ||
            table.firstUsableLba != s_originalTable.firstUsableLba ||
            table.lastUsableLba != s_originalTable.lastUsableLba ||
            !guid_equal(table.primaryDiskGuid, s_originalTable.primaryDiskGuid) ||
            !guid_equal(table.backupDiskGuid, s_originalTable.backupDiskGuid) ||
            !old_entries_unchanged(s_originalTable, table)) return false;
        bool foundPartition = false;
        for (uint16_t i = 0; i < table.partitionCount; ++i) {
            const PartitionEntry& candidate = table.partitions[i];
            if (candidate.partitionNumber != static_cast<uint16_t>(layout.slot + 1))
                continue;
            if (candidate.startLba != startLba || candidate.endLba != endLba ||
                !candidate.isGpt ||
                !guid_equal(candidate.typeGuid, GPT_TYPE_BASIC_DATA_GUID) ||
                !guid_equal(candidate.uniqueGuid, uniqueGuid) ||
                candidate.attributes != 0) return false;
            char expectedName[64];
            normalized_gpt_name(request.gptName, expectedName);
            if (!disk_manager_text_equal(candidate.name, expectedName)) return false;
            found = candidate;
            foundPartition = true;
            break;
        }
        return foundPartition;
    }

    if (table.state != DISK_STATE_VALID_MBR || table.extendedPartitionsPresent ||
        table.hybridMbr || table.partitionCount != s_originalTable.partitionCount + 1 ||
        table.mbrDiskSignature != s_originalTable.mbrDiskSignature ||
        !old_entries_unchanged(s_originalTable, table)) return false;
    bool foundPartition = false;
    for (uint16_t i = 0; i < table.partitionCount; ++i) {
        const PartitionEntry& candidate = table.partitions[i];
        if (candidate.partitionNumber != static_cast<uint16_t>(layout.slot + 1))
            continue;
        if (candidate.startLba != startLba || candidate.endLba != endLba ||
            candidate.mbrType != MBR_TYPE_FAT32_LBA || candidate.bootable)
            return false;
        found = candidate;
        foundPartition = true;
        break;
    }
    return foundPartition;
}

static bool verify_gpt_bytes(const TargetIdentity& target,
                             const ValidatedTarget& layout,
                             uint32_t sectorSize)
{
    const uint64_t padded64 = static_cast<uint64_t>(layout.arraySectors) *
        sectorSize;
    if (padded64 > GPT_MAX_ARRAY_BYTES) return false;
    const size_t padded = static_cast<size_t>(padded64);
    const size_t slotOffset = static_cast<size_t>(layout.slot) * layout.entrySize;
    const size_t slotEnd = slotOffset + layout.entrySize;
    if (slotEnd > layout.arrayBytes ||
        !bytes_equal(s_newArray, s_snapshot.primaryArray, slotOffset) ||
        !bytes_equal(s_newArray + slotEnd, s_snapshot.primaryArray + slotEnd,
                     padded - slotEnd)) return false;
    if (!read_region(target.globalIndex, layout.primaryArrayLba,
            layout.arraySectors, sectorSize, s_primaryScan) ||
        !read_region(target.globalIndex, layout.backupArrayLba,
            layout.arraySectors, sectorSize, s_backupScan) ||
        !bytes_equal(s_primaryScan, s_newArray, padded) ||
        !bytes_equal(s_backupScan, s_newArray, padded) ||
        !bytes_equal(s_primaryScan, s_backupScan, padded)) return false;
    if (!read_region(target.globalIndex, 1, 1, sectorSize, s_ioSector) ||
        !bytes_equal(s_ioSector, s_primaryHeader, sectorSize) ||
        !read_region(target.globalIndex, target.totalLogicalSectors - 1,
            1, sectorSize, s_ioSector) ||
        !bytes_equal(s_ioSector, s_backupHeader, sectorSize)) return false;
    // The GPT path never writes LBA 0; the normal parser confirms its
    // protective-MBR semantics as part of the independent table read-back.
    return true;
}

static bool verify_mbr_bytes(const TargetIdentity& target,
                             const ValidatedTarget& layout,
                             uint32_t sectorSize,
                             uint64_t startLba, uint64_t sectorCount)
{
    if (!read_region(target.globalIndex, 0, 1, sectorSize, s_ioSector))
        return false;
    const uint32_t slotOffset = MBR_TABLE_OFFSET +
        static_cast<uint32_t>(layout.slot) * MBR_ENTRY_BYTES;
    for (uint32_t i = 0; i < sectorSize; ++i) {
        if (i < slotOffset || i >= slotOffset + MBR_ENTRY_BYTES) {
            if (s_ioSector[i] != s_snapshot.mbr[i]) return false;
        }
    }
    const uint8_t* entry = s_ioSector + slotOffset;
    return s_ioSector[MBR_SIGNATURE_OFFSET] == 0x55 &&
        s_ioSector[MBR_SIGNATURE_OFFSET + 1] == 0xAA &&
        entry[0] == 0 && entry[4] == MBR_TYPE_FAT32_LBA &&
        read_u32(entry + 8) == startLba &&
        read_u32(entry + 12) == sectorCount;
}

static bool restore_region(const TargetIdentity& target, uint64_t lba,
                           uint32_t sectors, uint32_t sectorSize,
                           const uint8_t* snapshot,
                           CreatePartitionResult& result)
{
    for (uint32_t i = 0; i < sectors; ++i) {
        result.rollbackStage = CREATE_PARTITION_STAGE_ROLLBACK_WRITE;
        if (revalidate_target_identity(target) != TARGET_VALID) return false;
        copy_bytes(s_ioSector,
            snapshot + static_cast<size_t>(i) * sectorSize, sectorSize);
        result.rollbackWriteAttempted = true;
        result.rollbackWriteStatus = write_sectors_safe(
            target.globalIndex, lba + i, 1, s_ioSector, sectorSize);
        if (result.rollbackWriteStatus != block::BLOCK_OK) return false;
    }
    return true;
}

static bool restore_snapshot(const TargetIdentity& target,
                             CreatePartitionResult& result)
{
    if (!result.writeMayHaveReachedMedia || !s_snapshot.valid ||
        revalidate_target_identity(target) != TARGET_VALID) return false;
    DeviceCapabilities caps;
    if (!query_device_capabilities(target.globalIndex, caps) ||
        (caps.persistence != PERSISTENCE_SYNCHRONOUS_DURABLE &&
         !(caps.persistence == PERSISTENCE_FLUSH_REQUIRED &&
           caps.flushSupported && caps.flushSemanticsKnown))) return false;
    result.rollbackAttempted = true;
    bool restored = true;
    if (s_snapshot.isGpt) {
        restored = restore_region(target, 1, 1, s_snapshot.sectorSize,
                                  s_snapshot.primaryHeader, result) && restored;
        restored = restore_region(target,
            target.totalLogicalSectors - 1, 1, s_snapshot.sectorSize,
            s_snapshot.backupHeader, result) && restored;
        restored = restore_region(target, s_snapshot.primaryArrayLba,
            s_snapshot.arraySectors, s_snapshot.sectorSize,
            s_snapshot.primaryArray, result) && restored;
        restored = restore_region(target, s_snapshot.backupArrayLba,
            s_snapshot.arraySectors, s_snapshot.sectorSize,
            s_snapshot.backupArray, result) && restored;
    } else {
        restored = restore_region(target, 0, 1, s_snapshot.sectorSize,
                                  s_snapshot.mbr, result) && restored;
    }
    if (!restored || revalidate_target_identity(target) != TARGET_VALID)
        return false;
    result.rollbackStage = CREATE_PARTITION_STAGE_ROLLBACK_FLUSH;
    result.rollbackFlushAttempted = true;
    const block::FlushReport flush = block::flush_with_result(target.globalIndex);
    result.rollbackFlushOutcome = flush.outcome;
    result.rollbackFlushStatus = flush.status;
    if (!trusted_flush(flush)) return false;
    result.rollbackStage = CREATE_PARTITION_STAGE_ROLLBACK_VERIFY;
    if (!snapshot_matches(target, caps)) return false;
    if (!parse_partition_table(target.globalIndex, s_verifiedTable) ||
        !old_entries_unchanged(s_originalTable, s_verifiedTable) ||
        s_verifiedTable.state != s_originalTable.state ||
        s_verifiedTable.partitionCount != s_originalTable.partitionCount)
        return false;
    result.finalDetectedState = s_verifiedTable.state;
    result.finalPartitionCount = s_verifiedTable.partitionCount;
    result.rollbackVerificationPassed = true;
    return true;
}

static void finish_failure(StorageOperationLease& lease,
                           bool executionStarted,
                           const TargetIdentity& target,
                           CreatePartitionResult& result,
                           CreatePartitionStatus status)
{
    result.lastStage = result.stage;
    if (result.firstFailedStage == CREATE_PARTITION_STAGE_IDLE)
        result.firstFailedStage = result.lastStage;
    result.failureStatus = status;
    result.status = status;
    mark_create_failed_stage(result);
    set_diagnostic(result, create_partition_status_name(status));
    result.failedBeforeWrite = !result.writeMayHaveReachedMedia;
    if (result.writeMayHaveReachedMedia) {
        result.rollbackSucceeded = restore_snapshot(target, result);
        if (!result.rollbackSucceeded) {
            result.finalStateUncertain = true;
            result.status = CREATE_PARTITION_ROLLBACK_FAILED;
            result.stage = CREATE_PARTITION_STAGE_STATE_UNCERTAIN;
            set_diagnostic(result,
                "Partition creation failed; rollback could not be verified. Refresh and inspect the disk.");
        }
    }
    if (revalidate_target_identity(target) != TARGET_VALID) {
        if (result.writeMayHaveReachedMedia) {
            result.finalStateUncertain = true;
            result.status = CREATE_PARTITION_ROLLBACK_FAILED;
            result.stage = CREATE_PARTITION_STAGE_STATE_UNCERTAIN;
            set_diagnostic(result,
                "Disk identity changed during creation; state is uncertain. Refresh and inspect the disk.");
        }
    } else if (result.writeMayHaveReachedMedia && !result.rollbackSucceeded) {
        if (parse_partition_table(target.globalIndex, s_verifiedTable)) {
            result.finalDetectedState = s_verifiedTable.state;
            result.finalPartitionCount = s_verifiedTable.partitionCount;
        } else {
            result.finalDetectedState = DISK_STATE_UNREADABLE;
            result.finalStateUncertain = true;
        }
    }
    if (executionStarted) complete_storage_operation_execution(lease);
    else release_storage_operation(lease);
    clear_bytes(&s_snapshot, sizeof(s_snapshot));
}

static bool calculate_size(const CreatePartitionRequest& request,
                           const ValidatedTarget& layout,
                           uint32_t sectorSize, uint64_t& sectors,
                           uint64_t& endLba)
{
    if (request.useMaximumSize) {
        sectors = layout.probe.maximumSectorCount;
    } else {
        if (request.requestedSizeBytes < CREATE_PARTITION_MINIMUM_BYTES)
            return false;
        sectors = request.requestedSizeBytes / sectorSize;
        if (sectors == 0) return false;
        if (sectors > layout.probe.maximumSectorCount) return false;
        if (sectors > UINT64_MAX / sectorSize ||
            sectors * sectorSize > request.requestedSizeBytes) return false;
    }
    if (sectors == 0 || sectors > UINT64_MAX / sectorSize ||
        sectors * sectorSize < CREATE_PARTITION_MINIMUM_BYTES ||
        layout.probe.firstAlignedLba > UINT64_MAX - (sectors - 1)) return false;
    endLba = layout.probe.firstAlignedLba + sectors - 1;
    return endLba <= layout.probe.firstAlignedLba +
        layout.probe.maximumSectorCount - 1;
}

} // namespace

CreatePartitionStatus probe_create_partition(
    const TargetIdentity& target, PartitionScheme scheme,
    const UnallocatedRegion& selectedRegion, CreatePartitionProbe& result)
{
    reset_probe(result);
    StorageOperationLease lease = {};
    const StorageOperationLockStatus lock = try_acquire_storage_operation(lease);
    if (lock != STORAGE_OPERATION_LOCK_ACQUIRED) {
        result.status = lock == STORAGE_OPERATION_LOCK_BUSY
            ? CREATE_PARTITION_OPERATION_BUSY
            : CREATE_PARTITION_OPERATION_OWNERSHIP_INVALID;
        return result.status;
    }
    if (!pin_storage_operation_target(lease, target)) {
        result.status = map_identity(revalidate_target_identity(target));
        if (result.status == CREATE_PARTITION_SUCCESS)
            result.status = CREATE_PARTITION_IDENTITY_CHANGED;
        release_storage_operation(lease);
        return result.status;
    }
    ValidatedTarget validated;
    const CreatePartitionStatus status = validate_target(target, scheme,
        selectedRegion, lease, validated);
    result = validated.probe;
    release_storage_operation(lease);
    return status;
}

CreatePartitionStatus create_partition(const CreatePartitionRequest& request,
                                       CreatePartitionResult& result)
{
    reset_result(result);
    result.targetIdentity = request.targetSnapshot;
    result.requestedScheme = request.requestedScheme;
    result.stage = CREATE_PARTITION_STAGE_VALIDATING;
    if (request.targetSnapshot.registrationId == 0 ||
        request.expectedRegistryGeneration !=
            request.targetSnapshot.registryGeneration ||
        (request.requestedScheme != PARTITION_SCHEME_GPT &&
         request.requestedScheme != PARTITION_SCHEME_MBR) ||
        (!request.useMaximumSize && request.requestedSizeBytes == 0)) {
        result.status = CREATE_PARTITION_INVALID_REQUEST;
        mark_create_failed_stage(result);
        set_diagnostic(result, create_partition_status_name(result.status));
        return result.status;
    }
    if ((request.requestedScheme == PARTITION_SCHEME_GPT &&
         request.partitionType != CREATE_PARTITION_GPT_BASIC_DATA) ||
        (request.requestedScheme == PARTITION_SCHEME_MBR &&
         request.partitionType != CREATE_PARTITION_MBR_FAT32_LBA)) {
        result.status = CREATE_PARTITION_UNSUPPORTED_TYPE;
        mark_create_failed_stage(result);
        set_diagnostic(result, create_partition_status_name(result.status));
        return result.status;
    }

    StorageOperationLease lease = {};
    const StorageOperationLockStatus lock = try_acquire_storage_operation(lease);
    if (lock != STORAGE_OPERATION_LOCK_ACQUIRED) {
        result.status = lock == STORAGE_OPERATION_LOCK_BUSY
            ? CREATE_PARTITION_OPERATION_BUSY
            : CREATE_PARTITION_OPERATION_OWNERSHIP_INVALID;
        mark_create_failed_stage(result);
        set_diagnostic(result, create_partition_status_name(result.status));
        return result.status;
    }
    result.stage = CREATE_PARTITION_STAGE_PIN_TARGET;
    if (!pin_storage_operation_target(lease, request.targetSnapshot)) {
        result.status = map_identity(revalidate_target_identity(request.targetSnapshot));
        if (result.status == CREATE_PARTITION_SUCCESS)
            result.status = CREATE_PARTITION_IDENTITY_CHANGED;
        mark_create_failed_stage(result);
        set_diagnostic(result, create_partition_status_name(result.status));
        release_storage_operation(lease);
        return result.status;
    }

    ValidatedTarget layout;
    result.stage = CREATE_PARTITION_STAGE_PREFLIGHT;
    CreatePartitionStatus status = validate_target(request.targetSnapshot,
        request.requestedScheme, request.selectedRegion, lease, layout);
    if (status != CREATE_PARTITION_READY) {
        result.status = status;
        mark_create_failed_stage(result);
        set_diagnostic(result, create_partition_status_name(status));
        release_storage_operation(lease);
        return status;
    }
    result.finalDetectedState = layout.probe.detectedState;
    result.finalPartitionCount = s_currentTable.partitionCount;
    DeviceCapabilities caps;
    if (!query_device_capabilities(request.targetSnapshot.globalIndex, caps)) {
        result.status = CREATE_PARTITION_DEVICE_MISSING;
        mark_create_failed_stage(result);
        set_diagnostic(result, create_partition_status_name(result.status));
        release_storage_operation(lease);
        return result.status;
    }
    uint64_t sectorCount = 0;
    uint64_t endLba = 0;
    if (!calculate_size(request, layout, caps.logicalSectorSize,
                        sectorCount, endLba)) {
        status = request.requestedSizeBytes < CREATE_PARTITION_MINIMUM_BYTES
            ? CREATE_PARTITION_TOO_SMALL
            : (request.useMaximumSize ? CREATE_PARTITION_TOO_SMALL
                                      : CREATE_PARTITION_TOO_LARGE);
        result.status = status;
        mark_create_failed_stage(result);
        set_diagnostic(result, create_partition_status_name(status));
        release_storage_operation(lease);
        return status;
    }
    if (request.requestedScheme == PARTITION_SCHEME_MBR &&
        (layout.probe.firstAlignedLba > 0xFFFFFFFFull ||
         sectorCount > 0xFFFFFFFFull || endLba > 0xFFFFFFFFull)) {
        result.status = CREATE_PARTITION_TOO_LARGE;
        mark_create_failed_stage(result);
        set_diagnostic(result, create_partition_status_name(result.status));
        release_storage_operation(lease);
        return result.status;
    }
    const uint64_t capacityBytes = sectorCount * caps.logicalSectorSize;
    result.createdPartition.partitionNumber =
        static_cast<uint16_t>(layout.slot + 1);
    result.createdPartition.isGpt = request.requestedScheme == PARTITION_SCHEME_GPT;
    result.createdPartition.startLba = layout.probe.firstAlignedLba;
    result.createdPartition.endLba = endLba;
    result.createdPartition.sectorCount = sectorCount;
    result.createdPartition.mbrType = request.requestedScheme == PARTITION_SCHEME_MBR
        ? MBR_TYPE_FAT32_LBA : 0;
    result.partitionTableSlot = layout.slot;
    if (request.requestedScheme == PARTITION_SCHEME_GPT)
        copy_bytes(result.createdPartition.typeGuid, GPT_TYPE_BASIC_DATA_GUID, 16);

    s_originalTable = s_currentTable;
    result.stage = CREATE_PARTITION_STAGE_SNAPSHOT_TABLE;
    result.lastStage = result.stage;
    if (!capture_snapshot(request.targetSnapshot, caps, layout)) {
        (void)capture_current_io_result(result);
        result.firstFailedStage = result.stage;
        result.status = CREATE_PARTITION_READ_UNAVAILABLE;
        mark_create_failed_stage(result);
        set_diagnostic(result, create_partition_status_name(result.status));
        release_storage_operation(lease);
        clear_bytes(&s_snapshot, sizeof(s_snapshot));
        return result.status;
    }

    uint8_t newGuid[16] = {};
    result.stage = CREATE_PARTITION_STAGE_PREPARE_METADATA;
    result.lastStage = result.stage;
    if (request.requestedScheme == PARTITION_SCHEME_GPT) {
        bool unique = false;
        for (uint8_t attempt = 0; attempt < 8 && !unique; ++attempt) {
            if (!random_guid(newGuid, request)) break;
            unique = !guid_in_table(newGuid, s_originalTable);
#if defined(KERNEL_STORAGE_TEST)
            // An injected collision is a deterministic test failure.
            if (request.testGuidProvided && !unique) break;
#endif
        }
        if (bytes_zero(newGuid, sizeof(newGuid))) {
            result.status = CREATE_PARTITION_GUID_UNAVAILABLE;
            mark_create_failed_stage(result);
            set_diagnostic(result, create_partition_status_name(result.status));
            release_storage_operation(lease);
            clear_bytes(&s_snapshot, sizeof(s_snapshot));
            return result.status;
        }
        if (!unique) {
            result.status = CREATE_PARTITION_GUID_COLLISION;
            mark_create_failed_stage(result);
            set_diagnostic(result, create_partition_status_name(result.status));
            release_storage_operation(lease);
            clear_bytes(&s_snapshot, sizeof(s_snapshot));
            return result.status;
        }
        copy_bytes(result.createdPartition.uniqueGuid, newGuid, 16);
        if (!modify_gpt_entry(request, layout,
                layout.probe.firstAlignedLba, endLba, newGuid)) {
            result.status = CREATE_PARTITION_INVALID_TABLE;
            mark_create_failed_stage(result);
            set_diagnostic(result, create_partition_status_name(result.status));
            release_storage_operation(lease);
            clear_bytes(&s_snapshot, sizeof(s_snapshot));
            return result.status;
        }
    }

    if (!begin_storage_operation_execution(lease)) {
        result.status = CREATE_PARTITION_OPERATION_OWNERSHIP_INVALID;
        mark_create_failed_stage(result);
        set_diagnostic(result, create_partition_status_name(result.status));
        release_storage_operation(lease);
        clear_bytes(&s_snapshot, sizeof(s_snapshot));
        return result.status;
    }
    bool executionStarted = true;
    result.stage = CREATE_PARTITION_STAGE_REVALIDATING_FREE_SPACE;
    result.lastStage = result.stage;
    ValidatedTarget finalLayout;
    status = validate_target(request.targetSnapshot, request.requestedScheme,
        request.selectedRegion, lease, finalLayout);
    if (status != CREATE_PARTITION_READY || finalLayout.slot != layout.slot ||
        finalLayout.probe.firstAlignedLba != layout.probe.firstAlignedLba ||
        finalLayout.probe.maximumSectorCount != layout.probe.maximumSectorCount ||
        !snapshot_matches(request.targetSnapshot, caps)) {
        if (status == CREATE_PARTITION_READY)
            status = CREATE_PARTITION_STALE_REGION;
        finish_failure(lease, executionStarted, request.targetSnapshot,
                       result, status);
        return result.status;
    }
    // Re-check the requested range against the freshly derived bounds.
    uint64_t finalSectors = 0, finalEnd = 0;
    if (!calculate_size(request, finalLayout, caps.logicalSectorSize,
                        finalSectors, finalEnd) ||
        finalSectors != sectorCount || finalEnd != endLba) {
        finish_failure(lease, executionStarted, request.targetSnapshot,
            result, CREATE_PARTITION_STALE_REGION);
        return result.status;
    }
    if (request.requestedScheme == PARTITION_SCHEME_GPT &&
        !bytes_equal(s_primaryScan, s_backupScan,
            static_cast<size_t>(finalLayout.arraySectors) * caps.logicalSectorSize)) {
        finish_failure(lease, executionStarted, request.targetSnapshot,
            result, CREATE_PARTITION_GPT_DEGRADED);
        return result.status;
    }
    result.stage = CREATE_PARTITION_STAGE_FLUSH;
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
        finish_failure(lease, executionStarted, request.targetSnapshot,
            result, CREATE_PARTITION_FLUSH_FAILED);
        return result.status;
    }
    if (revalidate_target_identity(request.targetSnapshot) != TARGET_VALID ||
        !snapshot_matches(request.targetSnapshot, caps)) {
        status = revalidate_target_identity(request.targetSnapshot) == TARGET_VALID
            ? CREATE_PARTITION_STALE_REGION
            : map_identity(revalidate_target_identity(request.targetSnapshot));
        finish_failure(lease, executionStarted, request.targetSnapshot,
                       result, status);
        return result.status;
    }

    bool writeOk = request.requestedScheme == PARTITION_SCHEME_GPT
        ? write_gpt(request.targetSnapshot, layout, caps.logicalSectorSize, result)
        : write_mbr(request.targetSnapshot, layout,
                    layout.probe.firstAlignedLba, sectorCount,
                    caps.logicalSectorSize, result);
    if (!writeOk) {
        status = result.status == CREATE_PARTITION_SUCCESS
            ? CREATE_PARTITION_IO_FAILED : result.status;
        finish_failure(lease, executionStarted, request.targetSnapshot,
                       result, status);
        return result.status;
    }

    result.stage = CREATE_PARTITION_STAGE_FLUSH;
    result.lastStage = result.stage;
    result.flushAttempted = true;
    ++result.flushAttempts;
    const block::FlushReport writeFlush =
        block::flush_with_result(request.targetSnapshot.globalIndex);
    result.flushOutcome = writeFlush.outcome;
    result.flushStatus = writeFlush.status;
    if (!trusted_flush(writeFlush)) {
        result.failedOperation = block::OPERATION_FLUSH;
        (void)capture_current_io_result(result);
        finish_failure(lease, executionStarted, request.targetSnapshot,
            result, CREATE_PARTITION_FLUSH_FAILED);
        return result.status;
    }

    result.stage = CREATE_PARTITION_STAGE_VERIFY;
    result.lastStage = result.stage;
    if (!locate_created(request, layout, layout.probe.firstAlignedLba,
            endLba, newGuid, s_verifiedTable, result.createdPartition)) {
        finish_failure(lease, executionStarted, request.targetSnapshot,
            result, CREATE_PARTITION_VERIFICATION_FAILED);
        return result.status;
    }
    if (request.requestedScheme == PARTITION_SCHEME_GPT) {
        if (!verify_gpt_bytes(request.targetSnapshot, layout,
                              caps.logicalSectorSize)) {
            finish_failure(lease, executionStarted, request.targetSnapshot,
                result, CREATE_PARTITION_VERIFICATION_FAILED);
            return result.status;
        }
    } else if (!verify_mbr_bytes(request.targetSnapshot, layout,
            caps.logicalSectorSize, layout.probe.firstAlignedLba, sectorCount)) {
        finish_failure(lease, executionStarted, request.targetSnapshot,
            result, CREATE_PARTITION_VERIFICATION_FAILED);
        return result.status;
    }
    result.verificationPassed = true;

    result.stage = CREATE_PARTITION_STAGE_RESCAN;
    result.lastStage = result.stage;
    if (!parse_partition_table(request.targetSnapshot.globalIndex,
                               s_currentTable) ||
        s_currentTable.state != (request.requestedScheme == PARTITION_SCHEME_GPT
            ? DISK_STATE_VALID_GPT : DISK_STATE_VALID_MBR) ||
        !compute_unallocated_regions(s_currentTable, caps.totalLogicalSectors,
            caps.logicalSectorSize, s_regions, MAX_UNALLOCATED_REGIONS,
            result.finalUnallocatedRegionCount) ||
        revalidate_target_identity(request.targetSnapshot) != TARGET_VALID) {
        finish_failure(lease, executionStarted, request.targetSnapshot,
            result, CREATE_PARTITION_RESCAN_FAILED);
        return result.status;
    }
    result.finalDetectedState = s_currentTable.state;
    result.finalPartitionCount = s_currentTable.partitionCount;
    result.status = CREATE_PARTITION_SUCCESS;
    result.failureStatus = CREATE_PARTITION_SUCCESS;
    result.stage = CREATE_PARTITION_STAGE_COMPLETED;
    result.lastStage = CREATE_PARTITION_STAGE_COMPLETED;
    result.failedBeforeWrite = false;
    set_diagnostic(result,
        "Partition created and verified. It remains unformatted and unmounted.");
    complete_storage_operation_execution(lease);
    executionStarted = false;
    clear_bytes(&s_snapshot, sizeof(s_snapshot));
    clear_bytes(newGuid, sizeof(newGuid));
    (void)capacityBytes;
    return result.status;
}

namespace {

static const uint32_t DELETE_GPT_HEADER_CRC_OFFSET = 16;
static const uint32_t DELETE_GPT_ARRAY_CRC_OFFSET = 88;
static const uint32_t DELETE_GPT_MIN_HEADER_SIZE = 92;
static const uint32_t DELETE_MBR_TABLE_OFFSET = 446;
static const uint32_t DELETE_MBR_ENTRY_BYTES = 16;
static const uint8_t DELETE_MAX_ENTRY_SECTORS = 2;

struct DeleteMetadataSnapshot {
    bool valid;
    bool isGpt;
    uint32_t sectorSize;
    uint32_t arrayBytes;
    uint32_t arraySectors;
    uint32_t entrySize;
    uint16_t entryCount;
    uint16_t slot;
    uint8_t touchedSectorCount;
    uint64_t primaryHeaderLba;
    uint64_t backupHeaderLba;
    uint64_t primaryArrayLba[DELETE_MAX_ENTRY_SECTORS];
    uint64_t backupArrayLba[DELETE_MAX_ENTRY_SECTORS];
    uint8_t mbr[MAX_LOGICAL_SECTOR_SIZE];
    uint8_t primaryHeader[MAX_LOGICAL_SECTOR_SIZE];
    uint8_t backupHeader[MAX_LOGICAL_SECTOR_SIZE];
    uint8_t primarySectors[DELETE_MAX_ENTRY_SECTORS][MAX_LOGICAL_SECTOR_SIZE];
    uint8_t backupSectors[DELETE_MAX_ENTRY_SECTORS][MAX_LOGICAL_SECTOR_SIZE];
};

alignas(4096) static uint8_t s_deleteMbr[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_deleteNewMbr[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_deletePrimaryArray[GPT_MAX_ARRAY_BYTES];
alignas(4096) static uint8_t s_deleteBackupArray[GPT_MAX_ARRAY_BYTES];
alignas(4096) static uint8_t s_deleteNewPrimaryArray[GPT_MAX_ARRAY_BYTES];
alignas(4096) static uint8_t s_deleteNewBackupArray[GPT_MAX_ARRAY_BYTES];
alignas(4096) static uint8_t s_deleteNewPrimaryHeader[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_deleteNewBackupHeader[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static PartitionTableModel s_deleteTable;
alignas(4096) static PartitionTableModel s_deleteVerifiedTable;
static DeleteMetadataSnapshot s_deleteSnapshot;
static DeletePartitionPlan s_deletePreparedPlan;
static bool s_deletePlanValid;

static void reset_delete_result(DeletePartitionResult& result)
{
    clear_bytes(&result, sizeof(result));
    result.status = DELETE_PARTITION_INVALID_REQUEST;
    result.failureStatus = DELETE_PARTITION_INVALID_REQUEST;
    result.stage = DELETE_PARTITION_STAGE_IDLE;
    result.finalDetectedState = DISK_STATE_UNREADABLE;
    result.failedBeforeWrite = true;
    result.blockStatus = block::BLOCK_ERR_INVALID;
    result.failedOperation = block::OPERATION_NONE;
    result.flushOutcome = block::FLUSH_OUTCOME_INVALID;
    result.flushStatus = block::BLOCK_ERR_INVALID;
    result.rollbackWriteStatus = block::BLOCK_ERR_INVALID;
    result.rollbackFlushOutcome = block::FLUSH_OUTCOME_INVALID;
    result.rollbackFlushStatus = block::BLOCK_ERR_INVALID;
}

static void set_delete_diagnostic(DeletePartitionResult& result,
                                  const char* message)
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

static DeletePartitionStatus delete_map_identity(RevalidationStatus status)
{
    switch (status) {
        case TARGET_REGISTRY_CHANGED: return DELETE_PARTITION_REGISTRY_CHANGED;
        case TARGET_DEVICE_MISSING: return DELETE_PARTITION_DEVICE_MISSING;
        case TARGET_IDENTITY_MISMATCH: return DELETE_PARTITION_IDENTITY_CHANGED;
        case TARGET_VALID: default: return DELETE_PARTITION_SUCCESS;
    }
}

static bool delete_entry_equal(const PartitionEntry& left,
                               const PartitionEntry& right)
{
    return entry_matches(left, right);
}

static const PartitionEntry* find_delete_entry(const PartitionTableModel& table,
                                               const PartitionEntry& wanted)
{
    for (uint16_t i = 0; i < table.partitionCount; ++i)
        if (delete_entry_equal(table.partitions[i], wanted))
            return &table.partitions[i];
    return nullptr;
}

static uint32_t mbr_table_fingerprint(const uint8_t* sector)
{
    return crc32(sector + 440, 72);
}

static void capture_delete_fingerprint(const PartitionTableModel& table,
                                      DeletePartitionPlan& plan,
                                      const uint8_t* mbrSector)
{
    plan.parserState = table.state;
    plan.partitionCount = table.partitionCount;
    plan.tableEntryCount = table.tableEntryCount;
    plan.gptEntrySize = table.gptEntrySize;
    plan.gptEntryArraySectors = table.gptEntryArraySectors;
    plan.primaryGptEntryArrayLba = table.primaryGptEntryArrayLba;
    plan.backupGptEntryArrayLba = table.backupGptEntryArrayLba;
    plan.firstUsableLba = table.firstUsableLba;
    plan.lastUsableLba = table.lastUsableLba;
    plan.primaryGptHeaderCrc32 = table.primaryGptHeaderCrc32;
    plan.backupGptHeaderCrc32 = table.backupGptHeaderCrc32;
    plan.primaryGptEntryArrayCrc32 = table.primaryGptEntryArrayCrc32;
    plan.backupGptEntryArrayCrc32 = table.backupGptEntryArrayCrc32;
    plan.mbrTableCrc32 = mbrSector ? mbr_table_fingerprint(mbrSector) : 0;
    plan.mbrDiskSignature = table.mbrDiskSignature;
    copy_bytes(plan.primaryDiskGuid, table.primaryDiskGuid, 16);
    copy_bytes(plan.backupDiskGuid, table.backupDiskGuid, 16);
}

static bool delete_fingerprint_matches(const DeletePartitionPlan& plan,
                                       const PartitionTableModel& table,
                                       const uint8_t* mbrSector)
{
    if (plan.parserState != table.state ||
        plan.partitionCount != table.partitionCount ||
        plan.tableEntryCount != table.tableEntryCount ||
        plan.mbrDiskSignature != table.mbrDiskSignature) return false;
    if (plan.partitionScheme == PARTITION_SCHEME_MBR)
        return mbrSector && plan.mbrTableCrc32 ==
            mbr_table_fingerprint(mbrSector);
    return plan.gptEntrySize == table.gptEntrySize &&
        plan.gptEntryArraySectors == table.gptEntryArraySectors &&
        plan.primaryGptEntryArrayLba == table.primaryGptEntryArrayLba &&
        plan.backupGptEntryArrayLba == table.backupGptEntryArrayLba &&
        plan.firstUsableLba == table.firstUsableLba &&
        plan.lastUsableLba == table.lastUsableLba &&
        plan.primaryGptHeaderCrc32 == table.primaryGptHeaderCrc32 &&
        plan.backupGptHeaderCrc32 == table.backupGptHeaderCrc32 &&
        plan.primaryGptEntryArrayCrc32 == table.primaryGptEntryArrayCrc32 &&
        plan.backupGptEntryArrayCrc32 == table.backupGptEntryArrayCrc32 &&
        guid_equal(plan.primaryDiskGuid, table.primaryDiskGuid) &&
        guid_equal(plan.backupDiskGuid, table.backupDiskGuid);
}

static DeletePartitionStatus validate_delete_target(
    const TargetIdentity& target, PartitionScheme scheme,
    const PartitionEntry& selected, const StorageOperationLease& lease,
    PartitionTableModel& table, DeletePartitionResult& result)
{
    if (!storage_operation_lease_is_current(lease))
        return DELETE_PARTITION_OPERATION_OWNERSHIP_INVALID;
    const RevalidationStatus identity = revalidate_target_identity(target);
    if (identity != TARGET_VALID) return delete_map_identity(identity);
    DeviceCapabilities caps;
    if (!query_device_capabilities(target.globalIndex, caps))
        return DELETE_PARTITION_DEVICE_MISSING;
    if (!caps.geometryValid || !caps.capacityValid ||
        (caps.logicalSectorSize != 512 && caps.logicalSectorSize != 4096))
        return DELETE_PARTITION_INVALID_GEOMETRY;
    if (!caps.readable) return DELETE_PARTITION_READ_UNAVAILABLE;
    if (!caps.writable) return DELETE_PARTITION_READ_ONLY;
    if (caps.maxTransferBytes != 0 &&
        caps.maxTransferBytes < caps.logicalSectorSize)
        return DELETE_PARTITION_WRITE_UNAVAILABLE;
    if (caps.persistence == PERSISTENCE_FLUSH_REQUIRED) {
        if (!caps.flushSupported || !caps.flushSemanticsKnown)
            return DELETE_PARTITION_FLUSH_UNAVAILABLE;
    } else if (caps.persistence != PERSISTENCE_SYNCHRONOUS_DURABLE) {
        return DELETE_PARTITION_DURABILITY_UNKNOWN;
    }
    if (caps.requiredBufferAlignment > MAX_LOGICAL_SECTOR_SIZE ||
        (caps.requiredBufferAlignment > 1 &&
         (caps.requiredBufferAlignment &
          (caps.requiredBufferAlignment - 1)) != 0))
        return DELETE_PARTITION_INVALID_GEOMETRY;

    if (!parse_partition_table(target.globalIndex, table) ||
        table.state == DISK_STATE_UNREADABLE)
        return DELETE_PARTITION_READ_UNAVAILABLE;
    if (table.state == DISK_STATE_GPT_DEGRADED)
        return DELETE_PARTITION_GPT_DEGRADED;
    if (table.state == DISK_STATE_INVALID_PARTITION_TABLE)
        return DELETE_PARTITION_INVALID_TABLE;
    if (table.state == DISK_STATE_UNSUPPORTED_PARTITION_SCHEME ||
        table.scheme == PARTITION_SCHEME_UNSUPPORTED || table.hybridMbr ||
        table.extendedPartitionsPresent)
        return DELETE_PARTITION_UNSUPPORTED_SCHEME;
    if (scheme != PARTITION_SCHEME_GPT && scheme != PARTITION_SCHEME_MBR)
        return DELETE_PARTITION_UNSUPPORTED_SCHEME;
    if (table.scheme != scheme ||
        table.state != (scheme == PARTITION_SCHEME_GPT
            ? DISK_STATE_VALID_GPT : DISK_STATE_VALID_MBR))
        return DELETE_PARTITION_UNSUPPORTED_SCHEME;
    if (scheme == PARTITION_SCHEME_GPT &&
        (!table.primaryGptValid || !table.backupGptValid ||
         !table.gptCopiesAgree || table.primaryGptEntryArrayLba == 0 ||
         table.backupGptEntryArrayLba == 0 || table.gptEntrySize < 128 ||
         table.gptEntrySize > GPT_MAX_ENTRY_SIZE ||
         (table.gptEntrySize & 7u) != 0 || table.tableEntryCount == 0 ||
         table.tableEntryCount > MAX_PARSED_PARTITIONS))
        return DELETE_PARTITION_INVALID_TABLE;
    if (!find_delete_entry(table, selected))
        return DELETE_PARTITION_STALE_SELECTION;

    const MountProtection mount = query_partition_mount_protection(
        target, scheme, selected);
    if (mount.safety == DEVICE_ROOT_BACKING)
        return result.status = DELETE_PARTITION_ROOT_BACKING;
    if (mount.safety == DEVICE_MOUNTED)
        return DELETE_PARTITION_MOUNTED;
    if (mount.safety != DEVICE_UNMOUNTED)
        return DELETE_PARTITION_MOUNT_STATE_UNKNOWN;

    const BootProtection boot = query_partition_boot_protection(
        target, scheme, selected, table);
    if (boot.safety == BOOT_DEVICE_IS_TARGET)
        return DELETE_PARTITION_BOOT_BACKING;
    if (boot.safety != BOOT_DEVICE_DEFINITELY_NOT_TARGET)
        return DELETE_PARTITION_BOOT_IDENTITY_UNKNOWN;
    if (revalidate_target_identity(target) != TARGET_VALID)
        return delete_map_identity(revalidate_target_identity(target));
    result.finalDetectedState = table.state;
    result.finalPartitionCount = table.partitionCount;
    return DELETE_PARTITION_READY_FOR_CONFIRMATION;
}

static bool same_delete_plan(const DeletePartitionPlan& left,
                             const DeletePartitionPlan& right)
{
    return left.lease.ownerToken == right.lease.ownerToken &&
        left.lease.targetPinned == right.lease.targetPinned &&
        left.lease.pinnedIndex == right.lease.pinnedIndex &&
        left.lease.pinnedRegistrationId == right.lease.pinnedRegistrationId &&
        target_identities_equal(left.targetSnapshot, right.targetSnapshot) &&
        left.partitionScheme == right.partitionScheme &&
        delete_entry_equal(left.partitionSnapshot, right.partitionSnapshot) &&
        left.expectedRegistryGeneration == right.expectedRegistryGeneration &&
        left.parserState == right.parserState &&
        left.partitionCount == right.partitionCount &&
        left.tableEntryCount == right.tableEntryCount &&
        left.gptEntrySize == right.gptEntrySize &&
        left.gptEntryArraySectors == right.gptEntryArraySectors &&
        left.primaryGptEntryArrayLba == right.primaryGptEntryArrayLba &&
        left.backupGptEntryArrayLba == right.backupGptEntryArrayLba &&
        left.firstUsableLba == right.firstUsableLba &&
        left.lastUsableLba == right.lastUsableLba &&
        left.primaryGptHeaderCrc32 == right.primaryGptHeaderCrc32 &&
        left.backupGptHeaderCrc32 == right.backupGptHeaderCrc32 &&
        left.primaryGptEntryArrayCrc32 == right.primaryGptEntryArrayCrc32 &&
        left.backupGptEntryArrayCrc32 == right.backupGptEntryArrayCrc32 &&
        left.mbrTableCrc32 == right.mbrTableCrc32 &&
        left.mbrDiskSignature == right.mbrDiskSignature &&
        guid_equal(left.primaryDiskGuid, right.primaryDiskGuid) &&
        guid_equal(left.backupDiskGuid, right.backupDiskGuid) &&
        left.confirmationReady == right.confirmationReady;
}

static bool capture_delete_snapshot(const TargetIdentity& target,
                                    const PartitionTableModel& table,
                                    uint32_t sectorSize,
                                    DeletePartitionResult& result)
{
    clear_bytes(&s_deleteSnapshot, sizeof(s_deleteSnapshot));
    s_deleteSnapshot.isGpt = table.scheme == PARTITION_SCHEME_GPT;
    s_deleteSnapshot.sectorSize = sectorSize;
    if (!s_deleteSnapshot.isGpt) {
        if (!read_region(target.globalIndex, 0, 1, sectorSize,
                         s_deleteSnapshot.mbr)) return false;
        ++result.logicalSectorsRead;
        s_deleteSnapshot.valid = true;
        return true;
    }

    const uint64_t bytes64 = static_cast<uint64_t>(table.tableEntryCount) *
        table.gptEntrySize;
    const uint64_t padded64 = static_cast<uint64_t>(table.gptEntryArraySectors) *
        sectorSize;
    if (bytes64 == 0 || bytes64 > GPT_MAX_ARRAY_BYTES ||
        padded64 > GPT_MAX_ARRAY_BYTES || bytes64 > padded64 ||
        !table.partitionCount) return false;
    s_deleteSnapshot.arrayBytes = static_cast<uint32_t>(bytes64);
    s_deleteSnapshot.arraySectors = table.gptEntryArraySectors;
    s_deleteSnapshot.entrySize = table.gptEntrySize;
    s_deleteSnapshot.entryCount = table.tableEntryCount;
    s_deleteSnapshot.primaryHeaderLba = 1;
    s_deleteSnapshot.backupHeaderLba = target.totalLogicalSectors - 1;
    if (!read_region(target.globalIndex, 1, 1, sectorSize,
                     s_deleteSnapshot.primaryHeader) ||
        !read_region(target.globalIndex, s_deleteSnapshot.backupHeaderLba,
                     1, sectorSize, s_deleteSnapshot.backupHeader) ||
        !read_region(target.globalIndex, table.primaryGptEntryArrayLba,
                     table.gptEntryArraySectors, sectorSize,
                     s_deletePrimaryArray) ||
        !read_region(target.globalIndex, table.backupGptEntryArrayLba,
                     table.gptEntryArraySectors, sectorSize,
                     s_deleteBackupArray)) return false;
    result.logicalSectorsRead += 2 + 2 * table.gptEntryArraySectors;
    if (!bytes_equal(s_deletePrimaryArray, s_deleteBackupArray,
                     s_deleteSnapshot.arrayBytes)) return false;

    const PartitionEntry* selected = find_delete_entry(table,
                                                        s_deletePreparedPlan.partitionSnapshot);
    if (!selected || selected->partitionNumber == 0) return false;
    const uint64_t entryStart =
        static_cast<uint64_t>(selected->partitionNumber - 1) * table.gptEntrySize;
    const uint64_t entryEnd = entryStart + table.gptEntrySize - 1;
    const uint64_t firstSector = entryStart / sectorSize;
    const uint64_t lastSector = entryEnd / sectorSize;
    if (lastSector < firstSector || lastSector - firstSector + 1 >
            DELETE_MAX_ENTRY_SECTORS || lastSector >= table.gptEntryArraySectors)
        return false;
    s_deleteSnapshot.slot = static_cast<uint16_t>(selected->partitionNumber - 1);
    s_deleteSnapshot.touchedSectorCount =
        static_cast<uint8_t>(lastSector - firstSector + 1);
    for (uint8_t i = 0; i < s_deleteSnapshot.touchedSectorCount; ++i) {
        s_deleteSnapshot.primaryArrayLba[i] =
            table.primaryGptEntryArrayLba + firstSector + i;
        s_deleteSnapshot.backupArrayLba[i] =
            table.backupGptEntryArrayLba + firstSector + i;
        copy_bytes(s_deleteSnapshot.primarySectors[i],
            s_deletePrimaryArray + static_cast<size_t>(firstSector + i) * sectorSize,
            sectorSize);
        copy_bytes(s_deleteSnapshot.backupSectors[i],
            s_deleteBackupArray + static_cast<size_t>(firstSector + i) * sectorSize,
            sectorSize);
    }
    s_deleteSnapshot.valid = true;
    return true;
}

static void prepare_delete_gpt(const PartitionTableModel& table,
                               uint32_t sectorSize)
{
    const size_t padded = static_cast<size_t>(table.gptEntryArraySectors) *
        sectorSize;
    copy_bytes(s_deleteNewPrimaryArray, s_deletePrimaryArray, padded);
    copy_bytes(s_deleteNewBackupArray, s_deleteBackupArray, padded);
    uint8_t* primaryEntry = s_deleteNewPrimaryArray +
        static_cast<size_t>(s_deleteSnapshot.slot) * s_deleteSnapshot.entrySize;
    uint8_t* backupEntry = s_deleteNewBackupArray +
        static_cast<size_t>(s_deleteSnapshot.slot) * s_deleteSnapshot.entrySize;
    clear_bytes(primaryEntry, s_deleteSnapshot.entrySize);
    clear_bytes(backupEntry, s_deleteSnapshot.entrySize);

    copy_bytes(s_deleteNewPrimaryHeader, s_deleteSnapshot.primaryHeader,
               sectorSize);
    copy_bytes(s_deleteNewBackupHeader, s_deleteSnapshot.backupHeader,
               sectorSize);
    const uint32_t primarySize = read_u32(s_deleteNewPrimaryHeader + 12);
    const uint32_t backupSize = read_u32(s_deleteNewBackupHeader + 12);
    if (primarySize < DELETE_GPT_MIN_HEADER_SIZE || primarySize > sectorSize ||
        backupSize < DELETE_GPT_MIN_HEADER_SIZE || backupSize > sectorSize) {
        s_deleteSnapshot.valid = false;
        return;
    }
    const uint32_t arrayCrc = crc32(s_deleteNewPrimaryArray,
                                    s_deleteSnapshot.arrayBytes);
    write_u32(s_deleteNewPrimaryHeader + DELETE_GPT_ARRAY_CRC_OFFSET, arrayCrc);
    write_u32(s_deleteNewBackupHeader + DELETE_GPT_ARRAY_CRC_OFFSET, arrayCrc);
    write_u32(s_deleteNewPrimaryHeader + DELETE_GPT_HEADER_CRC_OFFSET, 0);
    write_u32(s_deleteNewBackupHeader + DELETE_GPT_HEADER_CRC_OFFSET, 0);
    write_u32(s_deleteNewPrimaryHeader + DELETE_GPT_HEADER_CRC_OFFSET,
              crc32(s_deleteNewPrimaryHeader, primarySize));
    write_u32(s_deleteNewBackupHeader + DELETE_GPT_HEADER_CRC_OFFSET,
              crc32(s_deleteNewBackupHeader, backupSize));
}

static void record_delete_write(DeletePartitionResult& result, uint64_t lba)
{
    if (result.writeRangeCount < DELETE_PARTITION_MAX_WRITE_RANGES) {
        DeletePartitionWriteRange& range =
            result.writeRanges[result.writeRangeCount++];
        range.startLba = lba;
        range.sectorCount = 1;
    }
    if (result.logicalSectorsWritten != UINT32_MAX)
        ++result.logicalSectorsWritten;
}

static bool delete_write_sector(const TargetIdentity& target, uint64_t lba,
                                const uint8_t* bytes,
                                DeletePartitionResult& result,
                                bool rollback)
{
    if (revalidate_target_identity(target) != TARGET_VALID) return false;
    copy_bytes(s_ioSector, bytes, s_deleteSnapshot.sectorSize);
    result.writeMayHaveReachedMedia = true;
    record_delete_write(result, lba);
    const block::Status status = write_sectors_safe(target.globalIndex, lba,
        1, s_ioSector, s_deleteSnapshot.sectorSize);
    if (status != block::BLOCK_OK) {
        if (!rollback) {
            result.failedOperation = block::OPERATION_WRITE;
            if (block::last_operation_diagnostic(result.failedBlockDiagnostic)) {
                result.blockStatusValid = true;
                result.blockStatus = result.failedBlockDiagnostic.status;
            }
        } else {
            result.rollbackWriteStatus = status;
        }
        return false;
    }
    if (rollback) {
        result.rollbackWriteAttempted = true;
        result.rollbackWriteStatus = block::BLOCK_OK;
    }
    return true;
}

static bool delete_flush(uint8_t device, DeletePartitionResult& result,
                         bool rollback)
{
    const block::FlushReport report = block::flush_with_result(device);
    if (rollback) {
        result.rollbackFlushAttempted = true;
        result.rollbackFlushOutcome = report.outcome;
        result.rollbackFlushStatus = report.status;
    } else {
        result.flushAttempted = true;
        ++result.flushAttempts;
        result.flushOutcome = report.outcome;
        result.flushStatus = report.status;
    }
    return trusted_flush(report);
}

static bool delete_read_sector(const TargetIdentity& target, uint64_t lba,
                               uint8_t* bytes,
                               DeletePartitionResult& result)
{
    if (read_logical_sector(target.globalIndex, lba, bytes,
            s_deleteSnapshot.sectorSize) != block::BLOCK_OK) return false;
    if (result.logicalSectorsRead != UINT32_MAX) ++result.logicalSectorsRead;
    return true;
}

static bool verify_delete_gpt_copy(const TargetIdentity& target,
                                   bool backup,
                                   DeletePartitionResult& result)
{
    const uint32_t sectorSize = s_deleteSnapshot.sectorSize;
    const uint64_t arrayLba = backup
        ? s_deletePreparedPlan.backupGptEntryArrayLba
        : s_deletePreparedPlan.primaryGptEntryArrayLba;
    const uint8_t* expectedArray = backup ? s_deleteNewBackupArray
                                           : s_deleteNewPrimaryArray;
    const uint8_t* expectedHeader = backup ? s_deleteNewBackupHeader
                                            : s_deleteNewPrimaryHeader;
    uint8_t* actualArray = backup ? s_deleteBackupArray : s_deletePrimaryArray;
    uint8_t* actualHeader = s_ioSector;
    if (!read_region(target.globalIndex, arrayLba,
            s_deleteSnapshot.arraySectors, sectorSize, actualArray) ||
        !delete_read_sector(target, backup ? s_deleteSnapshot.backupHeaderLba
                                           : s_deleteSnapshot.primaryHeaderLba,
                           actualHeader, result)) return false;
    result.logicalSectorsRead += s_deleteSnapshot.arraySectors;
    const size_t padded = static_cast<size_t>(s_deleteSnapshot.arraySectors) *
        sectorSize;
    return bytes_equal(actualArray, expectedArray, padded) &&
        bytes_equal(actualHeader, expectedHeader, sectorSize);
}

static bool old_delete_entries_unchanged(const PartitionTableModel& before,
                                         const PartitionTableModel& after,
                                         const PartitionEntry& deleted)
{
    bool foundDeleted = false;
    for (uint16_t i = 0; i < before.partitionCount; ++i) {
        const PartitionEntry& original = before.partitions[i];
        if (delete_entry_equal(original, deleted)) {
            foundDeleted = true;
            continue;
        }
        bool found = false;
        for (uint16_t j = 0; j < after.partitionCount; ++j) {
            if (after.partitions[j].partitionNumber != original.partitionNumber)
                continue;
            found = entry_matches(original, after.partitions[j]);
            break;
        }
        if (!found) return false;
    }
    return foundDeleted;
}

static bool delete_partition_absent(const PartitionTableModel& table,
                                    const PartitionEntry& deleted)
{
    for (uint16_t i = 0; i < table.partitionCount; ++i)
        if (delete_entry_equal(table.partitions[i], deleted)) return false;
    return true;
}

static bool verify_delete_gpt_all(const TargetIdentity& target,
                                  const PartitionTableModel& before,
                                  DeletePartitionResult& result)
{
    if (!verify_delete_gpt_copy(target, false, result) ||
        !verify_delete_gpt_copy(target, true, result)) return false;
    if (!parse_partition_table(target.globalIndex, s_deleteVerifiedTable) ||
        s_deleteVerifiedTable.state != DISK_STATE_VALID_GPT ||
        !s_deleteVerifiedTable.primaryGptValid ||
        !s_deleteVerifiedTable.backupGptValid ||
        !s_deleteVerifiedTable.gptCopiesAgree ||
        s_deleteVerifiedTable.partitionCount + 1 != before.partitionCount ||
        s_deleteVerifiedTable.tableEntryCount != before.tableEntryCount ||
        s_deleteVerifiedTable.gptEntrySize != before.gptEntrySize ||
        s_deleteVerifiedTable.gptEntryArraySectors != before.gptEntryArraySectors ||
        s_deleteVerifiedTable.primaryGptEntryArrayLba !=
            before.primaryGptEntryArrayLba ||
        s_deleteVerifiedTable.backupGptEntryArrayLba !=
            before.backupGptEntryArrayLba ||
        s_deleteVerifiedTable.firstUsableLba != before.firstUsableLba ||
        s_deleteVerifiedTable.lastUsableLba != before.lastUsableLba ||
        !guid_equal(s_deleteVerifiedTable.primaryDiskGuid,
                    before.primaryDiskGuid) ||
        !guid_equal(s_deleteVerifiedTable.backupDiskGuid,
                    before.backupDiskGuid) ||
        !old_delete_entries_unchanged(before, s_deleteVerifiedTable,
                                      s_deletePreparedPlan.partitionSnapshot) ||
        !delete_partition_absent(s_deleteVerifiedTable,
                                 s_deletePreparedPlan.partitionSnapshot))
        return false;
    return true;
}

static bool verify_delete_mbr(const TargetIdentity& target,
                              const PartitionTableModel& before,
                              DeletePartitionResult& result)
{
    if (!delete_read_sector(target, 0, s_ioSector, result)) return false;
    const uint32_t entryOffset = DELETE_MBR_TABLE_OFFSET +
        static_cast<uint32_t>(s_deletePreparedPlan.partitionSnapshot.partitionNumber - 1) *
        DELETE_MBR_ENTRY_BYTES;
    for (uint32_t i = 0; i < s_deleteSnapshot.sectorSize; ++i) {
        if (i >= entryOffset && i < entryOffset + DELETE_MBR_ENTRY_BYTES) {
            if (s_ioSector[i] != 0) return false;
        } else if (s_ioSector[i] != s_deleteSnapshot.mbr[i]) {
            return false;
        }
    }
    if (s_ioSector[510] != 0x55 || s_ioSector[511] != 0xAA ||
        !parse_partition_table(target.globalIndex, s_deleteVerifiedTable) ||
        s_deleteVerifiedTable.state != DISK_STATE_VALID_MBR ||
        s_deleteVerifiedTable.mbrDiskSignature != before.mbrDiskSignature ||
        s_deleteVerifiedTable.partitionCount + 1 != before.partitionCount ||
        !old_delete_entries_unchanged(before, s_deleteVerifiedTable,
                                      s_deletePreparedPlan.partitionSnapshot) ||
        !delete_partition_absent(s_deleteVerifiedTable,
                                 s_deletePreparedPlan.partitionSnapshot))
        return false;
    return true;
}

static bool verify_delete_snapshot(const TargetIdentity& target,
                                   DeletePartitionResult& result)
{
    if (!s_deleteSnapshot.valid ||
        revalidate_target_identity(target) != TARGET_VALID) return false;
    if (!s_deleteSnapshot.isGpt) {
        if (!delete_read_sector(target, 0, s_ioSector, result)) return false;
        if (!bytes_equal(s_ioSector, s_deleteSnapshot.mbr,
                         s_deleteSnapshot.sectorSize)) return false;
    } else {
        if (!delete_read_sector(target, s_deleteSnapshot.primaryHeaderLba,
                s_ioSector, result) ||
            !bytes_equal(s_ioSector, s_deleteSnapshot.primaryHeader,
                         s_deleteSnapshot.sectorSize)) return false;
        if (!delete_read_sector(target, s_deleteSnapshot.backupHeaderLba,
                s_ioSector, result) ||
            !bytes_equal(s_ioSector, s_deleteSnapshot.backupHeader,
                         s_deleteSnapshot.sectorSize)) return false;
        for (uint8_t i = 0; i < s_deleteSnapshot.touchedSectorCount; ++i) {
            if (!delete_read_sector(target, s_deleteSnapshot.primaryArrayLba[i],
                    s_ioSector, result) ||
                !bytes_equal(s_ioSector, s_deleteSnapshot.primarySectors[i],
                             s_deleteSnapshot.sectorSize)) return false;
            if (!delete_read_sector(target, s_deleteSnapshot.backupArrayLba[i],
                    s_ioSector, result) ||
                !bytes_equal(s_ioSector, s_deleteSnapshot.backupSectors[i],
                             s_deleteSnapshot.sectorSize)) return false;
        }
    }
    return true;
}

static bool rollback_delete(const TargetIdentity& target,
                            const PartitionTableModel& before,
                            DeletePartitionResult& result)
{
    if (!result.writeMayHaveReachedMedia || !s_deleteSnapshot.valid ||
        revalidate_target_identity(target) != TARGET_VALID) return false;
    result.rollbackAttempted = true;
    bool restored = true;
    if (!s_deleteSnapshot.isGpt) {
        result.stage = DELETE_PARTITION_STAGE_ROLLBACK_WRITE;
        restored = delete_write_sector(target, 0, s_deleteSnapshot.mbr,
                                       result, true);
    } else {
        result.stage = DELETE_PARTITION_STAGE_ROLLBACK_WRITE;
        // Restore the backup copy first so a still-valid primary copy remains
        // available while its peer is returned to the original generation.
        for (uint8_t i = 0; i < s_deleteSnapshot.touchedSectorCount; ++i)
            restored = delete_write_sector(target,
                s_deleteSnapshot.backupArrayLba[i],
                s_deleteSnapshot.backupSectors[i], result, true) && restored;
        restored = delete_write_sector(target, s_deleteSnapshot.backupHeaderLba,
            s_deleteSnapshot.backupHeader, result, true) && restored;
        for (uint8_t i = 0; i < s_deleteSnapshot.touchedSectorCount; ++i)
            restored = delete_write_sector(target,
                s_deleteSnapshot.primaryArrayLba[i],
                s_deleteSnapshot.primarySectors[i], result, true) && restored;
        restored = delete_write_sector(target, s_deleteSnapshot.primaryHeaderLba,
            s_deleteSnapshot.primaryHeader, result, true) && restored;
    }
    if (!restored || revalidate_target_identity(target) != TARGET_VALID)
        return false;
    result.stage = DELETE_PARTITION_STAGE_ROLLBACK_FLUSH;
    if (!delete_flush(target.globalIndex, result, true)) return false;
    result.stage = DELETE_PARTITION_STAGE_ROLLBACK_VERIFY;
    if (!verify_delete_snapshot(target, result) ||
        !parse_partition_table(target.globalIndex, s_deleteVerifiedTable) ||
        s_deleteVerifiedTable.state != before.state ||
        s_deleteVerifiedTable.partitionCount != before.partitionCount ||
        !old_entries_unchanged(before, s_deleteVerifiedTable)) return false;
    result.rollbackVerificationPassed = true;
    result.finalDetectedState = s_deleteVerifiedTable.state;
    result.finalPartitionCount = s_deleteVerifiedTable.partitionCount;
    result.rollbackSucceeded = true;
    return true;
}

static void fail_delete(StorageOperationLease& lease,
                        const TargetIdentity& target,
                        const PartitionTableModel& before,
                        DeletePartitionResult& result,
                        DeletePartitionStatus status)
{
    result.lastStage = result.stage;
    result.firstFailedStage = result.firstFailedStage == DELETE_PARTITION_STAGE_IDLE
        ? result.stage : result.firstFailedStage;
    result.failureStatus = status;
    result.status = status;
    result.stage = DELETE_PARTITION_STAGE_FAILED;
    result.failedBeforeWrite = !result.writeMayHaveReachedMedia;
    set_delete_diagnostic(result, delete_partition_status_name(status));
    if (result.writeMayHaveReachedMedia) {
        result.rollbackSucceeded = rollback_delete(target, before, result);
        if (!result.rollbackSucceeded) {
            result.finalStateUncertain = true;
            result.status = DELETE_PARTITION_ROLLBACK_FAILED;
            result.stage = DELETE_PARTITION_STAGE_STATE_UNCERTAIN;
            set_delete_diagnostic(result,
                "Deletion failed and metadata rollback could not be verified; inspect the disk before use.");
        }
    }
    if (revalidate_target_identity(target) != TARGET_VALID &&
        result.writeMayHaveReachedMedia) {
        result.finalStateUncertain = true;
        result.status = DELETE_PARTITION_ROLLBACK_FAILED;
        result.stage = DELETE_PARTITION_STAGE_STATE_UNCERTAIN;
        set_delete_diagnostic(result,
            "Disk identity changed during deletion; state is uncertain and replacement media was not written.");
    }
    complete_storage_operation_execution(lease);
    clear_bytes(&s_deleteSnapshot, sizeof(s_deleteSnapshot));
    s_deletePlanValid = false;
    clear_bytes(&s_deletePreparedPlan, sizeof(s_deletePreparedPlan));
}

static bool delete_plan_snapshot_current(const DeletePartitionPlan& plan,
                                         const PartitionTableModel& table,
                                         DeletePartitionResult& result)
{
    uint8_t* mbr = nullptr;
    if (plan.partitionScheme == PARTITION_SCHEME_MBR) {
        if (read_logical_sector(plan.targetSnapshot.globalIndex, 0, s_deleteMbr,
                plan.targetSnapshot.logicalSectorSize) != block::BLOCK_OK)
            return false;
        if (result.logicalSectorsRead != UINT32_MAX) ++result.logicalSectorsRead;
        mbr = s_deleteMbr;
    }
    return delete_fingerprint_matches(plan, table, mbr);
}

static bool delete_snapshot_matches_media(const TargetIdentity& target,
                                          DeletePartitionResult& result)
{
    if (!s_deleteSnapshot.valid || revalidate_target_identity(target) != TARGET_VALID)
        return false;
    if (!s_deleteSnapshot.isGpt) {
        if (!delete_read_sector(target, 0, s_ioSector, result)) return false;
        return bytes_equal(s_ioSector, s_deleteSnapshot.mbr,
                           s_deleteSnapshot.sectorSize);
    }
    if (!delete_read_sector(target, s_deleteSnapshot.primaryHeaderLba,
            s_ioSector, result) ||
        !bytes_equal(s_ioSector, s_deleteSnapshot.primaryHeader,
                     s_deleteSnapshot.sectorSize) ||
        !delete_read_sector(target, s_deleteSnapshot.backupHeaderLba,
            s_ioSector, result) ||
        !bytes_equal(s_ioSector, s_deleteSnapshot.backupHeader,
                     s_deleteSnapshot.sectorSize)) return false;
    for (uint32_t i = 0; i < s_deleteSnapshot.arraySectors; ++i) {
        if (!delete_read_sector(target,
                s_deletePreparedPlan.primaryGptEntryArrayLba + i,
                s_ioSector, result) ||
            !bytes_equal(s_ioSector,
                s_deletePrimaryArray + static_cast<size_t>(i) *
                    s_deleteSnapshot.sectorSize,
                s_deleteSnapshot.sectorSize)) return false;
        if (!delete_read_sector(target,
                s_deletePreparedPlan.backupGptEntryArrayLba + i,
                s_ioSector, result) ||
            !bytes_equal(s_ioSector,
                s_deleteBackupArray + static_cast<size_t>(i) *
                    s_deleteSnapshot.sectorSize,
                s_deleteSnapshot.sectorSize)) return false;
    }
    return true;
}

} // namespace

DeletePartitionStatus probe_delete_partition(
    const TargetIdentity& target, PartitionScheme scheme,
    const PartitionEntry& partition, DeletePartitionResult& result)
{
    reset_delete_result(result);
    result.targetIdentity = target;
    result.partitionScheme = scheme;
    result.deletedPartition = partition;
    StorageOperationLease lease = {};
    const StorageOperationLockStatus lock = try_acquire_storage_operation(lease);
    if (lock != STORAGE_OPERATION_LOCK_ACQUIRED) {
        result.status = lock == STORAGE_OPERATION_LOCK_BUSY
            ? DELETE_PARTITION_OPERATION_BUSY
            : DELETE_PARTITION_OPERATION_OWNERSHIP_INVALID;
        return result.status;
    }
    if (!pin_storage_operation_target(lease, target)) {
        result.status = delete_map_identity(revalidate_target_identity(target));
        if (result.status == DELETE_PARTITION_SUCCESS)
            result.status = DELETE_PARTITION_IDENTITY_CHANGED;
        release_storage_operation(lease);
        return result.status;
    }
    const DeletePartitionStatus status = validate_delete_target(target, scheme,
        partition, lease, s_deleteTable, result);
    result.status = status;
    release_storage_operation(lease);
    return status;
}

DeletePartitionStatus prepare_delete_partition(
    const DeletePartitionRequest& request, DeletePartitionPlan& plan,
    DeletePartitionResult& result)
{
    reset_delete_result(result);
    clear_bytes(&plan, sizeof(plan));
    result.targetIdentity = request.targetSnapshot;
    result.partitionScheme = request.partitionScheme;
    result.deletedPartition = request.partitionSnapshot;
    if (request.targetSnapshot.registrationId == 0 ||
        request.expectedRegistryGeneration !=
            request.targetSnapshot.registryGeneration ||
        request.partitionSnapshot.partitionNumber == 0 ||
        request.partitionSnapshot.sectorCount == 0 ||
        (request.partitionScheme != PARTITION_SCHEME_GPT &&
         request.partitionScheme != PARTITION_SCHEME_MBR)) {
        result.status = DELETE_PARTITION_INVALID_REQUEST;
        return result.status;
    }
    const StorageOperationLockStatus lock = try_acquire_storage_operation(plan.lease);
    if (lock != STORAGE_OPERATION_LOCK_ACQUIRED) {
        result.status = lock == STORAGE_OPERATION_LOCK_BUSY
            ? DELETE_PARTITION_OPERATION_BUSY
            : DELETE_PARTITION_OPERATION_OWNERSHIP_INVALID;
        return result.status;
    }
    result.stage = DELETE_PARTITION_STAGE_PIN_TARGET;
    if (!pin_storage_operation_target(plan.lease, request.targetSnapshot)) {
        result.status = delete_map_identity(
            revalidate_target_identity(request.targetSnapshot));
        if (result.status == DELETE_PARTITION_SUCCESS)
            result.status = DELETE_PARTITION_IDENTITY_CHANGED;
        release_storage_operation(plan.lease);
        return result.status;
    }
    result.stage = DELETE_PARTITION_STAGE_PREFLIGHT;
    DeletePartitionStatus status = validate_delete_target(
        request.targetSnapshot, request.partitionScheme,
        request.partitionSnapshot, plan.lease, s_deleteTable, result);
    if (status != DELETE_PARTITION_READY_FOR_CONFIRMATION) {
        result.status = status;
        release_storage_operation(plan.lease);
        return status;
    }
    plan.targetSnapshot = request.targetSnapshot;
    plan.partitionScheme = request.partitionScheme;
    plan.partitionSnapshot = *find_delete_entry(s_deleteTable,
                                                request.partitionSnapshot);
    plan.expectedRegistryGeneration = request.expectedRegistryGeneration;
    result.stage = DELETE_PARTITION_STAGE_SNAPSHOT;
    if (request.partitionScheme == PARTITION_SCHEME_MBR) {
        DeviceCapabilities caps;
        if (!query_device_capabilities(request.targetSnapshot.globalIndex, caps) ||
            read_logical_sector(request.targetSnapshot.globalIndex, 0, s_deleteMbr,
                caps.logicalSectorSize) != block::BLOCK_OK) {
            result.status = DELETE_PARTITION_READ_UNAVAILABLE;
            release_storage_operation(plan.lease);
            return result.status;
        }
        if (result.logicalSectorsRead != UINT32_MAX) ++result.logicalSectorsRead;
    }
    capture_delete_fingerprint(s_deleteTable, plan,
        request.partitionScheme == PARTITION_SCHEME_MBR ? s_deleteMbr : nullptr);
    plan.confirmationReady = true;
    result.stage = DELETE_PARTITION_STAGE_WAITING_FOR_CONFIRMATION;
    result.status = DELETE_PARTITION_READY_FOR_CONFIRMATION;
    set_delete_diagnostic(result,
        "Exact partition identity is current; explicit confirmation is required.");
    s_deletePreparedPlan = plan;
    s_deletePlanValid = true;
    return result.status;
}

bool cancel_delete_partition(DeletePartitionPlan& plan)
{
    if (!s_deletePlanValid || !same_delete_plan(plan, s_deletePreparedPlan))
        return false;
    const bool released = release_storage_operation(plan.lease);
    if (released) {
        clear_bytes(&s_deletePreparedPlan, sizeof(s_deletePreparedPlan));
        s_deletePlanValid = false;
    }
    clear_bytes(&plan, sizeof(plan));
    return released;
}

DeletePartitionStatus execute_delete_partition(DeletePartitionPlan& plan,
                                                DeletePartitionResult& result)
{
    reset_delete_result(result);
    result.targetIdentity = plan.targetSnapshot;
    result.partitionScheme = plan.partitionScheme;
    result.deletedPartition = plan.partitionSnapshot;
    if (!plan.confirmationReady || !s_deletePlanValid ||
        !same_delete_plan(plan, s_deletePreparedPlan) ||
        !storage_operation_lease_is_current(plan.lease)) {
        result.status = DELETE_PARTITION_OPERATION_OWNERSHIP_INVALID;
        result.failureStatus = result.status;
        return result.status;
    }
    if (!begin_storage_operation_execution(plan.lease)) {
        result.status = DELETE_PARTITION_OPERATION_OWNERSHIP_INVALID;
        result.failureStatus = result.status;
        return result.status;
    }
    result.stage = DELETE_PARTITION_STAGE_REVALIDATING;
    DeletePartitionStatus status = validate_delete_target(plan.targetSnapshot,
        plan.partitionScheme, plan.partitionSnapshot, plan.lease,
        s_deleteTable, result);
    if (status != DELETE_PARTITION_READY_FOR_CONFIRMATION) {
        result.stage = DELETE_PARTITION_STAGE_FAILED;
        result.status = status;
        result.failureStatus = status;
        set_delete_diagnostic(result, delete_partition_status_name(status));
        complete_storage_operation_execution(plan.lease);
        s_deletePlanValid = false;
        clear_bytes(&s_deletePreparedPlan, sizeof(s_deletePreparedPlan));
        clear_bytes(&plan, sizeof(plan));
        return result.status;
    }
    DeviceCapabilities caps;
    if (!query_device_capabilities(plan.targetSnapshot.globalIndex, caps) ||
        revalidate_target_identity(plan.targetSnapshot) != TARGET_VALID) {
        fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable, result,
            DELETE_PARTITION_IDENTITY_CHANGED);
        clear_bytes(&plan, sizeof(plan));
        return result.status;
    }
    result.stage = DELETE_PARTITION_STAGE_REVALIDATING;
    if (!delete_plan_snapshot_current(plan, s_deleteTable, result)) {
        fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable, result,
            DELETE_PARTITION_STALE_SELECTION);
        clear_bytes(&plan, sizeof(plan));
        return result.status;
    }
    if (plan.partitionScheme == PARTITION_SCHEME_MBR &&
        !delete_fingerprint_matches(plan, s_deleteTable, s_deleteMbr)) {
        fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable, result,
            DELETE_PARTITION_STALE_SELECTION);
        clear_bytes(&plan, sizeof(plan));
        return result.status;
    }
    s_deletePreparedPlan = plan;
    s_originalTable = s_deleteTable;
    result.stage = DELETE_PARTITION_STAGE_SNAPSHOT;
    if (!capture_delete_snapshot(plan.targetSnapshot, s_deleteTable,
            caps.logicalSectorSize, result)) {
        fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable, result,
            DELETE_PARTITION_READ_UNAVAILABLE);
        clear_bytes(&plan, sizeof(plan));
        return result.status;
    }
    if (plan.partitionScheme == PARTITION_SCHEME_GPT) {
        result.stage = DELETE_PARTITION_STAGE_PREPARE_METADATA;
        prepare_delete_gpt(s_deleteTable, caps.logicalSectorSize);
        if (!s_deleteSnapshot.valid) {
            fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable, result,
                DELETE_PARTITION_INVALID_TABLE);
            clear_bytes(&plan, sizeof(plan));
            return result.status;
        }
    } else {
        const uint32_t entryOffset = DELETE_MBR_TABLE_OFFSET +
            static_cast<uint32_t>(plan.partitionSnapshot.partitionNumber - 1) *
            DELETE_MBR_ENTRY_BYTES;
        copy_bytes(s_deleteNewMbr, s_deleteSnapshot.mbr, caps.logicalSectorSize);
        clear_bytes(s_deleteNewMbr + entryOffset, DELETE_MBR_ENTRY_BYTES);
    }

    result.stage = DELETE_PARTITION_STAGE_FLUSH;
    if (!delete_flush(plan.targetSnapshot.globalIndex, result, false)) {
        fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable, result,
            DELETE_PARTITION_FLUSH_FAILED);
        clear_bytes(&plan, sizeof(plan));
        return result.status;
    }
    status = validate_delete_target(plan.targetSnapshot, plan.partitionScheme,
        plan.partitionSnapshot, plan.lease, s_deleteTable, result);
    if (status != DELETE_PARTITION_READY_FOR_CONFIRMATION ||
        !delete_plan_snapshot_current(plan, s_deleteTable, result) ||
        !delete_snapshot_matches_media(plan.targetSnapshot, result)) {
        if (status == DELETE_PARTITION_READY_FOR_CONFIRMATION)
            status = revalidate_target_identity(plan.targetSnapshot) == TARGET_VALID
                ? DELETE_PARTITION_STALE_SELECTION
                : delete_map_identity(revalidate_target_identity(plan.targetSnapshot));
        fail_delete(plan.lease, plan.targetSnapshot, s_originalTable, result, status);
        clear_bytes(&plan, sizeof(plan));
        return result.status;
    }

    if (plan.partitionScheme == PARTITION_SCHEME_GPT) {
        result.stage = DELETE_PARTITION_STAGE_WRITE_BACKUP_GPT;
        const uint32_t sectorSize = caps.logicalSectorSize;
        const uint64_t entryStart = static_cast<uint64_t>(
            plan.partitionSnapshot.partitionNumber - 1) * s_deleteSnapshot.entrySize;
        const uint64_t firstSector = entryStart / sectorSize;
        for (uint8_t i = 0; i < s_deleteSnapshot.touchedSectorCount; ++i) {
            if (!delete_write_sector(plan.targetSnapshot,
                    s_deleteSnapshot.backupArrayLba[i],
                    s_deleteNewBackupArray +
                        static_cast<size_t>(firstSector + i) * sectorSize,
                    result, false)) {
                fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable,
                    result, DELETE_PARTITION_IO_FAILED);
                clear_bytes(&plan, sizeof(plan));
                return result.status;
            }
        }
        result.writeStagesCompleted |= DELETE_PARTITION_WRITE_BACKUP_ARRAY;
        if (!delete_write_sector(plan.targetSnapshot,
                s_deleteSnapshot.backupHeaderLba, s_deleteNewBackupHeader,
                result, false) ||
            !delete_flush(plan.targetSnapshot.globalIndex, result, false)) {
            fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable, result,
                DELETE_PARTITION_FLUSH_FAILED);
            clear_bytes(&plan, sizeof(plan));
            return result.status;
        }
        result.writeStagesCompleted |= DELETE_PARTITION_WRITE_BACKUP_HEADER;
        result.stage = DELETE_PARTITION_STAGE_VERIFY_BACKUP_GPT;
        if (!verify_delete_gpt_copy(plan.targetSnapshot, true, result)) {
            fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable, result,
                DELETE_PARTITION_VERIFICATION_FAILED);
            clear_bytes(&plan, sizeof(plan));
            return result.status;
        }

        result.stage = DELETE_PARTITION_STAGE_WRITE_PRIMARY_GPT;
        for (uint8_t i = 0; i < s_deleteSnapshot.touchedSectorCount; ++i) {
            if (!delete_write_sector(plan.targetSnapshot,
                    s_deleteSnapshot.primaryArrayLba[i],
                    s_deleteNewPrimaryArray +
                        static_cast<size_t>(firstSector + i) * sectorSize,
                    result, false)) {
                fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable,
                    result, DELETE_PARTITION_IO_FAILED);
                clear_bytes(&plan, sizeof(plan));
                return result.status;
            }
        }
        result.writeStagesCompleted |= DELETE_PARTITION_WRITE_PRIMARY_ARRAY;
        if (!delete_write_sector(plan.targetSnapshot,
                s_deleteSnapshot.primaryHeaderLba, s_deleteNewPrimaryHeader,
                result, false) ||
            !delete_flush(plan.targetSnapshot.globalIndex, result, false)) {
            fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable, result,
                DELETE_PARTITION_FLUSH_FAILED);
            clear_bytes(&plan, sizeof(plan));
            return result.status;
        }
        result.writeStagesCompleted |= DELETE_PARTITION_WRITE_PRIMARY_HEADER;
        result.stage = DELETE_PARTITION_STAGE_VERIFY;
        if (!verify_delete_gpt_all(plan.targetSnapshot, s_deleteTable, result)) {
            fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable, result,
                DELETE_PARTITION_VERIFICATION_FAILED);
            clear_bytes(&plan, sizeof(plan));
            return result.status;
        }
    } else {
        result.stage = DELETE_PARTITION_STAGE_WRITE_MBR;
        if (!delete_write_sector(plan.targetSnapshot, 0, s_deleteNewMbr,
                                 result, false)) {
            fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable, result,
                DELETE_PARTITION_IO_FAILED);
            clear_bytes(&plan, sizeof(plan));
            return result.status;
        }
        result.writeStagesCompleted |= DELETE_PARTITION_WRITE_MBR_ENTRY;
        result.stage = DELETE_PARTITION_STAGE_FLUSH;
        if (!delete_flush(plan.targetSnapshot.globalIndex, result, false)) {
            fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable, result,
                DELETE_PARTITION_FLUSH_FAILED);
            clear_bytes(&plan, sizeof(plan));
            return result.status;
        }
        result.stage = DELETE_PARTITION_STAGE_VERIFY;
        if (!verify_delete_mbr(plan.targetSnapshot, s_deleteTable, result)) {
            fail_delete(plan.lease, plan.targetSnapshot, s_deleteTable, result,
                DELETE_PARTITION_VERIFICATION_FAILED);
            clear_bytes(&plan, sizeof(plan));
            return result.status;
        }
    }

    result.verificationPassed = true;
    result.stage = DELETE_PARTITION_STAGE_RESCAN;
    if (!parse_partition_table(plan.targetSnapshot.globalIndex,
            s_deleteTable) ||
        s_deleteTable.state != (plan.partitionScheme == PARTITION_SCHEME_GPT
            ? DISK_STATE_VALID_GPT : DISK_STATE_VALID_MBR) ||
        !compute_unallocated_regions(s_deleteTable, caps.totalLogicalSectors,
            caps.logicalSectorSize, s_regions, MAX_UNALLOCATED_REGIONS,
            result.finalUnallocatedRegionCount) ||
        revalidate_target_identity(plan.targetSnapshot) != TARGET_VALID) {
        fail_delete(plan.lease, plan.targetSnapshot, s_originalTable, result,
            DELETE_PARTITION_RESCAN_FAILED);
        clear_bytes(&plan, sizeof(plan));
        return result.status;
    }
    result.finalDetectedState = s_deleteTable.state;
    result.finalPartitionCount = s_deleteTable.partitionCount;
    result.status = DELETE_PARTITION_SUCCESS;
    result.failureStatus = DELETE_PARTITION_SUCCESS;
    result.lastStage = DELETE_PARTITION_STAGE_COMPLETED;
    result.stage = DELETE_PARTITION_STAGE_COMPLETED;
    result.failedBeforeWrite = false;
    set_delete_diagnostic(result,
        "Partition table entry removed and verified; partition data sectors were not written.");
    complete_storage_operation_execution(plan.lease);
    clear_bytes(&s_deleteSnapshot, sizeof(s_deleteSnapshot));
    clear_bytes(&s_deletePreparedPlan, sizeof(s_deletePreparedPlan));
    s_deletePlanValid = false;
    clear_bytes(&plan, sizeof(plan));
    return result.status;
}

const char* delete_partition_status_name(DeletePartitionStatus status)
{
    switch (status) {
        case DELETE_PARTITION_READY_FOR_CONFIRMATION: return "Ready for explicit confirmation";
        case DELETE_PARTITION_SUCCESS: return "Partition entry removed and verified";
        case DELETE_PARTITION_OPERATION_BUSY: return "Another storage operation is active";
        case DELETE_PARTITION_INVALID_REQUEST: return "The partition deletion request is invalid";
        case DELETE_PARTITION_DEVICE_MISSING: return "The disk is no longer present";
        case DELETE_PARTITION_REGISTRY_CHANGED: return "Disk registry changed; refresh and reselect";
        case DELETE_PARTITION_IDENTITY_CHANGED: return "Disk identity changed; refresh and reselect";
        case DELETE_PARTITION_INVALID_GEOMETRY: return "Disk geometry is invalid or unsupported";
        case DELETE_PARTITION_READ_UNAVAILABLE: return "Disk reads are unavailable";
        case DELETE_PARTITION_WRITE_UNAVAILABLE: return "Metadata writes are unavailable";
        case DELETE_PARTITION_READ_ONLY: return "Disk is read-only";
        case DELETE_PARTITION_DURABILITY_UNKNOWN: return "Durable writes are not proven for this disk";
        case DELETE_PARTITION_FLUSH_UNAVAILABLE: return "A trusted flush is unavailable";
        case DELETE_PARTITION_MOUNTED: return "The selected partition is mounted; unmount it first";
        case DELETE_PARTITION_MOUNT_STATE_UNKNOWN: return "The selected partition's mount relationship is unknown";
        case DELETE_PARTITION_ROOT_BACKING: return "The selected partition backs the root filesystem";
        case DELETE_PARTITION_BOOT_BACKING: return "The selected partition is the boot partition";
        case DELETE_PARTITION_BOOT_IDENTITY_UNKNOWN: return "Boot partition identity is unknown";
        case DELETE_PARTITION_INVALID_TABLE: return "Partition table is invalid";
        case DELETE_PARTITION_GPT_DEGRADED: return "GPT copies are degraded or disagree";
        case DELETE_PARTITION_UNSUPPORTED_SCHEME: return "This partition-table layout is unsupported";
        case DELETE_PARTITION_STALE_SELECTION: return "Selected partition changed; refresh and reselect";
        case DELETE_PARTITION_OPERATION_OWNERSHIP_INVALID: return "Storage operation lease is no longer valid";
        case DELETE_PARTITION_IO_FAILED: return "Partition metadata write failed";
        case DELETE_PARTITION_FLUSH_FAILED: return "Partition metadata flush failed";
        case DELETE_PARTITION_VERIFICATION_FAILED: return "Partition metadata read-back verification failed";
        case DELETE_PARTITION_RESCAN_FAILED: return "Partition table rescan failed";
        case DELETE_PARTITION_ROLLBACK_FAILED: return "Deletion state is uncertain; inspect the disk before use";
        default: return "Unknown partition deletion status";
    }
}

const char* delete_partition_stage_name(DeletePartitionStage stage)
{
    switch (stage) {
        case DELETE_PARTITION_STAGE_IDLE: return "Idle";
        case DELETE_PARTITION_STAGE_ACQUIRE_LEASE: return "Acquire lease";
        case DELETE_PARTITION_STAGE_PIN_TARGET: return "Pin target";
        case DELETE_PARTITION_STAGE_PREFLIGHT: return "Preflight";
        case DELETE_PARTITION_STAGE_WAITING_FOR_CONFIRMATION: return "Waiting for confirmation";
        case DELETE_PARTITION_STAGE_REVALIDATING: return "Revalidate exact target";
        case DELETE_PARTITION_STAGE_SNAPSHOT: return "Snapshot partition metadata";
        case DELETE_PARTITION_STAGE_PREPARE_METADATA: return "Prepare GPT metadata";
        case DELETE_PARTITION_STAGE_WRITE_BACKUP_GPT: return "Write and verify backup GPT";
        case DELETE_PARTITION_STAGE_VERIFY_BACKUP_GPT: return "Verify backup GPT";
        case DELETE_PARTITION_STAGE_WRITE_PRIMARY_GPT: return "Write primary GPT";
        case DELETE_PARTITION_STAGE_WRITE_MBR: return "Write MBR";
        case DELETE_PARTITION_STAGE_FLUSH: return "Flush metadata";
        case DELETE_PARTITION_STAGE_VERIFY: return "Read back metadata";
        case DELETE_PARTITION_STAGE_RESCAN: return "Rescan partition table";
        case DELETE_PARTITION_STAGE_ROLLBACK_WRITE: return "Restore metadata";
        case DELETE_PARTITION_STAGE_ROLLBACK_FLUSH: return "Flush rollback";
        case DELETE_PARTITION_STAGE_ROLLBACK_VERIFY: return "Verify rollback";
        case DELETE_PARTITION_STAGE_COMPLETED: return "Completed";
        case DELETE_PARTITION_STAGE_FAILED: return "Failed";
        case DELETE_PARTITION_STAGE_STATE_UNCERTAIN: return "State uncertain";
        default: return "Unknown";
    }
}

const char* create_partition_status_name(CreatePartitionStatus status)
{
    switch (status) {
        case CREATE_PARTITION_READY: return "Ready to create partition";
        case CREATE_PARTITION_SUCCESS: return "Partition created and verified";
        case CREATE_PARTITION_OPERATION_BUSY: return "Another storage operation is active";
        case CREATE_PARTITION_INVALID_REQUEST: return "The partition request is invalid";
        case CREATE_PARTITION_DEVICE_MISSING: return "The disk is no longer present";
        case CREATE_PARTITION_REGISTRY_CHANGED: return "Disk registry changed; refresh and try again";
        case CREATE_PARTITION_IDENTITY_CHANGED: return "Disk identity changed; refresh and try again";
        case CREATE_PARTITION_INVALID_GEOMETRY: return "Disk geometry is invalid or unsupported";
        case CREATE_PARTITION_READ_UNAVAILABLE: return "Disk reads are unavailable";
        case CREATE_PARTITION_WRITE_UNAVAILABLE: return "Durable sector writes are unavailable";
        case CREATE_PARTITION_READ_ONLY: return "Disk is read-only";
        case CREATE_PARTITION_DURABILITY_UNKNOWN: return "Durable writes are not proven for this disk";
        case CREATE_PARTITION_FLUSH_UNAVAILABLE: return "A trusted flush is unavailable";
        case CREATE_PARTITION_MOUNTED: return "Disk is mounted and protected";
        case CREATE_PARTITION_ROOT_BACKING: return "Disk backs the root filesystem and is protected";
        case CREATE_PARTITION_BOOT_BACKING: return "Disk is the boot device and is protected";
        case CREATE_PARTITION_BOOT_IDENTITY_UNKNOWN: return "Boot-device identity is unknown";
        case CREATE_PARTITION_INVALID_TABLE: return "Partition table is invalid";
        case CREATE_PARTITION_GPT_DEGRADED: return "Partition creation unavailable: GPT metadata requires repair.";
        case CREATE_PARTITION_UNSUPPORTED_SCHEME: return "Partition scheme is unsupported for creation";
        case CREATE_PARTITION_STALE_REGION: return "Selected free space no longer exists";
        case CREATE_PARTITION_NO_GPT_ENTRY: return "No free GPT partition entries are available.";
        case CREATE_PARTITION_NO_MBR_ENTRY: return "No free primary MBR partition entries are available.";
        case CREATE_PARTITION_NO_ALIGNED_SPACE: return "No 1 MiB-aligned partition space remains in this region";
        case CREATE_PARTITION_TOO_SMALL: return "Requested size is below the 1 MiB minimum";
        case CREATE_PARTITION_TOO_LARGE: return "Requested partition is too large for this free region";
        case CREATE_PARTITION_UNSUPPORTED_TYPE: return "Partition type is not supported";
        case CREATE_PARTITION_GUID_UNAVAILABLE: return "A unique partition GUID could not be generated";
        case CREATE_PARTITION_GUID_COLLISION: return "Generated partition GUID is already in use";
        case CREATE_PARTITION_OPERATION_OWNERSHIP_INVALID: return "Storage operation lease is no longer valid";
        case CREATE_PARTITION_IO_FAILED: return "Partition table write failed";
        case CREATE_PARTITION_FLUSH_FAILED: return "Disk flush failed";
        case CREATE_PARTITION_VERIFICATION_FAILED: return "Partition table read-back verification failed";
        case CREATE_PARTITION_RESCAN_FAILED: return "Disk Manager rescan could not verify the new partition";
        case CREATE_PARTITION_ROLLBACK_FAILED: return "Rollback failed; disk state is uncertain";
        default: return "Partition creation failed";
    }
}

const char* create_partition_stage_name(CreatePartitionStage stage)
{
    switch (stage) {
        case CREATE_PARTITION_STAGE_ACQUIRE_LEASE: return "AcquireLease";
        case CREATE_PARTITION_STAGE_PIN_TARGET: return "PinTarget";
        case CREATE_PARTITION_STAGE_PREFLIGHT: return "Preflight";
        case CREATE_PARTITION_STAGE_VALIDATING: return "Validating";
        case CREATE_PARTITION_STAGE_REVALIDATING_FREE_SPACE: return "Revalidating free space";
        case CREATE_PARTITION_STAGE_SNAPSHOT_TABLE: return "SnapshotTable";
        case CREATE_PARTITION_STAGE_SNAPSHOTTING: return "Snapshotting metadata";
        case CREATE_PARTITION_STAGE_PREPARE_METADATA: return "PrepareMetadata";
        case CREATE_PARTITION_STAGE_WRITE_BACKUP_GPT: return "WriteBackupGPT";
        case CREATE_PARTITION_STAGE_WRITE_PRIMARY_GPT: return "WritePrimaryGPT";
        case CREATE_PARTITION_STAGE_FLUSH: return "Flush";
        case CREATE_PARTITION_STAGE_VERIFY: return "Verify";
        case CREATE_PARTITION_STAGE_RESCAN: return "Rescan";
        case CREATE_PARTITION_STAGE_ROLLBACK_WRITE: return "RollbackWrite";
        case CREATE_PARTITION_STAGE_ROLLBACK_FLUSH: return "RollbackFlush";
        case CREATE_PARTITION_STAGE_ROLLBACK_VERIFY: return "RollbackVerify";
        case CREATE_PARTITION_STAGE_WRITING_GPT:
        case CREATE_PARTITION_STAGE_WRITING_MBR: return "Writing partition table";
        case CREATE_PARTITION_STAGE_FLUSHING: return "Flushing";
        case CREATE_PARTITION_STAGE_VERIFYING: return "Verifying";
        case CREATE_PARTITION_STAGE_RESCANNING: return "Rescanning";
        case CREATE_PARTITION_STAGE_COMPLETED: return "Completed";
        case CREATE_PARTITION_STAGE_FAILED: return "Failed";
        case CREATE_PARTITION_STAGE_STATE_UNCERTAIN: return "State uncertain";
        case CREATE_PARTITION_STAGE_IDLE: default: return "Idle";
    }
}

} // namespace storage
} // namespace kernel
