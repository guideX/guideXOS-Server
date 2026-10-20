#ifndef KERNEL_TIME_H
#define KERNEL_TIME_H

#include "types.h"

namespace kernel {
namespace time {

// Calendar fields supplied by a source. nanosecond is zero for the current
// CMOS adapter; it is present for future sources such as UEFI GetTime().
struct DateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint32_t nanosecond;
};

enum class ClockReadStatus : uint8_t {
    Ok = 0,
    Unavailable,
    Invalid,
    Unsupported
};

enum class ClockSource : uint8_t {
    None = 0,
    CmosRtc,
    UefiRuntime,
    InjectedTest,
    Pit
};

// Provenance records why a calendar may be interpreted. Configured RTC
// policies establish intent only; they do not prove that the physical RTC was
// set correctly. SourceDefined entries model an explicit source contract.
enum class ClockProvenance : uint8_t {
    Unknown = 0,
    ConfiguredRtcUtc,
    ConfiguredRtcLocal,
    ConfiguredRtcUtcWithOffset,
    SourceDefinedUtc,
    SourceDefinedLocal,
    SourceDefinedUtcWithOffset,
    FirmwareLocal,
    FirmwareLocalWithOffset
};

enum class InterpretationBasis : uint8_t {
    Unknown = 0,
    OperatorConfiguration,
    SourceContract,
    FirmwareContract
};

enum class InterpretationStatus : uint8_t {
    Unknown = 0,
    LocalOnly,
    UtcOnly,
    LocalAndUtc,
    InvalidMetadata,
    Unsupported
};

enum class ClockAccuracy : uint8_t {
    Unknown = 0,
    ReportedPartsPerMillion
};

enum ClockLimitation : uint32_t {
    CLOCK_LIMIT_NONE = 0,
    CLOCK_LIMIT_ACCURACY_UNKNOWN = 1u << 0,
    CLOCK_LIMIT_RTC_NO_CENTURY_REGISTER = 1u << 1,
    CLOCK_LIMIT_RTC_INTERPRETATION_UNKNOWN = 1u << 2,
    CLOCK_LIMIT_CONFIGURED_SETTING_UNVERIFIED = 1u << 3,
    CLOCK_LIMIT_NO_TIMEZONE_RULES = 1u << 4,
    CLOCK_LIMIT_DAYLIGHT_STATE_ONLY = 1u << 5
};

enum class CmosDecodeStatus : uint8_t {
    Ok = 0,
    UpdateInProgress,
    InvalidValidityStatus,
    InvalidBcd,
    InvalidHour,
    InvalidCalendar
};

struct CmosRegisterSnapshot {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t status_a;
    uint8_t status_b;
    uint8_t status_d;
};

struct ClockObservation {
    ClockSource source;
    ClockProvenance provenance;
    DateTime calendar;
    bool utc_offset_known;
    int16_t utc_offset_minutes; // Conventional sign: local = UTC + offset.
    bool resolution_known;
    uint32_t resolution_nanoseconds;
    ClockAccuracy accuracy;
    uint32_t accuracy_parts_per_million;
    uint8_t daylight_flags;
    uint32_t source_limitations;
};

typedef ClockReadStatus (*ClockSourceReader)(void* user_data,
                                              ClockObservation& observation);

struct UefiTimeMetadata {
    DateTime local_calendar;
    int16_t time_zone_minutes; // EFI_TIME.TimeZone; local = UTC - TimeZone.
    uint8_t daylight_flags;
    bool resolution_known;
    uint32_t resolution_nanoseconds;
    bool accuracy_known;
    uint32_t accuracy_parts_per_million;
};

struct ClockReading {
    ClockReadStatus status;
    ClockSource source;
    bool calendar_valid;
    bool raw_available;
    bool local_available;
    bool utc_available;
    bool utc_offset_known;
    int16_t utc_offset_minutes; // Conventional sign: local = UTC + offset.
    ClockProvenance provenance;
    InterpretationBasis interpretation_basis;
    InterpretationStatus interpretation_status;
    DateTime raw_calendar;
    DateTime local_calendar;
    DateTime utc_calendar;
    bool resolution_known;
    uint32_t resolution_nanoseconds;
    ClockAccuracy accuracy;
    uint32_t accuracy_parts_per_million;
    uint32_t source_limitations;
    uint8_t daylight_flags;

    // Monotonic data is independent of the calendar result and has no epoch.
    bool monotonic_available;
    ClockSource monotonic_source;
    uint64_t monotonic_nanoseconds;
    uint32_t monotonic_resolution_nanoseconds;
};

bool is_valid_calendar(const DateTime& value);
CmosDecodeStatus decode_cmos_snapshot(const CmosRegisterSnapshot& snapshot,
                                      DateTime& value);

// Classifies valid source observations from provenance. Capabilities are
// derived from source + provenance; callers cannot assert capability flags.
void classify_clock_observation(const ClockObservation& observation,
                                ClockReading& reading);
void read_clock_from_source(ClockSourceReader reader, void* user_data,
                            ClockReading& reading);

// Converts UEFI's signed TimeZone convention to the conventional UTC offset.
// EFI_UNSPECIFIED_TIMEZONE (0x07ff) is handled as unspecified, never as zero.
bool uefi_timezone_to_utc_offset(int16_t time_zone_minutes,
                                 int16_t& utc_offset_minutes);
void classify_uefi_time(const UefiTimeMetadata& metadata,
                        ClockReading& reading);

bool calendar_to_unix_seconds(const DateTime& value, int64_t& seconds);
bool unix_seconds_to_calendar(int64_t seconds, DateTime& value);
bool shift_calendar_minutes(const DateTime& value, int32_t minutes,
                            DateTime& shifted);

// Shared bounded conversion used by the PIT and deterministic host tests.
bool pit_ticks_to_monotonic_nanoseconds(uint64_t ticks,
                                        uint32_t divisor,
                                        uint64_t base_nanoseconds,
                                        uint64_t& nanoseconds,
                                        uint32_t& resolution_nanoseconds);

// Reads a current raw source sample and independent PIT elapsed value where
// available. A valid CMOS sample remains uninterpreted by default.
bool get_clock_reading(ClockReading& out);

// Compatibility accessor retained for existing desktop services. Its fields
// are raw CMOS calendar values; success does not establish local or UTC time.
bool get_current_datetime(DateTime& out);

} // namespace time
} // namespace kernel

#endif // KERNEL_TIME_H
