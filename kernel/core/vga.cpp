//
// VGA Text Mode Driver Implementation
//
// Copyright (c) 2024 guideX
//

#include "include/kernel/vga.h"
#include "include/kernel/arch.h"

namespace kernel {
namespace vga {

#if ARCH_HAS_VGA_TEXT

// VGA text buffer constants
static const uint32_t VGA_WIDTH = 80;
static const uint32_t VGA_HEIGHT = 25;
static uint16_t* const VGA_BUFFER = reinterpret_cast<uint16_t*>(0xB8000);

// Current state
static uint32_t g_row = 0;
static uint32_t g_col = 0;
static uint8_t g_color = 0x07; // Light gray on black

// Helper to create VGA entry
static inline uint16_t make_vga_entry(char c, uint8_t color)
{
    return static_cast<uint16_t>(c) | (static_cast<uint16_t>(color) << 8);
}

// Helper to make color byte
static inline uint8_t make_color(Color fg, Color bg)
{
    return static_cast<uint8_t>(fg) | (static_cast<uint8_t>(bg) << 4);
}

void init()
{
    g_row = 0;
    g_col = 0;
    g_color = make_color(Color::LightGray, Color::Black);
    clear();
}

void clear()
{
    const uint16_t blank = make_vga_entry(' ', g_color);
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_BUFFER[i] = blank;
    }
    g_row = 0;
    g_col = 0;
}

void set_color(Color foreground, Color background)
{
    g_color = make_color(foreground, background);
}

static void scroll()
{
    // Move all lines up by one
    for (uint32_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (uint32_t x = 0; x < VGA_WIDTH; x++) {
            VGA_BUFFER[y * VGA_WIDTH + x] = VGA_BUFFER[(y + 1) * VGA_WIDTH + x];
        }
    }
    
    // Clear last line
    const uint16_t blank = make_vga_entry(' ', g_color);
    for (uint32_t x = 0; x < VGA_WIDTH; x++) {
        VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }
    
    g_row = VGA_HEIGHT - 1;
}

void putchar(char c)
{
    if (c == '\n') {
        g_col = 0;
        g_row++;
        if (g_row >= VGA_HEIGHT) {
            scroll();
        }
        return;
    }
    
    if (c == '\r') {
        g_col = 0;
        return;
    }
    
    if (c == '\t') {
        g_col = (g_col + 4) & ~3; // Align to 4
        if (g_col >= VGA_WIDTH) {
            g_col = 0;
            g_row++;
            if (g_row >= VGA_HEIGHT) {
                scroll();
            }
        }
        return;
    }
    
    // Regular character
    const uint32_t index = g_row * VGA_WIDTH + g_col;
    VGA_BUFFER[index] = make_vga_entry(c, g_color);
    
    g_col++;
    if (g_col >= VGA_WIDTH) {
        g_col = 0;
        g_row++;
        if (g_row >= VGA_HEIGHT) {
            scroll();
        }
    }
}

void print(const char* str)
{
    if (!str) return;
    
    while (*str) {
        putchar(*str);
        str++;
    }
}

void print(const char* str, uint32_t length)
{
    if (!str) return;
    
    for (uint32_t i = 0; i < length; i++) {
        putchar(str[i]);
    }
}

void print_dec(int32_t value)
{
    if (value == 0) {
        putchar('0');
        return;
    }
    
    if (value < 0) {
        putchar('-');
        value = -value;
    }
    
    char buffer[12]; // Enough for 32-bit int
    int pos = 0;
    
    while (value > 0) {
        buffer[pos++] = '0' + (value % 10);
        value /= 10;
    }
    
    // Print in reverse
    while (pos > 0) {
        putchar(buffer[--pos]);
    }
}

void print_hex(uint32_t value)
{
    const char hex_digits[] = "0123456789ABCDEF";
    
    print("0x");
    
    bool leading = true;
    for (int i = 7; i >= 0; i--) {
        uint8_t digit = (value >> (i * 4)) & 0xF;
        if (digit != 0) leading = false;
        if (!leading || i == 0) {
            putchar(hex_digits[digit]);
        }
    }
}

void print_colored(const char* str, Color fg, Color bg)
{
    uint8_t old_color = g_color;
    set_color(fg, bg);
    print(str);
    g_color = old_color;
}

void get_cursor(uint32_t& row, uint32_t& col)
{
    row = g_row;
    col = g_col;
}

void set_cursor(uint32_t row, uint32_t col)
{
    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1;
    if (col >= VGA_WIDTH) col = VGA_WIDTH - 1;
    g_row = row;
    g_col = col;
}

#else // !ARCH_HAS_VGA_TEXT

// Stub implementation for platforms without VGA text hardware
void init() { }
void clear() { }
void set_color(Color, Color) { }
void putchar(char) { }
void print(const char*) { }
void print(const char*, uint32_t) { }
void print_dec(int32_t) { }
void print_hex(uint32_t) { }
void print_colored(const char*, Color, Color) { }
void get_cursor(uint32_t& row, uint32_t& col) { row = 0; col = 0; }
void set_cursor(uint32_t, uint32_t) { }

#endif // ARCH_HAS_VGA_TEXT

} // namespace vga
} // namespace kernel
