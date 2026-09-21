#pragma once

// Narrow pointer-button tracking for move-event propagation (MC3).
//
// Defect: the hosted compositor published every pointer-move with button 0,
// so right-drag (and left-drag) state never reached Native ELF apps even
// though the ABI (GX_MOUSE_BUTTON_* + GX_MOUSE_ACTION_MOVE) supports it.
// Fix: track held buttons on down/up and expose them on move.
//
// This header is intentionally dependency-free (host-testable with plain
// g++; no Windows headers, no compositor state). The compositor owns one
// integer mask; these helpers mutate/query it. Button codes match the
// guideXOS mouse ABI: 0=none, 1=left, 2=right, 3=middle.

namespace gxos {
namespace gui {
namespace pointerbuttons {

const int kButtonNone = 0;
const int kButtonLeft = 1;
const int kButtonRight = 2;
const int kButtonMiddle = 3;

const int kHeldLeft = 1;
const int kHeldRight = 2;
const int kHeldMiddle = 4;

inline int ButtonToBit(int button) {
    if (button == kButtonLeft) return kHeldLeft;
    if (button == kButtonRight) return kHeldRight;
    if (button == kButtonMiddle) return kHeldMiddle;
    return 0;
}

inline int BitToButton(int bit) {
    if (bit == kHeldLeft) return kButtonLeft;
    if (bit == kHeldRight) return kButtonRight;
    if (bit == kHeldMiddle) return kButtonMiddle;
    return kButtonNone;
}

inline void OnDown(int& mask, int button) {
    const int bit = ButtonToBit(button);
    if (bit != 0) mask |= bit;
}

inline void OnUp(int& mask, int button) {
    const int bit = ButtonToBit(button);
    if (bit != 0) mask &= ~bit;
}

inline void Clear(int& mask) { mask = 0; }

// Fill `out` (capacity `cap`) with the move-button values implied by `mask`:
// no buttons -> single kButtonNone (preserves existing behavior);
// one held button -> single entry for that button;
// several held -> one entry per held button in left/right/middle order so
// every drag consumer observes its own button. Returns the entry count
// (0 when out is null or cap is 0).
inline int MoveButtonsForMask(int mask, int* out, int cap) {
    if (!out || cap <= 0) return 0;
    const int bits[3] = {kHeldLeft, kHeldRight, kHeldMiddle};
    int count = 0;
    for (int i = 0; i < 3; ++i) {
        if ((mask & bits[i]) != 0) {
            if (count >= cap) break;
            out[count++] = BitToButton(bits[i]);
        }
    }
    if (count == 0) {
        out[0] = kButtonNone;
        return 1;
    }
    return count;
}

// Hosted WM_MOUSEMOVE equivalent: derive the held mask from the Win32
// wParam button bits, then list move buttons as above.
inline int MoveButtonsForWParam(bool leftDown, bool rightDown, bool middleDown, int* out,
                                int cap) {
    int mask = 0;
    if (leftDown) mask |= kHeldLeft;
    if (rightDown) mask |= kHeldRight;
    if (middleDown) mask |= kHeldMiddle;
    return MoveButtonsForMask(mask, out, cap);
}

}  // namespace pointerbuttons
}  // namespace gui
}  // namespace gxos
