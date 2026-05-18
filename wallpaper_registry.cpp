#include "wallpaper_registry.h"

#include <algorithm>

namespace gxos { namespace gui {
namespace {

const std::vector<WallpaperEntry>& registry()
{
    static const std::vector<WallpaperEntry> entries = {
        { "legacy_blue_flower", "Blue Flower", "/system/wallpapers/blueflower.png", "/system/wallpapers/blueflower_thumb.png" },
        { "legacy_dinos", "Dinos", "/system/wallpapers/dinos.png", "/system/wallpapers/dinos_thumb.png" },
        { "legacy_flower", "Flower", "/system/wallpapers/flower.png", "/system/wallpapers/flower_thumb.png" },
        { "legacy_guidexos_space", "guideXOS Space", "/system/wallpapers/guidexosspace.png", "/system/wallpapers/guidexosspace_thumb.png" },
        { "legacy_red_flower", "Red Flower", "/system/wallpapers/redflower.png", "/system/wallpapers/redflower_thumb.png" },
        { "legacy_ameoba", "Ameoba", "/system/wallpapers/ameoba.png", "/system/wallpapers/ameoba_thumb.png" },
        { "legacy_ameobagx", "Ameoba GX", "/system/wallpapers/ameobagx.png", "/system/wallpapers/ameobagx_thumb.png" },
        { "legacy_tron_porsche", "Tron Porsche", "/system/wallpapers/tronporche.png", "/system/wallpapers/tronporche_thumb.png" },
        { "legacy_wallpaper2", "Wallpaper 2", "/system/wallpapers/Wallpaper2.png", "/system/wallpapers/Wallpaper2_thumb.png" },
    };
    return entries;
}

const std::vector<GradientEntry>& gradientRegistry()
{
    static const std::vector<GradientEntry> entries = {
        { "gradient_midnight", "Midnight", 0xFF142850, 0xFF0F121C, 0xFF192337 },
        { "gradient_ocean", "Ocean", 0xFF063B5C, 0xFF061522, 0xFF1496B8 },
        { "gradient_aurora", "Aurora", 0xFF0B2C35, 0xFF251046, 0xFF21C78A },
        { "gradient_violet", "Violet", 0xFF26104A, 0xFF0D0B18, 0xFF8A52E8 },
        { "gradient_sunset", "Sunset", 0xFF5E1B45, 0xFF17101E, 0xFFE06A55 },
        { "gradient_forest", "Forest", 0xFF123B2B, 0xFF071711, 0xFF5E9C50 },
        { "gradient_ember", "Ember", 0xFF45170F, 0xFF120B09, 0xFFD46A33 },
        { "gradient_graphite", "Graphite", 0xFF333946, 0xFF111318, 0xFF7E8796 },
    };
    return entries;
}

std::string normalizePath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

} // namespace

const std::vector<WallpaperEntry>& WallpaperRegistry::BuiltInWallpapers()
{
    return registry();
}

const std::vector<GradientEntry>& WallpaperRegistry::BuiltInGradients()
{
    return gradientRegistry();
}

const WallpaperEntry* WallpaperRegistry::FindById(const std::string& id)
{
    const auto& entries = registry();
    auto it = std::find_if(entries.begin(), entries.end(), [&id](const WallpaperEntry& entry) {
        return entry.id == id;
    });
    return it == entries.end() ? nullptr : &(*it);
}

const GradientEntry* WallpaperRegistry::FindGradientById(const std::string& id)
{
    const auto& entries = gradientRegistry();
    auto it = std::find_if(entries.begin(), entries.end(), [&id](const GradientEntry& entry) {
        return entry.id == id;
    });
    return it == entries.end() ? nullptr : &(*it);
}

const WallpaperEntry& WallpaperRegistry::DefaultWallpaper()
{
    return registry().front();
}

const GradientEntry& WallpaperRegistry::DefaultGradient()
{
    return gradientRegistry().front();
}

std::string WallpaperRegistry::ResolveIdOrDefault(const std::string& id)
{
    if (FindGradientById(id)) return id;
    return FindById(id) ? id : DefaultWallpaper().id;
}

std::string WallpaperRegistry::ResolveAssetPathOrDefault(const std::string& id)
{
    const WallpaperEntry* entry = FindById(id);
    return entry ? entry->fullImagePath : DefaultWallpaper().fullImagePath;
}

std::string WallpaperRegistry::ResolveThumbnailPathOrDefault(const std::string& id)
{
    const WallpaperEntry* entry = FindById(id);
    return entry ? entry->thumbnailPath : DefaultWallpaper().thumbnailPath;
}

std::string WallpaperRegistry::IdForAssetPath(const std::string& assetPath)
{
    std::string normalized = normalizePath(assetPath);
    for (const auto& entry : registry()) {
        if (normalizePath(entry.fullImagePath) == normalized || normalizePath(entry.thumbnailPath) == normalized) {
            return entry.id;
        }
    }
    return std::string();
}

bool WallpaperRegistry::IsGradientId(const std::string& id)
{
    return FindGradientById(id) != nullptr;
}

} }
