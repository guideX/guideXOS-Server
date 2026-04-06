//
// SPARC v9 (64-bit) System Call Interface
//
// Defines syscall numbers, the syscall handler interface, and
// trap dispatch for the SPARC v9 (UltraSPARC) architecture.
//
// SPARC v9 syscall mechanism:
//   - User code executes 'ta 0x6d' (typical for 64-bit syscalls)
//   - CPU traps to supervisor, vectors via TBA
//   - Syscall number in %g1
//   - Arguments in %o0-%o5
//   - Return value in %o0
//
// Key v9 differences:
//   - 64-bit registers
//   - Different trap table layout (32-byte entries)
//   - PSTATE instead of PSR
//   - Multiple trap levels (TL)
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace sparc64 {
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
static const uint64_t SYS_GXOS_WINDOW   = 0x83;

static const uint64_t SYS_MAX           = 0xFF;

// ================================================================
// Syscall result codes
// ================================================================

static const int64_t SYSCALL_SUCCESS    = 0;
static const int64_t SYSCALL_EPERM      = -1;
static const int64_t SYSCALL_ENOENT     = -2;
static const int64_t SYSCALL_ESRCH      = -3;
static const int64_t SYSCALL_EINTR      = -4;
static const int64_t SYSCALL_EIO        = -5;
static const int64_t SYSCALL_ENOMEM     = -12;
static const int64_t SYSCALL_EACCES     = -13;
static const int64_t SYSCALL_EFAULT     = -14;
static const int64_t SYSCALL_EBUSY      = -16;
static const int64_t SYSCALL_EEXIST     = -17;
static const int64_t SYSCALL_ENODEV     = -19;
static const int64_t SYSCALL_ENOTDIR    = -20;
static const int64_t SYSCALL_EISDIR     = -21;
static const int64_t SYSCALL_EINVAL     = -22;
static const int64_t SYSCALL_EMFILE     = -24;
static const int64_t SYSCALL_ENOSPC     = -28;
static const int64_t SYSCALL_ENOSYS     = -38;

// ================================================================
// Syscall argument structure
// ================================================================

struct SyscallArgs {
    uint64_t syscall_nr;   // %g1
    uint64_t arg0;         // %o0 (also return value)
    uint64_t arg1;         // %o1
    uint64_t arg2;         // %o2
    uint64_t arg3;         // %o3
    uint64_t arg4;         // %o4
    uint64_t arg5;         // %o5
};

typedef int64_t (*SyscallHandler)(SyscallArgs* args);

// ================================================================
// Public API
// ================================================================

void init();
bool register_handler(uint64_t syscall_nr, SyscallHandler handler);
void unregister_handler(uint64_t syscall_nr);
int64_t dispatch(SyscallArgs* args);

// ================================================================
// Trap handling
// ================================================================

// SPARC v9 trap types
static const uint64_t TT_ILLEGAL_INST     = 0x10;
static const uint64_t TT_PRIV_INST        = 0x11;
static const uint64_t TT_FP_DISABLED      = 0x20;
static const uint64_t TT_FP_EXCEPTION     = 0x21;
static const uint64_t TT_TAG_OVERFLOW     = 0x23;
static const uint64_t TT_CLEAN_WINDOW     = 0x24;
static const uint64_t TT_DIV_BY_ZERO      = 0x28;
static const uint64_t TT_DATA_ACCESS_EXC  = 0x30;
static const uint64_t TT_DATA_ACCESS_MMU  = 0x31;
static const uint64_t TT_DATA_ACCESS_ERR  = 0x32;
static const uint64_t TT_INST_ACCESS_EXC  = 0x08;
static const uint64_t TT_INST_ACCESS_MMU  = 0x09;
static const uint64_t TT_INST_ACCESS_ERR  = 0x0A;
static const uint64_t TT_SPILL_0_NORMAL   = 0x80;
static const uint64_t TT_FILL_0_NORMAL    = 0xC0;
// 0x100-0x17F: Software traps (ta instruction)
// 0x41-0x4F: Interrupt level 1-15

extern "C" void trap_dispatch(uint64_t tt, uint64_t tpc, uint64_t tstate);

void handle_syscall();
void handle_spill_trap();
void handle_fill_trap();
void handle_data_access_exception(uint64_t addr);
void handle_interrupt(uint32_t level);

// ================================================================
// PCI interrupt controller (Psycho/Sabre on Sun4u)
// ================================================================

void init_interrupts();
void enable_irq(uint32_t irq);
void disable_irq(uint32_t irq);
void ack_irq(uint32_t irq);

typedef void (*IrqHandler)(uint32_t irq);
void register_irq_handler(uint32_t irq, IrqHandler handler);

} // namespace syscall
} // namespace sparc64
} // namespace arch
} // namespace kernel
