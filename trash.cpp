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

    void publishGui(MsgType type, const std::string& payload) {
        ipc::Message msg;
        msg.type = static_cast<uint32_t>(type);
        msg.data.assign(payload.begin(), payload.end());
        ipc::Bus::publish("gui.input", std::move(msg), false);
    }

    void drawTextAt(uint64_t windowId, int x, int y, const std::string& text, int r, int g, int b) {
        std::ostringstream payload;
        payload << windowId << "|" << x << "|" << y << "|" << text << "|" << r << "|" << g << "|" << b;
        publishGui(MsgType::MT_DrawText, payload.str());
    }

    void drawRect(uint64_t windowId, int x, int y, int w, int h, int r, int g, int b) {
        std::ostringstream payload;
        payload << windowId << "|" << x << "|" << y << "|" << w << "|" << h << "|" << r << "|" << g << "|" << b;
        publishGui(MsgType::MT_DrawRect, payload.str());
    }

    void addButton(uint64_t windowId, int id, int x, int y, int w, int h, const std::string& text) {
        std::ostringstream payload;
        payload << windowId << "|1|" << id << "|" << x << "|" << y << "|" << w << "|" << h << "|" << text;
        publishGui(MsgType::MT_WidgetAdd, payload.str());
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
    Logger::write(LogLevel::Info, std::string("Trash item count computed=") + std::to_string(items.size()));
    return items;
}

bool Trash::purgeContents(std::string& error, size_t& deletedCount) {
    deletedCount = 0;
    std::error_code ec;
    std::filesystem::path trashPath = hostedPathForVirtual(kTrashRootPath);
    std::filesystem::path root = hostedRootPath();

    if (!std::filesystem::exists(trashPath, ec)) {
        if (!std::filesystem::create_directories(trashPath, ec) || ec) {
            error = ec ? ec.message() : "Unable to create Trash directory";
            return false;
        }
        return true;
    }
    if (!std::filesystem::is_directory(trashPath, ec) || ec) {
        error = "Trash path is not a directory";
        return false;
    }

    std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec) canonicalRoot = root.lexically_normal();
    std::filesystem::path canonicalTrash = std::filesystem::weakly_canonical(trashPath, ec);
    if (ec) canonicalTrash = trashPath.lexically_normal();
    if (canonicalTrash == canonicalRoot || canonicalTrash.empty()) {
        error = "Refusing to purge unsafe Trash path";
        return false;
    }

    Logger::write(LogLevel::Info, "Empty Trash confirmed; purging hosted Trash contents");
    bool ok = true;
    for (const auto& entry : std::filesystem::directory_iterator(trashPath, ec)) {
        if (ec) { ok = false; break; }
        std::filesystem::path child = entry.path();
        std::filesystem::path canonicalChild = std::filesystem::weakly_canonical(child, ec);
        if (ec) canonicalChild = child.lexically_normal();
        std::string childText = canonicalChild.generic_string();
        std::string trashText = canonicalTrash.generic_string();
        if (childText.size() <= trashText.size() || childText.compare(0, trashText.size(), trashText) != 0) {
            Logger::write(LogLevel::Error, std::string("Refusing to delete path outside Trash: ") + childText);
            ok = false;
            continue;
        }

        std::string name = child.filename().generic_string();
        bool realItem = !endsWithText(name, kTrashInfoSuffix);
        std::uintmax_t removed = std::filesystem::remove_all(child, ec);
        if (ec) {
            Logger::write(LogLevel::Error, std::string("Empty Trash failed deleting ") + child.generic_string() + ": " + ec.message());
            ok = false;
            ec.clear();
        } else {
            if (realItem && removed > 0) ++deletedCount;
            Logger::write(LogLevel::Info, std::string("Empty Trash deleted ") + child.generic_string());
        }
    }

    std::filesystem::create_directories(trashPath, ec);
    if (ec) {
        error = ec.message();
        ok = false;
    }
    if (!ok && error.empty()) error = "Some Trash items could not be deleted";
    Logger::write(ok ? LogLevel::Info : LogLevel::Error,
        std::string("Empty Trash purge ") + (ok ? "succeeded" : "failed") + "; deleted=" + std::to_string(deletedCount));
    return ok;
}

void Trash::render(uint64_t windowId, bool confirmEmpty, const std::string& status) {
    publishGui(MsgType::MT_DrawText, std::to_string(windowId) + "|\f");
    drawRect(windowId, 0, 0, 420, 240, 44, 46, 54);
    drawRect(windowId, 16, 18, 388, 196, 30, 32, 38);

    std::vector<TrashEntry> items = listEntries();
    Logger::write(LogLevel::Info, std::string("Trash window render; item count=") + std::to_string(items.size()));

    if (items.empty()) {
        drawTextAt(windowId, 26, 34, "Trash is empty.", 220, 225, 235);
        drawTextAt(windowId, 26, 58, status.empty() ? "Deleted files will appear here." : status, 165, 170, 185);
    } else {
        std::ostringstream summary;
        summary << "Trash contains " << items.size() << " item(s).";
        drawTextAt(windowId, 26, 34, summary.str(), 220, 225, 235);
        int y = 58;
        for (size_t i = 0; i < items.size() && i < 5; ++i) {
            std::ostringstream item;
            item << "- " << items[i].name << (items[i].isDirectory ? " [Folder]" : " [File]");
            if (!items[i].originalPath.empty()) item << " from " << items[i].originalPath;
            drawTextAt(windowId, 26, y, item.str(), 165, 170, 185);
            y += 20;
        }
        addButton(windowId, 200, 276, 188, 112, 22, "Empty Trash");
    }

    if (!status.empty() && !items.empty()) drawTextAt(windowId, 26, 182, status, 185, 190, 205);
    if (confirmEmpty) {
        drawRect(windowId, 64, 70, 292, 104, 55, 48, 48);
        drawTextAt(windowId, 84, 88, "Empty Trash?", 240, 230, 230);
        drawTextAt(windowId, 84, 112, "This will permanently delete all", 210, 205, 205);
        drawTextAt(windowId, 84, 130, "items in Trash.", 210, 205, 205);
        addButton(windowId, 201, 92, 146, 100, 22, "Empty Trash");
        addButton(windowId, 202, 214, 146, 70, 22, "Cancel");
    }

    gxos::gui::Compositor::requestDesktopRefresh();
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
        bool confirmEmpty = false;
        std::string status;
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

                render(windowId, confirmEmpty, status);
            }
            else if (msgType == MsgType::MT_WidgetEvt) {
                std::istringstream iss(payload);
                std::string winIdStr, widgetIdStr, event;
                std::getline(iss, winIdStr, '|');
                std::getline(iss, widgetIdStr, '|');
                std::getline(iss, event, '|');
                uint64_t eventWindowId = 0;
                try { eventWindowId = std::stoull(winIdStr); } catch (...) { eventWindowId = 0; }
                if (eventWindowId != windowId || event != "click") continue;
                int widgetId = 0;
                try { widgetId = std::stoi(widgetIdStr); } catch (...) { widgetId = 0; }
                if (widgetId == 200) {
                    if (listEntries().empty()) {
                        status = "Trash is already empty.";
                        Logger::write(LogLevel::Info, "Empty Trash requested while already empty");
                    } else {
                        status.clear();
                        confirmEmpty = true;
                        Logger::write(LogLevel::Info, "Empty Trash requested; showing confirmation");
                    }
                    render(windowId, confirmEmpty, status);
                } else if (widgetId == 201) {
                    std::string error;
                    size_t deleted = 0;
                    Logger::write(LogLevel::Info, "Empty Trash confirmed");
                    confirmEmpty = false;
                    if (purgeContents(error, deleted)) {
                        status = "Trash emptied.";
                    } else {
                        status = "Empty Trash failed: " + error;
                    }
                    render(windowId, confirmEmpty, status);
                } else if (widgetId == 202) {
                    Logger::write(LogLevel::Info, "Empty Trash canceled");
                    confirmEmpty = false;
                    status = "Empty Trash canceled.";
                    render(windowId, confirmEmpty, status);
                }
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
