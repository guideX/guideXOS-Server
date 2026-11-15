#pragma once

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
