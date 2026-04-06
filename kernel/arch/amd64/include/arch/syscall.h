//
// AMD64 (x86-64) System Call Interface
//
// Defines syscall numbers, the syscall handler interface, and
// interrupt dispatch for the AMD64 architecture.
//
// AMD64 syscall mechanism (SYSCALL/SYSRET):
//   - User code executes SYSCALL instruction
//   - CPU loads RIP from LSTAR MSR, CS from STAR MSR
//   - Syscall number is in RAX
//   - Arguments are in RDI, RSI, RDX, R10, R8, R9 (System V-like)
//   - Return value goes in RAX
//   - RCX and R11 are clobbered (RCX=RIP, R11=RFLAGS)
//
// Alternative: INT 0x80 (legacy, slower but simpler)
//   - Syscall number in EAX
//   - Arguments in EBX, ECX, EDX, ESI, EDI, EBP
//
// This implementation uses SYSCALL/SYSRET for 64-bit mode.
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace amd64 {
namespace syscall {

// ================================================================
// System Call Numbers
//
// These are guideXOS-specific syscall numbers. They are not
// compatible with Linux syscalls (which start at different values).
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
static const uint64_t SYS_EXIT          = 0x00;   // Exit process
static const uint64_t SYS_FORK          = 0x01;   // Fork process
static const uint64_t SYS_EXEC          = 0x02;   // Execute program
static const uint64_t SYS_WAIT          = 0x03;   // Wait for child
static const uint64_t SYS_GETPID        = 0x04;   // Get process ID
static const uint64_t SYS_GETPPID       = 0x05;   // Get parent PID
static const uint64_t SYS_GETTID        = 0x06;   // Get thread ID
static const uint64_t SYS_KILL          = 0x07;   // Send signal
static const uint64_t SYS_YIELD         = 0x08;   // Yield CPU
static const uint64_t SYS_CLONE         = 0x09;   // Create thread

// Memory management
static const uint64_t SYS_BRK           = 0x10;   // Set program break
static const uint64_t SYS_MMAP          = 0x11;   // Map memory
static const uint64_t SYS_MUNMAP        = 0x12;   // Unmap memory
static const uint64_t SYS_MPROTECT      = 0x13;   // Change protection

// File operations
static const uint64_t SYS_OPEN          = 0x20;   // Open file
static const uint64_t SYS_CLOSE         = 0x21;   // Close file
static const uint64_t SYS_READ          = 0x22;   // Read from file
static const uint64_t SYS_WRITE         = 0x23;   // Write to file
static const uint64_t SYS_LSEEK         = 0x24;   // Seek in file
static const uint64_t SYS_FSTAT         = 0x25;   // Get file status
static const uint64_t SYS_STAT          = 0x26;   // Get file status by path
static const uint64_t SYS_MKDIR         = 0x27;   // Create directory
static const uint64_t SYS_RMDIR         = 0x28;   // Remove directory
static const uint64_t SYS_UNLINK        = 0x29;   // Delete file
static const uint64_t SYS_RENAME        = 0x2A;   // Rename file
static const uint64_t SYS_READDIR       = 0x2B;   // Read directory entry
static const uint64_t SYS_DUP           = 0x2C;   // Duplicate file descriptor
static const uint64_t SYS_DUP2          = 0x2D;   // Duplicate to specific fd
static const uint64_t SYS_IOCTL         = 0x2E;   // Device control

// IPC (Inter-Process Communication)
static const uint64_t SYS_PIPE          = 0x30;   // Create pipe
static const uint64_t SYS_MSGGET        = 0x31;   // Get message queue
static const uint64_t SYS_MSGSND        = 0x32;   // Send message
static const uint64_t SYS_MSGRCV        = 0x33;   // Receive message
static const uint64_t SYS_SEMGET        = 0x34;   // Get semaphore
static const uint64_t SYS_SEMOP         = 0x35;   // Semaphore operation
static const uint64_t SYS_SHMGET        = 0x36;   // Get shared memory
static const uint64_t SYS_SHMAT         = 0x37;   // Attach shared memory
static const uint64_t SYS_SHMDT         = 0x38;   // Detach shared memory
static const uint64_t SYS_IPC_SEND      = 0x39;   // guideXOS IPC send
static const uint64_t SYS_IPC_RECV      = 0x3A;   // guideXOS IPC receive

// Time and scheduling
static const uint64_t SYS_GETTIME       = 0x40;   // Get current time
static const uint64_t SYS_SETTIME       = 0x41;   // Set current time
static const uint64_t SYS_NANOSLEEP     = 0x42;   // Sleep (nanoseconds)
static const uint64_t SYS_GETPRIORITY   = 0x43;   // Get scheduling priority
static const uint64_t SYS_SETPRIORITY   = 0x44;   // Set scheduling priority
static const uint64_t SYS_SCHED_YIELD   = 0x45;   // Yield to scheduler
static const uint64_t SYS_ALARM         = 0x46;   // Set alarm timer

// Device I/O
static const uint64_t SYS_DEVOPEN       = 0x50;   // Open device
static const uint64_t SYS_DEVCLOSE      = 0x51;   // Close device
static const uint64_t SYS_DEVREAD       = 0x52;   // Read from device
static const uint64_t SYS_DEVWRITE      = 0x53;   // Write to device
static const uint64_t SYS_DEVIOCTL      = 0x54;   // Device control

// Network
static const uint64_t SYS_SOCKET        = 0x60;   // Create socket
static const uint64_t SYS_BIND          = 0x61;   // Bind socket
static const uint64_t SYS_LISTEN        = 0x62;   // Listen on socket
static const uint64_t SYS_ACCEPT        = 0x63;   // Accept connection
static const uint64_t SYS_CONNECT       = 0x64;   // Connect to server
static const uint64_t SYS_SEND          = 0x65;   // Send data
static const uint64_t SYS_RECV          = 0x66;   // Receive data
static const uint64_t SYS_SENDTO        = 0x67;   // Send to address
static const uint64_t SYS_RECVFROM      = 0x68;   // Receive with address

// guideXOS extensions
static const uint64_t SYS_GXOS_DEBUG    = 0x80;   // Debug output
static const uint64_t SYS_GXOS_INFO     = 0x81;   // Get system info
static const uint64_t SYS_GXOS_GRAPHICS = 0x82;   // Graphics operations
static const uint64_t SYS_GXOS_WINDOW   = 0x83;   // Window management

// Maximum syscall number
static const uint64_t SYS_MAX           = 0xFF;

// ================================================================
// Syscall result codes
// ================================================================

static const int64_t SYSCALL_SUCCESS    = 0;
static const int64_t SYSCALL_EPERM      = -1;    // Operation not permitted
static const int64_t SYSCALL_ENOENT     = -2;    // No such file or directory
static const int64_t SYSCALL_ESRCH      = -3;    // No such process
static const int64_t SYSCALL_EINTR      = -4;    // Interrupted syscall
static const int64_t SYSCALL_EIO        = -5;    // I/O error
static const int64_t SYSCALL_ENOMEM     = -12;   // Out of memory
static const int64_t SYSCALL_EACCES     = -13;   // Permission denied
static const int64_t SYSCALL_EFAULT     = -14;   // Bad address
static const int64_t SYSCALL_EBUSY      = -16;   // Device busy
static const int64_t SYSCALL_EEXIST     = -17;   // File exists
static const int64_t SYSCALL_ENODEV     = -19;   // No such device
static const int64_t SYSCALL_ENOTDIR    = -20;   // Not a directory
static const int64_t SYSCALL_EISDIR     = -21;   // Is a directory
static const int64_t SYSCALL_EINVAL     = -22;   // Invalid argument
static const int64_t SYSCALL_EMFILE     = -24;   // Too many open files
static const int64_t SYSCALL_ENOSPC     = -28;   // No space left
static const int64_t SYSCALL_ENOSYS     = -38;   // Function not implemented

// ================================================================
// Syscall argument structure
//
// Passed to syscall handlers, contains all arguments from registers.
// AMD64 ABI: RDI, RSI, RDX, R10, R8, R9 (syscall clobbers RCX)
// ================================================================

struct SyscallArgs {
    uint64_t syscall_nr;   // Syscall number (from RAX)
    uint64_t arg0;         // RDI - First argument (also return value)
    uint64_t arg1;         // RSI
    uint64_t arg2;         // RDX
    uint64_t arg3;         // R10 (not RCX - clobbered by SYSCALL)
    uint64_t arg4;         // R8
    uint64_t arg5;         // R9
};

// ================================================================
// Syscall handler function type
// ================================================================

typedef int64_t (*SyscallHandler)(SyscallArgs* args);

// ================================================================
// Public API
// ================================================================

// Initialize the syscall subsystem.
// Configures SYSCALL/SYSRET MSRs and sets up dispatch tables.
void init();

// Register a handler for a specific syscall number.
// Returns true if registration succeeded.
bool register_handler(uint64_t syscall_nr, SyscallHandler handler);

// Unregister a syscall handler.
void unregister_handler(uint64_t syscall_nr);

// Dispatch a syscall (called from syscall entry stub).
// Returns the result to be placed in RAX.
int64_t dispatch(SyscallArgs* args);

// ================================================================
// Exception/Interrupt handling (integrates with IDT)
// ================================================================

// Main exception dispatcher (called from IDT stub).
// Determines exception type and routes to appropriate handler.
extern "C" void exception_dispatch(uint64_t vector, uint64_t error_code,
                                   uint64_t rip, uint64_t rflags);

// Specific exception handlers
void handle_syscall();                              // Via SYSCALL instruction
void handle_page_fault(uint64_t error_code, uint64_t fault_addr);
void handle_general_protection(uint64_t error_code);
void handle_divide_error();
void handle_invalid_opcode();
void handle_double_fault();
void handle_interrupt(uint32_t vector);

// ================================================================
// Interrupt controller interface (8259 PIC or APIC)
// ================================================================

// Initialize interrupt controller (PIC or APIC).
void init_interrupts();

// Enable a specific interrupt (IRQ number).
void enable_irq(uint32_t irq);

// Disable a specific interrupt.
void disable_irq(uint32_t irq);

// Acknowledge/clear an interrupt (send EOI).
void ack_irq(uint32_t irq);

// Register an interrupt handler.
typedef void (*IrqHandler)(uint32_t irq);
void register_irq_handler(uint32_t irq, IrqHandler handler);

// ================================================================
// MSR addresses for SYSCALL/SYSRET
// ================================================================

static const uint32_t MSR_EFER    = 0xC0000080;  // Extended Feature Enable Register
static const uint32_t MSR_STAR    = 0xC0000081;  // Segment selector MSR
static const uint32_t MSR_LSTAR   = 0xC0000082;  // Long mode SYSCALL target RIP
static const uint32_t MSR_CSTAR   = 0xC0000083;  // Compat mode SYSCALL target (not used)
static const uint32_t MSR_SFMASK  = 0xC0000084;  // SYSCALL flag mask

// EFER bits
static const uint64_t EFER_SCE    = (1ULL << 0);  // SYSCALL enable

} // namespace syscall
} // namespace amd64
} // namespace arch
} // namespace kernel
