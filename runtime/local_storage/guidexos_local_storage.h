#pragma once

// Runtime-neutral bounded local-storage index manager.
//
// The manager owns index metadata globally and keeps values in a fixed
// context owned by each native thread.  Platform adapters provide only the
// current-context and attach-state hooks; the generic API does not expose a
// scheduler TCB or a language-runtime TLS layout.

#if defined(GXOS_BARE_METAL)
#include <stddef.h>
#include <stdint.h>
using gxos_local_storage_uint32 = uint32_t;
using gxos_local_storage_size = size_t;
#else
#include <cstddef>
#include <cstdint>
using gxos_local_storage_uint32 = std::uint32_t;
using gxos_local_storage_size = std::size_t;
#endif

namespace gxos {
namespace runtime {

constexpr gxos_local_storage_uint32 kLocalStorageCapacity = 8;
constexpr gxos_local_storage_uint32 kLocalStorageMaximumContexts = 32;

using LocalStorageDetachCallback = void (*)(void* value);

struct LocalStorageIndex {
    gxos_local_storage_uint32 slot = 0;
    gxos_local_storage_uint32 generation = 0;

    constexpr bool isValid() const {
        return generation != 0;
    }
};

enum class LocalStorageResult {
    Success,
    Invalid,
    Exhausted,
    StaleIndex,
    NoCurrentThread,
    NotInitialized,
    CallbackFailed,
    Busy
};

// This is the complete per-thread value storage.  It is intentionally a
// pointer-only fixed table so ordinary get/set operations do not allocate.
struct LocalStorageContext {
    void* values[kLocalStorageCapacity] = {};
};

// Bare-metal and runtime-pack adapters install these hooks to map the current
// execution context to a LocalStorageContext.  Hosted callers use the
// built-in thread_local context and do not need to install hooks.
struct LocalStoragePlatformHooks {
    void* context;
    LocalStorageContext* (*currentContext)(void* context);
    bool (*isAttached)(void* context);
    void (*setAttached)(void* context, bool attached);
};

void installLocalStoragePlatformHooks(const LocalStoragePlatformHooks* hooks);

LocalStorageResult initializeLocalStorage();
LocalStorageResult shutdownLocalStorage();
bool isLocalStorageInitialized();

LocalStorageResult attachLocalStorage();
LocalStorageResult detachLocalStorage();

// Used only by scheduler/process teardown while the owner cannot resume.  A
// normal native-thread exit must use detachLocalStorage so callbacks execute
// on the exiting thread.  This operation still invokes callbacks before the
// context is reclaimed, but its callback must not depend on current-thread
// identity.
LocalStorageResult detachLocalStorageContext(LocalStorageContext* context);

// Last-resort forced cleanup for a TCB that is being reclaimed after an
// already-aborted process teardown.  It never invokes callbacks.
LocalStorageResult forceClearLocalStorageContext(LocalStorageContext* context);

LocalStorageResult allocateLocalStorageIndex(LocalStorageDetachCallback callback,
                                             LocalStorageIndex* result);
LocalStorageResult releaseLocalStorageIndex(LocalStorageIndex index);

LocalStorageResult setLocalStorageValue(LocalStorageIndex index, void* value);
LocalStorageResult getLocalStorageValue(LocalStorageIndex index, void** value);

const char* localStorageResultName(LocalStorageResult result);

} // namespace runtime
} // namespace gxos

