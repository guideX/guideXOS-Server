#include "settings_center_model.h"

#include <cstring>
#include <iostream>
#include <string>

using namespace gxos::settings_inventory;
using namespace gxos::apps::settings;

namespace {
int deviceChecks = 0;
int deviceFailures = 0;
int storageChecks = 0;
int storageFailures = 0;

void checkDevice(bool condition, const char* name)
{
    ++deviceChecks;
    if (!condition) {
        ++deviceFailures;
        std::cerr << "FAIL device: " << name << "\n";
    }
}

void checkStorage(bool condition, const char* name)
{
    ++storageChecks;
    if (!condition) {
        ++storageFailures;
        std::cerr << "FAIL storage: " << name << "\n";
    }
}

DeviceSnapshot emptyDevices()
{
    DeviceSnapshot snapshot{};
    snapshot.backend = Backend::Kernel;
    snapshot.state = SnapshotState::Empty;
    snapshot.generation = 1;
    return snapshot;
}

StorageSnapshot emptyStorage()
{
    StorageSnapshot snapshot{};
    snapshot.backend = Backend::Kernel;
    snapshot.state = SnapshotState::Empty;
    snapshot.generation = 1;
    return snapshot;
}

void setDevice(DeviceInfo& device, const char* id, const char* name,
               DeviceCategory category, DeviceStatus status)
{
    copyText(device.stableId, sizeof(device.stableId), id);
    copyText(device.name, sizeof(device.name), name);
    device.category = category;
    device.status = status;
}

void setDisk(DiskInfo& disk, const char* id, const char* name)
{
    copyText(disk.stableId, sizeof(disk.stableId), id);
    copyText(disk.name, sizeof(disk.name), name);
}
}

int main()
{
    DeviceSnapshot devices = emptyDevices();
    checkDevice(validDeviceSnapshot(devices), "zero-device kernel snapshot is a valid empty state");

    devices.state = SnapshotState::Available;
    devices.deviceCount = devices.totalDeviceCount = 1;
    setDevice(devices.devices[0], "pci:00:03.0", "PCI device 8086:100E",
        DeviceCategory::Network, DeviceStatus::DriverLoaded);
    devices.devices[0].flags = kDeviceFlagPciIdentity;
    checkDevice(validDeviceSnapshot(devices), "one PCI device with fallback identity is valid");
    checkDevice(std::string(deviceCategoryName(devices.devices[0].category)) == "Network",
        "network category has a Settings label");
    checkDevice(std::string(deviceStatusName(devices.devices[0].status)) == "Driver loaded",
        "driver-loaded state is shown without claiming working health");

    DeviceInfo noDriver{};
    setDevice(noDriver, "usb:00:01", "Unrecognized USB device", DeviceCategory::Usb,
        DeviceStatus::NoDriver);
    DeviceInfo unknown{};
    setDevice(unknown, "pci:01:00.0", "PCI 1234:ABCD", DeviceCategory::Other,
        DeviceStatus::Unknown);
    checkDevice(std::string(deviceCategoryName(unknown.category)) == "Other device" &&
        std::string(deviceStatusName(noDriver.status)) == "No driver" &&
        std::string(deviceStatusName(DeviceStatus::Unavailable)) == "Unavailable" &&
        std::string(deviceStatusName(DeviceStatus::Unknown)) == "Status unavailable",
        "unknown identity and no-driver, unavailable, and unknown statuses stay explicit");
    DeviceSnapshot multipleCategories = emptyDevices();
    multipleCategories.state = SnapshotState::Available;
    setDevice(multipleCategories.devices[0], "net:0", "Network adapter",
        DeviceCategory::Network, DeviceStatus::DriverLoaded);
    setDevice(multipleCategories.devices[1], "usb:0", "USB device",
        DeviceCategory::Usb, DeviceStatus::NoDriver);
    setDevice(multipleCategories.devices[2], "pci:1", "Other PCI device",
        DeviceCategory::Other, DeviceStatus::Unknown);
    multipleCategories.deviceCount = multipleCategories.totalDeviceCount = 3;
    checkDevice(validDeviceSnapshot(multipleCategories) &&
        multipleCategories.devices[0].category != multipleCategories.devices[1].category &&
        multipleCategories.devices[1].category != multipleCategories.devices[2].category,
        "multiple known and unknown device categories coexist in one bounded snapshot");
    checkDevice(deviceMatchesFilter(DeviceCategory::Usb, TargetId::DevicesUsb) &&
        deviceMatchesFilter(DeviceCategory::Network, TargetId::DevicesNetwork) &&
        !deviceMatchesFilter(DeviceCategory::Audio, TargetId::DevicesNetwork),
        "device category filters match only the requested category");
    const SearchResultSet keyboardSearch = searchSettings("keyboard");
    checkDevice(keyboardSearch.count > 0 &&
        keyboardSearch.values[0].route.category == CategoryId::Devices &&
        keyboardSearch.values[0].route.target == TargetId::DevicesInput,
        "keyboard search maps to the implemented input-device view");
    const SearchResultSet usbSearch = searchSettings("usb");
    checkDevice(usbSearch.count > 0 &&
        usbSearch.values[0].route.category == CategoryId::Devices &&
        usbSearch.values[0].route.target == TargetId::DevicesUsb,
        "USB search maps to the implemented USB-device view");

    SettingsRoute route{};
    checkDevice(parseSettingsRoute("settings://devices/input", route) &&
        route.category == CategoryId::Devices && route.target == TargetId::DevicesInput,
        "input deep link resolves to an implemented Devices filter");
    checkDevice(parseSettingsRoute("settings://devices/network", route) &&
        route.target == TargetId::DevicesNetwork,
        "network adapter deep link resolves to its Devices filter");
    checkDevice(parseSettingsRoute("settings://devices/display", route) &&
        route.target == TargetId::DevicesDisplay,
        "display adapter deep link resolves to its Devices filter");
    checkDevice(parseSettingsRoute("settings://devices/storage", route) &&
        route.target == TargetId::DevicesStorage,
        "storage device deep link resolves to its Devices filter");
    checkDevice(parseSettingsRoute("settings://devices/audio", route) &&
        route.target == TargetId::DevicesAudio,
        "audio deep link resolves to its Devices filter");
    checkDevice(parseSettingsRoute("settings://devices/usb", route) &&
        route.target == TargetId::DevicesUsb,
        "USB deep link resolves to its Devices filter");
    checkDevice(parseSettingsRoute("settings://devices/other", route) &&
        route.target == TargetId::DevicesOther,
        "other-device deep link resolves to its Devices filter");
    checkDevice(!parseSettingsRoute("settings://devices/driver-install", route),
        "unimplemented driver administration is not advertised as a route");

    char longName[160];
    std::memset(longName, 'N', sizeof(longName));
    longName[sizeof(longName) - 1] = '\0';
    DeviceInfo boundedDevice{};
    const bool fullNameCopied = copyText(boundedDevice.name, sizeof(boundedDevice.name), longName);
    boundedDevice.flags |= kDeviceFlagTextTruncated;
    setDevice(boundedDevice, "long-name-device", boundedDevice.name,
        DeviceCategory::Other, DeviceStatus::Unknown);
    checkDevice(!fullNameCopied && hasTerminator(boundedDevice.name, sizeof(boundedDevice.name)) &&
        (boundedDevice.flags & kDeviceFlagTextTruncated) && validDevice(boundedDevice),
        "long device names stay terminated, bounded, and marked as truncated");

    DeviceSnapshot fullDevices{};
    fullDevices.backend = Backend::Kernel;
    fullDevices.state = SnapshotState::Available;
    fullDevices.generation = 8;
    for (size_t i = 0; i < kMaxDevices + 1; ++i) {
        DeviceInfo item{};
        const std::string id = "device:" + std::to_string(i);
        copyText(item.stableId, sizeof(item.stableId), id.c_str());
        copyText(item.name, sizeof(item.name), "Bounded test device");
        appendDevice(fullDevices, item);
    }
    finishDeviceSnapshot(fullDevices);
    fullDevices.generation = 8;
    checkDevice(fullDevices.deviceCount == kMaxDevices && fullDevices.totalDeviceCount == kMaxDevices + 1 &&
        fullDevices.truncated && validDeviceSnapshot(fullDevices),
        "device capacity overflow is bounded and reports truncation");
    checkDevice(selectionGenerationMatches(8, 8) && !selectionGenerationMatches(8, 9) &&
        !selectionGenerationMatches(8, 0),
        "generation change or disappearance invalidates a selected device");
    checkDevice(std::string(deviceStatusName(DeviceStatus::Disconnected)) == "Disconnected",
        "disconnected is represented only as an explicit provider state");

    StorageSnapshot storage = emptyStorage();
    checkStorage(validStorageSnapshot(storage), "zero-disk zero-volume kernel snapshot is valid");

    storage.state = SnapshotState::Available;
    storage.diskCount = storage.totalDiskCount = 1;
    DiskInfo& disk = storage.disks[0];
    setDisk(disk, "ata:0:1", "Disk 0");
    disk.transport = DiskTransport::AtaPio;
    disk.flags = kDiskFlagCapacityAvailable | kDiskFlagSectorSizeAvailable | kDiskFlagWritable;
    disk.capacityBytes = 20ull * 1024ull * 1024ull * 1024ull;
    disk.sectorSize = 512;
    disk.partitionTable = PartitionTableState::NoMbr;
    checkStorage(validStorageSnapshot(storage), "one fixed writable disk with capacity and sector size is valid");
    checkStorage((disk.flags & kDiskFlagWritable) && disk.sectorSize == 512 && disk.partitionCount == 0,
        "disk capability, sector size, and zero-partition state are represented directly");
    checkStorage(std::string(diskTransportName(DiskTransport::AtaPio)) == "ATA PIO" &&
        std::string(partitionTableName(PartitionTableState::GptUnsupported)) ==
            "GPT detected; details unavailable",
        "transport and unsupported GPT labels expose current detail boundaries");

    DiskInfo removable{};
    setDisk(removable, "usb:2:1", "USB storage");
    removable.transport = DiskTransport::UsbMassStorage;
    removable.flags = kDiskFlagCapacityAvailable | kDiskFlagSectorSizeAvailable;
    removable.capacityBytes = 8ull * 1024ull * 1024ull;
    removable.sectorSize = 4096;
    removable.partitionTable = PartitionTableState::ValidMbr;
    removable.partitionCount = removable.totalPartitionCount = 2;
    removable.partitions[0] = PartitionInfo{ 1, 0x0B, FileSystem::Fat32,
        MountState::Unknown, 2048, 4096 };
    removable.partitions[1] = PartitionInfo{ 2, 0x83, FileSystem::Unknown,
        MountState::Unknown, 6144, 1024 };
    storage.diskCount = storage.totalDiskCount = 2;
    storage.disks[1] = removable;
    checkStorage(validStorageSnapshot(storage), "multiple disks include removable transport and two MBR partitions");
    checkStorage(removable.partitions[0].fileSystem == FileSystem::Fat32 &&
        removable.partitions[1].fileSystem == FileSystem::Unknown &&
        removable.partitions[0].mountState == MountState::Unknown,
        "known and unknown filesystems keep mount state unavailable when association is unknown");
    DiskInfo boundedPartitions = removable;
    boundedPartitions.partitionCount = static_cast<uint8_t>(kMaxPartitionsPerDisk + 1);
    StorageSnapshot invalidPartitions = emptyStorage();
    invalidPartitions.state = SnapshotState::Available;
    invalidPartitions.diskCount = invalidPartitions.totalDiskCount = 1;
    invalidPartitions.disks[0] = boundedPartitions;
    checkStorage(!validStorageSnapshot(invalidPartitions),
        "partition count beyond the fixed per-disk capacity is rejected");
    boundedPartitions.partitionCount = static_cast<uint8_t>(kMaxPartitionsPerDisk);
    boundedPartitions.totalPartitionCount = static_cast<uint8_t>(kMaxPartitionsPerDisk + 1);
    boundedPartitions.capacityBytes = 64ull * 1024ull * 1024ull;
    for (uint8_t i = 0; i < kMaxPartitionsPerDisk; ++i) {
        boundedPartitions.partitions[i] = PartitionInfo{ static_cast<uint8_t>(i + 1), 0x0B,
            FileSystem::Unknown, MountState::Unknown, static_cast<uint64_t>(i + 1) * 2048, 1024 };
    }
    StorageSnapshot truncatedPartitions{};
    truncatedPartitions.backend = Backend::Kernel;
    truncatedPartitions.state = SnapshotState::Available;
    truncatedPartitions.generation = 1;
    truncatedPartitions.diskCount = truncatedPartitions.totalDiskCount = 1;
    truncatedPartitions.disks[0] = boundedPartitions;
    checkStorage(validStorageSnapshot(truncatedPartitions) &&
        truncatedPartitions.disks[0].totalPartitionCount > truncatedPartitions.disks[0].partitionCount,
        "bounded partition list preserves the authoritative total partition count");
    checkStorage(std::string(fileSystemName(FileSystem::Fat32)) == "FAT32" &&
        std::string(fileSystemName(FileSystem::Unknown)) == "Filesystem unavailable",
        "known and unknown filesystem labels remain truthful");
    checkStorage(std::string(formatByteSize(0)) == "Unavailable" &&
        std::string(formatByteSize(1023)) == "1023 B" &&
        std::string(formatByteSize(1024)) == "1.0 KiB",
        "byte formatting covers zero and the KiB boundary");
    checkStorage(std::string(formatByteSize(1024ull * 1024ull - 1)) == "1024.0 KiB" &&
        std::string(formatByteSize(1024ull * 1024ull)) == "1.0 MiB",
        "byte formatting rounds immediately below and at the MiB boundary deterministically");
    checkStorage(std::string(formatByteSize(1024ull * 1024ull * 1024ull)) == "1.0 GiB" &&
        std::string(formatByteSize(1024ull * 1024ull * 1024ull * 1024ull)) == "1.0 TiB",
        "byte formatting covers the GiB and TiB boundaries");
    checkStorage(!formatByteSize(UINT64_MAX).empty() && formatByteSize(UINT64_MAX) != "Unavailable",
        "maximum uint64 capacity formats without arithmetic overflow");

    storage.volumeCount = storage.totalVolumeCount = 1;
    VolumeInfo& volume = storage.volumes[0];
    copyText(volume.stableId, sizeof(volume.stableId), "mount:0");
    copyText(volume.mountPath, sizeof(volume.mountPath), "/system");
    copyText(volume.diskStableId, sizeof(volume.diskStableId), "ata:0:1");
    volume.fileSystem = FileSystem::Fat32;
    volume.flags = kVolumeFlagCapacityAvailable | kVolumeFlagReadOnly;
    volume.capacityBytes = disk.capacityBytes;
    checkStorage(validStorageSnapshot(storage) && !(volume.flags & kVolumeFlagFreeSpaceAvailable),
        "mounted volume without reliable free-space data omits usage values");
    volume.flags |= kVolumeFlagFreeSpaceAvailable;
    volume.freeBytes = 5ull * 1024ull * 1024ull * 1024ull;
    checkStorage(validStorageSnapshot(storage), "reliable free-space fields are accepted when bounded by capacity");
    volume.freeBytes = volume.capacityBytes + 1;
    checkStorage(!validStorageSnapshot(storage), "free space greater than volume capacity is rejected");
    volume.freeBytes = 0;
    volume.flags &= static_cast<uint8_t>(~kVolumeFlagFreeSpaceAvailable);

    char longPath[256];
    std::memset(longPath, 'P', sizeof(longPath));
    longPath[sizeof(longPath) - 1] = '\0';
    const bool fullPathCopied = copyText(volume.mountPath, sizeof(volume.mountPath), longPath);
    volume.flags |= kVolumeFlagPathTruncated;
    checkStorage(!fullPathCopied && hasTerminator(volume.mountPath, sizeof(volume.mountPath)) &&
        (volume.flags & kVolumeFlagPathTruncated),
        "long mount paths stay bounded and carry a truncation marker");

    char longDiskName[128];
    std::memset(longDiskName, 'M', sizeof(longDiskName));
    longDiskName[sizeof(longDiskName) - 1] = '\0';
    const bool fullDiskNameCopied = copyText(disk.name, sizeof(disk.name), longDiskName);
    disk.flags |= kDiskFlagNameTruncated;
    checkStorage(!fullDiskNameCopied && hasTerminator(disk.name, sizeof(disk.name)) &&
        (disk.flags & kDiskFlagNameTruncated),
        "long disk labels remain bounded and carry a truncation marker");

    StorageSnapshot fullDisks{};
    fullDisks.backend = Backend::Kernel;
    fullDisks.state = SnapshotState::Available;
    fullDisks.generation = 12;
    for (size_t i = 0; i < kMaxDisks + 1; ++i) {
        DiskInfo item{};
        const std::string id = "disk:" + std::to_string(i);
        setDisk(item, id.c_str(), "Bounded test disk");
        item.partitionTable = PartitionTableState::Unknown;
        appendDisk(fullDisks, item);
    }
    finishStorageSnapshot(fullDisks);
    fullDisks.generation = 12;
    checkStorage(fullDisks.diskCount == kMaxDisks && fullDisks.totalDiskCount == kMaxDisks + 1 &&
        fullDisks.truncated && validStorageSnapshot(fullDisks),
        "disk capacity overflow is bounded and reports truncation");
    checkStorage(selectionGenerationMatches(12, 12) && !selectionGenerationMatches(12, 13),
        "storage generation change invalidates selected disk detail");

    SettingsRoute storageRoute{};
    checkStorage(parseSettingsRoute("settings://storage/disks", storageRoute) &&
        storageRoute.category == CategoryId::Storage && storageRoute.target == TargetId::StorageDisks,
        "disk and partition deep link reaches the implemented storage list");
    checkStorage(parseSettingsRoute("settings://storage/volumes", storageRoute) &&
        storageRoute.category == CategoryId::Storage && storageRoute.target == TargetId::StorageVolumes,
        "volume deep link reaches the implemented volume list");
    const SearchResultSet volumeSearch = searchSettings("volume");
    checkStorage(volumeSearch.count > 0 &&
        volumeSearch.values[0].route.category == CategoryId::Storage &&
        volumeSearch.values[0].route.target == TargetId::StorageVolumes,
        "volume search maps to the implemented mounted-volume view");
    const char* launchName = nullptr;
    checkStorage(builtInAdvancedTarget(CategoryId::Storage, launchName) && launchName &&
        std::string(launchName) == "DiskManager",
        "Storage advanced action routes to Disk Manager");

    std::cout << "Device model tests: " << (deviceChecks - deviceFailures) << "/" << deviceChecks << " passed\n";
    std::cout << "Storage model tests: " << (storageChecks - storageFailures) << "/" << storageChecks << " passed\n";
    return deviceFailures == 0 && storageFailures == 0 ? 0 : 1;
}
