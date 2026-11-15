#include "desktop_config.h"
#include "taskbar_application_model.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const char* name) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << name << "\n";
    if (!condition) ++failures;
}

gxos::gui::TaskbarWindowSnapshot window(uint64_t id, const char* title, const char* appId,
                                        bool minimized = false, bool focused = false) {
    gxos::gui::TaskbarWindowSnapshot result;
    result.id = id;
    result.title = title;
    result.appId = appId;
    result.minimized = minimized;
    result.focused = focused;
    return result;
}
}

int main() {
    check(gxos::gui::HasCanonicalTaskbarAppIdentity("org.guide.test"), "shared canonical identity accepts app IDs");
    check(!gxos::gui::HasCanonicalTaskbarAppIdentity(""), "shared canonical identity rejects empty IDs");
    check(gxos::gui::SameCanonicalTaskbarAppIdentity("org.guide.test", "org.guide.test"), "shared identity comparison groups exact IDs");
    check(!gxos::gui::SameCanonicalTaskbarAppIdentity("", ""), "shared identity comparison never groups anonymous windows");
    check(gxos::gui::DecideTaskbarCoreClick(2, false, false, true) ==
          gxos::gui::TaskbarCoreClickAction::ShowWindowChooser, "shared click policy selects grouped windows");
    check(gxos::gui::DecideTaskbarCoreClick(1, true, false, true) ==
          gxos::gui::TaskbarCoreClickAction::Minimize, "shared click policy minimizes focused window");
    check(gxos::gui::DecideTaskbarCoreClick(0, false, false, true) ==
          gxos::gui::TaskbarCoreClickAction::Launch, "shared click policy launches an idle pinned app");
    using namespace gxos::gui;

    const std::vector<std::string> pins{"app.editor", "app.editor", "app.mail"};
    const std::vector<TaskbarWindowSnapshot> initialWindows{
        window(10, "Untitled", "app.editor", false, true),
        window(11, "Notes", "app.editor", true),
        window(12, "Untitled", "app.mail"),
        window(13, "Untitled", ""),
        window(14, "Untitled", "")
    };
    std::vector<TaskbarApplicationEntry> entries = BuildTaskbarApplicationEntries(pins, initialWindows);
    check(entries.size() == 4, "pins and canonical app identities group without title grouping");
    check(entries[0].pinned && entries[0].appId == "app.editor" && entries[0].windowCount() == 2,
        "pinned app groups its running windows and keeps pin order");
    check(entries[1].pinned && entries[1].appId == "app.mail" && entries[1].windowCount() == 1,
        "second pinned app remains a separate taskbar entry");
    check(entries[0].appId != entries[1].appId && entries[0].windows.front().title == entries[1].windows.front().title,
        "unrelated applications with identical titles remain separate");
    check(entries[2].appId == "" && entries[2].windowCount() == 1 && entries[3].windowCount() == 1,
        "windows without canonical identity remain individual entries");
    check(entries[0].label == "app.editor" && entries[0].focusedWindowId() == 10,
        "group uses canonical app identity and tracks focused member");

    const std::vector<TaskbarWindowSnapshot> reorderedWindows{
        window(42, "Second", "app.alpha"),
        window(31, "Same title", "app.beta"),
        window(43, "First", "app.alpha")
    };
    const std::vector<TaskbarApplicationEntry> orderedEntries =
        BuildTaskbarApplicationEntries({}, reorderedWindows);
    check(orderedEntries.size() == 2 && orderedEntries[0].appId == "app.beta" &&
          orderedEntries[1].appId == "app.alpha",
        "running app order remains stable when z-order changes");

    TaskbarApplicationEntry single;
    single.appId = "app.editor";
    single.label = "Editor";
    single.windows.push_back(window(20, "Document", "app.editor", false, true));
    TaskbarClickDecision click = DecideTaskbarEntryClick(single);
    check(click.action == TaskbarClickAction::Minimize && click.windowId == 20,
        "clicking the focused single window minimizes it");
    single.windows.front().focused = false;
    single.windows.front().minimized = true;
    click = DecideTaskbarEntryClick(single);
    check(click.action == TaskbarClickAction::Activate && click.windowId == 20,
        "clicking a minimized single window restores it");

    check(DecideTaskbarCoreClick(1, true, true, true) == TaskbarCoreClickAction::Activate,
        "shared click policy restores a focused-but-minimized window");

    TaskbarApplicationEntry launcher;
    launcher.appId = "app.reader";
    launcher.label = "Reader";
    launcher.pinned = true;
    launcher.relaunchable = true;
    click = DecideTaskbarEntryClick(launcher);
    check(click.action == TaskbarClickAction::Launch,
        "inactive pinned launcher requests the App Model launch path");
    launcher.windows.push_back(window(30, "Book", "app.reader"));
    click = DecideTaskbarEntryClick(launcher);
    check(click.action == TaskbarClickAction::Activate && click.windowId == 30,
        "pinned launcher activates its running window");
    launcher.windows.clear();
    check(DecideTaskbarEntryClick(launcher).action == TaskbarClickAction::Launch,
        "closing the final window leaves a pinned launcher ready to launch");
    launcher.pinned = false;
    const std::vector<TaskbarContextAction> unpinnedLauncherActions = BuildTaskbarContextActions(launcher);
    check(std::any_of(unpinnedLauncherActions.begin(), unpinnedLauncherActions.end(), [](const TaskbarContextAction& action) {
        return action.kind == TaskbarContextActionKind::PinApp;
    }), "installed unpinned application exposes Pin to Taskbar");
    launcher.pinned = true;
    const std::vector<TaskbarContextAction> pinnedLauncherActions = BuildTaskbarContextActions(launcher);
    check(std::any_of(pinnedLauncherActions.begin(), pinnedLauncherActions.end(), [](const TaskbarContextAction& action) {
        return action.kind == TaskbarContextActionKind::UnpinApp;
    }), "pinned application exposes Unpin from Taskbar");

    TaskbarApplicationEntry unavailable;
    unavailable.appId = "app.removed";
    unavailable.label = "app.removed";
    unavailable.pinned = true;
    unavailable.missingApplication = true;
    const std::vector<TaskbarContextAction> unavailableActions = BuildTaskbarContextActions(unavailable);
    check(DecideTaskbarEntryClick(unavailable).action == TaskbarClickAction::None &&
          std::any_of(unavailableActions.begin(), unavailableActions.end(), [](const TaskbarContextAction& action) {
              return action.kind == TaskbarContextActionKind::UnpinApp;
          }), "unavailable pinned identity stays visible and can be removed safely");

    TaskbarApplicationEntry group = entries.front();
    check(DecideTaskbarEntryClick(group).action == TaskbarClickAction::ShowWindowChooser,
        "clicking a multi-window app opens a window chooser");
    const std::vector<TaskbarContextAction> groupActions = BuildTaskbarContextActions(group);
    bool hasCloseAll = false;
    bool hasMinimize = false;
    bool hasMaximize = false;
    for (const TaskbarContextAction& action : groupActions) {
        hasCloseAll = hasCloseAll || action.kind == TaskbarContextActionKind::CloseAllWindows;
        hasMinimize = hasMinimize || action.kind == TaskbarContextActionKind::MinimizeWindow;
        hasMaximize = hasMaximize || action.kind == TaskbarContextActionKind::ToggleMaximizeWindow;
    }
    check(hasCloseAll && hasMinimize && hasMaximize,
        "multi-window context actions expose supported window controls");
    check(std::count_if(groupActions.begin(), groupActions.end(), [](const TaskbarContextAction& action) {
        return action.kind == TaskbarContextActionKind::ActivateWindow;
    }) == 2 && std::any_of(groupActions.begin(), groupActions.end(), [](const TaskbarContextAction& action) {
        return action.kind == TaskbarContextActionKind::CloseWindow && action.windowId == 10;
    }), "group menu targets each member and its focused-window controls by window ID");
    TaskbarApplicationEntry oneWindow = entries[1];
    const std::vector<TaskbarContextAction> singleActions = BuildTaskbarContextActions(oneWindow);
    check(std::none_of(singleActions.begin(), singleActions.end(), [](const TaskbarContextAction& action) {
        return action.kind == TaskbarContextActionKind::CloseAllWindows;
    }), "single-window context menu omits Close All Windows");

    const std::vector<TaskbarWindowSnapshot> afterClose{
        window(10, "Untitled", "app.editor", false, true),
        window(12, "Untitled", "app.mail")
    };
    entries = BuildTaskbarApplicationEntries(pins, afterClose);
    check(entries[0].windowCount() == 1 && entries[0].windows.front().id == 10,
        "closing a window removes it from the next live taskbar snapshot");

    std::vector<int> widths{120, 140, 100};
    const auto bottom = LayoutTaskbarButtons(widths, 0, 728, 1024, 768,
        false, 216, 734, 6, 184);
    check(bottom.size() == widths.size() && bottom.front().left == 216 && bottom.back().right <= 840,
        "bottom taskbar button layout reserves the tray and clock area");
    const auto top = LayoutTaskbarButtons(widths, -1920, 0, 0, 1080,
        false, -1704, 6, 6, 180);
    check(top.size() == widths.size() && top.front().left == -1704 && top.back().right <= -180,
        "top taskbar layout supports a non-origin monitor coordinate");
    const auto left = LayoutTaskbarButtons(widths, -1920, -120, -1880, 960,
        true, -1916, -72, 4, -1880);
    check(!left.empty() && left.front().left == -1916 && left.front().top == -72 && left.back().bottom <= 956,
        "left taskbar layout stays within a vertically docked monitor");
    const size_t hit = HitTestTaskbarButton(left, left.front().left, left.front().top);
    check(hit == 0 && HitTestTaskbarButton(left, 900, 900) == static_cast<size_t>(-1),
        "taskbar hit testing uses the same docked button rectangles");
    const auto right = LayoutTaskbarButtons(widths, 1880, -120, 1920, 960,
        true, 1884, -72, 4, 1880);
    check(!right.empty() && right.front().left == 1884 && right.front().top == -72 &&
          right.back().bottom <= 956,
        "right taskbar layout stays within a vertically docked non-origin monitor");
    const auto compactBottom = LayoutTaskbarButtons({120}, 0, 212, 320, 240,
        false, 44, 218, 4, 260, 20);
    check(compactBottom.size() == 1 && compactBottom.front().left >= 0 &&
          compactBottom.front().top >= 212 && compactBottom.front().right <= 260 &&
          compactBottom.front().bottom <= 240 &&
          HitTestTaskbarButton(compactBottom, compactBottom.front().left, compactBottom.front().top) == 0,
        "compact bottom taskbar layout and hit testing stay inside a small display");
    const auto compactLeft = LayoutTaskbarButtons({120}, 0, 0, 32, 240,
        true, 4, 60, 4, 0, 28);
    check(compactLeft.size() == 1 && compactLeft.front().left >= 0 &&
          compactLeft.front().right <= 32 && compactLeft.front().top >= 0 &&
          compactLeft.front().bottom <= 240,
        "compact vertical taskbar layout stays inside a small display");

    TaskbarApplicationEntry previewEntry;
    previewEntry.appId = "app.preview";
    previewEntry.label = "Preview app";
    previewEntry.windows = {window(71, "One", "app.preview"), window(72, "Two", "app.preview", true)};
    const auto groupPreviews = BuildTaskbarPreviewWindows(previewEntry);
    check(groupPreviews.size() == 2 && groupPreviews[0].id == 71 && groupPreviews[1].id == 72,
        "grouped preview model includes each live window with stable activation IDs");
    previewEntry.windows.erase(previewEntry.windows.begin() + 1);
    const auto refreshedPreviews = BuildTaskbarPreviewWindows(previewEntry);
    check(refreshedPreviews.size() == 1 && refreshedPreviews.front().id == 71,
        "preview model drops a closed or tombstoned window on the next snapshot");
    previewEntry.windows = {window(73, "Single", "app.preview")};
    check(BuildTaskbarPreviewWindows(previewEntry).size() == 1,
        "single-window preview model produces one selectable card");

    TaskbarPreviewHoverState hoverState;
    hoverState = DecideTaskbarPreviewHoverTransition(hoverState, 1, 3, true, false, false);
    check(hoverState.visible() && hoverState.entryIndex == 1,
        "hovering a running taskbar entry opens its preview");
    hoverState = DecideTaskbarPreviewHoverTransition(
        hoverState, kTaskbarNoPreviewEntry, 3, false, true, false);
    check(hoverState.visible() && hoverState.entryIndex == 1,
        "moving the pointer into the preview keeps it open");
    hoverState = DecideTaskbarPreviewHoverTransition(hoverState, 2, 3, true, false, false);
    check(hoverState.visible() && hoverState.entryIndex == 2,
        "moving to an adjacent running entry switches previews");
    hoverState = DecideTaskbarPreviewHoverTransition(
        hoverState, kTaskbarNoPreviewEntry, 3, false, false, false);
    check(!hoverState.visible(), "leaving the taskbar and preview dismisses the popup");
    TaskbarPreviewHoverState suppressedState;
    suppressedState.entryIndex = 1;
    hoverState = DecideTaskbarPreviewHoverTransition(
        suppressedState, 1, 3, true, false, true);
    check(!hoverState.visible(), "start-menu or context-popup suppression dismisses previews");

    TaskbarActivityStore activityStore;
    check(activityStore.set("app.preview", 0, TaskbarProgressState::Determinate, 42, true),
        "taskbar activity contract accepts bounded determinate progress and attention");
    std::vector<TaskbarApplicationEntry> activityEntries{previewEntry};
    ApplyTaskbarActivity(activityEntries, activityStore);
    check(activityEntries.front().activity.attention &&
          activityEntries.front().activity.progress == TaskbarProgressState::Determinate &&
          activityEntries.front().activity.percent == 42,
        "application-scoped activity resolves onto the matching taskbar entry");
    activityStore.acknowledgeWindow("app.preview", 73);
    activityEntries = {previewEntry};
    ApplyTaskbarActivity(activityEntries, activityStore);
    check(!activityEntries.front().activity.attention &&
          activityEntries.front().activity.progress == TaskbarProgressState::Determinate,
        "acknowledging an application clears attention and preserves its live operation progress");
    check(!activityStore.set("app.preview", 0, TaskbarProgressState::Determinate, 101, false),
        "taskbar progress rejects values above one hundred percent");
    check(!activityStore.set("app.preview", 73, TaskbarProgressState::Determinate, 40, false),
        "taskbar activity rejects ambiguous app and window ownership tokens");
    check(!activityStore.set(nullptr, 0, TaskbarProgressState::Indeterminate, 0, false),
        "taskbar activity rejects records without an application or window owner");
    check(activityStore.set(nullptr, 73, TaskbarProgressState::Indeterminate, 0, false),
        "taskbar activity contract accepts a window-token operation");
    const uint64_t previewWindowIds[]{73};
    TaskbarActivity resolvedActivity = activityStore.resolve("app.preview", previewWindowIds, 1);
    check(resolvedActivity.progress == TaskbarProgressState::Indeterminate,
        "window-token activity resolves to the owning taskbar group");
    check(activityStore.set(nullptr, 73, TaskbarProgressState::Completed, 100, false),
        "taskbar activity contract accepts completed state at one hundred percent");
    resolvedActivity = activityStore.resolve("app.preview", previewWindowIds, 1);
    check(resolvedActivity.progress == TaskbarProgressState::Completed && resolvedActivity.percent == 100,
        "completed operation status replaces indeterminate activity");
    check(activityStore.set(nullptr, 73, TaskbarProgressState::Failed, 100, false),
        "taskbar activity contract accepts failed operations");
    resolvedActivity = activityStore.resolve("app.preview", previewWindowIds, 1);
    check(resolvedActivity.progress == TaskbarProgressState::Failed && !resolvedActivity.attention,
        "failed progress resolves as an error indicator without false attention state");
    activityStore.removeWindow(73);
    resolvedActivity = activityStore.resolve("app.preview", previewWindowIds, 1);
    check(resolvedActivity.progress == TaskbarProgressState::Determinate,
        "closing a window token removes its status without dropping application-scoped progress");
    check(activityStore.clear("app.preview", 0), "taskbar activity can be explicitly cleared by app identity");
    activityEntries = {previewEntry};
    ApplyTaskbarActivity(activityEntries, activityStore);
    check(!activityEntries.front().activity.attention && activityEntries.front().activity.progress == TaskbarProgressState::None,
        "cleared operation and attention states disappear from the next taskbar snapshot");
    check(activityStore.set("app.attention", 0, TaskbarProgressState::None, 0, true),
        "attention-only requests do not need a fabricated progress value");
    activityStore.acknowledgeWindow("app.attention", 900);
    check(activityStore.resolve("app.attention", static_cast<const uint64_t*>(nullptr), 0).progress == TaskbarProgressState::None &&
          !activityStore.resolve("app.attention", static_cast<const uint64_t*>(nullptr), 0).attention,
        "acknowledging the owning application clears an attention-only record");
    TaskbarActivityStore boundedActivityStore;
    bool capacityAccepted = true;
    for (int i = 0; i < static_cast<int>(TaskbarActivityStore::Capacity); ++i) {
        const std::string appId = "app.capacity." + std::to_string(i);
        capacityAccepted = capacityAccepted && boundedActivityStore.set(
            appId.c_str(), 0, TaskbarProgressState::Indeterminate, 0, false);
    }
    check(capacityAccepted && !boundedActivityStore.set("app.capacity.overflow", 0,
        TaskbarProgressState::Indeterminate, 0, false),
        "taskbar activity storage stays within its fixed record capacity");

    int thumbnailW = 0;
    int thumbnailH = 0;
    check(ComputeTaskbarThumbnailSize(1920, 1080, 180, 96, thumbnailW, thumbnailH) &&
          thumbnailW == 170 && thumbnailH == 96,
        "thumbnail sizing preserves source aspect ratio inside bounded dimensions");
    check(!ComputeTaskbarThumbnailSize(0, 1080, 180, 96, thumbnailW, thumbnailH),
        "thumbnail sizing rejects unavailable source surfaces");
    const TaskbarRect popup = PositionTaskbarPopup({300, 700, 360, 728}, {0, 0, 800, 768},
        240, 180, TaskbarDockEdge::Bottom);
    check(popup.left >= 0 && popup.top >= 0 && popup.right <= 800 && popup.bottom <= 768 && popup.bottom < 700,
        "preview popup positions above a bottom dock and clips to the display bounds");
    const TaskbarRect verticalPopup = PositionTaskbarPopup({-1920, 220, -1880, 260}, {-1920, 0, 0, 900},
        220, 160, TaskbarDockEdge::Left);
    check(verticalPopup.left >= -1880 && verticalPopup.top >= 0 && verticalPopup.bottom <= 900,
        "preview popup positions beside a left dock on non-origin monitor coordinates");
    const TaskbarRect topPopup = PositionTaskbarPopup({500, 0, 560, 36}, {0, 0, 1280, 720},
        260, 184, TaskbarDockEdge::Top, 0);
    const TaskbarRect rightPopup = PositionTaskbarPopup({1240, 240, 1280, 280}, {0, 0, 1280, 720},
        220, 160, TaskbarDockEdge::Right, 0);
    check(topPopup.top == 36 && topPopup.bottom <= 720 &&
          rightPopup.right == 1240 && rightPopup.left >= 0 && rightPopup.bottom <= 720,
        "preview popups respect top and right taskbar dock positions");

    const auto overflowBottom = LayoutTaskbarButtons({100, 100, 100, 100, 100}, 0, 700, 480, 740,
        false, 100, 706, 6, 100, 28);
    check(overflowBottom.size() == 3 && overflowBottom.back().entryIndex == kTaskbarOverflowEntry &&
          overflowBottom.back().right <= 380,
        "horizontal overflow reserves the right-side system controls and exposes an overflow button");
    check(HitTestTaskbarButton(overflowBottom, overflowBottom.back().left, overflowBottom.back().top) == kTaskbarOverflowEntry,
        "overflow control hit testing uses its visible rectangle");
    const auto overflowLeft = LayoutTaskbarButtons({120, 120, 120, 120, 120, 120}, 0, 0, 32, 240,
        true, 4, 60, 4, 0, 28);
    check(overflowLeft.size() == 5 && overflowLeft.back().entryIndex == kTaskbarOverflowEntry &&
          overflowLeft.back().bottom <= 240,
        "vertical overflow stays bounded and leaves remaining applications reachable");

    const std::filesystem::path configPath = "tmp/taskbar-application-model-test/desktop.json";
    std::filesystem::create_directories(configPath.parent_path());
    DesktopConfigData config;
    config.pinned = {"Start Menu App"};
    config.pinnedAppIds = {"app.startmenu"};
    config.taskbarPinnedAppIds = {"app.editor", "app.mail"};
    std::string error;
    const bool saved = DesktopConfig::Save(configPath.string(), config, error);
    DesktopConfigData restored;
    error.clear();
    const bool loaded = saved && DesktopConfig::Load(configPath.string(), restored, error);
    check(loaded && restored.taskbarPinnedAppIds == config.taskbarPinnedAppIds,
        "taskbar pins persist separately from Start Menu pins");
    check(loaded && restored.pinnedAppIds == config.pinnedAppIds,
        "taskbar pin persistence preserves existing Start Menu identities");

    return failures == 0 ? 0 : 1;
}
