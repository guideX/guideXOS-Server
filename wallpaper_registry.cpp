#include "wallpaper_registry.h"

#include <algorithm>

namespace gxos { namespace gui {
namespace {

const std::vector<BackgroundEntry>& backgroundRegistry()
{
    static const std::vector<BackgroundEntry> entries = {
        { "legacy_blue_flower", "Blue Flower", BackgroundKind::Image, "/system/wallpapers/blueflower.png", "/system/wallpapers/blueflower_thumb.png", 0xFF11336B, 0xFF0A1124, 0xFF6FA8FF, 0xFF11336B },
        { "legacy_dinos", "Dinos", BackgroundKind::Image, "/system/wallpapers/dinos.png", "/system/wallpapers/dinos_thumb.png", 0xFF315A25, 0xFF121C10, 0xFFA9D76B, 0xFF315A25 },
        { "legacy_flower", "Flower", BackgroundKind::Image, "/system/wallpapers/flower.png", "/system/wallpapers/flower_thumb.png", 0xFF2C5364, 0xFF0F2027, 0xFF9BD7E4, 0xFF2C5364 },
        { "legacy_guidexos_space", "guideXOS Space", BackgroundKind::Image, "/system/wallpapers/guidexosspace.png", "/system/wallpapers/guidexosspace_thumb.png", 0xFF112B63, 0xFF070B1C, 0xFF4D8DFF, 0xFF112B63 },
        { "legacy_red_flower", "Red Flower", BackgroundKind::Image, "/system/wallpapers/redflower.png", "/system/wallpapers/redflower_thumb.png", 0xFF5B1115, 0xFF120508, 0xFFFF5B68, 0xFF5B1115 },
        { "legacy_ameoba", "Ameoba", BackgroundKind::Image, "/system/wallpapers/ameoba.png", "/system/wallpapers/ameoba_thumb.png", 0xFF39196B, 0xFF11091F, 0xFF9057EA, 0xFF39196B },
        { "legacy_ameobagx", "Ameoba GX", BackgroundKind::Image, "/system/wallpapers/ameobagx.png", "/system/wallpapers/ameobagx_thumb.png", 0xFF3A1762, 0xFF10091D, 0xFFFF4FC4, 0xFF3A1762 },
        { "legacy_tron_porsche", "Tron Porsche", BackgroundKind::Image, "/system/wallpapers/tronporche.png", "/system/wallpapers/tronporche_thumb.png", 0xFF073645, 0xFF070B12, 0xFF18C7DF, 0xFF073645 },
        { "legacy_wallpaper2", "Wallpaper 2", BackgroundKind::Image, "/system/wallpapers/Wallpaper2.png", "/system/wallpapers/Wallpaper2_thumb.png", 0xFF100E35, 0xFF8C145F, 0xFFFF52B0, 0xFF100E35 },
        { "gradient_midnight", "Midnight", BackgroundKind::Gradient, "", "", 0xFF142850, 0xFF0F121C, 0xFF192337, 0xFF142850 },
        { "gradient_ocean", "Ocean", BackgroundKind::Gradient, "", "", 0xFF063B5C, 0xFF061522, 0xFF1496B8, 0xFF063B5C },
        { "gradient_aurora", "Aurora", BackgroundKind::Gradient, "", "", 0xFF0B2C35, 0xFF251046, 0xFF21C78A, 0xFF0B2C35 },
        { "gradient_violet", "Violet", BackgroundKind::Gradient, "", "", 0xFF26104A, 0xFF0D0B18, 0xFF8A52E8, 0xFF26104A },
        { "gradient_sunset", "Sunset", BackgroundKind::Gradient, "", "", 0xFF5E1B45, 0xFF17101E, 0xFFE06A55, 0xFF5E1B45 },
        { "gradient_forest", "Forest", BackgroundKind::Gradient, "", "", 0xFF123B2B, 0xFF071711, 0xFF5E9C50, 0xFF123B2B },
        { "gradient_ember", "Ember", BackgroundKind::Gradient, "", "", 0xFF45170F, 0xFF120B09, 0xFFD46A33, 0xFF45170F },
        { "gradient_graphite", "Graphite", BackgroundKind::Gradient, "", "", 0xFF333946, 0xFF111318, 0xFF7E8796, 0xFF333946 },
    };
    return entries;
}

const std::vector<WallpaperEntry>& imageRegistry()
{
    static const std::vector<WallpaperEntry> entries = [] {
        std::vector<WallpaperEntry> images;
        for (const auto& entry : backgroundRegistry()) {
            if (entry.kind == BackgroundKind::Image) {
                images.push_back({ entry.id, entry.displayName, entry.fullImagePath, entry.thumbnailPath });
            }
        }
        return images;
    }();
    return entries;
}

const std::vector<GradientEntry>& gradientRegistry()
{
    static const std::vector<GradientEntry> entries = [] {
        std::vector<GradientEntry> gradients;
        for (const auto& entry : backgroundRegistry()) {
            if (entry.kind == BackgroundKind::Gradient) {
                gradients.push_back({ entry.id, entry.displayName, entry.topColor, entry.bottomColor, entry.accentColor });
            }
        }
        return gradients;
    }();
    return entries;
}

std::string normalizePath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

} // namespace

const std::vector<BackgroundEntry>& WallpaperRegistry::BuiltInBackgrounds()
{
    return backgroundRegistry();
}

const std::vector<WallpaperEntry>& WallpaperRegistry::BuiltInWallpapers()
{
    return imageRegistry();
}

const std::vector<GradientEntry>& WallpaperRegistry::BuiltInGradients()
{
    return gradientRegistry();
}

const BackgroundEntry* WallpaperRegistry::FindBackgroundById(const std::string& id)
{
    const auto& entries = backgroundRegistry();
    auto it = std::find_if(entries.begin(), entries.end(), [&id](const BackgroundEntry& entry) {
        return entry.id == id;
    });
    return it == entries.end() ? nullptr : &(*it);
}

const WallpaperEntry* WallpaperRegistry::FindById(const std::string& id)
{
    const auto& entries = imageRegistry();
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
    return imageRegistry().front();
}

const BackgroundEntry& WallpaperRegistry::DefaultBackground()
{
    return backgroundRegistry().front();
}

const GradientEntry& WallpaperRegistry::DefaultGradient()
{
    return gradientRegistry().front();
}

std::string WallpaperRegistry::ResolveIdOrDefault(const std::string& id)
{
    return FindBackgroundById(id) ? id : DefaultBackground().id;
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
    for (const auto& entry : imageRegistry()) {
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

const char* WallpaperRegistry::KindName(BackgroundKind kind)
{
    switch (kind) {
    case BackgroundKind::Image: return "image";
    case BackgroundKind::Gradient: return "gradient";
    case BackgroundKind::SolidColor: return "solid";
    default: return "unknown";
    }
}

BackgroundScaleMode WallpaperRegistry::ParseScaleMode(const std::string& value)
{
    if (value == "fit" || value == "contain") return BackgroundScaleMode::Fit;
    if (value == "stretch") return BackgroundScaleMode::Stretch;
    if (value == "center") return BackgroundScaleMode::Center;
    if (value == "tile") return BackgroundScaleMode::Tile;
    return BackgroundScaleMode::Fill;
}

const char* WallpaperRegistry::ScaleModeName(BackgroundScaleMode mode)
{
    switch (mode) {
    case BackgroundScaleMode::Fill: return "fill";
    case BackgroundScaleMode::Fit: return "fit";
    case BackgroundScaleMode::Stretch: return "stretch";
    case BackgroundScaleMode::Center: return "center";
    case BackgroundScaleMode::Tile: return "tile";
    default: return "fill";
    }
}

std::string WallpaperRegistry::NormalizeScaleModeOrDefault(const std::string& value)
{
    return ScaleModeName(ParseScaleMode(value));
}

} }
