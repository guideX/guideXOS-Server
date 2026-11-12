#include "kernel/block_device.h"
#include "kernel/fs_ext4.h"
#include "kernel/fs_fat.h"
#include "kernel/vfs.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr uint32_t kSectorSize = 512;
std::fstream g_image;
uint64_t g_sectorCount = 0;
uint64_t g_writesUntilFailure = UINT64_MAX;
bool g_failFlush = false;
uint32_t g_fatStart = 0;
uint32_t g_fatSectors = 0;
uint8_t g_fatCopies = 0;
uint32_t g_fullFatReadSectors = 0;
uint32_t g_fullFatMappedEntries = 0;
int g_checks = 0;
int g_failures = 0;

uint16_t read_le16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8));
}

uint32_t read_le32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
}

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

kernel::block::Status image_read(uint8_t driverIndex, uint64_t lba,
                                 uint32_t count, void* output)
{
    if (!output || !count || lba > g_sectorCount || count > g_sectorCount - lba) {
        return kernel::block::BLOCK_ERR_INVALID;
    }
    g_image.clear();
    g_image.seekg(static_cast<std::streamoff>(lba * kSectorSize), std::ios::beg);
    g_image.read(static_cast<char*>(output), static_cast<std::streamsize>(count) * kSectorSize);
    if (!g_image.good()) return kernel::block::BLOCK_ERR_IO;
    if (driverIndex == 1u) {
        auto* bytes = static_cast<uint8_t*>(output);
        for (uint32_t sector = 0; sector < count; ++sector) {
            const uint64_t currentLba = lba + sector;
            if (currentLba < g_fatStart ||
                currentLba >= static_cast<uint64_t>(g_fatStart) +
                    static_cast<uint64_t>(g_fatSectors) * g_fatCopies) continue;
            ++g_fullFatReadSectors;
            uint8_t* fatSector = bytes + static_cast<size_t>(sector) * kSectorSize;
            const uint32_t copyRelativeSector = static_cast<uint32_t>(
                (currentLba - g_fatStart) % g_fatSectors);
            const uint32_t firstEntryOffset = copyRelativeSector == 0 ? 8u : 0u;
            for (uint32_t offset = firstEntryOffset; offset < kSectorSize; offset += 4) {
                uint32_t value = read_le32(fatSector + offset);
                if ((value & 0x0FFFFFFFu) == 0) {
                    ++g_fullFatMappedEntries;
                    value = 0x0FFFFFFFu;
                    fatSector[offset] = static_cast<uint8_t>(value);
                    fatSector[offset + 1] = static_cast<uint8_t>(value >> 8);
                    fatSector[offset + 2] = static_cast<uint8_t>(value >> 16);
                    fatSector[offset + 3] = static_cast<uint8_t>(value >> 24);
                }
            }
        }
    }
    return kernel::block::BLOCK_OK;
}

kernel::block::Status image_write(uint8_t, uint64_t lba, uint32_t count, const void* input)
{
    if (!input || !count || lba > g_sectorCount || count > g_sectorCount - lba) {
        return kernel::block::BLOCK_ERR_INVALID;
    }
    if (g_writesUntilFailure == 0) return kernel::block::BLOCK_ERR_IO;
    if (g_writesUntilFailure != UINT64_MAX) --g_writesUntilFailure;
    g_image.clear();
    g_image.seekp(static_cast<std::streamoff>(lba * kSectorSize), std::ios::beg);
    g_image.write(static_cast<const char*>(input), static_cast<std::streamsize>(count) * kSectorSize);
    return g_image.good() ? kernel::block::BLOCK_OK : kernel::block::BLOCK_ERR_IO;
}

kernel::block::Status refuse_full_fat_write(uint8_t, uint64_t, uint32_t, const void*)
{
    return kernel::block::BLOCK_ERR_IO;
}

kernel::block::Status image_flush(uint8_t)
{
    if (g_failFlush) return kernel::block::BLOCK_ERR_IO;
    g_image.flush();
    return g_image.good() ? kernel::block::BLOCK_OK : kernel::block::BLOCK_ERR_IO;
}

kernel::block::Status invalid_filesystem_read(uint8_t, uint64_t lba,
                                               uint32_t count, void* output)
{
    if (!output || !count || lba > 2048u || count > 2048u - lba) {
        return kernel::block::BLOCK_ERR_INVALID;
    }
    std::memset(output, 0, static_cast<size_t>(count) * kSectorSize);
    return kernel::block::BLOCK_OK;
}

bool setup_device(const char* imagePath)
{
    g_image.open(imagePath, std::ios::in | std::ios::out | std::ios::binary);
    if (!g_image) return false;
    g_image.seekg(0, std::ios::end);
    const std::streamoff length = g_image.tellg();
    if (length <= 0 || (length % kSectorSize) != 0) return false;
    g_sectorCount = static_cast<uint64_t>(length / kSectorSize);

    uint8_t bootSector[kSectorSize]{};
    g_image.clear();
    g_image.seekg(0, std::ios::beg);
    g_image.read(reinterpret_cast<char*>(bootSector), sizeof(bootSector));
    if (!g_image.good() || bootSector[510] != 0x55 || bootSector[511] != 0xAA) return false;
    g_fatStart = read_le16(bootSector + 14);
    g_fatSectors = read_le32(bootSector + 36);
    g_fatCopies = bootSector[16];
    if (!g_fatStart || !g_fatSectors || !g_fatCopies) return false;

    kernel::block::init();
    kernel::block::BlockDevice device{};
    device.active = true;
    device.type = kernel::block::BDEV_ATA_PIO;
    device.totalSectors = g_sectorCount;
    device.sectorSize = kSectorSize;
    std::strcpy(device.name, "host-image");
    std::strcpy(device.stableId, "GXOS-DATA-V1");
    device.readFn = image_read;
    device.writeFn = image_write;
    device.flushFn = image_flush;
    if (kernel::block::register_device(device) != 0) return false;

    kernel::block::BlockDevice invalidDevice{};
    invalidDevice.active = true;
    invalidDevice.type = kernel::block::BDEV_ATA_PIO;
    invalidDevice.totalSectors = 2048;
    invalidDevice.sectorSize = kSectorSize;
    std::strcpy(invalidDevice.name, "invalid-fixture");
    invalidDevice.readFn = invalid_filesystem_read;
    if (kernel::block::register_device(invalidDevice) != 1) return false;

    kernel::block::BlockDevice fullFatDevice{};
    fullFatDevice.active = true;
    fullFatDevice.type = kernel::block::BDEV_ATA_PIO;
    fullFatDevice.driverIndex = 1;
    fullFatDevice.totalSectors = g_sectorCount;
    fullFatDevice.sectorSize = kSectorSize;
    std::strcpy(fullFatDevice.name, "full-fat-fixture");
    fullFatDevice.readFn = image_read;
    fullFatDevice.writeFn = refuse_full_fat_write;
    fullFatDevice.flushFn = image_flush;
    return kernel::block::register_device(fullFatDevice) == 2;
}

void reset_mounts()
{
    (void)kernel::vfs::unmount("/AppData");
    (void)kernel::vfs::unmount("/Documents");
}

bool read_equals(const char* path, const std::vector<uint8_t>& expected)
{
    std::vector<uint8_t> actual(expected.size() + 1u, 0xA5u);
    const int32_t count = kernel::vfs::read_file(path, actual.data(),
                                                 static_cast<uint32_t>(expected.size()));
    return count == static_cast<int32_t>(expected.size()) &&
        std::equal(expected.begin(), expected.end(), actual.begin());
}
}

namespace kernel {
namespace arch {
namespace amd64 {
uint8_t inb(uint16_t) { return 0x20u; }
void outb(uint16_t, uint8_t) {}
}
}
namespace fs_ext4 {
uint8_t mount(uint8_t) { return 0xFF; }
void unmount(uint8_t) {}
bool open_root_dir(uint8_t) { return false; }
bool read_dir(uint8_t, Ext4DirEntry*) { return false; }
}
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: fs_fat_persistence_test <writable-image-copy>\n";
        return 2;
    }
    if (!setup_device(argv[1])) {
        std::cerr << "Could not open a writable, sector-aligned image copy.\n";
        return 2;
    }

    kernel::vfs::init();
    const uint8_t documents = kernel::vfs::mount_type(
        "/Documents", 0, kernel::vfs::FS_TYPE_FAT32);
    check(documents != 0xFF, "prepared FAT32 data volume mounts at /Documents");
    if (documents == 0xFF) return 1;
    check(kernel::vfs::mount_count() == 1,
          "first mount creates exactly one active VFS mount");
    check(kernel::vfs::mount_type("/Documents", 0, kernel::vfs::FS_TYPE_FAT32) == 0xFF &&
              kernel::vfs::mount_count() == 1,
          "repeated mount of the same path fails without changing mount state");
    check(kernel::vfs::mount_type("/Bad", 1, kernel::vfs::FS_TYPE_FAT32) == 0xFF &&
              kernel::vfs::mount_count() == 1 && !kernel::vfs::get_mount("/Bad"),
          "invalid filesystem fixture fails mount without leaving a partial VFS entry");

    kernel::vfs::FileInfo info{};
    check(kernel::vfs::stat("/Documents/APPDATA", &info) == kernel::vfs::VFS_OK &&
              info.type == kernel::vfs::FILE_TYPE_DIRECTORY,
          "provisioned APPDATA directory exists on the mounted volume");
    const uint8_t appData = kernel::vfs::mount_alias(
        "/AppData", "/Documents/APPDATA");
    check(appData != 0xFF && kernel::vfs::get_mount("/AppData/TEST.TXT") &&
              kernel::vfs::get_mount("/AppData/TEST.TXT")->alias,
          "supported VFS alias maps /AppData onto the data volume");
    check(kernel::vfs::unmount("/Documents") == kernel::vfs::VFS_ERR_BUSY,
          "source volume cannot unmount while an alias references it");
    check(kernel::vfs::unmount("/AppData") == kernel::vfs::VFS_OK &&
              kernel::vfs::unmount("/AppData") == kernel::vfs::VFS_ERR_NOT_MOUNT,
          "alias unmount removes only the alias and is repeat-safe");

    check(kernel::vfs::mkdir("/Documents/NOTES") == kernel::vfs::VFS_OK,
          "create a supported 8.3 document directory");
    check(kernel::vfs::stat("/Documents/NOTES", &info) == kernel::vfs::VFS_OK &&
              info.type == kernel::vfs::FILE_TYPE_DIRECTORY,
          "created document directory is visible through VFS");

    std::vector<uint8_t> payload(12291);
    for (size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<uint8_t>((index * 73u + 19u) & 0xFFu);
    }
    check(kernel::vfs::create_file("/Documents/NOTES/REPORT.BIN",
                                   payload.data(), static_cast<uint32_t>(payload.size())) ==
              static_cast<int32_t>(payload.size()),
          "create and close a multi-cluster document with exact byte count");
    check(kernel::vfs::stat("/Documents/NOTES/REPORT.BIN", &info) == kernel::vfs::VFS_OK &&
              info.type == kernel::vfs::FILE_TYPE_REGULAR && info.size == payload.size(),
          "file metadata reports the committed document size");
    check(read_equals("/Documents/NOTES/REPORT.BIN", payload),
          "reopen and read all committed document bytes exactly");
    check(kernel::vfs::create_file("/Documents/NOTES/REPORT.BIN", payload.data(), 1) ==
              kernel::vfs::VFS_ERR_EXISTS,
          "exclusive create refuses to replace an existing document");
    check(kernel::vfs::write_file("/Documents/NOTES/REPORT.BIN", "short replacement", 17) == 17,
          "overwrite truncates and commits a shorter document");
    check(kernel::vfs::stat("/Documents/NOTES/REPORT.BIN", &info) == kernel::vfs::VFS_OK &&
              info.size == 17 && kernel::vfs::read_file("/Documents/NOTES/REPORT.BIN",
                  payload.data(), static_cast<uint32_t>(payload.size())) == 17 &&
              std::memcmp(payload.data(), "short replacement", 17) == 0,
          "truncate leaves exact replacement bytes and size");
    check(kernel::vfs::append_file("/Documents/NOTES/REPORT.BIN", " appended", 9) == 9,
          "append opens at EOF and reports committed bytes");
    check(kernel::vfs::read_file("/Documents/NOTES/REPORT.BIN", payload.data(),
                                 static_cast<uint32_t>(payload.size())) == 26 &&
              std::memcmp(payload.data(), "short replacement appended", 26) == 0,
          "appended content is readable in the expected order");

    for (int iteration = 0; iteration < 12; ++iteration) {
        const uint8_t handle = kernel::vfs::open("/Documents/NOTES/REPORT.BIN",
                                                 kernel::vfs::OPEN_READ);
        if (handle == 0xFF) {
            check(false, "repeated open/close cycle opens the saved file");
            break;
        }
        uint8_t bytes[26]{};
        const int32_t count = kernel::vfs::read(handle, bytes, sizeof(bytes));
        const kernel::vfs::Status closeStatus = kernel::vfs::close(handle);
        if (iteration == 11) {
            check(count == 26 && closeStatus == kernel::vfs::VFS_OK &&
                      std::memcmp(bytes, "short replacement appended", 26) == 0,
                  "repeated open/close cycles preserve data and release handles");
        }
    }

    check(kernel::vfs::read_file("/Documents/MISSING.TXT", payload.data(), 64) ==
              kernel::vfs::VFS_ERR_NOT_FOUND,
          "missing document path returns a not-found status");
    check(kernel::vfs::create_file("/Documents/TOOLONGNAME.TXT", payload.data(), 1) ==
              kernel::vfs::VFS_ERR_INVALID,
          "unsupported 8.3 filename is rejected without truncation");

    const uint8_t fullFatMount = kernel::vfs::mount_type(
        "/Full", 2, kernel::vfs::FS_TYPE_FAT32);
    check(fullFatMount != 0xFF,
          "synthetic full-FAT view mounts over the prepared image metadata");
    const int32_t noSpaceStatus = fullFatMount == 0xFF
        ? kernel::vfs::VFS_ERR_IO
        : kernel::vfs::create_file("/Full/NOSPACE.TXT", payload.data(), 1);
    if (noSpaceStatus != kernel::vfs::VFS_ERR_NO_SPACE) {
        std::cout << "synthetic_full_fat_status=" << noSpaceStatus
                  << " fat_reads=" << g_fullFatReadSectors
                  << " mapped_entries=" << g_fullFatMappedEntries << "\n";
    }
    check(noSpaceStatus == kernel::vfs::VFS_ERR_NO_SPACE,
          "synthetic full-FAT fixture reports insufficient space");
    if (g_fullFatReadSectors == 0 || g_fullFatMappedEntries == 0) {
        std::cout << "full_fat_reads=" << g_fullFatReadSectors
                  << " mapped_entries=" << g_fullFatMappedEntries
                  << " fat_start=" << g_fatStart << " fat_sectors=" << g_fatSectors
                  << " copies=" << static_cast<unsigned>(g_fatCopies) << "\n";
        check(false, "synthetic capacity fixture overlays FAT allocation metadata");
    }
    if (fullFatMount != 0xFF) {
        check(kernel::vfs::unmount("/Full") == kernel::vfs::VFS_OK,
              "synthetic full-FAT fixture unmounts without changing the backing image");
    }

    const uint8_t writeHandle = kernel::vfs::open("/Documents/NOTES/REPORT.BIN",
        kernel::vfs::OPEN_RDWR);
    const bool modifyOpened = writeHandle != 0xFF &&
        kernel::vfs::seek(writeHandle, 6,
            static_cast<kernel::vfs::SeekOrigin>(0)) == kernel::vfs::VFS_OK &&
        kernel::vfs::write(writeHandle, "EDIT", 4) == 4 &&
        kernel::vfs::close(writeHandle) == kernel::vfs::VFS_OK;
    check(modifyOpened, "seek and modify an existing document in place");
    check(kernel::vfs::read_file("/Documents/NOTES/REPORT.BIN", payload.data(),
                                 static_cast<uint32_t>(payload.size())) == 26 &&
              std::memcmp(payload.data(), "short EDITacement appended", 26) == 0,
          "in-place modification is committed and readable");

    const uint8_t boundsHandle = kernel::vfs::open("/Documents/NOTES/REPORT.BIN",
        kernel::vfs::OPEN_WRITE);
    const bool rejectBeyondEnd = boundsHandle != 0xFF &&
        kernel::vfs::seek(boundsHandle, 0xFFFFFFFFll,
            static_cast<kernel::vfs::SeekOrigin>(0)) == kernel::vfs::VFS_OK &&
        kernel::vfs::write(boundsHandle, "X", 1) == kernel::vfs::VFS_ERR_INVALID &&
        kernel::vfs::close(boundsHandle) == kernel::vfs::VFS_OK;
    check(rejectBeyondEnd,
          "write beyond the 32-bit file offset bound is rejected without wrapping");

    const uint8_t flushHandle = kernel::vfs::open("/Documents/NOTES/REPORT.BIN",
        kernel::vfs::OPEN_RDWR);
    const bool wroteBeforeFlushFailure = flushHandle != 0xFF &&
        kernel::vfs::seek(flushHandle, 0, static_cast<kernel::vfs::SeekOrigin>(0)) ==
            kernel::vfs::VFS_OK &&
        kernel::vfs::write(flushHandle, "S", 1) == 1;
    g_failFlush = true;
    const kernel::vfs::Status failedClose = flushHandle == 0xFF
        ? kernel::vfs::VFS_ERR_INVALID : kernel::vfs::close(flushHandle);
    g_failFlush = false;
    check(wroteBeforeFlushFailure && failedClose == kernel::vfs::VFS_ERR_IO,
          "close surfaces a failed device flush instead of reporting a saved file");

    check(kernel::block::read_sectors(0, g_sectorCount, 1, payload.data()) ==
              kernel::block::BLOCK_ERR_INVALID &&
              kernel::block::write_sectors(0, g_sectorCount, 1, payload.data()) ==
              kernel::block::BLOCK_ERR_INVALID,
          "out-of-range block reads and writes are rejected");

    check(kernel::vfs::unmount("/Documents") == kernel::vfs::VFS_OK &&
              kernel::vfs::mount_count() == 0,
          "mounted filesystem can be cleanly unmounted");
    const uint8_t remounted = kernel::vfs::mount_type(
        "/Documents", 0, kernel::vfs::FS_TYPE_FAT32);
    check(remounted != 0xFF && kernel::vfs::read_file(
              "/Documents/NOTES/REPORT.BIN", payload.data(),
              static_cast<uint32_t>(payload.size())) == 26 &&
              std::memcmp(payload.data(), "Short EDITacement appended", 26) == 0,
          "filesystem teardown and remount recover saved bytes from the image");
    reset_mounts();

    g_writesUntilFailure = 2;
    const int32_t failedWrite = kernel::vfs::write_file(
        "/Documents/IOFAIL.TXT", "write that must not be reported as saved", 40);
    check(failedWrite < 0,
          "injected backing-image write failure is returned to the caller");
    g_writesUntilFailure = UINT64_MAX;
    reset_mounts();

    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
