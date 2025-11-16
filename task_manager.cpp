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
        
        // Update header with latest stats
        updateHeader();
        
        // Calculate visible processes (12 visible items)
        int visibleCount = 12;
        int startIndex = s_scrollOffset;
        int endIndex = std::min((int)s_processes.size(), startIndex + visibleCount);
        
        // Draw process list
        for (int i = startIndex; i < endIndex; i++) {
            const ProcessInfo& proc = s_processes[i];
            
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            
            std::ostringstream oss;
            oss << s_windowId << "|";
            
            // Selection indicator
            if (i == s_selectedIndex) {
                oss << "> ";
            } else {
                oss << "  ";
            }
            
            // PID (5 chars)
            oss << std::setw(5) << std::right << proc.pid << " ";
            
            // Name (18 chars)
            std::string name = proc.name;
            if (name.length() > 18) {
                name = name.substr(0, 15) + "...";
            }
            oss << std::setw(18) << std::left << name << " ";
            
            // Status (10 chars)
            std::string status = proc.running ? "Running" : ("Stopped:" + std::to_string(proc.exitCode));
            if (status.length() > 10) {
                status = status.substr(0, 10);
            }
            oss << std::setw(10) << std::left << status << " ";
            
            // Memory (in KB)
            oss << (proc.memoryBytes / 1024) << " KB";
            
            std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        }
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
