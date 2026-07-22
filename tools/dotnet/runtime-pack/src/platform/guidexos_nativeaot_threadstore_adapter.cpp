#include "guidexos_nativeaot_threadstore_adapter.h"

#include "guidexos_nativeaot_fls_adapter.h"
#include "guidexos_nativeaot_stack_bounds_adapter.h"

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace guidexos {
namespace nativeaot {
namespace threadstore {
namespace {

constexpr std::uint32_t kMaximumThreads = 32;
constexpr std::uintptr_t kTopOfStackMarker = static_cast<std::uintptr_t>(-1);

// This is the smallest state retained for the startup gate.  It mirrors the
// source-backed ThreadStore/Thread requirements without exposing the stock
// RuntimeThreadLocals ABI or a generic guideXOS TCB.
struct RuntimeThreadRecord {
    std::uintptr_t nativeThreadId = 0;
    gxos::runtime::NativeStackBounds stack{};
    std::uintptr_t transitionFrame = kTopOfStackMarker;
    std::uintptr_t deferredTransitionFrame = kTopOfStackMarker;
    std::uintptr_t cachedTransitionFrame = 0;
    std::uintptr_t allocationContext = 0;
    std::uint32_t generation = 1;
    bool allocated = false;
    bool attached = false;
    bool preemptive = true;
    std::int32_t previous = -1;
    std::int32_t next = -1;
};

std::mutex g_mutex;
std::array<RuntimeThreadRecord, kMaximumThreads> g_records{};
bool g_initialized = false;
std::uint32_t g_attachedCount = 0;
std::uint32_t g_callbackDetachCount = 0;
std::int32_t g_head = -1;
fls::Index g_flsIndex = fls::kOutOfIndexes;

std::uintptr_t currentNativeThreadId() {
    return static_cast<std::uintptr_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

RuntimeThreadRecord* recordFromPointer(void* pointer) {
    if (pointer == nullptr) {
        return nullptr;
    }
    for (RuntimeThreadRecord& record : g_records) {
        if (reinterpret_cast<void*>(&record) == pointer) {
            return &record;
        }
    }
    return nullptr;
}

RuntimeThreadRecord* currentRecordLocked() {
    if (!g_initialized || g_flsIndex == fls::kOutOfIndexes) {
        return nullptr;
    }
    return recordFromPointer(fls::get(g_flsIndex));
}

void unlinkLocked(RuntimeThreadRecord* record) {
    if (record == nullptr) {
        return;
    }
    if (record->previous >= 0) {
        g_records[record->previous].next = record->next;
    }
    else if (g_head >= 0) {
        g_head = record->next;
    }
    if (record->next >= 0) {
        g_records[record->next].previous = record->previous;
    }
    record->previous = -1;
    record->next = -1;
}

void retireRecordLocked(RuntimeThreadRecord* record) {
    if (record == nullptr) {
        return;
    }
    unlinkLocked(record);
    record->attached = false;
    record->allocated = false;
    record->transitionFrame = 0;
    record->deferredTransitionFrame = 0;
    record->cachedTransitionFrame = 0;
    record->allocationContext = 0;
    if (record->generation != 0xFFFFFFFFu) {
        ++record->generation;
    }
    if (g_attachedCount != 0) {
        --g_attachedCount;
    }
}

void runtimeThreadDetachCallback(void* value) {
    // Local-storage detach clears the FLS cell before invoking this callback.
    // The record is therefore still live, but calling fls::set here would be
    // both unnecessary and rejected during the generic detach window.
    std::lock_guard<std::mutex> lock(g_mutex);
    RuntimeThreadRecord* record = recordFromPointer(value);
    if (record == nullptr || !record->allocated || !record->attached ||
        record->nativeThreadId != currentNativeThreadId()) {
        return;
    }
    retireRecordLocked(record);
    ++g_callbackDetachCount;
}

Result stackBoundsFailure(gxos::runtime::StackBoundsResult result) {
    switch (result) {
        case gxos::runtime::StackBoundsResult::InvalidBounds:
            return Result::InvalidBounds;
        case gxos::runtime::StackBoundsResult::CurrentPointerOutsideBounds:
            return Result::CurrentPointerOutsideBounds;
        default:
            return Result::StackBoundsUnavailable;
    }
}

} // namespace

Result initialize() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) {
        return Result::AlreadyInitialized;
    }
    if (!gxos::runtime::isLocalStorageInitialized()) {
        return Result::NotInitialized;
    }
    g_flsIndex = fls::alloc(runtimeThreadDetachCallback);
    if (g_flsIndex == fls::kOutOfIndexes) {
        return Result::NoResources;
    }
    g_initialized = true;
    g_attachedCount = 0;
    g_callbackDetachCount = 0;
    g_head = -1;
    for (RuntimeThreadRecord& record : g_records) {
        record = RuntimeThreadRecord{};
    }
    return Result::Success;
}

Result shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) {
        return Result::AlreadyShutdown;
    }
    if (g_attachedCount != 0) {
        return Result::ShutdownWithAttachedThreads;
    }
    if (g_flsIndex != fls::kOutOfIndexes && !fls::free(g_flsIndex)) {
        return Result::FlsFailure;
    }
    g_flsIndex = fls::kOutOfIndexes;
    g_head = -1;
    g_initialized = false;
    return Result::Success;
}

bool isInitialized() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_initialized;
}

std::uint32_t attachedThreadCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_attachedCount;
}

std::uint32_t callbackDetachCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_callbackDetachCount;
}

Result attachCurrentThread() {
    gxos::runtime::NativeStackBounds bounds{};
    if (!pal::getMaximumStackBounds(&bounds)) {
        const gxos::runtime::StackBoundsResult result =
            gxos::runtime::queryCurrentNativeStackBounds(&bounds);
        return stackBoundsFailure(result);
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) {
        return Result::NotInitialized;
    }
    if (currentRecordLocked() != nullptr) {
        return Result::AlreadyAttached;
    }
    RuntimeThreadRecord* record = nullptr;
    for (RuntimeThreadRecord& candidate : g_records) {
        if (!candidate.allocated) {
            record = &candidate;
            break;
        }
    }
    if (record == nullptr) {
        return Result::NoResources;
    }
    record->nativeThreadId = currentNativeThreadId();
    record->stack = bounds;
    record->transitionFrame = kTopOfStackMarker;
    record->deferredTransitionFrame = kTopOfStackMarker;
    record->cachedTransitionFrame = 0;
    // NativeAOT explicitly permits a zero-initialized allocation context at
    // Thread::Construct time.  Allocation remains outside this probe.
    record->allocationContext = 0;
    record->allocated = true;
    record->attached = true;
    record->preemptive = true;
    const std::int32_t index = static_cast<std::int32_t>(record - g_records.data());
    record->next = g_head;
    record->previous = -1;
    if (g_head >= 0) {
        g_records[g_head].previous = index;
    }
    g_head = index;
    if (!fls::set(g_flsIndex, record)) {
        unlinkLocked(record);
        record->allocated = false;
        record->attached = false;
        return Result::FlsFailure;
    }
    ++g_attachedCount;
    return Result::Success;
}

Result detachCurrentThread() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) {
        return Result::NotInitialized;
    }
    RuntimeThreadRecord* record = currentRecordLocked();
    if (record == nullptr || !record->attached) {
        return Result::NotAttached;
    }
    if (record->nativeThreadId != currentNativeThreadId()) {
        return Result::CurrentThreadMismatch;
    }
    if (record->transitionFrame != kTopOfStackMarker ||
        record->cachedTransitionFrame != 0) {
        return Result::LiveTransitionFrame;
    }
    if (!fls::set(g_flsIndex, nullptr)) {
        return Result::FlsFailure;
    }
    retireRecordLocked(record);
    return Result::Success;
}

void* getCurrentThread() {
    std::lock_guard<std::mutex> lock(g_mutex);
    RuntimeThreadRecord* record = currentRecordLocked();
    if (record == nullptr || record->nativeThreadId != currentNativeThreadId()) {
        return nullptr;
    }
    return record;
}

bool snapshotCurrentThread(ThreadSnapshot* result) {
    if (result == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    RuntimeThreadRecord* record = currentRecordLocked();
    if (record == nullptr || record->nativeThreadId != currentNativeThreadId()) {
        *result = ThreadSnapshot{};
        return false;
    }
    result->nativeThreadId = record->nativeThreadId;
    result->stackLow = record->stack.low;
    result->stackHigh = record->stack.high;
    result->currentStackPointer = record->stack.current;
    result->transitionFrame = record->transitionFrame;
    result->deferredTransitionFrame = record->deferredTransitionFrame;
    result->allocationContext = record->allocationContext;
    result->generation = record->generation;
    result->attached = record->attached ? 1u : 0u;
    result->preemptive = record->preemptive ? 1u : 0u;
    return true;
}

const char* resultName(Result result) {
    switch (result) {
        case Result::Success: return "Success";
        case Result::InvalidArgument: return "InvalidArgument";
        case Result::NotInitialized: return "NotInitialized";
        case Result::AlreadyInitialized: return "AlreadyInitialized";
        case Result::AlreadyAttached: return "AlreadyAttached";
        case Result::NotAttached: return "NotAttached";
        case Result::InvalidBounds: return "InvalidBounds";
        case Result::CurrentPointerOutsideBounds:
            return "CurrentPointerOutsideBounds";
        case Result::StackBoundsUnavailable: return "StackBoundsUnavailable";
        case Result::NoResources: return "NoResources";
        case Result::ShutdownWithAttachedThreads:
            return "ShutdownWithAttachedThreads";
        case Result::AlreadyShutdown: return "AlreadyShutdown";
        case Result::LiveTransitionFrame: return "LiveTransitionFrame";
        case Result::CurrentThreadMismatch: return "CurrentThreadMismatch";
        case Result::FlsFailure: return "FlsFailure";
        case Result::CorruptRecord: return "CorruptRecord";
    }
    return "Unknown";
}

} // namespace threadstore
} // namespace nativeaot
} // namespace guidexos

extern "C" int guidexos_nativeaot_threadstore_initialize() {
    return guidexos::nativeaot::threadstore::initialize() ==
        guidexos::nativeaot::threadstore::Result::Success ? 1 : 0;
}

extern "C" int guidexos_nativeaot_threadstore_shutdown() {
    return guidexos::nativeaot::threadstore::shutdown() ==
        guidexos::nativeaot::threadstore::Result::Success ? 1 : 0;
}

extern "C" int guidexos_nativeaot_threadstore_attach_current_thread() {
    return guidexos::nativeaot::threadstore::attachCurrentThread() ==
        guidexos::nativeaot::threadstore::Result::Success ? 1 : 0;
}

extern "C" int guidexos_nativeaot_threadstore_detach_current_thread() {
    return guidexos::nativeaot::threadstore::detachCurrentThread() ==
        guidexos::nativeaot::threadstore::Result::Success ? 1 : 0;
}

extern "C" void* guidexos_nativeaot_threadstore_get_current_thread() {
    return guidexos::nativeaot::threadstore::getCurrentThread();
}

extern "C" unsigned int guidexos_nativeaot_threadstore_attached_count() {
    return guidexos::nativeaot::threadstore::attachedThreadCount();
}
