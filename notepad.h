#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>
#include <vector>
#include <cstdint>

namespace gxos { namespace apps {
    
    /// <summary>
    /// Notepad - Simple text editor application
    /// Features: Multi-line text editing, Open/Save via VFS, Text wrapping, Special characters,
    ///           Undo/Redo, Open dialog, Save/SaveAs/Save-changes dialogs
    /// Ported from guideXOS.Legacy DefaultApps/Notepad.cs
    /// </summary>
    class Notepad {
    public:
        /// <summary>
        /// Launch a new Notepad instance
        /// </summary>
        /// <returns>Process ID of the launched Notepad</returns>
        static uint64_t Launch();
        
        /// <summary>
        /// Launch Notepad with a specific file loaded
        /// </summary>
        static uint64_t LaunchWithFile(const std::string& filePath);
        
    private:
        // Main entry point for Notepad process
        static int main(int argc, char** argv);
        
        // Message handling
        static void handleMessage(const ipc::Message& m);
        
        // Text editing operations
        static void insertText(const std::string& text);
        static void deleteChar();       // Delete key (forward delete)
        static void deleteSelection();
        static void copy();
        static void paste();
        static void selectAll();
        
        // Undo / Redo (matching Legacy Notepad.cs)
        static void pushUndo();
        static void performUndo();
        static void performRedo();
        static const int kMaxUndo = 64;
        
        // File operations
        static void newFile();
        static void openFile();         // Load current s_filePath
        static void openFileDialog();   // Show Open dialog to pick file
        static void loadFile(const std::string& path);  // Load specific file
        static void saveFile();
        static void saveFileAs();
        static void closeWithPrompt();
        
        // UI operations
        static void toggleWrap();
        
        // UI update
        static void updateTitle();
        static void redrawContent();
        static void updateStatusBar();
        static void rebuildToolbarButtons();
        
        // Keyboard helpers
        static char mapKeyToChar(int keyCode);
        
        // Context menu operations
        static void showContextMenu(int x, int y);
        static void hideContextMenu();
        static bool handleContextMenuClick(int mx, int my);
        static void drawContextMenu();
        static bool updateMenuHover(int mx, int my);

        // File menu operations
        static void toggleFileMenu();
        static void hideFileMenu();
        static bool handleFileMenuClick(int mx, int my);
        static void drawFileMenu();

        // Edit menu operations
        static void toggleEditMenu();
        static void hideEditMenu();
        static bool handleEditMenuClick(int mx, int my);
        static void drawEditMenu();
        
        // State
        static uint64_t s_windowId;
        static std::string s_filePath;
        static std::vector<std::string> s_lines;
        static int s_cursorLine;
        static int s_cursorCol;
        static bool s_modified;
        static int s_scrollOffset;
        static bool s_wrapText;
        static bool s_shiftPressed;
        static bool s_ctrlPressed;
        static bool s_capsLockOn;
        static int s_lastKeyCode;
        static bool s_keyDown;
        static bool s_pendingClose;
        static int s_pendingModalLaunches;
        static std::vector<uint64_t> s_modalDialogWindowIds;
        
        // Context menu state
        static bool s_contextMenuVisible;
        static int s_contextMenuX;
        static int s_contextMenuY;
        static int s_contextMenuHoverIndex;

        // File menu state
        static bool s_fileMenuVisible;
        static int s_fileMenuX;
        static int s_fileMenuY;
        static int s_fileMenuHoverIndex;

        // Edit menu state
        static bool s_editMenuVisible;
        static int s_editMenuX;
        static int s_editMenuY;
        static int s_editMenuHoverIndex;
        
        // Undo / Redo stacks (store full line snapshots)
        struct TextSnapshot {
            std::vector<std::string> lines;
            int cursorLine;
            int cursorCol;
        };
        static std::vector<TextSnapshot> s_undoStack;
        static std::vector<TextSnapshot> s_redoStack;
    };
    
}} // namespace gxos::apps
