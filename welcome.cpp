#include "welcome.h"
#include "gui_protocol.h"
#include "logger.h"
#include <sstream>
#include <thread>
#include <chrono>

namespace gxos { namespace apps {

uint64_t Welcome::s_windowId = 0;
bool Welcome::s_closed = false;
int Welcome::s_lastKeyCode = 0;
bool Welcome::s_keyDown = false;

uint64_t Welcome::Launch() {
    s_closed = false;
    s_lastKeyCode = 0;
    s_keyDown = false;
    ProcessSpec spec{"Welcome", &Welcome::main};
    return ProcessTable::spawn(spec, {});
}

int Welcome::main(int /*argc*/, char** /*argv*/) {
    Logger::write(LogLevel::Info, "Welcome window starting");

    // Create the window via compositor
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_Create);
        std::string payload = "guideXOS|" + std::to_string(kWinW) + "|" + std::to_string(kWinH);
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
    while (!s_closed) {
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
                    s_closed = true;
                }
            }
            // Window close event
            if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_Close)) {
                s_closed = true;
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

    Logger::write(LogLevel::Info, "Welcome window closed");
    return 0;
}

void Welcome::updateDisplay() {
    if (s_windowId == 0) return;
    std::string wid = std::to_string(s_windowId);

    // Draw welcome text lines (matching Legacy Welcome.cs message)
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_DrawText);
        std::string payload = wid + "|Welcome to guideXOS";
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_DrawText);
        std::string payload = wid + "|";
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_DrawText);
        std::string payload = wid + "|This is a work in progress.";
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_DrawText);
        std::string payload = wid + "|Please direct all questions to";
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_DrawText);
        std::string payload = wid + "|guide_X@live.com";
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_DrawText);
        std::string payload = wid + "|";
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_DrawText);
        std::string payload = wid + "|http://team-nexgen.com";
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    // OK button at bottom center (id=1)
    {
        int btnW = 80;
        int btnH = 28;
        int btnX = (kWinW - btnW) / 2;
        int btnY = kWinH - 40;
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_WidgetAdd);
        auto payload = gui::packWidgetAdd(s_windowId, 1/*Button*/, 1, btnX, btnY, btnW, btnH, "OK");
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }
}

void Welcome::handleKeyPress(int keyCode) {
    // Enter or Escape closes the welcome window
    if (keyCode == 13 /*Enter*/ || keyCode == 27 /*Escape*/) {
        s_closed = true;
    }
}

}} // namespace gxos::apps
