#ifndef KERNEL_COMPOSITOR_WIDGET_PRESS_H
#define KERNEL_COMPOSITOR_WIDGET_PRESS_H

#include <stdint.h>

namespace kernel {
namespace compositor {
namespace widget_press {

struct Capture {
    uint32_t windowId;
    int widgetId;
    uint32_t inputContextGeneration;
    uint8_t button;
    bool active;
};

inline void begin(Capture& capture, uint32_t windowId, int widgetId,
                  uint32_t inputContextGeneration, uint8_t button)
{
    capture.windowId = windowId;
    capture.widgetId = widgetId;
    capture.inputContextGeneration = inputContextGeneration;
    capture.button = button;
    capture.active = windowId != 0 && widgetId >= 0 &&
        inputContextGeneration != 0;
}

inline bool is_current(const Capture& capture,
                       uint32_t currentInputContextGeneration)
{
    return capture.active && capture.windowId != 0 && capture.widgetId >= 0 &&
        capture.inputContextGeneration != 0 &&
        capture.inputContextGeneration == currentInputContextGeneration;
}

inline bool targets(const Capture& capture, uint32_t windowId, int widgetId)
{
    return capture.active && capture.windowId == windowId &&
        capture.widgetId == widgetId;
}

inline bool can_activate(const Capture& capture, uint32_t windowId,
                         int widgetId, uint8_t button,
                         uint32_t currentInputContextGeneration)
{
    return is_current(capture, currentInputContextGeneration) &&
        targets(capture, windowId, widgetId) && capture.button == button;
}

inline void clear(Capture& capture)
{
    capture.windowId = 0;
    capture.widgetId = -1;
    capture.inputContextGeneration = 0;
    capture.button = 0;
    capture.active = false;
}

} // namespace widget_press
} // namespace compositor
} // namespace kernel

#endif
