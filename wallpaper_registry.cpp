#include "wallpaper_registry.h"

#include <algorithm>

namespace gxos { namespace gui {
namespace {

const std::vector<WallpaperEntry>& registry()
{
    static const std::vector<WallpaperEntry> entries = {
        { "legacy_blue_flower", "Blue Flower", "assets/Backgrounds/blueflower.png", "assets/Backgrounds/blueflower_thumb.png" },
        { "legacy_dinos", "Dinos", "assets/Backgrounds/dinos.png", "assets/Backgrounds/dinos_thumb.png" },
        { "legacy_flower", "Flower", "assets/Backgrounds/flower.png", "assets/Backgrounds/flower_thumb.png" },
        { "legacy_guidexos_space", "guideXOS Space", "assets/Backgrounds/guidexosspace.png", "assets/Backgrounds/guidexosspace_thumb.png" },
        { "legacy_red_flower", "Red Flower", "assets/Backgrounds/redflower.png", "assets/Backgrounds/redflower_thumb.png" },
        { "legacy_ameoba", "Ameoba", "assets/Backgrounds/ameoba.png", "assets/Backgrounds/ameoba_thumb.png" },
        { "legacy_ameobagx", "Ameoba GX", "assets/Backgrounds/ameobagx.png", "assets/Backgrounds/ameobagx_thumb.png" },
        { "legacy_tron_porsche", "Tron Porsche", "assets/Backgrounds/tronporche.png", "assets/Backgrounds/tronporche_thumb.png" },
        { "legacy_wallpaper2", "Wallpaper 2", "assets/Backgrounds/Wallpaper2.png", "assets/Backgrounds/Wallpaper2_thumb.png" },
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

const WallpaperEntry* WallpaperRegistry::FindById(const std::string& id)
{
    const auto& entries = registry();
    auto it = std::find_if(entries.begin(), entries.end(), [&id](const WallpaperEntry& entry) {
        return entry.id == id;
    });
    return it == entries.end() ? nullptr : &(*it);
}

const WallpaperEntry& WallpaperRegistry::DefaultWallpaper()
{
    return registry().front();
}

std::string WallpaperRegistry::ResolveIdOrDefault(const std::string& id)
{
    return FindById(id) ? id : DefaultWallpaper().id;
}

std::string WallpaperRegistry::ResolveAssetPathOrDefault(const std::string& id)
{
    const WallpaperEntry* entry = FindById(id);
    return entry ? entry->assetPath : DefaultWallpaper().assetPath;
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
        if (normalizePath(entry.assetPath) == normalized || normalizePath(entry.thumbnailPath) == normalized) {
            return entry.id;
        }
    }
    return std::string();
}

} }
