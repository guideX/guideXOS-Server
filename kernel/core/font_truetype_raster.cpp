#include "include/kernel/font_contract.h"
#include "include/kernel/font_truetype.h"
#include "font_truetype_internal.h"

namespace gxos {
namespace gui {
namespace {

static const uint32_t kHeadTag = 0x68656164u;
static const uint32_t kMaxpTag = 0x6d617870u;
static const uint32_t kCmapTag = 0x636d6170u;
static const uint32_t kHheaTag = 0x68686561u;
static const uint32_t kHmtxTag = 0x686d7478u;
static const uint32_t kLocaTag = 0x6c6f6361u;
static const uint32_t kGlyfTag = 0x676c7966u;
static const int32_t kFixedOne = 65536;
static const int32_t kF2Dot14One = 16384;
static const int32_t kMaxCoordinate = 1000000;
static const int64_t kMaxCoordinateFixed = static_cast<int64_t>(kMaxCoordinate) * kFixedOne;
static const uint32_t kMaxOutlinePoints = 1024u;
static const uint32_t kMaxOutlineContours = 128u;
static const uint32_t kMaxFlattenedEdges = 8192u;
static const uint32_t kMaxCompositeComponents = 256u;
static const uint32_t kMaxCompositeDepth = 8u;
static const uint32_t kCurveSteps = 8u;
static const uint32_t kVerticalSamples = 8u;
static const uint64_t kMaxScanWork = 32000000ull;

struct TableRecord {
	uint32_t offset;
	uint32_t length;
};

struct FontTables {
	TableRecord head;
	TableRecord maxp;
	TableRecord cmap;
	TableRecord hhea;
	TableRecord hmtx;
	TableRecord loca;
	TableRecord glyf;
};

struct OutlinePoint {
	int64_t x;
	int64_t y;
	uint8_t onCurve;
	uint8_t reserved[3];
};

struct RasterEdge {
	int32_t x0;
	int32_t y0;
	int32_t x1;
	int32_t y1;
};

struct ScanIntersection {
	int32_t x;
	int8_t winding;
	uint8_t reserved[3];
};

struct RasterScratch {
	OutlinePoint points[kMaxOutlinePoints];
	uint8_t flags[kMaxOutlinePoints];
	uint16_t contourEnds[kMaxOutlineContours];
	RasterEdge edges[kMaxFlattenedEdges];
	ScanIntersection intersections[kMaxFlattenedEdges];
	// FONT5 shares this single staging area for a cache miss. It is in the same
	// serialized scratch owner as the outline decoder and rasterizer.
	uint8_t coverage[kFontTrueTypeMaxBitmapPixels];
	uint32_t pointCount;
	uint32_t contourCount;
	uint32_t edgeCount;
};

// Manager callbacks are serialized by the FONT2 manager lock. One fixed
// scratch block bounds memory and also prevents parallel operations from
// sharing decoder/raster work state.
RasterScratch s_scratch = {};

struct GlyphDecodeState {
	const FontTrueTypeSourceView* font;
	const FontTables* tables;
	RasterScratch* scratch;
	uint32_t pixelSize;
	uint32_t componentCount;
	uint16_t recursionStack[kMaxCompositeDepth];
};

struct GlyphLocation {
	uint32_t offset;
	uint32_t length;
};

struct HorizontalMetric {
	uint16_t advance;
	int16_t sideBearing;
};

struct Segment {
	OutlinePoint start;
	OutlinePoint control;
	OutlinePoint end;
	uint8_t quadratic;
};

typedef void (*SegmentCallback)(const Segment&, void*);

bool rangeFits(size_t total, uint32_t offset, uint32_t length)
{
	return static_cast<size_t>(offset) <= total &&
		static_cast<size_t>(length) <= total - static_cast<size_t>(offset);
}

bool addSize(size_t left, size_t right, size_t* out)
{
	if (!out || right > static_cast<size_t>(-1) - left) return false;
	*out = left + right;
	return true;
}

bool multiplySize(size_t left, size_t right, size_t* out)
{
	if (!out || (left != 0u && right > static_cast<size_t>(-1) / left)) return false;
	*out = left * right;
	return true;
}

uint16_t readU16(const uint8_t* bytes, size_t offset)
{
	return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8) |
		static_cast<uint16_t>(bytes[offset + 1u]));
}

uint32_t readU32(const uint8_t* bytes, size_t offset)
{
	return (static_cast<uint32_t>(bytes[offset]) << 24) |
		(static_cast<uint32_t>(bytes[offset + 1u]) << 16) |
		(static_cast<uint32_t>(bytes[offset + 2u]) << 8) |
		static_cast<uint32_t>(bytes[offset + 3u]);
}

int16_t readS16(const uint8_t* bytes, size_t offset)
{
	const uint16_t raw = readU16(bytes, offset);
	const int32_t value = (raw & 0x8000u) ? static_cast<int32_t>(raw) - 65536 : raw;
	return static_cast<int16_t>(value);
}

int8_t readS8(uint8_t value)
{
	const int16_t signedValue = (value & 0x80u) ?
		static_cast<int16_t>(value) - 256 : static_cast<int16_t>(value);
	return static_cast<int8_t>(signedValue);
}

FontStatus locateTables(const FontTrueTypeSourceView* font, FontTables* out)
{
	if (!font || !font->bytes || !font->metadata || !out ||
		font->byteLength < 12u || readU32(font->bytes, 0u) != 0x00010000u)
		return FontStatus::InvalidFontFile;
	const uint16_t count = readU16(font->bytes, 4u);
	if (count == 0u || count > kFontTrueTypeMaxTables ||
		static_cast<size_t>(count) > (font->byteLength - 12u) / 16u)
		return FontStatus::InvalidFontFile;
	*out = {};
	uint8_t seen = 0;
	for (uint16_t i = 0; i < count; ++i) {
		const size_t entry = 12u + static_cast<size_t>(i) * 16u;
		const uint32_t tag = readU32(font->bytes, entry);
		const uint32_t offset = readU32(font->bytes, entry + 8u);
		const uint32_t length = readU32(font->bytes, entry + 12u);
		if ((offset & 3u) != 0u || !rangeFits(font->byteLength, offset, length))
			return FontStatus::InvalidFontFile;
		TableRecord* destination = nullptr;
		uint8_t bit = 0;
		if (tag == kHeadTag) { destination = &out->head; bit = 1u << 0; }
		else if (tag == kMaxpTag) { destination = &out->maxp; bit = 1u << 1; }
		else if (tag == kCmapTag) { destination = &out->cmap; bit = 1u << 2; }
		else if (tag == kHheaTag) { destination = &out->hhea; bit = 1u << 3; }
		else if (tag == kHmtxTag) { destination = &out->hmtx; bit = 1u << 4; }
		else if (tag == kLocaTag) { destination = &out->loca; bit = 1u << 5; }
		else if (tag == kGlyfTag) { destination = &out->glyf; bit = 1u << 6; }
		if (!destination) continue;
		if ((seen & bit) != 0u) return FontStatus::InvalidFontFile;
		seen |= bit;
		destination->offset = offset;
		destination->length = length;
	}
	if (seen != 0x7fu || out->head.length < 54u || out->maxp.length < 6u ||
		out->cmap.length < 4u || out->hhea.length < 36u ||
		out->loca.length == 0u || out->glyf.length == 0u || out->hmtx.length == 0u)
		return FontStatus::InvalidFontFile;
	if (readU16(font->bytes, out->maxp.offset + 4u) != font->metadata->glyphCount ||
		readU16(font->bytes, out->head.offset + 18u) != font->metadata->unitsPerEm)
		return FontStatus::InvalidFontFile;
	return FontStatus::Ok;
}

int64_t floorDiv(int64_t value, int64_t divisor)
{
	if (value >= 0) return value / divisor;
	return -((-value + divisor - 1) / divisor);
}

int64_t ceilDiv(int64_t value, int64_t divisor)
{
	if (value >= 0) return (value + divisor - 1) / divisor;
	return -((-value) / divisor);
}

int64_t roundDivSigned(int64_t value, int64_t divisor)
{
	if (value >= 0) return (value + divisor / 2) / divisor;
	return -((-value + divisor / 2) / divisor);
}

bool scaleSigned(int32_t value, uint32_t pixelSize, uint32_t unitsPerEm,
	int32_t* out)
{
	if (!out || unitsPerEm == 0u) return false;
	const int64_t scaled = roundDivSigned(static_cast<int64_t>(value) * pixelSize,
		unitsPerEm);
	if (scaled < -2147483647ll - 1ll || scaled > 2147483647ll) return false;
	*out = static_cast<int32_t>(scaled);
	return true;
}

bool scaleUnsigned(uint32_t value, uint32_t pixelSize, uint32_t unitsPerEm,
	uint32_t* out)
{
	if (!out || unitsPerEm == 0u) return false;
	const uint64_t numerator = static_cast<uint64_t>(value) * pixelSize + unitsPerEm / 2u;
	const uint64_t scaled = numerator / unitsPerEm;
	if (scaled > 0xffffffffu) return false;
	*out = static_cast<uint32_t>(scaled);
	return true;
}

bool glyphLocation(const FontTrueTypeSourceView* font, const FontTables& tables,
	uint16_t glyphId, GlyphLocation* out)
{
	if (!font || !out || glyphId >= font->metadata->glyphCount || tables.head.length < 54u)
		return false;
	const int16_t format = readS16(font->bytes, tables.head.offset + 50u);
	if (format != 0 && format != 1) return false;
	const size_t entryBytes = format == 0 ? 2u : 4u;
	const size_t index0 = static_cast<size_t>(glyphId) * entryBytes;
	const size_t index1 = index0 + entryBytes;
	if (index1 > tables.loca.length || entryBytes > tables.loca.length - index1)
		return false;
	const uint32_t start = format == 0 ?
		static_cast<uint32_t>(readU16(font->bytes, tables.loca.offset + index0)) * 2u :
		readU32(font->bytes, tables.loca.offset + index0);
	const uint32_t end = format == 0 ?
		static_cast<uint32_t>(readU16(font->bytes, tables.loca.offset + index1)) * 2u :
		readU32(font->bytes, tables.loca.offset + index1);
	if (end < start || end > tables.glyf.length ||
		!rangeFits(font->byteLength, tables.glyf.offset + start, end - start)) return false;
	out->offset = tables.glyf.offset + start;
	out->length = end - start;
	return true;
}

bool horizontalMetric(const FontTrueTypeSourceView* font, const FontTables& tables,
	uint16_t glyphId, HorizontalMetric* out)
{
	if (!font || !out || glyphId >= font->metadata->glyphCount) return false;
	const uint16_t numberOfMetrics = font->metadata->numberOfHorizontalMetrics;
	if (numberOfMetrics == 0u || numberOfMetrics > font->metadata->glyphCount) return false;
	size_t advanceOffset = 0;
	size_t bearingOffset = 0;
	if (glyphId < numberOfMetrics) {
		advanceOffset = static_cast<size_t>(glyphId) * 4u;
		bearingOffset = advanceOffset + 2u;
	} else {
		advanceOffset = static_cast<size_t>(numberOfMetrics - 1u) * 4u;
		bearingOffset = static_cast<size_t>(numberOfMetrics) * 4u +
			static_cast<size_t>(glyphId - numberOfMetrics) * 2u;
	}
	const size_t advanceBytes = glyphId < numberOfMetrics ? 4u : 2u;
	if (advanceOffset > tables.hmtx.length || advanceBytes > tables.hmtx.length - advanceOffset ||
		bearingOffset > tables.hmtx.length || 2u > tables.hmtx.length - bearingOffset)
		return false;
	const uint8_t* bytes = font->bytes + tables.hmtx.offset;
	out->advance = readU16(bytes, advanceOffset);
	out->sideBearing = readS16(bytes, bearingOffset);
	return true;
}

bool lookupFormat12(const uint8_t* subtable, size_t available, uint32_t codePoint,
	uint16_t glyphCount, uint16_t* outGlyph, bool* found)
{
	if (!subtable || !outGlyph || !found || available < 16u || readU16(subtable, 0u) != 12u)
		return false;
	const uint32_t length = readU32(subtable, 4u);
	const uint32_t groupCount = readU32(subtable, 12u);
	if (length < 16u || length > available || groupCount > kFontTrueTypeMaxCmapGroups ||
		static_cast<size_t>(groupCount) > (length - 16u) / 12u) return false;
	uint32_t low = 0u;
	uint32_t high = groupCount;
	while (low < high) {
		const uint32_t mid = low + (high - low) / 2u;
		const size_t group = 16u + static_cast<size_t>(mid) * 12u;
		const uint32_t start = readU32(subtable, group);
		const uint32_t end = readU32(subtable, group + 4u);
		if (codePoint < start) high = mid;
		else if (codePoint > end) low = mid + 1u;
		else {
			const uint32_t firstGlyph = readU32(subtable, group + 8u);
			const uint64_t glyph = static_cast<uint64_t>(firstGlyph) + codePoint - start;
			if (glyph >= glyphCount) return false;
			*outGlyph = static_cast<uint16_t>(glyph);
			*found = glyph != 0u;
			return true;
		}
	}
	*found = false;
	return true;
}

bool lookupFormat4(const uint8_t* subtable, size_t available, uint32_t codePoint,
	uint16_t glyphCount, uint16_t* outGlyph, bool* found)
{
	if (!subtable || !outGlyph || !found || codePoint > 0xffffu || available < 16u ||
		readU16(subtable, 0u) != 4u) return false;
	const uint16_t length = readU16(subtable, 2u);
	const uint16_t segCountX2 = readU16(subtable, 6u);
	if (length < 16u || length > available || segCountX2 == 0u || (segCountX2 & 1u))
		return false;
	const uint32_t segmentCount = segCountX2 / 2u;
	if (segmentCount > 8192u || static_cast<size_t>(segmentCount) > (length - 16u) / 8u)
		return false;
	const size_t endCodes = 14u;
	const size_t startCodes = 16u + static_cast<size_t>(segmentCount) * 2u;
	const size_t deltas = startCodes + static_cast<size_t>(segmentCount) * 2u;
	const size_t ranges = deltas + static_cast<size_t>(segmentCount) * 2u;
	for (uint32_t i = 0; i < segmentCount; ++i) {
		const uint16_t endCode = readU16(subtable, endCodes + static_cast<size_t>(i) * 2u);
		if (codePoint > endCode) continue;
		const size_t startOffset = startCodes + static_cast<size_t>(i) * 2u;
		const uint16_t startCode = readU16(subtable, startOffset);
		if (codePoint < startCode) return true;
		const int16_t delta = readS16(subtable, deltas + static_cast<size_t>(i) * 2u);
		const uint16_t rangeOffset = readU16(subtable, ranges + static_cast<size_t>(i) * 2u);
		uint16_t glyph = 0u;
		if (rangeOffset == 0u) {
			glyph = static_cast<uint16_t>((codePoint + delta) & 0xffffu);
		} else {
			if ((rangeOffset & 1u) != 0u) return false;
			const size_t rangeWord = ranges + static_cast<size_t>(i) * 2u;
			const size_t target = rangeWord + rangeOffset +
				(static_cast<size_t>(codePoint) - startCode) * 2u;
			if (target > length || 2u > length - target) return false;
			glyph = readU16(subtable, target);
			if (glyph != 0u) glyph = static_cast<uint16_t>((glyph + delta) & 0xffffu);
		}
		if (glyph >= glyphCount) return false;
		*outGlyph = glyph;
		*found = glyph != 0u;
		return true;
	}
	*found = false;
	return true;
}

FontStatus mapCodePoint(const FontTrueTypeSourceView* font, const FontTables& tables,
	UnicodeCodePoint codePoint, uint16_t* outGlyph)
{
	if (!font || !outGlyph) return FontStatus::InvalidArgument;
	const uint8_t* cmap = font->bytes + tables.cmap.offset;
	if (tables.cmap.length < 4u || readU16(cmap, 0u) != 0u) return FontStatus::InvalidFontFile;
	const uint16_t recordCount = readU16(cmap, 2u);
	if (recordCount == 0u || recordCount > kFontTrueTypeMaxCmapRecords ||
		static_cast<size_t>(recordCount) > (tables.cmap.length - 4u) / 8u)
		return FontStatus::InvalidFontFile;
	for (uint16_t pass = 0; pass < 2u; ++pass) {
		for (uint16_t i = 0; i < recordCount; ++i) {
			const size_t record = 4u + static_cast<size_t>(i) * 8u;
			const uint16_t platform = readU16(cmap, record);
			const uint16_t encoding = readU16(cmap, record + 2u);
			const uint32_t offset = readU32(cmap, record + 4u);
			const bool unicode = platform == 0u ||
				(platform == 3u && (encoding == 1u || encoding == 10u));
			if (!unicode || offset > tables.cmap.length || tables.cmap.length - offset < 2u)
				continue;
			const uint8_t* subtable = cmap + offset;
			const uint16_t format = readU16(subtable, 0u);
			if ((pass == 0u && format != 12u) || (pass == 1u && format != 4u)) continue;
			if (format == 4u && codePoint > 0xffffu) continue;
			uint16_t glyph = 0u;
			bool found = false;
			const bool valid = format == 12u ?
				lookupFormat12(subtable, tables.cmap.length - offset, codePoint,
					font->metadata->glyphCount, &glyph, &found) :
				lookupFormat4(subtable, tables.cmap.length - offset, codePoint,
					font->metadata->glyphCount, &glyph, &found);
			if (!valid) return FontStatus::InvalidFontFile;
			if (found) {
				*outGlyph = glyph;
				return FontStatus::Ok;
			}
		}
	}
	return FontStatus::UnsupportedCodePoint;
}

FontStatus decodeGlyphMetrics(const FontTrueTypeSourceView* font,
	const FontTables& tables, uint16_t glyphId, UnicodeCodePoint codePoint,
	FontSize size, GlyphMetrics* outMetrics);

FontStatus backendMetrics(const void* context, FontSize size, FontMetrics* out)
{
	if (!out || !context || size.pixelHeight == 0u) return FontStatus::InvalidArgument;
	*out = {};
	if (size.pixelHeight > kFontTrueTypeMaxPixelSize) return FontStatus::UnsupportedSize;
	const FontTrueTypeSourceView* font = static_cast<const FontTrueTypeSourceView*>(context);
	FontTables tables = {};
	const FontStatus tablesStatus = locateTables(font, &tables);
	if (tablesStatus != FontStatus::Ok) return tablesStatus;
	const uint8_t* hhea = font->bytes + tables.hhea.offset;
	const int16_t ascender = readS16(hhea, 4u);
	const int16_t descender = readS16(hhea, 6u);
	const int16_t lineGap = readS16(hhea, 8u);
	const int32_t positiveAscent = ascender > 0 ? ascender : 0;
	const int32_t positiveDescent = descender < 0 ? -static_cast<int32_t>(descender) : 0;
	const int32_t positiveGap = lineGap > 0 ? lineGap : 0;
	uint32_t ascent = 0, descent = 0, gap = 0;
	if (!scaleUnsigned(static_cast<uint32_t>(positiveAscent), size.pixelHeight,
		font->metadata->unitsPerEm, &ascent) ||
		!scaleUnsigned(static_cast<uint32_t>(positiveDescent), size.pixelHeight,
		font->metadata->unitsPerEm, &descent) ||
		!scaleUnsigned(static_cast<uint32_t>(positiveGap), size.pixelHeight,
		font->metadata->unitsPerEm, &gap) ||
		ascent > 0xffffffffu - descent || ascent + descent > 0xffffffffu - gap)
		return FontStatus::ArithmeticOverflow;
	out->pixelSize = size.pixelHeight;
	out->ascentPx = ascent;
	out->descentPx = descent;
	out->lineGapPx = gap;
	out->lineHeightPx = ascent + descent + gap;
	if (out->lineHeightPx == 0u) out->lineHeightPx = 1u;
	out->baselinePx = ascent;
	return FontStatus::Ok;
}

FontStatus backendLookup(const void* context, FontSize size,
	UnicodeCodePoint codePoint, FontGlyphView* out)
{
	if (!context || !out) return FontStatus::InvalidArgument;
	const FontTrueTypeSourceView* font = static_cast<const FontTrueTypeSourceView*>(context);
	FontTables tables = {};
	FontStatus status = locateTables(font, &tables);
	if (status != FontStatus::Ok) return status;
	uint16_t glyph = 0;
	status = mapCodePoint(font, tables, codePoint, &glyph);
	if (status != FontStatus::Ok) return status;
	GlyphMetrics metrics = {};
	status = decodeGlyphMetrics(font, tables, glyph, codePoint, size, &metrics);
	if (status != FontStatus::Ok) return status;
	out->metrics = metrics;
	out->bitmap = nullptr;
	out->bitmapByteLength = 0u;
	out->bitmapFormat = FontBitmapFormat::None;
	out->fallbackUsed = 0u;
	out->reserved[0] = out->reserved[1] = 0u;
	out->requestedCodePoint = codePoint;
	return FontStatus::Ok;
}

bool appendPoint(GlyphDecodeState* state, int32_t x, int32_t y, bool onCurve)
{
	if (!state || !state->scratch || state->scratch->pointCount >= kMaxOutlinePoints ||
		x < -kMaxCoordinate || x > kMaxCoordinate ||
		y < -kMaxCoordinate || y > kMaxCoordinate) return false;
	OutlinePoint& point = state->scratch->points[state->scratch->pointCount++];
	point.x = static_cast<int64_t>(x) * kFixedOne;
	point.y = static_cast<int64_t>(y) * kFixedOne;
	point.onCurve = onCurve ? 1u : 0u;
	point.reserved[0] = point.reserved[1] = point.reserved[2] = 0u;
	return true;
}

bool addCoordinate(int32_t current, int32_t delta, int32_t* out)
{
	if (!out) return false;
	const int64_t sum = static_cast<int64_t>(current) + delta;
	if (sum < -kMaxCoordinate || sum > kMaxCoordinate) return false;
	*out = static_cast<int32_t>(sum);
	return true;
}

bool addFixedCoordinate(int64_t current, int64_t delta, int64_t* out)
{
	if (!out) return false;
	if ((delta > 0 && current > kMaxCoordinateFixed - delta) ||
		(delta < 0 && current < -kMaxCoordinateFixed - delta)) return false;
	*out = current + delta;
	return *out >= -kMaxCoordinateFixed && *out <= kMaxCoordinateFixed;
}

bool decodeSimpleGlyph(GlyphDecodeState* state, const uint8_t* glyph,
	uint32_t glyphLength, uint16_t contourCount)
{
	if (!state || !glyph || contourCount > kMaxOutlineContours - state->scratch->contourCount)
		return false;
	size_t position = 10u;
	const uint32_t contourBase = state->scratch->contourCount;
	const uint32_t pointBase = state->scratch->pointCount;
	uint16_t lastEndpoint = 0u;
	for (uint16_t i = 0; i < contourCount; ++i) {
		if (position > glyphLength || 2u > glyphLength - position) return false;
		const uint16_t endpoint = readU16(glyph, position);
		position += 2u;
		if ((i != 0u && endpoint <= lastEndpoint) ||
			static_cast<uint32_t>(endpoint) + 1u > kMaxOutlinePoints - pointBase)
			return false;
		lastEndpoint = endpoint;
		state->scratch->contourEnds[contourBase + i] =
			static_cast<uint16_t>(pointBase + endpoint);
	}
	const uint32_t pointCount = contourCount == 0u ? 0u :
		static_cast<uint32_t>(lastEndpoint) + 1u;
	if (position > glyphLength || 2u > glyphLength - position) return false;
	const uint16_t instructionLength = readU16(glyph, position);
	position += 2u;
	if (instructionLength > glyphLength - position) return false;
	position += instructionLength; // Instructions are intentionally not executed.
	if (pointCount > kMaxOutlinePoints - pointBase) return false;
	for (uint32_t i = 0; i < pointCount; ++i) {
		if (position >= glyphLength) return false;
		const uint8_t flag = glyph[position++];
		state->scratch->flags[pointBase + i] = flag;
		if ((flag & 0x08u) != 0u) {
			if (position >= glyphLength) return false;
			const uint32_t repeat = glyph[position++];
			if (repeat > pointCount - i - 1u) return false;
			for (uint32_t j = 0; j < repeat; ++j)
				state->scratch->flags[pointBase + ++i] = flag;
		}
	}
	int32_t coordinate = 0;
	for (uint32_t i = 0; i < pointCount; ++i) {
		const uint8_t flag = state->scratch->flags[pointBase + i];
		int32_t delta = 0;
		if ((flag & 0x02u) != 0u) {
			if (position >= glyphLength) return false;
			const int32_t magnitude = glyph[position++];
			delta = (flag & 0x10u) ? magnitude : -magnitude;
		} else if ((flag & 0x10u) == 0u) {
			if (position > glyphLength || 2u > glyphLength - position) return false;
			delta = readS16(glyph, position);
			position += 2u;
		}
		if (!addCoordinate(coordinate, delta, &coordinate) ||
			!appendPoint(state, coordinate, 0, (flag & 0x01u) != 0u)) return false;
	}
	coordinate = 0;
	for (uint32_t i = 0; i < pointCount; ++i) {
		const uint8_t flag = state->scratch->flags[pointBase + i];
		int32_t delta = 0;
		if ((flag & 0x04u) != 0u) {
			if (position >= glyphLength) return false;
			const int32_t magnitude = glyph[position++];
			delta = (flag & 0x20u) ? magnitude : -magnitude;
		} else if ((flag & 0x20u) == 0u) {
			if (position > glyphLength || 2u > glyphLength - position) return false;
			delta = readS16(glyph, position);
			position += 2u;
		}
		if (!addCoordinate(coordinate, delta, &coordinate)) return false;
		state->scratch->points[pointBase + i].y =
			static_cast<int64_t>(coordinate) * kFixedOne;
	}
	state->scratch->contourCount += contourCount;
	return true;
}

bool transformCoordinate(int64_t x, int64_t y, int16_t a, int16_t b,
	int16_t c, int16_t d, int64_t* outX, int64_t* outY)
{
	if (!outX || !outY) return false;
	const int64_t tx = static_cast<int64_t>(a) * x + static_cast<int64_t>(c) * y;
	const int64_t ty = static_cast<int64_t>(b) * x + static_cast<int64_t>(d) * y;
	const int64_t resultX = tx / kF2Dot14One;
	const int64_t resultY = ty / kF2Dot14One;
	if (resultX < -kMaxCoordinateFixed || resultX > kMaxCoordinateFixed ||
		resultY < -kMaxCoordinateFixed || resultY > kMaxCoordinateFixed) return false;
	*outX = resultX;
	*outY = resultY;
	return true;
}

bool applyTransformAndOffset(GlyphDecodeState* state, uint32_t firstPoint,
	int16_t a, int16_t b, int16_t c, int16_t d, int64_t dx, int64_t dy)
{
	if (!state || firstPoint > state->scratch->pointCount) return false;
	for (uint32_t i = firstPoint; i < state->scratch->pointCount; ++i) {
		OutlinePoint& point = state->scratch->points[i];
		int64_t x = 0, y = 0;
		if (!transformCoordinate(point.x, point.y, a, b, c, d, &x, &y) ||
			!addFixedCoordinate(x, dx, &x) || !addFixedCoordinate(y, dy, &y)) return false;
		point.x = x;
		point.y = y;
	}
	return true;
}

FontStatus decodeGlyph(GlyphDecodeState* state, uint16_t glyphId,
	uint32_t depth, uint32_t basePoint, uint16_t* outMetricGlyph);

FontStatus decodeCompositeGlyph(GlyphDecodeState* state, uint16_t glyphId,
	uint32_t depth, uint32_t basePoint, const uint8_t* glyph, uint32_t glyphLength,
	uint16_t* outMetricGlyph)
{
	if (!state || !glyph || !outMetricGlyph) return FontStatus::InvalidGlyphData;
	uint32_t position = 10u;
	bool anyInstructions = false;
	bool usedMetrics = false;
	uint16_t metricGlyph = glyphId;
	uint16_t flags = 0u;
	uint32_t localComponents = 0u;
	do {
		if (position > glyphLength || 4u > glyphLength - position)
			return FontStatus::InvalidGlyphData;
		flags = readU16(glyph, position);
		const uint16_t componentGlyph = readU16(glyph, position + 2u);
		position += 4u;
		++localComponents;
		if (++state->componentCount > kMaxCompositeComponents ||
			componentGlyph >= state->font->metadata->glyphCount || localComponents > kMaxCompositeComponents)
			return FontStatus::InvalidGlyphData;
		const uint16_t knownFlags = 0x0001u | 0x0002u | 0x0004u | 0x0008u |
			0x0020u | 0x0040u | 0x0080u | 0x0100u | 0x0200u |
			0x0400u | 0x0800u | 0x1000u;
		if ((flags & ~knownFlags) != 0u) return FontStatus::UnsupportedGlyphFeature;
		if ((flags & 0x0800u) != 0u && (flags & 0x1000u) != 0u)
			return FontStatus::UnsupportedGlyphFeature;
		const uint16_t transformFlags = flags & (0x0008u | 0x0040u | 0x0080u);
		if (transformFlags != 0u && (transformFlags & (transformFlags - 1u)) != 0u)
			return FontStatus::InvalidGlyphData;
		int32_t arg1 = 0, arg2 = 0;
		if ((flags & 0x0001u) != 0u) {
			if (position > glyphLength || 4u > glyphLength - position)
				return FontStatus::InvalidGlyphData;
			arg1 = (flags & 0x0002u) ? readS16(glyph, position) : readU16(glyph, position);
			arg2 = (flags & 0x0002u) ? readS16(glyph, position + 2u) : readU16(glyph, position + 2u);
			position += 4u;
		} else {
			if (position > glyphLength || 2u > glyphLength - position)
				return FontStatus::InvalidGlyphData;
			arg1 = (flags & 0x0002u) ? readS8(glyph[position]) : glyph[position];
			arg2 = (flags & 0x0002u) ? readS8(glyph[position + 1u]) : glyph[position + 1u];
			position += 2u;
		}
		int16_t a = kF2Dot14One, b = 0, c = 0, d = kF2Dot14One;
		if (transformFlags == 0x0008u) {
			if (position > glyphLength || 2u > glyphLength - position)
				return FontStatus::InvalidGlyphData;
			a = d = readS16(glyph, position);
			position += 2u;
		} else if (transformFlags == 0x0040u) {
			if (position > glyphLength || 4u > glyphLength - position)
				return FontStatus::InvalidGlyphData;
			a = readS16(glyph, position);
			d = readS16(glyph, position + 2u);
			position += 4u;
		} else if (transformFlags == 0x0080u) {
			if (position > glyphLength || 8u > glyphLength - position)
				return FontStatus::InvalidGlyphData;
			a = readS16(glyph, position);
			b = readS16(glyph, position + 2u);
			c = readS16(glyph, position + 4u);
			d = readS16(glyph, position + 6u);
			position += 8u;
		}
		const uint32_t childStart = state->scratch->pointCount;
		const uint32_t parentPointCount = childStart - basePoint;
		uint16_t childMetric = componentGlyph;
		FontStatus childStatus = decodeGlyph(state, componentGlyph, depth + 1u,
			childStart, &childMetric);
		if (childStatus != FontStatus::Ok) return childStatus;
		const uint32_t childPointCount = state->scratch->pointCount - childStart;
		int64_t dx = 0, dy = 0;
		if ((flags & 0x0002u) != 0u) {
			const int64_t argumentX = static_cast<int64_t>(arg1) * kFixedOne;
			const int64_t argumentY = static_cast<int64_t>(arg2) * kFixedOne;
			if ((flags & 0x0800u) != 0u) {
				if (!transformCoordinate(argumentX, argumentY, a, b, c, d, &dx, &dy))
					return FontStatus::InvalidGlyphData;
			} else {
				dx = argumentX;
				dy = argumentY;
			}
		} else {
			if (static_cast<uint32_t>(arg1) >= parentPointCount ||
				static_cast<uint32_t>(arg2) >= childPointCount)
				return FontStatus::InvalidGlyphData;
		}
		for (uint32_t i = childStart; i < state->scratch->pointCount; ++i) {
			OutlinePoint& point = state->scratch->points[i];
			int64_t x = 0, y = 0;
			if (!transformCoordinate(point.x, point.y, a, b, c, d, &x, &y))
				return FontStatus::InvalidGlyphData;
			point.x = x;
			point.y = y;
		}
		if ((flags & 0x0002u) == 0u) {
			const OutlinePoint& parent = state->scratch->points[basePoint + static_cast<uint32_t>(arg1)];
			const OutlinePoint& child = state->scratch->points[childStart + static_cast<uint32_t>(arg2)];
			const int64_t offsetX = parent.x - child.x;
			const int64_t offsetY = parent.y - child.y;
			if (offsetX < -kMaxCoordinateFixed || offsetX > kMaxCoordinateFixed ||
				offsetY < -kMaxCoordinateFixed || offsetY > kMaxCoordinateFixed)
				return FontStatus::InvalidGlyphData;
			dx = offsetX;
			dy = offsetY;
		}
		if ((flags & 0x0004u) != 0u && (flags & 0x0002u) != 0u) {
			const uint32_t upem = state->font->metadata->unitsPerEm;
			const int64_t pixelX = dx * state->pixelSize / upem;
			const int64_t pixelY = dy * state->pixelSize / upem;
			const int64_t roundedX = roundDivSigned(pixelX, kFixedOne) * kFixedOne;
			const int64_t roundedY = roundDivSigned(pixelY, kFixedOne) * kFixedOne;
			dx = roundDivSigned(roundedX * upem, state->pixelSize);
			dy = roundDivSigned(roundedY * upem, state->pixelSize);
		}
		if (!applyTransformAndOffset(state, childStart, kF2Dot14One, 0, 0,
			kF2Dot14One, dx, dy)) return FontStatus::InvalidGlyphData;
		if ((flags & 0x0200u) != 0u) {
			if (usedMetrics) return FontStatus::InvalidGlyphData;
			usedMetrics = true;
			metricGlyph = childMetric;
		}
		if ((flags & 0x0100u) != 0u) anyInstructions = true;
		if ((flags & 0x0020u) != 0u && (flags & 0x0100u) != 0u)
			return FontStatus::InvalidGlyphData;
	} while ((flags & 0x0020u) != 0u);
	if (anyInstructions) {
		if (position > glyphLength || 2u > glyphLength - position)
			return FontStatus::InvalidGlyphData;
		const uint16_t instructionLength = readU16(glyph, position);
		position += 2u;
		if (instructionLength > glyphLength - position) return FontStatus::InvalidGlyphData;
	}
	*outMetricGlyph = metricGlyph;
	return FontStatus::Ok;
}

FontStatus decodeGlyph(GlyphDecodeState* state, uint16_t glyphId,
	uint32_t depth, uint32_t basePoint, uint16_t* outMetricGlyph)
{
	if (!state || !outMetricGlyph || glyphId >= state->font->metadata->glyphCount ||
		depth >= kMaxCompositeDepth || basePoint > state->scratch->pointCount)
		return FontStatus::InvalidGlyphData;
	for (uint32_t i = 0; i < depth; ++i)
		if (state->recursionStack[i] == glyphId) return FontStatus::InvalidGlyphData;
	state->recursionStack[depth] = glyphId;
	GlyphLocation location = {};
	if (!glyphLocation(state->font, *state->tables, glyphId, &location))
		return FontStatus::InvalidGlyphData;
	if (location.length == 0u) {
		*outMetricGlyph = glyphId;
		return FontStatus::Ok;
	}
	if (location.length < 10u) return FontStatus::InvalidGlyphData;
	const uint8_t* glyph = state->font->bytes + location.offset;
	const int16_t contourCount = readS16(glyph, 0u);
	const int16_t xMin = readS16(glyph, 2u);
	const int16_t yMin = readS16(glyph, 4u);
	const int16_t xMax = readS16(glyph, 6u);
	const int16_t yMax = readS16(glyph, 8u);
	if (xMin > xMax || yMin > yMax) return FontStatus::InvalidGlyphData;
	if (contourCount >= 0) {
		if (contourCount > static_cast<int16_t>(kMaxOutlineContours))
			return FontStatus::RasterLimitExceeded;
		if (!decodeSimpleGlyph(state, glyph, location.length,
			static_cast<uint16_t>(contourCount))) return FontStatus::InvalidGlyphData;
		*outMetricGlyph = glyphId;
		return FontStatus::Ok;
	}
	if (contourCount != -1) return FontStatus::InvalidGlyphData;
	return decodeCompositeGlyph(state, glyphId, depth, basePoint, glyph,
		location.length, outMetricGlyph);
}

OutlinePoint midpoint(const OutlinePoint& a, const OutlinePoint& b)
{
	OutlinePoint result = {};
	result.x = (a.x + b.x) / 2;
	result.y = (a.y + b.y) / 2;
	result.onCurve = 1u;
	return result;
}

void emitLine(const OutlinePoint& from, const OutlinePoint& to,
	SegmentCallback callback, void* context)
{
	if (!callback || (from.x == to.x && from.y == to.y)) return;
	Segment segment = {};
	segment.start = from;
	segment.end = to;
	callback(segment, context);
}

bool walkContours(const RasterScratch* scratch, SegmentCallback callback, void* context)
{
	if (!scratch || !callback) return false;
	uint32_t first = 0u;
	for (uint32_t contour = 0; contour < scratch->contourCount; ++contour) {
		const uint32_t last = scratch->contourEnds[contour];
		if (last < first || last >= scratch->pointCount) return false;
		const uint32_t count = last - first + 1u;
		const OutlinePoint& firstPoint = scratch->points[first];
		const OutlinePoint& lastPoint = scratch->points[last];
		OutlinePoint start = {};
		uint32_t index = 0u;
		uint32_t consumed = 0u;
		if (firstPoint.onCurve) {
			start = firstPoint;
			index = 1u;
			consumed = 1u;
		} else if (lastPoint.onCurve) {
			start = lastPoint;
			index = 0u;
			consumed = 1u;
		} else {
			start = midpoint(lastPoint, firstPoint);
		}
		OutlinePoint current = start;
		while (consumed < count) {
			const uint32_t currentIndex = index % count;
			const OutlinePoint& point = scratch->points[first + currentIndex];
			if (point.onCurve) {
				emitLine(current, point, callback, context);
				current = point;
				++index;
				++consumed;
				continue;
			}
			const uint32_t nextIndex = (currentIndex + 1u) % count;
			const OutlinePoint& next = scratch->points[first + nextIndex];
			OutlinePoint endpoint = {};
			if (next.onCurve) {
				endpoint = next;
				Segment segment = {};
				segment.start = current;
				segment.control = point;
				segment.end = endpoint;
				segment.quadratic = 1u;
				callback(segment, context);
				current = endpoint;
				index += 2u;
				consumed += 2u;
			} else {
				endpoint = midpoint(point, next);
				Segment segment = {};
				segment.start = current;
				segment.control = point;
				segment.end = endpoint;
				segment.quadratic = 1u;
				callback(segment, context);
				current = endpoint;
				++index;
				++consumed;
			}
		}
		emitLine(current, start, callback, context);
		first = last + 1u;
	}
	return first == scratch->pointCount;
}

struct OutlineBounds {
	int64_t minX;
	int64_t minY;
	int64_t maxX;
	int64_t maxY;
	uint8_t hasPoint;
};

void includeBounds(OutlineBounds* bounds, int64_t x, int64_t y)
{
	if (!bounds) return;
	if (!bounds->hasPoint) {
		bounds->minX = bounds->maxX = x;
		bounds->minY = bounds->maxY = y;
		bounds->hasPoint = 1u;
		return;
	}
	if (x < bounds->minX) bounds->minX = x;
	if (x > bounds->maxX) bounds->maxX = x;
	if (y < bounds->minY) bounds->minY = y;
	if (y > bounds->maxY) bounds->maxY = y;
}

bool quadraticExtremum(int64_t p0, int64_t p1, int64_t p2, int64_t* outFixed)
{
	if (!outFixed) return false;
	const int64_t denominator = static_cast<int64_t>(p0) - 2ll * p1 + p2;
	const int64_t numerator = static_cast<int64_t>(p0) - p1;
	if (denominator == 0) return false;
	const bool inside = denominator > 0 ?
		(numerator > 0 && numerator < denominator) :
		(numerator < 0 && numerator > denominator);
	if (!inside) return false;
	const int64_t tFixed = numerator * kFixedOne / denominator;
	*outFixed = p0 - numerator * tFixed / kFixedOne;
	return true;
}

void collectBounds(const Segment& segment, void* context)
{
	OutlineBounds* bounds = static_cast<OutlineBounds*>(context);
	includeBounds(bounds, segment.start.x, segment.start.y);
	includeBounds(bounds, segment.end.x, segment.end.y);
	if (segment.quadratic) {
		int64_t extremum = 0;
		if (quadraticExtremum(segment.start.x, segment.control.x, segment.end.x, &extremum))
			includeBounds(bounds, extremum, segment.start.y);
		if (quadraticExtremum(segment.start.y, segment.control.y, segment.end.y, &extremum))
			includeBounds(bounds, segment.start.x, extremum);
	}
}

FontStatus decodeGlyphMetrics(const FontTrueTypeSourceView* font,
	const FontTables& tables, uint16_t glyphId, UnicodeCodePoint codePoint,
	FontSize size, GlyphMetrics* outMetrics)
{
	if (!font || !outMetrics || size.pixelHeight == 0u) return FontStatus::InvalidArgument;
	*outMetrics = {};
	if (size.pixelHeight > kFontTrueTypeMaxPixelSize) return FontStatus::UnsupportedSize;
	s_scratch.pointCount = 0u;
	s_scratch.contourCount = 0u;
	s_scratch.edgeCount = 0u;
	GlyphDecodeState state = {};
	state.font = font;
	state.tables = &tables;
	state.scratch = &s_scratch;
	state.pixelSize = size.pixelHeight;
	uint16_t metricGlyph = glyphId;
	const FontStatus decodeStatus = decodeGlyph(&state, glyphId, 0u, 0u, &metricGlyph);
	if (decodeStatus != FontStatus::Ok) return decodeStatus;
	HorizontalMetric horizontal = {};
	if (!horizontalMetric(font, tables, metricGlyph, &horizontal))
		return FontStatus::InvalidGlyphData;
	GlyphMetrics metrics = {};
	metrics.glyphId = glyphId;
	metrics.codePoint = codePoint;
	if (!scaleUnsigned(horizontal.advance, size.pixelHeight,
		font->metadata->unitsPerEm, &metrics.advancePx) ||
		!scaleSigned(horizontal.sideBearing, size.pixelHeight,
		font->metadata->unitsPerEm, &metrics.sideBearingPx))
		return FontStatus::ArithmeticOverflow;
	OutlineBounds bounds = {};
	if (!walkContours(&s_scratch, &collectBounds, &bounds))
		return FontStatus::InvalidGlyphData;
	if (bounds.hasPoint) {
		const int64_t minX = bounds.minX * size.pixelHeight / font->metadata->unitsPerEm;
		const int64_t maxX = bounds.maxX * size.pixelHeight / font->metadata->unitsPerEm;
		const int64_t minY = bounds.minY * size.pixelHeight / font->metadata->unitsPerEm;
		const int64_t maxY = bounds.maxY * size.pixelHeight / font->metadata->unitsPerEm;
		const int64_t originX = floorDiv(minX, kFixedOne);
		const int64_t originY = floorDiv(minY, kFixedOne);
		const int64_t right = ceilDiv(maxX, kFixedOne);
		const int64_t top = ceilDiv(maxY, kFixedOne);
		const int64_t width = right - originX;
		const int64_t height = top - originY;
		if (originX < -2147483647ll - 1ll || originX > 2147483647ll ||
			top < -2147483647ll - 1ll || top > 2147483647ll ||
			width < 0 || height < 0 || width > kFontTrueTypeMaxBitmapDimension ||
			height > kFontTrueTypeMaxBitmapDimension ||
			width * height > kFontTrueTypeMaxBitmapPixels)
			return FontStatus::RasterLimitExceeded;
		metrics.xBearingPx = static_cast<int32_t>(originX);
		metrics.yBearingPx = static_cast<int32_t>(top);
		metrics.widthPx = static_cast<uint32_t>(width);
		metrics.heightPx = static_cast<uint32_t>(height);
	}
	*outMetrics = metrics;
	return FontStatus::Ok;
}

int64_t fixedFontToPixel(int64_t fontFixed, uint32_t pixelSize, uint32_t unitsPerEm)
{
	return fontFixed * pixelSize / unitsPerEm;
}

bool localPixelFixed(int64_t fontFixed, int64_t originPixel,
	uint32_t pixelSize, uint32_t unitsPerEm, int32_t* out)
{
	if (!out) return false;
	const int64_t value = fixedFontToPixel(fontFixed, pixelSize, unitsPerEm) -
		originPixel * kFixedOne;
	if (value < -2147483647ll - 1ll || value > 2147483647ll) return false;
	*out = static_cast<int32_t>(value);
	return true;
}

bool appendEdgeFixed(RasterScratch* scratch, int64_t fromX, int64_t fromY,
	int64_t toX, int64_t toY, uint32_t pixelSize, uint32_t unitsPerEm,
	int64_t originX, int64_t originY)
{
	if (!scratch || (fromX == toX && fromY == toY)) return true;
	if (scratch->edgeCount >= kMaxFlattenedEdges) return false;
	RasterEdge& edge = scratch->edges[scratch->edgeCount];
	if (!localPixelFixed(fromX, originX,
		pixelSize, unitsPerEm, &edge.x0) ||
		!localPixelFixed(fromY, originY,
		pixelSize, unitsPerEm, &edge.y0) ||
		!localPixelFixed(toX, originX,
		pixelSize, unitsPerEm, &edge.x1) ||
		!localPixelFixed(toY, originY,
		pixelSize, unitsPerEm, &edge.y1)) return false;
	++scratch->edgeCount;
	return true;
}

int64_t interpolateFixed(int64_t first, int64_t second, uint32_t step)
{
	const int64_t t = static_cast<int64_t>(step) * kFixedOne / kCurveSteps;
	return (first * (kFixedOne - t) + second * t) / kFixedOne;
}

struct FixedOutlinePoint {
	int64_t x;
	int64_t y;
};

FixedOutlinePoint quadraticPoint(const OutlinePoint& start, const OutlinePoint& control,
	const OutlinePoint& end, uint32_t step)
{
	const int64_t sx = start.x;
	const int64_t sy = start.y;
	const int64_t cx = control.x;
	const int64_t cy = control.y;
	const int64_t ex = end.x;
	const int64_t ey = end.y;
	const int64_t ax = interpolateFixed(sx, cx, step);
	const int64_t ay = interpolateFixed(sy, cy, step);
	const int64_t bx = interpolateFixed(cx, ex, step);
	const int64_t by = interpolateFixed(cy, ey, step);
	FixedOutlinePoint point = {};
	point.x = interpolateFixed(ax, bx, step);
	point.y = interpolateFixed(ay, by, step);
	return point;
}

struct EdgeBuildContext {
	RasterScratch* scratch;
	uint32_t pixelSize;
	uint32_t unitsPerEm;
	int64_t originX;
	int64_t originY;
	uint8_t failed;
};

void buildSegmentEdges(const Segment& segment, void* context)
{
	EdgeBuildContext* build = static_cast<EdgeBuildContext*>(context);
	if (!build || build->failed) return;
	if (!segment.quadratic) {
		if (!appendEdgeFixed(build->scratch,
			segment.start.x, segment.start.y, segment.end.x, segment.end.y, build->pixelSize,
			build->unitsPerEm, build->originX, build->originY)) build->failed = 1u;
		return;
	}
	FixedOutlinePoint previous = {
		segment.start.x, segment.start.y};
	for (uint32_t i = 1u; i <= kCurveSteps; ++i) {
		const FixedOutlinePoint next = quadraticPoint(segment.start, segment.control,
			segment.end, i);
		if (!appendEdgeFixed(build->scratch, previous.x, previous.y, next.x, next.y,
			build->pixelSize,
			build->unitsPerEm, build->originX, build->originY)) {
			build->failed = 1u;
			return;
		}
		previous = next;
	}
}

bool intersectionLess(const ScanIntersection& left, const ScanIntersection& right)
{
	if (left.x != right.x) return left.x < right.x;
	return left.winding < right.winding;
}

void siftDown(ScanIntersection* values, size_t root, size_t end)
{
	while (root < end / 2u) {
		size_t child = root * 2u + 1u;
		if (child + 1u < end && intersectionLess(values[child], values[child + 1u]))
			++child;
		if (!intersectionLess(values[root], values[child])) return;
		const ScanIntersection temporary = values[root];
		values[root] = values[child];
		values[child] = temporary;
		root = child;
	}
}

void sortIntersections(ScanIntersection* values, size_t count)
{
	if (!values || count < 2u) return;
	for (size_t start = count / 2u; start > 0u; --start) siftDown(values, start - 1u, count);
	for (size_t end = count; end > 1u; --end) {
		const ScanIntersection temporary = values[0];
		values[0] = values[end - 1u];
		values[end - 1u] = temporary;
		siftDown(values, 0u, end - 1u);
	}
}

bool addHorizontalSpan(uint8_t* coverage, uint32_t stride, uint32_t width,
	uint32_t row, int32_t start, int32_t end)
{
	if (!coverage || end <= start) return true;
	const int64_t maxX = static_cast<int64_t>(width) * kFixedOne;
	int64_t left = start;
	int64_t right = end;
	if (left < 0) left = 0;
	if (right > maxX) right = maxX;
	if (right <= left) return true;
	const uint32_t firstPixel = static_cast<uint32_t>(left / kFixedOne);
	const uint32_t lastPixel = static_cast<uint32_t>((right - 1) / kFixedOne);
	for (uint32_t x = firstPixel; x <= lastPixel; ++x) {
		const int64_t pixelLeft = static_cast<int64_t>(x) * kFixedOne;
		const int64_t pixelRight = pixelLeft + kFixedOne;
		const int64_t overlapLeft = left > pixelLeft ? left : pixelLeft;
		const int64_t overlapRight = right < pixelRight ? right : pixelRight;
		const int64_t overlap = overlapRight - overlapLeft;
		const uint32_t amount = static_cast<uint32_t>((overlap * 255ll + 262144ll) / 524288ll);
		uint8_t& value = coverage[static_cast<size_t>(row) * stride + x];
		const uint32_t sum = static_cast<uint32_t>(value) + amount;
		value = static_cast<uint8_t>(sum > 255u ? 255u : sum);
	}
	return true;
}

uint32_t gatherIntersections(RasterScratch* scratch, int32_t y)
{
	uint32_t count = 0u;
	for (uint32_t i = 0; i < scratch->edgeCount; ++i) {
		const RasterEdge& edge = scratch->edges[i];
		int32_t lowY = edge.y0;
		int32_t highY = edge.y1;
		int32_t lowX = edge.x0;
		int32_t highX = edge.x1;
		int8_t winding = 1;
		if (lowY > highY) {
			lowY = edge.y1;
			highY = edge.y0;
			lowX = edge.x1;
			highX = edge.x0;
			winding = -1;
		}
		if (lowY == highY || y < lowY || y >= highY) continue;
		if (count >= kMaxFlattenedEdges) return kMaxFlattenedEdges;
		const int64_t numerator = static_cast<int64_t>(y - lowY) * (highX - lowX);
		const int64_t denominator = static_cast<int64_t>(highY) - lowY;
		ScanIntersection& crossing = scratch->intersections[count++];
		crossing.x = static_cast<int32_t>(static_cast<int64_t>(lowX) + numerator / denominator);
		crossing.winding = winding;
		crossing.reserved[0] = crossing.reserved[1] = crossing.reserved[2] = 0u;
	}
	return count;
}

void rasterizeEdges(RasterScratch* scratch, uint8_t* coverage, uint32_t stride,
	uint32_t width, uint32_t height)
{
	for (uint32_t subRow = 0; subRow < height * kVerticalSamples; ++subRow) {
		const int64_t y = static_cast<int64_t>(height) * kFixedOne -
			static_cast<int64_t>(subRow) * (kFixedOne / kVerticalSamples) -
			(kFixedOne / kVerticalSamples) / 2;
		const uint32_t count = gatherIntersections(scratch, static_cast<int32_t>(y));
		sortIntersections(scratch->intersections, count);
		int32_t winding = 0;
		int32_t previousX = 0;
		bool havePrevious = false;
		uint32_t index = 0u;
		while (index < count) {
			const int32_t currentX = scratch->intersections[index].x;
			if (havePrevious && winding != 0)
				addHorizontalSpan(coverage, stride, width,
					subRow / kVerticalSamples, previousX, currentX);
			int32_t groupWinding = 0;
			while (index < count && scratch->intersections[index].x == currentX)
				groupWinding += scratch->intersections[index++].winding;
			winding += groupWinding;
			previousX = currentX;
			havePrevious = true;
		}
	}
}

FontStatus rasterizeGlyphIndex(const FontTrueTypeSourceView* font,
	const FontTables& tables, uint16_t glyphId, UnicodeCodePoint codePoint,
	FontSize size, uint8_t* coverage, size_t coverageCapacity,
	uint32_t rowStrideBytes, GlyphMetrics* outMetrics)
{
	if (!outMetrics || size.pixelHeight == 0u) return FontStatus::InvalidArgument;
	*outMetrics = {};
	GlyphMetrics metrics = {};
	const FontStatus metricsStatus = decodeGlyphMetrics(font, tables, glyphId,
		codePoint, size, &metrics);
	if (metricsStatus != FontStatus::Ok) return metricsStatus;
	if (metrics.widthPx == 0u || metrics.heightPx == 0u) {
		*outMetrics = metrics;
		return FontStatus::Ok;
	}
	if (rowStrideBytes < metrics.widthPx || rowStrideBytes > kFontTrueTypeMaxBitmapStride)
		return FontStatus::InvalidArgument;
	size_t precedingRows = 0u;
	if (!multiplySize(metrics.heightPx - 1u, rowStrideBytes, &precedingRows) ||
		!addSize(precedingRows, metrics.widthPx, &precedingRows))
		return FontStatus::ArithmeticOverflow;
	if (!coverage || coverageCapacity < precedingRows) {
		*outMetrics = metrics;
		return FontStatus::BufferTooSmall;
	}
	EdgeBuildContext build = {};
	build.scratch = &s_scratch;
	build.pixelSize = size.pixelHeight;
	build.unitsPerEm = font->metadata->unitsPerEm;
	build.originX = metrics.xBearingPx;
	build.originY = metrics.yBearingPx - static_cast<int64_t>(metrics.heightPx);
	if (!walkContours(&s_scratch, &buildSegmentEdges, &build) || build.failed)
		return FontStatus::RasterLimitExceeded;
	if (s_scratch.edgeCount == 0u) {
		*outMetrics = metrics;
		return FontStatus::Ok;
	}
	const uint64_t sampleRows = static_cast<uint64_t>(metrics.heightPx) * kVerticalSamples;
	const uint64_t sortFactor = 28u;
	if (static_cast<uint64_t>(s_scratch.edgeCount) * sampleRows * sortFactor > kMaxScanWork)
		return FontStatus::RasterLimitExceeded;
	for (uint32_t row = 0u; row < metrics.heightPx; ++row)
		for (uint32_t column = 0u; column < metrics.widthPx; ++column)
			coverage[static_cast<size_t>(row) * rowStrideBytes + column] = 0u;
	rasterizeEdges(&s_scratch, coverage, rowStrideBytes,
		metrics.widthPx, metrics.heightPx);
	*outMetrics = metrics;
	return FontStatus::Ok;
}

FontStatus backendRasterize(const void* context, FontSize size,
	UnicodeCodePoint codePoint, uint8_t* coverage, size_t coverageCapacity,
	uint32_t rowStrideBytes, GlyphMetrics* outMetrics)
{
	if (!context || !outMetrics) return FontStatus::InvalidArgument;
	*outMetrics = {};
	if (size.pixelHeight == 0u) return FontStatus::InvalidArgument;
	if (size.pixelHeight > kFontTrueTypeMaxPixelSize) return FontStatus::UnsupportedSize;
	const FontTrueTypeSourceView* font = static_cast<const FontTrueTypeSourceView*>(context);
	FontTables tables = {};
	FontStatus status = locateTables(font, &tables);
	if (status != FontStatus::Ok) return status;
	uint16_t glyphId = 0u;
	status = mapCodePoint(font, tables, codePoint, &glyphId);
	if (status != FontStatus::Ok) return status;
	return rasterizeGlyphIndex(font, tables, glyphId, codePoint, size,
		coverage, coverageCapacity, rowStrideBytes, outMetrics);
}

} // namespace

namespace internal {

uint8_t* fontTrueTypeRasterScratchCoverage()
{
	return s_scratch.coverage;
}

size_t fontTrueTypeRasterScratchCoverageCapacity()
{
	return sizeof(s_scratch.coverage);
}

} // namespace internal

const FontBackend kFontTrueTypeBackend = {
	FontCapabilityGlyphLookup | FontCapabilityMeasurement | FontCapabilityUtf8Input |
		FontCapabilityRasterization | FontCapabilityNoDefaultSelection,
	backendMetrics,
	backendLookup,
	backendRasterize,
};

} // namespace gui
} // namespace gxos
