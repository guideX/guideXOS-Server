#include "open_dialog.h"
#include "gui_protocol.h"
#include "logger.h"
#include <sstream>
#include <thread>
#include <chrono>

namespace gxos { namespace dialogs {

using namespace gxos::gui;

uint64_t OpenDialog::s_windowId = 0;
std::string OpenDialog::s_currentPath;
std::vector<VfsEntryInfo> OpenDialog::s_entries;
int OpenDialog::s_selectedIndex = 0;
int OpenDialog::s_scrollOffset = 0;
std::function<void(const std::string&)> OpenDialog::s_onOpen;
bool OpenDialog::s_done = false;
int OpenDialog::s_lastKeyCode = 0;
bool OpenDialog::s_keyDown = false;

// Static storage for launch parameters
static std::string s_launchPath;
static std::function<void(const std::string&)> s_launchOnOpen;

void OpenDialog::Show(int /*ownerX*/, int /*ownerY*/,
                      const std::string& startPath,
                      std::function<void(const std::string&)> onOpen) {
    s_launchPath = startPath;
    s_launchOnOpen = onOpen;
    s_done = false;
    s_lastKeyCode = 0;
    s_keyDown = false;
    ProcessSpec spec{"OpenDialog", &OpenDialog::main};
    ProcessTable::spawn(spec, {});
}

int OpenDialog::main(int /*argc*/, char** /*argv*/) {
    Logger::write(LogLevel::Info, "OpenDialog starting");

    s_onOpen = s_launchOnOpen;
    s_currentPath = s_launchPath;
    if (s_currentPath.empty()) s_currentPath = "/";
    s_selectedIndex = 0;
    s_scrollOffset = 0;

    // Create the window
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(MsgType::MT_Create);
        std::string payload = "Open|" + std::to_string(kDialogW) + "|" + std::to_string(kDialogH);
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

    refresh();
    redraw();

    // Event loop
    while (!s_done) {
        ipc::Message ev;
        if (ipc::Bus::pop("gui.output", ev, 150)) {
            if (ev.type == static_cast<uint32_t>(MsgType::MT_InputKey)) {
                std::string payload(ev.data.begin(), ev.data.end());
                int keyCode = 0;
                try { keyCode = std::stoi(payload); } catch (...) {}
                handleKeyPress(keyCode);
            }
            if (ev.type == static_cast<uint32_t>(MsgType::MT_WidgetEvt)) {
                std::string payload(ev.data.begin(), ev.data.end());
                if (payload.find("BTN|1") != std::string::npos) {
                    // Open button
                    openAction();
                } else if (payload.find("BTN|2") != std::string::npos) {
                    // Cancel button
                    s_done = true;
                } else if (payload.find("BTN|3") != std::string::npos) {
                    // Up button
                    goUp();
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Close window
    if (s_windowId != 0) {
        ipc::Message m;
        m.type = static_cast<uint32_t>(MsgType::MT_Close);
        std::string id = std::to_string(s_windowId);
        m.data.assign(id.begin(), id.end());
        ipc::Bus::publish("gui.input", std::move(m), false);
    }

    Logger::write(LogLevel::Info, "OpenDialog closed");
    return 0;
}

void OpenDialog::navigate(const std::string& path) {
    s_currentPath = path;
    s_selectedIndex = 0;
    s_scrollOffset = 0;
    refresh();
    redraw();
}

void OpenDialog::goUp() {
    if (s_currentPath.size() <= 1) return;
    // Remove trailing slash
    std::string p = s_currentPath;
    if (!p.empty() && p.back() == '/') p.pop_back();
    auto pos = p.rfind('/');
    if (pos != std::string::npos) {
        p = p.substr(0, pos + 1);
    } else {
        p = "/";
    }
    navigate(p);
}

void OpenDialog::refresh() {
    s_entries = Vfs::instance().list(s_currentPath);
}

void OpenDialog::openAction() {
    if (s_selectedIndex < 0 || s_selectedIndex >= (int)s_entries.size()) return;
    const VfsEntryInfo& entry = s_entries[s_selectedIndex];
    if (entry.isDir) {
        // Navigate into directory
        std::string newPath = s_currentPath;
        if (!newPath.empty() && newPath.back() != '/') newPath += '/';
        newPath += entry.name + "/";
        navigate(newPath);
    } else {
        // File selected - invoke callback
        std::string fullPath = s_currentPath;
        if (!fullPath.empty() && fullPath.back() != '/') fullPath += '/';
        fullPath += entry.name;
        if (s_onOpen) {
            s_onOpen(fullPath);
        }
        s_done = true;
    }
}

void OpenDialog::handleKeyPress(int keyCode) {
    // Escape - cancel
    if (keyCode == 27) {
        s_done = true;
        return;
    }

    // Enter - open selected
    if (keyCode == 13) {
        openAction();
        return;
    }

    // Backspace - go up
    if (keyCode == 8) {
        goUp();
        return;
    }

    // Up arrow
    if (keyCode == 38) {
        if (s_selectedIndex > 0) {
            s_selectedIndex--;
            redraw();
        }
        return;
    }

    // Down arrow
    if (keyCode == 40) {
        if (s_selectedIndex < (int)s_entries.size() - 1) {
            s_selectedIndex++;
            redraw();
        }
        return;
    }
}

void OpenDialog::redraw() {
    if (s_windowId == 0) return;
    const char* kGuiChanIn = "gui.input";
    std::string wid = std::to_string(s_windowId);

    // Draw current path label
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(MsgType::MT_DrawText);
        std::string payload = wid + "|Path: " + s_currentPath;
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(m), false);
    }

    // Draw file list entries (up to 10 visible)
    int visibleCount = std::min(10, (int)s_entries.size());
    for (int i = 0; i < visibleCount; i++) {
        int index = s_scrollOffset + i;
        if (index >= (int)s_entries.size()) break;

        const VfsEntryInfo& entry = s_entries[index];
        ipc::Message m;
        m.type = static_cast<uint32_t>(MsgType::MT_DrawText);
        std::ostringstream oss;
        oss << wid << "|";
        if (index == s_selectedIndex) oss << "> ";
        if (entry.isDir) oss << "[DIR] ";
        oss << entry.name;
        std::string payload = oss.str();
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(m), false);
    }

    // Up button (id=3) - navigate to parent
    {
        ipc::Message m;
        m.type = static_cast<uint32_t>(MsgType::MT_WidgetAdd);
        auto payload = packWidgetAdd(s_windowId, 1, 3, kPadding, kDialogH - kPadding - kBtnH, kBtnW, kBtnH, "Up");
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(m), false);
    }

    // Open button (id=1)
    {
        int btnX = kDialogW - kPadding - kBtnW;
        int btnY = kDialogH - kPadding - kBtnH;
        ipc::Message m;
        m.type = static_cast<uint32_t>(MsgType::MT_WidgetAdd);
        auto payload = packWidgetAdd(s_windowId, 1, 1, btnX, btnY, kBtnW, kBtnH, "Open");
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(m), false);
    }

    // Cancel button (id=2)
    {
        int btnX = kDialogW - kPadding - kBtnW - 8 - kBtnW;
        int btnY = kDialogH - kPadding - kBtnH;
        ipc::Message m;
        m.type = static_cast<uint32_t>(MsgType::MT_WidgetAdd);
        auto payload = packWidgetAdd(s_windowId, 1, 2, btnX, btnY, kBtnW, kBtnH, "Cancel");
        m.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(m), false);
    }
}

}} // namespace gxos::dialogs
