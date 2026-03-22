#include "task_manager.h"
#include "gui_protocol.h"
#include "logger.h"
#include "allocator.h"
#include "scheduler.h"
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <chrono>

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
    int TaskManager::s_cpuPct = 0;
    int TaskManager::s_memPct = 0;
    int TaskManager::s_perfCategoryIndex = 0;
    
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
            s_cpuPct = 0;
            s_memPct = 0;
            s_perfCategoryIndex = 0;
            
            // Get initial system stats
            s_totalMemory = 512 * 1024 * 1024; // 512MB (from platform info)
            refreshProcessList();
            
            // Subscribe to IPC channels
            const char* kGuiChanIn = "gui.input";
            const char* kGuiChanOut = "gui.output";
            ipc::Bus::ensure(kGuiChanIn);
            ipc::Bus::ensure(kGuiChanOut);
            
            // Create window (640x480)
            ipc::Message createMsg;
            createMsg.type = (uint32_t)MsgType::MT_Create;
            std::ostringstream oss;
            oss << "Task Manager|640|480";
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
                                    
                                    // Tab buttons (matching Legacy TaskManager tabs)
                                    addButton(10, 4, 28, 120, 22, "Processes");
                                    addButton(11, 128, 28, 120, 22, "Performance");
                                    addButton(12, 252, 28, 120, 22, "Tombstoned");
                                    
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
            // Performance tab: Left/Right to change category
            if (keyCode == 37) { // Left
                if (s_perfCategoryIndex > 0) s_perfCategoryIndex--;
                updateDisplay();
                return;
            }
            if (keyCode == 39) { // Right
                if (s_perfCategoryIndex < 1) s_perfCategoryIndex++;
                updateDisplay();
                return;
            }
            // F5 still refreshes
            if (keyCode == 116) { refreshProcessList(); updateDisplay(); updateStatusBar(); }
            return;
        }
        
        if (s_currentTab == 2) {
            // Tombstoned tab: F5 refreshes
            if (keyCode == 116) { refreshProcessList(); updateDisplay(); updateStatusBar(); }
            return;
        }
        
        // Processes tab (s_currentTab == 0)
        // Up arrow - move selection up
        if (keyCode == 38) {
            if (s_selectedIndex > 0) {
                s_selectedIndex--;
                
                // Scroll up if needed
                if (s_selectedIndex < s_scrollOffset) {
                    s_scrollOffset = s_selectedIndex;
                }
                
                updateDisplay();
            }
        }
        // Down arrow - move selection down
        else if (keyCode == 40) {
            if (s_selectedIndex < (int)s_processes.size() - 1) {
                s_selectedIndex++;
                
                // Scroll down if needed (show 12 items at a time)
                if (s_selectedIndex >= s_scrollOffset + 12) {
                    s_scrollOffset = s_selectedIndex - 11;
                }
                
                updateDisplay();
            }
        }
        // Delete or E key - end selected process
        else if (keyCode == 46 || keyCode == 69 || keyCode == 101) {
            endSelectedProcess();
        }
        // F5 or R key - refresh
        else if (keyCode == 116 || keyCode == 82 || keyCode == 114) {
            refreshProcessList();
            updateDisplay();
            updateStatusBar();
        }
        // Page Up - scroll up
        else if (keyCode == 33) {
            if (s_scrollOffset > 0) {
                s_scrollOffset -= 10;
                if (s_scrollOffset < 0) s_scrollOffset = 0;
                updateDisplay();
            }
        }
        // Page Down - scroll down
        else if (keyCode == 34) {
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
            const char* tabNames[] = { "Processes", "Performance", "Tombstoned" };
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
                
                oss << (proc.memoryBytes / 1024) << " KB";
                
                auto payload = oss.str();
                msg.data.assign(payload.begin(), payload.end());
                ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
            }
            
            updateStatusBar();
        } else if (s_currentTab == 1) {
            updatePerformanceTab();
        } else if (s_currentTab == 2) {
            updateTombstonedTab();
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
        
        // CPU approximation from scheduler tasks
        s_tasksExecuted = Scheduler::tasksExecuted();
        s_cpuPct = (int)(s_tasksExecuted % 100); // synthetic
        
        auto sendLine = [&](const std::string& text) {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            std::string payload = std::to_string(s_windowId) + "|" + text;
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        };
        
        // Performance overview
        sendLine("--- Performance ---");
        sendLine("");
        
        // Category labels (matching Legacy: CPU, Memory)
        const char* catLabels[] = { "CPU", "Memory" };
        int catValues[] = { s_cpuPct, s_memPct };
        
        // Navigation hint
        {
            std::ostringstream oss;
            oss << "Category: ";
            for (int c = 0; c < 2; c++) {
                if (c == s_perfCategoryIndex) oss << "[" << catLabels[c] << "]";
                else oss << " " << catLabels[c] << " ";
                if (c < 1) oss << "  ";
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
            // 40-char bar
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
        } else {
            // Memory details
            std::ostringstream oss;
            oss << "In Use: " << (s_usedMemory / 1024) << " KB / " << (s_totalMemory / 1024) << " KB";
            sendLine(oss.str());
            sendLine("Peak: " + std::to_string(s_peakMemory / 1024) + " KB");
            sendLine("Utilization: " + std::to_string(s_memPct) + "%");
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
        for (const auto& proc : s_processes) {
            if (!proc.running) {
                std::ostringstream oss;
                oss << std::setw(5) << std::right << proc.pid << "  "
                    << std::setw(18) << std::left << proc.name << " "
                    << "Stopped:" << proc.exitCode;
                sendLine(oss.str());
                count++;
            }
        }
        
        if (count == 0) {
            sendLine("");
            sendLine("  No tombstoned apps.");
        }
        
        sendLine("");
        sendLine(std::to_string(count) + " tombstoned | F5=Refresh | Tab=Switch Tab");
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
    
}} // namespace gxos::apps
