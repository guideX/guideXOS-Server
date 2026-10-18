//
//
// guideXOS Desktop Environment - Kernel Framebuffer Renderer
//
// Draws the complete desktop UI directly to the framebuffer:
//   - Gradient wallpaper with branding
//   - Taskbar with start button, clock, system tray
//   - Desktop icons
//   - Start menu (toggled)
//   - GUI applications via kernel compositor
//
// Supports full GUI app functionality in UEFI/bare-metal mode
// without requiring the user-mode server (guideXOSServer).
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_DESKTOP_H
#define KERNEL_DESKTOP_H

#include "kernel/types.h"

namespace kernel {
namespace desktop {

struct SystemDesktopIconVisibility {
    bool showTrash;
    bool showThisSystem;
    bool showFileManager;
    bool showSystemSettings;
};

enum : uint32_t {
    kDesktopAppearanceConfigurationVersion = 1u,
    kDesktopAppearanceBackgroundWallpaper = 1u,
    kDesktopAppearanceBackgroundGradient = 2u,
    kDesktopAppearanceIconTrash = 1u << 0,
    kDesktopAppearanceIconThisSystem = 1u << 1,
    kDesktopAppearanceIconFileManager = 1u << 2,
    kDesktopAppearanceIconSystemSettings = 1u << 3
};

// Value-only view of the active, persisted desktop appearance. It is copied
// by the Settings provider and contains no pointers into desktop state.
struct DesktopAppearanceConfigurationV1 {
    uint32_t structureSize;
    uint32_t version;
    uint64_t revision;
    uint32_t backgroundKind;
    uint32_t gradientTopColor;
    uint32_t gradientBottomColor;
    uint32_t gradientAccentColor;
    uint32_t visibleIconFlags;
    char backgroundId[96];
    char backgroundName[32];
    char backgroundScaleMode[16];
};

static const uint32_t kDesktopBuiltInGradientPresetVersion = 1u;
static const uint32_t kDesktopBuiltInGradientPresetCapacity = 16u;
static const uint32_t kDesktopGradientApplyResultVersion = 1u;
static const uint32_t kDesktopBuiltInWallpaperPresetVersion = 1u;
static const uint32_t kDesktopBuiltInWallpaperPresetCapacity = 16u;

struct DesktopBuiltInGradientPresetV1 {
    uint32_t structureSize;
    uint32_t version;
    uint32_t topColor;
    uint32_t bottomColor;
    uint32_t accentColor;
    char id[96];
    char displayName[32];
};

enum : uint32_t {
    // The fixed image-pack path exists. Apply still performs a full bounded
    // GXIMG read/decode before changing active desktop state.
    kDesktopBuiltInWallpaperAssetPresent = 1u << 0
};

struct DesktopBuiltInWallpaperPresetV1 {
    uint32_t structureSize;
    uint32_t version;
    uint32_t flags;
    char id[96];
    char displayName[32];
};

enum class DesktopGradientApplyStatus : uint32_t {
    Committed = 0,
    NoChange = 1,
    InvalidPreset = 2,
    StaleRevision = 3,
    PersistenceUnavailable = 4,
    PersistenceConflict = 5,
    ApplyFailed = 6,
    SaveFailedRolledBack = 7,
    ReadbackFailedRolledBack = 8,
    RollbackFailed = 9,
    AssetUnavailable = 10
};

enum : uint32_t {
    kDesktopGradientApplyActiveAvailable = 1u << 0,
    kDesktopGradientApplyPersistedAvailable = 1u << 1,
    kDesktopGradientApplyRollbackVerified = 1u << 2
};

struct DesktopGradientApplyResultV1 {
    uint32_t structureSize;
    uint32_t version;
    DesktopGradientApplyStatus status;
    uint32_t flags;
    uint64_t revision;
    char appliedId[96];
    char persistedId[96];
};

struct CpuTelemetrySnapshot {
    bool available = false;
    int utilizationPct = 0;
    uint64_t sampleWindowMs = 0;
    uint64_t busyTimeMs = 0;
    uint64_t idleTimeMs = 0;
    const char* source = "kernelMainLoopIdleBusyWarmup";
};

// ================================================================
// Initialization & Core Loop
// ================================================================

// Initialize desktop state (call once after framebuffer is ready)
// This also initializes the kernel app framework, IPC, and compositor
void init();

// Draw the full desktop to the framebuffer
// Draws background, icons, taskbar, menus, and all GUI app windows
void draw();

// Update tick counter for timing (call from main loop, e.g., every 10ms)
// Also updates IPC, running apps, and taskbar
void tick();

// Bare-metal CPU telemetry helpers fed by the main loop.
void record_cpu_busy_ticks(uint64_t tickCount);
void record_cpu_idle_ticks(uint64_t tickCount);
CpuTelemetrySnapshot cpu_telemetry_snapshot();

// Let a synchronous long-running kernel job cooperatively service input and
// present the desktop without taking ownership of app animation state.
void cooperative_yield();

// Check if redraw is needed (called from main loop)
bool needs_redraw();

// Monotonic visible-desktop invalidation generation consumed by presentation
// backends.  The generation advances when the normal redraw flag is consumed;
// this keeps the existing narrow invalidation bridge without making a backend
// the owner of desktop/window state.
uint64_t redraw_generation();

// Request a normal desktop redraw.  QEMU-only diagnostic presenters may use
// this for a visible overlay update; it remains the desktop's existing redraw
// mechanism and does not route input.
void request_redraw();

// ================================================================
// Input Handling
// ================================================================

// Process mouse input: update cursor position and handle clicks
// Routes input to GUI apps, shell, or desktop as appropriate
void handle_mouse(int32_t mx, int32_t my, uint8_t buttons);

// Process mouse wheel input for the window under the pointer.
void handle_mouse_wheel(int32_t mx, int32_t my, int8_t wheelDelta);

// Process keyboard input: handle key presses
// Routes to focused GUI app, shell, or desktop shortcuts
// Special keys use codes from shell.h (KEY_UP, KEY_DOWN, etc.)
void handle_key(uint32_t key);
// Production PS/2 delivery path carrying the modifier snapshot captured with
// the key-down event. The original overload remains for compatibility.
void handle_key_with_modifiers(uint32_t key, uint32_t modifiers);

// Draw the mouse cursor at the given position (overlay on framebuffer)
void draw_cursor(int32_t mx, int32_t my);

// ================================================================
// UI Controls
// ================================================================

// Toggle start menu open/closed
void toggle_start_menu();

// Check if start menu is visible
bool is_start_menu_open();

// Toggle right-click context menu at position
void show_context_menu(uint32_t x, uint32_t y);

// Close right-click context menu
void close_context_menu();


// Dismiss notification toast
void dismiss_notification();

// ================================================================
// Application Management
// ================================================================

// Open the terminal/shell
void open_terminal();

// Launch an application by name
// In bare-metal mode: uses kernel app framework if available
// Returns true if app was launched or is already running
bool launch_app(const char* appName);

// Record a successful program launch for Start Menu recent history.
// In hosted builds this is a no-op; bare-metal persists to a small VFS file.
void record_recent_program(const char* appName);

// Remove a program from the bare-metal Start Menu recent history.
// In hosted builds this is a no-op; bare-metal persists the updated list immediately.
void remove_recent_program(const char* appName);

// Sync the bare-metal live desktop folder to a successful shell cwd change.
// Returns true if the desktop accepted the new directory.
bool sync_live_directory_from_shell_cwd(const char* cwd);

#if defined(GXOS_BARE_METAL)
// Re-run the bare-metal desktop folder scan once VFS and the desktop backing path are ready.
void refresh_bare_metal_desktop_folders_after_vfs_ready();
#endif

// Create a persistent desktop shortcut/reference to an existing VFS file/folder.
bool pin_filesystem_shortcut_to_desktop(const char* path, bool isDirectory);

// Apply a built-in desktop wallpaper/background id and persist it when possible.
void set_wallpaper_by_id(const char* wallpaperId);

// Return the currently selected built-in wallpaper/background id.
const char* get_wallpaper_id();

// Enumerate and apply the desktop-owned built-in gradient presets. The apply
// call checks the desktop appearance revision and commits through /desktop.cfg.
uint32_t get_builtin_gradient_preset_count();
bool get_builtin_gradient_preset(
    uint32_t index, DesktopBuiltInGradientPresetV1* output);
uint32_t get_builtin_wallpaper_preset_count();
bool get_builtin_wallpaper_preset(
    uint32_t index, DesktopBuiltInWallpaperPresetV1* output);
DesktopGradientApplyStatus apply_builtin_gradient_preset(
    const char* presetId, uint64_t expectedRevision,
    DesktopGradientApplyResultV1* output);
DesktopGradientApplyStatus apply_builtin_wallpaper(
    const char* wallpaperId, uint64_t expectedRevision,
    DesktopGradientApplyResultV1* output);

enum : uint32_t {
    kDesktopIconVisibilityRequestVersion = 1u,
    kDesktopIconVisibilityResultVersion = 1u,
    kDesktopIconTargetTrash = 1u << 0,
    kDesktopIconTargetThisSystem = 1u << 1,
    kDesktopIconTargetFileManager = 1u << 2,
    kDesktopIconTargetSystemSettings = 1u << 3,
    kDesktopIconTargetFileExplorer = kDesktopIconTargetThisSystem | kDesktopIconTargetFileManager,
    kDesktopIconTargetKnownMask = (1u << 4) - 1u
};

enum class DesktopIconVisibilityApplyStatus : uint32_t {
    Committed = 0,
    NoChange = 1,
    InvalidTarget = 2,
    StaleRevision = 3,
    PersistenceUnavailable = 4,
    PersistenceConflict = 5,
    SaveFailedRolledBack = 6,
    ReadbackFailedRolledBack = 7,
    RollbackFailed = 8,
    ApplyFailed = 9
};

enum : uint32_t {
    kDesktopIconVisibilityApplyActiveAvailable = 1u << 0,
    kDesktopIconVisibilityApplyPersistedAvailable = 1u << 1,
    kDesktopIconVisibilityApplyRollbackVerified = 1u << 2
};

struct DesktopIconVisibilityApplyResultV1 {
    uint32_t structureSize;
    uint32_t version;
    DesktopIconVisibilityApplyStatus status;
    uint32_t flags;
    uint64_t revision;
    uint32_t activeVisibility;
    uint32_t persistedVisibility;
};

// Apply selected public system-icon visibility groups through the desktop
// owner's shared appearance revision and canonical /desktop.cfg persistence.
DesktopIconVisibilityApplyStatus apply_system_icon_visibility(
    uint32_t targetMask, uint32_t visibleMask, uint64_t expectedRevision,
    DesktopIconVisibilityApplyResultV1* output);

// Copy the active appearance values used by the desktop renderer. Settings
// consumes this through a read-only provider and never changes persistence.
bool get_appearance_configuration(DesktopAppearanceConfigurationV1* output);

// Get or update the visible built-in system desktop icons.
SystemDesktopIconVisibility get_system_desktop_icon_visibility();
void set_system_desktop_icon_visibility(const SystemDesktopIconVisibility& visibility);
bool get_system_desktop_icon_visible(const char* key);
void set_system_desktop_icon_visible(const char* key, bool visible);
void reload_persisted_system_desktop_icons();

// Attach a boot-time wallpaper image pack loaded from ramdisk.img.
void set_wallpaper_image_pack(const void* packBase, uint64_t packSize);

// Draw a built-in wallpaper thumbnail if the image pack contains it.
bool draw_wallpaper_thumbnail_by_id(const char* wallpaperId, uint32_t x, uint32_t y, uint32_t w, uint32_t h);

// Reload persisted wallpaper selection after the VFS becomes available.
void reload_persisted_wallpaper();

// Check if running in bare-metal/UEFI mode (vs hosted mode with server)
bool is_bare_metal_mode();

// Get count of running GUI apps (excludes shell)
int get_running_app_count();

// ================================================================
// Testing & Diagnostics
// ================================================================

// Run test mode - attempts to launch Notepad, Calculator, TaskManager
// and logs results to kernel console. Call from shell or at startup.
void run_test_mode();

// Check if compositor and IPC are available for GUI apps
bool is_compositor_available();

// True after the normal shell, taskbar, Start Menu state, built-in app
// registry, and compositor have all been initialized.
bool is_initialized();

// Keep normal desktop layout state aligned with the committed virtual
// desktop dimensions reported by the QEMU-only display configuration service.
void reconcile_display_topology(uint32_t virtualDesktopWidth,
                                uint32_t virtualDesktopHeight,
                                uint32_t primaryWidth = 0u,
                                uint32_t primaryHeight = 0u);

#if defined(GXOS_APPMODEL_LAUNCHSHADOW_SMOKE_ACTIVE) && defined(GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY)
// Diagnostic-only smoke hook for SHADOW_ONLY FileOpen observation. Does not launch apps.
void run_launch_shadow_folder_fileopen_smoke();
void run_launch_shadow_text_fileopen_smoke();
#endif

#if defined(GXOS_LIVE_DIRECTORY_DESKTOP_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
// Diagnostic-only smoke hook for bare-metal live-directory transitions. Does not persist state.
void run_live_directory_runtime_smoke();
#endif

#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
// Diagnostic-only smoke hook for bare-metal Image Viewer runtime paint verification.
void run_imageviewer_runtime_smoke();
#endif

} // namespace desktop
} // namespace kernel

// External function for keyboard IRQ to request redraw
extern "C" void desktop_request_redraw();
// Re-scan filesystem-backed Desktop icons after a completed mutation.
extern "C" void desktop_request_folder_refresh();

#endif // KERNEL_DESKTOP_H
