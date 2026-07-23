#include "guidexos_gc_platform_services.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace guidexos {
namespace nativeaot {
namespace gcservices {
namespace {

using Clock = std::chrono::steady_clock;

} // namespace

std::uint64_t currentThreadIdForLogging() {
#if defined(GXOS_BARE_METAL)
    return 1;
#else
    const std::uint64_t value = static_cast<std::uint64_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    return value == 0 ? 1 : value;
#endif
}

std::uint32_t currentProcessId() {
    // guideXOS is a single-process runtime in this selected configuration.
    return 1;
}

std::uint32_t currentProcessorNumber() {
    return 0;
}

bool setCurrentThreadIdealAffinity(std::uint16_t source,
                                   std::uint16_t destination) {
    return source == 0 && destination == 0;
}

bool getCurrentThreadIdealProcessor(std::uint16_t* processor) {
    if (processor == nullptr) return false;
    *processor = 0;
    return true;
}

bool setThreadAffinity(std::uint16_t processor) {
    return processor == 0;
}

bool boostThreadPriority() {
    // Thread priorities are not part of the one-CPU guideXOS scheduler.
    return false;
}

void sleepMilliseconds(std::uint32_t milliseconds) {
#if defined(GXOS_BARE_METAL)
    (void)milliseconds;
    std::atomic_signal_fence(std::memory_order_seq_cst);
#else
    if (milliseconds != 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
#endif
}

void yieldThread() {
#if defined(GXOS_BARE_METAL)
    std::atomic_signal_fence(std::memory_order_seq_cst);
#else
    std::this_thread::yield();
#endif
}

std::int64_t performanceCounter() {
#if defined(GXOS_BARE_METAL)
    static std::int64_t counter = 0;
    return ++counter;
#else
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch()).count());
#endif
}

std::int64_t performanceFrequency() {
#if defined(GXOS_BARE_METAL)
    return 1000000000;
#else
    return 1000000000;
#endif
}

std::uint64_t lowPrecisionTimestamp() {
#if defined(GXOS_BARE_METAL)
    static std::atomic<std::uint64_t> ticks{0};
    return ++ticks;
#else
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch()).count());
#endif
}

void flushProcessWriteBuffers() {
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

std::uint32_t totalProcessorCount() {
    return 1;
}

} // namespace gcservices
} // namespace nativeaot
} // namespace guidexos
