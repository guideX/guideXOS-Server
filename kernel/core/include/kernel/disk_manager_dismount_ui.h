#ifndef KERNEL_DISK_MANAGER_DISMOUNT_UI_H
#define KERNEL_DISK_MANAGER_DISMOUNT_UI_H

#include "kernel/storage_manager.h"
#include "kernel/vfs.h"

namespace kernel { namespace apps { namespace disk_manager_dismount_ui {

enum AdmissionBlockReason : uint8_t {
    DISMOUNT_ADMISSION_READY = 0,
    DISMOUNT_ADMISSION_SELECTION_REQUIRED,
    DISMOUNT_ADMISSION_IDENTITY_INVALID,
    DISMOUNT_ADMISSION_STALE_TARGET,
    DISMOUNT_ADMISSION_DEVICE_REMOVED,
    DISMOUNT_ADMISSION_ALREADY_UNMOUNTED,
    DISMOUNT_ADMISSION_PROTECTED,
    DISMOUNT_ADMISSION_BUSY,
    DISMOUNT_ADMISSION_CONFLICTING_OPERATION,
    DISMOUNT_ADMISSION_UNSUPPORTED_DURABILITY,
};

inline uint8_t next_confirmation_focus(uint8_t current,
                                       bool dismountReady)
{
    return current == 2 && dismountReady ? 3 : 2;
}

struct CapturedTarget {
    bool valid;
    vfs::MountIdentity mount;
    storage::TargetIdentity device;
    storage::PartitionEntry partition;
    char mountPath[vfs::VFS_MAX_PATH];
};

inline bool same_captured_partition_identity(
    const block::PartitionIdentity& left,
    const block::PartitionIdentity& right)
{
    if (!left.valid || !right.valid || left.scheme != right.scheme ||
        left.parentDeviceIndex != right.parentDeviceIndex ||
        left.parentRegistrationId != right.parentRegistrationId ||
        left.parentRegistryGeneration != right.parentRegistryGeneration ||
        left.partitionNumber != right.partitionNumber ||
        left.mbrType != right.mbrType || left.startLba != right.startLba ||
        left.endLba != right.endLba || left.sectorCount != right.sectorCount)
        return false;
    if (left.scheme == storage::PARTITION_SCHEME_GPT) {
        for (uint8_t i = 0; i < sizeof(left.uniqueGuid); ++i)
            if (left.uniqueGuid[i] != right.uniqueGuid[i]) return false;
    }
    return true;
}

inline bool same_mount_identity(const vfs::MountIdentity& left,
                                const vfs::MountIdentity& right)
{
    return left.valid && right.valid &&
        left.mountIndex == right.mountIndex &&
        left.mountGeneration == right.mountGeneration &&
        left.blockDevIndex == right.blockDevIndex &&
        left.registrationId == right.registrationId &&
        left.registryGeneration == right.registryGeneration &&
        left.logicalSectorSize == right.logicalSectorSize &&
        left.partitionMount == right.partitionMount &&
        (!left.partitionMount || same_captured_partition_identity(
            left.partitionIdentity, right.partitionIdentity)) &&
        left.partitionView.slot == right.partitionView.slot &&
        left.partitionView.generation == right.partitionView.generation;
}

inline bool partition_matches_mount(const storage::TargetIdentity& device,
                                    const storage::PartitionEntry& partition,
                                    const vfs::MountIdentity& mount)
{
    if (!device.registrationId || !mount.valid || !mount.partitionMount ||
        mount.blockDevIndex != device.globalIndex ||
        mount.registrationId != device.registrationId ||
        mount.registryGeneration != device.registryGeneration ||
        mount.logicalSectorSize != device.logicalSectorSize ||
        !mount.partitionIdentity.valid)
        return false;
    const block::PartitionIdentity& identity = mount.partitionIdentity;
    if (identity.parentDeviceIndex != device.globalIndex ||
        identity.parentRegistrationId != device.registrationId ||
        identity.parentRegistryGeneration != device.registryGeneration ||
        identity.partitionNumber != partition.partitionNumber ||
        identity.scheme != (partition.isGpt ? storage::PARTITION_SCHEME_GPT
                                            : storage::PARTITION_SCHEME_MBR) ||
        identity.startLba != partition.startLba ||
        identity.endLba != partition.endLba ||
        identity.sectorCount != partition.sectorCount ||
        identity.mbrType != partition.mbrType)
        return false;
    if (partition.isGpt) {
        for (uint8_t i = 0; i < sizeof(identity.uniqueGuid); ++i)
            if (identity.uniqueGuid[i] != partition.uniqueGuid[i]) return false;
    }
    return true;
}

inline bool partition_matches_mount_point(
    const storage::TargetIdentity& device,
    const storage::PartitionEntry& partition,
    const vfs::MountPoint& mount)
{
    if (!mount.active || !mount.partitionMount || !mount.partitionIdentity.valid ||
        mount.blockDevIndex != device.globalIndex ||
        mount.parentRegistrationId != device.registrationId ||
        mount.parentRegistryGeneration != device.registryGeneration ||
        mount.logicalSectorSize != device.logicalSectorSize)
        return false;
    const block::PartitionIdentity& identity = mount.partitionIdentity;
    if (identity.parentDeviceIndex != device.globalIndex ||
        identity.parentRegistrationId != device.registrationId ||
        identity.parentRegistryGeneration != device.registryGeneration ||
        identity.partitionNumber != partition.partitionNumber ||
        identity.scheme != (partition.isGpt ? storage::PARTITION_SCHEME_GPT
                                            : storage::PARTITION_SCHEME_MBR) ||
        identity.startLba != partition.startLba ||
        identity.endLba != partition.endLba ||
        identity.sectorCount != partition.sectorCount ||
        identity.mbrType != partition.mbrType)
        return false;
    if (partition.isGpt) {
        for (uint8_t i = 0; i < sizeof(identity.uniqueGuid); ++i)
            if (identity.uniqueGuid[i] != partition.uniqueGuid[i]) return false;
    }
    return true;
}

inline bool same_partition_entry_identity(
    const storage::PartitionEntry& left,
    const storage::PartitionEntry& right)
{
    if (left.partitionNumber != right.partitionNumber ||
        left.isGpt != right.isGpt || left.startLba != right.startLba ||
        left.endLba != right.endLba || left.sectorCount != right.sectorCount ||
        left.mbrType != right.mbrType)
        return false;
    if (left.isGpt) {
        for (uint8_t i = 0; i < sizeof(left.uniqueGuid); ++i)
            if (left.uniqueGuid[i] != right.uniqueGuid[i]) return false;
    }
    return true;
}

inline bool partition_table_has_unique_entry(
    const storage::PartitionTableModel& table,
    const storage::PartitionEntry& expected)
{
    const storage::PartitionScheme scheme = expected.isGpt
        ? storage::PARTITION_SCHEME_GPT : storage::PARTITION_SCHEME_MBR;
    if (table.scheme != scheme) return false;
    const storage::PartitionEntry* match = nullptr;
    for (uint16_t i = 0; i < table.partitionCount; ++i) {
        if (table.partitions[i].partitionNumber != expected.partitionNumber)
            continue;
        if (match) return false;
        match = &table.partitions[i];
    }
    return match && same_partition_entry_identity(*match, expected);
}

inline bool capture_target(const storage::TargetIdentity& device,
                           const storage::PartitionEntry& partition,
                           const vfs::MountIdentity& mount,
                           const vfs::MountPoint& snapshot,
                           CapturedTarget& out)
{
    out = {};
    if (!partition_matches_mount(device, partition, mount) ||
        !snapshot.active ||
        snapshot.lifecycleState != vfs::MOUNT_LIFECYCLE_MOUNTED ||
        snapshot.mountGeneration != mount.mountGeneration ||
        snapshot.blockDevIndex != mount.blockDevIndex ||
        snapshot.parentRegistrationId != mount.registrationId ||
        snapshot.parentRegistryGeneration != mount.registryGeneration ||
        snapshot.logicalSectorSize != mount.logicalSectorSize ||
        !snapshot.partitionMount ||
        !same_captured_partition_identity(snapshot.partitionIdentity,
                                 mount.partitionIdentity) ||
        snapshot.partitionView.slot != mount.partitionView.slot ||
        snapshot.partitionView.generation != mount.partitionView.generation)
        return false;
    out.valid = true;
    out.mount = mount;
    out.device = device;
    out.partition = partition;
    for (size_t i = 0; i + 1 < sizeof(out.mountPath) && snapshot.path[i]; ++i)
        out.mountPath[i] = snapshot.path[i];
    return true;
}

inline bool path_equals(const char* left, const char* right);

inline bool binding_matches(const CapturedTarget& captured,
                            const storage::TargetIdentity& currentDevice,
                            const vfs::MountIdentity& currentMount,
                            const vfs::MountPoint& currentSnapshot)
{
    return captured.valid && storage::target_identities_equal(
            captured.device, currentDevice) &&
        same_mount_identity(captured.mount, currentMount) &&
        currentSnapshot.active &&
        currentSnapshot.lifecycleState == vfs::MOUNT_LIFECYCLE_MOUNTED &&
        currentSnapshot.mountGeneration == captured.mount.mountGeneration &&
        currentSnapshot.blockDevIndex == captured.mount.blockDevIndex &&
        currentSnapshot.parentRegistrationId == captured.mount.registrationId &&
        currentSnapshot.parentRegistryGeneration ==
            captured.mount.registryGeneration &&
        currentSnapshot.logicalSectorSize == captured.mount.logicalSectorSize &&
        currentSnapshot.partitionMount &&
        same_captured_partition_identity(currentSnapshot.partitionIdentity,
                                captured.mount.partitionIdentity) &&
        currentSnapshot.partitionView.slot == captured.mount.partitionView.slot &&
        currentSnapshot.partitionView.generation ==
            captured.mount.partitionView.generation &&
        path_equals(captured.mountPath, currentSnapshot.path);
}

inline bool path_equals(const char* left, const char* right)
{
    if (!left || !right) return false;
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

inline bool protected_mount_path(const char* path)
{
    if (!path) return false;
    if (path[0] != '/') return false;
    if (path[1] == '\0') return true;
    return (path[1] == 'b' && path[2] == 'o' && path[3] == 'o' &&
            path[4] == 't' && path[5] == '\0') ||
        (path[1] == 's' && path[2] == 'y' && path[3] == 's' &&
            path[4] == 't' && path[5] == 'e' && path[6] == 'm' &&
            path[7] == '\0');
}

inline AdmissionBlockReason admission_reason(
    bool identityValid, bool targetCurrent, bool alreadyUnmounted,
    bool protectedTarget, bool conflictingOperation, bool busy,
    bool requiredDurabilitySupported)
{
    if (!identityValid) return DISMOUNT_ADMISSION_IDENTITY_INVALID;
    if (alreadyUnmounted) return DISMOUNT_ADMISSION_ALREADY_UNMOUNTED;
    if (!targetCurrent) return DISMOUNT_ADMISSION_STALE_TARGET;
    if (protectedTarget) return DISMOUNT_ADMISSION_PROTECTED;
    if (conflictingOperation)
        return DISMOUNT_ADMISSION_CONFLICTING_OPERATION;
    if (busy) return DISMOUNT_ADMISSION_BUSY;
    if (!requiredDurabilitySupported)
        return DISMOUNT_ADMISSION_UNSUPPORTED_DURABILITY;
    return DISMOUNT_ADMISSION_READY;
}

inline const char* admission_reason_text(AdmissionBlockReason reason)
{
    switch (reason) {
        case DISMOUNT_ADMISSION_SELECTION_REQUIRED:
            return "Select one mounted partition.";
        case DISMOUNT_ADMISSION_IDENTITY_INVALID:
            return "The mounted partition identity cannot be established.";
        case DISMOUNT_ADMISSION_STALE_TARGET:
            return "The mount or device changed. Refresh and select it again.";
        case DISMOUNT_ADMISSION_DEVICE_REMOVED:
            return "The original device was removed. Refresh and select a current volume.";
        case DISMOUNT_ADMISSION_ALREADY_UNMOUNTED:
            return "The captured volume is already unmounted.";
        case DISMOUNT_ADMISSION_PROTECTED:
            return "Root, boot, system, or unknown-provenance storage is protected.";
        case DISMOUNT_ADMISSION_BUSY:
            return "Volume is busy. Close files and applications using this volume before dismounting.";
        case DISMOUNT_ADMISSION_CONFLICTING_OPERATION:
            return "Another storage operation owns this target.";
        case DISMOUNT_ADMISSION_UNSUPPORTED_DURABILITY:
            return "Required write or Flush durability is unsupported.";
        default:
            return "Ready to dismount the captured volume.";
    }
}

}}} // namespace kernel::apps::disk_manager_dismount_ui

#endif
