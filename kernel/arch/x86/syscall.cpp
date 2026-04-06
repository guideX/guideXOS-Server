//
// x86 (32-bit) System Call Handler Implementation
//
// Handles syscalls, exceptions, and interrupts for x86.
//
// INT 0x80 flow:
// 1. User code executes INT 0x80 instruction
// 2. CPU switches to kernel via IDT entry
// 3. IDT stub saves registers and calls syscall_dispatch()
// 4. syscall_dispatch() determines syscall and calls handler
// 5. Handler processes request and returns result in EAX
// 6. IDT stub restores registers and executes IRET
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/syscall.h"
#include "include/arch/x86.h"
#include "include/arch/context_switch.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace x86 {
namespace syscall {

namespace {

// ================================================================
// Syscall dispatch table
// ================================================================

static SyscallHandler s_handlers[SYS_MAX + 1] = { nullptr };

// ================================================================
// Interrupt handler table
// ================================================================

static const uint32_t MAX_IRQS = 256;
static IrqHandler s_irq_handlers[MAX_IRQS] = { nullptr };

// ================================================================
// Serial output for debugging (COM1)
// ================================================================

static void serial_putc(char c)
{
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

static void serial_put_hex(uint32_t value)
{
    serial_puts("0x");
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) {
        serial_putc(hex[(value >> i) & 0xF]);
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
        for (uint32_t i = 0; i < count && buf[i]; ++i) {
            serial_putc(buf[i]);
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
    serial_puts("[Syscall] Initializing x86 syscall subsystem\n");
    
    for (uint32_t i = 0; i <= SYS_MAX; ++i) {
        s_handlers[i] = nullptr;
    }
    
    register_default_handlers();
    init_interrupts();
    
    // TODO: Set up IDT entry 0x80 for INT 0x80 syscall
    
    serial_puts("[Syscall] x86 syscall subsystem initialized\n");
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

extern "C" void exception_dispatch(uint32_t vector, uint32_t error_code,
                                   uint32_t eip, uint32_t eflags)
{
    (void)eflags;
    
    switch (vector) {
        case 0:
            handle_divide_error();
            break;
        case 6:
            handle_invalid_opcode();
            break;
        case 8:
            handle_double_fault();
            break;
        case 13:
            handle_general_protection(error_code);
            break;
        case 14:
            {
                uint32_t fault_addr;
#if GXOS_MSVC_STUB
                fault_addr = 0;
#else
                fault_addr = read_cr2();
#endif
                handle_page_fault(error_code, fault_addr);
            }
            break;
        case 0x80:
            handle_syscall();
            break;
        default:
            if (vector >= 32 && vector < 256) {
                handle_interrupt(vector);
            } else {
                serial_puts("[Exception] Unhandled vector=");
                serial_put_hex(vector);
                serial_puts(" at EIP=");
                serial_put_hex(eip);
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
    // TODO: Read arguments from saved register context
}

void handle_page_fault(uint32_t error_code, uint32_t fault_addr)
{
    serial_puts("[PageFault] at ");
    serial_put_hex(fault_addr);
    serial_puts(" error=");
    serial_put_hex(error_code);
    serial_puts("\n");
    while (1) { halt(); }
}

void handle_general_protection(uint32_t error_code)
{
    serial_puts("[GPF] error=");
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
    serial_puts("[Exception] Double fault\n");
    while (1) { halt(); }
}

void handle_interrupt(uint32_t vector)
{
    uint32_t irq = vector - 32;
    
    if (irq == 0) {
        context::arch_timer_tick();
    } else if (irq < MAX_IRQS && s_irq_handlers[irq]) {
        s_irq_handlers[irq](irq);
    }
    
    ack_irq(irq);
}

// ================================================================
// PIC interface
// ================================================================

static const uint16_t PIC1_CMD  = 0x20;
static const uint16_t PIC1_DATA = 0x21;
static const uint16_t PIC2_CMD  = 0xA0;
static const uint16_t PIC2_DATA = 0xA1;
static const uint8_t PIC_EOI = 0x20;

void init_interrupts()
{
    for (uint32_t i = 0; i < MAX_IRQS; ++i) {
        s_irq_handlers[i] = nullptr;
    }
    
#if !GXOS_MSVC_STUB
    // Initialize 8259 PIC
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    outb(PIC1_DATA, 0xFB);
    outb(PIC2_DATA, 0xFF);
#endif
    
    serial_puts("[IRQ] PIC initialized\n");
}

void enable_irq(uint32_t irq)
{
    if (irq >= 16) return;
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (irq < 8) ? irq : (irq - 8);
    outb(port, inb(port) & ~(1 << bit));
}

void disable_irq(uint32_t irq)
{
    if (irq >= 16) return;
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (irq < 8) ? irq : (irq - 8);
    outb(port, inb(port) | (1 << bit));
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

} // namespace syscall
} // namespace x86
} // namespace arch
} // namespace kernel
