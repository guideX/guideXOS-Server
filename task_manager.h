#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>
#include <vector>

namespace gxos { namespace apps {
    
    struct ProcessInfo {
        uint64_t pid;
        std::string name;
        bool running;
        int exitCode;
        uint64_t memoryBytes;
    };
    
    /// <summary>
    /// TaskManager - System monitoring and process management
    /// Features: Process list, performance charts, tombstoned apps, memory details
    /// Tabs: Processes | Performance | Tombstoned (matching Legacy TaskManager.cs)
    /// </summary>
    class TaskManager {
    public:
        /// <summary>
        /// Launch a new TaskManager instance
        /// </summary>
        /// <returns>Process ID of the launched TaskManager</returns>
        static uint64_t Launch();
        
    private:
        // Main entry point for TaskManager process
        static int main(int argc, char** argv);
        
        // Process management
        static void refreshProcessList();
        static void endSelectedProcess();
        
        // UI update
        static void updateDisplay();
        static void updateHeader();
        static void updateStatusBar();
        static void updatePerformanceTab();
        static void updateTombstonedTab();
        
        // Keyboard handling
        static void handleKeyPress(int keyCode);
        
        // State
        static uint64_t s_windowId;
        static std::vector<ProcessInfo> s_processes;
        static int s_selectedIndex;
        static int s_scrollOffset;
        static uint64_t s_lastRefreshTicks;
        static int s_lastKeyCode;
        static bool s_keyDown;
        
        // Tabs: 0=Processes, 1=Performance, 2=Tombstoned
        static int s_currentTab;
        static const int kTabCount = 3;
        
        // Performance tab state
        static int s_cpuPct;
        static int s_memPct;
        static int s_perfCategoryIndex; // 0=CPU, 1=Memory
        
        // System stats
        static uint64_t s_totalMemory;
        static uint64_t s_usedMemory;
        static uint64_t s_peakMemory;
        static uint64_t s_tasksExecuted;
    };
    
}} // namespace gxos::apps
