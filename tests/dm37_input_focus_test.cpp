#include "../kernel/core/include/kernel/disk_manager_input_focus.h"

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    if (condition) return;
    ++failures;
    std::printf("FAIL: %s\n", name);
}

uint8_t settle(uint8_t pane, int& actionFocus,
               kernel::apps::disk_manager_input_focus::StorageModal modal) {
    kernel::apps::disk_manager_input_focus::settle_storage_modal_focus(
        pane, actionFocus, modal);
    return pane;
}

} // namespace

int main() {
    using namespace kernel::apps::disk_manager_input_focus;

    check(action_activation_allowed(PANE_ACTIONS),
          "ordinary focused action pane remains activatable");
    int actionFocus = 5;
    const uint8_t afterMount = settle(PANE_ACTIONS, actionFocus, MODAL_MOUNT);
    check(afterMount == PANE_PARTITIONS &&
          !action_activation_allowed(afterMount) && actionFocus == 0,
          "queued activation after mount settlement cannot use stale action focus");
    actionFocus = 4;
    const uint8_t afterDismountCancel = settle(
        PANE_ACTIONS, actionFocus, MODAL_DISMOUNT);
    check(afterDismountCancel == PANE_PARTITIONS &&
          !action_activation_allowed(afterDismountCancel) && actionFocus == 0,
          "queued activation after dismount confirmation closes cannot reach another action");
    actionFocus = 3;
    const uint8_t afterCheck = settle(
        PANE_ACTIONS, actionFocus, MODAL_FAT32_CHECK);
    check(afterCheck == PANE_PARTITIONS &&
          !action_activation_allowed(afterCheck) && actionFocus == 0,
          "queued activation after FAT32 checker closes cannot use stale action focus");
    actionFocus = 2;
    check(settle(PANE_ACTIONS, actionFocus, MODAL_OTHER) == PANE_ACTIONS &&
          action_activation_allowed(settle(
              PANE_ACTIONS, actionFocus, MODAL_OTHER)) && actionFocus == 2,
          "unrelated dialogs preserve existing keyboard pane behavior");

    std::printf("DM37 input focus checks: %s (%d failures)\n",
        failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
