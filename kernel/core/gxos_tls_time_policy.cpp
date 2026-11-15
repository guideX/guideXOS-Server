#include "include/gxos_tls_time_policy.h"

namespace gxos {
namespace tls_time {
namespace {

bool leap_year(int year)
{
    return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

int days_in_month(int year, int month)
{
    static const uint8_t lengths[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (month < 1 || month > 12) return 0;
    if (month == 2 && leap_year(year)) return 29;
    return lengths[month - 1];
}

int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2U;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned adjustedMonth = month > 2U ? month - 3U : month + 9U;
    const unsigned dayOfYear = (153U * adjustedMonth + 2U) / 5U + day - 1U;
    const unsigned dayOfEra = yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U + dayOfYear;
    return era * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

} // namespace

bool certificate_utc_to_unix_seconds(const UtcCalendarTime& calendar,
                                     int64_t* outputUnixSeconds)
{
    if (!outputUnixSeconds || calendar.year < 1 || calendar.year > 9999 ||
        calendar.month < 1 || calendar.month > 12 || calendar.day < 1 ||
        calendar.day > days_in_month(calendar.year, calendar.month) ||
        calendar.hour < 0 || calendar.hour > 23 ||
        calendar.minute < 0 || calendar.minute > 59 ||
        calendar.second < 0 || calendar.second > 59) {
        return false;
    }

    const int64_t days = days_from_civil(calendar.year,
        static_cast<unsigned>(calendar.month), static_cast<unsigned>(calendar.day));
    const int64_t daySeconds = static_cast<int64_t>(calendar.hour) * 3600 +
        static_cast<int64_t>(calendar.minute) * 60 + calendar.second;
    // Calendar input is bounded to years 1..9999, so this multiplication is
    // below 3e11 seconds and cannot approach the int64 limits.
    *outputUnixSeconds = days * 86400 + daySeconds;
    return true;
}

CertificateIntervalResult check_certificate_interval(
    uint64_t earliestUnixSeconds, uint64_t latestUnixSeconds,
    int64_t notBeforeUnixSeconds, int64_t notAfterUnixSeconds)
{
    if (earliestUnixSeconds > latestUnixSeconds) {
        return CertificateIntervalResult::MalformedTimeInterval;
    }
    if (notBeforeUnixSeconds > notAfterUnixSeconds) {
        return CertificateIntervalResult::InvalidCertificateDate;
    }
    if (earliestUnixSeconds > static_cast<uint64_t>(INT64_MAX) ||
        latestUnixSeconds > static_cast<uint64_t>(INT64_MAX)) {
        return CertificateIntervalResult::MalformedTimeInterval;
    }
    if (static_cast<int64_t>(earliestUnixSeconds) < notBeforeUnixSeconds) {
        return CertificateIntervalResult::BeforeNotBefore;
    }
    if (static_cast<int64_t>(latestUnixSeconds) > notAfterUnixSeconds) {
        return CertificateIntervalResult::AfterNotAfter;
    }
    return CertificateIntervalResult::Valid;
}

const char* certificate_interval_result_name(CertificateIntervalResult result)
{
    switch (result) {
    case CertificateIntervalResult::Valid: return "valid";
    case CertificateIntervalResult::MalformedTimeInterval: return "malformed-time-interval";
    case CertificateIntervalResult::InvalidCertificateDate: return "invalid-certificate-date";
    case CertificateIntervalResult::BeforeNotBefore: return "interval-before-or-crosses-notBefore";
    case CertificateIntervalResult::AfterNotAfter: return "interval-after-or-crosses-notAfter";
    default: return "unknown";
    }
}

} // namespace tls_time
} // namespace gxos
