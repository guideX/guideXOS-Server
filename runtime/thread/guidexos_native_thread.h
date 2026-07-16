#pragma once

// Runtime-neutral native thread start/join contract.
//
// The public handle is a value capability.  It never exposes a scheduler TCB,
// host thread object, or runtime-specific state.  Bare-metal builds route the
// operations through hooks installed by the existing scheduler; hosted builds
// provide the same contract with the host's native thread facility.

#if defined(GXOS_BARE_METAL)
#include <stddef.h>
#include <stdint.h>
#include "../synchronization/guidexos_event.h"
using gxos_thread_uint32 = uint32_t;
using gxos_thread_uintptr = uintptr_t;
using gxos_thread_size = size_t;
#else
#include <cstddef>
#include <cstdint>
#include "../synchronization/guidexos_event.h"
using gxos_thread_uint32 = std::uint32_t;
using gxos_thread_uintptr = std::uintptr_t;
using gxos_thread_size = std::size_t;
#endif

namespace gxos {
namespace runtime {

using NativeThreadEntry = gxos_thread_uintptr (*)(void* context);

constexpr gxos_thread_size kNativeThreadMinimumStackSize = 4096;
constexpr gxos_thread_size kNativeThreadDefaultStackSize = 8192;
constexpr gxos_thread_size kNativeThreadMaximumStackSize = 16384;

struct ThreadCreateOptions {
    gxos_thread_size stackSize = kNativeThreadDefaultStackSize;
    const char* debugName = nullptr;
    bool detached = false;
};

struct ThreadHandle {
    gxos_thread_uint32 slot = 0;
    gxos_thread_uint32 generation = 0;

    constexpr bool isValid() const {
        return generation != 0;
    }
};

enum class ThreadResult {
    Ok,
    InvalidArgument,
    InvalidHandle,
    NoResources,
    InvalidStackSize,
    Detached,
    AlreadyDetached,
    AlreadyJoined,
    SelfJoin,
    ProcessTeardown,
    NotSupported
};

// A joinable thread owns one completion Event until its join ownership is
// consumed.  A detached thread has no join owner and is reclaimed after exit.
ThreadResult createThread(NativeThreadEntry entry,
                          void* context,
                          const ThreadCreateOptions& options,
                          ThreadHandle* result);

WaitResult joinThread(ThreadHandle thread,
                      const WaitTimeout& timeout,
                      gxos_thread_uintptr* exitResult);

ThreadResult detachThread(ThreadHandle thread);

#if defined(GXOS_BARE_METAL)

struct NativeThreadPlatformHooks {
    void* context;
    ThreadResult (*create)(void* context,
                           NativeThreadEntry entry,
                           void* entryContext,
                           const ThreadCreateOptions& options,
                           ThreadHandle* result);
    WaitResult (*join)(void* context,
                       ThreadHandle thread,
                       const WaitTimeout& timeout,
                       gxos_thread_uintptr* exitResult);
    ThreadResult (*detach)(void* context, ThreadHandle thread);
};

void installNativeThreadPlatformHooks(const NativeThreadPlatformHooks* hooks);

#endif

} // namespace runtime
} // namespace gxos

