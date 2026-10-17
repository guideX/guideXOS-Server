#pragma once

#if defined(GXOS_BARE_METAL)
#include "types.h"
#else
#include <cstddef>
#include <cstdint>
#endif

#include "font_contract.h"

namespace gxos {
namespace gui {

enum class FontTextOverflowPolicy : uint8_t {
	Clip = 0,
	Ellipsis = 1,
};

// Bounded one-line layout over the same glyph lookup and UTF-8 contract used
// by the renderer. inkLeftPx may be negative; originOffsetXPx moves the pen
// right far enough to keep the complete ink box inside the available width.
struct FontTextLineLayout {
	uint32_t pixelSize;
	uint32_t ascentPx;
	uint32_t descentPx;
	uint32_t lineHeightPx;
	uint32_t baselineFromTopPx;
	uint32_t advancePx;
	uint32_t inkWidthPx;
	uint32_t inkAscentPx;
	uint32_t inkDescentPx;
	uint32_t outputByteLength;
	uint32_t sourceByteLength;
	int32_t inkLeftPx;
	int32_t inkRightPx;
	int32_t originOffsetXPx;
	uint8_t clipped;
	uint8_t truncated;
	uint8_t fallbackUsed;
	uint8_t reserved;
};

// Measures one bounded UTF-8 line and optionally copies its visual form into
// output. Ellipsis uses three ASCII periods, so it works with the current
// glyph coverage without requiring a shaping engine or U+2026 support.
FontStatus FontTextLayoutSingleLine(FontHandle font, FontSize size,
	const char* utf8, size_t byteLength, uint32_t availableWidthPx,
	FontTextOverflowPolicy overflowPolicy, char* output, size_t outputCapacity,
	FontTextLineLayout* outLayout);

} // namespace gui
} // namespace gxos
