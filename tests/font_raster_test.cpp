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

uint16_t be16(const std::vector<uint8_t>& bytes, size_t offset)
{
	return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8) |
		bytes[offset + 1u]);
}

uint32_t be32(const std::vector<uint8_t>& bytes, size_t offset)
{
	return (static_cast<uint32_t>(bytes[offset]) << 24) |
		(static_cast<uint32_t>(bytes[offset + 1u]) << 16) |
		(static_cast<uint32_t>(bytes[offset + 2u]) << 8) | bytes[offset + 3u];
}

int16_t bes16(const std::vector<uint8_t>& bytes, size_t offset)
{
	const uint16_t raw = be16(bytes, offset);
	return static_cast<int16_t>((raw & 0x8000u) ?
		static_cast<int32_t>(raw) - 65536 : raw);
}

void put16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value)
{
	bytes[offset] = static_cast<uint8_t>(value >> 8);
	bytes[offset + 1u] = static_cast<uint8_t>(value);
}

uint32_t tagValue(const char tag[5])
{
	return (static_cast<uint32_t>(tag[0]) << 24) |
		(static_cast<uint32_t>(tag[1]) << 16) |
		(static_cast<uint32_t>(tag[2]) << 8) | static_cast<uint32_t>(tag[3]);
}

size_t tableEntry(const std::vector<uint8_t>& bytes, const char tag[5])
{
	if (bytes.size() < 12u) return bytes.size();
	const uint16_t count = be16(bytes, 4u);
	for (uint16_t i = 0; i < count; ++i) {
		const size_t entry = 12u + static_cast<size_t>(i) * 16u;
		if (entry + 16u > bytes.size()) return bytes.size();
		if (be32(bytes, entry) == tagValue(tag)) return entry;
	}
	return bytes.size();
}

struct Table {
	uint32_t offset;
	uint32_t length;
};

Table table(const std::vector<uint8_t>& bytes, const char tag[5])
{
	const size_t entry = tableEntry(bytes, tag);
	if (entry >= bytes.size()) return {};
	return {be32(bytes, entry + 8u), be32(bytes, entry + 12u)};
}

uint32_t glyphLocation(const std::vector<uint8_t>& bytes, uint16_t glyphId,
	uint32_t* outEnd);

void set32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
	bytes[offset] = static_cast<uint8_t>(value >> 24);
	bytes[offset + 1u] = static_cast<uint8_t>(value >> 16);
	bytes[offset + 2u] = static_cast<uint8_t>(value >> 8);
	bytes[offset + 3u] = static_cast<uint8_t>(value);
}

void insertGlyfBytes(std::vector<uint8_t>* bytes, uint32_t absoluteOffset,
	uint32_t glyfRelativeOffset, const std::vector<uint8_t>& inserted)
{
	if (!bytes || inserted.empty()) return;
	const size_t directoryCount = be16(*bytes, 4u);
	const size_t glyfEntry = tableEntry(*bytes, "glyf");
	bytes->insert(bytes->begin() + absoluteOffset, inserted.begin(), inserted.end());
	for (size_t i = 0; i < directoryCount; ++i) {
		const size_t entry = 12u + i * 16u;
		uint32_t offset = be32(*bytes, entry + 8u);
		uint32_t length = be32(*bytes, entry + 12u);
		if (entry == glyfEntry) length += static_cast<uint32_t>(inserted.size());
		else if (offset >= absoluteOffset) offset += static_cast<uint32_t>(inserted.size());
		set32(*bytes, entry + 8u, offset);
		set32(*bytes, entry + 12u, length);
	}
	const Table head = table(*bytes, "head");
	const Table loca = table(*bytes, "loca");
	const int16_t format = bes16(*bytes, head.offset + 50u);
	const uint16_t glyphCount = be16(*bytes, table(*bytes, "maxp").offset + 4u);
	for (uint32_t i = 0; i <= glyphCount; ++i) {
		const size_t entry = loca.offset + static_cast<size_t>(i) * (format == 0 ? 2u : 4u);
		uint32_t location = format == 0 ? static_cast<uint32_t>(be16(*bytes, entry)) * 2u :
			be32(*bytes, entry);
		if (location >= glyfRelativeOffset) location += static_cast<uint32_t>(inserted.size());
		if (format == 0) put16(*bytes, entry, static_cast<uint16_t>(location / 2u));
		else set32(*bytes, entry, location);
	}
}

void insertTableBytes(std::vector<uint8_t>* bytes, uint32_t absoluteOffset,
	const char tableTag[5], const std::vector<uint8_t>& inserted)
{
	if (!bytes || inserted.empty()) return;
	const size_t directoryCount = be16(*bytes, 4u);
	const size_t targetEntry = tableEntry(*bytes, tableTag);
	bytes->insert(bytes->begin() + absoluteOffset, inserted.begin(), inserted.end());
	for (size_t i = 0; i < directoryCount; ++i) {
		const size_t entry = 12u + i * 16u;
		uint32_t offset = be32(*bytes, entry + 8u);
		uint32_t length = be32(*bytes, entry + 12u);
		if (entry == targetEntry) length += static_cast<uint32_t>(inserted.size());
		else if (offset >= absoluteOffset) offset += static_cast<uint32_t>(inserted.size());
		set32(*bytes, entry + 8u, offset);
		set32(*bytes, entry + 12u, length);
	}
}

std::vector<uint8_t> withFormat12Mapping(const std::vector<uint8_t>& fixture,
	uint32_t codePoint, uint16_t glyphId)
{
	std::vector<uint8_t> result = fixture;
	const Table cmap = table(result, "cmap");
	const uint16_t recordCount = be16(result, cmap.offset + 2u);
	const uint32_t recordEnd = cmap.offset + 4u + static_cast<uint32_t>(recordCount) * 8u;
	const uint32_t oldLength = cmap.length;
	const std::vector<uint8_t> format12 = {
		0x00u, 0x0cu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x1cu,
		0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u,
		static_cast<uint8_t>(codePoint >> 24), static_cast<uint8_t>(codePoint >> 16),
		static_cast<uint8_t>(codePoint >> 8), static_cast<uint8_t>(codePoint),
		static_cast<uint8_t>(codePoint >> 24), static_cast<uint8_t>(codePoint >> 16),
		static_cast<uint8_t>(codePoint >> 8), static_cast<uint8_t>(codePoint),
		static_cast<uint8_t>(glyphId >> 24), static_cast<uint8_t>(glyphId >> 16),
		static_cast<uint8_t>(glyphId >> 8), static_cast<uint8_t>(glyphId)};
	insertTableBytes(&result, cmap.offset + oldLength, "cmap", format12);
	insertTableBytes(&result, recordEnd, "cmap", std::vector<uint8_t>(8u, 0u));
	const Table updatedCmap = table(result, "cmap");
	put16(result, updatedCmap.offset + 2u, static_cast<uint16_t>(recordCount + 1u));
	for (uint16_t i = 0; i < recordCount; ++i) {
		const size_t record = updatedCmap.offset + 4u + static_cast<size_t>(i) * 8u;
		const uint32_t offset = be32(result, record + 4u);
		set32(result, record + 4u, offset + 8u);
	}
	const size_t newRecord = updatedCmap.offset + 4u + static_cast<size_t>(recordCount) * 8u;
	put16(result, newRecord, 3u);
	put16(result, newRecord + 2u, 10u);
	set32(result, newRecord + 4u, oldLength + 8u);
	return result;
}

bool secondCompositeComponent(const std::vector<uint8_t>& bytes, uint32_t glyphStart,
	uint32_t glyphEnd, uint32_t* outFlagsPosition, uint32_t* outArgsPosition)
{
	uint32_t position = glyphStart + 10u;
	if (position + 4u > glyphEnd) return false;
	const uint16_t firstFlags = be16(bytes, position);
	position += 4u;
	position += (firstFlags & 0x0001u) ? 4u : 2u;
	const uint16_t transform = firstFlags & (0x0008u | 0x0040u | 0x0080u);
	if (transform == 0x0008u) position += 2u;
	else if (transform == 0x0040u) position += 4u;
	else if (transform == 0x0080u) position += 8u;
	if ((firstFlags & 0x0020u) == 0u || position + 4u > glyphEnd) return false;
	if (outFlagsPosition) *outFlagsPosition = position;
	if (outArgsPosition) *outArgsPosition = position + 4u;
	return true;
}

bool findRepeatedFlag(const std::vector<uint8_t>& fixture, FontHandle font,
	UnicodeCodePoint* outCodePoint, uint32_t* outRepeatByte)
{
	for (UnicodeCodePoint cp = 32u; cp <= 126u; ++cp) {
		FontGlyphView view = {};
		if (FontLookupGlyph(font, {16u}, cp, &view) != FontStatus::Ok) continue;
		uint32_t end = 0u;
		const uint32_t start = glyphLocation(fixture,
			static_cast<uint16_t>(view.metrics.glyphId), &end);
		if (start == end || end - start < 12u) continue;
		const int16_t contours = bes16(fixture, start);
		if (contours <= 0 || contours > 128) continue;
		uint16_t lastEndpoint = 0u;
		bool endpointOk = true;
		for (int16_t i = 0; i < contours; ++i) {
			const size_t endpointOffset = start + 10u + static_cast<size_t>(i) * 2u;
			if (endpointOffset + 2u > end) { endpointOk = false; break; }
			lastEndpoint = be16(fixture, endpointOffset);
		}
		if (!endpointOk) continue;
		const uint32_t points = static_cast<uint32_t>(lastEndpoint) + 1u;
		uint32_t position = start + 10u + static_cast<uint32_t>(contours) * 2u;
		if (position + 2u > end) continue;
		const uint16_t instructions = be16(fixture, position);
		position += 2u + instructions;
		if (position > end) continue;
		for (uint32_t expanded = 0u; expanded < points;) {
			if (position >= end) break;
			const uint8_t flag = fixture[position++];
			uint32_t repeat = 0u;
			if ((flag & 0x08u) != 0u) {
				if (position >= end) break;
				const uint32_t repeatPosition = position++;
				repeat = fixture[repeatPosition];
				if (repeat != 0u) {
					*outCodePoint = cp;
					*outRepeatByte = repeatPosition;
					return true;
				}
			}
			expanded += repeat + 1u;
		}
	}
	return false;
}

std::vector<uint8_t> readFixture()
{
	std::ifstream input("assets/Fonts/roboto/Roboto-Regular.ttf", std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

uint32_t glyphLocation(const std::vector<uint8_t>& bytes, uint16_t glyphId,
	uint32_t* outEnd)
{
	const Table head = table(bytes, "head");
	const Table loca = table(bytes, "loca");
	const Table glyf = table(bytes, "glyf");
	const int16_t format = bes16(bytes, head.offset + 50u);
	const uint32_t start = format == 0 ?
		static_cast<uint32_t>(be16(bytes, loca.offset + glyphId * 2u)) * 2u :
		be32(bytes, loca.offset + static_cast<size_t>(glyphId) * 4u);
	const uint32_t end = format == 0 ?
		static_cast<uint32_t>(be16(bytes, loca.offset + (glyphId + 1u) * 2u)) * 2u :
		be32(bytes, loca.offset + static_cast<size_t>(glyphId + 1u) * 4u);
	if (outEnd) *outEnd = glyf.offset + end;
	return glyf.offset + start;
}

uint64_t hashBitmap(const uint8_t* pixels, const GlyphMetrics& metrics, uint32_t stride)
{
	uint64_t hash = 1469598103934665603ull;
	for (uint32_t y = 0; y < metrics.heightPx; ++y) {
		for (uint32_t x = 0; x < metrics.widthPx; ++x) {
			hash ^= pixels[static_cast<size_t>(y) * stride + x];
			hash *= 1099511628211ull;
		}
	}
	return hash;
}

uint32_t countCoverage(const uint8_t* pixels, const GlyphMetrics& metrics, uint32_t stride)
{
	uint32_t nonzero = 0u;
	for (uint32_t y = 0; y < metrics.heightPx; ++y)
		for (uint32_t x = 0; x < metrics.widthPx; ++x)
			if (pixels[static_cast<size_t>(y) * stride + x] != 0u) ++nonzero;
	return nonzero;
}

bool withinOnePixel(uint32_t actual, uint32_t expected)
{
	return actual <= expected + 1u && expected <= actual + 1u;
}

bool load(const std::vector<uint8_t>& bytes, uint64_t id, FontHandle* out)
{
	return FontTrueTypeLoadMemory({id}, bytes.data(), bytes.size(), out, nullptr) == FontStatus::Ok;
}

FontStatus render(FontHandle handle, uint32_t size, UnicodeCodePoint cp,
	std::vector<uint8_t>* pixels, uint32_t stride, GlyphMetrics* out)
{
	return FontRasterizeGlyph(handle, {size}, cp,
		pixels && !pixels->empty() ? pixels->data() : nullptr,
		pixels ? pixels->size() : 0u, stride, out);
}

void testAsciiAndMetrics(FontHandle handle, FontTrueTypeMetadata metadata)
{
	CHECK((metadata.cmapFlags & FontTrueTypeCmapFormat4) != 0u);
	std::vector<uint8_t> pixels(128u * 128u, 0xccu);
	uint32_t narrowAdvance = 0u, wideAdvance = 0u;
	GlyphMetrics uppercaseA = {}, lowercaseA = {}, digit1 = {}, whitespace = {};
	for (UnicodeCodePoint cp = 32u; cp <= 126u; ++cp) {
		GlyphMetrics metrics = {};
		const FontStatus status = render(handle, 24u, cp, &pixels, 128u, &metrics);
		if (status != FontStatus::Ok)
			std::fprintf(stderr, "ASCII U+%04x status=%u dims=%ux%u advance=%u\\n",
				cp, static_cast<unsigned>(status), metrics.widthPx, metrics.heightPx, metrics.advancePx);
		CHECK(status == FontStatus::Ok);
		CHECK(metrics.glyphId < metadata.glyphCount);
		CHECK(metrics.codePoint == cp);
		CHECK(metrics.advancePx > 0u);
		CHECK(metrics.widthPx <= kFontTrueTypeMaxBitmapDimension);
		CHECK(metrics.heightPx <= kFontTrueTypeMaxBitmapDimension);
		FontGlyphView measured = {};
		CHECK(FontLookupGlyph(handle, {24u}, cp, &measured) == FontStatus::Ok);
		CHECK(measured.metrics.glyphId == metrics.glyphId);
		CHECK(measured.metrics.codePoint == metrics.codePoint);
		CHECK(measured.metrics.xBearingPx == metrics.xBearingPx);
		CHECK(measured.metrics.yBearingPx == metrics.yBearingPx);
		CHECK(measured.metrics.widthPx == metrics.widthPx);
		CHECK(measured.metrics.heightPx == metrics.heightPx);
		CHECK(measured.metrics.advancePx == metrics.advancePx);
		CHECK(measured.metrics.sideBearingPx == metrics.sideBearingPx);
		if (cp == 'i') narrowAdvance = metrics.advancePx;
		if (cp == 'W') wideAdvance = metrics.advancePx;
		if (cp == 'A') uppercaseA = metrics;
		if (cp == 'a') lowercaseA = metrics;
		if (cp == '1') digit1 = metrics;
		if (cp == ' ') whitespace = metrics;
	}
	CHECK(wideAdvance > narrowAdvance);
	CHECK(uppercaseA.glyphId != lowercaseA.glyphId);
	CHECK(uppercaseA.advancePx != lowercaseA.advancePx);
	CHECK(digit1.advancePx > 0u);
	CHECK(whitespace.widthPx == 0u && whitespace.heightPx == 0u);
	CHECK(whitespace.advancePx > 0u);
	CHECK(uppercaseA.widthPx > 0u && uppercaseA.heightPx > 0u);
	CHECK(uppercaseA.sideBearingPx >= 0);

	// A NUL is a valid Unicode scalar with no cmap entry. The shared contract
	// substitutes the font's question-mark glyph and keeps the requested scalar.
	GlyphMetrics fallback = {};
	const FontStatus fallbackStatus = render(handle, 24u, 0x10ffffu, &pixels, 128u, &fallback);
	if (fallbackStatus != FontStatus::FallbackUsed)
		std::fprintf(stderr, "fallback status=%u dims=%ux%u gid=%u\\n",
			static_cast<unsigned>(fallbackStatus), fallback.widthPx, fallback.heightPx, fallback.glyphId);
	CHECK(fallbackStatus == FontStatus::FallbackUsed);
	FontGlyphView question = {};
	CHECK(FontLookupGlyph(handle, {24u}, '?', &question) == FontStatus::Ok);
	CHECK(fallback.glyphId == question.metrics.glyphId);
	CHECK(fallback.codePoint == 0x10ffffu);

	GlyphMetrics tooLarge = {};	std::fill(pixels.begin(), pixels.end(), 0xacu);
	CHECK(render(handle, kFontTrueTypeMaxPixelSize + 1u, 'A', &pixels, 128u,
		&tooLarge) == FontStatus::UnsupportedSize);
	CHECK(pixels[0] == 0xacu && pixels.back() == 0xacu);
}

void testIndependentGoldenMetrics(FontHandle handle)
{
	// These unhinted glyph metrics were established independently with FreeType
	// 2.14.3 (FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT). Its 26.6 control box can
	// round an edge by one pixel relative to this renderer's 16.16 curve bounds.
	struct Golden {
		UnicodeCodePoint codePoint;
		uint32_t size;
		uint32_t advance;
		int32_t left;
		int32_t top;
		uint32_t width;
		uint32_t height;
		int32_t sideBearing;
	};
	const Golden goldens[] = {
		{'A', 24u, 16u, 0, 18, 16u, 18u, 0},
		{'i', 24u, 6u, 1, 18, 4u, 18u, 2},
		{'W', 24u, 21u, 0, 18, 21u, 18u, 1},
		{'g', 24u, 13u, 1, 13, 11u, 18u, 1},
		{'j', 24u, 6u, -1, 18, 6u, 24u, -1},
		{'O', 32u, 22u, 1, 24, 20u, 25u, 2},
		{0x00e9u, 24u, 13u, 1, 18, 11u, 19u, 1},
	};
	std::vector<uint8_t> pixels(128u * 128u, 0u);
	for (const Golden& expected : goldens) {
		GlyphMetrics actual = {};
		CHECK(render(handle, expected.size, expected.codePoint, &pixels, 128u,
			&actual) == FontStatus::Ok);
		CHECK(actual.advancePx == expected.advance);
		CHECK(actual.xBearingPx == expected.left);
		CHECK(actual.yBearingPx == expected.top);
		CHECK(withinOnePixel(actual.widthPx, expected.width));
		CHECK(withinOnePixel(actual.heightPx, expected.height));
		CHECK(actual.sideBearingPx == expected.sideBearing);
	}
}

void testCurvesCompositesAndDeterminism(const std::vector<uint8_t>& fixture,
	FontHandle handle)
{
	std::vector<uint8_t> pixels(128u * 128u, 0u);
	FontGlyphView oGlyph = {}, eAcute = {}, gGlyph = {}, jGlyph = {};
	CHECK(FontLookupGlyph(handle, {32u}, 'O', &oGlyph) == FontStatus::Ok);
	CHECK(FontLookupGlyph(handle, {32u}, 0x00e9u, &eAcute) == FontStatus::Ok);
	CHECK(FontLookupGlyph(handle, {32u}, 'g', &gGlyph) == FontStatus::Ok);
	CHECK(FontLookupGlyph(handle, {32u}, 'j', &jGlyph) == FontStatus::Ok);
	CHECK(oGlyph.metrics.glyphId < 0xffffu && eAcute.metrics.glyphId < 0xffffu);
	CHECK(gGlyph.metrics.yBearingPx > 0 && gGlyph.metrics.heightPx > 0u);
	CHECK(jGlyph.metrics.xBearingPx <= 0);

	GlyphMetrics oMetrics = {};
	CHECK(render(handle, 32u, 'O', &pixels, 128u, &oMetrics) == FontStatus::Ok);
	CHECK(countCoverage(pixels.data(), oMetrics, 128u) > 0u);
	CHECK(oMetrics.widthPx > 8u && oMetrics.heightPx > 8u);
	const uint32_t centerX = oMetrics.widthPx / 2u;
	const uint32_t centerY = oMetrics.heightPx / 2u;
	CHECK(pixels[static_cast<size_t>(centerY) * 128u + centerX] == 0u);
	bool antialiased = false;
	for (uint32_t y = 0; y < oMetrics.heightPx; ++y)
		for (uint32_t x = 0; x < oMetrics.widthPx; ++x) {
			const uint8_t value = pixels[static_cast<size_t>(y) * 128u + x];
			if (value != 0u && value != 255u) antialiased = true;
		}
	CHECK(antialiased);
	const uint64_t firstHash = hashBitmap(pixels.data(), oMetrics, 128u);
	std::fill(pixels.begin(), pixels.end(), 0u);
	GlyphMetrics again = {};
	CHECK(render(handle, 32u, 'O', &pixels, 128u, &again) == FontStatus::Ok);
	CHECK(firstHash == hashBitmap(pixels.data(), again, 128u));
	CHECK(oMetrics.widthPx == again.widthPx && oMetrics.heightPx == again.heightPx);

	GlyphMetrics composite = {};
	const FontStatus compositeStatus = render(handle, 24u, 0x00e9u, &pixels, 128u, &composite);
	if (compositeStatus != FontStatus::Ok)
		std::fprintf(stderr, "composite status=%u dims=%ux%u\\n",
			static_cast<unsigned>(compositeStatus), composite.widthPx, composite.heightPx);
	CHECK(compositeStatus == FontStatus::Ok);
	CHECK(composite.glyphId == eAcute.metrics.glyphId);
	CHECK(countCoverage(pixels.data(), composite, 128u) > 0u);
	FontGlyphView compositeMeasured = {};
	CHECK(FontLookupGlyph(handle, {24u}, 0x00e9u, &compositeMeasured) == FontStatus::Ok);
	CHECK(compositeMeasured.metrics.xBearingPx == composite.xBearingPx);
	CHECK(compositeMeasured.metrics.yBearingPx == composite.yBearingPx);
	CHECK(compositeMeasured.metrics.widthPx == composite.widthPx);
	CHECK(compositeMeasured.metrics.heightPx == composite.heightPx);
	CHECK(compositeMeasured.metrics.advancePx == composite.advancePx);
	CHECK(compositeMeasured.metrics.sideBearingPx == composite.sideBearingPx);
	const uint32_t compositeStart = glyphLocation(fixture,
		static_cast<uint16_t>(eAcute.metrics.glyphId), nullptr);
	CHECK(bes16(fixture, compositeStart) == -1);

	GlyphMetrics empty = {};
	std::fill(pixels.begin(), pixels.end(), 0x69u);
	CHECK(render(handle, 24u, ' ', &pixels, 128u, &empty) == FontStatus::Ok);
	CHECK(empty.widthPx == 0u && empty.heightPx == 0u && empty.advancePx > 0u);
	CHECK(std::all_of(pixels.begin(), pixels.end(), [](uint8_t value) { return value == 0x69u; }));

	for (uint32_t size : {8u, 16u, 48u}) {
		std::fill(pixels.begin(), pixels.end(), 0u);
		GlyphMetrics sized = {};
		CHECK(render(handle, size, 'g', &pixels, 128u, &sized) == FontStatus::Ok);
		CHECK(sized.advancePx > 0u && countCoverage(pixels.data(), sized, 128u) > 0u);
	}
}

void testBufferBounds(FontHandle handle)
{
	GlyphMetrics metrics = {};
	std::vector<uint8_t> guarded(2u + 128u * 128u + 8u, 0xb6u);
	uint8_t* target = guarded.data() + 1u;
	CHECK(FontRasterizeGlyph(handle, {32u}, 'A', target, 128u * 128u,
		128u, &metrics) == FontStatus::Ok);
	CHECK(guarded.front() == 0xb6u && guarded.back() == 0xb6u);
	for (uint32_t y = 0; y < metrics.heightPx; ++y)
		for (uint32_t x = metrics.widthPx; x < 128u; ++x)
			CHECK(target[static_cast<size_t>(y) * 128u + x] == 0xb6u);

	std::vector<uint8_t> shortBuffer(128u * 128u, 0x5du);
	GlyphMetrics required = {};
	CHECK(FontRasterizeGlyph(handle, {32u}, 'A', shortBuffer.data(), 1u,
		128u, &required) == FontStatus::BufferTooSmall);
	CHECK(required.widthPx > 0u && required.heightPx > 0u);
	CHECK(std::all_of(shortBuffer.begin(), shortBuffer.end(),
		[](uint8_t value) { return value == 0x5du; }));
	CHECK(FontRasterizeGlyph(handle, {32u}, 'A', shortBuffer.data(), shortBuffer.size(),
		0xffffffffu, &required) == FontStatus::InvalidArgument);
	CHECK(FontRasterizeGlyph(handle, {32u}, 'A', shortBuffer.data(), shortBuffer.size(),
		1u, &required) == FontStatus::InvalidArgument);
}

void testCompositeTransforms(const std::vector<uint8_t>& fixture, FontHandle baseHandle)
{
	FontGlyphView composite = {};
	CHECK(FontLookupGlyph(baseHandle, {24u}, 0x00e9u, &composite) == FontStatus::Ok);
	const uint16_t glyphId = static_cast<uint16_t>(composite.metrics.glyphId);
	std::vector<uint8_t> pixels(128u * 128u, 0u);
	GlyphMetrics originalMetrics = {};
	CHECK(FontRasterizeGlyph(baseHandle, {24u}, 0x00e9u, pixels.data(), pixels.size(),
		128u, &originalMetrics) == FontStatus::Ok);
	const uint64_t originalHash = hashBitmap(pixels.data(), originalMetrics, 128u);

	std::vector<uint8_t> scaled = fixture;
	uint32_t end = 0u;
	const uint32_t start = glyphLocation(scaled, glyphId, &end);
	const uint16_t firstFlags = be16(scaled, start + 10u);
	CHECK((firstFlags & (0x0008u | 0x0040u | 0x0080u)) == 0u);
	const uint32_t argsLength = (firstFlags & 0x0001u) ? 4u : 2u;
	const Table glyf = table(scaled, "glyf");
	const uint32_t scalePosition = start + 14u + argsLength;
	insertGlyfBytes(&scaled, scalePosition, scalePosition - glyf.offset,
		std::vector<uint8_t>{0x20u, 0x00u});
	uint32_t updatedEnd = 0u;
	glyphLocation(scaled, glyphId, &updatedEnd);
	const Table updatedGlyf = table(scaled, "glyf");
	insertGlyfBytes(&scaled, updatedEnd, updatedEnd - updatedGlyf.offset,
		std::vector<uint8_t>{0u, 0u});
	const uint32_t updatedStart = glyphLocation(scaled, glyphId, nullptr);
	const uint16_t scaledFlags = static_cast<uint16_t>(
		(be16(scaled, updatedStart + 10u) & ~(0x0008u | 0x0040u | 0x0080u)) | 0x0008u);
	put16(scaled, updatedStart + 10u, scaledFlags);
	FontTrueTypeMetadata metadata = {};
	CHECK(FontTrueTypeValidate(scaled.data(), scaled.size(), &metadata) == FontTrueTypeOk);
	FontHandle scaledHandle = {};
	CHECK(load(scaled, 0x46544d000000000au, &scaledHandle));
	GlyphMetrics scaledMetrics = {};
	CHECK(FontRasterizeGlyph(scaledHandle, {24u}, 0x00e9u, pixels.data(), pixels.size(),
		128u, &scaledMetrics) == FontStatus::Ok);
	CHECK(scaledMetrics.widthPx > 0u && scaledMetrics.heightPx > 0u);
	CHECK(hashBitmap(pixels.data(), scaledMetrics, 128u) != originalHash);
	CHECK(FontManagerUnloadFont(scaledHandle) == FontStatus::Ok);

	std::vector<uint8_t> pointMatched = fixture;
	uint32_t originalEnd = 0u;
	const uint32_t originalStart = glyphLocation(pointMatched, glyphId, &originalEnd);
	uint32_t flagsPosition = 0u, argsPosition = 0u;
	CHECK(secondCompositeComponent(pointMatched, originalStart, originalEnd,
		&flagsPosition, &argsPosition));
	uint16_t secondFlags = be16(pointMatched, flagsPosition);
	if ((secondFlags & 0x0001u) == 0u) {
		const uint32_t argsEnd = argsPosition + 2u;
		insertGlyfBytes(&pointMatched, argsEnd,
			argsEnd - table(pointMatched, "glyf").offset,
			std::vector<uint8_t>{0u, 0u});
		uint32_t pointEnd = 0u;
		glyphLocation(pointMatched, glyphId, &pointEnd);
		const Table currentGlyf = table(pointMatched, "glyf");
		insertGlyfBytes(&pointMatched, pointEnd, pointEnd - currentGlyf.offset,
			std::vector<uint8_t>{0u, 0u});
	}
	const uint32_t pointStart = glyphLocation(pointMatched, glyphId, &end);
	const bool pointComponentsFound = secondCompositeComponent(pointMatched, pointStart,
		end, &flagsPosition, &argsPosition);
	if (!pointComponentsFound)
		std::fprintf(stderr, "point composite range=%u..%u firstflags=%04x bytes:",
			pointStart, end, be16(pointMatched, pointStart + 10u));
	if (!pointComponentsFound) {
		for (uint32_t i = 0; i < end - pointStart && i < 40u; ++i)
			std::fprintf(stderr, " %02x", pointMatched[pointStart + i]);
		std::fprintf(stderr, "\\n");
	}
	CHECK(pointComponentsFound);
	secondFlags = be16(pointMatched, flagsPosition);
	secondFlags = static_cast<uint16_t>((secondFlags | 0x0001u) & ~0x0002u);
	put16(pointMatched, flagsPosition, secondFlags);
	put16(pointMatched, argsPosition, 0u);
	put16(pointMatched, argsPosition + 2u, 0u);
	CHECK(FontTrueTypeValidate(pointMatched.data(), pointMatched.size(), &metadata) == FontTrueTypeOk);
	FontHandle pointHandle = {};
	CHECK(load(pointMatched, 0x46544d000000000bull, &pointHandle));
	GlyphMetrics pointMetrics = {};
	CHECK(FontRasterizeGlyph(pointHandle, {24u}, 0x00e9u, pixels.data(), pixels.size(),
		128u, &pointMetrics) == FontStatus::Ok);
	CHECK(pointMetrics.widthPx > 0u && pointMetrics.heightPx > 0u);
	CHECK(FontManagerUnloadFont(pointHandle) == FontStatus::Ok);
}

void testFormat12Mapping(const std::vector<uint8_t>& fixture, FontHandle baseHandle)
{
	FontGlyphView letterA = {};
	CHECK(FontLookupGlyph(baseHandle, {16u}, 'A', &letterA) == FontStatus::Ok);
	const std::vector<uint8_t> format12Fixture = withFormat12Mapping(fixture,
		0x1f600u, static_cast<uint16_t>(letterA.metrics.glyphId));
	FontTrueTypeMetadata metadata = {};
	CHECK(FontTrueTypeValidate(format12Fixture.data(), format12Fixture.size(),
		&metadata) == FontTrueTypeOk);
	CHECK((metadata.cmapFlags & FontTrueTypeCmapFormat4) != 0u);
	CHECK((metadata.cmapFlags & FontTrueTypeCmapFormat12) != 0u);
	FontHandle format12Font = {};
	CHECK(load(format12Fixture, 0x46544d000000000cull, &format12Font));
	FontGlyphView mapped = {};
	CHECK(FontLookupGlyph(format12Font, {16u}, 0x1f600u, &mapped) == FontStatus::Ok);
	CHECK(mapped.metrics.glyphId == letterA.metrics.glyphId);
	CHECK(mapped.metrics.codePoint == 0x1f600u);
	std::vector<uint8_t> pixels(128u * 128u, 0u);
	GlyphMetrics metrics = {};
	CHECK(FontRasterizeGlyph(format12Font, {16u}, 0x1f600u, pixels.data(), pixels.size(),
		128u, &metrics) == FontStatus::Ok);
	CHECK(metrics.glyphId == letterA.metrics.glyphId && metrics.codePoint == 0x1f600u);
	CHECK(countCoverage(pixels.data(), metrics, 128u) > 0u);
	CHECK(FontManagerUnloadFont(format12Font) == FontStatus::Ok);
}

void testMalformedGlyphs(const std::vector<uint8_t>& fixture, FontHandle baseHandle)
{
	FontGlyphView a = {}, eAcute = {};
	CHECK(FontLookupGlyph(baseHandle, {16u}, 'A', &a) == FontStatus::Ok);
	CHECK(FontLookupGlyph(baseHandle, {16u}, 0x00e9u, &eAcute) == FontStatus::Ok);
	const uint16_t aId = static_cast<uint16_t>(a.metrics.glyphId);
	const uint16_t compositeId = static_cast<uint16_t>(eAcute.metrics.glyphId);
	std::vector<uint8_t> pixels(128u * 128u, 0xacu);
	GlyphMetrics metrics = {};

	std::vector<uint8_t> badLoca = fixture;
	const Table head = table(badLoca, "head");
	const Table loca = table(badLoca, "loca");
	const int16_t locaFormat = bes16(badLoca, head.offset + 50u);
	if (locaFormat == 0) put16(badLoca, loca.offset + (aId + 1u) * 2u, 0u);
	else put16(badLoca, loca.offset + static_cast<size_t>(aId + 1u) * 4u, 0u);
	FontHandle invalidHandle = {};
	CHECK(FontTrueTypeLoadMemory({0x46544d0000000001ull}, badLoca.data(), badLoca.size(),
		&invalidHandle, nullptr) == FontStatus::InvalidFontFile);

	std::vector<uint8_t> excessiveContours = fixture;
	const uint32_t aOffset = glyphLocation(excessiveContours, aId, nullptr);
	put16(excessiveContours, aOffset, 129u);
	CHECK(load(excessiveContours, 0x46544d0000000002ull, &invalidHandle));
	CHECK(FontRasterizeGlyph(invalidHandle, {16u}, 'A', pixels.data(), pixels.size(),
		128u, &metrics) == FontStatus::RasterLimitExceeded);
	CHECK(std::all_of(pixels.begin(), pixels.end(),
		[](uint8_t value) { return value == 0xacu; }));
	CHECK(FontManagerUnloadFont(invalidHandle) == FontStatus::Ok);

	std::vector<uint8_t> truncatedSimple = fixture;
	uint32_t aEnd = 0u;
	const uint32_t aStart = glyphLocation(truncatedSimple, aId, &aEnd);
	const uint32_t simpleOffset = aStart - table(truncatedSimple, "glyf").offset;
	const uint32_t nextLocation = (simpleOffset + 4u) & ~1u;
	if (locaFormat == 0) put16(truncatedSimple, loca.offset + (aId + 1u) * 2u,
		static_cast<uint16_t>(nextLocation / 2u));
	else {
		const size_t offset = loca.offset + static_cast<size_t>(aId + 1u) * 4u;
		truncatedSimple[offset] = static_cast<uint8_t>(nextLocation >> 24);
		truncatedSimple[offset + 1u] = static_cast<uint8_t>(nextLocation >> 16);
		truncatedSimple[offset + 2u] = static_cast<uint8_t>(nextLocation >> 8);
		truncatedSimple[offset + 3u] = static_cast<uint8_t>(nextLocation);
	}
	CHECK(aEnd > aStart + 10u);
	CHECK(load(truncatedSimple, 0x46544d0000000003ull, &invalidHandle));
	CHECK(FontRasterizeGlyph(invalidHandle, {16u}, 'A', pixels.data(), pixels.size(),
		128u, &metrics) == FontStatus::InvalidGlyphData);
	CHECK(FontManagerUnloadFont(invalidHandle) == FontStatus::Ok);

	std::vector<uint8_t> brokenComposite = fixture;
	const uint32_t compStart = glyphLocation(brokenComposite, compositeId, nullptr);
	CHECK(bes16(brokenComposite, compStart) == -1);
	put16(brokenComposite, compStart + 12u, compositeId);
	CHECK(load(brokenComposite, 0x46544d0000000004ull, &invalidHandle));
	const FontStatus cycleStatus = FontRasterizeGlyph(invalidHandle, {16u}, 0x00e9u,
		pixels.data(), pixels.size(), 128u, &metrics);
	if (cycleStatus != FontStatus::InvalidGlyphData)
		std::fprintf(stderr, "cycle status=%u\\n", static_cast<unsigned>(cycleStatus));
	CHECK(cycleStatus == FontStatus::InvalidGlyphData);
	CHECK(FontManagerUnloadFont(invalidHandle) == FontStatus::Ok);

	std::vector<uint8_t> badComponent = fixture;
	const uint32_t badCompStart = glyphLocation(badComponent, compositeId, nullptr);
	put16(badComponent, badCompStart + 12u, 0xffffu);
	CHECK(load(badComponent, 0x46544d0000000005ull, &invalidHandle));
	CHECK(FontRasterizeGlyph(invalidHandle, {16u}, 0x00e9u, pixels.data(), pixels.size(),
		128u, &metrics) == FontStatus::InvalidGlyphData);
	CHECK(FontManagerUnloadFont(invalidHandle) == FontStatus::Ok);

	std::vector<uint8_t> unsupportedPosition = fixture;
	const uint32_t unsupportedStart = glyphLocation(unsupportedPosition, compositeId, nullptr);
	put16(unsupportedPosition, unsupportedStart + 10u,
		static_cast<uint16_t>(be16(unsupportedPosition, unsupportedStart + 10u) | 0x0010u));
	CHECK(load(unsupportedPosition, 0x46544d0000000006ull, &invalidHandle));
	CHECK(FontRasterizeGlyph(invalidHandle, {16u}, 0x00e9u, pixels.data(), pixels.size(),
		128u, &metrics) == FontStatus::UnsupportedGlyphFeature);
	CHECK(FontManagerUnloadFont(invalidHandle) == FontStatus::Ok);

	FontHandle stale = {};
	CHECK(load(fixture, 0x46544d0000000007ull, &stale));
	CHECK(FontManagerUnloadFont(stale) == FontStatus::Ok);
	std::fill(pixels.begin(), pixels.end(), 0x91u);
	CHECK(FontRasterizeGlyph(stale, {16u}, 'A', pixels.data(), pixels.size(),
		128u, &metrics) == FontStatus::InvalidHandle);
	CHECK(std::all_of(pixels.begin(), pixels.end(),
		[](uint8_t value) { return value == 0x91u; }));
}

void testRasterWorkLimit(const std::vector<uint8_t>& fixture, FontHandle baseHandle)
{
	FontGlyphView letter = {};
	CHECK(FontLookupGlyph(baseHandle, {128u}, 'A', &letter) == FontStatus::Ok);
	const uint16_t glyphId = static_cast<uint16_t>(letter.metrics.glyphId);
	uint32_t oldEnd = 0u;
	const uint32_t oldStart = glyphLocation(fixture, glyphId, &oldEnd);
	const uint32_t oldLength = oldEnd - oldStart;
	std::vector<uint8_t> workGlyph(10u + 2u + 2u + 1024u + 1024u * 4u, 0u);
	put16(workGlyph, 0u, 1u);
	put16(workGlyph, 2u, 0u);
	put16(workGlyph, 4u, 0u);
	put16(workGlyph, 6u, 1000u);
	put16(workGlyph, 8u, 1000u);
	put16(workGlyph, 10u, 1023u);
	put16(workGlyph, 12u, 0u);
	const uint32_t xDeltas = 14u + 1024u;
	const uint32_t yDeltas = xDeltas + 1024u * 2u;
	int32_t previousX = 0, previousY = 0;
	for (uint32_t i = 0; i < 1024u; ++i) {
		const uint32_t phase = i & 3u;
		const int32_t x = (phase == 1u || phase == 2u) ? 1000 : 0;
		const int32_t y = (phase >= 2u) ? 1000 : 0;
		put16(workGlyph, 14u + i, 0u); // All 1,024 points are off-curve.
		put16(workGlyph, xDeltas + static_cast<size_t>(i) * 2u,
			static_cast<uint16_t>(x - previousX));
		put16(workGlyph, yDeltas + static_cast<size_t>(i) * 2u,
			static_cast<uint16_t>(y - previousY));
		previousX = x;
		previousY = y;
	}
	CHECK(workGlyph.size() > oldLength);
	if (workGlyph.size() <= oldLength) return;
	std::vector<uint8_t> mutated = fixture;
	const Table glyf = table(mutated, "glyf");
	const uint32_t relativeEnd = oldEnd - glyf.offset;
	insertGlyfBytes(&mutated, oldEnd, relativeEnd,
		std::vector<uint8_t>(workGlyph.size() - oldLength, 0u));
	std::copy(workGlyph.begin(), workGlyph.end(), mutated.begin() + oldStart);
	FontHandle workFont = {};
	CHECK(load(mutated, 0x46544d000000000bull, &workFont));
	std::vector<uint8_t> pixels(128u * 128u, 0x4du);
	GlyphMetrics metrics = {};
	CHECK(FontRasterizeGlyph(workFont, {128u}, 'A', pixels.data(), pixels.size(),
		128u, &metrics) == FontStatus::RasterLimitExceeded);
	CHECK(std::all_of(pixels.begin(), pixels.end(),
		[](uint8_t value) { return value == 0x4du; }));
	CHECK(FontManagerUnloadFont(workFont) == FontStatus::Ok);
}

void testRepeatAndMutationBounds(const std::vector<uint8_t>& fixture,
	FontHandle baseHandle)
{
	FontGlyphView composite = {};
	CHECK(FontLookupGlyph(baseHandle, {16u}, 0x00e9u, &composite) == FontStatus::Ok);
	const uint16_t glyphId = static_cast<uint16_t>(composite.metrics.glyphId);
	std::mt19937 random(0xF04u);
	std::vector<uint8_t> pixels(128u * 128u, 0x37u);
	UnicodeCodePoint repeatedCodePoint = 0u;
	uint32_t repeatByte = 0u;
	CHECK(findRepeatedFlag(fixture, baseHandle, &repeatedCodePoint, &repeatByte));
	std::vector<uint8_t> badRepeat = fixture;
	badRepeat[repeatByte] = 0xffu;
	FontHandle repeatHandle = {};
	CHECK(load(badRepeat, 0x46544d0000000008ull, &repeatHandle));
	GlyphMetrics repeatMetrics = {};
	CHECK(FontRasterizeGlyph(repeatHandle, {16u}, repeatedCodePoint, pixels.data(),
		pixels.size(), 128u, &repeatMetrics) == FontStatus::InvalidGlyphData);
	CHECK(FontManagerUnloadFont(repeatHandle) == FontStatus::Ok);

	FontGlyphView simple = {};
	CHECK(FontLookupGlyph(baseHandle, {16u}, 'A', &simple) == FontStatus::Ok);
	std::vector<uint8_t> badPointCount = fixture;
	const uint32_t simpleStart = glyphLocation(badPointCount,
		static_cast<uint16_t>(simple.metrics.glyphId), nullptr);
	put16(badPointCount, simpleStart + 10u, kFontTrueTypeMaxBitmapPixels);
	FontHandle pointHandle = {};
	CHECK(load(badPointCount, 0x46544d0000000009ull, &pointHandle));
	CHECK(FontRasterizeGlyph(pointHandle, {16u}, 'A', pixels.data(), pixels.size(),
		128u, &repeatMetrics) == FontStatus::InvalidGlyphData);
	CHECK(FontManagerUnloadFont(pointHandle) == FontStatus::Ok);

	for (uint32_t iteration = 0; iteration < 96u; ++iteration) {
		std::vector<uint8_t> mutation = fixture;
		const uint32_t start = glyphLocation(mutation, glyphId, nullptr);
		const Table glyf = table(mutation, "glyf");
		uint32_t end = 0u;
		glyphLocation(mutation, glyphId, &end);
		const uint32_t recordLength = end - start;
		CHECK(recordLength >= 12u);
		const uint32_t byte = start + 10u + (random() % (recordLength - 10u));
		mutation[byte] ^= static_cast<uint8_t>(1u << (random() % 8u));
		CHECK(glyf.offset <= start && end <= glyf.offset + glyf.length);
		FontHandle mutated = {};
		const FontStatus loadStatus = FontTrueTypeLoadMemory(
			{0x46544d1000000000ull + iteration}, mutation.data(), mutation.size(),
			&mutated, nullptr);
		if (loadStatus == FontStatus::Ok) {
			GlyphMetrics result = {};
			const FontStatus renderStatus = FontRasterizeGlyph(mutated, {16u}, 0x00e9u,
				pixels.data(), pixels.size(), 128u, &result);
			CHECK(renderStatus == FontStatus::Ok ||
				renderStatus == FontStatus::InvalidGlyphData ||
				renderStatus == FontStatus::UnsupportedGlyphFeature ||
				renderStatus == FontStatus::RasterLimitExceeded ||
				renderStatus == FontStatus::ArithmeticOverflow);
			CHECK(FontManagerUnloadFont(mutated) == FontStatus::Ok);
		} else {
			CHECK(loadStatus == FontStatus::InvalidFontFile);
		}
	}
}

bool writeVisualGlyphSheet(FontHandle font, const char* path)
{
	if (!path) return false;
	static const UnicodeCodePoint glyphs[] = {'A', 'i', 'W', 'g', 'O', 0x00e9u};
	static const uint32_t sizes[] = {8u, 16u, 32u};
	const uint32_t cellWidth = 48u;
	const uint32_t cellHeight = 48u;
	const uint32_t width = cellWidth * 6u;
	const uint32_t height = cellHeight * 3u;
	std::vector<uint8_t> image(static_cast<size_t>(width) * height, 255u);
	std::vector<uint8_t> coverage(128u * 128u, 0u);
	for (uint32_t row = 0; row < 3u; ++row) {
		for (uint32_t column = 0; column < 6u; ++column) {
			GlyphMetrics metrics = {};
			const FontStatus status = FontRasterizeGlyph(font, {sizes[row]}, glyphs[column],
				coverage.data(), coverage.size(), 128u, &metrics);
			if (status != FontStatus::Ok) return false;
			const int32_t originX = static_cast<int32_t>(column * cellWidth + 8u) +
				metrics.xBearingPx;
			const int32_t baselineY = static_cast<int32_t>(row * cellHeight + 37u);
			const int32_t originY = baselineY - metrics.yBearingPx;
			for (uint32_t y = 0; y < metrics.heightPx; ++y) {
				for (uint32_t x = 0; x < metrics.widthPx; ++x) {
					const int32_t dstX = originX + static_cast<int32_t>(x);
					const int32_t dstY = originY + static_cast<int32_t>(y);
					if (dstX < 0 || dstY < 0 || dstX >= static_cast<int32_t>(width) ||
						dstY >= static_cast<int32_t>(height)) continue;
					image[static_cast<size_t>(dstY) * width + static_cast<uint32_t>(dstX)] =
						static_cast<uint8_t>(255u - coverage[static_cast<size_t>(y) * 128u + x]);
				}
			}
		}
	}
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output) return false;
	output << "P5\n" << width << " " << height << "\n255\n";
	output.write(reinterpret_cast<const char*>(image.data()),
		static_cast<std::streamsize>(image.size()));
	return output.good();
}

} // namespace

int main(int argc, char** argv)
{
	const std::vector<uint8_t> fixture = readFixture();
	CHECK(!fixture.empty());
	CHECK(FontManagerInitialize() == FontStatus::Ok);
	FontHandle defaultBefore = {};
	CHECK(FontManagerGetDefaultFont(&defaultBefore) == FontStatus::Ok);
	FontTrueTypeMetadata metadata = {};
	FontHandle font = {};
	CHECK(FontTrueTypeLoadMemory({0x46544d0000000000ull}, fixture.data(), fixture.size(),
		&font, &metadata) == FontStatus::Ok);
	FontHandle defaultAfter = {};
	CHECK(FontManagerGetDefaultFont(&defaultAfter) == FontStatus::Ok);
	CHECK(defaultBefore.slot == defaultAfter.slot && defaultBefore.generation == defaultAfter.generation);
	CHECK(FontManagerSetDefaultFont(font) == FontStatus::FontNotSelectable);
	FontMetrics faceMetrics = {};
	CHECK(FontGetMetrics(font, {24u}, &faceMetrics) == FontStatus::Ok);
	CHECK(faceMetrics.pixelSize == 24u && faceMetrics.lineHeightPx > 0u);
	testAsciiAndMetrics(font, metadata);
	testIndependentGoldenMetrics(font);
	testCurvesCompositesAndDeterminism(fixture, font);
	testCompositeTransforms(fixture, font);
	testFormat12Mapping(fixture, font);
	testBufferBounds(font);
	testMalformedGlyphs(fixture, font);
	testRasterWorkLimit(fixture, font);
	testRepeatAndMutationBounds(fixture, font);
	if (argc == 3 && std::strcmp(argv[1], "--visual") == 0) {
		if (!writeVisualGlyphSheet(font, argv[2])) {
			std::fprintf(stderr, "Unable to write visual glyph diagnostic.\n");
			++g_failures;
		} else {
			std::printf("Wrote visual glyph diagnostic: %s\n", argv[2]);
		}
	}
	CHECK(FontManagerUnloadFont(font) == FontStatus::Ok);
	FontManagerShutdown();
	std::printf("FONT4 TrueType rasterization: %d checks, %d failures\n", g_checks, g_failures);
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
