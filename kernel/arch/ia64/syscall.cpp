//
// IA-64 (Itanium) System Call Handler Implementation
//
// Handles syscalls, exceptions, and interrupts for IA-64.
//
// IA-64 exception flow:
// 1. User code executes break instruction (or epc)
// 2. CPU vectors to IVT entry based on interruption type
// 3. Handler saves minimal state and calls exception_dispatch
// 4. Dispatch routes to appropriate handler
// 5. Handler returns via rfi (return from interruption)
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/syscall.h"
#include "include/arch/ia64.h"
#include "include/arch/context_switch.h"
#include "include/arch/ski_console.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace ia64 {
namespace syscall {

namespace {

static SyscallHandler s_handlers[SYS_MAX + 1] = { nullptr };
static const uint32_t MAX_IRQS = 256;
static IrqHandler s_irq_handlers[MAX_IRQS] = { nullptr };

// ================================================================
// Default syscall handlers
// ================================================================

static int64_t sys_exit(SyscallArgs* args)
{
    ski_console::puts("[Syscall] exit(");
    ski_console::put_hex(args->arg0);
    ski_console::puts(")\n");
    while (1) { halt(); }
    return 0;
}

static int64_t sys_write(SyscallArgs* args)
{
    int fd = static_cast<int>(args->arg0);
    const char* buf = reinterpret_cast<const char*>(args->arg1);
    uint64_t count = args->arg2;
    
    if (fd == 1 || fd == 2) {
        ski_console::write(buf, count);
        return static_cast<int64_t>(count);
    }
    return SYSCALL_ENOSYS;
}

static int64_t sys_getpid(SyscallArgs* /* args */)
{
    return 1;
}

static int64_t sys_yield(SyscallArgs* /* args */)
{
    return SYSCALL_SUCCESS;
}

static int64_t sys_debug(SyscallArgs* args)
{
    const char* msg = reinterpret_cast<const char*>(args->arg0);
    if (msg) {
        ski_console::puts("[Debug] ");
        ski_console::puts(msg);
        ski_console::puts("\n");
    }
    return SYSCALL_SUCCESS;
}

static int64_t sys_not_implemented(SyscallArgs* args)
{
    ski_console::puts("[Syscall] Unimplemented: ");
    ski_console::put_hex(args->syscall_nr);
    ski_console::puts("\n");
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
    ski_console::puts("[Syscall] Initializing IA-64 syscall subsystem\n");
    
    for (uint32_t i = 0; i <= SYS_MAX; ++i) {
        s_handlers[i] = nullptr;
    }
    
    register_default_handlers();
    init_interrupts();
    
    // TODO: Set up IVT (Interruption Vector Table)
    // The IVT base is in IVA (CR.IVA) register
    
    ski_console::puts("[Syscall] IA-64 syscall subsystem initialized\n");
}

bool register_handler(uint64_t syscall_nr, SyscallHandler handler)
{
    if (syscall_nr > SYS_MAX) return false;
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
// Exception dispatcher
// ================================================================

extern "C" void exception_dispatch(uint64_t vector, uint64_t isr)
{
    switch (vector) {
        case IVT_BREAK:
            handle_syscall();
            break;
            
        case IVT_PAGE_NOT_PRES:
        case IVT_INST_ACCESS_R:
        case IVT_DATA_ACCESS_R:
            {
                uint64_t ifa = read_cr(20);  // CR.IFA
                handle_page_fault(ifa, isr);
            }
            break;
            
        case IVT_EXTERNAL_INT:
            handle_external_interrupt();
            break;
            
        default:
            ski_console::puts("[Exception] Unhandled vector: ");
            ski_console::put_hex(vector);
            ski_console::puts(" ISR=");
            ski_console::put_hex(isr);
            ski_console::puts("\n");
            while (1) { halt(); }
            break;
    }
}

// ================================================================
// Specific exception handlers
// ================================================================

void handle_syscall()
{
    // TODO: Read syscall arguments from saved context
    // Syscall number is in r15, args in r32-r37
}

void handle_page_fault(uint64_t ifa, uint64_t isr)
{
    ski_console::puts("[PageFault] at ");
    ski_console::put_hex(ifa);
    ski_console::puts(" ISR=");
    ski_console::put_hex(isr);
    ski_console::puts("\n");
    
    // TODO: Implement page fault handling
    while (1) { halt(); }
}

void handle_external_interrupt()
{
    // TODO: Read interrupt vector from SAPIC
    // Process the interrupt and send EOI
    
    context::arch_timer_tick();
}

void handle_break(uint64_t iim)
{
    // The immediate value in the break instruction
    ski_console::puts("[Break] IIM=");
    ski_console::put_hex(iim);
    ski_console::puts("\n");
}

// ================================================================
// SAPIC (Streamlined Advanced Programmable Interrupt Controller)
// ================================================================

void init_interrupts()
{
    for (uint32_t i = 0; i < MAX_IRQS; ++i) {
        s_irq_handlers[i] = nullptr;
    }
    
    // TODO: Initialize SAPIC
    // - Configure I/O SAPIC for external interrupts
    // - Set up Local SAPIC for timer and IPI
    
    ski_console::puts("[IRQ] SAPIC initialized\n");
}

void enable_irq(uint32_t irq)
{
    // TODO: Unmask interrupt in SAPIC
    (void)irq;
}

void disable_irq(uint32_t irq)
{
    // TODO: Mask interrupt in SAPIC
    (void)irq;
}

void ack_irq(uint32_t irq)
{
    // TODO: Send EOI to SAPIC
    (void)irq;
}

void register_irq_handler(uint32_t irq, IrqHandler handler)
{
    if (irq < MAX_IRQS) {
        s_irq_handlers[irq] = handler;
    }
}

} // namespace syscall
} // namespace ia64
} // namespace arch
} // namespace kernel
