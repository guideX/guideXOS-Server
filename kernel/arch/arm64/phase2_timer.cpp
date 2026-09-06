#include <stdint.h>

#include "phase2_timer.h"

namespace {

static uint32_t gTimerIrq = 0;
static uint64_t gTimerFrequency = 0;
static volatile uint32_t gTimerCount = 0;

static inline void timer_barrier()
{
    __asm__ volatile("dsb sy\n isb" ::: "memory");
}

static inline uint64_t read_frequency()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

static inline void write_physical_timer(uint64_t value)
{
    __asm__ volatile("msr cntp_tval_el0, %0" : : "r"(value) : "memory");
}

static inline void write_physical_control(uint64_t value)
{
    __asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(value) : "memory");
}

static inline uint64_t read_physical_control()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(value));
    return value;
}

static inline uint64_t read_physical_value()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, cntp_tval_el0" : "=r"(value));
    return value;
}

} // namespace

uint8_t phase2_timer_init(uint32_t irq)
{
    if (irq == 0 || irq >= 1024) return 0;
    gTimerIrq = irq;
    gTimerFrequency = read_frequency();
    if (gTimerFrequency == 0) return 0;

    // A bounded 100 ms one-shot is long enough for the exception path and
    // short enough to fit comfortably inside the test harness timeout.
    uint64_t ticks = gTimerFrequency / 10;
    if (ticks == 0) ticks = 1;
    if (ticks > UINT32_MAX) ticks = UINT32_MAX;
    gTimerCount = 0;
    write_physical_control(0);
    write_physical_timer(ticks);
    write_physical_control(1); // enable, unmasked, non-secure physical timer
    timer_barrier();
    return 1;
}

void phase2_timer_disable()
{
    write_physical_control(0);
    timer_barrier();
}

void phase2_timer_note_irq()
{
    ++gTimerCount;
    phase2_timer_disable();
}

uint64_t phase2_timer_frequency() { return gTimerFrequency; }
uint32_t phase2_timer_irq() { return gTimerIrq; }
uint32_t phase2_timer_count() { return gTimerCount; }
uint32_t phase2_timer_control() { return (uint32_t)read_physical_control(); }
uint32_t phase2_timer_value() { return (uint32_t)read_physical_value(); }
