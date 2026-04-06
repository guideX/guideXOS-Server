//
// LoongArch 64-bit Graphics Backend - Implementation
//
// ramfb (fw_cfg) and PCI VGA discovery for LoongArch virt platforms.
//
// On QEMU virt with -device ramfb, the framebuffer is configured
// by writing a RamfbConfig structure through the fw_cfg DMA
// interface. The guest allocates a contiguous physical region
// for the pixel data and tells QEMU where it is.
//
// On QEMU virt with -device bochs-display or PCI VGA, the
// framebuffer lives at PCI BAR0 discovered via ECAM.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/graphics.h"
#include "include/arch/loongarch64.h"

namespace kernel {
namespace arch {
namespace loongarch64 {
namespace graphics {

namespace {

// ================================================================
// Static state
// ================================================================

static FramebufferType s_type       = FB_NONE;
static bool            s_available  = false;
static uint64_t        s_lfb        = 0;
static uint32_t        s_width      = 0;
static uint32_t        s_height     = 0;
static uint32_t        s_pitch      = 0;
static uint8_t         s_bpp        = 0;

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

static void mmio_wr8(uint64_t addr, uint8_t val)
{
    volatile uint8_t* reg = reinterpret_cast<volatile uint8_t*>(addr);
    *reg = val;
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
    return ((v >> 24) & 0x000000FF) |
           ((v >> 8)  & 0x0000FF00) |
           ((v << 8)  & 0x00FF0000) |
           ((v << 24) & 0xFF000000);
}

static uint64_t bswap64(uint64_t v)
{
    uint32_t hi = static_cast<uint32_t>(v >> 32);
    uint32_t lo = static_cast<uint32_t>(v);
    return (static_cast<uint64_t>(bswap32(lo)) << 32) | bswap32(hi);
}

// ================================================================
// fw_cfg helpers
// ================================================================

// fw_cfg file directory entry
struct FwCfgFile {
    uint32_t size;
    uint16_t select;
    uint16_t reserved;
    char     name[56];
};

// fw_cfg DMA access structure
struct FwCfgDmaAccess {
    uint32_t control;
    uint32_t length;
    uint64_t address;
};

static const uint32_t FW_CFG_DMA_CTL_ERROR  = (1U << 0);
static const uint32_t FW_CFG_DMA_CTL_READ   = (1U << 1);
static const uint32_t FW_CFG_DMA_CTL_SKIP   = (1U << 2);
static const uint32_t FW_CFG_DMA_CTL_SELECT = (1U << 3);
static const uint32_t FW_CFG_DMA_CTL_WRITE  = (1U << 4);

// Selector for DMA operations
static void fw_cfg_select(uint16_t key)
{
    mmio_wr16(FW_CFG_SEL, bswap16(key));
}

// Read data from fw_cfg (byte-by-byte from DATA port)
static void fw_cfg_read_bytes(void* buf, uint32_t len)
{
    uint8_t* p = reinterpret_cast<uint8_t*>(buf);
    for (uint32_t i = 0; i < len; ++i) {
        p[i] = mmio_rd8(FW_CFG_DATA);
    }
}

// Check if fw_cfg is present
static bool fw_cfg_present()
{
    fw_cfg_select(FW_CFG_SIGNATURE);
    uint8_t sig[4];
    fw_cfg_read_bytes(sig, 4);
    // Signature should be "QEMU" in little-endian
    return (sig[0] == 'Q' && sig[1] == 'E' && sig[2] == 'M' && sig[3] == 'U');
}

// Find a fw_cfg file by name, return selector or 0 if not found
static uint16_t fw_cfg_find_file(const char* name)
{
    fw_cfg_select(FW_CFG_FILE_DIR);
    
    uint32_t count_be;
    fw_cfg_read_bytes(&count_be, 4);
    uint32_t count = bswap32(count_be);
    
    for (uint32_t i = 0; i < count; ++i) {
        FwCfgFile entry;
        fw_cfg_read_bytes(&entry, sizeof(entry));
        
        // Compare name (null-terminated)
        bool match = true;
        for (int j = 0; j < 56 && name[j] != '\0'; ++j) {
            if (entry.name[j] != name[j]) {
                match = false;
                break;
            }
            if (name[j] == '\0') break;
        }
        
        if (match) {
            return bswap16(entry.select);
        }
    }
    
    return 0;
}

// ================================================================
// ramfb initialization
// ================================================================

// Static framebuffer memory for ramfb (4MB, should be in uncached region)
// TODO: Allocate from kernel memory allocator with proper caching attributes
#if defined(_MSC_VER)
__declspec(align(4096)) static uint8_t s_ramfb_memory[4 * 1024 * 1024];
#else
static uint8_t s_ramfb_memory[4 * 1024 * 1024] __attribute__((aligned(4096)));
#endif

static bool init_ramfb()
{
    // Check if fw_cfg is present
    if (!fw_cfg_present()) {
        return false;
    }
    
    // Find ramfb configuration file
    uint16_t ramfb_sel = fw_cfg_find_file("etc/ramfb");
    if (ramfb_sel == 0) {
        return false;
    }
    
    // Configure ramfb
    // Default: 1024x768, 32bpp BGRA
    s_width  = 1024;
    s_height = 768;
    s_bpp    = 32;
    s_pitch  = s_width * (s_bpp / 8);
    
    // Get physical address of framebuffer
    // TODO: Convert virtual to physical address properly
    s_lfb = reinterpret_cast<uint64_t>(s_ramfb_memory);
    
    // Prepare ramfb configuration (big-endian)
    RamfbConfig cfg;
    cfg.addr   = bswap64(s_lfb);
    cfg.fourcc = bswap32(0x34325241);  // 'AR24' = BGRA8888
    cfg.flags  = 0;
    cfg.width  = bswap32(s_width);
    cfg.height = bswap32(s_height);
    cfg.stride = bswap32(s_pitch);
    
    // Write configuration via fw_cfg DMA
    // TODO: Implement DMA write (for now, use direct write if supported)
    fw_cfg_select(ramfb_sel);
    
    // Write config bytes (simple write without DMA)
    // Note: Full DMA implementation needed for proper operation
    volatile uint8_t* data = reinterpret_cast<volatile uint8_t*>(FW_CFG_DATA);
    const uint8_t* cfgp = reinterpret_cast<const uint8_t*>(&cfg);
    for (uint32_t i = 0; i < sizeof(cfg); ++i) {
        *data = cfgp[i];
    }
    
    s_type = FB_RAMFB;
    s_available = true;
    
    return true;
}

// ================================================================
// PCI VGA probe
// ================================================================

static bool probe_pci_vga()
{
    // Scan PCI bus 0 for VGA-compatible devices
    // PCI class code 0x03 = display controller
    // Subclass 0x00 = VGA compatible
    
    for (uint32_t dev = 0; dev < 32; ++dev) {
        for (uint32_t func = 0; func < 8; ++func) {
            uint64_t cfg_addr = PCI_ECAM_BASE + 
                               ((0ULL << 20) |      // Bus 0
                                (dev << 15) |       // Device
                                (func << 12));      // Function
            
            uint32_t vendor_device = mmio_rd32(cfg_addr + 0x00);
            if (vendor_device == 0xFFFFFFFF || vendor_device == 0x00000000) {
                if (func == 0) break;  // No device, skip remaining functions
                continue;
            }
            
            uint32_t class_rev = mmio_rd32(cfg_addr + 0x08);
            uint8_t class_code = (class_rev >> 24) & 0xFF;
            uint8_t subclass = (class_rev >> 16) & 0xFF;
            
            // Check for display controller
            if (class_code == 0x03) {
                // Found a display device
                uint32_t bar0 = mmio_rd32(cfg_addr + 0x10);
                
                if ((bar0 & 0x1) == 0) {
                    // Memory BAR
                    uint64_t fb_base = bar0 & ~0xFULL;
                    
                    // For 64-bit BAR, read high 32 bits
                    if ((bar0 & 0x6) == 0x4) {
                        uint32_t bar1 = mmio_rd32(cfg_addr + 0x14);
                        fb_base |= (static_cast<uint64_t>(bar1) << 32);
                    }
                    
                    // TODO: Enable memory space in command register
                    uint16_t cmd = mmio_rd16(cfg_addr + 0x04);
                    cmd |= 0x02;  // Memory Space Enable
                    mmio_wr16(cfg_addr + 0x04, cmd);
                    
                    // Set default mode (bochs-display style)
                    // TODO: Program display controller registers
                    s_lfb = fb_base;
                    s_width = 1024;
                    s_height = 768;
                    s_bpp = 32;
                    s_pitch = s_width * 4;
                    
                    s_type = FB_PCI_VGA;
                    s_available = true;
                    
                    return true;
                }
            }
            
            // Check if multi-function device
            if (func == 0) {
                uint32_t header = mmio_rd32(cfg_addr + 0x0C);
                if ((header & 0x00800000) == 0) {
                    break;  // Single-function device
                }
            }
        }
    }
    
    return false;
}

} // anonymous namespace

// ================================================================
// Public API implementation
// ================================================================

FramebufferType probe()
{
    // Try ramfb first (simpler, always works with QEMU)
    if (init_ramfb()) {
        return FB_RAMFB;
    }
    
    // Try PCI VGA
    if (probe_pci_vga()) {
        return FB_PCI_VGA;
    }
    
    // TODO: Probe Loongson integrated DC on real hardware
    
    return FB_NONE;
}

bool init()
{
    if (s_available) {
        // Already initialized
        return true;
    }
    
    FramebufferType type = probe();
    return (type != FB_NONE);
}

FramebufferType get_type()
{
    return s_type;
}

bool is_available()
{
    return s_available;
}

uint64_t get_lfb_address()
{
    return s_lfb;
}

uint32_t get_width()
{
    return s_width;
}

uint32_t get_height()
{
    return s_height;
}

uint32_t get_pitch()
{
    return s_pitch;
}

uint8_t get_bpp()
{
    return s_bpp;
}

void put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!s_available || x >= s_width || y >= s_height) {
        return;
    }
    
    uint64_t offset = y * s_pitch + x * (s_bpp / 8);
    volatile uint32_t* pixel = reinterpret_cast<volatile uint32_t*>(s_lfb + offset);
    *pixel = color;
}

void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (!s_available) {
        return;
    }
    
    // Clamp to screen bounds
    if (x >= s_width || y >= s_height) return;
    if (x + w > s_width) w = s_width - x;
    if (y + h > s_height) h = s_height - y;
    
    for (uint32_t row = 0; row < h; ++row) {
        uint64_t offset = (y + row) * s_pitch + x * (s_bpp / 8);
        volatile uint32_t* line = reinterpret_cast<volatile uint32_t*>(s_lfb + offset);
        for (uint32_t col = 0; col < w; ++col) {
            line[col] = color;
        }
    }
}

void clear_screen(uint32_t color)
{
    fill_rect(0, 0, s_width, s_height, color);
}

} // namespace graphics
} // namespace loongarch64
} // namespace arch
} // namespace kernel
