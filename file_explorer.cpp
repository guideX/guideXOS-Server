#include "file_explorer.h"
#include "desktop_theme.h"
#include "file_icon_provider.h"
#include "icon_theme_manager.h"
#include "desktop_service.h"
#include "logger.h"
#include "compositor.h"
#include "kernel/core/include/kernel/system_font.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#include "kernel/core/include/kernel/vfs.h"
#endif

namespace gxos { namespace apps {

    using namespace gxos::gui;

    namespace {
        constexpr int kWindowW = 900;
        constexpr int kWindowH = 560;
        constexpr int kLeftPaneW = 210;
        constexpr int kToolbarH = 34;
        constexpr int kAddressH = 30;
        constexpr int kHeaderH = 24;
        constexpr int kRowH = 22;
        constexpr size_t kLazyPageSize = 256;
        constexpr const char* kTrashRootPath = "/Trash";
        constexpr const char* kTrashInfoSuffix = ".trashinfo";

        constexpr int kIconSize = 16;
        constexpr int kNavIconSize = 16;
        constexpr int kToolbarIconSize = 16;
        constexpr int kPaneTextColorPad = 0;
        constexpr int kToolbarButtonY = 5;
        constexpr int kToolbarButtonH = 22;
        constexpr int kHeaderTextY = kToolbarH + kAddressH + 3;
        constexpr int kNavStartY = kToolbarH + kAddressH + 8;
        constexpr int kMainRowsStartY = kToolbarH + kAddressH + kHeaderH + 6;
        constexpr int kStatusBarY = kWindowH - 24;
        constexpr int kNavTextX = 26;
        constexpr int kNavIconX = 8;
        constexpr int kMainIconX = kLeftPaneW + 8;
        constexpr int kMainNameTextX = kMainIconX + kIconSize + 6;
        constexpr int kMainSizeTextX = kLeftPaneW + 350;
        constexpr int kMainTypeTextX = kLeftPaneW + 440;
        constexpr int kMainModifiedTextX = kLeftPaneW + 570;
        constexpr int kFileListScrollbarW = 8;
        constexpr int kFileListScrollbarPad = 2;
        constexpr int kFileListScrollbarMinThumbH = 18;
        constexpr int kWheelScrollRowsPerNotch = 3;
        constexpr int kContextMenuW = 170;
        constexpr int kContextMenuItemH = 24;
        constexpr uint64_t kDoubleClickThresholdMs = 450;

        uint64_t s_lastEntryClickTick = 0;
        int s_lastEntryClickRow = -1;

        enum class ContextMenuAction {
            Open = 0,
            PinToDesktop = 1,
            ShowOnDesktop = 2,
            Rename = 3,
            MoveToTrash = 4
        };

        static const char* contextMenuLabel(ContextMenuAction action) {
            switch (action) {
                case ContextMenuAction::Open: return "Open";
                case ContextMenuAction::PinToDesktop: return "Pin to Desktop";
                case ContextMenuAction::ShowOnDesktop: return "Show on Desktop";
                case ContextMenuAction::Rename: return "Rename";
                case ContextMenuAction::MoveToTrash: return "Move to Trash";
                default: return "";
            }
        }

        int uiLineHeight() {
            return SystemFont::MeasureHeight(FontRole::Default);
        }

        int centeredTextY(int top, int height) {
            const int lineH = uiLineHeight();
            return top + (height > lineH ? (height - lineH) / 2 : 0);
        }

        int rowTextY(int rowY) {
            return centeredTextY(rowY, kRowH);
        }

        int rowIconY(int rowY, int iconSize) {
            return rowY + (kRowH > iconSize ? (kRowH - iconSize) / 2 : 0);
        }

        int fileListViewportHeight() {
            return kStatusBarY - kMainRowsStartY;
        }

        uint32_t packRgb(int r, int g, int b) {
            return 0xFF000000u |
                (static_cast<uint32_t>(r & 0xFF) << 16) |
                (static_cast<uint32_t>(g & 0xFF) << 8) |
                static_cast<uint32_t>(b & 0xFF);
        }

        uint32_t blendColor(uint32_t baseColor, uint32_t overlayColor, int overlayPercent) {
            if (overlayPercent <= 0) return baseColor;
            if (overlayPercent >= 100) return overlayColor;

            const int baseR = static_cast<int>((baseColor >> 16) & 0xFF);
            const int baseG = static_cast<int>((baseColor >> 8) & 0xFF);
            const int baseB = static_cast<int>(baseColor & 0xFF);
            const int overR = static_cast<int>((overlayColor >> 16) & 0xFF);
            const int overG = static_cast<int>((overlayColor >> 8) & 0xFF);
            const int overB = static_cast<int>(overlayColor & 0xFF);
            const int keepPercent = 100 - overlayPercent;

            return packRgb(
                (baseR * keepPercent + overR * overlayPercent) / 100,
                (baseG * keepPercent + overG * overlayPercent) / 100,
                (baseB * keepPercent + overB * overlayPercent) / 100);
        }

        bool isSciFiThemeActive() {
            return GetCurrentDesktopThemeId() == DesktopThemeId::SciFi;
        }

        const DesktopTheme& fileExplorerTheme() {
            return GetCurrentDesktopTheme();
        }

        uint32_t FileExplorerBodyColor() {
            if (!isSciFiThemeActive()) return packRgb(244, 244, 244);
            const DesktopTheme& theme = fileExplorerTheme();
            return blendColor(theme.taskbarBackground, theme.windowBackground, 18);
        }

        uint32_t FileExplorerToolbarColor() {
            if (!isSciFiThemeActive()) return packRgb(240, 240, 240);
            const DesktopTheme& theme = fileExplorerTheme();
            return blendColor(theme.titleBarBackground, theme.windowBackground, 14);
        }

        uint32_t FileExplorerPathBoxColor() {
            if (!isSciFiThemeActive()) return packRgb(252, 252, 252);
            const DesktopTheme& theme = fileExplorerTheme();
            return blendColor(theme.windowBackground, theme.taskbarBackground, 10);
        }

        uint32_t FileExplorerPanelColor() {
            if (!isSciFiThemeActive()) return packRgb(248, 248, 248);
            const DesktopTheme& theme = fileExplorerTheme();
            return blendColor(theme.windowBackground, theme.taskbarBackground, 16);
        }

        uint32_t FileExplorerListColor() {
            if (!isSciFiThemeActive()) return packRgb(250, 250, 250);
            const DesktopTheme& theme = fileExplorerTheme();
            return blendColor(theme.windowBackground, theme.taskbarBackground, 12);
        }

        uint32_t FileExplorerHeaderColor() {
            if (!isSciFiThemeActive()) return packRgb(235, 235, 235);
            const DesktopTheme& theme = fileExplorerTheme();
            return blendColor(theme.windowBackground, theme.taskbarBackground, 18);
        }

        uint32_t FileExplorerStatusColor() {
            if (!isSciFiThemeActive()) return packRgb(238, 238, 238);
            const DesktopTheme& theme = fileExplorerTheme();
            return blendColor(theme.taskbarBackground, theme.windowBackground, 8);
        }

        uint32_t FileExplorerBorderColor() {
            if (!isSciFiThemeActive()) return packRgb(208, 208, 208);
            const DesktopTheme& theme = fileExplorerTheme();
            return blendColor(theme.windowBorder, theme.taskbarBorder, 24);
        }

        uint32_t FileExplorerTextColor() {
            if (!isSciFiThemeActive()) return packRgb(220, 220, 220);
            return fileExplorerTheme().titleBarText;
        }

        uint32_t FileExplorerMutedTextColor() {
            if (!isSciFiThemeActive()) return packRgb(182, 186, 194);
            const DesktopTheme& theme = fileExplorerTheme();
            return blendColor(theme.titleBarText, theme.taskbarBackground, 54);
        }

        uint32_t FileExplorerAccentColor() {
            if (!isSciFiThemeActive()) return packRgb(92, 136, 198);
            return fileExplorerTheme().accent;
        }

        uint32_t FileExplorerSelectedRowColor() {
            if (!isSciFiThemeActive()) return packRgb(80, 100, 150);
            const DesktopTheme& theme = fileExplorerTheme();
            return blendColor(theme.windowBackground, theme.accent, 20);
        }

        uint32_t FileExplorerHoveredRowColor() {
            if (!isSciFiThemeActive()) return packRgb(232, 232, 238);
            const DesktopTheme& theme = fileExplorerTheme();
            return blendColor(theme.windowBackground, theme.mutedAccent, 12);
        }

        uint32_t FileExplorerContextMenuColor() {
            if (!isSciFiThemeActive()) return packRgb(245, 245, 248);
            const DesktopTheme& theme = fileExplorerTheme();
            return blendColor(theme.windowBackground, theme.taskbarBackground, 12);
        }

        uint32_t FileExplorerContextMenuHoverColor() {
            if (!isSciFiThemeActive()) return packRgb(65, 105, 170);
            const DesktopTheme& theme = fileExplorerTheme();
            return blendColor(theme.accent, theme.windowBackground, 22);
        }

        enum PromptMode {
            PromptNone = 0,
            PromptAddress = 1,
            PromptNewFolder = 2,
            PromptNewFile = 3,
            PromptRename = 4
        };

        class KernelVfsExplorerFileSystem final : public IExplorerFileSystem {
        public:
            std::vector<ExplorerFileEntry> getRoots() override {
                std::vector<ExplorerFileEntry> roots;

#ifndef _WIN32
                uint8_t count = kernel::vfs::mount_count();
                for (uint8_t i = 0; i < kernel::vfs::VFS_MAX_MOUNTS; ++i) {
                    const kernel::vfs::MountPoint* mp = kernel::vfs::get_mount_by_index(i);
                    if (!mp || !mp->active) continue;

                    ExplorerFileEntry drive;
                    drive.name = std::string(mp->path) + " (" + kernel::vfs::fs_type_name(mp->fsType) + ")";
                    drive.fullPath = mp->path;
                    drive.kind = ExplorerEntryKind::Drive;
                    drive.type = "Mounted drive";
                    drive.modified = "--";
                    roots.push_back(drive);
                }

                if (count == 0) {
                    ExplorerFileEntry root;
                    root.name = "Root (no mounted drives)";
                    root.fullPath = "/";
                    root.kind = ExplorerEntryKind::Drive;
                    root.type = "Root";
                    root.modified = "--";
                    roots.push_back(root);
                }
#else
                ExplorerFileEntry root;
                root.name = "Root";
                root.fullPath = "/";
                root.kind = ExplorerEntryKind::Drive;
                root.type = "Host test root";
                root.modified = "--";
                roots.push_back(root);
#endif

                ExplorerFileEntry common;
                common.name = "Common folders";
                common.fullPath = "/";
                common.kind = ExplorerEntryKind::CommonFolder;
                common.type = "Shortcut group";
                common.modified = "--";
                roots.push_back(common);
                return roots;
            }

            std::vector<ExplorerFileEntry> listDirectory(const std::string& path, size_t offset, size_t limit, bool& hasMore) override {
                hasMore = false;
                std::vector<ExplorerFileEntry> entries;

#ifndef _WIN32
                uint8_t dir = kernel::vfs::opendir(path.c_str());
                if (dir == 0xFF) return entries;

                kernel::vfs::DirEntry item{};
                size_t index = 0;
                while (kernel::vfs::readdir(dir, &item)) {
                    if (index++ < offset) continue;
                    if (entries.size() >= limit) {
                        hasMore = true;
                        break;
                    }

                    ExplorerFileEntry entry;
                    entry.name = item.name;
                    entry.fullPath = combinePath(path, item.name);
                    entry.size = item.size;
                    entry.kind = item.type == kernel::vfs::FILE_TYPE_DIRECTORY ? ExplorerEntryKind::Directory : ExplorerEntryKind::File;
                    entry.type = entry.isDirectory() ? "File folder" : fileTypeFromName(entry.name);
                    entry.modified = "--";
                    entries.push_back(entry);
                }

                kernel::vfs::closedir(dir);
#else
                std::error_code ec;
                std::filesystem::path hostPath = hostPathForVirtual(path);
                if (!std::filesystem::exists(hostPath, ec) || !std::filesystem::is_directory(hostPath, ec)) return entries;
                size_t index = 0;
                for (const auto& item : std::filesystem::directory_iterator(hostPath, ec)) {
                    if (ec) break;
                    if (index++ < offset) continue;
                    if (entries.size() >= limit) {
                        hasMore = true;
                        break;
                    }
                    ExplorerFileEntry entry;
                    entry.name = item.path().filename().generic_string();
                    entry.fullPath = combinePath(normalizePath(path), entry.name);
                    entry.kind = item.is_directory(ec) ? ExplorerEntryKind::Directory : ExplorerEntryKind::File;
                    entry.type = entry.isDirectory() ? "File folder" : fileTypeFromName(entry.name);
                    entry.size = entry.isDirectory() ? 0 : static_cast<uint64_t>(item.file_size(ec));
                    entry.modified = "--";
                    entries.push_back(entry);
                }
#endif
                std::sort(entries.begin(), entries.end(), [](const ExplorerFileEntry& a, const ExplorerFileEntry& b) {
                    if (a.isDirectory() != b.isDirectory()) return a.isDirectory();
                    std::string an = a.name;
                    std::string bn = b.name;
                    std::transform(an.begin(), an.end(), an.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    std::transform(bn.begin(), bn.end(), bn.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    return an < bn;
                });
                return entries;
            }

            bool exists(const std::string& path) override {
#ifndef _WIN32
                return kernel::vfs::exists(path.c_str());
#else
                std::error_code ec;
                return std::filesystem::exists(hostPathForVirtual(path), ec) && !ec;
#endif
            }

            bool isDirectory(const std::string& path) override {
#ifndef _WIN32
                kernel::vfs::FileInfo info{};
                return kernel::vfs::stat(path.c_str(), &info) == kernel::vfs::VFS_OK && info.type == kernel::vfs::FILE_TYPE_DIRECTORY;
#else
                std::error_code ec;
                return std::filesystem::is_directory(hostPathForVirtual(path), ec) && !ec;
#endif
            }

            bool createDirectory(const std::string& path, std::string& error) override {
#ifndef _WIN32
                kernel::vfs::Status status = kernel::vfs::mkdir(path.c_str());
                if (status == kernel::vfs::VFS_OK) return true;
                error = statusText(status);
                return false;
#else
                std::error_code ec;
                if (std::filesystem::exists(hostPathForVirtual(path), ec)) return std::filesystem::is_directory(hostPathForVirtual(path), ec);
                bool created = std::filesystem::create_directories(hostPathForVirtual(path), ec);
                if (created && !ec) return true;
                error = ec ? ec.message() : "Unable to create directory";
                return false;
#endif
            }

            bool createFile(const std::string& path, std::string& error) override {
#ifndef _WIN32
                const char empty = 0;
                int32_t written = kernel::vfs::write_file(path.c_str(), &empty, 0);
                if (written >= 0) return true;
                error = "VFS write_file failed";
                return false;
#else
                std::ofstream file(hostPathForVirtual(path), std::ios::binary | std::ios::app);
                if (file.good()) return true;
                error = "Unable to create file";
                return false;
#endif
            }

            bool remove(const std::string& path, bool directory, std::string& error) override {
#ifndef _WIN32
                kernel::vfs::Status status = directory ? kernel::vfs::rmdir(path.c_str()) : kernel::vfs::unlink(path.c_str());
                if (status == kernel::vfs::VFS_OK) return true;
                error = statusText(status);
                return false;
#else
                std::error_code ec;
                if (directory) std::filesystem::remove_all(hostPathForVirtual(path), ec);
                else std::filesystem::remove(hostPathForVirtual(path), ec);
                if (!ec) return true;
                error = ec.message();
                return false;
#endif
            }

            bool rename(const std::string& oldPath, const std::string& newPath, std::string& error) override {
#ifndef _WIN32
                kernel::vfs::Status status = kernel::vfs::rename(oldPath.c_str(), newPath.c_str());
                if (status == kernel::vfs::VFS_OK) return true;
                error = statusText(status);
                return false;
#else
                std::error_code ec;
                std::filesystem::rename(hostPathForVirtual(oldPath), hostPathForVirtual(newPath), ec);
                if (!ec) return true;
                error = ec.message();
                return false;
#endif
            }

            std::string normalizePath(const std::string& path) override {
#ifndef _WIN32
                char normalized[kernel::vfs::VFS_MAX_PATH]{};
                kernel::vfs::normalize_path(path.c_str(), normalized, sizeof(normalized));
                return normalized[0] ? normalized : "/";
#else
                std::string normalized = path.empty() ? "/" : path;
                std::replace(normalized.begin(), normalized.end(), '\\', '/');
                std::filesystem::path virtualPath(normalized);
                if (virtualPath.is_relative()) virtualPath = std::filesystem::path("/") / virtualPath;
                std::string out = virtualPath.lexically_normal().generic_string();
                if (out.empty()) out = "/";
                if (out.front() != '/') out.insert(out.begin(), '/');
                return out;
#endif
            }

            std::string combinePath(const std::string& base, const std::string& name) override {
#ifndef _WIN32
                char combined[kernel::vfs::VFS_MAX_PATH]{};
                kernel::vfs::join_path(base.c_str(), name.c_str(), combined, sizeof(combined));
                return combined;
#else
                return normalizePath((base.empty() || base == "/") ? "/" + name : base + "/" + name);
#endif
            }

            std::string parentPath(const std::string& path) override {
#ifndef _WIN32
                char parent[kernel::vfs::VFS_MAX_PATH]{};
                kernel::vfs::parent_path(path.c_str(), parent, sizeof(parent));
                return parent;
#else
                if (path == "/") return "";
                size_t slash = path.find_last_of('/');
                return slash == 0 ? "/" : path.substr(0, slash);
#endif
            }

        private:
#ifdef _WIN32
            static std::filesystem::path hostRootPath() {
                return std::filesystem::current_path();
            }

            static std::filesystem::path hostPathForVirtual(const std::string& path) {
                std::string normalized = path.empty() ? "/" : path;
                std::replace(normalized.begin(), normalized.end(), '\\', '/');
                std::filesystem::path virtualPath(normalized);
                if (virtualPath.is_relative()) virtualPath = std::filesystem::path("/") / virtualPath;
                std::string generic = virtualPath.lexically_normal().generic_string();
                if (generic.empty() || generic == "/") return hostRootPath();
                if (generic.front() == '/') generic.erase(generic.begin());
                return hostRootPath() / std::filesystem::path(generic);
            }
#endif

            static std::string fileTypeFromName(const std::string& name) {
                size_t dot = name.find_last_of('.');
                if (dot == std::string::npos || dot + 1 >= name.size()) return "File";
                std::string ext = name.substr(dot + 1);
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == "txt" || ext == "log" || ext == "cfg") return "Text document";
                if (ext == "elf" || ext == "gxapp") return "Application";
                if (ext == "bmp" || ext == "png") return "Image";
                return ext + " file";
            }

#ifndef _WIN32
            static std::string statusText(kernel::vfs::Status status) {
                switch (status) {
                    case kernel::vfs::VFS_ERR_NOT_FOUND: return "Path not found";
                    case kernel::vfs::VFS_ERR_EXISTS: return "Already exists";
                    case kernel::vfs::VFS_ERR_NOT_DIR: return "Parent is not a directory";
                    case kernel::vfs::VFS_ERR_IS_DIR: return "Path is a directory";
                    case kernel::vfs::VFS_ERR_NOT_EMPTY: return "Directory is not empty";
                    case kernel::vfs::VFS_ERR_READ_ONLY: return "Filesystem is read-only";
                    case kernel::vfs::VFS_ERR_NOT_SUPPORTED: return "Operation is not supported by this filesystem";
                    default: return "Filesystem operation failed";
                }
            }
#endif
        };
    }

    uint64_t FileExplorer::s_windowId = 0;
    std::unique_ptr<IExplorerFileSystem> FileExplorer::s_fileSystem;
    std::string FileExplorer::s_currentPath = "/";
    std::vector<ExplorerFileEntry> FileExplorer::s_entries;
    std::vector<ExplorerFileEntry> FileExplorer::s_roots;
    std::vector<std::string> FileExplorer::s_backHistory;
    std::vector<std::string> FileExplorer::s_forwardHistory;
    int FileExplorer::s_selectedIndex = 0;
    int FileExplorer::s_scrollOffset = 0;
    int FileExplorer::s_hoveredIndex = -1;
    bool FileExplorer::s_draggingFileListScrollbar = false;
    int FileExplorer::s_fileListScrollbarDragStartY = 0;
    int FileExplorer::s_fileListScrollbarDragStartOffsetRows = 0;
    int FileExplorer::s_rootSelectedIndex = 0;
    int FileExplorer::s_lastKeyCode = 0;
    bool FileExplorer::s_keyDown = false;
    bool FileExplorer::s_loading = false;
    bool FileExplorer::s_hasMoreEntries = false;
    std::string FileExplorer::s_status = "Ready";
    int FileExplorer::s_promptMode = PromptNone;
    std::string FileExplorer::s_promptTitle;
    std::string FileExplorer::s_promptValue;
    bool FileExplorer::s_showDeleteConfirmation = false;
    std::string FileExplorer::s_deleteTargetPath;
    bool FileExplorer::s_deleteTargetIsDirectory = false;
    bool FileExplorer::s_contextMenuOpen = false;
    int FileExplorer::s_contextMenuX = 0;
    int FileExplorer::s_contextMenuY = 0;
    int FileExplorer::s_contextMenuHover = -1;
    std::vector<int> FileExplorer::s_contextMenuActions;

    uint64_t FileExplorer::Launch(const std::string& startPath) {
        ProcessSpec spec{"file_explorer", FileExplorer::main};
        spec.appId = "gxos.builtin.fileexplorer";
        return startPath.empty()
            ? ProcessTable::spawn(spec, {"file_explorer"})
            : ProcessTable::spawn(spec, {"file_explorer", startPath});
    }

    std::unique_ptr<IExplorerFileSystem> FileExplorer::createFileSystemProvider() {
        return std::make_unique<KernelVfsExplorerFileSystem>();
    }

    int FileExplorer::main(int argc, char** argv) {
        Logger::write(LogLevel::Info, "FileExplorer starting...");

        s_windowId = 0;
        s_fileSystem = createFileSystemProvider();
        s_roots = s_fileSystem->getRoots();
        s_currentPath = (argc > 1) ? s_fileSystem->normalizePath(argv[1]) : (s_roots.empty() ? "/" : s_fileSystem->normalizePath(s_roots[0].fullPath));
        s_entries.clear();
        s_backHistory.clear();
        s_forwardHistory.clear();
        s_selectedIndex = 0;
        s_scrollOffset = 0;
        s_hoveredIndex = -1;
        s_draggingFileListScrollbar = false;
        s_fileListScrollbarDragStartY = 0;
        s_fileListScrollbarDragStartOffsetRows = 0;
        s_rootSelectedIndex = 0;
        s_status = "Ready";
        s_promptMode = PromptNone;
        s_showDeleteConfirmation = false;
        s_contextMenuOpen = false;
        s_contextMenuHover = -1;
        s_contextMenuActions.clear();
        s_lastEntryClickTick = 0;
        s_lastEntryClickRow = -1;

        refresh();

        ipc::Bus::ensure("gui.input");
        ipc::Bus::ensure("gui.output");

        ipc::Message createMsg;
        createMsg.type = static_cast<uint32_t>(MsgType::MT_Create);
        std::ostringstream createPayload;
        createPayload << "File Explorer - " << s_currentPath << "|" << kWindowW << "|" << kWindowH;
        std::string payload = createPayload.str();
        createMsg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(createMsg), false);

        bool running = true;
        while (running) {
            ipc::Message msg;
            if (!ipc::Bus::pop("gui.output", msg, 100)) continue;

            MsgType msgType = static_cast<MsgType>(msg.type);
            std::string message(msg.data.begin(), msg.data.end());

            switch (msgType) {
                case MsgType::MT_Create: {
                    size_t sep = message.find('|');
                    if (sep == std::string::npos) break;
                    s_windowId = std::stoull(message.substr(0, sep));
                    Logger::write(LogLevel::Info, std::string("FileExplorer window created: ") + std::to_string(s_windowId));
                    updateDisplay();
                    break;
                }
                case MsgType::MT_Close: {
                    uint64_t closedId = 0;
                    try { closedId = std::stoull(message); } catch (...) {}
                    if (closedId == s_windowId) running = false;
                    break;
                }
                case MsgType::MT_InputKey: {
                    size_t sep = message.find('|');
                    if (sep == std::string::npos) break;
                    int keyCode = std::stoi(message.substr(0, sep));
                    std::string action = message.substr(sep + 1);
                    if (action == "down") {
                        if (s_keyDown && s_lastKeyCode == keyCode) break;
                        s_keyDown = true;
                        s_lastKeyCode = keyCode;
                    } else {
                        s_keyDown = false;
                        s_lastKeyCode = 0;
                    }
                    handleKeyPress(keyCode, action);
                    break;
                }
                case MsgType::MT_InputMouse: {
                    handleMouseInput(message);
                    break;
                }
                case MsgType::MT_WidgetEvt: {
                    std::istringstream iss(message);
                    std::string winIdStr, widgetIdStr, event;
                    std::getline(iss, winIdStr, '|');
                    std::getline(iss, widgetIdStr, '|');
                    std::getline(iss, event, '|');
                    uint64_t winId = std::stoull(winIdStr);
                    if (winId != s_windowId || event != "click") break;

                    switch (std::stoi(widgetIdStr)) {
                        case 1: goBack(); break;
                        case 2: goForward(); break;
                        case 3: goUp(); break;
                        case 4: refresh(); updateDisplay(); break;
                        case 5: beginPrompt(PromptAddress, "Address", s_currentPath); break;
                        case 6: createFolder(); break;
                        case 7: createFile(); break;
                        case 8: renameSelected(); break;
                        case 9: deleteSelected(); break;
                        case 10: renameSelected(); break;
                        case 11: showDeleteConfirmation(); break;
                        case 12: renameSelected(); break;
                        case 13: showDeleteConfirmation(); break;
                        case 14: refresh(); updateDisplay(); break;
                        case 100: confirmDelete(); break;
                        case 101: cancelDelete(); break;
                    }
                    break;
                }
                default:
                    break;
            }
        }

        s_fileSystem.reset();
        Logger::write(LogLevel::Info, "FileExplorer stopped");
        return 0;
    }

    void FileExplorer::navigate(const std::string& path, bool addHistory) {
        std::string normalized = s_fileSystem->normalizePath(path);
        if (!s_fileSystem->exists(normalized)) {
            s_status = "Path not found: " + normalized;
            updateDisplay();
            return;
        }
        if (!s_fileSystem->isDirectory(normalized)) {
            s_status = "Not a directory: " + normalized;
            updateDisplay();
            return;
        }

        if (addHistory && !s_currentPath.empty() && s_currentPath != normalized) {
            s_backHistory.push_back(s_currentPath);
            s_forwardHistory.clear();
        }

        s_currentPath = normalized;
        s_selectedIndex = 0;
        s_scrollOffset = 0;
        s_hoveredIndex = -1;
        s_draggingFileListScrollbar = false;
        resetFileListClickTracking();
        refresh();
        updateDisplay();
        Logger::write(LogLevel::Info, std::string("FileExplorer: Navigated to ") + s_currentPath);
    }

    void FileExplorer::goBack() {
        if (s_backHistory.empty()) return;
        std::string previous = s_backHistory.back();
        s_backHistory.pop_back();
        s_forwardHistory.push_back(s_currentPath);
        navigate(previous, false);
    }

    void FileExplorer::goForward() {
        if (s_forwardHistory.empty()) return;
        std::string next = s_forwardHistory.back();
        s_forwardHistory.pop_back();
        s_backHistory.push_back(s_currentPath);
        navigate(next, false);
    }

    void FileExplorer::goUp() {
        std::string parent = s_fileSystem->parentPath(s_currentPath);
        if (!parent.empty() && parent != s_currentPath) navigate(parent);
    }

    void FileExplorer::goHome() {
        if (!s_roots.empty()) navigate(s_roots[0].fullPath);
    }

    void FileExplorer::refresh() {
        s_loading = true;
        s_hoveredIndex = -1;
        bool hasMore = false;
        s_entries = s_fileSystem->listDirectory(s_currentPath, 0, kLazyPageSize, hasMore);
        s_hasMoreEntries = hasMore;
        s_roots = s_fileSystem->getRoots();
        s_draggingFileListScrollbar = false;

        clampFileListState();
        ensureSelectedFileVisible();
        resetFileListClickTracking();
        s_loading = false;
        s_status = s_hasMoreEntries
            ? ("Showing first " + std::to_string(kLazyPageSize) + " entries; more items available")
            : "Ready";
    }

    int FileExplorer::fileListVisibleRowCount() {
        const int visibleRows = fileListViewportHeight() / kRowH;
        return std::max(1, visibleRows);
    }

    int FileExplorer::fileListMaxScrollRows() {
        const int count = static_cast<int>(s_entries.size());
        return std::max(0, count - fileListVisibleRowCount());
    }

    bool FileExplorer::isFileListScrollbarVisible() {
        return fileListMaxScrollRows() > 0;
    }

    int FileExplorer::fileListScrollbarLeft() {
        return kWindowW - kFileListScrollbarPad - kFileListScrollbarW;
    }

    int FileExplorer::fileListScrollbarTrackTop() {
        return kMainRowsStartY + kFileListScrollbarPad;
    }

    int FileExplorer::fileListScrollbarTrackHeight() {
        return std::max(1, fileListViewportHeight() - (kFileListScrollbarPad * 2));
    }

    int FileExplorer::fileListScrollbarThumbHeight() {
        if (!isFileListScrollbarVisible()) return 0;
        const int visibleRows = fileListVisibleRowCount();
        const int totalRows = static_cast<int>(s_entries.size());
        const int trackH = fileListScrollbarTrackHeight();
        int thumbH = (trackH * visibleRows) / std::max(1, totalRows);
        if (thumbH < kFileListScrollbarMinThumbH) thumbH = kFileListScrollbarMinThumbH;
        if (thumbH > trackH) thumbH = trackH;
        return thumbH;
    }

    int FileExplorer::fileListScrollbarThumbTop() {
        if (!isFileListScrollbarVisible()) return fileListScrollbarTrackTop();
        const int maxScroll = fileListMaxScrollRows();
        const int trackTop = fileListScrollbarTrackTop();
        const int trackTravel = std::max(1, fileListScrollbarTrackHeight() - fileListScrollbarThumbHeight());
        if (maxScroll <= 0) return trackTop;
        const int offset = std::max(0, std::min(s_scrollOffset, maxScroll));
        return trackTop + (trackTravel * offset) / maxScroll;
    }

    void FileExplorer::clampFileListState() {
        const int count = static_cast<int>(s_entries.size());
        if (count <= 0) {
            s_selectedIndex = 0;
            s_scrollOffset = 0;
            return;
        }

        if (s_selectedIndex < 0) s_selectedIndex = 0;
        if (s_selectedIndex >= count) s_selectedIndex = count - 1;

        if (s_scrollOffset < 0) s_scrollOffset = 0;
        const int maxScroll = fileListMaxScrollRows();
        if (s_scrollOffset > maxScroll) s_scrollOffset = maxScroll;
    }

    void FileExplorer::ensureSelectedFileVisible() {
        if (s_entries.empty()) {
            s_scrollOffset = 0;
            return;
        }

        const int visibleRows = fileListVisibleRowCount();
        const int maxScroll = fileListMaxScrollRows();
        const int previousScrollOffset = s_scrollOffset;
        if (s_selectedIndex < s_scrollOffset) {
            s_scrollOffset = s_selectedIndex;
        } else if (s_selectedIndex >= s_scrollOffset + visibleRows) {
            s_scrollOffset = s_selectedIndex - visibleRows + 1;
        }

        if (s_scrollOffset < 0) s_scrollOffset = 0;
        if (s_scrollOffset > maxScroll) s_scrollOffset = maxScroll;
        if (s_scrollOffset != previousScrollOffset) {
            s_hoveredIndex = -1;
        }
    }

    void FileExplorer::scrollFileListByRows(int rows) {
        if (rows == 0 || s_entries.empty()) return;
        const int maxScroll = fileListMaxScrollRows();
        if (maxScroll <= 0) return;
        const int nextOffset = std::max(0, std::min(s_scrollOffset + rows, maxScroll));
        if (nextOffset == s_scrollOffset) return;
        s_scrollOffset = nextOffset;
        s_hoveredIndex = -1;
        resetFileListClickTracking();
        updateDisplay();
    }

    void FileExplorer::resetFileListClickTracking() {
        s_lastEntryClickRow = -1;
        s_lastEntryClickTick = 0;
    }

    void FileExplorer::beginPrompt(int mode, const std::string& title, const std::string& initialValue) {
        s_promptMode = mode;
        s_promptTitle = title;
        s_promptValue = initialValue;
        updateDisplay();
    }

    void FileExplorer::commitPrompt() {
        std::string value = s_promptValue;
        int mode = s_promptMode;
        s_promptMode = PromptNone;

        if (value.empty()) {
            s_status = "Operation cancelled";
            updateDisplay();
            return;
        }

        std::string error;
        if (mode == PromptAddress) {
            navigate(value);
            return;
        }
        if (mode == PromptNewFolder) {
            if (!s_fileSystem->createDirectory(s_fileSystem->combinePath(s_currentPath, value), error)) s_status = error;
        } else if (mode == PromptNewFile) {
            if (!s_fileSystem->createFile(s_fileSystem->combinePath(s_currentPath, value), error)) s_status = error;
        } else if (mode == PromptRename && s_selectedIndex >= 0 && s_selectedIndex < static_cast<int>(s_entries.size())) {
            std::string newPath = s_fileSystem->combinePath(s_currentPath, value);
            if (!s_fileSystem->rename(s_entries[s_selectedIndex].fullPath, newPath, error)) s_status = error;
        }

        refresh();
        updateDisplay();
    }

    void FileExplorer::cancelPrompt() {
        s_promptMode = PromptNone;
        s_status = "Operation cancelled";
        updateDisplay();
    }

    void FileExplorer::appendPromptChar(char ch) {
        if (s_promptValue.size() < 180) s_promptValue.push_back(ch);
        updateDisplay();
    }

    void FileExplorer::backspacePromptChar() {
        if (!s_promptValue.empty()) s_promptValue.pop_back();
        updateDisplay();
    }

    void FileExplorer::openSelected() {
        if (s_selectedIndex < 0 || s_selectedIndex >= static_cast<int>(s_entries.size())) return;
        const ExplorerFileEntry& entry = s_entries[s_selectedIndex];
        if (entry.isDirectory()) {
            navigate(entry.fullPath);
            return;
        }

        std::string error;
        if (DesktopService::OpenFilesystemEntry(entry.fullPath, false, error)) return;

        s_status = error.empty() ? ("No file association registered for " + entry.name) : error;
        updateDisplay();
    }

    void FileExplorer::deleteSelected() {
        if (s_selectedIndex < 0 || s_selectedIndex >= static_cast<int>(s_entries.size())) return;
        const ExplorerFileEntry entry = s_entries[s_selectedIndex];
        std::string error;
        std::string trashedPath;
        Logger::write(LogLevel::Info, std::string("FileExplorer delete requested path=") + entry.fullPath);
        if (!moveEntryToTrash(entry, error, trashedPath)) {
            s_status = "Move to Trash failed: " + error;
        } else {
            s_status = "Moved to Trash: " + entry.name;
        }
        refresh();
        updateDisplay();
    }

    void FileExplorer::showDeleteConfirmation() {
        if (s_selectedIndex < 0 || s_selectedIndex >= static_cast<int>(s_entries.size())) return;
        const ExplorerFileEntry& entry = s_entries[s_selectedIndex];
        s_deleteTargetPath = entry.fullPath;
        s_deleteTargetIsDirectory = entry.isDirectory();
        s_showDeleteConfirmation = true;
        updateDisplay();
    }

    void FileExplorer::confirmDelete() {
        s_showDeleteConfirmation = false;
        std::string name = s_deleteTargetPath.substr(s_deleteTargetPath.find_last_of('/') + 1);
        ExplorerFileEntry entry;
        entry.name = name;
        entry.fullPath = s_deleteTargetPath;
        entry.kind = s_deleteTargetIsDirectory ? ExplorerEntryKind::Directory : ExplorerEntryKind::File;
        entry.type = s_deleteTargetIsDirectory ? "File folder" : "File";
        std::string error;
        std::string trashedPath;
        Logger::write(LogLevel::Info, std::string("FileExplorer confirm delete path=") + s_deleteTargetPath);
        if (!moveEntryToTrash(entry, error, trashedPath)) {
            s_status = "Move to Trash failed: " + error;
        } else {
            s_status = "Moved to Trash: " + name;
        }
        refresh();
        updateDisplay();
    }

    void FileExplorer::cancelDelete() {
        s_showDeleteConfirmation = false;
        s_status = "Delete cancelled";
        updateDisplay();
    }

    void FileExplorer::pinSelectedToDesktop() {
        if (s_selectedIndex < 0 || s_selectedIndex >= static_cast<int>(s_entries.size())) return;
        const ExplorerFileEntry& entry = s_entries[s_selectedIndex];
        const std::string targetPath = s_fileSystem->normalizePath(entry.fullPath);
        const std::string iconName = FileIconProvider::logicalIconNameForEntry(entry);
        Logger::write(LogLevel::Info, "FileExplorer Pin to Desktop selected path=" + targetPath +
            " kind=" + std::string(entry.isDirectory() ? "Folder" : "File") +
            " icon=" + iconName);
        bool pinned = Compositor::pinFilesystemEntryToDesktop(targetPath, entry.isDirectory(), entry.name, iconName);
        s_status = pinned ? "Pinned to Desktop: " + entry.name : "Pin to Desktop skipped";
        updateDisplay();
    }

    void FileExplorer::showFolderOnDesktop() {
        if (s_selectedIndex < 0 || s_selectedIndex >= static_cast<int>(s_entries.size())) return;
        const ExplorerFileEntry& entry = s_entries[s_selectedIndex];
        if (!entry.isDirectory()) {
            s_status = "Show on Desktop requires a folder";
            updateDisplay();
            return;
        }

        const std::string targetPath = s_fileSystem->normalizePath(entry.fullPath);
        Logger::write(LogLevel::Info, "FileExplorer Show on Desktop selected path=" + targetPath);
        const bool shown = Compositor::showFolderOnHostedDesktop(targetPath);
        s_status = shown ? "Shown on Desktop: " + entry.name : "Show on Desktop skipped";
        updateDisplay();
    }

    void FileExplorer::createFolder() {
        beginPrompt(PromptNewFolder, "New folder name", "NewFold1");
    }

    void FileExplorer::createFile() {
        beginPrompt(PromptNewFile, "New file name", "NewFile.txt");
    }

    void FileExplorer::renameSelected() {
        if (s_selectedIndex < 0 || s_selectedIndex >= static_cast<int>(s_entries.size())) return;
        beginPrompt(PromptRename, "Rename", s_entries[s_selectedIndex].name);
    }

    void FileExplorer::navigateToSelectedRoot() {
        if (s_rootSelectedIndex >= 0 && s_rootSelectedIndex < static_cast<int>(s_roots.size())) {
            navigate(s_roots[s_rootSelectedIndex].fullPath);
        }
    }

    void FileExplorer::handleKeyPress(int keyCode, const std::string& action) {
        if (action != "down") return;

        if (s_promptMode != PromptNone) {
            if (keyCode == 13) commitPrompt();
            else if (keyCode == 27) cancelPrompt();
            else if (keyCode == 8) backspacePromptChar();
            else {
                char ch = mapKeyToChar(keyCode);
                if (ch) appendPromptChar(ch);
            }
            return;
        }

        if (keyCode == 38 && s_selectedIndex > 0) {
            --s_selectedIndex;
            ensureSelectedFileVisible();
            resetFileListClickTracking();
            updateDisplay();
        } else if (keyCode == 40 && s_selectedIndex < static_cast<int>(s_entries.size()) - 1) {
            ++s_selectedIndex;
            ensureSelectedFileVisible();
            resetFileListClickTracking();
            updateDisplay();
        } else if (keyCode == 37 && s_rootSelectedIndex > 0) {
            --s_rootSelectedIndex;
            updateDisplay();
        } else if (keyCode == 39 && s_rootSelectedIndex < static_cast<int>(s_roots.size()) - 1) {
            ++s_rootSelectedIndex;
            updateDisplay();
        } else if (keyCode == 13) {
            openSelected();
        } else if (keyCode == 8) {
            goUp();
        } else if (keyCode == 46) {
            deleteSelected();
        } else if (keyCode == 113) {
            renameSelected();
        } else if (keyCode == 116) {
            refresh();
            updateDisplay();
        } else if (keyCode == 33) {
            if (s_entries.empty()) {
                s_selectedIndex = 0;
                s_scrollOffset = 0;
                updateDisplay();
                return;
            }
            const int pageStep = fileListVisibleRowCount();
            s_selectedIndex = std::max(0, s_selectedIndex - pageStep);
            ensureSelectedFileVisible();
            resetFileListClickTracking();
            updateDisplay();
        } else if (keyCode == 34) {
            if (s_entries.empty()) {
                s_selectedIndex = 0;
                s_scrollOffset = 0;
                updateDisplay();
                return;
            }
            const int maxIndex = static_cast<int>(s_entries.size()) - 1;
            const int pageStep = fileListVisibleRowCount();
            s_selectedIndex = std::min(maxIndex, s_selectedIndex + pageStep);
            ensureSelectedFileVisible();
            resetFileListClickTracking();
            updateDisplay();
        } else if (keyCode == 35) {
            if (s_entries.empty()) {
                s_selectedIndex = 0;
                s_scrollOffset = 0;
            } else {
                s_selectedIndex = static_cast<int>(s_entries.size()) - 1;
                ensureSelectedFileVisible();
                resetFileListClickTracking();
            }
            updateDisplay();
        } else if (keyCode == 36) {
            goHome();
        } else if (keyCode == 79) {
            navigateToSelectedRoot();
        } else if (keyCode == 76) {
            beginPrompt(PromptAddress, "Address", s_currentPath);
        }
    }

    void FileExplorer::handleMouseInput(const std::string& payload) {
        std::istringstream iss(payload);
        std::string xText, yText, buttonText, action;
        std::getline(iss, xText, '|');
        std::getline(iss, yText, '|');
        std::getline(iss, buttonText, '|');
        std::getline(iss, action, '|');
        if (xText.empty() || yText.empty() || buttonText.empty()) return;

        int x = 0;
        int y = 0;
        int button = 0;
        try {
            x = std::stoi(xText);
            y = std::stoi(yText);
            button = std::stoi(buttonText);
        } catch (...) {
            Logger::write(LogLevel::Warn, "FileExplorer mouse payload parse failed: " + payload);
            return;
        }

        auto parseWheelSteps = [](const std::string& value, int& steps) {
            if (value.rfind("wheel", 0) != 0) return false;
            if (value == "wheel" || value == "wheelup") {
                steps = 1;
                return true;
            }
            if (value == "wheeldown") {
                steps = -1;
                return true;
            }
            const size_t colon = value.find(':');
            if (colon == std::string::npos || colon + 1 >= value.size()) return false;
            try {
                steps = std::stoi(value.substr(colon + 1));
                return steps != 0;
            } catch (...) {
                return false;
            }
        };

        int wheelSteps = 0;
        if (parseWheelSteps(action, wheelSteps)) {
            if (s_promptMode == PromptNone && !s_showDeleteConfirmation &&
                x >= kLeftPaneW && x < kWindowW && y >= kMainRowsStartY && y < kStatusBarY &&
                isFileListScrollbarVisible()) {
                scrollFileListByRows(-wheelSteps * kWheelScrollRowsPerNotch);
            }
            return;
        }

        if (s_contextMenuOpen && action == "move") {
            int hover = hitTestContextMenu(x, y);
            if (hover != s_contextMenuHover) {
                s_contextMenuHover = hover;
                updateDisplay();
            }
            return;
        }

        if (s_contextMenuOpen && action == "down") {
            if (button == 1 && handleContextMenuClick(x, y)) return;
            s_contextMenuOpen = false;
            s_contextMenuHover = -1;
            s_contextMenuActions.clear();
            updateDisplay();
            if (button != 2) return;
        }

        if (action == "up" && button == 1) {
            s_draggingFileListScrollbar = false;
            return;
        }

        if (button == 2 && action == "down") {
            int rowIndex = hitTestEntryRow(x, y);
            Logger::write(LogLevel::Info, "FileExplorer context menu creation requested row=" + std::to_string(rowIndex));
            if (rowIndex >= 0) {
                showContextMenuForRow(rowIndex, x, y);
                updateDisplay();
            }
            return;
        }

        if (button == 1 && action == "down") {
            if (handleNavigationPaneClick(x, y)) {
                resetFileListClickTracking();
                s_draggingFileListScrollbar = false;
                return;
            }

            const int scrollbarHit = hitTestFileListScrollbar(x, y);
            if (scrollbarHit == 1) {
                s_draggingFileListScrollbar = true;
                s_fileListScrollbarDragStartY = y;
                s_fileListScrollbarDragStartOffsetRows = s_scrollOffset;
                s_hoveredIndex = -1;
                return;
            }
            if (scrollbarHit == 2) {
                scrollFileListByRows(-fileListVisibleRowCount());
                return;
            }
            if (scrollbarHit == 3) {
                scrollFileListByRows(fileListVisibleRowCount());
                return;
            }

            int rowIndex = hitTestEntryRow(x, y);
            if (rowIndex >= 0) {
                uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                if (rowIndex == s_lastEntryClickRow && (now - s_lastEntryClickTick) < kDoubleClickThresholdMs) {
                    s_selectedIndex = rowIndex;
                    resetFileListClickTracking();
                    ensureSelectedFileVisible();
                    openSelected();
                } else {
                    s_selectedIndex = rowIndex;
                    ensureSelectedFileVisible();
                    updateDisplay();
                    s_lastEntryClickRow = rowIndex;
                    s_lastEntryClickTick = now;
                }
            } else {
                resetFileListClickTracking();
            }
            return;
        }

        if (button == 0 && action == "move" && s_draggingFileListScrollbar && isFileListScrollbarVisible()) {
            const int trackTravel = std::max(1, fileListScrollbarTrackHeight() - fileListScrollbarThumbHeight());
            const int maxScroll = fileListMaxScrollRows();
            int nextOffset = s_fileListScrollbarDragStartOffsetRows +
                ((y - s_fileListScrollbarDragStartY) * maxScroll) / trackTravel;
            nextOffset = std::max(0, std::min(nextOffset, maxScroll));
            if (nextOffset != s_scrollOffset) {
                s_scrollOffset = nextOffset;
                s_hoveredIndex = -1;
                resetFileListClickTracking();
                updateDisplay();
            }
        }

        if (action == "move" && !s_draggingFileListScrollbar && isSciFiThemeActive()) {
            const int hover = hitTestEntryRow(x, y);
            if (hover != s_hoveredIndex) {
                s_hoveredIndex = hover;
                updateDisplay();
            }
        }
    }

    char FileExplorer::mapKeyToChar(int keyCode) {
        if (keyCode >= 65 && keyCode <= 90) return static_cast<char>('a' + (keyCode - 65));
        if (keyCode >= 48 && keyCode <= 57) return static_cast<char>('0' + (keyCode - 48));
        if (keyCode == 32) return ' ';
        if (keyCode == 190) return '.';
        if (keyCode == 191) return '/';
        if (keyCode == 189) return '-';
        if (keyCode == 186) return ':';
        if (keyCode == 220) return '/';
        return 0;
    }

    void FileExplorer::publish(MsgType type, const std::string& payload) {
        ipc::Message msg;
        msg.type = static_cast<uint32_t>(type);
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(msg), false);
    }

    void FileExplorer::drawRect(int x, int y, int w, int h, int r, int g, int b) {
        std::ostringstream oss;
        oss << s_windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|" << r << "|" << g << "|" << b;
        publish(MsgType::MT_DrawRect, oss.str());
    }

    void FileExplorer::drawText(const std::string& text) {
        publish(MsgType::MT_DrawText, std::to_string(s_windowId) + "|" + text);
    }

    void FileExplorer::drawTextAt(int x, int y, const std::string& text) {
        publish(MsgType::MT_DrawTextAt, packDrawTextAt(s_windowId, x, y, text));
    }

    void FileExplorer::drawSurfaceTextAt(int x, int y, const std::string& text, uint32_t sciFiColor) {
        if (!isSciFiThemeActive()) {
            drawTextAt(x, y, text);
            return;
        }

        const uint32_t color = sciFiColor == 0 ? FileExplorerTextColor() : sciFiColor;
        publish(MsgType::MT_DrawTextAtColor, packDrawTextAtColor(s_windowId, x, y,
            static_cast<uint8_t>((color >> 16) & 0xFF),
            static_cast<uint8_t>((color >> 8) & 0xFF),
            static_cast<uint8_t>(color & 0xFF),
            text));
    }

    void FileExplorer::drawDebugPlaceholder(int x, int y, int size) {
        drawRect(x, y, size, size, 180, 35, 35);
        drawRect(x + 1, y + 1, size - 2, size - 2, 255, 255, 255);
    }

    void FileExplorer::drawIcon(const std::string& logicalIconName, int x, int y, int iconSize) {
        const std::string path = gui::IconThemeManager::Instance().ResolveIconPath(logicalIconName, iconSize);

        if (path.empty()) {
            drawDebugPlaceholder(x, y, iconSize);
            return;
        }

        publish(MsgType::MT_DrawImage, packDrawImage(s_windowId, x, y, path));
    }

    void FileExplorer::addButton(int id, int x, int y, int w, int h, const std::string& text) {
        std::ostringstream oss;
        oss << s_windowId << "|1|" << id << "|" << x << "|" << y << "|" << w << "|" << h << "|" << text;
        publish(MsgType::MT_WidgetAdd, oss.str());
    }

    std::string FileExplorer::formatSize(uint64_t bytes) {
        if (bytes < 1024) return std::to_string(bytes) + " B";
        if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
        return std::to_string(bytes / (1024 * 1024)) + " MB";
    }

    std::string FileExplorer::truncate(const std::string& value, size_t width) {
        if (value.size() <= width) return value;
        if (width <= 3) return value.substr(0, width);
        return value.substr(0, width - 3) + "...";
    }

    std::string FileExplorer::padRight(const std::string& value, size_t width) {
        std::string out = truncate(value, width);
        while (out.size() < width) out.push_back(' ');
        return out;
    }

    std::string FileExplorer::kindText(const ExplorerFileEntry& entry) {
        if (entry.kind == ExplorerEntryKind::Drive) return "Drive";
        if (entry.kind == ExplorerEntryKind::Directory || entry.kind == ExplorerEntryKind::CommonFolder) return "Folder";
        return "File";
    }

    std::string FileExplorer::selectedPath() {
        if (s_selectedIndex < 0 || s_selectedIndex >= static_cast<int>(s_entries.size())) return s_currentPath;
        return s_entries[s_selectedIndex].fullPath;
    }

    std::string FileExplorer::makeUniqueChildPath(const std::string& baseName, bool directory) {
        std::string path = s_fileSystem->combinePath(s_currentPath, baseName);
        if (!s_fileSystem->exists(path)) return path;
        for (int i = 2; i < 100; ++i) {
            std::string candidate = baseName + " (" + std::to_string(i) + ")";
            if (!directory && baseName.find('.') != std::string::npos) {
                size_t dot = baseName.find_last_of('.');
                candidate = baseName.substr(0, dot) + " (" + std::to_string(i) + ")" + baseName.substr(dot);
            }
            path = s_fileSystem->combinePath(s_currentPath, candidate);
            if (!s_fileSystem->exists(path)) return path;
        }
        return path;
    }

    std::string FileExplorer::trashRootPath() {
        return kTrashRootPath;
    }

    std::string FileExplorer::makeUniquePathInDirectory(const std::string& directoryPath, const std::string& baseName, bool directory) {
        std::string path = s_fileSystem->combinePath(directoryPath, baseName);
        if (!s_fileSystem->exists(path)) return path;
        for (int i = 1; i < 100; ++i) {
            std::string candidate = baseName + " (" + std::to_string(i) + ")";
            if (!directory && baseName.find('.') != std::string::npos) {
                size_t dot = baseName.find_last_of('.');
                candidate = baseName.substr(0, dot) + " (" + std::to_string(i) + ")" + baseName.substr(dot);
            }
            path = s_fileSystem->combinePath(directoryPath, candidate);
            if (!s_fileSystem->exists(path)) return path;
        }
        return path;
    }

    std::string FileExplorer::trashInfoPathFor(const std::string& trashedPath) {
        return trashedPath + kTrashInfoSuffix;
    }

    std::string FileExplorer::jsonEscape(const std::string& value) {
        std::string out;
        out.reserve(value.size() + 8);
        for (char ch : value) {
            switch (ch) {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out.push_back(ch); break;
            }
        }
        return out;
    }

    void FileExplorer::refreshTrashDesktopState() {
        Logger::write(LogLevel::Info, "FileExplorer desktop Trash icon refresh requested");
#ifdef _WIN32
        gxos::gui::Compositor::requestDesktopRefresh();
#endif
    }

    bool FileExplorer::handleNavigationPaneClick(int x, int y) {
        if (x < 0 || x >= kLeftPaneW) return false;

        const int rootShortcutY = kNavStartY + kRowH;
        const int mountedDrivesY = rootShortcutY + kRowH;
        const int rootsStartY = mountedDrivesY + kRowH;
        const int commonFoldersY = rootsStartY + static_cast<int>(s_roots.size()) * kRowH;

        auto isRowHit = [&](int rowY) {
            return y >= rowY && y < rowY + kRowH;
        };

        if (isRowHit(rootShortcutY)) {
            if (!s_roots.empty()) {
                s_rootSelectedIndex = 0;
                goHome();
            } else {
                goHome();
            }
            return true;
        }

        if (isRowHit(mountedDrivesY)) {
            if (!s_roots.empty()) {
                s_rootSelectedIndex = 0;
                navigateToSelectedRoot();
            } else {
                goHome();
            }
            return true;
        }

        for (size_t i = 0; i < s_roots.size(); ++i) {
            const int rowY = rootsStartY + static_cast<int>(i) * kRowH;
            if (!isRowHit(rowY)) continue;
            s_rootSelectedIndex = static_cast<int>(i);
            navigateToSelectedRoot();
            return true;
        }

        if (isRowHit(commonFoldersY)) {
            if (!s_roots.empty()) {
                const int commonIndex = static_cast<int>(s_roots.size()) - 1;
                if (s_roots[commonIndex].kind == ExplorerEntryKind::CommonFolder) {
                    s_rootSelectedIndex = commonIndex;
                    navigateToSelectedRoot();
                } else {
                    goHome();
                }
            } else {
                goHome();
            }
            return true;
        }

        return false;
    }

    int FileExplorer::hitTestEntryRow(int x, int y) {
        if (x < kLeftPaneW || x >= fileListScrollbarLeft() || y < kMainRowsStartY || y >= kStatusBarY) return -1;
        const int visibleRows = fileListVisibleRowCount();
        const int row = (y - kMainRowsStartY) / kRowH;
        if (row < 0 || row >= visibleRows) return -1;
        const int index = s_scrollOffset + row;
        return (index >= 0 && index < static_cast<int>(s_entries.size())) ? index : -1;
    }

    int FileExplorer::hitTestFileListScrollbar(int x, int y) {
        if (!isFileListScrollbarVisible()) return 0;
        if (x < fileListScrollbarLeft() || x >= kWindowW) return 0;
        if (y < fileListScrollbarTrackTop() || y >= fileListScrollbarTrackTop() + fileListScrollbarTrackHeight()) return 0;

        const int thumbTop = fileListScrollbarThumbTop();
        const int thumbBottom = thumbTop + fileListScrollbarThumbHeight();
        if (y >= thumbTop && y < thumbBottom) return 1;
        if (y < thumbTop) return 2;
        return 3;
    }

    int FileExplorer::hitTestContextMenu(int x, int y) {
        if (!s_contextMenuOpen) return -1;
        const int itemCount = static_cast<int>(s_contextMenuActions.size());
        if (itemCount <= 0) return -1;
        if (x < s_contextMenuX || x >= s_contextMenuX + kContextMenuW) return -1;
        if (y < s_contextMenuY || y >= s_contextMenuY + kContextMenuItemH * itemCount) return -1;
        return (y - s_contextMenuY) / kContextMenuItemH;
    }

    void FileExplorer::showContextMenuForRow(int rowIndex, int x, int y) {
        if (rowIndex < 0 || rowIndex >= static_cast<int>(s_entries.size())) return;
        s_selectedIndex = rowIndex;
        s_contextMenuOpen = true;
        s_contextMenuActions.clear();
        s_contextMenuActions.push_back(static_cast<int>(ContextMenuAction::Open));
        s_contextMenuActions.push_back(static_cast<int>(ContextMenuAction::PinToDesktop));
#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
        if (s_entries[rowIndex].isDirectory()) {
            s_contextMenuActions.push_back(static_cast<int>(ContextMenuAction::ShowOnDesktop));
        }
#endif
        s_contextMenuActions.push_back(static_cast<int>(ContextMenuAction::Rename));
        s_contextMenuActions.push_back(static_cast<int>(ContextMenuAction::MoveToTrash));
        const int menuH = kContextMenuItemH * static_cast<int>(s_contextMenuActions.size());
        s_contextMenuX = std::min(x, kWindowW - kContextMenuW - 2);
        s_contextMenuY = std::min(y, kWindowH - menuH - 28);
        if (s_contextMenuX < 0) s_contextMenuX = 0;
        if (s_contextMenuY < 0) s_contextMenuY = 0;
        s_contextMenuHover = -1;
        Logger::write(LogLevel::Info, "FileExplorer context menu created for path=" + s_entries[rowIndex].fullPath);
    }

    bool FileExplorer::handleContextMenuClick(int x, int y) {
        int item = hitTestContextMenu(x, y);
        if (item < 0) return false;
        if (item >= static_cast<int>(s_contextMenuActions.size())) return false;
        const ContextMenuAction action = static_cast<ContextMenuAction>(s_contextMenuActions[item]);
        s_contextMenuOpen = false;
        s_contextMenuHover = -1;
        s_contextMenuActions.clear();
        switch (action) {
            case ContextMenuAction::Open: openSelected(); break;
            case ContextMenuAction::PinToDesktop: pinSelectedToDesktop(); break;
            case ContextMenuAction::ShowOnDesktop: showFolderOnDesktop(); break;
            case ContextMenuAction::Rename: renameSelected(); break;
            case ContextMenuAction::MoveToTrash: showDeleteConfirmation(); break;
            default: break;
        }
        return true;
    }

    bool FileExplorer::moveEntryToTrash(const ExplorerFileEntry& entry, std::string& error, std::string& trashedPath) {
        Logger::write(LogLevel::Info, std::string("FileExplorer move-to-trash resolved path=") + entry.fullPath);
        const std::string trashPath = trashRootPath();
        Logger::write(LogLevel::Info, std::string("FileExplorer trash directory selected=") + trashPath);
        if (!s_fileSystem->exists(trashPath)) {
            if (!s_fileSystem->createDirectory(trashPath, error)) {
                Logger::write(LogLevel::Error, std::string("FileExplorer trash mkdir failed: ") + error);
                return false;
            }
            Logger::write(LogLevel::Info, std::string("FileExplorer trash directory created=") + trashPath);
        }

        trashedPath = makeUniquePathInDirectory(trashPath, entry.name, entry.isDirectory());
        if (trashedPath != s_fileSystem->combinePath(trashPath, entry.name)) {
            Logger::write(LogLevel::Info, std::string("FileExplorer trash collision resolved to=") + trashedPath);
        }

        if (!s_fileSystem->rename(entry.fullPath, trashedPath, error)) {
            Logger::write(LogLevel::Error, std::string("FileExplorer move-to-trash failed: ") + error);
            return false;
        }

        std::time_t now = std::time(nullptr);
        std::ostringstream info;
        info << "{\n"
             << "  \"originalPath\": \"" << jsonEscape(entry.fullPath) << "\",\n"
             << "  \"originalName\": \"" << jsonEscape(entry.name) << "\",\n"
             << "  \"isDirectory\": " << (entry.isDirectory() ? "true" : "false") << ",\n"
             << "  \"trashedAt\": " << static_cast<long long>(now) << "\n"
             << "}";
        std::string infoText = info.str();
#ifndef _WIN32
        int32_t infoWritten = kernel::vfs::write_file(trashInfoPathFor(trashedPath).c_str(), infoText.data(), static_cast<uint32_t>(infoText.size()));
        if (infoWritten < 0) {
            Logger::write(LogLevel::Warn, std::string("FileExplorer trash metadata write failed for ") + trashedPath);
        }
#else
        std::filesystem::path infoPath = std::filesystem::current_path() / std::filesystem::path(trashInfoPathFor(trashedPath).substr(1));
        std::ofstream infoFile(infoPath, std::ios::binary | std::ios::trunc);
        if (infoFile) infoFile << infoText;
        else Logger::write(LogLevel::Warn, std::string("FileExplorer trash metadata write failed for ") + trashedPath);
#endif

        Logger::write(LogLevel::Info, std::string("FileExplorer move-to-trash success path=") + trashedPath);
        refreshTrashDesktopState();
        return true;
    }

    void FileExplorer::updateDisplay() {
        if (s_windowId == 0) return;
        publish(MsgType::MT_SetTitle, std::to_string(s_windowId) + "|File Explorer - " + s_currentPath);
        drawText("\f");
        const uint32_t bodyColor = FileExplorerBodyColor();
        drawRect(0, 0, kWindowW, kWindowH,
            static_cast<int>((bodyColor >> 16) & 0xFF),
            static_cast<int>((bodyColor >> 8) & 0xFF),
            static_cast<int>(bodyColor & 0xFF));
        renderToolbar();
        renderAddressBar();
        renderNavigationPane();
        renderMainPane();
        renderStatusBar();
        if (s_contextMenuOpen) renderContextMenu();

        if (s_showDeleteConfirmation) {
            std::string itemName = s_deleteTargetPath.substr(s_deleteTargetPath.find_last_of('/') + 1);
            std::string itemType = s_deleteTargetIsDirectory ? "folder" : "file";
            const uint32_t dialogColor = isSciFiThemeActive()
                ? blendColor(fileExplorerTheme().windowBackground, fileExplorerTheme().taskbarBackground, 12)
                : packRgb(45, 45, 55);
            const uint32_t dialogBorder = isSciFiThemeActive() ? FileExplorerBorderColor() : packRgb(45, 45, 55);
            drawRect(230, 190, 420, 100,
                static_cast<int>((dialogColor >> 16) & 0xFF),
                static_cast<int>((dialogColor >> 8) & 0xFF),
                static_cast<int>(dialogColor & 0xFF));
            drawRect(230, 190, 420, 1,
                static_cast<int>((dialogBorder >> 16) & 0xFF),
                static_cast<int>((dialogBorder >> 8) & 0xFF),
                static_cast<int>(dialogBorder & 0xFF));
            drawRect(230, 289, 420, 1,
                static_cast<int>((dialogBorder >> 16) & 0xFF),
                static_cast<int>((dialogBorder >> 8) & 0xFF),
                static_cast<int>(dialogBorder & 0xFF));
            drawRect(230, 190, 1, 100,
                static_cast<int>((dialogBorder >> 16) & 0xFF),
                static_cast<int>((dialogBorder >> 8) & 0xFF),
                static_cast<int>(dialogBorder & 0xFF));
            drawRect(649, 190, 1, 100,
                static_cast<int>((dialogBorder >> 16) & 0xFF),
                static_cast<int>((dialogBorder >> 8) & 0xFF),
                static_cast<int>(dialogBorder & 0xFF));
            drawSurfaceTextAt(242, 200, "MOVE TO TRASH", FileExplorerAccentColor());
            drawSurfaceTextAt(242, 220, "Move this " + itemType + " to Trash?", FileExplorerTextColor());
            drawSurfaceTextAt(242, 238, itemName, FileExplorerMutedTextColor());
            addButton(100, 270, 262, 80, 22, "Move to Trash");
            addButton(101, 370, 262, 80, 22, "Cancel");
        } else if (s_promptMode != PromptNone) {
            const uint32_t dialogColor = isSciFiThemeActive()
                ? blendColor(fileExplorerTheme().windowBackground, fileExplorerTheme().taskbarBackground, 12)
                : packRgb(45, 45, 55);
            const uint32_t dialogBorder = isSciFiThemeActive() ? FileExplorerBorderColor() : packRgb(45, 45, 55);
            drawRect(230, 190, 420, 84,
                static_cast<int>((dialogColor >> 16) & 0xFF),
                static_cast<int>((dialogColor >> 8) & 0xFF),
                static_cast<int>(dialogColor & 0xFF));
            drawRect(230, 190, 420, 1,
                static_cast<int>((dialogBorder >> 16) & 0xFF),
                static_cast<int>((dialogBorder >> 8) & 0xFF),
                static_cast<int>(dialogBorder & 0xFF));
            drawRect(230, 273, 420, 1,
                static_cast<int>((dialogBorder >> 16) & 0xFF),
                static_cast<int>((dialogBorder >> 8) & 0xFF),
                static_cast<int>(dialogBorder & 0xFF));
            drawRect(230, 190, 1, 84,
                static_cast<int>((dialogBorder >> 16) & 0xFF),
                static_cast<int>((dialogBorder >> 8) & 0xFF),
                static_cast<int>(dialogBorder & 0xFF));
            drawRect(649, 190, 1, 84,
                static_cast<int>((dialogBorder >> 16) & 0xFF),
                static_cast<int>((dialogBorder >> 8) & 0xFF),
                static_cast<int>(dialogBorder & 0xFF));
            drawSurfaceTextAt(242, 200, "INPUT: " + s_promptTitle, FileExplorerAccentColor());
            drawSurfaceTextAt(242, 220, "> " + s_promptValue + "_", FileExplorerTextColor());
            drawSurfaceTextAt(242, 240, "Enter=OK  Esc=Cancel  Backspace=Delete", FileExplorerMutedTextColor());
        }
    }

    void FileExplorer::renderToolbar() {
        const uint32_t toolbarColor = FileExplorerToolbarColor();
        const uint32_t borderColor = FileExplorerBorderColor();
        drawRect(0, 0, kWindowW, kToolbarH,
            static_cast<int>((toolbarColor >> 16) & 0xFF),
            static_cast<int>((toolbarColor >> 8) & 0xFF),
            static_cast<int>(toolbarColor & 0xFF));
        drawRect(0, kToolbarH - 1, kWindowW, 1,
            static_cast<int>((borderColor >> 16) & 0xFF),
            static_cast<int>((borderColor >> 8) & 0xFF),
            static_cast<int>(borderColor & 0xFF));
        const int iconY = kToolbarButtonY + (kToolbarButtonH - kToolbarIconSize) / 2;
        auto tbIcon = [&](const std::string& iconName, int btnX) {
            drawIcon(iconName, btnX + 3, iconY, kToolbarIconSize);
        };

        addButton(1,   8, 5,  56, 22, "< Back");
        tbIcon("place.home", 8);

        addButton(2,  68, 5,  66, 22, "> Fwd");

        addButton(3, 138, 5,  42, 22, "Up");
        tbIcon("file.folder", 138);

        addButton(4, 184, 5,  62, 22, "Refresh");

        addButton(5, 250, 5,  72, 22, "Address");

        addButton(6, 330, 5,  82, 22, "New Dir");
        tbIcon("file.folder", 330);

        addButton(7, 416, 5,  78, 22, "New File");
        tbIcon("file.generic", 416);

        addButton(8, 498, 5,  70, 22, "Rename");
        addButton(9, 572, 5,  64, 22, "Delete");

        addButton(14, 640, 5, 80, 22, "Mounts");
        tbIcon("drive.mounted", 640);

        // Context-sensitive buttons appear when file/folder is selected
        if (s_selectedIndex >= 0 && s_selectedIndex < static_cast<int>(s_entries.size())) {
            const ExplorerFileEntry& entry = s_entries[s_selectedIndex];
            if (entry.isDirectory()) {
                addButton(12, 725, 5, 80, 22, "Ren Fld");
                addButton(13, 810, 5, 80, 22, "Del Fld");
            } else {
                addButton(10, 725, 5, 80, 22, "Ren File");
                addButton(11, 810, 5, 80, 22, "Del File");
            }
        }
    }

    void FileExplorer::renderAddressBar() {
        const uint32_t pathColor = FileExplorerPathBoxColor();
        const uint32_t borderColor = FileExplorerBorderColor();
        drawRect(0, kToolbarH, kWindowW, kAddressH,
            static_cast<int>((pathColor >> 16) & 0xFF),
            static_cast<int>((pathColor >> 8) & 0xFF),
            static_cast<int>(pathColor & 0xFF));
        drawRect(0, kToolbarH + kAddressH - 1, kWindowW, 1,
            static_cast<int>((borderColor >> 16) & 0xFF),
            static_cast<int>((borderColor >> 8) & 0xFF),
            static_cast<int>(borderColor & 0xFF));
        drawSurfaceTextAt(8, centeredTextY(kToolbarH, kAddressH), "Address: " + s_currentPath, FileExplorerTextColor());
    }

    void FileExplorer::renderNavigationPane() {
        const int paneTop = kToolbarH + kAddressH;
        const int paneHeight = kWindowH - kToolbarH - kAddressH - 30;
        const uint32_t panelColor = FileExplorerPanelColor();
        const uint32_t borderColor = FileExplorerBorderColor();
        drawRect(0, paneTop, kLeftPaneW, paneHeight,
            static_cast<int>((panelColor >> 16) & 0xFF),
            static_cast<int>((panelColor >> 8) & 0xFF),
            static_cast<int>(panelColor & 0xFF));
        drawRect(kLeftPaneW - 1, paneTop, 1, paneHeight,
            static_cast<int>((borderColor >> 16) & 0xFF),
            static_cast<int>((borderColor >> 8) & 0xFF),
            static_cast<int>(borderColor & 0xFF));

        int y = kNavStartY;
        drawSurfaceTextAt(8, y, "Navigation", FileExplorerMutedTextColor());
        y += kRowH;

        drawIcon("place.computer", kNavIconX, rowIconY(y, kNavIconSize), kNavIconSize);
        drawSurfaceTextAt(kNavTextX, rowTextY(y), "Root", FileExplorerTextColor());
        y += kRowH;

        drawIcon("drive.fixed", kNavIconX, rowIconY(y, kNavIconSize), kNavIconSize);
        drawSurfaceTextAt(kNavTextX, rowTextY(y), "Mounted drives", FileExplorerTextColor());
        y += kRowH;

        for (size_t i = 0; i < s_roots.size(); ++i) {
            const ExplorerFileEntry& root = s_roots[i];
            std::string marker = static_cast<int>(i) == s_rootSelectedIndex ? "> " : "  ";
            const char* iconName = FileIconProvider::logicalIconNameForEntry(root);
            drawIcon(iconName, kNavIconX, rowIconY(y, kNavIconSize), kNavIconSize);
            drawSurfaceTextAt(kNavTextX, rowTextY(y), marker + truncate(root.name, 22),
                static_cast<int>(i) == s_rootSelectedIndex ? FileExplorerAccentColor() : FileExplorerTextColor());
            y += kRowH;
        }

        drawIcon("file.sysfolder", kNavIconX, rowIconY(y, kNavIconSize), kNavIconSize);
        drawSurfaceTextAt(kNavTextX, rowTextY(y), "Common folders", FileExplorerTextColor());
        y += kRowH;
        drawSurfaceTextAt(8, rowTextY(y), "Keys: L/R roots, O open", FileExplorerMutedTextColor());
    }

    void FileExplorer::renderMainPane() {
        const int mainX = kLeftPaneW;
        const int mainY = kToolbarH + kAddressH;
        const int mainW = kWindowW - kLeftPaneW;
        const int mainH = kWindowH - kToolbarH - kAddressH - 30;
        const uint32_t listColor = FileExplorerListColor();
        const uint32_t headerColor = FileExplorerHeaderColor();
        const uint32_t borderColor = FileExplorerBorderColor();
        drawRect(mainX, mainY, mainW, mainH,
            static_cast<int>((listColor >> 16) & 0xFF),
            static_cast<int>((listColor >> 8) & 0xFF),
            static_cast<int>(listColor & 0xFF));
        drawRect(mainX, mainY, mainW, kHeaderH,
            static_cast<int>((headerColor >> 16) & 0xFF),
            static_cast<int>((headerColor >> 8) & 0xFF),
            static_cast<int>(headerColor & 0xFF));
        drawRect(mainX, mainY + kHeaderH - 1, mainW, 1,
            static_cast<int>((borderColor >> 16) & 0xFF),
            static_cast<int>((borderColor >> 8) & 0xFF),
            static_cast<int>(borderColor & 0xFF));
        drawSurfaceTextAt(kLeftPaneW + 8, kHeaderTextY, "Name", FileExplorerMutedTextColor());
        drawSurfaceTextAt(kMainSizeTextX, kHeaderTextY, "Size", FileExplorerMutedTextColor());
        drawSurfaceTextAt(kMainTypeTextX, kHeaderTextY, "Type", FileExplorerMutedTextColor());
        drawSurfaceTextAt(kMainModifiedTextX, kHeaderTextY, "Modified", FileExplorerMutedTextColor());

        if (s_loading) {
            drawSurfaceTextAt(kLeftPaneW + 8, rowTextY(kMainRowsStartY), "Loading...", FileExplorerMutedTextColor());
            return;
        }
        if (s_entries.empty()) {
            drawSurfaceTextAt(kLeftPaneW + 8, rowTextY(kMainRowsStartY), "(Empty directory or unavailable path)", FileExplorerMutedTextColor());
            return;
        }

        const int visibleRows = fileListVisibleRowCount();
        const int start = std::max(0, std::min(s_scrollOffset, fileListMaxScrollRows()));
        const int end = std::min(static_cast<int>(s_entries.size()), start + visibleRows);
        for (int i = start; i < end; ++i) {
            const ExplorerFileEntry& entry = s_entries[i];
            const int row = i - start;
            const int rowY = kMainRowsStartY + row * kRowH;
            const bool selected = i == s_selectedIndex;
            const bool hovered = i == s_hoveredIndex;

            if (selected) {
                const uint32_t selectedColor = FileExplorerSelectedRowColor();
                drawRect(kLeftPaneW, rowY, kWindowW - kLeftPaneW - 8, kRowH,
                    static_cast<int>((selectedColor >> 16) & 0xFF),
                    static_cast<int>((selectedColor >> 8) & 0xFF),
                    static_cast<int>(selectedColor & 0xFF));
            } else if (hovered && isSciFiThemeActive()) {
                const uint32_t hoverColor = FileExplorerHoveredRowColor();
                drawRect(kLeftPaneW, rowY, kWindowW - kLeftPaneW - 8, kRowH,
                    static_cast<int>((hoverColor >> 16) & 0xFF),
                    static_cast<int>((hoverColor >> 8) & 0xFF),
                    static_cast<int>(hoverColor & 0xFF));
            }

            const FileIconType iconType = FileIconProvider::iconTypeForEntry(entry);
            const char* iconName = FileIconProvider::logicalIconName(iconType);
            drawIcon(iconName, kMainIconX, rowIconY(rowY, kIconSize), kIconSize);

            const std::string prefix = selected ? "> " : "  ";
            drawSurfaceTextAt(kMainNameTextX, rowTextY(rowY), prefix + truncate(entry.name, 26), FileExplorerTextColor());
            drawSurfaceTextAt(kMainSizeTextX, rowTextY(rowY), entry.isDirectory() ? "" : formatSize(entry.size), FileExplorerMutedTextColor());
            drawSurfaceTextAt(kMainTypeTextX, rowTextY(rowY), truncate(entry.type, 14), FileExplorerMutedTextColor());
            drawSurfaceTextAt(kMainModifiedTextX, rowTextY(rowY), entry.modified, FileExplorerMutedTextColor());
        }

        if (isFileListScrollbarVisible()) {
            const int trackLeft = fileListScrollbarLeft();
            const int trackTop = fileListScrollbarTrackTop();
            const int trackH = fileListScrollbarTrackHeight();
            const int thumbTop = fileListScrollbarThumbTop();
            const int thumbH = fileListScrollbarThumbHeight();

            const uint32_t trackColor = isSciFiThemeActive()
                ? blendColor(fileExplorerTheme().windowBackground, fileExplorerTheme().taskbarBackground, 16)
                : packRgb(236, 238, 242);
            const uint32_t thumbColor = isSciFiThemeActive()
                ? blendColor(fileExplorerTheme().accent, fileExplorerTheme().windowBorder, 34)
                : packRgb(150, 160, 176);
            drawRect(trackLeft, trackTop, kFileListScrollbarW, trackH,
                static_cast<int>((trackColor >> 16) & 0xFF),
                static_cast<int>((trackColor >> 8) & 0xFF),
                static_cast<int>(trackColor & 0xFF));
            drawRect(trackLeft, thumbTop, kFileListScrollbarW, thumbH,
                static_cast<int>((thumbColor >> 16) & 0xFF),
                static_cast<int>((thumbColor >> 8) & 0xFF),
                static_cast<int>(thumbColor & 0xFF));
            drawRect(trackLeft, trackTop, kFileListScrollbarW, 1,
                static_cast<int>((borderColor >> 16) & 0xFF),
                static_cast<int>((borderColor >> 8) & 0xFF),
                static_cast<int>(borderColor & 0xFF));
            drawRect(trackLeft, trackTop + trackH - 1, kFileListScrollbarW, 1,
                static_cast<int>((borderColor >> 16) & 0xFF),
                static_cast<int>((borderColor >> 8) & 0xFF),
                static_cast<int>(borderColor & 0xFF));
        }
    }

    void FileExplorer::renderStatusBar() {
        const uint32_t statusColor = FileExplorerStatusColor();
        const uint32_t borderColor = FileExplorerBorderColor();
        drawRect(0, kStatusBarY, kWindowW, kWindowH - kStatusBarY,
            static_cast<int>((statusColor >> 16) & 0xFF),
            static_cast<int>((statusColor >> 8) & 0xFF),
            static_cast<int>(statusColor & 0xFF));
        drawRect(0, kStatusBarY, kWindowW, 1,
            static_cast<int>((borderColor >> 16) & 0xFF),
            static_cast<int>((borderColor >> 8) & 0xFF),
            static_cast<int>(borderColor & 0xFF));
        std::ostringstream oss;
        oss << s_entries.size() << " item(s)";
        if (s_hasMoreEntries) oss << " | lazy page loaded";
        if (!s_status.empty()) oss << " | " << s_status;
        if (s_selectedIndex >= 0 && s_selectedIndex < static_cast<int>(s_entries.size())) oss << " | Selected: " << s_entries[s_selectedIndex].name;
        drawSurfaceTextAt(8, centeredTextY(kStatusBarY, kWindowH - kStatusBarY), oss.str(), FileExplorerMutedTextColor());
    }

    void FileExplorer::renderContextMenu() {
        const int itemCount = static_cast<int>(s_contextMenuActions.size());
        if (itemCount <= 0) return;
        const int menuH = itemCount * kContextMenuItemH;
        const uint32_t menuColor = FileExplorerContextMenuColor();
        const uint32_t menuBorder = isSciFiThemeActive() ? FileExplorerBorderColor() : packRgb(120, 120, 140);
        const uint32_t shadowColor = isSciFiThemeActive() ? blendColor(menuColor, packRgb(0, 0, 0), 18) : packRgb(30, 30, 35);
        const uint32_t hoverColor = FileExplorerContextMenuHoverColor();
        drawRect(s_contextMenuX + 2, s_contextMenuY + 2, kContextMenuW, menuH,
            static_cast<int>((shadowColor >> 16) & 0xFF),
            static_cast<int>((shadowColor >> 8) & 0xFF),
            static_cast<int>(shadowColor & 0xFF));
        drawRect(s_contextMenuX, s_contextMenuY, kContextMenuW, menuH,
            static_cast<int>((menuColor >> 16) & 0xFF),
            static_cast<int>((menuColor >> 8) & 0xFF),
            static_cast<int>(menuColor & 0xFF));
        drawRect(s_contextMenuX, s_contextMenuY, kContextMenuW, 1,
            static_cast<int>((menuBorder >> 16) & 0xFF),
            static_cast<int>((menuBorder >> 8) & 0xFF),
            static_cast<int>(menuBorder & 0xFF));
        drawRect(s_contextMenuX, s_contextMenuY + menuH - 1, kContextMenuW, 1,
            static_cast<int>((menuBorder >> 16) & 0xFF),
            static_cast<int>((menuBorder >> 8) & 0xFF),
            static_cast<int>(menuBorder & 0xFF));
        drawRect(s_contextMenuX, s_contextMenuY, 1, menuH,
            static_cast<int>((menuBorder >> 16) & 0xFF),
            static_cast<int>((menuBorder >> 8) & 0xFF),
            static_cast<int>(menuBorder & 0xFF));
        drawRect(s_contextMenuX + kContextMenuW - 1, s_contextMenuY, 1, menuH,
            static_cast<int>((menuBorder >> 16) & 0xFF),
            static_cast<int>((menuBorder >> 8) & 0xFF),
            static_cast<int>(menuBorder & 0xFF));
        for (int i = 0; i < itemCount; ++i) {
            const int itemY = s_contextMenuY + i * kContextMenuItemH;
            if (i == s_contextMenuHover) {
                drawRect(s_contextMenuX + 1, itemY + 1, kContextMenuW - 2, kContextMenuItemH - 2,
                    static_cast<int>((hoverColor >> 16) & 0xFF),
                    static_cast<int>((hoverColor >> 8) & 0xFF),
                    static_cast<int>(hoverColor & 0xFF));
            }
            drawSurfaceTextAt(s_contextMenuX + 8, centeredTextY(itemY, kContextMenuItemH), contextMenuLabel(static_cast<ContextMenuAction>(s_contextMenuActions[i])),
                FileExplorerTextColor());
        }
    }

}} // namespace gxos::apps
