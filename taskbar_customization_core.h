#pragma once

#include <stdint.h>
#include "taskbar_activity_core.h"

namespace gxos { namespace gui {

enum class TaskbarDock : uint8_t { Bottom, Top, Left, Right };
enum class TaskbarSizeMode : uint8_t { Compact, Standard, Expanded };
enum class TaskbarPresentation : uint8_t { IconsOnly, IconsAndLabels };
enum class TaskbarAutoHideState : uint8_t { Visible, WaitingToHide, Hidden, WaitingToReveal };

struct TaskbarPreferences {
    TaskbarDock dock{TaskbarDock::Bottom};
    TaskbarSizeMode size{TaskbarSizeMode::Standard};
    TaskbarPresentation presentation{TaskbarPresentation::IconsAndLabels};
    bool autoHide{false};
    bool showPreviews{true};
    bool showActivity{true};
};

inline const char* TaskbarDockName(TaskbarDock value) {
    switch (value) {
    case TaskbarDock::Top: return "top";
    case TaskbarDock::Left: return "left";
    case TaskbarDock::Right: return "right";
    case TaskbarDock::Bottom:
    default: return "bottom";
    }
}

inline TaskbarDock ParseTaskbarDock(const char* value) {
    if (!value) return TaskbarDock::Bottom;
    if (value[0] == 't' && value[1] == 'o' && value[2] == 'p' && value[3] == '\0') return TaskbarDock::Top;
    if (value[0] == 'l' && value[1] == 'e' && value[2] == 'f' && value[3] == 't' && value[4] == '\0') return TaskbarDock::Left;
    if (value[0] == 'r' && value[1] == 'i' && value[2] == 'g' && value[3] == 'h' && value[4] == 't' && value[5] == '\0') return TaskbarDock::Right;
    return TaskbarDock::Bottom;
}

inline const char* TaskbarSizeModeName(TaskbarSizeMode value) {
    switch (value) {
    case TaskbarSizeMode::Compact: return "compact";
    case TaskbarSizeMode::Expanded: return "expanded";
    case TaskbarSizeMode::Standard:
    default: return "standard";
    }
}

inline TaskbarSizeMode ParseTaskbarSizeMode(const char* value) {
    if (!value) return TaskbarSizeMode::Standard;
    if (value[0] == 'c' && value[1] == 'o' && value[2] == 'm' && value[3] == 'p' &&
        value[4] == 'a' && value[5] == 'c' && value[6] == 't' && value[7] == '\0') return TaskbarSizeMode::Compact;
    if (value[0] == 'e' && value[1] == 'x' && value[2] == 'p' && value[3] == 'a' &&
        value[4] == 'n' && value[5] == 'd' && value[6] == 'e' && value[7] == 'd' && value[8] == '\0') return TaskbarSizeMode::Expanded;
    return TaskbarSizeMode::Standard;
}

inline const char* TaskbarPresentationName(TaskbarPresentation value) {
    return value == TaskbarPresentation::IconsOnly ? "icons-only" : "icons-labels";
}

inline TaskbarPresentation ParseTaskbarPresentation(const char* value) {
    if (value && value[0] == 'i' && value[1] == 'c' && value[2] == 'o' && value[3] == 'n' &&
        value[4] == 's' && value[5] == '-' && value[6] == 'o' && value[7] == 'n' &&
        value[8] == 'l' && value[9] == 'y' && value[10] == '\0') return TaskbarPresentation::IconsOnly;
    return TaskbarPresentation::IconsAndLabels;
}

inline int TaskbarThickness(TaskbarSizeMode mode, int displayWidth, int displayHeight) {
    int requested = 40;
    if (mode == TaskbarSizeMode::Compact) requested = 32;
    else if (mode == TaskbarSizeMode::Expanded) requested = 56;
    int limit = displayWidth < displayHeight ? displayWidth : displayHeight;
    if (limit < 1) return 0;
    return requested < limit ? requested : limit;
}

inline TaskbarRect TaskbarGeometryForDisplay(int left, int top, int width, int height,
                                             TaskbarDock dock, TaskbarSizeMode size) {
    if (width <= 0 || height <= 0) return TaskbarRect{};
    const int thickness = TaskbarThickness(size, width, height);
    switch (dock) {
    case TaskbarDock::Top: return {left, top, left + width, top + thickness};
    case TaskbarDock::Left: return {left, top, left + thickness, top + height};
    case TaskbarDock::Right: return {left + width - thickness, top, left + width, top + height};
    case TaskbarDock::Bottom:
    default: return {left, top + height - thickness, left + width, top + height};
    }
}

inline TaskbarRect TaskbarGeometryForViewport(int originX, int originY, int width, int height,
                                              TaskbarDock dock, TaskbarSizeMode size) {
    return TaskbarGeometryForDisplay(originX, originY, width, height, dock, size);
}

inline TaskbarRect TaskbarWorkAreaForDisplay(int left, int top, int width, int height,
                                             TaskbarDock dock, TaskbarSizeMode size,
                                             bool autoHide) {
    if (width <= 0 || height <= 0) return TaskbarRect{};
    if (autoHide) return {left, top, left + width, top + height};
    const TaskbarRect taskbar = TaskbarGeometryForDisplay(left, top, width, height, dock, size);
    switch (dock) {
    case TaskbarDock::Top: return {left, taskbar.bottom, left + width, top + height};
    case TaskbarDock::Left: return {taskbar.right, top, left + width, top + height};
    case TaskbarDock::Right: return {left, top, taskbar.left, top + height};
    case TaskbarDock::Bottom:
    default: return {left, top, left + width, taskbar.top};
    }
}

inline TaskbarRect TaskbarRevealRect(const TaskbarRect& taskbar, TaskbarDock dock, int revealThickness = 2) {
    if (taskbar.empty()) return TaskbarRect{};
    if (revealThickness < 1) revealThickness = 1;
    switch (dock) {
    case TaskbarDock::Top: return {taskbar.left, taskbar.top, taskbar.right,
        taskbar.top + (taskbar.height() < revealThickness ? taskbar.height() : revealThickness)};
    case TaskbarDock::Left: return {taskbar.left, taskbar.top,
        taskbar.left + (taskbar.width() < revealThickness ? taskbar.width() : revealThickness), taskbar.bottom};
    case TaskbarDock::Right: return {taskbar.right - (taskbar.width() < revealThickness ? taskbar.width() : revealThickness),
        taskbar.top, taskbar.right, taskbar.bottom};
    case TaskbarDock::Bottom:
    default: return {taskbar.left, taskbar.bottom - (taskbar.height() < revealThickness ? taskbar.height() : revealThickness),
        taskbar.right, taskbar.bottom};
    }
}

struct TaskbarAutoHideController {
    TaskbarAutoHideState state{TaskbarAutoHideState::Visible};
    uint64_t enteredAtMs{0};

    void reset() { state = TaskbarAutoHideState::Visible; enteredAtMs = 0; }

    bool advance(bool enabled, bool pointerAtRevealEdge, bool pointerOverTaskbarOrPopup,
                 uint64_t nowMs, uint32_t hideDelayMs = 500, uint32_t revealDelayMs = 80) {
        const TaskbarAutoHideState oldState = state;
        if (!enabled || pointerOverTaskbarOrPopup) {
            state = TaskbarAutoHideState::Visible;
            enteredAtMs = nowMs;
            return oldState != state;
        }
        switch (state) {
        case TaskbarAutoHideState::Visible:
            if (!pointerAtRevealEdge) {
                state = TaskbarAutoHideState::WaitingToHide;
                enteredAtMs = nowMs;
            }
            break;
        case TaskbarAutoHideState::WaitingToHide:
            if (pointerAtRevealEdge) {
                state = TaskbarAutoHideState::Visible;
                enteredAtMs = nowMs;
            } else if (nowMs - enteredAtMs >= hideDelayMs) {
                state = TaskbarAutoHideState::Hidden;
                enteredAtMs = nowMs;
            }
            break;
        case TaskbarAutoHideState::Hidden:
            if (pointerAtRevealEdge) {
                state = revealDelayMs == 0 ? TaskbarAutoHideState::Visible : TaskbarAutoHideState::WaitingToReveal;
                enteredAtMs = nowMs;
            }
            break;
        case TaskbarAutoHideState::WaitingToReveal:
            if (!pointerAtRevealEdge) {
                state = TaskbarAutoHideState::Hidden;
                enteredAtMs = nowMs;
            } else if (nowMs - enteredAtMs >= revealDelayMs) {
                state = TaskbarAutoHideState::Visible;
                enteredAtMs = nowMs;
            }
            break;
        }
        return oldState != state;
    }

    bool visible() const {
        return state == TaskbarAutoHideState::Visible || state == TaskbarAutoHideState::WaitingToHide;
    }
};

} }
