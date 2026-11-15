#pragma once

#include <stdint.h>

namespace gxos {
namespace tls_time {

struct UtcCalendarTime {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
};

enum class CertificateIntervalResult : uint8_t {
    Valid,
    MalformedTimeInterval,
    InvalidCertificateDate,
    BeforeNotBefore,
    AfterNotAfter
};

bool certificate_utc_to_unix_seconds(const UtcCalendarTime& calendar,
                                     int64_t* outputUnixSeconds);

// The complete uncertainty interval must be contained in the certificate's
// inclusive validity interval. The certificate bounds are Unix UTC seconds.
CertificateIntervalResult check_certificate_interval(
    uint64_t earliestUnixSeconds, uint64_t latestUnixSeconds,
    int64_t notBeforeUnixSeconds, int64_t notAfterUnixSeconds);

const char* certificate_interval_result_name(CertificateIntervalResult result);

} // namespace tls_time
} // namespace gxos
