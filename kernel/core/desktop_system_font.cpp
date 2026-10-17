#include "include/kernel/desktop_font.h"
#include "include/kernel/framebuffer.h"
#include "include/kernel/font_contract.h"
#include "include/kernel/font_text_layout.h"
#include "system_font_selection_internal.h"

namespace kernel {
namespace desktop {

namespace {

DesktopShellTypographyStatistics s_shellTypographyStatistics = {};

void observeSelectionGeneration(uint32_t generation)
{
	if (generation == 0u) return;
	if (s_shellTypographyStatistics.observedSelectionGeneration != 0u &&
		s_shellTypographyStatistics.observedSelectionGeneration != generation &&
		s_shellTypographyStatistics.generationChangesObserved != 0xffffffffu)
		++s_shellTypographyStatistics.generationChangesObserved;
	s_shellTypographyStatistics.observedSelectionGeneration = generation;
}

uint32_t roleMaximumPixelSize(DesktopSystemTextRole role)
{
	switch (role) {
	case DesktopSystemTextRole::WindowTitle: return 13u;
	case DesktopSystemTextRole::TaskbarLabel: return 12u;
	case DesktopSystemTextRole::StartMenuLabel: return 12u;
	case DesktopSystemTextRole::Default:
	default: return 48u;
	}
}

bool activeSystemTextState(DesktopSystemTextRole role,
	gxos::gui::FontHandle* handle, gxos::gui::FontSize* size)
{
	if (!handle || !size) return false;
	uint32_t generation = 0u;
	const bool active = gxos::gui::internal::systemFontSelectionGetRenderState(
		handle, size, &generation);
	observeSelectionGeneration(generation);
	if (!active) return false;
	const uint32_t maximum = roleMaximumPixelSize(role);
	if (size->pixelHeight > maximum) size->pixelHeight = maximum;
	return size->pixelHeight != 0u;
}

bool successfulFontStatus(gxos::gui::FontStatus status)
{
	return status == gxos::gui::FontStatus::Ok ||
		status == gxos::gui::FontStatus::FallbackUsed;
}

bool framebufferTextTarget(uint32_t** outTarget, uint32_t* outWidth,
	uint32_t* outHeight, uint32_t* outPitch)
{
	if (!outTarget || !outWidth || !outHeight || !outPitch ||
		!framebuffer::is_available() || framebuffer::get_bpp() != 32u) return false;
	uint32_t* target = framebuffer::is_double_buffered()
		? framebuffer::get_back_buffer() : framebuffer::get_buffer();
	const uint32_t width = framebuffer::get_width();
	const uint32_t height = framebuffer::get_height();
	const uint64_t tightPitch = static_cast<uint64_t>(width) * sizeof(uint32_t);
	const uint32_t pitch = framebuffer::is_double_buffered() && tightPitch <= 0xffffffffu
		? static_cast<uint32_t>(tightPitch) : framebuffer::get_pitch();
	if (!target || width == 0u || width > 4096u || height == 0u || height > 4096u ||
		tightPitch > 0xffffffffu || pitch < tightPitch || (pitch & 3u) != 0u) return false;
	*outTarget = target;
	*outWidth = width;
	*outHeight = height;
	*outPitch = pitch;
	return true;
}

bool drawScalableTextInRect(uint32_t x, uint32_t y, uint32_t boxWidth,
	uint32_t boxHeight, const char* str, uint32_t color,
	DesktopSystemTextRole role, DesktopTextAlignment alignment)
{
	if (!str || boxWidth == 0u || boxHeight == 0u) return false;
	uint32_t* target = nullptr;
	uint32_t width = 0u;
	uint32_t height = 0u;
	uint32_t pitch = 0u;
	if (!framebufferTextTarget(&target, &width, &height, &pitch) ||
		static_cast<uint64_t>(x) + boxWidth > width ||
		static_cast<uint64_t>(y) + boxHeight > height) return false;

	gxos::gui::FontHandle handle = {0xffffu, 0u, 0u};
	gxos::gui::FontSize size = {0u};
	if (!activeSystemTextState(role, &handle, &size)) return false;
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
	uint64_t originX = static_cast<uint64_t>(x) +
		static_cast<uint32_t>(layout.originOffsetXPx);
	if (alignment == DesktopTextAlignment::Center && layout.inkWidthPx < boxWidth)
		originX += (boxWidth - layout.inkWidthPx) / 2u;
	const uint64_t baseline = static_cast<uint64_t>(y) +
		(boxHeight - layout.lineHeightPx) / 2u + layout.baselineFromTopPx;
	const uint64_t destinationBytes = static_cast<uint64_t>(pitch) * height;
	if (baseline > 0x7fffffffu || originX > 0x7fffffffu ||
		destinationBytes > static_cast<uint64_t>(static_cast<size_t>(-1))) return false;
	const gxos::gui::FontStatus drawStatus = gxos::gui::FontDrawGrayscaleTextToBuffer(
		handle, size, target, static_cast<size_t>(destinationBytes), pitch,
		width, height, static_cast<int32_t>(originX), static_cast<int32_t>(baseline),
		fitted, layout.outputByteLength, color, nullptr);
	return successfulFontStatus(drawStatus);
}

} // namespace

bool measure_system_text(const char* str, DesktopSystemTextMetrics* outMetrics)
{
	if (!str || !outMetrics) return false;
	*outMetrics = {};
	gxos::gui::FontHandle handle = {0xffffu, 0u, 0u};
	gxos::gui::FontSize size = {0u};
	if (!activeSystemTextState(DesktopSystemTextRole::Default, &handle, &size)) return false;
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
	if (!str || boxWidth == 0u || boxHeight == 0u) return false;
	return drawScalableTextInRect(x, y, boxWidth, boxHeight, str, color,
		DesktopSystemTextRole::Default, DesktopTextAlignment::Left);
}

bool draw_shell_text_in_rect(uint32_t x, uint32_t y, uint32_t width,
	uint32_t height, const char* str, uint32_t color,
	DesktopSystemTextRole role, DesktopTextAlignment alignment)
{
	if (s_shellTypographyStatistics.textDrawRequests != 0xffffffffffffffffull)
		++s_shellTypographyStatistics.textDrawRequests;
	if (drawScalableTextInRect(x, y, width, height, str, color, role, alignment)) {
		if (s_shellTypographyStatistics.scalableDraws != 0xffffffffffffffffull)
			++s_shellTypographyStatistics.scalableDraws;
		return true;
	}
	if (draw_bitmap_text_in_rect(x, y, width, height, str, color, alignment)) {
		if (s_shellTypographyStatistics.bitmapFallbackDraws != 0xffffffffffffffffull)
			++s_shellTypographyStatistics.bitmapFallbackDraws;
		return true;
	}
	if (s_shellTypographyStatistics.bitmapFallbackFailures != 0xffffffffffffffffull)
		++s_shellTypographyStatistics.bitmapFallbackFailures;
	return false;
}

bool measure_system_text_for_role(const char* str, DesktopSystemTextRole role,
	DesktopSystemTextMetrics* outMetrics)
{
	if (!str || !outMetrics) return false;
	*outMetrics = {};
	gxos::gui::FontHandle handle = {0xffffu, 0u, 0u};
	gxos::gui::FontSize size = {0u};
	if (!activeSystemTextState(role, &handle, &size)) return false;
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

void desktop_shell_typography_get_statistics(
	DesktopShellTypographyStatistics* outStatistics)
{
	if (outStatistics) *outStatistics = s_shellTypographyStatistics;
}

void desktop_shell_typography_reset_statistics()
{
	s_shellTypographyStatistics = {};
}

void desktop_shell_typography_record_frame()
{
	if (s_shellTypographyStatistics.shellFrames != 0xffffffffffffffffull)
		++s_shellTypographyStatistics.shellFrames;
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
