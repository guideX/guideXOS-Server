#include <stdint.h>

#include "phase3_timer.h"
#include "../../../aarch64/phase2/phase2_platform.h"
#include "../../../kernel/core/include/kernel/common_scheduler.h"

extern "C" uint32_t phase3_irq_acknowledge();
extern "C" void phase3_irq_complete(uint32_t acknowledgement);
extern "C" uint32_t phase3_irq_id(uint32_t acknowledgement);
extern "C" void phase3_serial_print(const char* text);
extern "C" void phase3_serial_hex(uint64_t value);

namespace {

static volatile uint32_t gExceptionCount = 0;

static uint64_t read_esr()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(value));
    return value;
}

static uint64_t read_elr()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, elr_el1" : "=r"(value));
    return value;
}

static uint64_t read_far()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, far_el1" : "=r"(value));
    return value;
}

static uint64_t read_sp()
{
    uint64_t value = 0;
    __asm__ volatile("mov %0, sp" : "=r"(value));
    return value;
}

static void fatal(const char* reason)
{
    ++gExceptionCount;
    phase3_serial_print("[guideXOS] ");
    phase3_serial_print(reason);
    phase3_serial_print(" ESR=");
    phase3_serial_hex(read_esr());
    phase3_serial_print(" ELR=");
    phase3_serial_hex(read_elr());
    phase3_serial_print(" FAR=");
    phase3_serial_hex(read_far());
    phase3_serial_print(" SP=");
    phase3_serial_hex(read_sp());
    phase3_serial_print("\n[guideXOS] AARCH64_PHASE3_ERROR\n");
    for (;;) __asm__ volatile("wfi");
}

} // namespace

extern "C" uint32_t phase3_exception_count()
{
    return gExceptionCount;
}

extern "C" void* phase3_exception_dispatch(uint64_t* frame, uint64_t vector_class)
{
    if (vector_class == 1) {
        const uint32_t acknowledgement = phase3_irq_acknowledge();
        const uint32_t irq = phase3_irq_id(acknowledgement);
        if (irq != phase3_timer_irq()) {
            kernel::scheduler::note_unexpected_irq();
            phase3_irq_complete(acknowledgement);
            phase3_serial_print("[guideXOS] unexpected IRQ ID=");
            phase3_serial_hex(irq);
            phase3_serial_print("\n");
            fatal("unexpected timer/IRQ");
        }
        // Rearm before selecting the next task.  IRQs remain masked until the
        // exception-return path restores the selected task's SPSR.
        phase3_timer_ack_and_rearm();
        void* next = kernel::scheduler::timer_interrupt(frame);
        phase3_irq_complete(acknowledgement);
        return next;
    }

    fatal(vector_class == 0 ? "unexpected synchronous exception" :
                              "unsupported exception vector");
    return 0;
}
