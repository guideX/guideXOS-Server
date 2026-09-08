#include "include/kernel/input_queue.h"

namespace kernel {
namespace input_queue {

namespace {
static Event s_events[kCapacity];
static uint32_t s_head = 0;
static uint32_t s_tail = 0;
static uint32_t s_count = 0;
static uint32_t s_highWater = 0;
static uint64_t s_sequence = 0;
static uint64_t s_received = 0;
static uint64_t s_queued = 0;
static uint64_t s_dropped = 0;
static uint64_t s_coalesced = 0;

static uint32_t next_index(uint32_t index)
{
    return index + 1u == kCapacity ? 0u : index + 1u;
}

static bool can_coalesce_pointer(const Event& left, const Event& right)
{
    return left.type == EventType::Pointer && right.type == EventType::Pointer &&
           left.buttons == right.buttons && left.wheel == 0 && right.wheel == 0 &&
           left.origin == right.origin;
}
}

void initialize()
{
    s_head = s_tail = s_count = s_highWater = 0;
    s_sequence = s_received = s_queued = s_dropped = s_coalesced = 0;
}

bool push_pointer(int32_t x, int32_t y, uint8_t buttons, int16_t wheel,
                  uint8_t origin)
{
    ++s_received;
    Event event;
    event.type = EventType::Pointer;
    event.origin = origin;
    event.buttons = buttons;
    event.x = x;
    event.y = y;
    event.wheel = wheel;
    event.sequence = ++s_sequence;

    if (s_count != 0) {
        const uint32_t last = s_head == 0 ? kCapacity - 1u : s_head - 1u;
        if (can_coalesce_pointer(s_events[last], event)) {
            s_events[last].x = event.x;
            s_events[last].y = event.y;
            s_events[last].sequence = event.sequence;
            ++s_coalesced;
            return true;
        }
    }
    if (s_count == kCapacity) {
        ++s_dropped;
        return false; // Drop newest; button/key ordering is never displaced.
    }
    s_events[s_head] = event;
    s_head = next_index(s_head);
    ++s_count;
    ++s_queued;
    if (s_count > s_highWater) s_highWater = s_count;
    return true;
}

bool push_key(EventType type, uint32_t key, uint8_t origin)
{
    ++s_received;
    if (type != EventType::KeyDown && type != EventType::KeyUp) {
        ++s_dropped;
        return false;
    }
    if (s_count == kCapacity) {
        ++s_dropped;
        return false;
    }
    Event& event = s_events[s_head];
    event = Event();
    event.type = type;
    event.origin = origin;
    event.key = key;
    event.sequence = ++s_sequence;
    s_head = next_index(s_head);
    ++s_count;
    ++s_queued;
    if (s_count > s_highWater) s_highWater = s_count;
    return true;
}

bool pop(Event* event)
{
    if (event == nullptr || s_count == 0) return false;
    *event = s_events[s_tail];
    s_tail = next_index(s_tail);
    --s_count;
    return true;
}

bool empty() { return s_count == 0; }
uint32_t size() { return s_count; }
uint64_t events_received() { return s_received; }
uint64_t events_queued() { return s_queued; }
uint64_t events_dropped() { return s_dropped; }
uint64_t events_coalesced() { return s_coalesced; }
uint32_t high_water_mark() { return s_highWater; }

} // namespace input_queue
} // namespace kernel
