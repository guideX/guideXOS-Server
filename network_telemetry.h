#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace gxos {
namespace net {

struct NetworkTelemetrySnapshot {
    bool available = false;
    std::string source = "N/A";
    uint64_t sampleWindowMs = 0;
    uint64_t bytesSentTotal = 0;
    uint64_t bytesReceivedTotal = 0;
    uint64_t sendKBps = 0;
    uint64_t receiveKBps = 0;
    bool utilizationPctAvailable = false;
    int utilizationPct = 0;
};

namespace detail {
    struct NetworkTelemetryState {
        std::mutex mutex;
        bool active = false;
        std::string source = "N/A";
        uint64_t startMicros = 0;
        uint64_t lastSampleMicros = 0;
        uint64_t totalSentBytes = 0;
        uint64_t totalReceivedBytes = 0;
        uint64_t lastSampleSentBytes = 0;
        uint64_t lastSampleReceivedBytes = 0;
    };

    inline uint64_t steadyClockMicros()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    inline NetworkTelemetryState& state()
    {
        static NetworkTelemetryState s_state;
        return s_state;
    }

    inline uint64_t elapsedMicrosToWindowMs(uint64_t elapsedMicros)
    {
        return elapsedMicros == 0 ? 1 : std::max<uint64_t>(1, (elapsedMicros + 999ULL) / 1000ULL);
    }

    inline uint64_t bytesPerSecondToKbps(uint64_t bytes, uint64_t elapsedMicros)
    {
        if (elapsedMicros == 0) return 0;
        return (bytes * 1000000ULL) / (1024ULL * elapsedMicros);
    }
}

inline void armNetworkTelemetry(const std::string& source = "hostedSocketCounters")
{
    auto& st = detail::state();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (!st.active) {
        const uint64_t now = detail::steadyClockMicros();
        st.active = true;
        st.source = source.empty() ? "hostedSocketCounters" : source;
        st.startMicros = now;
        st.lastSampleMicros = now;
        st.totalSentBytes = 0;
        st.totalReceivedBytes = 0;
        st.lastSampleSentBytes = 0;
        st.lastSampleReceivedBytes = 0;
        return;
    }
    if (!source.empty() && st.source == "N/A") {
        st.source = source;
    }
}

inline void recordNetworkBytesSent(uint64_t bytes)
{
    if (bytes == 0) return;
    auto& st = detail::state();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (!st.active) {
        const uint64_t now = detail::steadyClockMicros();
        st.active = true;
        st.source = "hostedSocketCounters";
        st.startMicros = now;
        st.lastSampleMicros = now;
    }
    st.totalSentBytes += bytes;
}

inline void recordNetworkBytesReceived(uint64_t bytes)
{
    if (bytes == 0) return;
    auto& st = detail::state();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (!st.active) {
        const uint64_t now = detail::steadyClockMicros();
        st.active = true;
        st.source = "hostedSocketCounters";
        st.startMicros = now;
        st.lastSampleMicros = now;
    }
    st.totalReceivedBytes += bytes;
}

inline NetworkTelemetrySnapshot networkTelemetrySnapshot()
{
    auto& st = detail::state();
    std::lock_guard<std::mutex> lock(st.mutex);

    NetworkTelemetrySnapshot snapshot;
    snapshot.available = st.active;
    snapshot.source = st.active ? st.source : "N/A";
    snapshot.bytesSentTotal = st.totalSentBytes;
    snapshot.bytesReceivedTotal = st.totalReceivedBytes;

    if (!st.active) {
        return snapshot;
    }

    const uint64_t now = detail::steadyClockMicros();
    const uint64_t elapsedMicros = now > st.lastSampleMicros ? now - st.lastSampleMicros : 0;
    const uint64_t windowMs = detail::elapsedMicrosToWindowMs(elapsedMicros);
    snapshot.sampleWindowMs = windowMs;
    snapshot.sendKBps = detail::bytesPerSecondToKbps(st.totalSentBytes - st.lastSampleSentBytes, elapsedMicros);
    snapshot.receiveKBps = detail::bytesPerSecondToKbps(st.totalReceivedBytes - st.lastSampleReceivedBytes, elapsedMicros);
    snapshot.utilizationPctAvailable = false;
    snapshot.utilizationPct = 0;

    st.lastSampleMicros = now;
    st.lastSampleSentBytes = st.totalSentBytes;
    st.lastSampleReceivedBytes = st.totalReceivedBytes;

    return snapshot;
}

} // namespace net
} // namespace gxos
