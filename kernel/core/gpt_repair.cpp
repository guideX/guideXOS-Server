#include "include/kernel/gpt_repair.h"
#include "include/kernel/block_device.h"

namespace kernel {
namespace storage {

namespace {

static const uint8_t GPT_SIGNATURE[8] = {'E','F','I',' ','P','A','R','T'};
static const uint32_t GPT_HEADER_MINIMUM_SIZE = 92;
static const uint64_t FNV64_OFFSET = 14695981039346656037ull;
static const uint64_t FNV64_PRIME = 1099511628211ull;

alignas(4096) static PartitionTableModel s_table;
alignas(4096) static PartitionTableModel s_beforeTable;
alignas(4096) static uint8_t s_authoritativeArray[GPT_MAX_ARRAY_BYTES];
alignas(4096) static uint8_t s_authoritativeHeader[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_repairedHeader[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_ioSector[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_verifySector[MAX_LOGICAL_SECTOR_SIZE];
alignas(4096) static uint8_t s_protectiveMbr[MAX_LOGICAL_SECTOR_SIZE];
static GptRepairPlan s_activePlan;
static bool s_activePlanValid = false;

static void clear_bytes(void* out, size_t count)
{
    uint8_t* p = static_cast<uint8_t*>(out);
    for (size_t i = 0; i < count; ++i) p[i] = 0;
}

static void copy_bytes(void* out, const void* in, size_t count)
{
    uint8_t* d = static_cast<uint8_t*>(out);
    const uint8_t* s = static_cast<const uint8_t*>(in);
    for (size_t i = 0; i < count; ++i) d[i] = s[i];
}

static bool bytes_equal(const uint8_t* a, const uint8_t* b, size_t count)
{
    for (size_t i = 0; i < count; ++i) if (a[i] != b[i]) return false;
    return true;
}

static uint32_t read_u32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
}

static uint64_t read_u64(const uint8_t* p)
{
    return static_cast<uint64_t>(read_u32(p)) |
        (static_cast<uint64_t>(read_u32(p + 4)) << 32);
}

static void write_u32(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

static void write_u64(uint8_t* p, uint64_t value)
{
    write_u32(p, static_cast<uint32_t>(value));
    write_u32(p + 4, static_cast<uint32_t>(value >> 32));
}

static void set_diagnostic(GptRepairResult& result, const char* message)
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

static void reset_result(GptRepairResult& result)
{
    clear_bytes(&result, sizeof(result));
    result.status = GPT_REPAIR_INVALID_REQUEST;
    result.failureStatus = GPT_REPAIR_INVALID_REQUEST;
    result.flushOutcome = block::FLUSH_OUTCOME_INVALID;
    result.flushStatus = block::BLOCK_ERR_INVALID;
    result.finalDetectedState = DISK_STATE_UNREADABLE;
}

static GptRepairStatus identity_status(RevalidationStatus status)
{
    switch (status) {
        case TARGET_REGISTRY_CHANGED: return GPT_REPAIR_REGISTRY_CHANGED;
        case TARGET_DEVICE_MISSING: return GPT_REPAIR_DEVICE_MISSING;
        case TARGET_IDENTITY_MISMATCH: return GPT_REPAIR_IDENTITY_CHANGED;
        case TARGET_VALID: default: return GPT_REPAIR_SUCCESS;
    }
}

static uint32_t max_transfer_sectors(const DeviceCapabilities& capabilities,
                                    uint32_t sectorSize)
{
    if (capabilities.maxTransferBytes == 0) return 1;
    uint32_t count = capabilities.maxTransferBytes / sectorSize;
    return count == 0 ? 1 : count;
}

static block::Status read_range(const TargetIdentity& target,
                                const DeviceCapabilities& capabilities,
                                uint64_t lba, uint32_t count, void* buffer,
                                size_t bufferBytes, GptRepairResult* metrics)
{
    if (!count || count > SIZE_MAX / capabilities.logicalSectorSize ||
        bufferBytes < static_cast<size_t>(count) * capabilities.logicalSectorSize)
        return block::BLOCK_ERR_INVALID;
    const uint32_t limit = max_transfer_sectors(capabilities,
                                                capabilities.logicalSectorSize);
    uint8_t* bytes = static_cast<uint8_t*>(buffer);
    uint32_t completed = 0;
    while (completed < count) {
        if (revalidate_target_identity(target) != TARGET_VALID ||
            !block::registration_is_present(target.globalIndex,
                                             target.registrationId))
            return block::BLOCK_ERR_NO_MEDIA;
        const uint32_t chunk = count - completed < limit
            ? count - completed : limit;
        const size_t chunkBytes = static_cast<size_t>(chunk) *
                                  capabilities.logicalSectorSize;
        const block::Status status = read_sectors_safe(
            target.globalIndex, lba + completed, chunk,
            bytes + static_cast<size_t>(completed) * capabilities.logicalSectorSize,
            chunkBytes);
        if (status != block::BLOCK_OK) return status;
        if (metrics) metrics->logicalSectorsRead += chunk;
        completed += chunk;
    }
    return block::BLOCK_OK;
}

static block::Status write_range(const TargetIdentity& target,
                                 const DeviceCapabilities& capabilities,
                                 uint64_t lba, uint32_t count,
                                 const uint8_t* buffer,
                                 GptRepairResult& result)
{
    if (!count) return block::BLOCK_ERR_INVALID;
    const uint32_t limit = max_transfer_sectors(capabilities,
                                                capabilities.logicalSectorSize);
    uint32_t completed = 0;
    while (completed < count) {
        if (!storage_operation_lease_is_current(s_activePlan.lease) ||
            revalidate_target_identity(target) != TARGET_VALID ||
            !block::registration_is_present(target.globalIndex,
                                             target.registrationId))
            return block::BLOCK_ERR_NO_MEDIA;
        const uint32_t chunk = count - completed < limit
            ? count - completed : limit;
        const size_t chunkBytes = static_cast<size_t>(chunk) *
                                  capabilities.logicalSectorSize;
        result.writeAttempted = true;
        result.writeMayHaveReachedMedia = true;
        const block::Status status = write_sectors_safe(
            target.globalIndex, lba + completed, chunk,
            buffer + static_cast<size_t>(completed) * capabilities.logicalSectorSize,
            chunkBytes);
        if (status != block::BLOCK_OK) return status;
        result.logicalSectorsWritten += chunk;
        result.bytesWritten += chunkBytes;
        completed += chunk;
    }
    return block::BLOCK_OK;
}

static bool header_crc_valid(const uint8_t* header, uint32_t sectorSize)
{
    if (!bytes_equal(header, GPT_SIGNATURE, sizeof(GPT_SIGNATURE))) return false;
    const uint32_t headerSize = read_u32(header + 12);
    if (read_u32(header + 8) != 0x00010000u ||
        headerSize < GPT_HEADER_MINIMUM_SIZE || headerSize > sectorSize)
        return false;
    uint8_t copy[MAX_LOGICAL_SECTOR_SIZE];
    copy_bytes(copy, header, headerSize);
    const uint32_t expected = read_u32(copy + 16);
    write_u32(copy + 16, 0);
    return crc32(copy, headerSize) == expected;
}

static uint64_t fingerprint_update(uint64_t hash, const uint8_t* bytes,
                                   size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        hash ^= bytes[i];
        hash *= FNV64_PRIME;
    }
    return hash;
}

static uint64_t fingerprint_u64(uint64_t hash, uint64_t value)
{
    uint8_t bytes[8];
    write_u64(bytes, value);
    return fingerprint_update(hash, bytes, sizeof(bytes));
}

static uint64_t source_fingerprint(const TargetIdentity& target,
                                   const DeviceCapabilities& capabilities,
                                   const uint8_t* header,
                                   const uint8_t* array,
                                   uint32_t arraySectors)
{
    uint64_t hash = FNV64_OFFSET;
    hash = fingerprint_u64(hash, capabilities.totalLogicalSectors);
    hash = fingerprint_u64(hash, capabilities.logicalSectorSize);
    hash = fingerprint_u64(hash, target.registrationId);
    hash = fingerprint_update(hash, header, capabilities.logicalSectorSize);
    hash = fingerprint_update(hash, array,
        static_cast<size_t>(arraySectors) * capabilities.logicalSectorSize);
    return hash;
}

static bool partitions_equal(const PartitionTableModel& before,
                             const PartitionTableModel& after)
{
    if (before.partitionCount != after.partitionCount) return false;
    for (uint16_t i = 0; i < before.partitionCount; ++i) {
        const PartitionEntry& a = before.partitions[i];
        const PartitionEntry& b = after.partitions[i];
        if (a.partitionNumber != b.partitionNumber || a.isGpt != b.isGpt ||
            a.startLba != b.startLba || a.endLba != b.endLba ||
            a.sectorCount != b.sectorCount || a.attributes != b.attributes ||
            !bytes_equal(a.typeGuid, b.typeGuid, 16) ||
            !bytes_equal(a.uniqueGuid, b.uniqueGuid, 16))
            return false;
        for (size_t n = 0; n < sizeof(a.name); ++n)
            if (a.name[n] != b.name[n]) return false;
    }
    return true;
}

static bool plans_equal(const GptRepairPlan& a, const GptRepairPlan& b)
{
    return a.lease.ownerToken == b.lease.ownerToken &&
        target_identities_equal(a.targetSnapshot, b.targetSnapshot) &&
        a.expectedRegistryGeneration == b.expectedRegistryGeneration &&
        a.direction == b.direction &&
        a.damagedCopyState == b.damagedCopyState &&
        a.authoritativeFingerprint == b.authoritativeFingerprint &&
        a.totalLogicalSectors == b.totalLogicalSectors &&
        a.logicalSectorSize == b.logicalSectorSize &&
        a.authoritativeEntryArrayLba == b.authoritativeEntryArrayLba &&
        a.damagedEntryArrayLba == b.damagedEntryArrayLba &&
        a.authoritativeHeaderLba == b.authoritativeHeaderLba &&
        a.damagedHeaderLba == b.damagedHeaderLba &&
        a.firstUsableLba == b.firstUsableLba &&
        a.lastUsableLba == b.lastUsableLba &&
        a.partitionCount == b.partitionCount &&
        a.entryCount == b.entryCount && a.entrySize == b.entrySize &&
        a.entryArraySectors == b.entryArraySectors &&
        bytes_equal(a.diskGuid, b.diskGuid, 16) &&
        a.confirmationReady == b.confirmationReady;
}

static void mark_failure(GptRepairResult& result, GptRepairStatus status,
                         const char* diagnostic)
{
    result.lastStage = result.stage;
    if (result.firstFailedStage == GPT_REPAIR_STAGE_IDLE)
        result.firstFailedStage = result.lastStage;
    result.failureStatus = status;
    result.status = status;
    result.stage = GPT_REPAIR_STAGE_FAILED;
    result.finalStateUncertain = result.writeMayHaveReachedMedia;
    set_diagnostic(result, diagnostic);
}

static GptRepairStatus table_authority_status(PartitionTableModel& table,
                                               GptRepairDirection& direction,
                                               GptCopyState& damagedState)
{
    direction = GPT_REPAIR_NONE;
    damagedState = GPT_COPY_NOT_PRESENT;
    if (table.gptCopiesConflict)
        return GPT_REPAIR_CONFLICT;
    if (table.primaryGptValid && table.backupGptValid &&
        !table.gptCopiesAgree)
        return GPT_REPAIR_UNREADABLE_COPY;
    if (table.primaryGptCopyState == GPT_COPY_GEOMETRY_MISMATCH ||
        table.backupGptCopyState == GPT_COPY_GEOMETRY_MISMATCH)
        return GPT_REPAIR_GEOMETRY_MISMATCH;
    if (table.primaryGptCopyState == GPT_COPY_UNSUPPORTED ||
        table.backupGptCopyState == GPT_COPY_UNSUPPORTED)
        return GPT_REPAIR_UNSUPPORTED_COPY;
    if (table.primaryGptCopyState == GPT_COPY_UNREADABLE ||
        table.backupGptCopyState == GPT_COPY_UNREADABLE)
        return GPT_REPAIR_UNREADABLE_COPY;
    if (table.primaryGptValid && table.backupGptValid && table.gptCopiesAgree)
        return GPT_REPAIR_GPT_HEALTHY;
    if (table.primaryGptValid == table.backupGptValid)
        return GPT_REPAIR_NO_AUTHORITATIVE_COPY;

    const GptCopyState other = table.primaryGptValid
        ? table.backupGptCopyState : table.primaryGptCopyState;
    switch (other) {
        case GPT_COPY_HEADER_INVALID:
        case GPT_COPY_HEADER_CRC_INVALID:
        case GPT_COPY_ARRAY_CRC_INVALID:
        case GPT_COPY_NOT_PRESENT:
            break;
        case GPT_COPY_GEOMETRY_MISMATCH:
            return GPT_REPAIR_GEOMETRY_MISMATCH;
        case GPT_COPY_UNSUPPORTED:
            return GPT_REPAIR_UNSUPPORTED_COPY;
        case GPT_COPY_UNREADABLE:
            return GPT_REPAIR_UNREADABLE_COPY;
        case GPT_COPY_BOUNDS_INVALID:
        case GPT_COPY_VALID:
        default:
            return GPT_REPAIR_NO_AUTHORITATIVE_COPY;
    }
    direction = table.primaryGptValid ? GPT_REPAIR_BACKUP_FROM_PRIMARY
                                      : GPT_REPAIR_PRIMARY_FROM_BACKUP;
    damagedState = other;
    return GPT_REPAIR_SUCCESS;
}

static GptRepairStatus check_target_policy(const TargetIdentity& target,
                                           DeviceCapabilities& capabilities,
                                           GptRepairResult& result)
{
    const RevalidationStatus identity = revalidate_target_identity(target);
    if (identity != TARGET_VALID) return identity_status(identity);
    if (!query_device_capabilities(target.globalIndex, capabilities))
        return GPT_REPAIR_DEVICE_MISSING;
    if (!capabilities.geometryValid || !capabilities.capacityValid ||
        capabilities.totalLogicalSectors != target.totalLogicalSectors ||
        capabilities.logicalSectorSize != target.logicalSectorSize ||
        (capabilities.logicalSectorSize != 512 &&
         capabilities.logicalSectorSize != 4096) ||
        capabilities.totalLogicalSectors < 100)
        return GPT_REPAIR_INVALID_GEOMETRY;
    if (!capabilities.readable) return GPT_REPAIR_READ_UNAVAILABLE;
    if (!capabilities.writable) return GPT_REPAIR_READ_ONLY;
    if (capabilities.persistence == PERSISTENCE_UNKNOWN ||
        capabilities.persistence == PERSISTENCE_VOLATILE_MEMORY)
        return GPT_REPAIR_DURABILITY_UNKNOWN;
    if (capabilities.persistence == PERSISTENCE_FLUSH_REQUIRED &&
        (!capabilities.flushSupported || !capabilities.flushSemanticsKnown))
        return GPT_REPAIR_FLUSH_UNAVAILABLE;

    const MountProtection mount = query_mount_protection(target);
    if (mount.safety == DEVICE_ROOT_BACKING) return GPT_REPAIR_ROOT_BACKING;
    if (mount.safety == DEVICE_MOUNTED) return GPT_REPAIR_MOUNTED;
    if (mount.safety != DEVICE_UNMOUNTED) return GPT_REPAIR_MOUNT_STATE_UNKNOWN;
    const BootProtection boot = query_boot_protection(target);
    if (boot.safety == BOOT_DEVICE_IS_TARGET) return GPT_REPAIR_BOOT_BACKING;
    if (boot.safety != BOOT_DEVICE_DEFINITELY_NOT_TARGET)
        return GPT_REPAIR_BOOT_IDENTITY_UNKNOWN;
    result.targetIdentity = target;
    return GPT_REPAIR_SUCCESS;
}

static bool read_source_bundle(const TargetIdentity& target,
                               const DeviceCapabilities& capabilities,
                               const PartitionTableModel& table,
                               GptRepairDirection direction,
                               uint32_t arraySectors,
                               GptRepairResult* metrics,
                               uint64_t& fingerprint)
{
    const uint64_t headerLba = direction == GPT_REPAIR_BACKUP_FROM_PRIMARY
        ? 1 : capabilities.totalLogicalSectors - 1;
    const uint64_t arrayLba = direction == GPT_REPAIR_BACKUP_FROM_PRIMARY
        ? table.primaryGptEntryArrayLba : table.backupGptEntryArrayLba;
    if (!arrayLba || !arraySectors) return false;
    if (read_range(target, capabilities, headerLba, 1,
            s_authoritativeHeader, capabilities.logicalSectorSize, metrics) !=
        block::BLOCK_OK)
        return false;
    if (!header_crc_valid(s_authoritativeHeader,
                          capabilities.logicalSectorSize)) return false;
    const uint8_t* header = s_authoritativeHeader;
    const uint64_t expectedCurrent = headerLba;
    const uint64_t expectedBackup = direction == GPT_REPAIR_BACKUP_FROM_PRIMARY
        ? capabilities.totalLogicalSectors - 1 : 1;
    const uint32_t entryCount = table.tableEntryCount;
    const uint32_t entrySize = table.gptEntrySize;
    const uint64_t arrayBytes = static_cast<uint64_t>(entryCount) * entrySize;
    if (read_u64(header + 24) != expectedCurrent ||
        read_u64(header + 32) != expectedBackup ||
        read_u64(header + 72) != arrayLba ||
        read_u32(header + 80) != entryCount ||
        read_u32(header + 84) != entrySize ||
        read_u64(header + 40) != table.firstUsableLba ||
        read_u64(header + 48) != table.lastUsableLba ||
        !bytes_equal(header + 56, table.primaryGptValid
                ? table.primaryDiskGuid : table.backupDiskGuid, 16) ||
        arrayBytes == 0 || arrayBytes > GPT_MAX_ARRAY_BYTES ||
        static_cast<uint64_t>(arraySectors) * capabilities.logicalSectorSize < arrayBytes)
        return false;
    if (read_range(target, capabilities, arrayLba, arraySectors,
            s_authoritativeArray,
            static_cast<size_t>(arraySectors) * capabilities.logicalSectorSize,
            metrics) != block::BLOCK_OK)
        return false;
    if (crc32(s_authoritativeArray, static_cast<size_t>(arrayBytes)) !=
        read_u32(header + 88)) return false;
    fingerprint = source_fingerprint(target, capabilities, s_authoritativeHeader,
                                     s_authoritativeArray, arraySectors);
    return fingerprint != 0;
}

static GptRepairStatus diagnose(const TargetIdentity& target,
                                DeviceCapabilities& capabilities,
                                GptRepairPlan* plan,
                                GptRepairResult& result)
{
    const GptRepairStatus policy = check_target_policy(target, capabilities, result);
    // Report the GPT condition before capability restrictions so read-only
    // degraded devices retain a useful layout diagnosis.
    if (policy == GPT_REPAIR_DEVICE_MISSING ||
        policy == GPT_REPAIR_REGISTRY_CHANGED ||
        policy == GPT_REPAIR_IDENTITY_CHANGED ||
        policy == GPT_REPAIR_INVALID_GEOMETRY ||
        policy == GPT_REPAIR_READ_UNAVAILABLE)
        return policy;

    if (!parse_partition_table(target.globalIndex, s_table))
        return GPT_REPAIR_UNREADABLE_COPY;
    result.finalDetectedState = s_table.state;
    result.finalPrimaryState = s_table.primaryGptCopyState;
    result.finalBackupState = s_table.backupGptCopyState;
    result.protectiveMbrWasValid = s_table.protectiveMbrValid;
    GptRepairDirection direction = GPT_REPAIR_NONE;
    GptCopyState damagedState = GPT_COPY_NOT_PRESENT;
    const GptRepairStatus authority = table_authority_status(
        s_table, direction, damagedState);
    result.direction = direction;
    result.damagedCopyState = damagedState;
    result.partitionCount = s_table.partitionCount;
    if (authority != GPT_REPAIR_SUCCESS) return authority;

    if (policy != GPT_REPAIR_SUCCESS) return policy;
    const uint64_t arrayBytes = static_cast<uint64_t>(s_table.tableEntryCount) *
                                s_table.gptEntrySize;
    if (!s_table.tableEntryCount || !s_table.gptEntrySize ||
        arrayBytes == 0 || arrayBytes > GPT_MAX_ARRAY_BYTES)
        return GPT_REPAIR_INVALID_GEOMETRY;
    const uint32_t arraySectors = static_cast<uint32_t>(
        (arrayBytes + capabilities.logicalSectorSize - 1) /
        capabilities.logicalSectorSize);
    const uint64_t lastLba = capabilities.totalLogicalSectors - 1;
    if (arraySectors == 0 || arraySectors > GPT_MAX_ARRAY_BYTES /
            capabilities.logicalSectorSize + 1 ||
        lastLba <= arraySectors + 2)
        return GPT_REPAIR_INVALID_GEOMETRY;
    const uint64_t primaryArrayLba = 2;
    const uint64_t backupArrayLba = lastLba - arraySectors;
    if (primaryArrayLba + arraySectors > s_table.firstUsableLba ||
        backupArrayLba <= s_table.lastUsableLba ||
        s_table.firstUsableLba > s_table.lastUsableLba)
        return GPT_REPAIR_GEOMETRY_MISMATCH;

    uint64_t fingerprint = 0;
    if (!read_source_bundle(target, capabilities, s_table, direction,
                            arraySectors, nullptr, fingerprint))
        return GPT_REPAIR_STALE_SNAPSHOT;
    if (plan) {
        clear_bytes(plan, sizeof(*plan));
        plan->targetSnapshot = target;
        plan->expectedRegistryGeneration = target.registryGeneration;
        plan->direction = direction;
        plan->damagedCopyState = damagedState;
        plan->authoritativeFingerprint = fingerprint;
        plan->totalLogicalSectors = capabilities.totalLogicalSectors;
        plan->logicalSectorSize = capabilities.logicalSectorSize;
        plan->authoritativeHeaderLba = direction == GPT_REPAIR_BACKUP_FROM_PRIMARY
            ? 1 : lastLba;
        plan->damagedHeaderLba = direction == GPT_REPAIR_BACKUP_FROM_PRIMARY
            ? lastLba : 1;
        plan->authoritativeEntryArrayLba = direction == GPT_REPAIR_BACKUP_FROM_PRIMARY
            ? s_table.primaryGptEntryArrayLba : s_table.backupGptEntryArrayLba;
        plan->damagedEntryArrayLba = direction == GPT_REPAIR_BACKUP_FROM_PRIMARY
            ? backupArrayLba : primaryArrayLba;
        plan->firstUsableLba = s_table.firstUsableLba;
        plan->lastUsableLba = s_table.lastUsableLba;
        plan->partitionCount = s_table.partitionCount;
        plan->entryCount = s_table.tableEntryCount;
        plan->entrySize = s_table.gptEntrySize;
        plan->entryArraySectors = arraySectors;
        copy_bytes(plan->diskGuid, s_table.primaryGptValid
                ? s_table.primaryDiskGuid : s_table.backupDiskGuid, 16);
        plan->confirmationReady = true;
    }
    return GPT_REPAIR_SUCCESS;
}

static bool target_still_current(const GptRepairPlan& plan)
{
    return storage_operation_lease_is_current(plan.lease) &&
        revalidate_target_identity(plan.targetSnapshot) == TARGET_VALID &&
        block::registration_is_present(plan.targetSnapshot.globalIndex,
                                       plan.targetSnapshot.registrationId);
}

static bool read_copy_sector(const TargetIdentity& target,
                             const DeviceCapabilities& capabilities,
                             uint64_t lba, uint8_t* buffer,
                             GptRepairResult& result)
{
    return read_range(target, capabilities, lba, 1, buffer,
                      capabilities.logicalSectorSize, &result) == block::BLOCK_OK;
}

static bool compare_array_at(const TargetIdentity& target,
                             const DeviceCapabilities& capabilities,
                             uint64_t lba, uint32_t sectors,
                             const uint8_t* expected,
                             GptRepairResult& result)
{
    for (uint32_t i = 0; i < sectors; ++i) {
        if (!read_copy_sector(target, capabilities, lba + i,
                              s_verifySector, result) ||
            !bytes_equal(s_verifySector,
                expected + static_cast<size_t>(i) * capabilities.logicalSectorSize,
                capabilities.logicalSectorSize))
            return false;
    }
    return true;
}

static bool metadata_outside_partitions(const PartitionTableModel& table,
                                        uint64_t arrayLba,
                                        uint32_t arraySectors,
                                        uint64_t headerLba)
{
    const uint64_t arrayEnd = arrayLba + arraySectors;
    for (uint16_t i = 0; i < table.partitionCount; ++i) {
        const PartitionEntry& part = table.partitions[i];
        if ((arrayLba <= part.endLba && arrayEnd - 1 >= part.startLba) ||
            (headerLba >= part.startLba && headerLba <= part.endLba))
            return false;
    }
    return true;
}

static void prepare_destination_header(const GptRepairPlan& plan,
                                      const DeviceCapabilities& capabilities)
{
    copy_bytes(s_repairedHeader, s_authoritativeHeader,
               capabilities.logicalSectorSize);
    const uint64_t peerLba = plan.direction == GPT_REPAIR_BACKUP_FROM_PRIMARY
        ? 1 : plan.totalLogicalSectors - 1;
    write_u64(s_repairedHeader + 24, plan.damagedHeaderLba);
    write_u64(s_repairedHeader + 32, peerLba);
    write_u64(s_repairedHeader + 72, plan.damagedEntryArrayLba);
    write_u32(s_repairedHeader + 88,
        crc32(s_authoritativeArray,
            static_cast<size_t>(plan.entryCount) * plan.entrySize));
    const uint32_t headerSize = read_u32(s_repairedHeader + 12);
    write_u32(s_repairedHeader + 16, 0);
    write_u32(s_repairedHeader + 16, crc32(s_repairedHeader, headerSize));
}

static bool run_flush(const TargetIdentity& target, GptRepairResult& result)
{
    ++result.flushAttempts;
    const block::FlushReport report = block::flush_with_result(target.globalIndex);
    result.flushOutcome = report.outcome;
    result.flushStatus = report.status;
    return report.status == block::BLOCK_OK && report.semanticsKnown &&
        (report.outcome == block::FLUSH_OUTCOME_SUPPORTED_SUCCEEDED ||
         report.outcome == block::FLUSH_OUTCOME_SYNCHRONOUS_DURABLE);
}

static GptRepairStatus finish_execute(GptRepairPlan& plan,
                                      GptRepairResult& result,
                                      GptRepairStatus status,
                                      const char* diagnostic)
{
    if (status == GPT_REPAIR_SUCCESS) {
        result.status = GPT_REPAIR_SUCCESS;
        result.failureStatus = GPT_REPAIR_SUCCESS;
        result.stage = GPT_REPAIR_STAGE_COMPLETED;
        result.lastStage = GPT_REPAIR_STAGE_COMPLETED;
        set_diagnostic(result, diagnostic);
    } else {
        mark_failure(result, status, diagnostic);
    }
    plan.confirmationReady = false;
    s_activePlanValid = false;
    clear_bytes(&s_activePlan, sizeof(s_activePlan));
    complete_storage_operation_execution(plan.lease);
    return result.status;
}

} // namespace

GptRepairStatus probe_gpt_repair(const TargetIdentity& target,
                                 GptRepairResult& result)
{
    reset_result(result);
    if (!target.registrationId || target.globalIndex >= block::MAX_BLOCK_DEVICES) {
        set_diagnostic(result, "Invalid target identity.");
        return result.status = GPT_REPAIR_INVALID_REQUEST;
    }
    DeviceCapabilities capabilities = {};
    const GptRepairStatus status = diagnose(target, capabilities, nullptr, result);
    result.status = status;
    result.failureStatus = status;
    if (status == GPT_REPAIR_SUCCESS)
        set_diagnostic(result, "One GPT copy is authoritative; explicit repair is available.");
    else
        set_diagnostic(result, gpt_repair_status_name(status));
    return status;
}

GptRepairStatus prepare_gpt_repair(const GptRepairRequest& request,
                                   GptRepairPlan& plan,
                                   GptRepairResult& result)
{
    reset_result(result);
    clear_bytes(&plan, sizeof(plan));
    if (!request.targetSnapshot.registrationId ||
        request.expectedRegistryGeneration == 0 ||
        request.expectedRegistryGeneration !=
            request.targetSnapshot.registryGeneration) {
        set_diagnostic(result, "Invalid or stale repair request.");
        return result.status = GPT_REPAIR_INVALID_REQUEST;
    }
    if (s_activePlanValid) {
        result.status = GPT_REPAIR_OPERATION_BUSY;
        result.failureStatus = result.status;
        set_diagnostic(result, "Another GPT repair plan is active.");
        return result.status;
    }
    result.stage = GPT_REPAIR_STAGE_ACQUIRE_LEASE;
    if (try_acquire_storage_operation(plan.lease) !=
        STORAGE_OPERATION_LOCK_ACQUIRED) {
        result.status = GPT_REPAIR_OPERATION_BUSY;
        result.failureStatus = result.status;
        set_diagnostic(result, "Another storage operation is active.");
        return result.status;
    }
    result.stage = GPT_REPAIR_STAGE_PIN_TARGET;
    if (!pin_storage_operation_target(plan.lease, request.targetSnapshot)) {
        release_storage_operation(plan.lease);
        result.status = GPT_REPAIR_DEVICE_MISSING;
        result.failureStatus = result.status;
        set_diagnostic(result, "The exact target registration is no longer available.");
        return result.status;
    }
    result.stage = GPT_REPAIR_STAGE_PREFLIGHT;
    DeviceCapabilities capabilities = {};
    const StorageOperationLease acquiredLease = plan.lease;
    const GptRepairStatus status = diagnose(request.targetSnapshot,
                                             capabilities, &plan, result);
    plan.lease = acquiredLease;
    if (status != GPT_REPAIR_SUCCESS) {
        release_storage_operation(plan.lease);
        plan.confirmationReady = false;
        result.status = status;
        result.failureStatus = status;
        set_diagnostic(result, gpt_repair_status_name(status));
        return status;
    }
    plan.lease.targetPinned = true;
    plan.lease.pinnedIndex = request.targetSnapshot.globalIndex;
    plan.lease.pinnedRegistrationId = request.targetSnapshot.registrationId;
    plan.expectedRegistryGeneration = request.expectedRegistryGeneration;
    result.targetIdentity = request.targetSnapshot;
    result.direction = plan.direction;
    result.damagedCopyState = plan.damagedCopyState;
    result.authoritativeFingerprint = plan.authoritativeFingerprint;
    result.partitionCount = plan.partitionCount;
    result.status = GPT_REPAIR_READY_FOR_CONFIRMATION;
    result.failureStatus = GPT_REPAIR_READY_FOR_CONFIRMATION;
    result.stage = GPT_REPAIR_STAGE_WAITING_FOR_CONFIRMATION;
    result.finalDetectedState = s_table.state;
    result.finalPrimaryState = s_table.primaryGptCopyState;
    result.finalBackupState = s_table.backupGptCopyState;
    s_activePlan = plan;
    s_activePlanValid = true;
    set_diagnostic(result, "Review the exact disk and repair direction before confirming.");
    return result.status;
}

GptRepairStatus execute_gpt_repair(GptRepairPlan& plan,
                                   GptRepairResult& result)
{
    reset_result(result);
    result.targetIdentity = plan.targetSnapshot;
    result.direction = plan.direction;
    result.damagedCopyState = plan.damagedCopyState;
    result.authoritativeFingerprint = plan.authoritativeFingerprint;
    result.partitionCount = plan.partitionCount;
    if (!plan.confirmationReady || !s_activePlanValid ||
        !plans_equal(plan, s_activePlan)) {
        result.status = GPT_REPAIR_OPERATION_OWNERSHIP_INVALID;
        result.failureStatus = result.status;
        set_diagnostic(result, "The confirmed repair plan is no longer current.");
        return result.status;
    }
    if (!begin_storage_operation_execution(plan.lease)) {
        result.status = GPT_REPAIR_OPERATION_OWNERSHIP_INVALID;
        result.failureStatus = result.status;
        set_diagnostic(result, "The storage operation lease is no longer owned.");
        return result.status;
    }

    result.stage = GPT_REPAIR_STAGE_REVALIDATING;
    if (!target_still_current(plan))
        return finish_execute(plan, result, GPT_REPAIR_IDENTITY_CHANGED,
                              "Target identity changed; refresh and diagnose again.");
    DeviceCapabilities capabilities = {};
    GptRepairPlan current = {};
    const GptRepairStatus currentStatus = diagnose(plan.targetSnapshot,
        capabilities, &current, result);
    const bool changedGptState = currentStatus == GPT_REPAIR_GPT_HEALTHY ||
        currentStatus == GPT_REPAIR_NO_AUTHORITATIVE_COPY ||
        currentStatus == GPT_REPAIR_CONFLICT ||
        currentStatus == GPT_REPAIR_GEOMETRY_MISMATCH ||
        currentStatus == GPT_REPAIR_UNSUPPORTED_COPY ||
        currentStatus == GPT_REPAIR_UNREADABLE_COPY ||
        currentStatus == GPT_REPAIR_STALE_SNAPSHOT;
    if (currentStatus != GPT_REPAIR_SUCCESS ||
        current.direction != plan.direction ||
        current.damagedCopyState != plan.damagedCopyState ||
        current.authoritativeFingerprint != plan.authoritativeFingerprint ||
        current.totalLogicalSectors != plan.totalLogicalSectors ||
        current.logicalSectorSize != plan.logicalSectorSize ||
        current.entryCount != plan.entryCount || current.entrySize != plan.entrySize ||
        current.entryArraySectors != plan.entryArraySectors ||
        current.firstUsableLba != plan.firstUsableLba ||
        current.lastUsableLba != plan.lastUsableLba ||
        current.partitionCount != plan.partitionCount ||
        !bytes_equal(current.diskGuid, plan.diskGuid, 16))
        return finish_execute(plan, result,
            (currentStatus == GPT_REPAIR_SUCCESS || changedGptState)
                ? GPT_REPAIR_STALE_SNAPSHOT : currentStatus,
            "GPT metadata changed after diagnosis; refresh before repairing.");
    copy_bytes(&s_beforeTable, &s_table, sizeof(s_beforeTable));

    // Read and retain the exact source header/entry-array bytes after
    // confirmation. The fingerprint above binds them to the prepared plan.
    uint64_t fingerprint = 0;
    result.stage = GPT_REPAIR_STAGE_READING_AUTHORITATIVE_COPY;
    if (!read_source_bundle(plan.targetSnapshot, capabilities, s_table,
            plan.direction, plan.entryArraySectors, &result, fingerprint) ||
        fingerprint != plan.authoritativeFingerprint)
        return finish_execute(plan, result, GPT_REPAIR_STALE_SNAPSHOT,
                              "The authoritative GPT fingerprint changed.");
    if (read_range(plan.targetSnapshot, capabilities, 0, 1, s_protectiveMbr,
            capabilities.logicalSectorSize, &result) != block::BLOCK_OK)
        return finish_execute(plan, result, GPT_REPAIR_IO_FAILED,
                              "Could not snapshot the protective MBR.");
    if (!metadata_outside_partitions(s_table, plan.damagedEntryArrayLba,
            plan.entryArraySectors, plan.damagedHeaderLba))
        return finish_execute(plan, result, GPT_REPAIR_INVALID_GEOMETRY,
                              "The repair target overlaps a partition data range.");

    // Recheck all disk and safety bindings immediately before the first write.
    if (!target_still_current(plan))
        return finish_execute(plan, result, GPT_REPAIR_IDENTITY_CHANGED,
                              "Target identity changed before repair.");
    const GptRepairStatus policy = check_target_policy(plan.targetSnapshot,
                                                       capabilities, result);
    if (policy != GPT_REPAIR_SUCCESS)
        return finish_execute(plan, result, policy,
                              gpt_repair_status_name(policy));
    if (!run_flush(plan.targetSnapshot, result)) {
        result.stage = GPT_REPAIR_STAGE_FLUSH;
        return finish_execute(plan, result, GPT_REPAIR_FLUSH_FAILED,
                              "The pre-write durability check failed.");
    }

    result.stage = GPT_REPAIR_STAGE_WRITING_ARRAY;
    if (write_range(plan.targetSnapshot, capabilities,
            plan.damagedEntryArrayLba, plan.entryArraySectors,
            s_authoritativeArray, result) != block::BLOCK_OK)
        return finish_execute(plan, result, GPT_REPAIR_IO_FAILED,
                              "The damaged GPT entry array write did not complete.");

    result.stage = GPT_REPAIR_STAGE_VERIFYING_ARRAY;
    if (!compare_array_at(plan.targetSnapshot, capabilities,
            plan.damagedEntryArrayLba, plan.entryArraySectors,
            s_authoritativeArray, result))
        return finish_execute(plan, result, GPT_REPAIR_VERIFICATION_FAILED,
                              "The reconstructed entry array did not verify.");

    if (!target_still_current(plan))
        return finish_execute(plan, result, GPT_REPAIR_IDENTITY_CHANGED,
                              "Target or safety state changed before header publication.");
    const GptRepairStatus headerPolicy = check_target_policy(
        plan.targetSnapshot, capabilities, result);
    if (headerPolicy != GPT_REPAIR_SUCCESS)
        return finish_execute(plan, result, headerPolicy,
                              gpt_repair_status_name(headerPolicy));
    prepare_destination_header(plan, capabilities);
    result.stage = GPT_REPAIR_STAGE_WRITING_HEADER;
    if (write_range(plan.targetSnapshot, capabilities, plan.damagedHeaderLba,
            1, s_repairedHeader, result) != block::BLOCK_OK)
        return finish_execute(plan, result, GPT_REPAIR_IO_FAILED,
                              "The reconstructed GPT header write did not complete.");

    result.stage = GPT_REPAIR_STAGE_FLUSH;
    if (!run_flush(plan.targetSnapshot, result))
        return finish_execute(plan, result, GPT_REPAIR_FLUSH_FAILED,
                              "The repaired GPT metadata could not be made durable.");

    result.stage = GPT_REPAIR_STAGE_VERIFYING_BOTH_COPIES;
    if (!read_copy_sector(plan.targetSnapshot, capabilities,
            plan.damagedHeaderLba, s_ioSector, result) ||
        !bytes_equal(s_ioSector, s_repairedHeader,
                     capabilities.logicalSectorSize) ||
        !compare_array_at(plan.targetSnapshot, capabilities,
            plan.damagedEntryArrayLba, plan.entryArraySectors,
            s_authoritativeArray, result))
        return finish_execute(plan, result, GPT_REPAIR_VERIFICATION_FAILED,
                              "The repaired copy failed byte-for-byte verification.");

    // Prove that the healthy copy and protective MBR remain byte-identical.
    if (!read_copy_sector(plan.targetSnapshot, capabilities,
            plan.authoritativeHeaderLba, s_verifySector, result) ||
        !bytes_equal(s_verifySector, s_authoritativeHeader,
                     capabilities.logicalSectorSize) ||
        !compare_array_at(plan.targetSnapshot, capabilities,
            plan.authoritativeEntryArrayLba, plan.entryArraySectors,
            s_authoritativeArray, result))
        return finish_execute(plan, result, GPT_REPAIR_VERIFICATION_FAILED,
                              "The authoritative GPT copy changed during repair.");
    if (read_range(plan.targetSnapshot, capabilities, 0, 1, s_ioSector,
            capabilities.logicalSectorSize, &result) != block::BLOCK_OK ||
        !bytes_equal(s_ioSector, s_protectiveMbr,
                     capabilities.logicalSectorSize))
        return finish_execute(plan, result, GPT_REPAIR_VERIFICATION_FAILED,
                              "Protective MBR changed during GPT repair.");
    if (!target_still_current(plan))
        return finish_execute(plan, result, GPT_REPAIR_IDENTITY_CHANGED,
                              "Target identity changed during final verification.");

    result.stage = GPT_REPAIR_STAGE_RESCANNING;
    if (!parse_partition_table(plan.targetSnapshot.globalIndex, s_table) ||
        (s_table.state != DISK_STATE_VALID_GPT &&
         !(s_table.state == DISK_STATE_GPT_DEGRADED &&
           !s_table.protectiveMbrValid)) ||
        !s_table.primaryGptValid ||
        !s_table.backupGptValid || !s_table.gptCopiesAgree ||
        s_table.gptCopiesConflict ||
        !partitions_equal(s_beforeTable, s_table) ||
        s_table.tableEntryCount != s_beforeTable.tableEntryCount ||
        s_table.gptEntrySize != s_beforeTable.gptEntrySize ||
        s_table.firstUsableLba != s_beforeTable.firstUsableLba ||
        s_table.lastUsableLba != s_beforeTable.lastUsableLba ||
        !bytes_equal(s_table.primaryDiskGuid, plan.diskGuid, 16) ||
        !bytes_equal(s_table.backupDiskGuid, plan.diskGuid, 16))
        return finish_execute(plan, result, GPT_REPAIR_RESCAN_FAILED,
                              "The normal GPT parser did not confirm a healthy pair.");
    result.finalDetectedState = s_table.state;
    result.finalPrimaryState = s_table.primaryGptCopyState;
    result.finalBackupState = s_table.backupGptCopyState;
    result.finalVerificationPassed = true;
    result.authoritativeCopyPreserved = true;
    result.protectiveMbrPreserved = true;
    result.partitionIdentitiesPreserved = true;
    result.partitionDataUntouched = true;
    return finish_execute(plan, result, GPT_REPAIR_SUCCESS,
                          "GPT redundancy repaired and both copies verified.");
}

bool cancel_gpt_repair(GptRepairPlan& plan)
{
    if (!plan.confirmationReady || !s_activePlanValid ||
        !plans_equal(plan, s_activePlan)) return false;
    const bool released = release_storage_operation(plan.lease);
    plan.confirmationReady = false;
    s_activePlanValid = false;
    clear_bytes(&s_activePlan, sizeof(s_activePlan));
    return released;
}

const char* gpt_repair_status_name(GptRepairStatus status)
{
    switch (status) {
        case GPT_REPAIR_READY_FOR_CONFIRMATION: return "Ready for confirmation";
        case GPT_REPAIR_SUCCESS: return "Success";
        case GPT_REPAIR_OPERATION_BUSY: return "Another storage operation is active";
        case GPT_REPAIR_INVALID_REQUEST: return "Invalid repair request";
        case GPT_REPAIR_DEVICE_MISSING: return "Device missing";
        case GPT_REPAIR_REGISTRY_CHANGED: return "Device registry changed";
        case GPT_REPAIR_IDENTITY_CHANGED: return "Device identity changed";
        case GPT_REPAIR_INVALID_GEOMETRY: return "Invalid or unsupported device geometry";
        case GPT_REPAIR_READ_UNAVAILABLE: return "Device is not readable";
        case GPT_REPAIR_WRITE_UNAVAILABLE: return "Device is not writable";
        case GPT_REPAIR_READ_ONLY: return "Device is read-only";
        case GPT_REPAIR_DURABILITY_UNKNOWN: return "Write durability is unknown";
        case GPT_REPAIR_FLUSH_UNAVAILABLE: return "Trusted Flush is unavailable";
        case GPT_REPAIR_MOUNTED: return "Unmount all partitions first";
        case GPT_REPAIR_ROOT_BACKING: return "Root backing device is protected";
        case GPT_REPAIR_MOUNT_STATE_UNKNOWN: return "Mount state is unknown";
        case GPT_REPAIR_BOOT_BACKING: return "Boot backing device is protected";
        case GPT_REPAIR_BOOT_IDENTITY_UNKNOWN: return "Boot identity is unknown";
        case GPT_REPAIR_GPT_HEALTHY: return "Both GPT copies are healthy";
        case GPT_REPAIR_NO_AUTHORITATIVE_COPY: return "GPT damaged; no authoritative copy";
        case GPT_REPAIR_CONFLICT: return "GPT conflict; manual recovery required";
        case GPT_REPAIR_GEOMETRY_MISMATCH: return "GPT geometry does not match current device size";
        case GPT_REPAIR_UNSUPPORTED_COPY: return "GPT copy uses unsupported metadata";
        case GPT_REPAIR_UNREADABLE_COPY: return "GPT metadata is unreadable";
        case GPT_REPAIR_STALE_SNAPSHOT: return "GPT changed; refresh required";
        case GPT_REPAIR_OPERATION_OWNERSHIP_INVALID: return "Repair operation ownership invalid";
        case GPT_REPAIR_IO_FAILED: return "GPT metadata I/O failed";
        case GPT_REPAIR_FLUSH_FAILED: return "GPT metadata Flush failed";
        case GPT_REPAIR_VERIFICATION_FAILED: return "GPT repair verification failed";
        case GPT_REPAIR_RESCAN_FAILED: return "Final GPT parser verification failed";
        default: return "Unknown GPT repair status";
    }
}

const char* gpt_repair_stage_name(GptRepairStage stage)
{
    switch (stage) {
        case GPT_REPAIR_STAGE_IDLE: return "Idle";
        case GPT_REPAIR_STAGE_ACQUIRE_LEASE: return "AcquireLease";
        case GPT_REPAIR_STAGE_PIN_TARGET: return "PinTarget";
        case GPT_REPAIR_STAGE_PREFLIGHT: return "Preflight";
        case GPT_REPAIR_STAGE_WAITING_FOR_CONFIRMATION: return "WaitingForConfirmation";
        case GPT_REPAIR_STAGE_REVALIDATING: return "Revalidating";
        case GPT_REPAIR_STAGE_READING_AUTHORITATIVE_COPY: return "ReadAuthoritativeCopy";
        case GPT_REPAIR_STAGE_WRITING_ARRAY: return "WriteEntryArray";
        case GPT_REPAIR_STAGE_VERIFYING_ARRAY: return "VerifyEntryArray";
        case GPT_REPAIR_STAGE_WRITING_HEADER: return "WriteHeader";
        case GPT_REPAIR_STAGE_FLUSH: return "Flush";
        case GPT_REPAIR_STAGE_VERIFYING_BOTH_COPIES: return "VerifyBothCopies";
        case GPT_REPAIR_STAGE_RESCANNING: return "Rescan";
        case GPT_REPAIR_STAGE_COMPLETED: return "Completed";
        case GPT_REPAIR_STAGE_FAILED: return "Failed";
        default: return "Unknown";
    }
}

const char* gpt_copy_state_name(GptCopyState state)
{
    switch (state) {
        case GPT_COPY_NOT_PRESENT: return "NotPresent";
        case GPT_COPY_VALID: return "Valid";
        case GPT_COPY_HEADER_INVALID: return "HeaderInvalid";
        case GPT_COPY_HEADER_CRC_INVALID: return "HeaderCrcInvalid";
        case GPT_COPY_ARRAY_INVALID: return "ArrayInvalid";
        case GPT_COPY_ARRAY_CRC_INVALID: return "ArrayCrcInvalid";
        case GPT_COPY_BOUNDS_INVALID: return "BoundsInvalid";
        case GPT_COPY_UNREADABLE: return "Unreadable";
        case GPT_COPY_UNSUPPORTED: return "Unsupported";
        case GPT_COPY_GEOMETRY_MISMATCH: return "GeometryMismatch";
        default: return "Unknown";
    }
}

} // namespace storage
} // namespace kernel
