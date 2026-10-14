#include "kernel/block_device.h"
#include "kernel/fs_fat.h"
#include "kernel/vfs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
using kernel::block::Status;
static const uint32_t kSectorSize = 512;
static const uint32_t kSectorCount = 8192;
static const uint32_t kFat0Start = 1;
static const uint32_t kFatSize = 64;
static const uint32_t kFat1Start = kFat0Start + kFatSize;
static const uint32_t kFirstDataSector = kFat1Start + kFatSize;
static std::array<uint8_t, kSectorSize * kSectorCount> disk;
static uint64_t readCalls = 0;
static uint64_t writeCalls = 0;
static uint64_t failReadCall = 0;
static uint64_t failWriteCall = 0;
static int64_t failReadLba = -1;
static int64_t failWriteLba = -1;
static Status injectedStatus = kernel::block::BLOCK_ERR_IO;
static Status flushStatus = kernel::block::BLOCK_OK;
static int checks = 0;
static int failures = 0;

void check(bool condition, const char* name)
{
    ++checks;
    if (condition) std::cout << "PASS: " << name << "\n";
    else {
        ++failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

Status read_sectors(uint8_t, uint64_t lba, uint32_t count, void* buffer)
{
    ++readCalls;
    if ((failReadCall != 0 && readCalls == failReadCall) ||
        (failReadLba >= 0 && lba == static_cast<uint64_t>(failReadLba))) {
        failReadCall = 0;
        failReadLba = -1;
        return injectedStatus;
    }
    if (!buffer || lba > kSectorCount || count > kSectorCount - lba)
        return kernel::block::BLOCK_ERR_INVALID;
    std::memcpy(buffer, disk.data() + lba * kSectorSize, count * kSectorSize);
    return kernel::block::BLOCK_OK;
}

Status write_sectors(uint8_t, uint64_t lba, uint32_t count, const void* buffer)
{
    ++writeCalls;
    if ((failWriteCall != 0 && writeCalls == failWriteCall) ||
        (failWriteLba >= 0 && lba == static_cast<uint64_t>(failWriteLba))) {
        failWriteCall = 0;
        failWriteLba = -1;
        return injectedStatus;
    }
    if (!buffer || lba > kSectorCount || count > kSectorCount - lba)
        return kernel::block::BLOCK_ERR_INVALID;
    std::memcpy(disk.data() + lba * kSectorSize, buffer, count * kSectorSize);
    return kernel::block::BLOCK_OK;
}

Status flush_device(uint8_t)
{
    return flushStatus;
}

uint32_t cluster_lba(uint32_t cluster)
{
    return kFirstDataSector + cluster - 2u;
}

uint32_t fat_lba(uint32_t cluster, uint32_t copy)
{
    return (copy == 0 ? kFat0Start : kFat1Start) +
        (cluster * 4u) / kSectorSize;
}

uint32_t get_fat(uint32_t cluster, uint32_t copy = 0)
{
    const uint32_t lba = fat_lba(cluster, copy);
    const uint32_t offset = (cluster * 4u) % kSectorSize;
    uint32_t value = 0;
    std::memcpy(&value, disk.data() + lba * kSectorSize + offset, sizeof(value));
    return value & 0x0FFFFFFFu;
}

void set_fat(uint32_t cluster, uint32_t value)
{
    for (uint32_t copy = 0; copy < 2; ++copy) {
        const uint32_t lba = fat_lba(cluster, copy);
        const uint32_t offset = (cluster * 4u) % kSectorSize;
        uint32_t oldValue = 0;
        std::memcpy(&oldValue, disk.data() + lba * kSectorSize + offset,
                    sizeof(oldValue));
        oldValue = (oldValue & 0xF0000000u) | (value & 0x0FFFFFFFu);
        std::memcpy(disk.data() + lba * kSectorSize + offset, &oldValue,
                    sizeof(oldValue));
    }
}

void create_test_fat32_volume()
{
    disk.fill(0);
    kernel::fs_fat::FAT32_BPB bpb{};
    bpb.bytesPerSector = kSectorSize;
    bpb.sectorsPerCluster = 1;
    bpb.reservedSectors = 1;
    bpb.numFATs = 2;
    bpb.totalSectors32 = kSectorCount;
    bpb.fatSize32 = kFatSize;
    bpb.rootCluster = 2;
    std::memcpy(bpb.volumeLabel, "NDB1R2TEST ", 11);
    std::memcpy(bpb.fsType, "FAT32   ", 8);
    std::memcpy(disk.data(), &bpb, sizeof(bpb));
    disk[510] = 0x55;
    disk[511] = 0xAA;

    for (uint32_t copy = 0; copy < 2; ++copy) {
        const uint32_t start = copy == 0 ? kFat0Start : kFat1Start;
        uint32_t* fat = reinterpret_cast<uint32_t*>(disk.data() + start * kSectorSize);
        fat[0] = 0x0FFFFFF8u;
        fat[1] = 0x0FFFFFFFu;
        fat[2] = 3;
        fat[3] = 4;
        fat[4] = 5;
        fat[5] = 0x0FFFFFFFu;
    }
}

std::vector<uint8_t> pattern(size_t size, uint8_t seed)
{
    std::vector<uint8_t> bytes(size);
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<uint8_t>((i * 37u + seed * 19u + (i >> 3)) & 0xFFu);
    }
    return bytes;
}

std::string file_name(uint32_t id)
{
    char name[20];
    std::snprintf(name, sizeof(name), "R%07u.BIN", id);
    return name;
}

bool create_file(const std::string& path, const std::vector<uint8_t>& bytes)
{
    const uint8_t handle = kernel::vfs::open(
        path.c_str(), kernel::vfs::OPEN_CREATE | kernel::vfs::OPEN_READ |
                     kernel::vfs::OPEN_WRITE);
    if (handle == 0xFF) return false;
    const bool wrote = bytes.empty() ||
        kernel::vfs::write(handle, bytes.data(), static_cast<uint32_t>(bytes.size())) ==
            static_cast<int32_t>(bytes.size());
    const bool flushed = kernel::vfs::flush(handle) == kernel::vfs::VFS_OK;
    const bool closed = kernel::vfs::close(handle) == kernel::vfs::VFS_OK;
    return wrote && flushed && closed;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out)
{
    out.clear();
    const uint8_t handle = kernel::vfs::open(path.c_str(), kernel::vfs::OPEN_READ);
    if (handle == 0xFF) return false;
    const int64_t size = kernel::vfs::file_size(handle);
    if (size < 0 || size > static_cast<int64_t>(kSectorSize * 100u)) {
        kernel::vfs::close(handle);
        return false;
    }
    out.resize(static_cast<size_t>(size));
    const bool sought = kernel::vfs::seek(handle, 0,
        static_cast<kernel::vfs::SeekOrigin>(0)) ==
        kernel::vfs::VFS_OK;
    const int32_t amount = out.empty() ? 0 : kernel::vfs::read(
        handle, out.data(), static_cast<uint32_t>(out.size()));
    uint8_t extra = 0;
    const int32_t eof = kernel::vfs::read(handle, &extra, 1);
    const bool closed = kernel::vfs::close(handle) == kernel::vfs::VFS_OK;
    return sought && amount == static_cast<int32_t>(out.size()) && eof == 0 && closed;
}

bool validate_chain(const std::string& path, uint32_t expectedSize)
{
    const kernel::vfs::MountPoint* mount = kernel::vfs::get_mount("/resize");
    if (!mount) return false;
    kernel::fs_fat::DirEntry entry{};
    if (!kernel::fs_fat::lookup_path(mount->fsVolumeIndex, path.c_str() + 7, &entry)) {
        return false;
    }
    if (entry.fileSize != expectedSize) return false;
    if (expectedSize == 0) return entry.firstCluster == 0;
    if (entry.firstCluster < 2) return false;
    const uint32_t count = (expectedSize + kSectorSize - 1u) / kSectorSize;
    uint32_t cluster = entry.firstCluster;
    std::vector<uint32_t> seen;
    for (uint32_t i = 0; i < count; ++i) {
        if (cluster < 2 || cluster >= 2u + (kSectorCount - kFirstDataSector)) return false;
        if (std::find(seen.begin(), seen.end(), cluster) != seen.end()) return false;
        seen.push_back(cluster);
        const uint32_t next = get_fat(cluster);
        if (get_fat(cluster, 1) != next) return false;
        const bool eoc = next >= 0x0FFFFFF8u;
        if (eoc != (i + 1u == count)) return false;
        if (!eoc) cluster = next;
    }
    return true;
}

bool raw_find_short_name(const char* shortName, uint32_t* outLba, uint32_t* outOffset)
{
    const kernel::vfs::MountPoint* mount = kernel::vfs::get_mount("/resize");
    const kernel::fs_fat::FATVolume* volume = mount
        ? kernel::fs_fat::get_volume(mount->fsVolumeIndex) : nullptr;
    if (!volume || !shortName || !outLba || !outOffset) return false;
    char wanted[11];
    std::memset(wanted, ' ', sizeof(wanted));
    const char* dot = std::strchr(shortName, '.');
    const size_t stemLen = dot ? static_cast<size_t>(dot - shortName) : std::strlen(shortName);
    const size_t extLen = dot ? std::strlen(dot + 1) : 0;
    if (stemLen > 8 || extLen > 3) return false;
    for (size_t i = 0; i < stemLen; ++i) wanted[i] = static_cast<char>(std::toupper(shortName[i]));
    for (size_t i = 0; i < extLen; ++i) wanted[8 + i] = static_cast<char>(std::toupper(dot[1 + i]));

    uint32_t cluster = volume->rootCluster;
    for (uint32_t step = 0; step < 64; ++step) {
        for (uint32_t sec = 0; sec < volume->sectorsPerCluster; ++sec) {
            const uint32_t lba = cluster_lba(cluster) + sec;
            const uint8_t* sector = disk.data() + lba * kSectorSize;
            for (uint32_t offset = 0; offset + 32 <= kSectorSize; offset += 32) {
                if (sector[offset] == 0) return false;
                if (sector[offset] == 0xE5 || sector[offset + 11] == 0x0F ||
                    (sector[offset + 11] & 0x08u)) continue;
                if (std::memcmp(sector + offset, wanted, sizeof(wanted)) == 0) {
                    *outLba = lba;
                    *outOffset = offset;
                    return true;
                }
            }
        }
        const uint32_t next = get_fat(cluster);
        if (next >= 0x0FFFFFF8u) return false;
        cluster = next;
    }
    return false;
}

void check_prefix_after_resize(const std::string& path,
                               const std::vector<uint8_t>& original,
                               uint32_t newSize, const char* label)
{
    const uint8_t handle = kernel::vfs::open(path.c_str(), kernel::vfs::OPEN_RDWR);
    check(handle != 0xFF, label);
    if (handle == 0xFF) return;
    check(kernel::vfs::truncate_shrink(handle, newSize) == kernel::vfs::VFS_OK,
          "resize returns success");
    check(kernel::vfs::file_size(handle) == static_cast<int64_t>(newSize),
          "size query matches resized length");
    check(kernel::vfs::seek(handle, 0,
          static_cast<kernel::vfs::SeekOrigin>(0)) == kernel::vfs::VFS_OK,
          "seek to beginning after resize");
    std::vector<uint8_t> actual(newSize);
    const int32_t got = newSize == 0 ? 0 : kernel::vfs::read(
        handle, actual.data(), newSize);
    check(got == static_cast<int32_t>(newSize) &&
          std::equal(actual.begin(), actual.end(), original.begin()),
          "resize preserves the exact prefix");
    uint8_t extra = 0;
    check(kernel::vfs::read(handle, &extra, 1) == 0,
          "read beyond resized EOF returns zero");
    check(kernel::vfs::flush(handle) == kernel::vfs::VFS_OK,
          "successful resize flush propagates");
    check(kernel::vfs::close(handle) == kernel::vfs::VFS_OK,
          "resized handle closes cleanly");

    std::vector<uint8_t> reopened;
    check(read_file(path, reopened) && reopened == actual,
          "close/reopen retains resized size and bytes");
    check(validate_chain(path, newSize),
          "directory size and FAT chain agree after resize");
}

void run_boundary_tests()
{
    const uint32_t sizes[] = { 0, 1, 511, 512, 513, 1023, 1024, 1025 };
    const std::vector<uint8_t> source = pattern(4 * kSectorSize + 77, 11);
    uint32_t id = 1;
    for (uint32_t size : sizes) {
        const std::string path = "/resize/" + file_name(id++);
        check(create_file(path, source), "create deterministic multicluster file");
        check_prefix_after_resize(path, source, size, "open boundary test file");
    }

    const std::string samePath = "/resize/" + file_name(id++);
    check(create_file(samePath, source), "create same-size resize fixture");
    const uint8_t sameHandle = kernel::vfs::open(samePath.c_str(), kernel::vfs::OPEN_RDWR);
    check(sameHandle != 0xFF &&
        kernel::vfs::truncate_shrink(sameHandle, source.size()) == kernel::vfs::VFS_OK,
        "same-size resize succeeds without changing the file");
    check(kernel::vfs::close(sameHandle) == kernel::vfs::VFS_OK,
          "same-size resize handle closes");

    const std::string zeroPath = "/resize/" + file_name(id++);
    check(create_file(zeroPath, {}), "create zero-then-write fixture without allocating a cluster");
    check(validate_chain(zeroPath, 0), "new empty file has zero first-cluster pointer");
    uint8_t zeroHandle = kernel::vfs::open(zeroPath.c_str(), kernel::vfs::OPEN_RDWR);
    check(zeroHandle != 0xFF && kernel::vfs::truncate_shrink(zeroHandle, 0) ==
          kernel::vfs::VFS_OK, "truncate-to-zero releases file chain");
    check(kernel::vfs::close(zeroHandle) == kernel::vfs::VFS_OK,
          "zero-length file closes");
    check(validate_chain(zeroPath, 0), "zero-length entry has no first cluster");
    zeroHandle = kernel::vfs::open(zeroPath.c_str(), kernel::vfs::OPEN_RDWR);
    const std::vector<uint8_t> replacement = pattern(733, 29);
    check(zeroHandle != 0xFF && kernel::vfs::write(zeroHandle, replacement.data(),
          static_cast<uint32_t>(replacement.size())) ==
          static_cast<int32_t>(replacement.size()), "write after zero truncate reallocates a cluster");
    check(zeroHandle != 0xFF && kernel::vfs::close(zeroHandle) == kernel::vfs::VFS_OK,
          "rewritten zero-length file closes");
    std::vector<uint8_t> rewritten;
    check(read_file(zeroPath, rewritten) && rewritten == replacement,
          "write after zero truncate persists exact bytes");

    const std::string legacyEmptyPath = "/resize/LEGACY0.BIN";
    check(create_file(legacyEmptyPath, pattern(kSectorSize, 17)),
          "create legacy empty-file cleanup fixture");
    const kernel::vfs::MountPoint* mount = kernel::vfs::get_mount("/resize");
    kernel::fs_fat::DirEntry legacyEntry{};
    const bool foundLegacy = mount && kernel::fs_fat::lookup_path(
        mount->fsVolumeIndex, "/LEGACY0.BIN", &legacyEntry);
    check(foundLegacy && legacyEntry.firstCluster >= 2 &&
          kernel::fs_fat::update_file_metadata_path_status(
              mount->fsVolumeIndex, "/LEGACY0.BIN", legacyEntry.firstCluster,
              0, nullptr) == kernel::fs_fat::FILE_WRITE_OK,
          "simulate a prior zero-size entry that retained a valid cluster");
    const uint8_t legacyEmpty = kernel::vfs::open(
        legacyEmptyPath.c_str(), kernel::vfs::OPEN_RDWR);
    check(legacyEmpty != 0xFF && kernel::vfs::truncate_shrink(legacyEmpty, 0) ==
          kernel::vfs::VFS_OK, "same-size zero truncate cleans up a legacy cluster");
    if (legacyEmpty != 0xFF) kernel::vfs::close(legacyEmpty);
    check(validate_chain(legacyEmptyPath, 0),
          "legacy zero-size entry no longer retains an allocated cluster");
}

void run_fragmented_chain_test()
{
    const std::string path = "/resize/FRAG.BIN";
    const std::vector<uint8_t> source = pattern(3 * kSectorSize, 37);
    const std::string peerPath = "/resize/FRAGPEER.BIN";
    const std::vector<uint8_t> peerBytes = pattern(2 * kSectorSize + 11, 52);
    check(create_file(peerPath, peerBytes), "create unrelated fragmented-test peer");
    check(create_file(path, source), "create fragmented-chain source file");
    const kernel::vfs::MountPoint* mount = kernel::vfs::get_mount("/resize");
    kernel::fs_fat::DirEntry entry{};
    check(mount && kernel::fs_fat::lookup_path(mount->fsVolumeIndex,
        "/FRAG.BIN", &entry), "find fragmented-chain source entry");
    if (!mount || entry.firstCluster < 2) return;
    const uint32_t first = entry.firstCluster;
    const uint32_t second = 100;
    const uint32_t third = 200;
    const uint32_t oldSecond = get_fat(first);
    const uint32_t oldThird = get_fat(oldSecond);
    std::memcpy(disk.data() + cluster_lba(second) * kSectorSize,
                disk.data() + cluster_lba(oldSecond) * kSectorSize, kSectorSize);
    std::memcpy(disk.data() + cluster_lba(third) * kSectorSize,
                disk.data() + cluster_lba(oldThird) * kSectorSize, kSectorSize);
    set_fat(first, second);
    set_fat(second, third);
    set_fat(third, 0x0FFFFFFFu);
    set_fat(oldSecond, 0);
    set_fat(oldThird, 0);

    const uint8_t handle = kernel::vfs::open(path.c_str(), kernel::vfs::OPEN_RDWR);
    check(handle != 0xFF && kernel::vfs::truncate_shrink(handle, 513) ==
          kernel::vfs::VFS_OK, "truncate a genuinely fragmented FAT chain");
    check(kernel::vfs::close(handle) == kernel::vfs::VFS_OK,
          "fragmented file closes");
    check(get_fat(first) == second && get_fat(second) >= 0x0FFFFFF8u &&
          get_fat(third) == 0 && get_fat(second, 1) >= 0x0FFFFFF8u &&
          get_fat(third, 1) == 0,
          "fragmented retained chain ends correctly and detached cluster is free in both FATs");
    std::vector<uint8_t> actual;
    check(read_file(path, actual) && actual.size() == 513 &&
          std::equal(actual.begin(), actual.end(), source.begin()),
          "fragmented shrink retains exact source prefix");
    check(validate_chain(path, 513), "fragmented chain validates after reopen");
    std::vector<uint8_t> peerReadback;
    check(read_file(peerPath, peerReadback) && peerReadback == peerBytes,
          "fragmented truncation leaves unrelated file contents intact");
    check(validate_chain(peerPath, static_cast<uint32_t>(peerBytes.size())),
          "fragmented truncation leaves unrelated file chain intact");
}

void run_permission_and_handle_tests()
{
    const std::string path = "/resize/HANDLE.BIN";
    const std::vector<uint8_t> source = pattern(1200, 44);
    check(create_file(path, source), "create handle-policy fixture");
    const uint8_t readOnly = kernel::vfs::open(path.c_str(), kernel::vfs::OPEN_READ);
    check(readOnly != 0xFF && kernel::vfs::truncate_shrink(readOnly, 20) ==
          kernel::vfs::VFS_ERR_READ_ONLY, "read-only handle cannot truncate");
    check(kernel::vfs::close(readOnly) == kernel::vfs::VFS_OK,
          "read-only fixture handle closes");
    check(kernel::vfs::truncate_shrink(0xFF, 1) == kernel::vfs::VFS_ERR_INVALID &&
          kernel::vfs::file_size(0xFF) == -1,
          "invalid handle is rejected and size failure is distinct from zero");

    const uint8_t first = kernel::vfs::open(path.c_str(), kernel::vfs::OPEN_RDWR);
    const uint8_t second = kernel::vfs::open(path.c_str(), kernel::vfs::OPEN_READ);
    check(first != 0xFF && second != 0xFF && kernel::vfs::truncate_shrink(first, 20) ==
          kernel::vfs::VFS_ERR_BUSY, "second open handle blocks unsafe same-file mutation");
    check(kernel::vfs::close(second) == kernel::vfs::VFS_OK,
          "second handle closes before retry");
    check(kernel::vfs::truncate_shrink(first, 20) == kernel::vfs::VFS_OK,
          "truncate succeeds after the stale peer handle closes");
    check(kernel::vfs::truncate_shrink(first, 21) == kernel::vfs::VFS_ERR_NOT_SUPPORTED,
          "growth is explicitly rejected by shrink-only API");
    check(kernel::vfs::truncate_shrink(first, 0x100000000ull) ==
          kernel::vfs::VFS_ERR_RESOURCE_LIMIT, "size above FAT32 limit is rejected");
    check(kernel::vfs::close(first) == kernel::vfs::VFS_OK,
          "handle-policy fixture closes");

    const std::string cursorPath = "/resize/CURSOR.BIN";
    check(create_file(cursorPath, source), "create cursor-preservation fixture");
    const uint8_t cursor = kernel::vfs::open(cursorPath.c_str(), kernel::vfs::OPEN_RDWR);
    check(cursor != 0xFF && kernel::vfs::seek(cursor, 900,
          static_cast<kernel::vfs::SeekOrigin>(0)) == kernel::vfs::VFS_OK,
          "seek cursor before truncation");
    check(cursor != 0xFF && kernel::vfs::truncate_shrink(cursor, 300) ==
          kernel::vfs::VFS_OK && kernel::vfs::tell(cursor) == 900,
          "truncate preserves a cursor beyond the new EOF");
    uint8_t atEof = 0;
    check(cursor != 0xFF && kernel::vfs::read(cursor, &atEof, 1) == 0,
          "preserved cursor reads EOF after shrink");
    check(cursor != 0xFF && kernel::vfs::truncate_shrink(cursor, 100) ==
          kernel::vfs::VFS_OK && kernel::vfs::truncate_shrink(cursor, 0) ==
          kernel::vfs::VFS_OK, "repeated truncations release the remaining chain");
    if (cursor != 0xFF) kernel::vfs::close(cursor);
    check(validate_chain(cursorPath, 0), "repeated truncation leaves an empty file");

    check(kernel::vfs::mkdir("/resize/TDIR") == kernel::vfs::VFS_OK,
          "create directory-handle fixture");
    const uint8_t directory = kernel::vfs::open("/resize/TDIR", kernel::vfs::OPEN_READ);
    check(directory != 0xFF && kernel::vfs::truncate_shrink(directory, 0) ==
          kernel::vfs::VFS_ERR_IS_DIR, "directory handle cannot truncate as a regular file");
    if (directory != 0xFF) kernel::vfs::close(directory);
}

void run_corruption_tests()
{
    const std::string path = "/resize/CORRUPT.BIN";
    const std::vector<uint8_t> source = pattern(2 * kSectorSize, 61);
    check(create_file(path, source), "create corrupted-chain fixture");
    const kernel::vfs::MountPoint* mount = kernel::vfs::get_mount("/resize");
    kernel::fs_fat::DirEntry entry{};
    if (!mount || !kernel::fs_fat::lookup_path(mount->fsVolumeIndex,
            "/CORRUPT.BIN", &entry)) {
        check(false, "find corrupted-chain fixture");
        return;
    }
    const uint32_t first = entry.firstCluster;
    const uint32_t second = get_fat(first);
    const uint32_t originalSecond = get_fat(second);

    const uint32_t badLinks[] = { 0x0FFFFFF0u, 0x0FFFFFF7u, 0u, 90000u, first };
    const char* names[] = { "reserved FAT marker", "bad cluster marker",
                            "premature end of chain", "out-of-range cluster number",
                            "cycle in target chain" };
    for (size_t i = 0; i < sizeof(badLinks) / sizeof(badLinks[0]); ++i) {
        set_fat(first, badLinks[i]);
        if (i == 3) set_fat(second, first);
        const uint8_t handle = kernel::vfs::open(path.c_str(), kernel::vfs::OPEN_RDWR);
        check(handle != 0xFF && kernel::vfs::truncate_shrink(handle, 1) ==
              kernel::vfs::VFS_ERR_CORRUPT_CHAIN, names[i]);
        if (handle != 0xFF) kernel::vfs::close(handle);
        set_fat(first, second);
        set_fat(second, originalSecond);
    }

    const std::string peerPath = "/resize/PEER.BIN";
    check(create_file(peerPath, pattern(32, 71)), "create cross-link peer entry");
    uint32_t peerLba = 0;
    uint32_t peerOffset = 0;
    check(raw_find_short_name("PEER.BIN", &peerLba, &peerOffset),
          "locate peer directory entry for cross-link injection");
    if (peerLba != 0) {
        uint8_t* de = disk.data() + peerLba * kSectorSize + peerOffset;
        const uint16_t oldHi = *reinterpret_cast<uint16_t*>(de + 20);
        const uint16_t oldLo = *reinterpret_cast<uint16_t*>(de + 26);
        *reinterpret_cast<uint16_t*>(de + 20) = static_cast<uint16_t>(second >> 16);
        *reinterpret_cast<uint16_t*>(de + 26) = static_cast<uint16_t>(second);
        const uint8_t handle = kernel::vfs::open(path.c_str(), kernel::vfs::OPEN_RDWR);
        check(handle != 0xFF && kernel::vfs::truncate_shrink(handle, 1) ==
              kernel::vfs::VFS_ERR_CORRUPT_CHAIN,
              "detected peer-file cross-link blocks cluster release");
        if (handle != 0xFF) kernel::vfs::close(handle);
        *reinterpret_cast<uint16_t*>(de + 20) = oldHi;
        *reinterpret_cast<uint16_t*>(de + 26) = oldLo;
    }

    check(kernel::vfs::mkdir("/resize/NESTED") == kernel::vfs::VFS_OK,
          "create nested directory for cross-link coverage");
    check(create_file("/resize/NESTED/DEEPPEER.BIN", pattern(32, 73)),
          "create peer file in a different directory");
    kernel::fs_fat::DirEntry nestedPeer{};
    check(mount && kernel::fs_fat::lookup_path(mount->fsVolumeIndex,
        "/NESTED/DEEPPEER.BIN", &nestedPeer),
        "find nested cross-link peer entry");
    if (nestedPeer.firstCluster >= 2) {
        const uint32_t originalPeerNext = get_fat(nestedPeer.firstCluster);
        set_fat(nestedPeer.firstCluster, second);
        const uint8_t handle = kernel::vfs::open(path.c_str(), kernel::vfs::OPEN_RDWR);
        check(handle != 0xFF && kernel::vfs::truncate_shrink(handle, 1) ==
              kernel::vfs::VFS_ERR_CORRUPT_CHAIN,
              "volume-wide ownership scan detects a cross-link in a nested directory");
        if (handle != 0xFF) kernel::vfs::close(handle);
        set_fat(nestedPeer.firstCluster, originalPeerNext);
    }
}

void run_io_failure_tests()
{
    const std::vector<uint8_t> source = pattern(3 * kSectorSize, 83);

    const std::string readFailPath = "/resize/READERR.BIN";
    check(create_file(readFailPath, source), "create read-failure fixture");
    uint8_t handle = kernel::vfs::open(readFailPath.c_str(), kernel::vfs::OPEN_RDWR);
    failReadLba = kFat0Start;
    injectedStatus = kernel::block::BLOCK_ERR_TIMEOUT;
    check(handle != 0xFF && kernel::vfs::truncate_shrink(handle, 1) ==
          kernel::vfs::VFS_ERR_IO_TIMEOUT,
          "FAT traversal timeout propagates through VFS truncate");
    if (handle != 0xFF) kernel::vfs::close(handle);

    const std::string dirFailPath = "/resize/DIRERR.BIN";
    check(create_file(dirFailPath, source), "create directory-metadata failure fixture");
    handle = kernel::vfs::open(dirFailPath.c_str(), kernel::vfs::OPEN_RDWR);
    failWriteCall = writeCalls + 1;
    injectedStatus = kernel::block::BLOCK_ERR_IO;
    check(handle != 0xFF && kernel::vfs::truncate_shrink(handle, 1) ==
          kernel::vfs::VFS_ERR_IO,
          "directory metadata write error is reported");
    check(kernel::vfs::get_handle(handle) == nullptr,
          "ambiguous directory metadata failure invalidates VFS handle");

    const std::string fatFailPath = "/resize/FATERR.BIN";
    check(create_file(fatFailPath, source), "create FAT-update failure fixture");
    handle = kernel::vfs::open(fatFailPath.c_str(), kernel::vfs::OPEN_RDWR);
    failWriteCall = writeCalls + 2;
    injectedStatus = kernel::block::BLOCK_ERR_TIMEOUT;
    check(handle != 0xFF && kernel::vfs::truncate_shrink(handle, 1) ==
          kernel::vfs::VFS_ERR_IO_TIMEOUT,
          "surviving-tail FAT update timeout propagates");
    check(kernel::vfs::get_handle(handle) == nullptr,
          "partial FAT update invalidates VFS handle");

    const std::string releaseFailPath = "/resize/FREEERR.BIN";
    check(create_file(releaseFailPath, source), "create cluster-release failure fixture");
    handle = kernel::vfs::open(releaseFailPath.c_str(), kernel::vfs::OPEN_RDWR);
    failWriteCall = writeCalls + 4;
    injectedStatus = kernel::block::BLOCK_ERR_IO;
    check(handle != 0xFF && kernel::vfs::truncate_shrink(handle, 1) ==
          kernel::vfs::VFS_ERR_IO,
          "detached-cluster release write error propagates");
    check(kernel::vfs::get_handle(handle) == nullptr,
          "partial detached-chain release invalidates VFS handle");

    const std::string allocFailPath = "/resize/ALLOCERR.BIN";
    check(create_file(allocFailPath, source), "create allocation-failure fixture");
    handle = kernel::vfs::open(allocFailPath.c_str(), kernel::vfs::OPEN_RDWR);
    check(handle != 0xFF && kernel::vfs::truncate_shrink(handle, 0) ==
          kernel::vfs::VFS_OK, "prepare empty file for allocation failure");
    check(handle != 0xFF && kernel::vfs::close(handle) == kernel::vfs::VFS_OK,
          "close empty allocation fixture");
    handle = kernel::vfs::open(allocFailPath.c_str(), kernel::vfs::OPEN_RDWR);
    failWriteCall = writeCalls + 1;
    injectedStatus = kernel::block::BLOCK_ERR_IO;
    const uint8_t payload = 0xA5;
    check(handle != 0xFF && kernel::vfs::write(handle, &payload, 1) ==
          kernel::vfs::VFS_ERR_IO,
          "first-cluster allocation write failure is not reported as success");
    if (handle != 0xFF) kernel::vfs::close(handle);
    check(validate_chain(allocFailPath, 0),
          "failed first allocation leaves empty directory metadata intact");

    const std::string flushFailPath = "/resize/FLUSHER.BIN";
    check(create_file(flushFailPath, source), "create flush-failure fixture");
    handle = kernel::vfs::open(flushFailPath.c_str(), kernel::vfs::OPEN_RDWR);
    check(handle != 0xFF && kernel::vfs::truncate_shrink(handle, 1) ==
          kernel::vfs::VFS_OK, "resize succeeds before injected flush failure");
    flushStatus = kernel::block::BLOCK_ERR_TIMEOUT;
    check(handle != 0xFF && kernel::vfs::flush(handle) ==
          kernel::vfs::VFS_ERR_IO_TIMEOUT,
          "flush timeout remains visible through VFS");
    flushStatus = kernel::block::BLOCK_OK;
    if (handle != 0xFF) kernel::vfs::close(handle);
}

void run_wal_contract_test()
{
    // The current FAT path creator accepts 8.3 names, so this hosted fixture
    // models the SQL WAL byte contract under a FAT-compatible filename.
    const std::string path = "/resize/WAL.GWL";
    std::vector<uint8_t> bytes = pattern(64 + 5 * kSectorSize + 17, 101);
    const std::vector<uint8_t> header(bytes.begin(), bytes.begin() + 64);
    check(create_file(path, bytes), "create WAL-like file with header and payload");
    uint8_t handle = kernel::vfs::open(path.c_str(), kernel::vfs::OPEN_RDWR);
    check(handle != 0xFF && kernel::vfs::flush(handle) == kernel::vfs::VFS_OK,
          "flush complete WAL-like contents");
    check(handle != 0xFF && kernel::vfs::truncate_shrink(handle, 64) ==
          kernel::vfs::VFS_OK, "shrink WAL-like file to fixed header size");
    check(handle != 0xFF && kernel::vfs::flush(handle) == kernel::vfs::VFS_OK,
          "flush WAL-like truncation");
    if (handle != 0xFF) kernel::vfs::close(handle);
    std::vector<uint8_t> actual;
    check(read_file(path, actual) && actual == header,
          "reopened WAL-like file exposes exact preserved header only");
    check(validate_chain(path, 64), "WAL-like header file has exact FAT chain");
}
}

int main()
{
    using namespace kernel;
    block::init();
    fs_fat::init();
    vfs::init();
    create_test_fat32_volume();

    block::BlockDevice device{};
    device.active = true;
    device.type = block::BDEV_ATA_PIO;
    device.totalSectors = kSectorCount;
    device.sectorSize = kSectorSize;
    std::memcpy(device.name, "NDB1R2-resize", 14);
    device.readFn = read_sectors;
    device.writeFn = write_sectors;
    device.flushFn = flush_device;
    const uint8_t deviceIndex = block::register_device(device);
    check(deviceIndex != 0xFF, "register injectable FAT32 block device");
    const uint8_t mountIndex = vfs::mount_type("/resize", deviceIndex, vfs::FS_TYPE_FAT32);
    check(mountIndex != 0xFF, "mount dedicated hosted FAT32 resize volume");
    if (mountIndex != 0xFF) {
        run_boundary_tests();
        run_fragmented_chain_test();
        run_permission_and_handle_tests();
        run_corruption_tests();
        run_io_failure_tests();
        run_wal_contract_test();
    }

    std::cout << "vfsFileResizeChecks=" << (checks - failures) << "/" << checks << "\n";
    return failures == 0 ? 0 : 1;
}
