#include "kernel/desktop_redraw_state.h"
#include "kernel/desktop_input_probe_state.h"

static unsigned g_checks = 0;
static unsigned g_firstFailedCheck = 0;

static bool check(bool condition, const char* name) {
    ++g_checks;
    if (!condition && g_firstFailedCheck == 0) g_firstFailedCheck = g_checks;
    (void)name;
    return condition;
}

int main() {
    kernel::desktop::RedrawRequestState state;
    bool ok = true;
    ok &= check(!kernel::desktop::can_present_lifecycle_redraw(false, true),
                "lifecycle repaint requires initialized desktop");
    ok &= check(!kernel::desktop::can_present_lifecycle_redraw(true, false),
                "lifecycle repaint requires framebuffer");
    ok &= check(kernel::desktop::can_present_lifecycle_redraw(true, true),
                "lifecycle repaint is allowed at an explicit removal boundary");
    ok &= check(!kernel::desktop::should_present_lifecycle_redraw(false, true, true),
                "lifecycle repaint remains idle without a request");
    ok &= check(!kernel::desktop::should_present_lifecycle_redraw(true, false, true),
                "pending lifecycle repaint waits for desktop initialization");
    ok &= check(kernel::desktop::should_present_lifecycle_redraw(true, true, true),
                "pending lifecycle repaint presents when framebuffer is ready");
    ok &= check(!state.consume(), "no repaint before invalidation");
    ok &= check(state.generation() == 1, "initial redraw generation");

    state.request();
    ok &= check(state.consume(), "invalidation is consumed");
    ok &= check(state.generation() == 2, "consumed invalidation advances generation");
    ok &= check(!state.consume(), "one invalidation causes one repaint");

    state.request();
    state.request();
    ok &= check(state.consume(), "repeated invalidations coalesce");
    ok &= check(state.generation() == 3, "coalesced invalidations advance once");

    state.request();
    ok &= check(state.consume(), "later invalidation remains observable");
    ok &= check(state.generation() == 4, "later repaint advances generation");

    kernel::desktop::StudioPostCloseInputProbeState inputProbe;
    ok &= check(!inputProbe.observe_pointer_action(true, true),
                "post-close input probe is idle until armed");
    inputProbe.arm();
    ok &= check(inputProbe.armed() && !inputProbe.pointer_passed(),
                "arming resets post-close input evidence");
    ok &= check(!inputProbe.observe_pointer_action(false, true),
                "pointer evidence requires no remaining windows");
    ok &= check(!inputProbe.observe_pointer_action(true, false),
                "pointer evidence requires a desktop state change");
    ok &= check(!inputProbe.observe_keyboard_action(true, true),
                "keyboard evidence requires the pointer action first");
    ok &= check(inputProbe.observe_pointer_action(true, true) && inputProbe.pointer_passed(),
                "real desktop pointer action is recorded");
    ok &= check(!inputProbe.observe_pointer_action(true, true),
                "duplicate pointer evidence is rejected");
    inputProbe.arm();
    inputProbe.observe_pointer_action(true, true);
    ok &= check(!inputProbe.observe_keyboard_action(false, true),
                "keyboard evidence requires no remaining windows");
    ok &= check(!inputProbe.observe_keyboard_action(true, false),
                "keyboard evidence requires a desktop state change");
    ok &= check(inputProbe.observe_keyboard_action(true, true) && inputProbe.keyboard_passed(),
                "real desktop keyboard action is recorded");
    ok &= check(!inputProbe.armed(), "completed post-close probe disarms");
    ok &= check(!inputProbe.observe_keyboard_action(true, true),
                "duplicate keyboard evidence is rejected");

    if (g_firstFailedCheck != 0) return static_cast<int>(10 + g_firstFailedCheck);
    return g_checks == 27 ? 0 : 2;
}
