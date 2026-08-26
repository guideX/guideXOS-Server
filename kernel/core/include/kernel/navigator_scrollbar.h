// Bounded scrollbar geometry and mapping helpers for kernel Navigator.

#ifndef KERNEL_NAVIGATOR_SCROLLBAR_H
#define KERNEL_NAVIGATOR_SCROLLBAR_H

#include <stdint.h>

namespace kernel {
namespace navigator_scrollbar {

static const int64_t kMaxCoordinate = 0x7FFFFFFFLL;
static const int kMinimumThumbExtent = 20;

inline int bounded_max_scroll(int64_t contentExtent, int64_t viewportExtent)
{
    if (contentExtent <= 0 || viewportExtent <= 0 || contentExtent <= viewportExtent) return 0;
    const int64_t overflow = contentExtent - viewportExtent;
    return static_cast<int>(overflow > kMaxCoordinate ? kMaxCoordinate : overflow);
}

inline int clamp_scroll(int64_t requested, int maxScroll)
{
    const int legalMax = maxScroll > 0 ? maxScroll : 0;
    if (requested <= 0) return 0;
    if (requested >= legalMax) return legalMax;
    return static_cast<int>(requested);
}

inline int thumb_extent(int64_t viewportExtent, int64_t contentExtent, int trackExtent)
{
    if (viewportExtent <= 0 || contentExtent <= viewportExtent || trackExtent <= 0) {
        return trackExtent > 0 ? trackExtent : 0;
    }
    const int64_t numerator = static_cast<int64_t>(trackExtent) * viewportExtent;
    const int64_t proportional64 = numerator / contentExtent;
    int proportional = proportional64 > 0x7FFFFFFFLL ? 0x7FFFFFFF :
        static_cast<int>(proportional64);
    if (proportional < 1) proportional = 1;
    if (proportional < kMinimumThumbExtent) proportional = kMinimumThumbExtent;
    if (proportional > trackExtent) proportional = trackExtent;
    return proportional;
}

inline int thumb_travel(int trackExtent, int thumbExtent)
{
    if (trackExtent <= 0 || thumbExtent <= 0 || thumbExtent >= trackExtent) return 0;
    return trackExtent - thumbExtent;
}

inline int thumb_offset(int scroll, int maxScroll, int travel)
{
    if (travel <= 0 || maxScroll <= 0) return 0;
    const int legalScroll = clamp_scroll(static_cast<int64_t>(scroll), maxScroll);
    const int64_t scaled = static_cast<int64_t>(travel) * legalScroll;
    return clamp_scroll(scaled / maxScroll, travel);
}

inline int scroll_from_thumb_offset(int pointerThumbOffset, int maxScroll, int travel)
{
    if (travel <= 0 || maxScroll <= 0) return 0;
    const int legalOffset = clamp_scroll(static_cast<int64_t>(pointerThumbOffset), travel);
    const int64_t scaled = static_cast<int64_t>(legalOffset) * maxScroll;
    return clamp_scroll(scaled / travel, maxScroll);
}

inline int page_from_track_click(int current, int page, bool beforeThumb, int maxScroll)
{
    const int64_t delta = beforeThumb ? -static_cast<int64_t>(page)
                                      : static_cast<int64_t>(page);
    return clamp_scroll(static_cast<int64_t>(current) + delta, maxScroll);
}

} // namespace navigator_scrollbar
} // namespace kernel

#endif // KERNEL_NAVIGATOR_SCROLLBAR_H
