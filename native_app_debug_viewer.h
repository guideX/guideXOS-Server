#pragma once

#include "native_app_process_table.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gxos { namespace apps {

    class NativeAppDebugViewer {
    public:
        static uint64_t Launch();

    private:
        static int main(int argc, char** argv);
        static void refresh();
        static void updateDisplay();
        static void handleKeyPress(int keyCode, bool& running);
        static void drawText(const std::string& text);
        static void drawRect(int x, int y, int w, int h, int r, int g, int b);
        static std::string truncate(const std::string& text, size_t maxLength);
        static std::string formatDuration(const std::chrono::steady_clock::time_point& timePoint);

        static uint64_t s_windowId;
        static std::vector<NativeAppProcessInfo> s_processes;
        static int s_selectedIndex;
        static int s_scrollOffset;
        static int s_lastKeyCode;
        static bool s_keyDown;
    };

}} // namespace gxos::apps
