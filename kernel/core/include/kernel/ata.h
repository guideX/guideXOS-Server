// ATA / SATA Storage Driver
//
// Supports:
//   - Legacy ATA PIO mode (port I/O, x86/amd64 only)
//   - AHCI / SATA (MMIO, available on x86/amd64/ia64/sparc64)
//   - IDENTIFY DEVICE, READ SECTORS, WRITE SECTORS
//   - 28-bit and 48-bit LBA addressing
//
// On architectures without PCI or port I/O the driver compiles
// but init() returns immediately (no devices registered).
//
// Reference: ATA/ATAPI-8 (ACS-3), Serial ATA AHCI 1.3.1
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_ATA_H
#define KERNEL_ATA_H

#include "kernel/types.h"
#include "kernel/block_device.h"

namespace kernel {
namespace ata {

// ================================================================
// ATA register offsets (primary / secondary channel)
// ================================================================

static const uint16_t ATA_PRIMARY_IO   = 0x1F0;
static const uint16_t ATA_PRIMARY_CTRL = 0x3F6;
static const uint16_t ATA_SECONDARY_IO = 0x170;
static const uint16_t ATA_SECONDARY_CTRL = 0x376;

// Offsets from I/O base
static const uint16_t ATA_REG_DATA       = 0x00;
static const uint16_t ATA_REG_ERROR      = 0x01;
static const uint16_t ATA_REG_FEATURES   = 0x01;
static const uint16_t ATA_REG_SECCOUNT   = 0x02;
static const uint16_t ATA_REG_LBA_LO     = 0x03;
static const uint16_t ATA_REG_LBA_MID    = 0x04;
static const uint16_t ATA_REG_LBA_HI     = 0x05;
static const uint16_t ATA_REG_DRIVE_HEAD = 0x06;
static const uint16_t ATA_REG_STATUS     = 0x07;
static const uint16_t ATA_REG_COMMAND    = 0x07;

// ATA status bits
static const uint8_t ATA_SR_BSY  = 0x80;
static const uint8_t ATA_SR_DRDY = 0x40;
static const uint8_t ATA_SR_DF   = 0x20;
static const uint8_t ATA_SR_DSC  = 0x10;
static const uint8_t ATA_SR_DRQ  = 0x08;
static const uint8_t ATA_SR_CORR = 0x04;
static const uint8_t ATA_SR_IDX  = 0x02;
static const uint8_t ATA_SR_ERR  = 0x01;

// ATA commands
static const uint8_t ATA_CMD_IDENTIFY      = 0xEC;
static const uint8_t ATA_CMD_READ_PIO      = 0x20;  // READ SECTORS (28-bit)
static const uint8_t ATA_CMD_READ_PIO_EXT  = 0x24;  // READ SECTORS EXT (48-bit)
static const uint8_t ATA_CMD_WRITE_PIO     = 0x30;  // WRITE SECTORS (28-bit)
static const uint8_t ATA_CMD_WRITE_PIO_EXT = 0x34;  // WRITE SECTORS EXT (48-bit)
static const uint8_t ATA_CMD_CACHE_FLUSH   = 0xE7;
static const uint8_t ATA_CMD_CACHE_FLUSH_EXT = 0xEA;

struct AtaFlushSupport {
    bool flushCache;
    bool flushCacheExt;
};

static inline AtaFlushSupport ata_flush_support_from_identify_word83(uint16_t word83)
{
    const bool valid = (word83 & 0xC000u) == 0x4000u;
    AtaFlushSupport support;
    support.flushCache = valid && (word83 & (1u << 12)) != 0;
    support.flushCacheExt = valid && (word83 & (1u << 13)) != 0;
    return support;
}


// Port-I/O seam used by the PIO transport and deterministic command tests.
struct AtaFlushIoOps {
    void* context;
    uint8_t (*read8)(void* context, uint16_t port);
    void (*write8)(void* context, uint16_t port, uint8_t value);
};

static inline block::Status ata_flush_command_with_io(
    uint16_t ioBase, uint16_t ctrlBase, bool isMaster,
    bool flushCacheSupported, bool flushCacheExtSupported,
    const AtaFlushIoOps& io, uint32_t pollLimit = 500000u)
{
    if (!flushCacheSupported && !flushCacheExtSupported)
        return block::BLOCK_ERR_UNSUPPORTED;
    if (!io.read8 || !io.write8 || pollLimit == 0)
        return block::BLOCK_ERR_INVALID;

    io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_DRIVE_HEAD),
              isMaster ? 0xA0u : 0xB0u);
    for (uint8_t i = 0; i < 4; ++i)
        (void)io.read8(io.context, ctrlBase);

    bool ready = false;
    for (uint32_t i = 0; i < pollLimit; ++i) {
        const uint8_t status = io.read8(io.context,
            static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
        if (status == 0 || status == 0xFFu) return block::BLOCK_ERR_NO_MEDIA;
        if (status & (ATA_SR_ERR | ATA_SR_DF)) return block::BLOCK_ERR_IO;
        if (!(status & (ATA_SR_BSY | ATA_SR_DRQ)) && (status & ATA_SR_DRDY)) {
            ready = true;
            break;
        }
    }
    if (!ready) return block::BLOCK_ERR_TIMEOUT;

    io.write8(io.context, static_cast<uint16_t>(ioBase + ATA_REG_COMMAND),
        flushCacheExtSupported ? ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH);

    for (uint32_t i = 0; i < pollLimit; ++i) {
        const uint8_t status = io.read8(io.context,
            static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
        if (status == 0 || status == 0xFFu) return block::BLOCK_ERR_NO_MEDIA;
        if (status & (ATA_SR_ERR | ATA_SR_DF)) return block::BLOCK_ERR_IO;
        if (!(status & (ATA_SR_BSY | ATA_SR_DRQ)))
            return (status & ATA_SR_DRDY) ? block::BLOCK_OK : block::BLOCK_ERR_IO;
    }
    return block::BLOCK_ERR_TIMEOUT;
}

// ================================================================
// AHCI register offsets (HBA memory-mapped)
// ================================================================

// Generic Host Control registers (offsets from ABAR)
static const uint32_t AHCI_CAP       = 0x00;
static const uint32_t AHCI_GHC       = 0x04;
static const uint32_t AHCI_IS        = 0x08;
static const uint32_t AHCI_PI        = 0x0C;
static const uint32_t AHCI_VS        = 0x10;

// GHC bits
static const uint32_t AHCI_GHC_AE    = 0x80000000u;  // AHCI Enable
static const uint32_t AHCI_GHC_HR    = 0x00000001u;  // HBA Reset

// Per-port registers (offset from ABAR + 0x100 + port * 0x80)
static const uint32_t AHCI_PxCLB     = 0x00;
static const uint32_t AHCI_PxCLBU    = 0x04;
static const uint32_t AHCI_PxFB      = 0x08;
static const uint32_t AHCI_PxFBU     = 0x0C;
static const uint32_t AHCI_PxIS      = 0x10;
static const uint32_t AHCI_PxIE      = 0x14;
static const uint32_t AHCI_PxCMD     = 0x18;
static const uint32_t AHCI_PxTFD     = 0x20;
static const uint32_t AHCI_PxSIG     = 0x24;
static const uint32_t AHCI_PxSSTS    = 0x28;
static const uint32_t AHCI_PxCI      = 0x38;

// Port CMD bits
static const uint32_t AHCI_PxCMD_ST  = 0x0001;
static const uint32_t AHCI_PxCMD_FRE = 0x0010;
static const uint32_t AHCI_PxCMD_FR  = 0x4000;
static const uint32_t AHCI_PxCMD_CR  = 0x8000;

// SATA device signatures
static const uint32_t SATA_SIG_ATA   = 0x00000101;
static const uint32_t SATA_SIG_ATAPI = 0xEB140101;

// ================================================================
// IDENTIFY DEVICE response (selected fields)
// ================================================================

struct IdentifyData {
    uint16_t generalConfig;       // word 0
    uint16_t reserved1[9];        // words 1-9
    char     serial[20];          // words 10-19
    uint16_t reserved2[3];        // words 20-22
    char     firmware[8];         // words 23-26
    char     model[40];           // words 27-46
    uint16_t reserved3[13];       // words 47-59
    uint32_t lba28Sectors;        // words 60-61
    uint16_t reserved4[21];       // words 62-82
    uint16_t commandSets83;       // word 83 — bit 10 = LBA48 supported
    uint16_t reserved5[16];       // words 84-99
    uint64_t lba48Sectors;        // words 100-103
    uint16_t reserved6[2];        // words 104-105
    uint16_t logicalSectorInfo;   // word 106
    uint16_t reserved7[10];       // words 107-116
    uint32_t logicalSectorWords;  // words 117-118
    uint16_t reserved8[137];      // words 119-255
};

// ================================================================
// ATA device descriptor
// ================================================================

struct ATADevice {
    bool     active;
    uint16_t ioBase;              // I/O port base (PIO)
    uint16_t ctrlBase;            // control port base (PIO)
    uint8_t  isMaster;            // 1 = master, 0 = slave
    bool     isAHCI;              // true = AHCI port, false = PIO
    uint8_t  ahciPort;            // AHCI port number (if AHCI)
    uint64_t abar;                // AHCI Base Address (MMIO)
    bool     lba48;               // supports 48-bit LBA
    bool     flushCache;          // IDENTIFY word 83 advertises FLUSH CACHE
    bool     flushCacheExt;       // IDENTIFY word 83 advertises FLUSH CACHE EXT
    uint64_t totalSectors;
    uint32_t sectorSize;          // almost always 512
    char     model[41];           // null-terminated model string
    char     serial[21];          // null-terminated serial string
    char     name[32];            // human-readable, e.g. "ata0m"
};

static const uint8_t MAX_ATA_DEVICES = 8;

// ================================================================
// Public API
// ================================================================

// Scan for ATA/SATA controllers and register discovered drives
// with the block device layer.
void init();

// Return the number of discovered ATA/SATA drives.
uint8_t device_count();

// Return device info by driver-local index.
const ATADevice* get_device(uint8_t index);

} // namespace ata
} // namespace kernel

#endif // KERNEL_ATA_H
