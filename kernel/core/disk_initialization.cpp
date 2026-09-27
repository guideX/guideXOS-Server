#include "include/kernel/disk_initialization.h"
#include "include/kernel/block_device.h"
#if !defined(KERNEL_STORAGE_TEST)
#include "include/kernel/virtio_rng.h"
#endif
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace kernel {
namespace storage {

namespace {

static const uint64_t GPT_ALIGNMENT_BYTES = 1024ull * 1024ull;
static const uint8_t GPT_HEADER_SIGNATURE[8] = {'E','F','I',' ','P','A','R','T'};

static volatile uint32_t s_operationMetadataLock = 0;
static bool s_operationHeld = false;
static bool s_operationExecuting = false;
static uint64_t s_operationOwner = 0;
static uint64_t s_nextOperationOwner = 0;
static bool s_activePlanValid = false;
static InitializeDiskPlan s_activePlan;
static bool s_operationTargetPinned = false;
static uint8_t s_pinnedTargetIndex = 0xFFu;
static uint64_t s_pinnedTargetRegistrationId = 0;

struct MetadataSnapshot {
    alignas(4096) uint8_t mbr[MAX_LOGICAL_SECTOR_SIZE];
    alignas(4096) uint8_t primaryHeader[MAX_LOGICAL_SECTOR_SIZE];
    alignas(4096) uint8_t primaryArray[INITIALIZE_GPT_ARRAY_BYTES];
    alignas(4096) uint8_t backupArray[INITIALIZE_GPT_ARRAY_BYTES];
    alignas(4096) uint8_t backupHeader[MAX_LOGICAL_SECTOR_SIZE];
    bool valid;
};

static MetadataSnapshot s_snapshot;
alignas(4096) static uint8_t s_ioSector[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_entries[INITIALIZE_GPT_ARRAY_BYTES];
alignas(4096) static uint8_t s_primaryHeader[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_backupHeader[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_protectiveMbr[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static PartitionTableModel s_parseScratch;

static void lock_operation_metadata()
{
#if defined(_MSC_VER)
    while (_InterlockedCompareExchange(
        reinterpret_cast<volatile long*>(&s_operationMetadataLock), 1, 0) != 0) {}
#else
    while (__sync_lock_test_and_set(&s_operationMetadataLock, 1) != 0) {}
#endif
}

static void unlock_operation_metadata()
{
#if defined(_MSC_VER)
    _InterlockedExchange(reinterpret_cast<volatile long*>(&s_operationMetadataLock), 0);
#else
    __sync_lock_release(&s_operationMetadataLock);
#endif
}

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

static bool bytes_equal(const uint8_t* left, const uint8_t* right, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i) if (left[i] != right[i]) return false;
    return true;
}

static bool initialize_plans_equal(const InitializeDiskPlan& left,
                                   const InitializeDiskPlan& right)
{
    return left.ownerToken == right.ownerToken &&
        target_identities_equal(left.targetSnapshot, right.targetSnapshot) &&
        left.requestedScheme == right.requestedScheme &&
        left.expectedRegistryGeneration == right.expectedRegistryGeneration &&
        left.totalLogicalSectors == right.totalLogicalSectors &&
        left.logicalSectorSize == right.logicalSectorSize &&
        left.firstUsableLba == right.firstUsableLba &&
        left.lastUsableLba == right.lastUsableLba &&
        left.entryArraySectors == right.entryArraySectors &&
        bytes_equal(left.diskGuid, right.diskGuid, sizeof(left.diskGuid)) &&
        left.mbrDiskSignature == right.mbrDiskSignature &&
        left.confirmationReady == right.confirmationReady;
}

static bool publish_active_plan(const InitializeDiskPlan& plan)
{
    lock_operation_metadata();
    if (!s_operationHeld || s_operationExecuting ||
        s_operationOwner != plan.ownerToken) {
        unlock_operation_metadata();
        return false;
    }
    s_activePlan = plan;
    s_activePlanValid = true;
    unlock_operation_metadata();
    return true;
}

static bool get_active_plan(InitializeDiskPlan& plan)
{
    lock_operation_metadata();
    const bool valid = s_activePlanValid;
    if (valid) plan = s_activePlan;
    unlock_operation_metadata();
    return valid;
}

static bool bytes_zero(const uint8_t* bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) if (bytes[i] != 0) return false;
    return true;
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

static uint32_t read_u32(const uint8_t* input)
{
    return static_cast<uint32_t>(input[0]) |
           (static_cast<uint32_t>(input[1]) << 8) |
           (static_cast<uint32_t>(input[2]) << 16) |
           (static_cast<uint32_t>(input[3]) << 24);
}

static void set_diagnostic(InitializeDiskResult& result, const char* message)
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

static void reset_result(InitializeDiskResult& result)
{
    clear_bytes(&result, sizeof(result));
    result.status = INITIALIZE_DISK_INVALID_REQUEST;
    result.stage = INITIALIZE_STAGE_IDLE;
    result.flushOutcome = block::FLUSH_OUTCOME_INVALID;
    result.flushStatus = block::BLOCK_ERR_UNSUPPORTED;
    result.finalDetectedState = DISK_STATE_UNREADABLE;
}

static void reset_validation(InitializeTargetValidation& validation)
{
    clear_bytes(&validation, sizeof(validation));
    validation.status = INITIALIZE_DISK_INVALID_REQUEST;
    validation.detectedState = DISK_STATE_UNREADABLE;
    validation.mountSafety = DEVICE_IDENTITY_UNKNOWN;
    validation.bootSafety = BOOT_DEVICE_IDENTITY_UNKNOWN;
}

static InitializeDiskStatus map_identity_status(RevalidationStatus status)
{
    switch (status) {
        case TARGET_REGISTRY_CHANGED: return INITIALIZE_DISK_REGISTRY_CHANGED;
        case TARGET_DEVICE_MISSING: return INITIALIZE_DISK_DEVICE_MISSING;
        case TARGET_IDENTITY_MISMATCH: return INITIALIZE_DISK_IDENTITY_CHANGED;
        case TARGET_VALID: default: return INITIALIZE_DISK_SUCCESS;
    }
}

static bool calculate_layout(const DeviceCapabilities& capabilities,
                             PartitionScheme scheme,
                             InitializeTargetValidation& validation)
{
    const uint64_t total = capabilities.totalLogicalSectors;
    const uint32_t sectorSize = capabilities.logicalSectorSize;
    validation.logicalSectorSize = sectorSize;
    validation.totalLogicalSectors = total;

    if (sectorSize != 512 && sectorSize != 4096) {
        validation.status = INITIALIZE_DISK_UNSUPPORTED_SECTOR_SIZE;
        return false;
    }
    if (capabilities.requiredBufferAlignment > MAX_LOGICAL_SECTOR_SIZE ||
        (capabilities.requiredBufferAlignment > 1 &&
         (capabilities.requiredBufferAlignment &
          (capabilities.requiredBufferAlignment - 1)) != 0)) {
        validation.status = INITIALIZE_DISK_UNSUPPORTED_SECTOR_SIZE;
        return false;
    }

    if (scheme == PARTITION_SCHEME_GPT) {
        if (INITIALIZE_GPT_ARRAY_BYTES % sectorSize != 0) {
            validation.status = INITIALIZE_DISK_UNSUPPORTED_SECTOR_SIZE;
            return false;
        }
        const uint32_t arraySectors = INITIALIZE_GPT_ARRAY_BYTES / sectorSize;
        if (total <= static_cast<uint64_t>(arraySectors) + 3) {
            validation.status = INITIALIZE_DISK_TOO_SMALL;
            return false;
        }
        const uint64_t backupArrayLba = total - 1 - arraySectors;
        if (backupArrayLba == 0) {
            validation.status = INITIALIZE_DISK_TOO_SMALL;
            return false;
        }
        uint64_t firstUsable = 2 + arraySectors;
        const uint64_t alignmentSectors = GPT_ALIGNMENT_BYTES / sectorSize;
        if (firstUsable < alignmentSectors) firstUsable = alignmentSectors;
        else if (firstUsable % alignmentSectors != 0) {
            const uint64_t remainder = firstUsable % alignmentSectors;
            const uint64_t add = alignmentSectors - remainder;
            if (firstUsable > UINT64_MAX - add) {
                validation.status = INITIALIZE_DISK_TOO_SMALL;
                return false;
            }
            firstUsable += add;
        }
        const uint64_t lastUsable = backupArrayLba - 1;
        if (firstUsable > lastUsable) {
            validation.status = INITIALIZE_DISK_TOO_SMALL;
            return false;
        }
        validation.entryArraySectors = arraySectors;
        validation.firstUsableLba = firstUsable;
        validation.lastUsableLba = lastUsable;
        return true;
    }

    if (scheme == PARTITION_SCHEME_MBR) {
        if (total < 16) {
            validation.status = INITIALIZE_DISK_TOO_SMALL;
            return false;
        }
        if (total - 1 > 0xFFFFFFFFull) {
            validation.status = INITIALIZE_DISK_MBR_CAPACITY_LIMIT;
            return false;
        }
        validation.entryArraySectors = 0;
        validation.firstUsableLba = 1;
        validation.lastUsableLba = total - 1;
        return true;
    }

    validation.status = INITIALIZE_DISK_INVALID_REQUEST;
    return false;
}

static InitializeDiskStatus map_safety_issues(uint32_t issues)
{
    if (issues & SAFETY_ISSUE_REGISTRY_CHANGED) return INITIALIZE_DISK_REGISTRY_CHANGED;
    if (issues & SAFETY_ISSUE_DEVICE_MISSING) return INITIALIZE_DISK_DEVICE_MISSING;
    if (issues & SAFETY_ISSUE_IDENTITY_MISMATCH) return INITIALIZE_DISK_IDENTITY_CHANGED;
    if (issues & SAFETY_ISSUE_UNKNOWN_GEOMETRY) return INITIALIZE_DISK_INVALID_GEOMETRY;
    if (issues & SAFETY_ISSUE_UNREADABLE) return INITIALIZE_DISK_READ_UNAVAILABLE;
    if (issues & SAFETY_ISSUE_READ_ONLY) return INITIALIZE_DISK_READ_ONLY;
    if (issues & SAFETY_ISSUE_DURABILITY_UNKNOWN) return INITIALIZE_DISK_DURABILITY_UNKNOWN;
    if (issues & SAFETY_ISSUE_ROOT_BACKING) return INITIALIZE_DISK_ROOT_BACKING;
    if (issues & SAFETY_ISSUE_MOUNTED) return INITIALIZE_DISK_MOUNTED;
    if (issues & SAFETY_ISSUE_PARTITION_STATE) return INITIALIZE_DISK_NOT_RAW;
    return INITIALIZE_DISK_INVALID_REQUEST;
}

static bool read_one(uint8_t device, uint64_t lba, uint8_t* output,
                     uint32_t sectorSize)
{
    return read_logical_sector(device, lba, output, sectorSize) == block::BLOCK_OK;
}

static bool read_region_to_snapshot(uint8_t device, uint64_t lba,
                                    uint32_t sectors, uint32_t sectorSize,
                                    uint8_t* output)
{
    for (uint32_t i = 0; i < sectors; ++i) {
        if (!read_one(device, lba + i, s_ioSector, sectorSize)) return false;
        copy_bytes(output + static_cast<size_t>(i) * sectorSize,
                   s_ioSector, sectorSize);
    }
    return true;
}

static bool capture_metadata_snapshot(const InitializeDiskPlan& plan)
{
    clear_bytes(&s_snapshot, sizeof(s_snapshot));
    const uint8_t device = plan.targetSnapshot.globalIndex;
    const uint32_t sectorSize = plan.logicalSectorSize;
    if (!read_region_to_snapshot(device, 0, 1, sectorSize, s_snapshot.mbr))
        return false;
    if (plan.requestedScheme == PARTITION_SCHEME_GPT) {
        if (!read_region_to_snapshot(device, 1, 1, sectorSize,
                                     s_snapshot.primaryHeader) ||
            !read_region_to_snapshot(device, 2, plan.entryArraySectors,
                                     sectorSize, s_snapshot.primaryArray))
            return false;
        const uint64_t backupArrayLba = plan.totalLogicalSectors - 1 -
                                        plan.entryArraySectors;
        if (!read_region_to_snapshot(device, backupArrayLba,
                                     plan.entryArraySectors, sectorSize,
                                     s_snapshot.backupArray) ||
            !read_region_to_snapshot(device, plan.totalLogicalSectors - 1, 1,
                                     sectorSize, s_snapshot.backupHeader))
            return false;
    }
    s_snapshot.valid = true;
    return true;
}

static bool metadata_snapshot_is_clear(const InitializeDiskPlan& plan)
{
    const uint32_t sectorSize = plan.logicalSectorSize;
    if (!bytes_zero(s_snapshot.mbr, sectorSize)) return false;
    if (plan.requestedScheme != PARTITION_SCHEME_GPT) return true;
    return bytes_zero(s_snapshot.primaryHeader, sectorSize) &&
           bytes_zero(s_snapshot.primaryArray, INITIALIZE_GPT_ARRAY_BYTES) &&
           bytes_zero(s_snapshot.backupArray, INITIALIZE_GPT_ARRAY_BYTES) &&
           bytes_zero(s_snapshot.backupHeader, sectorSize);
}

static bool compare_region_to_snapshot(uint8_t device, uint64_t lba,
                                       uint32_t sectors, uint32_t sectorSize,
                                       const uint8_t* expected)
{
    for (uint32_t i = 0; i < sectors; ++i) {
        if (!read_one(device, lba + i, s_ioSector, sectorSize) ||
            !bytes_equal(s_ioSector,
                         expected + static_cast<size_t>(i) * sectorSize,
                         sectorSize)) return false;
    }
    return true;
}

static bool metadata_matches_snapshot(const InitializeDiskPlan& plan)
{
    if (!s_snapshot.valid) return false;
    const uint8_t device = plan.targetSnapshot.globalIndex;
    const uint32_t sectorSize = plan.logicalSectorSize;
    if (!compare_region_to_snapshot(device, 0, 1, sectorSize, s_snapshot.mbr))
        return false;
    if (plan.requestedScheme != PARTITION_SCHEME_GPT) return true;
    if (!compare_region_to_snapshot(device, 1, 1, sectorSize,
                                    s_snapshot.primaryHeader) ||
        !compare_region_to_snapshot(device, 2, plan.entryArraySectors,
                                    sectorSize, s_snapshot.primaryArray))
        return false;
    const uint64_t backupArrayLba = plan.totalLogicalSectors - 1 -
                                    plan.entryArraySectors;
    return compare_region_to_snapshot(device, backupArrayLba,
                                      plan.entryArraySectors, sectorSize,
                                      s_snapshot.backupArray) &&
           compare_region_to_snapshot(device, plan.totalLogicalSectors - 1,
                                      1, sectorSize, s_snapshot.backupHeader);
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

static bool make_disk_guid(uint8_t guid[16], const InitializeDiskRequest& request)
{
#if !defined(KERNEL_STORAGE_TEST)
    (void)request;
#endif
#if defined(KERNEL_STORAGE_TEST)
    if (request.testGuidProvided) {
        copy_bytes(guid, request.testDiskGuid, 16);
    } else
#endif
    {
#if defined(KERNEL_STORAGE_TEST)
        (void)request;
#endif
        bool gotNonzero = false;
        for (uint8_t attempt = 0; attempt < 3 && !gotNonzero; ++attempt) {
            if (!secure_random(guid, 16)) return false;
            gotNonzero = !bytes_zero(guid, 16);
        }
        if (!gotNonzero) return false;
    }
    if (bytes_zero(guid, 16)) return false;
    // RFC 4122 variant and version 4 bits in the on-disk EFI_GUID byte order.
    guid[7] = static_cast<uint8_t>((guid[7] & 0x0F) | 0x40);
    guid[8] = static_cast<uint8_t>((guid[8] & 0x3F) | 0x80);
    return !bytes_zero(guid, 16);
}

static bool make_mbr_signature(uint32_t& signature,
                               const InitializeDiskRequest& request)
{
#if defined(KERNEL_STORAGE_TEST)
    if (request.testMbrSignatureProvided) {
        signature = request.testMbrSignature;
        return signature != 0;
    }
#else
    (void)request;
#endif
    uint8_t bytes[4];
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        if (!secure_random(bytes, sizeof(bytes))) return false;
        signature = read_u32(bytes);
        if (signature != 0) return true;
    }
    return false;
}

static void build_gpt_header(uint8_t* sector, uint32_t sectorSize,
                             uint64_t currentLba, uint64_t backupLba,
                             uint64_t firstUsableLba, uint64_t lastUsableLba,
                             const uint8_t diskGuid[16], uint64_t entriesLba,
                             uint32_t entriesCrc)
{
    clear_bytes(sector, sectorSize);
    copy_bytes(sector, GPT_HEADER_SIGNATURE, sizeof(GPT_HEADER_SIGNATURE));
    write_u32(sector + 8, 0x00010000u);
    write_u32(sector + 12, 92);
    write_u64(sector + 24, currentLba);
    write_u64(sector + 32, backupLba);
    write_u64(sector + 40, firstUsableLba);
    write_u64(sector + 48, lastUsableLba);
    copy_bytes(sector + 56, diskGuid, 16);
    write_u64(sector + 72, entriesLba);
    write_u32(sector + 80, INITIALIZE_GPT_ENTRY_COUNT);
    write_u32(sector + 84, INITIALIZE_GPT_ENTRY_SIZE);
    write_u32(sector + 88, entriesCrc);
    write_u32(sector + 16, 0);
    write_u32(sector + 16, crc32(sector, 92));
}

static bool revalidate_before_io(const InitializeDiskPlan& plan,
                                 InitializeDiskStatus& error)
{
    error = map_identity_status(revalidate_target_identity(plan.targetSnapshot));
    if (error != INITIALIZE_DISK_SUCCESS) return false;
    return true;
}

static block::Status write_one(const InitializeDiskPlan& plan, uint64_t lba,
                               const uint8_t* source, bool& attempted,
                               InitializeDiskStatus& failure)
{
    if (!revalidate_before_io(plan, failure)) return block::BLOCK_ERR_INVALID;
    copy_bytes(s_ioSector, source, plan.logicalSectorSize);
    attempted = true;
    const block::Status status = write_sectors_safe(
        plan.targetSnapshot.globalIndex, lba, 1, s_ioSector,
        plan.logicalSectorSize);
    if (status != block::BLOCK_OK && failure == INITIALIZE_DISK_SUCCESS)
        failure = INITIALIZE_DISK_IO_FAILED;
    return status;
}

static bool write_region(const InitializeDiskPlan& plan, uint64_t lba,
                         uint32_t sectors, const uint8_t* source,
                         uint32_t completedStage, InitializeDiskResult& result)
{
    for (uint32_t i = 0; i < sectors; ++i) {
        InitializeDiskStatus failure = INITIALIZE_DISK_SUCCESS;
        const uint8_t* sector = source + static_cast<size_t>(i) *
                                plan.logicalSectorSize;
        const block::Status status = write_one(plan, lba + i, sector,
                                               result.writeAttempted, failure);
        if (status != block::BLOCK_OK) {
            result.status = failure;
            result.stage = INITIALIZE_STAGE_FAILED;
            return false;
        }
    }
    result.writeStagesCompleted |= completedStage;
    return true;
}

static void build_protective_mbr(const InitializeDiskPlan& plan)
{
    clear_bytes(s_protectiveMbr, plan.logicalSectorSize);
    uint8_t* entry = s_protectiveMbr + 446;
    entry[1] = 0x00;
    entry[2] = 0x02;
    entry[3] = 0x00;
    entry[4] = 0xEE;
    entry[5] = 0xFE;
    entry[6] = 0xFF;
    entry[7] = 0xFF;
    write_u32(entry + 8, 1);
    uint64_t count = plan.totalLogicalSectors - 1;
    if (count > 0xFFFFFFFFull) count = 0xFFFFFFFFull;
    write_u32(entry + 12, static_cast<uint32_t>(count));
    s_protectiveMbr[510] = 0x55;
    s_protectiveMbr[511] = 0xAA;
}

static bool write_gpt(const InitializeDiskPlan& plan,
                      InitializeDiskResult& result)
{
    result.stage = INITIALIZE_STAGE_WRITING_GPT_METADATA;
    clear_bytes(s_entries, INITIALIZE_GPT_ARRAY_BYTES);
    const uint32_t entriesCrc = crc32(s_entries, INITIALIZE_GPT_ARRAY_BYTES);
    const uint64_t backupArrayLba = plan.totalLogicalSectors - 1 -
                                    plan.entryArraySectors;
    build_gpt_header(s_backupHeader, plan.logicalSectorSize,
                     plan.totalLogicalSectors - 1, 1,
                     plan.firstUsableLba, plan.lastUsableLba, plan.diskGuid,
                     backupArrayLba, entriesCrc);
    build_gpt_header(s_primaryHeader, plan.logicalSectorSize,
                     1, plan.totalLogicalSectors - 1,
                     plan.firstUsableLba, plan.lastUsableLba, plan.diskGuid,
                     2, entriesCrc);
    build_protective_mbr(plan);

    if (!write_region(plan, backupArrayLba, plan.entryArraySectors,
                      s_entries, INITIALIZE_WRITE_BACKUP_ARRAY, result) ||
        !write_region(plan, plan.totalLogicalSectors - 1, 1,
                      s_backupHeader, INITIALIZE_WRITE_BACKUP_HEADER, result) ||
        !write_region(plan, 2, plan.entryArraySectors,
                      s_entries, INITIALIZE_WRITE_PRIMARY_ARRAY, result) ||
        !write_region(plan, 1, 1, s_primaryHeader,
                      INITIALIZE_WRITE_PRIMARY_HEADER, result) ||
        !write_region(plan, 0, 1, s_protectiveMbr,
                      INITIALIZE_WRITE_PROTECTIVE_MBR, result)) return false;
    return true;
}

static bool write_mbr(const InitializeDiskPlan& plan,
                      InitializeDiskResult& result)
{
    result.stage = INITIALIZE_STAGE_WRITING_MBR;
    clear_bytes(s_ioSector, plan.logicalSectorSize);
    write_u32(s_ioSector + 440, plan.mbrDiskSignature);
    s_ioSector[510] = 0x55;
    s_ioSector[511] = 0xAA;
    // Keep the write source separate because write_one uses s_ioSector as its
    // alignment-safe transport staging buffer.
    copy_bytes(s_protectiveMbr, s_ioSector, plan.logicalSectorSize);
    if (!write_region(plan, 0, 1, s_protectiveMbr,
                      INITIALIZE_WRITE_MBR, result)) return false;
    return true;
}

static bool flush_is_proven(const block::FlushReport& report)
{
    return report.status == block::BLOCK_OK && report.semanticsKnown &&
        (report.outcome == block::FLUSH_OUTCOME_SUPPORTED_SUCCEEDED ||
         report.outcome == block::FLUSH_OUTCOME_SYNCHRONOUS_DURABLE);
}

static bool verify_partition_state(const InitializeDiskPlan& plan,
                                   DiskState& detected)
{
    if (!parse_partition_table(plan.targetSnapshot.globalIndex, s_parseScratch)) {
        detected = DISK_STATE_UNREADABLE;
        return false;
    }
    detected = s_parseScratch.state;
    if (plan.requestedScheme == PARTITION_SCHEME_GPT) {
        return s_parseScratch.state == DISK_STATE_VALID_GPT &&
            s_parseScratch.primaryGptValid && s_parseScratch.backupGptValid &&
            s_parseScratch.gptCopiesAgree && s_parseScratch.protectiveMbr &&
            s_parseScratch.partitionCount == 0 &&
            bytes_equal(s_parseScratch.primaryDiskGuid, plan.diskGuid, 16) &&
            bytes_equal(s_parseScratch.backupDiskGuid, plan.diskGuid, 16);
    }
    return s_parseScratch.state == DISK_STATE_VALID_MBR &&
        !s_parseScratch.protectiveMbr && s_parseScratch.partitionCount == 0 &&
        s_parseScratch.tableEntryCount == 0 &&
        s_parseScratch.mbrDiskSignature == plan.mbrDiskSignature;
}

static bool restore_region(const InitializeDiskPlan& plan, uint64_t lba,
                           uint32_t sectors, const uint8_t* source)
{
    for (uint32_t i = 0; i < sectors; ++i) {
        if (revalidate_target_identity(plan.targetSnapshot) != TARGET_VALID)
            return false;
        copy_bytes(s_ioSector,
                   source + static_cast<size_t>(i) * plan.logicalSectorSize,
                   plan.logicalSectorSize);
        if (write_sectors_safe(plan.targetSnapshot.globalIndex, lba + i, 1,
                               s_ioSector, plan.logicalSectorSize) != block::BLOCK_OK)
            return false;
    }
    return true;
}

static bool restore_snapshot(const InitializeDiskPlan& plan,
                             InitializeDiskResult& result)
{
    if (!result.writeAttempted || !s_snapshot.valid ||
        revalidate_target_identity(plan.targetSnapshot) != TARGET_VALID)
        return false;
    result.rollbackAttempted = true;
    bool writesOk = true;
    // Remove the visible marker and both headers before restoring their arrays.
    writesOk = restore_region(plan, 0, 1, s_snapshot.mbr) && writesOk;
    if (plan.requestedScheme == PARTITION_SCHEME_GPT) {
        writesOk = restore_region(plan, 1, 1, s_snapshot.primaryHeader) && writesOk;
        writesOk = restore_region(plan, plan.totalLogicalSectors - 1, 1,
                                  s_snapshot.backupHeader) && writesOk;
        const uint64_t backupArrayLba = plan.totalLogicalSectors - 1 -
                                        plan.entryArraySectors;
        writesOk = restore_region(plan, backupArrayLba, plan.entryArraySectors,
                                  s_snapshot.backupArray) && writesOk;
        writesOk = restore_region(plan, 2, plan.entryArraySectors,
                                  s_snapshot.primaryArray) && writesOk;
    }
    if (!writesOk || revalidate_target_identity(plan.targetSnapshot) != TARGET_VALID)
        return false;

    const block::FlushReport rollbackFlush =
        block::flush_with_result(plan.targetSnapshot.globalIndex);
    if (!flush_is_proven(rollbackFlush)) return false;
    if (!metadata_matches_snapshot(plan)) return false;
    DiskState finalState = DISK_STATE_UNREADABLE;
    if (!parse_partition_table(plan.targetSnapshot.globalIndex, s_parseScratch))
        return false;
    finalState = s_parseScratch.state;
    result.finalDetectedState = finalState;
    return finalState == DISK_STATE_NOT_INITIALIZED;
}

static void finish_failure(InitializeDiskPlan& plan,
                           InitializeDiskResult& result,
                           InitializeDiskStatus failure,
                           const char* diagnostic)
{
    result.status = failure;
    result.stage = INITIALIZE_STAGE_FAILED;
    set_diagnostic(result, diagnostic ? diagnostic :
                   initialize_disk_status_name(failure));
    if (result.writeAttempted) {
        result.rollbackSucceeded = restore_snapshot(plan, result);
        if (!result.rollbackSucceeded) {
            result.finalStateUncertain = true;
            result.status = INITIALIZE_DISK_ROLLBACK_FAILED;
            set_diagnostic(result,
                "Initialization failed; rollback could not be verified. Refresh and inspect the disk.");
        }
    }
    const RevalidationStatus failureIdentity =
        revalidate_target_identity(plan.targetSnapshot);
    if (failureIdentity != TARGET_VALID) {
        result.finalStateUncertain = result.writeAttempted;
        if (result.writeAttempted) {
            result.status = INITIALIZE_DISK_ROLLBACK_FAILED;
            set_diagnostic(result,
                "Target identity changed during initialization; disk state is uncertain. Refresh and inspect it.");
        }
    } else if (result.writeAttempted) {
        // A failed or untrusted rollback always forces a fresh parser rescan
        // while the lease is still held. The UI performs its own enumeration
        // after this storage-layer rescan returns.
        if (!parse_partition_table(plan.targetSnapshot.globalIndex, s_parseScratch)) {
            result.finalDetectedState = DISK_STATE_UNREADABLE;
            result.finalStateUncertain = true;
        } else {
            result.finalDetectedState = s_parseScratch.state;
        }
    }
    StorageOperationLease lease;
    lease.ownerToken = plan.ownerToken;
    complete_storage_operation_execution(lease);
    plan.ownerToken = 0;
    plan.confirmationReady = false;
    clear_bytes(&s_snapshot, sizeof(s_snapshot));
}

static InitializeDiskStatus validate_target_impl(
    const TargetIdentity& target, PartitionScheme scheme,
    const StorageOperationLease& lease,
    InitializeTargetValidation& validation)
{
    reset_validation(validation);
    if (!storage_operation_lease_is_current(lease)) {
        validation.status = INITIALIZE_DISK_OPERATION_OWNERSHIP_INVALID;
        return validation.status;
    }
    const RevalidationStatus identity = revalidate_target_identity(target);
    if (identity != TARGET_VALID) {
        validation.status = map_identity_status(identity);
        return validation.status;
    }

    DeviceCapabilities capabilities;
    if (!query_device_capabilities(target.globalIndex, capabilities)) {
        validation.status = INITIALIZE_DISK_DEVICE_MISSING;
        return validation.status;
    }
    if (!capabilities.geometryValid || !capabilities.capacityValid) {
        validation.status = INITIALIZE_DISK_INVALID_GEOMETRY;
        return validation.status;
    }
    if (!capabilities.readable) {
        validation.status = INITIALIZE_DISK_READ_UNAVAILABLE;
        return validation.status;
    }
    if (!capabilities.writable) {
        validation.status = INITIALIZE_DISK_READ_ONLY;
        return validation.status;
    }
    if (capabilities.maxTransferBytes != 0 &&
        capabilities.maxTransferBytes < capabilities.logicalSectorSize) {
        validation.status = INITIALIZE_DISK_WRITE_UNAVAILABLE;
        return validation.status;
    }
    if (capabilities.persistence == PERSISTENCE_FLUSH_REQUIRED) {
        if (!capabilities.flushSupported || !capabilities.flushSemanticsKnown) {
            validation.status = INITIALIZE_DISK_FLUSH_UNAVAILABLE;
            return validation.status;
        }
    } else if (capabilities.persistence != PERSISTENCE_SYNCHRONOUS_DURABLE) {
        validation.status = INITIALIZE_DISK_DURABILITY_UNKNOWN;
        return validation.status;
    }

    // Reject MBR's hard addressability limit before probing a multi-terabyte
    // fake or real device. Other capacity checks follow the raw-state check so
    // existing tables receive the more useful "not raw" result.
    if (scheme == PARTITION_SCHEME_MBR &&
        capabilities.totalLogicalSectors - 1 > 0xFFFFFFFFull) {
        validation.status = INITIALIZE_DISK_MBR_CAPACITY_LIMIT;
        return validation.status;
    }

    SafetyRequest safetyRequest;
    safetyRequest.requireWritable = true;
    safetyRequest.requireDurableWrites = true;
    safetyRequest.requireKnownPartitionState = true;
    safetyRequest.allowNotInitialized = true;
    SafetyValidation safety;
    if (!validate_destructive_target(target, safetyRequest, safety)) {
        validation.destructiveIssues = safety.issues;
        validation.mountSafety = safety.mountSafety;
        validation.detectedState = safety.diskState;
        validation.status = map_safety_issues(safety.issues);
        return validation.status;
    }
    validation.destructiveIssues = safety.issues;
    validation.mountSafety = safety.mountSafety;
    validation.detectedState = safety.diskState;
    if (safety.diskState != DISK_STATE_NOT_INITIALIZED) {
        validation.status = INITIALIZE_DISK_NOT_RAW;
        return validation.status;
    }

    const BootProtection boot = query_boot_protection(target);
    validation.bootSafety = boot.safety;
    if (boot.safety == BOOT_DEVICE_IS_TARGET) {
        validation.status = INITIALIZE_DISK_BOOT_BACKING;
        return validation.status;
    }
    if (boot.safety != BOOT_DEVICE_DEFINITELY_NOT_TARGET) {
        validation.status = INITIALIZE_DISK_BOOT_IDENTITY_UNKNOWN;
        return validation.status;
    }
    if (!calculate_layout(capabilities, scheme, validation))
        return validation.status;
    if (revalidate_target_identity(target) != TARGET_VALID) {
        validation.status = map_identity_status(revalidate_target_identity(target));
        return validation.status;
    }
    validation.status = INITIALIZE_DISK_READY_FOR_CONFIRMATION;
    return validation.status;
}

} // namespace

StorageOperationLockStatus try_acquire_storage_operation(
    StorageOperationLease& lease)
{
    if (lease.ownerToken != 0) return STORAGE_OPERATION_LOCK_INVALID_OWNER;
    lock_operation_metadata();
    if (s_operationHeld) {
        unlock_operation_metadata();
        return STORAGE_OPERATION_LOCK_BUSY;
    }
    if (s_nextOperationOwner == UINT64_MAX) {
        unlock_operation_metadata();
        return STORAGE_OPERATION_LOCK_INVALID_OWNER;
    }
    ++s_nextOperationOwner;
    s_operationOwner = s_nextOperationOwner;
    s_operationHeld = true;
    s_operationExecuting = false;
    s_activePlanValid = false;
    s_operationTargetPinned = false;
    s_pinnedTargetIndex = 0xFFu;
    s_pinnedTargetRegistrationId = 0;
    lease.ownerToken = s_operationOwner;
    lease.targetPinned = false;
    lease.pinnedIndex = 0xFFu;
    lease.pinnedRegistrationId = 0;
    unlock_operation_metadata();
    return STORAGE_OPERATION_LOCK_ACQUIRED;
}

bool release_storage_operation(StorageOperationLease& lease)
{
    if (lease.ownerToken == 0) return false;
    lock_operation_metadata();
    if (!s_operationHeld || s_operationOwner != lease.ownerToken ||
        s_operationExecuting) {
        unlock_operation_metadata();
        return false;
    }
    const bool unpin = s_operationTargetPinned;
    const uint8_t pinnedIndex = s_pinnedTargetIndex;
    const uint64_t registrationId = s_pinnedTargetRegistrationId;
    s_operationTargetPinned = false;
    s_pinnedTargetIndex = 0xFFu;
    s_pinnedTargetRegistrationId = 0;
    s_operationHeld = false;
    s_operationExecuting = false;
    s_operationOwner = 0;
    if (s_activePlanValid && s_activePlan.ownerToken == lease.ownerToken)
        s_activePlanValid = false;
    lease.ownerToken = 0;
    lease.targetPinned = false;
    lease.pinnedIndex = 0xFFu;
    lease.pinnedRegistrationId = 0;
    unlock_operation_metadata();
    if (unpin) block::unpin_device(pinnedIndex, registrationId);
    return true;
}

bool begin_storage_operation_execution(const StorageOperationLease& lease)
{
    if (lease.ownerToken == 0) return false;
    lock_operation_metadata();
    if (!s_operationHeld || s_operationOwner != lease.ownerToken ||
        s_operationExecuting) {
        unlock_operation_metadata();
        return false;
    }
    s_operationExecuting = true;
    unlock_operation_metadata();
    return true;
}

bool complete_storage_operation_execution(StorageOperationLease& lease)
{
    if (lease.ownerToken == 0) return false;
    lock_operation_metadata();
    if (!s_operationHeld || s_operationOwner != lease.ownerToken ||
        !s_operationExecuting) {
        unlock_operation_metadata();
        return false;
    }
    const bool unpin = s_operationTargetPinned;
    const uint8_t pinnedIndex = s_pinnedTargetIndex;
    const uint64_t registrationId = s_pinnedTargetRegistrationId;
    s_operationTargetPinned = false;
    s_pinnedTargetIndex = 0xFFu;
    s_pinnedTargetRegistrationId = 0;
    s_operationHeld = false;
    s_operationExecuting = false;
    s_operationOwner = 0;
    if (s_activePlanValid && s_activePlan.ownerToken == lease.ownerToken)
        s_activePlanValid = false;
    lease.ownerToken = 0;
    lease.targetPinned = false;
    lease.pinnedIndex = 0xFFu;
    lease.pinnedRegistrationId = 0;
    unlock_operation_metadata();
    if (unpin) block::unpin_device(pinnedIndex, registrationId);
    return true;
}

bool storage_operation_lease_is_current(const StorageOperationLease& lease)
{
    if (lease.ownerToken == 0) return false;
    lock_operation_metadata();
    const bool current = s_operationHeld && s_operationOwner == lease.ownerToken;
    unlock_operation_metadata();
    return current;
}

bool pin_storage_operation_target(StorageOperationLease& lease,
                                  const TargetIdentity& target)
{
    if (!storage_operation_lease_is_current(lease) ||
        target.registrationId == 0) return false;
    if (lease.targetPinned)
        return lease.pinnedIndex == target.globalIndex &&
               lease.pinnedRegistrationId == target.registrationId;
    if (!block::pin_device(target.globalIndex, target.registrationId)) return false;

    lock_operation_metadata();
    const bool valid = s_operationHeld && s_operationOwner == lease.ownerToken &&
        !s_operationTargetPinned;
    if (valid) {
        s_operationTargetPinned = true;
        s_pinnedTargetIndex = target.globalIndex;
        s_pinnedTargetRegistrationId = target.registrationId;
        lease.targetPinned = true;
        lease.pinnedIndex = target.globalIndex;
        lease.pinnedRegistrationId = target.registrationId;
    }
    unlock_operation_metadata();
    if (!valid) block::unpin_device(target.globalIndex, target.registrationId);
    return valid;
}

bool storage_operation_active()
{
    lock_operation_metadata();
    const bool active = s_operationHeld;
    unlock_operation_metadata();
    return active;
}

InitializeDiskStatus validate_initialize_target(
    const TargetIdentity& target, PartitionScheme scheme,
    const StorageOperationLease& lease,
    InitializeTargetValidation& validation)
{
    return validate_target_impl(target, scheme, lease, validation);
}

InitializeDiskStatus probe_initialize_target(
    const TargetIdentity& target, PartitionScheme scheme,
    InitializeTargetValidation& validation)
{
    StorageOperationLease lease = {};
    if (try_acquire_storage_operation(lease) != STORAGE_OPERATION_LOCK_ACQUIRED) {
        reset_validation(validation);
        validation.status = INITIALIZE_DISK_OPERATION_BUSY;
        return validation.status;
    }
    if (!pin_storage_operation_target(lease, target)) {
        reset_validation(validation);
        validation.status = map_identity_status(revalidate_target_identity(target));
        if (validation.status == INITIALIZE_DISK_SUCCESS)
            validation.status = INITIALIZE_DISK_IDENTITY_CHANGED;
        release_storage_operation(lease);
        return validation.status;
    }
    InitializeDiskStatus status = validate_target_impl(target, scheme, lease,
                                                        validation);
    if (status == INITIALIZE_DISK_READY_FOR_CONFIRMATION) {
        InitializeDiskPlan plan;
        clear_bytes(&plan, sizeof(plan));
        plan.targetSnapshot = target;
        plan.requestedScheme = scheme;
        plan.expectedRegistryGeneration = target.registryGeneration;
        plan.totalLogicalSectors = validation.totalLogicalSectors;
        plan.logicalSectorSize = validation.logicalSectorSize;
        plan.firstUsableLba = validation.firstUsableLba;
        plan.lastUsableLba = validation.lastUsableLba;
        plan.entryArraySectors = validation.entryArraySectors;
        if (!capture_metadata_snapshot(plan))
            status = validation.status = INITIALIZE_DISK_READ_UNAVAILABLE;
        else if (!metadata_snapshot_is_clear(plan))
            status = validation.status = INITIALIZE_DISK_METADATA_NOT_CLEAR;
        else if (revalidate_target_identity(target) != TARGET_VALID)
            status = validation.status = map_identity_status(
                revalidate_target_identity(target));
        clear_bytes(&s_snapshot, sizeof(s_snapshot));
    }
    release_storage_operation(lease);
    return status;
}

InitializeDiskStatus prepare_initialize_disk(
    const InitializeDiskRequest& request, InitializeDiskPlan& plan,
    InitializeDiskResult& result)
{
    clear_bytes(&plan, sizeof(plan));
    reset_result(result);
    result.targetIdentity = request.targetSnapshot;
    result.requestedScheme = request.requestedScheme;
    result.stage = INITIALIZE_STAGE_VALIDATING;

    if (request.expectedRegistryGeneration !=
            request.targetSnapshot.registryGeneration ||
        (request.requestedScheme != PARTITION_SCHEME_GPT &&
         request.requestedScheme != PARTITION_SCHEME_MBR) ||
        request.mbrSignaturePolicy != MBR_SIGNATURE_RANDOM_NONZERO ||
        request.diskGuidSource != DISK_GUID_SECURE_RANDOM) {
        result.status = INITIALIZE_DISK_INVALID_REQUEST;
        result.stage = INITIALIZE_STAGE_FAILED;
        set_diagnostic(result, "The initialization request is invalid.");
        return result.status;
    }

    StorageOperationLease lease = {};
    const StorageOperationLockStatus lockStatus =
        try_acquire_storage_operation(lease);
    if (lockStatus != STORAGE_OPERATION_LOCK_ACQUIRED) {
        result.status = lockStatus == STORAGE_OPERATION_LOCK_BUSY
            ? INITIALIZE_DISK_OPERATION_BUSY
            : INITIALIZE_DISK_OPERATION_OWNERSHIP_INVALID;
        result.stage = INITIALIZE_STAGE_FAILED;
        set_diagnostic(result, initialize_disk_status_name(result.status));
        return result.status;
    }

    if (!pin_storage_operation_target(lease, request.targetSnapshot)) {
        result.status = map_identity_status(
            revalidate_target_identity(request.targetSnapshot));
        if (result.status == INITIALIZE_DISK_SUCCESS)
            result.status = INITIALIZE_DISK_IDENTITY_CHANGED;
        result.stage = INITIALIZE_STAGE_FAILED;
        set_diagnostic(result, initialize_disk_status_name(result.status));
        release_storage_operation(lease);
        return result.status;
    }

    InitializeTargetValidation validation;
    const InitializeDiskStatus validationStatus = validate_target_impl(
        request.targetSnapshot, request.requestedScheme, lease, validation);
    result.finalDetectedState = validation.detectedState;
    if (validationStatus != INITIALIZE_DISK_READY_FOR_CONFIRMATION) {
        result.status = validationStatus;
        result.stage = INITIALIZE_STAGE_FAILED;
        set_diagnostic(result, initialize_disk_status_name(validationStatus));
        release_storage_operation(lease);
        return result.status;
    }

    plan.ownerToken = lease.ownerToken;
    plan.targetSnapshot = request.targetSnapshot;
    plan.requestedScheme = request.requestedScheme;
    plan.expectedRegistryGeneration = request.expectedRegistryGeneration;
    plan.totalLogicalSectors = validation.totalLogicalSectors;
    plan.logicalSectorSize = validation.logicalSectorSize;
    plan.firstUsableLba = validation.firstUsableLba;
    plan.lastUsableLba = validation.lastUsableLba;
    plan.entryArraySectors = validation.entryArraySectors;

    if (!capture_metadata_snapshot(plan)) {
        result.status = INITIALIZE_DISK_READ_UNAVAILABLE;
        result.stage = INITIALIZE_STAGE_FAILED;
        set_diagnostic(result, initialize_disk_status_name(result.status));
        release_storage_operation(lease);
        plan.ownerToken = 0;
        clear_bytes(&s_snapshot, sizeof(s_snapshot));
        return result.status;
    }
    if (!metadata_snapshot_is_clear(plan)) {
        result.status = INITIALIZE_DISK_METADATA_NOT_CLEAR;
        result.stage = INITIALIZE_STAGE_FAILED;
        set_diagnostic(result, initialize_disk_status_name(result.status));
        release_storage_operation(lease);
        plan.ownerToken = 0;
        clear_bytes(&s_snapshot, sizeof(s_snapshot));
        return result.status;
    }

    bool identityValid = revalidate_target_identity(request.targetSnapshot) == TARGET_VALID;
    if (!identityValid) {
        result.status = map_identity_status(
            revalidate_target_identity(request.targetSnapshot));
        result.stage = INITIALIZE_STAGE_FAILED;
        set_diagnostic(result, initialize_disk_status_name(result.status));
        release_storage_operation(lease);
        plan.ownerToken = 0;
        clear_bytes(&s_snapshot, sizeof(s_snapshot));
        return result.status;
    }
    const InitializeDiskStatus finalValidation = validate_target_impl(
        request.targetSnapshot, request.requestedScheme, lease, validation);
    result.finalDetectedState = validation.detectedState;
    if (finalValidation != INITIALIZE_DISK_READY_FOR_CONFIRMATION ||
        !metadata_matches_snapshot(plan)) {
        result.status = finalValidation == INITIALIZE_DISK_READY_FOR_CONFIRMATION
            ? INITIALIZE_DISK_METADATA_NOT_CLEAR : finalValidation;
        result.stage = INITIALIZE_STAGE_FAILED;
        set_diagnostic(result, initialize_disk_status_name(result.status));
        release_storage_operation(lease);
        plan.ownerToken = 0;
        clear_bytes(&s_snapshot, sizeof(s_snapshot));
        return result.status;
    }

    if (request.requestedScheme == PARTITION_SCHEME_GPT) {
        if (!make_disk_guid(plan.diskGuid, request)) {
            result.status = INITIALIZE_DISK_ENTROPY_UNAVAILABLE;
            result.stage = INITIALIZE_STAGE_FAILED;
            set_diagnostic(result, initialize_disk_status_name(result.status));
            release_storage_operation(lease);
            plan.ownerToken = 0;
            clear_bytes(&s_snapshot, sizeof(s_snapshot));
            return result.status;
        }
    } else if (!make_mbr_signature(plan.mbrDiskSignature, request)) {
        result.status = INITIALIZE_DISK_ENTROPY_UNAVAILABLE;
        result.stage = INITIALIZE_STAGE_FAILED;
        set_diagnostic(result, initialize_disk_status_name(result.status));
        release_storage_operation(lease);
        plan.ownerToken = 0;
        clear_bytes(&s_snapshot, sizeof(s_snapshot));
        return result.status;
    }

    plan.confirmationReady = true;
    if (!publish_active_plan(plan)) {
        result.status = INITIALIZE_DISK_OPERATION_OWNERSHIP_INVALID;
        result.stage = INITIALIZE_STAGE_FAILED;
        set_diagnostic(result, initialize_disk_status_name(result.status));
        release_storage_operation(lease);
        plan.ownerToken = 0;
        plan.confirmationReady = false;
        clear_bytes(&s_snapshot, sizeof(s_snapshot));
        return result.status;
    }
    result.status = INITIALIZE_DISK_READY_FOR_CONFIRMATION;
    result.stage = INITIALIZE_STAGE_WAITING_FOR_CONFIRMATION;
    set_diagnostic(result, "Safety checks passed. Confirm to write partition-table metadata.");
    return result.status;
}

bool cancel_initialize_disk(InitializeDiskPlan& plan)
{
    StorageOperationLease lease = {};
    lease.ownerToken = plan.ownerToken;
    const bool released = release_storage_operation(lease);
    if (released) {
        plan.ownerToken = 0;
        plan.confirmationReady = false;
        clear_bytes(&s_snapshot, sizeof(s_snapshot));
    }
    return released;
}

InitializeDiskStatus execute_initialize_disk(
    InitializeDiskPlan& plan, InitializeDiskResult& result)
{
    reset_result(result);
    result.targetIdentity = plan.targetSnapshot;
    result.requestedScheme = plan.requestedScheme;
    InitializeDiskPlan confirmedPlan;
    const bool haveConfirmedPlan = get_active_plan(confirmedPlan);
    const bool planMatchesConfirmation = haveConfirmedPlan &&
        initialize_plans_equal(plan, confirmedPlan);
    if (!plan.confirmationReady || plan.ownerToken == 0 || !s_snapshot.valid ||
        !planMatchesConfirmation) {
        result.status = INITIALIZE_DISK_OPERATION_OWNERSHIP_INVALID;
        result.stage = INITIALIZE_STAGE_FAILED;
        if (haveConfirmedPlan) {
            result.targetIdentity = confirmedPlan.targetSnapshot;
            result.requestedScheme = confirmedPlan.requestedScheme;
        }
        set_diagnostic(result, initialize_disk_status_name(result.status));
        if (haveConfirmedPlan && plan.ownerToken == confirmedPlan.ownerToken) {
            StorageOperationLease invalidPlanLease = {};
            invalidPlanLease.ownerToken = confirmedPlan.ownerToken;
            if (release_storage_operation(invalidPlanLease)) {
                plan.ownerToken = 0;
                plan.confirmationReady = false;
                clear_bytes(&s_snapshot, sizeof(s_snapshot));
            }
        }
        return result.status;
    }
    StorageOperationLease lease = {};
    lease.ownerToken = plan.ownerToken;
    if (!storage_operation_lease_is_current(lease)) {
        result.status = INITIALIZE_DISK_OPERATION_OWNERSHIP_INVALID;
        result.stage = INITIALIZE_STAGE_FAILED;
        set_diagnostic(result, initialize_disk_status_name(result.status));
        return result.status;
    }
    if (!begin_storage_operation_execution(lease)) {
        result.status = INITIALIZE_DISK_OPERATION_OWNERSHIP_INVALID;
        result.stage = INITIALIZE_STAGE_FAILED;
        set_diagnostic(result, initialize_disk_status_name(result.status));
        return result.status;
    }

    result.stage = INITIALIZE_STAGE_REVALIDATING;
    if (plan.expectedRegistryGeneration != plan.targetSnapshot.registryGeneration) {
        finish_failure(plan, result, INITIALIZE_DISK_REGISTRY_CHANGED,
                       initialize_disk_status_name(INITIALIZE_DISK_REGISTRY_CHANGED));
        return result.status;
    }
    InitializeTargetValidation validation;
    InitializeDiskStatus status = validate_target_impl(
        plan.targetSnapshot, plan.requestedScheme, lease, validation);
    result.finalDetectedState = validation.detectedState;
    if (status != INITIALIZE_DISK_READY_FOR_CONFIRMATION) {
        finish_failure(plan, result, status, initialize_disk_status_name(status));
        return result.status;
    }
    if (!metadata_matches_snapshot(plan) || !metadata_snapshot_is_clear(plan)) {
        finish_failure(plan, result, INITIALIZE_DISK_METADATA_NOT_CLEAR,
                       initialize_disk_status_name(INITIALIZE_DISK_METADATA_NOT_CLEAR));
        return result.status;
    }

    const block::FlushReport initialFlush =
        block::flush_with_result(plan.targetSnapshot.globalIndex);
    if (!flush_is_proven(initialFlush)) {
        result.flushOutcome = initialFlush.outcome;
        result.flushStatus = initialFlush.status;
        finish_failure(plan, result, INITIALIZE_DISK_FLUSH_FAILED,
                       initialize_disk_status_name(INITIALIZE_DISK_FLUSH_FAILED));
        return result.status;
    }
    if (!metadata_matches_snapshot(plan) ||
        revalidate_target_identity(plan.targetSnapshot) != TARGET_VALID) {
        status = revalidate_target_identity(plan.targetSnapshot) == TARGET_VALID
            ? INITIALIZE_DISK_METADATA_NOT_CLEAR
            : map_identity_status(revalidate_target_identity(plan.targetSnapshot));
        finish_failure(plan, result, status, initialize_disk_status_name(status));
        return result.status;
    }

    bool writesOk = false;
    if (plan.requestedScheme == PARTITION_SCHEME_GPT)
        writesOk = write_gpt(plan, result);
    else
        writesOk = write_mbr(plan, result);
    if (!writesOk) {
        const InitializeDiskStatus failure = result.status == INITIALIZE_DISK_INVALID_REQUEST
            ? INITIALIZE_DISK_IO_FAILED : result.status;
        finish_failure(plan, result, failure, initialize_disk_status_name(failure));
        return result.status;
    }

    result.stage = INITIALIZE_STAGE_FLUSHING;
    const block::FlushReport writeFlush =
        block::flush_with_result(plan.targetSnapshot.globalIndex);
    result.flushOutcome = writeFlush.outcome;
    result.flushStatus = writeFlush.status;
    if (!flush_is_proven(writeFlush)) {
        finish_failure(plan, result, INITIALIZE_DISK_FLUSH_FAILED,
                       initialize_disk_status_name(INITIALIZE_DISK_FLUSH_FAILED));
        return result.status;
    }

    result.stage = INITIALIZE_STAGE_VERIFYING;
    DiskState detected = DISK_STATE_UNREADABLE;
    if (!verify_partition_state(plan, detected)) {
        result.finalDetectedState = detected;
        finish_failure(plan, result, INITIALIZE_DISK_VERIFICATION_FAILED,
                       initialize_disk_status_name(INITIALIZE_DISK_VERIFICATION_FAILED));
        return result.status;
    }
    result.verificationPassed = true;

    result.stage = INITIALIZE_STAGE_RESCANNING;
    detected = DISK_STATE_UNREADABLE;
    if (!verify_partition_state(plan, detected)) {
        result.finalDetectedState = detected;
        finish_failure(plan, result, INITIALIZE_DISK_RESCAN_FAILED,
                       initialize_disk_status_name(INITIALIZE_DISK_RESCAN_FAILED));
        return result.status;
    }
    result.finalDetectedState = detected;
    const RevalidationStatus afterScan = revalidate_target_identity(plan.targetSnapshot);
    if (afterScan != TARGET_VALID) {
        status = map_identity_status(afterScan);
        finish_failure(plan, result, status, initialize_disk_status_name(status));
        result.finalStateUncertain = true;
        if (result.rollbackSucceeded) result.rollbackSucceeded = false;
        return result.status;
    }

    result.status = INITIALIZE_DISK_SUCCESS;
    result.stage = INITIALIZE_STAGE_COMPLETED;
    result.verificationPassed = true;
    set_diagnostic(result, plan.requestedScheme == PARTITION_SCHEME_GPT
        ? "GPT initialized and verified; the disk has no partitions."
        : "MBR initialized and verified; the disk has no partitions.");
    StorageOperationLease releaseLease = {};
    releaseLease.ownerToken = plan.ownerToken;
    complete_storage_operation_execution(releaseLease);
    plan.ownerToken = 0;
    plan.confirmationReady = false;
    clear_bytes(&s_snapshot, sizeof(s_snapshot));
    return result.status;
}

const char* initialize_disk_status_name(InitializeDiskStatus status)
{
    switch (status) {
        case INITIALIZE_DISK_READY_FOR_CONFIRMATION: return "Ready for confirmation";
        case INITIALIZE_DISK_SUCCESS: return "Initialization completed";
        case INITIALIZE_DISK_OPERATION_BUSY: return "Another storage operation is active";
        case INITIALIZE_DISK_INVALID_REQUEST: return "The initialization request is invalid";
        case INITIALIZE_DISK_DEVICE_MISSING: return "The device is no longer present";
        case INITIALIZE_DISK_REGISTRY_CHANGED: return "Disk identity changed; refresh and try again";
        case INITIALIZE_DISK_IDENTITY_CHANGED: return "Disk identity changed; refresh and try again";
        case INITIALIZE_DISK_INVALID_GEOMETRY: return "Disk geometry is invalid";
        case INITIALIZE_DISK_READ_UNAVAILABLE: return "Disk reads are unavailable";
        case INITIALIZE_DISK_WRITE_UNAVAILABLE: return "A logical sector cannot be written by this device";
        case INITIALIZE_DISK_READ_ONLY: return "The device is read-only";
        case INITIALIZE_DISK_DURABILITY_UNKNOWN: return "Durable writes are unavailable for this device";
        case INITIALIZE_DISK_FLUSH_UNAVAILABLE: return "A trusted flush is unavailable for this device";
        case INITIALIZE_DISK_MOUNTED: return "Disk is currently mounted";
        case INITIALIZE_DISK_ROOT_BACKING: return "Disk backs the root filesystem";
        case INITIALIZE_DISK_BOOT_BACKING: return "Disk is a firmware boot source";
        case INITIALIZE_DISK_BOOT_IDENTITY_UNKNOWN: return "Boot-device identity cannot be safely excluded";
        case INITIALIZE_DISK_NOT_RAW: return "Disk contains a recognized or invalid partition table";
        case INITIALIZE_DISK_UNSUPPORTED_SECTOR_SIZE: return "Logical sector geometry is unsupported";
        case INITIALIZE_DISK_TOO_SMALL: return "Disk is too small for the requested scheme";
        case INITIALIZE_DISK_MBR_CAPACITY_LIMIT: return "Disk exceeds the MBR 32-bit LBA limit";
        case INITIALIZE_DISK_METADATA_NOT_CLEAR: return "Initialization metadata sectors are not clear";
        case INITIALIZE_DISK_ENTROPY_UNAVAILABLE: return "Secure random data is unavailable";
        case INITIALIZE_DISK_OPERATION_OWNERSHIP_INVALID: return "Storage operation ownership is invalid";
        case INITIALIZE_DISK_IO_FAILED: return "A metadata write failed";
        case INITIALIZE_DISK_FLUSH_FAILED: return "Durable flush failed";
        case INITIALIZE_DISK_VERIFICATION_FAILED: return "Read-back partition-table verification failed";
        case INITIALIZE_DISK_RESCAN_FAILED: return "The final disk rescan failed";
        case INITIALIZE_DISK_ROLLBACK_FAILED: return "Rollback could not be verified; disk state is uncertain";
        default: return "Initialization failed";
    }
}

const char* initialize_disk_stage_name(InitializeDiskStage stage)
{
    switch (stage) {
        case INITIALIZE_STAGE_VALIDATING: return "Validating";
        case INITIALIZE_STAGE_WAITING_FOR_CONFIRMATION: return "Waiting for confirmation";
        case INITIALIZE_STAGE_REVALIDATING: return "Revalidating";
        case INITIALIZE_STAGE_WRITING_GPT_METADATA: return "Writing GPT metadata";
        case INITIALIZE_STAGE_WRITING_MBR: return "Writing MBR";
        case INITIALIZE_STAGE_FLUSHING: return "Flushing";
        case INITIALIZE_STAGE_VERIFYING: return "Verifying";
        case INITIALIZE_STAGE_RESCANNING: return "Rescanning";
        case INITIALIZE_STAGE_COMPLETED: return "Completed";
        case INITIALIZE_STAGE_FAILED: return "Failed";
        case INITIALIZE_STAGE_IDLE: default: return "Idle";
    }
}

} // namespace storage
} // namespace kernel
