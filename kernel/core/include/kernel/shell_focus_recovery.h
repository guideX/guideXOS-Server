#ifndef KERNEL_SHELL_FOCUS_RECOVERY_H
#define KERNEL_SHELL_FOCUS_RECOVERY_H

#include "kernel/types.h"

namespace kernel { namespace shell_focus_recovery {

struct State {
    bool open;
    bool active;
    bool minimized;
};

inline State after_escape(State current, uint32_t key)
{
    if (current.open && key == 27) {
        current.open = false;
        current.active = true;
        current.minimized = false;
    }
    return current;
}

inline State after_grave(State current, uint32_t key)
{
    if (!current.open && (key == '`' || key == '~')) {
        current.open = true;
        current.active = true;
        current.minimized = false;
    }
    return current;
}

}} // namespace kernel::shell_focus_recovery

#endif
