#include "include/kernel/font_contract.h"
#include "bitmap_font.h"

namespace gxos {
namespace gui {
namespace {

FontStatus bitmapMetrics(const void*, FontSize size, FontMetrics* out)
{
	if (!out || size.pixelHeight == 0) return FontStatus::InvalidArgument;
	if (size.pixelHeight != kBitmapFontPixelHeight) return FontStatus::UnsupportedSize;
	out->pixelSize = kBitmapFontPixelHeight;
	out->ascentPx = BitmapFont::kGlyphH;
	out->descentPx = 0;
	out->lineGapPx = 0;
	out->lineHeightPx = BitmapFont::kGlyphH;
	out->baselinePx = BitmapFont::kGlyphH;
	return FontStatus::Ok;
}

FontStatus bitmapLookup(const void*, UnicodeCodePoint codePoint, FontGlyphView* out)
{
	if (!out) return FontStatus::InvalidArgument;
	if (codePoint < 32u || codePoint > 126u) return FontStatus::UnsupportedCodePoint;
	const char c = static_cast<char>(codePoint);
	const uint8_t* bitmap = BitmapFont::Glyph(c);
	if (!bitmap) return FontStatus::UnsupportedCodePoint;
	out->metrics.glyphId = codePoint - 32u;
	out->metrics.codePoint = codePoint;
	out->metrics.xBearingPx = 0;
	out->metrics.yBearingPx = 0;
	out->metrics.widthPx = BitmapFont::kGlyphW;
	out->metrics.heightPx = BitmapFont::kGlyphH;
	out->metrics.advancePx = BitmapFont::kGlyphW + BitmapFont::kSpacing;
	out->bitmap = bitmap;
	out->bitmapByteLength = BitmapFont::kGlyphW;
	out->bitmapFormat = FontBitmapFormat::ColumnLsbTop;
	out->fallbackUsed = 0;
	out->reserved[0] = 0;
	out->reserved[1] = 0;
	out->requestedCodePoint = codePoint;
	return FontStatus::Ok;
}

const FontBackend kBitmapBackend = {
	FontCapabilityGlyphLookup | FontCapabilityBitmapData |
		FontCapabilityMeasurement | FontCapabilityUtf8Input | FontCapabilityMonospace,
	bitmapMetrics,
	bitmapLookup
};

const FontDefinition kBitmapDefinition = {
	{kBuiltinBitmapFontId},
	"guideX bitmap 5x7",
	&kBitmapBackend,
	nullptr,
	nullptr,
	nullptr
};

} // namespace

const FontDefinition* FontBuiltinBitmapDefinition()
{
	return &kBitmapDefinition;
}

} // namespace gui
} // namespace gxos
