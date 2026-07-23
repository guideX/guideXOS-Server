#pragma once

#include <cstdint>

namespace guidexos {
namespace nativeaot {
namespace gcservices {

// The locked experiment is deliberately one-node/one-heap.  These helpers
// are the small platform seam used by the gcenv replacement; they do not
// expose Win32 types or provide compatibility exports.
std::uint64_t currentThreadIdForLogging();
std::uint32_t currentProcessId();
std::uint32_t currentProcessorNumber();
bool setCurrentThreadIdealAffinity(std::uint16_t source, std::uint16_t destination);
bool getCurrentThreadIdealProcessor(std::uint16_t* processor);
bool setThreadAffinity(std::uint16_t processor);
bool boostThreadPriority();
void sleepMilliseconds(std::uint32_t milliseconds);
void yieldThread();
std::int64_t performanceCounter();
std::int64_t performanceFrequency();
std::uint64_t lowPrecisionTimestamp();
void flushProcessWriteBuffers();
std::uint32_t totalProcessorCount();

} // namespace gcservices
} // namespace nativeaot
} // namespace guidexos
