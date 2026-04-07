//
// Framebuffer driver implementation
//
// Supports both Multiboot (legacy BIOS) and BootInfo (UEFI) initialization
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/framebuffer.h"
#include "include/kernel/arch.h"

#if ARCH_HAS_PIC_8259
#include "include/kernel/multiboot.h"
// Include BootInfo for UEFI boot support
#include "../../guideXOSBootLoader/guidexOSBootInfo.h"
#endif

namespace kernel {
namespace framebuffer {

static uint32_t* g_buffer = nullptr;      // Front buffer (video memory)
static uint32_t* g_backBuffer = nullptr;  // Back buffer (off-screen)
static uint32_t* g_drawTarget = nullptr;  // Current draw target (back or front buffer)
static uint32_t g_width = 0;
static uint32_t g_height = 0;
static uint32_t g_pitch = 0;
static uint8_t g_bpp = 0;
static bool g_available = false;
static bool g_doubleBuffered = false;

// Static back buffer storage (allocated in BSS segment)
// Max resolution support: 1920x1080 = 2,073,600 pixels * 4 bytes = ~8MB
// For kernel mode, we use a static buffer to avoid dynamic allocation
static const uint32_t MAX_BACKBUFFER_PIXELS = 1920 * 1080;
static uint32_t g_backBufferStorage[MAX_BACKBUFFER_PIXELS];

#if ARCH_HAS_PIC_8259

bool init(void* multiboot_info_ptr)
{
    auto* info = reinterpret_cast<multiboot::Info*>(multiboot_info_ptr);
    
    // Check if framebuffer info is available
    if (!(info->flags & multiboot::INFO_FRAMEBUFFER)) {
        return false;
    }
    
    // Check framebuffer type (we want RGB)
    if (info->framebuffer_type != multiboot::FRAMEBUFFER_TYPE_RGB) {
        return false;
    }
    
    // Get framebuffer info
    g_buffer = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(info->framebuffer_addr));
    g_width = info->framebuffer_width;
    g_height = info->framebuffer_height;
    g_pitch = info->framebuffer_pitch;
    g_bpp = info->framebuffer_bpp;
    
    // Validate
    if (!g_buffer || g_width == 0 || g_height == 0) {
        return false;
    }
    
    g_drawTarget = g_buffer;  // Draw directly to video memory by default
    g_available = true;
    return true;
}

bool init_from_bootinfo(const guideXOS::BootInfo* bootinfo)
{
    if (!bootinfo) {
        return false;
    }
    
    // Check if framebuffer is valid in BootInfo
    if (!(bootinfo->Flags & (1u << 1))) {
        return false;  // Framebuffer not available
    }
    
    // Validate framebuffer data
    if (bootinfo->FramebufferBase == 0 || bootinfo->FramebufferSize == 0) {
        return false;
    }
    
    if (bootinfo->FramebufferWidth == 0 || bootinfo->FramebufferHeight == 0) {
        return false;
    }
    
    // Initialize framebuffer from BootInfo
    g_buffer = reinterpret_cast<uint32_t*>(bootinfo->FramebufferBase);
    g_width = bootinfo->FramebufferWidth;
    g_height = bootinfo->FramebufferHeight;
    g_pitch = bootinfo->FramebufferPitch;
    g_bpp = 32;  // BootInfo always uses 32-bit format
    
    g_drawTarget = g_buffer;  // Draw directly to video memory by default
    g_available = true;
    return true;
}

#else // !ARCH_HAS_PIC_8259

bool init(void*) { return false; }

#if defined(ARCH_SPARC)

// Sun4m TCX framebuffer initialisation.
// On QEMU SS-5 the 24-bit direct-colour plane lives at 0x50800000.
// Resolution defaults to 1024x768, 32-bit XRGB pixels.
bool init_sun4m()
{
    g_buffer = reinterpret_cast<uint32_t*>(0x50800000u);
    g_width  = 1024;
    g_height = 768;
    g_pitch  = 1024 * 4;   // 4 bytes per pixel, no padding
    g_bpp    = 32;
    g_drawTarget = g_buffer;  // Draw directly to video memory by default
    g_available = true;
    return true;
}

bool init_sun4u() { return false; }

#elif defined(ARCH_SPARC64)

// Sun4u PCI VGA framebuffer initialisation.
// QEMU sun4u with -vga std maps the linear framebuffer at
// PCI BAR0, typically 0x80000000.  Default: 1024x768, 32-bit XRGB.
bool init_sun4m() { return false; }

bool init_sun4u()
{
    g_buffer = reinterpret_cast<uint32_t*>(0x80000000ULL);
    g_width  = 1024;
    g_height = 768;
    g_pitch  = 1024 * 4;
    g_bpp    = 32;
    g_drawTarget = g_buffer;  // Draw directly to video memory by default
    g_available = true;
    return true;
}

#else

bool init_sun4m() { return false; }
bool init_sun4u() { return false; }

#endif

// ================================================================
// RISC-V ramfb init
// ================================================================

#if defined(ARCH_RISCV64)
bool init_riscv_ramfb(uint64_t lfbBase, uint32_t width, uint32_t height,
                      uint32_t pitch, uint8_t bpp)
{
    return init_manual(lfbBase, width, height, pitch, bpp);
}
#else
bool init_riscv_ramfb(uint64_t, uint32_t, uint32_t, uint32_t, uint8_t) { return false; }
#endif

#endif // ARCH_HAS_PIC_8259

// ================================================================
// VESA / BGA init (x86 / amd64)
// ================================================================

bool init_vesa(uint16_t width, uint16_t height, uint8_t bpp)
{
#if ARCH_HAS_PORT_IO
    // Use the VESA core driver to set a BGA mode and read back
    // the LFB address.  If BGA is unavailable we still try to use
    // the PCI VGA BAR0 address with a firmware-set mode.
    //
    // The caller is expected to have called vesa::init() first.
    // We attempt BGA mode setting; if that fails we assume the
    // current multiboot/firmware mode is already active and just
    // record the geometry.

    // Try port-I/O BGA mode setting
    // (inline BGA register programming so we don't pull in vesa.h
    //  — keeps the dependency tree flat)
    const uint16_t BGA_INDEX = 0x01CE;
    const uint16_t BGA_DATA  = 0x01CF;

    // Read BGA ID
    arch::outw(BGA_INDEX, 0x0000);
    uint16_t id = arch::inw(BGA_DATA);
    bool hasBga = (id >= 0xB0C0 && id <= 0xB0CF);

    if (hasBga) {
        // Disable
        arch::outw(BGA_INDEX, 0x0004);
        arch::outw(BGA_DATA,  0x0000);
        // XRES
        arch::outw(BGA_INDEX, 0x0001);
        arch::outw(BGA_DATA,  width);
        // YRES
        arch::outw(BGA_INDEX, 0x0002);
        arch::outw(BGA_DATA,  height);
        // BPP
        arch::outw(BGA_INDEX, 0x0003);
        arch::outw(BGA_DATA,  bpp);
        // Enable + LFB
        arch::outw(BGA_INDEX, 0x0004);
        arch::outw(BGA_DATA,  0x0041);
    }

    // Scan PCI for VGA BAR0
    const uint16_t PCI_ADDR = 0x0CF8;
    const uint16_t PCI_DAT  = 0x0CFC;
    uint64_t lfb = 0xE0000000ULL; // default

    for (uint16_t bus = 0; bus < 256 && lfb == 0xE0000000ULL; ++bus) {
        for (uint8_t d = 0; d < 32; ++d) {
            uint32_t addr = 0x80000000u | (bus << 16) | (d << 11) | 0;
            arch::outl(PCI_ADDR, addr);
            uint32_t pid = arch::inl(PCI_DAT);
            if (pid == 0xFFFFFFFF) continue;

            arch::outl(PCI_ADDR, addr | 0x08);
            uint32_t cls = arch::inl(PCI_DAT);
            if ((cls >> 24) == 0x03 && ((cls >> 16) & 0xFF) == 0x00) {
                arch::outl(PCI_ADDR, addr | 0x10);
                uint32_t bar0 = arch::inl(PCI_DAT);
                if (!(bar0 & 1)) {
                    lfb = bar0 & 0xFFFFFFF0u;
                }
                break;
            }
        }
    }

    g_buffer = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(lfb));
    g_width  = width;
    g_height = height;
    g_bpp    = bpp;
    g_pitch  = static_cast<uint32_t>(width) * (bpp / 8);
    g_drawTarget = g_buffer;  // Draw directly to video memory by default
    g_available = true;
    return true;
#else
    (void)width; (void)height; (void)bpp;
    return false;
#endif
}

// ================================================================
// EFI GOP init (IA-64 and any EFI-booted platform)
// ================================================================

bool init_efi_gop(uint64_t lfbBase, uint32_t width, uint32_t height,
                  uint32_t pitch, uint8_t bpp)
{
    if (lfbBase == 0 || width == 0 || height == 0) return false;

    g_buffer = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(lfbBase));
    g_width  = width;
    g_height = height;
    g_pitch  = pitch;
    g_bpp    = bpp;
    g_drawTarget = g_buffer;  // Draw directly to video memory by default
    g_available = true;
    return true;
}

// ================================================================
// Manual init (used by arch-specific backends)
// ================================================================

bool init_manual(uint64_t lfbBase, uint32_t width, uint32_t height,
                 uint32_t pitch, uint8_t bpp)
{
    if (lfbBase == 0 || width == 0 || height == 0) return false;

    g_buffer = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(lfbBase));
    g_width  = width;
    g_height = height;
    g_pitch  = pitch;
    g_bpp    = bpp;
    g_drawTarget = g_buffer;  // Draw directly to video memory by default
    g_available = true;
    return true;
}

uint32_t get_width()
{
    return g_width;
}

uint32_t get_height()
{
    return g_height;
}

uint32_t get_pitch()
{
    return g_pitch;
}

uint8_t get_bpp()
{
    return g_bpp;
}

uint32_t* get_buffer()
{
    return g_buffer;
}

bool is_available()
{
    return g_available;
}

void clear(uint32_t color)
{
    if (!g_available || !g_drawTarget) return;
    
    uint32_t pixels = (g_pitch / 4) * g_height;
    for (uint32_t i = 0; i < pixels; i++) {
        g_drawTarget[i] = color;
    }
}

void put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!g_available || !g_drawTarget || x >= g_width || y >= g_height) return;
    
    uint32_t offset = y * (g_pitch / 4) + x;
    g_drawTarget[offset] = color;
}

uint32_t get_pixel(uint32_t x, uint32_t y)
{
    if (!g_available || !g_drawTarget || x >= g_width || y >= g_height) return 0;
    
    uint32_t offset = y * (g_pitch / 4) + x;
    return g_drawTarget[offset];
}

void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    if (!g_available || !g_drawTarget) return;
    
    // Optimized version: direct memory access with row-based filling
    uint32_t pitchPixels = g_pitch / 4;
    for (uint32_t dy = 0; dy < height; dy++) {
        uint32_t rowY = y + dy;
        if (rowY >= g_height) break;
        uint32_t* row = g_drawTarget + rowY * pitchPixels + x;
        for (uint32_t dx = 0; dx < width; dx++) {
            if (x + dx < g_width) {
                row[dx] = color;
            }
        }
    }
}

void draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color)
{
    if (!g_available) return;
    
    // Bresenham's line algorithm
    int dx = x2 > x1 ? x2 - x1 : x1 - x2;
    int dy = y2 > y1 ? y2 - y1 : y1 - y2;
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    
    while (true) {
        put_pixel(x1, y1, color);
        
        if (x1 == x2 && y1 == y2) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void blit(uint32_t* buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!g_available || !buffer) return;
    
    for (uint32_t dy = 0; dy < height; dy++) {
        for (uint32_t dx = 0; dx < width; dx++) {
            uint32_t color = buffer[dy * width + dx];
            put_pixel(x + dx, y + dy, color);
        }
    }
}

// ================================================================
// Double Buffering Support
// ================================================================

bool enable_double_buffering()
{
    if (!g_available) return false;
    
    // Check if resolution fits in our static back buffer
    uint32_t totalPixels = (g_pitch / 4) * g_height;
    if (totalPixels > MAX_BACKBUFFER_PIXELS) {
        return false;  // Resolution too high for our static buffer
    }
    
    // Use the static back buffer storage
    g_backBuffer = g_backBufferStorage;
    
    // Clear the back buffer
    for (uint32_t i = 0; i < totalPixels; i++) {
        g_backBuffer[i] = 0;
    }
    
    // Redirect all drawing operations to the back buffer
    g_drawTarget = g_backBuffer;
    g_doubleBuffered = true;
    
    return true;
}

bool is_double_buffered()
{
    return g_doubleBuffered;
}

void present()
{
    if (!g_available || !g_doubleBuffered || !g_backBuffer || !g_buffer) return;
    
    // Copy the entire back buffer to the front buffer (video memory)
    // This is done in one operation to minimize tearing
    uint32_t totalPixels = (g_pitch / 4) * g_height;
    
    // Use a simple memcpy-style copy for maximum speed
    uint32_t* src = g_backBuffer;
    uint32_t* dst = g_buffer;
    
    // Copy in larger chunks for better performance
    for (uint32_t i = 0; i < totalPixels; i++) {
        dst[i] = src[i];
    }
}

uint32_t* get_back_buffer()
{
    return g_backBuffer;
}

} // namespace framebuffer
} // namespace kernel
