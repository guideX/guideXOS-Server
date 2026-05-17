#include "trash.h"

#include "gui_protocol.h"
#include "ipc_bus.h"
#include "logger.h"
#include "process.h"

#include <exception>
#include <sstream>
#include <string>

namespace gxos {
namespace apps {

using namespace gxos::gui;

uint64_t Trash::Launch() {
    ProcessSpec spec{"trash", Trash::main};
    return ProcessTable::spawn(spec, {"trash"});
}

int Trash::main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    try {
        Logger::write(LogLevel::Info, "Trash starting...");

        const char* kGuiChanIn = "gui.input";
        const char* kGuiChanOut = "gui.output";
        ipc::Bus::ensure(kGuiChanIn);
        ipc::Bus::ensure(kGuiChanOut);

        ipc::Message createMsg;
        createMsg.type = static_cast<uint32_t>(MsgType::MT_Create);
        std::string createPayload = "Trash|420|240";
        createMsg.data.assign(createPayload.begin(), createPayload.end());
        ipc::Bus::publish(kGuiChanIn, std::move(createMsg), false);

        uint64_t windowId = 0;
        bool running = true;
        while (running) {
            ipc::Message msg;
            if (!ipc::Bus::pop(kGuiChanOut, msg, 100)) continue;

            MsgType msgType = static_cast<MsgType>(msg.type);
            std::string payload(msg.data.begin(), msg.data.end());
            if (msgType == MsgType::MT_Create) {
                size_t sep = payload.find('|');
                if (sep == std::string::npos || sep == 0) continue;
                try {
                    windowId = std::stoull(payload.substr(0, sep));
                }
                catch (...) {
                    continue;
                }

                ipc::Message clearMsg;
                clearMsg.type = static_cast<uint32_t>(MsgType::MT_DrawText);
                std::string clearPayload = std::to_string(windowId) + "|\f";
                clearMsg.data.assign(clearPayload.begin(), clearPayload.end());
                ipc::Bus::publish(kGuiChanIn, std::move(clearMsg), false);

                ipc::Message bgMsg;
                bgMsg.type = static_cast<uint32_t>(MsgType::MT_DrawRect);
                std::string bgPayload = std::to_string(windowId) + "|0|0|420|240|44|46|54";
                bgMsg.data.assign(bgPayload.begin(), bgPayload.end());
                ipc::Bus::publish(kGuiChanIn, std::move(bgMsg), false);

                ipc::Message panelMsg;
                panelMsg.type = static_cast<uint32_t>(MsgType::MT_DrawRect);
                std::string panelPayload = std::to_string(windowId) + "|16|18|388|196|30|32|38";
                panelMsg.data.assign(panelPayload.begin(), panelPayload.end());
                ipc::Bus::publish(kGuiChanIn, std::move(panelMsg), false);

                ipc::Message textMsg1;
                textMsg1.type = static_cast<uint32_t>(MsgType::MT_DrawText);
                std::ostringstream text1;
                text1 << windowId << "|26|34|Trash is empty.|220|225|235";
                std::string textPayload1 = text1.str();
                textMsg1.data.assign(textPayload1.begin(), textPayload1.end());
                ipc::Bus::publish(kGuiChanIn, std::move(textMsg1), false);

                ipc::Message textMsg2;
                textMsg2.type = static_cast<uint32_t>(MsgType::MT_DrawText);
                std::ostringstream text2;
                text2 << windowId << "|26|58|Deleted files will appear here.|165|170|185";
                std::string textPayload2 = text2.str();
                textMsg2.data.assign(textPayload2.begin(), textPayload2.end());
                ipc::Bus::publish(kGuiChanIn, std::move(textMsg2), false);
            }
            else if (msgType == MsgType::MT_Close) {
                running = false;
            }
        }

        Logger::write(LogLevel::Info, "Trash terminated");
        return 0;
    }
    catch (const std::exception& e) {
        Logger::write(LogLevel::Error, std::string("Trash exception: ") + e.what());
        return 1;
    }
}

} // namespace apps
} // namespace gxos
