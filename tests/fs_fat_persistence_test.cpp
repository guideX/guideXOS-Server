#include "kernel/block_device.h"
#include "kernel/fs_ext4.h"
#include "kernel/fs_fat.h"
#include "kernel/desktop_gradient_persistence.h"
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
uint32_t g_flushFailuresRemaining = 0;
kernel::block::Status g_flushFailureStatus = kernel::block::BLOCK_ERR_IO;
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
    if (g_flushFailuresRemaining != 0u) {
        --g_flushFailuresRemaining;
        return g_flushFailureStatus;
    }
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

uint8_t fat_lfn_checksum(const char shortName[11])
{
    uint8_t checksum = 0;
    for (uint32_t index = 0; index < 11; ++index) {
        const uint8_t byte = static_cast<uint8_t>(shortName[index]);
        checksum = static_cast<uint8_t>(((checksum & 1u) ? 0x80u : 0u) +
            (checksum >> 1) + byte);
    }
    return checksum;
}

bool insert_lfn_at_root_end(const char* longName, const char shortName[11])
{
    const kernel::vfs::MountPoint* mount = kernel::vfs::get_mount("/Documents");
    if (!mount) return false;
    const kernel::fs_fat::FATVolume* volume =
        kernel::fs_fat::get_volume(mount->fsVolumeIndex);
    if (!volume || volume->type != kernel::fs_fat::FAT_TYPE_FAT32 ||
        volume->bytesPerSector != kSectorSize || volume->rootCluster < 2u ||
        !longName || !shortName) {
        return false;
    }

    const size_t nameLength = std::strlen(longName);
    if (nameLength == 0 || nameLength > 12u) return false;
    const uint64_t rootSector = volume->partitionOffset + volume->firstDataSector +
        static_cast<uint64_t>(volume->rootCluster - 2u) * volume->sectorsPerCluster;
    uint8_t sector[kSectorSize];
    for (uint32_t sectorInCluster = 0;
         sectorInCluster < volume->sectorsPerCluster; ++sectorInCluster) {
        const uint64_t lba = rootSector + sectorInCluster;
        if (kernel::block::read_sectors(volume->blockDevIndex, lba, 1, sector) !=
            kernel::block::BLOCK_OK) {
            return false;
        }
        for (uint32_t offset = 0; offset + sizeof(kernel::fs_fat::FAT32_LFNEntry) <=
             sizeof(sector); offset += sizeof(kernel::fs_fat::FAT32_DirEntry)) {
            if (sector[offset] != 0x00u) continue;

            kernel::fs_fat::FAT32_LFNEntry entry{};
            entry.order = 0x41u;
            entry.attr = kernel::fs_fat::ATTR_LFN;
            entry.type = 0;
            entry.checksum = fat_lfn_checksum(shortName);
            entry.firstClusterLo = 0;
            uint16_t characters[13];
            for (uint32_t index = 0; index < 13; ++index) characters[index] = 0xFFFFu;
            for (size_t index = 0; index < nameLength; ++index) {
                characters[index] = static_cast<uint8_t>(longName[index]);
            }
            if (nameLength < 13u) characters[nameLength] = 0;
            for (uint32_t index = 0; index < 5; ++index) entry.name1[index] = characters[index];
            for (uint32_t index = 0; index < 6; ++index) entry.name2[index] = characters[index + 5];
            for (uint32_t index = 0; index < 2; ++index) entry.name3[index] = characters[index + 11];
            std::memcpy(sector + offset, &entry, sizeof(entry));
            return kernel::block::write_sectors(volume->blockDevIndex, lba, 1, sector) ==
                    kernel::block::BLOCK_OK &&
                kernel::block::flush(volume->blockDevIndex) == kernel::block::BLOCK_OK;
        }
    }
    return false;
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
    if (argc == 3 && std::string(argv[1]) == "--prepare-fixture") {
        if (!setup_device(argv[2])) {
            std::cerr << "Could not open a writable, sector-aligned image copy.\n";
            return 2;
        }
        kernel::vfs::init();
        if (kernel::vfs::mount_type("/Documents", 0,
                                    kernel::vfs::FS_TYPE_FAT32) == 0xFF) {
            std::cerr << "Could not mount fixture at /Documents.\n";
            return 2;
        }
        kernel::vfs::FileInfo info{};
        if (kernel::vfs::stat("/Documents/APPDATA", &info) ==
                kernel::vfs::VFS_ERR_NOT_FOUND &&
            kernel::vfs::mkdir("/Documents/APPDATA") != kernel::vfs::VFS_OK) {
            std::cerr << "Could not create fixture APPDATA directory.\n";
            return 2;
        }
        if (kernel::vfs::stat("/Documents/UNTITLED.TXT", &info) ==
                kernel::vfs::VFS_ERR_NOT_FOUND) {
            static const char document[] = "Hosted FAT persistence fixture.\n";
            if (kernel::vfs::create_file("/Documents/UNTITLED.TXT", document,
                    static_cast<uint32_t>(sizeof(document) - 1u)) !=
                    static_cast<int32_t>(sizeof(document) - 1u)) {
                std::cerr << "Could not create fixture Notepad document.\n";
                return 2;
            }
        }
        std::cout << "Prepared writable FAT32 fixture with APPDATA and UNTITLED.TXT.\n";
        return 0;
    }
    if (argc != 2) {
        std::cerr << "usage: fs_fat_persistence_test <writable-image-copy>\n"
                  << "   or: fs_fat_persistence_test --prepare-fixture <writable-image-copy>\n";
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

    const uint8_t settingsAlias = kernel::vfs::mount_alias(
        "/AppData", "/Documents/APPDATA");
    check(settingsAlias != 0xFF,
          "AppData alias remounts independently of the Documents mount");
    uint8_t savedDocumentBefore[256]{};
    const int32_t savedDocumentSizeBefore = kernel::vfs::read_file(
        "/Documents/UNTITLED.TXT", savedDocumentBefore,
        static_cast<uint32_t>(sizeof(savedDocumentBefore)));
    check(savedDocumentSizeBefore > 0,
          "accepted Notepad document is present before application settings writes");

    char storedWallpaper[kernel::desktop::kGradientPresetIdCapacity]{};
    uint32_t storedIconVisibility = 0u;
    check(kernel::desktop::readAppDataWallpaperId(
              storedWallpaper, sizeof(storedWallpaper)) ==
              kernel::desktop::DesktopAppDataReadStatus::Missing,
          "image-backed AppData starts without an application settings override");
    kernel::desktop::GradientPersistenceResult wallpaperResult{};
    check(kernel::desktop::persistBuiltInBackgroundId(
              kernel::desktop::kDesktopDefaultWallpaperId, "gradient_ocean",
              &wallpaperResult) == kernel::desktop::GradientPersistenceStatus::Ok &&
              kernel::desktop::readAppDataWallpaperId(
                  storedWallpaper, sizeof(storedWallpaper)) ==
                  kernel::desktop::DesktopAppDataReadStatus::Loaded &&
              std::string(storedWallpaper) == "gradient_ocean",
          "image-backed FAT32 stores and reopens the selected wallpaper ID");
    const uint32_t iconDefaults = kernel::desktop::kDesktopIconVisibilityTrash |
        kernel::desktop::kDesktopIconVisibilityThisSystem |
        kernel::desktop::kDesktopIconVisibilityFileManager;
    const uint32_t iconRequest = iconDefaults &
        ~kernel::desktop::kDesktopIconVisibilityTrash;
    kernel::desktop::DesktopIconPersistenceResult iconResult{};
    check(kernel::desktop::persistDesktopIconVisibility(
              iconDefaults, iconRequest, &iconResult) ==
              kernel::desktop::DesktopIconPersistenceStatus::Ok &&
              kernel::desktop::readAppDataDesktopIconVisibility(
                  iconDefaults, &storedIconVisibility) ==
                  kernel::desktop::DesktopAppDataReadStatus::Loaded &&
              storedIconVisibility == iconRequest,
          "image-backed FAT32 stores and reloads desktop icon visibility");
    kernel::vfs::FileInfo settingsFileInfo{};
    char settingsBytes[kernel::desktop::kGradientConfigTextCapacity]{};
    const int32_t settingsByteCount = kernel::vfs::read_file(
        kernel::desktop::kDesktopAppDataConfigPath, settingsBytes,
        static_cast<uint32_t>(sizeof(settingsBytes) - 1u));
    check(settingsByteCount > 0 &&
              kernel::vfs::stat(kernel::desktop::kDesktopAppDataConfigPath,
                                &settingsFileInfo) == kernel::vfs::VFS_OK &&
              settingsFileInfo.size == static_cast<uint64_t>(settingsByteCount) &&
              std::string(settingsBytes, static_cast<size_t>(settingsByteCount)).find(
                  "format=GUIDEXOS-DESKTOP-STATE\nversion=1\n") == 0 &&
              std::string(settingsBytes, static_cast<size_t>(settingsByteCount)).find(
                  "wallpaperId=gradient_ocean\n") != std::string::npos &&
              std::string(settingsBytes, static_cast<size_t>(settingsByteCount)).find(
                  "showDesktopTrash=0\n") != std::string::npos,
          "AppData file has the versioned format and exact saved setting values");
    check(kernel::vfs::stat("/Documents/DESKAPP.CFG", &settingsFileInfo) ==
              kernel::vfs::VFS_ERR_NOT_FOUND &&
              kernel::vfs::get_mount("/Documents/UNTITLED.TXT") !=
                  kernel::vfs::get_mount("/AppData/DESKAPP.CFG"),
          "AppData settings do not shadow the Documents namespace");
    uint8_t savedDocumentAfter[256]{};
    const int32_t savedDocumentSizeAfter = kernel::vfs::read_file(
        "/Documents/UNTITLED.TXT", savedDocumentAfter,
        static_cast<uint32_t>(sizeof(savedDocumentAfter)));
    check(savedDocumentSizeAfter == savedDocumentSizeBefore &&
              std::memcmp(savedDocumentAfter, savedDocumentBefore,
                          static_cast<size_t>(savedDocumentSizeBefore)) == 0,
          "application settings writes preserve the existing Notepad document bytes");

    const int32_t settingsSizeBeforeRestart = settingsByteCount;
    check(kernel::vfs::unmount("/AppData") == kernel::vfs::VFS_OK,
          "AppData alias can be removed for a simulated application restart");
    check(kernel::desktop::readAppDataWallpaperId(
              storedWallpaper, sizeof(storedWallpaper)) ==
              kernel::desktop::DesktopAppDataReadStatus::Unavailable,
          "settings load reports an unavailable AppData mount instead of a default save");
    check(kernel::vfs::mount_alias("/AppData", "/Documents/APPDATA") != 0xFF &&
              kernel::desktop::readAppDataWallpaperId(
                  storedWallpaper, sizeof(storedWallpaper)) ==
                  kernel::desktop::DesktopAppDataReadStatus::Loaded &&
              std::string(storedWallpaper) == "gradient_ocean" &&
              kernel::desktop::readAppDataDesktopIconVisibility(
                  iconDefaults, &storedIconVisibility) ==
                  kernel::desktop::DesktopAppDataReadStatus::Loaded &&
              storedIconVisibility == iconRequest &&
              kernel::vfs::stat(kernel::desktop::kDesktopAppDataConfigPath,
                                &settingsFileInfo) == kernel::vfs::VFS_OK &&
              settingsFileInfo.size == static_cast<uint64_t>(settingsSizeBeforeRestart),
          "application restart reloads both settings from the same FAT32 image");
    check(kernel::vfs::unmount("/AppData") == kernel::vfs::VFS_OK,
          "AppData settings alias releases before the broader VFS teardown checks");

    const char lfnTestShortName[11] = {'L','F','N','T','E','S','T',' ','T','M','P'};
    check(insert_lfn_at_root_end("lfntest.tmp", lfnTestShortName),
          "install a checksum-valid LFN entry before the root-directory end marker");
    const char lfnRenameContents[] = "rename keeps the current long name valid";
    const int32_t lfnRenameSize = static_cast<int32_t>(sizeof(lfnRenameContents) - 1u);
    check(kernel::vfs::create_file("/Documents/LFNTEST.TMP", lfnRenameContents,
                                   static_cast<uint32_t>(lfnRenameSize)) == lfnRenameSize &&
              kernel::vfs::stat("/Documents/lfntest.tmp", &info) == kernel::vfs::VFS_OK,
          "create and resolve a file through its checksum-valid long filename");
    check(kernel::vfs::rename("/Documents/lfntest.tmp", "/Documents/renamed.tmp") ==
              kernel::vfs::VFS_OK,
          "same-directory rename updates the LFN text and short-name checksum together");
    check(kernel::vfs::stat("/Documents/lfntest.tmp", &info) ==
              kernel::vfs::VFS_ERR_NOT_FOUND &&
              kernel::vfs::stat("/Documents/renamed.tmp", &info) == kernel::vfs::VFS_OK &&
              info.size == static_cast<uint64_t>(lfnRenameSize),
          "renamed entry rejects its old long name and resolves its new long name");
    check(kernel::vfs::read_file("/Documents/renamed.tmp", savedDocumentAfter,
                                 static_cast<uint32_t>(sizeof(savedDocumentAfter))) ==
              lfnRenameSize &&
              std::memcmp(savedDocumentAfter, lfnRenameContents,
                          static_cast<size_t>(lfnRenameSize)) == 0,
          "renamed LFN-backed file retains its original bytes");
    const uint8_t renamedDirectory = kernel::vfs::opendir("/Documents");
    bool sawOldLongName = false;
    bool sawNewLongName = false;
    if (renamedDirectory != 0xFF) {
        kernel::vfs::DirEntry entry{};
        while (kernel::vfs::readdir(renamedDirectory, &entry)) {
            if (std::string(entry.name) == "lfntest.tmp") sawOldLongName = true;
            if (std::string(entry.name) == "renamed.tmp") sawNewLongName = true;
        }
        kernel::vfs::closedir(renamedDirectory);
    }
    check(renamedDirectory != 0xFF && !sawOldLongName && sawNewLongName,
          "directory enumeration shows the renamed LFN without the old name");
    check(kernel::vfs::unlink("/Documents/renamed.tmp") == kernel::vfs::VFS_OK &&
              kernel::vfs::stat("/Documents/renamed.tmp", &info) ==
                  kernel::vfs::VFS_ERR_NOT_FOUND &&
              kernel::vfs::stat("/Documents/lfntest.tmp", &info) ==
                  kernel::vfs::VFS_ERR_NOT_FOUND,
          "unlink removes the short entry and its associated long-name records");

    const char staleLfnName[] = "stalex.tmp";
    const char staleLfnChecksumName[11] = {'O','T','H','E','R',' ',' ',' ','T','M','P'};
    const char staleLfnContents[] = "stale LFN checksum fixture";
    const int32_t staleLfnSize = static_cast<int32_t>(sizeof(staleLfnContents) - 1u);
    check(insert_lfn_at_root_end(staleLfnName, staleLfnChecksumName) &&
              kernel::vfs::create_file("/Documents/STALE.TMP", staleLfnContents,
                  static_cast<uint32_t>(staleLfnSize)) == staleLfnSize &&
              kernel::vfs::stat("/Documents/stalex.tmp", &info) ==
                  kernel::vfs::VFS_ERR_NOT_FOUND &&
              kernel::vfs::stat("/Documents/STALE.TMP", &info) == kernel::vfs::VFS_OK,
          "checksum-mismatched LFN is ignored while its short-name entry remains addressable");
    check(kernel::vfs::rename("/Documents/STALE.TMP", "/Documents/renamed2.tmp") ==
              kernel::vfs::VFS_OK,
          "rename through a short name does not depend on a stale LFN association");
    const uint8_t staleLfnDirectory = kernel::vfs::opendir("/Documents");
    bool sawRenamedShortName = false;
    bool sawStaleLongName = false;
    if (staleLfnDirectory != 0xFF) {
        kernel::vfs::DirEntry entry{};
        while (kernel::vfs::readdir(staleLfnDirectory, &entry)) {
            if (std::string(entry.name) == "RENAMED2.TMP") sawRenamedShortName = true;
            if (std::string(entry.name) == staleLfnName) sawStaleLongName = true;
        }
        kernel::vfs::closedir(staleLfnDirectory);
    }
    check(staleLfnDirectory != 0xFF && sawRenamedShortName && !sawStaleLongName,
          "rename ignores the checksum-mismatched LFN for directory enumeration");
    check(kernel::vfs::unlink("/Documents/renamed2.tmp") == kernel::vfs::VFS_OK &&
              kernel::vfs::stat("/Documents/renamed2.tmp", &info) ==
                  kernel::vfs::VFS_ERR_NOT_FOUND &&
              kernel::vfs::stat("/Documents/stalex.tmp", &info) ==
                  kernel::vfs::VFS_ERR_NOT_FOUND,
          "delete through a short name leaves no resolvable stale long-name alias");

    static const char renameRollbackContents[] = "preserve settings before retry";
    const int32_t renameRollbackSize =
        static_cast<int32_t>(sizeof(renameRollbackContents) - 1u);
    check(kernel::vfs::create_file("/Documents/RETRY.TMP", renameRollbackContents,
                                   static_cast<uint32_t>(renameRollbackSize)) ==
              renameRollbackSize,
          "create a settings-like file before an injected rename flush failure");
    g_flushFailureStatus = kernel::block::BLOCK_ERR_TIMEOUT;
    g_flushFailuresRemaining = 1;
    const kernel::vfs::Status renameFlushFailure = kernel::vfs::rename(
        "/Documents/RETRY.TMP", "/Documents/RETRY.NEW");
    g_flushFailuresRemaining = 0;
    (void)image_flush(0);
    check(renameFlushFailure == kernel::vfs::VFS_ERR_IO_TIMEOUT &&
              kernel::fs_fat::last_io_status() == kernel::block::BLOCK_ERR_TIMEOUT,
          "rename preserves the original flush timeout after a successful rollback");
    char renameRollbackReadback[64]{};
    check(kernel::vfs::stat("/Documents/RETRY.TMP", &info) == kernel::vfs::VFS_OK &&
              info.size == static_cast<uint64_t>(renameRollbackSize) &&
              kernel::vfs::stat("/Documents/RETRY.NEW", &info) ==
                  kernel::vfs::VFS_ERR_NOT_FOUND &&
              kernel::vfs::read_file("/Documents/RETRY.TMP", renameRollbackReadback,
                                     sizeof(renameRollbackReadback)) == renameRollbackSize &&
              std::memcmp(renameRollbackReadback, renameRollbackContents,
                          static_cast<size_t>(renameRollbackSize)) == 0,
          "failed rename preserves the original settings file and bytes for retry");
    g_flushFailureStatus = kernel::block::BLOCK_ERR_IO;
    check(kernel::vfs::rename("/Documents/RETRY.TMP", "/Documents/RETRY.NEW") ==
              kernel::vfs::VFS_OK &&
              kernel::vfs::stat("/Documents/RETRY.NEW", &info) == kernel::vfs::VFS_OK &&
              kernel::vfs::stat("/Documents/RETRY.TMP", &info) ==
                  kernel::vfs::VFS_ERR_NOT_FOUND,
          "the settings-like rename succeeds after the transient flush fault clears");
    check(kernel::vfs::unlink("/Documents/RETRY.NEW") == kernel::vfs::VFS_OK,
          "remove the settings rename retry fixture after requalification");

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

    check(kernel::vfs::create_file("/Documents/ABCDEFGH.TXT", "8.3 boundary", 12) == 12 &&
              kernel::vfs::stat("/Documents/ABCDEFGH.TXT", &info) == kernel::vfs::VFS_OK &&
              kernel::vfs::unlink("/Documents/ABCDEFGH.TXT") == kernel::vfs::VFS_OK,
          "accept a legal eight-character base and three-character extension");
    check(kernel::vfs::create_file("/Documents/ABCDEFGH", "base only", 9) == 9 &&
              kernel::vfs::unlink("/Documents/ABCDEFGH") == kernel::vfs::VFS_OK,
          "accept the eight-character filename boundary without an extension");
    check(kernel::vfs::mkdir("/Documents/DOCS") == kernel::vfs::VFS_OK &&
              kernel::vfs::stat("/Documents/DOCS", &info) == kernel::vfs::VFS_OK &&
              info.type == kernel::vfs::FILE_TYPE_DIRECTORY &&
              kernel::vfs::rmdir("/Documents/DOCS") == kernel::vfs::VFS_OK,
          "create and remove the supported short-name Documents backing directory");
    const int32_t invalidNameStatus = kernel::vfs::create_file(
        "/Documents/ABCDEFGHI.TXT", "invalid", 7);
    check(invalidNameStatus == kernel::vfs::VFS_ERR_INVALID &&
              kernel::vfs::last_open_status() == kernel::vfs::VFS_ERR_INVALID,
          "reject a nine-character base and preserve the invalid-name status");
    check(kernel::vfs::create_file("/Documents/FILE.TOOLONG", "invalid", 7) ==
              kernel::vfs::VFS_ERR_INVALID &&
              kernel::vfs::create_file("/Documents/DOUBLE.DOT.TXT", "invalid", 7) ==
              kernel::vfs::VFS_ERR_INVALID &&
              kernel::vfs::create_file("/Documents/TRAIL.", "invalid", 7) ==
              kernel::vfs::VFS_ERR_INVALID &&
              kernel::vfs::create_file("/Documents/.HIDDEN", "invalid", 7) ==
              kernel::vfs::VFS_ERR_INVALID,
          "reject overlong extensions, multiple dots, trailing dots, and leading dots");
    check(kernel::vfs::create_file("/Documents/BAD:NAME.TXT", "invalid", 7) ==
              kernel::vfs::VFS_ERR_INVALID &&
              kernel::vfs::create_file("/Documents/BAD?NAME.TXT", "invalid", 7) ==
              kernel::vfs::VFS_ERR_INVALID &&
              kernel::vfs::create_file("/Documents/BAD\\NAME.TXT", "invalid", 7) ==
              kernel::vfs::VFS_ERR_INVALID,
          "reject FAT-reserved filename characters and a backslash separator");
    static const char* legacyAuxiliaryPaths[] = {
        "/Documents/display-options.cfg",
        "/Documents/desktop.taskbar.position",
        "/Documents/desktop.system.icons",
        "/Documents/desktop.background.scale",
        "/Documents/.startmenu_recent",
    };
    bool legacyAuxiliaryPathsRejected = true;
    for (const char* path : legacyAuxiliaryPaths) {
        if (kernel::vfs::create_file(path, "invalid", 7) != kernel::vfs::VFS_ERR_INVALID ||
            kernel::vfs::last_open_status() != kernel::vfs::VFS_ERR_INVALID) {
            legacyAuxiliaryPathsRejected = false;
        }
    }
    check(legacyAuxiliaryPathsRejected,
          "reject each exact legacy auxiliary settings filename and preserve invalid-name status");
    bool legacyAuxiliaryWritesRejected = true;
    for (const char* path : legacyAuxiliaryPaths) {
        if (kernel::vfs::write_file(path, "invalid", 7) != kernel::vfs::VFS_ERR_INVALID) {
            legacyAuxiliaryWritesRejected = false;
        }
    }
    check(legacyAuxiliaryWritesRejected,
          "propagate the invalid-name error from each exact legacy auxiliary settings write");
    char normalizedLegacyPath[kernel::vfs::VFS_MAX_PATH]{};
    kernel::vfs::normalize_path("/Documents//./desktop.taskbar.position",
                                normalizedLegacyPath, sizeof(normalizedLegacyPath));
    check(std::strcmp(normalizedLegacyPath, "/Documents/desktop.taskbar.position") == 0 &&
              kernel::vfs::create_file(normalizedLegacyPath, "invalid", 7) ==
                  kernel::vfs::VFS_ERR_INVALID &&
              kernel::vfs::last_open_status() == kernel::vfs::VFS_ERR_INVALID,
          "normalize separators before preserving the legacy invalid-name status");
    check(kernel::vfs::create_file("/Documents/MISSING/CHILD.TXT", "invalid", 7) ==
              kernel::vfs::VFS_ERR_NOT_FOUND,
          "report a missing parent directory separately from an invalid filename");

    check(kernel::vfs::write_file("/Documents/DESKTOP.TMP", "save-one", 8) == 8 &&
              kernel::vfs::write_file("/Documents/DESKTOP.TMP", "save-two", 8) == 8 &&
              kernel::vfs::write_file("/Documents/DESKTOP.TMP", "save-three", 10) == 10 &&
              kernel::vfs::rename("/Documents/DESKTOP.TMP", "/Documents/DESKTOP.NEW") ==
                  kernel::vfs::VFS_OK &&
              kernel::vfs::read_file("/Documents/DESKTOP.NEW", payload.data(),
                  static_cast<uint32_t>(payload.size())) == 10 &&
              std::memcmp(payload.data(), "save-three", 10) == 0 &&
              kernel::vfs::stat("/Documents/DESKTOP.TMP", &info) ==
                  kernel::vfs::VFS_ERR_NOT_FOUND &&
              kernel::vfs::unlink("/Documents/DESKTOP.NEW") == kernel::vfs::VFS_OK,
          "repeatedly replace a settings-style temporary file and reopen its renamed contents");

    static const char pinStoreInitial[] = "version=1\n";
    static const char pinStoreFirst[] = "version=1\nappId=com.guidexos.pacman\n";
    static const char pinStoreSecond[] =
        "version=1\nappId=com.guidexos.pacman\nappId=gxos.builtin.fileexplorer\n";
    check(kernel::vfs::create_file("/Documents/taskbar.pin", pinStoreInitial,
                                   sizeof(pinStoreInitial) - 1) ==
              static_cast<int32_t>(sizeof(pinStoreInitial) - 1),
          "create the taskbar pin store using a FAT-supported 8.3 path");
    check(kernel::vfs::write_file("/Documents/taskbar.pin", pinStoreFirst,
                                  sizeof(pinStoreFirst) - 1) ==
              static_cast<int32_t>(sizeof(pinStoreFirst) - 1),
          "write a taskbar pin through the supported short-name path");
    check(kernel::vfs::write_file("/Documents/taskbar.pin", pinStoreSecond,
                                  sizeof(pinStoreSecond) - 1) ==
              static_cast<int32_t>(sizeof(pinStoreSecond) - 1),
          "replace the taskbar pin store repeatedly without changing its path");
    std::vector<char> pinStoreReadback(sizeof(pinStoreSecond));
    check(kernel::vfs::read_file("/Documents/taskbar.pin", pinStoreReadback.data(),
                                 static_cast<uint32_t>(pinStoreReadback.size())) ==
              static_cast<int32_t>(sizeof(pinStoreSecond) - 1) &&
              std::memcmp(pinStoreReadback.data(), pinStoreSecond,
                          sizeof(pinStoreSecond) - 1) == 0,
          "reopened taskbar pin store contains the complete latest pin set");

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
    g_flushFailureStatus = kernel::block::BLOCK_ERR_IO;
    g_flushFailuresRemaining = 1;
    const kernel::vfs::Status failedClose = flushHandle == 0xFF
        ? kernel::vfs::VFS_ERR_INVALID : kernel::vfs::close(flushHandle);
    g_flushFailuresRemaining = 0;
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

    const uint8_t syncHandle = kernel::vfs::open("/Documents/NOTES/REPORT.BIN",
                                                 kernel::vfs::OPEN_RDWR);
    const bool syncWriteReady = syncHandle != 0xFF &&
        kernel::vfs::seek(syncHandle, 0,
            static_cast<kernel::vfs::SeekOrigin>(0)) == kernel::vfs::VFS_OK &&
        kernel::vfs::write(syncHandle, "S", 1) == 1;
    g_flushFailureStatus = kernel::block::BLOCK_ERR_TIMEOUT;
    g_flushFailuresRemaining = 1;
    const kernel::vfs::Status failedSync = kernel::vfs::sync_all();
    check(syncWriteReady && failedSync == kernel::vfs::VFS_ERR_IO_TIMEOUT &&
              kernel::vfs::get_handle(syncHandle) == nullptr,
          "shutdown sync closes outstanding writes and propagates a close-time flush timeout");
    check(kernel::vfs::sync_all() == kernel::vfs::VFS_OK,
          "filesystem synchronization succeeds after the injected flush failure clears");
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
