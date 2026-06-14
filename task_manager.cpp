#include "task_manager.h"
#include "gui_protocol.h"
#include "logger.h"
#include "allocator.h"
#include "scheduler.h"
#include "compositor.h"
#include "desktop_service.h"
#include "native_app_process_table.h"
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <cctype>
#include <unordered_map>

namespace gxos { namespace apps {
    
    using namespace gxos::gui;
    
    // Static member initialization
    uint64_t TaskManager::s_windowId = 0;
    TaskManagerSnapshot TaskManager::s_snapshot{};
    std::vector<ProcessInfo> TaskManager::s_processes;
    int TaskManager::s_selectedIndex = 0;
    int TaskManager::s_scrollOffset = 0;
    uint64_t TaskManager::s_lastRefreshTicks = 0;
    int TaskManager::s_lastKeyCode = 0;
    bool TaskManager::s_keyDown = false;
    
    uint64_t TaskManager::s_totalMemory = 0;
    uint64_t TaskManager::s_usedMemory = 0;
    uint64_t TaskManager::s_peakMemory = 0;
    uint64_t TaskManager::s_tasksExecuted = 0;
    
    int TaskManager::s_currentTab = 0;
    int TaskManager::s_selectedTombIndex = -1;
    int TaskManager::s_cpuPct = 0;
    int TaskManager::s_memPct = 0;
    int TaskManager::s_diskPct = 0;
    int TaskManager::s_netPct = 0;
    int TaskManager::s_perfCategoryIndex = 0;
    
    // Memory Details tab
    uint64_t TaskManager::s_cumulativeAllocated = 0;
    uint64_t TaskManager::s_cumulativeFreed = 0;
    uint64_t TaskManager::s_lastMemDetailUpdate = 0;
    bool TaskManager::s_leakExists = false;
    int TaskManager::s_leakGrowthCounter = 0;
    std::vector<uint64_t> TaskManager::s_leakHistory;
    
    uint64_t TaskManager::Launch() {
        ProcessSpec spec{"task_manager", TaskManager::main};
        return ProcessTable::spawn(spec, {"task_manager"});
    }

    namespace {
        static std::string normalizeIdentityLabel(const std::string& value) {
            std::string normalized;
            normalized.reserve(value.size());
            for (char ch : value) {
                if (std::isalnum(static_cast<unsigned char>(ch))) {
                    normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                }
            }
            return normalized;
        }

        static const gxos::gui::RegisteredDesktopApp* findRegisteredAppForLabel(const std::string& label) {
            if (label.empty()) return nullptr;
            const std::string normalizedLabel = normalizeIdentityLabel(label);
            if (normalizedLabel.empty()) return nullptr;

            for (const auto& app : gxos::gui::DesktopService::GetRegisteredApps()) {
                if (normalizeIdentityLabel(app.displayName) == normalizedLabel) return &app;
                if (normalizeIdentityLabel(app.launchName) == normalizedLabel) return &app;
                if (normalizeIdentityLabel(app.id) == normalizedLabel) return &app;
            }
            return nullptr;
        }

        static std::string allocTagName(gxos::AllocTag tag) {
            switch (tag) {
                case gxos::AllocTag::Unknown: return "Unknown";
                case gxos::AllocTag::ThreadMeta: return "ThreadMeta";
                case gxos::AllocTag::ThreadStack: return "ThreadStack";
                case gxos::AllocTag::ExecImage: return "ExecImage";
                case gxos::AllocTag::Image: return "Image";
                case gxos::AllocTag::FileBuffer: return "FileBuffer";
                case gxos::AllocTag::Temp: return "Temp";
                case gxos::AllocTag::Count: break;
            }
            return "Unknown";
        }
    }

    TaskManagerSnapshot TaskManager::BuildTaskManagerSnapshot() {
        TaskManagerSnapshot snapshot;

        const std::vector<uint64_t> pidList = ProcessTable::list();
        const std::vector<gxos::apps::NativeAppProcessInfo> nativeProcesses = NativeAppProcessTable::List();
        const std::vector<gxos::gui::WindowDebugInfo> windows = gxos::gui::Compositor::debugWindowsSnapshot();
        const std::vector<std::pair<uint64_t, uint64_t>> pidBytes = Allocator::listPidBytes();

        std::unordered_map<uint64_t, uint64_t> bytesByPid;
        for (const auto& entry : pidBytes) {
            bytesByPid[entry.first] = entry.second;
        }

        std::unordered_map<uint64_t, const gxos::apps::NativeAppProcessInfo*> nativeByPid;
        for (const auto& process : nativeProcesses) {
            if (process.nativePid != 0) nativeByPid[process.nativePid] = &process;
            if (process.runtimeId != 0) nativeByPid[process.runtimeId] = &process;
        }

        std::unordered_map<uint64_t, std::string> windowTitleByPid;
        for (const auto& window : windows) {
            if (window.ownerPid == 0 || window.title.empty()) continue;
            auto it = windowTitleByPid.find(window.ownerPid);
            if (it == windowTitleByPid.end() || it->second.empty()) {
                windowTitleByPid[window.ownerPid] = window.title;
            }
        }

        snapshot.memory.totalBytes = Allocator::totalSize();
        snapshot.memory.totalAvailable = snapshot.memory.totalBytes > 0;
        snapshot.memory.usedBytes = Allocator::bytesInUse();
        snapshot.memory.peakBytes = Allocator::peakBytes();
        snapshot.memory.freeBytes = snapshot.memory.totalBytes > snapshot.memory.usedBytes
            ? snapshot.memory.totalBytes - snapshot.memory.usedBytes
            : 0;
        snapshot.memory.totalFreedBytes = Allocator::totalFreed();
        snapshot.memory.totalFreedAvailable = true;
        snapshot.memory.heapUtilPctAvailable = snapshot.memory.totalAvailable && snapshot.memory.totalBytes > 0;
        if (snapshot.memory.heapUtilPctAvailable) {
            snapshot.memory.heapUtilPct = static_cast<int>((snapshot.memory.usedBytes * 100) / snapshot.memory.totalBytes);
        }
        snapshot.memory.leakStateAvailable = true;
        snapshot.memory.leakState = s_leakExists;
        if (snapshot.memory.totalFreedAvailable) {
            const uint64_t allocatedBytes = snapshot.memory.usedBytes + snapshot.memory.totalFreedBytes;
            if (allocatedBytes > 0) {
                snapshot.memory.freeAllocRatioAvailable = true;
                snapshot.memory.freeAllocRatioPct = static_cast<int>((snapshot.memory.totalFreedBytes * 100) / allocatedBytes);
            }
        }

        uint64_t maxTagBytes = 0;
        gxos::AllocTag maxTag = gxos::AllocTag::Unknown;
        for (int tagIndex = 0; tagIndex < static_cast<int>(gxos::AllocTag::Count); ++tagIndex) {
            gxos::AllocTag tag = static_cast<gxos::AllocTag>(tagIndex);
            uint64_t tagBytes = Allocator::tagBytes(tag);
            if (tagBytes > maxTagBytes) {
                maxTagBytes = tagBytes;
                maxTag = tag;
            }
        }
        if (maxTagBytes > 0) {
            snapshot.memory.topTagAvailable = true;
            snapshot.memory.topTagName = allocTagName(maxTag);
            snapshot.memory.topTagBytes = maxTagBytes;
        }

        uint64_t topOwnerPid = 0;
        uint64_t topOwnerBytes = 0;
        for (const auto& entry : pidBytes) {
            if (entry.second > topOwnerBytes) {
                topOwnerPid = entry.first;
                topOwnerBytes = entry.second;
            }
        }
        if (topOwnerBytes > 0) {
            snapshot.memory.topOwnerAvailable = true;
            snapshot.memory.topOwnerPid = topOwnerPid;
            snapshot.memory.topOwnerBytes = topOwnerBytes;
        }

        snapshot.performance.processCount = pidList.size();
        snapshot.performance.nativeProcessCount = nativeProcesses.size();
        snapshot.performance.windowCount = windows.size();
        snapshot.performance.schedulerTasksExecuted = Scheduler::tasksExecuted();
        snapshot.performance.memoryAvailable = snapshot.memory.totalAvailable;
        snapshot.performance.memoryPct = snapshot.memory.heapUtilPctAvailable ? snapshot.memory.heapUtilPct : 0;
        snapshot.performance.cpuAvailable = false;
        snapshot.performance.diskAvailable = false;
        snapshot.performance.networkAvailable = false;
        snapshot.performance.syntheticCounters = false;

        for (uint64_t pid : pidList) {
            ProcessSnapshot info;
            info.pid = pid;

            bool running = false;
            int exitCode = 0;
            if (ProcessTable::getStatus(pid, running, exitCode)) {
                info.running = running;
                info.exitCode = exitCode;
            }

            auto bytesIt = bytesByPid.find(pid);
            if (bytesIt != bytesByPid.end()) {
                info.memoryBytes = bytesIt->second;
            }

            auto nativeIt = nativeByPid.find(pid);
            if (nativeIt != nativeByPid.end() && nativeIt->second) {
                const auto& native = *nativeIt->second;
                info.appId = native.appId;
                info.displayName = !native.displayName.empty() ? native.displayName :
                                   (!native.appId.empty() ? native.appId : std::string());
                auto windowTitleIt = windowTitleByPid.find(pid);
                if (windowTitleIt != windowTitleByPid.end()) {
                    info.windowTitle = windowTitleIt->second;
                }
            } else {
                auto windowTitleIt = windowTitleByPid.find(pid);
                if (windowTitleIt != windowTitleByPid.end()) {
                    info.windowTitle = windowTitleIt->second;
                    info.displayName = windowTitleIt->second;
                    if (const auto* app = findRegisteredAppForLabel(windowTitleIt->second)) {
                        if (!app->id.empty()) info.appId = app->id;
                    }
                }
            }

            if (info.displayName.empty()) {
                if (!info.windowTitle.empty()) {
                    info.displayName = info.windowTitle;
                } else if (!info.appId.empty()) {
                    info.displayName = info.appId;
                } else {
                    info.displayName = std::string("Process-") + std::to_string(pid);
                }
            }

            if (info.appId.empty()) {
                if (const auto* app = findRegisteredAppForLabel(info.displayName)) {
                    info.appId = app->id;
                    if (info.windowTitle.empty() && !app->displayName.empty()) {
                        info.displayName = app->displayName;
                    }
                }
            }

            info.cpuPctAvailable = false;
            info.diskPctAvailable = false;
            info.networkPctAvailable = false;
            snapshot.processes.push_back(info);
        }

        std::sort(snapshot.processes.begin(), snapshot.processes.end(),
            [](const ProcessSnapshot& a, const ProcessSnapshot& b) {
                return a.pid < b.pid;
            });

        for (const ProcessSnapshot& process : snapshot.processes) {
            if (!process.running) {
                TombstoneSnapshot tombstone;
                tombstone.displayName = process.displayName;
                tombstone.appId = process.appId;
                tombstone.pid = process.pid;
                if (process.exitCode != 0) {
                    tombstone.reason = std::string("exitCode=") + std::to_string(process.exitCode);
                } else {
                    tombstone.reason = "stopped";
                }
                tombstone.restoreSupported = true;
                tombstone.endSupported = true;
                snapshot.tombstoned.push_back(tombstone);
            }
        }

        snapshot.syntheticCounters = false;
        return snapshot;
    }

    std::string TaskManager::SnapshotDiagnostic() {
        TaskManagerSnapshot snapshot = BuildTaskManagerSnapshot();
        std::ostringstream oss;
        oss << "tabs=Processes,Performance,Tombstoned,Memory Details\n";
        oss << "title=Task Manager\n";
        oss << "processes=" << snapshot.performance.processCount << "\n";
        oss << "memoryUsed=" << snapshot.memory.usedBytes << "\n";
        if (snapshot.memory.totalAvailable) {
            oss << "memoryTotal=" << snapshot.memory.totalBytes << "\n";
            oss << "memoryTotalSource=allocatorHeap\n";
            oss << "memoryTotalDerived=true\n";
        } else {
            oss << "memoryTotal=N/A\n";
            oss << "memoryTotalDerived=false\n";
        }
        if (snapshot.performance.cpuAvailable) {
            oss << "cpu=" << snapshot.performance.cpuPct << "\n";
        } else {
            oss << "cpu=N/A\n";
        }
        oss << "disk=N/A\n";
        oss << "network=N/A\n";
        oss << "tombstoned=" << snapshot.tombstoned.size() << "\n";
        oss << "syntheticCounters=" << (snapshot.syntheticCounters ? "true" : "false") << "\n";
        return oss.str();
    }

    int TaskManager::main(int argc, char** argv) {
        try {
            Logger::write(LogLevel::Info, "TaskManager starting...");
            
            // Initialize state
            s_windowId = 0;
            s_snapshot = TaskManagerSnapshot{};
            s_processes.clear();
            s_selectedIndex = 0;
            s_scrollOffset = 0;
            s_lastRefreshTicks = 0;
            s_lastKeyCode = 0;
            s_keyDown = false;
            s_currentTab = 0;
            s_selectedTombIndex = -1;
            s_cpuPct = 0;
            s_memPct = 0;
            s_diskPct = 0;
            s_netPct = 0;
            s_perfCategoryIndex = 0;
            s_cumulativeAllocated = 0;
            s_cumulativeFreed = 0;
            s_lastMemDetailUpdate = 0;
            s_leakExists = false;
            s_leakGrowthCounter = 0;
            s_leakHistory.clear();
            
            refreshProcessList();
            
            // Subscribe to IPC channels
            const char* kGuiChanIn = "gui.input";
            const char* kGuiChanOut = "gui.output";
            ipc::Bus::ensure(kGuiChanIn);
            ipc::Bus::ensure(kGuiChanOut);
            
            // Create window (760x520 matching Legacy)
            ipc::Message createMsg;
            createMsg.type = (uint32_t)MsgType::MT_Create;
            std::ostringstream oss;
            oss << "Task Manager|760|520";
            std::string payload = oss.str();
            createMsg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(createMsg), false);
            
            // Main event loop
            bool running = true;
            while (running) {
                ipc::Message msg;
                if (ipc::Bus::pop(kGuiChanOut, msg, 50)) {  // 50ms timeout
                    MsgType msgType = (MsgType)msg.type;
                    
                    switch (msgType) {
                        case MsgType::MT_Create: {
                            // Window created - extract window ID
                            std::string payload(msg.data.begin(), msg.data.end());
                            size_t sep = payload.find('|');
                            if (sep != std::string::npos && sep > 0) {
                                try {
                                    std::string idStr = payload.substr(0, sep);
                                    s_windowId = std::stoull(idStr);
                                    Logger::write(LogLevel::Info, std::string("TaskManager window created: ") + std::to_string(s_windowId));
                                    
                                    // Add buttons
                                    auto addButton = [](int id, int x, int y, int w, int h, const std::string& text) {
                                        ipc::Message msg;
                                        msg.type = (uint32_t)MsgType::MT_WidgetAdd;
                                        std::ostringstream oss;
                                        oss << s_windowId << "|1|" << id << "|" << x << "|" << y << "|" << w << "|" << h << "|" << text;
                                        std::string payload = oss.str();
                                        msg.data.assign(payload.begin(), payload.end());
                                        ipc::Bus::publish("gui.input", std::move(msg), false);
                                    };
                                    
                                    // Control buttons
                                    addButton(1, 4, 4, 80, 20, "Refresh");
                                    addButton(2, 88, 4, 100, 20, "End Process");
                                    
                                    // Tab buttons (4 tabs matching Legacy)
                                    addButton(10, 4, 28, 110, 22, "Processes");
                                    addButton(11, 118, 28, 110, 22, "Performance");
                                    addButton(12, 232, 28, 110, 22, "Tombstoned");
                                    addButton(13, 346, 28, 130, 22, "Memory Details");
                                    
                                    // Tombstoned tab action buttons
                                    addButton(20, 560, 440, 150, 24, "Restore");
                                    addButton(21, 560, 468, 150, 24, "End Tombstoned");
                                    
                                    // Draw initial content
                                    updateHeader();
                                    updateDisplay();
                                    updateStatusBar();
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("TaskManager: Failed to parse window ID: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_Close: {
                            // Window closed
                            std::string payload(msg.data.begin(), msg.data.end());
                            if (!payload.empty()) {
                                try {
                                    uint64_t closedId = std::stoull(payload);
                                    if (closedId == s_windowId) {
                                        Logger::write(LogLevel::Info, "TaskManager closing...");
                                        running = false;
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("TaskManager: Failed to parse close ID: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_InputKey: {
                            // Keyboard input
                            std::string payload(msg.data.begin(), msg.data.end());
                            size_t sep = payload.find('|');
                            if (sep != std::string::npos && sep > 0) {
                                try {
                                    int keyCode = std::stoi(payload.substr(0, sep));
                                    std::string action = payload.substr(sep + 1);
                                    
                                    // Key debouncing
                                    if (action == "down") {
                                        if (s_keyDown && keyCode == s_lastKeyCode) {
                                            break;
                                        }
                                        s_keyDown = true;
                                        s_lastKeyCode = keyCode;
                                        handleKeyPress(keyCode);
                                    } else {
                                        s_keyDown = false;
                                        s_lastKeyCode = 0;
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("TaskManager: Failed to parse key code: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_WidgetEvt: {
                            // Button click
                            std::string payload(msg.data.begin(), msg.data.end());
                            std::istringstream iss(payload);
                            std::string winIdStr, widgetIdStr, event, value;
                            std::getline(iss, winIdStr, '|');
                            std::getline(iss, widgetIdStr, '|');
                            std::getline(iss, event, '|');
                            std::getline(iss, value);
                            
                            if (!winIdStr.empty() && !widgetIdStr.empty()) {
                                try {
                                    uint64_t winId = std::stoull(winIdStr);
                                    if (winId == s_windowId && event == "click") {
                                        int widgetId = std::stoi(widgetIdStr);
                                        
                                        switch (widgetId) {
                                            case 1: // Refresh
                                                refreshProcessList();
                                                updateDisplay();
                                                updateStatusBar();
                                                break;
                                            case 2: // End Process
                                                endSelectedProcess();
                                                break;
                                            case 10: // Processes tab
                                                s_currentTab = 0;
                                                updateDisplay();
                                                break;
                                            case 11: // Performance tab
                                                s_currentTab = 1;
                                                updateDisplay();
                                                break;
                                            case 12: // Tombstoned tab
                                                s_currentTab = 2;
                                                updateDisplay();
                                                break;
                                            case 13: // Memory Details tab
                                                s_currentTab = 3;
                                                updateDisplay();
                                                break;
                                            case 20: // Restore tombstoned
                                                restoreTombstoned();
                                                break;
                                            case 21: // End tombstoned
                                                endTombstoned();
                                                break;
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("TaskManager: Failed to parse widget event: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        default:
                            break;
                    }
                }
                
                // Auto-refresh every 2 seconds
                auto now = std::chrono::steady_clock::now();
                uint64_t nowTicks = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                
                if (s_windowId != 0 && (nowTicks - s_lastRefreshTicks >= 2000)) {
                    refreshProcessList();
                    updateDisplay();
                    updateStatusBar();
                    s_lastRefreshTicks = nowTicks;
                }
            }
            
            Logger::write(LogLevel::Info, "TaskManager stopped");
            return 0;
            
        } catch (const std::exception& e) {
            Logger::write(LogLevel::Error, std::string("TaskManager EXCEPTION: ") + e.what());
            return -1;
        } catch (...) {
            Logger::write(LogLevel::Error, "TaskManager UNKNOWN EXCEPTION");
            return -1;
        }
    }
    
    void TaskManager::refreshProcessList() {
        s_snapshot = BuildTaskManagerSnapshot();
        s_processes.clear();
        s_processes.reserve(s_snapshot.processes.size());
        for (const auto& process : s_snapshot.processes) {
            ProcessInfo info;
            info.pid = process.pid;
            info.name = process.displayName;
            info.running = process.running;
            info.exitCode = process.exitCode;
            info.memoryBytes = process.memoryBytes;
            s_processes.push_back(info);
        }
        s_totalMemory = s_snapshot.memory.totalAvailable ? s_snapshot.memory.totalBytes : 0;
        s_usedMemory = s_snapshot.memory.usedBytes;
        s_peakMemory = s_snapshot.memory.peakBytes;
        s_tasksExecuted = s_snapshot.performance.schedulerTasksExecuted;
        
        // Ensure selected index is valid
        if (s_selectedIndex >= (int)s_processes.size()) {
            s_selectedIndex = (int)s_processes.size() - 1;
        }
        if (s_selectedIndex < 0) {
            s_selectedIndex = 0;
        }
        
        Logger::write(LogLevel::Info, std::string("TaskManager: Refreshed ") + std::to_string(s_processes.size()) + " processes");
    }
    
    void TaskManager::endSelectedProcess() {
        if (s_selectedIndex < 0 || s_selectedIndex >= (int)s_processes.size()) {
            return;
        }
        
        const ProcessInfo& proc = s_processes[s_selectedIndex];
        
        // Don't allow ending process 0 (system)
        if (proc.pid == 0) {
            Logger::write(LogLevel::Warn, "TaskManager: Cannot end system process");
            return;
        }
        
        // Don't allow ending own process
        // (We can't get our own PID easily, but we can prevent ending the TaskManager window)
        
        Logger::write(LogLevel::Info, std::string("TaskManager: Ending process ") + std::to_string(proc.pid));
        
        if (ProcessTable::terminate(proc.pid)) {
            Logger::write(LogLevel::Info, std::string("TaskManager: Successfully ended process ") + std::to_string(proc.pid));
            // Refresh to show updated list
            refreshProcessList();
            updateDisplay();
            updateStatusBar();
        } else {
            Logger::write(LogLevel::Error, std::string("TaskManager: Failed to end process ") + std::to_string(proc.pid));
        }
    }
    
    void TaskManager::handleKeyPress(int keyCode) {
        // Tab key - cycle tabs
        if (keyCode == 9) {
            s_currentTab = (s_currentTab + 1) % kTabCount;
            updateDisplay();
            return;
        }
        
        // Tab-specific handling
        if (s_currentTab == 1) {
            // Performance tab: Left/Right to change category (4 categories)
            if (keyCode == 37) { // Left
                if (s_perfCategoryIndex > 0) s_perfCategoryIndex--;
                updateDisplay();
                return;
            }
            if (keyCode == 39) { // Right
                if (s_perfCategoryIndex < 3) s_perfCategoryIndex++;
                updateDisplay();
                return;
            }
            if (keyCode == 116) { refreshProcessList(); updateDisplay(); updateStatusBar(); }
            return;
        }
        
        if (s_currentTab == 2) {
            // Tombstoned tab: Up/Down to select, R=Restore, E/Del=End
            if (keyCode == 38) { // Up
                if (s_selectedTombIndex > 0) s_selectedTombIndex--;
                updateDisplay();
                return;
            }
            if (keyCode == 40) { // Down
                int count = countTombstoned();
                if (s_selectedTombIndex < count - 1) s_selectedTombIndex++;
                updateDisplay();
                return;
            }
            if (keyCode == 82 || keyCode == 114) { // R - Restore
                restoreTombstoned();
                return;
            }
            if (keyCode == 46 || keyCode == 69 || keyCode == 101) { // Del/E - End
                endTombstoned();
                return;
            }
            if (keyCode == 116) { refreshProcessList(); updateDisplay(); updateStatusBar(); }
            return;
        }
        
        if (s_currentTab == 3) {
            // Memory Details tab: F5 refreshes
            if (keyCode == 116) { refreshProcessList(); updateDisplay(); updateStatusBar(); }
            return;
        }
        
        // Processes tab (s_currentTab == 0)
        if (keyCode == 38) { // Up
            if (s_selectedIndex > 0) {
                s_selectedIndex--;
                if (s_selectedIndex < s_scrollOffset) {
                    s_scrollOffset = s_selectedIndex;
                }
                updateDisplay();
            }
        }
        else if (keyCode == 40) { // Down
            if (s_selectedIndex < (int)s_processes.size() - 1) {
                s_selectedIndex++;
                if (s_selectedIndex >= s_scrollOffset + 12) {
                    s_scrollOffset = s_selectedIndex - 11;
                }
                updateDisplay();
            }
        }
        else if (keyCode == 46 || keyCode == 69 || keyCode == 101) { // Del/E
            endSelectedProcess();
        }
        else if (keyCode == 116 || keyCode == 82 || keyCode == 114) { // F5/R
            refreshProcessList();
            updateDisplay();
            updateStatusBar();
        }
        else if (keyCode == 33) { // Page Up
            if (s_scrollOffset > 0) {
                s_scrollOffset -= 10;
                if (s_scrollOffset < 0) s_scrollOffset = 0;
                updateDisplay();
            }
        }
        else if (keyCode == 34) { // Page Down
            int maxScroll = (int)s_processes.size() - 12;
            if (maxScroll < 0) maxScroll = 0;
            s_scrollOffset += 10;
            if (s_scrollOffset > maxScroll) s_scrollOffset = maxScroll;
            updateDisplay();
        }
    }
    
    void TaskManager::updateHeader() {
        const char* kGuiChanIn = "gui.input";
        const MemorySnapshot& mem = s_snapshot.memory;
        
        // System stats header
        {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream oss;
            oss << s_windowId << "|System Monitor";
            std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        }
        
        // Memory usage
        {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream oss;
            oss << s_windowId << "|Memory: " << formatMemory(mem.usedBytes);
            if (mem.totalAvailable) {
                oss << " / " << formatMemory(mem.totalBytes) << " (allocator heap total)";
            } else {
                oss << " / N/A";
            }
            oss << " (Peak: " << formatMemory(mem.peakBytes) << ")";
            std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        }
        
        // Tasks executed
        {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream oss;
            oss << s_windowId << "|Tasks Executed: " << s_tasksExecuted;
            std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        }
        
        // Column headers
        {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream oss;
            oss << s_windowId << "|   PID  Name              Status       CPU     Memory       Disk     Net";
            std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        }
    }
    
    void TaskManager::updateDisplay() {
        const char* kGuiChanIn = "gui.input";
        
        // Clear previous text by sending a clear-texts message
        {
            ipc::Message clr;
            clr.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream oss;
            oss << s_windowId << "|\x01CLEAR";
            auto payload = oss.str();
            clr.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(clr), false);
        }
        
        // Tab header line
        {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream oss;
            oss << s_windowId << "|";
            const char* tabNames[] = { "Processes", "Performance", "Tombstoned", "Memory Details" };
            for (int t = 0; t < kTabCount; t++) {
                if (t == s_currentTab) oss << "[" << tabNames[t] << "]";
                else oss << " " << tabNames[t] << " ";
                if (t < kTabCount - 1) oss << "  ";
            }
            auto payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        }
        
        if (s_currentTab == 0) {
            // Processes tab
            updateHeader();
            
            int visibleCount = 12;
            int startIndex = s_scrollOffset;
            int endIndex = std::min((int)s_snapshot.processes.size(), startIndex + visibleCount);
            
            for (int i = startIndex; i < endIndex; i++) {
                const ProcessSnapshot& proc = s_snapshot.processes[i];
                
                ipc::Message msg;
                msg.type = (uint32_t)MsgType::MT_DrawText;
                
                std::ostringstream oss;
                oss << s_windowId << "|";
                
                if (i == s_selectedIndex) oss << "> ";
                else oss << "  ";
                
                oss << std::setw(5) << std::right << proc.pid << " ";
                
                std::string name = proc.displayName;
                if (name.length() > 18) name = name.substr(0, 15) + "...";
                oss << std::setw(18) << std::left << name << " ";
                
                std::string status = proc.running ? "Running" : ("Stopped:" + std::to_string(proc.exitCode));
                if (status.length() > 12) status = status.substr(0, 12);
                oss << std::setw(12) << std::left << status << " ";
                oss << std::setw(8) << std::left << (proc.cpuPctAvailable ? std::to_string(proc.cpuPct) + "%" : "N/A") << " ";
                oss << std::setw(12) << std::left << formatMemory(proc.memoryBytes) << " ";
                oss << std::setw(8) << std::left << (proc.diskPctAvailable ? std::to_string(proc.diskPct) + "%" : "N/A") << " ";
                oss << std::setw(8) << std::left << (proc.networkPctAvailable ? std::to_string(proc.networkPct) + "%" : "N/A");
                
                auto payload = oss.str();
                msg.data.assign(payload.begin(), payload.end());
                ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
            }
            
            updateStatusBar();
        } else if (s_currentTab == 1) {
            updatePerformanceTab();
        } else if (s_currentTab == 2) {
            updateTombstonedTab();
        } else if (s_currentTab == 3) {
            updateMemoryDetailsTab();
        }
    }
    
    void TaskManager::updatePerformanceTab() {
        const char* kGuiChanIn = "gui.input";
        const PerformanceSnapshot& perf = s_snapshot.performance;
        const MemorySnapshot& mem = s_snapshot.memory;
        s_tasksExecuted = perf.schedulerTasksExecuted;
        s_cpuPct = perf.cpuAvailable ? perf.cpuPct : 0;
        s_memPct = perf.memoryAvailable ? perf.memoryPct : 0;
        s_diskPct = perf.diskAvailable ? perf.diskPct : 0;
        s_netPct = perf.networkAvailable ? perf.networkPct : 0;
        
        auto sendLine = [&](const std::string& text) {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            std::string payload = std::to_string(s_windowId) + "|" + text;
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        };
        
        sendLine("--- Performance ---");
        sendLine("");
        
        // Category labels (4 categories matching Legacy)
        const char* catLabels[] = { "CPU", "Memory", "Disk", "Network" };
        const bool catAvailable[] = { perf.cpuAvailable, perf.memoryAvailable, perf.diskAvailable, perf.networkAvailable };
        const int catValues[] = { perf.cpuPct, perf.memoryPct, perf.diskPct, perf.networkPct };
        
        // Navigation hint
        {
            std::ostringstream oss;
            oss << "Category: ";
            for (int c = 0; c < 4; c++) {
                if (c == s_perfCategoryIndex) oss << "[" << catLabels[c] << "]";
                else oss << " " << catLabels[c] << " ";
                if (c < 3) oss << "  ";
            }
            oss << "    (Left/Right to switch)";
            sendLine(oss.str());
        }
        sendLine("");
        
        // ASCII bar chart for selected category
        int val = catAvailable[s_perfCategoryIndex] ? catValues[s_perfCategoryIndex] : 0;
        {
            std::ostringstream oss;
            if (catAvailable[s_perfCategoryIndex]) {
                oss << catLabels[s_perfCategoryIndex] << ": " << val << "%";
            } else {
                oss << catLabels[s_perfCategoryIndex] << ": N/A";
            }
            sendLine(oss.str());
        }
        if (catAvailable[s_perfCategoryIndex]) {
            int filled = val * 40 / 100;
            std::string bar = "[";
            for (int b = 0; b < 40; b++) bar += (b < filled ? '#' : '.');
            bar += "]";
            sendLine(bar);
        } else {
            sendLine("[N/A]");
        }
        sendLine("");
        
        // Detail stats for selected category
        if (s_perfCategoryIndex == 0) {
            // CPU details
            sendLine("Utilization: N/A");
            sendLine("Processes: " + std::to_string(perf.processCount));
            sendLine("Windows: " + std::to_string(perf.windowCount));
            sendLine("Tasks Executed: " + std::to_string(s_tasksExecuted));
            sendLine("Machine time: " + formatUptime((uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
        } else if (s_perfCategoryIndex == 1) {
            // Memory details
            uint64_t avail = mem.freeBytes;
            sendLine("In Use: " + formatMemory(mem.usedBytes) + " (" + std::to_string(mem.heapUtilPctAvailable ? mem.heapUtilPct : 0) + "%)");
            sendLine("Available: " + (mem.totalAvailable ? formatMemory(avail) : std::string("N/A")));
            sendLine("Total: " + (mem.totalAvailable ? formatMemory(mem.totalBytes) + " (allocator heap)" : std::string("N/A")));
            sendLine("Peak: " + formatMemory(mem.peakBytes));
            if (mem.freeAllocRatioAvailable) {
                sendLine("Free/Alloc Ratio: " + std::to_string(mem.freeAllocRatioPct) + "%");
            } else {
                sendLine("Free/Alloc Ratio: N/A");
            }
            if (mem.topTagAvailable) {
                sendLine("Top Tag: " + mem.topTagName + " (" + formatMemory(mem.topTagBytes) + ")");
            }
            if (mem.topOwnerAvailable) {
                sendLine("Top Owner: PID " + std::to_string(mem.topOwnerPid) + " (" + formatMemory(mem.topOwnerBytes) + ")");
            }
        } else if (s_perfCategoryIndex == 2) {
            // Disk details
            sendLine("Active time: N/A");
            sendLine("Avg response time: N/A");
            sendLine("Read speed: N/A");
            sendLine("Write speed: N/A");
            sendLine("Real counter: unavailable");
        } else if (s_perfCategoryIndex == 3) {
            // Network details
            sendLine("Send: N/A");
            sendLine("Receive: N/A");
            sendLine("Sent bytes: N/A");
            sendLine("Received bytes: N/A");
            sendLine("Real counter: unavailable");
        }
        
        sendLine("");
        sendLine("Auto-refresh every 2s | F5=Refresh | Tab=Switch Tab");
    }
    
    void TaskManager::updateTombstonedTab() {
        const char* kGuiChanIn = "gui.input";
        
        auto sendLine = [&](const std::string& text) {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            std::string payload = std::to_string(s_windowId) + "|" + text;
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        };
        
        sendLine("--- Tombstoned Apps ---");
        sendLine("");
        sendLine("   PID  Name               Reason");
        
        int count = 0;
        for (size_t tombIdx = 0; tombIdx < s_snapshot.tombstoned.size(); ++tombIdx) {
            const TombstoneSnapshot& tomb = s_snapshot.tombstoned[tombIdx];
            {
                std::ostringstream oss;
                if ((int)tombIdx == s_selectedTombIndex) oss << "> ";
                else oss << "  ";
                oss << std::setw(5) << std::right << tomb.pid << "  "
                    << std::setw(18) << std::left << tomb.displayName << " "
                    << (tomb.reason.empty() ? "stopped" : tomb.reason);
                sendLine(oss.str());
                count++;
            }
        }
        
        if (count == 0) {
            sendLine("");
            sendLine("  No tombstoned apps.");
        }
        
        // Ensure selection is valid
        if (s_selectedTombIndex >= count) {
            s_selectedTombIndex = count - 1;
        }
        
        sendLine("");
        sendLine(std::to_string(count) + " tombstoned | Up/Down=Select | R=Restore | Del/E=End | Tab=Switch");
    }
    
    void TaskManager::updateStatusBar() {
        const char* kGuiChanIn = "gui.input";
        
        ipc::Message msg;
        msg.type = (uint32_t)MsgType::MT_DrawText;
        
        std::ostringstream oss;
        oss << s_windowId << "|" << s_snapshot.performance.processCount << " processes";
        
        if (s_selectedIndex >= 0 && s_selectedIndex < (int)s_snapshot.processes.size()) {
            const ProcessSnapshot& proc = s_snapshot.processes[s_selectedIndex];
            oss << " | Selected: PID " << proc.pid;
        }
        
        oss << " | F5=Refresh | Del/E=End";
        
        std::string payload = oss.str();
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
    }
    
    // --- Tombstoned management (matching Legacy TaskManager.cs) ---
    
    int TaskManager::countTombstoned() {
        return (int)s_snapshot.tombstoned.size();
    }
    
    void TaskManager::restoreTombstoned() {
        if (s_selectedTombIndex < 0) return;
        if (s_selectedTombIndex >= (int)s_snapshot.tombstoned.size()) return;
        const TombstoneSnapshot& tomb = s_snapshot.tombstoned[s_selectedTombIndex];
        Logger::write(LogLevel::Info, std::string("TaskManager: Restoring tombstoned PID ") + std::to_string(tomb.pid));
        Logger::write(LogLevel::Info, "TaskManager: Process restore requested (server-side restart)");
        refreshProcessList();
        updateDisplay();
        updateStatusBar();
        return;
    }
    
    void TaskManager::endTombstoned() {
        if (s_selectedTombIndex < 0) return;
        if (s_selectedTombIndex >= (int)s_snapshot.tombstoned.size()) return;
        const TombstoneSnapshot& tomb = s_snapshot.tombstoned[s_selectedTombIndex];
        Logger::write(LogLevel::Info, std::string("TaskManager: Ending tombstoned PID ") + std::to_string(tomb.pid));
        ProcessTable::terminate(tomb.pid);
        s_selectedTombIndex = -1;
        refreshProcessList();
        updateDisplay();
        updateStatusBar();
        return;
    }
    
    // --- Memory Details tab (matching Legacy TaskManager.cs DrawMemoryDetails) ---
    
    void TaskManager::updateMemoryDetailsTab() {
        const char* kGuiChanIn = "gui.input";
        
        auto sendLine = [&](const std::string& text) {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            std::string payload = std::to_string(s_windowId) + "|" + text;
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        };
        
        // Update memory stats
        auto now = std::chrono::steady_clock::now();
        uint64_t nowTicks = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        if (nowTicks - s_lastMemDetailUpdate >= 1000) {
            s_lastMemDetailUpdate = nowTicks;

            refreshProcessList();

            // Track cumulative allocated/freed using snapshot data
            s_usedMemory = s_snapshot.memory.usedBytes;
            s_peakMemory = s_snapshot.memory.peakBytes;
            s_cumulativeFreed = s_snapshot.memory.totalFreedBytes;
            s_cumulativeAllocated = s_usedMemory + s_cumulativeFreed;

            // Leak detection (matching Legacy)
            int64_t netGrowth = (int64_t)s_cumulativeAllocated - (int64_t)s_cumulativeFreed;
            s_leakHistory.push_back((uint64_t)netGrowth);
            
            if ((int)s_leakHistory.size() > kLeakHistoryMax) {
                s_leakHistory.erase(s_leakHistory.begin());
            }
            
            if (s_leakHistory.size() >= 2) {
                uint64_t prev = s_leakHistory[s_leakHistory.size() - 2];
                uint64_t curr = s_leakHistory[s_leakHistory.size() - 1];
                
                if (curr > prev) {
                    s_leakGrowthCounter++;
                    if (s_leakGrowthCounter >= kLeakThreshold) {
                        s_leakExists = true;
                    }
                } else {
                    s_leakGrowthCounter = 0;
                    if ((int)s_leakHistory.size() >= kLeakHistoryMax) {
                        uint64_t first = s_leakHistory[0];
                        uint64_t last = s_leakHistory[s_leakHistory.size() - 1];
                        if (last <= first + 100) {
                            s_leakExists = false;
                        }
                    }
                }
            }
        }
        
        sendLine("=== Memory Allocator Details ===");
        sendLine("");
        
        // Allocated / Freed
        sendLine("Allocated (cumulative):  " + formatMemory(s_cumulativeAllocated));
        sendLine("Freed (cumulative):      " + formatMemory(s_cumulativeFreed));
        sendLine("Currently In Use:        " + formatMemory(s_usedMemory));
        
        int64_t netGrowth = (int64_t)s_cumulativeAllocated - (int64_t)s_cumulativeFreed;
        sendLine("Net Growth (Alloc-Free): " + std::to_string(netGrowth) + " bytes");
        sendLine("");
        
        // Leak detection
        std::string leakStr = s_leakExists ? "*** TRUE ***" : "FALSE";
        sendLine("Leak Exists: " + leakStr);
        sendLine("");
        
        // Free/Alloc ratio
        if (s_cumulativeAllocated > 0) {
            int freePct = (int)(s_cumulativeFreed * 100 / s_cumulativeAllocated);
            sendLine("Free/Alloc Ratio: " + std::to_string(freePct) + "%");
        } else {
            sendLine("Free/Alloc Ratio: N/A");
        }
        sendLine("");
        
        // Heap stats
        sendLine("=== Heap Allocator ===");
        uint64_t heapTotal = s_snapshot.memory.totalAvailable ? s_snapshot.memory.totalBytes : 0;
        uint64_t heapUsed = s_snapshot.memory.usedBytes;
        uint64_t heapFree = heapTotal > heapUsed ? heapTotal - heapUsed : 0;
        int heapUtilPct = heapTotal > 0 ? (int)(heapUsed * 100 / heapTotal) : 0;
        
        sendLine("Total Heap Size:   " + (heapTotal > 0 ? formatMemory(heapTotal) + " (allocator heap)" : std::string("N/A")));
        sendLine("Heap In Use:       " + formatMemory(heapUsed));
        sendLine("Heap Free:         " + (heapTotal > 0 ? formatMemory(heapFree) : std::string("N/A")));
        sendLine("Heap Utilization:  " + (heapTotal > 0 ? std::to_string(heapUtilPct) + "%" : std::string("N/A")));
        sendLine("");
        sendLine("Peak Memory:       " + formatMemory(s_peakMemory));
        if (s_snapshot.memory.topTagAvailable) {
            sendLine("Top Tag:           " + s_snapshot.memory.topTagName + " (" + formatMemory(s_snapshot.memory.topTagBytes) + ")");
        }
        if (s_snapshot.memory.topOwnerAvailable) {
            sendLine("Top Owner:         PID " + std::to_string(s_snapshot.memory.topOwnerPid) + " (" + formatMemory(s_snapshot.memory.topOwnerBytes) + ")");
        }
        sendLine("");
        sendLine("F5=Refresh | Tab=Switch Tab");
    }
    
    // --- Helper functions ---

    std::string TaskManager::formatMemory(uint64_t bytes) {
        if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
            double gb = (double)bytes / (1024.0 * 1024.0 * 1024.0);
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << gb << " GB";
            return oss.str();
        } else if (bytes >= 1024ULL * 1024ULL) {
            double mb = (double)bytes / (1024.0 * 1024.0);
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << mb << " MB";
            return oss.str();
        } else if (bytes >= 1024ULL) {
            return std::to_string(bytes / 1024) + " KB";
        } else {
            return std::to_string(bytes) + " B";
        }
    }
    
    std::string TaskManager::formatUptime(uint64_t ticks) {
        uint64_t sec = ticks / 1000;
        uint64_t min = sec / 60;
        uint64_t hrs = min / 60;
        uint64_t days = hrs / 24;
        sec %= 60;
        min %= 60;
        hrs %= 24;
        
        std::ostringstream oss;
        if (days > 0) oss << days << "d ";
        oss << std::setfill('0') << std::setw(2) << hrs << ":"
            << std::setw(2) << min << ":"
            << std::setw(2) << sec;
        return oss.str();
    }
    
}} // namespace gxos::apps
