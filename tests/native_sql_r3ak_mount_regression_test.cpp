#include "kernel/block_device.h"
#include "kernel/fs_fat.h"
#include "kernel/native_sql_read_diagnostic.h"
#include "kernel/ramdisk.h"
#include "kernel/vfs.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

struct MemoryDeviceState {
    uint8_t* bytes;
    size_t byteCount;
    uint32_t sectorSize;
    uint64_t readRequests;
    uint64_t writeRequests;
    uint64_t failedReadLba;
    kernel::block::Status failedReadStatus;
};

MemoryDeviceState g_memoryDevices[16] = {};
uint8_t g_memoryDeviceCount = 0;
uint64_t g_ramdiskReadRequests = 0;

void check(bool condition, const char* name) {
    ++checks;
    if (condition) {
        std::cout << "PASS: " << name << "\n";
    } else {
        ++failures;
        std::cout << "FAIL: " << name << "\n";
    }
}

bool readHostFile(const char* path, std::vector<uint8_t>& bytes) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    bytes.assign(std::istreambuf_iterator<char>(stream),
                 std::istreambuf_iterator<char>());
    return !stream.bad();
}

kernel::block::Status readMemoryDevice(uint8_t stateIndex, uint64_t lba,
                                      uint32_t count, void* buffer) {
    if (stateIndex >= g_memoryDeviceCount || !buffer || count == 0) {
        return kernel::block::BLOCK_ERR_INVALID;
    }
    MemoryDeviceState& state = g_memoryDevices[stateIndex];
    const uint64_t sectors = state.byteCount / state.sectorSize;
    if (lba > sectors || count > sectors - lba) {
        return kernel::block::BLOCK_ERR_INVALID;
    }
    ++state.readRequests;
    if (lba == state.failedReadLba) return state.failedReadStatus;
    const size_t offset = static_cast<size_t>(lba * state.sectorSize);
    const size_t length = static_cast<size_t>(count * state.sectorSize);
    std::memcpy(buffer, state.bytes + offset, length);
    return kernel::block::BLOCK_OK;
}

kernel::block::Status writeMemoryDevice(uint8_t stateIndex, uint64_t lba,
                                        uint32_t count, const void* buffer) {
    if (stateIndex >= g_memoryDeviceCount || !buffer || count == 0) {
        return kernel::block::BLOCK_ERR_INVALID;
    }
    MemoryDeviceState& state = g_memoryDevices[stateIndex];
    const uint64_t sectors = state.byteCount / state.sectorSize;
    if (lba > sectors || count > sectors - lba) {
        return kernel::block::BLOCK_ERR_INVALID;
    }
    ++state.writeRequests;
    const size_t offset = static_cast<size_t>(lba * state.sectorSize);
    const size_t length = static_cast<size_t>(count * state.sectorSize);
    std::memcpy(state.bytes + offset, buffer, length);
    return kernel::block::BLOCK_OK;
}

kernel::block::Status readRamDiskSpy(uint8_t ramdiskIndex, uint64_t lba,
                                     uint32_t count, void* buffer) {
    ++g_ramdiskReadRequests;
    return kernel::ramdisk::read_sectors(ramdiskIndex, lba, count, buffer);
}

uint8_t registerRamDiskReadSpy(uint8_t ramdiskIndex, const char* name) {
    const kernel::ramdisk::RamDisk* disk =
        kernel::ramdisk::get_disk(ramdiskIndex);
    if (!disk || !disk->data || !disk->readOnly) return 0xFF;
    kernel::block::BlockDevice device = {};
    device.active = true;
    device.type = kernel::block::BDEV_USB_MASS;
    device.driverIndex = ramdiskIndex;
    device.totalSectors = disk->sectorCount;
    device.sectorSize = disk->sectorSize;
    std::strncpy(device.name, name, sizeof(device.name) - 1);
    device.readFn = readRamDiskSpy;
    device.writeFn = nullptr;
    return kernel::block::register_device(device);
}

uint8_t registerMemoryDevice(std::vector<uint8_t>& bytes,
                             uint32_t sectorSize,
                             bool writable,
                             const char* name,
                             uint8_t* stateIndexOut = nullptr) {
    if (g_memoryDeviceCount >= 16 || sectorSize == 0 ||
        bytes.empty() || bytes.size() % sectorSize != 0) {
        return 0xFF;
    }
    const uint8_t stateIndex = g_memoryDeviceCount++;
    MemoryDeviceState& state = g_memoryDevices[stateIndex];
    state.bytes = bytes.data();
    state.byteCount = bytes.size();
    state.sectorSize = sectorSize;
    state.readRequests = 0;
    state.writeRequests = 0;
    state.failedReadLba = ~static_cast<uint64_t>(0);
    state.failedReadStatus = kernel::block::BLOCK_ERR_IO;

    kernel::block::BlockDevice device = {};
    device.active = true;
    device.type = kernel::block::BDEV_USB_MASS;
    device.driverIndex = stateIndex;
    device.totalSectors = bytes.size() / sectorSize;
    device.sectorSize = sectorSize;
    std::strncpy(device.name, name, sizeof(device.name) - 1);
    device.readFn = readMemoryDevice;
    device.writeFn = writable ? writeMemoryDevice : nullptr;
    const uint8_t blockIndex = kernel::block::register_device(device);
    if (stateIndexOut) *stateIndexOut = stateIndex;
    return blockIndex;
}

uint8_t findDevice(const char* name) {
    for (uint8_t i = 0; i < kernel::block::MAX_BLOCK_DEVICES; ++i) {
        const kernel::block::BlockDevice* device = kernel::block::get_device(i);
        if (device && std::strcmp(device->name, name) == 0) return i;
    }
    return 0xFF;
}

bool readVfsFile(const char* path, std::vector<uint8_t>& bytes) {
    const uint8_t handle = kernel::vfs::open(path, kernel::vfs::OPEN_READ);
    if (handle == 0xFF) return false;
    const int64_t size = kernel::vfs::file_size(handle);
    if (size < 0 || static_cast<uint64_t>(size) > 1024 * 1024) {
        (void)kernel::vfs::close(handle);
        return false;
    }
    bytes.resize(static_cast<size_t>(size));
    size_t offset = 0;
    while (offset < bytes.size()) {
        const uint32_t remaining = static_cast<uint32_t>(bytes.size() - offset);
        const int32_t count = kernel::vfs::read(handle, bytes.data() + offset,
                                                remaining);
        if (count <= 0 || static_cast<size_t>(count) > bytes.size() - offset) {
            (void)kernel::vfs::close(handle);
            return false;
        }
        offset += static_cast<size_t>(count);
    }
    return kernel::vfs::close(handle) == kernel::vfs::VFS_OK;
}

bool allVfsHandlesClosed() {
    for (uint8_t i = 0; i < kernel::vfs::VFS_MAX_OPEN_FILES; ++i) {
        const kernel::vfs::FileHandle* handle = kernel::vfs::get_handle(i);
        if (handle && handle->open) return false;
    }
    return true;
}

void writeLe32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

bool expectRejectedMount(std::vector<uint8_t>& bytes,
                         uint32_t sectorSize,
                         const char* deviceName,
                         const char* expectedCode,
                         const char* description) {
    const uint8_t device = registerMemoryDevice(
        bytes, sectorSize, false, deviceName);
    if (device == 0xFF) {
        check(false, description);
        return false;
    }
    const uint8_t mount = kernel::vfs::mount_type(
        "/probe", device, kernel::vfs::FS_TYPE_FAT32);
    check(mount == 0xFF, description);
    check(std::strcmp(kernel::fs_fat::mount_failure_code(), expectedCode) == 0,
          "mount rejection reports the precise FAT prerequisite");
    check(kernel::vfs::get_mount("/probe") == nullptr,
          "failed mount does not publish a VFS mount");
    check(allVfsHandlesClosed(), "failed mount leaves no open VFS handles");
    return mount == 0xFF;
}

} // namespace

int main(int argc, char** argv) {
    std::cout.setf(std::ios::unitbuf);
    if (argc != 4) {
        std::cerr << "usage: native_sql_r3ak_mount_regression_test <candidate-ramdisk> "
                     "<missing-fixture-ramdisk> <source-fixture>\n";
        return 2;
    }

    std::vector<uint8_t> candidateImage;
    std::vector<uint8_t> missingImage;
    std::vector<uint8_t> sourceFixture;
    check(readHostFile(argv[1], candidateImage), "read exact packaged R3AI ramdisk");
    check(readHostFile(argv[2], missingImage), "read missing-fixture control image");
    check(readHostFile(argv[3], sourceFixture), "read trusted SQL fixture");
    if (failures != 0) return 1;

    const std::vector<uint8_t> candidateBaseline = candidateImage;
    kernel::block::init();
    kernel::fs_fat::init();
    kernel::ramdisk::init();
    kernel::vfs::init();

    const uint8_t absent = kernel::vfs::mount_type(
        "/missing-device", 15, kernel::vfs::FS_TYPE_FAT32);
    check(absent == 0xFF && kernel::vfs::get_mount("/missing-device") == nullptr,
          "missing device before registration fails without publishing a mount");

    std::vector<uint8_t> blockReadFailureImage = candidateImage;
    uint8_t blockReadFailureState = 0xFF;
    const uint8_t blockReadFailureDevice = registerMemoryDevice(
        blockReadFailureImage, 512, false, "r3ak-read-failure",
        &blockReadFailureState);
    if (blockReadFailureState != 0xFF) {
        g_memoryDevices[blockReadFailureState].failedReadLba = 0;
        g_memoryDevices[blockReadFailureState].failedReadStatus =
            kernel::block::BLOCK_ERR_TIMEOUT;
    }
    const uint8_t blockReadFailureMount = blockReadFailureDevice == 0xFF
        ? 0xFF : kernel::vfs::mount_type(
            "/read-failure", blockReadFailureDevice,
            kernel::vfs::FS_TYPE_FAT32);
    check(blockReadFailureDevice != 0xFF && blockReadFailureMount == 0xFF,
          "LBA-zero block read failure rejects the production mount");
    check(std::strcmp(kernel::fs_fat::mount_failure_code(),
                      "BLOCK_READ_LBA0_FAILED") == 0,
          "LBA-zero read failure retains its distinct mount classification");
    check(kernel::vfs::get_mount("/read-failure") == nullptr &&
              allVfsHandlesClosed(),
          "block read failure publishes no mount and leaks no handles");

    const uint8_t ramdiskIndex = kernel::ramdisk::create_readonly_at(
        candidateImage.data(), candidateImage.size(), "r3ak-source");
    check(ramdiskIndex != 0xFF, "production read-only RAM disk attaches packaged bytes");
    const uint8_t spyDevice = registerRamDiskReadSpy(ramdiskIndex, "r3ak-read-spy");
    check(spyDevice != 0xFF, "counting block view delegates to production RAM disk reads");
    check(kernel::vfs::mount_type("/system", spyDevice,
                                  kernel::vfs::FS_TYPE_FAT32) != 0xFF,
          "exact unpartitioned package mounts through production FAT32 and VFS");
    const kernel::vfs::MountPoint* readOnlyMount = kernel::vfs::get_mount("/system");
    const kernel::block::BlockDevice* readOnlyDevice = readOnlyMount
        ? kernel::block::get_device(readOnlyMount->blockDevIndex) : nullptr;
    check(readOnlyMount && readOnlyMount->readOnly && readOnlyDevice &&
              readOnlyDevice->readFn && !readOnlyDevice->writeFn,
          "mounted RAM disk retains read-only capability through block and VFS layers");
    const kernel::fs_fat::FATVolume* packageVolume = readOnlyMount
        ? kernel::fs_fat::get_volume(readOnlyMount->fsVolumeIndex) : nullptr;
    check(packageVolume && packageVolume->partitionOffset == 0 &&
              packageVolume->bytesPerSector == 512 &&
              packageVolume->totalSectors == 120832 &&
              packageVolume->reservedSectors == 32 &&
              packageVolume->numFATs == 2 &&
              packageVolume->fatSizeSectors == 256 &&
              packageVolume->firstDataSector == 544 &&
              packageVolume->sectorsPerCluster == 16 &&
              packageVolume->rootCluster == 2,
          "production mount reports the exact unpartitioned package geometry");
    check(packageVolume && packageVolume->totalDataClusters == 7518,
          "production geometry computes 7,518 data clusters");
    uint8_t sector[kernel::ramdisk::RAMDISK_SECTOR_SIZE] = {};
    check(kernel::block::write_sectors(spyDevice, 0, 1, sector) ==
              kernel::block::BLOCK_ERR_UNSUPPORTED,
          "read-only RAM disk exposes no block write operation");

    std::vector<uint8_t> fixtureRead;
    check(readVfsFile("/system/apps/r3ai/multi.gdb", fixtureRead) &&
              fixtureRead == sourceFixture,
          "case-insensitive VFS path returns exact fixture bytes");
    check(kernel::native_sql_read_diagnostic::run_after_vfs_mount() ==
              kernel::native_sql_read_diagnostic::Passed,
          "SQL point and range diagnostics pass through production VFS operations");
    check(g_ramdiskReadRequests > 0,
          "SQL qualification issues block reads through the production RAM disk callback");
    check(allVfsHandlesClosed(), "successful SQL diagnostic closes every VFS handle");
    check(candidateImage == candidateBaseline && fixtureRead == sourceFixture,
          "read-only SQL qualification leaves packaged bytes unchanged");
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK,
          "unmount exact candidate after successful SQL qualification");

    const uint8_t missingRamdisk = kernel::ramdisk::create_readonly_at(
        missingImage.data(), missingImage.size(), "r3ak-missing");
    check(missingRamdisk != 0xFF, "missing-fixture control attaches read-only");
    const uint8_t missingDevice = findDevice("r3ak-missing");
    check(missingDevice != 0xFF && kernel::vfs::mount_type(
              "/system", missingDevice, kernel::vfs::FS_TYPE_FAT32) != 0xFF,
          "missing-fixture control still mounts as valid FAT32");
    check(kernel::native_sql_read_diagnostic::run_after_vfs_mount() ==
              kernel::native_sql_read_diagnostic::Failed,
          "missing fixture fails cleanly before publishing SQL results");
    check(allVfsHandlesClosed(), "missing-fixture failure closes all VFS handles");
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK,
          "unmount missing-fixture control");

    std::vector<uint8_t> writableImage = candidateImage;
    uint8_t writableState = 0xFF;
    const uint8_t writableDevice = registerMemoryDevice(
        writableImage, 512, true, "r3ak-writable", &writableState);
    check(writableDevice != 0xFF && kernel::vfs::mount_type(
              "/system", writableDevice, kernel::vfs::FS_TYPE_FAT32) != 0xFF,
          "existing write-capable FAT32 mount remains available");
    const kernel::vfs::MountPoint* writableMount = kernel::vfs::get_mount("/system");
    check(writableMount && !writableMount->readOnly,
          "VFS does not misreport a write-capable device as read-only");
    check(kernel::native_sql_read_diagnostic::run_after_vfs_mount() ==
              kernel::native_sql_read_diagnostic::Failed,
          "SQL diagnostic rejects a writable mount before fixture access");
    check(g_memoryDevices[writableState].writeRequests == 0,
          "read-only SQL preflight issues no writes on a writable-capability mismatch");
    check(allVfsHandlesClosed(), "writable-capability rejection leaks no VFS handles");
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK,
          "unmount write-capable compatibility control");

    std::vector<uint8_t> wrongSectorSize = candidateImage;
    expectRejectedMount(wrongSectorSize, 4096, "r3ak-sector-mismatch",
                        "BLOCK_SECTOR_SIZE_MISMATCH",
                        "block and FAT sector-size mismatch is rejected");

    std::vector<uint8_t> oversizedBlockSector = candidateImage;
    expectRejectedMount(oversizedBlockSector, 8192, "r3ak-sector-oversized",
                        "BLOCK_SECTOR_SIZE_INVALID",
                        "block sectors larger than the FAT metadata buffer are rejected");

    std::vector<uint8_t> badSignature = candidateImage;
    badSignature[510] = 0;
    expectRejectedMount(badSignature, 512, "r3ak-bad-signature",
                        "FAT32_BOOT_SIGNATURE_INVALID",
                        "corrupt FAT32 boot signature is rejected");

    std::vector<uint8_t> zeroFatSize = candidateImage;
    std::memset(zeroFatSize.data() + 36, 0, 4);
    expectRejectedMount(zeroFatSize, 512, "r3ak-zero-fat-size",
                        "FAT32_FAT_SIZE_ZERO",
                        "zero FAT32 size is attributed at the BPB validation stage");

    std::vector<uint8_t> incompatibleFatLabel = candidateImage;
    std::memcpy(incompatibleFatLabel.data() + 82, "FAT16   ", 8);
    expectRejectedMount(incompatibleFatLabel, 512, "r3ak-fat-label-mismatch",
                        "FAT32_TYPE_LABEL_MISMATCH",
                        "FAT32 type-label mismatch is attributed before fallback");

    std::vector<uint8_t> badClusterGeometry = candidateImage;
    badClusterGeometry[13] = 3;
    expectRejectedMount(badClusterGeometry, 512, "r3ak-bad-cluster",
                        "FAT32_SECTORS_PER_CLUSTER_INVALID",
                        "unsupported FAT32 cluster geometry is rejected");

    std::vector<uint8_t> oversizedCluster = candidateImage;
    oversizedCluster[13] = 128;
    expectRejectedMount(oversizedCluster, 512, "r3ak-cluster-oversized",
                        "FAT32_CLUSTER_SIZE_INVALID",
                        "FAT32 clusters larger than 32 KiB are rejected");

    std::vector<uint8_t> truncated = candidateImage;
    truncated.resize(truncated.size() - 512);
    expectRejectedMount(truncated, 512, "r3ak-truncated",
                        "FAT32_VOLUME_EXCEEDS_DEVICE",
                        "truncated media is rejected before mount publication");

    std::vector<uint8_t> invalidMbr(candidateImage.size(), 0);
    invalidMbr[446 + 4] = 0x0B;
    writeLe32(invalidMbr, 446 + 8,
              static_cast<uint32_t>(candidateImage.size() / 512 + 1));
    writeLe32(invalidMbr, 446 + 12, 128);
    invalidMbr[510] = 0x55;
    invalidMbr[511] = 0xAA;
    expectRejectedMount(invalidMbr, 512, "r3ak-invalid-mbr",
                        "PARTITION_BOOT_READ_FAILED",
                        "partition offset beyond device capacity is rejected");

    std::vector<uint8_t> partitioned(candidateImage.size() + 512, 0);
    partitioned[446 + 4] = 0x0B;
    writeLe32(partitioned, 446 + 8, 1);
    writeLe32(partitioned, 446 + 12,
              static_cast<uint32_t>(candidateImage.size() / 512));
    partitioned[510] = 0x55;
    partitioned[511] = 0xAA;
    std::memcpy(partitioned.data() + 512, candidateImage.data(), candidateImage.size());
    const uint8_t partitionedDevice = registerMemoryDevice(
        partitioned, 512, false, "r3ak-partitioned");
    check(partitionedDevice != 0xFF && kernel::vfs::mount_type(
              "/probe", partitionedDevice, kernel::vfs::FS_TYPE_FAT32) != 0xFF,
          "valid FAT32 partition at a nonzero volume offset mounts");
    check(readVfsFile("/probe/Apps/R3AI/MULTI.GDB", fixtureRead) &&
              fixtureRead == sourceFixture,
          "partition offset is applied to production file reads");
    check(kernel::vfs::unmount("/probe") == kernel::vfs::VFS_OK,
          "unmount valid partitioned volume");

    std::vector<uint8_t> lateImage = candidateImage;
    const uint8_t lateDevice = registerMemoryDevice(
        lateImage, 512, false, "r3ak-late-device");
    check(lateDevice != 0xFF && kernel::vfs::mount_type(
              "/late", lateDevice, kernel::vfs::FS_TYPE_FAT32) != 0xFF,
          "mount succeeds after block registration precedes VFS mount");
    check(kernel::vfs::unmount("/late") == kernel::vfs::VFS_OK,
          "unmount late-registration ordering control");

    check(allVfsHandlesClosed(), "all scenario handles are closed");
    std::cout << "nativeSqlR3akMountChecks=" << (checks - failures) << "/"
              << checks << "\n";
    return failures == 0 ? 0 : 1;
}
