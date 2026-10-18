#include "kernel/ps2keyboard.h"
#include "kernel/arch.h"

#include <iostream>

namespace {

int checks = 0;
int failures = 0;
uint8_t ps2Data = 0u;
bool ps2DataReady = false;

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
    ps2DataReady = true;
    kernel::ps2keyboard::irq_handler();
}

bool readKey(uint32_t expectedKey, uint32_t expectedModifiers, const char* description)
{
    uint32_t modifiers = 0xFFFFFFFFu;
    const uint32_t key = kernel::ps2keyboard::get_key_with_modifiers(&modifiers);
    const bool matched = key == expectedKey && modifiers == expectedModifiers;
    check(matched, description);
    return matched;
}

} // namespace

namespace kernel {
namespace arch {
namespace amd64 {

uint8_t inb(uint16_t port)
{
    if (port == 0x64u) return ps2DataReady ? 0x01u : 0u;
    if (port == 0x60u) {
        ps2DataReady = false;
        return ps2Data;
    }
    if (port == 0x3FDu) return 0x20u;
    return 0u;
}

void outb(uint16_t, uint8_t) {}

} // namespace amd64
} // namespace arch
} // namespace kernel

int main()
{
    kernel::ps2keyboard::ShiftState state;
    check(!state.isDown(), "both Shift keys begin released");

    state.setLeftDown(true);
    check(state.isDown() && state.leftDown && !state.rightDown,
          "left Shift make activates the modifier");
    state.setLeftDown(true);
    check(state.isDown(), "repeated left Shift make does not toggle modifier state");

    state.setRightDown(true);
    state.setLeftDown(false);
    check(state.isDown() && !state.leftDown && state.rightDown,
          "releasing left Shift retains right Shift state");
    state.setRightDown(false);
    check(!state.isDown(), "releasing both Shift keys clears the modifier");

    state.setRightDown(true);
    check(state.isDown() && !state.leftDown && state.rightDown,
          "right Shift make activates the modifier");
    state.setLeftDown(true);
    state.setRightDown(false);
    check(state.isDown() && state.leftDown && !state.rightDown,
          "releasing right Shift retains left Shift state");
    state.setLeftDown(false);
    check(!state.isDown(), "left Shift break clears the final held modifier");

    state.setLeftDown(true);
    const uint32_t shiftedTabSnapshot = state.isDown()
        ? kernel::ps2keyboard::KEY_MOD_SHIFT : 0u;
    check(shiftedTabSnapshot == kernel::ps2keyboard::KEY_MOD_SHIFT,
          "a key event during Shift carries the Shift modifier bit");
    bool focused = true;
    focused = false;
    check(!focused && state.isDown(),
          "window focus changes do not clear authoritative keyboard modifier state");
    state.setLeftDown(false);
    check(!state.isDown(), "ordinary key behavior resumes after Shift break");

    kernel::ps2keyboard::init();
    feed(0x12u); // left Shift make
    check(kernel::ps2keyboard::is_shift_down() && !kernel::ps2keyboard::has_key(),
          "production decoder tracks left Shift without generating printable input");
    feed(0x59u); // right Shift make
    feed(0xF0u); feed(0x12u); // left Shift break while right remains held
    check(kernel::ps2keyboard::is_shift_down(),
          "production decoder retains Shift when the other key remains held");
    feed(0x0Du); // Tab make
    readKey('\t', kernel::ps2keyboard::KEY_MOD_SHIFT,
            "production PS/2 Tab translation carries the held Shift snapshot");
    kernel::ps2keyboard::KeyEvent event{};
    check(kernel::ps2keyboard::get_event(&event) && event.key == '\t' &&
              event.action == kernel::ps2keyboard::KeyAction::Down &&
              event.modifiers == kernel::ps2keyboard::KEY_MOD_SHIFT,
          "Native ELF transition queue retains logical Tab, action, and modifiers");
    feed(0xF0u); feed(0x0Du); // Tab break
    check(kernel::ps2keyboard::get_event(&event) && event.key == '\t' &&
              event.action == kernel::ps2keyboard::KeyAction::Up &&
              event.modifiers == kernel::ps2keyboard::KEY_MOD_SHIFT,
          "key-up transition preserves modifiers captured before release");
    feed(0xF0u); feed(0x59u); // right Shift break
    check(!kernel::ps2keyboard::is_shift_down(),
          "production decoder clears Shift after both sides are released");
    feed(0x0Du);
    readKey('\t', 0u, "ordinary Tab resumes after the final Shift release");
    check(kernel::ps2keyboard::get_event(&event) && event.key == '\t' &&
              event.action == kernel::ps2keyboard::KeyAction::Down && event.modifiers == 0u,
          "ordinary Tab retains the existing ASCII key value and unmodified event");

    feed(0x12u); feed(0x0Du); feed(0x0Du);
    readKey('\t', kernel::ps2keyboard::KEY_MOD_SHIFT,
            "repeated Tab make keeps Shift active without toggling key state");
    feed(0xF0u); feed(0x12u); feed(0xF0u); feed(0x12u);
    check(!kernel::ps2keyboard::is_shift_down(),
          "duplicate Shift break remains a safe released state");
    feed(0x0Du);
    readKey('\t', 0u, "repeated ordinary Tab remains unmodified after Shift break");

    feed(0x5Au); readKey('\n', 0u, "Enter keeps its existing translated key value");
    feed(0x29u); readKey(' ', 0u, "Space keeps its existing translated key value");
    feed(0x76u); readKey(27u, 0u, "Escape keeps its existing translated key value");
    feed(0x12u); // left Shift make
    feed(0x14u); // left Control make
    feed(0x11u); // left Alt make
    feed(0x0Du); // Tab while Control, Alt, and Shift are all active
    readKey('\t', kernel::ps2keyboard::KEY_MOD_SHIFT |
                    kernel::ps2keyboard::KEY_MOD_CTRL |
                    kernel::ps2keyboard::KEY_MOD_ALT,
            "Tab snapshots all supported modifiers without changing key identity");
    feed(0xF0u); feed(0x14u);
    feed(0xF0u); feed(0x11u);
    feed(0xF0u); feed(0x12u);
    check(!kernel::ps2keyboard::is_ctrl_down() && !kernel::ps2keyboard::is_alt_down() &&
              !kernel::ps2keyboard::is_shift_down(),
          "releasing modifiers independently restores the neutral state");
    feed(0x0Du);
    readKey('\t', 0u, "modifier release restores ordinary Tab dispatch");
    feed(0xE0u); feed(0x75u);
    readKey(kernel::ps2keyboard::KEY_EVENT_UP, 0u,
            "extended arrow identity remains available to desktop input");
    feed(0x07u); readKey(0x11Bu, 0u, "F12 keeps its existing function-key dispatch value");
    feed(0xFFu);
    check(!kernel::ps2keyboard::has_key(),
          "unknown scancode does not manufacture a printable key event");

    // Fault-inject a missing physical Shift break by ending the stream after
    // its make.  The next ordinary key sees the still-authoritative modifier;
    // a later press/release of that same physical Shift supplies a fresh break
    // and restores ordinary keyboard input without reinitializing the driver.
    kernel::ps2keyboard::init();
    feed(0x12u); // left Shift make; intentionally omit its break
    feed(0x0Du); // Tab while the missed break is outstanding
    readKey('\t', kernel::ps2keyboard::KEY_MOD_SHIFT,
            "missing Shift break is reflected in the next ordinary key snapshot");
    feed(0x0Du); // another ordinary key before a matching break arrives
    readKey('\t', kernel::ps2keyboard::KEY_MOD_SHIFT,
            "a missing Shift break remains active until a matching break arrives");
    feed(0x12u); // user taps the same physical Shift key again
    feed(0xF0u); feed(0x12u); // fresh break clears its state
    check(!kernel::ps2keyboard::is_shift_down(),
          "a fresh break from the same Shift key recovers a missed key-up");
    feed(0x0Du);
    readKey('\t', 0u,
            "ordinary Tab resumes after missed-key-up recovery");
    feed(0x1Cu); // A
    readKey('a', 0u,
            "ordinary character input remains usable after modifier recovery");

    // Driver initialization also resets state, but production calls init only
    // during kernel startup; it is not an exposed runtime recovery control.
    feed(0x59u); // right Shift make; intentionally omit its break
    check(kernel::ps2keyboard::is_shift_down(),
          "right Shift can be left active by an omitted key-up");
    kernel::ps2keyboard::init();
    check(!kernel::ps2keyboard::is_shift_down(),
          "keyboard driver initialization clears stale modifier state");
    feed(0x0Du);
    readKey('\t', 0u,
            "ordinary Tab is neutral after driver reinitialization");

    std::cout << "PS/2 keyboard modifier tests: " << (checks - failures)
              << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
