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

    check(is_control_key(9) && is_control_key(10) && is_control_key(13) &&
          is_control_key(27) && is_control_key(32) &&
          is_control_key(0x102) && is_control_key(0x103),
          "trace filter includes the Disk Manager navigation and activation keys");
    check(!is_control_key('a') && !is_control_key(8) &&
          !is_control_key(0x100) && !is_control_key(0x101),
          "trace filter excludes printable text and unrelated keys");

    TraceBudget sequenceBudget = {};
    sequenceBudget.nextSequence = 0xFFFFFFFEu;
    check(sequenceBudget.begin_event() == 0xFFFFFFFFu &&
          sequenceBudget.begin_event() == 1u,
          "event sequences skip zero at wraparound");

    TraceBudget lineBudget = {};
    bool allAccepted = true;
    for (uint16_t i = 0; i < MAX_TRACE_LINES; ++i)
        allAccepted = lineBudget.claim_line(false) && allAccepted;
    check(allAccepted && !lineBudget.claim_line(false),
          "trace line budget is bounded at its configured maximum");

    TraceBudget pointerBudget = {};
    allAccepted = true;
    for (uint8_t i = 0; i < MAX_POINTER_MOTION_LINES; ++i)
        allAccepted = pointerBudget.claim_line(true) && allAccepted;
    check(allAccepted && !pointerBudget.claim_line(true) &&
          pointerBudget.claim_line(false),
          "pointer-motion cap drops extra motion without consuming remaining trace budget");

    std::printf("DM37 input diagnostic checks: %s (%d failures)\n",
        failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
