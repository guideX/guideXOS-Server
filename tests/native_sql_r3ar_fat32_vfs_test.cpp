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

uint16_t readLe16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t readLe32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

bool readVfsFile(const char* path, std::vector<uint8_t>& bytes) {
    const uint8_t handle = kernel::vfs::open(path, kernel::vfs::OPEN_READ);
    if (handle == 0xFF) return false;
    bytes.clear();
    uint8_t chunk[4096];
    bool ok = true;
    for (;;) {
        const int32_t count = kernel::vfs::read(handle, chunk, sizeof(chunk));
        if (count < 0) {
            ok = false;
            break;
        }
        if (count == 0) break;
        bytes.insert(bytes.end(), chunk, chunk + count);
    }
    const bool closed = kernel::vfs::close(handle) == kernel::vfs::VFS_OK;
    return ok && closed;
}

uint8_t findDevice(const char* name) {
    for (uint8_t i = 0; i < kernel::block::MAX_BLOCK_DEVICES; ++i) {
        const kernel::block::BlockDevice* device = kernel::block::get_device(i);
        if (device && std::strcmp(device->name, name) == 0) return i;
    }
    return 0xFF;
}

} // namespace

int main(int argc, char** argv) {
    std::cout.setf(std::ios::unitbuf);
    if (argc != 4) {
        std::cerr << "usage: native_sql_r3ar_fat32_vfs_test <ramdisk> "
                     "<trusted-fixture> <trusted-manifest>\n";
        return 2;
    }

    std::vector<uint8_t> candidateImage, sourceFixture, sourceManifest;
    check(readHostFile(argv[1], candidateImage), "read R3AR candidate RAM disk");
    check(readHostFile(argv[2], sourceFixture), "read closed trusted SQL fixture");
    check(readHostFile(argv[3], sourceManifest), "read trusted SQL provenance manifest");
    if (failures) return 1;
    const std::vector<uint8_t> candidateBefore = candidateImage;

    check(candidateImage.size() >= 512 && candidateImage.size() % 512 == 0,
          "candidate is a whole-sector RAM disk");
    if (candidateImage.size() < 512 || candidateImage.size() % 512 != 0) return 1;

    const uint8_t* boot = candidateImage.data();
    const uint32_t bps = readLe16(boot + 11);
    const uint32_t spc = boot[13];
    const uint32_t reserved = readLe16(boot + 14);
    const uint32_t fats = boot[16];
    const uint32_t totalSectors = readLe32(boot + 32);
    const uint32_t fatSectors = readLe32(boot + 36);
    const uint32_t rootCluster = readLe32(boot + 44) & 0x0FFFFFFFu;
    const uint32_t firstData = reserved + fats * fatSectors;
    const uint32_t dataSectors = totalSectors - firstData;
    const uint32_t dataClusters = spc ? dataSectors / spc : 0;
    const uint64_t fatEntries =
        (static_cast<uint64_t>(fatSectors) * bps) / sizeof(uint32_t);
    check(boot[510] == 0x55 && boot[511] == 0xAA && bps == 512 && spc == 1 &&
          reserved >= 9 && fats == 2 && readLe16(boot + 17) == 0 &&
          readLe16(boot + 19) == 0 && readLe16(boot + 22) == 0 &&
          fatSectors != 0 && readLe16(boot + 48) == 1 &&
          readLe16(boot + 50) == 6 &&
          totalSectors == candidateImage.size() / bps,
          "BPB uses supported FAT32 fields and exact device-sector accounting");
    check(dataClusters >= 65541 && fatEntries >= dataClusters + 2 &&
          rootCluster >= 2 && rootCluster < dataClusters + 2,
          "production target geometry classifies as FAT32 with adequate FAT capacity");

    kernel::block::init();
    kernel::fs_fat::init();
    kernel::ramdisk::init();
    kernel::vfs::init();
    const uint8_t ramdiskIndex = kernel::ramdisk::create_readonly_at(
        candidateImage.data(), candidateImage.size(), "r3ar-fat32");
    check(ramdiskIndex != 0xFF,
          "production read-only RAM disk attaches the corrected FAT32 bytes");
    if (ramdiskIndex == 0xFF) return 1;
    const uint8_t deviceIndex = findDevice("r3ar-fat32");
    check(deviceIndex != 0xFF && kernel::vfs::mount_type(
              "/system", deviceIndex, kernel::vfs::FS_TYPE_FAT32) != 0xFF,
          "production FAT32 driver and VFS mount the unpartitioned volume at LBA 0");
    const kernel::vfs::MountPoint* mount = kernel::vfs::get_mount("/system");
    const kernel::block::BlockDevice* device = mount
        ? kernel::block::get_device(mount->blockDevIndex) : nullptr;
    const kernel::fs_fat::FATVolume* volume = mount
        ? kernel::fs_fat::get_volume(mount->fsVolumeIndex) : nullptr;
    check(mount && mount->readOnly && device && device->readFn && !device->writeFn,
          "mounted production VFS and RAM disk expose read-only capabilities");
    check(volume && volume->type == kernel::fs_fat::FAT_TYPE_FAT32 &&
          volume->partitionOffset == 0 && volume->bytesPerSector == bps &&
          volume->sectorsPerCluster == spc && volume->reservedSectors == reserved &&
          volume->numFATs == fats && volume->fatSizeSectors == fatSectors &&
          volume->totalSectors == totalSectors &&
          volume->firstDataSector == firstData &&
          volume->totalDataClusters == dataClusters &&
          volume->rootCluster == rootCluster &&
          volume->totalDataClusters >= 65541,
          "production driver reports matching FAT32 geometry and cluster class");
    std::cout << "R3AR_GEOMETRY bps=" << bps << " spc=" << spc
              << " reserved=" << reserved << " fats=" << fats
              << " fat_sectors=" << fatSectors << " total=" << totalSectors
              << " first_data=" << firstData << " data_sectors=" << dataSectors
              << " clusters=" << dataClusters << " cluster_class=FAT32"
              << " root=" << rootCluster << "\n";

    std::vector<uint8_t> fixtureRead, manifestRead;
    check(readVfsFile("/system/Apps/R3AI/MULTI.GDB", fixtureRead) &&
          fixtureRead == sourceFixture && sourceFixture.size() == 250880,
          "production VFS reads byte-identical closed SQL fixture");
    check(readVfsFile("/system/Apps/R3AI/FIXTURE.MAN", manifestRead) &&
          manifestRead == sourceManifest,
          "production VFS reads byte-identical trusted fixture manifest");

    check(kernel::vfs::unlink("/system/Apps/R3AI/FIXTURE.MAN") ==
              kernel::vfs::VFS_ERR_READ_ONLY,
          "production VFS rejects fixture mutation on the read-only mount");
    uint8_t sector[512] = {};
    check(kernel::block::write_sectors(device ? mount->blockDevIndex : 0xFF,
              0, 1, sector) == kernel::block::BLOCK_ERR_UNSUPPORTED,
          "production block layer rejects a write to the RAM disk");
    check(kernel::native_sql_read_diagnostic::run_after_vfs_mount() ==
              kernel::native_sql_read_diagnostic::Passed,
          "production VFS SQL point/range diagnostic and cleanup pass");
    check(candidateImage == candidateBefore,
          "read-only VFS qualification made zero changes to the candidate bytes");
    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK,
          "production VFS unmounts the qualified R3AR volume");

    std::cout << "nativeSqlR3arFat32VfsChecks=" << (checks - failures) << "/"
              << checks << "\n";
    return failures == 0 ? 0 : 1;
}
