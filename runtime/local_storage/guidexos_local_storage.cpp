#include "guidexos_local_storage.h"

#if !defined(GXOS_BARE_METAL)
#include <mutex>
#endif

namespace gxos {
namespace runtime {
namespace {

struct LocalStorageSlot {
    LocalStorageDetachCallback callback;
    gxos_local_storage_uint32 generation;
    bool active;
};

struct LocalStorageDomain {
    bool initialized;
    bool detaching;
    bool callbackFailure;
    gxos_local_storage_uint32 contextCount;
    LocalStorageSlot slots[kLocalStorageCapacity];
    LocalStorageContext* contexts[kLocalStorageMaximumContexts];
};

LocalStorageDomain g_domain = {};
LocalStoragePlatformHooks g_hooks = { nullptr, nullptr, nullptr, nullptr };

#if !defined(GXOS_BARE_METAL)
std::mutex g_metadataMutex;
thread_local LocalStorageContext g_hostContext = {};
thread_local bool g_hostAttached = false;
thread_local LocalStorageResult g_hostLastDetach = LocalStorageResult::Success;
#endif

bool hasPlatformHooks() {
    return g_hooks.currentContext != nullptr &&
           g_hooks.isAttached != nullptr &&
           g_hooks.setAttached != nullptr;
}

LocalStorageContext* currentContext() {
#if defined(GXOS_BARE_METAL)
    return hasPlatformHooks() ? g_hooks.currentContext(g_hooks.context) : nullptr;
#else
    return hasPlatformHooks() ? g_hooks.currentContext(g_hooks.context) : &g_hostContext;
#endif
}

bool currentAttached() {
#if defined(GXOS_BARE_METAL)
    return hasPlatformHooks() && g_hooks.isAttached(g_hooks.context);
#else
    return hasPlatformHooks() ? g_hooks.isAttached(g_hooks.context) : g_hostAttached;
#endif
}

void setCurrentAttached(bool attached) {
#if defined(GXOS_BARE_METAL)
    if (hasPlatformHooks()) {
        g_hooks.setAttached(g_hooks.context, attached);
    }
#else
    if (hasPlatformHooks()) {
        g_hooks.setAttached(g_hooks.context, attached);
    }
    else {
        g_hostAttached = attached;
    }
#endif
}

void clearContext(LocalStorageContext* context) {
    if (context == nullptr) {
        return;
    }
    for (gxos_local_storage_uint32 i = 0; i < kLocalStorageCapacity; ++i) {
        context->values[i] = nullptr;
    }
}

int findContext(LocalStorageContext* context) {
    if (context == nullptr) {
        return -1;
    }
    for (gxos_local_storage_uint32 i = 0; i < g_domain.contextCount; ++i) {
        if (g_domain.contexts[i] == context) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void unregisterContext(LocalStorageContext* context) {
    const int found = findContext(context);
    if (found < 0) {
        return;
    }
    const gxos_local_storage_uint32 index = static_cast<gxos_local_storage_uint32>(found);
    for (gxos_local_storage_uint32 i = index + 1; i < g_domain.contextCount; ++i) {
        g_domain.contexts[i - 1] = g_domain.contexts[i];
    }
    if (g_domain.contextCount != 0) {
        --g_domain.contextCount;
        g_domain.contexts[g_domain.contextCount] = nullptr;
    }
}

bool registerContext(LocalStorageContext* context) {
    if (findContext(context) >= 0) {
        return true;
    }
    if (g_domain.contextCount >= kLocalStorageMaximumContexts) {
        return false;
    }
    g_domain.contexts[g_domain.contextCount++] = context;
    return true;
}

LocalStorageResult validateIndex(LocalStorageIndex index) {
    if (index.slot >= kLocalStorageCapacity || index.generation == 0) {
        return LocalStorageResult::Invalid;
    }
    const LocalStorageSlot& slot = g_domain.slots[index.slot];
    if (!slot.active || slot.generation != index.generation) {
        return LocalStorageResult::StaleIndex;
    }
    return LocalStorageResult::Success;
}

struct PendingCallback {
    LocalStorageDetachCallback callback;
    void* value;
};

LocalStorageResult detachContextInternal(LocalStorageContext* context) {
#if !defined(GXOS_BARE_METAL)
    std::unique_lock<std::mutex> lock(g_metadataMutex);
#endif
    if (!g_domain.initialized) {
        return LocalStorageResult::NotInitialized;
    }
    if (context == nullptr || findContext(context) < 0) {
        return LocalStorageResult::NoCurrentThread;
    }
    if (g_domain.detaching) {
        return LocalStorageResult::Busy;
    }

    PendingCallback pending[kLocalStorageCapacity] = {};
    gxos_local_storage_uint32 pendingCount = 0;
    g_domain.detaching = true;
    g_domain.callbackFailure = false;

    // Stage and clear every value before entering user/runtime callbacks.
    // This gives callbacks a stable snapshot and prevents a callback from
    // observing a value that is already being torn down.
    for (gxos_local_storage_uint32 i = 0; i < kLocalStorageCapacity; ++i) {
        pending[i].callback = nullptr;
        pending[i].value = nullptr;
        const LocalStorageSlot& slot = g_domain.slots[i];
        if (slot.active && slot.callback != nullptr && context->values[i] != nullptr) {
            pending[pendingCount].callback = slot.callback;
            pending[pendingCount].value = context->values[i];
            ++pendingCount;
        }
        context->values[i] = nullptr;
    }

#if !defined(GXOS_BARE_METAL)
    // Do not hold the metadata lock while callbacks execute.  The detaching
    // flag keeps reentrant manager operations deterministic while allowing a
    // second hosted thread to observe the in-progress teardown safely.
    lock.unlock();
#endif

    // The detaching flag makes allocation/release/get/set report Busy instead
    // of allowing callback reentrancy to invalidate staged metadata.
    for (gxos_local_storage_uint32 i = 0; i < pendingCount; ++i) {
        pending[i].callback(pending[i].value);
    }

#if !defined(GXOS_BARE_METAL)
    lock.lock();
#endif
    const bool callbackFailed = g_domain.callbackFailure;
    clearContext(context);
    unregisterContext(context);
    g_domain.detaching = false;
    g_domain.callbackFailure = false;
    return callbackFailed ? LocalStorageResult::CallbackFailed
                          : LocalStorageResult::Success;
}

} // namespace

void installLocalStoragePlatformHooks(const LocalStoragePlatformHooks* hooks) {
    g_hooks = hooks == nullptr
        ? LocalStoragePlatformHooks{ nullptr, nullptr, nullptr, nullptr }
        : *hooks;
}

LocalStorageResult initializeLocalStorage() {
#if !defined(GXOS_BARE_METAL)
    std::lock_guard<std::mutex> lock(g_metadataMutex);
#endif
    if (g_domain.initialized || g_domain.detaching) {
        return LocalStorageResult::Busy;
    }
    g_domain.initialized = true;
    g_domain.callbackFailure = false;
    g_domain.contextCount = 0;
    for (gxos_local_storage_uint32 i = 0; i < kLocalStorageMaximumContexts; ++i) {
        g_domain.contexts[i] = nullptr;
    }
    for (gxos_local_storage_uint32 i = 0; i < kLocalStorageCapacity; ++i) {
        LocalStorageSlot& slot = g_domain.slots[i];
        if (slot.generation == 0) {
            slot.generation = 1;
        }
        slot.callback = nullptr;
        slot.active = false;
    }
    return LocalStorageResult::Success;
}

LocalStorageResult shutdownLocalStorage() {
#if !defined(GXOS_BARE_METAL)
    std::lock_guard<std::mutex> lock(g_metadataMutex);
#endif
    if (!g_domain.initialized) {
        return LocalStorageResult::NotInitialized;
    }
    if (g_domain.detaching || g_domain.contextCount != 0) {
        return LocalStorageResult::Busy;
    }
    for (gxos_local_storage_uint32 i = 0; i < kLocalStorageCapacity; ++i) {
        if (g_domain.slots[i].active) {
            return LocalStorageResult::Busy;
        }
    }
    // Advance every slot epoch so a pre-shutdown index cannot become valid
    // after a later initialize/allocate cycle.
    for (gxos_local_storage_uint32 i = 0; i < kLocalStorageCapacity; ++i) {
        LocalStorageSlot& slot = g_domain.slots[i];
        if (slot.generation != 0xFFFFFFFFu) {
            ++slot.generation;
        }
        slot.callback = nullptr;
        slot.active = false;
    }
    g_domain.initialized = false;
    return LocalStorageResult::Success;
}

bool isLocalStorageInitialized() {
#if !defined(GXOS_BARE_METAL)
    std::lock_guard<std::mutex> lock(g_metadataMutex);
#endif
    return g_domain.initialized;
}

LocalStorageResult attachLocalStorage() {
#if !defined(GXOS_BARE_METAL)
    std::lock_guard<std::mutex> lock(g_metadataMutex);
#endif
    if (!g_domain.initialized) {
        return LocalStorageResult::NotInitialized;
    }
    LocalStorageContext* context = currentContext();
    if (context == nullptr) {
        return LocalStorageResult::NoCurrentThread;
    }
    if (currentAttached()) {
        if (findContext(context) < 0) {
            return LocalStorageResult::Busy;
        }
        return LocalStorageResult::Success;
    }
    clearContext(context);
    if (!registerContext(context)) {
        return LocalStorageResult::Exhausted;
    }
    setCurrentAttached(true);
    return LocalStorageResult::Success;
}

LocalStorageResult detachLocalStorage() {
    LocalStorageContext* context = currentContext();
    if (!g_domain.initialized) {
        return LocalStorageResult::NotInitialized;
    }
    if (context == nullptr || !currentAttached()) {
        return LocalStorageResult::NoCurrentThread;
    }
    const LocalStorageResult result = detachContextInternal(context);
    if (result != LocalStorageResult::Busy && result != LocalStorageResult::NotInitialized) {
        setCurrentAttached(false);
    }
#if !defined(GXOS_BARE_METAL)
    g_hostLastDetach = result;
#endif
    return result;
}

LocalStorageResult detachLocalStorageContext(LocalStorageContext* context) {
    return detachContextInternal(context);
}

LocalStorageResult forceClearLocalStorageContext(LocalStorageContext* context) {
#if !defined(GXOS_BARE_METAL)
    std::lock_guard<std::mutex> lock(g_metadataMutex);
#endif
    if (!g_domain.initialized) {
        return LocalStorageResult::NotInitialized;
    }
    if (context == nullptr || g_domain.detaching) {
        return LocalStorageResult::Busy;
    }
    clearContext(context);
    unregisterContext(context);
    return LocalStorageResult::Success;
}

LocalStorageResult allocateLocalStorageIndex(LocalStorageDetachCallback callback,
                                             LocalStorageIndex* result) {
    if (result == nullptr) {
        return LocalStorageResult::Invalid;
    }
    *result = LocalStorageIndex{};
#if !defined(GXOS_BARE_METAL)
    std::lock_guard<std::mutex> lock(g_metadataMutex);
#endif
    if (!g_domain.initialized) {
        return LocalStorageResult::NotInitialized;
    }
    if (g_domain.detaching) {
        return LocalStorageResult::Busy;
    }
    for (gxos_local_storage_uint32 i = 0; i < kLocalStorageCapacity; ++i) {
        LocalStorageSlot& slot = g_domain.slots[i];
        if (!slot.active) {
            slot.callback = callback;
            slot.active = true;
            result->slot = i;
            result->generation = slot.generation;
            return LocalStorageResult::Success;
        }
    }
    return LocalStorageResult::Exhausted;
}

LocalStorageResult releaseLocalStorageIndex(LocalStorageIndex index) {
#if !defined(GXOS_BARE_METAL)
    std::lock_guard<std::mutex> lock(g_metadataMutex);
#endif
    if (!g_domain.initialized) {
        return LocalStorageResult::NotInitialized;
    }
    if (g_domain.detaching) {
        return LocalStorageResult::Busy;
    }
    const LocalStorageResult validation = validateIndex(index);
    if (validation != LocalStorageResult::Success) {
        return validation;
    }
    for (gxos_local_storage_uint32 i = 0; i < g_domain.contextCount; ++i) {
        if (g_domain.contexts[i]->values[index.slot] != nullptr) {
            return LocalStorageResult::Busy;
        }
    }
    LocalStorageSlot& slot = g_domain.slots[index.slot];
    slot.active = false;
    slot.callback = nullptr;
    if (slot.generation != 0xFFFFFFFFu) {
        ++slot.generation;
    }
    return LocalStorageResult::Success;
}

LocalStorageResult setLocalStorageValue(LocalStorageIndex index, void* value) {
#if !defined(GXOS_BARE_METAL)
    std::lock_guard<std::mutex> lock(g_metadataMutex);
#endif
    if (!g_domain.initialized) {
        return LocalStorageResult::NotInitialized;
    }
    if (g_domain.detaching) {
        g_domain.callbackFailure = true;
        return LocalStorageResult::Busy;
    }
    LocalStorageContext* context = currentContext();
    if (context == nullptr || !currentAttached()) {
        return LocalStorageResult::NoCurrentThread;
    }
    const LocalStorageResult validation = validateIndex(index);
    if (validation != LocalStorageResult::Success) {
        return validation;
    }
    context->values[index.slot] = value;
    return LocalStorageResult::Success;
}

LocalStorageResult getLocalStorageValue(LocalStorageIndex index, void** value) {
    if (value == nullptr) {
        return LocalStorageResult::Invalid;
    }
    *value = nullptr;
#if !defined(GXOS_BARE_METAL)
    std::lock_guard<std::mutex> lock(g_metadataMutex);
#endif
    if (!g_domain.initialized) {
        return LocalStorageResult::NotInitialized;
    }
    if (g_domain.detaching) {
        return LocalStorageResult::Busy;
    }
    LocalStorageContext* context = currentContext();
    if (context == nullptr || !currentAttached()) {
        return LocalStorageResult::NoCurrentThread;
    }
    const LocalStorageResult validation = validateIndex(index);
    if (validation != LocalStorageResult::Success) {
        return validation;
    }
    *value = context->values[index.slot];
    return LocalStorageResult::Success;
}

const char* localStorageResultName(LocalStorageResult result) {
    switch (result) {
        case LocalStorageResult::Success: return "Success";
        case LocalStorageResult::Invalid: return "Invalid";
        case LocalStorageResult::Exhausted: return "Exhausted";
        case LocalStorageResult::StaleIndex: return "StaleIndex";
        case LocalStorageResult::NoCurrentThread: return "NoCurrentThread";
        case LocalStorageResult::NotInitialized: return "NotInitialized";
        case LocalStorageResult::CallbackFailed: return "CallbackFailed";
        case LocalStorageResult::Busy: return "Busy";
    }
    return "Unknown";
}

} // namespace runtime
} // namespace gxos
