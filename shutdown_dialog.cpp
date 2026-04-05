#include "shutdown_dialog.h"
#include "shutdown_dialog.h"
#include "gui_protocol.h"
#include "lifecycle.h"
#include "logger.h"
#include "allocator.h"
#include <sstream>
#include <thread>
#include <chrono>
#include <iostream>

namespace gxos { namespace apps {


static uint64_t parseWindowIdPayload(const std::string& payload) {
    const size_t sep = payload.find('|');
    const std::string idText = (sep == std::string::npos) ? payload : payload.substr(0, sep);
    try {
        return std::stoull(idText);
    } catch (...) {
        return 0;
    }
}

uint64_t ShutdownDialog::s_windowId = 0;
bool ShutdownDialog::s_confirmed = false;
int ShutdownDialog::s_lastKeyCode = 0;
bool ShutdownDialog::s_keyDown = false;

uint64_t ShutdownDialog::Launch() {
    std::cout << "[ShutdownDialog] Launch() called" << std::endl;
    s_confirmed = false;
    s_lastKeyCode = 0;
    s_keyDown = false;
    ProcessSpec spec{"ShutdownDialog", &ShutdownDialog::main};
    return ProcessTable::spawn(spec, {});
}

int ShutdownDialog::main(int /*argc*/, char** /*argv*/) {
    std::cout << "[ShutdownDialog] main() starting, pid=" << gxos::Allocator::currentPid() << std::endl;
    Logger::write(LogLevel::Info, "ShutdownDialog starting");
    
    // Reset static state for this run
    s_windowId = 0;

    // Ensure IPC channels exist
    ipc::Bus::ensure("gui.input");
    ipc::Bus::ensure("gui.output");

    // Create the window via compositor
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_Create);
        std::string payload = "Confirm Shutdown|" + std::to_string(kDialogW) + "|" + std::to_string(kDialogH);
        m.data.assign(payload.begin(), payload.end());
        std::cout << "[ShutdownDialog] Publishing MT_Create to gui.input" << std::endl;
        ipc::Bus::publish("gui.input", std::move(m), false);
        std::cout << "[ShutdownDialog] MT_Create published" << std::endl;
    }

    // Wait for the matching create acknowledgement from the compositor.
    std::cout << "[ShutdownDialog] Waiting for MT_Create ack..." << std::endl;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
    while (s_windowId == 0 && std::chrono::steady_clock::now() < deadline) {
    ipc::Message m;
    if (!ipc::Bus::pop("gui.output", m, 100)) {
        continue;
    }
        
    Logger::write(LogLevel::Info, std::string("ShutdownDialog got msg type=") + std::to_string(m.type));

    if (m.type != static_cast<uint32_t>(gui::MsgType::MT_Create)) {
        continue;
    }

    std::string payload(m.data.begin(), m.data.end());
    Logger::write(LogLevel::Info, std::string("ShutdownDialog got MT_Create payload=") + payload);
    const size_t sep = payload.find('|');
    if (sep != std::string::npos) {
        const std::string title = payload.substr(sep + 1);
        if (title != "Confirm Shutdown") {
            continue;
        }
    }

    s_windowId = parseWindowIdPayload(payload);
    Logger::write(LogLevel::Info, std::string("ShutdownDialog got window id=") + std::to_string(s_windowId));
}

if (s_windowId == 0) {
        Logger::write(LogLevel::Error, "ShutdownDialog failed to acquire a window id");
        return 1;
    }

    updateDisplay();

    // Simple event loop
    while (!s_confirmed) {
        ipc::Message ev;
        if (ipc::Bus::pop("gui.output", ev, 150)) {
            if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_InputKey)) {
                std::string payload(ev.data.begin(), ev.data.end());
                int keyCode = 0;
                try { keyCode = std::stoi(payload); } catch (...) {}
                handleKeyPress(keyCode);
            }
            if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_WidgetEvt)) {
                std::string payload(ev.data.begin(), ev.data.end());
                // Widget events: "winId|widgetId|event|value"
                std::istringstream iss(payload);
                std::string winIdStr, widgetIdStr, event;
                std::getline(iss, winIdStr, '|');
                std::getline(iss, widgetIdStr, '|');
                std::getline(iss, event, '|');
                if (!winIdStr.empty() && !widgetIdStr.empty()) {
                    try {
                        uint64_t winId = std::stoull(winIdStr);
                        int widgetId = std::stoi(widgetIdStr);
                        if (winId == s_windowId && event == "click") {
                            if (widgetId == 1) {
                                // Yes button
                                s_confirmed = true;
                                Logger::write(LogLevel::Info, "Shutdown confirmed by user");
                                Lifecycle::shutdown();
                            } else if (widgetId == 2) {
                                // No button -> close dialog
                                break;
                            }
                        }
                    } catch (...) {}
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Close the window
    if (s_windowId != 0) {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_Close);
        std::string id = std::to_string(s_windowId);
        m.data.assign(id.begin(), id.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    Logger::write(LogLevel::Info, "ShutdownDialog exiting");
    return 0;
}

void ShutdownDialog::updateDisplay() {
    if (s_windowId == 0) return;
    std::string wid = std::to_string(s_windowId);

    // Draw message text
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_DrawText);
        std::string payload = wid + "|Are you sure you want to shut down?";
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    // Draw background rectangle (dark)
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_DrawRect);
        // id|x|y|w|h|r|g|b
        std::ostringstream oss;
        oss << wid << "|1|30|" << (kDialogW - 2) << "|" << (kDialogH - 32) << "|34|34|34";
        auto s = oss.str();
        m.data.assign(s.begin(), s.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    // Yes button (id=1)
    {
        int yesX = kDialogW - kPadding - kBtnW;
        int btnY = kDialogH - kPadding - kBtnH;
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_WidgetAdd);
        auto payload = gui::packWidgetAdd(s_windowId, 1/*Button*/, 1, yesX, btnY, kBtnW, kBtnH, "Yes");
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    // No button (id=2)
    {
        int noX = kDialogW - kPadding - kBtnW - kGap - kBtnW;
        int btnY = kDialogH - kPadding - kBtnH;
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_WidgetAdd);
        auto payload = gui::packWidgetAdd(s_windowId, 1/*Button*/, 2, noX, btnY, kBtnW, kBtnH, "No");
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }
}

void ShutdownDialog::handleKeyPress(int keyCode) {
    if (keyCode == 'Y' || keyCode == 'y') {
        s_confirmed = true;
        Logger::write(LogLevel::Info, "Shutdown confirmed by keyboard");
        Lifecycle::shutdown();
    } else if (keyCode == 'N' || keyCode == 'n' || keyCode == 27 /*Escape*/) {
        s_confirmed = true; // exit loop
    }
}

}} // namespace gxos::apps
