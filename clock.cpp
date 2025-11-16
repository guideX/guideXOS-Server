#include "clock.h"
#include "gui_protocol.h"
#include "logger.h"
#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>

namespace gxos { namespace apps {
    
    using namespace gxos::gui;
    
    // Static member initialization
    uint64_t Clock::s_windowId = 0;
    uint64_t Clock::s_lastUpdateTicks = 0;
    
    uint64_t Clock::Launch() {
        ProcessSpec spec{"clock", Clock::main};
        return ProcessTable::spawn(spec, {"clock"});
    }
    
    int Clock::main(int argc, char** argv) {
        try {
            Logger::write(LogLevel::Info, "Clock starting...");
            
            // Initialize state
            s_windowId = 0;
            s_lastUpdateTicks = 0;
            
            // Subscribe to IPC channels
            const char* kGuiChanIn = "gui.input";
            const char* kGuiChanOut = "gui.output";
            ipc::Bus::ensure(kGuiChanIn);
            ipc::Bus::ensure(kGuiChanOut);
            
            // Create window (280x120 - compact size)
            ipc::Message createMsg;
            createMsg.type = (uint32_t)MsgType::MT_Create;
            std::ostringstream oss;
            oss << "Clock|280|120";
            std::string payload = oss.str();
            createMsg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(createMsg), false);
            
            // Main event loop
            bool running = true;
            while (running) {
                ipc::Message msg;
                if (ipc::Bus::pop(kGuiChanOut, msg, 50)) {  // 50ms timeout for responsiveness
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
                                    Logger::write(LogLevel::Info, std::string("Clock window created: ") + std::to_string(s_windowId));
                                    
                                    // Draw initial display
                                    updateDisplay();
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Clock: Failed to parse window ID: ") + e.what());
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
                                        Logger::write(LogLevel::Info, "Clock closing...");
                                        running = false;
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("Clock: Failed to parse close ID: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        default:
                            break;
                    }
                }
                
                // Update display every second
                auto now = std::chrono::steady_clock::now();
                uint64_t nowTicks = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                
                if (s_windowId != 0 && (nowTicks - s_lastUpdateTicks >= 1000)) {
                    updateDisplay();
                    s_lastUpdateTicks = nowTicks;
                }
            }
            
            Logger::write(LogLevel::Info, "Clock stopped");
            return 0;
            
        } catch (const std::exception& e) {
            Logger::write(LogLevel::Error, std::string("Clock EXCEPTION: ") + e.what());
            return -1;
        } catch (...) {
            Logger::write(LogLevel::Error, "Clock UNKNOWN EXCEPTION");
            return -1;
        }
    }
    
    void Clock::updateDisplay() {
        const char* kGuiChanIn = "gui.input";
        
        // Get current time and date
        std::string timeStr = getCurrentTime();
        std::string dateStr = getCurrentDate();
        
        // Display time (large)
        {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream oss;
            oss << s_windowId << "|" << timeStr;
            std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        }
        
        // Display date
        {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_DrawText;
            std::ostringstream oss;
            oss << s_windowId << "|" << dateStr;
            std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        }
        
        // Update window title
        {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_SetTitle;
            std::ostringstream oss;
            oss << s_windowId << "|Clock - " << timeStr;
            std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(msg), false);
        }
    }
    
    std::string Clock::getCurrentTime() {
        // Get current time
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm{};
        
#ifdef _WIN32
        localtime_s(&local_tm, &now_c);
#else
        std::tm* tmp = std::localtime(&now_c);
        if (tmp) local_tm = *tmp;
#endif
        
        // Format as HH:MM:SS
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << local_tm.tm_hour << ":"
            << std::setfill('0') << std::setw(2) << local_tm.tm_min << ":"
            << std::setfill('0') << std::setw(2) << local_tm.tm_sec;
        
        return oss.str();
    }
    
    std::string Clock::getCurrentDate() {
        // Get current date
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm{};
        
#ifdef _WIN32
        localtime_s(&local_tm, &now_c);
#else
        std::tm* tmp = std::localtime(&now_c);
        if (tmp) local_tm = *tmp;
#endif
        
        // Format as Day, Month DD, YYYY
        const char* weekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        const char* months[] = {"January", "February", "March", "April", "May", "June", 
                               "July", "August", "September", "October", "November", "December"};
        
        std::ostringstream oss;
        oss << weekdays[local_tm.tm_wday] << ", " 
            << months[local_tm.tm_mon] << " " 
            << local_tm.tm_mday << ", " 
            << (1900 + local_tm.tm_year);
        
        return oss.str();
    }
    
}} // namespace gxos::apps
