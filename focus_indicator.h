//
// Focus Indicator - Visual Focus System
//
// Provides visual feedback showing which UI element has keyboard focus
// Uses dashed rectangles with corner dots for clear visibility
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#ifdef _WIN32
#include <windows.h>

namespace gxos {
namespace gui {

class FocusIndicator {
public:
    // Draw a dashed focus rectangle with corner dots
    // x, y, w, h: rectangle bounds
    // dashLength: length of each dash segment
    // gapLength: length of gap between dashes
    // dotSize: size of corner dots
    static void DrawFocusRect(HDC dc, int x, int y, int w, int h, 
                              int dashLength = 4, int gapLength = 2, int dotSize = 3);
    
    // Draw focus indicator with custom color
    static void DrawFocusRectColored(HDC dc, int x, int y, int w, int h,
                                     COLORREF color, int dashLength = 4, 
                                     int gapLength = 2, int dotSize = 3);
    
    // Draw animated focus (cycling dots)
    static void DrawAnimatedFocus(HDC dc, int x, int y, int w, int h, 
                                  uint64_t tickCount);
    
private:
    static void DrawDashedLine(HDC dc, int x1, int y1, int x2, int y2,
                              int dashLength, int gapLength);
    static void DrawCornerDot(HDC dc, int x, int y, int size);
};

} // namespace gui
} // namespace gxos

#endif // _WIN32
