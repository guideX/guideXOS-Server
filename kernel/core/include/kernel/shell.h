// guideXOS Kernel Shell
//
// An interactive command-line shell for the kernel environment.
// Provides POSIX-like commands with Windows compatibility aliases.
//
// Commands:
//   POSIX:   ls, ll, cd, pwd, cat, echo, cp, mv, rm, mkdir, rmdir, touch, clear, exit, help
//   Windows: dir, cls, type, copy, del, md, rd, ren
//   System:  reboot, shutdown, sleep, uptime, mem, cpuinfo, ps, kill
//   Misc:    date, time, whoami, hostname, env, history, alias
//
// Copyright (c) 2026 guideXOS Server
//

#ifndef KERNEL_SHELL_H
#define KERNEL_SHELL_H

#include "kernel/types.h"

namespace kernel {
namespace shell {

// ================================================================
// Constants
// ================================================================

static const uint32_t MAX_CMD_LENGTH = 256;
static const uint32_t MAX_HISTORY = 32;
static const uint32_t MAX_ARGS = 16;

// `nicinfo brief` is intended to fit in one ordinary physical Console
// viewport.  Keep the contract explicit so the hosted diagnostics test and
// future changes have a stable bound to check.
static const uint32_t NICINFO_BRIEF_MAX_LINES = 20;
static const uint32_t NICINFO_BRIEF_EXPECTED_LINES = 18;
static_assert(NICINFO_BRIEF_EXPECTED_LINES <= NICINFO_BRIEF_MAX_LINES,
              "nicinfo brief expected output must stay within its line bound");

enum NicInfoMode : uint8_t {
    NICINFO_MODE_FULL = 0,
    NICINFO_MODE_BRIEF,
    NICINFO_MODE_INVALID,
};

inline NicInfoMode nicinfo_mode_from_arg(const char* arg)
{
    if (!arg || *arg == '\0') return NICINFO_MODE_FULL;

    const char* expected = "brief";
    while (*arg && *expected && *arg == *expected) {
        ++arg;
        ++expected;
    }
    return (*arg == '\0' && *expected == '\0')
        ? NICINFO_MODE_BRIEF : NICINFO_MODE_INVALID;
}

// ================================================================
// Shell State
// ================================================================

enum class ShellState {
    Closed,
    Open,
    Fullscreen
};

// ================================================================
// Public API
// ================================================================

// Initialize the shell (call once at startup)
void init();

// Check if shell is open
bool is_open();

// Open the shell window
void open();

// Close the shell window
void close();

// Toggle shell visibility
void toggle();

// Toggle fullscreen mode
void toggle_fullscreen();

// Process a key press (ASCII code or special key)
// Special keys: 0x100=Up, 0x101=Down, 0x102=Left, 0x103=Right,
//               0x104=Home, 0x105=End, 0x106=Delete, 0x107=Tab
void process_key(uint32_t key);

// Process a character input
void process_char(char c);

// Get shell state
ShellState get_state();

// Draw the shell window on the framebuffer
// x, y = top-left corner; w, h = dimensions
void draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

// Execute a command string directly
void execute(const char* cmd);

// Get current working directory
const char* get_cwd();

// ================================================================
// Special Key Codes
// ================================================================

static const uint32_t KEY_UP     = 0x100;
static const uint32_t KEY_DOWN   = 0x101;
static const uint32_t KEY_LEFT   = 0x102;
static const uint32_t KEY_RIGHT  = 0x103;
static const uint32_t KEY_HOME   = 0x104;
static const uint32_t KEY_END    = 0x105;
static const uint32_t KEY_DELETE = 0x106;
static const uint32_t KEY_TAB    = 0x107;
static const uint32_t KEY_PGUP   = 0x108;
static const uint32_t KEY_PGDN   = 0x109;
static const uint32_t KEY_SUPER  = 0x11C;

} // namespace shell
} // namespace kernel

#endif // KERNEL_SHELL_H
