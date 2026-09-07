//
// Framebuffer driver implementation
//
// Supports both Multiboot (legacy BIOS) and BootInfo (UEFI) initialization
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/framebuffer.h"
#include "include/kernel/arch.h"
#include "include/kernel/framebuffer_contract.h"

#if defined(__has_include)
#if __has_include(<string.h>)
#include <string.h>
#endif
#endif

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
static uint64_t g_framebufferSize = 0;
static uint32_t g_pixelFormat = kPixelFormatB8G8R8A8;
static bool g_available = false;
static bool g_doubleBuffered = false;
#if ARCH_HAS_PIC_8259
static kernel::framebuffer::DiagnosticFramebufferInventorySummary g_diagnosticInventorySummary{};
static kernel::framebuffer::DiagnosticFramebufferCandidate g_diagnosticInventoryCandidates[guideXOS::GUIDEXOS_MAX_FRAMEBUFFERS]{};
static uint32_t g_diagnosticInventoryCandidateCount = 0;
static bool g_hasDiagnosticInventory = false;
#endif

extern "C" void* memcpy(void* destination, const void* source, size_t count);

// Static back buffer storage (allocated in BSS segment)
// Max resolution support: 1920x1080 = 2,073,600 pixels * 4 bytes = ~8MB
// For kernel mode, we use a static buffer to avoid dynamic allocation
static const uint32_t MAX_BACKBUFFER_PIXELS = 1920 * 1080;
static uint32_t g_backBufferStorage[MAX_BACKBUFFER_PIXELS];

static uint32_t bytes_per_pixel()
{
    return g_bpp / 8;
}

static void reset_diagnostic_framebuffer_inventory()
{
#if ARCH_HAS_PIC_8259
    g_diagnosticInventorySummary = {};
    g_diagnosticInventoryCandidateCount = 0;
    g_hasDiagnosticInventory = false;
    for (uint32_t i = 0; i < guideXOS::GUIDEXOS_MAX_FRAMEBUFFERS; ++i) {
        g_diagnosticInventoryCandidates[i] = {};
    }
#endif
}

#if ARCH_HAS_PIC_8259
static void cache_diagnostic_framebuffer_inventory(const guideXOS::BootInfo* bootinfo)
{
    reset_diagnostic_framebuffer_inventory();

    if (!bootinfo) {
        return;
    }

    g_hasDiagnosticInventory = true;
    g_diagnosticInventorySummary.RawCount = bootinfo->FramebufferCount;
    g_diagnosticInventorySummary.DuplicateCount = bootinfo->FramebufferDuplicateCount;
    g_diagnosticInventorySummary.SuspiciousCount = bootinfo->FramebufferSuspiciousCount;

    const uint32_t rawCount = bootinfo->FramebufferCount > guideXOS::GUIDEXOS_MAX_FRAMEBUFFERS
        ? guideXOS::GUIDEXOS_MAX_FRAMEBUFFERS
        : bootinfo->FramebufferCount;

    for (uint32_t i = 0; i < rawCount; ++i) {
        const guideXOS::FramebufferDescriptor& descriptor = bootinfo->FramebufferDescriptors[i];
        if (!(descriptor.Flags & guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_VALID)) {
            continue;
        }

        if (descriptor.Flags & (guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_DUPLICATE | guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_ALIAS)) {
            continue;
        }

        if (g_diagnosticInventoryCandidateCount >= guideXOS::GUIDEXOS_MAX_FRAMEBUFFERS) {
            break;
        }

        kernel::framebuffer::DiagnosticFramebufferCandidate& candidate =
            g_diagnosticInventoryCandidates[g_diagnosticInventoryCandidateCount++];
        candidate.Base = descriptor.Base;
        candidate.Size = descriptor.Size;
        candidate.Width = descriptor.Width;
        candidate.Height = descriptor.Height;
        candidate.Pitch = descriptor.Pitch;
        candidate.BitsPerPixel = descriptor.BitsPerPixel;
        candidate.Format = static_cast<uint32_t>(descriptor.Format);
        candidate.Source = static_cast<uint32_t>(descriptor.Source);
        candidate.DescriptorFlags = descriptor.Flags;
        candidate.Flags = 0u;
    }

    if (g_diagnosticInventoryCandidateCount > 0u) {
        uint32_t activeIndex = 0u;
        for (uint32_t i = 0; i < g_diagnosticInventoryCandidateCount; ++i) {
            const uint32_t descriptorFlags = g_diagnosticInventoryCandidates[i].DescriptorFlags;
            if (descriptorFlags & (guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_PRIMARY | guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_SELECTED)) {
                activeIndex = i;
                break;
            }
        }

        for (uint32_t i = 0; i < g_diagnosticInventoryCandidateCount; ++i) {
            kernel::framebuffer::DiagnosticFramebufferCandidate& candidate =
                g_diagnosticInventoryCandidates[i];
            if (i == activeIndex) {
                candidate.Flags |= kernel::framebuffer::DIAGNOSTIC_FRAMEBUFFER_CANDIDATE_FLAG_ACTIVE;
            } else {
                candidate.Flags |= kernel::framebuffer::DIAGNOSTIC_FRAMEBUFFER_CANDIDATE_FLAG_DISABLED;
            }
        }
    }

    g_diagnosticInventorySummary.UniqueCount = g_diagnosticInventoryCandidateCount;
    g_diagnosticInventorySummary.ActiveRenderTargetCount = g_diagnosticInventoryCandidateCount > 0u ? 1u : 0u;
    g_diagnosticInventorySummary.DisabledCandidateCount =
        g_diagnosticInventoryCandidateCount > 0u ? (g_diagnosticInventoryCandidateCount - 1u) : 0u;
}
#else
static void cache_diagnostic_framebuffer_inventory(const void*) {}
#endif

#if ARCH_HAS_PIC_8259
static bool diagnostic_framebuffer_candidate_valid(uint32_t index)
{
    return g_hasDiagnosticInventory && index < g_diagnosticInventoryCandidateCount;
}
#else
static bool diagnostic_framebuffer_candidate_valid(uint32_t) { return false; }
#endif

static bool initialize_common_geometry(const Geometry& geometry)
{
    if (!validate_geometry(geometry)) return false;
    g_buffer = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(geometry.base));
    g_width = geometry.width;
    g_height = geometry.height;
    g_pitch = geometry.pitch;
    g_bpp = static_cast<uint8_t>(geometry.bits_per_pixel);
    g_framebufferSize = geometry.size;
    g_pixelFormat = geometry.format;
    g_drawTarget = g_buffer;
    g_backBuffer = nullptr;
    g_available = true;
    g_doubleBuffered = false;
    return true;
}

static bool supports_direct_color_bpp()
{
    return g_bpp == 16 || g_bpp == 24 || g_bpp == 32;
}

static uint16_t pack_rgb565(uint32_t color)
{
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static uint32_t unpack_rgb565(uint16_t color)
{
    uint8_t r = static_cast<uint8_t>(((color >> 11) & 0x1F) << 3);
    uint8_t g = static_cast<uint8_t>(((color >> 5) & 0x3F) << 2);
    uint8_t b = static_cast<uint8_t>((color & 0x1F) << 3);
    return 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) | b;
}

static void write_front_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    uint32_t bppBytes = bytes_per_pixel();
    if (!g_buffer || bppBytes == 0) return;

    uint8_t* base = reinterpret_cast<uint8_t*>(g_buffer);
    uint8_t* pixel = base + static_cast<uint64_t>(y) * g_pitch + static_cast<uint64_t>(x) * bppBytes;

    switch (g_bpp) {
        case 32:
            if (g_pixelFormat == kPixelFormatR8G8B8A8) {
                pixel[0] = static_cast<uint8_t>((color >> 16) & 0xFF);
                pixel[1] = static_cast<uint8_t>((color >> 8) & 0xFF);
                pixel[2] = static_cast<uint8_t>(color & 0xFF);
            } else {
                pixel[0] = static_cast<uint8_t>(color & 0xFF);
                pixel[1] = static_cast<uint8_t>((color >> 8) & 0xFF);
                pixel[2] = static_cast<uint8_t>((color >> 16) & 0xFF);
            }
            pixel[3] = static_cast<uint8_t>((color >> 24) & 0xFF);
            break;
        case 24:
            pixel[0] = static_cast<uint8_t>(color & 0xFF);
            pixel[1] = static_cast<uint8_t>((color >> 8) & 0xFF);
            pixel[2] = static_cast<uint8_t>((color >> 16) & 0xFF);
            break;
        case 16: {
            uint16_t packed = pack_rgb565(color);
            pixel[0] = static_cast<uint8_t>(packed & 0xFF);
            pixel[1] = static_cast<uint8_t>((packed >> 8) & 0xFF);
            break;
        }
        default:
            // TODO: Add palette-aware 8bpp support for CG3/other indexed framebuffers.
            break;
    }
}

static uint32_t read_front_pixel(uint32_t x, uint32_t y)
{
    uint32_t bppBytes = bytes_per_pixel();
    if (!g_buffer || bppBytes == 0) return 0;

    const uint8_t* base = reinterpret_cast<const uint8_t*>(g_buffer);
    const uint8_t* pixel = base + static_cast<uint64_t>(y) * g_pitch + static_cast<uint64_t>(x) * bppBytes;

    switch (g_bpp) {
        case 32:
            if (g_pixelFormat == kPixelFormatR8G8B8A8) {
                return 0xFF000000u | (static_cast<uint32_t>(pixel[0]) << 16) |
                       (static_cast<uint32_t>(pixel[1]) << 8) | pixel[2];
            }
            return 0xFF000000u | (static_cast<uint32_t>(pixel[2]) << 16) |
                   (static_cast<uint32_t>(pixel[1]) << 8) | pixel[0];
        case 24:
            return 0xFF000000u | (static_cast<uint32_t>(pixel[2]) << 16) |
                   (static_cast<uint32_t>(pixel[1]) << 8) | pixel[0];
        case 16: {
            uint16_t packed = static_cast<uint16_t>(pixel[0]) |
                              (static_cast<uint16_t>(pixel[1]) << 8);
            return unpack_rgb565(packed);
        }
        default:
            return 0;
    }
}

#if ARCH_HAS_PIC_8259

bool init(void* multiboot_info_ptr)
{
    reset_diagnostic_framebuffer_inventory();

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
    g_framebufferSize = static_cast<uint64_t>(g_pitch) * g_height;
    g_pixelFormat = kPixelFormatB8G8R8A8;
    
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
    cache_diagnostic_framebuffer_inventory(bootinfo);

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
    
    kernel::boot::CommonFramebufferInfo common{};
    common.base = bootinfo->FramebufferBase;
    common.size = bootinfo->FramebufferSize;
    common.width = bootinfo->FramebufferWidth;
    common.height = bootinfo->FramebufferHeight;
    common.pitch = bootinfo->FramebufferPitch;
    common.bits_per_pixel = 32;
    common.format = static_cast<uint32_t>(bootinfo->FramebufferFormat);
    if (common.format == 0) common.format = kPixelFormatB8G8R8A8;
    return initialize_common_geometry({common.base, common.size, common.width, common.height,
                                       common.pitch, common.bits_per_pixel, common.format});
}

#else // !ARCH_HAS_PIC_8259

bool init(void*)
{
    reset_diagnostic_framebuffer_inventory();
    return false;
}

#if defined(ARCH_SPARC)

// Sun4m TCX framebuffer initialisation.
// On QEMU SS-5 the 24-bit direct-colour plane lives at 0x50800000.
// Resolution defaults to 1024x768, 32-bit XRGB pixels.
bool init_sun4m()
{
    reset_diagnostic_framebuffer_inventory();
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
    reset_diagnostic_framebuffer_inventory();
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

bool init_from_common_bootinfo(const kernel::boot::CommonBootInfo* bootinfo)
{
    reset_diagnostic_framebuffer_inventory();
    if (!bootinfo || (bootinfo->flags & kernel::boot::kBootFlagFramebuffer) == 0) return false;
    const kernel::boot::CommonFramebufferInfo& fb = bootinfo->framebuffer;
    return initialize_common_geometry({fb.base, fb.size, fb.width, fb.height, fb.pitch,
                                       fb.bits_per_pixel, fb.format});
}

// ================================================================
// VESA / BGA init (x86 / amd64)
// ================================================================

bool init_vesa(uint16_t width, uint16_t height, uint8_t bpp)
{
    reset_diagnostic_framebuffer_inventory();
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
    uint64_t size = static_cast<uint64_t>(pitch) * height;
    return init_efi_gop_ex(lfbBase, size, width, height, pitch, bpp,
                           kPixelFormatB8G8R8A8);
}

bool init_efi_gop_ex(uint64_t lfbBase, uint64_t framebufferSize,
                     uint32_t width, uint32_t height, uint32_t pitch,
                     uint8_t bpp, uint32_t pixelFormat)
{
    reset_diagnostic_framebuffer_inventory();
    return initialize_common_geometry({lfbBase, framebufferSize, width, height,
                                       pitch, bpp, pixelFormat});
}

// ================================================================
// Manual init (used by arch-specific backends)
// ================================================================

bool init_manual(uint64_t lfbBase, uint32_t width, uint32_t height,
                 uint32_t pitch, uint8_t bpp)
{
    uint64_t size = static_cast<uint64_t>(pitch) * height;
    if (bpp == 32) {
        return init_efi_gop_ex(lfbBase, size, width, height, pitch, bpp,
                               kPixelFormatB8G8R8A8);
    }
    reset_diagnostic_framebuffer_inventory();
    if (lfbBase == 0 || width == 0 || height == 0 || pitch == 0 || size == 0) return false;
    g_buffer = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(lfbBase));
    g_width = width;
    g_height = height;
    g_pitch = pitch;
    g_bpp = bpp;
    g_framebufferSize = size;
    g_pixelFormat = kPixelFormatB8G8R8A8;
    g_drawTarget = g_buffer;
    g_backBuffer = nullptr;
    g_available = true;
    g_doubleBuffered = false;
    return true;
}

bool init_virtual(uint32_t width, uint32_t height)
{
    reset_diagnostic_framebuffer_inventory();
    if (width == 0u || height == 0u) return false;

    const uint64_t totalPixels = static_cast<uint64_t>(width) * height;
    if (totalPixels > MAX_BACKBUFFER_PIXELS) return false;

    // The software canvas is deliberately backed by the existing bounded
    // framebuffer storage.  Keeping front and draw pointers on the same
    // storage lets the normal desktop double-buffering and presentation code
    // remain the owner of all drawing operations.
    g_buffer = g_backBufferStorage;
    g_backBuffer = nullptr;
    g_drawTarget = g_buffer;
    g_width = width;
    g_height = height;
    g_pitch = width * sizeof(uint32_t);
    g_bpp = 32u;
    g_available = true;
    g_doubleBuffered = false;
    for (uint64_t i = 0u; i < totalPixels; ++i) {
        g_backBufferStorage[i] = 0u;
    }
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

uint32_t* get_draw_buffer()
{
    return g_drawTarget;
}

bool is_available()
{
    return g_available;
}

#if ARCH_HAS_PIC_8259
bool has_diagnostic_framebuffer_inventory()
{
    return g_hasDiagnosticInventory;
}

const DiagnosticFramebufferInventorySummary& diagnostic_framebuffer_inventory_summary()
{
    return g_diagnosticInventorySummary;
}

uint32_t diagnostic_framebuffer_candidate_count()
{
    return g_diagnosticInventoryCandidateCount;
}

bool diagnostic_framebuffer_candidate(uint32_t index, DiagnosticFramebufferCandidate& outCandidate)
{
    if (!diagnostic_framebuffer_candidate_valid(index)) {
        return false;
    }

    outCandidate = g_diagnosticInventoryCandidates[index];
    return true;
}
#else
bool has_diagnostic_framebuffer_inventory() { return false; }

const DiagnosticFramebufferInventorySummary& diagnostic_framebuffer_inventory_summary()
{
    static const DiagnosticFramebufferInventorySummary empty{};
    return empty;
}

uint32_t diagnostic_framebuffer_candidate_count() { return 0; }

bool diagnostic_framebuffer_candidate(uint32_t, DiagnosticFramebufferCandidate&)
{
    return false;
}
#endif

void clear(uint32_t color)
{
    if (!g_available) return;

    if (g_doubleBuffered && g_backBuffer) {
        for (uint32_t y = 0; y < g_height; ++y) {
            for (uint32_t x = 0; x < g_width; ++x) {
                g_backBuffer[static_cast<uint64_t>(y) * g_width + x] = color;
            }
        }
        return;
    }

    for (uint32_t y = 0; y < g_height; y++) {
        for (uint32_t x = 0; x < g_width; x++) {
            write_front_pixel(x, y, color);
        }
    }
}

void put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!g_available || x >= g_width || y >= g_height) return;

    if (g_doubleBuffered && g_backBuffer) {
        g_backBuffer[y * g_width + x] = color;
        return;
    }

    write_front_pixel(x, y, color);
}

void put_front_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!g_available || x >= g_width || y >= g_height) return;
    write_front_pixel(x, y, color);
}

uint32_t get_pixel(uint32_t x, uint32_t y)
{
    if (!g_available || x >= g_width || y >= g_height) return 0;

    if (g_doubleBuffered && g_backBuffer) {
        return g_backBuffer[y * g_width + x];
    }

    return read_front_pixel(x, y);
}

uint32_t get_front_pixel(uint32_t x, uint32_t y)
{
    if (!g_available || x >= g_width || y >= g_height) return 0;
    return read_front_pixel(x, y);
}

void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
{
    if (!g_available) return;
    ClippedRect clipped{};
    if (!clip_rect(x, y, width, height, g_width, g_height, &clipped)) return;
    x = clipped.x;
    y = clipped.y;
    width = clipped.width;
    height = clipped.height;

    if (g_bpp == 32) {
        uint32_t* base = (g_doubleBuffered && g_backBuffer) ? g_backBuffer : g_buffer;
        if (!base) return;
        const uint32_t stride = (g_doubleBuffered && g_backBuffer) ? g_width : (g_pitch / 4);
        if (g_doubleBuffered || (g_pixelFormat == kPixelFormatB8G8R8A8 &&
                                 (g_pitch & 3u) == 0u)) {
            for (uint32_t dy = 0; dy < height; dy++) {
                uint32_t* row = base + static_cast<uint64_t>(y + dy) * stride + x;
                for (uint32_t dx = 0; dx < width; dx++) {
                    row[dx] = color;
                }
            }
        } else {
            for (uint32_t dy = 0; dy < height; ++dy) {
                for (uint32_t dx = 0; dx < width; ++dx) {
                    write_front_pixel(x + dx, y + dy, color);
                }
            }
        }
        return;
    }

    for (uint32_t dy = 0; dy < height; dy++) {
        for (uint32_t dx = 0; dx < width; dx++) {
            put_pixel(x + dx, y + dy, color);
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
    if (!g_available || !buffer || x >= g_width || y >= g_height || width == 0 || height == 0) return;

    const uint32_t sourceWidth = width;
    if (width > g_width - x) width = g_width - x;
    if (height > g_height - y) height = g_height - y;

    if (g_bpp == 32 && (g_doubleBuffered ||
                        (g_pixelFormat == kPixelFormatB8G8R8A8 && (g_pitch & 3u) == 0u))) {
        uint32_t* base = (g_doubleBuffered && g_backBuffer) ? g_backBuffer : g_buffer;
        if (!base) return;
        const uint32_t stride = (g_doubleBuffered && g_backBuffer) ? g_width : (g_pitch / 4);
        for (uint32_t dy = 0; dy < height; dy++) {
            uint32_t* dstRow = base + static_cast<uint64_t>(y + dy) * stride + x;
            const uint32_t* srcRow = buffer + static_cast<uint64_t>(dy) * sourceWidth;
            memcpy(dstRow, srcRow, static_cast<size_t>(width) * sizeof(uint32_t));
        }
        return;
    }

    for (uint32_t dy = 0; dy < height; dy++) {
        for (uint32_t dx = 0; dx < width; dx++) {
            uint32_t color = buffer[static_cast<uint64_t>(dy) * sourceWidth + dx];
            put_pixel(x + dx, y + dy, color);
        }
    }
}

void blit_alpha(const uint32_t* buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!g_available || !buffer || x >= g_width || y >= g_height || width == 0 || height == 0) return;
    const uint32_t sourceWidth = width;
    if (width > g_width - x) width = g_width - x;
    if (height > g_height - y) height = g_height - y;

    for (uint32_t dy = 0; dy < height; dy++) {
        for (uint32_t dx = 0; dx < width; dx++) {
            uint32_t src = buffer[static_cast<uint64_t>(dy) * sourceWidth + dx];
            uint8_t a = (src >> 24) & 0xFF;
            if (a == 0) continue;
            if (a == 0xFF) {
                put_pixel(x + dx, y + dy, src);
            } else {
                uint32_t dst = get_pixel(x + dx, y + dy);
                uint8_t sr = (src >> 16) & 0xFF;
                uint8_t sg = (src >>  8) & 0xFF;
                uint8_t sb =  src        & 0xFF;
                uint8_t dr = (dst >> 16) & 0xFF;
                uint8_t dg = (dst >>  8) & 0xFF;
                uint8_t db =  dst        & 0xFF;
                uint8_t or_ = (uint8_t)((sr * a + dr * (255 - a)) / 255);
                uint8_t og  = (uint8_t)((sg * a + dg * (255 - a)) / 255);
                uint8_t ob  = (uint8_t)((sb * a + db * (255 - a)) / 255);
                put_pixel(x + dx, y + dy, 0xFF000000u | ((uint32_t)or_ << 16) | ((uint32_t)og << 8) | ob);
            }
        }
    }
}

// ================================================================
// Double Buffering Support
// ================================================================

bool enable_double_buffering()
{
    if (!g_available) return false;

    if (!supports_direct_color_bpp()) {
        // TODO: Add palette-aware double buffering for 8bpp indexed framebuffers.
        return false;
    }
    
    // Check if resolution fits in our static back buffer
    const uint64_t totalPixels = static_cast<uint64_t>(g_width) * g_height;
    if (totalPixels == 0 || totalPixels > MAX_BACKBUFFER_PIXELS) {
        return false;  // Resolution too high for our static buffer
    }
    
    // Use the static back buffer storage
    g_backBuffer = g_backBufferStorage;
    
    // Clear the back buffer
    for (uint64_t i = 0; i < totalPixels; i++) {
        g_backBuffer[i] = 0;
    }
    
    // Redirect all drawing operations to the ARGB back buffer. present()
    // converts to the active front-buffer pixel format.
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

    for (uint32_t y = 0; y < g_height; y++) {
        for (uint32_t x = 0; x < g_width; x++) {
            write_front_pixel(x, y, g_backBuffer[y * g_width + x]);
        }
    }
#if defined(ARCH_ARM64)
    __asm__ volatile("dsb sy" ::: "memory");
#endif
}

uint64_t verification_hash(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!g_available || x >= g_width || y >= g_height || width == 0 || height == 0) return 0;
    if (width > g_width - x) width = g_width - x;
    if (height > g_height - y) height = g_height - y;

    uint64_t hash = UINT64_C(1469598103934665603);
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t column = 0; column < width; ++column) {
            const uint32_t pixel = get_front_pixel(x + column, y + row);
            for (uint32_t byte = 0; byte < 4; ++byte) {
                hash ^= (pixel >> (byte * 8)) & 0xffu;
                hash *= UINT64_C(1099511628211);
            }
        }
    }
    return hash;
}

uint32_t* get_back_buffer()
{
    return g_backBuffer;
}

} // namespace framebuffer
} // namespace kernel
