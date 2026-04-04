//
// Video Backend Implementations
//
// Copyright (c) 2026 guideXOS Server
//

#include "video_backend.h"
#include "logger.h"

// Pull in kernel framebuffer API when building for bare-metal
#if !defined(_WIN32)
#include "include/kernel/framebuffer.h"
#include "include/kernel/vesa.h"
#endif

namespace gxos {
namespace gui {

// ================================================================
// GdiVideoBackend  (Win32 only)
// ================================================================

#ifdef _WIN32

GdiVideoBackend::GdiVideoBackend()
    : m_width(0)
    , m_height(0)
    , m_pixels(nullptr)
    , m_dib(nullptr)
    , m_memDC(nullptr)
{
}

GdiVideoBackend::~GdiVideoBackend()
{
    shutdown();
}

bool GdiVideoBackend::init(int width, int height)
{
    shutdown();

    m_width  = width;
    m_height = height;

    // Create a memory DC
    HDC screenDC = GetDC(nullptr);
    m_memDC = CreateCompatibleDC(screenDC);
    ReleaseDC(nullptr, screenDC);

    if (!m_memDC) {
        Logger::write(LogLevel::Error, "GdiVideoBackend: CreateCompatibleDC failed");
        return false;
    }

    // Create a top-down 32-bit DIB section
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = width;
    bmi.bmiHeader.biHeight      = -height;  // negative = top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    m_dib = CreateDIBSection(m_memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!m_dib || !bits) {
        Logger::write(LogLevel::Error, "GdiVideoBackend: CreateDIBSection failed");
        shutdown();
        return false;
    }

    m_pixels = static_cast<uint32_t*>(bits);
    SelectObject(m_memDC, m_dib);

    Logger::write(LogLevel::Info,
        std::string("GdiVideoBackend: initialized ") +
        std::to_string(width) + "x" + std::to_string(height));
    return true;
}

void GdiVideoBackend::shutdown()
{
    if (m_dib) {
        DeleteObject(m_dib);
        m_dib = nullptr;
    }
    if (m_memDC) {
        DeleteDC(m_memDC);
        m_memDC = nullptr;
    }
    m_pixels = nullptr;
    m_width  = 0;
    m_height = 0;
}

uint32_t* GdiVideoBackend::getPixels()
{
    return m_pixels;
}

void GdiVideoBackend::present()
{
    // The GDI backend is driven by WM_PAINT; the compositor
    // invalidates the window and the paint handler blits from
    // the memory DC.  present() is a no-op here because the
    // compositor currently paints directly via GDI calls.
    //
    // When the compositor is migrated to render into getPixels()
    // instead of painting via GDI, this function will BitBlt the
    // DIB to the window DC.
}

#endif // _WIN32

// ================================================================
// KernelFbVideoBackend
// ================================================================

KernelFbVideoBackend::KernelFbVideoBackend()
    : m_width(0)
    , m_height(0)
    , m_shadow(nullptr)
    , m_hwFb(nullptr)
    , m_ownsShadow(false)
{
}

KernelFbVideoBackend::~KernelFbVideoBackend()
{
    shutdown();
}

bool KernelFbVideoBackend::init(int width, int height)
{
    shutdown();

    m_width  = width;
    m_height = height;

#if !defined(_WIN32)
    // ---- Bare-metal path ----
    // Try to initialise the kernel framebuffer if not already done.
    if (!kernel::framebuffer::is_available()) {
        // Attempt VESA/BGA mode setting (x86/amd64)
        kernel::framebuffer::init_vesa(
            static_cast<uint16_t>(width),
            static_cast<uint16_t>(height), 32);
    }

    if (kernel::framebuffer::is_available()) {
        m_hwFb  = kernel::framebuffer::get_buffer();
        m_width  = static_cast<int>(kernel::framebuffer::get_width());
        m_height = static_cast<int>(kernel::framebuffer::get_height());

        // Allocate a shadow buffer so the compositor can do
        // double-buffered rendering (avoids tearing on the LFB).
        m_shadow = new (std::nothrow) uint32_t[m_width * m_height];
        if (m_shadow) {
            m_ownsShadow = true;
            std::memset(m_shadow, 0, static_cast<size_t>(m_width) * m_height * 4);
        } else {
            // Fall back to rendering directly into the LFB
            m_shadow = m_hwFb;
        }
        return true;
    }
    return false;

#else
    // ---- Windows host testing path ----
    // Allocate a plain heap buffer so the abstraction can be
    // exercised on Windows without real framebuffer hardware.
    m_shadow = new (std::nothrow) uint32_t[width * height];
    if (!m_shadow) {
        Logger::write(LogLevel::Error, "KernelFbVideoBackend: allocation failed");
        return false;
    }
    m_ownsShadow = true;
    m_hwFb = nullptr;
    std::memset(m_shadow, 0, static_cast<size_t>(width) * height * 4);

    Logger::write(LogLevel::Info,
        std::string("KernelFbVideoBackend: initialized (host mode) ") +
        std::to_string(width) + "x" + std::to_string(height));
    return true;
#endif
}

void KernelFbVideoBackend::shutdown()
{
    if (m_ownsShadow && m_shadow) {
        delete[] m_shadow;
    }
    m_shadow     = nullptr;
    m_hwFb       = nullptr;
    m_ownsShadow = false;
    m_width      = 0;
    m_height     = 0;
}

uint32_t* KernelFbVideoBackend::getPixels()
{
    return m_shadow;
}

void KernelFbVideoBackend::present()
{
#if !defined(_WIN32)
    // Copy shadow buffer to the hardware framebuffer
    if (m_shadow && m_hwFb && m_shadow != m_hwFb) {
        std::memcpy(m_hwFb, m_shadow,
                     static_cast<size_t>(m_width) * m_height * 4);
    }
#endif
    // On Windows host builds this is a no-op (VNC reads the
    // shadow buffer directly).
}

} // namespace gui
} // namespace gxos
