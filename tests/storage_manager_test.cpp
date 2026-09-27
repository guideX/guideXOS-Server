// DM2 deterministic storage tests. Every sector comes from FakeDisk memory;
// no host disk device is opened or written.
#include "../kernel/core/include/kernel/block_device.h"
#include "../kernel/core/include/kernel/partition_table.h"
#include "../kernel/core/include/kernel/storage_manager.h"
#include "../kernel/core/include/kernel/disk_initialization.h"
#include "../kernel/core/include/kernel/ramdisk.h"
#include "../kernel/core/include/kernel/vfs.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace kernel;

namespace {

struct FakeDisk {
    uint32_t sectorSize;
    uint64_t sectorCount;
    uint8_t driverId;
    std::vector<uint8_t> bytes;
    bool failReads;
    uint64_t failLba;
    uint32_t failReadAtCall;
    bool failFlush;
    uint32_t failFlushAtCall;
    uint32_t failWriteAtCall1;
    uint32_t failWriteAtCall2;
    uint64_t corruptWriteLbaOnce;
    bool corruptWritePending;
    uint32_t reads;
    uint32_t writes;
    uint32_t writeAttempts;
    uint32_t flushes;

    FakeDisk(uint32_t size, uint64_t count, bool allocate = true)
        : sectorSize(size), sectorCount(count), driverId(0),
          bytes(allocate ? static_cast<size_t>(size) * static_cast<size_t>(count) : 0, 0),
          failReads(false), failLba(UINT64_MAX), failReadAtCall(0), failFlush(false),
          failFlushAtCall(0), failWriteAtCall1(0), failWriteAtCall2(0),
          corruptWriteLbaOnce(UINT64_MAX), corruptWritePending(false),
          reads(0), writes(0), writeAttempts(0), flushes(0) {}
};

FakeDisk* g_fakeDisks[256] = {};
uint16_t g_nextDriverId = 1;
int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* label)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", label);
    }
}

block::Status fake_read(uint8_t driverId, uint64_t lba, uint32_t count, void* buffer)
{
    FakeDisk* disk = g_fakeDisks[driverId];
    if (!disk || !buffer || count == 0 || disk->failReads || lba == disk->failLba ||
        (disk->failReadAtCall != 0 && disk->reads + 1 == disk->failReadAtCall) ||
        lba > disk->sectorCount || count > disk->sectorCount - lba)
        return block::BLOCK_ERR_IO;
    ++disk->reads;
    const size_t offset = static_cast<size_t>(lba * disk->sectorSize);
    const size_t bytes = static_cast<size_t>(count) * disk->sectorSize;
    if (offset > disk->bytes.size() || bytes > disk->bytes.size() - offset)
        return block::BLOCK_ERR_IO;
    std::memcpy(buffer, disk->bytes.data() + offset, bytes);
    return block::BLOCK_OK;
}

block::Status fake_write(uint8_t driverId, uint64_t lba, uint32_t count,
                         const void* buffer)
{
    FakeDisk* disk = g_fakeDisks[driverId];
    if (!disk || !buffer || count == 0 || lba > disk->sectorCount ||
        count > disk->sectorCount - lba) return block::BLOCK_ERR_IO;
    ++disk->writeAttempts;
    if (disk->writeAttempts == disk->failWriteAtCall1 ||
        disk->writeAttempts == disk->failWriteAtCall2) return block::BLOCK_ERR_IO;
    const size_t offset = static_cast<size_t>(lba * disk->sectorSize);
    const size_t bytes = static_cast<size_t>(count) * disk->sectorSize;
    if (offset > disk->bytes.size() || bytes > disk->bytes.size() - offset)
        return block::BLOCK_ERR_IO;
    std::memcpy(disk->bytes.data() + offset, buffer, bytes);
    ++disk->writes;
    if (disk->corruptWritePending && lba == disk->corruptWriteLbaOnce) {
        disk->bytes[offset + 16] ^= 0x01;
        disk->corruptWritePending = false;
    }
    return block::BLOCK_OK;
}

block::Status fake_flush(uint8_t driverId)
{
    FakeDisk* disk = g_fakeDisks[driverId];
    if (!disk) return block::BLOCK_ERR_INVALID;
    ++disk->flushes;
    return disk->failFlush ||
        (disk->failFlushAtCall != 0 && disk->flushes == disk->failFlushAtCall)
        ? block::BLOCK_ERR_IO : block::BLOCK_OK;
}

uint8_t register_fake(FakeDisk& disk, bool writable = false,
                      bool withFlush = false, bool flushKnown = false,
                      bool durableCompletion = false,
                      uint16_t requiredAlignment = 0,
                      uint32_t maxTransferBytes = 0,
                      block::BootProvenance boot = block::BOOT_PROVENANCE_DEFINITELY_NOT_BOOT)
{
    disk.driverId = static_cast<uint8_t>(g_nextDriverId++);
    g_fakeDisks[disk.driverId] = &disk;
    block::BlockDevice descriptor = {};
    descriptor.active = true;
    descriptor.type = block::BDEV_ATA_PIO;
    descriptor.bootProvenance = boot;
    descriptor.driverIndex = disk.driverId;
    descriptor.totalSectors = disk.sectorCount;
    descriptor.sectorSize = disk.sectorSize;
    std::strncpy(descriptor.name, "fake-storage", sizeof(descriptor.name) - 1);
    std::strncpy(descriptor.model, "DM2 memory fixture", sizeof(descriptor.model) - 1);
    std::strncpy(descriptor.serial, "FAKE-ONLY", sizeof(descriptor.serial) - 1);
    descriptor.readFn = fake_read;
    descriptor.writeFn = writable ? fake_write : nullptr;
    descriptor.flushFn = withFlush ? fake_flush : nullptr;
    descriptor.flushSemanticsKnown = flushKnown;
    descriptor.writeCompletionDurable = durableCompletion;
    descriptor.requiredBufferAlignment = requiredAlignment;
    descriptor.maxTransferBytes = maxTransferBytes;
    return block::register_device(descriptor);
}

void unregister_fake(uint8_t index, FakeDisk& disk)
{
    block::unregister_device(index);
    g_fakeDisks[disk.driverId] = nullptr;
}

void write_u16(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

void write_u32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

void write_u64(uint8_t* p, uint64_t v)
{
    write_u32(p, static_cast<uint32_t>(v));
    write_u32(p + 4, static_cast<uint32_t>(v >> 32));
}

uint8_t* sector(FakeDisk& disk, uint64_t lba)
{
    return disk.bytes.data() + static_cast<size_t>(lba * disk.sectorSize);
}

void set_mbr_signature(FakeDisk& disk)
{
    uint8_t* mbr = sector(disk, 0);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
}

void set_mbr_partition(FakeDisk& disk, uint8_t slot, uint8_t status,
                       uint8_t type, uint32_t start, uint32_t count)
{
    uint8_t* entry = sector(disk, 0) + 446 + static_cast<uint32_t>(slot) * 16;
    entry[0] = status;
    entry[4] = type;
    write_u32(entry + 8, start);
    write_u32(entry + 12, count);
}

enum class GptFixture {
    Valid,
    PrimaryHeaderCrcBad,
    BackupHeaderCrcBad,
    PrimaryArrayCrcBad,
    BothArrayCrcBad,
    OutOfUsableRange,
    OverlappingPartitions,
    TooManyEntries,
};

void write_gpt_header(FakeDisk& disk, uint64_t lba, uint64_t backupLba,
                      uint64_t firstUsable, uint64_t lastUsable,
                      uint64_t entriesLba, uint32_t entryCount,
                      uint32_t arrayCrc)
{
    uint8_t* header = sector(disk, lba);
    std::memset(header, 0, disk.sectorSize);
    std::memcpy(header, "EFI PART", 8);
    write_u32(header + 8, 0x00010000u);
    write_u32(header + 12, 92);
    write_u64(header + 24, lba);
    write_u64(header + 32, backupLba);
    write_u64(header + 40, firstUsable);
    write_u64(header + 48, lastUsable);
    for (uint8_t i = 0; i < 16; ++i) header[56 + i] = static_cast<uint8_t>(i + 1);
    write_u64(header + 72, entriesLba);
    write_u32(header + 80, entryCount);
    write_u32(header + 84, 128);
    write_u32(header + 88, arrayCrc);
    write_u32(header + 16, 0);
    write_u32(header + 16, storage::crc32(header, 92));
}

void build_gpt(FakeDisk& disk, GptFixture fixture = GptFixture::Valid)
{
    std::fill(disk.bytes.begin(), disk.bytes.end(), 0);
    const uint32_t entryCount = fixture == GptFixture::TooManyEntries ? 129 : 128;
    const uint64_t arrayBytes = static_cast<uint64_t>(entryCount) * 128;
    const uint64_t arraySectors = (arrayBytes + disk.sectorSize - 1) / disk.sectorSize;
    const uint64_t primaryArrayLba = 2;
    const uint64_t backupHeaderLba = disk.sectorCount - 1;
    const uint64_t backupArrayLba = backupHeaderLba - arraySectors;
    const uint64_t firstUsable = primaryArrayLba + arraySectors;
    const uint64_t lastUsable = backupArrayLba - 1;
    std::vector<uint8_t> entries(static_cast<size_t>(arrayBytes), 0);

    uint8_t* first = entries.data();
    first[0] = 0x28;
    first[16] = 0x42;
    uint64_t firstStart = firstUsable + 4;
    uint64_t firstEnd = firstStart + 60;
    if (fixture == GptFixture::OutOfUsableRange) firstEnd = lastUsable + 1;
    write_u64(first + 32, firstStart);
    write_u64(first + 40, firstEnd);
    write_u64(first + 48, 0x1000000000000001ull);
    const char* name = "System";
    for (uint8_t i = 0; name[i]; ++i) write_u16(first + 56 + i * 2, name[i]);

    if (fixture == GptFixture::OverlappingPartitions) {
        uint8_t* second = entries.data() + 128;
        second[0] = 0x29;
        second[16] = 0x43;
        write_u64(second + 32, firstStart + 20);
        write_u64(second + 40, firstEnd + 20);
    }

    const uint32_t entriesCrc = storage::crc32(entries.data(), entries.size());
    for (uint64_t i = 0; i < arraySectors; ++i) {
        uint8_t* primary = sector(disk, primaryArrayLba + i);
        uint8_t* backup = sector(disk, backupArrayLba + i);
        const size_t offset = static_cast<size_t>(i * disk.sectorSize);
        const size_t amount = entries.size() - offset < disk.sectorSize
            ? entries.size() - offset : disk.sectorSize;
        std::memcpy(primary, entries.data() + offset, amount);
        std::memcpy(backup, entries.data() + offset, amount);
    }
    write_gpt_header(disk, 1, backupHeaderLba, firstUsable, lastUsable,
                     primaryArrayLba, entryCount, entriesCrc);
    write_gpt_header(disk, backupHeaderLba, 1, firstUsable, lastUsable,
                     backupArrayLba, entryCount, entriesCrc);

    set_mbr_signature(disk);
    const uint32_t protectiveCount = disk.sectorCount - 1 > 0xFFFFFFFFull
        ? 0xFFFFFFFFu : static_cast<uint32_t>(disk.sectorCount - 1);
    set_mbr_partition(disk, 0, 0, 0xEE, 1, protectiveCount);

    if (fixture == GptFixture::PrimaryHeaderCrcBad) sector(disk, 1)[16] ^= 0x80;
    if (fixture == GptFixture::BackupHeaderCrcBad) sector(disk, backupHeaderLba)[16] ^= 0x80;
    if (fixture == GptFixture::PrimaryArrayCrcBad ||
        fixture == GptFixture::BothArrayCrcBad) sector(disk, primaryArrayLba)[150] ^= 0x01;
    if (fixture == GptFixture::BothArrayCrcBad) sector(disk, backupArrayLba)[150] ^= 0x01;
}

storage::PartitionTableModel parse_fixture(FakeDisk& disk, bool& parsed)
{
    const uint8_t index = register_fake(disk);
    storage::PartitionTableModel model;
    parsed = storage::parse_partition_table(index, model);
    unregister_fake(index, disk);
    return model;
}

storage::InitializeDiskRequest make_initialize_request(
    uint8_t index, storage::PartitionScheme scheme)
{
    storage::InitializeDiskRequest request = {};
    storage::capture_target_identity(index, request.targetSnapshot);
    request.requestedScheme = scheme;
    request.mbrSignaturePolicy = storage::MBR_SIGNATURE_RANDOM_NONZERO;
    request.diskGuidSource = storage::DISK_GUID_SECURE_RANDOM;
    request.expectedRegistryGeneration = request.targetSnapshot.registryGeneration;
    request.testGuidProvided = true;
    for (uint8_t i = 0; i < 16; ++i) request.testDiskGuid[i] = i + 1;
    request.testMbrSignatureProvided = true;
    request.testMbrSignature = 0xA1B2C3D4u;
    return request;
}

storage::InitializeDiskStatus probe_fake_initialize(
    FakeDisk& disk, storage::PartitionScheme scheme,
    bool writable = true, bool withFlush = true, bool flushKnown = true,
    block::BootProvenance boot = block::BOOT_PROVENANCE_DEFINITELY_NOT_BOOT)
{
    const uint8_t index = register_fake(disk, writable, withFlush, flushKnown,
                                        false, 0, 0, boot);
    storage::TargetIdentity target;
    storage::InitializeTargetValidation validation;
    const bool captured = storage::capture_target_identity(index, target);
    const storage::InitializeDiskStatus status = captured
        ? storage::probe_initialize_target(target, scheme, validation)
        : storage::INITIALIZE_DISK_DEVICE_MISSING;
    unregister_fake(index, disk);
    return status;
}

uint32_t read_u32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t read_u64(const uint8_t* p)
{
    return static_cast<uint64_t>(read_u32(p)) |
        (static_cast<uint64_t>(read_u32(p + 4)) << 32);
}

bool verify_gpt_bytes(FakeDisk& disk, const storage::InitializeDiskPlan& plan)
{
    const uint32_t sectorSize = disk.sectorSize;
    const uint64_t backupHeaderLba = disk.sectorCount - 1;
    const uint64_t arrayBytes = storage::INITIALIZE_GPT_ARRAY_BYTES;
    const uint64_t arraySectors = arrayBytes / sectorSize;
    const uint64_t backupArrayLba = backupHeaderLba - arraySectors;
    const uint8_t* mbr = sector(disk, 0);
    const uint8_t* primary = sector(disk, 1);
    const uint8_t* backup = sector(disk, backupHeaderLba);
    const uint8_t* primaryArray = sector(disk, 2);
    const uint8_t* backupArray = sector(disk, backupArrayLba);
    if (mbr[510] != 0x55 || mbr[511] != 0xAA || mbr[450] != 0xEE ||
        read_u32(mbr + 454) != 1 || read_u32(mbr + 458) != disk.sectorCount - 1)
        return false;
    if (std::memcmp(primary, "EFI PART", 8) != 0 ||
        std::memcmp(backup, "EFI PART", 8) != 0) return false;
    if (read_u64(primary + 24) != 1 || read_u64(primary + 32) != backupHeaderLba ||
        read_u64(backup + 24) != backupHeaderLba || read_u64(backup + 32) != 1)
        return false;
    if (read_u64(primary + 40) != plan.firstUsableLba ||
        read_u64(primary + 48) != plan.lastUsableLba ||
        read_u64(backup + 40) != plan.firstUsableLba ||
        read_u64(backup + 48) != plan.lastUsableLba) return false;
    if (read_u64(primary + 72) != 2 || read_u64(backup + 72) != backupArrayLba ||
        read_u32(primary + 80) != storage::INITIALIZE_GPT_ENTRY_COUNT ||
        read_u32(primary + 84) != storage::INITIALIZE_GPT_ENTRY_SIZE)
        return false;
    if (std::memcmp(primary + 56, plan.diskGuid, 16) != 0 ||
        std::memcmp(backup + 56, plan.diskGuid, 16) != 0) return false;
    if (!std::all_of(primaryArray, primaryArray + arrayBytes,
                     [](uint8_t byte) { return byte == 0; }) ||
        !std::all_of(backupArray, backupArray + arrayBytes,
                     [](uint8_t byte) { return byte == 0; })) return false;
    if (read_u32(primary + 88) != storage::crc32(primaryArray, arrayBytes) ||
        read_u32(backup + 88) != storage::crc32(backupArray, arrayBytes)) return false;
    uint8_t headerCopy[4096];
    std::memcpy(headerCopy, primary, sectorSize);
    const uint32_t primaryHeaderCrc = read_u32(headerCopy + 16);
    write_u32(headerCopy + 16, 0);
    if (primaryHeaderCrc != storage::crc32(headerCopy, 92)) return false;
    std::memcpy(headerCopy, backup, sectorSize);
    const uint32_t backupHeaderCrc = read_u32(headerCopy + 16);
    write_u32(headerCopy + 16, 0);
    return backupHeaderCrc == storage::crc32(headerCopy, 92);
}

} // namespace

namespace kernel {
namespace vfs {

namespace {
MountPoint g_testMounts[VFS_MAX_MOUNTS] = {};
}

const MountPoint* get_mount_by_index(uint8_t index)
{
    return index < VFS_MAX_MOUNTS ? &g_testMounts[index] : nullptr;
}

void set_test_mount(uint8_t index, bool active, uint8_t deviceIndex, const char* path)
{
    if (index >= VFS_MAX_MOUNTS) return;
    std::memset(&g_testMounts[index], 0, sizeof(g_testMounts[index]));
    g_testMounts[index].active = active;
    g_testMounts[index].blockDevIndex = deviceIndex;
    if (path) std::strncpy(g_testMounts[index].path, path,
                           sizeof(g_testMounts[index].path) - 1);
}

void clear_test_mounts()
{
    std::memset(g_testMounts, 0, sizeof(g_testMounts));
}

} // namespace vfs
} // namespace kernel

int main()
{
    using namespace kernel;
    block::init();
    vfs::clear_test_mounts();

    check(storage::crc32("123456789", 9) == 0xCBF43926u, "CRC32 standard test vector");
    check(storage::valid_geometry(10, 512), "512-byte geometry accepted");
    check(storage::valid_geometry(10, 4096), "4096-byte geometry accepted");
    check(!storage::valid_geometry(10, 0), "zero sector size rejected");
    check(!storage::valid_geometry(10, 8192), "sector size above bound rejected");
    check(!storage::valid_geometry(UINT64_MAX, 4096), "capacity multiplication overflow rejected");
    check(!storage::checked_lba_range(100, 99, 2), "LBA range beyond capacity rejected");

    bool parsed = false;
    FakeDisk raw(512, 128);
    storage::PartitionTableModel model = parse_fixture(raw, parsed);
    check(parsed && model.state == storage::DISK_STATE_NOT_INITIALIZED,
          "zero-filled raw disk classified not initialized");

    FakeDisk emptyMbr(512, 128);
    set_mbr_signature(emptyMbr);
    model = parse_fixture(emptyMbr, parsed);
    check(parsed && model.state == storage::DISK_STATE_VALID_MBR && model.partitionCount == 0,
          "valid empty MBR distinguished from raw");

    FakeDisk protectiveMbr(512, 128);
    set_mbr_signature(protectiveMbr);
    set_mbr_partition(protectiveMbr, 0, 0, 0xEE, 1, 127);
    model = parse_fixture(protectiveMbr, parsed);
    check(parsed && model.protectiveMbr &&
          model.state == storage::DISK_STATE_INVALID_PARTITION_TABLE,
          "protective MBR without a valid GPT is not misclassified as ordinary MBR or raw");

    FakeDisk oneMbr(512, 128);
    set_mbr_signature(oneMbr);
    set_mbr_partition(oneMbr, 0, 0, 0x83, 8, 10);
    model = parse_fixture(oneMbr, parsed);
    check(parsed && model.state == storage::DISK_STATE_VALID_MBR &&
          model.partitionCount == 1 && model.partitions[0].startLba == 8 &&
          model.partitions[0].sectorCount == 10, "populated MBR partition parsed");

    FakeDisk outOfRangeMbr(512, 128);
    set_mbr_signature(outOfRangeMbr);
    set_mbr_partition(outOfRangeMbr, 0, 0, 0x83, 120, 20);
    model = parse_fixture(outOfRangeMbr, parsed);
    check(parsed && model.state == storage::DISK_STATE_INVALID_PARTITION_TABLE,
          "MBR partition beyond disk rejected");

    FakeDisk malformedMbr(512, 128);
    set_mbr_signature(malformedMbr);
    set_mbr_partition(malformedMbr, 0, 0x7F, 0x83, 8, 10);
    model = parse_fixture(malformedMbr, parsed);
    check(parsed && model.state == storage::DISK_STATE_INVALID_PARTITION_TABLE,
          "invalid MBR status byte rejected");

    FakeDisk overlapMbr(512, 128);
    set_mbr_signature(overlapMbr);
    set_mbr_partition(overlapMbr, 0, 0, 0x83, 8, 20);
    set_mbr_partition(overlapMbr, 1, 0, 0x07, 20, 20);
    model = parse_fixture(overlapMbr, parsed);
    check(parsed && model.state == storage::DISK_STATE_INVALID_PARTITION_TABLE,
          "overlapping primary MBR partitions rejected");

    FakeDisk extendedMbr(512, 128);
    set_mbr_signature(extendedMbr);
    set_mbr_partition(extendedMbr, 0, 0, 0x0F, 8, 100);
    model = parse_fixture(extendedMbr, parsed);
    check(parsed && model.state == storage::DISK_STATE_UNSUPPORTED_PARTITION_SCHEME &&
          model.extendedPartitionsPresent, "extended MBR partition explicitly unsupported");

    FakeDisk validGpt(512, 4096);
    build_gpt(validGpt);
    model = parse_fixture(validGpt, parsed);
    check(parsed && model.state == storage::DISK_STATE_VALID_GPT &&
          model.primaryGptValid && model.backupGptValid && model.partitionCount == 1 &&
          std::strcmp(model.partitions[0].name, "System") == 0,
          "valid primary and backup GPT with UTF-8 name parsed");

    FakeDisk badHeader(512, 4096);
    build_gpt(badHeader, GptFixture::PrimaryHeaderCrcBad);
    model = parse_fixture(badHeader, parsed);
    check(parsed && model.state == storage::DISK_STATE_GPT_DEGRADED &&
          !model.primaryGptValid && model.backupGptValid,
          "bad primary GPT header CRC falls back to valid backup");

    FakeDisk badArray(512, 4096);
    build_gpt(badArray, GptFixture::BothArrayCrcBad);
    model = parse_fixture(badArray, parsed);
    check(parsed && model.state == storage::DISK_STATE_INVALID_PARTITION_TABLE,
          "bad primary and backup partition-array CRCs rejected");

    FakeDisk primaryBadBackupValid(512, 4096);
    build_gpt(primaryBadBackupValid, GptFixture::PrimaryHeaderCrcBad);
    model = parse_fixture(primaryBadBackupValid, parsed);
    check(parsed && !model.primaryGptValid && model.backupGptValid &&
          model.partitionCount == 1, "primary-invalid backup-valid GPT fixture");

    FakeDisk primaryValidBackupBad(512, 4096);
    build_gpt(primaryValidBackupBad, GptFixture::BackupHeaderCrcBad);
    model = parse_fixture(primaryValidBackupBad, parsed);
    check(parsed && model.primaryGptValid && !model.backupGptValid &&
          model.state == storage::DISK_STATE_GPT_DEGRADED,
          "primary-valid backup-invalid GPT fixture");

    FakeDisk arrayOneBad(512, 4096);
    build_gpt(arrayOneBad, GptFixture::PrimaryArrayCrcBad);
    model = parse_fixture(arrayOneBad, parsed);
    check(parsed && !model.primaryGptValid && model.backupGptValid &&
          model.state == storage::DISK_STATE_GPT_DEGRADED,
          "single GPT partition-array CRC failure is degraded");

    FakeDisk outOfRangeGpt(512, 4096);
    build_gpt(outOfRangeGpt, GptFixture::OutOfUsableRange);
    model = parse_fixture(outOfRangeGpt, parsed);
    check(parsed && model.state == storage::DISK_STATE_INVALID_PARTITION_TABLE,
          "GPT partition outside usable range rejected");

    FakeDisk overlapGpt(512, 4096);
    build_gpt(overlapGpt, GptFixture::OverlappingPartitions);
    model = parse_fixture(overlapGpt, parsed);
    check(parsed && model.state == storage::DISK_STATE_INVALID_PARTITION_TABLE,
          "overlapping GPT partitions rejected");

    FakeDisk tooManyGpt(512, 4096);
    build_gpt(tooManyGpt, GptFixture::TooManyEntries);
    model = parse_fixture(tooManyGpt, parsed);
    check(parsed && model.state == storage::DISK_STATE_UNSUPPORTED_PARTITION_SCHEME,
          "GPT entry count above the bounded maximum rejected as unsupported");

    FakeDisk fourKnMbr(4096, 64);
    set_mbr_signature(fourKnMbr);
    set_mbr_partition(fourKnMbr, 0, 0, 0x83, 4, 8);
    model = parse_fixture(fourKnMbr, parsed);
    check(parsed && model.state == storage::DISK_STATE_VALID_MBR &&
          model.partitions[0].startLba == 4,
          "4096-byte logical sector MBR parsed without a 512-byte read buffer");

    FakeDisk fourKnGpt(4096, 256);
    build_gpt(fourKnGpt);
    model = parse_fixture(fourKnGpt, parsed);
    check(parsed && model.state == storage::DISK_STATE_VALID_GPT &&
          model.primaryGptValid && model.backupGptValid,
          "4096-byte logical sector GPT parsed with both copies");

    FakeDisk unreadable(512, 128);
    unreadable.failReads = true;
    model = parse_fixture(unreadable, parsed);
    check(!parsed && model.state == storage::DISK_STATE_UNREADABLE,
          "sector-zero read failure classified unreadable");

    FakeDisk unreadableRawProbe(512, 128);
    unreadableRawProbe.failLba = 1;
    model = parse_fixture(unreadableRawProbe, parsed);
    check(parsed && model.state == storage::DISK_STATE_UNREADABLE &&
          model.error == storage::PARTITION_ERROR_READ_FAILED,
          "failed bounded raw probe is unreadable rather than invalid or raw");

    FakeDisk smallBuffer(4096, 32);
    uint8_t guarded[520];
    std::memset(guarded, 0xA5, sizeof(guarded));
    uint8_t index = register_fake(smallBuffer);
    const block::Status shortRead = storage::read_logical_sector(index, 0, guarded + 4, 512);
    unregister_fake(index, smallBuffer);
    check(shortRead == block::BLOCK_ERR_INVALID && guarded[0] == 0xA5 &&
          guarded[3] == 0xA5 && guarded[516] == 0xA5 && guarded[519] == 0xA5,
          "geometry-safe helper rejects a short 4Kn buffer before dispatch");

    FakeDisk constrained(512, 128);
    index = register_fake(constrained, true, false, false, false, 4096, 4096);
    alignas(4096) uint8_t alignedTransfer[8192] = {};
    uint8_t unalignedTransfer[8193] = {};
    check(storage::read_logical_sector(index, 0, alignedTransfer,
                                       sizeof(alignedTransfer)) == block::BLOCK_OK,
          "aligned single-sector transfer respects declared DMA constraints");
    check(block::read_sectors(index, 0, 1, unalignedTransfer + 1) ==
              block::BLOCK_ERR_INVALID,
          "raw block I/O enforces declared DMA alignment");
    check(storage::read_sectors_safe(index, 0, 9, alignedTransfer,
                                     sizeof(alignedTransfer)) == block::BLOCK_ERR_UNSUPPORTED,
          "safe reads reject transfers above the declared transport limit");
    check(storage::write_sectors_safe(index, 0, 1, alignedTransfer,
                                      sizeof(alignedTransfer)) == block::BLOCK_OK &&
          constrained.writes == 1,
          "safe writes validate geometry, alignment, and buffer capacity");
    check(storage::write_sectors_safe(index, 0, 1, unalignedTransfer + 1,
                                      sizeof(unalignedTransfer) - 1) == block::BLOCK_ERR_INVALID,
          "safe writes reject misaligned DMA buffers");
    check(storage::write_sectors_safe(index, 0, 9, alignedTransfer,
                                      sizeof(alignedTransfer)) == block::BLOCK_ERR_UNSUPPORTED,
          "safe writes reject transfers above the declared transport limit");
    unregister_fake(index, constrained);

    FakeDisk readOnly(512, 128);
    index = register_fake(readOnly);
    storage::DeviceCapabilities capabilities;
    check(storage::query_device_capabilities(index, capabilities) &&
          capabilities.readable && !capabilities.writable &&
          capabilities.logicalSectorSize == 512 && capabilities.totalCapacityBytes == 65536,
          "capability query reports read-only device geometry and capacity");
    storage::TargetIdentity snapshot;
    check(storage::capture_target_identity(index, snapshot) &&
          storage::revalidate_target_identity(snapshot) == storage::TARGET_VALID,
          "target identity captures and revalidates while registry is stable");
    storage::SafetyRequest request = {};
    request.requireWritable = true;
    request.requireDurableWrites = false;
    request.requireKnownPartitionState = false;
    request.allowNotInitialized = true;
    storage::SafetyValidation validation;
    check(!storage::validate_destructive_target(snapshot, request, validation) &&
          (validation.issues & storage::SAFETY_ISSUE_READ_ONLY) != 0,
          "safety validator rejects a read-only device");

    FakeDisk extra(512, 128);
    const uint8_t extraIndex = register_fake(extra);
    check(storage::revalidate_target_identity(snapshot) == storage::TARGET_REGISTRY_CHANGED,
          "registry generation invalidates a previously captured target");
    unregister_fake(extraIndex, extra);
    unregister_fake(index, readOnly);

    FakeDisk rootDevice(512, 128);
    set_mbr_signature(rootDevice);
    index = register_fake(rootDevice, true, true, true);
    check(storage::capture_target_identity(index, snapshot), "root test target captured");
    vfs::set_test_mount(0, true, index, "/");
    storage::MountProtection mount = storage::query_mount_protection(snapshot);
    check(mount.safety == storage::DEVICE_ROOT_BACKING && !mount.partitionIdentityKnown,
          "root mount protects the whole backing block device");
    request.requireWritable = true;
    request.requireDurableWrites = true;
    request.requireKnownPartitionState = true;
    request.allowNotInitialized = true;
    check(!storage::validate_destructive_target(snapshot, request, validation) &&
          (validation.issues & storage::SAFETY_ISSUE_ROOT_BACKING) != 0,
          "destructive validator rejects the root backing device");
    vfs::clear_test_mounts();
    vfs::set_test_mount(1, true, index, "/data");
    check(storage::query_mount_protection(snapshot).safety == storage::DEVICE_MOUNTED,
          "active non-root mount protects the whole backing device");
    vfs::clear_test_mounts();
    unregister_fake(index, rootDevice);

    FakeDisk flushDevice(512, 128);
    index = register_fake(flushDevice, true, true, true);
    block::FlushReport flush = block::flush_with_result(index);
    check(flush.outcome == block::FLUSH_OUTCOME_SUPPORTED_SUCCEEDED &&
          flush.semanticsKnown && flush.status == block::BLOCK_OK,
          "supported flush reports success and known semantics");
    flushDevice.failFlush = true;
    flush = block::flush_with_result(index);
    check(flush.outcome == block::FLUSH_OUTCOME_FAILED &&
          flush.status == block::BLOCK_ERR_IO,
          "failed flush is distinguishable from unsupported flush");
    unregister_fake(index, flushDevice);

    FakeDisk noFlush(512, 128);
    index = register_fake(noFlush, true);
    flush = block::flush_with_result(index);
    check(flush.outcome == block::FLUSH_OUTCOME_UNSUPPORTED_UNKNOWN &&
          block::flush(index) == block::BLOCK_ERR_UNSUPPORTED,
          "missing flush callback is reported unsupported and unknown");
    storage::DeviceCapabilities noFlushCapabilities;
    storage::TargetIdentity noFlushTarget;
    storage::SafetyRequest durableRequest = {};
    durableRequest.requireWritable = true;
    durableRequest.requireDurableWrites = true;
    durableRequest.requireKnownPartitionState = true;
    durableRequest.allowNotInitialized = true;
    storage::SafetyValidation noFlushValidation;
    check(storage::query_device_capabilities(index, noFlushCapabilities) &&
          noFlushCapabilities.persistence == storage::PERSISTENCE_UNKNOWN &&
          storage::capture_target_identity(index, noFlushTarget) &&
          !storage::validate_destructive_target(noFlushTarget, durableRequest,
                                                noFlushValidation) &&
          (noFlushValidation.issues & storage::SAFETY_ISSUE_DURABILITY_UNKNOWN) != 0,
          "destructive preflight rejects a writable transport with unknown durability");
    unregister_fake(index, noFlush);

    FakeDisk synchronous(512, 128);
    index = register_fake(synchronous, true, false, false, true);
    flush = block::flush_with_result(index);
    check(flush.outcome == block::FLUSH_OUTCOME_SYNCHRONOUS_DURABLE &&
          flush.semanticsKnown, "explicit synchronous durability is represented");
    unregister_fake(index, synchronous);

    check(storage::DEFAULT_INITIALIZE_SCHEME == storage::PARTITION_SCHEME_GPT,
          "GPT is the default initialization scheme presented by the model");

    FakeDisk synchronousInitialize(512, 4096);
    index = register_fake(synchronousInitialize, true, false, false, true);
    storage::InitializeDiskRequest initializeRequest = {};
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    storage::InitializeDiskPlan initializePlan;
    storage::InitializeDiskResult initializeResult;
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION &&
          storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_SUCCESS &&
          initializeResult.flushOutcome == block::FLUSH_OUTCOME_SYNCHRONOUS_DURABLE &&
          synchronousInitialize.flushes == 0,
          "GPT initialization accepts only an explicitly synchronous-durable write model");
    unregister_fake(index, synchronousInitialize);

    FakeDisk initializeGpt512(512, 4096);
    index = register_fake(initializeGpt512, true, true, true);
    initializeRequest = make_initialize_request(
        index, storage::PARTITION_SCHEME_GPT);
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION &&
          initializePlan.confirmationReady && storage::storage_operation_active(),
          "GPT 512-byte raw disk reaches confirmation while holding the operation lease");
    check(storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_SUCCESS &&
          initializeResult.finalDetectedState == storage::DISK_STATE_VALID_GPT &&
          initializeResult.verificationPassed &&
          initializeResult.flushOutcome == block::FLUSH_OUTCOME_SUPPORTED_SUCCEEDED &&
          initializeResult.writeStagesCompleted ==
              (storage::INITIALIZE_WRITE_BACKUP_ARRAY |
               storage::INITIALIZE_WRITE_BACKUP_HEADER |
               storage::INITIALIZE_WRITE_PRIMARY_ARRAY |
               storage::INITIALIZE_WRITE_PRIMARY_HEADER |
               storage::INITIALIZE_WRITE_PROTECTIVE_MBR) &&
          !storage::storage_operation_active(),
          "GPT 512-byte initialization writes, flushes, verifies, rescans, and releases the lease");
    check(verify_gpt_bytes(initializeGpt512, initializePlan),
          "GPT 512-byte bytes have valid PMBR, primary/backup geometry, empty arrays, and CRCs");
    check((initializePlan.diskGuid[7] & 0xF0) == 0x40 &&
          (initializePlan.diskGuid[8] & 0xC0) == 0x80,
          "injected GPT GUID is emitted with UUID version and variant bits");
    check(storage::parse_partition_table(index, model) &&
          model.state == storage::DISK_STATE_VALID_GPT &&
          model.primaryGptValid && model.backupGptValid && model.gptCopiesAgree &&
          model.partitionCount == 0 &&
          std::memcmp(model.primaryDiskGuid, initializePlan.diskGuid, 16) == 0 &&
          std::memcmp(model.backupDiskGuid, initializePlan.diskGuid, 16) == 0,
          "normal parser confirms both GPT copies and zero used entries after rescan");
    unregister_fake(index, initializeGpt512);

    FakeDisk initializeGpt4Kn(4096, 1024);
    index = register_fake(initializeGpt4Kn, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION &&
          initializePlan.entryArraySectors == 4 &&
          initializePlan.firstUsableLba == 256,
          "GPT 4096-byte layout uses four array sectors and 1 MiB usable alignment");
    check(storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_SUCCESS &&
          initializeResult.finalDetectedState == storage::DISK_STATE_VALID_GPT &&
          verify_gpt_bytes(initializeGpt4Kn, initializePlan),
          "GPT initialization and independent byte checks pass on 4096-byte logical sectors");
    unregister_fake(index, initializeGpt4Kn);

    FakeDisk initializeMbr512(512, 128);
    index = register_fake(initializeMbr512, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_MBR);
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION &&
          storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_SUCCESS &&
          initializeResult.finalDetectedState == storage::DISK_STATE_VALID_MBR &&
          initializeResult.writeStagesCompleted == storage::INITIALIZE_WRITE_MBR,
          "MBR initializes a 512-byte raw disk as an empty valid MBR");
    check(initializeMbr512.bytes[510] == 0x55 && initializeMbr512.bytes[511] == 0xAA &&
          read_u32(initializeMbr512.bytes.data() + 440) == 0xA1B2C3D4u &&
          std::all_of(initializeMbr512.bytes.begin(), initializeMbr512.bytes.begin() + 440,
                      [](uint8_t byte) { return byte == 0; }) &&
          std::all_of(initializeMbr512.bytes.begin() + 446,
                      initializeMbr512.bytes.begin() + 510,
                      [](uint8_t byte) { return byte == 0; }) &&
          storage::parse_partition_table(index, model) &&
          model.state == storage::DISK_STATE_VALID_MBR &&
          model.partitionCount == 0 && model.mbrDiskSignature == 0xA1B2C3D4u,
          "MBR signature, zero entries, and expected disk signature are verified by parser");
    unregister_fake(index, initializeMbr512);

    FakeDisk initializeMbr4Kn(4096, 64);
    index = register_fake(initializeMbr4Kn, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_MBR);
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION &&
          storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_SUCCESS &&
          initializeResult.finalDetectedState == storage::DISK_STATE_VALID_MBR &&
          initializeMbr4Kn.bytes[510] == 0x55 && initializeMbr4Kn.bytes[511] == 0xAA,
          "MBR initialization handles a 4096-byte logical-sector device");
    unregister_fake(index, initializeMbr4Kn);

    FakeDisk existingMbr(512, 128);
    set_mbr_signature(existingMbr);
    check(probe_fake_initialize(existingMbr, storage::PARTITION_SCHEME_GPT) ==
              storage::INITIALIZE_DISK_NOT_RAW,
          "initialization refuses an existing valid MBR");
    FakeDisk existingGpt(512, 4096);
    build_gpt(existingGpt);
    check(probe_fake_initialize(existingGpt, storage::PARTITION_SCHEME_MBR) ==
              storage::INITIALIZE_DISK_NOT_RAW,
          "initialization refuses an existing valid GPT");
    FakeDisk degradedGpt(512, 4096);
    build_gpt(degradedGpt, GptFixture::PrimaryHeaderCrcBad);
    check(probe_fake_initialize(degradedGpt, storage::PARTITION_SCHEME_GPT) ==
              storage::INITIALIZE_DISK_NOT_RAW,
          "initialization refuses a degraded GPT");
    FakeDisk invalidMbr(512, 128);
    set_mbr_signature(invalidMbr);
    set_mbr_partition(invalidMbr, 0, 0x7F, 0x83, 8, 10);
    check(probe_fake_initialize(invalidMbr, storage::PARTITION_SCHEME_GPT) ==
              storage::INITIALIZE_DISK_NOT_RAW,
          "initialization refuses an invalid partition table");
    FakeDisk corruptGptInit(512, 4096);
    build_gpt(corruptGptInit, GptFixture::BothArrayCrcBad);
    check(probe_fake_initialize(corruptGptInit,
                                storage::PARTITION_SCHEME_GPT) ==
              storage::INITIALIZE_DISK_NOT_RAW,
          "initialization refuses a GPT with corrupt primary and backup arrays");
    FakeDisk unsupportedMbrInit(512, 128);
    set_mbr_signature(unsupportedMbrInit);
    set_mbr_partition(unsupportedMbrInit, 0, 0, 0x0F, 8, 100);
    check(probe_fake_initialize(unsupportedMbrInit,
                                storage::PARTITION_SCHEME_GPT) ==
              storage::INITIALIZE_DISK_NOT_RAW,
          "initialization refuses an unsupported extended-partition scheme");
    FakeDisk unreadableInit(512, 128);
    unreadableInit.failReads = true;
    check(probe_fake_initialize(unreadableInit, storage::PARTITION_SCHEME_GPT) ==
              storage::INITIALIZE_DISK_READ_UNAVAILABLE,
          "initialization refuses unreadable media");
    FakeDisk readOnlyInit(512, 128);
    check(probe_fake_initialize(readOnlyInit, storage::PARTITION_SCHEME_GPT,
                                false) == storage::INITIALIZE_DISK_READ_ONLY,
          "initialization refuses a read-only target");
    FakeDisk shortTransferInit(512, 128);
    index = register_fake(shortTransferInit, true, true, true, false, 0, 256);
    storage::TargetIdentity shortTransferIdentity;
    storage::InitializeTargetValidation shortTransferValidation;
    storage::capture_target_identity(index, shortTransferIdentity);
    check(storage::probe_initialize_target(shortTransferIdentity,
                                          storage::PARTITION_SCHEME_GPT,
                                          shortTransferValidation) ==
              storage::INITIALIZE_DISK_WRITE_UNAVAILABLE,
          "initialization rejects a transport that cannot write one logical sector");
    unregister_fake(index, shortTransferInit);
    FakeDisk mountedInit(512, 128);
    index = register_fake(mountedInit, true, true, true);
    storage::capture_target_identity(index, snapshot);
    vfs::set_test_mount(2, true, index, "/data");
    storage::InitializeTargetValidation initializeValidation;
    check(storage::probe_initialize_target(snapshot, storage::PARTITION_SCHEME_GPT,
                                           initializeValidation) ==
              storage::INITIALIZE_DISK_MOUNTED,
          "initialization refuses a mounted target");
    vfs::clear_test_mounts();
    vfs::set_test_mount(3, true, index, "/");
    check(storage::probe_initialize_target(snapshot, storage::PARTITION_SCHEME_GPT,
                                           initializeValidation) ==
              storage::INITIALIZE_DISK_ROOT_BACKING,
          "initialization refuses the root backing device");
    vfs::clear_test_mounts();
    unregister_fake(index, mountedInit);
    FakeDisk bootInit(512, 128);
    check(probe_fake_initialize(bootInit, storage::PARTITION_SCHEME_GPT, true,
                                true, true,
                                block::BOOT_PROVENANCE_BOOT_BACKING) ==
              storage::INITIALIZE_DISK_BOOT_BACKING,
          "initialization refuses a target explicitly marked as boot backing");
    FakeDisk unknownBootInit(512, 128);
    check(probe_fake_initialize(unknownBootInit, storage::PARTITION_SCHEME_GPT,
                                true, true, true,
                                block::BOOT_PROVENANCE_UNKNOWN) ==
              storage::INITIALIZE_DISK_BOOT_IDENTITY_UNKNOWN,
          "initialization refuses unknown firmware boot-device identity");
    FakeDisk unknownDurabilityInit(512, 128);
    check(probe_fake_initialize(unknownDurabilityInit,
                                storage::PARTITION_SCHEME_GPT,
                                true, false, false) ==
              storage::INITIALIZE_DISK_DURABILITY_UNKNOWN,
          "initialization refuses unknown durability");
    FakeDisk unsupportedSectorInit(1024, 4096);
    check(probe_fake_initialize(unsupportedSectorInit,
                                storage::PARTITION_SCHEME_GPT) ==
              storage::INITIALIZE_DISK_UNSUPPORTED_SECTOR_SIZE,
          "initialization refuses unsupported logical sector size");
    FakeDisk tooSmallGpt(512, 512);
    check(probe_fake_initialize(tooSmallGpt, storage::PARTITION_SCHEME_GPT) ==
              storage::INITIALIZE_DISK_TOO_SMALL,
          "GPT preflight rejects a disk too small for its 1 MiB usable range");
    FakeDisk tooSmallMbr(512, 8);
    check(probe_fake_initialize(tooSmallMbr, storage::PARTITION_SCHEME_MBR) ==
              storage::INITIALIZE_DISK_TOO_SMALL,
          "MBR preflight rejects a disk below its minimum sector count");
    FakeDisk tooLargeMbr(512, 0x100000001ull, false);
    check(probe_fake_initialize(tooLargeMbr, storage::PARTITION_SCHEME_MBR) ==
              storage::INITIALIZE_DISK_MBR_CAPACITY_LIMIT,
          "MBR preflight rejects capacity beyond 32-bit LBA addressability");
    FakeDisk dirtyBackupMetadata(512, 4096);
    sector(dirtyBackupMetadata, dirtyBackupMetadata.sectorCount - 33)[8] = 0x91;
    check(probe_fake_initialize(dirtyBackupMetadata,
                                storage::PARTITION_SCHEME_GPT) ==
              storage::INITIALIZE_DISK_METADATA_NOT_CLEAR,
          "initialization refuses nonzero metadata in an otherwise raw probe region");

    FakeDisk lockDiskA(512, 4096);
    FakeDisk lockDiskB(512, 4096);
    const uint8_t lockIndexA = register_fake(lockDiskA, true, true, true);
    const uint8_t lockIndexB = register_fake(lockDiskB, true, true, true);
    storage::InitializeDiskRequest lockRequestA = make_initialize_request(
        lockIndexA, storage::PARTITION_SCHEME_GPT);
    storage::InitializeDiskRequest lockRequestB = make_initialize_request(
        lockIndexB, storage::PARTITION_SCHEME_GPT);
    storage::InitializeDiskPlan lockPlanA;
    storage::InitializeDiskPlan lockPlanB;
    check(storage::prepare_initialize_disk(lockRequestA, lockPlanA,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION,
          "first initialization owns the storage operation lock through confirmation");
    check(storage::prepare_initialize_disk(lockRequestB, lockPlanB,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_OPERATION_BUSY &&
          storage::storage_operation_active(),
          "operation lock rejects a competing initialization while one is active");
    check(storage::cancel_initialize_disk(lockPlanA) &&
          !storage::storage_operation_active(),
          "cancel releases the confirmation-held operation lock");
    storage::StorageOperationLease leaseA = {0};
    storage::StorageOperationLease leaseB = {0};
    check(storage::try_acquire_storage_operation(leaseA) ==
              storage::STORAGE_OPERATION_LOCK_ACQUIRED,
          "operation lock can be acquired after cancellation");
    check(storage::try_acquire_storage_operation(leaseA) ==
              storage::STORAGE_OPERATION_LOCK_INVALID_OWNER &&
          storage::try_acquire_storage_operation(leaseB) ==
              storage::STORAGE_OPERATION_LOCK_BUSY,
          "nested and contending operation-lock acquisition are rejected");
    storage::StorageOperationLease staleLease = leaseA;
    check(storage::release_storage_operation(leaseA) &&
          storage::try_acquire_storage_operation(leaseB) ==
              storage::STORAGE_OPERATION_LOCK_ACQUIRED &&
          !storage::release_storage_operation(staleLease) &&
          storage::storage_operation_lease_is_current(leaseB),
          "stale ownership token cannot release a subsequently acquired operation");
    check(storage::release_storage_operation(leaseB) &&
          !storage::storage_operation_active(),
          "operation lock releases after explicit lease cleanup");
    check(storage::try_acquire_storage_operation(leaseA) ==
              storage::STORAGE_OPERATION_LOCK_ACQUIRED,
          "operation lease can be acquired for an executing operation");
    storage::StorageOperationLease copiedOwner = leaseA;
    check(storage::begin_storage_operation_execution(leaseA) &&
          !storage::release_storage_operation(copiedOwner) &&
          !storage::begin_storage_operation_execution(copiedOwner) &&
          storage::storage_operation_active(),
          "copied owner cannot cancel or begin a second execution after commit starts");
    check(storage::complete_storage_operation_execution(leaseA) &&
          !storage::storage_operation_active(),
          "executing operation releases only through its completion path");
    unregister_fake(lockIndexA, lockDiskA);
    unregister_fake(lockIndexB, lockDiskB);

    FakeDisk replacedDisk(512, 4096);
    index = register_fake(replacedDisk, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION,
          "identity-revalidation fixture reaches confirmation-ready state");
    unregister_fake(index, replacedDisk);
    FakeDisk replacementTargetDisk(512, 4096);
    const uint8_t replacementBlockIndex = register_fake(replacementTargetDisk, true, true, true);
    check(storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_REGISTRY_CHANGED &&
          replacedDisk.writeAttempts == 0 && replacementTargetDisk.writeAttempts == 0 &&
          !storage::storage_operation_active(),
          "registry change and device replacement after confirmation abort before writes");
    unregister_fake(replacementBlockIndex, replacementTargetDisk);

    FakeDisk identityChangedDisk(512, 4096);
    index = register_fake(identityChangedDisk, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION,
          "identity-change fixture reaches confirmation-ready state");
    const_cast<block::BlockDevice*>(block::get_device(index))->serial[0] = 'X';
    check(storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_IDENTITY_CHANGED &&
          identityChangedDisk.writeAttempts == 0 &&
          !storage::storage_operation_active(),
          "device identity change without a generation update aborts before writes");
    unregister_fake(index, identityChangedDisk);

    FakeDisk tamperedPlanDisk(512, 4096);
    index = register_fake(tamperedPlanDisk, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION,
          "immutable-plan fixture reaches confirmation-ready state");
    initializePlan.entryArraySectors = UINT32_MAX;
    check(storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_OPERATION_OWNERSHIP_INVALID &&
          tamperedPlanDisk.writeAttempts == 0 &&
          !storage::storage_operation_active(),
          "modified confirmation plan is rejected before any metadata write");
    unregister_fake(index, tamperedPlanDisk);

    FakeDisk changedTable(512, 4096);
    index = register_fake(changedTable, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION,
          "table-change fixture reaches confirmation-ready state");
    sector(changedTable, 0)[100] = 0x11;
    check(storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_NOT_RAW && changedTable.writeAttempts == 0,
          "media changed after confirmation is rejected before the first write");
    unregister_fake(index, changedTable);

    FakeDisk writeFailFirst(512, 4096);
    index = register_fake(writeFailFirst, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    writeFailFirst.failWriteAtCall1 = 1;
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION &&
          storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_IO_FAILED &&
          initializeResult.rollbackAttempted && initializeResult.rollbackSucceeded &&
          initializeResult.finalDetectedState == storage::DISK_STATE_NOT_INITIALIZED &&
          !storage::storage_operation_active(),
          "first GPT metadata write failure rolls back and releases the lock");
    unregister_fake(index, writeFailFirst);

    FakeDisk writeFailMiddle(512, 4096);
    index = register_fake(writeFailMiddle, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    writeFailMiddle.failWriteAtCall1 = 40;
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION &&
          storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_IO_FAILED &&
          initializeResult.rollbackAttempted && initializeResult.rollbackSucceeded &&
          initializeResult.finalDetectedState == storage::DISK_STATE_NOT_INITIALIZED,
          "middle GPT metadata write failure restores the bounded sector snapshot");
    unregister_fake(index, writeFailMiddle);

    FakeDisk writeFailFinal(512, 4096);
    index = register_fake(writeFailFinal, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    writeFailFinal.failWriteAtCall1 = 67;
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION &&
          storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_IO_FAILED &&
          initializeResult.rollbackAttempted && initializeResult.rollbackSucceeded,
          "final protective-MBR write failure rolls back the GPT metadata");
    unregister_fake(index, writeFailFinal);

    FakeDisk flushFailAfterWrite(512, 4096);
    index = register_fake(flushFailAfterWrite, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    flushFailAfterWrite.failFlushAtCall = 2;
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION &&
          storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_FLUSH_FAILED &&
          initializeResult.flushOutcome == block::FLUSH_OUTCOME_FAILED &&
          initializeResult.rollbackAttempted && initializeResult.rollbackSucceeded,
          "post-write flush failure rejects success and verifies snapshot restoration");
    unregister_fake(index, flushFailAfterWrite);

    FakeDisk corruptVerification(512, 4096);
    index = register_fake(corruptVerification, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    corruptVerification.corruptWriteLbaOnce = 1;
    corruptVerification.corruptWritePending = true;
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION &&
          storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_VERIFICATION_FAILED &&
          !initializeResult.verificationPassed && initializeResult.rollbackSucceeded &&
          initializeResult.finalDetectedState == storage::DISK_STATE_NOT_INITIALIZED,
          "normal-parser verification failure triggers bounded rollback");
    unregister_fake(index, corruptVerification);

    FakeDisk rollbackFail(512, 4096);
    index = register_fake(rollbackFail, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    rollbackFail.failWriteAtCall1 = 67;
    rollbackFail.failWriteAtCall2 = 68;
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION &&
          storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_ROLLBACK_FAILED &&
          initializeResult.rollbackAttempted && !initializeResult.rollbackSucceeded &&
          initializeResult.finalStateUncertain && !storage::storage_operation_active(),
          "rollback failure is reported as uncertain while still releasing the lock");
    unregister_fake(index, rollbackFail);

    FakeDisk firstFlushFail(512, 4096);
    index = register_fake(firstFlushFail, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    firstFlushFail.failFlushAtCall = 1;
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION &&
          storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_FLUSH_FAILED &&
          !initializeResult.writeAttempted && firstFlushFail.writeAttempts == 0 &&
          !storage::storage_operation_active(),
          "pre-write flush failure rejects the operation without modifying sectors");
    unregister_fake(index, firstFlushFail);

    block::init();
    ramdisk::init();
    uint8_t* image = new uint8_t[512];
    std::memset(image, 0x5A, 512);
    const uint8_t ramIndex = ramdisk::create_readonly_owned(image, 512, "rescan.img");
    check(ramIndex != 0xFF && ramdisk::get_disk(ramIndex) != nullptr,
          "read-only image attaches with transferred ownership");
    const ramdisk::RamDisk* ram = ramdisk::get_disk(ramIndex);
    const uint8_t ramBlockIndex = ram ? ram->blockDeviceIndex : 0xFF;
    const uint64_t instanceId = ram ? ram->instanceId : 0;
    storage::DeviceCapabilities ramCapabilities;
    flush = block::flush_with_result(ramBlockIndex);
    check(storage::query_device_capabilities(ramBlockIndex, ramCapabilities) &&
          ramCapabilities.persistence == storage::PERSISTENCE_VOLATILE_MEMORY &&
          flush.outcome == block::FLUSH_OUTCOME_SUPPORTED_SUCCEEDED &&
          flush.semanticsKnown,
          "RAM-disk flush completes synchronously without claiming durable storage");
    check(storage::write_sectors_safe(ramBlockIndex, 0, 1, image, 512) ==
              block::BLOCK_ERR_UNSUPPORTED,
          "attached image remains read-only through the safe write API");
    ramdisk::clear(ramIndex);
    check(image[0] == 0x5A,
          "clear does not mutate a read-only attached image");
    uint64_t generation = block::registry_generation();
    bool repeatStable = ram != nullptr;
    for (int rescan = 0; rescan < 32 && ram; ++rescan) {
        const ramdisk::RamDisk* current = ramdisk::get_disk(ramIndex);
        storage::DeviceCapabilities currentCapabilities;
        if (!current || current->instanceId != instanceId ||
            current->blockDeviceIndex != ramBlockIndex ||
            !ramdisk::validate_attachment_identity(ramIndex, ramBlockIndex,
                                                    instanceId) ||
            !storage::query_device_capabilities(ramBlockIndex, currentCapabilities) ||
            block::registry_generation() != generation) {
            repeatStable = false;
            break;
        }
    }
    alignas(4096) uint8_t imageSector[4096];
    const block::Status imageRead = ramBlockIndex == 0xFF
        ? block::BLOCK_ERR_INVALID
        : storage::read_logical_sector(ramBlockIndex, 0, imageSector, sizeof(imageSector));
    check(repeatStable && imageRead == block::BLOCK_OK && imageSector[0] == 0x5A &&
          ramdisk::disk_count() == 1,
          "32 repeated rescans preserve one live attachment and its owned backing");
    ramdisk::destroy(ramIndex);
    check(block::get_device(ramBlockIndex) == nullptr && ramdisk::disk_count() == 0,
          "destroy unregisters attached image and releases its owned memory");
    check(!ramdisk::validate_attachment_identity(ramIndex, ramBlockIndex, instanceId),
          "destroyed attachment identity is rejected");
    uint8_t* replacement = new uint8_t[512];
    std::memset(replacement, 0xA6, 512);
    const uint8_t replacementIndex = ramdisk::create_readonly_owned(
        replacement, 512, "replacement.img");
    const ramdisk::RamDisk* replacementDisk = ramdisk::get_disk(replacementIndex);
    check(replacementIndex == ramIndex && replacementDisk &&
          replacementDisk->instanceId != instanceId &&
          !ramdisk::validate_attachment_identity(ramIndex, ramBlockIndex, instanceId),
          "reused RAM-disk slot cannot inherit a stale attachment identity");
    ramdisk::destroy(replacementIndex);

    uint8_t* orphanImage = new uint8_t[512];
    std::memset(orphanImage, 0x3C, 512);
    const uint8_t orphanIndex = ramdisk::create_readonly_owned(
        orphanImage, 512, "orphan.img");
    if (orphanIndex == 0xFF) delete[] orphanImage;
    const ramdisk::RamDisk* orphanDisk = ramdisk::get_disk(orphanIndex);
    const bool orphanWasLive = orphanDisk != nullptr;
    block::init();
    FakeDisk unrelated(512, 128);
    const uint8_t unrelatedIndex = register_fake(unrelated, true);
    ramdisk::destroy(orphanIndex);
    check(orphanWasLive && block::get_device(unrelatedIndex) != nullptr &&
          block::get_device(unrelatedIndex)->type == block::BDEV_ATA_PIO &&
          ramdisk::disk_count() == 0,
          "destroying a stale image cannot unregister a replacement block device");
    unregister_fake(unrelatedIndex, unrelated);

    check(std::all_of(g_fakeDisks, g_fakeDisks + 256,
                      [](FakeDisk* disk) { return disk == nullptr; }),
          "fake-device suite leaves no registered fake callbacks");
    std::printf("Storage manager tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
