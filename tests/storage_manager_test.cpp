// DM2 deterministic storage tests. Every sector comes from FakeDisk memory;
// no host disk device is opened or written.
#include "../kernel/core/include/kernel/block_device.h"
#include "../kernel/core/include/kernel/partition_table.h"
#include "../kernel/core/include/kernel/storage_manager.h"
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
    bool failFlush;
    uint32_t reads;
    uint32_t writes;
    uint32_t flushes;

    FakeDisk(uint32_t size, uint64_t count)
        : sectorSize(size), sectorCount(count), driverId(0),
          bytes(static_cast<size_t>(size) * static_cast<size_t>(count), 0),
          failReads(false), failLba(UINT64_MAX), failFlush(false),
          reads(0), writes(0), flushes(0) {}
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
        lba > disk->sectorCount || count > disk->sectorCount - lba)
        return block::BLOCK_ERR_IO;
    ++disk->reads;
    const size_t offset = static_cast<size_t>(lba * disk->sectorSize);
    const size_t bytes = static_cast<size_t>(count) * disk->sectorSize;
    std::memcpy(buffer, disk->bytes.data() + offset, bytes);
    return block::BLOCK_OK;
}

block::Status fake_write(uint8_t driverId, uint64_t lba, uint32_t count,
                         const void* buffer)
{
    FakeDisk* disk = g_fakeDisks[driverId];
    if (!disk || !buffer || count == 0 || lba > disk->sectorCount ||
        count > disk->sectorCount - lba) return block::BLOCK_ERR_IO;
    ++disk->writes;
    const size_t offset = static_cast<size_t>(lba * disk->sectorSize);
    const size_t bytes = static_cast<size_t>(count) * disk->sectorSize;
    std::memcpy(disk->bytes.data() + offset, buffer, bytes);
    return block::BLOCK_OK;
}

block::Status fake_flush(uint8_t driverId)
{
    FakeDisk* disk = g_fakeDisks[driverId];
    if (!disk) return block::BLOCK_ERR_INVALID;
    ++disk->flushes;
    return disk->failFlush ? block::BLOCK_ERR_IO : block::BLOCK_OK;
}

uint8_t register_fake(FakeDisk& disk, bool writable = false,
                      bool withFlush = false, bool flushKnown = false,
                      bool durableCompletion = false,
                      uint16_t requiredAlignment = 0,
                      uint32_t maxTransferBytes = 0)
{
    disk.driverId = static_cast<uint8_t>(g_nextDriverId++);
    g_fakeDisks[disk.driverId] = &disk;
    block::BlockDevice descriptor = {};
    descriptor.active = true;
    descriptor.type = block::BDEV_ATA_PIO;
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
