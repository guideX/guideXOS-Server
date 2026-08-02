// Exact .NET 9.0 NativeAOT Workstation GC platform-object replacement.
//
// This source intentionally includes the locked GC declarations rather than
// copying or renaming collector call sites.  It replaces the complete
// gcenv.windows.cpp object family and routes its methods to guideXOS adapters.

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include "gcenv.structs.h"
#include "gcenv.os.h"

#include "../platform/guidexos_nativeaot_critical_section_adapter.h"
#include "../platform/guidexos_nativeaot_event_adapter.h"
#include "../platform/guidexos_nativeaot_virtual_memory_adapter.h"
#include "guidexos_gc_platform_services.h"

#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
extern "C" void __cdecl guideXosManagedAllocationInstallVmTraceCallbacks(
    uintptr_t traceCount, uintptr_t traceAt);
extern "C" void __cdecl guideXosManagedAllocationInstallSegmentDescriber(
    uintptr_t describeSegment);
extern "C" int32_t guidexos_nativeaot_gc_describe_segment(
    void* object,
    uintptr_t* segmentIdentity,
    uintptr_t* segmentBase,
    uintptr_t* segmentAllocated,
    uintptr_t* segmentCommitted,
    uintptr_t* segmentReserved,
    uint32_t* segmentFlags,
    uint32_t* segmentGeneration);
#endif

GCSystemInfo g_SystemInfo = {
    1,
    4096,
    4096,
};

namespace {

using guidexos::nativeaot::CriticalSectionHandle;
using guidexos::nativeaot::EventHandle;
using guidexos::nativeaot::createAutoResetEvent;
using guidexos::nativeaot::createManualResetEvent;
using guidexos::nativeaot::destroyEvent;
using guidexos::nativeaot::enterCriticalSection;
using guidexos::nativeaot::leaveCriticalSection;
using guidexos::nativeaot::setEvent;
using guidexos::nativeaot::resetEvent;
using guidexos::nativeaot::waitIndefinitely;
using guidexos::nativeaot::waitMilliseconds;
using guidexos::nativeaot::initializeCriticalSection;
using guidexos::nativeaot::deleteCriticalSection;
using guidexos::nativeaot::tryEnterCriticalSection;
using namespace guidexos::nativeaot::virtual_memory;

constexpr std::uint32_t kWaitObject0 = 0;
constexpr std::uint32_t kWaitTimeout = 258;
constexpr std::uint32_t kWaitFailed = 0xFFFFFFFFu;
constexpr std::uint32_t kInfiniteTimeout = 0xFFFFFFFFu;

[[noreturn]] void unexpectedPlatformEntry() {
    std::abort();
}

class CriticalStorage {
public:
    static CriticalSectionHandle* get(CRITICAL_SECTION* storage) {
        return *reinterpret_cast<CriticalSectionHandle**>(storage);
    }

    static void put(CRITICAL_SECTION* storage, CriticalSectionHandle* handle) {
        *reinterpret_cast<CriticalSectionHandle**>(storage) = handle;
    }
};

} // namespace

bool CLRCriticalSection::Initialize() {
    static_assert(sizeof(CRITICAL_SECTION) >= sizeof(void*),
                  "locked CLRCriticalSection storage must hold an adapter handle");
    CriticalSectionHandle* handle = initializeCriticalSection();
    if (handle == nullptr) return false;
    CriticalStorage::put(&m_cs, handle);
    return true;
}

void CLRCriticalSection::Destroy() {
    CriticalSectionHandle* handle = CriticalStorage::get(&m_cs);
    if (handle == nullptr ||
        deleteCriticalSection(handle) != gxos::runtime::MutexStatus::Ok) {
        unexpectedPlatformEntry();
    }
    CriticalStorage::put(&m_cs, nullptr);
}

void CLRCriticalSection::Enter() {
    CriticalSectionHandle* handle = CriticalStorage::get(&m_cs);
    if (handle == nullptr || enterCriticalSection(handle) != gxos::runtime::MutexResult::Acquired) {
        unexpectedPlatformEntry();
    }
}

void CLRCriticalSection::Leave() {
    CriticalSectionHandle* handle = CriticalStorage::get(&m_cs);
    if (handle == nullptr || leaveCriticalSection(handle) != gxos::runtime::MutexResult::Released) {
        unexpectedPlatformEntry();
    }
}

class GCEvent::Impl {
public:
    EventHandle* handle = nullptr;
};

GCEvent::GCEvent() : m_impl(nullptr) {}

void GCEvent::CloseEvent() {
    if (m_impl == nullptr || m_impl->handle == nullptr ||
        destroyEvent(m_impl->handle) != gxos::runtime::EventStatus::Ok) {
        unexpectedPlatformEntry();
    }
    delete m_impl;
    m_impl = nullptr;
}

void GCEvent::Set() {
    if (m_impl == nullptr || m_impl->handle == nullptr ||
        setEvent(m_impl->handle) != gxos::runtime::EventStatus::Ok) {
        unexpectedPlatformEntry();
    }
}

void GCEvent::Reset() {
    if (m_impl == nullptr || m_impl->handle == nullptr ||
        resetEvent(m_impl->handle) != gxos::runtime::EventStatus::Ok) {
        unexpectedPlatformEntry();
    }
}

uint32_t GCEvent::Wait(uint32_t timeout, bool alertable) {
    if (m_impl == nullptr || m_impl->handle == nullptr) return kWaitFailed;
    (void)alertable;
    const gxos::runtime::WaitResult result =
        timeout == kInfiniteTimeout ? waitIndefinitely(m_impl->handle) :
                              waitMilliseconds(m_impl->handle, timeout);
    switch (result) {
    case gxos::runtime::WaitResult::Signaled: return kWaitObject0;
    case gxos::runtime::WaitResult::TimedOut: return kWaitTimeout;
    default: return kWaitFailed;
    }
}

bool GCEvent::CreateAutoEventNoThrow(bool initialState) {
    return CreateOSAutoEventNoThrow(initialState);
}

bool GCEvent::CreateManualEventNoThrow(bool initialState) {
    return CreateOSManualEventNoThrow(initialState);
}

bool GCEvent::CreateOSAutoEventNoThrow(bool initialState) {
    if (m_impl != nullptr) return false;
    std::unique_ptr<GCEvent::Impl> impl(new (std::nothrow) GCEvent::Impl());
    if (!impl) return false;
    impl->handle = createAutoResetEvent(initialState);
    if (impl->handle == nullptr) return false;
    m_impl = impl.release();
    return true;
}

bool GCEvent::CreateOSManualEventNoThrow(bool initialState) {
    if (m_impl != nullptr) return false;
    std::unique_ptr<GCEvent::Impl> impl(new (std::nothrow) GCEvent::Impl());
    if (!impl) return false;
    impl->handle = createManualResetEvent(initialState);
    if (impl->handle == nullptr) return false;
    m_impl = impl.release();
    return true;
}

bool GCToOSInterface::Initialize() {
    g_SystemInfo.dwNumberOfProcessors =
        guidexos::nativeaot::gcservices::totalProcessorCount();
    g_SystemInfo.dwPageSize = static_cast<uint32_t>(getPageSize());
    g_SystemInfo.dwAllocationGranularity =
        static_cast<uint32_t>(getAllocationGranularity());
    const VmResult result = initializeVirtualMemoryAdapter();
#if defined(GUIDEXOS_NATIVEAOT_SEGMENT_BOUNDARY_ALLOCATION)
    if (result == VmResult::Ok) {
        guideXosManagedAllocationInstallVmTraceCallbacks(
            reinterpret_cast<uintptr_t>(traceEventCount),
            reinterpret_cast<uintptr_t>(traceEventAt));
        guideXosManagedAllocationInstallSegmentDescriber(
            reinterpret_cast<uintptr_t>(guidexos_nativeaot_gc_describe_segment));
    }
#endif
    return result == VmResult::Ok;
}

void GCToOSInterface::Shutdown() {
    if (virtualMemoryAdapterInitialized() &&
        shutdownVirtualMemoryAdapter(false) != VmResult::Ok) {
        unexpectedPlatformEntry();
    }
}

void* GCToOSInterface::VirtualReserve(size_t size, size_t alignment,
                                      uint32_t flags, uint16_t node) {
    return gcVirtualReserve(size, alignment, flags, node);
}

bool GCToOSInterface::VirtualRelease(void* address, size_t size) {
    return gcVirtualRelease(address, size);
}

void* GCToOSInterface::VirtualReserveAndCommitLargePages(size_t size,
                                                         uint16_t node) {
    return reserveAndCommitLargePages(size, node);
}

bool GCToOSInterface::VirtualCommit(void* address, size_t size,
                                    uint16_t node) {
    return gcVirtualCommit(address, size, node);
}

bool GCToOSInterface::VirtualDecommit(void* address, size_t size) {
    return gcVirtualDecommit(address, size);
}

bool GCToOSInterface::VirtualReset(void* address, size_t size, bool unlock) {
    return gcVirtualReset(address, size, unlock);
}

bool GCToOSInterface::SupportsWriteWatch() {
    return supportsWriteWatch();
}

void GCToOSInterface::ResetWriteWatch(void* address, size_t size) {
    (void)address;
    (void)size;
    // Write-watch is deliberately not enabled by the locked guideXOS GC
    // configuration.  Do not report a successful reset for an unsupported
    // optional path if a future configuration selects it unexpectedly.
    unexpectedPlatformEntry();
}

bool GCToOSInterface::GetWriteWatch(bool resetState, void* address, size_t size,
                                    void** pageAddresses,
                                    uintptr_t* pageAddressesCount) {
    return getWriteWatch(resetState, address, size, pageAddresses,
                         reinterpret_cast<size_t*>(pageAddressesCount));
}

void GCToOSInterface::Sleep(uint32_t sleepMSec) {
    guidexos::nativeaot::gcservices::sleepMilliseconds(sleepMSec);
}

void GCToOSInterface::YieldThread(uint32_t switchCount) {
    (void)switchCount;
    guidexos::nativeaot::gcservices::yieldThread();
}

uint32_t GCToOSInterface::GetCurrentProcessorNumber() {
    return guidexos::nativeaot::gcservices::currentProcessorNumber();
}

bool GCToOSInterface::CanGetCurrentProcessorNumber() {
    return true;
}

bool GCToOSInterface::SetCurrentThreadIdealAffinity(uint16_t srcProcNo,
                                                     uint16_t dstProcNo) {
    return guidexos::nativeaot::gcservices::setCurrentThreadIdealAffinity(
        srcProcNo, dstProcNo);
}

bool GCToOSInterface::GetCurrentThreadIdealProc(uint16_t* procNo) {
    return guidexos::nativeaot::gcservices::getCurrentThreadIdealProcessor(procNo);
}

uint64_t GCToOSInterface::GetCurrentThreadIdForLogging() {
    return guidexos::nativeaot::gcservices::currentThreadIdForLogging();
}

uint32_t GCToOSInterface::GetCurrentProcessId() {
    return guidexos::nativeaot::gcservices::currentProcessId();
}

size_t GCToOSInterface::GetCacheSizePerLogicalCpu(bool trueSize) {
    (void)trueSize;
    return 256u * 1024u;
}

bool GCToOSInterface::SetThreadAffinity(uint16_t procNo) {
    return guidexos::nativeaot::gcservices::setThreadAffinity(procNo);
}

bool GCToOSInterface::BoostThreadPriority() {
    return guidexos::nativeaot::gcservices::boostThreadPriority();
}

const AffinitySet* GCToOSInterface::SetGCThreadsAffinitySet(
    uintptr_t configAffinityMask, const AffinitySet* configAffinitySet) {
    static AffinitySet processAffinitySet;
    static bool initialized = false;
    if (!initialized) {
        processAffinitySet.Add(0);
        initialized = true;
    }
    if (configAffinityMask != 0 && (configAffinityMask & 1u) == 0) {
        processAffinitySet.Remove(0);
    }
    if (configAffinitySet != nullptr && !configAffinitySet->IsEmpty() &&
        !configAffinitySet->Contains(0)) {
        processAffinitySet.Remove(0);
    }
    return &processAffinitySet;
}

size_t GCToOSInterface::GetVirtualMemoryLimit() {
    return gcGetVirtualMemoryLimit();
}

size_t GCToOSInterface::GetVirtualMemoryMaxAddress() {
    return GetVirtualMemoryLimit();
}

uint64_t GCToOSInterface::GetPhysicalMemoryLimit(bool* is_restricted) {
    return gcGetPhysicalMemoryLimit(is_restricted);
}

void GCToOSInterface::GetMemoryStatus(uint64_t restricted_limit,
                                      uint32_t* memory_load,
                                      uint64_t* available_physical,
                                      uint64_t* available_page_file) {
    gcGetMemoryStatus(restricted_limit, memory_load, available_physical,
                       available_page_file);
}

void GCToOSInterface::FlushProcessWriteBuffers() {
    guidexos::nativeaot::gcservices::flushProcessWriteBuffers();
}

void GCToOSInterface::DebugBreak() {
    unexpectedPlatformEntry();
}

int64_t GCToOSInterface::QueryPerformanceCounter() {
    return guidexos::nativeaot::gcservices::performanceCounter();
}

int64_t GCToOSInterface::QueryPerformanceFrequency() {
    return guidexos::nativeaot::gcservices::performanceFrequency();
}

uint64_t GCToOSInterface::GetLowPrecisionTimeStamp() {
    return guidexos::nativeaot::gcservices::lowPrecisionTimestamp();
}

uint32_t GCToOSInterface::GetTotalProcessorCount() {
    return guidexos::nativeaot::gcservices::totalProcessorCount();
}

bool GCToOSInterface::CanEnableGCNumaAware() {
    return false;
}

bool GCToOSInterface::GetNumaInfo(uint16_t* total_nodes,
                                  uint32_t* max_procs_per_node) {
    (void)total_nodes;
    (void)max_procs_per_node;
    return false;
}

bool GCToOSInterface::CanEnableGCCPUGroups() {
    return false;
}

bool GCToOSInterface::GetProcessorForHeap(uint16_t heap_number,
                                          uint16_t* proc_no,
                                          uint16_t* node_no) {
    if (proc_no == nullptr || node_no == nullptr || heap_number != 0) {
        return false;
    }
    *proc_no = 0;
    *node_no = NUMA_NODE_UNDEFINED;
    return true;
}

bool GCToOSInterface::GetCPUGroupInfo(uint16_t* total_groups,
                                      uint32_t* max_procs_per_group) {
    (void)total_groups;
    (void)max_procs_per_group;
    return false;
}

bool GCToOSInterface::ParseGCHeapAffinitizeRangesEntry(
    const char** config_string, size_t* start_index, size_t* end_index) {
    (void)config_string;
    (void)start_index;
    (void)end_index;
    return false;
}
