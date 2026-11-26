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

// Desktop initialization can precede the persistent VFS mount. Keep persisted
// taskbar preferences retryable until a root mount exists, then consume one
// configuration snapshot for the desktop lifetime.
struct TaskbarSettingsLoadGate {
    TaskbarSettingsLoadGate() : m_loaded(false) {}

    void reset() { m_loaded = false; }
    bool loaded() const { return m_loaded; }
    bool shouldAttempt(bool settingsVolumeMounted) const {
        return !m_loaded && settingsVolumeMounted;
    }
    void markLoaded() { m_loaded = true; }

private:
    bool m_loaded;
};

// A popup that overlaps the dock owns pointer input over the taskbar surface.
// This matters for top-docked auto-hide, where the Start menu can begin at the
// same screen edge as the taskbar and otherwise lose clicks to dock hit-testing.
inline bool ShouldRoutePointerToTaskbar(bool pointerInsideTaskbar, bool popupOwnsPointer) {
    return pointerInsideTaskbar && !popupOwnsPointer;
}

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

// App IDs cross the taskbar persistence and native-window boundaries. Keep
// their bounded validation shared so a trusted launch cannot be truncated or
// smuggle control characters into the pin store.
inline bool IsSafeTaskbarAppIdentity(const char* appId, unsigned int capacity) {
    if (!HasCanonicalTaskbarAppIdentity(appId) || capacity < 2u) return false;
    unsigned int length = 0;
    while (appId[length]) {
        const unsigned char value = static_cast<unsigned char>(appId[length]);
        if (length + 1u >= capacity || value < 0x20u || value == 0x7Fu || value == '=') {
            return false;
        }
        ++length;
    }
    return true;
}

inline bool MatchesTrustedTaskbarAppIdentity(const char* requestedId,
                                             const char* registeredId,
                                             unsigned int capacity) {
    return IsSafeTaskbarAppIdentity(requestedId, capacity) &&
        IsSafeTaskbarAppIdentity(registeredId, capacity) &&
        SameCanonicalTaskbarAppIdentity(requestedId, registeredId);
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
