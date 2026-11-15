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
