//
// guideXOS Desktop Environment - Bitmap Font Rendering
//

#ifndef KERNEL_DESKTOP_FONT_H
#define KERNEL_DESKTOP_FONT_H

#include "kernel/types.h"

namespace kernel {
namespace desktop {

static const int kGlyphW = 5;
static const int kGlyphH = 7;
static const int kGlyphSpacing = 1;
static const int kGlyphCount = 95;

int measure_text(const char* str);
void draw_char(uint32_t px, uint32_t py, char c, uint32_t color, int scale);
void draw_text(uint32_t x, uint32_t y, const char* str, uint32_t color, int scale = 1);
void draw_text_centered(uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh,
                        const char* str, uint32_t color, int scale = 1);

} // namespace desktop
} // namespace kernel

#endif // KERNEL_DESKTOP_FONT_H
