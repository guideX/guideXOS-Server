#include "guidexos_local_storage.h"

#if !defined(GXOS_BARE_METAL)
#include <atomic>
#include <mutex>
#endif

namespace gxos {
namespace runtime {
namespace {

struct LocalStorageSlot {
#if !defined(GXOS_BARE_METAL)
    std::atomic<LocalStorageDetachCallback> callback;
    std::atomic<gxos_local_storage_uint32> generation;
    std::atomic<bool> active;
#else
    LocalStorageDetachCallback callback;
    gxos_local_storage_uint32 generation;
    bool active;
#endif
};

struct LocalStorageDomain {
#if !defined(GXOS_BARE_METAL)
    std::atomic<bool> initialized;
    std::atomic<bool> detaching;
    std::atomic<bool> callbackFailure;
#else
    bool initialized;
    bool detaching;
    bool callbackFailure;
#endif
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
#endif

bool domainInitialized() {
#if !defined(GXOS_BARE_METAL)
    return g_domain.initialized.load(std::memory_order_acquire);
#else
    return g_domain.initialized;
#endif
}

void setDomainInitialized(bool value) {
#if !defined(GXOS_BARE_METAL)
    g_domain.initialized.store(value, std::memory_order_release);
#else
    g_domain.initialized = value;
#endif
}

bool domainDetaching() {
#if !defined(GXOS_BARE_METAL)
    return g_domain.detaching.load(std::memory_order_acquire);
#else
    return g_domain.detaching;
#endif
}

void setDomainDetaching(bool value) {
#if !defined(GXOS_BARE_METAL)
    g_domain.detaching.store(value, std::memory_order_release);
#else
    g_domain.detaching = value;
#endif
}

bool callbackFailureRecorded() {
#if !defined(GXOS_BARE_METAL)
    return g_domain.callbackFailure.load(std::memory_order_acquire);
#else
    return g_domain.callbackFailure;
#endif
}

void recordCallbackFailure() {
#if !defined(GXOS_BARE_METAL)
    g_domain.callbackFailure.store(true, std::memory_order_release);
#else
    g_domain.callbackFailure = true;
#endif
}

void clearCallbackFailure() {
#if !defined(GXOS_BARE_METAL)
    g_domain.callbackFailure.store(false, std::memory_order_release);
#else
    g_domain.callbackFailure = false;
#endif
}

LocalStorageDetachCallback slotCallback(const LocalStorageSlot& slot) {
#if !defined(GXOS_BARE_METAL)
    return slot.callback.load(std::memory_order_acquire);
#else
    return slot.callback;
#endif
}

void setSlotCallback(LocalStorageSlot& slot, LocalStorageDetachCallback callback) {
#if !defined(GXOS_BARE_METAL)
    slot.callback.store(callback, std::memory_order_release);
#else
    slot.callback = callback;
#endif
}

gxos_local_storage_uint32 slotGeneration(const LocalStorageSlot& slot) {
#if !defined(GXOS_BARE_METAL)
    return slot.generation.load(std::memory_order_acquire);
#else
    return slot.generation;
#endif
}

void setSlotGeneration(LocalStorageSlot& slot,
                       gxos_local_storage_uint32 generation) {
#if !defined(GXOS_BARE_METAL)
    slot.generation.store(generation, std::memory_order_release);
#else
    slot.generation = generation;
#endif
}

bool slotActive(const LocalStorageSlot& slot) {
#if !defined(GXOS_BARE_METAL)
    return slot.active.load(std::memory_order_acquire);
#else
    return slot.active;
#endif
}

void setSlotActive(LocalStorageSlot& slot, bool active) {
#if !defined(GXOS_BARE_METAL)
    slot.active.store(active, std::memory_order_release);
#else
    slot.active = active;
#endif
}

// Values are pointer-sized and fixed in each attached context.  The compiler
// atomics make release-time scans and lock-free get/set well-defined on hosted
// builds without changing the freestanding context layout.
void* loadContextValue(const LocalStorageContext* context,
                       gxos_local_storage_uint32 slot) {
    if (context == nullptr || slot >= kLocalStorageCapacity) {
        return nullptr;
    }
#if defined(__GNUC__) || defined(__clang__)
    return __atomic_load_n(&context->values[slot], __ATOMIC_ACQUIRE);
#else
    return context->values[slot];
#endif
}

void storeContextValue(LocalStorageContext* context,
                       gxos_local_storage_uint32 slot,
                       void* value) {
    if (context == nullptr || slot >= kLocalStorageCapacity) {
        return;
    }
#if defined(__GNUC__) || defined(__clang__)
    __atomic_store_n(&context->values[slot], value, __ATOMIC_RELEASE);
#else
    context->values[slot] = value;
#endif
}

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
        storeContextValue(context, i, nullptr);
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
    if (!slotActive(slot) || slotGeneration(slot) != index.generation) {
        return LocalStorageResult::StaleIndex;
    }
    return LocalStorageResult::Success;
}

struct PendingCallback {
    LocalStorageDetachCallback callback;
    void* value;
};

LocalStorageResult invokePendingCallbacks(PendingCallback* pending,
                                          gxos_local_storage_uint32 count) {
    for (gxos_local_storage_uint32 i = 0; i < count; ++i) {
        pending[i].callback(pending[i].value);
    }
    return callbackFailureRecorded()
        ? LocalStorageResult::CallbackFailed
        : LocalStorageResult::Success;
}

LocalStorageResult detachContextInternal(LocalStorageContext* context) {
#if !defined(GXOS_BARE_METAL)
    std::unique_lock<std::mutex> lock(g_metadataMutex);
#endif
    if (!domainInitialized()) {
        return LocalStorageResult::NotInitialized;
    }
    if (context == nullptr || findContext(context) < 0) {
        return LocalStorageResult::NoCurrentThread;
    }
    if (domainDetaching()) {
        return LocalStorageResult::Busy;
    }

    PendingCallback pending[kLocalStorageCapacity] = {};
    gxos_local_storage_uint32 pendingCount = 0;
    setDomainDetaching(true);
    clearCallbackFailure();

    // Stage and clear every value before entering callbacks.  This is the
    // selected bounded policy for platform teardown callbacks: a callback gets
    // one pass per non-null slot and repopulation is rejected during the pass.
    for (gxos_local_storage_uint32 i = 0; i < kLocalStorageCapacity; ++i) {
        const LocalStorageSlot& slot = g_domain.slots[i];
        const LocalStorageDetachCallback callback = slotCallback(slot);
        void* value = loadContextValue(context, i);
        if (slotActive(slot) && callback != nullptr && value != nullptr) {
            pending[pendingCount].callback = callback;
            pending[pendingCount].value = value;
            ++pendingCount;
        }
        storeContextValue(context, i, nullptr);
    }

#if !defined(GXOS_BARE_METAL)
    // Callbacks may call manager APIs.  They observe detaching and receive
    // Busy; the metadata mutex is not held across callback code.
    lock.unlock();
#endif
    const LocalStorageResult callbackResult =
        invokePendingCallbacks(pending, pendingCount);
#if !defined(GXOS_BARE_METAL)
    lock.lock();
#endif

    clearContext(context);
    unregisterContext(context);
    setDomainDetaching(false);
    clearCallbackFailure();
    return callbackResult;
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
    if (domainInitialized() || domainDetaching()) {
        return LocalStorageResult::Busy;
    }
    setDomainInitialized(true);
    clearCallbackFailure();
    g_domain.contextCount = 0;
    for (gxos_local_storage_uint32 i = 0; i < kLocalStorageMaximumContexts; ++i) {
        g_domain.contexts[i] = nullptr;
    }
    for (gxos_local_storage_uint32 i = 0; i < kLocalStorageCapacity; ++i) {
        LocalStorageSlot& slot = g_domain.slots[i];
        if (slotGeneration(slot) == 0) {
            setSlotGeneration(slot, 1);
        }
        setSlotCallback(slot, nullptr);
        setSlotActive(slot, false);
    }
    return LocalStorageResult::Success;
}

LocalStorageResult shutdownLocalStorage() {
#if !defined(GXOS_BARE_METAL)
    std::lock_guard<std::mutex> lock(g_metadataMutex);
#endif
    if (!domainInitialized()) {
        return LocalStorageResult::NotInitialized;
    }
    if (domainDetaching() || g_domain.contextCount != 0) {
        return LocalStorageResult::Busy;
    }
    for (gxos_local_storage_uint32 i = 0; i < kLocalStorageCapacity; ++i) {
        if (slotActive(g_domain.slots[i])) {
            return LocalStorageResult::Busy;
        }
    }
    for (gxos_local_storage_uint32 i = 0; i < kLocalStorageCapacity; ++i) {
        LocalStorageSlot& slot = g_domain.slots[i];
        gxos_local_storage_uint32 generation = slotGeneration(slot);
        if (generation != 0xFFFFFFFFu) {
            ++generation;
        }
        setSlotGeneration(slot, generation);
        setSlotCallback(slot, nullptr);
        setSlotActive(slot, false);
    }
    setDomainInitialized(false);
    return LocalStorageResult::Success;
}

bool isLocalStorageInitialized() {
#if !defined(GXOS_BARE_METAL)
    std::lock_guard<std::mutex> lock(g_metadataMutex);
#endif
    return domainInitialized();
}

LocalStorageResult attachLocalStorage() {
#if !defined(GXOS_BARE_METAL)
    std::lock_guard<std::mutex> lock(g_metadataMutex);
#endif
    if (!domainInitialized()) {
        return LocalStorageResult::NotInitialized;
    }
    LocalStorageContext* context = currentContext();
    if (context == nullptr) {
        return LocalStorageResult::NoCurrentThread;
    }
    if (currentAttached()) {
        return findContext(context) < 0
            ? LocalStorageResult::Busy
            : LocalStorageResult::Success;
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
    if (!domainInitialized()) {
        return LocalStorageResult::NotInitialized;
    }
    if (context == nullptr || !currentAttached()) {
        return LocalStorageResult::NoCurrentThread;
    }
    const LocalStorageResult result = detachContextInternal(context);
    if (result != LocalStorageResult::Busy &&
        result != LocalStorageResult::NotInitialized) {
        setCurrentAttached(false);
    }
    return result;
}

LocalStorageResult detachLocalStorageContext(LocalStorageContext* context) {
    return detachContextInternal(context);
}

LocalStorageResult forceClearLocalStorageContext(LocalStorageContext* context) {
#if !defined(GXOS_BARE_METAL)
    std::lock_guard<std::mutex> lock(g_metadataMutex);
#endif
    if (!domainInitialized()) {
        return LocalStorageResult::NotInitialized;
    }
    if (context == nullptr || domainDetaching()) {
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
    if (!domainInitialized()) {
        return LocalStorageResult::NotInitialized;
    }
    if (domainDetaching()) {
        return LocalStorageResult::Busy;
    }
    for (gxos_local_storage_uint32 i = 0; i < kLocalStorageCapacity; ++i) {
        LocalStorageSlot& slot = g_domain.slots[i];
        if (!slotActive(slot)) {
            // Publish callback metadata before active so lock-free readers
            // never observe a partially initialized active slot.
            setSlotCallback(slot, callback);
            setSlotActive(slot, true);
            result->slot = i;
            result->generation = slotGeneration(slot);
            return LocalStorageResult::Success;
        }
    }
    return LocalStorageResult::Exhausted;
}

LocalStorageResult releaseLocalStorageIndex(LocalStorageIndex index) {
#if !defined(GXOS_BARE_METAL)
    std::unique_lock<std::mutex> lock(g_metadataMutex);
#endif
    if (!domainInitialized()) {
        return LocalStorageResult::NotInitialized;
    }
    if (domainDetaching()) {
        return LocalStorageResult::Busy;
    }
    const LocalStorageResult validation = validateIndex(index);
    if (validation != LocalStorageResult::Success) {
        return validation;
    }

    // Clear each live context value and invoke the registered callback once
    // per non-null value.  The operation is bounded by
    // kLocalStorageMaximumContexts.  A platform adapter may use this as its
    // index-release cleanup operation.
    PendingCallback pending[kLocalStorageMaximumContexts] = {};
    gxos_local_storage_uint32 pendingCount = 0;
    LocalStorageSlot& slot = g_domain.slots[index.slot];
    const LocalStorageDetachCallback callback = slotCallback(slot);
    setDomainDetaching(true);
    clearCallbackFailure();
    for (gxos_local_storage_uint32 i = 0; i < g_domain.contextCount; ++i) {
        LocalStorageContext* context = g_domain.contexts[i];
        void* value = loadContextValue(context, index.slot);
        if (callback != nullptr && value != nullptr) {
            pending[pendingCount].callback = callback;
            pending[pendingCount].value = value;
            ++pendingCount;
        }
        storeContextValue(context, index.slot, nullptr);
    }
#if !defined(GXOS_BARE_METAL)
    lock.unlock();
#endif
    const LocalStorageResult callbackResult =
        invokePendingCallbacks(pending, pendingCount);
#if !defined(GXOS_BARE_METAL)
    lock.lock();
#endif
    setSlotActive(slot, false);
    setSlotCallback(slot, nullptr);
    gxos_local_storage_uint32 generation = slotGeneration(slot);
    if (generation != 0xFFFFFFFFu) {
        ++generation;
    }
    setSlotGeneration(slot, generation);
    setDomainDetaching(false);
    clearCallbackFailure();
    return callbackResult;
}

// get/set deliberately do not take g_metadataMutex.  Their contract assumes
// index release and manager shutdown are quiescent operations, as they are in
// runtime startup/teardown and the single-CPU guideXOS scheduler.  They still
// use acquire/release reads for context values and metadata state, so ordinary
// access performs no allocation, wait, or blocking operation.
LocalStorageResult setLocalStorageValue(LocalStorageIndex index, void* value) {
    if (!domainInitialized()) {
        return LocalStorageResult::NotInitialized;
    }
    if (domainDetaching()) {
        recordCallbackFailure();
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
    storeContextValue(context, index.slot, value);
    return LocalStorageResult::Success;
}

LocalStorageResult getLocalStorageValue(LocalStorageIndex index, void** value) {
    if (value == nullptr) {
        return LocalStorageResult::Invalid;
    }
    *value = nullptr;
    if (!domainInitialized()) {
        return LocalStorageResult::NotInitialized;
    }
    if (domainDetaching()) {
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
    *value = loadContextValue(context, index.slot);
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
