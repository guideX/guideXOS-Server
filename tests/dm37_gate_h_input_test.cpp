#include "../kernel/core/include/kernel/disk_manager_input_focus.h"
#include "../kernel/core/include/kernel/compositor_widget_press.h"
#include "../kernel/core/include/kernel/ps2keyboard_legacy_queue.h"
#include "../kernel/core/include/kernel/widget_activation.h"

#include <cstdio>

namespace {

int checks = 0;
int failures = 0;

void check(bool condition, const char* name)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::printf("FAIL: %s\n", name);
}

kernel::app::Widget button(int id)
{
    kernel::app::Widget widget;
    widget.id = id;
    widget.type = kernel::app::WidgetType::Button;
    widget.visible = true;
    widget.enabled = true;
    return widget;
}

struct ModalHarness {
    static const int confirmId = 41;
    static const int cancelId = 42;

    uint32_t context;
    bool open;
    bool confirming;
    bool retainResult;
    uint8_t focus;
    int operations;
    int cancellations;
    kernel::app::KernelWindow window;

    ModalHarness() : context(1), open(false), confirming(false),
                     retainResult(false), focus(2),
                     operations(0), cancellations(0)
    {
        window.widgetCount = 2;
        window.widgets[0] = button(confirmId);
        window.widgets[1] = button(cancelId);
    }

    void openConfirmation()
    {
        kernel::ps2keyboard::legacy_queue::advance_input_context(context);
        open = true;
        confirming = true;
        focus = 2;
        window.widgets[0].visible = true;
        window.widgets[0].enabled = true;
        window.widgets[1].visible = true;
        window.widgets[1].enabled = true;
    }

    void retire()
    {
        open = false;
        confirming = false;
        window.widgets[0].visible = false;
        window.widgets[1].visible = false;
        kernel::ps2keyboard::legacy_queue::advance_input_context(context);
    }

    void process(const kernel::ps2keyboard::KeyEvent& event)
    {
        using namespace kernel::apps::disk_manager_input_focus;
        using namespace kernel::ps2keyboard::legacy_queue;
        if (!event_is_current_context(event, context) || !open) return;
        if (!modal_key_event_allowed(event.key, event.repeated)) return;
        const MountConfirmationAction action =
            mount_confirmation_action(focus, event.key);
        if (action == MOUNT_CONFIRMATION_FOCUS_CHANGED) {
            focus = next_mount_confirmation_focus(focus);
            advance_input_context(context);
            return;
        }
        if (action != MOUNT_CONFIRMATION_CANCEL &&
            action != MOUNT_CONFIRMATION_CONFIRM) return;
        const bool isConfirm = action == MOUNT_CONFIRMATION_CONFIRM;
        const int target = isConfirm ? confirmId : cancelId;
        const int buttonFocus = isConfirm ? 3 : 2;
        const int focusedId = kernel::app::focused_button_widget_id_for_index(
            focus, buttonFocus, target);
        const auto click = [this, isConfirm](int widgetId) {
            using namespace kernel::apps::disk_manager_input_focus;
            const ModalButtonAction selected = modal_button_action(
                open && confirming, widgetId, confirmId, cancelId);
            if (selected == MODAL_BUTTON_CONFIRM && isConfirm) {
                ++operations;
                // Dismount transitions to its result dialog synchronously.
                if (retainResult) {
                    confirming = false;
                    window.widgets[confirmId == widgetId ? 0 : 1].enabled = false;
                    advance_input_context(context);
                } else {
                    retire();
                }
            } else if (selected == MODAL_BUTTON_CANCEL && !isConfirm) {
                ++cancellations;
                retire();
            }
        };
        if (event.key == 27)
            dispatch_button_activation(&window, target, click);
        else
            dispatch_focused_button_activation(&window, target, focusedId,
                                               event.key, click);
    }

    kernel::ps2keyboard::KeyEvent key(uint32_t value, bool repeated = false) const
    {
        kernel::ps2keyboard::KeyEvent event = {};
        event.key = value;
        event.action = kernel::ps2keyboard::KeyAction::Down;
        event.inputContextGeneration = context;
        event.repeated = repeated;
        return event;
    }
};

} // namespace

int main()
{
    using namespace kernel::apps::disk_manager_input_focus;
    using namespace kernel::ps2keyboard;
    using namespace kernel::ps2keyboard::legacy_queue;

    ModalHarness mount;
    const KeyEvent beforeOpen = mount.key('\n');
    mount.openConfirmation();
    check(!event_is_current_context(beforeOpen, mount.context),
          "opening confirmation invalidates keys already queued in the prior UI context");
    check(mount.context == 2 && mount.open && mount.confirming &&
          mount.operations == 0,
          "Mount confirmation opens with a fresh context and performs no operation");

    const KeyEvent delayedTab = mount.key('\t');
    const KeyEvent delayedEnter = mount.key('\n');
    mount.process(delayedTab);
    check(mount.focus == 3 && mount.operations == 0 &&
          mount.context == 3,
          "Tab changes current focus and retires already queued activation input");
    mount.process(delayedEnter);
    check(mount.open && mount.focus == 3 && mount.operations == 0,
          "Enter queued against the old focus cannot confirm after focus changes");

    ModalHarness windowFocus;
    windowFocus.openConfirmation();
    const KeyEvent queuedBeforeWindowFocus = windowFocus.key('\n');
    advance_input_context(windowFocus.context);
    windowFocus.process(queuedBeforeWindowFocus);
    check(windowFocus.open && windowFocus.operations == 0 &&
          !event_is_current_context(queuedBeforeWindowFocus,
                                   windowFocus.context),
          "window focus transfer invalidates queued input from the prior focused widget");

    const KeyEvent confirmThenCancel = mount.key(27);
    const KeyEvent deliberateConfirm = mount.key('\n');
    mount.process(deliberateConfirm);
    mount.process(confirmThenCancel);
    check(!mount.open && mount.operations == 1 && mount.cancellations == 0 &&
          !event_is_current_context(confirmThenCancel, mount.context),
          "Confirm retires a queued Cancel before it can affect the result modal");
    check(!mount.open && mount.operations == 1 && mount.cancellations == 0,
          "fresh deliberate Enter activates the focused Mount control exactly once");
    mount.process(deliberateConfirm);
    check(mount.operations == 1,
          "duplicate Enter from the dismissed Mount confirmation is stale");

    mount.openConfirmation();
    const uint32_t cancelContext = mount.context;
    mount.process(mount.key('\n'));
    check(!mount.open && mount.operations == 1 &&
          mount.cancellations == 1 && mount.context == cancelContext + 1,
          "Enter on the default Cancel control dismisses without Mount");
    const KeyEvent oldConfirm = mount.key('\n');
    mount.openConfirmation();
    mount.process(oldConfirm);
    check(mount.open && mount.operations == 1 && mount.focus == 2,
          "a queued confirmation from before Cancel cannot activate a reopened dialog");

    const KeyEvent repeatedCancel = mount.key(27);
    mount.process(repeatedCancel);
    mount.process(repeatedCancel);
    check(!mount.open && mount.operations == 1 && mount.cancellations == 2,
          "Escape dispatches Cancel once and its delayed duplicate is stale");
    mount.openConfirmation();
    mount.process(mount.key(32));
    check(!mount.open && mount.operations == 1 && mount.cancellations == 3,
          "Space on default Cancel follows the production Mount key mapping");
    check(!modal_key_event_allowed('\n', true) &&
          !modal_key_event_allowed(32, true) &&
          !modal_key_event_allowed('\t', true) &&
          !modal_key_event_allowed(27, true) &&
          modal_key_event_allowed('\n', false),
          "repeated activation, focus, and Escape key-downs are ignored inside a modal");
    check(defer_close_for_active_dismount(true) &&
          defer_close_for_active_dismount(true) &&
          !defer_close_for_active_dismount(false),
          "repeated application Close defers until synchronous Dismount settles");

    kernel::compositor::widget_press::Capture pointerPress = {};
    kernel::compositor::widget_press::begin(pointerPress, 17, 41, 8, 1);
    check(kernel::compositor::widget_press::can_activate(
              pointerPress, 17, 41, 1, 8),
          "pointer-up can activate only the widget captured in its current window context");
    check(!kernel::compositor::widget_press::can_activate(
              pointerPress, 17, 41, 2, 8),
          "a release from a different pointer button cannot activate the captured widget");
    kernel::compositor::widget_press::Capture delayedPointer = {};
    kernel::compositor::widget_press::begin(delayedPointer, 17, 42, 8, 1);
    check(!kernel::compositor::widget_press::can_activate(
              delayedPointer, 17, 42, 1,
              9) &&
          !kernel::compositor::widget_press::targets(delayedPointer, 17, 41),
          "a delayed modal pointer-up cannot activate a replacement or neighboring widget");
    kernel::compositor::widget_press::clear(delayedPointer);
    check(!delayedPointer.active && delayedPointer.widgetId == -1 &&
          !kernel::compositor::widget_press::targets(delayedPointer, 17, 42),
          "pointer capture retires exactly once after release or stale rejection");

    bool heldKeys[512] = {};
    const bool firstEnterRepeat = record_key_transition(
        heldKeys, 512, '\n', KeyAction::Down);
    const bool secondEnterRepeat = record_key_transition(
        heldKeys, 512, '\n', KeyAction::Down);
    const bool releaseRepeat = record_key_transition(
        heldKeys, 512, '\n', KeyAction::Up);
    const bool pressAfterReleaseRepeat = record_key_transition(
        heldKeys, 512, '\n', KeyAction::Down);
    check(!firstEnterRepeat && secondEnterRepeat && !releaseRepeat &&
          !pressAfterReleaseRepeat && heldKeys['\n'],
          "PS/2 make-code repeats stay marked until the matching key release");

    // Dismount keeps its result modal open after one synchronous VFS outcome;
    // the confirmation route is then inactive until the result is closed.
    ModalHarness dismount;
    dismount.retainResult = true;
    dismount.openConfirmation();
    dismount.focus = 3;
    const KeyEvent delayedDismountSpace = dismount.key(32);
    const KeyEvent dismountSpace = dismount.key(32);
    dismount.process(dismountSpace);
    check(dismount.open && !dismount.confirming &&
          dismount.operations == 1,
          "focused Space dispatches Dismount Confirm once and retains its result dialog");
    dismount.process(delayedDismountSpace);
    check(dismount.operations == 1 &&
          !event_is_current_context(delayedDismountSpace, dismount.context),
          "queued repeated activation from Dismount confirmation is stale after result transition");
    const ModalButtonAction staleResultClick = modal_button_action(
        dismount.open && dismount.confirming, ModalHarness::confirmId,
        ModalHarness::confirmId, ModalHarness::cancelId);
    check(staleResultClick == MODAL_BUTTON_NO_ACTION,
          "a delayed Confirm cannot dispatch again after the handler enters result state");
    dismount.retire();
    dismount.openConfirmation();
    dismount.process(dismountSpace);
    check(dismount.open && dismount.operations == 1,
          "closing a result retires its input context before the next confirmation");

    volatile uint32_t wrapContext = 0xFFFFFFFFu;
    check(advance_input_context(wrapContext) == 1 && wrapContext == 1,
          "input context generation wraps without reusing the invalid zero token");

    std::printf("DM37 Gate H GUI/input checks: %d checks, %d failures\n",
        checks, failures);
    return failures == 0 ? 0 : 1;
}
