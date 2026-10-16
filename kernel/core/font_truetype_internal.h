#pragma once

#include "include/kernel/font_contract.h"
#include "include/kernel/font_truetype.h"

namespace gxos {
namespace gui {

// Private borrowed view into a FONT3-owned FontSourceRecord. The view itself
// lives in that record and remains valid only while its runtime handle resolves.
struct FontTrueTypeSourceView {
	const uint8_t* bytes;
	uint32_t byteLength;
	const FontTrueTypeMetadata* metadata;
};

extern const FontBackend kFontTrueTypeBackend;

} // namespace gui
} // namespace gxos
