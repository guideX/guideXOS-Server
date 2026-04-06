#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>
#include <vector>
#include <cstdint>

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
    /// Features: Process list, performance charts (CPU/Memory/Disk/Network),
    ///           tombstoned apps management, memory allocator details
    /// Tabs: Processes | Performance | Tombstoned | Memory Details
    /// Ported from guideXOS.Legacy DefaultApps/TaskManager.cs
    /// </summary>
    class TaskManager {
    public:
        static uint64_t Launch();
        
    private:
        // Main entry point
        static int main(int argc, char** argv);
        
        // Process management
        static void refreshProcessList();
        static void endSelectedProcess();
        
        // Tombstoned management (matching Legacy)
        static void restoreTombstoned();
        static void endTombstoned();
        static int countTombstoned();
        
        // UI update
        static void updateDisplay();
        static void updateHeader();
        static void updateStatusBar();
        static void updatePerformanceTab();
        static void updateTombstonedTab();
        static void updateMemoryDetailsTab();
        
        // Keyboard handling
        static void handleKeyPress(int keyCode);
        
        // Performance helpers (matching Legacy synthetic counters)
        static int wavePct(uint64_t ticks, int period);
        static std::string formatMemory(uint64_t bytes);
        static std::string formatTransferRate(int kbps);
        static std::string formatUptime(uint64_t ticks);
        
        // State
        static uint64_t s_windowId;
        static std::vector<ProcessInfo> s_processes;
        static int s_selectedIndex;
        static int s_scrollOffset;
        static uint64_t s_lastRefreshTicks;
        static int s_lastKeyCode;
        static bool s_keyDown;
        
        // Tabs: 0=Processes, 1=Performance, 2=Tombstoned, 3=Memory Details
        static int s_currentTab;
        static const int kTabCount = 4;
        
        // Tombstoned tab selection
        static int s_selectedTombIndex;
        
        // Performance tab: 4 categories (CPU, Memory, Disk, Network)
        static int s_cpuPct;
        static int s_memPct;
        static int s_diskPct;
        static int s_netPct;
        static int s_perfCategoryIndex; // 0=CPU, 1=Memory, 2=Disk, 3=Network
        
        // Synthetic disk/network counters (matching Legacy)
        static int s_diskActivePct;
        static int s_diskReadKBps;
        static int s_diskWriteKBps;
        static int s_diskRespMs;
        static int s_netSendKBps;
        static int s_netRecvKBps;
        static uint64_t s_bytesSent;
        static uint64_t s_bytesRecv;
        
        // System stats
        static uint64_t s_totalMemory;
        static uint64_t s_usedMemory;
        static uint64_t s_peakMemory;
        static uint64_t s_tasksExecuted;
        
        // Memory Details tab (matching Legacy)
        static uint64_t s_cumulativeAllocated;
        static uint64_t s_cumulativeFreed;
        static uint64_t s_lastMemDetailUpdate;
        static bool s_leakExists;
        static int s_leakGrowthCounter;
        static const int kLeakThreshold = 5;
        static std::vector<uint64_t> s_leakHistory;
        static const int kLeakHistoryMax = 60;
    };
    
}} // namespace gxos::apps
