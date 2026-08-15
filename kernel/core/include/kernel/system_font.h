#pragma once

#if defined(GXOS_BARE_METAL)
#include "types.h"
#else
#include <cstddef>
#include <cstdint>
#endif

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gxos {
namespace gui {

enum class FontSize : uint8_t {
	Small9 = 0,
	Normal12 = 1,
};

enum class FontWeight : uint8_t {
	Regular = 0,
	Bold = 1,
	Thin = 2,
};

enum class FontSlant : uint8_t {
	Normal = 0,
	Italic = 1,
};

enum class FontRole : uint8_t {
	Default = 0,
	Title = 1,
	Small = 2,
	SmallBold = 3,
	Emphasis = 4,
	Fallback = 5,
};

struct FontKey {
	FontSize size;
	FontWeight weight;
	FontSlant slant;
};

struct BitmapGlyphMetrics {
	uint8_t left;
	uint8_t top;
	uint8_t width;
	uint8_t height;
	int8_t xOffset;
	int8_t yOffset;
	uint8_t advance;
	uint8_t hasPixels;
};

struct BitmapFontFace {
	FontKey key;
	const char* name;
	const char* assetPath;
	const uint8_t* atlasAlpha;
	uint16_t atlasWidth;
	uint16_t atlasHeight;
	uint16_t cellWidth;
	uint16_t cellHeight;
	uint8_t columns;
	uint8_t rows;
	uint8_t firstCodepoint;
	uint8_t glyphCount;
	uint8_t baseline;
	uint8_t ascent;
	uint8_t descent;
	uint8_t lineHeight;
	uint8_t fallback;
	uint8_t ready;
	BitmapGlyphMetrics glyphs[95];
};

class SystemFont {
public:
	static void EnsureInitialized();
	// Navigator uses these helpers for a bounded CSS pixel-size model. The
	// packaged atlas remains the authoritative source for both measurement and
	// painting; scaling is performed only after the face has been selected.
	static const BitmapFontFace* GetFaceForPixelSize(int requestedPx,
		FontWeight weight = FontWeight::Regular, FontSlant slant = FontSlant::Normal);
	static int ScalePercentForPixelSize(int requestedPx);
	static bool IsFaceFallback(const BitmapFontFace* face);
	static bool IsRobotoAvailable();
	static const BitmapFontFace* GetFace(FontRole role);
	static const BitmapFontFace* GetFace(FontSize size, FontWeight weight = FontWeight::Regular,
										 FontSlant slant = FontSlant::Normal);
	static const BitmapFontFace* GetFallbackFace();
	static int MeasureWidth(const BitmapFontFace* face, const char* str, int len = -1);
	static int MeasureWidthScaled(const BitmapFontFace* face, const char* str, int len,
		int scalePercent);
	static int MeasureWidth(FontRole role, const char* str, int len = -1);
	static int MeasureAscent(const BitmapFontFace* face);
	static int MeasureAscent(FontRole role);
	static int MeasureDescent(const BitmapFontFace* face);
	static int MeasureDescent(FontRole role);
	static int MeasureLineHeight(const BitmapFontFace* face);
	static int MeasureLineHeight(FontRole role);
	static int BaselineOffset(const BitmapFontFace* face);
	static int BaselineOffset(FontRole role);
	static int MeasureHeight(const BitmapFontFace* face);
	static int MeasureHeight(FontRole role);
	static int MeasureAscentScaled(const BitmapFontFace* face, int scalePercent);
	static int MeasureDescentScaled(const BitmapFontFace* face, int scalePercent);
	static int MeasureLineHeightScaled(const BitmapFontFace* face, int scalePercent);
	static int BaselineOffsetScaled(const BitmapFontFace* face, int scalePercent);

#if defined(_WIN32) && !defined(GXOS_BARE_METAL)
	static void DrawText(HDC dc, int x, int y, const char* str, int len, COLORREF color,
						 const BitmapFontFace* face);
	static void DrawTextScaled(HDC dc, int x, int y, const char* str, int len, COLORREF color,
						 const BitmapFontFace* face, int scalePercent);
	static void DrawText(HDC dc, int x, int y, const char* str, int len, COLORREF color,
						 FontRole role = FontRole::Default);
#endif

	static void DrawTextToBuffer(uint32_t* pixels, int pitch, int bufW, int bufH,
								 int x, int y, const char* str, int len, uint32_t color,
								 const BitmapFontFace* face);
	static void DrawTextToBufferScaled(uint32_t* pixels, int pitch, int bufW, int bufH,
								 int x, int y, const char* str, int len, uint32_t color,
								 const BitmapFontFace* face, int scalePercent);
	static void DrawTextToBuffer(uint32_t* pixels, int pitch, int bufW, int bufH,
								 int x, int y, const char* str, int len, uint32_t color,
								 FontRole role = FontRole::Default);
};

} // namespace gui
} // namespace gxos
