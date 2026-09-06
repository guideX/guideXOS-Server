#include "trash.h"

#include "compositor.h"
#include "gui_protocol.h"
#include "ipc_bus.h"
#include "icon_theme_manager.h"
#include "logger.h"
#include "process.h"
#include "desktop_control_theme.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>

namespace gxos {
namespace apps {

using namespace gxos::gui;

namespace {
    constexpr const char* kTrashRootPath = "/Trash";
    constexpr const char* kTrashInfoSuffix = ".trashinfo";

    bool endsWithText(const std::string& value, const char* suffix) {
        if (!suffix) return false;
        size_t suffixLen = std::char_traits<char>::length(suffix);
        return value.size() >= suffixLen && value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
    }

    std::string normalizeHostedVirtualPath(const std::string& path) {
        std::string normalized = path.empty() ? "/" : path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        std::filesystem::path virtualPath(normalized);
        if (virtualPath.is_relative()) virtualPath = std::filesystem::path("/") / virtualPath;
        std::string out = virtualPath.lexically_normal().generic_string();
        if (out.empty()) out = "/";
        if (out.front() != '/') out.insert(out.begin(), '/');
        return out;
    }

    std::filesystem::path hostedRootPath() {
        return std::filesystem::current_path();
    }

    std::filesystem::path hostedPathForVirtual(const std::string& path) {
        std::string normalized = normalizeHostedVirtualPath(path);
        if (normalized == "/") return hostedRootPath();
        return hostedRootPath() / std::filesystem::path(normalized.substr(1));
    }

    std::string readOriginalPath(const std::filesystem::path& infoPath) {
        std::ifstream stream(infoPath);
        if (!stream) return std::string();
        std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        const std::string key = "\"originalPath\": \"";
        size_t start = content.find(key);
        if (start == std::string::npos) return std::string();
        start += key.size();
        size_t end = content.find('"', start);
        if (end == std::string::npos) return std::string();
        return content.substr(start, end - start);
    }

    std::string readJsonStringValue(const std::filesystem::path& infoPath, const std::string& keyName) {
        std::ifstream stream(infoPath);
        if (!stream) return std::string();
        std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        const std::string key = "\"" + keyName + "\": \"";
        size_t start = content.find(key);
        if (start == std::string::npos) return std::string();
        start += key.size();
        size_t end = content.find('"', start);
        if (end == std::string::npos) return std::string();
        return content.substr(start, end - start);
    }

    long long readJsonIntValue(const std::filesystem::path& infoPath, const std::string& keyName) {
        std::ifstream stream(infoPath);
        if (!stream) return 0;
        std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        const std::string key = "\"" + keyName + "\": ";
        size_t start = content.find(key);
        if (start == std::string::npos) return 0;
        start += key.size();
        size_t end = start;
        while (end < content.size() && content[end] >= '0' && content[end] <= '9') ++end;
        if (end == start) return 0;
        try { return std::stoll(content.substr(start, end - start)); } catch (...) { return 0; }
    }

    std::string parentVirtualPath(const std::string& path) {
        std::string normalized = normalizeHostedVirtualPath(path);
        if (normalized == "/") return "/";
        size_t slash = normalized.find_last_of('/');
        if (slash == std::string::npos || slash == 0) return "/";
        return normalized.substr(0, slash);
    }

    std::string basenameVirtualPath(const std::string& path) {
        std::string normalized = normalizeHostedVirtualPath(path);
        if (normalized == "/") return "/";
        size_t slash = normalized.find_last_of('/');
        return slash == std::string::npos ? normalized : normalized.substr(slash + 1);
    }

    std::string formatSize(uint64_t bytes) {
        if (bytes < 1024) return std::to_string(bytes) + " B";
        if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
        return std::to_string(bytes / (1024 * 1024)) + " MB";
    }

    std::string typeForName(const std::string& name, bool directory) {
        if (directory) return "File folder";
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (endsWithText(lower, ".txt") || endsWithText(lower, ".log") || endsWithText(lower, ".cfg") || endsWithText(lower, ".ini") || endsWithText(lower, ".md")) return "Text document";
        if (endsWithText(lower, ".bmp") || endsWithText(lower, ".png") || endsWithText(lower, ".jpg") || endsWithText(lower, ".jpeg") || endsWithText(lower, ".gif")) return "Image";
        if (endsWithText(lower, ".elf") || endsWithText(lower, ".gxapp") || endsWithText(lower, ".gxq") || endsWithText(lower, ".exe")) return "Application";
        if (endsWithText(lower, ".bin") || endsWithText(lower, ".dat") || endsWithText(lower, ".dll") || endsWithText(lower, ".so") || endsWithText(lower, ".o")) return "Binary file";
        return "File";
    }

    std::string iconKeyForName(const std::string& name, bool directory) {
        if (directory) return "file.folder";
        std::string type = typeForName(name, false);
        if (type == "Text document") return "file.text";
        if (type == "Image") return "file.image";
        if (type == "Application") return "app.generic";
        if (type == "Binary file") return "file.binary";
        return "file.unknown";
    }

    std::string formatDeletedTime(long long epoch) {
        if (epoch <= 0) return "Unknown";
        std::time_t now = std::time(nullptr);
        long long diff = static_cast<long long>(now) - epoch;
        if (diff < 60) return "Just now";
        if (diff < 3600) return std::to_string(diff / 60) + " min ago";
        if (diff < 86400) return std::to_string(diff / 3600) + " hr ago";
        return std::to_string(diff / 86400) + " day(s) ago";
    }

    std::string truncateText(const std::string& value, size_t width) {
        if (value.size() <= width) return value;
        if (width <= 3) return value.substr(0, width);
        return value.substr(0, width - 3) + "...";
    }

    struct TrashSurfaceColors {
        bool sciFi;
        uint32_t body;
        uint32_t panel;
        uint32_t row;
        uint32_t border;
        uint32_t headerBackground;
        uint32_t headerText;
        uint32_t primaryText;
        uint32_t secondaryText;
        uint32_t selection;
        uint32_t selectionText;
        uint32_t separator;
        uint32_t statusWarning;
        uint32_t propertiesPanel;
        uint32_t propertiesTitle;
        uint32_t propertiesText;
        uint32_t confirmPanel;
        uint32_t confirmHeading;
        uint32_t confirmText;
    };

    TrashSurfaceColors currentTrashSurfaceColors() {
        const DesktopTheme& theme = GetCurrentDesktopTheme();
        if (theme.id != DesktopThemeId::SciFi) {
            return {
                false,
                0xFF2C2E36u,
                0xFF1E2026u,
                0xFF1E2026u,
                0xFF3C4350u,
                0xFF1E2026u,
                0xFFBEC3CDu,
                0xFFDCE1EBu,
                0xFFA5AAB9u,
                0xFF465A87u,
                0xFFDCE1EBu,
                0xFF3C4350u,
                0xFFB8A0A0u,
                0xFF2D2D37u,
                0xFFE6EBF5u,
                0xFFC8CDD7u,
                0xFF373030u,
                0xFFF0E6E6u,
                0xFFD2CDCDu
            };
        }

        const DesktopControlTheme roles = GetDesktopControlTheme(theme);
        return {
            true,
            roles.panelBackground,
            roles.raisedPanel,
            roles.recessedField,
            roles.border,
            roles.tableHeaderBackground,
            roles.tableHeaderText,
            roles.primaryText,
            roles.secondaryText,
            DesktopSelectionColor(roles, true),
            roles.selectionText,
            roles.separator,
            roles.statusWarning,
            BlendDesktopThemeColor(roles.raisedPanel, roles.panelBackground, 18),
            roles.tableHeaderText,
            roles.primaryText,
            BlendDesktopThemeColor(roles.panelBackground, roles.statusWarning, 22),
            roles.statusWarning,
            roles.secondaryText
        };
    }

    bool trashStatusNeedsWarning(const std::string& status) {
        return status.find("failed") != std::string::npos ||
            status.find("error") != std::string::npos;
    }

    void publishGui(MsgType type, const std::string& payload) {
        ipc::Message msg;
        msg.type = static_cast<uint32_t>(type);
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(msg), false);
    }

    void drawTextAt(uint64_t windowId, int x, int y, const std::string& text, int r, int g, int b) {
        std::ostringstream payload;
        payload << windowId << "|" << x << "|" << y << "|" << text << "|" << r << "|" << g << "|" << b;
        publishGui(MsgType::MT_DrawText, payload.str());
    }

    void drawRect(uint64_t windowId, int x, int y, int w, int h, int r, int g, int b) {
        std::ostringstream payload;
        payload << windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|" << r << "|" << g << "|" << b;
        publishGui(MsgType::MT_DrawRect, payload.str());
    }

    void addButton(uint64_t windowId, int id, int x, int y, int w, int h, const std::string& text) {
        std::ostringstream payload;
        payload << windowId << "|1|" << id << "|" << x << "|" << y << "|" << w << "|" << h << "|" << text;
        publishGui(MsgType::MT_WidgetAdd, payload.str());
    }

    void drawImage(uint64_t windowId, int x, int y, const std::string& path) {
        std::ostringstream payload;
        payload << windowId << "|" << x << "|" << y << "|" << path;
        publishGui(MsgType::MT_DrawImage, payload.str());
    }

    std::string makeUniqueRestorePath(const std::string& desiredPath) {
        std::filesystem::path hostDesired = hostedPathForVirtual(desiredPath);
        std::error_code ec;
        if (!std::filesystem::exists(hostDesired, ec)) return desiredPath;
        std::string folder = parentVirtualPath(desiredPath);
        std::string name = basenameVirtualPath(desiredPath);
        bool directory = std::filesystem::is_directory(hostDesired, ec);
        for (int i = 1; i < 100; ++i) {
            std::string candidate = name + " (" + std::to_string(i) + ")";
            if (!directory && name.find('.') != std::string::npos) {
                size_t dot = name.find_last_of('.');
                candidate = name.substr(0, dot) + " (" + std::to_string(i) + ")" + name.substr(dot);
            }
            std::string candidatePath = normalizeHostedVirtualPath((folder == "/" ? "/" : folder + "/") + candidate);
            if (!std::filesystem::exists(hostedPathForVirtual(candidatePath), ec)) return candidatePath;
        }
        return desiredPath;
    }
}

uint64_t Trash::Launch(bool confirmEmpty) {
    ProcessSpec spec{"trash", Trash::main};
    spec.appId = "gxos.builtin.trash";
    std::vector<std::string> args = { "trash" };
    if (confirmEmpty) {
        args.push_back("--confirm-empty");
    }
    return ProcessTable::spawn(spec, args);
}

std::vector<Trash::TrashEntry> Trash::listEntries() {
    std::vector<TrashEntry> items;
    std::error_code error;
    std::filesystem::path trashPath = hostedPathForVirtual(kTrashRootPath);
    if (!std::filesystem::exists(trashPath, error) || error) return items;

    for (const auto& entry : std::filesystem::directory_iterator(trashPath, error)) {
        if (error) break;
        std::string name = entry.path().filename().generic_string();
        if (endsWithText(name, kTrashInfoSuffix)) continue;

        TrashEntry item;
        item.name = name;
        item.isDirectory = entry.is_directory(error);
        item.currentPath = normalizeHostedVirtualPath(kTrashRootPath + std::string("/") + name);
        item.originalPath = readJsonStringValue(entry.path().string() + kTrashInfoSuffix, "originalPath");
        if (item.originalPath.empty()) item.originalPath = "/" + name;
        item.originalFolder = parentVirtualPath(item.originalPath);
        item.type = typeForName(item.name, item.isDirectory);
        item.iconKey = iconKeyForName(item.name, item.isDirectory);
        item.size = item.isDirectory ? 0 : static_cast<uint64_t>(entry.file_size(error));
        item.deletedText = formatDeletedTime(readJsonIntValue(entry.path().string() + kTrashInfoSuffix, "trashedAt"));
        items.push_back(item);
    }

    std::sort(items.begin(), items.end(), [](const TrashEntry& a, const TrashEntry& b) {
        return a.name < b.name;
    });
    Logger::write(LogLevel::Info, std::string("Trash item count computed=") + std::to_string(items.size()));
    return items;
}

bool Trash::restoreEntry(const TrashEntry& entry, std::string& error, std::string& restoredPath) {
    std::filesystem::path trashPath = hostedPathForVirtual(entry.currentPath);
    std::string target = entry.originalPath.empty() ? normalizeHostedVirtualPath("/" + entry.name) : normalizeHostedVirtualPath(entry.originalPath);
    std::string parent = parentVirtualPath(target);
    std::error_code ec;
    std::filesystem::create_directories(hostedPathForVirtual(parent), ec);
    if (ec) {
        error = "Unable to create restore target folder: " + ec.message();
        return false;
    }
    restoredPath = makeUniqueRestorePath(target);
    std::filesystem::rename(trashPath, hostedPathForVirtual(restoredPath), ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    std::filesystem::remove(hostedPathForVirtual(entry.currentPath + kTrashInfoSuffix), ec);
    Logger::write(LogLevel::Info, std::string("Trash restored ") + entry.currentPath + " -> " + restoredPath);
    gxos::gui::Compositor::requestDesktopRefresh();
    return true;
}

bool Trash::deleteEntryPermanently(const TrashEntry& entry, std::string& error) {
    std::filesystem::path trashPath = hostedPathForVirtual(entry.currentPath);
    std::filesystem::path trashRoot = hostedPathForVirtual(kTrashRootPath);
    std::error_code ec;
    std::filesystem::path canonicalTrash = std::filesystem::weakly_canonical(trashRoot, ec);
    if (ec) canonicalTrash = trashRoot.lexically_normal();
    std::filesystem::path canonicalItem = std::filesystem::weakly_canonical(trashPath, ec);
    if (ec) canonicalItem = trashPath.lexically_normal();
    std::string trashText = canonicalTrash.generic_string();
    std::string itemText = canonicalItem.generic_string();
    if (itemText.size() <= trashText.size() || itemText.compare(0, trashText.size(), trashText) != 0) {
        error = "Refusing to delete outside Trash";
        return false;
    }
    std::filesystem::remove_all(trashPath, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    std::filesystem::remove(hostedPathForVirtual(entry.currentPath + kTrashInfoSuffix), ec);
    Logger::write(LogLevel::Info, std::string("Trash permanently deleted ") + entry.currentPath);
    gxos::gui::Compositor::requestDesktopRefresh();
    return true;
}

bool Trash::purgeContents(std::string& error, size_t& deletedCount) {
    deletedCount = 0;
    std::error_code ec;
    std::filesystem::path trashPath = hostedPathForVirtual(kTrashRootPath);
    std::filesystem::path root = hostedRootPath();

    if (!std::filesystem::exists(trashPath, ec)) {
        if (!std::filesystem::create_directories(trashPath, ec) || ec) {
            error = ec ? ec.message() : "Unable to create Trash directory";
            return false;
        }
        return true;
    }
    if (!std::filesystem::is_directory(trashPath, ec) || ec) {
        error = "Trash path is not a directory";
        return false;
    }

    std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec) canonicalRoot = root.lexically_normal();
    std::filesystem::path canonicalTrash = std::filesystem::weakly_canonical(trashPath, ec);
    if (ec) canonicalTrash = trashPath.lexically_normal();
    if (canonicalTrash == canonicalRoot || canonicalTrash.empty()) {
        error = "Refusing to purge unsafe Trash path";
        return false;
    }

    Logger::write(LogLevel::Info, "Empty Trash confirmed; purging hosted Trash contents");
    bool ok = true;
    for (const auto& entry : std::filesystem::directory_iterator(trashPath, ec)) {
        if (ec) { ok = false; break; }
        std::filesystem::path child = entry.path();
        std::filesystem::path canonicalChild = std::filesystem::weakly_canonical(child, ec);
        if (ec) canonicalChild = child.lexically_normal();
        std::string childText = canonicalChild.generic_string();
        std::string trashText = canonicalTrash.generic_string();
        if (childText.size() <= trashText.size() || childText.compare(0, trashText.size(), trashText) != 0) {
            Logger::write(LogLevel::Error, std::string("Refusing to delete path outside Trash: ") + childText);
            ok = false;
            continue;
        }

        std::string name = child.filename().generic_string();
        bool realItem = !endsWithText(name, kTrashInfoSuffix);
        std::uintmax_t removed = std::filesystem::remove_all(child, ec);
        if (ec) {
            Logger::write(LogLevel::Error, std::string("Empty Trash failed deleting ") + child.generic_string() + ": " + ec.message());
            ok = false;
            ec.clear();
        } else {
            if (realItem && removed > 0) ++deletedCount;
            Logger::write(LogLevel::Info, std::string("Empty Trash deleted ") + child.generic_string());
        }
    }

    std::filesystem::create_directories(trashPath, ec);
    if (ec) {
        error = ec.message();
        ok = false;
    }
    if (!ok && error.empty()) error = "Some Trash items could not be deleted";
    Logger::write(ok ? LogLevel::Info : LogLevel::Error,
        std::string("Empty Trash purge ") + (ok ? "succeeded" : "failed") + "; deleted=" + std::to_string(deletedCount));
    return ok;
}

void Trash::render(uint64_t windowId, bool confirmEmpty, bool showProperties, int selectedIndex, const std::string& status) {
    const TrashSurfaceColors colors = currentTrashSurfaceColors();
    publishGui(MsgType::MT_DrawText, std::to_string(windowId) + "|\f");
    drawRect(windowId, 0, 0, 420, 240,
        static_cast<int>((colors.body >> 16) & 0xFF),
        static_cast<int>((colors.body >> 8) & 0xFF),
        static_cast<int>(colors.body & 0xFF));
    drawRect(windowId, 16, 18, 388, 196,
        static_cast<int>((colors.panel >> 16) & 0xFF),
        static_cast<int>((colors.panel >> 8) & 0xFF),
        static_cast<int>(colors.panel & 0xFF));

    auto drawSurfaceRect = [&](int x, int y, int w, int h, uint32_t color) {
        drawRect(windowId, x, y, w, h,
            static_cast<int>((color >> 16) & 0xFF),
            static_cast<int>((color >> 8) & 0xFF),
            static_cast<int>(color & 0xFF));
    };
    auto drawSurfaceText = [&](int x, int y, const std::string& text, uint32_t color) {
        drawTextAt(windowId, x, y, text,
            static_cast<int>((color >> 16) & 0xFF),
            static_cast<int>((color >> 8) & 0xFF),
            static_cast<int>(color & 0xFF));
    };

    if (colors.sciFi) {
        drawSurfaceRect(16, 18, 388, 1, colors.border);
        drawSurfaceRect(16, 213, 388, 1, colors.border);
        drawSurfaceRect(16, 18, 1, 196, colors.border);
        drawSurfaceRect(403, 18, 1, 196, colors.border);
    }

    std::vector<TrashEntry> items = listEntries();
    Logger::write(LogLevel::Info, std::string("Trash window render; item count=") + std::to_string(items.size()));

    addButton(windowId, 210, 18, 6, 66, 20, "Restore");
    addButton(windowId, 211, 88, 6, 78, 20, "Restore All");
    addButton(windowId, 212, 170, 6, 106, 20, "Delete Perm");
    addButton(windowId, 200, 280, 6, 82, 20, "Empty");
    addButton(windowId, 213, 366, 6, 50, 20, "Refresh");

    if (items.empty()) {
        drawSurfaceText(26, 34, "Trash is empty.", colors.primaryText);
        const std::string emptyStatus = status.empty() ? "Deleted files will appear here." : status;
        drawSurfaceText(26, 58, emptyStatus,
            colors.sciFi && trashStatusNeedsWarning(emptyStatus) ? colors.statusWarning : colors.secondaryText);
    } else {
        std::ostringstream summary;
        summary << "Trash contains " << items.size() << " item(s).";
        drawSurfaceText(26, 32, summary.str(), colors.primaryText);
        if (colors.sciFi) {
            drawSurfaceRect(22, 48, 374, 20, colors.headerBackground);
            drawSurfaceRect(22, 68, 374, 1, colors.separator);
        }
        drawSurfaceText(46, 52, "Name", colors.headerText);
        drawSurfaceText(162, 52, "Original", colors.headerText);
        drawSurfaceText(248, 52, "Size", colors.headerText);
        drawSurfaceText(300, 52, "Type", colors.headerText);
        drawSurfaceText(360, 52, "Deleted", colors.headerText);
        int y = 70;
        for (size_t i = 0; i < items.size() && i < 6; ++i) {
            bool selected = static_cast<int>(i) == selectedIndex;
            if (colors.sciFi) {
                drawSurfaceRect(22, y - 2, 374, 18,
                    selected ? colors.selection : colors.row);
                if (i + 1 < items.size() && i < 5) {
                    drawSurfaceRect(22, y + 17, 374, 1, colors.separator);
                }
            } else if (selected) {
                drawRect(windowId, 22, y - 2, 374, 18, 70, 90, 135);
            }
            std::string iconPath = gxos::gui::IconThemeManager::Instance().ResolveIconPath(items[i].iconKey, 16);
            if (!iconPath.empty()) drawImage(windowId, 26, y - 2, iconPath);
            const uint32_t rowPrimary = selected && colors.sciFi ? colors.selectionText : colors.primaryText;
            const uint32_t rowSecondary = selected && colors.sciFi ? colors.selectionText : colors.secondaryText;
            drawSurfaceText(46, y + 2, truncateText(items[i].name, 16), rowPrimary);
            drawSurfaceText(162, y + 2, truncateText(items[i].originalFolder, 12), rowSecondary);
            drawSurfaceText(248, y + 2, items[i].isDirectory ? "Folder" : formatSize(items[i].size), rowSecondary);
            drawSurfaceText(300, y + 2, truncateText(items[i].type, 9), rowSecondary);
            drawSurfaceText(360, y + 2, truncateText(items[i].deletedText, 8), rowSecondary);
            addButton(windowId, 3000 + static_cast<int>(i), 22, y - 2, 374, 18, "");
            y += 20;
        }
    }

    if (!status.empty() && !items.empty()) {
        drawSurfaceText(26, 182, status,
            colors.sciFi && trashStatusNeedsWarning(status) ? colors.statusWarning : colors.secondaryText);
    }
    if (showProperties && selectedIndex >= 0 && selectedIndex < static_cast<int>(items.size())) {
        const TrashEntry& item = items[selectedIndex];
        drawSurfaceRect(54, 52, 312, 138, colors.propertiesPanel);
        if (colors.sciFi) {
            drawSurfaceRect(54, 52, 312, 1, colors.border);
            drawSurfaceRect(54, 189, 312, 1, colors.border);
            drawSurfaceRect(54, 52, 1, 138, colors.border);
            drawSurfaceRect(365, 52, 1, 138, colors.border);
        }
        drawSurfaceText(74, 70, "Properties", colors.propertiesTitle);
        drawSurfaceText(74, 94, "Name: " + item.name, colors.propertiesText);
        drawSurfaceText(74, 112, "Type: " + item.type, colors.propertiesText);
        drawSurfaceText(74, 130, "Size: " + (item.isDirectory ? std::string("Folder") : formatSize(item.size)), colors.propertiesText);
        drawSurfaceText(74, 148, "Original: " + truncateText(item.originalPath, 34), colors.propertiesText);
        drawSurfaceText(74, 166, "Trash: " + item.currentPath, colors.propertiesText);
        addButton(windowId, 214, 270, 164, 60, 22, "Close");
    }
    if (confirmEmpty) {
        drawSurfaceRect(64, 70, 292, 104, colors.confirmPanel);
        if (colors.sciFi) {
            drawSurfaceRect(64, 70, 292, 1, colors.statusWarning);
            drawSurfaceRect(64, 173, 292, 1, colors.statusWarning);
            drawSurfaceRect(64, 70, 1, 104, colors.statusWarning);
            drawSurfaceRect(355, 70, 1, 104, colors.statusWarning);
        }
        drawSurfaceText(84, 88, "Empty Trash?", colors.confirmHeading);
        drawSurfaceText(84, 112, "This will permanently delete all", colors.confirmText);
        drawSurfaceText(84, 130, "items in Trash.", colors.confirmText);
        addButton(windowId, 201, 92, 146, 100, 22, "Empty Trash");
        addButton(windowId, 202, 214, 146, 70, 22, "Cancel");
    }

    gxos::gui::Compositor::requestDesktopRefresh();
}

int Trash::main(int argc, char** argv) {
    try {
        Logger::write(LogLevel::Info, "Trash starting...");
        bool confirmEmptyRequested = false;
        for (int i = 1; i < argc && argv; ++i) {
            if (argv[i] && std::string(argv[i]) == "--confirm-empty") {
                confirmEmptyRequested = true;
                break;
            }
        }

        const char* kGuiChanIn = "gui.input";
        const char* kGuiChanOut = "gui.output";
        ipc::Bus::ensure(kGuiChanIn);
        ipc::Bus::ensure(kGuiChanOut);

        ipc::Message createMsg;
        createMsg.type = static_cast<uint32_t>(MsgType::MT_Create);
        std::string createPayload = "Trash|420|240";
        createMsg.data.assign(createPayload.begin(), createPayload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(createMsg), false);

        uint64_t windowId = 0;
        bool confirmEmpty = false;
        int selectedIndex = -1;
        bool showProperties = false;
        std::string status;
        bool running = true;
        while (running) {
            ipc::Message msg;
            if (!ipc::Bus::pop(kGuiChanOut, msg, 100)) continue;

            MsgType msgType = static_cast<MsgType>(msg.type);
            std::string payload(msg.data.begin(), msg.data.end());
            if (msgType == MsgType::MT_Create) {
                size_t sep = payload.find('|');
                if (sep == std::string::npos || sep == 0) continue;
                try {
                    windowId = std::stoull(payload.substr(0, sep));
                }
                catch (...) {
                    continue;
                }

                if (confirmEmptyRequested && listEntries().empty()) {
                    confirmEmpty = false;
                    status = "Trash is already empty.";
                } else {
                    confirmEmpty = confirmEmptyRequested;
                    if (confirmEmpty) status.clear();
                }
                render(windowId, confirmEmpty, showProperties, selectedIndex, status);
            }
            else if (msgType == MsgType::MT_WidgetEvt) {
                std::istringstream iss(payload);
                std::string winIdStr, widgetIdStr, event;
                std::getline(iss, winIdStr, '|');
                std::getline(iss, widgetIdStr, '|');
                std::getline(iss, event, '|');
                uint64_t eventWindowId = 0;
                try { eventWindowId = std::stoull(winIdStr); } catch (...) { eventWindowId = 0; }
                if (eventWindowId != windowId || event != "click") continue;
                int widgetId = 0;
                try { widgetId = std::stoi(widgetIdStr); } catch (...) { widgetId = 0; }
                if (widgetId == 200) {
                    if (listEntries().empty()) {
                        status = "Trash is already empty.";
                        Logger::write(LogLevel::Info, "Empty Trash requested while already empty");
                    } else {
                        status.clear();
                        confirmEmpty = true;
                        Logger::write(LogLevel::Info, "Empty Trash requested; showing confirmation");
                    }
                    render(windowId, confirmEmpty, showProperties, selectedIndex, status);
                } else if (widgetId == 201) {
                    std::string error;
                    size_t deleted = 0;
                    Logger::write(LogLevel::Info, "Empty Trash confirmed");
                    confirmEmpty = false;
                    if (purgeContents(error, deleted)) {
                        status = "Trash emptied.";
                    } else {
                        status = "Empty Trash failed: " + error;
                    }
                    selectedIndex = -1;
                    render(windowId, confirmEmpty, showProperties, selectedIndex, status);
                } else if (widgetId == 202) {
                    Logger::write(LogLevel::Info, "Empty Trash canceled");
                    confirmEmpty = false;
                    status = "Empty Trash canceled.";
                    render(windowId, confirmEmpty, showProperties, selectedIndex, status);
                } else if (widgetId >= 3000 && widgetId < 3010) {
                    selectedIndex = widgetId - 3000;
                    showProperties = false;
                    render(windowId, confirmEmpty, showProperties, selectedIndex, status);
                } else if (widgetId == 210) {
                    std::vector<TrashEntry> items = listEntries();
                    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(items.size())) {
                        std::string error;
                        std::string restored;
                        if (restoreEntry(items[selectedIndex], error, restored)) status = "Restored to " + restored;
                        else status = "Restore failed: " + error;
                        selectedIndex = -1;
                    } else status = "Select an item to restore.";
                    render(windowId, confirmEmpty, showProperties, selectedIndex, status);
                } else if (widgetId == 211) {
                    std::vector<TrashEntry> items = listEntries();
                    int restored = 0, failed = 0;
                    for (const auto& item : items) {
                        std::string error;
                        std::string restoredPath;
                        if (restoreEntry(item, error, restoredPath)) ++restored;
                        else ++failed;
                    }
                    status = "Restored: " + std::to_string(restored) + " Failed: " + std::to_string(failed);
                    selectedIndex = -1;
                    render(windowId, confirmEmpty, showProperties, selectedIndex, status);
                } else if (widgetId == 212) {
                    std::vector<TrashEntry> items = listEntries();
                    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(items.size())) {
                        std::string error;
                        if (deleteEntryPermanently(items[selectedIndex], error)) status = "Deleted permanently.";
                        else status = "Delete failed: " + error;
                        selectedIndex = -1;
                    } else status = "Select an item to delete.";
                    render(windowId, confirmEmpty, showProperties, selectedIndex, status);
                } else if (widgetId == 213) {
                    status = "Refreshed.";
                    render(windowId, confirmEmpty, showProperties, selectedIndex, status);
                } else if (widgetId == 214) {
                    showProperties = false;
                    render(windowId, confirmEmpty, showProperties, selectedIndex, status);
                }
            }
            else if (msgType == MsgType::MT_InputKey) {
                size_t sep = payload.find('|');
                if (sep == std::string::npos) continue;
                int keyCode = 0;
                try { keyCode = std::stoi(payload.substr(0, sep)); } catch (...) { keyCode = 0; }
                std::string action = payload.substr(sep + 1);
                if (action != "down") continue;
                std::vector<TrashEntry> items = listEntries();
                if (keyCode == 38 && selectedIndex > 0) --selectedIndex;
                else if (keyCode == 40 && selectedIndex < static_cast<int>(items.size()) - 1) ++selectedIndex;
                else if (keyCode == 13 && selectedIndex >= 0 && selectedIndex < static_cast<int>(items.size())) {
                    std::string error;
                    std::string restored;
                    if (restoreEntry(items[selectedIndex], error, restored)) status = "Restored to " + restored;
                    else status = "Restore failed: " + error;
                    selectedIndex = -1;
                } else if (keyCode == 46 && selectedIndex >= 0 && selectedIndex < static_cast<int>(items.size())) {
                    std::string error;
                    if (deleteEntryPermanently(items[selectedIndex], error)) status = "Deleted permanently.";
                    else status = "Delete failed: " + error;
                    selectedIndex = -1;
                } else if (keyCode == 116) {
                    status = "Refreshed.";
                }
                render(windowId, confirmEmpty, showProperties, selectedIndex, status);
            }
            else if (msgType == MsgType::MT_Close) {
                running = false;
            }
        }

        Logger::write(LogLevel::Info, "Trash terminated");
        return 0;
    }
    catch (const std::exception& e) {
        Logger::write(LogLevel::Error, std::string("Trash exception: ") + e.what());
        return 1;
    }
}

} // namespace apps
} // namespace gxos
