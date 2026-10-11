#pragma once

#include <cstdint>
#include <string>

namespace gxos { namespace gui { namespace resizenotice {

inline std::string payload(uint64_t windowId, int width, int height)
{
    return std::to_string(windowId) + "|" + std::to_string(width) + "|" +
        std::to_string(height);
}

} } }
