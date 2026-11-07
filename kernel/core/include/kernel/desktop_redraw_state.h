#pragma once

#include <stdint.h>

namespace kernel {
namespace desktop {

// A lifecycle repaint after removing a visible surface must be allowed to
// present even when a transport stress run is temporarily suppressing routine
// frames. Readiness still gates the framebuffer operation itself.
inline bool can_present_lifecycle_redraw(bool initialized,
                                        bool framebufferAvailable) {
    return initialized && framebufferAvailable;
}

inline bool should_present_lifecycle_redraw(bool pending, bool initialized,
                                           bool framebufferAvailable) {
    return pending && can_present_lifecycle_redraw(initialized, framebufferAvailable);
}

// Coalesces repaint requests until the UI thread consumes them. This state is
// also used by the hosted test so the interrupt-facing request contract stays
// covered without linking the full desktop renderer.
class RedrawRequestState {
public:
    bool consume() {
        if (!m_pending) return false;
        m_pending = false;
        ++m_generation;
        return true;
    }

    uint64_t generation() const { return m_generation; }

    void request() { m_pending = true; }

private:
    volatile bool m_pending = false;
    volatile uint64_t m_generation = 1;
};

} // namespace desktop
} // namespace kernel
