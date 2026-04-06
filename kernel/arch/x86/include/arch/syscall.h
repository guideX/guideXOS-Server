//
// x86 (32-bit) System Call Interface
//
// Defines syscall numbers, the syscall handler interface, and
// interrupt dispatch for the x86 (IA-32) architecture.
//
// x86 syscall mechanism (INT 0x80):
//   - User code executes INT 0x80 instruction
//   - CPU looks up IDT entry 0x80, switches to kernel
//   - Syscall number is in EAX
//   - Arguments are in EBX, ECX, EDX, ESI, EDI, EBP
//   - Return value goes in EAX
//
// Alternative: SYSENTER/SYSEXIT (faster, Pentium II+)
//   - Requires MSR setup (IA32_SYSENTER_CS/EIP/ESP)
//   - Syscall number in EAX, args in EBX, ECX, EDX, ESI, EDI
//
// This implementation uses INT 0x80 for simplicity.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace x86 {
namespace syscall {

// ================================================================
// System Call Numbers
//
// These are guideXOS-specific syscall numbers.
//
// Syscall categories:
//   0x00-0x0F: Process management
//   0x10-0x1F: Memory management
//   0x20-0x2F: File operations
//   0x30-0x3F: IPC (Inter-Process Communication)
//   0x40-0x4F: Time and scheduling
//   0x50-0x5F: Device I/O
//   0x60-0x6F: Network
//   0x70-0x7F: Reserved
//   0x80-0xFF: guideXOS extensions
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
// x86 INT 0x80 convention: EBX, ECX, EDX, ESI, EDI, EBP
// ================================================================

struct SyscallArgs {
    uint32_t syscall_nr;   // Syscall number (from EAX)
    uint32_t arg0;         // EBX
    uint32_t arg1;         // ECX
    uint32_t arg2;         // EDX
    uint32_t arg3;         // ESI
    uint32_t arg4;         // EDI
    uint32_t arg5;         // EBP
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
// Exception/Interrupt handling
// ================================================================

extern "C" void exception_dispatch(uint32_t vector, uint32_t error_code,
                                   uint32_t eip, uint32_t eflags);

void handle_syscall();
void handle_page_fault(uint32_t error_code, uint32_t fault_addr);
void handle_general_protection(uint32_t error_code);
void handle_divide_error();
void handle_invalid_opcode();
void handle_double_fault();
void handle_interrupt(uint32_t vector);

// ================================================================
// Interrupt controller interface
// ================================================================

void init_interrupts();
void enable_irq(uint32_t irq);
void disable_irq(uint32_t irq);
void ack_irq(uint32_t irq);

typedef void (*IrqHandler)(uint32_t irq);
void register_irq_handler(uint32_t irq, IrqHandler handler);

} // namespace syscall
} // namespace x86
} // namespace arch
} // namespace kernel
