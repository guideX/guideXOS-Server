#ifndef KERNEL_STORAGE_MANAGER_H
#define KERNEL_STORAGE_MANAGER_H

#include "kernel/block_device.h"
#include "kernel/partition_table.h"

namespace guideXOS { struct BootInfo; struct BootSourceDescriptor; }

namespace kernel {
namespace storage {

static const uint32_t MIN_LOGICAL_SECTOR_SIZE = 512;
static const uint32_t MAX_LOGICAL_SECTOR_SIZE = 4096;

enum PersistenceClass : uint8_t {
    PERSISTENCE_UNKNOWN = 0,
    PERSISTENCE_FLUSH_REQUIRED,
    PERSISTENCE_SYNCHRONOUS_DURABLE,
    PERSISTENCE_VOLATILE_MEMORY,
};

enum IdentityConfidence : uint8_t {
    IDENTITY_CONFIDENCE_UNCERTAIN = 0,
    IDENTITY_CONFIDENCE_STABLE_MEDIA_ID,
};

struct DeviceCapabilities {
    uint8_t globalIndex;
    block::DeviceType transport;
    uint8_t driverIndex;
    bool readable;
    bool writable;
    bool flushSupported;
    bool flushSemanticsKnown;
    bool removableKnown;
    bool removable;
    bool usbIdentityValid;
    uint16_t usbVendorId;
    uint16_t usbProductId;
    uint8_t usbPort;
    uint8_t usbInterface;
    uint8_t usbLun;
    uint8_t usbSyncCacheState;
    bool controllerPciLocationValid;
    uint32_t controllerPciSegment;
    uint8_t controllerPciBus;
    uint8_t controllerPciDevice;
    uint8_t controllerPciFunction;
    bool ahciPortValid;
    uint8_t ahciPort;
    bool geometryValid;
    bool capacityValid;
    uint32_t logicalSectorSize;
    uint64_t totalLogicalSectors;
    uint64_t totalCapacityBytes;
    uint16_t requiredBufferAlignment;
    uint32_t maxTransferBytes;
    PersistenceClass persistence;
    char name[32];
    char model[40];
    char serial[24];
    uint32_t physicalSectorSize;
    char firmwareRevision[9];
    IdentityConfidence identityConfidence;
    bool ataTargetValid;
    uint8_t ataChannel;
    uint8_t ataTarget;
    uint32_t namespaceId;
    bool namespaceNguidValid;
    uint8_t namespaceNguid[16];
    bool namespaceEui64Valid;
    uint8_t namespaceEui64[8];
};

struct TargetIdentity {
    uint64_t registryGeneration;
    uint64_t registrationId;
    uint8_t globalIndex;
    block::DeviceType transport;
    uint8_t driverIndex;
    uint64_t totalLogicalSectors;
    uint32_t logicalSectorSize;
    // Captured boot provenance is part of the immutable target snapshot used
    // by destructive operations. A registration cannot be reinterpreted as
    // an ordinary data device after admission.
    block::BootProvenance bootProvenance;
    bool ataTargetValid;
    uint8_t ataChannel;
    uint8_t ataTarget;
    bool usbIdentityValid;
    uint16_t usbVendorId;
    uint16_t usbProductId;
    uint8_t usbPort;
    uint8_t usbInterface;
    uint8_t usbLun;
    bool pciLocationValid;
    uint32_t pciSegment;
    uint8_t pciBus;
    uint8_t pciDevice;
    uint8_t pciFunction;
    bool ahciPortValid;
    uint8_t ahciPort;
    char name[32];
    char model[40];
    char serial[24];
    uint32_t physicalSectorSize;
    char firmwareRevision[9];
    uint32_t namespaceId;
    bool namespaceNguidValid;
    uint8_t namespaceNguid[16];
    bool namespaceEui64Valid;
    uint8_t namespaceEui64[8];
};

enum RevalidationStatus : uint8_t {
    TARGET_VALID = 0,
    TARGET_REGISTRY_CHANGED,
    TARGET_DEVICE_MISSING,
    TARGET_IDENTITY_MISMATCH,
};

enum MountSafety : uint8_t {
    DEVICE_UNMOUNTED = 0,
    DEVICE_MOUNTED,
    DEVICE_ROOT_BACKING,
    DEVICE_IDENTITY_UNKNOWN,
};

enum BootSafety : uint8_t {
    BOOT_DEVICE_DEFINITELY_NOT_TARGET = 0,
    BOOT_DEVICE_IS_TARGET,
    BOOT_DEVICE_IDENTITY_UNKNOWN,
};

struct MountProtection {
    MountSafety safety;
    bool partitionIdentityKnown;
};

struct BootProtection {
    BootSafety safety;
};

enum SafetyIssue : uint32_t {
    SAFETY_ISSUE_NONE = 0,
    SAFETY_ISSUE_DEVICE_MISSING = 1u << 0,
    SAFETY_ISSUE_REGISTRY_CHANGED = 1u << 1,
    SAFETY_ISSUE_IDENTITY_MISMATCH = 1u << 2,
    SAFETY_ISSUE_UNKNOWN_GEOMETRY = 1u << 3,
    SAFETY_ISSUE_UNREADABLE = 1u << 4,
    SAFETY_ISSUE_READ_ONLY = 1u << 5,
    SAFETY_ISSUE_DURABILITY_UNKNOWN = 1u << 6,
    SAFETY_ISSUE_MOUNTED = 1u << 7,
    SAFETY_ISSUE_ROOT_BACKING = 1u << 8,
    SAFETY_ISSUE_PARTITION_STATE = 1u << 9,
    SAFETY_ISSUE_BOOT_BACKING = 1u << 10,
    SAFETY_ISSUE_BOOT_IDENTITY_UNKNOWN = 1u << 11,
    SAFETY_ISSUE_IDENTITY_UNCERTAIN = 1u << 12,
};

struct SafetyRequest {
    bool requireWritable;
    bool requireDurableWrites;
    bool requireKnownPartitionState;
    bool allowNotInitialized;
};

struct SafetyValidation {
    bool allowed;
    uint32_t issues;
    RevalidationStatus identityStatus;
    MountSafety mountSafety;
    BootSafety bootSafety;
    DiskState diskState;
};

bool valid_logical_sector_size(uint32_t sectorSize);
bool valid_geometry(uint64_t totalSectors, uint32_t sectorSize,
                    uint64_t* capacityBytes = nullptr);
bool checked_lba_range(uint64_t totalSectors, uint64_t lba, uint64_t count);

bool query_device_capabilities(uint8_t globalIndex, DeviceCapabilities& out);
block::Status read_sectors_safe(uint8_t globalIndex, uint64_t lba,
                                uint32_t count, void* buffer,
                                size_t bufferBytes);
block::Status read_logical_sector(uint8_t globalIndex, uint64_t lba,
                                  void* buffer, size_t bufferBytes);
block::Status write_sectors_safe(uint8_t globalIndex, uint64_t lba,
                                 uint32_t count, const void* buffer,
                                 size_t bufferBytes);

bool capture_target_identity(uint8_t globalIndex, TargetIdentity& out);
bool target_identities_equal(const TargetIdentity& left,
                             const TargetIdentity& right);
RevalidationStatus revalidate_target_identity(const TargetIdentity& snapshot);

MountProtection query_mount_protection(const TargetIdentity& target);
// Live mount-presence snapshot for UI previews. This validates the exact
// device registration and scans bounded VFS metadata, but deliberately does
// not reparse the partition table or prove partition identity. Never use it
// to authorize destructive or lifecycle operations.
MountProtection query_mount_presence(const TargetIdentity& target);
// Partition-scoped protection used by delete operations. A validated mount of
// another partition on the same disk does not block this partition. Whole-disk
// or stale/ambiguous mounts fail closed.
MountProtection query_partition_mount_protection(
    const TargetIdentity& target, PartitionScheme scheme,
    const PartitionEntry& partition);
BootProtection query_boot_protection(const TargetIdentity& target);
// Boot provenance is disk-scoped by the transports. This narrows a known boot
// disk to its firmware-identified partition only when the UEFI hard-drive node
// exactly matches a validated partition in the current table.
BootProtection query_partition_boot_protection(
    const TargetIdentity& target, PartitionScheme scheme,
    const PartitionEntry& partition, const PartitionTableModel& table);
// Called before storage discovery. Invalid/missing/legacy BootInfo leaves
// provenance unknown. The setter exists for deterministic hosted tests.
void initialize_boot_source(const guideXOS::BootInfo* bootInfo);
bool set_boot_source_descriptor(const guideXOS::BootSourceDescriptor* source);
const char* boot_provenance_name(block::BootProvenance provenance);
bool validate_destructive_target(const TargetIdentity& target,
                                 const SafetyRequest& request,
                                 SafetyValidation& result);

} // namespace storage
} // namespace kernel

#endif
