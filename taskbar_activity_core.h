#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gxos { namespace gui {

// A small producer contract shared by the hosted compositor and freestanding
// kernel taskbar. Producers identify either one canonical app or one live
// window token; each key owns one operation status at a time.
enum class TaskbarProgressState : uint8_t {
    None,
    Indeterminate,
    Determinate,
    Completed,
    Failed
};

struct TaskbarActivity {
    bool attention{false};
    TaskbarProgressState progress{TaskbarProgressState::None};
    uint8_t percent{0};
};

struct TaskbarActivityRecord {
    char appId[96]{};
    uint64_t windowId{0};
    TaskbarActivity activity{};
    uint64_t sequence{0};
    bool occupied{false};
};

enum class TaskbarDockEdge : uint8_t { Bottom, Top, Left, Right };

static const size_t kTaskbarNoPreviewEntry = static_cast<size_t>(-1);

struct TaskbarPreviewHoverState {
    size_t entryIndex{kTaskbarNoPreviewEntry};
    bool visible() const { return entryIndex != kTaskbarNoPreviewEntry; }
};

inline TaskbarPreviewHoverState DecideTaskbarPreviewHoverTransition(
    const TaskbarPreviewHoverState& current,
    size_t hoveredEntryIndex,
    size_t entryCount,
    bool hoveredEntryRunning,
    bool pointerInsidePreview,
    bool popupSuppressed) {
    if (popupSuppressed) return TaskbarPreviewHoverState{};
    if (current.visible() && current.entryIndex < entryCount && pointerInsidePreview) return current;
    if (hoveredEntryIndex < entryCount && hoveredEntryRunning) {
        TaskbarPreviewHoverState next;
        next.entryIndex = hoveredEntryIndex;
        return next;
    }
    return TaskbarPreviewHoverState{};
}

struct TaskbarRect {
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
    int width() const { return right > left ? right - left : 0; }
    int height() const { return bottom > top ? bottom - top : 0; }
    bool empty() const { return right <= left || bottom <= top; }
    bool contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
};

inline TaskbarRect PositionTaskbarPopup(
    const TaskbarRect& anchor, const TaskbarRect& bounds,
    int requestedWidth, int requestedHeight, TaskbarDockEdge edge, int gap = 6) {
    if (anchor.empty() || bounds.empty() || requestedWidth <= 0 || requestedHeight <= 0) return TaskbarRect{};
    const int boundWidth = bounds.right - bounds.left;
    const int boundHeight = bounds.bottom - bounds.top;
    const int width = requestedWidth < boundWidth ? requestedWidth : boundWidth;
    const int height = requestedHeight < boundHeight ? requestedHeight : boundHeight;
    if (width <= 0 || height <= 0) return TaskbarRect{};
    int x = anchor.left;
    int y = anchor.top;
    if (edge == TaskbarDockEdge::Bottom) y = anchor.top - gap - height;
    else if (edge == TaskbarDockEdge::Top) y = anchor.bottom + gap;
    else if (edge == TaskbarDockEdge::Left) x = anchor.right + gap;
    else x = anchor.left - gap - width;
    if (edge == TaskbarDockEdge::Bottom || edge == TaskbarDockEdge::Top)
        x = anchor.left + (anchor.right - anchor.left - width) / 2;
    else
        y = anchor.top + (anchor.bottom - anchor.top - height) / 2;
    if (x < bounds.left) x = bounds.left;
    if (x > bounds.right - width) x = bounds.right - width;
    if (y < bounds.top) y = bounds.top;
    if (y > bounds.bottom - height) y = bounds.bottom - height;
    return TaskbarRect{x, y, x + width, y + height};
}

inline bool ComputeTaskbarThumbnailSize(
    int sourceWidth, int sourceHeight, int maximumWidth, int maximumHeight,
    int& outputWidth, int& outputHeight) {
    outputWidth = 0;
    outputHeight = 0;
    if (sourceWidth <= 0 || sourceHeight <= 0 || maximumWidth <= 0 || maximumHeight <= 0) return false;
    const uint64_t widthLimitedHeight = static_cast<uint64_t>(sourceHeight) * static_cast<uint64_t>(maximumWidth) /
        static_cast<uint64_t>(sourceWidth);
    if (widthLimitedHeight <= static_cast<uint64_t>(maximumHeight)) {
        outputWidth = maximumWidth;
        outputHeight = static_cast<int>(widthLimitedHeight == 0 ? 1 : widthLimitedHeight);
    } else {
        const uint64_t heightLimitedWidth = static_cast<uint64_t>(sourceWidth) * static_cast<uint64_t>(maximumHeight) /
            static_cast<uint64_t>(sourceHeight);
        outputWidth = static_cast<int>(heightLimitedWidth == 0 ? 1 : heightLimitedWidth);
        outputHeight = maximumHeight;
    }
    return outputWidth <= maximumWidth && outputHeight <= maximumHeight;
}

class TaskbarActivityStore {
public:
    static const size_t Capacity = 32;

    bool set(const char* appId, uint64_t windowId, TaskbarProgressState progress,
             uint8_t percent, bool attention) {
        const bool hasAppId = validAppId(appId);
        if ((windowId == 0) == !hasAppId || !validProgress(progress, percent)) return false;
        int index = find(appId, windowId);
        if (progress == TaskbarProgressState::None && !attention) {
            if (index >= 0) clearAt(static_cast<size_t>(index));
            return true;
        }
        if (index < 0) {
            for (size_t i = 0; i < Capacity; ++i) {
                if (!m_records[i].occupied) { index = static_cast<int>(i); break; }
            }
        }
        if (index < 0) return false;

        TaskbarActivityRecord& record = m_records[static_cast<size_t>(index)];
        record = TaskbarActivityRecord{};
        if (windowId == 0) copyAppId(record.appId, appId);
        record.windowId = windowId;
        record.activity.attention = attention;
        record.activity.progress = progress;
        record.activity.percent = percent;
        record.sequence = ++m_nextSequence;
        record.occupied = true;
        return true;
    }

    bool clear(const char* appId, uint64_t windowId) {
        const bool hasAppId = validAppId(appId);
        if ((windowId == 0) == !hasAppId) return false;
        const int index = find(appId, windowId);
        if (index >= 0) clearAt(static_cast<size_t>(index));
        return true;
    }

    void acknowledgeWindow(const char* appId, uint64_t windowId) {
        for (size_t i = 0; i < Capacity; ++i) {
            TaskbarActivityRecord& record = m_records[i];
            if (!record.occupied || !record.activity.attention) continue;
            const bool sameWindow = windowId != 0 && record.windowId == windowId;
            const bool sameApp = validAppId(appId) && record.windowId == 0 && sameString(record.appId, appId);
            if (!sameWindow && !sameApp) continue;
            record.activity.attention = false;
            if (record.activity.progress == TaskbarProgressState::None) clearAt(i);
            else record.sequence = ++m_nextSequence;
        }
    }

    void removeWindow(uint64_t windowId) {
        if (windowId == 0) return;
        for (size_t i = 0; i < Capacity; ++i) {
            if (m_records[i].occupied && m_records[i].windowId == windowId) clearAt(i);
        }
    }

    template <typename WindowId>
    TaskbarActivity resolve(const char* appId, const WindowId* windowIds, size_t windowCount) const {
        TaskbarActivity result{};
        uint64_t bestSequence = 0;
        for (size_t i = 0; i < Capacity; ++i) {
            const TaskbarActivityRecord& record = m_records[i];
            if (!record.occupied) continue;
            bool matches = record.windowId == 0 && validAppId(appId) && sameString(record.appId, appId);
            if (!matches && record.windowId != 0 && windowIds != nullptr) {
                for (size_t w = 0; w < windowCount; ++w) {
                    if (static_cast<uint64_t>(windowIds[w]) == record.windowId) { matches = true; break; }
                }
            }
            if (!matches) continue;
            result.attention = result.attention || record.activity.attention;
            if (record.activity.progress != TaskbarProgressState::None && record.sequence >= bestSequence) {
                result.progress = record.activity.progress;
                result.percent = record.activity.percent;
                bestSequence = record.sequence;
            }
        }
        return result;
    }

    const TaskbarActivityRecord& record(size_t index) const { return m_records[index]; }

private:
    TaskbarActivityRecord m_records[Capacity]{};
    uint64_t m_nextSequence{0};

    static size_t stringLength(const char* value) {
        size_t length = 0;
        if (value == nullptr) return 0;
        while (length < 96 && value[length] != '\0') ++length;
        return length;
    }
    static bool validAppId(const char* appId) {
        const size_t length = stringLength(appId);
        return length > 0 && length < 96;
    }
    static bool sameString(const char* left, const char* right) {
        if (left == nullptr || right == nullptr) return false;
        while (*left && *right && *left == *right) { ++left; ++right; }
        return *left == *right;
    }
    static void copyAppId(char* out, const char* appId) {
        size_t i = 0;
        while (appId[i] != '\0' && i < 95) { out[i] = appId[i]; ++i; }
        out[i] = '\0';
    }
    static bool validProgress(TaskbarProgressState state, uint8_t percent) {
        switch (state) {
        case TaskbarProgressState::None: return percent == 0;
        case TaskbarProgressState::Indeterminate: return percent == 0;
        case TaskbarProgressState::Determinate: return percent <= 100;
        case TaskbarProgressState::Completed: return percent == 100;
        case TaskbarProgressState::Failed: return percent <= 100;
        default: return false;
        }
    }
    int find(const char* appId, uint64_t windowId) const {
        for (size_t i = 0; i < Capacity; ++i) {
            const TaskbarActivityRecord& record = m_records[i];
            if (!record.occupied) continue;
            if (windowId != 0 && record.windowId == windowId) return static_cast<int>(i);
            if (windowId == 0 && record.windowId == 0 && sameString(record.appId, appId)) return static_cast<int>(i);
        }
        return -1;
    }
    void clearAt(size_t index) { m_records[index] = TaskbarActivityRecord{}; }
};

}} // namespace gxos::gui
