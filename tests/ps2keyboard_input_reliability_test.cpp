#include "kernel/ps2keyboard.h"
#include "kernel/arch.h"
#include "kernel/mouse_input_trace.h"
#include "kernel/ps2mouse.h"

#include <iostream>

namespace {

int checks = 0;
int failures = 0;
uint8_t ps2Data = 0u;
uint8_t ps2Status = 0u;

void check(bool passed, const char* description)
{
    ++checks;
    if (!passed) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

void feed(uint8_t scancode)
{
    ps2Data = scancode;
    ps2Status = 0x01u;
    kernel::ps2keyboard::irq_handler();
    ps2Status = 0u;
}

bool takeLegacy(uint32_t expectedKey, uint32_t expectedModifiers = 0u)
{
    uint32_t modifiers = 0xFFFFFFFFu;
    const uint32_t key = kernel::ps2keyboard::get_key_with_modifiers(&modifiers);
    if (key != expectedKey || modifiers != expectedModifiers) {
        std::cerr << "  expected key=" << expectedKey << " modifiers=" << expectedModifiers
                  << " got key=" << key << " modifiers=" << modifiers << '\n';
    }
    return key == expectedKey && modifiers == expectedModifiers;
}

void sendIrq12Byte(uint8_t status, uint8_t data)
{
    ps2Data = data;
    ps2Status = status;
    kernel::ps2mouse::irq_handler();
    ps2Status = 0u;
}

} // namespace

namespace kernel {
namespace arch {
namespace amd64 {

uint8_t inb(uint16_t port)
{
    if (port == 0x64u) return ps2Status;
    if (port == 0x60u) {
        ps2Status = 0u;
        return ps2Data;
    }
    if (port == 0x3FDu) return 0x20u;
    return 0u;
}

void outb(uint16_t, uint8_t) {}

} // namespace amd64
} // namespace arch
} // namespace kernel

namespace kernel {
namespace mouse_trace {

void record(uint32_t, Stage, int32_t, int32_t, uint8_t, int32_t, int32_t,
            uint32_t, int32_t) {}
void record_irq(uint32_t, Stage, int32_t, int32_t, uint8_t, int32_t, int32_t,
                uint32_t, int32_t) {}
void flush() {}

} // namespace mouse_trace
} // namespace kernel

int main()
{
    using kernel::ps2keyboard::KEY_MOD_SHIFT;
    using kernel::ps2keyboard::KeyAction;
    using kernel::ps2keyboard::KeyEvent;

    kernel::ps2keyboard::init();

    // Reproduce SIR1 exactly: a make, its break, then b make before the
    // desktop has had a chance to consume the legacy keyboard API.
    feed(0x1Cu);                    // a down
    feed(0xF0u); feed(0x1Cu);       // a up
    feed(0x32u);                    // b down

    kernel::ps2keyboard::QueueDiagnostics legacyStats{};
    kernel::ps2keyboard::QueueDiagnostics nativeStats{};
    kernel::ps2keyboard::get_legacy_queue_diagnostics(&legacyStats);
    kernel::ps2keyboard::get_native_queue_diagnostics(&nativeStats);
    check(legacyStats.receivedEvents == 3u && legacyStats.enqueuedEvents == 3u &&
              legacyStats.currentOccupancy == 3u &&
              legacyStats.maximumOccupancy == 3u && legacyStats.overflowDrops == 0u,
          "legacy queue reports enqueues, live occupancy, and high-water mark");
    check(nativeStats.receivedEvents == 3u && nativeStats.enqueuedEvents == 3u &&
              nativeStats.currentOccupancy == 3u &&
              nativeStats.maximumOccupancy == 3u && nativeStats.overflowDrops == 0u,
          "native queue diagnostics remain independent from legacy consumption");

    check(kernel::ps2keyboard::has_key(),
          "legacy API reports pending FIFO data after a burst");
    check(takeLegacy('a'),
          "legacy FIFO first delivers a down from a-down, a-up, b-down");
    check(kernel::ps2keyboard::has_key(),
          "legacy API keeps the later b down pending after consuming a");
    check(takeLegacy('b'),
          "legacy FIFO delivers b down after filtering a up");
    check(!kernel::ps2keyboard::has_key(),
          "legacy API reports empty after both key-downs are consumed");

    KeyEvent native{};
    check(kernel::ps2keyboard::get_event(&native) && native.key == 'a' &&
              native.action == KeyAction::Down && native.sequence == 1u,
          "native queue independently delivers a down");
    check(kernel::ps2keyboard::get_event(&native) && native.key == 'a' &&
              native.action == KeyAction::Up && native.sequence == 2u,
          "native queue independently preserves a up ordering");
    check(kernel::ps2keyboard::get_event(&native) && native.key == 'b' &&
              native.action == KeyAction::Down && native.sequence == 3u,
          "native queue independently delivers b down after a up");
    kernel::ps2keyboard::get_native_queue_diagnostics(&nativeStats);
    check(nativeStats.dequeuedEvents == 3u && nativeStats.pairedKeyTransitions == 1u &&
              nativeStats.outstandingKeyDowns == 1u,
          "native pairing diagnostics count a released key and the still-held b");

    // Pac-Man reads the native queue. Keep fast directional changes and
    // explicit release ordering independent from the desktop API above.
    kernel::ps2keyboard::init();
    feed(0xE0u); feed(0x75u);                   // Up down
    feed(0xE0u); feed(0xF0u); feed(0x75u);      // Up up
    feed(0xE0u); feed(0x74u);                   // Right down
    feed(0xE0u); feed(0xF0u); feed(0x74u);      // Right up
    check(takeLegacy(kernel::ps2keyboard::KEY_EVENT_UP) &&
              takeLegacy(kernel::ps2keyboard::KEY_EVENT_RIGHT),
          "legacy desktop receives rapid directional key-downs in order");
    const uint32_t expectedDirections[] = {
        kernel::ps2keyboard::KEY_EVENT_UP, kernel::ps2keyboard::KEY_EVENT_UP,
        kernel::ps2keyboard::KEY_EVENT_RIGHT, kernel::ps2keyboard::KEY_EVENT_RIGHT
    };
    const KeyAction expectedActions[] = {
        KeyAction::Down, KeyAction::Up, KeyAction::Down, KeyAction::Up
    };
    bool directionalOrder = true;
    for (uint32_t i = 0; i < 4u; ++i) {
        directionalOrder = kernel::ps2keyboard::get_event(&native) &&
            native.key == expectedDirections[i] && native.action == expectedActions[i] &&
            directionalOrder;
    }
    check(directionalOrder,
          "native queue preserves rapid up/right press-release ordering for Pac-Man");
    kernel::ps2keyboard::get_native_queue_diagnostics(&nativeStats);
    check(nativeStats.pairedKeyTransitions == 2u && nativeStats.outstandingKeyDowns == 0u,
          "native directional transitions finish with no outstanding held keys");

    // Modifiers are captured per transition, not sampled when foreground code
    // eventually consumes the queue.
    kernel::ps2keyboard::init();
    feed(0x12u);                    // left Shift down
    feed(0x1Cu);                    // a down while Shift is held
    feed(0xF0u); feed(0x12u);       // left Shift up
    feed(0x32u);                    // b down without Shift
    check(takeLegacy('A', KEY_MOD_SHIFT),
          "legacy FIFO retains shifted key identity and modifiers captured on a down");
    check(takeLegacy('b', 0u),
          "legacy FIFO retains the later unmodified b down");

    kernel::ps2keyboard::init();
    feed(0x11u); // left Alt down
    feed(0x0Cu); // F4 down while Alt is held
    feed(0x32u); // later ordinary key must not overwrite shortcut metadata
    uint32_t shortcutModifiers = 0u;
    check(kernel::ps2keyboard::get_key_with_modifiers(&shortcutModifiers) == 0x113u &&
              shortcutModifiers == kernel::ps2keyboard::KEY_MOD_ALT &&
              kernel::ps2keyboard::was_alt_f4_shortcut_candidate() &&
              kernel::ps2keyboard::last_key_alt_left_down(),
          "legacy FIFO keeps Alt-F4 candidate metadata with its own event");
    check(takeLegacy('b', kernel::ps2keyboard::KEY_MOD_ALT) &&
              !kernel::ps2keyboard::was_alt_f4_shortcut_candidate(),
          "later queued key receives its own shortcut state and modifier snapshot");

    // An IRQ12 invocation with output-buffer-full and AUX clear carries a
    // keyboard byte. It must reach the same decoder; AUX-set mouse bytes stay
    // on the mouse packet path.
    kernel::ps2keyboard::init();
    sendIrq12Byte(0x01u, 0x1Cu); // OBF=1, AUX=0, keyboard 'a' make
    check(kernel::ps2mouse::get_keyboard_bytes_routed_from_irq12() == 1u &&
              kernel::ps2keyboard::has_key() && takeLegacy('a'),
          "IRQ12 routes an OBF keyboard byte instead of discarding it");
    sendIrq12Byte(0x21u, 0x08u); // OBF=1, AUX=1, mouse packet header
    sendIrq12Byte(0x21u, 0x00u);
    sendIrq12Byte(0x21u, 0x00u);
    check(kernel::ps2mouse::get_keyboard_bytes_routed_from_irq12() == 1u &&
              !kernel::ps2keyboard::has_key(),
          "AUX-set mouse packet bytes are not routed into the keyboard decoder");

    // The ring reserves one slot to distinguish full from empty, so a
    // 64-slot array has 63 usable entries. Every accepted key remains
    // consumable in order and the 64th is not silently exposed as a new key.
    kernel::ps2keyboard::init();
    for (uint32_t i = 0; i < 64u; ++i) feed(0x1Cu);
    uint32_t delivered = 0u;
    while (kernel::ps2keyboard::has_key()) {
        if (kernel::ps2keyboard::get_key_with_modifiers(nullptr) == 'a') {
            ++delivered;
        }
    }
    check(delivered == 63u,
          "legacy saturation preserves the 63 accepted events without overwriting");
    kernel::ps2keyboard::get_legacy_queue_diagnostics(&legacyStats);
    check(legacyStats.receivedEvents == 64u && legacyStats.enqueuedEvents == 63u &&
              legacyStats.dequeuedEvents == 63u &&
              legacyStats.droppedEvents == 1u && legacyStats.overflowDrops == 1u &&
              legacyStats.overflowKeyDownDrops == 1u && legacyStats.currentOccupancy == 0u &&
              legacyStats.maximumOccupancy == 63u,
          "legacy saturation records the lost make and its occupancy history");

    // A break arriving while the native queue is full replaces only its last
    // pending make. Older entries stay ordered, and the loss remains visible.
    kernel::ps2keyboard::init();
    for (uint32_t i = 0; i < 63u; ++i) { feed(0xE0u); feed(0x75u); }
    feed(0xE0u); feed(0xF0u); feed(0x75u);
    kernel::ps2keyboard::get_native_queue_diagnostics(&nativeStats);
    check(nativeStats.currentOccupancy == 63u && nativeStats.overflowDrops == 1u &&
              nativeStats.overflowKeyDownDrops == 1u && nativeStats.overflowKeyUpDrops == 0u &&
              nativeStats.receivedEvents == 64u && nativeStats.enqueuedEvents == 64u &&
              nativeStats.dequeuedEvents == 0u && nativeStats.droppedEvents == 1u &&
              nativeStats.overflowDrops == nativeStats.overflowKeyDownDrops +
                  nativeStats.overflowKeyUpDrops &&
              nativeStats.receivedEvents == nativeStats.dequeuedEvents +
                  nativeStats.droppedEvents + nativeStats.currentOccupancy,
          "native matching-break replacement reports the evicted make and conserves queue events");
    uint32_t nativeDowns = 0u;
    uint32_t nativeUps = 0u;
    bool onlyUpArrow = true;
    while (kernel::ps2keyboard::get_event(&native)) {
        onlyUpArrow = onlyUpArrow && native.key == kernel::ps2keyboard::KEY_EVENT_UP;
        if (native.action == KeyAction::Down) ++nativeDowns;
        else ++nativeUps;
    }
    kernel::ps2keyboard::get_native_queue_diagnostics(&nativeStats);
    check(onlyUpArrow && nativeDowns == 62u && nativeUps == 1u &&
              nativeStats.sequenceGaps == 1u &&
              nativeStats.pairedKeyTransitions == 1u &&
              nativeStats.outstandingKeyDowns == 0u &&
              nativeStats.receivedEvents == nativeStats.dequeuedEvents +
                  nativeStats.droppedEvents + nativeStats.currentOccupancy &&
              nativeStats.overflowDrops == nativeStats.overflowKeyDownDrops +
                  nativeStats.overflowKeyUpDrops,
          "native queue recovers Pac-Man's held arrow with paired release and conserved counters");

    // A break for another key must not replace a pending make: that could
    // strand the queued key while inserting an unrelated unmatched release.
    kernel::ps2keyboard::init();
    for (uint32_t i = 0; i < 63u; ++i) feed(0x1Cu); // 'a' down fills the ring
    feed(0xE0u); feed(0xF0u); feed(0x74u);          // unrelated Right up
    kernel::ps2keyboard::get_native_queue_diagnostics(&nativeStats);
    check(nativeStats.currentOccupancy == 63u && nativeStats.droppedEvents == 1u &&
              nativeStats.receivedEvents == 64u && nativeStats.enqueuedEvents == 63u &&
              nativeStats.dequeuedEvents == 0u &&
              nativeStats.overflowDrops == 1u &&
              nativeStats.overflowKeyDownDrops == 0u &&
              nativeStats.overflowKeyUpDrops == 1u &&
              nativeStats.overflowDrops == nativeStats.overflowKeyDownDrops +
                  nativeStats.overflowKeyUpDrops &&
              nativeStats.receivedEvents == nativeStats.dequeuedEvents +
                  nativeStats.droppedEvents + nativeStats.currentOccupancy,
          "full queue drops an unrelated break with exact make/break and queue accounting");
    bool unrelatedBreakWasNotInserted = true;
    for (uint32_t i = 0; i < 63u; ++i) {
        unrelatedBreakWasNotInserted = kernel::ps2keyboard::get_event(&native) &&
            native.key == 'a' && native.action == KeyAction::Down &&
            unrelatedBreakWasNotInserted;
    }
    check(unrelatedBreakWasNotInserted && !kernel::ps2keyboard::get_event(&native),
          "native queue preserves all older makes when a nonmatching break overflows");

    // Drive the legacy ring indices around the 64-slot buffer and verify order.
    kernel::ps2keyboard::init();
    for (uint32_t i = 0; i < 40u; ++i) feed((i & 1u) ? 0x32u : 0x1Cu);
    for (uint32_t i = 0; i < 40u; ++i) {
        check(takeLegacy((i & 1u) ? 'b' : 'a'),
              "legacy FIFO preserves first batch order before wraparound");
    }
    for (uint32_t i = 0; i < 40u; ++i) feed((i & 1u) ? 0x32u : 0x1Cu);
    kernel::ps2keyboard::get_legacy_queue_diagnostics(&legacyStats);
    check(legacyStats.wraparounds == 1u && legacyStats.currentOccupancy == 40u &&
              legacyStats.maximumOccupancy == 40u && legacyStats.overflowDrops == 0u,
          "legacy ring reports wraparound and occupancy without overflow");
    for (uint32_t i = 0; i < 40u; ++i) {
        check(takeLegacy((i & 1u) ? 'b' : 'a'),
              "legacy FIFO preserves second batch order after wraparound");
    }

    kernel::ps2keyboard::init();
    for (uint32_t i = 0; i < 40u; ++i) feed((i & 1u) ? 0x32u : 0x1Cu);
    bool nativeFirstBatchOrder = true;
    for (uint32_t i = 0; i < 40u; ++i) {
        nativeFirstBatchOrder = kernel::ps2keyboard::get_event(&native) &&
            native.key == ((i & 1u) ? 'b' : 'a') && nativeFirstBatchOrder;
    }
    check(nativeFirstBatchOrder,
          "native FIFO preserves first batch order before wraparound");
    for (uint32_t i = 0; i < 40u; ++i) feed((i & 1u) ? 0x32u : 0x1Cu);
    kernel::ps2keyboard::get_native_queue_diagnostics(&nativeStats);
    check(nativeStats.wraparounds == 1u && nativeStats.currentOccupancy == 40u &&
              nativeStats.maximumOccupancy == 40u && nativeStats.overflowDrops == 0u,
          "native ring reports wraparound and occupancy without overflow");
    bool nativeSecondBatchOrder = true;
    for (uint32_t i = 0; i < 40u; ++i) {
        nativeSecondBatchOrder = kernel::ps2keyboard::get_event(&native) &&
            native.key == ((i & 1u) ? 'b' : 'a') && nativeSecondBatchOrder;
    }
    check(nativeSecondBatchOrder,
          "native FIFO preserves second batch order after wraparound");

    std::cout << "PS/2 keyboard input reliability tests: " << (checks - failures)
              << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
