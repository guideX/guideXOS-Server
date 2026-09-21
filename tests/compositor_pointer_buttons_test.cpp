// Host test for the MC3 compositor pointer-button fix + Missile Command
// input translation. Builds with host g++ (no Windows/compositor needed):
//   g++ -std=c++17 -Wall -Wextra -O2 tests/compositor_pointer_buttons_test.cpp -o out/...exe
//
// Covers: no buttons + move -> 0; left down -> move reports left;
// left up -> move reports none; right down -> move reports right;
// right drag sequence; right up -> subsequent move reports none;
// down/up values unchanged; no stale state after focus/window-destroy
// cleanup (Clear); multiple buttons sane; Missile Command translation
// (right-down alone does not fire, right-drag does, left-click retained).

#include "../compositor_pointer_buttons.h"
#include "../sdk/samples/missilecommand/missilecommand_state.h"

#include <iostream>

namespace {

int g_failures = 0;

bool expect(bool value, const char* label) {
    if (!value) {
        std::cerr << "FAIL: " << label << "\n";
        ++g_failures;
    }
    return value;
}

using gxos::gui::pointerbuttons::kButtonLeft;
using gxos::gui::pointerbuttons::kButtonMiddle;
using gxos::gui::pointerbuttons::kButtonNone;
using gxos::gui::pointerbuttons::kButtonRight;

}  // namespace

int main() {
    bool ok = true;

    // No buttons + move -> 0 (existing behavior preserved).
    {
        int mask = 0;
        int out[3] = {-1, -1, -1};
        int n = gxos::gui::pointerbuttons::MoveButtonsForMask(mask, out, 3);
        ok &= expect(n == 1 && out[0] == kButtonNone, "move: no buttons -> 0");
    }

    // Left down -> move reports left.
    {
        int mask = 0;
        gxos::gui::pointerbuttons::OnDown(mask, kButtonLeft);
        int out[3] = {-1, -1, -1};
        int n = gxos::gui::pointerbuttons::MoveButtonsForMask(mask, out, 3);
        ok &= expect(n == 1 && out[0] == kButtonLeft, "move: left held -> left");
    }

    // Left up -> subsequent move reports none.
    {
        int mask = 0;
        gxos::gui::pointerbuttons::OnDown(mask, kButtonLeft);
        gxos::gui::pointerbuttons::OnUp(mask, kButtonLeft);
        int out[3] = {-1, -1, -1};
        int n = gxos::gui::pointerbuttons::MoveButtonsForMask(mask, out, 3);
        ok &= expect(n == 1 && out[0] == kButtonNone, "move: left released -> 0");
        ok &= expect(mask == 0, "state: left mask cleared");
    }

    // Right down -> move reports right.
    {
        int mask = 0;
        gxos::gui::pointerbuttons::OnDown(mask, kButtonRight);
        int out[3] = {-1, -1, -1};
        int n = gxos::gui::pointerbuttons::MoveButtonsForMask(mask, out, 3);
        ok &= expect(n == 1 && out[0] == kButtonRight, "move: right held -> right");
    }

    // Right drag sequence: down, moves report right, up, move reports none.
    {
        int mask = 0;
        gxos::gui::pointerbuttons::OnDown(mask, kButtonRight);
        int drag[3] = {-1, -1, -1};
        int n1 = gxos::gui::pointerbuttons::MoveButtonsForMask(mask, drag, 3);
        int n2 = gxos::gui::pointerbuttons::MoveButtonsForMask(mask, drag, 3);
        ok &= expect(n1 == 1 && drag[0] == kButtonRight, "drag: move during hold -> right");
        ok &= expect(n2 == 1 && drag[0] == kButtonRight, "drag: second move -> right");
        gxos::gui::pointerbuttons::OnUp(mask, kButtonRight);
        int after[3] = {-1, -1, -1};
        int n3 = gxos::gui::pointerbuttons::MoveButtonsForMask(mask, after, 3);
        ok &= expect(n3 == 1 && after[0] == kButtonNone, "drag: move after up -> 0");
    }

    // Right up -> subsequent move reports none (explicit stale check).
    {
        int mask = 0;
        gxos::gui::pointerbuttons::OnDown(mask, kButtonRight);
        gxos::gui::pointerbuttons::OnUp(mask, kButtonRight);
        gxos::gui::pointerbuttons::OnUp(mask, kButtonRight);  // duplicate up is safe
        int out[3] = {-1, -1, -1};
        int n = gxos::gui::pointerbuttons::MoveButtonsForMask(mask, out, 3);
        ok &= expect(n == 1 && out[0] == kButtonNone, "move: right released -> 0");
    }

    // Button down/up values remain the ABI codes (unchanged contract).
    {
        ok &= expect(kButtonNone == 0, "abi: none == 0");
        ok &= expect(kButtonLeft == 1, "abi: left == 1");
        ok &= expect(kButtonRight == 2, "abi: right == 2");
        ok &= expect(kButtonMiddle == 3, "abi: middle == 3");
        ok &= expect(kMcActionMove == 0 && kMcActionDown == 1 && kMcActionUp == 2,
                     "abi: move/down/up == 0/1/2");
    }

    // No stale state after focus/window-destruction cleanup (Clear).
    {
        int mask = 0;
        gxos::gui::pointerbuttons::OnDown(mask, kButtonLeft);
        gxos::gui::pointerbuttons::OnDown(mask, kButtonRight);
        gxos::gui::pointerbuttons::Clear(mask);  // focus loss / window destroyed
        int out[3] = {-1, -1, -1};
        int n = gxos::gui::pointerbuttons::MoveButtonsForMask(mask, out, 3);
        ok &= expect(n == 1 && out[0] == kButtonNone, "cleanup: cleared -> 0");
        // Capture-lost path clears as well (same helper).
        gxos::gui::pointerbuttons::OnDown(mask, kButtonRight);
        gxos::gui::pointerbuttons::Clear(mask);
        int out2[3] = {-1, -1, -1};
        int n2 = gxos::gui::pointerbuttons::MoveButtonsForMask(mask, out2, 3);
        ok &= expect(n2 == 1 && out2[0] == kButtonNone, "cleanup: capture lost -> 0");
    }

    // Multiple buttons remain sane: one move entry per held button.
    {
        int mask = 0;
        gxos::gui::pointerbuttons::OnDown(mask, kButtonLeft);
        gxos::gui::pointerbuttons::OnDown(mask, kButtonRight);
        int out[3] = {-1, -1, -1};
        int n = gxos::gui::pointerbuttons::MoveButtonsForMask(mask, out, 3);
        ok &= expect(n == 2 && out[0] == kButtonLeft && out[1] == kButtonRight,
                     "multi: left+right -> two moves");
        gxos::gui::pointerbuttons::OnUp(mask, kButtonLeft);
        int out2[3] = {-1, -1, -1};
        int n2 = gxos::gui::pointerbuttons::MoveButtonsForMask(mask, out2, 3);
        ok &= expect(n2 == 1 && out2[0] == kButtonRight, "multi: left released -> right");
    }

    // Hosted wParam equivalent: button bits map to the same move values.
    {
        int out[3] = {-1, -1, -1};
        int n = gxos::gui::pointerbuttons::MoveButtonsForWParam(false, false, false, out, 3);
        ok &= expect(n == 1 && out[0] == kButtonNone, "wparam: none -> 0");
        n = gxos::gui::pointerbuttons::MoveButtonsForWParam(true, false, false, out, 3);
        ok &= expect(n == 1 && out[0] == kButtonLeft, "wparam: left -> left");
        n = gxos::gui::pointerbuttons::MoveButtonsForWParam(false, true, false, out, 3);
        ok &= expect(n == 1 && out[0] == kButtonRight, "wparam: right -> right");
        n = gxos::gui::pointerbuttons::MoveButtonsForWParam(true, true, false, out, 3);
        ok &= expect(n == 2 && out[0] == kButtonLeft && out[1] == kButtonRight,
                     "wparam: left+right -> two moves");
    }

    // Missile Command translation: right-down alone does not fire.
    {
        ok &= expect(!mc_should_fire(kMcActionDown, kButtonRight),
                     "mc: right-down alone does not fire");
        ok &= expect(!mc_should_fire(kMcActionDown, kButtonNone),
                     "mc: down+none does not fire");
        ok &= expect(!mc_should_fire(kMcActionMove, kButtonNone),
                     "mc: move+none does not fire");
        ok &= expect(!mc_should_fire(kMcActionMove, kButtonLeft),
                     "mc: left-move does not fire");
        ok &= expect(!mc_should_fire(kMcActionUp, kButtonLeft),
                     "mc: left-up does not fire");
        ok &= expect(!mc_should_fire(kMcActionUp, kButtonRight),
                     "mc: right-up does not fire");
        ok &= expect(!mc_should_fire(kMcActionDown, kButtonMiddle),
                     "mc: middle-down does not fire");
    }

    // Right-drag does fire; left-click path retained.
    {
        ok &= expect(mc_should_fire(kMcActionMove, kButtonRight),
                     "mc: right-drag fires");
        ok &= expect(mc_should_fire(kMcActionDown, kButtonLeft),
                     "mc: left-down fires");
    }

    // Release clears held state at the game latch level: a consumed latch
    // does not refire without a new press.
    {
        McState s;
        mc_init(&s);
        ok &= expect(mc_request_fire(&s, 500, 100), "latch: press accepted");
        mc_fixed_update(&s);
        ok &= expect(!s.pendingFire, "latch: consumed after tick");
        ok &= expect(s.mFired == 1, "latch: exactly one shot");
    }

    if (g_failures == 0) std::cout << "Compositor pointer buttons test PASS\n";
    return g_failures == 0 ? 0 : 1;
}
