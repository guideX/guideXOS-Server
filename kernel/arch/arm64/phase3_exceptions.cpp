#include <stdint.h>

#include "phase3_timer.h"
#include "../../../aarch64/phase2/phase2_platform.h"
#include "../../../kernel/core/include/kernel/common_scheduler.h"
#if defined(GXOS_AARCH64_EL0_ISOLATION)
#include "phase2_el0.h"
#endif
#if defined(GXOS_AARCH64_PHASE4)
#include "../../../kernel/core/include/kernel/irq_registry.h"
#endif
#if defined(GXOS_AARCH64_SCHEDR1_DIAGNOSTICS)
#include "phase4_diagnostics.h"
#endif

extern "C" uint32_t phase3_irq_acknowledge();
extern "C" void phase3_irq_complete(uint32_t acknowledgement);
extern "C" uint32_t phase3_irq_id(uint32_t acknowledgement);
extern "C" uint8_t phase3_irq_ack_valid(uint32_t acknowledgement);
extern "C" void phase3_serial_print(const char* text);
extern "C" void phase3_serial_hex(uint64_t value);
#if defined(GXOS_AARCH64_PHASE4)
extern "C" void* phase4_irq_dispatch(uint32_t irq, void* frame);
#endif

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

static uint64_t read_spsr()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, spsr_el1" : "=r"(value));
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
    phase3_serial_print(" SPSR=");
    phase3_serial_hex(read_spsr());
    phase3_serial_print(" FAR=");
    phase3_serial_hex(read_far());
    phase3_serial_print(" SP=");
    phase3_serial_hex(read_sp());
    phase3_serial_print("\n[guideXOS] ");
#if defined(GXOS_AARCH64_RPI4_P1)
    phase3_serial_print("AARCH64_P1_ERROR\n");
#elif defined(GXOS_AARCH64_PHASE4)
    phase3_serial_print("AARCH64_PHASE4_ERROR\n");
#else
    phase3_serial_print("AARCH64_PHASE3_ERROR\n");
#endif
    for (;;) __asm__ volatile("wfi");
}

} // namespace

extern "C" uint32_t phase3_exception_count()
{
    return gExceptionCount;
}

extern "C" void* phase3_exception_dispatch(uint64_t* frame, uint64_t vector_class)
{
#if defined(GXOS_AARCH64_EL0_ISOLATION)
    if (vector_class == GXOS_AARCH64_EL0_VECTOR_LOWER_SYNC_A64 ||
        vector_class == GXOS_AARCH64_EL0_VECTOR_LOWER_IRQ_A64) {
        if (vector_class == GXOS_AARCH64_EL0_VECTOR_LOWER_IRQ_A64) {
            const uint32_t acknowledgement = phase3_irq_acknowledge();
            if (!phase3_irq_ack_valid(acknowledgement)) {
                fatal("invalid lower-EL IRQ acknowledgement");
            }
            phase3_irq_complete(acknowledgement);
        }
        gxos_aarch64_el0_context* return_context = nullptr;
        if (!phase2_el0_dispatch_exception(frame, vector_class,
                                          read_esr(), read_elr(),
                                          read_spsr(), read_far(),
                                          &return_context) || !return_context) {
            fatal("unowned or invalid lower-EL exception");
        }
        return return_context;
    }
#endif
    if (vector_class == 1) {
#if defined(GXOS_AARCH64_SCHEDR1_DIAGNOSTICS)
        phase4_diag_irq_entry(frame);
#endif
        const uint32_t acknowledgement = phase3_irq_acknowledge();
        if (!phase3_irq_ack_valid(acknowledgement)) {
#if defined(GXOS_AARCH64_SCHEDR1_DIAGNOSTICS)
            ++g_phase4_diagnostic_record.invalid_ack_count;
#endif
            return nullptr;
        }
        const uint32_t irq = phase3_irq_id(acknowledgement);
#if defined(GXOS_AARCH64_SCHEDR1_DIAGNOSTICS)
        phase4_diag_irq_ack(irq, acknowledgement);
#endif
#if defined(GXOS_AARCH64_PHASE4)
        if (!kernel::irq::has_handler(irq)) {
            kernel::scheduler::note_unexpected_irq(irq);
            phase3_irq_complete(acknowledgement);
            phase3_serial_print("[guideXOS] unexpected IRQ ID=");
            phase3_serial_hex(irq);
            phase3_serial_print("\n");
            return nullptr;
        }
#else
        if (irq != phase3_timer_irq()) {
            kernel::scheduler::note_unexpected_irq(irq);
            phase3_irq_complete(acknowledgement);
            phase3_serial_print("[guideXOS] unexpected IRQ ID=");
            phase3_serial_hex(irq);
            phase3_serial_print("\n");
            fatal("unexpected timer/IRQ");
        }
#endif
        // The Phase-4 path routes timer policy through the common registry;
        // Phase 3 retains its direct, already-validated semantics.
#if defined(GXOS_AARCH64_PHASE4)
        void* next = phase4_irq_dispatch(irq, frame);
#else
        phase3_timer_ack_and_rearm();
        void* next = kernel::scheduler::timer_interrupt(frame);
#endif
#if defined(GXOS_AARCH64_SCHEDR1_DIAGNOSTICS)
        phase4_diag_irq_context(next);
        phase4_diag_checkpoint(PHASE4_DIAG_IRQ_CALLBACK_DONE);
#endif
        phase3_irq_complete(acknowledgement);
#if defined(GXOS_AARCH64_SCHEDR1_DIAGNOSTICS)
        phase4_diag_irq_eoi();
#endif
        return next;
    }

    fatal(vector_class == 0 ? "unexpected synchronous exception" :
                              "unsupported exception vector");
    return 0;
}
