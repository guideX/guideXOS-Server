#pragma once

#include "wallpaper_registry.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace gui {

// Logical paths are stable across hosted restarts and are the contract the
// later bare-metal implementation will consume.
static constexpr const char* kUserBackgroundRoot = "/user-data/backgrounds/";
static constexpr const char* kUserBackgroundManifestPath = "/user-data/backgrounds/manifest.cfg";
static constexpr size_t kUserBackgroundManifestVersion = 1;
static constexpr size_t kMaxUserBackgroundRecords = 64;
static constexpr size_t kMaxUserBackgroundManifestBytes = 64u * 1024u;
static constexpr size_t kMaxUserBackgroundManifestLineBytes = 2048u;
static constexpr int kUserBackgroundThumbnailMaxWidth = 160;
static constexpr int kUserBackgroundThumbnailMaxHeight = 120;

class BackgroundStore {
public:
    static bool Reload(std::string& error);
    static const std::vector<BackgroundEntry>& MergedBackgrounds();
    static const std::vector<BackgroundEntry>& MergedImageBackgrounds();
    static const std::vector<BackgroundEntry>& UserBackgrounds();
    static const BackgroundEntry* FindById(const std::string& id);
    static std::string ResolveIdOrDefault(const std::string& id);

    static std::string CanonicalFullImagePath(const std::string& id);
    static std::string CanonicalThumbnailPath(const std::string& id);
    static std::string HostStorageDirectory();
    static std::string HostPathForOwned(const std::string& logicalPath);
    static bool IsValidUserId(const std::string& id);
    static std::string ComputeContentHash(const std::vector<uint8_t>& bytes);

    // These two helpers are intentionally public so focused hosted smoke
    // tests can validate the bounded manifest contract without mutating the
    // live inventory.
    static bool ParseManifestText(const std::string& text, std::vector<BackgroundEntry>& records, std::string& error);
    static bool SerializeManifest(const std::vector<BackgroundEntry>& records, std::string& text, std::string& error);

private:
    static bool LoadManifestFromDisk(std::vector<BackgroundEntry>& records, std::string& error);
    static bool ValidateOwnedRecordFiles(BackgroundEntry& record, std::string& error);
    static void EnsureLoaded();
    static void RebuildMerged(const std::vector<BackgroundEntry>& records);
};

} // namespace gui
} // namespace gxos

