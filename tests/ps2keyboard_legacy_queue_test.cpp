#include "kernel/ps2keyboard_legacy_queue.h"

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

} // namespace

int main()
{
    using namespace kernel::ps2keyboard;
    const uint8_t capacity = 8;
    KeyEvent events[capacity] = {};
    uint8_t head = 0;
    volatile uint8_t tail = 0;
    events[head++] = {27, KeyAction::Down, 101, 4, false};
    events[head++] = {27, KeyAction::Up, 0, 4, false};
    events[head++] = {10, KeyAction::Down, 103, 4, false};
    events[head++] = {10, KeyAction::Up, 0, 4, false};
    events[head++] = {10, KeyAction::Down, 104, 4, true};
    events[head++] = {10, KeyAction::Up, 0, 4, false};
    events[head++] = {9, KeyAction::Down, 105, 5, false};

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
    check(legacy_queue::pop_next_key_down(
              events, tail, head, capacity, &event) &&
          event.key == 10 && event.repeated &&
          event.diagnosticSequence == 104,
          "a repeated Enter key-down remains a distinct FIFO activation event");
    check(legacy_queue::pop_next_key_down(
              events, tail, head, capacity, &event) &&
          event.key == 9 && event.inputContextGeneration == 5 &&
          !event.repeated,
          "the first Tab make after a context change carries the new context");
    check(!legacy_queue::has_key_down(events, tail, head, capacity) &&
          !legacy_queue::pop_next_key_down(
              events, tail, head, capacity, &event),
          "release-only queue tail does not report a pending key-down");

    volatile uint32_t context = 4;
    check(legacy_queue::event_is_current_context(events[0], context),
          "queued modal input is current before its owner is retired");
    check(legacy_queue::advance_input_context(context) == 5 &&
          !legacy_queue::event_is_current_context(events[0], context) &&
          legacy_queue::event_is_current_context(events[6], context),
          "retiring a modal rejects queued keys while preserving new-context input");

    std::printf("PS/2 legacy keyboard queue checks: %d checks, %d failures\n",
        checks, failures);
    return failures == 0 ? 0 : 1;
}
