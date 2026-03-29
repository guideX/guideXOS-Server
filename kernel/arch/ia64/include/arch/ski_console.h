// ski Simulator Console Interface
//
// The HP ski IA-64 simulator provides a firmware-style console via
// SSC (Simulator System Call) instructions.  A "break.i 0x80000"
// with r15 set to the SSC function code triggers the simulator to
// perform the requested I/O operation.
//
// This module wraps SSC_PUTCHAR / SSC_GETCHAR into a minimal
// console driver for early kernel output.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <cstdint>

namespace kernel {
namespace arch {
namespace ia64 {
namespace ski_console {

// SSC function codes
static const uint64_t SSC_STOP           = 0x00;
static const uint64_t SSC_CONSOLE_INIT   = 0x20;
static const uint64_t SSC_PUTCHAR        = 0x21;
static const uint64_t SSC_GETCHAR        = 0x22;

// Issue a ski SSC (Simulator System Call)
// Places arg0 in r32, function code in r15, then executes break.i 0x80000
void ssc(uint64_t func, uint64_t arg0 = 0);

// Initialize the ski console
void init();

// Write a single character to the ski console
void putchar(char c);

// Write a null-terminated string to the ski console
void puts(const char* str);

// Write a string with explicit length
void write(const char* str, uint64_t len);

// Print a 64-bit value in hexadecimal (for diagnostics)
void put_hex(uint64_t value);

// Read a character from the ski console (non-blocking, returns -1 if none)
int getchar();

// Stop the ski simulator
void stop();

} // namespace ski_console
} // namespace ia64
} // namespace arch
} // namespace kernel
