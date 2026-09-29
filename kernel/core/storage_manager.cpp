#include "include/kernel/storage_manager.h"
#include "include/kernel/vfs.h"
#include "include/kernel/usb.h"
#include "include/kernel/usb_storage.h"
#include "../../guideXOSBootLoader/guidexOSBootInfo.h"

namespace kernel {
namespace storage {

namespace {

// The normalized model is larger than the amd64 boot stack. Keep the
// preflight parser result in bounded BSS storage; callers must serialize this
// validation path until the storage layer grows an operation lock.
static PartitionTableModel s_validationTableScratch;
static guideXOS::BootSourceDescriptor s_bootSource;
static bool s_bootSourceValid = false;

static uint16_t read_u16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) |
        (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t read_u32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
}

static const uint8_t* find_device_path_node(uint8_t type, uint8_t subtype,
                                           uint16_t minLength)
{
    if (!s_bootSourceValid) return nullptr;
    uint32_t offset = 0;
    while (offset < s_bootSource.DevicePathLength) {
        const uint8_t* node = s_bootSource.DevicePath + offset;
        const uint16_t length = read_u16(node + 2);
        if (node[0] == type && node[1] == subtype && length >= minLength)
            return node;
        offset += length;
    }
    return nullptr;
}

static block::BootProvenance match_usb_boot_source(
    const block::BlockDevice& device)
{
    if (device.type != block::BDEV_USB_MASS || !s_bootSourceValid ||
        !device.usbIdentityValid)
        return block::BOOT_PROVENANCE_UNKNOWN;

    const uint8_t* usbNode = nullptr;
    const uint8_t* classNode = nullptr;
    const uint8_t* wwidNode = nullptr;
    const uint8_t* lunNode = nullptr;
    uint8_t usbCount = 0, classCount = 0, wwidCount = 0, lunCount = 0;
    uint8_t hardDriveCount = 0;
    bool hasHardDriveNode = false;
    uint32_t offset = 0;
    while (offset < s_bootSource.DevicePathLength) {
        const uint8_t* node = s_bootSource.DevicePath + offset;
        const uint16_t length = read_u16(node + 2);
        if (node[0] == 0x03u && node[1] == 0x05u) {
            ++usbCount;
            usbNode = node;
        } else if (node[0] == 0x03u && node[1] == 0x0Fu) {
            ++classCount;
            classNode = node;
        } else if (node[0] == 0x03u && node[1] == 0x10u) {
            ++wwidCount;
            wwidNode = node;
        } else if (node[0] == 0x03u && node[1] == 0x11u) {
            ++lunCount;
            lunNode = node;
        } else if (node[0] == 0x04u && node[1] == 0x01u) {
            ++hardDriveCount;
            hasHardDriveNode = length == 42u;
        }
        offset += length;
    }

    // The current UHCI driver has no hub topology support. A direct USB path
    // has exactly one USB node identifying the root port and interface.
    if (usbCount != 1 || !usbNode || read_u16(usbNode + 2) != 6u ||
        hardDriveCount != 1 || !hasHardDriveNode ||
        classCount > 1 || wwidCount > 1 || lunCount > 1)
        return block::BOOT_PROVENANCE_UNKNOWN;

    if (usbNode[4] != device.usbPort ||
        usbNode[5] != device.usbInterface)
        return block::BOOT_PROVENANCE_DEFINITELY_NOT_BOOT;

    if (classNode) {
        if (read_u16(classNode + 2) != 11u)
            return block::BOOT_PROVENANCE_UNKNOWN;
        const uint16_t vendor = read_u16(classNode + 4);
        const uint16_t product = read_u16(classNode + 6);
        if ((vendor != 0xFFFFu && vendor != device.usbVendorId) ||
            (product != 0xFFFFu && product != device.usbProductId) ||
            (classNode[8] != 0xFFu &&
             classNode[8] != usb::CLASS_MASS_STORAGE) ||
            (classNode[9] != 0xFFu &&
             classNode[9] != usb::MSC_SUBCLASS_SCSI_TRANSPARENT) ||
            (classNode[10] != 0xFFu &&
             classNode[10] != usb::MSC_PROTOCOL_BULK_ONLY))
            return block::BOOT_PROVENANCE_DEFINITELY_NOT_BOOT;
    }

    // WWID paths select a device by USB serial, while the current USB core
    // does not retain the USB string descriptor. VID/PID alone cannot prove
    // that identity, so leave this form unknown.
    if (wwidNode) {
        if (read_u16(wwidNode + 2) < 10u)
            return block::BOOT_PROVENANCE_UNKNOWN;
        const uint16_t interfaceNumber = read_u16(wwidNode + 4);
        const uint16_t vendor = read_u16(wwidNode + 6);
        const uint16_t product = read_u16(wwidNode + 8);
        if (interfaceNumber != device.usbInterface ||
            vendor != device.usbVendorId || product != device.usbProductId)
            return block::BOOT_PROVENANCE_DEFINITELY_NOT_BOOT;
        return block::BOOT_PROVENANCE_UNKNOWN;
    }

    const uint8_t pathLun = lunNode ? lunNode[4] : 0u;
    if (lunNode && read_u16(lunNode + 2) != 5u)
        return block::BOOT_PROVENANCE_UNKNOWN;
    if (pathLun != device.usbLun)
        return block::BOOT_PROVENANCE_DEFINITELY_NOT_BOOT;

    return block::BOOT_PROVENANCE_BOOT_BACKING;
}

static block::BootProvenance match_boot_source(const block::BlockDevice& device)
{
    if (device.bootProvenance != block::BOOT_PROVENANCE_UNKNOWN)
        return device.bootProvenance; // Explicit hosted/test or RAM-disk state.
    if (!s_bootSourceValid ||
        !(s_bootSource.Flags & guideXOS::BOOT_SOURCE_FLAG_PCI_LOCATION_VALID) ||
        !device.pciLocationValid)
        return block::BOOT_PROVENANCE_UNKNOWN;

    if (device.pciSegment != s_bootSource.PciSegment ||
        device.pciBus != s_bootSource.PciBus ||
        device.pciDevice != s_bootSource.PciDevice ||
        device.pciFunction != s_bootSource.PciFunction)
        return block::BOOT_PROVENANCE_DEFINITELY_NOT_BOOT;

    // Matching the controller and transport endpoint protects the whole disk:
    // UEFI's later Hard Drive media node may name only one partition.
    const uint8_t* nvme = find_device_path_node(0x03u, 0x17u, 16u);
    if (nvme && device.type == block::BDEV_NVME) {
        return read_u32(nvme + 4) == device.namespaceId
            ? block::BOOT_PROVENANCE_BOOT_BACKING
            : block::BOOT_PROVENANCE_DEFINITELY_NOT_BOOT;
    }

    const uint8_t* atapi = find_device_path_node(0x03u, 0x01u, 8u);
    if (atapi && device.type == block::BDEV_ATA_PIO && device.ataTargetValid) {
        const uint8_t channel = atapi[4];
        const uint8_t target = atapi[5];
        return channel == device.ataChannel && target == device.ataTarget
            ? block::BOOT_PROVENANCE_BOOT_BACKING
            : block::BOOT_PROVENANCE_DEFINITELY_NOT_BOOT;
    }

    // UEFI SATA Messaging Device Path node (Type 3, SubType 0x12) carries
    // HBA port, port-multiplier port, and LUN as 16-bit fields. This driver
    // supports direct HBA-port disks only; multiplier paths remain unknown.
    const uint8_t* sata = find_device_path_node(0x03u, 0x12u, 10u);
    if (sata && device.type == block::BDEV_AHCI && device.ahciPortValid) {
        const uint16_t hbaPort = read_u16(sata + 4);
        const uint16_t multiplierPort = read_u16(sata + 6);
        const uint16_t lun = read_u16(sata + 8);
        if (multiplierPort != 0xFFFFu || lun != 0u)
            return block::BOOT_PROVENANCE_UNKNOWN;
        return hbaPort == device.ahciPort
            ? block::BOOT_PROVENANCE_BOOT_BACKING
            : block::BOOT_PROVENANCE_DEFINITELY_NOT_BOOT;
    }

    return match_usb_boot_source(device);
}

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
    block::BlockDevice snapshot;
    if (!block::copy_device(globalIndex, snapshot)) return false;
    const block::BlockDevice* dev = &snapshot;

    out.globalIndex = globalIndex;
    out.transport = dev->type;
    out.driverIndex = dev->driverIndex;
    out.readable = dev->readFn != nullptr;
    out.writable = dev->writeFn != nullptr;
    out.flushSupported = dev->flushFn != nullptr;
    out.flushSemanticsKnown = dev->flushSemanticsKnown;
    out.removableKnown = dev->removableKnown;
    out.removable = dev->removableKnown && dev->removable;
    out.usbIdentityValid = dev->usbIdentityValid;
    out.usbVendorId = dev->usbVendorId;
    out.usbProductId = dev->usbProductId;
    out.usbPort = dev->usbPort;
    out.usbInterface = dev->usbInterface;
    out.usbLun = dev->usbLun;
    out.usbSyncCacheState = dev->usbSyncCacheState;
    if (dev->type == block::BDEV_USB_MASS && dev->getIoDiagnosticFn) {
        block::TransportIoDiagnostic io = {};
        if (dev->getIoDiagnosticFn(dev->driverIndex, io) && io.valid)
            out.usbSyncCacheState = io.usbSyncCacheState;
    }
    if (dev->type == block::BDEV_USB_MASS) {
        const bool syncCacheSucceeded = out.usbSyncCacheState ==
            usb_storage::SYNC_CACHE_SUCCEEDED;
        if (out.usbSyncCacheState == usb_storage::SYNC_CACHE_UNSUPPORTED)
            out.flushSupported = false;
        out.flushSemanticsKnown = out.flushSemanticsKnown &&
            syncCacheSucceeded;
    }
    out.controllerPciLocationValid = dev->pciLocationValid;
    out.controllerPciSegment = dev->pciSegment;
    out.controllerPciBus = dev->pciBus;
    out.controllerPciDevice = dev->pciDevice;
    out.controllerPciFunction = dev->pciFunction;
    out.ahciPortValid = dev->ahciPortValid;
    out.ahciPort = dev->ahciPort;
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
        // A device cannot simultaneously claim that each write is durable and
        // expose an untrusted/contradictory explicit-flush contract.
        out.persistence = dev->flushFn
            ? PERSISTENCE_UNKNOWN : PERSISTENCE_SYNCHRONOUS_DURABLE;
    } else if (out.flushSupported) {
        out.persistence = out.flushSemanticsKnown
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
    block::BlockDevice snapshotDevice;
    if (!block::copy_device(globalIndex, snapshotDevice)) return false;
    const block::BlockDevice* dev = &snapshotDevice;

    TargetIdentity snapshot;
    snapshot.registryGeneration = generation;
    snapshot.registrationId = dev->registrationId;
    snapshot.globalIndex = globalIndex;
    snapshot.transport = dev->type;
    snapshot.driverIndex = dev->driverIndex;
    snapshot.totalLogicalSectors = dev->totalSectors;
    snapshot.logicalSectorSize = dev->sectorSize;
    snapshot.usbIdentityValid = dev->usbIdentityValid;
    snapshot.usbVendorId = dev->usbVendorId;
    snapshot.usbProductId = dev->usbProductId;
    snapshot.usbPort = dev->usbPort;
    snapshot.usbInterface = dev->usbInterface;
    snapshot.usbLun = dev->usbLun;
    snapshot.pciLocationValid = dev->pciLocationValid;
    snapshot.pciSegment = dev->pciSegment;
    snapshot.pciBus = dev->pciBus;
    snapshot.pciDevice = dev->pciDevice;
    snapshot.pciFunction = dev->pciFunction;
    snapshot.ahciPortValid = dev->ahciPortValid;
    snapshot.ahciPort = dev->ahciPort;
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
           left.registrationId == right.registrationId &&
           left.globalIndex == right.globalIndex &&
           left.transport == right.transport &&
           left.driverIndex == right.driverIndex &&
           left.totalLogicalSectors == right.totalLogicalSectors &&
           left.logicalSectorSize == right.logicalSectorSize &&
           left.usbIdentityValid == right.usbIdentityValid &&
           left.usbVendorId == right.usbVendorId &&
           left.usbProductId == right.usbProductId &&
           left.usbPort == right.usbPort &&
           left.usbInterface == right.usbInterface &&
           left.usbLun == right.usbLun &&
           left.pciLocationValid == right.pciLocationValid &&
           left.pciSegment == right.pciSegment &&
           left.pciBus == right.pciBus &&
           left.pciDevice == right.pciDevice &&
           left.pciFunction == right.pciFunction &&
           left.ahciPortValid == right.ahciPortValid &&
           left.ahciPort == right.ahciPort &&
           text_equal(left.name, right.name, sizeof(left.name)) &&
           text_equal(left.model, right.model, sizeof(left.model)) &&
           text_equal(left.serial, right.serial, sizeof(left.serial));
}

RevalidationStatus revalidate_target_identity(const TargetIdentity& snapshot)
{
    const uint64_t currentGeneration = block::registry_generation();
    // A saturated generation can no longer distinguish mutations. Fail
    // closed instead of allowing equality after an integer wrap/ABA.
    if (snapshot.registryGeneration == UINT64_MAX ||
        currentGeneration == UINT64_MAX ||
        snapshot.registryGeneration != currentGeneration)
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
        if (!mount || !mount->active || mount->blockDevIndex != target.globalIndex ||
            mount->parentRegistrationId != target.registrationId)
            continue;
        if (mount->partitionMount && vfs::mount_identity_valid(i))
            result.partitionIdentityKnown = true;
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

    block::BlockDevice device;
    if (!block::copy_device(target.globalIndex, device)) return result;
    switch (match_boot_source(device)) {
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

void initialize_boot_source(const guideXOS::BootInfo* bootInfo)
{
    s_bootSourceValid = false;
    for (size_t i = 0; i < sizeof(s_bootSource); ++i)
        reinterpret_cast<uint8_t*>(&s_bootSource)[i] = 0;
    if (!bootInfo || bootInfo->Magic != guideXOS::GUIDEXOS_BOOTINFO_MAGIC ||
        bootInfo->Version != guideXOS::GUIDEXOS_BOOTINFO_VERSION ||
        bootInfo->Size != sizeof(guideXOS::BootInfo) ||
        !guideXOS::guidexos_bootinfo_checksum_valid(bootInfo)) return;
    set_boot_source_descriptor(&bootInfo->BootSource);
}

bool set_boot_source_descriptor(const guideXOS::BootSourceDescriptor* source)
{
    s_bootSourceValid = false;
    for (size_t i = 0; i < sizeof(s_bootSource); ++i)
        reinterpret_cast<uint8_t*>(&s_bootSource)[i] = 0;
    if (!guideXOS::guidexos_boot_source_descriptor_valid(source)) return false;
    const uint8_t* input = reinterpret_cast<const uint8_t*>(source);
    uint8_t* output = reinterpret_cast<uint8_t*>(&s_bootSource);
    for (size_t i = 0; i < sizeof(s_bootSource); ++i) output[i] = input[i];
    s_bootSourceValid = true;
    return true;
}

const char* boot_provenance_name(block::BootProvenance provenance)
{
    switch (provenance) {
        case block::BOOT_PROVENANCE_BOOT_BACKING: return "DefinitelyBoot";
        case block::BOOT_PROVENANCE_DEFINITELY_NOT_BOOT: return "DefinitelyNotBoot";
        case block::BOOT_PROVENANCE_UNKNOWN: default: return "Unknown";
    }
}

bool validate_destructive_target(const TargetIdentity& target,
                                 const SafetyRequest& request,
                                 SafetyValidation& result)
{
    result.allowed = false;
    result.issues = SAFETY_ISSUE_NONE;
    result.identityStatus = revalidate_target_identity(target);
    result.mountSafety = DEVICE_IDENTITY_UNKNOWN;
    result.bootSafety = BOOT_DEVICE_IDENTITY_UNKNOWN;
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

    const BootProtection boot = query_boot_protection(target);
    result.bootSafety = boot.safety;
    if (boot.safety == BOOT_DEVICE_IS_TARGET)
        result.issues |= SAFETY_ISSUE_BOOT_BACKING;
    else if (boot.safety == BOOT_DEVICE_IDENTITY_UNKNOWN)
        result.issues |= SAFETY_ISSUE_BOOT_IDENTITY_UNKNOWN;

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
