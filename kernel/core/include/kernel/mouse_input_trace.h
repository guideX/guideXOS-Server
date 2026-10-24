#ifndef KERNEL_MOUSE_INPUT_TRACE_H
#define KERNEL_MOUSE_INPUT_TRACE_H

#include "kernel/types.h"

namespace kernel {
namespace mouse_trace {

// Stable stage identifiers written by the bounded S22R13R4 diagnostic build.
enum Stage : uint8_t {
    Ps2Byte = 1,
    Ps2Packet = 2,
    ButtonTransition = 3,
    PointerMapped = 4,
    DesktopDispatch = 5,
    WindowHitTest = 6,
    WidgetHitTest = 7,
    ClickResult = 8
};

void record(uint32_t eventId, Stage stage, int32_t x, int32_t y,
            uint8_t buttons, int32_t valueA, int32_t valueB,
            uint32_t targetId, int32_t result);
void record_irq(uint32_t eventId, Stage stage, int32_t x, int32_t y,
                uint8_t buttons, int32_t valueA, int32_t valueB,
                uint32_t targetId, int32_t result);
void flush();

} // namespace mouse_trace
} // namespace kernel

#endif // KERNEL_MOUSE_INPUT_TRACE_H
