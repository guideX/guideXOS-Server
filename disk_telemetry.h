#pragma once

#include <cstdint>

namespace gxos {

struct DiskTelemetrySnapshot {
    bool available = false;
    const char* source = "N/A";
    uint64_t sampleWindowMs = 0;
    uint64_t readBytesTotal = 0;
    uint64_t writeBytesTotal = 0;
    uint64_t readKBps = 0;
    uint64_t writeKBps = 0;
    bool activePctAvailable = false;
    int activePct = 0;
};

} // namespace gxos
