#include "kernel/core/include/kernel/font_contract.h"
#include "kernel/core/include/kernel/font_truetype.h"
#include "kernel/core/include/kernel/vfs.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;
const std::vector<uint8_t>* g_vfsFixture = nullptr;
bool g_vfsFixtureExists = true;
bool g_vfsReportTooLarge = false;
bool g_vfsReportEmpty = false;
bool g_vfsReportTruncated = false;

#define CHECK(expression) do { \
	++g_checks; \
	if (!(expression)) { \
		++g_failures; \
		std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); \
	} \
} while (0)

uint16_t be16(const std::vector<uint8_t>& bytes, size_t offset)
{
	return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8) |
		bytes[offset + 1u]);
}

uint32_t be32(const std::vector<uint8_t>& bytes, size_t offset)
{
	return (static_cast<uint32_t>(bytes[offset]) << 24) |
		(static_cast<uint32_t>(bytes[offset + 1u]) << 16) |
		(static_cast<uint32_t>(bytes[offset + 2u]) << 8) |
		bytes[offset + 3u];
}

void put16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value)
{
	bytes[offset] = static_cast<uint8_t>(value >> 8);
	bytes[offset + 1u] = static_cast<uint8_t>(value);
}

void put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
	bytes[offset] = static_cast<uint8_t>(value >> 24);
	bytes[offset + 1u] = static_cast<uint8_t>(value >> 16);
	bytes[offset + 2u] = static_cast<uint8_t>(value >> 8);
	bytes[offset + 3u] = static_cast<uint8_t>(value);
}

size_t tableEntry(const std::vector<uint8_t>& bytes, const char tag[5])
{
	const uint16_t count = be16(bytes, 4u);
	const uint32_t expected = (static_cast<uint32_t>(tag[0]) << 24) |
		(static_cast<uint32_t>(tag[1]) << 16) |
		(static_cast<uint32_t>(tag[2]) << 8) | static_cast<uint32_t>(tag[3]);
	for (uint16_t i = 0; i < count; ++i) {
		const size_t entry = 12u + static_cast<size_t>(i) * 16u;
		if (be32(bytes, entry) == expected) return entry;
	}
	return bytes.size();
}

std::vector<uint8_t> readFixture()
{
	std::ifstream input("assets/Fonts/roboto/Roboto-Regular.ttf", std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

gxos::gui::FontStatus mockMetrics(const void*, gxos::gui::FontSize,
	gxos::gui::FontMetrics* out)
{
	if (!out) return gxos::gui::FontStatus::InvalidArgument;
	*out = {7u, 7u, 0u, 0u, 7u, 7u};
	return gxos::gui::FontStatus::Ok;
}

gxos::gui::FontStatus mockGlyph(const void*, gxos::gui::UnicodeCodePoint,
	gxos::gui::FontGlyphView* out)
{
	if (out) *out = {};
	return gxos::gui::FontStatus::UnsupportedCodePoint;
}

const gxos::gui::FontBackend kMockBackend = {
	gxos::gui::FontCapabilityGlyphLookup | gxos::gui::FontCapabilityMeasurement,
	&mockMetrics,
	&mockGlyph,
};

void testParser(const std::vector<uint8_t>& fixture)
{
	using namespace gxos::gui;
	FontTrueTypeMetadata metadata = {};
	CHECK(!fixture.empty());
	CHECK(FontTrueTypeValidate(fixture.data(), fixture.size(), &metadata) == FontTrueTypeOk);
	CHECK(std::strcmp(metadata.familyName, "Roboto") == 0);
	CHECK(std::strcmp(metadata.styleName, "Regular") == 0);
	CHECK(metadata.unitsPerEm >= 16u && metadata.glyphCount > 0u);
	CHECK(metadata.tableCount <= kFontTrueTypeMaxTables && metadata.faceCount == 1u);
	CHECK(metadata.numberOfHorizontalMetrics > 0u);
	CHECK(metadata.cmapFlags != 0u);
	CHECK(metadata.fileBytes == fixture.size());

	CHECK(FontTrueTypeValidate(nullptr, 0, &metadata) == FontTrueTypeInvalidArgument);
	CHECK(FontTrueTypeValidate(fixture.data(), 0, &metadata) == FontTrueTypeInvalidArgument);
	std::vector<uint8_t> truncated(fixture.begin(), fixture.begin() + 8);
	CHECK(FontTrueTypeValidate(truncated.data(), truncated.size(), &metadata) ==
		FontTrueTypeInvalidOffsetTable);
	std::vector<uint8_t> wrong = fixture;
	wrong[0] = 'b';
	CHECK(FontTrueTypeValidate(wrong.data(), wrong.size(), &metadata) ==
		FontTrueTypeUnsupportedSignature);

	std::vector<uint8_t> corruptOffset = fixture;
	put32(corruptOffset, 20u, 0xfffffff0u);
	put32(corruptOffset, 24u, 0x80u);
	CHECK(FontTrueTypeValidate(corruptOffset.data(), corruptOffset.size(), &metadata) ==
		FontTrueTypeInvalidTableRange);
	std::vector<uint8_t> overflow = fixture;
	put32(overflow, 20u, 0xfffffffcu);
	put32(overflow, 24u, 0xfffffff0u);
	CHECK(FontTrueTypeValidate(overflow.data(), overflow.size(), &metadata) ==
		FontTrueTypeInvalidTableRange);
	std::vector<uint8_t> tooManyTables = fixture;
	put16(tooManyTables, 4u, static_cast<uint16_t>(kFontTrueTypeMaxTables + 1u));
	CHECK(FontTrueTypeValidate(tooManyTables.data(), tooManyTables.size(), &metadata) ==
		FontTrueTypeTooManyTables);

	std::vector<uint8_t> missing = fixture;
	const size_t headEntry = tableEntry(missing, "head");
	CHECK(headEntry < missing.size());
	missing[headEntry] = 'x';
	CHECK(FontTrueTypeValidate(missing.data(), missing.size(), &metadata) ==
		FontTrueTypeMissingRequiredTable);

	std::vector<uint8_t> duplicate = fixture;
	duplicate[28u] = duplicate[12u];
	duplicate[29u] = duplicate[13u];
	duplicate[30u] = duplicate[14u];
	duplicate[31u] = duplicate[15u];
	CHECK(FontTrueTypeValidate(duplicate.data(), duplicate.size(), &metadata) ==
		FontTrueTypeDuplicateTable);

	std::vector<uint8_t> invalidName = fixture;
	const size_t nameEntry = tableEntry(invalidName, "name");
	CHECK(nameEntry < invalidName.size());
	const uint32_t nameOffset = be32(invalidName, nameEntry + 8u);
	put16(invalidName, nameOffset + 16u, 0xffffu);
	CHECK(FontTrueTypeValidate(invalidName.data(), invalidName.size(), &metadata) ==
		FontTrueTypeInvalidNameTable);

	std::vector<uint8_t> cff = fixture;
	put32(cff, 0u, 0x4f54544fu);
	CHECK(FontTrueTypeValidate(cff.data(), cff.size(), &metadata) ==
		FontTrueTypeUnsupportedOutline);
	std::vector<uint8_t> collection = fixture;
	put32(collection, 0u, 0x74746366u);
	CHECK(FontTrueTypeValidate(collection.data(), collection.size(), &metadata) ==
		FontTrueTypeUnsupportedCollection);
	std::vector<uint8_t> oversized(kFontTrueTypeMaxFileBytes + 1u, 0u);
	CHECK(FontTrueTypeValidate(oversized.data(), oversized.size(), &metadata) ==
		FontTrueTypeFileTooLarge);
	std::vector<uint8_t> variant = fixture;
	const size_t postEntry = tableEntry(variant, "post");
	CHECK(postEntry < variant.size());
	variant[postEntry] = 'f';
	variant[postEntry + 1u] = 'v';
	variant[postEntry + 2u] = 'a';
	variant[postEntry + 3u] = 'r';
	CHECK(FontTrueTypeValidate(variant.data(), variant.size(), &metadata) ==
		FontTrueTypeUnsupportedVariant);

	std::vector<uint8_t> overlapping = fixture;
	put32(overlapping, 12u + 16u + 8u, be32(overlapping, 12u + 8u));
	CHECK(FontTrueTypeValidate(overlapping.data(), overlapping.size(), &metadata) ==
		FontTrueTypeInvalidTableRange);

	std::mt19937 random(0xF03u);
	for (unsigned i = 0; i < 512u; ++i) {
		std::vector<uint8_t> mutation = fixture;
		const unsigned changes = 1u + (random() % 4u);
		for (unsigned j = 0; j < changes; ++j) {
			const size_t index = random() % mutation.size();
			mutation[index] ^= static_cast<uint8_t>(1u << (random() % 8u));
		}
		const FontTrueTypeStatus status = FontTrueTypeValidate(
			mutation.data(), mutation.size(), &metadata);
		CHECK(status <= FontTrueTypeUnsupportedUnicodeMap);
		if (status == FontTrueTypeOk)
			CHECK(metadata.glyphCount > 0u && metadata.fileBytes == mutation.size());
	}
}

void testManager(const std::vector<uint8_t>& fixture)
{
	using namespace gxos::gui;
	CHECK(FontManagerInitialize() == FontStatus::Ok);
	FontHandle builtin = {0xffffu, 0, 0};
	CHECK(FontManagerGetDefaultFont(&builtin) == FontStatus::Ok);
	CHECK(FontManagerUnloadFont(builtin) == FontStatus::CannotUnloadBuiltinFont);

	std::vector<uint8_t> source = fixture;
	FontHandle loaded = {0xffffu, 0, 0};
	FontTrueTypeMetadata loadedMetadata = {};
	const FontId id = {0x5454460000000001ull};
	CHECK(FontTrueTypeLoadMemory(id, source.data(), source.size(), &loaded,
		&loadedMetadata) == FontStatus::Ok);
	std::fill(source.begin(), source.end(), 0);
	FontTrueTypeMetadata storedMetadata = {};
	CHECK(FontManagerGetTrueTypeMetadata(loaded, &storedMetadata) == FontStatus::Ok);
	CHECK(std::strcmp(storedMetadata.familyName, "Roboto") == 0);
	CHECK(storedMetadata.fileBytes == fixture.size());
	FontMetrics metrics = {};
	CHECK(FontGetMetrics(loaded, {16u}, &metrics) == FontStatus::MetadataOnly);
	FontGlyphView glyph = {};
	CHECK(FontLookupGlyph(loaded, {16u}, 'A', &glyph) == FontStatus::MetadataOnly);
	CHECK(FontManagerSetDefaultFont(loaded) == FontStatus::MetadataOnly);
	FontHandle stillBuiltin = {0xffffu, 0, 0};
	CHECK(FontManagerGetDefaultFont(&stillBuiltin) == FontStatus::Ok);
	CHECK(stillBuiltin.slot == builtin.slot && stillBuiltin.generation == builtin.generation);

	std::vector<uint8_t> invalid = fixture;
	invalid[0] = 'z';
	FontHandle failedHandle = {0, 0, 0};
	const FontId failedId = {0x5454460000000002ull};
	CHECK(FontTrueTypeLoadMemory(failedId, invalid.data(), invalid.size(), &failedHandle,
		nullptr) == FontStatus::UnsupportedFontFormat);
	CHECK(failedHandle.generation == 0u);
	FontHandle resolveFailed = {0, 0, 0};
	CHECK(FontManagerResolveFont(failedId, &resolveFailed) == FontStatus::FallbackUsed);
	CHECK(resolveFailed.slot == builtin.slot);
	CHECK(FontTrueTypeLoadMemory(id, fixture.data(), fixture.size(), &failedHandle,
		nullptr) == FontStatus::DuplicateFontId);

	CHECK(FontManagerUnloadFont(loaded) == FontStatus::Ok);
	CHECK(FontManagerGetTrueTypeMetadata(loaded, &storedMetadata) == FontStatus::InvalidHandle);
	CHECK(FontManagerUnloadFont(loaded) == FontStatus::InvalidHandle);
	FontHandle reloaded = {0xffffu, 0, 0};
	CHECK(FontTrueTypeLoadMemory(id, fixture.data(), fixture.size(), &reloaded,
		nullptr) == FontStatus::Ok);
	CHECK(reloaded.generation != loaded.generation);
	CHECK(FontManagerGetTrueTypeMetadata(loaded, &storedMetadata) == FontStatus::InvalidHandle);
	CHECK(FontManagerUnloadFont(reloaded) == FontStatus::Ok);

	for (uint32_t i = 0; i < 24u; ++i) {
		FontHandle handle = {0xffffu, 0, 0};
		CHECK(FontTrueTypeLoadMemory({0x5454461000000000ull + i}, fixture.data(),
			fixture.size(), &handle, nullptr) == FontStatus::Ok);
		CHECK(FontManagerUnloadFont(handle) == FontStatus::Ok);
		CHECK(FontGetMetrics(handle, {7u}, &metrics) == FontStatus::InvalidHandle);
	}

	FontHandle external[3] = {};
	for (uint32_t i = 0; i < 3u; ++i)
		CHECK(FontTrueTypeLoadMemory({0x5454462000000000ull + i}, fixture.data(),
			fixture.size(), &external[i], nullptr) == FontStatus::Ok);
	CHECK(FontTrueTypeLoadMemory({0x5454462000000003ull}, fixture.data(), fixture.size(),
		&failedHandle, nullptr) == FontStatus::FontSourceCapacity);
	for (uint32_t i = 0; i < 3u; ++i)
		CHECK(FontManagerUnloadFont(external[i]) == FontStatus::Ok);

	FontDefinition mocks[3] = {};
	for (uint32_t i = 0; i < 3u; ++i) {
		mocks[i].id.value = 0x4d4f434b00000000ull + i;
		mocks[i].debugName = "capacity";
		mocks[i].backend = &kMockBackend;
		FontHandle ignored = {0xffffu, 0, 0};
		CHECK(FontManagerRegisterFont(&mocks[i], &ignored) == FontStatus::Ok);
	}
	const FontId exhaustedId = {0x5454463000000000ull};
	CHECK(FontTrueTypeLoadMemory(exhaustedId, fixture.data(), fixture.size(), &failedHandle,
		nullptr) == FontStatus::RegistryFull);
	CHECK(FontManagerResolveFont(exhaustedId, &resolveFailed) == FontStatus::FallbackUsed);
	for (uint32_t i = 0; i < 3u; ++i) {
		FontHandle handle = {0xffffu, 0, 0};
		CHECK(FontManagerResolveFont(mocks[i].id, &handle) == FontStatus::Ok);
		CHECK(FontManagerUnloadFont(handle) == FontStatus::Ok);
	}

	g_vfsFixture = &fixture;
	g_vfsFixtureExists = true;
	FontHandle fromFile = {0xffffu, 0, 0};
	CHECK(FontTrueTypeLoadFile("fixture.ttf", {0x5454464000000000ull}, &fromFile,
		nullptr) == FontStatus::Ok);
	CHECK(FontManagerUnloadFont(fromFile) == FontStatus::Ok);
	g_vfsFixtureExists = false;
	CHECK(FontTrueTypeLoadFile("missing.ttf", {0x5454464000000001ull}, &failedHandle,
		nullptr) == FontStatus::FontFileNotFound);
	g_vfsFixtureExists = true;
	g_vfsReportEmpty = true;
	CHECK(FontTrueTypeLoadFile("empty.ttf", {0x5454464000000003ull}, &failedHandle,
		nullptr) == FontStatus::InvalidFontFile);
	g_vfsReportEmpty = false;
	g_vfsReportTruncated = true;
	CHECK(FontTrueTypeLoadFile("truncated.ttf", {0x5454464000000004ull}, &failedHandle,
		nullptr) == FontStatus::InvalidFontFile);
	g_vfsReportTruncated = false;
	g_vfsReportTooLarge = true;
	CHECK(FontTrueTypeLoadFile("large.ttf", {0x5454464000000002ull}, &failedHandle,
		nullptr) == FontStatus::FontFileTooLarge);
	g_vfsReportTooLarge = false;

	FontManagerShutdown();
	CHECK(FontManagerInitialize() == FontStatus::Ok);
	CHECK(FontManagerGetTrueTypeMetadata(fromFile, &storedMetadata) == FontStatus::InvalidHandle);
	FontHandle afterShutdown = {0xffffu, 0, 0};
	CHECK(FontTrueTypeLoadMemory({0x5454465000000000ull}, fixture.data(), fixture.size(),
		&afterShutdown, nullptr) == FontStatus::Ok);
	CHECK(FontManagerUnloadFont(afterShutdown) == FontStatus::Ok);
	FontManagerShutdown();
}

} // namespace

namespace kernel {
namespace vfs {

Status stat(const char* path, FileInfo* info)
{
	if (!path || !info) return VFS_ERR_INVALID;
	if (std::strcmp(path, "missing.ttf") == 0 || !g_vfsFixtureExists)
		return VFS_ERR_NOT_FOUND;
	info->type = FILE_TYPE_REGULAR;
	info->size = g_vfsReportTooLarge ?
		static_cast<uint64_t>(gxos::gui::kFontTrueTypeMaxFileBytes) + 1u :
		(g_vfsReportEmpty ? 0u : (g_vfsReportTruncated ? 8u :
		static_cast<uint64_t>(g_vfsFixture ? g_vfsFixture->size() : 0u)));
	return VFS_OK;
}

int32_t read_file(const char* path, void* buffer, uint32_t maxSize)
{
	if (!path || !buffer || !g_vfsFixtureExists || !g_vfsFixture ||
		(std::strcmp(path, "fixture.ttf") != 0 && std::strcmp(path, "truncated.ttf") != 0) ||
		g_vfsReportTooLarge || g_vfsReportEmpty)
		return VFS_ERR_NOT_FOUND;
	const size_t readLength = g_vfsReportTruncated ? 8u : g_vfsFixture->size();
	if (readLength > maxSize) return VFS_ERR_INVALID;
	std::memcpy(buffer, g_vfsFixture->data(), readLength);
	return static_cast<int32_t>(readLength);
}

} // namespace vfs
} // namespace kernel

int main()
{
	const std::vector<uint8_t> fixture = readFixture();
	testParser(fixture);
	testManager(fixture);
	std::printf("FONT3 TrueType loading: %d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
