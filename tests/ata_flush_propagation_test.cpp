#include "kernel/block_device.h"
#include "kernel/fs_fat.h"
#include "kernel/vfs.h"

#include <array>
#include <cstring>
#include <iostream>

namespace {
static const uint32_t kSectorSize = 512;
static const uint32_t kSectorCount = 128;
static std::array<uint8_t, kSectorSize * kSectorCount> disk;
static kernel::block::Status flushStatus = kernel::block::BLOCK_OK;

int checks = 0;
int failures = 0;

void check(bool condition, const char* name)
{
    ++checks;
    if (condition) {
        std::cout << "PASS: " << name << "\n";
    } else {
        ++failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

kernel::block::Status read_sectors(uint8_t, uint64_t lba, uint32_t count,
                                   void* buffer)
{
    if (!buffer || lba > kSectorCount || count > kSectorCount - lba)
        return kernel::block::BLOCK_ERR_INVALID;
    std::memcpy(buffer, disk.data() + lba * kSectorSize, count * kSectorSize);
    return kernel::block::BLOCK_OK;
}

kernel::block::Status write_sectors(uint8_t, uint64_t lba, uint32_t count,
                                    const void* buffer)
{
    if (!buffer || lba > kSectorCount || count > kSectorCount - lba)
        return kernel::block::BLOCK_ERR_INVALID;
    std::memcpy(disk.data() + lba * kSectorSize, buffer, count * kSectorSize);
    return kernel::block::BLOCK_OK;
}

kernel::block::Status flush_device(uint8_t)
{
    return flushStatus;
}

void create_test_fat32_volume()
{
    kernel::fs_fat::FAT32_BPB bpb{};
    bpb.bytesPerSector = kSectorSize;
    bpb.sectorsPerCluster = 1;
    bpb.reservedSectors = 1;
    bpb.numFATs = 1;
    bpb.totalSectors32 = kSectorCount;
    bpb.fatSize32 = 1;
    bpb.rootCluster = 2;
    std::memcpy(bpb.volumeLabel, "NDB1R1TEST ", 11);
    std::memcpy(bpb.fsType, "FAT32   ", 8);
    std::memcpy(disk.data(), &bpb, sizeof(bpb));
    disk[510] = 0x55;
    disk[511] = 0xAA;

    const uint32_t fatEntries[] = { 0x0FFFFFF8u, 0x0FFFFFFFu, 0x0FFFFFFFu };
    std::memcpy(disk.data() + kSectorSize, fatEntries, sizeof(fatEntries));

    uint8_t* root = disk.data() + 2 * kSectorSize;
    std::memcpy(root, "TEST    TXT", 11);
    root[11] = 0x20; // archive file
    // A zero-length file needs no data cluster, and is sufficient for VFS flush.
}
}

int main()
{
    using namespace kernel;
    block::init();
    vfs::init();
    create_test_fat32_volume();

    block::BlockDevice device{};
    device.active = true;
    device.type = block::BDEV_ATA_PIO;
    device.totalSectors = kSectorCount;
    device.sectorSize = kSectorSize;
    std::memcpy(device.name, "ata-flush-test", 15);
    device.readFn = read_sectors;
    device.writeFn = write_sectors;
    device.flushFn = flush_device;
    const uint8_t deviceIndex = block::register_device(device);
    check(deviceIndex != 0xFF, "register injectable block device");

    const uint8_t mountIndex = vfs::mount_type("/flush-test", deviceIndex,
                                                vfs::FS_TYPE_FAT32);
    check(mountIndex != 0xFF, "mount hosted FAT32 test volume");
    const uint8_t handle = vfs::open("/flush-test/TEST.TXT", vfs::OPEN_READ);
    check(handle != 0xFF, "open a FAT file handle for VFS flush");

    const vfs::MountPoint* mount = vfs::get_mount_by_index(mountIndex);
    check(mount != nullptr, "mounted volume is available for direct FAT checks");
    if (!mount || handle == 0xFF) return 1;

    flushStatus = block::BLOCK_OK;
    check(block::flush(deviceIndex) == block::BLOCK_OK,
        "successful transport flush propagates through block layer");
    block::Status fatStatus = block::BLOCK_ERR_IO;
    check(fs_fat::flush(mount->fsVolumeIndex, &fatStatus) &&
        fatStatus == block::BLOCK_OK,
        "successful block flush propagates through FAT");
    check(vfs::flush(handle) == vfs::VFS_OK,
        "successful FAT flush propagates through VFS");

    flushStatus = block::BLOCK_ERR_IO;
    check(block::flush(deviceIndex) == block::BLOCK_ERR_IO,
        "failed transport flush remains a block-layer failure");
    fatStatus = block::BLOCK_OK;
    check(!fs_fat::flush(mount->fsVolumeIndex, &fatStatus) &&
        fatStatus == block::BLOCK_ERR_IO,
        "block I/O failure remains a FAT flush failure");
    check(vfs::flush(handle) == vfs::VFS_ERR_IO,
        "FAT I/O failure remains a VFS flush failure");

    flushStatus = block::BLOCK_ERR_TIMEOUT;
    fatStatus = block::BLOCK_OK;
    check(!fs_fat::flush(mount->fsVolumeIndex, &fatStatus) &&
        fatStatus == block::BLOCK_ERR_TIMEOUT,
        "block timeout remains a FAT flush failure");
    check(vfs::flush(handle) == vfs::VFS_ERR_IO_TIMEOUT,
        "FAT timeout remains a VFS timeout");

    std::cout << "ATA flush propagation checks=" << (checks - failures)
              << "/" << checks << "\n";
    return failures == 0 ? 0 : 1;
}
