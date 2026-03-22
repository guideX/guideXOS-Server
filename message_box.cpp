#include "message_box.h"
#include "gui_protocol.h"
#include "logger.h"
#include <sstream>
#include <thread>
#include <chrono>

namespace gxos { namespace apps {

uint64_t MessageBox::s_windowId = 0;
std::string MessageBox::s_title;
std::string MessageBox::s_message;
bool MessageBox::s_dismissed = false;
int MessageBox::s_lastKeyCode = 0;
bool MessageBox::s_keyDown = false;

// Static storage for launch parameters (passed via static state before spawn)
static std::string s_launchTitle;
static std::string s_launchMessage;

uint64_t MessageBox::Launch(const std::string& title, const std::string& message) {
    s_launchTitle = title;
    s_launchMessage = message;
    s_dismissed = false;
    s_lastKeyCode = 0;
    s_keyDown = false;
    ProcessSpec spec{"MessageBox", &MessageBox::main};
    return ProcessTable::spawn(spec, {});
}

int MessageBox::main(int /*argc*/, char** /*argv*/) {
    Logger::write(LogLevel::Info, "MessageBox starting");

    s_title = s_launchTitle;
    s_message = s_launchMessage;

    // Create the window via compositor
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_Create);
        std::string payload = s_title + "|" + std::to_string(kDialogW) + "|" + std::to_string(kDialogH);
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    // Wait for compositor to assign window id
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        ipc::Message m;
        if (ipc::Bus::pop("gui.output", m, 200)) {
            std::string s(m.data.begin(), m.data.end());
            try { s_windowId = std::stoull(s); } catch (...) { s_windowId = 0; }
        }
    }

    updateDisplay();

    // Event loop
    while (!s_dismissed) {
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
                // OK button (id=1)
                if (payload.find("BTN|1") != std::string::npos) {
                    s_dismissed = true;
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

    Logger::write(LogLevel::Info, "MessageBox dismissed");
    return 0;
}

void MessageBox::updateDisplay() {
    if (s_windowId == 0) return;
    std::string wid = std::to_string(s_windowId);

    // Draw message text
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_DrawText);
        std::string payload = wid + "|" + s_message;
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    // OK button centered at bottom (id=1)
    {
        int btnX = (kDialogW - kBtnW) / 2;
        int btnY = kDialogH - kPadding - kBtnH;
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_WidgetAdd);
        auto payload = gui::packWidgetAdd(s_windowId, 1/*Button*/, 1, btnX, btnY, kBtnW, kBtnH, "OK");
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }
}

void MessageBox::handleKeyPress(int keyCode) {
    // Enter or Escape dismiss the dialog
    if (keyCode == 13 /*Enter*/ || keyCode == 27 /*Escape*/) {
        s_dismissed = true;
    }
}

}} // namespace gxos::apps
