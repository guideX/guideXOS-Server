#pragma once

#include <stdint.h>

namespace kernel {
namespace gui_render {

struct CenteredClientWindowBounds {
    uint32_t x;
    uint32_t y;
    uint32_t clientWidth;
    uint32_t clientHeight;
    uint32_t outerWidth;
    uint32_t outerHeight;
};

// Keep the requested dimensions in client-area coordinates. The compositor
// adds the title bar to the outer frame and the backing surface uses only the
// granted client dimensions.
inline bool centered_client_window_bounds(
    int32_t requestedClientWidth, int32_t requestedClientHeight,
    uint32_t screenWidth, uint32_t screenHeight,
    uint32_t workX, uint32_t workY, uint32_t workWidth, uint32_t workHeight,
    uint32_t titlebarHeight, uint32_t maxClientWidth, uint32_t maxClientHeight,
    CenteredClientWindowBounds* out)
{
    if (!out) return false;
    *out = {};
    if (requestedClientWidth <= 0 || requestedClientHeight <= 0 ||
        static_cast<uint32_t>(requestedClientWidth) > maxClientWidth ||
        static_cast<uint32_t>(requestedClientHeight) > maxClientHeight ||
        screenWidth == 0 || screenHeight == 0 || workWidth == 0 || workHeight == 0 ||
        workX > screenWidth || workY > screenHeight ||
        workWidth > screenWidth - workX || workHeight > screenHeight - workY ||
        titlebarHeight >= workHeight) return false;

    const uint32_t clientWidth =
        static_cast<uint32_t>(requestedClientWidth) < workWidth
            ? static_cast<uint32_t>(requestedClientWidth) : workWidth;
    const uint32_t availableClientHeight = workHeight - titlebarHeight;
    const uint32_t clientHeight =
        static_cast<uint32_t>(requestedClientHeight) < availableClientHeight
            ? static_cast<uint32_t>(requestedClientHeight) : availableClientHeight;
    if (clientWidth == 0 || clientHeight == 0) return false;

    const uint32_t outerHeight = clientHeight + titlebarHeight;
    const uint32_t x = workX + (workWidth - clientWidth) / 2u;
    const uint32_t y = workY + (workHeight - outerHeight) / 2u;
    if (x > static_cast<uint32_t>(INT32_MAX) || y > static_cast<uint32_t>(INT32_MAX)) return false;

    out->x = x;
    out->y = y;
    out->clientWidth = clientWidth;
    out->clientHeight = clientHeight;
    out->outerWidth = clientWidth;
    out->outerHeight = outerHeight;
    return true;
}

} // namespace gui_render
} // namespace kernel
