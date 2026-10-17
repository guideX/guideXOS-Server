#include "../kernel/core/include/kernel/widget_activation.h"
#include "../kernel/core/include/kernel/disk_manager_wipe_ui.h"
#include "../kernel/core/include/kernel/disk_manager_whole_disk_erase_ui.h"
#include "../kernel/core/include/kernel/disk_manager_dismount_ui.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool condition, const char* name)
{
    if (condition) return;
    ++failures;
    std::printf("FAIL: %s\n", name);
}

kernel::app::Widget make_button(int id)
{
    kernel::app::Widget widget;
    widget.id = id;
    widget.type = kernel::app::WidgetType::Button;
    return widget;
}

} // namespace

int main()
{
    using namespace kernel::app;
    KernelWindow window;
    window.widgetCount = 1;
    window.widgets[0] = make_button(17);
    int clicks = 0;
    auto click = [&clicks](int id) {
        if (id == 17) ++clicks;
    };

    const int runningCancelFocusIndex = 2;
    const int runningCancelWidgetId = 17;
    const int runningFocusedWidgetId = focused_button_widget_id_for_index(
        runningCancelFocusIndex, 2, runningCancelWidgetId);
    check(runningCancelFocusIndex != runningCancelWidgetId &&
          runningFocusedWidgetId == runningCancelWidgetId &&
          dispatch_focused_button_activation(&window, runningCancelWidgetId,
              runningFocusedWidgetId, '\n', click) &&
          clicks == 1, "Enter activates the focused running Cancel button");
    check(dispatch_focused_button_activation(&window, runningCancelWidgetId,
              runningFocusedWidgetId, 32, click) &&
          clicks == 2, "Space activates the focused running Cancel button");

    const int beforeUnfocused = clicks;
    const int unrelatedFocusIndex = 3;
    const int unfocusedWidgetId = focused_button_widget_id_for_index(
        unrelatedFocusIndex, 2, runningCancelWidgetId);
    check(unfocusedWidgetId == -1 &&
          !dispatch_focused_button_activation(&window, runningCancelWidgetId,
              unfocusedWidgetId, '\n', click) &&
          clicks == beforeUnfocused,
          "running-operation focus index maps only to its rendered Cancel widget");

    window.widgets[0].enabled = false;
    check(!dispatch_focused_button_activation(&window, runningCancelWidgetId,
              runningFocusedWidgetId, 32, click) &&
          clicks == beforeUnfocused,
          "disabled Cancel cannot activate");
    window.widgets[0].enabled = true;

    int cancellationRequests = 0;
    const auto cancelOnce = [&window, &cancellationRequests](int id) {
        if (id != 17) return;
        ++cancellationRequests;
        window.widgets[0].visible = false;
    };
    const bool firstPress = dispatch_focused_button_activation(
        &window, runningCancelWidgetId, runningFocusedWidgetId, '\r', cancelOnce);
    const bool repeatedDelivery = dispatch_focused_button_activation(
        &window, runningCancelWidgetId, runningFocusedWidgetId, '\r', cancelOnce);
    check(firstPress && !repeatedDelivery && cancellationRequests == 1,
          "one key activation issues at most one cancellation request");

    // Format-options Cancel/Format and GPT Repair Cancel/Confirm use this same
    // button dispatcher, with their existing app click handlers.
    window.widgetCount = 2;
    window.widgets[0] = make_button(21);
    window.widgets[1] = make_button(22);
    int modalClicks = 0;
    const auto modalClick = [&modalClicks](int) { ++modalClicks; };
    check(dispatch_focused_button_activation(&window, 21,
              focused_button_widget_id_for_index(2, 2, 21), 32, modalClick) &&
          dispatch_focused_button_activation(&window, 22,
              focused_button_widget_id_for_index(3, 3, 22), '\n', modalClick) &&
          modalClicks == 2,
          "format-options and GPT Repair modal buttons share activation routing");

    window.widgets[0].type = WidgetType::TextBox;
    check(!dispatch_focused_button_activation(&window, 21, 21, '\n', modalClick) &&
          modalClicks == 2,
          "keyboard focus cannot activate a non-button widget");
    check(dispatch_widget_activation(&window, 21, modalClick) && modalClicks == 3,
          "pointer widget dispatch preserves text-box activation behavior");

    using namespace kernel::apps::disk_manager_wipe_ui;
    kernel::storage::PartitionWipeResult wipeProgress = {};
    wipeProgress.progressPercent = 43;
    wipeProgress.bytesWritten = 44u * 1024u;
    wipeProgress.totalBytes = 100u * 1024u;
    check(wipe_progress_value(wipeProgress) == 43,
          "wipe progress uses the engine percentage without timer estimation");

    window.widgetCount = 2;
    window.widgets[0] = {};
    window.widgets[0].id = 29;
    window.widgets[0].type = WidgetType::TextBox;
    window.widgets[0].visible = true;
    window.widgets[0].enabled = true;
    window.widgets[1] = make_button(30);
    window.widgets[1].visible = true;
    window.widgets[1].enabled = false;
    int wipeStarts = 0;
    const auto startWipe = [&window, &wipeStarts](int id) {
        if (id != 30) return;
        ++wipeStarts;
        window.widgets[1].enabled = false;
        window.widgets[1].visible = false;
    };
    const int initiallyFocusedConfirm = focused_button_widget_id_for_index(
        0, 3, 30);
    check(initiallyFocusedConfirm == -1 &&
          !dispatch_focused_button_activation(&window, 30,
              initiallyFocusedConfirm, '\n', startWipe) && wipeStarts == 0,
          "initial confirmation focus cannot activate Wipe Partition with Enter");
    check(!wipe_confirmation_can_start(true, WIPE_ACTION_READY, "WIPX") &&
          !wipe_confirmation_can_start(true, WIPE_ACTION_READY, ""),
          "wrong or empty typed confirmation leaves the destructive button disabled");
    window.widgets[1].enabled = wipe_confirmation_can_start(
        true, WIPE_ACTION_READY, "WIPE");
    check(window.widgets[1].enabled &&
          dispatch_focused_button_activation(&window, 30,
              focused_button_widget_id_for_index(3, 3, 30), '\n', startWipe) &&
          wipeStarts == 1,
          "typing WIPE and explicit focused Enter activates through normal widget dispatch");
    check(!dispatch_focused_button_activation(&window, 30,
              focused_button_widget_id_for_index(3, 3, 30), '\n', startWipe) &&
          wipeStarts == 1,
          "duplicate confirmation activation cannot start a second wipe");

    window.widgetCount = 1;
    window.widgets[0] = make_button(31);
    window.widgets[0].visible = true;
    window.widgets[0].enabled = true;
    int stopRequests = 0;
    const auto stopWipe = [&window, &stopRequests](int id) {
        if (id != 31) return;
        ++stopRequests;
        window.widgets[0].enabled = false;
    };
    check(dispatch_focused_button_activation(&window, 31,
              focused_button_widget_id_for_index(2, 2, 31), '\r', stopWipe) &&
          stopRequests == 1,
          "keyboard activation dispatches Stop Wipe through the normal button handler");
    window.widgets[0].enabled = true;
    check(dispatch_widget_activation(&window, 31, stopWipe) &&
          stopRequests == 2,
          "pointer activation dispatches Stop Wipe through the normal widget handler");
    window.widgets[0].enabled = false;
    check(!dispatch_widget_activation(&window, 31, stopWipe) &&
          stopRequests == 2,
          "a repeated Stop Wipe activation is ignored after terminal dispatch");

    using namespace kernel::apps::disk_manager_whole_disk_erase_ui;
    char erasePhrase[64] = {};
    check(build_confirmation_phrase("SN-1234", erasePhrase,
              sizeof(erasePhrase)) &&
          std::strcmp(erasePhrase, "ERASE SN-1234") == 0,
          "whole-disk final phrase names the displayed stable serial");
    check(!confirmation_phrase_matches("", erasePhrase) &&
          !confirmation_phrase_matches("WIPE", erasePhrase) &&
          !confirmation_phrase_matches("ERASE SN-9999", erasePhrase),
          "empty, Partition Wipe, and wrong-target phrases cannot enable disk erase");

    window.widgetCount = 2;
    window.widgets[0] = {};
    window.widgets[0].id = 41;
    window.widgets[0].type = WidgetType::TextBox;
    window.widgets[0].visible = true;
    window.widgets[0].enabled = true;
    window.widgets[1] = make_button(42);
    window.widgets[1].visible = true;
    window.widgets[1].enabled = false;
    int eraseStarts = 0;
    const auto startErase = [&window, &eraseStarts](int id) {
        if (id != 42) return;
        ++eraseStarts;
        window.widgets[1].enabled = false;
    };
    check(!dispatch_focused_button_activation(&window, 42,
              focused_button_widget_id_for_index(0, 3, 42), '\n', startErase) &&
          eraseStarts == 0,
          "Enter in the initially focused confirmation textbox cannot start erase");
    window.widgets[1].enabled = confirmation_phrase_matches(
        "ERASE SN-1234", erasePhrase);
    check(window.widgets[1].enabled &&
          !dispatch_focused_button_activation(&window, 42,
              focused_button_widget_id_for_index(0, 3, 42), '\n', startErase) &&
          eraseStarts == 0,
          "valid phrase still requires a separate final button activation");
    check(dispatch_focused_button_activation(&window, 42,
              focused_button_widget_id_for_index(3, 3, 42), '\n', startErase) &&
          eraseStarts == 1 &&
          !dispatch_focused_button_activation(&window, 42,
              focused_button_widget_id_for_index(3, 3, 42), '\n', startErase) &&
          eraseStarts == 1,
          "keyboard final activation uses normal dispatch and duplicate activation starts only once");

    window.widgets[1].enabled = true;
    int pointerEraseStarts = 0;
    const auto pointerStart = [&window, &pointerEraseStarts](int id) {
        if (id != 42) return;
        ++pointerEraseStarts;
        window.widgets[1].enabled = false;
    };
    check(dispatch_widget_activation(&window, 42, pointerStart) &&
          pointerEraseStarts == 1 &&
          !dispatch_widget_activation(&window, 42, pointerStart) &&
          pointerEraseStarts == 1,
          "pointer final activation shares normal widget dispatch and one-shot admission");

    int stopEraseRequests = 0;
    window.widgetCount = 1;
    window.widgets[0] = make_button(43);
    window.widgets[0].visible = true;
    window.widgets[0].enabled = true;
    const auto stopEraseClick = [&stopEraseRequests](int id) {
        if (id == 43) ++stopEraseRequests;
    };
    check(dispatch_focused_button_activation(&window, 43,
              focused_button_widget_id_for_index(2, 2, 43), '\r',
              stopEraseClick) && stopEraseRequests == 1,
          "keyboard Stop Erase routes through the normal widget dispatcher");
    check(dispatch_widget_activation(&window, 43, stopEraseClick) &&
          stopEraseRequests == 2,
          "pointer Stop Erase routes through the normal widget dispatcher");

    using namespace kernel::apps::disk_manager_dismount_ui;
    window.widgetCount = 3;
    window.widgets[0] = {};
    window.widgets[0].id = 50;
    window.widgets[0].type = WidgetType::TextBox;
    window.widgets[0].visible = true;
    window.widgets[0].enabled = true;
    window.widgets[1] = make_button(51); // Confirm Dismount
    window.widgets[1].visible = true;
    window.widgets[1].enabled = true;
    window.widgets[2] = make_button(52); // Cancel/result dismissal
    window.widgets[2].visible = true;
    window.widgets[2].enabled = true;
    int dismountStarts = 0;
    int dismountCancels = 0;
    const auto dismountClick = [&window, &dismountStarts,
                                &dismountCancels](int id) {
        if (id == 51) {
            ++dismountStarts;
            window.widgets[1].visible = false;
            window.widgets[1].enabled = false;
        } else if (id == 52) {
            ++dismountCancels;
            window.widgets[1].visible = false;
            window.widgets[1].enabled = false;
            window.widgets[2].visible = false;
            window.widgets[2].enabled = false;
        }
    };
    check(next_confirmation_focus(2, true) == 3 &&
          next_confirmation_focus(2, false) == 2 &&
          next_confirmation_focus(3, true) == 2,
          "Dismount Tab traversal reaches Confirm only when admission is ready");
    const int textboxFocus = focused_button_widget_id_for_index(0, 3, 51);
    check(textboxFocus == -1 &&
          !dispatch_focused_button_activation(&window, 51, textboxFocus,
              '\n', dismountClick) && dismountStarts == 0,
          "Enter from an unrelated textbox cannot confirm a Dismount");
    const int cancelFocus = focused_button_widget_id_for_index(2, 2, 52);
    check(dispatch_focused_button_activation(&window, 52, cancelFocus,
              '\n', dismountClick) && dismountCancels == 1 &&
          dismountStarts == 0,
          "Enter on the initial Cancel focus closes without dismounting");
    window.widgets[1].visible = true;
    window.widgets[1].enabled = false;
    check(!dispatch_focused_button_activation(&window, 51,
              focused_button_widget_id_for_index(3, 3, 51), 32,
              dismountClick) && dismountStarts == 0,
          "busy or protected Dismount confirmation cannot activate its disabled control");
    window.widgets[1].enabled = true;
    check(dispatch_focused_button_activation(&window, 51,
              focused_button_widget_id_for_index(3, 3, 51), 32,
              dismountClick) && dismountStarts == 1 &&
          !dispatch_focused_button_activation(&window, 51,
              focused_button_widget_id_for_index(3, 3, 51), '\r',
              dismountClick) && dismountStarts == 1,
          "explicit Space confirmation starts once and repeated activation is ignored");
    window.widgets[1].visible = true;
    window.widgets[1].enabled = true;
    window.widgets[2].visible = true;
    window.widgets[2].enabled = true;
    check(dispatch_widget_activation(&window, 51, dismountClick) &&
          dismountStarts == 2,
          "pointer Dismount activation uses the normal widget dispatcher");

    std::printf("Widget activation checks: %s (%d failures)\n",
        failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
