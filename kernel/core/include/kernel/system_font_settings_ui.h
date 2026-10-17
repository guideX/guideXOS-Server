#pragma once

#include "system_font_selection_types.h"

namespace kernel {
namespace apps {
namespace font_settings {

using gxos::gui::SystemFontCandidate;
using gxos::gui::SystemFontFaceInfo;
using gxos::gui::SystemFontIdentity;
using gxos::gui::SystemFontSelectionSnapshot;
using gxos::gui::SystemFontSelectionStatus;

SystemFontSelectionStatus SystemFontSelectionRefreshCatalog();
SystemFontSelectionStatus SystemFontSelectionGetActive(SystemFontSelectionSnapshot* output);
SystemFontSelectionStatus SystemFontSelectionGetAvailable(SystemFontFaceInfo* output,
	uint32_t capacity, uint32_t* outCount);
SystemFontSelectionStatus SystemFontSelectionPrepare(const SystemFontIdentity* identity,
	uint32_t pixelSize, SystemFontCandidate* output);
SystemFontSelectionStatus SystemFontSelectionApply(const SystemFontCandidate* candidate);
SystemFontSelectionStatus SystemFontSelectionResetToBuiltin();
SystemFontSelectionStatus SystemFontSelectionDrawPreview(const SystemFontIdentity* identity,
	uint32_t pixelSize, uint32_t* pixels, size_t destinationBytes,
	uint32_t pitchBytes, uint32_t bufferWidth, uint32_t bufferHeight,
	uint32_t clipX, uint32_t clipY, uint32_t clipWidth, uint32_t clipHeight,
	int32_t x, int32_t topY, const char* utf8, size_t byteLength, uint32_t color);
const char* SystemFontSelectionStatusName(SystemFontSelectionStatus status);

} // namespace font_settings
} // namespace apps
} // namespace kernel
