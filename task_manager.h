#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>
#include <vector>
#include <cstdint>

namespace gxos { namespace apps {

    struct ProcessSnapshot {
        uint64_t pid = 0;
        std::string displayName;
        std::string appId;
        std::string windowTitle;
        bool running = false;
        int exitCode = 0;
        uint64_t memoryBytes = 0;
        bool cpuPctAvailable = false;
        int cpuPct = 0;
        uint64_t cpuSampleWindowMs = 0;
        std::string cpuSource = "N/A";
        bool diskPctAvailable = false;
        int diskPct = 0;
        bool networkPctAvailable = false;
        int networkPct = 0;
    };

    struct MemorySnapshot {
        bool totalAvailable = false;
        uint64_t totalBytes = 0;
        uint64_t usedBytes = 0;
        uint64_t freeBytes = 0;
        uint64_t peakBytes = 0;
        bool totalFreedAvailable = false;
        uint64_t totalFreedBytes = 0;
        bool heapUtilPctAvailable = false;
        int heapUtilPct = 0;
        bool leakStateAvailable = false;
        bool leakState = false;
        bool freeAllocRatioAvailable = false;
        int freeAllocRatioPct = 0;
        bool topTagAvailable = false;
        std::string topTagName;
        uint64_t topTagBytes = 0;
        bool topOwnerAvailable = false;
        uint64_t topOwnerPid = 0;
        uint64_t topOwnerBytes = 0;
    };

    struct PerformanceSnapshot {
        bool cpuAvailable = false;
        int cpuPct = 0;
        std::string cpuSource = "N/A";
        uint64_t cpuSampleWindowMs = 0;
        uint64_t cpuBusyTimeMs = 0;
        uint64_t cpuIdleTimeMs = 0;
        bool memoryAvailable = false;
        int memoryPct = 0;
        bool diskAvailable = false;
        std::string diskSource = "N/A";
        uint64_t diskSampleWindowMs = 0;
        uint64_t diskReadBytesTotal = 0;
        uint64_t diskWriteBytesTotal = 0;
        uint64_t diskReadKBps = 0;
        uint64_t diskWriteKBps = 0;
        bool diskActivePctAvailable = false;
        int diskPct = 0;
        bool networkAvailable = false;
        int networkPct = 0;
        uint64_t processCount = 0;
        uint64_t nativeProcessCount = 0;
        uint64_t windowCount = 0;
        uint64_t schedulerTasksExecuted = 0;
        bool processCpuAvailable = false;
        std::string processCpuSource = "N/A";
        uint64_t processCpuSampleWindowMs = 0;
        uint64_t processCpuRowsWithCpu = 0;
        bool processDiskAvailable = false;
        bool syntheticCounters = false;
    };

    using TombstoneSnapshot = gxos::ProcessTombstoneRecord;

    struct TaskManagerSnapshot {
        std::vector<ProcessSnapshot> processes;
        MemorySnapshot memory;
        PerformanceSnapshot performance;
        std::vector<TombstoneSnapshot> tombstoned;
        bool tombstoneDetailsAvailable = false;
        bool tombstoneDiagnosticHistoryAvailable = false;
        bool appTombstonePolicyAvailable = false;
        std::string tombstoneCapabilitySource = "N/A";
        bool tombstoneRestoreImplemented = false;
        bool tombstoneRestoreSupported = false;
        uint32_t tombstoneHistoryCapacity = 0;
        uint32_t tombstoneAppCapabilityKnown = 0;
        uint32_t tombstoneRowsWithAppId = 0;
        uint32_t tombstoneRowsWithPolicy = 0;
        bool syntheticCounters = false;
    };
    
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
        static TaskManagerSnapshot BuildTaskManagerSnapshot();
        static std::string SnapshotDiagnostic();
        
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
        
        // Performance helpers
        static std::string formatMemory(uint64_t bytes);
        static std::string formatUptime(uint64_t ticks);
        static void recordPerformanceSnapshot(const TaskManagerSnapshot& snapshot);
        
        // State
        static uint64_t s_windowId;
        static TaskManagerSnapshot s_snapshot;
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
        static const int kCpuHistoryMax = 48;
        static uint64_t s_cpuHistory[kCpuHistoryMax];
        static int s_cpuHistoryCount;
        static int s_cpuHistoryHead;
        static int s_memPct;
        static int s_diskPct;
        static const int kDiskHistoryMax = 48;
        static uint64_t s_diskHistory[kDiskHistoryMax];
        static int s_diskHistoryCount;
        static int s_diskHistoryHead;
        static int s_netPct;
        static int s_perfCategoryIndex; // 0=CPU, 1=Memory, 2=Disk, 3=Network
        static const int kMemoryHistoryMax = 48;
        static uint64_t s_memoryHistory[kMemoryHistoryMax];
        static int s_memoryHistoryCount;
        static int s_memoryHistoryHead;
        
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
