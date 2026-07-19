#pragma once

#include <string>

namespace gxos {
namespace gui {

class DesktopBackgroundService {
public:
    static constexpr const char* kSetAsDesktopBackgroundAction = "SetAsDesktopBackground";

    static bool IsEligiblePngSource(const std::string& sourceVfsPath, bool isDirectory, bool isTrashItem = false);
    static bool ImportAndSetDesktopBackground(const std::string& sourceVfsPath, std::string& error);
    static bool RemoveBackground(const std::string& backgroundId, std::string& error);
};

} // namespace gui
} // namespace gxos
