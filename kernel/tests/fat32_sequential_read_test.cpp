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
static uint32_t g_fatSectorReads = 0u;
static uint64_t g_deviceGeneration = 1u;
static uint32_t g_checks = 0u;
static uint32_t g_failures = 0u;
static bool g_failNextFatRead = false;
static bool g_changeGenerationDuringFatRead = false;
static kernel::block::BlockDevice g_fakeBlockDevice{};

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
    if (device != 0u || !g_fakeBlockDevice.active || !buffer || count == 0u ||
        lba + count > kDiskSectors)
        return BLOCK_ERR_INVALID;
    const bool isFat = lba < kFatStart + kFatSectors && lba + count > kFatStart;
    if (isFat)
        g_fatSectorReads += count;
    if (isFat && g_failNextFatRead) {
        g_failNextFatRead = false;
        return BLOCK_ERR_IO;
    }
    std::memcpy(buffer, g_disk.data() + lba * kSectorSize, count * kSectorSize);
    if (isFat && g_changeGenerationDuringFatRead) {
        g_changeGenerationDuringFatRead = false;
        ++g_deviceGeneration;
    }
    return BLOCK_OK;
}

Status write_sectors(uint8_t device, uint64_t lba, uint32_t count, const void* buffer) {
    if (device != 0u || !g_fakeBlockDevice.active || !buffer || count == 0u ||
        lba + count > kDiskSectors)
        return BLOCK_ERR_INVALID;
    std::memcpy(g_disk.data() + lba * kSectorSize, buffer, count * kSectorSize);
    return BLOCK_OK;
}

Status flush(uint8_t device) {
    return device == 0u && g_fakeBlockDevice.active ? BLOCK_OK : BLOCK_ERR_INVALID;
}
uint64_t device_generation(uint8_t device) {
    return device == 0u && g_fakeBlockDevice.active ? g_deviceGeneration : 0u;
}
const BlockDevice* get_device(uint8_t device) {
    return device == 0u && g_fakeBlockDevice.active ? &g_fakeBlockDevice : nullptr;
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
    const kernel::vfs::Status staleClose = kernel::vfs::close(vfsHandle);
    const kernel::vfs::Status staleUnmount = kernel::vfs::unmount("/");
    if (!check(staleHandleSafe && staleClose == kernel::vfs::VFS_ERR_NOT_MOUNT &&
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

    std::cout << "fat32SequentialReadChecks=11/11 sequentialFatSectorReads="
              << sequentialFatSectorReads << "\n";
    std::cout << "fat32GenerationSafetyChecks=" << (g_checks - 11u) << "/"
              << (g_checks - 11u) << " totalChecks=" << g_checks
              << " failures=" << g_failures << '\n';
    return 0;
}
