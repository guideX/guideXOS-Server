#include "include/kernel/time.h"

namespace kernel {
namespace time {
namespace {

static const int64_t kSecondsPerDay = 86400;
static const uint64_t kPitInputFrequency = 1193182ULL;
static const uint32_t kUefiUnspecifiedTimezone = 0x07FFu;
static const uint8_t kUefiKnownDaylightFlags = 0x03u;
static const uint8_t kCmosUpdateInProgress = 0x80u;
static const uint8_t kCmosBinaryMode = 0x04u;
static const uint8_t kCmos24HourMode = 0x02u;
static const uint8_t kCmosValidTime = 0x80u;

static bool is_leap_year(uint16_t year)
{
    return (year % 4u == 0u && year % 100u != 0u) ||
           (year % 400u == 0u);
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (month == 2u && is_leap_year(year)) return 29u;
    return days[month - 1u];
}

static bool decode_bcd(uint8_t value, uint8_t& decoded)
{
    const uint8_t low = static_cast<uint8_t>(value & 0x0Fu);
    const uint8_t high = static_cast<uint8_t>((value >> 4) & 0x0Fu);
    if (low > 9u || high > 9u) return false;
    decoded = static_cast<uint8_t>(high * 10u + low);
    return true;
}

static bool offset_is_valid(int16_t minutes)
{
    return minutes >= -1440 && minutes <= 1440;
}

static void initialize_reading(ClockReading& reading,
                               ClockReadStatus status,
                               ClockSource source)
{
    reading = ClockReading{};
    reading.status = status;
    reading.source = source;
}

static bool source_accepts_provenance(ClockSource source,
                                      ClockProvenance provenance)
{
    if (source == ClockSource::InjectedTest) return true;
    if (source == ClockSource::CmosRtc) {
        return provenance == ClockProvenance::Unknown ||
               provenance == ClockProvenance::ConfiguredRtcUtc ||
               provenance == ClockProvenance::ConfiguredRtcLocal ||
               provenance == ClockProvenance::ConfiguredRtcUtcWithOffset;
    }
    if (source == ClockSource::UefiRuntime) {
        return provenance == ClockProvenance::FirmwareLocal ||
               provenance == ClockProvenance::FirmwareLocalWithOffset;
    }
    return false;
}

static bool provenance_has_offset(ClockProvenance provenance)
{
    return provenance == ClockProvenance::ConfiguredRtcUtcWithOffset ||
           provenance == ClockProvenance::SourceDefinedUtcWithOffset ||
           provenance == ClockProvenance::FirmwareLocalWithOffset;
}

static bool provenance_is_utc(ClockProvenance provenance)
{
    return provenance == ClockProvenance::ConfiguredRtcUtc ||
           provenance == ClockProvenance::ConfiguredRtcUtcWithOffset ||
           provenance == ClockProvenance::SourceDefinedUtc ||
           provenance == ClockProvenance::SourceDefinedUtcWithOffset ||
           provenance == ClockProvenance::FirmwareLocalWithOffset;
}

static bool provenance_is_local(ClockProvenance provenance)
{
    return provenance == ClockProvenance::ConfiguredRtcLocal ||
           provenance == ClockProvenance::SourceDefinedLocal ||
           provenance == ClockProvenance::FirmwareLocal ||
           provenance == ClockProvenance::FirmwareLocalWithOffset ||
           provenance == ClockProvenance::ConfiguredRtcUtcWithOffset ||
           provenance == ClockProvenance::SourceDefinedUtcWithOffset;
}

static InterpretationBasis basis_for_provenance(ClockProvenance provenance)
{
    switch (provenance) {
    case ClockProvenance::ConfiguredRtcUtc:
    case ClockProvenance::ConfiguredRtcLocal:
    case ClockProvenance::ConfiguredRtcUtcWithOffset:
        return InterpretationBasis::OperatorConfiguration;
    case ClockProvenance::SourceDefinedUtc:
    case ClockProvenance::SourceDefinedLocal:
    case ClockProvenance::SourceDefinedUtcWithOffset:
        return InterpretationBasis::SourceContract;
    case ClockProvenance::FirmwareLocal:
    case ClockProvenance::FirmwareLocalWithOffset:
        return InterpretationBasis::FirmwareContract;
    default:
        return InterpretationBasis::Unknown;
    }
}

static int64_t days_from_civil(uint16_t year, uint8_t month, uint8_t day)
{
    int64_t y = year;
    const int64_t m = month;
    y -= m <= 2 ? 1 : 0;
    const int64_t era = y / 400;
    const uint32_t year_of_era = static_cast<uint32_t>(y - era * 400);
    const int64_t adjusted_month = m + (m > 2 ? -3 : 9);
    const uint32_t day_of_year = static_cast<uint32_t>(
        (153 * adjusted_month + 2) / 5 + day - 1);
    const uint32_t day_of_era = year_of_era * 365u + year_of_era / 4u -
        year_of_era / 100u + day_of_year;
    return era * 146097 + static_cast<int64_t>(day_of_era) - 719468;
}

} // namespace

bool is_valid_calendar(const DateTime& value)
{
    if (value.year < 1u || value.year > 9999u) return false;
    if (value.month < 1u || value.month > 12u) return false;
    if (value.day < 1u || value.day > days_in_month(value.year, value.month))
        return false;
    if (value.hour > 23u || value.minute > 59u || value.second > 59u)
        return false;
    return value.nanosecond < 1000000000u;
}

CmosDecodeStatus decode_cmos_snapshot(const CmosRegisterSnapshot& snapshot,
                                      DateTime& value)
{
    value = DateTime{};
    if ((snapshot.status_a & kCmosUpdateInProgress) != 0u)
        return CmosDecodeStatus::UpdateInProgress;
    if ((snapshot.status_d & kCmosValidTime) == 0u)
        return CmosDecodeStatus::InvalidValidityStatus;

    const bool binary = (snapshot.status_b & kCmosBinaryMode) != 0u;
    const bool hour24 = (snapshot.status_b & kCmos24HourMode) != 0u;
    const bool pm = !hour24 && (snapshot.hours & 0x80u) != 0u;
    uint8_t second = snapshot.seconds;
    uint8_t minute = snapshot.minutes;
    uint8_t hour = static_cast<uint8_t>(snapshot.hours & 0x7Fu);
    uint8_t day = snapshot.day;
    uint8_t month = snapshot.month;
    uint8_t year = snapshot.year;

    if (!binary) {
        if (!decode_bcd(second, second) || !decode_bcd(minute, minute) ||
            !decode_bcd(hour, hour) || !decode_bcd(day, day) ||
            !decode_bcd(month, month) || !decode_bcd(year, year))
            return CmosDecodeStatus::InvalidBcd;
    }

    if (!hour24) {
        if (hour < 1u || hour > 12u) return CmosDecodeStatus::InvalidHour;
        if (pm && hour < 12u) hour = static_cast<uint8_t>(hour + 12u);
        if (!pm && hour == 12u) hour = 0u;
    }

    DateTime decoded{};
    decoded.year = static_cast<uint16_t>(2000u + year);
    decoded.month = month;
    decoded.day = day;
    decoded.hour = hour;
    decoded.minute = minute;
    decoded.second = second;

    // Preserve the existing x86 hardware policy: the two-digit RTC year is
    // interpreted as 2000 + YY, and dates before 2024 are rejected.
    if (decoded.year < 2024u || decoded.year > 2100u ||
        !is_valid_calendar(decoded))
        return CmosDecodeStatus::InvalidCalendar;

    value = decoded;
    return CmosDecodeStatus::Ok;
}

void classify_clock_observation(const ClockObservation& observation,
                                ClockReading& reading)
{
    initialize_reading(reading, ClockReadStatus::Invalid, observation.source);
    if (observation.source != ClockSource::CmosRtc &&
        observation.source != ClockSource::UefiRuntime &&
        observation.source != ClockSource::InjectedTest) {
        reading.status = ClockReadStatus::Unsupported;
        reading.interpretation_status = InterpretationStatus::Unsupported;
        return;
    }

    if (!is_valid_calendar(observation.calendar)) return;

    reading.status = ClockReadStatus::Ok;
    reading.calendar_valid = true;
    reading.raw_available = true;
    reading.raw_calendar = observation.calendar;
    reading.resolution_known = observation.resolution_known &&
        observation.resolution_nanoseconds > 0u &&
        observation.resolution_nanoseconds <= 1000000000u;
    reading.resolution_nanoseconds = reading.resolution_known
        ? observation.resolution_nanoseconds : 0u;
    reading.accuracy = observation.accuracy;
    reading.accuracy_parts_per_million =
        observation.accuracy == ClockAccuracy::ReportedPartsPerMillion
            ? observation.accuracy_parts_per_million : 0u;
    reading.daylight_flags = observation.daylight_flags;
    reading.source_limitations = observation.source_limitations;
    reading.provenance = observation.provenance;
    reading.interpretation_basis = basis_for_provenance(observation.provenance);

    if (observation.accuracy != ClockAccuracy::Unknown &&
        observation.accuracy != ClockAccuracy::ReportedPartsPerMillion) {
        reading.interpretation_status = InterpretationStatus::InvalidMetadata;
        return;
    }

    if (reading.accuracy == ClockAccuracy::Unknown)
        reading.source_limitations |= CLOCK_LIMIT_ACCURACY_UNKNOWN;
    if (observation.source == ClockSource::CmosRtc) {
        reading.source_limitations |= CLOCK_LIMIT_RTC_NO_CENTURY_REGISTER;
        if (observation.provenance == ClockProvenance::Unknown)
            reading.source_limitations |= CLOCK_LIMIT_RTC_INTERPRETATION_UNKNOWN;
    }
    if (observation.provenance == ClockProvenance::ConfiguredRtcUtc ||
        observation.provenance == ClockProvenance::ConfiguredRtcLocal ||
        observation.provenance == ClockProvenance::ConfiguredRtcUtcWithOffset)
        reading.source_limitations |= CLOCK_LIMIT_CONFIGURED_SETTING_UNVERIFIED;
    if (observation.source == ClockSource::UefiRuntime)
        reading.source_limitations |= CLOCK_LIMIT_NO_TIMEZONE_RULES |
                                      CLOCK_LIMIT_DAYLIGHT_STATE_ONLY;

    if (!source_accepts_provenance(observation.source,
                                   observation.provenance)) {
        reading.interpretation_status = InterpretationStatus::Unsupported;
        return;
    }

    const bool has_offset = provenance_has_offset(observation.provenance);
    if (has_offset && !observation.utc_offset_known) {
        reading.interpretation_status = InterpretationStatus::InvalidMetadata;
        reading.utc_available = provenance_is_utc(observation.provenance);
        if (reading.utc_available) reading.utc_calendar = observation.calendar;
        return;
    }
    if (observation.utc_offset_known && !has_offset) {
        reading.interpretation_status = InterpretationStatus::InvalidMetadata;
        return;
    }
    if (has_offset && !offset_is_valid(observation.utc_offset_minutes)) {
        reading.interpretation_status = InterpretationStatus::InvalidMetadata;
        reading.utc_available = provenance_is_utc(observation.provenance);
        if (reading.utc_available) reading.utc_calendar = observation.calendar;
        reading.local_available =
            observation.provenance == ClockProvenance::FirmwareLocalWithOffset
                ? true : false;
        if (reading.local_available) reading.local_calendar = observation.calendar;
        return;
    }

    const bool utc_is_source_calendar = provenance_is_utc(observation.provenance) &&
        observation.provenance != ClockProvenance::FirmwareLocalWithOffset;
    const bool local_is_source_calendar =
        (provenance_is_local(observation.provenance) && !utc_is_source_calendar) ||
        observation.provenance == ClockProvenance::FirmwareLocalWithOffset;

    if (utc_is_source_calendar) {
        reading.utc_available = true;
        reading.utc_calendar = observation.calendar;
    }
    if (local_is_source_calendar) {
        reading.local_available = true;
        reading.local_calendar = observation.calendar;
    }
    if (has_offset) {
        reading.utc_offset_known = true;
        reading.utc_offset_minutes = observation.utc_offset_minutes;
        if (utc_is_source_calendar) {
            reading.local_available = shift_calendar_minutes(
                reading.utc_calendar, observation.utc_offset_minutes,
                reading.local_calendar);
        } else if (observation.provenance ==
                   ClockProvenance::FirmwareLocalWithOffset) {
            reading.utc_available = shift_calendar_minutes(
                reading.local_calendar,
                -static_cast<int32_t>(observation.utc_offset_minutes),
                reading.utc_calendar);
        }
        if (!reading.local_available || !reading.utc_available) {
            reading.utc_offset_known = false;
            reading.utc_offset_minutes = 0;
            reading.interpretation_status = InterpretationStatus::InvalidMetadata;
            return;
        }
    }

    if (reading.local_available && reading.utc_available)
        reading.interpretation_status = InterpretationStatus::LocalAndUtc;
    else if (reading.local_available)
        reading.interpretation_status = InterpretationStatus::LocalOnly;
    else if (reading.utc_available)
        reading.interpretation_status = InterpretationStatus::UtcOnly;
    else
        reading.interpretation_status = InterpretationStatus::Unknown;
}

void read_clock_from_source(ClockSourceReader reader, void* user_data,
                            ClockReading& reading)
{
    reading = ClockReading{};
    if (reader == NULL) {
        reading.status = ClockReadStatus::Unavailable;
        return;
    }

    ClockObservation observation{};
    const ClockReadStatus source_status = reader(user_data, observation);
    if (source_status != ClockReadStatus::Ok) {
        reading.status = source_status == ClockReadStatus::Unavailable ||
                         source_status == ClockReadStatus::Invalid ||
                         source_status == ClockReadStatus::Unsupported
            ? source_status : ClockReadStatus::Unsupported;
        reading.source = observation.source;
        if (reading.status == ClockReadStatus::Unsupported)
            reading.interpretation_status = InterpretationStatus::Unsupported;
        return;
    }
    classify_clock_observation(observation, reading);
}

bool uefi_timezone_to_utc_offset(int16_t time_zone_minutes,
                                 int16_t& utc_offset_minutes)
{
    utc_offset_minutes = 0;
    if (static_cast<uint16_t>(time_zone_minutes) ==
        static_cast<uint16_t>(kUefiUnspecifiedTimezone)) return false;
    if (time_zone_minutes < -1440 || time_zone_minutes > 1440) return false;
    utc_offset_minutes = static_cast<int16_t>(-time_zone_minutes);
    return true;
}

void classify_uefi_time(const UefiTimeMetadata& metadata,
                        ClockReading& reading)
{
    initialize_reading(reading, ClockReadStatus::Invalid,
                       ClockSource::UefiRuntime);
    if (!is_valid_calendar(metadata.local_calendar)) return;

    const bool daylight_valid =
        (metadata.daylight_flags & static_cast<uint8_t>(~kUefiKnownDaylightFlags)) == 0u;
    int16_t conventional_offset = 0;
    const bool timezone_known = uefi_timezone_to_utc_offset(
        metadata.time_zone_minutes, conventional_offset);
    ClockObservation observation{};
    observation.source = ClockSource::UefiRuntime;
    observation.provenance = timezone_known && daylight_valid
        ? ClockProvenance::FirmwareLocalWithOffset
        : ClockProvenance::FirmwareLocal;
    observation.calendar = metadata.local_calendar;
    observation.utc_offset_known = timezone_known && daylight_valid;
    observation.utc_offset_minutes = conventional_offset;
    observation.resolution_known = metadata.resolution_known;
    observation.resolution_nanoseconds = metadata.resolution_nanoseconds;
    observation.accuracy = metadata.accuracy_known
        ? ClockAccuracy::ReportedPartsPerMillion : ClockAccuracy::Unknown;
    observation.accuracy_parts_per_million =
        metadata.accuracy_parts_per_million;
    observation.daylight_flags = metadata.daylight_flags;
    observation.source_limitations = CLOCK_LIMIT_NO_TIMEZONE_RULES |
                                     CLOCK_LIMIT_DAYLIGHT_STATE_ONLY;
    classify_clock_observation(observation, reading);

    // UEFI GetTime defines its fields as local civil time even if the timezone
    // is unspecified or invalid. Invalid timezone/daylight metadata blocks UTC
    // derivation while preserving the valid firmware-local calendar.
    if (!timezone_known || !daylight_valid) {
        reading.local_available = true;
        reading.local_calendar = metadata.local_calendar;
        reading.utc_available = false;
        reading.utc_offset_known = false;
        reading.utc_offset_minutes = 0;
        reading.utc_calendar = DateTime{};
        reading.interpretation_status = daylight_valid
            ? (static_cast<uint16_t>(metadata.time_zone_minutes) ==
                       static_cast<uint16_t>(kUefiUnspecifiedTimezone)
                   ? InterpretationStatus::LocalOnly
                   : InterpretationStatus::InvalidMetadata)
            : InterpretationStatus::InvalidMetadata;
    }
}

bool calendar_to_unix_seconds(const DateTime& value, int64_t& seconds)
{
    seconds = 0;
    if (!is_valid_calendar(value)) return false;
    const int64_t days = days_from_civil(value.year, value.month, value.day);
    seconds = days * kSecondsPerDay + static_cast<int64_t>(value.hour) * 3600 +
        static_cast<int64_t>(value.minute) * 60 + value.second;
    return true;
}

bool unix_seconds_to_calendar(int64_t seconds, DateTime& value)
{
    value = DateTime{};
    int64_t days = seconds / kSecondsPerDay;
    int64_t day_seconds = seconds % kSecondsPerDay;
    if (day_seconds < 0) {
        day_seconds += kSecondsPerDay;
        --days;
    }

    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t day_of_era = static_cast<uint32_t>(z - era * 146097);
    const uint32_t year_of_era = (day_of_era - day_of_era / 1460u +
        day_of_era / 36524u - day_of_era / 146096u) / 365u;
    int64_t year = static_cast<int64_t>(year_of_era) + era * 400;
    const uint32_t day_of_year = day_of_era -
        (365u * year_of_era + year_of_era / 4u - year_of_era / 100u);
    const uint32_t month_prime = (5u * day_of_year + 2u) / 153u;
    const uint32_t day = day_of_year - (153u * month_prime + 2u) / 5u + 1u;
    const uint32_t month = month_prime < 10u
        ? month_prime + 3u : month_prime - 9u;
    year += month <= 2u ? 1 : 0;
    if (year < 1 || year > 9999) return false;

    value.year = static_cast<uint16_t>(year);
    value.month = static_cast<uint8_t>(month);
    value.day = static_cast<uint8_t>(day);
    value.hour = static_cast<uint8_t>(day_seconds / 3600);
    value.minute = static_cast<uint8_t>((day_seconds % 3600) / 60);
    value.second = static_cast<uint8_t>(day_seconds % 60);
    return is_valid_calendar(value);
}

bool shift_calendar_minutes(const DateTime& value, int32_t minutes,
                            DateTime& shifted)
{
    shifted = DateTime{};
    int64_t seconds = 0;
    if (!calendar_to_unix_seconds(value, seconds)) return false;
    const int64_t delta = static_cast<int64_t>(minutes) * 60;
    if ((delta > 0 && seconds > 0x7FFFFFFFFFFFFFFFLL - delta) ||
        (delta < 0 && seconds < (-0x7FFFFFFFFFFFFFFFLL - 1LL) - delta))
        return false;
    if (!unix_seconds_to_calendar(seconds + delta, shifted)) return false;
    shifted.nanosecond = value.nanosecond;
    return true;
}

bool pit_ticks_to_monotonic_nanoseconds(uint64_t ticks,
                                        uint32_t divisor,
                                        uint64_t base_nanoseconds,
                                        uint64_t& nanoseconds,
                                        uint32_t& resolution_nanoseconds)
{
    nanoseconds = 0u;
    resolution_nanoseconds = 0u;
    if (divisor == 0u || divisor > 0xFFFFu) return false;

    const uint64_t whole_groups = ticks / kPitInputFrequency;
    const uint64_t remainder_ticks = ticks % kPitInputFrequency;
    if (whole_groups > 0xFFFFFFFFFFFFFFFFULL / divisor) return false;
    const uint64_t remaining_whole_seconds =
        (remainder_ticks * divisor) / kPitInputFrequency;
    const uint64_t whole_group_seconds = whole_groups * divisor;
    if (remaining_whole_seconds > 0xFFFFFFFFFFFFFFFFULL - whole_group_seconds)
        return false;
    const uint64_t whole_seconds = whole_group_seconds + remaining_whole_seconds;
    const uint64_t remaining_cycles =
        (remainder_ticks * divisor) % kPitInputFrequency;
    const uint64_t fractional_nanoseconds =
        (remaining_cycles * 1000000000ULL) / kPitInputFrequency;
    if (whole_seconds > (0xFFFFFFFFFFFFFFFFULL - fractional_nanoseconds) /
                            1000000000ULL) return false;
    const uint64_t relative_nanoseconds =
        whole_seconds * 1000000000ULL + fractional_nanoseconds;
    if (base_nanoseconds > 0xFFFFFFFFFFFFFFFFULL - relative_nanoseconds) return false;

    const uint64_t divisor_nanoseconds =
        (static_cast<uint64_t>(divisor) * 1000000000ULL +
         kPitInputFrequency / 2u) / kPitInputFrequency;
    if (divisor_nanoseconds == 0u || divisor_nanoseconds > 0xFFFFFFFFu)
        return false;
    nanoseconds = base_nanoseconds + relative_nanoseconds;
    resolution_nanoseconds = static_cast<uint32_t>(divisor_nanoseconds);
    return true;
}

} // namespace time
} // namespace kernel
