// Test-only QEMU qualification for production ATA PIO, block, FAT32, and VFS.

#include "include/kernel/qemu_vfs_resize_qualification.h"

#if defined(GXOS_QEMU_VFS_RESIZE_QUALIFICATION)

#include "include/kernel/ata.h"
#include "include/kernel/block_device.h"
#include "include/kernel/fs_fat.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"

namespace kernel {
namespace qemu_vfs_resize_qualification {
namespace {

static const uint64_t kExpectedTotalSectors = 131072;
static const uint32_t kSectorSize = 512;
static const uint32_t kReservedSectors = 32;
static const uint32_t kFatSizeSectors = 1024;
static const uint32_t kFat0Start = kReservedSectors;
static const uint32_t kFat1Start = kFat0Start + kFatSizeSectors;
static const uint32_t kFirstDataSector = kFat1Start + kFatSizeSectors;
static const uint32_t kExpectedResizeSize = 513;
static const uint32_t kExpectedWalSize = 64;
static const uint32_t kResizeSourceSize = 3 * kSectorSize + 77;
static const uint32_t kWalSourceSize = 64 + 8 * kSectorSize + 93;
static const char kExpectedModel[] = "QEMU HARDDISK";
static const char kExpectedSerial[] = "GXOSNDB1R2TEST01";
static const char kVolumePath[] = "/ndb1r2";
static const char kResizePath[] = "/ndb1r2/RESIZE.BIN";
static const char kWalPath[] = "/ndb1r2/WAL.GWL";
static uint8_t s_resizeSource[kResizeSourceSize];
static uint8_t s_walSource[kWalSourceSize];
static uint8_t s_readback[kWalSourceSize];

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

static void report_check(const char* name, bool passed)
{
    serial::puts(passed ? "[NDB1R2-VFS-QUAL] PASS: "
                        : "[NDB1R2-VFS-QUAL] FAIL: ");
    serial::puts(name);
    serial::putc('\n');
}

static void fill_pattern(uint8_t* buffer, uint32_t size, uint8_t seed)
{
    for (uint32_t i = 0; i < size; ++i) {
        buffer[i] = static_cast<uint8_t>(i * 37u + seed * 19u + (i >> 3));
    }
}

static bool bytes_equal(const uint8_t* left, const uint8_t* right, uint32_t size)
{
    for (uint32_t i = 0; i < size; ++i) {
        if (left[i] != right[i]) return false;
    }
    return true;
}

static bool bytes_are_zero(const uint8_t* bytes, uint32_t size)
{
    for (uint32_t i = 0; i < size; ++i) {
        if (bytes[i] != 0) return false;
    }
    return true;
}

static bool qualified_ata_device(const ata::ATADevice* device)
{
    return device && device->active && !device->isAHCI &&
        device->ioBase == 0x170 && device->isMaster != 0 &&
        device->sectorSize == kSectorSize &&
        device->totalSectors == kExpectedTotalSectors &&
        string_equal(device->model, kExpectedModel) &&
        string_equal(device->serial, kExpectedSerial) &&
        ((device->lba48 && device->flushCacheExt) || device->flushCache);
}

static bool locate_qualified_block_device(uint8_t* outBlockIndex)
{
    if (!outBlockIndex) return false;
    uint8_t ataIndex = 0xFF;
    uint8_t ataMatches = 0;
    for (uint8_t i = 0; i < ata::device_count(); ++i) {
        const ata::ATADevice* device = ata::get_device(i);
        if (qualified_ata_device(device)) {
            ataIndex = i;
            ++ataMatches;
        }
    }
    if (ataMatches != 1) {
        serial::puts("[NDB1R2-VFS-QUAL] IDENTITY REJECTED: expected exactly one "
                     "64 MiB disposable IDE PIO master; no volume writes issued\n");
        return false;
    }

    uint8_t blockIndex = 0xFF;
    uint8_t blockMatches = 0;
    for (uint8_t i = 0; i < block::MAX_BLOCK_DEVICES; ++i) {
        const block::BlockDevice* device = block::get_device(i);
        if (device && device->type == block::BDEV_ATA_PIO &&
            device->driverIndex == ataIndex && device->readFn &&
            device->writeFn && device->flushFn &&
            device->totalSectors == kExpectedTotalSectors &&
            device->sectorSize == kSectorSize) {
            blockIndex = i;
            ++blockMatches;
        }
    }
    if (blockMatches != 1) {
        serial::puts("[NDB1R2-VFS-QUAL] IDENTITY REJECTED: verified ATA device "
                     "does not map to one bounded PIO block device\n");
        return false;
    }
    *outBlockIndex = blockIndex;
    return true;
}

static bool read_sector(uint8_t blockIndex, uint32_t lba, uint8_t* buffer)
{
    return block::read_sectors(blockIndex, lba, 1, buffer) == block::BLOCK_OK;
}

static bool write_sector(uint8_t blockIndex, uint32_t lba, const uint8_t* buffer)
{
    return block::write_sectors(blockIndex, lba, 1, buffer) == block::BLOCK_OK;
}

static bool format_fresh_disposable_volume(uint8_t blockIndex)
{
    uint8_t sector[kSectorSize];
    if (!read_sector(blockIndex, 0, sector) || !bytes_are_zero(sector, sizeof(sector))) {
        serial::puts("[NDB1R2-VFS-QUAL] REFUSED: boot sector is not blank; "
                     "won't format an unknown volume\n");
        return false;
    }
    if (!read_sector(blockIndex, 1, sector) || !bytes_are_zero(sector, sizeof(sector)) ||
        !read_sector(blockIndex, kFat0Start, sector) || !bytes_are_zero(sector, sizeof(sector)) ||
        !read_sector(blockIndex, kFat1Start, sector) || !bytes_are_zero(sector, sizeof(sector)) ||
        !read_sector(blockIndex, kFirstDataSector, sector) || !bytes_are_zero(sector, sizeof(sector))) {
        serial::puts("[NDB1R2-VFS-QUAL] REFUSED: blank-disk signature sectors "
                     "contain data\n");
        return false;
    }

    for (uint32_t i = 0; i < sizeof(sector); ++i) sector[i] = 0;
    fs_fat::FAT32_BPB* bpb = reinterpret_cast<fs_fat::FAT32_BPB*>(sector);
    bpb->jmpBoot[0] = 0xEB;
    bpb->jmpBoot[1] = 0x58;
    bpb->jmpBoot[2] = 0x90;
    for (uint32_t i = 0; i < 8; ++i) bpb->oemName[i] = "GXOSNDB1"[i];
    bpb->bytesPerSector = kSectorSize;
    bpb->sectorsPerCluster = 1;
    bpb->reservedSectors = kReservedSectors;
    bpb->numFATs = 2;
    bpb->mediaType = 0xF8;
    bpb->totalSectors32 = static_cast<uint32_t>(kExpectedTotalSectors);
    bpb->fatSize32 = kFatSizeSectors;
    bpb->rootCluster = 2;
    bpb->fsInfoSector = 1;
    bpb->backupBootSector = 6;
    bpb->driveNumber = 0x80;
    bpb->bootSig = 0x29;
    bpb->volumeID = 0x4E444231u;
    const char label[11] = { 'N','D','B','1','R','2',' ',' ',' ',' ',' ' };
    for (uint32_t i = 0; i < sizeof(label); ++i) bpb->volumeLabel[i] = label[i];
    const char fsType[8] = { 'F','A','T','3','2',' ',' ',' ' };
    for (uint32_t i = 0; i < sizeof(fsType); ++i) bpb->fsType[i] = fsType[i];
    sector[510] = 0x55;
    sector[511] = 0xAA;
    if (!write_sector(blockIndex, 0, sector)) return false;

    for (uint32_t i = 0; i < sizeof(sector); ++i) sector[i] = 0;
    uint32_t* fsInfo = reinterpret_cast<uint32_t*>(sector);
    fsInfo[0] = 0x41615252u;
    fsInfo[121] = 0x61417272u;
    fsInfo[122] = 0xFFFFFFFFu; // free count unknown; this implementation doesn't update FSInfo
    fsInfo[123] = 0xFFFFFFFFu; // next-free hint unknown
    fsInfo[127] = 0xAA550000u;
    if (!write_sector(blockIndex, 1, sector)) return false;

    if (!read_sector(blockIndex, 0, sector) || !write_sector(blockIndex, 6, sector)) {
        return false;
    }
    if (!read_sector(blockIndex, 1, sector) || !write_sector(blockIndex, 7, sector)) {
        return false;
    }

    for (uint32_t i = 0; i < sizeof(sector); ++i) sector[i] = 0;
    uint32_t* fat = reinterpret_cast<uint32_t*>(sector);
    fat[0] = 0x0FFFFFF8u;
    fat[1] = 0x0FFFFFFFu;
    fat[2] = 0x0FFFFFFFu;
    if (!write_sector(blockIndex, kFat0Start, sector) ||
        !write_sector(blockIndex, kFat1Start, sector)) {
        return false;
    }
    return block::flush(blockIndex) == block::BLOCK_OK;
}

static bool volume_is_expected_fat32(uint8_t blockIndex)
{
    uint8_t sector[kSectorSize];
    if (!read_sector(blockIndex, 0, sector)) return false;
    const fs_fat::FAT32_BPB* bpb =
        reinterpret_cast<const fs_fat::FAT32_BPB*>(sector);
    return sector[510] == 0x55 && sector[511] == 0xAA &&
        bpb->bytesPerSector == kSectorSize && bpb->sectorsPerCluster == 1 &&
        bpb->reservedSectors == kReservedSectors && bpb->numFATs == 2 &&
        bpb->totalSectors32 == kExpectedTotalSectors &&
        bpb->fatSize32 == kFatSizeSectors && bpb->rootCluster == 2 &&
        bytes_equal(reinterpret_cast<const uint8_t*>(bpb->fsType),
                    reinterpret_cast<const uint8_t*>("FAT32   "), 8) &&
        bpb->volumeLabel[0] == 'N' && bpb->volumeLabel[1] == 'D' &&
        bpb->volumeLabel[2] == 'B' && bpb->volumeLabel[3] == '1' &&
        bpb->volumeLabel[4] == 'R' && bpb->volumeLabel[5] == '2';
}

static bool write_resize_file(const char* path, const uint8_t* bytes,
                              uint32_t sourceSize, uint32_t finalSize)
{
    const uint8_t handle = vfs::open(path, vfs::OPEN_CREATE | vfs::OPEN_RDWR);
    if (handle == 0xFF) return false;
    bool passed = vfs::write(handle, bytes, sourceSize) ==
        static_cast<int32_t>(sourceSize);
    passed = passed && vfs::flush(handle) == vfs::VFS_OK;
    passed = passed && vfs::truncate_shrink(handle, finalSize) == vfs::VFS_OK;
    passed = passed && vfs::flush(handle) == vfs::VFS_OK;
    const vfs::Status closeStatus = vfs::close(handle);
    return passed && closeStatus == vfs::VFS_OK;
}

static bool verify_file(const char* path, const uint8_t* expected,
                        uint32_t expectedSize)
{
    const uint8_t handle = vfs::open(path, vfs::OPEN_READ);
    if (handle == 0xFF) return false;
    const bool sizeOk = vfs::file_size(handle) == expectedSize;
    const bool seekOk = vfs::seek(handle, 0, static_cast<vfs::SeekOrigin>(0)) == vfs::VFS_OK;
    const int32_t amount = expectedSize == 0 ? 0 : vfs::read(
        handle, s_readback, expectedSize);
    uint8_t extra = 0;
    const int32_t eof = vfs::read(handle, &extra, 1);
    const bool contentOk = amount == static_cast<int32_t>(expectedSize) &&
        bytes_equal(s_readback, expected, expectedSize) && eof == 0;
    const vfs::Status closeStatus = vfs::close(handle);
    return sizeOk && seekOk && contentOk && closeStatus == vfs::VFS_OK;
}

static bool validate_chain(uint8_t blockIndex, const char* path, uint32_t fileSize)
{
    const vfs::MountPoint* mount = vfs::get_mount(kVolumePath);
    const fs_fat::FATVolume* volume = mount
        ? fs_fat::get_volume(mount->fsVolumeIndex) : nullptr;
    if (!mount || !volume || volume->type != fs_fat::FAT_TYPE_FAT32 ||
        volume->bytesPerSector == 0 || volume->sectorsPerCluster == 0) return false;

    fs_fat::DirEntry entry{};
    const char* relative = path + sizeof(kVolumePath) - 1u;
    if (!fs_fat::lookup_path(mount->fsVolumeIndex, relative, &entry) ||
        entry.fileSize != fileSize) return false;
    if (fileSize == 0) return entry.firstCluster == 0;
    if (entry.firstCluster < 2) return false;

    const uint32_t clusterBytes = volume->bytesPerSector * volume->sectorsPerCluster;
    const uint32_t required = static_cast<uint32_t>(
        (static_cast<uint64_t>(fileSize) + clusterBytes - 1u) / clusterBytes);
    uint32_t cluster = entry.firstCluster;
    uint8_t sector[kSectorSize];
    for (uint32_t index = 0; index < required; ++index) {
        if (cluster < 2 || cluster >= volume->totalDataClusters + 2u) return false;
        const uint64_t fatOffset = static_cast<uint64_t>(cluster) * 4u;
        const uint32_t sectorOffset = static_cast<uint32_t>(fatOffset / kSectorSize);
        const uint32_t entryOffset = static_cast<uint32_t>(fatOffset % kSectorSize);
        if (!read_sector(blockIndex, kFat0Start + sectorOffset, sector)) return false;
        const uint32_t next = (*reinterpret_cast<uint32_t*>(&sector[entryOffset])) &
            0x0FFFFFFFu;
        uint8_t mirror[kSectorSize];
        if (!read_sector(blockIndex, kFat1Start + sectorOffset, mirror) ||
            ((*reinterpret_cast<uint32_t*>(&mirror[entryOffset])) & 0x0FFFFFFFu) != next) {
            return false;
        }
        const bool eoc = next >= 0x0FFFFFF8u;
        if (eoc != (index + 1u == required)) return false;
        if (!eoc) cluster = next;
    }
    return true;
}

static bool run_first_process(uint8_t blockIndex)
{
    fill_pattern(s_resizeSource, sizeof(s_resizeSource), 0x35);
    fill_pattern(s_walSource, sizeof(s_walSource), 0xA7);
    const bool resizeWritten = write_resize_file(kResizePath, s_resizeSource,
        sizeof(s_resizeSource), kExpectedResizeSize);
    report_check("production VFS writes, flushes, and shrinks a multicluster file",
                 resizeWritten);
    if (!resizeWritten) return false;

    const bool walWritten = write_resize_file(kWalPath, s_walSource,
        sizeof(s_walSource), kExpectedWalSize);
    report_check("production VFS shrinks WAL-like contents to the 64-byte header",
                 walWritten);
    if (!walWritten) return false;

    const bool resizeReadback = verify_file(kResizePath, s_resizeSource,
                                             kExpectedResizeSize);
    report_check("same-process reopen preserves exact resized bytes and EOF",
                 resizeReadback);
    const bool walReadback = verify_file(kWalPath, s_walSource, kExpectedWalSize);
    report_check("same-process reopen preserves exact WAL header only", walReadback);
    const bool resizeChain = validate_chain(blockIndex, kResizePath, kExpectedResizeSize);
    report_check("multicluster FAT chain matches published resize size", resizeChain);
    const bool walChain = validate_chain(blockIndex, kWalPath, kExpectedWalSize);
    report_check("WAL header FAT chain matches 64-byte file size", walChain);

    if (!resizeReadback || !walReadback || !resizeChain || !walChain) return false;
    serial::puts("[NDB1R2-VFS-QUAL] WRITE_PHASE PASS\n");
    return true;
}

static bool run_independent_process_readback(uint8_t blockIndex)
{
    fill_pattern(s_resizeSource, sizeof(s_resizeSource), 0x35);
    fill_pattern(s_walSource, sizeof(s_walSource), 0xA7);
    const bool resizeReadback = verify_file(kResizePath, s_resizeSource,
                                             kExpectedResizeSize);
    report_check("independent process reads exact retained file prefix and size",
                 resizeReadback);
    const bool walReadback = verify_file(kWalPath, s_walSource, kExpectedWalSize);
    report_check("independent process reads exact WAL header and size", walReadback);
    const bool resizeChain = validate_chain(blockIndex, kResizePath, kExpectedResizeSize);
    report_check("independent process validates resized FAT chain", resizeChain);
    const bool walChain = validate_chain(blockIndex, kWalPath, kExpectedWalSize);
    report_check("independent process validates WAL header FAT chain", walChain);
    if (!resizeReadback || !walReadback || !resizeChain || !walChain) return false;
    serial::puts("[NDB1R2-VFS-QUAL] CROSS_PROCESS_READBACK PASS\n");
    return true;
}

} // namespace

bool run()
{
    serial::puts("[NDB1R2-VFS-QUAL] START production ATA/VFS/FAT32 resize qualification\n");
    uint8_t blockIndex = 0xFF;
    if (!locate_qualified_block_device(&blockIndex)) return false;

    const block::BlockDevice* device = block::get_device(blockIndex);
    if (!device || device->totalSectors != kExpectedTotalSectors ||
        device->sectorSize != kSectorSize) {
        serial::puts("[NDB1R2-VFS-QUAL] IDENTITY REJECTED: block capacity mismatch\n");
        return false;
    }

    uint8_t bootSector[kSectorSize];
    if (!read_sector(blockIndex, 0, bootSector)) {
        serial::puts("[NDB1R2-VFS-QUAL] FAIL: cannot inspect disposable disk\n");
        return false;
    }
    const bool blank = bytes_are_zero(bootSector, sizeof(bootSector));
    if (blank) {
        if (!format_fresh_disposable_volume(blockIndex)) return false;
        serial::puts("[NDB1R2-VFS-QUAL] formatted newly created blank disposable image\n");
    }
    const bool volumeIdentity = volume_is_expected_fat32(blockIndex);
    report_check("guest disk has the expected 64 MiB NDB1R2 FAT32 volume identity",
                 volumeIdentity);
    if (!volumeIdentity) return false;

    fs_fat::init();
    vfs::init();
    const uint8_t mountIndex = vfs::mount_type(kVolumePath, blockIndex, vfs::FS_TYPE_FAT32);
    report_check("mount expected FAT32 volume through production VFS", mountIndex != 0xFF);
    if (mountIndex == 0xFF) return false;

    const bool resizeExists = vfs::exists(kResizePath);
    const bool walExists = vfs::exists(kWalPath);
    if (resizeExists != walExists) {
        serial::puts("[NDB1R2-VFS-QUAL] FAIL: only one persisted qualification file exists\n");
        return false;
    }
    return resizeExists
        ? run_independent_process_readback(blockIndex)
        : run_first_process(blockIndex);
}

} // namespace qemu_vfs_resize_qualification
} // namespace kernel

#endif // GXOS_QEMU_VFS_RESIZE_QUALIFICATION
