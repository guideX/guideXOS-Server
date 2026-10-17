#include "kernel/core/include/kernel/desktop_font.h"
#include "kernel/core/include/kernel/font_contract.h"
#include "kernel/core/include/kernel/font_text_layout.h"
#include "kernel/core/include/kernel/framebuffer.h"
#include "kernel/core/system_font_selection_internal.h"

#include <cstdio>
#include <cstring>

namespace {

static const uint32_t kWidth = 240u;
static const uint32_t kHeight = 96u;
static uint32_t s_front[kWidth * kHeight + 2u] = {};
static uint32_t s_back[kWidth * kHeight + 2u] = {};
static bool s_scalableActive = true;
static bool s_fontDrawStale = false;
static uint32_t s_selectionGeneration = 10u;
static uint32_t s_activePixelSize = 48u;
static uint32_t s_lastScalableSize = 0u;
static uint32_t s_lastScalableX = 0u;
static uint32_t s_lastScalableBaseline = 0u;
static uint32_t s_lastScalableColor = 0u;
static uint32_t s_scalableDrawCount = 0u;
static uint32_t s_bitmapDrawCount = 0u;
static char s_lastScalableText[256] = {};
static char s_lastBitmapText[256] = {};
static int s_lastBitmapX = 0;
static int s_lastBitmapY = 0;

#define CHECK(expression) do { \
	if (!(expression)) { \
		std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); \
		++failures; \
	} \
	++checks; \
} while (0)

static int failures = 0;
static int checks = 0;

bool decodeUtf8(const char* text, size_t length, size_t* offset,
	uint32_t* codePoint)
{
	if (!text || !offset || !codePoint || *offset >= length) return false;
	const uint8_t first = static_cast<uint8_t>(text[*offset]);
	if (first < 0x80u) {
		*codePoint = first;
		++*offset;
		return true;
	}
	uint32_t count = 0u;
	uint32_t value = 0u;
	if (first >= 0xc2u && first <= 0xdfu) { count = 2u; value = first & 0x1fu; }
	else if (first >= 0xe0u && first <= 0xefu) { count = 3u; value = first & 0x0fu; }
	else if (first >= 0xf0u && first <= 0xf4u) { count = 4u; value = first & 0x07u; }
	else return false;
	if (count > length - *offset) return false;
	for (uint32_t i = 1u; i < count; ++i) {
		const uint8_t next = static_cast<uint8_t>(text[*offset + i]);
		if ((next & 0xc0u) != 0x80u || (i == 1u &&
			((first == 0xe0u && next < 0xa0u) || (first == 0xedu && next > 0x9fu) ||
			 (first == 0xf0u && next < 0x90u) || (first == 0xf4u && next > 0x8fu))))
			return false;
		value = (value << 6) | (next & 0x3fu);
	}
	*offset += count;
	*codePoint = value;
	return value <= 0x10ffffu && !(value >= 0xd800u && value <= 0xdfffu);
}

uint32_t advanceFor(uint32_t codePoint)
{
	if (codePoint == 'i') return 2u;
	if (codePoint == 'W') return 10u;
	if (codePoint == '.') return 3u;
	if (codePoint == ' ') return 4u;
	return 6u;
}

} // namespace

namespace kernel {
namespace framebuffer {

bool is_available() { return true; }
uint8_t get_bpp() { return 32u; }
bool is_double_buffered() { return true; }
uint32_t* get_back_buffer() { return s_back; }
uint32_t* get_buffer() { return s_front; }
uint32_t get_width() { return kWidth; }
uint32_t get_height() { return kHeight; }
uint32_t get_pitch() { return kWidth * sizeof(uint32_t); }
void put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
	if (x < kWidth && y < kHeight) s_back[y * kWidth + x] = color;
}

} // namespace framebuffer
} // namespace kernel

namespace gxos {
namespace gui {
namespace internal {

bool systemFontSelectionGetRenderState(FontHandle* outHandle, FontSize* outSize,
	uint32_t* outSelectionGeneration)
{
	if (outSelectionGeneration) *outSelectionGeneration = s_selectionGeneration;
	if (!outHandle || !outSize || !s_scalableActive) return false;
	*outHandle = {1u, 0u, s_selectionGeneration};
	outSize->pixelHeight = s_activePixelSize;
	return true;
}

} // namespace internal

FontStatus FontUtf8CStringLength(const char* utf8, size_t* outByteLength)
{
	if (!utf8 || !outByteLength) return FontStatus::InvalidArgument;
	size_t length = 0u;
	while (length < kFontMaxTextBytes && utf8[length]) ++length;
	if (length == kFontMaxTextBytes) return FontStatus::InputTooLong;
	*outByteLength = length;
	size_t offset = 0u;
	while (offset < length) {
		uint32_t codePoint = 0u;
		if (!decodeUtf8(utf8, length, &offset, &codePoint)) return FontStatus::InvalidUtf8;
	}
	return FontStatus::Ok;
}

FontStatus FontGetMetrics(FontHandle, FontSize size, FontMetrics* outMetrics)
{
	if (!outMetrics || size.pixelHeight == 0u) return FontStatus::InvalidArgument;
	*outMetrics = {size.pixelHeight, size.pixelHeight - 3u, 3u, 1u,
		size.pixelHeight + 4u, size.pixelHeight - 3u};
	return FontStatus::Ok;
}

FontStatus FontTextLayoutSingleLine(FontHandle, FontSize size, const char* utf8,
	size_t byteLength, uint32_t availableWidthPx, FontTextOverflowPolicy overflowPolicy,
	char* output, size_t outputCapacity, FontTextLineLayout* outLayout)
{
	if (!output || outputCapacity == 0u || !outLayout || (!utf8 && byteLength != 0u))
		return FontStatus::InvalidArgument;
	* outLayout = {};
	output[0] = '\0';
	uint32_t advances[4096] = {};
	size_t starts[4096] = {};
	size_t ends[4096] = {};
	uint32_t count = 0u;
	size_t offset = 0u;
	uint32_t fullWidth = 0u;
	bool fallback = false;
	while (offset < byteLength) {
		const size_t start = offset;
		uint32_t codePoint = 0u;
		if (!decodeUtf8(utf8, byteLength, &offset, &codePoint)) return FontStatus::InvalidUtf8;
		if (codePoint == '\r' || codePoint == '\n') return FontStatus::UnsupportedGlyphFeature;
		starts[count] = start;
		ends[count] = offset;
		advances[count] = advanceFor(codePoint);
		fullWidth += advances[count];
		if (codePoint > 0x7eu) fallback = true;
		++count;
		if (count >= 4096u) return FontStatus::InputTooLong;
	}
	uint32_t ascent = size.pixelHeight > 3u ? size.pixelHeight - 3u : 1u;
	uint32_t descent = 3u;
	uint32_t lineHeight = size.pixelHeight + 4u;
	uint32_t baseline = ascent + 2u;
	uint32_t copied = 0u;
	uint32_t advanceWidth = 0u;
	if (overflowPolicy == FontTextOverflowPolicy::Ellipsis && fullWidth > availableWidthPx) {
		uint32_t dotsWidth = 9u;
		uint32_t widthLimit = availableWidthPx > dotsWidth ? availableWidthPx - dotsWidth : 0u;
		while (copied < count && advanceWidth + advances[copied] <= widthLimit &&
			ends[copied] + 4u <= outputCapacity) {
			advanceWidth += advances[copied++];
		}
		if (availableWidthPx >= dotsWidth && copied + 3u < outputCapacity) {
			size_t outLen = 0u;
			for (uint32_t i = 0u; i < copied; ++i) {
				const size_t n = ends[i] - starts[i];
				for (size_t j = 0u; j < n; ++j) output[outLen++] = utf8[starts[i] + j];
			}
			output[outLen++] = '.'; output[outLen++] = '.'; output[outLen++] = '.';
			output[outLen] = '\0';
			const int32_t inkLeft = output[0] == 'j' ? -2 : 0;
			advanceWidth += dotsWidth;
			*outLayout = {size.pixelHeight, ascent, descent, lineHeight, baseline,
				advanceWidth, advanceWidth, ascent, descent, static_cast<uint32_t>(outLen),
				static_cast<uint32_t>(byteLength), inkLeft,
				static_cast<int32_t>(advanceWidth) + inkLeft, -inkLeft,
				1u, 1u, static_cast<uint8_t>(fallback), 0u};
			return fallback ? FontStatus::FallbackUsed : FontStatus::Ok;
		}
	}
	if (byteLength + 1u > outputCapacity) return FontStatus::BufferTooSmall;
	if (byteLength) std::memcpy(output, utf8, byteLength);
	output[byteLength] = '\0';
	advanceWidth = fullWidth;
	const int32_t inkLeft = output[0] == 'j' ? -2 : 0;
	*outLayout = {size.pixelHeight, ascent, descent, lineHeight, baseline,
		advanceWidth, advanceWidth, ascent, descent, static_cast<uint32_t>(byteLength),
		static_cast<uint32_t>(byteLength), inkLeft,
		static_cast<int32_t>(advanceWidth) + inkLeft, -inkLeft,
		static_cast<uint8_t>(fullWidth > availableWidthPx), 0u,
		static_cast<uint8_t>(fallback), 0u};
	return fallback ? FontStatus::FallbackUsed : FontStatus::Ok;
}

FontStatus FontDrawGrayscaleTextToBuffer(FontHandle, FontSize size, uint32_t* pixels,
	size_t destinationBytes, uint32_t pitchBytes, uint32_t bufferWidth,
	uint32_t bufferHeight, int32_t x, int32_t baselineY, const char* utf8,
	size_t byteLength, uint32_t color, TextMeasureResult*)
{
	if (s_fontDrawStale) return FontStatus::InvalidHandle;
	if (!pixels || !utf8 || pitchBytes < bufferWidth * sizeof(uint32_t) ||
	destinationBytes < static_cast<size_t>(pitchBytes) * bufferHeight)
		return FontStatus::InvalidArgument;
	s_lastScalableSize = size.pixelHeight;
	s_lastScalableX = static_cast<uint32_t>(x);
	s_lastScalableBaseline = static_cast<uint32_t>(baselineY);
	s_lastScalableColor = color;
	const size_t copyLength = byteLength < sizeof(s_lastScalableText) - 1u
		? byteLength : sizeof(s_lastScalableText) - 1u;
	std::memcpy(s_lastScalableText, utf8, copyLength);
	s_lastScalableText[copyLength] = '\0';
	++s_scalableDrawCount;
	int32_t pen = 0;
	for (size_t i = 0u; i < byteLength; ++i) {
		const char c = utf8[i];
		int32_t left = x + pen + (c == 'j' ? -2 : 0);
		const int32_t top = baselineY - static_cast<int32_t>(size.pixelHeight > 3u ? size.pixelHeight - 3u : 1u);
		if (left >= 0 && left < static_cast<int32_t>(bufferWidth) && top >= 0 &&
			top < static_cast<int32_t>(bufferHeight))
			pixels[static_cast<size_t>(top) * (pitchBytes / 4u) + static_cast<uint32_t>(left)] = color;
		pen += c == 'i' ? 2 : c == 'W' ? 10 : c == '.' ? 3 : 6;
	}
	return FontStatus::Ok;
}

} // namespace gui
} // namespace gxos

namespace test_font10 {

void resetDrawRecords()
{
	s_lastScalableSize = s_lastScalableX = s_lastScalableBaseline = 0u;
	s_lastScalableColor = s_scalableDrawCount = s_bitmapDrawCount = 0u;
	s_lastScalableText[0] = '\0';
}

uint32_t scalableSize() { return s_lastScalableSize; }
uint32_t scalableX() { return s_lastScalableX; }
uint32_t scalableBaseline() { return s_lastScalableBaseline; }
uint32_t scalableColor() { return s_lastScalableColor; }
uint32_t scalableDrawCount() { return s_scalableDrawCount; }
uint32_t bitmapDrawCount() { return s_bitmapDrawCount; }
const char* scalableText() { return s_lastScalableText; }
uint32_t* pixels() { return s_back; }
void setActive(bool active) { s_scalableActive = active; }
void setActiveSize(uint32_t size) { s_activePixelSize = size; }
void setStale(bool stale) { s_fontDrawStale = stale; }
void setGeneration(uint32_t generation) { s_selectionGeneration = generation; }
const char* bitmapText() { return s_lastBitmapText; }
int bitmapX() { return s_lastBitmapX; }
int bitmapY() { return s_lastBitmapY; }
void recordBitmapDraw(const char* text, int length, int x, int y)
{
	if (s_bitmapDrawCount != 0xffffffffu) ++s_bitmapDrawCount;
	if (length < 0) length = 0;
	const size_t count = static_cast<size_t>(length) < sizeof(s_lastBitmapText) - 1u
		? static_cast<size_t>(length) : sizeof(s_lastBitmapText) - 1u;
	if (text) std::memcpy(s_lastBitmapText, text, count);
	s_lastBitmapText[count] = '\0';
	s_lastBitmapX = x;
	s_lastBitmapY = y;
}

} // namespace test_font10

int main()
{
	using namespace kernel::desktop;
	using namespace gxos::gui;

	s_back[kWidth * kHeight] = 0x13579bdfu;
	s_back[kWidth * kHeight + 1u] = 0x2468ace0u;
	desktop_shell_typography_reset_statistics();
	test_font10::resetDrawRecords();

	CHECK(draw_shell_text_in_rect(8u, 8u, 140u, 22u, "WiWi",
		0xffeeeeeeu, DesktopSystemTextRole::WindowTitle,
		DesktopTextAlignment::Center));
	CHECK(test_font10::scalableSize() == 13u);
	CHECK(std::strcmp(test_font10::scalableText(), "WiWi") == 0);
	CHECK(test_font10::scalableX() == 66u);
	CHECK(test_font10::scalableBaseline() == 22u);
	CHECK(test_font10::scalableColor() == 0xffeeeeeeu);

	DesktopSystemTextMetrics roleMetrics = {};
	CHECK(measure_system_text_for_role("W", DesktopSystemTextRole::TaskbarLabel,
		&roleMetrics));
	CHECK(roleMetrics.pixelSize == 12u);
	CHECK(roleMetrics.lineHeightPx == 16u);
	CHECK(measure_system_text_for_role("Title", DesktopSystemTextRole::WindowTitle,
		&roleMetrics));
	CHECK(roleMetrics.pixelSize == 13u);
	CHECK(measure_system_text_for_role("Start Menu", DesktopSystemTextRole::StartMenuLabel,
		&roleMetrics));
	CHECK(roleMetrics.pixelSize == 12u);
	DesktopSystemTextMetrics preferenceMetrics = {};
	CHECK(measure_system_text("Large preference", &preferenceMetrics));
	CHECK(preferenceMetrics.pixelSize == 48u);
	CHECK(draw_system_text_in_rect(20u, 20u, 100u, 60u, "Large preference", 0xffeeeeeeu));
	CHECK(test_font10::scalableSize() == 48u);
	test_font10::setActiveSize(24u);
	CHECK(measure_system_text("Medium preference", &preferenceMetrics));
	CHECK(preferenceMetrics.pixelSize == 24u);
	CHECK(draw_system_text_in_rect(20u, 20u, 100u, 40u, "Medium preference", 0xffeeeeeeu));
	CHECK(test_font10::scalableSize() == 24u);
	test_font10::setActiveSize(48u);

	const char* longTitle = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
	CHECK(draw_shell_text_in_rect(10u, 34u, 56u, 22u, longTitle, 0xffddddddu,
		DesktopSystemTextRole::WindowTitle, DesktopTextAlignment::Center));
	CHECK(std::strlen(test_font10::scalableText()) < std::strlen(longTitle));
	CHECK(std::strstr(test_font10::scalableText(), "...") != nullptr);

	CHECK(draw_shell_text_in_rect(8u, 60u, 90u, 22u, "Wiii",
		0xffaabbccu, DesktopSystemTextRole::StartMenuLabel,
		DesktopTextAlignment::Left));
	CHECK(test_font10::scalableSize() == 12u);
	CHECK(test_font10::scalableX() == 8u);

	// Active and inactive title colors are supplied by the compositor and pass
	// through the same font-aware geometry without changing the title surface.
	CHECK(draw_shell_text_in_rect(8u, 8u, 140u, 22u, "Active",
		0xfff0f0f0u, DesktopSystemTextRole::WindowTitle,
		DesktopTextAlignment::Center));
	CHECK(test_font10::scalableColor() == 0xfff0f0f0u);
	CHECK(draw_shell_text_in_rect(8u, 8u, 140u, 22u, "Inactive",
		0xff606060u, DesktopSystemTextRole::WindowTitle,
		DesktopTextAlignment::Center));
	CHECK(test_font10::scalableColor() == 0xff606060u);

	// A negative left bearing is offset into the rectangle for both alignments.
	CHECK(draw_shell_text_in_rect(10u, 34u, 32u, 22u, "j",
		0xffeeeeeeu, DesktopSystemTextRole::WindowTitle,
		DesktopTextAlignment::Left));
	CHECK(test_font10::scalableX() == 12u);
	CHECK(draw_shell_text_in_rect(10u, 34u, 32u, 22u, "j",
		0xffeeeeeeu, DesktopSystemTextRole::WindowTitle,
		DesktopTextAlignment::Center));
	CHECK(test_font10::scalableX() == 25u);

	// A narrow taskbar button keeps the stored label bounded to its own box.
	const char* longTaskbarLabel = "guideXOS Navigator";
	CHECK(draw_shell_text_in_rect(10u, 60u, 24u, 22u, longTaskbarLabel,
		0xffeeeeeeu, DesktopSystemTextRole::TaskbarLabel,
		DesktopTextAlignment::Center));
	CHECK(std::strlen(test_font10::scalableText()) < std::strlen(longTaskbarLabel));
	CHECK(std::strstr(test_font10::scalableText(), "...") != nullptr);
	CHECK(test_font10::scalableX() >= 10u && test_font10::scalableX() < 34u);

	// Separate task buttons retain independent centering and colors.
	CHECK(draw_shell_text_in_rect(20u, 60u, 40u, 22u, "First",
		0xffe0e0e0u, DesktopSystemTextRole::TaskbarLabel,
		DesktopTextAlignment::Center));
	CHECK(test_font10::scalableX() == 27u);
	CHECK(draw_shell_text_in_rect(64u, 60u, 40u, 22u, "Second",
		0xff80a0c0u, DesktopSystemTextRole::TaskbarLabel,
		DesktopTextAlignment::Center));
	CHECK(test_font10::scalableX() == 66u);
	CHECK(test_font10::scalableColor() == 0xff80a0c0u);
	const char missingGlyph[] = "\xe2\x98\x83";
	CHECK(draw_shell_text_in_rect(8u, 60u, 90u, 22u, missingGlyph,
		0xffaabbccu, DesktopSystemTextRole::StartMenuLabel,
		DesktopTextAlignment::Left));

	const char invalidUtf8[] = {static_cast<char>(0xc0), static_cast<char>(0xaf), '\0'};
	test_font10::setActive(true);
	CHECK(draw_shell_text_in_rect(8u, 60u, 90u, 22u, invalidUtf8, 0xff123456u,
		DesktopSystemTextRole::StartMenuLabel, DesktopTextAlignment::Left));
	CHECK(test_font10::bitmapDrawCount() > 0u);

	test_font10::setStale(true);
	CHECK(draw_shell_text_in_rect(8u, 60u, 64u, 22u, "Stale handle",
		0xff123456u, DesktopSystemTextRole::TaskbarLabel,
		DesktopTextAlignment::Center));
	test_font10::setStale(false);
	CHECK(test_font10::bitmapDrawCount() > 1u);

	test_font10::setActive(false);
	CHECK(draw_shell_text_in_rect(8u, 60u, 54u, 22u,
		"Very long bitmap fallback label", 0xff123456u,
		DesktopSystemTextRole::TaskbarLabel, DesktopTextAlignment::Center));
	CHECK(std::strstr(test_font10::bitmapText(), "...") != nullptr);
	CHECK(test_font10::bitmapX() >= 8 && test_font10::bitmapX() < 62);
	CHECK(test_font10::bitmapY() >= 60 && test_font10::bitmapY() < 82);

	const uint32_t scalableBeforeBadBounds = test_font10::scalableDrawCount();
	const uint32_t bitmapBeforeBadBounds = test_font10::bitmapDrawCount();
	CHECK(!draw_shell_text_in_rect(kWidth - 3u, 4u, 10u, 20u, "bounds",
		0xffffffffu, DesktopSystemTextRole::WindowTitle,
		DesktopTextAlignment::Center));
	CHECK(test_font10::scalableDrawCount() == scalableBeforeBadBounds);
	CHECK(test_font10::bitmapDrawCount() == bitmapBeforeBadBounds);
	CHECK(test_font10::pixels()[kWidth * kHeight] == 0x13579bdfu);
	CHECK(test_font10::pixels()[kWidth * kHeight + 1u] == 0x2468ace0u);

	test_font10::setActive(true);
	test_font10::setGeneration(11u);
	CHECK(draw_shell_text_in_rect(12u, 60u, 64u, 22u, "Again",
		0xff123456u, DesktopSystemTextRole::TaskbarLabel,
		DesktopTextAlignment::Center));
	test_font10::setGeneration(12u);
	CHECK(draw_shell_text_in_rect(12u, 60u, 64u, 22u, "New generation",
		0xff123456u, DesktopSystemTextRole::TaskbarLabel,
		DesktopTextAlignment::Center));

	char oversizedText[4097] = {};
	for (size_t i = 0u; i < sizeof(oversizedText) - 1u; ++i) oversizedText[i] = 'W';
	CHECK(draw_shell_text_in_rect(8u, 60u, 54u, 22u, oversizedText,
		0xff123456u, DesktopSystemTextRole::StartMenuLabel,
		DesktopTextAlignment::Left));
	CHECK(std::strlen(test_font10::bitmapText()) < 32u);
	CHECK(std::strstr(test_font10::bitmapText(), "...") != nullptr);

	CHECK(!draw_shell_text_in_rect(8u, 60u, 54u, 5u, "Too tall",
		0xff123456u, DesktopSystemTextRole::WindowTitle,
		DesktopTextAlignment::Left));
	DesktopShellTypographyStatistics stats = {};
	desktop_shell_typography_get_statistics(&stats);
	CHECK(stats.textDrawRequests == 19u);
	CHECK(stats.scalableDraws == 13u);
	CHECK(stats.bitmapFallbackDraws == 4u);
	CHECK(stats.bitmapFallbackFailures == 2u);
	CHECK(stats.generationChangesObserved == 2u);
	CHECK(stats.observedSelectionGeneration == 12u);
	desktop_shell_typography_record_frame();
	desktop_shell_typography_get_statistics(&stats);
	CHECK(stats.shellFrames == 1u);

	std::printf("FONT10 shell typography tests: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
