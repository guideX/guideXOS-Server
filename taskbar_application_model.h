#pragma once

#include "taskbar_application_model_core.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gxos { namespace gui {

struct TaskbarWindowSnapshot {
    uint64_t id{0};
    std::string title;
    std::string appId;
    bool minimized{false};
    bool maximized{false};
    bool focused{false};
    bool tombstoned{false};
    bool resizable{true};
};

struct TaskbarApplicationEntry {
    std::string appId;
    std::string label;
    bool pinned{false};
    bool relaunchable{false};
    bool missingApplication{false};
    TaskbarActivity activity{};
    std::vector<TaskbarWindowSnapshot> windows;

    bool running() const { return !windows.empty(); }
    size_t windowCount() const { return windows.size(); }
    uint64_t focusedWindowId() const {
        for (const TaskbarWindowSnapshot& window : windows) {
            if (window.focused) return window.id;
        }
        return 0;
    }
};

struct TaskbarButtonRect {
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
    size_t entryIndex{0};

    bool contains(int x, int y) const {
        return x >= left && x < right && y >= top && y < bottom;
    }
};

static const size_t kTaskbarOverflowEntry = static_cast<size_t>(-2);

// Build stable pins first, then running unpinned apps in their supplied Z-order.
// Windows without a canonical App Model identity always keep individual entries.
inline std::vector<TaskbarApplicationEntry> BuildTaskbarApplicationEntries(
    const std::vector<std::string>& pinnedAppIds,
    const std::vector<TaskbarWindowSnapshot>& windows) {
    std::vector<TaskbarApplicationEntry> entries;
    std::unordered_map<std::string, size_t> appEntryById;
    std::unordered_set<std::string> seenPins;
    std::unordered_set<uint64_t> seenWindowIds;

    for (const std::string& appId : pinnedAppIds) {
        if (!HasCanonicalTaskbarAppIdentity(appId.c_str()) || !seenPins.insert(appId).second) continue;
        TaskbarApplicationEntry entry;
        entry.appId = appId;
        entry.label = appId;
        entry.pinned = true;
        appEntryById.emplace(appId, entries.size());
        entries.push_back(std::move(entry));
    }
    const size_t pinnedEntryCount = entries.size();

    for (const TaskbarWindowSnapshot& window : windows) {
        if (window.id == 0 || !seenWindowIds.insert(window.id).second) continue;
        if (HasCanonicalTaskbarAppIdentity(window.appId.c_str())) {
            auto found = appEntryById.find(window.appId);
            if (found == appEntryById.end()) {
                TaskbarApplicationEntry entry;
                entry.appId = window.appId;
                entry.label = window.title.empty() ? window.appId : window.title;
                const size_t index = entries.size();
                entries.push_back(std::move(entry));
                found = appEntryById.emplace(window.appId, index).first;
            }
            TaskbarApplicationEntry& entry = entries[found->second];
            if (entry.windows.empty() && !window.title.empty() && !entry.pinned) entry.label = window.title;
            entry.windows.push_back(window);
            continue;
        }

        TaskbarApplicationEntry entry;
        entry.label = window.title.empty() ? "Window" : window.title;
        entry.windows.push_back(window);
        entries.push_back(std::move(entry));
    }

    // Running entries are ordered by their oldest live window ID so focus and
    // z-order changes do not shuffle applications around the taskbar.
    std::stable_sort(entries.begin() + pinnedEntryCount, entries.end(),
        [](const TaskbarApplicationEntry& left, const TaskbarApplicationEntry& right) {
            const auto orderKey = [](const TaskbarApplicationEntry& entry) {
                uint64_t key = UINT64_MAX;
                for (const TaskbarWindowSnapshot& window : entry.windows) {
                    if (window.id < key) key = window.id;
                }
                return key;
            };
            return orderKey(left) < orderKey(right);
        });
    return entries;
}

inline uint64_t TaskbarEntryPrimaryWindowId(const TaskbarApplicationEntry& entry) {
    if (entry.windows.empty()) return 0;
    for (const TaskbarWindowSnapshot& window : entry.windows) {
        if (window.focused) return window.id;
    }
    for (const TaskbarWindowSnapshot& window : entry.windows) {
        if (!window.minimized && !window.tombstoned) return window.id;
    }
    return entry.windows.back().id;
}

// A click on a single focused window minimizes it. Otherwise it activates the
// focused member, the first available member, or the last minimized member.
inline uint64_t TaskbarEntryWindowForClick(const TaskbarApplicationEntry& entry) {
    if (entry.windows.empty()) return 0;
    for (const TaskbarWindowSnapshot& window : entry.windows) {
        if (window.focused) return window.id;
    }
    for (const TaskbarWindowSnapshot& window : entry.windows) {
        if (!window.minimized && !window.tombstoned) return window.id;
    }
    return entry.windows.back().id;
}

inline void ApplyTaskbarActivity(
    std::vector<TaskbarApplicationEntry>& entries,
    const TaskbarActivityStore& activityStore) {
    for (TaskbarApplicationEntry& entry : entries) {
        std::vector<uint64_t> ids;
        ids.reserve(entry.windows.size());
        for (const TaskbarWindowSnapshot& window : entry.windows) {
            if (!window.tombstoned && window.id != 0) ids.push_back(window.id);
        }
        entry.activity = activityStore.resolve(entry.appId.c_str(), ids.data(), ids.size());
    }
}

inline std::vector<TaskbarWindowSnapshot> BuildTaskbarPreviewWindows(
    const TaskbarApplicationEntry& entry) {
    std::vector<TaskbarWindowSnapshot> windows;
    windows.reserve(entry.windows.size());
    for (const TaskbarWindowSnapshot& window : entry.windows) {
        if (window.id != 0) windows.push_back(window);
    }
    return windows;
}

// Layout is shared by draw and hit testing. The caller supplies the text-driven
// preferred widths; all resulting rectangles stay within the supplied taskbar.
inline std::vector<TaskbarButtonRect> LayoutTaskbarButtons(
    const std::vector<int>& preferredWidths,
    int left, int top, int right, int bottom,
    bool vertical,
    int startX, int startY,
    int gap,
    int reservedRight,
    int preferredHeight = 28,
    int minHorizontalWidth = 56) {
    std::vector<TaskbarButtonRect> result;
    if (preferredWidths.empty() || right <= left || bottom <= top) return result;

    const int available = vertical
        ? std::max(0, bottom - startY - 4)
        : std::max(0, right - reservedRight - startX);
    const int minWidth = vertical ? std::max(0, right - left - 8) : std::min(std::max(0, minHorizontalWidth), available);
    const int overflowWidth = vertical ? minWidth : std::min(42, available);
    size_t visibleCount = preferredWidths.size();
    const int totalPreferred = [&]() {
        int sum = 0;
        for (int width : preferredWidths) sum += std::max(minWidth, width);
        return sum + static_cast<int>(preferredWidths.size() > 1 ? preferredWidths.size() - 1 : 0) * gap;
    }();
    if (!vertical && totalPreferred > available) {
        const int roomWithOverflow = std::max(0, available - overflowWidth - gap);
        visibleCount = 0;
        int used = 0;
        while (visibleCount < preferredWidths.size()) {
            const int need = std::max(minWidth, preferredWidths[visibleCount]) + (visibleCount == 0 ? 0 : gap);
            if (used + need > roomWithOverflow) break;
            used += need;
            ++visibleCount;
        }
        // On very compact displays, keep a single application reachable in
        // the remaining sliver instead of replacing it with an overflow-only
        // control. The shared clock/tray reservation is still respected.
        if (visibleCount == 0 && preferredWidths.size() == 1 && available > 0) visibleCount = 1;
    }
    const int verticalCapacity = vertical
        ? std::max(0, (available + gap) / std::max(1, preferredHeight + gap))
        : static_cast<int>(visibleCount);
    if (vertical && preferredWidths.size() > static_cast<size_t>(verticalCapacity) && verticalCapacity > 0) {
        visibleCount = static_cast<size_t>(verticalCapacity - 1);
    }
    const bool showOverflow = visibleCount < preferredWidths.size();
    const int entryGaps = static_cast<int>(visibleCount > 1 ? visibleCount - 1 : 0) * gap;
    const int perEntry = vertical || visibleCount == 0 ? 0 : std::max(0,
        (available - (showOverflow ? overflowWidth + gap : 0) - entryGaps) /
        static_cast<int>(visibleCount));

    int x = startX;
    int y = startY;
    for (size_t i = 0; i < visibleCount; ++i) {
        if (vertical && static_cast<int>(i) >= verticalCapacity) break;
        const int width = vertical
            ? std::max(0, right - left - 8)
            : std::min(std::max(minWidth, preferredWidths[i]), perEntry);
        const int height = vertical ? std::min(preferredHeight, bottom - y)
            : std::min(std::max(0, preferredHeight), bottom - top);
        if (width <= 0 || height <= 0) break;
        result.push_back(TaskbarButtonRect{x, y, x + width, y + height, i});
        if (vertical) y += height + gap;
        else x += width + gap;
    }
    if (showOverflow) {
        if (vertical) {
            if (verticalCapacity > 0) {
                const int height = std::min(preferredHeight, bottom - y);
                if (height > 0) result.push_back(TaskbarButtonRect{left + 4, y, right - 4, y + height, kTaskbarOverflowEntry});
            }
        } else {
            const int xOverflow = x;
            const int width = std::min(overflowWidth, std::max(0, right - reservedRight - xOverflow));
            const int height = std::max(0, bottom - top - 12);
            if (width > 0 && height > 0) result.push_back(TaskbarButtonRect{xOverflow, startY, xOverflow + width, startY + height, kTaskbarOverflowEntry});
        }
    }
    return result;
}

inline size_t HitTestTaskbarButton(const std::vector<TaskbarButtonRect>& buttons, int x, int y) {
    for (const TaskbarButtonRect& button : buttons) {
        if (button.contains(x, y)) return button.entryIndex;
    }
    return static_cast<size_t>(-1);
}

enum class TaskbarWindowAction {
    Activate,
    Minimize,
    ToggleMaximize,
    Close
};

enum class TaskbarClickAction {
    None,
    Launch,
    Activate,
    Minimize,
    ShowWindowChooser
};

struct TaskbarClickDecision {
    TaskbarClickAction action{TaskbarClickAction::None};
    uint64_t windowId{0};
};

inline TaskbarClickDecision DecideTaskbarEntryClick(const TaskbarApplicationEntry& entry) {
    const bool focused = !entry.windows.empty() && entry.windows.front().focused;
    const bool minimized = entry.windows.empty() || entry.windows.front().minimized || entry.windows.front().tombstoned;
    const TaskbarCoreClickAction action = DecideTaskbarCoreClick(
        static_cast<unsigned int>(entry.windows.size()), focused, minimized, entry.relaunchable);
    switch (action) {
    case TaskbarCoreClickAction::Launch: return { TaskbarClickAction::Launch, 0 };
    case TaskbarCoreClickAction::Activate: return { TaskbarClickAction::Activate, entry.windows.front().id };
    case TaskbarCoreClickAction::Minimize: return { TaskbarClickAction::Minimize, entry.windows.front().id };
    case TaskbarCoreClickAction::ShowWindowChooser: return { TaskbarClickAction::ShowWindowChooser, 0 };
    case TaskbarCoreClickAction::None:
    default: return { TaskbarClickAction::None, 0 };
    }
}

enum class TaskbarContextActionKind {
    ActivateWindow,
    MinimizeWindow,
    ToggleMaximizeWindow,
    CloseWindow,
    CloseAllWindows,
    PinApp,
    UnpinApp,
    LaunchApp
};

struct TaskbarContextAction {
    TaskbarContextActionKind kind{TaskbarContextActionKind::ActivateWindow};
    uint64_t windowId{0};
};

inline std::vector<TaskbarContextAction> BuildTaskbarContextActions(
    const TaskbarApplicationEntry& entry) {
    std::vector<TaskbarContextAction> actions;
    for (const TaskbarWindowSnapshot& window : entry.windows) {
        actions.push_back({TaskbarContextActionKind::ActivateWindow, window.id});
    }
    if (entry.windows.empty()) {
        if (entry.relaunchable) actions.push_back({TaskbarContextActionKind::LaunchApp, 0});
    } else {
        const uint64_t primaryId = TaskbarEntryPrimaryWindowId(entry);
        const auto selected = std::find_if(entry.windows.begin(), entry.windows.end(), [primaryId](const TaskbarWindowSnapshot& window) {
            return window.id == primaryId;
        });
        if (selected != entry.windows.end()) {
            if (!selected->minimized && !selected->tombstoned) {
                actions.push_back({TaskbarContextActionKind::MinimizeWindow, selected->id});
            }
            if (selected->resizable) {
                actions.push_back({TaskbarContextActionKind::ToggleMaximizeWindow, selected->id});
            }
            actions.push_back({TaskbarContextActionKind::CloseWindow, selected->id});
        }
        if (entry.windows.size() > 1) actions.push_back({TaskbarContextActionKind::CloseAllWindows, 0});
    }
    if (entry.pinned) actions.push_back({TaskbarContextActionKind::UnpinApp, 0});
    else if (entry.relaunchable) actions.push_back({TaskbarContextActionKind::PinApp, 0});
    return actions;
}

}} // namespace gxos::gui
