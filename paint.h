#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>
#include <vector>
#include <cstdint>

namespace gxos { namespace apps {

    enum class PaintTool : uint8_t {
        Pencil = 0,
        Brush  = 1,
        Eraser = 2,
        Line   = 3,
        Rect   = 4,
        Circle = 5,
        Fill   = 6
    };

    /// Paint application ported from guideXOS.Legacy Paint.cs
    /// Features: drawing canvas, tool selection, color palette, brush sizes, save/load
    class Paint {
    public:
        /// Launch a new Paint window
        static uint64_t Launch();

    private:
        static int main(int argc, char** argv);

        // Drawing
        static void drawPixel(int x, int y, uint32_t color);
        static void drawLine(int x0, int y0, int x1, int y1, uint32_t color, int size);
        static void drawRect(int x0, int y0, int x1, int y1, uint32_t color);
        static void drawCircle(int cx, int cy, int r, uint32_t color);
        static void floodFill(int x, int y, uint32_t newColor);
        static void clearCanvas();

        // UI
        static void updateDisplay();
        static void handleKeyPress(int keyCode);

        // State
        static uint64_t s_windowId;
        static PaintTool s_tool;
        static uint32_t s_color;
        static uint32_t s_bgColor;
        static int s_brushSize;
        static int s_lastX;
        static int s_lastY;
        static bool s_drawing;

        // Canvas (logical pixel buffer for state tracking)
        static std::vector<uint32_t> s_canvas;
        static int s_canvasW;
        static int s_canvasH;

        static int s_lastKeyCode;
        static bool s_keyDown;

        // Layout
        static constexpr int kWinW = 800;
        static constexpr int kWinH = 600;
        static constexpr int kToolbarH = 80;

        // Default colour palette (match legacy)
        static const uint32_t kPalette[];
        static constexpr int kPaletteCount = 16;

        // Tool names
        static const char* kToolNames[];
    };

}} // namespace gxos::apps
