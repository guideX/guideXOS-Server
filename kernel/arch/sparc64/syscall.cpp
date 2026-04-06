//
// SPARC v9 (64-bit) System Call Handler Implementation
//
// Handles syscalls, exceptions, and interrupts for SPARC v9.
//
// SPARC v9 trap flow:
// 1. User code executes 'ta 0x6d' (or other trap number)
// 2. CPU saves TPC/TNPC/TSTATE, increments TL, vectors via TBA
// 3. Trap handler saves registers, calls trap_dispatch
// 4. Handler processes trap
// 5. Trap handler restores registers, executes done/retry
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/syscall.h"
#include "include/arch/sparc64.h"
#include "include/arch/context_switch.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace sparc64 {
namespace syscall {

namespace {

static SyscallHandler s_handlers[SYS_MAX + 1] = { nullptr };
static const uint32_t MAX_IRQS = 64;
static IrqHandler s_irq_handlers[MAX_IRQS] = { nullptr };

// ================================================================
// Serial output for debugging
// Uses EBus 16550-compatible UART on Sun4u at 0x1FFF1300000
// ================================================================

static const uint64_t UART_BASE = 0x1FFF1300000ULL;

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

static void serial_write(const char* buf, uint64_t count)
{
    for (uint64_t i = 0; i < count; ++i) {
        serial_putc(buf[i]);
    }
}

// ================================================================
// Default syscall handlers
// ================================================================

static int64_t sys_exit(SyscallArgs* args)
{
    serial_puts("[Syscall] exit(");
    serial_put_hex(args->arg0);
    serial_puts(")\n");
    while (1) { halt(); }
    return 0;
}

static int64_t sys_write(SyscallArgs* args)
{
    int fd = static_cast<int>(args->arg0);
    const char* buf = reinterpret_cast<const char*>(args->arg1);
    uint64_t count = args->arg2;
    
    if (fd == 1 || fd == 2) {
        serial_write(buf, count);
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
    return static_cast<int64_t>(read_tick());
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

static int64_t sys_not_implemented(SyscallArgs* args)
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
    s_handlers[SYS_GETTIME] = sys_gettime;
    s_handlers[SYS_GXOS_DEBUG] = sys_debug;
}

} // anonymous namespace

// ================================================================
// Public API implementation
// ================================================================

void init()
{
    serial_puts("[Syscall] Initializing SPARC v9 syscall subsystem\n");
    
    for (uint32_t i = 0; i <= SYS_MAX; ++i) {
        s_handlers[i] = nullptr;
    }
    
    register_default_handlers();
    init_interrupts();
    
    // Set up TBA (Trap Base Address)
    // The trap table must be aligned to 32KB (bits [14:0] are trap offset)
    // TODO: Set TBA to trap table address
    
    serial_puts("[Syscall] SPARC v9 syscall subsystem initialized\n");
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

extern "C" void trap_dispatch(uint64_t tt, uint64_t tpc, uint64_t tstate)
{
    (void)tpc;
    (void)tstate;
    
    // Hardware interrupts (0x41-0x4F = PIL 1-15)
    if (tt >= 0x41 && tt <= 0x4F) {
        handle_interrupt(static_cast<uint32_t>(tt - 0x40));
        return;
    }
    
    // Software traps (0x100-0x17F)
    if (tt >= 0x100) {
        handle_syscall();
        return;
    }
    
    // Spill traps (0x80-0xBF)
    if (tt >= TT_SPILL_0_NORMAL && tt < TT_FILL_0_NORMAL) {
        handle_spill_trap();
        return;
    }
    
    // Fill traps (0xC0-0xFF)
    if (tt >= TT_FILL_0_NORMAL) {
        handle_fill_trap();
        return;
    }
    
    switch (tt) {
        case TT_DATA_ACCESS_EXC:
        case TT_DATA_ACCESS_MMU:
            {
                // TODO: Get fault address from MMU
                handle_data_access_exception(0);
            }
            break;
            
        case TT_ILLEGAL_INST:
            serial_puts("[Trap] Illegal instruction at ");
            serial_put_hex(tpc);
            serial_puts("\n");
            while (1) { halt(); }
            break;
            
        default:
            serial_puts("[Trap] Unhandled TT=");
            serial_put_hex(tt);
            serial_puts(" at ");
            serial_put_hex(tpc);
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

void handle_spill_trap()
{
    // Window spill - save register window to stack
    // This is typically handled entirely in assembly
}

void handle_fill_trap()
{
    // Window fill - restore register window from stack
    // This is typically handled entirely in assembly
}

void handle_data_access_exception(uint64_t addr)
{
    serial_puts("[DataAccess] Fault at ");
    serial_put_hex(addr);
    serial_puts("\n");
    
    // TODO: Implement page fault handling
    while (1) { halt(); }
}

void handle_interrupt(uint32_t level)
{
    // SPARC v9 uses PIL (Processor Interrupt Level) for masking
    if (level == 14) {
        // Timer interrupt (typically level 14)
        context::arch_timer_tick();
    } else if (level < MAX_IRQS && s_irq_handlers[level]) {
        s_irq_handlers[level](level);
    }
    
    pci_eoi(level);
}

// ================================================================
// PCI interrupt controller (Psycho/Sabre)
// ================================================================

void init_interrupts()
{
    for (uint32_t i = 0; i < MAX_IRQS; ++i) {
        s_irq_handlers[i] = nullptr;
    }
    
    pci_intctrl_init();
    
    serial_puts("[IRQ] PCI interrupt controller initialized\n");
}

void enable_irq(uint32_t irq)
{
    pci_irq_enable(irq);
}

void disable_irq(uint32_t irq)
{
    pci_irq_disable(irq);
}

void ack_irq(uint32_t irq)
{
    pci_eoi(irq);
}

void register_irq_handler(uint32_t irq, IrqHandler handler)
{
    if (irq < MAX_IRQS) {
        s_irq_handlers[irq] = handler;
    }
}

} // namespace syscall
} // namespace sparc64
} // namespace arch
} // namespace kernel
