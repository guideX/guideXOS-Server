#include "kernel/ps2keyboard_legacy_queue.h"

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char* name)
{
    if (condition) return;
    ++failures;
    std::printf("FAIL: %s\n", name);
}

} // namespace

int main()
{
    using namespace kernel::ps2keyboard;
    const uint8_t capacity = 8;
    KeyEvent events[capacity] = {};
    uint8_t head = 0;
    volatile uint8_t tail = 0;
    events[head++] = {27, KeyAction::Down, 101};
    events[head++] = {27, KeyAction::Up, 0};
    events[head++] = {10, KeyAction::Down, 103};
    events[head++] = {10, KeyAction::Up, 0};

    check(legacy_queue::has_key_down(events, tail, head, capacity),
          "queued key-downs remain visible past releases");

    KeyEvent event = {};
    check(legacy_queue::pop_next_key_down(
              events, tail, head, capacity, &event) &&
          event.key == 27 && event.diagnosticSequence == 101,
          "first queued key-down returns Escape when Enter is also queued");
    check(legacy_queue::has_key_down(events, tail, head, capacity),
          "second queued key-down remains available after first pop");
    check(legacy_queue::pop_next_key_down(
              events, tail, head, capacity, &event) &&
          event.key == 10 && event.diagnosticSequence == 103,
          "second queued key-down returns Enter in FIFO order");
    check(!legacy_queue::has_key_down(events, tail, head, capacity) &&
          !legacy_queue::pop_next_key_down(
              events, tail, head, capacity, &event),
          "release-only queue tail does not report a pending key-down");

    std::printf("PS/2 legacy keyboard queue checks: %s (%d failures)\n",
        failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
