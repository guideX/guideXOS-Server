#ifndef KERNEL_FRAMEBUFFER_H
#define KERNEL_FRAMEBUFFER_H

#include "types.h"
#include "arch.h"

// Forward declaration for BootInfo (x86/amd64 only)
#if ARCH_HAS_PIC_8259
namespace guideXOS {
    struct BootInfo;
}
#endif

namespace kernel {
namespace framebuffer {

// Initialize framebuffer from multiboot info (legacy BIOS)
bool init(void* multiboot_info);

#if ARCH_HAS_PIC_8259
// Initialize framebuffer from BootInfo (UEFI)
bool init_from_bootinfo(const guideXOS::BootInfo* bootinfo);
#endif

// Initialize framebuffer via VESA/BGA mode setting (x86/amd64).
// Uses the Bochs Graphics Adapter if available, otherwise falls
// back to the PCI VGA BAR0 linear framebuffer.
bool init_vesa(uint16_t width, uint16_t height, uint8_t bpp);

// Initialize framebuffer from an EFI GOP descriptor (IA-64).
// The EFI firmware provides the framebuffer address and pixel format.
bool init_efi_gop(uint64_t lfbBase, uint32_t width, uint32_t height,
                  uint32_t pitch, uint8_t bpp);

// Initialize Sun4m TCX framebuffer (SPARC v8 only, known MMIO address)
bool init_sun4m();

// Initialize Sun4u PCI VGA framebuffer (SPARC v9 only, known PCI BAR)
bool init_sun4u();

// Initialize from RISC-V ramfb (fw_cfg device, QEMU -device ramfb).
bool init_riscv_ramfb(uint64_t lfbBase, uint32_t width, uint32_t height,
                      uint32_t pitch, uint8_t bpp);

// Initialize from an arbitrary LFB address and geometry.
// Used by arch-specific graphics backends after probing hardware.
bool init_manual(uint64_t lfbBase, uint32_t width, uint32_t height,
                 uint32_t pitch, uint8_t bpp);

// Initialize a QEMU-only software canvas when VirtIO-GPU owns the visible
// scanouts and firmware did not provide a GOP framebuffer.  This is not a
// hardware framebuffer or an MMIO path; the live VirtIO-GPU presenter copies
// the canvas into its already-established 2D resources.
bool init_virtual(uint32_t width, uint32_t height);

// Get framebuffer dimensions
uint32_t get_width();
uint32_t get_height();
uint32_t get_pitch();
uint8_t get_bpp();

// Get framebuffer pointer
uint32_t* get_buffer();

// Get the buffer currently used by normal desktop drawing.
uint32_t* get_draw_buffer();

// Check if framebuffer is available
bool is_available();

// Diagnostic-only inventory of unique framebuffer-backed display candidates
// preserved from BootInfo. The active render target remains primary-only.
static const uint32_t DIAGNOSTIC_FRAMEBUFFER_CANDIDATE_FLAG_ACTIVE = (1u << 0);
static const uint32_t DIAGNOSTIC_FRAMEBUFFER_CANDIDATE_FLAG_DISABLED = (1u << 1);

struct DiagnosticFramebufferInventorySummary
{
    uint32_t RawCount{0};
    uint32_t UniqueCount{0};
    uint32_t DuplicateCount{0};
    uint32_t SuspiciousCount{0};
    uint32_t ActiveRenderTargetCount{0};
    uint32_t DisabledCandidateCount{0};
};

struct DiagnosticFramebufferCandidate
{
    uint64_t Base{0};
    uint64_t Size{0};
    uint32_t Width{0};
    uint32_t Height{0};
    uint32_t Pitch{0};
    uint32_t BitsPerPixel{0};
    uint32_t Format{0};
    uint32_t Source{0};
    uint32_t DescriptorFlags{0};
    uint32_t Flags{0};
};

bool has_diagnostic_framebuffer_inventory();
const DiagnosticFramebufferInventorySummary& diagnostic_framebuffer_inventory_summary();
uint32_t diagnostic_framebuffer_candidate_count();
bool diagnostic_framebuffer_candidate(uint32_t index, DiagnosticFramebufferCandidate& outCandidate);

// Clear screen to color
void clear(uint32_t color);

// Draw a pixel
void put_pixel(uint32_t x, uint32_t y, uint32_t color);

// Draw a pixel directly to the physical/front framebuffer.
// Use only for overlays that intentionally bypass double buffering.
void put_front_pixel(uint32_t x, uint32_t y, uint32_t color);

// Get a pixel
uint32_t get_pixel(uint32_t x, uint32_t y);

// Get a pixel directly from the physical/front framebuffer.
// Use only for overlays that intentionally bypass double buffering.
uint32_t get_front_pixel(uint32_t x, uint32_t y);

// Draw a filled rectangle
void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);

// Draw a line
void draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color);

// Copy a buffer to screen
void blit(uint32_t* buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

// Copy a buffer to screen with per-pixel alpha blending (source pixels in ARGB format)
void blit_alpha(const uint32_t* buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

// Double buffering support
// Enable double buffering (allocates back buffer)
bool enable_double_buffering();

// Check if double buffering is enabled
bool is_double_buffered();

// Present back buffer to screen (copy back buffer to front buffer)
// Call this after all drawing operations are complete for a frame
void present();

// Get the back buffer pointer (for direct access if needed)
uint32_t* get_back_buffer();

} // namespace framebuffer
} // namespace kernel

#endif // KERNEL_FRAMEBUFFER_H
