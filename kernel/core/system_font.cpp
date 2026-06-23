#include "include/kernel/system_font.h"

#include "bitmap_font.h"

#if defined(GXOS_BARE_METAL)
#include "include/kernel/framebuffer.h"
#include "include/kernel/generated/system_font_roboto.h"
#else
#include "kernel/core/include/kernel/image_adapter.h"
#include "logger.h"
#endif

#if defined(GXOS_BARE_METAL)
#include <string.h>
#else
#include <cstring>
#endif

namespace gxos {
namespace gui {
namespace {

static constexpr int kPrintableGlyphCount = 95;
static constexpr uint8_t kGlyphPadding = 1;

static inline int max_int(int a, int b)
{
	return a > b ? a : b;
}

static BitmapFontFace s_faces[] = {
	{{FontSize::Small9, FontWeight::Regular, FontSlant::Normal}, "Roboto 9 Regular", "/system/fonts/roboto/roboto_9pt_regular.png", nullptr, 260, 160, 20, 20, 13, 8, 32, kPrintableGlyphCount, 12, 8, 4, 16, 0, 0, {}},
	{{FontSize::Small9, FontWeight::Bold, FontSlant::Normal}, "Roboto 9 Bold", "/system/fonts/roboto/roboto_9pt_bold.png", nullptr, 260, 160, 20, 20, 13, 8, 32, kPrintableGlyphCount, 12, 8, 4, 16, 0, 0, {}},
	{{FontSize::Small9, FontWeight::Regular, FontSlant::Italic}, "Roboto 9 Italic", "/system/fonts/roboto/roboto_9pt_italic.png", nullptr, 260, 160, 20, 20, 13, 8, 32, kPrintableGlyphCount, 12, 8, 4, 16, 0, 0, {}},
	{{FontSize::Small9, FontWeight::Bold, FontSlant::Italic}, "Roboto 9 Bold Italic", "/system/fonts/roboto/roboto_9pt_bolditalic.png", nullptr, 260, 160, 20, 20, 13, 8, 32, kPrintableGlyphCount, 12, 8, 4, 16, 0, 0, {}},
	{{FontSize::Normal12, FontWeight::Regular, FontSlant::Normal}, "Roboto 12 Regular", "/system/fonts/roboto/roboto_12pt_regular.png", nullptr, 260, 160, 20, 20, 13, 8, 32, kPrintableGlyphCount, 13, 9, 5, 18, 0, 0, {}},
	{{FontSize::Normal12, FontWeight::Bold, FontSlant::Normal}, "Roboto 12 Bold", "/system/fonts/roboto/roboto_12pt_bold.png", nullptr, 260, 160, 20, 20, 13, 8, 32, kPrintableGlyphCount, 13, 9, 5, 18, 0, 0, {}},
	{{FontSize::Normal12, FontWeight::Regular, FontSlant::Italic}, "Roboto 12 Italic", "/system/fonts/roboto/roboto_12pt_italic.png", nullptr, 260, 160, 20, 20, 13, 8, 32, kPrintableGlyphCount, 13, 9, 5, 18, 0, 0, {}},
	{{FontSize::Normal12, FontWeight::Bold, FontSlant::Italic}, "Roboto 12 Bold Italic", "/system/fonts/roboto/roboto_12pt_bolditalic.png", nullptr, 260, 160, 20, 20, 13, 8, 32, kPrintableGlyphCount, 13, 9, 5, 18, 0, 0, {}},
	{{FontSize::Normal12, FontWeight::Thin, FontSlant::Normal}, "Roboto 12 Thin", "/system/fonts/roboto/roboto_12pt_thin.png", nullptr, 390, 240, 30, 30, 13, 8, 32, kPrintableGlyphCount, 20, 14, 6, 24, 0, 0, {}}};

static bool s_initialized = false;

static int stringLength(const char* str, int len)
{
	if (!str) return 0;
	if (len >= 0) return len;
	return BitmapFont::StringLength(str);
}

static bool keysEqual(const FontKey& a, const FontKey& b)
{
	return a.size == b.size && a.weight == b.weight && a.slant == b.slant;
}

static bool validGlyphIndex(const BitmapFontFace& face, int glyphIndex)
{
	return glyphIndex >= 0 && glyphIndex < face.glyphCount;
}

static const BitmapGlyphMetrics* glyphMetrics(const BitmapFontFace& face, char c)
{
	int glyphIndex = static_cast<int>(static_cast<unsigned char>(c)) - static_cast<int>(face.firstCodepoint);
	if (!validGlyphIndex(face, glyphIndex)) return nullptr;
	return &face.glyphs[glyphIndex];
}

static uint8_t atlasPixelAlpha(const BitmapFontFace& face, int atlasX, int atlasY)
{
	if (!face.atlasAlpha) return false;
	if (atlasX < 0 || atlasY < 0 || atlasX >= face.atlasWidth || atlasY >= face.atlasHeight) return false;
	return face.atlasAlpha[static_cast<size_t>(atlasY) * face.atlasWidth + atlasX];
}

static uint8_t spaceAdvance(const BitmapFontFace& face)
{
	if (face.cellWidth >= 30) return 7;
	return face.key.size == FontSize::Small9 ? 4 : 5;
}

static uint8_t fallbackAdvance(const BitmapFontFace& face)
{
	return spaceAdvance(face);
}

static int fallbackAscent()
{
	return max_int(1, BitmapFont::kGlyphH - 2);
}

static int fallbackDescent()
{
	return max_int(1, BitmapFont::kGlyphH - fallbackAscent());
}

static int fallbackLineHeight()
{
	return BitmapFont::kGlyphH;
}

static int fallbackBaselineOffset()
{
	return fallbackAscent();
}

static uint8_t boundedAdvance(int width)
{
	int advance = width + kGlyphPadding;
	if (advance < 1) advance = 1;
	if (advance > 255) advance = 255;
	return static_cast<uint8_t>(advance);
}

static bool stringEquals(const char* a, const char* b)
{
	if (!a || !b) return false;
	while (*a && *b) {
		if (*a != *b) return false;
		++a;
		++b;
	}
	return *a == *b;
}

static void populateLegacyFallbackMetrics(BitmapFontFace& face)
{
	for (int i = 0; i < face.glyphCount; ++i) {
		char c = static_cast<char>(face.firstCodepoint + i);
		const uint8_t* glyph = BitmapFont::Glyph(c);
		BitmapGlyphMetrics& metrics = face.glyphs[i];
		if (!glyph) {
			metrics = {0, 0, 0, 0, 0, 0, 6, 0};
			continue;
		}

		int minX = BitmapFont::kGlyphW;
		int minY = BitmapFont::kGlyphH;
		int maxX = -1;
		int maxY = -1;
		for (int x = 0; x < BitmapFont::kGlyphW; ++x) {
			uint8_t bits = glyph[x];
			for (int y = 0; y < BitmapFont::kGlyphH; ++y) {
				if ((bits & (1 << y)) == 0) continue;
				if (x < minX) minX = x;
				if (y < minY) minY = y;
				if (x > maxX) maxX = x;
				if (y > maxY) maxY = y;
			}
		}

		if (maxX < minX || maxY < minY) {
			metrics = {0, 0, 0, 0, 0, 0, 4, 0};
			continue;
		}

		metrics.left = static_cast<uint8_t>(minX);
		metrics.top = static_cast<uint8_t>(minY);
		metrics.width = static_cast<uint8_t>(maxX - minX + 1);
		metrics.height = static_cast<uint8_t>(maxY - minY + 1);
		metrics.xOffset = 0;
		metrics.yOffset = static_cast<int8_t>(minY);
		metrics.advance = static_cast<uint8_t>(BitmapFont::kGlyphW + BitmapFont::kSpacing);
		metrics.hasPixels = 1;
	}
}

static void populateAtlasMetrics(BitmapFontFace& face)
{
	for (int glyphIndex = 0; glyphIndex < face.glyphCount; ++glyphIndex) {
		BitmapGlyphMetrics& metrics = face.glyphs[glyphIndex];
		int col = glyphIndex % face.columns;
		int row = glyphIndex / face.columns;
		int originX = col * face.cellWidth;
		int originY = row * face.cellHeight;
		int minX = face.cellWidth;
		int minY = face.cellHeight;
		int maxX = -1;
		int maxY = -1;

		for (int y = 0; y < face.cellHeight; ++y) {
			for (int x = 0; x < face.cellWidth; ++x) {
				if (atlasPixelAlpha(face, originX + x, originY + y) == 0) continue;
				if (x < minX) minX = x;
				if (y < minY) minY = y;
				if (x > maxX) maxX = x;
				if (y > maxY) maxY = y;
			}
		}

		if (maxX < minX || maxY < minY) {
			char c = static_cast<char>(face.firstCodepoint + glyphIndex);
			metrics = {0, 0, 0, 0, 0, 0, c == ' ' ? spaceAdvance(face) : fallbackAdvance(face), 0};
			continue;
		}

		metrics.left = static_cast<uint8_t>(minX);
		metrics.top = static_cast<uint8_t>(minY);
		metrics.width = static_cast<uint8_t>(maxX - minX + 1);
		metrics.height = static_cast<uint8_t>(maxY - minY + 1);
		metrics.xOffset = 0;
		metrics.yOffset = static_cast<int8_t>(minY);
		metrics.advance = boundedAdvance(metrics.width);
		metrics.hasPixels = 1;
	}
}

#if defined(GXOS_BARE_METAL)
static const generated::EmbeddedRobotoAtlas* embeddedAtlasForFace(const BitmapFontFace& face)
{
	for (const generated::EmbeddedRobotoAtlas* atlas : generated::kEmbeddedRobotoAtlases) {
		if (!atlas) continue;
		if (face.key.size == FontSize::Normal12 &&
			face.key.slant == FontSlant::Normal &&
			face.key.weight == FontWeight::Regular &&
			stringEquals(atlas->name, "roboto_12pt_regular")) return atlas;
		if (face.key.size == FontSize::Normal12 &&
			face.key.slant == FontSlant::Normal &&
			face.key.weight == FontWeight::Bold &&
			stringEquals(atlas->name, "roboto_12pt_bold")) return atlas;
		if (face.key.size == FontSize::Small9 &&
			face.key.slant == FontSlant::Normal &&
			face.key.weight == FontWeight::Regular &&
			stringEquals(atlas->name, "roboto_9pt_regular")) return atlas;
		if (face.key.size == FontSize::Small9 &&
			face.key.slant == FontSlant::Normal &&
			face.key.weight == FontWeight::Bold &&
			stringEquals(atlas->name, "roboto_9pt_bold")) return atlas;
	}
	return nullptr;
}

static void loadEmbeddedAtlas(BitmapFontFace& face)
{
	const generated::EmbeddedRobotoAtlas* atlas = embeddedAtlasForFace(face);
	if (!atlas) {
		face.fallback = 1;
		face.ready = 1;
		populateLegacyFallbackMetrics(face);
		return;
	}

	face.atlasAlpha = atlas->alpha;
	face.atlasWidth = atlas->width;
	face.atlasHeight = atlas->height;
	face.cellWidth = atlas->cellWidth;
	face.cellHeight = atlas->cellHeight;
	face.columns = atlas->columns;
	face.rows = atlas->rows;
	face.firstCodepoint = atlas->firstCodepoint;
	face.glyphCount = static_cast<uint8_t>(atlas->lastCodepoint - atlas->firstCodepoint + 1);
	face.fallback = 0;
	face.ready = 1;
	populateAtlasMetrics(face);
}
#else
static void loadAtlas(BitmapFontFace& face)
{
	ImageBitmap atlas = ImageAdapter::LoadFromFile(face.assetPath);
	if (atlas.status != ImageLoadStatus::Ok || !atlas.image || !atlas.image->isValid()) {
		Logger::write(LogLevel::Warn, std::string("SystemFont: atlas unavailable, using fallback for ") + face.name + " path=" + face.assetPath);
		face.fallback = 1;
		face.ready = 1;
		populateLegacyFallbackMetrics(face);
		return;
	}

	if (atlas.width != face.atlasWidth || atlas.height != face.atlasHeight) {
		Logger::write(LogLevel::Warn, std::string("SystemFont: atlas size mismatch for ") + face.name);
		face.fallback = 1;
		face.ready = 1;
		populateLegacyFallbackMetrics(face);
		return;
	}

	const size_t alphaSize = static_cast<size_t>(face.atlasWidth) * static_cast<size_t>(face.atlasHeight);
	uint8_t* alpha = new uint8_t[alphaSize];
	if (!alpha) {
		face.fallback = 1;
		face.ready = 1;
		populateLegacyFallbackMetrics(face);
		return;
	}

	for (size_t i = 0; i < alphaSize; ++i) {
		alpha[i] = atlas.image->Pixels[i * atlas.image->Channels + 3];
	}

	face.atlasAlpha = alpha;
	face.fallback = 0;
	face.ready = 1;
	populateAtlasMetrics(face);
}
#endif

static BitmapFontFace* findFace(const FontKey& key)
{
	for (auto& face : s_faces) {
		if (keysEqual(face.key, key)) return &face;
	}
	return nullptr;
}

static const BitmapFontFace* defaultFallbackFace()
{
	return &s_faces[4];
}

static const BitmapFontFace* ensureFace(const FontKey& key)
{
	SystemFont::EnsureInitialized();
	BitmapFontFace* face = findFace(key);
	return face ? face : defaultFallbackFace();
}

static int measureFallbackWidth(const char* str, int len)
{
	return BitmapFont::MeasureWidth(str, len);
}

static void drawFallbackBuffer(uint32_t* pixels, int pitch, int bufW, int bufH,
							   int x, int y, const char* str, int len, uint32_t color)
{
	BitmapFont::DrawStringToBuffer(pixels, pitch, bufW, bufH, x, y, str, len, color);
}

static uint32_t blendColor(uint32_t dst, uint32_t src, uint8_t alpha)
{
	if (alpha == 0) return dst;
	if (alpha == 255) return src;
	uint8_t sr = static_cast<uint8_t>((src >> 16) & 0xFF);
	uint8_t sg = static_cast<uint8_t>((src >> 8) & 0xFF);
	uint8_t sb = static_cast<uint8_t>(src & 0xFF);
	uint8_t dr = static_cast<uint8_t>((dst >> 16) & 0xFF);
	uint8_t dg = static_cast<uint8_t>((dst >> 8) & 0xFF);
	uint8_t db = static_cast<uint8_t>(dst & 0xFF);
	uint8_t r = static_cast<uint8_t>((sr * alpha + dr * (255 - alpha)) / 255);
	uint8_t g = static_cast<uint8_t>((sg * alpha + dg * (255 - alpha)) / 255);
	uint8_t b = static_cast<uint8_t>((sb * alpha + db * (255 - alpha)) / 255);
	return (dst & 0xFF000000u) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
static void drawFallbackHdc(HDC dc, int x, int y, const char* str, int len, COLORREF color)
{
	BitmapFont::DrawString(dc, x, y, str, len, color);
}

static void blendPixelHdc(HDC dc, int x, int y, COLORREF color, uint8_t alpha)
{
	if (alpha == 0) return;
	if (alpha == 255) {
		SetPixel(dc, x, y, color);
		return;
	}
	COLORREF dst = GetPixel(dc, x, y);
	if (dst == CLR_INVALID) {
		SetPixel(dc, x, y, color);
		return;
	}
	uint8_t sr = GetRValue(color);
	uint8_t sg = GetGValue(color);
	uint8_t sb = GetBValue(color);
	uint8_t dr = GetRValue(dst);
	uint8_t dg = GetGValue(dst);
	uint8_t db = GetBValue(dst);
	uint8_t r = static_cast<uint8_t>((sr * alpha + dr * (255 - alpha)) / 255);
	uint8_t g = static_cast<uint8_t>((sg * alpha + dg * (255 - alpha)) / 255);
	uint8_t b = static_cast<uint8_t>((sb * alpha + db * (255 - alpha)) / 255);
	SetPixel(dc, x, y, RGB(r, g, b));
}
#endif

} // namespace

void SystemFont::EnsureInitialized()
{
	if (s_initialized) return;

	for (auto& face : s_faces) {
#if defined(GXOS_BARE_METAL)
		loadEmbeddedAtlas(face);
#else
		loadAtlas(face);
#endif
	}

	s_initialized = true;
}

const BitmapFontFace* SystemFont::GetFace(FontRole role)
{
	switch (role) {
	case FontRole::Default:
		return GetFace(FontSize::Normal12, FontWeight::Regular, FontSlant::Normal);
	case FontRole::Title:
		return GetFace(FontSize::Normal12, FontWeight::Bold, FontSlant::Normal);
	case FontRole::Small:
		return GetFace(FontSize::Small9, FontWeight::Regular, FontSlant::Normal);
	case FontRole::SmallBold:
		return GetFace(FontSize::Small9, FontWeight::Bold, FontSlant::Normal);
	case FontRole::Emphasis:
		return GetFace(FontSize::Normal12, FontWeight::Regular, FontSlant::Italic);
	case FontRole::Fallback:
	default:
		return GetFallbackFace();
	}
}

const BitmapFontFace* SystemFont::GetFace(FontSize size, FontWeight weight, FontSlant slant)
{
	FontKey key{size, weight, slant};
	return ensureFace(key);
}

const BitmapFontFace* SystemFont::GetFallbackFace()
{
	EnsureInitialized();
	return defaultFallbackFace();
}

int SystemFont::MeasureWidth(const BitmapFontFace* face, const char* str, int len)
{
	if (!str) return 0;
	if (!face || face->fallback) return measureFallbackWidth(str, len);

	len = stringLength(str, len);
	int width = 0;
	for (int i = 0; i < len; ++i) {
		const BitmapGlyphMetrics* metrics = glyphMetrics(*face, str[i]);
		if (!metrics) {
			width += fallbackAdvance(*face);
			continue;
		}
		width += metrics->advance;
	}
	return width;
}

int SystemFont::MeasureWidth(FontRole role, const char* str, int len)
{
	return MeasureWidth(GetFace(role), str, len);
}

int SystemFont::MeasureAscent(const BitmapFontFace* face)
{
	if (!face || face->fallback) return fallbackAscent();
	return max_int(0, static_cast<int>(face->ascent));
}

int SystemFont::MeasureAscent(FontRole role)
{
	return MeasureAscent(GetFace(role));
}

int SystemFont::MeasureDescent(const BitmapFontFace* face)
{
	if (!face || face->fallback) return fallbackDescent();
	return max_int(0, static_cast<int>(face->descent));
}

int SystemFont::MeasureDescent(FontRole role)
{
	return MeasureDescent(GetFace(role));
}

int SystemFont::MeasureLineHeight(const BitmapFontFace* face)
{
	if (!face || face->fallback) return fallbackLineHeight();
	return max_int(1, static_cast<int>(face->lineHeight));
}

int SystemFont::MeasureLineHeight(FontRole role)
{
	return MeasureLineHeight(GetFace(role));
}

int SystemFont::BaselineOffset(const BitmapFontFace* face)
{
	if (!face || face->fallback) return fallbackBaselineOffset();
	const int baseline = static_cast<int>(face->baseline);
	if (baseline > 0) return baseline;
	return MeasureAscent(face);
}

int SystemFont::BaselineOffset(FontRole role)
{
	return BaselineOffset(GetFace(role));
}

int SystemFont::MeasureHeight(const BitmapFontFace* face)
{
	return MeasureLineHeight(face);
}

int SystemFont::MeasureHeight(FontRole role)
{
	return MeasureHeight(GetFace(role));
}

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
void SystemFont::DrawText(HDC dc, int x, int y, const char* str, int len, COLORREF color, const BitmapFontFace* face)
{
	if (!dc || !str) return;
	if (!face || face->fallback || !face->atlasAlpha) {
		drawFallbackHdc(dc, x, y, str, len, color);
		return;
	}

	len = stringLength(str, len);
	int cursorX = x;
	for (int i = 0; i < len; ++i) {
		int glyphIndex = static_cast<int>(static_cast<unsigned char>(str[i])) - static_cast<int>(face->firstCodepoint);
		if (!validGlyphIndex(*face, glyphIndex)) {
			cursorX += fallbackAdvance(*face);
			continue;
		}

		const BitmapGlyphMetrics& metrics = face->glyphs[glyphIndex];
		if (!metrics.hasPixels) {
			cursorX += metrics.advance;
			continue;
		}

		int col = glyphIndex % face->columns;
		int row = glyphIndex / face->columns;
		int originX = col * face->cellWidth;
		int originY = row * face->cellHeight;
		for (int py = 0; py < metrics.height; ++py) {
			for (int px = 0; px < metrics.width; ++px) {
				uint8_t alpha = atlasPixelAlpha(*face, originX + metrics.left + px, originY + metrics.top + py);
				if (alpha == 0) continue;
				blendPixelHdc(dc, cursorX + metrics.xOffset + px, y + metrics.yOffset + py, color, alpha);
			}
		}
		cursorX += metrics.advance;
	}
}

void SystemFont::DrawText(HDC dc, int x, int y, const char* str, int len, COLORREF color, FontRole role)
{
	DrawText(dc, x, y, str, len, color, GetFace(role));
}
#endif

void SystemFont::DrawTextToBuffer(uint32_t* pixels, int pitch, int bufW, int bufH,
								  int x, int y, const char* str, int len, uint32_t color,
								  const BitmapFontFace* face)
{
	if (!str || !pixels) return;
	if (!face || face->fallback || !face->atlasAlpha) {
		drawFallbackBuffer(pixels, pitch, bufW, bufH, x, y, str, len, color);
		return;
	}

	len = stringLength(str, len);
	int cursorX = x;
	for (int i = 0; i < len; ++i) {
		int glyphIndex = static_cast<int>(static_cast<unsigned char>(str[i])) - static_cast<int>(face->firstCodepoint);
		if (!validGlyphIndex(*face, glyphIndex)) {
			cursorX += fallbackAdvance(*face);
			continue;
		}

		const BitmapGlyphMetrics& metrics = face->glyphs[glyphIndex];
		if (!metrics.hasPixels) {
			cursorX += metrics.advance;
			continue;
		}

		int col = glyphIndex % face->columns;
		int row = glyphIndex / face->columns;
		int originX = col * face->cellWidth;
		int originY = row * face->cellHeight;
		for (int py = 0; py < metrics.height; ++py) {
			int destY = y + metrics.yOffset + py;
			if (destY < 0 || destY >= bufH) continue;
			for (int px = 0; px < metrics.width; ++px) {
				int destX = cursorX + metrics.xOffset + px;
				if (destX < 0 || destX >= bufW) continue;
				uint8_t alpha = atlasPixelAlpha(*face, originX + metrics.left + px, originY + metrics.top + py);
				if (alpha == 0) continue;
				uint32_t& dst = pixels[destY * (pitch / 4) + destX];
				dst = blendColor(dst, color, alpha);
			}
		}
		cursorX += metrics.advance;
	}
}

void SystemFont::DrawTextToBuffer(uint32_t* pixels, int pitch, int bufW, int bufH,
								  int x, int y, const char* str, int len, uint32_t color,
								  FontRole role)
{
	DrawTextToBuffer(pixels, pitch, bufW, bufH, x, y, str, len, color, GetFace(role));
}

} // namespace gui
} // namespace gxos
