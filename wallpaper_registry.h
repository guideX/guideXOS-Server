#pragma once

#include <string>
#include <vector>

namespace gxos { namespace gui {

struct WallpaperEntry {
    std::string id;
    std::string displayName;
    std::string assetPath;
    std::string thumbnailPath;
};

class WallpaperRegistry {
public:
    static const std::vector<WallpaperEntry>& BuiltInWallpapers();
    static const WallpaperEntry* FindById(const std::string& id);
    static const WallpaperEntry& DefaultWallpaper();
    static std::string ResolveIdOrDefault(const std::string& id);
    static std::string ResolveAssetPathOrDefault(const std::string& id);
    static std::string ResolveThumbnailPathOrDefault(const std::string& id);
    static std::string IdForAssetPath(const std::string& assetPath);
};

} }
