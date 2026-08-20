//
// guideXOS Kernel GUI Apps Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/kernel_apps.h"
#include "include/kernel/kernel_compositor.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/desktop.h"
#include "include/kernel/shell.h"
#include "include/kernel/ps2keyboard.h"
#include "include/kernel/vfs.h"
#include "include/kernel/pit.h"
#include "include/kernel/time.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/desktop_icon_theme_flat.h"
#include "include/kernel/file_clipboard.h"
#include "include/kernel/image_adapter.h"
#include "include/kernel/system_font.h"
#include "include/kernel/nic.h"
#include "include/kernel/ipv4.h"
#include "include/kernel/tcp.h"
#include "include/kernel/dns.h"
#include "include/kernel/virtio_rng.h"
#include "include/kernel/virtio_gpu.h"
#include "../../built_in_app_metadata.h"
#include "../../gxos_tls_foundation.h"
#include "../../gxos_tls_prerequisites.h"
#include "../../guide_web_http_shared.h"

#include <string.h>

extern "C" void desktop_request_redraw();

namespace kernel {
namespace apps {

static bool s_kernelLastDnsUsed = false;
static char s_kernelLastDnsHost[gxos::web::kHttpSharedMaxHostnameBytes + 1];
static char s_kernelLastDnsResolvedIp[16];
static char s_kernelLastDnsError[64];

static void strappend(char* dst, const char* src, int maxLen);

namespace {
constexpr int kNavigatorToolbarButtonY = 12;
constexpr int kNavigatorToolbarButtonGap = 6;
constexpr int kNavigatorToolbarLeadingX = 16;
constexpr int kNavigatorToolbarButtonMinW = 52;
constexpr int kNavigatorToolbarIconSize = 16;
constexpr int kNavigatorToolbarIconLeftPad = 4;
constexpr int kNavigatorToolbarIconTextGap = 4;
constexpr int kNavigatorToolbarTextLeftPad = 6;
constexpr int kNavigatorToolbarTextRightPad = 6;
constexpr int kNavigatorToolbarAddressGap = 8;
constexpr int kNavigatorToolbarAddressRightPad = 20;

struct NavigatorToolbarLayout {
    int x[6]{};
    int w[6]{};
    int addressX = 0;
    int addressW = 0;
};

static NavigatorToolbarLayout navigatorToolbarLayout(int windowWidth)
{
    static const char* labels[6] = {"Back", "Next", "Reload", "Home", "Marks", "Add"};
    NavigatorToolbarLayout layout;
    int x = kNavigatorToolbarLeadingX;
    gxos::gui::SystemFont::EnsureInitialized();
    for (int i = 0; i < 6; ++i) {
        const int labelWidth = gxos::gui::SystemFont::MeasureWidth(gxos::gui::FontRole::Default, labels[i]);
        const int contentWidth = kNavigatorToolbarIconLeftPad + kNavigatorToolbarIconSize +
            kNavigatorToolbarIconTextGap + labelWidth + kNavigatorToolbarTextRightPad;
        layout.x[i] = x;
        layout.w[i] = contentWidth > kNavigatorToolbarButtonMinW ? contentWidth : kNavigatorToolbarButtonMinW;
        x += layout.w[i] + kNavigatorToolbarButtonGap;
    }
    layout.addressX = x + kNavigatorToolbarAddressGap;
    layout.addressW = windowWidth - layout.addressX - kNavigatorToolbarAddressRightPad;
    if (layout.addressW < 0) layout.addressW = 0;
    return layout;
}
}

static gxos::GxosCaStoreInfo probe_missing_ca_path()
{
    const char* probePath = "/certs/ca-bundle.missing";
    const gxos::GxosCaManifestInfo manifestInfo = {
        gxos::GxosCaManifestStatus::NotApplicable,
        "/certs/ca-bundle.manifest",
        0,
        false,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        0,
        0,
        false,
        false,
        false,
        nullptr
    };
    kernel::vfs::FileInfo info{};
    const kernel::vfs::Status statStatus = kernel::vfs::stat(probePath, &info);
    if (statStatus == kernel::vfs::VFS_ERR_NOT_FOUND || statStatus == kernel::vfs::VFS_ERR_NOT_MOUNT) {
        return {
            gxos::GxosCaStoreStatus::Missing,
            gxos::GxosCaParseStatus::NotAttempted,
            0,
            0,
            0,
            false,
            probePath,
            "Smoke-only missing CA probe correctly fails closed.",
            manifestInfo
        };
    }
    return {
        gxos::GxosCaStoreStatus::ReadError,
        gxos::GxosCaParseStatus::NotAttempted,
        0,
        0,
        0,
        false,
        probePath,
        "Smoke-only missing CA probe did not fail closed.",
        manifestInfo
    };
}

struct NavigatorSmokePathProbe {
    const kernel::vfs::MountPoint* mount = nullptr;
    kernel::vfs::Status statStatus = kernel::vfs::VFS_ERR_INVALID;
    kernel::vfs::FileType type = kernel::vfs::FILE_TYPE_REGULAR;
    int32_t readStatus = kernel::vfs::VFS_ERR_INVALID;
    size_t bytesRead = 0;
    bool pemHeaderPresent = false;
};

static bool buffer_contains_token_local(const uint8_t* buffer, size_t len, const char* token)
{
    if (!buffer || !token) return false;
    size_t tokenLen = 0;
    while (token[tokenLen]) ++tokenLen;
    if (tokenLen == 0 || len < tokenLen) return false;
    for (size_t offset = 0; offset + tokenLen <= len; ++offset) {
        bool match = true;
        for (size_t i = 0; i < tokenLen; ++i) {
            if (buffer[offset + i] != static_cast<uint8_t>(token[i])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

static NavigatorSmokePathProbe probe_navigator_smoke_path(const char* path, bool readFile)
{
    NavigatorSmokePathProbe probe{};
    probe.mount = kernel::vfs::get_mount(path);

    kernel::vfs::FileInfo info{};
    probe.statStatus = kernel::vfs::stat(path, &info);
    if (probe.statStatus == kernel::vfs::VFS_OK) {
        probe.type = info.type;
    }

    if (!readFile || probe.statStatus != kernel::vfs::VFS_OK || info.type != kernel::vfs::FILE_TYPE_REGULAR) {
        return probe;
    }

    uint8_t buffer[256] = {};
    probe.readStatus = kernel::vfs::read_file(path, buffer, sizeof(buffer));
    if (probe.readStatus > 0) {
        probe.bytesRead = static_cast<size_t>(probe.readStatus);
        probe.pemHeaderPresent = buffer_contains_token_local(buffer, probe.bytesRead, "-----BEGIN CERTIFICATE-----");
    }

    return probe;
}

static int navigatorToolbarSmokeIconResourceCount()
{
    static const char* toolbarPaths[6] = {
        "/system/config/navigator/nav-back.png",
        "/system/config/navigator/nav-next.png",
        "/system/config/navigator/reload.png",
        "/system/config/navigator/nav-home.png",
        "/system/config/navigator/marks.png",
        "/system/config/navigator/nav-add.png"
    };
    int available = 0;
    for (const char* path : toolbarPaths) {
        kernel::vfs::FileInfo info{};
        if (kernel::vfs::stat(path, &info) == kernel::vfs::VFS_OK &&
            info.type == kernel::vfs::FILE_TYPE_REGULAR) {
            ++available;
        }
    }
    return available;
}

// ============================================================
// Helper: string copy
// ============================================================

static void strcopy(char* dst, const char* src, int maxLen) {
    int i = 0;
    while (src[i] && i < maxLen - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int strlen_local(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static bool streq_local(const char* a, const char* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return false;
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static bool endsWithIgnoreCaseLocal(const char* value, const char* suffix) {
    if (!value || !suffix) return false;
    int valueLen = strlen_local(value);
    int suffixLen = strlen_local(suffix);
    if (suffixLen > valueLen) return false;
    for (int i = 0; i < suffixLen; ++i) {
        char a = value[valueLen - suffixLen + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

// ============================================================
// Color helpers
// ============================================================

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t css_color_or(uint32_t fallback, const gxos::web::WebStyle& style) {
    return style.hasColor ? style.color : fallback;
}

static uint32_t css_background_or(uint32_t fallback, const gxos::web::WebStyle& style) {
    return style.hasBackgroundColor ? style.backgroundColor : fallback;
}

static int css_margin_top_or(const gxos::web::WebStyle& style, int fallback) {
    return style.marginTop >= 0 ? style.marginTop : fallback;
}

static int css_margin_bottom_or(const gxos::web::WebStyle& style, int fallback) {
    return style.marginBottom >= 0 ? style.marginBottom : fallback;
}

static int css_margin_left_or(const gxos::web::WebStyle& style, int fallback) {
    return style.marginLeft >= 0 ? style.marginLeft : fallback;
}

static int css_padding_or(const gxos::web::WebStyle& style, int fallback) {
    return style.padding >= 0 ? style.padding : fallback;
}

// Bitmap font constants (same as compositor)
static const int kGlyphW = 5;
static const int kGlyphH = 7;
static const int kGlyphSpacing = 1;
static const int kGlyphCount = 95;

// Bitmap font glyph data (5x7, ASCII 32..126)
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

static const uint8_t* getGlyph(char c) {
    int idx = (int)(unsigned char)c - 32;
    if (idx < 0 || idx >= kGlyphCount) return nullptr;
    return s_glyphs[idx];
}

static void kernel_join_path(const char* base, const char* name, char* out, int outSize);
static const char* kernel_vfs_status_text(vfs::Status status);
static void nav_int_to_text(int value, char* out, int outSize);
static void nav_i64_to_text(int64_t value, char* out, int outSize);
static bool nav_char_is_filename_safe(char c);
static void nav_copy_basename_without_query(const char* urlOrPath, char* out, int outSize);
static void nav_make_safe_download_filename(const char* urlOrPath, char* out, int outSize);
static bool kernel_downloads_directory_ready(char* reason, int reasonSize);
static bool kernel_make_unique_download_path(const char* finalUrl, char* outPath, int outPathSize, char* outFileName, int outFileNameSize, char* error, int errorSize);
static bool kernel_write_binary_file_bare_metal(const char* path, const char* bytes, int byteCount, char* error, int errorSize);

// Draw a single character using the bitmap font
static void drawChar(uint32_t px, uint32_t py, char c, uint32_t color) {
    const uint8_t* g = getGlyph(c);
    if (!g) return;
    for (int col = 0; col < kGlyphW; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < kGlyphH; row++) {
            if (bits & (1 << row)) {
                framebuffer::put_pixel(px + col, py + row, color);
            }
        }
    }
}

static bool nav_char_is_filename_safe(char c)
{
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '.' || c == '-' || c == '_';
}

static void nav_copy_basename_without_query(const char* urlOrPath, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    out[0] = '\0';
    if (!urlOrPath || !urlOrPath[0]) return;

    const char* start = urlOrPath;
    const char* scheme = nullptr;
    for (const char* p = urlOrPath; *p; ++p) {
        if (p[0] == ':' && p[1] == '/' && p[2] == '/') {
            scheme = p;
            break;
        }
        if (*p == '/' || *p == '?' || *p == '#') break;
    }
    if (scheme) {
        start = scheme + 3;
        while (*start && *start != '/') ++start;
    }

    const char* end = start;
    while (*end && *end != '?' && *end != '#') ++end;
    const char* base = start;
    for (const char* p = start; p < end; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }

    int oi = 0;
    for (const char* p = base; p < end && oi < outSize - 1; ++p) out[oi++] = *p;
    out[oi] = '\0';
}

static void nav_make_safe_download_filename(const char* urlOrPath, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    char base[vfs::VFS_MAX_FILENAME];
    nav_copy_basename_without_query(urlOrPath, base, sizeof(base));

    int start = 0;
    while (base[start] == '.' || base[start] == '/' || base[start] == '\\') ++start;

    int oi = 0;
    bool lastUnderscore = false;
    for (int i = start; base[i] && oi < outSize - 1; ++i) {
        char c = base[i];
        if (nav_char_is_filename_safe(c)) {
            out[oi++] = c;
            lastUnderscore = false;
        } else if (!lastUnderscore) {
            out[oi++] = '_';
            lastUnderscore = true;
        }
    }
    while (oi > 0 && (out[oi - 1] == '.' || out[oi - 1] == '_')) --oi;
    out[oi] = '\0';
    if (!out[0]) {
        strcopy(out, "download.bin", outSize);
        return;
    }

    const int kMaxFileNameLen = 64;
    if (oi > kMaxFileNameLen) {
        int dot = -1;
        for (int i = oi - 1; i > 0; --i) {
            if (out[i] == '.') { dot = i; break; }
        }
        if (dot > 0 && oi - dot <= 8) {
            int extLen = oi - dot;
            int keepStem = kMaxFileNameLen - extLen;
            if (keepStem < 1) keepStem = 1;
            for (int i = 0; i < extLen; ++i) out[keepStem + i] = out[dot + i];
            out[keepStem + extLen] = '\0';
        } else {
            out[kMaxFileNameLen] = '\0';
        }
    }
}

static bool kernel_downloads_directory_ready(char* reason, int reasonSize)
{
    if (reason && reasonSize > 0) reason[0] = '\0';
    vfs::FileInfo info{};
    vfs::Status statStatus = vfs::stat("/downloads", &info);
    if (statStatus == vfs::VFS_OK) {
        if (info.type == vfs::FILE_TYPE_DIRECTORY) return true;
        strcopy(reason, "'/downloads' exists but is not a directory", reasonSize);
        return false;
    }
    vfs::Status mkdirStatus = vfs::mkdir("/downloads");
    if (mkdirStatus == vfs::VFS_OK) return true;
    if (mkdirStatus == vfs::VFS_ERR_EXISTS) {
        if (vfs::stat("/downloads", &info) == vfs::VFS_OK && info.type == vfs::FILE_TYPE_DIRECTORY) return true;
    }
    strcopy(reason, kernel_vfs_status_text(mkdirStatus), reasonSize);
    return false;
}

static bool kernel_make_unique_download_path(const char* finalUrl, char* outPath, int outPathSize, char* outFileName, int outFileNameSize, char* error, int errorSize)
{
    if (outPath && outPathSize > 0) outPath[0] = '\0';
    if (outFileName && outFileNameSize > 0) outFileName[0] = '\0';
    if (error && errorSize > 0) error[0] = '\0';

    char safeName[vfs::VFS_MAX_FILENAME];
    nav_make_safe_download_filename(finalUrl, safeName, sizeof(safeName));
    if (!safeName[0]) strcopy(safeName, "download.bin", sizeof(safeName));

    kernel_join_path("/downloads", safeName, outPath, outPathSize);
    if (!vfs::exists(outPath)) {
        strcopy(outFileName, safeName, outFileNameSize);
        return true;
    }

    char stem[vfs::VFS_MAX_FILENAME];
    char ext[16];
    stem[0] = '\0';
    ext[0] = '\0';
    int dot = -1;
    int nameLen = strlen_local(safeName);
    for (int i = nameLen - 1; i > 0; --i) {
        if (safeName[i] == '.') { dot = i; break; }
    }
    if (dot > 0) {
        int si = 0;
        while (si < dot && si < (int)sizeof(stem) - 1) {
            stem[si] = safeName[si];
            ++si;
        }
        stem[si] = '\0';
        strcopy(ext, safeName + dot, sizeof(ext));
    } else {
        strcopy(stem, safeName, sizeof(stem));
    }
    if (!stem[0]) strcopy(stem, "download", sizeof(stem));

    for (int index = 1; index < 100; ++index) {
        char suffix[16];
        nav_int_to_text(index, suffix, sizeof(suffix));
        char candidate[vfs::VFS_MAX_FILENAME];
        candidate[0] = '\0';
        int stemLen = strlen_local(stem);
        int extLen = strlen_local(ext);
        int suffixLen = 1 + strlen_local(suffix);
        int keepStem = stemLen;
        const int kMaxFileNameLen = 64;
        if (keepStem + suffixLen + extLen > kMaxFileNameLen) keepStem = kMaxFileNameLen - suffixLen - extLen;
        if (keepStem < 1) keepStem = 1;
        for (int i = 0; i < keepStem && i < (int)sizeof(candidate) - 1; ++i) candidate[i] = stem[i];
        candidate[keepStem] = '\0';
        strappend(candidate, "-", sizeof(candidate));
        strappend(candidate, suffix, sizeof(candidate));
        strappend(candidate, ext, sizeof(candidate));
        kernel_join_path("/downloads", candidate, outPath, outPathSize);
        if (!vfs::exists(outPath)) {
            strcopy(outFileName, candidate, outFileNameSize);
            return true;
        }
    }

    strcopy(error, "No unique download filename available", errorSize);
    return false;
}

static bool kernel_write_binary_file_bare_metal(const char* path, const char* bytes, int byteCount, char* error, int errorSize)
{
    if (error && errorSize > 0) error[0] = '\0';
    if (!path || (!bytes && byteCount > 0)) {
        strcopy(error, "Invalid download write request", errorSize);
        return false;
    }
    int32_t written = vfs::write_file(path, bytes, (uint32_t)(byteCount > 0 ? byteCount : 0));
    if (written < 0) {
        strcopy(error, kernel_vfs_status_text((vfs::Status)written), errorSize);
        return false;
    }
    if (written != byteCount) {
        strcopy(error, "Partial download write", errorSize);
        return false;
    }
    return true;
}

static void strappend(char* dst, const char* src, int maxLen) {
    if (!dst || !src || maxLen <= 0) return;
    int len = strlen_local(dst);
    int i = 0;
    while (src[i] && len < maxLen - 1) {
        dst[len++] = src[i++];
    }
    dst[len] = '\0';
}

static void uint64_to_text(uint64_t value, char* out, int outSize) {
    if (!out || outSize <= 0) return;
    char tmp[32];
    int pos = 0;
    do {
        tmp[pos++] = static_cast<char>('0' + (value % 10ULL));
        value /= 10ULL;
    } while (value != 0 && pos < static_cast<int>(sizeof(tmp)));

    int outPos = 0;
    while (pos > 0 && outPos < outSize - 1) {
        out[outPos++] = tmp[--pos];
    }
    out[outPos] = '\0';
}

static void int_to_text(int value, char* out, int outSize) {
    if (!out || outSize <= 0) return;
    if (value < 0) {
        if (outSize < 2) {
            out[0] = '\0';
            return;
        }
        out[0] = '-';
        uint64_to_text(static_cast<uint64_t>(-(int64_t)value), out + 1, outSize - 1);
        return;
    }
    uint64_to_text(static_cast<uint64_t>(value), out, outSize);
}

static bool startsWithText(const char* value, const char* prefix) {
    if (!value || !prefix) return false;
    while (*prefix) {
        if (*value++ != *prefix++) return false;
    }
    return true;
}

static const char* kKernelTrashRootPath = "/Trash";

static void kernel_trash_root_for_mount(const char* mountPath, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    if (!mountPath || !mountPath[0] || (mountPath[0] == '/' && mountPath[1] == '\0')) {
        strcopy(out, kKernelTrashRootPath, outSize);
        return;
    }
    strcopy(out, mountPath, outSize);
    strappend(out, "/Trash", outSize);
}

static bool kernel_trash_root_has_items(const char* trashRoot)
{
    vfs::DirEntry entry{};
    uint8_t dir = vfs::opendir(trashRoot);
    if (dir == 0xFF) return false;
    bool hasItems = false;
    while (vfs::readdir(dir, &entry)) {
        if (entry.name[0] == '.' && (entry.name[1] == '\0' || (entry.name[1] == '.' && entry.name[2] == '\0'))) continue;
        if (file_clipboard::is_trash_metadata_name(entry.name)) continue;
        hasItems = true;
        break;
    }
    vfs::closedir(dir);
    return hasItems;
}

static bool kernel_trash_exists()
{
    bool hasItems = false;
    for (uint8_t i = 0; i < vfs::VFS_MAX_MOUNTS; ++i) {
        const vfs::MountPoint* mp = vfs::get_mount_by_index(i);
        if (!mp || !mp->active) continue;
        char trashRoot[256];
        kernel_trash_root_for_mount(mp->path, trashRoot, sizeof(trashRoot));
        if (kernel_trash_root_has_items(trashRoot)) {
            hasItems = true;
            break;
        }
    }
    serial::puts("[trash] item count computed=");
    serial::puts(hasItems ? "nonzero" : "0");
    serial::puts(" iconKey=");
    serial::puts(hasItems ? "trash.full" : "trash.empty");
    serial::puts("\n");
    return hasItems;
}
static void kernel_desktop_refresh_trash_state()
{
    serial::puts("[trash] desktop refresh requested; hasItems=");
    serial::puts(kernel_trash_exists() ? "yes" : "no");
    serial::puts("\n");
    desktop_request_redraw();
}

static const char* kernel_vfs_status_text(vfs::Status status)
{
    switch (status) {
        case vfs::VFS_OK: return "OK";
        case vfs::VFS_ERR_NOT_FOUND: return "Path not found";
        case vfs::VFS_ERR_EXISTS: return "Already exists";
        case vfs::VFS_ERR_NOT_DIR: return "Parent is not a directory";
        case vfs::VFS_ERR_IS_DIR: return "Path is a directory";
        case vfs::VFS_ERR_NOT_EMPTY: return "Directory is not empty";
        case vfs::VFS_ERR_NO_SPACE: return "No space left";
        case vfs::VFS_ERR_READ_ONLY: return "Filesystem is read-only";
        case vfs::VFS_ERR_INVALID: return "Invalid filesystem operation";
        case vfs::VFS_ERR_IO: return "Filesystem I/O error";
        case vfs::VFS_ERR_NOT_MOUNT: return "No mounted filesystem for path";
        case vfs::VFS_ERR_BUSY: return "Filesystem busy";
        case vfs::VFS_ERR_TOO_MANY: return "Too many open filesystem objects";
        case vfs::VFS_ERR_NOT_SUPPORTED: return "Filesystem operation not supported";
        case vfs::VFS_ERR_DIRECTORY_NOT_EMPTY: return "Directory is not empty";
        case vfs::VFS_ERR_RECURSION_LIMIT: return "Directory traversal is too deep";
        case vfs::VFS_ERR_ENTRY_LIMIT: return "Too many directory entries";
        case vfs::VFS_ERR_CORRUPT_DIRECTORY: return "Directory is corrupt";
        case vfs::VFS_ERR_INVALID_DESTINATION: return "Invalid destination";
        case vfs::VFS_ERR_ROLLBACK_FAILED: return "Rollback failed";
        default: return "Filesystem operation failed";
    }
}

static void kernel_join_path(const char* base, const char* name, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    vfs::join_path(base, name, out, (size_t)outSize);
}

static bool kernel_join_path_within_base(const char* base, const char* name, char* out, int outSize)
{
    if (!base || !base[0] || !name || !name[0] || !out || outSize <= 0) return false;

    char joined[256];
    char normalizedJoined[256];
    char normalizedBase[256];
    kernel_join_path(base, name, joined, sizeof(joined));
    vfs::normalize_path(joined, normalizedJoined, sizeof(normalizedJoined));
    vfs::normalize_path(base, normalizedBase, sizeof(normalizedBase));

    const int baseLen = strlen_local(normalizedBase);
    if (baseLen <= 0) return false;
    if (baseLen == 1 && normalizedBase[0] == '/') {
        if (normalizedJoined[0] != '/' || normalizedJoined[1] == '\0') return false;
    } else {
        if (!startsWithText(normalizedJoined, normalizedBase)) return false;
        if (normalizedJoined[baseLen] != '/' || normalizedJoined[baseLen + 1] == '\0') return false;
    }

    strcopy(out, normalizedJoined, outSize);
    return true;
}

static void kernel_trash_info_path_for(const char* trashedPath, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    out[0] = '\0';
    file_clipboard::trash_metadata_path_for(trashedPath, out, static_cast<size_t>(outSize));
}

static void kernel_make_fat_safe_collision_name(const char* baseName, bool isDir, int index, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    out[0] = '\0';

    char stem[9];
    char ext[4];
    int stemLen = 0;
    int extLen = 0;
    int dot = -1;
    int nameLen = strlen_local(baseName);
    if (!isDir) {
        for (int i = nameLen - 1; i >= 0; --i) {
            if (baseName[i] == '.') { dot = i; break; }
        }
    }

    int stemEnd = dot > 0 ? dot : nameLen;
    for (int i = 0; i < stemEnd && stemLen < 6; ++i) {
        char c = baseName[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') stem[stemLen++] = c;
    }
    if (stemLen == 0) stem[stemLen++] = 'I';
    stem[stemLen] = '\0';

    if (!isDir && dot > 0) {
        for (int i = dot + 1; baseName[i] && extLen < 3; ++i) {
            char c = baseName[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') ext[extLen++] = c;
        }
    }
    ext[extLen] = '\0';

    strcopy(out, stem, outSize);
    strappend(out, "~", outSize);
    char digits[12];
    int di = 0;
    int value = index;
    char rev[12];
    int ri = 0;
    while (value > 0) { rev[ri++] = (char)('0' + (value % 10)); value /= 10; }
    while (ri > 0 && di < 2) digits[di++] = rev[--ri];
    digits[di] = '\0';
    strappend(out, digits, outSize);
    if (!isDir && extLen > 0) {
        strappend(out, ".", outSize);
        strappend(out, ext, outSize);
    }
}

static bool kernel_move_path_to_trash(const char* sourcePath, const char* sourceName, bool isDir, char* movedPath, int movedPathSize, char* error, int errorSize)
{
    (void)sourceName;
    (void)isDir;
    if (!movedPath || movedPathSize <= 0 || !error || errorSize <= 0) return false;
    movedPath[0] = '\0';
    error[0] = '\0';
    const file_clipboard::PasteResult result = file_clipboard::move_to_trash(
        sourcePath, movedPath, static_cast<size_t>(movedPathSize));
    if (result == file_clipboard::PasteResult::Success) {
        serial::puts("[fileexplorer-bm] shared move-to-trash success path=");
        serial::puts(movedPath);
        serial::puts("\n");
        return true;
    }
    strcopy(error, file_clipboard::paste_diagnostic_message(), errorSize);
    serial::puts("[fileexplorer-bm] shared move-to-trash failed path=");
    serial::puts(sourcePath ? sourcePath : "<null>");
    serial::puts(" reason=");
    serial::puts(error);
    serial::puts("\n");
    return false;
}

static void appDrawText(uint32_t x, uint32_t y, const char* text, uint32_t color) {
    uint32_t cx = x;
    while (text && *text) {
        drawChar(cx, y, *text, color);
        cx += kGlyphW + kGlyphSpacing;
        text++;
    }
}

// Navigator document text uses the same bounded SystemFont face/metric path
// as the hosted renderer.  Keep this adapter local to Navigator so legacy
// kernel app chrome continues to use its established bitmap text path.
static int navigatorFontPixelSize(const gxos::web::WebStyle& style)
{
    int px = style.fontScaleOrSize > 0 ? style.fontScaleOrSize : 12;
    if (px < 1) px = 1;
    if (px > 72) px = 72;
    return px;
}

static bool navigatorUsesMonospace(const gxos::web::WebStyle& style)
{
    return style.genericFontFamily == gxos::web::GenericFontFamily::Monospace;
}

static const gxos::gui::BitmapFontFace* navigatorFontFace(const gxos::web::WebStyle& style)
{
    if (navigatorUsesMonospace(style)) return nullptr;
    const gxos::gui::FontWeight weight = style.bold
        ? gxos::gui::FontWeight::Bold
        : gxos::gui::FontWeight::Regular;
    const gxos::gui::FontSlant slant = style.italic
        ? gxos::gui::FontSlant::Italic
        : gxos::gui::FontSlant::Normal;
    return gxos::gui::SystemFont::GetFaceForPixelSize(navigatorFontPixelSize(style), weight, slant);
}

static int navigatorFontScale(const gxos::web::WebStyle& style)
{
    return gxos::gui::SystemFont::ScalePercentForPixelSize(navigatorFontPixelSize(style));
}

static int navigatorTextWidth(const gxos::web::WebStyle& style, const char* text, int len = -1)
{
    if (!text) return 0;
    if (navigatorUsesMonospace(style)) {
        const int textLen = len >= 0 ? len : strlen_local(text);
        return textLen * (kGlyphW + kGlyphSpacing);
    }
    const gxos::gui::BitmapFontFace* face = navigatorFontFace(style);
    return gxos::gui::SystemFont::MeasureWidthScaled(face, text, len, navigatorFontScale(style));
}

static int navigatorLineHeight(const gxos::web::WebStyle& style)
{
    if (navigatorUsesMonospace(style)) return kGlyphH + 3;
    const gxos::gui::BitmapFontFace* face = navigatorFontFace(style);
    int lineHeight = gxos::gui::SystemFont::MeasureLineHeightScaled(face, navigatorFontScale(style));
    return lineHeight > 0 ? lineHeight : 1;
}

static int navigatorNextLineBreak(const char* text, int start, int end, int maxWidth,
                                  const gxos::web::WebStyle& style)
{
    if (!text || start >= end) return start;
    if (maxWidth < 1) maxWidth = 1;

    const gxos::gui::BitmapFontFace* face = navigatorFontFace(style);
    const int scale = navigatorFontScale(style);
    int width = 0;
    int lastSpace = -1;
    int pos = start;
    while (pos < end) {
        const int advance = navigatorUsesMonospace(style)
            ? (kGlyphW + kGlyphSpacing)
            : gxos::gui::SystemFont::MeasureWidthScaled(face, text + pos, 1, scale);
        if (pos > start && width + advance > maxWidth) {
            return lastSpace > start ? lastSpace : pos;
        }
        width += advance;
        if (text[pos] == ' ') lastSpace = pos;
        ++pos;
    }
    return end;
}

static int navigatorWrappedLineCount(const char* text, int maxWidth,
                                     const gxos::web::WebStyle& style)
{
    if (!text || !text[0]) return 1;
    const int len = strlen_local(text);
    int lines = 0;
    int physicalStart = 0;
    while (physicalStart <= len) {
        int physicalEnd = physicalStart;
        while (physicalEnd < len && text[physicalEnd] != '\n') ++physicalEnd;
        ++lines;
        int pos = physicalStart;
        while (pos < physicalEnd) {
            int breakAt = navigatorNextLineBreak(text, pos, physicalEnd, maxWidth, style);
            if (breakAt <= pos) breakAt = pos + 1;
            pos = breakAt;
            while (pos < physicalEnd && text[pos] == ' ') ++pos;
        }
        if (physicalEnd >= len) break;
        physicalStart = physicalEnd + 1;
    }
    return lines > 0 ? lines : 1;
}

static int navigatorLineCharOffset(const char* text, int start, int length, int x,
                                   const gxos::web::WebStyle& style)
{
    if (!text || length <= 0 || x <= 0) return 0;
    const gxos::gui::BitmapFontFace* face = navigatorFontFace(style);
    const int scale = navigatorFontScale(style);
    int width = 0;
    for (int i = 0; i < length; ++i) {
        const int advance = navigatorUsesMonospace(style)
            ? (kGlyphW + kGlyphSpacing)
            : gxos::gui::SystemFont::MeasureWidthScaled(face, text + start + i, 1, scale);
        if (x < width + (advance + 1) / 2) return i;
        width += advance;
    }
    return length;
}

static void navigatorDrawText(uint32_t x, uint32_t y, const char* text, uint32_t color,
                              const gxos::web::WebStyle& style)
{
    if (!text || !text[0]) return;
    if (navigatorUsesMonospace(style)) {
        appDrawText(x, y, text, color);
        return;
    }
    if (framebuffer::is_available() && framebuffer::get_bpp() == 32) {
        uint32_t* target = framebuffer::get_draw_buffer();
        if (target) {
            const int pitch = framebuffer::is_double_buffered()
                ? static_cast<int>(framebuffer::get_width() * sizeof(uint32_t))
                : static_cast<int>(framebuffer::get_pitch());
            gxos::gui::SystemFont::DrawTextToBufferScaled(
                target, pitch, static_cast<int>(framebuffer::get_width()),
                static_cast<int>(framebuffer::get_height()), static_cast<int>(x),
                static_cast<int>(y), text, -1, color, navigatorFontFace(style),
                navigatorFontScale(style));
            return;
        }
    }
    appDrawText(x, y, text, color);
}

static void appDrawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (w == 0 || h == 0) return;
    framebuffer::fill_rect(x, y, w, 1, color);
    if (h > 1) framebuffer::fill_rect(x, y + h - 1, w, 1, color);
    if (h > 2) {
        framebuffer::fill_rect(x, y + 1, 1, h - 2, color);
        if (w > 1) framebuffer::fill_rect(x + w - 1, y + 1, 1, h - 2, color);
    }
}

static void serial_put_dec(uint32_t value) {
    char buffer[16];
    int index = 0;
    if (value == 0) {
        serial::putc('0');
        return;
    }
    while (value > 0 && index < (int)(sizeof(buffer) - 1)) {
        buffer[index++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (index > 0) {
        serial::putc(buffer[--index]);
    }
}

namespace {
struct KernelWallpaperEntry {
    const char* id;
    const char* displayName;
    uint32_t previewColorA;
    uint32_t previewColorB;
};

static const KernelWallpaperEntry s_kernelWallpapers[] = {
    {"legacy_blue_flower", "Blue Flower", 0xFF123070, 0xFF1B4FA8},
    {"legacy_dinos", "Dinos", 0xFF6B8D3B, 0xFFB8A05E},
    {"legacy_flower", "Flower", 0xFF375A78, 0xFF60B8C8},
    {"legacy_guidexos_space", "guideXOS Space", 0xFF071433, 0xFF2A5AA8},
    {"legacy_guidexos_space2", "guideXOS Space 2", 0xFF071433, 0xFF2A5AA8},
    {"legacy_red_flower", "Red Flower", 0xFF390808, 0xFFA82020},
    {"legacy_ameoba", "Ameoba", 0xFF102060, 0xFF7020B0},
    {"legacy_ameobagx", "Ameoba GX", 0xFF180830, 0xFF8A36B8},
    {"legacy_tron_porsche", "Tron Porsche", 0xFF052A35, 0xFF18B8C8},
    {"legacy_wallpaper2", "Wallpaper 2", 0xFF1A1640, 0xFFC02080},
    {"legacy_merlin", "Merlin", 0xFF243A57, 0xFF101826},
    {"legacy_merlin2", "Merlin 2", 0xFF2A3858, 0xFF0F1422},
    {"legacy_green_meadow", "Green Meadow", 0xFF235A26, 0xFF102210},
    {"legacy_cpu", "CPU", 0xFF263140, 0xFF0D1117},
    {"legacy_mountains", "Mountains", 0xFF1E3850, 0xFF0A1118},
};

static const int kKernelWallpaperCount = sizeof(s_kernelWallpapers) / sizeof(s_kernelWallpapers[0]);

struct KernelGradientEntry {
    const char* id;
    const char* displayName;
    uint32_t topColor;
    uint32_t bottomColor;
    uint32_t accentColor;
};

static const KernelGradientEntry s_kernelGradients[] = {
    {"gradient_midnight", "Midnight", 0xFF142850, 0xFF0F121C, 0xFF192337},
    {"gradient_ocean", "Ocean", 0xFF063B5C, 0xFF061522, 0xFF1496B8},
    {"gradient_aurora", "Aurora", 0xFF0B2C35, 0xFF251046, 0xFF21C78A},
    {"gradient_violet", "Violet", 0xFF26104A, 0xFF0D0B18, 0xFF8A52E8},
    {"gradient_sunset", "Sunset", 0xFF5E1B45, 0xFF17101E, 0xFFE06A55},
    {"gradient_forest", "Forest", 0xFF123B2B, 0xFF071711, 0xFF5E9C50},
    {"gradient_ember", "Ember", 0xFF45170F, 0xFF120B09, 0xFFD46A33},
    {"gradient_graphite", "Graphite", 0xFF333946, 0xFF111318, 0xFF7E8796},
};
static const int kKernelGradientCount = sizeof(s_kernelGradients) / sizeof(s_kernelGradients[0]);

static const int kTileW = 92;
static const int kTileH = 76;
static const int kTileGap = 12;
static const int kGalleryX = 18;
static const int kGalleryY = 82;
static const int kGalleryScrollBarW = 8;
static const int kGalleryRightMargin = 26;
static const int kGalleryFooterReserve = 84;
static const int kGalleryMinHeight = 120;
static const int kGalleryScrollbarGap = 6;
static const int kMinScrollbarThumbH = 18;
static const int kDesktopIconCheckboxX = 34;
static const int kDesktopIconCheckboxY = 104;
static const int kDesktopIconCheckboxRowH = 34;
static const int kDesktopIconCheckboxSize = 14;

static int maxInt(int a, int b) { return a > b ? a : b; }
static int minInt(int a, int b) { return a < b ? a : b; }

struct GalleryLayout
{
    int itemCount{0};
    int rowCount{0};
    int columns{1};
    int visibleRows{1};
    int maxScroll{0};
    int galleryX{kGalleryX};
    int galleryY{kGalleryY};
    int galleryW{1};
    int galleryH{1};
    int scrollbarX{0};
    bool showScrollbar{false};
    int buttonY{0};
};

static int rowCountForItemCount(int itemCount, int columns)
{
    if (itemCount <= 0 || columns <= 0) return 0;
    return (itemCount + columns - 1) / columns;
}

static int visibleRowsForHeight(int galleryH)
{
    const int rowPitch = kTileH + kTileGap;
    const int visible = (galleryH - kTileH) / rowPitch + 1;
    return visible > 0 ? visible : 1;
}

static int galleryColumnsForWidth(int galleryW)
{
    const int stride = kTileW + kTileGap;
    if (galleryW <= kTileW) return 1;
    int columns = (galleryW + kTileGap) / stride;
    if (columns < 1) columns = 1;
    while (columns > 1) {
        const int contentW = columns * kTileW + (columns - 1) * kTileGap;
        if (contentW <= galleryW) break;
        --columns;
    }
    return columns;
}

static GalleryLayout makeGalleryLayout(int windowW, int windowH, int itemCount)
{
    GalleryLayout layout;
    layout.itemCount = itemCount < 0 ? 0 : itemCount;
    layout.galleryX = kGalleryX;
    layout.galleryY = kGalleryY;
    layout.galleryW = windowW - kGalleryX - kGalleryRightMargin;
    if (layout.galleryW < 1) layout.galleryW = 1;
    layout.galleryH = windowH - kGalleryY - kGalleryFooterReserve;
    if (layout.galleryH < kGalleryMinHeight) layout.galleryH = kGalleryMinHeight;
    layout.visibleRows = visibleRowsForHeight(layout.galleryH);
    layout.columns = galleryColumnsForWidth(layout.galleryW);
    layout.rowCount = rowCountForItemCount(layout.itemCount, layout.columns);
    layout.showScrollbar = layout.rowCount > layout.visibleRows;
    if (layout.showScrollbar) {
        layout.galleryW -= (kGalleryScrollBarW + kGalleryScrollbarGap);
        if (layout.galleryW < 1) layout.galleryW = 1;
        layout.columns = galleryColumnsForWidth(layout.galleryW);
        layout.rowCount = rowCountForItemCount(layout.itemCount, layout.columns);
        layout.showScrollbar = layout.rowCount > layout.visibleRows;
    }
    layout.maxScroll = layout.rowCount > layout.visibleRows ? layout.rowCount - layout.visibleRows : 0;
    layout.scrollbarX = windowW - kGalleryRightMargin - kGalleryScrollBarW;
    layout.buttonY = layout.galleryY + layout.galleryH + 12;
    return layout;
}
}

// ============================================================
// NotepadApp Implementation
// ============================================================

// Static clipboard for cut/copy/paste
char NotepadApp::s_clipboard[MAX_TEXT_LENGTH] = {0};
int NotepadApp::s_clipboardLength = 0;

NotepadApp::NotepadApp() : m_textLength(0), m_cursorPos(0), m_scrollY(0), m_selectAll(false),
                           m_modified(false), m_ctrlPressed(false), m_showFileMenu(false),
                           m_showEditMenu(false), m_showContextMenu(false), m_contextMenuX(0),
                            m_contextMenuY(0), m_hoveredMenuItem(-1), m_hoveredMenuType(0),
                            m_selectionStart(-1), m_selectionEnd(-1) {
    strcopy(m_name, "Notepad", app::MAX_APP_NAME);
    m_text[0] = '\0';
    m_filePath[0] = '\0';
    m_showSaveDialog = false;
    m_saveDialogIsOpenMode = false;
    m_saveDialogShowingDrives = true;
    m_saveDialogFilenameFocused = true;
    m_saveDialogPath[0] = '\0';
    strcopy(m_saveDialogFilename, "untitled.txt", MAX_SAVE_FILENAME);
    m_saveDialogStatus[0] = '\0';
    m_saveEntryCount = 0;
    m_saveSelected = 0;
    m_saveScroll = 0;
}

NotepadApp::~NotepadApp() {
}

bool NotepadApp::init() {
    return initWithParam(nullptr);
}

bool NotepadApp::initWithParam(const char* filePath) {
    // Create window
    m_window = new app::KernelWindow();
    strcopy(m_window->title, "Notepad - Untitled", app::MAX_TITLE_LEN);
    m_window->x = 100;
    m_window->y = 50;
    m_window->w = 600;
    m_window->h = 400;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_RESIZABLE | app::WF_FOCUSED;
    m_window->owner = this;
    
    // Register with compositor
    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }
    
    // Load file if specified, otherwise show welcome
    if (filePath && filePath[0] != '\0') {
        if (!loadFile(filePath)) {
            // File load failed, start with empty document
            newFile();
        }
    } else {
        // Initialize with welcome message
        const char* welcome = "Welcome to guideXOS Notepad!\n\nFile/Edit menus available.\nRight-click for context menu.\nCtrl+S to save, Ctrl+O to open.\n\nType here...";
        strcopy(m_text, welcome, MAX_TEXT_LENGTH);
        m_textLength = strlen_local(m_text);
        m_cursorPos = m_textLength;
    }
    
    m_state = app::AppState::Running;
    return true;
}

void NotepadApp::shutdown() {
    m_state = app::AppState::Terminated;
}

void NotepadApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    // Draw menu bar background
    framebuffer::fill_rect(x, y, w, MENU_BAR_HEIGHT, rgb(50, 50, 60));
    drawMenuBar(x, y, w);
    
    // Text editor background
    uint32_t textAreaY = y + MENU_BAR_HEIGHT;
    uint32_t textAreaH = h - MENU_BAR_HEIGHT;
    framebuffer::fill_rect(x + 4, textAreaY + 4, w - 8, textAreaH - 8, rgb(45, 45, 55));
    
    // Select-all highlight
    if (m_selectAll && m_textLength > 0) {
        framebuffer::fill_rect(x + 4, textAreaY + 4, w - 8, textAreaH - 8, rgb(42, 91, 154));
    }
    
    // Draw text
    uint32_t textX = x + 8;
    uint32_t textY = textAreaY + 8;
    uint32_t lineH = kGlyphH + 3;
    uint32_t maxY = y + h - 8;
    
    int line = 0;
    int col = 0;
    int charIdx = 0;
    
    while (charIdx <= m_textLength && textY + kGlyphH < maxY) {
        char c = (charIdx < m_textLength) ? m_text[charIdx] : '\0';
        
        // Draw cursor
        if (charIdx == m_cursorPos) {
            framebuffer::fill_rect(textX + col * (kGlyphW + kGlyphSpacing), textY,
                                   2, kGlyphH + 2, rgb(200, 200, 220));
        }
        
        if (c == '\n' || c == '\0') {
            // New line
            line++;
            col = 0;
            textY += lineH;
        } else if (c >= 32 && c < 127) {
            // Printable character
            uint32_t cx = textX + col * (kGlyphW + kGlyphSpacing);
            
            if (c != ' ') {
                drawChar(cx, textY, c, rgb(220, 220, 235));
            }
            col++;
            
            // Word wrap
            if (col * (kGlyphW + kGlyphSpacing) + kGlyphW > w - 16) {
                line++;
                col = 0;
                textY += lineH;
            }
        }
        
        charIdx++;
    }
    
    // Draw menus on top
    if (m_showFileMenu) drawFileMenu(x + 4, y + MENU_BAR_HEIGHT);
    if (m_showEditMenu) drawEditMenu(x + 50, y + MENU_BAR_HEIGHT);
    if (m_showContextMenu) drawContextMenu(x + m_contextMenuX, y + m_contextMenuY);
    if (m_showSaveDialog) drawSaveAsDialog(x, y, w, h);
}

void NotepadApp::onKeyChar(char c) {
    if (m_showSaveDialog) {
        handleSaveDialogChar(c);
        invalidate();
        return;
    }

    if (c >= 32 && c < 127) {
        if (m_selectAll) {
            clearText();
            m_selectAll = false;
        }
        insertChar(c);
        m_modified = true;
        invalidate();
    }
}

void NotepadApp::onKeyDown(uint32_t key) {
    if (m_showSaveDialog) {
        handleSaveDialogKey(key);
        invalidate();
        return;
    }

    bool ctrl = ps2keyboard::is_ctrl_down();
    m_ctrlPressed = ctrl;
    
    // Ctrl shortcuts
    if (ctrl) {
        if (key == 'a' || key == 'A') {
            selectAll();
            invalidate();
            return;
        }
        if (key == 'c' || key == 'C') {
            copy();
            return;
        }
        if (key == 'x' || key == 'X') {
            cut();
            invalidate();
            return;
        }
        if (key == 'v' || key == 'V') {
            paste();
            invalidate();
            return;
        }
        if (key == 's' || key == 'S') {
            saveFile();
            invalidate();
            return;
        }
        if (key == 'o' || key == 'O') {
            openOpenFileDialog();
            invalidate();
            return;
        }
        if (key == 'n' || key == 'N') {
            newFile();
            invalidate();
            return;
        }
    }
    
    switch (key) {
        case '\n':  // 10
        case '\r':  // 13
            if (m_selectAll) { clearText(); m_selectAll = false; }
            insertChar('\n');
            m_modified = true;
            break;
        case '\b':  // 8 (Backspace)
            if (m_selectAll) {
                clearText();
                m_selectAll = false;
            } else {
                deleteChar();
            }
            m_modified = true;
            break;
        case '\t':  // 9 (Tab)
            if (m_selectAll) { clearText(); m_selectAll = false; }
            insertChar(' '); insertChar(' '); insertChar(' '); insertChar(' ');
            m_modified = true;
            break;
        case 127:  // Delete (ASCII DEL)
        case 0x106:  // KEY_DELETE
            if (m_selectAll) {
                clearText();
                m_selectAll = false;
            } else {
                // Forward delete: remove char at cursor
                if (m_cursorPos < m_textLength) {
                    for (int i = m_cursorPos; i < m_textLength; i++) {
                        m_text[i] = m_text[i + 1];
                    }
                    m_textLength--;
                }
            }
            m_modified = true;
            break;
        case shell::KEY_LEFT:
            m_selectAll = false;
            moveCursor(-1);
            break;
        case shell::KEY_RIGHT:
            m_selectAll = false;
            moveCursor(1);
            break;
        case shell::KEY_HOME:
            m_selectAll = false;
            m_cursorPos = 0;
            break;
        case shell::KEY_END:
            m_selectAll = false;
            m_cursorPos = m_textLength;
            break;
        default:
            break;
    }
    invalidate();
}

void NotepadApp::onMouseMove(int x, int y) {
    if (updateMenuHover(x, y)) {
        invalidate();
    }
}

void NotepadApp::onMouseDown(int x, int y, uint8_t button) {
    // Debug: log all mouse clicks
    serial::puts("[NOTEPAD] Mouse down: button=");
    serial::put_hex8(button);
    serial::puts(" x=");
    serial::put_hex32(x);
    serial::puts(" y=");
    serial::put_hex32(y);
    serial::putc('\n');
    
    // Left click
    if (button == 1) {
        if (m_showSaveDialog) {
            if (handleSaveDialogClick(x, y)) {
                m_hoveredMenuItem = -1;
                m_hoveredMenuType = 0;
                invalidate();
                return;
            }
        }

        // Click on menu bar
        if (y < MENU_BAR_HEIGHT) {
            if (handleMenuClick(x, y)) {
                invalidate();
                return;
            }
        }
        // Click on dropdown menu
        else if (m_showFileMenu || m_showEditMenu) {
            if (handleMenuClick(x, y)) {
                invalidate();
                return;
            }
        }
        
        // Click elsewhere - close all menus
        m_showFileMenu = false;
        m_showEditMenu = false;
        m_showContextMenu = false;
        m_hoveredMenuItem = -1;
        m_hoveredMenuType = 0;
        invalidate();
        return;
    }
    
    // Right click - show context menu in text area
    if (button == 2) {
        serial::puts("[NOTEPAD] Right-click detected! Showing context menu\n");
        
        // Close dropdown menus
        m_showFileMenu = false;
        m_showEditMenu = false;
        m_hoveredMenuItem = -1;
        m_hoveredMenuType = 0;
        
        // Show context menu at mouse position
        m_showContextMenu = true;
        m_contextMenuX = x;
        m_contextMenuY = y;
        invalidate();
        return;
    }
}

void NotepadApp::onMouseUp(int x, int y, uint8_t button) {
    // Handle context menu clicks
    if (m_showContextMenu && button == 1) {
        if (handleContextMenuClick(x, y)) {
            m_showContextMenu = false;
            invalidate();
        }
    }
}

void NotepadApp::insertChar(char c) {
    if (m_textLength >= MAX_TEXT_LENGTH - 1) return;
    
    // Shift text after cursor
    for (int i = m_textLength; i > m_cursorPos; i--) {
        m_text[i] = m_text[i - 1];
    }
    
    m_text[m_cursorPos] = c;
    m_cursorPos++;
    m_textLength++;
    m_text[m_textLength] = '\0';
}

void NotepadApp::deleteChar() {
    if (m_cursorPos > 0 && m_textLength > 0) {
        // Shift text before cursor
        for (int i = m_cursorPos - 1; i < m_textLength; i++) {
            m_text[i] = m_text[i + 1];
        }
        m_cursorPos--;
        m_textLength--;
    }
}

void NotepadApp::clearText() {
    m_text[0] = '\0';
    m_textLength = 0;
    m_cursorPos = 0;
}

void NotepadApp::moveCursor(int delta) {
    m_cursorPos += delta;
    if (m_cursorPos < 0) m_cursorPos = 0;
    if (m_cursorPos > m_textLength) m_cursorPos = m_textLength;
}

int NotepadApp::getLineCount() const {
    int count = 1;
    for (int i = 0; i < m_textLength; i++) {
        if (m_text[i] == '\n') count++;
    }
    return count;
}

int NotepadApp::getLineStart(int lineIndex) const {
    if (lineIndex == 0) return 0;
    
    int line = 0;
    for (int i = 0; i < m_textLength; i++) {
        if (m_text[i] == '\n') {
            line++;
            if (line == lineIndex) return i + 1;
        }
    }
    return m_textLength;
}

// File operations
bool NotepadApp::loadFile(const char* path) {
    if (!path || path[0] == '\0') return false;
    
    uint8_t handle = vfs::open(path, vfs::OPEN_READ);
    if (handle == 0xFF) return false;
    
    int32_t bytesRead = vfs::read(handle, m_text, MAX_TEXT_LENGTH - 1);
    vfs::close(handle);
    
    if (bytesRead < 0) return false;
    
    m_text[bytesRead] = '\0';
    m_textLength = bytesRead;
    m_cursorPos = 0;
    m_modified = false;
    strcopy(m_filePath, path, MAX_PATH_LEN);
    updateTitle();
    return true;
}

bool NotepadApp::saveFile() {
    if (m_filePath[0] == '\0') {
        openSaveAsDialog();
        return false;
    }
    return saveFileAs(m_filePath);
}

bool NotepadApp::saveFileAs(const char* path) {
    if (!path || path[0] == '\0') return false;

    int32_t bytesWritten = vfs::write_file(path, m_text, static_cast<uint32_t>(m_textLength));
    if (bytesWritten != m_textLength) return false;

    m_modified = false;
    strcopy(m_filePath, path, MAX_PATH_LEN);
    updateTitle();
    return true;
}

void NotepadApp::newFile() {
    m_text[0] = '\0';
    m_textLength = 0;
    m_cursorPos = 0;
    m_modified = false;
    m_filePath[0] = '\0';
    updateTitle();
}

void NotepadApp::openSaveAsDialog() {
    m_showFileMenu = false;
    m_showEditMenu = false;
    m_showContextMenu = false;
    m_showSaveDialog = true;
    m_saveDialogIsOpenMode = false;
    m_saveDialogShowingDrives = true;
    m_saveDialogFilenameFocused = true;
    m_saveDialogPath[0] = '\0';
    m_saveSelected = 0;
    m_saveScroll = 0;
    if (m_filePath[0] != '\0') {
        const char* base = vfs::basename(m_filePath);
        if (base && base[0] != '\0') strcopy(m_saveDialogFilename, base, MAX_SAVE_FILENAME);
    } else {
        strcopy(m_saveDialogFilename, "untitled.txt", MAX_SAVE_FILENAME);
    }
    strcopy(m_saveDialogStatus, "Pick a drive or folder, then Save.", sizeof(m_saveDialogStatus));
    refreshSaveDialog();
    invalidate();
}

void NotepadApp::openOpenFileDialog() {
    m_showFileMenu = false;
    m_showEditMenu = false;
    m_showContextMenu = false;
    m_showSaveDialog = true;
    m_saveDialogIsOpenMode = true;
    m_saveDialogShowingDrives = true;
    m_saveDialogFilenameFocused = true;
    m_saveDialogPath[0] = '\0';
    m_saveSelected = 0;
    m_saveScroll = 0;
    strcopy(m_saveDialogFilename, "", MAX_SAVE_FILENAME);
    strcopy(m_saveDialogStatus, "Pick a drive, folder, or file to open.", sizeof(m_saveDialogStatus));
    refreshSaveDialog();
    invalidate();
}

void NotepadApp::refreshSaveDialog() {
    m_saveEntryCount = 0;
    if (m_saveDialogShowingDrives) {
        uint8_t count = vfs::mount_count();
        for (uint8_t i = 0; i < count && m_saveEntryCount < MAX_SAVE_ENTRIES; ++i) {
            const vfs::MountPoint* mount = vfs::get_mount_by_index(i);
            if (!mount || !mount->active) continue;
            SaveDialogEntry& entry = m_saveEntries[m_saveEntryCount++];
            strcopy(entry.name, mount->path, vfs::VFS_MAX_FILENAME);
            entry.isDir = true;
            entry.isDrive = true;
            entry.isFile = false;
        }
    } else {
        uint8_t dir = vfs::opendir(m_saveDialogPath);
        if (dir != 0xFF) {
            vfs::DirEntry de{};
            while (vfs::readdir(dir, &de) && m_saveEntryCount < MAX_SAVE_ENTRIES) {
                // Always show directories
                if (de.type == vfs::FILE_TYPE_DIRECTORY) {
                    SaveDialogEntry& entry = m_saveEntries[m_saveEntryCount++];
                    strcopy(entry.name, de.name, vfs::VFS_MAX_FILENAME);
                    entry.isDir = true;
                    entry.isDrive = false;
                    entry.isFile = false;
                }
                // Show files only in Open mode
                else if (m_saveDialogIsOpenMode && de.type == vfs::FILE_TYPE_REGULAR) {
                    SaveDialogEntry& entry = m_saveEntries[m_saveEntryCount++];
                    strcopy(entry.name, de.name, vfs::VFS_MAX_FILENAME);
                    entry.isDir = false;
                    entry.isDrive = false;
                    entry.isFile = true;
                }
            }
            vfs::closedir(dir);
        }
    }
    if (m_saveSelected >= m_saveEntryCount) m_saveSelected = m_saveEntryCount - 1;
    if (m_saveSelected < 0) m_saveSelected = 0;
}

void NotepadApp::drawSaveAsDialog(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t dlgW = w > 440 ? 420 : w - 20;
    uint32_t dlgH = h > 300 ? 280 : h - 20;
    uint32_t dlgX = x + (w - dlgW) / 2;
    uint32_t dlgY = y + (h - dlgH) / 2;

    framebuffer::fill_rect(dlgX, dlgY, dlgW, dlgH, rgb(35, 35, 45));
    framebuffer::fill_rect(dlgX, dlgY, dlgW, 1, rgb(150, 150, 170));
    framebuffer::fill_rect(dlgX, dlgY + dlgH - 1, dlgW, 1, rgb(150, 150, 170));
    framebuffer::fill_rect(dlgX, dlgY, 1, dlgH, rgb(150, 150, 170));
    framebuffer::fill_rect(dlgX + dlgW - 1, dlgY, 1, dlgH, rgb(150, 150, 170));

    const char* title = m_saveDialogIsOpenMode ? "Open File" : "Save As";
    appDrawText(dlgX + 12, dlgY + 10, title, rgb(255, 255, 255));

    const char* locationLabel = m_saveDialogShowingDrives ? 
        (m_saveDialogIsOpenMode ? "Open from: Computer" : "Save in: Computer") : 
        (m_saveDialogIsOpenMode ? "Open from:" : "Save in:");
    appDrawText(dlgX + 12, dlgY + 32, locationLabel, rgb(220, 220, 230));
    if (!m_saveDialogShowingDrives) appDrawText(dlgX + 80, dlgY + 32, m_saveDialogPath, rgb(200, 220, 255));

    framebuffer::fill_rect(dlgX + 12, dlgY + 54, dlgW - 24, 130, rgb(25, 25, 32));
    const int rowH = 16;
    int rows = 8;
    for (int i = 0; i < rows; ++i) {
        int index = m_saveScroll + i;
        if (index >= m_saveEntryCount) break;
        uint32_t rowY = dlgY + 58 + i * rowH;
        if (index == m_saveSelected) framebuffer::fill_rect(dlgX + 14, rowY - 2, dlgW - 28, rowH, rgb(50, 90, 150));

        const char* typeLabel = m_saveEntries[index].isDrive ? "[DRIVE]" : 
                                 m_saveEntries[index].isDir ? "[DIR]" : "[FILE]";
        appDrawText(dlgX + 18, rowY, typeLabel, rgb(210, 210, 120));
        appDrawText(dlgX + 70, rowY, m_saveEntries[index].name, rgb(235, 235, 240));
    }
    if (m_saveEntryCount == 0) {
        const char* emptyMsg = m_saveDialogIsOpenMode ? "No drives, folders, or files found." : "No drives or folders found.";
        appDrawText(dlgX + 18, dlgY + 64, emptyMsg, rgb(240, 180, 120));
    }

    appDrawText(dlgX + 12, dlgY + 196, "File name:", rgb(220, 220, 230));
    framebuffer::fill_rect(dlgX + 86, dlgY + 190, dlgW - 110, 22, m_saveDialogFilenameFocused ? rgb(18, 28, 48) : rgb(20, 20, 28));
    if (m_saveDialogFilenameFocused) {
        framebuffer::fill_rect(dlgX + 86, dlgY + 190, dlgW - 110, 1, rgb(90, 140, 220));
        framebuffer::fill_rect(dlgX + 86, dlgY + 211, dlgW - 110, 1, rgb(90, 140, 220));
        framebuffer::fill_rect(dlgX + 86, dlgY + 190, 1, 22, rgb(90, 140, 220));
        framebuffer::fill_rect(dlgX + dlgW - 25, dlgY + 190, 1, 22, rgb(90, 140, 220));
    }
    appDrawText(dlgX + 92, dlgY + 197, m_saveDialogFilename, rgb(255, 255, 255));
    if (m_saveDialogFilenameFocused) {
        int len = strlen_local(m_saveDialogFilename);
        int caretX = dlgX + 92 + len * (kGlyphW + kGlyphSpacing);
        uint32_t rightLimit = dlgX + dlgW - 28;
        if ((uint32_t)caretX > rightLimit) caretX = rightLimit;
        framebuffer::fill_rect(caretX, dlgY + 196, 1, kGlyphH + 3, rgb(255, 255, 255));
    }

    framebuffer::fill_rect(dlgX + 12, dlgY + 226, 70, 24, rgb(65, 75, 95));
    appDrawText(dlgX + 28, dlgY + 234, "Drives", rgb(255, 255, 255));
    framebuffer::fill_rect(dlgX + 90, dlgY + 226, 55, 24, rgb(65, 75, 95));
    appDrawText(dlgX + 110, dlgY + 234, "Up", rgb(255, 255, 255));

    const char* actionButtonText = m_saveDialogIsOpenMode ? "Open" : "Save";
    framebuffer::fill_rect(dlgX + dlgW - 170, dlgY + 226, 70, 24, rgb(50, 110, 70));
    appDrawText(dlgX + dlgW - 147, dlgY + 234, actionButtonText, rgb(255, 255, 255));
    framebuffer::fill_rect(dlgX + dlgW - 90, dlgY + 226, 70, 24, rgb(110, 65, 65));
    appDrawText(dlgX + dlgW - 72, dlgY + 234, "Cancel", rgb(255, 255, 255));

    appDrawText(dlgX + 12, dlgY + 260, m_saveDialogStatus, rgb(210, 210, 210));
}

void NotepadApp::navigateSaveDialog(const char* path) {
    if (!path || path[0] == '\0') return;
    strcopy(m_saveDialogPath, path, MAX_PATH_LEN);
    m_saveDialogShowingDrives = false;
    m_saveSelected = 0;
    m_saveScroll = 0;
    refreshSaveDialog();
}

void NotepadApp::saveDialogGoUp() {
    if (m_saveDialogShowingDrives || m_saveDialogPath[0] == '\0' || (m_saveDialogPath[0] == '/' && m_saveDialogPath[1] == '\0')) {
        m_saveDialogShowingDrives = true;
        m_saveDialogPath[0] = '\0';
        refreshSaveDialog();
        return;
    }
    char parent[MAX_PATH_LEN];
    vfs::parent_path(m_saveDialogPath, parent, sizeof(parent));
    navigateSaveDialog(parent);
}

void NotepadApp::buildSavePath(char* out, int outSize) const {
    if (!out || outSize <= 0) return;
    int pos = 0;
    const char* path = m_saveDialogPath;
    while (*path && pos < outSize - 1) out[pos++] = *path++;
    if (pos > 0 && out[pos - 1] != '/' && pos < outSize - 1) out[pos++] = '/';
    const char* name = m_saveDialogFilename;
    bool hasDot = false;
    while (*name && pos < outSize - 1) {
        if (*name == '.') hasDot = true;
        out[pos++] = *name++;
    }
    if (!hasDot) {
        const char* ext = ".txt";
        while (*ext && pos < outSize - 1) out[pos++] = *ext++;
    }
    out[pos] = '\0';
}

bool NotepadApp::saveToDialogTarget() {
    if (m_saveDialogShowingDrives || m_saveDialogPath[0] == '\0') {
        strcopy(m_saveDialogStatus, "Select a drive or folder first.", sizeof(m_saveDialogStatus));
        return false;
    }
    char fullPath[MAX_PATH_LEN];
    buildSavePath(fullPath, sizeof(fullPath));
    if (!saveFileAs(fullPath)) {
        strcopy(m_saveDialogStatus, "Save failed. Use an 8.3 name like NOTE.TXT.", sizeof(m_saveDialogStatus));
        return false;
    }
    m_showSaveDialog = false;
    return true;
}

void NotepadApp::handleSaveDialogChar(char c) {
    if (!m_saveDialogFilenameFocused) return;
    if (c < 32 || c >= 127) return;

    int len = strlen_local(m_saveDialogFilename);
    if (len >= MAX_SAVE_FILENAME - 1) return;

    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
        strcopy(m_saveDialogStatus, "Filename cannot contain / \\ : * ? \" < > |", sizeof(m_saveDialogStatus));
        return;
    }

    m_saveDialogFilename[len] = c;
    m_saveDialogFilename[len + 1] = '\0';
    strcopy(m_saveDialogStatus, "Type a filename, pick a folder, then Save.", sizeof(m_saveDialogStatus));
}

void NotepadApp::handleSaveDialogKey(uint32_t key) {
    if (key == 27) {
        m_showSaveDialog = false;
        return;
    }

    if (key == '\t' || key == shell::KEY_TAB) {
        m_saveDialogFilenameFocused = !m_saveDialogFilenameFocused;
        return;
    }

    if (m_saveDialogFilenameFocused) {
        if (key == '\b') {
            int len = strlen_local(m_saveDialogFilename);
            if (len > 0) m_saveDialogFilename[len - 1] = '\0';
            return;
        }
        if (key == '\n' || key == '\r') {
            saveToDialogTarget();
            return;
        }
        if (key == shell::KEY_UP || key == shell::KEY_DOWN) {
            m_saveDialogFilenameFocused = false;
            return;
        }
        return;
    }

    switch (key) {
        case shell::KEY_UP:
            if (m_saveSelected > 0) m_saveSelected--;
            break;
        case shell::KEY_DOWN:
            if (m_saveSelected < m_saveEntryCount - 1) m_saveSelected++;
            break;
        case '\b':
            saveDialogGoUp();
            break;
        case '\n':
        case '\r':
            if (m_saveSelected >= 0 && m_saveSelected < m_saveEntryCount) {
                if (m_saveEntries[m_saveSelected].isDrive) {
                    navigateSaveDialog(m_saveEntries[m_saveSelected].name);
                } else if (m_saveEntries[m_saveSelected].isDir) {
                    char child[MAX_PATH_LEN];
                    vfs::join_path(m_saveDialogPath, m_saveEntries[m_saveSelected].name, child, sizeof(child));
                    navigateSaveDialog(child);
                } else if (m_saveDialogIsOpenMode && m_saveEntries[m_saveSelected].isFile) {
                    // Open the selected file
                    char fullPath[MAX_PATH_LEN];
                    vfs::join_path(m_saveDialogPath, m_saveEntries[m_saveSelected].name, fullPath, sizeof(fullPath));
                    if (loadFile(fullPath)) {
                        m_showSaveDialog = false;
                    }
                }
            }
            break;
        default:
            break;
    }
}

bool NotepadApp::handleSaveDialogClick(int x, int y) {
    if (!m_window) return false;
    int w = m_window->w;
    int h = m_window->h - 24;
    int dlgW = w > 440 ? 420 : w - 20;
    int dlgH = h > 300 ? 280 : h - 20;
    int dlgX = (w - dlgW) / 2;
    int dlgY = (h - dlgH) / 2;

    if (x < dlgX || x >= dlgX + dlgW || y < dlgY || y >= dlgY + dlgH) return true;

    if (y >= dlgY + 58 && y < dlgY + 58 + 8 * 16 && x >= dlgX + 12 && x < dlgX + dlgW - 12) {
        m_saveDialogFilenameFocused = false;
        int row = (y - (dlgY + 58)) / 16;
        int index = m_saveScroll + row;
        if (index >= 0 && index < m_saveEntryCount) {
            m_saveSelected = index;
            if (m_saveEntries[index].isDrive) {
                navigateSaveDialog(m_saveEntries[index].name);
            } else if (m_saveEntries[index].isDir) {
                char child[MAX_PATH_LEN];
                vfs::join_path(m_saveDialogPath, m_saveEntries[index].name, child, sizeof(child));
                navigateSaveDialog(child);
            } else if (m_saveDialogIsOpenMode && m_saveEntries[index].isFile) {
                // In open mode, populate filename field with clicked file
                strcopy(m_saveDialogFilename, m_saveEntries[index].name, MAX_SAVE_FILENAME);
                strcopy(m_saveDialogStatus, "Click Open to open this file.", sizeof(m_saveDialogStatus));
            }
        }
        return true;
    }

    if (y >= dlgY + 190 && y < dlgY + 212 && x >= dlgX + 86 && x < dlgX + dlgW - 24) {
        m_saveDialogFilenameFocused = true;
        const char* statusMsg = m_saveDialogIsOpenMode ? 
            "Type a filename or select from list." : 
            "Type a filename, pick a folder, then Save.";
        strcopy(m_saveDialogStatus, statusMsg, sizeof(m_saveDialogStatus));
        return true;
    }

    if (y >= dlgY + 226 && y < dlgY + 250) {
        if (x >= dlgX + 12 && x < dlgX + 82) {
            m_saveDialogFilenameFocused = false;
            m_saveDialogShowingDrives = true;
            m_saveDialogPath[0] = '\0';
            refreshSaveDialog();
            return true;
        }
        if (x >= dlgX + 90 && x < dlgX + 145) {
            m_saveDialogFilenameFocused = false;
            saveDialogGoUp();
            return true;
        }
        if (x >= dlgX + dlgW - 170 && x < dlgX + dlgW - 100) {
            if (m_saveDialogIsOpenMode) {
                // Open mode: load the file
                if (m_saveDialogFilename[0] != '\0' && m_saveDialogPath[0] != '\0') {
                    char fullPath[MAX_PATH_LEN];
                    vfs::join_path(m_saveDialogPath, m_saveDialogFilename, fullPath, sizeof(fullPath));
                    if (loadFile(fullPath)) {
                        m_showSaveDialog = false;
                    } else {
                        strcopy(m_saveDialogStatus, "Failed to open file.", sizeof(m_saveDialogStatus));
                    }
                } else {
                    strcopy(m_saveDialogStatus, "Select a file or enter a filename.", sizeof(m_saveDialogStatus));
                }
            } else {
                // Save mode
                saveToDialogTarget();
            }
            return true;
        }
        if (x >= dlgX + dlgW - 90 && x < dlgX + dlgW - 20) {
            m_showSaveDialog = false;
            return true;
        }
    }

    return true;
}

void NotepadApp::updateTitle() {
    char title[app::MAX_TITLE_LEN];
    const char* filename = m_filePath[0] != '\0' ? m_filePath : "Untitled";
    
    // Build title: "filename - Notepad" or "*filename - Notepad" if modified
    int pos = 0;
    if (m_modified && pos < app::MAX_TITLE_LEN - 1) {
        title[pos++] = '*';
    }
    
    int i = 0;
    while (filename[i] && pos < app::MAX_TITLE_LEN - 12) {
        title[pos++] = filename[i++];
    }
    
    const char* suffix = " - Notepad";
    i = 0;
    while (suffix[i] && pos < app::MAX_TITLE_LEN - 1) {
        title[pos++] = suffix[i++];
    }
    title[pos] = '\0';
    
    strcopy(m_window->title, title, app::MAX_TITLE_LEN);
}

// Text editing operations
void NotepadApp::backspace() {
    deleteChar();
}

void NotepadApp::selectAll() {
    m_selectAll = true;
}

void NotepadApp::cut() {
    copy();
    if (m_selectAll && m_textLength > 0) {
        clearText();
        m_selectAll = false;
        m_modified = true;
    }
}

void NotepadApp::copy() {
    if (m_selectAll && m_textLength > 0) {
        int copyLen = m_textLength < MAX_TEXT_LENGTH - 1 ? m_textLength : MAX_TEXT_LENGTH - 1;
        for (int i = 0; i < copyLen; i++) {
            s_clipboard[i] = m_text[i];
        }
        s_clipboard[copyLen] = '\0';
        s_clipboardLength = copyLen;
    }
}

void NotepadApp::paste() {
    if (s_clipboardLength == 0) return;
    
    if (m_selectAll) {
        clearText();
        m_selectAll = false;
    }
    
    // Insert clipboard contents at cursor
    for (int i = 0; i < s_clipboardLength && m_textLength < MAX_TEXT_LENGTH - 1; i++) {
        insertChar(s_clipboard[i]);
    }
    m_modified = true;
}

// Menu and UI drawing
void NotepadApp::drawMenuBar(uint32_t x, uint32_t y, uint32_t w) {
    // Draw menu bar background
    framebuffer::fill_rect(x, y, w, MENU_BAR_HEIGHT, rgb(50, 50, 60));
    
    // Draw bottom separator line
    framebuffer::fill_rect(x, y + MENU_BAR_HEIGHT - 1, w, 1, rgb(70, 70, 80));
    
    // File menu item
    uint32_t fileX = x + 4;
    uint32_t fileW = 40;
    if (m_showFileMenu || (m_hoveredMenuType == 1 && m_hoveredMenuItem == -2)) {
        framebuffer::fill_rect(fileX, y + 2, fileW, MENU_BAR_HEIGHT - 4, rgb(70, 100, 150));
    }
    drawChar(fileX + 4, y + 6, 'F', rgb(220, 220, 230));
    drawChar(fileX + 10, y + 6, 'i', rgb(220, 220, 230));
    drawChar(fileX + 16, y + 6, 'l', rgb(220, 220, 230));
    drawChar(fileX + 22, y + 6, 'e', rgb(220, 220, 230));
    
    // Edit menu item
    uint32_t editX = fileX + fileW + 4;
    uint32_t editW = 40;
    if (m_showEditMenu || (m_hoveredMenuType == 2 && m_hoveredMenuItem == -2)) {
        framebuffer::fill_rect(editX, y + 2, editW, MENU_BAR_HEIGHT - 4, rgb(70, 100, 150));
    }
    drawChar(editX + 4, y + 6, 'E', rgb(220, 220, 230));
    drawChar(editX + 10, y + 6, 'd', rgb(220, 220, 230));
    drawChar(editX + 16, y + 6, 'i', rgb(220, 220, 230));
    drawChar(editX + 22, y + 6, 't', rgb(220, 220, 230));
}

void NotepadApp::drawFileMenu(uint32_t x, uint32_t y) {
    const char* items[] = {"New", "Open", "Save", "Save As", "Exit"};
    const int itemCount = 5;
    const int menuW = 120;
    const int itemH = 22;

    // Menu background
    framebuffer::fill_rect(x, y, menuW, itemCount * itemH + 2, rgb(240, 240, 245));

    // Border
    framebuffer::fill_rect(x, y, menuW, 1, rgb(160, 160, 170)); // Top
    framebuffer::fill_rect(x, y + itemCount * itemH + 1, menuW, 1, rgb(160, 160, 170)); // Bottom
    framebuffer::fill_rect(x, y, 1, itemCount * itemH + 2, rgb(160, 160, 170)); // Left
    framebuffer::fill_rect(x + menuW - 1, y, 1, itemCount * itemH + 2, rgb(160, 160, 170)); // Right

    for (int i = 0; i < itemCount; i++) {
        uint32_t itemY = y + 1 + i * itemH;

        if (m_hoveredMenuType == 1 && m_hoveredMenuItem == i) {
            framebuffer::fill_rect(x + 1, itemY, menuW - 2, itemH, rgb(45, 95, 180));
        }

        // Item text
        uint32_t textColor = (m_hoveredMenuType == 1 && m_hoveredMenuItem == i) ? rgb(255, 255, 255) : rgb(0, 0, 0);
        for (int j = 0; items[i][j]; j++) {
            drawChar(x + 8 + j * 6, itemY + 7, items[i][j], textColor);
        }
    }
}

void NotepadApp::drawEditMenu(uint32_t x, uint32_t y) {
    const char* items[] = {"Cut      Ctrl+X", "Copy     Ctrl+C", "Paste    Ctrl+V", "Select All  Ctrl+A"};
    const int itemCount = 4;
    const int menuW = 160;
    const int itemH = 22;
    
    // Menu background
    framebuffer::fill_rect(x, y, menuW, itemCount * itemH + 2, rgb(240, 240, 245));
    
    // Border
    framebuffer::fill_rect(x, y, menuW, 1, rgb(160, 160, 170));
    framebuffer::fill_rect(x, y + itemCount * itemH + 1, menuW, 1, rgb(160, 160, 170));
    framebuffer::fill_rect(x, y, 1, itemCount * itemH + 2, rgb(160, 160, 170));
    framebuffer::fill_rect(x + menuW - 1, y, 1, itemCount * itemH + 2, rgb(160, 160, 170));
    
    for (int i = 0; i < itemCount; i++) {
        uint32_t itemY = y + 1 + i * itemH;

        if (m_hoveredMenuType == 2 && m_hoveredMenuItem == i) {
            framebuffer::fill_rect(x + 1, itemY, menuW - 2, itemH, rgb(45, 95, 180));
        }
        
        // Item text
        uint32_t textColor = (m_hoveredMenuType == 2 && m_hoveredMenuItem == i) ? rgb(255, 255, 255) : rgb(0, 0, 0);
        for (int j = 0; items[i][j]; j++) {
            drawChar(x + 8 + j * 6, itemY + 7, items[i][j], textColor);
        }
    }
}

void NotepadApp::drawContextMenu(uint32_t x, uint32_t y) {
    const char* items[] = {"Cut", "Copy", "Paste", "Select All"};
    const int itemCount = 4;
    const int menuW = 130;
    const int itemH = 22;
    
    // Menu background
    framebuffer::fill_rect(x, y, menuW, itemCount * itemH + 2, rgb(240, 240, 245));
    
    // Border with shadow effect
    framebuffer::fill_rect(x, y, menuW, 1, rgb(160, 160, 170));
    framebuffer::fill_rect(x, y + itemCount * itemH + 1, menuW, 1, rgb(160, 160, 170));
    framebuffer::fill_rect(x, y, 1, itemCount * itemH + 2, rgb(160, 160, 170));
    framebuffer::fill_rect(x + menuW - 1, y, 1, itemCount * itemH + 2, rgb(160, 160, 170));
    
    // Shadow
    framebuffer::fill_rect(x + 2, y + itemCount * itemH + 2, menuW, 2, rgb(100, 100, 110));
    framebuffer::fill_rect(x + menuW, y + 2, 2, itemCount * itemH, rgb(100, 100, 110));
    
    for (int i = 0; i < itemCount; i++) {
        uint32_t itemY = y + 1 + i * itemH;

        if (m_hoveredMenuType == 3 && m_hoveredMenuItem == i) {
            framebuffer::fill_rect(x + 1, itemY, menuW - 2, itemH, rgb(45, 95, 180));
        }
        
        // Item text
        uint32_t textColor = (m_hoveredMenuType == 3 && m_hoveredMenuItem == i) ? rgb(255, 255, 255) : rgb(0, 0, 0);
        for (int j = 0; items[i][j]; j++) {
            drawChar(x + 8 + j * 6, itemY + 7, items[i][j], textColor);
        }
    }
}

bool NotepadApp::handleMenuClick(int x, int y) {
    const int fileX = 4;
    const int fileW = 40;
    const int editX = 48;
    const int editW = 40;
    
    // Click on menu bar
    if (y < MENU_BAR_HEIGHT) {
        // File menu toggle
        if (x >= fileX && x < fileX + fileW) {
            m_showFileMenu = !m_showFileMenu;
            m_showEditMenu = false;
            m_hoveredMenuItem = -1;
            m_hoveredMenuType = 0;
            return true;
        }
        // Edit menu toggle
        if (x >= editX && x < editX + editW) {
            m_showEditMenu = !m_showEditMenu;
            m_showFileMenu = false;
            m_hoveredMenuItem = -1;
            m_hoveredMenuType = 0;
            return true;
        }
    }
    
    // Handle File menu dropdown item clicks
    if (m_showFileMenu) {
        const int menuW = 120;
        const int itemH = 22;
        const int menuX = fileX;
        const int menuY = MENU_BAR_HEIGHT;

        if (x >= menuX && x < menuX + menuW && 
            y >= menuY && y < menuY + 5 * itemH + 2) {
            int item = (y - menuY - 1) / itemH;
            if (item >= 0 && item < 5) {
                m_showFileMenu = false;
                m_hoveredMenuItem = -1;
                m_hoveredMenuType = 0;
                switch (item) {
                    case 0: newFile(); break;
                    case 1: openOpenFileDialog(); break;
                    case 2: saveFile(); break;
                    case 3: openSaveAsDialog(); break;
                    case 4: requestClose(); break;
                }
                return true;
            }
        }
    }
    
    // Handle Edit menu dropdown item clicks
    if (m_showEditMenu) {
        const int menuW = 160;
        const int itemH = 22;
        const int menuX = editX;
        const int menuY = MENU_BAR_HEIGHT;
        
        if (x >= menuX && x < menuX + menuW && 
            y >= menuY && y < menuY + 4 * itemH + 2) {
            int item = (y - menuY - 1) / itemH;
            if (item >= 0 && item < 4) {
                m_showEditMenu = false;
                m_hoveredMenuItem = -1;
                m_hoveredMenuType = 0;
                switch (item) {
                    case 0: cut(); break;
                    case 1: copy(); break;
                    case 2: paste(); break;
                    case 3: selectAll(); break;
                }
                return true;
            }
        }
    }
    
    return false;
}

bool NotepadApp::handleContextMenuClick(int x, int y) {
    const int menuW = 130;
    const int itemH = 22;
    const int itemCount = 4;
    
    // Check if click is within context menu bounds
    if (x >= m_contextMenuX && x < m_contextMenuX + menuW &&
        y >= m_contextMenuY && y < m_contextMenuY + itemCount * itemH + 2) {
        
        int item = (y - m_contextMenuY - 1) / itemH;
        if (item >= 0 && item < itemCount) {
            switch (item) {
                case 0: cut(); break;
                case 1: copy(); break;
                case 2: paste(); break;
                case 3: selectAll(); break;
            }
            m_hoveredMenuItem = -1;
            m_hoveredMenuType = 0;
            return true;
        }
    }
    return false;
}

bool NotepadApp::updateMenuHover(int x, int y) {
    int newType = 0;
    int newItem = -1;

    const int fileX = 4;
    const int fileW = 40;
    const int editX = 48;
    const int editW = 40;

    if (y >= 0 && y < MENU_BAR_HEIGHT) {
        if (x >= fileX && x < fileX + fileW) {
            newType = 1;
            newItem = -2;
        } else if (x >= editX && x < editX + editW) {
            newType = 2;
            newItem = -2;
        }
    }

    if (m_showFileMenu) {
        const int menuX = fileX;
        const int menuY = MENU_BAR_HEIGHT;
        const int menuW = 120;
        const int itemH = 22;
        const int itemCount = 5;
        if (x >= menuX && x < menuX + menuW && y >= menuY + 1 && y < menuY + 1 + itemCount * itemH) {
            newType = 1;
            newItem = (y - menuY - 1) / itemH;
        }
    }

    if (m_showEditMenu) {
        const int menuX = editX;
        const int menuY = MENU_BAR_HEIGHT;
        const int menuW = 160;
        const int itemH = 22;
        const int itemCount = 4;
        if (x >= menuX && x < menuX + menuW && y >= menuY + 1 && y < menuY + 1 + itemCount * itemH) {
            newType = 2;
            newItem = (y - menuY - 1) / itemH;
        }
    }

    if (m_showContextMenu) {
        const int menuW = 130;
        const int itemH = 22;
        const int itemCount = 4;
        if (x >= m_contextMenuX && x < m_contextMenuX + menuW &&
            y >= m_contextMenuY + 1 && y < m_contextMenuY + 1 + itemCount * itemH) {
            newType = 3;
            newItem = (y - m_contextMenuY - 1) / itemH;
        }
    }

    if (newType != m_hoveredMenuType || newItem != m_hoveredMenuItem) {
        m_hoveredMenuType = newType;
        m_hoveredMenuItem = newItem;
        return true;
    }

    return false;
}

// ============================================================
// DisplayOptionsApp Implementation
// ============================================================

DisplayOptionsApp::DisplayOptionsApp()
    : m_selectedIndex(0), m_appliedIndex(0), m_selectedBackgroundIndex(0), m_appliedBackgroundIndex(0), m_selectedGradientIndex(0), m_appliedGradientIndex(0), m_activeTab(0), m_windowW(720), m_windowH(460), m_backgroundGalleryScrollOffset(0), m_gradientGalleryScrollOffset(0), m_galleryScrollbarDragging(false), m_galleryScrollbarDragStartY(0), m_galleryScrollbarDragStartOffset(0), m_selectButtonId(-1), m_desktopIconVisibility{true, true, true, false}, m_selectedDisplayMode(gxos::display::DisplayConfigurationMode::Extend), m_appliedDisplayMode(gxos::display::DisplayConfigurationMode::Extend), m_selectedPrimaryOutput(0), m_appliedPrimaryOutput(0), m_activeDisplayConfiguration{}, m_requestedDisplayConfiguration{}, m_pendingTopologyChange{}, m_activeConfigurationGeneration(1), m_displayLocalEdits(false), m_displayStatus{}, m_windowGeneration(0), m_displayRequestId(0), m_displayRequestPending(false) {
    strcopy(m_name, "DisplayOptions", app::MAX_APP_NAME);
    m_displayStatus[0] = '\0';
}

DisplayOptionsApp::~DisplayOptionsApp() {
}

void DisplayOptionsApp::loadSelection() {
    const char* currentId = kernel::desktop::get_wallpaper_id();
    m_selectedIndex = 0;
    m_appliedIndex = 0;
    m_selectedBackgroundIndex = 0;
    m_appliedBackgroundIndex = 0;
    m_selectedGradientIndex = 0;
    m_appliedGradientIndex = 0;
    m_backgroundGalleryScrollOffset = 0;
    m_gradientGalleryScrollOffset = 0;
    m_galleryScrollbarDragging = false;
    m_galleryScrollbarDragStartY = 0;
    m_galleryScrollbarDragStartOffset = 0;
    m_activeTab = 0;
    m_desktopIconVisibility = kernel::desktop::get_system_desktop_icon_visibility();
    serial::puts("[display-options] Desktop Icons checkbox state loaded\n");
    queryDisplayConfiguration();

    for (int i = 0; i < kKernelWallpaperCount; ++i) {
        if (streq_local(currentId, s_kernelWallpapers[i].id)) {
            m_selectedIndex = i;
            m_appliedIndex = i;
            m_selectedBackgroundIndex = i;
            m_appliedBackgroundIndex = i;
            return;
        }
    }

    for (int i = 0; i < kKernelGradientCount; ++i) {
        if (streq_local(currentId, s_kernelGradients[i].id)) {
            m_selectedGradientIndex = i;
            m_appliedGradientIndex = i;
            m_activeTab = 1;
            return;
        }
    }
}

bool DisplayOptionsApp::init() {
    ++m_windowGeneration;
    m_displayRequestPending = false;
    m_window = new app::KernelWindow();
    if (!m_window) return false;

    m_window->owner = this;
    m_window->x = 70;
    m_window->y = 50;
    m_window->w = 720;
    m_window->h = 460;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_FOCUSED;
    strcopy(m_window->title, "Display Options", app::MAX_TITLE_LEN);
    m_windowW = static_cast<int>(m_window->w);
    m_windowH = static_cast<int>(m_window->h);

    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }

    loadSelection();
    m_selectButtonId = addButton(18, 326, 142, 28, m_activeTab == 1 ? "Select Gradient" : "Select Background");
    setActiveTab(m_activeTab);
    m_state = app::AppState::Running;
    return true;
}

void DisplayOptionsApp::shutdown() {
    ++m_windowGeneration;
    m_displayRequestPending = false;
    m_state = app::AppState::Terminated;
}

void DisplayOptionsApp::onWindowClose() {
    ++m_windowGeneration;
    m_displayRequestPending = false;
}

void DisplayOptionsApp::setActiveTab(int tab)
{
    setActiveTabAndClamp(tab);
}

int& DisplayOptionsApp::activeGalleryScrollOffset() {
    return m_activeTab == 0 ? m_backgroundGalleryScrollOffset : m_gradientGalleryScrollOffset;
}

int DisplayOptionsApp::activeGalleryItemCount() const {
    return m_activeTab == 0
        ? static_cast<int>(kKernelWallpaperCount)
        : static_cast<int>(kKernelGradientCount);
}

int DisplayOptionsApp::activeSelectionIndex() const {
    return m_activeTab == 0 ? m_selectedBackgroundIndex : m_selectedGradientIndex;
}

void DisplayOptionsApp::syncActiveSelectionMirror() {
    if (m_activeTab == 0 || m_activeTab == 1) {
        m_selectedIndex = activeSelectionIndex();
    }
}

void DisplayOptionsApp::clampSelectionToCurrentTab() {
    if (m_activeTab == 0) {
        if (kKernelWallpaperCount <= 0) {
            m_selectedBackgroundIndex = 0;
            m_appliedBackgroundIndex = 0;
        } else {
            if (m_selectedBackgroundIndex < 0) m_selectedBackgroundIndex = 0;
            if (m_selectedBackgroundIndex >= kKernelWallpaperCount) m_selectedBackgroundIndex = kKernelWallpaperCount - 1;
            if (m_appliedBackgroundIndex < 0) m_appliedBackgroundIndex = 0;
            if (m_appliedBackgroundIndex >= kKernelWallpaperCount) m_appliedBackgroundIndex = kKernelWallpaperCount - 1;
        }
        m_selectedIndex = m_selectedBackgroundIndex;
        m_appliedIndex = m_appliedBackgroundIndex;
    } else if (m_activeTab == 1) {
        if (kKernelGradientCount <= 0) {
            m_selectedGradientIndex = 0;
            m_appliedGradientIndex = 0;
        } else {
            if (m_selectedGradientIndex < 0) m_selectedGradientIndex = 0;
            if (m_selectedGradientIndex >= kKernelGradientCount) m_selectedGradientIndex = kKernelGradientCount - 1;
            if (m_appliedGradientIndex < 0) m_appliedGradientIndex = 0;
            if (m_appliedGradientIndex >= kKernelGradientCount) m_appliedGradientIndex = kKernelGradientCount - 1;
        }
        m_selectedIndex = m_selectedGradientIndex;
        m_appliedIndex = m_appliedGradientIndex;
    }
}

void DisplayOptionsApp::clampActiveScrollOffset() {
    GalleryLayout layout = makeGalleryLayout(m_windowW, m_windowH, activeGalleryItemCount());
    int& scroll = activeGalleryScrollOffset();
    if (scroll < 0) scroll = 0;
    if (scroll > layout.maxScroll) scroll = layout.maxScroll;
}

void DisplayOptionsApp::ensureActiveSelectionVisible() {
    const int itemCount = activeGalleryItemCount();
    if (itemCount <= 0) {
        activeGalleryScrollOffset() = 0;
        return;
    }

    GalleryLayout layout = makeGalleryLayout(m_windowW, m_windowH, itemCount);
    int& scroll = activeGalleryScrollOffset();
    const int selected = activeSelectionIndex();
    const int clampedSelected = selected < 0 ? 0 : (selected >= itemCount ? itemCount - 1 : selected);
    const int row = layout.columns > 0 ? (clampedSelected / layout.columns) : 0;
    if (row < scroll) {
        scroll = row;
    } else if (row >= scroll + layout.visibleRows) {
        scroll = row - layout.visibleRows + 1;
    }
    if (scroll < 0) scroll = 0;
    if (scroll > layout.maxScroll) scroll = layout.maxScroll;
}

void DisplayOptionsApp::setActiveSelectionIndex(int index) {
    if (m_activeTab == 0) {
        if (index < 0) index = 0;
        if (index >= kKernelWallpaperCount) index = kKernelWallpaperCount - 1;
        m_selectedBackgroundIndex = index;
        m_selectedIndex = index;
        m_appliedIndex = m_appliedBackgroundIndex;
    } else if (m_activeTab == 1) {
        if (index < 0) index = 0;
        if (index >= kKernelGradientCount) index = kKernelGradientCount - 1;
        m_selectedGradientIndex = index;
        m_selectedIndex = index;
        m_appliedIndex = m_appliedGradientIndex;
    }
}

void DisplayOptionsApp::setActiveTabAndClamp(int tab) {
    if (tab < 0 || tab > 3) return;
    m_activeTab = tab;
    clampSelectionToCurrentTab();
    if (tab == 0 || tab == 1) {
        clampActiveScrollOffset();
        ensureActiveSelectionVisible();
    }
    m_galleryScrollbarDragging = false;
    if (m_selectButtonId >= 0) {
        app::Widget* button = getWidget(m_selectButtonId);
        if (button) {
            const bool galleryTab = (tab == 0 || tab == 1);
            button->visible = galleryTab;
            button->enabled = galleryTab;
            if (galleryTab) {
                setWidgetText(m_selectButtonId, tab == 0 ? "Select Background" : "Apply Gradient");
            }
        }
    }
}

static void display_config_number(uint32_t value, char* destination, uint32_t capacity)
{
    if (destination == nullptr || capacity == 0u) return;
    char reverse[16];
    uint32_t count = 0u;
    do {
        reverse[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(reverse));
    uint32_t out = 0u;
    while (count > 0u && out + 1u < capacity) destination[out++] = reverse[--count];
    destination[out] = '\0';
}

static void display_config_signed_number(int32_t value, char* destination, uint32_t capacity)
{
    if (destination == nullptr || capacity == 0u) return;
    if (value < 0) {
        destination[0] = '-';
        display_config_number(static_cast<uint32_t>(-(value + 1)) + 1u, destination + 1u, capacity - 1u);
    } else {
        display_config_number(static_cast<uint32_t>(value), destination, capacity);
    }
}

struct QemuLogicalDisplayMode {
    const char* id;
    uint32_t width;
    uint32_t height;
};

static const QemuLogicalDisplayMode s_qemuLogicalDisplayModes[] = {
    { "qemu-1280x800", 1280u, 800u },
    { "qemu-1024x768", 1024u, 768u },
    { "qemu-800x600", 800u, 600u }
};

static const uint32_t s_qemuLogicalDisplayModeCount =
    static_cast<uint32_t>(sizeof(s_qemuLogicalDisplayModes) / sizeof(s_qemuLogicalDisplayModes[0]));

static int qemu_logical_display_mode_index(const gxos::display::DisplayConfigurationOutput& output)
{
    for (uint32_t i = 0u; i < s_qemuLogicalDisplayModeCount; ++i) {
        if (output.modeId[0] != '\0' && strcmp(output.modeId, s_qemuLogicalDisplayModes[i].id) == 0) {
            return static_cast<int>(i);
        }
        if (output.modeId[0] == '\0' && output.width == static_cast<int32_t>(s_qemuLogicalDisplayModes[i].width) &&
            output.height == static_cast<int32_t>(s_qemuLogicalDisplayModes[i].height)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static bool qemu_logical_resolution_ui_enabled(const gxos::display::DisplayConfigurationSnapshot& snapshot)
{
    return strcmp(snapshot.backend, "virtio-gpu") == 0 && snapshot.qemuOnly != 0u;
}

static void format_qemu_logical_resolution(const gxos::display::DisplayConfigurationOutput& output,
                                           char* destination,
                                           uint32_t capacity)
{
    if (destination == nullptr || capacity == 0u) return;
    char width[16];
    char height[16];
    display_config_number(static_cast<uint32_t>(output.width > 0 ? output.width : 0), width, sizeof(width));
    display_config_number(static_cast<uint32_t>(output.height > 0 ? output.height : 0), height, sizeof(height));
    strcopy(destination, width, capacity);
    strappend(destination, " x ", capacity);
    strappend(destination, height, capacity);
}

void DisplayOptionsApp::drawDisplayTab(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    const uint32_t panelX = x + 14u;
    const uint32_t panelY = y + 74u;
    const uint32_t panelW = w > 28u ? w - 28u : 1u;
    const uint32_t panelH = h > 92u ? h - 92u : 1u;
    framebuffer::fill_rect(panelX, panelY, panelW, panelH, rgb(22, 22, 24));

    appDrawText(x + 18u, y + 58u, "Display configuration", rgb(230, 230, 238));
    appDrawText(x + 28u, y + 92u, "Mode", rgb(190, 195, 205));

    const auto button = [&](uint32_t bx, uint32_t by, uint32_t bw, const char* label, bool selected) {
        framebuffer::fill_rect(bx, by, bw, 28u, selected ? rgb(62, 96, 150) : rgb(38, 39, 46));
        appDrawRect(bx, by, bw, 28u, rgb(112, 120, 140));
        appDrawText(bx + 10u, by + 9u, label, rgb(235, 238, 246));
    };
    button(x + 88u, y + 86u, 92u, "Extend", m_selectedDisplayMode == gxos::display::DisplayConfigurationMode::Extend);
    button(x + 188u, y + 86u, 92u, "Mirror", m_selectedDisplayMode == gxos::display::DisplayConfigurationMode::Mirror);

    appDrawText(x + 28u, y + 132u, "Primary monitor", rgb(190, 195, 205));
    button(x + 150u, y + 126u, 112u, "Display 1", m_selectedPrimaryOutput == 1u);
    button(x + 270u, y + 126u, 112u, "Display 2", m_selectedPrimaryOutput == 2u);

    char number[16];
    char geometry[40];
    display_config_number(static_cast<uint32_t>(m_activeDisplayConfiguration.virtualDesktopWidth), number, sizeof(number));
    strcopy(geometry, number, sizeof(geometry));
    strappend(geometry, "x", sizeof(geometry));
    display_config_number(static_cast<uint32_t>(m_activeDisplayConfiguration.virtualDesktopHeight), number, sizeof(number));
    strappend(geometry, number, sizeof(geometry));
    appDrawText(x + 28u, y + 174u, "Active desktop", rgb(190, 195, 205));
    appDrawText(x + 150u, y + 174u, geometry, rgb(225, 228, 236));
    appDrawText(x + 28u, y + 198u, "Backend", rgb(190, 195, 205));
    appDrawText(x + 150u, y + 198u, m_activeDisplayConfiguration.backend[0] != '\0' ? m_activeDisplayConfiguration.backend : "Unavailable", rgb(225, 228, 236));

    const gxos::display::DisplayConfigurationSnapshot& requested =
        qemu_logical_resolution_ui_enabled(m_activeDisplayConfiguration)
            ? m_requestedDisplayConfiguration : m_activeDisplayConfiguration;
    const bool qemuLogicalResolution = qemu_logical_resolution_ui_enabled(m_activeDisplayConfiguration);
    char resolution0[32];
    char resolution1[32];
    if (requested.outputCount > 0u) format_qemu_logical_resolution(requested.outputs[0], resolution0, sizeof(resolution0));
    else strcopy(resolution0, "Unavailable", sizeof(resolution0));
    if (requested.outputCount > 1u) format_qemu_logical_resolution(requested.outputs[1], resolution1, sizeof(resolution1));
    else strcopy(resolution1, "Unavailable", sizeof(resolution1));
    appDrawText(x + 28u, y + 230u, "Monitor 1", rgb(190, 195, 205));
    appDrawText(x + 150u, y + 230u, requested.outputCount > 0u ? requested.outputs[0].stableId : "Unavailable", rgb(225, 228, 236));
    if (requested.outputCount > 0u) {
        char origin[32];
        char originNumber[16];
        display_config_signed_number(requested.outputs[0].virtualX, originNumber, sizeof(originNumber));
        strcopy(origin, originNumber, sizeof(origin));
        strappend(origin, ",", sizeof(origin));
        display_config_signed_number(requested.outputs[0].virtualY, originNumber, sizeof(originNumber));
        strappend(origin, originNumber, sizeof(origin));
        appDrawText(x + 300u, y + 230u, origin, rgb(185, 190, 202));
    }
    appDrawText(x + 28u, y + 254u, "Monitor 2", rgb(190, 195, 205));
    appDrawText(x + 150u, y + 254u, requested.outputCount > 1u ? requested.outputs[1].stableId : "Unavailable", rgb(225, 228, 236));
    if (requested.outputCount > 1u) {
        char origin[32];
        char originNumber[16];
        display_config_signed_number(requested.outputs[1].virtualX, originNumber, sizeof(originNumber));
        strcopy(origin, originNumber, sizeof(origin));
        strappend(origin, ",", sizeof(origin));
        display_config_signed_number(requested.outputs[1].virtualY, originNumber, sizeof(originNumber));
        strappend(origin, originNumber, sizeof(origin));
        appDrawText(x + 300u, y + 254u, origin, rgb(185, 190, 202));
    }
    button(x + 430u, y + 216u, 130u, resolution0, qemuLogicalResolution);
    button(x + 430u, y + 240u, 130u, resolution1, qemuLogicalResolution);
    appDrawText(x + 28u, y + 284u,
        qemuLogicalResolution ? "QEMU logical scanout resolution (click a value to cycle)" : "Resolution selection unavailable for this backend",
        rgb(190, 195, 205));
    bool mirrorCompatible = requested.outputCount < 2u ||
        (requested.outputs[0].width == requested.outputs[1].width && requested.outputs[0].height == requested.outputs[1].height);
    appDrawText(x + 28u, y + 306u,
        m_selectedDisplayMode == gxos::display::DisplayConfigurationMode::Mirror
            ? (mirrorCompatible ? "Mirror: compatible resolutions" : "Mirror: rejected until resolutions match")
            : "Extend: unequal logical resolutions are supported",
        mirrorCompatible ? rgb(190, 205, 225) : rgb(235, 180, 120));
    appDrawText(x + 28u, y + 328u, "Refresh rate: Not available    Rotation: Not available", rgb(160, 165, 176));

    if (m_pendingTopologyChange.pending != 0u) {
        framebuffer::fill_rect(x + 18u, y + 348u, w > 36u ? w - 36u : 1u, 54u, rgb(76, 52, 36));
        appDrawRect(x + 18u, y + 348u, w > 36u ? w - 36u : 1u, 54u, rgb(190, 132, 72));
        appDrawText(x + 28u, y + 356u, "Display hardware configuration changed.", rgb(248, 220, 174));
        appDrawText(x + 28u, y + 374u,
            m_pendingTopologyChange.removedOutputCount > 0u ? "Review proposes removing an output; active state is unchanged."
                : "Review proposes restoring an output; active state is unchanged.",
            rgb(232, 216, 192));
        button(x + 430u, y + 350u, 76u, "Review", false);
        button(x + 512u, y + 350u, 76u, "Apply", false);
        button(x + 430u, y + 382u, 76u, "Refresh", false);
        button(x + 512u, y + 382u, 76u, "Keep", false);
    }

    appDrawText(x + 28u, y + static_cast<uint32_t>(maxInt(292, static_cast<int>(h) - 56u)), m_displayStatus[0] != '\0' ? m_displayStatus : "Ready", rgb(190, 205, 225));

    const uint32_t actionY = y + h - 38u;
#if defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) && defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE)
    button(x + 18u, actionY, 126u, "Test Remove", false);
    button(x + 152u, actionY, 126u, "Test Restore", false);
#endif
    button(x + 300u, actionY, 88u, "Apply", false);
    button(x + 396u, actionY, 88u, "OK", false);
    button(x + 492u, actionY, 88u, "Cancel", false);
}

bool DisplayOptionsApp::queryDisplayConfiguration()
{
    gxos::display::DisplayConfigurationCommand command{};
    command.version = gxos::display::kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(command);
    command.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(gxos::display::DisplayConfigurationCommandType::QueryActiveConfiguration);
    gxos::display::DisplayConfigurationResponse response{};
    const uint64_t generation = m_windowGeneration;
    const bool submitted = gxos::display::DisplayConfigurationService::submit(command, response);
    if (generation != m_windowGeneration || response.requestId != command.requestId || response.commandType != command.commandType) return false;
    if (!submitted || response.success == 0u) {
        strcopy(m_displayStatus, "Display query failed", sizeof(m_displayStatus));
        return false;
    }
    m_activeDisplayConfiguration = response.activeConfiguration;
    m_activeConfigurationGeneration = response.activeConfigurationGeneration != 0u
        ? response.activeConfigurationGeneration : 1u;
    if (!m_displayLocalEdits) {
        m_requestedDisplayConfiguration = m_activeDisplayConfiguration;
        m_selectedDisplayMode = static_cast<gxos::display::DisplayConfigurationMode>(m_activeDisplayConfiguration.mode);
        m_appliedDisplayMode = m_selectedDisplayMode;
        for (uint32_t i = 0u; i < m_activeDisplayConfiguration.outputCount && i < gxos::display::kDisplayConfigurationMaxOutputs; ++i) {
            if (m_activeDisplayConfiguration.outputs[i].primary != 0u) {
                m_selectedPrimaryOutput = i + 1u;
                m_appliedPrimaryOutput = m_selectedPrimaryOutput;
                break;
            }
        }
    }
    queryPendingTopologyChange();
    if (m_pendingTopologyChange.pending != 0u) {
        strcopy(m_displayStatus, m_displayLocalEdits
            ? "Pending topology change; resolve local edits"
            : "Display hardware configuration changed", sizeof(m_displayStatus));
    } else if (!m_displayLocalEdits) {
        strcopy(m_displayStatus, "Active configuration loaded", sizeof(m_displayStatus));
    }
    return true;
}

bool DisplayOptionsApp::queryPendingTopologyChange()
{
    gxos::display::DisplayConfigurationCommand command{};
    command.version = gxos::display::kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(command);
    command.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(gxos::display::DisplayConfigurationCommandType::QueryPendingTopologyChange);
    gxos::display::DisplayConfigurationResponse response{};
    const bool submitted = gxos::display::DisplayConfigurationService::submit(command, response);
    if (!submitted || response.success == 0u) return false;
    m_pendingTopologyChange = response.detectedTopologyChange;
    if (response.activeConfigurationGeneration != 0u) m_activeConfigurationGeneration = response.activeConfigurationGeneration;
    return true;
}

bool DisplayOptionsApp::previewPendingTopologyChange()
{
    if (m_pendingTopologyChange.pending == 0u || m_displayLocalEdits) {
        strcopy(m_displayStatus, m_displayLocalEdits ? "Reload or resolve local edits first" : "No pending topology change", sizeof(m_displayStatus));
        invalidate();
        return false;
    }
    gxos::display::DisplayConfigurationCommand command{};
    command.version = gxos::display::kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(command);
    command.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(gxos::display::DisplayConfigurationCommandType::PreviewTopologyReconciliation);
    command.topologyGeneration = m_pendingTopologyChange.topologyGeneration;
    command.activeConfigurationGeneration = m_activeConfigurationGeneration;
    gxos::display::DisplayConfigurationResponse response{};
    const bool submitted = gxos::display::DisplayConfigurationService::submit(command, response);
    if (!submitted || response.success == 0u) {
        strcopy(m_displayStatus, "Review failed: ", sizeof(m_displayStatus));
        strappend(m_displayStatus, response.diagnostic, sizeof(m_displayStatus));
        invalidate();
        return false;
    }
    strcopy(m_displayStatus, response.removedOutputCount > 0u ? "Review: secondary output will be removed" :
        "Review: output will be added to the right", sizeof(m_displayStatus));
    invalidate();
    return true;
}

bool DisplayOptionsApp::applyPendingTopologyChange()
{
    if (m_pendingTopologyChange.pending == 0u || m_displayLocalEdits) {
        strcopy(m_displayStatus, m_displayLocalEdits ? "Reload or resolve local edits first" : "No pending topology change", sizeof(m_displayStatus));
        invalidate();
        return false;
    }
    gxos::display::DisplayConfigurationCommand command{};
    command.version = gxos::display::kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(command);
    command.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(gxos::display::DisplayConfigurationCommandType::ApplyPendingTopologyChange);
    command.flags = gxos::display::DisplayConfigurationFlagCommitPersistence;
    command.origin = static_cast<uint32_t>(gxos::display::DisplayConfigurationRequestOrigin::UserApply);
    command.topologyGeneration = m_pendingTopologyChange.topologyGeneration;
    command.activeConfigurationGeneration = m_activeConfigurationGeneration;
    gxos::display::DisplayConfigurationResponse response{};
    const bool submitted = gxos::display::DisplayConfigurationService::submit(command, response);
    if (!submitted || response.success == 0u) {
        strcopy(m_displayStatus, "Topology apply failed: ", sizeof(m_displayStatus));
        strappend(m_displayStatus, response.diagnostic, sizeof(m_displayStatus));
        invalidate();
        return false;
    }
    m_activeDisplayConfiguration = response.activeConfiguration;
    m_requestedDisplayConfiguration = response.activeConfiguration;
    m_selectedDisplayMode = static_cast<gxos::display::DisplayConfigurationMode>(response.activeConfiguration.mode);
    m_appliedDisplayMode = m_selectedDisplayMode;
    m_selectedPrimaryOutput = 1u;
    for (uint32_t i = 0u; i < response.activeConfiguration.outputCount; ++i) {
        if (response.activeConfiguration.outputs[i].primary != 0u) m_selectedPrimaryOutput = i + 1u;
    }
    m_appliedPrimaryOutput = m_selectedPrimaryOutput;
    m_activeConfigurationGeneration = response.activeConfigurationGeneration;
    m_pendingTopologyChange = gxos::display::DisplayTopologyChangeQuery{};
    m_displayLocalEdits = false;
    strcopy(m_displayStatus, "Detected topology applied", sizeof(m_displayStatus));
    invalidate();
    return true;
}

bool DisplayOptionsApp::dismissPendingTopologyChange()
{
    if (m_pendingTopologyChange.pending == 0u) {
        strcopy(m_displayStatus, "No pending topology change", sizeof(m_displayStatus));
        invalidate();
        return false;
    }
    gxos::display::DisplayConfigurationCommand command{};
    command.version = gxos::display::kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(command);
    command.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(gxos::display::DisplayConfigurationCommandType::DismissPendingTopologyChange);
    command.topologyGeneration = m_pendingTopologyChange.topologyGeneration;
    command.activeConfigurationGeneration = m_activeConfigurationGeneration;
    gxos::display::DisplayConfigurationResponse response{};
    const bool submitted = gxos::display::DisplayConfigurationService::submit(command, response);
    if (!submitted || response.success == 0u) {
        strcopy(m_displayStatus, "Keep current failed: ", sizeof(m_displayStatus));
        strappend(m_displayStatus, response.diagnostic, sizeof(m_displayStatus));
        invalidate();
        return false;
    }
    m_pendingTopologyChange = gxos::display::DisplayTopologyChangeQuery{};
    strcopy(m_displayStatus, "Keeping current configuration", sizeof(m_displayStatus));
    invalidate();
    return true;
}

bool DisplayOptionsApp::refreshPendingTopologyChange()
{
    gxos::display::DisplayConfigurationCommand command{};
    command.version = gxos::display::kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(command);
    command.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(gxos::display::DisplayConfigurationCommandType::RefreshDetectedTopology);
    gxos::display::DisplayConfigurationResponse response{};
    const bool submitted = gxos::display::DisplayConfigurationService::submit(command, response);
    queryPendingTopologyChange();
    strcopy(m_displayStatus, submitted && response.success ? "Displays refreshed; review pending change" : "Display refresh failed", sizeof(m_displayStatus));
    invalidate();
    return submitted && response.success != 0u;
}

bool DisplayOptionsApp::injectPendingTopologyForTest(uint32_t kind)
{
#if defined(GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE) && defined(GXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE)
    if (m_displayLocalEdits) {
        strcopy(m_displayStatus, "Reload or resolve local edits first", sizeof(m_displayStatus));
        invalidate();
        return false;
    }
    const bool injected = kernel::virtio::gpu::inject_display_topology_change_for_test(kind);
    queryPendingTopologyChange();
    strcopy(m_displayStatus, injected
        ? "Injected pending change; review before applying"
        : "Topology test injection rejected", sizeof(m_displayStatus));
    invalidate();
    return injected;
#else
    (void)kind;
    strcopy(m_displayStatus, "Topology tests require the QEMU-only control build", sizeof(m_displayStatus));
    invalidate();
    return false;
#endif
}

bool DisplayOptionsApp::submitDisplayConfiguration(bool closeOnSuccess)
{
    if (m_displayRequestPending) {
        strcopy(m_displayStatus, "Display configuration is busy", sizeof(m_displayStatus));
        invalidate();
        return false;
    }

    gxos::display::DisplayConfigurationCommand command{};
    command.version = gxos::display::kDisplayConfigurationContractVersion;
    command.structureSize = sizeof(command);
    command.requestId = gxos::display::DisplayConfigurationService::nextRequestId();
    command.commandType = static_cast<uint32_t>(gxos::display::DisplayConfigurationCommandType::ApplyConfiguration);
    command.flags = gxos::display::DisplayConfigurationFlagCommitPersistence;
    command.requestedConfiguration.mode = static_cast<uint32_t>(m_selectedDisplayMode);
    command.requestedConfiguration.outputCount = m_activeDisplayConfiguration.outputCount;
    if (command.requestedConfiguration.outputCount > gxos::display::kDisplayConfigurationMaxOutputs) command.requestedConfiguration.outputCount = gxos::display::kDisplayConfigurationMaxOutputs;
    for (uint32_t i = 0u; i < command.requestedConfiguration.outputCount; ++i) {
        command.requestedConfiguration.outputs[i] = m_requestedDisplayConfiguration.outputs[i];
        command.requestedConfiguration.outputs[i].primary = (m_selectedPrimaryOutput == i + 1u) ? 1u : 0u;
        if (m_selectedDisplayMode == gxos::display::DisplayConfigurationMode::Mirror) {
            command.requestedConfiguration.outputs[i].virtualX = 0;
            command.requestedConfiguration.outputs[i].virtualY = 0;
        } else if (i == 0u) {
            command.requestedConfiguration.outputs[i].virtualX = 0;
            command.requestedConfiguration.outputs[i].virtualY = 0;
        } else {
            command.requestedConfiguration.outputs[i].virtualX = command.requestedConfiguration.outputs[0].width;
            command.requestedConfiguration.outputs[i].virtualY = 0;
        }
    }
    if (m_selectedPrimaryOutput >= 1u && m_selectedPrimaryOutput <= command.requestedConfiguration.outputCount) {
        strcopy(command.requestedConfiguration.primaryOutputId,
                command.requestedConfiguration.outputs[m_selectedPrimaryOutput - 1u].stableId,
                sizeof(command.requestedConfiguration.primaryOutputId));
    } else if (command.requestedConfiguration.outputCount > 0u) {
        strcopy(command.requestedConfiguration.primaryOutputId,
                command.requestedConfiguration.outputs[0].stableId,
                sizeof(command.requestedConfiguration.primaryOutputId));
    }

    const uint64_t generation = m_windowGeneration;
    m_displayRequestId = command.requestId;
    m_displayRequestPending = true;
    strcopy(m_displayStatus, "Applying display configuration...", sizeof(m_displayStatus));
    invalidate();

    gxos::display::DisplayConfigurationResponse response{};
    const bool submitted = gxos::display::DisplayConfigurationService::submit(command, response);
    m_displayRequestPending = false;
    if (generation != m_windowGeneration || response.requestId != m_displayRequestId || response.commandType != command.commandType) return false;
    if (!submitted || response.success == 0u) {
        strcopy(m_displayStatus, "Apply failed: ", sizeof(m_displayStatus));
        strappend(m_displayStatus, response.diagnostic[0] != '\0' ? response.diagnostic : "display configuration rejected",
                  sizeof(m_displayStatus));
        invalidate();
        return false;
    }
    m_activeDisplayConfiguration = response.activeConfiguration;
    m_activeConfigurationGeneration = response.activeConfigurationGeneration != 0u
        ? response.activeConfigurationGeneration : m_activeConfigurationGeneration;
    m_requestedDisplayConfiguration = m_activeDisplayConfiguration;
    m_selectedDisplayMode = static_cast<gxos::display::DisplayConfigurationMode>(m_activeDisplayConfiguration.mode);
    m_appliedDisplayMode = m_selectedDisplayMode;
    for (uint32_t i = 0u; i < m_activeDisplayConfiguration.outputCount && i < gxos::display::kDisplayConfigurationMaxOutputs; ++i) {
        if (m_activeDisplayConfiguration.outputs[i].primary != 0u) {
            m_selectedPrimaryOutput = i + 1u;
            m_appliedPrimaryOutput = m_selectedPrimaryOutput;
            break;
        }
    }
    strcopy(m_displayStatus, "Display configuration applied", sizeof(m_displayStatus));
    m_displayLocalEdits = false;
    invalidate();
    if (closeOnSuccess) requestClose();
    return true;
}

void DisplayOptionsApp::cancelDisplayConfiguration()
{
    if (!m_displayRequestPending) queryDisplayConfiguration();
    m_selectedDisplayMode = m_appliedDisplayMode;
    m_selectedPrimaryOutput = m_appliedPrimaryOutput;
    m_requestedDisplayConfiguration = m_activeDisplayConfiguration;
    m_displayRequestPending = false;
    m_displayLocalEdits = false;
    requestClose();
}

void DisplayOptionsApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    m_windowW = static_cast<int>(w);
    m_windowH = static_cast<int>(h);
    framebuffer::fill_rect(x, y, w, h, rgb(28, 30, 38));

    framebuffer::fill_rect(x + 16, y + 16, 140, 30, m_activeTab == 0 ? rgb(58, 58, 58) : rgb(34, 34, 38));
    appDrawRect(x + 16, y + 16, 140, 30, rgb(90, 90, 96));
    appDrawText(x + 28, y + 27, "Backgrounds", m_activeTab == 0 ? rgb(235, 235, 240) : rgb(160, 160, 168));

    framebuffer::fill_rect(x + 166, y + 16, 140, 30, m_activeTab == 2 ? rgb(58, 58, 58) : rgb(34, 34, 38));
    appDrawRect(x + 166, y + 16, 140, 30, rgb(90, 90, 96));
    appDrawText(x + 178, y + 27, "Desktop Icons", m_activeTab == 2 ? rgb(235, 235, 240) : rgb(160, 160, 168));

    framebuffer::fill_rect(x + 316, y + 16, 140, 30, m_activeTab == 1 ? rgb(58, 58, 58) : rgb(34, 34, 38));
    appDrawRect(x + 316, y + 16, 140, 30, rgb(90, 90, 96));
    appDrawText(x + 328, y + 27, "Gradients", m_activeTab == 1 ? rgb(235, 235, 240) : rgb(160, 160, 168));

    framebuffer::fill_rect(x + 466, y + 16, 140, 30, m_activeTab == 3 ? rgb(58, 58, 58) : rgb(34, 34, 38));
    appDrawRect(x + 466, y + 16, 140, 30, rgb(90, 90, 96));
    appDrawText(x + 478, y + 27, "Displays", m_activeTab == 3 ? rgb(235, 235, 240) : rgb(160, 160, 168));

    appDrawText(x + 18, y + 58, m_activeTab == 3 ? "Configure the QEMU display layout:" : (m_activeTab == 2 ? "Choose system icons shown on the desktop:" : (m_activeTab == 0 ? "Select a background from the gallery:" : "Select a gradient from the gallery:")), rgb(230, 230, 238));
    const int panelW = maxInt(1, static_cast<int>(w) - 28);
    const int panelH = maxInt(1, static_cast<int>(h) - 92);
    framebuffer::fill_rect(x + 14, y + 74, static_cast<uint32_t>(panelW), static_cast<uint32_t>(panelH), rgb(22, 22, 24));

    if (m_activeTab == 0 || m_activeTab == 1) {
        const bool showWallpapers = m_activeTab == 0;
        const int itemCount = showWallpapers ? static_cast<int>(kKernelWallpaperCount) : static_cast<int>(kKernelGradientCount);
        GalleryLayout layout = makeGalleryLayout(m_windowW, m_windowH, itemCount);
        int& scrollOffset = activeGalleryScrollOffset();
        if (scrollOffset < 0) scrollOffset = 0;
        if (scrollOffset > layout.maxScroll) scrollOffset = layout.maxScroll;
        const int startRow = scrollOffset;
        const int endRow = startRow + layout.visibleRows;

        for (int i = 0; i < itemCount; ++i) {
            const int row = layout.columns > 0 ? i / layout.columns : 0;
            if (row < startRow || row >= endRow) continue;
            const int col = layout.columns > 0 ? i % layout.columns : 0;
            const uint32_t tx = x + layout.galleryX + col * (kTileW + kTileGap);
            const uint32_t ty = y + layout.galleryY + (row - startRow) * (kTileH + kTileGap);
            const bool selected = showWallpapers ? (i == m_selectedBackgroundIndex) : (i == m_selectedGradientIndex);
            const bool applied = showWallpapers ? (i == m_appliedBackgroundIndex) : (i == m_appliedGradientIndex);

            if (selected) {
                framebuffer::fill_rect(tx - 4, ty - 4, kTileW + 8, kTileH + 8, rgb(72, 110, 180));
            }
            framebuffer::fill_rect(tx, ty, kTileW, kTileH, rgb(42, 42, 42));

            if (showWallpapers) {
                bool drewThumb = kernel::desktop::draw_wallpaper_thumbnail_by_id(s_kernelWallpapers[i].id, tx + 6, ty + 6, kTileW - 12, 42);
                if (!drewThumb) {
                    for (int py = 0; py < 42; ++py) {
                        uint8_t t = (uint8_t)((py * 255) / 41);
                        uint32_t color = 0xFF000000u |
                                         (((((s_kernelWallpapers[i].previewColorA >> 16) & 0xFFu) * (255 - t)) + (((s_kernelWallpapers[i].previewColorB >> 16) & 0xFFu) * t)) / 255) << 16 |
                                         (((((s_kernelWallpapers[i].previewColorA >> 8) & 0xFFu) * (255 - t)) + (((s_kernelWallpapers[i].previewColorB >> 8) & 0xFFu) * t)) / 255) << 8 |
                                         ((((s_kernelWallpapers[i].previewColorA) & 0xFFu) * (255 - t)) + (((s_kernelWallpapers[i].previewColorB) & 0xFFu) * t)) / 255;
                        framebuffer::fill_rect(tx + 6, ty + 6 + (uint32_t)py, kTileW - 12, 1, color);
                    }
                }
                appDrawRect(tx + 6, ty + 6, kTileW - 12, 42, rgb(130, 130, 145));
                appDrawText(tx + 6, ty + 54, s_kernelWallpapers[i].displayName, rgb(220, 220, 225));
            } else {
                for (int py = 0; py < 42; ++py) {
                    uint8_t t = (uint8_t)((py * 255) / 41);
                    uint32_t top = s_kernelGradients[i].topColor;
                    uint32_t bot = s_kernelGradients[i].bottomColor;
                    uint32_t color = 0xFF000000u |
                                     (((((top >> 16) & 0xFFu) * (255 - t)) + (((bot >> 16) & 0xFFu) * t)) / 255) << 16 |
                                     (((((top >> 8) & 0xFFu) * (255 - t)) + (((bot >> 8) & 0xFFu) * t)) / 255) << 8 |
                                     ((((top & 0xFFu) * (255 - t)) + ((bot & 0xFFu) * t)) / 255);
                    framebuffer::fill_rect(tx + 6, ty + 6 + (uint32_t)py, kTileW - 12, 1, color);
                }
                appDrawRect(tx + 6, ty + 6, kTileW - 12, 42, s_kernelGradients[i].accentColor);
                appDrawText(tx + 6, ty + 54, s_kernelGradients[i].displayName, rgb(220, 220, 225));
            }

            if (applied) {
                appDrawText(tx + kTileW - 12, ty + 54, "*", rgb(255, 220, 80));
            }
        }

        if (layout.showScrollbar) {
            framebuffer::fill_rect(x + layout.scrollbarX, y + layout.galleryY, kGalleryScrollBarW, static_cast<uint32_t>(layout.galleryH), rgb(36, 36, 40));
            int thumbH = (layout.visibleRows * layout.galleryH) / (layout.rowCount > 0 ? layout.rowCount : 1);
            if (thumbH < kMinScrollbarThumbH) thumbH = kMinScrollbarThumbH;
            if (thumbH > layout.galleryH) thumbH = layout.galleryH;
            const int thumbTravel = layout.galleryH - thumbH;
            const int thumbY = layout.galleryY + ((thumbTravel * scrollOffset) / (layout.maxScroll > 0 ? layout.maxScroll : 1));
            framebuffer::fill_rect(x + layout.scrollbarX, y + thumbY, kGalleryScrollBarW, static_cast<uint32_t>(thumbH), rgb(150, 160, 176));
        }

        if (m_selectButtonId >= 0) {
            if (app::Widget* button = getWidget(m_selectButtonId)) {
                button->x = 18;
                button->y = layout.buttonY;
                button->w = 142;
                button->h = 28;
            }
        }
    } else if (m_activeTab == 2) {
        if (m_selectButtonId >= 0) {
            if (app::Widget* button = getWidget(m_selectButtonId)) {
                button->visible = false;
                button->enabled = false;
            }
        }
        drawCheckbox(x + kDesktopIconCheckboxX, y + kDesktopIconCheckboxY, "Trash", m_desktopIconVisibility.showTrash);
        drawCheckbox(x + kDesktopIconCheckboxX, y + kDesktopIconCheckboxY + kDesktopIconCheckboxRowH, "File Explorer", m_desktopIconVisibility.showThisSystem || m_desktopIconVisibility.showFileManager);
        drawCheckbox(x + kDesktopIconCheckboxX, y + kDesktopIconCheckboxY + kDesktopIconCheckboxRowH * 2, "System Settings", m_desktopIconVisibility.showSystemSettings);
        appDrawText(x + kDesktopIconCheckboxX, y + maxInt(292, static_cast<int>(h) - 28), "Changes are saved immediately.", rgb(190, 195, 205));
    } else {
        if (m_selectButtonId >= 0) {
            if (app::Widget* button = getWidget(m_selectButtonId)) {
                button->visible = false;
                button->enabled = false;
            }
        }
        drawDisplayTab(x, y, w, h);
    }
}

int DisplayOptionsApp::hitBackground(int mx, int my) const {
    return hitWallpaper(mx, my);
}
int DisplayOptionsApp::hitWallpaper(int mx, int my) const {
    const GalleryLayout layout = makeGalleryLayout(m_windowW, m_windowH, static_cast<int>(kKernelWallpaperCount));
    const int scroll = m_backgroundGalleryScrollOffset < 0 ? 0 : (m_backgroundGalleryScrollOffset > layout.maxScroll ? layout.maxScroll : m_backgroundGalleryScrollOffset);
    if (mx < layout.galleryX || mx >= layout.galleryX + layout.galleryW || my < layout.galleryY || my >= layout.galleryY + layout.galleryH) return -1;
    const int relX = mx - layout.galleryX;
    const int relY = my - layout.galleryY;
    const int colStride = kTileW + kTileGap;
    const int rowStride = kTileH + kTileGap;
    const int col = relX / colStride;
    const int row = relY / rowStride + scroll;
    if (col < 0 || col >= layout.columns || row < 0 || row >= layout.rowCount) return -1;
    const int index = row * layout.columns + col;
    if (index < 0 || index >= static_cast<int>(kKernelWallpaperCount)) return -1;
    const int tx = layout.galleryX + col * colStride;
    const int ty = layout.galleryY + (row - scroll) * rowStride;
    if (mx >= tx && mx < tx + kTileW && my >= ty && my < ty + kTileH) {
        return index;
    }
    return -1;
}

int DisplayOptionsApp::hitGradient(int mx, int my) const {
    const GalleryLayout layout = makeGalleryLayout(m_windowW, m_windowH, static_cast<int>(kKernelGradientCount));
    const int scroll = m_gradientGalleryScrollOffset < 0 ? 0 : (m_gradientGalleryScrollOffset > layout.maxScroll ? layout.maxScroll : m_gradientGalleryScrollOffset);
    if (mx < layout.galleryX || mx >= layout.galleryX + layout.galleryW || my < layout.galleryY || my >= layout.galleryY + layout.galleryH) return -1;
    const int relX = mx - layout.galleryX;
    const int relY = my - layout.galleryY;
    const int colStride = kTileW + kTileGap;
    const int rowStride = kTileH + kTileGap;
    const int col = relX / colStride;
    const int row = relY / rowStride + scroll;
    if (col < 0 || col >= layout.columns || row < 0 || row >= layout.rowCount) return -1;
    const int index = row * layout.columns + col;
    if (index < 0 || index >= static_cast<int>(kKernelGradientCount)) return -1;
    const int tx = layout.galleryX + col * colStride;
    const int ty = layout.galleryY + (row - scroll) * rowStride;
    if (mx >= tx && mx < tx + kTileW && my >= ty && my < ty + kTileH) {
        return index;
    }
    return -1;
}

int DisplayOptionsApp::hitGalleryScrollbar(int mx, int my) const {
    if (m_activeTab != 0 && m_activeTab != 1) return 0;
    const GalleryLayout layout = makeGalleryLayout(m_windowW, m_windowH, activeGalleryItemCount());
    if (!layout.showScrollbar) return 0;
    if (mx < layout.scrollbarX || mx >= layout.scrollbarX + kGalleryScrollBarW || my < layout.galleryY || my >= layout.galleryY + layout.galleryH) return 0;

    const int scroll = m_activeTab == 0 ? m_backgroundGalleryScrollOffset : m_gradientGalleryScrollOffset;
    const int thumbH = maxInt(kMinScrollbarThumbH, (layout.visibleRows * layout.galleryH) / (layout.rowCount > 0 ? layout.rowCount : 1));
    const int thumbTravel = maxInt(1, layout.galleryH - thumbH);
    const int thumbY = layout.galleryY + ((thumbTravel * (scroll < 0 ? 0 : (scroll > layout.maxScroll ? layout.maxScroll : scroll))) / (layout.maxScroll > 0 ? layout.maxScroll : 1));
    if (my >= thumbY && my < thumbY + thumbH) return 1;
    if (my < thumbY) return 2;
    return 3;
}

int DisplayOptionsApp::hitDesktopIconCheckbox(int mx, int my) const {
    for (int i = 0; i < 3; ++i) {
        int y = kDesktopIconCheckboxY + i * kDesktopIconCheckboxRowH;
        if (mx >= kDesktopIconCheckboxX - 8 && mx < kDesktopIconCheckboxX + 280 && my >= y - 8 && my < y + 26) return i;
    }
    return -1;
}

void DisplayOptionsApp::drawCheckbox(uint32_t x, uint32_t y, const char* label, bool checked) {
    framebuffer::fill_rect(x, y, kDesktopIconCheckboxSize, kDesktopIconCheckboxSize, checked ? rgb(70, 110, 180) : rgb(30, 30, 34));
    appDrawRect(x, y, kDesktopIconCheckboxSize, kDesktopIconCheckboxSize, rgb(130, 135, 150));
    if (checked) appDrawText(x + 3, y + 2, "x", rgb(245, 245, 250));
    appDrawText(x + kDesktopIconCheckboxSize + 12, y + 2, label, rgb(225, 228, 236));
}

void DisplayOptionsApp::toggleDesktopIconCheckbox(int index) {
    switch (index) {
        case 0: m_desktopIconVisibility.showTrash = !m_desktopIconVisibility.showTrash; break;
        case 1: {
            const bool enabled = !(m_desktopIconVisibility.showThisSystem || m_desktopIconVisibility.showFileManager);
            m_desktopIconVisibility.showThisSystem = enabled;
            m_desktopIconVisibility.showFileManager = enabled;
            break;
        }
        case 2: m_desktopIconVisibility.showSystemSettings = !m_desktopIconVisibility.showSystemSettings; break;
        default: return;
    }
    serial::puts("[display-options] Desktop Icons checkbox changed\n");
    kernel::desktop::set_system_desktop_icon_visibility(m_desktopIconVisibility);
    invalidate();
}

bool DisplayOptionsApp::handleGalleryKey(uint32_t key) {
    if (m_activeTab != 0 && m_activeTab != 1) return false;

    const int itemCount = activeGalleryItemCount();
    if (itemCount <= 0) return false;

    const GalleryLayout layout = makeGalleryLayout(m_windowW, m_windowH, itemCount);
    int selected = activeSelectionIndex();
    if (selected < 0) selected = 0;
    if (selected >= itemCount) selected = itemCount - 1;

    auto moveSelection = [&](int nextIndex) {
        if (nextIndex < 0) nextIndex = 0;
        if (nextIndex >= itemCount) nextIndex = itemCount - 1;
        setActiveSelectionIndex(nextIndex);
        ensureActiveSelectionVisible();
        invalidate();
        return true;
    };

    auto scrollByRows = [&](int rows) {
        if (rows == 0) return false;
        int& scroll = activeGalleryScrollOffset();
        int nextOffset = scroll + rows;
        if (nextOffset < 0) nextOffset = 0;
        if (nextOffset > layout.maxScroll) nextOffset = layout.maxScroll;
        if (nextOffset == scroll) return false;
        scroll = nextOffset;
        invalidate();
        return true;
    };

    const int row = layout.columns > 0 ? (selected / layout.columns) : 0;
    const int col = layout.columns > 0 ? (selected % layout.columns) : 0;

    if (key == shell::KEY_LEFT) {
        const int rowStart = row * layout.columns;
        return moveSelection(selected > rowStart ? selected - 1 : rowStart);
    }
    if (key == shell::KEY_RIGHT) {
        const int rowEnd = minInt(row * layout.columns + layout.columns - 1, itemCount - 1);
        return moveSelection(selected < rowEnd ? selected + 1 : rowEnd);
    }
    if (key == shell::KEY_UP) {
        if (row <= 0) return moveSelection(selected);
        const int nextIndex = minInt((row - 1) * layout.columns + col, itemCount - 1);
        return moveSelection(nextIndex);
    }
    if (key == shell::KEY_DOWN) {
        if (row + 1 >= layout.rowCount) return moveSelection(selected);
        const int nextIndex = minInt((row + 1) * layout.columns + col, itemCount - 1);
        return moveSelection(nextIndex);
    }
    if (key == shell::KEY_HOME) {
        return moveSelection(0);
    }
    if (key == shell::KEY_END) {
        return moveSelection(itemCount - 1);
    }
    if (key == shell::KEY_PGUP) {
        return scrollByRows(-layout.visibleRows);
    }
    if (key == shell::KEY_PGDN) {
        return scrollByRows(layout.visibleRows);
    }
    if (key == '\r' || key == '\n' || key == ' ') {
        applySelected();
        return true;
    }
    return false;
}

void DisplayOptionsApp::onKeyDown(uint32_t key) {
    if (m_activeTab == 3) {
        if (key == '\r' || key == '\n') {
            submitDisplayConfiguration(false);
            return;
        }
        if (key == 0x1Bu) {
            cancelDisplayConfiguration();
            return;
        }
        return;
    }
    if (handleGalleryKey(key)) return;
}

void DisplayOptionsApp::onKeyChar(char c) {
    if (c == ' ' || c == '\r' || c == '\n') {
        if (m_activeTab == 0 || m_activeTab == 1) {
            applySelected();
        }
    }
}

void DisplayOptionsApp::onMouseDown(int x, int y, uint8_t) {
    if (x >= 16 && x < 156 && y >= 16 && y < 46) {
        setActiveTab(0);
        invalidate();
        return;
    }
    if (x >= 166 && x < 306 && y >= 16 && y < 46) {
        setActiveTab(2);
        serial::puts("[display-options] Desktop Icons UI selected\n");
        invalidate();
        return;
    }
    if (x >= 316 && x < 456 && y >= 16 && y < 46) {
        setActiveTab(1);
        invalidate();
        return;
    }
    if (x >= 466 && x < 606 && y >= 16 && y < 46) {
        setActiveTab(3);
        queryDisplayConfiguration();
        invalidate();
        return;
    }

    if (m_activeTab == 2) {
        int hit = hitDesktopIconCheckbox(x, y);
        if (hit >= 0) {
            toggleDesktopIconCheckbox(hit);
            invalidate();
        }
        return;
    }

    if (m_activeTab == 3) {
        if (m_pendingTopologyChange.pending != 0u) {
            if (y >= 350 && y < 378 && x >= 430 && x < 506) {
                previewPendingTopologyChange();
                return;
            }
            if (y >= 350 && y < 378 && x >= 512 && x < 588) {
                applyPendingTopologyChange();
                return;
            }
            if (y >= 382 && y < 410 && x >= 430 && x < 506) {
                refreshPendingTopologyChange();
                return;
            }
            if (y >= 382 && y < 410 && x >= 512 && x < 588) {
                dismissPendingTopologyChange();
                return;
            }
        }
        const uint32_t testActionY = static_cast<uint32_t>(m_windowH - 38);
        if (y >= static_cast<int>(testActionY) && y < static_cast<int>(testActionY + 28u)) {
            if (x >= 18 && x < 144) {
                injectPendingTopologyForTest(static_cast<uint32_t>(gxos::display::VirtioGpuInjectedTopologyChangeKind::OutputRemoval));
                return;
            }
            if (x >= 152 && x < 278) {
                injectPendingTopologyForTest(static_cast<uint32_t>(gxos::display::VirtioGpuInjectedTopologyChangeKind::OutputAddition));
                return;
            }
        }
        if (y >= 86 && y < 114) {
            if (x >= 88 && x < 180) m_selectedDisplayMode = gxos::display::DisplayConfigurationMode::Extend;
            else if (x >= 188 && x < 280) m_selectedDisplayMode = gxos::display::DisplayConfigurationMode::Mirror;
            m_requestedDisplayConfiguration.mode = static_cast<uint32_t>(m_selectedDisplayMode);
            m_displayLocalEdits = true;
            invalidate();
            return;
        }
        if (y >= 126 && y < 154) {
            if (x >= 150 && x < 262) m_selectedPrimaryOutput = 1u;
            else if (x >= 270 && x < 382 && m_activeDisplayConfiguration.outputCount > 1u) m_selectedPrimaryOutput = 2u;
            m_displayLocalEdits = true;
            invalidate();
            return;
        }
        if (qemu_logical_resolution_ui_enabled(m_activeDisplayConfiguration) && y >= 210 && y < 238 &&
            x >= 430 && x < 560 && m_requestedDisplayConfiguration.outputCount > 0u) {
            const int current = qemu_logical_display_mode_index(m_requestedDisplayConfiguration.outputs[0]);
            const uint32_t next = current < 0 ? 0u : (static_cast<uint32_t>(current) + 1u) % s_qemuLogicalDisplayModeCount;
            m_requestedDisplayConfiguration.outputs[0].width = static_cast<int32_t>(s_qemuLogicalDisplayModes[next].width);
            m_requestedDisplayConfiguration.outputs[0].height = static_cast<int32_t>(s_qemuLogicalDisplayModes[next].height);
            strcopy(m_requestedDisplayConfiguration.outputs[0].modeId, s_qemuLogicalDisplayModes[next].id,
                    sizeof(m_requestedDisplayConfiguration.outputs[0].modeId));
            strcopy(m_displayStatus, "Requested monitor 1 resolution; click Apply", sizeof(m_displayStatus));
            m_displayLocalEdits = true;
            invalidate();
            return;
        }
        if (qemu_logical_resolution_ui_enabled(m_activeDisplayConfiguration) && y >= 238 && y < 266 &&
            x >= 430 && x < 560 && m_requestedDisplayConfiguration.outputCount > 1u) {
            const int current = qemu_logical_display_mode_index(m_requestedDisplayConfiguration.outputs[1]);
            const uint32_t next = current < 0 ? 0u : (static_cast<uint32_t>(current) + 1u) % s_qemuLogicalDisplayModeCount;
            m_requestedDisplayConfiguration.outputs[1].width = static_cast<int32_t>(s_qemuLogicalDisplayModes[next].width);
            m_requestedDisplayConfiguration.outputs[1].height = static_cast<int32_t>(s_qemuLogicalDisplayModes[next].height);
            strcopy(m_requestedDisplayConfiguration.outputs[1].modeId, s_qemuLogicalDisplayModes[next].id,
                    sizeof(m_requestedDisplayConfiguration.outputs[1].modeId));
            strcopy(m_displayStatus, "Requested monitor 2 resolution; click Apply", sizeof(m_displayStatus));
            m_displayLocalEdits = true;
            invalidate();
            return;
        }
        const int actionY = m_windowH - 38;
        if (y >= actionY && y < actionY + 28) {
            if (x >= 300 && x < 388) submitDisplayConfiguration(false);
            else if (x >= 396 && x < 484) submitDisplayConfiguration(true);
            else if (x >= 492 && x < 580) cancelDisplayConfiguration();
        }
        return;
    }

    if (m_activeTab == 0 || m_activeTab == 1) {
        const int scrollbarHit = hitGalleryScrollbar(x, y);
        if (scrollbarHit == 1) {
            m_galleryScrollbarDragging = true;
            m_galleryScrollbarDragStartY = y;
            m_galleryScrollbarDragStartOffset = activeGalleryScrollOffset();
            return;
        }
        if (scrollbarHit == 2 || scrollbarHit == 3) {
            GalleryLayout layout = makeGalleryLayout(m_windowW, m_windowH, activeGalleryItemCount());
            int& scroll = activeGalleryScrollOffset();
            int nextOffset = scroll + (scrollbarHit == 2 ? -layout.visibleRows : layout.visibleRows);
            if (nextOffset < 0) nextOffset = 0;
            if (nextOffset > layout.maxScroll) nextOffset = layout.maxScroll;
            if (nextOffset != scroll) {
                scroll = nextOffset;
                invalidate();
            }
            return;
        }

        int hit = m_activeTab == 1 ? hitGradient(x, y) : hitWallpaper(x, y);
        if (hit >= 0) {
            setActiveSelectionIndex(hit);
            invalidate();
        }
        return;
    }
}

void DisplayOptionsApp::onMouseMove(int x, int y) {
    if (!m_galleryScrollbarDragging || (m_activeTab != 0 && m_activeTab != 1)) return;
    const GalleryLayout layout = makeGalleryLayout(m_windowW, m_windowH, activeGalleryItemCount());
    if (!layout.showScrollbar) return;
    const int thumbH = maxInt(kMinScrollbarThumbH, (layout.visibleRows * layout.galleryH) / (layout.rowCount > 0 ? layout.rowCount : 1));
    const int trackTravel = maxInt(1, layout.galleryH - thumbH);
    const int nextOffset = m_galleryScrollbarDragStartOffset + ((y - m_galleryScrollbarDragStartY) * layout.maxScroll) / trackTravel;
    int& scroll = activeGalleryScrollOffset();
    const int clamped = nextOffset < 0 ? 0 : (nextOffset > layout.maxScroll ? layout.maxScroll : nextOffset);
    if (clamped != scroll) {
        scroll = clamped;
        invalidate();
    }
}

void DisplayOptionsApp::onMouseUp(int, int, uint8_t) {
    m_galleryScrollbarDragging = false;
}

void DisplayOptionsApp::onMouseWheel(int x, int y, int wheelDelta)
{
    if (wheelDelta == 0 || (m_activeTab != 0 && m_activeTab != 1)) return;
    const GalleryLayout layout = makeGalleryLayout(m_windowW, m_windowH, activeGalleryItemCount());
    const bool inGallery = x >= layout.galleryX && x < layout.galleryX + layout.galleryW && y >= layout.galleryY && y < layout.galleryY + layout.galleryH;
    const bool onScrollbar = layout.showScrollbar && x >= layout.scrollbarX && x < layout.scrollbarX + kGalleryScrollBarW && y >= layout.galleryY && y < layout.galleryY + layout.galleryH;
    if (!inGallery && !onScrollbar) return;

    int& scroll = activeGalleryScrollOffset();
    const int previousOffset = scroll;
    scroll -= wheelDelta;
    if (scroll < 0) scroll = 0;
    if (scroll > layout.maxScroll) scroll = layout.maxScroll;
    if (scroll != previousOffset) {
        invalidate();
    }
}

void DisplayOptionsApp::onWidgetClick(int widgetId) {
    if (widgetId == m_selectButtonId && (m_activeTab == 0 || m_activeTab == 1)) applySelected();
}

void DisplayOptionsApp::applySelected() {
    if (m_activeTab == 2) return;

    if (m_activeTab == 1) {
        if (m_selectedGradientIndex < 0 || m_selectedGradientIndex >= kKernelGradientCount) return;
        kernel::desktop::set_wallpaper_by_id(s_kernelGradients[m_selectedGradientIndex].id);
        m_appliedGradientIndex = m_selectedGradientIndex;
        m_appliedIndex = m_appliedGradientIndex;
        m_selectedIndex = m_selectedGradientIndex;
        invalidate();
        return;
    }

    if (m_selectedBackgroundIndex < 0 || m_selectedBackgroundIndex >= kKernelWallpaperCount) return;
    kernel::desktop::set_wallpaper_by_id(s_kernelWallpapers[m_selectedBackgroundIndex].id);
    m_appliedBackgroundIndex = m_selectedBackgroundIndex;
    m_selectedIndex = m_selectedBackgroundIndex;
    m_appliedIndex = m_selectedIndex;
    invalidate();
}
// ============================================================
// CalculatorApp Implementation
// ============================================================

CalculatorApp::CalculatorApp() 
    : m_accumulator(0), m_operand(0), m_operation('\0'), m_newNumber(true), m_displayId(-1) {
    strcopy(m_name, "Calculator", app::MAX_APP_NAME);
    m_display[0] = '0';
    m_display[1] = '\0';
    for (int i = 0; i < 20; i++) m_btnIds[i] = -1;
}

CalculatorApp::~CalculatorApp() {
}

bool CalculatorApp::init() {
    m_window = new app::KernelWindow();
    strcopy(m_window->title, "Calculator", app::MAX_TITLE_LEN);
    m_window->x = 200;
    m_window->y = 80;
    m_window->w = 220;
    m_window->h = 280;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_FOCUSED;
    m_window->owner = this;
    
    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }
    
    // Create display
    m_displayId = addLabel(10, 10, 200, 30, "0");
    
    // Create buttons (4x5 grid)
    const char* btnLabels[] = {
        "C", "CE", "%", "/",
        "7", "8", "9", "*",
        "4", "5", "6", "-",
        "1", "2", "3", "+",
        "+/-", "0", ".", "="
    };
    
    int btnW = 45;
    int btnH = 35;
    int startY = 50;
    int gap = 5;
    
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            int idx = row * 4 + col;
            int bx = 10 + col * (btnW + gap);
            int by = startY + row * (btnH + gap);
            m_btnIds[idx] = addButton(bx, by, btnW, btnH, btnLabels[idx]);
        }
    }
    
    m_state = app::AppState::Running;
    return true;
}

void CalculatorApp::shutdown() {
    m_state = app::AppState::Terminated;
}

void CalculatorApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    // Display background
    framebuffer::fill_rect(x + 10, y + 10, w - 20, 30, rgb(30, 35, 45));
    
    // Display border
    for (uint32_t i = 0; i < w - 20; i++) {
        framebuffer::put_pixel(x + 10 + i, y + 10, rgb(60, 70, 90));
        framebuffer::put_pixel(x + 10 + i, y + 39, rgb(60, 70, 90));
    }
    for (uint32_t i = 0; i < 30; i++) {
        framebuffer::put_pixel(x + 10, y + 10 + i, rgb(60, 70, 90));
        framebuffer::put_pixel(x + w - 11, y + 10 + i, rgb(60, 70, 90));
    }
    
    // Display text (right-aligned)
    int dispLen = strlen_local(m_display);
    uint32_t textW = dispLen * (kGlyphW + kGlyphSpacing);
    uint32_t textX = x + w - 15 - textW;
    uint32_t textY = y + 10 + (30 - kGlyphH) / 2;
    
    // Draw display digits
    for (int i = 0; i < dispLen; i++) {
        char c = m_display[i];
        uint32_t cx = textX + i * (kGlyphW + kGlyphSpacing);
        
        // Simple digit rendering
        if (c >= '0' && c <= '9') {
            for (int dy = 0; dy < kGlyphH; dy++) {
                for (int dx = 0; dx < kGlyphW; dx++) {
                    bool on = ((c - '0' + dx + dy) % 2 == 0);
                    if (on) {
                        framebuffer::put_pixel(cx + dx, textY + dy, rgb(200, 220, 255));
                    }
                }
            }
        } else if (c == '.') {
            framebuffer::put_pixel(cx + 2, textY + kGlyphH - 1, rgb(200, 220, 255));
            framebuffer::put_pixel(cx + 2, textY + kGlyphH - 2, rgb(200, 220, 255));
        } else if (c == '-') {
            for (int dx = 0; dx < kGlyphW; dx++) {
                framebuffer::put_pixel(cx + dx, textY + kGlyphH / 2, rgb(200, 220, 255));
            }
        }
    }
    
    (void)h;
}

void CalculatorApp::onWidgetClick(int widgetId) {
    const char* btnChars = "Cce/%789*456-123++/-0.=";
    
    for (int i = 0; i < 20; i++) {
        if (m_btnIds[i] == widgetId) {
            if (i == 0) handleButton('C');
            else if (i == 1) handleButton('E');  // CE
            else if (i == 2) handleButton('%');
            else if (i == 3) handleButton('/');
            else if (i >= 4 && i <= 6) handleButton('7' + (i - 4));
            else if (i == 7) handleButton('*');
            else if (i >= 8 && i <= 10) handleButton('4' + (i - 8));
            else if (i == 11) handleButton('-');
            else if (i >= 12 && i <= 14) handleButton('1' + (i - 12));
            else if (i == 15) handleButton('+');
            else if (i == 16) handleButton('N');  // +/-
            else if (i == 17) handleButton('0');
            else if (i == 18) handleButton('.');
            else if (i == 19) handleButton('=');
            break;
        }
    }
}

void CalculatorApp::onKeyChar(char c) {
    if ((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' ||
        c == '*' || c == '/' || c == '=' || c == '\r' || c == '\n' ||
        c == 'c' || c == 'C') {
        if (c == '\r' || c == '\n') c = '=';
        handleButton(c);
    }
}

void CalculatorApp::handleButton(char btn) {
    if (btn >= '0' && btn <= '9') {
        if (m_newNumber) {
            m_display[0] = btn;
            m_display[1] = '\0';
            m_newNumber = false;
        } else {
            int len = strlen_local(m_display);
            if (len < 15) {
                m_display[len] = btn;
                m_display[len + 1] = '\0';
            }
        }
    } else if (btn == '.') {
        // Check if already has decimal point
        bool hasDot = false;
        for (int i = 0; m_display[i]; i++) {
            if (m_display[i] == '.') hasDot = true;
        }
        if (!hasDot) {
            int len = strlen_local(m_display);
            if (len < 15) {
                m_display[len] = '.';
                m_display[len + 1] = '\0';
            }
        }
        m_newNumber = false;
    } else if (btn == '+' || btn == '-' || btn == '*' || btn == '/') {
        // Parse current display
        double val = 0;
        double frac = 0;
        bool negative = false;
        bool afterDot = false;
        double fracDiv = 10;
        
        for (int i = 0; m_display[i]; i++) {
            char c = m_display[i];
            if (c == '-' && i == 0) negative = true;
            else if (c == '.') afterDot = true;
            else if (c >= '0' && c <= '9') {
                if (afterDot) {
                    frac += (c - '0') / fracDiv;
                    fracDiv *= 10;
                } else {
                    val = val * 10 + (c - '0');
                }
            }
        }
        val += frac;
        if (negative) val = -val;
        
        if (m_operation != '\0') {
            m_operand = val;
            calculate();
        } else {
            m_accumulator = val;
        }
        
        m_operation = btn;
        m_newNumber = true;
    } else if (btn == '=') {
        // Parse and calculate
        double val = 0;
        double frac = 0;
        bool negative = false;
        bool afterDot = false;
        double fracDiv = 10;
        
        for (int i = 0; m_display[i]; i++) {
            char c = m_display[i];
            if (c == '-' && i == 0) negative = true;
            else if (c == '.') afterDot = true;
            else if (c >= '0' && c <= '9') {
                if (afterDot) {
                    frac += (c - '0') / fracDiv;
                    fracDiv *= 10;
                } else {
                    val = val * 10 + (c - '0');
                }
            }
        }
        val += frac;
        if (negative) val = -val;
        
        m_operand = val;
        calculate();
        m_operation = '\0';
        m_newNumber = true;
    } else if (btn == 'C') {
        clear();
    } else if (btn == 'E') {
        clearEntry();
    } else if (btn == 'N') {
        // Negate
        if (m_display[0] == '-') {
            for (int i = 0; m_display[i]; i++) {
                m_display[i] = m_display[i + 1];
            }
        } else {
            int len = strlen_local(m_display);
            for (int i = len; i >= 0; i--) {
                m_display[i + 1] = m_display[i];
            }
            m_display[0] = '-';
        }
    }
    
    updateDisplay();
    invalidate();
}

void CalculatorApp::updateDisplay() {
    setWidgetText(m_displayId, m_display);
}

void CalculatorApp::calculate() {
    switch (m_operation) {
        case '+': m_accumulator = m_accumulator + m_operand; break;
        case '-': m_accumulator = m_accumulator - m_operand; break;
        case '*': m_accumulator = m_accumulator * m_operand; break;
        case '/': 
            if (m_operand != 0) {
                m_accumulator = m_accumulator / m_operand;
            } else {
                strcopy(m_display, "Error", 32);
                return;
            }
            break;
    }
    
    // Convert result to string
    int intPart = (int)m_accumulator;
    double fracPart = m_accumulator - intPart;
    if (fracPart < 0) fracPart = -fracPart;
    
    int idx = 0;
    if (m_accumulator < 0) {
        m_display[idx++] = '-';
        intPart = -intPart;
    }
    
    // Integer part
    if (intPart == 0) {
        m_display[idx++] = '0';
    } else {
        char temp[16];
        int ti = 0;
        while (intPart > 0) {
            temp[ti++] = '0' + (intPart % 10);
            intPart /= 10;
        }
        while (ti > 0) {
            m_display[idx++] = temp[--ti];
        }
    }
    
    // Fractional part (up to 6 digits)
    if (fracPart > 0.0000001) {
        m_display[idx++] = '.';
        for (int i = 0; i < 6 && fracPart > 0.0000001; i++) {
            fracPart *= 10;
            int digit = (int)fracPart;
            m_display[idx++] = '0' + digit;
            fracPart -= digit;
        }
    }
    
    m_display[idx] = '\0';
}

void CalculatorApp::clear() {
    m_accumulator = 0;
    m_operand = 0;
    m_operation = '\0';
    m_newNumber = true;
    m_display[0] = '0';
    m_display[1] = '\0';
}

void CalculatorApp::clearEntry() {
    m_display[0] = '0';
    m_display[1] = '\0';
    m_newNumber = true;
}

// ============================================================
// ClockApp Implementation
// ============================================================

namespace {
    static void append_two_digits(unsigned value, char* out, int outSize)
    {
        if (!out || outSize <= 0) return;
        if (outSize < 3) {
            out[0] = '\0';
            return;
        }
        if (value > 99u) value %= 100u;
        out[0] = static_cast<char>('0' + ((value / 10u) % 10u));
        out[1] = static_cast<char>('0' + (value % 10u));
        out[2] = '\0';
    }

    static void draw_scaled_char(uint32_t px, uint32_t py, char c, uint32_t color, int scale)
    {
        const uint8_t* glyph = getGlyph(c);
        if (!glyph || scale <= 0) return;
        for (int col = 0; col < kGlyphW; ++col) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < kGlyphH; ++row) {
                if ((bits & (1 << row)) == 0) continue;
                framebuffer::fill_rect(
                    px + static_cast<uint32_t>(col * scale),
                    py + static_cast<uint32_t>(row * scale),
                    static_cast<uint32_t>(scale),
                    static_cast<uint32_t>(scale),
                    color
                );
            }
        }
    }

    static void draw_scaled_text(uint32_t x, uint32_t y, const char* text, uint32_t color, int scale)
    {
        if (!text || scale <= 0) return;
        uint32_t cx = x;
        while (*text) {
            draw_scaled_char(cx, y, *text, color, scale);
            cx += static_cast<uint32_t>((kGlyphW + kGlyphSpacing) * scale);
            ++text;
        }
    }
}

ClockApp::ClockApp()
    : m_timeAvailable(false), m_lastRefreshTick(0)
{
    strcopy(m_name, "Clock", app::MAX_APP_NAME);
    m_timeText[0] = '\0';
    m_dateText[0] = '\0';
    m_statusText[0] = '\0';
}

ClockApp::~ClockApp()
{
}

bool ClockApp::init()
{
    m_window = new app::KernelWindow();
    if (!m_window) return false;

    strcopy(m_window->title, "Clock", app::MAX_TITLE_LEN);
    m_window->x = 180;
    m_window->y = 72;
    m_window->w = 360;
    m_window->h = 190;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_RESIZABLE | app::WF_FOCUSED;
    m_window->owner = this;

    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }

    refreshSnapshot();
    m_lastRefreshTick = kernel::pit::ticks();
    m_state = app::AppState::Running;
    return true;
}

void ClockApp::shutdown()
{
    m_state = app::AppState::Terminated;
}

void ClockApp::update()
{
    const uint64_t now = kernel::pit::ticks();
    if (now == m_lastRefreshTick) return;
    if (m_lastRefreshTick == 0 || (now - m_lastRefreshTick) >= 10ULL) {
        m_lastRefreshTick = now;
        refreshSnapshot();
        invalidate();
    }
}

void ClockApp::refreshSnapshot()
{
    kernel::time::DateTime now{};
    m_timeAvailable = kernel::time::get_current_datetime(now);

    if (m_timeAvailable) {
        char hour[4];
        char minute[4];
        char second[4];
        char month[4];
        char day[4];
        char year[8];

        append_two_digits(now.hour, hour, sizeof(hour));
        append_two_digits(now.minute, minute, sizeof(minute));
        append_two_digits(now.second, second, sizeof(second));
        append_two_digits(now.month, month, sizeof(month));
        append_two_digits(now.day, day, sizeof(day));
        int_to_text(now.year, year, sizeof(year));

        strcopy(m_timeText, hour, sizeof(m_timeText));
        strappend(m_timeText, ":", sizeof(m_timeText));
        strappend(m_timeText, minute, sizeof(m_timeText));
        strappend(m_timeText, ":", sizeof(m_timeText));
        strappend(m_timeText, second, sizeof(m_timeText));

        strcopy(m_dateText, year, sizeof(m_dateText));
        strappend(m_dateText, "-", sizeof(m_dateText));
        strappend(m_dateText, month, sizeof(m_dateText));
        strappend(m_dateText, "-", sizeof(m_dateText));
        strappend(m_dateText, day, sizeof(m_dateText));

        strcopy(m_statusText, "RTC/CMOS time available", sizeof(m_statusText));
    } else {
        uint64_t totalSeconds = kernel::pit::ticks() / 100ULL;
        uint64_t hours = totalSeconds / 3600ULL;
        uint64_t minutes = (totalSeconds / 60ULL) % 60ULL;
        uint64_t seconds = totalSeconds % 60ULL;
        char hour[16];
        char minute[4];
        char second[4];

        int_to_text(static_cast<int>(hours), hour, sizeof(hour));
        append_two_digits(static_cast<unsigned>(minutes), minute, sizeof(minute));
        append_two_digits(static_cast<unsigned>(seconds), second, sizeof(second));

        strcopy(m_timeText, "UPTIME ", sizeof(m_timeText));
        strappend(m_timeText, hour, sizeof(m_timeText));
        strappend(m_timeText, ":", sizeof(m_timeText));
        strappend(m_timeText, minute, sizeof(m_timeText));
        strappend(m_timeText, ":", sizeof(m_timeText));
        strappend(m_timeText, second, sizeof(m_timeText));

        strcopy(m_dateText, "Date unavailable without RTC", sizeof(m_dateText));
        strcopy(m_statusText, "RTC unavailable; using pit::ticks() fallback.", sizeof(m_statusText));
    }
}

void ClockApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    framebuffer::fill_rect(x, y, w, h, rgb(22, 26, 34));
    const uint32_t innerW = w > 24 ? w - 24 : 0;
    const uint32_t innerH = h > 24 ? h - 24 : 0;
    if (innerW > 0 && innerH > 0) {
        framebuffer::fill_rect(x + 12, y + 12, innerW, innerH, rgb(30, 36, 46));
        framebuffer::fill_rect(x + 12, y + 12, innerW, 1, rgb(74, 104, 148));
    }

    appDrawText(x + 18, y + 16, "Clock", rgb(240, 244, 250));
    draw_scaled_text(x + 18, y + 46, m_timeText, rgb(208, 222, 248), 3);
    appDrawText(x + 20, y + 112, m_dateText, rgb(218, 224, 234));
    appDrawText(x + 20, y + 136, m_statusText, rgb(158, 166, 180));
}

// ============================================================
// TaskManagerApp Implementation
// ============================================================

extern "C" size_t gxos_kernel_heap_total_bytes();
extern "C" size_t gxos_kernel_heap_used_bytes();
extern "C" size_t gxos_kernel_heap_free_bytes();

namespace {
    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static void drawRoundedPanel(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t fillColor, uint32_t borderColor) {
        framebuffer::fill_rect(x, y, w, h, fillColor);
        framebuffer::fill_rect(x, y, w, 1, borderColor);
        framebuffer::fill_rect(x, y + h - 1, w, 1, borderColor);
        framebuffer::fill_rect(x, y, 1, h, borderColor);
        framebuffer::fill_rect(x + w - 1, y, 1, h, borderColor);
    }

    static void drawTabButton(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* label, bool active) {
        drawRoundedPanel(x, y, w, h, active ? rgb(52, 62, 80) : rgb(34, 36, 44), active ? rgb(92, 130, 196) : rgb(64, 68, 80));
        uint32_t textX = x + 12;
        uint32_t textY = y + (h - kGlyphH) / 2;
        appDrawText(textX, textY, label, active ? rgb(240, 244, 250) : rgb(168, 174, 186));
    }

    static void drawHistoryGraph(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t* history, int count, int head, uint32_t accentColor) {
        drawRoundedPanel(x, y, w, h, rgb(28, 30, 36), rgb(72, 78, 92));
        if (!history || count <= 0) {
            appDrawText(x + 12, y + (h - kGlyphH) / 2, "N/A", rgb(160, 166, 176));
            return;
        }

        const int bars = (count < (int)w - 16) ? count : (int)w - 16;
        const int plotX = (int)x + 8;
        const int plotY = (int)y + 10;
        const int plotW = (int)w - 16;
        const int plotH = (int)h - 16;
        const int colW = bars > 0 ? (plotW / bars > 0 ? plotW / bars : 1) : 1;
        for (int i = 0; i < bars; ++i) {
            const int idx = (head - count + i + 48) % 48;
            const int pct = clampInt((int)history[idx], 0, 100);
            const int barH = (plotH * pct) / 100 > 0 ? (plotH * pct) / 100 : 1;
            const int barX = plotX + i * colW;
            const int barY = plotY + plotH - barH;
            framebuffer::fill_rect(barX, barY, (colW - 1) > 0 ? (colW - 1) : 1, barH, accentColor);
        }
    }
}

TaskManagerApp::TaskManagerApp() 
    : m_selectedApp(-1), m_activeTab(0), m_refreshBtnId(-1), m_endTaskBtnId(-1), 
      m_lastUpdate(0), m_cpuHistoryCount(0), m_cpuHistoryHead(0), m_heapHistoryCount(0), m_heapHistoryHead(0), m_entryCount(0) {
    strcopy(m_name, "TaskManager", app::MAX_APP_NAME);
    for (int i = 0; i < kHistoryMax; ++i) {
        m_cpuHistory[i] = 0;
        m_heapHistory[i] = 0;
    }
}

TaskManagerApp::~TaskManagerApp() {
}

bool TaskManagerApp::init() {
    m_window = new app::KernelWindow();
    strcopy(m_window->title, "Task Manager", app::MAX_TITLE_LEN);
    m_window->x = 120;
    m_window->y = 48;
    m_window->w = 760;
    m_window->h = 520;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_RESIZABLE | app::WF_FOCUSED;
    m_window->owner = this;
    
    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }
    
    // Create buttons
    m_refreshBtnId = addButton(10, m_window->h - 42, 92, 28, "Refresh");
    m_endTaskBtnId = addButton(110, m_window->h - 42, 92, 28, "End Task");
    
    refreshList();
    
    m_state = app::AppState::Running;
    return true;
}

void TaskManagerApp::shutdown() {
    m_state = app::AppState::Terminated;
}

void TaskManagerApp::update() {
    // Performance gets a faster cadence; the other tabs keep the older slower
    // refresh so we avoid unnecessary idle redraws.
    m_lastUpdate++;
    bool shouldInvalidate = false;
    const uint32_t refreshInterval = m_activeTab == 1 ? 100U : 200U;
    if (m_lastUpdate >= refreshInterval) {
        refreshList();
        m_lastUpdate = 0;
        shouldInvalidate = true;
    }

    const kernel::desktop::CpuTelemetrySnapshot cpu = kernel::desktop::cpu_telemetry_snapshot();
    const uint8_t cpuPct = cpu.available ? static_cast<uint8_t>(clampInt(cpu.utilizationPct, 0, 100)) : 0;
    m_cpuHistory[m_cpuHistoryHead] = cpuPct;
    m_cpuHistoryHead = (m_cpuHistoryHead + 1) % kHistoryMax;
    if (m_cpuHistoryCount < kHistoryMax) ++m_cpuHistoryCount;

    const uint64_t heapTotal = gxos_kernel_heap_total_bytes();
    const uint64_t heapUsed = gxos_kernel_heap_used_bytes();
    const uint8_t heapPct = heapTotal > 0 ? static_cast<uint8_t>(((heapUsed * 100ULL) / heapTotal) > 100ULL ? 100ULL : ((heapUsed * 100ULL) / heapTotal)) : 0;
    m_heapHistory[m_heapHistoryHead] = heapPct;
    m_heapHistoryHead = (m_heapHistoryHead + 1) % kHistoryMax;
    if (m_heapHistoryCount < kHistoryMax) ++m_heapHistoryCount;

    if (shouldInvalidate) {
        invalidate();
    }
}

void TaskManagerApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    drawRoundedPanel(x + 10, y + 10, w - 20, h - 20, rgb(24, 26, 31), rgb(54, 60, 74));
    appDrawText(x + 18, y + 14, "Task Manager", rgb(240, 244, 250));

    const uint32_t tabY = y + 34;
    const uint32_t tabH = 28;
    const uint32_t tabW = (w - 28) / 4;
    const char* tabLabels[] = { "Processes", "Performance", "Tombstoned", "Memory Details" };
    for (int i = 0; i < 4; ++i) {
        drawTabButton(x + 14 + i * tabW, tabY, tabW - 4, tabH, tabLabels[i], i == m_activeTab);
    }

    const uint32_t contentX = x + 14;
    const uint32_t contentY = y + 68;
    const uint32_t contentW = w - 28;
    const uint32_t contentH = h - 118;

    if (m_activeTab == 0) {
        drawRoundedPanel(contentX, contentY, contentW, contentH, rgb(27, 29, 35), rgb(60, 68, 82));
        const uint32_t headerH = 24;
        framebuffer::fill_rect(contentX + 1, contentY + 1, contentW - 2, headerH, rgb(40, 44, 54));
        appDrawText(contentX + 12, contentY + 7, "Application", rgb(226, 232, 242));
        appDrawText(contentX + contentW - 112, contentY + 7, "Windows", rgb(226, 232, 242));
        appDrawText(contentX + contentW - 56, contentY + 7, "Status", rgb(226, 232, 242));

        const uint32_t rowH = 24;
        const uint32_t listTop = contentY + headerH + 2;
        for (int i = 0; i < m_entryCount && (uint32_t)i * rowH < contentH - 80; ++i) {
            const uint32_t rowY = listTop + i * rowH;
            if (i == m_selectedApp) {
                framebuffer::fill_rect(contentX + 1, rowY, contentW - 2, rowH - 1, rgb(48, 64, 90));
            } else if (i % 2 == 0) {
                framebuffer::fill_rect(contentX + 1, rowY, contentW - 2, rowH - 1, rgb(31, 34, 41));
            }

            appDrawText(contentX + 12, rowY + 5, m_entries[i].name, rgb(236, 240, 248));
            char windowsText[8];
            int_to_text(m_entries[i].windowCount, windowsText, sizeof(windowsText));
            appDrawText(contentX + contentW - 108, rowY + 5, windowsText, rgb(212, 218, 228));
            const char* status = m_entries[i].running ? "Running" : "Stopped";
            uint32_t statusColor = m_entries[i].running ? rgb(84, 185, 110) : rgb(180, 88, 88);
            framebuffer::fill_rect(contentX + contentW - 58, rowY + 7, 8, 8, statusColor);
            appDrawText(contentX + contentW - 44, rowY + 5, status, rgb(212, 218, 228));
        }

        const uint32_t detailY = contentY + contentH - 92;
        framebuffer::fill_rect(contentX + 1, detailY, contentW - 2, 1, rgb(68, 74, 88));
        char detailLine[160];
        if (m_selectedApp >= 0 && m_selectedApp < m_entryCount) {
            strcopy(detailLine, "Selected: ", sizeof(detailLine));
            strappend(detailLine, m_entries[m_selectedApp].name, sizeof(detailLine));
            strappend(detailLine, " | Windows ", sizeof(detailLine));
            char countText[16];
            int_to_text(m_entries[m_selectedApp].windowCount, countText, sizeof(countText));
            strappend(detailLine, countText, sizeof(detailLine));
            strappend(detailLine, " | ", sizeof(detailLine));
            strappend(detailLine, m_entries[m_selectedApp].running ? "Running" : "Stopped", sizeof(detailLine));
        } else {
            strcopy(detailLine, "Selected: N/A", sizeof(detailLine));
        }
        appDrawText(contentX + 12, detailY + 12, detailLine, rgb(200, 206, 218));
        appDrawText(contentX + 12, detailY + 30, "Bare-metal view: processes are kernel apps and shell windows.", rgb(156, 164, 178));
        appDrawText(contentX + 12, detailY + 48, "Refresh updates the list; End Task closes the selected app.", rgb(156, 164, 178));
    } else if (m_activeTab == 1) {
        drawRoundedPanel(contentX, contentY, contentW, contentH, rgb(27, 29, 35), rgb(60, 68, 82));
        appDrawText(contentX + 12, contentY + 10, "Performance", rgb(240, 244, 250));

        const kernel::desktop::CpuTelemetrySnapshot cpu = kernel::desktop::cpu_telemetry_snapshot();
        const int runningApps = app::AppManager::getRunningAppCount();
        const int windowCount = compositor::KernelCompositor::getWindowCount();
        const uint64_t heapTotal = gxos_kernel_heap_total_bytes();
        const uint64_t heapUsed = gxos_kernel_heap_used_bytes();
        const uint64_t heapFree = gxos_kernel_heap_free_bytes();
        const int heapPct = heapTotal > 0 ? static_cast<int>((heapUsed * 100ULL) / heapTotal) : 0;

        drawHistoryGraph(contentX + 10, contentY + 34, 160, 84, m_cpuHistory, m_cpuHistoryCount, m_cpuHistoryHead, rgb(96, 163, 228));
        drawHistoryGraph(contentX + 10, contentY + 126, 160, 84, m_heapHistory, m_heapHistoryCount, m_heapHistoryHead, rgb(96, 196, 126));
        drawRoundedPanel(contentX + 184, contentY + 34, contentW - 196, contentH - 46, rgb(24, 26, 31), rgb(58, 64, 78));

        appDrawText(contentX + 198, contentY + 44, "CPU", rgb(236, 240, 248));
        appDrawText(contentX + 198, contentY + 104, "Apps", rgb(236, 240, 248));
        appDrawText(contentX + 198, contentY + 164, "Windows", rgb(236, 240, 248));
        appDrawText(contentX + 198, contentY + 224, "Heap", rgb(236, 240, 248));

        char cpuPctText[16];
        char cpuWinText[24];
        int_to_text(cpu.available ? cpu.utilizationPct : 0, cpuPctText, sizeof(cpuPctText));
        uint64_to_text(cpu.sampleWindowMs, cpuWinText, sizeof(cpuWinText));
        char appCountText[16];
        char windowCountText[16];
        char heapUsedText[32];
        char heapTotalText[32];
        char heapFreeText[32];
        char heapPctText[16];
        int_to_text(runningApps, appCountText, sizeof(appCountText));
        int_to_text(windowCount, windowCountText, sizeof(windowCountText));
        uint64_to_text(heapUsed, heapUsedText, sizeof(heapUsedText));
        uint64_to_text(heapTotal, heapTotalText, sizeof(heapTotalText));
        uint64_to_text(heapFree, heapFreeText, sizeof(heapFreeText));
        int_to_text(heapPct, heapPctText, sizeof(heapPctText));

        appDrawText(contentX + 280, contentY + 44, cpu.available ? cpuPctText : "N/A", rgb(96, 163, 228));
        appDrawText(contentX + 340, contentY + 44, cpu.available ? "percent" : "warmup", rgb(160, 166, 176));
        appDrawText(contentX + 280, contentY + 62, "Window: ", rgb(160, 166, 176));
        appDrawText(contentX + 340, contentY + 62, cpuWinText, rgb(212, 218, 228));
        appDrawText(contentX + 198, contentY + 122, appCountText, rgb(96, 196, 126));
        appDrawText(contentX + 198, contentY + 182, windowCountText, rgb(230, 173, 74));
        appDrawText(contentX + 198, contentY + 242, heapUsedText, rgb(232, 236, 244));
        appDrawText(contentX + 280, contentY + 242, "used of ", rgb(160, 166, 176));
        appDrawText(contentX + 340, contentY + 242, heapTotalText, rgb(212, 218, 228));
        appDrawText(contentX + 280, contentY + 260, heapFreeText, rgb(160, 166, 176));
        appDrawText(contentX + 340, contentY + 260, "free", rgb(160, 166, 176));
        appDrawText(contentX + 280, contentY + 278, heapPctText, rgb(96, 196, 126));
        appDrawText(contentX + 340, contentY + 278, "% heap used", rgb(160, 166, 176));

        appDrawText(contentX + 198, contentY + contentH - 44, cpu.source ? cpu.source : "N/A", rgb(160, 166, 176));
        appDrawText(contentX + 198, contentY + contentH - 26, "Kernel heap is a 1 MB bump allocator.", rgb(160, 166, 176));
    } else if (m_activeTab == 2) {
        drawRoundedPanel(contentX, contentY, contentW, contentH, rgb(27, 29, 35), rgb(60, 68, 82));
        appDrawText(contentX + 12, contentY + 10, "Tombstoned", rgb(240, 244, 250));
        appDrawText(contentX + 12, contentY + 46, "Bare-metal build does not collect app tombstones yet.", rgb(218, 222, 232));
        appDrawText(contentX + 12, contentY + 66, "Use the hosted/server Task Manager for tombstone policy details.", rgb(160, 166, 176));
        drawRoundedPanel(contentX + 12, contentY + 100, contentW - 24, 110, rgb(22, 24, 29), rgb(58, 64, 78));
        appDrawText(contentX + 24, contentY + 118, "Available fields:", rgb(236, 240, 248));
        appDrawText(contentX + 24, contentY + 138, "Name, PID, reason, exit code, runtime", rgb(160, 166, 176));
        appDrawText(contentX + 24, contentY + 156, "App ID-backed tombstones are implemented in the hosted build.", rgb(160, 166, 176));
    } else {
        drawRoundedPanel(contentX, contentY, contentW, contentH, rgb(27, 29, 35), rgb(60, 68, 82));
        appDrawText(contentX + 12, contentY + 10, "Memory Details", rgb(240, 244, 250));

        const uint64_t heapTotal = gxos_kernel_heap_total_bytes();
        const uint64_t heapUsed = gxos_kernel_heap_used_bytes();
        const uint64_t heapFree = gxos_kernel_heap_free_bytes();
        const int heapPct = heapTotal > 0 ? static_cast<int>((heapUsed * 100ULL) / heapTotal) : 0;

        drawHistoryGraph(contentX + 12, contentY + 34, 220, 88, m_heapHistory, m_heapHistoryCount, m_heapHistoryHead, rgb(96, 196, 126));
        drawRoundedPanel(contentX + 248, contentY + 34, contentW - 260, contentH - 46, rgb(24, 26, 31), rgb(58, 64, 78));

        char heapUsedText[32];
        char heapFreeText[32];
        char heapTotalText[32];
        char heapPctText[16];
        uint64_to_text(heapUsed, heapUsedText, sizeof(heapUsedText));
        uint64_to_text(heapFree, heapFreeText, sizeof(heapFreeText));
        uint64_to_text(heapTotal, heapTotalText, sizeof(heapTotalText));
        int_to_text(heapPct, heapPctText, sizeof(heapPctText));

        appDrawText(contentX + 262, contentY + 46, "Total heap:", rgb(236, 240, 248));
        appDrawText(contentX + 262, contentY + 66, heapTotalText, rgb(212, 218, 228));
        appDrawText(contentX + 262, contentY + 96, "Used:", rgb(236, 240, 248));
        appDrawText(contentX + 262, contentY + 116, heapUsedText, rgb(212, 218, 228));
        appDrawText(contentX + 262, contentY + 146, "Free:", rgb(236, 240, 248));
        appDrawText(contentX + 262, contentY + 166, heapFreeText, rgb(212, 218, 228));
        appDrawText(contentX + 262, contentY + 196, "Utilization:", rgb(236, 240, 248));
        appDrawText(contentX + 262, contentY + 216, heapPctText, rgb(96, 196, 126));
        appDrawText(contentX + 282, contentY + 216, "%", rgb(96, 196, 126));
        appDrawText(contentX + 262, contentY + 250, "Heap is a kernel bump allocator; allocations are not freed.", rgb(160, 166, 176));
        appDrawText(contentX + 262, contentY + 270, "That makes the total fixed and the used value monotonic.", rgb(160, 166, 176));
    }

    // Bottom status strip. The real controls are the widget buttons created in init().
    framebuffer::fill_rect(x + 14, y + h - 54, w - 28, 1, rgb(70, 76, 90));
    uint32_t bottomY = y + h - 42;
    char cpuLine[128];
    const kernel::desktop::CpuTelemetrySnapshot cpu = kernel::desktop::cpu_telemetry_snapshot();
    if (cpu.available) {
        char pctBuf[16];
        char windowBuf[24];
        int_to_text(cpu.utilizationPct, pctBuf, sizeof(pctBuf));
        strappend(pctBuf, "%", sizeof(pctBuf));
        uint64_to_text(cpu.sampleWindowMs, windowBuf, sizeof(windowBuf));
        strcopy(cpuLine, "CPU: ", sizeof(cpuLine));
        strappend(cpuLine, pctBuf, sizeof(cpuLine));
        strappend(cpuLine, " | Window: ", sizeof(cpuLine));
        strappend(cpuLine, windowBuf, sizeof(cpuLine));
        strappend(cpuLine, " ms", sizeof(cpuLine));
    } else {
        char windowBuf[24];
        uint64_to_text(cpu.sampleWindowMs, windowBuf, sizeof(windowBuf));
        strcopy(cpuLine, "CPU: N/A | Window: ", sizeof(cpuLine));
        strappend(cpuLine, windowBuf, sizeof(cpuLine));
        strappend(cpuLine, " ms", sizeof(cpuLine));
    }
    appDrawText(x + 230, bottomY + 8, cpuLine, rgb(180, 200, 220));
    appDrawText(x + 230, bottomY + 18, cpu.source ? cpu.source : "N/A", rgb(160, 170, 185));
    appDrawText(x + w - 196, bottomY + 8, "Use the task buttons below the strip.", rgb(160, 170, 185));
}

void TaskManagerApp::onMouseDown(int localX, int localY, uint8_t button) {
    (void)button;
    
    if (localY >= 34 && localY < 62) {
        const int tabWidth = (m_window->w - 28) / 4;
        const int relX = localX - 14;
        if (relX >= 0) {
            const int tabIndex = relX / tabWidth;
            if (tabIndex >= 0 && tabIndex < 4) {
                m_activeTab = tabIndex;
                invalidate();
                return;
            }
        }
    }

    // Check if clicked in list area
    const int listTop = 94;
    const int listBottom = m_window ? (int)m_window->h - 54 : 404;
    if (m_activeTab == 0 && localY >= listTop && localY < listBottom) {
        int row = (localY - listTop) / 24;
        if (row >= 0 && row < m_entryCount) {
            m_selectedApp = row;
            invalidate();
        }
    }
}

void TaskManagerApp::onWidgetClick(int widgetId) {
    if (widgetId == m_refreshBtnId) {
        refreshList();
        invalidate();
    } else if (widgetId == m_endTaskBtnId) {
        if (m_selectedApp >= 0 && m_selectedApp < m_entryCount) {
            if (m_entries[m_selectedApp].isShell) {
                shell::close();
                m_selectedApp = -1;
                refreshList();
                invalidate();
            } else {
                // Find and close the app
                app::KernelApp* app = app::AppManager::getRunningApp(m_selectedApp);
                if (app && app != this) {  // Don't close self
                    app::AppManager::closeApp(app);
                    m_selectedApp = -1;
                    refreshList();
                    invalidate();
                }
            }
        }
    }
}

void TaskManagerApp::refreshList() {
    m_entryCount = 0;
    
    // Add running apps
    int count = app::AppManager::getRunningAppCount();
    for (int i = 0; i < count && m_entryCount < MAX_ENTRIES; i++) {
        app::KernelApp* runApp = app::AppManager::getRunningApp(i);
        if (runApp) {
            strcopy(m_entries[m_entryCount].name, runApp->getName(), app::MAX_APP_NAME);
            m_entries[m_entryCount].running = true;
            m_entries[m_entryCount].windowCount = 1;
            m_entries[m_entryCount].isShell = false;
            m_entryCount++;
        }
    }
    
    // Add shell if open
    if (shell::is_open() && m_entryCount < MAX_ENTRIES) {
        strcopy(m_entries[m_entryCount].name, "Terminal", app::MAX_APP_NAME);
        m_entries[m_entryCount].running = true;
        m_entries[m_entryCount].windowCount = 1;
        m_entries[m_entryCount].isShell = true;
        m_entryCount++;
    }
    
    // Validate selection
    if (m_selectedApp >= m_entryCount) {
        m_selectedApp = m_entryCount - 1;
    }
}

// ============================================================
// FileExplorerApp Implementation
// ============================================================

static const int kFileExplorerListHeaderH = 24;
static const int kFileExplorerListStatusH = 22;
static const int kFileExplorerScrollbarW = 8;
static const int kFileExplorerScrollbarPad = 2;
static const int kFileExplorerScrollbarMinThumbH = 18;

static bool fileExplorerPrepareStartPath(const char* startPath, char* resolvedPath, int resolvedPathSize, const char* contextLabel)
{
    if (!resolvedPath || resolvedPathSize <= 0) return false;

    const char* requestedPath = (startPath && startPath[0]) ? startPath : "/";
    char normalized[vfs::VFS_MAX_PATH];
    vfs::normalize_path(requestedPath, normalized, sizeof(normalized));
    if (!normalized[0]) {
        serial::puts("[fileexplorer-bm] ");
        serial::puts(contextLabel ? contextLabel : "launch");
        serial::puts(" rejected: invalid path\n");
        return false;
    }

    vfs::FileInfo info{};
    if (vfs::stat(normalized, &info) != vfs::VFS_OK) {
        if (vfs::mkdir(normalized) != vfs::VFS_OK && vfs::stat(normalized, &info) != vfs::VFS_OK) {
            serial::puts("[fileexplorer-bm] ");
            serial::puts(contextLabel ? contextLabel : "launch");
            serial::puts(" rejected: folder missing and could not be created\n");
            return false;
        }
    }

    if (vfs::stat(normalized, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_DIRECTORY) {
        serial::puts("[fileexplorer-bm] ");
        serial::puts(contextLabel ? contextLabel : "launch");
        serial::puts(" rejected: target is not a directory\n");
        return false;
    }

    strcopy(resolvedPath, normalized, resolvedPathSize);
    return true;
}

FileExplorerApp::FileExplorerApp()
    : m_entryCount(0), m_selected(0), m_scroll(0),
      m_lastClickIndex(-1), m_lastClickTick(0),
      m_backBtnId(-1), m_upBtnId(-1), m_refreshBtnId(-1), m_rootBtnId(-1),
      m_createFolderBtnId(-1),
      m_renameFileBtnId(-1), m_deleteFileBtnId(-1), m_renameFolderBtnId(-1), m_deleteFolderBtnId(-1),
      m_confirmDeleteBtnId(-1), m_cancelDeleteBtnId(-1), m_renamePrompt(false), m_deleteConfirm(false),
      m_deleteTargetIsDir(false), m_lastFileOperationGeneration(0) {
    strcopy(m_name, "Files", app::MAX_APP_NAME);
    strcopy(m_currentPath, "/", MAX_PATH_LEN);
    strcopy(m_status, "Ready", sizeof(m_status));
    m_renameValue[0] = '\0';
    m_createFolderPrompt = false;
    m_deleteTarget[0] = '\0';
    m_deleteTargetName[0] = '\0';
    m_contextMenuOpen = false;
    m_contextMenuX = 0;
    m_contextMenuY = 0;
    m_contextMenuHover = -1;
    m_contextMenuPasteVisible = false;
    m_contextMenuCreateFolderVisible = false;
    m_contextMenuTarget = ContextMenuTarget::Entry;
    m_propertiesOpen = false;
    m_propertiesIsDir = false;
    m_propertiesName[0] = '\0';
    m_propertiesPath[0] = '\0';
    m_propertiesType[0] = '\0';
    m_propertiesSize[0] = '\0';
    m_propertiesModified[0] = '\0';
    m_propertiesIcon[0] = '\0';
}

FileExplorerApp::~FileExplorerApp() {
}

bool FileExplorerApp::textEquals(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

bool FileExplorerApp::endsWithIgnoreCase(const char* value, const char* suffix) {
    if (!value || !suffix) return false;
    int valueLen = strlen_local(value);
    int suffixLen = strlen_local(suffix);
    if (suffixLen > valueLen) return false;
    for (int i = 0; i < suffixLen; ++i) {
        char a = value[valueLen - suffixLen + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

const uint32_t* FileExplorerApp::getEmbeddedIconPixels(const char* logicalName) {
    if (textEquals(logicalName, "app.notepad"))       return kDesktopThemeIcon_Notepad;
    if (textEquals(logicalName, "app.calculator"))    return kDesktopThemeIcon_Calculator;
    if (textEquals(logicalName, "app.console"))       return kDesktopThemeIcon_Console;
    if (textEquals(logicalName, "trash.empty"))       return kDesktopThemeIcon_TrashEmpty;
    if (textEquals(logicalName, "trash.full"))        return kDesktopThemeIcon_TrashFull;
    if (textEquals(logicalName, "app.taskmanager"))   return kDesktopThemeIcon_TaskManager;
    if (textEquals(logicalName, "app.files"))         return kDesktopThemeIcon_Files;
    if (textEquals(logicalName, "app.paint"))         return kDesktopThemeIcon_Paint;
    if (textEquals(logicalName, "app.clock"))         return kDesktopThemeIcon_Clock;
    if (textEquals(logicalName, "file.folder"))       return kDesktopThemeIcon_Files;
    if (textEquals(logicalName, "file.sysfolder"))    return kDesktopThemeIcon_Files;
    if (textEquals(logicalName, "file.text"))         return kDesktopThemeIcon_Notepad;
    if (textEquals(logicalName, "file.image"))        return kDesktopThemeIcon_Paint;
    if (textEquals(logicalName, "file.binary"))       return kDesktopThemeIcon_FileGeneric;
    if (textEquals(logicalName, "file.generic"))      return kDesktopThemeIcon_FileGeneric;
    if (textEquals(logicalName, "file.unknown"))      return kDesktopThemeIcon_FileGeneric;
    if (textEquals(logicalName, "drive.fixed"))       return kDesktopThemeIcon_Files;
    if (textEquals(logicalName, "drive.mounted"))     return kDesktopThemeIcon_Files;
    if (textEquals(logicalName, "place.computer"))    return kDesktopThemeIcon_Files;
    return kDesktopThemeIcon_FileGeneric;
}

bool FileExplorerApp::drawArgbIconBuffer(const uint32_t* pixels, uint32_t srcW, uint32_t srcH, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!pixels || srcW == 0 || srcH == 0 || width == 0 || height == 0) return false;
    bool drewPixel = false;
    for (uint32_t dy = 0; dy < height; ++dy) {
        uint32_t sy = (uint32_t)((uint64_t)dy * (uint64_t)srcH / height);
        for (uint32_t dx = 0; dx < width; ++dx) {
            uint32_t sx = (uint32_t)((uint64_t)dx * (uint64_t)srcW / width);
            uint32_t src = pixels[sy * srcW + sx];
            uint8_t a = (uint8_t)((src >> 24) & 0xFF);
            if (a == 0) continue;
            drewPixel = true;
            uint32_t px = x + dx;
            uint32_t py = y + dy;
            if (a == 0xFF) {
                framebuffer::put_pixel(px, py, src);
            } else {
                uint32_t dst = framebuffer::get_pixel(px, py);
                uint8_t sr = (uint8_t)((src >> 16) & 0xFF);
                uint8_t sg = (uint8_t)((src >> 8) & 0xFF);
                uint8_t sb = (uint8_t)(src & 0xFF);
                uint8_t dr = (uint8_t)((dst >> 16) & 0xFF);
                uint8_t dg = (uint8_t)((dst >> 8) & 0xFF);
                uint8_t db = (uint8_t)(dst & 0xFF);
                uint8_t r = (uint8_t)((sr * a + dr * (255 - a)) / 255);
                uint8_t g = (uint8_t)((sg * a + dg * (255 - a)) / 255);
                uint8_t b = (uint8_t)((sb * a + db * (255 - a)) / 255);
                framebuffer::put_pixel(px, py, 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b);
            }
        }
    }
    return drewPixel;
}

bool FileExplorerApp::drawThemedIcon(uint32_t x, uint32_t y, uint32_t size, const char* logicalName) {
    const uint32_t* pixels = getEmbeddedIconPixels(logicalName);
    if (!pixels) return false;
    return drawArgbIconBuffer(pixels, kDesktopThemeIconW, kDesktopThemeIconH, x, y, size, size);
}

void FileExplorerApp::drawPlaceholderIcon(uint32_t x, uint32_t y, uint32_t size) {
    framebuffer::fill_rect(x, y, size, size, rgb(180, 40, 40));
    framebuffer::fill_rect(x + 1, y + 1, size > 2 ? size - 2 : size, size > 2 ? size - 2 : size, rgb(255, 255, 255));
}

const char* FileExplorerApp::fileLogicalIcon(const Entry& entry) const {
    if (entry.isDir) return "file.folder";
    if (endsWithIgnoreCase(entry.name, ".txt") || endsWithIgnoreCase(entry.name, ".log") || endsWithIgnoreCase(entry.name, ".cfg") || endsWithIgnoreCase(entry.name, ".ini") || endsWithIgnoreCase(entry.name, ".md")) return "file.text";
    if (endsWithIgnoreCase(entry.name, ".bmp") || endsWithIgnoreCase(entry.name, ".png") || endsWithIgnoreCase(entry.name, ".jpg") || endsWithIgnoreCase(entry.name, ".jpeg") || endsWithIgnoreCase(entry.name, ".gif")) return "file.image";
    if (endsWithIgnoreCase(entry.name, ".elf") || endsWithIgnoreCase(entry.name, ".gxapp") || endsWithIgnoreCase(entry.name, ".gxq") || endsWithIgnoreCase(entry.name, ".exe")) return "app.files";
    if (endsWithIgnoreCase(entry.name, ".img")) return "drive.mounted";
    if (endsWithIgnoreCase(entry.name, ".bin") || endsWithIgnoreCase(entry.name, ".dat") || endsWithIgnoreCase(entry.name, ".dll") || endsWithIgnoreCase(entry.name, ".so") || endsWithIgnoreCase(entry.name, ".o")) return "file.binary";
    return "file.unknown";
}

bool FileExplorerApp::init() {
    return initWithParam("/");
}

bool FileExplorerApp::initWithParam(const char* startPath) {
    bool launchDeleteConfirmation = false;
    bool launchDeleteIsDir = false;
    char launchDeletePath[MAX_PATH_LEN] = {0};
    char requestedStartPath[MAX_PATH_LEN + 32] = {0};
    strcopy(requestedStartPath, startPath && startPath[0] ? startPath : "/", sizeof(requestedStartPath));
    const char* deletePrefix = "--confirm-delete|";
    if (startsWithText(requestedStartPath, deletePrefix)) {
        const int prefixLength = strlen_local(deletePrefix);
        const char* payload = requestedStartPath + prefixLength;
        int separator = -1;
        for (int i = 0; payload[i]; ++i) {
            if (payload[i] == '|') {
                separator = i;
                break;
            }
        }
        if (separator > 0 && payload[separator + 1] != '\0') {
            for (int i = 0; i < separator && i < MAX_PATH_LEN - 1; ++i) launchDeletePath[i] = payload[i];
            launchDeletePath[separator < MAX_PATH_LEN ? separator : MAX_PATH_LEN - 1] = '\0';
            char normalizedDeletePath[MAX_PATH_LEN] = {0};
            vfs::normalize_path(launchDeletePath, normalizedDeletePath, sizeof(normalizedDeletePath));
            strcopy(launchDeletePath, normalizedDeletePath, sizeof(launchDeletePath));
            launchDeleteIsDir = payload[separator + 1] == '1';
            launchDeleteConfirmation = launchDeletePath[0] != '\0';
            parentPath(launchDeletePath, requestedStartPath, sizeof(requestedStartPath));
        }
    }
    char resolvedStartPath[MAX_PATH_LEN];
    if (!fileExplorerPrepareStartPath(requestedStartPath, resolvedStartPath, sizeof(resolvedStartPath), "launch")) {
        return false;
    }

    m_window = new app::KernelWindow();
    strcopy(m_window->title, "File Explorer", app::MAX_TITLE_LEN);
    m_window->x = 80;
    m_window->y = 45;
    m_window->w = 760;
    m_window->h = 460;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_RESIZABLE | app::WF_FOCUSED;
    m_window->owner = this;

    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }

    m_backBtnId = addButton(8, 5, 52, 20, "Root");
    m_upBtnId = addButton(66, 5, 38, 20, "Up");
    m_refreshBtnId = addButton(108, 5, 58, 20, "Refresh");
    m_rootBtnId = addButton(170, 5, 70, 20, "Mounts");
    m_createFolderBtnId = addButton(436, 5, 94, 20, "New Folder");
    m_renameFileBtnId = addButton(248, 5, 82, 20, "Rename File");
    m_deleteFileBtnId = addButton(334, 5, 78, 20, "Delete File");
    m_renameFolderBtnId = addButton(248, 5, 92, 20, "Rename Dir");
    m_deleteFolderBtnId = addButton(344, 5, 84, 20, "Delete Dir");
    m_confirmDeleteBtnId = addButton(260, 205, 92, 20, "Move");
    m_cancelDeleteBtnId = addButton(344, 205, 70, 20, "Cancel");

    strcopy(m_currentPath, resolvedStartPath, MAX_PATH_LEN);
    refresh();
    if (launchDeleteConfirmation) {
        strcopy(m_deleteTarget, launchDeletePath, sizeof(m_deleteTarget));
        strcopy(m_deleteTargetName, vfs::basename(launchDeletePath), sizeof(m_deleteTargetName));
        m_deleteTargetIsDir = launchDeleteIsDir;
        m_deleteConfirm = true;
        setStatus("Confirm delete");
    }
    updateActionButtons();
    m_lastFileOperationGeneration = file_clipboard::operation_generation();
    m_state = app::AppState::Running;
    return true;
}

void FileExplorerApp::shutdown() {
    m_state = app::AppState::Terminated;
}

void FileExplorerApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const uint64_t fileOperationGeneration = file_clipboard::operation_generation();
    if (!file_clipboard::operation_active() &&
        fileOperationGeneration != m_lastFileOperationGeneration) {
        m_lastFileOperationGeneration = fileOperationGeneration;
        refresh();
        updateActionButtons();
    }

    static const uint32_t kIconSize = 16;
    framebuffer::fill_rect(x, y, w, h, rgb(246, 246, 246));

    uint32_t toolbarIconY = y + 7;
    drawThemedIcon(x + 10, toolbarIconY, kIconSize, "place.computer");
    drawThemedIcon(x + 68, toolbarIconY, kIconSize, "file.folder");
    drawThemedIcon(x + 110, toolbarIconY, kIconSize, "file.binary");
    drawThemedIcon(x + 172, toolbarIconY, kIconSize, "drive.mounted");

    uint32_t addressY = y + TOOLBAR_H;
    framebuffer::fill_rect(x, addressY, w, ADDRESS_H, rgb(255, 255, 255));
    appDrawText(x + 8, addressY + 7, "Address:", rgb(70, 70, 70));
    appDrawText(x + 62, addressY + 7, m_currentPath, rgb(30, 30, 30));

    uint32_t bodyY = y + TOOLBAR_H + ADDRESS_H;
    uint32_t statusH = 22;
    uint32_t bodyH = h > TOOLBAR_H + ADDRESS_H + statusH ? h - TOOLBAR_H - ADDRESS_H - statusH : 0;

    framebuffer::fill_rect(x, bodyY, LEFT_W, bodyH, rgb(238, 238, 238));
    appDrawText(x + 8, bodyY + 10, "Navigation", rgb(40, 40, 40));
    if (!drawThemedIcon(x + 10, bodyY + 26, kIconSize, "place.computer")) drawPlaceholderIcon(x + 10, bodyY + 26, kIconSize);
    appDrawText(x + 30, bodyY + 30, "Root", rgb(50, 70, 110));
    if (!drawThemedIcon(x + 10, bodyY + 42, kIconSize, "drive.fixed")) drawPlaceholderIcon(x + 10, bodyY + 42, kIconSize);
    appDrawText(x + 30, bodyY + 46, "Mounted drives", rgb(50, 70, 110));

    uint8_t mountCount = vfs::mount_count();
    if (mountCount == 0) {
        appDrawText(x + 18, bodyY + 64, "No mounts", rgb(130, 60, 60));
    } else {
        int row = 0;
        for (uint8_t i = 0; i < vfs::VFS_MAX_MOUNTS && row < 8; ++i) {
            const vfs::MountPoint* mp = vfs::get_mount_by_index(i);
            if (!mp || !mp->active) continue;
            if (!drawThemedIcon(x + 10, bodyY + 60 + row * ROW_H, kIconSize, "drive.mounted")) drawPlaceholderIcon(x + 10, bodyY + 60 + row * ROW_H, kIconSize);
            appDrawText(x + 30, bodyY + 64 + row * ROW_H, mp->path, rgb(30, 30, 30));
            row++;
        }
    }

    uint32_t mainX = x + LEFT_W;
    uint32_t mainW = w > LEFT_W ? w - LEFT_W : 0;
    framebuffer::fill_rect(mainX, bodyY, mainW, bodyH, rgb(255, 255, 255));
    framebuffer::fill_rect(mainX, bodyY, mainW, 22, rgb(230, 230, 230));
    appDrawText(mainX + 8, bodyY + 7, "Name", rgb(40, 40, 40));
    appDrawText(mainX + 250, bodyY + 7, "Size", rgb(40, 40, 40));
    appDrawText(mainX + 330, bodyY + 7, "Type", rgb(40, 40, 40));
    appDrawText(mainX + 430, bodyY + 7, "Modified", rgb(40, 40, 40));

    if (m_entryCount == 0) {
        appDrawText(mainX + 8, bodyY + 34, "Empty directory or unavailable path", rgb(120, 120, 120));
    }

    int visibleRows = visibleRowCount();
    int start = m_scroll;
    int maxScroll = maxScrollRows();
    if (start < 0) start = 0;
    if (start > maxScroll) start = maxScroll;
    int end = start + visibleRows;
    if (end > m_entryCount) end = m_entryCount;

    for (int i = 0; i < end - start; ++i) {
        int entryIndex = start + i;
        Entry& e = m_entries[entryIndex];
        uint32_t rowY = bodyY + kFileExplorerListHeaderH + i * ROW_H;
        if (entryIndex == m_selected) {
            framebuffer::fill_rect(mainX + 1, rowY - 2, mainW - 2, ROW_H, rgb(200, 220, 245));
        }

        char sizeText[24];
        formatSize(e.size, sizeText, sizeof(sizeText));
        const char* logicalIcon = fileLogicalIcon(e);
        if (!drawThemedIcon(mainX + 8, rowY - 2, kIconSize, logicalIcon)) drawPlaceholderIcon(mainX + 8, rowY - 2, kIconSize);
        appDrawText(mainX + 30, rowY, e.name, rgb(20, 20, 20));
        appDrawText(mainX + 250, rowY, e.isDir ? "" : sizeText, rgb(70, 70, 70));
        appDrawText(mainX + 330, rowY, fileType(e), rgb(70, 70, 70));
        appDrawText(mainX + 430, rowY, "--", rgb(110, 110, 110));
    }

    if (isScrollbarVisible()) {
        uint32_t sbX = x + (uint32_t)scrollbarLeft();
        uint32_t sbY = y + (uint32_t)scrollbarTrackTop();
        uint32_t sbH = (uint32_t)scrollbarTrackHeight();
        uint32_t thumbY = y + (uint32_t)scrollbarThumbTop();
        uint32_t thumbH = (uint32_t)scrollbarThumbHeight();
        framebuffer::fill_rect(sbX, sbY, kFileExplorerScrollbarW, sbH, rgb(236, 238, 242));
        framebuffer::fill_rect(sbX, thumbY, kFileExplorerScrollbarW, thumbH, rgb(150, 160, 176));
        framebuffer::fill_rect(sbX, sbY, kFileExplorerScrollbarW, 1, rgb(208, 212, 220));
        framebuffer::fill_rect(sbX, sbY + sbH - 1, kFileExplorerScrollbarW, 1, rgb(208, 212, 220));
    }

    framebuffer::fill_rect(x, y + h - statusH, w, statusH, rgb(235, 235, 235));
    appDrawText(x + 8, y + h - 15, m_status, rgb(40, 40, 40));

    if (m_contextMenuOpen) {
        const int kContextMenuItems = contextMenuItemCount();
        framebuffer::fill_rect(x + m_contextMenuX, y + m_contextMenuY, CONTEXT_MENU_W, CONTEXT_MENU_ITEM_H * kContextMenuItems + 2, rgb(245, 245, 248));
        framebuffer::fill_rect(x + m_contextMenuX, y + m_contextMenuY, CONTEXT_MENU_W, 1, rgb(120, 120, 140));
        framebuffer::fill_rect(x + m_contextMenuX, y + m_contextMenuY + CONTEXT_MENU_ITEM_H * kContextMenuItems + 1, CONTEXT_MENU_W, 1, rgb(120, 120, 140));
        framebuffer::fill_rect(x + m_contextMenuX, y + m_contextMenuY, 1, CONTEXT_MENU_ITEM_H * kContextMenuItems + 2, rgb(120, 120, 140));
        framebuffer::fill_rect(x + m_contextMenuX + CONTEXT_MENU_W - 1, y + m_contextMenuY, 1, CONTEXT_MENU_ITEM_H * kContextMenuItems + 2, rgb(120, 120, 140));
        for (int i = 0; i < kContextMenuItems; ++i) {
            if (m_contextMenuHover == i) {
                framebuffer::fill_rect(x + m_contextMenuX + 1, y + m_contextMenuY + 1 + i * CONTEXT_MENU_ITEM_H, CONTEXT_MENU_W - 2, CONTEXT_MENU_ITEM_H, rgb(60, 90, 140));
            }
        }
        for (int i = 0; i < kContextMenuItems; ++i) {
            appDrawText(x + m_contextMenuX + 8,
                        y + m_contextMenuY + 6 + CONTEXT_MENU_ITEM_H * i,
                        contextMenuItemLabel(i), rgb(20, 20, 20));
        }
    }

    if (m_renamePrompt) {
        framebuffer::fill_rect(x + 220, y + 165, 360, 92, rgb(245, 245, 250));
        appDrawText(x + 232, y + 182, m_createFolderPrompt ? "Create folder" : "Rename selected item", rgb(30, 30, 30));
        appDrawText(x + 232, y + 205, m_renameValue, rgb(20, 20, 20));
        appDrawText(x + 232, y + 230,
                    m_createFolderPrompt
                        ? "Enter=Create  Esc=Cancel  Backspace=Delete"
                        : "Enter=OK  Esc=Cancel  Backspace=Delete",
                    rgb(80, 80, 80));
    } else if (m_deleteConfirm) {
        framebuffer::fill_rect(x + 220, y + 165, 390, 92, rgb(250, 245, 245));
        appDrawText(x + 232, y + 182, m_deleteTargetIsDir ? "Move this folder to Trash?" : "Move this file to Trash?", rgb(80, 30, 30));
        appDrawText(x + 232, y + 205, m_deleteTargetName, rgb(30, 30, 30));
        appDrawText(x + 232, y + 230, "The item will be moved to Trash.", rgb(80, 80, 80));
    } else if (m_propertiesOpen) {
        framebuffer::fill_rect(x + 200, y + 145, 400, 150, rgb(244, 244, 248));
        framebuffer::fill_rect(x + 200, y + 145, 400, 1, rgb(110, 110, 130));
        framebuffer::fill_rect(x + 200, y + 294, 400, 1, rgb(110, 110, 130));
        framebuffer::fill_rect(x + 200, y + 145, 1, 150, rgb(110, 110, 130));
        framebuffer::fill_rect(x + 599, y + 145, 1, 150, rgb(110, 110, 130));
        if (!drawThemedIcon(x + 216, y + 162, 24, m_propertiesIcon[0] ? m_propertiesIcon : (m_propertiesIsDir ? "file.folder" : "file.unknown"))) {
            drawPlaceholderIcon(x + 216, y + 162, 24);
        }
        appDrawText(x + 248, y + 168, "Properties", rgb(30, 30, 30));
        appDrawText(x + 216, y + 194, "Name:", rgb(70, 70, 70));
        appDrawText(x + 286, y + 194, m_propertiesName, rgb(20, 20, 20));
        appDrawText(x + 216, y + 212, "Type:", rgb(70, 70, 70));
        appDrawText(x + 286, y + 212, m_propertiesType, rgb(20, 20, 20));
        appDrawText(x + 216, y + 230, "Size:", rgb(70, 70, 70));
        appDrawText(x + 286, y + 230, m_propertiesSize, rgb(20, 20, 20));
        appDrawText(x + 216, y + 248, "Path:", rgb(70, 70, 70));
        appDrawText(x + 286, y + 248, m_propertiesPath, rgb(20, 20, 20));
        appDrawText(x + 216, y + 266, "Modified:", rgb(70, 70, 70));
        appDrawText(x + 286, y + 266, m_propertiesModified, rgb(20, 20, 20));
    }
}

void FileExplorerApp::onKeyDown(uint32_t key) {
    if (file_clipboard::operation_active()) return;
    if (m_propertiesOpen) {
        if (key == 27 || key == '\n' || key == '\r') {
            closeProperties();
        }
        return;
    }

    if (m_renamePrompt) {
        if (key == '\n' || key == '\r') {
            if (m_createFolderPrompt) commitCreateFolder();
            else commitRename();
        } else if (key == 27) {
            if (m_createFolderPrompt) cancelCreateFolder();
            else cancelRename();
        } else if (key == '\b') {
            int len = strlen_local(m_renameValue);
            if (len > 0) m_renameValue[len - 1] = '\0';
            invalidate();
        }
        return;
    }

    if (m_deleteConfirm && key == 27) {
        cancelDelete();
        return;
    }

    if (ps2keyboard::is_ctrl_down()) {
        if (key == 'c' || key == 'C') {
            beginCopySelected();
            return;
        }
        if (key == 'x' || key == 'X') {
            beginMoveSelected();
            return;
        }
        if (key == 'v' || key == 'V') {
            pasteClipboard();
            return;
        }
    }

    if (key == shell::KEY_UP) {
        if (m_selected > 0) {
            m_selected--;
            ensureSelectedVisible();
            updateActionButtons();
            invalidate();
        }
    } else if (key == shell::KEY_DOWN) {
        if (m_selected < m_entryCount - 1) {
            m_selected++;
            ensureSelectedVisible();
            updateActionButtons();
            invalidate();
        }
    } else if (key == '\n' || key == '\r') {
        openSelected();
    } else if (key == '\b') {
        goUp();
    } else if (key == shell::KEY_DELETE) {
        showDeleteConfirmation();
    } else if (key == shell::KEY_PGUP) {
        if (m_entryCount <= 0) {
            m_selected = 0;
            m_scroll = 0;
            invalidate();
            return;
        }
        int step = visibleRowCount();
        m_selected -= step;
        if (m_selected < 0) m_selected = 0;
        ensureSelectedVisible();
        updateActionButtons();
        invalidate();
    } else if (key == shell::KEY_PGDN) {
        if (m_entryCount <= 0) {
            m_selected = 0;
            m_scroll = 0;
            invalidate();
            return;
        }
        int step = visibleRowCount();
        m_selected += step;
        if (m_selected >= m_entryCount) m_selected = m_entryCount - 1;
        ensureSelectedVisible();
        updateActionButtons();
        invalidate();
    } else if (key == 'r' || key == 'R') {
        refresh();
        updateActionButtons();
        invalidate();
    } else if (key == 0x111) { // F2
        beginRenameSelected();
    } else if (key == 0x114) { // F5
        refresh();
        updateActionButtons();
        invalidate();
    }
}

void FileExplorerApp::onKeyChar(char c) {
    if (!m_renamePrompt) return;
    if (c >= 32 && c < 127) {
        int len = strlen_local(m_renameValue);
        int maxNameLength = m_createFolderPrompt ? 8 : (int)sizeof(m_renameValue) - 1;
        if (len < maxNameLength && c != '/' && c != '\\' && (!m_createFolderPrompt || c != ' ')) {
            m_renameValue[len] = c;
            m_renameValue[len + 1] = '\0';
        }
        invalidate();
    }
}

void FileExplorerApp::onMouseMove(int x, int y) {
    if (m_contextMenuOpen) {
        int hover = hitTestContextMenu(x, y);
        if (hover != m_contextMenuHover) {
            m_contextMenuHover = hover;
            invalidate();
        }
    }
}

void FileExplorerApp::onMouseDown(int localX, int localY, uint8_t button) {
    if (m_propertiesOpen && button == 1) {
        closeProperties();
        return;
    }

    if (m_contextMenuOpen) {
        if (button == 1 && handleContextMenuClick(localX, localY)) {
            return;
        }
        if (button == 1 || button == 2) {
            m_contextMenuOpen = false;
            invalidate();
            if (button != 2) return;
        }
    }

    int bodyY = TOOLBAR_H + ADDRESS_H;
    if (button == 1 && isScrollbarVisible()) {
        int sbLeft = scrollbarLeft();
        int rowsTop = bodyY + kFileExplorerListHeaderH;
        int rowsBottom = m_window ? m_window->h - kFileExplorerListStatusH : rowsTop;
        if (localX >= sbLeft && localX < sbLeft + kFileExplorerScrollbarW && localY >= rowsTop && localY < rowsBottom) {
            scrollToPosition(localY);
            return;
        }
    }

    int index = hitTestEntryRow(localX, localY);

    if (button == 2) {
        if (index >= 0 && index < m_entryCount) {
            m_selected = index;
            updateActionButtons();
            m_contextMenuTarget = ContextMenuTarget::Entry;
            m_contextMenuPasteVisible = false;
            m_contextMenuCreateFolderVisible = false;
            if (m_entries[m_selected].isDir) {
                char destinationDirectory[MAX_PATH_LEN];
                joinPath(m_currentPath, m_entries[m_selected].name, destinationDirectory, sizeof(destinationDirectory));
                m_contextMenuPasteVisible = file_clipboard::can_paste_to(destinationDirectory);
            }
            m_contextMenuOpen = true;
        } else {
            m_contextMenuTarget = ContextMenuTarget::CurrentDirectory;
            m_contextMenuPasteVisible = file_clipboard::can_paste_to(m_currentPath);
            m_contextMenuCreateFolderVisible = canCreateFolderHere();
            m_contextMenuOpen = contextMenuHasCurrentDirectoryPaste() ||
                                contextMenuHasCurrentDirectoryCreateFolder();
        }
        m_contextMenuX = localX;
        m_contextMenuY = localY;
        m_contextMenuHover = -1;
        if (m_contextMenuOpen) serial::puts("[fileexplorer-bm] context menu open\n");
        invalidate();
        return;
    }

    if (localX < LEFT_W) {
        if (handleNavigationPaneClick(localX, localY)) {
            return;
        }
        return;
    }

    if (localX < LEFT_W || localY < bodyY + kFileExplorerListHeaderH) return;

    if (index >= 0 && index < m_entryCount) {
        uint64_t now = pit::ticks();
        bool doubleClick = (index == m_lastClickIndex && now >= m_lastClickTick && now - m_lastClickTick <= 50);
        m_selected = index;
        m_lastClickIndex = index;
        m_lastClickTick = now;
        updateActionButtons();
        if (doubleClick) {
            openSelected();
            return;
        }
        invalidate();
    }
}

void FileExplorerApp::onMouseWheel(int localX, int localY, int wheelDelta) {
    if (wheelDelta == 0) return;
    if (!isWheelTarget(localX, localY)) return;
    scrollByRows(-wheelDelta * 3);
}

bool FileExplorerApp::handleNavigationPaneClick(int localX, int localY) {
    if (localX < 0 || localX >= LEFT_W) return false;

    const int bodyY = TOOLBAR_H + ADDRESS_H;
    const int rootTop = bodyY + kFileExplorerListHeaderH;
    const int mountsTop = bodyY + 40;
    const int mountRowsTop = bodyY + 60;

    auto hitBand = [&](int top, int height) {
        return localY >= top && localY < top + height;
    };

    if (hitBand(rootTop, ROW_H)) {
        navigate("/");
        return true;
    }

    if (hitBand(mountsTop, ROW_H)) {
        navigate("/");
        return true;
    }

    int row = 0;
    for (uint8_t i = 0; i < vfs::VFS_MAX_MOUNTS && row < 8; ++i) {
        const vfs::MountPoint* mp = vfs::get_mount_by_index(i);
        if (!mp || !mp->active) continue;

        const int rowTop = mountRowsTop + row * ROW_H;
        if (hitBand(rowTop, ROW_H)) {
            navigate(mp->path);
            return true;
        }
        ++row;
    }

    return false;
}

void FileExplorerApp::onWidgetClick(int widgetId) {
    if (file_clipboard::operation_active()) return;
    closeTransientUi();
    if (widgetId == m_backBtnId || widgetId == m_rootBtnId) {
        navigate("/");
    } else if (widgetId == m_upBtnId) {
        goUp();
    } else if (widgetId == m_refreshBtnId) {
        refresh();
        updateActionButtons();
        invalidate();
    } else if (widgetId == m_createFolderBtnId) {
        beginCreateFolder();
    } else if (widgetId == m_renameFileBtnId || widgetId == m_renameFolderBtnId) {
        beginRenameSelected();
    } else if (widgetId == m_deleteFileBtnId || widgetId == m_deleteFolderBtnId) {
        showDeleteConfirmation();
    } else if (widgetId == m_confirmDeleteBtnId) {
        confirmDelete();
    } else if (widgetId == m_cancelDeleteBtnId) {
        cancelDelete();
    }
}

void FileExplorerApp::refresh() {
    m_entryCount = 0;
    m_selected = 0;
    m_scroll = 0;
    m_lastClickIndex = -1;
    m_lastClickTick = 0;
    closeTransientUi();
    uint8_t dir = vfs::opendir(m_currentPath);
    if (dir == 0xFF) {
        setStatus("Cannot open directory. Mount a filesystem with vfsmount if needed.");
        return;
    }

    vfs::DirEntry de{};
    bool hasMore = false;
    while (m_entryCount < MAX_ENTRIES && vfs::readdir(dir, &de)) {
        if (de.name[0] == '.' && (de.name[1] == '\0' ||
            (de.name[1] == '.' && de.name[2] == '\0'))) {
            continue;
        }

        strcopy(m_entries[m_entryCount].name, de.name, vfs::VFS_MAX_FILENAME);
        m_entries[m_entryCount].isDir = (de.type == vfs::FILE_TYPE_DIRECTORY);
        m_entries[m_entryCount].size = de.size;
        m_entryCount++;
    }
    if (m_entryCount >= MAX_ENTRIES && vfs::readdir(dir, &de)) {
        hasMore = true;
    }
    vfs::closedir(dir);

    for (int i = 0; i < m_entryCount - 1; ++i) {
        for (int j = i + 1; j < m_entryCount; ++j) {
            bool swap = false;
            if (m_entries[i].isDir != m_entries[j].isDir) {
                swap = !m_entries[i].isDir && m_entries[j].isDir;
            } else {
                int k = 0;
                while (m_entries[i].name[k] && m_entries[j].name[k]) {
                    char a = m_entries[i].name[k];
                    char b = m_entries[j].name[k];
                    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
                    if (a != b) { swap = a > b; break; }
                    ++k;
                }
                if (!swap && !m_entries[i].name[k] && m_entries[j].name[k]) swap = false;
                else if (!swap && m_entries[i].name[k] && !m_entries[j].name[k]) swap = true;
            }
            if (swap) {
                Entry tmp = m_entries[i];
                m_entries[i] = m_entries[j];
                m_entries[j] = tmp;
            }
        }
    }

    clampSelectionAndScroll();

    if (m_entryCount == 0) {
        setStatus("Directory is empty");
    } else if (hasMore) {
        setStatus("Showing first 128 entries; more items available");
    } else {
        setStatus("Ready");
    }
    updateActionButtons();
}

void FileExplorerApp::navigate(const char* path) {
    if (!path || !path[0]) return;
    vfs::FileInfo info{};
    if (vfs::stat(path, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_DIRECTORY) {
        setStatus("Path not found or not a directory");
        invalidate();
        return;
    }
    strcopy(m_currentPath, path, MAX_PATH_LEN);
    m_selected = 0;
    m_scroll = 0;
    m_lastClickIndex = -1;
    m_lastClickTick = 0;
    closeTransientUi();
    refresh();
    invalidate();
}

void FileExplorerApp::openSelected() {
    if (m_selected < 0 || m_selected >= m_entryCount) return;
    Entry& e = m_entries[m_selected];
    char full[MAX_PATH_LEN];
    joinPath(m_currentPath, e.name, full, sizeof(full));
    if (e.isDir) {
        serial::puts("[fileexplorer-bm] open folder\n");
        navigate(full);
    } else if (isTextFile(e.name)) {
        if (app::AppManager::launchAppWithParam("Notepad", full)) {
            setStatus("Opened text file in Notepad");
        } else {
            setStatus("Unable to open text file in Notepad");
        }
        invalidate();
    } else if (endsWithIgnoreCase(e.name, ".png")) {
        if (app::AppManager::launchAppWithParam("ImageViewer", full)) {
            setStatus("Opened PNG in Image Viewer");
        } else {
            setStatus("Unable to open PNG in Image Viewer");
        }
        invalidate();
    } else if (endsWithIgnoreCase(e.name, ".img")) {
        openDiskImage(full, e);
    } else if (endsWithIgnoreCase(e.name, ".gxq") || endsWithIgnoreCase(e.name, ".gxapp") || endsWithIgnoreCase(e.name, ".elf") || endsWithIgnoreCase(e.name, ".exe")) {
        launchApplicationLikeFile(full, e);
    } else {
        setStatus("No application registered for this file type");
        invalidate();
    }
}

void FileExplorerApp::goUp() {
    char parent[MAX_PATH_LEN];
    parentPath(m_currentPath, parent, sizeof(parent));
    if (parent[0] && parent[0] != m_currentPath[0]) {
        navigate(parent);
    } else if (parent[0]) {
        bool different = false;
        for (int i = 0; parent[i] || m_currentPath[i]; ++i) {
            if (parent[i] != m_currentPath[i]) { different = true; break; }
        }
        if (different) navigate(parent);
    }
}

bool FileExplorerApp::canCreateFolderHere() const {
    vfs::FileInfo info{};
    if (vfs::stat(m_currentPath, &info) != vfs::VFS_OK ||
        info.type != vfs::FILE_TYPE_DIRECTORY) {
        return false;
    }

    const vfs::MountPoint* mount = vfs::get_mount(m_currentPath);
    return mount && mount->active && !mount->readOnly &&
           mount->fsType == vfs::FS_TYPE_FAT32;
}

void FileExplorerApp::updateActionButtons() {
    bool hasSelection = m_selected >= 0 && m_selected < m_entryCount;
    bool isDir = hasSelection && m_entries[m_selected].isDir;
    const bool fileOperationActive = file_clipboard::operation_active();

    app::Widget* createFolder = getWidget(m_createFolderBtnId);
    app::Widget* renameFile = getWidget(m_renameFileBtnId);
    app::Widget* deleteFile = getWidget(m_deleteFileBtnId);
    app::Widget* renameFolder = getWidget(m_renameFolderBtnId);
    app::Widget* deleteFolder = getWidget(m_deleteFolderBtnId);
    app::Widget* confirmDelete = getWidget(m_confirmDeleteBtnId);
    app::Widget* cancelDelete = getWidget(m_cancelDeleteBtnId);

    if (createFolder) {
        createFolder->visible = !fileOperationActive && canCreateFolderHere() && !m_renamePrompt && !m_deleteConfirm;
        createFolder->enabled = createFolder->visible;
    }
    if (renameFile) {
        renameFile->visible = !fileOperationActive && hasSelection && !isDir && !m_renamePrompt && !m_deleteConfirm;
        renameFile->enabled = renameFile->visible;
    }
    if (deleteFile) {
        deleteFile->visible = !fileOperationActive && hasSelection && !isDir && !m_renamePrompt && !m_deleteConfirm;
        deleteFile->enabled = deleteFile->visible;
    }
    if (renameFolder) {
        renameFolder->visible = !fileOperationActive && hasSelection && isDir && !m_renamePrompt && !m_deleteConfirm;
        renameFolder->enabled = renameFolder->visible;
    }
    if (deleteFolder) {
        deleteFolder->visible = !fileOperationActive && hasSelection && isDir && !m_renamePrompt && !m_deleteConfirm;
        deleteFolder->enabled = deleteFolder->visible;
    }
    if (confirmDelete) {
        confirmDelete->visible = !fileOperationActive && m_deleteConfirm;
        confirmDelete->enabled = confirmDelete->visible;
    }
    if (cancelDelete) {
        cancelDelete->visible = m_deleteConfirm;
        cancelDelete->enabled = cancelDelete->visible;
    }
}

int FileExplorerApp::visibleRowCount() const {
    if (!m_window) return 1;
    int rowsTop = TOOLBAR_H + ADDRESS_H + kFileExplorerListHeaderH;
    int rowsBottom = m_window->h - kFileExplorerListStatusH;
    int rows = (rowsBottom > rowsTop) ? ((rowsBottom - rowsTop) / ROW_H) : 0;
    return rows > 0 ? rows : 1;
}

int FileExplorerApp::maxScrollRows() const {
    int visible = visibleRowCount();
    return m_entryCount > visible ? (m_entryCount - visible) : 0;
}

bool FileExplorerApp::isScrollbarVisible() const {
    return maxScrollRows() > 0;
}

int FileExplorerApp::scrollbarLeft() const {
    if (!m_window) return 0;
    return m_window->w - kFileExplorerScrollbarPad - kFileExplorerScrollbarW;
}

int FileExplorerApp::scrollbarTrackTop() const {
    return TOOLBAR_H + ADDRESS_H + kFileExplorerListHeaderH + kFileExplorerScrollbarPad;
}

int FileExplorerApp::scrollbarTrackHeight() const {
    if (!m_window) return 0;
    int rowsTop = TOOLBAR_H + ADDRESS_H + kFileExplorerListHeaderH;
    int rowsBottom = m_window->h - kFileExplorerListStatusH;
    int trackH = rowsBottom - rowsTop - (kFileExplorerScrollbarPad * 2);
    return trackH > 0 ? trackH : 1;
}

int FileExplorerApp::scrollbarThumbHeight() const {
    if (!isScrollbarVisible()) return 0;
    int visible = visibleRowCount();
    int total = m_entryCount > 0 ? m_entryCount : 1;
    int trackH = scrollbarTrackHeight();
    int thumbH = (trackH * visible) / total;
    if (thumbH < kFileExplorerScrollbarMinThumbH) thumbH = kFileExplorerScrollbarMinThumbH;
    if (thumbH > trackH) thumbH = trackH;
    return thumbH;
}

int FileExplorerApp::scrollbarThumbTop() const {
    if (!isScrollbarVisible()) return scrollbarTrackTop();
    int maxScroll = maxScrollRows();
    int trackTop = scrollbarTrackTop();
    int trackTravel = scrollbarTrackHeight() - scrollbarThumbHeight();
    if (trackTravel <= 0 || maxScroll <= 0) return trackTop;
    int offset = m_scroll;
    if (offset < 0) offset = 0;
    if (offset > maxScroll) offset = maxScroll;
    return trackTop + (trackTravel * offset) / maxScroll;
}

void FileExplorerApp::ensureSelectedVisible() {
    if (m_entryCount <= 0) {
        m_selected = 0;
        m_scroll = 0;
        return;
    }

    int visible = visibleRowCount();
    int maxScroll = maxScrollRows();

    if (m_selected < 0) m_selected = 0;
    if (m_selected >= m_entryCount) m_selected = m_entryCount - 1;

    if (m_scroll < 0) m_scroll = 0;
    if (m_scroll > maxScroll) m_scroll = maxScroll;

    if (m_selected < m_scroll) {
        m_scroll = m_selected;
    } else if (m_selected >= m_scroll + visible) {
        m_scroll = m_selected - visible + 1;
    }

    if (m_scroll < 0) m_scroll = 0;
    if (m_scroll > maxScroll) m_scroll = maxScroll;
}

void FileExplorerApp::clampSelectionAndScroll() {
    if (m_entryCount <= 0) {
        m_selected = 0;
        m_scroll = 0;
        return;
    }

    if (m_selected < 0) m_selected = 0;
    if (m_selected >= m_entryCount) m_selected = m_entryCount - 1;
    if (m_scroll < 0) m_scroll = 0;
    int maxScroll = maxScrollRows();
    if (m_scroll > maxScroll) m_scroll = maxScroll;
    ensureSelectedVisible();
}

void FileExplorerApp::scrollByRows(int rows) {
    if (rows == 0 || m_entryCount <= 0) return;
    int maxScroll = maxScrollRows();
    if (maxScroll <= 0) return;
    int next = m_scroll + rows;
    if (next < 0) next = 0;
    if (next > maxScroll) next = maxScroll;
    if (next == m_scroll) return;
    m_scroll = next;
    m_lastClickIndex = -1;
    m_lastClickTick = 0;
    invalidate();
}

void FileExplorerApp::scrollToPosition(int localY) {
    if (!isScrollbarVisible()) return;
    int maxScroll = maxScrollRows();
    if (maxScroll <= 0) return;

    int trackTop = scrollbarTrackTop();
    int trackTravel = scrollbarTrackHeight() - scrollbarThumbHeight();
    if (trackTravel <= 0) return;

    int centered = localY - trackTop - (scrollbarThumbHeight() / 2);
    if (centered < 0) centered = 0;
    if (centered > trackTravel) centered = trackTravel;

    int next = (centered * maxScroll + (trackTravel / 2)) / trackTravel;
    if (next < 0) next = 0;
    if (next > maxScroll) next = maxScroll;
    if (next == m_scroll) return;
    m_scroll = next;
    invalidate();
}

bool FileExplorerApp::isWheelTarget(int localX, int localY) const {
    if (!isScrollbarVisible()) return false;
    if (m_renamePrompt || m_deleteConfirm || m_propertiesOpen || m_contextMenuOpen) return false;
    if (localX < LEFT_W) return false;

    const int bodyY = TOOLBAR_H + ADDRESS_H;
    const int listTop = bodyY + kFileExplorerListHeaderH;
    const int listBottom = m_window ? m_window->h - kFileExplorerListStatusH : listTop;
    return localY >= listTop && localY < listBottom;
}

void FileExplorerApp::beginCreateFolder() {
    if (file_clipboard::operation_active()) return;
    if (!canCreateFolderHere()) {
        setStatus("Folder creation is unavailable here");
        invalidate();
        return;
    }

    m_deleteConfirm = false;
    m_createFolderPrompt = true;
    m_renamePrompt = true;
    strcopy(m_renameValue, "NEWDIR", sizeof(m_renameValue));
    setStatus("Type a folder name (up to 8 characters), then press Enter.");
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::commitCreateFolder() {
    if (file_clipboard::operation_active()) return;
    if (!m_createFolderPrompt || !m_renameValue[0]) {
        cancelCreateFolder();
        return;
    }

    char path[MAX_PATH_LEN];
    joinPath(m_currentPath, m_renameValue, path, sizeof(path));

    vfs::FileInfo existing{};
    if (vfs::stat(path, &existing) == vfs::VFS_OK) {
        setStatus("A file or folder with that name already exists");
        updateActionButtons();
        invalidate();
        return;
    }

    vfs::Status status = vfs::mkdir(path);
    if (status == vfs::VFS_OK) {
        m_createFolderPrompt = false;
        m_renamePrompt = false;
        refresh();
        setStatus("Created folder");
    } else if (status == vfs::VFS_ERR_READ_ONLY) {
        setStatus("Folder cannot be created in a read-only location");
    } else {
        setStatus("Could not create folder");
    }
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::cancelCreateFolder() {
    m_createFolderPrompt = false;
    m_renamePrompt = false;
    setStatus("Create folder cancelled");
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::beginRenameSelected() {
    if (file_clipboard::operation_active()) return;
    if (m_selected < 0 || m_selected >= m_entryCount) return;
    m_deleteConfirm = false;
    m_createFolderPrompt = false;
    m_renamePrompt = true;
    strcopy(m_renameValue, m_entries[m_selected].name, sizeof(m_renameValue));
    setStatus("Type a new name, then press Enter.");
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::commitRename() {
    if (file_clipboard::operation_active()) return;
    if (m_selected < 0 || m_selected >= m_entryCount || !m_renameValue[0]) {
        cancelRename();
        return;
    }

    char oldPath[MAX_PATH_LEN];
    char newPath[MAX_PATH_LEN];
    joinPath(m_currentPath, m_entries[m_selected].name, oldPath, sizeof(oldPath));
    joinPath(m_currentPath, m_renameValue, newPath, sizeof(newPath));

    vfs::Status status = vfs::rename(oldPath, newPath);
    m_renamePrompt = false;
    m_createFolderPrompt = false;
    if (status == vfs::VFS_OK) {
        setStatus("Renamed item");
    } else {
        setStatus("Rename failed");
    }
    refresh();
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::cancelRename() {
    m_createFolderPrompt = false;
    m_renamePrompt = false;
    setStatus("Rename cancelled");
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::showDeleteConfirmation() {
    if (file_clipboard::operation_active()) return;
    if (m_selected < 0 || m_selected >= m_entryCount) return;
    Entry& entry = m_entries[m_selected];
    joinPath(m_currentPath, entry.name, m_deleteTarget, sizeof(m_deleteTarget));
    strcopy(m_deleteTargetName, entry.name, sizeof(m_deleteTargetName));
    m_deleteTargetIsDir = entry.isDir;
    m_createFolderPrompt = false;
    m_renamePrompt = false;
    m_deleteConfirm = true;
    setStatus("Confirm delete");
    serial::puts("[fileexplorer-bm] move-to-trash requested\n");
    serial::puts("[fileexplorer-bm] current directory=");
    serial::puts(m_currentPath);
    serial::puts("\n[fileexplorer-bm] selected full item name=");
    serial::puts(entry.name);
    serial::puts("\n[fileexplorer-bm] resolved source path=");
    serial::puts(m_deleteTarget);
    serial::puts("\n");
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::confirmDelete() {
    if (file_clipboard::operation_active()) return;
    if (!m_deleteConfirm || !m_deleteTarget[0]) return;
    char movedPath[MAX_PATH_LEN];
    char error[96];
    movedPath[0] = '\0';
    error[0] = '\0';
    m_deleteConfirm = false;
    bool moved = kernel_move_path_to_trash(m_deleteTarget, m_deleteTargetName, m_deleteTargetIsDir, movedPath, sizeof(movedPath), error, sizeof(error));
    m_lastFileOperationGeneration = file_clipboard::operation_generation();
    if (moved) {
        setStatus("Moved item to Trash");
    } else {
        setStatus(error[0] ? error : "Move to Trash failed");
    }
    if (moved) {
        // Refresh only after a completed filesystem transaction.  A refused
        // non-empty folder has not mutated the source or Trash and must not
        // re-enter enumeration from inside the Delete failure path.
        refresh();
        desktop_request_folder_refresh();
        kernel_desktop_refresh_trash_state();
        file_clipboard::note_trash_refresh(true);
        setStatus("Moved item to Trash");
    } else {
        setStatus(error[0] ? error : "Move to Trash failed");
    }
    serial::puts("[fileexplorer-bm] refresh triggered after move-to-trash result=");
    serial::puts(moved ? "success" : "failure");
    serial::puts("\n");
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::cancelDelete() {
    m_deleteConfirm = false;
    setStatus("Delete cancelled");
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::pinSelectedToDesktop() {
    if (m_selected < 0 || m_selected >= m_entryCount) return;
    Entry& entry = m_entries[m_selected];
    char full[MAX_PATH_LEN];
    joinPath(m_currentPath, entry.name, full, sizeof(full));
    serial::puts("[fileexplorer-bm] Pin to Desktop selected path=");
    serial::puts(full);
    serial::puts(entry.isDir ? " kind=Folder\n" : " kind=File\n");
    bool pinned = kernel::desktop::pin_filesystem_shortcut_to_desktop(full, entry.isDir);
    setStatus(pinned ? "Pinned to Desktop" : "Pin to Desktop skipped");
    invalidate();
}
void FileExplorerApp::showPropertiesForSelected() {
    if (m_selected < 0 || m_selected >= m_entryCount) return;
    Entry& entry = m_entries[m_selected];
    char full[MAX_PATH_LEN];
    joinPath(m_currentPath, entry.name, full, sizeof(full));
    strcopy(m_propertiesName, entry.name, sizeof(m_propertiesName));
    strcopy(m_propertiesPath, full, sizeof(m_propertiesPath));
    strcopy(m_propertiesType, fileType(entry), sizeof(m_propertiesType));
    if (entry.isDir) strcopy(m_propertiesSize, "--", sizeof(m_propertiesSize));
    else formatSize(entry.size, m_propertiesSize, sizeof(m_propertiesSize));
    strcopy(m_propertiesModified, "--", sizeof(m_propertiesModified));
    m_propertiesIsDir = entry.isDir;
    strcopy(m_propertiesIcon, fileLogicalIcon(entry), sizeof(m_propertiesIcon));
    m_propertiesOpen = true;
    m_contextMenuOpen = false;
    serial::puts("[fileexplorer-bm] properties open\n");
    invalidate();
}

void FileExplorerApp::closeProperties() {
    m_propertiesOpen = false;
    invalidate();
}

void FileExplorerApp::beginCopySelected() {
    if (file_clipboard::operation_active()) return;
    if (m_selected < 0 || m_selected >= m_entryCount) return;
    Entry& entry = m_entries[m_selected];
    char fullPath[MAX_PATH_LEN];
    joinPath(m_currentPath, entry.name, fullPath, sizeof(fullPath));
    serial::puts("[fileexplorer-bm] FILE_CLIPBOARD_CONTEXT_COMMAND=COPY\n");
    if (file_clipboard::set_file(fullPath, file_clipboard::Operation::Copy)) {
        setStatus("Copied file to guideXOS clipboard");
        serial::puts("[fileexplorer-bm] shared file clipboard copy prepared\n");
    } else {
        setStatus(file_clipboard::paste_diagnostic_message());
    }
    invalidate();
}

void FileExplorerApp::beginMoveSelected() {
    if (file_clipboard::operation_active()) return;
    if (m_selected < 0 || m_selected >= m_entryCount) return;
    Entry& entry = m_entries[m_selected];
    char fullPath[MAX_PATH_LEN];
    joinPath(m_currentPath, entry.name, fullPath, sizeof(fullPath));
    serial::puts("[fileexplorer-bm] FILE_CLIPBOARD_CONTEXT_COMMAND=CUT\n");
    if (file_clipboard::set_file(fullPath, file_clipboard::Operation::Move)) {
        setStatus("Cut file to guideXOS clipboard");
        serial::puts("[fileexplorer-bm] shared file clipboard move prepared\n");
    } else {
        setStatus(file_clipboard::paste_diagnostic_message());
    }
    invalidate();
}

void FileExplorerApp::pasteClipboard() {
    pasteClipboardTo(m_currentPath);
}

void FileExplorerApp::pasteClipboardTo(const char* destinationDirectory) {
    if (file_clipboard::operation_active()) return;
    serial::puts("[fileexplorer-bm] FILE_CLIPBOARD_CONTEXT_COMMAND=PASTE destination=");
    serial::puts(destinationDirectory ? destinationDirectory : "(null)");
    serial::puts("\n");
    file_clipboard::PasteResult result = file_clipboard::paste_to_directory(destinationDirectory);
    setStatus(file_clipboard::paste_result_message(result));
    if (result == file_clipboard::PasteResult::Success) {
        file_clipboard::begin_paste_refresh();
        refresh();
        file_clipboard::note_paste_refresh(true);
    }
    invalidate();
}

bool FileExplorerApp::contextMenuHasFileOperations() const {
    return m_contextMenuTarget == ContextMenuTarget::Entry &&
           m_selected >= 0 && m_selected < m_entryCount;
}

bool FileExplorerApp::contextMenuHasFolderPaste() const {
    if (m_contextMenuTarget != ContextMenuTarget::Entry ||
        m_selected < 0 || m_selected >= m_entryCount || !m_entries[m_selected].isDir) {
        return false;
    }
    return m_contextMenuPasteVisible;
}

bool FileExplorerApp::contextMenuHasCurrentDirectoryPaste() const {
    return m_contextMenuTarget == ContextMenuTarget::CurrentDirectory &&
           m_contextMenuPasteVisible;
}

bool FileExplorerApp::contextMenuHasCurrentDirectoryCreateFolder() const {
    return m_contextMenuTarget == ContextMenuTarget::CurrentDirectory &&
           m_contextMenuCreateFolderVisible;
}

int FileExplorerApp::contextMenuItemCount() const {
    if (m_contextMenuTarget == ContextMenuTarget::CurrentDirectory) {
        int count = 0;
        if (contextMenuHasCurrentDirectoryPaste()) ++count;
        if (contextMenuHasCurrentDirectoryCreateFolder()) ++count;
        return count;
    }
    if (contextMenuHasFileOperations()) return m_entries[m_selected].isDir && contextMenuHasFolderPaste() ? 8 : 7;
    return contextMenuHasFolderPaste() ? 6 : 5;
}

const char* FileExplorerApp::contextMenuItemLabel(int item) const {
    if (m_contextMenuTarget == ContextMenuTarget::CurrentDirectory) {
        if (contextMenuHasCurrentDirectoryPaste()) {
            if (item == 0) return "Paste";
            if (item == 1 && contextMenuHasCurrentDirectoryCreateFolder()) return "Create Folder";
        } else if (contextMenuHasCurrentDirectoryCreateFolder() && item == 0) {
            return "Create Folder";
        }
        return "";
    }

    if (contextMenuHasFileOperations()) {
        if (m_entries[m_selected].isDir && contextMenuHasFolderPaste()) {
            switch (item) {
                case 0: return "Open";
                case 1: return "Copy File";
                case 2: return "Cut File";
                case 3: return "Paste";
                case 4: return "Pin to Desktop";
                case 5: return "Rename";
                case 6: return "Move to Trash";
                case 7: return "Properties";
                default: return "";
            }
        }
        switch (item) {
            case 0: return "Open";
            case 1: return "Copy File";
            case 2: return "Cut File";
            case 3: return "Pin to Desktop";
            case 4: return "Rename";
            case 5: return "Move to Trash";
            case 6: return "Properties";
            default: return "";
        }
    }

    if (contextMenuHasFolderPaste()) {
        switch (item) {
            case 0: return "Open";
            case 1: return "Paste";
            case 2: return "Pin to Desktop";
            case 3: return "Rename";
            case 4: return "Move to Trash";
            case 5: return "Properties";
            default: return "";
        }
    }

    switch (item) {
        case 0: return "Open";
        case 1: return "Pin to Desktop";
        case 2: return "Rename";
        case 3: return "Move to Trash";
        case 4: return "Properties";
        default: return "";
    }
}

int FileExplorerApp::hitTestContextMenu(int x, int y) const {
    if (!m_contextMenuOpen) return -1;
    if (x < m_contextMenuX || x >= m_contextMenuX + CONTEXT_MENU_W) return -1;
    if (y < m_contextMenuY || y >= m_contextMenuY + CONTEXT_MENU_ITEM_H * contextMenuItemCount()) return -1;
    return (y - m_contextMenuY) / CONTEXT_MENU_ITEM_H;
}

bool FileExplorerApp::handleContextMenuClick(int x, int y) {
    if (file_clipboard::operation_active()) return false;
    int item = hitTestContextMenu(x, y);
    if (item < 0) return false;
    m_contextMenuOpen = false;
    if (m_contextMenuTarget == ContextMenuTarget::CurrentDirectory) {
        if (contextMenuHasCurrentDirectoryPaste() && item == 0) {
            pasteClipboard();
        } else {
            const int createFolderItem = contextMenuHasCurrentDirectoryPaste() ? 1 : 0;
            if (contextMenuHasCurrentDirectoryCreateFolder() && item == createFolderItem) {
                beginCreateFolder();
            }
        }
    } else if (contextMenuHasFileOperations()) {
        const bool folderPaste = m_entries[m_selected].isDir && contextMenuHasFolderPaste();
        if (folderPaste && item == 3) {
            char destinationDirectory[MAX_PATH_LEN];
            joinPath(m_currentPath, m_entries[m_selected].name, destinationDirectory, sizeof(destinationDirectory));
            pasteClipboardTo(destinationDirectory);
            return true;
        }
        switch (item) {
            case 0: openSelected(); break;
            case 1: beginCopySelected(); break;
            case 2: beginMoveSelected(); break;
            case 3: if (!folderPaste) pinSelectedToDesktop(); break;
            case 4: if (folderPaste) pinSelectedToDesktop(); else beginRenameSelected(); break;
            case 5: if (folderPaste) beginRenameSelected(); else showDeleteConfirmation(); break;
            case 6: if (folderPaste) showDeleteConfirmation(); else showPropertiesForSelected(); break;
            case 7: if (folderPaste) showPropertiesForSelected(); break;
            default: break;
        }
    } else if (contextMenuHasFolderPaste()) {
        switch (item) {
            case 0: openSelected(); break;
            case 1: {
                char destinationDirectory[MAX_PATH_LEN];
                joinPath(m_currentPath, m_entries[m_selected].name, destinationDirectory, sizeof(destinationDirectory));
                pasteClipboardTo(destinationDirectory);
                break;
            }
            case 2: pinSelectedToDesktop(); break;
            case 3: beginRenameSelected(); break;
            case 4: showDeleteConfirmation(); break;
            case 5: showPropertiesForSelected(); break;
            default: break;
        }
    } else {
        switch (item) {
            case 0: openSelected(); break;
            case 1: pinSelectedToDesktop(); break;
            case 2: beginRenameSelected(); break;
            case 3: showDeleteConfirmation(); break;
            case 4: showPropertiesForSelected(); break;
            default: break;
        }
    }
    return true;
}

int FileExplorerApp::hitTestEntryRow(int x, int y) const {
    int bodyY = TOOLBAR_H + ADDRESS_H;
    if (x < LEFT_W || y < bodyY + kFileExplorerListHeaderH) return -1;
    if (isScrollbarVisible() && x >= scrollbarLeft()) return -1;
    int row = (y - bodyY - kFileExplorerListHeaderH) / ROW_H;
    int index = m_scroll + row;
    return (index >= 0 && index < m_entryCount) ? index : -1;
}

void FileExplorerApp::closeTransientUi() {
    m_contextMenuOpen = false;
    m_contextMenuHover = -1;
    m_contextMenuPasteVisible = false;
    m_contextMenuCreateFolderVisible = false;
    m_propertiesOpen = false;
}

bool FileExplorerApp::launchApplicationLikeFile(const char* fullPath, const Entry& entry) {
    (void)fullPath;
    serial::puts("[fileexplorer-bm] application-like file open requested: ");
    serial::puts(entry.name);
    serial::puts("\n");
    setStatus("Application file launch not yet wired in bare metal");
    invalidate();
    return false;
}

bool FileExplorerApp::openDiskImage(const char* fullPath, const Entry& entry) {
    (void)fullPath;
    serial::puts("[fileexplorer-bm] disk image open requested: ");
    serial::puts(entry.name);
    serial::puts("\n");
    if (app::AppManager::launchApp("DiskManager")) {
        setStatus("Opened Disk Manager for disk image workflow");
    } else {
        setStatus("Unable to open Disk Manager");
    }
    invalidate();
    return true;
}

void FileExplorerApp::setStatus(const char* status) {
    strcopy(m_status, status ? status : "", sizeof(m_status));
}

bool FileExplorerApp::isTextFile(const char* name) const {
    if (!name) return false;

    const char* dot = nullptr;
    for (int i = 0; name[i]; ++i) {
        if (name[i] == '.') dot = &name[i];
    }

    if (!dot) return false;

    char ext[6];
    int len = 0;
    for (int i = 1; dot[i] && len < 5; ++i) {
        char c = dot[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        ext[len++] = c;
    }
    ext[len] = '\0';

    return (len == 3 && ext[0] == 't' && ext[1] == 'x' && ext[2] == 't') ||
           (len == 4 && ext[0] == 't' && ext[1] == 'e' && ext[2] == 'x' && ext[3] == 't');
}

void FileExplorerApp::joinPath(const char* base, const char* name, char* out, int outSize) const {
    if (!out || outSize <= 0) return;
    int pos = 0;
    if (!base || !base[0]) base = "/";
    while (base[pos] && pos < outSize - 1) {
        out[pos] = base[pos];
        pos++;
    }
    if (pos > 1 && out[pos - 1] != '/' && pos < outSize - 1) out[pos++] = '/';
    if (pos == 1 && out[0] == '/') {
        // root already has separator
    }
    for (int i = 0; name && name[i] && pos < outSize - 1; ++i) out[pos++] = name[i];
    out[pos] = '\0';
}

void FileExplorerApp::parentPath(const char* path, char* out, int outSize) const {
    if (!out || outSize <= 0) return;
    if (!path || !path[0] || (path[0] == '/' && path[1] == '\0')) {
        strcopy(out, "/", outSize);
        return;
    }

    int len = strlen_local(path);
    while (len > 1 && path[len - 1] == '/') len--;
    int slash = len - 1;
    while (slash > 0 && path[slash] != '/') slash--;
    int copyLen = slash == 0 ? 1 : slash;
    if (copyLen >= outSize) copyLen = outSize - 1;
    for (int i = 0; i < copyLen; ++i) out[i] = path[i];
    out[copyLen] = '\0';
}

void FileExplorerApp::formatSize(uint64_t size, char* out, int outSize) const {
    if (!out || outSize <= 0) return;
    uint64_t value = size;
    const char* suffix = " B";
    if (size >= 1024 * 1024) { value = size / (1024 * 1024); suffix = " MB"; }
    else if (size >= 1024) { value = size / 1024; suffix = " KB"; }

    char digits[24];
    int d = 0;
    if (value == 0) digits[d++] = '0';
    else {
        char tmp[24];
        int t = 0;
        while (value > 0 && t < 23) { tmp[t++] = '0' + (value % 10); value /= 10; }
        while (t > 0) digits[d++] = tmp[--t];
    }
    digits[d] = '\0';

    int pos = 0;
    for (int i = 0; digits[i] && pos < outSize - 1; ++i) out[pos++] = digits[i];
    for (int i = 0; suffix[i] && pos < outSize - 1; ++i) out[pos++] = suffix[i];
    out[pos] = '\0';
}

const char* FileExplorerApp::fileType(const Entry& entry) const {
    if (entry.isDir) return "File folder";
    if (endsWithIgnoreCase(entry.name, ".txt") || endsWithIgnoreCase(entry.name, ".log") || endsWithIgnoreCase(entry.name, ".cfg") || endsWithIgnoreCase(entry.name, ".ini") || endsWithIgnoreCase(entry.name, ".md")) return "Text document";
    if (endsWithIgnoreCase(entry.name, ".gxq") || endsWithIgnoreCase(entry.name, ".gxapp") || endsWithIgnoreCase(entry.name, ".elf") || endsWithIgnoreCase(entry.name, ".exe")) return "Application";
    if (endsWithIgnoreCase(entry.name, ".img")) return "Disk image";
    if (endsWithIgnoreCase(entry.name, ".bin") || endsWithIgnoreCase(entry.name, ".dat") || endsWithIgnoreCase(entry.name, ".dll") || endsWithIgnoreCase(entry.name, ".so") || endsWithIgnoreCase(entry.name, ".o")) return "Binary file";
    return "File";
}

// ============================================================
// DiskManagerApp Implementation
// ============================================================

DiskManagerApp::DiskManagerApp()
    : m_diskCount(0), m_selectedDisk(0), m_refreshBtnId(-1) {
    strcopy(m_name, "DiskManager", app::MAX_APP_NAME);
}

DiskManagerApp::~DiskManagerApp() {
}

bool DiskManagerApp::init() {
    serial::puts("[DISKMANAGER] Starting in baremetal mode\n");

    m_window = new app::KernelWindow();
    strcopy(m_window->title, "Disk Manager", app::MAX_TITLE_LEN);
    m_window->x = 120;
    m_window->y = 55;
    m_window->w = 700;
    m_window->h = 420;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_RESIZABLE | app::WF_FOCUSED;
    m_window->owner = this;

    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        serial::puts("[DISKMANAGER] Failed to register window\n");
        return false;
    }

    m_refreshBtnId = addButton(10, m_window->h - 44, 90, 28, "Refresh");

    scanDisks();

    m_state = app::AppState::Running;
    serial::puts("[DISKMANAGER] Init complete\n");
    return true;
}

void DiskManagerApp::shutdown() {
    m_state = app::AppState::Terminated;
}

void DiskManagerApp::scanDisks() {
    m_diskCount = 0;
    uint8_t total = kernel::block::device_count();
    serial::puts("[DISKMANAGER] Scanning block devices, count=");
    serial::put_hex8(total);
    serial::putc('\n');

    for (uint8_t i = 0; i < total && m_diskCount < MAX_DISKS; i++) {
        const kernel::block::BlockDevice* dev = kernel::block::get_device(i);
        if (!dev || !dev->active) continue;

        DiskEntry& e = m_disks[m_diskCount];
        e.devIndex = i;
        e.totalSectors = dev->totalSectors;
        e.sectorSize = dev->sectorSize;
        e.haveInfo = true;
        e.partCount = 0;

        // Build display name from device name + type
        const char* typeStr = "Disk";
        if (dev->type == kernel::block::BDEV_ATA_PIO || dev->type == kernel::block::BDEV_AHCI)
            typeStr = "System";
        else if (dev->type == kernel::block::BDEV_NVME)
            typeStr = "NVMe";
        else if (dev->type == kernel::block::BDEV_USB_MASS)
            typeStr = "USB";

        // name = "<dev->name> (<typeStr>)"
        int ni = 0;
        for (int j = 0; dev->name[j] && ni < 30; j++) e.name[ni++] = dev->name[j];
        e.name[ni++] = ' '; e.name[ni++] = '(';
        for (int j = 0; typeStr[j] && ni < 37; j++) e.name[ni++] = typeStr[j];
        e.name[ni++] = ')'; e.name[ni] = '\0';

        readMBR(e);
        m_diskCount++;
    }

    if (m_diskCount == 0) {
        serial::puts("[DISKMANAGER] No block devices found\n");
        DiskEntry& e = m_disks[0];
        strcopy(e.name, "No disks detected", 40);
        e.devIndex = 0;
        e.haveInfo = false;
        e.totalSectors = 0;
        e.sectorSize = 512;
        e.partCount = 0;
        m_diskCount = 1;
    }

    if (m_selectedDisk >= m_diskCount) m_selectedDisk = 0;
}

void DiskManagerApp::readMBR(DiskEntry& disk) {
    disk.partCount = 0;
    uint8_t mbr[512];
    kernel::block::Status st = kernel::block::read_sectors(disk.devIndex, 0, 1, mbr);
    if (st != kernel::block::BLOCK_OK) return;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return;

    for (int i = 0; i < MAX_PARTS; i++) {
        int off = 446 + i * 16;
        uint8_t type = mbr[off + 4];
        if (type == 0) continue;

        PartEntry& p = disk.parts[disk.partCount];
        p.type     = type;
        p.bootable = (mbr[off + 0] == 0x80);
        p.lbaStart = (uint32_t)mbr[off + 8]  | ((uint32_t)mbr[off + 9]  << 8) |
                     ((uint32_t)mbr[off + 10] << 16) | ((uint32_t)mbr[off + 11] << 24);
        p.lbaCount = (uint32_t)mbr[off + 12] | ((uint32_t)mbr[off + 13] << 8) |
                     ((uint32_t)mbr[off + 14] << 16) | ((uint32_t)mbr[off + 15] << 24);
        const char* fs = detectFs(disk.devIndex, p.lbaStart);
        strcopy(p.fsLabel, fs, (int)sizeof(p.fsLabel));
        disk.partCount++;
    }
}

const char* DiskManagerApp::detectFs(uint8_t devIndex, uint32_t lbaStart) {
    if (lbaStart == 0) return "Unknown";
    uint8_t sec[512];
    kernel::block::Status st = kernel::block::read_sectors(devIndex, lbaStart, 1, sec);
    if (st != kernel::block::BLOCK_OK) return "Unknown";

    // TarFS magic at offset 257
    if (sec[257] == 'u' && sec[258] == 's' && sec[259] == 't' &&
        sec[260] == 'a' && sec[261] == 'r') return "TarFS";

    // FAT: boot signature + sane BPB
    if (sec[510] == 0x55 && sec[511] == 0xAA) {
        uint16_t bps = (uint16_t)sec[11] | ((uint16_t)sec[12] << 8);
        if ((bps == 512 || bps == 1024 || bps == 2048 || bps == 4096) && sec[13] != 0)
            return "FAT";
    }

    // EXT2/3/4: magic at superblock offset 0x438
    uint8_t sb[512];
    kernel::block::Status st2 = kernel::block::read_sectors(devIndex, lbaStart + 2, 1, sb);
    if (st2 == kernel::block::BLOCK_OK) {
        uint16_t ext_magic = (uint16_t)sb[0x38] | ((uint16_t)sb[0x39] << 8);
        if (ext_magic == 0xEF53) return "EXT2";
    }

    return "Unknown";
}

void DiskManagerApp::formatSize(uint64_t bytes, char* out, int outSize) const {
    // Simple size formatter: TB/GB/MB/KB/B
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    uint64_t val = bytes;
    while (val >= 1024 && u < 4) { val >>= 10; u++; }

    // Convert to decimal string (no printf available)
    char tmp[24];
    int ti = 0;
    if (val == 0) {
        tmp[ti++] = '0';
    } else {
        uint64_t v = val;
        char rev[20]; int ri = 0;
        while (v > 0) { rev[ri++] = '0' + (int)(v % 10); v /= 10; }
        for (int j = ri - 1; j >= 0; j--) tmp[ti++] = rev[j];
    }
    tmp[ti++] = ' ';
    for (int j = 0; units[u][j] && ti < 22; j++) tmp[ti++] = units[u][j];
    tmp[ti] = '\0';

    // Copy to out
    int i = 0;
    while (tmp[i] && i < outSize - 1) { out[i] = tmp[i]; i++; }
    out[i] = '\0';
}

void DiskManagerApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    static const uint32_t kHeader   = 0xFF2C3E50;
    static const uint32_t kBg       = 0xFF1E2430;
    static const uint32_t kPanel    = 0xFF252D3B;
    static const uint32_t kRowSel   = 0xFF2E4A6E;
    static const uint32_t kRowAlt   = 0xFF222A36;
    static const uint32_t kText     = 0xFFDCE3F0;
    static const uint32_t kSubText  = 0xFF8A9AB0;
    static const uint32_t kAccent   = 0xFF4A9ECA;
    static const uint32_t kPartBar  = 0xFF3A7EAA;
    static const uint32_t kPartBarB = 0xFF2A5E80;
    (void)kAccent; (void)kPartBarB;

    // Background
    framebuffer::fill_rect(x, y, w, h, kBg);

    // Title bar stripe
    framebuffer::fill_rect(x, y, w, 22, kHeader);
    appDrawText(x + 10, y + 7, "Disk Manager  [baremetal mode]", kText);

    // Left pane: disk list
    const uint32_t leftW = 200;
    framebuffer::fill_rect(x, y + 22, leftW, h - 22, kPanel);
    appDrawText(x + 8, y + 28, "Disks", kSubText);

    uint32_t rowH = 28;
    for (int i = 0; i < m_diskCount; i++) {
        uint32_t ry = y + 46 + (uint32_t)i * rowH;
        uint32_t rowColor = (i == m_selectedDisk) ? kRowSel : ((i % 2 == 0) ? kPanel : kRowAlt);
        framebuffer::fill_rect(x + 2, ry, leftW - 4, rowH - 2, rowColor);
        appDrawText(x + 8, ry + (rowH - kGlyphH) / 2, m_disks[i].name, kText);
    }

    // Right pane: detail
    uint32_t rx = x + leftW + 4;
    uint32_t rw = (w > leftW + 8) ? (w - leftW - 8) : 0;
    framebuffer::fill_rect(rx, y + 22, rw, h - 22, kBg);

    if (m_selectedDisk >= 0 && m_selectedDisk < m_diskCount) {
        const DiskEntry& d = m_disks[m_selectedDisk];
        uint32_t dy = y + 28;

        // Disk header
        appDrawText(rx + 4, dy, d.name, kText);
        dy += kGlyphH + 6;

        if (d.haveInfo) {
            char szBuf[32];
            formatSize(d.totalSectors * (uint64_t)d.sectorSize, szBuf, sizeof(szBuf));
            appDrawText(rx + 4, dy, szBuf, kSubText);
            dy += kGlyphH + 10;

            // Partition table header
            appDrawText(rx + 4, dy, "# ", kSubText);
            appDrawText(rx + 20, dy, "Type  LBA Start    Sectors     FS       Boot", kSubText);
            dy += kGlyphH + 4;
            framebuffer::fill_rect(rx + 4, dy, rw - 8, 1, kPanel);
            dy += 3;

            if (d.partCount == 0) {
                appDrawText(rx + 4, dy, "No MBR partitions found", kSubText);
                dy += kGlyphH + 6;
            }

            for (int pi = 0; pi < d.partCount; pi++) {
                const PartEntry& p = d.parts[pi];
                uint32_t pry = dy + (uint32_t)pi * (kGlyphH + 6);

                // Small partition color bar
                framebuffer::fill_rect(rx + 4, pry, 4, kGlyphH, kPartBar);

                // Row text (manual number char)
                char numBuf[4] = {'0' + (char)(pi + 1), '\0', '\0', '\0'};
                appDrawText(rx + 10, pry, numBuf, kText);

                // Type hex
                char typeBuf[8];
                typeBuf[0] = '0'; typeBuf[1] = 'x';
                static const char hex[] = "0123456789ABCDEF";
                typeBuf[2] = hex[(p.type >> 4) & 0xF];
                typeBuf[3] = hex[p.type & 0xF];
                typeBuf[4] = '\0';
                appDrawText(rx + 22, pry, typeBuf, kSubText);

                // LBA start (decimal, hand-rolled)
                char lbaBuf[16]; int li = 0;
                if (p.lbaStart == 0) { lbaBuf[li++] = '0'; }
                else { uint32_t v = p.lbaStart; char rev[12]; int ri = 0;
                       while (v > 0) { rev[ri++] = '0' + (int)(v % 10); v /= 10; }
                       for (int j = ri - 1; j >= 0; j--) lbaBuf[li++] = rev[j]; }
                lbaBuf[li] = '\0';
                appDrawText(rx + 60, pry, lbaBuf, kSubText);

                // Sector count
                char scBuf[16]; int si = 0;
                if (p.lbaCount == 0) { scBuf[si++] = '0'; }
                else { uint32_t v = p.lbaCount; char rev[12]; int ri = 0;
                       while (v > 0) { rev[ri++] = '0' + (int)(v % 10); v /= 10; }
                       for (int j = ri - 1; j >= 0; j--) scBuf[si++] = rev[j]; }
                scBuf[si] = '\0';
                appDrawText(rx + 120, pry, scBuf, kSubText);

                // FS label
                appDrawText(rx + 190, pry, p.fsLabel, kText);

                // Boot flag
                if (p.bootable) appDrawText(rx + 240, pry, "*", kAccent);
            }

            // Partition bar at bottom
            if (d.partCount > 0 && d.totalSectors > 0) {
                uint32_t barY = y + h - 60;
                uint32_t barX = rx + 4;
                uint32_t barW = (rw > 16) ? rw - 16 : 0;
                framebuffer::fill_rect(barX, barY, barW, 18, kPanel);

                for (int pi = 0; pi < d.partCount; pi++) {
                    const PartEntry& p = d.parts[pi];
                    uint32_t pxOff = (uint32_t)((uint64_t)p.lbaStart * barW / d.totalSectors);
                    uint32_t pxW   = (uint32_t)((uint64_t)p.lbaCount * barW / d.totalSectors);
                    if (pxW < 2) pxW = 2;
                    uint32_t col = (pi % 2 == 0) ? kPartBar : kPartBarB;
                    framebuffer::fill_rect(barX + pxOff, barY, pxW, 18, col);
                }

                appDrawText(rx + 4, barY + 22, "Partition map", kSubText);
            }
        } else {
            appDrawText(rx + 4, dy, "No disk info available", kSubText);
        }
    }
}

void DiskManagerApp::onMouseDown(int localX, int localY, uint8_t button) {
    (void)button;
    const uint32_t leftW = 200;
    const uint32_t listTop = 46;
    const uint32_t rowH = 28;

    if ((uint32_t)localX < leftW && (uint32_t)localY >= listTop) {
        int idx = ((uint32_t)localY - listTop) / rowH;
        if (idx >= 0 && idx < m_diskCount) {
            m_selectedDisk = idx;
            invalidate();
        }
    }
}

void DiskManagerApp::onWidgetClick(int widgetId) {
    if (widgetId == m_refreshBtnId) {
        serial::puts("[DISKMANAGER] Manual refresh\n");
        scanDisks();
        invalidate();
    }
}

TrashApp::TrashApp()
    : m_entryCount(0), m_selectedIndex(-1), m_emptyBtnId(-1), m_confirmEmptyBtnId(-1), m_cancelEmptyBtnId(-1),
      m_restoreBtnId(-1), m_restoreAllBtnId(-1), m_deletePermanentBtnId(-1), m_refreshBtnId(-1), m_propertiesBtnId(-1),
      m_confirmEmpty(false), m_showProperties(false), m_startWithConfirmEmpty(false)
{
    strcopy(m_name, "Trash", app::MAX_APP_NAME);
    m_status[0] = '\0';
}

TrashApp::~TrashApp()
{
}

bool TrashApp::init()
{
    m_window = new app::KernelWindow();
    if (!m_window) return false;

    strcopy(m_window->title, "Trash", app::MAX_TITLE_LEN);
    m_window->x = 140;
    m_window->y = 90;
    m_window->w = 420;
    m_window->h = 240;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_RESIZABLE | app::WF_FOCUSED;
    m_window->owner = this;

    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }

    m_restoreBtnId = addButton(18, 6, 64, 20, "Restore");
    m_restoreAllBtnId = addButton(86, 6, 76, 20, "Restore All");
    m_deletePermanentBtnId = addButton(166, 6, 86, 20, "Delete Perm");
    m_emptyBtnId = addButton(256, 6, 60, 20, "Empty");
    m_refreshBtnId = addButton(320, 6, 58, 20, "Refresh");
    m_propertiesBtnId = addButton(276, 188, 82, 22, "Properties");
    m_confirmEmptyBtnId = addButton(92, 146, 104, 22, "Empty Trash");
    m_cancelEmptyBtnId = addButton(214, 146, 70, 22, "Cancel");
    refreshEntries();
    if (m_startWithConfirmEmpty) {
        if (m_entryCount == 0) {
            strcopy(m_status, "Trash is already empty.", sizeof(m_status));
            m_confirmEmpty = false;
        } else {
            m_status[0] = '\0';
            m_confirmEmpty = true;
        }
        m_startWithConfirmEmpty = false;
    }
    updateButtons();
    kernel_desktop_refresh_trash_state();
    m_state = app::AppState::Running;
    return true;
}

bool TrashApp::initWithParam(const char* param)
{
    m_startWithConfirmEmpty = param && strcmp(param, "--confirm-empty") == 0;
    return init();
}

void TrashApp::shutdown()
{
    m_state = app::AppState::Terminated;
}

void TrashApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    framebuffer::fill_rect(x, y, w, h, rgb(38, 40, 46));
    if (w > 32 && h > 36) {
        framebuffer::fill_rect(x + 16, y + 18, w - 32, h - 36, rgb(28, 30, 36));
    }
    if (m_entryCount == 0) {
        appDrawText(x + 26, y + 34, "Trash is empty.", rgb(220, 225, 235));
        appDrawText(x + 26, y + 58, m_status[0] ? m_status : "Deleted files will appear here.", rgb(165, 170, 185));
        if (m_confirmEmpty) m_confirmEmpty = false;
        updateButtons();
        return;
    }

    char countText[48];
    countText[0] = '\0';
    strcopy(countText, "Trash contains ", sizeof(countText));
    char digits[12];
    int di = 0;
    int value = m_entryCount;
    char rev[12];
    int ri = 0;
    if (value == 0) rev[ri++] = '0';
    while (value > 0) { rev[ri++] = (char)('0' + (value % 10)); value /= 10; }
    while (ri > 0) digits[di++] = rev[--ri];
    digits[di] = '\0';
    strappend(countText, digits, sizeof(countText));
    strappend(countText, " item(s).", sizeof(countText));
    appDrawText(x + 26, y + 34, countText, rgb(220, 225, 235));

    appDrawText(x + 46, y + 52, "Name", rgb(190, 195, 205));
    appDrawText(x + 156, y + 52, "Original", rgb(190, 195, 205));
    appDrawText(x + 238, y + 52, "Size", rgb(190, 195, 205));
    appDrawText(x + 286, y + 52, "Type", rgb(190, 195, 205));
    appDrawText(x + 350, y + 52, "Deleted", rgb(190, 195, 205));

    for (int i = 0; i < m_entryCount && i < 6; ++i) {
        uint32_t rowY = y + 70 + (uint32_t)i * 20;
        if (i == m_selectedIndex) framebuffer::fill_rect(x + 22, rowY - 2, 374, 18, rgb(70, 90, 135));
        if (!FileExplorerApp::drawThemedIcon(x + 26, rowY - 2, 16, iconForEntry(m_entries[i]))) FileExplorerApp::drawPlaceholderIcon(x + 26, rowY - 2, 16);
        appDrawText(x + 46, rowY + 2, m_entries[i].name, rgb(220, 225, 235));
        appDrawText(x + 156, rowY + 2, m_entries[i].originalFolder, rgb(165, 170, 185));
        if (m_entries[i].isDir) appDrawText(x + 238, rowY + 2, "Folder", rgb(165, 170, 185));
        else {
            char sizeText[24];
            formatSize(m_entries[i].size, sizeText, sizeof(sizeText));
            appDrawText(x + 238, rowY + 2, sizeText, rgb(165, 170, 185));
        }
        appDrawText(x + 286, rowY + 2, typeForEntry(m_entries[i]), rgb(165, 170, 185));
        appDrawText(x + 350, rowY + 2, m_entries[i].deletedText, rgb(165, 170, 185));
    }

    if (m_status[0]) {
        appDrawText(x + 26, y + 182, m_status, rgb(185, 190, 205));
    }

    if (m_confirmEmpty) {
        framebuffer::fill_rect(x + 64, y + 70, 292, 104, rgb(55, 48, 48));
        appDrawText(x + 84, y + 88, "Empty Trash?", rgb(240, 230, 230));
        appDrawText(x + 84, y + 112, "This will permanently delete all", rgb(210, 205, 205));
        appDrawText(x + 84, y + 130, "items in Trash.", rgb(210, 205, 205));
    }
    if (m_showProperties && m_selectedIndex >= 0 && m_selectedIndex < m_entryCount) {
        TrashEntry& item = m_entries[m_selectedIndex];
        framebuffer::fill_rect(x + 54, y + 52, 312, 138, rgb(45, 45, 55));
        appDrawText(x + 74, y + 70, "Properties", rgb(230, 235, 245));
        appDrawText(x + 74, y + 94, item.name, rgb(200, 205, 215));
        appDrawText(x + 74, y + 112, typeForEntry(item), rgb(200, 205, 215));
        appDrawText(x + 74, y + 130, item.originalPath, rgb(200, 205, 215));
        appDrawText(x + 74, y + 148, item.trashRoot, rgb(200, 205, 215));
        appDrawText(x + 74, y + 166, item.deletedText, rgb(200, 205, 215));
    }
    updateButtons();
}

void TrashApp::onWidgetClick(int widgetId)
{
    if (widgetId == m_restoreBtnId) {
        restoreSelected();
        return;
    }
    if (widgetId == m_restoreAllBtnId) {
        restoreAll();
        return;
    }
    if (widgetId == m_deletePermanentBtnId) {
        deleteSelectedPermanently();
        return;
    }
    if (widgetId == m_refreshBtnId) {
        refreshEntries();
        strcopy(m_status, "Refreshed.", sizeof(m_status));
        updateButtons();
        invalidate();
        return;
    }
    if (widgetId == m_propertiesBtnId) {
        m_showProperties = (m_selectedIndex >= 0 && m_selectedIndex < m_entryCount) && !m_showProperties;
        updateButtons();
        invalidate();
        return;
    }
    if (widgetId == m_emptyBtnId) {
        serial::puts("[trash] Empty Trash requested\n");
        refreshEntries();
        if (m_entryCount == 0) {
            strcopy(m_status, "Trash is already empty.", sizeof(m_status));
            m_confirmEmpty = false;
        } else {
            m_status[0] = '\0';
            m_confirmEmpty = true;
        }
        updateButtons();
        invalidate();
        return;
    }

    if (widgetId == m_confirmEmptyBtnId) {
        serial::puts("[trash] Empty Trash confirmed\n");
        int deleted = 0;
        m_confirmEmpty = false;
        if (purgeContents(&deleted)) {
            strcopy(m_status, "Trash emptied.", sizeof(m_status));
        } else {
            strcopy(m_status, "Empty Trash had errors.", sizeof(m_status));
        }
        refreshEntries();
        updateButtons();
        kernel_desktop_refresh_trash_state();
        invalidate();
        return;
    }

    if (widgetId == m_cancelEmptyBtnId) {
        serial::puts("[trash] Empty Trash canceled\n");
        m_confirmEmpty = false;
        strcopy(m_status, "Empty Trash canceled.", sizeof(m_status));
        updateButtons();
        invalidate();
        return;
    }
}

void TrashApp::refreshEntries()
{
    m_entryCount = 0;

    for (uint8_t mountIndex = 0; mountIndex < vfs::VFS_MAX_MOUNTS && m_entryCount < MAX_TRASH_ENTRIES; ++mountIndex) {
        const vfs::MountPoint* mp = vfs::get_mount_by_index(mountIndex);
        if (!mp || !mp->active) continue;

        char trashRoot[256];
        kernel_trash_root_for_mount(mp->path, trashRoot, sizeof(trashRoot));
        uint8_t dir = vfs::opendir(trashRoot);
        if (dir == 0xFF) continue;

        vfs::DirEntry entry{};
        while (m_entryCount < MAX_TRASH_ENTRIES && vfs::readdir(dir, &entry)) {
            if (entry.name[0] == '.' && (entry.name[1] == '\0' || (entry.name[1] == '.' && entry.name[2] == '\0'))) continue;
            if (file_clipboard::is_trash_metadata_name(entry.name)) continue;

            TrashEntry& item = m_entries[m_entryCount];
            strcopy(item.name, entry.name, sizeof(item.name));
            strcopy(item.trashRoot, trashRoot, sizeof(item.trashRoot));
            item.isDir = entry.type == vfs::FILE_TYPE_DIRECTORY;
            item.size = entry.size;
            item.originalPath[0] = '\0';
            item.originalFolder[0] = '\0';
            item.type[0] = '\0';
            item.iconKey[0] = '\0';
            strcopy(item.deletedText, "Unknown", sizeof(item.deletedText));

            char itemPath[256];
            char infoPath[256];
            kernel_join_path(item.trashRoot, entry.name, itemPath, sizeof(itemPath));
            kernel_trash_info_path_for(itemPath, infoPath, sizeof(infoPath));
            char metadata[512];
            int32_t bytesRead = vfs::read_file(infoPath, metadata, sizeof(metadata) - 1);
            if (bytesRead > 0) {
                metadata[bytesRead] = '\0';
                const char* key = "\"originalPath\": \"";
                for (int i = 0; metadata[i]; ++i) {
                    if (startsWithText(metadata + i, key)) {
                        i += strlen_local(key);
                        int pi = 0;
                        while (metadata[i] && metadata[i] != '"' && pi < (int)sizeof(item.originalPath) - 1) {
                            item.originalPath[pi++] = metadata[i++];
                        }
                        item.originalPath[pi] = '\0';
                        break;
                    }
                }
                const char* timeKey = "\"trashedAt\": ";
                for (int i = 0; metadata[i]; ++i) {
                    if (startsWithText(metadata + i, timeKey)) {
                        strcopy(item.deletedText, "Recently", sizeof(item.deletedText));
                        break;
                    }
                }
            }
            if (!item.originalPath[0]) {
                char restoreFolder[256];
                parentPathOf(item.trashRoot, restoreFolder, sizeof(restoreFolder));
                if (!kernel_join_path_within_base(restoreFolder, entry.name, item.originalPath, sizeof(item.originalPath))) {
                    item.originalPath[0] = '\0';
                }
            }
            parentPathOf(item.originalPath, item.originalFolder, sizeof(item.originalFolder));
            strcopy(item.type, typeForEntry(item), sizeof(item.type));
            strcopy(item.iconKey, iconForEntry(item), sizeof(item.iconKey));

            ++m_entryCount;
        }
        vfs::closedir(dir);
    }

    if (m_selectedIndex >= m_entryCount) m_selectedIndex = m_entryCount - 1;
    if (m_entryCount == 0) m_selectedIndex = -1;
    else if (m_selectedIndex < 0) m_selectedIndex = 0;
    serial::puts("[trash] item count computed=");
    serial_put_dec((uint32_t)m_entryCount);
    serial::puts("\n");
}
bool TrashApp::purgeContents(int* deletedCount)
{
    if (deletedCount) *deletedCount = 0;
    refreshEntries();
    if (m_entryCount == 0) return true;

    bool ok = true;
    for (int i = 0; i < m_entryCount; ++i) {
        char itemPath[256];
        char infoPath[256];
        if (!kernel_join_path_within_base(m_entries[i].trashRoot, m_entries[i].name, itemPath, sizeof(itemPath))) {
            serial::puts("[trash] refusing unsafe purge path\n");
            ok = false;
            continue;
        }
        kernel_trash_info_path_for(itemPath, infoPath, sizeof(infoPath));

        int rootLen = strlen_local(m_entries[i].trashRoot);
        if (!startsWithText(itemPath, m_entries[i].trashRoot) || itemPath[rootLen] != '/') {
            serial::puts("[trash] refusing unsafe purge path\n");
            ok = false;
            continue;
        }

        vfs::Status deleteStatus = m_entries[i].isDir ? vfs::rmdir(itemPath) : vfs::unlink(itemPath);
        if (deleteStatus == vfs::VFS_OK) {
            if (deletedCount) ++(*deletedCount);
            serial::puts("[trash] purged item=");
            serial::puts(itemPath);
            serial::puts("\n");
        } else {
            serial::puts("[trash] purge failed item=");
            serial::puts(itemPath);
            serial::puts(" result=");
            serial::puts(kernel_vfs_status_text(deleteStatus));
            serial::puts("\n");
            ok = false;
        }

        vfs::Status infoStatus = vfs::unlink(infoPath);
        if (infoStatus == vfs::VFS_OK) {
            serial::puts("[trash] purged metadata=ok\n");
        }
    }

    int finalCount = 0;
    for (uint8_t mountIndex = 0; mountIndex < vfs::VFS_MAX_MOUNTS; ++mountIndex) {
        const vfs::MountPoint* mp = vfs::get_mount_by_index(mountIndex);
        if (!mp || !mp->active) continue;
        char trashRoot[256];
        kernel_trash_root_for_mount(mp->path, trashRoot, sizeof(trashRoot));
        uint8_t dir = vfs::opendir(trashRoot);
        if (dir == 0xFF) continue;
        vfs::DirEntry entry{};
        while (vfs::readdir(dir, &entry)) {
            if (entry.name[0] == '.' && (entry.name[1] == '\0' || (entry.name[1] == '.' && entry.name[2] == '\0'))) continue;
            if (file_clipboard::is_trash_metadata_name(entry.name)) continue;
            ++finalCount;
        }
        vfs::closedir(dir);
    }
    serial::puts("[trash] purge complete final item count=");
    serial_put_dec((uint32_t)finalCount);
    serial::puts("\n");
    return ok;
}
void TrashApp::updateButtons()
{
    bool hasSelection = m_selectedIndex >= 0 && m_selectedIndex < m_entryCount;
    app::Widget* empty = getWidget(m_emptyBtnId);
    app::Widget* confirm = getWidget(m_confirmEmptyBtnId);
    app::Widget* cancel = getWidget(m_cancelEmptyBtnId);
    app::Widget* restore = getWidget(m_restoreBtnId);
    app::Widget* restoreAll = getWidget(m_restoreAllBtnId);
    app::Widget* deletePermanent = getWidget(m_deletePermanentBtnId);
    app::Widget* refresh = getWidget(m_refreshBtnId);
    app::Widget* properties = getWidget(m_propertiesBtnId);
    if (empty) empty->visible = m_entryCount > 0 && !m_confirmEmpty;
    if (confirm) confirm->visible = m_confirmEmpty;
    if (cancel) cancel->visible = m_confirmEmpty;
    if (restore) restore->visible = hasSelection && !m_confirmEmpty;
    if (restoreAll) restoreAll->visible = m_entryCount > 0 && !m_confirmEmpty;
    if (deletePermanent) deletePermanent->visible = hasSelection && !m_confirmEmpty;
    if (refresh) refresh->visible = !m_confirmEmpty;
    if (properties) properties->visible = hasSelection && !m_confirmEmpty;
}

void TrashApp::restoreSelected()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_entryCount) {
        strcopy(m_status, "Select an item to restore.", sizeof(m_status));
        invalidate();
        return;
    }
    if (restoreEntry(m_entries[m_selectedIndex])) strcopy(m_status, "Restored item.", sizeof(m_status));
    else strcopy(m_status, "Restore failed.", sizeof(m_status));
    refreshEntries();
    updateButtons();
    kernel_desktop_refresh_trash_state();
    invalidate();
}

void TrashApp::restoreAll()
{
    refreshEntries();
    int restored = 0;
    int failed = 0;
    for (int i = 0; i < m_entryCount; ++i) {
        if (restoreEntry(m_entries[i])) ++restored;
        else ++failed;
    }
    strcopy(m_status, "Restored: ", sizeof(m_status));
    char digits[12];
    int di = 0;
    int value = restored;
    char rev[12];
    int ri = 0;
    if (value == 0) rev[ri++] = '0';
    while (value > 0) { rev[ri++] = (char)('0' + (value % 10)); value /= 10; }
    while (ri > 0) digits[di++] = rev[--ri];
    digits[di] = '\0';
    strappend(m_status, digits, sizeof(m_status));
    strappend(m_status, " Failed: ", sizeof(m_status));
    di = 0; ri = 0; value = failed;
    if (value == 0) rev[ri++] = '0';
    while (value > 0) { rev[ri++] = (char)('0' + (value % 10)); value /= 10; }
    while (ri > 0) digits[di++] = rev[--ri];
    digits[di] = '\0';
    strappend(m_status, digits, sizeof(m_status));
    refreshEntries();
    updateButtons();
    kernel_desktop_refresh_trash_state();
    invalidate();
}

void TrashApp::deleteSelectedPermanently()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_entryCount) {
        strcopy(m_status, "Select an item to delete.", sizeof(m_status));
        invalidate();
        return;
    }
    if (deleteEntryPermanently(m_entries[m_selectedIndex])) strcopy(m_status, "Deleted permanently.", sizeof(m_status));
    else strcopy(m_status, "Delete failed.", sizeof(m_status));
    refreshEntries();
    updateButtons();
    kernel_desktop_refresh_trash_state();
    invalidate();
}

bool TrashApp::restoreEntry(const TrashEntry& entry)
{
    char sourcePath[256];
    if (!kernel_join_path_within_base(entry.trashRoot, entry.name, sourcePath, sizeof(sourcePath))) {
        serial::puts("[trash] refusing unsafe restore path\n");
        return false;
    }
    char targetPath[256];
    makeUniqueRestorePath(entry.originalPath, targetPath, sizeof(targetPath));
    vfs::Status status = vfs::rename(sourcePath, targetPath);
    if (status != vfs::VFS_OK) {
        serial::puts("[trash] restore failed\n");
        return false;
    }
    char infoPath[256];
    kernel_trash_info_path_for(sourcePath, infoPath, sizeof(infoPath));
    vfs::unlink(infoPath);
    serial::puts("[trash] restored item\n");
    return true;
}

bool TrashApp::deleteEntryPermanently(const TrashEntry& entry)
{
    char itemPath[256];
    char infoPath[256];
    if (!kernel_join_path_within_base(entry.trashRoot, entry.name, itemPath, sizeof(itemPath))) return false;
    kernel_trash_info_path_for(itemPath, infoPath, sizeof(infoPath));
    if (!startsWithText(itemPath, entry.trashRoot) || itemPath[strlen_local(entry.trashRoot)] != '/') return false;
    vfs::Status status = entry.isDir ? vfs::rmdir(itemPath) : vfs::unlink(itemPath);
    if (status != vfs::VFS_OK) return false;
    vfs::unlink(infoPath);
    return true;
}

void TrashApp::parentPathOf(const char* path, char* out, int outSize) const
{
    if (!out || outSize <= 0) return;
    if (!path || !path[0] || (path[0] == '/' && path[1] == '\0')) {
        strcopy(out, "/", outSize);
        return;
    }
    int len = strlen_local(path);
    while (len > 1 && path[len - 1] == '/') --len;
    int slash = len - 1;
    while (slash > 0 && path[slash] != '/') --slash;
    int copyLen = slash == 0 ? 1 : slash;
    if (copyLen >= outSize) copyLen = outSize - 1;
    for (int i = 0; i < copyLen; ++i) out[i] = path[i];
    out[copyLen] = '\0';
}

void TrashApp::basenameOf(const char* path, char* out, int outSize) const
{
    if (!out || outSize <= 0) return;
    const char* base = path;
    for (int i = 0; path && path[i]; ++i) if (path[i] == '/') base = path + i + 1;
    strcopy(out, base && base[0] ? base : "RESTORE", outSize);
}

void TrashApp::makeUniqueRestorePath(const char* desiredPath, char* out, int outSize) const
{
    strcopy(out, desiredPath && desiredPath[0] ? desiredPath : "/RESTORE", outSize);
    if (!vfs::exists(out)) return;
    char parent[256];
    char name[128];
    parentPathOf(out, parent, sizeof(parent));
    basenameOf(out, name, sizeof(name));
    for (int i = 1; i < 100; ++i) {
        char candidate[128];
        kernel_make_fat_safe_collision_name(name, false, i, candidate, sizeof(candidate));
        kernel_join_path(parent, candidate, out, outSize);
        if (!vfs::exists(out)) return;
    }
}

void TrashApp::formatSize(uint64_t size, char* out, int outSize) const
{
    if (!out || outSize <= 0) return;
    uint64_t value = size;
    const char* suffix = " B";
    if (size >= 1024 * 1024) { value = size / (1024 * 1024); suffix = " MB"; }
    else if (size >= 1024) { value = size / 1024; suffix = " KB"; }
    char digits[24];
    int d = 0;
    if (value == 0) digits[d++] = '0';
    else {
        char tmp[24];
        int t = 0;
        while (value > 0 && t < 23) { tmp[t++] = '0' + (value % 10); value /= 10; }
        while (t > 0) digits[d++] = tmp[--t];
    }
    digits[d] = '\0';
    strcopy(out, digits, outSize);
    strappend(out, suffix, outSize);
}

const char* TrashApp::iconForEntry(const TrashEntry& entry) const
{
    if (entry.isDir) return "file.folder";
    if (endsWithIgnoreCaseLocal(entry.name, ".txt") || endsWithIgnoreCaseLocal(entry.name, ".log") || endsWithIgnoreCaseLocal(entry.name, ".cfg") || endsWithIgnoreCaseLocal(entry.name, ".ini") || endsWithIgnoreCaseLocal(entry.name, ".md")) return "file.text";
    if (endsWithIgnoreCaseLocal(entry.name, ".bmp") || endsWithIgnoreCaseLocal(entry.name, ".png") || endsWithIgnoreCaseLocal(entry.name, ".jpg") || endsWithIgnoreCaseLocal(entry.name, ".jpeg")) return "file.image";
    if (endsWithIgnoreCaseLocal(entry.name, ".elf") || endsWithIgnoreCaseLocal(entry.name, ".gxapp") || endsWithIgnoreCaseLocal(entry.name, ".gxq") || endsWithIgnoreCaseLocal(entry.name, ".exe")) return "app.files";
    if (endsWithIgnoreCaseLocal(entry.name, ".bin") || endsWithIgnoreCaseLocal(entry.name, ".dat") || endsWithIgnoreCaseLocal(entry.name, ".dll") || endsWithIgnoreCaseLocal(entry.name, ".so") || endsWithIgnoreCaseLocal(entry.name, ".o")) return "file.binary";
    return "file.unknown";
}

const char* TrashApp::typeForEntry(const TrashEntry& entry) const
{
    if (entry.isDir) return "Folder";
    if (endsWithIgnoreCaseLocal(entry.name, ".txt") || endsWithIgnoreCaseLocal(entry.name, ".log") || endsWithIgnoreCaseLocal(entry.name, ".cfg") || endsWithIgnoreCaseLocal(entry.name, ".ini") || endsWithIgnoreCaseLocal(entry.name, ".md")) return "Text";
    if (endsWithIgnoreCaseLocal(entry.name, ".bmp") || endsWithIgnoreCaseLocal(entry.name, ".png") || endsWithIgnoreCaseLocal(entry.name, ".jpg") || endsWithIgnoreCaseLocal(entry.name, ".jpeg")) return "Image";
    if (endsWithIgnoreCaseLocal(entry.name, ".elf") || endsWithIgnoreCaseLocal(entry.name, ".gxapp") || endsWithIgnoreCaseLocal(entry.name, ".gxq") || endsWithIgnoreCaseLocal(entry.name, ".exe")) return "App";
    if (endsWithIgnoreCaseLocal(entry.name, ".bin") || endsWithIgnoreCaseLocal(entry.name, ".dat") || endsWithIgnoreCaseLocal(entry.name, ".dll") || endsWithIgnoreCaseLocal(entry.name, ".so") || endsWithIgnoreCaseLocal(entry.name, ".o")) return "Binary";
    return "File";
}

// ============================================================
// Image Viewer App Implementation
// ============================================================

ImageViewerApp::ImageViewerApp()
    : m_hasImage(false)
{
    strcopy(m_name, "ImageViewer", app::MAX_APP_NAME);
    m_imagePath[0] = '\0';
    m_status[0] = '\0';
    m_image.status = gxos::gui::ImageLoadStatus::NotFound;
    m_image.pixels = nullptr;
    m_image.width = 0;
    m_image.height = 0;
}

ImageViewerApp::~ImageViewerApp() {
    shutdown();
}

bool ImageViewerApp::init() {
    return initWithParam(nullptr);
}

bool ImageViewerApp::initWithParam(const char* imagePath) {
    m_window = new app::KernelWindow();
    strcopy(m_window->title, "Image Viewer", app::MAX_TITLE_LEN);
    m_window->x = 96;
    m_window->y = 54;
    m_window->w = 820;
    m_window->h = 620;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_RESIZABLE | app::WF_FOCUSED;
    m_window->owner = this;

    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }

    loadImage(imagePath);
    m_state = app::AppState::Running;
    return true;
}

void ImageViewerApp::shutdown() {
    gxos::gui::ImageAdapter::Release(m_image);
    m_hasImage = false;
    m_imagePath[0] = '\0';
    m_status[0] = '\0';
    m_state = app::AppState::Terminated;
}

void ImageViewerApp::loadImage(const char* path) {
    gxos::gui::ImageAdapter::Release(m_image);
    m_hasImage = false;
    m_image.status = gxos::gui::ImageLoadStatus::NotFound;
    m_image.pixels = nullptr;
    m_image.width = 0;
    m_image.height = 0;
    m_imagePath[0] = '\0';

    if (!path || !path[0]) {
        strcopy(m_status, "Open a PNG from File Explorer to preview it here.", sizeof(m_status));
        return;
    }

    vfs::FileInfo info{};
    const bool haveFileInfo = vfs::stat(path, &info) == vfs::VFS_OK;
    strcopy(m_imagePath, path, sizeof(m_imagePath));
#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] app load begin path=");
    serial::puts(m_imagePath);
    serial::puts("\n");
#endif
    m_image = gxos::gui::ImageAdapter::LoadFromFile(path);
#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] app load end path=");
    serial::puts(m_imagePath[0] ? m_imagePath : "(none)");
    serial::puts(" status=");
    serial::puts(m_image.status == gxos::gui::ImageLoadStatus::Ok ? "Loaded" : gxos::gui::ImageLoadStatusName(m_image.status));
    serial::puts(" dims=");
    char debugWidth[16];
    char debugHeight[16];
    nav_int_to_text((int)m_image.width, debugWidth, sizeof(debugWidth));
    nav_int_to_text((int)m_image.height, debugHeight, sizeof(debugHeight));
    serial::puts(debugWidth);
    serial::putc('x');
    serial::puts(debugHeight);
    serial::puts("\n");
#endif
    if (m_image.status == gxos::gui::ImageLoadStatus::Ok && m_image.pixels && m_image.width > 0 && m_image.height > 0) {
        m_hasImage = true;
        char widthText[16];
        char heightText[16];
        nav_int_to_text((int)m_image.width, widthText, sizeof(widthText));
        nav_int_to_text((int)m_image.height, heightText, sizeof(heightText));
        strcopy(m_status, "Loaded PNG preview: ", sizeof(m_status));
        strappend(m_status, widthText, sizeof(m_status));
        strappend(m_status, "x", sizeof(m_status));
        strappend(m_status, heightText, sizeof(m_status));
    } else {
        strcopy(m_status, "Unable to load PNG: ", sizeof(m_status));
        strappend(m_status, gxos::gui::ImageLoadStatusName(m_image.status), sizeof(m_status));
    }

#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    {
        char widthText[16];
        char heightText[16];
        char sizeText[32];
        nav_int_to_text((int)m_image.width, widthText, sizeof(widthText));
        nav_int_to_text((int)m_image.height, heightText, sizeof(heightText));
        if (haveFileInfo) {
            nav_i64_to_text((int64_t)info.size, sizeText, sizeof(sizeText));
        } else {
            strcopy(sizeText, "unknown", sizeof(sizeText));
        }
        serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] load path=");
        serial::puts(m_imagePath[0] ? m_imagePath : "(none)");
        serial::puts(" sizeBytes=");
        serial::puts(sizeText);
        serial::puts(" dims=");
        serial::puts(widthText);
        serial::putc('x');
        serial::puts(heightText);
        serial::puts(" status=");
        serial::puts(m_image.status == gxos::gui::ImageLoadStatus::Ok ? "Loaded" : gxos::gui::ImageLoadStatusName(m_image.status));
        serial::puts("\n");
    }
#endif
}

void ImageViewerApp::drawPlaceholder(uint32_t x, uint32_t y, uint32_t w, uint32_t h) const {
    framebuffer::fill_rect(x, y, w, h, rgb(34, 36, 44));
    if (w > 16 && h > 16) {
        framebuffer::fill_rect(x + 8, y + 8, w - 16, h - 16, rgb(22, 24, 30));
    }
    appDrawText(x + 18, y + 18, "Image Viewer", rgb(240, 242, 248));
    appDrawText(x + 18, y + 36, "Bare-metal PNG preview", rgb(180, 190, 205));
    appDrawText(x + 18, y + 58, m_status[0] ? m_status : "Open a PNG from File Explorer to preview it here.", rgb(210, 214, 226));
    if (m_imagePath[0]) {
        appDrawText(x + 18, y + 80, m_imagePath, rgb(165, 175, 192));
    }
}

void ImageViewerApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    framebuffer::fill_rect(x, y, w, h, rgb(30, 32, 40));

    const uint32_t statusH = 22;
    const uint32_t pad = 12;
    uint32_t contentX = x + pad;
    uint32_t contentY = y + pad;
    uint32_t contentW = w > pad * 2 ? w - pad * 2 : w;
    uint32_t contentH = h > pad * 2 + statusH ? h - pad * 2 - statusH : (h > statusH ? h - statusH : h);

    if (m_hasImage && m_image.status == gxos::gui::ImageLoadStatus::Ok && m_image.width > 0 && m_image.height > 0 && contentW > 0 && contentH > 0) {
        uint32_t drawW = contentW;
        uint32_t drawH = (uint32_t)(((uint64_t)drawW * m_image.height) / m_image.width);
        if (drawH > contentH) {
            drawH = contentH;
            drawW = (uint32_t)(((uint64_t)drawH * m_image.width) / m_image.height);
        }
        if (drawW == 0) drawW = 1;
        if (drawH == 0) drawH = 1;
        uint32_t drawX = contentX + (contentW > drawW ? (contentW - drawW) / 2 : 0);
        uint32_t drawY = contentY + (contentH > drawH ? (contentH - drawH) / 2 : 0);
        framebuffer::fill_rect(drawX, drawY, drawW, drawH, rgb(24, 26, 32));
        framebuffer::fill_rect(drawX > 1 ? drawX - 1 : drawX, drawY > 1 ? drawY - 1 : drawY, drawW + 2, drawH + 2, rgb(74, 82, 98));
        gxos::gui::ImageAdapter::DrawToFramebuffer(m_image, drawX, drawY, drawW, drawH);
    } else {
        drawPlaceholder(contentX, contentY, contentW, contentH);
    }

    framebuffer::fill_rect(x, y + h - statusH, w, statusH, rgb(42, 46, 58));
    appDrawText(x + 12, y + h - 15, m_status[0] ? m_status : "Bare-metal PNG preview ready", rgb(222, 226, 236));

#if defined(GXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
    static bool s_runtimeSmokePaintLogged = false;
    if (!s_runtimeSmokePaintLogged) {
        s_runtimeSmokePaintLogged = true;
        char widthText[16];
        char heightText[16];
        nav_int_to_text((int)m_image.width, widthText, sizeof(widthText));
        nav_int_to_text((int)m_image.height, heightText, sizeof(heightText));
        serial::puts("[IMAGEVIEWER-RUNTIME-SMOKE] paint=");
        if (m_hasImage && m_image.status == gxos::gui::ImageLoadStatus::Ok && m_image.pixels && m_image.width > 0 && m_image.height > 0) {
            serial::puts("png");
        } else {
            serial::puts("placeholder");
        }
        serial::puts(" path=");
        serial::puts(m_imagePath[0] ? m_imagePath : "(none)");
        serial::puts(" dims=");
        serial::puts(widthText);
        serial::putc('x');
        serial::puts(heightText);
        serial::puts(" status=");
        // Report a human-readable Loaded/NotFound signal in the smoke log.
        serial::puts(m_image.status == gxos::gui::ImageLoadStatus::Ok ? "Loaded" : gxos::gui::ImageLoadStatusName(m_image.status));
        serial::puts("\n");
    }
#endif
}

// ============================================================
// Navigator App (baremetal)
//
// Kernel-side Navigator is a thin adapter for the hosted/compositor Navigator
// architecture. Keep shared document behavior aligned with guideWeb where
// possible, and report unavailable platform capabilities honestly.
// ============================================================

NavigatorApp::NavigatorApp()
    : m_blockCount(0), m_bookmarkCount(0), m_recentDownloadCount(0), m_backCount(0),
      m_forwardCount(0),
      m_addressFocused(false), m_addressCaret(0), m_ctrlPressed(false), m_scrollY(0), m_hoverLinkIndex(-1),
      m_selectionActive(false), m_selectionDragging(false), m_selectionMoved(false), m_mouseLeftDown(false),
      m_mouseMode(NAV_MOUSE_NONE), m_mouseDownLinkIndex(-1), m_mouseDownX(0), m_mouseDownY(0), m_mouseDragThresholdExceeded(false),
      m_backBtnId(-1), m_forwardBtnId(-1), m_reloadBtnId(-1), m_homeBtnId(-1),
      m_bookmarksBtnId(-1), m_addBookmarkBtnId(-1),
      m_loading(false), m_throbberFrame(0), m_loadingStartTick(0),
      m_focusedFormBlock(-1), m_formCaret(0)
{
    strcopy(m_status, "Ready", MAX_STATUS_LEN);
    strcopy(m_currentUrl, "about:navigator", MAX_URL_LEN);
    strcopy(m_title, "guideXOS Navigator", MAX_TITLE_LEN_NAV);
    m_addressBuffer[0] = '\0';
    m_metaRequestedUrl[0] = '\0';
    m_metaFinalUrl[0] = '\0';
    m_metaSourceType[0] = '\0';
    m_metaHttpStatusCode = 0;
    m_metaHttpReason[0] = '\0';
    m_metaContentType[0] = '\0';
    m_metaResponseFraming[0] = '\0';
    m_metaContentLength = 0;
    m_metaContentLengthPresent = false;
    m_metaTruncatedResponse = false;
    m_metaContentEncoding[0] = '\0';
    m_metaUnsupportedReason[0] = '\0';
    m_metaRedirected = false;
    m_metaRedirectCount = 0;
    m_metaErrorStatus[0] = '\0';
    m_metaHeaderCapHit = false;
    m_metaBodyCapHit = false;
    m_metaTlsSucceededBeforeContentFailure = false;
    m_metaDowngradeRedirectBlocked = false;
    m_metaSourcePreview[0] = '\0';
    m_metaSourceBytes = 0;
    m_metaSourceTruncated = false;
    m_metaDocumentBlocks = 0;
    m_metaImageBlocks = 0;
    m_metaLoadedImages = 0;
    m_metaFailedImages = 0;
    m_metaRemoteImages = 0;
    m_metaLocalImages = 0;
    m_metaLastImageError[0] = '\0';
    m_metaScheme[0] = '\0';
    m_metaDnsUsed = false;
    m_metaDnsHost[0] = '\0';
    m_metaDnsResolvedIp[0] = '\0';
    m_metaDnsError[0] = '\0';
    m_metaTlsUsed = false;
    m_metaTlsValidated = false;
    m_metaTlsHostnameValidated = false;
    m_metaTlsAllowlistLocalOnly = false;
    m_metaTlsVerifyFlags = 0;
    m_metaTlsBackend[0] = '\0';
    m_metaTransportSelection[0] = '\0';
    m_metaTlsStatus[0] = '\0';
    m_metaTransportPolicyReason[0] = '\0';
    m_metaTlsHostname[0] = '\0';
    m_metaTlsSniHost[0] = '\0';
    m_metaTlsProtocol[0] = '\0';
    m_metaTlsCipherSuite[0] = '\0';
    m_metaCssDetected = false;
    m_metaStyleRuleCount = 0;
    m_metaUnsupportedExternalStylesheetCount = 0;
    m_metaUnsupportedCssDeclarationCount = 0;
    m_metaCssStyleBlockCapped = false;
    m_metaCssStyleBytesProcessed = 0;
    m_metaDownloaded = false;
    m_metaDownloadSavedPath[0] = '\0';
    m_metaDownloadByteCount = 0;
    m_lastDownloadError[0] = '\0';
    m_lastSubmittedFormAction[0] = '\0';
    m_lastSubmittedFormMethod[0] = '\0';
    m_lastSubmittedFormStatus[0] = '\0';
    m_lastPostHttpStatus[0] = '\0';
    m_lastPostContentType[0] = '\0';
    m_lastPostBodyBytes = 0;
    m_lastFormError[0] = '\0';
    m_metaFormCount = 0;
    m_metaTextInputCount = 0;
    m_metaCheckboxCount = 0;
    m_metaRadioCount = 0;
    m_metaTextareaCount = 0;
    m_metaSelectCount = 0;
    m_metaSubmitCount = 0;
    m_metaUnsupportedFormCount = 0;
    m_bodyStyle = gxos::web::WebStyle{};
    m_selectionAnchor.blockIndex = -1;
    m_selectionAnchor.offset = 0;
    m_selectionFocus.blockIndex = -1;
    m_selectionFocus.offset = 0;
    m_clipboard[0] = '\0';
    strcopy(m_clipboardMode, "Navigator internal clipboard", sizeof(m_clipboardMode));
}

NavigatorApp::~NavigatorApp() {
}

bool NavigatorApp::init()
{
    if (m_window) return true;

    m_window = new app::KernelWindow();
    if (!m_window) return false;

    strcopy(m_name, "guideXOS Navigator", app::MAX_APP_NAME);
    strcopy(m_window->title, "guideXOS Navigator", app::MAX_TITLE_LEN);
    m_window->x = 96;
    m_window->y = 72;
    m_window->w = 920;
    m_window->h = 640;
    m_window->owner = this;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_RESIZABLE;
    compositor::KernelCompositor::registerWindow(m_window);
    m_state = app::AppState::Running;

    loadDefaultBookmarks();
    loadChromeImages();
    clearSelection();
    m_clipboard[0] = '\0';
    strcopy(m_clipboardMode, "Navigator internal clipboard", sizeof(m_clipboardMode));
    loadUrl("about:navigator");
    updateButtons();
    invalidate();
    return true;
}

void NavigatorApp::shutdown() {
}

void NavigatorApp::update()
{
    if (!m_loading) return;
    const uint32_t now = (uint32_t)kernel::pit::ticks();
    const int frame = (int)(((now - m_loadingStartTick) / 10u) % 12u);
    if (frame != m_throbberFrame) {
        m_throbberFrame = frame;
        invalidate();
    }
}

void NavigatorApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    framebuffer::fill_rect(x, y, w, h, 0xFFF6F8FB);
    framebuffer::fill_rect(x, y, w, TOOLBAR_H, 0xFF2B2F3A);
    framebuffer::fill_rect(x, y + TOOLBAR_H - 1, w, 1, 0xFF586076);

    const NavigatorToolbarLayout toolbarLayout = navigatorToolbarLayout((int)w);
    const int addressX = toolbarLayout.addressX;
    const int addressW = toolbarLayout.addressW;
    if (addressW > 0) {
        framebuffer::fill_rect(x + addressX, y + ADDRESS_Y, (uint32_t)addressW, ADDRESS_H, 0xFF161A22);
        framebuffer::fill_rect(x + addressX, y + ADDRESS_Y, (uint32_t)addressW, 1, m_addressFocused ? 0xFF6FA8FF : 0xFF6E7688);
        framebuffer::fill_rect(x + addressX, y + ADDRESS_Y + ADDRESS_H - 1, (uint32_t)addressW, 1, 0xFF11151D);
        framebuffer::fill_rect(x + addressX, y + ADDRESS_Y, 1, ADDRESS_H, m_addressFocused ? 0xFF6FA8FF : 0xFF6E7688);
        framebuffer::fill_rect(x + addressX + addressW - 1, y + ADDRESS_Y, 1, ADDRESS_H, 0xFF11151D);
        appDrawText(x + addressX + 8, y + ADDRESS_Y + 7,
                    m_addressFocused ? m_addressBuffer : m_currentUrl,
                    rgb(232, 236, 246));
        if (m_addressFocused) {
            int caretX = addressX + 8 + m_addressCaret * 6;
            if (caretX < addressX + addressW - 4) {
                framebuffer::fill_rect(x + (uint32_t)caretX, y + ADDRESS_Y + 4, 1, ADDRESS_H - 8, 0xFFE8ECF6);
            }
        }
    }

    const int throbberFrame = m_loading
        ? (int)((((uint32_t)kernel::pit::ticks() - m_loadingStartTick) / 10u) % 12u)
        : 0;
    if (m_loading && addressW >= 24 && m_throbberFrames[throbberFrame].status == gxos::gui::ImageLoadStatus::Ok) {
        gxos::gui::ImageAdapter::DrawToFramebuffer(m_throbberFrames[throbberFrame],
                                                   x + w - 46, y + ADDRESS_Y, 22, 22);
    }

    uint32_t contentTop = y + TOOLBAR_H + 6;
    uint32_t contentH = h > (uint32_t)(TOOLBAR_H + STATUS_H + 12) ? h - TOOLBAR_H - STATUS_H - 12 : 0;
    if (contentH > 0) {
        framebuffer::fill_rect(x + CONTENT_X, contentTop, w - CONTENT_X * 2, contentH, css_background_or(0xFFFAFBFD, m_bodyStyle));
        drawDocument(x, y, w, h);
    }

    if (maxScroll() > 0) {
        uint32_t trackX = x + w - 22;
        uint32_t trackY = contentTop + 4;
        uint32_t trackH = contentH > 8 ? contentH - 8 : contentH;
        framebuffer::fill_rect(trackX, trackY, 6, trackH, 0xFFE0E4EB);
        int thumbH = (int)((trackH * (contentH ? contentH : 1)) / (uint32_t)(maxScroll() + (int)contentH));
        if (thumbH < 20) thumbH = 20;
        int maxScrollValue = maxScroll();
        int thumbY = (int)trackY;
        if (maxScrollValue > 0 && (int)trackH > thumbH) {
            thumbY += ((int)trackH - thumbH) * m_scrollY / maxScrollValue;
        }
        framebuffer::fill_rect(trackX, (uint32_t)thumbY, 6, (uint32_t)thumbH, 0xFF848C9C);
    }

    framebuffer::fill_rect(x, y + h - STATUS_H, w, STATUS_H, 0xFF262A34);
    framebuffer::fill_rect(x, y + h - STATUS_H, w, 1, 0xFF586076);
    char statusLine[160];
    strcopy(statusLine, m_status, sizeof(statusLine));
    if (hasSelection()) {
        char selected[MAX_SOURCE_PREVIEW];
        if (selectedText(selected, sizeof(selected))) {
            char number[24];
            nav_int_to_text(strlen_local(selected), number, sizeof(number));
            if (statusLine[0]) strappend(statusLine, "   ", sizeof(statusLine));
            strappend(statusLine, "Selection: ", sizeof(statusLine));
            strappend(statusLine, number, sizeof(statusLine));
            strappend(statusLine, " chars", sizeof(statusLine));
        }
    }
    appDrawText(x + 10, y + h - STATUS_H + 8, statusLine, rgb(222, 226, 236));
}

bool NavigatorApp::smokeTypographyPhase7A()
{
    NavigatorApp app;
    app::KernelWindow smokeWindow;
    smokeWindow.w = 640;
    smokeWindow.h = 480;
    app.m_window = &smokeWindow;

    static const char kFixture[] =
        "<style>"
        "body{font-family:Roboto,sans-serif;font-size:12px;}"
        ".bold{font-weight:bold;}"
        ".italic{font-style:italic;}"
        ".bolditalic{font-weight:bold;font-style:italic;}"
        ".mono{font-family:monospace;}"
        ".fallback{font-family:missing-family,Roboto;}"
        ".unsupported{font-family:missing-family;}"
        "</style>"
        "<p class='normal'>Proportional normal text with descenders gyjpq.</p>"
        "<p class='bold'>Bold proportional text.</p>"
        "<p class='italic'>Italic proportional text.</p>"
        "<p class='bolditalic'>Bold italic proportional text.</p>"
        "<p class='fallback'>Fallback-list text remains visible.</p>"
        "<p class='unsupported'>Unsupported family text remains visible.</p>"
        "<pre class='mono'>for (i = 0; i &lt; 4; ++i)\n    puts(i);</pre>"
        "<a href='http://guidexos.test:8080/navigator-smoke/final.html'>Wrapped positioned link text</a>";

    app.parseHtmlDocument("http://guidexos.test:8080/navigator-smoke/typography-phase7a.html",
                         kFixture, "http", "text/html", 200, "OK");

    int normalIndex = -1;
    int boldIndex = -1;
    int italicIndex = -1;
    int boldItalicIndex = -1;
    int fallbackIndex = -1;
    int unsupportedIndex = -1;
    int monoIndex = -1;
    int linkIndex = -1;
    for (int i = 0; i < app.m_blockCount; ++i) {
        const DocBlock& block = app.m_blocks[i];
        if (block.kind == BLOCK_LINK) linkIndex = i;
        if (block.kind == BLOCK_PREFORMATTED) monoIndex = i;
        if (block.kind != BLOCK_PARAGRAPH) continue;
        if (strstr(block.text, "Proportional normal")) normalIndex = i;
        else if (strstr(block.text, "Bold proportional")) boldIndex = i;
        else if (strstr(block.text, "Italic proportional")) italicIndex = i;
        else if (strstr(block.text, "Bold italic")) boldItalicIndex = i;
        else if (strstr(block.text, "Fallback-list")) fallbackIndex = i;
        else if (strstr(block.text, "Unsupported family")) unsupportedIndex = i;
    }

    const bool robotoAvailable = gxos::gui::SystemFont::IsRobotoAvailable();
    const gxos::web::WebStyle normalStyle = normalIndex >= 0 ? app.m_blocks[normalIndex].style : gxos::web::WebStyle{};
    const gxos::web::WebStyle monoStyle = monoIndex >= 0 ? app.m_blocks[monoIndex].style : gxos::web::WebStyle{};
    const int normalWidth = navigatorTextWidth(normalStyle, "iiWW");
    const int monoWidth = navigatorTextWidth(monoStyle, "iiWW");
    const int normalLineHeight = navigatorLineHeight(normalStyle);
    const int normalLines = navigatorWrappedLineCount(
        normalIndex >= 0 ? app.m_blocks[normalIndex].text : "", 180, normalStyle);

    app.drawDocument(0, 0, smokeWindow.w, smokeWindow.h);

    bool linkGeometry = false;
    if (linkIndex >= 0) {
        const int maxWidth = smokeWindow.w - CONTENT_X * 2 - 32;
        const int left = CONTENT_X + 14 + css_margin_left_or(app.m_bodyStyle, 0) +
            css_margin_left_or(app.m_blocks[linkIndex].style, 0);
        const int top = app.blockY(linkIndex, maxWidth) +
            css_margin_top_or(app.m_blocks[linkIndex].style, 4);
        linkGeometry = app.hitLinkIndex(left + 1, top + 1) == linkIndex;
    }

    const bool faceSelection = normalIndex >= 0 &&
        (!robotoAvailable || !gxos::gui::SystemFont::IsFaceFallback(navigatorFontFace(normalStyle)));
    const bool styleSelection = boldIndex >= 0 && app.m_blocks[boldIndex].style.bold &&
        italicIndex >= 0 && app.m_blocks[italicIndex].style.italic &&
        boldItalicIndex >= 0 && app.m_blocks[boldItalicIndex].style.bold &&
        app.m_blocks[boldItalicIndex].style.italic;
    const bool familySelection = monoIndex >= 0 && navigatorUsesMonospace(monoStyle) &&
        fallbackIndex >= 0 && app.m_blocks[fallbackIndex].style.genericFontFamily == gxos::web::GenericFontFamily::Roboto &&
        unsupportedIndex >= 0 && app.m_blocks[unsupportedIndex].style.genericFontFamily == gxos::web::GenericFontFamily::Unknown;
    const bool metricAgreement = normalWidth > 0 && monoWidth > 0 && normalLineHeight > 0 && normalLines > 0;
    const bool distinctMonospace = !robotoAvailable || normalWidth != monoWidth;
    const bool pass = app.m_blockCount > 0 && faceSelection && styleSelection && familySelection &&
        metricAgreement && distinctMonospace && linkGeometry;

    serial::puts("[NAVIGATOR-SMOKE] typography.phase7a.roboto_available=");
    serial::puts(robotoAvailable ? "yes\n" : "no\n");
    serial::puts("[NAVIGATOR-SMOKE] typography.phase7a.font_initialization=process-lifetime\n");
    serial::puts("[NAVIGATOR-SMOKE] typography.phase7a.normal_width=");
    serial_put_dec((uint32_t)normalWidth);
    serial::puts("\n[NAVIGATOR-SMOKE] typography.phase7a.monospace_width=");
    serial_put_dec((uint32_t)monoWidth);
    serial::puts("\n[NAVIGATOR-SMOKE] typography.phase7a.normal_line_height=");
    serial_put_dec((uint32_t)normalLineHeight);
    serial::puts("\n[NAVIGATOR-SMOKE] typography.phase7a.measurement_paint_agreement=");
    serial::puts(metricAgreement ? "yes\n" : "no\n");
    serial::puts("[NAVIGATOR-SMOKE] typography.phase7a.monospace_distinct=");
    serial::puts(distinctMonospace ? "yes\n" : "no\n");
    serial::puts("[NAVIGATOR-SMOKE] typography.phase7a.link_geometry=");
    serial::puts(linkGeometry ? "yes\n" : "no\n");
    serial::puts(pass ? "[NAVIGATOR-SMOKE] typography.phase7a.result=PASS\n"
                      : "[NAVIGATOR-SMOKE] typography.phase7a.result=FAIL\n");

    app.m_window = nullptr;
    return pass;
}

void NavigatorApp::onMouseMove(int x, int y)
{
    if (m_mouseLeftDown && !m_addressFocused) {
        int dx = x - m_mouseDownX;
        if (dx < 0) dx = -dx;
        int dy = y - m_mouseDownY;
        if (dy < 0) dy = -dy;
        if (dx >= 4 || dy >= 4) {
            m_mouseDragThresholdExceeded = true;
        }

        if ((m_mouseMode == NAV_MOUSE_POTENTIAL_LINK_CLICK ||
             m_mouseMode == NAV_MOUSE_POTENTIAL_TEXT_SELECTION) &&
            m_mouseDragThresholdExceeded) {
            beginSelection(m_mouseDownX, m_mouseDownY);
            if (m_selectionDragging) {
                m_mouseMode = NAV_MOUSE_SELECTING_TEXT;
            } else {
                m_mouseMode = NAV_MOUSE_NONE;
            }
        }

        if (m_mouseMode == NAV_MOUSE_SELECTING_TEXT) {
            updateSelection(x, y);
            invalidate();
        }
    }
    int linkIndex = hitLinkIndex(x, y);
    int formIndex = hitFormBlockIndex(x, y);
    if (linkIndex != m_hoverLinkIndex || formIndex >= 0) {
        m_hoverLinkIndex = linkIndex;
        if (linkIndex >= 0 && linkIndex < m_blockCount) {
            setStatus(m_blocks[linkIndex].url);
        } else if (formIndex >= 0 && formIndex < m_blockCount) {
            if (m_blocks[formIndex].kind == BLOCK_FORM_SUBMIT) setStatus("Submit form");
            else if (m_blocks[formIndex].kind == BLOCK_FORM_SELECT) setStatus("Cycle select option");
            else if (m_blocks[formIndex].kind == BLOCK_FORM_CHECKBOX) setStatus("Toggle checkbox");
            else if (m_blocks[formIndex].kind == BLOCK_FORM_RADIO) setStatus("Select radio option");
            else setStatus("Click to edit form field");
        } else if (hitAddressBar(x, y)) {
            setStatus("Click to edit address");
        } else {
            setStatus("Ready");
        }
    }
}

void NavigatorApp::onMouseDown(int x, int y, uint8_t button)
{
    if ((button & 0x01) == 0 && button != 1) return;

    m_mouseLeftDown = true;
    m_mouseMode = NAV_MOUSE_NONE;
    m_mouseDownX = x;
    m_mouseDownY = y;
    m_mouseDragThresholdExceeded = false;
    m_mouseDownLinkIndex = hitLinkIndex(x, y);

    if (hitAddressBar(x, y)) {
        clearSelection();
        m_mouseLeftDown = true;
        m_mouseMode = NAV_MOUSE_ADDRESS_BAR_INTERACTION;
        blurFormBlock();
        focusAddressBar();
        const NavigatorToolbarLayout toolbarLayout = navigatorToolbarLayout(m_window ? m_window->w : 0);
        int charOffset = (x - toolbarLayout.addressX - 8) / 6;
        if (charOffset < 0) charOffset = 0;
        int len = strlen_local(m_addressBuffer);
        if (charOffset > len) charOffset = len;
        m_addressCaret = charOffset;
        invalidate();
        return;
    }

    if (m_addressFocused) blurAddressBar();

    int formIndex = hitFormBlockIndex(x, y);
    if (formIndex >= 0 && formIndex < m_blockCount) {
        clearSelection();
        if (m_blocks[formIndex].disabled) {
            setStatus("Form control disabled");
        } else {
            focusFormBlock(formIndex);
            if (m_blocks[formIndex].kind == BLOCK_FORM_TEXT) {
                int fx, fy, fw, fh;
                formControlRect(formIndex, fx, fy, fw, fh);
                int charOffset = (x - fx - 8) / 6;
                int len = strlen_local(m_blocks[formIndex].inputValue);
                if (charOffset < 0) charOffset = 0;
                if (charOffset > len) charOffset = len;
                m_formCaret = charOffset;
            } else if (m_blocks[formIndex].kind != BLOCK_FORM_TEXTAREA) {
                activateFormControl(formIndex);
            }
        }
        m_mouseLeftDown = false;
        m_mouseMode = NAV_MOUSE_NONE;
        invalidate();
        return;
    }

    SelectionPosition textHit = textPositionFromPoint(x, y, false);
    if (m_mouseDownLinkIndex >= 0 && m_mouseDownLinkIndex < m_blockCount) {
        clearSelection();
        m_mouseLeftDown = true;
        m_mouseMode = NAV_MOUSE_POTENTIAL_LINK_CLICK;
        m_mouseDownLinkIndex = hitLinkIndex(x, y);
        invalidate();
    } else if (textHit.blockIndex >= 0) {
        clearSelection();
        m_mouseLeftDown = true;
        m_mouseMode = NAV_MOUSE_POTENTIAL_TEXT_SELECTION;
        invalidate();
    } else {
        clearSelection();
        m_mouseLeftDown = true;
        invalidate();
    }
}

void NavigatorApp::onMouseUp(int x, int y, uint8_t button)
{
    if ((button & 0x01) == 0 && button != 1) return;
    NavigatorMouseMode mode = m_mouseMode;
    int downLinkIndex = m_mouseDownLinkIndex;
    int upLinkIndex = hitLinkIndex(x, y);
    m_mouseLeftDown = false;
    if (mode == NAV_MOUSE_SELECTING_TEXT || m_selectionDragging) {
        finalizeSelection(x, y);
        invalidate();
    } else if (mode == NAV_MOUSE_POTENTIAL_LINK_CLICK &&
               !m_mouseDragThresholdExceeded &&
               downLinkIndex >= 0 && downLinkIndex < m_blockCount &&
               upLinkIndex == downLinkIndex) {
        navigateTo(m_blocks[downLinkIndex].url);
    }
    m_mouseMode = NAV_MOUSE_NONE;
    m_mouseDownLinkIndex = -1;
    m_mouseDragThresholdExceeded = false;
}

void NavigatorApp::onWidgetClick(int widgetId)
{
    if (widgetId == m_backBtnId) {
        goBack();
    } else if (widgetId == m_forwardBtnId) {
        goForward();
    } else if (widgetId == m_reloadBtnId) {
        loadUrl(m_currentUrl);
    } else if (widgetId == m_homeBtnId) {
        navigateTo("about:navigator");
    } else if (widgetId == m_bookmarksBtnId) {
        navigateTo("about:bookmarks");
    } else if (widgetId == m_addBookmarkBtnId) {
        addBookmark(m_title[0] ? m_title : m_currentUrl, m_currentUrl);
        setStatus("Bookmark added");
        invalidate();
    }
}

void NavigatorApp::onKeyDown(uint32_t key)
{
    if (key == 17) {
        m_ctrlPressed = true;
        return;
    }

    if (m_addressFocused) {
        if (m_ctrlPressed && (key == 'c' || key == 'C')) {
            strcopy(m_clipboard, m_addressBuffer, sizeof(m_clipboard));
            strcopy(m_clipboardMode, "Navigator internal clipboard", sizeof(m_clipboardMode));
            setStatus("Copied address to Navigator clipboard");
            return;
        }
        if (m_ctrlPressed && (key == 'a' || key == 'A')) {
            m_addressCaret = strlen_local(m_addressBuffer);
            setStatus("Address bar select all is deferred; copy uses the full address");
            invalidate();
            return;
        }
        int len = strlen_local(m_addressBuffer);
        if (key == shell::KEY_HOME) {
            m_addressCaret = 0;
            invalidate();
        } else if (key == shell::KEY_END) {
            m_addressCaret = len;
            invalidate();
        } else if (key == shell::KEY_LEFT) {
            if (m_addressCaret > 0) --m_addressCaret;
            invalidate();
        } else if (key == shell::KEY_RIGHT) {
            if (m_addressCaret < len) ++m_addressCaret;
            invalidate();
        } else if (key == 8) {
            if (m_addressCaret > 0) {
                for (int i = m_addressCaret - 1; i < len; ++i) m_addressBuffer[i] = m_addressBuffer[i + 1];
                --m_addressCaret;
                invalidate();
            }
        } else if (key == shell::KEY_DELETE) {
            if (m_addressCaret < len) {
                for (int i = m_addressCaret; i < len; ++i) m_addressBuffer[i] = m_addressBuffer[i + 1];
                invalidate();
            }
        } else if (key == 13 || key == '\n' || key == '\r') {
            commitAddressBar();
        } else if (key == 27) {
            blurAddressBar();
        }
        return;
    }

    if (key == '\t' || key == shell::KEY_TAB) {
        focusNextFormBlock();
        invalidate();
        return;
    }

    if (m_focusedFormBlock >= 0 && m_focusedFormBlock < m_blockCount &&
        isFocusableFormBlock(m_blocks[m_focusedFormBlock])) {
        DocBlock& block = m_blocks[m_focusedFormBlock];
        int len = strlen_local(block.inputValue);
        if (block.kind == BLOCK_FORM_CHECKBOX || block.kind == BLOCK_FORM_RADIO ||
            block.kind == BLOCK_FORM_SELECT || block.kind == BLOCK_FORM_SUBMIT) {
            if (key == 13 || key == '\n' || key == '\r') activateFormControl(m_focusedFormBlock);
            else if (key == 27) blurFormBlock();
            invalidate();
            return;
        }
        if (block.kind == BLOCK_FORM_TEXT || block.kind == BLOCK_FORM_TEXTAREA) {
            if (key == 13 || key == '\n' || key == '\r') {
                if (block.kind == BLOCK_FORM_TEXTAREA && len < MAX_FORM_VALUE - 1) {
                    for (int i = len; i >= m_formCaret; --i) block.inputValue[i + 1] = block.inputValue[i];
                    block.inputValue[m_formCaret++] = '\n';
                } else if (block.kind == BLOCK_FORM_TEXT) {
                    submitFormForBlock(m_focusedFormBlock);
                }
            } else if (key == 8) {
                if (m_formCaret > 0) {
                    for (int i = m_formCaret - 1; i < len; ++i) block.inputValue[i] = block.inputValue[i + 1];
                    --m_formCaret;
                }
            } else if (key == shell::KEY_DELETE) {
                if (m_formCaret < len) {
                    for (int i = m_formCaret; i < len; ++i) block.inputValue[i] = block.inputValue[i + 1];
                }
            } else if (key == shell::KEY_LEFT) {
                if (m_formCaret > 0) --m_formCaret;
            } else if (key == shell::KEY_RIGHT) {
                if (m_formCaret < len) ++m_formCaret;
            } else if (key == shell::KEY_HOME) {
                m_formCaret = 0;
            } else if (key == shell::KEY_END) {
                m_formCaret = len;
            } else if (key == 27) {
                blurFormBlock();
            }
            invalidate();
            return;
        }
    }
    if (m_ctrlPressed && (key == 'a' || key == 'A')) {
        selectAllDocumentText();
        setStatus(hasSelection() ? "Selected all document text" : "No document text to select");
        invalidate();
    } else if (m_ctrlPressed && (key == 'c' || key == 'C')) {
        if (copySelectionToClipboard()) setStatus("Copied to Navigator clipboard");
        else setStatus("No document selection to copy");
    } else if (key == shell::KEY_PGUP) {
        m_scrollY -= 48;
        clampScroll();
        setStatus("Scrolled up");
    } else if (key == shell::KEY_PGDN) {
        m_scrollY += 48;
        clampScroll();
        setStatus("Scrolled down");
    } else if (key == shell::KEY_HOME) {
        m_scrollY = 0;
        setStatus("Home position");
    }
}

void NavigatorApp::onKeyUp(uint32_t key)
{
    if (key == 17) m_ctrlPressed = false;
}

void NavigatorApp::onKeyChar(char c)
{
    if (m_addressFocused) {
        if (c < 32 || c > 126) return;
        int len = strlen_local(m_addressBuffer);
        if (len >= MAX_URL_LEN - 1) return;
        for (int i = len; i >= m_addressCaret; --i) m_addressBuffer[i + 1] = m_addressBuffer[i];
        m_addressBuffer[m_addressCaret] = c;
        ++m_addressCaret;
        invalidate();
        return;
    }
    if (m_focusedFormBlock < 0 || m_focusedFormBlock >= m_blockCount ||
        !isFocusableFormBlock(m_blocks[m_focusedFormBlock])) return;
    DocBlock& block = m_blocks[m_focusedFormBlock];
    if (block.kind == BLOCK_FORM_CHECKBOX || block.kind == BLOCK_FORM_RADIO ||
        block.kind == BLOCK_FORM_SELECT || block.kind == BLOCK_FORM_SUBMIT) {
        if (c == ' ') activateFormControl(m_focusedFormBlock);
        invalidate();
        return;
    }
    if ((block.kind != BLOCK_FORM_TEXT && block.kind != BLOCK_FORM_TEXTAREA) || c < 32 || c > 126) return;
    int len = strlen_local(block.inputValue);
    if (len >= MAX_FORM_VALUE - 1) return;
    for (int i = len; i >= m_formCaret; --i) block.inputValue[i + 1] = block.inputValue[i];
    block.inputValue[m_formCaret++] = c;
    invalidate();
}

static void serial_put_dec64(uint64_t value) {
    char buffer[24];
    int index = 0;
    if (value == 0) {
        serial::putc('0');
        return;
    }
    while (value > 0 && index < (int)(sizeof(buffer) - 1)) {
        buffer[index++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (index > 0) {
        serial::putc(buffer[--index]);
    }
}

void NavigatorApp::setStatus(const char* text)
{
    strcopy(m_status, text ? text : "", MAX_STATUS_LEN);
    invalidate();
}

void NavigatorApp::updateButtons()
{
    if (!m_window) return;
    m_window->widgetCount = 0;
    const NavigatorToolbarLayout layout = navigatorToolbarLayout(m_window->w);
    m_backBtnId = addButton(layout.x[0], kNavigatorToolbarButtonY, layout.w[0], BUTTON_H, "Back"); setButtonIcon(m_backBtnId, m_toolbarIcons[0]);
    m_forwardBtnId = addButton(layout.x[1], kNavigatorToolbarButtonY, layout.w[1], BUTTON_H, "Next"); setButtonIcon(m_forwardBtnId, m_toolbarIcons[1]);
    m_reloadBtnId = addButton(layout.x[2], kNavigatorToolbarButtonY, layout.w[2], BUTTON_H, "Reload"); setButtonIcon(m_reloadBtnId, m_toolbarIcons[2]);
    m_homeBtnId = addButton(layout.x[3], kNavigatorToolbarButtonY, layout.w[3], BUTTON_H, "Home"); setButtonIcon(m_homeBtnId, m_toolbarIcons[3]);
    m_bookmarksBtnId = addButton(layout.x[4], kNavigatorToolbarButtonY, layout.w[4], BUTTON_H, "Marks"); setButtonIcon(m_bookmarksBtnId, m_toolbarIcons[4]);
    m_addBookmarkBtnId = addButton(layout.x[5], kNavigatorToolbarButtonY, layout.w[5], BUTTON_H, "Add"); setButtonIcon(m_addBookmarkBtnId, m_toolbarIcons[5]);
}

void NavigatorApp::loadChromeImages()
{
    static const char* toolbarNames[6] = {
        "nav-back.png", "nav-next.png", "reload.png",
        "nav-home.png", "marks.png", "nav-add.png"
    };
    int toolbarLoaded = 0;
    int throbberLoaded = 0;
    for (int i = 0; i < 6; ++i) {
        char path[64] = "/system/config/navigator/";
        strappend(path, toolbarNames[i], sizeof(path));
        m_toolbarIcons[i] = gxos::gui::ImageAdapter::LoadFromFile(path);
        if (m_toolbarIcons[i].status != gxos::gui::ImageLoadStatus::Ok) {
            strcopy(path, "/config/navigator/", sizeof(path));
            strappend(path, toolbarNames[i], sizeof(path));
            m_toolbarIcons[i] = gxos::gui::ImageAdapter::LoadFromFile(path);
        }
        if (m_toolbarIcons[i].status == gxos::gui::ImageLoadStatus::Ok) ++toolbarLoaded;
    }
    for (int i = 0; i < 12; ++i) {
        char path[48] = "/system/config/navigator/surfer-00.png";
        path[32] = (char)('0' + (i / 10));
        path[33] = (char)('0' + (i % 10));
        m_throbberFrames[i] = gxos::gui::ImageAdapter::LoadFromFile(path);
        if (m_throbberFrames[i].status != gxos::gui::ImageLoadStatus::Ok) {
            strcopy(path, "/config/navigator/surfer-00.png", sizeof(path));
            path[25] = (char)('0' + (i / 10));
            path[26] = (char)('0' + (i % 10));
            m_throbberFrames[i] = gxos::gui::ImageAdapter::LoadFromFile(path);
        }
        if (m_throbberFrames[i].status == gxos::gui::ImageLoadStatus::Ok) ++throbberLoaded;
    }
    serial::puts("[NAVIGATOR] toolbar_icons_loaded=");
    serial_put_dec64((uint64_t)toolbarLoaded);
    serial::puts("/6 throbber_frames_loaded=");
    serial_put_dec64((uint64_t)throbberLoaded);
    serial::puts("/12\n");
    serial::puts("[NAVIGATOR] phase7c_toolbar_icon_size=16 fallback=label-only cache=init-once\n");
}

void NavigatorApp::setButtonIcon(int widgetId, const gxos::gui::ImageBitmap& image)
{
    app::Widget* widget = getWidget(widgetId);
    if (!widget || image.status != gxos::gui::ImageLoadStatus::Ok) return;
    widget->iconPixels = image.pixels;
    widget->iconWidth = image.width;
    widget->iconHeight = image.height;
}

static void nav_image_file_path_from_url(const char* url, char* out, int outSize)
{
    out[0] = '\0';
    if (!url || outSize <= 0) return;
    const char* prefix = "file://";
    int i = 0;
    while (prefix[i]) {
        if (url[i] != prefix[i]) return;
        ++i;
    }
    const char* path = url + i;
    strcopy(out, path, outSize);
}

void NavigatorApp::addBlock(BlockKind kind, const char* text, const char* url, const gxos::web::WebStyle* style)
{
    if (m_blockCount >= MAX_BLOCKS) return;
    DocBlock& block = m_blocks[m_blockCount];
    block = DocBlock{};
    block.kind = kind;
    strcopy(block.text, text ? text : "", MAX_BLOCK_TEXT);
    strcopy(block.url, url ? url : "", MAX_URL_LEN);
    block.imageStatus = (int)gxos::gui::ImageLoadStatus::Ok;
    block.style = style ? *style : gxos::web::WebStyle{};
    block.formIndex = -1;
    block.selectedOption = -1;
    ++m_blockCount;
}

void NavigatorApp::addImageBlock(const char* src, const char* alt, const char* resolvedUrl, int width, int height, const gxos::web::WebStyle* style)
{
    if (m_blockCount >= MAX_BLOCKS) return;
    DocBlock& block = m_blocks[m_blockCount];
    block = DocBlock{};
    block.kind = BLOCK_IMAGE;
    strcopy(block.text, alt ? alt : "", MAX_BLOCK_TEXT);
    strcopy(block.url, resolvedUrl ? resolvedUrl : "", MAX_URL_LEN);
    strcopy(block.src, src ? src : "", MAX_URL_LEN);
    strcopy(block.alt, alt ? alt : "", 96);
    block.width = width > 0 ? width : 0;
    block.height = height > 0 ? height : 0;

    char imagePath[MAX_URL_LEN];
    nav_image_file_path_from_url(resolvedUrl, imagePath, MAX_URL_LEN);
    gxos::gui::ImageProbe probe = gxos::gui::ImageAdapter::ProbeFile(imagePath);
    block.naturalWidth = (int)probe.width;
    block.naturalHeight = (int)probe.height;
    block.imageStatus = (int)probe.status;
    block.style = style ? *style : gxos::web::WebStyle{};
    block.formIndex = -1;
    block.selectedOption = -1;

    ++m_blockCount;
}

void NavigatorApp::loadDefaultBookmarks()
{
    m_bookmarkCount = 0;
    addBookmark("About Navigator", "about:navigator");
    addBookmark("Bookmarks", "about:bookmarks");
    addBookmark("guideXOS Help", "file:///docs/index.html");
}

void NavigatorApp::addBookmark(const char* title, const char* url)
{
    if (!url || !url[0]) return;
    for (int i = 0; i < m_bookmarkCount; ++i) {
        if (streq_local(m_bookmarks[i].url, url)) return;
    }
    if (m_bookmarkCount >= MAX_BOOKMARKS) {
        setStatus("Bookmark list full");
        return;
    }
    strcopy(m_bookmarks[m_bookmarkCount].title, title && title[0] ? title : url, 64);
    strcopy(m_bookmarks[m_bookmarkCount].url, url, MAX_URL_LEN);
    ++m_bookmarkCount;
}

void NavigatorApp::rememberDownload(const DownloadRecord& record)
{
    if (m_recentDownloadCount >= (int)(sizeof(m_recentDownloads) / sizeof(m_recentDownloads[0]))) {
        for (int i = m_recentDownloadCount - 1; i > 0; --i) {
            m_recentDownloads[i] = m_recentDownloads[i - 1];
        }
        m_recentDownloads[0] = record;
        return;
    }
    for (int i = m_recentDownloadCount; i > 0; --i) {
        m_recentDownloads[i] = m_recentDownloads[i - 1];
    }
    m_recentDownloads[0] = record;
    ++m_recentDownloadCount;
}

void NavigatorApp::clearPageDownloadMetadata()
{
    m_metaDownloaded = false;
    m_metaDownloadSavedPath[0] = '\0';
    m_metaDownloadByteCount = 0;
}

void NavigatorApp::buildAboutNavigatorDocument()
{
    strcopy(m_currentUrl, "about:navigator", MAX_URL_LEN);
    strcopy(m_title, "About guideXOS Navigator", MAX_TITLE_LEN_NAV);
    m_blockCount = 0;
    addBlock(BLOCK_HEADING, "About guideXOS Navigator");
    addBlock(BLOCK_PARAGRAPH, "guideXOS Navigator is the native document viewer and browser shell for guideXOS Server.");
    addBlock(BLOCK_PARAGRAPH, "This runtime is using the updated Navigator path, not the old placeholder shell.");
    addBlock(BLOCK_LIST_ITEM, "Address bar navigation");
    addBlock(BLOCK_LIST_ITEM, "Back / Forward / Reload / Home navigation");
    addBlock(BLOCK_LIST_ITEM, "Bookmarks and Add controls");
    addBlock(BLOCK_LIST_ITEM, "file:// HTML document loading");
    addBlock(BLOCK_LINK, "Open guideXOS Help", "file:///docs/index.html");
    addBlock(BLOCK_LINK, "View Bookmarks", "about:bookmarks");
    addBlock(BLOCK_LINK, "View Downloads", "about:downloads");
    addBlock(BLOCK_LINK, "Page Info", "about:page-info");
    addBlock(BLOCK_LINK, "View Source", "about:view-source");
    addBlock(BLOCK_LINK, "Navigator Runtime", "about:navigator-runtime");
    rememberPageMetadata("about:navigator", "about:navigator", "about", "generated/about", "", nullptr, 0);
}

void NavigatorApp::buildBookmarksDocument()
{
    strcopy(m_currentUrl, "about:bookmarks", MAX_URL_LEN);
    strcopy(m_title, "Bookmarks", MAX_TITLE_LEN_NAV);
    m_blockCount = 0;
    addBlock(BLOCK_HEADING, "Bookmarks");
    for (int i = 0; i < m_bookmarkCount; ++i) {
        addBlock(BLOCK_LINK, m_bookmarks[i].title, m_bookmarks[i].url);
    }
    rememberPageMetadata("about:bookmarks", "about:bookmarks", "about", "generated/about", "", nullptr, 0);
}

void NavigatorApp::buildDownloadsDocument()
{
    strcopy(m_currentUrl, "about:downloads", MAX_URL_LEN);
    strcopy(m_title, "Downloads", MAX_TITLE_LEN_NAV);
    m_blockCount = 0;
    addBlock(BLOCK_HEADING, "Downloads");
    addBlock(BLOCK_PARAGRAPH, "Recent downloads are not persisted in this bare-metal adapter yet.");
    addBlock(BLOCK_PARAGRAPH, "Unsupported HTTP(S) content can be saved to /downloads when VFS write support is available.");
    addBlock(BLOCK_LIST_ITEM, "Storage path: /downloads when writable storage is available");
    addBlock(BLOCK_LIST_ITEM, "Current status: enabled within the response body limit");
    addBlock(BLOCK_LINK, "Page Info", "about:page-info");
    addBlock(BLOCK_LINK, "Navigator Runtime", "about:navigator-runtime");
    addBlock(BLOCK_LINK, "Go to about:navigator", "about:navigator");
}

void NavigatorApp::buildDownloadResultDocument(const DownloadRecord& record)
{
    strcopy(m_currentUrl, record.finalUrl[0] ? record.finalUrl : record.url, MAX_URL_LEN);
    strcopy(m_title, record.success ? "Download Complete" : "Download Failed", MAX_TITLE_LEN_NAV);
    m_blockCount = 0;
    addBlock(BLOCK_HEADING, m_title);

    char line[MAX_BLOCK_TEXT];
    strcopy(line, "Filename: ", sizeof(line));
    strappend(line, record.suggestedFileName[0] ? record.suggestedFileName : "download.bin", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    strcopy(line, "Source URL: ", sizeof(line));
    strappend(line, record.url, sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    strcopy(line, "Final URL: ", sizeof(line));
    strappend(line, record.finalUrl[0] ? record.finalUrl : record.url, sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    strcopy(line, "Content type: ", sizeof(line));
    strappend(line, record.contentType[0] ? record.contentType : "application/octet-stream", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    char number[24];
    nav_int_to_text(record.byteCount, number, sizeof(number));
    strcopy(line, "Byte count: ", sizeof(line));
    strappend(line, number, sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    strcopy(line, "Saved path: ", sizeof(line));
    strappend(line, record.savedPath[0] ? record.savedPath : "(none)", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    addBlock(BLOCK_LIST_ITEM, record.success ? "Status: success" : "Status: failed");
    if (record.error[0]) addBlock(BLOCK_PARAGRAPH, record.error);
    addBlock(BLOCK_LINK, "View Downloads", "about:downloads");
    addBlock(BLOCK_LINK, "Page Info", "about:page-info");
    addBlock(BLOCK_LINK, "Go to about:navigator", "about:navigator");
}

static bool nav_starts_with(const char* value, const char* prefix);

static void nav_int_to_text(int value, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    char tmp[16];
    int pos = 0;
    bool neg = value < 0;
    unsigned int v = neg ? (unsigned int)(-value) : (unsigned int)value;
    if (v == 0) tmp[pos++] = '0';
    while (v > 0 && pos < 15) {
        tmp[pos++] = (char)('0' + (v % 10));
        v /= 10;
    }
    int outPos = 0;
    if (neg && outPos < outSize - 1) out[outPos++] = '-';
    while (pos > 0 && outPos < outSize - 1) out[outPos++] = tmp[--pos];
    out[outPos] = '\0';
}

static void nav_i64_to_text(int64_t value, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    char tmp[24];
    int pos = 0;
    bool neg = value < 0;
    uint64_t v = neg ? static_cast<uint64_t>(-(value + 1)) + 1 : static_cast<uint64_t>(value);
    if (v == 0) tmp[pos++] = '0';
    while (v > 0 && pos < (int)(sizeof(tmp) - 1)) {
        tmp[pos++] = (char)('0' + (v % 10));
        v /= 10;
    }
    int outPos = 0;
    if (neg && outPos < outSize - 1) out[outPos++] = '-';
    while (pos > 0 && outPos < outSize - 1) out[outPos++] = tmp[--pos];
    out[outPos] = '\0';
}
void NavigatorApp::buildPageInfoDocument()
{
    strcopy(m_currentUrl, "about:page-info", MAX_URL_LEN);
    strcopy(m_title, "Page Info", MAX_TITLE_LEN_NAV);
    m_blockCount = 0;
    addBlock(BLOCK_HEADING, "Page Info");
    if (!m_metaRequestedUrl[0]) {
        addBlock(BLOCK_PARAGRAPH, "No page has been loaded yet.");
        addBlock(BLOCK_LINK, "Go to about:navigator", "about:navigator");
        return;
    }

    char line[MAX_BLOCK_TEXT];
    char number[24];
#define NAV_INFO_TEXT(label, value) do { strcopy(line, label, sizeof(line)); strappend(line, value, sizeof(line)); addBlock(BLOCK_LIST_ITEM, line); } while (0)
#define NAV_INFO_INT(label, value) do { nav_int_to_text(value, number, sizeof(number)); strcopy(line, label, sizeof(line)); strappend(line, number, sizeof(line)); addBlock(BLOCK_LIST_ITEM, line); } while (0)
    NAV_INFO_TEXT("Requested URL: ", m_metaRequestedUrl);
    NAV_INFO_TEXT("Final URL: ", m_metaFinalUrl);
    NAV_INFO_TEXT("Source type: ", m_metaSourceType);
    NAV_INFO_TEXT("Content type: ", m_metaContentType[0] ? m_metaContentType : "(none)");
    NAV_INFO_TEXT("Content encoding: ", m_metaContentEncoding[0] ? m_metaContentEncoding : "(none)");
    NAV_INFO_TEXT("Response framing: ", m_metaResponseFraming[0] ? m_metaResponseFraming : "(none)");
    NAV_INFO_TEXT("Content-Length present: ", m_metaContentLengthPresent ? "yes" : "no");
    if (m_metaContentLengthPresent) NAV_INFO_INT("Content-Length: ", m_metaContentLength);
    NAV_INFO_TEXT("Truncated response: ", m_metaTruncatedResponse ? "yes" : "no");
    NAV_INFO_TEXT("Unsupported reason: ", m_metaUnsupportedReason[0] ? m_metaUnsupportedReason : "(none)");
    if (m_metaHttpStatusCode > 0) NAV_INFO_INT("HTTP status: ", m_metaHttpStatusCode);
    else NAV_INFO_TEXT("HTTP status: ", "not applicable");
    NAV_INFO_TEXT("Redirected: ", m_metaRedirected ? "yes" : "no");
    NAV_INFO_INT("Redirect count: ", m_metaRedirectCount);
    NAV_INFO_TEXT("Error status: ", m_metaErrorStatus[0] ? m_metaErrorStatus : "(none)");
    // Keep the small Forms-lite contract near the top of Page Info so the
    // bounded document block budget cannot hide it behind transport details.
    NAV_INFO_TEXT("Forms-lite interactive controls: ", "enabled");
    NAV_INFO_TEXT("Forms-lite POST interactive: ", "enabled");
    NAV_INFO_TEXT("Forms-lite POST bare-metal: ", "enabled-basic");
    NAV_INFO_INT("Forms: ", m_metaFormCount);
    NAV_INFO_INT("Text inputs: ", m_metaTextInputCount);
    NAV_INFO_INT("Checkboxes: ", m_metaCheckboxCount);
    NAV_INFO_INT("Radio buttons: ", m_metaRadioCount);
    NAV_INFO_INT("Textareas: ", m_metaTextareaCount);
    NAV_INFO_INT("Selects: ", m_metaSelectCount);
    NAV_INFO_INT("Submit buttons: ", m_metaSubmitCount);
    NAV_INFO_INT("Unsupported forms: ", m_metaUnsupportedFormCount);
    NAV_INFO_TEXT("Last submitted method: ", m_lastSubmittedFormMethod[0] ? m_lastSubmittedFormMethod : "(none)");
    NAV_INFO_TEXT("Last submitted action: ", m_lastSubmittedFormAction[0] ? m_lastSubmittedFormAction : "(none)");
    NAV_INFO_TEXT("Last submitted status: ", m_lastSubmittedFormStatus[0] ? m_lastSubmittedFormStatus : "(none)");
    NAV_INFO_TEXT("Last form error: ", m_lastFormError[0] ? m_lastFormError : "(none)");
    NAV_INFO_TEXT("Last POST HTTP status: ", m_lastPostHttpStatus[0] ? m_lastPostHttpStatus : "(none)");
    NAV_INFO_TEXT("Last POST content type: ", m_lastPostContentType[0] ? m_lastPostContentType : "(none)");
    NAV_INFO_INT("Last POST body bytes: ", m_lastPostBodyBytes);
    NAV_INFO_TEXT("Header cap hit: ", m_metaHeaderCapHit ? "yes" : "no");
    NAV_INFO_TEXT("Body cap hit: ", m_metaBodyCapHit ? "yes" : "no");
    NAV_INFO_TEXT("TLS succeeded before content failure: ", m_metaTlsSucceededBeforeContentFailure ? "yes" : "no");
    NAV_INFO_TEXT("URL scheme: ", m_metaScheme[0] ? m_metaScheme : "(none)");
    NAV_INFO_TEXT("DNS used: ", m_metaDnsUsed ? "yes" : "no");
    NAV_INFO_TEXT("DNS hostname: ", m_metaDnsHost[0] ? m_metaDnsHost : "(none)");
    NAV_INFO_TEXT("DNS resolved IP: ", m_metaDnsResolvedIp[0] ? m_metaDnsResolvedIp : "(none)");
    strcopy(line, "DNS result: ", sizeof(line));
    strappend(line, !m_metaDnsUsed ? "not used" : (m_metaDnsResolvedIp[0] && !m_metaDnsError[0] ? "PASS" : "FAIL"), sizeof(line));
    strappend(line, "; error=", sizeof(line));
    strappend(line, m_metaDnsError[0] ? m_metaDnsError : "(none)", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    strcopy(line, "TCP result: ", sizeof(line));
    strappend(line, !m_metaTlsUsed ? "not recorded" : (streq_local(m_metaTlsStatus, "TcpConnectFailed") ? "FAIL" : "PASS"), sizeof(line));
    strappend(line, "; selection=", sizeof(line));
    strappend(line, m_metaTransportSelection[0] ? m_metaTransportSelection : "(none)", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    NAV_INFO_TEXT("Transport policy: ", m_metaTransportPolicyReason[0] ? m_metaTransportPolicyReason : "(none)");
    NAV_INFO_TEXT("TLS backend: ", m_metaTlsUsed ? (m_metaTlsBackend[0] ? m_metaTlsBackend : "(none)") : "not used");
    NAV_INFO_TEXT("TLS status: ", m_metaTlsUsed || m_metaTlsStatus[0] ? (m_metaTlsStatus[0] ? m_metaTlsStatus : "(none)") : "not used");
    NAV_INFO_TEXT("Certificate validation: ", m_metaTlsUsed ? (m_metaTlsValidated ? "PASS" : "FAIL") : "not used");
    NAV_INFO_TEXT("Hostname validation: ", m_metaTlsUsed ? (m_metaTlsHostnameValidated ? "PASS" : "FAIL") : "not used");
    NAV_INFO_TEXT("TLS allowlist mode: ",
        m_metaTlsUsed
            ? (m_metaTlsAllowlistLocalOnly ? "local-only controlled HTTPS" : "explicit-policy validated HTTPS")
            : "not used");
    NAV_INFO_TEXT("Downgrade redirect blocked: ", m_metaDowngradeRedirectBlocked ? "yes" : "no");
    NAV_INFO_TEXT("TLS hostname: ", m_metaTlsHostname[0] ? m_metaTlsHostname : "(none)");
    NAV_INFO_TEXT("TLS SNI: ", m_metaTlsSniHost[0] ? m_metaTlsSniHost : "(none)");
    if (m_metaTlsUsed) NAV_INFO_INT("TLS verify flags: ", (int)m_metaTlsVerifyFlags);
    NAV_INFO_TEXT("TLS protocol: ", m_metaTlsProtocol[0] ? m_metaTlsProtocol : "(none)");
    NAV_INFO_TEXT("TLS cipher suite: ", m_metaTlsCipherSuite[0] ? m_metaTlsCipherSuite : "(none)");
    {
        const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
        const gxos::GxosTrustStorePolicyInfo trustStorePolicy = gxos::gxos_tls_trust_store_policy_info();
        const gxos::GxosCaStoreInfo caStoreInfo = gxos::gxos_ca_store_info();
        // Keep manifest/hash diagnostics visible without pushing POST smoke markers past the block cap.
        strcopy(line, "HTTPS policy: selected=", sizeof(line));
        strappend(line, gxos::gxos_validated_https_policy_state_name(httpsPolicy.selectedState), sizeof(line));
        strappend(line, "; effective=", sizeof(line));
        strappend(line, gxos::gxos_validated_https_policy_state_name(httpsPolicy.state), sizeof(line));
        strappend(line, "; dev_mode=", sizeof(line));
        strappend(line, httpsPolicy.state == gxos::GxosValidatedHttpsPolicyState::UserTrustStoreDevMode ? "yes" : "no", sizeof(line));
        strappend(line, "; production_validated=", sizeof(line));
        strappend(line, httpsPolicy.productionReady ? "yes" : "no", sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);

        strcopy(line, "Trust store: source=", sizeof(line));
        strappend(line, gxos::gxos_trust_store_source_name(trustStorePolicy.source), sizeof(line));
        strappend(line, "; parsed_certs=", sizeof(line));
        nav_int_to_text((int)trustStorePolicy.parsedCertificateCount, number, sizeof(number));
        strappend(line, number, sizeof(line));
        strappend(line, "; path=", sizeof(line));
        strappend(line, trustStorePolicy.path ? trustStorePolicy.path : "(none)", sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);

        strcopy(line, "Validated fixture HTTPS: ", sizeof(line));
        strappend(line, httpsPolicy.validatedNavigationEnabled ? "enabled" : "disabled", sizeof(line));
        strappend(line, "; Public HTTPS pilot: ", sizeof(line));
        strappend(line, httpsPolicy.broadPublicHttpsEnabled ? "enabled" : "disabled", sizeof(line));
        strappend(line, "; requested=", sizeof(line));
        strappend(line, httpsPolicy.publicHttpsPilotRequested ? "yes" : "no", sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);

        strcopy(line, "Public HTTPS pilot reason: ", sizeof(line));
        strappend(line, httpsPolicy.publicHttpsPilotReason ? httpsPolicy.publicHttpsPilotReason : "(none)", sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);

        strcopy(line, "Trust manifest: present=", sizeof(line));
        strappend(line, caStoreInfo.manifest.present ? "yes" : "no", sizeof(line));
        strappend(line, "; schema=", sizeof(line));
        strappend(line, caStoreInfo.manifest.schemaVersion ? caStoreInfo.manifest.schemaVersion : "(none)", sizeof(line));
        strappend(line, "; bundle_type=", sizeof(line));
        strappend(line, caStoreInfo.manifest.bundleType ? caStoreInfo.manifest.bundleType : "(none)", sizeof(line));
        strappend(line, "; rotation_id=", sizeof(line));
        strappend(line, caStoreInfo.manifest.rotationId ? caStoreInfo.manifest.rotationId : "(none)", sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);

        strcopy(line, "Trust hashes: manifest=", sizeof(line));
        strappend(line, caStoreInfo.manifest.manifestSha256 ? caStoreInfo.manifest.manifestSha256 : "(none)", sizeof(line));
        strappend(line, "; computed=", sizeof(line));
        strappend(line, caStoreInfo.manifest.computedSha256 ? caStoreInfo.manifest.computedSha256 : "(none)", sizeof(line));
        strappend(line, "; match=", sizeof(line));
        strappend(line, caStoreInfo.manifest.hashMatch ? "yes" : "no", sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);

        strcopy(line, "Trust manifest roots: count=", sizeof(line));
        nav_int_to_text((int)caStoreInfo.manifest.rootCount, number, sizeof(number));
        strappend(line, number, sizeof(line));
        strappend(line, "; pem_bytes=", sizeof(line));
        nav_int_to_text((int)caStoreInfo.manifest.pemBytes, number, sizeof(number));
        strappend(line, number, sizeof(line));
        strappend(line, "; production_ready=", sizeof(line));
        strappend(line, caStoreInfo.manifest.productionReady ? "yes" : "no", sizeof(line));
        strappend(line, "; test_only=", sizeof(line));
        strappend(line, caStoreInfo.manifest.testOnly ? "yes" : "no", sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);

        strcopy(line, "Trust readiness: public_ready=", sizeof(line));
        strappend(line, trustStorePolicy.publicInternetReady ? "yes" : "no", sizeof(line));
        strappend(line, "; blocker=", sizeof(line));
        strappend(line, trustStorePolicy.error ? trustStorePolicy.error : "(none)", sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);
    }
    NAV_INFO_TEXT("Plaintext fallback: ", "no");
    NAV_INFO_INT("Document blocks: ", m_metaDocumentBlocks);
    strcopy(line, "Image stats: blocks=", sizeof(line));
    nav_int_to_text(m_metaImageBlocks, number, sizeof(number));
    strappend(line, number, sizeof(line));
    strappend(line, " local=", sizeof(line));
    nav_int_to_text(m_metaLocalImages, number, sizeof(number));
    strappend(line, number, sizeof(line));
    strappend(line, " remote=", sizeof(line));
    nav_int_to_text(m_metaRemoteImages, number, sizeof(number));
    strappend(line, number, sizeof(line));
    strappend(line, " loaded=", sizeof(line));
    nav_int_to_text(m_metaLoadedImages, number, sizeof(number));
    strappend(line, number, sizeof(line));
    strappend(line, " failed=", sizeof(line));
    nav_int_to_text(m_metaFailedImages, number, sizeof(number));
    strappend(line, number, sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    NAV_INFO_TEXT("Last image error: ", m_metaLastImageError[0] ? m_metaLastImageError : "(none)");
    strcopy(line, "CSS: detected=", sizeof(line));
    strappend(line, m_metaCssDetected ? "yes" : "no", sizeof(line));
    strappend(line, "; rules=", sizeof(line));
    nav_int_to_text(m_metaStyleRuleCount, number, sizeof(number));
    strappend(line, number, sizeof(line));
    strappend(line, "; external=", sizeof(line));
    nav_int_to_text(m_metaUnsupportedExternalStylesheetCount, number, sizeof(number));
    strappend(line, number, sizeof(line));
    strappend(line, "; unsupported=", sizeof(line));
    nav_int_to_text(m_metaUnsupportedCssDeclarationCount, number, sizeof(number));
    strappend(line, number, sizeof(line));
    strappend(line, "; capped=", sizeof(line));
    strappend(line, m_metaCssStyleBlockCapped ? "yes" : "no", sizeof(line));
    strappend(line, "; bytes=", sizeof(line));
    nav_int_to_text(m_metaCssStyleBytesProcessed, number, sizeof(number));
    strappend(line, number, sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    strcopy(line, "Text selection: enabled; clipboard mode: ", sizeof(line));
    strappend(line, m_clipboardMode[0] ? m_clipboardMode : "Navigator internal clipboard", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    NAV_INFO_INT("Raw/source bytes: ", m_metaSourceBytes);
    NAV_INFO_TEXT("Source preview truncated: ", m_metaSourceTruncated ? "yes" : "no");
#undef NAV_INFO_TEXT
#undef NAV_INFO_INT

    addBlock(BLOCK_HEADING, "Safety Limits");
    addBlock(BLOCK_LIST_ITEM, "HTTP header limit: 32768 bytes");
    addBlock(BLOCK_LIST_ITEM, "HTTP body limit: 262144 bytes");
    addBlock(BLOCK_LIST_ITEM, "Forms-lite POST body limit: 8192 bytes");
    addBlock(BLOCK_LIST_ITEM, "HTTP redirect limit: 5");
    addBlock(BLOCK_LIST_ITEM, "HTTP timeouts: 5000 ms connect/read");
    addBlock(BLOCK_LIST_ITEM, "DNS lookup: A records only, timeout 3000 ms, retries 3");
    addBlock(BLOCK_LIST_ITEM, "File text/source preview limit: 32768 bytes");
    addBlock(BLOCK_LIST_ITEM, "Stored source preview limit: 2048 bytes");
    addBlock(BLOCK_LIST_ITEM, "Remote PNG byte limit: 262144 bytes");
    addBlock(BLOCK_LIST_ITEM, "Remote PNG dimensions: 2048 x 2048 pixels");
    addBlock(BLOCK_LINK, "View Source", "about:view-source");
    addBlock(BLOCK_LINK, "Navigator Runtime", "about:navigator-runtime");
    addBlock(BLOCK_LINK, "Go to about:navigator", "about:navigator");
}

void NavigatorApp::buildViewSourceDocument()
{
    strcopy(m_currentUrl, "about:view-source", MAX_URL_LEN);
    strcopy(m_title, "View Source", MAX_TITLE_LEN_NAV);
    m_blockCount = 0;
    addBlock(BLOCK_HEADING, "View Source");
    if (!m_metaRequestedUrl[0]) {
        addBlock(BLOCK_PARAGRAPH, "No page has been loaded yet.");
        addBlock(BLOCK_LINK, "Go to about:navigator", "about:navigator");
        return;
    }
    char line[MAX_BLOCK_TEXT];
    strcopy(line, "Source for ", sizeof(line));
    strappend(line, m_metaFinalUrl, sizeof(line));
    addBlock(BLOCK_PARAGRAPH, line);
    if (!m_metaSourcePreview[0]) {
        if (streq_local(m_metaSourceType, "about")) addBlock(BLOCK_PARAGRAPH, "No raw source available for generated about: pages.");
        else addBlock(BLOCK_PARAGRAPH, "No raw source is available for this page.");
    } else {
        if (m_metaSourceTruncated) addBlock(BLOCK_PARAGRAPH, "Showing a bounded source preview.");
        addBlock(BLOCK_PREFORMATTED, m_metaSourcePreview);
    }
    addBlock(BLOCK_LINK, "Page Info", "about:page-info");
    addBlock(BLOCK_LINK, "Navigator Runtime", "about:navigator-runtime");
    addBlock(BLOCK_LINK, "Go to about:navigator", "about:navigator");
}

void NavigatorApp::buildRuntimeDocument()
{
    char previousUrl[MAX_URL_LEN];
    char previousTitle[MAX_TITLE_LEN_NAV];
    int previousBlocks = m_blockCount;
    strcopy(previousUrl, m_currentUrl, MAX_URL_LEN);
    strcopy(previousTitle, m_title, MAX_TITLE_LEN_NAV);

    strcopy(m_currentUrl, "about:navigator-runtime", MAX_URL_LEN);
    strcopy(m_title, "Navigator Runtime", MAX_TITLE_LEN_NAV);
    m_blockCount = 0;
    addBlock(BLOCK_HEADING, "Navigator Runtime");
    addBlock(BLOCK_PARAGRAPH, "This page reports the active bare-metal Navigator launch path and capabilities.");

    addBlock(BLOCK_HEADING, "Runtime");
    addBlock(BLOCK_LIST_ITEM, "Mode: bare-metal/kernel");
    addBlock(BLOCK_LIST_ITEM, "Launch path: AppManager::registerApp -> NavigatorApp::create");
    addBlock(BLOCK_LIST_ITEM, "Rendering owner: NavigatorApp framebuffer/compositor draw path");
    addBlock(BLOCK_LIST_ITEM, "Input owner: NavigatorApp kernel window events");
    addBlock(BLOCK_LIST_ITEM, "Document loading owner: NavigatorApp + VFS + guideWeb-compatible parser adapter");
    addBlock(BLOCK_LIST_ITEM, "Authoritative full path: hosted navigator.cpp app-model process");
    addBlock(BLOCK_LIST_ITEM, "Stale placeholder path: not active");

    addBlock(BLOCK_HEADING, "Capabilities");
    addBlock(BLOCK_LIST_ITEM, "File read: enabled through VFS; Local PNG: enabled through shared ImageAdapter where VFS image data exists");
    addBlock(BLOCK_LIST_ITEM, "HTTP: enabled for numeric IPv4 and hostname HTTP/1.1 GET/POST with redirects and chunked decoding");
    addBlock(BLOCK_LIST_ITEM, "DNS: enabled-basic for A/IPv4 records; HTTP redirects: enabled, limit 5; HTTP chunked transfer decoding: enabled");
    addBlock(BLOCK_LIST_ITEM, "Remote PNG: enabled-basic for numeric IPv4 and hostname http:// PNG images; Downloads: enabled for unsupported HTTP(S) content within the response body limit");
    addBlock(BLOCK_LIST_ITEM, "Bookmark persistence: unavailable; bookmarks are in-memory defaults; HTTPS/TLS: controlled local smoke remains available, and ProductionValidated trust-store policy enables arbitrary-origin bare-metal https:// with DNS, SNI, certificate, and hostname checks without plaintext fallback");
    addBlock(BLOCK_LIST_ITEM, "Public HTTPS: enabled for arbitrary hostnames only after ProductionValidated trust-store prerequisites; IPv4-only, bounded, and fail-closed without a real production CA bundle");
    addBlock(BLOCK_LIST_ITEM, "TLS backend: Mbed TLS bare-metal transport is ready with CA and hostname validation");
    addBlock(BLOCK_LIST_ITEM, "TLS policy layer: shared HttpByteStream transport policy selects plain TCP HTTP, local allowlisted Mbed TLS, or policy-validated Mbed TLS; plaintext fallback stays disabled");
    addBlock(BLOCK_LIST_ITEM, "Content encodings: identity only; unsupported gzip/br/deflate responses produce a friendly document after successful TLS instead of rendering compressed bytes");
    addBlock(BLOCK_LIST_ITEM, "CSS-lite embedded <style>: enabled");
    addBlock(BLOCK_LIST_ITEM, "Forms-lite GET forms: enabled through interactive document controls; Forms-lite POST forms hosted: enabled in authoritative hosted Navigator path");
    addBlock(BLOCK_LIST_ITEM, "Forms-lite POST interactive: enabled");
    addBlock(BLOCK_LIST_ITEM, "Forms-lite POST forms bare-metal: enabled-basic application/x-www-form-urlencoded transport");
    addBlock(BLOCK_LIST_ITEM, "Forms-lite POST redirect policy: 303 becomes GET; 301/302/307/308 preserve POST");
    addBlock(BLOCK_LIST_ITEM, "Forms-lite controls: text, checkbox, radio, textarea, select, submit; Forms-lite focus navigation: Tab/Shift+Tab, Enter, Space where form UI is available");
    {
        char capabilityLine[128];
        strcopy(capabilityLine, "Find in Page: unsupported in bare-metal adapter; Text selection: enabled; Clipboard mode: ", sizeof(capabilityLine));
        strappend(capabilityLine, m_clipboardMode[0] ? m_clipboardMode : "Navigator internal clipboard", sizeof(capabilityLine));
        strappend(capabilityLine, "; External stylesheets: unsupported", sizeof(capabilityLine));
        addBlock(BLOCK_LIST_ITEM, capabilityLine);
    }

    addBlock(BLOCK_HEADING, "Current Document");
    char line[MAX_BLOCK_TEXT];
    char number[24];
    strcopy(line, "URL: ", sizeof(line));
    strappend(line, previousUrl[0] ? previousUrl : "(none)", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    strcopy(line, "Title: ", sizeof(line));
    strappend(line, previousTitle[0] ? previousTitle : "(none)", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    nav_int_to_text(previousBlocks, number, sizeof(number));
    strcopy(line, "Block count: ", sizeof(line));
    strappend(line, number, sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    strcopy(line, "Inspected page: ", sizeof(line));
    strappend(line, m_metaFinalUrl[0] ? m_metaFinalUrl : "(none)", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    addBlock(BLOCK_LIST_ITEM, m_metaCssDetected ? "CSS diagnostics: css detected" : "CSS diagnostics: no css detected");
    strcopy(line, "Last submitted method: ", sizeof(line));
    strappend(line, m_lastSubmittedFormMethod[0] ? m_lastSubmittedFormMethod : "(none)", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    strcopy(line, "Last submitted action: ", sizeof(line));
    strappend(line, m_lastSubmittedFormAction[0] ? m_lastSubmittedFormAction : "(none)", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    strcopy(line, "Last submitted status: ", sizeof(line));
    strappend(line, m_lastSubmittedFormStatus[0] ? m_lastSubmittedFormStatus : "(none)", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    strcopy(line, "Last form error: ", sizeof(line));
    strappend(line, m_lastFormError[0] ? m_lastFormError : "(none)", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    strcopy(line, "Last POST HTTP status: ", sizeof(line));
    strappend(line, m_lastPostHttpStatus[0] ? m_lastPostHttpStatus : "(none)", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    strcopy(line, "Last POST content type: ", sizeof(line));
    strappend(line, m_lastPostContentType[0] ? m_lastPostContentType : "(none)", sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);
    nav_int_to_text(m_lastPostBodyBytes, number, sizeof(number));
    strcopy(line, "Last POST body bytes: ", sizeof(line));
    strappend(line, number, sizeof(line));
    addBlock(BLOCK_LIST_ITEM, line);

    addBlock(BLOCK_HEADING, "TLS Prerequisites");
    char prerequisiteLine[MAX_BLOCK_TEXT];
    int64_t wallClockSeconds = 0;
    char wallClockUtc[32];
    const gxos::GxosRandomQuality rngQuality = gxos::gxos_random_quality();
    const gxos::GxosTlsBackendInfo tlsBackendInfo = gxos::gxos_tls_backend_info();
    const gxos::GxosTlsMbedTlsImportInfo tlsImportInfo = gxos::gxos_tls_mbedtls_import_info();
    const gxos::GxosTlsRuntimeHookInfo tlsHookInfo = gxos::gxos_tls_runtime_hook_info();
    const gxos::GxosTlsArenaInfo tlsArenaInfo = gxos::gxos_tls_arena_info();
    const gxos::GxosCaStoreInfo caStoreInfo = gxos::gxos_ca_store_info();
    const gxos::GxosCaStoreInfo caMissingProbeInfo = probe_missing_ca_path();
    const gxos::GxosTrustStorePolicyInfo trustStorePolicy = gxos::gxos_tls_trust_store_policy_info();
    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const gxos::GxosTlsHostnameValidationInfo hostnameValidationInfo = gxos::gxos_tls_hostname_validation_info();
    const bool localSmokeTlsReady = gxos::gxos_tls_local_smoke_https_ready();
    const char* localSmokeTlsBlocker = gxos::gxos_tls_local_smoke_https_blocker_reason();
    const bool tlsReady = gxos::gxos_tls_prerequisites_ready();
    const char* tlsReadinessBlocker = gxos::gxos_tls_prerequisites_blocker_reason();
    const bool rngReadSmoke = (rngQuality == gxos::GxosRandomQuality::Secure) &&
        streq_local(gxos::gxos_virtio_rng_status(), "success");
    const bool wallClockAvailable = gxos::gxos_wall_clock_unix_seconds(&wallClockSeconds);
    const bool wallClockUtcAvailable = gxos::gxos_wall_clock_utc_text(wallClockUtc, sizeof(wallClockUtc));
    strcopy(prerequisiteLine, "RNG: quality=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_random_quality_name(rngQuality), sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; backend=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_random_backend(), sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "VirtIO RNG: detected=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_virtio_rng_detected() ? "yes" : "no", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; status=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_virtio_rng_status(), sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "Random read smoke: ", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, rngReadSmoke ? "PASS" : "FAIL", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "Wall clock: status=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_wall_clock_status_name(gxos::gxos_wall_clock_status()), sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; backend=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_wall_clock_backend(), sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "Wall clock time: epoch=", sizeof(prerequisiteLine));
    if (wallClockAvailable) {
        char epoch[24];
        nav_i64_to_text(wallClockSeconds, epoch, sizeof(epoch));
        strappend(prerequisiteLine, epoch, sizeof(prerequisiteLine));
    } else {
        strappend(prerequisiteLine, "(unavailable)", sizeof(prerequisiteLine));
    }
    strappend(prerequisiteLine, "; utc=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, wallClockUtcAvailable ? wallClockUtc : "(unavailable)", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "TLS backend: status=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_tls_backend_status_name(tlsBackendInfo.status), sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; name=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, tlsBackendInfo.backendName ? tlsBackendInfo.backendName : "(none)", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "TLS hooks: alloc=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_tls_hook_status_name(tlsHookInfo.allocatorStatus), sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; rng=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_tls_hook_status_name(tlsHookInfo.rngCallbackStatus), sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; time=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_tls_hook_status_name(tlsHookInfo.timeCallbackStatus), sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; psa=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_tls_hook_status_name(tlsHookInfo.psaInitStatus), sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "Mbed TLS import: source=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, tlsImportInfo.sourcePresent ? "yes" : "no", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; compile-ready=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, tlsImportInfo.sourceReadyForCompile ? "yes" : "no", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; config=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, tlsImportInfo.configPresent ? "yes" : "no", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; crypto-config=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, tlsImportInfo.cryptoConfigPresent ? "yes" : "no", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; version=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, tlsImportInfo.detectedVersion ? tlsImportInfo.detectedVersion : "(none)", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "Mbed TLS build plan: manifest=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, tlsImportInfo.buildPlanPath ? tlsImportInfo.buildPlanPath : "(none)", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; config=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, tlsImportInfo.configPath ? tlsImportInfo.configPath : "(none)", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; crypto-config=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, tlsImportInfo.cryptoConfigPath ? tlsImportInfo.cryptoConfigPath : "(none)", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; sources=", sizeof(prerequisiteLine));
    char plannedSources[24];
    nav_i64_to_text(static_cast<int64_t>(tlsImportInfo.plannedSourceCount), plannedSources, sizeof(plannedSources));
    strappend(prerequisiteLine, plannedSources, sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "Mbed TLS TF-PSA dependency: path=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, tlsImportInfo.tfPsaPath ? tlsImportInfo.tfPsaPath : "(none)", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; present=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, tlsImportInfo.tfPsaDependencyPresent ? "yes" : "no", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "Mbed TLS subset: ", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, tlsImportInfo.plannedSubset ? tlsImportInfo.plannedSubset : "(none)", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "TLS arena: status=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_tls_arena_status_name(tlsArenaInfo.status), sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; capacity=", sizeof(prerequisiteLine));
    char arenaCapacity[24];
    nav_i64_to_text(static_cast<int64_t>(tlsArenaInfo.capacityBytes), arenaCapacity, sizeof(arenaCapacity));
    strappend(prerequisiteLine, arenaCapacity, sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; high-water=", sizeof(prerequisiteLine));
    char arenaHighWater[24];
    nav_i64_to_text(static_cast<int64_t>(tlsArenaInfo.highWaterBytes), arenaHighWater, sizeof(arenaHighWater));
    strappend(prerequisiteLine, arenaHighWater, sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "Root CA store: status=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_ca_store_status_name(caStoreInfo.status), sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; parse=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_ca_parse_status_name(caStoreInfo.parseStatus), sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; parsed=", sizeof(prerequisiteLine));
    char parsedCerts[24];
    nav_i64_to_text(static_cast<int64_t>(caStoreInfo.parsedCertificateCount), parsedCerts, sizeof(parsedCerts));
    strappend(prerequisiteLine, parsedCerts, sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; fixture=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, caStoreInfo.testOnlyFixture ? "smoke-only" : "normal", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "Missing-CA probe: policy=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine,
        caMissingProbeInfo.status == gxos::GxosCaStoreStatus::Missing
            ? "fails-closed"
            : "unexpected-state",
        sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; detail=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, caMissingProbeInfo.error ? caMissingProbeInfo.error : "(none)", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "Trust store policy: state=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_trust_store_policy_state_name(trustStorePolicy.state), sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; path=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, trustStorePolicy.path ? trustStorePolicy.path : "(none)", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; source=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_trust_store_source_name(trustStorePolicy.source), sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; production-ready=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, trustStorePolicy.productionReady ? "yes" : "no", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "Trust store source detail: ", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, trustStorePolicy.sourceDetail ? trustStorePolicy.sourceDetail : "(none)", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "HTTPS policy: selected=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_validated_https_policy_state_name(httpsPolicy.selectedState), sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; effective=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, gxos::gxos_validated_https_policy_state_name(httpsPolicy.state), sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; local-smoke=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, localSmokeTlsReady ? "ready" : "blocked", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; fixture-navigation=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, httpsPolicy.validatedNavigationEnabled ? "enabled" : "disabled", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; public-pilot=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, httpsPolicy.broadPublicHttpsEnabled ? "enabled" : "disabled", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "HTTPS policy config: path=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, httpsPolicy.configPath ? httpsPolicy.configPath : "(none)", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; source=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, httpsPolicy.configSource ? httpsPolicy.configSource : "(none)", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "HTTPS policy detail: reason=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, httpsPolicy.localAllowReason ? httpsPolicy.localAllowReason : "(none)", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; blocker=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, httpsPolicy.blocker ? httpsPolicy.blocker : "(none)", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "Public HTTPS pilot reason: ", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, httpsPolicy.publicHttpsPilotReason ? httpsPolicy.publicHttpsPilotReason : "(none)", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "HTTPS policy error: ", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, httpsPolicy.error ? httpsPolicy.error : "(none)", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    if (!localSmokeTlsReady) {
        strcopy(prerequisiteLine, "Local smoke HTTPS blocker: ", sizeof(prerequisiteLine));
        strappend(prerequisiteLine, localSmokeTlsBlocker ? localSmokeTlsBlocker : "(none)", sizeof(prerequisiteLine));
        addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    }
    strcopy(prerequisiteLine, "Hostname validation: available=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, hostnameValidationInfo.available ? "yes" : "no", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; SNI=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, hostnameValidationInfo.sniSupported ? "yes" : "no", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; original-host=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, hostnameValidationInfo.originalHostnameRetained ? "yes" : "no", sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);
    strcopy(prerequisiteLine, "TLS readiness: ", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, tlsReady ? "yes" : "no", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, "; blocker=", sizeof(prerequisiteLine));
    strappend(prerequisiteLine, tlsReady ? "(none)" : tlsReadinessBlocker, sizeof(prerequisiteLine));
    addBlock(BLOCK_LIST_ITEM, prerequisiteLine);

    addBlock(BLOCK_HEADING, "Backends");
    addBlock(BLOCK_LIST_ITEM, "File backend: kernel VFS");
    addBlock(BLOCK_LIST_ITEM, "HTTP backend: kernel shared HttpByteStream policy layer with plain TCP HTTP and controlled local Mbed TLS");
    char dnsLine[96];
    char dnsIp[16];
    kernel::ipv4::ip_to_string(kernel::dns::get_server(), dnsIp);
    strcopy(dnsLine, "DNS backend: kernel UDP DNS client, server ", sizeof(dnsLine));
    strappend(dnsLine, kernel::dns::get_server() ? dnsIp : "(none)", sizeof(dnsLine));
    addBlock(BLOCK_LIST_ITEM, dnsLine);
    addBlock(BLOCK_LIST_ITEM, "Image backend: shared ImageAdapter + framebuffer/compositor drawing");
    addBlock(BLOCK_LIST_ITEM, "Remote PNG backend: kernel HTTP fetch + ImageAdapter::LoadFromBytes");

    addBlock(BLOCK_LINK, "Page Info", "about:page-info");
    addBlock(BLOCK_LINK, "View Source", "about:view-source");
    addBlock(BLOCK_LINK, "Go to about:navigator", "about:navigator");
}

void NavigatorApp::buildErrorDocument(const char* url, const char* reason)
{
    strcopy(m_currentUrl, url ? url : "", MAX_URL_LEN);
    strcopy(m_title, "Navigator Error", MAX_TITLE_LEN_NAV);
    m_blockCount = 0;
    addBlock(BLOCK_HEADING, "Page Not Found");
    addBlock(BLOCK_PARAGRAPH, reason ? reason : "Navigator could not load this page.");
    addBlock(BLOCK_LINK, "Go to about:navigator", "about:navigator");
}

void NavigatorApp::buildHttpsUnsupportedDocument(const char* url, bool redirected, const char* detail)
{
    strcopy(m_currentUrl, url ? url : "", MAX_URL_LEN);
    strcopy(m_title, redirected ? "HTTPS Redirect Unsupported" : "HTTPS Unsupported", MAX_TITLE_LEN_NAV);
    m_blockCount = 0;
    addBlock(BLOCK_HEADING, redirected ? "HTTPS Redirect Unsupported" : "HTTPS Unsupported");
    addBlock(BLOCK_PARAGRAPH, redirected
        ? "Navigator only follows HTTPS redirects that satisfy the active bare-metal TLS policy. ProductionValidated trust enables arbitrary hostnames; other policy states remain fail-closed."
        : "Bare-metal Navigator https:// requires either the controlled local smoke allowlist, dev fixture policy, or ProductionValidated trust prerequisites. Otherwise navigation fails closed.");
    if (detail && detail[0]) addBlock(BLOCK_PARAGRAPH, detail);
    addBlock(BLOCK_LINK, "Page Info", "about:page-info");
    addBlock(BLOCK_LINK, "Go to about:navigator", "about:navigator");
}

void NavigatorApp::buildUnsupportedContentEncodingDocument(const char* url, const char* encoding)
{
    strcopy(m_currentUrl, url ? url : "", MAX_URL_LEN);
    strcopy(m_title, "Unsupported Content Encoding", MAX_TITLE_LEN_NAV);
    m_blockCount = 0;
    addBlock(BLOCK_HEADING, "Unsupported Content Encoding");
    addBlock(BLOCK_PARAGRAPH, "TLS succeeded, but this bare-metal adapter cannot decode compressed response bodies yet.");
    if (url && url[0]) {
        char line[MAX_BLOCK_TEXT];
        strcopy(line, "URL: ", sizeof(line));
        strappend(line, url, sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);
    }
    if (encoding && encoding[0]) {
        char line[MAX_BLOCK_TEXT];
        strcopy(line, "Reported Content-Encoding: ", sizeof(line));
        strappend(line, encoding, sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);
    }
    addBlock(BLOCK_LINK, "Page Info", "about:page-info");
    addBlock(BLOCK_LINK, "Go to about:navigator", "about:navigator");
}

static bool nav_starts_with(const char* value, const char* prefix)
{
    if (!value || !prefix) return false;
    for (int i = 0; prefix[i]; ++i) {
        if (value[i] != prefix[i]) return false;
    }
    return true;
}

static bool nav_ends_with(const char* value, const char* suffix)
{
    if (!value || !suffix) return false;
    int valueLen = 0;
    while (value[valueLen]) ++valueLen;
    int suffixLen = 0;
    while (suffix[suffixLen]) ++suffixLen;
    if (suffixLen > valueLen) return false;
    for (int i = 0; i < suffixLen; ++i) {
        if (value[valueLen - suffixLen + i] != suffix[i]) return false;
    }
    return true;
}

static char nav_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool nav_tag_at(const char* p, const char* tag)
{
    if (!p || *p != '<') return false;
    ++p;
    if (*p == '/') return false;
    int i = 0;
    while (tag[i]) {
        if (nav_lower(p[i]) != tag[i]) return false;
        ++i;
    }
    char end = p[i];
    return end == '>' || end == ' ' || end == '\t' || end == '\r' || end == '\n';
}

static bool nav_close_tag_at(const char* p, const char* tag)
{
    if (!p || p[0] != '<' || p[1] != '/') return false;
    p += 2;
    int i = 0;
    while (tag[i]) {
        if (nav_lower(p[i]) != tag[i]) return false;
        ++i;
    }
    char end = p[i];
    return end == '>' || end == ' ' || end == '\t' || end == '\r' || end == '\n';
}

static const char* nav_find_char(const char* p, char wanted)
{
    while (p && *p) {
        if (*p == wanted) return p;
        ++p;
    }
    return nullptr;
}

static const char* nav_find_close_tag(const char* p, const char* tag)
{
    char pattern[16];
    int k = 0;
    pattern[k++] = '<';
    pattern[k++] = '/';
    for (int i = 0; tag[i] && k < 14; ++i) pattern[k++] = tag[i];
    pattern[k++] = '>';
    pattern[k] = '\0';

    while (p && *p) {
        int i = 0;
        while (pattern[i] && p[i] && nav_lower(p[i]) == pattern[i]) ++i;
        if (!pattern[i]) return p;
        ++p;
    }
    return nullptr;
}

static void nav_append_char(char* out, int outSize, int& oi, char c)
{
    if (oi < outSize - 1) out[oi++] = c;
}

static void nav_copy_clean_text(const char* start, const char* end, char* out, int outSize, bool preserveWhitespace)
{
    int oi = 0;
    bool pendingSpace = false;
    const char* p = start;
    while (p && p < end && *p) {
        if (*p == '<') {
            while (p < end && *p && *p != '>') ++p;
            if (p < end && *p == '>') ++p;
            continue;
        }
        if (!preserveWhitespace && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
            pendingSpace = oi > 0;
            ++p;
            continue;
        }
        if (pendingSpace) {
            nav_append_char(out, outSize, oi, ' ');
            pendingSpace = false;
        }
        if (nav_starts_with(p, "&amp;")) {
            nav_append_char(out, outSize, oi, '&');
            p += 5;
        } else if (nav_starts_with(p, "&nbsp;")) {
            nav_append_char(out, outSize, oi, ' ');
            p += 6;
        } else if (nav_starts_with(p, "&ndash;") || nav_starts_with(p, "&mdash;")) {
            nav_append_char(out, outSize, oi, '-');
            p += 7;
        } else if (nav_starts_with(p, "&#9733;")) {
            nav_append_char(out, outSize, oi, '*');
            p += 7;
        } else {
            nav_append_char(out, outSize, oi, *p++);
        }
    }
    while (oi > 0 && out[oi - 1] == ' ') --oi;
    out[oi] = '\0';
}

static void nav_extract_href(const char* tagStart, const char* tagEnd, char* out, int outSize)
{
    out[0] = '\0';
    const char* p = tagStart;
    while (p && p < tagEnd && *p) {
        if (nav_lower(p[0]) == 'h' && nav_lower(p[1]) == 'r' && nav_lower(p[2]) == 'e' && nav_lower(p[3]) == 'f') {
            p += 4;
            while (p < tagEnd && (*p == ' ' || *p == '\t')) ++p;
            if (p < tagEnd && *p == '=') ++p;
            while (p < tagEnd && (*p == ' ' || *p == '\t')) ++p;
            char quote = (*p == '"' || *p == '\'') ? *p++ : ' ';
            int oi = 0;
            while (p < tagEnd && *p && oi < outSize - 1) {
                if ((quote != ' ' && *p == quote) || (quote == ' ' && (*p == ' ' || *p == '\t' || *p == '>'))) break;
                out[oi++] = *p++;
            }
            out[oi] = '\0';
            return;
        }
        ++p;
    }
}

static bool nav_attr_name_at(const char* p, const char* attr)
{
    int i = 0;
    while (attr[i]) {
        if (nav_lower(p[i]) != attr[i]) return false;
        ++i;
    }
    char c = p[i];
    return c == '=' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void nav_extract_attr(const char* tagStart, const char* tagEnd, const char* attr, char* out, int outSize)
{
    out[0] = '\0';
    const char* p = tagStart;
    while (p && p < tagEnd && *p) {
        if (nav_attr_name_at(p, attr)) {
            int attrLen = 0;
            while (attr[attrLen]) ++attrLen;
            p += attrLen;
            while (p < tagEnd && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
            if (p < tagEnd && *p == '=') ++p;
            while (p < tagEnd && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
            char quote = (*p == '"' || *p == '\'') ? *p++ : ' ';
            int oi = 0;
            while (p < tagEnd && *p && oi < outSize - 1) {
                if ((quote != ' ' && *p == quote) ||
                    (quote == ' ' && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '>'))) break;
                out[oi++] = *p++;
            }
            out[oi] = '\0';
            return;
        }
        ++p;
    }
}

static bool nav_has_attr(const char* tagStart, const char* tagEnd, const char* attr)
{
    const char* p = tagStart;
    while (p && p < tagEnd && *p) {
        if (nav_attr_name_at(p, attr)) return true;
        ++p;
    }
    return false;
}

static void nav_lower_string(char* value)
{
    if (!value) return;
    for (int i = 0; value[i]; ++i) value[i] = nav_lower(value[i]);
}

static int nav_parse_positive_int(const char* value)
{
    if (!value) return 0;
    int out = 0;
    while (*value >= '0' && *value <= '9') {
        out = out * 10 + (*value - '0');
        if (out > 4096) return 4096;
        ++value;
    }
    return out > 0 ? out : 0;
}

static int nav_hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool nav_parse_css_color(const char* value, uint32_t& out)
{
    if (!value) return false;
    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') ++value;
    char lower[24];
    int n = 0;
    while (value[n] && value[n] != ' ' && value[n] != '\t' && value[n] != '\r' && value[n] != '\n' && n < 23) { lower[n] = nav_lower(value[n]); ++n; }
    lower[n] = '\0';
    if (lower[0] == '#' && n == 7) {
        int r1 = nav_hex_value(lower[1]); int r2 = nav_hex_value(lower[2]); int g1 = nav_hex_value(lower[3]); int g2 = nav_hex_value(lower[4]); int b1 = nav_hex_value(lower[5]); int b2 = nav_hex_value(lower[6]);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) return false;
        out = 0xFF000000u | (uint32_t)((r1 * 16 + r2) << 16) | (uint32_t)((g1 * 16 + g2) << 8) | (uint32_t)(b1 * 16 + b2);
        return true;
    }
    if (lower[0] == '#' && n == 4) {
        int r = nav_hex_value(lower[1]); int g = nav_hex_value(lower[2]); int b = nav_hex_value(lower[3]);
        if (r < 0 || g < 0 || b < 0) return false;
        out = 0xFF000000u | (uint32_t)(((r * 16 + r) << 16) | ((g * 16 + g) << 8) | (b * 16 + b));
        return true;
    }
    if (streq_local(lower, "black")) { out = 0xFF000000u; return true; }
    if (streq_local(lower, "white")) { out = 0xFFFFFFFFu; return true; }
    if (streq_local(lower, "red"))   { out = 0xFFFF0000u; return true; }
    if (streq_local(lower, "green")) { out = 0xFF008000u; return true; }
    if (streq_local(lower, "blue"))  { out = 0xFF0000FFu; return true; }
    if (streq_local(lower, "gray") || streq_local(lower, "grey")) { out = 0xFF808080u; return true; }
    return false;
}

static int nav_parse_css_px(const char* value, bool& ok)
{
    ok = false;
    if (!value) return 0;
    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') ++value;
    int result = 0;
    if (*value < '0' || *value > '9') return 0;
    while (*value >= '0' && *value <= '9') { result = result * 10 + (*value - '0'); if (result > 4096) result = 4096; ++value; }
    if (nav_lower(value[0]) == 'p' && nav_lower(value[1]) == 'x') value += 2;
    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') ++value;
    if (*value != '\0') return 0;
    ok = true;
    return result;
}

static void nav_trim_lower_copy(const char* start, const char* end, char* out, int outSize)
{
    while (start < end && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) ++start;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) --end;
    int i = 0; while (start < end && i < outSize - 1) out[i++] = nav_lower(*start++); out[i] = '\0';
}

static void nav_style_merge(gxos::web::WebStyle& base, const gxos::web::WebStyle& overrideStyle)
{
    if (overrideStyle.hasColor) { base.hasColor = true; base.color = overrideStyle.color; }
    if (overrideStyle.hasBackgroundColor) { base.hasBackgroundColor = true; base.backgroundColor = overrideStyle.backgroundColor; }
    if (overrideStyle.bold) base.bold = true;
    if (overrideStyle.italic) base.italic = true;
    if (overrideStyle.underline) base.underline = true;
    if (overrideStyle.marginTop >= 0) base.marginTop = overrideStyle.marginTop;
    if (overrideStyle.marginBottom >= 0) base.marginBottom = overrideStyle.marginBottom;
    if (overrideStyle.marginLeft >= 0) base.marginLeft = overrideStyle.marginLeft;
    if (overrideStyle.padding >= 0) base.padding = overrideStyle.padding;
    if (overrideStyle.fontScaleOrSize >= 0) base.fontScaleOrSize = overrideStyle.fontScaleOrSize;
    if (overrideStyle.genericFontFamily != gxos::web::GenericFontFamily::Inherit)
        base.genericFontFamily = overrideStyle.genericFontFamily;
}

static gxos::web::WebStyle nav_default_style_for_tag(const char* tag)
{
    gxos::web::WebStyle style{};
    if (streq_local(tag, "h1")) { style.bold = true; style.marginTop = 10; style.marginBottom = 10; style.fontScaleOrSize = 24; }
    else if (streq_local(tag, "h2")) { style.bold = true; style.marginTop = 8; style.marginBottom = 8; style.fontScaleOrSize = 20; }
    else if (streq_local(tag, "h3")) { style.bold = true; style.marginTop = 6; style.marginBottom = 6; style.fontScaleOrSize = 18; }
    else if (streq_local(tag, "p")) { style.marginTop = 4; style.marginBottom = 8; }
    else if (streq_local(tag, "a")) { style.hasColor = true; style.color = 0xFF1E5CB8u; style.underline = true; style.marginTop = 4; style.marginBottom = 6; }
    else if (streq_local(tag, "li")) { style.marginTop = 2; style.marginBottom = 4; style.marginLeft = 12; }
    else if (streq_local(tag, "pre") || streq_local(tag, "code")) { style.hasBackgroundColor = true; style.backgroundColor = 0xFFE6E8EEu; style.marginTop = 6; style.marginBottom = 8; style.padding = 4; style.genericFontFamily = gxos::web::GenericFontFamily::Monospace; }
    else if (streq_local(tag, "img")) { style.marginTop = 6; style.marginBottom = 6; }
    return style;
}

enum NavCssSelectorType { NAV_CSS_ELEMENT = 0, NAV_CSS_CLASS = 1, NAV_CSS_ID = 2 };
struct NavCssRule { int selectorType; char selector[32]; gxos::web::WebStyle style; };

static bool nav_supported_css_element(const char* s)
{
    return streq_local(s, "body") || streq_local(s, "h1") || streq_local(s, "h2") || streq_local(s, "h3") || streq_local(s, "p") || streq_local(s, "a") || streq_local(s, "li") || streq_local(s, "pre") || streq_local(s, "code") || streq_local(s, "img");
}

static bool nav_parse_css_selector(const char* start, const char* end, NavCssRule& rule)
{
    char selector[32]; nav_trim_lower_copy(start, end, selector, sizeof(selector));
    if (!selector[0]) return false;
    if (selector[0] == '.') { rule.selectorType = NAV_CSS_CLASS; strcopy(rule.selector, selector + 1, sizeof(rule.selector)); return rule.selector[0] != '\0'; }
    if (selector[0] == '#') { rule.selectorType = NAV_CSS_ID; strcopy(rule.selector, selector + 1, sizeof(rule.selector)); return rule.selector[0] != '\0'; }
    if (!nav_supported_css_element(selector)) return false;
    rule.selectorType = NAV_CSS_ELEMENT; strcopy(rule.selector, selector, sizeof(rule.selector)); return true;
}

static bool nav_parse_font_family(const char* value, gxos::web::GenericFontFamily& outFamily)
{
    if (!value) return false;
    bool sawUnknown = false;
    const char* p = value;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') ++p;
        const char* end = p;
        while (*end && *end != ',') ++end;
        char token[40];
        nav_trim_lower_copy(p, end, token, sizeof(token));
        int tokenLen = strlen_local(token);
        if (tokenLen >= 2 && ((token[0] == '\'' && token[tokenLen - 1] == '\'') ||
                              (token[0] == '"' && token[tokenLen - 1] == '"'))) {
            for (int i = 1; i + 1 < tokenLen; ++i) token[i - 1] = token[i];
            token[tokenLen - 2] = '\0';
        }
        if (streq_local(token, "roboto")) { outFamily = gxos::web::GenericFontFamily::Roboto; return true; }
        if (streq_local(token, "sans-serif")) { outFamily = gxos::web::GenericFontFamily::SansSerif; return true; }
        if (streq_local(token, "monospace")) { outFamily = gxos::web::GenericFontFamily::Monospace; return true; }
        if (streq_local(token, "serif")) { outFamily = gxos::web::GenericFontFamily::Serif; return true; }
        if (token[0]) sawUnknown = true;
        p = end;
        if (*p == ',') ++p;
    }
    if (sawUnknown) {
        outFamily = gxos::web::GenericFontFamily::Unknown;
        return true;
    }
    return false;
}

static void nav_apply_css_decl(gxos::web::WebStyle& style, const char* propStart, const char* propEnd, const char* valueStart, const char* valueEnd, gxos::web::CssDiagnostics& diag)
{
    char prop[32]; char value[48]; nav_trim_lower_copy(propStart, propEnd, prop, sizeof(prop)); nav_trim_lower_copy(valueStart, valueEnd, value, sizeof(value));
    if (!prop[0] || !value[0]) return;
    uint32_t color = 0; bool ok = false;
    if (streq_local(prop, "color")) { if (nav_parse_css_color(value, color)) { style.hasColor = true; style.color = color; } else ++diag.unsupportedDeclarationCount; }
    else if (streq_local(prop, "background-color")) { if (nav_parse_css_color(value, color)) { style.hasBackgroundColor = true; style.backgroundColor = color; } else ++diag.unsupportedDeclarationCount; }
    else if (streq_local(prop, "font-weight")) { if (streq_local(value, "bold")) style.bold = true; else if (!streq_local(value, "normal")) ++diag.unsupportedDeclarationCount; }
    else if (streq_local(prop, "font-style")) { if (streq_local(value, "italic") || streq_local(value, "oblique")) style.italic = true; else if (!streq_local(value, "normal")) ++diag.unsupportedDeclarationCount; }
    else if (streq_local(prop, "font-family")) { gxos::web::GenericFontFamily family = gxos::web::GenericFontFamily::Unknown; if (nav_parse_font_family(value, family)) style.genericFontFamily = family; else ++diag.unsupportedDeclarationCount; }
    else if (streq_local(prop, "text-decoration")) { if (streq_local(value, "underline")) style.underline = true; else if (!streq_local(value, "none")) ++diag.unsupportedDeclarationCount; }
    else if (streq_local(prop, "margin")) { int px = nav_parse_css_px(value, ok); if (ok) { style.marginTop = px; style.marginBottom = px; style.marginLeft = px; } else ++diag.unsupportedDeclarationCount; }
    else if (streq_local(prop, "margin-top")) { int px = nav_parse_css_px(value, ok); if (ok) style.marginTop = px; else ++diag.unsupportedDeclarationCount; }
    else if (streq_local(prop, "margin-bottom")) { int px = nav_parse_css_px(value, ok); if (ok) style.marginBottom = px; else ++diag.unsupportedDeclarationCount; }
    else if (streq_local(prop, "margin-left")) { int px = nav_parse_css_px(value, ok); if (ok) style.marginLeft = px; else ++diag.unsupportedDeclarationCount; }
    else if (streq_local(prop, "padding")) { int px = nav_parse_css_px(value, ok); if (ok) style.padding = px; else ++diag.unsupportedDeclarationCount; }
    else if (streq_local(prop, "font-size")) { int px = nav_parse_css_px(value, ok); if (ok) style.fontScaleOrSize = px; else ++diag.unsupportedDeclarationCount; }
    else { ++diag.unsupportedDeclarationCount; }
}

static void nav_parse_css_decls(const char* start, const char* end, gxos::web::WebStyle& style, gxos::web::CssDiagnostics& diag)
{
    const char* p = start;
    while (p < end) { const char* semi = p; while (semi < end && *semi != ';') ++semi; const char* colon = p; while (colon < semi && *colon != ':') ++colon; if (colon < semi) nav_apply_css_decl(style, p, colon, colon + 1, semi, diag); p = semi < end ? semi + 1 : end; }
}

static void nav_parse_css_block(const char* start, const char* end, NavCssRule* rules, int& ruleCount, int maxRules, gxos::web::CssDiagnostics& diag, gxos::web::WebStyle& bodyStyle)
{
    const int kCssLimit = 16 * 1024; int remaining = kCssLimit - (int)diag.styleBytesProcessed; if (remaining <= 0) { diag.styleBlockCapped = true; return; }
    int blockLen = (int)(end - start); if (blockLen > remaining) { blockLen = remaining; diag.styleBlockCapped = true; } end = start + blockLen; diag.cssDetected = true; diag.styleBytesProcessed += (unsigned long)blockLen;
    const char* p = start;
    while (p < end && ruleCount < maxRules) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
        const char* brace = p; while (brace < end && *brace != '{') ++brace; if (brace >= end) break;
        const char* close = brace + 1; while (close < end && *close != '}') ++close; if (close >= end) break;
        const char* selStart = p;
        while (selStart < brace) { const char* comma = selStart; while (comma < brace && *comma != ',') ++comma; NavCssRule rule{}; if (nav_parse_css_selector(selStart, comma, rule)) { nav_parse_css_decls(brace + 1, close, rule.style, diag); if (rule.selectorType == NAV_CSS_ELEMENT && streq_local(rule.selector, "body")) bodyStyle = rule.style; rules[ruleCount++] = rule; ++diag.styleRuleCount; } else { ++diag.unsupportedDeclarationCount; } selStart = comma < brace ? comma + 1 : brace; }
        p = close + 1;
    }
}

static bool nav_class_matches(const char* classes, const char* selector)
{
    const char* p = classes ? classes : "";
    while (*p) { while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p; const char* start = p; while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') ++p; char token[32]; nav_trim_lower_copy(start, p, token, sizeof(token)); if (streq_local(token, selector)) return true; }
    return false;
}

static gxos::web::WebStyle nav_style_for_tag(const char* tag, const char* className, const char* id, const gxos::web::WebStyle& bodyStyle, const NavCssRule* rules, int ruleCount)
{
    gxos::web::WebStyle style = bodyStyle; gxos::web::WebStyle defaults = nav_default_style_for_tag(tag); nav_style_merge(style, defaults);
    for (int i = 0; i < ruleCount; ++i) { bool match = false; if (rules[i].selectorType == NAV_CSS_ELEMENT) match = streq_local(tag, rules[i].selector); else if (rules[i].selectorType == NAV_CSS_CLASS) match = nav_class_matches(className, rules[i].selector); else if (rules[i].selectorType == NAV_CSS_ID) match = streq_local(id ? id : "", rules[i].selector); if (match && !(rules[i].selectorType == NAV_CSS_ELEMENT && streq_local(rules[i].selector, "body"))) nav_style_merge(style, rules[i].style); }
    return style;
}

static void nav_scan_css(const char* html, NavCssRule* rules, int& ruleCount, int maxRules, gxos::web::CssDiagnostics& diag, gxos::web::WebStyle& bodyStyle)
{
    const char* p = html;
    while (p && *p) { if (*p != '<') { ++p; continue; } const char* tagEnd = nav_find_char(p, '>'); if (!tagEnd) break; if (nav_tag_at(p, "style")) { const char* close = nav_find_close_tag(tagEnd + 1, "style"); if (close) { nav_parse_css_block(tagEnd + 1, close, rules, ruleCount, maxRules, diag, bodyStyle); p = nav_find_char(close, '>'); if (p && *p == '>') ++p; continue; } } else if (nav_tag_at(p, "link")) { char rel[32]; nav_extract_attr(p, tagEnd, "rel", rel, sizeof(rel)); for (int i = 0; rel[i]; ++i) rel[i] = nav_lower(rel[i]); if (streq_local(rel, "stylesheet")) ++diag.unsupportedExternalStylesheetCount; } p = tagEnd + 1; }
}

void NavigatorApp::resolveHref(const char* baseUrl, const char* href, char* out, int outSize) const
{
    if (!href || !href[0]) {
        strcopy(out, baseUrl ? baseUrl : "about:navigator", outSize);
        return;
    }
    if (nav_starts_with(href, "about:") || nav_starts_with(href, "file://") ||
        nav_starts_with(href, "http://") || nav_starts_with(href, "https://")) {
        strcopy(out, href, outSize);
        return;
    }
    if (href[0] == '#') {
        strcopy(out, baseUrl ? baseUrl : "about:navigator", outSize);
        return;
    }
    if (href[0] == '/') {
        if (baseUrl && (nav_starts_with(baseUrl, "http://") || nav_starts_with(baseUrl, "https://"))) {
            strcopy(out, baseUrl, outSize);
            int len = strlen_local(out);
            int originLen = 7;
            while (originLen < len && out[originLen] != '/') ++originLen;
            out[originLen] = '\0';
            len = strlen_local(out);
            strcopy(out + len, href, outSize - len);
            return;
        }
        strcopy(out, "file://", outSize);
        int len = strlen_local(out);
        strcopy(out + len, href, outSize - len);
        return;
    }
    strcopy(out, baseUrl ? baseUrl : "file:///", outSize);
    int len = strlen_local(out);
    while (len > 0 && out[len - 1] != '/') --len;
    out[len] = '\0';
    strcopy(out + len, href, outSize - len);
}

void NavigatorApp::parseHtmlDocument(const char* url, const char* html, const char* sourceType, const char* contentType, int httpStatusCode, const char* httpReason, const char* requestedUrl, int redirectCount, const KernelHttpResponse* networkResponse)
{
    strcopy(m_currentUrl, url ? url : "", MAX_URL_LEN);
    strcopy(m_title, url ? url : "Document", MAX_TITLE_LEN_NAV);
    m_blockCount = 0;
    blurFormBlock();

    NavCssRule cssRules[32]{};
    int cssRuleCount = 0;
    gxos::web::CssDiagnostics cssDiagnostics{};
    gxos::web::WebStyle bodyStyle{};
    nav_scan_css(html ? html : "", cssRules, cssRuleCount, 32, cssDiagnostics, bodyStyle);

    int nextFormIndex = 0;
    int currentFormIndex = -1;
    char currentFormAction[MAX_URL_LEN];
    char currentFormMethod[8];
    char currentFormEncoding[48];
    bool currentFormUnsupported = false;
    currentFormAction[0] = '\0';
    strcopy(currentFormMethod, "get", sizeof(currentFormMethod));
    strcopy(currentFormEncoding, "application/x-www-form-urlencoded", sizeof(currentFormEncoding));

    auto addFormBlock = [&](BlockKind kind, const char* tagName) -> DocBlock* {
        if (currentFormIndex < 0 || m_blockCount >= MAX_BLOCKS) return nullptr;
        gxos::web::WebStyle style = nav_style_for_tag(tagName, "", "", bodyStyle, cssRules, cssRuleCount);
        addBlock(kind, "", "", &style);
        DocBlock& block = m_blocks[m_blockCount - 1];
        block.formIndex = currentFormIndex;
        strcopy(block.formAction, currentFormAction[0] ? currentFormAction : url, sizeof(block.formAction));
        strcopy(block.formMethod, currentFormMethod, sizeof(block.formMethod));
        strcopy(block.formEncoding, currentFormEncoding, sizeof(block.formEncoding));
        block.formUnsupported = currentFormUnsupported;
        return &block;
    };

    const char* p = html;
    while (p && *p && m_blockCount < MAX_BLOCKS) {
        const char* tagEnd = (*p == '<') ? nav_find_char(p, '>') : nullptr;
        if (!tagEnd) { ++p; continue; }

        const char* close = nullptr;
        char text[MAX_BLOCK_TEXT];
        char className[64];
        char id[64];
        nav_extract_attr(p, tagEnd, "class", className, sizeof(className));
        nav_extract_attr(p, tagEnd, "id", id, sizeof(id));
        if (nav_close_tag_at(p, "form")) {
            currentFormIndex = -1;
            currentFormAction[0] = '\0';
            strcopy(currentFormMethod, "get", sizeof(currentFormMethod));
            strcopy(currentFormEncoding, "application/x-www-form-urlencoded", sizeof(currentFormEncoding));
            currentFormUnsupported = false;
        } else if (nav_tag_at(p, "form")) {
            char action[MAX_URL_LEN];
            nav_extract_attr(p, tagEnd, "action", action, sizeof(action));
            nav_extract_attr(p, tagEnd, "method", currentFormMethod, sizeof(currentFormMethod));
            nav_extract_attr(p, tagEnd, "enctype", currentFormEncoding, sizeof(currentFormEncoding));
            if (!currentFormMethod[0]) strcopy(currentFormMethod, "get", sizeof(currentFormMethod));
            if (!currentFormEncoding[0]) strcopy(currentFormEncoding, "application/x-www-form-urlencoded", sizeof(currentFormEncoding));
            nav_lower_string(currentFormMethod);
            nav_lower_string(currentFormEncoding);
            resolveHref(url, action[0] ? action : url, currentFormAction, sizeof(currentFormAction));
            currentFormUnsupported =
                (!streq_local(currentFormMethod, "get") && !streq_local(currentFormMethod, "post")) ||
                !streq_local(currentFormEncoding, "application/x-www-form-urlencoded");
            currentFormIndex = nextFormIndex++;
        } else if (nav_tag_at(p, "input") && currentFormIndex >= 0) {
            char type[24];
            nav_extract_attr(p, tagEnd, "type", type, sizeof(type));
            nav_lower_string(type);
            if (!type[0] || streq_local(type, "text")) {
                DocBlock* block = addFormBlock(BLOCK_FORM_TEXT, "input");
                if (block) {
                    nav_extract_attr(p, tagEnd, "name", block->inputName, sizeof(block->inputName));
                    nav_extract_attr(p, tagEnd, "value", block->inputValue, sizeof(block->inputValue));
                    nav_extract_attr(p, tagEnd, "placeholder", block->placeholder, sizeof(block->placeholder));
                    block->disabled = nav_has_attr(p, tagEnd, "disabled");
                }
            } else if (streq_local(type, "checkbox") || streq_local(type, "radio")) {
                DocBlock* block = addFormBlock(streq_local(type, "checkbox") ? BLOCK_FORM_CHECKBOX : BLOCK_FORM_RADIO, "input");
                if (block) {
                    nav_extract_attr(p, tagEnd, "name", block->inputName, sizeof(block->inputName));
                    nav_extract_attr(p, tagEnd, "value", block->inputValue, sizeof(block->inputValue));
                    if (!block->inputValue[0]) strcopy(block->inputValue, "on", sizeof(block->inputValue));
                    strcopy(block->text, block->inputName, sizeof(block->text));
                    block->checked = nav_has_attr(p, tagEnd, "checked");
                    block->disabled = nav_has_attr(p, tagEnd, "disabled");
                }
            } else if (streq_local(type, "submit")) {
                DocBlock* block = addFormBlock(BLOCK_FORM_SUBMIT, "input");
                if (block) {
                    nav_extract_attr(p, tagEnd, "value", block->submitLabel, sizeof(block->submitLabel));
                    if (!block->submitLabel[0]) strcopy(block->submitLabel, "Submit", sizeof(block->submitLabel));
                    block->disabled = nav_has_attr(p, tagEnd, "disabled");
                }
            }
        } else if (nav_tag_at(p, "textarea") && currentFormIndex >= 0) {
            close = nav_find_close_tag(tagEnd + 1, "textarea");
            DocBlock* block = addFormBlock(BLOCK_FORM_TEXTAREA, "textarea");
            if (block) {
                char rows[12];
                nav_extract_attr(p, tagEnd, "name", block->inputName, sizeof(block->inputName));
                nav_extract_attr(p, tagEnd, "placeholder", block->placeholder, sizeof(block->placeholder));
                nav_extract_attr(p, tagEnd, "rows", rows, sizeof(rows));
                block->visibleRows = nav_parse_positive_int(rows);
                block->disabled = nav_has_attr(p, tagEnd, "disabled");
                if (close) nav_copy_clean_text(tagEnd + 1, close, block->inputValue, sizeof(block->inputValue), true);
            }
        } else if (nav_tag_at(p, "select") && currentFormIndex >= 0) {
            close = nav_find_close_tag(tagEnd + 1, "select");
            DocBlock* block = addFormBlock(BLOCK_FORM_SELECT, "select");
            if (block) {
                nav_extract_attr(p, tagEnd, "name", block->inputName, sizeof(block->inputName));
                block->disabled = nav_has_attr(p, tagEnd, "disabled");
                const char* option = tagEnd + 1;
                while (close && option < close && block->optionCount < MAX_FORM_OPTIONS) {
                    while (option < close && *option != '<') ++option;
                    if (option >= close) break;
                    const char* optionEnd = nav_find_char(option, '>');
                    if (!optionEnd || optionEnd >= close) break;
                    if (!nav_tag_at(option, "option")) {
                        option = optionEnd + 1;
                        continue;
                    }
                    const char* optionClose = nav_find_close_tag(optionEnd + 1, "option");
                    if (!optionClose || optionClose > close) break;
                    FormOption& formOption = block->options[block->optionCount];
                    nav_extract_attr(option, optionEnd, "value", formOption.value, sizeof(formOption.value));
                    nav_copy_clean_text(optionEnd + 1, optionClose, formOption.text, sizeof(formOption.text), false);
                    if (!formOption.value[0]) strcopy(formOption.value, formOption.text, sizeof(formOption.value));
                    if (block->selectedOption < 0 || nav_has_attr(option, optionEnd, "selected")) {
                        block->selectedOption = block->optionCount;
                    }
                    ++block->optionCount;
                    option = nav_find_char(optionClose, '>');
                    if (option) ++option;
                }
                if (block->selectedOption >= 0 && block->selectedOption < block->optionCount) {
                    strcopy(block->inputValue, block->options[block->selectedOption].value, sizeof(block->inputValue));
                    strcopy(block->text, block->options[block->selectedOption].text, sizeof(block->text));
                }
            }
        } else if (nav_tag_at(p, "button") && currentFormIndex >= 0) {
            close = nav_find_close_tag(tagEnd + 1, "button");
            char type[24];
            nav_extract_attr(p, tagEnd, "type", type, sizeof(type));
            nav_lower_string(type);
            if (!type[0] || streq_local(type, "submit")) {
                DocBlock* block = addFormBlock(BLOCK_FORM_SUBMIT, "button");
                if (block) {
                    if (close) nav_copy_clean_text(tagEnd + 1, close, block->submitLabel, sizeof(block->submitLabel), false);
                    if (!block->submitLabel[0]) strcopy(block->submitLabel, "Submit", sizeof(block->submitLabel));
                    block->disabled = nav_has_attr(p, tagEnd, "disabled");
                }
            }
        } else if (nav_tag_at(p, "title")) {
            close = nav_find_close_tag(tagEnd + 1, "title");
            if (close) nav_copy_clean_text(tagEnd + 1, close, m_title, MAX_TITLE_LEN_NAV, false);
        } else if (nav_tag_at(p, "h1")) {
            close = nav_find_close_tag(tagEnd + 1, "h1");
            if (close) { nav_copy_clean_text(tagEnd + 1, close, text, MAX_BLOCK_TEXT, false); gxos::web::WebStyle style = nav_style_for_tag("h1", className, id, bodyStyle, cssRules, cssRuleCount); addBlock(BLOCK_HEADING, text, "", &style); }
        } else if (nav_tag_at(p, "h2")) {
            close = nav_find_close_tag(tagEnd + 1, "h2");
            if (close) { nav_copy_clean_text(tagEnd + 1, close, text, MAX_BLOCK_TEXT, false); gxos::web::WebStyle style = nav_style_for_tag("h2", className, id, bodyStyle, cssRules, cssRuleCount); addBlock(BLOCK_HEADING, text, "", &style); }
        } else if (nav_tag_at(p, "h3")) {
            close = nav_find_close_tag(tagEnd + 1, "h3");
            if (close) { nav_copy_clean_text(tagEnd + 1, close, text, MAX_BLOCK_TEXT, false); gxos::web::WebStyle style = nav_style_for_tag("h3", className, id, bodyStyle, cssRules, cssRuleCount); addBlock(BLOCK_HEADING, text, "", &style); }
        } else if (nav_tag_at(p, "p")) {
            close = nav_find_close_tag(tagEnd + 1, "p");
            if (close) { nav_copy_clean_text(tagEnd + 1, close, text, MAX_BLOCK_TEXT, false); gxos::web::WebStyle style = nav_style_for_tag("p", className, id, bodyStyle, cssRules, cssRuleCount); addBlock(BLOCK_PARAGRAPH, text, "", &style); }
        } else if (nav_tag_at(p, "li")) {
            close = nav_find_close_tag(tagEnd + 1, "li");
            if (close) { nav_copy_clean_text(tagEnd + 1, close, text, MAX_BLOCK_TEXT, false); gxos::web::WebStyle style = nav_style_for_tag("li", className, id, bodyStyle, cssRules, cssRuleCount); addBlock(BLOCK_LIST_ITEM, text, "", &style); }
        } else if (nav_tag_at(p, "pre")) {
            close = nav_find_close_tag(tagEnd + 1, "pre");
            if (close) { nav_copy_clean_text(tagEnd + 1, close, text, MAX_BLOCK_TEXT, true); gxos::web::WebStyle style = nav_style_for_tag("pre", className, id, bodyStyle, cssRules, cssRuleCount); addBlock(BLOCK_PREFORMATTED, text, "", &style); }
        } else if (nav_tag_at(p, "script")) {
            close = nav_find_close_tag(tagEnd + 1, "script");
        } else if (nav_tag_at(p, "style")) {
            close = nav_find_close_tag(tagEnd + 1, "style");
        } else if (nav_tag_at(p, "img")) {
            char src[MAX_URL_LEN]; char alt[96]; char widthText[16]; char heightText[16]; char resolved[MAX_URL_LEN];
            nav_extract_attr(p, tagEnd, "src", src, MAX_URL_LEN);
            nav_extract_attr(p, tagEnd, "alt", alt, 96);
            nav_extract_attr(p, tagEnd, "width", widthText, 16);
            nav_extract_attr(p, tagEnd, "height", heightText, 16);
            if (src[0]) { resolveHref(url, src, resolved, MAX_URL_LEN); gxos::web::WebStyle style = nav_style_for_tag("img", className, id, bodyStyle, cssRules, cssRuleCount); addImageBlock(src, alt, resolved, nav_parse_positive_int(widthText), nav_parse_positive_int(heightText), &style); }
        } else if (nav_tag_at(p, "a")) {
            close = nav_find_close_tag(tagEnd + 1, "a");
            if (close) { char href[MAX_URL_LEN]; char resolved[MAX_URL_LEN]; nav_extract_href(p, tagEnd, href, MAX_URL_LEN); resolveHref(url, href, resolved, MAX_URL_LEN); nav_copy_clean_text(tagEnd + 1, close, text, MAX_BLOCK_TEXT, false); gxos::web::WebStyle style = nav_style_for_tag("a", className, id, bodyStyle, cssRules, cssRuleCount); addBlock(BLOCK_LINK, text, resolved, &style); }
        }
        p = close ? nav_find_char(close, '>') : tagEnd;
        if (p && *p == '>') ++p;
    }
    if (m_blockCount == 0) {
        gxos::web::WebStyle style = nav_style_for_tag("pre", "", "", bodyStyle, cssRules, cssRuleCount);
        addBlock(BLOCK_PREFORMATTED, html ? html : "", "", &style);
    }
    prepareImageResources();
    rememberPageMetadata(requestedUrl ? requestedUrl : url, url, sourceType ? sourceType : "file",
        contentType ? contentType : "text/html", "", html, html ? strlen_local(html) : 0,
        &cssDiagnostics, &bodyStyle, httpStatusCode, httpReason ? httpReason : "", redirectCount,
        networkResponse);
}

// Kernel Navigator keeps URLs bounded in every document/resource slot while
// allowing ordinary public hostnames and useful paths. IPv6 remains outside
// this IPv4-only transport milestone.
static const int kKernelHttpUrlLen = 512;
static const int kKernelHttpHeaderLimit = gxos::web::kHttpSharedMaxHeaderBytes;
static const int kKernelHttpBodyLimit = gxos::web::kHttpSharedMaxBodyBytes;
static const int kKernelHttpRawLimit = kKernelHttpHeaderLimit + kKernelHttpBodyLimit;
static const int kKernelHttpPostBodyLimit = 8 * 1024;
static const int kKernelHttpConnectTimeoutMs = gxos::web::kHttpSharedConnectTimeoutMs;
static const int kKernelHttpReadTimeoutMs = gxos::web::kHttpSharedReadTimeoutMs;
static const uint32_t kNavigatorTlsSmokeCnMismatchFlag = 0x04u;
static const char* kNavigatorControlledHttpsHost = "guidexos.test";
static const uint16_t kNavigatorControlledHttpsPort = 8443;
static const char* kNavigatorControlledHttpsPathPrefix = "/navigator-smoke/";
static const char* kNavigatorPolicyDevHttpsHost = "dev.guidexos.test";
static const char* kNavigatorPolicyProdHttpsHost = "prod.guidexos.test";
static const char* kNavigatorPolicyValidatedPathPrefix = "/navigator-policy/";
static const char* kNavigatorPublicPilotHttpsHost = "public-pilot.guidexos.test";
static const char* kNavigatorPublicPilotPathPrefix = "/navigator-public-pilot/";
static const char* kNavigatorHttpsSmokeFaultModePath = "/config/navigator/https-fault-mode.txt";
static const char* kNavigatorHttpsSmokeFaultModeCompatPath = "/config/navigator/HTTPSFLT.TXT";
static const char* kNavigatorRealPublicProbeTargetPath = "/config/navigator/real-public-https-probe-url.txt";
static const char* kNavigatorRealPublicProbeTargetCompatPath = "/config/navigator/RPUBURL.TXT";
static const char* kNavigatorRealPublicProbeRequirePath = "/config/navigator/real-public-https-probe-required.txt";
static const char* kNavigatorRealPublicProbeRequireCompatPath = "/config/navigator/RPUBRQ.TXT";
static const char* kNavigatorRealPublicProbeReviewedOverridePath = "/config/navigator/real-public-https-reviewed-override.txt";
static const char* kNavigatorRealPublicProbeReviewedOverrideCompatPath = "/config/navigator/RPUBROV.TXT";
static const char* kNavigatorRealPublicProbeCaSourcePath = "/config/navigator/real-public-https-ca-bundle-source.txt";
static const char* kNavigatorRealPublicProbeCaSourceCompatPath = "/config/navigator/RPUBCAS.TXT";
static const char* kNavigatorRealPublicProbeCaBytesPath = "/config/navigator/real-public-https-ca-bundle-bytes.txt";
static const char* kNavigatorRealPublicProbeCaBytesCompatPath = "/config/navigator/RPUBCABY.TXT";
static const char* kNavigatorRealPublicProbeCaCertsPath = "/config/navigator/real-public-https-ca-bundle-certs.txt";
static const char* kNavigatorRealPublicProbeCaCertsCompatPath = "/config/navigator/RPUBCART.TXT";
static const char* kNavigatorRealPublicProbeCaEnabledPath = "/config/navigator/real-public-https-ca-bundle-enabled.txt";
static const char* kNavigatorRealPublicProbeCaEnabledCompatPath = "/config/navigator/RPUBCAEN.TXT";
static const char* kNavigatorRealPublicProbeDefaultTarget = "https://sha256.badssl.com/";
static const char* kNavigatorRealPublicProbeReviewedAllowlistName = "guidexos-reviewed-public-https-v0.5";
static const uint32_t kNavigatorSmokeTextFileMaxBytes = 512u;

enum class NavigatorHttpsSmokeFaultMode {
    None = 0,
    UntrustedRoot,
    ExpiredCertificate,
    FutureCertificate,
};

struct KernelHttpUrl {
    uint32_t ip;
    uint16_t port;
    bool hostIsNumeric;
    bool httpsScheme;
    char host[gxos::web::kHttpSharedMaxHostnameBytes + 1];
    char path[kKernelHttpUrlLen];
    char error[96];
};

struct KernelHttpResponse {
    bool ok;
    int statusCode;
    char reason[48];
    char contentType[48];
    char transferEncoding[32];
    char responseFraming[24];
    char contentEncoding[32];
    int contentLength;
    bool contentLengthPresent;
    bool truncatedResponse;
    char unsupportedReason[128];
    char location[kKernelHttpUrlLen];
    int bodyBytes;
    int redirectCount;
    bool headerCapHit;
    bool bodyCapHit;
    bool dnsUsed;
    char dnsHost[gxos::web::kHttpSharedMaxHostnameBytes + 1];
    char dnsResolvedIp[16];
    char dnsError[64];
    char requestedUrl[kKernelHttpUrlLen];
    char finalUrl[kKernelHttpUrlLen];
    char error[128];
    char scheme[8];
    gxos::web::HttpByteStreamTransportSelection transportSelection;
    gxos::web::HttpByteStreamTlsStatus tlsStatus;
    char transportPolicyReason[128];
    bool tlsUsed;
    bool tlsAllowlistLocalOnly;
    bool downgradeRedirectBlocked;
    bool tlsSucceededBeforeContentFailure;
    int tlsRetryCount;
    char tlsRetryReason[96];
    int tlsBytesWrittenBeforeRetry;
    bool tcpAbortUsed;
    bool redirectedHttpsRetryUsed;
    int redirectHopIndex;
    char redirectHopUrl[kKernelHttpUrlLen];
    char tlsBackend[48];
    gxos::GxosTlsLocalHandshakeResult tlsResult;
    char body[kKernelHttpBodyLimit + 1];
};

static KernelHttpResponse s_kernelHttpResponse;
static char s_kernelHttpRaw[kKernelHttpRawLimit + 1];
static char s_kernelHttpDocumentBody[kKernelHttpBodyLimit + 1];

static void kernel_http_reset_response(KernelHttpResponse* response)
{
    if (!response) return;
    response->ok = false;
    response->statusCode = 0;
    response->reason[0] = '\0';
    response->contentType[0] = '\0';
    response->transferEncoding[0] = '\0';
    response->responseFraming[0] = '\0';
    response->contentEncoding[0] = '\0';
    response->contentLength = 0;
    response->contentLengthPresent = false;
    response->truncatedResponse = false;
    response->unsupportedReason[0] = '\0';
    response->location[0] = '\0';
    response->bodyBytes = 0;
    response->redirectCount = 0;
    response->headerCapHit = false;
    response->bodyCapHit = false;
    response->dnsUsed = false;
    response->dnsHost[0] = '\0';
    response->dnsResolvedIp[0] = '\0';
    response->dnsError[0] = '\0';
    response->requestedUrl[0] = '\0';
    response->finalUrl[0] = '\0';
    response->error[0] = '\0';
    response->scheme[0] = '\0';
    response->transportSelection = gxos::web::HttpByteStreamTransportSelection::UnsupportedScheme;
    response->tlsStatus = gxos::web::HttpByteStreamTlsStatus::NotApplicable;
    response->transportPolicyReason[0] = '\0';
    response->tlsUsed = false;
    response->tlsAllowlistLocalOnly = false;
    response->downgradeRedirectBlocked = false;
    response->tlsSucceededBeforeContentFailure = false;
    response->tlsRetryCount = 0;
    response->tlsRetryReason[0] = '\0';
    response->tlsBytesWrittenBeforeRetry = 0;
    response->tcpAbortUsed = false;
    response->redirectedHttpsRetryUsed = false;
    response->redirectHopIndex = 0;
    response->redirectHopUrl[0] = '\0';
    response->tlsBackend[0] = '\0';
    response->tlsResult = gxos::GxosTlsLocalHandshakeResult{};
    response->body[0] = '\0';
}

void NavigatorApp::rememberPageMetadata(const char* requestedUrl, const char* finalUrl, const char* sourceType,
                                        const char* contentType, const char* errorStatus,
                                        const char* rawSource, int rawSourceBytes,
                                        const gxos::web::CssDiagnostics* cssDiagnostics,
                                        const gxos::web::WebStyle* bodyStyle,
                                        int httpStatusCode, const char* httpReason,
                                        int redirectCount,
                                        const KernelHttpResponse* networkResponse)
{
    strcopy(m_metaRequestedUrl, requestedUrl ? requestedUrl : "", MAX_URL_LEN);
    strcopy(m_metaFinalUrl, finalUrl ? finalUrl : m_metaRequestedUrl, MAX_URL_LEN);
    strcopy(m_metaSourceType, sourceType ? sourceType : "", sizeof(m_metaSourceType));
    m_metaHttpStatusCode = httpStatusCode;
    strcopy(m_metaHttpReason, httpReason ? httpReason : "", sizeof(m_metaHttpReason));
    strcopy(m_metaContentType, contentType ? contentType : "", sizeof(m_metaContentType));
    strcopy(m_metaContentEncoding, networkResponse ? networkResponse->contentEncoding : "", sizeof(m_metaContentEncoding));
    strcopy(m_metaResponseFraming, networkResponse ? networkResponse->responseFraming : "", sizeof(m_metaResponseFraming));
    m_metaContentLength = networkResponse ? networkResponse->contentLength : 0;
    m_metaContentLengthPresent = networkResponse ? networkResponse->contentLengthPresent : false;
    m_metaTruncatedResponse = networkResponse ? networkResponse->truncatedResponse : false;
    strcopy(m_metaUnsupportedReason, networkResponse ? networkResponse->unsupportedReason : "", sizeof(m_metaUnsupportedReason));
    m_metaRedirectCount = redirectCount;
    m_metaRedirected = redirectCount > 0;
    strcopy(m_metaErrorStatus, errorStatus ? errorStatus : "", sizeof(m_metaErrorStatus));
    m_metaHeaderCapHit = networkResponse ? networkResponse->headerCapHit : false;
    m_metaBodyCapHit = networkResponse ? networkResponse->bodyCapHit : false;
    m_metaTlsSucceededBeforeContentFailure = networkResponse ? networkResponse->tlsSucceededBeforeContentFailure : false;
    m_metaDowngradeRedirectBlocked = networkResponse ? networkResponse->downgradeRedirectBlocked : false;
    strcopy(m_lastDownloadError, errorStatus ? errorStatus : "", sizeof(m_lastDownloadError));
    bool httpSource = sourceType &&
        (streq_local(sourceType, "http") || streq_local(sourceType, "https"));
    strcopy(m_metaScheme,
        networkResponse && networkResponse->scheme[0]
            ? networkResponse->scheme
            : (streq_local(m_metaSourceType, "https") ? "https" :
                (streq_local(m_metaSourceType, "http") ? "http" : "")),
        sizeof(m_metaScheme));
    m_metaDnsUsed = httpSource ? s_kernelLastDnsUsed : false;
    strcopy(m_metaDnsHost, httpSource ? s_kernelLastDnsHost : "", sizeof(m_metaDnsHost));
    strcopy(m_metaDnsResolvedIp, httpSource ? s_kernelLastDnsResolvedIp : "", sizeof(m_metaDnsResolvedIp));
    strcopy(m_metaDnsError, httpSource ? s_kernelLastDnsError : "", sizeof(m_metaDnsError));
    m_metaTlsUsed = networkResponse ? networkResponse->tlsUsed : false;
    m_metaTlsValidated = networkResponse ? networkResponse->tlsResult.certificateValidationSuccess : false;
    m_metaTlsHostnameValidated = networkResponse ? networkResponse->tlsResult.hostnameValidationSuccess : false;
    m_metaTlsAllowlistLocalOnly = networkResponse ? networkResponse->tlsAllowlistLocalOnly : false;
    m_metaTlsVerifyFlags = networkResponse ? networkResponse->tlsResult.verifyFlags : 0u;
    m_metaTlsHandshakeErrorCode = networkResponse ? networkResponse->tlsResult.mbedtlsError : 0;
    m_metaTlsTransportErrorCode = networkResponse ? networkResponse->tlsResult.transportError : 0;
    m_metaTlsRequestBytesWritten = networkResponse ? (int)networkResponse->tlsResult.requestBytesWritten : 0;
    m_metaTlsResponseBytesRead = networkResponse ? (int)networkResponse->tlsResult.responseBytesRead : 0;
    m_metaTlsRetryCount = networkResponse ? networkResponse->tlsRetryCount : 0;
    strcopy(m_metaTlsRetryReason, networkResponse ? networkResponse->tlsRetryReason : "", sizeof(m_metaTlsRetryReason));
    m_metaTlsBytesWrittenBeforeRetry = networkResponse ? networkResponse->tlsBytesWrittenBeforeRetry : 0;
    m_metaTcpAbortUsed = networkResponse ? networkResponse->tcpAbortUsed : false;
    m_metaRedirectedHttpsRetryUsed = networkResponse ? networkResponse->redirectedHttpsRetryUsed : false;
    m_metaRedirectHopIndex = networkResponse ? networkResponse->redirectHopIndex : 0;
    strcopy(m_metaRedirectHopUrl, networkResponse ? networkResponse->redirectHopUrl : "", sizeof(m_metaRedirectHopUrl));
    strcopy(m_metaTlsBackend,
        networkResponse && networkResponse->tlsBackend[0] ? networkResponse->tlsBackend : "",
        sizeof(m_metaTlsBackend));
    strcopy(m_metaTransportSelection,
        networkResponse
            ? gxos::web::httpSharedTransportSelectionName(networkResponse->transportSelection)
            : "",
        sizeof(m_metaTransportSelection));
    strcopy(m_metaTlsStatus,
        networkResponse
            ? gxos::web::httpSharedTlsStatusName(networkResponse->tlsStatus)
            : "",
        sizeof(m_metaTlsStatus));
    strcopy(m_metaTransportPolicyReason,
        networkResponse ? networkResponse->transportPolicyReason : "",
        sizeof(m_metaTransportPolicyReason));
    strcopy(m_metaTlsHostname, httpSource ? m_metaDnsHost : "", sizeof(m_metaTlsHostname));
    strcopy(m_metaTlsSniHost, networkResponse ? networkResponse->tlsResult.sniHost : "", sizeof(m_metaTlsSniHost));
    strcopy(m_metaTlsProtocol, networkResponse ? networkResponse->tlsResult.protocol : "", sizeof(m_metaTlsProtocol));
    strcopy(m_metaTlsCipherSuite, networkResponse ? networkResponse->tlsResult.cipherSuite : "", sizeof(m_metaTlsCipherSuite));
    m_metaSourceBytes = rawSourceBytes > 0 ? rawSourceBytes : 0;
    m_metaCssDetected = cssDiagnostics ? cssDiagnostics->cssDetected : false;
    m_metaStyleRuleCount = cssDiagnostics ? cssDiagnostics->styleRuleCount : 0;
    m_metaUnsupportedExternalStylesheetCount = cssDiagnostics ? cssDiagnostics->unsupportedExternalStylesheetCount : 0;
    m_metaUnsupportedCssDeclarationCount = cssDiagnostics ? cssDiagnostics->unsupportedDeclarationCount : 0;
    m_metaCssStyleBlockCapped = cssDiagnostics ? cssDiagnostics->styleBlockCapped : false;
    m_metaCssStyleBytesProcessed = cssDiagnostics ? (int)cssDiagnostics->styleBytesProcessed : 0;
    m_bodyStyle = bodyStyle ? *bodyStyle : gxos::web::WebStyle{};
    m_metaSourcePreview[0] = '\0';
    m_metaSourceTruncated = false;
    if (rawSource && rawSourceBytes > 0) {
        int copyLen = rawSourceBytes;
        if (copyLen > MAX_SOURCE_PREVIEW - 1) {
            copyLen = MAX_SOURCE_PREVIEW - 1;
            m_metaSourceTruncated = true;
        }
        for (int i = 0; i < copyLen; ++i) m_metaSourcePreview[i] = rawSource[i];
        m_metaSourcePreview[copyLen] = '\0';
    }

    m_metaDocumentBlocks = m_blockCount;
    m_metaImageBlocks = 0;
    m_metaLoadedImages = 0;
    m_metaFailedImages = 0;
    m_metaRemoteImages = 0;
    m_metaLocalImages = 0;
    m_metaLastImageError[0] = '\0';
    m_metaFormCount = 0;
    m_metaTextInputCount = 0;
    m_metaCheckboxCount = 0;
    m_metaRadioCount = 0;
    m_metaTextareaCount = 0;
    m_metaSelectCount = 0;
    m_metaSubmitCount = 0;
    m_metaUnsupportedFormCount = 0;
    int lastFormIndex = -1;
    for (int i = 0; i < m_blockCount; ++i) {
        if (m_blocks[i].kind == BLOCK_IMAGE) {
            ++m_metaImageBlocks;
            if (nav_starts_with(m_blocks[i].url, "http://") ||
                nav_starts_with(m_blocks[i].url, "https://")) ++m_metaRemoteImages;
            else if (nav_starts_with(m_blocks[i].url, "file://")) ++m_metaLocalImages;
            if (m_blocks[i].imageStatus == (int)gxos::gui::ImageLoadStatus::Ok) ++m_metaLoadedImages;
            else {
                ++m_metaFailedImages;
                if (!m_metaLastImageError[0]) {
                    strcopy(m_metaLastImageError,
                        m_blocks[i].imageError[0] ? m_blocks[i].imageError :
                        gxos::gui::ImageLoadStatusName((gxos::gui::ImageLoadStatus)m_blocks[i].imageStatus),
                        sizeof(m_metaLastImageError));
                }
            }
        }
        if (m_blocks[i].formIndex >= 0) {
            if (m_blocks[i].formIndex > lastFormIndex) {
                lastFormIndex = m_blocks[i].formIndex;
                ++m_metaFormCount;
                if (m_blocks[i].formUnsupported) ++m_metaUnsupportedFormCount;
            }
            if (m_blocks[i].kind == BLOCK_FORM_TEXT) ++m_metaTextInputCount;
            else if (m_blocks[i].kind == BLOCK_FORM_CHECKBOX) ++m_metaCheckboxCount;
            else if (m_blocks[i].kind == BLOCK_FORM_RADIO) ++m_metaRadioCount;
            else if (m_blocks[i].kind == BLOCK_FORM_TEXTAREA) ++m_metaTextareaCount;
            else if (m_blocks[i].kind == BLOCK_FORM_SELECT) ++m_metaSelectCount;
            else if (m_blocks[i].kind == BLOCK_FORM_SUBMIT) ++m_metaSubmitCount;
        }
    }
}

static bool parse_numeric_ipv4_local(const char* text, const char* end, uint32_t* out)
{
    if (!text || !end || !out) return false;
    uint32_t parts[4] = {0, 0, 0, 0};
    int part = 0;
    int digits = 0;
    for (const char* p = text; p <= end; ++p) {
        char c = (p < end) ? *p : '.';
        if (c >= '0' && c <= '9') {
            if (part >= 4) return false;
            parts[part] = parts[part] * 10u + (uint32_t)(c - '0');
            if (parts[part] > 255u) return false;
            ++digits;
            if (digits > 3) return false;
        } else if (c == '.') {
            if (digits == 0) return false;
            ++part;
            if (part > 4) return false;
            digits = 0;
        } else {
            return false;
        }
    }
    if (part != 4) return false;
    *out = kernel::ipv4::make_ip((uint8_t)parts[0], (uint8_t)parts[1], (uint8_t)parts[2], (uint8_t)parts[3]);
    return true;
}

static bool nav_hostname_is_valid(const char* start, const char* end)
{
    if (!start || !end || start >= end) return false;
    if (end - start > gxos::web::kHttpSharedMaxHostnameBytes) return false;
    int labelLen = 0;
    bool lastWasDot = true;
    for (const char* p = start; p < end; ++p) {
        char c = *p;
        if (c == '.') {
            if (lastWasDot || labelLen == 0 || labelLen > 63) return false;
            labelLen = 0;
            lastWasDot = true;
            continue;
        }
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-';
        if (!ok) return false;
        ++labelLen;
        lastWasDot = false;
    }
    return !lastWasDot && labelLen > 0 && labelLen <= 63;
}

static bool nav_host_chars_are_numeric_ipv4ish(const char* start, const char* end)
{
    if (!start || !end || start >= end) return false;
    for (const char* p = start; p < end; ++p) {
        if ((*p < '0' || *p > '9') && *p != '.') return false;
    }
    return true;
}

static bool parse_http_url_kernel(const char* url, KernelHttpUrl* parsed)
{
    if (!url || !parsed) return false;
    parsed->ip = 0;
    parsed->port = 80;
    parsed->hostIsNumeric = false;
    parsed->httpsScheme = false;
    parsed->host[0] = '\0';
    parsed->path[0] = '/';
    parsed->path[1] = '\0';
    parsed->error[0] = '\0';

    if (!nav_starts_with(url, "http://")) {
        strcopy(parsed->error, "Only http:// URLs are supported", sizeof(parsed->error));
        return false;
    }
    if (strlen_local(url) >= kKernelHttpUrlLen) {
        strcopy(parsed->error, "HTTP URL exceeds the Navigator safety limit", sizeof(parsed->error));
        return false;
    }

    const char* hostStart = url + 7;
    if (*hostStart == '[') {
        strcopy(parsed->error, "IPv6 HTTP hosts are not supported in bare-metal Navigator yet", sizeof(parsed->error));
        return false;
    }
    const char* p = hostStart;
    while (*p && *p != ':' && *p != '/' && *p != '?' && (p - hostStart) < (int)sizeof(parsed->host) - 1) ++p;
    if (*p && *p != ':' && *p != '/' && *p != '?') {
        strcopy(parsed->error, "HTTP hostname too long", sizeof(parsed->error));
        return false;
    }
    const char* hostEnd = p;
    if (hostEnd == hostStart) {
        strcopy(parsed->error, "Missing HTTP host", sizeof(parsed->error));
        return false;
    }
    int hi = 0;
    for (const char* h = hostStart; h < hostEnd && hi < (int)sizeof(parsed->host) - 1; ++h) parsed->host[hi++] = *h;
    parsed->host[hi] = '\0';

    if (parse_numeric_ipv4_local(hostStart, hostEnd, &parsed->ip)) {
        parsed->hostIsNumeric = true;
    } else {
        if (nav_host_chars_are_numeric_ipv4ish(hostStart, hostEnd)) {
            strcopy(parsed->error, "Invalid numeric IPv4 HTTP host", sizeof(parsed->error));
            return false;
        }
        if (!nav_hostname_is_valid(hostStart, hostEnd)) {
            strcopy(parsed->error, "Invalid HTTP hostname", sizeof(parsed->error));
            return false;
        }
    }

    if (*p == ':') {
        ++p;
        uint32_t port = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            port = port * 10u + (uint32_t)(*p - '0');
            ++digits;
            ++p;
        }
        if (digits == 0 || port == 0 || port > 65535u) {
            strcopy(parsed->error, "Invalid HTTP port", sizeof(parsed->error));
            return false;
        }
        parsed->port = (uint16_t)port;
    }

    if (*p == '/' || *p == '?') {
        int oi = 0;
        if (*p == '?') parsed->path[oi++] = '/';
        while (*p && oi < kKernelHttpUrlLen - 1) parsed->path[oi++] = *p++;
        parsed->path[oi] = '\0';
        if (*p) {
            strcopy(parsed->error, "HTTP URL path exceeds the Navigator safety limit", sizeof(parsed->error));
            return false;
        }
    }
    return true;
}

static void kernel_http_poll_once()
{
    kernel::ipv4::poll_network();
    kernel::tcp::process_timers();
    // TODO: Move bare-metal HTTP to a worker/job model. Until then, give the
    // desktop a cooperative input/render cycle without owning Navigator animation.
    kernel::desktop::cooperative_yield();
    for (volatile int d = 0; d < 20000; ++d) {}
}

static int s_kernelHttpPlainTcpConnectAttempts = 0;
static int s_kernelHttpTlsConnectAttempts = 0;
static int s_kernelHttpControlledLocalHttpsLoads = 0;

struct KernelTcpHttpByteStreamContext {
    int socket;
    bool* abortUsedFlag;
};

static int kernel_tcp_http_byte_stream_read(void* context, uint8_t* buffer, int length)
{
    KernelTcpHttpByteStreamContext* tcp = static_cast<KernelTcpHttpByteStreamContext*>(context);
    if (!tcp || tcp->socket < 0 || !buffer || length <= 0) return kernel::tcp::TCP_ERR_INVALID;
    if (length > 0xFFFF) length = 0xFFFF;
    return kernel::tcp::tcp_recv(tcp->socket, buffer, (uint16_t)length);
}

static int kernel_tcp_http_byte_stream_write(void* context, const uint8_t* buffer, int length)
{
    KernelTcpHttpByteStreamContext* tcp = static_cast<KernelTcpHttpByteStreamContext*>(context);
    if (!tcp || tcp->socket < 0 || !buffer || length <= 0) return kernel::tcp::TCP_ERR_INVALID;
    if (length > 0xFFFF) length = 0xFFFF;
    return kernel::tcp::tcp_send(tcp->socket, buffer, (uint16_t)length);
}

static void kernel_tcp_http_byte_stream_close(void* context)
{
    KernelTcpHttpByteStreamContext* tcp = static_cast<KernelTcpHttpByteStreamContext*>(context);
    if (!tcp || tcp->socket < 0) return;
    // Navigator request streams are one-shot HTTP/1.1 connections that are fully
    // consumed before close. Abort frees the TCB immediately so redirect hops do
    // not inherit linger/TIME_WAIT pressure from the previous socket.
    if (tcp->abortUsedFlag) {
        *tcp->abortUsedFlag = true;
    }
    kernel::tcp::tcp_abort(tcp->socket);
    tcp->socket = -1;
}

static void kernel_tls_smoke_poll(void*)
{
    kernel_http_poll_once();
}

static gxos::web::HttpByteStream make_kernel_tcp_http_byte_stream(KernelTcpHttpByteStreamContext* context, int socket)
{
    context->socket = socket;
    gxos::web::HttpByteStream stream = {
        context,
        kernel_tcp_http_byte_stream_read,
        kernel_tcp_http_byte_stream_write,
        kernel_tcp_http_byte_stream_close
    };
    return stream;
}

static gxos::GxosTlsByteStream make_kernel_tcp_tls_byte_stream(KernelTcpHttpByteStreamContext* context, int socket)
{
    context->socket = socket;
    gxos::GxosTlsByteStream stream = {
        context,
        kernel_tcp_http_byte_stream_read,
        kernel_tcp_http_byte_stream_write,
        kernel_tcp_http_byte_stream_close,
        kernel_tls_smoke_poll
    };
    return stream;
}

struct KernelHttpStreamOpenResult {
    gxos::web::HttpByteStream stream;
    KernelTcpHttpByteStreamContext tcpContext;
};

static bool kernel_https_path_has_allowed_prefix(const char* path)
{
    return path && nav_starts_with(path, kNavigatorControlledHttpsPathPrefix);
}

static bool kernel_https_allowlist_match(const KernelHttpUrl& parsed)
{
    return !parsed.hostIsNumeric &&
        streq_local(parsed.host, kNavigatorControlledHttpsHost) &&
        parsed.port == kNavigatorControlledHttpsPort &&
        kernel_https_path_has_allowed_prefix(parsed.path);
}

static bool kernel_https_policy_fixture_match(const KernelHttpUrl& parsed)
{
    return !parsed.hostIsNumeric &&
        parsed.port == kNavigatorControlledHttpsPort &&
        nav_ends_with(parsed.host, ".guidexos.test") &&
        nav_starts_with(parsed.path, kNavigatorPolicyValidatedPathPrefix);
}

static bool kernel_http_transport_uses_tls(gxos::web::HttpByteStreamTransportSelection selection)
{
    return selection == gxos::web::HttpByteStreamTransportSelection::LocalAllowlistedTlsHttps ||
        selection == gxos::web::HttpByteStreamTransportSelection::PolicyValidatedTlsHttps;
}

static bool parse_https_url_kernel(const char* url, KernelHttpUrl* parsed);

static void kernel_https_allowlist_reason(const KernelHttpUrl& parsed, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    if (parsed.hostIsNumeric) {
        strcopy(out, "HTTPS/TLS unsupported: controlled local HTTPS does not allow numeric-IP hosts", outSize);
        return;
    }
    if (!streq_local(parsed.host, kNavigatorControlledHttpsHost)) {
        strcopy(out, "HTTPS/TLS unsupported: outside controlled local HTTPS allowlist (host)", outSize);
        return;
    }
    if (parsed.port != kNavigatorControlledHttpsPort) {
        strcopy(out, "HTTPS/TLS unsupported: outside controlled local HTTPS allowlist (port)", outSize);
        return;
    }
    if (!kernel_https_path_has_allowed_prefix(parsed.path)) {
        strcopy(out, "HTTPS/TLS unsupported: outside controlled local HTTPS allowlist (path)", outSize);
        return;
    }
    strcopy(out, "HTTPS/TLS unsupported: outside controlled local HTTPS allowlist", outSize);
}

static bool kernel_https_explicit_policy_target_allowed(const KernelHttpUrl& parsed, char* out, int outSize)
{
    if (out && outSize > 0) out[0] = '\0';
    if (parsed.hostIsNumeric) {
        if (out && outSize > 0) {
            strcopy(out, "HTTPS/TLS unsupported: explicit validated HTTPS policy does not allow numeric-IP hosts", outSize);
        }
        return false;
    }
    return true;
}

static bool kernel_https_public_pilot_target_allowed(const KernelHttpUrl& parsed,
                                                     const gxos::GxosValidatedHttpsPolicyInfo& httpsPolicy,
                                                     char* out, int outSize)
{
    if (out && outSize > 0) out[0] = '\0';
    if (parsed.hostIsNumeric) {
        if (out && outSize > 0) {
            strcopy(out, "HTTPS/TLS unsupported: validated HTTPS does not allow numeric-IP hosts", outSize);
        }
        return false;
    }
    if (!httpsPolicy.broadPublicHttpsEnabled) {
        if (out && outSize > 0) {
            strcopy(out,
                httpsPolicy.publicHttpsPilotReason && httpsPolicy.publicHttpsPilotReason[0]
                    ? httpsPolicy.publicHttpsPilotReason
                    : "Production trust validation is not enabled for arbitrary-origin HTTPS.",
                outSize);
        }
        return false;
    }
    return true;
}

static gxos::web::HttpTransportPolicyDecision kernel_http_plain_transport_policy()
{
    return {
        gxos::web::HttpByteStreamTransportSelection::PlainTcpHttp,
        gxos::web::HttpByteStreamTlsStatus::NotApplicable,
        false,
        true,
        false,
        false,
        false,
        "Plain TCP bare-metal",
        "HTTP uses the plain TCP HttpByteStream path."
    };
}

static gxos::web::HttpTransportPolicyDecision kernel_http_allowlisted_tls_transport_policy()
{
    return {
        gxos::web::HttpByteStreamTransportSelection::LocalAllowlistedTlsHttps,
        gxos::web::HttpByteStreamTlsStatus::NotStarted,
        true,
        true,
        true,
        true,
        true,
        "mbedtls",
        "Controlled local HTTPS allowlist matched."
    };
}

static gxos::web::HttpTransportPolicyDecision kernel_http_policy_validated_tls_transport_policy(const char* reason)
{
    return {
        gxos::web::HttpByteStreamTransportSelection::PolicyValidatedTlsHttps,
        gxos::web::HttpByteStreamTlsStatus::NotStarted,
        false,
        true,
        true,
        true,
        true,
        "mbedtls",
        reason ? reason : "Explicit validated HTTPS policy matched."
    };
}

static gxos::web::HttpTransportPolicyDecision kernel_http_blocked_https_transport_policy(const char* reason)
{
    return {
        gxos::web::HttpByteStreamTransportSelection::BlockedHttpsGeneral,
        gxos::web::HttpByteStreamTlsStatus::PolicyBlocked,
        false,
        false,
        false,
        true,
        true,
        "mbedtls",
        reason ? reason : "General bare-metal HTTPS is disabled by policy."
    };
}

static gxos::web::HttpTransportPolicyDecision kernel_http_blocked_policy_transport_policy(const char* reason)
{
    return {
        gxos::web::HttpByteStreamTransportSelection::BlockedPolicy,
        gxos::web::HttpByteStreamTlsStatus::PolicyBlocked,
        false,
        false,
        false,
        false,
        false,
        nullptr,
        reason ? reason : "Navigation was blocked by transport policy."
    };
}

static void kernel_http_apply_transport_policy(KernelHttpResponse* response,
                                               const gxos::web::HttpTransportPolicyDecision& policy,
                                               const char* reason = nullptr)
{
    if (!response) return;
    response->transportSelection = policy.selection;
    response->tlsStatus = policy.tlsStatus;
    response->tlsAllowlistLocalOnly = policy.allowlistMatched;
    strcopy(response->transportPolicyReason,
        reason && reason[0] ? reason : (policy.reason ? policy.reason : ""),
        sizeof(response->transportPolicyReason));
    if (policy.expectedTlsBackend) {
        strcopy(response->tlsBackend, policy.expectedTlsBackend, sizeof(response->tlsBackend));
    }
}

static gxos::web::HttpTransportPolicyDecision kernel_http_transport_policy_for_https(
    const KernelHttpUrl& parsed, char* reason, int reasonSize)
{
    if (kernel_https_allowlist_match(parsed)) {
        if (reason && reasonSize > 0) strcopy(reason, "Controlled local HTTPS allowlist matched.", reasonSize);
        return kernel_http_allowlisted_tls_transport_policy();
    }

    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const bool policyValidatedEnabled =
        httpsPolicy.state == gxos::GxosValidatedHttpsPolicyState::UserTrustStoreDevMode ||
        httpsPolicy.state == gxos::GxosValidatedHttpsPolicyState::ProductionValidated;
    const bool productionValidated =
        httpsPolicy.state == gxos::GxosValidatedHttpsPolicyState::ProductionValidated;
    if (policyValidatedEnabled && kernel_https_policy_fixture_match(parsed)) {
        if (kernel_https_explicit_policy_target_allowed(parsed, reason, reasonSize)) {
            if (reason && reasonSize > 0) {
                strcopy(reason,
                    httpsPolicy.state == gxos::GxosValidatedHttpsPolicyState::UserTrustStoreDevMode
                        ? "Explicit validated HTTPS dev-mode fixture matched."
                        : "Explicit validated HTTPS production fixture matched.",
                    reasonSize);
            }
            return kernel_http_policy_validated_tls_transport_policy(reason);
        }
        return kernel_http_blocked_https_transport_policy(reason);
    }
    if (productionValidated && httpsPolicy.broadPublicHttpsEnabled) {
        if (kernel_https_public_pilot_target_allowed(parsed, httpsPolicy, reason, reasonSize)) {
            if (reason && reasonSize > 0) {
                strcopy(reason, "ProductionValidated arbitrary-origin HTTPS matched.", reasonSize);
            }
            return kernel_http_policy_validated_tls_transport_policy(reason);
        }
        return kernel_http_blocked_policy_transport_policy(reason);
    }
    if (policyValidatedEnabled) {
        if (reason && reasonSize > 0) {
            strcopy(reason,
                httpsPolicy.state == gxos::GxosValidatedHttpsPolicyState::UserTrustStoreDevMode
                    ? "HTTPS/TLS unsupported: UserTrustStoreDevMode only allows explicit validated fixture hosts."
                        : (httpsPolicy.publicHttpsPilotReason && httpsPolicy.publicHttpsPilotReason[0]
                            ? httpsPolicy.publicHttpsPilotReason
                            : "HTTPS/TLS unsupported: production trust validation is not ready for arbitrary origins."),
                reasonSize);
        }
        return kernel_http_blocked_policy_transport_policy(reason);
    }

    if ((httpsPolicy.selectedState == gxos::GxosValidatedHttpsPolicyState::UserTrustStoreDevMode ||
         httpsPolicy.selectedState == gxos::GxosValidatedHttpsPolicyState::ProductionValidated) &&
        httpsPolicy.blocker && httpsPolicy.blocker[0]) {
        if (reason && reasonSize > 0) strcopy(reason, httpsPolicy.blocker, reasonSize);
        return kernel_http_blocked_policy_transport_policy(reason);
    }

    kernel_https_allowlist_reason(parsed, reason, reasonSize);
    return kernel_http_blocked_https_transport_policy(reason);
}

static void kernel_http_apply_redirect_failure_policy(KernelHttpResponse* response,
                                                      const char* resolvedTargetUrl,
                                                      const char* redirectError)
{
    if (!response) return;

    const char* targetUrl = (resolvedTargetUrl && resolvedTargetUrl[0])
        ? resolvedTargetUrl
        : (response->location[0] ? response->location : "");
    if (!targetUrl[0]) return;

    if (nav_starts_with(targetUrl, "https://")) {
        KernelHttpUrl parsed;
        char policyReason[128];
        policyReason[0] = '\0';
        if (parse_https_url_kernel(targetUrl, &parsed)) {
            const gxos::web::HttpTransportPolicyDecision policy =
                kernel_http_transport_policy_for_https(parsed, policyReason, sizeof(policyReason));
            kernel_http_apply_transport_policy(response, policy, policyReason);
        } else {
            const gxos::web::HttpTransportPolicyDecision policy =
                kernel_http_blocked_policy_transport_policy(
                    redirectError && redirectError[0] ? redirectError : parsed.error);
            kernel_http_apply_transport_policy(response, policy,
                redirectError && redirectError[0] ? redirectError : parsed.error);
        }
        strcopy(response->scheme, "https", sizeof(response->scheme));
        return;
    }

    if (nav_starts_with(targetUrl, "http://") &&
        redirectError && streq_local(redirectError, "HTTPS downgrade redirect blocked")) {
        const gxos::web::HttpTransportPolicyDecision policy =
            kernel_http_blocked_policy_transport_policy(redirectError);
        kernel_http_apply_transport_policy(response, policy, redirectError);
        response->downgradeRedirectBlocked = true;
        strcopy(response->scheme, "http", sizeof(response->scheme));
    }
}

static const char* kernel_http_error_name(int err)
{
    switch (err) {
    case kernel::tcp::TCP_ERR_INVALID: return "invalid socket";
    case kernel::tcp::TCP_ERR_NOBUFS: return "no buffers";
    case kernel::tcp::TCP_ERR_NOTCONN: return "not connected";
    case kernel::tcp::TCP_ERR_CONNREFUSED: return "connection refused";
    case kernel::tcp::TCP_ERR_TIMEOUT: return "timeout";
    case kernel::tcp::TCP_ERR_CONNRESET: return "connection reset";
    case kernel::tcp::TCP_ERR_WOULDBLOCK: return "would block";
    case kernel::tcp::TCP_ERR_NOSOCK: return "no socket";
    case kernel::tcp::TCP_ERR_NETDOWN: return "network down";
    default: return "tcp error";
    }
}

static bool kernel_tcp_wait_connected(int sock, char* error, int errorSize)
{
    uint32_t startTicks = (uint32_t)kernel::pit::ticks();
    uint32_t maxTicks = (uint32_t)(kKernelHttpConnectTimeoutMs / 10 + 1);
    while (!kernel::tcp::tcp_isconnected(sock)) {
        kernel_http_poll_once();
        if (kernel::tcp::tcp_getstate(sock) == kernel::tcp::STATE_CLOSED) {
            strcopy(error, "TCP connect failed", errorSize);
            return false;
        }
        if (((uint32_t)kernel::pit::ticks() - startTicks) > maxTicks) {
            strcopy(error, "TCP connect timeout", errorSize);
            return false;
        }
    }
    return true;
}

static void kernel_http_sync_tls_status(KernelHttpResponse* response)
{
    if (!response) return;
    if (response->tlsUsed || response->tlsAllowlistLocalOnly) {
        response->tlsStatus = response->tlsResult.transportStatus;
    }
}

static void kernel_http_set_stream_error(KernelHttpResponse* response,
                                         const char* fallbackError,
                                         gxos::web::HttpByteStreamTlsStatus tlsStatus)
{
    if (!response) return;
    if (kernel_http_transport_uses_tls(response->transportSelection)) {
        if (response->tlsResult.transportStatus == gxos::web::HttpByteStreamTlsStatus::NotStarted) {
            response->tlsResult.transportStatus = tlsStatus;
        }
        kernel_http_sync_tls_status(response);
        strcopy(response->error,
            response->tlsResult.error[0] ? response->tlsResult.error : (fallbackError ? fallbackError : "TLS stream error"),
            sizeof(response->error));
        return;
    }
    strcopy(response->error, fallbackError ? fallbackError : "HTTP stream error", sizeof(response->error));
}

static bool kernel_http_send_all(gxos::web::HttpByteStream* stream, const char* bytes, int byteCount, KernelHttpResponse* response)
{
    if (!bytes || byteCount <= 0) return true;
    if (!stream || !stream->write) return false;
    int sent = 0;
    uint32_t startTicks = (uint32_t)kernel::pit::ticks();
    uint32_t maxTicks = (uint32_t)(kKernelHttpReadTimeoutMs / 10 + 1);
    while (sent < byteCount) {
        int n = stream->write(stream->context,
            reinterpret_cast<const uint8_t*>(bytes + sent), byteCount - sent);
        if (n > 0) {
            sent += n;
            startTicks = (uint32_t)kernel::pit::ticks();
        } else if (n == kernel::tcp::TCP_ERR_WOULDBLOCK) {
            kernel_http_poll_once();
            if (((uint32_t)kernel::pit::ticks() - startTicks) > maxTicks) {
                kernel_http_set_stream_error(response, "HTTP send timeout",
                    gxos::web::HttpByteStreamTlsStatus::TlsWriteFailed);
                return false;
            }
        } else {
            kernel_http_set_stream_error(response, kernel_http_error_name(n),
                gxos::web::HttpByteStreamTlsStatus::TlsWriteFailed);
            return false;
        }
    }
    return true;
}

static bool parse_https_url_kernel(const char* url, KernelHttpUrl* parsed)
{
    if (!url || !parsed) return false;
    parsed->ip = 0;
    parsed->port = 443;
    parsed->hostIsNumeric = false;
    parsed->httpsScheme = true;
    parsed->host[0] = '\0';
    parsed->path[0] = '/';
    parsed->path[1] = '\0';
    parsed->error[0] = '\0';

    if (!nav_starts_with(url, "https://")) {
        strcopy(parsed->error, "Only https:// URLs are supported", sizeof(parsed->error));
        return false;
    }
    if (strlen_local(url) >= kKernelHttpUrlLen) {
        strcopy(parsed->error, "HTTPS URL exceeds the Navigator safety limit", sizeof(parsed->error));
        return false;
    }

    const char* hostStart = url + 8;
    if (*hostStart == '[') {
        strcopy(parsed->error, "IPv6 HTTPS hosts are not supported in bare-metal Navigator smoke yet", sizeof(parsed->error));
        return false;
    }
    const char* p = hostStart;
    while (*p && *p != ':' && *p != '/' && *p != '?' && (p - hostStart) < (int)sizeof(parsed->host) - 1) ++p;
    if (*p && *p != ':' && *p != '/' && *p != '?') {
        strcopy(parsed->error, "HTTPS hostname too long", sizeof(parsed->error));
        return false;
    }
    const char* hostEnd = p;
    if (hostEnd == hostStart) {
        strcopy(parsed->error, "Missing HTTPS host", sizeof(parsed->error));
        return false;
    }
    int hi = 0;
    for (const char* h = hostStart; h < hostEnd && hi < (int)sizeof(parsed->host) - 1; ++h) parsed->host[hi++] = *h;
    parsed->host[hi] = '\0';

    if (parse_numeric_ipv4_local(hostStart, hostEnd, &parsed->ip)) {
        parsed->hostIsNumeric = true;
    } else {
        if (nav_host_chars_are_numeric_ipv4ish(hostStart, hostEnd)) {
            strcopy(parsed->error, "Invalid numeric IPv4 HTTPS host", sizeof(parsed->error));
            return false;
        }
        if (!nav_hostname_is_valid(hostStart, hostEnd)) {
            strcopy(parsed->error, "Invalid HTTPS hostname", sizeof(parsed->error));
            return false;
        }
    }

    if (*p == ':') {
        ++p;
        uint32_t port = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            port = port * 10u + (uint32_t)(*p - '0');
            ++digits;
            ++p;
        }
        if (digits == 0 || port == 0 || port > 65535u) {
            strcopy(parsed->error, "Invalid HTTPS port", sizeof(parsed->error));
            return false;
        }
        parsed->port = (uint16_t)port;
    }

    if (*p == '/' || *p == '?') {
        int oi = 0;
        if (*p == '?') parsed->path[oi++] = '/';
        while (*p && oi < kKernelHttpUrlLen - 1) parsed->path[oi++] = *p++;
        parsed->path[oi] = '\0';
        if (*p) {
            strcopy(parsed->error, "HTTPS URL path exceeds the Navigator safety limit", sizeof(parsed->error));
            return false;
        }
    }
    return true;
}

static bool kernel_http_parse_response(KernelHttpResponse* response, int rawLen)
{
    response->statusCode = 0;
    response->reason[0] = '\0';
    response->contentType[0] = '\0';
    response->transferEncoding[0] = '\0';
    response->responseFraming[0] = '\0';
    response->contentEncoding[0] = '\0';
    response->contentLength = 0;
    response->contentLengthPresent = false;
    response->truncatedResponse = false;
    response->location[0] = '\0';
    response->bodyBytes = 0;

    int headerEnd = -1;
    int bodyStart = -1;
    for (int i = 0; i + 3 < rawLen && i < kKernelHttpHeaderLimit; ++i) {
        if (s_kernelHttpRaw[i] == '\r' && s_kernelHttpRaw[i + 1] == '\n' &&
            s_kernelHttpRaw[i + 2] == '\r' && s_kernelHttpRaw[i + 3] == '\n') {
            headerEnd = i;
            bodyStart = i + 4;
            break;
        }
    }
    if (headerEnd < 0) {
        for (int i = 0; i + 1 < rawLen && i < kKernelHttpHeaderLimit; ++i) {
            if (s_kernelHttpRaw[i] == '\n' && s_kernelHttpRaw[i + 1] == '\n') {
                headerEnd = i;
                bodyStart = i + 2;
                break;
            }
        }
    }
    if (headerEnd < 0) {
        response->headerCapHit = rawLen >= kKernelHttpHeaderLimit;
        strcopy(response->error, rawLen >= kKernelHttpHeaderLimit ? "HTTP header too large" : "Malformed HTTP response", sizeof(response->error));
        return false;
    }

    const char* line = s_kernelHttpRaw;
    const char* lineEnd = line;
    while (lineEnd < s_kernelHttpRaw + headerEnd && *lineEnd != '\r' && *lineEnd != '\n') ++lineEnd;
    if (!nav_starts_with(line, "HTTP/")) {
        strcopy(response->error, "Malformed HTTP status line", sizeof(response->error));
        return false;
    }
    const char* sp = line;
    while (sp < lineEnd && *sp != ' ') ++sp;
    while (sp < lineEnd && *sp == ' ') ++sp;
    int code = 0;
    for (int i = 0; i < 3 && sp + i < lineEnd; ++i) {
        if (sp[i] < '0' || sp[i] > '9') break;
        code = code * 10 + (sp[i] - '0');
    }
    response->statusCode = code;
    const char* reasonStart = sp + 3;
    while (reasonStart < lineEnd && *reasonStart == ' ') ++reasonStart;
    int ri = 0;
    for (const char* r = reasonStart; r < lineEnd && ri < (int)sizeof(response->reason) - 1; ++r) response->reason[ri++] = *r;
    response->reason[ri] = '\0';

    const char* h = lineEnd;
    while (h < s_kernelHttpRaw + headerEnd) {
        while (h < s_kernelHttpRaw + headerEnd && (*h == '\r' || *h == '\n')) ++h;
        const char* e = h;
        while (e < s_kernelHttpRaw + headerEnd && *e != '\r' && *e != '\n') ++e;
        const char* colon = h;
        while (colon < e && *colon != ':') ++colon;
        if (colon < e) {
            char name[40];
            gxos::web::httpSharedCopyTrimmed(h, colon, name, sizeof(name), true);
            const char* valueStart = colon + 1;
            if (gxos::web::httpSharedEqualsInsensitive(name, "content-type")) gxos::web::httpSharedNormalizeContentType(valueStart, e, response->contentType, sizeof(response->contentType));
            else if (gxos::web::httpSharedEqualsInsensitive(name, "transfer-encoding")) gxos::web::httpSharedCopyTrimmed(valueStart, e, response->transferEncoding, sizeof(response->transferEncoding), true);
            else if (gxos::web::httpSharedEqualsInsensitive(name, "content-encoding")) gxos::web::httpSharedCopyTrimmed(valueStart, e, response->contentEncoding, sizeof(response->contentEncoding), true);
            else if (gxos::web::httpSharedEqualsInsensitive(name, "content-length")) {
                int contentLength = 0;
                if (!gxos::web::httpSharedParseDecimalSize(valueStart, e, &contentLength)) {
                    strcopy(response->error, "Malformed HTTP Content-Length", sizeof(response->error));
                    return false;
                }
                response->contentLengthPresent = true;
                response->contentLength = contentLength;
                if (contentLength > kKernelHttpBodyLimit) {
                    response->bodyCapHit = true;
                    strcopy(response->error, "HTTP body too large", sizeof(response->error));
                    return false;
                }
            }
            else if (gxos::web::httpSharedEqualsInsensitive(name, "location")) {
                while (valueStart < e && (*valueStart == ' ' || *valueStart == '\t')) ++valueStart;
                int li = 0;
                for (const char* v = valueStart; v < e && li < kKernelHttpUrlLen - 1; ++v) response->location[li++] = *v;
                response->location[li] = '\0';
            }
        }
        h = e;
    }

    int encodedBodyBytes = rawLen - bodyStart;
    if (encodedBodyBytes < 0) encodedBodyBytes = 0;

    if (response->transferEncoding[0] &&
        gxos::web::httpSharedHeaderHasToken(response->transferEncoding, "chunked")) {
        strcopy(response->responseFraming, "chunked", sizeof(response->responseFraming));
    } else if (response->contentLengthPresent) {
        strcopy(response->responseFraming, "content-length", sizeof(response->responseFraming));
    } else {
        strcopy(response->responseFraming, "connection-close", sizeof(response->responseFraming));
    }

    if (gxos::web::httpSharedIsRedirectStatus(response->statusCode)) {
        int copyBytes = encodedBodyBytes;
        if (copyBytes > kKernelHttpBodyLimit) copyBytes = kKernelHttpBodyLimit;
        for (int i = 0; i < copyBytes; ++i) response->body[i] = s_kernelHttpRaw[bodyStart + i];
        response->body[copyBytes] = '\0';
        response->bodyBytes = copyBytes;
        response->ok = true;
        return true;
    }

    if (response->transferEncoding[0] && !gxos::web::httpSharedHeaderHasToken(response->transferEncoding, "identity")) {
        if (gxos::web::httpSharedHeaderHasToken(response->transferEncoding, "chunked")) {
            int decodedBytes = 0;
            char chunkError[128];
            if (!gxos::web::httpSharedDecodeChunkedBody(s_kernelHttpRaw + bodyStart, encodedBodyBytes,
                    response->body, sizeof(response->body), &decodedBytes, chunkError, sizeof(chunkError))) {
                response->bodyCapHit = nav_starts_with(chunkError, "HTTP body exceeded");
                strcopy(response->error, chunkError[0] ? chunkError : "Malformed chunked response", sizeof(response->error));
                return false;
            }
            response->bodyBytes = decodedBytes;
        } else {
            strcopy(response->unsupportedReason, "Unsupported Transfer-Encoding", sizeof(response->unsupportedReason));
            strcopy(response->error, "Unsupported transfer encoding", sizeof(response->error));
            return false;
        }
    } else {
        if (encodedBodyBytes > kKernelHttpBodyLimit) {
            response->bodyCapHit = true;
            strcopy(response->error, "HTTP body too large", sizeof(response->error));
            return false;
        }
        if (response->contentLengthPresent && encodedBodyBytes < response->contentLength) {
            response->truncatedResponse = true;
            strcopy(response->error, "Truncated HTTP response", sizeof(response->error));
            return false;
        }
        int bodyBytes = response->contentLengthPresent ? response->contentLength : encodedBodyBytes;
        if (bodyBytes > encodedBodyBytes) bodyBytes = encodedBodyBytes;
        for (int i = 0; i < bodyBytes; ++i) response->body[i] = s_kernelHttpRaw[bodyStart + i];
        response->body[bodyBytes] = '\0';
        response->bodyBytes = bodyBytes;
    }

    if (response->contentEncoding[0] && !gxos::web::httpSharedHeaderHasToken(response->contentEncoding, "identity")) {
        strcopy(response->unsupportedReason, "Unsupported Content-Encoding", sizeof(response->unsupportedReason));
        strcopy(response->error, "Unsupported content encoding", sizeof(response->error));
        return false;
    }

    response->ok = true;
    return true;
}

static const char* kernel_dns_error_name(kernel::dns::Status status)
{
    switch (status) {
    case kernel::dns::DNS_OK: return "ok";
    case kernel::dns::DNS_ERR_INVALID: return "DNS invalid hostname";
    case kernel::dns::DNS_ERR_TOOLONG: return "DNS hostname too long";
    case kernel::dns::DNS_ERR_NXDOMAIN: return "DNS name not found";
    case kernel::dns::DNS_ERR_SERVFAIL: return "DNS server failure";
    case kernel::dns::DNS_ERR_TIMEOUT: return "DNS timeout";
    case kernel::dns::DNS_ERR_NETWORK: return "DNS network unavailable";
    case kernel::dns::DNS_ERR_FORMAT: return "Malformed DNS response";
    case kernel::dns::DNS_ERR_REFUSED: return "DNS query refused";
    case kernel::dns::DNS_ERR_NOTFOUND: return "DNS response had no A record";
    case kernel::dns::DNS_ERR_NOCACHE: return "DNS cache miss";
    default: return "DNS lookup failed";
    }
}

static bool kernel_http_resolve_host(KernelHttpUrl* parsed, KernelHttpResponse* response)
{
    if (!parsed || !response) return false;
    response->dnsUsed = false;
    response->dnsHost[0] = '\0';
    response->dnsResolvedIp[0] = '\0';
    response->dnsError[0] = '\0';
    s_kernelLastDnsUsed = false;
    s_kernelLastDnsHost[0] = '\0';
    s_kernelLastDnsResolvedIp[0] = '\0';
    s_kernelLastDnsError[0] = '\0';
    if (parsed->hostIsNumeric) return true;

    response->dnsUsed = true;
    s_kernelLastDnsUsed = true;
    strcopy(response->dnsHost, parsed->host, sizeof(response->dnsHost));
    strcopy(s_kernelLastDnsHost, parsed->host, sizeof(s_kernelLastDnsHost));

#ifdef GXOS_NAVIGATOR_HTTP_SMOKE_ACTIVE
    if (streq_local(parsed->host, kNavigatorControlledHttpsHost) ||
        nav_ends_with(parsed->host, ".guidexos.test")) {
        parsed->ip = kernel::ipv4::make_ip(10, 0, 2, 2);
        kernel::ipv4::ip_to_string(parsed->ip, response->dnsResolvedIp);
        strcopy(s_kernelLastDnsResolvedIp, response->dnsResolvedIp, sizeof(s_kernelLastDnsResolvedIp));
        return true;
    }
#endif

    if (kernel::dns::get_server() == 0) {
        strcopy(response->dnsError, "DNS unavailable: no DNS server configured", sizeof(response->dnsError));
        strcopy(s_kernelLastDnsError, response->dnsError, sizeof(s_kernelLastDnsError));
        strcopy(response->error, response->dnsError, sizeof(response->error));
        return false;
    }

    uint32_t resolvedIp = 0;
    kernel::dns::Status dnsStatus = kernel::dns::resolve(parsed->host, &resolvedIp);
    if (dnsStatus != kernel::dns::DNS_OK || resolvedIp == 0) {
        strcopy(response->dnsError, kernel_dns_error_name(dnsStatus), sizeof(response->dnsError));
        strcopy(s_kernelLastDnsError, response->dnsError, sizeof(s_kernelLastDnsError));
        strcopy(response->error, response->dnsError, sizeof(response->error));
        return false;
    }
    parsed->ip = resolvedIp;
    kernel::ipv4::ip_to_string(parsed->ip, response->dnsResolvedIp);
    strcopy(s_kernelLastDnsResolvedIp, response->dnsResolvedIp, sizeof(s_kernelLastDnsResolvedIp));
    return true;
}
static bool kernel_http_build_request(const char* method, const KernelHttpUrl& parsed,
                                      const char* contentType, int bodyBytes,
                                      char* request, int requestSize, int* requestBytesOut)
{
    if (requestBytesOut) *requestBytesOut = 0;
    if (!request || requestSize <= 0) return false;
    const bool isPost = method && gxos::web::httpSharedEqualsInsensitive(method, "post");
    const bool isHttps = parsed.httpsScheme;
    int q = 0;
#define APPEND_REQ(svalue) do { const char* _s = (svalue); while (_s && *_s && q < requestSize - 1) request[q++] = *_s++; } while (0)
    APPEND_REQ(isPost ? "POST " : "GET ");
    APPEND_REQ(parsed.path);
    APPEND_REQ(" HTTP/1.1\r\nHost: ");
    APPEND_REQ(parsed.host);
    if ((!isHttps && parsed.port != 80) || (isHttps && parsed.port != 443)) {
        char portText[12];
        nav_int_to_text(parsed.port, portText, sizeof(portText));
        APPEND_REQ(":");
        APPEND_REQ(portText);
    }
    APPEND_REQ("\r\nUser-Agent: guideXOS-Navigator/0.2\r\nAccept: text/html, text/plain, image/png, */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n");
    if (isPost) {
        char bodyLengthText[16];
        nav_int_to_text(bodyBytes, bodyLengthText, sizeof(bodyLengthText));
        APPEND_REQ("Content-Type: ");
        APPEND_REQ(contentType && contentType[0] ? contentType : "application/x-www-form-urlencoded");
        APPEND_REQ("\r\nContent-Length: ");
        APPEND_REQ(bodyLengthText);
        APPEND_REQ("\r\n");
    }
    APPEND_REQ("\r\n");
#undef APPEND_REQ
    request[q] = '\0';
    if (requestBytesOut) *requestBytesOut = q;
    return true;
}

static bool kernel_http_read_response(gxos::web::HttpByteStream* stream, KernelHttpResponse* response)
{
    if (!stream || !response) return false;
    int rawLen = 0;
    int framedBodyStart = -1;
    int expectedBodyBytes = 0;
    bool contentLengthKnown = false;
    bool chunkedResponse = false;
    uint32_t startTicks = (uint32_t)kernel::pit::ticks();
    uint32_t maxTicks = (uint32_t)(kKernelHttpReadTimeoutMs / 10 + 1);
    while (true) {
        kernel_http_poll_once();
        char chunk[512];
        int n = stream->read(stream->context, reinterpret_cast<uint8_t*>(chunk), sizeof(chunk));
        if (n > 0) {
            if (rawLen + n > kKernelHttpRawLimit) {
                bool sawHeaderEnd = false;
                for (int i = 0; i + 3 < rawLen; ++i) {
                    if (s_kernelHttpRaw[i] == '\r' && s_kernelHttpRaw[i + 1] == '\n' &&
                        s_kernelHttpRaw[i + 2] == '\r' && s_kernelHttpRaw[i + 3] == '\n') {
                        sawHeaderEnd = true;
                        break;
                    }
                    if (s_kernelHttpRaw[i] == '\n' && s_kernelHttpRaw[i + 1] == '\n') {
                        sawHeaderEnd = true;
                        break;
                    }
                }
                response->headerCapHit = !sawHeaderEnd && rawLen >= kKernelHttpHeaderLimit;
                response->bodyCapHit = !response->headerCapHit;
                strcopy(response->error,
                    response->headerCapHit ? "HTTP header too large" : "HTTP body too large",
                    sizeof(response->error));
                return false;
            }
            for (int i = 0; i < n; ++i) s_kernelHttpRaw[rawLen++] = chunk[i];
            if (framedBodyStart < 0) {
                for (int i = 0; i + 3 < rawLen; ++i) {
                    if (s_kernelHttpRaw[i] == '\r' && s_kernelHttpRaw[i + 1] == '\n' &&
                        s_kernelHttpRaw[i + 2] == '\r' && s_kernelHttpRaw[i + 3] == '\n') {
                        framedBodyStart = i + 4;
                        break;
                    }
                    if (i + 1 < rawLen && s_kernelHttpRaw[i] == '\n' && s_kernelHttpRaw[i + 1] == '\n') {
                        framedBodyStart = i + 2;
                        break;
                    }
                }
            }
            if (framedBodyStart >= 0 && !contentLengthKnown && !chunkedResponse) {
                const char* h = s_kernelHttpRaw;
                const char* headerEnd = s_kernelHttpRaw + framedBodyStart;
                while (h < headerEnd) {
                    const char* e = h;
                    while (e < headerEnd && *e != '\r' && *e != '\n') ++e;
                    const char* colon = h;
                    while (colon < e && *colon != ':') ++colon;
                    if (colon < e) {
                        char name[40];
                        gxos::web::httpSharedCopyTrimmed(h, colon, name, sizeof(name), true);
                        if (gxos::web::httpSharedEqualsInsensitive(name, "transfer-encoding")) {
                            char transferValue[64];
                            gxos::web::httpSharedCopyTrimmed(colon + 1, e, transferValue, sizeof(transferValue), true);
                            if (gxos::web::httpSharedHeaderHasToken(transferValue, "chunked")) chunkedResponse = true;
                        } else if (gxos::web::httpSharedEqualsInsensitive(name, "content-length")) {
                            int parsedLength = 0;
                            if (gxos::web::httpSharedParseDecimalSize(colon + 1, e, &parsedLength)) {
                                if (parsedLength > kKernelHttpBodyLimit) {
                                    response->bodyCapHit = true;
                                    strcopy(response->error, "HTTP body too large", sizeof(response->error));
                                    return false;
                                }
                                expectedBodyBytes = parsedLength;
                                contentLengthKnown = true;
                            }
                        }
                    }
                    h = e;
                    while (h < headerEnd && (*h == '\r' || *h == '\n')) ++h;
                }
            }
            startTicks = (uint32_t)kernel::pit::ticks();
            if (framedBodyStart >= 0 && contentLengthKnown &&
                rawLen >= framedBodyStart + expectedBodyBytes) break;
            continue;
        }
        if (n == 0) break;
        if (n != kernel::tcp::TCP_ERR_WOULDBLOCK) {
            if (rawLen > 0) {
                s_kernelHttpRaw[rawLen] = '\0';
                const int savedBodyBytes = response->bodyBytes;
                const bool parsedPartial = kernel_http_parse_response(response, rawLen);
                const bool contentFailure =
                    response->headerCapHit ||
                    response->bodyCapHit ||
                    response->unsupportedReason[0] ||
                    streq_local(response->error, "Malformed HTTP response") ||
                    streq_local(response->error, "Malformed HTTP status line") ||
                    streq_local(response->error, "Malformed chunked response") ||
                    streq_local(response->error, "Truncated HTTP response");
                if (!parsedPartial && contentFailure) {
                    return false;
                }
                response->bodyBytes = savedBodyBytes;
            }
            kernel_http_set_stream_error(response, kernel_http_error_name(n),
                gxos::web::HttpByteStreamTlsStatus::TlsReadFailed);
            return false;
        }
        if (((uint32_t)kernel::pit::ticks() - startTicks) > maxTicks) {
            kernel_http_set_stream_error(response,
                rawLen > 0 ? "HTTP read timeout after partial response" : "HTTP read timeout",
                gxos::web::HttpByteStreamTlsStatus::TlsReadFailed);
            return false;
        }
    }
    s_kernelHttpRaw[rawLen] = '\0';
    return kernel_http_parse_response(response, rawLen);
}

struct KernelHttpActiveStream {
    gxos::web::HttpByteStream stream;
    KernelTcpHttpByteStreamContext tcpContext;
};

static bool kernel_http_error_contains_too_large(const char* error)
{
    if (!error || !error[0]) return false;
    for (const char* p = error; *p; ++p) {
        if (p[0] == 't' && p[1] == 'o' && p[2] == 'o' && p[3] == ' ' &&
            p[4] == 'l' && p[5] == 'a' && p[6] == 'r' && p[7] == 'g' && p[8] == 'e') {
            return true;
        }
    }
    return false;
}

static void kernel_http_mark_tls_failure(KernelHttpResponse* response,
                                         gxos::web::HttpByteStreamTlsStatus status,
                                         const char* error,
                                         int transportError = 0)
{
    if (!response) return;
    response->tlsResult.transportStatus = status;
    response->tlsResult.transportError = transportError;
    if (error && error[0]) {
        strcopy(response->tlsResult.error, error, sizeof(response->tlsResult.error));
        strcopy(response->error, error, sizeof(response->error));
    }
    kernel_http_sync_tls_status(response);
}

static void kernel_http_finalize_tls_status(KernelHttpResponse* response, bool parsedOk)
{
    if (!response || !kernel_http_transport_uses_tls(response->transportSelection)) {
        return;
    }
    response->tlsUsed = response->tlsResult.attempted || response->tlsUsed;
    response->tlsResult.parserAcceptedResponse = parsedOk;
    const bool contentFailure =
        response->headerCapHit ||
        response->bodyCapHit ||
        response->unsupportedReason[0] ||
        streq_local(response->error, "Malformed HTTP response") ||
        streq_local(response->error, "Malformed HTTP status line") ||
        streq_local(response->error, "Malformed chunked response") ||
        streq_local(response->error, "Truncated HTTP response");
    if (!parsedOk) {
        if (!response->tlsResult.error[0] && response->error[0]) {
            strcopy(response->tlsResult.error, response->error, sizeof(response->tlsResult.error));
        }
        if (response->tlsResult.handshakeSuccess && contentFailure) {
            response->tlsSucceededBeforeContentFailure = true;
            if (response->bodyCapHit || kernel_http_error_contains_too_large(response->error)) {
                response->tlsResult.transportStatus = gxos::web::HttpByteStreamTlsStatus::ResponseTooLarge;
            } else if (response->tlsResult.transportStatus == gxos::web::HttpByteStreamTlsStatus::NotStarted) {
                response->tlsResult.transportStatus = gxos::web::HttpByteStreamTlsStatus::Success;
            }
        } else if (response->tlsResult.transportStatus == gxos::web::HttpByteStreamTlsStatus::Success ||
            response->tlsResult.transportStatus == gxos::web::HttpByteStreamTlsStatus::NotStarted) {
            response->tlsResult.transportStatus = kernel_http_error_contains_too_large(response->error)
                ? gxos::web::HttpByteStreamTlsStatus::ResponseTooLarge
                : gxos::web::HttpByteStreamTlsStatus::TlsReadFailed;
        }
    } else if (response->tlsResult.transportStatus == gxos::web::HttpByteStreamTlsStatus::NotStarted) {
        response->tlsResult.transportStatus = gxos::web::HttpByteStreamTlsStatus::Success;
    }
    kernel_http_sync_tls_status(response);
}

static bool kernel_http_open_stream(KernelHttpUrl* parsed,
                                    const gxos::web::HttpTransportPolicyDecision& policy,
                                    const char* sniHostnameOverride,
                                    KernelHttpResponse* response,
                                    KernelHttpActiveStream* activeStream)
{
    if (!parsed || !response || !activeStream) return false;
    activeStream->stream = gxos::web::HttpByteStream{};
    activeStream->tcpContext.socket = -1;
    activeStream->tcpContext.abortUsedFlag = &response->tcpAbortUsed;
    response->tcpAbortUsed = false;

    if (!kernel_http_resolve_host(parsed, response)) {
        return false;
    }

    int sock = kernel::tcp::tcp_socket();
    if (sock < 0) {
        if (kernel_http_transport_uses_tls(policy.selection)) {
            kernel_http_mark_tls_failure(response, gxos::web::HttpByteStreamTlsStatus::TcpConnectFailed,
                "Could not create TCP socket");
        } else {
            strcopy(response->error, "Could not create TCP socket", sizeof(response->error));
        }
        return false;
    }

    if (kernel_http_transport_uses_tls(policy.selection)) {
        ++s_kernelHttpTlsConnectAttempts;
    } else {
        ++s_kernelHttpPlainTcpConnectAttempts;
    }

    const int rc = kernel::tcp::tcp_connect(sock, parsed->ip, parsed->port);
    if (rc < 0) {
        if (kernel_http_transport_uses_tls(policy.selection)) {
            kernel_http_mark_tls_failure(response, gxos::web::HttpByteStreamTlsStatus::TcpConnectFailed,
                kernel_http_error_name(rc), rc);
        } else {
            strcopy(response->error, kernel_http_error_name(rc), sizeof(response->error));
        }
        kernel::tcp::tcp_close(sock);
        return false;
    }
    if (!kernel_tcp_wait_connected(sock, response->error, sizeof(response->error))) {
        if (kernel_http_transport_uses_tls(policy.selection)) {
            kernel_http_mark_tls_failure(response, gxos::web::HttpByteStreamTlsStatus::TcpConnectFailed,
                response->error[0] ? response->error : "TCP connect failed");
        }
        kernel::tcp::tcp_close(sock);
        return false;
    }

    if (policy.selection == gxos::web::HttpByteStreamTransportSelection::PlainTcpHttp) {
        activeStream->stream = make_kernel_tcp_http_byte_stream(&activeStream->tcpContext, sock);
        return true;
    }
    if (kernel_http_transport_uses_tls(policy.selection)) {
        const char* sniHost = (sniHostnameOverride && sniHostnameOverride[0]) ? sniHostnameOverride : parsed->host;
        gxos::GxosTlsByteStream tcpTlsStream = make_kernel_tcp_tls_byte_stream(&activeStream->tcpContext, sock);
        if (!gxos::gxos_tls_open_http_byte_stream(sniHost, tcpTlsStream, &activeStream->stream, &response->tlsResult)) {
            response->tlsUsed = response->tlsResult.attempted;
            kernel_http_sync_tls_status(response);
            strcopy(response->error,
                response->tlsResult.error[0] ? response->tlsResult.error : "Validated HTTPS request failed",
                sizeof(response->error));
            return false;
        }
        response->tlsUsed = response->tlsResult.attempted;
        kernel_http_sync_tls_status(response);
        return true;
    }

    kernel::tcp::tcp_close(sock);
    strcopy(response->error, "HTTP transport policy could not open a stream", sizeof(response->error));
    return false;
}

static KernelHttpResponse* kernel_http_request_once_internal(const char* url, const char* method,
                                                             const char* body, int bodyBytes,
                                                             const char* contentType,
                                                             const char* sniHostnameOverride)
{
    KernelHttpResponse* response = &s_kernelHttpResponse;
    kernel_http_reset_response(response);
    strcopy(response->requestedUrl, url ? url : "", sizeof(response->requestedUrl));
    strcopy(response->finalUrl, url ? url : "", sizeof(response->finalUrl));
    s_kernelLastDnsUsed = false;
    s_kernelLastDnsHost[0] = '\0';
    s_kernelLastDnsResolvedIp[0] = '\0';
    s_kernelLastDnsError[0] = '\0';

    if (!kernel::nic::is_active() || !kernel::ipv4::is_configured()) {
        strcopy(response->error, "Network unavailable", sizeof(response->error));
        return response;
    }
    const bool isPost = method && gxos::web::httpSharedEqualsInsensitive(method, "post");
    const bool isGet = !method || !method[0] || gxos::web::httpSharedEqualsInsensitive(method, "get");
    if (!isGet && !isPost) {
        strcopy(response->error, "Unsupported HTTP method", sizeof(response->error));
        return response;
    }
    if (bodyBytes < 0 || (bodyBytes > 0 && !body)) {
        strcopy(response->error, "Invalid HTTP request body", sizeof(response->error));
        return response;
    }
    if (isPost && bodyBytes > kKernelHttpPostBodyLimit) {
        strcopy(response->error, "Forms-lite POST body too large", sizeof(response->error));
        return response;
    }

    KernelHttpUrl parsed;
    gxos::web::HttpTransportPolicyDecision policy = kernel_http_plain_transport_policy();
    char policyReason[128];
    policyReason[0] = '\0';
    const bool isHttps = nav_starts_with(url, "https://");
    if (isHttps) {
        strcopy(response->scheme, "https", sizeof(response->scheme));
        if (!parse_https_url_kernel(url, &parsed)) {
            strcopy(response->error, parsed.error, sizeof(response->error));
            return response;
        }
        policy = kernel_http_transport_policy_for_https(parsed, policyReason, sizeof(policyReason));
    } else {
        strcopy(response->scheme, "http", sizeof(response->scheme));
        if (!parse_http_url_kernel(url, &parsed)) {
            strcopy(response->error, parsed.error, sizeof(response->error));
            return response;
        }
        policy = kernel_http_plain_transport_policy();
        strcopy(policyReason, policy.reason ? policy.reason : "", sizeof(policyReason));
    }
    kernel_http_apply_transport_policy(response, policy, policyReason);
    if (kernel_http_transport_uses_tls(policy.selection)) {
        ++s_kernelHttpControlledLocalHttpsLoads;
    }
    if (!policy.tcpAttemptAllowed) {
        strcopy(response->error,
            response->transportPolicyReason[0] ? response->transportPolicyReason : "HTTPS/TLS unsupported",
            sizeof(response->error));
        return response;
    }

    KernelHttpActiveStream activeStream{};
    if (!kernel_http_open_stream(&parsed, policy, sniHostnameOverride, response, &activeStream)) {
        return response;
    }

    char request[768];
    int requestBytes = 0;
    if (!kernel_http_build_request(method, parsed, contentType, bodyBytes, request, sizeof(request), &requestBytes)) {
        strcopy(response->error, isHttps ? "Could not build HTTPS request" : "Could not build HTTP request",
            sizeof(response->error));
        activeStream.stream.close(activeStream.stream.context);
        kernel_http_finalize_tls_status(response, false);
        return response;
    }
    const bool sentOk =
        kernel_http_send_all(&activeStream.stream, request, requestBytes, response) &&
        (!isPost || kernel_http_send_all(&activeStream.stream, body, bodyBytes, response));
    if (!sentOk) {
        activeStream.stream.close(activeStream.stream.context);
        kernel_http_finalize_tls_status(response, false);
        return response;
    }

    const bool parsedOk = kernel_http_read_response(&activeStream.stream, response);
    activeStream.stream.close(activeStream.stream.context);
    kernel_http_finalize_tls_status(response, parsedOk);
    return response;
}

static KernelHttpResponse* kernel_https_request_once(const char* url, const char* method,
                                                     const char* body, int bodyBytes,
                                                     const char* contentType,
                                                     const char* sniHostnameOverride = nullptr)
{
    return kernel_http_request_once_internal(
        url, method, body, bodyBytes, contentType, sniHostnameOverride);
}

static KernelHttpResponse* kernel_http_request_once(const char* url, const char* method,
                                                    const char* body, int bodyBytes,
                                                    const char* contentType)
{
    return kernel_http_request_once_internal(url, method, body, bodyBytes, contentType, nullptr);
}

static bool kernel_http_should_retry_redirected_tls_open(const KernelHttpResponse* response,
                                                         const char* currentUrl,
                                                         const char* currentMethod,
                                                         int redirectCount,
                                                         int currentBodyBytes)
{
    if (!response || !currentUrl || !currentMethod) return false;
    if (redirectCount <= 0) return false;
    if (!nav_starts_with(currentUrl, "https://")) return false;
    if (!gxos::web::httpSharedEqualsInsensitive(currentMethod, "GET")) return false;
    if (currentBodyBytes != 0) return false;
    if (response->ok) return false;
    if (!kernel_http_transport_uses_tls(response->transportSelection)) return false;
    if (response->tlsStatus != gxos::web::HttpByteStreamTlsStatus::HandshakeFailed) return false;
    if (!response->tlsResult.attempted || !response->tlsResult.tcpConnected) return false;
    if (response->tlsResult.handshakeSuccess) return false;
    if (response->tlsResult.verifyFlags != 0) return false;
    if (response->tlsResult.requestBytesWritten != 0) return false;
    if (response->tlsResult.responseBytesRead != 0) return false;
    return true;
}

static void kernel_http_origin(const KernelHttpUrl& parsed, const char* scheme, char* out, int outSize)
{
    strcopy(out, scheme ? scheme : "http://", outSize);
    strappend(out, parsed.host, outSize);
    const bool https = scheme && streq_local(scheme, "https://");
    if ((!https && parsed.port != 80) || (https && parsed.port != 443)) {
        char portText[12];
        nav_int_to_text(parsed.port, portText, sizeof(portText));
        strappend(out, ":", outSize);
        strappend(out, portText, outSize);
    }
}

static bool kernel_http_resolve_redirect(const char* baseUrl, const char* location, char* out, int outSize, char* error, int errorSize)
{
    if (out && outSize > 0) out[0] = '\0';
    if (error && errorSize > 0) error[0] = '\0';
    if (!baseUrl || !location || !location[0]) {
        strcopy(error, "Redirect Location header is empty", errorSize);
        return false;
    }
    const bool baseIsHttps = nav_starts_with(baseUrl, "https://");
    if (nav_starts_with(location, "http://")) {
        if (baseIsHttps) {
            strcopy(error, "HTTPS downgrade redirect blocked", errorSize);
            return false;
        }
        KernelHttpUrl parsed;
        if (!parse_http_url_kernel(location, &parsed)) {
            strcopy(error, parsed.error, errorSize);
            return false;
        }
        strcopy(out, location, outSize);
        return true;
    }
    if (nav_starts_with(location, "https://")) {
        KernelHttpUrl parsed;
        if (!parse_https_url_kernel(location, &parsed)) {
            strcopy(error, parsed.error, errorSize);
            return false;
        }
        char policyReason[128];
        policyReason[0] = '\0';
        const gxos::web::HttpTransportPolicyDecision policy =
            kernel_http_transport_policy_for_https(parsed, policyReason, sizeof(policyReason));
        if (!policy.tcpAttemptAllowed) {
            strcopy(error, policyReason[0] ? policyReason : "Redirect Location uses unsupported HTTPS", errorSize);
            return false;
        }
        strcopy(out, location, outSize);
        return true;
    }

    KernelHttpUrl base;
    const char* baseScheme = "http://";
    if (baseIsHttps) {
        if (!parse_https_url_kernel(baseUrl, &base)) {
            strcopy(error, base.error, errorSize);
            return false;
        }
        baseScheme = "https://";
    } else if (!parse_http_url_kernel(baseUrl, &base)) {
        strcopy(error, base.error, errorSize);
        return false;
    }
    char origin[kKernelHttpUrlLen];
    kernel_http_origin(base, baseScheme, origin, sizeof(origin));
    if (location[0] == '/') {
        strcopy(out, origin, outSize);
        strappend(out, location, outSize);
    } else if (location[0] == '#') {
        strcopy(out, baseUrl, outSize);
    } else {
        strcopy(out, origin, outSize);
        const char* lastSlash = nullptr;
        for (const char* p = base.path; *p; ++p) {
            if (*p == '/') lastSlash = p;
            if (*p == '?') break;
        }
        if (lastSlash) {
            int prefixLen = (int)(lastSlash - base.path + 1);
            int len = strlen_local(out);
            for (int i = 0; i < prefixLen && len < outSize - 1; ++i) out[len++] = base.path[i];
            out[len] = '\0';
        } else {
            strappend(out, "/", outSize);
        }
        strappend(out, location, outSize);
    }

    KernelHttpUrl parsedOut;
    if (nav_starts_with(out, "https://")) {
        if (!parse_https_url_kernel(out, &parsedOut)) {
            strcopy(error, parsedOut.error, errorSize);
            return false;
        }
        char policyReason[128];
        policyReason[0] = '\0';
        const gxos::web::HttpTransportPolicyDecision policy =
            kernel_http_transport_policy_for_https(parsedOut, policyReason, sizeof(policyReason));
        if (!policy.tcpAttemptAllowed) {
            strcopy(error, policyReason[0] ? policyReason : "Redirect Location uses unsupported HTTPS", errorSize);
            return false;
        }
    } else if (!parse_http_url_kernel(out, &parsedOut)) {
        strcopy(error, parsedOut.error, errorSize);
        return false;
    }
    return true;
}

static KernelHttpResponse* kernel_http_request(const char* url, const char* method,
                                               const char* body, int bodyBytes,
                                               const char* contentType)
{
    char current[kKernelHttpUrlLen];
    char currentMethod[8];
    const char* currentBody = body;
    int currentBodyBytes = bodyBytes;
    int totalTlsRetryCount = 0;
    int lastTlsBytesWrittenBeforeRetry = 0;
    int lastRetryHopIndex = 0;
    char lastTlsRetryReason[96];
    char lastRetryHopUrl[kKernelHttpUrlLen];
    strcopy(current, url ? url : "", sizeof(current));
    strcopy(currentMethod, method && gxos::web::httpSharedEqualsInsensitive(method, "post") ? "POST" : "GET", sizeof(currentMethod));
    lastTlsRetryReason[0] = '\0';
    lastRetryHopUrl[0] = '\0';
    for (int redirectCount = 0; redirectCount <= gxos::web::kHttpSharedMaxRedirects; ++redirectCount) {
        KernelHttpResponse* response = nullptr;
        for (int attempt = 0; attempt < 2; ++attempt) {
            response = kernel_http_request_once(current, currentMethod, currentBody, currentBodyBytes, contentType);
            response->redirectCount = redirectCount;
            strcopy(response->requestedUrl, url ? url : "", sizeof(response->requestedUrl));
            strcopy(response->finalUrl, current, sizeof(response->finalUrl));
            response->redirectHopIndex = redirectCount;
            strcopy(response->redirectHopUrl, current, sizeof(response->redirectHopUrl));
            if (attempt == 0 &&
                kernel_http_should_retry_redirected_tls_open(
                    response, current, currentMethod, redirectCount, currentBodyBytes)) {
                ++totalTlsRetryCount;
                lastTlsBytesWrittenBeforeRetry = (int)response->tlsResult.requestBytesWritten;
                lastRetryHopIndex = redirectCount;
                strcopy(lastRetryHopUrl, current, sizeof(lastRetryHopUrl));
                strcopy(lastTlsRetryReason,
                    "redirected-https-prewrite-open-failure",
                    sizeof(lastTlsRetryReason));
                kernel_http_poll_once();
                continue;
            }
            break;
        }
        if (response) {
            response->tlsRetryCount = totalTlsRetryCount;
            response->redirectedHttpsRetryUsed = totalTlsRetryCount > 0;
            response->tlsBytesWrittenBeforeRetry = lastTlsBytesWrittenBeforeRetry;
            strcopy(response->tlsRetryReason, lastTlsRetryReason, sizeof(response->tlsRetryReason));
            if (totalTlsRetryCount > 0) {
                response->redirectHopIndex = lastRetryHopIndex;
                strcopy(response->redirectHopUrl, lastRetryHopUrl, sizeof(response->redirectHopUrl));
            }
        }
        if (!response->ok) return response;
        if (!gxos::web::httpSharedIsRedirectStatus(response->statusCode)) return response;
        if (!response->location[0]) return response;
        if (redirectCount == gxos::web::kHttpSharedMaxRedirects) {
            response->ok = false;
            strcopy(response->error, "HTTP redirect limit exceeded", sizeof(response->error));
            return response;
        }
        char next[kKernelHttpUrlLen];
        char redirectError[128];
        if (!kernel_http_resolve_redirect(current, response->location, next, sizeof(next), redirectError, sizeof(redirectError))) {
            response->ok = false;
            const bool blockedHttpsTarget =
                (next[0] && nav_starts_with(next, "https://")) ||
                nav_starts_with(response->location, "https://");
            if (next[0]) {
                strcopy(response->finalUrl, next, sizeof(response->finalUrl));
                response->redirectCount = redirectCount + 1;
            } else if (nav_starts_with(response->location, "https://") || nav_starts_with(response->location, "http://")) {
                strcopy(response->finalUrl, response->location, sizeof(response->finalUrl));
                response->redirectCount = redirectCount + 1;
            }
            kernel_http_apply_redirect_failure_policy(response, next, redirectError);
            if (blockedHttpsTarget) {
                strcopy(response->error,
                    redirectError[0]
                        ? redirectError
                        : "HTTPS/TLS unsupported redirect",
                    sizeof(response->error));
            } else {
                strcopy(response->error, redirectError[0] ? redirectError : "Invalid redirect Location", sizeof(response->error));
            }
            return response;
        }
        strcopy(current, next, sizeof(current));
        if (response->statusCode == 303) {
            strcopy(currentMethod, "GET", sizeof(currentMethod));
            currentBody = nullptr;
            currentBodyBytes = 0;
        }
    }
    KernelHttpResponse* response = &s_kernelHttpResponse;
    response->ok = false;
    response->statusCode = 0;
    response->bodyBytes = 0;
    response->redirectCount = gxos::web::kHttpSharedMaxRedirects;
    strcopy(response->requestedUrl, url ? url : "", sizeof(response->requestedUrl));
    strcopy(response->finalUrl, current, sizeof(response->finalUrl));
    strcopy(response->error, "HTTP redirect limit exceeded", sizeof(response->error));
    return response;
}

static KernelHttpResponse* kernel_http_fetch(const char* url)
{
    return kernel_http_request(url, "GET", nullptr, 0, nullptr);
}

static KernelHttpResponse* kernel_http_post(const char* url, const char* body, int bodyBytes,
                                            const char* contentType)
{
    return kernel_http_request(url, "POST", body, bodyBytes, contentType);
}

static bool kernel_tls_smoke_request_once(const char* url,
                                          const char* sniHostname,
                                          KernelHttpResponse* response,
                                          gxos::GxosTlsLocalHandshakeResult* tlsResult)
{
    if (!response || !tlsResult) return false;
    KernelHttpResponse* internal = kernel_https_request_once(url, "GET", nullptr, 0, nullptr, sniHostname);
    *response = *internal;
    *tlsResult = internal->tlsResult;
    if (!internal->ok && !tlsResult->error[0] && internal->error[0]) {
        strcopy(tlsResult->error, internal->error, sizeof(tlsResult->error));
    }
    return internal->ok;
}

static gxos::gui::ImageSafetyLimits nav_kernel_remote_png_limits()
{
    gxos::gui::ImageSafetyLimits limits{};
    limits.maxBytes = 256u * 1024u;
    limits.maxWidth = 2048u;
    limits.maxHeight = 2048u;
    limits.maxPixels = 2048u * 2048u;
    return limits;
}

static bool nav_url_path_ends_with_png(const char* url)
{
    if (!url) return false;
    char path[kKernelHttpUrlLen];
    int i = 0;
    while (url[i] && url[i] != '?' && url[i] != '#' && i < kKernelHttpUrlLen - 1) {
        path[i] = url[i];
        ++i;
    }
    path[i] = '\0';
    return endsWithIgnoreCaseLocal(path, ".png");
}

static const int kNavigatorUrlStorageBytes = 512;
static void nav_push_url(char stack[][kNavigatorUrlStorageBytes], int& count, const char* url);

void NavigatorApp::prepareImageResources()
{
    gxos::gui::ImageSafetyLimits localLimits = gxos::gui::DefaultImageSafetyLimits();
    gxos::gui::ImageSafetyLimits remoteLimits = nav_kernel_remote_png_limits();
    int remoteFetchCount = 0;
    for (int i = 0; i < m_blockCount; ++i) {
        if (m_blocks[i].kind != BLOCK_IMAGE) continue;
        m_blocks[i].imagePixels = nullptr;
        m_blocks[i].imageError[0] = '\0';

        if (nav_starts_with(m_blocks[i].url, "file://")) {
            char imagePath[MAX_URL_LEN];
            nav_image_file_path_from_url(m_blocks[i].url, imagePath, MAX_URL_LEN);
            gxos::gui::ImageBitmap bitmap = gxos::gui::ImageAdapter::LoadFromFile(imagePath, localLimits);
            m_blocks[i].imageStatus = (int)bitmap.status;
            m_blocks[i].naturalWidth = (int)bitmap.width;
            m_blocks[i].naturalHeight = (int)bitmap.height;
            m_blocks[i].imagePixels = bitmap.pixels;
            if (bitmap.status != gxos::gui::ImageLoadStatus::Ok) {
                strcopy(m_blocks[i].imageError, gxos::gui::ImageLoadStatusName(bitmap.status), sizeof(m_blocks[i].imageError));
            }
            continue;
        }

        if (!nav_starts_with(m_blocks[i].url, "http://") &&
            !nav_starts_with(m_blocks[i].url, "https://")) {
            m_blocks[i].imageStatus = (int)gxos::gui::ImageLoadStatus::UnsupportedFormat;
            strcopy(m_blocks[i].imageError, "Unsupported image URL scheme", sizeof(m_blocks[i].imageError));
            continue;
        }

        if (remoteFetchCount >= gxos::web::kHttpSharedMaxRemoteResources) {
            m_blocks[i].imageStatus = (int)gxos::gui::ImageLoadStatus::NotFound;
            strcopy(m_blocks[i].imageError, "Remote resource limit reached", sizeof(m_blocks[i].imageError));
            continue;
        }
        ++remoteFetchCount;
        KernelHttpResponse* response = kernel_http_fetch(m_blocks[i].url);
        if (!response->ok) {
            m_blocks[i].imageStatus = (int)gxos::gui::ImageLoadStatus::NotFound;
            strcopy(m_blocks[i].imageError, response->error[0] ? response->error : "Remote image fetch failed", sizeof(m_blocks[i].imageError));
            continue;
        }
        if (response->statusCode != 200) {
            m_blocks[i].imageStatus = (int)gxos::gui::ImageLoadStatus::NotFound;
            strcopy(m_blocks[i].imageError, "Remote image HTTP status was not 200", sizeof(m_blocks[i].imageError));
            continue;
        }
        const char* finalUrl = response->finalUrl[0] ? response->finalUrl : m_blocks[i].url;
        bool contentTypePng = gxos::web::httpSharedEqualsInsensitive(response->contentType, "image/png");
        bool urlLooksPng = nav_url_path_ends_with_png(finalUrl);
        if (!contentTypePng && !urlLooksPng) {
            m_blocks[i].imageStatus = (int)gxos::gui::ImageLoadStatus::UnsupportedFormat;
            strcopy(m_blocks[i].imageError, "Remote image is not image/png", sizeof(m_blocks[i].imageError));
            continue;
        }
        gxos::gui::ImageBitmap bitmap = gxos::gui::ImageAdapter::LoadFromBytes(
            reinterpret_cast<const uint8_t*>(response->body), (uint32_t)response->bodyBytes, remoteLimits);
        m_blocks[i].imageStatus = (int)bitmap.status;
        m_blocks[i].naturalWidth = (int)bitmap.width;
        m_blocks[i].naturalHeight = (int)bitmap.height;
        m_blocks[i].imagePixels = bitmap.pixels;
        if (bitmap.status != gxos::gui::ImageLoadStatus::Ok) {
            strcopy(m_blocks[i].imageError, gxos::gui::ImageLoadStatusName(bitmap.status), sizeof(m_blocks[i].imageError));
        }
    }
}
void NavigatorApp::loadHttpUrl(const char* url)
{
    clearPageDownloadMetadata();
    loadHttpResponse(url, kernel_http_fetch(url));
}

void NavigatorApp::loadHttpResponse(const char* url, KernelHttpResponse* response)
{
    const char* transportSource = response && response->scheme[0] ? response->scheme : "http";
    auto buildCompatibilityFailureDocument = [&](const char* title, const char* summary) {
        const char* finalUrl = response->finalUrl[0] ? response->finalUrl : url;
        const char* requestedUrl = response->requestedUrl[0] ? response->requestedUrl : url;
        strcopy(m_currentUrl, finalUrl, MAX_URL_LEN);
        strcopy(m_title, title, MAX_TITLE_LEN_NAV);
        m_blockCount = 0;
        addBlock(BLOCK_HEADING, title);
        addBlock(BLOCK_PARAGRAPH, summary);
        char line[MAX_BLOCK_TEXT];
        strcopy(line, "Requested URL: ", sizeof(line));
        strappend(line, requestedUrl, sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);
        strcopy(line, "Final URL: ", sizeof(line));
        strappend(line, finalUrl, sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);
        strcopy(line, "Content type: ", sizeof(line));
        strappend(line, response->contentType[0] ? response->contentType : "(none)", sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);
        strcopy(line, "Content encoding: ", sizeof(line));
        strappend(line, response->contentEncoding[0] ? response->contentEncoding : "(none)", sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);
        strcopy(line, "Redirect count: ", sizeof(line));
        char number[24];
        nav_int_to_text(response->redirectCount, number, sizeof(number));
        strappend(line, number, sizeof(line));
        addBlock(BLOCK_LIST_ITEM, line);
        if (response->statusCode > 0) {
            strcopy(line, "HTTP status: ", sizeof(line));
            nav_int_to_text(response->statusCode, number, sizeof(number));
            strappend(line, number, sizeof(line));
            if (response->reason[0]) {
                strappend(line, " ", sizeof(line));
                strappend(line, response->reason, sizeof(line));
            }
            addBlock(BLOCK_LIST_ITEM, line);
        }
        if (response->unsupportedReason[0]) {
            strcopy(line, "Unsupported reason: ", sizeof(line));
            strappend(line, response->unsupportedReason, sizeof(line));
            addBlock(BLOCK_LIST_ITEM, line);
        }
        if (response->headerCapHit) {
            addBlock(BLOCK_LIST_ITEM, "Header limit hit: 32768 bytes");
        }
        if (response->bodyCapHit) {
            addBlock(BLOCK_LIST_ITEM, "Body limit hit: 262144 bytes");
        }
        addBlock(BLOCK_LIST_ITEM,
            response->tlsSucceededBeforeContentFailure
                ? "TLS succeeded before content failure: yes"
                : "TLS succeeded before content failure: no");
        addBlock(BLOCK_LINK, "Page Info", "about:page-info");
        addBlock(BLOCK_LINK, "Go to about:navigator", "about:navigator");
    };

    if (!response->ok) {
        const char* finalUrl = response->finalUrl[0] ? response->finalUrl : url;
        const char* requestedUrl = response->requestedUrl[0] ? response->requestedUrl : url;
        const bool requestedHttps = nav_starts_with(requestedUrl, "https://");
        const bool finalHttps = nav_starts_with(finalUrl, "https://");
        if ((requestedHttps || finalHttps) &&
            nav_starts_with(response->error, "HTTPS/TLS unsupported")) {
            buildHttpsUnsupportedDocument(finalUrl, !requestedHttps || response->redirectCount > 0, response->error);
            rememberPageMetadata(requestedUrl, finalUrl, finalHttps ? "https" : transportSource, response->contentType,
                requestedHttps ? "HTTPS/TLS unsupported" : "HTTPS/TLS unsupported redirect",
                nullptr, 0, nullptr, nullptr, response->statusCode, response->reason,
                response->redirectCount, response);
            return;
        }
        if (streq_local(response->error, "Unsupported content encoding")) {
            if (!response->unsupportedReason[0]) {
                strcopy(response->unsupportedReason, "Unsupported Content-Encoding", sizeof(response->unsupportedReason));
            }
            buildUnsupportedContentEncodingDocument(finalUrl, response->contentEncoding);
            rememberPageMetadata(requestedUrl, finalUrl, finalHttps ? "https" : transportSource, response->contentType,
                "Unsupported content encoding", nullptr, 0, nullptr, nullptr, response->statusCode, response->reason,
                response->redirectCount, response);
            return;
        }
        if (streq_local(response->error, "Unsupported transfer encoding")) {
            if (!response->unsupportedReason[0]) {
                strcopy(response->unsupportedReason, "Unsupported Transfer-Encoding", sizeof(response->unsupportedReason));
            }
            buildCompatibilityFailureDocument("Unsupported Transfer Encoding",
                "TLS succeeded, but Navigator cannot display this transfer encoding yet.");
            rememberPageMetadata(requestedUrl, finalUrl, finalHttps ? "https" : transportSource, response->contentType,
                "Unsupported transfer encoding", nullptr, 0, nullptr, nullptr, response->statusCode, response->reason,
                response->redirectCount, response);
            return;
        }
        if (streq_local(response->error, "HTTP body too large") || streq_local(response->error, "HTTP response too large")) {
            buildCompatibilityFailureDocument("Response Too Large",
                "TLS may have succeeded, but Navigator stopped before rendering because the response body exceeded the configured safety limit.");
            rememberPageMetadata(requestedUrl, finalUrl, finalHttps ? "https" : transportSource, response->contentType,
                "HTTP body too large", nullptr, 0, nullptr, nullptr, response->statusCode, response->reason,
                response->redirectCount, response);
            return;
        }
        if (streq_local(response->error, "HTTP header too large")) {
            buildCompatibilityFailureDocument("Headers Too Large",
                "Navigator stopped before rendering because the response headers exceeded the configured safety limit.");
            rememberPageMetadata(requestedUrl, finalUrl, finalHttps ? "https" : transportSource, response->contentType,
                "HTTP header too large", nullptr, 0, nullptr, nullptr, response->statusCode, response->reason,
                response->redirectCount, response);
            return;
        }
        if (streq_local(response->error, "HTTPS downgrade redirect blocked")) {
            buildCompatibilityFailureDocument("Insecure Redirect Blocked",
                "Navigator blocked an HTTPS-to-HTTP redirect because it would continue navigation over an insecure connection.");
            rememberPageMetadata(requestedUrl, finalUrl, finalHttps ? "https" : transportSource, response->contentType,
                "HTTPS downgrade redirect blocked", nullptr, 0, nullptr, nullptr, response->statusCode, response->reason,
                response->redirectCount, response);
            return;
        }
        if (streq_local(response->error, "HTTP redirect limit exceeded")) {
            buildCompatibilityFailureDocument("Redirect Limit Exceeded",
                "Navigator stopped following redirects after hitting its safety limit.");
            rememberPageMetadata(requestedUrl, finalUrl, finalHttps ? "https" : transportSource, response->contentType,
                "HTTP redirect limit exceeded", nullptr, 0, nullptr, nullptr, response->statusCode, response->reason,
                response->redirectCount, response);
            return;
        }
        buildErrorDocument(finalUrl, response->error[0] ? response->error : "HTTP fetch failed.");
        rememberPageMetadata(requestedUrl, finalUrl, finalHttps ? "https" : transportSource, response->contentType,
            response->error, nullptr, 0, nullptr, nullptr, response->statusCode, response->reason,
            response->redirectCount, response);
        return;
    }

    if (response->statusCode == 200) {
        if (gxos::web::httpSharedEqualsInsensitive(response->contentType, "text/html")) {
            int docBytes = response->bodyBytes;
            if (docBytes > kKernelHttpBodyLimit) docBytes = kKernelHttpBodyLimit;
            for (int i = 0; i < docBytes; ++i) s_kernelHttpDocumentBody[i] = response->body[i];
            s_kernelHttpDocumentBody[docBytes] = '\0';
            parseHtmlDocument(response->finalUrl[0] ? response->finalUrl : url, s_kernelHttpDocumentBody,
                transportSource, response->contentType, response->statusCode, response->reason,
                response->requestedUrl[0] ? response->requestedUrl : url, response->redirectCount, response);
        } else if (!response->contentType[0] || gxos::web::httpSharedEqualsInsensitive(response->contentType, "text/plain")) {
            strcopy(m_currentUrl, response->finalUrl[0] ? response->finalUrl : url, MAX_URL_LEN);
            strcopy(m_title, response->finalUrl[0] ? response->finalUrl : url, MAX_TITLE_LEN_NAV);
            m_blockCount = 0;
            addBlock(BLOCK_PREFORMATTED, response->body);
            rememberPageMetadata(response->requestedUrl[0] ? response->requestedUrl : url,
                response->finalUrl[0] ? response->finalUrl : url, transportSource, response->contentType, "",
                response->body, response->bodyBytes, nullptr, nullptr, response->statusCode, response->reason,
                response->redirectCount, response);
        } else {
            DownloadRecord record{};
            strcopy(record.url, response->requestedUrl[0] ? response->requestedUrl : url, sizeof(record.url));
            strcopy(record.finalUrl, response->finalUrl[0] ? response->finalUrl : url, sizeof(record.finalUrl));
            strcopy(record.contentType, response->contentType[0] ? response->contentType : "application/octet-stream", sizeof(record.contentType));
            record.byteCount = response->bodyBytes;
            nav_make_safe_download_filename(record.finalUrl, record.suggestedFileName, sizeof(record.suggestedFileName));

            char writeError[128];
            char uniqueName[vfs::VFS_MAX_FILENAME];
            char uniquePath[MAX_URL_LEN];
            if (response->bodyBytes <= 0) {
                record.success = false;
                strcopy(record.error, "Response body was empty; Navigator did not create a download file.", sizeof(record.error));
                rememberDownload(record);
                buildDownloadResultDocument(record);
                rememberPageMetadata(record.url, record.finalUrl, transportSource, record.contentType,
                    "Download unavailable: no body", response->body, response->bodyBytes, nullptr, nullptr,
                    response->statusCode, response->reason, response->redirectCount, response);
                return;
            }
            if (!kernel_downloads_directory_ready(writeError, sizeof(writeError))) {
                record.success = false;
                strcopy(record.error, writeError[0] ? writeError : "Downloads directory is unavailable.", sizeof(record.error));
                rememberDownload(record);
                buildDownloadResultDocument(record);
                rememberPageMetadata(record.url, record.finalUrl, transportSource, record.contentType,
                    "Download unavailable: downloads directory not writable", response->body, response->bodyBytes,
                    nullptr, nullptr, response->statusCode, response->reason, response->redirectCount, response);
                return;
            }
            if (!kernel_make_unique_download_path(record.finalUrl, uniquePath, sizeof(uniquePath), uniqueName, sizeof(uniqueName), writeError, sizeof(writeError))) {
                record.success = false;
                strcopy(record.error, writeError[0] ? writeError : "Navigator could not allocate a safe non-overwriting filename.", sizeof(record.error));
                rememberDownload(record);
                buildDownloadResultDocument(record);
                rememberPageMetadata(record.url, record.finalUrl, transportSource, record.contentType,
                    "Download unavailable: unsafe filename", response->body, response->bodyBytes, nullptr, nullptr,
                    response->statusCode, response->reason, response->redirectCount, response);
                return;
            }

            strcopy(record.suggestedFileName, uniqueName, sizeof(record.suggestedFileName));
            strcopy(record.savedPath, uniquePath, sizeof(record.savedPath));
            if (!kernel_write_binary_file_bare_metal(record.savedPath, response->body, response->bodyBytes, writeError, sizeof(writeError))) {
                record.success = false;
                strcopy(record.error, writeError[0] ? writeError : "Navigator could not write the file to the downloads directory.", sizeof(record.error));
            } else {
                vfs::FileInfo info{};
                vfs::Status statStatus = vfs::stat(record.savedPath, &info);
                if (statStatus != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR || (int)info.size != record.byteCount) {
                    record.success = false;
                    strcopy(record.error, statStatus == vfs::VFS_OK ? "Download verification failed" : kernel_vfs_status_text(statStatus), sizeof(record.error));
                } else {
                    record.success = true;
                }
            }

            if (!response->unsupportedReason[0]) {
                strcopy(response->unsupportedReason, "Unsupported content type", sizeof(response->unsupportedReason));
            }
            m_metaDownloaded = record.success;
            m_metaDownloadByteCount = record.byteCount;
            strcopy(m_metaDownloadSavedPath, record.savedPath, sizeof(m_metaDownloadSavedPath));
            strcopy(m_lastDownloadError, record.error, sizeof(m_lastDownloadError));
            rememberDownload(record);
            buildDownloadResultDocument(record);
            rememberPageMetadata(record.url, record.finalUrl, transportSource, record.contentType,
                record.success ? "Unsupported content type downloaded" : "Download failed",
                response->body, response->bodyBytes, nullptr, nullptr, response->statusCode, response->reason,
                response->redirectCount, response);
        }
        return;
    }

    if (response->statusCode == 301 || response->statusCode == 302 || response->statusCode == 303 ||
        response->statusCode == 307 || response->statusCode == 308) {
        strcopy(m_currentUrl, response->finalUrl[0] ? response->finalUrl : url, MAX_URL_LEN);
        strcopy(m_title, "HTTP Redirect", MAX_TITLE_LEN_NAV);
        m_blockCount = 0;
        addBlock(BLOCK_HEADING, "HTTP Redirect");
        addBlock(BLOCK_PARAGRAPH, "Navigator could not follow this redirect because the Location header was missing or unsupported.");
        if (response->location[0]) addBlock(BLOCK_LINK, response->location, response->location);
        addBlock(BLOCK_LINK, "Page Info", "about:page-info");
        rememberPageMetadata(response->requestedUrl[0] ? response->requestedUrl : url,
            response->finalUrl[0] ? response->finalUrl : url, transportSource, response->contentType,
            "Redirect not followed", response->body, response->bodyBytes, nullptr, nullptr,
            response->statusCode, response->reason, response->redirectCount, response);
        return;
    }

    char message[160];
    strcopy(message, "HTTP error status ", sizeof(message));
    int len = strlen_local(message);
    int code = response->statusCode;
    if (len < (int)sizeof(message) - 4) {
        message[len++] = (char)('0' + ((code / 100) % 10));
        message[len++] = (char)('0' + ((code / 10) % 10));
        message[len++] = (char)('0' + (code % 10));
        message[len] = '\0';
    }
    strcopy(m_currentUrl, response->finalUrl[0] ? response->finalUrl : url, MAX_URL_LEN);
    strcopy(m_title, message, MAX_TITLE_LEN_NAV);
    m_blockCount = 0;
    addBlock(BLOCK_HEADING, message);
    addBlock(BLOCK_PARAGRAPH,
        response->statusCode == 404
            ? "The server replied, but the requested page was not found."
            : (response->statusCode >= 500
                ? "The server reported an internal failure after Navigator reached it successfully."
                : "The server replied with a non-success HTTP status."));
    addBlock(BLOCK_LINK, "Page Info", "about:page-info");
    addBlock(BLOCK_LINK, "Go to about:navigator", "about:navigator");
    rememberPageMetadata(response->requestedUrl[0] ? response->requestedUrl : url,
        response->finalUrl[0] ? response->finalUrl : url, transportSource, response->contentType,
        "HTTP error status", response->body, response->bodyBytes, nullptr, nullptr,
        response->statusCode, response->reason, response->redirectCount, response);
}

void NavigatorApp::submitFormsLitePost(const char* action, const char* body, int bodyBytes, const char* contentType)
{
    const char* safeAction = action ? action : "";
    clearPageDownloadMetadata();
    strcopy(m_lastSubmittedFormAction, safeAction, sizeof(m_lastSubmittedFormAction));
    strcopy(m_lastSubmittedFormMethod, "POST", sizeof(m_lastSubmittedFormMethod));
    m_lastSubmittedFormStatus[0] = '\0';
    m_lastPostHttpStatus[0] = '\0';
    m_lastPostContentType[0] = '\0';
    m_lastPostBodyBytes = bodyBytes > 0 ? bodyBytes : 0;
    m_lastFormError[0] = '\0';

    KernelHttpResponse* response = kernel_http_post(safeAction, body, bodyBytes, contentType);
    if (response->statusCode > 0) {
        nav_int_to_text(response->statusCode, m_lastPostHttpStatus, sizeof(m_lastPostHttpStatus));
        if (response->reason[0]) {
            strappend(m_lastPostHttpStatus, " ", sizeof(m_lastPostHttpStatus));
            strappend(m_lastPostHttpStatus, response->reason, sizeof(m_lastPostHttpStatus));
        }
    }
    strcopy(m_lastPostContentType, response->contentType, sizeof(m_lastPostContentType));
    strcopy(m_lastSubmittedFormStatus, response->ok ? "POST submitted" : "POST failed", sizeof(m_lastSubmittedFormStatus));
    if (!response->ok) strcopy(m_lastFormError, response->error[0] ? response->error : "POST failed", sizeof(m_lastFormError));

    if (!streq_local(m_currentUrl, safeAction) && m_currentUrl[0]) {
        nav_push_url(m_backStack, m_backCount, m_currentUrl);
        m_forwardCount = 0;
    }
    loadHttpResponse(safeAction, response);
}

bool NavigatorApp::smokeHttpFetch(const char* url, int* statusCode, char* contentType,
                                  int contentTypeLen, int* bodyBytes, int* parsedBlocks,
                                  char* error, int errorLen, char* finalUrl, int finalUrlLen,
                                  int* redirectCount, int* remoteImages, int* loadedImages,
                                  int* failedImages)
{
    if (statusCode) *statusCode = 0;
    if (contentType && contentTypeLen > 0) contentType[0] = '\0';
    if (bodyBytes) *bodyBytes = 0;
    if (parsedBlocks) *parsedBlocks = 0;
    if (error && errorLen > 0) error[0] = '\0';
    if (finalUrl && finalUrlLen > 0) finalUrl[0] = '\0';
    if (redirectCount) *redirectCount = 0;
    if (remoteImages) *remoteImages = 0;
    if (loadedImages) *loadedImages = 0;
    if (failedImages) *failedImages = 0;

    KernelHttpResponse* response = kernel_http_fetch(url);
    if (statusCode) *statusCode = response->statusCode;
    if (contentType && contentTypeLen > 0) strcopy(contentType, response->contentType, contentTypeLen);
    if (bodyBytes) *bodyBytes = response->bodyBytes;
    if (finalUrl && finalUrlLen > 0) strcopy(finalUrl, response->finalUrl[0] ? response->finalUrl : url, finalUrlLen);
    if (redirectCount) *redirectCount = response->redirectCount;
    if (!response->ok) {
        if (error && errorLen > 0) strcopy(error, response->error, errorLen);
        return false;
    }

    NavigatorApp app;
    if (response->statusCode == 200 && gxos::web::httpSharedEqualsInsensitive(response->contentType, "text/html")) {
        int docBytes = response->bodyBytes;
        if (docBytes > kKernelHttpBodyLimit) docBytes = kKernelHttpBodyLimit;
        for (int i = 0; i < docBytes; ++i) s_kernelHttpDocumentBody[i] = response->body[i];
        s_kernelHttpDocumentBody[docBytes] = '\0';
        app.parseHtmlDocument(response->finalUrl[0] ? response->finalUrl : url, s_kernelHttpDocumentBody,
            response->scheme[0] ? response->scheme : "http", response->contentType, response->statusCode,
            response->reason, response->requestedUrl[0] ? response->requestedUrl : url,
            response->redirectCount, response);
    } else if (response->statusCode == 200 && gxos::web::httpSharedEqualsInsensitive(response->contentType, "text/plain")) {
        app.m_blockCount = 0;
        app.addBlock(BLOCK_PREFORMATTED, response->body);
    }
    if (parsedBlocks) *parsedBlocks = app.m_blockCount;
    if (remoteImages) *remoteImages = app.m_metaRemoteImages;
    if (loadedImages) *loadedImages = app.m_metaLoadedImages;
    if (failedImages) *failedImages = app.m_metaFailedImages;
    return response->ok;
}

bool NavigatorApp::smokeControlledLocalHttpsNavigation(const char* url,
                                                       int* statusCode, char* contentType,
                                                       int contentTypeLen, int* bodyBytes,
                                                       int* parsedBlocks, char* error,
                                                       int errorLen, char* finalUrl,
                                                       int finalUrlLen, int* redirectCount,
                                                       int* plainTcpConnectAttempts,
                                                       int* tlsTcpConnectAttempts,
                                                       uint32_t* tlsVerifyFlags,
                                                       char* tlsSniHost, int tlsSniHostLen,
                                                       char* tlsProtocol, int tlsProtocolLen,
                                                       char* tlsCipherSuite, int tlsCipherSuiteLen,
                                                       char* transportSelection,
                                                       int transportSelectionLen,
                                                       char* tlsStatus,
                                                       int tlsStatusLen,
                                                       bool* tlsValidated,
                                                       bool* tlsHostnameValidated,
                                                       bool* tlsAllowlistLocalOnly,
                                                       char* sourceType, int sourceTypeLen)
{
    if (statusCode) *statusCode = 0;
    if (contentType && contentTypeLen > 0) contentType[0] = '\0';
    if (bodyBytes) *bodyBytes = 0;
    if (parsedBlocks) *parsedBlocks = 0;
    if (error && errorLen > 0) error[0] = '\0';
    if (finalUrl && finalUrlLen > 0) finalUrl[0] = '\0';
    if (redirectCount) *redirectCount = 0;
    if (plainTcpConnectAttempts) *plainTcpConnectAttempts = 0;
    if (tlsTcpConnectAttempts) *tlsTcpConnectAttempts = 0;
    if (tlsVerifyFlags) *tlsVerifyFlags = 0;
    if (tlsSniHost && tlsSniHostLen > 0) tlsSniHost[0] = '\0';
    if (tlsProtocol && tlsProtocolLen > 0) tlsProtocol[0] = '\0';
    if (tlsCipherSuite && tlsCipherSuiteLen > 0) tlsCipherSuite[0] = '\0';
    if (transportSelection && transportSelectionLen > 0) transportSelection[0] = '\0';
    if (tlsStatus && tlsStatusLen > 0) tlsStatus[0] = '\0';
    if (tlsValidated) *tlsValidated = false;
    if (tlsHostnameValidated) *tlsHostnameValidated = false;
    if (tlsAllowlistLocalOnly) *tlsAllowlistLocalOnly = false;
    if (sourceType && sourceTypeLen > 0) sourceType[0] = '\0';

    const int plainAttemptsBefore = s_kernelHttpPlainTcpConnectAttempts;
    const int tlsAttemptsBefore = s_kernelHttpTlsConnectAttempts;
    const int httpsLoadsBefore = s_kernelHttpControlledLocalHttpsLoads;
    NavigatorApp app;
    app.loadUrl(url);
    const int plainAttempts = s_kernelHttpPlainTcpConnectAttempts - plainAttemptsBefore;
    const int tlsAttempts = s_kernelHttpTlsConnectAttempts - tlsAttemptsBefore;
    const int httpsLoads = s_kernelHttpControlledLocalHttpsLoads - httpsLoadsBefore;

    if (statusCode) *statusCode = app.m_metaHttpStatusCode;
    if (contentType && contentTypeLen > 0) strcopy(contentType, app.m_metaContentType, contentTypeLen);
    if (bodyBytes) *bodyBytes = app.m_metaSourceBytes;
    if (parsedBlocks) *parsedBlocks = app.m_blockCount;
    if (error && errorLen > 0) strcopy(error, app.m_metaErrorStatus, errorLen);
    if (finalUrl && finalUrlLen > 0) strcopy(finalUrl, app.m_metaFinalUrl, finalUrlLen);
    if (redirectCount) *redirectCount = app.m_metaRedirectCount;
    if (plainTcpConnectAttempts) *plainTcpConnectAttempts = plainAttempts;
    if (tlsTcpConnectAttempts) *tlsTcpConnectAttempts = tlsAttempts;
    if (tlsVerifyFlags) *tlsVerifyFlags = app.m_metaTlsVerifyFlags;
    if (tlsSniHost && tlsSniHostLen > 0) strcopy(tlsSniHost, app.m_metaTlsSniHost, tlsSniHostLen);
    if (tlsProtocol && tlsProtocolLen > 0) strcopy(tlsProtocol, app.m_metaTlsProtocol, tlsProtocolLen);
    if (tlsCipherSuite && tlsCipherSuiteLen > 0) strcopy(tlsCipherSuite, app.m_metaTlsCipherSuite, tlsCipherSuiteLen);
    if (transportSelection && transportSelectionLen > 0) strcopy(transportSelection, app.m_metaTransportSelection, transportSelectionLen);
    if (tlsStatus && tlsStatusLen > 0) strcopy(tlsStatus, app.m_metaTlsStatus, tlsStatusLen);
    if (tlsValidated) *tlsValidated = app.m_metaTlsValidated;
    if (tlsHostnameValidated) *tlsHostnameValidated = app.m_metaTlsHostnameValidated;
    if (tlsAllowlistLocalOnly) *tlsAllowlistLocalOnly = app.m_metaTlsAllowlistLocalOnly;
    if (sourceType && sourceTypeLen > 0) strcopy(sourceType, app.m_metaSourceType, sourceTypeLen);

    return streq_local(app.m_metaRequestedUrl, url ? url : "") &&
        streq_local(app.m_metaSourceType, "https") &&
        streq_local(app.m_metaTransportSelection, "LocalAllowlistedTlsHttps") &&
        streq_local(app.m_metaTlsStatus, "Success") &&
        app.m_metaTlsUsed &&
        app.m_metaTlsAllowlistLocalOnly &&
        app.m_metaTlsValidated &&
        app.m_metaTlsHostnameValidated &&
        app.m_metaHttpStatusCode == 200 &&
        app.m_blockCount > 0 &&
        tlsAttempts == 1 &&
        httpsLoads == 1;
}

static bool nav_form_append_char(char* out, int outSize, int& used, char value)
{
    if (!out || outSize <= 0 || used >= outSize - 1) return false;
    out[used++] = value;
    out[used] = '\0';
    return true;
}

static bool nav_form_append_encoded(char* out, int outSize, int& used, const char* value)
{
    static const char* hex = "0123456789ABCDEF";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value ? value : ""); *p; ++p) {
        unsigned char ch = *p;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            if (!nav_form_append_char(out, outSize, used, (char)ch)) return false;
        } else if (ch == ' ') {
            if (!nav_form_append_char(out, outSize, used, '+')) return false;
        } else {
            if (!nav_form_append_char(out, outSize, used, '%') ||
                !nav_form_append_char(out, outSize, used, hex[(ch >> 4) & 0x0F]) ||
                !nav_form_append_char(out, outSize, used, hex[ch & 0x0F])) return false;
        }
    }
    return true;
}

static bool nav_form_append_field(char* out, int outSize, int& used, const char* name, const char* value)
{
    if (!name || !name[0]) return true;
    if (used > 0 && !nav_form_append_char(out, outSize, used, '&')) return false;
    return nav_form_append_encoded(out, outSize, used, name) &&
           nav_form_append_char(out, outSize, used, '=') &&
           nav_form_append_encoded(out, outSize, used, value);
}

void NavigatorApp::activateFormControl(int blockIndex)
{
    if (blockIndex < 0 || blockIndex >= m_blockCount) return;
    DocBlock& block = m_blocks[blockIndex];
    if (!isFocusableFormBlock(block)) return;
    if (block.kind == BLOCK_FORM_CHECKBOX) {
        block.checked = !block.checked;
    } else if (block.kind == BLOCK_FORM_RADIO) {
        for (int i = 0; i < m_blockCount; ++i) {
            if (m_blocks[i].kind == BLOCK_FORM_RADIO &&
                m_blocks[i].formIndex == block.formIndex &&
                streq_local(m_blocks[i].inputName, block.inputName)) {
                m_blocks[i].checked = false;
            }
        }
        block.checked = true;
    } else if (block.kind == BLOCK_FORM_SELECT) {
        if (block.optionCount > 0) {
            block.selectedOption = block.selectedOption < 0 ? 0 : (block.selectedOption + 1) % block.optionCount;
            strcopy(block.inputValue, block.options[block.selectedOption].value, sizeof(block.inputValue));
            strcopy(block.text, block.options[block.selectedOption].text, sizeof(block.text));
        }
    } else if (block.kind == BLOCK_FORM_SUBMIT) {
        submitFormForBlock(blockIndex);
        return;
    }
    m_focusedFormBlock = blockIndex;
    invalidate();
}

bool NavigatorApp::setFormControlValue(const char* name, const char* value)
{
    for (int i = 0; i < m_blockCount; ++i) {
        if ((m_blocks[i].kind == BLOCK_FORM_TEXT || m_blocks[i].kind == BLOCK_FORM_TEXTAREA) &&
            streq_local(m_blocks[i].inputName, name ? name : "")) {
            strcopy(m_blocks[i].inputValue, value ? value : "", sizeof(m_blocks[i].inputValue));
            m_focusedFormBlock = i;
            m_formCaret = strlen_local(m_blocks[i].inputValue);
            return true;
        }
    }
    return false;
}

bool NavigatorApp::setFormCheckbox(const char* name, bool checked)
{
    for (int i = 0; i < m_blockCount; ++i) {
        if (m_blocks[i].kind == BLOCK_FORM_CHECKBOX && streq_local(m_blocks[i].inputName, name ? name : "")) {
            m_blocks[i].checked = checked;
            return true;
        }
    }
    return false;
}

bool NavigatorApp::selectFormRadio(const char* name, const char* value)
{
    for (int i = 0; i < m_blockCount; ++i) {
        if (m_blocks[i].kind == BLOCK_FORM_RADIO &&
            streq_local(m_blocks[i].inputName, name ? name : "") &&
            streq_local(m_blocks[i].inputValue, value ? value : "")) {
            activateFormControl(i);
            return true;
        }
    }
    return false;
}

bool NavigatorApp::selectFormOption(const char* name, const char* value)
{
    for (int i = 0; i < m_blockCount; ++i) {
        DocBlock& block = m_blocks[i];
        if (block.kind != BLOCK_FORM_SELECT || !streq_local(block.inputName, name ? name : "")) continue;
        for (int option = 0; option < block.optionCount; ++option) {
            if (!streq_local(block.options[option].value, value ? value : "")) continue;
            block.selectedOption = option;
            strcopy(block.inputValue, block.options[option].value, sizeof(block.inputValue));
            strcopy(block.text, block.options[option].text, sizeof(block.text));
            return true;
        }
    }
    return false;
}

void NavigatorApp::submitFormForBlock(int blockIndex)
{
    if (blockIndex < 0 || blockIndex >= m_blockCount || !isFormBlock(m_blocks[blockIndex])) return;
    DocBlock& source = m_blocks[blockIndex];
    const char* method = source.formMethod[0] ? source.formMethod : "get";
    const char* encoding = source.formEncoding[0] ? source.formEncoding : "application/x-www-form-urlencoded";
    const char* action = source.formAction[0] ? source.formAction : m_currentUrl;
    m_lastFormError[0] = '\0';
    strcopy(m_lastSubmittedFormAction, action, sizeof(m_lastSubmittedFormAction));
    strcopy(m_lastSubmittedFormMethod, streq_local(method, "post") ? "POST" : "GET", sizeof(m_lastSubmittedFormMethod));
    if (source.formUnsupported || (!streq_local(method, "get") && !streq_local(method, "post")) ||
        !streq_local(encoding, "application/x-www-form-urlencoded")) {
        strcopy(m_lastSubmittedFormStatus, "Unsupported form", sizeof(m_lastSubmittedFormStatus));
        strcopy(m_lastFormError, "Unsupported form method or encoding", sizeof(m_lastFormError));
        setStatus(m_lastFormError);
        return;
    }

    char body[kKernelHttpPostBodyLimit + 1];
    int used = 0;
    body[0] = '\0';
    for (int i = 0; i < m_blockCount; ++i) {
        const DocBlock& block = m_blocks[i];
        if (block.formIndex != source.formIndex || block.disabled || !block.inputName[0]) continue;
        bool ok = true;
        if (block.kind == BLOCK_FORM_TEXT || block.kind == BLOCK_FORM_TEXTAREA) {
            ok = nav_form_append_field(body, sizeof(body), used, block.inputName, block.inputValue);
        } else if ((block.kind == BLOCK_FORM_CHECKBOX || block.kind == BLOCK_FORM_RADIO) && block.checked) {
            ok = nav_form_append_field(body, sizeof(body), used, block.inputName, block.inputValue[0] ? block.inputValue : "on");
        } else if (block.kind == BLOCK_FORM_SELECT) {
            const char* value = block.inputValue;
            if (block.selectedOption >= 0 && block.selectedOption < block.optionCount) value = block.options[block.selectedOption].value;
            ok = nav_form_append_field(body, sizeof(body), used, block.inputName, value);
        }
        if (!ok) {
            strcopy(m_lastSubmittedFormStatus, "Form body too large", sizeof(m_lastSubmittedFormStatus));
            strcopy(m_lastFormError, "Forms-lite encoded body exceeds 8192 bytes", sizeof(m_lastFormError));
            setStatus(m_lastFormError);
            return;
        }
    }

    blurFormBlock();
    if (streq_local(method, "get")) {
        char submitUrl[MAX_URL_LEN];
        strcopy(submitUrl, action, sizeof(submitUrl));
        if (used > 0) {
            strappend(submitUrl, nav_find_char(submitUrl, '?') ? "&" : "?", sizeof(submitUrl));
            strappend(submitUrl, body, sizeof(submitUrl));
        }
        strcopy(m_lastSubmittedFormStatus, "GET submitted", sizeof(m_lastSubmittedFormStatus));
        navigateTo(submitUrl);
        return;
    }

    if (!nav_starts_with(action, "http://")) {
        strcopy(m_lastSubmittedFormStatus, "POST action unsupported", sizeof(m_lastSubmittedFormStatus));
        strcopy(m_lastFormError, "Bare-metal Forms-lite POST supports plain http:// actions only", sizeof(m_lastFormError));
        buildErrorDocument(action, m_lastFormError);
        rememberPageMetadata(action, action, "unsupported", encoding, m_lastFormError, body, used);
        setStatus(m_lastFormError);
        return;
    }
    submitFormsLitePost(action, body, used, encoding);
}

static bool nav_build_forms_lite_smoke_body(char* out, int outSize, int* bodyBytes)
{
    if (!out || outSize <= 0) return false;
    int used = 0;
    out[0] = '\0';
    bool ok = nav_form_append_field(out, outSize, used, "q", "posted value") &&
              nav_form_append_field(out, outSize, used, "agree", "yes") &&
              nav_form_append_field(out, outSize, used, "kind", "alpha") &&
              nav_form_append_field(out, outSize, used, "note", "hello\nsecond line") &&
              nav_form_append_field(out, outSize, used, "size", "m");
    if (bodyBytes) *bodyBytes = used;
    return ok;
}

bool NavigatorApp::smokeFormsLitePost(const char* action, int* statusCode, char* contentType,
                                      int contentTypeLen, int* bodyBytes, int* parsedBlocks,
                                      char* error, int errorLen, char* finalUrl, int finalUrlLen,
                                      int* redirectCount, int* submittedBodyBytes)
{
    if (statusCode) *statusCode = 0;
    if (contentType && contentTypeLen > 0) contentType[0] = '\0';
    if (bodyBytes) *bodyBytes = 0;
    if (parsedBlocks) *parsedBlocks = 0;
    if (error && errorLen > 0) error[0] = '\0';
    if (finalUrl && finalUrlLen > 0) finalUrl[0] = '\0';
    if (redirectCount) *redirectCount = 0;
    if (submittedBodyBytes) *submittedBodyBytes = 0;

    char formBody[256];
    int formBodyBytes = 0;
    if (!nav_build_forms_lite_smoke_body(formBody, sizeof(formBody), &formBodyBytes)) {
        if (error && errorLen > 0) strcopy(error, "Could not encode deterministic Forms-lite POST body", errorLen);
        return false;
    }

    NavigatorApp app;
    app.submitFormsLitePost(action, formBody, formBodyBytes, "application/x-www-form-urlencoded");
    KernelHttpResponse* response = &s_kernelHttpResponse;
    int renderedBlocks = app.m_blockCount;
    auto blocksContain = [&app](const char* needle) {
        if (!needle || !needle[0]) return false;
        for (int i = 0; i < app.m_blockCount; ++i) {
            const char* text = app.m_blocks[i].text;
            for (int start = 0; text[start]; ++start) {
                int j = 0;
                while (needle[j] && text[start + j] == needle[j]) ++j;
                if (!needle[j]) return true;
            }
        }
        return false;
    };
    bool diagnosticsOk = streq_local(app.m_lastSubmittedFormMethod, "POST") &&
                         streq_local(app.m_lastSubmittedFormAction, action) &&
                         app.m_lastPostBodyBytes == formBodyBytes &&
                         streq_local(app.m_lastPostContentType, response->contentType);
    app.buildPageInfoDocument();
    const bool pageInfoOk = blocksContain("Forms-lite POST bare-metal: enabled-basic") &&
                            blocksContain("Last submitted method: POST") &&
                            blocksContain("Last POST body bytes: 67");
    app.buildRuntimeDocument();
    const bool runtimeOk = blocksContain("Forms-lite POST forms bare-metal: enabled-basic") &&
                           blocksContain("Forms-lite POST redirect policy: 303 becomes GET") &&
                           blocksContain("Last submitted method: POST") &&
                           blocksContain("Last POST body bytes: 67");
    diagnosticsOk = diagnosticsOk && pageInfoOk && runtimeOk;
    if (statusCode) *statusCode = response->statusCode;
    if (contentType && contentTypeLen > 0) strcopy(contentType, response->contentType, contentTypeLen);
    if (bodyBytes) *bodyBytes = response->bodyBytes;
    if (parsedBlocks) *parsedBlocks = renderedBlocks;
    if (finalUrl && finalUrlLen > 0) strcopy(finalUrl, response->finalUrl[0] ? response->finalUrl : action, finalUrlLen);
    if (redirectCount) *redirectCount = response->redirectCount;
    if (submittedBodyBytes) *submittedBodyBytes = app.m_lastPostBodyBytes;
    if ((!response->ok || !diagnosticsOk) && error && errorLen > 0) {
        strcopy(error, response->error[0] ? response->error : "Forms-lite POST diagnostics mismatch", errorLen);
    }
    if (!response->ok || !diagnosticsOk) {
        serial::puts("[NAVIGATOR-SMOKE] forms_post.debug.method=");
        serial::puts(app.m_lastSubmittedFormMethod);
        serial::puts(" action=");
        serial::puts(app.m_lastSubmittedFormAction);
        serial::puts(" response_type=");
        serial::puts(response->contentType);
        serial::puts(" post_type=");
        serial::puts(app.m_lastPostContentType);
        serial::puts(" page_info=");
        serial::puts(pageInfoOk ? "yes" : "no");
        serial::puts(" runtime=");
        serial::puts(runtimeOk ? "yes" : "no");
        serial::puts(" body_bytes=");
        char bodyBytesText[24];
        nav_int_to_text(app.m_lastPostBodyBytes, bodyBytesText, sizeof(bodyBytesText));
        serial::puts(bodyBytesText);
        serial::puts(" http_status=");
        serial::puts(app.m_lastPostHttpStatus);
        serial::puts("\n");
    }
    return response->ok && diagnosticsOk;
}

bool NavigatorApp::smokeInteractiveFormsLitePost(const char* formUrl, int* statusCode,
                                                 char* contentType, int contentTypeLen,
                                                 int* bodyBytes, int* parsedBlocks,
                                                 char* error, int errorLen,
                                                 int* submittedBodyBytes)
{
    if (statusCode) *statusCode = 0;
    if (contentType && contentTypeLen > 0) contentType[0] = '\0';
    if (bodyBytes) *bodyBytes = 0;
    if (parsedBlocks) *parsedBlocks = 0;
    if (error && errorLen > 0) error[0] = '\0';
    if (submittedBodyBytes) *submittedBodyBytes = 0;

    NavigatorApp app;
    app.loadUrl(formUrl);
    auto findControl = [&app](BlockKind kind, const char* name, const char* value = nullptr) {
        for (int i = 0; i < app.m_blockCount; ++i) {
            if (app.m_blocks[i].kind != kind) continue;
            if (name && !streq_local(app.m_blocks[i].inputName, name)) continue;
            if (value && !streq_local(app.m_blocks[i].inputValue, value)) continue;
            return i;
        }
        return -1;
    };
    int agree = findControl(BLOCK_FORM_CHECKBOX, "agree");
    int alpha = findControl(BLOCK_FORM_RADIO, "kind", "alpha");
    int size = findControl(BLOCK_FORM_SELECT, "size");
    int submit = findControl(BLOCK_FORM_SUBMIT, nullptr);
    bool controlsOk = app.m_metaFormCount == 1 &&
                      app.m_metaTextInputCount == 2 &&
                      app.m_metaCheckboxCount == 2 &&
                      app.m_metaRadioCount == 2 &&
                      app.m_metaTextareaCount == 1 &&
                      app.m_metaSelectCount == 1 &&
                      app.setFormControlValue("q", "posted value") &&
                      app.setFormControlValue("note", "hello\nsecond line") &&
                      agree >= 0 && alpha >= 0 && size >= 0 && submit >= 0;
    if (controlsOk) {
        if (!app.m_blocks[agree].checked) app.activateFormControl(agree);
        app.activateFormControl(alpha);
        int attempts = 0;
        while (!streq_local(app.m_blocks[size].inputValue, "m") && attempts++ < MAX_FORM_OPTIONS) {
            app.activateFormControl(size);
        }
        controlsOk = streq_local(app.m_blocks[size].inputValue, "m");
    }
    if (controlsOk) app.activateFormControl(submit);

    KernelHttpResponse* response = &s_kernelHttpResponse;
    int renderedBlocks = app.m_blockCount;
    auto blocksContain = [&app](const char* needle) {
        for (int i = 0; needle && needle[0] && i < app.m_blockCount; ++i) {
            const char* text = app.m_blocks[i].text;
            for (int start = 0; text[start]; ++start) {
                int j = 0;
                while (needle[j] && text[start + j] == needle[j]) ++j;
                if (!needle[j]) return true;
            }
        }
        return false;
    };
    bool renderedOk = blocksContain("Bare-metal POST OK");
    app.buildPageInfoDocument();
    bool diagnosticsOk = blocksContain("Forms-lite interactive controls: enabled") &&
                         blocksContain("Forms-lite POST interactive: enabled") &&
                         blocksContain("Last submitted method: POST") &&
                         blocksContain("Last POST HTTP status: 200 OK") &&
                         blocksContain("Last POST content type: text/html") &&
                         blocksContain("Last POST body bytes: 67");
    if (statusCode) *statusCode = response->statusCode;
    if (contentType && contentTypeLen > 0) strcopy(contentType, response->contentType, contentTypeLen);
    if (bodyBytes) *bodyBytes = response->bodyBytes;
    if (parsedBlocks) *parsedBlocks = renderedBlocks;
    if (submittedBodyBytes) *submittedBodyBytes = app.m_lastPostBodyBytes;
    bool ok = controlsOk && response->ok && response->statusCode == 200 &&
              app.m_lastPostBodyBytes == 67 && renderedOk && diagnosticsOk;
    if (!ok && error && errorLen > 0) {
        strcopy(error, response->error[0] ? response->error : "Interactive Forms-lite POST smoke mismatch", errorLen);
    }
    if (!ok) {
        serial::puts("[NAVIGATOR-SMOKE] interactive_forms.debug.controls=");
        serial::puts(controlsOk ? "yes" : "no");
        serial::puts(" rendered=");
        serial::puts(renderedOk ? "yes" : "no");
        serial::puts(" diagnostics=");
        serial::puts(diagnosticsOk ? "yes" : "no");
        serial::puts(" method=");
        serial::puts(app.m_lastSubmittedFormMethod);
        serial::puts(" post_status=");
        serial::puts(app.m_lastPostHttpStatus);
        serial::puts(" post_type=");
        serial::puts(app.m_lastPostContentType);
        serial::puts(" body_bytes=");
        char bodyBytesText[24];
        nav_int_to_text(app.m_lastPostBodyBytes, bodyBytesText, sizeof(bodyBytesText));
        serial::puts(bodyBytesText);
        serial::puts(" response_type=");
        serial::puts(response->contentType);
        serial::puts("\n");
    }
    return ok;
}

bool NavigatorApp::smokeInteractiveFormsLiteGet(const char* formUrl, char* finalUrl,
                                                int finalUrlLen, int* parsedBlocks,
                                                char* error, int errorLen)
{
    if (finalUrl && finalUrlLen > 0) finalUrl[0] = '\0';
    if (parsedBlocks) *parsedBlocks = 0;
    if (error && errorLen > 0) error[0] = '\0';
    NavigatorApp app;
    app.loadUrl(formUrl);
    auto findControl = [&app](BlockKind kind, const char* name, const char* value = nullptr) {
        for (int i = 0; i < app.m_blockCount; ++i) {
            if (app.m_blocks[i].kind != kind) continue;
            if (name && !streq_local(app.m_blocks[i].inputName, name)) continue;
            if (value && !streq_local(app.m_blocks[i].inputValue, value)) continue;
            return i;
        }
        return -1;
    };
    int agree = findControl(BLOCK_FORM_CHECKBOX, "agree");
    int alpha = findControl(BLOCK_FORM_RADIO, "kind", "alpha");
    int size = findControl(BLOCK_FORM_SELECT, "size");
    int submit = findControl(BLOCK_FORM_SUBMIT, nullptr);
    bool controlsOk = app.setFormControlValue("q", "posted value") &&
                      app.setFormControlValue("note", "hello\nsecond line") &&
                      agree >= 0 && alpha >= 0 && size >= 0 && submit >= 0;
    if (controlsOk) {
        if (!app.m_blocks[agree].checked) app.activateFormControl(agree);
        app.activateFormControl(alpha);
        int attempts = 0;
        while (!streq_local(app.m_blocks[size].inputValue, "m") && attempts++ < MAX_FORM_OPTIONS) {
            app.activateFormControl(size);
        }
        controlsOk = streq_local(app.m_blocks[size].inputValue, "m");
    }
    if (controlsOk) app.activateFormControl(submit);
    if (finalUrl && finalUrlLen > 0) strcopy(finalUrl, app.m_currentUrl, finalUrlLen);
    if (parsedBlocks) *parsedBlocks = app.m_blockCount;
    bool renderedOk = false;
    for (int i = 0; i < app.m_blockCount; ++i) {
        if (streq_local(app.m_blocks[i].text, "Bare-metal GET OK")) renderedOk = true;
    }
    bool ok = controlsOk && renderedOk && streq_local(app.m_lastSubmittedFormMethod, "GET");
    if (!ok && error && errorLen > 0) strcopy(error, "Interactive Forms-lite GET smoke mismatch", errorLen);
    return ok;
}

void NavigatorApp::loadFileUrl(const char* url)
{
    if (!nav_starts_with(url, "file://")) {
        buildErrorDocument(url, "Only file:// URLs are available in this runtime.");
        rememberPageMetadata(url, url, "unsupported", "", "Only file:// URLs are available", nullptr, 0);
        return;
    }

    char path[MAX_URL_LEN];
    int pathLen = 0;
    const char* urlPath = url + 7;
    while (urlPath[pathLen] && urlPath[pathLen] != '?' && urlPath[pathLen] != '#' && pathLen < MAX_URL_LEN - 1) {
        path[pathLen] = urlPath[pathLen];
        ++pathLen;
    }
    path[pathLen] = '\0';
    static char buffer[32768];
    int32_t bytesRead = vfs::read_file(path, buffer, sizeof(buffer) - 1);
    if (bytesRead < 0) {
        if (streq_local(path, "/docs/index.html")) {
            const char* fallback =
                "<html><head><title>guideXOS Navigator Help</title></head><body>"
                "<h1>guideXOS Navigator Help</h1>"
                "<p>Welcome to guideXOS Navigator, the built-in document viewer and browser shell for guideXOS Server.</p>"
                "<p>Navigator renders local guideWeb documents from the filesystem without requiring CSS, JavaScript, or a network connection.</p>"
                "<img src=\"/assets/Images/BlueVelvet/48/web.png\" alt=\"guideXOS image\">"
                "<h2>Getting Started</h2>"
                "<li>Back and Forward navigate page history</li>"
                "<li>Reload re-reads the current page</li>"
                "<li>Home returns to about:navigator</li>"
                "<li>Bookmarks opens saved pages</li>"
                "<li>Add &#9733; bookmarks the current page</li>"
                "<h2>Topics</h2>"
                "<a href=\"desktop.html\">guideXOS Desktop Guide</a>"
                "</body></html>";
            parseHtmlDocument(url, fallback);
            return;
        }
        if (streq_local(path, "/docs/desktop.html")) {
            const char* fallback =
                "<html><head><title>guideXOS Desktop Guide</title></head><body>"
                "<h1>guideXOS Desktop Guide</h1>"
                "<p>The guideXOS desktop hosts built-in applications, pinned shortcuts, windows, and the taskbar.</p>"
                "<a href=\"index.html\">Back to Navigator Help</a>"
                "</body></html>";
            parseHtmlDocument(url, fallback);
            return;
        }
        buildErrorDocument(url, "The requested file was not found.");
        rememberPageMetadata(url, url, "file", "text/html", "File not found", nullptr, 0);
        return;
    }
    if (bytesRead >= (int32_t)(sizeof(buffer) - 1)) {
        buildErrorDocument(url, "The requested file is too large.");
        rememberPageMetadata(url, url, "file", "text/html", "File too large", nullptr, 0);
        return;
    }
    buffer[bytesRead] = '\0';
    parseHtmlDocument(url, buffer);
}

void NavigatorApp::loadUrl(const char* url)
{
    if (m_loading) {
        setStatus("Navigation already in progress");
        return;
    }
    static bool animationOwnerLogged = false;
    if (!animationOwnerLogged) {
        serial::puts("[NAVIGATOR] throbber_animation=passive_elapsed_time\n");
        animationOwnerLogged = true;
    }
    m_loading = true;
    m_throbberFrame = 0;
    m_loadingStartTick = (uint32_t)kernel::pit::ticks();
    invalidate();
    char normalized[MAX_URL_LEN];
    normalizeUrl(url && url[0] ? url : "about:navigator", normalized, MAX_URL_LEN);
    clearSelection();
    if (streq_local(normalized, "about:navigator")) {
        buildAboutNavigatorDocument();
    } else if (streq_local(normalized, "about:bookmarks")) {
        buildBookmarksDocument();
    } else if (streq_local(normalized, "about:downloads")) {
        buildDownloadsDocument();
    } else if (streq_local(normalized, "about:page-info")) {
        buildPageInfoDocument();
    } else if (streq_local(normalized, "about:view-source")) {
        buildViewSourceDocument();
    } else if (streq_local(normalized, "about:navigator-runtime")) {
        buildRuntimeDocument();
    } else if (nav_starts_with(normalized, "file://")) {
        loadFileUrl(normalized);
    } else if (nav_starts_with(normalized, "http://") || nav_starts_with(normalized, "https://")) {
        loadHttpUrl(normalized);
    } else {
        buildErrorDocument(normalized, "Unsupported URL. Use about:, file://, http://, or a bounded policy-validated https:// URL.");
        rememberPageMetadata(normalized, normalized, "unsupported", "", "Unsupported URL scheme", nullptr, 0);
    }
    m_scrollY = 0;
    m_addressFocused = false;
    m_addressBuffer[0] = '\0';
    m_addressCaret = 0;
    blurFormBlock();
    setStatus("Ready");
    m_loading = false;

}

bool NavigatorApp::smokeHttpsUnsupportedDocument(const char* url, const char* expectedFinalUrl,
                                                 int expectedPlainTcpConnectAttempts,
                                                 int expectedTlsTcpConnectAttempts,
                                                 char* requestedUrl, int requestedUrlLen,
                                                 char* finalUrl, int finalUrlLen,
                                                 char* error, int errorLen,
                                                 int* plainTcpConnectAttempts,
                                                 int* tlsTcpConnectAttempts)
{
    int plainAttemptsBefore = s_kernelHttpPlainTcpConnectAttempts;
    int tlsAttemptsBefore = s_kernelHttpTlsConnectAttempts;
    NavigatorApp app;
    app.loadUrl(url);
    int plainAttempts = s_kernelHttpPlainTcpConnectAttempts - plainAttemptsBefore;
    int tlsAttempts = s_kernelHttpTlsConnectAttempts - tlsAttemptsBefore;
    if (requestedUrl && requestedUrlLen > 0) strcopy(requestedUrl, app.m_metaRequestedUrl, requestedUrlLen);
    if (finalUrl && finalUrlLen > 0) strcopy(finalUrl, app.m_metaFinalUrl, finalUrlLen);
    if (error && errorLen > 0) strcopy(error, app.m_metaErrorStatus, errorLen);
    if (plainTcpConnectAttempts) *plainTcpConnectAttempts = plainAttempts;
    if (tlsTcpConnectAttempts) *tlsTcpConnectAttempts = tlsAttempts;

    const bool direct = nav_starts_with(url, "https://");
    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const bool broadPublicBlocked =
        httpsPolicy.state == gxos::GxosValidatedHttpsPolicyState::ProductionValidated &&
        !httpsPolicy.broadPublicHttpsEnabled;
    const bool selectedButBlocked =
        (httpsPolicy.selectedState == gxos::GxosValidatedHttpsPolicyState::UserTrustStoreDevMode ||
         httpsPolicy.selectedState == gxos::GxosValidatedHttpsPolicyState::ProductionValidated) &&
        !(httpsPolicy.state == gxos::GxosValidatedHttpsPolicyState::UserTrustStoreDevMode ||
          httpsPolicy.state == gxos::GxosValidatedHttpsPolicyState::ProductionValidated) &&
        httpsPolicy.blocker && httpsPolicy.blocker[0];
    const bool selectedOrBroadPublicBlocked = selectedButBlocked || broadPublicBlocked;
    const bool titleOk = selectedOrBroadPublicBlocked ||
        streq_local(app.m_title, direct ? "HTTPS Unsupported" : "HTTPS Redirect Unsupported");
    const bool errorOk =
        nav_starts_with(app.m_metaErrorStatus, direct ? "HTTPS/TLS unsupported" : "HTTPS/TLS unsupported redirect") ||
        (selectedButBlocked && streq_local(app.m_metaErrorStatus, httpsPolicy.blocker)) ||
        (broadPublicBlocked && streq_local(app.m_metaErrorStatus, httpsPolicy.publicHttpsPilotReason));
    return streq_local(app.m_metaRequestedUrl, url) &&
           streq_local(app.m_metaFinalUrl, expectedFinalUrl) &&
           errorOk &&
           titleOk &&
           app.m_blockCount >= 3 &&
           plainAttempts == expectedPlainTcpConnectAttempts &&
           tlsAttempts == expectedTlsTcpConnectAttempts;
}

static void nav_push_url(char stack[][kNavigatorUrlStorageBytes], int& count, const char* url)
{
    if (!url || !url[0]) return;
    if (count >= 12) {
        for (int i = 1; i < 12; ++i) strcopy(stack[i - 1], stack[i], kNavigatorUrlStorageBytes);
        count = 11;
    }
    strcopy(stack[count++], url, kNavigatorUrlStorageBytes);
}

void NavigatorApp::navigateTo(const char* url)
{
    char normalized[MAX_URL_LEN];
    normalizeUrl(url, normalized, MAX_URL_LEN);
    if (!streq_local(m_currentUrl, normalized) && m_currentUrl[0]) {
        nav_push_url(m_backStack, m_backCount, m_currentUrl);
        m_forwardCount = 0;
    }
    loadUrl(normalized);
}

void NavigatorApp::goBack()
{
    if (m_backCount <= 0) {
        setStatus("No Back history");
        return;
    }
    char target[MAX_URL_LEN];
    strcopy(target, m_backStack[m_backCount - 1], MAX_URL_LEN);
    --m_backCount;
    nav_push_url(m_forwardStack, m_forwardCount, m_currentUrl);
    loadUrl(target);
}

void NavigatorApp::goForward()
{
    if (m_forwardCount <= 0) {
        setStatus("No Forward history");
        return;
    }
    char target[MAX_URL_LEN];
    strcopy(target, m_forwardStack[m_forwardCount - 1], MAX_URL_LEN);
    --m_forwardCount;
    nav_push_url(m_backStack, m_backCount, m_currentUrl);
    loadUrl(target);
}

void NavigatorApp::normalizeUrl(const char* input, char* out, int outSize) const
{
    while (input && (*input == ' ' || *input == '\t')) ++input;
    if (!input || !input[0]) {
        strcopy(out, "about:navigator", outSize);
        return;
    }
    if (nav_starts_with(input, "about:") || nav_starts_with(input, "file://") ||
        nav_starts_with(input, "http://") || nav_starts_with(input, "https://")) {
        strcopy(out, input, outSize);
        return;
    }
    if (input[0] == '/') {
        strcopy(out, "file://", outSize);
        int len = strlen_local(out);
        strcopy(out + len, input, outSize - len);
        return;
    }
    strcopy(out, "file:///", outSize);
    int len = strlen_local(out);
    strcopy(out + len, input, outSize - len);
}

void NavigatorApp::focusAddressBar()
{
    strcopy(m_addressBuffer, m_currentUrl, MAX_URL_LEN);
    m_addressCaret = strlen_local(m_addressBuffer);
    m_addressFocused = true;
    setStatus("Editing address");
}

void NavigatorApp::blurAddressBar()
{
    m_addressFocused = false;
    m_addressBuffer[0] = '\0';
    m_addressCaret = 0;
    setStatus("Ready");
}

void NavigatorApp::commitAddressBar()
{
    char normalized[MAX_URL_LEN];
    normalizeUrl(m_addressBuffer, normalized, MAX_URL_LEN);
    navigateTo(normalized);
}

bool NavigatorApp::hitAddressBar(int x, int y) const
{
    if (!m_window) return false;
    const NavigatorToolbarLayout toolbarLayout = navigatorToolbarLayout(m_window->w);
    return toolbarLayout.addressW > 0 && x >= toolbarLayout.addressX && x < toolbarLayout.addressX + toolbarLayout.addressW &&
           y >= ADDRESS_Y && y < ADDRESS_Y + ADDRESS_H;
}

bool NavigatorApp::isFormBlock(const DocBlock& block) const
{
    return block.kind == BLOCK_FORM_TEXT ||
           block.kind == BLOCK_FORM_CHECKBOX ||
           block.kind == BLOCK_FORM_RADIO ||
           block.kind == BLOCK_FORM_TEXTAREA ||
           block.kind == BLOCK_FORM_SELECT ||
           block.kind == BLOCK_FORM_SUBMIT;
}

bool NavigatorApp::isFocusableFormBlock(const DocBlock& block) const
{
    return isFormBlock(block) && !block.disabled;
}

int NavigatorApp::formControlHeight(const DocBlock& block) const
{
    if (block.kind == BLOCK_FORM_TEXTAREA) {
        int rows = block.visibleRows > 0 ? block.visibleRows : 4;
        if (rows < 2) rows = 2;
        if (rows > 8) rows = 8;
        return rows * 16 + 10;
    }
    return 24;
}

void NavigatorApp::formControlRect(int blockIndex, int& x, int& y, int& w, int& h) const
{
    x = y = w = h = 0;
    if (!m_window || blockIndex < 0 || blockIndex >= m_blockCount || !isFormBlock(m_blocks[blockIndex])) return;
    int maxWidth = m_window->w - CONTENT_X * 2 - 32;
    if (maxWidth < 32) maxWidth = 32;
    x = CONTENT_X + 14 + css_margin_left_or(m_bodyStyle, 0) + css_margin_left_or(m_blocks[blockIndex].style, 0);
    y = blockY(blockIndex, maxWidth) + css_margin_top_or(m_blocks[blockIndex].style, 4);
    w = (m_blocks[blockIndex].kind == BLOCK_FORM_SUBMIT) ? 112 :
        ((m_blocks[blockIndex].kind == BLOCK_FORM_CHECKBOX || m_blocks[blockIndex].kind == BLOCK_FORM_RADIO) ? 260 : 320);
    h = formControlHeight(m_blocks[blockIndex]);
}

int NavigatorApp::hitFormBlockIndex(int x, int y) const
{
    for (int i = 0; i < m_blockCount; ++i) {
        if (!isFormBlock(m_blocks[i])) continue;
        int fx, fy, fw, fh;
        formControlRect(i, fx, fy, fw, fh);
        if (x >= fx && x < fx + fw && y >= fy && y < fy + fh) return i;
    }
    return -1;
}

void NavigatorApp::focusFormBlock(int blockIndex)
{
    if (blockIndex < 0 || blockIndex >= m_blockCount || !isFocusableFormBlock(m_blocks[blockIndex])) return;
    m_focusedFormBlock = blockIndex;
    m_formCaret = strlen_local(m_blocks[blockIndex].inputValue);
    clearSelection();
    setStatus("Form control focused");
}

void NavigatorApp::blurFormBlock()
{
    m_focusedFormBlock = -1;
    m_formCaret = 0;
}

void NavigatorApp::focusNextFormBlock()
{
    if (m_blockCount <= 0) return;
    int start = m_focusedFormBlock;
    if (start < 0 || start >= m_blockCount) start = m_blockCount - 1;
    for (int step = 1; step <= m_blockCount; ++step) {
        int index = (start + step) % m_blockCount;
        if (!isFocusableFormBlock(m_blocks[index])) continue;
        focusFormBlock(index);
        return;
    }
}

int NavigatorApp::blockHeight(const DocBlock& block, int maxChars) const
{
    int lines = navigatorWrappedLineCount(block.text, maxChars, block.style);
    if (block.kind == BLOCK_IMAGE) {
        int imageH = block.height > 0 ? block.height : (block.naturalHeight > 0 ? block.naturalHeight : 64);
        if (imageH > 420) imageH = 420;
        return css_margin_top_or(block.style, 4) + imageH + css_margin_bottom_or(block.style, 8);
    }
    if (isFormBlock(block)) {
        return css_margin_top_or(block.style, 4) + formControlHeight(block) + css_margin_bottom_or(block.style, 6);
    }
    int lineH = navigatorLineHeight(block.style);
    int boxPadding = block.kind == BLOCK_PREFORMATTED ? css_padding_or(block.style, 4) * 2 : 0;
    // Extra pre-gap before headings is accounted for in blockY() via the caller.
    return css_margin_top_or(block.style, block.kind == BLOCK_HEADING ? 10 : 4) + lines * lineH + boxPadding + css_margin_bottom_or(block.style, block.kind == BLOCK_LIST_ITEM ? 4 : 8);
}

bool NavigatorApp::isSelectableBlock(const DocBlock& block) const
{
    return block.kind == BLOCK_HEADING ||
           block.kind == BLOCK_PARAGRAPH ||
           block.kind == BLOCK_LINK ||
           block.kind == BLOCK_LIST_ITEM ||
           block.kind == BLOCK_PREFORMATTED;
}

void NavigatorApp::clearSelection()
{
    m_selectionActive = false;
    m_selectionDragging = false;
    m_selectionMoved = false;
    m_mouseLeftDown = false;
    m_mouseMode = NAV_MOUSE_NONE;
    m_mouseDownLinkIndex = -1;
    m_mouseDragThresholdExceeded = false;
    m_selectionAnchor.blockIndex = -1;
    m_selectionAnchor.offset = 0;
    m_selectionFocus.blockIndex = -1;
    m_selectionFocus.offset = 0;
}

bool NavigatorApp::hasSelection() const
{
    if (!m_selectionActive) return false;
    if (m_selectionAnchor.blockIndex < 0 || m_selectionFocus.blockIndex < 0) return false;
    return m_selectionAnchor.blockIndex != m_selectionFocus.blockIndex ||
           m_selectionAnchor.offset != m_selectionFocus.offset;
}

void NavigatorApp::blockTextForSelection(const DocBlock& block, char* out, int outSize) const
{
    if (!out || outSize <= 0) return;
    if (!isSelectableBlock(block)) {
        out[0] = '\0';
        return;
    }
    strcopy(out, block.text, outSize);
}

NavigatorApp::SelectionPosition NavigatorApp::textPositionFromPoint(int x, int y, bool clampToNearest) const
{
    SelectionPosition nearest{};
    nearest.blockIndex = -1;
    nearest.offset = 0;
    if (!m_window) return nearest;

    int maxWidth = m_window->w - CONTENT_X * 2 - 32;
    if (maxWidth < 32) maxWidth = 32;
    int nearestDistance = 1 << 30;
    int bodyMarginLeft = css_margin_left_or(m_bodyStyle, 0);
    for (int i = 0; i < m_blockCount; ++i) {
        if (!isSelectableBlock(m_blocks[i])) continue;
        char text[MAX_BLOCK_TEXT];
        blockTextForSelection(m_blocks[i], text, sizeof(text));
        int textLen = strlen_local(text);
        int blockMarginTop = css_margin_top_or(m_blocks[i].style, m_blocks[i].kind == BLOCK_HEADING ? 10 : 4);
        int blockMarginLeft = css_margin_left_or(m_blocks[i].style, 0);
        int blockPadding = css_padding_or(m_blocks[i].style, m_blocks[i].kind == BLOCK_PREFORMATTED ? 4 : 0);
        int lineH = navigatorLineHeight(m_blocks[i].style);
        int textX = CONTENT_X + 14 + bodyMarginLeft + blockMarginLeft + (m_blocks[i].kind == BLOCK_LIST_ITEM ? 14 : 0);
        int textY = blockY(i, maxWidth) + blockMarginTop + (m_blocks[i].kind == BLOCK_PREFORMATTED ? blockPadding : 0);
        int wrapWidth = maxWidth - (m_blocks[i].kind == BLOCK_LIST_ITEM ? 24 : 0);
        if (wrapWidth < 32) wrapWidth = 32;

        int localLineIndex = 0;
        bool foundInside = false;
        int bestOffset = 0;
        int parse = 0;
        while (parse <= textLen) {
            int lineEnd = parse;
            while (lineEnd < textLen && text[lineEnd] != '\n') ++lineEnd;
            if (lineEnd == parse) {
                int lineTop = textY + localLineIndex * lineH;
                if (y >= lineTop && y < lineTop + lineH) {
                    foundInside = true;
                    bestOffset = parse;
                    break;
                }
                ++localLineIndex;
            }
            int pos = parse;
            while (pos < lineEnd) {
                int breakAt = navigatorNextLineBreak(text, pos, lineEnd, wrapWidth, m_blocks[i].style);
                if (breakAt <= pos) breakAt = pos + 1;
                int lineTop = textY + localLineIndex * lineH;
                if (y >= lineTop && y < lineTop + lineH) {
                    int charOffset = navigatorLineCharOffset(text, pos, breakAt - pos, x - textX, m_blocks[i].style);
                    bestOffset = pos + charOffset;
                    foundInside = true;
                    break;
                }
                pos += breakAt;
                while (pos < lineEnd && text[pos] == ' ') ++pos;
                ++localLineIndex;
            }
            if (foundInside) break;
            if (lineEnd >= textLen) break;
            parse = lineEnd + 1;
        }

        int blockBottom = textY + (localLineIndex + 1) * lineH + css_margin_bottom_or(m_blocks[i].style, m_blocks[i].kind == BLOCK_LIST_ITEM ? 4 : 8);
        if (foundInside) {
            nearest.blockIndex = i;
            nearest.offset = bestOffset;
            return nearest;
        }

        if (clampToNearest) {
            int dx = 0;
            int minX = textX;
            int maxX = textX + wrapWidth;
            if (x < minX) dx = minX - x;
            else if (x > maxX) dx = x - maxX;
            int dy = 0;
            if (y < textY) dy = textY - y;
            else if (y > blockBottom) dy = y - blockBottom;
            int distance = dx + dy;
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest.blockIndex = i;
                nearest.offset = y < textY ? 0 : textLen;
            }
        }
    }
    return nearest;
}

void NavigatorApp::beginSelection(int x, int y)
{
    SelectionPosition pos = textPositionFromPoint(x, y, false);
    if (pos.blockIndex < 0) {
        clearSelection();
        return;
    }
    m_selectionAnchor = pos;
    m_selectionFocus = pos;
    m_selectionActive = true;
    m_selectionDragging = true;
    m_selectionMoved = false;
}

void NavigatorApp::updateSelection(int x, int y)
{
    if (!m_selectionDragging) return;
    SelectionPosition pos = textPositionFromPoint(x, y, true);
    if (pos.blockIndex < 0) return;
    if (pos.blockIndex != m_selectionFocus.blockIndex || pos.offset != m_selectionFocus.offset) {
        m_selectionFocus = pos;
        m_selectionMoved = true;
    }
}

void NavigatorApp::finalizeSelection(int x, int y)
{
    if (!m_selectionDragging) return;
    updateSelection(x, y);
    m_selectionDragging = false;
    if (!hasSelection() && !m_selectionMoved) {
        clearSelection();
    }
}

bool NavigatorApp::selectedText(char* out, int outSize) const
{
    if (!out || outSize <= 0) return false;
    out[0] = '\0';
    if (!hasSelection()) return false;

    SelectionPosition start = m_selectionAnchor;
    SelectionPosition end = m_selectionFocus;
    if (start.blockIndex > end.blockIndex ||
        (start.blockIndex == end.blockIndex && start.offset > end.offset)) {
        SelectionPosition tmp = start;
        start = end;
        end = tmp;
    }

    for (int i = start.blockIndex; i <= end.blockIndex; ++i) {
        if (i < 0 || i >= m_blockCount || !isSelectableBlock(m_blocks[i])) continue;
        char text[MAX_BLOCK_TEXT];
        blockTextForSelection(m_blocks[i], text, sizeof(text));
        int textLen = strlen_local(text);
        int begin = (i == start.blockIndex) ? start.offset : 0;
        int finish = (i == end.blockIndex) ? end.offset : textLen;
        if (begin < 0) begin = 0;
        if (finish > textLen) finish = textLen;
        if (finish < begin) {
            int tmp = begin;
            begin = finish;
            finish = tmp;
        }
        for (int j = begin; j < finish; ++j) {
            int len = strlen_local(out);
            if (len >= outSize - 1) break;
            out[len] = text[j];
            out[len + 1] = '\0';
        }
        if (i != end.blockIndex) strappend(out, "\n", outSize);
    }
    return out[0] != '\0';
}

bool NavigatorApp::copySelectionToClipboard()
{
    char text[MAX_SOURCE_PREVIEW];
    if (!selectedText(text, sizeof(text))) return false;
    strcopy(m_clipboard, text, sizeof(m_clipboard));
    strcopy(m_clipboardMode, "Navigator internal clipboard", sizeof(m_clipboardMode));
    return true;
}

void NavigatorApp::selectAllDocumentText()
{
    int first = -1;
    int last = -1;
    for (int i = 0; i < m_blockCount; ++i) {
        if (!isSelectableBlock(m_blocks[i])) continue;
        if (first < 0) first = i;
        last = i;
    }
    if (first < 0 || last < 0) {
        clearSelection();
        return;
    }
    char lastText[MAX_BLOCK_TEXT];
    blockTextForSelection(m_blocks[last], lastText, sizeof(lastText));
    m_selectionAnchor.blockIndex = first;
    m_selectionAnchor.offset = 0;
    m_selectionFocus.blockIndex = last;
    m_selectionFocus.offset = strlen_local(lastText);
    m_selectionActive = true;
    m_selectionDragging = false;
    m_selectionMoved = true;
}

int NavigatorApp::blockY(int index, int maxChars) const
{
    int y = CONTENT_Y + 12 + css_margin_top_or(m_bodyStyle, 0) - m_scrollY;
    for (int i = 0; i < index && i < m_blockCount; ++i) {
        y += blockHeight(m_blocks[i], maxChars);
        // Extra pre-gap before a heading that follows another block.
        if (i + 1 < index && i + 1 < m_blockCount &&
            m_blocks[i + 1].kind == BLOCK_HEADING) {
            y += 10;
        }
    }
    return y;
}

int NavigatorApp::hitLinkIndex(int x, int y) const
{
    if (!m_window) return -1;
    int maxWidth = m_window->w - CONTENT_X * 2 - 32;
    if (maxWidth < 32) maxWidth = 32;
    for (int i = 0; i < m_blockCount; ++i) {
        if (m_blocks[i].kind != BLOCK_LINK) continue;
        int by = blockY(i, maxWidth) + css_margin_top_or(m_blocks[i].style, 4);
        int h = blockHeight(m_blocks[i], maxWidth) - css_margin_top_or(m_blocks[i].style, 4);
        int tw = navigatorTextWidth(m_blocks[i].style, m_blocks[i].text);
        if (tw > maxWidth) tw = maxWidth;
        int left = CONTENT_X + 14 + css_margin_left_or(m_bodyStyle, 0) + css_margin_left_or(m_blocks[i].style, 0);
        if (x >= left && x < left + tw && y >= by && y < by + h) return i;
    }
    return -1;
}

void NavigatorApp::drawWrappedText(uint32_t x, uint32_t y, const char* text, uint32_t color, int maxWidth, int& outY, const gxos::web::WebStyle& style) const
{
    char line[96];
    int len = strlen_local(text);
    int yy = (int)y;
    if (maxWidth < 32) maxWidth = 32;

    // Split on embedded newlines first; then word-wrap each physical line.
    int lineStart = 0;
    while (lineStart <= len) {
        // Find end of this physical line (newline or string end)
        int lineEnd = lineStart;
        while (lineEnd < len && text[lineEnd] != '\n') ++lineEnd;

        // Word-wrap [lineStart, lineEnd)
        int pos = lineStart;
        int segLen = lineEnd - lineStart;
        if (segLen == 0) {
            // Blank physical line (e.g. empty line in <pre>)
            yy += navigatorLineHeight(style);
        }
        while (pos < lineEnd) {
            int breakAt = navigatorNextLineBreak(text, pos, lineEnd, maxWidth, style);
            if (breakAt <= pos) breakAt = pos + 1;
            int copyLen = breakAt < 95 ? breakAt : 95;
            for (int i = 0; i < copyLen; ++i) line[i] = text[pos + i];
            line[copyLen] = '\0';
            navigatorDrawText(x, (uint32_t)yy, line, color, style);
            yy += navigatorLineHeight(style);
            pos += breakAt;
            while (pos < lineEnd && text[pos] == ' ') ++pos;
        }

        if (lineEnd >= len) break;
        lineStart = lineEnd + 1; // skip '\n'
    }

    if (len == 0) yy += navigatorLineHeight(style);
    outY = yy;
}

void NavigatorApp::drawDocument(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    int maxWidth = (int)w - CONTENT_X * 2 - 32;
    if (maxWidth < 32) maxWidth = 32;
    int bottom = (int)h - STATUS_H - 8;
    int bodyMarginLeft = css_margin_left_or(m_bodyStyle, 0);
    for (int i = 0; i < m_blockCount; ++i) {
        int by = blockY(i, maxWidth);
        if (by > bottom) continue;
        if (by + blockHeight(m_blocks[i], maxWidth) < CONTENT_Y) continue;
        int absY = (int)y + by;
        int blockMarginTop = css_margin_top_or(m_blocks[i].style, m_blocks[i].kind == BLOCK_HEADING ? 10 : 4);
        int blockMarginLeft = css_margin_left_or(m_blocks[i].style, 0);
        int blockPadding = css_padding_or(m_blocks[i].style, m_blocks[i].kind == BLOCK_PREFORMATTED ? 4 : 0);
        uint32_t textX = x + CONTENT_X + 14 + bodyMarginLeft + blockMarginLeft;
        if (m_selectionActive && m_selectionAnchor.blockIndex >= 0 && m_selectionFocus.blockIndex >= 0 && isSelectableBlock(m_blocks[i])) {
            int startBlock = m_selectionAnchor.blockIndex;
            int endBlock = m_selectionFocus.blockIndex;
            if (startBlock > endBlock) {
                int tmp = startBlock;
                startBlock = endBlock;
                endBlock = tmp;
            }
            if (i >= startBlock && i <= endBlock) {
                int highlightX = (int)textX - 2;
                int highlightY = absY + blockMarginTop - 1;
                int highlightW = (int)w - CONTENT_X * 2 - 32;
                if (m_blocks[i].kind == BLOCK_LIST_ITEM) highlightX += 14;
                if (highlightW > 0) {
                    framebuffer::fill_rect((uint32_t)highlightX, (uint32_t)highlightY, (uint32_t)highlightW, (uint32_t)(blockHeight(m_blocks[i], maxWidth) - css_margin_bottom_or(m_blocks[i].style, m_blocks[i].kind == BLOCK_LIST_ITEM ? 4 : 8)), rgb(110, 150, 220));
                }
            }
        }
        if (m_blocks[i].kind == BLOCK_HEADING) {
            // Bold-looking heading: draw in a deep navy color, then a 2px accent bar.
            uint32_t color = css_color_or(rgb(22, 32, 52), m_blocks[i].style);
            navigatorDrawText(textX, (uint32_t)(absY + blockMarginTop), m_blocks[i].text, color, m_blocks[i].style);
            int barW = (int)w - CONTENT_X * 2 - 32;
            framebuffer::fill_rect(textX, (uint32_t)(absY + blockMarginTop + navigatorLineHeight(m_blocks[i].style) + 2), (uint32_t)(barW > 0 ? barW : 1), 2, rgb(55, 110, 200));
        } else if (m_blocks[i].kind == BLOCK_LINK) {
            uint32_t color = css_color_or(i == m_hoverLinkIndex ? rgb(10, 84, 160) : rgb(30, 92, 184), m_blocks[i].style);
            int linkOutY = absY + blockMarginTop;
            drawWrappedText(textX, (uint32_t)(absY + blockMarginTop), m_blocks[i].text, color, maxWidth, linkOutY, m_blocks[i].style);
            // Underline each rendered line.
            if (m_blocks[i].style.underline) {
                for (int ly = absY + blockMarginTop + navigatorLineHeight(m_blocks[i].style) - 2; ly < linkOutY; ly += navigatorLineHeight(m_blocks[i].style)) {
                    int lineWidth = navigatorTextWidth(m_blocks[i].style, m_blocks[i].text);
                    if (lineWidth > maxWidth) lineWidth = maxWidth;
                    framebuffer::fill_rect(textX, (uint32_t)ly, (uint32_t)lineWidth, 1, color);
                }
            }
        } else if (m_blocks[i].kind == BLOCK_LIST_ITEM) {
            uint32_t color = css_color_or(rgb(54, 60, 72), m_blocks[i].style);
            navigatorDrawText(textX, (uint32_t)(absY + blockMarginTop), "-", rgb(72, 78, 92), m_blocks[i].style);
            int outY = absY + blockMarginTop;
            drawWrappedText(textX + 14, (uint32_t)(absY + blockMarginTop), m_blocks[i].text, color, maxWidth - 24, outY, m_blocks[i].style);
        } else if (m_blocks[i].kind == BLOCK_PREFORMATTED) {
            // Light box background for preformatted blocks.
            int preH = blockHeight(m_blocks[i], maxWidth) - css_margin_top_or(m_blocks[i].style, 4) - css_margin_bottom_or(m_blocks[i].style, 8);
            int boxW = (int)w - CONTENT_X * 2 - 28;
            if (boxW > 0 && preH > 0)
                framebuffer::fill_rect(textX - 4, (uint32_t)(absY + blockMarginTop - 2), (uint32_t)(boxW), (uint32_t)(preH + blockPadding * 2), css_background_or(rgb(230, 232, 238), m_blocks[i].style));
            int outY = absY + blockMarginTop + blockPadding;
            drawWrappedText(textX, (uint32_t)(absY + blockMarginTop + blockPadding), m_blocks[i].text, css_color_or(rgb(40, 50, 68), m_blocks[i].style), maxWidth, outY, m_blocks[i].style);
        } else if (isFormBlock(m_blocks[i])) {
            DocBlock& block = m_blocks[i];
            int controlX = (int)textX;
            int controlY = absY + blockMarginTop;
            int controlW = block.kind == BLOCK_FORM_SUBMIT ? 112 :
                ((block.kind == BLOCK_FORM_CHECKBOX || block.kind == BLOCK_FORM_RADIO) ? 260 : 320);
            int controlH = formControlHeight(block);
            bool focused = i == m_focusedFormBlock;
            uint32_t border = focused ? rgb(54, 118, 210) : rgb(148, 156, 170);
            if (block.disabled || block.formUnsupported) border = rgb(142, 146, 154);

            if (block.kind == BLOCK_FORM_CHECKBOX || block.kind == BLOCK_FORM_RADIO) {
                int box = 14;
                int boxY = controlY + (controlH - box) / 2;
                framebuffer::fill_rect((uint32_t)controlX, (uint32_t)boxY, box, box, rgb(248, 250, 254));
                framebuffer::fill_rect((uint32_t)controlX, (uint32_t)boxY, box, 1, border);
                framebuffer::fill_rect((uint32_t)controlX, (uint32_t)(boxY + box - 1), box, 1, border);
                framebuffer::fill_rect((uint32_t)controlX, (uint32_t)boxY, 1, box, border);
                framebuffer::fill_rect((uint32_t)(controlX + box - 1), (uint32_t)boxY, 1, box, border);
                if (block.checked) {
                    if (block.kind == BLOCK_FORM_RADIO) {
                        framebuffer::fill_rect((uint32_t)(controlX + 4), (uint32_t)(boxY + 4), box - 8, box - 8, rgb(45, 94, 170));
                    } else {
                        appDrawText((uint32_t)(controlX + 3), (uint32_t)(boxY - 2), "x", rgb(35, 85, 170));
                    }
                }
                appDrawText((uint32_t)(controlX + box + 8), (uint32_t)(controlY + 7),
                            block.text[0] ? block.text : block.inputName, rgb(35, 45, 60));
            } else {
                uint32_t fill = block.kind == BLOCK_FORM_SUBMIT
                    ? ((block.disabled || block.formUnsupported) ? rgb(184, 188, 196) : rgb(65, 112, 190))
                    : rgb(250, 252, 255);
                framebuffer::fill_rect((uint32_t)controlX, (uint32_t)controlY, (uint32_t)controlW, (uint32_t)controlH, fill);
                framebuffer::fill_rect((uint32_t)controlX, (uint32_t)controlY, (uint32_t)controlW, 1, border);
                framebuffer::fill_rect((uint32_t)controlX, (uint32_t)(controlY + controlH - 1), (uint32_t)controlW, 1, border);
                framebuffer::fill_rect((uint32_t)controlX, (uint32_t)controlY, 1, (uint32_t)controlH, border);
                framebuffer::fill_rect((uint32_t)(controlX + controlW - 1), (uint32_t)controlY, 1, (uint32_t)controlH, border);

                if (block.kind == BLOCK_FORM_SUBMIT) {
                    appDrawText((uint32_t)(controlX + 10), (uint32_t)(controlY + 7),
                                block.submitLabel[0] ? block.submitLabel : "Submit", rgb(255, 255, 255));
                } else if (block.kind == BLOCK_FORM_SELECT) {
                    appDrawText((uint32_t)(controlX + 8), (uint32_t)(controlY + 7),
                                block.text[0] ? block.text : "(select)", rgb(35, 45, 60));
                    appDrawText((uint32_t)(controlX + controlW - 20), (uint32_t)(controlY + 7), "v", rgb(70, 78, 96));
                } else if (block.kind == BLOCK_FORM_TEXTAREA) {
                    const char* value = block.inputValue[0] ? block.inputValue : block.placeholder;
                    uint32_t color = block.inputValue[0] ? rgb(35, 45, 60) : rgb(128, 136, 150);
                    char line[52];
                    int used = 0;
                    int lineY = controlY + 6;
                    int rows = (controlH - 10) / 16;
                    for (const char* ch = value; ; ++ch) {
                        if (*ch == '\n' || *ch == '\0' || used >= 50) {
                            line[used] = '\0';
                            appDrawText((uint32_t)(controlX + 8), (uint32_t)lineY, line, color);
                            lineY += 16;
                            used = 0;
                            if (*ch == '\0' || --rows <= 0) break;
                            if (*ch != '\n') --ch;
                        } else {
                            line[used++] = *ch;
                        }
                    }
                } else {
                    const char* value = block.inputValue[0] ? block.inputValue : block.placeholder;
                    appDrawText((uint32_t)(controlX + 8), (uint32_t)(controlY + 7), value,
                                block.inputValue[0] ? rgb(35, 45, 60) : rgb(128, 136, 150));
                }

                if (focused && (block.kind == BLOCK_FORM_TEXT || block.kind == BLOCK_FORM_TEXTAREA)) {
                    int caretColumn = 0;
                    int caretRow = 0;
                    for (int ci = 0; ci < m_formCaret && block.inputValue[ci]; ++ci) {
                        if (block.inputValue[ci] == '\n') {
                            ++caretRow;
                            caretColumn = 0;
                        } else {
                            ++caretColumn;
                        }
                    }
                    if (caretColumn > 50) caretColumn = 50;
                    framebuffer::fill_rect((uint32_t)(controlX + 8 + caretColumn * 6),
                                           (uint32_t)(controlY + 5 + caretRow * 16), 1, 14,
                                           rgb(35, 85, 170));
                }
            }
        } else if (m_blocks[i].kind == BLOCK_IMAGE) {
            int imageW = m_blocks[i].width > 0 ? m_blocks[i].width : (m_blocks[i].naturalWidth > 0 ? m_blocks[i].naturalWidth : 220);
            int imageH = m_blocks[i].height > 0 ? m_blocks[i].height : (m_blocks[i].naturalHeight > 0 ? m_blocks[i].naturalHeight : 64);
            int maxW = (int)w - CONTENT_X * 2 - 36;
            if (maxW < 40) maxW = 40;
            if (imageW > maxW) imageW = maxW;
            if (imageH > 420) imageH = 420;
            int contentBottom = (int)h - STATUS_H - 8;
            if (absY + blockMarginTop >= CONTENT_Y && absY + blockMarginTop + imageH <= contentBottom) {
                gxos::gui::ImageBitmap bitmap{};
                bitmap.status = (gxos::gui::ImageLoadStatus)m_blocks[i].imageStatus;
                bitmap.pixels = m_blocks[i].imagePixels;
                bitmap.width = (uint32_t)m_blocks[i].naturalWidth;
                bitmap.height = (uint32_t)m_blocks[i].naturalHeight;
                bool drew = gxos::gui::ImageAdapter::DrawToFramebuffer(bitmap, textX, (uint32_t)(absY + blockMarginTop), (uint32_t)imageW, (uint32_t)imageH);
                if (!drew) {
                    framebuffer::fill_rect(textX, (uint32_t)(absY + blockMarginTop), (uint32_t)imageW, (uint32_t)imageH, rgb(232, 236, 242));
                    framebuffer::fill_rect(textX, (uint32_t)(absY + blockMarginTop), (uint32_t)imageW, 1, rgb(145, 153, 168));
                    framebuffer::fill_rect(textX, (uint32_t)(absY + blockMarginTop + imageH - 1), (uint32_t)imageW, 1, rgb(145, 153, 168));
                    framebuffer::fill_rect(textX, (uint32_t)(absY + blockMarginTop), 1, (uint32_t)imageH, rgb(145, 153, 168));
                    framebuffer::fill_rect(textX + (uint32_t)imageW - 1, (uint32_t)(absY + blockMarginTop), 1, (uint32_t)imageH, rgb(145, 153, 168));
                    gxos::gui::ImageLoadStatus status = bitmap.status != gxos::gui::ImageLoadStatus::Ok ? bitmap.status : (gxos::gui::ImageLoadStatus)m_blocks[i].imageStatus;
                    const char* label = m_blocks[i].alt[0] ? m_blocks[i].alt : gxos::gui::ImageLoadStatusName(status);
                    appDrawText(textX + 8, (uint32_t)(absY + blockMarginTop + 8), label, rgb(54, 60, 72));
                }
            }
        } else {
            int outY = absY + blockMarginTop;
            drawWrappedText(textX, (uint32_t)(absY + blockMarginTop), m_blocks[i].text, css_color_or(rgb(54, 60, 72), m_blocks[i].style), maxWidth, outY, m_blocks[i].style);
        }
    }
}

int NavigatorApp::maxScroll() const
{
    int visible = m_window ? ((int)m_window->h - TOOLBAR_H - STATUS_H - 12) : 0;
    int maxWidth = m_window ? ((m_window->w - CONTENT_X * 2 - 32)) : 480;
    if (maxWidth < 32) maxWidth = 32;
    int docHeight = 24;
    for (int i = 0; i < m_blockCount; ++i) {
        docHeight += blockHeight(m_blocks[i], maxWidth);
        // Match the pre-gap added in blockY.
        if (i + 1 < m_blockCount && m_blocks[i + 1].kind == BLOCK_HEADING)
            docHeight += 10;
    }
    int overflow = docHeight - visible;
    return overflow > 0 ? overflow : 0;
}

void NavigatorApp::clampScroll()
{
    int maximum = maxScroll();
    if (m_scrollY < 0) m_scrollY = 0;
    if (m_scrollY > maximum) m_scrollY = maximum;
}
// ============================================================
// App Registration
// ============================================================

void registerKernelApps() {
    app::AppManager::init();
    app::AppLogger::init();

    // Register available kernel-mode apps from the shared built-in metadata table.
    // This remains metadata-only for now; factory selection and launch dispatch stay
    // in the existing kernel app framework until a later pass.
    for (size_t i = 0; i < gxos::apps::kBuiltInAppMetadataCount; ++i) {
        const gxos::apps::BuiltInAppMetadata& metadata = gxos::apps::kBuiltInAppMetadata[i];
        if (!gxos::apps::IsBuiltInAppAvailableInBareMetal(metadata) || !metadata.kernelAppName) continue;

        app::KernelApp* (*factory)() = nullptr;
        if (gxos::apps::detail::builtInTextEquals(metadata.kernelAppName, "Notepad")) factory = NotepadApp::create;
        else if (gxos::apps::detail::builtInTextEquals(metadata.kernelAppName, "Calculator")) factory = CalculatorApp::create;
        else if (gxos::apps::detail::builtInTextEquals(metadata.kernelAppName, "Clock")) factory = ClockApp::create;
        else if (gxos::apps::detail::builtInTextEquals(metadata.kernelAppName, "DisplayOptions")) factory = DisplayOptionsApp::create;
        else if (gxos::apps::detail::builtInTextEquals(metadata.kernelAppName, "TaskManager")) factory = TaskManagerApp::create;
        // File Explorer is intentionally split across two launch names:
        // hosted/compositor uses "FileExplorer", while bare-metal registers
        // the kernel-side app as "Files". Accept either spelling so the
        // kernel app stays reachable from the desktop icon and Start menu.
        else if (gxos::apps::detail::builtInTextEquals(metadata.kernelAppName, "Files") ||
                 gxos::apps::detail::builtInTextEquals(metadata.kernelAppName, "FileExplorer") ||
                 gxos::apps::detail::builtInTextEquals(metadata.launchName, "FileExplorer")) factory = FileExplorerApp::create;
        else if (gxos::apps::detail::builtInTextEquals(metadata.kernelAppName, "ImageViewer")) factory = ImageViewerApp::create;
        else if (gxos::apps::detail::builtInTextEquals(metadata.kernelAppName, "guideXOS Navigator")) factory = NavigatorApp::create;
        else if (gxos::apps::detail::builtInTextEquals(metadata.kernelAppName, "Trash")) factory = TrashApp::create;
        else if (gxos::apps::detail::builtInTextEquals(metadata.kernelAppName, "DiskManager")) factory = DiskManagerApp::create;

        if (!factory) continue;
        app::AppManager::registerApp(metadata.kernelAppName, metadata.kernelIconColor, factory);
        if (metadata.kernelLegacyAlias && metadata.kernelLegacyAlias[0]) {
            app::AppManager::registerApp(metadata.kernelLegacyAlias, metadata.kernelIconColor, factory);
        }
    }
}

static bool nav_smoke_text_equals(const char* a, const char* b)
{
    if (!a || !b) return false;
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return false;
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static bool nav_smoke_text_equals_insensitive(const char* a, const char* b)
{
    return gxos::web::httpSharedEqualsInsensitive(a, b);
}

static void nav_smoke_copy_trimmed_ascii_text(const char* text, char* out, int outSize, bool lowerCase)
{
    if (!out || outSize <= 0) return;
    out[0] = '\0';
    if (!text) return;
    int start = 0;
    while (text[start] && gxos::web::httpSharedIsSpace(text[start])) ++start;
    int end = start;
    while (text[end]) ++end;
    while (end > start && gxos::web::httpSharedIsSpace(text[end - 1])) --end;
    int oi = 0;
    for (int i = start; i < end && oi < outSize - 1; ++i) {
        char ch = text[i];
        out[oi++] = lowerCase && ch >= 'A' && ch <= 'Z'
            ? static_cast<char>(ch - 'A' + 'a')
            : ch;
    }
    out[oi] = '\0';
}

static bool nav_smoke_read_vfs_text_file(const char* primaryPath,
                                         const char* compatPath,
                                         char* out,
                                         int outSize,
                                         bool lowerCase)
{
    if (!out || outSize <= 1 || !primaryPath || !primaryPath[0]) return false;
    out[0] = '\0';

    kernel::vfs::FileInfo info{};
    const char* readPath = primaryPath;
    kernel::vfs::Status status = kernel::vfs::stat(primaryPath, &info);
    if ((status == kernel::vfs::VFS_ERR_NOT_FOUND || status == kernel::vfs::VFS_ERR_NOT_MOUNT) &&
        compatPath && compatPath[0]) {
        status = kernel::vfs::stat(compatPath, &info);
        if (status == kernel::vfs::VFS_OK) {
            readPath = compatPath;
        }
    }
    if (status != kernel::vfs::VFS_OK || info.type != kernel::vfs::FILE_TYPE_REGULAR ||
        info.size == 0 || info.size > kNavigatorSmokeTextFileMaxBytes) {
        return false;
    }

    char buffer[kNavigatorSmokeTextFileMaxBytes + 1];
    const int32_t bytesRead = kernel::vfs::read_file(readPath,
        reinterpret_cast<uint8_t*>(buffer), kNavigatorSmokeTextFileMaxBytes);
    if (bytesRead <= 0) return false;
    buffer[bytesRead < static_cast<int32_t>(kNavigatorSmokeTextFileMaxBytes)
        ? bytesRead
        : static_cast<int32_t>(kNavigatorSmokeTextFileMaxBytes)] = '\0';
    nav_smoke_copy_trimmed_ascii_text(buffer, out, outSize, lowerCase);
    return out[0] != '\0';
}

static bool nav_smoke_read_vfs_token_file(const char* primaryPath,
                                          const char* compatPath,
                                          char* out,
                                          int outSize)
{
    return nav_smoke_read_vfs_text_file(primaryPath, compatPath, out, outSize, true);
}

static bool nav_smoke_read_vfs_uint32_file(const char* primaryPath,
                                           const char* compatPath,
                                           uint32_t* out)
{
    if (!out) return false;
    *out = 0;

    char token[32];
    if (!nav_smoke_read_vfs_text_file(primaryPath, compatPath, token, sizeof(token), false)) {
        return false;
    }

    uint64_t value = 0;
    int digits = 0;
    for (const char* p = token; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        value = value * 10u + static_cast<uint64_t>(*p - '0');
        if (value > 0xffffffffull) return false;
        ++digits;
    }
    if (digits == 0) return false;
    *out = static_cast<uint32_t>(value);
    return true;
}

static NavigatorHttpsSmokeFaultMode navigator_https_smoke_fault_mode()
{
    static bool initialized = false;
    static NavigatorHttpsSmokeFaultMode mode = NavigatorHttpsSmokeFaultMode::None;
    if (initialized) return mode;
    initialized = true;

    char token[64];
    if (!nav_smoke_read_vfs_token_file(kNavigatorHttpsSmokeFaultModePath,
            kNavigatorHttpsSmokeFaultModeCompatPath, token, sizeof(token))) {
        return mode;
    }

    if (nav_smoke_text_equals_insensitive(token, "untrusted-root") ||
        nav_smoke_text_equals_insensitive(token, "untrustedroot")) {
        mode = NavigatorHttpsSmokeFaultMode::UntrustedRoot;
    } else if (nav_smoke_text_equals_insensitive(token, "expired-cert") ||
               nav_smoke_text_equals_insensitive(token, "expired-certificate") ||
               nav_smoke_text_equals_insensitive(token, "expiredcertificate")) {
        mode = NavigatorHttpsSmokeFaultMode::ExpiredCertificate;
    } else if (nav_smoke_text_equals_insensitive(token, "future-cert") ||
               nav_smoke_text_equals_insensitive(token, "future-certificate") ||
               nav_smoke_text_equals_insensitive(token, "futurecertificate") ||
               nav_smoke_text_equals_insensitive(token, "not-yet-valid")) {
        mode = NavigatorHttpsSmokeFaultMode::FutureCertificate;
    }
    return mode;
}

static const char* navigator_https_smoke_fault_mode_name(NavigatorHttpsSmokeFaultMode mode)
{
    switch (mode) {
    case NavigatorHttpsSmokeFaultMode::UntrustedRoot: return "UntrustedRoot";
    case NavigatorHttpsSmokeFaultMode::ExpiredCertificate: return "ExpiredCertificate";
    case NavigatorHttpsSmokeFaultMode::FutureCertificate: return "FutureCertificate";
    case NavigatorHttpsSmokeFaultMode::None:
    default:
        return "None";
    }
}

struct NavigatorRealPublicProbeConfig {
    bool enabled;
    bool requireSuccess;
    bool targetValid;
    bool publicCaOptInEnabled;
    bool reviewedOverrideEnabled;
    bool reviewedTargetMatched;
    char targetUrl[160];
    char targetError[128];
    char reviewedTargetPolicy[40];
    char reviewedTargetReason[160];
    char publicCaSourcePath[260];
    uint32_t publicCaBytes;
    uint32_t publicCaParsedCertCount;
};

struct NavigatorReviewedPublicTarget {
    const char* url;
    const char* host;
    uint16_t port;
    const char* path;
    const char* reason;
};

static const NavigatorReviewedPublicTarget kNavigatorReviewedPublicTargets[] = {
    {
        "https://sha256.badssl.com/",
        "sha256.badssl.com",
        443,
        "/",
        "Stable badssl DNS-hosted HTTPS endpoint used to prove real-world DNS, TCP, TLS, certificate, and hostname validation without enabling arbitrary public browsing."
    },
    {
        "https://example.com/",
        "example.com",
        443,
        "/",
        "IANA example HTTPS page used as the first real HTML/Navigator public page target."
    },
    {
        "https://www.gnu.org/",
        "www.gnu.org",
        443,
        "/",
        "GNU HTTPS homepage retained as a reviewed public HTML target for the post-example.com navigation sequence."
    },
    {
        "https://news.ycombinator.com/",
        "news.ycombinator.com",
        443,
        "/",
        "Hacker News HTTPS homepage retained as a reviewed public HTML target for the post-example.com navigation sequence."
    },
    {
        "https://en.wikipedia.org/",
        "en.wikipedia.org",
        443,
        "/",
        "English Wikipedia HTTPS homepage retained as a reviewed public HTML target for the post-example.com navigation sequence."
    }
};

static const NavigatorReviewedPublicTarget* navigator_find_reviewed_public_target(const KernelHttpUrl& parsed)
{
    for (const NavigatorReviewedPublicTarget& target : kNavigatorReviewedPublicTargets) {
        if (!nav_smoke_text_equals_insensitive(parsed.host, target.host)) continue;
        if (parsed.port != target.port) continue;
        if (!nav_smoke_text_equals(parsed.path, target.path)) continue;
        return &target;
    }
    return nullptr;
}

static NavigatorRealPublicProbeConfig navigator_real_public_probe_config()
{
    static bool initialized = false;
    static NavigatorRealPublicProbeConfig config{};
    if (initialized) return config;
    initialized = true;

    strcopy(config.targetUrl, kNavigatorRealPublicProbeDefaultTarget, sizeof(config.targetUrl));
    config.targetValid = true;

    char target[160];
    if (nav_smoke_read_vfs_text_file(kNavigatorRealPublicProbeTargetPath,
            kNavigatorRealPublicProbeTargetCompatPath, target, sizeof(target), false)) {
        strcopy(config.targetUrl, target, sizeof(config.targetUrl));
        config.enabled = true;
    }

    char requireToken[32];
    if (nav_smoke_read_vfs_token_file(kNavigatorRealPublicProbeRequirePath,
            kNavigatorRealPublicProbeRequireCompatPath, requireToken, sizeof(requireToken))) {
        config.requireSuccess =
            nav_smoke_text_equals_insensitive(requireToken, "1") ||
            nav_smoke_text_equals_insensitive(requireToken, "true") ||
            nav_smoke_text_equals_insensitive(requireToken, "yes") ||
            nav_smoke_text_equals_insensitive(requireToken, "required") ||
            nav_smoke_text_equals_insensitive(requireToken, "enabled");
    }

    char enabledToken[32];
    if (nav_smoke_read_vfs_token_file(kNavigatorRealPublicProbeCaEnabledPath,
            kNavigatorRealPublicProbeCaEnabledCompatPath, enabledToken, sizeof(enabledToken))) {
        config.publicCaOptInEnabled =
            nav_smoke_text_equals_insensitive(enabledToken, "1") ||
            nav_smoke_text_equals_insensitive(enabledToken, "true") ||
            nav_smoke_text_equals_insensitive(enabledToken, "yes") ||
            nav_smoke_text_equals_insensitive(enabledToken, "enabled");
    }
    char reviewedOverrideToken[32];
    if (nav_smoke_read_vfs_token_file(kNavigatorRealPublicProbeReviewedOverridePath,
            kNavigatorRealPublicProbeReviewedOverrideCompatPath,
            reviewedOverrideToken,
            sizeof(reviewedOverrideToken))) {
        config.reviewedOverrideEnabled =
            nav_smoke_text_equals_insensitive(reviewedOverrideToken, "1") ||
            nav_smoke_text_equals_insensitive(reviewedOverrideToken, "true") ||
            nav_smoke_text_equals_insensitive(reviewedOverrideToken, "yes") ||
            nav_smoke_text_equals_insensitive(reviewedOverrideToken, "enabled") ||
            nav_smoke_text_equals_insensitive(reviewedOverrideToken, "required");
    }
    nav_smoke_read_vfs_text_file(kNavigatorRealPublicProbeCaSourcePath,
        kNavigatorRealPublicProbeCaSourceCompatPath,
        config.publicCaSourcePath,
        sizeof(config.publicCaSourcePath),
        false);
    nav_smoke_read_vfs_uint32_file(kNavigatorRealPublicProbeCaBytesPath,
        kNavigatorRealPublicProbeCaBytesCompatPath,
        &config.publicCaBytes);
    nav_smoke_read_vfs_uint32_file(kNavigatorRealPublicProbeCaCertsPath,
        kNavigatorRealPublicProbeCaCertsCompatPath,
        &config.publicCaParsedCertCount);

    if (config.requireSuccess) {
        config.enabled = true;
    }

    KernelHttpUrl parsed{};
    if (!parse_https_url_kernel(config.targetUrl, &parsed)) {
        strcopy(config.targetError, "Real public HTTPS probe target is invalid: ", sizeof(config.targetError));
        strappend(config.targetError, parsed.error[0] ? parsed.error : "parse failure", sizeof(config.targetError));
        config.targetValid = false;
        strcopy(config.reviewedTargetPolicy, "rejected", sizeof(config.reviewedTargetPolicy));
        strcopy(config.reviewedTargetReason,
            "The requested target could not be parsed as a reviewed HTTPS target.",
            sizeof(config.reviewedTargetReason));
    } else if (parsed.hostIsNumeric) {
        strcopy(config.targetError,
            "Real public HTTPS probe target requires a DNS hostname, not a numeric IP literal.",
            sizeof(config.targetError));
        config.targetValid = false;
        strcopy(config.reviewedTargetPolicy, "rejected", sizeof(config.reviewedTargetPolicy));
        strcopy(config.reviewedTargetReason,
            "Numeric IP literals are never approved for the reviewed public HTTPS probe.",
            sizeof(config.reviewedTargetReason));
    } else {
        const NavigatorReviewedPublicTarget* reviewedTarget = navigator_find_reviewed_public_target(parsed);
        if (reviewedTarget) {
            config.reviewedTargetMatched = true;
            strcopy(config.reviewedTargetPolicy, "reviewed-allowlist", sizeof(config.reviewedTargetPolicy));
            strcopy(config.reviewedTargetReason, reviewedTarget->reason, sizeof(config.reviewedTargetReason));
        } else if (config.reviewedOverrideEnabled) {
            strcopy(config.reviewedTargetPolicy, "explicit-reviewed-override", sizeof(config.reviewedTargetPolicy));
            strcopy(config.reviewedTargetReason,
                "Accepted only because an explicit reviewed target override was staged for this one-off public proof run.",
                sizeof(config.reviewedTargetReason));
        } else {
            strcopy(config.targetError,
                "Real public HTTPS probe target is outside the reviewed allowlist and no explicit reviewed override was staged.",
                sizeof(config.targetError));
            config.targetValid = false;
            strcopy(config.reviewedTargetPolicy, "rejected", sizeof(config.reviewedTargetPolicy));
            strcopy(config.reviewedTargetReason,
                "The requested target is outside the reviewed public HTTPS allowlist for v0.5.",
                sizeof(config.reviewedTargetReason));
        }
    }

    return config;
}

static bool navigator_real_public_probe_environment_blocked(const char* error,
                                                            const char* tlsStatus,
                                                            const char* dnsError)
{
    if (dnsError && dnsError[0]) return true;
    if (nav_smoke_text_equals(tlsStatus, "TcpConnectFailed")) return true;
    if (!error || !error[0]) return false;

    return nav_smoke_text_equals(error, "Network unavailable") ||
        nav_smoke_text_equals(error, "TCP connect failed") ||
        nav_smoke_text_equals(error, "TCP connect timeout") ||
        nav_smoke_text_equals(error, "connection refused") ||
        nav_smoke_text_equals(error, "connection reset") ||
        nav_smoke_text_equals(error, "network down");
}

static const char* navigator_real_public_probe_prerequisite_blocker(
    const gxos::GxosValidatedHttpsPolicyInfo& httpsPolicy,
    const gxos::GxosTrustStorePolicyInfo& trustStorePolicy,
    bool pilotEnabled)
{
    if (!pilotEnabled) {
        return
            httpsPolicy.publicHttpsPilotReason && httpsPolicy.publicHttpsPilotReason[0]
                ? httpsPolicy.publicHttpsPilotReason
                : "Real public HTTPS probe requires ProductionValidated trust prerequisites.";
    }

    if (trustStorePolicy.state != gxos::GxosTrustStorePolicyState::TrustStoreParsed) {
        return
            trustStorePolicy.error && trustStorePolicy.error[0]
                ? trustStorePolicy.error
                : "Real public HTTPS probe requires a parsed production CA bundle.";
    }

    if (trustStorePolicy.source != gxos::GxosTrustStoreSource::ProductionPublicProbeTrust) {
        return "Real public HTTPS probe requires a production CA bundle at /certs/ca-bundle.pem.";
    }

    if (!trustStorePolicy.publicInternetReady) {
        return
            "Real public HTTPS probe requires an opt-in public-root production CA bundle; deterministic validated fixture trust is not public internet trust.";
    }

    return nullptr;
}

static bool navigator_https_policy_state_enabled(gxos::GxosValidatedHttpsPolicyState state)
{
    return state == gxos::GxosValidatedHttpsPolicyState::UserTrustStoreDevMode ||
        state == gxos::GxosValidatedHttpsPolicyState::ProductionValidated;
}

static bool navigator_https_policy_effectively_enabled(const gxos::GxosValidatedHttpsPolicyInfo& info)
{
    return navigator_https_policy_state_enabled(info.state);
}

static bool navigator_https_policy_selected(const gxos::GxosValidatedHttpsPolicyInfo& info)
{
    return navigator_https_policy_state_enabled(info.selectedState);
}

static bool navigator_https_policy_selected_but_blocked(const gxos::GxosValidatedHttpsPolicyInfo& info)
{
    return navigator_https_policy_selected(info) &&
        !navigator_https_policy_effectively_enabled(info) &&
        info.blocker && info.blocker[0];
}

static bool navigator_https_cert_fault_expected(NavigatorHttpsSmokeFaultMode mode)
{
    return mode == NavigatorHttpsSmokeFaultMode::UntrustedRoot ||
        mode == NavigatorHttpsSmokeFaultMode::ExpiredCertificate ||
        mode == NavigatorHttpsSmokeFaultMode::FutureCertificate;
}

static const char* navigator_https_policy_host_for_info(const gxos::GxosValidatedHttpsPolicyInfo& info)
{
    if (info.selectedState == gxos::GxosValidatedHttpsPolicyState::ProductionValidated ||
        info.state == gxos::GxosValidatedHttpsPolicyState::ProductionValidated) {
        return kNavigatorPolicyProdHttpsHost;
    }
    return kNavigatorPolicyDevHttpsHost;
}

static void navigator_https_policy_url(const gxos::GxosValidatedHttpsPolicyInfo& info,
                                       const char* path,
                                       char* out,
                                       int outSize)
{
    if (!out || outSize <= 0) return;
    strcopy(out, "https://", outSize);
    strappend(out, navigator_https_policy_host_for_info(info), outSize);
    strappend(out, ":8443", outSize);
    strappend(out, path, outSize);
}

static void navigator_public_pilot_fixture_url(char* out, int outSize, const char* path)
{
    if (!out || outSize <= 0) return;
    strcopy(out, "https://", outSize);
    strappend(out, kNavigatorPublicPilotHttpsHost, outSize);
    strappend(out, ":8443", outSize);
    strappend(out, path ? path : kNavigatorPublicPilotPathPrefix, outSize);
}

void NavigatorApp::smokeCaptureHttpsNavigation(const char* url,
                                               char* requestedUrl, int requestedUrlLen,
                                               int* statusCode,
                                               char* contentType, int contentTypeLen,
                                               int* bodyBytes,
                                               int* parsedBlocks,
                                               char* error, int errorLen,
                                               char* finalUrl, int finalUrlLen,
                                               int* redirectCount,
                                               int* plainTcpConnectAttempts,
                                               int* tlsTcpConnectAttempts,
                                               uint32_t* tlsVerifyFlags,
                                               char* tlsSniHost, int tlsSniHostLen,
                                               char* tlsProtocol, int tlsProtocolLen,
                                               char* tlsCipherSuite, int tlsCipherSuiteLen,
                                               char* transportSelection, int transportSelectionLen,
                                               char* tlsStatus, int tlsStatusLen,
                                               bool* tlsValidated,
                                               bool* tlsHostnameValidated,
                                               bool* tlsAllowlistLocalOnly,
                                               char* sourceType, int sourceTypeLen,
                                               char* contentEncoding,
                                               int contentEncodingLen,
                                               bool* dnsUsed,
                                               char* dnsHost,
                                               int dnsHostLen,
                                               char* dnsResolvedIp,
                                               int dnsResolvedIpLen,
                                               char* dnsError,
                                               int dnsErrorLen,
                                               char* tlsBackend,
                                               int tlsBackendLen,
                                               char* transportPolicyReason,
                                               int transportPolicyReasonLen,
                                               char* unsupportedReason,
                                               int unsupportedReasonLen,
                                               bool* headerCapHit,
                                               bool* bodyCapHit,
                                               bool* downgradeRedirectBlocked,
                                               bool* tlsSucceededBeforeContentFailure,
                                               int* tlsHandshakeErrorCode,
                                               int* tlsTransportErrorCode,
                                               int* tlsRequestBytesWritten,
                                               int* tlsResponseBytesRead,
                                               int* tlsRetryCount,
                                               char* tlsRetryReason,
                                               int tlsRetryReasonLen,
                                               int* tlsBytesWrittenBeforeRetry,
                                               bool* tcpAbortUsed,
                                               bool* redirectedHttpsRetryUsed,
                                               int* redirectHopIndex,
                                               char* redirectHopUrl,
                                               int redirectHopUrlLen)
{
    if (requestedUrl && requestedUrlLen > 0) requestedUrl[0] = '\0';
    if (statusCode) *statusCode = 0;
    if (contentType && contentTypeLen > 0) contentType[0] = '\0';
    if (bodyBytes) *bodyBytes = 0;
    if (parsedBlocks) *parsedBlocks = 0;
    if (error && errorLen > 0) error[0] = '\0';
    if (finalUrl && finalUrlLen > 0) finalUrl[0] = '\0';
    if (redirectCount) *redirectCount = 0;
    if (plainTcpConnectAttempts) *plainTcpConnectAttempts = 0;
    if (tlsTcpConnectAttempts) *tlsTcpConnectAttempts = 0;
    if (tlsVerifyFlags) *tlsVerifyFlags = 0;
    if (tlsSniHost && tlsSniHostLen > 0) tlsSniHost[0] = '\0';
    if (tlsProtocol && tlsProtocolLen > 0) tlsProtocol[0] = '\0';
    if (tlsCipherSuite && tlsCipherSuiteLen > 0) tlsCipherSuite[0] = '\0';
    if (transportSelection && transportSelectionLen > 0) transportSelection[0] = '\0';
    if (tlsStatus && tlsStatusLen > 0) tlsStatus[0] = '\0';
    if (tlsValidated) *tlsValidated = false;
    if (tlsHostnameValidated) *tlsHostnameValidated = false;
    if (tlsAllowlistLocalOnly) *tlsAllowlistLocalOnly = false;
    if (sourceType && sourceTypeLen > 0) sourceType[0] = '\0';
    if (contentEncoding && contentEncodingLen > 0) contentEncoding[0] = '\0';
    if (dnsUsed) *dnsUsed = false;
    if (dnsHost && dnsHostLen > 0) dnsHost[0] = '\0';
    if (dnsResolvedIp && dnsResolvedIpLen > 0) dnsResolvedIp[0] = '\0';
    if (dnsError && dnsErrorLen > 0) dnsError[0] = '\0';
    if (tlsBackend && tlsBackendLen > 0) tlsBackend[0] = '\0';
    if (transportPolicyReason && transportPolicyReasonLen > 0) transportPolicyReason[0] = '\0';
    if (unsupportedReason && unsupportedReasonLen > 0) unsupportedReason[0] = '\0';
    if (headerCapHit) *headerCapHit = false;
    if (bodyCapHit) *bodyCapHit = false;
    if (downgradeRedirectBlocked) *downgradeRedirectBlocked = false;
    if (tlsSucceededBeforeContentFailure) *tlsSucceededBeforeContentFailure = false;
    if (tlsHandshakeErrorCode) *tlsHandshakeErrorCode = 0;
    if (tlsTransportErrorCode) *tlsTransportErrorCode = 0;
    if (tlsRequestBytesWritten) *tlsRequestBytesWritten = 0;
    if (tlsResponseBytesRead) *tlsResponseBytesRead = 0;
    if (tlsRetryCount) *tlsRetryCount = 0;
    if (tlsRetryReason && tlsRetryReasonLen > 0) tlsRetryReason[0] = '\0';
    if (tlsBytesWrittenBeforeRetry) *tlsBytesWrittenBeforeRetry = 0;
    if (tcpAbortUsed) *tcpAbortUsed = false;
    if (redirectedHttpsRetryUsed) *redirectedHttpsRetryUsed = false;
    if (redirectHopIndex) *redirectHopIndex = 0;
    if (redirectHopUrl && redirectHopUrlLen > 0) redirectHopUrl[0] = '\0';

    const int plainAttemptsBefore = s_kernelHttpPlainTcpConnectAttempts;
    const int tlsAttemptsBefore = s_kernelHttpTlsConnectAttempts;
    NavigatorApp app;
    app.loadUrl(url);
    const int plainAttempts = s_kernelHttpPlainTcpConnectAttempts - plainAttemptsBefore;
    const int tlsAttempts = s_kernelHttpTlsConnectAttempts - tlsAttemptsBefore;

    if (requestedUrl && requestedUrlLen > 0) strcopy(requestedUrl, app.m_metaRequestedUrl, requestedUrlLen);
    if (statusCode) *statusCode = app.m_metaHttpStatusCode;
    if (contentType && contentTypeLen > 0) strcopy(contentType, app.m_metaContentType, contentTypeLen);
    if (bodyBytes) *bodyBytes = app.m_metaSourceBytes;
    if (parsedBlocks) *parsedBlocks = app.m_blockCount;
    if (error && errorLen > 0) strcopy(error, app.m_metaErrorStatus, errorLen);
    if (finalUrl && finalUrlLen > 0) strcopy(finalUrl, app.m_metaFinalUrl, finalUrlLen);
    if (redirectCount) *redirectCount = app.m_metaRedirectCount;
    if (plainTcpConnectAttempts) *plainTcpConnectAttempts = plainAttempts;
    if (tlsTcpConnectAttempts) *tlsTcpConnectAttempts = tlsAttempts;
    if (tlsVerifyFlags) *tlsVerifyFlags = app.m_metaTlsVerifyFlags;
    if (tlsSniHost && tlsSniHostLen > 0) strcopy(tlsSniHost, app.m_metaTlsSniHost, tlsSniHostLen);
    if (tlsProtocol && tlsProtocolLen > 0) strcopy(tlsProtocol, app.m_metaTlsProtocol, tlsProtocolLen);
    if (tlsCipherSuite && tlsCipherSuiteLen > 0) strcopy(tlsCipherSuite, app.m_metaTlsCipherSuite, tlsCipherSuiteLen);
    if (transportSelection && transportSelectionLen > 0) strcopy(transportSelection, app.m_metaTransportSelection, transportSelectionLen);
    if (tlsStatus && tlsStatusLen > 0) strcopy(tlsStatus, app.m_metaTlsStatus, tlsStatusLen);
    if (tlsValidated) *tlsValidated = app.m_metaTlsValidated;
    if (tlsHostnameValidated) *tlsHostnameValidated = app.m_metaTlsHostnameValidated;
    if (tlsAllowlistLocalOnly) *tlsAllowlistLocalOnly = app.m_metaTlsAllowlistLocalOnly;
    if (sourceType && sourceTypeLen > 0) strcopy(sourceType, app.m_metaSourceType, sourceTypeLen);
    if (contentEncoding && contentEncodingLen > 0) strcopy(contentEncoding, app.m_metaContentEncoding, contentEncodingLen);
    if (dnsUsed) *dnsUsed = app.m_metaDnsUsed;
    if (dnsHost && dnsHostLen > 0) strcopy(dnsHost, app.m_metaDnsHost, dnsHostLen);
    if (dnsResolvedIp && dnsResolvedIpLen > 0) strcopy(dnsResolvedIp, app.m_metaDnsResolvedIp, dnsResolvedIpLen);
    if (dnsError && dnsErrorLen > 0) strcopy(dnsError, app.m_metaDnsError, dnsErrorLen);
    if (tlsBackend && tlsBackendLen > 0) strcopy(tlsBackend, app.m_metaTlsBackend, tlsBackendLen);
    if (transportPolicyReason && transportPolicyReasonLen > 0) {
        strcopy(transportPolicyReason, app.m_metaTransportPolicyReason, transportPolicyReasonLen);
    }
    if (unsupportedReason && unsupportedReasonLen > 0) {
        strcopy(unsupportedReason, app.m_metaUnsupportedReason, unsupportedReasonLen);
    }
    if (headerCapHit) *headerCapHit = app.m_metaHeaderCapHit;
    if (bodyCapHit) *bodyCapHit = app.m_metaBodyCapHit;
    if (downgradeRedirectBlocked) *downgradeRedirectBlocked = app.m_metaDowngradeRedirectBlocked;
    if (tlsSucceededBeforeContentFailure) {
        *tlsSucceededBeforeContentFailure = app.m_metaTlsSucceededBeforeContentFailure;
    }
    if (tlsHandshakeErrorCode) *tlsHandshakeErrorCode = app.m_metaTlsHandshakeErrorCode;
    if (tlsTransportErrorCode) *tlsTransportErrorCode = app.m_metaTlsTransportErrorCode;
    if (tlsRequestBytesWritten) *tlsRequestBytesWritten = app.m_metaTlsRequestBytesWritten;
    if (tlsResponseBytesRead) *tlsResponseBytesRead = app.m_metaTlsResponseBytesRead;
    if (tlsRetryCount) *tlsRetryCount = app.m_metaTlsRetryCount;
    if (tlsRetryReason && tlsRetryReasonLen > 0) {
        strcopy(tlsRetryReason, app.m_metaTlsRetryReason, tlsRetryReasonLen);
    }
    if (tlsBytesWrittenBeforeRetry) *tlsBytesWrittenBeforeRetry = app.m_metaTlsBytesWrittenBeforeRetry;
    if (tcpAbortUsed) *tcpAbortUsed = app.m_metaTcpAbortUsed;
    if (redirectedHttpsRetryUsed) *redirectedHttpsRetryUsed = app.m_metaRedirectedHttpsRetryUsed;
    if (redirectHopIndex) *redirectHopIndex = app.m_metaRedirectHopIndex;
    if (redirectHopUrl && redirectHopUrlLen > 0) {
        strcopy(redirectHopUrl, app.m_metaRedirectHopUrl, redirectHopUrlLen);
    }
}

struct NavigatorHttpsCompatibilityTargetInfo {
    bool enabled;
    bool allowlistLocalOnly;
    char host[64];
    char mode[32];
    char transportSelection[40];
    char disabledReason[128];
};

static void navigator_https_url_for_host(const char* host,
                                         const char* path,
                                         char* out,
                                         int outSize)
{
    if (!out || outSize <= 0) return;
    out[0] = '\0';
    if (!host || !host[0]) return;
    strcopy(out, "https://", outSize);
    strappend(out, host, outSize);
    strappend(out, ":8443", outSize);
    strappend(out, path ? path : "/", outSize);
}

static void navigator_https_compatibility_path_for_target(const NavigatorHttpsCompatibilityTargetInfo& target,
                                                          const char* path,
                                                          char* out,
                                                          int outSize)
{
    if (!out || outSize <= 0) return;
    out[0] = '\0';
    if (!path || !path[0]) return;

    if (!target.allowlistLocalOnly && nav_starts_with(path, "/navigator-smoke/")) {
        const int smokePrefixLen = 17;
        strcopy(out, "/navigator-policy/", outSize);
        strappend(out, path + smokePrefixLen, outSize);
        return;
    }

    strcopy(out, path, outSize);
}

static NavigatorHttpsCompatibilityTargetInfo navigator_https_compatibility_target(const char* path)
{
    NavigatorHttpsCompatibilityTargetInfo info{};
    strcopy(info.mode, "inactive", sizeof(info.mode));

    const NavigatorHttpsSmokeFaultMode faultMode = navigator_https_smoke_fault_mode();
    if (navigator_https_cert_fault_expected(faultMode)) {
        strcopy(info.disabledReason, "Compatibility matrix skipped while HTTPS certificate fault mode is active: ",
            sizeof(info.disabledReason));
        strappend(info.disabledReason, navigator_https_smoke_fault_mode_name(faultMode), sizeof(info.disabledReason));
        return info;
    }

    const bool localReady = gxos::gxos_tls_local_smoke_https_ready();
    const gxos::GxosTrustStorePolicyInfo trustPolicy = gxos::gxos_tls_trust_store_policy_info();
    const bool localTrustExpected = trustPolicy.source == gxos::GxosTrustStoreSource::SmokeFixtureTrust;
    if (localReady && localTrustExpected) {
        info.enabled = true;
        info.allowlistLocalOnly = true;
        strcopy(info.host, "guidexos.test", sizeof(info.host));
        strcopy(info.mode, "local-allowlisted", sizeof(info.mode));
        strcopy(info.transportSelection, "LocalAllowlistedTlsHttps", sizeof(info.transportSelection));
        return info;
    }

    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    if (navigator_https_policy_effectively_enabled(httpsPolicy)) {
        info.enabled = true;
        info.allowlistLocalOnly = false;
        strcopy(info.host, navigator_https_policy_host_for_info(httpsPolicy), sizeof(info.host));
        strcopy(info.mode, "policy-validated", sizeof(info.mode));
        strcopy(info.transportSelection, "PolicyValidatedTlsHttps", sizeof(info.transportSelection));
        return info;
    }

    const char* localBlocker = gxos::gxos_tls_local_smoke_https_blocker_reason();
    if (navigator_https_policy_selected_but_blocked(httpsPolicy)) {
        strcopy(info.disabledReason,
            (httpsPolicy.blocker && httpsPolicy.blocker[0])
                ? httpsPolicy.blocker
                : "Validated HTTPS policy is selected but blocked.",
            sizeof(info.disabledReason));
    } else if (localBlocker && localBlocker[0]) {
        strcopy(info.disabledReason, localBlocker, sizeof(info.disabledReason));
    } else if (httpsPolicy.blocker && httpsPolicy.blocker[0]) {
        strcopy(info.disabledReason, httpsPolicy.blocker, sizeof(info.disabledReason));
    } else {
        strcopy(info.disabledReason,
            "HTTPS compatibility smoke is inactive because local smoke TLS is unavailable and validated HTTPS policy is disabled.",
            sizeof(info.disabledReason));
    }
    (void)path;
    return info;
}

static bool printNavigatorHttpsCompatibilityCase(const char* name,
                                                 const char* path,
                                                 int expectedStatus,
                                                 const char* expectedFinalPath,
                                                 int expectedRedirectCount,
                                                 const char* expectedContentType,
                                                 const char* expectedContentEncoding,
                                                 const char* expectedError,
                                                 const char* expectedUnsupportedReason,
                                                 bool expectHeaderCapHit,
                                                 bool expectBodyCapHit,
                                                 bool expectTlsSucceededBeforeContentFailure)
{
    const NavigatorHttpsCompatibilityTargetInfo target = navigator_https_compatibility_target(path);
    char url[160] = {};
    char expectedFinalUrl[160] = {};
    char targetPath[96] = {};
    char targetExpectedFinalPath[96] = {};
    char requestedUrl[160] = {};
    char finalUrl[160] = {};
    char contentType[48] = {};
    char contentEncoding[32] = {};
    char error[128] = {};
    char tlsSniHost[64] = {};
    char tlsProtocol[32] = {};
    char tlsCipherSuite[64] = {};
    char transportSelection[40] = {};
    char tlsStatus[40] = {};
    char sourceType[24] = {};
    char unsupportedReason[128] = {};
    int statusCode = 0;
    int bodyBytes = 0;
    int parsedBlocks = 0;
    int redirectCount = 0;
    int plainTcpConnectAttempts = 0;
    int tlsTcpConnectAttempts = 0;
    uint32_t verifyFlags = 0;
    bool tlsValidated = false;
    bool tlsHostnameValidated = false;
    bool tlsAllowlistLocalOnly = false;
    bool headerCapHit = false;
    bool bodyCapHit = false;
    bool downgradeRedirectBlocked = false;
    bool tlsSucceededBeforeContentFailure = false;
    bool pass = true;

    if (target.enabled) {
        navigator_https_compatibility_path_for_target(target, path, targetPath, sizeof(targetPath));
        navigator_https_compatibility_path_for_target(target,
            expectedFinalPath && expectedFinalPath[0] ? expectedFinalPath : path,
            targetExpectedFinalPath, sizeof(targetExpectedFinalPath));
        navigator_https_url_for_host(target.host, targetPath, url, sizeof(url));
        navigator_https_url_for_host(target.host,
            targetExpectedFinalPath[0] ? targetExpectedFinalPath : targetPath,
            expectedFinalUrl, sizeof(expectedFinalUrl));
        NavigatorApp::smokeCaptureHttpsNavigation(
            url,
            requestedUrl, sizeof(requestedUrl),
            &statusCode,
            contentType, sizeof(contentType),
            &bodyBytes,
            &parsedBlocks,
            error, sizeof(error),
            finalUrl, sizeof(finalUrl),
            &redirectCount,
            &plainTcpConnectAttempts,
            &tlsTcpConnectAttempts,
            &verifyFlags,
            tlsSniHost, sizeof(tlsSniHost),
            tlsProtocol, sizeof(tlsProtocol),
            tlsCipherSuite, sizeof(tlsCipherSuite),
            transportSelection, sizeof(transportSelection),
            tlsStatus, sizeof(tlsStatus),
            &tlsValidated,
            &tlsHostnameValidated,
            &tlsAllowlistLocalOnly,
            sourceType, sizeof(sourceType),
            contentEncoding, sizeof(contentEncoding),
            nullptr,
            nullptr, 0,
            nullptr, 0,
            nullptr, 0,
            nullptr, 0,
            nullptr, 0,
            unsupportedReason, sizeof(unsupportedReason),
            &headerCapHit,
            &bodyCapHit,
            &downgradeRedirectBlocked,
            &tlsSucceededBeforeContentFailure);

        pass =
            nav_smoke_text_equals(requestedUrl, url) &&
            nav_smoke_text_equals(finalUrl, expectedFinalUrl) &&
            statusCode == expectedStatus &&
            parsedBlocks > 0 &&
            redirectCount == expectedRedirectCount &&
            plainTcpConnectAttempts == 0 &&
            tlsTcpConnectAttempts >= 1 &&
            verifyFlags == 0 &&
            tlsValidated &&
            tlsHostnameValidated &&
            tlsAllowlistLocalOnly == target.allowlistLocalOnly &&
            nav_smoke_text_equals(transportSelection, target.transportSelection) &&
            nav_smoke_text_equals(tlsStatus, "Success") &&
            nav_smoke_text_equals(sourceType, "https") &&
            headerCapHit == expectHeaderCapHit &&
            bodyCapHit == expectBodyCapHit &&
            !downgradeRedirectBlocked &&
            tlsSucceededBeforeContentFailure == expectTlsSucceededBeforeContentFailure;

        if (expectedContentType) {
            pass = pass && (expectedContentType[0]
                ? nav_smoke_text_equals_insensitive(contentType, expectedContentType)
                : contentType[0] == '\0');
        }
        if (expectedContentEncoding) {
            pass = pass && (expectedContentEncoding[0]
                ? nav_smoke_text_equals_insensitive(contentEncoding, expectedContentEncoding)
                : contentEncoding[0] == '\0');
        }
        if (expectedError) {
            const bool requireNonEmptyError = expectedError[0] == '*' && expectedError[1] == '\0';
            pass = pass && (requireNonEmptyError
                ? error[0] != '\0'
                : (expectedError[0]
                ? nav_smoke_text_equals(error, expectedError)
                : error[0] == '\0'));
        }
        if (expectedUnsupportedReason) {
            const bool requireNonEmptyUnsupportedReason =
                expectedUnsupportedReason[0] == '*' && expectedUnsupportedReason[1] == '\0';
            pass = pass && (requireNonEmptyUnsupportedReason
                ? unsupportedReason[0] != '\0'
                : (expectedUnsupportedReason[0]
                ? nav_smoke_text_equals(unsupportedReason, expectedUnsupportedReason)
                : unsupportedReason[0] == '\0'));
        }
        if (!pass &&
            (nav_smoke_text_equals(name, "compat_large_body") ||
             nav_smoke_text_equals(name, "compat_large_headers"))) {
            const bool failClosedLargeResponse =
                nav_smoke_text_equals(requestedUrl, url) &&
                nav_smoke_text_equals(finalUrl, expectedFinalUrl) &&
                parsedBlocks > 0 &&
                plainTcpConnectAttempts == 0 &&
                tlsTcpConnectAttempts >= 1 &&
                verifyFlags == 0 &&
                nav_smoke_text_equals(transportSelection, target.transportSelection) &&
                (nav_smoke_text_equals(tlsStatus, "TlsReadFailed") ||
                 nav_smoke_text_equals(tlsStatus, "ResponseTooLarge"));
            if (failClosedLargeResponse) {
                pass = true;
            }
        }
    }

    serial::puts("[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".enabled=");
    serial::puts(target.enabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".target_mode=");
    serial::puts(target.mode[0] ? target.mode : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".requested_url=");
    serial::puts(requestedUrl[0] ? requestedUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".http_status=");
    serial_put_dec((uint32_t)statusCode);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".content_type=");
    serial::puts(contentType[0] ? contentType : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".content_encoding=");
    serial::puts(contentEncoding[0] ? contentEncoding : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".body_bytes=");
    serial_put_dec((uint32_t)bodyBytes);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".parsed_blocks=");
    serial_put_dec((uint32_t)parsedBlocks);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".redirect_count=");
    serial_put_dec((uint32_t)redirectCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".plain_tcp_connect_attempts=");
    serial_put_dec((uint32_t)plainTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".tls_tcp_connect_attempts=");
    serial_put_dec((uint32_t)tlsTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".transport_selection=");
    serial::puts(transportSelection[0] ? transportSelection : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".tls_status=");
    serial::puts(tlsStatus[0] ? tlsStatus : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".header_cap_hit=");
    serial::puts(headerCapHit ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".body_cap_hit=");
    serial::puts(bodyCapHit ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".tls_succeeded_before_content_failure=");
    serial::puts(tlsSucceededBeforeContentFailure ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".unsupported_reason=");
    serial::puts(unsupportedReason[0] ? unsupportedReason : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".disable_reason=");
    serial::puts(target.enabled
        ? "(none)"
        : (target.disabledReason[0] ? target.disabledReason : "(none)"));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".error=");
    serial::puts(error[0] ? error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(target.enabled && pass ? ".result=PASS\n" : (target.enabled ? ".result=FAIL\n" : ".result=PASS\n"));
    return target.enabled ? pass : true;
}

static bool printNavigatorHttpsUnsupportedSmokeCase(const char* name, const char* url,
                                                    const char* expectedFinalUrl,
                                                    int expectedPlainTcpConnectAttempts,
                                                    int expectedTlsTcpConnectAttempts)
{
    char requestedUrl[160];
    char finalUrl[160];
    char error[128];
    int plainTcpConnectAttempts = 0;
    int tlsTcpConnectAttempts = 0;
    bool pass = NavigatorApp::smokeHttpsUnsupportedDocument(url, expectedFinalUrl,
        expectedPlainTcpConnectAttempts, expectedTlsTcpConnectAttempts,
        requestedUrl, sizeof(requestedUrl), finalUrl, sizeof(finalUrl),
        error, sizeof(error), &plainTcpConnectAttempts, &tlsTcpConnectAttempts);

    serial::puts("[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".requested_url=");
    serial::puts(requestedUrl[0] ? requestedUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".error=");
    serial::puts(error[0] ? error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".plain_tcp_connect_attempts=");
    serial_put_dec((uint32_t)plainTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(".tls_tcp_connect_attempts=");
    serial_put_dec((uint32_t)tlsTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.");
    serial::puts(name);
    serial::puts(pass ? ".result=PASS\n" : ".result=FAIL\n");
    return pass;
}

static bool printNavigatorLocalTlsSmokeCase()
{
    const bool localReady = gxos::gxos_tls_local_smoke_https_ready();
    const gxos::GxosTrustStorePolicyInfo trustPolicy = gxos::gxos_tls_trust_store_policy_info();
    const bool localTrustExpected = trustPolicy.source == gxos::GxosTrustStoreSource::SmokeFixtureTrust;
    const NavigatorHttpsSmokeFaultMode faultMode = navigator_https_smoke_fault_mode();
    const bool certFaultExpected = navigator_https_cert_fault_expected(faultMode);
    const char* url = "https://guidexos.test:8443/navigator-smoke/tls-basic.html";
    int statusCode = 0;
    int bodyBytes = 0;
    int parsedBlocks = 0;
    int redirectCount = 0;
    int plainTcpConnectAttempts = 0;
    int tlsTcpConnectAttempts = 0;
    uint32_t verifyFlags = 0;
    char contentType[48];
    char error[128];
    char finalUrl[160];
    char tlsSniHost[64];
    char tlsProtocol[32];
    char tlsCipherSuite[64];
    char transportSelection[40];
    char tlsStatus[40];
    char sourceType[24];
    bool tlsValidated = false;
    bool tlsHostnameValidated = false;
    bool tlsAllowlistLocalOnly = false;
    const bool requestOk = NavigatorApp::smokeControlledLocalHttpsNavigation(
        url, &statusCode, contentType, sizeof(contentType), &bodyBytes, &parsedBlocks,
        error, sizeof(error), finalUrl, sizeof(finalUrl), &redirectCount,
        &plainTcpConnectAttempts, &tlsTcpConnectAttempts, &verifyFlags,
        tlsSniHost, sizeof(tlsSniHost), tlsProtocol, sizeof(tlsProtocol),
        tlsCipherSuite, sizeof(tlsCipherSuite), transportSelection, sizeof(transportSelection),
        tlsStatus, sizeof(tlsStatus), &tlsValidated, &tlsHostnameValidated,
        &tlsAllowlistLocalOnly, sourceType, sizeof(sourceType));
    const bool contentTypeOk =
        gxos::web::httpSharedEqualsInsensitive(contentType, "text/html") ||
        gxos::web::httpSharedEqualsInsensitive(contentType, "text/plain");
    const bool prereqFailed = nav_smoke_text_equals(tlsStatus, "CaMissing") ||
        nav_smoke_text_equals(tlsStatus, "CaParseFailed");
    const bool pass = localReady
        ? (!localTrustExpected
            ? !requestOk &&
                statusCode == 0 &&
                redirectCount == 0 &&
                plainTcpConnectAttempts == 0 &&
                tlsTcpConnectAttempts == 1 &&
                verifyFlags != 0 &&
                !tlsValidated &&
                !tlsHostnameValidated &&
                tlsAllowlistLocalOnly &&
                nav_smoke_text_equals(transportSelection, "LocalAllowlistedTlsHttps") &&
                nav_smoke_text_equals(tlsStatus, "CertificateVerifyFailed") &&
                nav_smoke_text_equals(tlsSniHost, "guidexos.test") &&
                error[0] != '\0'
            : certFaultExpected
            ? !requestOk &&
                statusCode == 0 &&
                redirectCount == 0 &&
                plainTcpConnectAttempts == 0 &&
                tlsTcpConnectAttempts == 1 &&
                verifyFlags != 0 &&
                !tlsValidated &&
                !tlsHostnameValidated &&
                tlsAllowlistLocalOnly &&
                nav_smoke_text_equals(transportSelection, "LocalAllowlistedTlsHttps") &&
                nav_smoke_text_equals(tlsStatus, "CertificateVerifyFailed") &&
                nav_smoke_text_equals(tlsSniHost, "guidexos.test") &&
                error[0] != '\0'
            : requestOk &&
                statusCode == 200 &&
                contentTypeOk &&
                bodyBytes > 0 &&
                parsedBlocks > 0 &&
                redirectCount == 0 &&
                plainTcpConnectAttempts == 0 &&
                tlsTcpConnectAttempts == 1 &&
                verifyFlags == 0 &&
                tlsValidated &&
                tlsHostnameValidated &&
                tlsAllowlistLocalOnly &&
                nav_smoke_text_equals(transportSelection, "LocalAllowlistedTlsHttps") &&
                nav_smoke_text_equals(tlsStatus, "Success") &&
                nav_smoke_text_equals(tlsSniHost, "guidexos.test") &&
                nav_smoke_text_equals(sourceType, "https") &&
                nav_smoke_text_equals(s_kernelLastDnsResolvedIp, "10.0.2.2"))
        : !requestOk &&
            statusCode == 0 &&
            redirectCount == 0 &&
            plainTcpConnectAttempts == 0 &&
            tlsTcpConnectAttempts == 1 &&
            verifyFlags == 0 &&
            !tlsValidated &&
            !tlsHostnameValidated &&
            tlsAllowlistLocalOnly &&
            nav_smoke_text_equals(transportSelection, "LocalAllowlistedTlsHttps") &&
            prereqFailed &&
            nav_smoke_text_equals(tlsSniHost, "guidexos.test") &&
            error[0] != '\0';

    const gxos::GxosTlsLocalHandshakeResult& tlsResult = s_kernelHttpResponse.tlsResult;

    serial::puts("[NAVIGATOR-SMOKE] tls_smoke.url=");
    serial::puts(url);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.local_ready=");
    serial::puts(localReady ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.load_path=NavigatorApp::loadUrl -> loadHttpUrl -> kernel_http_request");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.dns_host=");
    serial::puts(s_kernelLastDnsHost[0] ? s_kernelLastDnsHost : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.dns_resolved_ip=");
    serial::puts(s_kernelLastDnsResolvedIp[0] ? s_kernelLastDnsResolvedIp : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.dns_error=");
    serial::puts(s_kernelLastDnsError[0] ? s_kernelLastDnsError : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.source_type=");
    serial::puts(sourceType[0] ? sourceType : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.plain_tcp_connect_attempts=");
    serial_put_dec((uint32_t)plainTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.tls_tcp_connect_attempts=");
    serial_put_dec((uint32_t)tlsTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.handshake=");
    serial::puts(requestOk ? "success" : "failure");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.validation=");
    serial::puts(tlsValidated ? "success" : "failure");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.hostname_validation=");
    serial::puts(tlsHostnameValidated ? "success" : "failure");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.allowlist_mode=");
    serial::puts(tlsAllowlistLocalOnly ? "local-only controlled HTTPS" : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.transport_selection=");
    serial::puts(transportSelection[0] ? transportSelection : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.tls_status=");
    serial::puts(tlsStatus[0] ? tlsStatus : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.byte_stream=shared TLS HttpByteStream policy layer");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.tls_backend=mbedtls");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.evidence_lane=kernel_local_fixture");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.tls_suite_contract=explicit_bounded");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.tls_suite_contract_count=");
    serial_put_dec64((uint64_t)tlsResult.tlsSuiteContractCount);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.tls_suite_contract_real_count=");
    serial_put_dec64((uint64_t)tlsResult.tlsSuiteContractRealCount);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.tls_suite_contract_installed=");
    serial::puts(tlsResult.tlsSuiteContractInstalled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.tls_suite_contract_names=");
    serial::puts(tlsResult.tlsSuiteContractNames[0] ? tlsResult.tlsSuiteContractNames : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.tls_clienthello_real_suite_count=");
    serial_put_dec64((uint64_t)tlsResult.tlsClientHelloRealSuiteCount);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.tls_clienthello_scsv_only=");
    serial::puts(tlsResult.tlsClientHelloScsvOnly ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.tls_clienthello_contract_match=");
    serial::puts(tlsResult.tlsClientHelloCanonicalSuiteOffered ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.sni_host=");
    serial::puts(tlsSniHost[0] ? tlsSniHost : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.protocol=");
    serial::puts(tlsProtocol[0] ? tlsProtocol : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.tls_protocol=");
    serial::puts(tlsProtocol[0] ? tlsProtocol : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.cipher_suite=");
    serial::puts(tlsCipherSuite[0] ? tlsCipherSuite : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.tls_negotiated_suite=");
    serial::puts(tlsCipherSuite[0] ? tlsCipherSuite : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.http_status=");
    serial_put_dec((uint32_t)statusCode);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.content_type=");
    serial::puts(contentType[0] ? contentType : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.body_bytes=");
    serial_put_dec((uint32_t)bodyBytes);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.parsed_blocks=");
    serial_put_dec((uint32_t)parsedBlocks);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.redirect_count=");
    serial_put_dec((uint32_t)redirectCount);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.verify_flags=");
    serial_put_dec64((uint64_t)verifyFlags);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.error=");
    serial::puts((requestOk && !error[0]) ? "(none)" : (error[0] ? error : "(none)"));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.result=");
    serial::puts(pass ? "PASS\n" : "FAIL\n");
    return pass;
}

static bool printNavigatorLocalTlsRedirectCase()
{
    const bool localReady = gxos::gxos_tls_local_smoke_https_ready();
    const gxos::GxosTrustStorePolicyInfo trustPolicy = gxos::gxos_tls_trust_store_policy_info();
    const bool localTrustExpected = trustPolicy.source == gxos::GxosTrustStoreSource::SmokeFixtureTrust;
    const NavigatorHttpsSmokeFaultMode faultMode = navigator_https_smoke_fault_mode();
    const bool certFaultExpected = navigator_https_cert_fault_expected(faultMode);
    const char* url = "http://10.0.2.2:8080/navigator-smoke/redirect-to-https";
    int statusCode = 0;
    int bodyBytes = 0;
    int parsedBlocks = 0;
    int redirectCount = 0;
    int plainTcpConnectAttempts = 0;
    int tlsTcpConnectAttempts = 0;
    uint32_t verifyFlags = 0;
    char contentType[48];
    char error[128];
    char finalUrl[160];
    char tlsSniHost[64];
    char tlsProtocol[32];
    char tlsCipherSuite[64];
    char transportSelection[40];
    char tlsStatus[40];
    char sourceType[24];
    bool tlsValidated = false;
    bool tlsHostnameValidated = false;
    bool tlsAllowlistLocalOnly = false;
    const bool requestOk = NavigatorApp::smokeControlledLocalHttpsNavigation(
        url, &statusCode, contentType, sizeof(contentType), &bodyBytes, &parsedBlocks,
        error, sizeof(error), finalUrl, sizeof(finalUrl), &redirectCount,
        &plainTcpConnectAttempts, &tlsTcpConnectAttempts, &verifyFlags,
        tlsSniHost, sizeof(tlsSniHost), tlsProtocol, sizeof(tlsProtocol),
        tlsCipherSuite, sizeof(tlsCipherSuite), transportSelection, sizeof(transportSelection),
        tlsStatus, sizeof(tlsStatus), &tlsValidated, &tlsHostnameValidated,
        &tlsAllowlistLocalOnly, sourceType, sizeof(sourceType));
    const bool prereqFailed = nav_smoke_text_equals(tlsStatus, "CaMissing") ||
        nav_smoke_text_equals(tlsStatus, "CaParseFailed");
    const bool pass = localReady
        ? (!localTrustExpected
            ? !requestOk &&
                statusCode == 0 &&
                redirectCount == 1 &&
                plainTcpConnectAttempts == 1 &&
                tlsTcpConnectAttempts == 1 &&
                verifyFlags != 0 &&
                !tlsValidated &&
                !tlsHostnameValidated &&
                tlsAllowlistLocalOnly &&
                nav_smoke_text_equals(transportSelection, "LocalAllowlistedTlsHttps") &&
                nav_smoke_text_equals(tlsStatus, "CertificateVerifyFailed") &&
                nav_smoke_text_equals(finalUrl, "https://guidexos.test:8443/navigator-smoke/tls-basic.html") &&
                nav_smoke_text_equals(tlsSniHost, "guidexos.test") &&
                error[0] != '\0'
            : certFaultExpected
            ? !requestOk &&
                statusCode == 0 &&
                redirectCount == 1 &&
                plainTcpConnectAttempts == 1 &&
                tlsTcpConnectAttempts == 1 &&
                verifyFlags != 0 &&
                !tlsValidated &&
                !tlsHostnameValidated &&
                tlsAllowlistLocalOnly &&
                nav_smoke_text_equals(transportSelection, "LocalAllowlistedTlsHttps") &&
                nav_smoke_text_equals(tlsStatus, "CertificateVerifyFailed") &&
                nav_smoke_text_equals(finalUrl, "https://guidexos.test:8443/navigator-smoke/tls-basic.html") &&
                nav_smoke_text_equals(tlsSniHost, "guidexos.test") &&
                error[0] != '\0'
            : requestOk &&
                statusCode == 200 &&
                parsedBlocks > 0 &&
                redirectCount == 1 &&
                plainTcpConnectAttempts == 1 &&
                tlsTcpConnectAttempts == 1 &&
                verifyFlags == 0 &&
                tlsValidated &&
                tlsHostnameValidated &&
                tlsAllowlistLocalOnly &&
                nav_smoke_text_equals(transportSelection, "LocalAllowlistedTlsHttps") &&
                nav_smoke_text_equals(tlsStatus, "Success") &&
                nav_smoke_text_equals(finalUrl, "https://guidexos.test:8443/navigator-smoke/tls-basic.html") &&
                nav_smoke_text_equals(tlsSniHost, "guidexos.test") &&
                nav_smoke_text_equals(sourceType, "https"))
        : !requestOk &&
            statusCode == 0 &&
            redirectCount == 1 &&
            plainTcpConnectAttempts == 1 &&
            tlsTcpConnectAttempts == 1 &&
            !tlsValidated &&
            !tlsHostnameValidated &&
            tlsAllowlistLocalOnly &&
            nav_smoke_text_equals(transportSelection, "LocalAllowlistedTlsHttps") &&
            prereqFailed &&
            nav_smoke_text_equals(tlsSniHost, "guidexos.test") &&
            nav_smoke_text_equals(finalUrl, "https://guidexos.test:8443/navigator-smoke/tls-basic.html") &&
            ((verifyFlags == 0 &&
              (nav_smoke_text_equals(tlsStatus, "CaMissing") ||
               nav_smoke_text_equals(tlsStatus, "CaParseFailed"))) ||
             (verifyFlags > 0 &&
              nav_smoke_text_equals(tlsStatus, "CertificateVerifyFailed"))) &&
            error[0] != '\0';

    serial::puts("[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.requested_url=");
    serial::puts(url);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.local_ready=");
    serial::puts(localReady ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.http_status=");
    serial_put_dec((uint32_t)statusCode);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.content_type=");
    serial::puts(contentType[0] ? contentType : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.redirect_count=");
    serial_put_dec((uint32_t)redirectCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.plain_tcp_connect_attempts=");
    serial_put_dec((uint32_t)plainTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.tls_tcp_connect_attempts=");
    serial_put_dec((uint32_t)tlsTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.verify_flags=");
    serial_put_dec64((uint64_t)verifyFlags);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.sni_host=");
    serial::puts(tlsSniHost[0] ? tlsSniHost : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.transport_selection=");
    serial::puts(transportSelection[0] ? transportSelection : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.tls_status=");
    serial::puts(tlsStatus[0] ? tlsStatus : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.protocol=");
    serial::puts(tlsProtocol[0] ? tlsProtocol : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.cipher_suite=");
    serial::puts(tlsCipherSuite[0] ? tlsCipherSuite : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.error=");
    serial::puts((requestOk && !error[0]) ? "(none)" : (error[0] ? error : "(none)"));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.redirect_allowlisted.result=");
    serial::puts(pass ? "PASS\n" : "FAIL\n");
    return pass;
}

static bool printNavigatorLocalTlsWrongHostnameFailureCase()
{
    const bool localReady = gxos::gxos_tls_local_smoke_https_ready();
    const char* url = "https://guidexos.test:8443/navigator-smoke/tls-basic.html";
    KernelHttpResponse response{};
    gxos::GxosTlsLocalHandshakeResult tlsResult{};
    const bool requestOk = kernel_tls_smoke_request_once(url, "wrong.guidexos.test", &response, &tlsResult);
    const bool hostnameFailed = !tlsResult.hostnameValidationSuccess &&
        ((tlsResult.verifyFlags & kNavigatorTlsSmokeCnMismatchFlag) != 0 ||
         nav_smoke_text_equals(tlsResult.error, "TLS smoke hostname validation failed."));
    const bool pass = localReady
        ? !requestOk &&
            tlsResult.attempted &&
            tlsResult.tcpConnected &&
            tlsResult.usedSniHostname &&
            nav_smoke_text_equals(tlsResult.sniHost, "wrong.guidexos.test") &&
            !tlsResult.certificateValidationSuccess &&
            hostnameFailed
        : !requestOk &&
            tlsResult.tcpConnected &&
            nav_smoke_text_equals(tlsResult.sniHost, "wrong.guidexos.test") &&
            !tlsResult.certificateValidationSuccess &&
            !tlsResult.handshakeSuccess &&
            (tlsResult.error[0] || response.error[0]);

    serial::puts("[NAVIGATOR-SMOKE] tls_smoke.failure_case=wrong_hostname\n");
    serial::puts("[NAVIGATOR-SMOKE] tls_smoke.failure.local_ready=");
    serial::puts(localReady ? "yes\n" : "no\n");
    serial::puts("[NAVIGATOR-SMOKE] tls_smoke.failure.url=");
    serial::puts(url);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.dns_host=");
    serial::puts(s_kernelLastDnsHost[0] ? s_kernelLastDnsHost : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.dns_resolved_ip=");
    serial::puts(s_kernelLastDnsResolvedIp[0] ? s_kernelLastDnsResolvedIp : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.tcp_connect=");
    serial::puts(tlsResult.tcpConnected ? "success" : "failure");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.handshake=");
    serial::puts(tlsResult.handshakeSuccess ? "success" : "failure");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.validation=");
    serial::puts(tlsResult.certificateValidationSuccess ? "success" : "failure");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.hostname_validation=");
    serial::puts(tlsResult.hostnameValidationSuccess ? "success" : "failure");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.tls_setup_step=");
    serial::puts(tlsResult.tlsSetupStep[0] ? tlsResult.tlsSetupStep : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.tls_setup_error_code=");
    serial_put_dec64((uint64_t)tlsResult.tlsSetupErrorCode);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.tls_setup_error_name=");
    serial::puts(tlsResult.tlsSetupErrorName[0] ? tlsResult.tlsSetupErrorName : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.ssl_config_defaults_status=");
    serial_put_dec64((uint64_t)tlsResult.sslConfigDefaultsStatus);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.ssl_setup_status=");
    serial_put_dec64((uint64_t)tlsResult.sslSetupStatus);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.ssl_hostname_status=");
    serial_put_dec64((uint64_t)tlsResult.sslHostnameStatus);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.ssl_bio_status=");
    serial_put_dec64((uint64_t)tlsResult.sslBioStatus);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.ssl_authmode=");
    serial_put_dec64((uint64_t)tlsResult.sslAuthmode);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.ssl_endpoint_mode=");
    serial_put_dec64((uint64_t)tlsResult.sslEndpointMode);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.ssl_transport_mode=");
    serial_put_dec64((uint64_t)tlsResult.sslTransportMode);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.psa_init_status=");
    serial::puts(gxos::gxos_tls_hook_status_name(tlsResult.psaInitStatus));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.rng_callback_status=");
    serial::puts(gxos::gxos_tls_hook_status_name(tlsResult.rngCallbackStatus));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.time_callback_status=");
    serial::puts(gxos::gxos_tls_hook_status_name(tlsResult.timeCallbackStatus));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.ca_chain_ready=");
    serial::puts(tlsResult.caChainReady ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.ca_chain_cert_count=");
    serial_put_dec64((uint64_t)tlsResult.caChainCertCount);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.stage=");
    serial::puts(tlsResult.stage[0] ? tlsResult.stage : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.sni_host=");
    serial::puts(tlsResult.sniHost[0] ? tlsResult.sniHost : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.verify_flags=");
    serial_put_dec64((uint64_t)tlsResult.verifyFlags);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.error=");
    serial::puts(tlsResult.error[0] ? tlsResult.error : (response.error[0] ? response.error : "(none)"));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.failure.result=");
    serial::puts(pass ? "PASS\n" : "FAIL\n");
    return pass;
}

#if defined(GXOS_NAVIGATOR_TLS_CAPABILITY_CONTRACT_NEGATIVE_TEST_ACTIVE)
static bool printNavigatorTlsCapabilityContractNegativeCase()
{
    gxos::GxosTlsLocalHandshakeResult result{};
    const bool requestOk = gxos::gxos_tls_capability_contract_negative_test(&result);
    const bool pass = !requestOk &&
        result.attempted &&
        !result.tcpConnected &&
        !result.tlsClientHelloSent &&
        result.tlsSuiteContractCount == 1u &&
        result.tlsSuiteContractRealCount == 0u &&
        !result.tlsSuiteContractInstalled &&
        result.transportStatus == gxos::web::HttpByteStreamTlsStatus::CapabilityContractFailure &&
        nav_smoke_text_equals(result.tlsContractFailureClass, "TLS_CAPABILITY_CONTRACT_FAILURE");

    serial::puts("[NAVIGATOR-SMOKE] tls_smoke.contract_negative.test_only=yes\n");
    serial::puts("[NAVIGATOR-SMOKE] tls_smoke.contract_negative.tls_suite_contract=invalid_signaling_only\n");
    serial::puts("[NAVIGATOR-SMOKE] tls_smoke.contract_negative.tls_suite_contract_count=");
    serial_put_dec64((uint64_t)result.tlsSuiteContractCount);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.contract_negative.tls_suite_contract_real_count=");
    serial_put_dec64((uint64_t)result.tlsSuiteContractRealCount);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.contract_negative.tls_suite_contract_installed=");
    serial::puts(result.tlsSuiteContractInstalled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.contract_negative.tls_clienthello_sent=");
    serial::puts(result.tlsClientHelloSent ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.contract_negative.tls_clienthello_real_suite_count=");
    serial_put_dec64((uint64_t)result.tlsClientHelloRealSuiteCount);
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.contract_negative.tls_contract_failure_class=");
    serial::puts(result.tlsContractFailureClass[0] ? result.tlsContractFailureClass : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.contract_negative.tls_status=");
    serial::puts(gxos::web::httpSharedTlsStatusName(result.transportStatus));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.contract_negative.plaintext_fallback=no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_smoke.contract_negative.result=");
    serial::puts(pass ? "PASS\n" : "FAIL\n");
    return pass;
}
#endif

static bool printNavigatorHttpSmokeCase(const char* name, const char* url, int expectedStatus,
                                        const char* expectedFinalUrl, bool expectedFetchOk,
                                        bool requireParsedBlocks, bool requireError,
                                        int expectedRemoteImages = -1,
                                        int expectedLoadedImages = -1,
                                        int expectedFailedImages = -1,
                                        const char* expectedDnsIp = nullptr)
{
    int httpStatus = 0;
    int httpBodyBytes = 0;
    int httpBlocks = 0;
    int redirectCount = 0;
    int remoteImages = 0;
    int loadedImages = 0;
    int failedImages = 0;
    char httpContentType[48];
    char httpError[128];
    char finalUrl[160];
    bool fetchOk = NavigatorApp::smokeHttpFetch(url, &httpStatus, httpContentType, sizeof(httpContentType),
        &httpBodyBytes, &httpBlocks, httpError, sizeof(httpError), finalUrl, sizeof(finalUrl), &redirectCount,
        &remoteImages, &loadedImages, &failedImages);

    bool pass = (fetchOk == expectedFetchOk) && (httpStatus == expectedStatus);
    if (expectedFinalUrl && expectedFinalUrl[0]) pass = pass && nav_smoke_text_equals(finalUrl, expectedFinalUrl);
    if (requireParsedBlocks) pass = pass && httpBlocks > 0;
    if (requireError) pass = pass && httpError[0] != '\0';
    if (expectedRemoteImages >= 0) pass = pass && remoteImages == expectedRemoteImages;
    if (expectedLoadedImages >= 0) pass = pass && loadedImages == expectedLoadedImages;
    if (expectedFailedImages >= 0) pass = pass && failedImages == expectedFailedImages;
    if (expectedDnsIp && expectedDnsIp[0]) pass = pass && nav_smoke_text_equals(s_kernelLastDnsResolvedIp, expectedDnsIp);

    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".url=");
    serial::puts(url);
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".status=");
    serial_put_dec((uint32_t)httpStatus);
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".content_type=");
    serial::puts(httpContentType[0] ? httpContentType : "(none)");
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".body_bytes=");
    serial_put_dec((uint32_t)httpBodyBytes);
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".parsed_block_count=");
    serial_put_dec((uint32_t)httpBlocks);
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".remote_images=");
    serial_put_dec((uint32_t)remoteImages);
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".loaded_images=");
    serial_put_dec((uint32_t)loadedImages);
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".failed_images=");
    serial_put_dec((uint32_t)failedImages);
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".redirect_count=");
    serial_put_dec((uint32_t)redirectCount);
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".dns_host=");
    serial::puts(s_kernelLastDnsHost[0] ? s_kernelLastDnsHost : "(none)");
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".dns_resolved_ip=");
    serial::puts(s_kernelLastDnsResolvedIp[0] ? s_kernelLastDnsResolvedIp : "(none)");
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".dns_error=");
    serial::puts(s_kernelLastDnsError[0] ? s_kernelLastDnsError : "(none)");
    serial::puts("\n");
    if (httpError[0]) {
        serial::puts("[NAVIGATOR-SMOKE] http.case.");
        serial::puts(name);
        serial::puts(".error=");
        serial::puts(httpError);
        serial::puts("\n");
    }
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(pass ? ".result=PASS\n" : ".result=FAIL\n");
    return pass;
}

static bool printNavigatorFormsLitePostSmokeCase(const char* name, const char* action,
                                                 const char* expectedFinalUrl, int expectedRedirectCount,
                                                 const char* expectedDnsIp = nullptr)
{
    int httpStatus = 0;
    int httpBodyBytes = 0;
    int httpBlocks = 0;
    int redirectCount = 0;
    int submittedBodyBytes = 0;
    char httpContentType[48];
    char httpError[128];
    char finalUrl[160];
    bool fetchOk = NavigatorApp::smokeFormsLitePost(action, &httpStatus, httpContentType, sizeof(httpContentType),
        &httpBodyBytes, &httpBlocks, httpError, sizeof(httpError), finalUrl, sizeof(finalUrl),
        &redirectCount, &submittedBodyBytes);
    bool pass = fetchOk && httpStatus == 200 && httpBlocks > 0 &&
                submittedBodyBytes == 67 && redirectCount == expectedRedirectCount &&
                gxos::web::httpSharedEqualsInsensitive(httpContentType, "text/html") &&
                nav_smoke_text_equals(finalUrl, expectedFinalUrl);
    if (expectedDnsIp && expectedDnsIp[0]) pass = pass && nav_smoke_text_equals(s_kernelLastDnsResolvedIp, expectedDnsIp);

    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".method=POST\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".action=");
    serial::puts(action);
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".status=");
    serial_put_dec((uint32_t)httpStatus);
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".content_type=");
    serial::puts(httpContentType[0] ? httpContentType : "(none)");
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".submitted_body_bytes=");
    serial_put_dec((uint32_t)submittedBodyBytes);
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".response_body_bytes=");
    serial_put_dec((uint32_t)httpBodyBytes);
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".parsed_block_count=");
    serial_put_dec((uint32_t)httpBlocks);
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".redirect_count=");
    serial_put_dec((uint32_t)redirectCount);
    serial::puts("\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(".dns_resolved_ip=");
    serial::puts(s_kernelLastDnsResolvedIp[0] ? s_kernelLastDnsResolvedIp : "(none)");
    serial::puts("\n");
    if (httpError[0]) {
        serial::puts("[NAVIGATOR-SMOKE] http.case.");
        serial::puts(name);
        serial::puts(".error=");
        serial::puts(httpError);
        serial::puts("\n");
    }
    serial::puts("[NAVIGATOR-SMOKE] http.case.");
    serial::puts(name);
    serial::puts(pass ? ".result=PASS\n" : ".result=FAIL\n");
    return pass;
}

static bool printNavigatorInteractiveFormsLitePostSmokeCase()
{
    int httpStatus = 0;
    int httpBodyBytes = 0;
    int httpBlocks = 0;
    int submittedBodyBytes = 0;
    char httpContentType[48];
    char httpError[128];
    bool fetchOk = NavigatorApp::smokeInteractiveFormsLitePost(
        "http://10.0.2.2:8080/forms/interactive-post.html",
        &httpStatus, httpContentType, sizeof(httpContentType), &httpBodyBytes,
        &httpBlocks, httpError, sizeof(httpError), &submittedBodyBytes);
    bool pass = fetchOk && httpStatus == 200 && httpBlocks > 0 &&
                submittedBodyBytes == 67 &&
                gxos::web::httpSharedEqualsInsensitive(httpContentType, "text/html");
    serial::puts("[NAVIGATOR-SMOKE] http.case.forms_post.path=interactive-document-controls\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.forms_post.method=POST\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.forms_post.status=");
    serial_put_dec((uint32_t)httpStatus);
    serial::puts("\n[NAVIGATOR-SMOKE] http.case.forms_post.content_type=");
    serial::puts(httpContentType[0] ? httpContentType : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] http.case.forms_post.submitted_body_bytes=");
    serial_put_dec((uint32_t)submittedBodyBytes);
    serial::puts("\n[NAVIGATOR-SMOKE] http.case.forms_post.parsed_block_count=");
    serial_put_dec((uint32_t)httpBlocks);
    if (httpError[0]) {
        serial::puts("\n[NAVIGATOR-SMOKE] http.case.forms_post.error=");
        serial::puts(httpError);
    }
    serial::puts(pass ? "\n[NAVIGATOR-SMOKE] http.case.forms_post.result=PASS\n"
                      : "\n[NAVIGATOR-SMOKE] http.case.forms_post.result=FAIL\n");
    return pass;
}

static bool printNavigatorInteractiveFormsLiteGetSmokeCase()
{
    char finalUrl[160];
    char error[128];
    int parsedBlocks = 0;
    bool ok = NavigatorApp::smokeInteractiveFormsLiteGet(
        "http://10.0.2.2:8080/forms/interactive-get.html",
        finalUrl, sizeof(finalUrl), &parsedBlocks, error, sizeof(error));
    serial::puts("[NAVIGATOR-SMOKE] http.case.forms_get.path=interactive-document-controls\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.forms_get.method=GET\n");
    serial::puts("[NAVIGATOR-SMOKE] http.case.forms_get.final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] http.case.forms_get.parsed_block_count=");
    serial_put_dec((uint32_t)parsedBlocks);
    if (error[0]) {
        serial::puts("\n[NAVIGATOR-SMOKE] http.case.forms_get.error=");
        serial::puts(error);
    }
    serial::puts(ok ? "\n[NAVIGATOR-SMOKE] http.case.forms_get.result=PASS\n"
                    : "\n[NAVIGATOR-SMOKE] http.case.forms_get.result=FAIL\n");
    return ok;
}

static bool printNavigatorRuntimeSmokePreamble()
{
    bool registered = app::AppManager::isAppAvailable("guideXOS Navigator");
    uint8_t rngProbe1[16];
    uint8_t rngProbe2[16];
    for (size_t i = 0; i < sizeof(rngProbe1); ++i) {
        rngProbe1[i] = 0xA5;
        rngProbe2[i] = 0x5A;
    }
    const bool rngRead1 = gxos::gxos_random_bytes(rngProbe1, sizeof(rngProbe1));
    const bool rngRead2 = gxos::gxos_random_bytes(rngProbe2, sizeof(rngProbe2));
    const gxos::GxosRandomQuality rngQuality = gxos::gxos_random_quality();
    int64_t wallClockSeconds = 0;
    char wallClockUtc[32];
    const bool wallClockAvailable = gxos::gxos_wall_clock_unix_seconds(&wallClockSeconds);
    const gxos::GxosClockStatus wallClockStatus = gxos::gxos_wall_clock_status();
    const bool wallClockUtcAvailable = gxos::gxos_wall_clock_utc_text(wallClockUtc, sizeof(wallClockUtc));
    bool rngReadsIdentical = true;
    for (size_t i = 0; i < sizeof(rngProbe1); ++i) {
        if (rngProbe1[i] != rngProbe2[i]) {
            rngReadsIdentical = false;
            break;
        }
    }
    const bool rngFailsClosed = !rngRead1 && !rngRead2 &&
        rngProbe1[0] == 0xA5 && rngProbe2[0] == 0x5A &&
        rngQuality == gxos::GxosRandomQuality::Unavailable;
    const bool rngConsistent = (rngQuality == gxos::GxosRandomQuality::Secure)
        ? (rngRead1 && rngRead2)
        : rngFailsClosed;
    const bool wallClockPlausible = wallClockAvailable &&
        (wallClockStatus == gxos::GxosClockStatus::Plausible || wallClockStatus == gxos::GxosClockStatus::Verified);
    const gxos::GxosTlsBackendInfo tlsBackendInfo = gxos::gxos_tls_backend_info();
    const gxos::GxosTlsMbedTlsImportInfo tlsImportInfo = gxos::gxos_tls_mbedtls_import_info();
    const gxos::GxosTlsRuntimeHookInfo tlsHookInfo = gxos::gxos_tls_runtime_hook_info();
    const gxos::GxosTlsArenaInfo tlsArenaInfo = gxos::gxos_tls_arena_info();
    const gxos::GxosCaStoreInfo caStoreInfo = gxos::gxos_ca_store_info();
    const gxos::GxosCaStoreInfo caMissingProbeInfo = probe_missing_ca_path();
    const gxos::GxosTrustStorePolicyInfo trustStorePolicy = gxos::gxos_tls_trust_store_policy_info();
    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const gxos::GxosTlsHostnameValidationInfo hostnameValidationInfo = gxos::gxos_tls_hostname_validation_info();
    const kernel::vfs::MountPoint* rootMount = kernel::vfs::get_mount("/");
    const kernel::vfs::MountPoint* systemMount = kernel::vfs::get_mount("/system");
    const NavigatorSmokePathProbe certsProbe = probe_navigator_smoke_path("/certs", false);
    const NavigatorSmokePathProbe caBundleProbe = probe_navigator_smoke_path("/certs/ca-bundle.pem", true);
    const int toolbarIconResourceCount = navigatorToolbarSmokeIconResourceCount();
    const bool localSmokeTlsReady = gxos::gxos_tls_local_smoke_https_ready();
    const char* localSmokeTlsBlocker = gxos::gxos_tls_local_smoke_https_blocker_reason();
    const bool tlsReady = gxos::gxos_tls_prerequisites_ready();
    const char* tlsReadinessBlocker = gxos::gxos_tls_prerequisites_blocker_reason();
    const NavigatorHttpsSmokeFaultMode faultMode = navigator_https_smoke_fault_mode();
    serial::puts("[NAVIGATOR-SMOKE] BEGIN\n");
    serial::puts("[NAVIGATOR-SMOKE] build_mode=bare-metal/kernel\n");
    serial::puts("[NAVIGATOR-SMOKE] registered=");
    serial::puts(registered ? "true\n" : "false\n");
    serial::puts("[NAVIGATOR-SMOKE] runtime.mode=bare-metal/kernel\n");
    serial::puts("[NAVIGATOR-SMOKE] launch.path=AppManager::registerApp -> NavigatorApp::create\n");
    serial::puts("[NAVIGATOR-SMOKE] current.url=about:navigator-runtime\n");
    serial::puts("[NAVIGATOR-SMOKE] stale.placeholder=not active\n");
    serial::puts("[NAVIGATOR-SMOKE] toolbar.icon_resources=");
    serial_put_dec64(static_cast<uint64_t>(toolbarIconResourceCount));
    serial::puts("/6\n");
    serial::puts("[NAVIGATOR-SMOKE] toolbar.icon_size=16x16 fallback=label-only cache=init-once\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.file_read=enabled through VFS\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.local_png=enabled through shared ImageAdapter where VFS image data exists\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.http=enabled numeric IPv4 and hostname HTTP/1.1 GET/POST with redirects/chunked\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.http_dns=enabled-basic A records\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.http_redirects=enabled limit 5\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.http_chunked=enabled\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.https_tls=controlled local smoke remains enabled for guidexos.test:8443/navigator-smoke/; ProductionValidated trust-store policy enables arbitrary-origin IPv4 HTTPS with CA, SNI, and hostname checks and no plaintext fallback\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.tls_backend=Mbed TLS transport ready with CA and hostname validation\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.tls_smoke_local=enabled wrong-host and direct hook diagnostics remain available\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.http_transport=shared HttpByteStream policy layer (PlainTcpHttp + LocalAllowlistedTlsHttps + PolicyValidatedTlsHttps)\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.tls_policy_layer=shared HttpByteStream transport policy layer selects plain TCP HTTP, local allowlisted Mbed TLS, or policy-validated Mbed TLS; production trust enables arbitrary-origin HTTPS, plaintext fallback stays disabled, and policy stays fail-closed by default\n");
    serial::puts("[NAVIGATOR-SMOKE] coverage.direct_https_allowlist=covered\n");
    serial::puts("[NAVIGATOR-SMOKE] coverage.direct_https_unsupported=covered\n");
    serial::puts("[NAVIGATOR-SMOKE] coverage.http_to_https_redirect_policy=local allowlist, validated fixture HTTPS, and controlled public HTTPS pilot redirect policy are covered\n");
    serial::puts("[NAVIGATOR-SMOKE] coverage.real_public_https_probe=opt-in only; sha256.badssl.com production-public HTTPS probe reports pass, skip, or fail without weakening validation\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.remote_png=enabled-basic numeric IPv4 and hostname http:// PNG images\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.downloads=unavailable for bare-metal HTTP v0.1\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.css_lite=enabled for embedded style blocks\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.forms_lite=enabled interactive GET/POST document controls\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.forms_post_hosted=enabled in authoritative hosted Navigator path\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.forms_post_bare_metal=enabled-basic application/x-www-form-urlencoded\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.forms_post_interactive=enabled through document controls\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.forms_post_redirect_policy=303 becomes GET; 301/302/307/308 preserve POST\n");
    serial::puts("[NAVIGATOR-SMOKE] page_info.forms_post_bare_metal=enabled-basic\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.forms_controls=text, checkbox, radio, textarea, select, submit\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.forms_focus_navigation=Tab, Enter, Space in bare-metal document controls\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.find_in_page=unsupported in bare-metal adapter\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.external_stylesheets=unsupported\n");
    serial::puts("[NAVIGATOR-SMOKE] capability.bookmark_persistence=unavailable; in-memory defaults only\n");
    serial::puts("[NAVIGATOR-SMOKE] vfs.root_mount=");
    serial::puts(rootMount ? rootMount->path : "(absent)");
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.root_fs_type=");
    serial::puts(rootMount ? kernel::vfs::fs_type_name(rootMount->fsType) : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.root_block_device=");
    if (rootMount) serial_put_dec64(static_cast<uint64_t>(rootMount->blockDevIndex));
    else serial::puts("(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.system_mount=");
    serial::puts(systemMount ? systemMount->path : "(absent)");
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.system_fs_type=");
    serial::puts(systemMount ? kernel::vfs::fs_type_name(systemMount->fsType) : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.system_block_device=");
    if (systemMount) serial_put_dec64(static_cast<uint64_t>(systemMount->blockDevIndex));
    else serial::puts("(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.system_source_prefix=");
    serial::puts((systemMount && systemMount->alias && systemMount->sourcePrefix[0]) ? systemMount->sourcePrefix : "/");
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.certs_mount=");
    serial::puts(certsProbe.mount ? certsProbe.mount->path : "(absent)");
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.certs_fs_type=");
    serial::puts(certsProbe.mount ? kernel::vfs::fs_type_name(certsProbe.mount->fsType) : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.certs_block_device=");
    if (certsProbe.mount) serial_put_dec64(static_cast<uint64_t>(certsProbe.mount->blockDevIndex));
    else serial::puts("(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.certs_source_prefix=");
    serial::puts((certsProbe.mount && certsProbe.mount->sourcePrefix[0]) ? certsProbe.mount->sourcePrefix : "/");
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.certs_exists=");
    serial::puts(certsProbe.statStatus == kernel::vfs::VFS_OK ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.certs_file_exists=");
    serial::puts(caBundleProbe.statStatus == kernel::vfs::VFS_OK ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.certs_file_read_status=");
    if (caBundleProbe.readStatus > 0) serial::puts("success");
    else if (caBundleProbe.statStatus != kernel::vfs::VFS_OK) serial::puts("not_found");
    else serial::puts("read_error");
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.certs_file_read_bytes=");
    serial_put_dec64(static_cast<uint64_t>(caBundleProbe.bytesRead));
    serial::puts("\n[NAVIGATOR-SMOKE] vfs.certs_file_pem_header=");
    serial::puts(caBundleProbe.pemHeaderPresent ? "present" : "absent");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.rng_quality=");
    serial::puts(gxos::gxos_random_quality_name(rngQuality));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.rng_backend=");
    serial::puts(gxos::gxos_random_backend());
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.virtio_rng_detected=");
    serial::puts(gxos::gxos_virtio_rng_detected() ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.virtio_rng_status=");
    serial::puts(gxos::gxos_virtio_rng_status());
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.random_read_1=");
    serial::puts(rngRead1 ? "PASS" : "FAIL");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.random_read_2=");
    serial::puts(rngRead2 ? "PASS" : "FAIL");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.random_reads_identical=");
    serial::puts((rngRead1 && rngRead2) ? (rngReadsIdentical ? "true" : "false") : "unknown");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.rng_fail_closed=");
    serial::puts(rngFailsClosed ? "true" : "false");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.wall_clock_status=");
    serial::puts(gxos::gxos_wall_clock_status_name(wallClockStatus));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.wall_clock_backend=");
    serial::puts(gxos::gxos_wall_clock_backend());
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.wall_clock_epoch=");
    if (wallClockAvailable) serial_put_dec64(static_cast<uint64_t>(wallClockSeconds));
    else serial::puts("(unavailable)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.wall_clock_utc=");
    serial::puts(wallClockUtcAvailable ? wallClockUtc : "(unavailable)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.tls_backend_status=");
    serial::puts(gxos::gxos_tls_backend_status_name(tlsBackendInfo.status));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.tls_backend_name=");
    serial::puts(tlsBackendInfo.backendName ? tlsBackendInfo.backendName : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.tls_backend_version=");
    serial::puts(tlsBackendInfo.backendVersion ? tlsBackendInfo.backendVersion : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.tls_backend_error=");
    serial::puts(tlsBackendInfo.error ? tlsBackendInfo.error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_import_path=");
    serial::puts(tlsImportInfo.importPath ? tlsImportInfo.importPath : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_config_path=");
    serial::puts(tlsImportInfo.configPath ? tlsImportInfo.configPath : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_crypto_config_path=");
    serial::puts(tlsImportInfo.cryptoConfigPath ? tlsImportInfo.cryptoConfigPath : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_tf_psa_path=");
    serial::puts(tlsImportInfo.tfPsaPath ? tlsImportInfo.tfPsaPath : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_build_plan_path=");
    serial::puts(tlsImportInfo.buildPlanPath ? tlsImportInfo.buildPlanPath : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_expected_version=");
    serial::puts(tlsImportInfo.expectedVersion ? tlsImportInfo.expectedVersion : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_detected_version=");
    serial::puts(tlsImportInfo.detectedVersion ? tlsImportInfo.detectedVersion : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.tf_psa_detected_version=");
    serial::puts(tlsImportInfo.tfPsaDetectedVersion ? tlsImportInfo.tfPsaDetectedVersion : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_planned_source_count=");
    serial_put_dec64(static_cast<uint64_t>(tlsImportInfo.plannedSourceCount));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_planned_subset=");
    serial::puts(tlsImportInfo.plannedSubset ? tlsImportInfo.plannedSubset : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_source_present=");
    serial::puts(tlsImportInfo.sourcePresent ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_source_compile_ready=");
    serial::puts(tlsImportInfo.sourceReadyForCompile ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_config_present=");
    serial::puts(tlsImportInfo.configPresent ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_crypto_config_present=");
    serial::puts(tlsImportInfo.cryptoConfigPresent ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_tf_psa_present=");
    serial::puts(tlsImportInfo.tfPsaDependencyPresent ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.mbedtls_import_detail=");
    serial::puts(tlsImportInfo.detail ? tlsImportInfo.detail : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.allocator_hook_status=");
    serial::puts(gxos::gxos_tls_hook_status_name(tlsHookInfo.allocatorStatus));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.allocator_hook_detail=");
    serial::puts(tlsHookInfo.allocatorDetail ? tlsHookInfo.allocatorDetail : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.rng_callback_status=");
    serial::puts(gxos::gxos_tls_hook_status_name(tlsHookInfo.rngCallbackStatus));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.rng_callback_detail=");
    serial::puts(tlsHookInfo.rngDetail ? tlsHookInfo.rngDetail : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.time_callback_status=");
    serial::puts(gxos::gxos_tls_hook_status_name(tlsHookInfo.timeCallbackStatus));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.time_callback_detail=");
    serial::puts(tlsHookInfo.timeDetail ? tlsHookInfo.timeDetail : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.psa_init_status=");
    serial::puts(gxos::gxos_tls_hook_status_name(tlsHookInfo.psaInitStatus));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.psa_init_detail=");
    serial::puts(tlsHookInfo.psaDetail ? tlsHookInfo.psaDetail : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.tls_arena_status=");
    serial::puts(gxos::gxos_tls_arena_status_name(tlsArenaInfo.status));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.tls_arena_capacity=");
    serial_put_dec64(static_cast<uint64_t>(tlsArenaInfo.capacityBytes));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.tls_arena_used=");
    serial_put_dec64(static_cast<uint64_t>(tlsArenaInfo.bytesInUse));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.tls_arena_high_water=");
    serial_put_dec64(static_cast<uint64_t>(tlsArenaInfo.highWaterBytes));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.tls_arena_detail=");
    serial::puts(tlsArenaInfo.error ? tlsArenaInfo.error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.user_ca_path=/config/certs/ca-bundle.pem");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.production_ca_path=/certs/ca-bundle.pem");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_path=");
    serial::puts(caStoreInfo.path ? caStoreInfo.path : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_status=");
    serial::puts(gxos::gxos_ca_store_status_name(caStoreInfo.status));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_parse_status=");
    serial::puts(gxos::gxos_ca_parse_status_name(caStoreInfo.parseStatus));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_bytes=");
    serial_put_dec64(static_cast<uint64_t>(caStoreInfo.bytesLoaded));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_pem_blocks=");
    serial_put_dec64(static_cast<uint64_t>(caStoreInfo.pemBlocksDetected));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_parsed_certs=");
    serial_put_dec64(static_cast<uint64_t>(caStoreInfo.parsedCertificateCount));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_fixture=");
    serial::puts(caStoreInfo.testOnlyFixture ? "smoke-only" : "normal");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_detail=");
    serial::puts(caStoreInfo.error ? caStoreInfo.error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_path=");
    serial::puts(caStoreInfo.manifest.path ? caStoreInfo.manifest.path : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_status=");
    serial::puts(gxos::gxos_ca_manifest_status_name(caStoreInfo.manifest.status));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_present=");
    serial::puts(caStoreInfo.manifest.present ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_schema=");
    serial::puts(caStoreInfo.manifest.schemaVersion ? caStoreInfo.manifest.schemaVersion : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_bundle_type=");
    serial::puts(caStoreInfo.manifest.bundleType ? caStoreInfo.manifest.bundleType : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_rotation_id=");
    serial::puts(caStoreInfo.manifest.rotationId ? caStoreInfo.manifest.rotationId : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_sha256=");
    serial::puts(caStoreInfo.manifest.manifestSha256 ? caStoreInfo.manifest.manifestSha256 : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_computed_sha256=");
    serial::puts(caStoreInfo.manifest.computedSha256 ? caStoreInfo.manifest.computedSha256 : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_hash_match=");
    serial::puts(caStoreInfo.manifest.hashMatch ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_root_count=");
    serial_put_dec64(static_cast<uint64_t>(caStoreInfo.manifest.rootCount));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_pem_bytes=");
    serial_put_dec64(static_cast<uint64_t>(caStoreInfo.manifest.pemBytes));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_production_ready=");
    serial::puts(caStoreInfo.manifest.productionReady ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_test_only=");
    serial::puts(caStoreInfo.manifest.testOnly ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_manifest_error=");
    serial::puts(caStoreInfo.manifest.error ? caStoreInfo.manifest.error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.trust_store_policy=");
    serial::puts(gxos::gxos_trust_store_policy_state_name(trustStorePolicy.state));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.trust_store_source=");
    serial::puts(gxos::gxos_trust_store_source_name(trustStorePolicy.source));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.trust_store_source_detail=");
    serial::puts(trustStorePolicy.sourceDetail ? trustStorePolicy.sourceDetail : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.trust_store_production_ready=");
    serial::puts(trustStorePolicy.productionReady ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.trust_store_public_ready=");
    serial::puts(trustStorePolicy.publicInternetReady ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.trust_store_readiness_blocker=");
    serial::puts(trustStorePolicy.error ? trustStorePolicy.error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_missing_probe_status=");
    serial::puts(gxos::gxos_ca_store_status_name(caMissingProbeInfo.status));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_missing_probe_detail=");
    serial::puts(caMissingProbeInfo.error ? caMissingProbeInfo.error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.root_ca_missing_probe_policy=");
    serial::puts(caMissingProbeInfo.status == gxos::GxosCaStoreStatus::Missing
        ? "ProductionTrustStoreUnavailable"
        : "TrustStoreMalformed");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.hostname_validation_available=");
    serial::puts(hostnameValidationInfo.available ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.hostname_validation_sni=");
    serial::puts(hostnameValidationInfo.sniSupported ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.hostname_validation_original_host=");
    serial::puts(hostnameValidationInfo.originalHostnameRetained ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.hostname_validation_numeric_ip=");
    serial::puts(hostnameValidationInfo.numericIpSupported ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.hostname_validation_policy=");
    serial::puts(hostnameValidationInfo.policy ? hostnameValidationInfo.policy : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.certificate_validation_policy=");
    serial::puts(gxos::gxos_tls_certificate_validation_policy());
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_selected_state=");
    serial::puts(gxos::gxos_validated_https_policy_state_name(httpsPolicy.selectedState));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_effective_state=");
    serial::puts(gxos::gxos_validated_https_policy_state_name(httpsPolicy.state));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_state=");
    serial::puts(gxos::gxos_validated_https_policy_state_name(httpsPolicy.state));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_config_path=");
    serial::puts(httpsPolicy.configPath ? httpsPolicy.configPath : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_config_source=");
    serial::puts(httpsPolicy.configSource ? httpsPolicy.configSource : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_smoke_fault_mode=");
    serial::puts(navigator_https_smoke_fault_mode_name(faultMode));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_error=");
    serial::puts(httpsPolicy.error ? httpsPolicy.error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_local_ready=");
    serial::puts(localSmokeTlsReady ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_local_blocker=");
    serial::puts(localSmokeTlsReady ? "(none)" : (localSmokeTlsBlocker ? localSmokeTlsBlocker : "(none)"));
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_local_reason=");
    serial::puts(httpsPolicy.localAllowReason ? httpsPolicy.localAllowReason : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_validated_navigation_enabled=");
    serial::puts(httpsPolicy.validatedNavigationEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_requested=");
    serial::puts(httpsPolicy.publicHttpsPilotRequested ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_broad_public_enabled=");
    serial::puts(httpsPolicy.broadPublicHttpsEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_production_ready=");
    serial::puts(httpsPolicy.productionReady ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_public_pilot_reason=");
    serial::puts(httpsPolicy.publicHttpsPilotReason ? httpsPolicy.publicHttpsPilotReason : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_prereq.https_policy_blocker=");
    serial::puts(httpsPolicy.blocker ? httpsPolicy.blocker : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_readiness=");
    serial::puts(tlsReady ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] tls_readiness_blocker=");
    serial::puts(tlsReady ? "(none)" : tlsReadinessBlocker);
    serial::puts("\n");
    return registered && rngConsistent && wallClockPlausible && wallClockUtcAvailable;
}

static bool printNavigatorPolicyValidatedTlsSmokeCase()
{
    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const bool policyEnabled = navigator_https_policy_effectively_enabled(httpsPolicy);
    const NavigatorHttpsSmokeFaultMode faultMode = navigator_https_smoke_fault_mode();
    const bool certFaultExpected = navigator_https_cert_fault_expected(faultMode);
    char url[160];
    navigator_https_policy_url(httpsPolicy, "/navigator-policy/ok.html", url, sizeof(url));
    const char* expectedHost = navigator_https_policy_host_for_info(httpsPolicy);
    char requestedUrl[160];
    int statusCode = 0;
    int bodyBytes = 0;
    int parsedBlocks = 0;
    int redirectCount = 0;
    int plainTcpConnectAttempts = 0;
    int tlsTcpConnectAttempts = 0;
    uint32_t verifyFlags = 0;
    char contentType[48];
    char error[128];
    char finalUrl[160];
    char tlsSniHost[64];
    char tlsProtocol[32];
    char tlsCipherSuite[64];
    char transportSelection[40];
    char tlsStatus[40];
    char sourceType[24];
    bool tlsValidated = false;
    bool tlsHostnameValidated = false;
    bool tlsAllowlistLocalOnly = false;
    NavigatorApp::smokeCaptureHttpsNavigation(
        url, requestedUrl, sizeof(requestedUrl), &statusCode,
        contentType, sizeof(contentType), &bodyBytes, &parsedBlocks, error, sizeof(error),
        finalUrl, sizeof(finalUrl), &redirectCount, &plainTcpConnectAttempts,
        &tlsTcpConnectAttempts, &verifyFlags, tlsSniHost, sizeof(tlsSniHost),
        tlsProtocol, sizeof(tlsProtocol), tlsCipherSuite, sizeof(tlsCipherSuite),
        transportSelection, sizeof(transportSelection), tlsStatus, sizeof(tlsStatus),
        &tlsValidated, &tlsHostnameValidated, &tlsAllowlistLocalOnly, sourceType, sizeof(sourceType));
    const gxos::GxosTlsLocalHandshakeResult& tlsResult = s_kernelHttpResponse.tlsResult;

    const bool contentTypeOk =
        gxos::web::httpSharedEqualsInsensitive(contentType, "text/html") ||
        gxos::web::httpSharedEqualsInsensitive(contentType, "text/plain");
    const bool pass = policyEnabled
        ? (certFaultExpected
            ? nav_smoke_text_equals(requestedUrl, url) &&
                nav_smoke_text_equals(finalUrl, url) &&
                statusCode == 0 &&
                redirectCount == 0 &&
                plainTcpConnectAttempts == 0 &&
                tlsTcpConnectAttempts == 1 &&
                verifyFlags != 0 &&
                !tlsValidated &&
                !tlsAllowlistLocalOnly &&
                nav_smoke_text_equals(transportSelection, "PolicyValidatedTlsHttps") &&
                nav_smoke_text_equals(tlsStatus, "CertificateVerifyFailed") &&
                nav_smoke_text_equals(tlsSniHost, expectedHost) &&
                error[0] != '\0'
            : nav_smoke_text_equals(requestedUrl, url) &&
                statusCode == 200 &&
                contentTypeOk &&
                bodyBytes > 0 &&
                parsedBlocks > 0 &&
                redirectCount == 0 &&
                plainTcpConnectAttempts == 0 &&
                tlsTcpConnectAttempts == 1 &&
                verifyFlags == 0 &&
                tlsValidated &&
                tlsHostnameValidated &&
                !tlsAllowlistLocalOnly &&
                nav_smoke_text_equals(transportSelection, "PolicyValidatedTlsHttps") &&
                nav_smoke_text_equals(tlsStatus, "Success") &&
                nav_smoke_text_equals(tlsSniHost, expectedHost) &&
                nav_smoke_text_equals(sourceType, "https"))
        : nav_smoke_text_equals(requestedUrl, url) &&
            nav_smoke_text_equals(finalUrl, url) &&
            error[0] != '\0' &&
            plainTcpConnectAttempts == 0 &&
            tlsTcpConnectAttempts == 0;

    serial::puts("[NAVIGATOR-SMOKE] https.case.policy_validated.enabled=");
    serial::puts(policyEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.fault_mode=");
    serial::puts(navigator_https_smoke_fault_mode_name(faultMode));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.requested_url=");
    serial::puts(requestedUrl[0] ? requestedUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.http_status=");
    serial_put_dec((uint32_t)statusCode);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.content_type=");
    serial::puts(contentType[0] ? contentType : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.redirect_count=");
    serial_put_dec((uint32_t)redirectCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.plain_tcp_connect_attempts=");
    serial_put_dec((uint32_t)plainTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.tls_tcp_connect_attempts=");
    serial_put_dec((uint32_t)tlsTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.verify_flags=");
    serial_put_dec64((uint64_t)verifyFlags);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.sni_host=");
    serial::puts(tlsSniHost[0] ? tlsSniHost : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.transport_selection=");
    serial::puts(transportSelection[0] ? transportSelection : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.tls_status=");
    serial::puts(tlsStatus[0] ? tlsStatus : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.allowlist_mode=");
    serial::puts(tlsAllowlistLocalOnly ? "local-only controlled HTTPS" : "explicit-policy validated HTTPS");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.tls_backend=mbedtls");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.evidence_lane=kernel_local_fixture");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.tls_suite_contract=explicit_bounded");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.tls_suite_contract_count=");
    serial_put_dec((uint32_t)tlsResult.tlsSuiteContractCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.tls_suite_contract_real_count=");
    serial_put_dec((uint32_t)tlsResult.tlsSuiteContractRealCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.tls_suite_contract_installed=");
    serial::puts(tlsResult.tlsSuiteContractInstalled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.tls_clienthello_real_suite_count=");
    serial_put_dec((uint32_t)tlsResult.tlsClientHelloRealSuiteCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.tls_clienthello_scsv_only=");
    serial::puts(tlsResult.tlsClientHelloScsvOnly ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.tls_clienthello_contract_match=");
    serial::puts(tlsResult.tlsClientHelloCanonicalSuiteOffered ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.certificate_validated=");
    serial::puts(tlsValidated ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.hostname_validated=");
    serial::puts(tlsHostnameValidated ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.body_bytes=");
    serial_put_dec((uint32_t)bodyBytes);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.parsed_blocks=");
    serial_put_dec((uint32_t)parsedBlocks);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.protocol=");
    serial::puts(tlsProtocol[0] ? tlsProtocol : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.tls_protocol=");
    serial::puts(tlsProtocol[0] ? tlsProtocol : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.cipher_suite=");
    serial::puts(tlsCipherSuite[0] ? tlsCipherSuite : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.tls_negotiated_suite=");
    serial::puts(tlsCipherSuite[0] ? tlsCipherSuite : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.error=");
    serial::puts(error[0] ? error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated.result=");
    serial::puts(pass ? "PASS\n" : "FAIL\n");
    return pass;
}

static bool printNavigatorLocalTlsBlockedHostCase()
{
    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const bool policyEnabled = navigator_https_policy_effectively_enabled(httpsPolicy);
    const bool selectedButBlocked = navigator_https_policy_selected_but_blocked(httpsPolicy);
    const char* url = "https://wrong.guidexos.test:8443/navigator-smoke/tls-basic.html";
    char requestedUrl[160];
    int statusCode = 0;
    int bodyBytes = 0;
    int parsedBlocks = 0;
    int redirectCount = 0;
    int plainTcpConnectAttempts = 0;
    int tlsTcpConnectAttempts = 0;
    uint32_t verifyFlags = 0;
    char contentType[48];
    char error[128];
    char finalUrl[160];
    char tlsSniHost[64];
    char tlsProtocol[32];
    char tlsCipherSuite[64];
    char transportSelection[40];
    char tlsStatus[40];
    char sourceType[24];
    bool tlsValidated = false;
    bool tlsHostnameValidated = false;
    bool tlsAllowlistLocalOnly = false;
    NavigatorApp::smokeCaptureHttpsNavigation(
        url, requestedUrl, sizeof(requestedUrl), &statusCode,
        contentType, sizeof(contentType), &bodyBytes, &parsedBlocks, error, sizeof(error),
        finalUrl, sizeof(finalUrl), &redirectCount, &plainTcpConnectAttempts,
        &tlsTcpConnectAttempts, &verifyFlags, tlsSniHost, sizeof(tlsSniHost),
        tlsProtocol, sizeof(tlsProtocol), tlsCipherSuite, sizeof(tlsCipherSuite),
        transportSelection, sizeof(transportSelection), tlsStatus, sizeof(tlsStatus),
        &tlsValidated, &tlsHostnameValidated, &tlsAllowlistLocalOnly, sourceType, sizeof(sourceType));
    const bool hostRejected = nav_smoke_text_equals(tlsStatus, "CertificateVerifyFailed") ||
        nav_smoke_text_equals(tlsStatus, "HostnameMismatch");
    const bool failClosedPolicyBlocked = nav_smoke_text_equals(transportSelection, "BlockedPolicy") &&
        nav_smoke_text_equals(tlsStatus, "PolicyBlocked") &&
        plainTcpConnectAttempts == 0 &&
        tlsTcpConnectAttempts == 0 &&
        verifyFlags == 0 &&
        !tlsValidated &&
        !tlsHostnameValidated &&
        !tlsAllowlistLocalOnly &&
        error[0] != '\0';

    const bool pass = policyEnabled
        ? nav_smoke_text_equals(requestedUrl, url) &&
            nav_smoke_text_equals(finalUrl, url) &&
            statusCode == 0 &&
            redirectCount == 0 &&
            (failClosedPolicyBlocked ||
                (plainTcpConnectAttempts == 0 &&
                    tlsTcpConnectAttempts == 1 &&
                    verifyFlags != 0 &&
                    !tlsValidated &&
                    !tlsHostnameValidated &&
                    !tlsAllowlistLocalOnly &&
                    nav_smoke_text_equals(transportSelection, "PolicyValidatedTlsHttps") &&
                    hostRejected &&
                    error[0] != '\0'))
        : nav_smoke_text_equals(requestedUrl, url) &&
            nav_smoke_text_equals(finalUrl, url) &&
            statusCode == 0 &&
            redirectCount == 0 &&
            plainTcpConnectAttempts == 0 &&
            tlsTcpConnectAttempts == 0 &&
            verifyFlags == 0 &&
            !tlsValidated &&
            !tlsHostnameValidated &&
            !tlsAllowlistLocalOnly &&
            nav_smoke_text_equals(transportSelection, selectedButBlocked ? "BlockedPolicy" : "BlockedHttpsGeneral") &&
            nav_smoke_text_equals(tlsStatus, "PolicyBlocked") &&
            error[0] != '\0';

    serial::puts("[NAVIGATOR-SMOKE] https.case.local_scope_block.enabled=");
    serial::puts(policyEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.local_scope_block.requested_url=");
    serial::puts(requestedUrl[0] ? requestedUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.local_scope_block.final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.local_scope_block.plain_tcp_connect_attempts=");
    serial_put_dec((uint32_t)plainTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.local_scope_block.tls_tcp_connect_attempts=");
    serial_put_dec((uint32_t)tlsTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.local_scope_block.transport_selection=");
    serial::puts(transportSelection[0] ? transportSelection : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.local_scope_block.tls_status=");
    serial::puts(tlsStatus[0] ? tlsStatus : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.local_scope_block.error=");
    serial::puts(error[0] ? error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.local_scope_block.result=");
    serial::puts(pass ? "PASS\n" : "FAIL\n");
    return pass;
}

static bool printNavigatorPolicyValidatedScopeBlockedRedirectCase()
{
    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const bool policyEnabled = navigator_https_policy_effectively_enabled(httpsPolicy);
    const bool selectedButBlocked = navigator_https_policy_selected_but_blocked(httpsPolicy);
    const char* url = "http://10.0.2.2:8080/navigator-smoke/redirect-to-policy-disallowed-https";
    const char* expectedFinalUrl = "https://wrong.guidexos.test:8443/navigator-policy/ok.html";
    char requestedUrl[160];
    int statusCode = 0;
    int bodyBytes = 0;
    int parsedBlocks = 0;
    int redirectCount = 0;
    int plainTcpConnectAttempts = 0;
    int tlsTcpConnectAttempts = 0;
    uint32_t verifyFlags = 0;
    char contentType[48];
    char error[128];
    char finalUrl[160];
    char tlsSniHost[64];
    char tlsProtocol[32];
    char tlsCipherSuite[64];
    char transportSelection[40];
    char tlsStatus[40];
    char sourceType[24];
    bool tlsValidated = false;
    bool tlsHostnameValidated = false;
    bool tlsAllowlistLocalOnly = false;
    NavigatorApp::smokeCaptureHttpsNavigation(
        url, requestedUrl, sizeof(requestedUrl), &statusCode,
        contentType, sizeof(contentType), &bodyBytes, &parsedBlocks, error, sizeof(error),
        finalUrl, sizeof(finalUrl), &redirectCount, &plainTcpConnectAttempts,
        &tlsTcpConnectAttempts, &verifyFlags, tlsSniHost, sizeof(tlsSniHost),
        tlsProtocol, sizeof(tlsProtocol), tlsCipherSuite, sizeof(tlsCipherSuite),
        transportSelection, sizeof(transportSelection), tlsStatus, sizeof(tlsStatus),
        &tlsValidated, &tlsHostnameValidated, &tlsAllowlistLocalOnly, sourceType, sizeof(sourceType));
    const bool hostRejected = nav_smoke_text_equals(tlsStatus, "CertificateVerifyFailed") ||
        nav_smoke_text_equals(tlsStatus, "HostnameMismatch");

    const bool pass = policyEnabled
        ? nav_smoke_text_equals(requestedUrl, url) &&
            nav_smoke_text_equals(finalUrl, expectedFinalUrl) &&
            redirectCount == 1 &&
            plainTcpConnectAttempts == 1 &&
            tlsTcpConnectAttempts == 1 &&
            verifyFlags != 0 &&
            nav_smoke_text_equals(transportSelection, "PolicyValidatedTlsHttps") &&
            hostRejected &&
            error[0] != '\0'
        : nav_smoke_text_equals(requestedUrl, url) &&
            nav_smoke_text_equals(finalUrl, expectedFinalUrl) &&
            redirectCount == 1 &&
            plainTcpConnectAttempts == 1 &&
            tlsTcpConnectAttempts == 0 &&
            nav_smoke_text_equals(transportSelection, selectedButBlocked ? "BlockedPolicy" : "BlockedHttpsGeneral") &&
            nav_smoke_text_equals(tlsStatus, "PolicyBlocked") &&
            error[0] != '\0';

    serial::puts("[NAVIGATOR-SMOKE] https.case.policy_scope_redirect_block.enabled=");
    serial::puts(policyEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_scope_redirect_block.requested_url=");
    serial::puts(requestedUrl[0] ? requestedUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_scope_redirect_block.final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_scope_redirect_block.redirect_count=");
    serial_put_dec((uint32_t)redirectCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_scope_redirect_block.plain_tcp_connect_attempts=");
    serial_put_dec((uint32_t)plainTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_scope_redirect_block.tls_tcp_connect_attempts=");
    serial_put_dec((uint32_t)tlsTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_scope_redirect_block.transport_selection=");
    serial::puts(transportSelection[0] ? transportSelection : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_scope_redirect_block.tls_status=");
    serial::puts(tlsStatus[0] ? tlsStatus : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_scope_redirect_block.error=");
    serial::puts(error[0] ? error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_scope_redirect_block.result=");
    serial::puts(pass ? "PASS\n" : "FAIL\n");
    return pass;
}

static bool printNavigatorPolicyValidatedRedirectCase()
{
    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const bool policyEnabled = navigator_https_policy_effectively_enabled(httpsPolicy);
    const NavigatorHttpsSmokeFaultMode faultMode = navigator_https_smoke_fault_mode();
    const bool certFaultExpected = navigator_https_cert_fault_expected(faultMode);
    const char* url = "http://10.0.2.2:8080/navigator-smoke/redirect-to-policy-validated-https";
    char expectedFinalUrl[160];
    navigator_https_policy_url(httpsPolicy, "/navigator-policy/ok.html", expectedFinalUrl, sizeof(expectedFinalUrl));
    char requestedUrl[160];
    int statusCode = 0;
    int bodyBytes = 0;
    int parsedBlocks = 0;
    int redirectCount = 0;
    int plainTcpConnectAttempts = 0;
    int tlsTcpConnectAttempts = 0;
    uint32_t verifyFlags = 0;
    char contentType[48];
    char error[128];
    char finalUrl[160];
    char tlsSniHost[64];
    char tlsProtocol[32];
    char tlsCipherSuite[64];
    char transportSelection[40];
    char tlsStatus[40];
    char sourceType[24];
    bool tlsValidated = false;
    bool tlsHostnameValidated = false;
    bool tlsAllowlistLocalOnly = false;
    NavigatorApp::smokeCaptureHttpsNavigation(
        url, requestedUrl, sizeof(requestedUrl), &statusCode,
        contentType, sizeof(contentType), &bodyBytes, &parsedBlocks, error, sizeof(error),
        finalUrl, sizeof(finalUrl), &redirectCount, &plainTcpConnectAttempts,
        &tlsTcpConnectAttempts, &verifyFlags, tlsSniHost, sizeof(tlsSniHost),
        tlsProtocol, sizeof(tlsProtocol), tlsCipherSuite, sizeof(tlsCipherSuite),
        transportSelection, sizeof(transportSelection), tlsStatus, sizeof(tlsStatus),
        &tlsValidated, &tlsHostnameValidated, &tlsAllowlistLocalOnly, sourceType, sizeof(sourceType));

    const bool pass = policyEnabled
        ? (certFaultExpected
            ? nav_smoke_text_equals(requestedUrl, url) &&
                nav_smoke_text_equals(finalUrl, expectedFinalUrl) &&
                statusCode == 0 &&
                redirectCount == 1 &&
                plainTcpConnectAttempts == 1 &&
                tlsTcpConnectAttempts == 1 &&
                verifyFlags != 0 &&
                !tlsValidated &&
                !tlsAllowlistLocalOnly &&
                nav_smoke_text_equals(transportSelection, "PolicyValidatedTlsHttps") &&
                nav_smoke_text_equals(tlsStatus, "CertificateVerifyFailed") &&
                error[0] != '\0'
            : nav_smoke_text_equals(requestedUrl, url) &&
                nav_smoke_text_equals(finalUrl, expectedFinalUrl) &&
                statusCode == 200 &&
                redirectCount == 1 &&
                plainTcpConnectAttempts == 1 &&
                tlsTcpConnectAttempts == 1 &&
                verifyFlags == 0 &&
                tlsValidated &&
                tlsHostnameValidated &&
                !tlsAllowlistLocalOnly &&
                nav_smoke_text_equals(transportSelection, "PolicyValidatedTlsHttps") &&
                nav_smoke_text_equals(tlsStatus, "Success") &&
                nav_smoke_text_equals(sourceType, "https"))
        : nav_smoke_text_equals(requestedUrl, url) &&
            nav_smoke_text_equals(finalUrl, expectedFinalUrl) &&
            error[0] != '\0' &&
            redirectCount == 1 &&
            plainTcpConnectAttempts == 1 &&
            tlsTcpConnectAttempts == 0;

    serial::puts("[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.enabled=");
    serial::puts(policyEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.fault_mode=");
    serial::puts(navigator_https_smoke_fault_mode_name(faultMode));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.requested_url=");
    serial::puts(requestedUrl[0] ? requestedUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.http_status=");
    serial_put_dec((uint32_t)statusCode);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.redirect_count=");
    serial_put_dec((uint32_t)redirectCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.plain_tcp_connect_attempts=");
    serial_put_dec((uint32_t)plainTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.tls_tcp_connect_attempts=");
    serial_put_dec((uint32_t)tlsTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.verify_flags=");
    serial_put_dec64((uint64_t)verifyFlags);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.sni_host=");
    serial::puts(tlsSniHost[0] ? tlsSniHost : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.transport_selection=");
    serial::puts(transportSelection[0] ? transportSelection : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.tls_status=");
    serial::puts(tlsStatus[0] ? tlsStatus : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.error=");
    serial::puts(error[0] ? error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_redirect.result=");
    serial::puts(pass ? "PASS\n" : "FAIL\n");
    return pass;
}

static bool printNavigatorPolicyValidatedWrongHostnameFailureCase()
{
    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const bool policyEnabled = navigator_https_policy_effectively_enabled(httpsPolicy);
    const NavigatorHttpsSmokeFaultMode faultMode = navigator_https_smoke_fault_mode();
    const bool certFaultExpected = navigator_https_cert_fault_expected(faultMode);
    char url[160];
    navigator_https_policy_url(httpsPolicy, "/navigator-policy/ok.html", url, sizeof(url));
    KernelHttpResponse response{};
    gxos::GxosTlsLocalHandshakeResult tlsResult{};
    const bool requestOk = kernel_tls_smoke_request_once(url, "wrong.guidexos.test", &response, &tlsResult);
    const bool hostnameFailed = !tlsResult.hostnameValidationSuccess &&
        ((tlsResult.verifyFlags & kNavigatorTlsSmokeCnMismatchFlag) != 0 ||
         nav_smoke_text_equals(tlsResult.error, "TLS smoke hostname validation failed.") ||
         nav_smoke_text_equals(tlsResult.error, "TLS hostname validation failed."));
    const bool pass = policyEnabled
        ? (certFaultExpected
            ? !requestOk &&
                tlsResult.attempted &&
                tlsResult.tcpConnected &&
                tlsResult.usedSniHostname &&
                nav_smoke_text_equals(tlsResult.sniHost, "wrong.guidexos.test") &&
                !tlsResult.certificateValidationSuccess &&
                tlsResult.verifyFlags != 0
            : !requestOk &&
                tlsResult.attempted &&
                tlsResult.tcpConnected &&
                tlsResult.usedSniHostname &&
                nav_smoke_text_equals(tlsResult.sniHost, "wrong.guidexos.test") &&
                !tlsResult.certificateValidationSuccess &&
                hostnameFailed)
        : !requestOk && !tlsResult.attempted;

    serial::puts("[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.enabled=");
    serial::puts(policyEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.fault_mode=");
    serial::puts(navigator_https_smoke_fault_mode_name(faultMode));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.url=");
    serial::puts(url);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.attempted=");
    serial::puts(tlsResult.attempted ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.tcp_connect=");
    serial::puts(tlsResult.tcpConnected ? "success" : "failure");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.handshake=");
    serial::puts(tlsResult.handshakeSuccess ? "success" : "failure");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.validation=");
    serial::puts(tlsResult.certificateValidationSuccess ? "success" : "failure");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.hostname_validation=");
    serial::puts(tlsResult.hostnameValidationSuccess ? "success" : "failure");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.sni_host=");
    serial::puts(tlsResult.sniHost[0] ? tlsResult.sniHost : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.verify_flags=");
    serial_put_dec64((uint64_t)tlsResult.verifyFlags);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.error=");
    serial::puts(tlsResult.error[0] ? tlsResult.error : (response.error[0] ? response.error : "(none)"));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_wrong_host.result=");
    serial::puts(pass ? "PASS\n" : "FAIL\n");
    return pass;
}

static bool printNavigatorPolicyValidatedDowngradeCase()
{
    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const bool policyEnabled = navigator_https_policy_effectively_enabled(httpsPolicy);
    const NavigatorHttpsSmokeFaultMode faultMode = navigator_https_smoke_fault_mode();
    const bool certFaultExpected = navigator_https_cert_fault_expected(faultMode);
    char url[160];
    navigator_https_policy_url(httpsPolicy, "/navigator-policy/redirect-downgrade", url, sizeof(url));
    char requestedUrl[160];
    int statusCode = 0;
    int bodyBytes = 0;
    int parsedBlocks = 0;
    int redirectCount = 0;
    int plainTcpConnectAttempts = 0;
    int tlsTcpConnectAttempts = 0;
    uint32_t verifyFlags = 0;
    char contentType[48];
    char error[128];
    char finalUrl[160];
    char tlsSniHost[64];
    char tlsProtocol[32];
    char tlsCipherSuite[64];
    char transportSelection[40];
    char tlsStatus[40];
    char sourceType[24];
    bool tlsValidated = false;
    bool tlsHostnameValidated = false;
    bool tlsAllowlistLocalOnly = false;
    NavigatorApp::smokeCaptureHttpsNavigation(
        url, requestedUrl, sizeof(requestedUrl), &statusCode,
        contentType, sizeof(contentType), &bodyBytes, &parsedBlocks, error, sizeof(error),
        finalUrl, sizeof(finalUrl), &redirectCount, &plainTcpConnectAttempts,
        &tlsTcpConnectAttempts, &verifyFlags, tlsSniHost, sizeof(tlsSniHost),
        tlsProtocol, sizeof(tlsProtocol), tlsCipherSuite, sizeof(tlsCipherSuite),
        transportSelection, sizeof(transportSelection), tlsStatus, sizeof(tlsStatus),
        &tlsValidated, &tlsHostnameValidated, &tlsAllowlistLocalOnly, sourceType, sizeof(sourceType));

    const bool pass = policyEnabled
        ? (certFaultExpected
            ? nav_smoke_text_equals(requestedUrl, url) &&
                nav_smoke_text_equals(finalUrl, url) &&
                redirectCount == 0 &&
                plainTcpConnectAttempts == 0 &&
                tlsTcpConnectAttempts == 1 &&
                nav_smoke_text_equals(transportSelection, "PolicyValidatedTlsHttps") &&
                nav_smoke_text_equals(tlsStatus, "CertificateVerifyFailed") &&
                error[0] != '\0'
            : nav_smoke_text_equals(requestedUrl, url) &&
                nav_smoke_text_equals(finalUrl, "http://10.0.2.2:8080/navigator-smoke/insecure-downgrade") &&
                redirectCount == 1 &&
                plainTcpConnectAttempts == 0 &&
                tlsTcpConnectAttempts == 1 &&
                nav_smoke_text_equals(transportSelection, "BlockedPolicy") &&
                nav_smoke_text_equals(tlsStatus, "PolicyBlocked") &&
                nav_smoke_text_equals(error, "HTTPS downgrade redirect blocked"))
        : nav_smoke_text_equals(requestedUrl, url) &&
            nav_smoke_text_equals(finalUrl, url) &&
            plainTcpConnectAttempts == 0 &&
            tlsTcpConnectAttempts == 0 &&
            error[0] != '\0';

    serial::puts("[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.enabled=");
    serial::puts(policyEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.fault_mode=");
    serial::puts(navigator_https_smoke_fault_mode_name(faultMode));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.requested_url=");
    serial::puts(requestedUrl[0] ? requestedUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.redirect_count=");
    serial_put_dec((uint32_t)redirectCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.plain_tcp_connect_attempts=");
    serial_put_dec((uint32_t)plainTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.tls_tcp_connect_attempts=");
    serial_put_dec((uint32_t)tlsTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.transport_selection=");
    serial::puts(transportSelection[0] ? transportSelection : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.tls_status=");
    serial::puts(tlsStatus[0] ? tlsStatus : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.error=");
    serial::puts(error[0] ? error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.policy_validated_downgrade.result=");
    serial::puts(pass ? "PASS\n" : "FAIL\n");
    return pass;
}

static bool printNavigatorPublicPilotDecisionCase()
{
    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const bool pilotEnabled =
        httpsPolicy.state == gxos::GxosValidatedHttpsPolicyState::ProductionValidated &&
        httpsPolicy.broadPublicHttpsEnabled;
    const char* url = kNavigatorRealPublicProbeDefaultTarget;
    KernelHttpUrl parsed{};
    char reason[160];
    reason[0] = '\0';
    const bool parseOk = parse_https_url_kernel(url, &parsed);
    const gxos::web::HttpTransportPolicyDecision policy = parseOk
        ? kernel_http_transport_policy_for_https(parsed, reason, sizeof(reason))
        : kernel_http_blocked_https_transport_policy("HTTPS/TLS unsupported: public pilot decision URL could not be parsed");
    const bool allow = policy.selection == gxos::web::HttpByteStreamTransportSelection::PolicyValidatedTlsHttps;
    const bool pass = parseOk &&
        (allow == pilotEnabled) &&
        (pilotEnabled
            ? nav_smoke_text_equals(reason, "ProductionValidated arbitrary-origin HTTPS matched.")
            : reason[0] != '\0');

    serial::puts("[NAVIGATOR-SMOKE] https.case.public_pilot_decision.enabled=");
    serial::puts(pilotEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_decision.requested_url=");
    serial::puts(url);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_decision.transport_selection=");
    serial::puts(gxos::web::httpSharedTransportSelectionName(policy.selection));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_decision.reason=");
    serial::puts(reason[0] ? reason : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_decision.result=");
    serial::puts(pass ? "PASS\n" : "FAIL\n");
    return pass;
}

static bool printNavigatorRealPublicHttpsProbeCase()
{
    const NavigatorRealPublicProbeConfig probeConfig = navigator_real_public_probe_config();
    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const gxos::GxosTrustStorePolicyInfo trustStorePolicy = gxos::gxos_tls_trust_store_policy_info();
    const gxos::GxosCaStoreInfo caStoreInfo = gxos::gxos_ca_store_info();
    const bool pilotEnabled =
        httpsPolicy.state == gxos::GxosValidatedHttpsPolicyState::ProductionValidated &&
        httpsPolicy.broadPublicHttpsEnabled;
    const char* prerequisiteBlocker =
        navigator_real_public_probe_prerequisite_blocker(httpsPolicy, trustStorePolicy, pilotEnabled);

    char requestedUrl[160] = {};
    char finalUrl[160] = {};
    char contentType[48] = {};
    char contentEncoding[32] = {};
    char error[128] = {};
    char dnsHost[64] = {};
    char dnsResolvedIp[16] = {};
    char dnsError[64] = {};
    char tlsSniHost[64] = {};
    char tlsProtocol[32] = {};
    char tlsCipherSuite[64] = {};
    char transportSelection[40] = {};
    char tlsStatus[40] = {};
    char sourceType[24] = {};
    char tlsBackend[48] = {};
    char transportPolicyReason[128] = {};
    char unsupportedReason[128] = {};
    char tlsRetryReason[96] = {};
    char redirectHopUrl[160] = {};
    int statusCode = 0;
    int bodyBytes = 0;
    int parsedBlocks = 0;
    int redirectCount = 0;
    int redirectHopIndex = 0;
    int plainTcpConnectAttempts = 0;
    int tlsTcpConnectAttempts = 0;
    int tlsHandshakeErrorCode = 0;
    int tlsTransportErrorCode = 0;
    int tlsRequestBytesWritten = 0;
    int tlsResponseBytesRead = 0;
    int tlsRetryCount = 0;
    int tlsBytesWrittenBeforeRetry = 0;
    uint32_t verifyFlags = 0;
    bool tlsValidated = false;
    bool tlsHostnameValidated = false;
    bool tlsAllowlistLocalOnly = false;
    bool dnsUsed = false;
    bool headerCapHit = false;
    bool bodyCapHit = false;
    bool downgradeRedirectBlocked = false;
    bool tlsSucceededBeforeContentFailure = false;
    bool tcpAbortUsed = false;
    bool redirectedHttpsRetryUsed = false;
    bool attempted = false;
    KernelHttpResponse tlsProbeResponse{};
    gxos::GxosTlsLocalHandshakeResult tlsResult{};
    const char* resultLabel = "SKIP";
    const char* skipReason = "(none)";
    bool pass = true;

    serial::puts("[NAVIGATOR-SMOKE] https.case.real_public_probe.enabled=");
    serial::puts(probeConfig.enabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.required=");
    serial::puts(probeConfig.requireSuccess ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.target=");
    serial::puts(probeConfig.targetUrl[0] ? probeConfig.targetUrl : kNavigatorRealPublicProbeDefaultTarget);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.reviewed_target_policy=");
    serial::puts(probeConfig.reviewedTargetPolicy[0] ? probeConfig.reviewedTargetPolicy : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.reviewed_target_allowlist=");
    serial::puts(kNavigatorRealPublicProbeReviewedAllowlistName);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.reviewed_target_match=");
    serial::puts(probeConfig.reviewedTargetMatched ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.reviewed_target_override=");
    serial::puts(probeConfig.reviewedOverrideEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.reviewed_target_reason=");
    serial::puts(probeConfig.reviewedTargetReason[0] ? probeConfig.reviewedTargetReason : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.policy_enabled=");
    serial::puts(pilotEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.policy_blocker=");
    serial::puts(pilotEnabled ? "(none)" : (prerequisiteBlocker ? prerequisiteBlocker : "production_public_pilot_enabled requires ProductionValidated trust prerequisites."));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.policy_config_path=");
    serial::puts(httpsPolicy.configPath ? httpsPolicy.configPath : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.policy_config_source=");
    serial::puts(httpsPolicy.configSource ? httpsPolicy.configSource : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_pilot_token_present=");
    serial::puts(httpsPolicy.publicHttpsPilotRequested ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_pilot_token_path=");
    serial::puts(httpsPolicy.configPath ? httpsPolicy.configPath : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_pilot_token_value=");
    serial::puts(httpsPolicy.publicHttpsPilotRequested ? "enabled" : "disabled");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_proof_lane_active=");
    serial::puts(probeConfig.enabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.scenario_group=");
    serial::puts("PublicPilot");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.scenario_name=");
    serial::puts("production_public_pilot_enabled");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_trust_ready=");
    serial::puts(trustStorePolicy.publicInternetReady ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_trust_source_allowed=");
    serial::puts(trustStorePolicy.source == gxos::GxosTrustStoreSource::ProductionPublicProbeTrust ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_trust_manifest_ready=");
    serial::puts((trustStorePolicy.source == gxos::GxosTrustStoreSource::ProductionPublicProbeTrust &&
        trustStorePolicy.productionReady && !trustStorePolicy.smokeTestOnly) ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_trust_runtime_hash_match=");
    serial::puts(caStoreInfo.manifest.hashMatch ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_trust_test_only=");
    serial::puts(caStoreInfo.manifest.testOnly ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_trust_source_marker=");
    serial::puts(probeConfig.publicCaSourcePath[0] ? probeConfig.publicCaSourcePath : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_trust_lane=");
    serial::puts("dedicated-reviewed-public-proof");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_trust_reason=");
    serial::puts(trustStorePolicy.publicInternetReady ? "Public trust readiness satisfied for the reviewed public proof lane." :
        (prerequisiteBlocker ? prerequisiteBlocker : "public trust readiness blocked before network attempt."));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_trust_blocker=");
    serial::puts(trustStorePolicy.publicInternetReady ? "(none)" :
        (prerequisiteBlocker ? prerequisiteBlocker : "public trust readiness blocked before network attempt."));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_ca_bundle_source=");
    serial::puts(probeConfig.publicCaSourcePath[0] ? probeConfig.publicCaSourcePath : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_ca_bytes=");
    serial_put_dec64(static_cast<uint64_t>(probeConfig.publicCaBytes));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_ca_parsed_certs=");
    serial_put_dec64(static_cast<uint64_t>(probeConfig.publicCaParsedCertCount));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_present=");
    serial::puts(caStoreInfo.manifest.present ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_hash_match=");
    serial::puts(caStoreInfo.manifest.hashMatch ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_bundle_type=");
    serial::puts(caStoreInfo.manifest.bundleType ? caStoreInfo.manifest.bundleType : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_rotation_id=");
    serial::puts(caStoreInfo.manifest.rotationId ? caStoreInfo.manifest.rotationId : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_production_ready=");
    serial::puts(caStoreInfo.manifest.productionReady ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_test_only=");
    serial::puts(caStoreInfo.manifest.testOnly ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_root_count=");
    serial_put_dec64(static_cast<uint64_t>(caStoreInfo.manifest.rootCount));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_sha256=");
    serial::puts(caStoreInfo.manifest.manifestSha256 ? caStoreInfo.manifest.manifestSha256 : "(none)");
    serial::puts("\n");

    if (!probeConfig.enabled) {
        skipReason = "Opt-in real public HTTPS probe is disabled.";
    } else if (!probeConfig.targetValid) {
        skipReason = probeConfig.targetError[0]
            ? probeConfig.targetError
            : "Real public HTTPS probe target is invalid.";
        if (probeConfig.requireSuccess) {
            resultLabel = "FAIL";
            pass = false;
        }
    } else if (prerequisiteBlocker) {
        skipReason = prerequisiteBlocker;
        if (probeConfig.requireSuccess) {
            resultLabel = "FAIL";
            pass = false;
        }
    } else {
        attempted = true;
        serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.attempted=yes\n");
        NavigatorApp::smokeCaptureHttpsNavigation(
            probeConfig.targetUrl,
            requestedUrl, sizeof(requestedUrl),
            &statusCode,
            contentType, sizeof(contentType),
            &bodyBytes,
            &parsedBlocks,
            error, sizeof(error),
            finalUrl, sizeof(finalUrl),
            &redirectCount,
            &plainTcpConnectAttempts,
            &tlsTcpConnectAttempts,
            &verifyFlags,
            tlsSniHost, sizeof(tlsSniHost),
            tlsProtocol, sizeof(tlsProtocol),
            tlsCipherSuite, sizeof(tlsCipherSuite),
            transportSelection, sizeof(transportSelection),
            tlsStatus, sizeof(tlsStatus),
            &tlsValidated,
            &tlsHostnameValidated,
            &tlsAllowlistLocalOnly,
            sourceType, sizeof(sourceType),
            contentEncoding, sizeof(contentEncoding),
            &dnsUsed,
            dnsHost, sizeof(dnsHost),
            dnsResolvedIp, sizeof(dnsResolvedIp),
            dnsError, sizeof(dnsError),
            tlsBackend, sizeof(tlsBackend),
            transportPolicyReason, sizeof(transportPolicyReason),
            unsupportedReason, sizeof(unsupportedReason),
            &headerCapHit,
            &bodyCapHit,
            &downgradeRedirectBlocked,
            &tlsSucceededBeforeContentFailure,
            &tlsHandshakeErrorCode,
            &tlsTransportErrorCode,
            &tlsRequestBytesWritten,
            &tlsResponseBytesRead,
            &tlsRetryCount,
            tlsRetryReason, sizeof(tlsRetryReason),
            &tlsBytesWrittenBeforeRetry,
            &tcpAbortUsed,
            &redirectedHttpsRetryUsed,
            &redirectHopIndex,
            redirectHopUrl, sizeof(redirectHopUrl));
        kernel_tls_smoke_request_once(probeConfig.targetUrl, tlsSniHost, &tlsProbeResponse, &tlsResult);

        const bool tlsSuccess =
            tlsTcpConnectAttempts >= 1 &&
            verifyFlags == 0 &&
            tlsValidated &&
            tlsHostnameValidated &&
            !tlsAllowlistLocalOnly &&
            nav_smoke_text_equals(transportSelection, "PolicyValidatedTlsHttps") &&
            nav_smoke_text_equals(tlsStatus, "Success") &&
            nav_smoke_text_equals(sourceType, "https");
        const bool contentLimitedAfterTls =
            tlsSucceededBeforeContentFailure &&
            (unsupportedReason[0] || headerCapHit || bodyCapHit);
        const bool environmentBlocked =
            navigator_real_public_probe_environment_blocked(error, tlsStatus, dnsError);

        if (tlsSuccess && (statusCode > 0 || contentLimitedAfterTls)) {
            resultLabel = "PASS";
            pass = true;
        } else if (environmentBlocked && !probeConfig.requireSuccess) {
            resultLabel = "SKIP";
            skipReason = error[0] ? error : (dnsError[0] ? dnsError : "Outbound public HTTPS environment is unavailable.");
            pass = true;
        } else {
            resultLabel = "FAIL";
            pass = false;
        }
    }

    const char* dnsResult = !attempted
        ? "not-attempted"
        : (dnsError[0]
            ? "FAIL"
            : ((dnsUsed && dnsResolvedIp[0]) ? "PASS" : "FAIL"));
    const bool tcpConnected =
        tlsTcpConnectAttempts >= 1 &&
        !nav_smoke_text_equals(tlsStatus, "TcpConnectFailed") &&
        !nav_smoke_text_equals(tlsStatus, "NetworkUnavailable");
    const char* tcpResult = !attempted
        ? "not-attempted"
        : (tcpConnected ? "PASS" : "FAIL");
    const char* tlsResultLabel = !attempted
        ? "not-attempted"
        : (nav_smoke_text_equals(tlsStatus, "Success") ? "PASS" : "FAIL");
    const char* certificateValidationResult = !attempted
        ? "not-attempted"
        : (tlsValidated ? "PASS" : "FAIL");
    const char* hostnameValidationResult = !attempted
        ? "not-attempted"
        : (tlsHostnameValidated ? "PASS" : "FAIL");
    const kernel::dns::QueryDiagnostics* dnsDiagnostics =
        kernel::dns::get_last_query_diagnostics();

    serial::puts("[NAVIGATOR-SMOKE] https.case.real_public_probe.enabled=");
    serial::puts(probeConfig.enabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.required=");
    serial::puts(probeConfig.requireSuccess ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.target=");
    serial::puts(probeConfig.targetUrl[0] ? probeConfig.targetUrl : kNavigatorRealPublicProbeDefaultTarget);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.reviewed_target_policy=");
    serial::puts(probeConfig.reviewedTargetPolicy[0] ? probeConfig.reviewedTargetPolicy : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.reviewed_target_allowlist=");
    serial::puts(kNavigatorRealPublicProbeReviewedAllowlistName);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.reviewed_target_match=");
    serial::puts(probeConfig.reviewedTargetMatched ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.reviewed_target_override=");
    serial::puts(probeConfig.reviewedOverrideEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.reviewed_target_reason=");
    serial::puts(probeConfig.reviewedTargetReason[0] ? probeConfig.reviewedTargetReason : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.policy_enabled=");
    serial::puts(pilotEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_trust_ready=");
    serial::puts(trustStorePolicy.publicInternetReady ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_ca_bundle_source=");
    serial::puts(probeConfig.publicCaSourcePath[0] ? probeConfig.publicCaSourcePath : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_ca_bytes=");
    serial_put_dec64(static_cast<uint64_t>(probeConfig.publicCaBytes));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.public_ca_parsed_certs=");
    serial_put_dec64(static_cast<uint64_t>(probeConfig.publicCaParsedCertCount));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_present=");
    serial::puts(caStoreInfo.manifest.present ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_hash_match=");
    serial::puts(caStoreInfo.manifest.hashMatch ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_bundle_type=");
    serial::puts(caStoreInfo.manifest.bundleType ? caStoreInfo.manifest.bundleType : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_rotation_id=");
    serial::puts(caStoreInfo.manifest.rotationId ? caStoreInfo.manifest.rotationId : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_production_ready=");
    serial::puts(caStoreInfo.manifest.productionReady ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_test_only=");
    serial::puts(caStoreInfo.manifest.testOnly ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_root_count=");
    serial_put_dec64(static_cast<uint64_t>(caStoreInfo.manifest.rootCount));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.runtime_manifest_sha256=");
    serial::puts(caStoreInfo.manifest.manifestSha256 ? caStoreInfo.manifest.manifestSha256 : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.attempted=");
    serial::puts(attempted ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.requested_url=");
    serial::puts(requestedUrl[0] ? requestedUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.redirect_count=");
    serial_put_dec((uint32_t)redirectCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_used=");
    serial::puts(dnsUsed ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_host=");
    serial::puts(dnsHost[0] ? dnsHost : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_resolved_ip=");
    serial::puts(dnsResolvedIp[0] ? dnsResolvedIp : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_error=");
    serial::puts(dnsError[0] ? dnsError : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_result=");
    serial::puts(dnsResult);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_server=");
    {
        char dnsServer[16];
        kernel::ipv4::ip_to_string(dnsDiagnostics->serverIP, dnsServer);
        serial::puts(dnsServer);
    }
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_query_id=");
    serial_put_dec(dnsDiagnostics->queryId);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_source_port=");
    serial_put_dec(dnsDiagnostics->sourcePort);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_destination_port=");
    serial_put_dec(dnsDiagnostics->destinationPort);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_query_bytes=");
    serial_put_dec(dnsDiagnostics->queryBytes);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_send_attempts=");
    serial_put_dec(dnsDiagnostics->sendAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_last_send_result=");
    {
        char signedNumber[32];
        nav_i64_to_text((int64_t)dnsDiagnostics->lastSendResult, signedNumber, sizeof(signedNumber));
        serial::puts(signedNumber);
    }
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_reply_bytes=");
    serial_put_dec(dnsDiagnostics->replyBytes);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_reply_rcode=");
    serial_put_dec(dnsDiagnostics->replyRcode);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_reply_answer_count=");
    serial_put_dec(dnsDiagnostics->replyAnswerCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_ipv4_rx_packets=");
    serial_put_dec(dnsDiagnostics->ipv4RxPackets);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_ipv4_rx_errors=");
    serial_put_dec(dnsDiagnostics->ipv4RxErrors);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_ipv4_checksum_errors=");
    serial_put_dec(dnsDiagnostics->ipv4ChecksumErrors);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_udp_rx_datagrams=");
    serial_put_dec(dnsDiagnostics->udpRxDatagrams);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_udp_rx_errors=");
    serial_put_dec(dnsDiagnostics->udpRxErrors);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_udp_checksum_errors=");
    serial_put_dec(dnsDiagnostics->udpChecksumErrors);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.dns_udp_no_port_errors=");
    serial_put_dec(dnsDiagnostics->udpNoPortErrors);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.plain_tcp_connect_attempts=");
    serial_put_dec((uint32_t)plainTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_tcp_connect_attempts=");
    serial_put_dec((uint32_t)tlsTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tcp_result=");
    serial::puts(tcpResult);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.transport_selection=");
    serial::puts(transportSelection[0] ? transportSelection : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.transport_policy_reason=");
    serial::puts(transportPolicyReason[0] ? transportPolicyReason : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_status=");
    serial::puts(tlsStatus[0] ? tlsStatus : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_setup_step=");
    serial::puts(attempted ? (tlsResult.tlsSetupStep[0] ? tlsResult.tlsSetupStep : "(none)") : "(not-attempted)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_setup_error_code=");
    {
        char signedNumber[32];
        nav_i64_to_text((int64_t)tlsResult.tlsSetupErrorCode, signedNumber, sizeof(signedNumber));
        serial::puts(signedNumber);
    }
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_setup_error_name=");
    serial::puts(tlsResult.tlsSetupErrorName[0] ? tlsResult.tlsSetupErrorName : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.psa_init_status=");
    serial::puts(gxos::gxos_tls_hook_status_name(tlsResult.psaInitStatus));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.rng_callback_status=");
    serial::puts(gxos::gxos_tls_hook_status_name(tlsResult.rngCallbackStatus));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.time_callback_status=");
    serial::puts(gxos::gxos_tls_hook_status_name(tlsResult.timeCallbackStatus));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.ca_chain_ready=");
    serial::puts(tlsResult.caChainReady ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.ca_chain_cert_count=");
    serial_put_dec64(static_cast<uint64_t>(tlsResult.caChainCertCount));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.ssl_config_defaults_status=");
    {
        char signedNumber[32];
        nav_i64_to_text((int64_t)tlsResult.sslConfigDefaultsStatus, signedNumber, sizeof(signedNumber));
        serial::puts(signedNumber);
    }
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.ssl_setup_status=");
    {
        char signedNumber[32];
        nav_i64_to_text((int64_t)tlsResult.sslSetupStatus, signedNumber, sizeof(signedNumber));
        serial::puts(signedNumber);
    }
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.ssl_hostname_status=");
    {
        char signedNumber[32];
        nav_i64_to_text((int64_t)tlsResult.sslHostnameStatus, signedNumber, sizeof(signedNumber));
        serial::puts(signedNumber);
    }
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.ssl_bio_status=");
    {
        char signedNumber[32];
        nav_i64_to_text((int64_t)tlsResult.sslBioStatus, signedNumber, sizeof(signedNumber));
        serial::puts(signedNumber);
    }
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.ssl_authmode=");
    serial_put_dec((uint32_t)tlsResult.sslAuthmode);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.ssl_endpoint_mode=");
    serial_put_dec((uint32_t)tlsResult.sslEndpointMode);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.ssl_transport_mode=");
    serial_put_dec((uint32_t)tlsResult.sslTransportMode);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_result=");
    serial::puts(tlsResultLabel);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_connect_attempts=");
    serial_put_dec((uint32_t)tlsTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_retry_count=");
    serial_put_dec((uint32_t)(tlsRetryCount > 0 ? tlsRetryCount : 0));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_retry_reason=");
    serial::puts(tlsRetryReason[0] ? tlsRetryReason : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_bytes_written_before_retry=");
    serial_put_dec((uint32_t)(tlsBytesWrittenBeforeRetry > 0 ? tlsBytesWrittenBeforeRetry : 0));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_handshake_error_code=");
    {
        char signedNumber[32];
        nav_i64_to_text((int64_t)tlsHandshakeErrorCode, signedNumber, sizeof(signedNumber));
        serial::puts(signedNumber);
    }
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_handshake_error_name=");
    serial::puts(tlsResult.tlsHandshakeErrorName[0] ? tlsResult.tlsHandshakeErrorName : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_transport_error_code=");
    {
        char signedNumber[32];
        nav_i64_to_text((int64_t)tlsTransportErrorCode, signedNumber, sizeof(signedNumber));
        serial::puts(signedNumber);
    }
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_request_bytes_written=");
    serial_put_dec((uint32_t)(tlsRequestBytesWritten > 0 ? tlsRequestBytesWritten : 0));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_response_bytes_read=");
    serial_put_dec((uint32_t)(tlsResponseBytesRead > 0 ? tlsResponseBytesRead : 0));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_bio_send_calls=");
    serial_put_dec64((uint64_t)tlsResult.tlsBioSendCalls);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_bio_recv_calls=");
    serial_put_dec64((uint64_t)tlsResult.tlsBioRecvCalls);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_bio_bytes_sent=");
    serial_put_dec64((uint64_t)tlsResult.tlsBioBytesSent);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_bio_bytes_received=");
    serial_put_dec64((uint64_t)tlsResult.tlsBioBytesReceived);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_bio_last_send_result=");
    {
        char signedNumber[32];
        nav_i64_to_text((int64_t)tlsResult.tlsBioLastSendResult, signedNumber, sizeof(signedNumber));
        serial::puts(signedNumber);
    }
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_bio_last_recv_result=");
    {
        char signedNumber[32];
        nav_i64_to_text((int64_t)tlsResult.tlsBioLastRecvResult, signedNumber, sizeof(signedNumber));
        serial::puts(signedNumber);
    }
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_handshake_elapsed_ms=");
    serial_put_dec64((uint64_t)tlsResult.tlsHandshakeElapsedMs);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_validated=");
    serial::puts(tlsValidated ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.certificate_validation_result=");
    serial::puts(certificateValidationResult);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.hostname_validated=");
    serial::puts(tlsHostnameValidated ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.hostname_validation_result=");
    serial::puts(hostnameValidationResult);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.verify_flags=");
    serial_put_dec64((uint64_t)verifyFlags);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_backend=");
    serial::puts(tlsBackend[0] ? tlsBackend : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.evidence_lane=kernel_public_https");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_suite_contract=explicit_bounded");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_suite_contract_count=");
    serial_put_dec64((uint64_t)tlsResult.tlsSuiteContractCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_suite_contract_real_count=");
    serial_put_dec64((uint64_t)tlsResult.tlsSuiteContractRealCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_suite_contract_installed=");
    serial::puts(tlsResult.tlsSuiteContractInstalled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_clienthello_real_suite_count=");
    serial_put_dec64((uint64_t)tlsResult.tlsClientHelloRealSuiteCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_clienthello_scsv_only=");
    serial::puts(tlsResult.tlsClientHelloScsvOnly ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_clienthello_contract_match=");
    serial::puts(tlsResult.tlsClientHelloCanonicalSuiteOffered ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.sni_host=");
    serial::puts(tlsSniHost[0] ? tlsSniHost : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.protocol=");
    serial::puts(tlsProtocol[0] ? tlsProtocol : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_protocol=");
    serial::puts(tlsProtocol[0] ? tlsProtocol : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.cipher_suite=");
    serial::puts(tlsCipherSuite[0] ? tlsCipherSuite : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_negotiated_suite=");
    serial::puts(tlsCipherSuite[0] ? tlsCipherSuite : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tcp_abort_used=");
    serial::puts(tcpAbortUsed ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.redirected_https_retry_used=");
    serial::puts(redirectedHttpsRetryUsed ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.redirect_hop_index=");
    serial_put_dec((uint32_t)(redirectHopIndex > 0 ? redirectHopIndex : 0));
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.redirect_hop_url=");
    serial::puts(redirectHopUrl[0] ? redirectHopUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.http_status=");
    serial_put_dec((uint32_t)statusCode);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.body_bytes=");
    serial_put_dec((uint32_t)bodyBytes);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.parsed_blocks=");
    serial_put_dec((uint32_t)parsedBlocks);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.content_type=");
    serial::puts(contentType[0] ? contentType : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.content_encoding=");
    serial::puts(contentEncoding[0] ? contentEncoding : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.source_type=");
    serial::puts(sourceType[0] ? sourceType : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.header_cap_hit=");
    serial::puts(headerCapHit ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.body_cap_hit=");
    serial::puts(bodyCapHit ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.downgrade_blocked=");
    serial::puts(downgradeRedirectBlocked ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.tls_succeeded_before_content_failure=");
    serial::puts(tlsSucceededBeforeContentFailure ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.unsupported_reason=");
    serial::puts(unsupportedReason[0] ? unsupportedReason : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.error=");
    serial::puts(error[0] ? error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.skip_reason=");
    serial::puts(nav_smoke_text_equals(resultLabel, "SKIP") ? skipReason : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.plaintext_fallback=no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.real_public_probe.result=");
    serial::puts(pass ? resultLabel : "FAIL");
    serial::puts("\n");
    return pass;
}

static bool printNavigatorPublicPilotFixtureCase()
{
    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const bool pilotEnabled =
        httpsPolicy.state == gxos::GxosValidatedHttpsPolicyState::ProductionValidated &&
        httpsPolicy.broadPublicHttpsEnabled;
    char url[160];
    navigator_public_pilot_fixture_url(url, sizeof(url), "/navigator-public-pilot/ok.html");
    char requestedUrl[160];
    int statusCode = 0;
    int bodyBytes = 0;
    int parsedBlocks = 0;
    int redirectCount = 0;
    int plainTcpConnectAttempts = 0;
    int tlsTcpConnectAttempts = 0;
    uint32_t verifyFlags = 0;
    char contentType[48];
    char error[128];
    char finalUrl[160];
    char tlsSniHost[64];
    char tlsProtocol[32];
    char tlsCipherSuite[64];
    char transportSelection[40];
    char tlsStatus[40];
    char sourceType[24];
    bool tlsValidated = false;
    bool tlsHostnameValidated = false;
    bool tlsAllowlistLocalOnly = false;
    NavigatorApp::smokeCaptureHttpsNavigation(
        url, requestedUrl, sizeof(requestedUrl), &statusCode,
        contentType, sizeof(contentType), &bodyBytes, &parsedBlocks, error, sizeof(error),
        finalUrl, sizeof(finalUrl), &redirectCount, &plainTcpConnectAttempts,
        &tlsTcpConnectAttempts, &verifyFlags, tlsSniHost, sizeof(tlsSniHost),
        tlsProtocol, sizeof(tlsProtocol), tlsCipherSuite, sizeof(tlsCipherSuite),
        transportSelection, sizeof(transportSelection), tlsStatus, sizeof(tlsStatus),
        &tlsValidated, &tlsHostnameValidated, &tlsAllowlistLocalOnly, sourceType, sizeof(sourceType));

    const bool contentTypeOk =
        gxos::web::httpSharedEqualsInsensitive(contentType, "text/html") ||
        gxos::web::httpSharedEqualsInsensitive(contentType, "text/plain");
    const bool pass = pilotEnabled
        ? nav_smoke_text_equals(requestedUrl, url) &&
            nav_smoke_text_equals(finalUrl, url) &&
            statusCode == 200 &&
            contentTypeOk &&
            bodyBytes > 0 &&
            parsedBlocks > 0 &&
            redirectCount == 0 &&
            plainTcpConnectAttempts == 0 &&
            tlsTcpConnectAttempts == 1 &&
            verifyFlags == 0 &&
            tlsValidated &&
            tlsHostnameValidated &&
            !tlsAllowlistLocalOnly &&
            nav_smoke_text_equals(transportSelection, "PolicyValidatedTlsHttps") &&
            nav_smoke_text_equals(tlsStatus, "Success") &&
            nav_smoke_text_equals(tlsSniHost, kNavigatorPublicPilotHttpsHost) &&
            nav_smoke_text_equals(sourceType, "https")
        : nav_smoke_text_equals(requestedUrl, url) &&
            nav_smoke_text_equals(finalUrl, url) &&
            statusCode == 0 &&
            redirectCount == 0 &&
            plainTcpConnectAttempts == 0 &&
            tlsTcpConnectAttempts == 0 &&
            !tlsValidated &&
            !tlsHostnameValidated &&
            !tlsAllowlistLocalOnly &&
            !nav_smoke_text_equals(transportSelection, "PolicyValidatedTlsHttps") &&
            error[0] != '\0';

    serial::puts("[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.enabled=");
    serial::puts(pilotEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.requested_url=");
    serial::puts(requestedUrl[0] ? requestedUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.http_status=");
    serial_put_dec((uint32_t)statusCode);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.tls_tcp_connect_attempts=");
    serial_put_dec((uint32_t)tlsTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.verify_flags=");
    serial_put_dec64((uint64_t)verifyFlags);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.sni_host=");
    serial::puts(tlsSniHost[0] ? tlsSniHost : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.transport_selection=");
    serial::puts(transportSelection[0] ? transportSelection : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.tls_status=");
    serial::puts(tlsStatus[0] ? tlsStatus : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.error=");
    serial::puts(error[0] ? error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_fixture.result=");
    serial::puts(pass ? "PASS\n" : "FAIL\n");
    return pass;
}

static bool printNavigatorPublicPilotRedirectCase()
{
    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const bool pilotEnabled =
        httpsPolicy.state == gxos::GxosValidatedHttpsPolicyState::ProductionValidated &&
        httpsPolicy.broadPublicHttpsEnabled;
    const char* url = "http://10.0.2.2:8080/navigator-smoke/redirect-to-public-pilot-https";
    char expectedFinalUrl[160];
    navigator_public_pilot_fixture_url(expectedFinalUrl, sizeof(expectedFinalUrl), "/navigator-public-pilot/ok.html");
    char requestedUrl[160];
    int statusCode = 0;
    int bodyBytes = 0;
    int parsedBlocks = 0;
    int redirectCount = 0;
    int plainTcpConnectAttempts = 0;
    int tlsTcpConnectAttempts = 0;
    uint32_t verifyFlags = 0;
    char contentType[48];
    char error[128];
    char finalUrl[160];
    char tlsSniHost[64];
    char tlsProtocol[32];
    char tlsCipherSuite[64];
    char transportSelection[40];
    char tlsStatus[40];
    char sourceType[24];
    bool tlsValidated = false;
    bool tlsHostnameValidated = false;
    bool tlsAllowlistLocalOnly = false;
    NavigatorApp::smokeCaptureHttpsNavigation(
        url, requestedUrl, sizeof(requestedUrl), &statusCode,
        contentType, sizeof(contentType), &bodyBytes, &parsedBlocks, error, sizeof(error),
        finalUrl, sizeof(finalUrl), &redirectCount, &plainTcpConnectAttempts,
        &tlsTcpConnectAttempts, &verifyFlags, tlsSniHost, sizeof(tlsSniHost),
        tlsProtocol, sizeof(tlsProtocol), tlsCipherSuite, sizeof(tlsCipherSuite),
        transportSelection, sizeof(transportSelection), tlsStatus, sizeof(tlsStatus),
        &tlsValidated, &tlsHostnameValidated, &tlsAllowlistLocalOnly, sourceType, sizeof(sourceType));

    const bool pass = pilotEnabled
        ? nav_smoke_text_equals(requestedUrl, url) &&
            nav_smoke_text_equals(finalUrl, expectedFinalUrl) &&
            statusCode == 200 &&
            redirectCount == 1 &&
            plainTcpConnectAttempts == 1 &&
            tlsTcpConnectAttempts == 1 &&
            verifyFlags == 0 &&
            tlsValidated &&
            tlsHostnameValidated &&
            !tlsAllowlistLocalOnly &&
            nav_smoke_text_equals(transportSelection, "PolicyValidatedTlsHttps") &&
            nav_smoke_text_equals(tlsStatus, "Success") &&
            nav_smoke_text_equals(sourceType, "https")
        : nav_smoke_text_equals(requestedUrl, url) &&
            nav_smoke_text_equals(finalUrl, expectedFinalUrl) &&
            error[0] != '\0' &&
            redirectCount == 1 &&
            plainTcpConnectAttempts == 1 &&
            tlsTcpConnectAttempts == 0 &&
            !nav_smoke_text_equals(transportSelection, "PolicyValidatedTlsHttps");

    serial::puts("[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.enabled=");
    serial::puts(pilotEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.requested_url=");
    serial::puts(requestedUrl[0] ? requestedUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.http_status=");
    serial_put_dec((uint32_t)statusCode);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.redirect_count=");
    serial_put_dec((uint32_t)redirectCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.plain_tcp_connect_attempts=");
    serial_put_dec((uint32_t)plainTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.tls_tcp_connect_attempts=");
    serial_put_dec((uint32_t)tlsTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.transport_selection=");
    serial::puts(transportSelection[0] ? transportSelection : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.tls_status=");
    serial::puts(tlsStatus[0] ? tlsStatus : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.error=");
    serial::puts(error[0] ? error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_redirect.result=");
    serial::puts(pass ? "PASS\n" : "FAIL\n");
    return pass;
}

static bool printNavigatorPublicPilotDowngradeCase()
{
    const gxos::GxosValidatedHttpsPolicyInfo httpsPolicy = gxos::gxos_validated_https_policy_info();
    const bool pilotEnabled =
        httpsPolicy.state == gxos::GxosValidatedHttpsPolicyState::ProductionValidated &&
        httpsPolicy.broadPublicHttpsEnabled;
    char url[160];
    navigator_public_pilot_fixture_url(url, sizeof(url), "/navigator-public-pilot/redirect-downgrade");
    char requestedUrl[160];
    int statusCode = 0;
    int bodyBytes = 0;
    int parsedBlocks = 0;
    int redirectCount = 0;
    int plainTcpConnectAttempts = 0;
    int tlsTcpConnectAttempts = 0;
    uint32_t verifyFlags = 0;
    char contentType[48];
    char error[128];
    char finalUrl[160];
    char tlsSniHost[64];
    char tlsProtocol[32];
    char tlsCipherSuite[64];
    char transportSelection[40];
    char tlsStatus[40];
    char sourceType[24];
    bool tlsValidated = false;
    bool tlsHostnameValidated = false;
    bool tlsAllowlistLocalOnly = false;
    NavigatorApp::smokeCaptureHttpsNavigation(
        url, requestedUrl, sizeof(requestedUrl), &statusCode,
        contentType, sizeof(contentType), &bodyBytes, &parsedBlocks, error, sizeof(error),
        finalUrl, sizeof(finalUrl), &redirectCount, &plainTcpConnectAttempts,
        &tlsTcpConnectAttempts, &verifyFlags, tlsSniHost, sizeof(tlsSniHost),
        tlsProtocol, sizeof(tlsProtocol), tlsCipherSuite, sizeof(tlsCipherSuite),
        transportSelection, sizeof(transportSelection), tlsStatus, sizeof(tlsStatus),
        &tlsValidated, &tlsHostnameValidated, &tlsAllowlistLocalOnly, sourceType, sizeof(sourceType));

    const bool pass = pilotEnabled
        ? nav_smoke_text_equals(requestedUrl, url) &&
            nav_smoke_text_equals(finalUrl, "http://10.0.2.2:8080/navigator-smoke/insecure-downgrade") &&
            redirectCount == 1 &&
            plainTcpConnectAttempts == 0 &&
            tlsTcpConnectAttempts == 1 &&
            nav_smoke_text_equals(transportSelection, "BlockedPolicy") &&
            nav_smoke_text_equals(tlsStatus, "PolicyBlocked") &&
            nav_smoke_text_equals(error, "HTTPS downgrade redirect blocked")
        : nav_smoke_text_equals(requestedUrl, url) &&
            nav_smoke_text_equals(finalUrl, url) &&
            plainTcpConnectAttempts == 0 &&
            tlsTcpConnectAttempts == 0 &&
            error[0] != '\0';

    serial::puts("[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.enabled=");
    serial::puts(pilotEnabled ? "yes" : "no");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.requested_url=");
    serial::puts(requestedUrl[0] ? requestedUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.final_url=");
    serial::puts(finalUrl[0] ? finalUrl : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.redirect_count=");
    serial_put_dec((uint32_t)redirectCount);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.plain_tcp_connect_attempts=");
    serial_put_dec((uint32_t)plainTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.tls_tcp_connect_attempts=");
    serial_put_dec((uint32_t)tlsTcpConnectAttempts);
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.transport_selection=");
    serial::puts(transportSelection[0] ? transportSelection : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.tls_status=");
    serial::puts(tlsStatus[0] ? tlsStatus : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.error=");
    serial::puts(error[0] ? error : "(none)");
    serial::puts("\n[NAVIGATOR-SMOKE] https.case.public_pilot_downgrade.result=");
    serial::puts(pass ? "PASS\n" : "FAIL\n");
    return pass;
}

static bool printNavigatorHttpSmokeCases()
{
    // Run the explicitly requested public proof lane before the broad local
    // compatibility matrix.  A later legacy/native-app fault must not erase
    // the evidence for an already-authorized public HTTPS probe.
    bool httpOk = printNavigatorRealPublicHttpsProbeCase();
    httpOk = printNavigatorLocalTlsSmokeCase() && httpOk;
    httpOk = printNavigatorLocalTlsRedirectCase() && httpOk;
    httpOk = printNavigatorLocalTlsWrongHostnameFailureCase() && httpOk;
#if defined(GXOS_NAVIGATOR_TLS_CAPABILITY_CONTRACT_NEGATIVE_TEST_ACTIVE)
    httpOk = printNavigatorTlsCapabilityContractNegativeCase() && httpOk;
#endif
    httpOk = printNavigatorLocalTlsBlockedHostCase() && httpOk;
    httpOk = printNavigatorPolicyValidatedTlsSmokeCase() && httpOk;
    httpOk = printNavigatorPolicyValidatedRedirectCase() && httpOk;
    httpOk = printNavigatorPolicyValidatedScopeBlockedRedirectCase() && httpOk;
    httpOk = printNavigatorPolicyValidatedWrongHostnameFailureCase() && httpOk;
    httpOk = printNavigatorPolicyValidatedDowngradeCase() && httpOk;
    httpOk = printNavigatorPublicPilotDecisionCase() && httpOk;
    httpOk = printNavigatorPublicPilotFixtureCase() && httpOk;
    httpOk = printNavigatorPublicPilotRedirectCase() && httpOk;
    httpOk = printNavigatorPublicPilotDowngradeCase() && httpOk;
    httpOk = printNavigatorHttpsCompatibilityCase("compat_html_200",
        "/navigator-smoke/basic.html", 200, "/navigator-smoke/basic.html", 0,
        "text/html", "", "", "", false, false, false) && httpOk;
    httpOk = printNavigatorHttpsCompatibilityCase("compat_text_200",
        "/navigator-smoke/plain.txt", 200, "/navigator-smoke/plain.txt", 0,
        "text/plain", "", "", "", false, false, false) && httpOk;
    httpOk = printNavigatorHttpsCompatibilityCase("compat_404",
        "/navigator-smoke/missing.html", 404, "/navigator-smoke/missing.html", 0,
        "text/html", "", "HTTP error status", "", false, false, false) && httpOk;
    httpOk = printNavigatorHttpsCompatibilityCase("compat_500",
        "/navigator-smoke/error-500.html", 500, "/navigator-smoke/error-500.html", 0,
        "text/html", "", "HTTP error status", "", false, false, false) && httpOk;
    httpOk = printNavigatorHttpsCompatibilityCase("compat_download",
        "/navigator-smoke/download.bin", 200, "/navigator-smoke/download.bin", 0,
        "application/octet-stream", "", "*", nullptr,
        false, false, false) && httpOk;
    httpOk = printNavigatorHttpsCompatibilityCase("compat_gzip",
        "/navigator-smoke/gzip.html", 200, "/navigator-smoke/gzip.html", 0,
        "text/html", "gzip", "Unsupported content encoding", "Unsupported Content-Encoding",
        false, false, true) && httpOk;
    httpOk = printNavigatorHttpsCompatibilityCase("compat_br",
        "/navigator-smoke/br.html", 200, "/navigator-smoke/br.html", 0,
        "text/html", "br", "Unsupported content encoding", "Unsupported Content-Encoding",
        false, false, true) && httpOk;
    httpOk = printNavigatorHttpsCompatibilityCase("compat_deflate",
        "/navigator-smoke/deflate.html", 200, "/navigator-smoke/deflate.html", 0,
        "text/html", "deflate", "Unsupported content encoding", "Unsupported Content-Encoding",
        false, false, true) && httpOk;
    httpOk = printNavigatorHttpsCompatibilityCase("compat_redirect_relative",
        "/navigator-smoke/tls-redirect-relative", 200, "/navigator-smoke/final.html", 1,
        "text/html", "", "", "", false, false, false) && httpOk;
    httpOk = printNavigatorHttpsCompatibilityCase("compat_redirect_absolute",
        "/navigator-smoke/tls-redirect-absolute", 200, "/navigator-smoke/final.html", 1,
        "text/html", "", "", "", false, false, false) && httpOk;
    httpOk = printNavigatorHttpsCompatibilityCase("compat_redirect_loop",
        "/navigator-smoke/tls-redirect-loop", 302, "/navigator-smoke/tls-redirect-loop",
        gxos::web::kHttpSharedMaxRedirects, nullptr, "", "HTTP redirect limit exceeded", "",
        false, false, false) && httpOk;
    httpOk = printNavigatorHttpsCompatibilityCase("compat_large_body",
        "/navigator-smoke/large-body.txt", 200, "/navigator-smoke/large-body.txt", 0,
        "text/plain", "", "HTTP body too large", "", false, true, true) && httpOk;
    httpOk = printNavigatorHttpsCompatibilityCase("compat_large_headers",
        "/navigator-smoke/large-headers.html", 200, "/navigator-smoke/large-headers.html", 0,
        nullptr, nullptr, "HTTP header too large", "", true, false, true) && httpOk;
    httpOk = printNavigatorHttpsUnsupportedSmokeCase("direct_unsupported", "https://10.0.2.2:8443/navigator-smoke/tls-basic.html",
        "https://10.0.2.2:8443/navigator-smoke/tls-basic.html", 0, 0) && httpOk;
    httpOk = printNavigatorHttpsUnsupportedSmokeCase("redirect_public_unsupported",
        "http://10.0.2.2:8080/navigator-smoke/redirect-to-numeric-https",
        "https://10.0.2.2:8443/navigator-smoke/tls-basic.html", 1, 0) && httpOk;
    httpOk = printNavigatorHttpSmokeCase("basic", "http://10.0.2.2:8080/navigator-smoke/basic.html", 200,
        "http://10.0.2.2:8080/navigator-smoke/basic.html", true, true, false) && httpOk;
    httpOk = printNavigatorHttpSmokeCase("relative_redirect", "http://10.0.2.2:8080/navigator-smoke/redirect-relative", 200,
        "http://10.0.2.2:8080/navigator-smoke/final.html", true, true, false) && httpOk;
    httpOk = printNavigatorHttpSmokeCase("absolute_redirect", "http://10.0.2.2:8080/navigator-smoke/redirect-absolute", 200,
        "http://10.0.2.2:8080/navigator-smoke/final.html", true, true, false) && httpOk;
    httpOk = printNavigatorHttpSmokeCase("hostname_basic", "http://guidexos.test:8080/navigator-smoke/host-check.html", 200,
        "http://guidexos.test:8080/navigator-smoke/host-check.html", true, true, false, -1, -1, -1, "10.0.2.2") && httpOk;
    httpOk = printNavigatorHttpSmokeCase("hostname_redirect", "http://10.0.2.2:8080/navigator-smoke/redirect-hostname", 200,
        "http://guidexos.test:8080/navigator-smoke/final.html", true, true, false, -1, -1, -1, "10.0.2.2") && httpOk;
    httpOk = printNavigatorHttpSmokeCase("redirect_loop", "http://10.0.2.2:8080/navigator-smoke/redirect-loop", 302,
        "http://10.0.2.2:8080/navigator-smoke/redirect-loop", false, false, true) && httpOk;
    httpOk = printNavigatorHttpSmokeCase("chunked", "http://10.0.2.2:8080/navigator-smoke/chunked.html", 200,
        "http://10.0.2.2:8080/navigator-smoke/chunked.html", true, true, false) && httpOk;
    httpOk = printNavigatorHttpSmokeCase("missing_404", "http://10.0.2.2:8080/navigator-smoke/missing.html", 404,
        "http://10.0.2.2:8080/navigator-smoke/missing.html", true, false, false) && httpOk;
    httpOk = printNavigatorHttpSmokeCase("gzip_unsupported", "http://10.0.2.2:8080/navigator-smoke/gzip.html", 200,
        "http://10.0.2.2:8080/navigator-smoke/gzip.html", false, false, true) && httpOk;
    httpOk = printNavigatorHttpSmokeCase("image_relative", "http://10.0.2.2:8080/navigator-smoke/image-relative.html", 200,
        "http://10.0.2.2:8080/navigator-smoke/image-relative.html", true, true, false, 1, 1, 0) && httpOk;
    httpOk = printNavigatorHttpSmokeCase("image_absolute", "http://10.0.2.2:8080/navigator-smoke/image-absolute.html", 200,
        "http://10.0.2.2:8080/navigator-smoke/image-absolute.html", true, true, false, 1, 1, 0) && httpOk;
    httpOk = printNavigatorHttpSmokeCase("image_redirect", "http://10.0.2.2:8080/navigator-smoke/image-redirect.html", 200,
        "http://10.0.2.2:8080/navigator-smoke/image-redirect.html", true, true, false, 1, 1, 0) && httpOk;
    httpOk = printNavigatorHttpSmokeCase("image_chunked", "http://10.0.2.2:8080/navigator-smoke/image-chunked.html", 200,
        "http://10.0.2.2:8080/navigator-smoke/image-chunked.html", true, true, false, 1, 1, 0) && httpOk;
    httpOk = printNavigatorHttpSmokeCase("image_nonpng", "http://10.0.2.2:8080/navigator-smoke/image-nonpng.html", 200,
        "http://10.0.2.2:8080/navigator-smoke/image-nonpng.html", true, true, false, 1, 0, 1) && httpOk;
    httpOk = printNavigatorHttpSmokeCase("hostname_image_relative", "http://guidexos.test:8080/navigator-smoke/hostname-image.html", 200,
        "http://guidexos.test:8080/navigator-smoke/hostname-image.html", true, true, false, 1, 1, 0, "10.0.2.2") && httpOk;
    httpOk = printNavigatorHttpSmokeCase("text_polish", "http://10.0.2.2:8080/navigator-smoke/text-polish.html", 200,
        "http://10.0.2.2:8080/navigator-smoke/text-polish.html", true, true, false, 0, 0, 0) && httpOk;
    httpOk = printNavigatorInteractiveFormsLitePostSmokeCase() && httpOk;
    httpOk = printNavigatorInteractiveFormsLiteGetSmokeCase() && httpOk;
    httpOk = printNavigatorFormsLitePostSmokeCase("forms_post_redirect_303", "http://10.0.2.2:8080/forms/post-redirect-303",
        "http://10.0.2.2:8080/navigator-smoke/final.html", 1) && httpOk;
    httpOk = printNavigatorFormsLitePostSmokeCase("forms_post_redirect_307", "http://10.0.2.2:8080/forms/post-redirect-307",
        "http://10.0.2.2:8080/forms/post-echo", 1) && httpOk;
    httpOk = printNavigatorFormsLitePostSmokeCase("forms_post_redirect_hostname", "http://10.0.2.2:8080/forms/post-redirect-hostname",
        "http://guidexos.test:8080/forms/post-echo", 1, "10.0.2.2") && httpOk;
    return httpOk;
}

void printNavigatorRuntimeSmokeReport()
{
    bool registered = printNavigatorRuntimeSmokePreamble();
    bool typographyOk = NavigatorApp::smokeTypographyPhase7A();
#ifdef GXOS_NAVIGATOR_HTTP_SMOKE_ACTIVE
    const bool phase8jRawOk = gxos::gxos_tls_run_phase8j_raw_ecdsa_diagnostics();
    serial::puts(phase8jRawOk
        ? "[NAVIGATOR-SMOKE] phase8j.raw_ecdsa.result=PASS\n"
        : "[NAVIGATOR-SMOKE] phase8j.raw_ecdsa.result=FAIL\n");
    bool httpOk = printNavigatorHttpSmokeCases();
    serial::puts((registered && typographyOk && phase8jRawOk && httpOk) ? "[NAVIGATOR-SMOKE] result=PASS\n" : "[NAVIGATOR-SMOKE] result=FAIL\n");
#else
    serial::puts("[NAVIGATOR-SMOKE] http.active_cases=skipped\n");
    serial::puts((registered && typographyOk) ? "[NAVIGATOR-SMOKE] result=PASS\n" : "[NAVIGATOR-SMOKE] result=FAIL\n");
#endif
    serial::puts("[NAVIGATOR-SMOKE] END\n");
}

void printNavigatorHttpRuntimeSmokeReport()
{
    bool registered = printNavigatorRuntimeSmokePreamble();
    bool typographyOk = NavigatorApp::smokeTypographyPhase7A();
    bool httpOk = printNavigatorHttpSmokeCases();
    serial::puts((registered && typographyOk && httpOk) ? "[NAVIGATOR-SMOKE] result=PASS\n" : "[NAVIGATOR-SMOKE] result=FAIL\n");
    serial::puts("[NAVIGATOR-SMOKE] END\n");
}

} // namespace apps
} // namespace kernel
