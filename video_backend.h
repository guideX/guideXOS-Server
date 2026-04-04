#pragma once
//
// Video Backend Abstraction Layer
//
// Provides a common interface for rendering the compositor output to
// different display targets:
//
//   GdiVideoBackend      - Win32 GDI (development / Windows host builds)
//   KernelFbVideoBackend - Kernel framebuffer (native OS, bare-metal)
//
// The VNC server reads from whichever backend is active, so VNC
// remains available as a secondary viewer regardless of backend.
//
// Copyright (c) 2026 guideXOS Server
//

#include <cstdint>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gxos {
namespace gui {

// ================================================================
// Abstract video backend
// ================================================================

class VideoBackend {
public:
    virtual ~VideoBackend() {}

    // Initialise the backend with the requested resolution.
    // Returns true on success.
    virtual bool init(int width, int height) = 0;

    // Shut down and release resources.
    virtual void shutdown() = 0;

    // Get a pointer to the 32-bit XRGB pixel buffer (software
    // framebuffer).  The compositor writes into this, then calls
    // present() to push it to the actual display.
    virtual uint32_t* getPixels() = 0;

    // Push the software framebuffer to the display device.
    virtual void present() = 0;

    virtual int getWidth()  const = 0;
    virtual int getHeight() const = 0;
    virtual int getPitch()  const = 0;   // bytes per scanline

    // True if this backend owns a native OS window (GDI).
    virtual bool hasNativeWindow() const { return false; }
};

// ================================================================
// Win32 GDI backend  (development builds on Windows)
//
// Allocates a DIB section as the software framebuffer and blits
// it to the compositor HWND on present().
// ================================================================

#ifdef _WIN32
class GdiVideoBackend : public VideoBackend {
public:
    GdiVideoBackend();
    ~GdiVideoBackend();

    bool init(int width, int height) override;
    void shutdown() override;

    uint32_t* getPixels() override;
    void present() override;

    int getWidth()  const override { return m_width; }
    int getHeight() const override { return m_height; }
    int getPitch()  const override { return m_width * 4; }

    bool hasNativeWindow() const override { return true; }

    // GDI-specific: get the memory DC that holds the DIB
    HDC getMemDC() const { return m_memDC; }

private:
    int       m_width;
    int       m_height;
    uint32_t* m_pixels;    // DIB section bits
    HBITMAP   m_dib;
    HDC       m_memDC;
};
#endif // _WIN32

// ================================================================
// Kernel framebuffer backend  (bare-metal / kernel builds)
//
// Wraps the kernel::framebuffer API.  The software pixel buffer
// is the kernel framebuffer itself (memory-mapped LFB), or a
// shadow buffer that is blitted to the LFB on present().
//
// On Windows host builds this compiles to a simple heap-backed
// buffer (useful for testing the abstraction without real HW).
// ================================================================

class KernelFbVideoBackend : public VideoBackend {
public:
    KernelFbVideoBackend();
    ~KernelFbVideoBackend();

    bool init(int width, int height) override;
    void shutdown() override;

    uint32_t* getPixels() override;
    void present() override;

    int getWidth()  const override { return m_width; }
    int getHeight() const override { return m_height; }
    int getPitch()  const override { return m_width * 4; }

private:
    int       m_width;
    int       m_height;
    uint32_t* m_shadow;       // shadow buffer (compositor writes here)
    uint32_t* m_hwFb;         // hardware framebuffer pointer
    bool      m_ownsShadow;   // true if we allocated m_shadow
};

} // namespace gui
} // namespace gxos
