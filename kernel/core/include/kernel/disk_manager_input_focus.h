#pragma once

#include <stdint.h>

namespace kernel { namespace apps { namespace disk_manager_input_focus {

// Numeric values match DiskManagerApp::KeyboardPane. Keeping settlement in a
// small freestanding-safe helper lets production dispatch and host regression
// exercise the same transition.
enum PaneValue : uint8_t {
    PANE_DISKS = 0,
    PANE_PARTITIONS = 1,
    PANE_ACTIONS = 2,
    PANE_DETAILS = 3,
};

enum StorageModal : uint8_t {
    MODAL_OTHER = 0,
    MODAL_MOUNT,
    MODAL_DISMOUNT,
    MODAL_FAT32_CHECK,
};

enum MountConfirmationAction : uint8_t {
    MOUNT_CONFIRMATION_NO_ACTION = 0,
    MOUNT_CONFIRMATION_FOCUS_CHANGED,
    MOUNT_CONFIRMATION_CANCEL,
    MOUNT_CONFIRMATION_CONFIRM,
};

enum ModalButtonAction : uint8_t {
    MODAL_BUTTON_NO_ACTION = 0,
    MODAL_BUTTON_CANCEL,
    MODAL_BUTTON_CONFIRM,
};

inline ModalButtonAction modal_button_action(bool active, int widgetId,
                                             int confirmWidgetId,
                                             int cancelWidgetId) {
    if (!active) return MODAL_BUTTON_NO_ACTION;
    if (widgetId == confirmWidgetId) return MODAL_BUTTON_CONFIRM;
    if (widgetId == cancelWidgetId) return MODAL_BUTTON_CANCEL;
    return MODAL_BUTTON_NO_ACTION;
}

inline uint8_t next_mount_confirmation_focus(uint8_t focus) {
    return focus == 2 ? 3 : 2;
}

inline MountConfirmationAction mount_confirmation_action(uint8_t focus,
                                                           uint32_t key) {
    if (key == 9 || key == 0x107)
        return MOUNT_CONFIRMATION_FOCUS_CHANGED;
    if (key == 27) return MOUNT_CONFIRMATION_CANCEL;
    if (key == '\n' || key == '\r' || key == 32)
        return focus == 3 ? MOUNT_CONFIRMATION_CONFIRM
                          : MOUNT_CONFIRMATION_CANCEL;
    return MOUNT_CONFIRMATION_NO_ACTION;
}

template <typename Pane>
inline void settle_storage_modal_focus(Pane& pane, int& actionFocus,
                                       StorageModal modal) {
    if (modal != MODAL_MOUNT && modal != MODAL_DISMOUNT &&
        modal != MODAL_FAT32_CHECK) return;
    pane = static_cast<Pane>(PANE_PARTITIONS);
    actionFocus = 0;
}

inline bool action_activation_allowed(uint8_t pane) {
    return pane == PANE_ACTIONS;
}

inline bool modal_key_event_allowed(uint32_t key, bool repeated) {
    const bool activationOrFocusKey = key == 27 || key == 9 || key == 0x107 ||
        key == '\n' || key == 13 || key == 32;
    return !repeated || !activationOrFocusKey;
}

inline bool defer_close_for_active_dismount(bool dismountOperationActive) {
    return dismountOperationActive;
}

}}} // namespace kernel::apps::disk_manager_input_focus
