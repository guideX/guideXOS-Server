#include "kernel/core/include/kernel/font_contract.h"
#include "kernel/core/include/kernel/font_truetype.h"
#include "kernel/core/include/kernel/font_text_layout.h"
#include "kernel/core/include/kernel/vfs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

using namespace gxos::gui;
int checks = 0;
int failures = 0;

#define CHECK(expression) do { \
	++checks; \
	if (!(expression)) { \
		std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); \
		++failures; \
	} \
} while (0)

std::vector<uint8_t> readRoboto()
{
	std::ifstream input("assets/Fonts/roboto/Roboto-Regular.ttf", std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

bool endsWithEllipsis(const char* text)
{
	if (!text) return false;
	size_t length = 0u;
	while (text[length]) ++length;
	return length >= 3u && text[length - 3u] == '.' &&
		text[length - 2u] == '.' && text[length - 1u] == '.';
}

} // namespace

int main()
{
	CHECK(FontManagerInitialize() == FontStatus::Ok);
	FontHandle bitmap = {0xffffu, 0u, 0u};
	CHECK(FontManagerResolveFont({kBuiltinBitmapFontId}, &bitmap) == FontStatus::Ok);
	char output[4096] = {};
	FontTextLineLayout layout = {};
	FontStatus status = FontTextLayoutSingleLine(bitmap, {kBitmapFontPixelHeight},
		"Bitmap", 6u, 64u, FontTextOverflowPolicy::Ellipsis,
		output, sizeof(output), &layout);
	CHECK(status == FontStatus::Ok);
	CHECK(std::strcmp(output, "Bitmap") == 0);
	CHECK(layout.advancePx == 36u);
	CHECK(layout.lineHeightPx == 7u && layout.ascentPx == 7u && layout.descentPx == 0u);

	FontTextLineLayout clipLayout = {};
	status = FontTextLayoutSingleLine(bitmap, {7u}, "Bitmap", 6u, 5u,
		FontTextOverflowPolicy::Clip, output, sizeof(output), &clipLayout);
	CHECK(status == FontStatus::Ok && clipLayout.clipped == 1u);
	CHECK(clipLayout.truncated == 0u && std::strcmp(output, "Bitmap") == 0);
	CHECK(FontTextLayoutSingleLine(bitmap, {7u}, "A\nB", 3u, 64u,
		FontTextOverflowPolicy::Ellipsis, output, sizeof(output), &layout) ==
		FontStatus::UnsupportedGlyphFeature);

	const std::vector<uint8_t> bytes = readRoboto();
	CHECK(!bytes.empty());
	FontHandle roboto = {0xffffu, 0u, 0u};
	FontTrueTypeMetadata metadata = {};
	CHECK(FontTrueTypeLoadMemory({0x464f4e54384c4159ull}, bytes.data(), bytes.size(),
		&roboto, &metadata) == FontStatus::Ok);

	TextMeasureResult narrow = {};
	TextMeasureResult wide = {};
	CHECK(FontMeasureText(roboto, {18u}, "iiii", 4u, &narrow) == FontStatus::Ok);
	CHECK(FontMeasureText(roboto, {18u}, "WWWW", 4u, &wide) == FontStatus::Ok);
	CHECK(narrow.pixelWidth < wide.pixelWidth);

	status = FontTextLayoutSingleLine(roboto, {18u}, "Agjp", 4u, 256u,
		FontTextOverflowPolicy::Ellipsis, output, sizeof(output), &layout);
	CHECK(status == FontStatus::Ok && std::strcmp(output, "Agjp") == 0);
	CHECK(layout.lineHeightPx >= layout.ascentPx + layout.descentPx);
	CHECK(layout.inkDescentPx > 0u);

	const char* longName = "This is a deliberately long font family label for a narrow Settings row";
	status = FontTextLayoutSingleLine(roboto, {18u}, longName, std::strlen(longName),
		128u, FontTextOverflowPolicy::Ellipsis, output, sizeof(output), &layout);
	CHECK(status == FontStatus::Ok);
	CHECK(layout.truncated == 1u && layout.clipped == 1u && endsWithEllipsis(output));
	CHECK(layout.inkWidthPx <= 128u && layout.outputByteLength < layout.sourceByteLength);

	char longLabel[301] = {};
	for (size_t i = 0u; i < sizeof(longLabel) - 1u; ++i) longLabel[i] = "WRobotoFamily"[i % 13u];
	status = FontTextLayoutSingleLine(roboto, {22u}, longLabel, sizeof(longLabel) - 1u,
		120u, FontTextOverflowPolicy::Ellipsis, output, 32u, &layout);
	CHECK(status == FontStatus::Ok);
	CHECK(layout.outputByteLength < 32u && endsWithEllipsis(output));
	CHECK(layout.inkWidthPx <= 120u && layout.truncated == 1u);

	bool foundNegativeBearing = false;
	for (uint32_t codePoint = 32u; codePoint <= 126u; ++codePoint) {
		FontGlyphView glyph = {};
		const FontStatus lookup = FontLookupGlyph(roboto, {48u}, codePoint, &glyph);
		if (lookup == FontStatus::Ok && glyph.metrics.xBearingPx < 0) {
			char one[2] = {static_cast<char>(codePoint), '\0'};
			status = FontTextLayoutSingleLine(roboto, {48u}, one, 1u, 128u,
				FontTextOverflowPolicy::Ellipsis, output, sizeof(output), &layout);
			foundNegativeBearing = status == FontStatus::Ok && layout.originOffsetXPx > 0;
			if (foundNegativeBearing) break;
		}
	}
	CHECK(foundNegativeBearing);

	FontTextLineLayout minSize = {};
	FontTextLineLayout maxSize = {};
	CHECK(FontTextLayoutSingleLine(roboto, {8u}, "Size", 4u, 64u,
		FontTextOverflowPolicy::Ellipsis, output, sizeof(output), &minSize) == FontStatus::Ok);
	CHECK(FontTextLayoutSingleLine(roboto, {48u}, "Size", 4u, 256u,
		FontTextOverflowPolicy::Ellipsis, output, sizeof(output), &maxSize) == FontStatus::Ok);
	CHECK(minSize.pixelSize == 8u && maxSize.pixelSize == 48u);
	CHECK(minSize.lineHeightPx < maxSize.lineHeightPx);

	const char invalidUtf8[] = {static_cast<char>(0xc0), static_cast<char>(0xaf)};
	CHECK(FontTextLayoutSingleLine(roboto, {16u}, invalidUtf8, sizeof(invalidUtf8), 64u,
		FontTextOverflowPolicy::Ellipsis, output, sizeof(output), &layout) == FontStatus::InvalidUtf8);
	const char missingGlyph[] = "\xe2\x98\x83";
	status = FontTextLayoutSingleLine(roboto, {16u}, missingGlyph, 3u, 64u,
		FontTextOverflowPolicy::Ellipsis, output, sizeof(output), &layout);
	CHECK(status == FontStatus::FallbackUsed && layout.fallbackUsed == 1u);
	CHECK(std::strcmp(output, missingGlyph) == 0);

	FontGlyphCacheReset();
	static const uint32_t surfacePixels = 320u * 64u;
	uint32_t surface[surfacePixels + 2u] = {};
	surface[surfacePixels] = 0x13579bdfu;
	surface[surfacePixels + 1u] = 0x2468ace0u;
	const size_t destinationBytes = static_cast<size_t>(surfacePixels) * sizeof(uint32_t);
	const char* drawText = "Variable width redraw";
	const size_t drawLength = std::strlen(drawText);
	CHECK(FontDrawGrayscaleTextToBuffer(roboto, {18u}, surface, destinationBytes,
		320u * sizeof(uint32_t), 320u, 64u, 4, 30, drawText, drawLength,
		0xffffffffu, nullptr) == FontStatus::Ok);
	FontGlyphCacheStatistics cold = {};
	CHECK(FontGlyphCacheGetStatistics(&cold) == FontStatus::Ok);
	CHECK(cold.misses > 0u && cold.rasterizations > 0u);
	CHECK(FontDrawGrayscaleTextToBuffer(roboto, {18u}, surface, destinationBytes,
		320u * sizeof(uint32_t), 320u, 64u, 4, 30, drawText, drawLength,
		0xffffffffu, nullptr) == FontStatus::Ok);
	FontGlyphCacheStatistics warm = {};
	CHECK(FontGlyphCacheGetStatistics(&warm) == FontStatus::Ok);
	CHECK(warm.hits > cold.hits && warm.rasterizations == cold.rasterizations);
	CHECK(FontDrawGrayscaleTextToBuffer(roboto, {32u}, surface, destinationBytes,
		320u * sizeof(uint32_t), 320u, 64u, 4, 48, drawText, drawLength,
		0xffffffffu, nullptr) == FontStatus::Ok);
	FontGlyphCacheStatistics resized = {};
	CHECK(FontGlyphCacheGetStatistics(&resized) == FontStatus::Ok);
	CHECK(resized.rasterizations > warm.rasterizations);
	CHECK(surface[surfacePixels] == 0x13579bdfu && surface[surfacePixels + 1u] == 0x2468ace0u);

	char tooLong[4098] = {};
	for (size_t i = 0u; i < sizeof(tooLong) - 1u; ++i) tooLong[i] = 'a';
	CHECK(FontTextLayoutSingleLine(roboto, {16u}, tooLong, sizeof(tooLong) - 1u, 64u,
		FontTextOverflowPolicy::Ellipsis, output, sizeof(output), &layout) == FontStatus::InputTooLong);
	CHECK(FontTextLayoutSingleLine(roboto, {16u}, "abcd", 4u, 64u,
		FontTextOverflowPolicy::Clip, output, 4u, &layout) == FontStatus::BufferTooSmall);

	CHECK(FontManagerUnloadFont(roboto) == FontStatus::Ok);
	CHECK(FontDrawGrayscaleTextToBuffer(roboto, {18u}, surface, destinationBytes,
		320u * sizeof(uint32_t), 320u, 64u, 4, 30, drawText, drawLength,
		0xffffffffu, nullptr) == FontStatus::InvalidHandle);
	CHECK(surface[surfacePixels] == 0x13579bdfu && surface[surfacePixels + 1u] == 0x2468ace0u);
	FontManagerShutdown();
	std::printf("FONT8 text layout: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}

namespace kernel {
namespace vfs {

Status stat(const char*, FileInfo*)
{
	return VFS_ERR_NOT_FOUND;
}

int32_t read_file(const char*, void*, uint32_t)
{
	return VFS_ERR_NOT_FOUND;
}

} // namespace vfs
} // namespace kernel
