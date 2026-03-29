// Framebuffer Text Console
//
// Provides a text console rendered onto the graphical framebuffer.
// This replaces the VGA text-mode console on architectures that
// lack VGA hardware (SPARC, IA-64, ARM).
//
// Features:
//   - 8x16 bitmap font (CP437 glyph set, 128 printable ASCII chars)
//   - Foreground / background colour
//   - Scrolling
//   - Cursor tracking
//   - print / putchar / print_hex / print_dec
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_FB_CONSOLE_H
#define KERNEL_FB_CONSOLE_H

#include "kernel/types.h"

namespace kernel {
namespace fb_console {

// ================================================================
// Colour constants (32-bit ARGB)
// ================================================================

static const uint32_t COLOR_BLACK        = 0xFF000000;
static const uint32_t COLOR_BLUE         = 0xFF0000AA;
static const uint32_t COLOR_GREEN        = 0xFF00AA00;
static const uint32_t COLOR_CYAN         = 0xFF00AAAA;
static const uint32_t COLOR_RED          = 0xFFAA0000;
static const uint32_t COLOR_MAGENTA      = 0xFFAA00AA;
static const uint32_t COLOR_BROWN        = 0xFFAA5500;
static const uint32_t COLOR_LIGHT_GRAY   = 0xFFAAAAAA;
static const uint32_t COLOR_DARK_GRAY    = 0xFF555555;
static const uint32_t COLOR_LIGHT_BLUE   = 0xFF5555FF;
static const uint32_t COLOR_LIGHT_GREEN  = 0xFF55FF55;
static const uint32_t COLOR_LIGHT_CYAN   = 0xFF55FFFF;
static const uint32_t COLOR_LIGHT_RED    = 0xFFFF5555;
static const uint32_t COLOR_LIGHT_MAGENTA= 0xFFFF55FF;
static const uint32_t COLOR_YELLOW       = 0xFFFFFF55;
static const uint32_t COLOR_WHITE        = 0xFFFFFFFF;

// ================================================================
// Font parameters
// ================================================================

static const uint8_t FONT_WIDTH  = 8;
static const uint8_t FONT_HEIGHT = 16;

// ================================================================
// Public API
// ================================================================

// Initialise the framebuffer console.
// Must be called after the kernel framebuffer is initialised.
// Returns false if no framebuffer is available.
bool init();

// Clear the console.
void clear();

// Set foreground and background colours.
void set_color(uint32_t fg, uint32_t bg);

// Print a single character.
void putchar(char c);

// Print a null-terminated string.
void print(const char* str);

// Print a string with explicit length.
void print(const char* str, uint32_t length);

// Print a decimal integer.
void print_dec(int32_t value);

// Print a hexadecimal value.
void print_hex(uint32_t value);

// Print a 64-bit hexadecimal value.
void print_hex64(uint64_t value);

// Print with specific colours (restores previous colours after).
void print_colored(const char* str, uint32_t fg, uint32_t bg);

// Get / set cursor position (in character cells).
void get_cursor(uint32_t& row, uint32_t& col);
void set_cursor(uint32_t row, uint32_t col);

// Get console dimensions (in character cells).
uint32_t get_cols();
uint32_t get_rows();

} // namespace fb_console
} // namespace kernel

#endif // KERNEL_FB_CONSOLE_H
