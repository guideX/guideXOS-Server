#include "guidexos_nativeaot_fls_adapter.h"

namespace guidexos {
namespace nativeaot {
namespace fls {
namespace {

constexpr Index kCapacity = gxos::runtime::kLocalStorageCapacity;
gxos::runtime::LocalStorageIndex g_handles[kCapacity] = {};
bool g_handleActive[kCapacity] = {};

bool validAdapterIndex(Index index) {
    return index < kCapacity && g_handleActive[index];
}

} // namespace

void initialize() {
    (void)gxos::runtime::initializeLocalStorage();
    for (Index i = 0; i < kCapacity; ++i) {
        g_handles[i] = gxos::runtime::LocalStorageIndex{};
        g_handleActive[i] = false;
    }
}

void shutdown() {
    const gxos::runtime::LocalStorageResult result =
        gxos::runtime::shutdownLocalStorage();
    if (result == gxos::runtime::LocalStorageResult::Success) {
        for (Index i = 0; i < kCapacity; ++i) {
            g_handles[i] = gxos::runtime::LocalStorageIndex{};
            g_handleActive[i] = false;
        }
    }
}

void installPlatformHooks(const gxos::runtime::LocalStoragePlatformHooks* hooks) {
    gxos::runtime::installLocalStoragePlatformHooks(hooks);
}

bool attachCurrentThread() {
    return gxos::runtime::attachLocalStorage() ==
        gxos::runtime::LocalStorageResult::Success;
}

bool detachCurrentThread() {
    const gxos::runtime::LocalStorageResult result =
        gxos::runtime::detachLocalStorage();
    return result == gxos::runtime::LocalStorageResult::Success ||
           result == gxos::runtime::LocalStorageResult::CallbackFailed;
}

Index alloc(Callback callback) {
    gxos::runtime::LocalStorageIndex handle{};
    const gxos::runtime::LocalStorageResult result =
        gxos::runtime::allocateLocalStorageIndex(
            reinterpret_cast<gxos::runtime::LocalStorageDetachCallback>(callback),
            &handle);
    if (result != gxos::runtime::LocalStorageResult::Success ||
        handle.slot >= kCapacity) {
        return kOutOfIndexes;
    }
    g_handles[handle.slot] = handle;
    g_handleActive[handle.slot] = true;
    return handle.slot;
}

bool free(Index index) {
    if (!validAdapterIndex(index)) {
        return false;
    }
    const gxos::runtime::LocalStorageResult result =
        gxos::runtime::releaseLocalStorageIndex(g_handles[index]);
    if (result != gxos::runtime::LocalStorageResult::Success &&
        result != gxos::runtime::LocalStorageResult::CallbackFailed) {
        return false;
    }
    g_handleActive[index] = false;
    g_handles[index] = gxos::runtime::LocalStorageIndex{};
    return result == gxos::runtime::LocalStorageResult::Success;
}

void* get(Index index) {
    if (!validAdapterIndex(index)) {
        return nullptr;
    }
    void* value = nullptr;
    return gxos::runtime::getLocalStorageValue(g_handles[index], &value) ==
        gxos::runtime::LocalStorageResult::Success ? value : nullptr;
}

bool set(Index index, void* value) {
    return validAdapterIndex(index) &&
        gxos::runtime::setLocalStorageValue(g_handles[index], value) ==
        gxos::runtime::LocalStorageResult::Success;
}

} // namespace fls
} // namespace nativeaot
} // namespace guidexos

extern "C" void guidexos_nativeaot_fls_initialize() {
    guidexos::nativeaot::fls::initialize();
}

extern "C" void guidexos_nativeaot_fls_shutdown() {
    guidexos::nativeaot::fls::shutdown();
}

extern "C" int guidexos_nativeaot_fls_attach_current_thread() {
    return guidexos::nativeaot::fls::attachCurrentThread() ? 1 : 0;
}

extern "C" int guidexos_nativeaot_fls_detach_current_thread() {
    return guidexos::nativeaot::fls::detachCurrentThread() ? 1 : 0;
}

extern "C" unsigned long guidexos_nativeaot_fls_alloc(
    guidexos::nativeaot::fls::Callback callback) {
    return guidexos::nativeaot::fls::alloc(callback);
}

extern "C" int guidexos_nativeaot_fls_free(unsigned long index) {
    return guidexos::nativeaot::fls::free(
        static_cast<guidexos::nativeaot::fls::Index>(index)) ? 1 : 0;
}

extern "C" void* guidexos_nativeaot_fls_get(unsigned long index) {
    return guidexos::nativeaot::fls::get(
        static_cast<guidexos::nativeaot::fls::Index>(index));
}

extern "C" int guidexos_nativeaot_fls_set(unsigned long index, void* value) {
    return guidexos::nativeaot::fls::set(
        static_cast<guidexos::nativeaot::fls::Index>(index), value) ? 1 : 0;
}
