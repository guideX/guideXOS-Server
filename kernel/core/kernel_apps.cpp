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
#include "include/kernel/serial_debug.h"
#include "include/kernel/desktop_icon_theme_flat.h"

extern "C" void desktop_request_redraw();

namespace kernel {
namespace apps {

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

static void strappend(char* dst, const char* src, int maxLen) {
    if (!dst || !src || maxLen <= 0) return;
    int len = strlen_local(dst);
    int i = 0;
    while (src[i] && len < maxLen - 1) {
        dst[len++] = src[i++];
    }
    dst[len] = '\0';
}

static bool startsWithText(const char* value, const char* prefix) {
    if (!value || !prefix) return false;
    while (*prefix) {
        if (*value++ != *prefix++) return false;
    }
    return true;
}

static bool endsWithText(const char* value, const char* suffix) {
    if (!value || !suffix) return false;
    int valueLen = strlen_local(value);
    int suffixLen = strlen_local(suffix);
    if (suffixLen > valueLen) return false;
    for (int i = 0; i < suffixLen; ++i) {
        if (value[valueLen - suffixLen + i] != suffix[i]) return false;
    }
    return true;
}

static const char* kKernelTrashRootPath = "/Trash";
static const char* kKernelTrashInfoSuffix = ".trashinfo";

static bool kernel_trash_exists()
{
    vfs::DirEntry entry{};
    uint8_t dir = vfs::opendir(kKernelTrashRootPath);
    if (dir == 0xFF) return false;
    bool hasItems = false;
    while (vfs::readdir(dir, &entry)) {
        if (entry.name[0] == '.' && (entry.name[1] == '\0' || (entry.name[1] == '.' && entry.name[2] == '\0'))) continue;
        if (endsWithText(entry.name, kKernelTrashInfoSuffix)) continue;
        hasItems = true;
        break;
    }
    vfs::closedir(dir);
    serial::puts("[trash] item count computed=");
    serial::puts(hasItems ? "nonzero" : "0");
    serial::puts(" iconKey=");
    serial::puts(hasItems ? "trash.full" : "trash.empty");
    serial::puts("\n");
    return hasItems;
}

static void kernel_write_text_file(const char* path, const char* text)
{
    if (!path || !text) return;
    vfs::write_file(path, text, (uint32_t)strlen_local(text));
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
        default: return "Filesystem operation failed";
    }
}

static bool kernel_make_directory_if_missing(const char* path)
{
    vfs::FileInfo info{};
    vfs::Status statStatus = vfs::stat(path, &info);
    if (statStatus == vfs::VFS_OK) return info.type == vfs::FILE_TYPE_DIRECTORY;
    vfs::Status mkdirStatus = vfs::mkdir(path);
    serial::puts("[trash] mkdir ");
    serial::puts(path ? path : "<null>");
    serial::puts(" result=");
    serial::puts(kernel_vfs_status_text(mkdirStatus));
    serial::puts("\n");
    return mkdirStatus == vfs::VFS_OK;
}

static bool kernel_copy_file_to_trash_then_delete(const char* sourcePath, const char* destPath, char* error, int errorSize)
{
    vfs::FileInfo info{};
    vfs::Status statStatus = vfs::stat(sourcePath, &info);
    if (statStatus != vfs::VFS_OK) {
        strcopy(error, kernel_vfs_status_text(statStatus), errorSize);
        return false;
    }
    if (info.type == vfs::FILE_TYPE_DIRECTORY) {
        strcopy(error, "Folder move requires filesystem rename support", errorSize);
        return false;
    }
    if (info.size > 1024 * 1024) {
        strcopy(error, "File too large for Trash fallback", errorSize);
        return false;
    }

    static char buffer[1024 * 1024];
    int32_t bytesRead = vfs::read_file(sourcePath, buffer, (uint32_t)info.size);
    if (bytesRead < 0 || (uint64_t)bytesRead != info.size) {
        strcopy(error, "Unable to read source for Trash copy", errorSize);
        serial::puts("[trash] copy fallback read failed\n");
        return false;
    }

    int32_t bytesWritten = vfs::write_file(destPath, buffer, (uint32_t)bytesRead);
    if (bytesWritten < 0 || bytesWritten != bytesRead) {
        strcopy(error, "Unable to write Trash copy", errorSize);
        serial::puts("[trash] copy fallback write failed\n");
        return false;
    }

    vfs::FileInfo destInfo{};
    vfs::Status destStat = vfs::stat(destPath, &destInfo);
    if (destStat != vfs::VFS_OK || destInfo.size != info.size) {
        strcopy(error, "Trash copy verification failed", errorSize);
        serial::puts("[trash] copy fallback verify failed\n");
        return false;
    }

    vfs::Status unlinkStatus = vfs::unlink(sourcePath);
    if (unlinkStatus != vfs::VFS_OK) {
        strcopy(error, "Copied to Trash but source delete failed", errorSize);
        serial::puts("[trash] copy fallback source unlink failed\n");
        return false;
    }

    serial::puts("[trash] filesystem operation=copy-verify-delete fallback\n");
    return true;
}

static void kernel_join_path(const char* base, const char* name, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    vfs::join_path(base, name, out, (size_t)outSize);
}

static void kernel_trash_info_path_for(const char* trashedPath, char* out, int outSize)
{
    if (!out || outSize <= 0) return;
    strcopy(out, trashedPath, outSize);
    strappend(out, kKernelTrashInfoSuffix, outSize);
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

static void kernel_unique_trash_path(const char* baseName, bool isDir, char* out, int outSize)
{
    kernel_join_path(kKernelTrashRootPath, baseName, out, outSize);
    if (!vfs::exists(out)) return;

    for (int index = 1; index < 100; ++index) {
        char candidate[vfs::VFS_MAX_FILENAME];
        kernel_make_fat_safe_collision_name(baseName, isDir, index, candidate, sizeof(candidate));
        kernel_join_path(kKernelTrashRootPath, candidate, out, outSize);
        if (!vfs::exists(out)) return;
    }
}

static bool kernel_move_path_to_trash(const char* sourcePath, const char* sourceName, bool isDir, char* movedPath, int movedPathSize, char* error, int errorSize)
{
    serial::puts("[trash] delete requested path=");
    serial::puts(sourcePath ? sourcePath : "<null>");
    serial::puts("\n");
    serial::puts("[trash] selected full item name=");
    serial::puts(sourceName ? sourceName : "<null>");
    serial::puts("\n");

    if (!kernel_make_directory_if_missing(kKernelTrashRootPath)) {
        strcopy(error, "Unable to create Trash directory", errorSize);
        serial::puts("[trash] Trash directory unavailable\n");
        return false;
    }

    serial::puts("[trash] selected trash dir=");
    serial::puts(kKernelTrashRootPath);
    serial::puts("\n");

    kernel_unique_trash_path(sourceName, isDir, movedPath, movedPathSize);
    serial::puts("[trash] collision-safe target=");
    serial::puts(movedPath);
    serial::puts("\n");

    serial::puts("[trash] filesystem operation=rename/move\n");
    vfs::Status renameStatus = vfs::rename(sourcePath, movedPath);
    if (renameStatus != vfs::VFS_OK) {
        serial::puts("[trash] rename/move failed result=");
        serial::puts(kernel_vfs_status_text(renameStatus));
        serial::puts("\n");
        if (!kernel_copy_file_to_trash_then_delete(sourcePath, movedPath, error, errorSize)) {
            if (!error[0]) strcopy(error, kernel_vfs_status_text(renameStatus), errorSize);
            serial::puts("[trash] move-to-trash failed\n");
            return false;
        }
    }

    char infoPath[256];
    kernel_trash_info_path_for(movedPath, infoPath, sizeof(infoPath));
    char metadata[512];
    metadata[0] = '\0';
    strappend(metadata, "{\n  \"originalPath\": \"", sizeof(metadata));
    strappend(metadata, sourcePath, sizeof(metadata));
    strappend(metadata, "\",\n  \"originalName\": \"", sizeof(metadata));
    strappend(metadata, sourceName, sizeof(metadata));
    strappend(metadata, "\",\n  \"isDirectory\": ", sizeof(metadata));
    strappend(metadata, isDir ? "true" : "false", sizeof(metadata));
    strappend(metadata, "\n}", sizeof(metadata));
    kernel_write_text_file(infoPath, metadata);

    serial::puts("[trash] move-to-trash success path=");
    serial::puts(movedPath);
    serial::puts("\n");
    kernel_desktop_refresh_trash_state();
    return true;
}

static void appDrawText(uint32_t x, uint32_t y, const char* text, uint32_t color) {
    uint32_t cx = x;
    while (text && *text) {
        drawChar(cx, y, *text, color);
        cx += kGlyphW + kGlyphSpacing;
        text++;
    }
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
    {"legacy_red_flower", "Red Flower", 0xFF390808, 0xFFA82020},
    {"legacy_ameoba", "Ameoba", 0xFF102060, 0xFF7020B0},
    {"legacy_ameobagx", "Ameoba GX", 0xFF180830, 0xFF8A36B8},
    {"legacy_tron_porsche", "Tron Porsche", 0xFF052A35, 0xFF18B8C8},
    {"legacy_wallpaper2", "Wallpaper 2", 0xFF1A1640, 0xFFC02080},
};

static const int kKernelWallpaperCount = sizeof(s_kernelWallpapers) / sizeof(s_kernelWallpapers[0]);
static const int kTileW = 92;
static const int kTileH = 76;
static const int kTileGap = 12;
static const int kTileCols = 4;
static const int kGalleryX = 18;
static const int kGalleryY = 82;
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
    : m_selectedIndex(0), m_appliedIndex(0), m_selectButtonId(-1) {
    strcopy(m_name, "DisplayOptions", app::MAX_APP_NAME);
}

DisplayOptionsApp::~DisplayOptionsApp() {
}

void DisplayOptionsApp::loadSelection() {
    const char* currentId = kernel::desktop::get_wallpaper_id();
    m_selectedIndex = 0;
    m_appliedIndex = 0;
    for (int i = 0; i < kKernelWallpaperCount; ++i) {
        if (streq_local(currentId, s_kernelWallpapers[i].id)) {
            m_selectedIndex = i;
            m_appliedIndex = i;
            return;
        }
    }
}

bool DisplayOptionsApp::init() {
    m_window = new app::KernelWindow();
    if (!m_window) return false;

    m_window->owner = this;
    m_window->x = 70;
    m_window->y = 50;
    m_window->w = 520;
    m_window->h = 390;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_FOCUSED;
    strcopy(m_window->title, "Display Options", app::MAX_TITLE_LEN);

    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }

    loadSelection();
    m_selectButtonId = addButton(18, 326, 142, 28, "Select Background");
    m_state = app::AppState::Running;
    return true;
}

void DisplayOptionsApp::shutdown() {
    m_state = app::AppState::Terminated;
}

void DisplayOptionsApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    framebuffer::fill_rect(x, y, w, h, rgb(28, 30, 38));

    framebuffer::fill_rect(x + 16, y + 16, 140, 30, rgb(58, 58, 58));
    appDrawRect(x + 16, y + 16, 140, 30, rgb(90, 90, 96));
    appDrawText(x + 28, y + 27, "Backgrounds", rgb(235, 235, 240));

    framebuffer::fill_rect(x + 166, y + 16, 140, 30, rgb(34, 34, 38));
    appDrawRect(x + 166, y + 16, 140, 30, rgb(70, 70, 76));
    appDrawText(x + 178, y + 27, "Resolution", rgb(130, 130, 138));

    framebuffer::fill_rect(x + 316, y + 16, 140, 30, rgb(34, 34, 38));
    appDrawRect(x + 316, y + 16, 140, 30, rgb(70, 70, 76));
    appDrawText(x + 328, y + 27, "Gradients", rgb(130, 130, 138));

    appDrawText(x + 18, y + 58, "Select a background from the gallery:", rgb(230, 230, 238));
    framebuffer::fill_rect(x + 14, y + 74, w - 28, 240, rgb(22, 22, 24));

    for (int i = 0; i < kKernelWallpaperCount; ++i) {
        int col = i % kTileCols;
        int row = i / kTileCols;
        uint32_t tx = x + kGalleryX + col * (kTileW + kTileGap);
        uint32_t ty = y + kGalleryY + row * (kTileH + kTileGap);

        if (i == m_selectedIndex) {
            framebuffer::fill_rect(tx - 4, ty - 4, kTileW + 8, kTileH + 8, rgb(72, 110, 180));
        }

        framebuffer::fill_rect(tx, ty, kTileW, kTileH, rgb(42, 42, 42));
        bool drewThumb = kernel::desktop::draw_wallpaper_thumbnail_by_id(s_kernelWallpapers[i].id, tx + 6, ty + 6, kTileW - 12, 42);
        if (!drewThumb) {
            for (int py = 0; py < 42; ++py) {
                uint8_t t = (uint8_t)((py * 255) / 41);
                uint32_t color = ((s_kernelWallpapers[i].previewColorA & 0xFF000000u)) |
                                 (((((s_kernelWallpapers[i].previewColorA >> 16) & 0xFFu) * (255 - t)) + (((s_kernelWallpapers[i].previewColorB >> 16) & 0xFFu) * t)) / 255) << 16 |
                                 (((((s_kernelWallpapers[i].previewColorA >> 8) & 0xFFu) * (255 - t)) + (((s_kernelWallpapers[i].previewColorB >> 8) & 0xFFu) * t)) / 255) << 8 |
                                 (((((s_kernelWallpapers[i].previewColorA) & 0xFFu) * (255 - t)) + (((s_kernelWallpapers[i].previewColorB) & 0xFFu) * t)) / 255);
                framebuffer::fill_rect(tx + 6, ty + 6 + (uint32_t)py, kTileW - 12, 1, color);
            }
        }

        appDrawRect(tx + 6, ty + 6, kTileW - 12, 42, rgb(130, 130, 145));
        appDrawText(tx + 6, ty + 54, s_kernelWallpapers[i].displayName, rgb(220, 220, 225));
        if (i == m_appliedIndex) appDrawText(tx + kTileW - 12, ty + 54, "*", rgb(255, 220, 80));
    }
}

int DisplayOptionsApp::hitWallpaper(int mx, int my) const {
    for (int i = 0; i < kKernelWallpaperCount; ++i) {
        int col = i % kTileCols;
        int row = i / kTileCols;
        int tx = kGalleryX + col * (kTileW + kTileGap);
        int ty = kGalleryY + row * (kTileH + kTileGap);
        if (mx >= tx && mx < tx + kTileW && my >= ty && my < ty + kTileH) return i;
    }
    return -1;
}

void DisplayOptionsApp::onMouseDown(int x, int y, uint8_t) {
    int hit = hitWallpaper(x, y);
    if (hit >= 0) {
        m_selectedIndex = hit;
        invalidate();
    }
}

void DisplayOptionsApp::onWidgetClick(int widgetId) {
    if (widgetId == m_selectButtonId) applySelected();
}

void DisplayOptionsApp::applySelected() {
    if (m_selectedIndex < 0 || m_selectedIndex >= kKernelWallpaperCount) return;
    kernel::desktop::set_wallpaper_by_id(s_kernelWallpapers[m_selectedIndex].id);
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
// TaskManagerApp Implementation
// ============================================================

TaskManagerApp::TaskManagerApp() 
    : m_selectedApp(-1), m_refreshBtnId(-1), m_endTaskBtnId(-1), 
      m_lastUpdate(0), m_entryCount(0) {
    strcopy(m_name, "TaskManager", app::MAX_APP_NAME);
}

TaskManagerApp::~TaskManagerApp() {
}

bool TaskManagerApp::init() {
    m_window = new app::KernelWindow();
    strcopy(m_window->title, "Task Manager", app::MAX_TITLE_LEN);
    m_window->x = 150;
    m_window->y = 60;
    m_window->w = 350;
    m_window->h = 300;
    m_window->flags = app::WF_VISIBLE | app::WF_TITLEBAR | app::WF_CLOSABLE | app::WF_RESIZABLE | app::WF_FOCUSED;
    m_window->owner = this;
    
    if (!compositor::KernelCompositor::registerWindow(m_window)) {
        delete m_window;
        m_window = nullptr;
        return false;
    }
    
    // Create buttons
    m_refreshBtnId = addButton(10, 240, 80, 28, "Refresh");
    m_endTaskBtnId = addButton(100, 240, 80, 28, "End Task");
    
    refreshList();
    
    m_state = app::AppState::Running;
    return true;
}

void TaskManagerApp::shutdown() {
    m_state = app::AppState::Terminated;
}

void TaskManagerApp::update() {
    // Auto-refresh every 100 ticks
    m_lastUpdate++;
    if (m_lastUpdate >= 100) {
        refreshList();
        m_lastUpdate = 0;
        invalidate();
    }
}

void TaskManagerApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    // Header
    framebuffer::fill_rect(x + 10, y + 10, w - 20, 24, rgb(40, 50, 65));
    
    // Column headers
    uint32_t headerY = y + 10 + (24 - kGlyphH) / 2;
    appDrawText(x + 15, headerY, "Application", rgb(220, 225, 240));
    appDrawText(x + w - 95, headerY, "Status", rgb(220, 225, 240));
    
    // List background
    uint32_t listY = y + 40;
    uint32_t listH = h - 90;
    framebuffer::fill_rect(x + 10, listY, w - 20, listH, rgb(30, 30, 38));
    
    // Draw entries
    uint32_t rowH = 24;
    for (int i = 0; i < m_entryCount && (uint32_t)i * rowH < listH - rowH; i++) {
        uint32_t rowY = listY + i * rowH;
        
        // Selection highlight
        if (i == m_selectedApp) {
            framebuffer::fill_rect(x + 11, rowY + 1, w - 22, rowH - 2, rgb(50, 70, 100));
        } else if (i % 2 == 0) {
            framebuffer::fill_rect(x + 11, rowY + 1, w - 22, rowH - 2, rgb(35, 35, 43));
        }
        
        // App name
        uint32_t textY = rowY + (rowH - kGlyphH) / 2;
        appDrawText(x + 15, textY, m_entries[i].name, rgb(235, 235, 245));
        
        // Status indicator
        uint32_t statusColor = m_entries[i].running ? rgb(80, 180, 100) : rgb(180, 80, 80);
        framebuffer::fill_rect(x + w - 80, textY, 8, 8, statusColor);
        
        // Status text
        const char* status = m_entries[i].running ? "Running" : "Stopped";
        appDrawText(x + w - 68, textY, status, rgb(210, 215, 225));
    }
}

void TaskManagerApp::onMouseDown(int localX, int localY, uint8_t button) {
    (void)button;
    
    // Check if clicked in list area
    if (localY >= 40 && localY < 240) {
        int row = (localY - 40) / 24;
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
            m_entryCount++;
        }
    }
    
    // Add shell if open
    if (shell::is_open() && m_entryCount < MAX_ENTRIES) {
        strcopy(m_entries[m_entryCount].name, "Terminal", app::MAX_APP_NAME);
        m_entries[m_entryCount].running = true;
        m_entries[m_entryCount].windowCount = 1;
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

FileExplorerApp::FileExplorerApp()
    : m_entryCount(0), m_selected(0), m_scroll(0),
      m_lastClickIndex(-1), m_lastClickTick(0),
      m_backBtnId(-1), m_upBtnId(-1), m_refreshBtnId(-1), m_rootBtnId(-1),
      m_renameFileBtnId(-1), m_deleteFileBtnId(-1), m_renameFolderBtnId(-1), m_deleteFolderBtnId(-1),
      m_confirmDeleteBtnId(-1), m_cancelDeleteBtnId(-1), m_renamePrompt(false), m_deleteConfirm(false),
      m_deleteTargetIsDir(false) {
    strcopy(m_name, "Files", app::MAX_APP_NAME);
    strcopy(m_currentPath, "/", MAX_PATH_LEN);
    strcopy(m_status, "Ready", sizeof(m_status));
    m_renameValue[0] = '\0';
    m_deleteTarget[0] = '\0';
    m_deleteTargetName[0] = '\0';
    m_clipboard.sourcePath[0] = '\0';
    m_clipboard.sourceName[0] = '\0';
    m_clipboard.sourceMount[0] = '\0';
    m_clipboard.sourceIsDir = false;
    m_clipboard.operation = ClipboardOperation::None;
    m_contextMenuOpen = false;
    m_contextMenuX = 0;
    m_contextMenuY = 0;
    m_contextMenuHover = -1;
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
    serial::puts("[fileexplorer-bm] draw icon logical=");
    serial::puts(logicalName ? logicalName : "<null>");
    serial::puts(" x="); serial_put_dec(x);
    serial::puts(" y="); serial_put_dec(y);
    serial::puts(" size="); serial_put_dec(size);
    serial::puts(" pixels="); serial::puts(pixels ? "yes" : "no");
    serial::puts("\n");
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
    m_renameFileBtnId = addButton(248, 5, 82, 20, "Rename File");
    m_deleteFileBtnId = addButton(334, 5, 78, 20, "Delete File");
    m_renameFolderBtnId = addButton(248, 5, 92, 20, "Rename Dir");
    m_deleteFolderBtnId = addButton(344, 5, 84, 20, "Delete Dir");
    m_confirmDeleteBtnId = addButton(260, 205, 92, 20, "Move");
    m_cancelDeleteBtnId = addButton(344, 205, 70, 20, "Cancel");

    strcopy(m_currentPath, startPath && startPath[0] ? startPath : "/", MAX_PATH_LEN);
    refresh();
    updateActionButtons();
    m_state = app::AppState::Running;
    return true;
}

void FileExplorerApp::shutdown() {
    m_state = app::AppState::Terminated;
}

void FileExplorerApp::draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
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

    int visibleRows = bodyH > 30 ? (int)((bodyH - 30) / ROW_H) : 0;
    for (int i = 0; i < visibleRows && m_scroll + i < m_entryCount; ++i) {
        int entryIndex = m_scroll + i;
        Entry& e = m_entries[entryIndex];
        uint32_t rowY = bodyY + 24 + i * ROW_H;
        if (entryIndex == m_selected) {
            framebuffer::fill_rect(mainX + 1, rowY - 2, mainW - 2, ROW_H, rgb(200, 220, 245));
        }

        char sizeText[24];
        formatSize(e.size, sizeText, sizeof(sizeText));
        const char* logicalIcon = fileLogicalIcon(e);
        serial::puts("[fileexplorer-bm] row file=");
        serial::puts(e.name);
        serial::puts(" logical=");
        serial::puts(logicalIcon);
        serial::puts(" rowY=");
        serial_put_dec(rowY);
        serial::puts("\n");
        if (!drawThemedIcon(mainX + 8, rowY - 2, kIconSize, logicalIcon)) drawPlaceholderIcon(mainX + 8, rowY - 2, kIconSize);
        appDrawText(mainX + 30, rowY, e.name, rgb(20, 20, 20));
        appDrawText(mainX + 250, rowY, e.isDir ? "" : sizeText, rgb(70, 70, 70));
        appDrawText(mainX + 330, rowY, fileType(e), rgb(70, 70, 70));
        appDrawText(mainX + 430, rowY, "--", rgb(110, 110, 110));
    }

    framebuffer::fill_rect(x, y + h - statusH, w, statusH, rgb(235, 235, 235));
    appDrawText(x + 8, y + h - 15, m_status, rgb(40, 40, 40));

    if (m_contextMenuOpen) {
        framebuffer::fill_rect(x + m_contextMenuX, y + m_contextMenuY, CONTEXT_MENU_W, CONTEXT_MENU_ITEM_H * 4 + 2, rgb(245, 245, 248));
        framebuffer::fill_rect(x + m_contextMenuX, y + m_contextMenuY, CONTEXT_MENU_W, 1, rgb(120, 120, 140));
        framebuffer::fill_rect(x + m_contextMenuX, y + m_contextMenuY + CONTEXT_MENU_ITEM_H * 4 + 1, CONTEXT_MENU_W, 1, rgb(120, 120, 140));
        framebuffer::fill_rect(x + m_contextMenuX, y + m_contextMenuY, 1, CONTEXT_MENU_ITEM_H * 4 + 2, rgb(120, 120, 140));
        framebuffer::fill_rect(x + m_contextMenuX + CONTEXT_MENU_W - 1, y + m_contextMenuY, 1, CONTEXT_MENU_ITEM_H * 4 + 2, rgb(120, 120, 140));
        for (int i = 0; i < 4; ++i) {
            if (m_contextMenuHover == i) {
                framebuffer::fill_rect(x + m_contextMenuX + 1, y + m_contextMenuY + 1 + i * CONTEXT_MENU_ITEM_H, CONTEXT_MENU_W - 2, CONTEXT_MENU_ITEM_H, rgb(60, 90, 140));
            }
        }
        appDrawText(x + m_contextMenuX + 8, y + m_contextMenuY + 6,  "Open", rgb(20, 20, 20));
        appDrawText(x + m_contextMenuX + 8, y + m_contextMenuY + 6 + CONTEXT_MENU_ITEM_H, "Rename", rgb(20, 20, 20));
        appDrawText(x + m_contextMenuX + 8, y + m_contextMenuY + 6 + CONTEXT_MENU_ITEM_H * 2, "Delete", rgb(20, 20, 20));
        appDrawText(x + m_contextMenuX + 8, y + m_contextMenuY + 6 + CONTEXT_MENU_ITEM_H * 3, "Properties", rgb(20, 20, 20));
    }

    if (m_renamePrompt) {
        framebuffer::fill_rect(x + 220, y + 165, 360, 92, rgb(245, 245, 250));
        appDrawText(x + 232, y + 182, "Rename selected item", rgb(30, 30, 30));
        appDrawText(x + 232, y + 205, m_renameValue, rgb(20, 20, 20));
        appDrawText(x + 232, y + 230, "Enter=OK  Esc=Cancel  Backspace=Delete", rgb(80, 80, 80));
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
    if (m_propertiesOpen) {
        if (key == 27 || key == '\n' || key == '\r') {
            closeProperties();
        }
        return;
    }

    if (m_renamePrompt) {
        if (key == '\n' || key == '\r') {
            commitRename();
        } else if (key == 27) {
            cancelRename();
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
            if (m_selected < m_scroll) m_scroll = m_selected;
            updateActionButtons();
            invalidate();
        }
    } else if (key == shell::KEY_DOWN) {
        if (m_selected < m_entryCount - 1) {
            m_selected++;
            if (m_selected >= m_scroll + 20) m_scroll = m_selected - 19;
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
        m_selected -= 20;
        if (m_selected < 0) m_selected = 0;
        m_scroll -= 20;
        if (m_scroll < 0) m_scroll = 0;
        updateActionButtons();
        invalidate();
    } else if (key == shell::KEY_PGDN) {
        m_selected += 20;
        if (m_selected >= m_entryCount) m_selected = m_entryCount - 1;
        m_scroll += 20;
        if (m_scroll > m_selected) m_scroll = m_selected;
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
        if (len < (int)sizeof(m_renameValue) - 1 && c != '/') {
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
    int index = hitTestEntryRow(localX, localY);

    if (button == 2) {
        if (index >= 0 && index < m_entryCount) {
            m_selected = index;
            updateActionButtons();
        }
        m_contextMenuOpen = (index >= 0 && index < m_entryCount);
        m_contextMenuX = localX;
        m_contextMenuY = localY;
        m_contextMenuHover = -1;
        serial::puts("[fileexplorer-bm] context menu open\n");
        invalidate();
        return;
    }

    if (localX < LEFT_W || localY < bodyY + 24) return;

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

void FileExplorerApp::onWidgetClick(int widgetId) {
    closeTransientUi();
    if (widgetId == m_backBtnId || widgetId == m_rootBtnId) {
        navigate("/");
    } else if (widgetId == m_upBtnId) {
        goUp();
    } else if (widgetId == m_refreshBtnId) {
        refresh();
        updateActionButtons();
        invalidate();
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
    uint8_t dir = vfs::opendir(m_currentPath);
    if (dir == 0xFF) {
        setStatus("Cannot open directory. Mount a filesystem with vfsmount if needed.");
        return;
    }

    vfs::DirEntry de{};
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

    if (m_selected >= m_entryCount) m_selected = m_entryCount - 1;
    if (m_selected < 0) m_selected = 0;
    if (m_scroll > m_selected) m_scroll = m_selected;
    if (m_entryCount == 0) {
        setStatus("Directory is empty");
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

void FileExplorerApp::updateActionButtons() {
    bool hasSelection = m_selected >= 0 && m_selected < m_entryCount;
    bool isDir = hasSelection && m_entries[m_selected].isDir;

    app::Widget* renameFile = getWidget(m_renameFileBtnId);
    app::Widget* deleteFile = getWidget(m_deleteFileBtnId);
    app::Widget* renameFolder = getWidget(m_renameFolderBtnId);
    app::Widget* deleteFolder = getWidget(m_deleteFolderBtnId);
    app::Widget* confirmDelete = getWidget(m_confirmDeleteBtnId);
    app::Widget* cancelDelete = getWidget(m_cancelDeleteBtnId);

    if (renameFile) renameFile->visible = hasSelection && !isDir && !m_renamePrompt && !m_deleteConfirm;
    if (deleteFile) deleteFile->visible = hasSelection && !isDir && !m_renamePrompt && !m_deleteConfirm;
    if (renameFolder) renameFolder->visible = hasSelection && isDir && !m_renamePrompt && !m_deleteConfirm;
    if (deleteFolder) deleteFolder->visible = hasSelection && isDir && !m_renamePrompt && !m_deleteConfirm;
    if (confirmDelete) confirmDelete->visible = m_deleteConfirm;
    if (cancelDelete) cancelDelete->visible = m_deleteConfirm;
}

void FileExplorerApp::beginRenameSelected() {
    if (m_selected < 0 || m_selected >= m_entryCount) return;
    m_deleteConfirm = false;
    m_renamePrompt = true;
    strcopy(m_renameValue, m_entries[m_selected].name, sizeof(m_renameValue));
    setStatus("Type a new name, then press Enter.");
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::commitRename() {
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
    m_renamePrompt = false;
    setStatus("Rename cancelled");
    updateActionButtons();
    invalidate();
}

void FileExplorerApp::showDeleteConfirmation() {
    if (m_selected < 0 || m_selected >= m_entryCount) return;
    Entry& entry = m_entries[m_selected];
    joinPath(m_currentPath, entry.name, m_deleteTarget, sizeof(m_deleteTarget));
    strcopy(m_deleteTargetName, entry.name, sizeof(m_deleteTargetName));
    m_deleteTargetIsDir = entry.isDir;
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
    if (!m_deleteConfirm || !m_deleteTarget[0]) return;
    char movedPath[MAX_PATH_LEN];
    char error[96];
    movedPath[0] = '\0';
    error[0] = '\0';
    m_deleteConfirm = false;
    bool moved = kernel_move_path_to_trash(m_deleteTarget, m_deleteTargetName, m_deleteTargetIsDir, movedPath, sizeof(movedPath), error, sizeof(error));
    if (moved) {
        setStatus("Moved item to Trash");
    } else {
        setStatus(error[0] ? error : "Move to Trash failed");
    }
    refresh();
    if (moved) {
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
    if (m_selected < 0 || m_selected >= m_entryCount) return;
    Entry& entry = m_entries[m_selected];
    joinPath(m_currentPath, entry.name, m_clipboard.sourcePath, sizeof(m_clipboard.sourcePath));
    strcopy(m_clipboard.sourceName, entry.name, sizeof(m_clipboard.sourceName));
    strcopy(m_clipboard.sourceMount, m_currentPath, sizeof(m_clipboard.sourceMount));
    m_clipboard.sourceIsDir = entry.isDir;
    m_clipboard.operation = ClipboardOperation::Copy;
    setStatus("Copied item path to File Explorer clipboard");
    serial::puts("[fileexplorer-bm] clipboard copy prepared\n");
    invalidate();
}

void FileExplorerApp::beginMoveSelected() {
    if (m_selected < 0 || m_selected >= m_entryCount) return;
    Entry& entry = m_entries[m_selected];
    joinPath(m_currentPath, entry.name, m_clipboard.sourcePath, sizeof(m_clipboard.sourcePath));
    strcopy(m_clipboard.sourceName, entry.name, sizeof(m_clipboard.sourceName));
    strcopy(m_clipboard.sourceMount, m_currentPath, sizeof(m_clipboard.sourceMount));
    m_clipboard.sourceIsDir = entry.isDir;
    m_clipboard.operation = ClipboardOperation::Move;
    setStatus("Prepared move in File Explorer clipboard");
    serial::puts("[fileexplorer-bm] clipboard move prepared\n");
    invalidate();
}

bool FileExplorerApp::copyFileContents(const char* sourcePath, const char* destPath) {
    char buffer[8192];
    int32_t bytesRead = vfs::read_file(sourcePath, buffer, sizeof(buffer));
    if (bytesRead < 0) return false;
    int32_t bytesWritten = vfs::write_file(destPath, buffer, (uint32_t)bytesRead);
    return bytesWritten == bytesRead;
}

void FileExplorerApp::pasteClipboard() {
    if (m_clipboard.operation == ClipboardOperation::None || !m_clipboard.sourcePath[0]) {
        setStatus("Clipboard is empty");
        invalidate();
        return;
    }

    char destPath[MAX_PATH_LEN];
    joinPath(m_currentPath, m_clipboard.sourceName, destPath, sizeof(destPath));

    if (m_clipboard.sourceIsDir) {
        setStatus("copy/paste not yet supported for folders");
        serial::puts("[fileexplorer-bm] copy/paste not yet supported for folders\n");
        invalidate();
        return;
    }

    if (m_clipboard.operation == ClipboardOperation::Copy) {
        if (copyFileContents(m_clipboard.sourcePath, destPath)) {
            setStatus("Copied item into current directory");
            refresh();
        } else {
            setStatus("copy/paste not yet supported");
            serial::puts("[fileexplorer-bm] file copy failed or unsupported\n");
        }
    } else if (m_clipboard.operation == ClipboardOperation::Move) {
        vfs::Status status = vfs::rename(m_clipboard.sourcePath, destPath);
        if (status == vfs::VFS_OK) {
            setStatus("Moved item into current directory");
            m_clipboard.operation = ClipboardOperation::None;
            m_clipboard.sourcePath[0] = '\0';
            refresh();
        } else {
            setStatus("Move not yet supported");
            serial::puts("[fileexplorer-bm] move failed\n");
        }
    }
    invalidate();
}

int FileExplorerApp::hitTestContextMenu(int x, int y) const {
    if (!m_contextMenuOpen) return -1;
    if (x < m_contextMenuX || x >= m_contextMenuX + CONTEXT_MENU_W) return -1;
    if (y < m_contextMenuY || y >= m_contextMenuY + CONTEXT_MENU_ITEM_H * 4) return -1;
    return (y - m_contextMenuY) / CONTEXT_MENU_ITEM_H;
}

bool FileExplorerApp::handleContextMenuClick(int x, int y) {
    int item = hitTestContextMenu(x, y);
    if (item < 0) return false;
    m_contextMenuOpen = false;
    switch (item) {
        case 0: openSelected(); break;
        case 1: beginRenameSelected(); break;
        case 2: showDeleteConfirmation(); break;
        case 3: showPropertiesForSelected(); break;
        default: break;
    }
    return true;
}

int FileExplorerApp::hitTestEntryRow(int x, int y) const {
    int bodyY = TOOLBAR_H + ADDRESS_H;
    if (x < LEFT_W || y < bodyY + 24) return -1;
    int row = (y - bodyY - 24) / ROW_H;
    int index = m_scroll + row;
    return (index >= 0 && index < m_entryCount) ? index : -1;
}

void FileExplorerApp::closeTransientUi() {
    m_contextMenuOpen = false;
    m_contextMenuHover = -1;
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

                // Row text  (manual number GåÆ char)
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
      m_confirmEmpty(false), m_showProperties(false)
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
    updateButtons();
    kernel_desktop_refresh_trash_state();
    m_state = app::AppState::Running;
    return true;
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
        appDrawText(x + 74, y + 148, "Current: /Trash", rgb(200, 205, 215));
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
    uint8_t dir = vfs::opendir(kKernelTrashRootPath);
    if (dir == 0xFF) return;

    vfs::DirEntry entry{};
    while (m_entryCount < MAX_TRASH_ENTRIES && vfs::readdir(dir, &entry)) {
        if (entry.name[0] == '.' && (entry.name[1] == '\0' || (entry.name[1] == '.' && entry.name[2] == '\0'))) continue;
        if (endsWithText(entry.name, kKernelTrashInfoSuffix)) continue;

        TrashEntry& item = m_entries[m_entryCount];
        strcopy(item.name, entry.name, sizeof(item.name));
        item.isDir = entry.type == vfs::FILE_TYPE_DIRECTORY;
        item.size = entry.size;
        item.originalPath[0] = '\0';
        item.originalFolder[0] = '\0';
        item.type[0] = '\0';
        item.iconKey[0] = '\0';
        strcopy(item.deletedText, "Unknown", sizeof(item.deletedText));

        char itemPath[256];
        char infoPath[256];
        kernel_join_path(kKernelTrashRootPath, entry.name, itemPath, sizeof(itemPath));
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
            strcopy(item.originalPath, "/", sizeof(item.originalPath));
            strappend(item.originalPath, entry.name, sizeof(item.originalPath));
        }
        parentPathOf(item.originalPath, item.originalFolder, sizeof(item.originalFolder));
        strcopy(item.type, typeForEntry(item), sizeof(item.type));
        strcopy(item.iconKey, iconForEntry(item), sizeof(item.iconKey));

        ++m_entryCount;
    }
    vfs::closedir(dir);
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
        kernel_join_path(kKernelTrashRootPath, m_entries[i].name, itemPath, sizeof(itemPath));
        kernel_trash_info_path_for(itemPath, infoPath, sizeof(infoPath));

        if (!startsWithText(itemPath, kKernelTrashRootPath) || itemPath[strlen_local(kKernelTrashRootPath)] != '/') {
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

    kernel_make_directory_if_missing(kKernelTrashRootPath);
    int finalCount = 0;
    uint8_t dir = vfs::opendir(kKernelTrashRootPath);
    if (dir != 0xFF) {
        vfs::DirEntry entry{};
        while (vfs::readdir(dir, &entry)) {
            if (entry.name[0] == '.' && (entry.name[1] == '\0' || (entry.name[1] == '.' && entry.name[2] == '\0'))) continue;
            if (endsWithText(entry.name, kKernelTrashInfoSuffix)) continue;
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
    kernel_join_path(kKernelTrashRootPath, entry.name, sourcePath, sizeof(sourcePath));
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
    kernel_join_path(kKernelTrashRootPath, entry.name, itemPath, sizeof(itemPath));
    kernel_trash_info_path_for(itemPath, infoPath, sizeof(infoPath));
    if (!startsWithText(itemPath, kKernelTrashRootPath) || itemPath[strlen_local(kKernelTrashRootPath)] != '/') return false;
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
// App Registration
// ============================================================

void registerKernelApps() {
    app::AppManager::init();
    app::AppLogger::init();
    
    // Register available kernel-mode apps
    app::AppManager::registerApp("Notepad", 0xFF78B450, NotepadApp::create);
    app::AppManager::registerApp("Calculator", 0xFF4690C8, CalculatorApp::create);
    app::AppManager::registerApp("DisplayOptions", 0xFF606878, DisplayOptionsApp::create);
    app::AppManager::registerApp("TaskManager", 0xFFB44646, TaskManagerApp::create);
    app::AppManager::registerApp("Files", 0xFFC8B43C, FileExplorerApp::create);
    app::AppManager::registerApp("FileExplorer", 0xFFC8B43C, FileExplorerApp::create);
    app::AppManager::registerApp("Trash", 0xFF9098A4, TrashApp::create);
    app::AppManager::registerApp("DiskManager", 0xFF7050C0, DiskManagerApp::create);
}

} // namespace apps
} // namespace kernel
