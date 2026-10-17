#include "include/kernel/desktop_font.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/font_contract.h"
#include "include/kernel/font_text_layout.h"
#include "system_font_selection_internal.h"

namespace kernel {
namespace desktop {

namespace {

bool activeSystemTextState(gxos::gui::FontHandle* handle,
	gxos::gui::FontSize* size)
{
	return handle && size && gxos::gui::internal::systemFontSelectionGetRenderState(
		handle, size, nullptr);
}

bool successfulFontStatus(gxos::gui::FontStatus status)
{
	return status == gxos::gui::FontStatus::Ok ||
		status == gxos::gui::FontStatus::FallbackUsed;
}

} // namespace

bool measure_system_text(const char* str, DesktopSystemTextMetrics* outMetrics)
{
	if (!str || !outMetrics) return false;
	*outMetrics = {};
	gxos::gui::FontHandle handle = {0xffffu, 0u, 0u};
	gxos::gui::FontSize size = {0u};
	if (!activeSystemTextState(&handle, &size)) return false;
	size_t length = 0u;
	if (gxos::gui::FontUtf8CStringLength(str, &length) != gxos::gui::FontStatus::Ok)
		return false;
	char output[256] = {};
	gxos::gui::FontTextLineLayout layout = {};
	const gxos::gui::FontStatus status = gxos::gui::FontTextLayoutSingleLine(
		handle, size, str, length, 4096u, gxos::gui::FontTextOverflowPolicy::Clip,
		output, sizeof(output), &layout);
	if (!successfulFontStatus(status)) return false;
	outMetrics->lineHeightPx = layout.lineHeightPx;
	outMetrics->ascentPx = layout.ascentPx;
	outMetrics->descentPx = layout.descentPx;
	outMetrics->pixelSize = layout.pixelSize;
	return true;
}

bool draw_system_text_in_rect(uint32_t x, uint32_t y, uint32_t boxWidth,
	uint32_t boxHeight, const char* str, uint32_t color)
{
	if (!str || boxWidth == 0u || boxHeight == 0u || !framebuffer::is_available() ||
		framebuffer::get_bpp() != 32u) return false;
	uint32_t* target = framebuffer::is_double_buffered()
		? framebuffer::get_back_buffer() : framebuffer::get_buffer();
	const uint32_t width = framebuffer::get_width();
	const uint32_t height = framebuffer::get_height();
	const uint64_t tightPitch = static_cast<uint64_t>(width) * sizeof(uint32_t);
	const uint32_t pitch = framebuffer::is_double_buffered() && tightPitch <= 0xffffffffu
		? static_cast<uint32_t>(tightPitch) : framebuffer::get_pitch();
	if (!target || width == 0u || width > 4096u || height == 0u || height > 4096u ||
		tightPitch > 0xffffffffu || pitch < tightPitch || (pitch & 3u) != 0u ||
		static_cast<uint64_t>(x) + boxWidth > width ||
		static_cast<uint64_t>(y) + boxHeight > height)
		return false;

	gxos::gui::FontHandle handle = {0xffffu, 0u, 0u};
	gxos::gui::FontSize size = {0u};
	if (!activeSystemTextState(&handle, &size)) return false;
	size_t sourceLength = 0u;
	if (gxos::gui::FontUtf8CStringLength(str, &sourceLength) != gxos::gui::FontStatus::Ok)
		return false;
	char fitted[256] = {};
	gxos::gui::FontTextLineLayout layout = {};
	const gxos::gui::FontStatus layoutStatus = gxos::gui::FontTextLayoutSingleLine(
		handle, size, str, sourceLength, boxWidth,
		gxos::gui::FontTextOverflowPolicy::Ellipsis, fitted, sizeof(fitted), &layout);
	if (!successfulFontStatus(layoutStatus) || layout.lineHeightPx > boxHeight)
		return false;
	const uint64_t baseline = static_cast<uint64_t>(y) +
		(boxHeight - layout.lineHeightPx) / 2u + layout.baselineFromTopPx;
	const uint64_t originX = static_cast<uint64_t>(x) +
		static_cast<uint32_t>(layout.originOffsetXPx);
	const uint64_t destinationBytes = static_cast<uint64_t>(pitch) * height;
	if (baseline > 0x7fffffffu || originX > 0x7fffffffu ||
		destinationBytes > static_cast<uint64_t>(static_cast<size_t>(-1)))
		return false;
	const gxos::gui::FontStatus drawStatus = gxos::gui::FontDrawGrayscaleTextToBuffer(
		handle, size, target, static_cast<size_t>(destinationBytes), pitch,
		width, height, static_cast<int32_t>(originX), static_cast<int32_t>(baseline),
		fitted, layout.outputByteLength, color, nullptr);
	return successfulFontStatus(drawStatus);
}

void draw_system_text(uint32_t x, uint32_t y, const char* str, uint32_t color)
{
	if (str && x <= 0x7fffffffu && y <= 0x7fffffffu && framebuffer::is_available() &&
		framebuffer::get_bpp() == 32) {
		uint32_t* target = framebuffer::is_double_buffered()
			? framebuffer::get_back_buffer() : framebuffer::get_buffer();
		const uint32_t width = framebuffer::get_width();
		const uint32_t height = framebuffer::get_height();
		const uint64_t tightPitch = static_cast<uint64_t>(width) * sizeof(uint32_t);
		const uint32_t pitch = framebuffer::is_double_buffered() && tightPitch <= 0xffffffffu
			? static_cast<uint32_t>(tightPitch) : framebuffer::get_pitch();
		gxos::gui::FontHandle handle = {0xffffu, 0u, 0u};
		gxos::gui::FontSize size = {0u};
		if (target && width != 0u && width <= 4096u && height != 0u && height <= 4096u &&
			tightPitch <= 0xffffffffu && static_cast<uint64_t>(pitch) >= tightPitch &&
			gxos::gui::internal::systemFontSelectionGetRenderState(&handle, &size, nullptr)) {
			gxos::gui::FontMetrics metrics = {};
			size_t textBytes = 0u;
			if (gxos::gui::FontGetMetrics(handle, size, &metrics) == gxos::gui::FontStatus::Ok &&
				gxos::gui::FontUtf8CStringLength(str, &textBytes) == gxos::gui::FontStatus::Ok &&
				metrics.ascentPx <= static_cast<uint32_t>(0x7fffffffu - y)) {
				const uint64_t destinationBytes = static_cast<uint64_t>(pitch) * height;
				if (destinationBytes <= static_cast<uint64_t>(static_cast<size_t>(-1))) {
					const gxos::gui::FontStatus status = gxos::gui::FontDrawGrayscaleTextToBuffer(
						handle, size, target, static_cast<size_t>(destinationBytes), pitch,
						width, height, static_cast<int32_t>(x),
						static_cast<int32_t>(y + metrics.ascentPx), str, textBytes,
						color, nullptr);
					if (status == gxos::gui::FontStatus::Ok ||
						status == gxos::gui::FontStatus::FallbackUsed) return;
				}
			}
		}
	}
	draw_text(x, y, str, color, 1);
}

} // namespace desktop
} // namespace kernel
