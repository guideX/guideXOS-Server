#include "kernel/block_device.h"
#include "kernel/fs_fat.h"
#include "kernel/ramdisk.h"
#include "kernel/serial_debug.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

static std::string g_serialCapture;

namespace kernel { namespace serial {
void hosted_write_char(char c)
{
    if (g_serialCapture.size() < 32768) g_serialCapture.push_back(c);
}
} }

namespace {

int checks = 0;
int failures = 0;

void check(bool condition, const char* name)
{
    ++checks;
    if (condition) std::cout << "PASS: " << name << "\n";
    else {
        ++failures;
        std::cout << "FAIL: " << name << "\n";
    }
}

bool read_file(const char* path, std::vector<uint8_t>& bytes)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return !stream.bad();
}

bool contains(const std::string& value, const std::string& needle)
{
    return value.find(needle) != std::string::npos;
}

std::string hex16(const uint8_t* bytes)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string value;
    for (size_t i = 0; i < 16; ++i) {
        value.push_back(hex[(bytes[i] >> 4) & 0x0F]);
        value.push_back(hex[bytes[i] & 0x0F]);
    }
    return value;
}

size_t occurrences(const std::string& value, const std::string& needle)
{
    size_t count = 0;
    size_t position = 0;
    while ((position = value.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

void reset_case()
{
    g_serialCapture.clear();
    kernel::block::set_hosted_read_mutation(nullptr);
    kernel::block::init();
    kernel::ramdisk::reset_for_provenance_test();
    kernel::fs_fat::init();
}

uint8_t find_device(const char* name)
{
    for (uint8_t i = 0; i < kernel::block::MAX_BLOCK_DEVICES; ++i) {
        const kernel::block::BlockDevice* device = kernel::block::get_device(i);
        if (!device || !name) continue;
        size_t j = 0;
        while (name[j] && device->name[j] == name[j]) ++j;
        if (!name[j] && !device->name[j]) return i;
    }
    return 0xFF;
}

uint8_t attach(const std::vector<uint8_t>& image, const char* name)
{
    const uint8_t ramdisk = kernel::ramdisk::create_readonly_at(image.data(), image.size(), name);
    if (ramdisk == 0xFF) return 0xFF;
    return find_device(name);
}

kernel::block::Status failed_read(uint8_t, uint64_t, uint32_t, void*)
{
    return kernel::block::BLOCK_ERR_IO;
}

void corrupt_consumer_buffer(uint8_t, uint64_t lba, uint32_t count, void* buffer)
{
    if (lba == 0 && count != 0 && buffer) {
        uint8_t* bytes = static_cast<uint8_t*>(buffer);
        bytes[0x0B] = 0;
        bytes[0x0C] = 0;
    }
}

void write_le32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

bool test_known_good(const std::vector<uint8_t>& image)
{
    reset_case();
    const std::string expected = "EB58904D5357494E342E310002012000";
    check(image.size() >= 512 &&
              std::string(reinterpret_cast<const char*>(image.data()), 16) ==
                  std::string("\xEB\x58\x90\x4D\x53\x57\x49\x4E\x34\x2E\x31\x00\x02\x01\x20\x00", 16),
          "offline RAM disk has the pinned first 16 bytes");
    check(hex16(image.data()) == expected,
          "trace reference is the exact 16-byte hex encoding");

    const uint8_t ramdisk = kernel::ramdisk::create_readonly_at(
        image.data(), image.size(), "wallimg");
    const uint8_t device = find_device("wallimg");
    const kernel::ramdisk::RamDisk* disk = kernel::ramdisk::get_disk(ramdisk);
    check(ramdisk != 0xFF && device != 0xFF && disk && disk->data == image.data() &&
              !disk->ownsMemory && disk->readOnly && disk->sectorSize == 512,
          "read-only attachment borrows the exact input buffer");

    std::vector<uint8_t> destination(512, 0xA5);
    check(kernel::block::read_sectors(device, 0, 1, destination.data()) ==
              kernel::block::BLOCK_OK,
          "production block layer reads RAM disk LBA 0");
    check(destination.size() >= 16 &&
              std::string(reinterpret_cast<const char*>(destination.data()), 16) ==
                  std::string(reinterpret_cast<const char*>(image.data()), 16),
          "production RAM disk callback copies the exact source bytes");
    check(contains(g_serialCapture, "SQL_R3AM_BLOCK_SRC valid=1") &&
              contains(g_serialCapture, "bytes16=" + expected),
          "callback source trace records the known-good bytes");
    check(contains(g_serialCapture, "SQL_R3AM_BLOCK_DST valid=1") &&
              contains(g_serialCapture, "copy_length=0000000000000200"),
          "callback destination trace records a bounded one-sector copy");
    check(occurrences(g_serialCapture, "SQL_R3AM_BLOCK_SRC") == 1,
          "callback emits only one bounded LBA 0 observation");

    g_serialCapture.clear();
    const uint8_t volume = kernel::fs_fat::mount(device);
    check(volume != 0xFF, "production FAT32 parser accepts the pinned volume offline");
    check(contains(g_serialCapture, "SQL_R3AM_FAT_READ valid=1") &&
              contains(g_serialCapture, "bytes16=" + expected) &&
              contains(g_serialCapture, "bps=0200"),
          "FAT consumer trace decodes little-endian 512-byte sectors");
    check(contains(g_serialCapture, "SQL_R3AM_FAT_BPB") &&
              contains(g_serialCapture, "validation=ACCEPTED"),
          "FAT trace records the exact accepted validation result");
    if (volume != 0xFF) kernel::fs_fat::unmount(volume);
    return failures == 0;
}

bool test_wrong_source_matches_destination(const std::vector<uint8_t>& image)
{
    reset_case();
    std::vector<uint8_t> wrong = image;
    wrong[0x0B] = 0;
    wrong[0x0C] = 0;
    const uint8_t device = attach(wrong, "wallimg");
    const uint8_t volume = kernel::fs_fat::mount(device);
    check(volume == 0xFF, "incorrect backing source is rejected by FAT32");
    check(contains(g_serialCapture, "SQL_R3AM_BLOCK_SRC valid=1") &&
              contains(g_serialCapture, "SQL_R3AM_BLOCK_DST valid=1"),
          "incorrect source and matching destination are both observed");
    check(contains(g_serialCapture, "SQL_R3AM_BLOCK_SRC") &&
              g_serialCapture.find("SQL_R3AM_BLOCK_SRC") <
                  g_serialCapture.find("SQL_R3AM_BLOCK_DST"),
          "source and destination observations preserve copy order");
    check(contains(g_serialCapture, "SQL_R3AM_FAT_READ valid=1") &&
              contains(g_serialCapture, "SQL_R3AM_FAT_BPB") &&
              contains(g_serialCapture, "FAT32_BYTES_PER_SECTOR_INVALID"),
          "FAT consumer identifies the zero bytes-per-sector rejection");
    check(wrong[0x0B] == 0 && wrong[0x0C] == 0 && image[0x0B] == 0 && image[0x0C] == 2,
          "fault input is deterministic and isolated to the hosted copy");
    return failures == 0;
}

bool test_copy_destination_fault(const std::vector<uint8_t>& image)
{
    reset_case();
    kernel::ramdisk::set_provenance_test_fault(
        kernel::ramdisk::PROVENANCE_TEST_FAULT_COPY_DESTINATION_BPS);
    const uint8_t device = attach(image, "wallimg");
    const uint8_t volume = kernel::fs_fat::mount(device);
    check(volume == 0xFF, "controlled destination fault is rejected by FAT32");
    check(contains(g_serialCapture, "SQL_R3AM_BLOCK_SRC valid=1") &&
              contains(g_serialCapture, "bytes16=EB58904D5357494E342E310002012000") &&
              contains(g_serialCapture, "SQL_R3AM_BLOCK_DST valid=1") &&
              contains(g_serialCapture, "bytes16=EB58904D5357494E342E310000012000"),
          "trace distinguishes correct source from incorrect callback destination");
    check(contains(g_serialCapture, "SQL_R3AM_FAT_READ valid=1") &&
              contains(g_serialCapture, "bps=0000"),
          "FAT consumer sees the controlled callback destination fault");
    return failures == 0;
}

bool test_consumer_handoff_fault(const std::vector<uint8_t>& image)
{
    reset_case();
    const uint8_t device = attach(image, "wallimg");
    kernel::block::set_hosted_read_mutation(&corrupt_consumer_buffer);
    const uint8_t volume = kernel::fs_fat::mount(device);
    check(volume == 0xFF, "controlled post-callback consumer fault is rejected");
    check(contains(g_serialCapture, "SQL_R3AM_BLOCK_SRC valid=1") &&
              contains(g_serialCapture, "SQL_R3AM_BLOCK_DST valid=1") &&
              contains(g_serialCapture, "SQL_R3AM_BLOCK_DST") &&
              g_serialCapture.find("SQL_R3AM_BLOCK_DST") <
                  g_serialCapture.find("SQL_R3AM_FAT_READ"),
          "block callback succeeds with the correct bytes before the consumer handoff");
    check(contains(g_serialCapture, "SQL_R3AM_FAT_READ valid=1") &&
              contains(g_serialCapture, "bps=0000"),
          "trace distinguishes callback bytes from the FAT caller-visible buffer");
    return failures == 0;
}

bool test_invalid_bounds_and_failed_read(const std::vector<uint8_t>& image)
{
    reset_case();
    std::vector<uint8_t> shortImage(15, 0);
    check(kernel::ramdisk::create_readonly_at(shortImage.data(), shortImage.size(), "wallimg") == 0xFF,
          "short backing image is rejected before any diagnostic byte access");

    reset_case();
    const uint8_t shortBacking = kernel::ramdisk::create_readonly_at(
        image.data(), image.size(), "wallimg");
    std::vector<uint8_t> rangeDestination(512, 0xA5);
    kernel::ramdisk::RamDisk* boundedDisk = const_cast<kernel::ramdisk::RamDisk*>(
        kernel::ramdisk::get_disk(shortBacking));
    boundedDisk->sectorCount = 0;
    check(kernel::ramdisk::read_sectors(shortBacking, 0, 1, rangeDestination.data()) ==
              kernel::block::BLOCK_ERR_INVALID,
          "invalid source sector bounds fail in the production callback");
    check(contains(g_serialCapture, "SQL_R3AM_BLOCK_SRC valid=0 status=05") &&
              contains(g_serialCapture, "offset=0000000000000000") &&
              contains(g_serialCapture, "byte_count=0000000000000200") &&
              contains(g_serialCapture, "bytes16=unavailable"),
          "invalid source bounds report calculated range without reading it");

    reset_case();
    const uint8_t ramdisk = kernel::ramdisk::create_readonly_at(image.data(), image.size(), "wallimg");
    std::vector<uint8_t> destination(512, 0xA5);
    kernel::ramdisk::RamDisk* disk = const_cast<kernel::ramdisk::RamDisk*>(
        kernel::ramdisk::get_disk(ramdisk));
    disk->data = nullptr;
    check(kernel::ramdisk::read_sectors(ramdisk, 0, 1, destination.data()) ==
              kernel::block::BLOCK_ERR_INVALID,
          "invalid source backing fails without touching destination bytes");
    check(contains(g_serialCapture, "SQL_R3AM_BLOCK_SRC valid=0 status=05") &&
              contains(g_serialCapture, "bytes16=unavailable") &&
              contains(g_serialCapture, "SQL_R3AM_BLOCK_DST valid=0"),
          "invalid source trace reports failure without dumping unavailable bytes");

    reset_case();
    const uint8_t nullDestinationDisk = kernel::ramdisk::create_readonly_at(
        image.data(), image.size(), "wallimg");
    check(kernel::ramdisk::read_sectors(nullDestinationDisk, 0, 1, nullptr) ==
              kernel::block::BLOCK_ERR_INVALID,
          "invalid destination is rejected by the production callback");
    check(contains(g_serialCapture, "SQL_R3AM_BLOCK_SRC valid=1") &&
              contains(g_serialCapture, "SQL_R3AM_BLOCK_DST valid=0 status=05") &&
              contains(g_serialCapture, "bytes16=unavailable"),
          "failed destination trace never reads uninitialized destination memory");

    reset_case();
    kernel::block::BlockDevice failing = {};
    failing.active = true;
    failing.type = kernel::block::BDEV_USB_MASS;
    failing.totalSectors = 1;
    failing.sectorSize = 512;
    failing.readFn = &failed_read;
    const char name[] = "wallimg";
    for (size_t i = 0; i < sizeof(name); ++i) failing.name[i] = name[i];
    const uint8_t failedDevice = kernel::block::register_device(failing);
    const uint8_t failedVolume = kernel::fs_fat::mount(failedDevice);
    check(failedVolume == 0xFF &&
              contains(g_serialCapture, "SQL_R3AM_FAT_READ valid=0") &&
              contains(g_serialCapture, "read_status=01") &&
              contains(g_serialCapture, "bytes16=unavailable"),
          "failed block read is reported without dumping the destination buffer");
    return failures == 0;
}

bool test_device_identity_and_partition_offset(const std::vector<uint8_t>& image)
{
    reset_case();
    const uint8_t otherDevice = attach(image, "other");
    std::vector<uint8_t> destination(512, 0);
    check(otherDevice != 0xFF && kernel::block::read_sectors(
              otherDevice, 0, 1, destination.data()) == kernel::block::BLOCK_OK,
          "non-diagnostic device remains readable");
    check(!contains(g_serialCapture, "SQL_R3AM_BLOCK_SRC"),
          "callback trace does not cross device identity");

    reset_case();
    std::vector<uint8_t> partitioned(512 + image.size(), 0);
    partitioned[446 + 4] = 0x0B;
    write_le32(partitioned, 446 + 8, 1);
    write_le32(partitioned, 446 + 12, static_cast<uint32_t>(image.size() / 512));
    partitioned[510] = 0x55;
    partitioned[511] = 0xAA;
    for (size_t i = 0; i < image.size(); ++i) partitioned[512 + i] = image[i];
    const uint8_t partitionedDevice = attach(partitioned, "wallimg");
    const uint8_t volume = kernel::fs_fat::mount(partitionedDevice);
    check(volume != 0xFF, "production FAT mount handles a nonzero partition start");
    check(contains(g_serialCapture, "SQL_R3AM_BLOCK_SRC") &&
              contains(g_serialCapture, "offset=0000000000000000"),
          "LBA 0 backing offset is explicit for a partitioned device");
    if (volume != 0xFF) kernel::fs_fat::unmount(volume);
    return failures == 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: native_sql_r3am_ramdisk_provenance_test <ramdisk.img>\n";
        return 2;
    }
    std::vector<uint8_t> image;
    if (!read_file(argv[1], image) || image.size() < 512) {
        std::cerr << "could not read the pinned RAM disk input\n";
        return 2;
    }

    test_known_good(image);
    test_wrong_source_matches_destination(image);
    test_copy_destination_fault(image);
    test_consumer_handoff_fault(image);
    test_invalid_bounds_and_failed_read(image);
    test_device_identity_and_partition_offset(image);

    std::cout << "nativeSqlR3amProvenanceChecks=" << (checks - failures)
              << "/" << checks << "\n";
    return failures == 0 ? 0 : 1;
}
