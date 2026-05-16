#include "native_app_debug_viewer.h"

#include "gui_protocol.h"
#include "ipc_bus.h"
#include "logger.h"
#include "native_app_debug_log.h"
#include "native_elf_executor.h"
#include "process.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace gxos { namespace apps {

    using namespace gxos::gui;

    uint64_t NativeAppDebugViewer::s_windowId = 0;
    std::vector<NativeAppProcessInfo> NativeAppDebugViewer::s_processes;
    int NativeAppDebugViewer::s_selectedIndex = 0;
    int NativeAppDebugViewer::s_scrollOffset = 0;
    int NativeAppDebugViewer::s_lastKeyCode = 0;
    bool NativeAppDebugViewer::s_keyDown = false;

    uint64_t NativeAppDebugViewer::Launch() {
        ProcessSpec spec{"native_app_debug_viewer", NativeAppDebugViewer::main};
        return ProcessTable::spawn(spec, {"native_app_debug_viewer"});
    }

    int NativeAppDebugViewer::main(int argc, char** argv) {
        try {
            Logger::write(LogLevel::Info, "NativeAppDebugViewer starting...");

            s_windowId = 0;
            s_processes.clear();
            s_selectedIndex = 0;
            s_scrollOffset = 0;
            s_lastKeyCode = 0;
            s_keyDown = false;
            refresh();

            ipc::Bus::ensure("gui.input");
            ipc::Bus::ensure("gui.output");

            ipc::Message createMsg;
            createMsg.type = static_cast<uint32_t>(MsgType::MT_Create);
            std::string payload = "Native App Debug Viewer|900|560";
            createMsg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish("gui.input", std::move(createMsg), false);

            bool running = true;
            while (running) {
                ipc::Message msg;
                if (!ipc::Bus::pop("gui.output", msg, 50)) continue;

                MsgType msgType = static_cast<MsgType>(msg.type);
                switch (msgType) {
                    case MsgType::MT_Create: {
                        std::string payload(msg.data.begin(), msg.data.end());
                        size_t sep = payload.find('|');
                        if (sep != std::string::npos && sep > 0) {
                            try {
                                s_windowId = std::stoull(payload.substr(0, sep));
                                Logger::write(LogLevel::Info, "NativeAppDebugViewer window created: " + std::to_string(s_windowId));
                                updateDisplay();
                            } catch (const std::exception& e) {
                                Logger::write(LogLevel::Error, std::string("NativeAppDebugViewer: Failed to parse window ID: ") + e.what());
                            }
                        }
                        break;
                    }
                    case MsgType::MT_Close: {
                        std::string payload(msg.data.begin(), msg.data.end());
                        try {
                            if (!payload.empty() && std::stoull(payload) == s_windowId) running = false;
                        } catch (...) {
                        }
                        break;
                    }
                    case MsgType::MT_InputKey: {
                        std::string payload(msg.data.begin(), msg.data.end());
                        size_t sep = payload.find('|');
                        if (sep != std::string::npos && sep > 0) {
                            try {
                                int keyCode = std::stoi(payload.substr(0, sep));
                                std::string action = payload.substr(sep + 1);
                                if (action == "down") {
                                    if (s_keyDown && keyCode == s_lastKeyCode) break;
                                    s_keyDown = true;
                                    s_lastKeyCode = keyCode;
                                    handleKeyPress(keyCode, running);
                                } else {
                                    s_keyDown = false;
                                    s_lastKeyCode = 0;
                                }
                            } catch (const std::exception& e) {
                                Logger::write(LogLevel::Error, std::string("NativeAppDebugViewer: Failed to parse key code: ") + e.what());
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            Logger::write(LogLevel::Info, "NativeAppDebugViewer stopped");
            return 0;
        } catch (const std::exception& e) {
            Logger::write(LogLevel::Error, std::string("NativeAppDebugViewer EXCEPTION: ") + e.what());
            return -1;
        } catch (...) {
            Logger::write(LogLevel::Error, "NativeAppDebugViewer UNKNOWN EXCEPTION");
            return -1;
        }
    }

    void NativeAppDebugViewer::refresh() {
        s_processes = NativeAppProcessTable::List();
        std::sort(s_processes.begin(), s_processes.end(), [](const NativeAppProcessInfo& a, const NativeAppProcessInfo& b) {
            return a.runtimeId < b.runtimeId;
        });

        if (s_selectedIndex >= static_cast<int>(s_processes.size())) s_selectedIndex = static_cast<int>(s_processes.size()) - 1;
        if (s_selectedIndex < 0) s_selectedIndex = 0;
        if (s_scrollOffset < 0) s_scrollOffset = 0;
        if (s_selectedIndex < s_scrollOffset) s_scrollOffset = s_selectedIndex;
        if (s_selectedIndex >= s_scrollOffset + 10) s_scrollOffset = s_selectedIndex - 9;
    }

    void NativeAppDebugViewer::drawText(const std::string& text) {
        if (s_windowId == 0) return;
        ipc::Message msg;
        msg.type = static_cast<uint32_t>(MsgType::MT_DrawText);
        std::string payload = std::to_string(s_windowId) + "|" + text;
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(msg), false);
    }

    void NativeAppDebugViewer::drawRect(int x, int y, int w, int h, int r, int g, int b) {
        if (s_windowId == 0) return;
        ipc::Message msg;
        msg.type = static_cast<uint32_t>(MsgType::MT_DrawRect);
        std::ostringstream oss;
        oss << s_windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|" << r << "|" << g << "|" << b;
        std::string payload = oss.str();
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(msg), false);
    }

    std::string NativeAppDebugViewer::truncate(const std::string& text, size_t maxLength) {
        if (text.size() <= maxLength) return text;
        if (maxLength <= 3) return text.substr(0, maxLength);
        return text.substr(0, maxLength - 3) + "...";
    }

    std::string NativeAppDebugViewer::formatDuration(const std::chrono::steady_clock::time_point& timePoint) {
        if (timePoint == std::chrono::steady_clock::time_point()) return "n/a";
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch()).count();
        return std::to_string(ms) + "ms";
    }

    void NativeAppDebugViewer::updateDisplay() {
        if (s_windowId == 0) return;

        drawText("\f");
        drawRect(0, 0, 900, 28, 32, 40, 58);
        drawRect(0, 168, 900, 2, 64, 80, 110);
        drawRect(0, 350, 900, 2, 64, 80, 110);
        drawRect(0, 524, 900, 24, 32, 40, 58);

        int running = 0;
        int exited = 0;
        int failed = 0;
        for (const auto& process : s_processes) {
            if (process.lifecycleState == NativeAppLifecycleState::Running) ++running;
            else if (process.lifecycleState == NativeAppLifecycleState::Exited) ++exited;
            else if (process.lifecycleState == NativeAppLifecycleState::Failed) ++failed;
        }

        std::ostringstream header;
        header << "Native App Debug Viewer  |  Total: " << s_processes.size()
               << "  Running: " << running
               << "  Exited: " << exited
               << "  Failed: " << failed
               << "  Experimental: " << (NativeElfExecutor::ExperimentalExecutionEnabled() ? "enabled" : "disabled");
        drawText(header.str());
        drawText("Keys: Up/Down select  R/F5 refresh  Escape close");
        drawText("");
        drawText("Runtime  App ID                         Display Name       Arch   State    Exit Windows C/R/Rem Exp Failure");

        int start = s_scrollOffset;
        int end = std::min(static_cast<int>(s_processes.size()), start + 10);
        if (s_processes.empty()) {
            drawText("  No native app processes have been recorded.");
        }
        for (int i = start; i < end; ++i) {
            const auto& p = s_processes[i];
            std::ostringstream row;
            row << (i == s_selectedIndex ? "> " : "  ")
                << std::setw(6) << std::right << p.runtimeId << "  "
                << std::setw(30) << std::left << truncate(p.appId, 30) << " "
                << std::setw(18) << std::left << truncate(p.displayName, 18) << " "
                << std::setw(6) << std::left << truncate(p.architecture, 6) << " "
                << std::setw(8) << std::left << NativeAppRuntime::ToString(p.lifecycleState) << " "
                << std::setw(4) << std::right << p.exitCode << " "
                << p.createdWindowCount << "/" << p.cleanedWindowCount << "/" << p.remainingWindowCount << "     "
                << (p.experimentalExecutionEnabled ? "yes" : "no ") << " "
                << truncate(p.failureReason, 16);
            drawText(row.str());
        }

        drawText("");
        drawText("Selected runtime details");
        if (!s_processes.empty() && s_selectedIndex >= 0 && s_selectedIndex < static_cast<int>(s_processes.size())) {
            const auto& p = s_processes[s_selectedIndex];
            drawText("  runtimeId=" + std::to_string(p.runtimeId) + " appId=" + p.appId + " displayName=" + p.displayName);
            drawText("  architecture=" + p.architecture + " lifecycle=" + NativeAppRuntime::ToString(p.lifecycleState) + " hostArchitecture=" + p.hostArchitecture);
            drawText("  start=" + formatDuration(p.startTime) + " end=" + formatDuration(p.endTime) + " exitCode=" + std::to_string(p.exitCode));
            drawText("  failureReason=" + (p.failureReason.empty() ? std::string("none") : p.failureReason));
            drawText("  windows created=" + std::to_string(p.createdWindowCount) + " cleaned=" + std::to_string(p.cleanedWindowCount) + " remaining=" + std::to_string(p.remainingWindowCount));
            drawText("  hostCalls log=" + std::to_string(p.hostLogCallCount) + " request_window=" + std::to_string(p.requestWindowCallCount) + " draw_text=" + std::to_string(p.drawTextCallCount) + " draw_rect=" + std::to_string(p.drawRectCallCount));
            drawText("            poll_event=" + std::to_string(p.pollEventCallCount) + " file_exists=" + std::to_string(p.fileExistsCallCount) + " file_read_all=" + std::to_string(p.fileReadCallCount));
            drawText("  experimentalExecutionEnabled=" + std::string(p.experimentalExecutionEnabled ? "true" : "false"));
        } else {
            drawText("  No runtime selected.");
        }

        drawText("");
        drawText("Recent native app diagnostics");
        auto entries = NativeAppDebugLog::Recent(12);
        if (entries.empty()) {
            drawText("  No diagnostic log entries.");
        } else {
            for (const auto& entry : entries) {
                std::ostringstream line;
                line << "  #" << entry.timestamp << " rt=" << entry.runtimeId << " " << entry.severity << " " << truncate(entry.appId, 24) << " - " << truncate(entry.message, 78);
                drawText(line.str());
            }
        }

        std::ostringstream status;
        status << "Status: read-only snapshot; row " << (s_processes.empty() ? 0 : s_selectedIndex + 1) << "/" << s_processes.size();
        drawText(status.str());
    }

    void NativeAppDebugViewer::handleKeyPress(int keyCode, bool& running) {
        if (keyCode == 27) {
            running = false;
            if (s_windowId != 0) {
                ipc::Message msg;
                msg.type = static_cast<uint32_t>(MsgType::MT_Close);
                std::string payload = std::to_string(s_windowId);
                msg.data.assign(payload.begin(), payload.end());
                ipc::Bus::publish("gui.input", std::move(msg), false);
            }
            return;
        }

        if (keyCode == 38) {
            if (s_selectedIndex > 0) --s_selectedIndex;
            if (s_selectedIndex < s_scrollOffset) s_scrollOffset = s_selectedIndex;
            updateDisplay();
            return;
        }

        if (keyCode == 40) {
            if (s_selectedIndex < static_cast<int>(s_processes.size()) - 1) ++s_selectedIndex;
            if (s_selectedIndex >= s_scrollOffset + 10) s_scrollOffset = s_selectedIndex - 9;
            updateDisplay();
            return;
        }

        if (keyCode == 82 || keyCode == 114 || keyCode == 116) {
            refresh();
            updateDisplay();
        }
    }

}} // namespace gxos::apps
