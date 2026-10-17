#include "kernel/core/include/kernel/system_font.h"

#include <cstdio>
#include <cstring>

namespace test_font10 {
void recordBitmapDraw(const char* text, int length, int x, int y);
}

namespace gxos {
namespace gui {
namespace {

BitmapFontFace s_face = {};

int textLength(const char* text, int length)
{
	if (!text) return 0;
	if (length >= 0) return length;
	int result = 0;
	while (text[result]) ++result;
	return result;
}

} // namespace

const BitmapFontFace* SystemFont::GetFace(FontRole)
{
	return &s_face;
}

int SystemFont::MeasureWidth(const BitmapFontFace*, const char* text, int length)
{
	return textLength(text, length) * 6;
}

int SystemFont::MeasureWidth(FontRole role, const char* text, int length)
{
	return MeasureWidth(GetFace(role), text, length);
}

int SystemFont::MeasureLineHeight(const BitmapFontFace*)
{
	return 8;
}

int SystemFont::MeasureHeight(FontRole)
{
	return 8;
}

void SystemFont::DrawTextToBuffer(uint32_t*, int, int, int, int x, int y,
	const char* text, int length, uint32_t, const BitmapFontFace*)
{
	test_font10::recordBitmapDraw(text, textLength(text, length), x, y);
}

void SystemFont::DrawTextToBuffer(uint32_t* pixels, int pitch, int width, int height,
	int x, int y, const char* text, int length, uint32_t color, FontRole role)
{
	DrawTextToBuffer(pixels, pitch, width, height, x, y, text, length, color, GetFace(role));
}

} // namespace gui
} // namespace gxos
