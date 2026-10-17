#include "include/kernel/desktop_font.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/font_contract.h"
#include "system_font_selection_internal.h"

namespace kernel {
namespace desktop {

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
