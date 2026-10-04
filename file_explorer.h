#pragma once
#include "app_registry.h"
#include "file_explorer_open_with_model.h"
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
        static uint64_t LaunchWithActivation(const AppActivationContext& activation);
        static bool InspectVirtualPath(const std::string& path,
                                       std::string& normalizedPath,
                                       bool& exists,
                                       bool& isDirectory,
                                       std::string& error);
        /// Launch the existing Explorer confirmation UI for a stable target.
        static uint64_t LaunchDeleteConfirmation(const std::string& targetPath, bool isDirectory);
        
    private:
        // Main entry point for FileExplorer process
        static int main(int argc, char** argv);

        // Filesystem binding
        static std::unique_ptr<IExplorerFileSystem> createFileSystemProvider();
        
        // Navigation
        void navigate(const std::string& path, bool addHistory = true);
        void goBack();
        void goForward();
        void goUp();
        void goHome();
        void refresh();

        // Address/input prompt handling
        void beginPrompt(int mode, const std::string& title, const std::string& initialValue);
        void commitPrompt();
        void cancelPrompt();
        void appendPromptChar(char ch);
        void backspacePromptChar();
        
        // Actions
        void openSelected();
        void deleteSelected();
        void createFolder();
        void createFile();
        void renameSelected();
        void navigateToSelectedRoot();
        void showDeleteConfirmation();
        void confirmDelete();
        void cancelDelete();
        void pinSelectedToDesktop();
        void showFolderOnDesktop();
        void copySelectedFile();
        void cutSelectedFile();
        void pasteFileTo(const std::string& destinationPath);
        
        // Keyboard handling
        void handleKeyPress(int keyCode, const std::string& action);
        void handleMouseInput(const std::string& payload);
        char mapKeyToChar(int keyCode);

        // UI rendering helpers
        void publish(gxos::gui::MsgType type, const std::string& payload);
        void drawRect(int x, int y, int w, int h, int r, int g, int b);
        void drawText(const std::string& text);
        void drawTextAt(int x, int y, const std::string& text);
        void drawSurfaceTextAt(int x, int y, const std::string& text, uint32_t sciFiColor = 0);
        void drawIcon(const std::string& logicalIconName, int x, int y, int iconSize = 16);
        void drawDebugPlaceholder(int x, int y, int size);
        void addButton(int id, int x, int y, int w, int h, const std::string& text);
        std::string formatSize(uint64_t bytes);
        std::string truncate(const std::string& value, size_t width);
        std::string padRight(const std::string& value, size_t width);
        std::string kindText(const ExplorerFileEntry& entry);
        std::string selectedPath();
        std::string makeUniqueChildPath(const std::string& baseName, bool directory);
        bool moveEntryToTrash(const ExplorerFileEntry& entry, std::string& error, std::string& trashedPath);
        bool handleNavigationPaneClick(int x, int y);
        int hitTestEntryRow(int x, int y);
        int hitTestFileListScrollbar(int x, int y);
        int hitTestContextMenu(int x, int y);
        int hitTestOpenWithSubmenu(int x, int y);
        void showContextMenuForRow(int rowIndex, int x, int y);
        void showContextMenuForEmptySpace(int x, int y);
        bool handleContextMenuClick(int x, int y);
        int fileListVisibleRowCount();
        int fileListMaxScrollRows();
        bool isFileListScrollbarVisible();
        int fileListScrollbarLeft();
        int fileListScrollbarTrackTop();
        int fileListScrollbarTrackHeight();
        int fileListScrollbarThumbHeight();
        int fileListScrollbarThumbTop();
        void clampFileListState();
        void ensureSelectedFileVisible();
        bool isFileListWheelTarget(int x, int y);
        void scrollFileListByRows(int rows);
        void resetFileListClickTracking();
        
        // UI update
        void updateDisplay();
        void renderToolbar();
        void renderAddressBar();
        void renderNavigationPane();
        void renderMainPane();
        void renderStatusBar();
        void renderContextMenu();
        int run(int argc, char** argv);
        
        uint64_t s_windowId{0};
        std::unique_ptr<IExplorerFileSystem> s_fileSystem;
        std::string s_currentPath{"/"};
        std::vector<ExplorerFileEntry> s_entries;
        std::vector<ExplorerFileEntry> s_roots;
        std::vector<std::string> s_backHistory;
        std::vector<std::string> s_forwardHistory;
        int s_selectedIndex{0};
        int s_scrollOffset{0};
        int s_hoveredIndex{-1};
        bool s_draggingFileListScrollbar{false};
        int s_fileListScrollbarDragStartY{0};
        int s_fileListScrollbarDragStartOffsetRows{0};
        int s_rootSelectedIndex{0};
        int s_lastKeyCode{0};
        bool s_keyDown{false};
        bool s_loading{false};
        bool s_hasMoreEntries{false};
        std::string s_status{"Ready"};
        int s_promptMode{0};
        std::string s_promptTitle;
        std::string s_promptValue;
        bool s_showDeleteConfirmation{false};
        std::string s_deleteTargetPath;
        bool s_deleteTargetIsDirectory{false};
        bool s_contextMenuOpen{false};
        bool s_openWithSubmenuOpen{false};
        int s_contextMenuX{0};
        int s_contextMenuY{0};
        int s_contextMenuHover{-1};
        int s_openWithSubmenuHover{-1};
        std::vector<int> s_contextMenuActions;
        FileExplorerOpenWithMenuSnapshot s_contextMenuOpenWith;
        std::string s_contextMenuDestinationPath;
        uint64_t s_lastFileOperationGeneration{0};
        uint64_t s_lastEntryClickTick{0};
        int s_lastEntryClickRow{-1};
    };
    
}} // namespace gxos::apps
