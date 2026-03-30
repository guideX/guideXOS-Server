// RISC-V 64 Graphics Backend - Implementation
//
// ramfb (fw_cfg) and PCI VGA discovery for RISC-V virt platforms.
//
// On QEMU virt with -device ramfb, the framebuffer is configured
// by writing a RamfbConfig structure through the fw_cfg DMA
// interface.  The guest allocates a contiguous physical region
// for the pixel data and tells QEMU where it is.
//
// On QEMU virt with -device bochs-display or -vga std, the
// framebuffer lives at PCI BAR0 discovered via ECAM.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/graphics.h"
#include "include/arch/riscv64.h"

namespace kernel {
namespace arch {
namespace riscv64 {
namespace graphics {

namespace {

static FramebufferType s_type  = FB_NONE;
static bool     s_available    = false;
static uint64_t s_lfb          = 0;
static uint32_t s_width        = 0;
static uint32_t s_height       = 0;
static uint32_t s_pitch        = 0;
static uint8_t  s_bpp          = 0;

// ================================================================
// MMIO helpers
// ================================================================

static uint8_t mmio_rd8(uint64_t addr)
{
    volatile uint8_t* reg = reinterpret_cast<volatile uint8_t*>(addr);
    return *reg;
}

static uint16_t mmio_rd16(uint64_t addr)
{
    volatile uint16_t* reg = reinterpret_cast<volatile uint16_t*>(addr);
    return *reg;
}

static uint32_t mmio_rd32(uint64_t addr)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(addr);
    return *reg;
}

static void mmio_wr16(uint64_t addr, uint16_t val)
{
    volatile uint16_t* reg = reinterpret_cast<volatile uint16_t*>(addr);
    *reg = val;
}

static void mmio_wr32(uint64_t addr, uint32_t val)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(addr);
    *reg = val;
}

static void mmio_wr64(uint64_t addr, uint64_t val)
{
    volatile uint64_t* reg = reinterpret_cast<volatile uint64_t*>(addr);
    *reg = val;
}

// ================================================================
// Byte-swap helpers (fw_cfg uses big-endian on wire)
// ================================================================

static uint16_t bswap16(uint16_t v)
{
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

static uint32_t bswap32(uint32_t v)
{
    return ((v & 0xFF000000u) >> 24) |
           ((v & 0x00FF0000u) >> 8)  |
           ((v & 0x0000FF00u) << 8)  |
           ((v & 0x000000FFu) << 24);
}

static uint64_t bswap64(uint64_t v)
{
    return (static_cast<uint64_t>(bswap32(static_cast<uint32_t>(v))) << 32) |
            bswap32(static_cast<uint32_t>(v >> 32));
}

// ================================================================
// fw_cfg file directory entry
// ================================================================

struct FwCfgFile {
    uint32_t size;      // big-endian
    uint16_t select;    // big-endian
    uint16_t reserved;
    char     name[56];
};

// ================================================================
// Statically allocated framebuffer memory for ramfb
// 1024 * 768 * 4 = 3 MB.  Placed in BSS (zeroed at boot).
// ================================================================

static uint32_t s_ramfb_memory[1024 * 768] __attribute__((aligned(4096)));

// ramfb fw_cfg DMA access structure (must be 8-byte aligned)
struct FwCfgDmaAccess {
    uint32_t control;   // big-endian
    uint32_t length;    // big-endian
    uint64_t address;   // big-endian
} __attribute__((aligned(8)));

// ================================================================
// fw_cfg helpers
// ================================================================

static void fw_cfg_select(uint16_t key)
{
    mmio_wr16(FW_CFG_SEL, bswap16(key));
}

static uint8_t fw_cfg_read8()
{
    return mmio_rd8(FW_CFG_DATA);
}

static uint32_t fw_cfg_read32()
{
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v = (v << 8) | fw_cfg_read8();
    }
    return v;
}

static void fw_cfg_dma_write(uint16_t key, const void* data, uint32_t len)
{
    static FwCfgDmaAccess dma __attribute__((aligned(8)));
    // control: bit 4 = write, bit 3 = select, bits 16-31 = selector
    dma.control = bswap32((1u << 4) | (1u << 3) |
                           (static_cast<uint32_t>(key) << 16));
    dma.length  = bswap32(len);
    dma.address = bswap64(reinterpret_cast<uint64_t>(data));

    // Write DMA address (big-endian 64-bit) to fw_cfg DMA register
    uint64_t dma_addr = reinterpret_cast<uint64_t>(&dma);
    mmio_wr64(FW_CFG_DMA, bswap64(dma_addr));

    // Poll until DMA completes (control field is zeroed by QEMU)
    volatile uint32_t* ctrl = &dma.control;
    while (*ctrl != 0) {
        asm volatile ("" ::: "memory");
    }
}

// ================================================================
// Find ramfb fw_cfg file entry
// ================================================================

static uint16_t find_ramfb_selector()
{
    fw_cfg_select(FW_CFG_FILE_DIR);
    uint32_t count = fw_cfg_read32();  // big-endian count

    for (uint32_t i = 0; i < count; ++i) {
        FwCfgFile entry;
        // Read entry byte-by-byte from fw_cfg data port
        uint8_t* p = reinterpret_cast<uint8_t*>(&entry);
        for (uint32_t b = 0; b < sizeof(FwCfgFile); ++b) {
            p[b] = fw_cfg_read8();
        }

        // Compare name
        const char* target = "etc/ramfb";
        const char* n = entry.name;
        const char* t = target;
        bool match = true;
        while (*t) {
            if (*n != *t) { match = false; break; }
            ++n; ++t;
        }
        if (match && *n == '\0') {
            return bswap16(entry.select);
        }
    }
    return 0;
}

// ================================================================
// ramfb initialisation
// ================================================================

static bool init_ramfb()
{
    uint16_t sel = find_ramfb_selector();
    if (sel == 0) return false;

    // Configure 1024x768 @ 32-bit XRGB8888
    // DRM_FORMAT_XRGB8888 fourcc = 'XR24' = 0x34325258
    static RamfbConfig cfg __attribute__((aligned(8)));
    cfg.addr   = bswap64(reinterpret_cast<uint64_t>(s_ramfb_memory));
    cfg.fourcc = bswap32(0x34325258u);  // XR24
    cfg.flags  = bswap32(0);
    cfg.width  = bswap32(1024);
    cfg.height = bswap32(768);
    cfg.stride = bswap32(1024 * 4);

    fw_cfg_dma_write(sel, &cfg, sizeof(cfg));

    s_lfb    = reinterpret_cast<uint64_t>(s_ramfb_memory);
    s_width  = 1024;
    s_height = 768;
    s_bpp    = 32;
    s_pitch  = 1024 * 4;
    s_type   = FB_RAMFB;
    s_available = true;
    return true;
}

// ================================================================
// PCI VGA probe via ECAM
// ================================================================

static uint32_t pci_ecam_read32(uint8_t bus, uint8_t dev,
                                 uint8_t func, uint16_t offset)
{
    uint64_t addr = PCI_ECAM_BASE |
                    (static_cast<uint64_t>(bus)  << 20) |
                    (static_cast<uint64_t>(dev)  << 15) |
                    (static_cast<uint64_t>(func) << 12) |
                    (offset & 0xFFC);
    return mmio_rd32(addr);
}

static bool scan_pci_vga()
{
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t dev = 0; dev < 32; ++dev) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t id = pci_ecam_read32(
                    static_cast<uint8_t>(bus), dev, func, 0);
                if (id == 0xFFFFFFFF || id == 0x00000000) continue;

                uint32_t classReg = pci_ecam_read32(
                    static_cast<uint8_t>(bus), dev, func, 0x08);
                uint8_t baseClass = static_cast<uint8_t>(classReg >> 24);
                uint8_t subClass  = static_cast<uint8_t>(classReg >> 16);

                if (baseClass == 0x03 && subClass == 0x00) {
                    // VGA device found - read BAR0
                    uint32_t bar0Lo = pci_ecam_read32(
                        static_cast<uint8_t>(bus), dev, func, 0x10);
                    uint32_t bar0Hi = pci_ecam_read32(
                        static_cast<uint8_t>(bus), dev, func, 0x14);

                    if (!(bar0Lo & 0x01)) {
                        s_lfb = (static_cast<uint64_t>(bar0Hi) << 32) |
                                (bar0Lo & 0xFFFFFFF0u);
                        s_width  = 1024;
                        s_height = 768;
                        s_bpp    = 32;
                        s_pitch  = s_width * 4;
                        s_type   = FB_PCI_VGA;
                        s_available = true;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

} // anonymous namespace

FramebufferType probe()
{
    // Try ramfb first (preferred on QEMU virt)
    if (init_ramfb()) return FB_RAMFB;

    // Fall back to PCI VGA
    if (scan_pci_vga()) return FB_PCI_VGA;

    return FB_NONE;
}

bool init()
{
    s_available = false;
    s_lfb = 0;

    // Try ramfb first, then PCI VGA
    if (init_ramfb()) return true;
    if (scan_pci_vga()) return true;

    return false;
}

FramebufferType get_type()   { return s_type; }
bool is_available()          { return s_available; }
uint64_t get_lfb_address()   { return s_lfb; }
uint32_t get_width()         { return s_width; }
uint32_t get_height()        { return s_height; }
uint32_t get_pitch()         { return s_pitch; }
uint8_t  get_bpp()           { return s_bpp; }

} // namespace graphics
} // namespace riscv64
} // namespace arch
} // namespace kernel
