#pragma once

#include <cstddef>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "clock_time_settings.h"
#include "desktop_theme.h"

#if defined(GXOS_BARE_METAL)
#include "kernel/core/include/kernel/vfs.h"
#else
#include "fs.h"
#endif

namespace gxos {
namespace gui {

static constexpr size_t kDisplayOptionsStoreMaxBytes = 4096;

struct DisplayOptionsStoreData {
    std::string wallpaperId;
    std::string backgroundScaleMode{"fill"};
    std::string desktopThemeId{"classic"};
    std::string taskbarPosition{"bottom"};
    std::string timeZoneId{"pacific"};
    bool use24HourTime{false};
    bool showDesktopTrash{true};
    bool showDesktopThisSystem{true};
    bool showDesktopFileManager{true};
    bool showDesktopSystemSettings{false};
    bool smallLiveDesktopFolderIcons{true};
    bool autoArrangeDesktopIcons{false};
};

class DisplayOptionsStore {
public:
    static bool Load(const std::string& path, DisplayOptionsStoreData& out, std::string& err) {
        err.clear();
        out = DisplayOptionsStoreData{};

        std::vector<uint8_t> bytes;
#if defined(GXOS_BARE_METAL)
        kernel::vfs::FileInfo info{};
        if (kernel::vfs::stat(path.c_str(), &info) != kernel::vfs::VFS_OK) {
            err = "open fail";
            return false;
        }
        if (info.size == 0) {
            err = "empty";
            return false;
        }
        if (info.size > kDisplayOptionsStoreMaxBytes) {
            err = "file too large";
            return false;
        }
        bytes.resize(static_cast<size_t>(info.size));
        const int32_t read = kernel::vfs::read_file(path.c_str(), bytes.data(), static_cast<uint32_t>(bytes.size()));
        if (read < 0) {
            err = "read fail";
            return false;
        }
        bytes.resize(static_cast<size_t>(read));
#else
        FSResult readResult = FS::readAll(path, bytes, static_cast<uint64_t>(kDisplayOptionsStoreMaxBytes));
        if (!readResult.success) {
            err = readResult.message.empty() ? "open fail" : readResult.message;
            return false;
        }
#endif

        const std::string text(bytes.begin(), bytes.end());
        if (text.empty()) {
            err = "empty";
            return false;
        }

        auto trim = [](const std::string& value) {
            size_t begin = 0;
            while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
            size_t end = value.size();
            while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
            return value.substr(begin, end - begin);
        };

        auto unquote = [&](std::string value) {
            value = trim(value);
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                std::string decoded;
                decoded.reserve(value.size() - 2);
                for (size_t i = 1; i + 1 < value.size(); ++i) {
                    char c = value[i];
                    if (c == '\\' && i + 2 < value.size()) {
                        char next = value[++i];
                        switch (next) {
                        case '\\': decoded.push_back('\\'); break;
                        case '"': decoded.push_back('"'); break;
                        case 'n': decoded.push_back('\n'); break;
                        case 'r': decoded.push_back('\r'); break;
                        case 't': decoded.push_back('\t'); break;
                        default: decoded.push_back(next); break;
                        }
                    } else {
                        decoded.push_back(c);
                    }
                }
                return decoded;
            }
            return value;
        };

        auto parseBool = [](const std::string& value, bool fallback) {
            std::string lower;
            lower.reserve(value.size());
            for (char c : value) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") return true;
            if (lower == "0" || lower == "false" || lower == "no" || lower == "off") return false;
            return fallback;
        };

        auto normalizeScaleMode = [](const std::string& value) {
            std::string lower;
            lower.reserve(value.size());
            for (char c : value) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (lower == "fill" || lower == "fit" || lower == "stretch" || lower == "center" || lower == "tile") {
                return lower;
            }
            return std::string("fill");
        };

        auto normalizeTaskbarPosition = [](const std::string& value) {
            std::string lower;
            lower.reserve(value.size());
            for (char c : value) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (lower == "bottom" || lower == "top" || lower == "left" || lower == "right") {
                return lower;
            }
            return std::string("bottom");
        };

        bool parsedAny = false;
        std::istringstream input(text);
        std::string line;
        while (std::getline(input, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            const size_t sep = line.find('=');
            if (sep == std::string::npos) continue;

            const std::string key = trim(line.substr(0, sep));
            const std::string value = unquote(line.substr(sep + 1));
            if (key.empty()) continue;

            if (key == "version" || key == "displayOptionsVersion") {
                parsedAny = true;
                continue;
            }

            if (key == "wallpaperId") {
                out.wallpaperId = value;
                parsedAny = true;
            } else if (key == "backgroundScaleMode") {
                out.backgroundScaleMode = value;
                parsedAny = true;
            } else if (key == "desktopThemeId") {
                out.desktopThemeId = value;
                parsedAny = true;
            } else if (key == "taskbarPosition") {
                out.taskbarPosition = value;
                parsedAny = true;
            } else if (key == "timeZoneId" || key == "clockTimeZoneId" || key == "timeZone") {
                out.timeZoneId = value;
                parsedAny = true;
            } else if (key == "use24HourTime" || key == "clockUse24HourTime" || key == "use24Hour") {
                out.use24HourTime = parseBool(value, out.use24HourTime);
                parsedAny = true;
            } else if (key == "showDesktopTrash") {
                out.showDesktopTrash = parseBool(value, out.showDesktopTrash);
                parsedAny = true;
            } else if (key == "showDesktopThisSystem") {
                out.showDesktopThisSystem = parseBool(value, out.showDesktopThisSystem);
                parsedAny = true;
            } else if (key == "showDesktopFileManager") {
                out.showDesktopFileManager = parseBool(value, out.showDesktopFileManager);
                parsedAny = true;
            } else if (key == "showDesktopSystemSettings") {
                out.showDesktopSystemSettings = parseBool(value, out.showDesktopSystemSettings);
                parsedAny = true;
            } else if (key == "smallLiveDesktopFolderIcons") {
                out.smallLiveDesktopFolderIcons = parseBool(value, out.smallLiveDesktopFolderIcons);
                parsedAny = true;
            } else if (key == "autoArrangeDesktopIcons") {
                out.autoArrangeDesktopIcons = parseBool(value, out.autoArrangeDesktopIcons);
                parsedAny = true;
            }
        }

        DesktopThemeId themeId = DesktopThemeId::Classic;
        TryParseDesktopThemeId(out.desktopThemeId.c_str(), &themeId);
        out.desktopThemeId = DesktopThemeIdToString(themeId);
        out.backgroundScaleMode = normalizeScaleMode(out.backgroundScaleMode);
        out.taskbarPosition = normalizeTaskbarPosition(out.taskbarPosition);
        out.timeZoneId = gxos::clocktime::NormalizeTimeZoneId(out.timeZoneId);

        if (!parsedAny) {
            err = "no settings";
            return false;
        }

        return true;
    }

    static bool Save(const std::string& path, const DisplayOptionsStoreData& data, std::string& err) {
        err.clear();

        auto escape = [](const std::string& value) {
            std::ostringstream out;
            out << '"';
            for (char c : value) {
                switch (c) {
                case '\\': out << "\\\\"; break;
                case '"': out << "\\\""; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default: out << c; break;
                }
            }
            out << '"';
            return out.str();
        };

        auto normalizeScaleMode = [](const std::string& value) {
            std::string lower;
            lower.reserve(value.size());
            for (char c : value) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (lower == "fill" || lower == "fit" || lower == "stretch" || lower == "center" || lower == "tile") {
                return lower;
            }
            return std::string("fill");
        };

        auto normalizeTaskbarPosition = [](const std::string& value) {
            std::string lower;
            lower.reserve(value.size());
            for (char c : value) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (lower == "bottom" || lower == "top" || lower == "left" || lower == "right") {
                return lower;
            }
            return std::string("bottom");
        };

        DisplayOptionsStoreData normalized = data;
        DesktopThemeId themeId = DesktopThemeId::Classic;
        TryParseDesktopThemeId(normalized.desktopThemeId.c_str(), &themeId);
        normalized.desktopThemeId = DesktopThemeIdToString(themeId);
        normalized.backgroundScaleMode = normalizeScaleMode(normalized.backgroundScaleMode);
        normalized.taskbarPosition = normalizeTaskbarPosition(normalized.taskbarPosition);
        normalized.timeZoneId = gxos::clocktime::NormalizeTimeZoneId(normalized.timeZoneId);

        std::ostringstream out;
        out << "version=1\n";
        out << "wallpaperId=" << escape(normalized.wallpaperId) << "\n";
        out << "backgroundScaleMode=" << escape(normalized.backgroundScaleMode) << "\n";
        out << "desktopThemeId=" << escape(normalized.desktopThemeId) << "\n";
        out << "taskbarPosition=" << escape(normalized.taskbarPosition) << "\n";
        out << "timeZoneId=" << escape(normalized.timeZoneId) << "\n";
        out << "use24HourTime=" << (normalized.use24HourTime ? "1" : "0") << "\n";
        out << "showDesktopTrash=" << (normalized.showDesktopTrash ? "1" : "0") << "\n";
        out << "showDesktopThisSystem=" << (normalized.showDesktopThisSystem ? "1" : "0") << "\n";
        out << "showDesktopFileManager=" << (normalized.showDesktopFileManager ? "1" : "0") << "\n";
        out << "showDesktopSystemSettings=" << (normalized.showDesktopSystemSettings ? "1" : "0") << "\n";
        out << "smallLiveDesktopFolderIcons=" << (normalized.smallLiveDesktopFolderIcons ? "1" : "0") << "\n";
        out << "autoArrangeDesktopIcons=" << (normalized.autoArrangeDesktopIcons ? "1" : "0") << "\n";

        const std::string serialized = out.str();
#if defined(GXOS_BARE_METAL)
        const int32_t written = kernel::vfs::write_file(path.c_str(), serialized.c_str(), static_cast<uint32_t>(serialized.size()));
        if (written < 0 || static_cast<size_t>(written) != serialized.size()) {
            err = "write fail";
            return false;
        }
#else
        std::vector<uint8_t> bytes(serialized.begin(), serialized.end());
        if (!FS::writeAll(path, bytes)) {
            err = "write fail";
            return false;
        }
#endif
        return true;
    }
};

} // namespace gui
} // namespace gxos
