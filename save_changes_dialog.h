#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>
#include <functional>

namespace gxos { namespace dialogs {
    
    /// <summary>
    /// SaveChangesDialog - Prompts user before closing with unsaved changes
    /// Simple dialog with three buttons: Save, Don't Save, Cancel
    /// </summary>
    class SaveChangesDialog {
    public:
        /// <summary>
        /// Show the dialog
        /// </summary>
        /// <param name="ownerX">Owner window X position</param>
        /// <param name="ownerY">Owner window Y position</param>
        /// <param name="onSave">Callback when Save clicked</param>
        /// <param name="onDontSave">Callback when Don't Save clicked</param>
        /// <param name="onCancel">Callback when Cancel clicked</param>
        static void Show(int ownerX, int ownerY, 
                        std::function<void()> onSave,
                        std::function<void()> onDontSave,
                        std::function<void()> onCancel);
        
    private:
        // Main entry point for dialog process
        static int main(int argc, char** argv);
        
        // UI update
        static void redraw();
        
        // State
        static uint64_t s_windowId;
        static std::function<void()> s_onSave;
        static std::function<void()> s_onDontSave;
        static std::function<void()> s_onCancel;
    };
    
}} // namespace gxos::dialogs
