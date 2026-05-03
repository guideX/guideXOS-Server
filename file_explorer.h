#pragma once
#include "process.h"
#include "ipc_bus.h"
#include "gui_protocol.h"
#include <string>
#include <vector>
#include <memory>

namespace gxos { namespace apps {

    enum class ExplorerEntryKind {
        File,
        Directory,
        Drive,
        CommonFolder
    };

    /// <summary>
    /// UI-facing file model.  It intentionally contains only filesystem-neutral
    /// metadata so the explorer can be backed by kernel VFS, image readers, or a
    /// future network/permission-aware filesystem service.
    /// </summary>
    struct ExplorerFileEntry {
        std::string name;
        std::string fullPath;
        uint64_t size{0};
        ExplorerEntryKind kind{ExplorerEntryKind::File};
        std::string type;
        std::string modified;

        bool isDirectory() const {
            return kind == ExplorerEntryKind::Directory || kind == ExplorerEntryKind::Drive || kind == ExplorerEntryKind::CommonFolder;
        }
    };

    /// <summary>
    /// Filesystem abstraction used by the explorer.  The UI never calls FAT,
    /// ext4, host, or kernel APIs directly.
    /// </summary>
    class IExplorerFileSystem {
    public:
        virtual ~IExplorerFileSystem() = default;
        virtual std::vector<ExplorerFileEntry> getRoots() = 0;
        virtual std::vector<ExplorerFileEntry> listDirectory(const std::string& path, size_t offset, size_t limit, bool& hasMore) = 0;
        virtual bool exists(const std::string& path) = 0;
        virtual bool isDirectory(const std::string& path) = 0;
        virtual bool createDirectory(const std::string& path, std::string& error) = 0;
        virtual bool createFile(const std::string& path, std::string& error) = 0;
        virtual bool remove(const std::string& path, bool directory, std::string& error) = 0;
        virtual bool rename(const std::string& oldPath, const std::string& newPath, std::string& error) = 0;
        virtual std::string normalizePath(const std::string& path) = 0;
        virtual std::string combinePath(const std::string& base, const std::string& name) = 0;
        virtual std::string parentPath(const std::string& path) = 0;
    };
    
    /// <summary>
    /// FileExplorer - Windows Explorer-style file browser over the OS filesystem abstraction.
    /// Features: roots/mounts pane, directory navigation, history, address editing,
    /// metadata display, create/delete/rename hooks, and file-open association point.
    /// </summary>
    class FileExplorer {
    public:
        /// <summary>
        /// Launch a new FileExplorer instance
        /// </summary>
        /// <param name="startPath">Optional starting directory path</param>
        /// <returns>Process ID of the launched FileExplorer</returns>
        static uint64_t Launch(const std::string& startPath = "");
        
    private:
        // Main entry point for FileExplorer process
        static int main(int argc, char** argv);

        // Filesystem binding
        static std::unique_ptr<IExplorerFileSystem> createFileSystemProvider();
        
        // Navigation
        static void navigate(const std::string& path, bool addHistory = true);
        static void goBack();
        static void goForward();
        static void goUp();
        static void goHome();
        static void refresh();

        // Address/input prompt handling
        static void beginPrompt(int mode, const std::string& title, const std::string& initialValue);
        static void commitPrompt();
        static void cancelPrompt();
        static void appendPromptChar(char ch);
        static void backspacePromptChar();
        
        // Actions
        static void openSelected();
        static void deleteSelected();
        static void createFolder();
        static void createFile();
        static void renameSelected();
        static void navigateToSelectedRoot();
        static void showDeleteConfirmation();
        static void confirmDelete();
        static void cancelDelete();
        
        // Keyboard handling
        static void handleKeyPress(int keyCode, const std::string& action);
        static char mapKeyToChar(int keyCode);

        // UI rendering helpers
        static void publish(gxos::gui::MsgType type, const std::string& payload);
        static void drawRect(int x, int y, int w, int h, int r, int g, int b);
        static void drawText(const std::string& text);
        static void addButton(int id, int x, int y, int w, int h, const std::string& text);
        static std::string formatSize(uint64_t bytes);
        static std::string truncate(const std::string& value, size_t width);
        static std::string padRight(const std::string& value, size_t width);
        static std::string kindText(const ExplorerFileEntry& entry);
        static std::string selectedPath();
        static std::string makeUniqueChildPath(const std::string& baseName, bool directory);
        
        // UI update
        static void updateDisplay();
        static void renderToolbar();
        static void renderAddressBar();
        static void renderNavigationPane();
        static void renderMainPane();
        static void renderStatusBar();
        
        // State
        static uint64_t s_windowId;
        static std::unique_ptr<IExplorerFileSystem> s_fileSystem;
        static std::string s_currentPath;
        static std::vector<ExplorerFileEntry> s_entries;
        static std::vector<ExplorerFileEntry> s_roots;
        static std::vector<std::string> s_backHistory;
        static std::vector<std::string> s_forwardHistory;
        static int s_selectedIndex;
        static int s_scrollOffset;
        static int s_rootSelectedIndex;
        static int s_lastKeyCode;
        static bool s_keyDown;
        static bool s_loading;
        static bool s_hasMoreEntries;
        static std::string s_status;
        static int s_promptMode;
        static std::string s_promptTitle;
        static std::string s_promptValue;
        static bool s_showDeleteConfirmation;
        static std::string s_deleteTargetPath;
        static bool s_deleteTargetIsDirectory;
    };
    
}} // namespace gxos::apps
