#include "task_manager.h"
#include "gui_protocol.h"
#include "logger.h"
#include "allocator.h"
#include "scheduler.h"
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <chrono>
#include <cmath>

namespace gxos { namespace apps {
    
    using namespace gxos::gui;
    
    // Static member initialization
    uint64_t TaskManager::s_windowId = 0;
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
    
    // Synthetic disk/network counters
    int TaskManager::s_diskActivePct = 0;
    int TaskManager::s_diskReadKBps = 0;
    int TaskManager::s_diskWriteKBps = 0;
    int TaskManager::s_diskRespMs = 0;
    int TaskManager::s_netSendKBps = 0;
    int TaskManager::s_netRecvKBps = 0;
    uint64_t TaskManager::s_bytesSent = 0;
    uint64_t TaskManager::s_bytesRecv = 0;
    
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
    
    int TaskManager::main(int argc, char** argv) {
        try {
            Logger::write(LogLevel::Info, "TaskManager starting...");
            
            // Initialize state
            s_windowId = 0;
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
            s_diskActivePct = 0;
            s_diskReadKBps = 0;
            s_diskWriteKBps = 0;
            s_diskRespMs = 0;
            s_netSendKBps = 0;
            s_netRecvKBps = 0;
            s_bytesSent = 0;
            s_bytesRecv = 0;
            s_cumulativeAllocated = 0;
            s_cumulativeFreed = 0;
            s_lastMemDetailUpdate = 0;
            s_leakExists = false;
            s_leakGrowthCounter = 0;
            s_leakHistory.clear();
            
            // Get initial system stats
            s_totalMemory = 512 * 1024 * 1024; // 512MB (from platform info)
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
        s_processes.clear();
        
        // Get list of process IDs
        auto pidList = ProcessTable::list();
        
        // Get memory allocation info per PID
        auto memList = Allocator::listPidBytes();
        
        // Build process info list
        for (uint64_t pid : pidList) {
            ProcessInfo info;
            info.pid = pid;
            
            // Get process status
            bool running;
            int exitCode;
            if (ProcessTable::getStatus(pid, running, exitCode)) {
                info.running = running;
                info.exitCode = exitCode;
            } else {
                info.running = false;
                info.exitCode = 0;
            }
            
            // Get process name (we'll use pid as name for now since we don't store names)
            info.name = "Process-" + std::to_string(pid);
            
            // Get memory usage
            info.memoryBytes = 0;
            for (const auto& memPair : memList) {
                if (memPair.first == pid) {
                    info.memoryBytes = memPair.second;
                    break;
                }
            }
            
            s_processes.push_back(info);
        }
        
        // Sort by PID
        std::sort(s_processes.begin(), s_processes.end(), 
            [](const ProcessInfo& a, const ProcessInfo& b) {
                return a.pid < b.pid;
            });
        
        // Update system stats
        s_usedMemory = Allocator::bytesInUse();
        s_peakMemory = Allocator::peakBytes();
        s_tasksExecuted = Scheduler::tasksExecuted();
        
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
            oss << s_windowId << "|Memory: " << (s_usedMemory / 1024) << " KB / " 
                << (s_totalMemory / 1024) << " KB (Peak: " << (s_peakMemory / 1024) << " KB)";
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
            oss << s_windowId << "|   PID  Name              Status    Memory";
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
            int endIndex = std::min((int)s_processes.size(), startIndex + visibleCount);
            
            for (int i = startIndex; i < endIndex; i++) {
                const ProcessInfo& proc = s_processes[i];
                
                ipc::Message msg;
                msg.type = (uint32_t)MsgType::MT_DrawText;
                
                std::ostringstream oss;
                oss << s_windowId << "|";
                
                if (i == s_selectedIndex) oss << "> ";
                else oss << "  ";
                
                oss << std::setw(5) << std::right << proc.pid << " ";
                
                std::string name = proc.name;
                if (name.length() > 18) name = name.substr(0, 15) + "...";
                oss << std::setw(18) << std::left << name << " ";
                
                std::string status = proc.running ? "Running" : ("Stopped:" + std::to_string(proc.exitCode));
                if (status.length() > 10) status = status.substr(0, 10);
                oss << std::setw(10) << std::left << status << " ";
                
                oss << formatMemory(proc.memoryBytes);
                
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
        
        // Compute live percentages
        s_usedMemory = Allocator::bytesInUse();
        s_peakMemory = Allocator::peakBytes();
        uint64_t totalMem = s_totalMemory > 0 ? s_totalMemory : 1;
        s_memPct = (int)(s_usedMemory * 100 / totalMem);
        if (s_memPct > 100) s_memPct = 100;
        
        // CPU approximation from scheduler
        s_tasksExecuted = Scheduler::tasksExecuted();
        s_cpuPct = (int)(s_tasksExecuted % 100);
        
        // Synthetic disk/network counters (matching Legacy wave animation)
        auto now = std::chrono::steady_clock::now();
        uint64_t ticks = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        s_diskPct = wavePct(ticks, 24000);
        s_diskActivePct = wavePct(ticks + 6000, 30000);
        s_diskReadKBps = s_diskPct * 4;
        s_diskWriteKBps = s_diskActivePct * 3 / 2;
        s_diskRespMs = 1 + s_diskActivePct / 10;
        
        s_netPct = wavePct(ticks + 12000, 28000);
        s_netSendKBps = s_netPct * 2;
        s_netRecvKBps = (100 - s_netPct) * 2;
        s_bytesSent += (uint64_t)(s_netSendKBps * 1024 / 10);
        s_bytesRecv += (uint64_t)(s_netRecvKBps * 1024 / 10);
        
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
        int catValues[] = { s_cpuPct, s_memPct, s_diskPct, s_netPct };
        
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
        int val = catValues[s_perfCategoryIndex];
        {
            std::ostringstream oss;
            oss << catLabels[s_perfCategoryIndex] << ": " << val << "%";
            sendLine(oss.str());
        }
        {
            int filled = val * 40 / 100;
            std::string bar = "[";
            for (int b = 0; b < 40; b++) bar += (b < filled ? '#' : '.');
            bar += "]";
            sendLine(bar);
        }
        sendLine("");
        
        // Detail stats for selected category
        if (s_perfCategoryIndex == 0) {
            // CPU details
            sendLine("Utilization: " + std::to_string(s_cpuPct) + "%");
            sendLine("Processes: " + std::to_string(s_processes.size()));
            sendLine("Tasks Executed: " + std::to_string(s_tasksExecuted));
            sendLine("Machine time: " + formatUptime(ticks));
        } else if (s_perfCategoryIndex == 1) {
            // Memory details
            uint64_t avail = s_totalMemory > s_usedMemory ? s_totalMemory - s_usedMemory : 0;
            sendLine("In Use: " + formatMemory(s_usedMemory) + " (" + std::to_string(s_memPct) + "%)");
            sendLine("Available: " + formatMemory(avail));
            sendLine("Total: " + formatMemory(s_totalMemory));
            sendLine("Peak: " + formatMemory(s_peakMemory));
        } else if (s_perfCategoryIndex == 2) {
            // Disk details (synthetic, matching Legacy)
            sendLine("Active time: " + std::to_string(s_diskActivePct) + "%");
            sendLine("Avg response time: " + std::to_string(s_diskRespMs) + " ms");
            sendLine("Read speed: " + formatTransferRate(s_diskReadKBps));
            sendLine("Write speed: " + formatTransferRate(s_diskWriteKBps));
        } else if (s_perfCategoryIndex == 3) {
            // Network details (synthetic, matching Legacy)
            sendLine("Send: " + formatTransferRate(s_netSendKBps));
            sendLine("Receive: " + formatTransferRate(s_netRecvKBps));
            sendLine("Sent bytes: " + std::to_string(s_bytesSent));
            sendLine("Received bytes: " + std::to_string(s_bytesRecv));
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
        sendLine("   PID  Name               Status");
        
        int count = 0;
        int tombIdx = 0;
        for (const auto& proc : s_processes) {
            if (!proc.running) {
                std::ostringstream oss;
                if (tombIdx == s_selectedTombIndex) oss << "> ";
                else oss << "  ";
                oss << std::setw(5) << std::right << proc.pid << "  "
                    << std::setw(18) << std::left << proc.name << " "
                    << "Stopped:" << proc.exitCode;
                sendLine(oss.str());
                count++;
                tombIdx++;
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
        oss << s_windowId << "|" << s_processes.size() << " processes";
        
        if (s_selectedIndex >= 0 && s_selectedIndex < (int)s_processes.size()) {
            const ProcessInfo& proc = s_processes[s_selectedIndex];
            oss << " | Selected: PID " << proc.pid;
        }
        
        oss << " | F5=Refresh | Del/E=End";
        
        std::string payload = oss.str();
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
    }
    
    // --- Tombstoned management (matching Legacy TaskManager.cs) ---
    
    int TaskManager::countTombstoned() {
        int count = 0;
        for (const auto& proc : s_processes) {
            if (!proc.running) count++;
        }
        return count;
    }
    
    void TaskManager::restoreTombstoned() {
        if (s_selectedTombIndex < 0) return;
        
        // Find the Nth tombstoned process
        int tombIdx = 0;
        for (const auto& proc : s_processes) {
            if (!proc.running) {
                if (tombIdx == s_selectedTombIndex) {
                    Logger::write(LogLevel::Info, std::string("TaskManager: Restoring tombstoned PID ") + std::to_string(proc.pid));
                    // Attempt to restart/restore the process
                    // In the server model, "restore" means re-launching the process
                    // For now, log the action
                    Logger::write(LogLevel::Info, "TaskManager: Process restore requested (server-side restart)");
                    refreshProcessList();
                    updateDisplay();
                    updateStatusBar();
                    return;
                }
                tombIdx++;
            }
        }
    }
    
    void TaskManager::endTombstoned() {
        if (s_selectedTombIndex < 0) return;
        
        int tombIdx = 0;
        for (const auto& proc : s_processes) {
            if (!proc.running) {
                if (tombIdx == s_selectedTombIndex) {
                    Logger::write(LogLevel::Info, std::string("TaskManager: Ending tombstoned PID ") + std::to_string(proc.pid));
                    ProcessTable::terminate(proc.pid);
                    s_selectedTombIndex = -1;
                    refreshProcessList();
                    updateDisplay();
                    updateStatusBar();
                    return;
                }
                tombIdx++;
            }
        }
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
            
            // Track cumulative allocated/freed
            s_usedMemory = Allocator::bytesInUse();
            s_peakMemory = Allocator::peakBytes();
            s_cumulativeFreed = Allocator::totalFreed();
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
        uint64_t heapTotal = Allocator::totalSize();
        uint64_t heapUsed = Allocator::bytesInUse();
        uint64_t heapFree = heapTotal > heapUsed ? heapTotal - heapUsed : 0;
        int heapUtilPct = heapTotal > 0 ? (int)(heapUsed * 100 / heapTotal) : 0;
        
        sendLine("Total Heap Size:   " + formatMemory(heapTotal));
        sendLine("Heap In Use:       " + formatMemory(heapUsed));
        sendLine("Heap Free:         " + formatMemory(heapFree));
        sendLine("Heap Utilization:  " + std::to_string(heapUtilPct) + "%");
        sendLine("");
        sendLine("Peak Memory:       " + formatMemory(s_peakMemory));
        sendLine("");
        sendLine("F5=Refresh | Tab=Switch Tab");
    }
    
    // --- Helper functions ---
    
    int TaskManager::wavePct(uint64_t ticks, int period) {
        int t = (int)(ticks % (uint64_t)period);
        int up = period / 2;
        if (t < up) return t * 100 / up;
        else return (period - t) * 100 / up;
    }
    
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
    
    std::string TaskManager::formatTransferRate(int kbps) {
        if (kbps >= 1024) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << ((double)kbps / 1024.0) << " MB/s";
            return oss.str();
        }
        return std::to_string(kbps) + " KB/s";
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
