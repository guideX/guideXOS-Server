#include "../kernel/core/include/kernel/time.h"

#include <atomic>
#include <cstdio>
#include <thread>

using namespace kernel::time;

static uint32_t g_checks = 0u;

#define CHECK(condition) do { \
    ++g_checks; \
    if (!(condition)) { \
        std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        return false; \
    } \
} while (0)

static DateTime date_time(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour = 0u, uint8_t minute = 0u,
                          uint8_t second = 0u, uint32_t nanosecond = 0u)
{
    DateTime value{};
    value.year = year;
    value.month = month;
    value.day = day;
    value.hour = hour;
    value.minute = minute;
    value.second = second;
    value.nanosecond = nanosecond;
    return value;
}

static bool equals(const DateTime& left, const DateTime& right)
{
    return left.year == right.year && left.month == right.month &&
        left.day == right.day && left.hour == right.hour &&
        left.minute == right.minute && left.second == right.second &&
        left.nanosecond == right.nanosecond;
}

static CmosRegisterSnapshot cmos_sample()
{
    CmosRegisterSnapshot sample{};
    sample.seconds = 0x56;
    sample.minutes = 0x34;
    sample.hours = 0x23;
    sample.day = 0x29;
    sample.month = 0x02;
    sample.year = 0x24;
    sample.status_b = 0x02; // BCD, 24-hour mode.
    sample.status_d = 0x80; // RTC time and RAM valid.
    return sample;
}

static ClockObservation observation(ClockSource source,
                                   ClockProvenance provenance,
                                   const DateTime& calendar)
{
    ClockObservation value{};
    value.source = source;
    value.provenance = provenance;
    value.calendar = calendar;
    value.resolution_known = true;
    value.resolution_nanoseconds = UINT32_C(1000000000);
    value.accuracy = ClockAccuracy::Unknown;
    return value;
}

static bool test_calendar_and_cmos()
{
    DateTime decoded{};
    CmosRegisterSnapshot sample = cmos_sample();
    CHECK(decode_cmos_snapshot(sample, decoded) == CmosDecodeStatus::Ok);
    CHECK(equals(decoded, date_time(2024, 2, 29, 23, 34, 56)));

    sample.status_b = 0x00; // BCD, 12-hour mode.
    sample.hours = 0x91; // 11 PM.
    CHECK(decode_cmos_snapshot(sample, decoded) == CmosDecodeStatus::Ok);
    CHECK(decoded.hour == 23u);
    sample.hours = 0x92; // 12 PM.
    CHECK(decode_cmos_snapshot(sample, decoded) == CmosDecodeStatus::Ok);
    CHECK(decoded.hour == 12u);
    sample.hours = 0x12; // 12 AM.
    CHECK(decode_cmos_snapshot(sample, decoded) == CmosDecodeStatus::Ok);
    CHECK(decoded.hour == 0u);

    sample.status_b = 0x06; // Binary, 24-hour mode.
    sample.hours = 23u;
    sample.seconds = 59u;
    sample.minutes = 34u;
    sample.day = 29u;
    sample.month = 2u;
    sample.year = 24u;
    CHECK(decode_cmos_snapshot(sample, decoded) == CmosDecodeStatus::Ok);
    CHECK(equals(decoded, date_time(2024, 2, 29, 23, 34, 59)));

    sample = cmos_sample();
    sample.status_a = 0x80;
    CHECK(decode_cmos_snapshot(sample, decoded) ==
          CmosDecodeStatus::UpdateInProgress);
    CHECK(decoded.year == 0u && decoded.month == 0u);
    sample = cmos_sample();
    sample.status_d = 0u;
    CHECK(decode_cmos_snapshot(sample, decoded) ==
          CmosDecodeStatus::InvalidValidityStatus);
    sample = cmos_sample();
    sample.seconds = 0x6A;
    CHECK(decode_cmos_snapshot(sample, decoded) == CmosDecodeStatus::InvalidBcd);
    sample = cmos_sample();
    sample.day = 0x30;
    CHECK(decode_cmos_snapshot(sample, decoded) == CmosDecodeStatus::InvalidCalendar);
    sample = cmos_sample();
    sample.year = 0x23;
    CHECK(decode_cmos_snapshot(sample, decoded) == CmosDecodeStatus::InvalidCalendar);
    sample = cmos_sample();
    sample.status_b = 0u;
    sample.hours = 0x00;
    CHECK(decode_cmos_snapshot(sample, decoded) == CmosDecodeStatus::InvalidHour);

    CHECK(is_valid_calendar(date_time(2000, 2, 29)));
    CHECK(!is_valid_calendar(date_time(1900, 2, 29)));
    CHECK(!is_valid_calendar(date_time(2024, 13, 1)));
    CHECK(!is_valid_calendar(date_time(2024, 4, 31)));
    CHECK(!is_valid_calendar(date_time(2024, 1, 1, 24)));
    CHECK(!is_valid_calendar(date_time(2024, 1, 1, 0, 0, 0, 1000000000u)));
    return true;
}

static bool test_capability_matrix()
{
    ClockReading reading{};
    ClockObservation input = observation(ClockSource::CmosRtc,
        ClockProvenance::Unknown, date_time(2024, 2, 29, 12, 0, 0));
    classify_clock_observation(input, reading);
    CHECK(reading.status == ClockReadStatus::Ok);
    CHECK(reading.source == ClockSource::CmosRtc);
    CHECK(reading.raw_available && reading.calendar_valid);
    CHECK(!reading.local_available && !reading.utc_available);
    CHECK(!reading.utc_offset_known && reading.utc_offset_minutes == 0);
    CHECK(reading.provenance == ClockProvenance::Unknown);
    CHECK(reading.resolution_known &&
          reading.resolution_nanoseconds == UINT32_C(1000000000));
    CHECK(reading.accuracy == ClockAccuracy::Unknown);
    CHECK((reading.source_limitations & CLOCK_LIMIT_RTC_NO_CENTURY_REGISTER) != 0u);

    input.calendar = date_time(2024, 2, 30);
    classify_clock_observation(input, reading);
    CHECK(reading.status == ClockReadStatus::Invalid);
    CHECK(!reading.raw_available && !reading.local_available && !reading.utc_available);
    CHECK(reading.raw_calendar.year == 0u);

    input = observation(ClockSource::InjectedTest,
        ClockProvenance::SourceDefinedLocal, date_time(2025, 1, 2, 3, 4, 5));
    classify_clock_observation(input, reading);
    CHECK(reading.raw_available && reading.local_available && !reading.utc_available);
    CHECK(reading.interpretation_status == InterpretationStatus::LocalOnly);

    input = observation(ClockSource::InjectedTest,
        ClockProvenance::SourceDefinedUtc, date_time(2025, 1, 2, 3, 4, 5));
    classify_clock_observation(input, reading);
    CHECK(reading.raw_available && !reading.local_available && reading.utc_available);
    CHECK(reading.interpretation_status == InterpretationStatus::UtcOnly);

    input = observation(ClockSource::InjectedTest,
        ClockProvenance::SourceDefinedUtcWithOffset,
        date_time(2024, 12, 31, 23, 30, 0));
    input.utc_offset_known = true;
    input.utc_offset_minutes = 60;
    classify_clock_observation(input, reading);
    CHECK(reading.raw_available && reading.local_available && reading.utc_available);
    CHECK(reading.utc_offset_known && reading.utc_offset_minutes == 60);
    CHECK(equals(reading.local_calendar, date_time(2025, 1, 1, 0, 30, 0)));

    input = observation(ClockSource::CmosRtc,
        ClockProvenance::ConfiguredRtcUtc, date_time(2025, 1, 2, 3, 4, 5));
    classify_clock_observation(input, reading);
    CHECK(reading.utc_available && !reading.local_available);
    CHECK(reading.interpretation_basis == InterpretationBasis::OperatorConfiguration);
    CHECK((reading.source_limitations &
           CLOCK_LIMIT_CONFIGURED_SETTING_UNVERIFIED) != 0u);

    input = observation(ClockSource::InjectedTest,
        ClockProvenance::Unknown, date_time(2025, 1, 2));
    input.utc_offset_known = true; // Offset cannot make unknown provenance trustworthy.
    input.utc_offset_minutes = 0;
    classify_clock_observation(input, reading);
    CHECK(reading.raw_available && !reading.local_available && !reading.utc_available);
    CHECK(reading.interpretation_status == InterpretationStatus::InvalidMetadata);

    input.source = ClockSource::Pit;
    classify_clock_observation(input, reading);
    CHECK(reading.status == ClockReadStatus::Unsupported);
    CHECK(!reading.raw_available && !reading.local_available && !reading.utc_available);
    return true;
}

static bool test_uefi_metadata_and_conversion()
{
    ClockReading reading{};
    int16_t offset = 123;
    CHECK(!uefi_timezone_to_utc_offset(2047, offset));
    CHECK(offset == 0);
    CHECK(uefi_timezone_to_utc_offset(0, offset) && offset == 0);
    CHECK(uefi_timezone_to_utc_offset(-480, offset) && offset == 480);
    CHECK(uefi_timezone_to_utc_offset(330, offset) && offset == -330);
    CHECK(uefi_timezone_to_utc_offset(1440, offset) && offset == -1440);
    CHECK(uefi_timezone_to_utc_offset(-1440, offset) && offset == 1440);
    CHECK(!uefi_timezone_to_utc_offset(1441, offset));
    CHECK(!uefi_timezone_to_utc_offset(-1441, offset));

    UefiTimeMetadata metadata{};
    metadata.local_calendar = date_time(2024, 1, 1, 8, 0, 0, 250000000u);
    metadata.time_zone_minutes = -480;
    metadata.daylight_flags = 0u;
    metadata.resolution_known = true;
    metadata.resolution_nanoseconds = 1000000000u;
    metadata.accuracy_known = false;
    classify_uefi_time(metadata, reading);
    CHECK(reading.source == ClockSource::UefiRuntime && reading.raw_available);
    CHECK(reading.local_available && reading.utc_available &&
          reading.utc_offset_known);
    CHECK(reading.utc_offset_minutes == 480);
    CHECK(equals(reading.utc_calendar, date_time(2024, 1, 1, 0, 0, 0, 250000000u)));
    CHECK(reading.accuracy == ClockAccuracy::Unknown);

    metadata.local_calendar = date_time(2024, 3, 1, 3, 30, 0);
    metadata.time_zone_minutes = 330;
    classify_uefi_time(metadata, reading);
    CHECK(reading.utc_offset_known && reading.utc_offset_minutes == -330);
    CHECK(equals(reading.utc_calendar, date_time(2024, 3, 1, 9, 0, 0)));

    metadata.local_calendar = date_time(2024, 5, 1, 10, 15, 0);
    metadata.time_zone_minutes = 0;
    classify_uefi_time(metadata, reading);
    CHECK(reading.utc_offset_known && reading.utc_offset_minutes == 0);
    CHECK(reading.local_available && reading.utc_available);

    metadata.time_zone_minutes = 2047;
    classify_uefi_time(metadata, reading);
    CHECK(reading.raw_available && reading.local_available);
    CHECK(!reading.utc_available && !reading.utc_offset_known);
    CHECK(reading.interpretation_status == InterpretationStatus::LocalOnly);

    metadata.time_zone_minutes = 1441;
    classify_uefi_time(metadata, reading);
    CHECK(reading.local_available && !reading.utc_available);
    CHECK(reading.interpretation_status == InterpretationStatus::InvalidMetadata);
    metadata.time_zone_minutes = -1441;
    classify_uefi_time(metadata, reading);
    CHECK(reading.local_available && !reading.utc_available);
    CHECK(reading.interpretation_status == InterpretationStatus::InvalidMetadata);

    // UEFI local fields are already the source's local result. A set daylight
    // bit is preserved as metadata; no extra hour is added during conversion.
    metadata.local_calendar = date_time(2024, 7, 1, 9, 30, 0);
    metadata.time_zone_minutes = -240;
    metadata.daylight_flags = 0x03;
    classify_uefi_time(metadata, reading);
    CHECK(reading.local_available && reading.utc_available);
    CHECK(equals(reading.local_calendar, date_time(2024, 7, 1, 9, 30, 0)));
    CHECK(equals(reading.utc_calendar, date_time(2024, 7, 1, 5, 30, 0)));
    metadata.daylight_flags = 0x04;
    classify_uefi_time(metadata, reading);
    CHECK(reading.local_available && !reading.utc_available);
    CHECK(reading.interpretation_status == InterpretationStatus::InvalidMetadata);

    metadata.daylight_flags = 0u;
    metadata.local_calendar = date_time(2024, 2, 30);
    classify_uefi_time(metadata, reading);
    CHECK(reading.status == ClockReadStatus::Invalid && !reading.raw_available);

    DateTime shifted{};
    CHECK(shift_calendar_minutes(date_time(2024, 1, 1, 0, 30), -60, shifted));
    CHECK(equals(shifted, date_time(2023, 12, 31, 23, 30)));
    CHECK(shift_calendar_minutes(date_time(2024, 2, 29, 23, 30), 60, shifted));
    CHECK(equals(shifted, date_time(2024, 3, 1, 0, 30)));
    CHECK(shift_calendar_minutes(date_time(2024, 12, 31, 23, 30), 60, shifted));
    CHECK(equals(shifted, date_time(2025, 1, 1, 0, 30)));
    CHECK(!shift_calendar_minutes(date_time(1, 1, 1), -1, shifted));
    return true;
}

static bool test_epoch_and_pit_math()
{
    int64_t seconds = INT64_C(-1);
    CHECK(calendar_to_unix_seconds(date_time(1970, 1, 1), seconds));
    CHECK(seconds == 0);
    CHECK(calendar_to_unix_seconds(date_time(2000, 1, 1), seconds));
    CHECK(seconds == INT64_C(946684800));
    CHECK(calendar_to_unix_seconds(date_time(2024, 2, 29), seconds));
    CHECK(seconds == INT64_C(1709164800));
    DateTime converted{};
    CHECK(unix_seconds_to_calendar(INT64_C(1709164800), converted));
    CHECK(equals(converted, date_time(2024, 2, 29)));
    CHECK(unix_seconds_to_calendar(INT64_C(-1), converted));
    CHECK(equals(converted, date_time(1969, 12, 31, 23, 59, 59)));
    CHECK(!calendar_to_unix_seconds(date_time(2024, 2, 30), seconds));

    uint64_t nanoseconds = 0u;
    uint32_t resolution = 0u;
    CHECK(pit_ticks_to_monotonic_nanoseconds(
        UINT64_C(1193182), 1u, 0u, nanoseconds, resolution));
    CHECK(nanoseconds == UINT64_C(1000000000));
    CHECK(resolution == 838u);
    CHECK(pit_ticks_to_monotonic_nanoseconds(
        UINT64_C(1193182), 1u, UINT64_C(5000000000), nanoseconds, resolution));
    CHECK(nanoseconds == UINT64_C(6000000000));
    CHECK(!pit_ticks_to_monotonic_nanoseconds(
        UINT64_MAX, 0xFFFFu, 0u, nanoseconds, resolution));
    CHECK(!pit_ticks_to_monotonic_nanoseconds(
        1u, 1u, UINT64_MAX, nanoseconds, resolution));
    CHECK(!pit_ticks_to_monotonic_nanoseconds(
        1u, 0u, 0u, nanoseconds, resolution));
    CHECK(nanoseconds == 0u && resolution == 0u);
    return true;
}

static bool test_failure_transition_and_concurrency()
{
    ClockReading reading{};
    ClockObservation valid = observation(ClockSource::InjectedTest,
        ClockProvenance::SourceDefinedLocal, date_time(2025, 6, 7, 8, 9, 10));
    classify_clock_observation(valid, reading);
    CHECK(reading.local_available && reading.raw_calendar.year == 2025u);

    ClockObservation unavailable{};
    unavailable.source = ClockSource::None;
    unavailable.provenance = ClockProvenance::Unknown;
    classify_clock_observation(unavailable, reading);
    CHECK(reading.status == ClockReadStatus::Unsupported);
    CHECK(!reading.raw_available && reading.raw_calendar.year == 0u);
    CHECK(!reading.local_available && !reading.utc_available);

    // Configuration transitions are reclassified per reading. A configured
    // intent is not recorded as physical RTC correctness.
    ClockObservation configured = observation(ClockSource::CmosRtc,
        ClockProvenance::ConfiguredRtcLocal, date_time(2025, 6, 7, 8, 9, 10));
    classify_clock_observation(configured, reading);
    CHECK(reading.local_available && !reading.utc_available);
    CHECK(reading.interpretation_basis == InterpretationBasis::OperatorConfiguration);
    CHECK((reading.source_limitations &
           CLOCK_LIMIT_CONFIGURED_SETTING_UNVERIFIED) != 0u);
    configured.provenance = ClockProvenance::Unknown;
    classify_clock_observation(configured, reading);
    CHECK(!reading.local_available && !reading.utc_available);

    struct InjectedSourceState {
        ClockReadStatus status;
        ClockObservation next;
    } source_state{};
    const auto injected_reader = [](void* data, ClockObservation& next) -> ClockReadStatus {
        InjectedSourceState* state = static_cast<InjectedSourceState*>(data);
        next = state->next;
        return state->status;
    };
    source_state.status = ClockReadStatus::Ok;
    source_state.next = valid;
    read_clock_from_source(injected_reader, &source_state, reading);
    CHECK(reading.status == ClockReadStatus::Ok && reading.local_available);
    source_state.status = ClockReadStatus::Unavailable;
    read_clock_from_source(injected_reader, &source_state, reading);
    CHECK(reading.status == ClockReadStatus::Unavailable);
    CHECK(!reading.raw_available && !reading.local_available && !reading.utc_available);
    CHECK(reading.raw_calendar.year == 0u && reading.local_calendar.year == 0u);
    source_state.status = ClockReadStatus::Invalid;
    read_clock_from_source(injected_reader, &source_state, reading);
    CHECK(reading.status == ClockReadStatus::Invalid && !reading.raw_available);
    source_state.status = ClockReadStatus::Ok;
    source_state.next.calendar = date_time(2026, 1, 1, 0, 0, 1);
    read_clock_from_source(injected_reader, &source_state, reading);
    CHECK(reading.status == ClockReadStatus::Ok && reading.local_available);
    CHECK(reading.raw_calendar.year == 2026u);

    std::atomic<uint32_t> failures(0u);
    std::thread readers[4];
    for (uint32_t thread_index = 0u; thread_index < 4u; ++thread_index) {
        readers[thread_index] = std::thread([thread_index, &failures]() {
            for (uint32_t i = 0u; i < 250u; ++i) {
                const uint16_t year = static_cast<uint16_t>(2024u +
                    ((thread_index + i) % 5u));
                ClockObservation concurrent = observation(ClockSource::InjectedTest,
                    (i & 1u) != 0u ? ClockProvenance::SourceDefinedLocal
                                   : ClockProvenance::SourceDefinedUtc,
                    date_time(year, 2, 29));
                if (year % 4u != 0u) concurrent.calendar = date_time(year, 3, 1);
                ClockReading result{};
                classify_clock_observation(concurrent, result);
                const bool expect_local = (i & 1u) != 0u;
                const bool expect_utc = !expect_local;
                if (!result.raw_available || result.local_available != expect_local ||
                    result.utc_available != expect_utc)
                    failures.fetch_add(1u, std::memory_order_relaxed);
            }
        });
    }
    for (std::thread& reader : readers) reader.join();
    CHECK(failures.load(std::memory_order_relaxed) == 0u);
    return true;
}

int main()
{
    if (!test_calendar_and_cmos() || !test_capability_matrix() ||
        !test_uefi_metadata_and_conversion() || !test_epoch_and_pit_math() ||
        !test_failure_transition_and_concurrency()) return 1;
    std::printf("RX20 clock contract: PASS (%u checks)\n", g_checks);
    return 0;
}
