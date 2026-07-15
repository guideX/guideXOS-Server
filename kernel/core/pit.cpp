//
// PIT (Programmable Interval Timer) Driver - Implementation
//
// Configures the 8253/8254 PIT channel 0 for periodic IRQ0.
// Only compiled on x86/amd64 where port-I/O PIT is available.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/pit.h"
#include "include/kernel/arch.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/process.h"

namespace kernel {
namespace pit {

#if ARCH_HAS_PORT_IO

// PIT I/O ports
static const uint16_t kChannel0Data = 0x40;
static const uint16_t kCommandReg   = 0x43;

// PIT oscillator frequency (~1.193182 MHz)
static const uint32_t kBaseFrequency = 1193182;

static volatile uint64_t s_ticks = 0;
static uint32_t s_hz = 100;

void init(uint32_t hz)
{
    if (hz == 0) hz = 100;
    s_hz = hz;

    uint32_t divisor = kBaseFrequency / hz;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    if (divisor < 1) divisor = 1;

    // Channel 0, lo/hi byte, rate generator (mode 2)
    arch::outb(kCommandReg, 0x34);
    arch::outb(kChannel0Data, static_cast<uint8_t>(divisor & 0xFF));
    arch::outb(kChannel0Data, static_cast<uint8_t>((divisor >> 8) & 0xFF));

    s_ticks = 0;

    serial::puts("[PIT] Timer configured at ~");
    serial::put_hex32(hz);
    serial::puts(" Hz (divisor=0x");
    serial::put_hex32(divisor);
    serial::puts(")\n");
}

void irq_handler()
{
    s_ticks++;
    kernel::process::timer_tick();
}

uint64_t ticks()
{
    return s_ticks;
}

uint64_t ticks_for_nanoseconds(uint64_t nanoseconds, bool* valid)
{
    if (valid != nullptr) *valid = false;
    if (nanoseconds == 0 || s_hz == 0) return 0;

    const uint64_t wholeSeconds = nanoseconds / 1000000000ULL;
    const uint64_t remainder = nanoseconds % 1000000000ULL;
    if (wholeSeconds > 0xFFFFFFFFFFFFFFFFULL / s_hz) return 0;

    uint64_t result = wholeSeconds * s_hz;
    const uint64_t fractional = (remainder * s_hz + 999999999ULL) / 1000000000ULL;
    if (result > 0xFFFFFFFFFFFFFFFFULL - fractional) return 0;
    result += fractional;
    if (result == 0) result = 1;
    if (valid != nullptr) *valid = true;
    return result;
}

#else // !ARCH_HAS_PORT_IO

void     init(uint32_t) { }
void     irq_handler()  { }
uint64_t ticks()        { return 0; }
uint64_t ticks_for_nanoseconds(uint64_t, bool* valid) {
    if (valid != nullptr) *valid = false;
    return 0;
}

#endif // ARCH_HAS_PORT_IO

} // namespace pit
} // namespace kernel
