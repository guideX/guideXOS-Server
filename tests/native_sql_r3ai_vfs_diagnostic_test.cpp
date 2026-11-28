#include "kernel/block_device.h"
#include "kernel/fs_fat.h"
#include "kernel/native_sql_read_diagnostic.h"
#include "kernel/ramdisk.h"
#include "kernel/vfs.h"

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

uint8_t attachReadonlyVolume(const std::vector<uint8_t>& image,
                             const char* name) {
    if (image.empty() || image.size() % kernel::ramdisk::RAMDISK_SECTOR_SIZE != 0) {
        return 0xFF;
    }
    return kernel::ramdisk::create_readonly_at(image.data(), image.size(), name);
}

} // namespace

int main(int argc, char** argv) {
    std::cout.setf(std::ios::unitbuf);
    if (argc != 4) {
        std::cerr << "usage: native_sql_r3ai_vfs_diagnostic_test <candidate-ramdisk> "
                     "<missing-fixture-ramdisk> <source-fixture>\n";
        return 2;
    }

    std::vector<uint8_t> candidateImage;
    std::vector<uint8_t> missingImage;
    std::vector<uint8_t> sourceFixture;
    check(readHostFile(argv[1], candidateImage), "read packaged candidate FAT image");
    check(readHostFile(argv[2], missingImage), "read missing-fixture FAT image");
    check(readHostFile(argv[3], sourceFixture), "read trusted source fixture");
    if (failures != 0) return 1;

    kernel::block::init();
    kernel::fs_fat::init();
    kernel::ramdisk::init();
    kernel::vfs::init();

    const uint8_t candidateDevice = attachReadonlyVolume(candidateImage, "r3aipack");
    check(candidateDevice != 0xFF, "attach boot-package bytes using production read-only RAM disk");
    if (candidateDevice == 0xFF) return 1;
    const uint8_t candidateMount = kernel::vfs::mount_type(
        "/system", candidateDevice, kernel::vfs::FS_TYPE_FAT32);
    check(candidateMount != 0xFF, "mount candidate with production FAT VFS");
    if (candidateMount == 0xFF) return 1;

    const kernel::vfs::MountPoint* mount = kernel::vfs::get_mount("/system");
    const kernel::block::BlockDevice* device = mount
        ? kernel::block::get_device(mount->blockDevIndex) : nullptr;
    check(mount && mount->readOnly && device && device->readFn && !device->writeFn,
          "mounted boot image is enforced read-only by the production VFS/block path");
    if (device) {
        uint8_t sector[kernel::ramdisk::RAMDISK_SECTOR_SIZE] = {};
        check(kernel::block::write_sectors(mount->blockDevIndex, 0, 1, sector) ==
                  kernel::block::BLOCK_ERR_UNSUPPORTED,
              "read-only boot image has no block write operation");
    }

    const char fixturePath[] = "/system/Apps/R3AI/MULTI.GDB";
    std::vector<uint8_t> before;
    std::vector<uint8_t> after;
    check(readVfsFile(fixturePath, before) && before == sourceFixture,
          "production VFS returns byte-identical fixed database fixture");

    check(kernel::native_sql_read_diagnostic::run_after_vfs_mount() ==
              kernel::native_sql_read_diagnostic::Passed,
          "gated point/range diagnostic passes through production VFS operations");
    check(kernel::native_sql_read_diagnostic::run_after_vfs_mount() ==
              kernel::native_sql_read_diagnostic::Passed,
          "diagnostic can be repeated after closing all resources");
    check(readVfsFile(fixturePath, after) && after == before && after == sourceFixture,
          "fixture bytes are unchanged across repeated diagnostics");
    check(allVfsHandlesClosed(), "successful diagnostics and reads close every VFS handle");

    const uint8_t readOnlyHandle = kernel::vfs::open(
        fixturePath, kernel::vfs::OPEN_READ);
    check(readOnlyHandle != 0xFF,
          "open fixture through an explicitly read-only VFS handle");
    check(readOnlyHandle != 0xFF && kernel::vfs::truncate_shrink(
              readOnlyHandle, 0) == kernel::vfs::VFS_ERR_READ_ONLY,
          "production VFS rejects mutation of the read-only fixture mount");
    check(readOnlyHandle != 0xFF && kernel::vfs::close(readOnlyHandle) ==
              kernel::vfs::VFS_OK,
          "close read-only mutation probe handle");
    check(allVfsHandlesClosed(), "rejected mutation leaves no VFS handle");

    check(kernel::vfs::unmount("/system") == kernel::vfs::VFS_OK,
          "unmount candidate image before missing-fixture case");
    const uint8_t missingDevice = attachReadonlyVolume(missingImage, "r3aimiss");
    check(missingDevice != 0xFF, "attach controlled missing-fixture volume");
    if (missingDevice != 0xFF) {
        const uint8_t missingMount = kernel::vfs::mount_type(
            "/system", missingDevice, kernel::vfs::FS_TYPE_FAT32);
        check(missingMount != 0xFF, "mount missing-fixture image through production FAT VFS");
        if (missingMount != 0xFF) {
            check(kernel::native_sql_read_diagnostic::run_after_vfs_mount() ==
                      kernel::native_sql_read_diagnostic::Failed,
                  "missing fixture produces a controlled diagnostic failure");
            check(kernel::native_sql_read_diagnostic::run_after_vfs_mount() ==
                      kernel::native_sql_read_diagnostic::Failed,
                  "missing-fixture failure is repeatable");
            check(allVfsHandlesClosed(), "missing-fixture failures leave no VFS handle");
        }
    }

    std::cout << "nativeSqlR3aiVfsChecks=" << (checks - failures) << "/"
              << checks << "\n";
    return failures == 0 ? 0 : 1;
}
