#include "onscreen_keyboard.h"
#include "gui_protocol.h"
#include "logger.h"
#include <sstream>
#include <thread>
#include <chrono>

namespace gxos { namespace apps {

// US-QWERTY rows (lowercase / unshifted)
const char* OnScreenKeyboard::kRows[] = {
    "`1234567890-=",
    "qwertyuiop[]\\",
    "asdfghjkl;'",
    "zxcvbnm,./"
};

// Shifted rows
const char* OnScreenKeyboard::kRowsShift[] = {
    "~!@#$%^&*()_+",
    "QWERTYUIOP{}|",
    "ASDFGHJKL:\"",
    "ZXCVBNM<>?"
};

uint64_t OnScreenKeyboard::s_windowId = 0;
bool OnScreenKeyboard::s_shift = false;
bool OnScreenKeyboard::s_caps  = false;
int  OnScreenKeyboard::s_lastKeyCode = 0;
bool OnScreenKeyboard::s_keyDown = false;

uint64_t OnScreenKeyboard::Launch() {
    s_shift = false;
    s_caps  = false;
    s_lastKeyCode = 0;
    s_keyDown = false;
    ProcessSpec spec{"OnScreenKeyboard", &OnScreenKeyboard::main};
    return ProcessTable::spawn(spec, {});
}

int OnScreenKeyboard::main(int /*argc*/, char** /*argv*/) {
    Logger::write(LogLevel::Info, "OnScreenKeyboard starting");

    // Create the window
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_Create);
        std::string payload = "On-Screen Keyboard|" + std::to_string(kWinW) + "|" + std::to_string(kWinH);
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

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
    bool running = true;
    while (running) {
        ipc::Message ev;
        if (ipc::Bus::pop("gui.output", ev, 150)) {
            if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_Close)) {
                running = false;
            } else if (ev.type == static_cast<uint32_t>(gui::MsgType::MT_WidgetEvt)) {
                std::string payload(ev.data.begin(), ev.data.end());
                // Button events from compositor: "BTN|<id>"
                if (payload.find("BTN|") == 0) {
                    int id = 0;
                    try { id = std::stoi(payload.substr(4)); } catch (...) {}

                    // id encoding:
                    // 1000 + row*100 + col  -> key on row
                    // 2001 = Shift, 2002 = CapsLock, 2003 = Space, 2004 = Backspace, 2005 = Enter
                    if (id == 2001) {
                        s_shift = !s_shift;
                        updateDisplay();
                    } else if (id == 2002) {
                        s_caps = !s_caps;
                        updateDisplay();
                    } else if (id == 2003) {
                        sendKey(' ');
                    } else if (id == 2004) {
                        sendKey('\b');
                    } else if (id == 2005) {
                        sendKey('\r');
                    } else if (id >= 1000 && id < 2000) {
                        int row = (id - 1000) / 100;
                        int col = (id - 1000) % 100;
                        if (row >= 0 && row < kRowCount) {
                            const char* layout = (s_shift || s_caps) ? kRowsShift[row] : kRows[row];
                            int len = static_cast<int>(std::string(layout).size());
                            if (col >= 0 && col < len) {
                                sendKey(layout[col]);
                                if (s_shift && !s_caps) {
                                    s_shift = false;
                                    updateDisplay();
                                }
                            }
                        }
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    // Close window
    if (s_windowId != 0) {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_Close);
        std::string id = std::to_string(s_windowId);
        m.data.assign(id.begin(), id.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    Logger::write(LogLevel::Info, "OnScreenKeyboard exiting");
    return 0;
}

void OnScreenKeyboard::updateDisplay() {
    if (s_windowId == 0) return;
    std::string wid = std::to_string(s_windowId);

    int startY = 10;

    // Main keyboard rows
    for (int row = 0; row < kRowCount; ++row) {
        const char* layout = (s_shift || s_caps) ? kRowsShift[row] : kRows[row];
        int len = static_cast<int>(std::string(layout).size());
        int startX = 10 + row * 12; // slight indent per row (match legacy)

        for (int col = 0; col < len; ++col) {
            int kx = startX + col * (kKeyW + kGap);
            int ky = startY + row * (kKeyH + kGap);
            int btnId = 1000 + row * 100 + col;

            std::string label(1, layout[col]);
            ipc::Message m;
            m.type = static_cast<uint32_t>(gui::MsgType::MT_WidgetAdd);
            auto payload = gui::packWidgetAdd(s_windowId, 1/*Button*/, btnId, kx, ky, kKeyW, kKeyH, label);
            m.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish("gui.input", std::move(m), false);
        }
    }

    // Bottom row: Shift, CapsLock, Space, Backspace, Enter
    int bottomY = startY + kRowCount * (kKeyH + kGap);
    int x = 10;

    auto addSpecial = [&](int id, int w, const std::string& label) {
        ipc::Message m;
        m.type = static_cast<uint32_t>(gui::MsgType::MT_WidgetAdd);
        auto payload = gui::packWidgetAdd(s_windowId, 1/*Button*/, id, x, bottomY, w, kKeyH, label);
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
        x += w + kGap;
    };

    addSpecial(2001, kShiftW, s_shift ? "SHIFT*" : "Shift");
    addSpecial(2002, kCapsW,  s_caps  ? "CAPS*"  : "Caps");
    addSpecial(2003, kSpaceW, "Space");
    addSpecial(2004, kBackW,  "Bksp");
    addSpecial(2005, kEnterW, "Enter");
}

void OnScreenKeyboard::sendKey(char ch) {
    // Publish a key event on the IPC bus so the focused window receives it
    ipc::Message m;
    m.type = static_cast<uint32_t>(gui::MsgType::MT_InputKey);
    std::string payload(1, ch);
    m.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(m), false);
}

void OnScreenKeyboard::handleKeyPress(int keyCode) {
    // Physical keyboard passthrough (toggle shift/caps)
    if (keyCode == 16) { s_shift = !s_shift; updateDisplay(); }
    if (keyCode == 20) { s_caps  = !s_caps;  updateDisplay(); }
}

}} // namespace gxos::apps
