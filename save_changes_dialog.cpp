#include "save_changes_dialog.h"
#include "gui_protocol.h"
#include "logger.h"
#include <sstream>

namespace gxos { namespace dialogs {
    
    using namespace gxos::gui;
    
    // Static member initialization
    uint64_t SaveChangesDialog::s_windowId = 0;
    std::function<void()> SaveChangesDialog::s_onSave = nullptr;
    std::function<void()> SaveChangesDialog::s_onDontSave = nullptr;
    std::function<void()> SaveChangesDialog::s_onCancel = nullptr;
    
    void SaveChangesDialog::Show(int ownerX, int ownerY,
                                 std::function<void()> onSave,
                                 std::function<void()> onDontSave,
                                 std::function<void()> onCancel) {
        s_onSave = onSave;
        s_onDontSave = onDontSave;
        s_onCancel = onCancel;
        
        // Launch dialog as a new process
        ProcessSpec spec{"save_changes_dialog", SaveChangesDialog::main};
        std::string xStr = std::to_string(ownerX + 40);
        std::string yStr = std::to_string(ownerY + 40);
        ProcessTable::spawn(spec, {"save_changes_dialog", xStr.c_str(), yStr.c_str()});
    }
    
    int SaveChangesDialog::main(int argc, char** argv) {
        try {
            Logger::write(LogLevel::Info, "SaveChangesDialog starting...");
            
            // Parse position from args
            int x = 100, y = 100;
            if (argc >= 3) {
                x = std::stoi(argv[1]);
                y = std::stoi(argv[2]);
            }
            
            // Subscribe to IPC channels
            const char* kGuiChanIn = "gui.input";
            const char* kGuiChanOut = "gui.output";
            ipc::Bus::ensure(kGuiChanIn);
            ipc::Bus::ensure(kGuiChanOut);
            
            // Create window
            ipc::Message createMsg;
            createMsg.type = (uint32_t)MsgType::MT_Create;
            std::ostringstream oss;
            oss << "Unsaved Changes|380|160|" << x << "|" << y;
            std::string payload = oss.str();
            createMsg.data.assign(payload.begin(), payload.end());
            ipc::Bus::publish(kGuiChanIn, std::move(createMsg), false);
            
            // Main event loop
            bool running = true;
            while (running) {
                ipc::Message msg;
                if (ipc::Bus::pop(kGuiChanOut, msg, 100)) {
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
                                    Logger::write(LogLevel::Info, std::string("SaveChangesDialog window created: ") + std::to_string(s_windowId));
                                    
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
                                    
                                    // Add three buttons at bottom
                                    int btnY = 120;  // Near bottom of 160px window
                                    addButton(1, 10, btnY, 90, 28, "Save");
                                    addButton(2, 108, btnY, 110, 28, "Don't Save");
                                    addButton(3, 226, btnY, 90, 28, "Cancel");
                                    
                                    // Draw message text
                                    redraw();
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("SaveChangesDialog: Failed to parse window ID: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        case MsgType::MT_Close: {
                            // Window closed - treat as cancel
                            std::string payload(msg.data.begin(), msg.data.end());
                            if (!payload.empty()) {
                                try {
                                    uint64_t closedId = std::stoull(payload);
                                    if (closedId == s_windowId) {
                                        Logger::write(LogLevel::Info, "SaveChangesDialog closing (cancelled)...");
                                        if (s_onCancel) s_onCancel();
                                        running = false;
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("SaveChangesDialog: Failed to parse close ID: ") + e.what());
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
                                            case 1: // Save
                                                Logger::write(LogLevel::Info, "SaveChangesDialog: Save clicked");
                                                if (s_onSave) s_onSave();
                                                running = false;
                                                break;
                                            case 2: // Don't Save
                                                Logger::write(LogLevel::Info, "SaveChangesDialog: Don't Save clicked");
                                                if (s_onDontSave) s_onDontSave();
                                                running = false;
                                                break;
                                            case 3: // Cancel
                                                Logger::write(LogLevel::Info, "SaveChangesDialog: Cancel clicked");
                                                if (s_onCancel) s_onCancel();
                                                running = false;
                                                break;
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    Logger::write(LogLevel::Error, std::string("SaveChangesDialog: Failed to parse widget event: ") + e.what());
                                }
                            }
                            break;
                        }
                        
                        default:
                            break;
                    }
                }
            }
            
            Logger::write(LogLevel::Info, "SaveChangesDialog stopped");
            return 0;
            
        } catch (const std::exception& e) {
            Logger::write(LogLevel::Error, std::string("SaveChangesDialog EXCEPTION: ") + e.what());
            return -1;
        } catch (...) {
            Logger::write(LogLevel::Error, "SaveChangesDialog UNKNOWN EXCEPTION");
            return -1;
        }
    }
    
    void SaveChangesDialog::redraw() {
        const char* kGuiChanIn = "gui.input";
        
        // Draw message text
        ipc::Message textMsg;
        textMsg.type = (uint32_t)MsgType::MT_DrawText;
        std::ostringstream oss;
        oss << s_windowId << "|Do you want to save changes?";
        std::string payload = oss.str();
        textMsg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(textMsg), false);
    }
    
}} // namespace gxos::dialogs
