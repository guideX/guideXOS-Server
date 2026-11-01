#define GXOS_DM37_INPUT_DIAGNOSTICS
#define GXOS_DM37_INPUT_DIAGNOSTICS_POSTCHECK_ONLY
#define GXOS_DM37_INPUT_DIAGNOSTICS_TEST
#include "../kernel/core/include/kernel/dm37_input_diagnostics.h"

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char* name)
{
    if (condition) return;
    ++failures;
    std::printf("FAIL: %s\n", name);
}

} // namespace

int main()
{
    using namespace kernel::dm37_input_diagnostics;

    check(is_trace_key(9) && is_trace_key(10) && is_trace_key(13) &&
          is_trace_key(27) && is_trace_key(32) &&
          is_trace_key(0x102) && is_trace_key(0x103) &&
          is_trace_key(0x113) && is_trace_key('`') &&
          is_trace_key('~'),
          "trace filter includes navigation, Close, and Shell activation keys");
    check(!is_trace_key('a') && !is_trace_key(8) &&
          !is_trace_key(0x100) && !is_trace_key(0x101),
          "trace filter excludes ordinary text and unrelated keys");

    check(!capture_armed() && begin_event() == 0 &&
          !should_trace_key('`'),
          "post-checkpoint capture stays quiet before a relevant Disk Manager transition");
    arm_selection_focus_checkpoint();
    const uint32_t checkpointSequence = begin_event();
    check(capture_armed() && checkpointSequence != 0 &&
          should_trace_key('`'),
          "first selected partition arms a fresh bounded focus trace checkpoint");

    for (uint32_t i = 0; i < MAX_TRACE_LINES; ++i)
        emit("selection-test", i + 1u, i, 0);
    emit("newest-dropped", MAX_TRACE_LINES + 1u, MAX_TRACE_LINES, 0);
    check(reserved_record_count() == MAX_TRACE_LINES &&
          records()[0].sequence == 1u &&
          records()[MAX_TRACE_LINES - 1u].sequence == MAX_TRACE_LINES &&
          budget().droppedLines == 1u &&
          loss_summary_pending(budget(), MAX_TRACE_LINES, MAX_TRACE_LINES),
          "fixed trace retains the oldest 512 records, drops/counts newest records, and marks an incomplete drained trace");
    record_loss_summary_emitted(budget());
    check(!loss_summary_pending(budget(), MAX_TRACE_LINES, MAX_TRACE_LINES),
          "loss summary does not repeat while its counters stay unchanged");
    __atomic_store_n(&budget().droppedLines, static_cast<uint32_t>(2),
                     __ATOMIC_RELAXED);
    const bool doubledLossReported = loss_summary_pending(
        budget(), MAX_TRACE_LINES, MAX_TRACE_LINES);
    record_loss_summary_emitted(budget());
    __atomic_store_n(&budget().droppedLines, static_cast<uint32_t>(3),
                     __ATOMIC_RELAXED);
    const bool intermediateLossQuiet = !loss_summary_pending(
        budget(), MAX_TRACE_LINES, MAX_TRACE_LINES);
    __atomic_store_n(&budget().droppedLines, static_cast<uint32_t>(4),
                     __ATOMIC_RELAXED);
    check(doubledLossReported && intermediateLossQuiet &&
          loss_summary_pending(budget(), MAX_TRACE_LINES, MAX_TRACE_LINES),
          "overflow summaries report new loss at bounded doubling thresholds");
    arm_postcheck_focus_checkpoint();
    check(reserved_record_count() == MAX_TRACE_LINES &&
          records()[0].sequence == 1u,
          "later FAT32 result checkpoint does not erase the active selection trace");

    TraceBudget sequenceBudget = {};
    sequenceBudget.nextSequence = 0xFFFFFFFEu;
    check(sequenceBudget.begin_event() == 0xFFFFFFFFu &&
          sequenceBudget.begin_event() == 1u,
          "event sequences skip zero at wraparound");

    TraceBudget lineBudget = {};
    bool allAccepted = true;
    for (uint16_t i = 0; i < MAX_TRACE_LINES; ++i)
        allAccepted = lineBudget.claim_line(false) && allAccepted;
    check(allAccepted && !lineBudget.claim_line(false) &&
          lineBudget.droppedLines == 1,
          "trace overflow is bounded and counted as dropped evidence");

    TraceBudget pointerBudget = {};
    allAccepted = true;
    for (uint8_t i = 0; i < MAX_POINTER_MOTION_LINES; ++i)
        allAccepted = pointerBudget.claim_line(true) && allAccepted;
    check(allAccepted && !pointerBudget.claim_line(true) &&
          pointerBudget.droppedPointerMotionLines == 1 &&
          pointerBudget.claim_line(false),
          "pointer-motion omissions are counted without consuming trace budget");
    std::printf("DM37 input diagnostic checks: %s (%d failures)\n",
        failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
