#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>

namespace gxos { namespace apps {

    /// On-screen keyboard ported from guideXOS.Legacy OnScreenKeyboard.cs
    /// Displays a virtual QWERTY keyboard that injects key events via IPC.
    class OnScreenKeyboard {
    public:
        /// Launch the on-screen keyboard window
        /// @return PID of the keyboard process
        static uint64_t Launch();

    private:
        static int main(int argc, char** argv);

        // UI
        static void updateDisplay();
        static void handleKeyPress(int keyCode);
        static void sendKey(char ch);

        // State
        static uint64_t s_windowId;
        static bool s_shift;
        static bool s_caps;
        static int s_lastKeyCode;
        static bool s_keyDown;

        // Keyboard layout (US QWERTY)
        static const char* kRows[];
        static const char* kRowsShift[];
        static constexpr int kRowCount = 4;

        // Layout constants (match legacy)
        static constexpr int kKeyW    = 48;
        static constexpr int kKeyH    = 48;
        static constexpr int kGap     = 4;
        static constexpr int kWinW    = 800;
        static constexpr int kWinH    = 280;
        static constexpr int kShiftW  = 80;
        static constexpr int kCapsW   = 100;
        static constexpr int kSpaceW  = 300;
        static constexpr int kBackW   = 100;
        static constexpr int kEnterW  = 80;
    };

}} // namespace gxos::apps
