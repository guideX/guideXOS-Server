#include "workspace_manager.h"
#include "logger.h"
#include <algorithm>

namespace gxos { namespace gui {
    
    // Static member initialization
    int WorkspaceManager::s_currentWorkspace = 0;
    std::vector<std::pair<uint64_t, int>> WorkspaceManager::s_windowWorkspaces;
    
    void WorkspaceManager::Initialize() {
        s_currentWorkspace = 0;
        s_windowWorkspaces.clear();
        Logger::write(LogLevel::Info, "WorkspaceManager initialized with " + std::to_string(s_workspaceCount) + " workspaces");
    }
    
    void WorkspaceManager::SwitchToWorkspace(int index) {
        if(index < 0 || index >= s_workspaceCount) {
            Logger::write(LogLevel::Info, "Invalid workspace index: " + std::to_string(index));
            return;
        }
        s_currentWorkspace = index;
        Logger::write(LogLevel::Info, "Switched to workspace " + std::to_string(index));
    }
    
    void WorkspaceManager::NextWorkspace() {
        s_currentWorkspace = (s_currentWorkspace + 1) % s_workspaceCount;
        Logger::write(LogLevel::Info, "Switched to workspace " + std::to_string(s_currentWorkspace));
    }
    
    void WorkspaceManager::PreviousWorkspace() {
        s_currentWorkspace = (s_currentWorkspace - 1 + s_workspaceCount) % s_workspaceCount;
        Logger::write(LogLevel::Info, "Switched to workspace " + std::to_string(s_currentWorkspace));
    }
    
    void WorkspaceManager::AssignWindowToWorkspace(uint64_t windowId, int workspace) {
        if(workspace < 0 || workspace >= s_workspaceCount) return;
        
        // Remove existing mapping if present
        for(auto it = s_windowWorkspaces.begin(); it != s_windowWorkspaces.end(); ++it) {
            if(it->first == windowId) {
                s_windowWorkspaces.erase(it);
                break;
            }
        }
        
        // Add new mapping
        s_windowWorkspaces.push_back({windowId, workspace});
    }
    
    int WorkspaceManager::GetWindowWorkspace(uint64_t windowId) {
        for(const auto& pair : s_windowWorkspaces) {
            if(pair.first == windowId) {
                return pair.second;
            }
        }
        // Default to current workspace if not explicitly assigned
        return s_currentWorkspace;
    }
    
    bool WorkspaceManager::IsWindowInCurrentWorkspace(uint64_t windowId) {
        for(const auto& pair : s_windowWorkspaces) {
            if(pair.first == windowId) {
                return pair.second == s_currentWorkspace;
            }
        }
        // Windows without explicit assignment are visible in all workspaces
        return true;
    }
    
} }
