// ARM64 implementation of the small common scheduler architecture contract.

#include <stdint.h>

#include "kernel/arch_interface.h"
#include "phase2_gic.h"
#include "../../../aarch64/phase2/phase2_platform.h"

extern "C" void phase3_context_switch(void** old_context, void* new_context);
extern "C" void phase3_thread_start();

namespace kernel {
namespace arch {

namespace {

struct Arm64Context {
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t x29;
    uint64_t x30;
    uint64_t sp;
};

struct Arm64InterruptContext {
    uint64_t x[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

static_assert(sizeof(Arm64Context) == 104, "ARM64 cooperative context layout changed");
static_assert(sizeof(Arm64InterruptContext) <= kInterruptContextWords * sizeof(uint64_t),
              "ARM64 interrupt context exceeds common storage");

static inline uint64_t read_elr()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, elr_el1" : "=r"(value));
    return value;
}

static inline uint64_t read_spsr()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, spsr_el1" : "=r"(value));
    return value;
}

static void zero_bytes(void* destination, uint64_t size)
{
    uint8_t* bytes = static_cast<uint8_t*>(destination);
    for (uint64_t i = 0; i < size; ++i) bytes[i] = 0;
}

} // namespace

interrupt_state_t irq_save()
{
    uint64_t state = 0;
    __asm__ volatile(
        "mrs %0, daif\n"
        "msr daifset, #2\n"
        "isb\n"
        : "=r"(state) : : "memory");
    return state;
}

void irq_restore(interrupt_state_t state)
{
    __asm__ volatile("msr daif, %0\n isb" : : "r"(state) : "memory");
}

void irq_enable()
{
    __asm__ volatile("msr daifclr, #2\n isb" ::: "memory");
}

void irq_disable()
{
    __asm__ volatile("msr daifset, #2\n isb" ::: "memory");
}

void idle()
{
    // Callers have checked runnable work and enabled IRQs before WFI.
    __asm__ volatile("wfi" ::: "memory");
}

void memory_barrier()
{
    __asm__ volatile("dmb ish" ::: "memory");
}

const char* interface_name()
{
    return "ARM64";
}

void* context_init(uint64_t stack_base, uint64_t stack_size,
                   thread_entry_t entry, void* argument)
{
    if (!entry || stack_base == 0 || stack_size < 0x1000 ||
        stack_size > UINT64_MAX - stack_base) return 0;
    const uint64_t stack_end = stack_base + stack_size;
    if (stack_end <= stack_base || stack_end < 0x10) return 0;
    const uint64_t stack_top = stack_end - 0x10;
    if ((stack_top & 0xf) != 0 || stack_top < stack_base + 112) return 0;
    // The switch frame occupies 112 bytes and the saved SP must point past
    // it, where the interrupted caller expects its stack to resume.
    const uint64_t contextAddress = (stack_top - 112) & ~UINT64_C(0xf);
    if (contextAddress < stack_base || contextAddress + sizeof(Arm64Context) > stack_top) return 0;

    Arm64Context* context = reinterpret_cast<Arm64Context*>(
        static_cast<uintptr_t>(contextAddress));
    zero_bytes(context, sizeof(*context));
    context->x19 = reinterpret_cast<uint64_t>(entry);
    context->x20 = reinterpret_cast<uint64_t>(argument);
    context->x30 = reinterpret_cast<uint64_t>(&phase3_thread_start);
    context->sp = contextAddress + 112;
    return context;
}

void context_switch(void** old_context, void* new_context)
{
    if (!old_context || !new_context) return;
    phase3_context_switch(old_context, new_context);
}

bool interrupt_frame_save(void* frame, void* destination)
{
    if (!frame || !destination) return false;
    Arm64InterruptContext* saved = static_cast<Arm64InterruptContext*>(destination);
    const uint64_t* registers = static_cast<const uint64_t*>(frame);
    for (uint32_t i = 0; i < 31; ++i) saved->x[i] = registers[i];
    // phase3_entry.S keeps a 16-byte scratch prefix below the 256-byte
    // register frame, so the interrupted task SP is frame + 272.
    saved->sp = reinterpret_cast<uint64_t>(frame) + 272;
    saved->pc = read_elr();
    saved->pstate = read_spsr();
    return (saved->sp & 0xf) == 0;
}

bool context_to_interrupt(void* context, void* destination)
{
    if (!context || !destination) return false;
    Arm64Context* source = static_cast<Arm64Context*>(context);
    Arm64InterruptContext* saved = static_cast<Arm64InterruptContext*>(destination);
    zero_bytes(saved, kInterruptContextWords * sizeof(uint64_t));
    saved->x[19] = source->x19;
    saved->x[20] = source->x20;
    saved->x[21] = source->x21;
    saved->x[22] = source->x22;
    saved->x[23] = source->x23;
    saved->x[24] = source->x24;
    saved->x[25] = source->x25;
    saved->x[26] = source->x26;
    saved->x[27] = source->x27;
    saved->x[28] = source->x28;
    saved->x[29] = source->x29;
    saved->x[30] = source->x30;
    saved->sp = source->sp;
    saved->pc = source->x30;
    // EL1h with all exception masks clear.  The thread trampoline also
    // enables IRQs, but this makes an exception-return start self-contained.
    saved->pstate = 0x5;
    return (saved->sp & 0xf) == 0 && saved->pc != 0;
}

} // namespace arch
} // namespace kernel

extern "C" uint8_t phase3_irq_controller_init(
    const gxos_aarch64_phase2_platform* platform, uint32_t timer_irq)
{
    return phase2_gic_init(platform, timer_irq);
}

extern "C" uint32_t phase3_irq_acknowledge()
{
    return phase2_gic_acknowledge();
}

extern "C" void phase3_irq_complete(uint32_t acknowledgement)
{
    phase2_gic_complete(acknowledgement);
}

extern "C" uint32_t phase3_irq_id(uint32_t acknowledgement)
{
    return phase2_gic_irq_id(acknowledgement);
}
