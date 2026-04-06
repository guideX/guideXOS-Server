//
// PowerPC64 System Call Interface
//
// Defines syscall numbers, the syscall handler interface, and
// trap dispatch for the PowerPC64 architecture.
//
// PowerPC64 syscall mechanism (sc instruction):
//   - User code executes sc instruction
//   - CPU traps to supervisor mode, jumps to vector 0xC00
//   - Syscall number is in r0
//   - Arguments are in r3-r8
//   - Return value goes in r3
//   - SRR0 holds return address
//   - SRR1 holds saved MSR
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace ppc64 {
namespace syscall {

// ================================================================
// System Call Numbers (guideXOS-specific)
// ================================================================

static const uint64_t SYS_EXIT          = 0x00;
static const uint64_t SYS_FORK          = 0x01;
static const uint64_t SYS_EXEC          = 0x02;
static const uint64_t SYS_WAIT          = 0x03;
static const uint64_t SYS_GETPID        = 0x04;
static const uint64_t SYS_GETPPID       = 0x05;
static const uint64_t SYS_GETTID        = 0x06;
static const uint64_t SYS_KILL          = 0x07;
static const uint64_t SYS_YIELD         = 0x08;
static const uint64_t SYS_CLONE         = 0x09;

static const uint64_t SYS_BRK           = 0x10;
static const uint64_t SYS_MMAP          = 0x11;
static const uint64_t SYS_MUNMAP        = 0x12;
static const uint64_t SYS_MPROTECT      = 0x13;

static const uint64_t SYS_OPEN          = 0x20;
static const uint64_t SYS_CLOSE         = 0x21;
static const uint64_t SYS_READ          = 0x22;
static const uint64_t SYS_WRITE         = 0x23;
static const uint64_t SYS_LSEEK         = 0x24;
static const uint64_t SYS_FSTAT         = 0x25;
static const uint64_t SYS_STAT          = 0x26;
static const uint64_t SYS_MKDIR         = 0x27;
static const uint64_t SYS_RMDIR         = 0x28;
static const uint64_t SYS_UNLINK        = 0x29;
static const uint64_t SYS_RENAME        = 0x2A;
static const uint64_t SYS_READDIR       = 0x2B;
static const uint64_t SYS_DUP           = 0x2C;
static const uint64_t SYS_DUP2          = 0x2D;
static const uint64_t SYS_IOCTL         = 0x2E;

static const uint64_t SYS_TIME          = 0x30;
static const uint64_t SYS_SLEEP         = 0x31;
static const uint64_t SYS_CLOCK_GETTIME = 0x32;
static const uint64_t SYS_NANOSLEEP     = 0x33;

static const uint64_t SYS_SOCKET        = 0x40;
static const uint64_t SYS_BIND          = 0x41;
static const uint64_t SYS_LISTEN        = 0x42;
static const uint64_t SYS_ACCEPT        = 0x43;
static const uint64_t SYS_CONNECT       = 0x44;
static const uint64_t SYS_SEND          = 0x45;
static const uint64_t SYS_RECV          = 0x46;

static const uint64_t SYS_DEBUG_PRINT   = 0xF0;
static const uint64_t SYS_DEBUG_HALT    = 0xFF;

// ================================================================
// Maximum syscall number
// ================================================================

static const uint64_t SYS_MAX = 0x100;

// ================================================================
// Syscall handler function type
// ================================================================

typedef int64_t (*syscall_handler_t)(uint64_t a0, uint64_t a1, uint64_t a2,
                                      uint64_t a3, uint64_t a4, uint64_t a5);

// ================================================================
// Syscall subsystem API
// ================================================================

void init();
void register_handler(uint64_t num, syscall_handler_t handler);
int64_t dispatch(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2,
                 uint64_t a3, uint64_t a4, uint64_t a5);

} // namespace syscall
} // namespace ppc64
} // namespace arch
} // namespace kernel
