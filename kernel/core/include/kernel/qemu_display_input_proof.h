//
// QEMU-only interactive display-input proof state.
//
// This adapter feeds already-mapped pointer events into the ordinary kernel
// window-manager hit-test, focus, and drag state. It is not a product input
// backend and is never enabled without the explicit QEMU virtio-gpu probe
// gates.
//

#ifndef KERNEL_QEMU_DISPLAY_INPUT_PROOF_H
#define KERNEL_QEMU_DISPLAY_INPUT_PROOF_H

#include "kernel/types.h"
#include "kernel/display_input_mapper.h"

namespace kernel {
namespace qemu_display_input_proof {

struct State {
    bool initialized;
    bool active;
    uint32_t windowId;
    int32_t cursorX;
    int32_t cursorY;
    uint8_t buttons;
    int32_t pointerMonitor;
    int32_t windowDominantMonitor;
    int32_t boundaryX;
    int32_t initialWindowX;
    int32_t initialWindowY;
    int32_t initialWindowW;
    int32_t initialWindowH;
    int32_t windowX;
    int32_t windowY;
    int32_t windowW;
    int32_t windowH;
    int32_t boundaryWindowX;
    int32_t boundaryWindowY;
    int32_t boundaryWindowW;
    int32_t boundaryWindowH;
    bool clickFocus;
    bool expectedWindowHit;
    bool dragCapture;
    bool dragCrossedBoundary;
    bool captureReleased;
    bool finalWindowOnSecondary;
    bool finalWindowIntersectsBoth;
    uint32_t eventsHandled;
    uint32_t buttonDowns;
    uint32_t buttonUps;
    char lastMappingReason[64];

    State();
};

void init(int32_t virtualWidth, int32_t virtualHeight,
          const display_input::DisplayInputMapper* mapper);
void handle(const display_input::DisplayPointerEvent& event);
void finish();
const State* state();
bool is_active();

} // namespace qemu_display_input_proof
} // namespace kernel

#endif // KERNEL_QEMU_DISPLAY_INPUT_PROOF_H
