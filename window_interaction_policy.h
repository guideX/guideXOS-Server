#pragma once

#include "window_bounds_store.h"

#include <algorithm>
#include <cstdint>

namespace gxos { namespace gui {

    struct WindowWorkArea {
        int left{0};
        int top{0};
        int right{0};
        int bottom{0};
    };

    inline void ClampNormalWindowBounds(NormalWindowBounds& bounds, const WindowWorkArea& work) {
        const int usableWidth = std::max(1, work.right - work.left);
        const int usableHeight = std::max(1, work.bottom - work.top);
        const int minWidth = std::min(160, usableWidth);
        const int minHeight = std::min(120, usableHeight);
        bounds.w = std::max(minWidth, std::min(bounds.w, usableWidth));
        bounds.h = std::max(minHeight, std::min(bounds.h, usableHeight));
        const int maxX = std::max(work.left, work.right - bounds.w);
        const int maxY = std::max(work.top, work.bottom - bounds.h);
        bounds.x = std::max(work.left, std::min(bounds.x, maxX));
        bounds.y = std::max(work.top, std::min(bounds.y, maxY));
    }

    inline bool IsTitleBarControlAt(int windowX, int windowY, int windowWidth, bool resizable,
        int titleBarHeight, int controlPadding, int titleButtonGap, int mouseX, int mouseY) {
        const int buttonSize = std::max(12, titleBarHeight - controlPadding * 2);
        const int buttonY = windowY + controlPadding;
        const int closeLeft = windowX + windowWidth - controlPadding - buttonSize;
        const int tombLeft = closeLeft - titleButtonGap - buttonSize;
        const int maxLeft = tombLeft - titleButtonGap - buttonSize;
        const int minLeft = maxLeft - titleButtonGap - buttonSize;
        if (mouseY < buttonY || mouseY >= buttonY + buttonSize) return false;
        const auto inButton = [&](int left) {
            return mouseX >= left && mouseX < left + buttonSize;
        };
        return inButton(closeLeft) || inButton(tombLeft) ||
            (resizable && inButton(maxLeft)) || inButton(minLeft);
    }

    /// Small, deterministic state machine for title-bar double-click recognition.
    /// Controls, client-area clicks, and an already-started drag reset the sequence.
    class TitleBarDoubleClickTracker {
    public:
        void reset() {
            lastWindowId_ = 0;
            lastClickTicks_ = 0;
        }

        bool onDown(uint64_t windowId, uint64_t ticks, bool inTitleBar,
            bool onControl, bool dragActive) {
            if (!inTitleBar || onControl || dragActive) {
                reset();
                return false;
            }
            if (lastWindowId_ == windowId && ticks >= lastClickTicks_ &&
                ticks - lastClickTicks_ < 450) {
                reset();
                return true;
            }
            lastWindowId_ = windowId;
            lastClickTicks_ = ticks;
            return false;
        }

    private:
        uint64_t lastWindowId_{0};
        uint64_t lastClickTicks_{0};
    };

}} // namespace gxos::gui
