#include "background_store.h"
#include "wallpaper_registry.h"

#include <iostream>
#include <string>
#include <vector>

using gxos::gui::BackgroundEntry;
using gxos::gui::BackgroundKind;
using gxos::gui::BackgroundOwner;
using gxos::gui::BackgroundStore;

namespace {

const std::string kId = "user_0123456789abcdef0123456789abcdef";
const std::string kHash = "0123456789abcdef0123456789abcdef";

std::string validManifest(const std::string& id = kId,
                          const std::string& fullPath = BackgroundStore::CanonicalFullImagePath(kId),
                          const std::string& thumbnailPath = BackgroundStore::CanonicalThumbnailPath(kId),
                          const std::string& kind = "image",
                          const std::string& owner = "user")
{
    return std::string("version=1\ncount=1\n") +
        "background.0.id=\"" + id + "\"\n" +
        "background.0.displayName=\"Example\"\n" +
        "background.0.kind=\"" + kind + "\"\n" +
        "background.0.owner=\"" + owner + "\"\n" +
        "background.0.fullImagePath=\"" + fullPath + "\"\n" +
        "background.0.thumbnailPath=\"" + thumbnailPath + "\"\n" +
        "background.0.sourceName=\"original-name.png\"\n" +
        "background.0.contentHash=\"" + kHash + "\"\n" +
        "background.0.contentSize=\"12\"\n";
}

bool expect(bool condition, const char* label)
{
    if (!condition) std::cerr << "FAIL: " << label << "\n";
    return condition;
}

} // namespace

int main()
{
    bool ok = true;
    std::vector<BackgroundEntry> records;
    std::string error;

    ok &= expect(BackgroundStore::ParseManifestText("version=1\ncount=0\n", records, error) && records.empty(), "empty manifest");
    ok &= expect(BackgroundStore::ParseManifestText(validManifest(), records, error) && records.size() == 1,
                 "valid one-record manifest");
    if (records.size() == 1) {
        ok &= expect(records[0].owner == BackgroundOwner::UserImported, "user ownership");
        ok &= expect(records[0].kind == BackgroundKind::Image, "image kind");
        ok &= expect(records[0].thumbnailPath == BackgroundStore::CanonicalThumbnailPath(kId), "derived thumbnail path");
    }

    ok &= expect(!BackgroundStore::ParseManifestText("version=2\ncount=0\n", records, error), "unsupported version");
    ok &= expect(!BackgroundStore::ParseManifestText(std::string(gxos::gui::kMaxUserBackgroundManifestBytes + 1, 'x'), records, error),
                 "oversized manifest");
    ok &= expect(!BackgroundStore::ParseManifestText("version=1\ncount=65\n", records, error), "more than 64 entries");
    std::string duplicateManifest = validManifest();
    duplicateManifest.replace(0, std::string("version=1\ncount=1\n").size(), "version=1\ncount=2\n");
    duplicateManifest += "background.1.id=\"" + kId + "\"\n" +
        "background.1.displayName=\"Duplicate\"\n" +
        "background.1.kind=\"image\"\n" +
        "background.1.owner=\"user\"\n" +
        "background.1.fullImagePath=\"" + BackgroundStore::CanonicalFullImagePath(kId) + "\"\n" +
        "background.1.thumbnailPath=\"" + BackgroundStore::CanonicalThumbnailPath(kId) + "\"\n" +
        "background.1.sourceName=\"duplicate.png\"\n" +
        "background.1.contentHash=\"" + kHash + "\"\n" +
        "background.1.contentSize=\"12\"\n";
    ok &= expect(BackgroundStore::ParseManifestText(duplicateManifest, records, error) && records.size() == 1,
                 "duplicate ID ignored safely");
    ok &= expect(BackgroundStore::ParseManifestText(validManifest(kId, "/outside/full.png"), records, error) && records.empty(),
                 "out-of-root path ignored");
    ok &= expect(BackgroundStore::ParseManifestText(validManifest(kId, BackgroundStore::CanonicalFullImagePath(kId),
                                                                  "/user-data/backgrounds/" + kId + ".png"),
                                                     records, error) && records.empty(),
                 "thumbnail path typo rejected");
    ok &= expect(BackgroundStore::ParseManifestText(validManifest(kId, BackgroundStore::CanonicalFullImagePath(kId),
                                                                  BackgroundStore::CanonicalThumbnailPath(kId), "gradient"),
                                                     records, error) && records.empty(),
                 "non-image kind ignored");
    ok &= expect(BackgroundStore::ParseManifestText(validManifest(kId, BackgroundStore::CanonicalFullImagePath(kId),
                                                                  BackgroundStore::CanonicalThumbnailPath(kId), "image", "builtin"),
                                                     records, error) && records.empty(),
                 "non-user ownership ignored");
    ok &= expect(BackgroundStore::ParseManifestText("version=1\ncount=1\nbackground.0.id=\"unterminated\n", records, error) && records.empty(),
                 "malformed quoted values ignored safely");
    ok &= expect(BackgroundStore::HostPathForOwned("/outside/file.png").empty(), "out-of-root host path rejected");

    BackgroundEntry serializable{};
    serializable.id = kId;
    serializable.displayName = "Example";
    serializable.kind = BackgroundKind::Image;
    serializable.owner = BackgroundOwner::UserImported;
    serializable.sourceName = "original-name.png";
    serializable.contentHash = kHash;
    serializable.contentSize = 12;
    std::string serialized;
    ok &= expect(BackgroundStore::SerializeManifest({serializable}, serialized, error), "serialize valid record");
    ok &= expect(serialized.find("thumbnailPath=\"/user-data/backgrounds/" + kId + "_thumb.png\"") != std::string::npos,
                 "serialized thumbnail points to thumb file");

    std::cout << (ok ? "BackgroundStore manifest tests PASS\n" : "BackgroundStore manifest tests FAIL\n");
    return ok ? 0 : 1;
}
