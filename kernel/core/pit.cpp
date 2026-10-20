//
// PIT (Programmable Interval Timer) Driver - Implementation
//
// Configures the 8253/8254 PIT channel 0 for periodic IRQ0.
// Only compiled on x86/amd64 where port-I/O PIT is available.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/pit.h"
#include "include/kernel/time.h"
#include "include/kernel/arch.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace pit {

#if ARCH_HAS_PORT_IO

// PIT I/O ports
static const uint16_t kChannel0Data = 0x40;
static const uint16_t kCommandReg   = 0x43;

// PIT oscillator frequency (~1.193182 MHz)
static const uint32_t kBaseFrequency = 1193182;

static volatile uint64_t s_ticks = 0;
static volatile bool s_heartbeatSeen = false;
static uint32_t s_divisor = 0;
static bool s_initialized = false;
static uint64_t s_monotonic_base_nanoseconds = 0;

void init(uint32_t hz)
{
    uint64_t previous_nanoseconds = 0;
    uint32_t previous_resolution_nanoseconds = 0;
    bool preserve_monotonic = get_monotonic_nanoseconds(
        previous_nanoseconds, previous_resolution_nanoseconds);
    if (hz == 0) hz = 100;

    uint32_t divisor = kBaseFrequency / hz;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    if (divisor < 1) divisor = 1;

    s_monotonic_base_nanoseconds = preserve_monotonic
        ? previous_nanoseconds : 0u;
    s_divisor = divisor;
    s_initialized = false;

    // Channel 0, lo/hi byte, rate generator (mode 2)
    arch::outb(kCommandReg, 0x34);
    arch::outb(kChannel0Data, static_cast<uint8_t>(divisor & 0xFF));
    arch::outb(kChannel0Data, static_cast<uint8_t>((divisor >> 8) & 0xFF));

    s_ticks = 0;
    s_heartbeatSeen = false;
    s_initialized = true;

    serial::puts("[PIT] Timer configured at ~");
    serial::put_hex32(hz);
    serial::puts(" Hz (divisor=0x");
    serial::put_hex32(divisor);
    serial::puts(")\n");
}

void irq_handler()
{
    if (s_ticks != UINT64_MAX) ++s_ticks;
    s_heartbeatSeen = true;
    if ((s_ticks % 100u) == 0u) {
        serial::puts("[KERNEL-HEARTBEAT] ticks=");
        serial::put_hex64(s_ticks);
        serial::puts(" source=PIT_IRQ0\n");
    }
}

uint64_t ticks()
{
    return s_ticks;
}

bool get_monotonic_nanoseconds(uint64_t& nanoseconds,
                               uint32_t& resolution_nanoseconds)
{
    if (!s_initialized || !s_heartbeatSeen || s_divisor == 0u) return false;
    const uint64_t tick_count = s_ticks;
    return time::pit_ticks_to_monotonic_nanoseconds(
        tick_count, s_divisor, s_monotonic_base_nanoseconds, nanoseconds,
        resolution_nanoseconds);
}

bool heartbeat_seen()
{
    return s_heartbeatSeen;
}

#else // !ARCH_HAS_PORT_IO

void     init(uint32_t) { }
void     irq_handler()  { }
uint64_t ticks()        { return 0; }
bool get_monotonic_nanoseconds(uint64_t&, uint32_t&) { return false; }
bool     heartbeat_seen() { return false; }

#endif // ARCH_HAS_PORT_IO

} // namespace pit
} // namespace kernel
