//
// guideXOS Desktop Environment - Kernel Framebuffer Renderer
//
// Renders the full desktop environment directly to the kernel framebuffer.
// Ported visual style from guideXOS Server compositor (compositor.cpp),
// desktop_wallpaper.h, system_tray.h, and bitmap_font.h.
//
// Now includes full GUI app support in bare-metal/UEFI mode via the
// kernel compositor and app framework.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/desktop.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/shell.h"
#include "include/kernel/kernel_app.h"
#include "include/kernel/kernel_apps.h"
#include "include/kernel/kernel_compositor.h"
#include "include/kernel/kernel_ipc.h"
#include "include/kernel/vfs.h"

#if defined(_MSC_VER)
#include <intrin.h>  // For MSVC intrinsics (__outbyte, __halt, etc.)
#endif

// ============================================================
// System Shutdown Implementation
// ============================================================

// QEMU debug exit port (isa-debug-exit device)
// Writing to this port causes QEMU to exit with code (value << 1) | 1
// Use -device isa-debug-exit,iobase=0x501,iosize=0x04 in QEMU command line
static const uint16_t QEMU_DEBUG_EXIT_PORT = 0x501;

// ACPI PM1a Control Register (standard location for many systems)
// Writing SLP_TYPa | SLP_EN triggers sleep/shutdown
static const uint16_t ACPI_PM1A_CNT = 0x604;  // Common ACPI PM1a port

// Bochs/QEMU shutdown via special port
static const uint16_t BOCHS_SHUTDOWN_PORT = 0xB004;

// Keyboard controller ports for reset
static const uint16_t KB_CTRL_STATUS_PORT = 0x64;
static const uint16_t KB_CTRL_CMD_RESET = 0xFE;

// Architecture-specific port I/O
#if defined(ARCH_X86) || defined(ARCH_AMD64)
static inline void outw_power(uint16_t port, uint16_t value)
{
#if defined(_MSC_VER)
    __outword(port, value);
#else
    asm volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
#endif
}

static inline void outb_power(uint16_t port, uint8_t value)
{
#if defined(_MSC_VER)
    __outbyte(port, value);
#else
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
#endif
}

static inline uint8_t inb_power(uint16_t port)
{
#if defined(_MSC_VER)
    return __inbyte(port);
#else
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
#endif
}

static inline void halt_cpu()
{
#if defined(_MSC_VER)
    __halt();
#else
    asm volatile ("cli; hlt");
#endif
}

static inline void enable_interrupts()
{
#if defined(_MSC_VER)
    _enable();
#else
    asm volatile ("sti");
#endif
}

static inline void disable_interrupts()
{
#if defined(_MSC_VER)
    _disable();
#else
    asm volatile ("cli");
#endif
}
#endif

// Perform system shutdown (extern for shell access)
void perform_shutdown()
{
#if defined(ARCH_X86) || defined(ARCH_AMD64)
    // Method 1: QEMU debug exit (cleanest for QEMU with isa-debug-exit device)
    // Exit code will be (0 << 1) | 1 = 1, indicating success
    outb_power(QEMU_DEBUG_EXIT_PORT, 0x00);
    
    // Method 2: QEMU/Bochs ACPI shutdown
    // S5 sleep state = shutdown (SLP_TYPa=5, SLP_EN=1)
    // Value: (5 << 10) | (1 << 13) = 0x2000 | 0x1400 = 0x3400
    outw_power(ACPI_PM1A_CNT, 0x2000);
    
    // Method 3: Bochs-specific shutdown port
    outw_power(BOCHS_SHUTDOWN_PORT, 0x2000);
    
    // If we're still here, just halt
    while (true) {
        halt_cpu();
    }
#elif defined(ARCH_RISCV64)
    // RISC-V uses SBI shutdown
    // SBI_EXT_SHUTDOWN = 0x08
    asm volatile (
        "li a7, 0x08\n"
        "ecall\n"
        ::: "a7"
    );
    while (true) {}
#else
    // Other architectures: infinite halt loop
    while (true) {
#if defined(_MSC_VER)
        __nop();
#else
        asm volatile ("nop");
#endif
    }
#endif
}

// Perform system restart/reboot (extern for shell access)
void perform_restart()
{
#if defined(ARCH_X86) || defined(ARCH_AMD64)
    disable_interrupts();
    
    // Method 1: Keyboard controller reset (most reliable)
    // Wait for keyboard controller to be ready
    uint8_t status;
    do {
        status = inb_power(KB_CTRL_STATUS_PORT);
    } while (status & 0x02);  // Wait while input buffer is full
    
    // Send reset command
    outb_power(KB_CTRL_STATUS_PORT, KB_CTRL_CMD_RESET);
    
    // Method 2: Triple fault (backup) - load null IDT and trigger interrupt
    // This usually causes a CPU reset
#if defined(_MSC_VER)
#pragma pack(push, 1)
    struct NullIDT { uint16_t limit; uint32_t base; };
#pragma pack(pop)
    NullIDT null_idt = {0, 0};
#else
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) null_idt = {0, 0};
#endif
    (void)null_idt;  // Suppress unused warning if not using this method
    
    // If keyboard reset didn't work, try QEMU reset via debug exit with special code
    outb_power(QEMU_DEBUG_EXIT_PORT, 0x01);  // Different exit code for restart
    
    // If we're still here, halt and wait for watchdog or manual reset
    while (true) {
        halt_cpu();
    }
#elif defined(ARCH_RISCV64)
    // RISC-V uses SBI system reset
    // SBI_EXT_SRST = 0x53525354 ("SRST")
    // Function 0 = system reset, type 0 = shutdown, type 1 = cold reboot
    asm volatile (
        "li a7, 0x53525354\n"  // SBI_EXT_SRST
        "li a6, 0\n"           // Function ID: system_reset
        "li a0, 1\n"           // Reset type: cold reboot
        "li a1, 0\n"           // Reset reason: no reason
        "ecall\n"
        ::: "a0", "a1", "a6", "a7"
    );
    while (true) {}
#else
    // Other architectures: just halt (restart not supported)
    while (true) {
#if defined(_MSC_VER)
        __nop();
#else
        asm volatile ("nop");
#endif
    }
#endif
}

// Perform system sleep/suspend (extern for shell access)
void perform_sleep()
{
#if defined(ARCH_X86) || defined(ARCH_AMD64)
    // ACPI S1 sleep state (CPU stops, system power maintained)
    // For proper S3 (suspend to RAM), we'd need to save system state first
    
    // Write S1 sleep type to PM1a control register
    // S1: SLP_TYPa = 1, SLP_EN = 1
    // Value: (1 << 10) | (1 << 13) = 0x2400
    outw_power(ACPI_PM1A_CNT, 0x2400);
    
    // If ACPI sleep doesn't work, just halt with interrupts enabled
    // This allows the system to wake on any interrupt (keyboard, timer, etc.)
    enable_interrupts();
    halt_cpu();
    
    // We'll wake up here when an interrupt occurs
#elif defined(ARCH_RISCV64)
    // RISC-V: use WFI (wait for interrupt) instruction
    asm volatile ("wfi");
#else
    // Other architectures: halt with interrupts enabled
#if defined(_MSC_VER)
    // MSVC: no standard way, just return
#else
    asm volatile ("sti; hlt");
#endif
#endif
}

namespace kernel {
namespace desktop {

// ============================================================
// Bitmap font (5x7, ASCII 32..126) - ported from bitmap_font.h
// ============================================================

static const int kGlyphW = 5;
static const int kGlyphH = 7;
static const int kGlyphSpacing = 1;
static const int kGlyphCount = 95;

static const uint8_t s_glyphs[kGlyphCount][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 32 ' '
    {0x00,0x00,0x5F,0x00,0x00}, // 33 '!'
    {0x00,0x07,0x00,0x07,0x00}, // 34 '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // 35 '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // 36 '$'
    {0x23,0x13,0x08,0x64,0x62}, // 37 '%'
    {0x36,0x49,0x55,0x22,0x50}, // 38 '&'
    {0x00,0x05,0x03,0x00,0x00}, // 39 '''
    {0x00,0x1C,0x22,0x41,0x00}, // 40 '('
    {0x00,0x41,0x22,0x1C,0x00}, // 41 ')'
    {0x14,0x08,0x3E,0x08,0x14}, // 42 '*'
    {0x08,0x08,0x3E,0x08,0x08}, // 43 '+'
    {0x00,0x50,0x30,0x00,0x00}, // 44 ','
    {0x08,0x08,0x08,0x08,0x08}, // 45 '-'
    {0x00,0x60,0x60,0x00,0x00}, // 46 '.'
    {0x20,0x10,0x08,0x04,0x02}, // 47 '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // 48 '0'
    {0x00,0x42,0x7F,0x40,0x00}, // 49 '1'
    {0x42,0x61,0x51,0x49,0x46}, // 50 '2'
    {0x21,0x41,0x45,0x4B,0x31}, // 51 '3'
    {0x18,0x14,0x12,0x7F,0x10}, // 52 '4'
    {0x27,0x45,0x45,0x45,0x39}, // 53 '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // 54 '6'
    {0x01,0x71,0x09,0x05,0x03}, // 55 '7'
    {0x36,0x49,0x49,0x49,0x36}, // 56 '8'
    {0x06,0x49,0x49,0x29,0x1E}, // 57 '9'
    {0x00,0x36,0x36,0x00,0x00}, // 58 ':'
    {0x00,0x56,0x36,0x00,0x00}, // 59 ';'
    {0x08,0x14,0x22,0x41,0x00}, // 60 '<'
    {0x14,0x14,0x14,0x14,0x14}, // 61 '='
    {0x00,0x41,0x22,0x14,0x08}, // 62 '>'
    {0x02,0x01,0x51,0x09,0x06}, // 63 '?'
    {0x32,0x49,0x79,0x41,0x3E}, // 64 '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 65 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 66 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 67 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 68 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 69 'E'
    {0x7F,0x09,0x09,0x09,0x01}, // 70 'F'
    {0x3E,0x41,0x49,0x49,0x7A}, // 71 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 72 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 73 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 74 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 75 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 76 'L'
    {0x7F,0x02,0x0C,0x02,0x7F}, // 77 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 78 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 79 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 80 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 81 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 82 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 83 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 84 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 85 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 86 'V'
    {0x3F,0x40,0x38,0x40,0x3F}, // 87 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 88 'X'
    {0x07,0x08,0x70,0x08,0x07}, // 89 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 90 'Z'
    {0x00,0x7F,0x41,0x41,0x00}, // 91 '['
    {0x02,0x04,0x08,0x10,0x20}, // 92 backslash
    {0x00,0x41,0x41,0x7F,0x00}, // 93 ']'
    {0x04,0x02,0x01,0x02,0x04}, // 94 '^'
    {0x40,0x40,0x40,0x40,0x40}, // 95 '_'
    {0x00,0x01,0x02,0x04,0x00}, // 96 '`'
    {0x20,0x54,0x54,0x54,0x78}, // 97 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 98 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 99 'c'
    {0x38,0x44,0x44,0x48,0x7F}, //100 'd'
    {0x38,0x54,0x54,0x54,0x18}, //101 'e'
    {0x08,0x7E,0x09,0x01,0x02}, //102 'f'
    {0x0C,0x52,0x52,0x52,0x3E}, //103 'g'
    {0x7F,0x08,0x04,0x04,0x78}, //104 'h'
    {0x00,0x44,0x7D,0x40,0x00}, //105 'i'
    {0x20,0x40,0x44,0x3D,0x00}, //106 'j'
    {0x7F,0x10,0x28,0x44,0x00}, //107 'k'
    {0x00,0x41,0x7F,0x40,0x00}, //108 'l'
    {0x7C,0x04,0x18,0x04,0x78}, //109 'm'
    {0x7C,0x08,0x04,0x04,0x78}, //110 'n'
    {0x38,0x44,0x44,0x44,0x38}, //111 'o'
    {0x7C,0x14,0x14,0x14,0x08}, //112 'p'
    {0x08,0x14,0x14,0x18,0x7C}, //113 'q'
    {0x7C,0x08,0x04,0x04,0x08}, //114 'r'
    {0x48,0x54,0x54,0x54,0x20}, //115 's'
    {0x04,0x3F,0x44,0x40,0x20}, //116 't'
    {0x3C,0x40,0x40,0x20,0x7C}, //117 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, //118 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, //119 'w'
    {0x44,0x28,0x10,0x28,0x44}, //120 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, //121 'y'
    {0x44,0x64,0x54,0x4C,0x44}, //122 'z'
    {0x00,0x08,0x36,0x41,0x00}, //123 '{'
    {0x00,0x00,0x7F,0x00,0x00}, //124 '|'
    {0x00,0x41,0x36,0x08,0x00}, //125 '}'
    {0x10,0x08,0x08,0x10,0x08}, //126 '~'
};

static const uint8_t* glyph(char c)
{
    int idx = (int)(unsigned char)c - 32;
    if (idx < 0 || idx >= kGlyphCount) return nullptr;
    return s_glyphs[idx];
}

static int measure_text(const char* str)
{
    int len = 0;
    while (str[len]) len++;
    if (len == 0) return 0;
    return len * (kGlyphW + kGlyphSpacing) - kGlyphSpacing;
}

// Draw a single character at (px, py) with scale factor
static void draw_char(uint32_t px, uint32_t py, char c, uint32_t color, int scale)
{
    const uint8_t* g = glyph(c);
    if (!g) return;
    for (int col = 0; col < kGlyphW; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < kGlyphH; row++) {
            if (bits & (1 << row)) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        framebuffer::put_pixel(px + col * scale + sx, py + row * scale + sy, color);
            }
        }
    }
}

// Draw a null-terminated string
static void draw_text(uint32_t x, uint32_t y, const char* str, uint32_t color, int scale = 1)
{
    uint32_t cx = x;
    while (*str) {
        draw_char(cx, y, *str, color, scale);
        cx += (kGlyphW + kGlyphSpacing) * scale;
        str++;
    }
}

// Draw text centered horizontally within a region
static void draw_text_centered(uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh,
                                const char* str, uint32_t color, int scale = 1)
{
    int tw = measure_text(str) * scale;
    int th = kGlyphH * scale;
    uint32_t tx = rx + (rw > (uint32_t)tw ? (rw - tw) / 2 : 0);
    uint32_t ty = ry + (rh > (uint32_t)th ? (rh - th) / 2 : 0);
    draw_text(tx, ty, str, color, scale);
}

// ============================================================
// Color helpers
// ============================================================

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t lerp_color(uint32_t c1, uint32_t c2, uint32_t num, uint32_t den)
{
    if (den == 0) return c1;
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = (uint8_t)(r1 + (int)(r2 - r1) * (int)num / (int)den);
    uint8_t g = (uint8_t)(g1 + (int)(g2 - g1) * (int)num / (int)den);
    uint8_t b = (uint8_t)(b1 + (int)(b2 - b1) * (int)num / (int)den);
    return rgb(r, g, b);
}

// ============================================================
// Desktop state
// ============================================================


static bool s_initialized = false;
static bool s_startMenuOpen = false;
static bool s_rightClickMenuOpen = false;
static uint32_t s_rightClickX = 0;
static uint32_t s_rightClickY = 0;
static uint32_t s_screenW = 0;
static uint32_t s_screenH = 0;

// Shutdown dialog state
static bool s_shutdownDialogOpen = false;
static int s_shutdownDialogHover = -1;  // 0 = Yes, 1 = No, -1 = none

// Control Panel window state
static bool s_controlPanelOpen = false;
static int s_controlPanelHover = -1;  // 0 = Device Manager, 1 = Network Adapters, -1 = none

// Device Manager window state
static bool s_deviceManagerOpen = false;
static int s_deviceManagerScroll = 0;
static int s_deviceManagerSelected = -1;

// Network Adapters window state
static bool s_networkAdaptersOpen = false;
static int s_networkAdapterSelected = -1;
static int s_networkAdapterHover = -1;

// Network Configuration dialog state
static bool s_networkConfigOpen = false;
static int s_networkConfigFocusField = -1;  // Which IP octet field is focused
static bool s_networkConfigUseDHCP = true;
static bool s_networkConfigUseDHCPforDNS = true;
// IP configuration values (4 octets each)
static uint8_t s_netConfigIP[4] = {0, 0, 0, 0};
static uint8_t s_netConfigMask[4] = {255, 255, 255, 0};
static uint8_t s_netConfigGateway[4] = {0, 0, 0, 0};
static uint8_t s_netConfigDNS[4] = {0, 0, 0, 0};
static int s_networkConfigBtnHover = -1;  // 0 = OK, 1 = Cancel

// Layout constants
static const uint32_t kTaskbarH = 40;
static const uint32_t kStartBtnW = 100;
static const uint32_t kSearchBoxW = 160;
static const uint32_t kSearchBoxH = 24;
static const uint32_t kShowDesktopW = 6;
static const uint32_t kStartMenuW = 420;
static const uint32_t kStartMenuItemH = 28;
static const uint32_t kStartMenuRightColW = 160;
static const uint32_t kIconSize = 48;
static const uint32_t kIconCellW = 80;
static const uint32_t kIconCellH = 76;
static const uint32_t kIconMargin = 24;
static const uint32_t kTrayIconSize = 16;
static const uint32_t kTrayIconGap = 6;
static const uint32_t kTaskbarBtnMaxW = 150;
static const uint32_t kTaskbarBtnH = 28;
static const uint32_t kTaskbarBtnGap = 4;

// Desktop icon entries with enhanced management
struct DesktopIcon {
    const char* label;
    uint32_t color;
    bool pinned;      // True if pinned to desktop
    bool recent;      // True if in recent apps list
    int32_t savedX;   // Saved X position (-1 = use default grid)
    int32_t savedY;   // Saved Y position (-1 = use default grid)
};

// Desktop icons: now mutable to support pin/unpin
static DesktopIcon s_desktopIcons[] = {
    {"Notepad",     0xFF78B450, true, false, -1, -1},  // green, pinned
    {"Calculator",  0xFF4690C8, true, false, -1, -1},  // blue, pinned
    {"Console",     0xFF78B450, true, false, -1, -1},  // green, pinned
    {"Paint",       0xFFC87830, false, true, -1, -1},  // orange, recent
    {"Clock",       0xFF4690C8, false, true, -1, -1},  // blue, recent
    {"TaskManager", 0xFFB44646, true, false, -1, -1},  // red, pinned (matches registered app name)
    {"Files",       0xFFC8B43C, false, true, -1, -1},  // yellow, recent
    {"ImgViewer",   0xFFC87830, false, false, -1, -1}, // orange
};
static const int kDesktopIconCount = 8;
static const int kMaxRecentApps = 5;  // Max recent apps to show

// Start menu entries structure for dynamic list
struct StartMenuApp {
    const char* name;
    bool pinned;
    bool recent;
    uint32_t color;
};

// Start menu application list (pinned + recent, matching desktop icons)
static StartMenuApp s_startMenuApps[] = {
    {"Calculator",  true,  false, 0xFF4690C8},  // pinned
    {"Notepad",     true,  false, 0xFF78B450},  // pinned
    {"Console",     true,  false, 0xFF78B450},  // pinned
    {"TaskManager", true,  false, 0xFFB44646},  // pinned
    {"Paint",       false, true,  0xFFC87830},  // recent
    {"Clock",       false, true,  0xFF4690C8},  // recent
    {"Files",       false, true,  0xFFC8B43C},  // recent
    {"ImgViewer",   false, false, 0xFFC87830},  // not shown by default
};
static const int kStartMenuAppCount = 8;
static const int kMaxStartMenuRecent = 5;  // Max recent apps in start menu

// All Programs alphabetically sorted list (for "All Programs" view)
static const char* s_allProgramsList[] = {
    "Calculator",
    "Clock",
    "Console",
    "Files",
    "ImgViewer",
    "Notepad",
    "Paint",
    "TaskManager",
};
static const int kAllProgramsCount = 8;

// Start menu right column entries (system shortcuts, matching Legacy StartMenu.cs)
struct StartMenuRightItem {
    const char* label;
    uint32_t color;
};

static const StartMenuRightItem s_startMenuRight[] = {
    {"Computer",      0xFF4690C8},
    {"Documents",     0xFFC8B43C},
    {"Pictures",      0xFFC87830},
    {"Music",         0xFF9050B4},
    {"Network",       0xFF50B478},
    {"Control Panel", 0xFF808890},
    {"Settings",      0xFF606878},
};
static const int kStartMenuRightCount = 7;

// Simulated running windows for taskbar buttons
struct TaskbarEntry {
    const char* title;
    uint32_t color;
    bool active;
};

static TaskbarEntry s_taskbarEntries[] = {
    {"Welcome", 0xFF4690C8, true},
};
static const int kTaskbarEntryCount = 1;

// Right-click context menu entries
static const char* s_contextMenuItems[] = {
    "Refresh",
    "Display Settings",
    "Personalize",
    "New Folder",
    "Open Terminal",
    "TaskManager",
};
static const int kContextMenuCount = 6;
static const uint32_t kContextMenuW = 160;
static const uint32_t kContextMenuItemH = 24;

// Notification toast
struct NotificationToast {
    const char* title;
    const char* message;
    bool visible;
};

static NotificationToast s_notification = {
    "Welcome to guideXOS",
    "System started successfully",
    true
};

// ============================================================
// Wallpaper Configuration
// ============================================================

enum class WallpaperType {
    Gradient,      // Gradient from top to bottom
    SolidColor,    // Single solid color
    Grid,          // Grid pattern overlay
    Custom         // Custom image (placeholder for future)
};

struct WallpaperConfig {
    WallpaperType type;
    uint32_t topColor;       // For gradient or solid color
    uint32_t bottomColor;    // For gradient
    bool showBranding;       // Show "guideXOS" branding
    bool showGrid;           // Show subtle grid overlay
    uint32_t gridColor;      // Grid line color
    uint32_t gridSpacing;    // Grid spacing in pixels
    
    WallpaperConfig() 
        : type(WallpaperType::Gradient),
          topColor(0xFF142850),    // Dark blue top
          bottomColor(0xFF0F121C), // Darker blue bottom
          showBranding(true),
          showGrid(true),
          gridColor(0xFF192337),
          gridSpacing(100) {}
};

static WallpaperConfig s_wallpaperConfig;

// ============================================================
// Time and Date Tracking
// ============================================================

struct DateTime {
    int hour;      // 0-23
    int minute;    // 0-59
    int second;    // 0-59
    int day;       // 1-31
    int month;     // 1-12
    int year;      // e.g., 2025
    
    DateTime() : hour(12), minute(0), second(0), day(1), month(1), year(2025) {}
};

static DateTime s_currentTime;

// Simple helper to format time string "HH:MM"
static void format_time_string(char* buffer, int bufSize)
{
    if (bufSize < 6) return;  // Need at least "HH:MM\0"
    
    // Format hours (0-23 to 12-hour format)
    int displayHour = s_currentTime.hour;
    if (displayHour == 0) displayHour = 12;
    else if (displayHour > 12) displayHour -= 12;
    
    buffer[0] = (displayHour >= 10) ? ('0' + displayHour / 10) : ' ';
    buffer[1] = '0' + (displayHour % 10);
    buffer[2] = ':';
    buffer[3] = '0' + (s_currentTime.minute / 10);
    buffer[4] = '0' + (s_currentTime.minute % 10);
    buffer[5] = '\0';
}

// Simple helper to format date string "M/D/YYYY"
static void format_date_string(char* buffer, int bufSize)
{
    if (bufSize < 11) return;  // Need space for "MM/DD/YYYY\0"
    
    int pos = 0;
    
    // Month
    if (s_currentTime.month >= 10) {
        buffer[pos++] = '0' + (s_currentTime.month / 10);
    }
    buffer[pos++] = '0' + (s_currentTime.month % 10);
    buffer[pos++] = '/';
    
    // Day
    if (s_currentTime.day >= 10) {
        buffer[pos++] = '0' + (s_currentTime.day / 10);
    }
    buffer[pos++] = '0' + (s_currentTime.day % 10);
    buffer[pos++] = '/';
    
    // Year
    buffer[pos++] = '0' + (s_currentTime.year / 1000);
    buffer[pos++] = '0' + ((s_currentTime.year / 100) % 10);
    buffer[pos++] = '0' + ((s_currentTime.year / 10) % 10);
    buffer[pos++] = '0' + (s_currentTime.year % 10);
    buffer[pos] = '\0';
}

// Update time (called from tick, increments by 1 second)
static void update_time()
{
    s_currentTime.second++;
    
    if (s_currentTime.second >= 60) {
        s_currentTime.second = 0;
        s_currentTime.minute++;
        
        if (s_currentTime.minute >= 60) {
            s_currentTime.minute = 0;
            s_currentTime.hour++;
            
            if (s_currentTime.hour >= 24) {
                s_currentTime.hour = 0;
                s_currentTime.day++;
                
                // Simple month day limits (not accounting for leap years)
                int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                int maxDays = (s_currentTime.month >= 1 && s_currentTime.month <= 12) 
                    ? daysInMonth[s_currentTime.month - 1] : 31;
                
                if (s_currentTime.day > maxDays) {
                    s_currentTime.day = 1;
                    s_currentTime.month++;
                    
                    if (s_currentTime.month > 12) {
                        s_currentTime.month = 1;
                        s_currentTime.year++;
                    }
                }
            }
        }
    }
}

// Per-icon positions (mutable, set to grid layout on init)
static int32_t s_iconPosX[8];
static int32_t s_iconPosY[8];
static bool    s_iconPositionsInitialized = false;

// Selected desktop icon (-1 = none)
static int s_selectedIcon = -1;

// Icon management helpers
static int s_visibleIconCount = 0;  // Count of icons to display (pinned + recent)
static int s_visibleIconIndices[8]; // Indices of visible icons in display order

// Drag state for icon repositioning
static bool  s_dragging = false;
static int   s_dragIconIndex = -1;
static int32_t s_dragOffsetX = 0;
static int32_t s_dragOffsetY = 0;
static int32_t s_dragStartMouseX = 0;
static int32_t s_dragStartMouseY = 0;
static int32_t s_dragCurrentX = 0;  // current mouse X during drag
static int32_t s_dragCurrentY = 0;  // current mouse Y during drag
static const int32_t kDragThreshold = 4;  // pixels before drag starts
static bool  s_dragStarted = false;  // true once threshold exceeded

// Double-click detection for icons
static int s_lastClickedIcon = -1;
static uint32_t s_lastClickTime = 0;
static const uint32_t kDoubleClickMs = 500;  // Max ms between clicks for double-click
static uint32_t s_tickCounter = 0;  // Incremented by main loop

// Icon management function prototypes
static void refresh_desktop_icons();      // Rebuild visible icon list
static void add_to_recent(const char* appName);  // Add app to recent list
static void pin_icon(const char* appName);       // Pin app to desktop
static void unpin_icon(const char* appName);     // Unpin app from desktop
static int find_icon_by_name(const char* name);  // Find icon index by name
static void initialize_icon_positions();         // Set up initial icon grid layout
static void save_icon_position(int iconIndex);   // Save position after drag

// Start menu state and navigation
static int s_hoverMenuLeft  = -1;   // hovered left-column item index
static int s_hoverMenuRight = -1;   // hovered right-column item index
static int s_clickedMenuLeft  = -1; // clicked left-column item index
static int s_clickedMenuRight = -1; // clicked right-column item index

// Enhanced start menu state (matching compositor.cpp)
static int s_startMenuSelection = 0;    // Currently selected item (keyboard nav)
static int s_startMenuScroll = 0;       // Scroll offset for long lists
static bool s_startMenuAllProgs = false; // Toggle between Recent/Pinned vs All Programs
static const int kStartMenuMaxRows = 14; // Max visible rows before scrolling
static const int kStartMenuRowH = 20;    // Height of each menu row

// Start menu helper functions
static void refresh_start_menu_list();   // Rebuild visible start menu items
static void add_to_start_menu_recent(const char* appName);  // Add to recent list
static int get_start_menu_item_count();  // Get current item count (pinned+recent or all)

// Shell window state
static int32_t s_shellPosX = -1;    // Shell window X position (-1 = centered)
static int32_t s_shellPosY = -1;    // Shell window Y position (-1 = centered)
static int32_t s_shellW = -1;       // Shell window width (-1 = default)
static int32_t s_shellH = -1;       // Shell window height (-1 = default)
static bool    s_shellActive = true; // Whether shell window is active (focused)
static bool    s_shellMinimized = false; // Whether shell is minimized to taskbar
static bool    s_shellMaximized = false; // Whether shell is maximized
static const uint32_t kShellDefaultW = 700;
static const uint32_t kShellDefaultH = 450;
static const uint32_t kShellMinW = 400;     // Minimum resize width
static const uint32_t kShellMinH = 200;     // Minimum resize height
static const uint32_t kShellTitlebarH = 24;
static const uint32_t kShellResizeMargin = 8; // Corner resize area size

// Saved position/size before maximize (to restore)
static int32_t s_shellSavedX = -1;
static int32_t s_shellSavedY = -1;
static int32_t s_shellSavedW = -1;
static int32_t s_shellSavedH = -1;

// Shell window dragging state
static bool    s_shellDragging = false;
static int32_t s_shellDragOffsetX = 0;
static int32_t s_shellDragOffsetY = 0;

// Shell window resize state
static bool    s_shellResizing = false;
static int32_t s_shellResizeStartX = 0;
static int32_t s_shellResizeStartY = 0;
static int32_t s_shellResizeStartW = 0;
static int32_t s_shellResizeStartH = 0;

// Forward declarations
static void draw_shell_window();
static int find_nearest_icon_in_direction(int currentIcon, int direction);
static void show_icon_notification(int iconIndex);

// ============================================================
// Icon Management Implementation
// ============================================================

// Find icon by name (case-insensitive comparison)
static int find_icon_by_name(const char* name)
{
    if (!name) return -1;
    
    for (int i = 0; i < kDesktopIconCount; i++) {
        const char* a = s_desktopIcons[i].label;
        const char* b = name;
        bool match = true;
        
        while (*a && *b) {
            char ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
            char cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
            if (ca != cb) {
                match = false;
                break;
            }
            a++;
            b++;
        }
        
        if (match && *a == *b) return i;
    }
    
    return -1;
}

// Rebuild visible icon list based on pinned and recent status
static void refresh_desktop_icons()
{
    s_visibleIconCount = 0;
    
    // First add all pinned icons
    for (int i = 0; i < kDesktopIconCount && s_visibleIconCount < 8; i++) {
        if (s_desktopIcons[i].pinned) {
            s_visibleIconIndices[s_visibleIconCount++] = i;
        }
    }
    
    // Then add recent icons (up to limit)
    int recentCount = 0;
    for (int i = 0; i < kDesktopIconCount && s_visibleIconCount < 8; i++) {
        if (s_desktopIcons[i].recent && !s_desktopIcons[i].pinned) {
            if (recentCount < kMaxRecentApps) {
                s_visibleIconIndices[s_visibleIconCount++] = i;
                recentCount++;
            }
        }
    }
}

// Add app to recent list
static void add_to_recent(const char* appName)
{
    int idx = find_icon_by_name(appName);
    if (idx < 0) return;
    
    // If already pinned, don't add to recent
    if (s_desktopIcons[idx].pinned) return;
    
    // Mark as recent
    s_desktopIcons[idx].recent = true;
    
    // Refresh visible icon list
    refresh_desktop_icons();
}

// Pin app to desktop
static void pin_icon(const char* appName)
{
    int idx = find_icon_by_name(appName);
    if (idx < 0) return;
    
    s_desktopIcons[idx].pinned = true;
    s_desktopIcons[idx].recent = false;  // Remove from recent if pinned
    
    // Refresh visible icon list
    refresh_desktop_icons();
}

// Unpin app from desktop
static void unpin_icon(const char* appName)
{
    int idx = find_icon_by_name(appName);
    if (idx < 0) return;
    
    s_desktopIcons[idx].pinned = false;
    
    // Optionally add to recent
    s_desktopIcons[idx].recent = true;
    
    // Refresh visible icon list
    refresh_desktop_icons();
}

// Initialize icon positions in grid layout
static void initialize_icon_positions()
{
    if (s_iconPositionsInitialized) return;
    
    refresh_desktop_icons();  // Build visible icon list first
    
    const int iconsPerRow = 8;
    
    for (int i = 0; i < s_visibleIconCount; i++) {
        int iconIdx = s_visibleIconIndices[i];
        
        // Check if icon has saved position
        if (s_desktopIcons[iconIdx].savedX >= 0 && s_desktopIcons[iconIdx].savedY >= 0) {
            s_iconPosX[i] = s_desktopIcons[iconIdx].savedX;
            s_iconPosY[i] = s_desktopIcons[iconIdx].savedY;
        } else {
            // Calculate default grid position
            int row = i / iconsPerRow;
            int col = i % iconsPerRow;
            s_iconPosX[i] = (int32_t)(kIconMargin + col * kIconCellW);
            s_iconPosY[i] = (int32_t)(kIconMargin + row * kIconCellH);
        }
    }
    
    s_iconPositionsInitialized = true;
}

// Save icon position after drag (store in icon structure)
static void save_icon_position(int displayIndex)
{
    if (displayIndex < 0 || displayIndex >= s_visibleIconCount) return;
    
    int iconIdx = s_visibleIconIndices[displayIndex];
    s_desktopIcons[iconIdx].savedX = s_iconPosX[displayIndex];
    s_desktopIcons[iconIdx].savedY = s_iconPosY[displayIndex];
    
    // In a real system, this would persist to disk/NVRAM
    // For now, positions are stored in memory only
}

// ============================================================
// Start Menu Management Implementation
// ============================================================

// Refresh start menu app list (sync with desktop icons for pinned/recent)
static void refresh_start_menu_list()
{
    // Update start menu apps to match desktop icon states
    for (int i = 0; i < kStartMenuAppCount; i++) {
        int iconIdx = find_icon_by_name(s_startMenuApps[i].name);
        if (iconIdx >= 0) {
            s_startMenuApps[i].pinned = s_desktopIcons[iconIdx].pinned;
            s_startMenuApps[i].recent = s_desktopIcons[iconIdx].recent;
        }
    }
}

// Add app to start menu recent list
static void add_to_start_menu_recent(const char* appName)
{
    // Find the app in the start menu list
    for (int i = 0; i < kStartMenuAppCount; i++) {
        if (s_startMenuApps[i].name == appName ||
            (s_startMenuApps[i].name[0] == appName[0] && 
             s_startMenuApps[i].name[1] == appName[1])) {
            
            // Don't add if already pinned
            if (s_startMenuApps[i].pinned) return;
            
            // Mark as recent
            s_startMenuApps[i].recent = true;
            return;
        }
    }
}

// Get current start menu item count based on mode
static int get_start_menu_item_count()
{
    if (s_startMenuAllProgs) {
        return kAllProgramsCount;
    } else {
        // Count pinned + recent items
        int count = 0;
        int recentCount = 0;
        
        // First count pinned
        for (int i = 0; i < kStartMenuAppCount; i++) {
            if (s_startMenuApps[i].pinned) count++;
        }
        
        // Then count recent (up to limit)
        for (int i = 0; i < kStartMenuAppCount && recentCount < kMaxStartMenuRecent; i++) {
            if (s_startMenuApps[i].recent && !s_startMenuApps[i].pinned) {
                count++;
                recentCount++;
            }
        }
        
        return count;
    }
}

// ============================================================
// Wallpaper Management
// ============================================================

// Set wallpaper to gradient mode
static void set_wallpaper_gradient(uint32_t topColor, uint32_t bottomColor)
{
    s_wallpaperConfig.type = WallpaperType::Gradient;
    s_wallpaperConfig.topColor = topColor;
    s_wallpaperConfig.bottomColor = bottomColor;
}

// Set wallpaper to solid color mode
static void set_wallpaper_solid(uint32_t color)
{
    s_wallpaperConfig.type = WallpaperType::SolidColor;
    s_wallpaperConfig.topColor = color;
}

// Toggle branding visibility
static void toggle_branding()
{
    s_wallpaperConfig.showBranding = !s_wallpaperConfig.showBranding;
}

// Toggle grid overlay
static void toggle_grid()
{
    s_wallpaperConfig.showGrid = !s_wallpaperConfig.showGrid;
}

// Set wallpaper to grid mode
static void set_wallpaper_grid(uint32_t topColor, uint32_t bottomColor, uint32_t gridColor, uint32_t spacing)
{
    s_wallpaperConfig.type = WallpaperType::Grid;
    s_wallpaperConfig.topColor = topColor;
    s_wallpaperConfig.bottomColor = bottomColor;
    s_wallpaperConfig.gridColor = gridColor;
    s_wallpaperConfig.gridSpacing = spacing;
}

// ============================================================
// Drawing routines
// ============================================================

// Draw a horizontal line
static void hline(uint32_t x, uint32_t y, uint32_t w, uint32_t color)
{
    for (uint32_t i = 0; i < w; i++)
        framebuffer::put_pixel(x + i, y, color);
}

// Draw a vertical line
static void vline(uint32_t x, uint32_t y, uint32_t h, uint32_t color)
{
    for (uint32_t i = 0; i < h; i++)
        framebuffer::put_pixel(x, y + i, color);
}

// Draw a rectangle outline
static void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    hline(x, y, w, color);
    hline(x, y + h - 1, w, color);
    vline(x, y, h, color);
    vline(x + w - 1, y, h, color);
}

// ============================================================
// Background - gradient + branding (matches desktop_wallpaper.h)
// ============================================================

static void draw_background()
{
    uint32_t w = s_screenW;
    uint32_t h = s_screenH - kTaskbarH;

    // Draw wallpaper based on configured type
    switch (s_wallpaperConfig.type) {
        case WallpaperType::Gradient: {
            // Gradient from top to bottom
            for (uint32_t y = 0; y < h; y++) {
                uint32_t lineColor = lerp_color(
                    s_wallpaperConfig.topColor, 
                    s_wallpaperConfig.bottomColor, 
                    y, 
                    h > 1 ? h - 1 : 1
                );
                framebuffer::fill_rect(0, y, w, 1, lineColor);
            }
            break;
        }
        
        case WallpaperType::SolidColor: {
            // Single solid color
            framebuffer::fill_rect(0, 0, w, h, s_wallpaperConfig.topColor);
            break;
        }
        
        case WallpaperType::Grid: {
            // Gradient base with prominent grid
            for (uint32_t y = 0; y < h; y++) {
                uint32_t lineColor = lerp_color(
                    s_wallpaperConfig.topColor, 
                    s_wallpaperConfig.bottomColor, 
                    y, 
                    h > 1 ? h - 1 : 1
                );
                framebuffer::fill_rect(0, y, w, 1, lineColor);
            }
            // Draw more prominent grid
            uint32_t gridCol = s_wallpaperConfig.gridColor;
            for (uint32_t x = s_wallpaperConfig.gridSpacing; x < w; x += s_wallpaperConfig.gridSpacing)
                vline(x, 0, h, gridCol);
            for (uint32_t y = s_wallpaperConfig.gridSpacing; y < h; y += s_wallpaperConfig.gridSpacing)
                hline(0, y, w, gridCol);
            break;
        }
        
        case WallpaperType::Custom:
        default: {
            // Default to gradient
            for (uint32_t y = 0; y < h; y++) {
                uint32_t lineColor = lerp_color(
                    s_wallpaperConfig.topColor, 
                    s_wallpaperConfig.bottomColor, 
                    y, 
                    h > 1 ? h - 1 : 1
                );
                framebuffer::fill_rect(0, y, w, 1, lineColor);
            }
            break;
        }
    }

    // Optional subtle grid overlay (if enabled and not Grid type)
    if (s_wallpaperConfig.showGrid && s_wallpaperConfig.type != WallpaperType::Grid) {
        uint32_t gridColor = s_wallpaperConfig.gridColor;
        for (uint32_t x = s_wallpaperConfig.gridSpacing; x < w; x += s_wallpaperConfig.gridSpacing)
            vline(x, 0, h, gridColor);
        for (uint32_t y = s_wallpaperConfig.gridSpacing; y < h; y += s_wallpaperConfig.gridSpacing)
            hline(0, y, w, gridColor);
    }

    // Optional "guideXOS" branding
    if (s_wallpaperConfig.showBranding) {
        // Calculate branding colors based on wallpaper
        // Use darker/lighter version of top color for subtle branding
        uint32_t topCol = s_wallpaperConfig.topColor;
        uint8_t r = (uint8_t)((topCol >> 16) & 0xFF);
        uint8_t g = (uint8_t)((topCol >> 8) & 0xFF);
        uint8_t b = (uint8_t)(topCol & 0xFF);
        
        // Slightly lighter for main branding
        uint8_t br = (uint8_t)(r + 20 > 255 ? 255 : r + 20);
        uint8_t bg = (uint8_t)(g + 25 > 255 ? 255 : g + 25);
        uint8_t bb = (uint8_t)(b + 35 > 255 ? 255 : b + 35);
        uint32_t brandColor = rgb(br, bg, bb);
        
        // Even lighter for version text
        uint8_t vr = (uint8_t)(r + 30 > 255 ? 255 : r + 30);
        uint8_t vg = (uint8_t)(g + 35 > 255 ? 255 : g + 35);
        uint8_t vb = (uint8_t)(b + 45 > 255 ? 255 : b + 45);
        uint32_t verColor = rgb(vr, vg, vb);
        
        // "guideXOS" branding - large centered text (scale 4)
        {
            const char* brand = "guideXOS";
            int tw = measure_text(brand) * 4;
            uint32_t bx = (w > (uint32_t)tw) ? (w - tw) / 2 : 0;
            uint32_t by = h / 2 - 60;
            draw_text(bx, by, brand, brandColor, 4);
        }

        // Version subtitle (scale 2)
        {
            const char* ver = "Server Edition";
            int tw = measure_text(ver) * 2;
            uint32_t vx = (w > (uint32_t)tw) ? (w - tw) / 2 : 0;
            uint32_t vy = s_screenH / 2 - 60 + 4 * kGlyphH + 12;
            draw_text(vx, vy, ver, verColor, 2);
        }
    }
}

// ============================================================
// Desktop icons
// ============================================================

// Draw a simple icon glyph/symbol inside the icon square
static void draw_icon_symbol(uint32_t ix, uint32_t iy, uint32_t size, const char* appName)
{
    // Center coordinates
    uint32_t cx = ix + size / 2;
    uint32_t cy = iy + size / 2;
    uint32_t iconColor = rgb(255, 255, 255);
    
    // Draw different symbols based on app type
    // Using simple geometric shapes with bitmap font characters
    
    // Notepad - draw lines representing text
    if (appName[0] == 'N' && appName[1] == 'o') {  // "Notepad"
        for (int i = 0; i < 4; i++) {
            hline(cx - 12, cy - 8 + i * 5, 24, iconColor);
        }
        return;
    }
    
    // Calculator - draw grid pattern
    if (appName[0] == 'C' && appName[1] == 'a') {  // "Calculator"
        // Draw # symbol using bitmap font
        draw_text(cx - 3, cy - 4, "#", iconColor, 1);
        return;
    }
    
    // Console/Terminal - draw prompt symbol
    if (appName[0] == 'C' && appName[1] == 'o') {  // "Console"
        draw_text(cx - 3, cy - 4, ">", iconColor, 1);
        draw_text(cx + 2, cy - 4, "_", iconColor, 1);
        return;
    }
    
    // Paint - draw palette/brush
    if (appName[0] == 'P' && appName[1] == 'a') {  // "Paint"
        // Draw brush/pencil icon
        for (int i = 0; i < 10; i++) {
            framebuffer::put_pixel(cx - 6 + i, cy - 6 + i, iconColor);
            framebuffer::put_pixel(cx - 5 + i, cy - 6 + i, iconColor);
        }
        return;
    }
    
    // Clock - draw clock face
    if (appName[0] == 'C' && appName[1] == 'l') {  // "Clock"
        // Circle outline
        draw_rect(cx - 8, cy - 8, 16, 16, iconColor);
        // Clock hands
        vline(cx, cy - 6, 6, iconColor);  // Hour hand (vertical)
        hline(cx, cy, 6, iconColor);      // Minute hand (horizontal)
        return;
    }
    
    // TaskManager - draw chart/bars
    if (appName[0] == 'T' && appName[1] == 'a') {  // "TaskManager"
        // Bar chart representation
        for (int i = 0; i < 4; i++) {
            uint32_t barH = 4 + i * 3;
            framebuffer::fill_rect(cx - 8 + i * 5, cy + 8 - barH, 3, barH, iconColor);
        }
        return;
    }
    
    // Files - draw folder icon
    if (appName[0] == 'F' && appName[1] == 'i') {  // "Files"
        // Folder shape
        framebuffer::fill_rect(cx - 10, cy - 4, 8, 2, iconColor);  // Folder tab
        draw_rect(cx - 10, cy - 2, 20, 12, iconColor);  // Folder body
        return;
    }
    
    // ImageViewer - draw picture frame
    if (appName[0] == 'I' && appName[1] == 'm') {  // "ImgViewer"
        draw_rect(cx - 10, cy - 8, 20, 16, iconColor);
        // Simple mountain/image symbol inside
        for (int i = 0; i < 6; i++) {
            framebuffer::put_pixel(cx - 6 + i, cy + 4 - i/2, iconColor);
            framebuffer::put_pixel(cx + i, cy + 4 - i/3, iconColor);
        }
        return;
    }
    
    // Default - draw first letter of app name
    if (appName[0] >= 'A' && appName[0] <= 'Z') {
        char letter[2] = { appName[0], '\0' };
        draw_text(cx - 3, cy - 4, letter, iconColor, 1);
    }
}

static void draw_desktop_icons()
{
    uint32_t deskH = s_screenH - kTaskbarH;

    // Draw visible icons only (pinned + recent)
    for (int displayIdx = 0; displayIdx < s_visibleIconCount; displayIdx++) {
        // Skip drawing the icon being dragged (it will be drawn at cursor)
        if (s_dragging && s_dragStarted && displayIdx == s_dragIconIndex) continue;

        int iconIdx = s_visibleIconIndices[displayIdx];
        uint32_t cx = (uint32_t)s_iconPosX[displayIdx];
        uint32_t cy = (uint32_t)s_iconPosY[displayIdx];

        if (cy + kIconCellH > deskH) continue;

        // Selection highlight background (matching compositor style)
        if (displayIdx == s_selectedIcon) {
            framebuffer::fill_rect(cx, cy, kIconCellW, kIconCellH, rgb(50, 90, 160));
            draw_rect(cx, cy, kIconCellW, kIconCellH, rgb(100, 160, 240));
        }

        // Icon background square with rounded appearance
        uint32_t ix = cx + (kIconCellW - kIconSize) / 2;
        uint32_t iy = cy + 4;
        
        // Draw icon background with base color
        framebuffer::fill_rect(ix, iy, kIconSize, kIconSize, s_desktopIcons[iconIdx].color);

        // Lighter top edge for depth effect
        uint32_t baseColor = s_desktopIcons[iconIdx].color;
        uint8_t br = (uint8_t)(((baseColor >> 16) & 0xFF));
        uint8_t bg = (uint8_t)(((baseColor >> 8) & 0xFF));
        uint8_t bb = (uint8_t)((baseColor & 0xFF));
        uint8_t lr = (uint8_t)(br + 30 > 255 ? 255 : br + 30);
        uint8_t lg = (uint8_t)(bg + 30 > 255 ? 255 : bg + 30);
        uint8_t lb = (uint8_t)(bb + 30 > 255 ? 255 : bb + 30);
        hline(ix + 1, iy + 1, kIconSize - 2, rgb(lr, lg, lb));
        hline(ix + 1, iy + 2, kIconSize - 2, rgb(lr, lg, lb));

        // Draw app-specific icon symbol
        draw_icon_symbol(ix, iy, kIconSize, s_desktopIcons[iconIdx].label);

        // Icon border with subtle 3D effect
        draw_rect(ix, iy, kIconSize, kIconSize, rgb(200, 200, 220));
        // Darker bottom/right edge for depth
        hline(ix + 1, iy + kIconSize - 1, kIconSize - 1, rgb(80, 80, 100));
        vline(ix + kIconSize - 1, iy + 1, kIconSize - 1, rgb(80, 80, 100));

        // Pin indicator (bright star in top-right corner if pinned)
        if (s_desktopIcons[iconIdx].pinned) {
            draw_text(ix + kIconSize - 8, iy + 2, "*", rgb(255, 220, 80), 1);
        }

        // Label with shadow for better readability
        uint32_t labelY = iy + kIconSize + 4;
        const char* lbl = s_desktopIcons[iconIdx].label;
        int tw = measure_text(lbl);
        uint32_t lx = cx + (kIconCellW > (uint32_t)tw ? (kIconCellW - tw) / 2 : 0);
        
        // Text shadow (slightly offset black text)
        draw_text(lx + 1, labelY + 1, lbl, rgb(0, 0, 0), 1);
        // Main text (bright white)
        draw_text(lx, labelY, lbl, rgb(240, 240, 250), 1);
    }

    // Draw the dragged icon ghost at current drag position
    if (s_dragging && s_dragStarted && s_dragIconIndex >= 0 && s_dragIconIndex < s_visibleIconCount) {
        int displayIdx = s_dragIconIndex;
        int iconIdx = s_visibleIconIndices[displayIdx];
        uint32_t cx = (uint32_t)(s_dragCurrentX - s_dragOffsetX);
        uint32_t cy = (uint32_t)(s_dragCurrentY - s_dragOffsetY);

        // Semi-transparent selection highlight (darker for dragging)
        framebuffer::fill_rect(cx, cy, kIconCellW, kIconCellH, rgb(30, 50, 90));
        draw_rect(cx, cy, kIconCellW, kIconCellH, rgb(80, 120, 180));

        uint32_t ix = cx + (kIconCellW - kIconSize) / 2;
        uint32_t iy = cy + 4;
        
        // Draw icon with slightly dimmed colors when dragging
        uint32_t dragColor = s_desktopIcons[iconIdx].color;
        uint8_t dr = (uint8_t)(((dragColor >> 16) & 0xFF) * 7 / 10);
        uint8_t dg = (uint8_t)(((dragColor >> 8) & 0xFF) * 7 / 10);
        uint8_t db = (uint8_t)((dragColor & 0xFF) * 7 / 10);
        framebuffer::fill_rect(ix, iy, kIconSize, kIconSize, rgb(dr, dg, db));
        
        // Draw icon symbol
        draw_icon_symbol(ix, iy, kIconSize, s_desktopIcons[iconIdx].label);
        
        draw_rect(ix, iy, kIconSize, kIconSize, rgb(140, 140, 160));

        // Pin indicator on dragged icon
        if (s_desktopIcons[iconIdx].pinned) {
            draw_text(ix + kIconSize - 8, iy + 2, "*", rgb(255, 220, 80), 1);
        }

        // Label
        uint32_t labelY = iy + kIconSize + 4;
        const char* lbl = s_desktopIcons[iconIdx].label;
        int tw = measure_text(lbl);
        uint32_t lx = cx + (kIconCellW > (uint32_t)tw ? (kIconCellW - tw) / 2 : 0);
        draw_text(lx + 1, labelY + 1, lbl, rgb(0, 0, 0), 1);
        draw_text(lx, labelY, lbl, rgb(200, 200, 210), 1);
    }
}

// ============================================================
// System tray (network bars, volume, battery)
// ============================================================

static void draw_network_icon(uint32_t x, uint32_t y)
{
    // 4 signal bars, all lit (green)
    for (int i = 0; i < 4; i++) {
        uint32_t barH = 4 + (uint32_t)i * 3;
        uint32_t barW = 3;
        uint32_t bx = x + (uint32_t)i * 4;
        uint32_t by = y + kTrayIconSize - barH;
        framebuffer::fill_rect(bx, by, barW, barH, rgb(100, 200, 100));
    }
}

static void draw_volume_icon(uint32_t x, uint32_t y)
{
    // Speaker body
    framebuffer::fill_rect(x + 2, y + 5, 4, 6, rgb(180, 180, 190));
    // Speaker cone (triangle approximation)
    for (int i = 0; i < 5; i++) {
        framebuffer::fill_rect(x + 6, y + 4 - i/2, 1, 8 + i, rgb(180, 180, 190));
    }
    // Sound waves (two arcs as vertical bars)
    vline(x + 12, y + 4, 8, rgb(130, 130, 150));
    vline(x + 14, y + 2, 12, rgb(100, 100, 120));
}

static void draw_battery_icon(uint32_t x, uint32_t y)
{
    // Battery outline
    draw_rect(x + 1, y + 4, 12, 8, rgb(180, 180, 190));
    // Battery tip
    framebuffer::fill_rect(x + 13, y + 6, 2, 4, rgb(180, 180, 190));
    // Battery fill (green = full)
    framebuffer::fill_rect(x + 3, y + 6, 8, 4, rgb(100, 200, 100));
}

static void draw_system_tray(uint32_t trayX, uint32_t taskbarY)
{
    uint32_t iconY = taskbarY + (kTaskbarH - kTrayIconSize) / 2;

    // Separator line
    vline(trayX - 2, taskbarY + 4, kTaskbarH - 8, rgb(80, 80, 90));

    uint32_t cx = trayX + 4;
    draw_network_icon(cx, iconY);
    cx += kTrayIconSize + kTrayIconGap;
    draw_volume_icon(cx, iconY);
    cx += kTrayIconSize + kTrayIconGap;
    draw_battery_icon(cx, iconY);
}

// ============================================================
// Search box (taskbar, matching Legacy/compositor drawTaskbarSearchBox)
// ============================================================

static void draw_search_box(uint32_t x, uint32_t y)
{
    // Search box background
    framebuffer::fill_rect(x, y, kSearchBoxW, kSearchBoxH, rgb(35, 35, 45));
    draw_rect(x, y, kSearchBoxW, kSearchBoxH, rgb(70, 80, 100));

    // Magnifying glass icon (small circle + handle)
    uint32_t iconX = x + 6;
    uint32_t iconY = y + 5;
    // Circle (approximated as small square with gap)
    draw_rect(iconX, iconY, 10, 10, rgb(120, 130, 150));
    // Handle (diagonal line approximation)
    framebuffer::put_pixel(iconX + 10, iconY + 10, rgb(120, 130, 150));
    framebuffer::put_pixel(iconX + 11, iconY + 11, rgb(120, 130, 150));
    framebuffer::put_pixel(iconX + 12, iconY + 12, rgb(120, 130, 150));

    // Placeholder text
    draw_text(x + 22, y + (kSearchBoxH - kGlyphH) / 2, "Search...", rgb(100, 105, 120), 1);
}

// ============================================================
// Taskbar window buttons (matching Legacy Taskbar.cs button rendering)
// ============================================================

// Store terminal button bounds for click detection
static uint32_t s_terminalBtnX = 0;
static uint32_t s_terminalBtnY = 0;
static uint32_t s_terminalBtnW = 0;
static uint32_t s_terminalBtnH = 0;
static bool s_terminalBtnVisible = false;

static void draw_taskbar_buttons(uint32_t startX, uint32_t tbY, uint32_t maxX)
{
    uint32_t btnX = startX;
    uint32_t btnY = tbY + (kTaskbarH - kTaskbarBtnH) / 2;

    // Draw static taskbar entries
    for (int i = 0; i < kTaskbarEntryCount; i++) {
        if (btnX + kTaskbarBtnMaxW > maxX) break;

        uint32_t tw = (uint32_t)measure_text(s_taskbarEntries[i].title);
        uint32_t btnW = tw + 30;
        if (btnW > kTaskbarBtnMaxW) btnW = kTaskbarBtnMaxW;

        // Button background
        uint32_t bgColor = s_taskbarEntries[i].active
            ? rgb(70, 100, 150)
            : rgb(55, 58, 70);
        framebuffer::fill_rect(btnX, btnY, btnW, kTaskbarBtnH, bgColor);

        // Active indicator line at bottom (matching compositor)
        if (s_taskbarEntries[i].active) {
            framebuffer::fill_rect(btnX + 2, btnY + kTaskbarBtnH - 3, btnW - 4, 2, rgb(100, 160, 240));
        }

        // Small colored icon
        uint32_t iconSz = 14;
        uint32_t iconX = btnX + 4;
        uint32_t iconY2 = btnY + (kTaskbarBtnH - iconSz) / 2;
        framebuffer::fill_rect(iconX, iconY2, iconSz, iconSz, s_taskbarEntries[i].color);

        // Title text
        draw_text(btnX + 22, btnY + (kTaskbarBtnH - kGlyphH) / 2,
                  s_taskbarEntries[i].title, rgb(230, 230, 240), 1);

        btnX += btnW + kTaskbarBtnGap;
    }
    
    // Draw Terminal button if shell is open
    s_terminalBtnVisible = false;
    if (shell::is_open()) {
        if (btnX + kTaskbarBtnMaxW <= maxX) {
            const char* title = "Terminal";
            uint32_t tw = (uint32_t)measure_text(title);
            uint32_t btnW = tw + 30;
            if (btnW > kTaskbarBtnMaxW) btnW = kTaskbarBtnMaxW;
            
            // Store button bounds for click detection
            s_terminalBtnX = btnX;
            s_terminalBtnY = btnY;
            s_terminalBtnW = btnW;
            s_terminalBtnH = kTaskbarBtnH;
            s_terminalBtnVisible = true;
            
            // Button background - active if not minimized and is active
            bool isActive = !s_shellMinimized && s_shellActive;
            uint32_t bgColor = isActive ? rgb(70, 100, 150) : rgb(55, 58, 70);
            framebuffer::fill_rect(btnX, btnY, btnW, kTaskbarBtnH, bgColor);
            
            // Active indicator line at bottom
            if (isActive) {
                framebuffer::fill_rect(btnX + 2, btnY + kTaskbarBtnH - 3, btnW - 4, 2, rgb(100, 160, 240));
            }
            
            // Minimized indicator (dot) when minimized
            if (s_shellMinimized) {
                framebuffer::fill_rect(btnX + btnW/2 - 2, btnY + kTaskbarBtnH - 5, 4, 2, rgb(100, 160, 240));
            }
            
            // Terminal icon (green square)
            uint32_t iconSz = 14;
            uint32_t iconX = btnX + 4;
            uint32_t iconY2 = btnY + (kTaskbarBtnH - iconSz) / 2;
            framebuffer::fill_rect(iconX, iconY2, iconSz, iconSz, rgb(120, 180, 80));
            
            // Title text
            draw_text(btnX + 22, btnY + (kTaskbarBtnH - kGlyphH) / 2, title, rgb(230, 230, 240), 1);
        }
    }
}

// ============================================================
// Taskbar
// ============================================================

static void draw_taskbar()
{
    uint32_t tbY = s_screenH - kTaskbarH;

    // Taskbar background (dark gradient)
    uint32_t tbTop = rgb(45, 45, 55);
    uint32_t tbBot = rgb(30, 30, 38);
    for (uint32_t y = 0; y < kTaskbarH; y++) {
        uint32_t c = lerp_color(tbTop, tbBot, y, kTaskbarH - 1);
        framebuffer::fill_rect(0, tbY + y, s_screenW, 1, c);
    }

    // Top border highlight
    hline(0, tbY, s_screenW, rgb(70, 70, 85));

    // Start button (highlighted when menu is open)
    uint32_t btnY = tbY + 4;
    uint32_t btnH = kTaskbarH - 8;
    uint32_t btnColor = s_startMenuOpen ? rgb(70, 100, 150) : rgb(50, 70, 110);
    uint32_t btnBorder = s_startMenuOpen ? rgb(100, 140, 200) : rgb(90, 120, 180);
    framebuffer::fill_rect(4, btnY, kStartBtnW, btnH, btnColor);
    draw_rect(4, btnY, kStartBtnW, btnH, btnBorder);
    draw_text_centered(4, btnY, kStartBtnW, btnH, "guideXOS", rgb(240, 240, 255), 1);

    // Search box (after start button, matching compositor layout)
    uint32_t searchX = 4 + kStartBtnW + 8;
    uint32_t searchY = tbY + (kTaskbarH - kSearchBoxH) / 2;
    draw_search_box(searchX, searchY);

    // Taskbar window buttons (after search box)
    uint32_t taskBtnStart = searchX + kSearchBoxW + 8;

    // Calculate right-side reserved area
    uint32_t trayW = (kTrayIconSize + kTrayIconGap) * 3 + 12;
    uint32_t clockW = 70;  // Slightly wider for date
    uint32_t rightReserved = kShowDesktopW + trayW + clockW + 24;
    uint32_t taskBtnMaxX = s_screenW - rightReserved;

    draw_taskbar_buttons(taskBtnStart, tbY, taskBtnMaxX);

    // Clock and date area (right side, before tray)
    uint32_t clockX = s_screenW - kShowDesktopW - trayW - clockW - 16;
    
    // Format time and date strings
    char timeStr[6];
    char dateStr[11];
    format_time_string(timeStr, sizeof(timeStr));
    format_date_string(dateStr, sizeof(dateStr));
    
    // Time (centered in upper half)
    uint32_t timeY = tbY + 6;
    draw_text_centered(clockX, timeY, clockW, kTaskbarH / 2 - 4, timeStr, rgb(210, 210, 220), 1);
    
    // Date below time (smaller, lighter color)
    uint32_t dateY = tbY + kTaskbarH / 2 + 2;
    draw_text_centered(clockX, dateY, clockW, kTaskbarH / 2 - 4, dateStr, rgb(160, 160, 175), 1);

    // System tray
    uint32_t trayX = s_screenW - kShowDesktopW - trayW;
    draw_system_tray(trayX, tbY);

    // Show Desktop button (thin sliver on far right)
    uint32_t sdX = s_screenW - kShowDesktopW;
    framebuffer::fill_rect(sdX, tbY, kShowDesktopW, kTaskbarH, rgb(50, 50, 60));
    // Separator before show desktop
    vline(sdX, tbY + 4, kTaskbarH - 8, rgb(70, 75, 90));
    
    // Subtle vertical line pattern in show desktop area
    vline(sdX + 2, tbY + 10, kTaskbarH - 20, rgb(60, 60, 70));
}

// ============================================================
// Start menu
// ============================================================

static void draw_start_menu()
{
    if (!s_startMenuOpen) return;

    // Get current item count based on mode (pinned+recent or all programs)
    int itemCount = get_start_menu_item_count();
    int visibleRows = itemCount < kStartMenuMaxRows ? itemCount : kStartMenuMaxRows;
    
    // Two-column start menu matching compositor layout
    // Left column: app list, Right column: system shortcuts
    uint32_t headerH = 30;
    uint32_t footerH = 36;
    uint32_t bodyH = (uint32_t)visibleRows * kStartMenuRowH;
    uint32_t rightBodyH = (uint32_t)kStartMenuRightCount * kStartMenuRowH;
    uint32_t maxBodyH = bodyH > rightBodyH ? bodyH : rightBodyH;
    uint32_t menuH = headerH + maxBodyH + footerH;
    uint32_t menuX = 4;
    uint32_t menuY = s_screenH - kTaskbarH - menuH;
    uint32_t leftColW = kStartMenuW - kStartMenuRightColW;

    // Menu background
    framebuffer::fill_rect(menuX, menuY, kStartMenuW, menuH, rgb(40, 40, 50));
    draw_rect(menuX, menuY, kStartMenuW, menuH, rgb(80, 100, 140));

    // Header bar (user profile area)
    framebuffer::fill_rect(menuX + 1, menuY + 1, kStartMenuW - 2, headerH - 1, rgb(50, 70, 110));
    // User avatar placeholder
    framebuffer::fill_rect(menuX + 8, menuY + 6, 18, 18, rgb(90, 140, 200));
    draw_rect(menuX + 8, menuY + 6, 18, 18, rgb(130, 170, 230));
    // Username
    draw_text(menuX + 32, menuY + 10, "User", rgb(230, 230, 250), 1);
    hline(menuX + 1, menuY + headerH, kStartMenuW - 2, rgb(60, 70, 90));

    // === Left column: App list (pinned/recent or all programs) ===
    uint32_t leftX = menuX;
    uint32_t contentY = menuY + headerH + 1;

    // Left column background
    framebuffer::fill_rect(leftX + 1, contentY, leftColW - 1, maxBodyH, rgb(42, 42, 52));

    // Draw visible items with scroll
    for (int i = 0; i < visibleRows; i++) {
        int itemIndex = i + s_startMenuScroll;
        if (itemIndex >= itemCount) break;
        
        uint32_t itemY = contentY + (uint32_t)i * kStartMenuRowH;

        // Get app name and color based on mode
        const char* appName;
        uint32_t appColor;
        bool isPinned = false;
        
        if (s_startMenuAllProgs) {
            // All Programs mode - use sorted list
            appName = s_allProgramsList[itemIndex];
            // Find color from app list
            appColor = rgb(90, 130, 180); // default
            for (int j = 0; j < kStartMenuAppCount; j++) {
                if (s_startMenuApps[j].name[0] == appName[0] &&
                    s_startMenuApps[j].name[1] == appName[1]) {
                    appColor = s_startMenuApps[j].color;
                    isPinned = s_startMenuApps[j].pinned;
                    break;
                }
            }
        } else {
            // Pinned/Recent mode - build from app list
            int currentIdx = 0;
            bool found = false;
            
            // First iterate through pinned
            for (int j = 0; j < kStartMenuAppCount && currentIdx <= itemIndex; j++) {
                if (s_startMenuApps[j].pinned) {
                    if (currentIdx == itemIndex) {
                        appName = s_startMenuApps[j].name;
                        appColor = s_startMenuApps[j].color;
                        isPinned = true;
                        found = true;
                        break;
                    }
                    currentIdx++;
                }
            }
            
            // Then recent if not found
            if (!found) {
                int recentCount = 0;
                for (int j = 0; j < kStartMenuAppCount && recentCount < kMaxStartMenuRecent; j++) {
                    if (s_startMenuApps[j].recent && !s_startMenuApps[j].pinned) {
                        if (currentIdx == itemIndex) {
                            appName = s_startMenuApps[j].name;
                            appColor = s_startMenuApps[j].color;
                            isPinned = false;
                            found = true;
                            break;
                        }
                        currentIdx++;
                        recentCount++;
                    }
                }
            }
            
            if (!found) continue;
        }

        // Keyboard selection highlight (yellow/gold)
        if (itemIndex == s_startMenuSelection) {
            framebuffer::fill_rect(leftX + 1, itemY, leftColW - 2, kStartMenuRowH, rgb(90, 100, 60));
        }
        // Clicked item highlight (bright blue)
        else if (i == s_clickedMenuLeft) {
            framebuffer::fill_rect(leftX + 1, itemY, leftColW - 2, kStartMenuRowH, rgb(50, 90, 160));
        }
        // Hover highlight (subtle blue tint)
        else if (i == s_hoverMenuLeft) {
            framebuffer::fill_rect(leftX + 1, itemY, leftColW - 2, kStartMenuRowH, rgb(55, 60, 80));
        }
        // Alternate row shading
        else if (i % 2 == 0) {
            framebuffer::fill_rect(leftX + 1, itemY, leftColW - 2, kStartMenuRowH, rgb(46, 46, 58));
        }

        // Small colored icon square
        framebuffer::fill_rect(leftX + 10, itemY + 3, 16, 16, appColor);
        draw_rect(leftX + 10, itemY + 3, 16, 16, rgb(160, 160, 180));
        
        // Pin indicator (star) if pinned
        if (isPinned) {
            draw_text(leftX + 20, itemY + 4, "*", rgb(255, 220, 80), 1);
        }

        // App name
        uint32_t textColor = (itemIndex == s_startMenuSelection || i == s_clickedMenuLeft || i == s_hoverMenuLeft)
            ? rgb(255, 255, 255) : rgb(210, 210, 225);
        draw_text(leftX + 32, itemY + 6, appName, textColor, 1);
    }
    
    // Scroll indicators if needed
    if (s_startMenuScroll > 0) {
        // Up arrow indicator
        draw_text(leftX + leftColW - 16, contentY + 2, "^", rgb(180, 190, 210), 1);
    }
    if (s_startMenuScroll + visibleRows < itemCount) {
        // Down arrow indicator
        draw_text(leftX + leftColW - 16, contentY + maxBodyH - 12, "v", rgb(180, 190, 210), 1);
    }

    // Vertical divider between columns
    vline(leftX + leftColW, contentY, maxBodyH, rgb(60, 70, 90));

    // === Right column: System shortcuts ===
    uint32_t rightX = leftX + leftColW + 1;
    framebuffer::fill_rect(rightX, contentY, kStartMenuRightColW - 2, maxBodyH, rgb(38, 38, 48));

    for (int i = 0; i < kStartMenuRightCount; i++) {
        uint32_t itemY = contentY + (uint32_t)i * kStartMenuRowH;
        if (itemY + kStartMenuRowH > menuY + headerH + maxBodyH) break;

        // Clicked item highlight
        if (i == s_clickedMenuRight) {
            framebuffer::fill_rect(rightX, itemY, kStartMenuRightColW - 2, kStartMenuRowH, rgb(50, 90, 160));
        }
        // Hover highlight
        else if (i == s_hoverMenuRight) {
            framebuffer::fill_rect(rightX, itemY, kStartMenuRightColW - 2, kStartMenuRowH, rgb(50, 55, 72));
        }
        // Alternate shading
        else if (i % 2 == 1) {
            framebuffer::fill_rect(rightX, itemY, kStartMenuRightColW - 2, kStartMenuRowH, rgb(42, 42, 52));
        }

        // Small colored icon
        framebuffer::fill_rect(rightX + 8, itemY + 4, 14, 14, s_startMenuRight[i].color);
        draw_rect(rightX + 8, itemY + 4, 14, 14, rgb(140, 140, 160));

        // Label
        uint32_t rTextColor = (i == s_clickedMenuRight || i == s_hoverMenuRight)
            ? rgb(255, 255, 255) : rgb(200, 200, 220);
        draw_text(rightX + 28, itemY + 6, s_startMenuRight[i].label, rTextColor, 1);
    }

    // === Footer: "All Programs" toggle + Power buttons ===
    uint32_t footerY = menuY + headerH + maxBodyH;
    hline(menuX + 1, footerY, kStartMenuW - 2, rgb(60, 70, 90));

    // Footer background
    framebuffer::fill_rect(menuX + 1, footerY + 1, kStartMenuW - 2, footerH - 2, rgb(38, 38, 46));

    // "All Programs" toggle button (highlighted if active)
    uint32_t allBtnW = 110;
    uint32_t allBtnH = 24;
    uint32_t allBtnX = menuX + 10;
    uint32_t allBtnY = footerY + (footerH - allBtnH) / 2;
    uint32_t allBtnBg = s_startMenuAllProgs ? rgb(60, 80, 120) : rgb(50, 55, 65);
    uint32_t allBtnBorder = s_startMenuAllProgs ? rgb(90, 120, 180) : rgb(70, 80, 100);
    framebuffer::fill_rect(allBtnX, allBtnY, allBtnW, allBtnH, allBtnBg);
    draw_rect(allBtnX, allBtnY, allBtnW, allBtnH, allBtnBorder);
    const char* allBtnText = s_startMenuAllProgs ? "< Back" : "All Programs";
    draw_text_centered(allBtnX, allBtnY, allBtnW, allBtnH, allBtnText, rgb(190, 195, 210), 1);

    // Power buttons (right side of footer)
    uint32_t shutW = 80;
    uint32_t shutH = 24;
    uint32_t shutX = menuX + kStartMenuW - shutW - 12;
    uint32_t shutY = footerY + (footerH - shutH) / 2;
    framebuffer::fill_rect(shutX, shutY, shutW, shutH, rgb(140, 50, 50));
    draw_rect(shutX, shutY, shutW, shutH, rgb(180, 80, 80));
    draw_text_centered(shutX, shutY, shutW, shutH, "Shut Down", rgb(240, 220, 220), 1);

    // Restart button
    uint32_t restartW = 62;
    uint32_t restartX = shutX - restartW - 6;
    framebuffer::fill_rect(restartX, shutY, restartW, shutH, rgb(50, 60, 80));
    draw_rect(restartX, shutY, restartW, shutH, rgb(80, 100, 130));
    draw_text_centered(restartX, shutY, restartW, shutH, "Restart", rgb(200, 200, 220), 1);

    // Sleep button
    uint32_t sleepW = 50;
    uint32_t sleepX = restartX - sleepW - 6;
    framebuffer::fill_rect(sleepX, shutY, sleepW, shutH, rgb(50, 60, 80));
    draw_rect(sleepX, shutY, sleepW, shutH, rgb(80, 100, 130));
    draw_text_centered(sleepX, shutY, sleepW, shutH, "Sleep", rgb(200, 200, 220), 1);
}

// ============================================================
// Right-click context menu (matching Legacy RightMenu.cs)
// ============================================================

static void draw_right_click_menu()
{
    if (!s_rightClickMenuOpen) return;

    uint32_t menuH = (uint32_t)kContextMenuCount * kContextMenuItemH + 4;
    uint32_t mx = s_rightClickX;
    uint32_t my = s_rightClickY;

    // Clamp to screen
    if (mx + kContextMenuW > s_screenW) mx = s_screenW - kContextMenuW;
    if (my + menuH > s_screenH - kTaskbarH) my = s_screenH - kTaskbarH - menuH;

    // Background with shadow
    framebuffer::fill_rect(mx + 2, my + 2, kContextMenuW, menuH, rgb(20, 20, 25));
    framebuffer::fill_rect(mx, my, kContextMenuW, menuH, rgb(45, 45, 55));
    draw_rect(mx, my, kContextMenuW, menuH, rgb(80, 90, 110));

    for (int i = 0; i < kContextMenuCount; i++) {
        uint32_t itemY = my + 2 + (uint32_t)i * kContextMenuItemH;

        // Alternate shading
        if (i % 2 == 0)
            framebuffer::fill_rect(mx + 1, itemY, kContextMenuW - 2, kContextMenuItemH, rgb(48, 48, 58));

        draw_text(mx + 12, itemY + (kContextMenuItemH - kGlyphH) / 2,
                  s_contextMenuItems[i], rgb(210, 210, 225), 1);

        // Separator after "Personalize" (index 2)
        if (i == 2) {
            hline(mx + 4, itemY + kContextMenuItemH - 1, kContextMenuW - 8, rgb(60, 65, 80));
        }
    }
}

// ============================================================
// Notification toasts (top-right, matching Legacy NotificationManager)
// ============================================================

static void draw_notifications()
{
    if (!s_notification.visible) return;

    uint32_t toastW = 260;
    uint32_t toastH = 60;
    uint32_t toastX = s_screenW - toastW - 12;
    uint32_t toastY = 12;

    // Shadow
    framebuffer::fill_rect(toastX + 3, toastY + 3, toastW, toastH, rgb(15, 15, 20));

    // Toast background
    framebuffer::fill_rect(toastX, toastY, toastW, toastH, rgb(50, 55, 65));
    draw_rect(toastX, toastY, toastW, toastH, rgb(80, 100, 150));

    // Accent bar on left
    framebuffer::fill_rect(toastX + 1, toastY + 1, 4, toastH - 2, rgb(70, 130, 220));

    // Title
    draw_text(toastX + 12, toastY + 10, s_notification.title, rgb(230, 230, 245), 1);


    // Message
    draw_text(toastX + 12, toastY + 26, s_notification.message, rgb(170, 175, 190), 1);

    // Close button (X) in top-right corner
    uint32_t closeX = toastX + toastW - 16;
    uint32_t closeY = toastY + 4;
    draw_text(closeX, closeY, "x", rgb(160, 160, 175), 1);

    // Timestamp
    draw_text(toastX + 12, toastY + 42, "Just now", rgb(120, 125, 140), 1);
}

// ============================================================
// Shutdown Confirmation Dialog
// ============================================================

static const uint32_t kShutdownDlgW = 320;
static const uint32_t kShutdownDlgH = 140;
static const uint32_t kShutdownBtnW = 80;
static const uint32_t kShutdownBtnH = 28;

static void draw_shutdown_dialog()
{
    if (!s_shutdownDialogOpen) return;

    // Center the dialog on screen
    uint32_t dlgX = (s_screenW - kShutdownDlgW) / 2;
    uint32_t dlgY = (s_screenH - kShutdownDlgH) / 2;

    // Shadow
    framebuffer::fill_rect(dlgX + 4, dlgY + 4, kShutdownDlgW, kShutdownDlgH, rgb(10, 10, 15));

    // Dialog background
    framebuffer::fill_rect(dlgX, dlgY, kShutdownDlgW, kShutdownDlgH, rgb(45, 45, 55));

    // Border
    draw_rect(dlgX, dlgY, kShutdownDlgW, kShutdownDlgH, rgb(80, 100, 140));

    // Title bar
    framebuffer::fill_rect(dlgX + 1, dlgY + 1, kShutdownDlgW - 2, 28, rgb(50, 70, 110));
    draw_text(dlgX + 12, dlgY + 8, "Confirm Shutdown", rgb(220, 225, 240), 1);

    // Message
    draw_text(dlgX + 30, dlgY + 50, "Are you sure you want to", rgb(200, 205, 220), 1);
    draw_text(dlgX + 30, dlgY + 68, "shut down the system?", rgb(200, 205, 220), 1);

    // Buttons
    uint32_t btnY = dlgY + kShutdownDlgH - kShutdownBtnH - 16;
    uint32_t yesX = dlgX + kShutdownDlgW / 2 - kShutdownBtnW - 10;
    uint32_t noX = dlgX + kShutdownDlgW / 2 + 10;

    // Yes button
    uint32_t yesBg = (s_shutdownDialogHover == 0) ? rgb(70, 120, 180) : rgb(50, 90, 150);
    framebuffer::fill_rect(yesX, btnY, kShutdownBtnW, kShutdownBtnH, yesBg);
    draw_rect(yesX, btnY, kShutdownBtnW, kShutdownBtnH, rgb(100, 140, 200));
    draw_text_centered(yesX, btnY, kShutdownBtnW, kShutdownBtnH, "Yes", rgb(220, 230, 250), 1);

    // No button
    uint32_t noBg = (s_shutdownDialogHover == 1) ? rgb(90, 60, 60) : rgb(70, 50, 50);
    framebuffer::fill_rect(noX, btnY, kShutdownBtnW, kShutdownBtnH, noBg);
    draw_rect(noX, btnY, kShutdownBtnW, kShutdownBtnH, rgb(140, 90, 90));
    draw_text_centered(noX, btnY, kShutdownBtnW, kShutdownBtnH, "No", rgb(240, 210, 210), 1);
}

// Hit test shutdown dialog buttons: returns 0=Yes, 1=No, -1=none
static int hit_test_shutdown_dialog(int32_t mx, int32_t my)
{
    if (!s_shutdownDialogOpen) return -1;

    uint32_t dlgX = (s_screenW - kShutdownDlgW) / 2;
    uint32_t dlgY = (s_screenH - kShutdownDlgH) / 2;
    uint32_t btnY = dlgY + kShutdownDlgH - kShutdownBtnH - 16;
    uint32_t yesX = dlgX + kShutdownDlgW / 2 - kShutdownBtnW - 10;
    uint32_t noX = dlgX + kShutdownDlgW / 2 + 10;

    // Yes button
    if ((uint32_t)mx >= yesX && (uint32_t)mx < yesX + kShutdownBtnW &&
        (uint32_t)my >= btnY && (uint32_t)my < btnY + kShutdownBtnH) {
        return 0;
    }

    // No button
    if ((uint32_t)mx >= noX && (uint32_t)mx < noX + kShutdownBtnW &&
        (uint32_t)my >= btnY && (uint32_t)my < btnY + kShutdownBtnH) {
        return 1;
    }

    return -1;
}

// ============================================================
// Control Panel Window
// ============================================================

static const uint32_t kControlPanelW = 420;
static const uint32_t kControlPanelH = 320;
static const uint32_t kCPIconSize = 48;
static const uint32_t kCPIconSpacing = 100;

struct ControlPanelItem {
    const char* label;
    uint32_t color;
};

static const ControlPanelItem s_controlPanelItems[] = {
    {"Device Mgr",     0xFF4690C8},
    {"Network",        0xFF50B478},
};
static const int kControlPanelItemCount = 2;

static void draw_control_panel()
{
    if (!s_controlPanelOpen) return;

    uint32_t dlgX = (s_screenW - kControlPanelW) / 2;
    uint32_t dlgY = (s_screenH - kControlPanelH) / 2;

    // Shadow
    framebuffer::fill_rect(dlgX + 4, dlgY + 4, kControlPanelW, kControlPanelH, rgb(10, 10, 15));

    // Window background
    framebuffer::fill_rect(dlgX, dlgY, kControlPanelW, kControlPanelH, rgb(35, 35, 45));
    draw_rect(dlgX, dlgY, kControlPanelW, kControlPanelH, rgb(70, 90, 130));

    // Title bar
    framebuffer::fill_rect(dlgX + 1, dlgY + 1, kControlPanelW - 2, 28, rgb(50, 70, 110));
    draw_text(dlgX + 12, dlgY + 8, "Control Panel", rgb(220, 225, 240), 1);

    // Close button
    uint32_t closeBtnX = dlgX + kControlPanelW - 26;
    uint32_t closeBtnY = dlgY + 4;
    framebuffer::fill_rect(closeBtnX, closeBtnY, 20, 20, rgb(180, 60, 60));
    draw_rect(closeBtnX, closeBtnY, 20, 20, rgb(200, 80, 80));
    draw_text(closeBtnX + 6, closeBtnY + 5, "x", rgb(240, 220, 220), 1);

    // Content area
    uint32_t contentY = dlgY + 36;
    uint32_t contentH = kControlPanelH - 44;
    framebuffer::fill_rect(dlgX + 8, contentY, kControlPanelW - 16, contentH, rgb(30, 30, 38));

    // Draw header text
    draw_text(dlgX + 20, contentY + 12, "System Settings", rgb(200, 200, 220), 1);

    // Draw icons
    for (int i = 0; i < kControlPanelItemCount; i++) {
        uint32_t iconX = dlgX + 30 + (uint32_t)i * kCPIconSpacing;
        uint32_t iconY = contentY + 50;

        // Hover highlight
        if (s_controlPanelHover == i) {
            framebuffer::fill_rect(iconX - 8, iconY - 4, kCPIconSize + 16, kCPIconSize + 30, rgb(50, 70, 100));
            draw_rect(iconX - 8, iconY - 4, kCPIconSize + 16, kCPIconSize + 30, rgb(80, 110, 160));
        }

        // Icon
        framebuffer::fill_rect(iconX, iconY, kCPIconSize, kCPIconSize, s_controlPanelItems[i].color);
        draw_rect(iconX, iconY, kCPIconSize, kCPIconSize, rgb(180, 180, 200));

        // Inner highlight
        uint32_t innerSize = 20;
        uint32_t innerX = iconX + (kCPIconSize - innerSize) / 2;
        uint32_t innerY = iconY + (kCPIconSize - innerSize) / 2;
        uint32_t c = s_controlPanelItems[i].color;
        uint8_t hr = (uint8_t)(((c >> 16) & 0xFF) + 40 > 255 ? 255 : ((c >> 16) & 0xFF) + 40);
        uint8_t hg = (uint8_t)(((c >> 8) & 0xFF) + 40 > 255 ? 255 : ((c >> 8) & 0xFF) + 40);
        uint8_t hb = (uint8_t)((c & 0xFF) + 40 > 255 ? 255 : (c & 0xFF) + 40);
        framebuffer::fill_rect(innerX, innerY, innerSize, innerSize, rgb(hr, hg, hb));

        // Label
        const char* lbl = s_controlPanelItems[i].label;
        int tw = measure_text(lbl);
        uint32_t lx = iconX + (kCPIconSize > (uint32_t)tw ? (kCPIconSize - tw) / 2 : 0);
        uint32_t ly = iconY + kCPIconSize + 8;
        draw_text(lx, ly, lbl, rgb(210, 210, 225), 1);
    }
}

// Hit test Control Panel: returns 0=Device Manager, 1=Network, 2=close, -1=none
static int hit_test_control_panel(int32_t mx, int32_t my)
{
    if (!s_controlPanelOpen) return -1;

    uint32_t dlgX = (s_screenW - kControlPanelW) / 2;
    uint32_t dlgY = (s_screenH - kControlPanelH) / 2;
    uint32_t contentY = dlgY + 36;

    // Close button
    uint32_t closeBtnX = dlgX + kControlPanelW - 26;
    uint32_t closeBtnY = dlgY + 4;
    if ((uint32_t)mx >= closeBtnX && (uint32_t)mx < closeBtnX + 20 &&
        (uint32_t)my >= closeBtnY && (uint32_t)my < closeBtnY + 20) {
        return 2;
    }

    // Icon hit test
    for (int i = 0; i < kControlPanelItemCount; i++) {
        uint32_t iconX = dlgX + 30 + (uint32_t)i * kCPIconSpacing;
        uint32_t iconY = contentY + 50;

        if ((uint32_t)mx >= iconX - 8 && (uint32_t)mx < iconX + kCPIconSize + 8 &&
            (uint32_t)my >= iconY - 4 && (uint32_t)my < iconY + kCPIconSize + 26) {
            return i;
        }
    }

    return -1;
}

// ============================================================
// Device Manager Window
// ============================================================

static const uint32_t kDeviceMgrW = 560;
static const uint32_t kDeviceMgrH = 400;
static const uint32_t kDeviceMgrRowH = 24;

struct DeviceEntry {
    const char* name;
    const char* status;
    const char* detail;
    uint32_t statusColor;
    bool isNetwork;
};

static const DeviceEntry s_devices[] = {
    {"Audio: Intel AC'97",          "Detected",     "Vendor 8086 Dev 2415",    0xFF5FB878, false},
    {"Audio: ES1371",               "Not found",    "Vendor 1274 Dev 1371",    0xFFD96C6C, false},
    {"Network: Intel 825xx",        "Detected",     "Vendor 8086 Class 02",    0xFF5FB878, true},
    {"Network: Realtek 8111/8168",  "Not found",    "Vendor 10EC Dev 8168",    0xFFD96C6C, true},
    {"USB Mass Storage",            "0 device(s)",  "",                        0xFFCCCCCC, false},
    {"PCI Host Bridge",             "Bus:0 Slot:0", "Class: Host Bridge",      0xFFDDDDDD, false},
    {"ISA Bridge",                  "Bus:0 Slot:1", "Class: ISA Bridge",       0xFFDDDDDD, false},
    {"VGA Controller",              "Bus:0 Slot:2", "Class: Display",          0xFFDDDDDD, false},
};
static const int kDeviceCount = 8;

static void draw_device_manager()
{
    if (!s_deviceManagerOpen) return;

    uint32_t dlgX = (s_screenW - kDeviceMgrW) / 2;
    uint32_t dlgY = (s_screenH - kDeviceMgrH) / 2;

    // Shadow
    framebuffer::fill_rect(dlgX + 4, dlgY + 4, kDeviceMgrW, kDeviceMgrH, rgb(10, 10, 15));

    // Window background
    framebuffer::fill_rect(dlgX, dlgY, kDeviceMgrW, kDeviceMgrH, rgb(35, 35, 45));
    draw_rect(dlgX, dlgY, kDeviceMgrW, kDeviceMgrH, rgb(70, 90, 130));

    // Title bar
    framebuffer::fill_rect(dlgX + 1, dlgY + 1, kDeviceMgrW - 2, 28, rgb(50, 70, 110));
    draw_text(dlgX + 12, dlgY + 8, "Device Manager", rgb(220, 225, 240), 1);

    // Close button
    uint32_t closeBtnX = dlgX + kDeviceMgrW - 26;
    uint32_t closeBtnY = dlgY + 4;
    framebuffer::fill_rect(closeBtnX, closeBtnY, 20, 20, rgb(180, 60, 60));
    draw_rect(closeBtnX, closeBtnY, 20, 20, rgb(200, 80, 80));
    draw_text(closeBtnX + 6, closeBtnY + 5, "x", rgb(240, 220, 220), 1);

    // Content area
    uint32_t contentY = dlgY + 36;
    framebuffer::fill_rect(dlgX + 8, contentY, kDeviceMgrW - 16, kDeviceMgrH - 44, rgb(28, 28, 35));

    // Header
    draw_text(dlgX + 20, contentY + 8, "Hardware Inventory", rgb(200, 200, 220), 1);

    // Column headers
    uint32_t listY = contentY + 30;
    uint32_t nameW = (uint32_t)((kDeviceMgrW - 32) * 0.38f);
    uint32_t statusW = (uint32_t)((kDeviceMgrW - 32) * 0.22f);

    framebuffer::fill_rect(dlgX + 12, listY, kDeviceMgrW - 24, kDeviceMgrRowH, rgb(40, 45, 55));
    draw_text(dlgX + 18, listY + 6, "Device", rgb(180, 180, 200), 1);
    draw_text(dlgX + 18 + nameW, listY + 6, "Status", rgb(180, 180, 200), 1);
    draw_text(dlgX + 18 + nameW + statusW, listY + 6, "Details", rgb(180, 180, 200), 1);

    // Device list
    listY += kDeviceMgrRowH + 2;
    for (int i = 0; i < kDeviceCount; i++) {
        uint32_t rowY = listY + (uint32_t)i * kDeviceMgrRowH;
        if (rowY + kDeviceMgrRowH > dlgY + kDeviceMgrH - 12) break;

        uint32_t rowBg = (i % 2 == 0) ? rgb(32, 32, 40) : rgb(28, 28, 36);
        if (s_deviceManagerSelected == i) rowBg = rgb(50, 70, 100);
        framebuffer::fill_rect(dlgX + 12, rowY, kDeviceMgrW - 24, kDeviceMgrRowH - 1, rowBg);

        draw_text(dlgX + 18, rowY + 5, s_devices[i].name, rgb(210, 210, 225), 1);

        uint32_t statusX = dlgX + 18 + nameW;
        framebuffer::fill_rect(statusX, rowY + 8, 8, 8, s_devices[i].statusColor);
        draw_text(statusX + 12, rowY + 5, s_devices[i].status, rgb(200, 200, 215), 1);

        draw_text(dlgX + 18 + nameW + statusW, rowY + 5, s_devices[i].detail, rgb(160, 160, 180), 1);

        if (s_devices[i].isNetwork && s_devices[i].statusColor == 0xFF5FB878) {
            uint32_t btnX = dlgX + kDeviceMgrW - 90;
            uint32_t btnY2 = rowY + 2;
            framebuffer::fill_rect(btnX, btnY2, 70, kDeviceMgrRowH - 4, rgb(50, 80, 120));
            draw_rect(btnX, btnY2, 70, kDeviceMgrRowH - 4, rgb(70, 100, 150));
            draw_text_centered(btnX, btnY2, 70, kDeviceMgrRowH - 4, "Config", rgb(200, 210, 230), 1);
        }
    }
}

static int hit_test_device_manager(int32_t mx, int32_t my)
{
    if (!s_deviceManagerOpen) return -1;

    uint32_t dlgX = (s_screenW - kDeviceMgrW) / 2;
    uint32_t dlgY = (s_screenH - kDeviceMgrH) / 2;

    uint32_t closeBtnX = dlgX + kDeviceMgrW - 26;
    uint32_t closeBtnY = dlgY + 4;
    if ((uint32_t)mx >= closeBtnX && (uint32_t)mx < closeBtnX + 20 &&
        (uint32_t)my >= closeBtnY && (uint32_t)my < closeBtnY + 20) {
        return -2;
    }

    uint32_t contentY = dlgY + 36;
    uint32_t listY = contentY + 30 + kDeviceMgrRowH + 2;

    for (int i = 0; i < kDeviceCount; i++) {
        uint32_t rowY = listY + (uint32_t)i * kDeviceMgrRowH;
        if (rowY + kDeviceMgrRowH > dlgY + kDeviceMgrH - 12) break;

        if (s_devices[i].isNetwork && s_devices[i].statusColor == 0xFF5FB878) {
            uint32_t btnX = dlgX + kDeviceMgrW - 90;
            uint32_t btnY2 = rowY + 2;
            if ((uint32_t)mx >= btnX && (uint32_t)mx < btnX + 70 &&
                (uint32_t)my >= btnY2 && (uint32_t)my < btnY2 + kDeviceMgrRowH - 4) {
                return 100 + i;
            }
        }

        if ((uint32_t)mx >= dlgX + 12 && (uint32_t)mx < dlgX + kDeviceMgrW - 12 &&
            (uint32_t)my >= rowY && (uint32_t)my < rowY + kDeviceMgrRowH) {
            return i;
        }
    }

    return -1;
}

// ============================================================
// Network Adapters Window
// ============================================================

static const uint32_t kNetAdaptersW = 520;
static const uint32_t kNetAdaptersH = 360;
static const uint32_t kNetAdapterRowH = 56;

struct NetworkAdapter {
    const char* name;
    const char* vendor;
    const char* status;
    bool detected;
};

static const NetworkAdapter s_networkAdapters[] = {
    {"Intel Ethernet Adapter",      "Intel Corporation",     "Connected", true},
    {"Realtek PCIe GbE Controller", "Realtek Semiconductor", "Not found", false},
};
static const int kNetAdapterCount = 2;

static void draw_network_adapters()
{
    if (!s_networkAdaptersOpen) return;

    uint32_t dlgX = (s_screenW - kNetAdaptersW) / 2;
    uint32_t dlgY = (s_screenH - kNetAdaptersH) / 2;

    framebuffer::fill_rect(dlgX + 4, dlgY + 4, kNetAdaptersW, kNetAdaptersH, rgb(10, 10, 15));
    framebuffer::fill_rect(dlgX, dlgY, kNetAdaptersW, kNetAdaptersH, rgb(35, 35, 45));
    draw_rect(dlgX, dlgY, kNetAdaptersW, kNetAdaptersH, rgb(70, 90, 130));

    framebuffer::fill_rect(dlgX + 1, dlgY + 1, kNetAdaptersW - 2, 28, rgb(50, 70, 110));
    draw_text(dlgX + 12, dlgY + 8, "Network Adapters", rgb(220, 225, 240), 1);

    uint32_t closeBtnX = dlgX + kNetAdaptersW - 26;
    framebuffer::fill_rect(closeBtnX, dlgY + 4, 20, 20, rgb(180, 60, 60));
    draw_rect(closeBtnX, dlgY + 4, 20, 20, rgb(200, 80, 80));
    draw_text(closeBtnX + 6, dlgY + 9, "x", rgb(240, 220, 220), 1);

    uint32_t contentY = dlgY + 36;
    framebuffer::fill_rect(dlgX + 8, contentY, kNetAdaptersW - 16, kNetAdaptersH - 90, rgb(28, 28, 35));
    draw_text(dlgX + 20, contentY + 8, "Network Connections", rgb(200, 200, 220), 1);

    uint32_t listY = contentY + 30;
    for (int i = 0; i < kNetAdapterCount; i++) {
        uint32_t rowY = listY + (uint32_t)i * (kNetAdapterRowH + 4);
        uint32_t rowBg = (s_networkAdapterSelected == i) ? rgb(45, 65, 95) : 
                         (s_networkAdapterHover == i) ? rgb(38, 48, 58) : rgb(32, 32, 40);
        framebuffer::fill_rect(dlgX + 12, rowY, kNetAdaptersW - 24, kNetAdapterRowH, rowBg);

        uint32_t statusColor = s_networkAdapters[i].detected ? rgb(95, 184, 120) : rgb(100, 100, 110);
        framebuffer::fill_rect(dlgX + 12, rowY, 4, kNetAdapterRowH, statusColor);

        uint32_t iconColor = s_networkAdapters[i].detected ? rgb(80, 180, 120) : rgb(80, 80, 90);
        framebuffer::fill_rect(dlgX + 24, rowY + 12, 32, 32, iconColor);
        draw_rect(dlgX + 24, rowY + 12, 32, 32, rgb(120, 140, 160));

        draw_text(dlgX + 64, rowY + 10, s_networkAdapters[i].name, rgb(220, 220, 235), 1);
        draw_text(dlgX + 64, rowY + 28, s_networkAdapters[i].vendor, rgb(160, 165, 180), 1);
        draw_text(dlgX + kNetAdaptersW - 120, rowY + 20, s_networkAdapters[i].status, rgb(180, 185, 200), 1);
    }

    uint32_t btnY = dlgY + kNetAdaptersH - 46;
    uint32_t propBtnX = dlgX + kNetAdaptersW - 200;
    bool propEnabled = (s_networkAdapterSelected >= 0 && s_networkAdapters[s_networkAdapterSelected].detected);
    uint32_t propBg = propEnabled ? rgb(55, 85, 125) : rgb(45, 45, 55);
    framebuffer::fill_rect(propBtnX, btnY, 90, 30, propBg);
    draw_rect(propBtnX, btnY, 90, 30, propEnabled ? rgb(80, 120, 170) : rgb(60, 60, 70));
    draw_text_centered(propBtnX, btnY, 90, 30, "Properties", propEnabled ? rgb(210, 220, 240) : rgb(120, 120, 130), 1);

    uint32_t statusBtnX = dlgX + kNetAdaptersW - 100;
    framebuffer::fill_rect(statusBtnX, btnY, 90, 30, rgb(45, 50, 60));
    draw_rect(statusBtnX, btnY, 90, 30, rgb(65, 75, 90));
    draw_text_centered(statusBtnX, btnY, 90, 30, "Status", rgb(160, 165, 180), 1);
}

static int hit_test_network_adapters(int32_t mx, int32_t my)
{
    if (!s_networkAdaptersOpen) return -1;

    uint32_t dlgX = (s_screenW - kNetAdaptersW) / 2;
    uint32_t dlgY = (s_screenH - kNetAdaptersH) / 2;

    uint32_t closeBtnX = dlgX + kNetAdaptersW - 26;
    if ((uint32_t)mx >= closeBtnX && (uint32_t)mx < closeBtnX + 20 &&
        (uint32_t)my >= dlgY + 4 && (uint32_t)my < dlgY + 24) {
        return -2;
    }

    uint32_t btnY = dlgY + kNetAdaptersH - 46;
    uint32_t propBtnX = dlgX + kNetAdaptersW - 200;
    if ((uint32_t)mx >= propBtnX && (uint32_t)mx < propBtnX + 90 &&
        (uint32_t)my >= btnY && (uint32_t)my < btnY + 30) {
        return -10;
    }

    uint32_t contentY = dlgY + 36;
    uint32_t listY = contentY + 30;
    for (int i = 0; i < kNetAdapterCount; i++) {
        uint32_t rowY = listY + (uint32_t)i * (kNetAdapterRowH + 4);
        if ((uint32_t)mx >= dlgX + 12 && (uint32_t)mx < dlgX + kNetAdaptersW - 12 &&
            (uint32_t)my >= rowY && (uint32_t)my < rowY + kNetAdapterRowH) {
            return i;
        }
    }

    return -1;
}

// ============================================================
// Network Configuration Dialog
// ============================================================

static const uint32_t kNetConfigW = 480;
static const uint32_t kNetConfigH = 420;

static void draw_network_config()
{
    if (!s_networkConfigOpen) return;

    uint32_t dlgX = (s_screenW - kNetConfigW) / 2;
    uint32_t dlgY = (s_screenH - kNetConfigH) / 2;

    framebuffer::fill_rect(dlgX + 4, dlgY + 4, kNetConfigW, kNetConfigH, rgb(10, 10, 15));
    framebuffer::fill_rect(dlgX, dlgY, kNetConfigW, kNetConfigH, rgb(35, 35, 45));
    draw_rect(dlgX, dlgY, kNetConfigW, kNetConfigH, rgb(70, 90, 130));

    framebuffer::fill_rect(dlgX + 1, dlgY + 1, kNetConfigW - 2, 28, rgb(50, 70, 110));
    draw_text(dlgX + 12, dlgY + 8, "TCP/IPv4 Properties", rgb(220, 225, 240), 1);

    uint32_t closeBtnX = dlgX + kNetConfigW - 26;
    framebuffer::fill_rect(closeBtnX, dlgY + 4, 20, 20, rgb(180, 60, 60));
    draw_text(closeBtnX + 6, dlgY + 9, "x", rgb(240, 220, 220), 1);

    uint32_t contentY = dlgY + 36;
    framebuffer::fill_rect(dlgX + 8, contentY, kNetConfigW - 16, kNetConfigH - 90, rgb(30, 30, 38));

    uint32_t textY = contentY + 12;
    draw_text(dlgX + 20, textY, "Network settings can be configured here.", rgb(180, 185, 200), 1);
    textY += 32;

    // DHCP option
    framebuffer::fill_rect(dlgX + 20, textY + 2, 12, 12, s_networkConfigUseDHCP ? rgb(74, 158, 255) : rgb(60, 60, 70));
    draw_rect(dlgX + 20, textY + 2, 12, 12, rgb(100, 120, 150));
    draw_text(dlgX + 40, textY, "Obtain IP address automatically (DHCP)", rgb(200, 200, 220), 1);
    textY += 28;

    // Manual option
    framebuffer::fill_rect(dlgX + 20, textY + 2, 12, 12, !s_networkConfigUseDHCP ? rgb(74, 158, 255) : rgb(60, 60, 70));
    draw_rect(dlgX + 20, textY + 2, 12, 12, rgb(100, 120, 150));
    draw_text(dlgX + 40, textY, "Use the following IP address:", rgb(200, 200, 220), 1);
    textY += 36;

    uint32_t labelColor = !s_networkConfigUseDHCP ? rgb(200, 200, 220) : rgb(120, 120, 135);
    uint32_t fieldBg = !s_networkConfigUseDHCP ? rgb(42, 42, 52) : rgb(32, 32, 40);

    // IP Address
    draw_text(dlgX + 48, textY, "IP address:", labelColor, 1);
    framebuffer::fill_rect(dlgX + 180, textY - 4, 200, 20, fieldBg);
    draw_rect(dlgX + 180, textY - 4, 200, 20, rgb(70, 80, 95));
    char ipStr[20]; 
    // Format IP as string
    int idx = 0;
    for (int o = 0; o < 4; o++) {
        uint8_t v = s_netConfigIP[o];
        if (v >= 100) ipStr[idx++] = '0' + v / 100;
        if (v >= 10) ipStr[idx++] = '0' + (v / 10) % 10;
        ipStr[idx++] = '0' + v % 10;
        if (o < 3) ipStr[idx++] = '.';
    }
    ipStr[idx] = '\0';
    draw_text(dlgX + 188, textY, ipStr, labelColor, 1);
    textY += 28;

    // Subnet mask
    draw_text(dlgX + 48, textY, "Subnet mask:", labelColor, 1);
    framebuffer::fill_rect(dlgX + 180, textY - 4, 200, 20, fieldBg);
    draw_rect(dlgX + 180, textY - 4, 200, 20, rgb(70, 80, 95));
    idx = 0;
    for (int o = 0; o < 4; o++) {
        uint8_t v = s_netConfigMask[o];
        if (v >= 100) ipStr[idx++] = '0' + v / 100;
        if (v >= 10) ipStr[idx++] = '0' + (v / 10) % 10;
        ipStr[idx++] = '0' + v % 10;
        if (o < 3) ipStr[idx++] = '.';
    }
    ipStr[idx] = '\0';
    draw_text(dlgX + 188, textY, ipStr, labelColor, 1);
    textY += 28;

    // Gateway
    draw_text(dlgX + 48, textY, "Default gateway:", labelColor, 1);
    framebuffer::fill_rect(dlgX + 180, textY - 4, 200, 20, fieldBg);
    draw_rect(dlgX + 180, textY - 4, 200, 20, rgb(70, 80, 95));
    idx = 0;
    for (int o = 0; o < 4; o++) {
        uint8_t v = s_netConfigGateway[o];
        if (v >= 100) ipStr[idx++] = '0' + v / 100;
        if (v >= 10) ipStr[idx++] = '0' + (v / 10) % 10;
        ipStr[idx++] = '0' + v % 10;
        if (o < 3) ipStr[idx++] = '.';
    }
    ipStr[idx] = '\0';
    draw_text(dlgX + 188, textY, ipStr, labelColor, 1);
    textY += 36;

    // DNS
    draw_text(dlgX + 48, textY, "Preferred DNS:", labelColor, 1);
    framebuffer::fill_rect(dlgX + 180, textY - 4, 200, 20, fieldBg);
    draw_rect(dlgX + 180, textY - 4, 200, 20, rgb(70, 80, 95));
    idx = 0;
    for (int o = 0; o < 4; o++) {
        uint8_t v = s_netConfigDNS[o];
        if (v >= 100) ipStr[idx++] = '0' + v / 100;
        if (v >= 10) ipStr[idx++] = '0' + (v / 10) % 10;
        ipStr[idx++] = '0' + v % 10;
        if (o < 3) ipStr[idx++] = '.';
    }
    ipStr[idx] = '\0';
    draw_text(dlgX + 188, textY, ipStr, labelColor, 1);

    // Buttons
    uint32_t btnY = dlgY + kNetConfigH - 50;
    uint32_t okBtnX = dlgX + kNetConfigW - 200;
    uint32_t cancelBtnX = dlgX + kNetConfigW - 100;

    uint32_t okBg = (s_networkConfigBtnHover == 0) ? rgb(70, 100, 145) : rgb(55, 85, 130);
    framebuffer::fill_rect(okBtnX, btnY, 90, 32, okBg);
    draw_rect(okBtnX, btnY, 90, 32, rgb(85, 120, 175));
    draw_text_centered(okBtnX, btnY, 90, 32, "OK", rgb(220, 230, 250), 1);

    uint32_t cancelBg = (s_networkConfigBtnHover == 1) ? rgb(60, 55, 55) : rgb(50, 45, 45);
    framebuffer::fill_rect(cancelBtnX, btnY, 90, 32, cancelBg);
    draw_rect(cancelBtnX, btnY, 90, 32, rgb(90, 70, 70));
    draw_text_centered(cancelBtnX, btnY, 90, 32, "Cancel", rgb(220, 200, 200), 1);
}

static int hit_test_network_config(int32_t mx, int32_t my)
{
    if (!s_networkConfigOpen) return -1;

    uint32_t dlgX = (s_screenW - kNetConfigW) / 2;
    uint32_t dlgY = (s_screenH - kNetConfigH) / 2;

    uint32_t closeBtnX = dlgX + kNetConfigW - 26;
    if ((uint32_t)mx >= closeBtnX && (uint32_t)mx < closeBtnX + 20 &&
        (uint32_t)my >= dlgY + 4 && (uint32_t)my < dlgY + 24) {
        return -2;
    }

    uint32_t btnY = dlgY + kNetConfigH - 50;
    uint32_t okBtnX = dlgX + kNetConfigW - 200;
    uint32_t cancelBtnX = dlgX + kNetConfigW - 100;

    if ((uint32_t)mx >= okBtnX && (uint32_t)mx < okBtnX + 90 &&
        (uint32_t)my >= btnY && (uint32_t)my < btnY + 32) {
        return -10;
    }
    if ((uint32_t)mx >= cancelBtnX && (uint32_t)mx < cancelBtnX + 90 &&
        (uint32_t)my >= btnY && (uint32_t)my < btnY + 32) {
        return -11;
    }

    // DHCP radio
    uint32_t contentY = dlgY + 36;
    uint32_t textY = contentY + 12 + 32;
    if ((uint32_t)mx >= dlgX + 20 && (uint32_t)mx < dlgX + 32 &&
        (uint32_t)my >= textY && (uint32_t)my < textY + 16) {
        return -20;
    }
    textY += 28;
    if ((uint32_t)mx >= dlgX + 20 && (uint32_t)mx < dlgX + 32 &&
        (uint32_t)my >= textY && (uint32_t)my < textY + 16) {
        return -21;
    }

    return -1;
}

// ============================================================
// Shell Window Geometry and Hit-Testing
// ============================================================

struct ShellWindowGeometry {
    uint32_t x, y, w, h;
    uint32_t titlebarY, titlebarH;
    uint32_t closeBtnX, closeBtnY, closeBtnW, closeBtnH;
    uint32_t maxBtnX, maxBtnY, maxBtnW, maxBtnH;
    uint32_t minBtnX, minBtnY, minBtnW, minBtnH;
};

static ShellWindowGeometry get_shell_geometry()
{
    ShellWindowGeometry g;
    
    if (shell::get_state() == shell::ShellState::Fullscreen || s_shellMaximized) {
        g.x = 0;
        g.y = 0;
        g.w = s_screenW;
        g.h = s_screenH - kTaskbarH;
    } else {
        // Use custom size if set, otherwise default
        g.w = (s_shellW > 0) ? (uint32_t)s_shellW : kShellDefaultW;
        g.h = (s_shellH > 0) ? (uint32_t)s_shellH : kShellDefaultH;
        
        // Use stored position or center if not set
        if (s_shellPosX < 0) {
            g.x = (s_screenW - g.w) / 2;
        } else {
            g.x = (uint32_t)s_shellPosX;
        }
        if (s_shellPosY < 0) {
            g.y = (s_screenH - kTaskbarH - g.h) / 2;
        } else {
            g.y = (uint32_t)s_shellPosY;
        }
    }
    
    g.titlebarY = g.y;
    g.titlebarH = kShellTitlebarH;
    
    // Window control buttons (right side of titlebar): [_] [?] [X]
    // Matching guideXOS.Legacy button sizing
    const uint32_t btnSize = 16;
    const uint32_t btnGap = 6;
    const uint32_t btnY = g.y + 4;
    
    // Close button (rightmost)
    g.closeBtnW = btnSize;
    g.closeBtnH = btnSize;
    g.closeBtnX = g.x + g.w - btnGap - btnSize;
    g.closeBtnY = btnY;
    
    // Maximize button (left of close)
    g.maxBtnW = btnSize;
    g.maxBtnH = btnSize;
    g.maxBtnX = g.closeBtnX - btnGap - btnSize;
    g.maxBtnY = btnY;
    
    // Minimize button (left of maximize)
    g.minBtnW = btnSize;
    g.minBtnH = btnSize;
    g.minBtnX = g.maxBtnX - btnGap - btnSize;
    g.minBtnY = btnY;
    
    return g;
}

// Hit test result for shell window
enum ShellHitTest {
    SHELL_HIT_NONE = 0,
    SHELL_HIT_TITLEBAR,
    SHELL_HIT_CLOSE_BTN,
    SHELL_HIT_MAX_BTN,
    SHELL_HIT_MIN_BTN,
    SHELL_HIT_RESIZE_CORNER,
    SHELL_HIT_CLIENT
};

static ShellHitTest hit_test_shell(int32_t mx, int32_t my)
{
    if (!shell::is_open() || s_shellMinimized) return SHELL_HIT_NONE;
    
    ShellWindowGeometry g = get_shell_geometry();
    
    // Check if outside window bounds
    if ((uint32_t)mx < g.x || (uint32_t)mx >= g.x + g.w ||
        (uint32_t)my < g.y || (uint32_t)my >= g.y + g.h) {
        return SHELL_HIT_NONE;
    }
    
    // Check close button first
    if ((uint32_t)mx >= g.closeBtnX && (uint32_t)mx < g.closeBtnX + g.closeBtnW &&
        (uint32_t)my >= g.closeBtnY && (uint32_t)my < g.closeBtnY + g.closeBtnH) {
        return SHELL_HIT_CLOSE_BTN;
    }
    
    // Check maximize button
    if ((uint32_t)mx >= g.maxBtnX && (uint32_t)mx < g.maxBtnX + g.maxBtnW &&
        (uint32_t)my >= g.maxBtnY && (uint32_t)my < g.maxBtnY + g.maxBtnH) {
        return SHELL_HIT_MAX_BTN;
    }
    
    // Check minimize button
    if ((uint32_t)mx >= g.minBtnX && (uint32_t)mx < g.minBtnX + g.minBtnW &&
        (uint32_t)my >= g.minBtnY && (uint32_t)my < g.minBtnY + g.minBtnH) {
        return SHELL_HIT_MIN_BTN;
    }
    
    // Check resize corner (bottom-right, only when not maximized)
    if (!s_shellMaximized && shell::get_state() != shell::ShellState::Fullscreen) {
        uint32_t resizeX = g.x + g.w - kShellResizeMargin;
        uint32_t resizeY = g.y + g.h - kShellResizeMargin;
        if ((uint32_t)mx >= resizeX && (uint32_t)my >= resizeY) {
            return SHELL_HIT_RESIZE_CORNER;
        }
    }
    
    // Check titlebar area
    if ((uint32_t)my >= g.titlebarY && (uint32_t)my < g.titlebarY + g.titlebarH) {
        return SHELL_HIT_TITLEBAR;
    }
    
    // Otherwise it's the client area
    return SHELL_HIT_CLIENT;
}

// ============================================================
// Public API
// ============================================================

// Forward declarations for icon persistence
static bool load_icon_positions();
static void save_icon_positions();

static void init_icon_positions()
{
    uint32_t cols = (s_screenW - kIconMargin * 2) / kIconCellW;
    if (cols < 1) cols = 1;

    // Try to load saved positions first
    if (load_icon_positions()) {
        s_iconPositionsInitialized = true;
        return;
    }

    // If no saved positions, use grid layout
    for (int i = 0; i < kDesktopIconCount; i++) {
        uint32_t col = (uint32_t)i % cols;
        uint32_t row = (uint32_t)i / cols;
        s_iconPosX[i] = (int32_t)(kIconMargin + col * kIconCellW);
        s_iconPosY[i] = (int32_t)(kIconMargin + row * kIconCellH);
    }
    s_iconPositionsInitialized = true;
}

// Save icon positions to VFS file
static void save_icon_positions()
{
    // Create simple text format: x,y pairs on separate lines
    char buffer[512];
    int pos = 0;
    
    // Write header
    const char* header = "# guideXOS Desktop Icon Positions\n";
    for (int i = 0; header[i] && pos < 500; i++) {
        buffer[pos++] = header[i];
    }
    
    // Write each icon position
    for (int i = 0; i < kDesktopIconCount; i++) {
        // Convert X position to string
        int32_t x = s_iconPosX[i];
        int32_t y = s_iconPosY[i];
        
        // Simple number to string conversion
        char xStr[16], yStr[16];
        int xi = 0, yi = 0;
        
        // Convert X
        if (x < 0) { xStr[xi++] = '-'; x = -x; }
        if (x == 0) { xStr[xi++] = '0'; }
        else {
            char tmp[16];
            int ti = 0;
            while (x > 0) { tmp[ti++] = '0' + (x % 10); x /= 10; }
            while (ti > 0) { xStr[xi++] = tmp[--ti]; }
        }
        xStr[xi] = '\0';
        
        // Convert Y
        if (y < 0) { yStr[yi++] = '-'; y = -y; }
        if (y == 0) { yStr[yi++] = '0'; }
        else {
            char tmp[16];
            int ti = 0;
            while (y > 0) { tmp[ti++] = '0' + (y % 10); y /= 10; }
            while (ti > 0) { yStr[yi++] = tmp[--ti]; }
        }
        yStr[yi] = '\0';
        
        // Write "x,y\n"
        for (int j = 0; xStr[j] && pos < 500; j++) buffer[pos++] = xStr[j];
        if (pos < 500) buffer[pos++] = ',';
        for (int j = 0; yStr[j] && pos < 500; j++) buffer[pos++] = yStr[j];
        if (pos < 500) buffer[pos++] = '\n';
    }
    
    // Write to VFS
    uint8_t handle = vfs::open("/.desktop_icons", vfs::OPEN_WRITE);
    if (handle != 0xFF) {
        vfs::write(handle, buffer, pos);
        vfs::close(handle);
    }
}

// Load icon positions from VFS file
static bool load_icon_positions()
{
    uint8_t handle = vfs::open("/.desktop_icons", vfs::OPEN_READ);
    if (handle == 0xFF) return false;
    
    char buffer[512];
    int32_t bytesRead = vfs::read(handle, buffer, 511);
    vfs::close(handle);
    
    if (bytesRead <= 0) return false;
    buffer[bytesRead] = '\0';
    
    // Parse the file
    int iconIdx = 0;
    int pos = 0;
    
    // Skip header line
    while (pos < bytesRead && buffer[pos] != '\n') pos++;
    if (pos < bytesRead) pos++; // Skip newline
    
    // Parse each line as "x,y"
    while (pos < bytesRead && iconIdx < kDesktopIconCount) {
        // Skip empty lines
        if (buffer[pos] == '\n') { pos++; continue; }
        
        // Parse X coordinate
        bool negX = false;
        if (buffer[pos] == '-') { negX = true; pos++; }
        int32_t x = 0;
        while (pos < bytesRead && buffer[pos] >= '0' && buffer[pos] <= '9') {
            x = x * 10 + (buffer[pos] - '0');
            pos++;
        }
        if (negX) x = -x;
        
        // Skip comma
        if (pos < bytesRead && buffer[pos] == ',') pos++;
        
        // Parse Y coordinate
        bool negY = false;
        if (buffer[pos] == '-') { negY = true; pos++; }
        int32_t y = 0;
        while (pos < bytesRead && buffer[pos] >= '0' && buffer[pos] <= '9') {
            y = y * 10 + (buffer[pos] - '0');
            pos++;
        }
        if (negY) y = -y;
        
        // Skip to next line
        while (pos < bytesRead && buffer[pos] != '\n') pos++;
        if (pos < bytesRead) pos++;
        
        // Store position
        s_iconPosX[iconIdx] = x;
        s_iconPosY[iconIdx] = y;
        iconIdx++;
    }
    
    return iconIdx == kDesktopIconCount;
}

// Helper: try to launch an app using the kernel app framework
static bool try_launch_kernel_app(const char* appName)
{
    if (!appName) return false;
    
    // Check if app is available in kernel mode
    if (app::AppManager::isAppAvailable(appName)) {
        return app::AppManager::launchApp(appName);
    }
    
    // Debug: Check registered app count to see if registration worked
    int regCount = 0;
    for (int i = 0; i < app::MAX_APPS; i++) {
        const app::AppInfo* info = app::AppManager::getAppInfo(appName);
        if (info) {
            regCount++;
            break;
        }
    }
    
    return false;
}

// ============================================================
// Test Mode - Validates compositor and app launching
// ============================================================

static bool s_testModeRan = false;

// Run test mode to validate app launching (call from init or shell command)
void run_test_mode()
{
    if (s_testModeRan) return;  // Only run once
    s_testModeRan = true;
    
    // Log test start
    shell::execute("echo === guideXOS Kernel GUI Test Mode ===");
    shell::execute("echo Testing compositor and app framework...");
    
    // Test 1: Check if bare-metal mode
    bool bareMetal = app::AppManager::isBareMetal();
    if (bareMetal) {
        shell::execute("echo [PASS] Running in bare-metal/UEFI mode");
    } else {
        shell::execute("echo [INFO] Running in hosted mode");
    }
    
    // Test 2: Check compositor initialization
    if (compositor::KernelCompositor::getWindowCount() >= 0) {
        shell::execute("echo [PASS] Compositor initialized");
    } else {
        shell::execute("echo [FAIL] Compositor not initialized");
    }
    
    // Test 3: Check IPC system
    int testChannel = ipc::IpcManager::createChannel("test", 999);
    if (testChannel >= 0) {
        shell::execute("echo [PASS] IPC system operational");
        ipc::IpcManager::destroyChannel(testChannel);
    } else {
        shell::execute("echo [FAIL] IPC system failed");
    }
    
    // Test 4: Try launching Notepad
    shell::execute("echo Testing Notepad launch...");
    if (app::AppManager::isAppAvailable("Notepad")) {
        bool launched = app::AppManager::launchApp("Notepad");
        if (launched) {
            shell::execute("echo [PASS] Notepad launched successfully");
        } else {
            shell::execute("echo [FAIL] Notepad failed to launch");
        }
    } else {
        shell::execute("echo [SKIP] Notepad not registered");
    }
    
    // Test 5: Try launching Calculator  
    shell::execute("echo Testing Calculator launch...");
    if (app::AppManager::isAppAvailable("Calculator")) {
        bool launched = app::AppManager::launchApp("Calculator");
        if (launched) {
            shell::execute("echo [PASS] Calculator launched successfully");
        } else {
            shell::execute("echo [FAIL] Calculator failed to launch");
        }
    } else {
        shell::execute("echo [SKIP] Calculator not registered");
    }
    
    // Test 6: Try launching TaskManager
    shell::execute("echo Testing TaskManager launch...");
    if (app::AppManager::isAppAvailable("TaskManager")) {
        bool launched = app::AppManager::launchApp("TaskManager");
        if (launched) {
            shell::execute("echo [PASS] TaskManager launched successfully");
        } else {
            shell::execute("echo [FAIL] TaskManager failed to launch");
        }
    } else {
        shell::execute("echo [SKIP] TaskManager not registered");
    }
    
    // Test 7: Check running app count
    int runningCount = app::AppManager::getRunningAppCount();
    if (runningCount > 0) {
        // Build result message manually (no sprintf in kernel)
        shell::execute("echo [PASS] Apps running in compositor");
    } else {
        shell::execute("echo [INFO] No apps currently running");
    }
    
    // Test 8: Check window count in compositor
    int windowCount = compositor::KernelCompositor::getWindowCount();
    if (windowCount >= 0) {
        shell::execute("echo [PASS] Compositor window tracking operational");
    }
    
    // Log test completion
    shell::execute("echo === Test Mode Complete ===");
    
    // Log summary from AppLogger
    int logCount = app::AppLogger::getLogCount();
    if (logCount > 0) {
        shell::execute("echo App launch log entries recorded");
    }
    
    // Show notification about test completion
    s_notification.title = "Test Mode Complete";
    s_notification.message = "Check console for results";
    s_notification.visible = true;
}

// Check if compositor/IPC is available for GUI apps
bool is_compositor_available()
{
    return s_initialized && 
           compositor::KernelCompositor::getWindowCount() >= 0;
}

void init()
{
    s_screenW = framebuffer::get_width();
    s_screenH = framebuffer::get_height();
    s_startMenuOpen = false;
    s_rightClickMenuOpen = false;
    s_notification.visible = true;
    s_tickCounter = 0;
    s_lastClickedIcon = -1;
    s_lastClickTime = 0;
    initialize_icon_positions();  // Use new icon management system
    shell::init();
    
    // Enable double buffering to prevent flickering during window movement
    framebuffer::enable_double_buffering();
    
    // Initialize kernel app framework
    ipc::IpcManager::init();
    apps::registerKernelApps();
    compositor::KernelCompositor::init(s_screenW, s_screenH, kTaskbarH);
    compositor::TaskbarManager::init(s_screenW, s_screenH, kTaskbarH, 
                                     4 + kStartBtnW + 8 + kSearchBoxW + 8);
    
    s_initialized = true;
}

// Update tick counter (call this from main loop, e.g., every 10ms)
void tick()
{
    s_tickCounter++;
    
    // Update time every ~100 ticks (roughly 1 second if tick is called every 10ms)
    if (s_tickCounter % 100 == 0) {
        update_time();
    }
    
    ipc::IpcManager::tick();
    
    // Update running apps
    app::AppManager::update();
    
    // Update taskbar buttons for kernel apps
    compositor::TaskbarManager::updateButtons();
}


// Helper function to draw the shell window
static void draw_shell_window()
{
    ShellWindowGeometry g = get_shell_geometry();
    
    // ===== Window shadow (matching compositor style) =====
    framebuffer::fill_rect(g.x + 3, g.y + 3, g.w, g.h, rgb(15, 15, 20));
    
    // ===== Window content background =====
    framebuffer::fill_rect(g.x, g.y, g.w, g.h, rgb(34, 34, 34));
    
    // ===== Window border (focused = blue accent, unfocused = gray) =====
    uint32_t borderColor = s_shellActive ? rgb(85, 136, 170) : rgb(51, 51, 51);
    draw_rect(g.x, g.y, g.w, g.h, borderColor);
    
    // ===== Title bar (dark gradient, matching Legacy) =====
    uint32_t titlebarColor = s_shellActive ? rgb(43, 80, 111) : rgb(17, 17, 17);
    framebuffer::fill_rect(g.x + 1, g.y + 1, g.w - 2, g.titlebarH - 1, titlebarColor);
    
    // ===== Window title text (centered, matching Legacy) =====
    const char* title = "Terminal";
    int titleW = measure_text(title);
    uint32_t titleX = g.x + (g.w - titleW) / 2;
    draw_text(titleX, g.y + 7, title, s_shellActive ? rgb(240, 240, 240) : rgb(150, 150, 160), 1);
    
    // ===== Title bar buttons (matching guideXOS.Legacy style) =====
    // Button styling constants
    const uint32_t btnSize = 16;
    const uint32_t btnGap = 6;
    const uint32_t btnY = g.y + 4;
    
    // Close button (rightmost) - red background
    uint32_t closeBtnX = g.x + g.w - btnGap - btnSize;
    uint32_t closeNormal = rgb(120, 40, 40);
    framebuffer::fill_rect(closeBtnX, btnY, btnSize, btnSize, closeNormal);
    // Draw X icon (2px thick lines)
    uint32_t iconColor = rgb(250, 250, 250);
    int iconOff = btnSize / 3;
    for (int i = 0; i < iconOff * 2; i++) {
        framebuffer::put_pixel(closeBtnX + iconOff + i, btnY + iconOff + i, iconColor);
        framebuffer::put_pixel(closeBtnX + iconOff + i + 1, btnY + iconOff + i, iconColor);
        framebuffer::put_pixel(closeBtnX + btnSize - iconOff - i - 1, btnY + iconOff + i, iconColor);
        framebuffer::put_pixel(closeBtnX + btnSize - iconOff - i, btnY + iconOff + i, iconColor);
    }
    // Border
    draw_rect(closeBtnX, btnY, btnSize, btnSize, rgb(80, 80, 80));
    
    // Maximize button (left of close) - blue tint on hover
    uint32_t maxBtnX = closeBtnX - btnGap - btnSize;
    uint32_t maxColor = rgb(46, 46, 46);
    framebuffer::fill_rect(maxBtnX, btnY, btnSize, btnSize, maxColor);
    if (s_shellMaximized) {
        // Restore icon (two overlapping squares)
        draw_rect(maxBtnX + 5, btnY + 3, 7, 7, iconColor);
        draw_rect(maxBtnX + 3, btnY + 5, 7, 7, iconColor);
    } else {
        // Maximize icon (square)
        draw_rect(maxBtnX + 4, btnY + 4, 8, 8, iconColor);
    }
    draw_rect(maxBtnX, btnY, btnSize, btnSize, rgb(80, 80, 80));
    
    // Minimize button (left of maximize) - green tint on hover
    uint32_t minBtnX = maxBtnX - btnGap - btnSize;
    uint32_t minColor = rgb(46, 46, 46);
    framebuffer::fill_rect(minBtnX, btnY, btnSize, btnSize, minColor);
    // Minimize icon (underscore line)
    hline(minBtnX + 4, btnY + btnSize - 5, 8, iconColor);
    draw_rect(minBtnX, btnY, btnSize, btnSize, rgb(80, 80, 80));
    
    // ===== Resize grip (bottom-right corner, matching Legacy) =====
    if (!s_shellMaximized && shell::get_state() != shell::ShellState::Fullscreen) {
        uint32_t gripX = g.x + g.w - 16;
        uint32_t gripY = g.y + g.h - 16;
        uint32_t gripLineColor = rgb(119, 119, 119);
        // Draw 3 diagonal grip lines
        for (int line = 0; line < 3; line++) {
            int offset = line * 4;
            // Each line is a few pixels
            framebuffer::put_pixel(gripX + 14 - offset, gripY + 13, gripLineColor);
            framebuffer::put_pixel(gripX + 13 - offset, gripY + 12, gripLineColor);
            framebuffer::put_pixel(gripX + 12 - offset, gripY + 11, gripLineColor);
            framebuffer::put_pixel(gripX + 13, gripY + 13 - offset, gripLineColor);
            framebuffer::put_pixel(gripX + 12, gripY + 12 - offset, gripLineColor);
            framebuffer::put_pixel(gripX + 11, gripY + 11 - offset, gripLineColor);
        }
    }
    
    shell::draw(g.x, g.y, g.w, g.h);
}


void draw()
{
    if (!s_initialized || !framebuffer::is_available()) return;

    draw_background();
    draw_desktop_icons();
    draw_taskbar();
    draw_right_click_menu();
    draw_notifications();
    draw_shutdown_dialog();
    draw_control_panel();
    draw_device_manager();
    draw_network_adapters();
    draw_network_config();
    
    // Draw windows in proper z-order based on focus
    // Shell window is drawn first if not active, last if active
    bool shellVisible = shell::is_open() && !s_shellMinimized;
    
    // Draw shell window first if not active (so compositor windows appear on top)
    if (shellVisible && !s_shellActive) {
        draw_shell_window();
    }
    
    // Draw kernel compositor windows (GUI apps)
    compositor::KernelCompositor::drawAllWindows();
    
    // Draw taskbar buttons for kernel apps
    compositor::TaskbarManager::drawButtons();
    
    // Draw shell window last if active (so it appears on top)
    if (shellVisible && s_shellActive) {
        draw_shell_window();
    }
    
    // Draw start menu LAST so it appears on top of all windows
    draw_start_menu();
    
    // Present the back buffer to the screen (swap buffers)
    // This is the key to preventing flickering - all drawing is done
    // to the back buffer, then copied to the screen in one operation
    framebuffer::present();
}




void show_context_menu(uint32_t x, uint32_t y)
{
    s_rightClickX = x;
    s_rightClickY = y;
    s_rightClickMenuOpen = true;
    // Close start menu when context menu is shown
    s_startMenuOpen = false;
}

void close_context_menu()
{
    s_rightClickMenuOpen = false;
}

void toggle_start_menu()
{
    s_startMenuOpen = !s_startMenuOpen;
    
    if (s_startMenuOpen) {
        // Initialize start menu state when opening
        s_startMenuSelection = 0;
        s_startMenuScroll = 0;
        s_startMenuAllProgs = false;  // Start with pinned/recent view
        refresh_start_menu_list();     // Sync with desktop icon states
        s_hoverMenuLeft = -1;
        s_hoverMenuRight = -1;
        s_clickedMenuLeft = -1;
        s_clickedMenuRight = -1;
    }
}

bool is_start_menu_open()
{
    return s_startMenuOpen;
}

void dismiss_notification()
{
    s_notification.visible = false;
}

void open_terminal()
{
    shell::open();
    s_shellActive = true;
    s_shellMinimized = false;
}

bool launch_app(const char* appName)
{
    if (!appName) return false;
    
    // Check for Console/Terminal special case
    bool isConsole = false;
    const char* c = "Console";
    const char* l = appName;
    while (*c && *l && *c == *l) { c++; l++; }
    if (*c == '\0' && *l == '\0') isConsole = true;
    
    // Also check for "Terminal"
    if (!isConsole) {
        c = "Terminal";
        l = appName;
        while (*c && *l && *c == *l) { c++; l++; }
        if (*c == '\0' && *l == '\0') isConsole = true;
    }
    
    if (isConsole) {
        open_terminal();
        return true;
    }
    
    // Try to launch as kernel GUI app
    return try_launch_kernel_app(appName);
}

bool is_bare_metal_mode()
{
    return app::AppManager::isBareMetal();
}

int get_running_app_count()
{
    return app::AppManager::getRunningAppCount();
}

void handle_key(uint32_t key)
{
    // Route keyboard input to focused kernel compositor window first
    if (compositor::KernelCompositor::hasWindows()) {
        app::KernelWindow* focused = compositor::KernelCompositor::getFocusedWindow();
        if (focused) {
            // Escape closes app window
            if (key == 27) {  // ESC
                compositor::KernelCompositor::closeWindow(focused->id);
                draw();
                return;
            }
            
            // Route printable character keys to the compositor
            if (key >= 32 && key < 127) {
                compositor::KernelCompositor::handleKeyChar((char)key);
                draw();
                return;
            }
            
            // Route control characters and special keys to handleKeyDown
            // This includes Enter ('\n'=10), Backspace ('\b'=8), Tab ('\t'=9),
            // arrow keys (0x100+), and all other non-printable keys.
            compositor::KernelCompositor::handleKeyDown(key);
            draw();
            return;
        }
    }
    
    // If shell is open, send keys to it
    if (shell::is_open()) {
        // Escape closes shell
        if (key == 27) {  // ESC
            shell::close();
            s_shellPosX = -1;  // Reset position for next open
            s_shellPosY = -1;
            s_shellW = -1;
            s_shellH = -1;
            s_shellMinimized = false;
            s_shellMaximized = false;
            s_shellActive = true;
            draw();
            return;
        }
        // F11 toggles fullscreen
        if (key == 0x10A) {  // F11
            shell::toggle_fullscreen();
            draw();
            return;
        }
        // Only process keys if shell is active and not minimized
        if (s_shellActive && !s_shellMinimized) {
            shell::process_key(key);
            draw();
        }
        return;
    }
    
    // Desktop keyboard shortcuts
    if (key == '`' || key == '~') {
        // Backtick opens/restores terminal
        if (shell::is_open() && s_shellMinimized) {
            s_shellMinimized = false;
            s_shellActive = true;
        } else {
            shell::toggle();
            s_shellActive = true;
            s_shellMinimized = false;
        }
        draw();
        return;
    }
    
    // Arrow key navigation for desktop icons
    if (s_selectedIcon >= 0 && s_selectedIcon < s_visibleIconCount) {
        int newIcon = -1;
        
        if (key == shell::KEY_UP) {
            newIcon = find_nearest_icon_in_direction(s_selectedIcon, 0);
        } else if (key == shell::KEY_DOWN) {
            newIcon = find_nearest_icon_in_direction(s_selectedIcon, 1);
        } else if (key == shell::KEY_LEFT) {
            newIcon = find_nearest_icon_in_direction(s_selectedIcon, 2);
        } else if (key == shell::KEY_RIGHT) {
            newIcon = find_nearest_icon_in_direction(s_selectedIcon, 3);
        } else if (key == '\n' || key == '\r') {
            // Enter key launches the selected icon
            show_icon_notification(s_selectedIcon);
            draw();
            return;
        }
        
        if (newIcon >= 0) {
            s_selectedIcon = newIcon;
            draw();
            return;
        }
    }
}

// ============================================================
// Mouse cursor (12x19 arrow, 1-bit mask)
// ============================================================

// Request redraw flag for keyboard IRQ handler
static volatile bool s_needsRedraw = false;

} // namespace desktop
} // namespace kernel

// External function for keyboard IRQ to request redraw
void desktop_request_redraw()
{
    kernel::desktop::s_needsRedraw = true;
}

namespace kernel {
namespace desktop {

// Check and clear redraw flag
bool needs_redraw()
{
    if (s_needsRedraw) {
        s_needsRedraw = false;
        return true;
    }
    return false;
}

// Classic arrow cursor bitmap (12 wide x 19 tall)
// 0=transparent, 1=black outline, 2=white fill
static const uint8_t s_cursorBitmap[19][12] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,1,1,1,1,1,0},
    {1,2,2,2,1,2,1,0,0,0,0,0},
    {1,2,2,1,0,1,2,1,0,0,0,0},
    {1,2,1,0,0,1,2,1,0,0,0,0},
    {1,1,0,0,0,0,1,2,1,0,0,0},
    {1,0,0,0,0,0,1,2,1,0,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0},
};

static const int kCursorW = 12;
static const int kCursorH = 19;

// Saved pixels under cursor for restore
static uint32_t s_cursorSave[19][12];
static int32_t  s_lastCursorX = -1;
static int32_t  s_lastCursorY = -1;
static bool     s_cursorDrawn = false;

// Previous button state for edge detection
static uint8_t  s_prevButtons = 0;

// Hit-test desktop icons: returns display index or -1
static int hit_test_icon(int32_t mx, int32_t my)
{
    uint32_t deskH = s_screenH - kTaskbarH;

    for (int displayIdx = 0; displayIdx < s_visibleIconCount; displayIdx++) {
        uint32_t cx = (uint32_t)s_iconPosX[displayIdx];
        uint32_t cy = (uint32_t)s_iconPosY[displayIdx];

        if (cy + kIconCellH > deskH) continue;

        if ((uint32_t)mx >= cx && (uint32_t)mx < cx + kIconCellW &&
            (uint32_t)my >= cy && (uint32_t)my < cy + kIconCellH) {
            return displayIdx;
        }
    }
    return -1;
}

// Find the nearest icon in a given direction from the currently selected icon
// direction: 0=up, 1=down, 2=left, 3=right
// Returns display index or -1 if none found
static int find_nearest_icon_in_direction(int currentIcon, int direction)
{
    if (currentIcon < 0 || currentIcon >= s_visibleIconCount) return -1;
    
    int32_t currX = s_iconPosX[currentIcon];
    int32_t currY = s_iconPosY[currentIcon];
    int32_t currCenterX = currX + (int32_t)(kIconCellW / 2);
    int32_t currCenterY = currY + (int32_t)(kIconCellH / 2);
    
    int bestIcon = -1;
    int32_t bestDistance = 0x7FFFFFFF; // Max int32
    
    for (int i = 0; i < s_visibleIconCount; i++) {
        if (i == currentIcon) continue;
        
        int32_t iconX = s_iconPosX[i];
        int32_t iconY = s_iconPosY[i];
        int32_t iconCenterX = iconX + (int32_t)(kIconCellW / 2);
        int32_t iconCenterY = iconY + (int32_t)(kIconCellH / 2);
        
        int32_t dx = iconCenterX - currCenterX;
        int32_t dy = iconCenterY - currCenterY;
        
        // Check if icon is in the correct direction
        bool validDirection = false;
        int32_t primaryDist = 0;
        int32_t secondaryDist = 0;
        
        switch (direction) {
            case 0: // Up
                if (dy < 0) {
                    validDirection = true;
                    primaryDist = -dy;
                    secondaryDist = (dx < 0 ? -dx : dx);
                }
                break;
            case 1: // Down
                if (dy > 0) {
                    validDirection = true;
                    primaryDist = dy;
                    secondaryDist = (dx < 0 ? -dx : dx);
                }
                break;
            case 2: // Left
                if (dx < 0) {
                    validDirection = true;
                    primaryDist = -dx;
                    secondaryDist = (dy < 0 ? -dy : dy);
                }
                break;
            case 3: // Right
                if (dx > 0) {
                    validDirection = true;
                    primaryDist = dx;
                    secondaryDist = (dy < 0 ? -dy : dy);
                }
                break;
        }
        
        if (!validDirection) continue;
        
        // Calculate weighted distance (prioritize primary direction)
        int32_t distance = primaryDist + (secondaryDist / 2);
        
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIcon = i;
        }
    }
    
    return bestIcon;
}

// Show a notification for an icon launch (or launch the app if available)
static void show_icon_notification(int displayIndex)
{
    if (displayIndex < 0 || displayIndex >= s_visibleIconCount) return;
    
    int iconIdx = s_visibleIconIndices[displayIndex];
    const char* label = s_desktopIcons[iconIdx].label;
    
    // Check if this is Console - special case for shell
    bool isConsole = false;
    const char* c = "Console";
    const char* l = label;
    while (*c && *l && *c == *l) { c++; l++; }
    if (*c == '\0' && *l == '\0') isConsole = true;
    
    if (isConsole) {
        shell::open();
        s_shellActive = true;
        s_shellMinimized = false;
        add_to_recent(label);  // Add to recent apps
        return;
    }
    
    // Try to launch as a kernel GUI app
    if (app::AppManager::isAppAvailable(label)) {
        if (app::AppManager::launchApp(label)) {
            // App launched successfully
            app::AppLogger::logLaunch(label, app::LaunchResult::Success);
            add_to_recent(label);  // Add to recent apps
            return;
        }
        // App available but failed to launch
        s_notification.title = label;
        s_notification.message = "Failed to launch app";
        s_notification.visible = true;
        app::AppLogger::logLaunch(label, app::LaunchResult::FailedToInit);
        return;
    }
    
    // App not available in kernel mode - show notification
    s_notification.title = label;
    s_notification.message = "Not available in bare-metal mode";
    s_notification.visible = true;
    app::AppLogger::logLaunch(label, app::LaunchResult::NotAvailable);
}

// Compute start menu geometry (shared between drawing and hit-testing)
struct StartMenuGeometry {
    uint32_t menuX, menuY, menuH;
    uint32_t headerH, footerH, maxBodyH;
    uint32_t contentY;
    uint32_t leftColW;
    uint32_t rightX;
};

static StartMenuGeometry get_start_menu_geometry()
{
    StartMenuGeometry g;
    g.headerH = 30;
    g.footerH = 36;
    uint32_t bodyH = (uint32_t)kStartMenuAppCount * kStartMenuItemH;
    uint32_t rightBodyH = (uint32_t)kStartMenuRightCount * kStartMenuItemH;
    g.maxBodyH = bodyH > rightBodyH ? bodyH : rightBodyH;
    g.menuH = g.headerH + g.maxBodyH + g.footerH;
    g.menuX = 4;
    g.menuY = s_screenH - kTaskbarH - g.menuH;
    g.leftColW = kStartMenuW - kStartMenuRightColW;
    g.contentY = g.menuY + g.headerH + 1;
    g.rightX = g.menuX + g.leftColW + 1;
    return g;
}

// Check if a point is inside the start menu bounds (when open)
static bool is_point_in_start_menu(int32_t mx, int32_t my)
{
    if (!s_startMenuOpen) return false;
    StartMenuGeometry g = get_start_menu_geometry();
    return (uint32_t)mx >= g.menuX && (uint32_t)mx < g.menuX + kStartMenuW &&
           (uint32_t)my >= g.menuY && (uint32_t)my < g.menuY + g.menuH;
}

// Check if a point is inside any modal dialog (Control Panel, Device Manager, etc.)
static bool is_point_in_modal_dialog(int32_t mx, int32_t my)
{
    // Shutdown dialog
    if (s_shutdownDialogOpen) {
        uint32_t dlgX = (s_screenW - kShutdownDlgW) / 2;
        uint32_t dlgY = (s_screenH - kShutdownDlgH) / 2;
        if ((uint32_t)mx >= dlgX && (uint32_t)mx < dlgX + kShutdownDlgW &&
            (uint32_t)my >= dlgY && (uint32_t)my < dlgY + kShutdownDlgH) {
            return true;
        }
    }
    
    // Control Panel
    if (s_controlPanelOpen) {
        uint32_t dlgX = (s_screenW - kControlPanelW) / 2;
        uint32_t dlgY = (s_screenH - kControlPanelH) / 2;
        if ((uint32_t)mx >= dlgX && (uint32_t)mx < dlgX + kControlPanelW &&
            (uint32_t)my >= dlgY && (uint32_t)my < dlgY + kControlPanelH) {
            return true;
        }
    }
    
    // Device Manager
    if (s_deviceManagerOpen) {
        uint32_t dlgX = (s_screenW - kDeviceMgrW) / 2;
        uint32_t dlgY = (s_screenH - kDeviceMgrH) / 2;
        if ((uint32_t)mx >= dlgX && (uint32_t)mx < dlgX + kDeviceMgrW &&
            (uint32_t)my >= dlgY && (uint32_t)my < dlgY + kDeviceMgrH) {
            return true;
        }
    }
    
    // Network Adapters
    if (s_networkAdaptersOpen) {
        uint32_t dlgX = (s_screenW - kNetAdaptersW) / 2;
        uint32_t dlgY = (s_screenH - kNetAdaptersH) / 2;
        if ((uint32_t)mx >= dlgX && (uint32_t)mx < dlgX + kNetAdaptersW &&
            (uint32_t)my >= dlgY && (uint32_t)my < dlgY + kNetAdaptersH) {
            return true;
        }
    }
    
    // Network Config dialog
    if (s_networkConfigOpen) {
        uint32_t dlgX = (s_screenW - kNetConfigW) / 2;
        uint32_t dlgY = (s_screenH - kNetConfigH) / 2;
        if ((uint32_t)mx >= dlgX && (uint32_t)mx < dlgX + kNetConfigW &&
            (uint32_t)my >= dlgY && (uint32_t)my < dlgY + kNetConfigH) {
            return true;
        }
    }
    
    return false;
}

// Hit-test start menu items: sets leftIdx or rightIdx to item index, or -1
static void hit_test_start_menu(int32_t mx, int32_t my, int& leftIdx, int& rightIdx)
{
    leftIdx = -1;
    rightIdx = -1;
    if (!s_startMenuOpen) return;

    StartMenuGeometry g = get_start_menu_geometry();

    // Check if inside menu bounds at all
    if ((uint32_t)mx < g.menuX || (uint32_t)mx >= g.menuX + kStartMenuW ||
        (uint32_t)my < g.menuY || (uint32_t)my >= g.menuY + g.menuH) {
        return;
    }

    // Only test in the body area (below header, above footer)
    if ((uint32_t)my < g.contentY || (uint32_t)my >= g.contentY + g.maxBodyH) {
        return;
    }

    uint32_t relY = (uint32_t)my - g.contentY;
    int itemRow = static_cast<int>(relY / kStartMenuItemH);

    // Left column
    if ((uint32_t)mx >= g.menuX && (uint32_t)mx < g.menuX + g.leftColW) {
        if (itemRow >= 0 && itemRow < kStartMenuAppCount) {
            leftIdx = itemRow;
        }
    }
    // Right column
    else if ((uint32_t)mx >= g.rightX && (uint32_t)mx < g.rightX + kStartMenuRightColW) {
        if (itemRow >= 0 && itemRow < kStartMenuRightCount) {
            rightIdx = itemRow;
        }
    }
}


// Show notification for a start menu item launch (or launch the app)
static void show_start_menu_notification(const char* label)
{
    // Check if this is Console - if so, launch the shell
    bool isConsole = false;
    const char* c = "Console";
    const char* l = label;
    while (*c && *l && *c == *l) { c++; l++; }
    if (*c == '\0' && *l == '\0') isConsole = true;
    
    if (isConsole) {
        s_startMenuOpen = false;
        shell::open();
        s_shellActive = true;
        s_shellMinimized = false;
        return;
    }
    
    // Close start menu before launching app
    s_startMenuOpen = false;
    
    // Try to launch as a kernel GUI app
    if (try_launch_kernel_app(label)) {
        // App launched successfully
        app::AppLogger::logLaunch(label, app::LaunchResult::Success);
        return;
    }
    
    // App not available in kernel mode - show notification
    s_notification.title = label;
    s_notification.message = "Not available in bare-metal mode";
    s_notification.visible = true;
    app::AppLogger::logLaunch(label, app::LaunchResult::NotAvailable);
}


// Footer button IDs
enum FooterButton {
    FOOTER_NONE = 0,
    FOOTER_SHUTDOWN = 1,
    FOOTER_RESTART = 2,
    FOOTER_SLEEP = 3,
    FOOTER_ALL_PROGRAMS = 4
};

// Hit-test start menu footer buttons
static FooterButton hit_test_start_menu_footer(int32_t mx, int32_t my)
{
    if (!s_startMenuOpen) return FOOTER_NONE;

    StartMenuGeometry g = get_start_menu_geometry();

    // Check if inside menu bounds
    if ((uint32_t)mx < g.menuX || (uint32_t)mx >= g.menuX + kStartMenuW ||
        (uint32_t)my < g.menuY || (uint32_t)my >= g.menuY + g.menuH) {
        return FOOTER_NONE;
    }

    // Check if in footer area
    uint32_t footerY = g.menuY + g.headerH + g.maxBodyH;
    if ((uint32_t)my < footerY || (uint32_t)my >= footerY + g.footerH) {
        return FOOTER_NONE;
    }

    // Button dimensions (must match draw_start_menu)
    uint32_t shutW = 80, shutH = 24;
    uint32_t shutX = g.menuX + kStartMenuW - shutW - 12;
    uint32_t shutY = footerY + (g.footerH - shutH) / 2;

    uint32_t restartW = 60;
    uint32_t restartX = shutX - restartW - 6;

    uint32_t sleepW = 50;
    uint32_t sleepX = restartX - sleepW - 6;

    // Test Shutdown button
    if ((uint32_t)mx >= shutX && (uint32_t)mx < shutX + shutW &&
        (uint32_t)my >= shutY && (uint32_t)my < shutY + shutH) {
        return FOOTER_SHUTDOWN;
    }

    // Test Restart button
    if ((uint32_t)mx >= restartX && (uint32_t)mx < restartX + restartW &&
        (uint32_t)my >= shutY && (uint32_t)my < shutY + shutH) {
        return FOOTER_RESTART;
    }

    // Test Sleep button
    if ((uint32_t)mx >= sleepX && (uint32_t)mx < sleepX + sleepW &&
        (uint32_t)my >= shutY && (uint32_t)my < shutY + shutH) {
        return FOOTER_SLEEP;
    }

    return FOOTER_NONE;
}

static void save_under_cursor(int32_t mx, int32_t my)
{
    // Save from front buffer (video memory) since cursor is drawn there
    uint32_t* frontBuffer = framebuffer::get_buffer();
    if (!frontBuffer) return;
    
    uint32_t pitch = framebuffer::get_pitch() / 4;
    
    for (int row = 0; row < kCursorH; row++) {
        for (int col = 0; col < kCursorW; col++) {
            int32_t px = mx + col;
            int32_t py = my + row;
            if (px >= 0 && px < (int32_t)s_screenW && py >= 0 && py < (int32_t)s_screenH)
                s_cursorSave[row][col] = frontBuffer[py * pitch + px];
            else
                s_cursorSave[row][col] = 0;
        }
    }
}

static void restore_under_cursor()
{
    if (!s_cursorDrawn) return;
    
    // Restore to front buffer (video memory) since cursor was drawn there
    uint32_t* frontBuffer = framebuffer::get_buffer();
    if (!frontBuffer) return;
    
    uint32_t pitch = framebuffer::get_pitch() / 4;
    
    for (int row = 0; row < kCursorH; row++) {
        for (int col = 0; col < kCursorW; col++) {
            int32_t px = s_lastCursorX + col;
            int32_t py = s_lastCursorY + row;
            if (px >= 0 && px < (int32_t)s_screenW && py >= 0 && py < (int32_t)s_screenH)
                frontBuffer[py * pitch + px] = s_cursorSave[row][col];
        }
    }
    s_cursorDrawn = false;
}

void draw_cursor(int32_t mx, int32_t my)
{
    // Restore previous cursor area
    restore_under_cursor();

    // Save pixels under new position (from front buffer)
    save_under_cursor(mx, my);
    s_lastCursorX = mx;
    s_lastCursorY = my;

    // Draw cursor directly to front buffer (bypass double buffering for cursor)
    // This ensures the cursor is always visible even with double buffering enabled
    uint32_t* frontBuffer = framebuffer::get_buffer();
    if (!frontBuffer) return;
    
    uint32_t pitch = framebuffer::get_pitch() / 4;
    
    for (int row = 0; row < kCursorH; row++) {
        for (int col = 0; col < kCursorW; col++) {
            uint8_t p = s_cursorBitmap[row][col];
            if (p == 0) continue;
            int32_t px = mx + col;
            int32_t py = my + row;
            if (px >= 0 && px < (int32_t)s_screenW && py >= 0 && py < (int32_t)s_screenH) {
                uint32_t color = (p == 1) ? rgb(0, 0, 0) : rgb(255, 255, 255);
                // Write directly to front buffer
                frontBuffer[py * pitch + px] = color;
            }
        }
    }
    s_cursorDrawn = true;
}

void handle_mouse(int32_t mx, int32_t my, uint8_t buttons)
{
    if (!s_initialized) return;

    // Detect button press/release edges
    uint8_t pressed  = buttons & ~s_prevButtons;   // newly pressed
    uint8_t released = s_prevButtons & ~buttons;    // newly released
    s_prevButtons = buttons;

    uint32_t taskbarY = s_screenH - kTaskbarH;

    // ---- Route input to kernel compositor first (GUI apps) ----
    // Check if point is over a compositor window OR if compositor has an active button press
    // (need to send mouse up even if mouse moved outside window bounds)
    // BUT: Start menu and modal dialogs take priority over compositor windows
    bool overStartMenu = is_point_in_start_menu(mx, my);
    bool overModalDialog = is_point_in_modal_dialog(mx, my);
    bool overUIOverlay = overStartMenu || overModalDialog;
    bool overCompositorWindow = !overUIOverlay && compositor::KernelCompositor::isPointOverWindow(mx, my);
    bool compositorHasActivePress = compositor::KernelCompositor::isButtonPressActive();
    
    if ((overCompositorWindow || compositorHasActivePress) && !overUIOverlay) {
        // Deactivate shell window when clicking a compositor window
        if (overCompositorWindow && s_shellActive) {
            s_shellActive = false;
        }
        
        compositor::KernelCompositor::handleMouseMove(mx, my);
        if (pressed & 0x01) {
            compositor::KernelCompositor::handleMouseDown(mx, my, 0x01);
        }
        if (released & 0x01) {
            compositor::KernelCompositor::handleMouseUp(mx, my, 0x01);
        }
        if (pressed & 0x02) {
            compositor::KernelCompositor::handleMouseDown(mx, my, 0x02);
        }
        if (released & 0x02) {
            compositor::KernelCompositor::handleMouseUp(mx, my, 0x02);
        }
        draw();
        draw_cursor(mx, my);
        
        // Return early if actually over window OR handling any button press/release
        // This ensures mouse events are properly sent to compositor and not to desktop
        if (overCompositorWindow || (pressed & 0x03) || (released & 0x03)) {
            return;
        }
    }
    
    // Update compositor hover states even when not over a window
    compositor::KernelCompositor::handleMouseMove(mx, my);
    
    // Check for taskbar button clicks (for kernel apps)
    if ((pressed & 0x01) && (uint32_t)my >= taskbarY) {
        compositor::TaskbarManager::updateHover(mx, my);
        if (compositor::TaskbarManager::handleClick(mx, my)) {
            draw();
            draw_cursor(mx, my);
            return;
        }
    }

    // ---- Handle shell window dragging (left button held) ----
    if (s_shellDragging && (buttons & 0x01)) {
        // Update shell window position during drag
        int32_t newX = mx - s_shellDragOffsetX;
        int32_t newY = my - s_shellDragOffsetY;
        
        // Clamp to screen bounds
        if (newX < 0) newX = 0;
        if (newY < 0) newY = 0;
        if (newX + (int32_t)kShellDefaultW > (int32_t)s_screenW)
            newX = (int32_t)s_screenW - (int32_t)kShellDefaultW;
        if (newY + (int32_t)kShellDefaultH > (int32_t)taskbarY)
            newY = (int32_t)taskbarY - (int32_t)kShellDefaultH;
        
        s_shellPosX = newX;
        s_shellPosY = newY;
        
        draw();
        draw_cursor(mx, my);
        return;
    }
    
    // ---- Handle shell window drag release ----
    if (s_shellDragging && (released & 0x01)) {
        s_shellDragging = false;
        draw();
        draw_cursor(mx, my);
        return;
    }
    
    // ---- Handle shell window resize in progress ----
    if (s_shellResizing && (buttons & 0x01)) {
        int32_t deltaX = mx - s_shellResizeStartX;
        int32_t deltaY = my - s_shellResizeStartY;
        
        int32_t newW = s_shellResizeStartW + deltaX;
        int32_t newH = s_shellResizeStartH + deltaY;
        
        // Clamp to minimum size
        if (newW < (int32_t)kShellMinW) newW = (int32_t)kShellMinW;
        if (newH < (int32_t)kShellMinH) newH = (int32_t)kShellMinH;
        
        // Clamp to screen bounds
        ShellWindowGeometry g = get_shell_geometry();
        if ((int32_t)g.x + newW > (int32_t)s_screenW)
            newW = (int32_t)s_screenW - (int32_t)g.x;
        if ((int32_t)g.y + newH > (int32_t)(s_screenH - kTaskbarH))
            newH = (int32_t)(s_screenH - kTaskbarH) - (int32_t)g.y;
        
        s_shellW = newW;
        s_shellH = newH;
        
        draw();
        draw_cursor(mx, my);
        return;
    }
    
    // ---- Handle shell window resize release ----
    if (s_shellResizing && (released & 0x01)) {
        s_shellResizing = false;
        draw();
        draw_cursor(mx, my);
        return;
    }

    // ---- Handle icon drag-in-progress (left button held) ----
    if (s_dragging && (buttons & 0x01)) {
        // Update current drag position
        s_dragCurrentX = mx;
        s_dragCurrentY = my;

        // Check if we've exceeded the drag threshold
        if (!s_dragStarted) {
            int32_t dx = mx - s_dragStartMouseX;
            int32_t dy = my - s_dragStartMouseY;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx > kDragThreshold || dy > kDragThreshold) {
                s_dragStarted = true;
            }
        }

        if (s_dragStarted) {
            // Redraw scene with icon at drag position
            draw();
            draw_cursor(mx, my);
            return;
        }
    }

    // ---- Handle drag release (left button released while dragging) ----
    if (s_dragging && (released & 0x01)) {
        if (s_dragStarted && s_dragIconIndex >= 0 && s_dragIconIndex < kDesktopIconCount) {
            // Compute new icon position from drop point
            int32_t newX = mx - s_dragOffsetX;
            int32_t newY = my - s_dragOffsetY;

            // Clamp to desktop area
            if (newX < 0) newX = 0;
            if (newY < 0) newY = 0;
            if (newX + (int32_t)kIconCellW > (int32_t)s_screenW)
                newX = (int32_t)s_screenW - (int32_t)kIconCellW;
            if (newY + (int32_t)kIconCellH > (int32_t)taskbarY)
                newY = (int32_t)taskbarY - (int32_t)kIconCellH;

            s_iconPosX[s_dragIconIndex] = newX;
            s_iconPosY[s_dragIconIndex] = newY;
            
            // Save icon positions to VFS
            save_icon_positions();
        } else if (!s_dragStarted && s_dragIconIndex >= 0 && s_dragIconIndex < kDesktopIconCount) {
            // Click without drag - just select the icon (launch happens on double-click)
            s_selectedIcon = s_dragIconIndex;
        }

        s_dragging = false;
        s_dragStarted = false;
        s_dragIconIndex = -1;
        draw();
        draw_cursor(mx, my);
        return;
    }

    // Left button press
    if (pressed & 0x01) {
        // Check for Terminal taskbar button click first
        if (s_terminalBtnVisible && shell::is_open()) {
            if ((uint32_t)mx >= s_terminalBtnX && (uint32_t)mx < s_terminalBtnX + s_terminalBtnW &&
                (uint32_t)my >= s_terminalBtnY && (uint32_t)my < s_terminalBtnY + s_terminalBtnH) {
                // Toggle minimize state or activate window
                if (s_shellMinimized) {
                    s_shellMinimized = false;
                    s_shellActive = true;
                } else if (s_shellActive) {
                    s_shellMinimized = true;
                    s_shellActive = false;
                } else {
                    s_shellActive = true;
                }
                draw();
                draw_cursor(mx, my);
                return;
            }
        }
        
        // Handle shell window clicks (if shell is open and not minimized)
        if (shell::is_open() && !s_shellMinimized) {
            ShellHitTest hit = hit_test_shell(mx, my);
            
            if (hit == SHELL_HIT_CLOSE_BTN) {
                // Close button clicked
                shell::close();
                s_shellPosX = -1;  // Reset position for next open
                s_shellPosY = -1;
                s_shellW = -1;
                s_shellH = -1;
                s_shellMinimized = false;
                s_shellMaximized = false;
                s_shellActive = true;
                draw();
                draw_cursor(mx, my);
                return;
            }
            
            if (hit == SHELL_HIT_MAX_BTN) {
                // Maximize button clicked - toggle maximize
                if (s_shellMaximized) {
                    // Restore to saved position/size
                    s_shellMaximized = false;
                    s_shellPosX = s_shellSavedX;
                    s_shellPosY = s_shellSavedY;
                    s_shellW = s_shellSavedW;
                    s_shellH = s_shellSavedH;
                } else {
                    // Save current position/size and maximize
                    ShellWindowGeometry g = get_shell_geometry();
                    s_shellSavedX = (int32_t)g.x;
                    s_shellSavedY = (int32_t)g.y;
                    s_shellSavedW = (int32_t)g.w;
                    s_shellSavedH = (int32_t)g.h;
                    s_shellMaximized = true;
                }
                s_shellActive = true;
                draw();
                draw_cursor(mx, my);
                return;
            }
            
            if (hit == SHELL_HIT_MIN_BTN) {
                // Minimize button clicked
                s_shellMinimized = true;
                s_shellActive = false;
                draw();
                draw_cursor(mx, my);
                return;
            }
            
            if (hit == SHELL_HIT_RESIZE_CORNER) {
                // Start resize operation
                s_shellResizing = true;
                s_shellActive = true;
                s_shellResizeStartX = mx;
                s_shellResizeStartY = my;
                ShellWindowGeometry g = get_shell_geometry();
                s_shellResizeStartW = (int32_t)g.w;
                s_shellResizeStartH = (int32_t)g.h;
                draw();
                draw_cursor(mx, my);
                return;
            }
            
            if (hit == SHELL_HIT_TITLEBAR && shell::get_state() != shell::ShellState::Fullscreen && !s_shellMaximized) {
                // Titlebar clicked - start dragging (only when not maximized)
                s_shellDragging = true;
                s_shellActive = true;
                ShellWindowGeometry g = get_shell_geometry();
                s_shellDragOffsetX = mx - (int32_t)g.x;
                s_shellDragOffsetY = my - (int32_t)g.y;
                draw();
                draw_cursor(mx, my);
                return;
            }
            
            if (hit == SHELL_HIT_CLIENT || hit == SHELL_HIT_TITLEBAR) {
                // Click inside shell window - activate it and unfocus compositor windows
                s_shellActive = true;
                compositor::KernelCompositor::setFocus(0);  // Unfocus all compositor windows
                draw();
                draw_cursor(mx, my);
                return;
            }
            
            // Click outside shell window - deactivate it (don't close)
            if (hit == SHELL_HIT_NONE) {
                s_shellActive = false;
                draw();
                draw_cursor(mx, my);
                // Don't return - allow click to be handled by desktop elements
            }
        }
        
        // Handle shutdown dialog clicks first (dialog takes priority)
        if (s_shutdownDialogOpen) {
            int btn = hit_test_shutdown_dialog(mx, my);
            if (btn == 0) {
                // Yes - initiate shutdown
                s_shutdownDialogOpen = false;
                s_notification.title = "Shutdown";
                s_notification.message = "System is shutting down...";
                s_notification.visible = true;
                draw();
                draw_cursor(mx, my);
                
                // Actually perform the shutdown
                perform_shutdown();
                // If we return here, shutdown failed - stay in the system
                return;
            } else if (btn == 1) {
                // No - close dialog
                s_shutdownDialogOpen = false;
                draw();
                draw_cursor(mx, my);
                return;
            }
            // Click outside buttons but inside dialog - do nothing
            // Click outside dialog - close it
            uint32_t dlgX = (s_screenW - kShutdownDlgW) / 2;
            uint32_t dlgY = (s_screenH - kShutdownDlgH) / 2;
            if ((uint32_t)mx < dlgX || (uint32_t)mx >= dlgX + kShutdownDlgW ||
                (uint32_t)my < dlgY || (uint32_t)my >= dlgY + kShutdownDlgH) {
                s_shutdownDialogOpen = false;
                draw();
                draw_cursor(mx, my);
                return;
            }
            // Inside dialog but not on buttons - ignore click
            return;
        }

        // Handle Network Config dialog clicks
        if (s_networkConfigOpen) {
            int btn = hit_test_network_config(mx, my);
            if (btn == -2 || btn == -11) {
                // Close or Cancel
                s_networkConfigOpen = false;
                draw();
                draw_cursor(mx, my);
                return;
            } else if (btn == -10) {
                // OK - apply config
                s_networkConfigOpen = false;
                s_notification.title = "Network";
                s_notification.message = "Configuration applied";
                s_notification.visible = true;
                draw();
                draw_cursor(mx, my);
                return;
            } else if (btn == -20) {
                // DHCP selected
                s_networkConfigUseDHCP = true;
                draw();
                draw_cursor(mx, my);
                return;
            } else if (btn == -21) {
                // Manual selected
                s_networkConfigUseDHCP = false;
                draw();
                draw_cursor(mx, my);
                return;
            }
            return;  // Inside dialog
        }

        // Handle Network Adapters dialog clicks
        if (s_networkAdaptersOpen) {
            int btn = hit_test_network_adapters(mx, my);
            if (btn == -2) {
                // Close
                s_networkAdaptersOpen = false;
                draw();
                draw_cursor(mx, my);
                return;
            } else if (btn == -10) {
                // Properties - open config dialog
                if (s_networkAdapterSelected >= 0 && s_networkAdapters[s_networkAdapterSelected].detected) {
                    s_networkConfigOpen = true;
                    draw();
                    draw_cursor(mx, my);
                    return;
                }
            } else if (btn >= 0 && btn < kNetAdapterCount) {
                s_networkAdapterSelected = btn;
                draw();
                draw_cursor(mx, my);
                return;
            }
            return;
        }

        // Handle Device Manager dialog clicks
        if (s_deviceManagerOpen) {
            int btn = hit_test_device_manager(mx, my);
            if (btn == -2) {
                // Close
                s_deviceManagerOpen = false;
                draw();
                draw_cursor(mx, my);
                return;
            } else if (btn >= 100) {
                // Config button clicked
                s_networkConfigOpen = true;
                draw();
                draw_cursor(mx, my);
                return;
            } else if (btn >= 0 && btn < kDeviceCount) {
                s_deviceManagerSelected = btn;
                draw();
                draw_cursor(mx, my);
                return;
            }
            return;
        }

        // Handle Control Panel dialog clicks
        if (s_controlPanelOpen) {
            int btn = hit_test_control_panel(mx, my);
            if (btn == 2) {
                // Close
                s_controlPanelOpen = false;
                draw();
                draw_cursor(mx, my);
                return;
            } else if (btn == 0) {
                // Device Manager
                s_controlPanelOpen = false;
                s_deviceManagerOpen = true;
                draw();
                draw_cursor(mx, my);
                return;
            } else if (btn == 1) {
                // Network Adapters
                s_controlPanelOpen = false;
                s_networkAdaptersOpen = true;
                draw();
                draw_cursor(mx, my);
                return;
            }
            return;
        }

        // Close context menu on any click
        if (s_rightClickMenuOpen) {
            s_rightClickMenuOpen = false;
            draw();
            draw_cursor(mx, my);
            return;
        }

        // Start button area: x=[4..4+kStartBtnW], y=[taskbarY+4..taskbarY+kTaskbarH-4]
        if ((uint32_t)mx >= 4 && (uint32_t)mx <= 4 + kStartBtnW &&
            (uint32_t)my >= taskbarY + 4 && (uint32_t)my <= taskbarY + kTaskbarH - 4) {
            toggle_start_menu();
            draw();
            draw_cursor(mx, my);
            return;
        }

        // If start menu is open and click is outside it, close it
        if (s_startMenuOpen) {
            // First check if click is on a start menu item
            int leftHit = -1, rightHit = -1;
            hit_test_start_menu(mx, my, leftHit, rightHit);

            if (leftHit >= 0) {
                // Clicked a left-column app
                s_clickedMenuLeft = leftHit;
                s_clickedMenuRight = -1;
                show_start_menu_notification(s_startMenuApps[leftHit].name);
                draw();
                draw_cursor(mx, my);
                return;
            }
            if (rightHit >= 0) {
                // Clicked a right-column item
                s_clickedMenuRight = rightHit;
                s_clickedMenuLeft = -1;
                
                // Handle Control Panel specially (index 5)
                if (rightHit == 5) {
                    s_startMenuOpen = false;
                    s_controlPanelOpen = true;
                    draw();
                    draw_cursor(mx, my);
                    return;
                }
                
                show_start_menu_notification(s_startMenuRight[rightHit].label);
                draw();
                draw_cursor(mx, my);
                return;
            }

            // Check footer buttons (Shutdown, Restart, Sleep)
            FooterButton footerBtn = hit_test_start_menu_footer(mx, my);
            if (footerBtn != FOOTER_NONE) {
                s_startMenuOpen = false;
                s_hoverMenuLeft = -1;
                s_hoverMenuRight = -1;
                switch (footerBtn) {
                    case FOOTER_SHUTDOWN:
                        // Show shutdown confirmation dialog
                        s_shutdownDialogOpen = true;
                        s_shutdownDialogHover = -1;
                        break;
                    case FOOTER_RESTART:
                        s_notification.title = "Restart";
                        s_notification.message = "System is restarting...";
                        s_notification.visible = true;
                        draw();
                        draw_cursor(mx, my);
                        perform_restart();
                        // If we return here, restart failed
                        return;
                    case FOOTER_SLEEP:
                        s_notification.title = "Sleep";
                        s_notification.message = "System entering sleep mode...";
                        s_notification.visible = true;
                        draw();
                        draw_cursor(mx, my);
                        perform_sleep();
                        // System wakes up here after sleep
                        s_notification.title = "Awake";
                        s_notification.message = "System resumed from sleep";
                        draw();
                        draw_cursor(mx, my);
                        return;
                    default:
                        break;
                }
                draw();
                draw_cursor(mx, my);
                return;
            }


            // Click outside start menu items - close it
            s_startMenuOpen = false;
            s_hoverMenuLeft = -1;
            s_hoverMenuRight = -1;
            s_clickedMenuLeft = -1;
            s_clickedMenuRight = -1;
            draw();
            draw_cursor(mx, my);
            return;
        }


        // Desktop icon click - begin potential drag or handle double-click
        int iconIdx = hit_test_icon(mx, my);
        if (iconIdx >= 0) {
            // Check for double-click
            uint32_t now = s_tickCounter;
            if (iconIdx == s_lastClickedIcon && (now - s_lastClickTime) < kDoubleClickMs) {
                // Double-click detected - launch the app
                s_lastClickedIcon = -1;
                s_lastClickTime = 0;
                
                // Use show_icon_notification which handles kernel app launches
                show_icon_notification(iconIdx);
                draw();
                draw_cursor(mx, my);
                return;
            }
            
            // First click - record for double-click detection
            s_lastClickedIcon = iconIdx;
            s_lastClickTime = now;
            
            s_selectedIcon = iconIdx;
            // Start drag tracking (actual drag begins after threshold)
            s_dragging = true;
            s_dragStarted = false;
            s_dragIconIndex = iconIdx;
            s_dragStartMouseX = mx;
            s_dragStartMouseY = my;
            s_dragOffsetX = mx - s_iconPosX[iconIdx];
            s_dragOffsetY = my - s_iconPosY[iconIdx];
            draw();
            draw_cursor(mx, my);
            return;
        }


        // Click on empty desktop area: deselect icon
        if ((uint32_t)my < taskbarY) {
            if (s_selectedIcon >= 0) {
                s_selectedIcon = -1;
                draw();
                draw_cursor(mx, my);
                return;
            }
        }

        // Notification dismiss: check if clicking the notification toast
        if (s_notification.visible) {
            uint32_t toastW = 280;
            uint32_t toastH = 64;
            uint32_t toastX = s_screenW - toastW - 16;
            uint32_t toastY = taskbarY - toastH - 12;
            if ((uint32_t)mx >= toastX && (uint32_t)mx <= toastX + toastW &&
                (uint32_t)my >= toastY && (uint32_t)my <= toastY + toastH) {
                dismiss_notification();
                draw();
                draw_cursor(mx, my);
                return;
            }
        }
    }

    // Right button click - show context menu on desktop area
    if (pressed & 0x02) {
        if ((uint32_t)my < taskbarY) {
            show_context_menu((uint32_t)mx, (uint32_t)my);
            draw();
            draw_cursor(mx, my);
            return;
        }
    }

    // Shutdown dialog hover tracking
    if (s_shutdownDialogOpen) {
        int newHover = hit_test_shutdown_dialog(mx, my);
        if (newHover != s_shutdownDialogHover) {
            s_shutdownDialogHover = newHover;
            draw();
            draw_cursor(mx, my);
            return;
        }
    }

    // Control Panel hover tracking
    if (s_controlPanelOpen) {
        int newHover = hit_test_control_panel(mx, my);
        if (newHover != 2 && newHover != s_controlPanelHover) {  // Don't track close button as hover
            s_controlPanelHover = newHover;
            draw();
            draw_cursor(mx, my);
            return;
        }
    }

    // Network Adapters hover tracking
    if (s_networkAdaptersOpen) {
        int newHover = hit_test_network_adapters(mx, my);
        if (newHover >= 0 && newHover < kNetAdapterCount && newHover != s_networkAdapterHover) {
            s_networkAdapterHover = newHover;
            draw();
            draw_cursor(mx, my);
            return;
        } else if (newHover < 0 && s_networkAdapterHover >= 0) {
            s_networkAdapterHover = -1;
            draw();
            draw_cursor(mx, my);
            return;
        }
    }

    // Network Config button hover tracking
    if (s_networkConfigOpen) {
        int newHover = -1;
        int hit = hit_test_network_config(mx, my);
        if (hit == -10) newHover = 0;  // OK
        else if (hit == -11) newHover = 1;  // Cancel
        if (newHover != s_networkConfigBtnHover) {
            s_networkConfigBtnHover = newHover;
            draw();
            draw_cursor(mx, my);
            return;
        }
    }

    // Start menu hover tracking (on any mouse move, not just clicks)
    if (s_startMenuOpen) {
        int newHoverLeft = -1, newHoverRight = -1;
        hit_test_start_menu(mx, my, newHoverLeft, newHoverRight);

        if (newHoverLeft != s_hoverMenuLeft || newHoverRight != s_hoverMenuRight) {
            s_hoverMenuLeft = newHoverLeft;
            s_hoverMenuRight = newHoverRight;
            draw();
            draw_cursor(mx, my);
            return;
        }
    }

    // Always redraw cursor at new position
    draw_cursor(mx, my);
}

} // namespace desktop
} // namespace kernel

// External function for shell to call test mode
void desktop_run_test_mode()
{
    kernel::desktop::run_test_mode();
}
