#include "../kernel/core/include/kernel/shell_focus_recovery.h"

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
    using kernel::shell_focus_recovery::State;

    const State inactiveOpen = {true, false, false};
    const State closed = kernel::shell_focus_recovery::after_escape(
        inactiveOpen, 27);
    check(!closed.open && closed.active && !closed.minimized,
          "Escape closes an open inactive Shell through the normal desktop path");

    const State reopened = kernel::shell_focus_recovery::after_grave(
        closed, '`');
    check(reopened.open && reopened.active && !reopened.minimized,
          "the existing grave shortcut reopens a closed Shell as active");

    const State backtickWhileInactive =
        kernel::shell_focus_recovery::after_grave(inactiveOpen, '`');
    check(backtickWhileInactive.open && !backtickWhileInactive.active,
          "an open inactive Shell must be closed before the grave shortcut can restore it");

    const State unrelated = kernel::shell_focus_recovery::after_escape(
        inactiveOpen, 'a');
    check(unrelated.open == inactiveOpen.open &&
          unrelated.active == inactiveOpen.active &&
          unrelated.minimized == inactiveOpen.minimized,
          "ordinary text input does not alter Shell activation state");

    std::printf("DM37 Shell focus checks: %s (%d failures)\n",
        failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
