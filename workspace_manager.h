#pragma once
#include <vector>
#include <cstdint>

namespace gxos { namespace gui {
    
    // Simple workspace manager for organizing windows
    class WorkspaceManager {
    public:
        static void Initialize();
        static void SwitchToWorkspace(int index);
        static void NextWorkspace();
        static void PreviousWorkspace();
        static int GetCurrentWorkspace() { return s_currentWorkspace; }
        static int GetWorkspaceCount() { return s_workspaceCount; }
        
        // Assign window to workspace
        static void AssignWindowToWorkspace(uint64_t windowId, int workspace);
        static int GetWindowWorkspace(uint64_t windowId);
        
        // Check if window should be visible in current workspace
        static bool IsWindowInCurrentWorkspace(uint64_t windowId);
        
    private:
        static const int s_workspaceCount = 4; // Support 4 workspaces
        static int s_currentWorkspace;
        static std::vector<std::pair<uint64_t, int>> s_windowWorkspaces; // windowId -> workspace mapping
    };
    
} }
