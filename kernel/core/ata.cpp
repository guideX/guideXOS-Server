// ATA / SATA Storage Driver — Implementation
//
// Scans PCI for IDE controllers (class 01/01) and AHCI controllers
// (class 01/06), identifies attached drives, and registers them
// with the block device layer.
//
// PIO-mode ATA uses port I/O and is only available on x86/amd64.
// AHCI uses MMIO and works on any architecture with PCI.
// On architectures without PCI the driver is a no-op.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/ata.h"
#include "include/kernel/block_device.h"
#include "include/kernel/arch.h"
#include "include/kernel/pit.h"

// Define ARCH_HAS_PORT_IO for x86/amd64 architectures
#if defined(ARCH_X86) || defined(ARCH_AMD64) || defined(__i386__) || defined(__x86_64__)
    #define ARCH_HAS_PORT_IO 1
#else
    #define ARCH_HAS_PORT_IO 0
#endif

// For debug output
#if defined(__GNUC__) || defined(__clang__)
#include "include/kernel/serial_debug.h"
#endif

namespace kernel {
namespace ata {

// ================================================================
// Internal state
// ================================================================

static ATADevice s_devices[MAX_ATA_DEVICES];
static uint8_t   s_deviceCount = 0;
static AtaIoDiagnostic s_ioDiagnostics[MAX_ATA_DEVICES];

// ================================================================
// Helpers
// ================================================================

static void memzero(void* dst, uint32_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < len; ++i) p[i] = 0;
}

static void memcopy(void* dst, const void* src, uint32_t len)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (uint32_t i = 0; i < len; ++i) d[i] = s[i];
}

static void delay_400ns(uint16_t ctrlBase)
{
    (void)ctrlBase;
#if ARCH_HAS_PORT_IO
    // Reading the alternate status register 4 times provides ~400 ns delay
    arch::inb(ctrlBase);
    arch::inb(ctrlBase);
    arch::inb(ctrlBase);
    arch::inb(ctrlBase);
#else
    for (volatile int i = 0; i < 100; ++i) {}
#endif
}

// Swap byte pairs in ATA identify strings (stored as big-endian words)
static void fix_ata_string(char* str, uint32_t len)
{
    for (uint32_t i = 0; i < len; i += 2) {
        char tmp = str[i];
        str[i] = str[i + 1];
        str[i + 1] = tmp;
    }
    // Trim trailing spaces
    int end = static_cast<int>(len) - 1;
    while (end >= 0 && str[end] == ' ') str[end--] = '\0';
    str[len] = '\0';
}

// ================================================================
// PCI configuration (port I/O method — x86/amd64 only)
// ================================================================

#if ARCH_HAS_PORT_IO

static const uint16_t PCI_CONFIG_ADDR = 0x0CF8;
static const uint16_t PCI_CONFIG_DATA = 0x0CFC;

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t addr = 0x80000000u |
                    (static_cast<uint32_t>(bus)  << 16) |
                    (static_cast<uint32_t>(dev)  << 11) |
                    (static_cast<uint32_t>(func) << 8)  |
                    (offset & 0xFC);
    arch::outl(PCI_CONFIG_ADDR, addr);
    return arch::inl(PCI_CONFIG_DATA);
}

// Port-I/O wrappers let the actual PIO state machine run through a deterministic
// seam in tests without mocking the storage service above it.
static uint8_t pio_read8(void*, uint16_t port) { return arch::inb(port); }
static void pio_write8(void*, uint16_t port, uint8_t value) { arch::outb(port, value); }
static uint16_t pio_read16(void*, uint16_t port) { return arch::inw(port); }
static void pio_write16(void*, uint16_t port, uint16_t value) { arch::outw(port, value); }
static void pio_delay400ns(void*, uint16_t ctrlBase) { delay_400ns(ctrlBase); }
static uint64_t pio_ticks(void*) { return pit::ticks(); }

static const AtaPioIoOps s_pioIo = {
    nullptr, pio_read8, pio_write8, pio_read16, pio_write16, pio_delay400ns,
    pio_ticks
};

static bool ata_wait_identify_bsy(uint16_t ioBase, uint32_t timeout)
{
    for (uint32_t i = 0; i < timeout; ++i) {
        const uint8_t status = arch::inb(
            static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
        if (status == 0 || status == 0xFFu ||
            (status & (ATA_SR_ERR | ATA_SR_DF))) return false;
        if ((status & ATA_SR_BSY) == 0) return true;
    }
    return false;
}

static bool ata_wait_identify_drq(uint16_t ioBase, uint32_t timeout)
{
    for (uint32_t i = 0; i < timeout; ++i) {
        const uint8_t status = arch::inb(
            static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
        if (status == 0 || status == 0xFFu ||
            (status & (ATA_SR_ERR | ATA_SR_DF))) return false;
        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ) != 0)
            return true;
    }
    return false;
}

static void reset_io_diagnostic(AtaIoDiagnostic& diagnostic,
                                AtaOperationKind operation,
                                uint64_t lba, uint32_t count)
{
    memzero(&diagnostic, sizeof(diagnostic));
    diagnostic.valid = true;
    diagnostic.operation = operation;
    diagnostic.requestedLba = lba;
    diagnostic.failingLba = lba;
    diagnostic.requestedSectors = count;
    diagnostic.result = block::BLOCK_OK;
}

static bool get_io_diagnostic(uint8_t devIdx,
                              block::TransportIoDiagnostic& out)
{
    memzero(&out, sizeof(out));
    if (devIdx >= MAX_ATA_DEVICES || !s_devices[devIdx].active ||
        !s_ioDiagnostics[devIdx].valid) return false;
    const AtaIoDiagnostic& source = s_ioDiagnostics[devIdx];
    out.valid = source.result != block::BLOCK_OK;
    out.stage = static_cast<uint8_t>(source.stage);
    out.statusRegister = source.statusRegister;
    out.errorRegister = source.errorRegister;
    out.errorRegisterValid = source.errorRegisterValid;
    out.failingLba = source.failingLba;
    out.completedSectors = source.completedSectors;
    out.dataSectorsTransferred = source.dataSectorsTransferred;
    return out.valid;
}

// ================================================================
// ATA PIO — IDENTIFY DEVICE
// ================================================================

static bool ata_identify(uint16_t ioBase, uint16_t ctrlBase,
                         bool master, IdentifyData* id)
{
    uint8_t drv = master ? 0xA0 : 0xB0;
    arch::outb(static_cast<uint16_t>(ioBase + ATA_REG_DRIVE_HEAD), drv);
    delay_400ns(ctrlBase);

    // Zero out the sector count / LBA registers
    arch::outb(static_cast<uint16_t>(ioBase + ATA_REG_SECCOUNT), 0);
    arch::outb(static_cast<uint16_t>(ioBase + ATA_REG_LBA_LO), 0);
    arch::outb(static_cast<uint16_t>(ioBase + ATA_REG_LBA_MID), 0);
    arch::outb(static_cast<uint16_t>(ioBase + ATA_REG_LBA_HI), 0);

    arch::outb(static_cast<uint16_t>(ioBase + ATA_REG_COMMAND), ATA_CMD_IDENTIFY);
    delay_400ns(ctrlBase);

    uint8_t st = arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
    if (st == 0) return false; // no device

    if (!ata_wait_identify_bsy(ioBase, 100000)) return false;

    // Check for ATAPI — LBA_MID/HI become non-zero
    if (arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_LBA_MID)) != 0 ||
        arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_LBA_HI))  != 0) {
        return false; // ATAPI or SATA — skip for PIO driver
    }

    if (!ata_wait_identify_drq(ioBase, 100000)) return false;

    // Read 256 words (512 bytes)
    uint16_t* buf = reinterpret_cast<uint16_t*>(id);
    for (int i = 0; i < 256; ++i) {
        buf[i] = arch::inw(static_cast<uint16_t>(ioBase + ATA_REG_DATA));
    }
    return true;
}

// ================================================================
// ATA PIO — read sectors (28-bit LBA)
// ================================================================

static block::Status ata_pio_read(uint8_t devIdx,
                                  uint64_t lba,
                                  uint32_t count,
                                  void* buffer)
{
    if (devIdx >= MAX_ATA_DEVICES || !s_devices[devIdx].active)
        return block::BLOCK_ERR_INVALID;

    ATADevice& dev = s_devices[devIdx];
    uint8_t* buf = static_cast<uint8_t*>(buffer);
    reset_io_diagnostic(s_ioDiagnostics[devIdx], ATA_OPERATION_READ, lba, count);

    for (uint32_t sec = 0; sec < count; ++sec) {
        uint64_t curLBA = lba + sec;
        const block::Status status = ata_pio_transfer_sector_with_io(
            dev.ioBase, dev.ctrlBase, dev.isMaster != 0, dev.lba48,
            dev.sectorSize, curLBA, false,
            buf + static_cast<size_t>(sec) * dev.sectorSize,
            s_pioIo, s_ioDiagnostics[devIdx]);
        if (status != block::BLOCK_OK) return status;
    }
    return block::BLOCK_OK;
}

// ================================================================
// ATA PIO — write sectors (28-bit LBA)
// ================================================================

static block::Status ata_pio_write(uint8_t devIdx,
                                   uint64_t lba,
                                   uint32_t count,
                                   const void* buffer)
{
    if (devIdx >= MAX_ATA_DEVICES || !s_devices[devIdx].active)
        return block::BLOCK_ERR_INVALID;

    ATADevice& dev = s_devices[devIdx];
    const uint8_t* buf = static_cast<const uint8_t*>(buffer);
    reset_io_diagnostic(s_ioDiagnostics[devIdx], ATA_OPERATION_WRITE, lba, count);

    for (uint32_t sec = 0; sec < count; ++sec) {
        uint64_t curLBA = lba + sec;
        const block::Status status = ata_pio_transfer_sector_with_io(
            dev.ioBase, dev.ctrlBase, dev.isMaster != 0, dev.lba48,
            dev.sectorSize, curLBA, true,
            const_cast<uint8_t*>(buf + static_cast<size_t>(sec) * dev.sectorSize),
            s_pioIo, s_ioDiagnostics[devIdx]);
        if (status != block::BLOCK_OK) return status;
    }

    return block::BLOCK_OK;
}

// Cache flush is deliberately separate from a block write. FAT commits call
// this after data and metadata are complete, instead of paying for a device
// cache flush after every cluster and FAT-entry update.
static uint8_t ata_flush_read8(void*, uint16_t port)
{
    return arch::inb(port);
}

static void ata_flush_write8(void*, uint16_t port, uint8_t value)
{
    arch::outb(port, value);
}

static uint64_t ata_flush_ticks(void*) { return pit::ticks(); }

static block::Status ata_pio_flush(uint8_t devIdx)
{
    if (devIdx >= MAX_ATA_DEVICES || !s_devices[devIdx].active)
        return block::BLOCK_ERR_INVALID;

    ATADevice& dev = s_devices[devIdx];
    reset_io_diagnostic(s_ioDiagnostics[devIdx], ATA_OPERATION_FLUSH, 0, 0);
    const AtaFlushIoOps io = {
        nullptr, ata_flush_read8, ata_flush_write8, ata_flush_ticks
    };
    return ata_flush_command_with_io(dev.ioBase, dev.ctrlBase,
        dev.isMaster != 0, dev.flushCache, dev.flushCacheExt, io,
        500000u, &s_ioDiagnostics[devIdx]);
}

// ================================================================
// Probe a single ATA channel (master + slave)
// ================================================================

static void probe_channel(uint16_t ioBase, uint16_t ctrlBase,
                          const char* prefix, uint8_t chanIdx,
                          bool pciLocationValid = false,
                          uint8_t pciBus = 0, uint8_t pciDevice = 0,
                          uint8_t pciFunction = 0)
{
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[ATA] Probing channel ");
    serial::puts(prefix);
    serial::puts(" (ioBase=0x");
    serial::put_hex16(ioBase);
    serial::puts(")\n");
#endif

    for (uint8_t drive = 0; drive < 2; ++drive) {
        if (s_deviceCount >= MAX_ATA_DEVICES) return;

        IdentifyData id;
        memzero(&id, sizeof(id));

        bool isMaster = (drive == 0);
        
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[ATA]   Checking ");
        serial::puts(isMaster ? "master" : "slave");
        serial::puts("...\n");
#endif

        if (!ata_identify(ioBase, ctrlBase, isMaster, &id)) {
#if defined(__GNUC__) || defined(__clang__)
            serial::puts("[ATA]   No device\n");
#endif
            continue;
        }

#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[ATA]   Found device!\n");
#endif

        ATADevice& dev = s_devices[s_deviceCount];
        memzero(&dev, sizeof(dev));
        dev.active     = true;
        dev.ioBase     = ioBase;
        dev.ctrlBase   = ctrlBase;
        dev.isMaster   = isMaster ? 1 : 0;
        dev.isAHCI     = false;
        dev.sectorSize = 512;
        if ((id.logicalSectorInfo & 0xC000u) == 0x4000u &&
            (id.logicalSectorInfo & 0x1000u) != 0) {
            const uint64_t logicalBytes = static_cast<uint64_t>(id.logicalSectorWords) * 2;
            if (logicalBytes < 512 || logicalBytes > 4096 ||
                (logicalBytes & (logicalBytes - 1)) != 0) {
#if defined(__GNUC__) || defined(__clang__)
                serial::puts("[ATA] Skipping device with unsupported logical sector size\n");
#endif
                continue;
            }
            dev.sectorSize = static_cast<uint32_t>(logicalBytes);
        }

        // Word 83's support bits are meaningful only when bits 15:14 mark
        // the word valid. Cache flush support is recorded per IDENTIFY data.
        const bool commandSets83Valid = (id.commandSets83 & 0xC000u) == 0x4000u;
        const AtaFlushSupport flushSupport =
            ata_flush_support_from_identify_word83(id.commandSets83);
        dev.lba48 = commandSets83Valid && (id.commandSets83 & (1 << 10)) != 0;
        dev.identifyCommandSets83 = id.commandSets83;
        dev.flushCache = flushSupport.flushCache;
        dev.flushCacheExt = flushSupport.flushCacheExt;

        if (dev.lba48) {
            dev.totalSectors = id.lba48Sectors;
        } else {
            dev.totalSectors = id.lba28Sectors;
        }

        // Copy and fix model / serial strings
        memcopy(dev.model, id.model, 40);
        fix_ata_string(dev.model, 40);
        memcopy(dev.serial, id.serial, 20);
        fix_ata_string(dev.serial, 20);

        // Build device name: "ata0m", "ata0s", "ata1m", "ata1s"
        dev.name[0] = 'a'; dev.name[1] = 't'; dev.name[2] = 'a';
        dev.name[3] = static_cast<char>('0' + chanIdx);
        dev.name[4] = isMaster ? 'm' : 's';
        dev.name[5] = '\0';

        // Register with block layer
        block::BlockDevice bdev;
        memzero(&bdev, sizeof(bdev));
        bdev.active       = true;
        bdev.type         = block::BDEV_ATA_PIO;
        bdev.bootProvenance = block::BOOT_PROVENANCE_UNKNOWN;
        bdev.driverIndex  = s_deviceCount;
        bdev.totalSectors = dev.totalSectors;
        bdev.sectorSize   = dev.sectorSize;
        bdev.readFn       = ata_pio_read;
        bdev.writeFn      = ata_pio_write;
        bdev.flushFn      = (dev.flushCache || dev.flushCacheExt)
            ? ata_pio_flush : nullptr;
        bdev.flushSemanticsKnown = bdev.flushFn != nullptr;
        bdev.pciLocationValid = pciLocationValid;
        bdev.pciSegment = 0; // Legacy PCI config mechanism addresses segment 0 only.
        bdev.pciBus = pciBus;
        bdev.pciDevice = pciDevice;
        bdev.pciFunction = pciFunction;
        bdev.ataTargetValid = true;
        bdev.ataChannel = chanIdx;
        bdev.ataTarget = drive;
        bdev.getIoDiagnosticFn = get_io_diagnostic;
        memcopy(bdev.name, dev.name, 6);
        memcopy(bdev.model, dev.model, sizeof(dev.model) - 1);
        memcopy(bdev.serial, dev.serial, sizeof(dev.serial) - 1);

        if (block::register_device(bdev) == 0xFF) {
            memzero(&dev, sizeof(dev));
            continue;
        }
        ++s_deviceCount;
    }

    (void)prefix; // used for debug output in future
}

// ================================================================
// PCI scan for IDE controllers (class 01 / subclass 01)
// ================================================================

static bool scan_pci_ide()
{
    bool found = false;
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t id = pci_read32(static_cast<uint8_t>(bus), dev, func, 0);
                if (id == 0xFFFFFFFF) continue;

                uint32_t classReg = pci_read32(static_cast<uint8_t>(bus), dev, func, 0x08);
                uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                uint8_t subClass  = static_cast<uint8_t>(classReg >> 16);
                uint8_t progIf    = static_cast<uint8_t>(classReg >> 8);

                if (baseClass == 0x01 && subClass == 0x01) {
                    // The legacy port ranges are authoritative only for IDE
                    // channels still in PCI compatibility mode.
                    if ((progIf & 0x01u) == 0)
                        probe_channel(ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, "pri", 0,
                                      true, static_cast<uint8_t>(bus), dev, func);
                    if ((progIf & 0x04u) == 0)
                        probe_channel(ATA_SECONDARY_IO, ATA_SECONDARY_CTRL, "sec", 1,
                                      true, static_cast<uint8_t>(bus), dev, func);
                    found = true;
                    return found; // one IDE controller is enough for now
                }
            }
        }
    }
    return found;
}

#endif // ARCH_HAS_PORT_IO

// ================================================================
// Public API
// ================================================================

void init()
{
    memzero(s_devices, sizeof(s_devices));
    s_deviceCount = 0;

#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[ATA] Initializing ATA driver...\n");
#endif

#if ARCH_HAS_PORT_IO
    // x86 / amd64: scan PCI for IDE controllers, probe ATA PIO
    bool foundIDE = scan_pci_ide();
    
#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[ATA] PCI scan ");
    serial::puts(foundIDE ? "found" : "did not find");
    serial::puts(" IDE controller\n");
#endif
    
    // If no IDE controller found via PCI, try probing standard ports anyway
    // This handles cases like QEMU's ISA IDE or legacy systems
    if (!foundIDE) {
#if defined(__GNUC__) || defined(__clang__)
        serial::puts("[ATA] Trying standard IDE ports...\n");
#endif
        // Try standard IDE ports directly
        probe_channel(ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, "pri", 0);
        probe_channel(ATA_SECONDARY_IO, ATA_SECONDARY_CTRL, "sec", 1);
    }
#endif

#if defined(__GNUC__) || defined(__clang__)
    serial::puts("[ATA] Init complete. Devices: ");
    serial::putc('0' + s_deviceCount);
    serial::puts("\n");
#endif

    // AHCI support (MMIO-based, all architectures with PCI) would
    // be added here in a future iteration: scan PCI for class 01/06,
    // map ABAR, enumerate ports, send IDENTIFY via FIS.
}

uint8_t device_count()
{
    return s_deviceCount;
}

const char* ata_operation_stage_name(AtaOperationStage stage)
{
    switch (stage) {
        case ATA_STAGE_SELECT_DEVICE: return "SelectDevice";
        case ATA_STAGE_WAIT_READY: return "WaitReady";
        case ATA_STAGE_PROGRAM_LBA: return "ProgramLBA";
        case ATA_STAGE_ISSUE_COMMAND: return "IssueCommand";
        case ATA_STAGE_WAIT_DRQ: return "WaitDRQ";
        case ATA_STAGE_TRANSFER_DATA: return "TransferData";
        case ATA_STAGE_WAIT_COMPLETION: return "WaitCompletion";
        case ATA_STAGE_FLUSH_WAIT_READY: return "FlushWaitReady";
        case ATA_STAGE_FLUSH_ISSUE: return "FlushIssue";
        case ATA_STAGE_FLUSH_WAIT_COMPLETION: return "FlushWaitCompletion";
        case ATA_STAGE_NONE: default: return "None";
    }
}

const ATADevice* get_device(uint8_t index)
{
    if (index >= MAX_ATA_DEVICES) return nullptr;
    if (!s_devices[index].active) return nullptr;
    return &s_devices[index];
}

} // namespace ata
} // namespace kernel
