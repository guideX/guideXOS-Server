// VESA / VBE / BGA Graphics Driver
//
// Provides hardware-level graphics mode setting for x86/amd64:
//
//   1. Bochs Graphics Adapter (BGA / VBE Extensions)
//      Used by QEMU (-vga std), Bochs, and VirtualBox.
//      Programmed via I/O ports 0x01CE/0x01CF — no BIOS call needed.
//
//   2. VBE 2.0+ (VESA BIOS Extensions)
//      The standard way to set graphics modes on real BIOS hardware.
//      Requires a real-mode INT 0x10 thunk — only from a Multiboot
//      bootloader or a v86 monitor.  The multiboot header requests
//      a framebuffer, so the bootloader has already set the mode;
//      this module can enumerate modes for later switching.
//
// On non-x86 architectures this header still provides the common
// VBE/BGA structures (useful for PCI VGA cards on SPARC64/IA-64),
// but the port-I/O paths compile to stubs.
//
// Reference: VESA BIOS Extension Core Functions Standard 3.0,
//            Bochs VBE interface (bochs.sourceforge.io)
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_VESA_H
#define KERNEL_VESA_H

#include "kernel/types.h"

namespace kernel {
namespace vesa {

// ================================================================
// Bochs Graphics Adapter (BGA) I/O ports and registers
// ================================================================

static const uint16_t BGA_IOPORT_INDEX = 0x01CE;
static const uint16_t BGA_IOPORT_DATA  = 0x01CF;

// BGA index register values
static const uint16_t BGA_INDEX_ID           = 0x00;
static const uint16_t BGA_INDEX_XRES         = 0x01;
static const uint16_t BGA_INDEX_YRES         = 0x02;
static const uint16_t BGA_INDEX_BPP          = 0x03;
static const uint16_t BGA_INDEX_ENABLE       = 0x04;
static const uint16_t BGA_INDEX_BANK         = 0x05;
static const uint16_t BGA_INDEX_VIRT_WIDTH   = 0x06;
static const uint16_t BGA_INDEX_VIRT_HEIGHT  = 0x07;
static const uint16_t BGA_INDEX_X_OFFSET     = 0x08;
static const uint16_t BGA_INDEX_Y_OFFSET     = 0x09;

// BGA_INDEX_ENABLE bits
static const uint16_t BGA_DISABLED       = 0x00;
static const uint16_t BGA_ENABLED        = 0x01;
static const uint16_t BGA_LFB_ENABLED    = 0x40;
static const uint16_t BGA_NOCLEARMEM     = 0x80;

// Valid BGA ID range (0xB0C0 .. 0xB0C5+)
static const uint16_t BGA_ID_MIN = 0xB0C0;
static const uint16_t BGA_ID_MAX = 0xB0CF;

// Default PCI VGA linear framebuffer address (BAR0 on most emulators)
static const uint32_t BGA_LFB_ADDR_DEFAULT = 0xE0000000u;

// ================================================================
// VBE Info Block (returned by VBE function 0x4F00)
// ================================================================

#if defined(__GNUC__) || defined(__clang__)
#define VESA_PACKED __attribute__((packed))
#else
#pragma pack(push, 1)
#define VESA_PACKED
#endif

struct VbeInfoBlock {
    char     signature[4];       // "VESA" or "VBE2"
    uint16_t version;            // e.g. 0x0300
    uint32_t oemStringPtr;       // far pointer to OEM string
    uint32_t capabilities;
    uint32_t modeListPtr;        // far pointer to mode list
    uint16_t totalMemory;        // in 64 KB blocks
    uint16_t oemSoftwareRev;
    uint32_t oemVendorNamePtr;
    uint32_t oemProductNamePtr;
    uint32_t oemProductRevPtr;
    uint8_t  reserved[222];
    uint8_t  oemData[256];
} VESA_PACKED;

// ================================================================
// VBE Mode Info Block (returned by VBE function 0x4F01)
// ================================================================

struct VbeModeInfo {
    uint16_t attributes;
    uint8_t  windowA;
    uint8_t  windowB;
    uint16_t granularity;
    uint16_t windowSize;
    uint16_t segmentA;
    uint16_t segmentB;
    uint32_t winFuncPtr;
    uint16_t pitch;              // bytes per scan line
    uint16_t width;
    uint16_t height;
    uint8_t  charWidth;
    uint8_t  charHeight;
    uint8_t  numPlanes;
    uint8_t  bpp;
    uint8_t  numBanks;
    uint8_t  memoryModel;
    uint8_t  bankSize;
    uint8_t  numImagePages;
    uint8_t  reserved0;
    // Direct color fields
    uint8_t  redMaskSize;
    uint8_t  redFieldPosition;
    uint8_t  greenMaskSize;
    uint8_t  greenFieldPosition;
    uint8_t  blueMaskSize;
    uint8_t  blueFieldPosition;
    uint8_t  rsvdMaskSize;
    uint8_t  rsvdFieldPosition;
    uint8_t  directColorAttributes;
    // VBE 2.0+
    uint32_t physBasePtr;        // physical address of LFB
    uint32_t reserved1;
    uint16_t reserved2;
    // VBE 3.0+
    uint16_t linBytesPerScanLine;
    uint8_t  bnkNumImagePages;
    uint8_t  linNumImagePages;
    uint8_t  linRedMaskSize;
    uint8_t  linRedFieldPosition;
    uint8_t  linGreenMaskSize;
    uint8_t  linGreenFieldPosition;
    uint8_t  linBlueMaskSize;
    uint8_t  linBlueFieldPosition;
    uint8_t  linRsvdMaskSize;
    uint8_t  linRsvdFieldPosition;
    uint32_t maxPixelClock;
    uint8_t  reserved3[189];
} VESA_PACKED;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#undef VESA_PACKED

// VBE mode attribute flags
static const uint16_t VBE_ATTR_SUPPORTED     = 0x0001;
static const uint16_t VBE_ATTR_COLOR         = 0x0008;
static const uint16_t VBE_ATTR_GRAPHICS      = 0x0010;
static const uint16_t VBE_ATTR_LFB           = 0x0080;

// VBE memory model types
static const uint8_t VBE_MEMMODEL_TEXT        = 0x00;
static const uint8_t VBE_MEMMODEL_PACKED      = 0x04;
static const uint8_t VBE_MEMMODEL_DIRECT      = 0x06;

// ================================================================
// Video mode descriptor (internal representation)
// ================================================================

struct VideoMode {
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;                // 8, 15, 16, 24, or 32
    uint32_t pitch;              // bytes per scan line
    uint64_t lfbAddress;         // physical address of linear framebuffer
    bool     valid;
};

// ================================================================
// PCI VGA device descriptor
// ================================================================

struct PciVgaDevice {
    bool     found;
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
    uint16_t vendorId;
    uint16_t deviceId;
    uint64_t bar0;               // linear framebuffer BAR
    uint32_t bar0Size;
    uint64_t bar2;               // MMIO registers (if applicable)
};

// ================================================================
// Predefined common modes
// ================================================================

static const uint16_t MODE_640x480x32   = 0;
static const uint16_t MODE_800x600x32   = 1;
static const uint16_t MODE_1024x768x32  = 2;
static const uint16_t MODE_1280x1024x32 = 3;
static const uint16_t MODE_1920x1080x32 = 4;

static const uint8_t  MAX_MODES = 16;

// ================================================================
// Public API
// ================================================================

// Initialise the VESA/BGA driver.
// On x86/amd64: probes for BGA, reads PCI VGA BAR0.
// On SPARC64/IA64: probes PCI for VGA-class devices.
// On SPARC v8: no-op (TCX is handled separately).
void init();

// Check whether a BGA-compatible adapter was detected.
bool is_bga_available();

// Probe PCI for a VGA-class device and return its info.
bool probe_pci_vga(PciVgaDevice* out);

// Set a video mode via BGA registers (x86/amd64 with BGA only).
// Returns true on success.
bool set_mode_bga(uint16_t width, uint16_t height, uint8_t bpp);

// Get the current video mode.
VideoMode get_current_mode();

// Get the linear framebuffer address for the current mode.
uint64_t get_lfb_address();

// Return a list of available modes (populated during init).
uint8_t get_mode_count();
const VideoMode* get_mode(uint8_t index);

// Apply a video mode by index (from get_mode list) and configure
// the kernel framebuffer subsystem accordingly.
bool apply_mode(uint8_t modeIndex);

} // namespace vesa
} // namespace kernel

#endif // KERNEL_VESA_H
