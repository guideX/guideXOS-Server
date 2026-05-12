#include "include/kernel/desktop_capabilities.h"
#include "include/kernel/arch.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/serial_debug.h"

#if defined(ARCH_IA64)
#include <arch/ski_console.h>
#endif

#if defined(ARCH_RISCV64)
#include <arch/sbi_console.h>
#endif

#if defined(ARCH_ARM64)
#include <arch/serial_console.h>
#endif

#if defined(ARCH_MIPS64)
#include <arch/serial_console.h>
#endif

namespace kernel {
namespace desktop_capabilities {

static const char* state_text(CapabilityState state)
{
    switch (state) {
        case CapabilityState::False: return "false";
        case CapabilityState::True: return "true";
        default: return "unknown";
    }
}

static const char* bool_text(bool value)
{
    return value ? "true" : "false";
}

static void log_puts(const char* text)
{
#if defined(ARCH_IA64)
    kernel::arch::ia64::ski_console::puts(text);
#elif defined(ARCH_RISCV64)
    kernel::arch::riscv64::sbi_console::puts(text);
#elif defined(ARCH_ARM64)
    kernel::arch::arm64::serial_console::print(text);
#elif defined(ARCH_MIPS64)
    kernel::arch::mips64::serial_console::puts(text);
#else
    kernel::serial::puts(text);
#endif
}

static void log_hex32(uint32_t value)
{
#if defined(ARCH_IA64)
    kernel::arch::ia64::ski_console::put_hex(value);
#elif defined(ARCH_RISCV64)
    kernel::arch::riscv64::sbi_console::put_hex(value);
#elif defined(ARCH_ARM64)
    kernel::arch::arm64::serial_console::print_hex(value);
#elif defined(ARCH_MIPS64)
    kernel::arch::mips64::serial_console::put_hex(value);
#else
    kernel::serial::put_hex32(value);
#endif
}

static void log_bool(const char* name, bool value)
{
    log_puts("[DESKTOP CAP] ");
    log_puts(name);
    log_puts("=");
    log_puts(bool_text(value));
    log_puts("\n");
}

static void log_state(const char* name, CapabilityState value)
{
    log_puts("[DESKTOP CAP] ");
    log_puts(name);
    log_puts("=");
    log_puts(state_text(value));
    log_puts("\n");
}

static CapabilityState pixel_format_supported(uint8_t bpp)
{
    if (bpp == 16 || bpp == 24 || bpp == 32) return CapabilityState::True;
    if (bpp == 8) return CapabilityState::False;
    if (bpp == 0) return CapabilityState::Unknown;
    return CapabilityState::False;
}

static CapabilityState mouse_available()
{
#if defined(ARCH_X86) || defined(ARCH_AMD64) || defined(ARCH_SPARC) || defined(ARCH_SPARC64)
    return CapabilityState::True;
#else
    return CapabilityState::Unknown;
#endif
}

static CapabilityState keyboard_available()
{
#if defined(ARCH_X86) || defined(ARCH_AMD64)
    return CapabilityState::True;
#elif defined(ARCH_SPARC) || defined(ARCH_SPARC64)
    return CapabilityState::Unknown;
#else
    return CapabilityState::Unknown;
#endif
}

static CapabilityState timer_tick_available()
{
#if defined(ARCH_X86) || defined(ARCH_AMD64)
    return CapabilityState::True;
#else
    return CapabilityState::Unknown;
#endif
}

static bool shutdown_supported()
{
#if defined(ARCH_X86) || defined(ARCH_AMD64) || defined(ARCH_RISCV64)
    return true;
#else
    return false;
#endif
}

static bool restart_supported()
{
#if defined(ARCH_X86) || defined(ARCH_AMD64) || defined(ARCH_RISCV64)
    return true;
#else
    return false;
#endif
}

DesktopCapabilities collect(bool desktopEventLoopActive, bool cursorRenderingActive)
{
    DesktopCapabilities caps{};
    caps.architectureName = kernel::arch::get_arch_name();
    caps.framebufferAvailable = framebuffer::is_available();
    caps.framebufferWidth = framebuffer::get_width();
    caps.framebufferHeight = framebuffer::get_height();
    caps.framebufferPitch = framebuffer::get_pitch();
    caps.framebufferBpp = framebuffer::get_bpp();
    caps.framebufferPixelFormatSupported = caps.framebufferAvailable
        ? pixel_format_supported(caps.framebufferBpp)
        : CapabilityState::False;
    caps.doubleBufferingEnabled = framebuffer::is_double_buffered();
    caps.mouseAvailable = mouse_available();
    caps.keyboardAvailable = keyboard_available();
    caps.timerTickAvailable = timer_tick_available();
    caps.desktopEventLoopActive = desktopEventLoopActive;
    caps.cursorRenderingActive = cursorRenderingActive;
    caps.taskbarEnabled = caps.framebufferAvailable;
    caps.startMenuEnabled = caps.framebufferAvailable;
    caps.windowMoveEnabled = caps.framebufferAvailable;
    caps.windowResizeEnabled = caps.framebufferAvailable;
    caps.desktopIconsEnabled = caps.framebufferAvailable;
    caps.iconPersistenceAvailable = false;
    caps.shutdownSupported = shutdown_supported();
    caps.restartSupported = restart_supported();
    return caps;
}

void log(const DesktopCapabilities& caps)
{
    log_puts("[DESKTOP CAP] begin\n");
    log_puts("[DESKTOP CAP] architecture=");
    log_puts(caps.architectureName ? caps.architectureName : "unknown");
    log_puts("\n");
    log_bool("framebuffer_available", caps.framebufferAvailable);
    log_puts("[DESKTOP CAP] framebuffer_width=0x");
    log_hex32(caps.framebufferWidth);
    log_puts("\n");
    log_puts("[DESKTOP CAP] framebuffer_height=0x");
    log_hex32(caps.framebufferHeight);
    log_puts("\n");
    log_puts("[DESKTOP CAP] framebuffer_pitch=0x");
    log_hex32(caps.framebufferPitch);
    log_puts("\n");
    log_puts("[DESKTOP CAP] framebuffer_bpp=0x");
    log_hex32(caps.framebufferBpp);
    log_puts("\n");
    log_state("framebuffer_pixel_format_supported", caps.framebufferPixelFormatSupported);
    log_bool("double_buffering_enabled", caps.doubleBufferingEnabled);
    log_state("mouse_available", caps.mouseAvailable);
    log_state("keyboard_available", caps.keyboardAvailable);
    log_state("timer_tick_available", caps.timerTickAvailable);
    log_bool("desktop_event_loop_active", caps.desktopEventLoopActive);
    log_bool("cursor_rendering_active", caps.cursorRenderingActive);
    log_bool("taskbar_enabled", caps.taskbarEnabled);
    log_bool("start_menu_enabled", caps.startMenuEnabled);
    log_bool("window_move_enabled", caps.windowMoveEnabled);
    log_bool("window_resize_enabled", caps.windowResizeEnabled);
    log_bool("desktop_icons_enabled", caps.desktopIconsEnabled);
    log_bool("icon_persistence_available", caps.iconPersistenceAvailable);
    log_bool("shutdown_supported", caps.shutdownSupported);
    log_bool("restart_supported", caps.restartSupported);
    log_puts("[DESKTOP CAP] end\n");
}

void log_current(bool desktopEventLoopActive, bool cursorRenderingActive)
{
    log(collect(desktopEventLoopActive, cursorRenderingActive));
}

} // namespace desktop_capabilities
} // namespace kernel
