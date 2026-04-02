//
// guideXOS Desktop Environment - Kernel Framebuffer Renderer
//
// Renders the full desktop environment directly to the kernel framebuffer.
// Ported visual style from guideXOS Server compositor (compositor.cpp),
// desktop_wallpaper.h, system_tray.h, and bitmap_font.h.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/desktop.h"
#include "include/kernel/framebuffer.h"

namespace kernel {
namespace desktop {

// ============================================================
// Bitmap font (5x7, ASCII 32..126) - ported from bitmap_font.h
// ============================================================

static const int kGlyphW = 5;
static const int kGlyphH = 7;
static const int kGlyphSpacing = 1;
static const int kGlyphCount = 95;

static const uint8_t s_glyphs[kGlyphCount][5] = {
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
    {0x02,0x04,0x08,0x10,0x20}, // 92 backslash
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

static const uint8_t* glyph(char c)
{
    int idx = (int)(unsigned char)c - 32;
    if (idx < 0 || idx >= kGlyphCount) return nullptr;
    return s_glyphs[idx];
}

static int measure_text(const char* str)
{
    int len = 0;
    while (str[len]) len++;
    if (len == 0) return 0;
    return len * (kGlyphW + kGlyphSpacing) - kGlyphSpacing;
}

// Draw a single character at (px, py) with scale factor
static void draw_char(uint32_t px, uint32_t py, char c, uint32_t color, int scale)
{
    const uint8_t* g = glyph(c);
    if (!g) return;
    for (int col = 0; col < kGlyphW; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < kGlyphH; row++) {
            if (bits & (1 << row)) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        framebuffer::put_pixel(px + col * scale + sx, py + row * scale + sy, color);
            }
        }
    }
}

// Draw a null-terminated string
static void draw_text(uint32_t x, uint32_t y, const char* str, uint32_t color, int scale = 1)
{
    uint32_t cx = x;
    while (*str) {
        draw_char(cx, y, *str, color, scale);
        cx += (kGlyphW + kGlyphSpacing) * scale;
        str++;
    }
}

// Draw text centered horizontally within a region
static void draw_text_centered(uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh,
                                const char* str, uint32_t color, int scale = 1)
{
    int tw = measure_text(str) * scale;
    int th = kGlyphH * scale;
    uint32_t tx = rx + (rw > (uint32_t)tw ? (rw - tw) / 2 : 0);
    uint32_t ty = ry + (rh > (uint32_t)th ? (rh - th) / 2 : 0);
    draw_text(tx, ty, str, color, scale);
}

// ============================================================
// Color helpers
// ============================================================

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t lerp_color(uint32_t c1, uint32_t c2, uint32_t num, uint32_t den)
{
    if (den == 0) return c1;
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = (uint8_t)(r1 + (int)(r2 - r1) * (int)num / (int)den);
    uint8_t g = (uint8_t)(g1 + (int)(g2 - g1) * (int)num / (int)den);
    uint8_t b = (uint8_t)(b1 + (int)(b2 - b1) * (int)num / (int)den);
    return rgb(r, g, b);
}

// ============================================================
// Desktop state
// ============================================================

static bool s_initialized = false;
static bool s_startMenuOpen = false;
static bool s_rightClickMenuOpen = false;
static uint32_t s_rightClickX = 0;
static uint32_t s_rightClickY = 0;
static uint32_t s_screenW = 0;
static uint32_t s_screenH = 0;

// Layout constants
static const uint32_t kTaskbarH = 40;
static const uint32_t kStartBtnW = 100;
static const uint32_t kSearchBoxW = 160;
static const uint32_t kSearchBoxH = 24;
static const uint32_t kShowDesktopW = 6;
static const uint32_t kStartMenuW = 420;
static const uint32_t kStartMenuItemH = 28;
static const uint32_t kStartMenuRightColW = 160;
static const uint32_t kIconSize = 48;
static const uint32_t kIconCellW = 80;
static const uint32_t kIconCellH = 76;
static const uint32_t kIconMargin = 24;
static const uint32_t kTrayIconSize = 16;
static const uint32_t kTrayIconGap = 6;
static const uint32_t kTaskbarBtnMaxW = 150;
static const uint32_t kTaskbarBtnH = 28;
static const uint32_t kTaskbarBtnGap = 4;

// Desktop icon entries
struct DesktopIcon {
    const char* label;
    uint32_t color;
};

static const DesktopIcon s_desktopIcons[] = {
    {"Notepad",    0xFF78B450},  // green
    {"Calculator", 0xFF4690C8},  // blue
    {"Console",    0xFF78B450},  // green
    {"Paint",      0xFFC87830},  // orange
    {"Clock",      0xFF4690C8},  // blue
    {"TaskMgr",    0xFFB44646},  // red
    {"Files",      0xFFC8B43C},  // yellow
    {"ImgViewer",  0xFFC87830},  // orange
};
static const int kDesktopIconCount = 8;

// Start menu entries (left column - recent/pinned apps)
static const char* s_startMenuApps[] = {
    "Calculator",
    "Clock",
    "Console",
    "File Explorer",
    "Image Viewer",
    "Notepad",
    "Paint",
    "Task Manager",
};
static const int kStartMenuAppCount = 8;

// Start menu right column entries (system shortcuts, matching Legacy StartMenu.cs)
struct StartMenuRightItem {
    const char* label;
    uint32_t color;
};

static const StartMenuRightItem s_startMenuRight[] = {
    {"Computer",    0xFF4690C8},
    {"Documents",   0xFFC8B43C},
    {"Pictures",    0xFFC87830},
    {"Music",       0xFF9050B4},
    {"Network",     0xFF50B478},
    {"Settings",    0xFF808890},
};
static const int kStartMenuRightCount = 6;

// Simulated running windows for taskbar buttons
struct TaskbarEntry {
    const char* title;
    uint32_t color;
    bool active;
};

static TaskbarEntry s_taskbarEntries[] = {
    {"Welcome", 0xFF4690C8, true},
};
static const int kTaskbarEntryCount = 1;

// Right-click context menu entries
static const char* s_contextMenuItems[] = {
    "Refresh",
    "Display Settings",
    "Personalize",
    "New Folder",
    "Open Terminal",
    "Task Manager",
};
static const int kContextMenuCount = 6;
static const uint32_t kContextMenuW = 160;
static const uint32_t kContextMenuItemH = 24;

// Notification toast
struct NotificationToast {
    const char* title;
    const char* message;
    bool visible;
};

static NotificationToast s_notification = {
    "Welcome to guideXOS",
    "System started successfully",
    true
};

// Selected desktop icon (-1 = none)
static int s_selectedIcon = -1;

// Start menu hover and click state (-1 = none)
static int s_hoverMenuLeft  = -1;   // hovered left-column item index
static int s_hoverMenuRight = -1;   // hovered right-column item index
static int s_clickedMenuLeft  = -1; // clicked left-column item index
static int s_clickedMenuRight = -1; // clicked right-column item index

// ============================================================
// Drawing routines
// ============================================================

// Draw a horizontal line
static void hline(uint32_t x, uint32_t y, uint32_t w, uint32_t color)
{
    for (uint32_t i = 0; i < w; i++)
        framebuffer::put_pixel(x + i, y, color);
}

// Draw a vertical line
static void vline(uint32_t x, uint32_t y, uint32_t h, uint32_t color)
{
    for (uint32_t i = 0; i < h; i++)
        framebuffer::put_pixel(x, y + i, color);
}

// Draw a rectangle outline
static void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    hline(x, y, w, color);
    hline(x, y + h - 1, w, color);
    vline(x, y, h, color);
    vline(x + w - 1, y, h, color);
}

// ============================================================
// Background - gradient + branding (matches desktop_wallpaper.h)
// ============================================================

static void draw_background()
{
    uint32_t w = s_screenW;
    uint32_t h = s_screenH - kTaskbarH;
    uint32_t topColor = rgb(20, 40, 80);
    uint32_t botColor = rgb(15, 18, 28);

    for (uint32_t y = 0; y < h; y++) {
        uint32_t lineColor = lerp_color(topColor, botColor, y, h > 1 ? h - 1 : 1);
        framebuffer::fill_rect(0, y, w, 1, lineColor);
    }

    // Subtle grid overlay (every 100px)
    uint32_t gridColor = rgb(25, 35, 55);
    for (uint32_t x = 100; x < w; x += 100)
        vline(x, 0, h, gridColor);
    for (uint32_t y = 100; y < h; y += 100)
        hline(0, y, w, gridColor);

    // "guideXOS" branding - large centered text (scale 4)
    {
        const char* brand = "guideXOS";
        int tw = measure_text(brand) * 4;
        uint32_t bx = (w > (uint32_t)tw) ? (w - tw) / 2 : 0;
        uint32_t by = h / 2 - 60;
        draw_text(bx, by, brand, rgb(35, 50, 75), 4);
    }

    // Version subtitle (scale 2)
    {
        const char* ver = "Server Edition";
        int tw = measure_text(ver) * 2;
        uint32_t vx = (w > (uint32_t)tw) ? (w - tw) / 2 : 0;
        uint32_t vy = s_screenH / 2 - 60 + 4 * kGlyphH + 12;
        draw_text(vx, vy, ver, rgb(45, 60, 85), 2);
    }
}

// ============================================================
// Desktop icons
// ============================================================

static void draw_desktop_icons()
{
    uint32_t deskH = s_screenH - kTaskbarH;
    uint32_t cols = (s_screenW - kIconMargin * 2) / kIconCellW;
    if (cols < 1) cols = 1;

    for (int i = 0; i < kDesktopIconCount; i++) {
        uint32_t col = (uint32_t)i % cols;
        uint32_t row = (uint32_t)i / cols;
        uint32_t cx = kIconMargin + col * kIconCellW;
        uint32_t cy = kIconMargin + row * kIconCellH;

        if (cy + kIconCellH > deskH) break;

        // Selection highlight background
        if (i == s_selectedIcon) {
            framebuffer::fill_rect(cx, cy, kIconCellW, kIconCellH, rgb(60, 80, 120));
            draw_rect(cx, cy, kIconCellW, kIconCellH, rgb(100, 140, 200));
        }

        // Icon background square
        uint32_t ix = cx + (kIconCellW - kIconSize) / 2;
        uint32_t iy = cy + 4;
        framebuffer::fill_rect(ix, iy, kIconSize, kIconSize, s_desktopIcons[i].color);

        // Inner highlight
        uint32_t innerSize = 20;
        uint32_t innerX = ix + (kIconSize - innerSize) / 2;
        uint32_t innerY = iy + (kIconSize - innerSize) / 2;
        uint32_t ic = s_desktopIcons[i].color;
        uint8_t hr = (uint8_t)(((ic >> 16) & 0xFF) + 40 > 255 ? 255 : ((ic >> 16) & 0xFF) + 40);
        uint8_t hg = (uint8_t)(((ic >> 8) & 0xFF) + 40 > 255 ? 255 : ((ic >> 8) & 0xFF) + 40);
        uint8_t hb = (uint8_t)((ic & 0xFF) + 40 > 255 ? 255 : (ic & 0xFF) + 40);
        framebuffer::fill_rect(innerX, innerY, innerSize, innerSize, rgb(hr, hg, hb));

        // Icon border
        draw_rect(ix, iy, kIconSize, kIconSize, rgb(180, 180, 200));

        // Label (shadow + text)
        uint32_t labelY = iy + kIconSize + 4;
        const char* lbl = s_desktopIcons[i].label;
        int tw = measure_text(lbl);
        uint32_t lx = cx + (kIconCellW > (uint32_t)tw ? (kIconCellW - tw) / 2 : 0);
        draw_text(lx + 1, labelY + 1, lbl, rgb(0, 0, 0), 1);
        draw_text(lx, labelY, lbl, rgb(230, 230, 240), 1);
    }
}

// ============================================================
// System tray (network bars, volume, battery)
// ============================================================

static void draw_network_icon(uint32_t x, uint32_t y)
{
    // 4 signal bars, all lit (green)
    for (int i = 0; i < 4; i++) {
        uint32_t barH = 4 + (uint32_t)i * 3;
        uint32_t barW = 3;
        uint32_t bx = x + (uint32_t)i * 4;
        uint32_t by = y + kTrayIconSize - barH;
        framebuffer::fill_rect(bx, by, barW, barH, rgb(100, 200, 100));
    }
}

static void draw_volume_icon(uint32_t x, uint32_t y)
{
    // Speaker body
    framebuffer::fill_rect(x + 2, y + 5, 4, 6, rgb(180, 180, 190));
    // Speaker cone (triangle approximation)
    for (int i = 0; i < 5; i++) {
        framebuffer::fill_rect(x + 6, y + 4 - i/2, 1, 8 + i, rgb(180, 180, 190));
    }
    // Sound waves (two arcs as vertical bars)
    vline(x + 12, y + 4, 8, rgb(130, 130, 150));
    vline(x + 14, y + 2, 12, rgb(100, 100, 120));
}

static void draw_battery_icon(uint32_t x, uint32_t y)
{
    // Battery outline
    draw_rect(x + 1, y + 4, 12, 8, rgb(180, 180, 190));
    // Battery tip
    framebuffer::fill_rect(x + 13, y + 6, 2, 4, rgb(180, 180, 190));
    // Battery fill (green = full)
    framebuffer::fill_rect(x + 3, y + 6, 8, 4, rgb(100, 200, 100));
}

static void draw_system_tray(uint32_t trayX, uint32_t taskbarY)
{
    uint32_t iconY = taskbarY + (kTaskbarH - kTrayIconSize) / 2;

    // Separator line
    vline(trayX - 2, taskbarY + 4, kTaskbarH - 8, rgb(80, 80, 90));

    uint32_t cx = trayX + 4;
    draw_network_icon(cx, iconY);
    cx += kTrayIconSize + kTrayIconGap;
    draw_volume_icon(cx, iconY);
    cx += kTrayIconSize + kTrayIconGap;
    draw_battery_icon(cx, iconY);
}

// ============================================================
// Search box (taskbar, matching Legacy/compositor drawTaskbarSearchBox)
// ============================================================

static void draw_search_box(uint32_t x, uint32_t y)
{
    // Search box background
    framebuffer::fill_rect(x, y, kSearchBoxW, kSearchBoxH, rgb(35, 35, 45));
    draw_rect(x, y, kSearchBoxW, kSearchBoxH, rgb(70, 80, 100));

    // Magnifying glass icon (small circle + handle)
    uint32_t iconX = x + 6;
    uint32_t iconY = y + 5;
    // Circle (approximated as small square with gap)
    draw_rect(iconX, iconY, 10, 10, rgb(120, 130, 150));
    // Handle (diagonal line approximation)
    framebuffer::put_pixel(iconX + 10, iconY + 10, rgb(120, 130, 150));
    framebuffer::put_pixel(iconX + 11, iconY + 11, rgb(120, 130, 150));
    framebuffer::put_pixel(iconX + 12, iconY + 12, rgb(120, 130, 150));

    // Placeholder text
    draw_text(x + 22, y + (kSearchBoxH - kGlyphH) / 2, "Search...", rgb(100, 105, 120), 1);
}

// ============================================================
// Taskbar window buttons (matching Legacy Taskbar.cs button rendering)
// ============================================================

static void draw_taskbar_buttons(uint32_t startX, uint32_t tbY, uint32_t maxX)
{
    uint32_t btnX = startX;
    uint32_t btnY = tbY + (kTaskbarH - kTaskbarBtnH) / 2;

    for (int i = 0; i < kTaskbarEntryCount; i++) {
        if (btnX + kTaskbarBtnMaxW > maxX) break;

        uint32_t tw = (uint32_t)measure_text(s_taskbarEntries[i].title);
        uint32_t btnW = tw + 30;
        if (btnW > kTaskbarBtnMaxW) btnW = kTaskbarBtnMaxW;

        // Button background
        uint32_t bgColor = s_taskbarEntries[i].active
            ? rgb(70, 100, 150)
            : rgb(55, 58, 70);
        framebuffer::fill_rect(btnX, btnY, btnW, kTaskbarBtnH, bgColor);

        // Active indicator line at bottom (matching compositor)
        if (s_taskbarEntries[i].active) {
            framebuffer::fill_rect(btnX + 2, btnY + kTaskbarBtnH - 3, btnW - 4, 2, rgb(100, 160, 240));
        }

        // Small colored icon
        uint32_t iconSz = 14;
        uint32_t iconX = btnX + 4;
        uint32_t iconY2 = btnY + (kTaskbarBtnH - iconSz) / 2;
        framebuffer::fill_rect(iconX, iconY2, iconSz, iconSz, s_taskbarEntries[i].color);

        // Title text
        draw_text(btnX + 22, btnY + (kTaskbarBtnH - kGlyphH) / 2,
                  s_taskbarEntries[i].title, rgb(230, 230, 240), 1);

        btnX += btnW + kTaskbarBtnGap;
    }
}

// ============================================================
// Taskbar
// ============================================================

static void draw_taskbar()
{
    uint32_t tbY = s_screenH - kTaskbarH;

    // Taskbar background (dark gradient)
    uint32_t tbTop = rgb(45, 45, 55);
    uint32_t tbBot = rgb(30, 30, 38);
    for (uint32_t y = 0; y < kTaskbarH; y++) {
        uint32_t c = lerp_color(tbTop, tbBot, y, kTaskbarH - 1);
        framebuffer::fill_rect(0, tbY + y, s_screenW, 1, c);
    }

    // Top border highlight
    hline(0, tbY, s_screenW, rgb(70, 70, 85));

    // Start button
    uint32_t btnY = tbY + 4;
    uint32_t btnH = kTaskbarH - 8;
    uint32_t btnColor = s_startMenuOpen ? rgb(70, 100, 150) : rgb(50, 70, 110);
    framebuffer::fill_rect(4, btnY, kStartBtnW, btnH, btnColor);
    draw_rect(4, btnY, kStartBtnW, btnH, rgb(90, 120, 180));
    draw_text_centered(4, btnY, kStartBtnW, btnH, "guideXOS", rgb(240, 240, 255), 1);

    // Search box (after start button, matching Legacy/compositor layout)
    uint32_t searchX = 4 + kStartBtnW + 8;
    uint32_t searchY = tbY + (kTaskbarH - kSearchBoxH) / 2;
    draw_search_box(searchX, searchY);

    // Taskbar window buttons (after search box)
    uint32_t taskBtnStart = searchX + kSearchBoxW + 8;

    // Calculate right-side reserved area
    uint32_t trayW = (kTrayIconSize + kTrayIconGap) * 3 + 12;
    uint32_t clockW = 60;
    uint32_t rightReserved = kShowDesktopW + trayW + clockW + 24;
    uint32_t taskBtnMaxX = s_screenW - rightReserved;

    draw_taskbar_buttons(taskBtnStart, tbY, taskBtnMaxX);

    // Clock area (right side, before tray) - time and date
    uint32_t clockX = s_screenW - kShowDesktopW - trayW - clockW - 16;
    // Time (centered in upper half)
    uint32_t timeY = tbY + 4;
    draw_text_centered(clockX, timeY, clockW, kTaskbarH / 2 - 2, "12:00", rgb(200, 200, 210), 1);
    // Date below time (matching Legacy Taskbar.cs date rendering)
    uint32_t dateY = tbY + kTaskbarH / 2 + 2;
    draw_text_centered(clockX, dateY, clockW, kTaskbarH / 2 - 4, "1/1/2025", rgb(150, 150, 165), 1);

    // System tray
    uint32_t trayX = s_screenW - kShowDesktopW - trayW;
    draw_system_tray(trayX, tbY);

    // Show Desktop button (thin sliver on far right, matching Legacy Taskbar.cs)
    uint32_t sdX = s_screenW - kShowDesktopW;
    framebuffer::fill_rect(sdX, tbY, kShowDesktopW, kTaskbarH, rgb(50, 50, 60));
    // Separator before show desktop
    vline(sdX, tbY + 4, kTaskbarH - 8, rgb(70, 75, 90));
}

// ============================================================
// Start menu
// ============================================================

static void draw_start_menu()
{
    if (!s_startMenuOpen) return;

    // Two-column start menu matching Legacy StartMenu.cs layout
    // Left column: app list, Right column: system shortcuts
    uint32_t headerH = 30;
    uint32_t footerH = 36;
    uint32_t bodyH = (uint32_t)kStartMenuAppCount * kStartMenuItemH;
    uint32_t rightBodyH = (uint32_t)kStartMenuRightCount * kStartMenuItemH;
    uint32_t maxBodyH = bodyH > rightBodyH ? bodyH : rightBodyH;
    uint32_t menuH = headerH + maxBodyH + footerH;
    uint32_t menuX = 4;
    uint32_t menuY = s_screenH - kTaskbarH - menuH;
    uint32_t leftColW = kStartMenuW - kStartMenuRightColW;

    // Menu background
    framebuffer::fill_rect(menuX, menuY, kStartMenuW, menuH, rgb(40, 40, 50));
    draw_rect(menuX, menuY, kStartMenuW, menuH, rgb(80, 100, 140));

    // Header bar (user profile area, matching Legacy)
    framebuffer::fill_rect(menuX + 1, menuY + 1, kStartMenuW - 2, headerH - 1, rgb(50, 70, 110));
    // User avatar placeholder (small colored square)
    framebuffer::fill_rect(menuX + 8, menuY + 6, 18, 18, rgb(90, 140, 200));
    draw_rect(menuX + 8, menuY + 6, 18, 18, rgb(130, 170, 230));
    // Username
    draw_text(menuX + 32, menuY + 10, "User", rgb(230, 230, 250), 1);
    hline(menuX + 1, menuY + headerH, kStartMenuW - 2, rgb(60, 70, 90));

    // === Left column: Recent/Pinned Programs ===
    uint32_t leftX = menuX;
    uint32_t contentY = menuY + headerH + 1;

    // Left column background (slightly lighter)
    framebuffer::fill_rect(leftX + 1, contentY, leftColW - 1, maxBodyH, rgb(42, 42, 52));

    for (int i = 0; i < kStartMenuAppCount; i++) {
        uint32_t itemY = contentY + (uint32_t)i * kStartMenuItemH;
        if (itemY + kStartMenuItemH > menuY + headerH + maxBodyH) break;

        // Clicked item highlight (bright blue)
        if (i == s_clickedMenuLeft) {
            framebuffer::fill_rect(leftX + 1, itemY, leftColW - 2, kStartMenuItemH, rgb(50, 90, 160));
        }
        // Hover highlight (subtle blue tint)
        else if (i == s_hoverMenuLeft) {
            framebuffer::fill_rect(leftX + 1, itemY, leftColW - 2, kStartMenuItemH, rgb(55, 60, 80));
        }
        // Alternate row shading
        else if (i % 2 == 0) {
            framebuffer::fill_rect(leftX + 1, itemY, leftColW - 2, kStartMenuItemH, rgb(46, 46, 58));
        }

        // Small colored icon square
        uint32_t iconColor;
        if (i == 0 || i == 1) iconColor = rgb(70, 140, 200);
        else if (i == 2 || i == 5) iconColor = rgb(120, 180, 80);
        else if (i == 6) iconColor = rgb(200, 120, 60);
        else if (i == 7) iconColor = rgb(180, 70, 70);
        else iconColor = rgb(90, 130, 180);

        framebuffer::fill_rect(leftX + 10, itemY + 4, 20, 20, iconColor);
        draw_rect(leftX + 10, itemY + 4, 20, 20, rgb(160, 160, 180));

        // App name (brighter when hovered or clicked)
        uint32_t textColor = (i == s_clickedMenuLeft || i == s_hoverMenuLeft)
            ? rgb(255, 255, 255) : rgb(210, 210, 225);
        draw_text(leftX + 36, itemY + 10, s_startMenuApps[i], textColor, 1);
    }

    // Vertical divider between columns
    vline(leftX + leftColW, contentY, maxBodyH, rgb(60, 70, 90));

    // === Right column: System shortcuts (matching Legacy StartMenu.cs right panel) ===
    uint32_t rightX = leftX + leftColW + 1;
    framebuffer::fill_rect(rightX, contentY, kStartMenuRightColW - 2, maxBodyH, rgb(38, 38, 48));

    for (int i = 0; i < kStartMenuRightCount; i++) {
        uint32_t itemY = contentY + (uint32_t)i * kStartMenuItemH;
        if (itemY + kStartMenuItemH > menuY + headerH + maxBodyH) break;

        // Clicked item highlight (bright blue)
        if (i == s_clickedMenuRight) {
            framebuffer::fill_rect(rightX, itemY, kStartMenuRightColW - 2, kStartMenuItemH, rgb(50, 90, 160));
        }
        // Hover highlight (subtle blue tint)
        else if (i == s_hoverMenuRight) {
            framebuffer::fill_rect(rightX, itemY, kStartMenuRightColW - 2, kStartMenuItemH, rgb(50, 55, 72));
        }
        // Alternate shading
        else if (i % 2 == 1) {
            framebuffer::fill_rect(rightX, itemY, kStartMenuRightColW - 2, kStartMenuItemH, rgb(42, 42, 52));
        }

        // Small colored icon
        framebuffer::fill_rect(rightX + 8, itemY + 5, 16, 16, s_startMenuRight[i].color);
        draw_rect(rightX + 8, itemY + 5, 16, 16, rgb(140, 140, 160));

        // Label (brighter when hovered or clicked)
        uint32_t rTextColor = (i == s_clickedMenuRight || i == s_hoverMenuRight)
            ? rgb(255, 255, 255) : rgb(200, 200, 220);
        draw_text(rightX + 30, itemY + 10, s_startMenuRight[i].label, rTextColor, 1);
    }

    // === Footer: "All Programs" button + Power menu (matching Legacy) ===
    uint32_t footerY = menuY + headerH + maxBodyH;
    hline(menuX + 1, footerY, kStartMenuW - 2, rgb(60, 70, 90));

    // Footer background
    framebuffer::fill_rect(menuX + 1, footerY + 1, kStartMenuW - 2, footerH - 2, rgb(38, 38, 46));

    // "All Programs" toggle button (bottom-left, matching Legacy)
    uint32_t allBtnW = 110;
    uint32_t allBtnH = 24;
    uint32_t allBtnX = menuX + 10;
    uint32_t allBtnY = footerY + (footerH - allBtnH) / 2;
    framebuffer::fill_rect(allBtnX, allBtnY, allBtnW, allBtnH, rgb(50, 55, 65));
    draw_rect(allBtnX, allBtnY, allBtnW, allBtnH, rgb(70, 80, 100));
    draw_text_centered(allBtnX, allBtnY, allBtnW, allBtnH, "All Programs", rgb(190, 195, 210), 1);

    // Power buttons (right side of footer, matching Legacy)
    uint32_t shutW = 80;
    uint32_t shutH = 24;
    uint32_t shutX = menuX + kStartMenuW - shutW - 12;
    uint32_t shutY = footerY + (footerH - shutH) / 2;
    framebuffer::fill_rect(shutX, shutY, shutW, shutH, rgb(140, 50, 50));
    draw_rect(shutX, shutY, shutW, shutH, rgb(180, 80, 80));
    draw_text_centered(shutX, shutY, shutW, shutH, "Shut Down", rgb(240, 220, 220), 1);

    // Restart button (left of Shut Down)
    uint32_t restartW = 62;
    uint32_t restartX = shutX - restartW - 6;
    framebuffer::fill_rect(restartX, shutY, restartW, shutH, rgb(50, 60, 80));
    draw_rect(restartX, shutY, restartW, shutH, rgb(80, 100, 130));
    draw_text_centered(restartX, shutY, restartW, shutH, "Restart", rgb(200, 200, 220), 1);

    // Sleep button (left of Restart)
    uint32_t sleepW = 50;
    uint32_t sleepX = restartX - sleepW - 6;
    framebuffer::fill_rect(sleepX, shutY, sleepW, shutH, rgb(50, 60, 80));
    draw_rect(sleepX, shutY, sleepW, shutH, rgb(80, 100, 130));
    draw_text_centered(sleepX, shutY, sleepW, shutH, "Sleep", rgb(200, 200, 220), 1);
}

// ============================================================
// Right-click context menu (matching Legacy RightMenu.cs)
// ============================================================

static void draw_right_click_menu()
{
    if (!s_rightClickMenuOpen) return;

    uint32_t menuH = (uint32_t)kContextMenuCount * kContextMenuItemH + 4;
    uint32_t mx = s_rightClickX;
    uint32_t my = s_rightClickY;

    // Clamp to screen
    if (mx + kContextMenuW > s_screenW) mx = s_screenW - kContextMenuW;
    if (my + menuH > s_screenH - kTaskbarH) my = s_screenH - kTaskbarH - menuH;

    // Background with shadow
    framebuffer::fill_rect(mx + 2, my + 2, kContextMenuW, menuH, rgb(20, 20, 25));
    framebuffer::fill_rect(mx, my, kContextMenuW, menuH, rgb(45, 45, 55));
    draw_rect(mx, my, kContextMenuW, menuH, rgb(80, 90, 110));

    for (int i = 0; i < kContextMenuCount; i++) {
        uint32_t itemY = my + 2 + (uint32_t)i * kContextMenuItemH;

        // Alternate shading
        if (i % 2 == 0)
            framebuffer::fill_rect(mx + 1, itemY, kContextMenuW - 2, kContextMenuItemH, rgb(48, 48, 58));

        draw_text(mx + 12, itemY + (kContextMenuItemH - kGlyphH) / 2,
                  s_contextMenuItems[i], rgb(210, 210, 225), 1);

        // Separator after "Personalize" (index 2)
        if (i == 2) {
            hline(mx + 4, itemY + kContextMenuItemH - 1, kContextMenuW - 8, rgb(60, 65, 80));
        }
    }
}

// ============================================================
// Notification toasts (top-right, matching Legacy NotificationManager)
// ============================================================

static void draw_notifications()
{
    if (!s_notification.visible) return;

    uint32_t toastW = 260;
    uint32_t toastH = 60;
    uint32_t toastX = s_screenW - toastW - 12;
    uint32_t toastY = 12;

    // Shadow
    framebuffer::fill_rect(toastX + 3, toastY + 3, toastW, toastH, rgb(15, 15, 20));

    // Toast background
    framebuffer::fill_rect(toastX, toastY, toastW, toastH, rgb(50, 55, 65));
    draw_rect(toastX, toastY, toastW, toastH, rgb(80, 100, 150));

    // Accent bar on left
    framebuffer::fill_rect(toastX + 1, toastY + 1, 4, toastH - 2, rgb(70, 130, 220));

    // Title
    draw_text(toastX + 12, toastY + 10, s_notification.title, rgb(230, 230, 245), 1);

    // Message
    draw_text(toastX + 12, toastY + 26, s_notification.message, rgb(170, 175, 190), 1);

    // Close button (X) in top-right corner
    uint32_t closeX = toastX + toastW - 16;
    uint32_t closeY = toastY + 4;
    draw_text(closeX, closeY, "x", rgb(160, 160, 175), 1);

    // Timestamp
    draw_text(toastX + 12, toastY + 42, "Just now", rgb(120, 125, 140), 1);
}

// ============================================================
// Public API
// ============================================================

void init()
{
    s_screenW = framebuffer::get_width();
    s_screenH = framebuffer::get_height();
    s_startMenuOpen = false;
    s_rightClickMenuOpen = false;
    s_notification.visible = true;
    s_initialized = true;
}

void draw()
{
    if (!s_initialized || !framebuffer::is_available()) return;

    draw_background();
    draw_desktop_icons();
    draw_taskbar();
    draw_start_menu();
    draw_right_click_menu();
    draw_notifications();
}

void toggle_start_menu()
{
    s_startMenuOpen = !s_startMenuOpen;
    // Close context menu when start menu is toggled
    if (s_startMenuOpen) s_rightClickMenuOpen = false;
    // Reset hover/click state
    s_hoverMenuLeft = -1;
    s_hoverMenuRight = -1;
    s_clickedMenuLeft = -1;
    s_clickedMenuRight = -1;
}

bool is_start_menu_open()
{
    return s_startMenuOpen;
}

void show_context_menu(uint32_t x, uint32_t y)
{
    s_rightClickX = x;
    s_rightClickY = y;
    s_rightClickMenuOpen = true;
    // Close start menu when context menu is shown
    s_startMenuOpen = false;
}

void close_context_menu()
{
    s_rightClickMenuOpen = false;
}

void dismiss_notification()
{
    s_notification.visible = false;
}

// ============================================================
// Mouse cursor (12x19 arrow, 1-bit mask)
// ============================================================

// Classic arrow cursor bitmap (12 wide x 19 tall)
// 0=transparent, 1=black outline, 2=white fill
static const uint8_t s_cursorBitmap[19][12] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,1,1,1,1,1,0},
    {1,2,2,2,1,2,1,0,0,0,0,0},
    {1,2,2,1,0,1,2,1,0,0,0,0},
    {1,2,1,0,0,1,2,1,0,0,0,0},
    {1,1,0,0,0,0,1,2,1,0,0,0},
    {1,0,0,0,0,0,1,2,1,0,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0},
};

static const int kCursorW = 12;
static const int kCursorH = 19;

// Saved pixels under cursor for restore
static uint32_t s_cursorSave[19][12];
static int32_t  s_lastCursorX = -1;
static int32_t  s_lastCursorY = -1;
static bool     s_cursorDrawn = false;

// Previous button state for edge detection
static uint8_t  s_prevButtons = 0;

// Hit-test desktop icons: returns icon index or -1
static int hit_test_icon(int32_t mx, int32_t my)
{
    uint32_t deskH = s_screenH - kTaskbarH;
    uint32_t cols = (s_screenW - kIconMargin * 2) / kIconCellW;
    if (cols < 1) cols = 1;

    for (int i = 0; i < kDesktopIconCount; i++) {
        uint32_t col = (uint32_t)i % cols;
        uint32_t row = (uint32_t)i / cols;
        uint32_t cx = kIconMargin + col * kIconCellW;
        uint32_t cy = kIconMargin + row * kIconCellH;

        if (cy + kIconCellH > deskH) break;

        if ((uint32_t)mx >= cx && (uint32_t)mx < cx + kIconCellW &&
            (uint32_t)my >= cy && (uint32_t)my < cy + kIconCellH) {
            return i;
        }
    }
    return -1;
}

// Show a notification for an icon launch
static void show_icon_notification(int iconIndex)
{
    if (iconIndex < 0 || iconIndex >= kDesktopIconCount) return;
    s_notification.title = s_desktopIcons[iconIndex].label;
    s_notification.message = "Application launched";
    s_notification.visible = true;
}

// Compute start menu geometry (shared between drawing and hit-testing)
struct StartMenuGeometry {
    uint32_t menuX, menuY, menuH;
    uint32_t headerH, footerH, maxBodyH;
    uint32_t contentY;
    uint32_t leftColW;
    uint32_t rightX;
};

static StartMenuGeometry get_start_menu_geometry()
{
    StartMenuGeometry g;
    g.headerH = 30;
    g.footerH = 36;
    uint32_t bodyH = (uint32_t)kStartMenuAppCount * kStartMenuItemH;
    uint32_t rightBodyH = (uint32_t)kStartMenuRightCount * kStartMenuItemH;
    g.maxBodyH = bodyH > rightBodyH ? bodyH : rightBodyH;
    g.menuH = g.headerH + g.maxBodyH + g.footerH;
    g.menuX = 4;
    g.menuY = s_screenH - kTaskbarH - g.menuH;
    g.leftColW = kStartMenuW - kStartMenuRightColW;
    g.contentY = g.menuY + g.headerH + 1;
    g.rightX = g.menuX + g.leftColW + 1;
    return g;
}

// Hit-test start menu items: sets leftIdx or rightIdx to item index, or -1
static void hit_test_start_menu(int32_t mx, int32_t my, int& leftIdx, int& rightIdx)
{
    leftIdx = -1;
    rightIdx = -1;
    if (!s_startMenuOpen) return;

    StartMenuGeometry g = get_start_menu_geometry();

    // Check if inside menu bounds at all
    if ((uint32_t)mx < g.menuX || (uint32_t)mx >= g.menuX + kStartMenuW ||
        (uint32_t)my < g.menuY || (uint32_t)my >= g.menuY + g.menuH) {
        return;
    }

    // Only test in the body area (below header, above footer)
    if ((uint32_t)my < g.contentY || (uint32_t)my >= g.contentY + g.maxBodyH) {
        return;
    }

    uint32_t relY = (uint32_t)my - g.contentY;
    int itemRow = static_cast<int>(relY / kStartMenuItemH);

    // Left column
    if ((uint32_t)mx >= g.menuX && (uint32_t)mx < g.menuX + g.leftColW) {
        if (itemRow >= 0 && itemRow < kStartMenuAppCount) {
            leftIdx = itemRow;
        }
    }
    // Right column
    else if ((uint32_t)mx >= g.rightX && (uint32_t)mx < g.rightX + kStartMenuRightColW) {
        if (itemRow >= 0 && itemRow < kStartMenuRightCount) {
            rightIdx = itemRow;
        }
    }
}

// Show notification for a start menu item launch
static void show_start_menu_notification(const char* label)
{
    s_notification.title = label;
    s_notification.message = "Application launched";
    s_notification.visible = true;
}

static void save_under_cursor(int32_t mx, int32_t my)
{
    for (int row = 0; row < kCursorH; row++) {
        for (int col = 0; col < kCursorW; col++) {
            int32_t px = mx + col;
            int32_t py = my + row;
            if (px >= 0 && px < (int32_t)s_screenW && py >= 0 && py < (int32_t)s_screenH)
                s_cursorSave[row][col] = framebuffer::get_pixel((uint32_t)px, (uint32_t)py);
            else
                s_cursorSave[row][col] = 0;
        }
    }
}

static void restore_under_cursor()
{
    if (!s_cursorDrawn) return;
    for (int row = 0; row < kCursorH; row++) {
        for (int col = 0; col < kCursorW; col++) {
            int32_t px = s_lastCursorX + col;
            int32_t py = s_lastCursorY + row;
            if (px >= 0 && px < (int32_t)s_screenW && py >= 0 && py < (int32_t)s_screenH)
                framebuffer::put_pixel((uint32_t)px, (uint32_t)py, s_cursorSave[row][col]);
        }
    }
    s_cursorDrawn = false;
}

void draw_cursor(int32_t mx, int32_t my)
{
    // Restore previous cursor area
    restore_under_cursor();

    // Save pixels under new position
    save_under_cursor(mx, my);
    s_lastCursorX = mx;
    s_lastCursorY = my;

    // Draw cursor
    for (int row = 0; row < kCursorH; row++) {
        for (int col = 0; col < kCursorW; col++) {
            uint8_t p = s_cursorBitmap[row][col];
            if (p == 0) continue;
            int32_t px = mx + col;
            int32_t py = my + row;
            if (px >= 0 && px < (int32_t)s_screenW && py >= 0 && py < (int32_t)s_screenH) {
                uint32_t color = (p == 1) ? rgb(0, 0, 0) : rgb(255, 255, 255);
                framebuffer::put_pixel((uint32_t)px, (uint32_t)py, color);
            }
        }
    }
    s_cursorDrawn = true;
}

void handle_mouse(int32_t mx, int32_t my, uint8_t buttons)
{
    if (!s_initialized) return;

    // Detect button press edges (newly pressed)
    uint8_t pressed = buttons & ~s_prevButtons;
    s_prevButtons = buttons;

    uint32_t taskbarY = s_screenH - kTaskbarH;

    // Left button click
    if (pressed & 0x01) {
        // Close context menu on any click
        if (s_rightClickMenuOpen) {
            s_rightClickMenuOpen = false;
            draw();
            draw_cursor(mx, my);
            return;
        }

        // Start button area: x=[4..4+kStartBtnW], y=[taskbarY+4..taskbarY+kTaskbarH-4]
        if ((uint32_t)mx >= 4 && (uint32_t)mx <= 4 + kStartBtnW &&
            (uint32_t)my >= taskbarY + 4 && (uint32_t)my <= taskbarY + kTaskbarH - 4) {
            toggle_start_menu();
            draw();
            draw_cursor(mx, my);
            return;
        }

        // If start menu is open and click is outside it, close it
        if (s_startMenuOpen) {
            // First check if click is on a start menu item
            int leftHit = -1, rightHit = -1;
            hit_test_start_menu(mx, my, leftHit, rightHit);

            if (leftHit >= 0) {
                // Clicked a left-column app
                s_clickedMenuLeft = leftHit;
                s_clickedMenuRight = -1;
                show_start_menu_notification(s_startMenuApps[leftHit]);
                draw();
                draw_cursor(mx, my);
                return;
            }
            if (rightHit >= 0) {
                // Clicked a right-column item
                s_clickedMenuRight = rightHit;
                s_clickedMenuLeft = -1;
                show_start_menu_notification(s_startMenuRight[rightHit].label);
                draw();
                draw_cursor(mx, my);
                return;
            }

            // Click outside start menu items — close it
            s_startMenuOpen = false;
            s_hoverMenuLeft = -1;
            s_hoverMenuRight = -1;
            s_clickedMenuLeft = -1;
            s_clickedMenuRight = -1;
            draw();
            draw_cursor(mx, my);
            return;
        }

        // Desktop icon click
        int iconIdx = hit_test_icon(mx, my);
        if (iconIdx >= 0) {
            s_selectedIcon = iconIdx;
            show_icon_notification(iconIdx);
            draw();
            draw_cursor(mx, my);
            return;
        }

        // Click on empty desktop area: deselect icon
        if ((uint32_t)my < taskbarY) {
            if (s_selectedIcon >= 0) {
                s_selectedIcon = -1;
                draw();
                draw_cursor(mx, my);
                return;
            }
        }

        // Notification dismiss: check if clicking the notification toast
        if (s_notification.visible) {
            uint32_t toastW = 280;
            uint32_t toastH = 64;
            uint32_t toastX = s_screenW - toastW - 16;
            uint32_t toastY = taskbarY - toastH - 12;
            if ((uint32_t)mx >= toastX && (uint32_t)mx <= toastX + toastW &&
                (uint32_t)my >= toastY && (uint32_t)my <= toastY + toastH) {
                dismiss_notification();
                draw();
                draw_cursor(mx, my);
                return;
            }
        }
    }

    // Right button click — show context menu on desktop area
    if (pressed & 0x02) {
        if ((uint32_t)my < taskbarY) {
            show_context_menu((uint32_t)mx, (uint32_t)my);
            draw();
            draw_cursor(mx, my);
            return;
        }
    }

    // Start menu hover tracking (on any mouse move, not just clicks)
    if (s_startMenuOpen) {
        int newHoverLeft = -1, newHoverRight = -1;
        hit_test_start_menu(mx, my, newHoverLeft, newHoverRight);

        if (newHoverLeft != s_hoverMenuLeft || newHoverRight != s_hoverMenuRight) {
            s_hoverMenuLeft = newHoverLeft;
            s_hoverMenuRight = newHoverRight;
            draw();
            draw_cursor(mx, my);
            return;
        }
    }

    // Always redraw cursor at new position
    draw_cursor(mx, my);
}

} // namespace desktop
} // namespace kernel
