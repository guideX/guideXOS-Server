// guideXOS Kernel Shell - Implementation
//
// Interactive command-line shell with POSIX and Windows command support.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/shell.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/icmp.h"
#include "include/kernel/ipv4.h"
#include "include/kernel/nic.h"
#include "include/kernel/udp.h"
#include "include/kernel/dns.h"
#include "include/kernel/dhcp.h"
#include "include/kernel/vfs.h"
#include "include/kernel/block_device.h"
#include "include/kernel/fs_fat.h"
#include "include/kernel/desktop.h"
#include "include/kernel/kernel_app.h"

// Forward declarations for power functions (defined in desktop.cpp, outside any namespace)
extern void perform_shutdown();
extern void perform_restart();
extern void perform_sleep();

// Forward declaration for GUI test mode (defined in desktop.cpp, outside any namespace)
extern void desktop_run_test_mode();

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
    output_string("  head, tail     - Show beginning/end of file\n");
    output_string("  mkdir, md      - Create directory (not impl)\n");
    output_string("  touch          - Create empty file (not impl)\n");
    output_string("  rm, del        - Remove file (not impl)\n");
    output_string("  df             - Disk space usage\n");
    output_string("  stat           - File status\n");
    output_string("\n");
    output_string("VFS Commands (Phase 7.5):\n");
    output_string("  vfsinfo        - Show filesystems and mounts\n");
    output_string("  vfsmount / <n> - Mount block device n at /\n");
    output_string("  vfsls [path]   - List directory via VFS\n");
    output_string("  vfscat <file>  - Display file via VFS\n");
    output_string("  vfsstat <file> - File info via VFS\n");
    output_string("  vfstest        - Run filesystem tests\n");
    output_string("\n");
    output_string("System Info:\n");
    output_string("  neofetch       - System info with logo\n");
    output_string("  uname          - System information\n");
    output_string("  version, ver   - OS version\n");
    output_string("  about          - About guideXOS\n");
    output_string("  dmesg          - Kernel messages\n");
    output_string("  whoami         - Current user\n");
    output_string("  id             - User/group IDs\n");
    output_string("  hostname       - System hostname\n");
    output_string("  uptime         - System uptime\n");
    output_string("  w, who         - Who is logged in\n");
    output_string("  mem, free      - Memory info\n");
    output_string("  cpuinfo, lscpu - CPU information\n");
    output_string("  ps, top        - Process information\n");
    output_string("  date, time     - Current date/time\n");
    output_string("  cal            - Calendar\n");
    output_string("\n");
    output_string("Hardware:\n");
    output_string("  lspci          - List PCI devices\n");
    output_string("  lsusb          - List USB devices\n");
    output_string("  lsblk          - List block devices\n");
    output_string("\n");
    output_string("Network:\n");
    output_string("  ping <ip>      - Send ICMP echo request\n");
    output_string("  ifconfig, ip   - Network interface info\n");
    output_string("  ipconfig       - Windows-style IP config\n");
    output_string("  ipconfig /all  - Full IP configuration\n");
    output_string("  ipconfig /flushdns - Flush DNS cache\n");
    output_string("  dhcp           - DHCP client status\n");
    output_string("  dhcp /discover - Discover DHCP server\n");
    output_string("  dhcp /release  - Release DHCP lease\n");
    output_string("  dhcp /renew    - Renew DHCP lease\n");
    output_string("  nslookup <host>- DNS lookup\n");
    output_string("  dig <host>     - DNS lookup (detailed)\n");
    output_string("  host <host>    - DNS lookup (simple)\n");
    output_string("  route          - Routing table\n");
    output_string("  netstat, ss    - Network connections\n");
    output_string("  nc <ip> <port> - Send UDP datagram\n");
    output_string("  udpstat        - UDP statistics\n");
    output_string("\n");
    output_string("Desktop:\n");
    output_string("  apps           - List applications\n");
    output_string("  workspaces     - Workspace info\n");
    output_string("  osk            - On-screen keyboard\n");
    output_string("  testmode       - GUI compositor test\n");
    output_string("\n");
    output_string("Utilities:\n");
    output_string("  clear, cls     - Clear screen\n");
    output_string("  echo           - Print text\n");
    output_string("  which          - Locate command\n");
    output_string("  alias          - Show aliases\n");
    output_string("  history        - Command history\n");
    output_string("  env            - Environment vars\n");
    output_string("  colors         - Color test\n");
    output_string("  fortune, tip   - Random tip\n");
    output_string("  motd           - Message of the day\n");
    output_string("  banner <text>  - Display banner\n");
    output_string("\n");
    output_string("Power:\n");
    output_string("  reboot         - Restart system\n");
    output_string("  shutdown       - Power off system\n");
    output_string("  sleep          - Suspend system\n");
    output_string("  exit           - Close shell\n");
    output_string("\n");
    output_string("  help, ?        - Show this help\n");
    output_string("  man <cmd>      - Command manual\n");
}

static void cmd_clear() {
    s_outputCount = 0;
    s_scrollOffset = 0;
}

static void cmd_pwd() {
    output_string(s_cwd);
}

static void cmd_ls(const char* path) {
    // Determine target path - use argument if provided, else current directory
    char targetPath[256];
    if (path && path[0] != '\0') {
        if (path[0] == '/') {
            // Absolute path
            str_copy(targetPath, path, 256);
        } else {
            // Relative path - combine with cwd
            str_copy(targetPath, s_cwd, 256);
            int len = str_len(targetPath);
            if (len > 1 && targetPath[len-1] != '/') {
                targetPath[len++] = '/';
            }
            str_copy(targetPath + len, path, 256 - len);
        }
    } else {
        str_copy(targetPath, s_cwd, 256);
    }
    
    // Try to open directory via VFS
    uint8_t dirHandle = vfs::opendir(targetPath);
    if (dirHandle == 0xFF) {
        // VFS not mounted or directory not found - show fallback
        output_string("ls: cannot access '");
        output_string(targetPath);
        output_string("': No filesystem mounted\n");
        output_string("Use 'vfsmount / <device>' to mount a filesystem first\n");
        return;
    }
    
    vfs::DirEntry entry;
    int count = 0;
    
    while (vfs::readdir(dirHandle, &entry)) {
        // Skip . and .. entries
        if (entry.name[0] == '.' && (entry.name[1] == '\0' || 
            (entry.name[1] == '.' && entry.name[2] == '\0'))) {
            continue;
        }
        
        // Format output
        if (entry.type == vfs::FILE_TYPE_DIRECTORY) {
            output_string(entry.name);
            output_string("/\n");
        } else {
            output_string(entry.name);
            output_string("\n");
        }
        count++;
    }
    
    vfs::closedir(dirHandle);
    
    if (count == 0) {
        output_string("(empty directory)\n");
    }
}

static void cmd_ll(const char* path) {
    // Determine target path
    char targetPath[256];
    if (path && path[0] != '\0') {
        if (path[0] == '/') {
            str_copy(targetPath, path, 256);
        } else {
            str_copy(targetPath, s_cwd, 256);
            int len = str_len(targetPath);
            if (len > 1 && targetPath[len-1] != '/') {
                targetPath[len++] = '/';
            }
            str_copy(targetPath + len, path, 256 - len);
        }
    } else {
        str_copy(targetPath, s_cwd, 256);
    }
    
    // Try to open directory via VFS
    uint8_t dirHandle = vfs::opendir(targetPath);
    if (dirHandle == 0xFF) {
        output_string("ls: cannot access '");
        output_string(targetPath);
        output_string("': No filesystem mounted\n");
        return;
    }
    
    vfs::DirEntry entry;
    int count = 0;
    uint64_t totalSize = 0;
    
    while (vfs::readdir(dirHandle, &entry)) {
        // Skip . and .. entries
        if (entry.name[0] == '.' && (entry.name[1] == '\0' || 
            (entry.name[1] == '.' && entry.name[2] == '\0'))) {
            continue;
        }
        
        // Permission string
        if (entry.type == vfs::FILE_TYPE_DIRECTORY) {
            output_string("drwxr-xr-x  ");
        } else {
            if (entry.isReadOnly) {
                output_string("-r--r--r--  ");
            } else {
                output_string("-rw-r--r--  ");
            }
        }
        
        // Owner/group
        output_string("1 root root  ");
        
        // Size (right-aligned in 8 chars)
        char sizeStr[16];
        uint64_t sz = entry.size;
        int i = 0;
        if (sz == 0) { sizeStr[i++] = '0'; }
        else {
            char tmp[16];
            int j = 0;
            while (sz > 0) { tmp[j++] = '0' + (sz % 10); sz /= 10; }
            while (j > 0) { sizeStr[i++] = tmp[--j]; }
        }
        sizeStr[i] = '\0';
        // Pad to 8 chars
        int padding = 8 - i;
        while (padding-- > 0) output_string(" ");
        output_string(sizeStr);
        
        output_string(" Jan  1 00:00 ");
        output_string(entry.name);
        
        if (entry.type == vfs::FILE_TYPE_DIRECTORY) {
            output_string("/");
        }
        output_string("\n");
        
        count++;
        totalSize += entry.size;
    }
    
    vfs::closedir(dirHandle);
    
    // Print total
    output_string("total ");
    char countStr[8];
    countStr[0] = '0' + (count / 10) % 10;
    countStr[1] = '0' + count % 10;
    countStr[2] = '\0';
    output_string(countStr);
    output_string("\n");
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
    
    // Build full path
    char fullPath[256];
    if (filename[0] == '/') {
        // Absolute path
        str_copy(fullPath, filename, 256);
    } else {
        // Relative path - combine with cwd
        str_copy(fullPath, s_cwd, 256);
        int len = str_len(fullPath);
        if (len > 1 && fullPath[len-1] != '/') {
            fullPath[len++] = '/';
        }
        str_copy(fullPath + len, filename, 256 - len);
    }
    
    // Try to open file via VFS
    uint8_t handle = vfs::open(fullPath, vfs::OPEN_READ);
    if (handle == 0xFF) {
        output_string("cat: ");
        output_string(fullPath);
        output_string(": No such file or directory\n");
        return;
    }
    
    // Read and display file contents
    char buffer[513];  // 512 bytes + null terminator
    int32_t bytesRead;
    int32_t totalRead = 0;
    
    while ((bytesRead = vfs::read(handle, buffer, 512)) > 0) {
        buffer[bytesRead] = '\0';
        output_string(buffer);
        totalRead += bytesRead;
    }
    
    vfs::close(handle);
    
    // Add newline if file didn't end with one
    if (totalRead > 0) {
        output_string("\n");
    }
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
// Additional Commands (ported from guideXOS.Legacy)
// ============================================================

static void cmd_neofetch() {
    // ASCII art logo (simplified guideXOS logo)
    output_string("        ??????? ???  ??? ??????? ????????\n");
    output_string("       ???????? ?????????????????????????\n");
    output_string("       ???  ???? ?????? ???   ???????????\n");
    output_string("       ???   ??? ?????? ???   ???????????\n");
    output_string("       ????????????? ????????????????????\n");
    output_string("        ??????? ???  ??? ??????? ????????\n");
    output_string("\n");
    output_string("  root@guideXOS\n");
    output_string("  -------------\n");
    output_string("  OS:        guideXOS Server\n");
    output_string("  Kernel:    guideXOS Kernel 1.0.0\n");
    output_string("  Shell:     guideXOS Shell v1.0\n");
    
    // Uptime
    char buf[64];
    uint32_t hours = s_uptimeSeconds / 3600;
    uint32_t mins = (s_uptimeSeconds % 3600) / 60;
    int i = 0;
    buf[i++] = ' '; buf[i++] = ' ';
    buf[i++] = 'U'; buf[i++] = 'p'; buf[i++] = 't'; buf[i++] = 'i'; buf[i++] = 'm'; buf[i++] = 'e'; buf[i++] = ':';
    buf[i++] = ' '; buf[i++] = ' '; buf[i++] = ' '; buf[i++] = ' ';
    buf[i++] = '0' + (hours / 10) % 10;
    buf[i++] = '0' + hours % 10;
    buf[i++] = ' '; buf[i++] = 'h'; buf[i++] = 'o'; buf[i++] = 'u'; buf[i++] = 'r'; buf[i++] = 's'; buf[i++] = ','; buf[i++] = ' ';
    buf[i++] = '0' + (mins / 10) % 10;
    buf[i++] = '0' + mins % 10;
    buf[i++] = ' '; buf[i++] = 'm'; buf[i++] = 'i'; buf[i++] = 'n'; buf[i++] = 's';
    buf[i++] = '\n';
    buf[i] = '\0';
    output_string(buf);
    
    output_string("  Resolution: 1024x768\n");
    output_string("  Memory:    128 MB\n");
    output_string("  CPU:       x86_64\n");
    output_string("\n");
}

static void cmd_version() {
    output_string("guideXOS Server\n");
    output_string("Version: 1.0.0\n");
    output_string("Build:   2026.01.01\n");
    output_string("Arch:    x86_64\n");
    output_string("\n");
    output_string("Copyright (c) 2026 guideX\n");
}

static void cmd_about() {
    output_string("??????????????????????????????????????????????\n");
    output_string("?          guideXOS Server                   ?\n");
    output_string("??????????????????????????????????????????????\n");
    output_string("?  A modern operating system built from      ?\n");
    output_string("?  scratch, featuring a windowing system,    ?\n");
    output_string("?  desktop environment, and native apps.     ?\n");
    output_string("?                                            ?\n");
    output_string("?  Features:                                 ?\n");
    output_string("?  - Desktop with icons and taskbar          ?\n");
    output_string("?  - Window management (drag, resize)        ?\n");
    output_string("?  - Start menu with applications            ?\n");
    output_string("?  - Interactive shell                       ?\n");
    output_string("?  - VNC remote access                       ?\n");
    output_string("?                                            ?\n");
    output_string("?  github.com/guideX/guidexos-server         ?\n");
    output_string("??????????????????????????????????????????????\n");
}

static void cmd_apps() {
    output_string("Available Applications:\n");
    output_string("\n");
    output_string("  Calculator   - Basic calculator\n");
    output_string("  Clock        - Digital clock\n");
    output_string("  Console      - Command-line terminal\n");
    output_string("  Files        - File explorer\n");
    output_string("  ImageViewer  - View images\n");
    output_string("  Notepad      - Text editor\n");
    output_string("  Paint        - Drawing application\n");
    output_string("  TaskManager  - Process viewer\n");
    output_string("\n");
    output_string("Launch from Start Menu or double-click desktop icons.\n");
}

static void cmd_workspaces() {
    output_string("Workspace Information:\n");
    output_string("\n");
    output_string("  Current Workspace: 1\n");
    output_string("  Total Workspaces:  4\n");
    output_string("\n");
    output_string("Keyboard Shortcuts:\n");
    output_string("  Ctrl+Alt+Left    - Previous workspace\n");
    output_string("  Ctrl+Alt+Right   - Next workspace\n");
    output_string("  Ctrl+Alt+1-4     - Switch to workspace 1-4\n");
    output_string("\n");
}

static void cmd_colors() {
    output_string("Terminal Color Test:\n");
    output_string("\n");
    output_string("  Standard Colors:\n");
    output_string("  Black   Red     Green   Yellow\n");
    output_string("  Blue    Magenta Cyan    White\n");
    output_string("\n");
    output_string("  Bright Colors:\n");
    output_string("  Black   Red     Green   Yellow\n");
    output_string("  Blue    Magenta Cyan    White\n");
    output_string("\n");
}

static void cmd_fortune() {
    // Random fortune cookies / tips
    static int fortune_idx = 0;
    const char* fortunes[] = {
        "Tip: Use 'clear' or 'cls' to clear the screen.",
        "Tip: Press Up/Down arrows to navigate command history.",
        "Tip: Type 'neofetch' for system information.",
        "Tip: Use 'cd ..' to go up one directory.",
        "Tip: Press Escape to close the terminal window.",
        "Tip: Double-click desktop icons to launch apps.",
        "Tip: The Start Menu has all available applications.",
        "Tip: Use 'help' to see all available commands.",
    };
    const int num_fortunes = 8;
    
    output_string(fortunes[fortune_idx % num_fortunes]);
    output_string("\n");
    fortune_idx++;
}

static void cmd_motd() {
    output_string("\n");
    output_string("  Welcome to guideXOS Server!\n");
    output_string("\n");
    output_string("  System Status: Running\n");
    output_string("  Shell:         Active\n");
    output_string("\n");
    output_string("  Type 'help' for available commands.\n");
    output_string("  Type 'apps' to see available applications.\n");
    output_string("  Type 'about' for system information.\n");
    output_string("\n");
}

static void cmd_cal() {
    output_string("    January 2026\n");
    output_string(" Su Mo Tu We Th Fr Sa\n");
    output_string("              1  2  3\n");
    output_string("  4  5  6  7  8  9 10\n");
    output_string(" 11 12 13 14 15 16 17\n");
    output_string(" 18 19 20 21 22 23 24\n");
    output_string(" 25 26 27 28 29 30 31\n");
}

static void cmd_df() {
    output_string("Filesystem      Size   Used  Avail  Use%  Mounted on\n");
    output_string("/dev/ram0       128M    32M    96M   25%  /\n");
    output_string("/dev/vda1       4.0G   1.2G   2.8G   30%  /mnt/disk\n");
    output_string("tmpfs            64M    12M    52M   19%  /tmp\n");
}

static void cmd_id() {
    output_string("uid=0(root) gid=0(root) groups=0(root)\n");
}

static void cmd_w() {
    output_string(" 00:00:00 up ");
    
    char buf[32];
    uint32_t hours = s_uptimeSeconds / 3600;
    uint32_t mins = (s_uptimeSeconds % 3600) / 60;
    int i = 0;
    buf[i++] = '0' + (hours / 10) % 10;
    buf[i++] = '0' + hours % 10;
    buf[i++] = ':';
    buf[i++] = '0' + (mins / 10) % 10;
    buf[i++] = '0' + mins % 10;
    buf[i] = '\0';
    output_string(buf);
    
    output_string(",  1 user,  load average: 0.00, 0.01, 0.05\n");
    output_string("USER     TTY      FROM             LOGIN@   IDLE   JCPU   PCPU WHAT\n");
    output_string("root     tty1     -                00:00    0.00s  0.00s  0.00s shell\n");
}

static void cmd_head(const char* filename) {
    if (!filename || filename[0] == '\0') {
        output_string("head: missing operand\n");
        output_string("Usage: head <file>\n");
        return;
    }
    output_string("head: ");
    output_string(filename);
    output_string(": File system not fully implemented\n");
}

static void cmd_tail(const char* filename) {
    if (!filename || filename[0] == '\0') {
        output_string("tail: missing operand\n");
        output_string("Usage: tail <file>\n");
        return;
    }
    output_string("tail: ");
    output_string(filename);
    output_string(": File system not fully implemented\n");
}

static void cmd_wc(const char* filename) {
    if (!filename || filename[0] == '\0') {
        output_string("wc: missing operand\n");
        output_string("Usage: wc <file>\n");
        return;
    }
    output_string("wc: ");
    output_string(filename);
    output_string(": File system not fully implemented\n");
}

static void cmd_grep(const char* pattern) {
    if (!pattern || pattern[0] == '\0') {
        output_string("grep: missing pattern\n");
        output_string("Usage: grep <pattern> <file>\n");
        return;
    }
    output_string("grep: File system not fully implemented\n");
}

static void cmd_find(const char* path) {
    if (!path || path[0] == '\0') {
        output_string("find: missing path\n");
        output_string("Usage: find <path> [-name pattern]\n");
        return;
    }
    output_string("find: File system not fully implemented\n");
}

static void cmd_which(const char* program) {
    if (!program || program[0] == '\0') {
        output_string("which: missing argument\n");
        return;
    }
    
    // Check for known commands
    if (str_eq(program, "ls") || str_eq(program, "cd") || str_eq(program, "cat") ||
        str_eq(program, "echo") || str_eq(program, "clear") || str_eq(program, "help")) {
        output_string("/bin/");
        output_string(program);
        output_string("\n");
    } else {
        output_string(program);
        output_string(" not found\n");
    }
}

static void cmd_osk() {
    output_string("On-Screen Keyboard\n");
    output_string("==================\n");
    output_string("\n");
    output_string("The on-screen keyboard provides touch/mouse input\n");
    output_string("for systems without physical keyboards.\n");
    output_string("\n");
    output_string("To access the on-screen keyboard:\n");
    output_string("  - Click the keyboard icon in the system tray\n");
    output_string("  - Or launch from Start Menu > Accessories\n");
    output_string("\n");
    output_string("Note: Currently available in GUI mode only.\n");
}

static void cmd_dmesg() {
    output_string("[    0.000000] guideXOS kernel initializing...\n");
    output_string("[    0.001234] Detecting memory... 128 MB\n");
    output_string("[    0.002345] Initializing framebuffer... 1024x768x32\n");
    output_string("[    0.003456] PS/2 keyboard driver initialized\n");
    output_string("[    0.004567] PS/2 mouse driver initialized\n");
    output_string("[    0.005678] VFS mounted at /\n");
    output_string("[    0.006789] Desktop environment starting...\n");
    output_string("[    0.007890] Shell ready\n");
}

static void cmd_lsblk() {
    output_string("NAME   MAJ:MIN RM  SIZE RO TYPE MOUNTPOINT\n");
    output_string("ram0     1:0    0  128M  0 disk /\n");
    output_string("vda    254:0    0    4G  0 disk \n");
    output_string("|-vda1 254:1    0    4G  0 part /mnt/disk\n");
}

static void cmd_lspci() {
    output_string("00:00.0 Host bridge: guideXOS Virtual Host Bridge\n");
    output_string("00:01.0 VGA compatible controller: guideXOS Framebuffer\n");
    output_string("00:02.0 Ethernet controller: Intel 82540EM Gigabit\n");
    output_string("00:03.0 USB controller: UHCI Host Controller\n");
}

static void cmd_lsusb() {
    output_string("Bus 001 Device 001: ID 0000:0000 guideXOS Virtual Hub\n");
    output_string("Bus 001 Device 002: ID 0627:0001 USB Keyboard\n");
    output_string("Bus 001 Device 003: ID 0627:0001 USB Mouse\n");
}

// ============================================================
// Filesystem Test Commands (Phase 7.5)
// ============================================================

static void cmd_vfsmount(const char* path, const char* devIdx) {
    if (!path || path[0] == '\0') {
        output_string("Usage: vfsmount <path> <block_dev_index>\n");
        output_string("Example: vfsmount / 0\n");
        return;
    }
    
    // Parse device index
    uint8_t idx = 0;
    if (devIdx && devIdx[0] >= '0' && devIdx[0] <= '9') {
        idx = devIdx[0] - '0';
    }
    
    output_string("Mounting block device ");
    char numStr[4] = {'0' + idx, '\0'};
    output_string(numStr);
    output_string(" at ");
    output_string(path);
    output_string("...\n");
    
    uint8_t mountIdx = vfs::mount(path, idx);
    if (mountIdx != 0xFF) {
        output_string("Mount successful (index ");
        numStr[0] = '0' + mountIdx;
        output_string(numStr);
        output_string(")\n");
        
        // Show filesystem type
        const vfs::MountPoint* mp = vfs::get_mount_by_index(mountIdx);
        if (mp) {
            output_string("Filesystem type: ");
            output_string(vfs::fs_type_name(mp->fsType));
            output_string("\n");
        }
    } else {
        output_string("Mount failed!\n");
    }
}

static void cmd_vfsls(const char* path) {
    const char* targetPath = (path && path[0] != '\0') ? path : "/";
    
    output_string("Directory listing: ");
    output_string(targetPath);
    output_string("\n\n");
    
    uint8_t dirHandle = vfs::opendir(targetPath);
    if (dirHandle == 0xFF) {
        output_string("Cannot open directory\n");
        return;
    }
    
    vfs::DirEntry entry;
    int count = 0;
    while (vfs::readdir(dirHandle, &entry)) {
        // Type indicator
        if (entry.type == vfs::FILE_TYPE_DIRECTORY) {
            output_string("[DIR]  ");
        } else {
            output_string("[FILE] ");
        }
        
        // Name
        output_string(entry.name);
        
        // Size for files
        if (entry.type != vfs::FILE_TYPE_DIRECTORY) {
            output_string("  (");
            // Format size
            char sizeStr[16];
            uint64_t sz = entry.size;
            int i = 0;
            if (sz == 0) { sizeStr[i++] = '0'; }
            else {
                char tmp[16];
                int j = 0;
                while (sz > 0) { tmp[j++] = '0' + (sz % 10); sz /= 10; }
                while (j > 0) { sizeStr[i++] = tmp[--j]; }
            }
            sizeStr[i] = '\0';
            output_string(sizeStr);
            output_string(" bytes)");
        }
        
        output_string("\n");
        count++;
    }
    
    vfs::closedir(dirHandle);
    
    output_string("\nTotal: ");
    char countStr[8];
    countStr[0] = '0' + (count / 10) % 10;
    countStr[1] = '0' + count % 10;
    countStr[2] = '\0';
    output_string(countStr);
    output_string(" entries\n");
}

static void cmd_vfscat(const char* path) {
    if (!path || path[0] == '\0') {
        output_string("Usage: vfscat <filepath>\n");
        return;
    }
    
    output_string("Reading file: ");
    output_string(path);
    output_string("\n\n");
    
    uint8_t handle = vfs::open(path, vfs::OPEN_READ);
    if (handle == 0xFF) {
        output_string("Cannot open file\n");
        return;
    }
    
    // Read file in chunks
    char buffer[513];  // 512 bytes + null terminator
    int32_t totalRead = 0;
    int32_t bytesRead;
    
    while ((bytesRead = vfs::read(handle, buffer, 512)) > 0) {
        buffer[bytesRead] = '\0';
        output_string(buffer);
        totalRead += bytesRead;
    }
    
    vfs::close(handle);
    
    output_string("\n\n--- End of file (");
    char sizeStr[16];
    int i = 0;
    int32_t sz = totalRead;
    if (sz == 0) { sizeStr[i++] = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (sz > 0) { tmp[j++] = '0' + (sz % 10); sz /= 10; }
        while (j > 0) { sizeStr[i++] = tmp[--j]; }
    }
    sizeStr[i] = '\0';
    output_string(sizeStr);
    output_string(" bytes read) ---\n");
}

static void cmd_vfsstat(const char* path) {
    if (!path || path[0] == '\0') {
        output_string("Usage: vfsstat <filepath>\n");
        return;
    }
    
    vfs::FileInfo info;
    vfs::Status st = vfs::stat(path, &info);
    
    if (st != vfs::VFS_OK) {
        output_string("Cannot stat file: ");
        output_string(path);
        output_string("\n");
        return;
    }
    
    output_string("File: ");
    output_string(path);
    output_string("\n");
    
    output_string("Name: ");
    output_string(info.name);
    output_string("\n");
    
    output_string("Type: ");
    switch (info.type) {
        case vfs::FILE_TYPE_REGULAR:   output_string("Regular file\n"); break;
        case vfs::FILE_TYPE_DIRECTORY: output_string("Directory\n"); break;
        default:                       output_string("Unknown\n"); break;
    }
    
    output_string("Size: ");
    char sizeStr[16];
    uint64_t sz = info.size;
    int i = 0;
    if (sz == 0) { sizeStr[i++] = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (sz > 0) { tmp[j++] = '0' + (sz % 10); sz /= 10; }
        while (j > 0) { sizeStr[i++] = tmp[--j]; }
    }
    sizeStr[i] = '\0';
    output_string(sizeStr);
    output_string(" bytes\n");
}

static void cmd_vfstest() {
    output_string("=== VFS Filesystem Test Suite ===\n\n");
    
    // Test 1: Check block devices
    output_string("1. Checking block devices...\n");
    uint8_t devCount = block::device_count();
    output_string("   Block devices found: ");
    char countStr[4] = {'0' + devCount, '\0'};
    output_string(countStr);
    output_string("\n");
    
    if (devCount == 0) {
        output_string("   ERROR: No block devices available!\n");
        output_string("   Make sure disk images are attached to QEMU.\n");
        return;
    }
    
    // List block devices
    for (uint8_t i = 0; i < 16 && i < devCount; i++) {
        const block::BlockDevice* dev = block::get_device(i);
        if (dev && dev->active) {
            output_string("   Device ");
            char idxStr[4] = {'0' + i, ':', ' ', '\0'};
            output_string(idxStr);
            output_string(dev->name);
            output_string("\n");
        }
    }
    
    // Test 2: Check mounts
    output_string("\n2. Checking mount points...\n");
    uint8_t mountCount = vfs::mount_count();
    output_string("   Active mounts: ");
    countStr[0] = '0' + mountCount;
    output_string(countStr);
    output_string("\n");
    
    if (mountCount == 0) {
        output_string("   No filesystems mounted.\n");
        output_string("   Try: vfsmount / 0\n");
        return;
    }
    
    // List mounts
    for (uint8_t i = 0; i < 8; i++) {
        const vfs::MountPoint* mp = vfs::get_mount_by_index(i);
        if (mp && mp->active) {
            output_string("   ");
            output_string(mp->path);
            output_string(" -> ");
            output_string(vfs::fs_type_name(mp->fsType));
            output_string("\n");
        }
    }
    
    // Test 3: Try to read a test file
    output_string("\n3. Testing file read...\n");
    output_string("   Attempting to open /test.txt...\n");
    
    uint8_t handle = vfs::open("/test.txt", vfs::OPEN_READ);
    if (handle != 0xFF) {
        char buf[64];
        int32_t bytesRead = vfs::read(handle, buf, 63);
        vfs::close(handle);
        
        if (bytesRead > 0) {
            buf[bytesRead] = '\0';
            output_string("   SUCCESS! Read ");
            char numStr[8];
            int i = 0;
            int32_t n = bytesRead;
            if (n == 0) { numStr[i++] = '0'; }
            else {
                char tmp[8];
                int j = 0;
                while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
                while (j > 0) { numStr[i++] = tmp[--j]; }
            }
            numStr[i] = '\0';
            output_string(numStr);
            output_string(" bytes\n");
            output_string("   Content: ");
            output_string(buf);
            output_string("\n");
        } else {
            output_string("   File opened but read returned 0 bytes\n");
        }
    } else {
        output_string("   Could not open /test.txt\n");
        output_string("   (This is OK if the file doesn't exist)\n");
    }
    
    output_string("\n=== Test complete ===\n");
}

static void cmd_vfsinfo() {
    output_string("=== Filesystem Information ===\n\n");
    
    // Block devices
    output_string("Block Devices:\n");
    uint8_t devCount = block::device_count();
    char numStr[8];
    numStr[0] = '0' + devCount;
    numStr[1] = '\0';
    output_string("  Total: ");
    output_string(numStr);
    output_string("\n");
    
    for (uint8_t i = 0; i < 16; i++) {
        const block::BlockDevice* dev = block::get_device(i);
        if (dev && dev->active) {
            output_string("  [");
            char idxStr[4] = {'0' + i, ']', ' ', '\0'};
            output_string(idxStr);
            output_string(dev->name);
            output_string(" - ");
            
            // Size in MB
            uint64_t sizeBytes = dev->totalSectors * dev->sectorSize;
            uint32_t sizeMB = static_cast<uint32_t>(sizeBytes / (1024 * 1024));
            char sizeStr[16];
            int si = 0;
            if (sizeMB == 0) { sizeStr[si++] = '0'; }
            else {
                char tmp[16];
                int sj = 0;
                while (sizeMB > 0) { tmp[sj++] = '0' + (sizeMB % 10); sizeMB /= 10; }
                while (sj > 0) { sizeStr[si++] = tmp[--sj]; }
            }
            sizeStr[si] = '\0';
            output_string(sizeStr);
            output_string(" MB\n");
        }
    }
    
    output_string("\nMount Points:\n");
    uint8_t mountCount = vfs::mount_count();
    numStr[0] = '0' + mountCount;
    output_string("  Active mounts: ");
    output_string(numStr);
    output_string("\n");
    
    for (uint8_t i = 0; i < 8; i++) {
        const vfs::MountPoint* mp = vfs::get_mount_by_index(i);
        if (mp && mp->active) {
            output_string("  ");
            output_string(mp->path);
            output_string(" -> ");
            output_string(vfs::fs_type_name(mp->fsType));
            output_string(" (device ");
            char devStr[4] = {'0' + mp->blockDevIndex, ')', '\0'};
            output_string(devStr);
            output_string("\n");
        }
    }
    
    if (mountCount == 0) {
        output_string("  (no filesystems mounted)\n");
        output_string("\n  Tip: Use 'vfsmount / <device_num>' to mount\n");
    }
    
    output_string("\n");
}

// ============================================================
// Network Commands
// ============================================================

static void cmd_ifconfig() {
    if (!nic::is_active()) {
        output_string("No network interface active\n");
        return;
    }
    
    const nic::NICDevice* dev = nic::get_device();
    const ipv4::NetworkConfig* cfg = ipv4::get_config();
    
    output_string(dev->name);
    output_string(": flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500\n");
    
    if (cfg && cfg->configured) {
        output_string("        inet ");
        char ipStr[16];
        ipv4::ip_to_string(cfg->ipAddr, ipStr);
        output_string(ipStr);
        output_string("  netmask ");
        ipv4::ip_to_string(cfg->subnetMask, ipStr);
        output_string(ipStr);
        output_string("\n");
    }
    
    output_string("        ether ");
    char macStr[18];
    ethernet::mac_to_string(dev->macAddress, macStr);
    output_string(macStr);
    output_string("\n");
    
    output_string("        RX packets ");
    // Simple number output
    const nic::NetStats* stats = nic::get_stats();
    if (stats) {
        char num[12];
        int i = 0;
        uint32_t n = stats->rxFrames;
        if (n == 0) { num[i++] = '0'; }
        else {
            char tmp[12];
            int j = 0;
            while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
            while (j > 0) { num[i++] = tmp[--j]; }
        }
        num[i] = '\0';
        output_string(num);
    }
    output_string("  bytes ");
    if (stats) {
        char num[12];
        int i = 0;
        uint32_t n = stats->rxBytes;
        if (n == 0) { num[i++] = '0'; }
        else {
            char tmp[12];
            int j = 0;
            while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
            while (j > 0) { num[i++] = tmp[--j]; }
        }
        num[i] = '\0';
        output_string(num);
    }
    output_string("\n");
    
    output_string("        TX packets ");
    if (stats) {
        char num[12];
        int i = 0;
        uint32_t n = stats->txFrames;
        if (n == 0) { num[i++] = '0'; }
        else {
            char tmp[12];
            int j = 0;
            while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
            while (j > 0) { num[i++] = tmp[--j]; }
        }
        num[i] = '\0';
        output_string(num);
    }
    output_string("  bytes ");
    if (stats) {
        char num[12];
        int i = 0;
        uint32_t n = stats->txBytes;
        if (n == 0) { num[i++] = '0'; }
        else {
            char tmp[12];
            int j = 0;
            while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
            while (j > 0) { num[i++] = tmp[--j]; }
        }
        num[i] = '\0';
        output_string(num);
    }
    output_string("\n");
}

static void cmd_ping(const char* target) {
    if (!target || target[0] == '\0') {
        output_string("Usage: ping <ip-address>\n");
        output_string("Example: ping 10.0.2.2\n");
        return;
    }
    
    if (!nic::is_active()) {
        output_string("ping: network interface not active\n");
        return;
    }
    
    if (!ipv4::is_configured()) {
        output_string("ping: network not configured\n");
        return;
    }
    
    // Parse IP address
    uint32_t targetIP;
    if (!ipv4::ip_from_string(target, &targetIP)) {
        output_string("ping: invalid IP address '");
        output_string(target);
        output_string("'\n");
        return;
    }
    
    output_string("PING ");
    output_string(target);
    output_string(" 56 data bytes\n");
    
    // Send 4 pings
    uint16_t sent = 0;
    uint16_t received = 0;
    
    icmp::Status status = icmp::start_ping_session(targetIP, 1000);
    if (status != icmp::ICMP_OK) {
        output_string("ping: failed to start session\n");
        return;
    }
    
    for (int i = 0; i < 4; ++i) {
        status = icmp::ping_session_send();
        if (status != icmp::ICMP_OK) {
            output_string("ping: send failed\n");
            continue;
        }
        sent++;
        
        // Wait for reply (simplified polling)
        bool gotReply = false;
        for (int wait = 0; wait < 100; ++wait) {
            icmp::PingReply reply;
            if (icmp::ping_session_check_reply(&reply)) {
                gotReply = true;
                received++;
                
                // Format output
                output_string("64 bytes from ");
                char ipStr[16];
                ipv4::ip_to_string(reply.srcIP, ipStr);
                output_string(ipStr);
                output_string(": icmp_seq=");
                char seqStr[8];
                seqStr[0] = '0' + ((reply.sequence / 10) % 10);
                seqStr[1] = '0' + (reply.sequence % 10);
                seqStr[2] = '\0';
                output_string(seqStr);
                output_string(" ttl=");
                char ttlStr[8];
                int ti = 0;
                uint16_t t = reply.ttl;
                if (t == 0) { ttlStr[ti++] = '0'; }
                else {
                    char tmp[8];
                    int tj = 0;
                    while (t > 0) { tmp[tj++] = '0' + (t % 10); t /= 10; }
                    while (tj > 0) { ttlStr[ti++] = tmp[--tj]; }
                }
                ttlStr[ti] = '\0';
                output_string(ttlStr);
                output_string(" time=");
                char rttStr[8];
                int ri = 0;
                uint16_t r = reply.rtt;
                if (r == 0) { rttStr[ri++] = '0'; }
                else {
                    char tmp[8];
                    int rj = 0;
                    while (r > 0) { tmp[rj++] = '0' + (r % 10); r /= 10; }
                    while (rj > 0) { rttStr[ri++] = tmp[--rj]; }
                }
                rttStr[ri] = '\0';
                output_string(rttStr);
                output_string(" ms\n");
                break;
            }
            
            // Small delay between checks
            for (volatile int d = 0; d < 10000; ++d) {}
        }
        
        if (!gotReply) {
            output_string("Request timeout for icmp_seq ");
            char seqStr[8];
            seqStr[0] = '0' + (((i+1) / 10) % 10);
            seqStr[1] = '0' + ((i+1) % 10);
            seqStr[2] = '\0';
            output_string(seqStr);
            output_string("\n");
        }
        
        // Delay between pings (approximately 1 second)
        for (volatile int d = 0; d < 1000000; ++d) {}
    }
    
    icmp::end_ping_session();
    
    // Print statistics
    output_string("\n--- ");
    output_string(target);
    output_string(" ping statistics ---\n");
    
    char statStr[64];
    int si = 0;
    // sent
    if (sent == 0) { statStr[si++] = '0'; }
    else {
        char tmp[8];
        int tj = 0;
        uint16_t n = sent;
        while (n > 0) { tmp[tj++] = '0' + (n % 10); n /= 10; }
        while (tj > 0) { statStr[si++] = tmp[--tj]; }
    }
    statStr[si++] = ' '; statStr[si++] = 'p'; statStr[si++] = 'a';
    statStr[si++] = 'c'; statStr[si++] = 'k'; statStr[si++] = 'e';
    statStr[si++] = 't'; statStr[si++] = 's'; statStr[si++] = ' ';
    statStr[si++] = 't'; statStr[si++] = 'x'; statStr[si++] = ',';
    statStr[si++] = ' ';
    // received
    if (received == 0) { statStr[si++] = '0'; }
    else {
        char tmp[8];
        int tj = 0;
        uint16_t n = received;
        while (n > 0) { tmp[tj++] = '0' + (n % 10); n /= 10; }
        while (tj > 0) { statStr[si++] = tmp[--tj]; }
    }
    statStr[si++] = ' '; statStr[si++] = 'r'; statStr[si++] = 'x';
    statStr[si++] = ','; statStr[si++] = ' ';
    // loss
    uint16_t loss = (sent > 0) ? ((sent - received) * 100 / sent) : 0;
    if (loss == 0) { statStr[si++] = '0'; }
    else {
        char tmp[8];
        int tj = 0;
        while (loss > 0) { tmp[tj++] = '0' + (loss % 10); loss /= 10; }
        while (tj > 0) { statStr[si++] = tmp[--tj]; }
    }
    statStr[si++] = '%'; statStr[si++] = ' ';
    statStr[si++] = 'l'; statStr[si++] = 'o'; statStr[si++] = 's';
    statStr[si++] = 's'; statStr[si++] = '\n';
    statStr[si] = '\0';
    output_string(statStr);
}

static void cmd_netstat() {
    output_string("Active Internet connections\n");
    output_string("Proto Recv-Q Send-Q Local Address           Foreign Address         State\n");
    // In a real implementation, this would show actual connections
    output_string("(no active connections)\n");
}

static void cmd_route() {
    output_string("Kernel IP routing table\n");
    output_string("Destination     Gateway         Genmask         Flags Metric Iface\n");
    
    const ipv4::NetworkConfig* cfg = ipv4::get_config();
    if (cfg && cfg->configured) {
        char ipStr[16];
        
        // Local network route
        uint32_t network = cfg->ipAddr & cfg->subnetMask;
        ipv4::ip_to_string(network, ipStr);
        output_string(ipStr);
        output_string("     0.0.0.0         ");
        ipv4::ip_to_string(cfg->subnetMask, ipStr);
        output_string(ipStr);
        output_string("     U     1      eth0\n");
        
        // Default gateway
        output_string("0.0.0.0         ");
        ipv4::ip_to_string(cfg->gateway, ipStr);
        output_string(ipStr);
        output_string("     0.0.0.0         UG    100    eth0\n");
    } else {
        output_string("(network not configured)\n");
    }
}

// Simple number to string helper for UDP stats
static void uint_to_str(uint32_t n, char* buf) {
    int i = 0;
    if (n == 0) {
        buf[i++] = '0';
    } else {
        char tmp[12];
        int j = 0;
        while (n > 0) {
            tmp[j++] = '0' + (n % 10);
            n /= 10;
        }
        while (j > 0) {
            buf[i++] = tmp[--j];
        }
    }
    buf[i] = '\0';
}

static void cmd_nc(const char* args[], uint32_t argCount) {
    // Usage: nc [-u] <ip> <port> [message]
    // -u for UDP (default)
    
    if (argCount < 3) {
        output_string("Usage: nc <ip> <port> [message]\n");
        output_string("Send a UDP datagram to the specified host and port.\n");
        output_string("Example: nc 10.0.2.2 1234 Hello\n");
        return;
    }
    
    if (!nic::is_active()) {
        output_string("nc: network interface not active\n");
        return;
    }
    
    if (!ipv4::is_configured()) {
        output_string("nc: network not configured\n");
        return;
    }
    
    // Parse IP
    uint32_t dstIP;
    if (!ipv4::ip_from_string(args[1], &dstIP)) {
        output_string("nc: invalid IP address '");
        output_string(args[1]);
        output_string("'\n");
        return;
    }
    
    // Parse port
    uint16_t dstPort = 0;
    const char* portStr = args[2];
    while (*portStr >= '0' && *portStr <= '9') {
        dstPort = dstPort * 10 + (*portStr - '0');
        portStr++;
    }
    
    if (dstPort == 0) {
        output_string("nc: invalid port '");
        output_string(args[2]);
        output_string("'\n");
        return;
    }
    
    // Build message from remaining args
    char message[256];
    uint16_t msgLen = 0;
    
    if (argCount > 3) {
        for (uint32_t i = 3; i < argCount && msgLen < 250; i++) {
            if (i > 3 && msgLen < 250) message[msgLen++] = ' ';
            const char* arg = args[i];
            while (*arg && msgLen < 250) {
                message[msgLen++] = *arg++;
            }
        }
    } else {
        // Default test message
        const char* defaultMsg = "Hello from guideXOS";
        while (*defaultMsg && msgLen < 250) {
            message[msgLen++] = *defaultMsg++;
        }
    }
    message[msgLen] = '\0';
    
    // Send UDP datagram
    uint16_t srcPort;
    udp::Status status = udp::send_auto(dstIP, dstPort,
                                         reinterpret_cast<const uint8_t*>(message),
                                         msgLen, &srcPort);
    
    if (status == udp::UDP_OK) {
        output_string("Sent ");
        char numStr[12];
        uint_to_str(msgLen, numStr);
        output_string(numStr);
        output_string(" bytes to ");
        char ipStr[16];
        ipv4::ip_to_string(dstIP, ipStr);
        output_string(ipStr);
        output_string(":");
        uint_to_str(dstPort, numStr);
        output_string(numStr);
        output_string(" from port ");
        uint_to_str(srcPort, numStr);
        output_string(numStr);
        output_string("\n");
    } else {
        output_string("nc: send failed (error ");
        char numStr[12];
        uint_to_str(static_cast<uint32_t>(status), numStr);
        output_string(numStr);
        output_string(")\n");
    }
}

static void cmd_udpstat() {
    output_string("UDP Statistics:\n");
    
    const udp::Statistics* stats = udp::get_stats();
    char numStr[12];
    
    output_string("  Datagrams received: ");
    uint_to_str(stats->rxDatagrams, numStr);
    output_string(numStr);
    output_string("\n");
    
    output_string("  Datagrams sent:     ");
    uint_to_str(stats->txDatagrams, numStr);
    output_string(numStr);
    output_string("\n");
    
    output_string("  Bytes received:     ");
    uint_to_str(stats->rxBytes, numStr);
    output_string(numStr);
    output_string("\n");
    
    output_string("  Bytes sent:         ");
    uint_to_str(stats->txBytes, numStr);
    output_string(numStr);
    output_string("\n");
    
    output_string("  RX errors:          ");
    uint_to_str(stats->rxErrors, numStr);
    output_string(numStr);
    output_string("\n");
    
    output_string("  TX errors:          ");
    uint_to_str(stats->txErrors, numStr);
    output_string(numStr);
    output_string("\n");
    
    output_string("  Checksum errors:    ");
    uint_to_str(stats->checksumErrors, numStr);
    output_string(numStr);
    output_string("\n");
    
    output_string("  No port errors:     ");
    uint_to_str(stats->noPortErrors, numStr);
    output_string(numStr);
    output_string("\n");
}

// ============================================================
// ipconfig Command (Windows-style network configuration)
// ============================================================

static void cmd_ipconfig(const char* args[], uint32_t argCount) {
    // Check for flags
    bool showAll = false;
    bool flushDns = false;
    bool release = false;
    bool renew = false;
    
    for (uint32_t i = 1; i < argCount; ++i) {
        if (str_eq(args[i], "/all") || str_eq(args[i], "-all") ||
            str_eq(args[i], "/a") || str_eq(args[i], "-a")) {
            showAll = true;
        } else if (str_eq(args[i], "/flushdns") || str_eq(args[i], "-flushdns")) {
            flushDns = true;
        } else if (str_eq(args[i], "/release") || str_eq(args[i], "-release")) {
            release = true;
        } else if (str_eq(args[i], "/renew") || str_eq(args[i], "-renew")) {
            renew = true;
        } else if (str_eq(args[i], "/?") || str_eq(args[i], "-h") || str_eq(args[i], "--help")) {
            output_string("Usage: ipconfig [/all] [/flushdns] [/release] [/renew]\n");
            output_string("\n");
            output_string("Options:\n");
            output_string("  /all       Display full configuration\n");
            output_string("  /flushdns  Flush the DNS resolver cache\n");
            output_string("  /release   Release the current IP address\n");
            output_string("  /renew     Renew the IP address (DHCP)\n");
            return;
        }
    }
    
    // Handle special operations
    if (flushDns) {
        dns::cache_flush();
        output_string("\nguideXOS IP Configuration\n");
        output_string("\nSuccessfully flushed the DNS Resolver Cache.\n");
        return;
    }
    
    if (release) {
        output_string("\nguideXOS IP Configuration\n\n");
        
        if (!nic::is_active()) {
            output_string("No adapter is in a state permissible for this operation.\n");
            return;
        }
        
        dhcp::Status st = dhcp::dhcp_release();
        if (st == dhcp::DHCP_OK) {
            output_string("Successfully released IP address for adapter ");
            const nic::NICDevice* dev = nic::get_device();
            output_string(dev ? dev->name : "eth0");
            output_string(".\n");
        } else if (st == dhcp::DHCP_ERR_NOT_BOUND) {
            output_string("No DHCP lease to release. Using static configuration.\n");
        } else {
            output_string("An error occurred while releasing the interface.\n");
        }
        return;
    }
    
    if (renew) {
        output_string("\nguideXOS IP Configuration\n\n");
        
        if (!nic::is_active()) {
            output_string("No adapter is in a state permissible for this operation.\n");
            return;
        }
        
        output_string("Renewing IP address for adapter ");
        const nic::NICDevice* dev = nic::get_device();
        output_string(dev ? dev->name : "eth0");
        output_string("...\n");
        
        dhcp::Status st = dhcp::discover();
        if (st == dhcp::DHCP_OK) {
            const dhcp::LeaseInfo* lease = dhcp::get_lease();
            output_string("\nIP Address obtained: ");
            char ipStr[16];
            ipv4::ip_to_string(lease->assignedIP, ipStr);
            output_string(ipStr);
            output_string("\n");
        } else {
            output_string("\nFailed to renew IP address.\n");
            switch (st) {
                case dhcp::DHCP_ERR_NO_NIC:
                    output_string("Error: No network interface available.\n");
                    break;
                case dhcp::DHCP_ERR_TIMEOUT:
                    output_string("Error: DHCP request timed out.\n");
                    break;
                case dhcp::DHCP_ERR_NO_OFFER:
                    output_string("Error: No DHCP server responded.\n");
                    break;
                default:
                    output_string("Error: DHCP operation failed.\n");
                    break;
            }
        }
        return;
    }
    
    // Display configuration
    output_string("\nguideXOS IP Configuration\n");
    output_string("\n");
    
    const nic::NICDevice* dev = nic::get_device();
    const ipv4::NetworkConfig* cfg = ipv4::get_config();
    
    if (!nic::is_active()) {
        output_string("No network interfaces found.\n");
        return;
    }
    
    if (showAll) {
        // Full configuration display
        output_string("   Host Name . . . . . . . . . . . . : guideXOS\n");
        output_string("   Primary Dns Suffix  . . . . . . . : \n");
        output_string("   Node Type . . . . . . . . . . . . : Hybrid\n");
        output_string("   IP Routing Enabled. . . . . . . . : No\n");
        output_string("   WINS Proxy Enabled. . . . . . . . : No\n");
        
        // DNS cache info
        output_string("   DNS Cache Entries . . . . . . . . : ");
        char numStr[12];
        uint_to_str(dns::cache_size(), numStr);
        output_string(numStr);
        output_string("\n");
        
        output_string("\n");
    }
    
    // Ethernet adapter info
    output_string("Ethernet adapter ");
    if (dev) {
        output_string(dev->name);
    } else {
        output_string("eth0");
    }
    output_string(":\n\n");
    
    if (!cfg || !cfg->configured) {
        output_string("   Media State . . . . . . . . . . . : Media disconnected\n");
        return;
    }
    
    output_string("   Connection-specific DNS Suffix  . : \n");
    
    if (showAll) {
        output_string("   Description . . . . . . . . . . . : Intel 82540EM Gigabit Ethernet\n");
        output_string("   Physical Address. . . . . . . . . : ");
        char macStr[18];
        ethernet::mac_to_string(dev->macAddress, macStr);
        // Convert to Windows format (XX-XX-XX-XX-XX-XX)
        for (int i = 0; macStr[i]; ++i) {
            if (macStr[i] == ':') macStr[i] = '-';
            else if (macStr[i] >= 'a' && macStr[i] <= 'f') macStr[i] -= 32;  // Uppercase
        }
        output_string(macStr);
        output_string("\n");
        output_string("   DHCP Enabled. . . . . . . . . . . : No\n");
        output_string("   Autoconfiguration Enabled . . . . : Yes\n");
    }
    
    // IPv4 Address
    output_string("   IPv4 Address. . . . . . . . . . . : ");
    char ipStr[16];
    ipv4::ip_to_string(cfg->ipAddr, ipStr);
    output_string(ipStr);
    if (showAll) {
        output_string("(Preferred)");
    }
    output_string("\n");
    
    // Subnet Mask
    output_string("   Subnet Mask . . . . . . . . . . . : ");
    ipv4::ip_to_string(cfg->subnetMask, ipStr);
    output_string(ipStr);
    output_string("\n");
    
    // Default Gateway
    output_string("   Default Gateway . . . . . . . . . : ");
    if (cfg->gateway != 0) {
        ipv4::ip_to_string(cfg->gateway, ipStr);
        output_string(ipStr);
    }
    output_string("\n");
    
    if (showAll) {
        // DNS Servers
        output_string("   DNS Servers . . . . . . . . . . . : ");
        uint32_t dnsServer = dns::get_server();
        if (dnsServer != 0) {
            ipv4::ip_to_string(dnsServer, ipStr);
            output_string(ipStr);
        }
        output_string("\n");
        
        output_string("   NetBIOS over Tcpip. . . . . . . . : Disabled\n");
    }
}

// ============================================================
// nslookup Command (DNS lookup)
// ============================================================

static void cmd_nslookup(const char* domain) {
    if (!domain || domain[0] == '\0') {
        output_string("Usage: nslookup <domain>\n");
        output_string("Example: nslookup example.com\n");
        return;
    }
    
    if (!nic::is_active()) {
        output_string("nslookup: network interface not active\n");
        return;
    }
    
    if (!ipv4::is_configured()) {
        output_string("nslookup: network not configured\n");
        return;
    }
    
    output_string("Server:  ");
    char ipStr[16];
    uint32_t dnsServer = dns::get_server();
    ipv4::ip_to_string(dnsServer, ipStr);
    output_string(ipStr);
    output_string("\n");
    output_string("Address: ");
    output_string(ipStr);
    output_string("#53\n");
    output_string("\n");
    
    // Perform DNS lookup
    dns::QueryResult result;
    dns::Status status = dns::resolve_full(domain, dns::TYPE_A, &result);
    
    if (status == dns::DNS_OK && result.success) {
        output_string("Non-authoritative answer:\n");
        output_string("Name:    ");
        output_string(domain);
        output_string("\n");
        
        // Display all A records
        for (uint8_t i = 0; i < result.answerCount; ++i) {
            if (result.answers[i].type == dns::TYPE_A) {
                output_string("Address: ");
                ipv4::ip_to_string(result.answers[i].data.ipv4, ipStr);
                output_string(ipStr);
                output_string("\n");
            } else if (result.answers[i].type == dns::TYPE_CNAME) {
                output_string("Canonical name: ");
                output_string(result.answers[i].data.cname);
                output_string("\n");
            } else if (result.answers[i].type == dns::TYPE_AAAA) {
                output_string("IPv6 Address: (not displayed)\n");
            }
        }
    } else {
        output_string("** server can't find ");
        output_string(domain);
        output_string(": ");
        
        switch (status) {
            case dns::DNS_ERR_NXDOMAIN:
                output_string("NXDOMAIN\n");
                break;
            case dns::DNS_ERR_SERVFAIL:
                output_string("SERVFAIL\n");
                break;
            case dns::DNS_ERR_TIMEOUT:
                output_string("Query timed out\n");
                break;
            case dns::DNS_ERR_NETWORK:
                output_string("Network error\n");
                break;
            default:
                output_string(result.errorMsg);
                output_string("\n");
                break;
        }
    }
}

// ============================================================
// dig Command (DNS lookup - more detailed)
// ============================================================

static void cmd_dig(const char* domain) {
    if (!domain || domain[0] == '\0') {
        output_string("Usage: dig <domain>\n");
        output_string("Example: dig example.com\n");
        return;
    }
    
    if (!ipv4::is_configured()) {
        output_string("dig: network not configured\n");
        return;
    }
    
    output_string("; <<>> DiG guideXOS <<>> ");
    output_string(domain);
    output_string("\n");
    output_string(";; global options: +cmd\n");
    output_string(";; Got answer:\n");
    
    // Perform DNS lookup
    dns::QueryResult result;
    dns::Status status = dns::resolve_full(domain, dns::TYPE_A, &result);
    
    output_string(";; ->>HEADER<<- opcode: QUERY, status: ");
    output_string(dns::rcode_to_string(result.rcode));
    output_string(", id: 1\n");
    
    char numStr[12];
    output_string(";; ANSWER SECTION:\n");
    
    if (status == dns::DNS_OK && result.success) {
        for (uint8_t i = 0; i < result.answerCount; ++i) {
            output_string(result.answers[i].name);
            output_string(".\t");
            uint_to_str(result.answers[i].ttl, numStr);
            output_string(numStr);
            output_string("\tIN\t");
            output_string(dns::type_to_string(static_cast<dns::RecordType>(result.answers[i].type)));
            output_string("\t");
            
            if (result.answers[i].type == dns::TYPE_A) {
                char ipStr[16];
                ipv4::ip_to_string(result.answers[i].data.ipv4, ipStr);
                output_string(ipStr);
            } else if (result.answers[i].type == dns::TYPE_CNAME) {
                output_string(result.answers[i].data.cname);
            }
            output_string("\n");
        }
    }
    
    output_string("\n;; Query time: N/A\n");
    output_string(";; SERVER: ");
    char ipStr[16];
    ipv4::ip_to_string(dns::get_server(), ipStr);
    output_string(ipStr);
    output_string("#53\n");
}

// ============================================================
// host Command (DNS lookup - simple)
// ============================================================

static void cmd_host(const char* domain) {
    if (!domain || domain[0] == '\0') {
        output_string("Usage: host <domain>\n");
        return;
    }
    
    uint32_t ip;
    dns::Status status = dns::resolve(domain, &ip);
    
    if (status == dns::DNS_OK) {
        output_string(domain);
        output_string(" has address ");
        char ipStr[16];
        ipv4::ip_to_string(ip, ipStr);
        output_string(ipStr);
        output_string("\n");
    } else {
        output_string("Host ");
        output_string(domain);
        output_string(" not found\n");
    }
}

// ============================================================
// dhcp Command (DHCP client operations)
// ============================================================

static void cmd_dhcp(const char* args[], uint32_t argCount) {
    // Check for flags
    bool showStatus = (argCount <= 1);
    bool doDiscover = false;
    bool doRelease = false;
    bool doRenew = false;
    bool showStats = false;
    
    for (uint32_t i = 1; i < argCount; ++i) {
        if (str_eq(args[i], "/discover") || str_eq(args[i], "-discover") ||
            str_eq(args[i], "/d") || str_eq(args[i], "-d")) {
            doDiscover = true;
        } else if (str_eq(args[i], "/release") || str_eq(args[i], "-release") ||
                   str_eq(args[i], "/r") || str_eq(args[i], "-r")) {
            doRelease = true;
        } else if (str_eq(args[i], "/renew") || str_eq(args[i], "-renew") ||
                   str_eq(args[i], "/n") || str_eq(args[i], "-n")) {
            doRenew = true;
        } else if (str_eq(args[i], "/stats") || str_eq(args[i], "-stats") ||
                   str_eq(args[i], "/s") || str_eq(args[i], "-s")) {
            showStats = true;
        } else if (str_eq(args[i], "/?") || str_eq(args[i], "-h") || str_eq(args[i], "--help")) {
            output_string("Usage: dhcp [/discover] [/release] [/renew] [/stats]\n");
            output_string("\n");
            output_string("Options:\n");
            output_string("  (none)     Display current DHCP lease status\n");
            output_string("  /discover  Discover a DHCP server and obtain IP\n");
            output_string("  /release   Release the current DHCP lease\n");
            output_string("  /renew     Renew the current DHCP lease\n");
            output_string("  /stats     Display DHCP client statistics\n");
            return;
        }
    }
    
    if (!nic::is_active()) {
        output_string("dhcp: no network interface active\n");
        return;
    }
    
    // Handle operations
    if (doRelease) {
        output_string("Releasing DHCP lease...\n");
        dhcp::Status st = dhcp::dhcp_release();
        if (st == dhcp::DHCP_OK) {
            output_string("DHCP lease released successfully.\n");
        } else if (st == dhcp::DHCP_ERR_NOT_BOUND) {
            output_string("No active DHCP lease to release.\n");
        } else {
            output_string("Failed to release DHCP lease.\n");
        }
        return;
    }
    
    if (doRenew || doDiscover) {
        output_string("Requesting IP address from DHCP server...\n");
        dhcp::Status st = dhcp::discover();
        if (st == dhcp::DHCP_OK) {
            output_string("DHCP configuration successful.\n\n");
            // Show the obtained lease info
            const dhcp::LeaseInfo* lease = dhcp::get_lease();
            char ipStr[16];
            
            output_string("  IP Address:    ");
            ipv4::ip_to_string(lease->assignedIP, ipStr);
            output_string(ipStr);
            output_string("\n");
            
            output_string("  Subnet Mask:   ");
            ipv4::ip_to_string(lease->subnetMask, ipStr);
            output_string(ipStr);
            output_string("\n");
            
            output_string("  Gateway:       ");
            ipv4::ip_to_string(lease->gateway, ipStr);
            output_string(ipStr);
            output_string("\n");
            
            output_string("  DNS Server:    ");
            ipv4::ip_to_string(lease->dnsServer, ipStr);
            output_string(ipStr);
            output_string("\n");
            
            output_string("  DHCP Server:   ");
            ipv4::ip_to_string(lease->serverIP, ipStr);
            output_string(ipStr);
            output_string("\n");
            
            output_string("  Lease Time:    ");
            char numStr[12];
            uint_to_str(lease->leaseTime, numStr);
            output_string(numStr);
            output_string(" seconds\n");
        } else {
            output_string("DHCP configuration failed.\n");
            switch (st) {
                case dhcp::DHCP_ERR_NO_NIC:
                    output_string("Error: No network interface available.\n");
                    break;
                case dhcp::DHCP_ERR_TIMEOUT:
                    output_string("Error: DHCP request timed out.\n");
                    break;
                case dhcp::DHCP_ERR_NO_OFFER:
                    output_string("Error: No DHCP server responded.\n");
                    break;
                case dhcp::DHCP_ERR_NO_ACK:
                    output_string("Error: DHCP server did not acknowledge.\n");
                    break;
                case dhcp::DHCP_ERR_NAK:
                    output_string("Error: DHCP server refused the request.\n");
                    break;
                default:
                    output_string("Error: Unknown DHCP error.\n");
                    break;
            }
        }
        return;
    }
    
    if (showStats) {
        output_string("DHCP Client Statistics:\n\n");
        const dhcp::Statistics* stats = dhcp::get_stats();
        char numStr[12];
        
        output_string("  Discovers sent:   ");
        uint_to_str(stats->discoversSent, numStr);
        output_string(numStr);
        output_string("\n");
        
        output_string("  Offers received:  ");
        uint_to_str(stats->offersReceived, numStr);
        output_string(numStr);
        output_string("\n");
        
        output_string("  Requests sent:    ");
        uint_to_str(stats->requestsSent, numStr);
        output_string(numStr);
        output_string("\n");
        
        output_string("  ACKs received:    ");
        uint_to_str(stats->acksReceived, numStr);
        output_string(numStr);
        output_string("\n");
        
        output_string("  NAKs received:    ");
        uint_to_str(stats->naksReceived, numStr);
        output_string(numStr);
        output_string("\n");
        
        output_string("  Releases sent:    ");
        uint_to_str(stats->releasesSent, numStr);
        output_string(numStr);
        output_string("\n");
        
        output_string("  Timeouts:         ");
        uint_to_str(stats->timeouts, numStr);
        output_string(numStr);
        output_string("\n");
        
        output_string("  Renewals:         ");
        uint_to_str(stats->renewals, numStr);
        output_string(numStr);
        output_string("\n");
        
        output_string("  Errors:           ");
        uint_to_str(stats->errors, numStr);
        output_string(numStr);
        output_string("\n");
        return;
    }
    
    // Default: show status
    output_string("DHCP Client Status:\n\n");
    
    output_string("  State: ");
    output_string(dhcp::state_to_string(dhcp::get_state()));
    output_string("\n");
    
    const dhcp::LeaseInfo* lease = dhcp::get_lease();
    if (lease->valid) {
        char ipStr[16];
        char numStr[12];
        
        output_string("\n  Active Lease:\n");
        
        output_string("    IP Address:    ");
        ipv4::ip_to_string(lease->assignedIP, ipStr);
        output_string(ipStr);
        output_string("\n");
        
        output_string("    Subnet Mask:   ");
        ipv4::ip_to_string(lease->subnetMask, ipStr);
        output_string(ipStr);
        output_string("\n");
        
        output_string("    Gateway:       ");
        ipv4::ip_to_string(lease->gateway, ipStr);
        output_string(ipStr);
        output_string("\n");
        
        output_string("    DNS Server:    ");
        ipv4::ip_to_string(lease->dnsServer, ipStr);
        output_string(ipStr);
        output_string("\n");
        
        output_string("    DHCP Server:   ");
        ipv4::ip_to_string(lease->serverIP, ipStr);
        output_string(ipStr);
        output_string("\n");
        
        output_string("    Lease Time:    ");
        uint_to_str(lease->leaseTime, numStr);
        output_string(numStr);
        output_string(" seconds\n");
        
        output_string("    Renewal (T1):  ");
        uint_to_str(lease->renewalTime, numStr);
        output_string(numStr);
        output_string(" seconds\n");
        
        output_string("    Rebind (T2):   ");
        uint_to_str(lease->rebindingTime, numStr);
        output_string(numStr);
        output_string(" seconds\n");
    } else {
        output_string("\n  No active DHCP lease.\n");
        output_string("  Use 'dhcp /discover' to obtain an IP address.\n");
    }
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
    }
    // New commands ported from guideXOS.Legacy
    else if (str_eq(command, "neofetch") || str_eq(command, "screenfetch") || str_eq(command, "sysinfo")) {
        cmd_neofetch();
    } else if (str_eq(command, "version") || str_eq(command, "ver")) {
        cmd_version();
    } else if (str_eq(command, "about")) {
        cmd_about();
    } else if (str_eq(command, "apps") || str_eq(command, "programs")) {
        cmd_apps();
    } else if (str_eq(command, "workspaces") || str_eq(command, "desktops")) {
        cmd_workspaces();
    } else if (str_eq(command, "colors") || str_eq(command, "colortest")) {
        cmd_colors();
    } else if (str_eq(command, "fortune") || str_eq(command, "tip")) {
        cmd_fortune();
    } else if (str_eq(command, "motd") || str_eq(command, "welcome")) {
        cmd_motd();
    } else if (str_eq(command, "cal") || str_eq(command, "calendar")) {
        cmd_cal();
    } else if (str_eq(command, "df")) {
        cmd_df();
    } else if (str_eq(command, "id")) {
        cmd_id();
    } else if (str_eq(command, "w") || str_eq(command, "who")) {
        cmd_w();
    } else if (str_eq(command, "head")) {
        cmd_head(arg1);
    } else if (str_eq(command, "tail")) {
        cmd_tail(arg1);
    } else if (str_eq(command, "wc")) {
        cmd_wc(arg1);
    } else if (str_eq(command, "grep")) {
        cmd_grep(arg1);
    } else if (str_eq(command, "find")) {
        cmd_find(arg1);
    } else if (str_eq(command, "which") || str_eq(command, "whereis")) {
        cmd_which(arg1);
    } else if (str_eq(command, "alias")) {
        output_string("alias ls='ls --color=auto'\n");
        output_string("alias ll='ls -la'\n");
        output_string("alias cls='clear'\n");
    } else if (str_eq(command, "man")) {
        if (arg1[0] == '\0') {
            output_string("Usage: man <command>\n");
            output_string("Available: help, ls, cd, cat, echo, clear\n");
        } else {
            output_string("No manual entry for ");
            output_string(arg1);
            output_string("\nTry 'help' for available commands.\n");
        }
    } else if (str_eq(command, "true")) {
        // Do nothing, return success (implicit)
    } else if (str_eq(command, "false")) {
        output_string(""); // Returns failure conceptually
    } else if (str_eq(command, "yes")) {
        output_string("y\ny\ny\n(use Ctrl+C to stop in a real terminal)\n");
    } else if (str_eq(command, "banner")) {
        if (arg1[0] == '\0') {
            output_string("Usage: banner <text>\n");
        } else {
            output_string("#####################\n");
            output_string("#                   #\n");
            output_string("#  ");
            output_string(arg1);
            output_string("\n");
            output_string("#                   #\n");
            output_string("#####################\n");
        }
    } else if (str_eq(command, "notepad")) {
        // Launch Notepad with optional file parameter
        if (arg1[0] != '\0') {
            output_string("Launching Notepad with file: ");
            output_string(arg1);
            output_string("\n");
            ::kernel::app::AppManager::launchAppWithParam("Notepad", arg1);
        } else {
            output_string("Launching Notepad...\n");
            ::kernel::desktop::launch_app("Notepad");
        }
    } else if (str_eq(command, "osk") || str_eq(command, "onscreen-keyboard")) {
        cmd_osk();
    } else if (str_eq(command, "dmesg")) {
        cmd_dmesg();
    } else if (str_eq(command, "lsblk")) {
        cmd_lsblk();
    } else if (str_eq(command, "lspci")) {
        cmd_lspci();
    } else if (str_eq(command, "lsusb")) {
        cmd_lsusb();
    }
    // Filesystem test commands (Phase 7.5)
    else if (str_eq(command, "vfsmount")) {
        cmd_vfsmount(arg1, argCount > 2 ? args[2] : "0");
    } else if (str_eq(command, "vfsls")) {
        cmd_vfsls(arg1);
    } else if (str_eq(command, "vfscat")) {
        cmd_vfscat(arg1);
    } else if (str_eq(command, "vfsstat")) {
        cmd_vfsstat(arg1);
    } else if (str_eq(command, "vfstest")) {
        cmd_vfstest();
    } else if (str_eq(command, "vfsinfo")) {
        cmd_vfsinfo();
    }
    // Network commands
    else if (str_eq(command, "ping")) {
        cmd_ping(arg1);
    } else if (str_eq(command, "ifconfig") || str_eq(command, "ip")) {
        cmd_ifconfig();
    } else if (str_eq(command, "netstat") || str_eq(command, "ss")) {
        cmd_netstat();
    } else if (str_eq(command, "route")) {
        cmd_route();
    } else if (str_eq(command, "nc") || str_eq(command, "netcat")) {
        // Pass all args to nc command
        const char* ncArgs[MAX_ARGS];
        for (uint32_t i = 0; i < argCount; i++) {
            ncArgs[i] = args[i];
        }
        cmd_nc(ncArgs, argCount);
    } else if (str_eq(command, "udpstat")) {
        cmd_udpstat();
    } else if (str_eq(command, "ipconfig")) {
        // Pass all args to ipconfig command
        const char* ipconfigArgs[MAX_ARGS];
        for (uint32_t i = 0; i < argCount; i++) {
            ipconfigArgs[i] = args[i];
        }
        cmd_ipconfig(ipconfigArgs, argCount);
    } else if (str_eq(command, "nslookup")) {
        cmd_nslookup(arg1);
    } else if (str_eq(command, "dig")) {
        cmd_dig(arg1);
    } else if (str_eq(command, "host")) {
        cmd_host(arg1);
    } else if (str_eq(command, "dhcp")) {
        // Pass all args to dhcp command
        const char* dhcpArgs[MAX_ARGS];
        for (uint32_t i = 0; i < argCount; i++) {
            dhcpArgs[i] = args[i];
        }
        cmd_dhcp(dhcpArgs, argCount);
    }
    // Kernel GUI test mode
    else if (str_eq(command, "testmode") || str_eq(command, "guitest")) {
        output_string("Running GUI test mode...\n");
        // Call the desktop test mode function (declared at file scope)
        ::desktop_run_test_mode();
    }
    else if (str_eq(command, "top") || str_eq(command, "htop")) {
        output_string("top - 00:00:00 up ");
        char buf[16];
        uint32_t hours = s_uptimeSeconds / 3600;
        uint32_t mins = (s_uptimeSeconds % 3600) / 60;
        buf[0] = '0' + (hours / 10) % 10;
        buf[1] = '0' + hours % 10;
        buf[2] = ':';
        buf[3] = '0' + (mins / 10) % 10;
        buf[4] = '0' + mins % 10;
        buf[5] = '\0';
        output_string(buf);
        output_string(",  1 user,  load: 0.00\n");
        output_string("Tasks:   4 total,   1 running,   3 sleeping\n");
        output_string("%Cpu(s):  0.0 us,  0.0 sy,  0.0 ni, 100.0 id\n");
        output_string("MiB Mem:   128.0 total,    96.0 free,    32.0 used\n");
        output_string("\n");
        output_string("  PID USER      PR  NI    VIRT    RES %CPU CMD\n");
        output_string("    1 root      20   0    4096   1024  0.0 init\n");
        output_string("    2 root      20   0    8192   2048  0.0 kernel\n");
        output_string("    3 root      20   0   16384   4096  0.0 desktop\n");
        output_string("    4 root      20   0    8192   2048  0.1 shell\n");
    } else if (str_eq(command, "kill")) {
        if (arg1[0] == '\0') {
            output_string("kill: usage: kill <pid>\n");
        } else {
            output_string("kill: cannot kill process ");
            output_string(arg1);
            output_string(": Not permitted\n");
        }
    } else if (str_eq(command, "sudo")) {
        output_string("root is not in the sudoers file. (Just kidding, you're already root!)\n");
    } else if (str_eq(command, "su")) {
        output_string("Already running as root.\n");
    } else if (str_eq(command, "chmod") || str_eq(command, "chown") || str_eq(command, "chgrp")) {
        output_string(command);
        output_string(": Permission management not implemented in this shell.\n");
    } else if (str_eq(command, "ln")) {
        output_string("ln: Symbolic links not implemented.\n");
    } else if (str_eq(command, "stat")) {
        if (arg1[0] == '\0') {
            output_string("stat: missing operand\n");
        } else {
            output_string("  File: ");
            output_string(arg1);
            output_string("\n");
            output_string("  Size: 0\t\tBlocks: 0\n");
            output_string("Access: (0755/drwxr-xr-x)  Uid: (0/root)   Gid: (0/root)\n");
        }
    } else if (str_eq(command, "file")) {
        if (arg1[0] == '\0') {
            output_string("file: missing operand\n");
        } else {
            output_string(arg1);
            output_string(": ASCII text\n");
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
