#ifndef KERNEL_DESKTOP_CAPABILITIES_H
#define KERNEL_DESKTOP_CAPABILITIES_H

#include "kernel/types.h"

namespace kernel {
namespace desktop_capabilities {

enum class CapabilityState : uint8_t {
    False = 0,
    True = 1,
    Unknown = 2,
};

struct DesktopCapabilities {
    const char* architectureName;
    bool framebufferAvailable;
    uint32_t framebufferWidth;
    uint32_t framebufferHeight;
    uint32_t framebufferPitch;
    uint8_t framebufferBpp;
    CapabilityState framebufferPixelFormatSupported;
    bool doubleBufferingEnabled;
    CapabilityState mouseAvailable;
    CapabilityState keyboardAvailable;
    CapabilityState timerTickAvailable;
    bool desktopEventLoopActive;
    bool cursorRenderingActive;
    bool taskbarEnabled;
    bool startMenuEnabled;
    bool windowMoveEnabled;
    bool windowResizeEnabled;
    bool desktopIconsEnabled;
    bool iconPersistenceAvailable;
    bool shutdownSupported;
    bool restartSupported;
};

DesktopCapabilities collect(bool desktopEventLoopActive, bool cursorRenderingActive);
void log(const DesktopCapabilities& caps);
void log_current(bool desktopEventLoopActive, bool cursorRenderingActive);

} // namespace desktop_capabilities
} // namespace kernel

#endif // KERNEL_DESKTOP_CAPABILITIES_H
