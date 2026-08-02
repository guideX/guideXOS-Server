// Win64-side implementation of the guideXOS GC adapter names used by the
// locked gcenv object.  The objects on this side are private implementation
// details.  Only fixed-width values and opaque event addresses cross into the
// SysV kernel through guidexos_nativeaot_gc_startup_platform_contract.h.

#include "guidexos_nativeaot_gc_startup_platform_contract.h"
#include "guidexos_nativeaot_critical_section_adapter.h"
#include "guidexos_nativeaot_event_adapter.h"
#include "guidexos_nativeaot_virtual_memory_adapter.h"
#include "../gcenv/guidexos_gc_platform_services.h"

#include <stdint.h>
#include <new>
#include <intrin.h>

extern "C" uint32_t g_guidexos_nativeaot_gc_startup_stage = 0;
extern "C" uint32_t g_guidexos_nativeaot_gc_startup_reserve_pages = 0;
extern "C" uint32_t g_guidexos_nativeaot_gc_startup_reserve_mib = 0;

namespace {

constexpr uint32_t kWaitObject0 = 0u;
constexpr uint32_t kWaitTimeout = 258u;
constexpr uint32_t kWaitFailed = 0xFFFFFFFFu;
constexpr uint32_t kInfinite = 0xFFFFFFFFu;
constexpr uint32_t kPageSize = 4096u;
constexpr uint64_t kMemoryLimit = UINT64_C(64) * 1024u * 1024u;

uint64_t currentThreadId() {
    uint64_t id = 0;
    return guidexos_nativeaot_pal_current_thread_id(&id) == 0 ? id : 0;
}

void yield() {
    (void)guidexos_nativeaot_pal_yield();
}

} // namespace

namespace guidexos {
namespace nativeaot {

struct CriticalSectionHandle {
    volatile long held;
    uint64_t owner;
    uint32_t recursion;
};

CriticalSectionHandle* initializeCriticalSection() {
    CriticalSectionHandle* handle = new (std::nothrow) CriticalSectionHandle{};
    if (handle != nullptr) {
        handle->held = 0;
        handle->owner = 0;
        handle->recursion = 0;
    }
    return handle;
}

gxos::runtime::MutexResult enterCriticalSection(CriticalSectionHandle* handle) {
    if (handle == nullptr) return gxos::runtime::MutexResult::Invalid;
    const uint64_t owner = currentThreadId();
    if (owner == 0) return gxos::runtime::MutexResult::Invalid;
    if (handle->owner == owner) {
        if (handle->recursion >= gxos::runtime::kMutexMaximumRecursion) {
            return gxos::runtime::MutexResult::RecursionLimit;
        }
        ++handle->recursion;
        return gxos::runtime::MutexResult::Acquired;
    }
    for (;;) {
        if (_InterlockedCompareExchange(&handle->held, 1, 0) == 0) {
            handle->owner = owner;
            handle->recursion = 1;
            return gxos::runtime::MutexResult::Acquired;
        }
        yield();
    }
}

gxos::runtime::MutexResult tryEnterCriticalSection(CriticalSectionHandle* handle) {
    if (handle == nullptr) return gxos::runtime::MutexResult::Invalid;
    const uint64_t owner = currentThreadId();
    if (owner == 0) return gxos::runtime::MutexResult::Invalid;
    if (handle->owner == owner) {
        if (handle->recursion >= gxos::runtime::kMutexMaximumRecursion) {
            return gxos::runtime::MutexResult::RecursionLimit;
        }
        ++handle->recursion;
        return gxos::runtime::MutexResult::Acquired;
    }
    if (_InterlockedCompareExchange(&handle->held, 1, 0) != 0) {
        return gxos::runtime::MutexResult::WouldBlock;
    }
    handle->owner = owner;
    handle->recursion = 1;
    return gxos::runtime::MutexResult::Acquired;
}

gxos::runtime::MutexResult leaveCriticalSection(CriticalSectionHandle* handle) {
    if (handle == nullptr || handle->owner != currentThreadId() ||
        handle->recursion == 0) return gxos::runtime::MutexResult::NotOwner;
    if (--handle->recursion == 0) {
        handle->owner = 0;
        _InterlockedExchange(&handle->held, 0);
    }
    return gxos::runtime::MutexResult::Released;
}

gxos::runtime::MutexStatus deleteCriticalSection(CriticalSectionHandle* handle) {
    if (handle == nullptr) return gxos::runtime::MutexStatus::Invalid;
    if (handle->held != 0 || handle->recursion != 0) {
        return gxos::runtime::MutexStatus::Busy;
    }
    delete handle;
    return gxos::runtime::MutexStatus::Ok;
}

struct EventHandle {
    void* raw;
};

namespace {
EventHandle* createEvent(uint32_t manual_reset, uint32_t initial_state) {
    g_guidexos_nativeaot_gc_startup_stage = 0x20u;
    EventHandle* result = new (std::nothrow) EventHandle{};
    if (result == nullptr) return nullptr;
    result->raw = guidexos_nativeaot_gc_create_event(manual_reset, initial_state);
    if (result->raw == nullptr) {
        delete result;
        return nullptr;
    }
    return result;
}
}

EventHandle* createAutoResetEvent(bool initiallySignaled) {
    return createEvent(0u, initiallySignaled ? 1u : 0u);
}

EventHandle* createManualResetEvent(bool initiallySignaled) {
    return createEvent(1u, initiallySignaled ? 1u : 0u);
}

gxos::runtime::EventStatus setEvent(EventHandle* handle) {
    return handle == nullptr || guidexos_nativeaot_gc_set_event(handle->raw) != 0
        ? gxos::runtime::EventStatus::Invalid : gxos::runtime::EventStatus::Ok;
}

gxos::runtime::EventStatus resetEvent(EventHandle* handle) {
    return handle == nullptr || guidexos_nativeaot_gc_reset_event(handle->raw) != 0
        ? gxos::runtime::EventStatus::Invalid : gxos::runtime::EventStatus::Ok;
}

gxos::runtime::WaitResult waitIndefinitely(EventHandle* handle) {
    if (handle == nullptr) return gxos::runtime::WaitResult::Invalid;
    const int32_t result = guidexos_nativeaot_gc_wait_event(handle->raw, kInfinite);
    return result == static_cast<int32_t>(kWaitObject0)
        ? gxos::runtime::WaitResult::Signaled
        : gxos::runtime::WaitResult::Invalid;
}

gxos::runtime::WaitResult waitMilliseconds(EventHandle* handle, int64_t milliseconds) {
    if (handle == nullptr || milliseconds < 0 ||
        milliseconds > static_cast<int64_t>(0xFFFFFFFEu)) {
        return gxos::runtime::WaitResult::Invalid;
    }
    const int32_t result = guidexos_nativeaot_gc_wait_event(
        handle->raw, static_cast<uint32_t>(milliseconds));
    if (result == static_cast<int32_t>(kWaitObject0)) return gxos::runtime::WaitResult::Signaled;
    if (result == static_cast<int32_t>(kWaitTimeout)) return gxos::runtime::WaitResult::TimedOut;
    return gxos::runtime::WaitResult::Invalid;
}

gxos::runtime::EventStatus destroyEvent(EventHandle* handle) {
    if (handle == nullptr) return gxos::runtime::EventStatus::Invalid;
    const int32_t result = guidexos_nativeaot_gc_close_event(handle->raw);
    delete handle;
    return result == 0 ? gxos::runtime::EventStatus::Ok
                       : gxos::runtime::EventStatus::Invalid;
}

} // namespace nativeaot
} // namespace guidexos

namespace guidexos {
namespace nativeaot {
namespace virtual_memory {

#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
namespace {

constexpr nativeaot_vm_size kSegmentBoundaryTraceCapacity = 128;
TraceEvent g_segmentBoundaryTrace[kSegmentBoundaryTraceCapacity]{};
nativeaot_vm_size g_segmentBoundaryTraceCount = 0;
nativeaot_vm_uint64 g_segmentBoundaryTraceSequence = 0;

void recordSegmentBoundaryTrace(TraceOperation operation, void* address,
                                nativeaot_vm_size size, VmResult result) {
    if (g_segmentBoundaryTraceCount >= kSegmentBoundaryTraceCapacity) return;
    TraceEvent& event = g_segmentBoundaryTrace[g_segmentBoundaryTraceCount++];
    event = TraceEvent{};
    event.sequence = ++g_segmentBoundaryTraceSequence;
    event.operation = operation;
    event.address = address;
    event.size = size;
    event.result = result;
}

} // namespace

nativeaot_vm_size traceEventCount() {
    return g_segmentBoundaryTraceCount;
}

bool traceEventAt(nativeaot_vm_size index, TraceEvent* event) {
    if (event == nullptr || index >= g_segmentBoundaryTraceCount) return false;
    *event = g_segmentBoundaryTrace[index];
    return true;
}
#endif

VmResult initializeVirtualMemoryAdapter() {
    g_guidexos_nativeaot_gc_startup_stage = 0x10u;
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    g_segmentBoundaryTraceCount = 0;
    g_segmentBoundaryTraceSequence = 0;
#endif
    return guidexos_nativeaot_gc_page_size() == kPageSize
        ? VmResult::Ok : VmResult::HostFailure;
}

VmResult shutdownVirtualMemoryAdapter(bool) {
    return VmResult::Ok;
}

bool virtualMemoryAdapterInitialized() {
    return guidexos_nativeaot_gc_page_size() != 0;
}

void* gcVirtualReserve(nativeaot_vm_size size, nativeaot_vm_size alignment,
                       nativeaot_vm_uint32 flags, nativeaot_vm_uint16 node) {
    g_guidexos_nativeaot_gc_startup_reserve_pages =
        static_cast<uint32_t>((size >> 12) > 0xFFFFFFFFu
            ? 0xFFFFFFFFu : (size >> 12));
    g_guidexos_nativeaot_gc_startup_reserve_mib =
        static_cast<uint32_t>((size >> 20) > 0xFFFFFFFFu
            ? 0xFFFFFFFFu : (size >> 20));
    g_guidexos_nativeaot_gc_startup_stage = 0x11000000u |
        static_cast<uint32_t>((size >> 12) & 0x00FFFFFFu);
    void* result = guidexos_nativeaot_gc_reserve(size, alignment, flags, node);
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    recordSegmentBoundaryTrace(TraceOperation::Reserve, result, size,
                               result == nullptr ? VmResult::HostFailure : VmResult::Ok);
#endif
    return result;
}

bool gcVirtualCommit(void* address, nativeaot_vm_size size,
                     nativeaot_vm_uint16 node) {
    g_guidexos_nativeaot_gc_startup_stage = 0x12u;
    const bool success = guidexos_nativeaot_gc_commit(address, size, node) == 0;
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    recordSegmentBoundaryTrace(TraceOperation::Commit, address, size,
                               success ? VmResult::Ok : VmResult::HostFailure);
#endif
    return success;
}

bool gcVirtualDecommit(void* address, nativeaot_vm_size size) {
    g_guidexos_nativeaot_gc_startup_stage = 0x13u;
    const bool success = guidexos_nativeaot_gc_decommit(address, size) == 0;
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    recordSegmentBoundaryTrace(TraceOperation::Decommit, address, size,
                               success ? VmResult::Ok : VmResult::HostFailure);
#endif
    return success;
}

bool gcVirtualRelease(void* address, nativeaot_vm_size size) {
    g_guidexos_nativeaot_gc_startup_stage = 0x14u;
    const bool success = guidexos_nativeaot_gc_release(address, size) == 0;
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    recordSegmentBoundaryTrace(TraceOperation::Release, address, size,
                               success ? VmResult::Ok : VmResult::HostFailure);
#endif
    return success;
}

bool gcVirtualReset(void* address, nativeaot_vm_size size, bool unlock) {
    const bool success = guidexos_nativeaot_gc_reset(address, size, unlock ? 1u : 0u) == 0;
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    recordSegmentBoundaryTrace(TraceOperation::Reset, address, size,
                               success ? VmResult::Ok : VmResult::HostFailure);
#endif
    return success;
}

bool supportsWriteWatch() { return false; }

void resetWriteWatch(void*, nativeaot_vm_size) {}

bool getWriteWatch(bool, void*, nativeaot_vm_size, void**, nativeaot_vm_size*) {
    return false;
}

void* reserveAndCommitLargePages(nativeaot_vm_size, nativeaot_vm_uint16) {
    return nullptr;
}

nativeaot_vm_size getPageSize() { return guidexos_nativeaot_gc_page_size(); }
nativeaot_vm_size getAllocationGranularity() {
    return guidexos_nativeaot_gc_allocation_granularity();
}
nativeaot_vm_size gcGetVirtualMemoryLimit() {
    g_guidexos_nativeaot_gc_startup_stage = 0x30u;
    return static_cast<nativeaot_vm_size>(guidexos_nativeaot_gc_virtual_memory_limit());
}
nativeaot_vm_uint64 gcGetPhysicalMemoryLimit(bool* restricted) {
    g_guidexos_nativeaot_gc_startup_stage = 0x31u;
    uint32_t value = 0;
    const uint64_t result = guidexos_nativeaot_gc_physical_memory_limit(&value);
    if (restricted != nullptr) *restricted = value != 0;
    return result;
}
void gcGetMemoryStatus(nativeaot_vm_uint64 restrictedLimit,
                       nativeaot_vm_uint32* memoryLoad,
                       nativeaot_vm_uint64* availablePhysical,
                       nativeaot_vm_uint64* availablePageFile) {
    g_guidexos_nativeaot_gc_startup_stage = 0x32u;
    guidexos_nativeaot_gc_memory_status(restrictedLimit, memoryLoad,
                                        availablePhysical, availablePageFile);
}

} // namespace virtual_memory
} // namespace nativeaot
} // namespace guidexos

namespace guidexos {
namespace nativeaot {
namespace gcservices {

uint64_t currentThreadIdForLogging() {
    return currentThreadId();
}
uint32_t currentProcessId() { return 1u; }
uint32_t currentProcessorNumber() { return 0u; }
bool setCurrentThreadIdealAffinity(uint16_t, uint16_t) { return true; }
bool getCurrentThreadIdealProcessor(uint16_t* processor) {
    if (processor == nullptr) return false;
    *processor = 0;
    return true;
}
bool setThreadAffinity(uint16_t) { return true; }
bool boostThreadPriority() { return true; }
void sleepMilliseconds(uint32_t milliseconds) {
    (void)guidexos_nativeaot_pal_sleep(milliseconds);
}
void yieldThread() { yield(); }
int64_t performanceCounter() {
    uint64_t value = 0;
    return guidexos_nativeaot_pal_counter(&value) == 0 ? static_cast<int64_t>(value) : 0;
}
int64_t performanceFrequency() {
    uint64_t value = 0;
    return guidexos_nativeaot_pal_frequency(&value) == 0 ? static_cast<int64_t>(value) : 0;
}
uint64_t lowPrecisionTimestamp() {
    uint64_t value = 0;
    return guidexos_nativeaot_pal_counter(&value) == 0 ? value : 0;
}
void flushProcessWriteBuffers() {}
uint32_t totalProcessorCount() { return 1u; }

} // namespace gcservices
} // namespace nativeaot
} // namespace guidexos
