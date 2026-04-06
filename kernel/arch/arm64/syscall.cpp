// ARM64 System Call Implementation
//
// Handles SVC (Supervisor Call) exceptions from user space.
// Maps syscall numbers to handler functions.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/arch/syscall.h"
#include "include/arch/arm64.h"
#include "include/arch/serial_console.h"

#if defined(_MSC_VER)
#define GXOS_MSVC_STUB 1
#else
#define GXOS_MSVC_STUB 0
#endif

namespace kernel {
namespace arch {
namespace arm64 {
namespace syscall {

// ================================================================
// Internal state
// ================================================================

static SyscallHandler s_handlers[SYS_MAX + 1];
static bool s_initialized = false;

// ================================================================
// Helper functions
// ================================================================

static void memzero(void* dst, size_t len)
{
    uint8_t* p = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0;
    }
}

// ================================================================
// Default syscall handlers (stubs)
// ================================================================

int64_t sys_exit(SyscallContext* ctx)
{
    int exitCode = static_cast<int>(ctx->arg0);
    
    // For now, just halt. Real implementation would terminate the process.
    serial_console::print("Process exiting with code: ");
    // Print exit code (simple decimal)
    char buf[16];
    int i = 0;
    int val = exitCode < 0 ? -exitCode : exitCode;
    if (exitCode < 0) {
        buf[i++] = '-';
    }
    do {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    } while (val > 0 && i < 15);
    buf[i] = '\0';
    // Reverse the number part
    int start = exitCode < 0 ? 1 : 0;
    int end = i - 1;
    while (start < end) {
        char tmp = buf[start];
        buf[start] = buf[end];
        buf[end] = tmp;
        start++;
        end--;
    }
    serial_console::print(buf);
    serial_console::print("\n");
    
    // Halt (should switch to next thread or idle)
    halt();
    
    return E_OK;  // Never reached
}

int64_t sys_read(SyscallContext* ctx)
{
    // int fd, void* buf, size_t count
    (void)ctx;
    // TODO: Implement proper file read
    return E_NOSYS;
}

int64_t sys_write(SyscallContext* ctx)
{
    int fd = static_cast<int>(ctx->arg0);
    const char* buf = reinterpret_cast<const char*>(ctx->arg1);
    size_t count = static_cast<size_t>(ctx->arg2);
    
    // Only support stdout/stderr for now
    if (fd != 1 && fd != 2) {
        return E_BADF;
    }
    
    // Write to serial console
    for (size_t i = 0; i < count; ++i) {
        serial_console::putc(buf[i]);
    }
    
    return static_cast<int64_t>(count);
}

int64_t sys_open(SyscallContext* ctx)
{
    (void)ctx;
    // TODO: Implement file open
    return E_NOSYS;
}

int64_t sys_close(SyscallContext* ctx)
{
    (void)ctx;
    // TODO: Implement file close
    return E_NOSYS;
}

int64_t sys_getpid(SyscallContext* ctx)
{
    (void)ctx;
    // TODO: Return actual PID from process table
    return 1;  // Return kernel PID for now
}

int64_t sys_yield(SyscallContext* ctx)
{
    (void)ctx;
    // TODO: Trigger scheduler to switch to another thread
    return E_OK;
}

int64_t sys_mmap(SyscallContext* ctx)
{
    (void)ctx;
    // TODO: Implement memory mapping
    return E_NOSYS;
}

int64_t sys_munmap(SyscallContext* ctx)
{
    (void)ctx;
    // TODO: Implement memory unmapping
    return E_NOSYS;
}

int64_t sys_gettime(SyscallContext* ctx)
{
    (void)ctx;
    // Return timer ticks
    uint64_t ticks = read_cntpct_el0();
    uint64_t freq = read_cntfrq_el0();
    
    // Convert to seconds (in X0) and nanoseconds (in X1 via fullCtx)
    uint64_t seconds = ticks / freq;
    uint64_t remainder = ticks % freq;
    uint64_t nanoseconds = (remainder * 1000000000ULL) / freq;
    
    // Store nanoseconds in X1 if we have context access
    if (ctx->fullCtx) {
        ctx->fullCtx->x1 = nanoseconds;
    }
    
    return static_cast<int64_t>(seconds);
}

int64_t sys_sleep(SyscallContext* ctx)
{
    uint64_t milliseconds = ctx->arg0;
    
    // Calculate target time
    uint64_t freq = read_cntfrq_el0();
    uint64_t ticks = (milliseconds * freq) / 1000;
    uint64_t target = read_cntpct_el0() + ticks;
    
    // Busy wait (should be replaced with proper scheduler sleep)
    while (read_cntpct_el0() < target) {
        wait_for_interrupt();
    }
    
    return E_OK;
}

int64_t sys_debug_print(SyscallContext* ctx)
{
    const char* msg = reinterpret_cast<const char*>(ctx->arg0);
    
    if (msg) {
        serial_console::print(msg);
    }
    
    return E_OK;
}

int64_t sys_sysinfo(SyscallContext* ctx)
{
    uint64_t info_type = ctx->arg0;
    
    switch (info_type) {
        case 0:  // CPU implementer
            return static_cast<int64_t>((read_midr_el1() >> 24) & 0xFF);
        case 1:  // CPU part number
            return static_cast<int64_t>((read_midr_el1() >> 4) & 0xFFF);
        case 2:  // Timer frequency
            return static_cast<int64_t>(read_cntfrq_el0());
        case 3:  // Current exception level
            return static_cast<int64_t>((read_current_el() >> 2) & 3);
        default:
            return E_INVAL;
    }
}

// Default handler for unimplemented syscalls
static int64_t sys_nosys(SyscallContext* ctx)
{
    (void)ctx;
    return E_NOSYS;
}

// ================================================================
// Syscall initialization
// ================================================================

void init()
{
    if (s_initialized) return;
    
    // Clear all handlers
    memzero(s_handlers, sizeof(s_handlers));
    
    // Set default handler for all
    for (uint64_t i = 0; i <= SYS_MAX; ++i) {
        s_handlers[i] = sys_nosys;
    }
    
    // Register implemented syscalls
    s_handlers[SYS_EXIT] = sys_exit;
    s_handlers[SYS_READ] = sys_read;
    s_handlers[SYS_WRITE] = sys_write;
    s_handlers[SYS_OPEN] = sys_open;
    s_handlers[SYS_CLOSE] = sys_close;
    s_handlers[SYS_GETPID] = sys_getpid;
    s_handlers[SYS_YIELD] = sys_yield;
    s_handlers[SYS_MMAP] = sys_mmap;
    s_handlers[SYS_MUNMAP] = sys_munmap;
    s_handlers[SYS_GETTIME] = sys_gettime;
    s_handlers[SYS_SLEEP] = sys_sleep;
    s_handlers[SYS_DEBUG_PRINT] = sys_debug_print;
    s_handlers[SYS_SYSINFO] = sys_sysinfo;
    
    s_initialized = true;
}

// ================================================================
// Syscall registration
// ================================================================

void register_handler(uint64_t number, SyscallHandler handler)
{
    if (number > SYS_MAX) return;
    
    s_handlers[number] = handler ? handler : sys_nosys;
}

// ================================================================
// Syscall dispatch
// ================================================================

int64_t dispatch(context::FullContext* ctx)
{
    if (!ctx) return E_FAULT;
    
    // Initialize if not done
    if (!s_initialized) {
        init();
    }
    
    // Build syscall context from full context
    SyscallContext sctx;
    sctx.arg0 = ctx->x0;
    sctx.arg1 = ctx->x1;
    sctx.arg2 = ctx->x2;
    sctx.arg3 = ctx->x3;
    sctx.arg4 = ctx->x4;
    sctx.arg5 = ctx->x5;
    sctx.arg6 = ctx->x6;
    sctx.arg7 = ctx->x7;
    sctx.number = ctx->x8;  // Syscall number in X8
    sctx.result = 0;
    sctx.fullCtx = ctx;
    
    // Validate syscall number
    if (sctx.number > SYS_MAX) {
        return E_NOSYS;
    }
    
    // Get handler
    SyscallHandler handler = s_handlers[sctx.number];
    if (!handler) {
        return E_NOSYS;
    }
    
    // Call handler
    int64_t result = handler(&sctx);
    
    // Store result in X0
    ctx->x0 = static_cast<uint64_t>(result);
    
    return result;
}

} // namespace syscall
} // namespace arm64
} // namespace arch
} // namespace kernel

// ================================================================
// C linkage exception handler (called from boot.S)
// ================================================================

extern "C" void syscall_handler(void* context, int type)
{
    (void)type;
    
    kernel::arch::arm64::context::FullContext* ctx = 
        static_cast<kernel::arch::arm64::context::FullContext*>(context);
    
    // Check if this is actually a syscall (EC_SVC_AARCH64)
    uint64_t esr = kernel::arch::arm64::read_esr_el1();
    uint64_t ec = (esr >> 26) & 0x3F;
    
    if (ec == kernel::arch::arm64::EC_SVC_AARCH64) {
        // Handle syscall
        kernel::arch::arm64::syscall::dispatch(ctx);
    } else {
        // Not a syscall, handle as generic exception
        // For now, print debug info and halt
        kernel::arch::arm64::serial_console::print("Unexpected exception from lower EL\n");
        kernel::arch::arm64::halt();
    }
}

// ================================================================
// C linkage exception handler for synchronous exceptions
// ================================================================

extern "C" void exception_handler(void* context, int type)
{
    kernel::arch::arm64::context::FullContext* ctx = 
        static_cast<kernel::arch::arm64::context::FullContext*>(context);
    
    uint64_t esr = kernel::arch::arm64::read_esr_el1();
    uint64_t ec = (esr >> 26) & 0x3F;
    uint64_t far = kernel::arch::arm64::read_far_el1();
    
    kernel::arch::arm64::serial_console::print("Exception: EC=0x");
    // Print EC in hex (simplified)
    char hex[3];
    hex[0] = "0123456789ABCDEF"[(ec >> 4) & 0xF];
    hex[1] = "0123456789ABCDEF"[ec & 0xF];
    hex[2] = '\0';
    kernel::arch::arm64::serial_console::print(hex);
    kernel::arch::arm64::serial_console::print(" Type=");
    char typeChar = '0' + type;
    kernel::arch::arm64::serial_console::putc(typeChar);
    kernel::arch::arm64::serial_console::print("\n");
    
    // Handle based on exception class
    switch (ec) {
        case kernel::arch::arm64::EC_DATA_ABORT:
        case kernel::arch::arm64::EC_DATA_ABORT_LOW:
            kernel::arch::arm64::serial_console::print("Data abort at FAR=0x");
            // Print FAR (simplified hex)
            for (int i = 60; i >= 0; i -= 4) {
                char c = "0123456789ABCDEF"[(far >> i) & 0xF];
                kernel::arch::arm64::serial_console::putc(c);
            }
            kernel::arch::arm64::serial_console::print("\n");
            break;
            
        case kernel::arch::arm64::EC_INST_ABORT:
        case kernel::arch::arm64::EC_INST_ABORT_LOW:
            kernel::arch::arm64::serial_console::print("Instruction abort at FAR=0x");
            for (int i = 60; i >= 0; i -= 4) {
                char c = "0123456789ABCDEF"[(far >> i) & 0xF];
                kernel::arch::arm64::serial_console::putc(c);
            }
            kernel::arch::arm64::serial_console::print("\n");
            break;
            
        case kernel::arch::arm64::EC_SVC_AARCH64:
            // Syscall - should have been handled by syscall_handler
            kernel::arch::arm64::syscall::dispatch(ctx);
            return;
            
        default:
            kernel::arch::arm64::serial_console::print("Unhandled exception\n");
            break;
    }
    
    // For unrecoverable exceptions, halt
    kernel::arch::arm64::halt();
}

// ================================================================
// IRQ handler
// ================================================================

extern "C" void irq_handler(void* context, int type)
{
    (void)context;
    (void)type;
    
    // Acknowledge the interrupt from GIC
    uint32_t irq = kernel::arch::arm64::gic_acknowledge_irq();
    
    if (irq == kernel::arch::arm64::GIC_INTID_SPURIOUS) {
        // Spurious interrupt, ignore
        return;
    }
    
    // Handle based on IRQ number
    switch (irq) {
        case 30:  // Physical timer interrupt
            // Acknowledge timer by writing new compare value
            {
                uint64_t freq = kernel::arch::arm64::read_cntfrq_el0();
                uint64_t interval = freq / 100;  // 100 Hz
                uint64_t current = kernel::arch::arm64::read_cntpct_el0();
                kernel::arch::arm64::write_cntp_cval_el0(current + interval);
            }
            // TODO: Call scheduler tick
            break;
            
        default:
            // Unknown IRQ
            break;
    }
    
    // Signal end of interrupt
    kernel::arch::arm64::gic_end_irq(irq);
}

// ================================================================
// FIQ and SError handlers
// ================================================================

extern "C" void fiq_handler(void* context, int type)
{
    (void)context;
    (void)type;
    
    // FIQ is typically used for secure world or high-priority interrupts
    // For now, just acknowledge and return
    kernel::arch::arm64::serial_console::print("FIQ received\n");
}

extern "C" void serror_handler(void* context, int type)
{
    (void)context;
    (void)type;
    
    // SError is typically a hardware error - fatal
    kernel::arch::arm64::serial_console::print("SError: System error (hardware fault)\n");
    kernel::arch::arm64::halt();
}
