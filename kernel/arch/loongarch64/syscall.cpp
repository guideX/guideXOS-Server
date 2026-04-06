//
// LoongArch 64-bit System Call Handler Implementation
//
// Handles syscalls, exceptions, and interrupts for LoongArch64.
// This module is the central dispatcher for all trap-level events.
//
// Exception flow:
// 1. User/kernel code triggers exception (syscall, fault, interrupt)
// 2. CPU saves ERA, PRMD and jumps to EENTRY (see boot.s)
// 3. Assembly stub saves registers and calls exception_dispatch()
// 4. exception_dispatch() determines cause and calls specific handler
// 5. Handler processes event and returns
// 6. Assembly stub restores registers and executes ertn
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/syscall.h"
#include "include/arch/loongarch64.h"
#include "include/arch/context_switch.h"
#include "include/arch/loongarch_console.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace loongarch64 {
namespace syscall {

namespace {

// ================================================================
// Syscall dispatch table
// ================================================================

static SyscallHandler s_handlers[SYS_MAX + 1] = { nullptr };

// ================================================================
// Current syscall arguments (set by exception handler)
// ================================================================

static SyscallArgs s_current_args;

// ================================================================
// Interrupt handler table
// ================================================================

static const uint32_t MAX_IRQS = 64;
static IrqHandler s_irq_handlers[MAX_IRQS] = { nullptr };

// ================================================================
// Default syscall handlers (stubs)
// ================================================================

static int64_t sys_exit(SyscallArgs* args)
{
    int64_t exit_code = static_cast<int64_t>(args->arg0);
    loongarch_console::puts("[Syscall] exit(");
    loongarch_console::put_hex(exit_code);
    loongarch_console::puts(")\n");
    
    // TODO: Actually terminate the process
    // For now, just halt
    while (1) {
        wait_for_interrupt();
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
        loongarch_console::write(buf, count);
        return static_cast<int64_t>(count);
    }
    
    return SYSCALL_ENOSYS;  // Not implemented
}

static int64_t sys_getpid(SyscallArgs* /* args */)
{
    // TODO: Return actual PID from process table
    return 1;  // Placeholder
}

static int64_t sys_yield(SyscallArgs* /* args */)
{
    // TODO: Call scheduler to yield
    // scheduler::yield();
    return SYSCALL_SUCCESS;
}

static int64_t sys_debug(SyscallArgs* args)
{
    // Debug syscall: print a message to console
    const char* msg = reinterpret_cast<const char*>(args->arg0);
    if (msg) {
        loongarch_console::puts("[Debug] ");
        loongarch_console::puts(msg);
        loongarch_console::puts("\n");
    }
    return SYSCALL_SUCCESS;
}

static int64_t sys_gettime(SyscallArgs* /* args */)
{
    // Return timer value as a simple timestamp
    return static_cast<int64_t>(read_tval());
}

static int64_t sys_not_implemented(SyscallArgs* args)
{
    loongarch_console::puts("[Syscall] Unimplemented syscall: ");
    loongarch_console::put_hex(args->syscall_nr);
    loongarch_console::puts("\n");
    return SYSCALL_ENOSYS;
}

// ================================================================
// Register default syscall handlers
// ================================================================

static void register_default_handlers()
{
    // Process management
    s_handlers[SYS_EXIT] = sys_exit;
    s_handlers[SYS_GETPID] = sys_getpid;
    s_handlers[SYS_YIELD] = sys_yield;
    
    // File operations
    s_handlers[SYS_WRITE] = sys_write;
    
    // Time
    s_handlers[SYS_GETTIME] = sys_gettime;
    
    // guideXOS extensions
    s_handlers[SYS_GXOS_DEBUG] = sys_debug;
}

} // anonymous namespace

// ================================================================
// Public API implementation
// ================================================================

void init()
{
    loongarch_console::puts("[Syscall] Initializing syscall subsystem\n");
    
    // Clear all handlers
    for (uint32_t i = 0; i <= SYS_MAX; ++i) {
        s_handlers[i] = nullptr;
    }
    
    // Register default handlers
    register_default_handlers();
    
    // Initialize interrupt controller
    init_interrupts();
    
    loongarch_console::puts("[Syscall] Syscall subsystem initialized\n");
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
// Exception dispatcher (called from assembly)
// ================================================================

extern "C" void exception_dispatch(uint64_t estat, uint64_t era, uint64_t badv)
{
    // Extract exception code from ESTAT
    uint32_t ecode = (estat >> 16) & 0x3F;
    uint32_t esubcode = (estat >> 22) & 0x1FF;
    
    (void)esubcode;  // May be used for specific exception handling
    (void)era;       // Exception return address
    
    switch (ecode) {
        case ECODE_INT:
            // Interrupt
            {
                uint32_t irq_bits = estat & ESTAT_IS_MASK;
                handle_interrupt(irq_bits);
            }
            break;
            
        case ECODE_PIL:
            // Page Invalid (Load)
            handle_page_fault_load(badv);
            break;
            
        case ECODE_PIS:
            // Page Invalid (Store)
            handle_page_fault_store(badv);
            break;
            
        case ECODE_PIF:
            // Page Invalid (Fetch)
            handle_page_fault_fetch(badv);
            break;
            
        case ECODE_SYS:
            // System call
            handle_syscall();
            break;
            
        case ECODE_BRK:
            // Breakpoint
            loongarch_console::puts("[Exception] Breakpoint at ");
            loongarch_console::put_hex(era);
            loongarch_console::puts("\n");
            // TODO: Handle breakpoint (debugger integration)
            break;
            
        case ECODE_INE:
            // Instruction Not Exist (illegal instruction)
            loongarch_console::puts("[Exception] Illegal instruction at ");
            loongarch_console::put_hex(era);
            loongarch_console::puts("\n");
            // TODO: Terminate process or send signal
            break;
            
        case ECODE_ALE:
            // Address Alignment Error
            loongarch_console::puts("[Exception] Alignment error at ");
            loongarch_console::put_hex(era);
            loongarch_console::puts(" badv=");
            loongarch_console::put_hex(badv);
            loongarch_console::puts("\n");
            // TODO: Handle or terminate
            break;
            
        default:
            loongarch_console::puts("[Exception] Unhandled exception ecode=");
            loongarch_console::put_hex32(ecode);
            loongarch_console::puts(" at ");
            loongarch_console::put_hex(era);
            loongarch_console::puts("\n");
            // Halt on unhandled exception
            while (1) {
                wait_for_interrupt();
            }
            break;
    }
}

// ================================================================
// Specific exception handlers
// ================================================================

void handle_syscall()
{
    // Get syscall arguments from registers
    // In a real implementation, these would be read from the saved
    // context on the exception stack.
    //
    // TODO: Access saved register context properly
    // For now, use the current args structure (set by assembly stub)
    
#if GXOS_MSVC_STUB
    // Stub for MSVC
    (void)s_current_args;
#else
    // Read syscall number from $a7 and arguments from $a0-$a6
    // This would be done from the saved context
    
    // Dispatch the syscall
    int64_t result = dispatch(&s_current_args);
    
    // Store result in $a0 (in saved context)
    // The assembly stub will restore this when returning
    s_current_args.arg0 = static_cast<uint64_t>(result);
    
    // Advance ERA past the syscall instruction (4 bytes)
    uint64_t era = read_era();
    write_era(era + 4);
#endif
}

void handle_page_fault_load(uint64_t badv)
{
    loongarch_console::puts("[PageFault] Load fault at ");
    loongarch_console::put_hex(badv);
    loongarch_console::puts("\n");
    
    // TODO: Implement page fault handling
    // 1. Check if the address is valid for this process
    // 2. If valid, allocate a page and map it
    // 3. If invalid, send SIGSEGV or terminate
    
    // For now, just halt
    while (1) {
        wait_for_interrupt();
    }
}

void handle_page_fault_store(uint64_t badv)
{
    loongarch_console::puts("[PageFault] Store fault at ");
    loongarch_console::put_hex(badv);
    loongarch_console::puts("\n");
    
    // TODO: Implement page fault handling (similar to load)
    
    while (1) {
        wait_for_interrupt();
    }
}

void handle_page_fault_fetch(uint64_t badv)
{
    loongarch_console::puts("[PageFault] Fetch fault at ");
    loongarch_console::put_hex(badv);
    loongarch_console::puts("\n");
    
    // TODO: Implement page fault handling (similar to load)
    
    while (1) {
        wait_for_interrupt();
    }
}

void handle_interrupt(uint64_t irq_bits)
{
    // Process all pending interrupts
    for (uint32_t irq = 0; irq < MAX_IRQS && irq_bits != 0; ++irq) {
        if (irq_bits & (1ULL << irq)) {
            // Clear this bit
            irq_bits &= ~(1ULL << irq);
            
            // Check for timer interrupt (typically IRQ 11 for TI)
            if (irq == 11) {
                context::arch_timer_tick();
                continue;
            }
            
            // Call registered handler
            if (s_irq_handlers[irq]) {
                s_irq_handlers[irq](irq);
            }
            
            // Acknowledge the interrupt
            ack_irq(irq);
        }
    }
}

// ================================================================
// Interrupt controller interface
// ================================================================

void init_interrupts()
{
    // Clear all interrupt handlers
    for (uint32_t i = 0; i < MAX_IRQS; ++i) {
        s_irq_handlers[i] = nullptr;
    }
    
    // Configure exception control
    // ECFG CSR controls which interrupts are enabled
    // Bit layout: [12:0] = interrupt enables (IPI, TI, PMC, HWI0-9)
    //
    // Enable timer interrupt (TI, bit 11)
    uint64_t ecfg = read_csr(CSR_ECFG);
    ecfg |= (1ULL << 11);  // Enable timer interrupt
    write_csr(CSR_ECFG, ecfg);
    
    // On QEMU virt, additional interrupt configuration may be needed
    // for the Extended I/O Interrupt Controller (EXTIOI)
    //
    // TODO: Initialize EXTIOI for external device interrupts
    
    loongarch_console::puts("[IRQ] Interrupt controller initialized\n");
}

void enable_irq(uint32_t irq)
{
    if (irq >= MAX_IRQS) return;
    
    // For internal interrupts (0-12), modify ECFG
    if (irq < 13) {
        uint64_t ecfg = read_csr(CSR_ECFG);
        ecfg |= (1ULL << irq);
        write_csr(CSR_ECFG, ecfg);
    }
    // For external interrupts, configure EXTIOI
    // TODO: EXTIOI support
}

void disable_irq(uint32_t irq)
{
    if (irq >= MAX_IRQS) return;
    
    if (irq < 13) {
        uint64_t ecfg = read_csr(CSR_ECFG);
        ecfg &= ~(1ULL << irq);
        write_csr(CSR_ECFG, ecfg);
    }
    // TODO: EXTIOI support
}

void ack_irq(uint32_t irq)
{
    (void)irq;
    // Most interrupts are acknowledged by handling the cause
    // Timer interrupt is cleared via TICLR CSR
    // External interrupts may need EXTIOI acknowledgment
}

void register_irq_handler(uint32_t irq, IrqHandler handler)
{
    if (irq < MAX_IRQS) {
        s_irq_handlers[irq] = handler;
    }
}

} // namespace syscall
} // namespace loongarch64
} // namespace arch
} // namespace kernel
