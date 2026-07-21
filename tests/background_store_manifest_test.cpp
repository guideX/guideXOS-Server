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
const std::string kSecondId = "user_fedcba9876543210fedcba9876543210";
const std::string kHash = "0123456789abcdef0123456789abcdef";
const std::string kSecondHash = "fedcba9876543210fedcba9876543210";

std::string recordLines(size_t index, const std::string& id = kId,
                        const std::string& fullPath = std::string(),
                        const std::string& thumbnailPath = std::string(),
                        const std::string& displayName = "Example",
                        const std::string& hash = kHash)
{
    const std::string full = fullPath.empty() ? BackgroundStore::CanonicalFullImagePath(id) : fullPath;
    const std::string thumb = thumbnailPath.empty() ? BackgroundStore::CanonicalThumbnailPath(id) : thumbnailPath;
    return "background." + std::to_string(index) + ".id=\"" + id + "\"\n" +
        "background." + std::to_string(index) + ".displayName=\"" + displayName + "\"\n" +
        "background." + std::to_string(index) + ".kind=\"image\"\n" +
        "background." + std::to_string(index) + ".owner=\"user\"\n" +
        "background." + std::to_string(index) + ".fullImagePath=\"" + full + "\"\n" +
        "background." + std::to_string(index) + ".thumbnailPath=\"" + thumb + "\"\n" +
        "background." + std::to_string(index) + ".sourceName=\"original-name.png\"\n" +
        "background." + std::to_string(index) + ".contentHash=\"" + hash + "\"\n" +
        "background." + std::to_string(index) + ".contentSize=\"12\"\n";
}

std::string validManifest(const std::string& body = recordLines(0))
{
    const size_t count = body.empty() ? 0 : 1;
    return "version=1\ncount=" + std::to_string(count) + "\n" + body;
}

bool expect(bool condition, const char* label)
{
    if (!condition) std::cerr << "FAIL: " << label << "\n";
    return condition;
}

bool rejects(const std::string& manifest, const char* label)
{
    std::vector<BackgroundEntry> records;
    std::string error;
    return expect(!BackgroundStore::ParseManifestText(manifest, records, error) && records.empty(), label);
}

} // namespace

int main()
{
    bool ok = true;
    std::vector<BackgroundEntry> records;
    std::string error;

    ok &= expect(!BackgroundStore::ParseManifestText("", records, error), "empty manifest rejected");
    ok &= expect(BackgroundStore::ParseManifestText("version=1\ncount=0\n", records, error) && records.empty(), "empty record manifest");
    ok &= expect(BackgroundStore::ParseManifestText(validManifest(), records, error) && records.size() == 1,
                 "valid one-record manifest");
    if (records.size() == 1) {
        ok &= expect(records[0].owner == BackgroundOwner::UserImported, "user ownership");
        ok &= expect(records[0].kind == BackgroundKind::Image, "image kind");
        ok &= expect(records[0].fullImagePath == BackgroundStore::CanonicalFullImagePath(kId), "derived full path");
        ok &= expect(records[0].thumbnailPath == BackgroundStore::CanonicalThumbnailPath(kId), "derived thumbnail path");
    }

    ok &= rejects("version=2\ncount=0\n", "unsupported version");
    ok &= rejects("version=1\ncount=184467440737095516160\n", "count overflow");
    ok &= rejects("version=1\ncount=-1\n", "negative count");
    ok &= rejects(std::string(gxos::gui::kMaxUserBackgroundManifestBytes + 1, 'x'), "oversized manifest");
    ok &= rejects("version=1\ncount=65\n", "more than 64 entries");

    const std::string duplicateFields = "version=1\nversion=1\ncount=0\n";
    ok &= rejects(duplicateFields, "duplicate fields rejected");
    ok &= rejects("version=1\ncount=0\nunknown=value\n", "unknown fields rejected");
    ok &= rejects("version=1\ncount=0\nbackground.0.id=\"orphan\"\n", "more records than declared rejected");
    ok &= rejects("version=1\ncount=1\n", "declared record absent rejected");

    const std::string duplicateId = "version=1\ncount=2\n" + recordLines(0) + recordLines(1, kId, std::string(), std::string(), "Duplicate", kHash);
    ok &= rejects(duplicateId, "duplicate ID rejected");
    ok &= rejects(validManifest(recordLines(0, "legacy_blue_flower")), "built-in collision rejected");
    ok &= rejects(validManifest(recordLines(0, kId, "/user-data/backgrounds/../outside.png")), "traversal path rejected");
    ok &= rejects(validManifest(recordLines(0, kId, "/user-data/backgrounds-evil/file.png")), "prefix confusion path rejected");
    ok &= rejects(validManifest(recordLines(0, kId, "\\user-data\\backgrounds\\..\\outside.png")), "mixed separator traversal rejected");
    ok &= rejects(validManifest(recordLines(0, kId, "/user-data/backgrounds/.")), "dot path rejected");
    ok &= rejects(validManifest(recordLines(0, kId, "/user-data/backgrounds/", BackgroundStore::CanonicalThumbnailPath(kId))), "missing image rejected");
    ok &= rejects(validManifest(recordLines(0, kId, BackgroundStore::CanonicalFullImagePath(kId), "/user-data/backgrounds/")), "missing thumbnail rejected");
    ok &= rejects(validManifest(recordLines(0, kId, BackgroundStore::CanonicalFullImagePath(kId), BackgroundStore::CanonicalThumbnailPath(kId), "Bad\x01Name")),
                 "embedded control character rejected");
    ok &= rejects("version=1\ncount=1\nbackground.0.id=\"unterminated\n", "malformed quoted value rejected");
    ok &= rejects("version=1\ncount=1\n" + recordLines(0).substr(0, recordLines(0).size() - 1), "truncated final line rejected");

    const std::string malformedThenValid = "version=1\ncount=2\n" +
        recordLines(0, kId, "/outside/file.png") + recordLines(1, kSecondId, std::string(), std::string(), "Second", kSecondHash);
    ok &= rejects(malformedThenValid, "valid record after malformed record does not bypass rejection");

    const std::string reordered = std::string("version=1\ncount=1\n") +
        "background.0.contentSize=\"12\"\n" +
        "background.0.thumbnailPath=\"" + BackgroundStore::CanonicalThumbnailPath(kId) + "\"\n" +
        "background.0.owner=\"user\"\n" +
        "background.0.id=\"" + kId + "\"\n" +
        "background.0.sourceName=\"original-name.png\"\n" +
        "background.0.kind=\"image\"\n" +
        "background.0.contentHash=\"" + kHash + "\"\n" +
        "background.0.fullImagePath=\"" + BackgroundStore::CanonicalFullImagePath(kId) + "\"\n" +
        "background.0.displayName=\"Example\"\n";
    ok &= expect(BackgroundStore::ParseManifestText(reordered, records, error) && records.size() == 1,
                 "reordered valid fields accepted");

    BackgroundEntry serializable{};
    serializable.id = kSecondId;
    serializable.displayName = "Example = \\\"two\\\" / test";
    serializable.kind = BackgroundKind::Image;
    serializable.owner = BackgroundOwner::UserImported;
    serializable.sourceName = "original=name\\file.png";
    serializable.contentHash = kSecondHash;
    serializable.contentSize = 12;
    std::string serialized;
    std::string serializedAgain;
    ok &= expect(BackgroundStore::SerializeManifest({serializable}, serialized, error), "serialize valid record");
    ok &= expect(serialized.find("thumbnailPath=\"/user-data/backgrounds/" + kSecondId + "_thumb.png\"") != std::string::npos,
                 "serializer derives thumbnail path");
    ok &= expect(BackgroundStore::ParseManifestText(serialized, records, error) &&
        BackgroundStore::SerializeManifest(records, serializedAgain, error) && serialized == serializedAgain,
        "stable serialize-parse-serialize output");

    ok &= expect(BackgroundStore::HostPathForOwned("/outside/file.png").empty(), "out-of-root host path rejected");
    ok &= expect(BackgroundStore::HostPathForOwned("/user-data/backgrounds-evil/file.png").empty(), "host prefix confusion rejected");
    ok &= expect(BackgroundStore::HostPathForOwned("/user-data/backgrounds/../file.png").empty(), "host traversal rejected");
    ok &= expect(BackgroundStore::HostPathForOwned("/user-data/backgrounds\\..\\file.png").empty(), "host mixed traversal rejected");
    ok &= expect(BackgroundStore::HostPathForOwned("/user-data/backgrounds/.").empty(), "host dot path rejected");
    ok &= expect(BackgroundStore::HostPathForOwned(BackgroundStore::CanonicalFullImagePath(kId)).find(kId + ".png") != std::string::npos,
                 "canonical owned path accepted");
    ok &= expect(!BackgroundStore::IsValidUserId("user_short") && !BackgroundStore::IsValidUserId("user_0123456789abcdef0123456789abcdefx"),
                 "user ID length validation");
    ok &= expect(!BackgroundStore::IsValidUserId("user_0123456789abcdef0123456789abcde!"), "user ID character validation");

    const std::vector<uint8_t> hashInput = { 0x00, 0x01, 0x7F, 0x80, 0xFF };
    const std::string firstHash = BackgroundStore::ComputeContentHash(hashInput);
    ok &= expect(firstHash == BackgroundStore::ComputeContentHash(hashInput) && firstHash.size() == 32,
                 "deterministic bounded content hash");
    std::cout << "BackgroundStore hash=" << firstHash << "\n";
    std::cout << (ok ? "BackgroundStore manifest tests PASS\n" : "BackgroundStore manifest tests FAIL\n");
    return ok ? 0 : 1;
}

