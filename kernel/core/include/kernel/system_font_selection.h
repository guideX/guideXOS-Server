#pragma once

#if defined(GXOS_BARE_METAL)
#include "types.h"
#else
#include <cstddef>
#include <cstdint>
#endif

#include "font_contract.h"
#include "system_font_selection_types.h"

namespace gxos {
namespace gui {

// Initializes the existing fixed-capacity font manager, scans only direct
// children of /system/fonts, and restores one validated preference. Missing
// storage or fonts leave the built-in bitmap face active and do not fail boot.
SystemFontSelectionStatus SystemFontSelectionInitialize();
void SystemFontSelectionShutdown();
SystemFontSelectionStatus SystemFontSelectionRefreshCatalog();
SystemFontSelectionStatus SystemFontSelectionGetActive(
	SystemFontSelectionSnapshot* outSnapshot);
SystemFontSelectionStatus SystemFontSelectionGetConfigured(
	SystemFontConfiguredPreference* outPreference);
SystemFontSelectionStatus SystemFontSelectionGetAvailable(
	SystemFontFaceInfo* output, uint32_t capacity, uint32_t* outCount);
SystemFontSelectionStatus SystemFontSelectionPrepare(
	const SystemFontIdentity* identity, uint32_t pixelSize,
	SystemFontCandidate* outCandidate);
SystemFontSelectionStatus SystemFontSelectionPrepareActiveSize(
	uint32_t pixelSize, SystemFontCandidate* outCandidate);
// Draws a preview for an enumerated identity into a caller-owned XRGB8888
// surface. The explicit clip rectangle is intersected with the surface. This
// uses the existing FONT3/FONT4 glyph path without touching the shared FONT5
// cache or changing the active/default selection. Built-in previews use the
// real bitmap glyph table and require its native 7-pixel size.
SystemFontSelectionStatus SystemFontSelectionDrawPreview(
	const SystemFontIdentity* identity, uint32_t pixelSize,
	uint32_t* pixels, size_t destinationBytes, uint32_t pitchBytes,
	uint32_t bufferWidth, uint32_t bufferHeight,
	uint32_t clipX, uint32_t clipY, uint32_t clipWidth, uint32_t clipHeight,
	int32_t x, int32_t topY, const char* utf8, size_t byteLength, uint32_t color);
SystemFontSelectionStatus SystemFontSelectionApply(
	const SystemFontCandidate* candidate);
SystemFontSelectionStatus SystemFontSelectionRevert();
SystemFontSelectionStatus SystemFontSelectionResetToBuiltin();
SystemFontSelectionStatus SystemFontSelectionPreferenceStatus();
const char* SystemFontSelectionStatusName(SystemFontSelectionStatus status);

} // namespace gui
} // namespace gxos
