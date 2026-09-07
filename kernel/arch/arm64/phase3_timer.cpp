#include <stdint.h>

#include "phase3_timer.h"

namespace {

static uint32_t gTimerIrq = 0;
static uint64_t gFrequency = 0;
static uint64_t gInterval = 0;
static volatile uint64_t gCount = 0;

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

static inline void write_timer_value(uint64_t value)
{
    __asm__ volatile("msr cntp_tval_el0, %0" : : "r"(value) : "memory");
}

static inline void write_timer_control(uint64_t value)
{
    __asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(value) : "memory");
}

} // namespace

uint8_t phase3_timer_configure(uint32_t irq, uint32_t interval_us)
{
    if (irq < 16 || irq >= 32 || interval_us == 0) return 0;
    gTimerIrq = irq;
    gFrequency = read_frequency();
    if (gFrequency == 0 || gFrequency > UINT64_MAX / interval_us) return 0;
    gInterval = (gFrequency * interval_us) / UINT64_C(1000000);
    if (gInterval == 0) gInterval = 1;
    gCount = 0;
    write_timer_control(0);
    write_timer_value(gInterval);
    timer_barrier();
    return 1;
}

void phase3_timer_start()
{
    write_timer_value(gInterval);
    write_timer_control(1); // enable, unmasked, non-secure physical timer
    timer_barrier();
}

void phase3_timer_ack_and_rearm()
{
    ++gCount;
    write_timer_value(gInterval);
    write_timer_control(1);
    timer_barrier();
}

void phase3_timer_stop()
{
    write_timer_control(0);
    timer_barrier();
}

uint32_t phase3_timer_irq() { return gTimerIrq; }
uint64_t phase3_timer_frequency() { return gFrequency; }
uint64_t phase3_timer_count() { return gCount; }

