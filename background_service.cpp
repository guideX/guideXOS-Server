#include "background_service.h"

#include "background_store.h"

#if !defined(GXOS_BARE_METAL)
#include "desktop_config.h"
#include "display_options_store.h"
#include "fs.h"
#include "gui_protocol.h"
#include "kernel/core/include/kernel/image_adapter.h"
#include "ipc_bus.h"
#include "logger.h"
#include "notification_manager.h"
#include "png_codec.h"
#include "vfs.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#endif

namespace gxos {
namespace gui {

#if defined(GXOS_BARE_METAL)

bool DesktopBackgroundService::IsEligiblePngSource(const std::string&, bool, bool)
{
    return false;
}

bool DesktopBackgroundService::ImportAndSetDesktopBackground(const std::string&, std::string& error)
{
    error = "user background import is hosted-only in Phase 1";
    return false;
}

bool DesktopBackgroundService::RemoveBackground(const std::string&, std::string& error)
{
    error = "user background removal is hosted-only in Phase 1";
    return false;
}

#else
namespace {

std::atomic<uint64_t> s_transactionCounter{1};

void diagnostic(const char* marker, const std::string& detail = std::string())
{
    Logger::write(LogLevel::Info, std::string("[UserBackground] marker=") + marker + (detail.empty() ? std::string() : " " + detail));
}

bool pathHasPngExtension(const std::string& path)
{
    std::filesystem::path fsPath(path);
    std::string extension = fsPath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".png";
}

bool isTrashPath(const std::string& path)
{
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.empty() || normalized.front() != '/') normalized.insert(normalized.begin(), '/');
    return normalized == "/Trash" || normalized.rfind("/Trash/", 0) == 0;
}

std::string sourceHostPath(const std::string& sourceVfsPath)
{
    std::string path = sourceVfsPath;
    std::replace(path.begin(), path.end(), '\\', '/');
    // Hosted Explorer exposes a virtual root mapped to the repository's
    // current directory.  Preserve explicit drive-qualified host paths.
    if (path.size() > 1 && path[0] == '/' && path[1] != '/') path.erase(path.begin());
    else if (!path.empty() && path[0] == '/') path.erase(path.begin());
    return path;
}

bool readRegularSource(const std::string& sourceVfsPath, std::vector<uint8_t>& bytes, std::string& error)
{
    bytes.clear();
    if (sourceVfsPath.empty() || isTrashPath(sourceVfsPath)) {
        error = "source is not a regular PNG file";
        return false;
    }
    if (Vfs::instance().readFile(sourceVfsPath, bytes)) return true;

    const std::string hostPath = sourceHostPath(sourceVfsPath);
    std::error_code ec;
    if (hostPath.empty() || !std::filesystem::is_regular_file(std::filesystem::path(hostPath), ec) || ec) {
        error = "source is not a readable regular file";
        return false;
    }
    const FSResult read = FS::readAll(hostPath, bytes, gui::DefaultImageSafetyLimits().maxBytes);
    if (!read.success) {
        error = read.message.empty() ? "source read failed" : read.message;
        return false;
    }
    return true;
}

std::string boundedDisplayName(const std::string& sourceVfsPath)
{
    std::filesystem::path source(sourceVfsPath);
    std::string name = source.filename().string();
    if (name.size() >= 4) {
        std::string extension = name.substr(name.size() - 4);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".png") name.erase(name.size() - 4);
    }
    std::string sanitized;
    sanitized.reserve(name.size());
    for (unsigned char c : name) {
        if (c < 0x20u || c == 0x7Fu) continue;
        sanitized.push_back(static_cast<char>(c));
    }
    if (sanitized.empty()) sanitized = "Custom Background";
    if (sanitized.size() > 128) sanitized.resize(128);
    return sanitized;
}

std::string transactionSuffix()
{
    return std::to_string(s_transactionCounter.fetch_add(1));
}

bool readOwnedFile(const std::string& logicalPath, std::vector<uint8_t>& bytes)
{
    const std::string hostPath = BackgroundStore::HostPathForOwned(logicalPath);
    return !hostPath.empty() && FS::readAll(hostPath, bytes);
}

bool cleanupOwnedFile(const std::string& path)
{
    if (path.empty() || !FS::removeFile(path)) {
        diagnostic("cleanup-failure", path);
        return false;
    }
    return true;
}

void publishBackgroundMessage(MsgType type, const std::string& payload)
{
    ipc::Message message;
    message.type = static_cast<uint32_t>(type);
    message.data.assign(payload.begin(), payload.end());
    ipc::Bus::publish("gui.input", std::move(message), false);
}

bool loadSelection(DisplayOptionsStoreData& store, std::string& error)
{
    if (DisplayOptionsStore::Load("display-options.cfg", store, error)) return true;

    DesktopConfigData legacy;
    std::string legacyError;
    if (DesktopConfig::Load("desktop.json", legacy, legacyError)) {
        store = DisplayOptionsStoreData{};
        store.wallpaperId = legacy.wallpaperId;
        store.backgroundScaleMode = legacy.backgroundScaleMode.empty() ? "fill" : legacy.backgroundScaleMode;
        store.desktopThemeId = legacy.desktopThemeId.empty() ? "classic" : legacy.desktopThemeId;
        store.taskbarPosition = legacy.taskbarPosition.empty() ? "bottom" : legacy.taskbarPosition;
        store.timeZoneId = legacy.timeZoneId;
        store.use24HourTime = legacy.use24HourTime;
        store.showDesktopTrash = legacy.showDesktopTrash;
        store.showDesktopThisSystem = legacy.showDesktopThisSystem;
        store.showDesktopFileManager = legacy.showDesktopFileManager;
        store.showDesktopSystemSettings = legacy.showDesktopSystemSettings;
        store.smallLiveDesktopFolderIcons = legacy.smallLiveDesktopFolderIcons;
        store.autoArrangeDesktopIcons = legacy.autoArrangeDesktopIcons;
        error.clear();
        return true;
    }
    error = legacyError.empty() ? error : legacyError;
    return false;
}

std::string currentSelectionId()
{
    DisplayOptionsStoreData store;
    std::string error;
    if (loadSelection(store, error) && !store.wallpaperId.empty()) return BackgroundStore::ResolveIdOrDefault(store.wallpaperId);
    return WallpaperRegistry::DefaultBackground().id;
}

bool persistSelection(const std::string& id, std::string& error)
{
    DisplayOptionsStoreData store;
    std::string loadError;
    (void)loadSelection(store, loadError);
    store.wallpaperId = id;
    if (!DisplayOptionsStore::Save("display-options.cfg", store, error)) return false;

    DesktopConfigData legacy;
    std::string legacyError;
    if (DesktopConfig::Load("desktop.json", legacy, legacyError)) {
        const BackgroundEntry* entry = BackgroundStore::FindById(id);
        legacy.wallpaperId = id;
        legacy.wallpaperPath = entry && entry->kind == BackgroundKind::Image ? entry->fullImagePath : std::string();
        legacyError.clear();
        if (!DesktopConfig::Save("desktop.json", legacy, legacyError)) {
            Logger::write(LogLevel::Warn, "[UserBackground] legacy selection persistence failed: " + legacyError);
        }
    }
    return true;
}

void notifySelectionChanged(const std::string& id)
{
    publishBackgroundMessage(MsgType::MT_DesktopWallpaperSet, id);
    publishBackgroundMessage(MsgType::MT_DesktopBackgroundInventoryChanged, id);
}

bool replaceManifest(const std::vector<BackgroundEntry>& records, const std::string& suffix, std::string& error)
{
    std::string manifestText;
    if (!BackgroundStore::SerializeManifest(records, manifestText, error)) return false;
    if (!FS::createDirectories("user-data\\backgrounds")) {
        error = "unable to create user background directory";
        return false;
    }
    const std::string tempPath = "user-data\\backgrounds\\.manifest-" + suffix + ".tmp";
    if (!FS::writeAll(tempPath, std::vector<uint8_t>(manifestText.begin(), manifestText.end()))) {
        error = "unable to write temporary manifest";
        return false;
    }
    std::vector<BackgroundEntry> parsed;
    std::string parseError;
    if (!BackgroundStore::ParseManifestText(manifestText, parsed, parseError) || parsed.size() != records.size()) {
        cleanupOwnedFile(tempPath);
        error = parseError.empty() ? "temporary manifest verification failed" : parseError;
        return false;
    }
    if (!FS::renameFile(tempPath, "user-data\\backgrounds\\manifest.cfg", true)) {
        cleanupOwnedFile(tempPath);
        error = "unable to replace manifest";
        return false;
    }
    return true;
}

} // namespace

bool DesktopBackgroundService::IsEligiblePngSource(const std::string& sourceVfsPath, bool isDirectory, bool isTrashItem)
{
    if (isDirectory || isTrashItem || !pathHasPngExtension(sourceVfsPath) || isTrashPath(sourceVfsPath)) return false;
    std::vector<uint8_t> bytes;
    std::string error;
    if (!readRegularSource(sourceVfsPath, bytes, error)) return false;
    const ImageBitmap image = ImageAdapter::LoadFromBytes(bytes, sourceVfsPath);
    return image.status == ImageLoadStatus::Ok;
}

bool DesktopBackgroundService::ImportAndSetDesktopBackground(const std::string& sourceVfsPath, std::string& error)
{
    error.clear();
    if (!pathHasPngExtension(sourceVfsPath) || isTrashPath(sourceVfsPath)) {
        error = "Only regular PNG files can be set as a desktop background";
        diagnostic("validation-failure", error);
        return false;
    }

    std::vector<uint8_t> sourceBytes;
    if (!readRegularSource(sourceVfsPath, sourceBytes, error)) {
        diagnostic("validation-failure", error);
        return false;
    }
    const ImageBitmap decoded = ImageAdapter::LoadFromBytes(sourceBytes, sourceVfsPath);
    if (decoded.status != ImageLoadStatus::Ok || !decoded.image) {
        error = std::string("PNG validation failed: ") + ImageLoadStatusName(decoded.status);
        diagnostic("validation-failure", error);
        return false;
    }

    std::string storeError;
    if (!BackgroundStore::Reload(storeError)) {
        error = "User background manifest could not be loaded: " + storeError;
        diagnostic("manifest-failure", error);
        return false;
    }
    const std::string contentHash = BackgroundStore::ComputeContentHash(sourceBytes);
    for (const auto& existing : BackgroundStore::UserBackgrounds()) {
        if (existing.contentHash != contentHash || existing.contentSize != sourceBytes.size()) continue;
        std::vector<uint8_t> stored;
        if (readOwnedFile(existing.fullImagePath, stored) && stored == sourceBytes) {
            if (!persistSelection(existing.id, error)) {
                diagnostic("duplicate-selection", "id=" + existing.id + " persistence-failure");
                return false;
            }
            notifySelectionChanged(existing.id);
            diagnostic("duplicate-selection", "id=" + existing.id);
            return true;
        }
        error = "content identity collision detected; import was not changed";
        diagnostic("validation-failure", error);
        return false;
    }

    const std::string id = "user_" + contentHash;
    if (BackgroundStore::FindById(id) != nullptr) {
        error = "content identity collision detected; import was not changed";
        diagnostic("validation-failure", error);
        return false;
    }

    if (!FS::createDirectories("user-data\\backgrounds")) {
        error = "Unable to create user background storage";
        diagnostic("manifest-failure", error);
        return false;
    }
    const std::string suffix = transactionSuffix();
    const std::string tempFull = "user-data\\backgrounds\\." + id + "-" + suffix + ".png.tmp";
    const std::string tempThumb = "user-data\\backgrounds\\." + id + "-" + suffix + "_thumb.png.tmp";
    const std::string finalFull = BackgroundStore::HostPathForOwned(BackgroundStore::CanonicalFullImagePath(id));
    const std::string finalThumb = BackgroundStore::HostPathForOwned(BackgroundStore::CanonicalThumbnailPath(id));
    bool fullCommitted = false;
    bool thumbCommitted = false;

    auto cleanupTemps = [&]() {
        cleanupOwnedFile(tempFull);
        cleanupOwnedFile(tempThumb);
    };

    if (!FS::writeAll(tempFull, sourceBytes)) {
        cleanupTemps();
        error = "Unable to write imported PNG";
        diagnostic("rollback", error);
        return false;
    }

    const double scale = std::min(
        static_cast<double>(kUserBackgroundThumbnailMaxWidth) / std::max(1, decoded.width),
        static_cast<double>(kUserBackgroundThumbnailMaxHeight) / std::max(1, decoded.height));
    const int thumbWidth = std::max(1, std::min(kUserBackgroundThumbnailMaxWidth, static_cast<int>(std::lround(decoded.width * std::min(1.0, scale)))));
    const int thumbHeight = std::max(1, std::min(kUserBackgroundThumbnailMaxHeight, static_cast<int>(std::lround(decoded.height * std::min(1.0, scale)))));
    const ImagePtr thumbImage = PngCodec::ScaleNearest(decoded.image, thumbWidth, thumbHeight);
    std::vector<uint8_t> thumbBytes;
    if (!thumbImage || !PngCodec::EncodeRgba8(thumbImage, thumbBytes, error) || !FS::writeAll(tempThumb, thumbBytes)) {
        cleanupTemps();
        if (error.empty()) error = "Unable to generate thumbnail";
        diagnostic("rollback", error);
        return false;
    }

    std::vector<uint8_t> verifyBytes;
    if (!FS::readAll(tempFull, verifyBytes) || verifyBytes != sourceBytes ||
        ImageAdapter::LoadFromBytes(verifyBytes, tempFull).status != ImageLoadStatus::Ok ||
        !FS::readAll(tempThumb, verifyBytes) ||
        ImageAdapter::LoadFromBytes(verifyBytes, tempThumb).status != ImageLoadStatus::Ok) {
        cleanupTemps();
        error = "temporary background verification failed";
        diagnostic("rollback", error);
        return false;
    }

    if (!FS::renameFile(tempFull, finalFull, false)) {
        cleanupTemps();
        error = "Unable to commit imported PNG";
        diagnostic("rollback", error);
        return false;
    }
    fullCommitted = true;
    if (!FS::renameFile(tempThumb, finalThumb, false)) {
        cleanupOwnedFile(finalFull);
        cleanupTemps();
        error = "Unable to commit imported thumbnail";
        diagnostic("rollback", error);
        return false;
    }
    thumbCommitted = true;

    BackgroundEntry record{};
    record.id = id;
    record.displayName = boundedDisplayName(sourceVfsPath);
    record.kind = BackgroundKind::Image;
    record.owner = BackgroundOwner::UserImported;
    record.fullImagePath = BackgroundStore::CanonicalFullImagePath(id);
    record.thumbnailPath = BackgroundStore::CanonicalThumbnailPath(id);
    record.sourceName = std::filesystem::path(sourceVfsPath).filename().string();
    if (record.sourceName.empty()) record.sourceName = "original-name.png";
    for (char& c : record.sourceName) {
        if (static_cast<unsigned char>(c) < 0x20u || static_cast<unsigned char>(c) == 0x7Fu) c = '_';
    }
    if (record.sourceName.size() > 255) record.sourceName.resize(255);
    record.contentHash = contentHash;
    record.contentSize = sourceBytes.size();
    record.topColor = 0xFF142850;
    record.bottomColor = 0xFF0F121C;
    record.accentColor = 0xFF6FA8FF;
    record.solidColor = record.topColor;

    std::vector<BackgroundEntry> nextRecords = BackgroundStore::UserBackgrounds();
    nextRecords.push_back(record);
    if (!replaceManifest(nextRecords, suffix, error)) {
        cleanupOwnedFile(finalFull);
        cleanupOwnedFile(finalThumb);
        cleanupTemps();
        diagnostic("rollback", "manifest-failure=" + error);
        return false;
    }
    std::string reloadError;
    const BackgroundEntry* verified = nullptr;
    if (!BackgroundStore::Reload(reloadError) || !(verified = BackgroundStore::FindById(id)) || verified->owner != BackgroundOwner::UserImported) {
        cleanupOwnedFile(finalFull);
        cleanupOwnedFile(finalThumb);
        diagnostic("rollback", "reload-verification-failure=" + reloadError);
        error = "Imported background could not be reloaded after manifest commit";
        return false;
    }

    if (!persistSelection(id, error)) {
        // The record is valid and durable; retaining it is safer than rolling
        // back a committed manifest.  The prior selection remains active.
        diagnostic("success", "id=" + id + " partial=selection-persistence-failed");
        error = "Background imported, but active selection persistence failed: " + error;
        return false;
    }
    notifySelectionChanged(id);
    diagnostic("success", "id=" + id + " full=" + verified->fullImagePath + " thumb=" + verified->thumbnailPath);
    (void)fullCommitted;
    (void)thumbCommitted;
    return true;
}

bool DesktopBackgroundService::RemoveBackground(const std::string& backgroundId, std::string& error)
{
    error.clear();
    if (WallpaperRegistry::FindBackgroundById(backgroundId) != nullptr) {
        error = "Built-in backgrounds cannot be removed";
        diagnostic("validation-failure", error);
        return false;
    }
    std::string reloadError;
    if (!BackgroundStore::Reload(reloadError)) {
        error = reloadError;
        diagnostic("manifest-failure", error);
        return false;
    }
    const auto& users = BackgroundStore::UserBackgrounds();
    auto it = std::find_if(users.begin(), users.end(), [&backgroundId](const BackgroundEntry& entry) { return entry.id == backgroundId; });
    if (it == users.end() || it->owner != BackgroundOwner::UserImported || it->kind != BackgroundKind::Image || !BackgroundStore::IsValidUserId(backgroundId)) {
        error = "Unknown or non-removable user background";
        diagnostic("validation-failure", error);
        return false;
    }

    const BackgroundEntry removed = *it;
    const std::string activeId = currentSelectionId();
    const bool wasActive = activeId == backgroundId;
    const std::string fallbackId = WallpaperRegistry::DefaultBackground().id;
    if (wasActive) {
        if (!persistSelection(fallbackId, error)) {
            diagnostic("rollback", "fallback-selection-failure=" + error);
            return false;
        }
        notifySelectionChanged(fallbackId);
    }

    std::vector<BackgroundEntry> replacement;
    for (const auto& record : users) {
        if (record.id != backgroundId) replacement.push_back(record);
    }
    const std::string suffix = transactionSuffix();
    if (!replaceManifest(replacement, suffix, error)) {
        if (wasActive) {
            std::string restoreError;
            if (persistSelection(activeId, restoreError)) notifySelectionChanged(activeId);
        }
        diagnostic("rollback", "manifest-replacement-failure=" + error);
        return false;
    }

    const std::string fullPath = BackgroundStore::HostPathForOwned(BackgroundStore::CanonicalFullImagePath(backgroundId));
    const std::string thumbPath = BackgroundStore::HostPathForOwned(BackgroundStore::CanonicalThumbnailPath(backgroundId));
    const bool thumbDeleted = cleanupOwnedFile(thumbPath);
    const bool fullDeleted = cleanupOwnedFile(fullPath);
    if (!thumbDeleted || !fullDeleted) {
        // Restore only while both owned files still exist and can still be
        // represented by the old valid manifest.  Never use manifest paths as
        // deletion targets.
        std::vector<uint8_t> fullBytes;
        std::vector<uint8_t> thumbBytes;
        const bool usable = readOwnedFile(removed.fullImagePath, fullBytes) && readOwnedFile(removed.thumbnailPath, thumbBytes) &&
            fullBytes.size() == removed.contentSize && BackgroundStore::ComputeContentHash(fullBytes) == removed.contentHash &&
            ImageAdapter::LoadFromBytes(fullBytes, removed.fullImagePath).status == ImageLoadStatus::Ok &&
            ImageAdapter::LoadFromBytes(thumbBytes, removed.thumbnailPath).status == ImageLoadStatus::Ok;
        std::string restoreError;
        bool restored = false;
        if (usable) {
            std::vector<BackgroundEntry> oldRecords = replacement;
            oldRecords.push_back(removed);
            restored = replaceManifest(oldRecords, transactionSuffix(), restoreError);
        }
        if (restored) {
            BackgroundStore::Reload(reloadError);
            if (wasActive) {
                std::string restoreSelectionError;
                if (persistSelection(activeId, restoreSelectionError)) notifySelectionChanged(activeId);
            }
            error = "Background removal was rolled back because owned-file deletion failed";
            diagnostic("rollback", error);
        } else {
            BackgroundStore::Reload(reloadError);
            error = "Background record removed, but owned-file cleanup failed; orphaned files remain";
            diagnostic("cleanup-failure", error);
        }
        return false;
    }

    BackgroundStore::Reload(reloadError);
    notifySelectionChanged(wasActive ? fallbackId : activeId);
    diagnostic("success", "removed=" + backgroundId + (wasActive ? " fallback=" + fallbackId : std::string()));
    return true;
}

#endif

} // namespace gui
} // namespace gxos
