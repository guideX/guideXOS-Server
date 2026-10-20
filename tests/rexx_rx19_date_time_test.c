#include "../rexx_builtin.h"

#include <stdio.h>
#include <string.h>

static size_t assertion_count;
static size_t dispatch_case_count;
static size_t property_case_count;
static size_t failure_count;

typedef struct FakeClock {
    RexxVMClockReading readings[64];
    RexxVMClockReadStatus statuses[64];
    size_t count;
    size_t calls;
} FakeClock;

typedef struct OutputBuffer {
    char bytes[4096];
    size_t length;
} OutputBuffer;

static int check(int condition, const char *description)
{
    ++assertion_count;
    if (condition) return 0;
    ++failure_count;
    fprintf(stderr, "FAIL: %s\n", description);
    return 1;
}

static int check_string(const RexxValue *value, const char *expected,
                        const char *description)
{
    size_t expected_length = expected != NULL ? strlen(expected) : 0u;
    return check(value != NULL && value->type == REXX_VALUE_STRING &&
                 value->as.string.length == expected_length &&
                 (expected_length == 0u ||
                  (value->as.string.bytes != NULL &&
                   memcmp(value->as.string.bytes, expected,
                          expected_length) == 0)), description);
}

static RexxVMClockReading reading(uint16_t year, uint8_t month, uint8_t day,
                                  uint8_t hour, uint8_t minute, uint8_t second,
                                  uint32_t fraction_ns, uint32_t wall_resolution,
                                  uint64_t monotonic_ns,
                                  uint32_t monotonic_resolution,
                                  RexxClockWallInterpretation interpretation,
                                  int32_t utc_offset_seconds)
{
    RexxVMClockReading out;
    memset(&out, 0, sizeof(out));
    out.capabilities = REXX_CLOCK_CAP_WALL_CIVIL | REXX_CLOCK_CAP_MONOTONIC;
    out.wall_interpretation = interpretation;
    out.utc_offset_seconds = utc_offset_seconds;
    out.wall_resolution_nanoseconds = wall_resolution;
    out.year = year;
    out.month = month;
    out.day = day;
    out.hour = hour;
    out.minute = minute;
    out.second = second;
    out.fractional_nanoseconds = fraction_ns;
    out.monotonic_nanoseconds = monotonic_ns;
    out.monotonic_resolution_nanoseconds = monotonic_resolution;
    return out;
}

static int fake_clock_push(FakeClock *clock, RexxVMClockReading value)
{
    if (clock == NULL || clock->count >=
            sizeof(clock->readings) / sizeof(clock->readings[0])) return 0;
    clock->readings[clock->count] = value;
    clock->statuses[clock->count] = REXX_VM_CLOCK_READ_OK;
    ++clock->count;
    return 1;
}

static int fake_clock_push_failure(FakeClock *clock,
                                  RexxVMClockReadStatus status)
{
    if (clock == NULL || clock->count >=
            sizeof(clock->readings) / sizeof(clock->readings[0])) return 0;
    memset(&clock->readings[clock->count], 0,
           sizeof(clock->readings[clock->count]));
    clock->statuses[clock->count] = status;
    ++clock->count;
    return 1;
}

static RexxVMClockReadStatus fake_clock_read(void *user_data,
                                             RexxVMClockReading *out)
{
    FakeClock *clock = (FakeClock *)user_data;
    size_t index;
    if (clock == NULL || out == NULL) return REXX_VM_CLOCK_READ_INVALID;
    index = clock->calls++;
    if (index >= clock->count) return REXX_VM_CLOCK_READ_UNAVAILABLE;
    *out = clock->readings[index];
    return clock->statuses[index];
}

static int collect_output(void *user_data, const char *bytes, size_t length)
{
    OutputBuffer *output = (OutputBuffer *)user_data;
    if (output == NULL || (bytes == NULL && length != 0u) ||
        length > sizeof(output->bytes) - output->length - 1u) return 0;
    if (length != 0u) memcpy(output->bytes + output->length, bytes, length);
    output->length += length;
    output->bytes[output->length] = '\0';
    return 1;
}

static RexxVMStatus execute_source(FakeClock *clock, const char *source,
                                  OutputBuffer *output, char *error,
                                  size_t error_capacity)
{
    RexxLexer lexer;
    RexxParser parser;
    RexxAstProgram *program = NULL;
    RexxBytecodeChunk *chunk = NULL;
    RexxCompilerError compiler_error;
    RexxVM vm;
    RexxVMStatus status = REXX_VM_STATUS_INVALID_ARGUMENT;
    memset(&lexer, 0, sizeof(lexer));
    memset(&parser, 0, sizeof(parser));
    memset(&compiler_error, 0, sizeof(compiler_error));
    memset(&vm, 0, sizeof(vm));
    if (output != NULL) memset(output, 0, sizeof(*output));
    if (error != NULL && error_capacity != 0u) error[0] = '\0';
    if (source == NULL || rexx_lexer_init(&lexer, source, strlen(source)) !=
                              REXX_LEXER_STATUS_OK) goto cleanup;
    if (rexx_parser_init(&parser, &lexer) != REXX_PARSER_STATUS_OK ||
        rexx_parser_parse(&parser, &program) != REXX_PARSER_STATUS_OK)
        goto cleanup;
    if (rexx_compiler_compile(program, &chunk, &compiler_error) !=
            REXX_COMPILER_STATUS_OK) goto cleanup;
    status = rexx_vm_init(&vm, 0u);
    if (status != REXX_VM_STATUS_OK) goto cleanup;
    if (clock != NULL) rexx_vm_set_clock_reader(&vm, fake_clock_read, clock);
    if (output != NULL)
        rexx_vm_set_output_writer(&vm, collect_output, output);
    status = rexx_vm_execute(&vm, chunk);
    if (status != REXX_VM_STATUS_OK && error != NULL && error_capacity != 0u) {
        const char *message = vm.error.message != NULL
            ? vm.error.message : rexx_vm_status_name(status);
        size_t length = strlen(message);
        if (length >= error_capacity) length = error_capacity - 1u;
        memcpy(error, message, length);
        error[length] = '\0';
    }
cleanup:
    if (vm.initialized) rexx_vm_free(&vm);
    rexx_bytecode_chunk_free(chunk);
    rexx_parser_free_ast(program);
    return status;
}

static RexxVMStatus dispatch_date(const char *output_option,
                                  const char *input_date,
                                  const char *input_option,
                                  RexxValue *result)
{
    RexxValue arguments[3] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    size_t count = output_option != NULL ? 1u : 0u;
    RexxVMStatus status = REXX_VM_STATUS_OK;
    if (output_option != NULL)
        status = rexx_value_set_string(&arguments[0], output_option,
                                       strlen(output_option));
    if (input_date != NULL) {
        count = 2u;
        if (status == REXX_VM_STATUS_OK)
            status = rexx_value_set_string(&arguments[1], input_date,
                                           strlen(input_date));
    }
    if (input_option != NULL) {
        count = 3u;
        if (status == REXX_VM_STATUS_OK)
            status = rexx_value_set_string(&arguments[2], input_option,
                                           strlen(input_option));
    }
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_builtin_dispatch("DATE", 4u, arguments, count, result);
        ++dispatch_case_count;
    }
    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);
    rexx_value_clear(&arguments[2]);
    return status;
}

static RexxVMStatus dispatch_time(const char *option, RexxVM *vm,
                                  RexxValue *result)
{
    RexxValue argument = REXX_VALUE_INIT;
    RexxVMStatus status = REXX_VM_STATUS_OK;
    size_t count = option != NULL ? 1u : 0u;
    if (option != NULL)
        status = rexx_value_set_string(&argument, option, strlen(option));
    if (status == REXX_VM_STATUS_OK) {
        status = rexx_builtin_dispatch_with_vm(vm, "TIME", 4u,
            option != NULL ? &argument : NULL, count, result);
        ++dispatch_case_count;
    }
    rexx_value_clear(&argument);
    return status;
}

static int test_date_reference_vectors(void)
{
    RexxValue result = REXX_VALUE_INIT;
    int failed = 0;
    RexxVMStatus status;

    status = dispatch_date("B", "1 Jan 0001", "N", &result);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 DATE parses the base origin");
    failed += check_string(&result, "0", "RX19 Base day begins at 0001-01-01");
    rexx_value_clear(&result);

    status = dispatch_date("S", "0", "B", &result);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 DATE converts base zero to Standard");
    failed += check_string(&result, "00010101", "RX19 base zero round trips");
    rexx_value_clear(&result);

    status = dispatch_date("B", "29 Feb 2000", "N", &result);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 DATE accepts leap day in 2000");
    failed += check_string(&result, "730178", "RX19 leap-century Base reference");
    rexx_value_clear(&result);

    status = dispatch_date("D", "20000229", "S", &result);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 DATE accepts the leap day in Standard input");
    failed += check_string(&result, "60", "RX19 leap day is day 60");
    rexx_value_clear(&result);

    status = dispatch_date("D", "20240229", "S", &result);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 DATE accepts leap year 2024");
    failed += check_string(&result, "60", "RX19 2024 day-of-year reference");
    rexx_value_clear(&result);

    status = dispatch_date("D", "20250228", "S", &result);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 DATE accepts non-leap year 2025");
    failed += check_string(&result, "59", "RX19 2025 day-of-year reference");
    rexx_value_clear(&result);

    status = dispatch_date("S", "19991231", "S", &result);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 DATE preserves year-end Standard input");
    failed += check_string(&result, "19991231", "RX19 year boundary is stable");
    rexx_value_clear(&result);

    status = dispatch_date("S", "2024-02-29", "S", &result);
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                    "RX19 DATE rejects noncanonical separators");
    rexx_value_clear(&result);

    status = dispatch_date("B", "01 Jan 0001", "N", &result);
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                    "RX19 DATE rejects a leading zero in Normal input");
    rexx_value_clear(&result);

    status = dispatch_date("B", "1 JAN 0001", "N", &result);
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                    "RX19 DATE requires IBM mixed-case month abbreviations");
    rexx_value_clear(&result);

    status = dispatch_date("B", "29 Feb 1900", "N", &result);
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                    "RX19 DATE rejects 1900-02-29");
    rexx_value_clear(&result);

    status = dispatch_date("S", "19000229", "S", &result);
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                    "RX19 DATE rejects invalid Standard dates");
    rexx_value_clear(&result);

    status = dispatch_date("J", "20000229", "S", &result);
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                    "RX19 DATE rejects Julian as an output conversion");
    rexx_value_clear(&result);

    status = dispatch_date("Standard", "20240229", "S", &result);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 DATE option suffix follows classic first-letter rules");
    failed += check_string(&result, "20240229", "RX19 Standard conversion output");
    rexx_value_clear(&result);
    return failed;
}

static uint32_t independent_base_day(uint16_t year, uint8_t month, uint8_t day)
{
    uint32_t total = 0u;
    uint16_t y;
    uint8_t m;
    for (y = 1u; y < year; ++y)
        total += ((y % 4u == 0u && y % 100u != 0u) || y % 400u == 0u)
            ? 366u : 365u;
    for (m = 1u; m < month; ++m) {
        static const uint8_t lengths[] = {
            31u, 28u, 31u, 30u, 31u, 30u,
            31u, 31u, 30u, 31u, 30u, 31u
        };
        total += lengths[m - 1u];
        if (m == 2u &&
            ((year % 4u == 0u && year % 100u != 0u) || year % 400u == 0u))
            ++total;
    }
    return total + day - 1u;
}

static void independent_base_to_standard(uint32_t base_day, char output[10])
{
    uint16_t year = 1u;
    uint8_t month = 1u;
    uint8_t day = 1u;
    static const uint8_t lengths[] = {
        31u, 28u, 31u, 30u, 31u, 30u,
        31u, 31u, 30u, 31u, 30u, 31u
    };
    while (year < 9999u) {
        uint32_t days = ((year % 4u == 0u && year % 100u != 0u) ||
                         year % 400u == 0u) ? 366u : 365u;
        if (base_day < days) break;
        base_day -= days;
        ++year;
    }
    while (month <= 12u) {
        uint32_t days = lengths[month - 1u];
        if (month == 2u &&
            ((year % 4u == 0u && year % 100u != 0u) || year % 400u == 0u))
            ++days;
        if (base_day < days) break;
        base_day -= days;
        ++month;
    }
    day = (uint8_t)(base_day + 1u);
    (void)snprintf(output, 10u, "%04u%02u%02u",
                   (unsigned)year, (unsigned)month, (unsigned)day);
}

static int test_calendar_roundtrip_properties(void)
{
    static const uint32_t max_base = 3652058u;
    uint32_t state = UINT32_C(0x52583139);
    uint32_t iteration;
    int failed = 0;
    for (iteration = 0u; iteration < 2048u; ++iteration) {
        char input[16];
        char expected[10];
        char returned_base[16];
        RexxValue to_standard = REXX_VALUE_INIT;
        RexxValue to_base = REXX_VALUE_INIT;
        uint32_t base;
        int written;
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        base = state % (max_base + 1u);
        written = snprintf(input, sizeof(input), "%u", base);
        independent_base_to_standard(base, expected);
        if (written < 0 || (size_t)written >= sizeof(input)) {
            failed += check(0, "RX19 Base property input formats safely");
            continue;
        }
        failed += check(dispatch_date("S", input, "B", &to_standard) ==
                            REXX_VM_STATUS_OK,
                        "RX19 Base property converts to Standard");
        failed += check_string(&to_standard, expected,
                               "RX19 Base conversion matches independent calendar oracle");
        written = snprintf(returned_base, sizeof(returned_base), "%s",
                          input);
        (void)written;
        rexx_value_clear(&to_standard);
        failed += check(dispatch_date("B", expected, "S", &to_base) ==
                            REXX_VM_STATUS_OK,
                        "RX19 Standard property converts back to Base");
        failed += check_string(&to_base, returned_base,
                               "RX19 Base round trip preserves the day number");
        rexx_value_clear(&to_base);
        ++property_case_count;
    }

    failed += check(independent_base_day(1u, 1u, 1u) == 0u,
                    "RX19 independent oracle starts at Base day zero");
    failed += check(independent_base_day(2000u, 2u, 29u) == 730178u,
                    "RX19 independent oracle validates 2000-02-29");
    failed += check(independent_base_day(2024u, 2u, 29u) == 738944u,
                    "RX19 independent oracle validates 2024-02-29");
    failed += check(independent_base_day(9999u, 12u, 31u) == max_base,
                    "RX19 independent oracle validates the upper calendar boundary");
    return failed;
}

static int test_two_digit_year_window(void)
{
    FakeClock clock;
    OutputBuffer output;
    RexxVMStatus status;
    int failed = 0;
    memset(&clock, 0, sizeof(clock));
    failed += check(fake_clock_push(&clock, reading(2026u, 10u, 20u,
        12u, 0u, 0u, 0u, 1000000000u, 0u, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0)),
        "RX19 year-window fake reading is queued");
    status = execute_source(&clock,
        "say date('S','76/01/01','O') date('S','75/12/31','O')\n",
        &output, NULL, 0u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 rolling-window conversions execute");
    failed += check(strcmp(output.bytes, "19760101 20751231\n") == 0,
                    "RX19 2-digit dates use current-year minus 50 through plus 49");
    failed += check(clock.calls == 1u,
                    "RX19 2-digit conversions share one clause snapshot");
    return failed;
}

static int test_current_date_and_clause_snapshot(void)
{
    FakeClock clock;
    OutputBuffer output;
    RexxVMStatus status;
    int failed = 0;
    memset(&clock, 0, sizeof(clock));
    fake_clock_push(&clock, reading(2024u, 12u, 31u, 23u, 59u, 59u,
        0u, 1000000000u, 10000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    status = execute_source(&clock, "say date('S') time('N')\n",
                            &output, NULL, 0u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 DATE and TIME current values execute");
    failed += check(strcmp(output.bytes, "20241231 23:59:59\n") == 0,
                    "RX19 DATE and TIME in one clause describe one instant");
    failed += check(clock.calls == 1u,
                    "RX19 one clause takes one provider snapshot");

    memset(&clock, 0, sizeof(clock));
    fake_clock_push(&clock, reading(2024u, 2u, 29u, 8u, 0u, 0u,
        0u, 1000000000u, 0u, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    status = execute_source(&clock,
        "say date('C') date('J') date('M') date('W')\n",
        &output, NULL, 0u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 current-only DATE formats execute");
    failed += check(strcmp(output.bytes,
        "8826 24060 February Thursday\n") == 0,
        "RX19 current-only century, Julian, month and weekday use the injected date");
    failed += check(clock.calls == 1u,
                    "RX19 current-only DATE formats share one snapshot");

    memset(&clock, 0, sizeof(clock));
    fake_clock_push(&clock, reading(2024u, 12u, 31u, 23u, 59u, 59u,
        0u, 1000000000u, 10000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2025u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 10010000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    status = execute_source(&clock,
        "say date('S'); say time('N')\n", &output, NULL, 0u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 separate clauses execute across midnight");
    failed += check(strcmp(output.bytes,
        "20241231\n00:00:00\n") == 0,
        "RX19 a new clause gets a new midnight snapshot");
    failed += check(clock.calls == 2u,
                    "RX19 distinct semicolon clauses read independently");
    return failed;
}

static int test_time_options_and_precision(void)
{
    FakeClock clock;
    OutputBuffer output;
    RexxVMStatus status;
    int failed = 0;
    memset(&clock, 0, sizeof(clock));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 1000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 11u, 59u, 59u,
        0u, 1000000000u, 2000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 12u, 0u, 0u,
        0u, 1000000000u, 3000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 23u, 59u, 59u,
        0u, 1000000000u, 4000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 11u, 59u, 59u,
        123456789u, 1000u, 5000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    status = execute_source(&clock,
        "say time('C')\nsay time('C')\nsay time('C')\n"
        "say time('N')\nsay time('H') time('M') time('S') time('L')\n",
        &output, NULL, 0u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 TIME format options execute");
    failed += check(strcmp(output.bytes,
        "12:00am\n11:59am\n12:00pm\n23:59:59\n"
        "11 719 43199 11:59:59.123456\n") == 0,
        "RX19 TIME civil, counters, long format and fractional precision match references");
    failed += check(clock.calls == 5u,
                    "RX19 TIME H/M/S/L share the final clause snapshot");
    return failed;
}

static int test_fixed_offset_boundaries(void)
{
    FakeClock clock;
    OutputBuffer output;
    RexxVMStatus status;
    char error[128];
    int failed = 0;
    memset(&clock, 0, sizeof(clock));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 0u, 30u, 0u,
        0u, 1000000000u, 0u, 10000000u,
        REXX_CLOCK_WALL_UTC_FIXED_OFFSET, -3600));
    status = execute_source(&clock, "say date('S') time('N')\n",
                            &output, NULL, 0u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 fixed UTC offset reading executes");
    failed += check(strcmp(output.bytes, "20231231 23:30:00\n") == 0,
                    "RX19 fixed offset crosses the year boundary correctly");

    memset(&clock, 0, sizeof(clock));
    fake_clock_push(&clock, reading(2024u, 12u, 31u, 23u, 30u, 0u,
        0u, 1000000000u, 0u, 10000000u,
        REXX_CLOCK_WALL_UTC_FIXED_OFFSET, 3600));
    status = execute_source(&clock, "say date('S') time('N')\n",
                            &output, NULL, 0u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 positive fixed UTC offset reading executes");
    failed += check(strcmp(output.bytes, "20250101 00:30:00\n") == 0,
                    "RX19 positive fixed offset crosses the year boundary correctly");

    memset(&clock, 0, sizeof(clock));
    fake_clock_push(&clock, reading(2024u, 2u, 29u, 12u, 0u, 0u,
        0u, 1000000000u, 0u, 10000000u,
        REXX_CLOCK_WALL_UNSPECIFIED, 0));
    status = execute_source(&clock, "say date()\n", &output,
                            error, sizeof(error));
    failed += check(status == REXX_VM_STATUS_CLOCK_UNAVAILABLE,
                    "RX19 valid civil fields with unknown interpretation stay unavailable");
    failed += check(strstr(error, "clock source unavailable") != NULL ||
                    strstr(error, "Clock source unavailable") != NULL,
                    "RX19 unspecified interpretation produces the documented clock error");

    {
        const int32_t invalid_offsets[] = { 86400, -86400, 86460, -86460 };
        size_t i;
        for (i = 0u; i < sizeof(invalid_offsets) / sizeof(invalid_offsets[0]); ++i) {
            memset(&clock, 0, sizeof(clock));
            fake_clock_push(&clock, reading(2024u, 2u, 29u, 12u, 0u, 0u,
                0u, 1000000000u, 0u, 10000000u,
                REXX_CLOCK_WALL_UTC_FIXED_OFFSET, invalid_offsets[i]));
            status = execute_source(&clock, "say date()\n", &output,
                                    error, sizeof(error));
            failed += check(status == REXX_VM_STATUS_CLOCK_UNAVAILABLE,
                            "RX19 out-of-range provider offset is rejected deterministically");
            failed += check(clock.calls == 1u,
                            "RX19 invalid provider offset is read once for its clause");
        }
    }
    return failed;
}

static int test_elapsed_timer_and_wall_jumps(void)
{
    FakeClock clock;
    OutputBuffer output;
    RexxVMStatus status;
    int failed = 0;
    memset(&clock, 0, sizeof(clock));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 12u, 0u, 0u,
        0u, 1000000000u, 10000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2024u, 1u, 2u, 12u, 0u, 0u,
        0u, 1000000000u, 11250000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2023u, 12u, 30u, 12u, 0u, 0u,
        0u, 1000000000u, 13000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2024u, 1u, 3u, 12u, 0u, 0u,
        0u, 1000000000u, 13750000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    status = execute_source(&clock,
        "say time('E')\nsay time('E')\nsay time('E')\nsay time('E')\n",
        &output, NULL, 0u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 elapsed timer survives wall-clock jumps");
    failed += check(strcmp(output.bytes,
        "0.000000\n1.250000\n3.000000\n3.750000\n") == 0,
        "RX19 elapsed time follows monotonic nanoseconds, not wall time");
    return failed;
}

static int test_elapsed_reset_and_clause_consistency(void)
{
    FakeClock clock;
    OutputBuffer output;
    RexxVMStatus status;
    int failed = 0;
    memset(&clock, 0, sizeof(clock));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 1000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 2250000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 2750000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 3000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    status = execute_source(&clock,
        "say time('E')\nsay time('R')\nsay time('E')\nsay time('R')\n",
        &output, NULL, 0u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 elapsed reset options execute");
    failed += check(strcmp(output.bytes,
        "0.000000\n1.250000\n0.500000\n0.750000\n") == 0,
        "RX19 TIME R returns the interval and resets its baseline");

    memset(&clock, 0, sizeof(clock));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 1000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 2250000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    status = execute_source(&clock,
        "say time('E')\nsay time('E') time('E')\n",
        &output, NULL, 0u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 repeated elapsed calls execute");
    failed += check(strcmp(output.bytes,
        "0.000000\n1.250000 1.250000\n") == 0,
        "RX19 elapsed calls in a clause use one value");
    failed += check(clock.calls == 2u,
                    "RX19 repeated elapsed calls take one clause read");
    return failed;
}

static int test_internal_routine_clock_isolation(void)
{
    FakeClock clock;
    OutputBuffer output;
    RexxVMStatus status;
    int failed = 0;
    memset(&clock, 0, sizeof(clock));
    fake_clock_push(&clock, reading(2024u, 12u, 31u, 23u, 59u, 59u,
        0u, 1000000000u, 1000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2025u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 2000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    status = execute_source(&clock,
        "say time('N') helper() time('N')\nexit\n"
        "helper:\nreturn date('S')\n",
        &output, NULL, 0u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 internal function call executes");
    failed += check(strcmp(output.bytes,
        "23:59:59 20250101 23:59:59\n") == 0,
        "RX19 internal routine gets its clause snapshot and caller resumes its original snapshot");
    failed += check(clock.calls == 2u,
                    "RX19 routine call takes one additional snapshot");

    memset(&clock, 0, sizeof(clock));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 1000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 3000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&clock, reading(2024u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 4000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    status = execute_source(&clock,
        "say time('E')\ncall sub\nsay time('E')\nexit\n"
        "sub:\nsay time('R')\nreturn\n",
        &output, NULL, 0u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 elapsed timer internal-call test executes");
    failed += check(strcmp(output.bytes,
        "0.000000\n2.000000\n3.000000\n") == 0,
        "RX19 internal routine reset does not change caller elapsed state");
    return failed;
}

static int test_clock_failure_recovery_and_isolation(void)
{
    FakeClock failed_clock;
    FakeClock first_vm_clock;
    FakeClock second_vm_clock;
    OutputBuffer output;
    RexxVMStatus status;
    int failed = 0;
    char error[128];

    memset(&failed_clock, 0, sizeof(failed_clock));
    fake_clock_push_failure(&failed_clock, REXX_VM_CLOCK_READ_UNAVAILABLE);
    fake_clock_push(&failed_clock, reading(2024u, 2u, 29u, 12u, 0u, 0u,
        0u, 1000000000u, 0u, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    status = execute_source(&failed_clock,
        "signal on syntax name no_clock\nsay time('N')\nexit 1\n"
        "no_clock:\nsignal off syntax\nsay 'recovered'\nsay date('S')\n",
        &output, error, sizeof(error));
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 failed clock read is trapped and later calls recover");
    failed += check(strcmp(output.bytes, "recovered\n20240229\n") == 0,
                    "RX19 failed read does not retain a stale valid snapshot");
    failed += check(failed_clock.calls == 2u,
                    "RX19 recovery takes a fresh clock read in the next clause");

    memset(&first_vm_clock, 0, sizeof(first_vm_clock));
    memset(&second_vm_clock, 0, sizeof(second_vm_clock));
    fake_clock_push(&first_vm_clock, reading(2024u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 1000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&second_vm_clock, reading(2025u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 9000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    status = execute_source(&first_vm_clock, "say time('E')\n",
                            &output, NULL, 0u);
    failed += check(status == REXX_VM_STATUS_OK &&
                     strcmp(output.bytes, "0.000000\n") == 0,
                    "RX19 first independent VM starts its own elapsed timer");
    status = execute_source(&second_vm_clock, "say time('E')\n",
                            &output, NULL, 0u);
    failed += check(status == REXX_VM_STATUS_OK &&
                     strcmp(output.bytes, "0.000000\n") == 0,
                    "RX19 second VM starts an independent elapsed timer");

    status = execute_source(NULL, "say date()\n", &output,
                            error, sizeof(error));
    failed += check(status == REXX_VM_STATUS_CLOCK_UNAVAILABLE,
                    "RX19 missing production clock returns a deterministic status");
    failed += check(strstr(error, "clock source unavailable") != NULL ||
                    strstr(error, "Clock source unavailable") != NULL,
                    "RX19 unavailable clock diagnostic is explicit");
    return failed;
}

static int test_error_limits_and_allocation_recovery(void)
{
    RexxValue result = REXX_VALUE_INIT;
    RexxValue arguments[3] = {
        REXX_VALUE_INIT, REXX_VALUE_INIT, REXX_VALUE_INIT
    };
    FakeClock overflow_clock;
    OutputBuffer output;
    RexxVM vm;
    RexxVMStatus status;
    int failed = 0;

    status = dispatch_date("Q", NULL, NULL, &result);
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                    "RX19 DATE rejects unknown option strings");
    status = dispatch_time("Q", NULL, &result);
    failed += check(status == REXX_VM_STATUS_INVALID_FUNCTION_CALL,
                    "RX19 TIME rejects unknown option strings");
    rexx_value_clear(&result);

    status = rexx_value_set_string(&arguments[0], "S", 1u);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_string(&arguments[1], "20240229", 8u);
    if (status == REXX_VM_STATUS_OK)
        status = rexx_value_set_string(&arguments[2], "S", 1u);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 allocation recovery inputs are created");
#if defined(REXX_VM_TEST_ALLOCATOR) && !defined(GXOS_BARE_METAL)
    size_t live_before = rexx_vm_test_allocator_live_blocks();
    rexx_vm_test_allocator_fail_on(1u);
    status = rexx_builtin_dispatch("DATE", 4u, arguments, 3u, &result);
    rexx_vm_test_allocator_disable();
    failed += check(status == REXX_VM_STATUS_OUT_OF_MEMORY &&
                     result.type == REXX_VALUE_UNSET,
                    "RX19 DATE allocation failure publishes no partial result");
    failed += check(rexx_vm_test_allocator_live_blocks() == live_before,
                    "RX19 DATE allocation failure leaves only argument ownership");
#endif
    status = rexx_builtin_dispatch("DATE", 4u, arguments, 3u, &result);
    failed += check(status == REXX_VM_STATUS_OK,
                    "RX19 DATE succeeds after allocation failure");
    failed += check_string(&result, "20240229",
                           "RX19 DATE recovery output is complete");
    rexx_value_clear(&arguments[0]);
    rexx_value_clear(&arguments[1]);
    rexx_value_clear(&arguments[2]);
    rexx_value_clear(&result);

    memset(&overflow_clock, 0, sizeof(overflow_clock));
    fake_clock_push(&overflow_clock, reading(2024u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 0u, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    fake_clock_push(&overflow_clock, reading(2024u, 1u, 1u, 0u, 0u, 0u,
        0u, 1000000000u, 1000000000000000000ull, 10000000u,
        REXX_CLOCK_WALL_LOCAL_CIVIL, 0));
    memset(&vm, 0, sizeof(vm));
    status = rexx_vm_init(&vm, 0u);
    if (status == REXX_VM_STATUS_OK) {
        RexxLexer lexer;
        RexxParser parser;
        RexxAstProgram *program = NULL;
        RexxBytecodeChunk *chunk = NULL;
        RexxCompilerError compiler_error;
        const char source[] = "say time('E')\nsay time('E')\n";
        memset(&lexer, 0, sizeof(lexer));
        memset(&parser, 0, sizeof(parser));
        memset(&compiler_error, 0, sizeof(compiler_error));
        if (rexx_lexer_init(&lexer, source, sizeof(source) - 1u) ==
                REXX_LEXER_STATUS_OK &&
            rexx_parser_init(&parser, &lexer) == REXX_PARSER_STATUS_OK &&
            rexx_parser_parse(&parser, &program) == REXX_PARSER_STATUS_OK &&
            rexx_compiler_compile(program, &chunk, &compiler_error) ==
                REXX_COMPILER_STATUS_OK) {
            rexx_vm_set_clock_reader(&vm, fake_clock_read, &overflow_clock);
            rexx_vm_set_output_writer(&vm, collect_output, &output);
            memset(&output, 0, sizeof(output));
            status = rexx_vm_execute(&vm, chunk);
        } else {
            status = REXX_VM_STATUS_INVALID_BYTECODE;
        }
        failed += check(status == REXX_VM_STATUS_RESOURCE_LIMIT,
                        "RX19 elapsed output rejects values beyond nine digits");
        rexx_bytecode_chunk_free(chunk);
        rexx_parser_free_ast(program);
        rexx_vm_free(&vm);
    } else {
        failed += check(0, "RX19 overflow VM initializes");
    }
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_date_reference_vectors();
    failed += test_calendar_roundtrip_properties();
    failed += test_two_digit_year_window();
    failed += test_current_date_and_clause_snapshot();
    failed += test_time_options_and_precision();
    failed += test_fixed_offset_boundaries();
    failed += test_elapsed_timer_and_wall_jumps();
    failed += test_elapsed_reset_and_clause_consistency();
    failed += test_internal_routine_clock_isolation();
    failed += test_clock_failure_recovery_and_isolation();
    failed += test_error_limits_and_allocation_recovery();
    printf("RX19_DIRECT_ASSERTIONS=%zu\n", assertion_count);
    printf("RX19_DIRECT_CASES=%zu\n", dispatch_case_count);
    printf("RX19_PROPERTY_CASES=%zu\n", property_case_count);
    printf("RX19_PROPERTY_SEED=0x52583139\n");
    printf("RX19_FAILURES=%zu\n", failure_count);
    return failed == 0 ? 0 : 1;
}
