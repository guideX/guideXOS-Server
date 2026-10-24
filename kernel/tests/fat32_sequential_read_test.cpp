#include "kernel/block_device.h"
#include "kernel/fs_ext4.h"
#include "kernel/fs_fat.h"
#include "kernel/vfs.h"
#include <arch/amd64.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
static const uint32_t kSectorSize = 512u;
static const uint32_t kDiskSectors = 4096u;
static const uint32_t kFatStart = 1u;
static const uint32_t kFatSectors = 16u;
static const uint32_t kDataStart = kFatStart + kFatSectors;
static const uint32_t kFileFirstCluster = 3u;
static const uint32_t kFileClusters = 300u;
static std::vector<uint8_t> g_disk(kDiskSectors * kSectorSize, 0u);
static std::vector<uint8_t> g_replacementDisk(kDiskSectors * kSectorSize, 0u);
static uint32_t g_fatSectorReads = 0u;
static uint64_t g_deviceGeneration = 1u;
static uint32_t g_checks = 0u;
static uint32_t g_failures = 0u;
static bool g_failNextFatRead = false;
static bool g_changeGenerationDuringFatRead = false;
static bool g_changeGenerationDuringNextRead = false;
static kernel::block::BlockDevice g_fakeBlockDevice{};
static kernel::block::BlockDevice g_replacementBlockDevice{};
static uint64_t g_replacementGeneration = 1u;

void put16(uint8_t* destination, uint16_t value) {
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8);
}

void put32(uint8_t* destination, uint32_t value) {
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8);
    destination[2] = static_cast<uint8_t>(value >> 16);
    destination[3] = static_cast<uint8_t>(value >> 24);
}

void set_fat_entry(uint32_t cluster, uint32_t value) {
    const uint32_t offset = kFatStart * kSectorSize + cluster * 4u;
    put32(g_disk.data() + offset, value);
}

uint32_t cluster_sector(uint32_t cluster) {
    return kDataStart + cluster - 2u;
}

void prepare_disk() {
    uint8_t* bpb = g_disk.data();
    bpb[0] = 0xEB; bpb[1] = 0x58; bpb[2] = 0x90;
    std::memcpy(bpb + 3, "GXOSTEST", 8);
    put16(bpb + 11, kSectorSize);
    bpb[13] = 1u;
    put16(bpb + 14, 1u);
    bpb[16] = 1u;
    put32(bpb + 32, kDiskSectors);
    put32(bpb + 36, kFatSectors);
    put32(bpb + 44, 2u);
    std::memcpy(bpb + 82, "FAT32   ", 8);
    bpb[510] = 0x55; bpb[511] = 0xAA;

    set_fat_entry(2u, 0x0FFFFFFFu);
    for (uint32_t i = 0; i < kFileClusters; ++i) {
        const uint32_t cluster = kFileFirstCluster + i;
        set_fat_entry(cluster, i + 1u == kFileClusters ? 0x0FFFFFFFu : cluster + 1u);
        const uint32_t sector = cluster_sector(cluster);
        for (uint32_t byte = 0; byte < kSectorSize; ++byte)
            g_disk[sector * kSectorSize + byte] = static_cast<uint8_t>(cluster * 7u + byte);
    }

    uint8_t* root = g_disk.data() + cluster_sector(2u) * kSectorSize;
    std::memcpy(root, "TEST    BIN", 11);
    root[11] = 0x20u;
    put16(root + 20, 0u);
    put16(root + 26, static_cast<uint16_t>(kFileFirstCluster));
    put32(root + 28, kSectorSize);
}

bool check(bool passed, const char* label) {
    ++g_checks;
    std::cout << (passed ? "PASS: " : "FAIL: ") << label << '\n';
    if (!passed) ++g_failures;
    return passed;
}

static uint32_t g_rootMountChecks = 0u;
static uint32_t g_rootMountFailures = 0u;

bool check_root_mount(bool passed, const char* label) {
    ++g_rootMountChecks;
    std::cout << (passed ? "PASS: " : "FAIL: ") << label << '\n';
    if (!passed) ++g_rootMountFailures;
    return passed;
}

bool read_vfs_file(const char* path, const uint8_t* expected, uint32_t expectedBytes) {
    const uint8_t handle = kernel::vfs::open(path, kernel::vfs::OPEN_READ);
    if (handle == 0xFFu || expectedBytes > 128u) return false;
    uint8_t content[128] = {};
    const int32_t bytesRead = kernel::vfs::read(handle, content, expectedBytes);
    const kernel::vfs::Status closeStatus = kernel::vfs::close(handle);
    return bytesRead == static_cast<int32_t>(expectedBytes) &&
        closeStatus == kernel::vfs::VFS_OK &&
        std::memcmp(content, expected, expectedBytes) == 0;
}

uint8_t short_name_checksum(const char shortName[11]) {
    uint8_t checksum = 0u;
    for (uint32_t i = 0u; i < 11u; ++i) {
        checksum = static_cast<uint8_t>(((checksum & 1u) ? 0x80u : 0u) +
            (checksum >> 1) + static_cast<uint8_t>(shortName[i]));
    }
    return checksum;
}

void write_short_entry(uint32_t directoryCluster, uint32_t slot,
                       const char shortName[11], uint8_t attributes,
                       uint32_t firstCluster, uint32_t fileSize) {
    uint8_t* entry = g_disk.data() + cluster_sector(directoryCluster) * kSectorSize + slot * 32u;
    std::memset(entry, 0, 32u);
    std::memcpy(entry, shortName, 11u);
    entry[11] = attributes;
    put16(entry + 20u, static_cast<uint16_t>(firstCluster >> 16));
    put16(entry + 26u, static_cast<uint16_t>(firstCluster));
    put32(entry + 28u, fileSize);
}

void write_long_entry(uint32_t directoryCluster, uint32_t& slot,
                      const char* longName, const char shortName[11],
                      uint8_t attributes, uint32_t firstCluster,
                      const void* content, uint32_t fileSize) {
    const uint32_t nameLength = static_cast<uint32_t>(std::strlen(longName));
    const uint32_t lfnSlots = (nameLength + 12u) / 13u;
    const uint8_t checksum = short_name_checksum(shortName);
    for (uint32_t sequence = lfnSlots; sequence > 0u; --sequence) {
        uint8_t* entry = g_disk.data() + cluster_sector(directoryCluster) * kSectorSize + slot * 32u;
        std::memset(entry, 0, 32u);
        entry[0] = static_cast<uint8_t>(sequence | (sequence == lfnSlots ? 0x40u : 0u));
        entry[11] = 0x0Fu;
        entry[12] = 0u;
        entry[13] = checksum;
        put16(entry + 26u, 0u);
        for (uint32_t character = 0u; character < 13u; ++character) {
            const uint32_t position = (sequence - 1u) * 13u + character;
            const uint16_t value = position < nameLength
                ? static_cast<uint8_t>(longName[position])
                : (position == nameLength ? 0u : 0xFFFFu);
            const uint32_t offset = character < 5u ? 1u + character * 2u
                : (character < 11u ? 14u + (character - 5u) * 2u
                                   : 28u + (character - 11u) * 2u);
            put16(entry + offset, value);
        }
        ++slot;
    }
    write_short_entry(directoryCluster, slot++, shortName, attributes, firstCluster, fileSize);
    if (content && fileSize != 0u) {
        const uint32_t sector = cluster_sector(firstCluster);
        std::memcpy(g_disk.data() + sector * kSectorSize, content, fileSize);
    }
    set_fat_entry(firstCluster, 0x0FFFFFFFu);
}

void prepare_app_model_tree(const char* developerManifest,
                            const char* pacmanManifest,
                            const char* navigatorManifest,
                            const uint8_t elfHeader[64]) {
    static const char appsAlias[11] = {'A','P','P','S',' ',' ',' ',' ',' ',' ',' '};
    static const char developerAlias[11] = {'D','E','V','S','T','U','~','1',' ',' ',' '};
    static const char pacmanAlias[11] = {'P','A','C','M','A','N',' ',' ',' ',' ',' '};
    static const char navigatorAlias[11] = {'S','T','E','L','L','~','1',' ',' ',' ',' '};
    static const char binAlias[11] = {'B','I','N',' ',' ',' ',' ',' ',' ',' ',' '};
    static const char amd64Alias[11] = {'A','M','D','6','4',' ',' ',' ',' ',' ',' '};
    static const char manifestAlias[11] = {'A','P','P',' ',' ',' ',' ',' ','J','S','O'};
    static const char elfAlias[11] = {'S','T','E','L','L','~','1',' ','E','L','F'};
    const uint32_t appsCluster = 303u;
    const uint32_t developerCluster = 304u;
    const uint32_t pacmanCluster = 305u;
    const uint32_t navigatorCluster = 306u;
    const uint32_t binCluster = 307u;
    const uint32_t amd64Cluster = 308u;

    set_fat_entry(appsCluster, 0x0FFFFFFFu);
    write_short_entry(2u, 1u, appsAlias, 0x10u, appsCluster, 0u);

    uint32_t slot = 0u;
    write_long_entry(appsCluster, slot, "DeveloperStudio", developerAlias, 0x10u,
        developerCluster, nullptr, 0u);
    write_long_entry(appsCluster, slot, "PacMan", pacmanAlias, 0x10u,
        pacmanCluster, nullptr, 0u);
    write_long_entry(appsCluster, slot, "StellarNavigator", navigatorAlias, 0x10u,
        navigatorCluster, nullptr, 0u);

    slot = 0u;
    write_long_entry(developerCluster, slot, "app.json", manifestAlias, 0x20u,
        309u, developerManifest, static_cast<uint32_t>(std::strlen(developerManifest)));
    slot = 0u;
    write_long_entry(pacmanCluster, slot, "app.json", manifestAlias, 0x20u,
        310u, pacmanManifest, static_cast<uint32_t>(std::strlen(pacmanManifest)));
    slot = 0u;
    write_long_entry(navigatorCluster, slot, "app.json", manifestAlias, 0x20u,
        311u, navigatorManifest, static_cast<uint32_t>(std::strlen(navigatorManifest)));
    write_long_entry(navigatorCluster, slot, "bin", binAlias, 0x10u,
        binCluster, nullptr, 0u);
    slot = 0u;
    write_long_entry(binCluster, slot, "amd64", amd64Alias, 0x10u,
        amd64Cluster, nullptr, 0u);
    slot = 0u;
    write_long_entry(amd64Cluster, slot, "stellarnavigator.elf", elfAlias, 0x20u,
        312u, elfHeader, 64u);
}
}

namespace kernel {
namespace arch {
namespace amd64 {
uint8_t inb(uint16_t) { return 0x20u; }
void outb(uint16_t, uint8_t) { }
}
}
namespace block {
Status read_sectors(uint8_t device, uint64_t lba, uint32_t count, void* buffer) {
    const bool active = device == 0u ? g_fakeBlockDevice.active
        : (device == 1u && g_replacementBlockDevice.active);
    if (!active || device > 1u || !buffer || count == 0u ||
        lba + count > kDiskSectors)
        return BLOCK_ERR_INVALID;
    std::vector<uint8_t>& disk = device == 0u ? g_disk : g_replacementDisk;
    const bool isFat = lba < kFatStart + kFatSectors && lba + count > kFatStart;
    if (isFat && device == 0u)
        g_fatSectorReads += count;
    if (isFat && device == 0u && g_failNextFatRead) {
        g_failNextFatRead = false;
        return BLOCK_ERR_IO;
    }
    std::memcpy(buffer, disk.data() + lba * kSectorSize, count * kSectorSize);
    if (device == 0u && g_changeGenerationDuringNextRead) {
        g_changeGenerationDuringNextRead = false;
        ++g_deviceGeneration;
    }
    if (isFat && device == 0u && g_changeGenerationDuringFatRead) {
        g_changeGenerationDuringFatRead = false;
        ++g_deviceGeneration;
    }
    return BLOCK_OK;
}

Status write_sectors(uint8_t device, uint64_t lba, uint32_t count, const void* buffer) {
    const bool active = device == 0u ? g_fakeBlockDevice.active
        : (device == 1u && g_replacementBlockDevice.active);
    if (!active || device > 1u || !buffer || count == 0u ||
        lba + count > kDiskSectors)
        return BLOCK_ERR_INVALID;
    std::vector<uint8_t>& disk = device == 0u ? g_disk : g_replacementDisk;
    std::memcpy(disk.data() + lba * kSectorSize, buffer, count * kSectorSize);
    return BLOCK_OK;
}

Status flush(uint8_t device) {
    return (device == 0u && g_fakeBlockDevice.active) ||
        (device == 1u && g_replacementBlockDevice.active) ? BLOCK_OK : BLOCK_ERR_INVALID;
}
uint64_t device_generation(uint8_t device) {
    if (device == 0u && g_fakeBlockDevice.active) return g_deviceGeneration;
    if (device == 1u && g_replacementBlockDevice.active) return g_replacementGeneration;
    return 0u;
}
const BlockDevice* get_device(uint8_t device) {
    if (device == 0u && g_fakeBlockDevice.active) return &g_fakeBlockDevice;
    if (device == 1u && g_replacementBlockDevice.active) return &g_replacementBlockDevice;
    return nullptr;
}
}
namespace fs_ext4 {
uint8_t mount(uint8_t) { return 0xFFu; }
void unmount(uint8_t) { }
bool open_root_dir(uint8_t) { return false; }
bool read_dir(uint8_t, Ext4DirEntry*) { return false; }
}
}

int main() {
    g_fakeBlockDevice.active = true;
    g_fakeBlockDevice.type = kernel::block::BDEV_ATA_PIO;
    g_fakeBlockDevice.totalSectors = kDiskSectors;
    g_fakeBlockDevice.sectorSize = kSectorSize;
    std::memcpy(g_fakeBlockDevice.name, "fake-fat32", 11);
    g_replacementBlockDevice.active = true;
    g_replacementBlockDevice.type = kernel::block::BDEV_ATA_PIO;
    g_replacementBlockDevice.totalSectors = kDiskSectors;
    g_replacementBlockDevice.sectorSize = kSectorSize;
    std::memcpy(g_replacementBlockDevice.name, "replacement", 12);
    prepare_disk();
    kernel::fs_fat::init();
    const uint8_t volume = kernel::fs_fat::mount(0u);
    if (!check(volume != 0xFFu, "mount synthetic FAT32 volume")) return 1;

    const uint32_t fileBytes = kFileClusters * kSectorSize;
    const uint8_t handle = kernel::fs_fat::open_file(volume, kFileFirstCluster, fileBytes, 0u);
    if (!check(handle != 0xFFu, "open long sequential file cursor")) return 1;
    std::vector<uint8_t> output(fileBytes, 0u);
    uint32_t offset = 0u;
    bool complete = true;
    while (offset < fileBytes) {
        const uint32_t requested = (fileBytes - offset) > 65536u ? 65536u : fileBytes - offset;
        const uint32_t amount = kernel::fs_fat::read_file(handle, output.data() + offset, requested);
        if (amount != requested) { complete = false; break; }
        offset += amount;
    }
    bool dataMatches = complete;
    for (uint32_t clusterIndex = 0; dataMatches && clusterIndex < kFileClusters; ++clusterIndex) {
        const uint32_t cluster = kFileFirstCluster + clusterIndex;
        for (uint32_t byte = 0; byte < kSectorSize; ++byte) {
            if (output[clusterIndex * kSectorSize + byte] !=
                static_cast<uint8_t>(cluster * 7u + byte)) {
                dataMatches = false;
                break;
            }
        }
    }
    const bool cacheReducedReads = g_fatSectorReads < 100u;
    const uint32_t sequentialFatSectorReads = g_fatSectorReads;
    kernel::fs_fat::close_file(handle);
    if (!check(dataMatches, "read and validate all bytes across sequential 64 KiB chunks")) return 1;
    if (!check(cacheReducedReads, "reuse FAT sectors during sequential chain and cycle checks")) {
        std::cerr << "fatSectorReads=" << g_fatSectorReads << '\n';
        return 1;
    }

    kernel::fs_fat::unmount(volume);
    set_fat_entry(kFileFirstCluster, 0x0FFFFFFFu);
    const uint8_t writeVolume = kernel::fs_fat::mount(0u);
    if (!check(writeVolume != 0xFFu, "remount before isolated chain-extension check")) return 1;
    const uint8_t writeHandle = kernel::fs_fat::open_file(writeVolume, kFileFirstCluster,
        kSectorSize, 0u);
    if (!check(writeHandle != 0xFFu, "reopen file for bounded chain-extension regression")) return 1;
    uint8_t prefix[kSectorSize];
    if (!check(kernel::fs_fat::read_file(writeHandle, prefix, sizeof(prefix)) == sizeof(prefix),
            "prime FAT-sector cache before chain extension")) return 1;
    if (!check(kernel::fs_fat::seek_file(writeHandle, 0u), "rewind file before extension")) return 1;
    uint8_t extended[513];
    for (uint32_t i = 0; i < sizeof(extended); ++i)
        extended[i] = static_cast<uint8_t>(0xA0u + i);
    if (!check(kernel::fs_fat::write_file(writeHandle, extended, sizeof(extended)) == sizeof(extended),
            "extend chain through FAT write and invalidate cached sector")) return 1;
    if (!check(kernel::fs_fat::seek_file(writeHandle, 0u), "rewind extended file")) return 1;
    uint8_t readBack[513] = {};
    if (!check(kernel::fs_fat::read_file(writeHandle, readBack, sizeof(readBack)) == sizeof(readBack) &&
            std::memcmp(readBack, extended, sizeof(extended)) == 0,
            "read newly linked cluster after cache invalidation")) return 1;
    kernel::fs_fat::close_file(writeHandle);
    kernel::fs_fat::unmount(writeVolume);

    const uint8_t cacheVolume = kernel::fs_fat::mount(0u);
    const uint8_t cacheHandle = kernel::fs_fat::open_file(cacheVolume,
        kFileFirstCluster, 2u * kSectorSize, 0u);
    uint8_t oneSector[kSectorSize] = {};
    const bool firstRead = cacheHandle != 0xFFu &&
        kernel::fs_fat::read_file(cacheHandle, oneSector, sizeof(oneSector)) == sizeof(oneSector);
    const uint32_t readsAfterPrime = g_fatSectorReads;
    const bool rewound = cacheHandle != 0xFFu && kernel::fs_fat::seek_file(cacheHandle, 0u);
    const bool sameGenerationHit = rewound &&
        kernel::fs_fat::read_file(cacheHandle, oneSector, sizeof(oneSector)) == sizeof(oneSector) &&
        g_deviceGeneration == 1u && g_fatSectorReads == readsAfterPrime;
    if (!check(firstRead && sameGenerationHit,
            "same device generation reuses the cached FAT sector")) return 1;

    ++g_deviceGeneration;
    const bool staleFileRejected =
        kernel::fs_fat::read_file(cacheHandle, oneSector, sizeof(oneSector)) == 0u;
    if (!check(staleFileRejected,
            "incremented device generation rejects an open FAT file cursor")) return 1;
    kernel::fs_fat::close_file(cacheHandle);
    kernel::fs_fat::unmount(cacheVolume);

    set_fat_entry(kFileFirstCluster, 0x0FFFFFFFu);
    for (uint32_t byte = 0; byte < kSectorSize; ++byte)
        g_disk[cluster_sector(kFileFirstCluster) * kSectorSize + byte] =
            static_cast<uint8_t>(0xE0u ^ byte);
    const uint8_t replacementVolume = kernel::fs_fat::mount(0u);
    const uint8_t replacementHandle = kernel::fs_fat::open_file(replacementVolume,
        kFileFirstCluster, 2u * kSectorSize, 0u);
    uint8_t replacementData[2u * kSectorSize] = {};
    const uint32_t replacementBytes = replacementHandle == 0xFFu ? 0u :
        kernel::fs_fat::read_file(replacementHandle, replacementData, sizeof(replacementData));
    bool replacementMatches = replacementBytes == kSectorSize;
    for (uint32_t byte = 0; replacementMatches && byte < kSectorSize; ++byte)
        replacementMatches = replacementData[byte] == static_cast<uint8_t>(0xE0u ^ byte);
    if (!check(replacementMatches,
            "same-index replacement returns its new FAT and payload data after remount")) return 1;
    kernel::fs_fat::close_file(replacementHandle);
    kernel::fs_fat::unmount(replacementVolume);

    const uint8_t changingVolume = kernel::fs_fat::mount(0u);
    const uint8_t changingHandle = kernel::fs_fat::open_file(changingVolume,
        kFileFirstCluster, kSectorSize, 0u);
    g_changeGenerationDuringFatRead = true;
    const uint32_t beforeChangingRead = g_fatSectorReads;
    const uint32_t changedReadBytes = changingHandle == 0xFFu ? 0u :
        kernel::fs_fat::read_file(changingHandle, oneSector, sizeof(oneSector));
    const bool generationChangeRejected = changedReadBytes == 0u &&
        g_deviceGeneration == 3u && g_fatSectorReads > beforeChangingRead;
    if (!check(generationChangeRejected,
            "generation change during FAT-sector read fails closed")) return 1;
    kernel::fs_fat::close_file(changingHandle);
    kernel::fs_fat::unmount(changingVolume);

    g_changeGenerationDuringNextRead = true;
    const uint8_t interruptedMount = kernel::fs_fat::mount(0u);
    const bool mountTransitionRejected = interruptedMount == 0xFFu &&
        g_deviceGeneration == 4u;
    if (!check(mountTransitionRejected,
            "device generation change during FAT mount initialization is rejected")) return 1;
    const uint8_t recoveredMount = kernel::fs_fat::mount(0u);
    const uint8_t recoveredHandle = recoveredMount == 0xFFu ? 0xFFu :
        kernel::fs_fat::open_file(recoveredMount, kFileFirstCluster, kSectorSize, 0u);
    const bool recoveredMountUsable = recoveredHandle != 0xFFu;
    if (!check(recoveredMountUsable,
            "fresh FAT mount succeeds after initialization generation transition")) return 1;
    if (recoveredMountUsable) kernel::fs_fat::close_file(recoveredHandle);
    kernel::fs_fat::unmount(recoveredMount);

    const uint8_t failureVolume = kernel::fs_fat::mount(0u);
    const uint8_t failureHandle = kernel::fs_fat::open_file(failureVolume,
        kFileFirstCluster, kSectorSize, 0u);
    g_failNextFatRead = true;
    const uint32_t beforeFailedRead = g_fatSectorReads;
    const uint32_t failedBytes = kernel::fs_fat::read_file(failureHandle,
        oneSector, sizeof(oneSector));
    const uint32_t afterFailedRead = g_fatSectorReads;
    const uint32_t retryBytes = kernel::fs_fat::read_file(failureHandle,
        oneSector, sizeof(oneSector));
    if (!check(failedBytes == 0u && retryBytes == sizeof(oneSector) &&
            afterFailedRead >= beforeFailedRead + 2u,
            "failed FAT-sector read is not cached and valid retry succeeds")) return 1;
    kernel::fs_fat::close_file(failureHandle);
    kernel::fs_fat::unmount(failureVolume);

    const uint8_t beforeRemountVolume = kernel::fs_fat::mount(0u);
    const uint8_t beforeRemountHandle = kernel::fs_fat::open_file(beforeRemountVolume,
        kFileFirstCluster, kSectorSize, 0u);
    const bool beforeRemountRead = kernel::fs_fat::read_file(beforeRemountHandle,
        oneSector, sizeof(oneSector)) == sizeof(oneSector);
    kernel::fs_fat::close_file(beforeRemountHandle);
    kernel::fs_fat::unmount(beforeRemountVolume);
    const uint32_t readsBeforeRemount = g_fatSectorReads;
    const uint8_t afterRemountVolume = kernel::fs_fat::mount(0u);
    const uint8_t afterRemountHandle = kernel::fs_fat::open_file(afterRemountVolume,
        kFileFirstCluster, kSectorSize, 0u);
    const bool afterRemountRead = kernel::fs_fat::read_file(afterRemountHandle,
        oneSector, sizeof(oneSector)) == sizeof(oneSector);
    if (!check(beforeRemountRead && afterRemountRead &&
            g_fatSectorReads > readsBeforeRemount,
            "same-generation unmount/remount invalidates FAT cache entries")) return 1;
    kernel::fs_fat::close_file(afterRemountHandle);
    kernel::fs_fat::unmount(afterRemountVolume);

    kernel::vfs::init();
    const uint8_t vfsMount = kernel::vfs::mount_type("/", 0u,
        kernel::vfs::FS_TYPE_FAT32);
    const uint8_t vfsHandle = kernel::vfs::open("/TEST.BIN", kernel::vfs::OPEN_READ);
    const bool vfsOpened = vfsMount != 0xFFu && vfsHandle != 0xFFu;
    const int32_t vfsRead = vfsOpened
        ? kernel::vfs::read(vfsHandle, oneSector, sizeof(oneSector)) : 0;
    ++g_deviceGeneration;
    const int32_t staleVfsRead = vfsOpened
        ? kernel::vfs::read(vfsHandle, oneSector, sizeof(oneSector)) : 0;
    const bool staleHandleSafe = vfsRead == static_cast<int32_t>(sizeof(oneSector)) &&
        staleVfsRead == kernel::vfs::VFS_ERR_NOT_MOUNT &&
        kernel::vfs::tell(vfsHandle) == -1;
    const uint8_t stalePathHandle = kernel::vfs::open("/TEST.BIN", kernel::vfs::OPEN_READ);
    const bool staleRootClassified = stalePathHandle == 0xFFu &&
        kernel::vfs::last_mount_lookup_result() == kernel::vfs::MOUNT_STALE_GENERATION;
    const kernel::vfs::Status staleClose = kernel::vfs::close(vfsHandle);
    const kernel::vfs::Status staleUnmount = kernel::vfs::unmount("/");
    if (!check(staleHandleSafe && staleRootClassified &&
            staleClose == kernel::vfs::VFS_ERR_NOT_MOUNT &&
            staleUnmount == kernel::vfs::VFS_OK,
            "stale VFS handle fails closed and releases the mount for remount")) return 1;

    const uint8_t replacementMount = kernel::vfs::mount_type("/", 0u,
        kernel::vfs::FS_TYPE_FAT32);
    const uint8_t replacementVfsHandle = kernel::vfs::open("/TEST.BIN",
        kernel::vfs::OPEN_READ);
    uint8_t reopenedData[kSectorSize] = {};
    const int32_t reopenedBytes = replacementVfsHandle == 0xFFu ? 0 :
        kernel::vfs::read(replacementVfsHandle, reopenedData, sizeof(reopenedData));
    bool reopenedMatches = replacementMount != 0xFFu &&
        reopenedBytes == static_cast<int32_t>(kSectorSize);
    for (uint32_t byte = 0; reopenedMatches && byte < kSectorSize; ++byte)
        reopenedMatches = reopenedData[byte] == static_cast<uint8_t>(0xE0u ^ byte);
    if (!check(reopenedMatches,
            "explicit VFS remount and reopen reads replacement media data")) return 1;
    kernel::vfs::close(replacementVfsHandle);
    kernel::vfs::unmount("/");

    // Reproduce package discovery followed by the native ELF header open on
    // the same root volume, including handle closes between manifests.
    const char developerManifest[] = "{\"id\":\"com.guidexos.developerstudio\"}";
    const char pacmanManifest[] = "{\"id\":\"com.guidexos.pacman\"}";
    const char navigatorManifest[] = "{\"id\":\"com.guidexos.stellarnavigator\"}";
    uint8_t elfHeader[64] = {};
    elfHeader[0] = 0x7Fu; elfHeader[1] = 'E'; elfHeader[2] = 'L'; elfHeader[3] = 'F';
    elfHeader[4] = 2u; elfHeader[5] = 1u; elfHeader[6] = 1u;
    put16(elfHeader + 16u, 2u); put16(elfHeader + 18u, 62u);
    prepare_app_model_tree(developerManifest, pacmanManifest, navigatorManifest, elfHeader);
    const uint8_t appVolume = kernel::fs_fat::mount(0u);
    kernel::fs_fat::DirEntry appElfEntry{};
    const bool appTreeCreated = appVolume != 0xFFu &&
        kernel::fs_fat::lookup_path(appVolume,
            "/Apps/StellarNavigator/bin/amd64/stellarnavigator.elf", &appElfEntry) &&
        !appElfEntry.isDir && appElfEntry.fileSize == sizeof(elfHeader);
    kernel::fs_fat::unmount(appVolume);
    if (!check_root_mount(appTreeCreated,
            "create synthetic AppModel manifests and native ELF package tree")) return 1;
    g_replacementDisk = g_disk;
    std::memcpy(g_replacementDisk.data() + cluster_sector(2u) * kSectorSize,
        g_disk.data() + cluster_sector(303u) * kSectorSize, kSectorSize);
    uint8_t replacementElfHeader[sizeof(elfHeader)];
    std::memcpy(replacementElfHeader, elfHeader, sizeof(elfHeader));
    replacementElfHeader[7] = 0xA5u;
    std::memcpy(g_replacementDisk.data() + cluster_sector(312u) * kSectorSize,
        replacementElfHeader, sizeof(replacementElfHeader));

    const uint8_t packageMount = kernel::vfs::mount_type("/", 0u,
        kernel::vfs::FS_TYPE_FAT32);
    const kernel::vfs::MountPoint* rootOwner = kernel::vfs::get_mount("/");
    const uint64_t packageGeneration = kernel::block::device_generation(0u);
    const bool rootOwnershipReady = packageMount != 0xFFu && rootOwner &&
        rootOwner->active && rootOwner->blockDevIndex == 0u &&
        rootOwner->blockDeviceGeneration == packageGeneration &&
        kernel::vfs::mount_index_for_path("/") == packageMount &&
        kernel::vfs::mount_index_for_path("/Apps/StellarNavigator/bin/amd64/stellarnavigator.elf") == packageMount;
    if (!check_root_mount(rootOwnershipReady,
            "root mount owns package and ELF paths at captured device generation")) return 1;

    const uint8_t relativePathHandle = kernel::vfs::open("Apps/StellarNavigator/app.json",
        kernel::vfs::OPEN_READ);
    const bool missingMountClassified = relativePathHandle == 0xFFu &&
        kernel::vfs::last_mount_lookup_result() == kernel::vfs::MOUNT_NOT_FOUND;
    if (!check_root_mount(missingMountClassified,
            "path with no matching mount is classified as MOUNT_NOT_FOUND")) return 1;

    const bool manifestReads =
        read_vfs_file("/Apps/DeveloperStudio/app.json",
            reinterpret_cast<const uint8_t*>(developerManifest), sizeof(developerManifest) - 1u) &&
        read_vfs_file("/Apps/PacMan/app.json",
            reinterpret_cast<const uint8_t*>(pacmanManifest), sizeof(pacmanManifest) - 1u) &&
        read_vfs_file("/Apps/StellarNavigator/app.json",
            reinterpret_cast<const uint8_t*>(navigatorManifest), sizeof(navigatorManifest) - 1u);
    rootOwner = kernel::vfs::get_mount("/");
    const bool rootSurvivedDiscovery = manifestReads && rootOwner && rootOwner->active &&
        rootOwner->blockDevIndex == 0u && rootOwner->blockDeviceGeneration == packageGeneration &&
        kernel::vfs::mount_index_for_path("/Apps/StellarNavigator/app.json") == packageMount &&
        kernel::block::device_generation(0u) == packageGeneration;
    if (!check_root_mount(rootSurvivedDiscovery,
            "manifest opens and closes leave root mount selected and generation stable")) return 1;

    const char* elfPath = "/Apps/StellarNavigator/bin/amd64/stellarnavigator.elf";
    const uint8_t elfHandle = kernel::vfs::open(elfPath, kernel::vfs::OPEN_READ);
    uint8_t observedElfHeader[sizeof(elfHeader)] = {};
    const int32_t elfHeaderBytes = elfHandle == 0xFFu ? 0 :
        kernel::vfs::read(elfHandle, observedElfHeader, sizeof(observedElfHeader));
    const kernel::vfs::Status elfClose = elfHandle == 0xFFu
        ? kernel::vfs::VFS_ERR_INVALID : kernel::vfs::close(elfHandle);
    const bool elfHeaderRead = elfHeaderBytes == static_cast<int32_t>(sizeof(elfHeader)) &&
        elfClose == kernel::vfs::VFS_OK &&
        std::memcmp(observedElfHeader, elfHeader, sizeof(elfHeader)) == 0 &&
        kernel::vfs::mount_index_for_path(elfPath) == packageMount;
    if (!check_root_mount(elfHeaderRead,
            "resolve native ELF path and read complete 64-byte header")) return 1;

    const uint8_t nestedMount = kernel::vfs::mount_type("/Apps", 1u,
        kernel::vfs::FS_TYPE_FAT32);
    const uint8_t nestedOwner = kernel::vfs::mount_index_for_path(elfPath);
    const uint8_t rootPathOwner = kernel::vfs::mount_index_for_path("/desktop.cfg");
    const bool nestedOwnership = nestedMount != 0xFFu && nestedOwner == nestedMount &&
        rootPathOwner == packageMount && kernel::vfs::get_mount("/") != nullptr &&
        read_vfs_file("/Apps/StellarNavigator/app.json",
            reinterpret_cast<const uint8_t*>(navigatorManifest), sizeof(navigatorManifest) - 1u);
    if (!nestedOwnership) {
        std::cout << "nestedMount=" << static_cast<uint32_t>(nestedMount)
                  << " nestedOwner=" << static_cast<uint32_t>(nestedOwner)
                  << " packageMount=" << static_cast<uint32_t>(packageMount)
                  << " rootPathOwner=" << static_cast<uint32_t>(rootPathOwner)
                  << " secondaryGeneration=" << g_replacementGeneration << '\n';
    }
    if (!check_root_mount(nestedOwnership,
            "nested /Apps mount wins its prefix while root continues to own other paths")) return 1;

    const uint8_t nestedElfHandle = kernel::vfs::open(elfPath, kernel::vfs::OPEN_READ);
    std::memset(observedElfHeader, 0, sizeof(observedElfHeader));
    const int32_t nestedElfBytes = nestedElfHandle == 0xFFu ? 0 :
        kernel::vfs::read(nestedElfHandle, observedElfHeader, sizeof(observedElfHeader));
    const kernel::vfs::Status nestedElfClose = nestedElfHandle == 0xFFu
        ? kernel::vfs::VFS_ERR_INVALID : kernel::vfs::close(nestedElfHandle);
    const bool nestedElfRead = nestedElfBytes == static_cast<int32_t>(sizeof(elfHeader)) &&
        nestedElfClose == kernel::vfs::VFS_OK &&
        std::memcmp(observedElfHeader, replacementElfHeader, sizeof(replacementElfHeader)) == 0;
    if (!nestedElfRead) {
        std::cout << "nestedElfBytes=" << nestedElfBytes
                  << " nestedElfClose=" << static_cast<int32_t>(nestedElfClose)
                  << " nestedElfFirstByte=" << static_cast<uint32_t>(observedElfHeader[0])
                  << " nestedOwnerNow=" << static_cast<uint32_t>(kernel::vfs::mount_index_for_path(elfPath))
                  << '\n';
    }
    if (!check_root_mount(nestedElfRead,
            "native ELF header remains readable through nested mount prefix")) return 1;

    const uint8_t staleNestedHandle = kernel::vfs::open(elfPath, kernel::vfs::OPEN_READ);
    ++g_replacementGeneration;
    const int32_t staleNestedRead = staleNestedHandle == 0xFFu ? 0 :
        kernel::vfs::read(staleNestedHandle, observedElfHeader, sizeof(observedElfHeader));
    const uint8_t staleNestedPathHandle = kernel::vfs::open(elfPath, kernel::vfs::OPEN_READ);
    const bool staleNestedFailsClosed = staleNestedHandle != 0xFFu &&
        staleNestedRead == kernel::vfs::VFS_ERR_NOT_MOUNT &&
        staleNestedPathHandle == 0xFFu &&
        kernel::vfs::last_mount_lookup_result() == kernel::vfs::MOUNT_STALE_GENERATION &&
        kernel::vfs::mount_index_for_path("/desktop.cfg") == packageMount;
    if (!check_root_mount(staleNestedFailsClosed,
            "stale nested mount blocks parent fallback while unrelated root paths remain valid")) return 1;
    const kernel::vfs::Status staleNestedClose = staleNestedHandle == 0xFFu
        ? kernel::vfs::VFS_ERR_INVALID : kernel::vfs::close(staleNestedHandle);
    const kernel::vfs::Status staleNestedUnmount = kernel::vfs::unmount("/Apps");
    const uint8_t rootFallbackOwner = kernel::vfs::mount_index_for_path(elfPath);
    const uint8_t rootFallbackHandle = kernel::vfs::open(elfPath, kernel::vfs::OPEN_READ);
    uint8_t rootFallbackHeader[sizeof(elfHeader)] = {};
    const int32_t rootFallbackBytes = rootFallbackHandle == 0xFFu ? 0 :
        kernel::vfs::read(rootFallbackHandle, rootFallbackHeader, sizeof(rootFallbackHeader));
    const kernel::vfs::Status rootFallbackClose = rootFallbackHandle == 0xFFu
        ? kernel::vfs::VFS_ERR_INVALID : kernel::vfs::close(rootFallbackHandle);
    const bool rootRestoredAfterInvalidation = staleNestedClose == kernel::vfs::VFS_ERR_NOT_MOUNT &&
        staleNestedUnmount == kernel::vfs::VFS_OK && rootFallbackOwner == packageMount &&
        rootFallbackBytes == static_cast<int32_t>(sizeof(elfHeader)) &&
        rootFallbackClose == kernel::vfs::VFS_OK &&
        std::memcmp(rootFallbackHeader, elfHeader, sizeof(elfHeader)) == 0;
    if (!check_root_mount(rootRestoredAfterInvalidation,
            "explicitly removing stale nested mount restores valid root ownership")) return 1;

    const uint8_t freshNestedMount = kernel::vfs::mount_type("/Apps", 1u,
        kernel::vfs::FS_TYPE_FAT32);
    const uint8_t freshNestedHandle = kernel::vfs::open(elfPath, kernel::vfs::OPEN_READ);
    std::memset(observedElfHeader, 0, sizeof(observedElfHeader));
    const int32_t freshNestedBytes = freshNestedHandle == 0xFFu ? 0 :
        kernel::vfs::read(freshNestedHandle, observedElfHeader, sizeof(observedElfHeader));
    const kernel::vfs::Status freshNestedClose = freshNestedHandle == 0xFFu
        ? kernel::vfs::VFS_ERR_INVALID : kernel::vfs::close(freshNestedHandle);
    const bool freshNestedWorks = freshNestedMount != 0xFFu &&
        kernel::vfs::mount_index_for_path(elfPath) == freshNestedMount &&
        freshNestedBytes == static_cast<int32_t>(sizeof(replacementElfHeader)) &&
        freshNestedClose == kernel::vfs::VFS_OK &&
        std::memcmp(observedElfHeader, replacementElfHeader, sizeof(replacementElfHeader)) == 0;
    if (!check_root_mount(freshNestedWorks,
            "fresh nested mount reads replacement media at its new generation")) return 1;

    kernel::vfs::unmount("/Apps");
    kernel::vfs::unmount("/");
    const uint8_t invalidatedPathHandle = kernel::vfs::open(elfPath, kernel::vfs::OPEN_READ);
    const bool explicitInvalidationReported = invalidatedPathHandle == 0xFFu &&
        kernel::vfs::last_mount_lookup_result() == kernel::vfs::MOUNT_INVALIDATED;
    if (!check_root_mount(explicitInvalidationReported,
            "unmounted root is classified as explicitly invalidated")) return 1;

    std::cout << "fat32SequentialReadChecks=11/11 sequentialFatSectorReads="
              << sequentialFatSectorReads << "\n";
    std::cout << "fat32GenerationSafetyChecks=" << (g_checks - 11u) << "/"
              << (g_checks - 11u) << " totalChecks=" << g_checks
              << " failures=" << g_failures << '\n';
    std::cout << "rootMountRegressionChecks="
              << (g_rootMountChecks - g_rootMountFailures) << '/'
              << g_rootMountChecks << " failures=" << g_rootMountFailures << '\n';
    if (g_rootMountFailures != 0u) return 1;
    return 0;
}
