//
// AMD64 voluntary context-switch support.
//
// This code implements only an ordinary Microsoft x64 ABI function-call
// switch between kernel contexts. It does not save an interrupt frame and
// is not a preemptive scheduler.
//

#include "include/arch/context_switch.h"
#include "include/arch/amd64.h"

#if defined(GXOS_BARE_METAL) && (defined(__SSE__) || defined(__SSE2__) || defined(__MMX__))
#error "AMD64 voluntary contexts require the freestanding no-SIMD compiler contract"
#endif

namespace kernel {
namespace arch {
namespace amd64 {
namespace context {

extern "C" void thread_entry_trampoline();
extern "C" void thread_returned();

SwitchContext* init_context(uint64_t stack_base, uint64_t stack_size,
                            void (*entry_point)(void*), void* arg)
{
    if (!entry_point || stack_size < sizeof(SwitchContext) + 40u ||
        stack_base > ~static_cast<uint64_t>(0) - stack_size) {
        return nullptr;
    }

    uint64_t stack_top = (stack_base + stack_size) & ~static_cast<uint64_t>(0xFULL);
    const uint64_t initial_stack_bytes = 40u; // fake return address + 32-byte shadow space
    if (stack_top < stack_base || stack_top - stack_base < sizeof(SwitchContext) + initial_stack_bytes) {
        return nullptr;
    }

    const uint64_t context_address = stack_top - sizeof(SwitchContext) - initial_stack_bytes;
    SwitchContext* ctx = reinterpret_cast<SwitchContext*>(context_address);
    ctx->rbx = 0;
    ctx->rbp = 0;
    ctx->rdi = 0;
    ctx->rsi = 0;
    ctx->r12 = reinterpret_cast<uint64_t>(entry_point);
    ctx->r13 = reinterpret_cast<uint64_t>(arg);
    ctx->r14 = 0;
    ctx->r15 = 0;
    ctx->rsp = stack_top - initial_stack_bytes;
    ctx->rip = reinterpret_cast<uint64_t>(&thread_entry_trampoline);

    // A returned task lands here. Reaping is intentionally not implemented;
    // the scheduler must own the stack and reclaim it from another context.
    *reinterpret_cast<uint64_t*>(ctx->rsp) = reinterpret_cast<uint64_t>(&thread_returned);
    return ctx;
}

void arch_thread_create(ArchThreadData* data, uint64_t stack_base, uint64_t stack_size,
                        void (*entry)(void*), void* arg)
{
    if (!data) return;

    data->kernel_stack_base = stack_base;
    data->kernel_stack_size = stack_size;
    data->context = init_context(stack_base, stack_size, entry, arg);
    data->kernel_sp = data->context ? data->context->rsp : 0;
    data->fpu_state = nullptr;
    data->fs_base = 0;
    data->gs_base = 0;
    data->cr3 = data->context ? read_cr3() : 0;
    data->iopb_offset = 0;
}

void arch_thread_destroy(ArchThreadData* data)
{
    if (!data) return;

    // This clears metadata only. The caller owns the stack and must not call
    // this while the context is executing on that stack.
    data->context = nullptr;
    data->kernel_stack_base = 0;
    data->kernel_stack_size = 0;
    data->kernel_sp = 0;
    data->fpu_state = nullptr;
    data->fs_base = 0;
    data->gs_base = 0;
    data->cr3 = 0;
    data->iopb_offset = 0;
}

void arch_switch_to(ArchThreadData* current, ArchThreadData* next)
{
    if (!current || !next || current == next || !current->context || !next->context) {
        return;
    }

    // Both contexts must execute in mappings that cover the switch code and
    // both kernel stacks. This primitive does not create or validate CR3s.
    if (current->cr3 != next->cr3 && next->cr3 != 0) {
        write_cr3(next->cr3);
    }
    if (current->fs_base != next->fs_base) {
        set_fs_base(next->fs_base);
    }

    switch_context(&current->context, next->context);
}

void arch_timer_tick()
{
    // Timer-driven interrupt-frame scheduling is not implemented here.
}

void arch_set_timer(uint64_t ticks)
{
    // The active freestanding timer remains the PIT timekeeping path.
    (void)ticks;
}

} // namespace context
} // namespace amd64
} // namespace arch
} // namespace kernel
