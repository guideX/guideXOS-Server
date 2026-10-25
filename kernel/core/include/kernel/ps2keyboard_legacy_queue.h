#ifndef KERNEL_PS2KEYBOARD_LEGACY_QUEUE_H
#define KERNEL_PS2KEYBOARD_LEGACY_QUEUE_H

#include "kernel/ps2keyboard.h"

namespace kernel {
namespace ps2keyboard {
namespace legacy_queue {

inline bool has_key_down(const KeyEvent* events, uint8_t tail,
                         uint8_t head, uint8_t capacity)
{
    if (!events || capacity < 2) return false;
    while (tail != head) {
        if (events[tail].action == KeyAction::Down) return true;
        tail = static_cast<uint8_t>((tail + 1u) % capacity);
    }
    return false;
}

inline bool pop_next_key_down(const KeyEvent* events,
                              volatile uint8_t& tail, uint8_t head,
                              uint8_t capacity, KeyEvent* outEvent)
{
    if (!events || !outEvent || capacity < 2) return false;
    while (tail != head) {
        const KeyEvent event = events[tail];
        tail = static_cast<uint8_t>((tail + 1u) % capacity);
        if (event.action != KeyAction::Down) continue;
        *outEvent = event;
        return true;
    }
    return false;
}

} // namespace legacy_queue
} // namespace ps2keyboard
} // namespace kernel

#endif // KERNEL_PS2KEYBOARD_LEGACY_QUEUE_H
