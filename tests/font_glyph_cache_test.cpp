#include "kernel/core/include/kernel/font_contract.h"
#include "kernel/core/include/kernel/vfs.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

using namespace gxos::gui;

int g_checks = 0;
int g_failures = 0;

#define CHECK(expression) do { \
	++g_checks; \
	if (!(expression)) { \
		++g_failures; \
		std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); \
	} \
} while (0)

struct TestFontContext {
	UnicodeCodePoint failCodePoint;
};

TestFontContext g_fontOne = {};
TestFontContext g_fontTwo = {};

GlyphMetrics testMetrics(UnicodeCodePoint cp, uint32_t pixelSize)
{
	GlyphMetrics result = {};
	result.glyphId = cp;
	result.codePoint = cp;
	result.advancePx = (pixelSize * 3u + 8u) / 16u;
	if (result.advancePx == 0u) result.advancePx = 1u;
	result.yBearingPx = static_cast<int32_t>((pixelSize + 1u) / 2u);
	if (cp == ' ') {
		result.widthPx = 0u;
		result.heightPx = 0u;
		result.advancePx = pixelSize / 3u + 1u;
		return result;
	}
	if (cp == 'T') {
		result.widthPx = (3u * pixelSize + 15u) / 16u;
		result.heightPx = (1u * pixelSize + 15u) / 16u;
		result.advancePx = (4u * pixelSize + 15u) / 16u;
		result.yBearingPx = static_cast<int32_t>(result.heightPx);
		return result;
	}
	if (cp == 'i') {
		result.widthPx = (1u * pixelSize + 15u) / 16u;
		result.heightPx = (5u * pixelSize + 15u) / 16u;
		result.advancePx = (3u * pixelSize + 15u) / 16u;
		result.yBearingPx = static_cast<int32_t>(result.heightPx);
		return result;
	}
	if (cp == 'W') {
		result.widthPx = (8u * pixelSize + 15u) / 16u;
		result.heightPx = (5u * pixelSize + 15u) / 16u;
		result.advancePx = (9u * pixelSize + 15u) / 16u;
		result.yBearingPx = static_cast<int32_t>(result.heightPx);
		return result;
	}
	if (cp == 'j') {
		result.widthPx = (3u * pixelSize + 15u) / 16u;
		result.heightPx = (6u * pixelSize + 15u) / 16u;
		result.xBearingPx = -static_cast<int32_t>((pixelSize + 15u) / 16u);
		result.yBearingPx = static_cast<int32_t>(result.heightPx);
		result.advancePx = (4u * pixelSize + 15u) / 16u;
		return result;
	}
	if (cp == 'G') {
		result.widthPx = (72u * pixelSize + 127u) / 128u;
		result.heightPx = (72u * pixelSize + 127u) / 128u;
		result.advancePx = result.widthPx + 2u;
		result.yBearingPx = static_cast<int32_t>(result.heightPx);
		return result;
	}
	result.widthPx = 2u + ((cp * 2654435761u) % 4u);
	result.heightPx = (5u * pixelSize + 15u) / 16u;
	result.yBearingPx = static_cast<int32_t>(result.heightPx);
	return result;
}

FontStatus testGetMetrics(const void*, FontSize size, FontMetrics* out)
{
	if (!out || size.pixelHeight == 0u || size.pixelHeight > 128u)
		return FontStatus::UnsupportedSize;
	*out = {};
	out->pixelSize = size.pixelHeight;
	out->ascentPx = size.pixelHeight;
	out->baselinePx = size.pixelHeight;
	out->lineHeightPx = size.pixelHeight + 2u;
	return FontStatus::Ok;
}

FontStatus testLookup(const void*, FontSize size, UnicodeCodePoint cp, FontGlyphView* out)
{
	if (!out || size.pixelHeight == 0u || size.pixelHeight > 128u)
		return FontStatus::UnsupportedSize;
	if (cp > 0x7fu) return FontStatus::UnsupportedCodePoint;
	out->metrics = testMetrics(cp, size.pixelHeight);
	out->requestedCodePoint = cp;
	return FontStatus::Ok;
}

FontStatus testRasterize(const void* rawContext, FontSize size, UnicodeCodePoint cp,
	uint8_t* pixels, size_t capacity, uint32_t stride, GlyphMetrics* out)
{
	if (!rawContext || !out) return FontStatus::InvalidArgument;
	if (size.pixelHeight == 0u || size.pixelHeight > 128u)
		return FontStatus::UnsupportedSize;
	if (cp > 0x7fu) return FontStatus::UnsupportedCodePoint;
	const TestFontContext* context = static_cast<const TestFontContext*>(rawContext);
	if (context->failCodePoint == cp) return FontStatus::BackendFailure;
	const GlyphMetrics metrics = testMetrics(cp, size.pixelHeight);
	const uint64_t area = static_cast<uint64_t>(metrics.widthPx) * metrics.heightPx;
	if (area == 0u) {
		*out = metrics;
		return FontStatus::Ok;
	}
	if (!pixels || stride < metrics.widthPx ||
		static_cast<uint64_t>(metrics.heightPx - 1u) * stride + metrics.widthPx > capacity)
		return FontStatus::BufferTooSmall;
	for (uint32_t row = 0; row < metrics.heightPx; ++row) {
		for (uint32_t column = 0; column < metrics.widthPx; ++column) {
			uint8_t value = 255u;
			if (cp == 'T') value = column == 0u ? 0u : (column == 1u ? 255u : 128u);
			pixels[static_cast<size_t>(row) * stride + column] = value;
		}
	}
	*out = metrics;
	return FontStatus::Ok;
}

const FontBackend kTestBackend = {
	FontCapabilityGlyphLookup | FontCapabilityMeasurement | FontCapabilityUtf8Input |
		FontCapabilityRasterization,
	&testGetMetrics,
	&testLookup,
	&testRasterize,
};

const FontDefinition kFontOneDefinition = {
	{0x464f4e5435000001ull}, "FONT5 synthetic one", &kTestBackend,
	&g_fontOne, nullptr, nullptr,
};

const FontDefinition kFontTwoDefinition = {
	{0x464f4e5435000002ull}, "FONT5 synthetic two", &kTestBackend,
	&g_fontTwo, nullptr, nullptr,
};

void clearCanvas(std::vector<uint32_t>* pixels, uint32_t color)
{
	std::fill(pixels->begin(), pixels->end(), color);
}

FontStatus draw(FontHandle handle, const char* text, uint32_t size,
	std::vector<uint32_t>* pixels, uint32_t width, uint32_t height,
	int32_t x, int32_t baseline, uint32_t color = 0xffffffffu)
{
	return FontDrawGrayscaleTextToBuffer(handle, {size}, pixels->data(),
	pixels->size() * sizeof(uint32_t), width * 4u, width, height, x, baseline,
	text, std::strlen(text), color, nullptr);
}

uint32_t blend(uint32_t foreground, uint32_t background, uint32_t coverage)
{
	const uint32_t alpha = ((foreground >> 24) & 255u) * coverage / 255u;
	const uint32_t red = (((foreground >> 16) & 255u) * alpha +
		((background >> 16) & 255u) * (255u - alpha) + 127u) / 255u;
	const uint32_t green = (((foreground >> 8) & 255u) * alpha +
		((background >> 8) & 255u) * (255u - alpha) + 127u) / 255u;
	const uint32_t blue = ((foreground & 255u) * alpha +
		(background & 255u) * (255u - alpha) + 127u) / 255u;
	return 0xff000000u | (red << 16) | (green << 8) | blue;
}

void checkLimits()
{
	FontGlyphCacheStatistics stats = {};
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.maxEntries == 16u);
	CHECK(stats.maxPixelBytes == 65536u);
	CHECK(stats.maxGlyphPixels == 4096u);
	CHECK(stats.maxMetadataBytes <= 4096u);
	CHECK(stats.cacheMemoryBytes <= stats.maxCacheMemoryBytes);
	CHECK(stats.maxCacheMemoryBytes == 72u * 1024u);
}

void testHitsAdvancesAndSizes(FontHandle font)
{
	FontGlyphCacheReset();
	std::vector<uint32_t> pixels(64u * 32u, 0xff101820u);
	CHECK(draw(font, "Wi", 16u, &pixels, 64u, 32u, 2, 16) == FontStatus::Ok);
	FontGlyphCacheStatistics stats = {};
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.misses == 2u && stats.hits == 0u && stats.rasterizations == 2u);
	const std::vector<uint32_t> first = pixels;
	clearCanvas(&pixels, 0xff101820u);
	CHECK(draw(font, "Wi", 16u, &pixels, 64u, 32u, 2, 16) == FontStatus::Ok);
	CHECK(pixels == first);
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.hits == 2u && stats.rasterizations == 2u);
	CHECK(draw(font, "W", 24u, &pixels, 64u, 32u, 2, 25) == FontStatus::Ok);
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.misses == 3u && stats.rasterizations == 3u);
	CHECK(stats.entryCount == 3u);
}

void testBlendAndEmptyGlyph(FontHandle font)
{
	FontGlyphCacheReset();
	const uint32_t background = 0xff203060u;
	const uint32_t foreground = 0xffff4000u;
	std::vector<uint32_t> pixels(8u * 8u, background);
	CHECK(draw(font, "T", 16u, &pixels, 8u, 8u, 0, 1, foreground) == FontStatus::Ok);
	CHECK(pixels[0] == background); // fully transparent coverage
	CHECK(pixels[1] == foreground); // fully opaque coverage
	CHECK(pixels[2] == blend(foreground, background, 128u)); // intermediate coverage
	clearCanvas(&pixels, background);
	CHECK(draw(font, "T", 16u, &pixels, 8u, 8u, 0, 1, 0x80ff4000u) == FontStatus::Ok);
	CHECK(pixels[1] == blend(0x80ff4000u, background, 255u));
	clearCanvas(&pixels, background);
	CHECK(draw(font, " ", 16u, &pixels, 8u, 8u, 0, 6) == FontStatus::Ok);
	CHECK(std::all_of(pixels.begin(), pixels.end(), [background](uint32_t p) { return p == background; }));
	FontGlyphCacheStatistics stats = {};
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.entryCount == 2u); // T and the cached empty-space metrics
}

void testMissingGlyphFallback(FontHandle font)
{
	FontGlyphCacheReset();
	std::vector<uint32_t> pixels(32u * 16u, 0xff000000u);
	const char missing[] = "\xf0\x9f\x99\x82";
	CHECK(FontDrawGrayscaleTextToBuffer(font, {16u}, pixels.data(),
	pixels.size() * sizeof(uint32_t), 32u * 4u, 32u, 16u, 0, 12,
	missing, sizeof(missing) - 1u, 0xffffffffu, nullptr) == FontStatus::FallbackUsed);
	CHECK(std::any_of(pixels.begin(), pixels.end(), [](uint32_t p) { return p != 0xff000000u; }));
	FontGlyphCacheStatistics stats = {};
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.misses == 1u && stats.rasterizations == 1u && stats.entryCount == 1u);
}

void testEviction(FontHandle font)
{
	FontGlyphCacheReset();
	std::vector<uint32_t> pixels(512u * 24u, 0xff000000u);
	CHECK(draw(font, "ABCDEFGHIJKLMNOPQ", 16u, &pixels, 512u, 24u, 0, 16) == FontStatus::Ok);
	FontGlyphCacheStatistics stats = {};
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.entryCount == 16u && stats.misses == 17u);
	CHECK(stats.evictions == 1u && stats.bytesUsed <= stats.maxPixelBytes);
	CHECK(draw(font, "A", 16u, &pixels, 512u, 24u, 0, 16) == FontStatus::Ok);
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.misses == 18u && stats.evictions == 2u);
}

void testOversizedUncachedGlyph(FontHandle font)
{
	FontGlyphCacheReset();
	std::vector<uint32_t> pixels(128u * 128u, 0xff000000u);
	CHECK(draw(font, "G", 128u, &pixels, 128u, 128u, 0, 72) == FontStatus::Ok);
	FontGlyphCacheStatistics stats = {};
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.misses == 1u && stats.rasterizations == 1u);
	CHECK(stats.entryCount == 0u && stats.bytesUsed == 0u);
	CHECK(std::any_of(pixels.begin(), pixels.end(), [](uint32_t p) { return p != 0xff000000u; }));
}

void testClippingAndDestinationBounds(FontHandle font)
{
	FontGlyphCacheReset();
	const uint32_t canary = 0xcafef00du;
	std::vector<uint32_t> guarded(100u, canary);
	uint32_t* surface = guarded.data() + 11u;
	for (uint32_t row = 0; row < 8u; ++row)
		for (uint32_t column = 0; column < 8u; ++column)
			surface[row * 8u + column] = 0xff000000u;
	const size_t capacity = (8u - 1u) * 8u * sizeof(uint32_t) + 8u * sizeof(uint32_t);
	CHECK(FontDrawGrayscaleTextToBuffer(font, {16u}, surface, capacity,
		8u * 4u, 8u, 8u, -1, 4, "j", 1u, 0xffffffffu, nullptr) == FontStatus::Ok);
	CHECK(FontDrawGrayscaleTextToBuffer(font, {16u}, surface, capacity,
		8u * 4u, 8u, 8u, 7, 4, "W", 1u, 0xffffffffu, nullptr) == FontStatus::Ok);
	CHECK(FontDrawGrayscaleTextToBuffer(font, {16u}, surface, capacity,
		8u * 4u, 8u, 8u, 40, 40, "W", 1u, 0xffffffffu, nullptr) == FontStatus::Ok);
	for (size_t i = 0; i < guarded.size(); ++i) {
		const bool visible = i >= 11u && i < 75u;
		if (!visible) CHECK(guarded[i] == canary);
	}

	std::vector<uint32_t> before = guarded;
	CHECK(FontDrawGrayscaleTextToBuffer(font, {16u}, surface, capacity - 1u,
		8u * 4u, 8u, 8u, 0, 4, "T", 1u, 0xffffffffu, nullptr) == FontStatus::BufferTooSmall);
	CHECK(guarded == before);
	CHECK(FontDrawGrayscaleTextToBuffer(font, {16u}, surface, capacity,
		7u * 4u, 8u, 8u, 0, 4, "T", 1u, 0xffffffffu, nullptr) == FontStatus::InvalidArgument);
	CHECK(FontDrawGrayscaleTextToBuffer(font, {16u}, surface, capacity,
		33u, 8u, 8u, 0, 4, "T", 1u, 0xffffffffu, nullptr) == FontStatus::InvalidArgument);
	CHECK(FontDrawGrayscaleTextToBuffer(font, {16u}, surface, capacity,
		8u * 4u, 0xffffffffu, 8u, 0, 4, "T", 1u,
		0xffffffffu, nullptr) == FontStatus::InvalidArgument);
	CHECK(FontDrawGrayscaleTextToBuffer(font, {16u}, surface, capacity,
		0xfffffffcu, 4096u, 4096u, 0, 4, "T", 1u,
		0xffffffffu, nullptr) == FontStatus::BufferTooSmall);
	const char invalidUtf8[] = "\xc0\xaf";
	CHECK(FontDrawGrayscaleTextToBuffer(font, {16u}, surface, capacity,
		8u * 4u, 8u, 8u, 0, 4, invalidUtf8, sizeof(invalidUtf8) - 1u,
		0xffffffffu, nullptr) == FontStatus::InvalidUtf8);
	CHECK(guarded == before);
}

void testFailedRasterRollbackAndFallback(FontHandle font)
{
	FontGlyphCacheReset();
	g_fontOne.failCodePoint = 'F';
	std::vector<uint32_t> pixels(32u * 16u, 0xff101010u);
	CHECK(draw(font, "F", 16u, &pixels, 32u, 16u, 0, 12) == FontStatus::FallbackUsed);
	g_fontOne.failCodePoint = 0u;
	CHECK(std::any_of(pixels.begin(), pixels.end(), [](uint32_t p) { return p != 0xff101010u; }));
	FontGlyphCacheStatistics stats = {};
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.misses == 2u && stats.rasterizations == 2u);
	CHECK(stats.entryCount == 1u && stats.bytesUsed == 35u);
	CHECK(draw(font, "F", 16u, &pixels, 32u, 16u, 0, 12) == FontStatus::Ok);
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.hits == 0u && stats.misses == 3u && stats.rasterizations == 3u);
}

void testIdentityGenerationAndReset(FontHandle* font)
{
	FontGlyphCacheReset();
	std::vector<uint32_t> pixels(32u * 16u, 0xff000000u);
	CHECK(draw(*font, "A", 16u, &pixels, 32u, 16u, 0, 12) == FontStatus::Ok);
	FontHandle second = {};
	CHECK(FontManagerRegisterFont(&kFontTwoDefinition, &second) == FontStatus::Ok);
	CHECK(draw(second, "A", 16u, &pixels, 32u, 16u, 0, 12) == FontStatus::Ok);
	FontGlyphCacheStatistics stats = {};
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.misses == 2u && stats.hits == 0u && stats.entryCount == 2u);
	const FontHandle stale = *font;
	const uint32_t oldGeneration = font->generation;
	CHECK(FontManagerUnloadFont(*font) == FontStatus::Ok);
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.entryCount == 1u); // unload invalidated only that slot/generation
	CHECK(FontGetMetrics(stale, {16u}, nullptr) == FontStatus::InvalidArgument);
	FontMetrics staleMetrics = {};
	CHECK(FontGetMetrics(stale, {16u}, &staleMetrics) == FontStatus::InvalidHandle);
	CHECK(FontManagerRegisterFont(&kFontOneDefinition, font) == FontStatus::Ok);
	CHECK(font->generation != oldGeneration);
	CHECK(draw(*font, "A", 16u, &pixels, 32u, 16u, 0, 12) == FontStatus::Ok);
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.misses == 3u && stats.hits == 0u);
	CHECK(FontManagerUnloadFont(second) == FontStatus::Ok);
	CHECK(FontManagerUnloadFont(*font) == FontStatus::Ok);
	FontGlyphCacheReset();
	CHECK(FontGlyphCacheGetStatistics(&stats) == FontStatus::Ok);
	CHECK(stats.hits == 0u && stats.misses == 0u && stats.rasterizations == 0u &&
		stats.evictions == 0u && stats.entryCount == 0u && stats.bytesUsed == 0u);
}

} // namespace

int main()
{
	const std::chrono::steady_clock::time_point initializationStart =
		std::chrono::steady_clock::now();
	CHECK(FontManagerInitialize() == FontStatus::Ok);
	const uint64_t initializationMicros = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - initializationStart).count());
	FontHandle font = {};
	CHECK(FontManagerRegisterFont(&kFontOneDefinition, &font) == FontStatus::Ok);
	checkLimits();
	testHitsAdvancesAndSizes(font);
	testBlendAndEmptyGlyph(font);
	testMissingGlyphFallback(font);
	testEviction(font);
	testOversizedUncachedGlyph(font);
	testClippingAndDestinationBounds(font);
	testFailedRasterRollbackAndFallback(font);
	testIdentityGenerationAndReset(&font);
	std::ifstream fixtureFile("assets/Fonts/roboto/Roboto-Regular.ttf", std::ios::binary);
	const std::vector<uint8_t> fixture{std::istreambuf_iterator<char>(fixtureFile),
		std::istreambuf_iterator<char>()};
	CHECK(!fixture.empty());
	FontHandle roboto = {};
	CHECK(FontTrueTypeLoadMemory({0x464f4e5435000003ull}, fixture.data(),
		fixture.size(), &roboto, nullptr) == FontStatus::Ok);
	FontGlyphCacheReset();
	std::vector<uint32_t> pixels(256u * 64u, 0xff101820u);
	const char performanceText[] = "iW AVATAR 0123";
	const std::chrono::steady_clock::time_point coldStart = std::chrono::steady_clock::now();
	CHECK(FontDrawGrayscaleTextToBuffer(roboto, {32u}, pixels.data(),
		pixels.size() * sizeof(uint32_t), 256u * 4u, 256u, 64u, 4, 42,
		performanceText, sizeof(performanceText) - 1u, 0xffffffffu, nullptr) == FontStatus::Ok);
	const uint64_t coldDrawMicros = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - coldStart).count());
	const std::chrono::steady_clock::time_point warmStart = std::chrono::steady_clock::now();
	for (uint32_t iteration = 0; iteration < 100u; ++iteration) {
		std::fill(pixels.begin(), pixels.end(), 0xff101820u);
		CHECK(FontDrawGrayscaleTextToBuffer(roboto, {32u}, pixels.data(),
			pixels.size() * sizeof(uint32_t), 256u * 4u, 256u, 64u, 4, 42,
			performanceText, sizeof(performanceText) - 1u, 0xffffffffu, nullptr) == FontStatus::Ok);
	}
	const uint64_t warmDrawNanos = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - warmStart).count());
	FontGlyphCacheStatistics performanceStats = {};
	CHECK(FontGlyphCacheGetStatistics(&performanceStats) == FontStatus::Ok);
	CHECK(performanceStats.hits >= 100u && performanceStats.rasterizations > 0u);
	std::printf("FONT5 evidence: init_us=%llu cold_draw_us=%llu warm_draw_ns_per_call=%llu "
		"hits=%llu misses=%llu rasterizations=%llu evictions=%llu bytes=%u peak=%u cache_bytes=%u\n",
		static_cast<unsigned long long>(initializationMicros),
		static_cast<unsigned long long>(coldDrawMicros),
		static_cast<unsigned long long>(warmDrawNanos / 100u),
		static_cast<unsigned long long>(performanceStats.hits),
		static_cast<unsigned long long>(performanceStats.misses),
		static_cast<unsigned long long>(performanceStats.rasterizations),
		static_cast<unsigned long long>(performanceStats.evictions),
		performanceStats.bytesUsed, performanceStats.peakBytesUsed,
		performanceStats.cacheMemoryBytes);
	CHECK(FontManagerUnloadFont(roboto) == FontStatus::Ok);
	FontManagerShutdown();
	CHECK(FontManagerInitialize() == FontStatus::Ok);
	FontMetrics staleMetrics = {};
	CHECK(FontGetMetrics(font, {16u}, &staleMetrics) == FontStatus::InvalidHandle);
	FontManagerShutdown();
	std::printf("FONT5 glyph cache and grayscale renderer: %d checks, %d failures\n",
		g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
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
