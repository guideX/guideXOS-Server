//
// RISC-V 64-bit System Call Interface
//
// Defines syscall numbers, the syscall handler interface, and
// trap dispatch for the RISC-V 64-bit architecture.
//
// RISC-V syscall mechanism (ecall instruction):
//   - User code executes ecall instruction
//   - CPU traps to supervisor mode, jumps to stvec
//   - Syscall number is in a7 (x17)
//   - Arguments are in a0-a6 (x10-x16)
//   - Return value goes in a0
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace riscv64 {
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
    uint64_t syscall_nr;   // a7 (x17)
    uint64_t arg0;         // a0 (x10) - also return value
    uint64_t arg1;         // a1 (x11)
    uint64_t arg2;         // a2 (x12)
    uint64_t arg3;         // a3 (x13)
    uint64_t arg4;         // a4 (x14)
    uint64_t arg5;         // a5 (x15)
    uint64_t arg6;         // a6 (x16)
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

// RISC-V scause values for exceptions
static const uint64_t CAUSE_MISALIGNED_FETCH    = 0;
static const uint64_t CAUSE_FETCH_ACCESS        = 1;
static const uint64_t CAUSE_ILLEGAL_INST        = 2;
static const uint64_t CAUSE_BREAKPOINT          = 3;
static const uint64_t CAUSE_MISALIGNED_LOAD     = 4;
static const uint64_t CAUSE_LOAD_ACCESS         = 5;
static const uint64_t CAUSE_MISALIGNED_STORE    = 6;
static const uint64_t CAUSE_STORE_ACCESS        = 7;
static const uint64_t CAUSE_USER_ECALL          = 8;
static const uint64_t CAUSE_SUPERVISOR_ECALL    = 9;
static const uint64_t CAUSE_MACHINE_ECALL       = 11;
static const uint64_t CAUSE_FETCH_PAGE_FAULT    = 12;
static const uint64_t CAUSE_LOAD_PAGE_FAULT     = 13;
static const uint64_t CAUSE_STORE_PAGE_FAULT    = 15;

// Interrupt bit (MSB of scause)
static const uint64_t CAUSE_INTERRUPT           = (1ULL << 63);

// Interrupt causes
static const uint64_t IRQ_S_SOFT                = 1;
static const uint64_t IRQ_S_TIMER               = 5;
static const uint64_t IRQ_S_EXTERNAL            = 9;

extern "C" void trap_dispatch(uint64_t scause, uint64_t sepc, uint64_t stval);

void handle_syscall();
void handle_page_fault(uint64_t stval, bool is_store, bool is_fetch);
void handle_illegal_instruction(uint64_t stval);
void handle_interrupt(uint64_t cause);

// ================================================================
// Interrupt controller interface (PLIC)
// ================================================================

void init_interrupts();
void enable_irq(uint32_t irq);
void disable_irq(uint32_t irq);
void ack_irq(uint32_t irq);

typedef void (*IrqHandler)(uint32_t irq);
void register_irq_handler(uint32_t irq, IrqHandler handler);

} // namespace syscall
} // namespace riscv64
} // namespace arch
} // namespace kernel
