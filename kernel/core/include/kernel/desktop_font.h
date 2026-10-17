//
// guideXOS Desktop Environment - Bitmap Font Rendering
//

#ifndef KERNEL_DESKTOP_FONT_H
#define KERNEL_DESKTOP_FONT_H

#include "kernel/types.h"

struct DesktopSystemTextMetrics {
	uint32_t lineHeightPx;
	uint32_t ascentPx;
	uint32_t descentPx;
	uint32_t pixelSize;
};

struct DesktopTaskbarClockTextLayout {
	uint32_t x;
	uint32_t width;
	uint32_t timeY;
	uint32_t dateY;
	uint32_t lineHeight;
};

struct DesktopShellTypographyStatistics {
	uint64_t shellFrames;
	uint64_t textDrawRequests;
	uint64_t scalableDraws;
	uint64_t bitmapFallbackDraws;
	uint64_t bitmapFallbackFailures;
	uint32_t observedSelectionGeneration;
	uint32_t generationChangesObserved;
};

#if defined(GXOS_FONT11_QEMU_PROOF)
struct DesktopFontCacheDebugStatistics {
	uint64_t hits;
	uint64_t misses;
	uint64_t rasterizations;
	uint64_t evictions;
	uint64_t entryCapacityEvictions;
	uint64_t coverageCapacityEvictions;
	uint32_t entryCount;
	uint32_t bytesUsed;
	uint32_t peakBytesUsed;
};
#endif

namespace kernel {
namespace desktop {

enum class DesktopSystemTextRole : uint8_t {
	Default = 0,
	WindowTitle = 1,
	TaskbarLabel = 2,
	StartMenuLabel = 3,
	TaskbarClock = 4,
};

enum class DesktopTextAlignment : uint8_t {
	Left = 0,
	Center = 1,
	Right = 2,
};

static const int kGlyphW = 5;
static const int kGlyphH = 7;
static const int kGlyphSpacing = 1;
static const int kGlyphCount = 95;

int measure_text(const char* str);
void draw_char(uint32_t px, uint32_t py, char c, uint32_t color, int scale);
void draw_text(uint32_t x, uint32_t y, const char* str, uint32_t color, int scale = 1);
// Opt-in desktop surface path for the active system font. It retains the
// bitmap renderer as the default and returns to it when no scalable selection
// is active or the active runtime handle is stale.
void draw_system_text(uint32_t x, uint32_t y, const char* str, uint32_t color);
// Draws one active-system-font line inside a control rectangle. The line is
// ellipsized by measured advances and ink bounds. Returns false when the
// active face/size cannot fit the rectangle so the caller can keep its bitmap
// fallback without changing the control's geometry.
bool draw_system_text_in_rect(uint32_t x, uint32_t y, uint32_t width,
	uint32_t height, const char* str, uint32_t color);
// System-font-aware shell text that keeps the role's fixed geometry. The
// requested preference size is capped per role, and an ellipsized bitmap
// rendering is used when the scalable face is unavailable or cannot fit.
bool draw_shell_text_in_rect(uint32_t x, uint32_t y, uint32_t width,
	uint32_t height, const char* str, uint32_t color,
	DesktopSystemTextRole role, DesktopTextAlignment alignment);
bool draw_bitmap_text_in_rect(uint32_t x, uint32_t y, uint32_t width,
	uint32_t height, const char* str, uint32_t color,
	DesktopTextAlignment alignment);
bool measure_system_text_for_role(const char* str, DesktopSystemTextRole role,
	DesktopSystemTextMetrics* outMetrics);
bool desktop_taskbar_clock_text_layout(uint32_t widgetX, uint32_t widgetY,
	uint32_t widgetWidth, uint32_t widgetHeight,
	DesktopTaskbarClockTextLayout* outLayout);
void desktop_shell_typography_get_statistics(
	DesktopShellTypographyStatistics* outStatistics);
void desktop_shell_typography_reset_statistics();
void desktop_shell_typography_record_frame();
#if defined(GXOS_FONT11_QEMU_PROOF)
bool desktop_font11_qemu_get_cache_statistics(
	DesktopFontCacheDebugStatistics* outStatistics);
#endif
bool measure_system_text(const char* str, DesktopSystemTextMetrics* outMetrics);
void draw_text_centered(uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh,
                        const char* str, uint32_t color, int scale = 1);

} // namespace desktop
} // namespace kernel

#endif // KERNEL_DESKTOP_FONT_H
