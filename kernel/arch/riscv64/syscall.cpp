//
// RISC-V 64-bit System Call Handler Implementation
//
// Handles syscalls, exceptions, and interrupts for RISC-V 64.
//
// RISC-V trap flow:
// 1. User code executes ecall instruction
// 2. CPU traps to supervisor mode, jumps to stvec
// 3. Trap handler saves registers and calls trap_dispatch
// 4. trap_dispatch routes to appropriate handler based on scause
// 5. Handler returns, trap handler restores registers and sret
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/syscall.h"
#include "include/arch/riscv64.h"
#include "include/arch/context_switch.h"
#include "include/arch/sbi_console.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace riscv64 {
namespace syscall {

namespace {

static SyscallHandler s_handlers[SYS_MAX + 1] = { nullptr };
static const uint32_t MAX_IRQS = 1024;  // PLIC supports up to 1024 sources
static IrqHandler s_irq_handlers[MAX_IRQS] = { nullptr };

// ================================================================
// Default syscall handlers
// ================================================================

static int64_t sys_exit(SyscallArgs* args)
{
    sbi_console::puts("[Syscall] exit(");
    sbi_console::put_hex(args->arg0);
    sbi_console::puts(")\n");
    sbi_console::shutdown();
    while (1) { wait_for_interrupt(); }
    return 0;
}

static int64_t sys_write(SyscallArgs* args)
{
    int fd = static_cast<int>(args->arg0);
    const char* buf = reinterpret_cast<const char*>(args->arg1);
    uint64_t count = args->arg2;
    
    if (fd == 1 || fd == 2) {
        sbi_console::write(buf, count);
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

static int64_t sys_gettime(SyscallArgs* /* args */)
{
    return static_cast<int64_t>(read_time());
}

static int64_t sys_debug(SyscallArgs* args)
{
    const char* msg = reinterpret_cast<const char*>(args->arg0);
    if (msg) {
        sbi_console::puts("[Debug] ");
        sbi_console::puts(msg);
        sbi_console::puts("\n");
    }
    return SYSCALL_SUCCESS;
}

static int64_t sys_not_implemented(SyscallArgs* args)
{
    sbi_console::puts("[Syscall] Unimplemented: ");
    sbi_console::put_hex(args->syscall_nr);
    sbi_console::puts("\n");
    return SYSCALL_ENOSYS;
}

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
    sbi_console::puts("[Syscall] Initializing RISC-V 64 syscall subsystem\n");
    
    for (uint32_t i = 0; i <= SYS_MAX; ++i) {
        s_handlers[i] = nullptr;
    }
    
    register_default_handlers();
    init_interrupts();
    
    // Set up stvec (trap vector)
    // TODO: Set stvec to trap entry point
    
    sbi_console::puts("[Syscall] RISC-V 64 syscall subsystem initialized\n");
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
// Trap dispatcher
// ================================================================

extern "C" void trap_dispatch(uint64_t scause, uint64_t sepc, uint64_t stval)
{
    bool is_interrupt = (scause & CAUSE_INTERRUPT) != 0;
    uint64_t cause = scause & ~CAUSE_INTERRUPT;
    
    if (is_interrupt) {
        handle_interrupt(cause);
        return;
    }
    
    switch (cause) {
        case CAUSE_USER_ECALL:
        case CAUSE_SUPERVISOR_ECALL:
            handle_syscall();
            break;
            
        case CAUSE_FETCH_PAGE_FAULT:
            handle_page_fault(stval, false, true);
            break;
            
        case CAUSE_LOAD_PAGE_FAULT:
            handle_page_fault(stval, false, false);
            break;
            
        case CAUSE_STORE_PAGE_FAULT:
            handle_page_fault(stval, true, false);
            break;
            
        case CAUSE_ILLEGAL_INST:
            handle_illegal_instruction(stval);
            break;
            
        default:
            sbi_console::puts("[Trap] Unhandled cause=");
            sbi_console::put_hex(scause);
            sbi_console::puts(" sepc=");
            sbi_console::put_hex(sepc);
            sbi_console::puts(" stval=");
            sbi_console::put_hex(stval);
            sbi_console::puts("\n");
            while (1) { wait_for_interrupt(); }
            break;
    }
}

// ================================================================
// Specific handlers
// ================================================================

void handle_syscall()
{
    // TODO: Read syscall arguments from saved context
    // Advance sepc past ecall instruction (4 bytes)
#if !GXOS_MSVC_STUB
    uint64_t sepc = read_sepc();
    write_sepc(sepc + 4);
#endif
}

void handle_page_fault(uint64_t stval, bool is_store, bool is_fetch)
{
    sbi_console::puts("[PageFault] ");
    sbi_console::puts(is_fetch ? "Fetch" : (is_store ? "Store" : "Load"));
    sbi_console::puts(" at ");
    sbi_console::put_hex(stval);
    sbi_console::puts("\n");
    
    // TODO: Implement page fault handling
    while (1) { wait_for_interrupt(); }
}

void handle_illegal_instruction(uint64_t stval)
{
    sbi_console::puts("[Exception] Illegal instruction: ");
    sbi_console::put_hex(stval);
    sbi_console::puts("\n");
    while (1) { wait_for_interrupt(); }
}

void handle_interrupt(uint64_t cause)
{
    switch (cause) {
        case IRQ_S_TIMER:
            context::arch_timer_tick();
            break;
            
        case IRQ_S_EXTERNAL:
            // Read PLIC claim register to get interrupt source
            // TODO: Implement PLIC handling
            break;
            
        case IRQ_S_SOFT:
            // Software interrupt (IPI)
            // TODO: Clear SIP.SSIP
            break;
            
        default:
            sbi_console::puts("[IRQ] Unknown interrupt: ");
            sbi_console::put_hex(cause);
            sbi_console::puts("\n");
            break;
    }
}

// ================================================================
// PLIC (Platform-Level Interrupt Controller)
// ================================================================

// QEMU virt PLIC base address
static const uint64_t PLIC_BASE = 0x0C000000ULL;
static const uint64_t PLIC_PRIORITY     = PLIC_BASE + 0x0;
static const uint64_t PLIC_PENDING      = PLIC_BASE + 0x1000;
static const uint64_t PLIC_ENABLE       = PLIC_BASE + 0x2000;
static const uint64_t PLIC_THRESHOLD    = PLIC_BASE + 0x200000;
static const uint64_t PLIC_CLAIM        = PLIC_BASE + 0x200004;

void init_interrupts()
{
    for (uint32_t i = 0; i < MAX_IRQS; ++i) {
        s_irq_handlers[i] = nullptr;
    }
    
#if !GXOS_MSVC_STUB
    // Set threshold to 0 (accept all priorities)
    volatile uint32_t* threshold = reinterpret_cast<volatile uint32_t*>(PLIC_THRESHOLD);
    *threshold = 0;
    
    // Enable supervisor external interrupts
    uint64_t sie = read_sie();
    sie |= (1ULL << 9);  // SEIE - Supervisor External Interrupt Enable
    sie |= (1ULL << 5);  // STIE - Supervisor Timer Interrupt Enable
    write_sie(sie);
#endif
    
    sbi_console::puts("[IRQ] PLIC initialized\n");
}

void enable_irq(uint32_t irq)
{
    if (irq == 0 || irq >= MAX_IRQS) return;
    
#if !GXOS_MSVC_STUB
    // Set priority to non-zero
    volatile uint32_t* priority = reinterpret_cast<volatile uint32_t*>(
        PLIC_PRIORITY + irq * 4);
    *priority = 1;
    
    // Enable in enable register (S-mode, hart 0)
    uint32_t word = irq / 32;
    uint32_t bit = irq % 32;
    volatile uint32_t* enable = reinterpret_cast<volatile uint32_t*>(
        PLIC_ENABLE + 0x80 + word * 4);  // S-mode offset
    *enable |= (1U << bit);
#else
    (void)irq;
#endif
}

void disable_irq(uint32_t irq)
{
    if (irq == 0 || irq >= MAX_IRQS) return;
    
#if !GXOS_MSVC_STUB
    uint32_t word = irq / 32;
    uint32_t bit = irq % 32;
    volatile uint32_t* enable = reinterpret_cast<volatile uint32_t*>(
        PLIC_ENABLE + 0x80 + word * 4);
    *enable &= ~(1U << bit);
#else
    (void)irq;
#endif
}

void ack_irq(uint32_t irq)
{
#if !GXOS_MSVC_STUB
    // Write to complete register
    volatile uint32_t* complete = reinterpret_cast<volatile uint32_t*>(
        PLIC_CLAIM + 0x1000);  // S-mode offset
    *complete = irq;
#else
    (void)irq;
#endif
}

void register_irq_handler(uint32_t irq, IrqHandler handler)
{
    if (irq < MAX_IRQS) {
        s_irq_handlers[irq] = handler;
    }
}

} // namespace syscall
} // namespace riscv64
} // namespace arch
} // namespace kernel
