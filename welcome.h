#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>

namespace gxos { namespace apps {

    /// Welcome window - informational dialog shown on first launch.
    /// Ported from guideXOS.Legacy Welcome.cs
    /// Shows banner text and project info.
    class Welcome {
    public:
        /// Launch the Welcome window
        /// @return PID of the window process
        static uint64_t Launch();

    private:
        static int main(int argc, char** argv);

        // UI
        static void updateDisplay();
        static void handleKeyPress(int keyCode);

        // State
        static uint64_t s_windowId;
        static bool s_closed;
        static int s_lastKeyCode;
        static bool s_keyDown;

        // Layout (match legacy: 280x225)
        static constexpr int kWinW = 320;
        static constexpr int kWinH = 200;
    };

}} // namespace gxos::apps
