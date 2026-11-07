#pragma once

#include <stdint.h>

namespace kernel {
namespace input_queue {

static const uint32_t kCapacity = 256;

enum class EventType : uint8_t {
    Pointer = 1,
    KeyDown = 2,
    KeyUp = 3,
};

enum EventOrigin : uint8_t {
    OriginSynthetic = 0,
    OriginVirtioHardware = 1,
};

struct Event {
    EventType type;
    uint8_t origin;
    uint8_t buttons;
    // Virtio keyboard modifier state: shift=1, caps=2, ctrl=4, alt=8, meta=16.
    uint8_t modifiers;
    int32_t x;
    int32_t y;
    int16_t wheel;
    int16_t reserved2;
    uint32_t key;       // translated character or legacy desktop key
    uint32_t nativeKey; // stable virtual key for focused native applications
    uint64_t sequence;

    Event()
        : type(EventType::Pointer), origin(OriginSynthetic), buttons(0), modifiers(0),
          x(0), y(0), wheel(0), reserved2(0), key(0), nativeKey(0), sequence(0) {}
};

void initialize();
bool push_pointer(int32_t x, int32_t y, uint8_t buttons, int16_t wheel,
                  uint8_t origin);
bool push_key(EventType type, uint32_t key, uint32_t native_key,
              uint8_t modifiers, uint8_t origin);
inline bool push_key(EventType type, uint32_t key, uint8_t origin) {
    return push_key(type, key, key, 0, origin);
}
bool pop(Event* event);
bool empty();
uint32_t size();

uint64_t events_received();
uint64_t events_queued();
uint64_t events_dropped();
uint64_t events_coalesced();
uint32_t high_water_mark();

} // namespace input_queue
} // namespace kernel
