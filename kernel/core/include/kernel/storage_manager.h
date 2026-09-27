#ifndef KERNEL_STORAGE_MANAGER_H
#define KERNEL_STORAGE_MANAGER_H

#include "kernel/block_device.h"
#include "kernel/partition_table.h"

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
};

struct TargetIdentity {
    uint64_t registryGeneration;
    uint8_t globalIndex;
    block::DeviceType transport;
    uint8_t driverIndex;
    uint64_t totalLogicalSectors;
    uint32_t logicalSectorSize;
    char name[32];
    char model[40];
    char serial[24];
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

struct MountProtection {
    MountSafety safety;
    bool partitionIdentityKnown;
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
bool validate_destructive_target(const TargetIdentity& target,
                                 const SafetyRequest& request,
                                 SafetyValidation& result);

} // namespace storage
} // namespace kernel

#endif
