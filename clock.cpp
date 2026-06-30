#include "clock.h"
#include "desktop_theme.h"
#include "gui_protocol.h"
#include "logger.h"
#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>

namespace gxos { namespace apps {
    
    using namespace gxos::gui;

    namespace {
        constexpr int kClockWindowWidth = 280;
        constexpr int kClockWindowHeight = 120;
        constexpr int kClockTimeY = 0;
        constexpr int kClockDateY = 18;

        uint32_t packRgb(int r, int g, int b)
        {
            return 0xFF000000u |
                (static_cast<uint32_t>(r & 0xFF) << 16) |
                (static_cast<uint32_t>(g & 0xFF) << 8) |
                static_cast<uint32_t>(b & 0xFF);
        }

        uint32_t blendColor(uint32_t baseColor, uint32_t overlayColor, int overlayPercent)
        {
            if (overlayPercent <= 0) {
                return baseColor;
            }
            if (overlayPercent >= 100) {
                return overlayColor;
            }

            const int baseR = static_cast<int>((baseColor >> 16) & 0xFF);
            const int baseG = static_cast<int>((baseColor >> 8) & 0xFF);
            const int baseB = static_cast<int>(baseColor & 0xFF);
            const int overR = static_cast<int>((overlayColor >> 16) & 0xFF);
            const int overG = static_cast<int>((overlayColor >> 8) & 0xFF);
            const int overB = static_cast<int>(overlayColor & 0xFF);
            const int keepPercent = 100 - overlayPercent;

            return packRgb(
                (baseR * keepPercent + overR * overlayPercent) / 100,
                (baseG * keepPercent + overG * overlayPercent) / 100,
                (baseB * keepPercent + overB * overlayPercent) / 100);
        }

        bool isSciFiThemeActive()
        {
            return GetCurrentDesktopThemeId() == DesktopThemeId::SciFi;
        }

        const DesktopTheme& clockTheme()
        {
            return GetCurrentDesktopTheme();
        }

        void publish(uint64_t windowId, MsgType type, const std::string& payload)
        {
            ipc::Message msg;
            msg.type = static_cast<uint32_t>(type);
            std::string packed = std::to_string(windowId);
            if (!payload.empty()) {
                packed.push_back('|');
                packed += payload;
            }
            msg.data.assign(packed.begin(), packed.end());
            ipc::Bus::publish("gui.input", std::move(msg), false);
        }

        void clearWindowSurface(uint64_t windowId)
        {
            publish(windowId, MsgType::MT_DrawText, "\f");
        }

        void drawRect(uint64_t windowId, int x, int y, int w, int h, uint32_t color)
        {
            std::ostringstream oss;
            oss << x << "|" << y << "|" << w << "|" << h << "|"
                << static_cast<int>((color >> 16) & 0xFF) << "|"
                << static_cast<int>((color >> 8) & 0xFF) << "|"
                << static_cast<int>(color & 0xFF);
            publish(windowId, MsgType::MT_DrawRect, oss.str());
        }

        void drawTextAt(uint64_t windowId, int x, int y, const std::string& text)
        {
            std::ostringstream oss;
            oss << x << "|" << y << "|" << text;
            publish(windowId, MsgType::MT_DrawTextAt, oss.str());
        }

        void drawTextAtColor(uint64_t windowId, int x, int y, const std::string& text, uint32_t color)
        {
            std::ostringstream oss;
            oss << x << "|" << y << "|"
                << static_cast<int>((color >> 16) & 0xFF) << "|"
                << static_cast<int>((color >> 8) & 0xFF) << "|"
                << static_cast<int>(color & 0xFF) << "|" << text;
            publish(windowId, MsgType::MT_DrawTextAtColor, oss.str());
        }

        uint32_t ClockBodyColor()
        {
            if (!isSciFiThemeActive()) {
                return packRgb(34, 34, 34);
            }

            const DesktopTheme& theme = clockTheme();
            return blendColor(theme.windowBackground, theme.taskbarBackground, 14);
        }

        uint32_t ClockFaceColor()
        {
            if (!isSciFiThemeActive()) {
                return packRgb(30, 30, 30);
            }

            const DesktopTheme& theme = clockTheme();
            return blendColor(theme.windowBackground, theme.taskbarBackground, 8);
        }

        uint32_t ClockBorderColor()
        {
            if (!isSciFiThemeActive()) {
                return packRgb(60, 60, 60);
            }

            const DesktopTheme& theme = clockTheme();
            return blendColor(theme.windowBorder, theme.mutedAccent, 24);
        }

        uint32_t ClockAccentColor()
        {
            if (!isSciFiThemeActive()) {
                return packRgb(78, 122, 186);
            }

            const DesktopTheme& theme = clockTheme();
            return blendColor(theme.accent, theme.windowBackground, 12);
        }

        uint32_t ClockReadoutColor()
        {
            if (!isSciFiThemeActive()) {
                return packRgb(220, 220, 220);
            }

            return clockTheme().titleBarText;
        }

        uint32_t ClockMutedTextColor()
        {
            if (!isSciFiThemeActive()) {
                return packRgb(180, 180, 180);
            }

            const DesktopTheme& theme = clockTheme();
            return blendColor(theme.titleBarText, theme.taskbarBackground, 56);
        }

        void paintClockSurface(uint64_t windowId)
        {
            clearWindowSurface(windowId);

            if (!isSciFiThemeActive()) {
                return;
            }

            const DesktopTheme& theme = clockTheme();
            const int bodyW = kClockWindowWidth - theme.windowPadding * 2;
            const int bodyH = kClockWindowHeight - theme.titleBarHeight - theme.windowPadding;
            if (bodyW <= 0 || bodyH <= 0) {
                return;
            }

            const int faceInset = theme.windowPadding + 2;
            const int faceW = bodyW - faceInset * 2;
            const int faceH = bodyH - faceInset * 2;
            if (faceW <= 0 || faceH <= 0) {
                return;
            }

            drawRect(windowId, 0, 0, bodyW, bodyH, ClockBodyColor());
            drawRect(windowId, faceInset, faceInset, faceW, faceH, ClockFaceColor());
            drawRect(windowId, faceInset, faceInset, faceW, 1, ClockAccentColor());
            drawRect(windowId, faceInset, faceInset + faceH - 1, faceW, 1, ClockBorderColor());
        }

        void drawClockText(uint64_t windowId, int x, int y, const std::string& text, uint32_t sciFiColor)
        {
            if (isSciFiThemeActive()) {
                drawTextAtColor(windowId, x, y, text, sciFiColor);
                return;
            }

            drawTextAt(windowId, x, y, text);
        }
    }
    
    // Static member initialization
    uint64_t Clock::s_windowId = 0;
    uint64_t Clock::s_lastUpdateTicks = 0;
    
    uint64_t Clock::Launch() {
        ProcessSpec spec{"clock", Clock::main};
        spec.appId = "gxos.builtin.clock";
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
        // Get current time and date
        std::string timeStr = getCurrentTime();
        std::string dateStr = getCurrentDate();

        paintClockSurface(s_windowId);
        
        // Display readout and date using the theme-aware surface colors.
        drawClockText(s_windowId, 0, kClockTimeY, timeStr, ClockReadoutColor());
        drawClockText(s_windowId, 0, kClockDateY, dateStr, ClockMutedTextColor());
        
        // Update window title
        {
            ipc::Message msg;
            msg.type = (uint32_t)MsgType::MT_SetTitle;
            std::ostringstream oss;
            oss << s_windowId << "|Clock - " << timeStr;
            std::string payload = oss.str();
            msg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish("gui.input", std::move(msg), false);
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
