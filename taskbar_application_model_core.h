#pragma once

#include "taskbar_activity_core.h"

// Small policy helpers shared by the hosted taskbar model and the freestanding
// native taskbar. Keep this header free of the standard library so the kernel
// can use the same identity and activation rules without pulling in containers.
namespace gxos { namespace gui {

enum class TaskbarCoreClickAction {
    None,
    Launch,
    Activate,
    Minimize,
    ShowWindowChooser
};

// Native taskbar initialization can run before the writable root VFS mount is
// ready. Keep the initial unavailable result retryable, then load the store
// exactly once when a writable mount becomes available.
struct TaskbarPinStoreLoadGate {
    TaskbarPinStoreLoadGate() : m_loaded(false), m_unavailableReported(false) {}

    void reset() {
        m_loaded = false;
        m_unavailableReported = false;
    }

    bool loaded() const { return m_loaded; }
    bool shouldAttempt(bool writableStoreAvailable) const {
        return !m_loaded && writableStoreAvailable;
    }

    bool markUnavailableReported() {
        if (m_unavailableReported) return false;
        m_unavailableReported = true;
        return true;
    }

    void markLoaded() { m_loaded = true; }

private:
    bool m_loaded;
    bool m_unavailableReported;
};

inline bool HasCanonicalTaskbarAppIdentity(const char* appId) {
    return appId != nullptr && appId[0] != '\0';
}

inline bool SameCanonicalTaskbarAppIdentity(const char* left, const char* right) {
    if (!HasCanonicalTaskbarAppIdentity(left) || !HasCanonicalTaskbarAppIdentity(right)) return false;
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

inline TaskbarCoreClickAction DecideTaskbarCoreClick(
    unsigned int windowCount,
    bool focused,
    bool minimized,
    bool launchable) {
    if (windowCount == 0u) return launchable ? TaskbarCoreClickAction::Launch : TaskbarCoreClickAction::None;
    if (windowCount > 1u) return TaskbarCoreClickAction::ShowWindowChooser;
    return focused && !minimized ? TaskbarCoreClickAction::Minimize : TaskbarCoreClickAction::Activate;
}

}} // namespace gxos::gui
