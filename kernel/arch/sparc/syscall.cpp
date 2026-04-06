//
// SPARC v8 (32-bit) System Call Handler Implementation
//
// Handles syscalls, exceptions, and interrupts for SPARC v8.
//
// SPARC trap flow:
// 1. User code executes 'ta 0x10' (or other trap number)
// 2. CPU saves PC/nPC, sets PSR.S=1, jumps to TBR vector
// 3. Trap handler saves registers, calls trap_dispatch
// 4. Handler processes trap
// 5. Trap handler restores registers, executes rett
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/syscall.h"
#include "include/arch/sparc.h"
#include "include/arch/context_switch.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace sparc {
namespace syscall {

namespace {

static SyscallHandler s_handlers[SYS_MAX + 1] = { nullptr };
static const uint32_t MAX_IRQS = 16;
static IrqHandler s_irq_handlers[MAX_IRQS] = { nullptr };

// ================================================================
// Serial output for debugging
// Uses NS16550 UART on Sun4m at 0x71100000
// ================================================================

static const uint32_t UART_BASE = 0x71100000U;

static void serial_putc(char c)
{
#if !GXOS_MSVC_STUB
    volatile uint8_t* lsr = reinterpret_cast<volatile uint8_t*>(UART_BASE + 5);
    volatile uint8_t* thr = reinterpret_cast<volatile uint8_t*>(UART_BASE + 0);
    while ((*lsr & 0x20) == 0) {}  // Wait for THRE
    *thr = static_cast<uint8_t>(c);
#else
    (void)c;
#endif
}

static void serial_puts(const char* str)
{
    if (!str) return;
    while (*str) {
        if (*str == '\n') serial_putc('\r');
        serial_putc(*str++);
    }
}

static void serial_put_hex(uint32_t value)
{
    serial_puts("0x");
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) {
        serial_putc(hex[(value >> i) & 0xF]);
    }
}

static void serial_write(const char* buf, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        serial_putc(buf[i]);
    }
}

// ================================================================
// Default syscall handlers
// ================================================================

static int32_t sys_exit(SyscallArgs* args)
{
    serial_puts("[Syscall] exit(");
    serial_put_hex(args->arg0);
    serial_puts(")\n");
    while (1) { halt(); }
    return 0;
}

static int32_t sys_write(SyscallArgs* args)
{
    int fd = static_cast<int>(args->arg0);
    const char* buf = reinterpret_cast<const char*>(args->arg1);
    uint32_t count = args->arg2;
    
    if (fd == 1 || fd == 2) {
        serial_write(buf, count);
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
        serial_puts("[Debug] ");
        serial_puts(msg);
        serial_puts("\n");
    }
    return SYSCALL_SUCCESS;
}

static int32_t sys_not_implemented(SyscallArgs* args)
{
    serial_puts("[Syscall] Unimplemented: ");
    serial_put_hex(args->syscall_nr);
    serial_puts("\n");
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
    serial_puts("[Syscall] Initializing SPARC v8 syscall subsystem\n");
    
    for (uint32_t i = 0; i <= SYS_MAX; ++i) {
        s_handlers[i] = nullptr;
    }
    
    register_default_handlers();
    init_interrupts();
    
    // Set up TBR (Trap Base Register)
    // The trap table must be 4KB aligned
    // TODO: Set TBR to trap table address
    
    serial_puts("[Syscall] SPARC v8 syscall subsystem initialized\n");
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
// Trap dispatcher
// ================================================================

extern "C" void trap_dispatch(uint32_t trap_type, uint32_t pc, uint32_t npc)
{
    (void)pc;
    (void)npc;
    
    if (trap_type >= 0x11 && trap_type <= 0x1F) {
        // Hardware interrupt (level 1-15)
        handle_interrupt(trap_type - 0x10);
        return;
    }
    
    if (trap_type >= 0x80) {
        // Software trap (syscall)
        handle_syscall();
        return;
    }
    
    switch (trap_type) {
        case TRAP_WINDOW_OVERFLOW:
            handle_window_overflow();
            break;
            
        case TRAP_WINDOW_UNDERFLOW:
            handle_window_underflow();
            break;
            
        case TRAP_DATA_ACCESS:
            {
                // Read fault address from MMU
                // TODO: Get actual fault address
                handle_data_access_exception(0);
            }
            break;
            
        case TRAP_ILLEGAL_INST:
            serial_puts("[Trap] Illegal instruction at ");
            serial_put_hex(pc);
            serial_puts("\n");
            while (1) { halt(); }
            break;
            
        default:
            serial_puts("[Trap] Unhandled type ");
            serial_put_hex(trap_type);
            serial_puts(" at ");
            serial_put_hex(pc);
            serial_puts("\n");
            while (1) { halt(); }
            break;
    }
}

// ================================================================
// Specific handlers
// ================================================================

void handle_syscall()
{
    // TODO: Read syscall arguments from registers
}

void handle_window_overflow()
{
    // Save the window to the stack
    // This is typically done entirely in assembly
    // TODO: Implement window overflow handler
}

void handle_window_underflow()
{
    // Restore window from the stack
    // TODO: Implement window underflow handler
}

void handle_data_access_exception(uint32_t addr)
{
    serial_puts("[DataAccess] Fault at ");
    serial_put_hex(addr);
    serial_puts("\n");
    
    // TODO: Implement page fault handling
    while (1) { halt(); }
}

void handle_interrupt(uint32_t level)
{
    // SPARC has 15 interrupt levels (1-15)
    if (level == 10) {
        // Timer interrupt (typically level 10 on Sun4m)
        context::arch_timer_tick();
    } else if (level < MAX_IRQS && s_irq_handlers[level]) {
        s_irq_handlers[level](level);
    }
    
    slavio_eoi(level);
}

// ================================================================
// SLAVIO interrupt controller
// ================================================================

void init_interrupts()
{
    for (uint32_t i = 0; i < MAX_IRQS; ++i) {
        s_irq_handlers[i] = nullptr;
    }
    
    slavio_init();
    
    serial_puts("[IRQ] SLAVIO initialized\n");
}

void enable_irq(uint32_t irq)
{
    if (irq < 16) {
        slavio_irq_enable(irq);
    }
}

void disable_irq(uint32_t irq)
{
    if (irq < 16) {
        slavio_irq_disable(irq);
    }
}

void ack_irq(uint32_t irq)
{
    slavio_eoi(irq);
}

void register_irq_handler(uint32_t irq, IrqHandler handler)
{
    if (irq < MAX_IRQS) {
        s_irq_handlers[irq] = handler;
    }
}

} // namespace syscall
} // namespace sparc
} // namespace arch
} // namespace kernel
