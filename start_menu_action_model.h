#pragma once

#include "app_registry.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace gxos { namespace gui {

enum class StartMenuAppContextRowKind {
    Open,
    PinToggle,
    Separator,
    AppAction,
    RemoveRecent
};

struct StartMenuAppContextRow {
    StartMenuAppContextRowKind kind = StartMenuAppContextRowKind::Open;
    std::string label;
    apps::AppActionInfo action;
};

struct StartMenuAppContextMenu {
    std::vector<StartMenuAppContextRow> rows;
};

inline std::vector<StartMenuAppContextRow> BuildStartMenuAppActionRows(
    const apps::AppActionList& snapshot) {
    std::vector<StartMenuAppContextRow> rows;
    const size_t count = std::min(snapshot.count, snapshot.actions.size());
    rows.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const apps::AppActionInfo& action = snapshot.actions[i];
        if (!action.available || !action.registrationCurrent || action.registrationGeneration == 0 ||
            action.appId.empty() || action.appId != snapshot.appId || action.actionId.empty() ||
            action.label.empty() || action.label.size() > apps::kAppModelMaxActionLabelBytes) {
            continue;
        }
        StartMenuAppContextRow row;
        row.kind = StartMenuAppContextRowKind::AppAction;
        row.label = action.label;
        row.action = action;
        rows.push_back(std::move(row));
    }
    return rows;
}

inline StartMenuAppContextMenu BuildStartMenuAppContextMenu(
    const apps::AppActionList& snapshot,
    bool allProgramsView,
    bool pinnedToDesktop) {
    StartMenuAppContextMenu menu;
    menu.rows.push_back({ StartMenuAppContextRowKind::Open, "Open", {} });
    menu.rows.push_back({ StartMenuAppContextRowKind::PinToggle,
        pinnedToDesktop ? "Unpin from Desktop" : "Pin to Desktop", {} });

    const std::vector<StartMenuAppContextRow> actions = BuildStartMenuAppActionRows(snapshot);
    if (!actions.empty()) {
        menu.rows.push_back({ StartMenuAppContextRowKind::Separator, "", {} });
        menu.rows.insert(menu.rows.end(), actions.begin(), actions.end());
    }

    if (!allProgramsView) {
        menu.rows.push_back({ StartMenuAppContextRowKind::Separator, "", {} });
        menu.rows.push_back({ StartMenuAppContextRowKind::RemoveRecent, "Remove from This List", {} });
    }
    return menu;
}

inline int ClampStartMenuContextOrigin(int requestedOrigin, int viewportExtent, int menuExtent) {
    const int maxOrigin = std::max(0, viewportExtent - menuExtent);
    return std::max(0, std::min(requestedOrigin, maxOrigin));
}

}} // namespace gxos::gui
