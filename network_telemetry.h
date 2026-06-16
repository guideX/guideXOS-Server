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
    bool ratesAvailable = false;
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
        uint64_t totalSentBytes = 0;
        uint64_t totalReceivedBytes = 0;
        uint64_t windowStartMicros = 0;
        uint64_t windowStartSentBytes = 0;
        uint64_t windowStartReceivedBytes = 0;
        bool hasStableSample = false;
        uint64_t stableWindowMs = 0;
        uint64_t stableSendKBps = 0;
        uint64_t stableReceiveKBps = 0;
    };

    constexpr uint64_t kNetworkTelemetryDisplayWindowMicros = 750ULL * 1000ULL;

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

    inline std::string warmupSourceName(const std::string& source)
    {
        return source.empty() ? std::string("hostedSocketCountersWarmup") : source + "Warmup";
    }

    inline void resetNetworkWindow(NetworkTelemetryState& st, uint64_t now)
    {
        st.windowStartMicros = now;
        st.windowStartSentBytes = st.totalSentBytes;
        st.windowStartReceivedBytes = st.totalReceivedBytes;
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
        st.totalSentBytes = 0;
        st.totalReceivedBytes = 0;
        st.hasStableSample = false;
        st.stableWindowMs = 0;
        st.stableSendKBps = 0;
        st.stableReceiveKBps = 0;
        detail::resetNetworkWindow(st, now);
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
        st.hasStableSample = false;
        st.stableWindowMs = 0;
        st.stableSendKBps = 0;
        st.stableReceiveKBps = 0;
        detail::resetNetworkWindow(st, now);
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
        st.hasStableSample = false;
        st.stableWindowMs = 0;
        st.stableSendKBps = 0;
        st.stableReceiveKBps = 0;
        detail::resetNetworkWindow(st, now);
    }
    st.totalReceivedBytes += bytes;
}

inline NetworkTelemetrySnapshot networkTelemetrySnapshot()
{
    auto& st = detail::state();
    std::lock_guard<std::mutex> lock(st.mutex);

    NetworkTelemetrySnapshot snapshot;
    snapshot.available = st.active;
    snapshot.bytesSentTotal = st.totalSentBytes;
    snapshot.bytesReceivedTotal = st.totalReceivedBytes;

    if (!st.active) {
        return snapshot;
    }

    const uint64_t now = detail::steadyClockMicros();
    const uint64_t elapsedMicros = now > st.windowStartMicros ? now - st.windowStartMicros : 0;
    const uint64_t windowMs = detail::elapsedMicrosToWindowMs(elapsedMicros);
    const uint64_t windowSentBytes = st.totalSentBytes - st.windowStartSentBytes;
    const uint64_t windowReceivedBytes = st.totalReceivedBytes - st.windowStartReceivedBytes;

    if (elapsedMicros >= detail::kNetworkTelemetryDisplayWindowMicros) {
        st.hasStableSample = true;
        st.stableWindowMs = windowMs;
        st.stableSendKBps = detail::bytesPerSecondToKbps(windowSentBytes, elapsedMicros);
        st.stableReceiveKBps = detail::bytesPerSecondToKbps(windowReceivedBytes, elapsedMicros);
        detail::resetNetworkWindow(st, now);
    }

    snapshot.source = st.hasStableSample ? st.source : detail::warmupSourceName(st.source);
    snapshot.ratesAvailable = st.hasStableSample;
    snapshot.sampleWindowMs = st.hasStableSample ? st.stableWindowMs : windowMs;
    snapshot.sendKBps = st.hasStableSample ? st.stableSendKBps : 0;
    snapshot.receiveKBps = st.hasStableSample ? st.stableReceiveKBps : 0;
    snapshot.utilizationPctAvailable = false;
    snapshot.utilizationPct = 0;

    return snapshot;
}

} // namespace net
} // namespace gxos
