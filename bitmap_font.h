#pragma once
#pragma once
// bitmap_font.h - Simple 5x7 bitmap font for platform-independent text rendering
// Used by compositor when GDI fonts are not available (kernel framebuffer mode)
// and for drawing icon labels, tooltips, and system tray text.

#include <cstdint>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gxos { namespace gui {

    // 5x7 bitmap font: each glyph is 5 columns x 7 rows packed into 5 bytes (one per column, LSB = top row)
    // Covers ASCII 32..126
    class BitmapFont {
    public:
        static constexpr int kGlyphW = 5;
        static constexpr int kGlyphH = 7;
        static constexpr int kSpacing = 1; // 1px between characters

        // Returns pointer to 5-byte glyph data (one byte per column), or nullptr for unsupported chars
        static const uint8_t* Glyph(char c) {
            int idx = (int)(unsigned char)c - 32;
            if (idx < 0 || idx >= kGlyphCount) return nullptr;
            return s_glyphs[idx];
        }

        // Measure width of a string in pixels
        static int MeasureWidth(const char* str, int len = -1) {
            if (!str) return 0;
            if (len < 0) len = (int)strlen(str);
            if (len == 0) return 0;
            return len * (kGlyphW + kSpacing) - kSpacing;
        }

#ifdef _WIN32
        // Draw a string onto an HDC at (x,y) with the given color
        // Uses SetPixel - slow but compatible; for small text like tooltips and tray labels
        static void DrawString(HDC dc, int x, int y, const char* str, int len, COLORREF color) {
            if (!str) return;
            if (len < 0) len = (int)strlen(str);
            int cx = x;
            for (int i = 0; i < len; ++i) {
                const uint8_t* g = Glyph(str[i]);
                if (g) {
                    for (int col = 0; col < kGlyphW; ++col) {
                        uint8_t bits = g[col];
                        for (int row = 0; row < kGlyphH; ++row) {
                            if (bits & (1 << row))
                                SetPixel(dc, cx + col, y + row, color);
                        }
                    }
                }
                cx += kGlyphW + kSpacing;
            }
        }

        // Draw a string scaled 2x for readability
        static void DrawStringScaled(HDC dc, int x, int y, const char* str, int len, COLORREF color, int scale = 2) {
            if (!str) return;
            if (len < 0) len = (int)strlen(str);
            int cx = x;
            for (int i = 0; i < len; ++i) {
                const uint8_t* g = Glyph(str[i]);
                if (g) {
                    for (int col = 0; col < kGlyphW; ++col) {
                        uint8_t bits = g[col];
                        for (int row = 0; row < kGlyphH; ++row) {
                            if (bits & (1 << row)) {
                                for (int sy = 0; sy < scale; ++sy)
                                    for (int sx = 0; sx < scale; ++sx)
                                        SetPixel(dc, cx + col * scale + sx, y + row * scale + sy, color);
                            }
                        }
                    }
                }
                cx += (kGlyphW + kSpacing) * scale;
            }
        }
#endif

        // Framebuffer rendering (for bare-metal / kernel builds)
        // Draw a string directly to a 32-bit XRGB pixel buffer
        static void DrawStringToBuffer(uint32_t* pixels, int pitch, int bufW, int bufH,
                                       int x, int y, const char* str, int len, uint32_t color) {
            if (!str || !pixels) return;
            if (len < 0) len = (int)strlen(str);
            int cx = x;
            for (int i = 0; i < len; ++i) {
                const uint8_t* g = Glyph(str[i]);
                if (g) {
                    for (int col = 0; col < kGlyphW; ++col) {
                        uint8_t bits = g[col];
                        for (int row = 0; row < kGlyphH; ++row) {
                            if (bits & (1 << row)) {
                                int px = cx + col;
                                int py = y + row;
                                if (px >= 0 && px < bufW && py >= 0 && py < bufH) {
                                    pixels[py * (pitch / 4) + px] = color;
                                }
                            }
                        }
                    }
                }
                cx += kGlyphW + kSpacing;
            }
        }

        // Draw a string scaled (e.g., 2x) to framebuffer
        static void DrawStringToBufferScaled(uint32_t* pixels, int pitch, int bufW, int bufH,
                                             int x, int y, const char* str, int len, uint32_t color, int scale = 2) {
            if (!str || !pixels) return;
            if (len < 0) len = (int)strlen(str);
            int cx = x;
            for (int i = 0; i < len; ++i) {
                const uint8_t* g = Glyph(str[i]);
                if (g) {
                    for (int col = 0; col < kGlyphW; ++col) {
                        uint8_t bits = g[col];
                        for (int row = 0; row < kGlyphH; ++row) {
                            if (bits & (1 << row)) {
                                for (int sy = 0; sy < scale; ++sy) {
                                    for (int sx = 0; sx < scale; ++sx) {
                                        int px = cx + col * scale + sx;
                                        int py = y + row * scale + sy;
                                        if (px >= 0 && px < bufW && py >= 0 && py < bufH) {
                                            pixels[py * (pitch / 4) + px] = color;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                cx += (kGlyphW + kSpacing) * scale;
            }
        }

    private:
        static constexpr int kGlyphCount = 95; // ASCII 32..126

        // Glyph data: 5 bytes per glyph, one per column (LSB = top row)
        // Standard 5x7 font
        static constexpr uint8_t s_glyphs[kGlyphCount][5] = {
            {0x00,0x00,0x00,0x00,0x00}, // 32 ' '
            {0x00,0x00,0x5F,0x00,0x00}, // 33 '!'
            {0x00,0x07,0x00,0x07,0x00}, // 34 '"'
            {0x14,0x7F,0x14,0x7F,0x14}, // 35 '#'
            {0x24,0x2A,0x7F,0x2A,0x12}, // 36 '$'
            {0x23,0x13,0x08,0x64,0x62}, // 37 '%'
            {0x36,0x49,0x55,0x22,0x50}, // 38 '&'
            {0x00,0x05,0x03,0x00,0x00}, // 39 '''
            {0x00,0x1C,0x22,0x41,0x00}, // 40 '('
            {0x00,0x41,0x22,0x1C,0x00}, // 41 ')'
            {0x14,0x08,0x3E,0x08,0x14}, // 42 '*'
            {0x08,0x08,0x3E,0x08,0x08}, // 43 '+'
            {0x00,0x50,0x30,0x00,0x00}, // 44 ','
            {0x08,0x08,0x08,0x08,0x08}, // 45 '-'
            {0x00,0x60,0x60,0x00,0x00}, // 46 '.'
            {0x20,0x10,0x08,0x04,0x02}, // 47 '/'
            {0x3E,0x51,0x49,0x45,0x3E}, // 48 '0'
            {0x00,0x42,0x7F,0x40,0x00}, // 49 '1'
            {0x42,0x61,0x51,0x49,0x46}, // 50 '2'
            {0x21,0x41,0x45,0x4B,0x31}, // 51 '3'
            {0x18,0x14,0x12,0x7F,0x10}, // 52 '4'
            {0x27,0x45,0x45,0x45,0x39}, // 53 '5'
            {0x3C,0x4A,0x49,0x49,0x30}, // 54 '6'
            {0x01,0x71,0x09,0x05,0x03}, // 55 '7'
            {0x36,0x49,0x49,0x49,0x36}, // 56 '8'
            {0x06,0x49,0x49,0x29,0x1E}, // 57 '9'
            {0x00,0x36,0x36,0x00,0x00}, // 58 ':'
            {0x00,0x56,0x36,0x00,0x00}, // 59 ';'
            {0x08,0x14,0x22,0x41,0x00}, // 60 '<'
            {0x14,0x14,0x14,0x14,0x14}, // 61 '='
            {0x00,0x41,0x22,0x14,0x08}, // 62 '>'
            {0x02,0x01,0x51,0x09,0x06}, // 63 '?'
            {0x32,0x49,0x79,0x41,0x3E}, // 64 '@'
            {0x7E,0x11,0x11,0x11,0x7E}, // 65 'A'
            {0x7F,0x49,0x49,0x49,0x36}, // 66 'B'
            {0x3E,0x41,0x41,0x41,0x22}, // 67 'C'
            {0x7F,0x41,0x41,0x22,0x1C}, // 68 'D'
            {0x7F,0x49,0x49,0x49,0x41}, // 69 'E'
            {0x7F,0x09,0x09,0x09,0x01}, // 70 'F'
            {0x3E,0x41,0x49,0x49,0x7A}, // 71 'G'
            {0x7F,0x08,0x08,0x08,0x7F}, // 72 'H'
            {0x00,0x41,0x7F,0x41,0x00}, // 73 'I'
            {0x20,0x40,0x41,0x3F,0x01}, // 74 'J'
            {0x7F,0x08,0x14,0x22,0x41}, // 75 'K'
            {0x7F,0x40,0x40,0x40,0x40}, // 76 'L'
            {0x7F,0x02,0x0C,0x02,0x7F}, // 77 'M'
            {0x7F,0x04,0x08,0x10,0x7F}, // 78 'N'
            {0x3E,0x41,0x41,0x41,0x3E}, // 79 'O'
            {0x7F,0x09,0x09,0x09,0x06}, // 80 'P'
            {0x3E,0x41,0x51,0x21,0x5E}, // 81 'Q'
            {0x7F,0x09,0x19,0x29,0x46}, // 82 'R'
            {0x46,0x49,0x49,0x49,0x31}, // 83 'S'
            {0x01,0x01,0x7F,0x01,0x01}, // 84 'T'
            {0x3F,0x40,0x40,0x40,0x3F}, // 85 'U'
            {0x1F,0x20,0x40,0x20,0x1F}, // 86 'V'
            {0x3F,0x40,0x38,0x40,0x3F}, // 87 'W'
            {0x63,0x14,0x08,0x14,0x63}, // 88 'X'
            {0x07,0x08,0x70,0x08,0x07}, // 89 'Y'
            {0x61,0x51,0x49,0x45,0x43}, // 90 'Z'
            {0x00,0x7F,0x41,0x41,0x00}, // 91 '['
            {0x02,0x04,0x08,0x10,0x20}, // 92 '\'
            {0x00,0x41,0x41,0x7F,0x00}, // 93 ']'
            {0x04,0x02,0x01,0x02,0x04}, // 94 '^'
            {0x40,0x40,0x40,0x40,0x40}, // 95 '_'
            {0x00,0x01,0x02,0x04,0x00}, // 96 '`'
            {0x20,0x54,0x54,0x54,0x78}, // 97 'a'
            {0x7F,0x48,0x44,0x44,0x38}, // 98 'b'
            {0x38,0x44,0x44,0x44,0x20}, // 99 'c'
            {0x38,0x44,0x44,0x48,0x7F}, //100 'd'
            {0x38,0x54,0x54,0x54,0x18}, //101 'e'
            {0x08,0x7E,0x09,0x01,0x02}, //102 'f'
            {0x0C,0x52,0x52,0x52,0x3E}, //103 'g'
            {0x7F,0x08,0x04,0x04,0x78}, //104 'h'
            {0x00,0x44,0x7D,0x40,0x00}, //105 'i'
            {0x20,0x40,0x44,0x3D,0x00}, //106 'j'
            {0x7F,0x10,0x28,0x44,0x00}, //107 'k'
            {0x00,0x41,0x7F,0x40,0x00}, //108 'l'
            {0x7C,0x04,0x18,0x04,0x78}, //109 'm'
            {0x7C,0x08,0x04,0x04,0x78}, //110 'n'
            {0x38,0x44,0x44,0x44,0x38}, //111 'o'
            {0x7C,0x14,0x14,0x14,0x08}, //112 'p'
            {0x08,0x14,0x14,0x18,0x7C}, //113 'q'
            {0x7C,0x08,0x04,0x04,0x08}, //114 'r'
            {0x48,0x54,0x54,0x54,0x20}, //115 's'
            {0x04,0x3F,0x44,0x40,0x20}, //116 't'
            {0x3C,0x40,0x40,0x20,0x7C}, //117 'u'
            {0x1C,0x20,0x40,0x20,0x1C}, //118 'v'
            {0x3C,0x40,0x30,0x40,0x3C}, //119 'w'
            {0x44,0x28,0x10,0x28,0x44}, //120 'x'
            {0x0C,0x50,0x50,0x50,0x3C}, //121 'y'
            {0x44,0x64,0x54,0x4C,0x44}, //122 'z'
            {0x00,0x08,0x36,0x41,0x00}, //123 '{'
            {0x00,0x00,0x7F,0x00,0x00}, //124 '|'
            {0x00,0x41,0x36,0x08,0x00}, //125 '}'
            {0x10,0x08,0x08,0x10,0x08}, //126 '~'
        };
    };

}} // namespace gxos::gui
