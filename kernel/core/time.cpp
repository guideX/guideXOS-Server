#include "include/kernel/time.h"
#include "include/kernel/arch.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace time {

static const uint16_t CMOS_ADDR_PORT = 0x70;
static const uint16_t CMOS_DATA_PORT = 0x71;

static const uint8_t RTC_REG_SECONDS = 0x00;
static const uint8_t RTC_REG_MINUTES = 0x02;
static const uint8_t RTC_REG_HOURS = 0x04;
static const uint8_t RTC_REG_DAY = 0x07;
static const uint8_t RTC_REG_MONTH = 0x08;
static const uint8_t RTC_REG_YEAR = 0x09;
static const uint8_t RTC_REG_STATUS_A = 0x0A;
static const uint8_t RTC_REG_STATUS_B = 0x0B;
static const uint8_t RTC_UIP = 0x80;
static const uint8_t RTC_24_HOUR = 0x02;
static const uint8_t RTC_BINARY = 0x04;

static uint8_t bcd_to_binary(uint8_t value)
{
    return static_cast<uint8_t>((value & 0x0F) + ((value >> 4) * 10));
}

#if ARCH_HAS_PORT_IO
static uint8_t read_cmos(uint8_t reg)
{
    arch::outb(CMOS_ADDR_PORT, static_cast<uint8_t>(0x80 | reg));
    return arch::inb(CMOS_DATA_PORT);
}

static bool rtc_update_in_progress()
{
    return (read_cmos(RTC_REG_STATUS_A) & RTC_UIP) != 0;
}

static bool valid_datetime(const DateTime& dt)
{
    if (dt.year < 2020 || dt.year > 2099) return false;
    if (dt.month < 1 || dt.month > 12) return false;
    if (dt.day < 1 || dt.day > 31) return false;
    if (dt.hour > 23) return false;
    if (dt.minute > 59) return false;
    if (dt.second > 59) return false;
    return true;
}

bool get_current_datetime(DateTime& out)
{
    for (int i = 0; i < 100000 && rtc_update_in_progress(); i++) {}
    if (rtc_update_in_progress()) return false;

    uint8_t second = read_cmos(RTC_REG_SECONDS);
    uint8_t minute = read_cmos(RTC_REG_MINUTES);
    uint8_t hour = read_cmos(RTC_REG_HOURS);
    uint8_t day = read_cmos(RTC_REG_DAY);
    uint8_t month = read_cmos(RTC_REG_MONTH);
    uint8_t year = read_cmos(RTC_REG_YEAR);
    uint8_t statusB = read_cmos(RTC_REG_STATUS_B);

    for (int attempt = 0; attempt < 3; attempt++) {
        for (int i = 0; i < 100000 && rtc_update_in_progress(); i++) {}
        if (rtc_update_in_progress()) return false;

        uint8_t second2 = read_cmos(RTC_REG_SECONDS);
        uint8_t minute2 = read_cmos(RTC_REG_MINUTES);
        uint8_t hour2 = read_cmos(RTC_REG_HOURS);
        uint8_t day2 = read_cmos(RTC_REG_DAY);
        uint8_t month2 = read_cmos(RTC_REG_MONTH);
        uint8_t year2 = read_cmos(RTC_REG_YEAR);
        uint8_t statusB2 = read_cmos(RTC_REG_STATUS_B);

        if (second == second2 && minute == minute2 && hour == hour2 &&
            day == day2 && month == month2 && year == year2 && statusB == statusB2) {
            break;
        }

        second = second2;
        minute = minute2;
        hour = hour2;
        day = day2;
        month = month2;
        year = year2;
        statusB = statusB2;
    }

    bool binary = (statusB & RTC_BINARY) != 0;
    bool hour24 = (statusB & RTC_24_HOUR) != 0;
    bool pm = !hour24 && (hour & 0x80) != 0;

    hour = static_cast<uint8_t>(hour & 0x7F);

    if (!binary) {
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour = bcd_to_binary(hour);
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
    }

    if (!hour24) {
        if (pm && hour < 12) hour = static_cast<uint8_t>(hour + 12);
        if (!pm && hour == 12) hour = 0;
    }

    DateTime dt{};
    dt.year = static_cast<uint16_t>(2000 + year);
    dt.month = month;
    dt.day = day;
    dt.hour = hour;
    dt.minute = minute;
    dt.second = second;

    if (!valid_datetime(dt)) {
        serial::puts("[TIME] RTC returned invalid date/time\n");
        return false;
    }

    out = dt;
    return true;
}
#else
bool get_current_datetime(DateTime& out)
{
    (void)out;
    return false;
}
#endif

} // namespace time
} // namespace kernel
