// DM2 deterministic storage tests. Every sector comes from FakeDisk memory;
// no host disk device is opened or written.
#include "../kernel/core/include/kernel/block_device.h"
#include "../kernel/core/include/kernel/partition_table.h"
#include "../kernel/core/include/kernel/storage_manager.h"
#include "../kernel/core/include/kernel/disk_initialization.h"
#include "../kernel/core/include/kernel/partition_operations.h"
#include "../kernel/core/include/kernel/disk_manager_model.h"
#include "../kernel/core/include/kernel/ramdisk.h"
#include "../kernel/core/include/kernel/vfs.h"
#include "../kernel/core/include/kernel/ata.h"
#include "../guideXOSBootLoader/guidexOSBootInfo.h"

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
    bool sparse;
    std::vector<uint8_t> sparseMbr;
    std::vector<uint8_t> bytes;
    bool failReads;
    uint64_t failLba;
    uint32_t failReadAtCall;
    bool failFlush;
    uint32_t failFlushAtCall;
    block::Status failFlushStatus;
    uint32_t failWriteAtCall1;
    uint32_t failWriteAtCall2;
    uint64_t corruptWriteLbaOnce;
    bool corruptWritePending;
    uint32_t reads;
    uint32_t writes;
    uint32_t writeAttempts;
    uint32_t flushes;
    uint8_t registryIndex;
    uint64_t registrationId;
    bool removeOnVerifyRead;
    bool removeOnFlush;
    uint32_t removeOnFlushAt;
    bool removed;

    FakeDisk(uint32_t size, uint64_t count, bool allocate = true)
        : sectorSize(size), sectorCount(count), driverId(0), sparse(!allocate),
          sparseMbr(static_cast<size_t>(size), 0),
          bytes(allocate ? static_cast<size_t>(size) * static_cast<size_t>(count) : 0, 0),
          failReads(false), failLba(UINT64_MAX), failReadAtCall(0), failFlush(false),
          failFlushAtCall(0), failFlushStatus(block::BLOCK_ERR_IO),
          failWriteAtCall1(0), failWriteAtCall2(0),
          corruptWriteLbaOnce(UINT64_MAX), corruptWritePending(false),
          reads(0), writes(0), writeAttempts(0), flushes(0),
          registryIndex(0xFF), registrationId(0), removeOnVerifyRead(false), removeOnFlush(false), removeOnFlushAt(0),
          removed(false) {}
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
    if (disk->removeOnVerifyRead && lba == 1 &&
        disk->bytes.size() >= disk->sectorSize * 2 &&
        disk->bytes[disk->sectorSize] != 0) {
        disk->removed = block::mark_device_offline(
            disk->registryIndex, disk->registrationId);
        return block::BLOCK_ERR_NO_MEDIA;
    }
    if (disk->sparse) {
        std::memset(buffer, 0, static_cast<size_t>(count) * disk->sectorSize);
        if (lba == 0 && count == 1)
            std::memcpy(buffer, disk->sparseMbr.data(), disk->sectorSize);
        ++disk->reads;
        return block::BLOCK_OK;
    }
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
    if (disk->sparse) {
        if (lba != 0 || count != 1) return block::BLOCK_ERR_IO;
        std::memcpy(disk->sparseMbr.data(), buffer, disk->sectorSize);
        ++disk->writes;
        return block::BLOCK_OK;
    }
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
    if (disk->removeOnFlush &&
        (disk->removeOnFlushAt == 0 || disk->flushes == disk->removeOnFlushAt)) {
        disk->removed = block::mark_device_offline(
            disk->registryIndex, disk->registrationId);
        return block::BLOCK_ERR_NO_MEDIA;
    }
    return disk->failFlush ||
        (disk->failFlushAtCall != 0 && disk->flushes == disk->failFlushAtCall)
        ? disk->failFlushStatus : block::BLOCK_OK;
}

uint8_t register_fake(FakeDisk& disk, bool writable = false,
                      bool withFlush = false, bool flushKnown = false,
                      bool durableCompletion = false,
                      uint16_t requiredAlignment = 0,
                      uint32_t maxTransferBytes = 0,
                      block::BootProvenance boot = block::BOOT_PROVENANCE_DEFINITELY_NOT_BOOT,
                      block::DeviceType transport = block::BDEV_ATA_PIO,
                      bool pciLocationValid = false, uint32_t pciSegment = 0,
                      uint8_t pciBus = 0, uint8_t pciDevice = 0,
                      uint8_t pciFunction = 0, bool ataTargetValid = false,
                      uint8_t ataChannel = 0, uint8_t ataTarget = 0,
                      uint32_t namespaceId = 0)
{
    disk.driverId = static_cast<uint8_t>(g_nextDriverId++);
    g_fakeDisks[disk.driverId] = &disk;
    block::BlockDevice descriptor = {};
    descriptor.active = true;
    descriptor.type = transport;
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
    descriptor.pciLocationValid = pciLocationValid;
    descriptor.pciSegment = pciSegment;
    descriptor.pciBus = pciBus;
    descriptor.pciDevice = pciDevice;
    descriptor.pciFunction = pciFunction;
    descriptor.ataTargetValid = ataTargetValid;
    descriptor.ataChannel = ataChannel;
    descriptor.ataTarget = ataTarget;
    descriptor.namespaceId = namespaceId;
    const uint8_t index = block::register_device(descriptor);
    if (index != 0xFF) {
        disk.registryIndex = index;
        block::BlockDevice registered = {};
        if (block::copy_device(index, registered))
            disk.registrationId = registered.registrationId;
    }
    return index;
}

bool unregister_fake(uint8_t index, FakeDisk& disk)
{
    if (!block::unregister_device(index)) return false;
    g_fakeDisks[disk.driverId] = nullptr;
    return true;
}

struct AtaFlushFakeIo {
    uint16_t ioBase;
    uint8_t statuses[16];
    size_t statusCount;
    size_t statusIndex;
    uint16_t writePorts[8];
    uint8_t writeValues[8];
    size_t writeCount;
};

uint8_t fake_ata_read8(void* context, uint16_t port)
{
    AtaFlushFakeIo* io = static_cast<AtaFlushFakeIo*>(context);
    if (port == static_cast<uint16_t>(io->ioBase + ata::ATA_REG_STATUS)) {
        const size_t index = io->statusIndex < io->statusCount
            ? io->statusIndex++ : io->statusCount - 1;
        return io->statuses[index];
    }
    return 0;
}

void fake_ata_write8(void* context, uint16_t port, uint8_t value)
{
    AtaFlushFakeIo* io = static_cast<AtaFlushFakeIo*>(context);
    if (io->writeCount < 8) {
        io->writePorts[io->writeCount] = port;
        io->writeValues[io->writeCount] = value;
        ++io->writeCount;
    }
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
    CopiesDisagree,
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
    if (fixture == GptFixture::CopiesDisagree) {
        write_gpt_header(disk, backupHeaderLba, 1, firstUsable + 1, lastUsable,
                         backupArrayLba, entryCount, entriesCrc);
    }

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

void build_empty_gpt(FakeDisk& disk)
{
    std::fill(disk.bytes.begin(), disk.bytes.end(), 0);
    const uint32_t entryCount = 128;
    const uint64_t arrayBytes = static_cast<uint64_t>(entryCount) * 128;
    const uint64_t arraySectors = (arrayBytes + disk.sectorSize - 1) / disk.sectorSize;
    const uint64_t backupHeaderLba = disk.sectorCount - 1;
    const uint64_t backupArrayLba = backupHeaderLba - arraySectors;
    const uint64_t firstUsable = 2 + arraySectors;
    const uint64_t lastUsable = backupArrayLba - 1;
    std::vector<uint8_t> entries(static_cast<size_t>(arrayBytes), 0);
    const uint32_t entriesCrc = storage::crc32(entries.data(), entries.size());
    for (uint64_t i = 0; i < arraySectors; ++i) {
        const size_t offset = static_cast<size_t>(i * disk.sectorSize);
        const size_t amount = entries.size() - offset < disk.sectorSize
            ? entries.size() - offset : disk.sectorSize;
        std::memcpy(sector(disk, 2 + i), entries.data() + offset, amount);
        std::memcpy(sector(disk, backupArrayLba + i), entries.data() + offset, amount);
    }
    write_gpt_header(disk, 1, backupHeaderLba, firstUsable, lastUsable,
                     2, entryCount, entriesCrc);
    write_gpt_header(disk, backupHeaderLba, 1, firstUsable, lastUsable,
                     backupArrayLba, entryCount, entriesCrc);
    set_mbr_signature(disk);
    set_mbr_partition(disk, 0, 0, 0xEE, 1,
        disk.sectorCount - 1 > 0xFFFFFFFFull
            ? 0xFFFFFFFFu : static_cast<uint32_t>(disk.sectorCount - 1));
}

uint16_t read_u16(const uint8_t* p);
uint32_t read_u32(const uint8_t* p);
uint64_t read_u64(const uint8_t* p);

void build_full_gpt(FakeDisk& disk)
{
    std::fill(disk.bytes.begin(), disk.bytes.end(), 0);
    const uint32_t entryCount = 128;
    const uint32_t entrySize = 128;
    const uint64_t arrayBytes = static_cast<uint64_t>(entryCount) * entrySize;
    const uint64_t arraySectors = arrayBytes / disk.sectorSize;
    const uint64_t backupHeaderLba = disk.sectorCount - 1;
    const uint64_t backupArrayLba = backupHeaderLba - arraySectors;
    const uint64_t firstUsable = 2 + arraySectors;
    const uint64_t lastUsable = backupArrayLba - 1;
    std::vector<uint8_t> entries(static_cast<size_t>(arrayBytes), 0);
    for (uint32_t i = 0; i < entryCount; ++i) {
        uint8_t* entry = entries.data() + static_cast<size_t>(i) * entrySize;
        entry[0] = 0x28;
        entry[16] = static_cast<uint8_t>(i + 1);
        entry[17] = 0x53;
        const uint64_t start = firstUsable + i * 2;
        write_u64(entry + 32, start);
        write_u64(entry + 40, start);
    }
    const uint32_t entriesCrc = storage::crc32(entries.data(), entries.size());
    for (uint64_t i = 0; i < arraySectors; ++i) {
        const size_t offset = static_cast<size_t>(i * disk.sectorSize);
        const size_t amount = entries.size() - offset < disk.sectorSize
            ? entries.size() - offset : disk.sectorSize;
        std::memcpy(sector(disk, 2 + i), entries.data() + offset, amount);
        std::memcpy(sector(disk, backupArrayLba + i), entries.data() + offset, amount);
    }
    write_gpt_header(disk, 1, backupHeaderLba, firstUsable, lastUsable,
                     2, entryCount, entriesCrc);
    write_gpt_header(disk, backupHeaderLba, 1, firstUsable, lastUsable,
                     backupArrayLba, entryCount, entriesCrc);
    set_mbr_signature(disk);
    set_mbr_partition(disk, 0, 0, 0xEE, 1,
        disk.sectorCount - 1 > 0xFFFFFFFFull
            ? 0xFFFFFFFFu : static_cast<uint32_t>(disk.sectorCount - 1));
}

bool current_regions(uint8_t index, FakeDisk& disk,
                     storage::UnallocatedRegion* regions,
                     uint16_t& count)
{
    storage::PartitionTableModel table = {};
    return storage::parse_partition_table(index, table) &&
        storage::compute_unallocated_regions(table, disk.sectorCount,
            disk.sectorSize, regions, storage::MAX_UNALLOCATED_REGIONS, count);
}

storage::CreatePartitionRequest make_create_request(
    uint8_t index, storage::PartitionScheme scheme,
    const storage::UnallocatedRegion& region, uint64_t sizeBytes,
    bool maximum, uint8_t guidSeed = 0x91)
{
    storage::CreatePartitionRequest request = {};
    storage::capture_target_identity(index, request.targetSnapshot);
    request.requestedScheme = scheme;
    request.selectedRegion = region;
    request.requestedSizeBytes = sizeBytes;
    request.useMaximumSize = maximum;
    request.partitionType = scheme == storage::PARTITION_SCHEME_GPT
        ? storage::CREATE_PARTITION_GPT_BASIC_DATA
        : storage::CREATE_PARTITION_MBR_FAT32_LBA;
    std::strncpy(request.gptName, "New Volume", sizeof(request.gptName) - 1);
    request.expectedRegistryGeneration = request.targetSnapshot.registryGeneration;
    request.testGuidProvided = true;
    for (uint8_t i = 0; i < 16; ++i)
        request.testUniqueGuid[i] = static_cast<uint8_t>(guidSeed + i * 3);
    return request;
}

uint32_t independent_crc32(const uint8_t* bytes, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return crc ^ 0xFFFFFFFFu;
}

bool independent_verify_gpt_create(const FakeDisk& disk,
                                   const std::vector<uint8_t>& before,
                                   const storage::CreatePartitionResult& result,
                                   const char* expectedName)
{
    static const uint8_t expectedBasicDataGuid[16] = {
        0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
        0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
    };
    const uint8_t* primary = disk.bytes.data() + disk.sectorSize;
    const uint8_t* backup = disk.bytes.data() +
        static_cast<size_t>((disk.sectorCount - 1) * disk.sectorSize);
    const uint32_t headerSize = read_u32(primary + 12);
    const uint32_t entryCount = read_u32(primary + 80);
    const uint32_t entrySize = read_u32(primary + 84);
    const size_t arrayBytes = static_cast<size_t>(entryCount) * entrySize;
    const uint64_t primaryLba = read_u64(primary + 72);
    const uint64_t backupLba = read_u64(backup + 72);
    const size_t arraySectors = (arrayBytes + disk.sectorSize - 1) /
        disk.sectorSize;
    const uint8_t* primaryArray = disk.bytes.data() +
        static_cast<size_t>(primaryLba * disk.sectorSize);
    const uint8_t* backupArray = disk.bytes.data() +
        static_cast<size_t>(backupLba * disk.sectorSize);
    if (headerSize < 92 || headerSize > disk.sectorSize ||
        arrayBytes == 0 || primaryLba == 0 || backupLba == 0 ||
        std::memcmp(primaryArray, backupArray,
                    arraySectors * disk.sectorSize) != 0 ||
        independent_crc32(primaryArray, arrayBytes) != read_u32(primary + 88) ||
        independent_crc32(backupArray, arrayBytes) != read_u32(backup + 88))
        return false;
    std::vector<uint8_t> header(primary, primary + headerSize);
    const uint32_t primaryHeaderCrc = read_u32(primary + 16);
    write_u32(header.data() + 16, 0);
    if (independent_crc32(header.data(), header.size()) != primaryHeaderCrc)
        return false;
    header.assign(backup, backup + read_u32(backup + 12));
    const uint32_t backupHeaderCrc = read_u32(backup + 16);
    write_u32(header.data() + 16, 0);
    if (independent_crc32(header.data(), header.size()) != backupHeaderCrc)
        return false;
    if (std::memcmp(primary + 56, backup + 56, 16) != 0 ||
        std::memcmp(primaryArray, backupArray, arrayBytes) != 0 ||
        result.partitionTableSlot >= entryCount) return false;

    const size_t slot = result.partitionTableSlot;
    const uint8_t* entry = primaryArray + slot * entrySize;
    if (std::memcmp(entry, expectedBasicDataGuid, 16) != 0 ||
        std::memcmp(entry + 16, result.createdPartition.uniqueGuid, 16) != 0 ||
        read_u64(entry + 32) != result.createdPartition.startLba ||
        read_u64(entry + 40) != result.createdPartition.endLba ||
        read_u64(entry + 48) != 0) return false;
    for (size_t byteInArray = 0; byteInArray < arrayBytes; ++byteInArray) {
        if (byteInArray >= slot * entrySize &&
            byteInArray < (slot + 1) * entrySize) continue;
        const uint64_t originalLba = primaryLba + byteInArray / disk.sectorSize;
        const size_t originalOffset = static_cast<size_t>(originalLba * disk.sectorSize) +
            byteInArray % disk.sectorSize;
        const size_t currentOffset = static_cast<size_t>(primaryLba * disk.sectorSize) + byteInArray;
        if (before[originalOffset] != disk.bytes[currentOffset]) return false;
    }
    if (expectedName) {
        for (size_t i = 0; expectedName[i] && i < 36; ++i)
            if (read_u16(entry + 56 + i * 2) !=
                static_cast<uint8_t>(expectedName[i])) return false;
        if (expectedName[std::min<size_t>(std::strlen(expectedName), 36)] != '\0' &&
            std::strlen(expectedName) > 36) return false;
    }
    return std::memcmp(disk.bytes.data(), before.data(), disk.sectorSize) == 0;
}

bool independent_verify_mbr_create(const FakeDisk& disk,
                                   const std::vector<uint8_t>& before,
                                   const storage::CreatePartitionResult& result)
{
    const uint8_t* now = disk.bytes.data();
    if (result.partitionTableSlot >= 4) return false;
    const uint32_t slotOffset = 446 +
        static_cast<uint32_t>(result.partitionTableSlot) * 16;
    for (uint32_t i = 0; i < disk.sectorSize; ++i)
        if (i < slotOffset || i >= slotOffset + 16)
            if (now[i] != before[i]) return false;
    const uint8_t* entry = now + slotOffset;
    return entry[0] == 0 && entry[4] == 0x0C &&
        read_u32(entry + 8) == result.createdPartition.startLba &&
        read_u32(entry + 12) == result.createdPartition.sectorCount &&
        now[510] == 0x55 && now[511] == 0xAA &&
        std::memcmp(now + 440, before.data() + 440, 4) == 0;
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

guideXOS::BootSourceDescriptor make_boot_source(bool nvmePath,
    uint32_t namespaceId = 1, uint8_t ataChannel = 0, uint8_t ataTarget = 0)
{
    guideXOS::BootSourceDescriptor source = {};
    source.Version = guideXOS::GUIDEXOS_BOOT_SOURCE_VERSION;
    source.Size = sizeof(source);
    source.Flags = guideXOS::BOOT_SOURCE_FLAG_VALID |
        guideXOS::BOOT_SOURCE_FLAG_DEVICE_PATH_VALID |
        guideXOS::BOOT_SOURCE_FLAG_PCI_LOCATION_VALID;
    source.PciSegment = 0;
    source.PciBus = 0;
    source.PciDevice = 5;
    source.PciFunction = 0;
    uint16_t offset = 0;
    uint8_t* p = source.DevicePath;
    p[offset++] = 0x02; p[offset++] = 0x01; write_u16(p + offset, 12); offset += 2;
    write_u32(p + offset, 0x0A0341D0u); offset += 4;
    write_u32(p + offset, 0); offset += 4;
    p[offset++] = 0x01; p[offset++] = 0x01; write_u16(p + offset, 6); offset += 2;
    p[offset++] = 0; p[offset++] = source.PciDevice;
    if (nvmePath) {
        p[offset++] = 0x03; p[offset++] = 0x17; write_u16(p + offset, 16); offset += 2;
        write_u32(p + offset, namespaceId); offset += 4;
        for (uint8_t i = 0; i < 8; ++i) p[offset++] = 0;
    } else {
        p[offset++] = 0x03; p[offset++] = 0x01; write_u16(p + offset, 8); offset += 2;
        p[offset++] = ataChannel; p[offset++] = ataTarget;
        write_u16(p + offset, 0); offset += 2;
    }
    p[offset++] = 0x04; p[offset++] = 0x01; write_u16(p + offset, 42); offset += 2;
    write_u32(p + offset, 1); offset += 4;
    write_u64(p + offset, 34); offset += 8;
    write_u64(p + offset, 60); offset += 8;
    for (uint8_t i = 0; i < 16; ++i) p[offset++] = static_cast<uint8_t>(i + 1);
    p[offset++] = 1; p[offset++] = 2;
    p[offset++] = 0x7F; p[offset++] = 0xFF; write_u16(p + offset, 4); offset += 2;
    source.DevicePathLength = offset;
    return source;
}

void fix_bootinfo_checksum(guideXOS::BootInfo& info)
{
    info.HeaderChecksum = 0;
    const uint32_t* words = reinterpret_cast<const uint32_t*>(&info);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < (info.Size / 4u); ++i) sum += words[i];
    info.HeaderChecksum = 0u - sum;
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

uint16_t read_u16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) |
        (static_cast<uint16_t>(p[1]) << 8);
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
    storage::UnallocatedRegion fourKnGaps[storage::MAX_UNALLOCATED_REGIONS] = {};
    uint16_t fourKnGapCount = 0;
    check(storage::compute_unallocated_regions(model, fourKnMbr.sectorCount,
              fourKnMbr.sectorSize, fourKnGaps,
              storage::MAX_UNALLOCATED_REGIONS, fourKnGapCount) &&
          fourKnGapCount == 2 && fourKnGaps[0].startLba == 1 &&
          fourKnGaps[0].endLba == 3 && fourKnGaps[0].capacityBytes == 3u * 4096u &&
          fourKnGaps[0].alignmentSectors == 256 &&
          !fourKnGaps[0].startAlignedTo1MiB,
          "4Kn MBR gaps report correct byte size and 1 MiB alignment");

    FakeDisk fourKnGpt(4096, 256);
    build_gpt(fourKnGpt);
    model = parse_fixture(fourKnGpt, parsed);
    check(parsed && model.state == storage::DISK_STATE_VALID_GPT &&
          model.primaryGptValid && model.backupGptValid,
          "4096-byte logical sector GPT parsed with both copies");

    FakeDisk disagreeingGpt(512, 4096);
    build_gpt(disagreeingGpt, GptFixture::CopiesDisagree);
    model = parse_fixture(disagreeingGpt, parsed);
    storage::UnallocatedRegion disagreeingGaps[storage::MAX_UNALLOCATED_REGIONS] = {};
    uint16_t disagreeingGapCount = 0;
    check(parsed && model.state == storage::DISK_STATE_GPT_DEGRADED &&
          model.primaryGptValid && model.backupGptValid && !model.gptCopiesAgree &&
          !storage::compute_unallocated_regions(model, disagreeingGpt.sectorCount,
              disagreeingGpt.sectorSize, disagreeingGaps,
              storage::MAX_UNALLOCATED_REGIONS, disagreeingGapCount) &&
          disagreeingGapCount == 0,
          "disagreeing valid GPT copies do not produce unallocated-space claims");

    storage::UnallocatedRegion gaps[storage::MAX_UNALLOCATED_REGIONS] = {};
    uint16_t gapCount = 0;
    check(!storage::compute_unallocated_regions(
              parse_fixture(raw, parsed), raw.sectorCount, raw.sectorSize,
              gaps, storage::MAX_UNALLOCATED_REGIONS, gapCount) && gapCount == 0,
          "raw media does not invent validated unallocated regions");

    model = parse_fixture(emptyMbr, parsed);
    check(storage::compute_unallocated_regions(model, emptyMbr.sectorCount,
              emptyMbr.sectorSize, gaps, storage::MAX_UNALLOCATED_REGIONS,
              gapCount) && gapCount == 1 && gaps[0].startLba == 1 &&
          gaps[0].endLba == 127 && gaps[0].sectorCount == 127 &&
          gaps[0].capacityBytes == 127u * 512u,
          "empty valid MBR exposes only sectors after the partition-table sector");

    model = parse_fixture(oneMbr, parsed);
    check(storage::compute_unallocated_regions(model, oneMbr.sectorCount,
              oneMbr.sectorSize, gaps, storage::MAX_UNALLOCATED_REGIONS,
              gapCount) && gapCount == 2 && gaps[0].startLba == 1 &&
          gaps[0].endLba == 7 && gaps[1].startLba == 18 &&
          gaps[1].endLba == 127,
          "MBR gap calculation bounds both sides of the validated partition");

    FakeDisk emptyGpt(512, 4096);
    build_empty_gpt(emptyGpt);
    model = parse_fixture(emptyGpt, parsed);
    check(parsed && model.state == storage::DISK_STATE_VALID_GPT &&
          model.partitionCount == 0 &&
          storage::compute_unallocated_regions(model, emptyGpt.sectorCount,
              emptyGpt.sectorSize, gaps, storage::MAX_UNALLOCATED_REGIONS,
              gapCount) && gapCount == 1 && gaps[0].startLba == 34 &&
          gaps[0].endLba == 4062 && gaps[0].insideUsableRange,
          "empty GPT free extent is clipped to its validated usable LBA range");

    model = parse_fixture(validGpt, parsed);
    check(storage::compute_unallocated_regions(model, validGpt.sectorCount,
              validGpt.sectorSize, gaps, storage::MAX_UNALLOCATED_REGIONS,
              gapCount) && gapCount == 2 && gaps[0].startLba == 34 &&
          gaps[0].endLba == 37 && gaps[1].startLba == 99 &&
          gaps[1].endLba == 4062,
          "populated GPT extents leave metadata outside the usable map");

    model = parse_fixture(badHeader, parsed);
    check(model.state == storage::DISK_STATE_GPT_DEGRADED &&
          storage::compute_unallocated_regions(model, badHeader.sectorCount,
              badHeader.sectorSize, gaps, storage::MAX_UNALLOCATED_REGIONS,
              gapCount) && gapCount == 2,
          "degraded GPT read-only extent model uses its valid backup copy");

    model = parse_fixture(overlapMbr, parsed);
    check(model.state == storage::DISK_STATE_INVALID_PARTITION_TABLE &&
          !storage::compute_unallocated_regions(model, overlapMbr.sectorCount,
              overlapMbr.sectorSize, gaps, storage::MAX_UNALLOCATED_REGIONS,
              gapCount) && gapCount == 0,
          "overlapping MBR entries cannot create false free-space gaps");

    storage::UnallocatedRegion clipped[1] = {};
    model = parse_fixture(validGpt, parsed);
    check(!storage::compute_unallocated_regions(model, validGpt.sectorCount,
              validGpt.sectorSize, clipped, 1, gapCount) && gapCount == 0,
          "unallocated extent output bound fails closed without truncation");

    check(std::strcmp(storage::disk_manager_state_summary(
              storage::DISK_STATE_NOT_INITIALIZED, false, false),
              "Not Initialized") == 0 &&
          std::strcmp(storage::disk_manager_state_summary(
              storage::DISK_STATE_INVALID_PARTITION_TABLE, false, false),
              "Invalid Partition Table") == 0 &&
          std::strcmp(storage::disk_manager_state_summary(
              storage::DISK_STATE_GPT_DEGRADED, true, false),
              "GPT | Primary valid, backup invalid") == 0 &&
          std::strcmp(storage::disk_manager_state_summary(
              storage::DISK_STATE_GPT_DEGRADED, false, true),
              "GPT | Primary invalid, backup valid") == 0 &&
          std::strcmp(storage::disk_manager_state_summary(
              storage::DISK_STATE_UNREADABLE, false, false), "Unreadable") == 0 &&
          std::strcmp(storage::disk_manager_state_summary(
              storage::DISK_STATE_VALID_MBR, false, false), "Online | MBR") == 0 &&
          std::strcmp(storage::disk_manager_state_summary(
              storage::DISK_STATE_VALID_GPT, true, true), "Online | GPT") == 0,
          "state summary separates raw, invalid, and both GPT degraded directions");

    check(std::strcmp(storage::disk_manager_boot_summary(
              storage::BOOT_DEVICE_IS_TARGET), "Boot device; protected") == 0 &&
          std::strcmp(storage::disk_manager_boot_summary(
              storage::BOOT_DEVICE_DEFINITELY_NOT_TARGET),
              "Definitely not boot device") == 0 &&
          std::strcmp(storage::disk_manager_boot_summary(
              storage::BOOT_DEVICE_IDENTITY_UNKNOWN),
              "Boot identity unknown") == 0,
          "boot presentation distinguishes protected, definitely-not-boot, and unknown");

    int dm5WriteActions = 0;
    for (uint8_t action = 0; action < storage::DISK_MANAGER_ACTION_COUNT; ++action)
        if (storage::disk_manager_action_is_write(
                static_cast<storage::DiskManagerAction>(action))) ++dm5WriteActions;
    check(dm5WriteActions == 2 && storage::disk_manager_action_is_write(
              storage::DISK_MANAGER_ACTION_INITIALIZE) &&
          storage::disk_manager_action_is_write(
              storage::DISK_MANAGER_ACTION_CREATE_PARTITION),
          "Initialize Disk and contextual Create Partition are the only storage write actions");
    check(storage::disk_manager_create_partition_action_enabled(
              true, true, true, true, true) &&
          !storage::disk_manager_create_partition_action_enabled(
              true, false, true, true, true) &&
          !storage::disk_manager_create_partition_action_enabled(
              true, true, false, true, true) &&
          !storage::disk_manager_create_partition_action_enabled(
              true, true, true, false, true) &&
          !storage::disk_manager_create_partition_action_enabled(
              true, true, true, true, false),
          "Create Partition is contextual to a selected validated gap and storage preflight");
    check(storage::disk_manager_action_enabled(
              storage::DISK_MANAGER_ACTION_INITIALIZE, true,
              storage::DISK_STATE_NOT_INITIALIZED, true, true) &&
          !storage::disk_manager_action_enabled(
              storage::DISK_MANAGER_ACTION_INITIALIZE, true,
              storage::DISK_STATE_NOT_INITIALIZED, false, true) &&
          !storage::disk_manager_action_enabled(
              storage::DISK_MANAGER_ACTION_INITIALIZE, true,
              storage::DISK_STATE_INVALID_PARTITION_TABLE, true, true) &&
          !storage::disk_manager_action_enabled(
              storage::DISK_MANAGER_ACTION_INITIALIZE, true,
              storage::DISK_STATE_VALID_GPT, true, true),
          "Initialize Disk action is enabled only for storage-approved raw state");
    check(!storage::disk_manager_action_enabled(
              storage::DISK_MANAGER_ACTION_PROPERTIES, false,
              storage::DISK_STATE_UNREADABLE, false, true) &&
          storage::disk_manager_action_enabled(
              storage::DISK_MANAGER_ACTION_REFRESH, false,
              storage::DISK_STATE_UNREADABLE, false, true) &&
          !storage::disk_manager_action_enabled(
              storage::DISK_MANAGER_ACTION_REFRESH, true,
              storage::DISK_STATE_VALID_GPT, false, false),
          "contextual read-only actions require selection and closed dialogs as appropriate");

    check(std::strcmp(storage::disk_manager_mount_summary(
              storage::DEVICE_ROOT_BACKING), "Root backing device") == 0 &&
          std::strcmp(storage::disk_manager_mount_summary(
              storage::DEVICE_MOUNTED), "Mounted on device") == 0 &&
          std::strcmp(storage::disk_manager_mount_summary(
              storage::DEVICE_IDENTITY_UNKNOWN),
              "Mount relationship unknown") == 0,
          "mount presentation stays at device level when partition identity is unknown");

    uint16_t visibleFirst = 0, visibleCount = 0;
    storage::disk_manager_visible_range(0, 0, 5, visibleFirst, visibleCount);
    check(visibleFirst == 0 && visibleCount == 0,
          "bounded list displays a truthful empty state for zero entries");
    storage::disk_manager_visible_range(1, 0, 5, visibleFirst, visibleCount);
    check(visibleFirst == 0 && visibleCount == 1,
          "bounded list displays one item without fabricating rows");
    storage::disk_manager_visible_range(4, 0, 4, visibleFirst, visibleCount);
    check(visibleFirst == 0 && visibleCount == 4,
          "bounded list includes all four entries when they fit");
    storage::disk_manager_visible_range(16, 7, 5, visibleFirst, visibleCount);
    check(visibleFirst == 7 && visibleCount == 5,
          "16-entry device list pages from a stable offset");
    storage::disk_manager_visible_range(42, 30, 8, visibleFirst, visibleCount);
    check(visibleFirst == 30 && visibleCount == 8,
          "32-plus partition pages show bounded rows without dropping the total");
    storage::disk_manager_visible_range(128, 120, 16, visibleFirst, visibleCount);
    check(visibleFirst == 120 && visibleCount == 8,
          "maximum parser partition count remains navigable at the final page");

    storage::PartitionEntry guidBefore = {};
    guidBefore.isGpt = true;
    guidBefore.uniqueGuid[0] = 0x42;
    guidBefore.startLba = 100;
    guidBefore.endLba = 199;
    storage::PartitionEntry guidAfter = guidBefore;
    guidAfter.startLba = 120;
    guidAfter.endLba = 219;
    check(storage::disk_manager_same_partition(guidBefore, guidAfter),
          "GPT selection follows its unique GUID across a moved table row");
    guidAfter.uniqueGuid[0] = 0x43;
    check(!storage::disk_manager_same_partition(guidBefore, guidAfter),
          "GPT selection clears when the unique GUID changes");
    storage::PartitionEntry mbrBefore = {};
    mbrBefore.mbrType = 0x83;
    mbrBefore.startLba = 8;
    mbrBefore.endLba = 17;
    storage::PartitionEntry mbrAfter = mbrBefore;
    check(storage::disk_manager_same_partition(mbrBefore, mbrAfter),
          "MBR selection matches validated location and type identity");
    mbrAfter.bootable = !mbrBefore.bootable;
    check(storage::disk_manager_same_partition(mbrBefore, mbrAfter),
          "MBR selection survives an active-flag change to the same location/type");
    mbrAfter = mbrBefore;
    ++mbrAfter.startLba;
    check(!storage::disk_manager_same_partition(mbrBefore, mbrAfter),
          "MBR selection clears when partition bounds change");
    mbrAfter = mbrBefore;
    mbrAfter.partitionNumber = 2;
    check(!storage::disk_manager_same_partition(mbrBefore, mbrAfter),
          "MBR selection clears when the primary table slot changes");

    storage::UnallocatedRegion regionBefore = {};
    regionBefore.startLba = 1000;
    regionBefore.endLba = 1999;
    storage::UnallocatedRegion regionAfter = regionBefore;
    check(storage::disk_manager_same_region(regionBefore, regionAfter),
          "unallocated-space selection follows exact LBA bounds on refresh");
    ++regionAfter.endLba;
    check(!storage::disk_manager_same_region(regionBefore, regionAfter),
          "unallocated-space selection clears when its bounds change");

    FakeDisk dm5IdentityDisk(512, 128);
    FakeDisk dm5UnrelatedDisk(512, 128);
    const uint8_t dm5IdentityIndex = register_fake(dm5IdentityDisk);
    storage::TargetIdentity oldIdentity = {};
    storage::TargetIdentity refreshedIdentity = {};
    storage::capture_target_identity(dm5IdentityIndex, oldIdentity);
    const uint8_t dm5UnrelatedIndex = register_fake(dm5UnrelatedDisk);
    storage::capture_target_identity(dm5IdentityIndex, refreshedIdentity);
    check(oldIdentity.registryGeneration != refreshedIdentity.registryGeneration &&
          oldIdentity.registrationId == refreshedIdentity.registrationId &&
          storage::disk_manager_same_disk_incarnation(oldIdentity, refreshedIdentity),
          "disk selection survives safe refresh after an unrelated registry change");
    check(unregister_fake(dm5IdentityIndex, dm5IdentityDisk),
          "selection fixture unregisters its original target");
    FakeDisk dm5SlotReplacement(512, 128);
    const uint8_t dm5ReplacementIndex = register_fake(dm5SlotReplacement);
    storage::TargetIdentity dm5ReplacementIdentity = {};
    storage::capture_target_identity(dm5ReplacementIndex, dm5ReplacementIdentity);
    check(dm5ReplacementIndex == dm5IdentityIndex &&
          !storage::disk_manager_same_disk_incarnation(oldIdentity,
                                                        dm5ReplacementIdentity),
          "slot reuse cannot retain stale disk selection");
    check(unregister_fake(dm5ReplacementIndex, dm5SlotReplacement) &&
          unregister_fake(dm5UnrelatedIndex, dm5UnrelatedDisk),
          "identity fixtures release all registered devices");

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
    flushDevice.failFlush = false;
    flushDevice.failFlushAtCall = flushDevice.flushes + 1;
    flushDevice.failFlushStatus = block::BLOCK_ERR_TIMEOUT;
    flush = block::flush_with_result(index);
    check(flush.outcome == block::FLUSH_OUTCOME_FAILED &&
          flush.status == block::BLOCK_ERR_TIMEOUT,
          "transport flush timeout is preserved as a distinct status");
    unregister_fake(index, flushDevice);

    FakeDisk unknownFlushSemantics(512, 128);
    index = register_fake(unknownFlushSemantics, true, true, false);
    storage::DeviceCapabilities mismatchCapabilities;
    flush = block::flush_with_result(index);
    check(storage::query_device_capabilities(index, mismatchCapabilities) &&
          mismatchCapabilities.flushSupported &&
          mismatchCapabilities.persistence == storage::PERSISTENCE_UNKNOWN &&
          flush.outcome == block::FLUSH_OUTCOME_SUPPORTED_SUCCEEDED &&
          !flush.semanticsKnown,
          "a working callback without trusted semantics stays durability-unknown");
    unregister_fake(index, unknownFlushSemantics);
    FakeDisk staleFlushCapability(512, 128);
    index = register_fake(staleFlushCapability, true, false, true);
    flush = block::flush_with_result(index);
    check(storage::query_device_capabilities(index, mismatchCapabilities) &&
          !mismatchCapabilities.flushSupported &&
          mismatchCapabilities.persistence == storage::PERSISTENCE_UNKNOWN &&
          flush.outcome == block::FLUSH_OUTCOME_UNSUPPORTED_UNKNOWN,
          "flush semantics metadata without a callback cannot claim flush support");
    unregister_fake(index, staleFlushCapability);
    FakeDisk contradictoryDurability(512, 128);
    index = register_fake(contradictoryDurability, true, true, true, true);
    flush = block::flush_with_result(index);
    check(storage::query_device_capabilities(index, mismatchCapabilities) &&
          mismatchCapabilities.persistence == storage::PERSISTENCE_UNKNOWN &&
          flush.outcome == block::FLUSH_OUTCOME_SUPPORTED_SUCCEEDED &&
          !flush.semanticsKnown,
          "durable-write and explicit-flush metadata cannot contradict each other into eligibility");
    unregister_fake(index, contradictoryDurability);

    FakeDisk nvmeNoFlush(512, 4096);
    index = register_fake(nvmeNoFlush, true, false, false, false, 0, 0,
        block::BOOT_PROVENANCE_DEFINITELY_NOT_BOOT, block::BDEV_NVME);
    storage::TargetIdentity nvmeNoFlushIdentity;
    storage::capture_target_identity(index, nvmeNoFlushIdentity);
    storage::InitializeTargetValidation nvmeNoFlushValidation;
    flush = block::flush_with_result(index);
    check(flush.outcome == block::FLUSH_OUTCOME_UNSUPPORTED_UNKNOWN &&
          storage::query_device_capabilities(index, mismatchCapabilities) &&
          mismatchCapabilities.persistence == storage::PERSISTENCE_UNKNOWN &&
          storage::probe_initialize_target(nvmeNoFlushIdentity,
              storage::PARTITION_SCHEME_GPT, nvmeNoFlushValidation) ==
              storage::INITIALIZE_DISK_DURABILITY_UNKNOWN,
          "NVMe without a proven Flush command remains blocked by the common preflight engine");
    unregister_fake(index, nvmeNoFlush);

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

    AtaFlushFakeIo ataIo = {};
    ataIo.ioBase = ata::ATA_PRIMARY_IO;
    ataIo.statuses[0] = ata::ATA_SR_DRDY;
    ataIo.statuses[1] = ata::ATA_SR_DRDY;
    ataIo.statusCount = 2;
    ata::AtaFlushIoOps ataOps = { &ataIo, fake_ata_read8, fake_ata_write8 };
    check(ata::ata_flush_command_with_io(ataIo.ioBase, ata::ATA_PRIMARY_CTRL,
              true, true, true, ataOps, 4) == block::BLOCK_OK &&
          ataIo.writeCount == 2 && ataIo.writeValues[0] == 0xA0 &&
          ataIo.writeValues[1] == ata::ATA_CMD_CACHE_FLUSH_EXT,
          "ATA flush selects the device, issues FLUSH CACHE EXT, and waits for ready completion");
    ataIo = {};
    ataIo.ioBase = ata::ATA_PRIMARY_IO;
    ataIo.statuses[0] = ata::ATA_SR_DRDY;
    ataIo.statuses[1] = ata::ATA_SR_DRDY | ata::ATA_SR_ERR;
    ataIo.statusCount = 2;
    ataOps.context = &ataIo;
    check(ata::ata_flush_command_with_io(ataIo.ioBase, ata::ATA_PRIMARY_CTRL,
              false, true, false, ataOps, 4) == block::BLOCK_ERR_IO &&
          ataIo.writeValues[0] == 0xB0 &&
          ataIo.writeValues[1] == ata::ATA_CMD_CACHE_FLUSH,
          "ATA flush reports ERR after FLUSH CACHE and uses the supported opcode");
    ataIo = {};
    ataIo.ioBase = ata::ATA_PRIMARY_IO;
    ataIo.statuses[0] = ata::ATA_SR_BSY;
    ataIo.statusCount = 1;
    ataOps.context = &ataIo;
    check(ata::ata_flush_command_with_io(ataIo.ioBase, ata::ATA_PRIMARY_CTRL,
              true, true, false, ataOps, 3) == block::BLOCK_ERR_TIMEOUT &&
          ataIo.writeCount == 1,
          "ATA flush times out before issuing a command when the device is not ready");
    ataIo = {};
    ataIo.ioBase = ata::ATA_PRIMARY_IO;
    ataOps.context = &ataIo;
    check(ata::ata_flush_command_with_io(ataIo.ioBase, ata::ATA_PRIMARY_CTRL,
              true, false, false, ataOps, 3) == block::BLOCK_ERR_UNSUPPORTED &&
          ataIo.writeCount == 0,
          "ATA flush is unsupported when IDENTIFY advertises neither flush command");
    const ata::AtaFlushSupport flushCacheOnly =
        ata::ata_flush_support_from_identify_word83(0x5000u);
    const ata::AtaFlushSupport flushExtOnly =
        ata::ata_flush_support_from_identify_word83(0x6000u);
    const ata::AtaFlushSupport invalidWord83 =
        ata::ata_flush_support_from_identify_word83(0x9000u);
    check(flushCacheOnly.flushCache && !flushCacheOnly.flushCacheExt &&
          !flushExtOnly.flushCache && flushExtOnly.flushCacheExt &&
          !invalidWord83.flushCache && !invalidWord83.flushCacheExt,
          "ATA cache-flush support is accepted only from valid IDENTIFY word 83 bits");

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
    guideXOS::BootSourceDescriptor nvmeBootPath = make_boot_source(true, 1);
    check(storage::set_boot_source_descriptor(&nvmeBootPath),
          "valid bounded UEFI NVMe device path is accepted");
    FakeDisk nvmeBootDisk(512, 128);
    const uint8_t nvmeBootIndex = register_fake(nvmeBootDisk, true, true, true,
        false, 0, 0, block::BOOT_PROVENANCE_UNKNOWN, block::BDEV_NVME,
        true, 0, 0, 5, 0, false, 0, 0, 1);
    storage::TargetIdentity nvmeBootIdentity;
    storage::capture_target_identity(nvmeBootIndex, nvmeBootIdentity);
    check(storage::query_boot_protection(nvmeBootIdentity).safety ==
              storage::BOOT_DEVICE_IS_TARGET,
          "matching controller and namespace protect the whole boot disk despite a partition node");

    FakeDisk nvmeOtherNamespace(512, 128);
    const uint8_t nvmeOtherIndex = register_fake(nvmeOtherNamespace, true,
        true, true, false, 0, 0, block::BOOT_PROVENANCE_UNKNOWN,
        block::BDEV_NVME, true, 0, 0, 5, 0, false, 0, 0, 2);
    storage::TargetIdentity nvmeOtherIdentity;
    storage::capture_target_identity(nvmeOtherIndex, nvmeOtherIdentity);
    check(storage::query_boot_protection(nvmeOtherIdentity).safety ==
              storage::BOOT_DEVICE_DEFINITELY_NOT_TARGET,
          "same NVMe controller with a different namespace is definitely not the boot namespace");

    FakeDisk sameCapacityOtherPci(512, 128);
    const uint8_t otherPciIndex = register_fake(sameCapacityOtherPci, true,
        true, true, false, 0, 0, block::BOOT_PROVENANCE_UNKNOWN,
        block::BDEV_NVME, true, 0, 0, 6, 0, false, 0, 0, 1);
    storage::TargetIdentity otherPciIdentity;
    storage::capture_target_identity(otherPciIndex, otherPciIdentity);
    check(storage::query_boot_protection(otherPciIdentity).safety ==
              storage::BOOT_DEVICE_DEFINITELY_NOT_TARGET,
          "same-capacity device at a different PCI BDF is definitely not boot backing");

    guideXOS::BootSourceDescriptor atapiBootPath =
        make_boot_source(false, 0, 1, 1);
    check(storage::set_boot_source_descriptor(&atapiBootPath),
          "valid UEFI ATAPI path is accepted for ATA target matching");
    FakeDisk ataBootDisk(512, 128);
    const uint8_t ataBootIndex = register_fake(ataBootDisk, true, true, true,
        false, 0, 0, block::BOOT_PROVENANCE_UNKNOWN, block::BDEV_ATA_PIO,
        true, 0, 0, 5, 0, true, 1, 1);
    storage::TargetIdentity ataBootIdentity;
    storage::capture_target_identity(ataBootIndex, ataBootIdentity);
    FakeDisk ataOtherTarget(512, 128);
    const uint8_t ataOtherIndex = register_fake(ataOtherTarget, true, true, true,
        false, 0, 0, block::BOOT_PROVENANCE_UNKNOWN, block::BDEV_ATA_PIO,
        true, 0, 0, 5, 0, true, 1, 0);
    storage::TargetIdentity ataOtherIdentity;
    storage::capture_target_identity(ataOtherIndex, ataOtherIdentity);
    storage::capture_target_identity(ataBootIndex, ataBootIdentity);
    storage::capture_target_identity(nvmeBootIndex, nvmeBootIdentity);
    check(storage::query_boot_protection(ataBootIdentity).safety ==
              storage::BOOT_DEVICE_IS_TARGET &&
          storage::query_boot_protection(ataOtherIdentity).safety ==
              storage::BOOT_DEVICE_DEFINITELY_NOT_TARGET,
          "same ATA controller distinguishes the boot channel/target from another device");

    storage::capture_target_identity(nvmeBootIndex, nvmeBootIdentity);
    guideXOS::BootSourceDescriptor truncatedPath = nvmeBootPath;
    truncatedPath.DevicePathLength = guideXOS::GUIDEXOS_BOOT_SOURCE_PATH_MAX + 1;
    check(!storage::set_boot_source_descriptor(&truncatedPath) &&
          storage::query_boot_protection(nvmeBootIdentity).safety ==
              storage::BOOT_DEVICE_IDENTITY_UNKNOWN,
          "truncated device-path provenance is rejected and remains Unknown");
    guideXOS::BootSourceDescriptor malformedPath = nvmeBootPath;
    malformedPath.DevicePath[2] = 0;
    malformedPath.DevicePath[3] = 0;
    check(!storage::set_boot_source_descriptor(&malformedPath) &&
          storage::query_boot_protection(nvmeBootIdentity).safety ==
              storage::BOOT_DEVICE_IDENTITY_UNKNOWN,
          "malformed device-path provenance is rejected and remains Unknown");
    storage::initialize_boot_source(nullptr);
    check(storage::query_boot_protection(nvmeBootIdentity).safety ==
              storage::BOOT_DEVICE_IDENTITY_UNKNOWN,
          "missing BootInfo provenance does not infer DefinitelyNotBoot");
    guideXOS::BootInfo legacyBootInfo = {};
    legacyBootInfo.Magic = guideXOS::GUIDEXOS_BOOTINFO_MAGIC;
    legacyBootInfo.Version = guideXOS::GUIDEXOS_BOOTINFO_LEGACY_VERSION;
    legacyBootInfo.Size = guideXOS::GUIDEXOS_BOOTINFO_LEGACY_SIZE;
    fix_bootinfo_checksum(legacyBootInfo);
    check(guideXOS::guidexos_bootinfo_checksum_valid(&legacyBootInfo),
          "legacy BootInfo v2 checksum and exact old size remain valid");
    storage::initialize_boot_source(&legacyBootInfo);
    check(storage::query_boot_protection(nvmeBootIdentity).safety ==
              storage::BOOT_DEVICE_IDENTITY_UNKNOWN,
          "legacy BootInfo without a descriptor falls back to Unknown");
    guideXOS::BootInfo currentBootInfo = {};
    currentBootInfo.Magic = guideXOS::GUIDEXOS_BOOTINFO_MAGIC;
    currentBootInfo.Version = guideXOS::GUIDEXOS_BOOTINFO_VERSION;
    currentBootInfo.Size = sizeof(currentBootInfo);
    currentBootInfo.BootSource = nvmeBootPath;
    fix_bootinfo_checksum(currentBootInfo);
    check(guideXOS::guidexos_bootinfo_checksum_valid(&currentBootInfo),
          "current BootInfo v3 checksum includes its bounded boot descriptor");
    storage::initialize_boot_source(&currentBootInfo);
    check(storage::query_boot_protection(nvmeBootIdentity).safety ==
              storage::BOOT_DEVICE_IS_TARGET,
          "kernel copies a valid BootInfo v3 descriptor before matching storage");
    storage::set_boot_source_descriptor(&nvmeBootPath);

    storage::TargetIdentity oldProvenanceIdentity = nvmeBootIdentity;
    check(unregister_fake(nvmeBootIndex, nvmeBootDisk),
          "boot provenance test device unregisters when no lease pins it");
    FakeDisk reusedProvenanceSlot(512, 128);
    const uint8_t reusedIndex = register_fake(reusedProvenanceSlot, true,
        true, true, false, 0, 0, block::BOOT_PROVENANCE_UNKNOWN,
        block::BDEV_NVME, true, 0, 0, 5, 0, false, 0, 0, 1);
    storage::TargetIdentity reusedIdentity;
    storage::capture_target_identity(reusedIndex, reusedIdentity);
    check(reusedIndex == oldProvenanceIdentity.globalIndex &&
          reusedIdentity.registrationId != oldProvenanceIdentity.registrationId &&
          !storage::target_identities_equal(oldProvenanceIdentity, reusedIdentity) &&
          storage::revalidate_target_identity(oldProvenanceIdentity) != storage::TARGET_VALID,
          "registry slot reuse after provenance matching rejects the old registration identity");
    unregister_fake(reusedIndex, reusedProvenanceSlot);
    unregister_fake(nvmeOtherIndex, nvmeOtherNamespace);
    unregister_fake(otherPciIndex, sameCapacityOtherPci);
    unregister_fake(ataBootIndex, ataBootDisk);
    unregister_fake(ataOtherIndex, ataOtherTarget);

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
    storage::StorageOperationLease leaseA = {};
    storage::StorageOperationLease leaseB = {};
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

    FakeDisk pinnedLeaseDisk(512, 128);
    const uint8_t pinnedLeaseIndex = register_fake(pinnedLeaseDisk, true,
        true, true);
    FakeDisk unrelatedLeaseDisk(512, 128);
    const uint8_t unrelatedLeaseIndex = register_fake(unrelatedLeaseDisk);
    storage::TargetIdentity pinnedLeaseIdentity;
    storage::capture_target_identity(pinnedLeaseIndex, pinnedLeaseIdentity);
    storage::StorageOperationLease pinnedLease = {};
    const bool leaseAcquired = storage::try_acquire_storage_operation(pinnedLease) ==
        storage::STORAGE_OPERATION_LOCK_ACQUIRED;
    const bool targetPinned = leaseAcquired && storage::pin_storage_operation_target(
        pinnedLease, pinnedLeaseIdentity);
    check(targetPinned && !block::unregister_device(pinnedLeaseIndex) &&
          block::get_device(pinnedLeaseIndex) != nullptr,
          "an operation target cannot be unregistered or rebound while its registration is pinned");
    FakeDisk registrationDuringLease(512, 128);
    const uint8_t registrationDuringLeaseIndex = register_fake(registrationDuringLease);
    check(registrationDuringLeaseIndex != 0xFF &&
          unregister_fake(registrationDuringLeaseIndex, registrationDuringLease),
          "unrelated registration remains possible while an operation target is pinned");
    check(unregister_fake(unrelatedLeaseIndex, unrelatedLeaseDisk) &&
          storage::release_storage_operation(pinnedLease) &&
          unregister_fake(pinnedLeaseIndex, pinnedLeaseDisk),
          "unrelated device removal remains available and target removal succeeds after lease release");

    FakeDisk replacedDisk(512, 4096);
    index = register_fake(replacedDisk, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION,
          "identity-revalidation fixture reaches confirmation-ready state");
    check(!unregister_fake(index, replacedDisk) &&
          block::get_device(index) != nullptr,
          "normal unregister refuses a confirmation-held target pin");
    FakeDisk replacementTargetDisk(512, 4096);
    const uint8_t replacementBlockIndex = register_fake(replacementTargetDisk, true, true, true);
    check(storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_REGISTRY_CHANGED &&
          replacedDisk.writeAttempts == 0 && replacementTargetDisk.writeAttempts == 0 &&
          !storage::storage_operation_active(),
          "registry change and device replacement after confirmation abort before writes");
    check(unregister_fake(index, replacedDisk),
          "registry change releases the original target pin before cleanup");
    unregister_fake(replacementBlockIndex, replacementTargetDisk);

    FakeDisk forcedRemovalDisk(512, 4096);
    index = register_fake(forcedRemovalDisk, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    storage::TargetIdentity forcedRemovalIdentity = initializeRequest.targetSnapshot;
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION,
          "forced-removal fixture pins a confirmation-ready target");
    check(block::mark_device_offline(index, forcedRemovalIdentity.registrationId) &&
          block::get_device(index) == nullptr,
          "forced disappearance marks a pinned target offline without exposing it");
    FakeDisk whileOffline(512, 128);
    const uint8_t whileOfflineIndex = register_fake(whileOffline);
    check(whileOfflineIndex != index,
          "offline pinned tombstone prevents registration slot reuse");
    check(storage::execute_initialize_disk(initializePlan, initializeResult) ==
              storage::INITIALIZE_DISK_REGISTRY_CHANGED &&
          forcedRemovalDisk.writeAttempts == 0 &&
          !storage::storage_operation_active(),
          "forced removal after confirmation aborts before writes and releases ownership");
    g_fakeDisks[forcedRemovalDisk.driverId] = nullptr;
    FakeDisk reusedAfterRemoval(512, 128);
    const uint8_t reusedAfterRemovalIndex = register_fake(reusedAfterRemoval);
    storage::TargetIdentity reusedAfterRemovalIdentity;
    storage::capture_target_identity(reusedAfterRemovalIndex,
                                     reusedAfterRemovalIdentity);
    check(reusedAfterRemovalIndex == index &&
          reusedAfterRemovalIdentity.registrationId != forcedRemovalIdentity.registrationId &&
          storage::revalidate_target_identity(forcedRemovalIdentity) != storage::TARGET_VALID,
          "released offline tombstone permits slot reuse with a new incarnation identity");
    unregister_fake(reusedAfterRemovalIndex, reusedAfterRemoval);
    unregister_fake(whileOfflineIndex, whileOffline);

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

    FakeDisk removedDuringVerify(512, 4096);
    index = register_fake(removedDuringVerify, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION,
          "surprise-removal verification fixture reaches confirmation");
    removedDuringVerify.removeOnVerifyRead = true;
    check(storage::execute_initialize_disk(initializePlan, initializeResult) !=
              storage::INITIALIZE_DISK_SUCCESS && removedDuringVerify.removed &&
          initializeResult.finalStateUncertain &&
          !storage::storage_operation_active() && block::get_device(index) == nullptr,
          "removal during verification stops safely, marks state uncertain, and releases the lease");
    g_fakeDisks[removedDuringVerify.driverId] = nullptr;

    FakeDisk removedDuringFlush(512, 4096);
    index = register_fake(removedDuringFlush, true, true, true);
    initializeRequest = make_initialize_request(index, storage::PARTITION_SCHEME_GPT);
    check(storage::prepare_initialize_disk(initializeRequest, initializePlan,
                                           initializeResult) ==
              storage::INITIALIZE_DISK_READY_FOR_CONFIRMATION,
          "surprise-removal flush fixture reaches confirmation");
    removedDuringFlush.removeOnFlush = true;
    removedDuringFlush.removeOnFlushAt = 2;
    const storage::InitializeDiskStatus removedFlushStatus =
        storage::execute_initialize_disk(initializePlan, initializeResult);
    check(removedFlushStatus != storage::INITIALIZE_DISK_SUCCESS && removedDuringFlush.removed &&
          initializeResult.finalStateUncertain &&
          !storage::storage_operation_active() && block::get_device(index) == nullptr,
          "removal during flush stops further writes, reports uncertainty, and releases the lease");
    g_fakeDisks[removedDuringFlush.driverId] = nullptr;

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

    {
        FakeDisk createGpt(512, 16384);
        build_empty_gpt(createGpt);
        index = register_fake(createGpt, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        storage::CreatePartitionProbe probe = {};
        storage::TargetIdentity target = {};
        storage::capture_target_identity(index, target);
        check(current_regions(index, createGpt, regions, regionCount) &&
              regionCount == 1 &&
              storage::probe_create_partition(target,
                  storage::PARTITION_SCHEME_GPT, regions[0], probe) ==
                  storage::CREATE_PARTITION_READY &&
              probe.firstAlignedLba == 2048 &&
              probe.maximumBytes >= 1024u * 1024u,
              "GPT create preflight derives a 1 MiB-aligned maximum from the selected gap");
        std::vector<uint8_t> before = createGpt.bytes;
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_GPT, regions[0], 0, true, 0xA0);
        uint8_t expectedGuid[16];
        for (uint8_t i = 0; i < 16; ++i)
            expectedGuid[i] = static_cast<uint8_t>(0xA0 + i * 3);
        expectedGuid[7] = static_cast<uint8_t>((expectedGuid[7] & 0x0F) | 0x40);
        expectedGuid[8] = static_cast<uint8_t>((expectedGuid[8] & 0x3F) | 0x80);
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_SUCCESS &&
              createResult.verificationPassed &&
              std::memcmp(createResult.createdPartition.uniqueGuid,
                          expectedGuid, sizeof(expectedGuid)) == 0 &&
              createResult.createdPartition.startLba == 2048 &&
              createResult.finalDetectedState == storage::DISK_STATE_VALID_GPT &&
              createResult.finalPartitionCount == 1 &&
              createResult.finalUnallocatedRegionCount == 1 &&
              !storage::storage_operation_active(),
              "GPT maximum-size creation succeeds, verifies, rescans, and releases the lease");
        check(independent_verify_gpt_create(createGpt, before, createResult,
                  "New Volume"),
              "independent GPT bytes verify entry, GUID, bounds, UTF-16 name, CRCs, copy parity, and preservation");
        unregister_fake(index, createGpt);
    }

    {
        FakeDisk unnamedGpt(512, 8192);
        build_empty_gpt(unnamedGpt);
        index = register_fake(unnamedGpt, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, unnamedGpt, regions, regionCount);
        std::vector<uint8_t> before = unnamedGpt.bytes;
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_GPT, regions[0], 0, true, 0xA8);
        request.gptName[0] = '\0';
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_SUCCESS &&
              independent_verify_gpt_create(unnamedGpt, before, createResult, "") &&
              createResult.createdPartition.name[0] == '\0',
              "GPT partition creation accepts an intentionally empty bounded name");
        unregister_fake(index, unnamedGpt);
    }

    {
        FakeDisk guidCollision(512, 16384);
        build_empty_gpt(guidCollision);
        uint8_t* primaryHeader = sector(guidCollision, 1);
        const uint64_t backupHeaderLba = guidCollision.sectorCount - 1;
        const uint64_t primaryArrayLba = read_u64(primaryHeader + 72);
        const uint32_t entryCount = read_u32(primaryHeader + 80);
        const uint32_t entrySize = read_u32(primaryHeader + 84);
        const size_t arrayBytes = static_cast<size_t>(entryCount) * entrySize;
        const size_t arraySectors = (arrayBytes + guidCollision.sectorSize - 1) /
            guidCollision.sectorSize;
        std::vector<uint8_t> entries(arrayBytes, 0);
        uint8_t collisionGuid[16];
        for (uint8_t i = 0; i < 16; ++i)
            collisionGuid[i] = static_cast<uint8_t>(0x30 + i);
        collisionGuid[7] = static_cast<uint8_t>((collisionGuid[7] & 0x0F) | 0x40);
        collisionGuid[8] = static_cast<uint8_t>((collisionGuid[8] & 0x3F) | 0x80);
        std::memcpy(entries.data(), storage::GPT_TYPE_BASIC_DATA_GUID, 16);
        std::memcpy(entries.data() + 16, collisionGuid, sizeof(collisionGuid));
        write_u64(entries.data() + 32, 34);
        write_u64(entries.data() + 40, 2047);
        for (uint8_t i = 0; i < 8; ++i)
            write_u16(entries.data() + 56 + i * 2, "Occupied"[i]);
        const uint32_t entriesCrc = storage::crc32(entries.data(), entries.size());
        const uint64_t backupArrayLba = read_u64(sector(guidCollision,
            backupHeaderLba) + 72);
        for (size_t i = 0; i < arraySectors; ++i) {
            const size_t offset = i * guidCollision.sectorSize;
            const size_t amount = entries.size() - offset < guidCollision.sectorSize
                ? entries.size() - offset : guidCollision.sectorSize;
            std::memcpy(sector(guidCollision, primaryArrayLba + i),
                        entries.data() + offset, amount);
            std::memcpy(sector(guidCollision, backupArrayLba + i),
                        entries.data() + offset, amount);
        }
        const uint64_t firstUsable = read_u64(primaryHeader + 40);
        const uint64_t lastUsable = read_u64(primaryHeader + 48);
        write_gpt_header(guidCollision, 1, backupHeaderLba, firstUsable,
            lastUsable, primaryArrayLba, entryCount, entriesCrc);
        write_gpt_header(guidCollision, backupHeaderLba, 1, firstUsable,
            lastUsable, backupArrayLba, entryCount, entriesCrc);
        index = register_fake(guidCollision, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, guidCollision, regions, regionCount);
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_GPT, regions[regionCount - 1],
            2ull * 1024 * 1024, false, 0xB8);
        std::memcpy(request.testUniqueGuid, collisionGuid,
                    sizeof(request.testUniqueGuid));
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_GUID_COLLISION &&
              guidCollision.writeAttempts == 0 &&
              !storage::storage_operation_active(),
              "GPT creation rejects an injected GUID already used by another entry without writing");
        unregister_fake(index, guidCollision);
    }

    {
        FakeDisk sequentialGpt(512, 16384);
        build_empty_gpt(sequentialGpt);
        index = register_fake(sequentialGpt, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, sequentialGpt, regions, regionCount);
        std::vector<uint8_t> beforeFirst = sequentialGpt.bytes;
        storage::CreatePartitionRequest firstRequest = make_create_request(index,
            storage::PARTITION_SCHEME_GPT, regions[0], 4ull * 1024 * 1024,
            false, 0xA1);
        storage::CreatePartitionResult firstResult = {};
        check(storage::create_partition(firstRequest, firstResult) ==
                  storage::CREATE_PARTITION_SUCCESS &&
              firstResult.createdPartition.startLba == 2048 &&
              firstResult.createdPartition.sectorCount == 8192 &&
              firstResult.createdPartition.endLba == 10239 &&
              independent_verify_gpt_create(sequentialGpt, beforeFirst,
                  firstResult, "New Volume"),
              "GPT partial-gap creation stays within the requested 4 MiB and preserves entry bytes");
        current_regions(index, sequentialGpt, regions, regionCount);
        check(regionCount == 2 && regions[1].startLba == 10240,
              "GPT partial creation recomputes the trailing gap from parser geometry");
        std::vector<uint8_t> beforeSecond = sequentialGpt.bytes;
        storage::CreatePartitionRequest secondRequest = make_create_request(index,
            storage::PARTITION_SCHEME_GPT, regions[1], 2ull * 1024 * 1024,
            false, 0xA2);
        storage::CreatePartitionResult secondResult = {};
        check(storage::create_partition(secondRequest, secondResult) ==
                  storage::CREATE_PARTITION_SUCCESS &&
              secondResult.createdPartition.partitionNumber == 2 &&
              secondResult.finalPartitionCount == 2 &&
              secondResult.finalUnallocatedRegionCount == 2 &&
              independent_verify_gpt_create(sequentialGpt, beforeSecond,
                  secondResult, "New Volume"),
              "sequential GPT creation preserves the first partition and creates a second real entry");
        storage::PartitionTableModel after = {};
        check(storage::parse_partition_table(index, after) &&
              after.state == storage::DISK_STATE_VALID_GPT &&
              after.partitionCount == 2 &&
              after.partitions[0].partitionNumber == 1 &&
              after.partitions[1].partitionNumber == 2 &&
              !storage::storage_operation_active(),
              "multiple GPT creates retain on-disk slots and leave Format unavailable");
        unregister_fake(index, sequentialGpt);
    }

    {
        FakeDisk fourKnGpt(4096, 8192);
        build_empty_gpt(fourKnGpt);
        index = register_fake(fourKnGpt, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, fourKnGpt, regions, regionCount);
        std::vector<uint8_t> before = fourKnGpt.bytes;
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_GPT, regions[0], 3ull * 1024 * 1024,
            false, 0xA3);
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_SUCCESS &&
              createResult.createdPartition.startLba == 256 &&
              createResult.createdPartition.startLba % 256 == 0 &&
              createResult.createdPartition.sectorCount == 768 &&
              independent_verify_gpt_create(fourKnGpt, before, createResult,
                  "New Volume"),
              "4096-byte GPT creation derives 1 MiB alignment as 256 sectors and verifies both copies");
        unregister_fake(index, fourKnGpt);
    }

    {
        FakeDisk sequentialMbr(512, 16384);
        set_mbr_signature(sequentialMbr);
        std::fill(sequentialMbr.bytes.begin(), sequentialMbr.bytes.begin() + 440, 0x6D);
        write_u32(sector(sequentialMbr, 0) + 440, 0x78563412u);
        index = register_fake(sequentialMbr, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, sequentialMbr, regions, regionCount);
        std::vector<uint8_t> beforeFirst = sequentialMbr.bytes;
        storage::CreatePartitionRequest firstRequest = make_create_request(index,
            storage::PARTITION_SCHEME_MBR, regions[0], 2ull * 1024 * 1024,
            false, 0xA4);
        storage::CreatePartitionResult firstResult = {};
        check(storage::create_partition(firstRequest, firstResult) ==
                  storage::CREATE_PARTITION_SUCCESS &&
              firstResult.createdPartition.partitionNumber == 1 &&
              firstResult.createdPartition.startLba == 2048 &&
              independent_verify_mbr_create(sequentialMbr, beforeFirst,
                  firstResult),
              "MBR creation writes an inactive FAT32-LBA entry and preserves boot code/signature");
        current_regions(index, sequentialMbr, regions, regionCount);
        check(regionCount == 2 && regions[1].startLba == 6144,
              "MBR partial creation exposes its exact remaining aligned gap");
        std::vector<uint8_t> beforeSecond = sequentialMbr.bytes;
        storage::CreatePartitionRequest secondRequest = make_create_request(index,
            storage::PARTITION_SCHEME_MBR, regions[1], 2ull * 1024 * 1024,
            false, 0xA5);
        storage::CreatePartitionResult secondResult = {};
        check(storage::create_partition(secondRequest, secondResult) ==
                  storage::CREATE_PARTITION_SUCCESS &&
              secondResult.createdPartition.partitionNumber == 2 &&
              secondResult.finalPartitionCount == 2 &&
              independent_verify_mbr_create(sequentialMbr, beforeSecond,
                  secondResult),
              "second MBR creation preserves the first entry and modifies only its own slot");
        unregister_fake(index, sequentialMbr);
    }

    {
        FakeDisk fourKnMbr(4096, 8192);
        set_mbr_signature(fourKnMbr);
        index = register_fake(fourKnMbr, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, fourKnMbr, regions, regionCount);
        std::vector<uint8_t> before = fourKnMbr.bytes;
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_MBR, regions[0], 2ull * 1024 * 1024,
            false, 0xA6);
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_SUCCESS &&
              createResult.createdPartition.startLba == 256 &&
              createResult.createdPartition.sectorCount == 512 &&
              independent_verify_mbr_create(fourKnMbr, before, createResult),
              "4096-byte MBR creation uses 256-sector alignment and preserves the full logical sector");
        unregister_fake(index, fourKnMbr);
    }

    {
        FakeDisk fullGpt(512, 16384);
        build_full_gpt(fullGpt);
        index = register_fake(fullGpt, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, fullGpt, regions, regionCount);
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_GPT, regions[regionCount - 1], 0, true, 0xA7);
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_NO_GPT_ENTRY &&
              fullGpt.writeAttempts == 0 && !storage::storage_operation_active(),
              "full GPT entry array rejects creation without metadata writes");
        unregister_fake(index, fullGpt);
    }

    {
        FakeDisk fullMbr(512, 16384);
        set_mbr_signature(fullMbr);
        for (uint8_t slot = 0; slot < 4; ++slot)
            set_mbr_partition(fullMbr, slot, 0, 0x0C,
                2048u + static_cast<uint32_t>(slot) * 2048u, 1024u);
        index = register_fake(fullMbr, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, fullMbr, regions, regionCount);
        storage::CreatePartitionProbe probe = {};
        storage::TargetIdentity target = {};
        storage::capture_target_identity(index, target);
        check(storage::probe_create_partition(target, storage::PARTITION_SCHEME_MBR,
                  regions[regionCount - 1], probe) ==
                  storage::CREATE_PARTITION_NO_MBR_ENTRY &&
              fullMbr.writeAttempts == 0,
              "full MBR primary table rejects creation and disables the contextual action");
        unregister_fake(index, fullMbr);
    }

    {
        FakeDisk degraded(512, 4096);
        build_gpt(degraded, GptFixture::PrimaryHeaderCrcBad);
        index = register_fake(degraded, true, true, true);
        storage::PartitionTableModel table = {};
        storage::parse_partition_table(index, table);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        check(storage::compute_unallocated_regions(table, degraded.sectorCount,
                  degraded.sectorSize, regions,
                  storage::MAX_UNALLOCATED_REGIONS, regionCount),
              "degraded GPT read-only regions remain available for inspection");
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_GPT, regions[0], 0, true, 0xA8);
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_GPT_DEGRADED &&
              degraded.writeAttempts == 0,
              "degraded GPT is explicitly rejected without attempting repair or creation");
        unregister_fake(index, degraded);
    }

    {
        FakeDisk conflicting(512, 4096);
        build_gpt(conflicting, GptFixture::CopiesDisagree);
        index = register_fake(conflicting, true, true, true);
        storage::CreatePartitionRequest request = {};
        storage::capture_target_identity(index, request.targetSnapshot);
        request.requestedScheme = storage::PARTITION_SCHEME_GPT;
        request.useMaximumSize = true;
        request.expectedRegistryGeneration = request.targetSnapshot.registryGeneration;
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_GPT_DEGRADED &&
              conflicting.writeAttempts == 0,
              "conflicting valid GPT copies are rejected before writes");
        unregister_fake(index, conflicting);
    }

    {
        FakeDisk extended(512, 16384);
        set_mbr_signature(extended);
        set_mbr_partition(extended, 0, 0, 0x0F, 2048, 4096);
        index = register_fake(extended, true, true, true);
        storage::CreatePartitionRequest request = {};
        storage::capture_target_identity(index, request.targetSnapshot);
        request.requestedScheme = storage::PARTITION_SCHEME_MBR;
        request.useMaximumSize = true;
        request.partitionType = storage::CREATE_PARTITION_MBR_FAT32_LBA;
        request.expectedRegistryGeneration = request.targetSnapshot.registryGeneration;
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_UNSUPPORTED_SCHEME &&
              extended.writeAttempts == 0,
              "extended/logical MBR layouts remain unsupported");
        unregister_fake(index, extended);
    }

    {
        FakeDisk changedGap(512, 16384);
        set_mbr_signature(changedGap);
        index = register_fake(changedGap, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, changedGap, regions, regionCount);
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_MBR, regions[0], 2ull * 1024 * 1024,
            false, 0xA9);
        // Simulate another writer changing the table after the row was selected.
        set_mbr_partition(changedGap, 0, 0, 0x0C, 4096, 1024);
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_STALE_REGION &&
              changedGap.writeAttempts == 0,
              "selected MBR gap bounds are reparsed and stale selections abort without writes");
        unregister_fake(index, changedGap);
    }

    {
        FakeDisk bounds(512, 16384);
        set_mbr_signature(bounds);
        index = register_fake(bounds, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, bounds, regions, regionCount);
        storage::CreatePartitionProbe probe = {};
        storage::TargetIdentity target = {};
        storage::capture_target_identity(index, target);
        storage::probe_create_partition(target, storage::PARTITION_SCHEME_MBR,
            regions[0], probe);
        storage::CreatePartitionRequest overlap = make_create_request(index,
            storage::PARTITION_SCHEME_MBR, regions[0], 2ull * 1024 * 1024,
            false, 0xAA);
        overlap.selectedRegion.startLba = 2048;
        overlap.selectedRegion.endLba = 4095;
        overlap.selectedRegion.sectorCount = 2048;
        overlap.selectedRegion.capacityBytes = 2048ull * 512;
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(overlap, createResult) ==
                  storage::CREATE_PARTITION_STALE_REGION && bounds.writeAttempts == 0,
              "overlap-shaped request cannot authorize space outside the validated gap model");
        storage::CreatePartitionRequest tooSmall = make_create_request(index,
            storage::PARTITION_SCHEME_MBR, regions[0],
            storage::CREATE_PARTITION_MINIMUM_BYTES - 1, false, 0xAB);
        check(storage::create_partition(tooSmall, createResult) ==
                  storage::CREATE_PARTITION_TOO_SMALL && bounds.writeAttempts == 0,
              "sub-minimum partition size is rejected by the storage layer");
        storage::CreatePartitionRequest tooLarge = make_create_request(index,
            storage::PARTITION_SCHEME_MBR, regions[0],
            probe.maximumBytes + 1024u * 1024u, false, 0xAC);
        check(storage::create_partition(tooLarge, createResult) ==
                  storage::CREATE_PARTITION_TOO_LARGE && bounds.writeAttempts == 0,
              "requested size above the revalidated maximum is rejected without rounding up");
        storage::CreatePartitionRequest badType = make_create_request(index,
            storage::PARTITION_SCHEME_MBR, regions[0], 2ull * 1024 * 1024,
            false, 0xAD);
        badType.partitionType = storage::CREATE_PARTITION_GPT_BASIC_DATA;
        check(storage::create_partition(badType, createResult) ==
                  storage::CREATE_PARTITION_UNSUPPORTED_TYPE && bounds.writeAttempts == 0,
              "unsupported partition types fail closed");
        unregister_fake(index, bounds);
    }

    {
        FakeDisk noAlignedSpace(512, 8192);
        set_mbr_signature(noAlignedSpace);
        set_mbr_partition(noAlignedSpace, 0, 0, 0x0C, 2048, 1024);
        index = register_fake(noAlignedSpace, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, noAlignedSpace, regions, regionCount);
        storage::CreatePartitionProbe probe = {};
        storage::TargetIdentity target = {};
        storage::capture_target_identity(index, target);
        check(storage::probe_create_partition(target, storage::PARTITION_SCHEME_MBR,
                  regions[0], probe) == storage::CREATE_PARTITION_NO_ALIGNED_SPACE,
              "gap too short to contain a 1 MiB-aligned partition is unavailable");
        unregister_fake(index, noAlignedSpace);
    }

    {
        FakeDisk mounted(512, 8192);
        set_mbr_signature(mounted);
        index = register_fake(mounted, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, mounted, regions, regionCount);
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_MBR, regions[0], 2ull * 1024 * 1024,
            false, 0xB0);
        storage::CreatePartitionResult createResult = {};
        vfs::set_test_mount(4, true, index, "/data");
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_MOUNTED && mounted.writeAttempts == 0,
              "mounted disk protection is preserved for partition-table writes");
        vfs::clear_test_mounts();
        unregister_fake(index, mounted);
    }

    {
        FakeDisk root(512, 8192);
        set_mbr_signature(root);
        index = register_fake(root, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, root, regions, regionCount);
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_MBR, regions[0], 2ull * 1024 * 1024,
            false, 0xB1);
        storage::CreatePartitionResult createResult = {};
        vfs::set_test_mount(5, true, index, "/");
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_ROOT_BACKING && root.writeAttempts == 0,
              "root backing disk remains protected from partition-table writes");
        vfs::clear_test_mounts();
        unregister_fake(index, root);
    }

    {
        FakeDisk boot(512, 8192);
        set_mbr_signature(boot);
        index = register_fake(boot, true, true, true, false, 0, 0,
            block::BOOT_PROVENANCE_BOOT_BACKING);
        storage::CreatePartitionRequest request = {};
        storage::capture_target_identity(index, request.targetSnapshot);
        request.requestedScheme = storage::PARTITION_SCHEME_MBR;
        request.useMaximumSize = true;
        request.partitionType = storage::CREATE_PARTITION_MBR_FAT32_LBA;
        request.expectedRegistryGeneration = request.targetSnapshot.registryGeneration;
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_BOOT_BACKING && boot.writeAttempts == 0,
              "boot device remains protected from partition-table writes");
        unregister_fake(index, boot);
    }

    {
        FakeDisk bootUnknown(512, 8192);
        set_mbr_signature(bootUnknown);
        index = register_fake(bootUnknown, true, true, true, false, 0, 0,
            block::BOOT_PROVENANCE_UNKNOWN);
        storage::CreatePartitionRequest request = {};
        storage::capture_target_identity(index, request.targetSnapshot);
        request.requestedScheme = storage::PARTITION_SCHEME_MBR;
        request.useMaximumSize = true;
        request.partitionType = storage::CREATE_PARTITION_MBR_FAT32_LBA;
        request.expectedRegistryGeneration = request.targetSnapshot.registryGeneration;
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_BOOT_IDENTITY_UNKNOWN &&
              bootUnknown.writeAttempts == 0,
              "unknown boot provenance fails closed for partition creation");
        unregister_fake(index, bootUnknown);
    }

    {
        FakeDisk readOnly(512, 8192);
        set_mbr_signature(readOnly);
        index = register_fake(readOnly, false, true, true);
        storage::CreatePartitionRequest request = {};
        storage::capture_target_identity(index, request.targetSnapshot);
        request.requestedScheme = storage::PARTITION_SCHEME_MBR;
        request.useMaximumSize = true;
        request.partitionType = storage::CREATE_PARTITION_MBR_FAT32_LBA;
        request.expectedRegistryGeneration = request.targetSnapshot.registryGeneration;
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_READ_ONLY && readOnly.writeAttempts == 0,
              "read-only disk is rejected before partition metadata writes");
        unregister_fake(index, readOnly);
    }

    {
        FakeDisk persistenceUnknown(512, 8192);
        set_mbr_signature(persistenceUnknown);
        index = register_fake(persistenceUnknown, true, false, false);
        storage::CreatePartitionRequest request = {};
        storage::capture_target_identity(index, request.targetSnapshot);
        request.requestedScheme = storage::PARTITION_SCHEME_MBR;
        request.useMaximumSize = true;
        request.partitionType = storage::CREATE_PARTITION_MBR_FAT32_LBA;
        request.expectedRegistryGeneration = request.targetSnapshot.registryGeneration;
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_DURABILITY_UNKNOWN &&
              persistenceUnknown.writeAttempts == 0,
              "unknown persistence cannot pass partition creation preflight");
        unregister_fake(index, persistenceUnknown);
    }

    {
        FakeDisk flushUnknown(512, 8192);
        set_mbr_signature(flushUnknown);
        index = register_fake(flushUnknown, true, true, false);
        storage::CreatePartitionRequest request = {};
        storage::capture_target_identity(index, request.targetSnapshot);
        request.requestedScheme = storage::PARTITION_SCHEME_MBR;
        request.useMaximumSize = true;
        request.partitionType = storage::CREATE_PARTITION_MBR_FAT32_LBA;
        request.expectedRegistryGeneration = request.targetSnapshot.registryGeneration;
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_DURABILITY_UNKNOWN &&
              flushUnknown.writeAttempts == 0,
              "unknown flush semantics reject creation before any write");
        unregister_fake(index, flushUnknown);
    }

    {
        FakeDisk generation(512, 8192);
        set_mbr_signature(generation);
        index = register_fake(generation, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, generation, regions, regionCount);
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_MBR, regions[0], 2ull * 1024 * 1024,
            false, 0xB2);
        FakeDisk unrelated(512, 128);
        const uint8_t unrelatedIndex = register_fake(unrelated);
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_REGISTRY_CHANGED &&
              generation.writeAttempts == 0,
              "registry generation change invalidates the captured target before writes");
        unregister_fake(unrelatedIndex, unrelated);
        unregister_fake(index, generation);
    }

    {
        FakeDisk replaced(512, 8192);
        set_mbr_signature(replaced);
        index = register_fake(replaced, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, replaced, regions, regionCount);
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_MBR, regions[0], 2ull * 1024 * 1024,
            false, 0xB3);
        unregister_fake(index, replaced);
        FakeDisk replacement(512, 8192);
        set_mbr_signature(replacement);
        const uint8_t replacementIndex = register_fake(replacement, true, true, true);
        storage::CreatePartitionResult createResult = {};
        check(replacementIndex == index &&
              storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_REGISTRY_CHANGED &&
              replacement.writeAttempts == 0,
              "slot replacement cannot redirect a stale create request to another disk");
        unregister_fake(replacementIndex, replacement);
    }

    {
        FakeDisk contended(512, 8192);
        set_mbr_signature(contended);
        index = register_fake(contended, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, contended, regions, regionCount);
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_MBR, regions[0], 2ull * 1024 * 1024,
            false, 0xB4);
        storage::StorageOperationLease held = {};
        storage::try_acquire_storage_operation(held);
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_OPERATION_BUSY &&
              contended.writeAttempts == 0 &&
              storage::release_storage_operation(held) &&
              !storage::storage_operation_active(),
              "partition creation shares operation-lock contention and does not start a second lock");
        unregister_fake(index, contended);
    }

    {
        FakeDisk largeMbr(512, 0x100000100ull, false);
        largeMbr.sparseMbr[510] = 0x55;
        largeMbr.sparseMbr[511] = 0xAA;
        index = register_fake(largeMbr, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        check(current_regions(index, largeMbr, regions, regionCount) &&
              regionCount == 1,
              "sparse fake MBR exposes addressable space on media above the 32-bit LBA ceiling");
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_MBR, regions[0], 0, true, 0xB5);
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_SUCCESS &&
              createResult.createdPartition.startLba == 2048 &&
              createResult.createdPartition.endLba == 0xFFFFFFFFull &&
              createResult.createdPartition.sectorCount <= 0xFFFFFFFFull &&
              read_u32(largeMbr.sparseMbr.data() + 446 + 8) == 2048 &&
              read_u32(largeMbr.sparseMbr.data() + 446 + 12) ==
                  createResult.createdPartition.sectorCount,
              "MBR creation clips ranges and sector counts to their 32-bit on-disk limits");
        unregister_fake(index, largeMbr);
    }

    {
        const uint32_t failureCalls[] = {1, 17, 66};
        const char* failureNames[] = {"first", "middle", "final"};
        for (size_t scenario = 0; scenario < 3; ++scenario) {
            FakeDisk failedWrite(512, 16384);
            build_empty_gpt(failedWrite);
            index = register_fake(failedWrite, true, true, true);
            storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
            uint16_t regionCount = 0;
            current_regions(index, failedWrite, regions, regionCount);
            std::vector<uint8_t> before = failedWrite.bytes;
            storage::CreatePartitionRequest request = make_create_request(index,
                storage::PARTITION_SCHEME_GPT, regions[0], 0, true,
                static_cast<uint8_t>(0xC0 + scenario));
            failedWrite.failWriteAtCall1 = failureCalls[scenario];
            storage::CreatePartitionResult createResult = {};
            const storage::CreatePartitionStatus status = storage::create_partition(
                request, createResult);
            char label[128];
            std::snprintf(label, sizeof(label),
                "%s GPT metadata write failure rolls back byte-for-byte and releases the lease",
                failureNames[scenario]);
            check(status == storage::CREATE_PARTITION_IO_FAILED &&
                  createResult.rollbackAttempted && createResult.rollbackSucceeded &&
                  !createResult.finalStateUncertain && failedWrite.bytes == before &&
                  !storage::storage_operation_active(), label);
            unregister_fake(index, failedWrite);
        }
    }

    {
        FakeDisk flushFail(512, 16384);
        build_empty_gpt(flushFail);
        index = register_fake(flushFail, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, flushFail, regions, regionCount);
        std::vector<uint8_t> before = flushFail.bytes;
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_GPT, regions[0], 0, true, 0xC3);
        flushFail.failFlushAtCall = 2;
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_FLUSH_FAILED &&
              createResult.rollbackAttempted && createResult.rollbackSucceeded &&
              flushFail.bytes == before && !storage::storage_operation_active(),
              "post-write flush failure restores and verifies original GPT metadata");
        unregister_fake(index, flushFail);
    }

    {
        FakeDisk corruptVerification(512, 16384);
        build_empty_gpt(corruptVerification);
        index = register_fake(corruptVerification, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, corruptVerification, regions, regionCount);
        std::vector<uint8_t> before = corruptVerification.bytes;
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_GPT, regions[0], 0, true, 0xC4);
        corruptVerification.corruptWriteLbaOnce = 2;
        corruptVerification.corruptWritePending = true;
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_VERIFICATION_FAILED &&
              !createResult.verificationPassed && createResult.rollbackSucceeded &&
              corruptVerification.bytes == before,
              "normal-parser read-back failure triggers verified GPT rollback");
        unregister_fake(index, corruptVerification);
    }

    {
        FakeDisk rollbackFail(512, 16384);
        build_empty_gpt(rollbackFail);
        index = register_fake(rollbackFail, true, true, true);
        storage::UnallocatedRegion regions[storage::MAX_UNALLOCATED_REGIONS] = {};
        uint16_t regionCount = 0;
        current_regions(index, rollbackFail, regions, regionCount);
        storage::CreatePartitionRequest request = make_create_request(index,
            storage::PARTITION_SCHEME_GPT, regions[0], 0, true, 0xC5);
        rollbackFail.failWriteAtCall1 = 1;
        rollbackFail.failWriteAtCall2 = 2;
        storage::CreatePartitionResult createResult = {};
        check(storage::create_partition(request, createResult) ==
                  storage::CREATE_PARTITION_ROLLBACK_FAILED &&
              createResult.rollbackAttempted && !createResult.rollbackSucceeded &&
              createResult.finalStateUncertain &&
              createResult.stage == storage::CREATE_PARTITION_STAGE_STATE_UNCERTAIN &&
              !storage::storage_operation_active(),
              "rollback failure reports uncertain state and releases the operation lease");
        unregister_fake(index, rollbackFail);
    }

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
    storage::TargetIdentity ramLeaseIdentity;
    const bool ramIdentityCaptured = storage::capture_target_identity(
        ramBlockIndex, ramLeaseIdentity);
    storage::StorageOperationLease ramLease = {};
    const bool ramLeasePinned = ramIdentityCaptured &&
        storage::try_acquire_storage_operation(ramLease) ==
            storage::STORAGE_OPERATION_LOCK_ACQUIRED &&
        storage::pin_storage_operation_target(ramLease, ramLeaseIdentity);
    ramdisk::destroy(ramIndex);
    check(ramLeasePinned && ramdisk::get_disk(ramIndex) != nullptr &&
          block::get_device(ramBlockIndex) != nullptr && image[0] == 0x5A,
          "RAM-disk rescan cannot free image memory while a storage lease pins it");
    check(storage::release_storage_operation(ramLease),
          "RAM-disk operation lease releases its registry pin");
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
