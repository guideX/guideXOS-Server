//
// VGA Text Mode Driver
//
// Copyright (c) 2024 guideX
//

#pragma once

#include "types.h"

namespace kernel {
namespace vga {

// VGA colors
enum class Color : uint8_t {
    Black = 0,
    Blue = 1,
    Green = 2,
    Cyan = 3,
    Red = 4,
    Magenta = 5,
    Brown = 6,
    LightGray = 7,
    DarkGray = 8,
    LightBlue = 9,
    LightGreen = 10,
    LightCyan = 11,
    LightRed = 12,
    LightMagenta = 13,
    Yellow = 14,
    White = 15
};

// Initialize VGA driver
void init();

// Clear screen
void clear();

// Set text color
void set_color(Color foreground, Color background);

// Print single character
void putchar(char c);

// Print null-terminated string
void print(const char* str);

// Print string with length
void print(const char* str, uint32_t length);

// Print integer
void print_dec(int32_t value);
void print_hex(uint32_t value);

// Print with color
void print_colored(const char* str, Color fg, Color bg);

// Get current cursor position
void get_cursor(uint32_t& row, uint32_t& col);

// Set cursor position
void set_cursor(uint32_t row, uint32_t col);

} // namespace vga
} // namespace kernel
