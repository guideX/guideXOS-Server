#pragma once

#include "image.h"
#include <string>
#include <unordered_map>

#ifdef LoadIcon
#undef LoadIcon
#endif

namespace gxos {
namespace gui {

class IconThemeManager {
public:
    static IconThemeManager& Instance();

    bool IconsEnabled() const;
    void SetIconsEnabled(bool enabled);

    const std::string& CurrentThemeName() const;
    void SetCurrentThemeName(const std::string& themeName);

    int ResolveBestSize(int requestedSize) const;
    std::string ResolveIconPath(const std::string& logicalIconName, int requestedSize) const;
    ImagePtr LoadIcon(const std::string& logicalIconName, int requestedSize);

private:
    IconThemeManager();

    const char* ResolveFilename(const std::string& logicalIconName) const;
    std::string BuildPath(const char* filename, int size) const;
    ImagePtr LoadIconFromPath(const std::string& path);
    void LogMissingIcon(const std::string& logicalIconName, const std::string& detail) const;

    bool m_iconsEnabled;
    std::string m_currentThemeName;
    std::unordered_map<std::string, const char*> m_manifest;
    std::unordered_map<std::string, ImagePtr> m_cache;
};

} // namespace gui
} // namespace gxos
