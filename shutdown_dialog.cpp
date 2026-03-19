#include "shutdown_dialog.h"
#include "gui_protocol.h"
#include "lifecycle.h"
#include "logger.h"
#include <sstream>
#include <thread>
#include <chrono>

namespace gxos { namespace apps {

uint64_t ShutdownDialog::s_windowId = 0;
bool ShutdownDialog::s_confirmed = false;
int ShutdownDialog::s_lastKeyCode = 0;
bool ShutdownDialog::s_keyDown = false;

uint64_t ShutdownDialog::Launch() {
    s_confirmed = false;
    s_lastKeyCode = 0;
    s_keyDown = false;
    ProcessSpec spec{"ShutdownDialog", &ShutdownDialog::main};
    return ProcessTable::spawn(spec, {});
}

int ShutdownDialog::main(int /*argc*/, char** /*argv*/) {
    Logger::write(LogLevel::Info, "ShutdownDialog starting");

    // Create the window via compositor
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_Create);
        std::string payload = "Confirm Shutdown|" + std::to_string(kDialogW) + "|" + std::to_string(kDialogH);
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    // Wait briefly for compositor to process and capture the window id
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        ipc::Message m;
        if (ipc::Bus::pop("gui.output", m, 200)) {
            std::string s(m.data.begin(), m.data.end());
            // Expect id in response
            try { s_windowId = std::stoull(s); } catch (...) { s_windowId = 0; }
        }
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
                // Button events: "BTN|<id>"
                if (payload.find("BTN|1") != std::string::npos) {
                    // Yes button
                    s_confirmed = true;
                    Logger::write(LogLevel::Info, "Shutdown confirmed by user");
                    Lifecycle::shutdown();
                } else if (payload.find("BTN|2") != std::string::npos) {
                    // No button -> close dialog
                    break;
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
