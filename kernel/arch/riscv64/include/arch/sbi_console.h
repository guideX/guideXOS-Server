// SBI (Supervisor Binary Interface) Console
//
// RISC-V SBI provides firmware-level services via the ecall
// instruction.  The legacy SBI v0.1 console_putchar (EID 0x01)
// and console_getchar (EID 0x02) extensions are used for early
// kernel output before any UART driver is initialised.
//
// On QEMU virt with OpenSBI, ecall in S-mode traps into M-mode
// firmware which performs the actual I/O.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace riscv64 {
namespace sbi_console {

// SBI extension IDs (legacy v0.1)
static const uint64_t SBI_EXT_SET_TIMER     = 0x00;
static const uint64_t SBI_EXT_CONSOLE_PUTCHAR = 0x01;
static const uint64_t SBI_EXT_CONSOLE_GETCHAR = 0x02;
static const uint64_t SBI_EXT_SHUTDOWN       = 0x08;

// SBI return structure
struct SbiRet {
    int64_t error;
    int64_t value;
};

// Issue an SBI ecall (legacy interface)
SbiRet sbi_call(uint64_t ext, uint64_t arg0 = 0);

// Initialize the SBI console (no-op for legacy, included for
// consistency with other arch console drivers)
void init();

// Write a single character to the SBI console
void putchar(char c);

// Write a null-terminated string to the SBI console
void puts(const char* str);

// Write a string with explicit length
void write(const char* str, uint64_t len);

// Print a 64-bit value in hexadecimal (for diagnostics)
void put_hex(uint64_t value);

// Read a character from the SBI console (non-blocking, returns -1 if none)
int getchar();

// Shut down the system via SBI
void shutdown();

} // namespace sbi_console
} // namespace riscv64
} // namespace arch
} // namespace kernel
