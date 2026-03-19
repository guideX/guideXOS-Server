#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>

namespace gxos { namespace apps {

    /// Shutdown confirmation dialog ported from guideXOS.Legacy ShutdownDialog.cs
    /// Shows Yes/No buttons; Yes triggers orderly shutdown, No closes the dialog.
    class ShutdownDialog {
    public:
        /// Launch the shutdown dialog as a GUI window
        /// @return PID of the dialog process
        static uint64_t Launch();

    private:
        static int main(int argc, char** argv);

        // UI helpers
        static void updateDisplay();
        static void handleKeyPress(int keyCode);

        // State
        static uint64_t s_windowId;
        static bool s_confirmed;
        static int s_lastKeyCode;
        static bool s_keyDown;

        // Layout constants (match legacy)
        static constexpr int kDialogW   = 360;
        static constexpr int kDialogH   = 160;
        static constexpr int kPadding   = 12;
        static constexpr int kBtnW      = 80;
        static constexpr int kBtnH      = 28;
        static constexpr int kGap       = 10;
    };

}} // namespace gxos::apps
