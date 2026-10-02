#pragma once

#include "app_registry.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace gxos { namespace apps {

constexpr size_t kFileExplorerOpenWithLabelMaxChars = 30;

struct FileExplorerOpenWithItem {
    DocumentHandlerInfo handler;
    std::string label;
};

// Immutable, value-owned menu snapshot. Its activation targets survive registry
// vector relocation, but the owner/generation snapshot is revalidated on use.
struct FileExplorerOpenWithMenuSnapshot {
    std::array<FileExplorerOpenWithItem, kAppModelMaxDocumentHandlersPerExtension> items{};
    std::string targetPath;
    size_t count = 0;
    bool visible = false;
    bool truncated = false;

    bool Select(size_t index, DocumentHandlerInfo& handler, std::string& path) const {
        if (!visible || index >= count) return false;
        handler = items[index].handler;
        path = targetPath;
        return true;
    }

    void Reset() {
        items = {};
        targetPath.clear();
        count = 0;
        visible = false;
        truncated = false;
    }
};

inline std::string boundedOpenWithLabel(const std::string& text, size_t maxChars) {
    if (text.size() <= maxChars) return text;
    if (maxChars <= 3) return text.substr(0, maxChars);
    return text.substr(0, maxChars - 3) + "...";
}

inline FileExplorerOpenWithMenuSnapshot BuildFileExplorerOpenWithMenu(
    const DocumentHandlerList& handlers,
    bool isDirectory,
    const std::string& targetPath) {
    FileExplorerOpenWithMenuSnapshot menu;
    if (isDirectory || !handlers.validExtension || targetPath.empty()) return menu;

    menu.targetPath = targetPath;
    menu.truncated = handlers.truncated;
    for (size_t i = 0; i < handlers.count && menu.count < menu.items.size(); ++i) {
        const DocumentHandlerInfo& handler = handlers.handlers[i];
        if (!handler.available) continue;

        size_t sameLabelCount = 0;
        size_t sameLabelOrdinal = 0;
        for (size_t j = 0; j < handlers.count; ++j) {
            if (!handlers.handlers[j].available || handlers.handlers[j].displayName != handler.displayName) continue;
            if (handlers.handlers[j].appId == handler.appId) sameLabelOrdinal = sameLabelCount;
            ++sameLabelCount;
        }

        std::string label = handler.displayName;
        if (sameLabelCount > 1) {
            const std::string suffix = " (" + std::to_string(sameLabelOrdinal + 1) + ")";
            label = boundedOpenWithLabel(label, kFileExplorerOpenWithLabelMaxChars - suffix.size()) + suffix;
        } else {
            label = boundedOpenWithLabel(label, kFileExplorerOpenWithLabelMaxChars);
        }
        menu.items[menu.count].handler = handler;
        menu.items[menu.count].label = std::move(label);
        ++menu.count;
    }
    menu.visible = menu.count != 0;
    if (menu.count < handlers.availableHandlerCount) menu.truncated = true;
    if (!menu.visible) menu.Reset();
    return menu;
}

}} // namespace gxos::apps
