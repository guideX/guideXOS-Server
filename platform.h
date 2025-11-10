#pragma once
#include <cstdint>
namespace gxos {
    struct PlatformInfo {
        uint32_t cpuCount; uint64_t totalMemBytes; uint64_t startTicks;
    };
    PlatformInfo queryPlatform();
    uint64_t ticks();
}
