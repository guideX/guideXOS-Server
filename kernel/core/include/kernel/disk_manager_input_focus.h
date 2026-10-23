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

}}} // namespace kernel::apps::disk_manager_input_focus
