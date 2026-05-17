#include "icon_theme_manager.h"
#include "logger.h"
#include "png_loader.h"

#include <cstdlib>
#include <limits>

namespace gxos {
namespace gui {

static const int kAvailableIconSizes[] = {32, 48, 64, 72, 96, 128, 256};

IconThemeManager& IconThemeManager::Instance()
{
    static IconThemeManager instance;
    return instance;
}

IconThemeManager::IconThemeManager()
    : m_iconsEnabled(true)
    , m_currentThemeName("Flat")
{
    m_manifest.emplace("app.notepad", "27-Edit_Text_256x256_35395.png");
    m_manifest.emplace("app.files", "25-Folder_256x256_35390.png");
    m_manifest.emplace("app.settings", "11-Preferences_256x256_35370.png");
    m_manifest.emplace("app.controlpanel", "11-Preferences_256x256_35370.png");
    m_manifest.emplace("app.taskmanager", "15-Dashboard__256x256_35400.png");
    m_manifest.emplace("app.diskmanager", "41-Macintosh_HD_256x256_35377.png");
    m_manifest.emplace("app.installer", "18-CD_256x256_35401.png");
    m_manifest.emplace("app.paint", "7-Image_capture_256x256_35382.png");
    m_manifest.emplace("app.clock", "47-_iCal_256x256_35384.png");
    m_manifest.emplace("app.console", "29-Generic_256x256_35387.png");
    m_manifest.emplace("app.calculator", "15-Dashboard__256x256_35400.png");
    m_manifest.emplace("app.generic", "31-Document_256x256_35398.png");
    m_manifest.emplace("place.computer", "1_My_Computer_256x256_35372.png");
    m_manifest.emplace("place.home", "1_-_Home_256x256_35385.png");
    m_manifest.emplace("place.documents", "32-Documents_256x256_35397.png");
    m_manifest.emplace("place.pictures", "7-Image_capture_256x256_35382.png");
    m_manifest.emplace("place.music", "22-iTUnes_256x256_35379.png");
    m_manifest.emplace("place.network", "39-Globe_256x256_35386.png");
    m_manifest.emplace("file.folder", "25-Folder_256x256_35390.png");
    m_manifest.emplace("file.sysfolder", "12-Desktop_256x256_35399.png");
    m_manifest.emplace("file.text", "33-TXT_256x256_35365.png");
    m_manifest.emplace("file.image", "7-Image_capture_256x256_35382.png");
    m_manifest.emplace("file.binary", "29-Generic_256x256_35387.png");
    m_manifest.emplace("file.generic", "31-Document_256x256_35398.png");
    m_manifest.emplace("file.unknown", "34-Unknown__256x256_35364.png");
    m_manifest.emplace("drive.fixed", "41-Macintosh_HD_256x256_35377.png");
    m_manifest.emplace("drive.mounted", "36-Removable_HD_256x256_35368.png");
    m_manifest.emplace("drive.usb", "38-USB_HD_256x256_35363.png");
    m_manifest.emplace("trash.empty", "24-Empty_Trash_256x256_35394.png");
    m_manifest.emplace("trash.full", "23-Full_Trash_256x256_35388.png");
}

bool IconThemeManager::IconsEnabled() const
{
    return m_iconsEnabled;
}

void IconThemeManager::SetIconsEnabled(bool enabled)
{
    m_iconsEnabled = enabled;
}

const std::string& IconThemeManager::CurrentThemeName() const
{
    return m_currentThemeName;
}

void IconThemeManager::SetCurrentThemeName(const std::string& themeName)
{
    m_currentThemeName = themeName.empty() ? "Flat" : themeName;
}

int IconThemeManager::ResolveBestSize(int requestedSize) const
{
    int bestSize = kAvailableIconSizes[0];
    int bestDistance = std::numeric_limits<int>::max();
    for (int size : kAvailableIconSizes) {
        int distance = std::abs(size - requestedSize);
        if (distance < bestDistance || (distance == bestDistance && size > bestSize)) {
            bestSize = size;
            bestDistance = distance;
        }
    }
    return bestSize;
}

std::string IconThemeManager::ResolveIconPath(const std::string& logicalIconName, int requestedSize) const
{
    const char* filename = ResolveFilename(logicalIconName);
    if (!filename) {
        LogMissingIcon(logicalIconName, "manifest entry not found");
        filename = ResolveFilename(logicalIconName.find("app.") == 0 ? "app.generic" : "file.unknown");
        if (!filename) filename = ResolveFilename("file.unknown");
    }

    return filename ? BuildPath(filename, ResolveBestSize(requestedSize)) : std::string();
}

ImagePtr IconThemeManager::LoadIcon(const std::string& logicalIconName, int requestedSize)
{
    if (!m_iconsEnabled) return nullptr;

    std::string path = ResolveIconPath(logicalIconName, requestedSize);
    ImagePtr image = LoadIconFromPath(path);
    if (image) return image;

    LogMissingIcon(logicalIconName, path.empty() ? "resolved path is empty" : path);

    const bool appIcon = logicalIconName.find("app.") == 0;
    const char* fallbackName = appIcon ? "app.generic" : "file.unknown";
    std::string fallbackPath = ResolveIconPath(fallbackName, requestedSize);
    if (fallbackPath != path) {
        image = LoadIconFromPath(fallbackPath);
        if (image) return image;
    }

    if (appIcon) {
        fallbackPath = ResolveIconPath("file.unknown", requestedSize);
        if (fallbackPath != path) return LoadIconFromPath(fallbackPath);
    }

    return nullptr;
}

const char* IconThemeManager::ResolveFilename(const std::string& logicalIconName) const
{
    auto it = m_manifest.find(logicalIconName);
    return it == m_manifest.end() ? nullptr : it->second;
}

std::string IconThemeManager::BuildPath(const char* filename, int size) const
{
    return std::string("assets/Images/") + m_currentThemeName + "/" + std::to_string(size) + "/" + filename;
}

ImagePtr IconThemeManager::LoadIconFromPath(const std::string& path)
{
    if (path.empty()) return nullptr;

    auto it = m_cache.find(path);
    if (it != m_cache.end()) return it->second;

    ImagePtr image = PngLoader::LoadFromFile(path);
    if (image) m_cache.emplace(path, image);
    return image;
}

void IconThemeManager::LogMissingIcon(const std::string& logicalIconName, const std::string& detail) const
{
    Logger::write(LogLevel::Warn, std::string("IconThemeManager: missing icon '") + logicalIconName + "' (" + detail + ")");
}

} // namespace gui
} // namespace gxos
