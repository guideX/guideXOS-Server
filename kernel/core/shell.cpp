// guideXOS Kernel Shell - Implementation
//
// Interactive command-line shell with POSIX and Windows command support.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/shell.h"
#include "include/kernel/framebuffer.h"

// Forward declarations for power functions (defined in desktop.cpp, outside any namespace)
extern void perform_shutdown();
extern void perform_restart();
extern void perform_sleep();

namespace kernel {
namespace shell {

// ============================================================
// Bitmap font (8x16, ASCII 32..126) - compact subset
// ============================================================

static const int kFontW = 8;
static const int kFontH = 16;


// Simplified 8x8 font data for basic ASCII (we'll scale to 8x16)
static const uint8_t s_font[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 ' '
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // 33 '!'
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, // 34 '"'
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // 35 '#'
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // 36 '$'
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // 37 '%'
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // 38 '&'
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // 39 '''
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // 40 '('
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // 41 ')'
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // 42 '*'
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, // 43 '+'
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, // 44 ','
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, // 45 '-'
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // 46 '.'
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // 47 '/'
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, // 48 '0'
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, // 49 '1'
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, // 50 '2'
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, // 51 '3'
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, // 52 '4'
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, // 53 '5'
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, // 54 '6'
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, // 55 '7'
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, // 56 '8'
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, // 57 '9'
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, // 58 ':'
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, // 59 ';'
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // 60 '<'
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, // 61 '='
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // 62 '>'
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, // 63 '?'
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, // 64 '@'
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // 65 'A'
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, // 66 'B'
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // 67 'C'
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, // 68 'D'
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // 69 'E'
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, // 70 'F'
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // 71 'G'
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, // 72 'H'
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 73 'I'
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, // 74 'J'
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // 75 'K'
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, // 76 'L'
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // 77 'M'
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, // 78 'N'
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // 79 'O'
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, // 80 'P'
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // 81 'Q'
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, // 82 'R'
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // 83 'S'
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 84 'T'
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, // 85 'U'
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 86 'V'
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // 87 'W'
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, // 88 'X'
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // 89 'Y'
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, // 90 'Z'
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // 91 '['
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // 92 '\'
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // 93 ']'
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, // 94 '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // 95 '_'
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // 96 '`'
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // 97 'a'
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, // 98 'b'
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // 99 'c'
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, // 100 'd'
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, // 101 'e'
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, // 102 'f'
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // 103 'g'
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, // 104 'h'
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // 105 'i'
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, // 106 'j'
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // 107 'k'
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 108 'l'
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // 109 'm'
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, // 110 'n'
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // 111 'o'
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, // 112 'p'
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, // 113 'q'
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, // 114 'r'
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // 115 's'
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, // 116 't'
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // 117 'u'
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 118 'v'
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // 119 'w'
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, // 120 'x'
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // 121 'y'
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, // 122 'z'
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // 123 '{'
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // 124 '|'
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // 125 '}'
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, // 126 '~'
};

// ============================================================
// Shell State
// ============================================================

static ShellState s_state = ShellState::Closed;
static char s_cmdBuffer[MAX_CMD_LENGTH];
static uint32_t s_cmdLen = 0;
static uint32_t s_cursorPos = 0;

// Output buffer (scrollback)
static const uint32_t MAX_OUTPUT_LINES = 100;
static const uint32_t MAX_LINE_LENGTH = 120;
static char s_output[MAX_OUTPUT_LINES][MAX_LINE_LENGTH];
static uint32_t s_outputCount = 0;
static uint32_t s_scrollOffset = 0;

// Command history
static char s_history[MAX_HISTORY][MAX_CMD_LENGTH];
static uint32_t s_historyCount = 0;
static uint32_t s_historyIdx = 0;

// Current working directory
static char s_cwd[256] = "/";

// Hostname and username
static const char* s_hostname = "guideXOS";
static const char* s_username = "root";

// Uptime counter (incremented externally)
static uint32_t s_uptimeSeconds = 0;

// ============================================================
// Helper Functions
// ============================================================

static int str_len(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void str_copy(char* dst, const char* src, uint32_t maxLen) {
    uint32_t i = 0;
    while (src[i] && i < maxLen - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static bool str_eq(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return false;
        a++; b++;
    }
    return *a == *b;
}

static bool str_starts_with(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str != *prefix) return false;
        str++; prefix++;
    }
    return true;
}

// ============================================================
// Output Functions
// ============================================================

static void output_line(const char* line) {
    if (s_outputCount < MAX_OUTPUT_LINES) {
        str_copy(s_output[s_outputCount], line, MAX_LINE_LENGTH);
        s_outputCount++;
    } else {
        // Scroll up: shift all lines up by 1
        for (uint32_t i = 0; i < MAX_OUTPUT_LINES - 1; i++) {
            str_copy(s_output[i], s_output[i + 1], MAX_LINE_LENGTH);
        }
        str_copy(s_output[MAX_OUTPUT_LINES - 1], line, MAX_LINE_LENGTH);
    }
}

static void output_string(const char* str) {
    // Handle multi-line strings
    char line[MAX_LINE_LENGTH];
    uint32_t linePos = 0;
    
    while (*str) {
        if (*str == '\n' || linePos >= MAX_LINE_LENGTH - 1) {
            line[linePos] = '\0';
            output_line(line);
            linePos = 0;
            if (*str == '\n') str++;
        } else {
            line[linePos++] = *str++;
        }
    }
    if (linePos > 0) {
        line[linePos] = '\0';
        output_line(line);
    }
}

static void output_prompt() {
    char prompt[128];
    // Format: [user@host cwd]$ 
    uint32_t i = 0;
    prompt[i++] = '[';
    for (const char* p = s_username; *p && i < 120; p++) prompt[i++] = *p;
    prompt[i++] = '@';
    for (const char* p = s_hostname; *p && i < 120; p++) prompt[i++] = *p;
    prompt[i++] = ' ';
    for (const char* p = s_cwd; *p && i < 120; p++) prompt[i++] = *p;
    prompt[i++] = ']';
    prompt[i++] = '$';
    prompt[i++] = ' ';
    
    // Append current command
    for (uint32_t j = 0; j < s_cmdLen && i < 127; j++) {
        prompt[i++] = s_cmdBuffer[j];
    }
    prompt[i] = '\0';
    
    output_line(prompt);
}

// ============================================================
// Built-in Commands
// ============================================================

static void cmd_help() {
    output_string("guideXOS Shell - Available Commands:\n");
    output_string("\n");
    output_string("File System:\n");
    output_string("  ls, dir, ll    - List directory contents\n");
    output_string("  cd <dir>       - Change directory\n");
    output_string("  pwd            - Print working directory\n");
    output_string("  cat, type      - Display file contents\n");
    output_string("  mkdir, md      - Create directory\n");
    output_string("  rmdir, rd      - Remove directory\n");
    output_string("  touch          - Create empty file\n");
    output_string("  rm, del        - Remove file\n");
    output_string("  cp, copy       - Copy file\n");
    output_string("  mv, ren        - Move/rename file\n");
    output_string("\n");
    output_string("System:\n");
    output_string("  clear, cls     - Clear screen\n");
    output_string("  echo           - Print text\n");
    output_string("  whoami         - Current user\n");
    output_string("  hostname       - System hostname\n");
    output_string("  uptime         - System uptime\n");
    output_string("  date, time     - Current date/time\n");
    output_string("  mem            - Memory info\n");
    output_string("  cpuinfo        - CPU information\n");
    output_string("  ps             - List processes\n");
    output_string("  uname          - System information\n");
    output_string("\n");
    output_string("Power:\n");
    output_string("  reboot         - Restart system\n");
    output_string("  shutdown       - Power off system\n");
    output_string("  exit           - Close shell\n");
    output_string("\n");
    output_string("  help           - Show this help\n");
}

static void cmd_clear() {
    s_outputCount = 0;
    s_scrollOffset = 0;
}

static void cmd_pwd() {
    output_string(s_cwd);
}

static void cmd_ls(const char* path) {
    (void)path;  // TODO: implement VFS listing
    output_string("bin/\n");
    output_string("dev/\n");
    output_string("etc/\n");
    output_string("home/\n");
    output_string("proc/\n");
    output_string("sys/\n");
    output_string("tmp/\n");
    output_string("usr/\n");
    output_string("var/\n");
}

static void cmd_ll(const char* path) {
    (void)path;
    output_string("total 9\n");
    output_string("drwxr-xr-x  2 root root  4096 Jan  1 00:00 bin/\n");
    output_string("drwxr-xr-x  3 root root  4096 Jan  1 00:00 dev/\n");
    output_string("drwxr-xr-x  2 root root  4096 Jan  1 00:00 etc/\n");
    output_string("drwxr-xr-x  2 root root  4096 Jan  1 00:00 home/\n");
    output_string("dr-xr-xr-x  1 root root     0 Jan  1 00:00 proc/\n");
    output_string("dr-xr-xr-x  1 root root     0 Jan  1 00:00 sys/\n");
    output_string("drwxrwxrwt  2 root root  4096 Jan  1 00:00 tmp/\n");
    output_string("drwxr-xr-x  4 root root  4096 Jan  1 00:00 usr/\n");
    output_string("drwxr-xr-x  3 root root  4096 Jan  1 00:00 var/\n");
}

static void cmd_cd(const char* path) {
    if (!path || path[0] == '\0' || str_eq(path, "~")) {
        str_copy(s_cwd, "/home/root", 256);
    } else if (str_eq(path, "/")) {
        str_copy(s_cwd, "/", 256);
    } else if (str_eq(path, "..")) {
        // Go up one level
        int len = str_len(s_cwd);
        while (len > 1 && s_cwd[len - 1] != '/') len--;
        if (len > 1) len--;  // Remove trailing slash
        s_cwd[len] = '\0';
        if (s_cwd[0] == '\0') s_cwd[0] = '/';
    } else if (path[0] == '/') {
        str_copy(s_cwd, path, 256);
    } else {
        // Relative path
        int cwdLen = str_len(s_cwd);
        if (cwdLen > 1) {
            s_cwd[cwdLen++] = '/';
        }
        str_copy(s_cwd + cwdLen, path, 256 - cwdLen);
    }
}

static void cmd_cat(const char* filename) {
    if (!filename || filename[0] == '\0') {
        output_string("cat: missing operand\n");
        return;
    }
    // TODO: implement VFS file reading
    output_string("cat: ");
    output_string(filename);
    output_string(": No such file or directory\n");
}

static void cmd_echo(const char* text) {
    if (text && text[0]) {
        output_string(text);
    }
    output_string("\n");
}

static void cmd_whoami() {
    output_string(s_username);
    output_string("\n");
}

static void cmd_hostname() {
    output_string(s_hostname);
    output_string("\n");
}

static void cmd_uptime() {
    char buf[64];
    uint32_t days = s_uptimeSeconds / 86400;
    uint32_t hours = (s_uptimeSeconds % 86400) / 3600;
    uint32_t mins = (s_uptimeSeconds % 3600) / 60;
    uint32_t secs = s_uptimeSeconds % 60;
    
    // Simple number to string
    int i = 0;
    buf[i++] = 'u'; buf[i++] = 'p'; buf[i++] = ' ';
    if (days > 0) {
        buf[i++] = '0' + (days / 10) % 10;
        buf[i++] = '0' + days % 10;
        buf[i++] = ' '; buf[i++] = 'd'; buf[i++] = 'a'; buf[i++] = 'y'; buf[i++] = 's'; buf[i++] = ' ';
    }
    buf[i++] = '0' + (hours / 10) % 10;
    buf[i++] = '0' + hours % 10;
    buf[i++] = ':';
    buf[i++] = '0' + (mins / 10) % 10;
    buf[i++] = '0' + mins % 10;
    buf[i++] = ':';
    buf[i++] = '0' + (secs / 10) % 10;
    buf[i++] = '0' + secs % 10;
    buf[i++] = '\n';
    buf[i] = '\0';
    
    output_string(buf);
}

static void cmd_uname(const char* flags) {
    if (!flags || flags[0] == '\0' || str_eq(flags, "-s")) {
        output_string("guideXOS\n");
    } else if (str_eq(flags, "-a")) {
        output_string("guideXOS guideXOS 1.0.0 guideXOS Kernel x86_64\n");
    } else if (str_eq(flags, "-r")) {
        output_string("1.0.0\n");
    } else if (str_eq(flags, "-m")) {
        output_string("x86_64\n");
    } else {
        output_string("guideXOS\n");
    }
}

static void cmd_mem() {
    output_string("Memory Information:\n");
    output_string("  Total:     128 MB\n");
    output_string("  Used:       32 MB\n");
    output_string("  Free:       96 MB\n");
    output_string("  Buffers:     8 MB\n");
}

static void cmd_cpuinfo() {
    output_string("CPU Information:\n");
    output_string("  Model:    guideXOS Virtual CPU\n");
    output_string("  Cores:    1\n");
    output_string("  Speed:    3.0 GHz\n");
    output_string("  Cache:    4 MB\n");
}

static void cmd_ps() {
    output_string("  PID TTY          TIME CMD\n");
    output_string("    1 tty1     00:00:00 init\n");
    output_string("    2 tty1     00:00:00 kernel\n");
    output_string("    3 tty1     00:00:00 desktop\n");
    output_string("    4 tty1     00:00:00 shell\n");
}

static void cmd_date() {
    output_string("Thu Jan  1 00:00:00 UTC 2026\n");
}

static void cmd_mkdir(const char* dirname) {
    if (!dirname || dirname[0] == '\0') {
        output_string("mkdir: missing operand\n");
        return;
    }
    output_string("mkdir: created directory '");
    output_string(dirname);
    output_string("'\n");
}

static void cmd_touch(const char* filename) {
    if (!filename || filename[0] == '\0') {
        output_string("touch: missing operand\n");
        return;
    }
    output_string("touch: created '");
    output_string(filename);
    output_string("'\n");
}

static void cmd_rm(const char* filename) {
    if (!filename || filename[0] == '\0') {
        output_string("rm: missing operand\n");
        return;
    }
    output_string("rm: cannot remove '");
    output_string(filename);
    output_string("': No such file or directory\n");
}

static void cmd_not_found(const char* cmd) {
    output_string(cmd);
    output_string(": command not found\n");
}

// ============================================================
// Command Parser and Executor
// ============================================================

static void parse_args(const char* cmd, char args[MAX_ARGS][64], uint32_t& argCount) {
    argCount = 0;
    uint32_t i = 0;
    uint32_t argPos = 0;
    bool inArg = false;
    
    while (cmd[i] && argCount < MAX_ARGS) {
        if (cmd[i] == ' ' || cmd[i] == '\t') {
            if (inArg) {
                args[argCount][argPos] = '\0';
                argCount++;
                argPos = 0;
                inArg = false;
            }
        } else {
            inArg = true;
            if (argPos < 63) {
                args[argCount][argPos++] = cmd[i];
            }
        }
        i++;
    }
    
    if (inArg && argPos > 0) {
        args[argCount][argPos] = '\0';
        argCount++;
    }
}

static void execute_command(const char* cmd) {
    // Parse command into arguments
    char args[MAX_ARGS][64];
    uint32_t argCount = 0;
    parse_args(cmd, args, argCount);
    
    if (argCount == 0) return;

    
    const char* command = args[0];
    const char* arg1 = (argCount > 1) ? args[1] : "";
    
    // POSIX commands
    if (str_eq(command, "help") || str_eq(command, "?")) {
        cmd_help();
    } else if (str_eq(command, "clear") || str_eq(command, "cls")) {
        cmd_clear();
    } else if (str_eq(command, "pwd")) {
        cmd_pwd();
    } else if (str_eq(command, "ls") || str_eq(command, "dir")) {
        cmd_ls(arg1);
    } else if (str_eq(command, "ll")) {
        cmd_ll(arg1);
    } else if (str_eq(command, "cd")) {
        cmd_cd(arg1);
    } else if (str_eq(command, "cat") || str_eq(command, "type")) {
        cmd_cat(arg1);
    } else if (str_eq(command, "echo")) {
        // Rejoin args for echo
        char text[256] = "";
        uint32_t pos = 0;
        for (uint32_t i = 1; i < argCount; i++) {
            if (i > 1 && pos < 255) text[pos++] = ' ';
            for (uint32_t j = 0; args[i][j] && pos < 255; j++) {
                text[pos++] = args[i][j];
            }
        }
        text[pos] = '\0';
        cmd_echo(text);
    } else if (str_eq(command, "whoami")) {
        cmd_whoami();
    } else if (str_eq(command, "hostname")) {
        cmd_hostname();
    } else if (str_eq(command, "uptime")) {
        cmd_uptime();
    } else if (str_eq(command, "uname")) {
        cmd_uname(arg1);
    } else if (str_eq(command, "mem") || str_eq(command, "free")) {
        cmd_mem();
    } else if (str_eq(command, "cpuinfo") || str_eq(command, "lscpu")) {
        cmd_cpuinfo();
    } else if (str_eq(command, "ps")) {
        cmd_ps();
    } else if (str_eq(command, "date") || str_eq(command, "time")) {
        cmd_date();
    } else if (str_eq(command, "mkdir") || str_eq(command, "md")) {
        cmd_mkdir(arg1);
    } else if (str_eq(command, "touch")) {
        cmd_touch(arg1);
    } else if (str_eq(command, "rm") || str_eq(command, "del")) {
        cmd_rm(arg1);
    } else if (str_eq(command, "rmdir") || str_eq(command, "rd")) {
        cmd_rm(arg1);
    } else if (str_eq(command, "cp") || str_eq(command, "copy")) {
        output_string("cp: TODO\n");
    } else if (str_eq(command, "mv") || str_eq(command, "ren") || str_eq(command, "move")) {
        output_string("mv: TODO\n");
    } else if (str_eq(command, "exit") || str_eq(command, "quit")) {
        close();
    } else if (str_eq(command, "reboot") || str_eq(command, "restart")) {
        output_string("Rebooting...\n");
        perform_restart();
    } else if (str_eq(command, "shutdown") || str_eq(command, "poweroff") || str_eq(command, "halt")) {
        output_string("Shutting down...\n");
        perform_shutdown();
    } else if (str_eq(command, "sleep") || str_eq(command, "suspend")) {
        output_string("Entering sleep mode...\n");
        perform_sleep();
    } else if (str_eq(command, "env")) {
        output_string("PATH=/bin:/usr/bin\n");
        output_string("HOME=/home/root\n");
        output_string("USER=root\n");
        output_string("SHELL=/bin/sh\n");
        output_string("TERM=guideXOS-256color\n");
    } else if (str_eq(command, "history")) {
        for (uint32_t i = 0; i < s_historyCount; i++) {
            char num[8];
            num[0] = ' ';
            num[1] = ' ';
            num[2] = '0' + ((i + 1) / 10) % 10;
            num[3] = '0' + (i + 1) % 10;
            num[4] = ' ';
            num[5] = ' ';
            num[6] = '\0';
            output_string(num);
            output_string(s_history[i]);
            output_string("\n");
        }
    } else {
        cmd_not_found(command);
    }
}

// ============================================================
// Public API Implementation
// ============================================================

void init() {
    s_state = ShellState::Closed;
    s_cmdLen = 0;
    s_cursorPos = 0;
    s_outputCount = 0;
    s_scrollOffset = 0;
    s_historyCount = 0;
    s_historyIdx = 0;
    str_copy(s_cwd, "/", 256);
}

bool is_open() {
    return s_state != ShellState::Closed;
}

void open() {
    if (s_state == ShellState::Closed) {
        s_state = ShellState::Open;
        s_cmdLen = 0;
        s_cursorPos = 0;
        
        // Welcome message
        output_string("guideXOS Shell v1.0\n");
        output_string("Type 'help' for available commands.\n");
        output_string("\n");
    }
}

void close() {
    s_state = ShellState::Closed;
}

void toggle() {
    if (s_state == ShellState::Closed) {
        open();
    } else {
        close();
    }
}

void toggle_fullscreen() {
    if (s_state == ShellState::Open) {
        s_state = ShellState::Fullscreen;
    } else if (s_state == ShellState::Fullscreen) {
        s_state = ShellState::Open;
    }
}

ShellState get_state() {
    return s_state;
}

const char* get_cwd() {
    return s_cwd;
}

void process_char(char c) {
    if (s_state == ShellState::Closed) return;
    
    if (c == '\n' || c == '\r') {
        // Execute command
        s_cmdBuffer[s_cmdLen] = '\0';
        
        // Add to history
        if (s_cmdLen > 0) {
            if (s_historyCount < MAX_HISTORY) {
                str_copy(s_history[s_historyCount], s_cmdBuffer, MAX_CMD_LENGTH);
                s_historyCount++;
            } else {
                for (uint32_t i = 0; i < MAX_HISTORY - 1; i++) {
                    str_copy(s_history[i], s_history[i + 1], MAX_CMD_LENGTH);
                }
                str_copy(s_history[MAX_HISTORY - 1], s_cmdBuffer, MAX_CMD_LENGTH);
            }
            s_historyIdx = s_historyCount;
        }
        
        // Show command in output
        char prompt[256];
        int p = 0;
        prompt[p++] = '[';
        for (const char* s = s_username; *s; s++) prompt[p++] = *s;
        prompt[p++] = '@';
        for (const char* s = s_hostname; *s; s++) prompt[p++] = *s;
        prompt[p++] = ' ';
        for (const char* s = s_cwd; *s; s++) prompt[p++] = *s;
        prompt[p++] = ']';
        prompt[p++] = '$';
        prompt[p++] = ' ';
        for (uint32_t i = 0; i < s_cmdLen; i++) prompt[p++] = s_cmdBuffer[i];
        prompt[p] = '\0';
        output_line(prompt);
        
        // Execute
        if (s_cmdLen > 0) {
            execute_command(s_cmdBuffer);
        }
        
        s_cmdLen = 0;
        s_cursorPos = 0;
    } else if (c == '\b' || c == 127) {
        // Backspace
        if (s_cursorPos > 0) {
            // Shift characters left
            for (uint32_t i = s_cursorPos - 1; i < s_cmdLen - 1; i++) {
                s_cmdBuffer[i] = s_cmdBuffer[i + 1];
            }
            s_cmdLen--;
            s_cursorPos--;
        }
    } else if (c == '\t') {
        // Tab completion (TODO)
    } else if (c >= 32 && c < 127) {
        // Printable character
        if (s_cmdLen < MAX_CMD_LENGTH - 1) {
            // Insert at cursor position
            for (uint32_t i = s_cmdLen; i > s_cursorPos; i--) {
                s_cmdBuffer[i] = s_cmdBuffer[i - 1];
            }
            s_cmdBuffer[s_cursorPos] = c;
            s_cmdLen++;
            s_cursorPos++;
        }
    }
}

void process_key(uint32_t key) {
    if (s_state == ShellState::Closed) return;
    
    switch (key) {
        case KEY_UP:
            if (s_historyIdx > 0) {
                s_historyIdx--;
                str_copy(s_cmdBuffer, s_history[s_historyIdx], MAX_CMD_LENGTH);
                s_cmdLen = str_len(s_cmdBuffer);
                s_cursorPos = s_cmdLen;
            }
            break;
        case KEY_DOWN:
            if (s_historyIdx < s_historyCount - 1) {
                s_historyIdx++;
                str_copy(s_cmdBuffer, s_history[s_historyIdx], MAX_CMD_LENGTH);
                s_cmdLen = str_len(s_cmdBuffer);
                s_cursorPos = s_cmdLen;
            } else {
                s_historyIdx = s_historyCount;
                s_cmdLen = 0;
                s_cursorPos = 0;
            }
            break;
        case KEY_LEFT:
            if (s_cursorPos > 0) s_cursorPos--;
            break;
        case KEY_RIGHT:
            if (s_cursorPos < s_cmdLen) s_cursorPos++;
            break;
        case KEY_HOME:
            s_cursorPos = 0;
            break;
        case KEY_END:
            s_cursorPos = s_cmdLen;
            break;
        case KEY_DELETE:
            if (s_cursorPos < s_cmdLen) {
                for (uint32_t i = s_cursorPos; i < s_cmdLen - 1; i++) {
                    s_cmdBuffer[i] = s_cmdBuffer[i + 1];
                }
                s_cmdLen--;
            }
            break;
        case KEY_PGUP:
            if (s_scrollOffset < s_outputCount) s_scrollOffset += 5;
            if (s_scrollOffset > s_outputCount) s_scrollOffset = s_outputCount;
            break;
        case KEY_PGDN:
            if (s_scrollOffset > 5) s_scrollOffset -= 5;
            else s_scrollOffset = 0;
            break;
        default:
            if (key < 128) {
                process_char((char)key);
            }
            break;
    }
}

void execute(const char* cmd) {
    str_copy(s_cmdBuffer, cmd, MAX_CMD_LENGTH);
    s_cmdLen = str_len(s_cmdBuffer);
    execute_command(s_cmdBuffer);
    s_cmdLen = 0;
    s_cursorPos = 0;
}

// ============================================================
// Drawing
// ============================================================

static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void draw_char_at(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 126) c = ' ';
    const uint8_t* glyph = s_font[c - 32];
    
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row / 2];  // Scale 8 rows to 16
        for (int col = 0; col < 8; col++) {
            // Bit 0 is leftmost pixel, bit 7 is rightmost
            uint32_t color = (bits & (1 << col)) ? fg : bg;
            framebuffer::put_pixel(x + col, y + row, color);
        }
    }
}

static void draw_string_at(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg) {
    while (*str) {
        draw_char_at(x, y, *str, fg, bg);
        x += kFontW;
        str++;
    }
}

void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (s_state == ShellState::Closed) return;
    
    const uint32_t bgColor = rgb(20, 20, 30);
    const uint32_t fgColor = rgb(200, 200, 210);
    const uint32_t promptColor = rgb(100, 180, 100);
    const uint32_t titleBg = rgb(40, 50, 70);
    const uint32_t borderColor = rgb(80, 100, 140);
    
    // Draw background
    for (uint32_t py = y; py < y + h; py++) {
        for (uint32_t px = x; px < x + w; px++) {
            framebuffer::put_pixel(px, py, bgColor);
        }
    }
    
    // Draw border
    for (uint32_t px = x; px < x + w; px++) {
        framebuffer::put_pixel(px, y, borderColor);
        framebuffer::put_pixel(px, y + h - 1, borderColor);
    }
    for (uint32_t py = y; py < y + h; py++) {
        framebuffer::put_pixel(x, py, borderColor);
        framebuffer::put_pixel(x + w - 1, py, borderColor);
    }
    
    // Draw title bar
    for (uint32_t py = y + 1; py < y + 24; py++) {
        for (uint32_t px = x + 1; px < x + w - 1; px++) {
            framebuffer::put_pixel(px, py, titleBg);
        }
    }
    draw_string_at(x + 8, y + 4, "Terminal - guideXOS Shell", fgColor, titleBg);
    
    // Draw close button [X] in top-right corner
    uint32_t closeBtnX = x + w - 22;
    uint32_t closeBtnY = y + 4;
    // Red background for close button
    for (uint32_t py = closeBtnY; py < closeBtnY + 16; py++) {
        for (uint32_t px = closeBtnX; px < closeBtnX + 18; px++) {
            framebuffer::put_pixel(px, py, rgb(180, 60, 60));
        }
    }
    draw_string_at(closeBtnX + 5, closeBtnY, "X", rgb(255, 255, 255), rgb(180, 60, 60));
    
    // Calculate text area
    uint32_t textX = x + 8;
    uint32_t textY = y + 28;
    uint32_t textW = w - 16;
    uint32_t textH = h - 48;
    uint32_t charsPerLine = textW / kFontW;
    uint32_t linesVisible = textH / kFontH;
    
    // Draw output lines
    uint32_t startLine = 0;
    if (s_outputCount > linesVisible + s_scrollOffset) {
        startLine = s_outputCount - linesVisible - s_scrollOffset;
    }
    
    uint32_t lineY = textY;
    for (uint32_t i = startLine; i < s_outputCount && (lineY + kFontH) < (y + h - 20); i++) {
        draw_string_at(textX, lineY, s_output[i], fgColor, bgColor);
        lineY += kFontH;
    }
    
    // Draw prompt and current command
    uint32_t promptY = y + h - 20;
    char prompt[256];
    int p = 0;
    prompt[p++] = '[';
    for (const char* s = s_username; *s && p < 240; s++) prompt[p++] = *s;
    prompt[p++] = '@';
    for (const char* s = s_hostname; *s && p < 240; s++) prompt[p++] = *s;
    prompt[p++] = ' ';
    for (const char* s = s_cwd; *s && p < 240; s++) prompt[p++] = *s;
    prompt[p++] = ']';
    prompt[p++] = '$';
    prompt[p++] = ' ';
    prompt[p] = '\0';
    
    draw_string_at(textX, promptY, prompt, promptColor, bgColor);
    
    uint32_t cmdX = textX + str_len(prompt) * kFontW;
    for (uint32_t i = 0; i < s_cmdLen; i++) {
        draw_char_at(cmdX + i * kFontW, promptY, s_cmdBuffer[i], fgColor, bgColor);
    }
    
    // Draw cursor
    uint32_t cursorX = cmdX + s_cursorPos * kFontW;
    for (int cy = 0; cy < kFontH; cy++) {
        for (int cx = 0; cx < 2; cx++) {
            framebuffer::put_pixel(cursorX + cx, promptY + cy, fgColor);
        }
    }
}

} // namespace shell
} // namespace kernel
