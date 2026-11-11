#include "gxos_tls_prerequisites.h"
#include "gxos_clock_assurance.h"

#if defined(GXOS_BARE_METAL)

#include "kernel/core/include/kernel/arch.h"
#include "kernel/core/include/kernel/serial_debug.h"
#include "kernel/core/include/kernel/secure_random.h"
#include "kernel/core/include/kernel/time.h"
#include "kernel/core/include/kernel/pit.h"
#include "kernel/core/include/kernel/virtio_rng.h"

namespace gxos {
namespace {

bool s_entropyReadMarkerEmitted = false;
clockassurance::WithinBootContinuity s_rtcContinuity;

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

int64_t datetime_to_unix_seconds(const kernel::time::DateTime& dt)
{
    int64_t days = 0;
    for (uint16_t year = 1970; year < dt.year; ++year) {
        days += is_leap_year(year) ? 366 : 365;
    }
    for (uint8_t month = 1; month < dt.month; ++month) {
        days += days_in_month(dt.year, month);
    }
    days += static_cast<int64_t>(dt.day - 1);
    return (((days * 24) + dt.hour) * 60 + dt.minute) * 60 + dt.second;
}

bool unix_seconds_to_datetime(int64_t seconds, kernel::time::DateTime* out)
{
    if (!out || seconds < clockassurance::kPlausibleMinimumUnixSeconds ||
        seconds >= clockassurance::kPlausibleMaximumUnixSeconds) return false;
    uint64_t days = static_cast<uint64_t>(seconds / 86400);
    const uint32_t daySeconds = static_cast<uint32_t>(seconds % 86400);
    uint16_t year = 1970;
    while (year < 2101) {
        const uint16_t daysThisYear = is_leap_year(year) ? 366u : 365u;
        if (days < daysThisYear) break;
        days -= daysThisYear;
        ++year;
    }
    if (year > 2100) return false;
    uint8_t month = 1;
    while (month <= 12) {
        const uint8_t daysThisMonth = days_in_month(year, month);
        if (days < daysThisMonth) break;
        days -= daysThisMonth;
        ++month;
    }
    if (month > 12) return false;
    out->year = year;
    out->month = month;
    out->day = static_cast<uint8_t>(days + 1u);
    out->hour = static_cast<uint8_t>(daySeconds / 3600u);
    out->minute = static_cast<uint8_t>((daySeconds / 60u) % 60u);
    out->second = static_cast<uint8_t>(daySeconds % 60u);
    return true;
}

GxosClockAssuranceInfo rtc_failure_info()
{
    const clockassurance::ContinuityFailure failure = s_rtcContinuity.failure();
    const bool rollback = failure == clockassurance::ContinuityFailure::MonotonicWentBackwards ||
        failure == clockassurance::ContinuityFailure::WallClockWentBackwards;
    const char* reason = "CMOS RTC is unavailable or invalid; certificate time checks fail closed.";
    if (failure == clockassurance::ContinuityFailure::WallClockWentBackwards) {
        reason = "CMOS RTC moved backward against this boot's clock evidence; time is rejected until reboot.";
    } else if (failure == clockassurance::ContinuityFailure::MonotonicWentBackwards) {
        reason = "PIT monotonic evidence moved backward; time is rejected until reboot.";
    } else if (failure == clockassurance::ContinuityFailure::ExcessiveForwardJump) {
        reason = "CMOS RTC jumped forward beyond the bounded PIT consistency allowance; time is rejected until reboot.";
    }
    return {
        rollback ? GxosClockStatus::RollbackSuspected : GxosClockStatus::Invalid,
        "CMOS RTC",
        0,
        0,
        0,
        0,
        kernel::pit::ticks() / 100u,
        0,
        false,
        false,
        false,
        rollback,
        reason
    };
}

} // namespace

bool gxos_random_bytes(void* buffer, size_t len)
{
    if (!buffer && len != 0) return false;
    if (len == 0) return true;

    if (!kernel::secure_random::fill(buffer, len)) return false;
    if (!s_entropyReadMarkerEmitted) {
        s_entropyReadMarkerEmitted = true;
        kernel::serial::puts("[RNG] entropy sample request success; secure source read confirmed; provider=");
        kernel::serial::puts(gxos_secure_random_source());
        kernel::serial::puts("\n");
    }
    return true;
}

GxosRandomQuality gxos_random_quality()
{
    return kernel::secure_random::ready()
        ? GxosRandomQuality::Secure
        : GxosRandomQuality::Unavailable;
}

const char* gxos_random_backend()
{
    return kernel::secure_random::source_name();
}

const char* gxos_secure_random_source()
{
    return kernel::secure_random::source_name();
}

const char* gxos_secure_random_status()
{
    return kernel::secure_random::status_name();
}

bool gxos_rdseed_supported()
{
    return kernel::secure_random::rdseed_supported();
}

bool gxos_rdrand_supported()
{
    return kernel::secure_random::rdrand_supported();
}

bool gxos_virtio_rng_detected()
{
    return kernel::virtio::rng::detected();
}

const char* gxos_virtio_rng_status()
{
    return kernel::virtio::rng::last_status_name();
}

GxosSecureRandomDiagnostics gxos_secure_random_diagnostics()
{
    const kernel::secure_random::SecureRandomDiagnostics diagnostics =
        kernel::secure_random::diagnostics();
    return {
        diagnostics.initialized,
        diagnostics.cpu.rdseed,
        diagnostics.cpu.rdrand,
        diagnostics.virtioDetected,
        diagnostics.virtioReady,
        kernel::secure_random::source_name(),
        kernel::secure_random::status_name(),
        diagnostics.fillRequests,
        diagnostics.fillFailures,
        diagnostics.rdseedRetryFailures,
        diagnostics.rdrandRetryFailures,
        diagnostics.providerFallbacks
    };
}

GxosClockAssuranceInfo gxos_wall_clock_assurance()
{
    if (s_rtcContinuity.failed()) return rtc_failure_info();

    kernel::time::DateTime dt{};
    if (!kernel::time::get_current_datetime(dt) || !plausible_datetime(dt)) {
        s_rtcContinuity.source_failure();
        return rtc_failure_info();
    }

    const int64_t seconds = datetime_to_unix_seconds(dt);
    const uint64_t monotonicSeconds = kernel::pit::ticks() / 100u;
    const clockassurance::Observation observation = s_rtcContinuity.observe(seconds, monotonicSeconds);
    if (observation != clockassurance::Observation::Accepted) return rtc_failure_info();

    return {
        GxosClockStatus::Plausible,
        "CMOS RTC",
        seconds,
        0,
        0,
        0,
        monotonicSeconds,
        0,
        false,
        false,
        false,
        false,
        "Stable RTC date passes calendar and range checks; authenticity, uncertainty bounds, and cross-boot rollback protection are unavailable."
    };
}

bool gxos_wall_clock_unix_seconds(int64_t* out)
{
    if (!out) return false;
    const GxosClockAssuranceInfo assurance = gxos_wall_clock_assurance();
    if (assurance.status != GxosClockStatus::Plausible &&
        assurance.status != GxosClockStatus::Authenticated) return false;
    *out = assurance.estimateUnixSeconds;
    return true;
}

GxosClockStatus gxos_wall_clock_status()
{
    return gxos_wall_clock_assurance().status;
}

const char* gxos_wall_clock_backend()
{
#if ARCH_HAS_PORT_IO
    return "CMOS RTC (raw date interpreted as UTC; timezone metadata unavailable)";
#else
    return "none (unsupported architecture)";
#endif
}

bool gxos_wall_clock_utc_text(char* out, size_t out_size)
{
    if (!out || out_size < 21) return false;
    const GxosClockAssuranceInfo assurance = gxos_wall_clock_assurance();
    if (assurance.status != GxosClockStatus::Plausible &&
        assurance.status != GxosClockStatus::Authenticated) return false;
    kernel::time::DateTime dt{};
    if (!unix_seconds_to_datetime(assurance.estimateUnixSeconds, &dt)) return false;

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

const char* gxos_secure_random_source()
{
    return gxos_random_backend();
}

const char* gxos_secure_random_status()
{
#if defined(_WIN32)
    return "hosted-system-rng";
#else
    return "hosted-not-applicable";
#endif
}

bool gxos_rdseed_supported()
{
    return false;
}

bool gxos_rdrand_supported()
{
    return false;
}

bool gxos_virtio_rng_detected()
{
    return false;
}

const char* gxos_virtio_rng_status()
{
    return "hosted-not-applicable";
}

GxosSecureRandomDiagnostics gxos_secure_random_diagnostics()
{
#if defined(_WIN32)
    return {
        true,
        false,
        false,
        false,
        false,
        "BCryptGenRandom system-preferred RNG",
        "hosted-system-rng",
        0,
        0,
        0,
        0,
        0
    };
#else
    return {
        false,
        false,
        false,
        false,
        false,
        "none (unsupported hosted platform)",
        "hosted-not-applicable",
        0,
        0,
        0,
        0,
        0
    };
#endif
}

static bool read_host_system_utc(int64_t* out)
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

GxosClockAssuranceInfo gxos_wall_clock_assurance()
{
    int64_t seconds = 0;
    if (read_host_system_utc(&seconds)) {
        return {
            GxosClockStatus::Plausible,
            "Windows system UTC",
            seconds,
            0,
            0,
            0,
#if defined(_WIN32)
            static_cast<uint64_t>(GetTickCount64() / 1000ull),
#else
            0,
#endif
            0,
            false,
            false,
            false,
            false,
            "Host operating-system UTC passes range checks; guideXOS has no cryptographic freshness or rollback evidence."
        };
    }
    return {
        GxosClockStatus::Invalid,
#if defined(_WIN32)
        "Windows system UTC",
#else
        "none",
#endif
        0,
        0,
        0,
        0,
        0,
        0,
        false,
        false,
        false,
        false,
        "Host wall clock is unavailable or outside the supported date range."
    };
}

bool gxos_wall_clock_unix_seconds(int64_t* out)
{
    if (!out) return false;
    const GxosClockAssuranceInfo assurance = gxos_wall_clock_assurance();
    if (assurance.status != GxosClockStatus::Plausible &&
        assurance.status != GxosClockStatus::Authenticated) return false;
    *out = assurance.estimateUnixSeconds;
    return true;
}

GxosClockStatus gxos_wall_clock_status()
{
    return gxos_wall_clock_assurance().status;
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
    case GxosClockStatus::Invalid: return "Invalid";
    case GxosClockStatus::Plausible: return "Plausible";
    case GxosClockStatus::Authenticated: return "Authenticated";
    case GxosClockStatus::RollbackSuspected: return "RollbackSuspected";
    default: return "Invalid";
    }
}

} // namespace gxos
