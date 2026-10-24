#include "kernel/block_device.h"
#include "kernel/fs_fat.h"
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
}

bool check(bool passed, const char* label) {
    std::cout << (passed ? "PASS: " : "FAIL: ") << label << '\n';
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
    if (device != 0u || !buffer || count == 0u || lba + count > kDiskSectors)
        return BLOCK_ERR_INVALID;
    if (lba < kFatStart + kFatSectors && lba + count > kFatStart)
        g_fatSectorReads += count;
    std::memcpy(buffer, g_disk.data() + lba * kSectorSize, count * kSectorSize);
    return BLOCK_OK;
}

Status write_sectors(uint8_t device, uint64_t lba, uint32_t count, const void* buffer) {
    if (device != 0u || !buffer || count == 0u || lba + count > kDiskSectors)
        return BLOCK_ERR_INVALID;
    std::memcpy(g_disk.data() + lba * kSectorSize, buffer, count * kSectorSize);
    return BLOCK_OK;
}

Status flush(uint8_t device) { return device == 0u ? BLOCK_OK : BLOCK_ERR_INVALID; }
}
}

int main() {
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
    std::cout << "fat32SequentialReadChecks=11/11 sequentialFatSectorReads="
              << sequentialFatSectorReads << " totalFatSectorReads=" << g_fatSectorReads << '\n';
    return 0;
}
