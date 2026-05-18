#include "trash.h"

#include "compositor.h"
#include "gui_protocol.h"
#include "ipc_bus.h"
#include "logger.h"
#include "process.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace gxos {
namespace apps {

using namespace gxos::gui;

namespace {
    constexpr const char* kTrashRootPath = "/Trash";
    constexpr const char* kTrashInfoSuffix = ".trashinfo";

    bool endsWithText(const std::string& value, const char* suffix) {
        if (!suffix) return false;
        size_t suffixLen = std::char_traits<char>::length(suffix);
        return value.size() >= suffixLen && value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
    }

    std::string normalizeHostedVirtualPath(const std::string& path) {
        std::string normalized = path.empty() ? "/" : path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        std::filesystem::path virtualPath(normalized);
        if (virtualPath.is_relative()) virtualPath = std::filesystem::path("/") / virtualPath;
        std::string out = virtualPath.lexically_normal().generic_string();
        if (out.empty()) out = "/";
        if (out.front() != '/') out.insert(out.begin(), '/');
        return out;
    }

    std::filesystem::path hostedRootPath() {
        return std::filesystem::current_path();
    }

    std::filesystem::path hostedPathForVirtual(const std::string& path) {
        std::string normalized = normalizeHostedVirtualPath(path);
        if (normalized == "/") return hostedRootPath();
        return hostedRootPath() / std::filesystem::path(normalized.substr(1));
    }

    std::string readOriginalPath(const std::filesystem::path& infoPath) {
        std::ifstream stream(infoPath);
        if (!stream) return std::string();
        std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        const std::string key = "\"originalPath\": \"";
        size_t start = content.find(key);
        if (start == std::string::npos) return std::string();
        start += key.size();
        size_t end = content.find('"', start);
        if (end == std::string::npos) return std::string();
        return content.substr(start, end - start);
    }
}

uint64_t Trash::Launch() {
    ProcessSpec spec{"trash", Trash::main};
    return ProcessTable::spawn(spec, {"trash"});
}

std::vector<Trash::TrashEntry> Trash::listEntries() {
    std::vector<TrashEntry> items;
    std::error_code error;
    std::filesystem::path trashPath = hostedPathForVirtual(kTrashRootPath);
    if (!std::filesystem::exists(trashPath, error) || error) return items;

    for (const auto& entry : std::filesystem::directory_iterator(trashPath, error)) {
        if (error) break;
        std::string name = entry.path().filename().generic_string();
        if (endsWithText(name, kTrashInfoSuffix)) continue;

        TrashEntry item;
        item.name = name;
        item.isDirectory = entry.is_directory(error);
        item.originalPath = readOriginalPath(entry.path().string() + kTrashInfoSuffix);
        items.push_back(item);
    }

    std::sort(items.begin(), items.end(), [](const TrashEntry& a, const TrashEntry& b) {
        return a.name < b.name;
    });
    return items;
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

                std::vector<TrashEntry> items = listEntries();
                Logger::write(LogLevel::Info, std::string("Trash window opened; item count=") + std::to_string(items.size()));
                if (items.empty()) {
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
                } else {
                    ipc::Message summaryMsg;
                    summaryMsg.type = static_cast<uint32_t>(MsgType::MT_DrawText);
                    std::ostringstream summary;
                    summary << windowId << "|26|34|Trash contains " << items.size() << " item(s).|220|225|235";
                    std::string summaryPayload = summary.str();
                    summaryMsg.data.assign(summaryPayload.begin(), summaryPayload.end());
                    ipc::Bus::publish(kGuiChanIn, std::move(summaryMsg), false);

                    int y = 58;
                    for (size_t i = 0; i < items.size() && i < 6; ++i) {
                        ipc::Message itemMsg;
                        itemMsg.type = static_cast<uint32_t>(MsgType::MT_DrawText);
                        std::ostringstream item;
                        item << windowId << "|26|" << y << "|- " << items[i].name;
                        if (items[i].isDirectory) item << " [Folder]";
                        else item << " [File]";
                        if (!items[i].originalPath.empty()) item << " from " << items[i].originalPath;
                        item << "|165|170|185";
                        std::string itemPayload = item.str();
                        itemMsg.data.assign(itemPayload.begin(), itemPayload.end());
                        ipc::Bus::publish(kGuiChanIn, std::move(itemMsg), false);
                        y += 20;
                    }
                }
                gxos::gui::Compositor::requestDesktopRefresh();
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
