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

// ================================================================
// ATA PIO — wait for BSY clear, DRQ set, or error
// ================================================================

static bool ata_wait_bsy(uint16_t ioBase, uint32_t timeout)
{
    for (uint32_t i = 0; i < timeout; ++i) {
        uint8_t st = arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
        if (!(st & ATA_SR_BSY)) return true;
    }
    return false;
}

static bool ata_wait_drq(uint16_t ioBase, uint32_t timeout)
{
    for (uint32_t i = 0; i < timeout; ++i) {
        uint8_t st = arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_STATUS));
        if (st & ATA_SR_ERR) return false;
        if (st & ATA_SR_DF)  return false;
        if (st & ATA_SR_DRQ) return true;
    }
    return false;
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

    if (!ata_wait_bsy(ioBase, 100000)) return false;

    // Check for ATAPI — LBA_MID/HI become non-zero
    if (arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_LBA_MID)) != 0 ||
        arch::inb(static_cast<uint16_t>(ioBase + ATA_REG_LBA_HI))  != 0) {
        return false; // ATAPI or SATA — skip for PIO driver
    }

    if (!ata_wait_drq(ioBase, 100000)) return false;

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

    for (uint32_t sec = 0; sec < count; ++sec) {
        uint64_t curLBA = lba + sec;

        if (dev.lba48 && curLBA >= 0x10000000ULL) {
            // 48-bit LBA
            uint8_t drv = dev.isMaster ? 0x40 : 0x50;
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_DRIVE_HEAD), drv);
            delay_400ns(dev.ctrlBase);

            // High bytes first
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_SECCOUNT),
                       0); // sector count high = 0
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_LO),
                       static_cast<uint8_t>(curLBA >> 24));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_MID),
                       static_cast<uint8_t>(curLBA >> 32));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_HI),
                       static_cast<uint8_t>(curLBA >> 40));

            // Low bytes
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_SECCOUNT), 1);
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_LO),
                       static_cast<uint8_t>(curLBA));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_MID),
                       static_cast<uint8_t>(curLBA >> 8));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_HI),
                       static_cast<uint8_t>(curLBA >> 16));

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_COMMAND),
                       ATA_CMD_READ_PIO_EXT);
        } else {
            // 28-bit LBA
            uint8_t drv = (dev.isMaster ? 0xE0 : 0xF0) |
                          static_cast<uint8_t>((curLBA >> 24) & 0x0F);
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_DRIVE_HEAD), drv);
            delay_400ns(dev.ctrlBase);

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_SECCOUNT), 1);
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_LO),
                       static_cast<uint8_t>(curLBA));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_MID),
                       static_cast<uint8_t>(curLBA >> 8));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_HI),
                       static_cast<uint8_t>(curLBA >> 16));

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_COMMAND),
                       ATA_CMD_READ_PIO);
        }

        if (!ata_wait_drq(dev.ioBase, 500000))
            return block::BLOCK_ERR_TIMEOUT;

        uint16_t* wbuf = reinterpret_cast<uint16_t*>(buf + sec * dev.sectorSize);
        for (uint32_t w = 0; w < dev.sectorSize / 2; ++w) {
            wbuf[w] = arch::inw(static_cast<uint16_t>(dev.ioBase + ATA_REG_DATA));
        }
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

    for (uint32_t sec = 0; sec < count; ++sec) {
        uint64_t curLBA = lba + sec;

        if (dev.lba48 && curLBA >= 0x10000000ULL) {
            uint8_t drv = dev.isMaster ? 0x40 : 0x50;
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_DRIVE_HEAD), drv);
            delay_400ns(dev.ctrlBase);

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_SECCOUNT), 0);
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_LO),
                       static_cast<uint8_t>(curLBA >> 24));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_MID),
                       static_cast<uint8_t>(curLBA >> 32));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_HI),
                       static_cast<uint8_t>(curLBA >> 40));

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_SECCOUNT), 1);
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_LO),
                       static_cast<uint8_t>(curLBA));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_MID),
                       static_cast<uint8_t>(curLBA >> 8));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_HI),
                       static_cast<uint8_t>(curLBA >> 16));

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_COMMAND),
                       ATA_CMD_WRITE_PIO_EXT);
        } else {
            uint8_t drv = (dev.isMaster ? 0xE0 : 0xF0) |
                          static_cast<uint8_t>((curLBA >> 24) & 0x0F);
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_DRIVE_HEAD), drv);
            delay_400ns(dev.ctrlBase);

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_SECCOUNT), 1);
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_LO),
                       static_cast<uint8_t>(curLBA));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_MID),
                       static_cast<uint8_t>(curLBA >> 8));
            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_LBA_HI),
                       static_cast<uint8_t>(curLBA >> 16));

            arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_COMMAND),
                       ATA_CMD_WRITE_PIO);
        }

        if (!ata_wait_drq(dev.ioBase, 500000))
            return block::BLOCK_ERR_TIMEOUT;

        const uint16_t* wbuf = reinterpret_cast<const uint16_t*>(
            buf + sec * dev.sectorSize);
        for (uint32_t w = 0; w < dev.sectorSize / 2; ++w) {
            arch::outw(static_cast<uint16_t>(dev.ioBase + ATA_REG_DATA), wbuf[w]);
        }

        // Flush cache
        arch::outb(static_cast<uint16_t>(dev.ioBase + ATA_REG_COMMAND),
                   dev.lba48 ? ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH);
        if (!ata_wait_bsy(dev.ioBase, 500000))
            return block::BLOCK_ERR_TIMEOUT;
    }
    return block::BLOCK_OK;
}

// ================================================================
// Probe a single ATA channel (master + slave)
// ================================================================

static void probe_channel(uint16_t ioBase, uint16_t ctrlBase,
                          const char* prefix, uint8_t chanIdx)
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

        // LBA48 support: word 83 bit 10
        dev.lba48 = (id.commandSets83 & (1 << 10)) != 0;

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
        bdev.driverIndex  = s_deviceCount;
        bdev.totalSectors = dev.totalSectors;
        bdev.sectorSize   = dev.sectorSize;
        bdev.readFn       = ata_pio_read;
        bdev.writeFn      = ata_pio_write;
        memcopy(bdev.name, dev.name, 6);

        block::register_device(bdev);
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

                if (baseClass == 0x01 && subClass == 0x01) {
                    // IDE controller found — use standard channel ports
                    probe_channel(ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, "pri", 0);
                    probe_channel(ATA_SECONDARY_IO, ATA_SECONDARY_CTRL, "sec", 1);
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

const ATADevice* get_device(uint8_t index)
{
    if (index >= MAX_ATA_DEVICES) return nullptr;
    if (!s_devices[index].active) return nullptr;
    return &s_devices[index];
}

} // namespace ata
} // namespace kernel
