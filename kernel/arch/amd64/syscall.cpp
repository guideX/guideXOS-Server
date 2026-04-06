//
// AMD64 System Call Handler Implementation
//
// Handles syscalls, exceptions, and interrupts for AMD64.
// This module is the central dispatcher for all trap-level events.
//
// SYSCALL/SYSRET flow:
// 1. User code executes SYSCALL instruction
// 2. CPU loads RIP from LSTAR, saves user RIP to RCX, RFLAGS to R11
// 3. Assembly stub saves registers and calls syscall_dispatch()
// 4. syscall_dispatch() determines syscall and calls handler
// 5. Handler processes request and returns result in RAX
// 6. Assembly stub restores registers and executes SYSRET
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/syscall.h"
#include "include/arch/amd64.h"
#include "include/arch/context_switch.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace amd64 {
namespace syscall {

namespace {

// ================================================================
// Syscall dispatch table
// ================================================================

static SyscallHandler s_handlers[SYS_MAX + 1] = { nullptr };

// ================================================================
// Current syscall arguments (set by syscall entry stub)
// ================================================================

static SyscallArgs s_current_args;

// ================================================================
// Interrupt handler table
// ================================================================

static const uint32_t MAX_IRQS = 256;  // Full IDT range
static IrqHandler s_irq_handlers[MAX_IRQS] = { nullptr };

// ================================================================
// Simple serial output for early debugging
// (Uses COM1 at 0x3F8)
// ================================================================

static void serial_putc(char c)
{
    // Wait for transmit buffer empty
    while ((inb(0x3F8 + 5) & 0x20) == 0) {}
    outb(0x3F8, static_cast<uint8_t>(c));
}

static void serial_puts(const char* str)
{
    if (!str) return;
    while (*str) {
        if (*str == '\n') serial_putc('\r');
        serial_putc(*str++);
    }
}

static void serial_put_hex(uint64_t value)
{
    serial_puts("0x");
    static const char hex[] = "0123456789ABCDEF";
    bool leading = true;
    for (int i = 60; i >= 0; i -= 4) {
        uint64_t nibble = (value >> i) & 0xF;
        if (nibble != 0) leading = false;
        if (!leading || i == 0) {
            serial_putc(hex[nibble]);
        }
    }
}

// ================================================================
// Default syscall handlers (stubs)
// ================================================================

static int64_t sys_exit(SyscallArgs* args)
{
    int64_t exit_code = static_cast<int64_t>(args->arg0);
    serial_puts("[Syscall] exit(");
    serial_put_hex(static_cast<uint64_t>(exit_code));
    serial_puts(")\n");
    
    // TODO: Actually terminate the process
    // For now, just halt
    while (1) {
        halt();
    }
    
    return 0;
}

static int64_t sys_write(SyscallArgs* args)
{
    int fd = static_cast<int>(args->arg0);
    const char* buf = reinterpret_cast<const char*>(args->arg1);
    uint64_t count = args->arg2;
    
    // Only handle stdout/stderr for now
    if (fd == 1 || fd == 2) {
        for (uint64_t i = 0; i < count && buf[i]; ++i) {
            serial_putc(buf[i]);
        }
        return static_cast<int64_t>(count);
    }
    
    return SYSCALL_ENOSYS;
}

static int64_t sys_getpid(SyscallArgs* /* args */)
{
    // TODO: Return actual PID from process table
    return 1;
}

static int64_t sys_yield(SyscallArgs* /* args */)
{
    // TODO: Call scheduler to yield
    return SYSCALL_SUCCESS;
}

static int64_t sys_debug(SyscallArgs* args)
{
    const char* msg = reinterpret_cast<const char*>(args->arg0);
    if (msg) {
        serial_puts("[Debug] ");
        serial_puts(msg);
        serial_puts("\n");
    }
    return SYSCALL_SUCCESS;
}

static int64_t sys_gettime(SyscallArgs* /* args */)
{
    // TODO: Read from RTC or TSC
    // For now, read TSC (Time Stamp Counter)
    uint32_t lo, hi;
#if GXOS_MSVC_STUB
    lo = hi = 0;
#else
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
#endif
    return static_cast<int64_t>((static_cast<uint64_t>(hi) << 32) | lo);
}

static int64_t sys_not_implemented(SyscallArgs* args)
{
    serial_puts("[Syscall] Unimplemented syscall: ");
    serial_put_hex(args->syscall_nr);
    serial_puts("\n");
    return SYSCALL_ENOSYS;
}

// ================================================================
// Register default syscall handlers
// ================================================================

static void register_default_handlers()
{
    s_handlers[SYS_EXIT] = sys_exit;
    s_handlers[SYS_GETPID] = sys_getpid;
    s_handlers[SYS_YIELD] = sys_yield;
    s_handlers[SYS_WRITE] = sys_write;
    s_handlers[SYS_GETTIME] = sys_gettime;
    s_handlers[SYS_GXOS_DEBUG] = sys_debug;
}

} // anonymous namespace

// ================================================================
// Public API implementation
// ================================================================

void init()
{
    serial_puts("[Syscall] Initializing AMD64 syscall subsystem\n");
    
    // Clear all handlers
    for (uint32_t i = 0; i <= SYS_MAX; ++i) {
        s_handlers[i] = nullptr;
    }
    
    // Register default handlers
    register_default_handlers();
    
#if !GXOS_MSVC_STUB
    // Configure SYSCALL/SYSRET MSRs
    
    // Enable SYSCALL in EFER
    uint64_t efer = read_msr(MSR_EFER);
    efer |= EFER_SCE;
    write_msr(MSR_EFER, efer);
    
    // STAR MSR: bits 47:32 = SYSRET CS/SS base, bits 63:48 = SYSCALL CS/SS base
    // Assuming GDT layout: 0x08 = kernel code, 0x10 = kernel data,
    //                      0x18 = user code, 0x20 = user data
    // SYSCALL loads CS from bits 47:32, SS from bits 47:32 + 8
    // SYSRET loads CS from bits 63:48 + 16 (64-bit) or +0 (32-bit), SS from bits 63:48 + 8
    uint64_t star = ((uint64_t)0x08 << 32) |  // SYSCALL: kernel CS = 0x08
                    ((uint64_t)0x18 << 48);   // SYSRET: user CS base = 0x18
    write_msr(MSR_STAR, star);
    
    // LSTAR MSR: SYSCALL entry point (64-bit mode)
    // TODO: Set to actual syscall_entry assembly stub address
    extern "C" void syscall_entry();
    write_msr(MSR_LSTAR, reinterpret_cast<uint64_t>(&syscall_entry));
    
    // SFMASK MSR: RFLAGS bits to clear on SYSCALL
    // Clear IF (interrupts), TF (trap), DF (direction), AC (alignment check)
    write_msr(MSR_SFMASK, 0x47700);
#endif
    
    // Initialize interrupt controller
    init_interrupts();
    
    serial_puts("[Syscall] AMD64 syscall subsystem initialized\n");
}

bool register_handler(uint64_t syscall_nr, SyscallHandler handler)
{
    if (syscall_nr > SYS_MAX) {
        return false;
    }
    s_handlers[syscall_nr] = handler;
    return true;
}

void unregister_handler(uint64_t syscall_nr)
{
    if (syscall_nr <= SYS_MAX) {
        s_handlers[syscall_nr] = nullptr;
    }
}

int64_t dispatch(SyscallArgs* args)
{
    if (!args || args->syscall_nr > SYS_MAX) {
        return SYSCALL_EINVAL;
    }
    
    SyscallHandler handler = s_handlers[args->syscall_nr];
    if (handler) {
        return handler(args);
    }
    
    return sys_not_implemented(args);
}

// ================================================================
// Exception dispatcher (called from IDT stubs)
// ================================================================

extern "C" void exception_dispatch(uint64_t vector, uint64_t error_code,
                                   uint64_t rip, uint64_t rflags)
{
    (void)rflags;
    
    switch (vector) {
        case 0:   // Divide Error
            handle_divide_error();
            break;
            
        case 6:   // Invalid Opcode
            handle_invalid_opcode();
            break;
            
        case 8:   // Double Fault
            handle_double_fault();
            break;
            
        case 13:  // General Protection Fault
            handle_general_protection(error_code);
            break;
            
        case 14:  // Page Fault
            {
                uint64_t fault_addr;
#if GXOS_MSVC_STUB
                fault_addr = 0;
#else
                fault_addr = read_cr2();
#endif
                handle_page_fault(error_code, fault_addr);
            }
            break;
            
        default:
            if (vector >= 32 && vector < 256) {
                // Hardware interrupt or software interrupt
                handle_interrupt(static_cast<uint32_t>(vector));
            } else {
                serial_puts("[Exception] Unhandled exception vector=");
                serial_put_hex(vector);
                serial_puts(" at RIP=");
                serial_put_hex(rip);
                serial_puts("\n");
                while (1) { halt(); }
            }
            break;
    }
}

// ================================================================
// Specific exception handlers
// ================================================================

void handle_syscall()
{
    // Note: This is called from the SYSCALL entry stub
    // The stub should have set up s_current_args from registers
    
#if !GXOS_MSVC_STUB
    // Dispatch the syscall
    int64_t result = dispatch(&s_current_args);
    
    // Store result (will be returned in RAX by the stub)
    s_current_args.arg0 = static_cast<uint64_t>(result);
#endif
}

void handle_page_fault(uint64_t error_code, uint64_t fault_addr)
{
    serial_puts("[PageFault] ");
    serial_puts((error_code & 0x1) ? "Protection" : "Not-present");
    serial_puts(" violation on ");
    serial_puts((error_code & 0x2) ? "write" : "read");
    serial_puts((error_code & 0x4) ? " (user)" : " (kernel)");
    serial_puts(" at ");
    serial_put_hex(fault_addr);
    serial_puts("\n");
    
    // TODO: Implement page fault handling
    // 1. Check if address is valid for this process
    // 2. If valid, allocate page and update page tables
    // 3. If invalid, send SIGSEGV or terminate
    
    while (1) { halt(); }
}

void handle_general_protection(uint64_t error_code)
{
    serial_puts("[GPF] General Protection Fault, error_code=");
    serial_put_hex(error_code);
    serial_puts("\n");
    while (1) { halt(); }
}

void handle_divide_error()
{
    serial_puts("[Exception] Divide by zero\n");
    while (1) { halt(); }
}

void handle_invalid_opcode()
{
    serial_puts("[Exception] Invalid opcode\n");
    while (1) { halt(); }
}

void handle_double_fault()
{
    serial_puts("[Exception] Double fault - system halted\n");
    while (1) { halt(); }
}

void handle_interrupt(uint32_t vector)
{
    // Map vector to IRQ number (assuming PIC remapped to 32-47)
    uint32_t irq = vector - 32;
    
    if (irq == 0) {
        // Timer interrupt (IRQ 0)
        context::arch_timer_tick();
    } else if (irq < MAX_IRQS && s_irq_handlers[irq]) {
        s_irq_handlers[irq](irq);
    }
    
    ack_irq(irq);
}

// ================================================================
// Interrupt controller interface (8259 PIC)
// ================================================================

// PIC ports
static const uint16_t PIC1_CMD  = 0x20;
static const uint16_t PIC1_DATA = 0x21;
static const uint16_t PIC2_CMD  = 0xA0;
static const uint16_t PIC2_DATA = 0xA1;

// PIC commands
static const uint8_t PIC_EOI = 0x20;

void init_interrupts()
{
    for (uint32_t i = 0; i < MAX_IRQS; ++i) {
        s_irq_handlers[i] = nullptr;
    }
    
#if !GXOS_MSVC_STUB
    // Initialize 8259 PIC
    // ICW1: Initialize + ICW4 needed
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);
    
    // ICW2: Vector offset (remap to 0x20-0x2F)
    outb(PIC1_DATA, 0x20);  // PIC1 -> IRQ 0-7  = vectors 0x20-0x27
    outb(PIC2_DATA, 0x28);  // PIC2 -> IRQ 8-15 = vectors 0x28-0x2F
    
    // ICW3: Cascade configuration
    outb(PIC1_DATA, 0x04);  // PIC1 has slave on IRQ2
    outb(PIC2_DATA, 0x02);  // PIC2 cascade identity
    
    // ICW4: 8086 mode
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    
    // Mask all interrupts initially
    outb(PIC1_DATA, 0xFB);  // Allow IRQ2 (cascade)
    outb(PIC2_DATA, 0xFF);
#endif
    
    serial_puts("[IRQ] 8259 PIC initialized\n");
}

void enable_irq(uint32_t irq)
{
    if (irq >= 16) return;
    
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (irq < 8) ? irq : (irq - 8);
    uint8_t mask = inb(port);
    mask &= ~(1 << bit);
    outb(port, mask);
}

void disable_irq(uint32_t irq)
{
    if (irq >= 16) return;
    
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (irq < 8) ? irq : (irq - 8);
    uint8_t mask = inb(port);
    mask |= (1 << bit);
    outb(port, mask);
}

void ack_irq(uint32_t irq)
{
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}

void register_irq_handler(uint32_t irq, IrqHandler handler)
{
    if (irq < MAX_IRQS) {
        s_irq_handlers[irq] = handler;
    }
}

// ================================================================
// SYSCALL entry stub (assembly)
//
// TODO: This should be in a separate .asm file for proper
// low-level register handling. The stub needs to:
// 1. Save user RSP to per-CPU area
// 2. Load kernel RSP from TSS.RSP0
// 3. Save all registers to kernel stack
// 4. Call handle_syscall()
// 5. Restore registers
// 6. Execute SYSRET
// ================================================================

#if !GXOS_MSVC_STUB

// Placeholder syscall entry - needs proper implementation
asm(
    ".global syscall_entry\n"
    ".type syscall_entry, @function\n"
    "syscall_entry:\n"
    
    // TODO: Proper implementation
    // For now, just return -ENOSYS
    "    mov     $-38, %rax\n"   // SYSCALL_ENOSYS
    "    sysretq\n"
    
    ".size syscall_entry, .-syscall_entry\n"
);

#endif

} // namespace syscall
} // namespace amd64
} // namespace arch
} // namespace kernel
