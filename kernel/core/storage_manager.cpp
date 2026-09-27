#include "include/kernel/storage_manager.h"
#include "include/kernel/vfs.h"

namespace kernel {
namespace storage {

namespace {

// The normalized model is larger than the amd64 boot stack. Keep the
// preflight parser result in bounded BSS storage; callers must serialize this
// validation path until the storage layer grows an operation lock.
static PartitionTableModel s_validationTableScratch;

static void copy_text(char* dst, size_t capacity, const char* src)
{
    if (capacity == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] && i + 1 < capacity) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static bool text_equal(const char* a, const char* b, size_t capacity)
{
    for (size_t i = 0; i < capacity; ++i) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
    }
    return true;
}

static bool is_root_path(const char* path)
{
    return path && path[0] == '/' && path[1] == '\0';
}

} // namespace

bool valid_logical_sector_size(uint32_t sectorSize)
{
    return sectorSize >= MIN_LOGICAL_SECTOR_SIZE &&
           sectorSize <= MAX_LOGICAL_SECTOR_SIZE &&
           (sectorSize & (sectorSize - 1)) == 0;
}

bool valid_geometry(uint64_t totalSectors, uint32_t sectorSize,
                    uint64_t* capacityBytes)
{
    if (capacityBytes) *capacityBytes = 0;
    if (totalSectors == 0 || !valid_logical_sector_size(sectorSize) ||
        totalSectors > UINT64_MAX / sectorSize) return false;
    if (capacityBytes) *capacityBytes = totalSectors * sectorSize;
    return true;
}

bool checked_lba_range(uint64_t totalSectors, uint64_t lba, uint64_t count)
{
    return count != 0 && lba <= totalSectors && count <= totalSectors - lba;
}

bool query_device_capabilities(uint8_t globalIndex, DeviceCapabilities& out)
{
    const block::BlockDevice* dev = block::get_device(globalIndex);
    if (!dev) return false;

    out.globalIndex = globalIndex;
    out.transport = dev->type;
    out.driverIndex = dev->driverIndex;
    out.readable = dev->readFn != nullptr;
    out.writable = dev->writeFn != nullptr;
    out.flushSupported = dev->flushFn != nullptr;
    out.flushSemanticsKnown = dev->flushSemanticsKnown;
    out.removableKnown = dev->removableKnown;
    out.removable = dev->removableKnown && dev->removable;
    out.logicalSectorSize = dev->sectorSize;
    out.totalLogicalSectors = dev->totalSectors;
    out.requiredBufferAlignment = dev->requiredBufferAlignment;
    out.maxTransferBytes = dev->maxTransferBytes;
    out.geometryValid = valid_logical_sector_size(dev->sectorSize);
    out.capacityValid = valid_geometry(dev->totalSectors, dev->sectorSize,
                                       &out.totalCapacityBytes);
    if (!out.capacityValid) out.totalCapacityBytes = 0;
    if (dev->type == block::BDEV_RAMDISK) {
        out.persistence = PERSISTENCE_VOLATILE_MEMORY;
    } else if (dev->writeCompletionDurable) {
        out.persistence = PERSISTENCE_SYNCHRONOUS_DURABLE;
    } else if (dev->flushFn) {
        out.persistence = dev->flushSemanticsKnown
            ? PERSISTENCE_FLUSH_REQUIRED : PERSISTENCE_UNKNOWN;
    } else {
        out.persistence = PERSISTENCE_UNKNOWN;
    }
    copy_text(out.name, sizeof(out.name), dev->name);
    copy_text(out.model, sizeof(out.model), dev->model);
    copy_text(out.serial, sizeof(out.serial), dev->serial);
    return true;
}

block::Status read_sectors_safe(uint8_t globalIndex, uint64_t lba,
                                uint32_t count, void* buffer,
                                size_t bufferBytes)
{
    DeviceCapabilities caps;
    if (!buffer || count == 0 || !query_device_capabilities(globalIndex, caps) ||
        !caps.geometryValid || !caps.capacityValid || !caps.readable ||
        !checked_lba_range(caps.totalLogicalSectors, lba, count)) {
        return block::BLOCK_ERR_INVALID;
    }
    const uint64_t requiredBytes = static_cast<uint64_t>(count) * caps.logicalSectorSize;
    if (requiredBytes > static_cast<uint64_t>(bufferBytes))
        return block::BLOCK_ERR_INVALID;
    if (caps.maxTransferBytes != 0 && requiredBytes > caps.maxTransferBytes)
        return block::BLOCK_ERR_UNSUPPORTED;
    if (caps.requiredBufferAlignment > 1 &&
        (reinterpret_cast<uintptr_t>(buffer) % caps.requiredBufferAlignment) != 0)
        return block::BLOCK_ERR_INVALID;
    return block::read_sectors_checked(globalIndex, lba, count, buffer, bufferBytes);
}

block::Status read_logical_sector(uint8_t globalIndex, uint64_t lba,
                                  void* buffer, size_t bufferBytes)
{
    return read_sectors_safe(globalIndex, lba, 1, buffer, bufferBytes);
}

block::Status write_sectors_safe(uint8_t globalIndex, uint64_t lba,
                                 uint32_t count, const void* buffer,
                                 size_t bufferBytes)
{
    DeviceCapabilities caps;
    if (!buffer || count == 0) return block::BLOCK_ERR_INVALID;
    if (!query_device_capabilities(globalIndex, caps)) return block::BLOCK_ERR_INVALID;
    if (!caps.writable) return block::BLOCK_ERR_UNSUPPORTED;
    if (!caps.geometryValid || !caps.capacityValid ||
        !checked_lba_range(caps.totalLogicalSectors, lba, count))
        return block::BLOCK_ERR_INVALID;
    const uint64_t requiredBytes = static_cast<uint64_t>(count) * caps.logicalSectorSize;
    if (requiredBytes > static_cast<uint64_t>(bufferBytes))
        return block::BLOCK_ERR_INVALID;
    if (caps.maxTransferBytes != 0 && requiredBytes > caps.maxTransferBytes)
        return block::BLOCK_ERR_UNSUPPORTED;
    if (caps.requiredBufferAlignment > 1 &&
        (reinterpret_cast<uintptr_t>(buffer) % caps.requiredBufferAlignment) != 0)
        return block::BLOCK_ERR_INVALID;
    return block::write_sectors_checked(globalIndex, lba, count, buffer, bufferBytes);
}

bool capture_target_identity(uint8_t globalIndex, TargetIdentity& out)
{
    const uint64_t generation = block::registry_generation();
    const block::BlockDevice* dev = block::get_device(globalIndex);
    if (!dev) return false;

    TargetIdentity snapshot;
    snapshot.registryGeneration = generation;
    snapshot.globalIndex = globalIndex;
    snapshot.transport = dev->type;
    snapshot.driverIndex = dev->driverIndex;
    snapshot.totalLogicalSectors = dev->totalSectors;
    snapshot.logicalSectorSize = dev->sectorSize;
    copy_text(snapshot.name, sizeof(snapshot.name), dev->name);
    copy_text(snapshot.model, sizeof(snapshot.model), dev->model);
    copy_text(snapshot.serial, sizeof(snapshot.serial), dev->serial);
    if (generation != block::registry_generation()) return false;
    out = snapshot;
    return true;
}

bool target_identities_equal(const TargetIdentity& left,
                             const TargetIdentity& right)
{
    return left.registryGeneration == right.registryGeneration &&
           left.globalIndex == right.globalIndex &&
           left.transport == right.transport &&
           left.driverIndex == right.driverIndex &&
           left.totalLogicalSectors == right.totalLogicalSectors &&
           left.logicalSectorSize == right.logicalSectorSize &&
           text_equal(left.name, right.name, sizeof(left.name)) &&
           text_equal(left.model, right.model, sizeof(left.model)) &&
           text_equal(left.serial, right.serial, sizeof(left.serial));
}

RevalidationStatus revalidate_target_identity(const TargetIdentity& snapshot)
{
    if (snapshot.registryGeneration != block::registry_generation())
        return TARGET_REGISTRY_CHANGED;
    TargetIdentity current;
    if (!capture_target_identity(snapshot.globalIndex, current))
        return TARGET_DEVICE_MISSING;
    return target_identities_equal(snapshot, current)
        ? TARGET_VALID : TARGET_IDENTITY_MISMATCH;
}

MountProtection query_mount_protection(const TargetIdentity& target)
{
    MountProtection result;
    result.safety = DEVICE_IDENTITY_UNKNOWN;
    result.partitionIdentityKnown = false;
    if (revalidate_target_identity(target) != TARGET_VALID) return result;

    result.safety = DEVICE_UNMOUNTED;
    for (uint8_t i = 0; i < vfs::VFS_MAX_MOUNTS; ++i) {
        const vfs::MountPoint* mount = vfs::get_mount_by_index(i);
        if (!mount || !mount->active || mount->blockDevIndex != target.globalIndex)
            continue;
        if (is_root_path(mount->path)) {
            result.safety = DEVICE_ROOT_BACKING;
            break;
        }
        result.safety = DEVICE_MOUNTED;
    }
    if (revalidate_target_identity(target) != TARGET_VALID)
        result.safety = DEVICE_IDENTITY_UNKNOWN;
    return result;
}

BootProtection query_boot_protection(const TargetIdentity& target)
{
    BootProtection result;
    result.safety = BOOT_DEVICE_IDENTITY_UNKNOWN;
    if (revalidate_target_identity(target) != TARGET_VALID) return result;

    const block::BlockDevice* device = block::get_device(target.globalIndex);
    if (!device) return result;
    switch (device->bootProvenance) {
        case block::BOOT_PROVENANCE_BOOT_BACKING:
            result.safety = BOOT_DEVICE_IS_TARGET;
            break;
        case block::BOOT_PROVENANCE_DEFINITELY_NOT_BOOT:
            result.safety = BOOT_DEVICE_DEFINITELY_NOT_TARGET;
            break;
        case block::BOOT_PROVENANCE_UNKNOWN:
        default:
            result.safety = BOOT_DEVICE_IDENTITY_UNKNOWN;
            break;
    }
    if (revalidate_target_identity(target) != TARGET_VALID)
        result.safety = BOOT_DEVICE_IDENTITY_UNKNOWN;
    return result;
}

bool validate_destructive_target(const TargetIdentity& target,
                                 const SafetyRequest& request,
                                 SafetyValidation& result)
{
    result.allowed = false;
    result.issues = SAFETY_ISSUE_NONE;
    result.identityStatus = revalidate_target_identity(target);
    result.mountSafety = DEVICE_IDENTITY_UNKNOWN;
    result.diskState = DISK_STATE_UNREADABLE;

    switch (result.identityStatus) {
        case TARGET_REGISTRY_CHANGED:
            result.issues |= SAFETY_ISSUE_REGISTRY_CHANGED;
            break;
        case TARGET_DEVICE_MISSING:
            result.issues |= SAFETY_ISSUE_DEVICE_MISSING;
            break;
        case TARGET_IDENTITY_MISMATCH:
            result.issues |= SAFETY_ISSUE_IDENTITY_MISMATCH;
            break;
        case TARGET_VALID:
        default:
            break;
    }
    if (result.identityStatus != TARGET_VALID) return false;

    DeviceCapabilities caps;
    if (!query_device_capabilities(target.globalIndex, caps)) {
        result.issues |= SAFETY_ISSUE_DEVICE_MISSING;
        return false;
    }
    if (!caps.geometryValid || !caps.capacityValid)
        result.issues |= SAFETY_ISSUE_UNKNOWN_GEOMETRY;
    if (request.requireWritable && !caps.writable)
        result.issues |= SAFETY_ISSUE_READ_ONLY;
    if (request.requireDurableWrites &&
        !(caps.persistence == PERSISTENCE_SYNCHRONOUS_DURABLE ||
          (caps.persistence == PERSISTENCE_FLUSH_REQUIRED &&
           caps.flushSupported && caps.flushSemanticsKnown))) {
        result.issues |= SAFETY_ISSUE_DURABILITY_UNKNOWN;
    }

    PartitionTableModel& table = s_validationTableScratch;
    if (!caps.readable || !parse_partition_table(target.globalIndex, table) ||
        table.state == DISK_STATE_UNREADABLE) {
        result.issues |= SAFETY_ISSUE_UNREADABLE;
    } else {
        result.diskState = table.state;
        if (request.requireKnownPartitionState) {
            const bool knownInitialized = table.state == DISK_STATE_VALID_MBR ||
                                          table.state == DISK_STATE_VALID_GPT;
            const bool permittedRaw = request.allowNotInitialized &&
                                      table.state == DISK_STATE_NOT_INITIALIZED;
            if (!knownInitialized && !permittedRaw)
                result.issues |= SAFETY_ISSUE_PARTITION_STATE;
        }
    }

    const MountProtection protection = query_mount_protection(target);
    result.mountSafety = protection.safety;
    if (protection.safety == DEVICE_ROOT_BACKING)
        result.issues |= SAFETY_ISSUE_ROOT_BACKING;
    else if (protection.safety == DEVICE_MOUNTED)
        result.issues |= SAFETY_ISSUE_MOUNTED;
    else if (protection.safety == DEVICE_IDENTITY_UNKNOWN)
        result.issues |= SAFETY_ISSUE_IDENTITY_MISMATCH;

    if (revalidate_target_identity(target) != TARGET_VALID)
        result.issues |= SAFETY_ISSUE_REGISTRY_CHANGED;
    result.allowed = result.issues == SAFETY_ISSUE_NONE;
    return result.allowed;
}

} // namespace storage
} // namespace kernel
