#include "file_explorer.h"
#include "logger.h"
#include "notepad.h"
#include <sstream>
#include <algorithm>
#include <cctype>

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
        constexpr int kRowH = 16;
        constexpr int kVisibleRows = 20;
        constexpr size_t kLazyPageSize = 256;

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
                return path == "/";
#endif
            }

            bool isDirectory(const std::string& path) override {
#ifndef _WIN32
                kernel::vfs::FileInfo info{};
                return kernel::vfs::stat(path.c_str(), &info) == kernel::vfs::VFS_OK && info.type == kernel::vfs::FILE_TYPE_DIRECTORY;
#else
                return path == "/";
#endif
            }

            bool createDirectory(const std::string& path, std::string& error) override {
#ifndef _WIN32
                kernel::vfs::Status status = kernel::vfs::mkdir(path.c_str());
                if (status == kernel::vfs::VFS_OK) return true;
                error = statusText(status);
                return false;
#else
                error = "Not supported in host test mode";
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
                error = "Not supported in host test mode";
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
                error = "Not supported in host test mode";
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
                error = "Not supported in host test mode";
                return false;
#endif
            }

            std::string normalizePath(const std::string& path) override {
#ifndef _WIN32
                char normalized[kernel::vfs::VFS_MAX_PATH]{};
                kernel::vfs::normalize_path(path.c_str(), normalized, sizeof(normalized));
                return normalized[0] ? normalized : "/";
#else
                return path.empty() ? "/" : path;
#endif
            }

            std::string combinePath(const std::string& base, const std::string& name) override {
#ifndef _WIN32
                char combined[kernel::vfs::VFS_MAX_PATH]{};
                kernel::vfs::join_path(base.c_str(), name.c_str(), combined, sizeof(combined));
                return combined;
#else
                if (base.empty() || base == "/") return "/" + name;
                return base + "/" + name;
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

    uint64_t FileExplorer::Launch(const std::string& startPath) {
        ProcessSpec spec{"file_explorer", FileExplorer::main};
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
        s_rootSelectedIndex = 0;
        s_status = "Ready";
        s_promptMode = PromptNone;
        s_showDeleteConfirmation = false;

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
        bool hasMore = false;
        s_entries = s_fileSystem->listDirectory(s_currentPath, 0, kLazyPageSize, hasMore);
        s_hasMoreEntries = hasMore;
        s_roots = s_fileSystem->getRoots();

        if (s_selectedIndex >= static_cast<int>(s_entries.size())) s_selectedIndex = static_cast<int>(s_entries.size()) - 1;
        if (s_selectedIndex < 0) s_selectedIndex = 0;
        s_loading = false;
        s_status = s_hasMoreEntries ? "Showing first page; more items available" : "Ready";
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

        std::string name = entry.name;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name.size() >= 4 && name.substr(name.size() - 4) == ".txt") {
            Notepad::LaunchWithFile(entry.fullPath);
        } else {
            s_status = "No file association registered for " + entry.name;
            updateDisplay();
        }
    }

    void FileExplorer::deleteSelected() {
        if (s_selectedIndex < 0 || s_selectedIndex >= static_cast<int>(s_entries.size())) return;
        const ExplorerFileEntry entry = s_entries[s_selectedIndex];
        std::string error;
        if (!s_fileSystem->remove(entry.fullPath, entry.isDirectory(), error)) {
            s_status = "Delete failed: " + error;
        } else {
            s_status = "Deleted " + entry.name;
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
        std::string error;
        std::string name = s_deleteTargetPath.substr(s_deleteTargetPath.find_last_of('/') + 1);
        if (!s_fileSystem->remove(s_deleteTargetPath, s_deleteTargetIsDirectory, error)) {
            s_status = "Delete failed: " + error;
        } else {
            s_status = "Deleted " + name;
        }
        refresh();
        updateDisplay();
    }

    void FileExplorer::cancelDelete() {
        s_showDeleteConfirmation = false;
        s_status = "Delete cancelled";
        updateDisplay();
    }

    void FileExplorer::createFolder() {
        beginPrompt(PromptNewFolder, "New folder name", "New Folder");
    }

    void FileExplorer::createFile() {
        beginPrompt(PromptNewFile, "New file name", "New File.txt");
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
            if (s_selectedIndex < s_scrollOffset) s_scrollOffset = s_selectedIndex;
            updateDisplay();
        } else if (keyCode == 40 && s_selectedIndex < static_cast<int>(s_entries.size()) - 1) {
            ++s_selectedIndex;
            if (s_selectedIndex >= s_scrollOffset + kVisibleRows) s_scrollOffset = s_selectedIndex - kVisibleRows + 1;
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
            s_scrollOffset = std::max(0, s_scrollOffset - kVisibleRows);
            s_selectedIndex = std::max(0, s_selectedIndex - kVisibleRows);
            updateDisplay();
        } else if (keyCode == 34) {
            int maxIndex = static_cast<int>(s_entries.size()) - 1;
            s_selectedIndex = std::min(maxIndex, s_selectedIndex + kVisibleRows);
            s_scrollOffset = std::max(0, std::min(s_selectedIndex, maxIndex - kVisibleRows + 1));
            updateDisplay();
        } else if (keyCode == 36) {
            goHome();
        } else if (keyCode == 79) {
            navigateToSelectedRoot();
        } else if (keyCode == 76) {
            beginPrompt(PromptAddress, "Address", s_currentPath);
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

    void FileExplorer::updateDisplay() {
        if (s_windowId == 0) return;
        publish(MsgType::MT_SetTitle, std::to_string(s_windowId) + "|File Explorer - " + s_currentPath);
        drawText("\f");
        renderToolbar();
        renderAddressBar();
        renderNavigationPane();
        renderMainPane();
        renderStatusBar();

        if (s_showDeleteConfirmation) {
            std::string itemName = s_deleteTargetPath.substr(s_deleteTargetPath.find_last_of('/') + 1);
            std::string itemType = s_deleteTargetIsDirectory ? "folder" : "file";
            drawRect(230, 190, 420, 100, 45, 45, 55);
            drawText("CONFIRM DELETE");
            drawText("");
            drawText("Are you sure you want to delete this " + itemType + "?");
            drawText(itemName);
            drawText("");
            addButton(100, 270, 262, 80, 22, "Yes, Delete");
            addButton(101, 370, 262, 80, 22, "Cancel");
        } else if (s_promptMode != PromptNone) {
            drawRect(230, 190, 420, 84, 45, 45, 55);
            drawText("INPUT: " + s_promptTitle);
            drawText("> " + s_promptValue + "_");
            drawText("Enter=OK  Esc=Cancel  Backspace=Delete");
        }
    }

    void FileExplorer::renderToolbar() {
        addButton(1, 8, 5, 56, 22, "Back");
        addButton(2, 68, 5, 66, 22, "Forward");
        addButton(3, 138, 5, 42, 22, "Up");
        addButton(4, 184, 5, 62, 22, "Refresh");
        addButton(5, 250, 5, 72, 22, "Address");
        addButton(6, 330, 5, 82, 22, "New Dir");
        addButton(7, 416, 5, 78, 22, "New File");
        addButton(8, 498, 5, 70, 22, "Rename");
        addButton(9, 572, 5, 64, 22, "Delete");

        if (s_selectedIndex >= 0 && s_selectedIndex < static_cast<int>(s_entries.size())) {
            const ExplorerFileEntry& entry = s_entries[s_selectedIndex];
            if (entry.isDirectory()) {
                addButton(12, 650, 5, 100, 22, "Rename Folder");
                addButton(13, 754, 5, 98, 22, "Delete Folder");
            } else {
                addButton(10, 650, 5, 88, 22, "Rename File");
                addButton(11, 742, 5, 86, 22, "Delete File");
            }
        }
    }

    void FileExplorer::renderAddressBar() {
        drawRect(0, 0, kWindowW, kToolbarH, 240, 240, 240);
        drawRect(0, kToolbarH, kWindowW, kAddressH, 252, 252, 252);
        drawText("Address: " + s_currentPath);
    }

    void FileExplorer::renderNavigationPane() {
        drawRect(0, kToolbarH + kAddressH, kLeftPaneW, kWindowH - kToolbarH - kAddressH - 30, 248, 248, 248);
        drawText("Navigation");
        drawText("  Root");
        drawText("  Mounted drives");
        for (size_t i = 0; i < s_roots.size(); ++i) {
            std::string marker = static_cast<int>(i) == s_rootSelectedIndex ? "> " : "  ";
            drawText(marker + truncate(s_roots[i].name, 24));
        }
        drawText("  Common folders");
        drawText("Keys: Left/Right roots, O open");
    }

    void FileExplorer::renderMainPane() {
        drawRect(kLeftPaneW, kToolbarH + kAddressH, kWindowW - kLeftPaneW, kHeaderH, 235, 235, 235);
        drawText("Name                              Size        Type             Modified");

        if (s_loading) {
            drawText("Loading...");
            return;
        }
        if (s_entries.empty()) {
            drawText("(Empty directory or unavailable path)");
            return;
        }

        int end = std::min(static_cast<int>(s_entries.size()), s_scrollOffset + kVisibleRows);
        for (int i = s_scrollOffset; i < end; ++i) {
            const ExplorerFileEntry& entry = s_entries[i];
            std::string prefix = i == s_selectedIndex ? "> " : "  ";
            std::string icon = entry.isDirectory() ? "[DIR] " : "[FILE]";
            std::string line = prefix + icon + " " + padRight(entry.name, 30)
                + padRight(entry.isDirectory() ? "" : formatSize(entry.size), 12)
                + padRight(entry.type, 17)
                + entry.modified;
            drawText(line);
        }
    }

    void FileExplorer::renderStatusBar() {
        std::ostringstream oss;
        oss << s_entries.size() << " item(s)";
        if (s_hasMoreEntries) oss << " | lazy page loaded";
        if (!s_status.empty()) oss << " | " << s_status;
        if (s_selectedIndex >= 0 && s_selectedIndex < static_cast<int>(s_entries.size())) oss << " | Selected: " << s_entries[s_selectedIndex].name;
        drawText(oss.str());
    }

}} // namespace gxos::apps
