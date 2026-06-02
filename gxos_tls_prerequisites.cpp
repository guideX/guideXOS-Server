#include "gxos_tls_prerequisites.h"

#if defined(GXOS_BARE_METAL)

#include "kernel/core/include/kernel/arch.h"
#include "kernel/core/include/kernel/time.h"
#include "kernel/core/include/kernel/virtio_rng.h"

namespace gxos {
namespace {

bool is_leap_year(uint16_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2 && is_leap_year(year)) return 29;
    return days[month - 1];
}

bool plausible_datetime(const kernel::time::DateTime& dt)
{
    return dt.year >= 2024 && dt.year <= 2100 &&
        dt.month >= 1 && dt.month <= 12 &&
        dt.day >= 1 && dt.day <= days_in_month(dt.year, dt.month) &&
        dt.hour <= 23 && dt.minute <= 59 && dt.second <= 59;
}

void put_two_digits(char* out, uint8_t value)
{
    out[0] = static_cast<char>('0' + ((value / 10) % 10));
    out[1] = static_cast<char>('0' + (value % 10));
}

} // namespace

bool gxos_random_bytes(void* buffer, size_t len)
{
    if (!buffer && len != 0) return false;
    if (len == 0) return true;

    uint8_t* out = static_cast<uint8_t*>(buffer);
    while (len != 0) {
        uint8_t chunk[64];
        const size_t request = len > sizeof(chunk) ? sizeof(chunk) : len;
        if (!kernel::virtio::rng::fill(chunk, request)) return false;
        for (size_t i = 0; i < request; ++i) out[i] = chunk[i];
        out += request;
        len -= request;
    }
    return true;
}

GxosRandomQuality gxos_random_quality()
{
    return kernel::virtio::rng::ready()
        ? GxosRandomQuality::Secure
        : GxosRandomQuality::Unavailable;
}

const char* gxos_random_backend()
{
    if (kernel::virtio::rng::ready()) {
        return kernel::virtio::rng::backend_name();
    }

    switch (kernel::virtio::rng::last_status()) {
    case kernel::virtio::rng::STATUS_DEVICE_NOT_FOUND:
        return "none (virtio-rng PCI device not found)";
    case kernel::virtio::rng::STATUS_UNSUPPORTED_ARCH:
        return "none (virtio-rng requires x86/AMD64 PCI port I/O)";
    case kernel::virtio::rng::STATUS_UNSUPPORTED_VIRTIO_MODE:
        return "none (virtio-rng modern/non-transitional PCI unsupported)";
    case kernel::virtio::rng::STATUS_QUEUE_SETUP_FAILED:
        return "none (virtio-rng queue setup failed)";
    case kernel::virtio::rng::STATUS_REQUEST_TIMEOUT:
        return "none (virtio-rng request timeout)";
    case kernel::virtio::rng::STATUS_SHORT_READ:
        return "none (virtio-rng short read)";
    case kernel::virtio::rng::STATUS_DEVICE_ERROR:
        return "none (virtio-rng device error)";
    default:
        return "none (secure entropy unavailable)";
    }
}

bool gxos_virtio_rng_detected()
{
    return kernel::virtio::rng::detected();
}

const char* gxos_virtio_rng_status()
{
    return kernel::virtio::rng::last_status_name();
}

bool gxos_wall_clock_unix_seconds(int64_t* out)
{
    if (!out) return false;

    kernel::time::DateTime dt{};
    if (!kernel::time::get_current_datetime(dt) || !plausible_datetime(dt)) return false;

    int64_t days = 0;
    for (uint16_t year = 1970; year < dt.year; ++year) {
        days += is_leap_year(year) ? 366 : 365;
    }
    for (uint8_t month = 1; month < dt.month; ++month) {
        days += days_in_month(dt.year, month);
    }
    days += static_cast<int64_t>(dt.day - 1);

    const int64_t seconds = (((days * 24) + dt.hour) * 60 + dt.minute) * 60 + dt.second;
    if (seconds <= 0) return false;
    *out = seconds;
    return true;
}

GxosClockStatus gxos_wall_clock_status()
{
    int64_t seconds = 0;
    return gxos_wall_clock_unix_seconds(&seconds)
        ? GxosClockStatus::Plausible
        : GxosClockStatus::Unavailable;
}

const char* gxos_wall_clock_backend()
{
#if ARCH_HAS_PORT_IO
    return "CMOS RTC (interpreted as UTC)";
#else
    return "none (unsupported architecture)";
#endif
}

bool gxos_wall_clock_utc_text(char* out, size_t out_size)
{
    if (!out || out_size < 21) return false;

    kernel::time::DateTime dt{};
    if (!kernel::time::get_current_datetime(dt) || !plausible_datetime(dt)) return false;

    out[0] = static_cast<char>('0' + ((dt.year / 1000) % 10));
    out[1] = static_cast<char>('0' + ((dt.year / 100) % 10));
    out[2] = static_cast<char>('0' + ((dt.year / 10) % 10));
    out[3] = static_cast<char>('0' + (dt.year % 10));
    out[4] = '-';
    put_two_digits(out + 5, dt.month);
    out[7] = '-';
    put_two_digits(out + 8, dt.day);
    out[10] = 'T';
    put_two_digits(out + 11, dt.hour);
    out[13] = ':';
    put_two_digits(out + 14, dt.minute);
    out[16] = ':';
    put_two_digits(out + 17, dt.second);
    out[19] = 'Z';
    out[20] = '\0';
    return true;
}

#else

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#include <ctime>
#include <cstdio>
#include <limits>
#if defined(_MSC_VER)
#pragma comment(lib, "bcrypt.lib")
#endif
#endif

namespace gxos {

bool gxos_random_bytes(void* buffer, size_t len)
{
#if defined(_WIN32)
    if (!buffer && len != 0) return false;
    uint8_t* bytes = static_cast<uint8_t*>(buffer);
    while (len != 0) {
        const size_t max_chunk = static_cast<size_t>((std::numeric_limits<ULONG>::max)());
        const ULONG chunk = static_cast<ULONG>(len > max_chunk ? max_chunk : len);
        if (BCryptGenRandom(nullptr, bytes, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return false;
        bytes += chunk;
        len -= chunk;
    }
    return true;
#else
    (void)buffer;
    (void)len;
    return false;
#endif
}

GxosRandomQuality gxos_random_quality()
{
    uint8_t probe = 0;
    return gxos_random_bytes(&probe, sizeof(probe))
        ? GxosRandomQuality::Secure
        : GxosRandomQuality::Unavailable;
}

const char* gxos_random_backend()
{
#if defined(_WIN32)
    return "BCryptGenRandom system-preferred RNG";
#else
    return "none (unsupported hosted platform)";
#endif
}

bool gxos_virtio_rng_detected()
{
    return false;
}

const char* gxos_virtio_rng_status()
{
    return "hosted-not-applicable";
}

bool gxos_wall_clock_unix_seconds(int64_t* out)
{
#if defined(_WIN32)
    if (!out) return false;
    FILETIME file_time{};
    GetSystemTimeAsFileTime(&file_time);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = file_time.dwLowDateTime;
    ticks.HighPart = file_time.dwHighDateTime;
    constexpr uint64_t kUnixEpochFileTimeTicks = 116444736000000000ull;
    if (ticks.QuadPart <= kUnixEpochFileTimeTicks) return false;
    const int64_t seconds = static_cast<int64_t>((ticks.QuadPart - kUnixEpochFileTimeTicks) / 10000000ull);
    constexpr int64_t kPlausibleMinimum = 1704067200ll; // 2024-01-01T00:00:00Z
    constexpr int64_t kPlausibleMaximum = 4133980800ll; // 2101-01-01T00:00:00Z
    if (seconds < kPlausibleMinimum || seconds >= kPlausibleMaximum) return false;
    *out = seconds;
    return true;
#else
    (void)out;
    return false;
#endif
}

GxosClockStatus gxos_wall_clock_status()
{
    int64_t seconds = 0;
    return gxos_wall_clock_unix_seconds(&seconds)
        ? GxosClockStatus::Verified
        : GxosClockStatus::Unavailable;
}

const char* gxos_wall_clock_backend()
{
#if defined(_WIN32)
    return "Windows system UTC";
#else
    return "none (unsupported hosted platform)";
#endif
}

bool gxos_wall_clock_utc_text(char* out, size_t out_size)
{
#if defined(_WIN32)
    if (!out || out_size == 0) return false;
    int64_t seconds = 0;
    if (!gxos_wall_clock_unix_seconds(&seconds)) return false;
    const std::time_t raw = static_cast<std::time_t>(seconds);
    std::tm* utc = std::gmtime(&raw);
    if (!utc) return false;
    return std::snprintf(out, out_size, "%04d-%02d-%02dT%02d:%02d:%02dZ",
        utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
        utc->tm_hour, utc->tm_min, utc->tm_sec) > 0;
#else
    (void)out;
    (void)out_size;
    return false;
#endif
}

#endif

const char* gxos_random_quality_name(GxosRandomQuality quality)
{
    switch (quality) {
    case GxosRandomQuality::TestOnly: return "TestOnly";
    case GxosRandomQuality::Secure: return "Secure";
    default: return "Unavailable";
    }
}

const char* gxos_wall_clock_status_name(GxosClockStatus status)
{
    switch (status) {
    case GxosClockStatus::Plausible: return "Plausible";
    case GxosClockStatus::Verified: return "Verified";
    default: return "Unavailable";
    }
}

} // namespace gxos
