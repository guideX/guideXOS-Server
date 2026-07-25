#pragma once

// Minimal application-scoped NativeAOT ThreadStore lifecycle used only by the
// inactive startup-readiness probe.  The runtime-thread record is deliberately
// opaque outside this runtime-pack boundary.

#if defined(GXOS_BARE_METAL)
#include <stdint.h>
namespace std {
using ::int32_t;
using ::uint32_t;
using ::uintptr_t;
}
#else
#include <cstdint>
#endif

#include "../../../../../runtime/thread/guidexos_native_stack_bounds.h"

namespace guidexos {
namespace nativeaot {
namespace threadstore {

enum class Result {
    Success,
    InvalidArgument,
    NotInitialized,
    AlreadyInitialized,
    AlreadyAttached,
    NotAttached,
    InvalidBounds,
    CurrentPointerOutsideBounds,
    StackBoundsUnavailable,
    NoResources,
    ShutdownWithAttachedThreads,
    AlreadyShutdown,
    LiveTransitionFrame,
    CurrentThreadMismatch,
    FlsFailure,
    CorruptRecord
};

struct ThreadSnapshot {
    std::uintptr_t nativeThreadId;
    std::uintptr_t stackLow;
    std::uintptr_t stackHigh;
    std::uintptr_t currentStackPointer;
    std::uintptr_t transitionFrame;
    std::uintptr_t deferredTransitionFrame;
    std::uintptr_t allocationContext;
    std::uint32_t generation;
    std::uint32_t attached;
    std::uint32_t preemptive;
};

Result initialize();
Result shutdown();
bool isInitialized();
std::uint32_t attachedThreadCount();
std::uint32_t callbackDetachCount();

Result attachCurrentThread();
Result detachCurrentThread();

void* getCurrentThread();
bool snapshotCurrentThread(ThreadSnapshot* result);

const char* resultName(Result result);

} // namespace threadstore
} // namespace nativeaot
} // namespace guidexos

extern "C" {
int guidexos_nativeaot_threadstore_initialize();
int guidexos_nativeaot_threadstore_shutdown();
int guidexos_nativeaot_threadstore_attach_current_thread();
int guidexos_nativeaot_threadstore_detach_current_thread();
void* guidexos_nativeaot_threadstore_get_current_thread();
unsigned int guidexos_nativeaot_threadstore_attached_count();
}
