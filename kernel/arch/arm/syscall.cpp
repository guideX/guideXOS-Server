//
// ARM (32-bit) System Call Handler Implementation
//
// Handles syscalls, exceptions, and interrupts for ARM.
//
// ARM exception flow:
// 1. User code executes SVC #0 instruction
// 2. CPU switches to SVC mode, branches to vector at 0x08
// 3. Vector stub saves registers and calls syscall handler
// 4. Handler processes request via dispatch table
// 5. Stub restores registers and returns via MOVS PC, LR
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/syscall.h"
#include "include/arch/arm.h"
#include "include/arch/context_switch.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace arm {
namespace syscall {

namespace {

// ================================================================
// Syscall dispatch table
// ================================================================

static SyscallHandler s_handlers[SYS_MAX + 1] = { nullptr };

// ================================================================
// Interrupt handler table
// ================================================================

static const uint32_t MAX_IRQS = 128;
static IrqHandler s_irq_handlers[MAX_IRQS] = { nullptr };

// ================================================================
// UART output for debugging (PL011 at 0x101F1000 for Versatile)
// ================================================================

static const uint32_t UART0_BASE = 0x101F1000;
static const uint32_t UART_DR    = 0x00;  // Data Register
static const uint32_t UART_FR    = 0x18;  // Flag Register
static const uint32_t UART_FR_TXFF = (1 << 5);  // TX FIFO Full

static void uart_putc(char c)
{
#if !GXOS_MSVC_STUB
    volatile uint32_t* dr = reinterpret_cast<volatile uint32_t*>(UART0_BASE + UART_DR);
    volatile uint32_t* fr = reinterpret_cast<volatile uint32_t*>(UART0_BASE + UART_FR);
    
    // Wait for TX FIFO not full
    while (*fr & UART_FR_TXFF) {}
    *dr = static_cast<uint32_t>(c);
#else
    (void)c;
#endif
}

static void uart_puts(const char* str)
{
    if (!str) return;
    while (*str) {
        if (*str == '\n') uart_putc('\r');
        uart_putc(*str++);
    }
}

static void uart_put_hex(uint32_t value)
{
    uart_puts("0x");
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(value >> i) & 0xF]);
    }
}

// ================================================================
// Default syscall handlers
// ================================================================

static int32_t sys_exit(SyscallArgs* args)
{
    uart_puts("[Syscall] exit(");
    uart_put_hex(args->arg0);
    uart_puts(")\n");
    while (1) { wait_for_interrupt(); }
    return 0;
}

static int32_t sys_write(SyscallArgs* args)
{
    int fd = static_cast<int>(args->arg0);
    const char* buf = reinterpret_cast<const char*>(args->arg1);
    uint32_t count = args->arg2;
    
    if (fd == 1 || fd == 2) {
        for (uint32_t i = 0; i < count && buf[i]; ++i) {
            uart_putc(buf[i]);
        }
        return static_cast<int32_t>(count);
    }
    return SYSCALL_ENOSYS;
}

static int32_t sys_getpid(SyscallArgs* /* args */)
{
    return 1;
}

static int32_t sys_yield(SyscallArgs* /* args */)
{
    return SYSCALL_SUCCESS;
}

static int32_t sys_debug(SyscallArgs* args)
{
    const char* msg = reinterpret_cast<const char*>(args->arg0);
    if (msg) {
        uart_puts("[Debug] ");
        uart_puts(msg);
        uart_puts("\n");
    }
    return SYSCALL_SUCCESS;
}

static int32_t sys_not_implemented(SyscallArgs* args)
{
    uart_puts("[Syscall] Unimplemented: ");
    uart_put_hex(args->syscall_nr);
    uart_puts("\n");
    return SYSCALL_ENOSYS;
}

static void register_default_handlers()
{
    s_handlers[SYS_EXIT] = sys_exit;
    s_handlers[SYS_GETPID] = sys_getpid;
    s_handlers[SYS_YIELD] = sys_yield;
    s_handlers[SYS_WRITE] = sys_write;
    s_handlers[SYS_GXOS_DEBUG] = sys_debug;
}

} // anonymous namespace

// ================================================================
// Public API implementation
// ================================================================

void init()
{
    uart_puts("[Syscall] Initializing ARM syscall subsystem\n");
    
    for (uint32_t i = 0; i <= SYS_MAX; ++i) {
        s_handlers[i] = nullptr;
    }
    
    register_default_handlers();
    init_interrupts();
    
    // TODO: Set up vector table for SVC exception
    // The vector table is at 0x00000000 or 0xFFFF0000 (high vectors)
    
    uart_puts("[Syscall] ARM syscall subsystem initialized\n");
}

bool register_handler(uint32_t syscall_nr, SyscallHandler handler)
{
    if (syscall_nr > SYS_MAX) return false;
    s_handlers[syscall_nr] = handler;
    return true;
}

void unregister_handler(uint32_t syscall_nr)
{
    if (syscall_nr <= SYS_MAX) {
        s_handlers[syscall_nr] = nullptr;
    }
}

int32_t dispatch(SyscallArgs* args)
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
// Exception dispatcher
// ================================================================

extern "C" void exception_dispatch(uint32_t exc_type, uint32_t fault_addr)
{
    switch (exc_type) {
        case EXC_SVC:
            handle_syscall();
            break;
        case EXC_PREFETCH_ABORT:
            handle_prefetch_abort(fault_addr);
            break;
        case EXC_DATA_ABORT:
            handle_data_abort(fault_addr);
            break;
        case EXC_UNDEFINED:
            handle_undefined();
            break;
        case EXC_IRQ:
            handle_irq();
            break;
        case EXC_FIQ:
            handle_fiq();
            break;
        default:
            uart_puts("[Exception] Unknown type: ");
            uart_put_hex(exc_type);
            uart_puts("\n");
            while (1) { wait_for_interrupt(); }
            break;
    }
}

// ================================================================
// Specific exception handlers
// ================================================================

void handle_syscall()
{
    // TODO: Read syscall number from R7, args from R0-R6
    // These should be read from the saved context on the SVC stack
}

void handle_prefetch_abort(uint32_t fault_addr)
{
    uart_puts("[PrefetchAbort] at ");
    uart_put_hex(fault_addr);
    uart_puts("\n");
    
    // Read IFSR (Instruction Fault Status Register)
#if !GXOS_MSVC_STUB
    uint32_t ifsr;
    asm volatile ("mrc p15, 0, %0, c5, c0, 1" : "=r"(ifsr));
    uart_puts("IFSR=");
    uart_put_hex(ifsr);
    uart_puts("\n");
#endif
    
    while (1) { wait_for_interrupt(); }
}

void handle_data_abort(uint32_t fault_addr)
{
    uart_puts("[DataAbort] at ");
    uart_put_hex(fault_addr);
    uart_puts("\n");
    
    // Read DFSR (Data Fault Status Register)
#if !GXOS_MSVC_STUB
    uint32_t dfsr;
    asm volatile ("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));
    uart_puts("DFSR=");
    uart_put_hex(dfsr);
    uart_puts("\n");
#endif
    
    while (1) { wait_for_interrupt(); }
}

void handle_undefined()
{
    uart_puts("[Exception] Undefined instruction\n");
    while (1) { wait_for_interrupt(); }
}

void handle_irq()
{
    // TODO: Read interrupt number from VIC/GIC
    // For now, assume timer interrupt
    context::arch_timer_tick();
    
    // Acknowledge interrupt (platform-specific)
}

void handle_fiq()
{
    uart_puts("[FIQ] Fast interrupt\n");
    // FIQ is typically used for high-priority, single-source interrupts
}

// ================================================================
// Interrupt controller interface
// Platform: Versatile/RealView VIC (PL190)
// ================================================================

static const uint32_t VIC_BASE = 0x10140000;  // Versatile VIC base
static const uint32_t VIC_IRQSTATUS    = 0x00;
static const uint32_t VIC_INTENABLE    = 0x10;
static const uint32_t VIC_INTENCLEAR   = 0x14;
static const uint32_t VIC_SOFTINT      = 0x18;
static const uint32_t VIC_SOFTINTCLEAR = 0x1C;

void init_interrupts()
{
    for (uint32_t i = 0; i < MAX_IRQS; ++i) {
        s_irq_handlers[i] = nullptr;
    }
    
#if !GXOS_MSVC_STUB
    // Disable all interrupts initially
    volatile uint32_t* vic_intenclear = 
        reinterpret_cast<volatile uint32_t*>(VIC_BASE + VIC_INTENCLEAR);
    *vic_intenclear = 0xFFFFFFFF;
#endif
    
    uart_puts("[IRQ] VIC initialized\n");
}

void enable_irq(uint32_t irq)
{
    if (irq >= 32) return;
    
#if !GXOS_MSVC_STUB
    volatile uint32_t* vic_intenable = 
        reinterpret_cast<volatile uint32_t*>(VIC_BASE + VIC_INTENABLE);
    *vic_intenable = (1 << irq);
#else
    (void)irq;
#endif
}

void disable_irq(uint32_t irq)
{
    if (irq >= 32) return;
    
#if !GXOS_MSVC_STUB
    volatile uint32_t* vic_intenclear = 
        reinterpret_cast<volatile uint32_t*>(VIC_BASE + VIC_INTENCLEAR);
    *vic_intenclear = (1 << irq);
#else
    (void)irq;
#endif
}

void ack_irq(uint32_t irq)
{
    // VIC doesn't require explicit EOI for most interrupts
    // The device itself needs to clear the interrupt source
    (void)irq;
}

void register_irq_handler(uint32_t irq, IrqHandler handler)
{
    if (irq < MAX_IRQS) {
        s_irq_handlers[irq] = handler;
    }
}

} // namespace syscall
} // namespace arm
} // namespace arch
} // namespace kernel
