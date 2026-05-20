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
#include "include/kernel/desktop_font.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/startmenubutton_img.h"
#include "include/kernel/desktop_icon_theme_flat.h"
#include "include/kernel/shell.h"
#include "include/kernel/kernel_app.h"
#include "include/kernel/kernel_apps.h"
#include "include/kernel/kernel_compositor.h"
#include "include/kernel/kernel_ipc.h"
#include "include/kernel/ps2keyboard.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"
#include "include/kernel/desktop_capabilities.h"
#include "include/kernel/nic.h"
#include "include/kernel/usb_net.h"
#include "include/kernel/ipv4.h"
#include "include/kernel/pci_audio.h"
#include "include/kernel/usb_audio.h"
#include "include/kernel/time.h"
#include "include/kernel/ramdisk.h"
#include "include/kernel/block_device.h"
#if !defined(GXOS_BARE_METAL)
#include "../../icon_theme_manager.h"
#endif

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

static bool desktop_starts_with(const char* value, const char* prefix)
{
    if (!value || !prefix) return false;
    while (*prefix) {
        if (*value != *prefix) return false;
        ++value;
        ++prefix;
    }
    return true;
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

static bool desktop_str_eq(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int desktop_strlen(const char* value)
{
    int len = 0;
    if (!value) return 0;
    while (value[len]) ++len;
    return len;
}

// ============================================================
// Desktop state
// ============================================================


static bool s_initialized = false;
static bool s_startMenuOpen = false;
static bool s_rightClickMenuOpen = false;
static uint32_t s_rightClickX = 0;
static uint32_t s_rightClickY = 0;
static int32_t s_mouseX = 0;
static int32_t s_mouseY = 0;
static int s_rightClickHover = -1;
static uint32_t s_screenW = 0;
static uint32_t s_screenH = 0;
static volatile bool s_needsRedraw = false;

// Shutdown dialog state
static bool s_shutdownDialogOpen = false;
static int s_shutdownDialogHover = -1;  // 0 = Yes, 1 = No, -1 = none

// App model demo dialog state
static bool s_appModelDialogOpen = false;
static int s_appModelDialogHover = -1;  // 0 = Close, -1 = none
static int s_appModelSelectedIndex = 0;
static const char* s_appModelStatus = "Ready. Use Up/Down, 1-4, Enter, I, S, Escape.";

// Control Panel window state
static bool s_controlPanelOpen = false;
static int s_controlPanelHover = -1;  // 0 = Display, 1 = Device Manager, 2 = Network Adapters, -1 = none

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
static const uint32_t kTaskbarSnapThreshold = 48;
static const uint32_t kStartBtnW = 100;
static const uint32_t kSearchBoxW = 160;
static const uint32_t kSearchBoxH = 24;
static const uint32_t kShowDesktopW = 6;
static const uint32_t kStartMenuW = 420;
static const uint32_t kStartMenuRightColW = 160;
static const uint32_t kIconSize = 48;
static const uint32_t kIconCellW = 80;
static const uint32_t kIconCellH = 76;
static const uint32_t kIconMargin = 24;
static bool s_enableDesktopIcons = true;
static uint32_t s_desktopIconSize = kIconSize;
static const char* s_desktopIconTheme = "Flat";
static const uint32_t kTrayIconSize = 16;
static const uint32_t kTrayIconGap = 6;
static const uint32_t kTaskbarBtnMaxW = 150;
static const uint32_t kTaskbarBtnH = 28;
static const uint32_t kTaskbarBtnGap = 4;
static bool s_enableStartMenuIcons = true;
static uint32_t s_startMenuIconSize = 16;
static const char* s_startMenuIconTheme = "Flat";

struct DesktopRect {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
};

enum class TaskbarDockPosition : uint8_t {
    Bottom,
    Top,
    Left,
    Right
};

static TaskbarDockPosition s_taskbarDockPosition = TaskbarDockPosition::Bottom;
static bool s_taskbarDragging = false;

static bool is_vertical_taskbar(TaskbarDockPosition position)
{
    return position == TaskbarDockPosition::Left || position == TaskbarDockPosition::Right;
}

static DesktopRect get_taskbar_rect(uint32_t screenW, uint32_t screenH, TaskbarDockPosition position)
{
    DesktopRect r;
    switch (position) {
        case TaskbarDockPosition::Top:
            r = {0, 0, screenW, kTaskbarH};
            break;
        case TaskbarDockPosition::Left:
            r = {0, 0, kTaskbarH, screenH};
            break;
        case TaskbarDockPosition::Right:
            r = {screenW > kTaskbarH ? screenW - kTaskbarH : 0, 0, kTaskbarH, screenH};
            break;
        case TaskbarDockPosition::Bottom:
        default:
            r = {0, screenH > kTaskbarH ? screenH - kTaskbarH : 0, screenW, kTaskbarH};
            break;
    }
    return r;
}

static DesktopRect get_desktop_work_area(uint32_t screenW, uint32_t screenH, TaskbarDockPosition position)
{
    switch (position) {
        case TaskbarDockPosition::Top:
            return {0, kTaskbarH, screenW, screenH > kTaskbarH ? screenH - kTaskbarH : 0};
        case TaskbarDockPosition::Left:
            return {kTaskbarH, 0, screenW > kTaskbarH ? screenW - kTaskbarH : 0, screenH};
        case TaskbarDockPosition::Right:
            return {0, 0, screenW > kTaskbarH ? screenW - kTaskbarH : 0, screenH};
        case TaskbarDockPosition::Bottom:
        default:
            return {0, 0, screenW, screenH > kTaskbarH ? screenH - kTaskbarH : 0};
    }
}

static bool point_in_rect(int32_t mx, int32_t my, const DesktopRect& r)
{
    return mx >= 0 && my >= 0 &&
           (uint32_t)mx >= r.x && (uint32_t)mx < r.x + r.w &&
           (uint32_t)my >= r.y && (uint32_t)my < r.y + r.h;
}

static DesktopRect get_current_taskbar_rect()
{
    return get_taskbar_rect(s_screenW, s_screenH, s_taskbarDockPosition);
}

static DesktopRect get_current_work_area()
{
    return get_desktop_work_area(s_screenW, s_screenH, s_taskbarDockPosition);
}

static DesktopRect get_start_button_rect()
{
    DesktopRect tb = get_current_taskbar_rect();
    if (is_vertical_taskbar(s_taskbarDockPosition)) {
        return {tb.x + 4, tb.y + 4, tb.w > 8 ? tb.w - 8 : tb.w, kTaskbarH - 8};
    }

    return {tb.x + 4, tb.y + 4, kStartBtnW, tb.h > 8 ? tb.h - 8 : tb.h};
}

static DesktopRect get_show_desktop_rect()
{
    DesktopRect tb = get_current_taskbar_rect();
    if (is_vertical_taskbar(s_taskbarDockPosition)) {
        return {tb.x, tb.y + tb.h - kShowDesktopW, tb.w, kShowDesktopW};
    }

    return {tb.x + tb.w - kShowDesktopW, tb.y, kShowDesktopW, tb.h};
}

static bool is_point_in_work_area(int32_t mx, int32_t my)
{
    return point_in_rect(mx, my, get_current_work_area());
}

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static bool is_near_screen_edge(int32_t mouseX, int32_t mouseY, uint32_t screenW, uint32_t screenH)
{
    int32_t right = (int32_t)screenW - 1 - mouseX;
    int32_t bottom = (int32_t)screenH - 1 - mouseY;
    return mouseX <= (int32_t)kTaskbarSnapThreshold ||
           mouseY <= (int32_t)kTaskbarSnapThreshold ||
           right <= (int32_t)kTaskbarSnapThreshold ||
           bottom <= (int32_t)kTaskbarSnapThreshold;
}

static TaskbarDockPosition get_nearest_dock_edge(int32_t mouseX, int32_t mouseY, uint32_t screenW, uint32_t screenH)
{
    int32_t left = mouseX < 0 ? 0 : mouseX;
    int32_t top = mouseY < 0 ? 0 : mouseY;
    int32_t right = (int32_t)screenW - 1 - mouseX;
    int32_t bottom = (int32_t)screenH - 1 - mouseY;
    if (right < 0) right = 0;
    if (bottom < 0) bottom = 0;

    int32_t best = bottom;
    TaskbarDockPosition pos = TaskbarDockPosition::Bottom;
    if (top < best) { best = top; pos = TaskbarDockPosition::Top; }
    if (left < best) { best = left; pos = TaskbarDockPosition::Left; }
    if (right < best) { pos = TaskbarDockPosition::Right; }
    return pos;
}

static void apply_taskbar_layout()
{
    DesktopRect work = get_current_work_area();
    DesktopRect tb = get_current_taskbar_rect();
    bool vertical = is_vertical_taskbar(s_taskbarDockPosition);
    DesktopRect start = get_start_button_rect();
    uint32_t buttonStartX = vertical ? tb.x + 4 : start.x + start.w + 8;
    uint32_t buttonStartY = vertical ? start.y + start.h + 8 : tb.y + 6;
    compositor::KernelCompositor::setWorkArea(work.x, work.y, work.w, work.h);
    compositor::TaskbarManager::setLayout(tb.x, tb.y, tb.w, tb.h, vertical, buttonStartX, buttonStartY);
}

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
    {"Trash",       0xFF9098A4, true, false, -1, -1},  // gray, pinned
    {"DisplayOptions", 0xFF606878, true, false, -1, -1}, // display options
    {"guideXOS Navigator", 0xFF4678BE, true, false, -1, -1}, // navigator
    {"Paint",       0xFFC87830, false, true, -1, -1},  // orange, recent
    {"Clock",       0xFF4690C8, false, true, -1, -1},  // blue, recent
    {"TaskManager", 0xFFB44646, true, false, -1, -1},  // red, pinned (matches registered app name)
    {"Files",       0xFFC8B43C, true, false, -1, -1},  // yellow, pinned
    {"ImgViewer",   0xFFC87830, false, false, -1, -1}, // orange
    {"AppModel",    0xFF5587D2, true, false, -1, -1},  // blue, app model demo
};
static const int kDesktopIconCount = sizeof(s_desktopIcons) / sizeof(s_desktopIcons[0]);
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
    {"Trash",       true,  false, 0xFF9098A4},  // pinned
    {"TaskManager", true,  false, 0xFFB44646},  // pinned
    {"DiskManager", true,  false, 0xFFB48C46},  // pinned (orange-brown for disk)
    {"DisplayOptions", true, false, 0xFF606878}, // display options
    {"guideXOS Navigator", true, false, 0xFF4678BE}, // pinned navigator
    {"HDInstaller", true,  false, 0xFFB48C46},  // pinned (orange-brown for installer)
    {"AppModel",    true,  false, 0xFF5587D2},  // pinned app model demo entry
    {"Paint",       false, true,  0xFFC87830},  // recent
    {"Clock",       false, true,  0xFF4690C8},  // recent
    {"Files",       false, true,  0xFFC8B43C},  // recent
    {"ImgViewer",   false, false, 0xFFC87830},  // not shown by default
};
static const int kStartMenuAppCount = sizeof(s_startMenuApps) / sizeof(s_startMenuApps[0]);
static const int kMaxStartMenuRecent = 5;  // Max recent apps in start menu

// All Programs alphabetically sorted list (for "All Programs" view)
static const char* s_allProgramsList[] = {
    "Calculator",
    "Clock",
    "Console",
    "ControlPanel",
    "DiskManager",
    "Files",
    "guideXOS Navigator",
    "HDInstaller",
    "ImgViewer",
    "AppModel",
    "Notepad",
    "Paint",
    "TaskManager",
    "Trash",
};
static const int kAllProgramsCount = sizeof(s_allProgramsList) / sizeof(s_allProgramsList[0]);

struct AppModelDemoRow {
    const char* displayName;
    const char* appId;
    const char* type;
    const char* status;
};

static const AppModelDemoRow s_appModelDemoRows[] = {
    {"Hello World", "gxos.sample.helloworld", "native sample", "unsupported in bare-metal / native disabled"},
    {"Resource Viewer", "gxos.sample.resourceviewer", "native sample", "unsupported in bare-metal / native disabled"},
    {"Native App Debug Viewer", "gxos.builtin.nativeappdebugviewer", "debug tool", "hosted compositor only"},
    {"App Model Demo", "AppModel", "built-in diagnostic", "launchable viewer"},
};
static const int kAppModelDemoRowCount = sizeof(s_appModelDemoRows) / sizeof(s_appModelDemoRows[0]);

static const char* kAppModelViewerMarker = "APP MODEL VIEWER v3 - OLD TOAST REMOVED";

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
    {"", 0, false}, // Placeholder; kTaskbarEntryCount keeps this disabled.
};
static const int kTaskbarEntryCount = 0;

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
static const uint32_t kContextMenuPad = 2;

// Notification toast
struct NotificationToast {
    const char* title;
    const char* message;
    bool visible;
    uint32_t showTime;  // Tick counter when notification was shown
};

static NotificationToast s_notification = {
    "Welcome to guideXOS",
    "System started successfully",
    true,
    0
};

// ============================================================
// Wallpaper Configuration
// ============================================================

enum class WallpaperType {
    Gradient,      // Gradient from top to bottom
    SolidColor,    // Single solid color
    Grid,          // Grid pattern overlay
    Custom,        // Custom image (placeholder for future)
    BuiltIn        // Built-in wallpaper palette/preview for bare-metal
};

enum class BackgroundKind {
    Image,
    Gradient,
    SolidColor
};

enum class WallpaperScaleMode {
    Fill,
    Fit,
    Stretch,
    Center,
    Tile
};

struct WallpaperConfig {
    WallpaperType type;
    uint32_t topColor;       // For gradient or solid color
    uint32_t bottomColor;    // For gradient
    bool showBranding;       // Show "guideXOS" branding
    bool showGrid;           // Show subtle grid overlay
    uint32_t gridColor;      // Grid line color
    uint32_t gridSpacing;    // Grid spacing in pixels
    const char* wallpaperId; // Stable built-in wallpaper id
    WallpaperScaleMode scaleMode; // Image scaling mode
    
    WallpaperConfig() 
        : type(WallpaperType::BuiltIn),
          topColor(0xFF142850),    // Dark blue top
          bottomColor(0xFF0F121C), // Darker blue bottom
          showBranding(false),
          showGrid(false),
          gridColor(0xFF192337),
          gridSpacing(100),
          wallpaperId("legacy_blue_flower"),
          scaleMode(WallpaperScaleMode::Fill) {}
};

static WallpaperConfig s_wallpaperConfig;

struct BuiltInWallpaperPalette {
    const char* id;
    const char* displayName;
    const char* fullImagePath;
    const char* thumbnailPath;
    uint32_t topColor;
    uint32_t bottomColor;
    uint32_t accentColor;
};

static const BuiltInWallpaperPalette s_builtInWallpapers[] = {
    {"legacy_blue_flower", "Blue Flower", "/system/wall/blueflwr.gxi", "/system/wall/bluef_t.gxi", 0xFF061638, 0xFF123B84, 0xFF2568D8},
    {"legacy_dinos", "Dinos", "/system/wall/dinos.gxi", "/system/wall/dinos_t.gxi", 0xFF476020, 0xFFB8A05E, 0xFF70A048},
    {"legacy_flower", "Flower", "/system/wall/flower.gxi", "/system/wall/flower_t.gxi", 0xFF103C50, 0xFF51AFC2, 0xFF88E0F0},
    {"legacy_guidexos_space", "guideXOS Space", "/system/wall/gspace.gxi", "/system/wall/gspace_t.gxi", 0xFF030713, 0xFF102A70, 0xFF2F6BDC},
    {"legacy_red_flower", "Red Flower", "/system/wall/redflwr.gxi", "/system/wall/redf_t.gxi", 0xFF190202, 0xFF7D1010, 0xFFD82020},
    {"legacy_ameoba", "Ameoba", "/system/wall/ameoba.gxi", "/system/wall/ameoba_t.gxi", 0xFF071044, 0xFF501090, 0xFF7E2DDD},
    {"legacy_ameobagx", "Ameoba GX", "/system/wall/ameobagx.gxi", "/system/wall/amebgx_t.gxi", 0xFF13051F, 0xFF68289A, 0xFFD04DF0},
    {"legacy_tron_porsche", "Tron Porsche", "/system/wall/tronpor.gxi", "/system/wall/tronp_t.gxi", 0xFF031820, 0xFF0B7485, 0xFF20E0F0},
    {"legacy_wallpaper2", "Wallpaper 2", "/system/wall/wallp2.gxi", "/system/wall/wallp2_t.gxi", 0xFF100E35, 0xFF8C145F, 0xFFFF52B0},
};
static const int kBuiltInWallpaperCount = sizeof(s_builtInWallpapers) / sizeof(s_builtInWallpapers[0]);

struct BuiltInGradientPalette {
    const char* id;
    const char* displayName;
    uint32_t topColor;
    uint32_t bottomColor;
    uint32_t accentColor;
};

static const BuiltInGradientPalette s_builtInGradients[] = {
    {"gradient_midnight", "Midnight", 0xFF142850, 0xFF0F121C, 0xFF192337},
    {"gradient_ocean", "Ocean", 0xFF063B5C, 0xFF061522, 0xFF1496B8},
    {"gradient_aurora", "Aurora", 0xFF0B2C35, 0xFF251046, 0xFF21C78A},
    {"gradient_violet", "Violet", 0xFF26104A, 0xFF0D0B18, 0xFF8A52E8},
    {"gradient_sunset", "Sunset", 0xFF5E1B45, 0xFF17101E, 0xFFE06A55},
    {"gradient_forest", "Forest", 0xFF123B2B, 0xFF071711, 0xFF5E9C50},
    {"gradient_ember", "Ember", 0xFF45170F, 0xFF120B09, 0xFFD46A33},
    {"gradient_graphite", "Graphite", 0xFF333946, 0xFF111318, 0xFF7E8796},
};
static const int kBuiltInGradientCount = sizeof(s_builtInGradients) / sizeof(s_builtInGradients[0]);

struct WallpaperPackEntry {
    const char* id;
    const char* path;
    bool thumbnail;
};

struct GximgHeader {
    char magic[8];
    uint32_t width;
    uint32_t height;
    uint32_t format;
};

struct WallpaperImageCache {
    uint32_t* pixels;
    uint32_t width;
    uint32_t height;
    bool loadAttempted;
    bool loadSucceeded;
};

static WallpaperPackEntry s_wallpaperPackEntries[kBuiltInWallpaperCount * 2];
static int s_wallpaperPackEntryCount = 0;
static bool s_wallpaperPackMounted = false;
static WallpaperImageCache s_wallpaperThumbCache[kBuiltInWallpaperCount];
static WallpaperImageCache s_wallpaperFullCache{};
static const char* s_wallpaperFullCacheId = nullptr;
static const uint32_t kWallpaperFullPixelCapacity = 800u * 600u;
static const uint32_t kWallpaperThumbPixelCapacity = 256u * 128u;
static uint32_t s_wallpaperFullPixels[kWallpaperFullPixelCapacity];
static uint32_t s_wallpaperThumbPixels[kBuiltInWallpaperCount][kWallpaperThumbPixelCapacity];
static uint32_t s_wallpaperLoadScratch[(sizeof(GximgHeader) + (kWallpaperFullPixelCapacity * sizeof(uint32_t)) + 3u) / 4u];

static void reset_wallpaper_image_caches()
{
    for (int i = 0; i < kBuiltInWallpaperCount; ++i) {
        s_wallpaperThumbCache[i].pixels = nullptr;
        s_wallpaperThumbCache[i].width = 0;
        s_wallpaperThumbCache[i].height = 0;
        s_wallpaperThumbCache[i].loadAttempted = false;
        s_wallpaperThumbCache[i].loadSucceeded = false;
    }

    s_wallpaperFullCache.pixels = nullptr;
    s_wallpaperFullCache.width = 0;
    s_wallpaperFullCache.height = 0;
    s_wallpaperFullCache.loadAttempted = false;
    s_wallpaperFullCache.loadSucceeded = false;
    s_wallpaperFullCacheId = nullptr;
}

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
    
    DateTime() : hour(0), minute(0), second(0), day(0), month(0), year(0) {}
};

static DateTime s_currentTime;
static bool s_timeAvailable = false;
static int s_lastRenderedClockMinute = -1;

static void copy_datetime_from_time_service(const time::DateTime& src)
{
    s_currentTime.year = src.year;
    s_currentTime.month = src.month;
    s_currentTime.day = src.day;
    s_currentTime.hour = src.hour;
    s_currentTime.minute = src.minute;
    s_currentTime.second = src.second;
}

// Simple helper to format time string "HH:MM"
static void format_time_string(char* buffer, int bufSize)
{
    if (bufSize < 6) return;  // Need at least "HH:MM\0"

    buffer[0] = '0' + (s_currentTime.hour / 10);
    buffer[1] = '0' + (s_currentTime.hour % 10);
    buffer[2] = ':';
    buffer[3] = '0' + (s_currentTime.minute / 10);
    buffer[4] = '0' + (s_currentTime.minute % 10);
    buffer[5] = '\0';
}

// Simple helper to format date string "MM/DD/YYYY"
static void format_date_string(char* buffer, int bufSize)
{
    if (bufSize < 11) return;  // Need space for "MM/DD/YYYY\0"

    buffer[0] = '0' + (s_currentTime.month / 10);
    buffer[1] = '0' + (s_currentTime.month % 10);
    buffer[2] = '/';
    buffer[3] = '0' + (s_currentTime.day / 10);
    buffer[4] = '0' + (s_currentTime.day % 10);
    buffer[5] = '/';
    buffer[6] = '0' + (s_currentTime.year / 1000);
    buffer[7] = '0' + ((s_currentTime.year / 100) % 10);
    buffer[8] = '0' + ((s_currentTime.year / 10) % 10);
    buffer[9] = '0' + (s_currentTime.year % 10);
    buffer[10] = '\0';
}

// Update time (called from tick, increments by 1 second)
static void update_time()
{
    if (!s_timeAvailable) return;

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

// Initialize time from system clock
static void init_time()
{
    time::DateTime now{};
    s_timeAvailable = time::get_current_datetime(now);
    if (s_timeAvailable) {
        copy_datetime_from_time_service(now);
        s_lastRenderedClockMinute = s_currentTime.minute;
    }
}

// Per-icon positions (mutable, set to grid layout on init)
static int32_t s_iconPosX[kDesktopIconCount];
static int32_t s_iconPosY[kDesktopIconCount];
static bool    s_iconPositionsInitialized = false;

// Selected desktop icon (-1 = none)
static int s_selectedIcon = -1;
static bool s_selectedIconIds[kDesktopIconCount] = {false};
static bool s_selectionBaseIconIds[kDesktopIconCount] = {false};
static int s_lastSelectedIconId = -1;
static int s_focusedSelectedIconId = -1;

// Desktop marquee selection state
static bool s_selectionDragPending = false;
static bool s_selectionDragActive = false;
static bool s_selectionDragAdditive = false;
static int32_t s_selectionStartX = 0;
static int32_t s_selectionStartY = 0;
static int32_t s_selectionCurrentX = 0;
static int32_t s_selectionCurrentY = 0;

#if !defined(GXOS_BARE_METAL)
static gxos::gui::ImagePtr s_desktopIconImageCache[kDesktopIconCount];
static bool s_desktopIconLoadAttempted[kDesktopIconCount] = {false};
static bool s_desktopIconMissingLogged[kDesktopIconCount] = {false};
static uint32_t s_cachedDesktopIconSize = 0;
#endif

// Icon management helpers
static int s_visibleIconCount = 0;  // Count of icons to display (pinned + recent)
static int s_visibleIconIndices[kDesktopIconCount]; // Indices of visible icons in display order

// Drag state for icon repositioning
static bool  s_dragging = false;
static int   s_dragIconIndex = -1;
static int32_t s_dragStartMouseX = 0;
static int32_t s_dragStartMouseY = 0;
static int32_t s_dragCurrentX = 0;  // current mouse X during drag
static int32_t s_dragCurrentY = 0;  // current mouse Y during drag
static int32_t s_dragOriginalIconX[kDesktopIconCount];
static int32_t s_dragOriginalIconY[kDesktopIconCount];
static bool    s_dragSelectedIcons[kDesktopIconCount];
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
static void sync_selected_icon_after_layout();
static bool is_display_icon_selected(int displayIndex);
static void ClearDesktopIconSelection();
static void SelectDesktopIcon(int displayIndex, bool additive);
static void ToggleDesktopIconSelection(int displayIndex);
static void SelectDesktopIconRange(int startDisplayIndex, int endDisplayIndex);
static int GetSelectedDesktopIconIndices(int* outIndices, int maxIndices);
static int HitTestDesktopIcon(int32_t mx, int32_t my);
static void SelectIconsInRectangle(int32_t left, int32_t top, int32_t right, int32_t bottom, bool additive);

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

static bool draw_argb_icon_buffer(const uint32_t* pixels, uint32_t srcW, uint32_t srcH, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

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
static bool    s_showDesktopActive = false;
static bool    s_showDesktopHidShell = false;
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
static void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
static void draw_shell_window();
static void forget_cursor_save();
static void get_context_menu_geometry(uint32_t& menuX, uint32_t& menuY, uint32_t& menuH);
static int find_nearest_icon_in_direction(int currentIcon, int direction);
static void show_icon_notification(int iconIndex);
static void toggle_show_desktop();

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

// ============================================================
// App Model Demo Viewer
// ============================================================

static const uint32_t kAppModelDlgW = 720;
static const uint32_t kAppModelDlgH = 340;
static const uint32_t kAppModelBtnW = 92;
static const uint32_t kAppModelBtnH = 28;

static uint32_t app_model_dialog_width()
{
    return (s_screenW > kAppModelDlgW + 24) ? kAppModelDlgW : (s_screenW > 24 ? s_screenW - 24 : s_screenW);
}

static uint32_t app_model_dialog_height()
{
    return (s_screenH > kAppModelDlgH + 16) ? kAppModelDlgH : (s_screenH > 16 ? s_screenH - 16 : s_screenH);
}

static uint32_t app_model_dialog_x()
{
    uint32_t dlgW = app_model_dialog_width();
    return (s_screenW > dlgW) ? (s_screenW - dlgW) / 2 : 0;
}

static uint32_t app_model_dialog_y()
{
    uint32_t dlgH = app_model_dialog_height();
    return (s_screenH > dlgH) ? (s_screenH - dlgH) / 2 : 0;
}

static void open_app_model_viewer()
{
    s_appModelDialogOpen = true;
    s_appModelDialogHover = -1;
    s_appModelSelectedIndex = 0;
    s_notification.visible = false;
}

static void draw_app_model_dialog()
{
    if (!s_appModelDialogOpen) return;

    uint32_t dlgW = app_model_dialog_width();
    uint32_t dlgH = app_model_dialog_height();
    uint32_t dlgX = app_model_dialog_x();
    uint32_t dlgY = app_model_dialog_y();

    framebuffer::fill_rect(dlgX + 4, dlgY + 4, dlgW, dlgH, rgb(10, 10, 15));
    framebuffer::fill_rect(dlgX, dlgY, dlgW, dlgH, rgb(35, 35, 45));
    draw_rect(dlgX, dlgY, dlgW, dlgH, rgb(80, 110, 170));

    framebuffer::fill_rect(dlgX + 1, dlgY + 1, dlgW - 2, 30, rgb(50, 70, 110));
    draw_text(dlgX + 12, dlgY + 9, "App Model Demo", rgb(230, 235, 250), 1);

    uint32_t closeX = dlgX + dlgW - 26;
    uint32_t closeY = dlgY + 5;
    framebuffer::fill_rect(closeX, closeY, 20, 20, s_appModelDialogHover == 0 ? rgb(210, 70, 70) : rgb(160, 55, 55));
    draw_rect(closeX, closeY, 20, 20, rgb(220, 100, 100));
    draw_text(closeX + 6, closeY + 5, "x", rgb(250, 230, 230), 1);

    uint32_t contentX = dlgX + 16;
    uint32_t y = dlgY + 46;
    draw_text(contentX, y, kAppModelViewerMarker, rgb(255, 230, 120), 1);
    y += 24;
    draw_text(contentX, y, "Runtime:", rgb(220, 225, 240), 1);
    y += 18;
    draw_text(contentX + 12, y, "Mode: bare-metal", rgb(200, 210, 230), 1);
    y += 18;
    draw_text(contentX + 12, y, "Native execution: unsupported", rgb(200, 210, 230), 1);
    y += 18;
    draw_text(contentX + 12, y, "Discovered apps: 4 (temporary bare-metal list)", rgb(200, 210, 230), 1);
    y += 28;

    draw_text(contentX, y, "Apps:", rgb(220, 225, 240), 1);
    y += 20;
    for (int i = 0; i < kAppModelDemoRowCount; ++i) {
        char line[128];
        int pos = 0;
        line[pos++] = static_cast<char>('1' + i);
        line[pos++] = '.';
        line[pos++] = ' ';
        const char* name = s_appModelDemoRows[i].displayName;
        while (*name && pos < 126) line[pos++] = *name++;
        line[pos++] = ' ';
        line[pos++] = '-';
        line[pos++] = ' ';
        const char* status = s_appModelDemoRows[i].status;
        while (*status && pos < 126) line[pos++] = *status++;
        line[pos] = '\0';
        draw_text(contentX + 12, y, line, i == 3 ? rgb(255, 230, 120) : rgb(205, 210, 225), 1);
        y += 18;
    }

    y += 10;
    draw_text(contentX, y, "Close: button or Escape. Launch/inspect disabled in bare-metal for this pass.", rgb(190, 200, 220), 1);

    uint32_t btnX = dlgX + dlgW - kAppModelBtnW - 18;
    uint32_t btnY = dlgY + dlgH - kAppModelBtnH - 14;
    uint32_t btnBg = (s_appModelDialogHover == 0) ? rgb(70, 120, 180) : rgb(50, 90, 150);
    framebuffer::fill_rect(btnX, btnY, kAppModelBtnW, kAppModelBtnH, btnBg);
    draw_rect(btnX, btnY, kAppModelBtnW, kAppModelBtnH, rgb(100, 140, 200));
    draw_text_centered(btnX, btnY, kAppModelBtnW, kAppModelBtnH, "Close", rgb(220, 230, 250), 1);
}

static int hit_test_app_model_dialog(int32_t mx, int32_t my)
{
    if (!s_appModelDialogOpen) return -1;

    uint32_t dlgW = app_model_dialog_width();
    uint32_t dlgH = app_model_dialog_height();
    uint32_t dlgX = app_model_dialog_x();
    uint32_t dlgY = app_model_dialog_y();

    uint32_t closeX = dlgX + dlgW - 26;
    uint32_t closeY = dlgY + 5;
    if ((uint32_t)mx >= closeX && (uint32_t)mx < closeX + 20 &&
        (uint32_t)my >= closeY && (uint32_t)my < closeY + 20) {
        return 0;
    }

    uint32_t btnX = dlgX + dlgW - kAppModelBtnW - 18;
    uint32_t btnY = dlgY + dlgH - kAppModelBtnH - 14;
    if ((uint32_t)mx >= btnX && (uint32_t)mx < btnX + kAppModelBtnW &&
        (uint32_t)my >= btnY && (uint32_t)my < btnY + kAppModelBtnH) {
        return 0;
    }

    return -1;
}

// Rebuild visible icon list based on pinned and recent status
static void refresh_desktop_icons()
{
    s_visibleIconCount = 0;
    
    // First add all pinned icons
    for (int i = 0; i < kDesktopIconCount && s_visibleIconCount < kDesktopIconCount; i++) {
        if (s_desktopIcons[i].pinned) {
            s_visibleIconIndices[s_visibleIconCount++] = i;
        }
    }
    
    // Then add recent icons (up to limit)
    int recentCount = 0;
    for (int i = 0; i < kDesktopIconCount && s_visibleIconCount < kDesktopIconCount; i++) {
        if (s_desktopIcons[i].recent && !s_desktopIcons[i].pinned) {
            if (recentCount < kMaxRecentApps) {
                s_visibleIconIndices[s_visibleIconCount++] = i;
                recentCount++;
            }
        }
    }

    sync_selected_icon_after_layout();
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

static bool is_ctrl_modifier_down()
{
    return ps2keyboard::is_ctrl_down();
}

static bool is_shift_modifier_down()
{
    return ps2keyboard::is_shift_down();
}

static void log_selection_change(const char* action)
{
    serial::puts("[desktop] icon selection ");
    serial::puts(action);
    serial::puts(" count=0x");
    serial::put_hex32((uint32_t)GetSelectedDesktopIconIndices(nullptr, 0));
    serial::puts("\n");
}

static int display_index_for_icon_id(int iconId)
{
    if (iconId < 0 || iconId >= kDesktopIconCount) return -1;
    for (int displayIdx = 0; displayIdx < s_visibleIconCount; displayIdx++) {
        if (s_visibleIconIndices[displayIdx] == iconId) return displayIdx;
    }
    return -1;
}

static void sync_selected_icon_after_layout()
{
    int focusedDisplayIndex = display_index_for_icon_id(s_focusedSelectedIconId);
    if (focusedDisplayIndex >= 0) {
        s_selectedIcon = focusedDisplayIndex;
        return;
    }

    s_selectedIcon = -1;
    for (int displayIdx = 0; displayIdx < s_visibleIconCount; displayIdx++) {
        int iconId = s_visibleIconIndices[displayIdx];
        if (iconId >= 0 && iconId < kDesktopIconCount && s_selectedIconIds[iconId]) {
            s_selectedIcon = displayIdx;
            s_focusedSelectedIconId = iconId;
            return;
        }
    }
}

static bool is_display_icon_selected(int displayIndex)
{
    if (displayIndex < 0 || displayIndex >= s_visibleIconCount) return false;
    int iconId = s_visibleIconIndices[displayIndex];
    return iconId >= 0 && iconId < kDesktopIconCount && s_selectedIconIds[iconId];
}

static void ClearDesktopIconSelection()
{
    bool changed = false;
    for (int i = 0; i < kDesktopIconCount; i++) {
        if (s_selectedIconIds[i]) changed = true;
        s_selectedIconIds[i] = false;
    }
    s_selectedIcon = -1;
    s_focusedSelectedIconId = -1;
    s_lastSelectedIconId = -1;
    if (changed) log_selection_change("cleared");
}

static void SelectDesktopIcon(int displayIndex, bool additive)
{
    if (displayIndex < 0 || displayIndex >= s_visibleIconCount) return;
    int iconId = s_visibleIconIndices[displayIndex];
    if (iconId < 0 || iconId >= kDesktopIconCount) return;

    if (!additive) {
        for (int i = 0; i < kDesktopIconCount; i++) {
            s_selectedIconIds[i] = false;
        }
    }

    s_selectedIconIds[iconId] = true;
    s_selectedIcon = displayIndex;
    s_focusedSelectedIconId = iconId;
    s_lastSelectedIconId = iconId;
    log_selection_change(additive ? "added" : "selected");
}

static void ToggleDesktopIconSelection(int displayIndex)
{
    if (displayIndex < 0 || displayIndex >= s_visibleIconCount) return;
    int iconId = s_visibleIconIndices[displayIndex];
    if (iconId < 0 || iconId >= kDesktopIconCount) return;

    s_selectedIconIds[iconId] = !s_selectedIconIds[iconId];
    if (s_selectedIconIds[iconId]) {
        s_selectedIcon = displayIndex;
        s_focusedSelectedIconId = iconId;
        s_lastSelectedIconId = iconId;
    } else if (s_focusedSelectedIconId == iconId) {
        s_focusedSelectedIconId = -1;
        sync_selected_icon_after_layout();
    }

    log_selection_change(s_selectedIconIds[iconId] ? "toggled on" : "toggled off");
}

static void SelectDesktopIconRange(int startDisplayIndex, int endDisplayIndex)
{
    if (startDisplayIndex < 0 || startDisplayIndex >= s_visibleIconCount) startDisplayIndex = endDisplayIndex;
    if (endDisplayIndex < 0 || endDisplayIndex >= s_visibleIconCount) return;

    if (startDisplayIndex > endDisplayIndex) {
        int tmp = startDisplayIndex;
        startDisplayIndex = endDisplayIndex;
        endDisplayIndex = tmp;
    }

    for (int i = 0; i < kDesktopIconCount; i++) {
        s_selectedIconIds[i] = false;
    }

    for (int displayIdx = startDisplayIndex; displayIdx <= endDisplayIndex; displayIdx++) {
        int iconId = s_visibleIconIndices[displayIdx];
        if (iconId >= 0 && iconId < kDesktopIconCount) {
            s_selectedIconIds[iconId] = true;
        }
    }

    s_selectedIcon = endDisplayIndex;
    s_focusedSelectedIconId = s_visibleIconIndices[endDisplayIndex];
    s_lastSelectedIconId = s_focusedSelectedIconId;
    log_selection_change("range selected");
}

static int GetSelectedDesktopIconIndices(int* outIndices, int maxIndices)
{
    int count = 0;
    for (int displayIdx = 0; displayIdx < s_visibleIconCount; displayIdx++) {
        if (is_display_icon_selected(displayIdx)) {
            if (outIndices && count < maxIndices) {
                outIndices[count] = displayIdx;
            }
            count++;
        }
    }
    return count;
}

static void snapshot_icon_drag_positions()
{
    for (int displayIdx = 0; displayIdx < s_visibleIconCount; displayIdx++) {
        s_dragOriginalIconX[displayIdx] = s_iconPosX[displayIdx];
        s_dragOriginalIconY[displayIdx] = s_iconPosY[displayIdx];
        s_dragSelectedIcons[displayIdx] = is_display_icon_selected(displayIdx);
    }
}

static void clear_icon_drag_state()
{
    s_dragging = false;
    s_dragStarted = false;
    s_dragIconIndex = -1;
    for (int displayIdx = 0; displayIdx < kDesktopIconCount; displayIdx++) {
        s_dragSelectedIcons[displayIdx] = false;
    }
}

static bool icon_bounds_intersect_rect(int displayIndex, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    if (displayIndex < 0 || displayIndex >= s_visibleIconCount) return false;
    int32_t iconLeft = s_iconPosX[displayIndex];
    int32_t iconTop = s_iconPosY[displayIndex];
    int32_t iconRight = iconLeft + (int32_t)kIconCellW;
    int32_t iconBottom = iconTop + (int32_t)kIconCellH;
    return iconLeft < right && iconRight > left && iconTop < bottom && iconBottom > top;
}

static void snapshot_selection_base()
{
    for (int i = 0; i < kDesktopIconCount; i++) {
        s_selectionBaseIconIds[i] = s_selectedIconIds[i];
    }
}

static void SelectIconsInRectangle(int32_t left, int32_t top, int32_t right, int32_t bottom, bool additive)
{
    if (left > right) { int32_t t = left; left = right; right = t; }
    if (top > bottom) { int32_t t = top; top = bottom; bottom = t; }

    for (int i = 0; i < kDesktopIconCount; i++) {
        s_selectedIconIds[i] = additive ? s_selectionBaseIconIds[i] : false;
    }

    int lastHit = -1;
    for (int displayIdx = 0; displayIdx < s_visibleIconCount; displayIdx++) {
        if (icon_bounds_intersect_rect(displayIdx, left, top, right, bottom)) {
            int iconId = s_visibleIconIndices[displayIdx];
            if (iconId >= 0 && iconId < kDesktopIconCount) {
                s_selectedIconIds[iconId] = true;
                lastHit = displayIdx;
            }
        }
    }

    if (lastHit >= 0) {
        s_selectedIcon = lastHit;
        s_focusedSelectedIconId = s_visibleIconIndices[lastHit];
        s_lastSelectedIconId = s_focusedSelectedIconId;
    } else {
        sync_selected_icon_after_layout();
    }
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
    if (s_wallpaperConfig.gridSpacing == 0)
        s_wallpaperConfig.gridSpacing = 50;
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

static const BuiltInWallpaperPalette* find_builtin_wallpaper(const char* id)
{
    if (!id) return nullptr;
    for (int i = 0; i < kBuiltInWallpaperCount; ++i) {
        if (desktop_str_eq(id, s_builtInWallpapers[i].id)) return &s_builtInWallpapers[i];
    }
    return nullptr;
}

static const BuiltInGradientPalette* find_builtin_gradient(const char* id)
{
    if (!id) return nullptr;
    for (int i = 0; i < kBuiltInGradientCount; ++i) {
        if (desktop_str_eq(id, s_builtInGradients[i].id)) return &s_builtInGradients[i];
    }
    return nullptr;
}

static const char* wallpaper_scale_mode_name(WallpaperScaleMode mode)
{
    switch (mode) {
        case WallpaperScaleMode::Fit: return "fit";
        case WallpaperScaleMode::Stretch: return "stretch";
        case WallpaperScaleMode::Center: return "center";
        case WallpaperScaleMode::Tile: return "tile";
        case WallpaperScaleMode::Fill:
        default: return "fill";
    }
}

static WallpaperScaleMode parse_wallpaper_scale_mode(const char* value, bool* supported = nullptr)
{
    if (supported) *supported = true;
    if (!value || value[0] == '\0' || desktop_str_eq(value, "fill") || desktop_str_eq(value, "cover")) return WallpaperScaleMode::Fill;
    if (desktop_str_eq(value, "fit") || desktop_str_eq(value, "contain")) return WallpaperScaleMode::Fit;
    if (desktop_str_eq(value, "stretch")) return WallpaperScaleMode::Stretch;
    if (desktop_str_eq(value, "center")) return WallpaperScaleMode::Center;
    if (desktop_str_eq(value, "tile")) return WallpaperScaleMode::Tile;
    if (supported) *supported = false;
    return WallpaperScaleMode::Fill;
}

static bool gximg_header_valid(const GximgHeader* header)
{
    return header &&
           header->magic[0] == 'G' && header->magic[1] == 'X' && header->magic[2] == 'I' && header->magic[3] == 'M' &&
           header->magic[4] == 'G' && header->magic[5] == '0' && header->magic[6] == '0' && header->magic[7] == '1' &&
           header->width > 0 && header->height > 0 && header->format == 1;
}

static bool load_gximg_file(const char* path, WallpaperImageCache& cache)
{
    cache.loadAttempted = true;
    cache.loadSucceeded = false;
    cache.pixels = nullptr;
    cache.width = 0;
    cache.height = 0;

    if (!path || !path[0]) return false;

    serial::puts("[desktop] wallpaper image lookup path=");
    serial::puts(path);
    serial::puts("\n");

    kernel::vfs::FileInfo info{};
    if (kernel::vfs::stat(path, &info) != kernel::vfs::VFS_OK || info.size < sizeof(GximgHeader)) {
        serial::puts("[desktop] gximg stat failed: ");
        serial::puts(path);
        serial::puts("\n");
        return false;
    }

    if (info.size > 0x7FFFFFFF) {
        serial::puts("[desktop] gximg too large: ");
        serial::puts(path);
        serial::puts("\n");
        return false;
    }

    if (info.size > sizeof(s_wallpaperLoadScratch)) {
        serial::puts("[desktop] gximg exceeds static scratch buffer: ");
        serial::puts(path);
        serial::puts("\n");
        return false;
    }

    uint8_t* raw = reinterpret_cast<uint8_t*>(s_wallpaperLoadScratch);

    int32_t fileRead = kernel::vfs::read_file(path, raw, (uint32_t)info.size);
    if (fileRead != (int32_t)info.size) {
        serial::puts("[desktop] gximg file read failed: ");
        serial::puts(path);
        serial::puts(" read=");
        serial::put_hex32((uint32_t)(fileRead < 0 ? 0 : fileRead));
        serial::puts(" expected=");
        serial::put_hex32((uint32_t)info.size);
        serial::puts("\n");
        return false;
    }

    const GximgHeader* header = reinterpret_cast<const GximgHeader*>(raw);
    if (!gximg_header_valid(header)) {
        serial::puts("[desktop] gximg header invalid: ");
        serial::puts(path);
        serial::puts("\n");
        return false;
    }

    uint64_t pixelCount = (uint64_t)header->width * (uint64_t)header->height;
    uint64_t byteCount = pixelCount * sizeof(uint32_t);
    if (sizeof(GximgHeader) + byteCount > info.size) {
        serial::puts("[desktop] gximg payload truncated: ");
        serial::puts(path);
        serial::puts("\n");
        return false;
    }

    uint32_t* pixels = nullptr;
    uint32_t pixelCapacity = 0;
    if (&cache == &s_wallpaperFullCache) {
        pixels = s_wallpaperFullPixels;
        pixelCapacity = kWallpaperFullPixelCapacity;
    } else {
        for (int i = 0; i < kBuiltInWallpaperCount; ++i) {
            if (&cache == &s_wallpaperThumbCache[i]) {
                pixels = s_wallpaperThumbPixels[i];
                pixelCapacity = kWallpaperThumbPixelCapacity;
                break;
            }
        }
    }
    if (!pixels || pixelCount > pixelCapacity) {
        serial::puts("[desktop] gximg exceeds static pixel cache: ");
        serial::puts(path);
        serial::puts("\n");
        return false;
    }

    uint32_t imageWidth = header->width;
    uint32_t imageHeight = header->height;
    const uint32_t* srcPixels = reinterpret_cast<const uint32_t*>(raw + sizeof(GximgHeader));
    for (uint64_t i = 0; i < pixelCount; ++i) {
        pixels[(size_t)i] = srcPixels[(size_t)i];
    }
    cache.pixels = pixels;
    cache.width = imageWidth;
    cache.height = imageHeight;
    cache.loadSucceeded = true;
    serial::puts("[desktop] gximg loaded: ");
    serial::puts(path);
    serial::puts("\n");
    return true;
}

static void draw_scaled_gximg(const WallpaperImageCache& cache, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!cache.pixels || cache.width == 0 || cache.height == 0 || width == 0 || height == 0) return;
    draw_argb_icon_buffer(cache.pixels, cache.width, cache.height, x, y, width, height);
}

static void draw_wallpaper_gradient_base(uint32_t w, uint32_t h)
{
    for (uint32_t y = 0; y < h; y++) {
        uint32_t lineColor = lerp_color(
            s_wallpaperConfig.topColor,
            s_wallpaperConfig.bottomColor,
            y,
            h > 1 ? h - 1 : 1
        );
        framebuffer::fill_rect(0, y, w, 1, lineColor);
    }
}

static void draw_gximg_scaled_mode(const WallpaperImageCache& cache, uint32_t targetW, uint32_t targetH, WallpaperScaleMode mode)
{
    if (!cache.pixels || cache.width == 0 || cache.height == 0 || targetW == 0 || targetH == 0) return;

    if (mode == WallpaperScaleMode::Tile) {
        uint32_t stepW = cache.width ? cache.width : 1;
        uint32_t stepH = cache.height ? cache.height : 1;
        for (uint32_t ty = 0; ty < targetH; ty += stepH) {
            for (uint32_t tx = 0; tx < targetW; tx += stepW) {
                for (uint32_t y = 0; y < stepH && ty + y < targetH; ++y) {
                    for (uint32_t x = 0; x < stepW && tx + x < targetW; ++x) {
                        framebuffer::put_pixel(tx + x, ty + y, cache.pixels[y * cache.width + x]);
                    }
                }
            }
        }
        return;
    }

    int32_t drawX = 0;
    int32_t drawY = 0;
    uint32_t drawW = targetW;
    uint32_t drawH = targetH;

    if (mode == WallpaperScaleMode::Center) {
        drawW = cache.width;
        drawH = cache.height;
        drawX = ((int32_t)targetW - (int32_t)drawW) / 2;
        drawY = ((int32_t)targetH - (int32_t)drawH) / 2;
    } else if (mode == WallpaperScaleMode::Fit || mode == WallpaperScaleMode::Fill) {
        uint64_t byWidthH = ((uint64_t)cache.height * targetW) / cache.width;
        uint64_t byHeightW = ((uint64_t)cache.width * targetH) / cache.height;
        if (mode == WallpaperScaleMode::Fit) {
            if (byWidthH <= targetH) {
                drawW = targetW;
                drawH = (uint32_t)(byWidthH ? byWidthH : 1);
            } else {
                drawW = (uint32_t)(byHeightW ? byHeightW : 1);
                drawH = targetH;
            }
        } else {
            if (byWidthH >= targetH) {
                drawW = targetW;
                drawH = (uint32_t)(byWidthH ? byWidthH : 1);
            } else {
                drawW = (uint32_t)(byHeightW ? byHeightW : 1);
                drawH = targetH;
            }
        }
        drawX = ((int32_t)targetW - (int32_t)drawW) / 2;
        drawY = ((int32_t)targetH - (int32_t)drawH) / 2;
    }

    int32_t minY = drawY < 0 ? 0 : drawY;
    int32_t minX = drawX < 0 ? 0 : drawX;
    int32_t maxY = drawY + (int32_t)drawH;
    int32_t maxX = drawX + (int32_t)drawW;
    if (maxY > (int32_t)targetH) maxY = (int32_t)targetH;
    if (maxX > (int32_t)targetW) maxX = (int32_t)targetW;
    if (maxX <= minX || maxY <= minY) return;

    for (int32_t y = minY; y < maxY; ++y) {
        uint32_t sy = (uint32_t)(((uint64_t)(y - drawY) * cache.height) / drawH);
        if (sy >= cache.height) sy = cache.height - 1;
        for (int32_t x = minX; x < maxX; ++x) {
            uint32_t sx = (uint32_t)(((uint64_t)(x - drawX) * cache.width) / drawW);
            if (sx >= cache.width) sx = cache.width - 1;
            framebuffer::put_pixel((uint32_t)x, (uint32_t)y, cache.pixels[sy * cache.width + sx]);
        }
    }
}

static WallpaperImageCache* load_wallpaper_thumbnail_cache(const char* wallpaperId)
{
    for (int i = 0; i < kBuiltInWallpaperCount; ++i) {
        if (!desktop_str_eq(s_builtInWallpapers[i].id, wallpaperId)) continue;
        if (!s_wallpaperThumbCache[i].loadAttempted) load_gximg_file(s_builtInWallpapers[i].thumbnailPath, s_wallpaperThumbCache[i]);
        return s_wallpaperThumbCache[i].loadSucceeded ? &s_wallpaperThumbCache[i] : nullptr;
    }
    return nullptr;
}

static WallpaperImageCache* load_wallpaper_full_cache(const char* wallpaperId)
{
    const BuiltInWallpaperPalette* entry = find_builtin_wallpaper(wallpaperId);
    if (!entry) return nullptr;
    if (s_wallpaperFullCacheId && desktop_str_eq(s_wallpaperFullCacheId, wallpaperId) && s_wallpaperFullCache.loadAttempted) {
        return s_wallpaperFullCache.loadSucceeded ? &s_wallpaperFullCache : nullptr;
    }
    s_wallpaperFullCache.pixels = nullptr;
    s_wallpaperFullCacheId = wallpaperId;
    load_gximg_file(entry->fullImagePath, s_wallpaperFullCache);
    return s_wallpaperFullCache.loadSucceeded ? &s_wallpaperFullCache : nullptr;
}

static void initialize_wallpaper_pack_registry()
{
    if (s_wallpaperPackEntryCount != 0) return;
    for (int i = 0; i < kBuiltInWallpaperCount; ++i) {
        s_wallpaperPackEntries[s_wallpaperPackEntryCount++] = { s_builtInWallpapers[i].id, s_builtInWallpapers[i].fullImagePath, false };
        s_wallpaperPackEntries[s_wallpaperPackEntryCount++] = { s_builtInWallpapers[i].id, s_builtInWallpapers[i].thumbnailPath, true };
    }
}

static WallpaperPackEntry* find_wallpaper_pack_entry(const char* wallpaperId, bool thumbnail)
{
    if (!wallpaperId) return nullptr;
    initialize_wallpaper_pack_registry();
    for (int i = 0; i < s_wallpaperPackEntryCount; ++i) {
        WallpaperPackEntry& entry = s_wallpaperPackEntries[i];
        if (desktop_str_eq(entry.id, wallpaperId) && entry.thumbnail == thumbnail) return &entry;
    }
    return nullptr;
}

void set_wallpaper_image_pack(const void* packBase, uint64_t packSize)
{
    initialize_wallpaper_pack_registry();
    if (!packBase || packSize < kernel::ramdisk::RAMDISK_SECTOR_SIZE) {
        serial::puts("[desktop] wallpaper image pack missing\n");
        return;
    }

    uint8_t ramdiskIndex = kernel::ramdisk::create_readonly_at(packBase, (size_t)packSize, "wallimg");
    if (ramdiskIndex == 0xFF) {
        serial::puts("[desktop] failed to attach wallpaper image pack\n");
        return;
    }

    uint8_t blockIndex = 0xFF;
    for (uint8_t i = 0; i < kernel::block::MAX_BLOCK_DEVICES; ++i) {
        const kernel::block::BlockDevice* dev = kernel::block::get_device(i);
        if (dev && desktop_str_eq(dev->name, "wallimg")) {
            blockIndex = i;
            break;
        }
    }

    if (blockIndex == 0xFF) {
        serial::puts("[desktop] failed to find wallpaper block device\n");
        return;
    }

    if (kernel::vfs::mount_type("/system", blockIndex, kernel::vfs::FS_TYPE_FAT32) == 0xFF) {
        serial::puts("[desktop] failed to mount wallpaper image pack at /system\n");
        return;
    }

    s_wallpaperPackMounted = true;
    reset_wallpaper_image_caches();
    serial::puts("[desktop] mounted wallpaper image pack at /system; wallpapers live at /system/wall\n");
}

bool draw_wallpaper_thumbnail_by_id(const char* wallpaperId, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!s_wallpaperPackMounted) {
        serial::puts("[desktop] wallpaper thumbnail fallback: image pack not mounted\n");
        return false;
    }
    WallpaperImageCache* cache = load_wallpaper_thumbnail_cache(wallpaperId);
    if (!cache) return false;
    draw_scaled_gximg(*cache, x, y, w, h);
    return true;
}

static void persist_wallpaper_id(const char* id)
{
    if (!id || !id[0]) return;
    vfs::write_file("/desktop.wallpaper.id", id, (uint32_t)desktop_strlen(id));
}

static void persist_wallpaper_scale_mode()
{
    const char* mode = wallpaper_scale_mode_name(s_wallpaperConfig.scaleMode);
    vfs::write_file("/desktop.background.scale", mode, (uint32_t)desktop_strlen(mode));
}

static void load_persisted_wallpaper_scale_mode()
{
    char modeBuf[32];
    int32_t count = vfs::read_file("/desktop.background.scale", modeBuf, sizeof(modeBuf) - 1);
    if (count <= 0) {
        serial::puts("[desktop] background scale mode default=fill\n");
        s_wallpaperConfig.scaleMode = WallpaperScaleMode::Fill;
        return;
    }
    modeBuf[count] = '\0';
    while (count > 0 && (modeBuf[count - 1] == '\n' || modeBuf[count - 1] == '\r' || modeBuf[count - 1] == ' ' || modeBuf[count - 1] == '\t')) {
        modeBuf[--count] = '\0';
    }
    bool supported = true;
    s_wallpaperConfig.scaleMode = parse_wallpaper_scale_mode(modeBuf, &supported);
    serial::puts("[desktop] loaded background scale mode=");
    serial::puts(wallpaper_scale_mode_name(s_wallpaperConfig.scaleMode));
    serial::puts("\n");
    if (!supported) serial::puts("[desktop] unsupported background scale mode, falling back to fill\n");
}

static void load_persisted_wallpaper_id()
{
    char idBuf[96];
    int32_t count = vfs::read_file("/desktop.wallpaper.id", idBuf, sizeof(idBuf) - 1);
    if (count <= 0) return;
    idBuf[count] = '\0';
    while (count > 0 && (idBuf[count - 1] == '\n' || idBuf[count - 1] == '\r' || idBuf[count - 1] == ' ' || idBuf[count - 1] == '\t')) {
        idBuf[--count] = '\0';
    }
    serial::puts("[desktop] loaded saved background id=");
    serial::puts(idBuf);
    serial::puts("\n");
    const BuiltInGradientPalette* gradient = find_builtin_gradient(idBuf);
    if (gradient) {
        s_wallpaperConfig.type = WallpaperType::Gradient;
        s_wallpaperConfig.topColor = gradient->topColor;
        s_wallpaperConfig.bottomColor = gradient->bottomColor;
        s_wallpaperConfig.gridColor = gradient->accentColor;
        s_wallpaperConfig.wallpaperId = gradient->id;
        s_wallpaperConfig.showBranding = true;
        s_wallpaperConfig.showGrid = true;
        serial::puts("[desktop] loaded background kind=gradient\n");
        return;
    }

    const BuiltInWallpaperPalette* entry = find_builtin_wallpaper(idBuf);
    if (entry) {
        s_wallpaperConfig.type = WallpaperType::BuiltIn;
        s_wallpaperConfig.topColor = entry->topColor;
        s_wallpaperConfig.bottomColor = entry->bottomColor;
        s_wallpaperConfig.gridColor = entry->accentColor;
        s_wallpaperConfig.wallpaperId = entry->id;
        s_wallpaperConfig.showBranding = false;
        s_wallpaperConfig.showGrid = false;
        serial::puts("[desktop] loaded background kind=image\n");
    } else {
        serial::puts("[desktop] Invalid persisted wallpaper id, using default\n");
    }
}

void set_wallpaper_by_id(const char* wallpaperId)
{
    serial::puts("[desktop] set wallpaper request id=");
    serial::puts(wallpaperId ? wallpaperId : "(null)");
    serial::puts("\n");
    const BuiltInGradientPalette* gradient = find_builtin_gradient(wallpaperId);
    if (gradient) {
        serial::puts("[desktop] selected gradient id=");
        serial::puts(gradient->id);
        serial::puts("\n");
        s_wallpaperConfig.type = WallpaperType::Gradient;
        s_wallpaperConfig.topColor = gradient->topColor;
        s_wallpaperConfig.bottomColor = gradient->bottomColor;
        s_wallpaperConfig.gridColor = gradient->accentColor;
        s_wallpaperConfig.wallpaperId = gradient->id;
        s_wallpaperConfig.showBranding = true;
        s_wallpaperConfig.showGrid = true;
        persist_wallpaper_id(gradient->id);
        persist_wallpaper_scale_mode();
        s_needsRedraw = true;
        return;
    }

    const BuiltInWallpaperPalette* entry = find_builtin_wallpaper(wallpaperId);
    if (!entry) {
        serial::puts("[desktop] Wallpaper id not found, falling back to default\n");
        entry = &s_builtInWallpapers[0];
    }
    serial::puts("[desktop] selected wallpaper full=");
    serial::puts(entry->fullImagePath);
    serial::puts(" thumb=");
    serial::puts(entry->thumbnailPath);
    serial::puts("\n");
    s_wallpaperConfig.type = WallpaperType::BuiltIn;
    s_wallpaperConfig.topColor = entry->topColor;
    s_wallpaperConfig.bottomColor = entry->bottomColor;
    s_wallpaperConfig.gridColor = entry->accentColor;
    s_wallpaperConfig.wallpaperId = entry->id;
    s_wallpaperConfig.showBranding = false;
    s_wallpaperConfig.showGrid = false;
    persist_wallpaper_id(entry->id);
    persist_wallpaper_scale_mode();
    s_needsRedraw = true;
}

const char* get_wallpaper_id()
{
    return s_wallpaperConfig.wallpaperId ? s_wallpaperConfig.wallpaperId : s_builtInWallpapers[0].id;
}

void reload_persisted_wallpaper()
{
    load_persisted_wallpaper_scale_mode();
    load_persisted_wallpaper_id();
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

static void draw_selection_rectangle()
{
    if (!s_selectionDragPending && !s_selectionDragActive) return;

    int32_t left = s_selectionStartX;
    int32_t top = s_selectionStartY;
    int32_t right = s_selectionCurrentX;
    int32_t bottom = s_selectionCurrentY;
    if (left > right) { int32_t t = left; left = right; right = t; }
    if (top > bottom) { int32_t t = top; top = bottom; bottom = t; }

    DesktopRect work = get_current_work_area();
    if (left < (int32_t)work.x) left = (int32_t)work.x;
    if (top < (int32_t)work.y) top = (int32_t)work.y;
    if (right >= (int32_t)(work.x + work.w)) right = (int32_t)(work.x + work.w) - 1;
    if (bottom >= (int32_t)(work.y + work.h)) bottom = (int32_t)(work.y + work.h) - 1;

    int32_t width = right - left;
    int32_t height = bottom - top;
    if (width < 2 || height < 2) return;

    uint32_t outline = rgb(120, 170, 255);
    uint32_t fill = rgb(35, 65, 120);
    for (int32_t y = top + 2; y < bottom - 1; y += 4) {
        for (int32_t x = left + 2; x < right - 1; x += 4) {
            framebuffer::put_pixel(x, y, fill);
        }
    }
    draw_rect((uint32_t)left, (uint32_t)top, (uint32_t)width, (uint32_t)height, outline);
}

// ============================================================
// Background - gradient + branding (matches desktop_wallpaper.h)
// ============================================================

static void draw_background()
{
    uint32_t w = s_screenW;
    uint32_t h = s_screenH;

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
            uint32_t gs = s_wallpaperConfig.gridSpacing > 0 ? s_wallpaperConfig.gridSpacing : 50;
            for (uint32_t x = gs; x < w; x += gs)
                vline(x, 0, h, gridCol);
            for (uint32_t y = gs; y < h; y += gs)
                hline(0, y, w, gridCol);
            break;
        }

        case WallpaperType::BuiltIn: {
            draw_wallpaper_gradient_base(w, h);
            WallpaperImageCache* image = load_wallpaper_full_cache(get_wallpaper_id());
            if (image) {
                serial::puts("[desktop] rendering wallpaper image for id=");
                serial::puts(get_wallpaper_id());
                serial::puts(" scale=");
                serial::puts(wallpaper_scale_mode_name(s_wallpaperConfig.scaleMode));
                serial::puts("\n");
                draw_gximg_scaled_mode(*image, w, h, s_wallpaperConfig.scaleMode);
            } else {
                serial::puts("[desktop] wallpaper image fallback to gradient\n");
                for (uint32_t y = 0; y < h; y++) {
                    uint32_t lineColor = lerp_color(
                        s_wallpaperConfig.topColor,
                        s_wallpaperConfig.bottomColor,
                        y,
                        h > 1 ? h - 1 : 1
                    );
                    framebuffer::fill_rect(0, y, w, 1, lineColor);
                }

                uint32_t accent = s_wallpaperConfig.gridColor;
                uint32_t cx = w / 2;
                uint32_t cy = h / 2;
                for (uint32_t ring = 0; ring < 7; ++ring) {
                    uint32_t rw = 80 + ring * 46;
                    uint32_t rh = 24 + ring * 18;
                    uint32_t ox = cx > rw / 2 ? cx - rw / 2 : 0;
                    uint32_t oy = cy > rh / 2 ? cy - rh / 2 : 0;
                    draw_rect(ox, oy, rw, rh, accent);
                }
            }
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
        uint32_t gs = s_wallpaperConfig.gridSpacing > 0 ? s_wallpaperConfig.gridSpacing : 50;
        for (uint32_t x = gs; x < w; x += gs)
            vline(x, 0, h, gridColor);
        for (uint32_t y = gs; y < h; y += gs)
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

    if (appName[0] == 'T' && appName[1] == 'r') {  // "Trash"
        framebuffer::fill_rect(cx - 7, cy - 6, 14, 2, iconColor);
        framebuffer::fill_rect(cx - 5, cy - 8, 10, 2, iconColor);
        draw_rect(cx - 6, cy - 4, 12, 14, iconColor);
        vline(cx - 2, cy - 2, 10, iconColor);
        vline(cx + 1, cy - 2, 10, iconColor);
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

static bool text_equals(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

static bool text_ends_with(const char* value, const char* suffix)
{
    if (!value || !suffix) return false;
    int valueLen = 0;
    int suffixLen = 0;
    while (value[valueLen]) ++valueLen;
    while (suffix[suffixLen]) ++suffixLen;
    if (suffixLen > valueLen) return false;
    for (int i = 0; i < suffixLen; ++i) {
        if (value[valueLen - suffixLen + i] != suffix[i]) return false;
    }
    return true;
}

static const char* GetDesktopIconLogicalName(const char* label)
{
    if (text_equals(label, "Notepad")) return "app.notepad";
    if (text_equals(label, "Calculator")) return "app.calculator";
    if (text_equals(label, "Console")) return "app.console";
    if (text_equals(label, "Trash")) {
        int count = 0;
        uint8_t dir = vfs::opendir("/Trash");
        if (dir != 0xFF) {
            vfs::DirEntry entry{};
            while (vfs::readdir(dir, &entry)) {
                if (entry.name[0] == '.' && (entry.name[1] == '\0' || (entry.name[1] == '.' && entry.name[2] == '\0'))) continue;
                if (text_ends_with(entry.name, ".trashinfo")) continue;
                ++count;
                break;
            }
            vfs::closedir(dir);
        }
        serial::puts("[desktop] Trash icon state count=");
        serial::puts(count > 0 ? "nonzero" : "0");
        serial::puts(" iconKey=");
        serial::puts(count > 0 ? "trash.full" : "trash.empty");
        serial::puts("\n");
        return count > 0 ? "trash.full" : "trash.empty";
    }
    if (text_equals(label, "TaskManager")) return "app.taskmanager";
    if (text_equals(label, "Files")) return "app.files";
    if (text_equals(label, "Paint")) return "app.paint";
    if (text_equals(label, "Clock")) return "app.clock";
    if (text_equals(label, "DiskManager")) return "app.diskmanager";
    if (text_equals(label, "HDInstaller")) return "app.installer";
    if (text_equals(label, "DisplayOptions")) return "app.settings";
    if (text_equals(label, "Control Panel")) return "app.controlpanel";
    if (text_equals(label, "Settings")) return "app.settings";
    if (text_equals(label, "Computer")) return "place.computer";
    if (text_equals(label, "Documents")) return "place.documents";
    if (text_equals(label, "Pictures")) return "place.pictures";
    if (text_equals(label, "Music")) return "place.music";
    if (text_equals(label, "Network")) return "place.network";
    return "file.generic";
}

static const char* GetStartMenuLogicalIconName(const char* label)
{
    return GetDesktopIconLogicalName(label);
}

static void draw_colored_desktop_icon(uint32_t ix, uint32_t iy, uint32_t color, const char* label, bool dragging)
{
    if (dragging) {
        uint8_t dr = (uint8_t)(((color >> 16) & 0xFF) * 7 / 10);
        uint8_t dg = (uint8_t)(((color >> 8) & 0xFF) * 7 / 10);
        uint8_t db = (uint8_t)((color & 0xFF) * 7 / 10);
        framebuffer::fill_rect(ix, iy, kIconSize, kIconSize, rgb(dr, dg, db));
        draw_icon_symbol(ix, iy, kIconSize, label);
        draw_rect(ix, iy, kIconSize, kIconSize, rgb(140, 140, 160));
        return;
    }

    framebuffer::fill_rect(ix, iy, kIconSize, kIconSize, color);

    uint8_t br = (uint8_t)(((color >> 16) & 0xFF));
    uint8_t bg = (uint8_t)(((color >> 8) & 0xFF));
    uint8_t bb = (uint8_t)(color & 0xFF);
    uint8_t lr = (uint8_t)(br + 30 > 255 ? 255 : br + 30);
    uint8_t lg = (uint8_t)(bg + 30 > 255 ? 255 : bg + 30);
    uint8_t lb = (uint8_t)(bb + 30 > 255 ? 255 : bb + 30);
    hline(ix + 1, iy + 1, kIconSize - 2, rgb(lr, lg, lb));
    hline(ix + 1, iy + 2, kIconSize - 2, rgb(lr, lg, lb));

    draw_icon_symbol(ix, iy, kIconSize, label);
    draw_rect(ix, iy, kIconSize, kIconSize, rgb(200, 200, 220));
    hline(ix + 1, iy + kIconSize - 1, kIconSize - 1, rgb(80, 80, 100));
    vline(ix + kIconSize - 1, iy + 1, kIconSize - 1, rgb(80, 80, 100));
}

static bool draw_argb_icon_buffer(const uint32_t* pixels, uint32_t srcW, uint32_t srcH, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!pixels || srcW == 0 || srcH == 0 || width == 0 || height == 0) return false;

    bool drewPixel = false;
    for (uint32_t dy = 0; dy < height; dy++) {
        uint32_t sy = (uint32_t)((uint64_t)dy * (uint64_t)srcH / height);
        for (uint32_t dx = 0; dx < width; dx++) {
            uint32_t sx = (uint32_t)((uint64_t)dx * (uint64_t)srcW / width);
            uint32_t src = pixels[sy * srcW + sx];
            uint8_t a = (uint8_t)((src >> 24) & 0xFF);
            if (a == 0) continue;

            uint32_t px = x + dx;
            uint32_t py = y + dy;
            drewPixel = true;
            if (a == 0xFF) {
                framebuffer::put_pixel(px, py, src);
            } else {
                uint32_t dst = framebuffer::get_pixel(px, py);
                uint8_t sr = (uint8_t)((src >> 16) & 0xFF);
                uint8_t sg = (uint8_t)((src >> 8) & 0xFF);
                uint8_t sb = (uint8_t)(src & 0xFF);
                uint8_t dr = (uint8_t)((dst >> 16) & 0xFF);
                uint8_t dg = (uint8_t)((dst >> 8) & 0xFF);
                uint8_t db = (uint8_t)(dst & 0xFF);
                uint8_t r = (uint8_t)((sr * a + dr * (255 - a)) / 255);
                uint8_t g = (uint8_t)((sg * a + dg * (255 - a)) / 255);
                uint8_t b = (uint8_t)((sb * a + db * (255 - a)) / 255);
                framebuffer::put_pixel(px, py, 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b);
            }
        }
    }
    return drewPixel;
}

static const uint32_t* get_embedded_desktop_icon_pixels(const char* logicalName)
{
    if (text_equals(logicalName, "app.notepad"))       return kDesktopThemeIcon_Notepad;
    if (text_equals(logicalName, "app.calculator"))    return kDesktopThemeIcon_Calculator;
    if (text_equals(logicalName, "app.console"))       return kDesktopThemeIcon_Console;
    if (text_equals(logicalName, "trash.empty"))       return kDesktopThemeIcon_TrashEmpty;
    if (text_equals(logicalName, "trash.full"))        return kDesktopThemeIcon_TrashFull;
    if (text_equals(logicalName, "app.taskmanager"))   return kDesktopThemeIcon_TaskManager;
    if (text_equals(logicalName, "app.files"))         return kDesktopThemeIcon_Files;
    if (text_equals(logicalName, "app.paint"))         return kDesktopThemeIcon_Paint;
    if (text_equals(logicalName, "app.clock"))         return kDesktopThemeIcon_Clock;
    // Unmapped app icons: use closest available embedded icon as fallback
    if (text_equals(logicalName, "app.diskmanager"))   return kDesktopThemeIcon_Files;
    if (text_equals(logicalName, "app.installer"))     return kDesktopThemeIcon_Files;
    if (text_equals(logicalName, "app.controlpanel"))  return kDesktopThemeIcon_TaskManager;
    if (text_equals(logicalName, "app.settings"))      return kDesktopThemeIcon_TaskManager;
    // place.* icons (Start Menu right column)
    if (text_equals(logicalName, "place.computer"))    return kDesktopThemeIcon_Files;
    if (text_equals(logicalName, "place.documents"))   return kDesktopThemeIcon_Notepad;
    if (text_equals(logicalName, "place.pictures"))    return kDesktopThemeIcon_Paint;
    if (text_equals(logicalName, "place.music"))       return kDesktopThemeIcon_Clock;
    if (text_equals(logicalName, "place.network"))     return kDesktopThemeIcon_Console;
    return kDesktopThemeIcon_FileGeneric;
}

#if !defined(GXOS_BARE_METAL)
// Forward declaration - defined below inside the non-bare-metal block
static void draw_scaled_rgba_icon(const gxos::gui::ImagePtr& image, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
#endif

static bool draw_themed_start_menu_icon(uint32_t x, uint32_t y, const char* label)
{
    if (!s_enableStartMenuIcons) return false;
    if (!label || label[0] == '\0') return false;

    const char* logicalName = GetStartMenuLogicalIconName(label);
    uint32_t drawSize = s_startMenuIconSize > 0 ? s_startMenuIconSize : 16;

#if defined(GXOS_BARE_METAL)
    // Bare-metal: use embedded ARGB pixel arrays
    if (!s_startMenuIconTheme || !text_equals(s_startMenuIconTheme, "Flat")) return false;
    const uint32_t* pixels = get_embedded_desktop_icon_pixels(logicalName);
    if (!pixels) {
        serial::puts("[startmenu] embedded icon not found: ");
        serial::puts(logicalName);
        serial::puts("\n");
        return false;
    }
    return draw_argb_icon_buffer(pixels, kDesktopThemeIconW, kDesktopThemeIconH, x, y, drawSize, drawSize);
#else
    // Server / Windows host: use IconThemeManager (same path as desktop icons)
    gxos::gui::IconThemeManager& manager = gxos::gui::IconThemeManager::Instance();
    manager.SetCurrentThemeName(s_startMenuIconTheme ? s_startMenuIconTheme : "Flat");
    if (!manager.IconsEnabled()) return false;

    gxos::gui::ImagePtr image = manager.LoadIcon(logicalName, (int)drawSize);
    if (!image) {
        serial::puts("[startmenu] themed icon unavailable: ");
        serial::puts(logicalName);
        serial::puts("\n");
        return false;
    }
    draw_scaled_rgba_icon(image, x, y, drawSize, drawSize);
    return true;
#endif
}

#if defined(GXOS_BARE_METAL)
static bool draw_themed_desktop_icon(int iconIdx, uint32_t cx, uint32_t iy)
{
    if (!s_enableDesktopIcons || iconIdx < 0 || iconIdx >= kDesktopIconCount) return false;
    const char* logicalName = GetDesktopIconLogicalName(s_desktopIcons[iconIdx].label);
    const uint32_t* pixels = get_embedded_desktop_icon_pixels(logicalName);
    if (!pixels) return false;

    uint32_t drawSize = s_desktopIconSize > 0 ? s_desktopIconSize : kIconSize;
    uint32_t ix = cx + (kIconCellW > drawSize ? (kIconCellW - drawSize) / 2 : 0);
    return draw_argb_icon_buffer(pixels, kDesktopThemeIconW, kDesktopThemeIconH, ix, iy, drawSize, drawSize);
}
#else
static void reset_desktop_icon_cache_if_needed()
{
    if (s_cachedDesktopIconSize == s_desktopIconSize) return;
    for (int i = 0; i < kDesktopIconCount; i++) {
        s_desktopIconImageCache[i].reset();
        s_desktopIconLoadAttempted[i] = false;
        s_desktopIconMissingLogged[i] = false;
    }
    s_cachedDesktopIconSize = s_desktopIconSize;
}

static void draw_scaled_rgba_icon(const gxos::gui::ImagePtr& image, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!image || !image->isValid() || image->Channels < 4 || width == 0 || height == 0) return;

    for (uint32_t dy = 0; dy < height; dy++) {
        int sy = (int)((uint64_t)dy * (uint64_t)image->Height / height);
        for (uint32_t dx = 0; dx < width; dx++) {
            int sx = (int)((uint64_t)dx * (uint64_t)image->Width / width);
            const uint8_t* src = image->Pixels + ((sy * image->Width + sx) * image->Channels);
            uint8_t a = src[3];
            if (a == 0) continue;

            uint32_t px = x + dx;
            uint32_t py = y + dy;
            uint32_t srcColor = 0xFF000000u | ((uint32_t)src[0] << 16) | ((uint32_t)src[1] << 8) | (uint32_t)src[2];
            if (a == 0xFF) {
                framebuffer::put_pixel(px, py, srcColor);
            } else {
                uint32_t dst = framebuffer::get_pixel(px, py);
                uint8_t dr = (uint8_t)((dst >> 16) & 0xFF);
                uint8_t dg = (uint8_t)((dst >> 8) & 0xFF);
                uint8_t db = (uint8_t)(dst & 0xFF);
                uint8_t r = (uint8_t)((src[0] * a + dr * (255 - a)) / 255);
                uint8_t g = (uint8_t)((src[1] * a + dg * (255 - a)) / 255);
                uint8_t b = (uint8_t)((src[2] * a + db * (255 - a)) / 255);
                framebuffer::put_pixel(px, py, 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b);
            }
        }
    }
}

static bool draw_themed_desktop_icon(int iconIdx, uint32_t cx, uint32_t iy)
{
    if (!s_enableDesktopIcons || iconIdx < 0 || iconIdx >= kDesktopIconCount) return false;

    reset_desktop_icon_cache_if_needed();

    gxos::gui::IconThemeManager& manager = gxos::gui::IconThemeManager::Instance();
    manager.SetCurrentThemeName(s_desktopIconTheme);
    if (!manager.IconsEnabled()) return false;

    if (!s_desktopIconLoadAttempted[iconIdx]) {
        s_desktopIconLoadAttempted[iconIdx] = true;
        const char* logicalName = GetDesktopIconLogicalName(s_desktopIcons[iconIdx].label);
        s_desktopIconImageCache[iconIdx] = manager.LoadIcon(logicalName, (int)s_desktopIconSize);
        if (!s_desktopIconImageCache[iconIdx] && !s_desktopIconMissingLogged[iconIdx]) {
            serial::puts("[desktop] themed icon unavailable: ");
            serial::puts(logicalName);
            serial::puts("\n");
            s_desktopIconMissingLogged[iconIdx] = true;
        }
    }

    gxos::gui::ImagePtr image = s_desktopIconImageCache[iconIdx];
    if (!image) return false;

    uint32_t drawSize = s_desktopIconSize > 0 ? s_desktopIconSize : kIconSize;
    uint32_t ix = cx + (kIconCellW > drawSize ? (kIconCellW - drawSize) / 2 : 0);
    draw_scaled_rgba_icon(image, ix, iy, drawSize, drawSize);
    return true;
}
#endif

static void draw_desktop_icon_item(int iconIdx, uint32_t cx, uint32_t cy, bool dragging)
{
    uint32_t ix = cx + (kIconCellW - kIconSize) / 2;
    uint32_t iy = cy + 4;
    const char* lbl = s_desktopIcons[iconIdx].label;

    if (!draw_themed_desktop_icon(iconIdx, cx, iy)) {
        draw_colored_desktop_icon(ix, iy, s_desktopIcons[iconIdx].color, lbl, dragging);
    }

    if (s_desktopIcons[iconIdx].pinned) {
        draw_text(ix + kIconSize - 8, iy + 2, "*", rgb(255, 220, 80), 1);
    }

    uint32_t labelY = iy + kIconSize + 4;
    int tw = measure_text(lbl);
    uint32_t lx = cx + (kIconCellW > (uint32_t)tw ? (kIconCellW - tw) / 2 : 0);
    draw_text(lx + 1, labelY + 1, lbl, rgb(0, 0, 0), 1);
    draw_text(lx, labelY, lbl, dragging ? rgb(200, 200, 210) : rgb(240, 240, 250), 1);
}

static void draw_desktop_icons()
{
    DesktopRect work = get_current_work_area();
    int32_t dragDeltaX = s_dragCurrentX - s_dragStartMouseX;
    int32_t dragDeltaY = s_dragCurrentY - s_dragStartMouseY;

    // Draw visible icons only (pinned + recent)
    for (int displayIdx = 0; displayIdx < s_visibleIconCount; displayIdx++) {
        // Selected icons are drawn together below while a group drag is active.
        if (s_dragging && s_dragStarted && s_dragSelectedIcons[displayIdx]) continue;

        int iconIdx = s_visibleIconIndices[displayIdx];
        uint32_t cx = (uint32_t)s_iconPosX[displayIdx];
        uint32_t cy = (uint32_t)s_iconPosY[displayIdx];

        if (cx < work.x || cy < work.y || cx + kIconCellW > work.x + work.w || cy + kIconCellH > work.y + work.h) continue;

        // Selection highlight background (matching compositor style)
        if (is_display_icon_selected(displayIdx)) {
            framebuffer::fill_rect(cx, cy, kIconCellW, kIconCellH, rgb(50, 90, 160));
            draw_rect(cx, cy, kIconCellW, kIconCellH, rgb(100, 160, 240));
        }

        draw_desktop_icon_item(iconIdx, cx, cy, false);
    }

    // Draw the dragged icon ghost(s) at current drag position. For a group
    // drag, every icon keeps the offset it had when the mouse button went down.
    if (s_dragging && s_dragStarted && s_dragIconIndex >= 0 && s_dragIconIndex < s_visibleIconCount) {
        for (int displayIdx = 0; displayIdx < s_visibleIconCount; displayIdx++) {
            if (!s_dragSelectedIcons[displayIdx]) continue;

            int iconIdx = s_visibleIconIndices[displayIdx];
            uint32_t cx = (uint32_t)(s_dragOriginalIconX[displayIdx] + dragDeltaX);
            uint32_t cy = (uint32_t)(s_dragOriginalIconY[displayIdx] + dragDeltaY);

            draw_desktop_icon_item(iconIdx, cx, cy, true);
        }
    }

    draw_selection_rectangle();
}

// ============================================================
// Taskbar widgets
// ============================================================

enum class TaskbarWidgetType : uint8_t {
    Search = 0,
    Clock,
    Network,
    Volume,
    Battery,
    Count,
};

struct TaskbarWidgetBounds {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
};

struct TaskbarWidget {
    TaskbarWidgetType type;
    bool visible;
    bool enabled;
    TaskbarWidgetBounds bounds;
    const char* hiddenReason;
    void (*render)(TaskbarWidget& widget);
    void (*click)(TaskbarWidget& widget);
    void (*update)(TaskbarWidget& widget);
};

static TaskbarWidget s_taskbarWidgets[(int)TaskbarWidgetType::Count];
static bool s_taskbarWidgetsInitialized = false;

static const char* taskbar_widget_name(TaskbarWidgetType type)
{
    switch (type) {
        case TaskbarWidgetType::Search: return "SearchWidget";
        case TaskbarWidgetType::Clock: return "ClockWidget";
        case TaskbarWidgetType::Network: return "NetworkWidget";
        case TaskbarWidgetType::Volume: return "VolumeWidget";
        case TaskbarWidgetType::Battery: return "BatteryWidget";
        default: return "UnknownWidget";
    }
}

static void log_taskbar_widget(TaskbarWidgetType type, bool visible, const char* reason)
{
    serial::puts("Taskbar: ");
    serial::puts(taskbar_widget_name(type));
    serial::puts(visible ? " enabled, " : " hidden, ");
    serial::puts(reason ? reason : "no reason recorded");
    serial::puts("\n");
}

static bool search_provider_available()
{
    // TODO: Re-enable SearchWidget when an app/command/file search provider and taskbar text input focus are available.
    return false;
}

static bool battery_provider_available()
{
    // TODO: Re-enable BatteryWidget when ACPI battery/power service exposes real battery state.
    return false;
}

static bool network_provider_available()
{
    return nic::is_active() || usb_net::device_count() > 0;
}

static bool network_link_up()
{
    if (nic::is_active() && nic::get_link_state() == nic::NIC_LINK_UP) return true;

    uint8_t count = usb_net::device_count();
    for (uint8_t i = 0; i < count; i++) {
        const usb_net::NetDevice* dev = usb_net::get_device(i);
        if (dev && dev->link == usb_net::LINK_UP) return true;
    }

    return false;
}

static bool audio_provider_available()
{
    return pci_audio::controller_count() > 0 || usb_audio::device_count() > 0;
}

static bool audio_muted()
{
    if (pci_audio::controller_count() > 0) return pci_audio::get_mute(0);
    if (usb_audio::device_count() > 0) return usb_audio::get_mute(0);
    return false;
}

static void draw_network_widget(TaskbarWidget& widget)
{
    uint32_t x = widget.bounds.x;
    uint32_t y = widget.bounds.y;
    bool linkUp = network_link_up();
    bool configured = ipv4::is_configured();
    uint32_t barColor = linkUp ? (configured ? rgb(100, 200, 100) : rgb(200, 170, 80)) : rgb(110, 110, 120);

    for (int i = 0; i < 4; i++) {
        uint32_t barH = 4 + (uint32_t)i * 3;
        uint32_t barW = 3;
        uint32_t bx = x + (uint32_t)i * 4;
        uint32_t by = y + kTrayIconSize - barH;
        if (linkUp || i == 0) framebuffer::fill_rect(bx, by, barW, barH, barColor);
        else draw_rect(bx, by, barW, barH, barColor);
    }
}

static void draw_volume_widget(TaskbarWidget& widget)
{
    uint32_t x = widget.bounds.x;
    uint32_t y = widget.bounds.y;
    bool muted = audio_muted();
    uint32_t speakerColor = muted ? rgb(130, 130, 140) : rgb(180, 180, 190);
    uint32_t waveColor = muted ? rgb(110, 70, 70) : rgb(130, 130, 150);

    framebuffer::fill_rect(x + 2, y + 5, 4, 6, speakerColor);
    for (int i = 0; i < 5; i++) {
        framebuffer::fill_rect(x + 6, y + 4 - i/2, 1, 8 + i, speakerColor);
    }

    if (muted) {
        for (int i = 0; i < 8; i++) {
            framebuffer::put_pixel(x + 11 + (uint32_t)i / 2, y + 4 + (uint32_t)i, waveColor);
            framebuffer::put_pixel(x + 14 - (uint32_t)i / 2, y + 4 + (uint32_t)i, waveColor);
        }
    } else {
        vline(x + 12, y + 4, 8, waveColor);
        vline(x + 14, y + 2, 12, rgb(100, 100, 120));
    }
}

static void click_volume_widget(TaskbarWidget& widget)
{
    (void)widget;
    if (pci_audio::controller_count() > 0) {
        pci_audio::set_mute(0, !pci_audio::get_mute(0));
        s_needsRedraw = true;
        return;
    }
    if (usb_audio::device_count() > 0) {
        usb_audio::set_mute(0, !usb_audio::get_mute(0));
        s_needsRedraw = true;
    }
}

static void draw_clock_widget(TaskbarWidget& widget)
{
    char timeStr[6];
    char dateStr[11];
    format_time_string(timeStr, sizeof(timeStr));
    format_date_string(dateStr, sizeof(dateStr));

    if (is_vertical_taskbar(s_taskbarDockPosition)) {
        draw_text_centered(widget.bounds.x, widget.bounds.y + 4, widget.bounds.w, 12, timeStr, rgb(210, 210, 220), 1);
        draw_text_centered(widget.bounds.x, widget.bounds.y + 18, widget.bounds.w, 12, dateStr, rgb(160, 160, 175), 1);
        return;
    }

    uint32_t timeY = widget.bounds.y + 6;
    draw_text_centered(widget.bounds.x, timeY, widget.bounds.w, kTaskbarH / 2 - 4, timeStr, rgb(210, 210, 220), 1);
    uint32_t dateY = widget.bounds.y + kTaskbarH / 2 + 2;
    draw_text_centered(widget.bounds.x, dateY, widget.bounds.w, kTaskbarH / 2 - 4, dateStr, rgb(160, 160, 175), 1);
}

static void init_taskbar_widgets()
{
    TaskbarWidget& search = s_taskbarWidgets[(int)TaskbarWidgetType::Search];
    search = {TaskbarWidgetType::Search, search_provider_available(), false, {0, 0, 0, 0}, "search provider unavailable", nullptr, nullptr, nullptr};
    log_taskbar_widget(TaskbarWidgetType::Search, search.visible, search.visible ? "search provider available" : search.hiddenReason);

    TaskbarWidget& clock = s_taskbarWidgets[(int)TaskbarWidgetType::Clock];
    clock = {TaskbarWidgetType::Clock, s_timeAvailable, false, {0, 0, 0, 0}, "RTC/time service unavailable", draw_clock_widget, nullptr, nullptr};
    log_taskbar_widget(TaskbarWidgetType::Clock, clock.visible, clock.visible ? "RTC/CMOS time available" : clock.hiddenReason);

    TaskbarWidget& network = s_taskbarWidgets[(int)TaskbarWidgetType::Network];
    network = {TaskbarWidgetType::Network, network_provider_available(), false, {0, 0, 0, 0}, "no network provider available", draw_network_widget, nullptr, nullptr};
    log_taskbar_widget(TaskbarWidgetType::Network, network.visible, network.visible ? "network provider available" : network.hiddenReason);

    TaskbarWidget& volume = s_taskbarWidgets[(int)TaskbarWidgetType::Volume];
    volume = {TaskbarWidgetType::Volume, audio_provider_available(), true, {0, 0, 0, 0}, "audio provider unavailable", draw_volume_widget, click_volume_widget, nullptr};
    log_taskbar_widget(TaskbarWidgetType::Volume, volume.visible, volume.visible ? "audio provider available" : volume.hiddenReason);

    TaskbarWidget& battery = s_taskbarWidgets[(int)TaskbarWidgetType::Battery];
    battery = {TaskbarWidgetType::Battery, battery_provider_available(), false, {0, 0, 0, 0}, "no battery provider available", nullptr, nullptr, nullptr};
    log_taskbar_widget(TaskbarWidgetType::Battery, battery.visible, battery.visible ? "battery provider available" : battery.hiddenReason);

    s_taskbarWidgetsInitialized = true;
}

static uint32_t taskbar_widget_width(TaskbarWidgetType type)
{
    TaskbarWidget& widget = s_taskbarWidgets[(int)type];
    if (!widget.visible) return 0;
    if (is_vertical_taskbar(s_taskbarDockPosition)) return kTaskbarH > 8 ? kTaskbarH - 8 : kTaskbarH;
    if (type == TaskbarWidgetType::Search) return kSearchBoxW;
    if (type == TaskbarWidgetType::Clock) return 70;
    return kTrayIconSize;
}

static uint32_t taskbar_tray_width()
{
    uint32_t count = 0;
    for (int i = (int)TaskbarWidgetType::Network; i <= (int)TaskbarWidgetType::Battery; i++) {
        if (s_taskbarWidgets[i].visible) count++;
    }

    if (count == 0) return 0;
    return 8 + count * kTrayIconSize + (count - 1) * kTrayIconGap;
}

static void draw_system_tray(uint32_t trayX, uint32_t taskbarY)
{
    if (is_vertical_taskbar(s_taskbarDockPosition)) return;

    uint32_t trayW = taskbar_tray_width();
    if (trayW == 0) return;

    uint32_t iconY = taskbarY + (kTaskbarH - kTrayIconSize) / 2;
    vline(trayX - 2, taskbarY + 4, kTaskbarH - 8, rgb(80, 80, 90));

    uint32_t cx = trayX + 4;
    for (int i = (int)TaskbarWidgetType::Network; i <= (int)TaskbarWidgetType::Battery; i++) {
        TaskbarWidget& widget = s_taskbarWidgets[i];
        if (!widget.visible || !widget.render) continue;
        widget.bounds = {cx, iconY, kTrayIconSize, kTrayIconSize};
        widget.render(widget);
        cx += kTrayIconSize + kTrayIconGap;
    }
}

static bool handle_taskbar_widget_click(int32_t mx, int32_t my)
{
    for (int i = 0; i < (int)TaskbarWidgetType::Count; i++) {
        TaskbarWidget& widget = s_taskbarWidgets[i];
        if (!widget.visible || !widget.enabled || !widget.click) continue;
        if ((uint32_t)mx >= widget.bounds.x && (uint32_t)mx < widget.bounds.x + widget.bounds.w &&
            (uint32_t)my >= widget.bounds.y && (uint32_t)my < widget.bounds.y + widget.bounds.h) {
            widget.click(widget);
            return true;
        }
    }

    return false;
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

static bool is_taskbar_drag_area(int32_t mx, int32_t my)
{
    DesktopRect tb = get_current_taskbar_rect();
    if (!point_in_rect(mx, my, tb)) return false;
    if (point_in_rect(mx, my, get_start_button_rect())) return false;
    if (point_in_rect(mx, my, get_show_desktop_rect())) return false;

    if (s_terminalBtnVisible && shell::is_open()) {
        DesktopRect terminal = {s_terminalBtnX, s_terminalBtnY, s_terminalBtnW, s_terminalBtnH};
        if (point_in_rect(mx, my, terminal)) return false;
    }

    for (int i = 0; i < (int)TaskbarWidgetType::Count; i++) {
        TaskbarWidget& widget = s_taskbarWidgets[i];
        if (!widget.visible) continue;
        DesktopRect bounds = {widget.bounds.x, widget.bounds.y, widget.bounds.w, widget.bounds.h};
        if (point_in_rect(mx, my, bounds)) return false;
    }

    return true;
}

static void draw_taskbar_buttons(uint32_t startX, uint32_t tbY, uint32_t maxX)
{
    if (is_vertical_taskbar(s_taskbarDockPosition)) {
        s_terminalBtnVisible = false;
        if (!shell::is_open()) return;

        DesktopRect tb = get_current_taskbar_rect();
        uint32_t btnX = tb.x + 4;
        uint32_t btnY = tbY;
        uint32_t btnW = tb.w > 8 ? tb.w - 8 : tb.w;
        uint32_t btnH = kTaskbarBtnH;
        if (btnY + btnH > tb.y + tb.h) return;

        s_terminalBtnX = btnX;
        s_terminalBtnY = btnY;
        s_terminalBtnW = btnW;
        s_terminalBtnH = btnH;
        s_terminalBtnVisible = true;

        bool isActive = !s_shellMinimized && s_shellActive;
        uint32_t bgColor = isActive ? rgb(70, 100, 150) : rgb(55, 58, 70);
        framebuffer::fill_rect(btnX, btnY, btnW, btnH, bgColor);
        if (isActive) framebuffer::fill_rect(btnX + 2, btnY + btnH - 3, btnW - 4, 2, rgb(100, 160, 240));
        framebuffer::fill_rect(btnX + (btnW > 14 ? (btnW - 14) / 2 : 0), btnY + 4, 14, 14, rgb(120, 180, 80));
        draw_text_centered(btnX, btnY + 18, btnW, btnH > 18 ? btnH - 18 : btnH, "Term", rgb(230, 230, 240), 1);
        return;
    }

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
    apply_taskbar_layout();
    DesktopRect tb = get_current_taskbar_rect();
    bool vertical = is_vertical_taskbar(s_taskbarDockPosition);

    // Taskbar background (dark gradient)
    uint32_t tbTop = rgb(45, 45, 55);
    uint32_t tbBot = rgb(30, 30, 38);
    uint32_t gradientLen = vertical ? tb.w : tb.h;
    for (uint32_t i = 0; i < gradientLen; i++) {
        uint32_t c = lerp_color(tbTop, tbBot, i, gradientLen > 1 ? gradientLen - 1 : 1);
        if (vertical) framebuffer::fill_rect(tb.x + i, tb.y, 1, tb.h, c);
        else framebuffer::fill_rect(tb.x, tb.y + i, tb.w, 1, c);
    }

    if (vertical) vline(tb.x, tb.y, tb.h, rgb(70, 70, 85));
    else hline(tb.x, tb.y, tb.w, rgb(70, 70, 85));

    // Start button - draw image centered in button area, with tint when menu open
    DesktopRect start = get_start_button_rect();
    uint32_t btnColor = s_startMenuOpen ? rgb(70, 100, 150) : rgb(50, 70, 110);
    uint32_t btnBorder = s_startMenuOpen ? rgb(100, 140, 200) : rgb(90, 120, 180);
    framebuffer::fill_rect(start.x, start.y, start.w, start.h, btnColor);
    draw_rect(start.x, start.y, start.w, start.h, btnBorder);
    // Draw the start button image, centered within the button
    {
        uint32_t imgX = start.x + (start.w > kStartBtnImgW ? (start.w - kStartBtnImgW) / 2 : 0);
        uint32_t imgY = start.y + (start.h > kStartBtnImgH ? (start.h - kStartBtnImgH) / 2 : 0);
        framebuffer::blit_alpha(kStartBtnImg, imgX, imgY, kStartBtnImgW, kStartBtnImgH);
    }

    if (vertical) {
        uint32_t itemY = start.y + start.h + 8;
        draw_taskbar_buttons(tb.x + 4, itemY, tb.x + tb.w - 4);

        TaskbarWidget& clockWidget = s_taskbarWidgets[(int)TaskbarWidgetType::Clock];
        if (clockWidget.visible && clockWidget.render && tb.h > 82) {
            clockWidget.bounds = {tb.x + 2, tb.y + tb.h - 76, tb.w > 4 ? tb.w - 4 : tb.w, 34};
            clockWidget.render(clockWidget);
        }

        DesktopRect sd = get_show_desktop_rect();
        framebuffer::fill_rect(sd.x, sd.y, sd.w, sd.h, rgb(50, 50, 60));
        hline(sd.x + 4, sd.y, sd.w > 8 ? sd.w - 8 : sd.w, rgb(70, 75, 90));
        return;
    }

    // Search is hidden unless a real search provider exists.
    uint32_t searchX = start.x + start.w + 8;
    uint32_t taskBtnStart = searchX;
    TaskbarWidget& searchWidget = s_taskbarWidgets[(int)TaskbarWidgetType::Search];
    if (searchWidget.visible) {
        searchWidget.bounds = {searchX, tb.y + (kTaskbarH - kSearchBoxH) / 2, kSearchBoxW, kSearchBoxH};
        if (searchWidget.render) searchWidget.render(searchWidget);
        taskBtnStart = searchX + kSearchBoxW + 8;
    }

    // Calculate right-side reserved area
    uint32_t trayW = taskbar_tray_width();
    uint32_t clockW = taskbar_widget_width(TaskbarWidgetType::Clock);
    uint32_t rightReserved = kShowDesktopW + 12;
    if (trayW > 0) rightReserved += trayW + 10;
    if (clockW > 0) rightReserved += clockW + 6;
    uint32_t taskBtnMaxX = tb.x + tb.w - rightReserved;

    draw_taskbar_buttons(taskBtnStart, tb.y, taskBtnMaxX);

    uint32_t rightX = tb.x + tb.w - kShowDesktopW;
    if (trayW > 0) {
        uint32_t trayX = rightX - trayW;
        draw_system_tray(trayX, tb.y);
        rightX = trayX - 10;
    }

    TaskbarWidget& clockWidget = s_taskbarWidgets[(int)TaskbarWidgetType::Clock];
    if (clockWidget.visible && clockWidget.render) {
        clockWidget.bounds = {rightX - clockW, tb.y, clockW, kTaskbarH};
        clockWidget.render(clockWidget);
    }

    // Show Desktop button (thin sliver on far right)
    uint32_t sdX = tb.x + tb.w - kShowDesktopW;
    framebuffer::fill_rect(sdX, tb.y, kShowDesktopW, kTaskbarH, rgb(50, 50, 60));
    // Separator before show desktop
    vline(sdX, tb.y + 4, kTaskbarH - 8, rgb(70, 75, 90));
    
    // Subtle vertical line pattern in show desktop area
    vline(sdX + 2, tb.y + 10, kTaskbarH - 20, rgb(60, 60, 70));
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
    // Use kStartMenuAppCount (not visibleRows) so menuH/menuY stays consistent
    // with get_start_menu_geometry() used by hit-testing — otherwise the menu
    // renders at a different Y than the hover/click hit-test expects.
    uint32_t bodyH = (uint32_t)kStartMenuAppCount * kStartMenuRowH;
    uint32_t rightBodyH = (uint32_t)kStartMenuRightCount * kStartMenuRowH;
    uint32_t maxBodyH = bodyH > rightBodyH ? bodyH : rightBodyH;
    uint32_t menuH = headerH + maxBodyH + footerH;
    DesktopRect work = get_current_work_area();
    DesktopRect start = get_start_button_rect();
    uint32_t menuX;
    uint32_t menuY;
    if (is_vertical_taskbar(s_taskbarDockPosition)) {
        menuX = (s_taskbarDockPosition == TaskbarDockPosition::Left) ? work.x : (work.w > kStartMenuW ? work.w - kStartMenuW : 0);
        menuY = start.y;
        if (menuY + menuH > work.y + work.h)
            menuY = work.h > menuH ? work.y + work.h - menuH : work.y;
    } else {
        menuX = start.x;
        menuY = (s_taskbarDockPosition == TaskbarDockPosition::Top) ? work.y : (work.h > menuH ? work.y + work.h - menuH : work.y);
    }
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
        const char* appName = "";
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

        uint32_t iconSize = s_startMenuIconSize > 0 ? s_startMenuIconSize : 16;
        uint32_t iconX = leftX + 10;
        uint32_t iconY = itemY + (kStartMenuRowH > iconSize ? (kStartMenuRowH - iconSize) / 2 : 0);
        if (!draw_themed_start_menu_icon(iconX, iconY, appName)) {
            framebuffer::fill_rect(iconX, iconY, iconSize, iconSize, appColor);
            draw_rect(iconX, iconY, iconSize, iconSize, rgb(160, 160, 180));
        }
        
        // Pin indicator (star) if pinned
        if (isPinned) {
            draw_text(leftX + 20, itemY + 4, "*", rgb(255, 220, 80), 1);
        }

        // App name
        uint32_t textColor = (itemIndex == s_startMenuSelection || i == s_clickedMenuLeft || i == s_hoverMenuLeft)
            ? rgb(255, 255, 255) : rgb(210, 210, 225);
        draw_text(iconX + iconSize + 6, itemY + 6, appName, textColor, 1);
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

        uint32_t iconSize = s_startMenuIconSize > 0 ? s_startMenuIconSize : 16;
        uint32_t iconX = rightX + 8;
        uint32_t iconY = itemY + (kStartMenuRowH > iconSize ? (kStartMenuRowH - iconSize) / 2 : 0);
        if (!draw_themed_start_menu_icon(iconX, iconY, s_startMenuRight[i].label)) {
            framebuffer::fill_rect(iconX, iconY, iconSize, iconSize, s_startMenuRight[i].color);
            draw_rect(iconX, iconY, iconSize, iconSize, rgb(140, 140, 160));
        }

        // Label
        uint32_t rTextColor = (i == s_clickedMenuRight || i == s_hoverMenuRight)
            ? rgb(255, 255, 255) : rgb(200, 200, 220);
        draw_text(iconX + iconSize + 6, itemY + 6, s_startMenuRight[i].label, rTextColor, 1);
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

    uint32_t menuH;
    uint32_t mx;
    uint32_t my;
    get_context_menu_geometry(mx, my, menuH);
    int hoveredItem = s_rightClickHover;

    // Background with shadow
    framebuffer::fill_rect(mx + 2, my + 2, kContextMenuW, menuH, rgb(20, 20, 25));
    framebuffer::fill_rect(mx, my, kContextMenuW, menuH, rgb(45, 45, 55));
    draw_rect(mx, my, kContextMenuW, menuH, rgb(80, 90, 110));

    for (int i = 0; i < kContextMenuCount; i++) {
        uint32_t itemY = my + kContextMenuPad + (uint32_t)i * kContextMenuItemH;

        if (i == hoveredItem) {
            framebuffer::fill_rect(mx + 1, itemY, kContextMenuW - 2, kContextMenuItemH, rgb(45, 95, 180));
            draw_rect(mx + 2, itemY + 1, kContextMenuW - 4, kContextMenuItemH - 2, rgb(120, 165, 230));
        } else if (i % 2 == 0) {
            framebuffer::fill_rect(mx + 1, itemY, kContextMenuW - 2, kContextMenuItemH, rgb(48, 48, 58));
        }

        draw_text(mx + 12, itemY + (kContextMenuItemH - kGlyphH) / 2,
                  s_contextMenuItems[i], i == hoveredItem ? rgb(255, 255, 255) : rgb(210, 210, 225), 1);

        // Separator after "Personalize" (index 2)
        if (i == 2) {
            hline(mx + 4, itemY + kContextMenuItemH - 1, kContextMenuW - 8, rgb(60, 65, 80));
        }
    }
}

static void get_context_menu_geometry(uint32_t& menuX, uint32_t& menuY, uint32_t& menuH)
{
    menuH = (uint32_t)kContextMenuCount * kContextMenuItemH + kContextMenuPad * 2;
    menuX = s_rightClickX;
    menuY = s_rightClickY;

    if (s_screenW > kContextMenuW && menuX + kContextMenuW > s_screenW)
        menuX = s_screenW - kContextMenuW;
    if (s_screenH > kTaskbarH + menuH && menuY + menuH > s_screenH - kTaskbarH)
        menuY = s_screenH - kTaskbarH - menuH;
}

static int hit_test_context_menu(int32_t mx, int32_t my)
{
    if (!s_rightClickMenuOpen || mx < 0 || my < 0) return -1;

    uint32_t menuX, menuY, menuH;
    get_context_menu_geometry(menuX, menuY, menuH);

    if ((uint32_t)mx < menuX || (uint32_t)mx >= menuX + kContextMenuW ||
        (uint32_t)my < menuY + kContextMenuPad || (uint32_t)my >= menuY + menuH - kContextMenuPad) {
        return -1;
    }

    int idx = ((int32_t)my - (int32_t)menuY - (int32_t)kContextMenuPad) / (int32_t)kContextMenuItemH;
    return (idx >= 0 && idx < kContextMenuCount) ? idx : -1;
}

static void handle_context_menu_command(int item)
{
    switch (item) {
        case 0: // Refresh
            refresh_desktop_icons();
            refresh_start_menu_list();
            s_notification.title = "Desktop";
            s_notification.message = "Refreshed";
            s_notification.visible = true;
            s_notification.showTime = s_tickCounter;
            break;
        case 1: // Display Settings
            launch_app("DisplayOptions");
            break;
        case 2: // Personalize
            launch_app("DisplayOptions");
            break;
        case 3: { // New Folder
            vfs::Status status = vfs::mkdir("/New Folder");
            s_notification.title = "New Folder";
            if (status == vfs::VFS_OK) {
                s_notification.message = "Created on desktop";
            } else if (status == vfs::VFS_ERR_EXISTS) {
                s_notification.message = "Already exists";
            } else {
                s_notification.message = "Not supported by filesystem";
            }
            s_notification.visible = true;
            s_notification.showTime = s_tickCounter;
            break;
        }
        case 4: // Open Terminal
            open_terminal();
            break;
        case 5: // TaskManager
            launch_app("TaskManager");
            break;
        default:
            break;
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
    {"Display",        0xFF606878},
    {"Device Mgr",     0xFF4690C8},
    {"Network",        0xFF50B478},
};
static const int kControlPanelItemCount = 3;

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

// Hit test Control Panel: returns item index, 3=close, -1=none
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
        return 3;
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
    DesktopRect work = get_current_work_area();
    
    if (shell::get_state() == shell::ShellState::Fullscreen || s_shellMaximized) {
        g.x = work.x;
        g.y = work.y;
        g.w = work.w;
        g.h = work.h;
    } else {
        // Use custom size if set, otherwise default
        g.w = (s_shellW > 0) ? (uint32_t)s_shellW : kShellDefaultW;
        g.h = (s_shellH > 0) ? (uint32_t)s_shellH : kShellDefaultH;
        
        // Use stored position or center if not set
        if (s_shellPosX < 0) {
            g.x = work.x + (work.w > g.w ? (work.w - g.w) / 2 : 0);
        } else {
            g.x = (uint32_t)s_shellPosX;
        }
        if (s_shellPosY < 0) {
            g.y = work.y + (work.h > g.h ? (work.h - g.h) / 2 : 0);
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
    s_notification.showTime = s_tickCounter;
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
    s_notification.showTime = 0;
    s_tickCounter = 0;
    s_lastClickedIcon = -1;
    s_lastClickTime = 0;
    initialize_icon_positions();  // Use new icon management system
    init_time();  // Initialize time only if a real clock source is available
    shell::init();

    s_wallpaperConfig.type = WallpaperType::BuiltIn;
    s_wallpaperConfig.topColor = 0xFF142850;
    s_wallpaperConfig.bottomColor = 0xFF0F121C;
    s_wallpaperConfig.showBranding = false;
    s_wallpaperConfig.showGrid = false;
    s_wallpaperConfig.gridColor = 0xFF192337;
    s_wallpaperConfig.gridSpacing = 100;
    s_wallpaperConfig.wallpaperId = "legacy_blue_flower";
    s_wallpaperConfig.scaleMode = WallpaperScaleMode::Fill;
    load_persisted_wallpaper_scale_mode();
    load_persisted_wallpaper_id();
    
    // Enable double buffering to prevent flickering during window movement
    framebuffer::enable_double_buffering();
    
    // Initialize kernel app framework
    ipc::IpcManager::init();
    apps::registerKernelApps();
    compositor::KernelCompositor::init(s_screenW, s_screenH, kTaskbarH);
    compositor::TaskbarManager::init(s_screenW, s_screenH, kTaskbarH, 
                                     4 + kStartBtnW + 8);
    init_taskbar_widgets();
    
    s_initialized = true;
    desktop_capabilities::log_current(false, false);
}

// Update tick counter (call this from main loop, e.g., every 10ms)
void tick()
{
    s_tickCounter++;
    
    // Update time every ~100 ticks (roughly 1 second if tick is called every 10ms)
    if (s_tickCounter % 100 == 0) {
        time::DateTime now{};
        if (time::get_current_datetime(now)) {
            bool wasAvailable = s_timeAvailable;
            s_timeAvailable = true;
            copy_datetime_from_time_service(now);
            TaskbarWidget& clock = s_taskbarWidgets[(int)TaskbarWidgetType::Clock];
            if (!clock.visible) clock.visible = true;
            if (!wasAvailable) {
                log_taskbar_widget(TaskbarWidgetType::Clock, true, "RTC/CMOS time became available");
            }
        } else {
            update_time();
        }

        if (s_timeAvailable && s_currentTime.minute != s_lastRenderedClockMinute) {
            s_lastRenderedClockMinute = s_currentTime.minute;
            s_needsRedraw = true;
        }
    }
    
    // Auto-hide notifications after 5 seconds (500 ticks at 10ms each)
    if (s_notification.visible && s_notification.showTime > 0) {
        if (s_tickCounter - s_notification.showTime >= 500) {
            s_notification.visible = false;
        }
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

    forget_cursor_save();

    draw_background();
    draw_desktop_icons();
    draw_taskbar();
    draw_notifications();
    draw_shutdown_dialog();
    draw_app_model_dialog();
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
    
    // Draw shell window last if active (so it appears on top)
    if (shellVisible && s_shellActive) {
        draw_shell_window();
    }

    // Draw taskbar buttons for kernel apps after windows so minimized apps remain reachable.
    compositor::TaskbarManager::drawButtons();
    
    // Draw start menu LAST so it appears on top of all windows
    draw_start_menu();

    // Desktop context menu is a top-level overlay and should stay above windows.
    draw_right_click_menu();
    
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
    s_rightClickHover = hit_test_context_menu((int32_t)x, (int32_t)y);
    // Close start menu when context menu is shown
    s_startMenuOpen = false;
}

void close_context_menu()
{
    s_rightClickMenuOpen = false;
    s_rightClickHover = -1;
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
    s_showDesktopActive = false;
    s_showDesktopHidShell = false;
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

static void toggle_show_desktop()
{
    if (s_showDesktopActive) {
        compositor::KernelCompositor::restoreWindowsFromShowDesktop();
        if (s_showDesktopHidShell && shell::is_open()) {
            s_shellMinimized = false;
            s_shellActive = true;
        }
        s_showDesktopActive = false;
        s_showDesktopHidShell = false;
    } else {
        s_showDesktopHidShell = shell::is_open() && !s_shellMinimized;
        if (s_showDesktopHidShell) {
            s_shellMinimized = true;
            s_shellActive = false;
        }
        compositor::KernelCompositor::minimizeWindowsForShowDesktop();
        s_showDesktopActive = true;
    }

    s_startMenuOpen = false;
    s_rightClickMenuOpen = false;
}

int get_running_app_count()
{
    return app::AppManager::getRunningAppCount();
}

void handle_key(uint32_t key)
{
    if (key == shell::KEY_SUPER) {
        serial::puts("[desktop] Super key pressed, toggling Start Menu\n");
        toggle_start_menu();
        draw();
        return;
    }

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
            SelectDesktopIcon(newIcon, false);
            draw();
            return;
        }
    }
}

// ============================================================
// Mouse cursor (12x19 arrow, 1-bit mask)
// ============================================================

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

static void forget_cursor_save()
{
    s_cursorDrawn = false;
}

// Previous button state for edge detection
static uint8_t  s_prevButtons = 0;

// Hit-test desktop icons: returns display index or -1
static int hit_test_icon(int32_t mx, int32_t my)
{
    DesktopRect work = get_current_work_area();
    if (!point_in_rect(mx, my, work)) return -1;

    for (int displayIdx = 0; displayIdx < s_visibleIconCount; displayIdx++) {
        uint32_t cx = (uint32_t)s_iconPosX[displayIdx];
        uint32_t cy = (uint32_t)s_iconPosY[displayIdx];

        if (cx < work.x || cy < work.y || cx + kIconCellW > work.x + work.w || cy + kIconCellH > work.y + work.h) continue;

        if ((uint32_t)mx >= cx && (uint32_t)mx < cx + kIconCellW &&
            (uint32_t)my >= cy && (uint32_t)my < cy + kIconCellH) {
            return displayIdx;
        }
    }
    return -1;
}

static int HitTestDesktopIcon(int32_t mx, int32_t my)
{
    return hit_test_icon(mx, my);
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
    if (desktop_str_eq(label, "AppModel")) {
        open_app_model_viewer();
        app::AppLogger::logLaunch(label, app::LaunchResult::NotAvailable);
        return;
    }
    
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
        s_notification.showTime = s_tickCounter;
        app::AppLogger::logLaunch(label, app::LaunchResult::FailedToInit);
        return;
    }
    
    // App not available in kernel mode - show notification
    s_notification.title = label;
    s_notification.message = "Not available in bare-metal mode";
    s_notification.visible = true;
    s_notification.showTime = s_tickCounter;
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
    DesktopRect work = get_current_work_area();
    DesktopRect start = get_start_button_rect();
    g.headerH = 30;
    g.footerH = 36;
    uint32_t bodyH = (uint32_t)kStartMenuAppCount * kStartMenuRowH;
    uint32_t rightBodyH = (uint32_t)kStartMenuRightCount * kStartMenuRowH;
    g.maxBodyH = bodyH > rightBodyH ? bodyH : rightBodyH;
    g.menuH = g.headerH + g.maxBodyH + g.footerH;
    if (is_vertical_taskbar(s_taskbarDockPosition)) {
        if (s_taskbarDockPosition == TaskbarDockPosition::Left)
            g.menuX = work.x;
        else
            g.menuX = work.w > kStartMenuW ? work.w - kStartMenuW : 0;
        g.menuY = start.y;
        if (g.menuY + g.menuH > work.y + work.h)
            g.menuY = work.h > g.menuH ? work.y + work.h - g.menuH : work.y;
    } else {
        g.menuX = start.x;
        if (s_taskbarDockPosition == TaskbarDockPosition::Top)
            g.menuY = work.y;
        else
            g.menuY = work.h > g.menuH ? work.y + work.h - g.menuH : work.y;
    }
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

    // App Model Demo
    if (s_appModelDialogOpen) {
        uint32_t dlgW = app_model_dialog_width();
        uint32_t dlgH = app_model_dialog_height();
        uint32_t dlgX = app_model_dialog_x();
        uint32_t dlgY = app_model_dialog_y();
        if ((uint32_t)mx >= dlgX && (uint32_t)mx < dlgX + dlgW &&
            (uint32_t)my >= dlgY && (uint32_t)my < dlgY + dlgH) {
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
    int itemRow = static_cast<int>(relY / kStartMenuRowH);

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
    if (desktop_str_eq(label, "AppModel")) {
        s_startMenuOpen = false;
        open_app_model_viewer();
        app::AppLogger::logLaunch(label, app::LaunchResult::NotAvailable);
        return;
    }

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
    s_notification.showTime = s_tickCounter;
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
    for (int row = 0; row < kCursorH; row++) {
        for (int col = 0; col < kCursorW; col++) {
            int32_t px = mx + col;
            int32_t py = my + row;
            if (px >= 0 && px < (int32_t)s_screenW && py >= 0 && py < (int32_t)s_screenH)
                s_cursorSave[row][col] = framebuffer::get_front_pixel((uint32_t)px, (uint32_t)py);
            else
                s_cursorSave[row][col] = 0;
        }
    }
}

static void restore_under_cursor()
{
    if (!s_cursorDrawn) return;

    for (int row = 0; row < kCursorH; row++) {
        for (int col = 0; col < kCursorW; col++) {
            int32_t px = s_lastCursorX + col;
            int32_t py = s_lastCursorY + row;
            if (px >= 0 && px < (int32_t)s_screenW && py >= 0 && py < (int32_t)s_screenH)
                framebuffer::put_front_pixel((uint32_t)px, (uint32_t)py, s_cursorSave[row][col]);
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
    for (int row = 0; row < kCursorH; row++) {
        for (int col = 0; col < kCursorW; col++) {
            uint8_t p = s_cursorBitmap[row][col];
            if (p == 0) continue;
            int32_t px = mx + col;
            int32_t py = my + row;
            if (px >= 0 && px < (int32_t)s_screenW && py >= 0 && py < (int32_t)s_screenH) {
                uint32_t color = (p == 1) ? rgb(0, 0, 0) : rgb(255, 255, 255);
                framebuffer::put_front_pixel((uint32_t)px, (uint32_t)py, color);
            }
        }
    }
    s_cursorDrawn = true;
}

void handle_mouse(int32_t mx, int32_t my, uint8_t buttons)
{
    if (!s_initialized) return;

    s_mouseX = mx;
    s_mouseY = my;

    // Detect button press/release edges
    uint8_t pressed  = buttons & ~s_prevButtons;   // newly pressed
    uint8_t released = s_prevButtons & ~buttons;    // newly released
    s_prevButtons = buttons;

    apply_taskbar_layout();
    DesktopRect workArea = get_current_work_area();
    DesktopRect taskbarRect = get_current_taskbar_rect();

    if (s_taskbarDragging) {
        if (buttons & 0x01) {
            if (is_near_screen_edge(mx, my, s_screenW, s_screenH)) {
                TaskbarDockPosition newPosition = get_nearest_dock_edge(mx, my, s_screenW, s_screenH);
                if (newPosition != s_taskbarDockPosition) {
                    s_taskbarDockPosition = newPosition;
                    apply_taskbar_layout();
                }
            }
            draw();
            draw_cursor(mx, my);
            return;
        }

        if (released & 0x01) {
            s_taskbarDragging = false;
            apply_taskbar_layout();
            draw();
            draw_cursor(mx, my);
            return;
        }
    }

    // ---- Route input to kernel compositor first (GUI apps) ----
    // Check if point is over a compositor window OR if compositor has an active button press
    // (need to send mouse up even if mouse moved outside window bounds)
    // BUT: Start menu and modal dialogs take priority over compositor windows
    bool overStartMenu = is_point_in_start_menu(mx, my);
    bool overModalDialog = is_point_in_modal_dialog(mx, my);
    bool overUIOverlay = overStartMenu || overModalDialog || s_rightClickMenuOpen;
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
    if ((pressed & 0x01) && point_in_rect(mx, my, taskbarRect)) {
        if (point_in_rect(mx, my, get_show_desktop_rect())) {
            toggle_show_desktop();
            draw();
            draw_cursor(mx, my);
            return;
        }

        if (handle_taskbar_widget_click(mx, my)) {
            draw();
            draw_cursor(mx, my);
            return;
        }

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
        
        // Clamp to desktop work area
        if (newX < (int32_t)workArea.x) newX = (int32_t)workArea.x;
        if (newY < (int32_t)workArea.y) newY = (int32_t)workArea.y;
        if (newX + (int32_t)kShellDefaultW > (int32_t)(workArea.x + workArea.w))
            newX = (int32_t)(workArea.x + workArea.w) - (int32_t)kShellDefaultW;
        if (newY + (int32_t)kShellDefaultH > (int32_t)(workArea.y + workArea.h))
            newY = (int32_t)(workArea.y + workArea.h) - (int32_t)kShellDefaultH;
        
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
        
        // Clamp to desktop work area
        ShellWindowGeometry g = get_shell_geometry();
        if ((int32_t)g.x + newW > (int32_t)(workArea.x + workArea.w))
            newW = (int32_t)(workArea.x + workArea.w) - (int32_t)g.x;
        if ((int32_t)g.y + newH > (int32_t)(workArea.y + workArea.h))
            newH = (int32_t)(workArea.y + workArea.h) - (int32_t)g.y;
        
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

    // ---- Handle desktop rectangle selection (left button held on empty desktop) ----
    if ((s_selectionDragPending || s_selectionDragActive) && (buttons & 0x01)) {
        s_selectionCurrentX = mx;
        s_selectionCurrentY = my;

        if (!s_selectionDragActive) {
            int32_t dx = mx - s_selectionStartX;
            int32_t dy = my - s_selectionStartY;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx > kDragThreshold || dy > kDragThreshold) {
                s_selectionDragActive = true;
                log_selection_change("marquee started");
            }
        }

        if (s_selectionDragActive) {
            SelectIconsInRectangle(s_selectionStartX, s_selectionStartY,
                                   s_selectionCurrentX, s_selectionCurrentY,
                                   s_selectionDragAdditive);
            draw();
            draw_cursor(mx, my);
            return;
        }
    }

    // ---- Finalize desktop rectangle selection ----
    if ((s_selectionDragPending || s_selectionDragActive) && (released & 0x01)) {
        if (s_selectionDragActive) {
            SelectIconsInRectangle(s_selectionStartX, s_selectionStartY,
                                   s_selectionCurrentX, s_selectionCurrentY,
                                   s_selectionDragAdditive);
            log_selection_change("marquee finished");
        }
        s_selectionDragPending = false;
        s_selectionDragActive = false;
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
        if (s_dragStarted && s_dragIconIndex >= 0 && s_dragIconIndex < s_visibleIconCount) {
            // Commit the same mouse delta to every icon that was selected
            // when dragging began, preserving the group's relative spacing.
            int32_t deltaX = mx - s_dragStartMouseX;
            int32_t deltaY = my - s_dragStartMouseY;
            int32_t groupLeft = 0;
            int32_t groupTop = 0;
            int32_t groupRight = 0;
            int32_t groupBottom = 0;
            bool haveGroupBounds = false;
            for (int displayIdx = 0; displayIdx < s_visibleIconCount; displayIdx++) {
                if (!s_dragSelectedIcons[displayIdx]) continue;
                int32_t left = s_dragOriginalIconX[displayIdx] + deltaX;
                int32_t top = s_dragOriginalIconY[displayIdx] + deltaY;
                int32_t right = left + (int32_t)kIconCellW;
                int32_t bottom = top + (int32_t)kIconCellH;
                if (!haveGroupBounds) {
                    groupLeft = left;
                    groupTop = top;
                    groupRight = right;
                    groupBottom = bottom;
                    haveGroupBounds = true;
                } else {
                    if (left < groupLeft) groupLeft = left;
                    if (top < groupTop) groupTop = top;
                    if (right > groupRight) groupRight = right;
                    if (bottom > groupBottom) groupBottom = bottom;
                }
            }

            // Clamp the selected group as one unit to avoid dropping any
            // selected icon outside the visible desktop work area.
            if (haveGroupBounds) {
                if (groupLeft < (int32_t)workArea.x) deltaX += (int32_t)workArea.x - groupLeft;
                if (groupTop < (int32_t)workArea.y) deltaY += (int32_t)workArea.y - groupTop;
                if (groupRight > (int32_t)(workArea.x + workArea.w))
                    deltaX -= groupRight - (int32_t)(workArea.x + workArea.w);
                if (groupBottom > (int32_t)(workArea.y + workArea.h))
                    deltaY -= groupBottom - (int32_t)(workArea.y + workArea.h);
            }

            for (int displayIdx = 0; displayIdx < s_visibleIconCount; displayIdx++) {
                if (!s_dragSelectedIcons[displayIdx]) continue;
                s_iconPosX[displayIdx] = s_dragOriginalIconX[displayIdx] + deltaX;
                s_iconPosY[displayIdx] = s_dragOriginalIconY[displayIdx] + deltaY;
            }
            
            // Save icon positions to VFS
            save_icon_positions();
        } else if (!s_dragStarted && s_dragIconIndex >= 0 && s_dragIconIndex < s_visibleIconCount) {
            // Click without drag - just select the icon (launch happens on double-click)
            s_selectedIcon = s_dragIconIndex;
        }

        clear_icon_drag_state();
        draw();
        draw_cursor(mx, my);
        return;
    }

    // Left button press
    if (pressed & 0x01) {
        // Desktop context menu consumes the next left click. If the click hits
        // an item, run it before closing; otherwise just dismiss the menu.
        if (s_rightClickMenuOpen) {
            int menuItem = hit_test_context_menu(mx, my);
            s_rightClickMenuOpen = false;
            s_rightClickHover = -1;
            if (menuItem >= 0) {
                handle_context_menu_command(menuItem);
            }
            draw();
            draw_cursor(mx, my);
            return;
        }

        // Check for notification close button click first
        if (s_notification.visible) {
            uint32_t toastW = 260;
            uint32_t toastX = s_screenW - toastW - 12;
            uint32_t toastY = 12;
            uint32_t closeX = toastX + toastW - 16;
            uint32_t closeY = toastY + 4;
            
            // Close button is roughly 12x12 pixels around the 'x' character
            if ((uint32_t)mx >= closeX && (uint32_t)mx < closeX + 12 &&
                (uint32_t)my >= closeY && (uint32_t)my < closeY + 12) {
                s_notification.visible = false;
                draw();
                draw_cursor(mx, my);
                return;
            }
        }
        
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

        // Handle App Model Demo dialog clicks
        if (s_appModelDialogOpen) {
            int btn = hit_test_app_model_dialog(mx, my);
            if (btn == 0) {
                s_appModelDialogOpen = false;
                draw();
                draw_cursor(mx, my);
                return;
            }
            uint32_t dlgW = app_model_dialog_width();
            uint32_t dlgH = app_model_dialog_height();
            uint32_t dlgX = app_model_dialog_x();
            uint32_t dlgY = app_model_dialog_y();
            if ((uint32_t)mx < dlgX || (uint32_t)mx >= dlgX + dlgW ||
                (uint32_t)my < dlgY || (uint32_t)my >= dlgY + dlgH) {
                s_appModelDialogOpen = false;
                draw();
                draw_cursor(mx, my);
                return;
            }
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
                s_notification.showTime = s_tickCounter;
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
            if (btn == 3) {
                // Close
                s_controlPanelOpen = false;
                draw();
                draw_cursor(mx, my);
                return;
            } else if (btn == 0) {
                // Display Options
                s_controlPanelOpen = false;
                launch_app("DisplayOptions");
                draw();
                draw_cursor(mx, my);
                return;
            } else if (btn == 1) {
                // Device Manager
                s_controlPanelOpen = false;
                s_deviceManagerOpen = true;
                draw();
                draw_cursor(mx, my);
                return;
            } else if (btn == 2) {
                // Network Adapters
                s_controlPanelOpen = false;
                s_networkAdaptersOpen = true;
                draw();
                draw_cursor(mx, my);
                return;
            }
            return;
        }

        // Start button area follows the current taskbar dock position.
        if (point_in_rect(mx, my, get_start_button_rect())) {
            toggle_start_menu();
            draw();
            draw_cursor(mx, my);
            return;
        }

        if (is_taskbar_drag_area(mx, my)) {
            s_taskbarDragging = true;
            s_startMenuOpen = false;
            s_rightClickMenuOpen = false;
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
                        s_notification.showTime = s_tickCounter;
                        draw();
                        draw_cursor(mx, my);
                        perform_restart();
                        // If we return here, restart failed
                        return;
                    case FOOTER_SLEEP:
                        s_notification.title = "Sleep";
                        s_notification.message = "System entering sleep mode...";
                        s_notification.visible = true;
                        s_notification.showTime = s_tickCounter;
                        draw();
                        draw_cursor(mx, my);
                        perform_sleep();
                        // System wakes up here after sleep
                        s_notification.title = "Awake";
                        s_notification.message = "System resumed from sleep";
                        s_notification.visible = true;
                        s_notification.showTime = s_tickCounter;
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
        int iconIdx = HitTestDesktopIcon(mx, my);
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
            
            bool ctrlDown = is_ctrl_modifier_down();
            bool shiftDown = is_shift_modifier_down();
            bool wasSelected = is_display_icon_selected(iconIdx);
            if (shiftDown) {
                int anchorDisplayIndex = display_index_for_icon_id(s_lastSelectedIconId);
                SelectDesktopIconRange(anchorDisplayIndex >= 0 ? anchorDisplayIndex : iconIdx, iconIdx);
            } else if (ctrlDown) {
                ToggleDesktopIconSelection(iconIdx);
            } else if (wasSelected) {
                // Pressing an already-selected icon starts a possible group drag;
                // do not collapse the selection unless the click was on an unselected icon.
                s_selectedIcon = iconIdx;
                s_focusedSelectedIconId = s_visibleIconIndices[iconIdx];
            } else {
                SelectDesktopIcon(iconIdx, false);
            }

            // Start drag tracking (actual drag begins after threshold)
            if (is_display_icon_selected(iconIdx)) {
                s_dragging = true;
                s_dragStarted = false;
                s_dragIconIndex = iconIdx;
                s_dragStartMouseX = mx;
                s_dragStartMouseY = my;
                s_dragCurrentX = mx;
                s_dragCurrentY = my;
                snapshot_icon_drag_positions();
            }
            draw();
            draw_cursor(mx, my);
            return;
        }

        // Notification dismiss: check before empty desktop selection handling.
        if (s_notification.visible) {
            uint32_t toastW = 280;
            uint32_t toastH = 64;
            uint32_t toastX = s_screenW - toastW - 16;
            uint32_t toastY = workArea.y + workArea.h > toastH + 12 ? workArea.y + workArea.h - toastH - 12 : workArea.y;
            if ((uint32_t)mx >= toastX && (uint32_t)mx <= toastX + toastW &&
                (uint32_t)my >= toastY && (uint32_t)my <= toastY + toastH) {
                dismiss_notification();
                draw();
                draw_cursor(mx, my);
                return;
            }
        }

        // Click on empty desktop area: clear selection unless Ctrl is held, then
        // prepare a marquee selection if the pointer moves beyond the drag threshold.
        if (is_point_in_work_area(mx, my)) {
            bool ctrlDown = is_ctrl_modifier_down();
            if (!ctrlDown) {
                ClearDesktopIconSelection();
            }
            snapshot_selection_base();
            s_selectionDragPending = true;
            s_selectionDragActive = false;
            s_selectionDragAdditive = ctrlDown;
            s_selectionStartX = mx;
            s_selectionStartY = my;
            s_selectionCurrentX = mx;
            s_selectionCurrentY = my;
            draw();
            draw_cursor(mx, my);
            return;
        }

        // Notification dismiss: check if clicking the notification toast
        if (s_notification.visible) {
            uint32_t toastW = 280;
            uint32_t toastH = 64;
            uint32_t toastX = s_screenW - toastW - 16;
            uint32_t toastY = workArea.y + workArea.h > toastH + 12 ? workArea.y + workArea.h - toastH - 12 : workArea.y;
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
        if (is_point_in_work_area(mx, my)) {
            show_context_menu((uint32_t)mx, (uint32_t)my);
            draw();
            draw_cursor(mx, my);
            return;
        }
    }

    // Context menu hover tracking
    if (s_rightClickMenuOpen) {
        int newHover = hit_test_context_menu(mx, my);
        if (newHover != s_rightClickHover) {
            s_rightClickHover = newHover;
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
        if (newHover != 3 && newHover != s_controlPanelHover) {  // Don't track close button as hover
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

    // App Model Demo hover tracking
    if (s_appModelDialogOpen) {
        int newHover = hit_test_app_model_dialog(mx, my);
        if (newHover != s_appModelDialogHover) {
            s_appModelDialogHover = newHover;
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
