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
          "post-checkpoint capture stays quiet before the FAT32 result closes");
    arm_postcheck_focus_checkpoint();
    const uint32_t checkpointSequence = begin_event();
    check(capture_armed() && checkpointSequence != 0 &&
          should_trace_key('`'),
          "FAT32 result closure arms a fresh bounded focus trace checkpoint");

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
    check(loss_summary_pending(lineBudget, MAX_TRACE_LINES,
                               MAX_TRACE_LINES),
          "a drained trace with loss must emit an incomplete marker");

    std::printf("DM37 input diagnostic checks: %s (%d failures)\n",
        failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
