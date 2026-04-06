//
// MIPS64 System Call Handler Implementation
//
// Handles syscalls, exceptions, and interrupts for MIPS64.
//
// MIPS64 trap flow:
// 1. User code executes syscall instruction
// 2. CPU traps to kernel mode, jumps to general exception vector (0x180)
// 3. Exception handler saves context and calls trap_dispatch
// 4. trap_dispatch examines Cause register ExcCode and routes to handler
// 5. Handler returns, exception handler restores context and eret
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/syscall.h"
#include "include/arch/mips64.h"
#include "include/arch/context_switch.h"
#include "include/arch/serial_console.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace mips64 {
namespace syscall {

namespace {

static SyscallHandler s_handlers[SYS_MAX + 1] = { nullptr };
static const uint32_t MAX_IRQS = 8;  // MIPS has 8 interrupt lines (IP[7:0])
static IrqHandler s_irq_handlers[MAX_IRQS] = { nullptr };

// ================================================================
// Default syscall handlers
// ================================================================

static int64_t sys_exit(SyscallArgs* args)
{
    serial_console::puts("[Syscall] exit(");
    serial_console::put_hex(args->arg0);
    serial_console::puts(")\n");
    
    // Halt the CPU
    while (1) { wait_for_interrupt(); }
    return 0;
}

static int64_t sys_write(SyscallArgs* args)
{
    int fd = static_cast<int>(args->arg0);
    const char* buf = reinterpret_cast<const char*>(args->arg1);
    uint64_t count = args->arg2;
    
    // Only support stdout (1) and stderr (2) to serial console
    if (fd == 1 || fd == 2) {
        serial_console::write(buf, count);
        return static_cast<int64_t>(count);
    }
    return SYSCALL_ENOSYS;
}

static int64_t sys_getpid(SyscallArgs* /* args */)
{
    // Stub: return PID 1
    return 1;
}

static int64_t sys_yield(SyscallArgs* /* args */)
{
    // Stub: no-op, scheduler would handle this
    return SYSCALL_SUCCESS;
}

static int64_t sys_gettime(SyscallArgs* /* args */)
{
    // Return Count register value as time
    return static_cast<int64_t>(read_count());
}

static int64_t sys_debug(SyscallArgs* args)
{
    const char* msg = reinterpret_cast<const char*>(args->arg0);
    if (msg) {
        serial_console::puts("[Debug] ");
        serial_console::puts(msg);
        serial_console::puts("\n");
    }
    return SYSCALL_SUCCESS;
}

static int64_t sys_not_implemented(SyscallArgs* args)
{
    serial_console::puts("[Syscall] Unimplemented: ");
    serial_console::put_hex(args->syscall_nr);
    serial_console::puts("\n");
    return SYSCALL_ENOSYS;
}

static void register_default_handlers()
{
    // Initialize all handlers to not_implemented
    for (uint64_t i = 0; i <= SYS_MAX; ++i) {
        s_handlers[i] = sys_not_implemented;
    }
    
    // Register implemented handlers
    s_handlers[SYS_EXIT] = sys_exit;
    s_handlers[SYS_WRITE] = sys_write;
    s_handlers[SYS_GETPID] = sys_getpid;
    s_handlers[SYS_YIELD] = sys_yield;
    s_handlers[SYS_GETTIME] = sys_gettime;
    s_handlers[SYS_GXOS_DEBUG] = sys_debug;
}

// ================================================================
// Exception handlers
// ================================================================

static void handle_interrupt(context::FullContext* ctx)
{
    // Get pending interrupts from Cause register IP field
    uint64_t cause = ctx->cause;
    uint64_t status = ctx->status;
    
    // IP bits are [15:8] in both Cause and Status
    // Interrupts are pending if Cause.IP & Status.IM
    uint64_t pending = (cause >> 8) & (status >> 8) & 0xFF;
    
    if (pending == 0) {
        serial_console::puts("[IRQ] Spurious interrupt\n");
        return;
    }
    
    // Handle each pending interrupt (lowest number first)
    for (uint32_t i = 0; i < MAX_IRQS; ++i) {
        if (pending & (1ULL << i)) {
            // Timer interrupt is IP7 (bit 7)
            if (i == 7) {
                serial_console::puts("[IRQ] Timer interrupt\n");
                // Clear timer interrupt by writing to Compare
                write_compare(read_count() + 0x1000000);
            }
            
            // Call registered handler if any
            if (s_irq_handlers[i]) {
                s_irq_handlers[i](i);
            }
        }
    }
}

static void handle_syscall(context::FullContext* ctx)
{
    // Build syscall args from saved context
    SyscallArgs args;
    args.syscall_nr = ctx->v0;      // Syscall number in $v0
    args.arg0 = ctx->a0;            // Arguments in $a0-$a7
    args.arg1 = ctx->a1;
    args.arg2 = ctx->a2;
    args.arg3 = ctx->a3;
    args.arg4 = ctx->a4;
    args.arg5 = ctx->a5;
    args.arg6 = ctx->a6;
    args.arg7 = ctx->a7;
    
    // Dispatch to handler
    int64_t result;
    if (args.syscall_nr <= SYS_MAX && s_handlers[args.syscall_nr]) {
        result = s_handlers[args.syscall_nr](&args);
    } else {
        result = SYSCALL_ENOSYS;
    }
    
    // Return value in $v0, error flag in $a3
    ctx->v0 = static_cast<uint64_t>(result);
    ctx->a3 = (result < 0) ? 1 : 0;
    
    // Advance EPC past the syscall instruction (4 bytes)
    ctx->epc += 4;
}

static void handle_tlb_exception(context::FullContext* ctx, const char* type)
{
    uint64_t badvaddr = ctx->badvaddr;
    
    serial_console::puts("[TLB] ");
    serial_console::puts(type);
    serial_console::puts(" at ");
    serial_console::put_hex(badvaddr);
    serial_console::puts(", EPC=");
    serial_console::put_hex(ctx->epc);
    serial_console::puts("\n");
    
    // For now, just halt on TLB exceptions
    // A real implementation would handle page faults here
    serial_console::puts("[TLB] Unhandled - halting\n");
    while (1) { wait_for_interrupt(); }
}

static void handle_address_error(context::FullContext* ctx, bool is_load)
{
    uint64_t badvaddr = ctx->badvaddr;
    
    serial_console::puts("[AddrErr] ");
    serial_console::puts(is_load ? "Load" : "Store");
    serial_console::puts(" at ");
    serial_console::put_hex(badvaddr);
    serial_console::puts(", EPC=");
    serial_console::put_hex(ctx->epc);
    serial_console::puts("\n");
    
    // Fatal error - halt
    while (1) { wait_for_interrupt(); }
}

static void handle_bus_error(context::FullContext* ctx, bool is_fetch)
{
    serial_console::puts("[BusErr] ");
    serial_console::puts(is_fetch ? "Instruction" : "Data");
    serial_console::puts(" fetch at EPC=");
    serial_console::put_hex(ctx->epc);
    serial_console::puts("\n");
    
    // Fatal error - halt
    while (1) { wait_for_interrupt(); }
}

static void handle_breakpoint(context::FullContext* ctx)
{
    serial_console::puts("[Break] Breakpoint at EPC=");
    serial_console::put_hex(ctx->epc);
    serial_console::puts("\n");
    
    // Skip the break instruction
    ctx->epc += 4;
}

static void handle_reserved_instruction(context::FullContext* ctx)
{
    serial_console::puts("[RI] Reserved instruction at EPC=");
    serial_console::put_hex(ctx->epc);
    serial_console::puts("\n");
    
    // Fatal error - halt
    while (1) { wait_for_interrupt(); }
}

static void handle_coprocessor_unusable(context::FullContext* ctx)
{
    // CE field in Cause tells which coprocessor
    uint64_t ce = (ctx->cause >> 28) & 0x3;
    
    serial_console::puts("[CpU] Coprocessor ");
    serial_console::put_hex(ce);
    serial_console::puts(" unusable at EPC=");
    serial_console::put_hex(ctx->epc);
    serial_console::puts("\n");
    
    // Could enable FPU here if ce == 1
    // For now, halt
    while (1) { wait_for_interrupt(); }
}

static void handle_overflow(context::FullContext* ctx)
{
    serial_console::puts("[Ovf] Arithmetic overflow at EPC=");
    serial_console::put_hex(ctx->epc);
    serial_console::puts("\n");
    
    // Fatal error - halt
    while (1) { wait_for_interrupt(); }
}

static void handle_trap(context::FullContext* ctx)
{
    serial_console::puts("[Trap] Trap instruction at EPC=");
    serial_console::put_hex(ctx->epc);
    serial_console::puts("\n");
    
    // Skip the trap instruction
    ctx->epc += 4;
}

} // anonymous namespace

// ================================================================
// Public interface
// ================================================================

void init()
{
    serial_console::puts("[MIPS64] Initializing syscall/exception handling\n");
    
    register_default_handlers();
    interrupt_init();
    
    serial_console::puts("[MIPS64] Syscall/exception init complete\n");
}

void register_handler(uint64_t syscall_nr, SyscallHandler handler)
{
    if (syscall_nr <= SYS_MAX) {
        s_handlers[syscall_nr] = handler;
    }
}

void register_irq_handler(uint32_t irq, IrqHandler handler)
{
    if (irq < MAX_IRQS) {
        s_irq_handlers[irq] = handler;
    }
}

void enable_irq(uint32_t irq)
{
    if (irq >= MAX_IRQS) return;
    
    // Set corresponding bit in Status.IM
    uint64_t status = read_status();
    status |= (1ULL << (8 + irq));
    write_status(status);
}

void disable_irq(uint32_t irq)
{
    if (irq >= MAX_IRQS) return;
    
    // Clear corresponding bit in Status.IM
    uint64_t status = read_status();
    status &= ~(1ULL << (8 + irq));
    write_status(status);
}

void set_timer(uint64_t ticks)
{
    // Set Compare register for timer interrupt
    // Timer interrupt fires when Count == Compare
    write_compare(read_count() + ticks);
}

uint64_t get_timer_count()
{
    return read_count();
}

void interrupt_init()
{
    // Enable all interrupt mask bits in Status
    uint64_t status = read_status();
    status |= (0xFFULL << 8);  // IM[7:0]
    write_status(status);
    
    // Set up initial timer compare (far future)
    write_compare(0xFFFFFFFFULL);
    
    serial_console::puts("[MIPS64] Interrupts configured\n");
}

// ================================================================
// Trap dispatcher (called from boot.s)
// ================================================================

extern "C" void trap_dispatch(void* context_ptr)
{
    context::FullContext* ctx = static_cast<context::FullContext*>(context_ptr);
    
    // Extract exception code from Cause register
    uint64_t cause = ctx->cause;
    uint64_t exccode = (cause >> 2) & 0x1F;  // ExcCode is bits [6:2]
    
    switch (exccode) {
        case TRAP_INTERRUPT:
            handle_interrupt(ctx);
            break;
            
        case TRAP_TLB_MOD:
            handle_tlb_exception(ctx, "Modification");
            break;
            
        case TRAP_TLB_LOAD:
            handle_tlb_exception(ctx, "Load miss");
            break;
            
        case TRAP_TLB_STORE:
            handle_tlb_exception(ctx, "Store miss");
            break;
            
        case TRAP_ADDR_LOAD:
            handle_address_error(ctx, true);
            break;
            
        case TRAP_ADDR_STORE:
            handle_address_error(ctx, false);
            break;
            
        case TRAP_BUS_FETCH:
            handle_bus_error(ctx, true);
            break;
            
        case TRAP_BUS_DATA:
            handle_bus_error(ctx, false);
            break;
            
        case TRAP_SYSCALL:
            handle_syscall(ctx);
            break;
            
        case TRAP_BREAKPOINT:
            handle_breakpoint(ctx);
            break;
            
        case TRAP_RESERVED_INST:
            handle_reserved_instruction(ctx);
            break;
            
        case TRAP_COP_UNUSABLE:
            handle_coprocessor_unusable(ctx);
            break;
            
        case TRAP_OVERFLOW:
            handle_overflow(ctx);
            break;
            
        case TRAP_TRAP:
            handle_trap(ctx);
            break;
            
        default:
            serial_console::puts("[Trap] Unknown exception code: ");
            serial_console::put_hex(exccode);
            serial_console::puts(" at EPC=");
            serial_console::put_hex(ctx->epc);
            serial_console::puts("\n");
            while (1) { wait_for_interrupt(); }
    }
}

// ================================================================
// TLB refill handlers
// ================================================================

extern "C" void trap_tlb_refill()
{
    serial_console::puts("[TLB] 32-bit TLB refill\n");
    
    // Get the faulting address
    uint64_t badvaddr = read_badvaddr();
    serial_console::puts("  BadVAddr: ");
    serial_console::put_hex(badvaddr);
    serial_console::puts("\n");
    
    // For now, just halt
    // A real implementation would look up the page table and fill TLB
    while (1) { wait_for_interrupt(); }
}

extern "C" void trap_xtlb_refill()
{
    serial_console::puts("[TLB] 64-bit XTLB refill\n");
    
    // Get the faulting address
    uint64_t badvaddr = read_badvaddr();
    serial_console::puts("  BadVAddr: ");
    serial_console::put_hex(badvaddr);
    serial_console::puts("\n");
    
    // For now, just halt
    // A real implementation would look up the page table and fill TLB
    while (1) { wait_for_interrupt(); }
}

// ================================================================
// Cache error handler
// ================================================================

extern "C" void trap_cache_error()
{
    serial_console::puts("[Cache] Cache error - FATAL\n");
    
    // Cache errors are usually unrecoverable
    while (1) { wait_for_interrupt(); }
}

} // namespace syscall
} // namespace mips64
} // namespace arch
} // namespace kernel
