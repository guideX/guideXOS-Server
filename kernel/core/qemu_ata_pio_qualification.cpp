// Compile-time-gated QEMU runtime qualification for the production ATA PIO
// block callbacks. Normal kernel builds do not run this sector-writing test.

#include "include/kernel/qemu_ata_pio_qualification.h"

#if defined(GXOS_QEMU_ATA_PIO_QUALIFICATION)

#include "include/kernel/ata.h"
#include "include/kernel/block_device.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace qemu_ata_pio_qualification {
namespace {

static const uint64_t kExpectedTotalSectors = 65536;
static const uint32_t kSectorSize = 512;
static const uint32_t kPatternSectorCount = 4;
static const uint64_t kPatternALba = 4096;
static const uint64_t kPatternBLba = 4100;
static const char kExpectedModel[] = "QEMU HARDDISK";
static const char kExpectedSerial[] = "GXOSNDB1R1TEST01";

static uint8_t s_expectedA[kPatternSectorCount * kSectorSize];
static uint8_t s_expectedB[kPatternSectorCount * kSectorSize];
static uint8_t s_readbackA[kPatternSectorCount * kSectorSize];
static uint8_t s_readbackB[kPatternSectorCount * kSectorSize];
static uint8_t s_boundarySector[kSectorSize];

static bool string_equal(const char* left, const char* right)
{
    if (!left || !right) return false;
    while (*left && *right) {
        if (*left != *right) return false;
        ++left;
        ++right;
    }
    return *left == *right;
}

static void put_u64(uint64_t value)
{
    char digits[21];
    uint8_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value && count < sizeof(digits));
    while (count) serial::putc(digits[--count]);
}

static void report_check(const char* name, bool passed)
{
    serial::puts(passed ? "[NDB1R1-ATA-QUAL] PASS: "
                        : "[NDB1R1-ATA-QUAL] FAIL: ");
    serial::puts(name);
    serial::putc('\n');
}

static bool report_status(const char* name, block::Status actual,
                          block::Status expected)
{
    serial::puts("[NDB1R1-ATA-QUAL] ");
    serial::puts(name);
    serial::puts(" status=0x");
    serial::put_hex8(static_cast<uint8_t>(actual));
    serial::puts(" expected=0x");
    serial::put_hex8(static_cast<uint8_t>(expected));
    serial::putc('\n');
    return actual == expected;
}

static void fill_pattern(uint8_t* output, uint64_t startLba, uint8_t seed)
{
    for (uint32_t sector = 0; sector < kPatternSectorCount; ++sector) {
        for (uint32_t offset = 0; offset < kSectorSize; ++offset) {
            const uint32_t mixed = static_cast<uint32_t>(seed) +
                sector * 61u + offset * 29u + (offset >> 2) +
                static_cast<uint32_t>(startLba + sector);
            output[sector * kSectorSize + offset] =
                static_cast<uint8_t>(mixed);
        }
    }
}

static bool bytes_equal(const uint8_t* left, const uint8_t* right,
                        uint32_t length)
{
    for (uint32_t i = 0; i < length; ++i) {
        if (left[i] != right[i]) return false;
    }
    return true;
}

static bool bytes_are_zero(const uint8_t* bytes, uint32_t length)
{
    for (uint32_t i = 0; i < length; ++i) {
        if (bytes[i] != 0) return false;
    }
    return true;
}

static bool is_qualified_device(const ata::ATADevice* device)
{
    return device && device->active && !device->isAHCI &&
        device->ioBase == 0x170 && device->isMaster != 0 &&
        device->sectorSize == kSectorSize &&
        device->totalSectors == kExpectedTotalSectors &&
        string_equal(device->model, kExpectedModel) &&
        string_equal(device->serial, kExpectedSerial);
}

static void print_identify(uint8_t index, const ata::ATADevice* device)
{
    if (!device) return;
    serial::puts("[NDB1R1-ATA-QUAL] IDENTIFY index=");
    serial::put_hex8(index);
    serial::puts(" name=");
    serial::puts(device->name);
    serial::puts(" model=");
    serial::puts(device->model);
    serial::puts(" serial=");
    serial::puts(device->serial);
    serial::puts(" sectors=");
    put_u64(device->totalSectors);
    serial::puts(" sectorSize=");
    put_u64(device->sectorSize);
    serial::puts(" ioBase=0x");
    serial::put_hex16(device->ioBase);
    serial::puts(" role=");
    serial::puts(device->isMaster ? "master" : "slave");
    serial::puts(" transport=");
    serial::puts(device->isAHCI ? "AHCI" : "PIO");
    serial::puts(" lba48=");
    serial::putc(device->lba48 ? '1' : '0');
    serial::puts(" flushCache=");
    serial::putc(device->flushCache ? '1' : '0');
    serial::puts(" flushCacheExt=");
    serial::putc(device->flushCacheExt ? '1' : '0');
    serial::putc('\n');
}

static bool read_patterns(uint8_t blockIndex)
{
    const block::Status readA = block::read_sectors(
        blockIndex, kPatternALba, kPatternSectorCount, s_readbackA);
    const block::Status readB = block::read_sectors(
        blockIndex, kPatternBLba, kPatternSectorCount, s_readbackB);
    const bool statusA = report_status("PIO read pattern A", readA,
                                       block::BLOCK_OK);
    const bool statusB = report_status("PIO read pattern B", readB,
                                       block::BLOCK_OK);
    if (!statusA || !statusB) return false;
    const bool bytesA = bytes_equal(s_readbackA, s_expectedA,
                                    sizeof(s_expectedA));
    const bool bytesB = bytes_equal(s_readbackB, s_expectedB,
                                    sizeof(s_expectedB));
    report_check("exact byte comparison for pattern A", bytesA);
    report_check("exact byte comparison for pattern B", bytesB);
    return bytesA && bytesB;
}

} // namespace

bool run()
{
    serial::puts("[NDB1R1-ATA-QUAL] START production ATA PIO qualification\n");
    serial::puts("[NDB1R1-ATA-QUAL] expected test identity model=");
    serial::puts(kExpectedModel);
    serial::puts(" serial=");
    serial::puts(kExpectedSerial);
    serial::puts(" sectors=");
    put_u64(kExpectedTotalSectors);
    serial::puts(" rangeA=4096..4099 rangeB=4100..4103\n");

    uint8_t matchingAtaIndex = 0xFF;
    uint8_t matchingCount = 0;
    for (uint8_t index = 0; index < ata::device_count(); ++index) {
        const ata::ATADevice* device = ata::get_device(index);
        print_identify(index, device);
        if (is_qualified_device(device)) {
            matchingAtaIndex = index;
            ++matchingCount;
        }
    }

    if (matchingCount != 1) {
        serial::puts("[NDB1R1-ATA-QUAL] IDENTITY REJECTED: expected exactly "
                     "one matching disposable IDE master; no sector reads or "
                     "writes issued\n");
        return false;
    }

    const ata::ATADevice* ataDevice = ata::get_device(matchingAtaIndex);
    uint8_t blockIndex = 0xFF;
    uint8_t matchingBlockCount = 0;
    for (uint8_t index = 0; index < block::MAX_BLOCK_DEVICES; ++index) {
        const block::BlockDevice* device = block::get_device(index);
        if (device && device->type == block::BDEV_ATA_PIO &&
            device->driverIndex == matchingAtaIndex &&
            string_equal(device->name, ataDevice->name)) {
            blockIndex = index;
            ++matchingBlockCount;
        }
    }

    if (matchingBlockCount != 1) {
        serial::puts("[NDB1R1-ATA-QUAL] FAIL: uniquely identified ATA device "
                     "does not map to exactly one ATA PIO block device; no "
                     "sector writes issued\n");
        return false;
    }

    const block::BlockDevice* blockDevice = block::get_device(blockIndex);
    const bool descriptorMatches = blockDevice &&
        blockDevice->totalSectors == ataDevice->totalSectors &&
        blockDevice->sectorSize == kSectorSize && blockDevice->readFn &&
        blockDevice->writeFn && blockDevice->flushFn;
    report_check("ATA IDENTIFY maps to the expected PIO block callbacks",
                 descriptorMatches);
    if (!descriptorMatches) return false;

    const bool flushExt = ataDevice->lba48 && ataDevice->flushCacheExt;
    const bool flushBasic = ataDevice->flushCache;
    if (!flushExt && !flushBasic) {
        serial::puts("[NDB1R1-ATA-QUAL] FAIL: verified test disk advertises "
                     "no cache flush command; no sector writes issued\n");
        return false;
    }

    serial::puts("[NDB1R1-ATA-QUAL] selected blockIndex=");
    serial::put_hex8(blockIndex);
    serial::puts(" driverIndex=");
    serial::put_hex8(blockDevice->driverIndex);
    serial::puts(" flushCommand=0x");
    serial::put_hex8(flushExt ? 0xEA : 0xE7);
    serial::puts("\n");

    const bool rangesSafe = kPatternALba >= 2048 &&
        kPatternBLba >= kPatternALba + kPatternSectorCount &&
        kPatternBLba + kPatternSectorCount <= blockDevice->totalSectors;
    report_check("test range is in capacity and starts after boot metadata",
                 rangesSafe);
    if (!rangesSafe) return false;

    fill_pattern(s_expectedA, kPatternALba, 0x35);
    fill_pattern(s_expectedB, kPatternBLba, 0xA7);

    // The block layer must reject these out-of-range requests before invoking
    // the ATA callbacks. No out-of-range ATA command is issued.
    const bool invalidReadRejected = block::read_sectors(
        blockIndex, blockDevice->totalSectors, 1, s_boundarySector) ==
        block::BLOCK_ERR_INVALID;
    const bool invalidWriteRejected = block::write_sectors(
        blockIndex, blockDevice->totalSectors, 1, s_boundarySector) ==
        block::BLOCK_ERR_INVALID;
    report_check("out-of-range read is rejected before ATA I/O",
                 invalidReadRejected);
    report_check("out-of-range write is rejected before ATA I/O",
                 invalidWriteRejected);
    if (!invalidReadRejected || !invalidWriteRejected) return false;

    const block::Status initialA = block::read_sectors(
        blockIndex, kPatternALba, kPatternSectorCount, s_readbackA);
    const block::Status initialB = block::read_sectors(
        blockIndex, kPatternBLba, kPatternSectorCount, s_readbackB);
    if (!report_status("initial PIO read range A", initialA, block::BLOCK_OK) ||
        !report_status("initial PIO read range B", initialB, block::BLOCK_OK)) {
        serial::puts("[NDB1R1-ATA-QUAL] FAIL: initial read failed; no sector "
                     "writes issued\n");
        return false;
    }

    const bool alreadyQualified =
        bytes_equal(s_readbackA, s_expectedA, sizeof(s_expectedA)) &&
        bytes_equal(s_readbackB, s_expectedB, sizeof(s_expectedB));
    if (alreadyQualified) {
        serial::puts("[NDB1R1-ATA-QUAL] existing exact patterns found; "
                     "running read-only independent-process verification\n");
        if (read_patterns(blockIndex)) {
            serial::puts("[NDB1R1-ATA-QUAL] CROSS_PROCESS_READBACK PASS "
                         "(this boot issued no writes)\n");
            serial::puts("[NDB1R1-ATA-QUAL] PASS\n");
            return true;
        } else {
            serial::puts("[NDB1R1-ATA-QUAL] FAIL: independent-process "
                         "readback mismatch\n");
            return false;
        }
    }

    const bool initiallyBlank =
        bytes_are_zero(s_readbackA, sizeof(s_readbackA)) &&
        bytes_are_zero(s_readbackB, sizeof(s_readbackB));
    if (!initiallyBlank) {
        serial::puts("[NDB1R1-ATA-QUAL] IDENTITY/CONTENT REJECTED: test "
                     "range is neither blank nor a prior exact qualification; "
                     "no sector writes issued\n");
        return false;
    }
    report_check("dedicated test sectors were initially all zero", true);

    const block::Status writeA = block::write_sectors(
        blockIndex, kPatternALba, kPatternSectorCount, s_expectedA);
    if (!report_status("PIO write pattern A", writeA, block::BLOCK_OK)) return false;
    const block::Status readAfterWriteA = block::read_sectors(
        blockIndex, kPatternALba, kPatternSectorCount, s_readbackA);
    const bool readAStatus = report_status("PIO read after write A",
                                           readAfterWriteA, block::BLOCK_OK);
    const bool readAExact = readAStatus && bytes_equal(
        s_readbackA, s_expectedA, sizeof(s_expectedA));
    report_check("pattern A exact readback before flush", readAExact);
    if (!readAExact) return false;

    if (!report_status("production ATA cache flush after pattern A",
                       block::flush(blockIndex), block::BLOCK_OK)) return false;
    const block::Status readAfterFlushA = block::read_sectors(
        blockIndex, kPatternALba, kPatternSectorCount, s_readbackA);
    const bool readAfterFlushAOk = report_status(
        "PIO read after cache flush A", readAfterFlushA, block::BLOCK_OK);
    const bool exactAfterFlushA = readAfterFlushAOk && bytes_equal(
        s_readbackA, s_expectedA, sizeof(s_expectedA));
    report_check("pattern A exact readback after flush", exactAfterFlushA);
    if (!exactAfterFlushA) return false;

    if (!report_status("repeated production ATA cache flush",
                       block::flush(blockIndex), block::BLOCK_OK)) return false;

    const block::Status writeB = block::write_sectors(
        blockIndex, kPatternBLba, kPatternSectorCount, s_expectedB);
    if (!report_status("PIO write pattern B after flush", writeB,
                       block::BLOCK_OK)) return false;
    const block::Status readAfterWriteB = block::read_sectors(
        blockIndex, kPatternBLba, kPatternSectorCount, s_readbackB);
    const bool readBStatus = report_status("PIO read after write B",
                                           readAfterWriteB, block::BLOCK_OK);
    const bool readBExact = readBStatus && bytes_equal(
        s_readbackB, s_expectedB, sizeof(s_expectedB));
    report_check("pattern B exact readback before flush", readBExact);
    if (!readBExact) return false;

    if (!report_status("production ATA cache flush after pattern B",
                       block::flush(blockIndex), block::BLOCK_OK)) return false;
    if (!read_patterns(blockIndex)) return false;

    serial::puts("[NDB1R1-ATA-QUAL] WRITE_PHASE PASS\n");
    return true;
}

} // namespace qemu_ata_pio_qualification
} // namespace kernel

#endif // GXOS_QEMU_ATA_PIO_QUALIFICATION
