#include <stdint.h>

#include "phase2_exceptions.h"
#include "phase2_gic.h"
#include "phase2_timer.h"

extern "C" void phase2_serial_print(const char* text);
extern "C" void phase2_serial_hex(uint64_t value);
extern "C" uint8_t phase2_vectors[];

namespace {

static volatile uint8_t gSelfTestActive = 0;
static volatile uint8_t gSelfTestPassed = 0;
static volatile uint64_t gSelfTestExpectedElr = 0;
static volatile uint64_t gSelfTestEsr = 0;
static volatile uint64_t gSelfTestElr = 0;

static inline uint64_t read_esr()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(value));
    return value;
}

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

static inline uint64_t read_far()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, far_el1" : "=r"(value));
    return value;
}

static inline void write_elr(uint64_t value)
{
    __asm__ volatile("msr elr_el1, %0" : : "r"(value) : "memory");
}

static void print_fatal(const char* category, uint64_t esr, uint64_t elr, uint64_t spsr, uint64_t far)
{
    phase2_serial_print("[guideXOS] FATAL exception: ");
    phase2_serial_print(category);
    phase2_serial_print(" ESR="); phase2_serial_hex(esr);
    phase2_serial_print(" ELR="); phase2_serial_hex(elr);
    phase2_serial_print(" SPSR="); phase2_serial_hex(spsr);
    phase2_serial_print(" FAR="); phase2_serial_hex(far);
    phase2_serial_print("\n");
    for (;;) __asm__ volatile("wfi");
}

} // namespace

extern "C" void phase2_trigger_break(uint64_t* expected)
    __attribute__((noinline));

extern "C" void phase2_trigger_break(uint64_t* expected)
{
    __asm__ volatile(
        "adr x1, 1f\n"
        "str x1, [%0]\n"
        "1: brk #0x2a\n"
        :
        : "r"(expected)
        : "x1", "memory");
}

extern "C" void phase2_exception_dispatch(uint64_t* frame, uint64_t vector_class)
{
    (void)frame;
    const uint64_t esr = read_esr();
    const uint64_t elr = read_elr();
    const uint64_t spsr = read_spsr();
    const uint64_t far = read_far();

    if (vector_class == 0 || vector_class == 2) {
        const uint64_t ec = (esr >> 26) & 0x3f;
        // BRK carries its imm16 directly in ISS[15:0].
        const uint64_t immediate = esr & 0xffff;
        if (vector_class == 0 && gSelfTestActive && ec == 0x3c && immediate == 0x2a &&
            elr == gSelfTestExpectedElr) {
            gSelfTestEsr = esr;
            gSelfTestElr = elr;
            gSelfTestPassed = 1;
            gSelfTestActive = 0;
            write_elr(elr + 4);
            return;
        }
        print_fatal(vector_class == 2 ? "synchronous exception from SP0" : "unexpected synchronous exception",
                    esr, elr, spsr, far);
    }

    if (vector_class == 1 || vector_class == 3) {
        const uint32_t acknowledgement = phase2_gic_acknowledge();
        const uint32_t irq = phase2_gic_irq_id(acknowledgement);
        if (irq == phase2_timer_irq()) {
            phase2_timer_note_irq();
            phase2_gic_complete(acknowledgement);
            return;
        }
        phase2_serial_print("[guideXOS] FATAL unexpected IRQ ID=");
        phase2_serial_hex(irq);
        phase2_serial_print(" ESR="); phase2_serial_hex(esr);
        phase2_serial_print(" ELR="); phase2_serial_hex(elr);
        phase2_serial_print("\n");
        for (;;) __asm__ volatile("wfi");
    }

    print_fatal("FIQ/SError/lower-EL vector", esr, elr, spsr, far);
}

uint8_t phase2_exception_self_test()
{
    gSelfTestPassed = 0;
    gSelfTestEsr = 0;
    gSelfTestElr = 0;
    gSelfTestExpectedElr = 0;
    gSelfTestActive = 1;
    phase2_trigger_break((uint64_t*)&gSelfTestExpectedElr);
    return gSelfTestPassed && gSelfTestElr == gSelfTestExpectedElr &&
           ((gSelfTestEsr >> 26) & 0x3f) == 0x3c;
}

uint64_t phase2_exception_self_test_esr() { return gSelfTestEsr; }
uint64_t phase2_exception_self_test_elr() { return gSelfTestElr; }
uint64_t phase2_exception_self_test_expected_elr() { return gSelfTestExpectedElr; }
uint64_t phase2_exception_vectors_address()
{
    return (uint64_t)(uintptr_t)phase2_vectors;
}
