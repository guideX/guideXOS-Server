#include "kernel/block_device.h"
#include "kernel/fs_fat.h"

#include <cstdint>
#include <cstring>
#include <iostream>

namespace kernel { namespace arch { namespace amd64 {
uint8_t inb(uint16_t) { return 0x20; }
void outb(uint16_t, uint8_t) { }
}}}

namespace {
uint8_t g_sector[512];
uint32_t g_readCalls = 0;
bool g_failReads = false;
int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* label)
{
    ++g_checks;
    if (condition) {
        std::cout << "PASS: " << label << "\n";
    } else {
        ++g_failures;
        std::cout << "FAIL: " << label << "\n";
    }
}

void write_u16(uint32_t offset, uint16_t value)
{
    g_sector[offset] = static_cast<uint8_t>(value & 0xFF);
    g_sector[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void write_u32(uint32_t offset, uint32_t value)
{
    g_sector[offset] = static_cast<uint8_t>(value & 0xFF);
    g_sector[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    g_sector[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    g_sector[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

kernel::block::Status read_sector(uint8_t, uint64_t lba, uint32_t count, void* buffer)
{
    ++g_readCalls;
    if (g_failReads) return kernel::block::BLOCK_ERR_IO;
    if (lba != 0 || count != 1 || !buffer) return kernel::block::BLOCK_ERR_INVALID;
    std::memcpy(buffer, g_sector, sizeof(g_sector));
    return kernel::block::BLOCK_OK;
}

void make_valid_fat32_sector()
{
    std::memset(g_sector, 0, sizeof(g_sector));
    g_sector[0] = 0xEB;
    g_sector[1] = 0x58;
    g_sector[2] = 0x90;
    std::memcpy(&g_sector[82], "FAT32   ", 8);
    write_u16(11, 512);
    g_sector[13] = 16;
    write_u16(14, 32);
    g_sector[16] = 2;
    write_u32(32, 131072);
    write_u32(36, 256);
    write_u32(44, 2);
    g_sector[510] = 0x55;
    g_sector[511] = 0xAA;
}

uint8_t mount_sector(bool failReads)
{
    kernel::block::init();
    kernel::fs_fat::init();
    g_readCalls = 0;
    g_failReads = failReads;

    kernel::block::BlockDevice device{};
    device.type = kernel::block::BDEV_RAMDISK;
    device.totalSectors = 1;
    device.sectorSize = sizeof(g_sector);
    std::memcpy(device.name, "fat-diag-fixture", sizeof("fat-diag-fixture"));
    device.readFn = read_sector;
    const uint8_t deviceIndex = kernel::block::register_device(device);
    if (deviceIndex == 0xFF) return 0xFF;
    return kernel::fs_fat::mount(deviceIndex);
}

#if defined(GXOS_FAT_MOUNT_DIAGNOSTICS)
kernel::fs_fat::MountDiagnostic diagnostic()
{
    return kernel::fs_fat::last_mount_diagnostic();
}
#endif
}

int main()
{
    make_valid_fat32_sector();
    uint8_t volume = mount_sector(false);
    check(volume != 0xFF, "valid FAT32 BPB still mounts through the production FAT driver");
    check(g_readCalls == 1, "valid FAT32 mount keeps its original single-sector read path");
#if defined(GXOS_FAT_MOUNT_DIAGNOSTICS)
    kernel::fs_fat::MountDiagnostic d = diagnostic();
    check(d.attemptId != 0 && d.blockDeviceIndex == 0 &&
              d.initialRequestedSector == 0 && d.firstReadStatus == kernel::block::BLOCK_OK,
          "successful mount records its attempt, selected device, initial LBA, and direct read result");
    check(d.sectorZeroObtained && d.bootSignatureReadable && d.bootSignatureValid &&
              d.bpbFieldsReadable,
          "successful mount records sector-zero availability and safe boot-sector signature data");
    check(d.bytesPerSector == 512 && d.sectorsPerCluster == 16 &&
              d.reservedSectorCount == 32 && d.fatCount == 2 &&
              d.fatSize32 == 256 && d.totalSectors32 == 131072 &&
              d.finalStage == kernel::fs_fat::FAT_MOUNT_DIAG_STAGE_MOUNTED,
          "successful mount records relevant BPB geometry and final stage");
#endif
    if (volume != 0xFF) kernel::fs_fat::unmount(volume);

    make_valid_fat32_sector();
    g_sector[510] = 0;
    g_sector[511] = 0;
    volume = mount_sector(false);
    check(volume != 0xFF, "invalid FAT boot signature does not change the existing BPB mount decision");
#if defined(GXOS_FAT_MOUNT_DIAGNOSTICS)
    d = diagnostic();
    check(d.bootSignatureReadable && !d.bootSignatureValid &&
              d.finalStage == kernel::fs_fat::FAT_MOUNT_DIAG_STAGE_MOUNTED,
          "invalid FAT boot signature is reported as an observation without changing mount behavior");
#endif
    if (volume != 0xFF) kernel::fs_fat::unmount(volume);

    make_valid_fat32_sector();
    g_sector[16] = 0;
    volume = mount_sector(false);
    check(volume == 0xFF, "malformed FAT count is rejected by the production FAT validators");
#if defined(GXOS_FAT_MOUNT_DIAGNOSTICS)
    d = diagnostic();
    check(d.firstBpbRejectStage == kernel::fs_fat::FAT_MOUNT_DIAG_STAGE_FAT32_FAT_COUNT_REJECT &&
              d.lastBpbRejectStage == kernel::fs_fat::FAT_MOUNT_DIAG_STAGE_FAT16_FAT_COUNT_REJECT,
          "FAT32 and FAT16 malformed-field rejection stages remain distinct");
#endif

    make_valid_fat32_sector();
    write_u16(11, 0);
    volume = mount_sector(false);
    check(volume == 0xFF, "invalid BPB sector size is rejected");
#if defined(GXOS_FAT_MOUNT_DIAGNOSTICS)
    d = diagnostic();
    check(d.firstBpbRejectStage == kernel::fs_fat::FAT_MOUNT_DIAG_STAGE_FAT32_BYTES_PER_SECTOR_REJECT &&
              d.firstBpbRejectStage != d.lastBpbRejectStage,
          "invalid bytes-per-sector reports a distinct first BPB rejection stage");
#endif

    make_valid_fat32_sector();
    write_u32(32, 544);
    volume = mount_sector(false);
    check(volume == 0xFF, "volume with no data region after reserved sectors and FATs is rejected");
#if defined(GXOS_FAT_MOUNT_DIAGNOSTICS)
    d = diagnostic();
    check(d.firstBpbRejectStage == kernel::fs_fat::FAT_MOUNT_DIAG_STAGE_FAT32_DATA_REGION_REJECT &&
              d.firstBpbRejectStage != kernel::fs_fat::FAT_MOUNT_DIAG_STAGE_FAT32_BYTES_PER_SECTOR_REJECT &&
              d.firstBpbRejectStage != kernel::fs_fat::FAT_MOUNT_DIAG_STAGE_FAT32_FAT_COUNT_REJECT,
          "unsupported empty-data geometry reports a distinct BPB rejection stage");
#endif

    make_valid_fat32_sector();
    volume = mount_sector(true);
    check(volume == 0xFF && g_readCalls == 2,
          "initial sector read failure remains a failed mount without additional storage writes");
#if defined(GXOS_FAT_MOUNT_DIAGNOSTICS)
    d = diagnostic();
    check(d.firstReadStatus == kernel::block::BLOCK_ERR_IO &&
              d.lastReadStatus == kernel::block::BLOCK_ERR_IO &&
              !d.sectorZeroObtained &&
              d.finalStage == kernel::fs_fat::FAT_MOUNT_DIAG_STAGE_EXFAT_SECTOR_READ_FAILED,
          "initial block-layer error is preserved directly in mount diagnostics");
#endif

    std::cout << "checks=" << g_checks << " failures=" << g_failures
#if defined(GXOS_FAT_MOUNT_DIAGNOSTICS)
              << " diagnostics=on"
#else
              << " diagnostics=off"
#endif
              << "\n";
    return g_failures == 0 ? 0 : 1;
}
