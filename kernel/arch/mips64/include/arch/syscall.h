//
// MIPS64 System Call Interface
//
// Defines syscall numbers, the syscall handler interface, and
// trap dispatch for the MIPS64 architecture.
//
// MIPS64 syscall mechanism (syscall instruction):
//   - User code executes syscall instruction
//   - CPU traps to kernel mode, jumps to general exception vector
//   - Exception code in Cause register = 8 (Sys)
//   - Syscall number is in $v0 ($2)
//   - Arguments are in $a0-$a7 ($4-$11)
//   - Return value goes in $v0 ($2)
//   - Error flag in $a3 ($7) - 0 = success, 1 = error
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace mips64 {
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

static const uint64_t SYS_PIPE          = 0x30;
static const uint64_t SYS_MSGGET        = 0x31;
static const uint64_t SYS_MSGSND        = 0x32;
static const uint64_t SYS_MSGRCV        = 0x33;
static const uint64_t SYS_SEMGET        = 0x34;
static const uint64_t SYS_SEMOP         = 0x35;
static const uint64_t SYS_SHMGET        = 0x36;
static const uint64_t SYS_SHMAT         = 0x37;
static const uint64_t SYS_SHMDT         = 0x38;
static const uint64_t SYS_IPC_SEND      = 0x39;
static const uint64_t SYS_IPC_RECV      = 0x3A;

static const uint64_t SYS_GETTIME       = 0x40;
static const uint64_t SYS_SETTIME       = 0x41;
static const uint64_t SYS_NANOSLEEP     = 0x42;
static const uint64_t SYS_GETPRIORITY   = 0x43;
static const uint64_t SYS_SETPRIORITY   = 0x44;
static const uint64_t SYS_SCHED_YIELD   = 0x45;
static const uint64_t SYS_ALARM         = 0x46;

static const uint64_t SYS_DEVOPEN       = 0x50;
static const uint64_t SYS_DEVCLOSE      = 0x51;
static const uint64_t SYS_DEVREAD       = 0x52;
static const uint64_t SYS_DEVWRITE      = 0x53;
static const uint64_t SYS_DEVIOCTL      = 0x54;

static const uint64_t SYS_SOCKET        = 0x60;
static const uint64_t SYS_BIND          = 0x61;
static const uint64_t SYS_LISTEN        = 0x62;
static const uint64_t SYS_ACCEPT        = 0x63;
static const uint64_t SYS_CONNECT       = 0x64;
static const uint64_t SYS_SEND          = 0x65;
static const uint64_t SYS_RECV          = 0x66;
static const uint64_t SYS_SENDTO        = 0x67;
static const uint64_t SYS_RECVFROM      = 0x68;

static const uint64_t SYS_GXOS_DEBUG    = 0x80;
static const uint64_t SYS_GXOS_INFO     = 0x81;
static const uint64_t SYS_GXOS_GRAPHICS = 0x82;

static const uint64_t SYS_MAX           = 0xFF;

// ================================================================
// Syscall return values
// ================================================================

static const int64_t SYSCALL_SUCCESS    = 0;
static const int64_t SYSCALL_ENOSYS     = -38;   // Function not implemented
static const int64_t SYSCALL_EINVAL     = -22;   // Invalid argument
static const int64_t SYSCALL_ENOMEM     = -12;   // Out of memory
static const int64_t SYSCALL_EFAULT     = -14;   // Bad address
static const int64_t SYSCALL_ENOENT     = -2;    // No such file or directory
static const int64_t SYSCALL_EIO        = -5;    // I/O error
static const int64_t SYSCALL_EACCES     = -13;   // Permission denied
static const int64_t SYSCALL_EEXIST     = -17;   // File exists
static const int64_t SYSCALL_ENOTDIR    = -20;   // Not a directory
static const int64_t SYSCALL_EISDIR     = -21;   // Is a directory
static const int64_t SYSCALL_EMFILE     = -24;   // Too many open files
static const int64_t SYSCALL_EBADF      = -9;    // Bad file descriptor

// ================================================================
// Syscall argument structure
// ================================================================

struct SyscallArgs {
    uint64_t syscall_nr;    // Syscall number (from $v0)
    uint64_t arg0;          // $a0 ($4)
    uint64_t arg1;          // $a1 ($5)
    uint64_t arg2;          // $a2 ($6)
    uint64_t arg3;          // $a3 ($7)
    uint64_t arg4;          // $a4 ($8) - t0 in o32
    uint64_t arg5;          // $a5 ($9) - t1 in o32
    uint64_t arg6;          // $a6 ($10) - t2 in o32
    uint64_t arg7;          // $a7 ($11) - t3 in o32
};

// ================================================================
// Syscall handler type
// ================================================================

typedef int64_t (*SyscallHandler)(SyscallArgs* args);

// ================================================================
// Trap/Exception types
// ================================================================

// Exception codes from Cause register ExcCode field
enum ExceptionCode : uint8_t {
    TRAP_INTERRUPT      = 0,   // Interrupt
    TRAP_TLB_MOD        = 1,   // TLB modification exception
    TRAP_TLB_LOAD       = 2,   // TLB miss (load or fetch)
    TRAP_TLB_STORE      = 3,   // TLB miss (store)
    TRAP_ADDR_LOAD      = 4,   // Address error (load or fetch)
    TRAP_ADDR_STORE     = 5,   // Address error (store)
    TRAP_BUS_FETCH      = 6,   // Bus error (instruction fetch)
    TRAP_BUS_DATA       = 7,   // Bus error (data load/store)
    TRAP_SYSCALL        = 8,   // System call
    TRAP_BREAKPOINT     = 9,   // Breakpoint
    TRAP_RESERVED_INST  = 10,  // Reserved instruction
    TRAP_COP_UNUSABLE   = 11,  // Coprocessor unusable
    TRAP_OVERFLOW       = 12,  // Arithmetic overflow
    TRAP_TRAP           = 13,  // Trap instruction
    TRAP_MSAFPE         = 14,  // MSA floating-point exception
    TRAP_FPE            = 15,  // Floating-point exception
    TRAP_WATCH          = 23,  // Watch exception
    TRAP_MACHINE_CHECK  = 24,  // Machine check
};

// ================================================================
// IRQ handler type
// ================================================================

typedef void (*IrqHandler)(uint32_t irq_number);

// ================================================================
// Syscall/Exception management functions
// ================================================================

// Initialize the syscall and exception handling subsystem
void init();

// Register a syscall handler
void register_handler(uint64_t syscall_nr, SyscallHandler handler);

// Register an IRQ handler
void register_irq_handler(uint32_t irq, IrqHandler handler);

// Main trap dispatcher (called from boot.s)
extern "C" void trap_dispatch(void* context);

// TLB refill handlers (called from boot.s)
extern "C" void trap_tlb_refill();
extern "C" void trap_xtlb_refill();

// Cache error handler (called from boot.s)
extern "C" void trap_cache_error();

// Enable/disable specific interrupt sources
void enable_irq(uint32_t irq);
void disable_irq(uint32_t irq);

// Timer interrupt handling
void set_timer(uint64_t ticks);
uint64_t get_timer_count();

// Interrupt management
void interrupt_init();

} // namespace syscall
} // namespace mips64
} // namespace arch
} // namespace kernel
