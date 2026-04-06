//
// ARM (32-bit) System Call Interface
//
// Defines syscall numbers, the syscall handler interface, and
// interrupt dispatch for the ARM architecture.
//
// ARM syscall mechanism (SVC/SWI instruction):
//   - User code executes SVC #imm (or SWI #imm in older syntax)
//   - CPU switches to SVC mode, branches to vector table
//   - Syscall number can be in immediate or R7 (EABI)
//   - Arguments are in R0-R6
//   - Return value goes in R0
//
// This implementation uses the ARM EABI convention:
//   - Syscall number in R7
//   - Arguments in R0-R6
//   - Return value in R0
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace arm {
namespace syscall {

// ================================================================
// System Call Numbers
//
// guideXOS-specific syscall numbers (ARM EABI style).
// ================================================================

// Process management
static const uint32_t SYS_EXIT          = 0x00;
static const uint32_t SYS_FORK          = 0x01;
static const uint32_t SYS_EXEC          = 0x02;
static const uint32_t SYS_WAIT          = 0x03;
static const uint32_t SYS_GETPID        = 0x04;
static const uint32_t SYS_GETPPID       = 0x05;
static const uint32_t SYS_GETTID        = 0x06;
static const uint32_t SYS_KILL          = 0x07;
static const uint32_t SYS_YIELD         = 0x08;
static const uint32_t SYS_CLONE         = 0x09;

// Memory management
static const uint32_t SYS_BRK           = 0x10;
static const uint32_t SYS_MMAP          = 0x11;
static const uint32_t SYS_MUNMAP        = 0x12;
static const uint32_t SYS_MPROTECT      = 0x13;

// File operations
static const uint32_t SYS_OPEN          = 0x20;
static const uint32_t SYS_CLOSE         = 0x21;
static const uint32_t SYS_READ          = 0x22;
static const uint32_t SYS_WRITE         = 0x23;
static const uint32_t SYS_LSEEK         = 0x24;
static const uint32_t SYS_FSTAT         = 0x25;
static const uint32_t SYS_STAT          = 0x26;
static const uint32_t SYS_MKDIR         = 0x27;
static const uint32_t SYS_RMDIR         = 0x28;
static const uint32_t SYS_UNLINK        = 0x29;
static const uint32_t SYS_RENAME        = 0x2A;
static const uint32_t SYS_READDIR       = 0x2B;
static const uint32_t SYS_DUP           = 0x2C;
static const uint32_t SYS_DUP2          = 0x2D;
static const uint32_t SYS_IOCTL         = 0x2E;

// IPC
static const uint32_t SYS_PIPE          = 0x30;
static const uint32_t SYS_MSGGET        = 0x31;
static const uint32_t SYS_MSGSND        = 0x32;
static const uint32_t SYS_MSGRCV        = 0x33;
static const uint32_t SYS_SEMGET        = 0x34;
static const uint32_t SYS_SEMOP         = 0x35;
static const uint32_t SYS_SHMGET        = 0x36;
static const uint32_t SYS_SHMAT         = 0x37;
static const uint32_t SYS_SHMDT         = 0x38;
static const uint32_t SYS_IPC_SEND      = 0x39;
static const uint32_t SYS_IPC_RECV      = 0x3A;

// Time and scheduling
static const uint32_t SYS_GETTIME       = 0x40;
static const uint32_t SYS_SETTIME       = 0x41;
static const uint32_t SYS_NANOSLEEP     = 0x42;
static const uint32_t SYS_GETPRIORITY   = 0x43;
static const uint32_t SYS_SETPRIORITY   = 0x44;
static const uint32_t SYS_SCHED_YIELD   = 0x45;
static const uint32_t SYS_ALARM         = 0x46;

// Device I/O
static const uint32_t SYS_DEVOPEN       = 0x50;
static const uint32_t SYS_DEVCLOSE      = 0x51;
static const uint32_t SYS_DEVREAD       = 0x52;
static const uint32_t SYS_DEVWRITE      = 0x53;
static const uint32_t SYS_DEVIOCTL      = 0x54;

// Network
static const uint32_t SYS_SOCKET        = 0x60;
static const uint32_t SYS_BIND          = 0x61;
static const uint32_t SYS_LISTEN        = 0x62;
static const uint32_t SYS_ACCEPT        = 0x63;
static const uint32_t SYS_CONNECT       = 0x64;
static const uint32_t SYS_SEND          = 0x65;
static const uint32_t SYS_RECV          = 0x66;
static const uint32_t SYS_SENDTO        = 0x67;
static const uint32_t SYS_RECVFROM      = 0x68;

// guideXOS extensions
static const uint32_t SYS_GXOS_DEBUG    = 0x80;
static const uint32_t SYS_GXOS_INFO     = 0x81;
static const uint32_t SYS_GXOS_GRAPHICS = 0x82;
static const uint32_t SYS_GXOS_WINDOW   = 0x83;

static const uint32_t SYS_MAX           = 0xFF;

// ================================================================
// Syscall result codes
// ================================================================

static const int32_t SYSCALL_SUCCESS    = 0;
static const int32_t SYSCALL_EPERM      = -1;
static const int32_t SYSCALL_ENOENT     = -2;
static const int32_t SYSCALL_ESRCH      = -3;
static const int32_t SYSCALL_EINTR      = -4;
static const int32_t SYSCALL_EIO        = -5;
static const int32_t SYSCALL_ENOMEM     = -12;
static const int32_t SYSCALL_EACCES     = -13;
static const int32_t SYSCALL_EFAULT     = -14;
static const int32_t SYSCALL_EBUSY      = -16;
static const int32_t SYSCALL_EEXIST     = -17;
static const int32_t SYSCALL_ENODEV     = -19;
static const int32_t SYSCALL_ENOTDIR    = -20;
static const int32_t SYSCALL_EISDIR     = -21;
static const int32_t SYSCALL_EINVAL     = -22;
static const int32_t SYSCALL_EMFILE     = -24;
static const int32_t SYSCALL_ENOSPC     = -28;
static const int32_t SYSCALL_ENOSYS     = -38;

// ================================================================
// Syscall argument structure
//
// ARM EABI: R0-R6 for arguments, R7 for syscall number
// ================================================================

struct SyscallArgs {
    uint32_t syscall_nr;   // Syscall number (from R7)
    uint32_t arg0;         // R0 (also return value)
    uint32_t arg1;         // R1
    uint32_t arg2;         // R2
    uint32_t arg3;         // R3
    uint32_t arg4;         // R4
    uint32_t arg5;         // R5
    uint32_t arg6;         // R6
};

// ================================================================
// Syscall handler function type
// ================================================================

typedef int32_t (*SyscallHandler)(SyscallArgs* args);

// ================================================================
// Public API
// ================================================================

void init();
bool register_handler(uint32_t syscall_nr, SyscallHandler handler);
void unregister_handler(uint32_t syscall_nr);
int32_t dispatch(SyscallArgs* args);

// ================================================================
// Exception handling (ARM vector table)
// ================================================================

// Exception types
static const uint32_t EXC_RESET          = 0;
static const uint32_t EXC_UNDEFINED      = 1;
static const uint32_t EXC_SVC            = 2;  // Supervisor Call (syscall)
static const uint32_t EXC_PREFETCH_ABORT = 3;
static const uint32_t EXC_DATA_ABORT     = 4;
static const uint32_t EXC_IRQ            = 5;
static const uint32_t EXC_FIQ            = 6;

extern "C" void exception_dispatch(uint32_t exc_type, uint32_t fault_addr);

void handle_syscall();
void handle_prefetch_abort(uint32_t fault_addr);
void handle_data_abort(uint32_t fault_addr);
void handle_undefined();
void handle_irq();
void handle_fiq();

// ================================================================
// Interrupt controller interface (VIC or GIC)
// ================================================================

void init_interrupts();
void enable_irq(uint32_t irq);
void disable_irq(uint32_t irq);
void ack_irq(uint32_t irq);

typedef void (*IrqHandler)(uint32_t irq);
void register_irq_handler(uint32_t irq, IrqHandler handler);

} // namespace syscall
} // namespace arm
} // namespace arch
} // namespace kernel
