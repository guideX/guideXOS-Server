#pragma once
#include "process.h"
#include "app_activation.h"
#include "ipc_bus.h"
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <memory>

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
        
        static uint64_t LaunchWithActivation(const AppActivationContext& activation);
        
    private:
        enum class DialogResultKind { OpenPath, SavePath, SavePathAndClose };
        struct DialogResult {
            DialogResultKind kind;
            std::string path;
        };
        struct DialogResultQueue {
            std::mutex mutex;
            std::deque<DialogResult> pending;
        };

        // Main entry point for Notepad process
        static int main(int argc, char** argv);
        int run(int argc, char** argv);
        
        // Message handling
        void handleMessage(const ipc::Message& m);
        
        // Text editing operations
        void insertText(const std::string& text);
        void deleteChar();       // Delete key (forward delete)
        void deleteSelection();
        void deleteSelectionWithoutUndo();
        void copy();
        void paste();
        void selectAll();

        // Selection/caret helpers
        int documentLength();
        int cursorTextIndex();
        int lineColToTextIndex(int line, int col);
        void textIndexToLineCol(int index, int& line, int& col);
        void setCursorFromTextIndex(int index);
        bool hasSelection();
        int getSelectionStart();
        int getSelectionEnd();
        void clearSelection();
        bool isPointInTextArea(int x, int y);
        int pointToTextIndex(int x, int y);
        void beginMouseSelection(int x, int y);
        void updateMouseSelection(int x, int y);
        void endMouseSelection(int x, int y);
        bool runSelectionSelfTest();
        
        // Undo / Redo (matching Legacy Notepad.cs)
        void pushUndo();
        void performUndo();
        void performRedo();
        const int kMaxUndo = 64;
        
        // File operations
        void newFile();
        void openFile();         // Load current s_filePath
        void openFileDialog();   // Show Open dialog to pick file
        bool loadFile(const std::string& path);  // Load specific file
        void saveFile();
        void saveFileAs();
        void closeWithPrompt();
        std::function<void()> modalDialogCompletion();
        static void enqueueDialogResult(const std::shared_ptr<DialogResultQueue>& queue,
                                        DialogResultKind kind,
                                        const std::string& path);
        
        // UI operations
        void toggleWrap();
        
        // UI update
        void updateTitle();
        void redrawContent();
        void updateStatusBar();
        void rebuildToolbarButtons();
        
        // Keyboard helpers
        char mapKeyToChar(int keyCode);
        
        // Context menu operations
        void showContextMenu(int x, int y);
        void hideContextMenu();
        bool handleContextMenuClick(int mx, int my);
        void drawContextMenu();
        bool updateMenuHover(int mx, int my);

        // File menu operations
        void toggleFileMenu();
        void hideFileMenu();
        bool handleFileMenuClick(int mx, int my);
        void drawFileMenu();

        // Edit menu operations
        void toggleEditMenu();
        void hideEditMenu();
        bool handleEditMenuClick(int mx, int my);
        void drawEditMenu();
        
        // State
        uint64_t s_windowId;
        std::string s_filePath;
        std::vector<std::string> s_lines;
        int s_cursorLine;
        int s_cursorCol;
        int s_selectionAnchorIndex;
        int s_selectionActiveEndIndex;
        bool s_mouseSelecting;
        bool s_modified;
        int s_scrollOffset;
        bool s_wrapText;
        bool s_shiftPressed;
        bool s_ctrlPressed;
        bool s_capsLockOn;
        int s_lastKeyCode;
        bool s_keyDown;
        std::atomic<bool> s_pendingClose;
        std::atomic<int> s_closePromptResult;
        std::shared_ptr<std::atomic<int>> s_activeModalDialogs;
        std::shared_ptr<DialogResultQueue> s_dialogResults;
        
        // Context menu state
        bool s_contextMenuVisible;
        int s_contextMenuX;
        int s_contextMenuY;
        int s_contextMenuHoverIndex;

        // File menu state
        bool s_fileMenuVisible;
        int s_fileMenuX;
        int s_fileMenuY;
        int s_fileMenuHoverIndex;

        // Edit menu state
        bool s_editMenuVisible;
        int s_editMenuX;
        int s_editMenuY;
        int s_editMenuHoverIndex;
        
        // Undo / Redo stacks (store full line snapshots)
        struct TextSnapshot {
            std::vector<std::string> lines;
            int cursorLine;
            int cursorCol;
        };
        std::vector<TextSnapshot> s_undoStack;
        std::vector<TextSnapshot> s_redoStack;
    };
    
}} // namespace gxos::apps
