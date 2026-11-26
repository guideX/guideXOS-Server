#include "kernel/block_device.h"
#include "kernel/fs_fat.h"
#include "kernel/ramdisk.h"
#include "kernel/vfs.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace kernel { namespace arch { namespace amd64 {
uint8_t inb(uint16_t) { return 0x20; }
void outb(uint16_t, uint8_t) { }
}}}

namespace {
int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* label)
{
    ++g_checks;
    if (condition) {
        std::cout << "PASS: " << label << "\n";
    } else {
        ++g_failures;
        std::cout << "FAIL: " << label << "\n";
    }
}
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: release_ramdisk_mount_test <ramdisk.img>\n";
        return 2;
    }

    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "Could not open release ramdisk image.\n";
        return 2;
    }
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(input)), {});

    check(!image.empty() && image.size() % kernel::ramdisk::RAMDISK_SECTOR_SIZE == 0,
          "ramdisk is nonempty and sector aligned");
    if (image.empty() || image.size() % kernel::ramdisk::RAMDISK_SECTOR_SIZE != 0) {
        return 1;
    }

    kernel::block::init();
    kernel::ramdisk::init();
    const uint8_t device = kernel::ramdisk::create_readonly_release_image_at(
        image.data(), image.size(), "wallimg");
    check(device != 0xFF, "release-image RAM disk registers as a block device");
    if (device == 0xFF) return 1;

    const uint64_t generation = kernel::block::device_generation(device);
    const kernel::block::BlockDevice* blockDevice = kernel::block::get_device(device);
    check(generation != 0 && blockDevice && !blockDevice->writeFn &&
              kernel::ramdisk::is_readonly_release_image_device(device, generation),
          "release-image source remains read-only and owner-verified");

    kernel::vfs::init();
    const uint8_t rootMount = kernel::vfs::mount_type(
        "/", device, kernel::vfs::FS_TYPE_FAT32);
    check(rootMount != 0xFF, "release ramdisk mounts as the VFS root");
    if (rootMount == 0xFF) {
        std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
        return 1;
    }
#if defined(GXOS_FAT_MOUNT_DIAGNOSTICS)
    const kernel::fs_fat::MountDiagnostic mountDiagnostic =
        kernel::fs_fat::last_mount_diagnostic();
    check(mountDiagnostic.initialRequestedSector == 0 &&
              mountDiagnostic.firstReadStatus == kernel::block::BLOCK_OK &&
              mountDiagnostic.sectorZeroObtained,
          "instrumented production mount records the exact initial wallimg sector read");
    check(mountDiagnostic.bootSignatureReadable && mountDiagnostic.bootSignatureValid &&
              mountDiagnostic.bpbFieldsReadable &&
              mountDiagnostic.bytesPerSector == 512 &&
              mountDiagnostic.sectorsPerCluster == 16 &&
              mountDiagnostic.reservedSectorCount == 32 &&
              mountDiagnostic.fatCount == 2 &&
              mountDiagnostic.fatSize32 == 256 &&
              mountDiagnostic.totalSectors32 == 131072,
          "instrumented production mount records the qualified wallimg BPB fields");
    check(mountDiagnostic.finalStage == kernel::fs_fat::FAT_MOUNT_DIAG_STAGE_MOUNTED,
          "instrumented production mount records the successful final mount stage");
#endif

    const kernel::vfs::MountPoint* mount = kernel::vfs::get_mount("/");
    check(mount && mount->readOnly && mount->blockDevIndex == device &&
              mount->blockDeviceGeneration == generation,
          "root mount retains the expected read-only block owner");

    kernel::vfs::FileInfo appsInfo{};
    check(kernel::vfs::stat("/Apps", &appsInfo) == kernel::vfs::VFS_OK &&
              appsInfo.type == kernel::vfs::FILE_TYPE_DIRECTORY,
          "/Apps resolves as a directory on the root filesystem");

    const uint8_t appsIterator = kernel::vfs::opendir("/Apps");
    check(appsIterator != 0xFF, "/Apps directory enumeration opens");
    bool pacManFound = false;
    if (appsIterator != 0xFF) {
        kernel::vfs::DirEntry entry{};
        while (kernel::vfs::readdir(appsIterator, &entry)) {
            if (std::string(entry.name) == "PacMan" &&
                entry.type == kernel::vfs::FILE_TYPE_DIRECTORY) {
                pacManFound = true;
            }
        }
        kernel::vfs::closedir(appsIterator);
    }
    check(pacManFound, "approved PacMan package directory is present under /Apps");

    char manifest[1024]{};
    const int32_t manifestBytes = kernel::vfs::read_file(
        "/Apps/PacMan/app.json", manifest, sizeof(manifest) - 1);
    const std::string manifestText(manifest,
        manifestBytes > 0 ? static_cast<size_t>(manifestBytes) : 0u);
    check(manifestBytes == 956 &&
              manifestText.find("com.guidexos.pacman") != std::string::npos,
          "PacMan manifest is fully readable with its expected package identity");

    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
