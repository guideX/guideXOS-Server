//
// Focus Indicator - Implementation
//
// Copyright (c) 2026 guideXOS Server
//

#include "focus_indicator.h"
#include <cmath>

#ifdef _WIN32

namespace gxos {
namespace gui {

void FocusIndicator::DrawDashedLine(HDC dc, int x1, int y1, int x2, int y2,
                                    int dashLength, int gapLength) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    double length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0) return;
    
    double dirX = dx / length;
    double dirY = dy / length;
    
    double pos = 0.0;
    bool drawing = true;
    
    while (pos < length) {
        double startPos = pos;
        double endPos = pos + (drawing ? dashLength : gapLength);
        if (endPos > length) endPos = length;
        
        if (drawing) {
            int sx = x1 + static_cast<int>(startPos * dirX);
            int sy = y1 + static_cast<int>(startPos * dirY);
            int ex = x1 + static_cast<int>(endPos * dirX);
            int ey = y1 + static_cast<int>(endPos * dirY);
            
            MoveToEx(dc, sx, sy, nullptr);
            LineTo(dc, ex, ey);
        }
        
        pos = endPos;
        drawing = !drawing;
    }
}

void FocusIndicator::DrawCornerDot(HDC dc, int x, int y, int size) {
    HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(100, 150, 255));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    
    Ellipse(dc, x - size, y - size, x + size, y + size);
    
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void FocusIndicator::DrawFocusRect(HDC dc, int x, int y, int w, int h,
                                   int dashLength, int gapLength, int dotSize) {
    DrawFocusRectColored(dc, x, y, w, h, RGB(100, 150, 255), 
                        dashLength, gapLength, dotSize);
}

void FocusIndicator::DrawFocusRectColored(HDC dc, int x, int y, int w, int h,
                                          COLORREF color, int dashLength,
                                          int gapLength, int dotSize) {
    // Create pen for dashed lines
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    
    // Inset by 1 pixel to ensure visibility
    int x1 = x + 1;
    int y1 = y + 1;
    int x2 = x + w - 1;
    int y2 = y + h - 1;
    
    // Draw four sides with dashed lines
    DrawDashedLine(dc, x1, y1, x2, y1, dashLength, gapLength); // Top
    DrawDashedLine(dc, x2, y1, x2, y2, dashLength, gapLength); // Right
    DrawDashedLine(dc, x2, y2, x1, y2, dashLength, gapLength); // Bottom
    DrawDashedLine(dc, x1, y2, x1, y1, dashLength, gapLength); // Left
    
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    
    // Draw corner dots
    if (dotSize > 0) {
        DrawCornerDot(dc, x1, y1, dotSize);         // Top-left
        DrawCornerDot(dc, x2, y1, dotSize);         // Top-right
        DrawCornerDot(dc, x2, y2, dotSize);         // Bottom-right
        DrawCornerDot(dc, x1, y2, dotSize);         // Bottom-left
    }
}

void FocusIndicator::DrawAnimatedFocus(HDC dc, int x, int y, int w, int h,
                                       uint64_t tickCount) {
    // Animate by shifting the dash pattern based on tick count
    int offset = static_cast<int>((tickCount / 50) % 6);
    
    // Cycle through colors
    int colorPhase = static_cast<int>((tickCount / 200) % 3);
    COLORREF color;
    switch (colorPhase) {
        case 0: color = RGB(100, 150, 255); break;
        case 1: color = RGB(150, 200, 255); break;
        default: color = RGB(200, 220, 255); break;
    }
    
    DrawFocusRectColored(dc, x - offset, y - offset, w + offset * 2, h + offset * 2,
                        color, 4, 2, 3);
}

} // namespace gui
} // namespace gxos

#endif // _WIN32
