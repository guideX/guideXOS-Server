#include "include/kernel/time.h"
#include "include/kernel/arch.h"
#include "include/kernel/pit.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace time {
namespace {

static const uint16_t kCmosAddressPort = 0x70;
static const uint16_t kCmosDataPort = 0x71;
static const uint8_t kRtcRegSeconds = 0x00;
static const uint8_t kRtcRegMinutes = 0x02;
static const uint8_t kRtcRegHours = 0x04;
static const uint8_t kRtcRegDay = 0x07;
static const uint8_t kRtcRegMonth = 0x08;
static const uint8_t kRtcRegYear = 0x09;
static const uint8_t kRtcRegStatusA = 0x0A;
static const uint8_t kRtcRegStatusB = 0x0B;
static const uint8_t kRtcRegStatusD = 0x0D;
static const uint8_t kRtcUpdateInProgress = 0x80;
static bool s_clock_source_reported = false;

#if ARCH_HAS_PORT_IO
static uint8_t read_cmos(uint8_t reg)
{
    // Keep NMI enabled. The index/data pair is protected against maskable
    // interrupt re-entry below; no current NMI handler accesses CMOS.
    arch::outb(kCmosAddressPort, static_cast<uint8_t>(reg & 0x7Fu));
    return arch::inb(kCmosDataPort);
}

static CmosRegisterSnapshot read_register_set()
{
    CmosRegisterSnapshot value{};
    value.seconds = read_cmos(kRtcRegSeconds);
    value.minutes = read_cmos(kRtcRegMinutes);
    value.hours = read_cmos(kRtcRegHours);
    value.day = read_cmos(kRtcRegDay);
    value.month = read_cmos(kRtcRegMonth);
    value.year = read_cmos(kRtcRegYear);
    value.status_b = read_cmos(kRtcRegStatusB);
    value.status_d = read_cmos(kRtcRegStatusD);
    return value;
}

static bool wait_until_cmos_ready()
{
    for (uint32_t attempt = 0; attempt < 100000u; ++attempt) {
        const uint64_t interrupt_flags = arch::save_and_disable_interrupts();
        const uint8_t status_a = read_cmos(kRtcRegStatusA);
        arch::restore_interrupts(interrupt_flags);
        if ((status_a & kRtcUpdateInProgress) == 0u) return true;
    }
    return false;
}

static ClockReadStatus read_cmos_calendar(DateTime& out)
{
    out = DateTime{};
    for (uint32_t attempt = 0; attempt < 3u; ++attempt) {
        if (!wait_until_cmos_ready()) return ClockReadStatus::Unavailable;

        const uint64_t interrupt_flags = arch::save_and_disable_interrupts();
        const uint8_t before = read_cmos(kRtcRegStatusA);
        if ((before & kRtcUpdateInProgress) != 0u) {
            arch::restore_interrupts(interrupt_flags);
            continue;
        }
        CmosRegisterSnapshot first = read_register_set();
        const uint8_t between = read_cmos(kRtcRegStatusA);
        CmosRegisterSnapshot second = read_register_set();
        const uint8_t after = read_cmos(kRtcRegStatusA);
        arch::restore_interrupts(interrupt_flags);

        if ((between & kRtcUpdateInProgress) != 0u ||
            (after & kRtcUpdateInProgress) != 0u) continue;
        first.status_a = before;
        second.status_a = after;
        if (first.seconds != second.seconds || first.minutes != second.minutes ||
            first.hours != second.hours || first.day != second.day ||
            first.month != second.month || first.year != second.year ||
            first.status_b != second.status_b || first.status_d != second.status_d)
            continue;

        const CmosDecodeStatus decode = decode_cmos_snapshot(second, out);
        if (decode == CmosDecodeStatus::Ok) return ClockReadStatus::Ok;
        out = DateTime{};
        return ClockReadStatus::Invalid;
    }
    return ClockReadStatus::Invalid;
}

static ClockReadStatus read_cmos_observation(void*, ClockObservation& out)
{
    out = ClockObservation{};
    out.source = ClockSource::CmosRtc;
    out.provenance = ClockProvenance::Unknown;
    out.resolution_known = true;
    out.resolution_nanoseconds = UINT32_C(1000000000);
    out.accuracy = ClockAccuracy::Unknown;
    return read_cmos_calendar(out.calendar);
}
#endif

static void add_pit_monotonic(ClockReading& reading)
{
    uint64_t nanoseconds = 0u;
    uint32_t resolution_nanoseconds = 0u;
    if (!pit::get_monotonic_nanoseconds(nanoseconds,
                                       resolution_nanoseconds)) return;
    reading.monotonic_available = true;
    reading.monotonic_source = ClockSource::Pit;
    reading.monotonic_nanoseconds = nanoseconds;
    reading.monotonic_resolution_nanoseconds = resolution_nanoseconds;
}

static void report_clock_source_once(const ClockReading& reading)
{
    if (s_clock_source_reported) return;
    s_clock_source_reported = true;
    serial::puts("[REXX-CLOCK] source=CMOS_RTC read=");
    serial::puts(reading.status == ClockReadStatus::Ok ? "ok" :
        (reading.status == ClockReadStatus::Invalid ? "invalid" :
         (reading.status == ClockReadStatus::Unsupported ? "unsupported" :
          "unavailable")));
    serial::puts(" raw=");
    serial::puts(reading.raw_available ? "1" : "0");
    serial::puts(" local=");
    serial::puts(reading.local_available ? "1" : "0");
    serial::puts(" utc=");
    serial::puts(reading.utc_available ? "1" : "0");
    serial::puts(" offset=");
    serial::puts(reading.utc_offset_known ? "known" : "unknown");
    serial::puts(" resolution_ns=");
    serial::put_hex32(reading.resolution_nanoseconds);
    serial::puts(" accuracy=unknown monotonic=");
    serial::puts(reading.monotonic_available ? "PIT" : "unavailable");
    serial::puts("\n");
}

} // namespace

bool get_clock_reading(ClockReading& out)
{
#if ARCH_HAS_PORT_IO
    read_clock_from_source(read_cmos_observation, NULL, out);
#else
    out = ClockReading{};
    out.source = ClockSource::CmosRtc;
    out.status = ClockReadStatus::Unsupported;
    out.interpretation_status = InterpretationStatus::Unsupported;
#endif
    add_pit_monotonic(out);
    report_clock_source_once(out);
    return out.status == ClockReadStatus::Ok || out.monotonic_available;
}

bool get_current_datetime(DateTime& out)
{
    out = DateTime{};
#if ARCH_HAS_PORT_IO
    return read_cmos_calendar(out) == ClockReadStatus::Ok;
#else
    return false;
#endif
}

} // namespace time
} // namespace kernel
