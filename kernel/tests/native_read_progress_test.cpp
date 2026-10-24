#include "kernel/native_read_progress.h"

#include <cstdint>
#include <iostream>

namespace {
using kernel::native_read_progress::State;
static const uint64_t kInterval = kernel::native_read_progress::kCheckpointInterval;
static uint32_t g_checks = 0;
static uint32_t g_failures = 0;

void check(bool passed, const char* label)
{
    ++g_checks;
    if (passed) std::cout << "PASS: " << label << '\n';
    else {
        ++g_failures;
        std::cout << "FAIL: " << label << '\n';
    }
}

void add_checkpoints(State& state, uint64_t offset, uint32_t bytes,
                     bool successful, uint64_t* checkpoints, uint32_t& count)
{
    uint64_t first = 0;
    const uint32_t crossed = kernel::native_read_progress::observe(
        state, offset, bytes, successful, &first);
    for (uint32_t i = 0; i < crossed && count < 256u; ++i)
        checkpoints[count++] = first + static_cast<uint64_t>(i) * kInterval;
}
}

int main()
{
    uint64_t points[256] = {};
    uint32_t count = 0;
    State state{};
    kernel::native_read_progress::reset(state);
    for (uint64_t offset = 0; offset < 3u * kInterval; offset += 65536u)
        add_checkpoints(state, offset, 65536u, true, points, count);
    check(count == 3u && points[0] == kInterval && points[1] == 2u * kInterval &&
          points[2] == 3u * kInterval,
          "sequential reads emit every 1 MiB checkpoint exactly once");

    kernel::native_read_progress::reset(state);
    count = 0;
    add_checkpoints(state, 9u * kInterval, 0u, true, points, count); // EOF probe first
    for (uint64_t offset = 0; offset < 2u * kInterval; offset += 65536u)
        add_checkpoints(state, offset, 65536u, true, points, count);
    check(count == 2u && points[0] == kInterval && points[1] == 2u * kInterval,
          "EOF probe before first read does not advance checkpoints");

    kernel::native_read_progress::reset(state);
    count = 0;
    uint64_t offset = 0;
    while (offset < kInterval) {
        add_checkpoints(state, offset, 65536u, true, points, count);
        offset += 65536u;
    }
    add_checkpoints(state, offset, 8192u, true, points, count); // partial final chunk
    check(count == 1u && points[0] == kInterval,
          "partial final chunk records only the observed crossed threshold");

    kernel::native_read_progress::reset(state);
    count = 0;
    add_checkpoints(state, 0u, 300000u, true, points, count);
    add_checkpoints(state, 300000u, 200000u, true, points, count); // short read then resume
    add_checkpoints(state, 500000u, 600000u, true, points, count);
    add_checkpoints(state, 1100000u, 100000u, true, points, count);
    check(count == 1u && points[0] == kInterval && state.lastEnd == 1200000u,
          "short reads advance by returned bytes and resume without skipping a threshold");

    kernel::native_read_progress::reset(state);
    count = 0;
    add_checkpoints(state, 0u, 262144u, true, points, count);
    add_checkpoints(state, 8u * kInterval, 262144u, true, points, count); // seek baseline
    for (uint32_t i = 1; i <= 8u; ++i)
        add_checkpoints(state, 8u * kInterval + i * 262144u, 262144u,
                        true, points, count);
    check(count == 2u && points[0] == kInterval && points[1] == 2u * kInterval,
          "nonsequential seek is excluded and resumed sequential reads are counted");

    kernel::native_read_progress::reset(state);
    count = 0;
    add_checkpoints(state, 0u, 512u, true, points, count);
    add_checkpoints(state, 512u, 0u, true, points, count); // EOF/no progress
    add_checkpoints(state, 512u, 1024u, true, points, count);
    check(count == 0u && state.lastEnd == 1536u,
          "zero-progress reads do not move or terminate an active sequence");

    kernel::native_read_progress::reset(state);
    count = 0;
    add_checkpoints(state, 0u, 512u * 1024u, true, points, count);
    add_checkpoints(state, 512u * 1024u, 1u, false, points, count); // failure termination
    add_checkpoints(state, 512u * 1024u, 512u * 1024u, true, points, count);
    add_checkpoints(state, 1024u * 1024u, 512u * 1024u, true, points, count);
    check(count == 0u && state.sequentialBytes == 512u * 1024u,
          "failed read resets progress and later nonzero seek starts a fresh baseline");

    kernel::native_read_progress::reset(state);
    count = 0;
    for (uint64_t i = 0; i < 5u; ++i)
        add_checkpoints(state, i * kInterval, static_cast<uint32_t>(kInterval),
                        true, points, count);
    bool ordered = count == 5u;
    for (uint32_t i = 0; ordered && i < count; ++i)
        ordered = points[i] == static_cast<uint64_t>(i + 1u) * kInterval;
    check(ordered, "successive boundary-sized reads have no duplicate or skipped thresholds");

    kernel::native_read_progress::reset(state);
    count = 0;
    uint32_t diagnosticRecords = 0;
    for (uint64_t offsetBytes = 0; offsetBytes < 120u * kInterval; offsetBytes += 65536u) {
        uint64_t first = 0;
        const uint32_t crossed = kernel::native_read_progress::observe(
            state, offsetBytes, 65536u, true, &first);
        for (uint32_t i = 0; i < crossed; ++i) {
            (void)first;
            if (diagnosticRecords < kernel::native_read_progress::kMaxDiagnosticRecords)
                ++diagnosticRecords;
        }
    }
    check(diagnosticRecords == kernel::native_read_progress::kMaxDiagnosticRecords,
          "diagnostic record count remains capped at the production bound");

    uint64_t first = 0;
    kernel::native_read_progress::reset(state);
    const uint32_t overflowCrossings = kernel::native_read_progress::observe(
        state, UINT64_MAX - 3u, 8u, true, &first);
    check(overflowCrossings == 0u && !state.hasLastEnd,
          "offset overflow terminates progress tracking safely");

    std::cout << "nativeReadProgressChecks=" << g_checks - g_failures << '/'
              << g_checks << " failures=" << g_failures << '\n';
    return g_failures == 0 ? 0 : 1;
}
