#pragma once

namespace kernel {
namespace desktop {

class StudioPostCloseInputProbeState {
public:
    void arm() {
        m_armed = true;
        m_pointerPassed = false;
        m_keyboardPassed = false;
    }

    bool observe_pointer_action(bool desktop_has_no_windows, bool state_changed) {
        if (!m_armed || m_pointerPassed || !desktop_has_no_windows || !state_changed) return false;
        m_pointerPassed = true;
        return true;
    }

    bool observe_keyboard_action(bool desktop_has_no_windows, bool state_changed) {
        if (!m_armed || !m_pointerPassed || !desktop_has_no_windows ||
            !state_changed || m_keyboardPassed) return false;
        m_keyboardPassed = true;
        m_armed = false;
        return true;
    }

    bool armed() const { return m_armed; }
    bool pointer_passed() const { return m_pointerPassed; }
    bool keyboard_passed() const { return m_keyboardPassed; }

private:
    bool m_armed = false;
    bool m_pointerPassed = false;
    bool m_keyboardPassed = false;
};

} // namespace desktop
} // namespace kernel
