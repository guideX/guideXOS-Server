#include "include/kernel/system_font_settings_ui.h"
#include "include/kernel/system_font_selection.h"

namespace kernel {
namespace apps {
namespace font_settings {

SystemFontSelectionStatus SystemFontSelectionRefreshCatalog()
{
	return gxos::gui::SystemFontSelectionRefreshCatalog();
}

SystemFontSelectionStatus SystemFontSelectionGetActive(SystemFontSelectionSnapshot* output)
{
	return gxos::gui::SystemFontSelectionGetActive(output);
}

SystemFontSelectionStatus SystemFontSelectionGetAvailable(SystemFontFaceInfo* output,
	uint32_t capacity, uint32_t* outCount)
{
	return gxos::gui::SystemFontSelectionGetAvailable(output, capacity, outCount);
}

SystemFontSelectionStatus SystemFontSelectionPrepare(const SystemFontIdentity* identity,
	uint32_t pixelSize, SystemFontCandidate* output)
{
	return gxos::gui::SystemFontSelectionPrepare(identity, pixelSize, output);
}

SystemFontSelectionStatus SystemFontSelectionApply(const SystemFontCandidate* candidate)
{
	return gxos::gui::SystemFontSelectionApply(candidate);
}

SystemFontSelectionStatus SystemFontSelectionResetToBuiltin()
{
	return gxos::gui::SystemFontSelectionResetToBuiltin();
}

SystemFontSelectionStatus SystemFontSelectionDrawPreview(const SystemFontIdentity* identity,
	uint32_t pixelSize, uint32_t* pixels, size_t destinationBytes,
	uint32_t pitchBytes, uint32_t bufferWidth, uint32_t bufferHeight,
	uint32_t clipX, uint32_t clipY, uint32_t clipWidth, uint32_t clipHeight,
	int32_t x, int32_t topY, const char* utf8, size_t byteLength, uint32_t color)
{
	return gxos::gui::SystemFontSelectionDrawPreview(identity, pixelSize, pixels,
	destinationBytes, pitchBytes, bufferWidth, bufferHeight, clipX, clipY,
	clipWidth, clipHeight, x, topY, utf8, byteLength, color);
}

const char* SystemFontSelectionStatusName(SystemFontSelectionStatus status)
{
	return gxos::gui::SystemFontSelectionStatusName(status);
}

} // namespace font_settings
} // namespace apps
} // namespace kernel
