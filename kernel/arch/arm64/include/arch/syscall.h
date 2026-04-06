// ARM64 System Call Interface
//
// ARM64 uses the SVC (Supervisor Call) instruction for syscalls.
// Parameters are passed in X0-X7, syscall number in X8.
// Return value in X0, secondary return in X1 if needed.
//
// Exception class EC_SVC_AARCH64 (0x15) identifies syscalls.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>
#include "context_switch.h"

namespace kernel {
namespace arch {
namespace arm64 {
namespace syscall {

// ================================================================
// System Call Numbers
// ================================================================

// Process/Thread management
static const uint64_t SYS_EXIT          = 0;
static const uint64_t SYS_FORK          = 1;
static const uint64_t SYS_READ          = 2;
static const uint64_t SYS_WRITE         = 3;
static const uint64_t SYS_OPEN          = 4;
static const uint64_t SYS_CLOSE         = 5;
static const uint64_t SYS_WAIT          = 6;
static const uint64_t SYS_EXEC          = 7;
static const uint64_t SYS_GETPID        = 8;
static const uint64_t SYS_YIELD         = 9;

// Memory management
static const uint64_t SYS_MMAP          = 10;
static const uint64_t SYS_MUNMAP        = 11;
static const uint64_t SYS_MPROTECT      = 12;
static const uint64_t SYS_BRK           = 13;

// File system
static const uint64_t SYS_LSEEK         = 14;
static const uint64_t SYS_STAT          = 15;
static const uint64_t SYS_FSTAT         = 16;
static const uint64_t SYS_MKDIR         = 17;
static const uint64_t SYS_RMDIR         = 18;
static const uint64_t SYS_UNLINK        = 19;
static const uint64_t SYS_RENAME        = 20;
static const uint64_t SYS_GETCWD        = 21;
static const uint64_t SYS_CHDIR         = 22;

// Time
static const uint64_t SYS_GETTIME       = 30;
static const uint64_t SYS_SLEEP         = 31;
static const uint64_t SYS_NANOSLEEP     = 32;

// IPC
static const uint64_t SYS_PIPE          = 40;
static const uint64_t SYS_DUP           = 41;
static const uint64_t SYS_DUP2          = 42;

// Signals
static const uint64_t SYS_KILL          = 50;
static const uint64_t SYS_SIGACTION     = 51;
static const uint64_t SYS_SIGRETURN     = 52;

// Networking
static const uint64_t SYS_SOCKET        = 60;
static const uint64_t SYS_BIND          = 61;
static const uint64_t SYS_LISTEN        = 62;
static const uint64_t SYS_ACCEPT        = 63;
static const uint64_t SYS_CONNECT       = 64;
static const uint64_t SYS_SEND          = 65;
static const uint64_t SYS_RECV          = 66;
static const uint64_t SYS_SENDTO        = 67;
static const uint64_t SYS_RECVFROM      = 68;

// guideXOS specific
static const uint64_t SYS_DEBUG_PRINT   = 100;
static const uint64_t SYS_SYSINFO       = 101;

// Maximum syscall number
static const uint64_t SYS_MAX           = 127;

// ================================================================
// Error codes
// ================================================================

static const int64_t E_OK       = 0;
static const int64_t E_PERM     = -1;   // Operation not permitted
static const int64_t E_NOENT    = -2;   // No such file or directory
static const int64_t E_SRCH     = -3;   // No such process
static const int64_t E_INTR     = -4;   // Interrupted syscall
static const int64_t E_IO       = -5;   // I/O error
static const int64_t E_NXIO     = -6;   // No such device
static const int64_t E_2BIG     = -7;   // Argument list too long
static const int64_t E_NOEXEC   = -8;   // Exec format error
static const int64_t E_BADF     = -9;   // Bad file descriptor
static const int64_t E_CHILD    = -10;  // No child processes
static const int64_t E_AGAIN    = -11;  // Resource temporarily unavailable
static const int64_t E_NOMEM    = -12;  // Out of memory
static const int64_t E_ACCES    = -13;  // Permission denied
static const int64_t E_FAULT    = -14;  // Bad address
static const int64_t E_BUSY     = -16;  // Device busy
static const int64_t E_EXIST    = -17;  // File exists
static const int64_t E_NODEV    = -19;  // No such device
static const int64_t E_NOTDIR   = -20;  // Not a directory
static const int64_t E_ISDIR    = -21;  // Is a directory
static const int64_t E_INVAL    = -22;  // Invalid argument
static const int64_t E_NFILE    = -23;  // File table overflow
static const int64_t E_MFILE    = -24;  // Too many open files
static const int64_t E_NOTTY    = -25;  // Not a terminal
static const int64_t E_FBIG     = -27;  // File too large
static const int64_t E_NOSPC    = -28;  // No space left
static const int64_t E_SPIPE    = -29;  // Illegal seek
static const int64_t E_ROFS     = -30;  // Read-only filesystem
static const int64_t E_PIPE     = -32;  // Broken pipe
static const int64_t E_NOSYS    = -38;  // Function not implemented
static const int64_t E_NOTEMPTY = -39;  // Directory not empty

// ================================================================
// Syscall context (passed to handlers)
// ================================================================

struct SyscallContext {
    // Arguments (from registers X0-X7)
    uint64_t arg0;  // X0
    uint64_t arg1;  // X1
    uint64_t arg2;  // X2
    uint64_t arg3;  // X3
    uint64_t arg4;  // X4
    uint64_t arg5;  // X5
    uint64_t arg6;  // X6
    uint64_t arg7;  // X7
    
    // Syscall number (from X8)
    uint64_t number;
    
    // Return value (written to X0)
    int64_t result;
    
    // Full context reference (for advanced handlers)
    context::FullContext* fullCtx;
};

// ================================================================
// Syscall handler type
// ================================================================

typedef int64_t (*SyscallHandler)(SyscallContext* ctx);

// ================================================================
// Syscall registration and dispatch
// ================================================================

// Initialize the syscall subsystem
void init();

// Register a syscall handler
void register_handler(uint64_t number, SyscallHandler handler);

// Dispatch a syscall (called from exception handler)
// Returns the result to place in X0
int64_t dispatch(context::FullContext* ctx);

// ================================================================
// Default syscall implementations (stubs)
// ================================================================

int64_t sys_exit(SyscallContext* ctx);
int64_t sys_read(SyscallContext* ctx);
int64_t sys_write(SyscallContext* ctx);
int64_t sys_open(SyscallContext* ctx);
int64_t sys_close(SyscallContext* ctx);
int64_t sys_getpid(SyscallContext* ctx);
int64_t sys_yield(SyscallContext* ctx);
int64_t sys_mmap(SyscallContext* ctx);
int64_t sys_munmap(SyscallContext* ctx);
int64_t sys_gettime(SyscallContext* ctx);
int64_t sys_sleep(SyscallContext* ctx);
int64_t sys_debug_print(SyscallContext* ctx);
int64_t sys_sysinfo(SyscallContext* ctx);

} // namespace syscall
} // namespace arm64
} // namespace arch
} // namespace kernel
