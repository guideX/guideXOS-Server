#include "include/kernel/settings_inventory_provider.h"

#include "include/kernel/block_device.h"
#include "include/kernel/nic.h"
#include "include/kernel/network_settings_provider.h"
#include "include/kernel/pci_audio.h"
#include "include/kernel/usb.h"
#include "include/kernel/vesa.h"
#include "include/kernel/vfs.h"
#include "include/kernel/virtio_gpu.h"

#include "system_service_protocol.h"

namespace kernel {
namespace settings_inventory_provider {
namespace {

using namespace gxos::settings_inventory;
using gxos::system_service::ResponseStatus;

bool sameText(const char* left, const char* right, size_t capacity)
{
    for (size_t i = 0; i < capacity; ++i) {
        if (left[i] != right[i]) return false;
        if (left[i] == '\0') return true;
    }
    return true;
}

bool copyFixedText(char* output, size_t outputCapacity,
                   const char* input, size_t inputCapacity)
{
    if (!output || outputCapacity == 0) return false;
    size_t i = 0;
    while (input && i < inputCapacity && input[i] && i + 1 < outputCapacity) {
        output[i] = input[i];
        ++i;
    }
    output[i] = '\0';
    return input && i < inputCapacity && input[i] == '\0';
}

bool sameDevice(const DeviceInfo& a, const DeviceInfo& b)
{
    return sameText(a.stableId, b.stableId, sizeof(a.stableId)) &&
        sameText(a.name, b.name, sizeof(a.name)) &&
        sameText(a.driver, b.driver, sizeof(a.driver)) &&
        sameText(a.location, b.location, sizeof(a.location)) &&
        a.category == b.category && a.status == b.status && a.vendorId == b.vendorId &&
        a.deviceId == b.deviceId && a.bus == b.bus && a.slot == b.slot &&
        a.function == b.function && a.flags == b.flags;
}

bool sameDeviceSnapshot(const DeviceSnapshot& a, const DeviceSnapshot& b)
{
    if (a.version != b.version || a.backend != b.backend || a.state != b.state ||
        a.truncated != b.truncated || a.deviceCount != b.deviceCount ||
        a.totalDeviceCount != b.totalDeviceCount) return false;
    for (uint16_t i = 0; i < a.deviceCount; ++i)
        if (!sameDevice(a.devices[i], b.devices[i])) return false;
    return true;
}

bool samePartition(const PartitionInfo& a, const PartitionInfo& b)
{
    return a.number == b.number && a.typeCode == b.typeCode &&
        a.fileSystem == b.fileSystem && a.mountState == b.mountState &&
        a.startSector == b.startSector && a.sectorCount == b.sectorCount;
}

bool sameDisk(const DiskInfo& a, const DiskInfo& b)
{
    if (!sameText(a.stableId, b.stableId, sizeof(a.stableId)) ||
        !sameText(a.name, b.name, sizeof(a.name)) || a.transport != b.transport ||
        a.flags != b.flags || a.sectorSize != b.sectorSize ||
        a.capacityBytes != b.capacityBytes || a.partitionTable != b.partitionTable ||
        a.partitionCount != b.partitionCount || a.totalPartitionCount != b.totalPartitionCount) return false;
    for (uint8_t i = 0; i < a.partitionCount; ++i)
        if (!samePartition(a.partitions[i], b.partitions[i])) return false;
    return true;
}

bool sameVolume(const VolumeInfo& a, const VolumeInfo& b)
{
    return sameText(a.stableId, b.stableId, sizeof(a.stableId)) &&
        sameText(a.mountPath, b.mountPath, sizeof(a.mountPath)) &&
        sameText(a.diskStableId, b.diskStableId, sizeof(a.diskStableId)) &&
        a.fileSystem == b.fileSystem && a.flags == b.flags &&
        a.capacityBytes == b.capacityBytes && a.freeBytes == b.freeBytes;
}

bool sameStorageSnapshot(const StorageSnapshot& a, const StorageSnapshot& b)
{
    if (a.version != b.version || a.backend != b.backend || a.state != b.state ||
        a.truncated != b.truncated || a.diskCount != b.diskCount ||
        a.volumeCount != b.volumeCount || a.totalDiskCount != b.totalDiskCount ||
        a.totalVolumeCount != b.totalVolumeCount) return false;
    for (uint16_t i = 0; i < a.diskCount; ++i)
        if (!sameDisk(a.disks[i], b.disks[i])) return false;
    for (uint16_t i = 0; i < a.volumeCount; ++i)
        if (!sameVolume(a.volumes[i], b.volumes[i])) return false;
    return true;
}

void updateDeviceGeneration(DeviceSnapshot& snapshot)
{
    static DeviceSnapshot previous{};
    static bool hasPrevious = false;
    if (!hasPrevious) {
        snapshot.generation = 1;
        previous = snapshot;
        hasPrevious = true;
    } else if (sameDeviceSnapshot(snapshot, previous)) {
        snapshot.generation = previous.generation;
    } else {
        snapshot.generation = previous.generation + 1;
        if (snapshot.generation == 0) snapshot.generation = 1;
        previous = snapshot;
    }
}

void updateStorageGeneration(StorageSnapshot& snapshot)
{
    static StorageSnapshot previous{};
    static bool hasPrevious = false;
    if (!hasPrevious) {
        snapshot.generation = 1;
        previous = snapshot;
        hasPrevious = true;
    } else if (sameStorageSnapshot(snapshot, previous)) {
        snapshot.generation = previous.generation;
    } else {
        snapshot.generation = previous.generation + 1;
        if (snapshot.generation == 0) snapshot.generation = 1;
        previous = snapshot;
    }
}

void appendHex(char* output, size_t capacity, size_t& offset, uint32_t value, size_t digits)
{
    static const char kHex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < digits && offset + 1 < capacity; ++i) {
        const size_t shift = (digits - i - 1) * 4;
        output[offset++] = kHex[(value >> shift) & 0xFu];
    }
    if (capacity) output[offset < capacity ? offset : capacity - 1] = '\0';
}

void appendHex64(char* output, size_t capacity, size_t& offset, uint64_t value)
{
    appendHex(output, capacity, offset, static_cast<uint32_t>(value >> 32), 8);
    appendHex(output, capacity, offset, static_cast<uint32_t>(value), 8);
}

void appendDecimal(char* output, size_t capacity, size_t& offset, uint32_t value)
{
    char digits[10]{};
    size_t count = 0;
    do { digits[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value && count < sizeof(digits));
    while (count && offset + 1 < capacity) output[offset++] = digits[--count];
    if (capacity) output[offset < capacity ? offset : capacity - 1] = '\0';
}

void makePciLocation(uint8_t bus, uint8_t slot, uint8_t function, char* output, size_t capacity)
{
    if (!output || capacity == 0) return;
    output[0] = '\0';
    size_t at = 0;
    const char prefix[] = "PCI ";
    for (size_t i = 0; i < sizeof(prefix) - 1 && at + 1 < capacity; ++i) output[at++] = prefix[i];
    appendHex(output, capacity, at, bus, 2);
    if (at + 1 < capacity) output[at++] = ':';
    appendHex(output, capacity, at, slot, 2);
    if (at + 1 < capacity) output[at++] = '.';
    appendDecimal(output, capacity, at, function);
    if (at < capacity) output[at] = '\0';
}

void makeUsbLocation(uint8_t address, char* output, size_t capacity)
{
    if (!output || capacity == 0) return;
    output[0] = '\0';
    const char prefix[] = "USB address ";
    size_t at = 0;
    for (size_t i = 0; i < sizeof(prefix) - 1 && at + 1 < capacity; ++i) output[at++] = prefix[i];
    appendDecimal(output, capacity, at, address);
}

void setPciIdentity(DeviceInfo& item, uint16_t vendor, uint16_t device,
                    uint8_t bus, uint8_t slot, uint8_t function)
{
    item.vendorId = vendor;
    item.deviceId = device;
    item.bus = bus;
    item.slot = slot;
    item.function = function;
    item.flags |= kDeviceFlagPciIdentity;
    makePciLocation(bus, slot, function, item.location, sizeof(item.location));
}

void setPciName(DeviceInfo& item, const char* prefix, uint16_t vendor, uint16_t device)
{
    size_t at = 0;
    copyText(item.name, sizeof(item.name), prefix);
    while (item.name[at] && at + 1 < sizeof(item.name)) ++at;
    if (at + 1 < sizeof(item.name)) item.name[at++] = ' ';
    appendHex(item.name, sizeof(item.name), at, vendor, 4);
    if (at + 1 < sizeof(item.name)) item.name[at++] = ':';
    appendHex(item.name, sizeof(item.name), at, device, 4);
    if (at < sizeof(item.name)) item.name[at] = '\0';
}

const char* blockTransportName(block::DeviceType type)
{
    switch (type) {
    case block::BDEV_ATA_PIO: return "ATA PIO";
    case block::BDEV_AHCI: return "AHCI";
    case block::BDEV_NVME: return "NVMe";
    case block::BDEV_USB_MASS: return "USB Mass Storage";
    default: return "Unknown";
    }
}

DiskTransport blockTransport(block::DeviceType type)
{
    switch (type) {
    case block::BDEV_ATA_PIO: return DiskTransport::AtaPio;
    case block::BDEV_AHCI: return DiskTransport::Ahci;
    case block::BDEV_NVME: return DiskTransport::Nvme;
    case block::BDEV_USB_MASS: return DiskTransport::UsbMassStorage;
    default: return DiskTransport::Unknown;
    }
}

FileSystem fileSystem(vfs::FSType type)
{
    switch (type) {
    case vfs::FS_TYPE_FAT32: return FileSystem::Fat32;
    case vfs::FS_TYPE_EXFAT: return FileSystem::ExFat;
    case vfs::FS_TYPE_EXT2: return FileSystem::Ext2;
    case vfs::FS_TYPE_EXT4: return FileSystem::Ext4;
    case vfs::FS_TYPE_UFS: return FileSystem::Ufs;
    case vfs::FS_TYPE_ISO9660: return FileSystem::Iso9660;
    case vfs::FS_TYPE_RAMDISK: return FileSystem::RamDisk;
    default: return FileSystem::Unknown;
    }
}

void makeBlockStableId(uint8_t index, const block::BlockDevice& dev,
                       char* output, size_t capacity)
{
    if (!output || capacity == 0) return;
    output[0] = '\0';
    const char prefix[] = "block:";
    size_t at = 0;
    for (size_t i = 0; i < sizeof(prefix) - 1 && at + 1 < capacity; ++i) output[at++] = prefix[i];
    appendDecimal(output, capacity, at, static_cast<uint32_t>(dev.type));
    if (at + 1 < capacity) output[at++] = ':';
    appendDecimal(output, capacity, at, dev.driverIndex);
    if (at + 1 < capacity) output[at++] = ':';
    appendDecimal(output, capacity, at, index);
    if (at + 1 < capacity) output[at++] = ':';
    appendHex64(output, capacity, at, block::device_generation(index));
    if (at < capacity) output[at] = '\0';
}

uint32_t readLe32(const uint8_t* value)
{
    return static_cast<uint32_t>(value[0]) |
        (static_cast<uint32_t>(value[1]) << 8) |
        (static_cast<uint32_t>(value[2]) << 16) |
        (static_cast<uint32_t>(value[3]) << 24);
}

void readMbr(const block::BlockDevice& dev, uint8_t index, DiskInfo& disk)
{
    if (!dev.readFn || dev.sectorSize < 512 || dev.sectorSize > 4096 || dev.totalSectors == 0) {
        disk.partitionTable = PartitionTableState::Unreadable;
        return;
    }
    uint8_t sector[4096]{};
    if (block::read_sectors(index, 0, 1, sector) != block::BLOCK_OK) {
        disk.partitionTable = PartitionTableState::Unreadable;
        return;
    }
    if (sector[510] != 0x55u || sector[511] != 0xAAu) {
        disk.partitionTable = PartitionTableState::NoMbr;
        return;
    }
    bool protectiveGpt = false;
    for (uint8_t slot = 0; slot < 4; ++slot) {
        const uint8_t* entry = sector + 446 + static_cast<size_t>(slot) * 16;
        const uint8_t bootFlag = entry[0];
        const uint8_t type = entry[4];
        const uint64_t start = readLe32(entry + 8);
        const uint64_t sectors = readLe32(entry + 12);
        if (type == 0 && sectors == 0) continue;
        if (type == 0xEEu) { protectiveGpt = true; continue; }
        if ((bootFlag != 0 && bootFlag != 0x80u) || type == 0 || sectors == 0 ||
            start >= dev.totalSectors || sectors > dev.totalSectors - start) {
            disk.partitionTable = PartitionTableState::InvalidMbr;
            disk.partitionCount = 0;
            disk.totalPartitionCount = 0;
            return;
        }
        if (disk.totalPartitionCount != 0xFFu) ++disk.totalPartitionCount;
        if (disk.partitionCount < kMaxPartitionsPerDisk) {
            PartitionInfo& part = disk.partitions[disk.partitionCount++];
            part.number = static_cast<uint8_t>(slot + 1);
            part.typeCode = type;
            part.startSector = start;
            part.sectorCount = sectors;
            part.fileSystem = FileSystem::Unknown;
            part.mountState = MountState::Unknown;
        }
    }
    disk.partitionTable = protectiveGpt ? PartitionTableState::GptUnsupported :
        PartitionTableState::ValidMbr;
    if (protectiveGpt) {
        disk.partitionCount = 0;
        disk.totalPartitionCount = 0;
    }
}

void appendBlockDevice(DeviceSnapshot& snapshot, uint8_t index,
                       const block::BlockDevice& dev)
{
    DeviceInfo item{};
    makeBlockStableId(index, dev, item.stableId, sizeof(item.stableId));
    if (!copyFixedText(item.name, sizeof(item.name), dev.name, sizeof(dev.name)))
        item.flags |= kDeviceFlagTextTruncated;
    const char* transport = blockTransportName(dev.type);
    if (!copyText(item.driver, sizeof(item.driver), transport)) item.flags |= kDeviceFlagTextTruncated;
    item.category = DeviceCategory::Storage;
    item.status = dev.readFn ? DeviceStatus::DriverLoaded : DeviceStatus::Unknown;
    appendDevice(snapshot, item);
}

DeviceCategory usbCategory(const usb::Device& dev)
{
    for (uint8_t i = 0; i < dev.numInterfaces && i < usb::MAX_INTERFACES_PER_DEVICE; ++i) {
        switch (dev.interfaceClass[i]) {
        case 0x03: return DeviceCategory::Input;
        case 0x01: return DeviceCategory::Audio;
        case 0x08: return DeviceCategory::Storage;
        case 0x02:
        case 0x0A: return DeviceCategory::Network;
        case 0x0E: return DeviceCategory::Display;
        default: break;
        }
    }
    return DeviceCategory::Usb;
}

void appendUsbDevices(DeviceSnapshot& snapshot)
{
    for (uint16_t address = 1; address <= 127; ++address) {
        const usb::Device* dev = usb::get_device(static_cast<uint8_t>(address));
        if (!dev || !dev->present) continue;
        DeviceInfo item{};
        size_t at = 0;
        const char prefix[] = "usb:";
        for (size_t i = 0; i < sizeof(prefix) - 1 && at + 1 < sizeof(item.stableId); ++i)
            item.stableId[at++] = prefix[i];
        appendDecimal(item.stableId, sizeof(item.stableId), at, address);
        if (at + 1 < sizeof(item.stableId)) item.stableId[at++] = ':';
        appendHex(item.stableId, sizeof(item.stableId), at, dev->devDesc.idVendor, 4);
        if (at + 1 < sizeof(item.stableId)) item.stableId[at++] = ':';
        appendHex(item.stableId, sizeof(item.stableId), at, dev->devDesc.idProduct, 4);
        if (at < sizeof(item.stableId)) item.stableId[at] = '\0';
        setPciName(item, "USB device", dev->devDesc.idVendor, dev->devDesc.idProduct);
        item.category = usbCategory(*dev);
        item.status = DeviceStatus::Unknown;
        makeUsbLocation(static_cast<uint8_t>(address), item.location, sizeof(item.location));
        appendDevice(snapshot, item);
    }
}

uint64_t calculateBytes(uint64_t sectors, uint32_t sectorSize, bool* available)
{
    if (available) *available = false;
    if (sectorSize == 0 || sectors > UINT64_MAX / sectorSize) return 0;
    if (available) *available = true;
    return sectors * sectorSize;
}

void makeVolumeStableId(uint8_t slot, char* output, size_t capacity)
{
    if (!output || capacity == 0) return;
    output[0] = '\0';
    const char prefix[] = "mount-slot:";
    size_t at = 0;
    for (size_t i = 0; i < sizeof(prefix) - 1 && at + 1 < capacity; ++i) output[at++] = prefix[i];
    appendDecimal(output, capacity, at, slot);
}

} // namespace

ResponseStatus readDeviceSnapshot(void*, DeviceSnapshot* output)
{
    if (!output) return ResponseStatus::InternalError;
    DeviceSnapshot snapshot{};
    snapshot.backend = Backend::Kernel;

    gxos::network_settings::NetworkSnapshot network{};
    if (network_settings_provider::readSnapshot(&network) == gxos::network_settings::Result::Ok) {
        for (uint32_t i = 0; i < network.adapterCount && i < gxos::network_settings::kMaxAdapters; ++i) {
            const gxos::network_settings::NetworkInterfaceInfo& adapter = network.adapters[i];
            DeviceInfo item{};
            copyText(item.stableId, sizeof(item.stableId), adapter.stableId);
            if (adapter.driver[0]) copyText(item.name, sizeof(item.name), adapter.driver);
            else copyText(item.name, sizeof(item.name), "Network adapter");
            copyText(item.driver, sizeof(item.driver), adapter.driver);
            item.category = DeviceCategory::Network;
            item.status = adapter.linkState == gxos::network_settings::LinkState::Down
                ? DeviceStatus::Disconnected
                : adapter.linkState == gxos::network_settings::LinkState::Up
                    ? DeviceStatus::DriverLoaded : DeviceStatus::Unknown;
            const nic::NICDevice* nicDevice = nic::get_device();
            if (nicDevice && nicDevice->active) {
                setPciIdentity(item, nicDevice->vendorId, nicDevice->deviceId,
                    nicDevice->pciBus, nicDevice->pciSlot, nicDevice->pciFunc);
            } else {
                item.location[0] = '\0';
            }
            appendDevice(snapshot, item);
        }
    }

    for (uint8_t i = 0; i < pci_audio::controller_count(); ++i) {
        const pci_audio::AudioController* audio = pci_audio::get_controller(i);
        if (!audio || !audio->active) continue;
        DeviceInfo item{};
        size_t at = 0;
        const char idPrefix[] = "pci-audio:";
        for (size_t j = 0; j < sizeof(idPrefix) - 1 && at + 1 < sizeof(item.stableId); ++j)
            item.stableId[at++] = idPrefix[j];
        appendHex(item.stableId, sizeof(item.stableId), at, audio->pciBus, 2);
        if (at + 1 < sizeof(item.stableId)) item.stableId[at++] = ':';
        appendHex(item.stableId, sizeof(item.stableId), at, audio->pciDev, 2);
        if (at + 1 < sizeof(item.stableId)) item.stableId[at++] = '.';
        appendDecimal(item.stableId, sizeof(item.stableId), at, audio->pciFun);
        setPciName(item, "PCI audio device", audio->vendorId, audio->deviceId);
        const char* driver = audio->type == pci_audio::AUDIO_HDA ? "Intel HDA" :
            audio->type == pci_audio::AUDIO_AC97 ? "AC'97" : "Audio driver";
        copyText(item.driver, sizeof(item.driver), driver);
        item.category = DeviceCategory::Audio;
        item.status = DeviceStatus::DriverLoaded;
        setPciIdentity(item, audio->vendorId, audio->deviceId,
            audio->pciBus, audio->pciDev, audio->pciFun);
        appendDevice(snapshot, item);
    }

    vesa::PciVgaDevice vga{};
    if (vesa::probe_pci_vga(&vga) && vga.found) {
        DeviceInfo item{};
        size_t at = 0;
        const char prefix[] = "pci-display:";
        for (size_t i = 0; i < sizeof(prefix) - 1 && at + 1 < sizeof(item.stableId); ++i)
            item.stableId[at++] = prefix[i];
        appendHex(item.stableId, sizeof(item.stableId), at, vga.bus, 2);
        if (at + 1 < sizeof(item.stableId)) item.stableId[at++] = ':';
        appendHex(item.stableId, sizeof(item.stableId), at, vga.dev, 2);
        if (at + 1 < sizeof(item.stableId)) item.stableId[at++] = '.';
        appendDecimal(item.stableId, sizeof(item.stableId), at, vga.func);
        setPciName(item, "PCI display adapter", vga.vendorId, vga.deviceId);
        item.category = DeviceCategory::Display;
        item.status = DeviceStatus::Unknown;
        setPciIdentity(item, vga.vendorId, vga.deviceId, vga.bus, vga.dev, vga.func);
        appendDevice(snapshot, item);
    }

    const int gpuCount = virtio::gpu::device_count();
    for (int i = 0; i < gpuCount && i < 8; ++i) {
        virtio::gpu::GpuDevice* gpu = virtio::gpu::get_device(i);
        if (!gpu) continue;
        bool duplicate = false;
        if (gpu->isPci) {
            for (uint16_t j = 0; j < snapshot.deviceCount; ++j) {
                const DeviceInfo& existing = snapshot.devices[j];
                duplicate = duplicate || ((existing.flags & kDeviceFlagPciIdentity) != 0 &&
                    existing.bus == gpu->pciBus && existing.slot == gpu->pciDevice &&
                    existing.function == gpu->pciFunction);
            }
        }
        if (duplicate) continue;
        DeviceInfo item{};
        size_t at = 0;
        const char prefix[] = "virtio-gpu:";
        for (size_t j = 0; j < sizeof(prefix) - 1 && at + 1 < sizeof(item.stableId); ++j)
            item.stableId[at++] = prefix[j];
        appendDecimal(item.stableId, sizeof(item.stableId), at, static_cast<uint32_t>(i));
        copyText(item.name, sizeof(item.name), "VirtIO GPU");
        copyText(item.driver, sizeof(item.driver), "VirtIO GPU");
        item.category = DeviceCategory::Display;
        item.status = gpu->initialized ? DeviceStatus::DriverLoaded : DeviceStatus::Unknown;
        if (gpu->isPci) makePciLocation(gpu->pciBus, gpu->pciDevice, gpu->pciFunction,
            item.location, sizeof(item.location));
        else copyText(item.location, sizeof(item.location), "VirtIO MMIO");
        appendDevice(snapshot, item);
    }

    for (uint8_t i = 0; i < block::MAX_BLOCK_DEVICES; ++i) {
        const block::BlockDevice* dev = block::get_device(i);
        if (dev) appendBlockDevice(snapshot, i, *dev);
    }
    appendUsbDevices(snapshot);
    finishDeviceSnapshot(snapshot);
    updateDeviceGeneration(snapshot);
    if (!validDeviceSnapshot(snapshot)) return ResponseStatus::MalformedSnapshot;
    *output = snapshot;
    return ResponseStatus::Ok;
}

ResponseStatus readStorageSnapshot(void*, StorageSnapshot* output)
{
    if (!output) return ResponseStatus::InternalError;
    StorageSnapshot snapshot{};
    snapshot.backend = Backend::Kernel;
    for (uint8_t i = 0; i < block::MAX_BLOCK_DEVICES; ++i) {
        const block::BlockDevice* dev = block::get_device(i);
        if (!dev) continue;
        DiskInfo disk{};
        makeBlockStableId(i, *dev, disk.stableId, sizeof(disk.stableId));
        if (!copyFixedText(disk.name, sizeof(disk.name), dev->name, sizeof(dev->name)))
            disk.flags |= kDiskFlagNameTruncated;
        disk.transport = blockTransport(dev->type);
        disk.sectorSize = dev->sectorSize;
        bool capacityAvailable = false;
        disk.capacityBytes = calculateBytes(dev->totalSectors, dev->sectorSize, &capacityAvailable);
        if (capacityAvailable) disk.flags |= kDiskFlagCapacityAvailable;
        if (dev->sectorSize != 0) disk.flags |= kDiskFlagSectorSizeAvailable;
        if (dev->writeFn) disk.flags |= kDiskFlagWritable;
        readMbr(*dev, i, disk);
        appendDisk(snapshot, disk);
    }

    for (uint8_t i = 0; i < vfs::VFS_MAX_MOUNTS; ++i) {
        const vfs::MountPoint* mount = vfs::get_mount_by_index(i);
        if (!mount || !mount->active) continue;
        const block::BlockDevice* dev = block::get_device(mount->blockDevIndex);
        if (!dev || mount->blockDeviceGeneration == 0 ||
            block::device_generation(mount->blockDevIndex) != mount->blockDeviceGeneration) {
            // A reused block registry slot must never inherit an old mount.
            continue;
        }
        VolumeInfo volume{};
        makeVolumeStableId(i, volume.stableId, sizeof(volume.stableId));
        if (!copyFixedText(volume.mountPath, sizeof(volume.mountPath), mount->path, sizeof(mount->path)))
            volume.flags |= kVolumeFlagPathTruncated;
        volume.fileSystem = fileSystem(mount->fsType);
        if (mount->readOnly) volume.flags |= kVolumeFlagReadOnly;
        // The mount table does not identify a volume's partition extent, and
        // the filesystem provider has no reliable volume-capacity/free-space
        // API. Do not copy the backing disk's capacity into a volume field.
        makeBlockStableId(mount->blockDevIndex, *dev,
            volume.diskStableId, sizeof(volume.diskStableId));
        // VFS free_space() currently returns zero as an unsupported sentinel;
        // it is deliberately not copied as a real free-space value.
        appendVolume(snapshot, volume);
    }

    finishStorageSnapshot(snapshot);
    updateStorageGeneration(snapshot);
    if (!validStorageSnapshot(snapshot)) return ResponseStatus::MalformedSnapshot;
    *output = snapshot;
    return ResponseStatus::Ok;
}

ResponseStatus readDevicesCallback(void* context, DeviceSnapshot* output)
{
    return readDeviceSnapshot(context, output);
}

ResponseStatus readStorageCallback(void* context, StorageSnapshot* output)
{
    return readStorageSnapshot(context, output);
}

gxos::system_service::DispatchProviders appModelProvider()
{
    gxos::system_service::DispatchProviders provider{};
    provider.network = network_settings_provider::appModelProvider();
    provider.readDevices = readDevicesCallback;
    provider.readStorage = readStorageCallback;
    return provider;
}

} // namespace settings_inventory_provider
} // namespace kernel
