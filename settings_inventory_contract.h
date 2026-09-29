#pragma once

// Bounded, pointer-free snapshots of guideXOS-owned devices and storage.
// Hosted operating-system inventories are deliberately not part of this
// contract: an unavailable kernel provider stays unavailable.

#include <stddef.h>
#include <stdint.h>

namespace gxos {
namespace settings_inventory {

constexpr uint32_t kContractVersion = 1;
constexpr size_t kMaxDevices = 24;
constexpr size_t kMaxDisks = 16;
constexpr size_t kMaxPartitionsPerDisk = 4;
constexpr size_t kMaxVolumes = 8;
constexpr size_t kStableIdBytes = 40;
constexpr size_t kDeviceNameBytes = 64;
constexpr size_t kDriverNameBytes = 32;
constexpr size_t kDeviceLocationBytes = 32;
constexpr size_t kDiskNameBytes = 48;
constexpr size_t kMountPathBytes = 64;

enum class Backend : uint8_t { Unavailable = 0, Kernel = 1, HostedTest = 2 };
enum class SnapshotState : uint8_t { Unavailable = 0, Empty = 1, Available = 2 };
enum class DeviceCategory : uint8_t {
    Input = 0, Network = 1, Display = 2, Storage = 3, Audio = 4, Usb = 5, Other = 6
};
enum class DeviceStatus : uint8_t {
    Unknown = 0, DriverLoaded = 1, NoDriver = 2, Disconnected = 3, Unavailable = 4
};
enum class DiskTransport : uint8_t {
    Unknown = 0, AtaPio = 1, Ahci = 2, Nvme = 3, UsbMassStorage = 4
};
enum class PartitionTableState : uint8_t {
    Unknown = 0, Unreadable = 1, NoMbr = 2, ValidMbr = 3, InvalidMbr = 4,
    GptUnsupported = 5
};
enum class FileSystem : uint8_t {
    Unknown = 0, Fat32 = 1, ExFat = 2, Ext2 = 3, Ext4 = 4, Ufs = 5,
    Iso9660 = 6, RamDisk = 7
};
enum class MountState : uint8_t { Unknown = 0, Unmounted = 1, Mounted = 2 };

constexpr uint8_t kDeviceFlagPciIdentity = 1u << 0;
constexpr uint8_t kDeviceFlagTextTruncated = 1u << 1;
constexpr uint8_t kDiskFlagCapacityAvailable = 1u << 0;
constexpr uint8_t kDiskFlagWritable = 1u << 1;
constexpr uint8_t kDiskFlagSectorSizeAvailable = 1u << 2;
constexpr uint8_t kDiskFlagNameTruncated = 1u << 3;
constexpr uint8_t kVolumeFlagReadOnly = 1u << 0;
constexpr uint8_t kVolumeFlagCapacityAvailable = 1u << 1;
constexpr uint8_t kVolumeFlagFreeSpaceAvailable = 1u << 2;
constexpr uint8_t kVolumeFlagPathTruncated = 1u << 3;

struct DeviceInfo {
    char stableId[kStableIdBytes]{};
    char name[kDeviceNameBytes]{};
    char driver[kDriverNameBytes]{};
    char location[kDeviceLocationBytes]{};
    DeviceCategory category{DeviceCategory::Other};
    DeviceStatus status{DeviceStatus::Unknown};
    uint16_t vendorId{0};
    uint16_t deviceId{0};
    uint8_t bus{0};
    uint8_t slot{0};
    uint8_t function{0};
    uint8_t flags{0};
};

struct DeviceSnapshot {
    uint32_t version{kContractVersion};
    Backend backend{Backend::Unavailable};
    SnapshotState state{SnapshotState::Unavailable};
    bool truncated{false};
    uint64_t generation{0};
    uint16_t deviceCount{0};
    uint16_t totalDeviceCount{0};
    DeviceInfo devices[kMaxDevices]{};
};

struct PartitionInfo {
    uint8_t number{0};
    uint8_t typeCode{0};
    FileSystem fileSystem{FileSystem::Unknown};
    MountState mountState{MountState::Unknown};
    uint64_t startSector{0};
    uint64_t sectorCount{0};
};

struct DiskInfo {
    char stableId[kStableIdBytes]{};
    char name[kDiskNameBytes]{};
    DiskTransport transport{DiskTransport::Unknown};
    uint8_t flags{0};
    uint32_t sectorSize{0};
    uint64_t capacityBytes{0};
    PartitionTableState partitionTable{PartitionTableState::Unknown};
    uint8_t partitionCount{0};
    uint8_t totalPartitionCount{0};
    PartitionInfo partitions[kMaxPartitionsPerDisk]{};
};

struct VolumeInfo {
    char stableId[kStableIdBytes]{};
    char mountPath[kMountPathBytes]{};
    char diskStableId[kStableIdBytes]{};
    FileSystem fileSystem{FileSystem::Unknown};
    uint8_t flags{0};
    uint64_t capacityBytes{0};
    uint64_t freeBytes{0};
};

struct StorageSnapshot {
    uint32_t version{kContractVersion};
    Backend backend{Backend::Unavailable};
    SnapshotState state{SnapshotState::Unavailable};
    bool truncated{false};
    uint64_t generation{0};
    uint16_t diskCount{0};
    uint16_t volumeCount{0};
    uint16_t totalDiskCount{0};
    uint16_t totalVolumeCount{0};
    DiskInfo disks[kMaxDisks]{};
    VolumeInfo volumes[kMaxVolumes]{};
};

inline bool hasTerminator(const char* text, size_t capacity)
{
    if (!text || capacity == 0) return false;
    for (size_t i = 0; i < capacity; ++i) if (text[i] == '\0') return true;
    return false;
}

inline bool copyText(char* output, size_t capacity, const char* input)
{
    if (!output || capacity == 0) return false;
    size_t i = 0;
    if (input) {
        while (input[i] && i + 1 < capacity) {
            output[i] = input[i];
            ++i;
        }
        output[i] = '\0';
        return input[i] == '\0';
    }
    output[0] = '\0';
    return true;
}

inline bool knownBackend(Backend backend)
{
    return backend == Backend::Unavailable || backend == Backend::Kernel ||
        backend == Backend::HostedTest;
}

inline bool knownSnapshotState(SnapshotState state)
{
    return state == SnapshotState::Unavailable || state == SnapshotState::Empty ||
        state == SnapshotState::Available;
}

inline bool validDevice(const DeviceInfo& item)
{
    return hasTerminator(item.stableId, sizeof(item.stableId)) &&
        hasTerminator(item.name, sizeof(item.name)) &&
        hasTerminator(item.driver, sizeof(item.driver)) &&
        hasTerminator(item.location, sizeof(item.location)) &&
        item.category <= DeviceCategory::Other && item.status <= DeviceStatus::Unavailable &&
        (item.flags & static_cast<uint8_t>(~(kDeviceFlagPciIdentity | kDeviceFlagTextTruncated))) == 0;
}

inline bool validSnapshotState(Backend backend, SnapshotState state,
                               uint16_t represented, uint16_t total,
                               bool truncated, uint64_t generation)
{
    if (!knownBackend(backend) || !knownSnapshotState(state) || generation == 0 || total < represented)
        return false;
    if (truncated != (total > represented)) return false;
    if (backend == Backend::Unavailable)
        return state == SnapshotState::Unavailable && represented == 0 && total == 0;
    if (state == SnapshotState::Unavailable) return represented == 0 && total == 0;
    if (state == SnapshotState::Empty) return represented == 0 && total == 0;
    return represented != 0;
}

inline bool validDeviceSnapshot(const DeviceSnapshot& snapshot)
{
    if (snapshot.version != kContractVersion || snapshot.deviceCount > kMaxDevices ||
        !validSnapshotState(snapshot.backend, snapshot.state, snapshot.deviceCount,
            snapshot.totalDeviceCount, snapshot.truncated, snapshot.generation)) return false;
    for (uint16_t i = 0; i < snapshot.deviceCount; ++i) {
        if (!validDevice(snapshot.devices[i]) || snapshot.devices[i].stableId[0] == '\0') return false;
        for (uint16_t j = 0; j < i; ++j) {
            size_t k = 0;
            while (snapshot.devices[i].stableId[k] &&
                snapshot.devices[i].stableId[k] == snapshot.devices[j].stableId[k]) ++k;
            if (snapshot.devices[i].stableId[k] == '\0' && snapshot.devices[j].stableId[k] == '\0') return false;
        }
    }
    return true;
}

inline bool knownDiskTransport(DiskTransport value)
{
    return value >= DiskTransport::Unknown && value <= DiskTransport::UsbMassStorage;
}

inline bool knownPartitionTable(PartitionTableState value)
{
    return value >= PartitionTableState::Unknown && value <= PartitionTableState::GptUnsupported;
}

inline bool knownFileSystem(FileSystem value)
{
    return value >= FileSystem::Unknown && value <= FileSystem::RamDisk;
}

inline bool validStorageSnapshot(const StorageSnapshot& snapshot)
{
    if (snapshot.version != kContractVersion || snapshot.diskCount > kMaxDisks ||
        snapshot.volumeCount > kMaxVolumes ||
        snapshot.totalDiskCount < snapshot.diskCount ||
        snapshot.totalVolumeCount < snapshot.volumeCount ||
        !validSnapshotState(snapshot.backend, snapshot.state,
            static_cast<uint16_t>(snapshot.diskCount + snapshot.volumeCount),
            static_cast<uint16_t>(snapshot.totalDiskCount + snapshot.totalVolumeCount),
            snapshot.truncated, snapshot.generation)) return false;
    if (snapshot.truncated != (snapshot.totalDiskCount > snapshot.diskCount ||
        snapshot.totalVolumeCount > snapshot.volumeCount)) return false;
    for (uint16_t i = 0; i < snapshot.diskCount; ++i) {
        const DiskInfo& disk = snapshot.disks[i];
        if (!hasTerminator(disk.stableId, sizeof(disk.stableId)) || disk.stableId[0] == '\0' ||
            !hasTerminator(disk.name, sizeof(disk.name)) || !knownDiskTransport(disk.transport) ||
            !knownPartitionTable(disk.partitionTable) || disk.partitionCount > kMaxPartitionsPerDisk ||
            disk.totalPartitionCount < disk.partitionCount) return false;
        if ((disk.flags & static_cast<uint8_t>(~(kDiskFlagCapacityAvailable | kDiskFlagWritable |
                kDiskFlagSectorSizeAvailable | kDiskFlagNameTruncated))) != 0) return false;
        if ((disk.flags & kDiskFlagCapacityAvailable) == 0 && disk.capacityBytes != 0) return false;
        if ((disk.flags & kDiskFlagSectorSizeAvailable) == 0 && disk.sectorSize != 0) return false;
        for (uint16_t j = 0; j < i; ++j) {
            size_t k = 0;
            while (disk.stableId[k] && disk.stableId[k] == snapshot.disks[j].stableId[k]) ++k;
            if (disk.stableId[k] == '\0' && snapshot.disks[j].stableId[k] == '\0') return false;
        }
        for (uint8_t p = 0; p < disk.partitionCount; ++p) {
            const PartitionInfo& part = disk.partitions[p];
            if (part.number == 0 || !knownFileSystem(part.fileSystem) ||
                part.mountState > MountState::Mounted || part.sectorCount == 0) return false;
        }
    }
    for (uint16_t i = 0; i < snapshot.volumeCount; ++i) {
        const VolumeInfo& volume = snapshot.volumes[i];
        if (!hasTerminator(volume.stableId, sizeof(volume.stableId)) || volume.stableId[0] == '\0' ||
            !hasTerminator(volume.mountPath, sizeof(volume.mountPath)) ||
            !hasTerminator(volume.diskStableId, sizeof(volume.diskStableId)) ||
            !knownFileSystem(volume.fileSystem) ||
            (volume.flags & static_cast<uint8_t>(~(kVolumeFlagReadOnly | kVolumeFlagCapacityAvailable |
                kVolumeFlagFreeSpaceAvailable | kVolumeFlagPathTruncated))) != 0 ||
            ((volume.flags & kVolumeFlagCapacityAvailable) == 0 && volume.capacityBytes != 0) ||
            ((volume.flags & kVolumeFlagFreeSpaceAvailable) == 0 && volume.freeBytes != 0) ||
            ((volume.flags & kVolumeFlagFreeSpaceAvailable) != 0 &&
                ((volume.flags & kVolumeFlagCapacityAvailable) == 0 || volume.freeBytes > volume.capacityBytes)))
            return false;
        for (uint16_t j = 0; j < i; ++j) {
            size_t k = 0;
            while (volume.stableId[k] && volume.stableId[k] == snapshot.volumes[j].stableId[k]) ++k;
            if (volume.stableId[k] == '\0' && snapshot.volumes[j].stableId[k] == '\0') return false;
        }
    }
    return true;
}

inline void appendDevice(DeviceSnapshot& snapshot, const DeviceInfo& device)
{
    if (snapshot.totalDeviceCount != UINT16_MAX) ++snapshot.totalDeviceCount;
    if (snapshot.deviceCount < kMaxDevices) snapshot.devices[snapshot.deviceCount++] = device;
    else snapshot.truncated = true;
}

inline void appendDisk(StorageSnapshot& snapshot, const DiskInfo& disk)
{
    if (snapshot.totalDiskCount != UINT16_MAX) ++snapshot.totalDiskCount;
    if (snapshot.diskCount < kMaxDisks) snapshot.disks[snapshot.diskCount++] = disk;
    else snapshot.truncated = true;
}

inline void appendVolume(StorageSnapshot& snapshot, const VolumeInfo& volume)
{
    if (snapshot.totalVolumeCount != UINT16_MAX) ++snapshot.totalVolumeCount;
    if (snapshot.volumeCount < kMaxVolumes) snapshot.volumes[snapshot.volumeCount++] = volume;
    else snapshot.truncated = true;
}

inline void finishDeviceSnapshot(DeviceSnapshot& snapshot)
{
    if (snapshot.backend == Backend::Unavailable) snapshot.state = SnapshotState::Unavailable;
    else snapshot.state = snapshot.deviceCount == 0 ? SnapshotState::Empty : SnapshotState::Available;
}

inline void finishStorageSnapshot(StorageSnapshot& snapshot)
{
    if (snapshot.backend == Backend::Unavailable) snapshot.state = SnapshotState::Unavailable;
    else snapshot.state = snapshot.diskCount == 0 && snapshot.volumeCount == 0
        ? SnapshotState::Empty : SnapshotState::Available;
}

} // namespace settings_inventory
} // namespace gxos
