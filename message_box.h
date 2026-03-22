#pragma once

// Prevent Windows API macro collision - Windows defines MessageBox as a macro
// that expands to MessageBoxW or MessageBoxA, which mangles our class name
#ifdef MessageBox
#undef MessageBox
#endif

#include "process.h"
#include "ipc_bus.h"
#include <string>
#include <functional>

namespace gxos { namespace apps {

    /// MessageBox - Simple popup message dialog ported from guideXOS.Legacy MessageBox.cs
    /// Shows a title, message text, and an OK button.
    class MessageBox {
    public:
        /// Launch a new MessageBox with the given title and message
        /// @param title Window title
        /// @param message Body text to display
        /// @return PID of the dialog process
        static uint64_t Launch(const std::string& title, const std::string& message);

    private:
        static int main(int argc, char** argv);

        // UI helpers
        static void updateDisplay();
        static void handleKeyPress(int keyCode);

        // State
        static uint64_t s_windowId;
        static std::string s_title;
        static std::string s_message;
        static bool s_dismissed;
        static int s_lastKeyCode;
        static bool s_keyDown;

        // Layout
        static constexpr int kDialogW = 320;
        static constexpr int kDialogH = 140;
        static constexpr int kPadding = 12;
        static constexpr int kBtnW   = 80;
        static constexpr int kBtnH   = 28;
    };

}} // namespace gxos::apps
