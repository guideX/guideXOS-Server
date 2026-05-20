#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gxos { namespace gui {

enum class BackgroundKind {
    Image,
    Gradient,
    SolidColor
};

enum class BackgroundScaleMode {
    Fill,
    Fit,
    Stretch,
    Center,
    Tile
};

struct BackgroundEntry {
    std::string id;
    std::string displayName;
    BackgroundKind kind;
    std::string fullImagePath;
    std::string thumbnailPath;
    uint32_t topColor;
    uint32_t bottomColor;
    uint32_t accentColor;
    uint32_t solidColor;
};

struct WallpaperEntry {
    std::string id;
    std::string displayName;
    std::string fullImagePath;
    std::string thumbnailPath;
};

struct GradientEntry {
    std::string id;
    std::string displayName;
    uint32_t topColor;
    uint32_t bottomColor;
    uint32_t accentColor;
};

class WallpaperRegistry {
public:
    static const std::vector<BackgroundEntry>& BuiltInBackgrounds();
    static const std::vector<WallpaperEntry>& BuiltInWallpapers();
    static const std::vector<GradientEntry>& BuiltInGradients();
    static const BackgroundEntry* FindBackgroundById(const std::string& id);
    static const WallpaperEntry* FindById(const std::string& id);
    static const GradientEntry* FindGradientById(const std::string& id);
    static const BackgroundEntry& DefaultBackground();
    static const WallpaperEntry& DefaultWallpaper();
    static const GradientEntry& DefaultGradient();
    static std::string ResolveIdOrDefault(const std::string& id);
    static std::string ResolveAssetPathOrDefault(const std::string& id);
    static std::string ResolveThumbnailPathOrDefault(const std::string& id);
    static std::string IdForAssetPath(const std::string& assetPath);
    static bool IsGradientId(const std::string& id);
    static const char* KindName(BackgroundKind kind);
    static BackgroundScaleMode ParseScaleMode(const std::string& value);
    static const char* ScaleModeName(BackgroundScaleMode mode);
    static std::string NormalizeScaleModeOrDefault(const std::string& value);
};

} }
