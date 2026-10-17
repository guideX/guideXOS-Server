#include "kernel/core/include/kernel/system_font_selection.h"
#include "kernel/core/include/kernel/font_contract.h"
#include "kernel/core/include/kernel/vfs.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace gxos::gui;

struct TestFile {
	std::vector<uint8_t> bytes;
	kernel::vfs::FileType type{kernel::vfs::FILE_TYPE_REGULAR};
};

std::map<std::string, TestFile> g_files;
std::vector<kernel::vfs::DirEntry> g_directoryEntries;
uint32_t g_directoryCursor = 0;
bool g_directoryPresent = true;
bool g_failWrite = false;
bool g_failRead = false;
bool g_failRename = false;
int g_redrawCount = 0;
int g_checks = 0;
int g_failures = 0;
std::vector<uint8_t> g_fixture;
std::vector<uint8_t> g_replacementFixture;

#define CHECK(expression) do { \
	++g_checks; \
	if (!(expression)) { \
		std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); \
		++g_failures; \
	} \
} while (0)

std::string lowerPath(const char* path)
{
	std::string result = path ? path : "";
	std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
		return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
	});
	return result;
}

std::vector<uint8_t> readFixture()
{
	std::ifstream input("assets/Fonts/roboto/Roboto-Regular.ttf", std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

std::vector<uint8_t> readReplacementFixture()
{
	std::ifstream input("assets/Fonts/roboto/Roboto-Italic.ttf", std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

void addFile(const char* name, const std::vector<uint8_t>& bytes)
{
	TestFile file;
	file.bytes = bytes;
	g_files[lowerPath((std::string("/system/fonts/") + name).c_str())] = file;
}

void addInvalidFile(const char* name)
{
	addFile(name, std::vector<uint8_t>{'n', 'o', 't', ' ', 'a', ' ', 'f', 'o', 'n', 't'});
}

void rebuildDirectoryEntries()
{
	g_directoryEntries.clear();
	const std::string prefix = "/system/fonts/";
	for (const auto& pair : g_files) {
		if (pair.first.compare(0u, prefix.size(), prefix) != 0) continue;
		const std::string name = pair.first.substr(prefix.size());
		if (name.find('/') != std::string::npos) continue;
		kernel::vfs::DirEntry entry{};
		const size_t amount = name.size() < sizeof(entry.name) - 1u ? name.size() : sizeof(entry.name) - 1u;
		for (size_t i = 0u; i < amount; ++i) {
			const char c = name[i];
			entry.name[i] = c >= 'a' && c <= 'z' ? static_cast<char>(c - ('a' - 'A')) : c;
		}
		entry.name[amount] = '\0';
		entry.type = pair.second.type;
		entry.size = pair.second.bytes.size();
		g_directoryEntries.push_back(entry);
	}
	std::reverse(g_directoryEntries.begin(), g_directoryEntries.end());
}

void resetFixtureStore()
{
	g_files.clear();
	g_directoryPresent = true;
	g_failWrite = false;
	g_failRead = false;
	g_failRename = false;
	g_redrawCount = 0;
	g_directoryCursor = 0u;
}

int32_t drawToBuffer(FontHandle font, uint32_t size)
{
	uint32_t pixels[64u * 64u];
	for (uint32_t i = 0u; i < 64u * 64u; ++i) pixels[i] = 0xff101820u;
	return static_cast<int32_t>(FontDrawGrayscaleTextToBuffer(font, {size}, pixels,
	sizeof(pixels), 64u * sizeof(uint32_t), 64u, 64u, 2, 24, "Ag", 2u,
	0xffffffffu, nullptr));
}

FontHandle handleFor(const SystemFontIdentity& identity)
{
	FontHandle handle = {0xffffu, 0u, 0u};
	FontManagerResolveFont({identity.stableId}, &handle);
	return handle;
}

std::string encodeHex32(uint32_t value)
{
	static const char digits[] = "0123456789abcdef";
	std::string output(8u, '0');
	for (int32_t i = 7; i >= 0; --i) {
		output[static_cast<size_t>(i)] = digits[value & 0xfu];
		value >>= 4;
	}
	return output;
}

void markPreferenceVersionUnsupported(const std::string& path)
{
	auto found = g_files.find(lowerPath(path.c_str()));
	if (found == g_files.end() || found->second.bytes.size() != 1024u) return;
	std::vector<uint8_t>& bytes = found->second.bytes;
	const char version[] = "format=2";
	for (uint32_t i = 0u; i < sizeof(version) - 1u; ++i) bytes[i] = static_cast<uint8_t>(version[i]);
	uint32_t checksumOffset = 0u;
	while (checksumOffset + 9u < bytes.size() &&
		std::memcmp(bytes.data() + checksumOffset, "checksum=", 9u) != 0) ++checksumOffset;
	if (checksumOffset + 17u >= bytes.size()) return;
	uint32_t checksum = 2166136261u;
	for (uint32_t i = 0u; i < checksumOffset; ++i) {
		checksum ^= bytes[i];
		checksum *= 16777619u;
	}
	const std::string hex = encodeHex32(checksum);
	for (uint32_t i = 0u; i < 8u; ++i) bytes[checksumOffset + 9u + i] = static_cast<uint8_t>(hex[i]);
}

void checkActiveAndConfigured(const SystemFontIdentity& identity, uint32_t size)
{
	SystemFontSelectionSnapshot active{};
	SystemFontConfiguredPreference configured{};
	CHECK(SystemFontSelectionGetActive(&active) == SystemFontSelectionStatus::Ok);
	CHECK(SystemFontSelectionGetConfigured(&configured) == SystemFontSelectionStatus::Ok);
	CHECK(active.identity.stableId == identity.stableId);
	CHECK(active.pixelSize == size);
	CHECK(active.isPersisted == 1u);
	CHECK(configured.identity.stableId == identity.stableId);
	CHECK(configured.pixelSize == size);
	CHECK(configured.isPersisted == 1u);
}

void testDiscoverySelectionAndRollback()
{
	resetFixtureStore();
	addFile("A-Regular.ttf", g_fixture);
	addFile("B-Regular.ttf", g_fixture); // Same family, independent path identity.
	addFile("C-Regular.ttf", g_fixture);
	addFile("D-Regular.ttf", g_fixture); // Fourth valid source exceeds fixed registry capacity.
	addInvalidFile("broken.ttf");
	addFile("unsupported.otf", g_fixture);
	rebuildDirectoryEntries();
	CHECK(SystemFontSelectionInitialize() == SystemFontSelectionStatus::Ok);
	SystemFontFaceInfo faces[kSystemFontMaxAvailableFonts]{};
	uint32_t count = 0u;
	CHECK(SystemFontSelectionGetAvailable(faces, kSystemFontMaxAvailableFonts, &count) ==
		SystemFontSelectionStatus::Ok);
	CHECK(count == kSystemFontMaxAvailableFonts);
	CHECK(std::strcmp(faces[0].metadata.familyName, faces[1].metadata.familyName) == 0);
	CHECK(faces[0].identity.stableId != faces[1].identity.stableId);
	CHECK(faces[0].identity.contentFingerprint != 0u);
	SystemFontSelectionSnapshot active{};
	CHECK(SystemFontSelectionGetActive(&active) == SystemFontSelectionStatus::Ok);
	CHECK(active.identity.kind == SystemFontIdentityKind::BuiltinBitmap);
	CHECK(active.pixelSize == kBitmapFontPixelHeight);

	SystemFontIdentity first = faces[0].identity;
	SystemFontIdentity second = faces[1].identity;
	SystemFontCandidate candidate{};
	CHECK(SystemFontSelectionPrepare(&first, 16u, &candidate) == SystemFontSelectionStatus::Ok);
	SystemFontCandidate repeatedCandidate{};
	const SystemFontSelectionStatus repeatedPrepare = SystemFontSelectionPrepare(&first, 16u, &repeatedCandidate);
	CHECK(repeatedPrepare == SystemFontSelectionStatus::Ok);
	candidate = repeatedCandidate;
	const uint32_t beforeGeneration = active.selectionGeneration;
	const int redrawBeforeApply = g_redrawCount;
	const SystemFontSelectionStatus firstApply = SystemFontSelectionApply(&candidate);
	CHECK(firstApply == SystemFontSelectionStatus::Ok);
	checkActiveAndConfigured(first, 16u);
	CHECK(g_redrawCount == redrawBeforeApply + 1);
	CHECK(SystemFontSelectionGetActive(&active) == SystemFontSelectionStatus::Ok);
	CHECK(active.selectionGeneration > beforeGeneration);

	FontHandle firstHandle = handleFor(first);
	CHECK(drawToBuffer(firstHandle, 16u) == static_cast<int32_t>(FontStatus::Ok));
	FontGlyphCacheStatistics cacheBefore{};
	CHECK(FontGlyphCacheGetStatistics(&cacheBefore) == FontStatus::Ok);
	CHECK(SystemFontSelectionPrepareActiveSize(24u, &candidate) == SystemFontSelectionStatus::Ok);
	CHECK(SystemFontSelectionApply(&candidate) == SystemFontSelectionStatus::Ok);
	checkActiveAndConfigured(first, 24u);
	CHECK(drawToBuffer(firstHandle, 24u) == static_cast<int32_t>(FontStatus::Ok));
	FontGlyphCacheStatistics cacheAfter{};
	CHECK(FontGlyphCacheGetStatistics(&cacheAfter) == FontStatus::Ok);
	CHECK(cacheAfter.misses >= cacheBefore.misses + 2u);
	CHECK(SystemFontSelectionRevert() == SystemFontSelectionStatus::Ok);
	checkActiveAndConfigured(first, 16u);

	g_failRead = true;
	CHECK(SystemFontSelectionPrepare(&second, 20u, &candidate) ==
		SystemFontSelectionStatus::PreparationFailed);
	g_failRead = false;
	CHECK(SystemFontSelectionPrepare(&second, 20u, &candidate) == SystemFontSelectionStatus::Ok);
	g_failWrite = true;
	const int redrawBeforeFailure = g_redrawCount;
	CHECK(SystemFontSelectionApply(&candidate) == SystemFontSelectionStatus::PersistenceFailed);
	g_failWrite = false;
	checkActiveAndConfigured(first, 16u);
	CHECK(g_redrawCount == redrawBeforeFailure + 1);
	CHECK(FontManagerGetDefaultFont(&firstHandle) == FontStatus::Ok);
	CHECK(firstHandle.slot == handleFor(first).slot &&
		firstHandle.generation == handleFor(first).generation);

	CHECK(SystemFontSelectionPrepare(&second, 20u, &candidate) == SystemFontSelectionStatus::Ok);
	g_failRename = true;
	CHECK(SystemFontSelectionApply(&candidate) == SystemFontSelectionStatus::PersistenceFailed);
	g_failRename = false;
	checkActiveAndConfigured(first, 16u);

	FontGlyphCacheStatistics beforeFontSwitch{};
	CHECK(FontGlyphCacheGetStatistics(&beforeFontSwitch) == FontStatus::Ok);
	CHECK(SystemFontSelectionPrepare(&second, 20u, &candidate) == SystemFontSelectionStatus::Ok);
	CHECK(SystemFontSelectionApply(&candidate) == SystemFontSelectionStatus::Ok);
	checkActiveAndConfigured(second, 20u);
	CHECK(drawToBuffer(handleFor(second), 20u) == static_cast<int32_t>(FontStatus::Ok));
	FontGlyphCacheStatistics afterFontSwitch{};
	CHECK(FontGlyphCacheGetStatistics(&afterFontSwitch) == FontStatus::Ok);
	CHECK(afterFontSwitch.misses > beforeFontSwitch.misses);
	CHECK(SystemFontSelectionRevert() == SystemFontSelectionStatus::Ok);
	checkActiveAndConfigured(first, 16u);

	SystemFontIdentity tampered = second;
	tampered.stableId ^= 1u;
	CHECK(SystemFontSelectionPrepare(&tampered, 16u, &candidate) == SystemFontSelectionStatus::NotFound);
	CHECK(SystemFontSelectionPrepare(&second, 7u, &candidate) == SystemFontSelectionStatus::SizeOutOfRange);
	CHECK(SystemFontSelectionPrepare(&second, 129u, &candidate) == SystemFontSelectionStatus::SizeOutOfRange);

	// A replacement at the same path gets a new fingerprint and cannot reuse
	// the prepared identity or runtime generation.
	const std::vector<uint8_t> original = g_files[lowerPath("/system/fonts/a-regular.ttf")].bytes;
	g_files[lowerPath("/system/fonts/a-regular.ttf")].bytes = g_replacementFixture;
	CHECK(SystemFontSelectionPrepare(&first, 16u, &candidate) == SystemFontSelectionStatus::StaleIdentity);
	g_files[lowerPath("/system/fonts/a-regular.ttf")].bytes = original;

	CHECK(SystemFontSelectionResetToBuiltin() == SystemFontSelectionStatus::Ok);
	FontHandle secondHandle = handleFor(second);
	CHECK(FontManagerUnloadFont(secondHandle) == FontStatus::Ok);
	CHECK(SystemFontSelectionPrepare(&second, 16u, &candidate) ==
		SystemFontSelectionStatus::PreparationFailed);
	const SystemFontSelectionStatus reloadStatus = SystemFontSelectionRefreshCatalog();
	CHECK(reloadStatus == SystemFontSelectionStatus::RegistryCapacity ||
		reloadStatus == SystemFontSelectionStatus::InvalidFont);
	FontHandle reloaded = handleFor(second);
	CHECK(reloaded.slot != 0xffffu);
	CHECK(reloaded.generation != secondHandle.generation);
	CHECK(SystemFontSelectionResetToBuiltin() == SystemFontSelectionStatus::Ok);
}

void testRestoreAndMissingFontFallback()
{
	SystemFontSelectionShutdown();
	resetFixtureStore();
	addFile("Roboto-Regular.ttf", g_fixture);
	rebuildDirectoryEntries();
	CHECK(SystemFontSelectionInitialize() == SystemFontSelectionStatus::Ok);
	SystemFontFaceInfo faces[kSystemFontMaxAvailableFonts]{};
	uint32_t count = 0u;
	CHECK(SystemFontSelectionGetAvailable(faces, kSystemFontMaxAvailableFonts, &count) ==
		SystemFontSelectionStatus::Ok);
	CHECK(count == 1u);
	const SystemFontIdentity identity = faces[0].identity;
	SystemFontCandidate candidate{};
	CHECK(SystemFontSelectionPrepare(&identity, 18u, &candidate) == SystemFontSelectionStatus::Ok);
	CHECK(SystemFontSelectionApply(&candidate) == SystemFontSelectionStatus::Ok);
	checkActiveAndConfigured(identity, 18u);
	SystemFontSelectionShutdown();
	CHECK(SystemFontSelectionInitialize() == SystemFontSelectionStatus::Ok);
	checkActiveAndConfigured(identity, 18u);

	// The active source is copied into manager-owned storage: removing the VFS
	// file does not invalidate a draw in this boot, while a restart falls back.
	FontHandle activeHandle = handleFor(identity);
	CHECK(drawToBuffer(activeHandle, 18u) == static_cast<int32_t>(FontStatus::Ok));
	g_files.erase(lowerPath("/system/fonts/roboto-regular.ttf"));
	rebuildDirectoryEntries();
	CHECK(SystemFontSelectionRefreshCatalog() == SystemFontSelectionStatus::Ok);
	CHECK(drawToBuffer(activeHandle, 18u) == static_cast<int32_t>(FontStatus::Ok));
	SystemFontSelectionShutdown();
	CHECK(SystemFontSelectionInitialize() == SystemFontSelectionStatus::Ok);
	SystemFontSelectionSnapshot active{};
	CHECK(SystemFontSelectionGetActive(&active) == SystemFontSelectionStatus::Ok);
	CHECK(active.identity.kind == SystemFontIdentityKind::BuiltinBitmap);
	CHECK(active.pixelSize == kBitmapFontPixelHeight);
	CHECK(SystemFontSelectionPreferenceStatus() == SystemFontSelectionStatus::NotFound);
	SystemFontConfiguredPreference configured{};
	CHECK(SystemFontSelectionGetConfigured(&configured) == SystemFontSelectionStatus::NotFound);
	CHECK(configured.identity.kind == SystemFontIdentityKind::TrueType);
	CHECK(configured.identity.stableId == identity.stableId);
}

void testCorruptAndUnsupportedPreferences()
{
	SystemFontSelectionShutdown();
	resetFixtureStore();
	addFile("Roboto-Regular.ttf", g_fixture);
	rebuildDirectoryEntries();
	CHECK(SystemFontSelectionInitialize() == SystemFontSelectionStatus::Ok);
	SystemFontFaceInfo faces[kSystemFontMaxAvailableFonts]{};
	uint32_t count = 0u;
	CHECK(SystemFontSelectionGetAvailable(faces, kSystemFontMaxAvailableFonts, &count) ==
		SystemFontSelectionStatus::Ok);
	SystemFontCandidate candidate{};
	CHECK(SystemFontSelectionPrepare(&faces[0].identity, 20u, &candidate) == SystemFontSelectionStatus::Ok);
	CHECK(SystemFontSelectionApply(&candidate) == SystemFontSelectionStatus::Ok);
	CHECK(SystemFontSelectionPrepareActiveSize(22u, &candidate) == SystemFontSelectionStatus::Ok);
	CHECK(SystemFontSelectionApply(&candidate) == SystemFontSelectionStatus::Ok);
	SystemFontSelectionShutdown();
	markPreferenceVersionUnsupported("/fontsel0.cfg");
	markPreferenceVersionUnsupported("/fontsel1.cfg");
	CHECK(SystemFontSelectionInitialize() == SystemFontSelectionStatus::Ok);
	CHECK(SystemFontSelectionPreferenceStatus() == SystemFontSelectionStatus::PreferenceUnsupported);
	SystemFontSelectionSnapshot active{};
	CHECK(SystemFontSelectionGetActive(&active) == SystemFontSelectionStatus::Ok);
	CHECK(active.identity.kind == SystemFontIdentityKind::BuiltinBitmap);
	SystemFontSelectionShutdown();
	for (const char* path : {"/fontsel0.cfg", "/fontsel1.cfg"}) {
		TestFile corrupted;
		corrupted.bytes.assign(1024u, static_cast<uint8_t>('x'));
		g_files[lowerPath(path)] = corrupted;
	}
	CHECK(SystemFontSelectionInitialize() == SystemFontSelectionStatus::Ok);
	CHECK(SystemFontSelectionPreferenceStatus() == SystemFontSelectionStatus::PreferenceCorrupt);
	CHECK(SystemFontSelectionGetActive(&active) == SystemFontSelectionStatus::Ok);
	CHECK(active.identity.kind == SystemFontIdentityKind::BuiltinBitmap);
	CHECK(SystemFontSelectionResetToBuiltin() == SystemFontSelectionStatus::Ok);
}

void testReplacementCapacityAndLegacyFallback()
{
	SystemFontSelectionShutdown();
	resetFixtureStore();
	addFile("A.ttf", g_fixture);
	addFile("B.ttf", g_fixture);
	addFile("C.ttf", g_fixture);
	addFile("D.ttf", g_fixture);
	rebuildDirectoryEntries();
	CHECK(SystemFontSelectionInitialize() == SystemFontSelectionStatus::Ok);
	SystemFontFaceInfo faces[kSystemFontMaxAvailableFonts]{};
	uint32_t count = 0u;
	CHECK(SystemFontSelectionGetAvailable(faces, kSystemFontMaxAvailableFonts, &count) ==
		SystemFontSelectionStatus::Ok);
	CHECK(count == 3u);
	SystemFontIdentity identity = faces[0].identity;
	SystemFontCandidate candidate{};
	CHECK(SystemFontSelectionPrepare(&identity, 16u, &candidate) == SystemFontSelectionStatus::Ok);
	CHECK(SystemFontSelectionApply(&candidate) == SystemFontSelectionStatus::Ok);

	g_files[lowerPath("/system/fonts/a.ttf")].bytes = g_replacementFixture;
	CHECK(SystemFontSelectionPrepare(&identity, 16u, &candidate) == SystemFontSelectionStatus::StaleIdentity);
	const SystemFontSelectionStatus activeReplacementStatus = SystemFontSelectionRefreshCatalog();
	CHECK(activeReplacementStatus == SystemFontSelectionStatus::StaleIdentity ||
		activeReplacementStatus == SystemFontSelectionStatus::RegistryCapacity);
	CHECK(SystemFontSelectionResetToBuiltin() == SystemFontSelectionStatus::Ok);
	const SystemFontSelectionStatus refreshed = SystemFontSelectionRefreshCatalog();
	CHECK(refreshed == SystemFontSelectionStatus::Ok || refreshed == SystemFontSelectionStatus::RegistryCapacity);
	CHECK(SystemFontSelectionGetAvailable(faces, kSystemFontMaxAvailableFonts, &count) ==
		SystemFontSelectionStatus::Ok);
	CHECK(count == 3u);
	bool foundReplacement = false;
	for (uint32_t i = 0u; i < count; ++i)
		if (faces[i].identity.canonicalPath[14] == 'A' &&
			faces[i].identity.contentFingerprint != identity.contentFingerprint)
			foundReplacement = true;
	CHECK(foundReplacement);

	SystemFontSelectionShutdown();
	resetFixtureStore();
	g_directoryPresent = false;
	CHECK(SystemFontSelectionInitialize() == SystemFontSelectionStatus::Ok);
	CHECK(SystemFontSelectionRefreshCatalog() == SystemFontSelectionStatus::DirectoryUnavailable);
	SystemFontSelectionSnapshot active{};
	CHECK(SystemFontSelectionGetActive(&active) == SystemFontSelectionStatus::Ok);
	CHECK(active.identity.kind == SystemFontIdentityKind::BuiltinBitmap);
}

void testDirectoryTraversalBound()
{
	SystemFontSelectionShutdown();
	resetFixtureStore();
	addFile("a-font.ttf", g_fixture);
	for (uint32_t i = 0u; i < kSystemFontMaxDiscoveredEntries + 1u; ++i) {
		char name[32] = {};
		std::snprintf(name, sizeof(name), "z%03u.txt", i);
		addFile(name, g_fixture);
	}
	rebuildDirectoryEntries();
	CHECK(SystemFontSelectionInitialize() == SystemFontSelectionStatus::Ok);
	CHECK(SystemFontSelectionRefreshCatalog() == SystemFontSelectionStatus::DiscoveryLimitReached);
	SystemFontFaceInfo faces[kSystemFontMaxAvailableFonts]{};
	uint32_t count = 0u;
	CHECK(SystemFontSelectionGetAvailable(faces, kSystemFontMaxAvailableFonts, &count) ==
		SystemFontSelectionStatus::Ok);
	CHECK(count == 0u);
}

} // namespace

namespace kernel {
namespace desktop {
void request_redraw() { ++g_redrawCount; }
}
namespace vfs {

Status stat(const char* path, FileInfo* info)
{
	if (!path || !info) return VFS_ERR_INVALID;
	const std::string normalized = lowerPath(path);
	if (normalized == "/system/fonts" || normalized == "/system/fonts/") {
		if (!g_directoryPresent) return VFS_ERR_NOT_FOUND;
		info->type = FILE_TYPE_DIRECTORY;
		info->size = 0u;
		return VFS_OK;
	}
	const auto found = g_files.find(normalized);
	if (found == g_files.end()) return VFS_ERR_NOT_FOUND;
	info->type = found->second.type;
	info->size = found->second.bytes.size();
	return VFS_OK;
}

int32_t read_file(const char* path, void* buffer, uint32_t maxSize)
{
	if (!path || (!buffer && maxSize != 0u) || g_failRead) return VFS_ERR_IO;
	const auto found = g_files.find(lowerPath(path));
	if (found == g_files.end()) return VFS_ERR_NOT_FOUND;
	if (found->second.bytes.size() > maxSize) return VFS_ERR_INVALID;
	if (!found->second.bytes.empty())
		std::memcpy(buffer, found->second.bytes.data(), found->second.bytes.size());
	return static_cast<int32_t>(found->second.bytes.size());
}

int32_t write_file(const char* path, const void* buffer, uint32_t size)
{
	if (!path || (!buffer && size != 0u)) return VFS_ERR_INVALID;
	if (g_failWrite) return VFS_ERR_IO;
	TestFile file;
	const uint8_t* bytes = static_cast<const uint8_t*>(buffer);
	file.bytes.assign(bytes, bytes + size);
	g_files[lowerPath(path)] = file;
	return static_cast<int32_t>(size);
}

int32_t create_file(const char* path, const void* buffer, uint32_t size)
{
	if (!path || (!buffer && size != 0u)) return VFS_ERR_INVALID;
	const std::string normalized = lowerPath(path);
	if (g_files.find(normalized) != g_files.end()) return VFS_ERR_EXISTS;
	return write_file(path, buffer, size);
}

Status unlink(const char* path)
{
	if (!path) return VFS_ERR_INVALID;
	const Status result = g_files.erase(lowerPath(path)) ? VFS_OK : VFS_ERR_NOT_FOUND;
	return result;
}

Status rename(const char* oldPath, const char* newPath)
{
	if (!oldPath || !newPath) return VFS_ERR_INVALID;
	if (g_failRename) return VFS_ERR_IO;
	const std::string oldName = lowerPath(oldPath);
	const std::string newName = lowerPath(newPath);
	const auto old = g_files.find(oldName);
	if (old == g_files.end()) return VFS_ERR_NOT_FOUND;
	if (g_files.find(newName) != g_files.end()) return VFS_ERR_EXISTS;
	TestFile file = old->second;
	g_files.erase(old);
	g_files[newName] = file;
	return VFS_OK;
}

uint8_t opendir(const char* path)
{
	if (!path || lowerPath(path) != "/system/fonts" || !g_directoryPresent) return 0xffu;
	rebuildDirectoryEntries();
	g_directoryCursor = 0u;
	return 0u;
}

bool readdir(uint8_t iterator, DirEntry* entry)
{
	if (iterator != 0u || !entry || g_directoryCursor >= g_directoryEntries.size()) return false;
	*entry = g_directoryEntries[g_directoryCursor++];
	return true;
}

void closedir(uint8_t iterator)
{
	(void)iterator;
}

} // namespace vfs
} // namespace kernel

int main()
{
	g_fixture = readFixture();
	g_replacementFixture = readReplacementFixture();
	if (g_fixture.empty()) {
		std::fprintf(stderr, "Could not read Roboto TrueType fixture.\n");
		return 2;
	}
	if (g_replacementFixture.empty()) {
		std::fprintf(stderr, "Could not read replacement TrueType fixture.\n");
		return 2;
	}
	testDiscoverySelectionAndRollback();
	testRestoreAndMissingFontFallback();
	testCorruptAndUnsupportedPreferences();
	testReplacementCapacityAndLegacyFallback();
	testDirectoryTraversalBound();
	SystemFontSelectionShutdown();
	std::printf("FONT6 system font selection: %d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
