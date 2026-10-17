#include "include/kernel/font_text_layout.h"

namespace gxos {
namespace gui {
namespace {

struct LineState {
	int64_t pen;
	int64_t left;
	int64_t right;
	uint32_t ascent;
	uint32_t descent;
	uint32_t fallbackCount;
	uint8_t fallback;
};

bool isSuccess(FontStatus status)
{
	return status == FontStatus::Ok || status == FontStatus::FallbackUsed;
}

FontStatus decodeUtf8(const char* input, size_t length, size_t* offset,
	UnicodeCodePoint* outCodePoint)
{
	if (!input || !offset || !outCodePoint || *offset >= length)
		return FontStatus::InvalidArgument;
	const size_t start = *offset;
	const uint8_t first = static_cast<uint8_t>(input[start]);
	if (first <= 0x7fu) {
		*outCodePoint = first;
		*offset = start + 1u;
		return FontStatus::Ok;
	}
	uint32_t codePoint = 0u;
	size_t count = 0u;
	if (first >= 0xc2u && first <= 0xdfu) { codePoint = first & 0x1fu; count = 2u; }
	else if (first >= 0xe0u && first <= 0xefu) { codePoint = first & 0x0fu; count = 3u; }
	else if (first >= 0xf0u && first <= 0xf4u) { codePoint = first & 0x07u; count = 4u; }
	else return FontStatus::InvalidUtf8;
	if (count > length - start) return FontStatus::InvalidUtf8;
	for (size_t i = 1u; i < count; ++i) {
		const uint8_t next = static_cast<uint8_t>(input[start + i]);
		if ((next & 0xc0u) != 0x80u) return FontStatus::InvalidUtf8;
		if (i == 1u && ((first == 0xe0u && next < 0xa0u) ||
			(first == 0xedu && next > 0x9fu) || (first == 0xf0u && next < 0x90u) ||
			(first == 0xf4u && next > 0x8fu))) return FontStatus::InvalidUtf8;
		codePoint = (codePoint << 6) | (next & 0x3fu);
	}
	if (codePoint > 0x10ffffu || (codePoint >= 0xd800u && codePoint <= 0xdfffu))
		return FontStatus::InvalidUtf8;
	*outCodePoint = codePoint;
	*offset = start + count;
	return FontStatus::Ok;
}

FontStatus appendGlyph(FontHandle font, FontSize size, UnicodeCodePoint codePoint,
	const FontMetrics& faceMetrics, LineState* state)
{
	if (!state) return FontStatus::InvalidArgument;
	FontGlyphView glyph = {};
	const FontStatus lookupStatus = FontLookupGlyph(font, size, codePoint, &glyph);
	if (!isSuccess(lookupStatus)) return lookupStatus;
	const GlyphMetrics& metrics = glyph.metrics;
	const bool topOriginBitmap = glyph.bitmapFormat == FontBitmapFormat::ColumnLsbTop;
	const int64_t yBearing = topOriginBitmap
		? static_cast<int64_t>(faceMetrics.baselinePx)
		: static_cast<int64_t>(metrics.yBearingPx);
	const int64_t glyphLeft = state->pen + metrics.xBearingPx;
	const int64_t glyphRight = glyphLeft + metrics.widthPx;
	const int64_t nextPen = state->pen + metrics.advancePx;
	if (glyphLeft < -0x7fffffffLL || glyphRight > 0x7fffffffLL ||
		nextPen > 0x7fffffffLL || yBearing < -0x7fffffffLL ||
		yBearing > 0x7fffffffLL)
		return FontStatus::ArithmeticOverflow;
	if (glyphLeft < state->left) state->left = glyphLeft;
	if (glyphRight > state->right) state->right = glyphRight;
	if (nextPen > state->right) state->right = nextPen;
	const int64_t inkAscent = yBearing;
	const int64_t inkDescent = static_cast<int64_t>(metrics.heightPx) - yBearing;
	if (inkAscent > state->ascent) {
		if (inkAscent > 0x7fffffffLL) return FontStatus::ArithmeticOverflow;
		state->ascent = static_cast<uint32_t>(inkAscent);
	}
	if (inkDescent > state->descent) {
		if (inkDescent > 0x7fffffffLL) return FontStatus::ArithmeticOverflow;
		state->descent = static_cast<uint32_t>(inkDescent);
	}
	state->pen = nextPen;
	if (lookupStatus == FontStatus::FallbackUsed || glyph.fallbackUsed) {
		state->fallback = 1u;
		if (state->fallbackCount != 0xffffffffu) ++state->fallbackCount;
	}
	return FontStatus::Ok;
}

FontStatus scanLine(FontHandle font, FontSize size, const FontMetrics& metrics,
	const char* text, size_t length, LineState* state)
{
	if (!state) return FontStatus::InvalidArgument;
	*state = {};
	state->ascent = metrics.ascentPx;
	state->descent = metrics.descentPx;
	state->right = 0;
	state->left = 0;
	if (length == 0u) return FontStatus::Ok;
	size_t offset = 0u;
	while (offset < length) {
		UnicodeCodePoint codePoint = 0u;
		const FontStatus decodeStatus = decodeUtf8(text, length, &offset, &codePoint);
		if (decodeStatus != FontStatus::Ok) return decodeStatus;
		if (codePoint == '\r' || codePoint == '\n') return FontStatus::UnsupportedGlyphFeature;
		const FontStatus appendStatus = appendGlyph(font, size, codePoint, metrics, state);
		if (appendStatus != FontStatus::Ok) return appendStatus;
	}
	return FontStatus::Ok;
}

uint64_t occupiedWidth(const LineState& state)
{
	return state.right >= state.left ? static_cast<uint64_t>(state.right - state.left) : 0u;
}

void copyBytes(char* output, const char* source, size_t length)
{
	for (size_t i = 0u; i < length; ++i) output[i] = source[i];
}

FontStatus fillLayout(FontSize size, const FontMetrics& metrics,
	const LineState& state, size_t outputLength, size_t sourceLength,
	bool clipped, bool truncated, FontTextLineLayout* output)
{
	if (!output) return FontStatus::InvalidArgument;
	const uint64_t lineInk = static_cast<uint64_t>(state.ascent) + state.descent;
	const uint64_t lineHeight = lineInk > metrics.lineHeightPx ? lineInk : metrics.lineHeightPx;
	if (lineHeight > 0x7fffffffu || state.pen > 0x7fffffffu ||
		state.left < -0x7fffffffLL || state.right > 0x7fffffffLL ||
		occupiedWidth(state) > 0xffffffffu)
		return FontStatus::ArithmeticOverflow;
	const uint32_t leading = static_cast<uint32_t>((lineHeight - lineInk) / 2u);
	*output = {};
	output->pixelSize = size.pixelHeight;
	output->ascentPx = state.ascent;
	output->descentPx = state.descent;
	output->lineHeightPx = static_cast<uint32_t>(lineHeight);
	output->baselineFromTopPx = leading + state.ascent;
	output->advancePx = static_cast<uint32_t>(state.pen);
	output->inkWidthPx = static_cast<uint32_t>(occupiedWidth(state));
	output->inkAscentPx = state.ascent;
	output->inkDescentPx = state.descent;
	output->outputByteLength = static_cast<uint32_t>(outputLength);
	output->sourceByteLength = static_cast<uint32_t>(sourceLength);
	output->inkLeftPx = static_cast<int32_t>(state.left);
	output->inkRightPx = static_cast<int32_t>(state.right);
	output->originOffsetXPx = state.left < 0 ? static_cast<int32_t>(-state.left) : 0;
	output->clipped = clipped ? 1u : 0u;
	output->truncated = truncated ? 1u : 0u;
	output->fallbackUsed = state.fallback;
	return FontStatus::Ok;
}

} // namespace

FontStatus FontTextLayoutSingleLine(FontHandle font, FontSize size,
	const char* utf8, size_t byteLength, uint32_t availableWidthPx,
	FontTextOverflowPolicy overflowPolicy, char* output, size_t outputCapacity,
	FontTextLineLayout* outLayout)
{
	if (outLayout) *outLayout = {};
	if (!outLayout || !output || outputCapacity == 0u ||	(!utf8 && byteLength != 0u) ||
		byteLength > kFontMaxTextBytes ||
		(overflowPolicy != FontTextOverflowPolicy::Clip &&
		 overflowPolicy != FontTextOverflowPolicy::Ellipsis))
		return byteLength > kFontMaxTextBytes ? FontStatus::InputTooLong : FontStatus::InvalidArgument;
	output[0] = '\0';
	FontMetrics metrics = {};
	FontStatus status = FontGetMetrics(font, size, &metrics);
	if (!isSuccess(status)) return status;
	if (metrics.lineHeightPx == 0u || metrics.lineHeightPx > 4096u)
		return FontStatus::BackendFailure;
	TextMeasureResult measured = {};
	const char* safeText = utf8 ? utf8 : "";
	status = FontMeasureText(font, size, safeText, byteLength, &measured);
	if (!isSuccess(status)) return status;
	LineState full = {};
	const FontStatus scanStatus = scanLine(font, size, metrics, safeText, byteLength, &full);
	if (scanStatus != FontStatus::Ok) return scanStatus;

	if (overflowPolicy == FontTextOverflowPolicy::Clip) {
		if (byteLength + 1u > outputCapacity) return FontStatus::BufferTooSmall;
		copyBytes(output, safeText, byteLength);
		output[byteLength] = '\0';
		const bool clipped = occupiedWidth(full) > availableWidthPx;
		const FontStatus fillStatus = fillLayout(size, metrics, full, byteLength,
			byteLength, clipped, false, outLayout);
		if (fillStatus != FontStatus::Ok) return fillStatus;
		return (full.fallback || status == FontStatus::FallbackUsed)
			? FontStatus::FallbackUsed : FontStatus::Ok;
	}

	if (occupiedWidth(full) <= availableWidthPx && byteLength + 1u <= outputCapacity) {
		copyBytes(output, safeText, byteLength);
		output[byteLength] = '\0';
		const FontStatus fillStatus = fillLayout(size, metrics, full, byteLength,
			byteLength, false, false, outLayout);
		if (fillStatus != FontStatus::Ok) return fillStatus;
		return (full.fallback || status == FontStatus::FallbackUsed)
			? FontStatus::FallbackUsed : FontStatus::Ok;
	}

	const char* ellipsis = "...";
	LineState dots = {};
	dots.ascent = metrics.ascentPx;
	dots.descent = metrics.descentPx;
	for (uint32_t i = 0u; i < 3u; ++i) {
		const FontStatus appendStatus = appendGlyph(font, size, '.', metrics, &dots);
		if (appendStatus != FontStatus::Ok) return appendStatus;
	}
	bool foundFit = false;
	size_t bestBytes = 0u;
	LineState bestState = {};
	LineState prefix = {};
	prefix.ascent = metrics.ascentPx;
	prefix.descent = metrics.descentPx;
	auto consider = [&](size_t prefixBytes) {
		LineState combined = prefix;
		const int64_t base = combined.pen;
		const int64_t dotLeft = base + dots.left;
		const int64_t dotRight = base + dots.right;
		const int64_t dotPen = base + dots.pen;
		if (dotLeft < combined.left) combined.left = dotLeft;
		if (dotRight > combined.right) combined.right = dotRight;
		if (dotPen > combined.right) combined.right = dotPen;
		if (dots.ascent > combined.ascent) combined.ascent = dots.ascent;
		if (dots.descent > combined.descent) combined.descent = dots.descent;
		combined.pen = dotPen;
		combined.fallback = static_cast<uint8_t>(combined.fallback || dots.fallback);
		if (0xffffffffu - combined.fallbackCount < dots.fallbackCount)
			combined.fallbackCount = 0xffffffffu;
		else combined.fallbackCount += dots.fallbackCount;
		if (prefixBytes + 3u + 1u <= outputCapacity &&
			occupiedWidth(combined) <= availableWidthPx) {
			foundFit = true;
			bestBytes = prefixBytes;
			bestState = combined;
		}
	};
	consider(0u);
	size_t offset = 0u;
	while (offset < byteLength) {
		const size_t cpStart = offset;
		UnicodeCodePoint codePoint = 0u;
		const FontStatus decodeStatus = decodeUtf8(safeText, byteLength, &offset, &codePoint);
		if (decodeStatus != FontStatus::Ok) return decodeStatus;
		const FontStatus appendStatus = appendGlyph(font, size, codePoint, metrics, &prefix);
		if (appendStatus != FontStatus::Ok) return appendStatus;
		consider(offset);
		(void)cpStart;
	}
	if (foundFit) {
		copyBytes(output, safeText, bestBytes);
		copyBytes(output + bestBytes, ellipsis, 3u);
		output[bestBytes + 3u] = '\0';
		const FontStatus fillStatus = fillLayout(size, metrics, bestState,
			bestBytes + 3u, byteLength, true, true, outLayout);
		if (fillStatus != FontStatus::Ok) return fillStatus;
		return (bestState.fallback || status == FontStatus::FallbackUsed)
			? FontStatus::FallbackUsed : FontStatus::Ok;
	}

	const FontStatus fillStatus = fillLayout(size, metrics, prefix, 0u,
		byteLength, true, byteLength != 0u, outLayout);
	if (fillStatus != FontStatus::Ok) return fillStatus;
	return (prefix.fallback || status == FontStatus::FallbackUsed)
		? FontStatus::FallbackUsed : FontStatus::Ok;
}

} // namespace gui
} // namespace gxos
