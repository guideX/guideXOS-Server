#include "kernel/core/include/kernel/font_contract.h"
#include "bitmap_font.h"

#include <cstdio>
#include <cstring>
#include <limits>

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(expr) do { \
	++g_checks; \
	if (!(expr)) { \
		++g_failures; \
		std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
	} \
} while (0)

struct MockFont {
	uint32_t lineHeight;
	uint32_t advance;
};

gxos::gui::FontStatus mockMetrics(const void* context, gxos::gui::FontSize size,
	gxos::gui::FontMetrics* out)
{
	if (!context || !out || size.pixelHeight == 0) return gxos::gui::FontStatus::InvalidArgument;
	const MockFont* mock = static_cast<const MockFont*>(context);
	out->pixelSize = size.pixelHeight;
	out->ascentPx = mock->lineHeight;
	out->descentPx = 0;
	out->lineGapPx = 0;
	out->lineHeightPx = mock->lineHeight;
	out->baselinePx = mock->lineHeight;
	return gxos::gui::FontStatus::Ok;
}

gxos::gui::FontStatus mockGlyph(const void* context, gxos::gui::UnicodeCodePoint cp,
	gxos::gui::FontGlyphView* out)
{
	if (!context || !out) return gxos::gui::FontStatus::InvalidArgument;
	if (cp < 32u || cp > 126u) return gxos::gui::FontStatus::UnsupportedCodePoint;
	out->metrics.glyphId = cp - 32u;
	out->metrics.codePoint = cp;
	out->metrics.xBearingPx = 0;
	out->metrics.yBearingPx = 0;
	out->metrics.widthPx = 0;
	out->metrics.heightPx = 0;
	out->metrics.advancePx = static_cast<const MockFont*>(context)->advance;
	out->bitmap = nullptr;
	out->bitmapByteLength = 0;
	out->bitmapFormat = gxos::gui::FontBitmapFormat::None;
	out->fallbackUsed = 0;
	out->reserved[0] = 0;
	out->reserved[1] = 0;
	out->requestedCodePoint = cp;
	return gxos::gui::FontStatus::Ok;
}

const gxos::gui::FontBackend kMockBackend = {
	gxos::gui::FontCapabilityGlyphLookup | gxos::gui::FontCapabilityMeasurement,
	mockMetrics,
	mockGlyph
};

void testBitmapAndMeasurement(gxos::gui::FontHandle builtin)
{
	using namespace gxos::gui;
	FontMetrics metrics = {};
	CHECK(FontGetMetrics(builtin, {kBitmapFontPixelHeight}, &metrics) == FontStatus::Ok);
	CHECK(metrics.ascentPx == 7 && metrics.descentPx == 0 && metrics.lineHeightPx == 7);
	CHECK(metrics.baselinePx == 7);
	CHECK(FontGetMetrics(builtin, {8}, &metrics) == FontStatus::UnsupportedSize);

	FontGlyphView glyph = {};
	CHECK(FontLookupGlyph(builtin, {7}, 'A', &glyph) == FontStatus::Ok);
	CHECK(glyph.metrics.widthPx == 5 && glyph.metrics.heightPx == 7);
	CHECK(glyph.metrics.advancePx == 6 && glyph.bitmapByteLength == 5);
	CHECK(glyph.bitmapFormat == FontBitmapFormat::ColumnLsbTop);
	CHECK(glyph.bitmap && std::memcmp(glyph.bitmap, BitmapFont::Glyph('A'), 5) == 0);
	CHECK(FontLookupGlyph(builtin, {7}, 0x1f642u, &glyph) == FontStatus::FallbackUsed);
	CHECK(glyph.fallbackUsed == 1 && glyph.requestedCodePoint == 0x1f642u);
	CHECK(glyph.metrics.codePoint == static_cast<uint32_t>('?'));
	CHECK(std::memcmp(glyph.bitmap, BitmapFont::Glyph('?'), 5) == 0);

	TextMeasureResult measured = {};
	CHECK(FontMeasureText(builtin, {7}, "AB", 2, &measured) == FontStatus::Ok);
	CHECK(measured.byteLength == 2 && measured.codePointCount == 2 && measured.glyphCount == 2);
	CHECK(measured.pixelWidth == 12 && measured.pixelHeight == 7);
	CHECK(measured.maxLineAdvancePx == 12 && measured.lastLineAdvancePx == 12);
	CHECK(FontMeasureText(builtin, {7}, "", 0, &measured) == FontStatus::Ok);
	CHECK(measured.byteLength == 0 && measured.codePointCount == 0 && measured.glyphCount == 0);
	CHECK(measured.pixelWidth == 0 && measured.pixelHeight == 7 && measured.lineCount == 1);
	const char lines[] = "AB\nC\r\nD\rE";
	CHECK(FontMeasureText(builtin, {7}, lines, sizeof(lines) - 1, &measured) == FontStatus::Ok);
	CHECK(measured.lineCount == 4 && measured.pixelHeight == 28);
	CHECK(measured.pixelWidth == 12 && measured.lastLineAdvancePx == 6);
	CHECK(measured.codePointCount == 9 && measured.glyphCount == 5);

	const char emoji[] = "\xf0\x9f\x99\x82";
	CHECK(FontMeasureText(builtin, {7}, emoji, sizeof(emoji) - 1, &measured) == FontStatus::FallbackUsed);
	CHECK(measured.byteLength == 4 && measured.codePointCount == 1 && measured.glyphCount == 1);
	CHECK(measured.fallbackGlyphCount == 1 && measured.pixelWidth == 6);
}

void testUtf8AndBounds(gxos::gui::FontHandle builtin)
{
	using namespace gxos::gui;
	TextMeasureResult measured = {};
	FontMetrics metrics = {};
	CHECK(FontGetMetrics({0xffffu, 0, 1u}, {7}, &metrics) == FontStatus::InvalidHandle);
	const char invalid[] = {'A', static_cast<char>(0xc3), '(', 0};
	CHECK(FontMeasureText(builtin, {7}, invalid, 3, &measured) == FontStatus::InvalidUtf8);
	CHECK(measured.errorByteOffset == 1 && measured.pixelWidth == 0);
	const char truncated[] = {static_cast<char>(0xe2), static_cast<char>(0x82)};
	CHECK(FontMeasureText(builtin, {7}, truncated, sizeof(truncated), &measured) == FontStatus::InvalidUtf8);
	CHECK(measured.errorByteOffset == 0);
	const char overlong[] = {static_cast<char>(0xc0), static_cast<char>(0xaf)};
	CHECK(FontMeasureText(builtin, {7}, overlong, sizeof(overlong), &measured) == FontStatus::InvalidUtf8);
	const char surrogate[] = {static_cast<char>(0xed), static_cast<char>(0xa0), static_cast<char>(0x80)};
	CHECK(FontMeasureText(builtin, {7}, surrogate, sizeof(surrogate), &measured) == FontStatus::InvalidUtf8);
	CHECK(FontMeasureText(builtin, {7}, "", static_cast<size_t>(kFontMaxTextBytes) + 1u,
		&measured) == FontStatus::InputTooLong);
	const char shortCString[] = "ok";
	size_t cStringLength = 99;
	CHECK(FontUtf8CStringLength(shortCString, &cStringLength) == FontStatus::Ok);
	CHECK(cStringLength == 2);
	char unterminated[kFontMaxTextBytes];
	std::memset(unterminated, 'x', sizeof(unterminated));
	cStringLength = 99;
	CHECK(FontUtf8CStringLength(unterminated, &cStringLength) == FontStatus::InputTooLong);
	CHECK(cStringLength == 0);
	CHECK(FontMeasureText(builtin, {0}, "x", 1, &measured) == FontStatus::InvalidArgument);
	CHECK(FontLookupGlyph(builtin, {7}, 0xd800u, nullptr) == FontStatus::InvalidArgument);
}

void testManagerAndOverflow(gxos::gui::FontHandle builtin)
{
	using namespace gxos::gui;
	const uint32_t initialGeneration = FontManagerGeneration();
	CHECK(FontManagerInitialize() == FontStatus::Ok);
	CHECK(FontManagerGeneration() == initialGeneration);
	FontHandle defaultFont = {0xffffu, 0, 0};
	CHECK(FontManagerGetDefaultFont(&defaultFont) == FontStatus::Ok);
	CHECK(defaultFont.slot == builtin.slot && defaultFont.generation == builtin.generation);
	FontHandle resolved = {0xffffu, 0, 0};
	CHECK(FontManagerResolveFont({0xdeadbeefull}, &resolved) == FontStatus::FallbackUsed);
	CHECK(resolved.slot == builtin.slot && resolved.generation == builtin.generation);

	static const MockFont wide = {1u, 0xffffffffu};
	static const MockFont tall = {0xffffffffu, 1u};
	static const MockFont spare = {1u, 1u};
	static const FontDefinition wideDefinition = {{0x1001u}, "overflow width", &kMockBackend, &wide};
	static const FontDefinition tallDefinition = {{0x1002u}, "overflow height", &kMockBackend, &tall};
	static const FontDefinition spareDefinition = {{0x1003u}, "bounded slot", &kMockBackend, &spare};
	static const FontDefinition excessDefinition = {{0x1004u}, "excess slot", &kMockBackend, &spare};
	FontHandle wideHandle = {0xffffu, 0, 0};
	FontHandle tallHandle = {0xffffu, 0, 0};
	FontHandle spareHandle = {0xffffu, 0, 0};
	FontHandle excessHandle = {0xffffu, 0, 0};
	CHECK(FontManagerRegisterFont(&wideDefinition, &wideHandle) == FontStatus::Ok);
	CHECK(FontManagerRegisterFont(&tallDefinition, &tallHandle) == FontStatus::Ok);
	CHECK(FontManagerRegisterFont(&wideDefinition, &excessHandle) == FontStatus::DuplicateFontId);
	CHECK(FontManagerRegisterFont(&spareDefinition, &spareHandle) == FontStatus::Ok);
	CHECK(FontManagerRegisterFont(&excessDefinition, &excessHandle) == FontStatus::RegistryFull);
	TextMeasureResult measured = {};
	CHECK(FontMeasureText(wideHandle, {1}, "aa", 2, &measured) == FontStatus::ArithmeticOverflow);
	CHECK(FontMeasureText(tallHandle, {0xffffffffu}, "\n", 1, &measured) == FontStatus::ArithmeticOverflow);
	CHECK(FontManagerSetDefaultFont(wideHandle) == FontStatus::Ok);
	const uint32_t changedGeneration = FontManagerGeneration();
	CHECK(changedGeneration == initialGeneration + 1u);
	CHECK(FontManagerSetDefaultFont(wideHandle) == FontStatus::Ok);
	CHECK(FontManagerGeneration() == changedGeneration);
	CHECK(FontManagerSetDefaultFont(builtin) == FontStatus::Ok);
	CHECK(FontManagerGeneration() == changedGeneration + 1u);
	CHECK(FontManagerSetDefaultFont({0xffffu, 0, 1}) == FontStatus::InvalidHandle);
}

void testBitmapDrawCompatibility(gxos::gui::FontHandle builtin)
{
	using namespace gxos::gui;
	static uint32_t shared[24 * 10];
	static uint32_t legacy[24 * 10];
	for (size_t i = 0; i < 24u * 10u; ++i) shared[i] = legacy[i] = 0xff123456u;
	const char text[] = "A?~z";
	CHECK(FontDrawTextToBuffer(builtin, {7}, shared, 24u * 4u, 24u, 10u,
		1, 1, text, sizeof(text) - 1u, 0xffabcdefu, nullptr) == FontStatus::Ok);
	BitmapFont::DrawStringToBuffer(legacy, 24 * 4, 24, 10, 1, 1, text,
		static_cast<int>(sizeof(text) - 1u), 0xffabcdefu);
	CHECK(std::memcmp(shared, legacy, sizeof(shared)) == 0);
	const char emoji[] = "\xf0\x9f\x99\x82";
	for (size_t i = 0; i < 24u * 10u; ++i) shared[i] = legacy[i] = 0;
	CHECK(FontDrawTextToBuffer(builtin, {7}, shared, 24u * 4u, 24u, 10u,
		0, 0, emoji, sizeof(emoji) - 1u, 0xffffffffu, nullptr) == FontStatus::FallbackUsed);
	BitmapFont::DrawStringToBuffer(legacy, 24 * 4, 24, 10, 0, 0, "?", 1, 0xffffffffu);
	CHECK(std::memcmp(shared, legacy, sizeof(shared)) == 0);
}

} // namespace

int main()
{
	using namespace gxos::gui;
	CHECK(!FontManagerIsInitialized());
	FontHandle missing = {0xffffu, 0, 0};
	CHECK(FontManagerGetDefaultFont(&missing) == FontStatus::NotInitialized);
	CHECK(FontManagerInitialize() == FontStatus::Ok);
	CHECK(FontManagerIsInitialized());
	FontHandle builtin = {0xffffu, 0, 0};
	CHECK(FontManagerGetDefaultFont(&builtin) == FontStatus::Ok);
	const uint32_t initializedGeneration = FontManagerGeneration();
	CHECK(FontManagerInitialize() == FontStatus::Ok);
	CHECK(FontManagerGeneration() == initializedGeneration);
	testBitmapAndMeasurement(builtin);
	testUtf8AndBounds(builtin);
	testManagerAndOverflow(builtin);
	testBitmapDrawCompatibility(builtin);
	FontManagerShutdown();
	CHECK(!FontManagerIsInitialized());
	CHECK(FontMeasureText(builtin, {7}, "x", 1, nullptr) == FontStatus::InvalidArgument);
	CHECK(FontManagerInitialize() == FontStatus::Ok);
	FontHandle reinitialized = {0xffffu, 0, 0};
	CHECK(FontManagerGetDefaultFont(&reinitialized) == FontStatus::Ok);
	FontMetrics metrics = {};
	CHECK(FontGetMetrics(builtin, {7}, &metrics) == FontStatus::InvalidHandle);
	CHECK(reinitialized.generation != builtin.generation);
	FontManagerShutdown();
	std::printf("FONT2 font contract: %d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
