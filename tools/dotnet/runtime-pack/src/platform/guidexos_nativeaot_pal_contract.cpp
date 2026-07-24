#include "guidexos_nativeaot_pal_contract.h"

#include <stddef.h>

namespace {

guidexos_nativeaot_pal_hooks g_pal_hooks = {};
guidexos_nativeaot_fls_hooks g_fls_hooks = {};

bool validPalHooks(const guidexos_nativeaot_pal_hooks* hooks) {
    return hooks != nullptr &&
           hooks->size >= sizeof(guidexos_nativeaot_pal_hooks) &&
           hooks->abi_version == GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION &&
           hooks->current_thread_id != nullptr &&
           hooks->stack_bounds != nullptr &&
           hooks->counter != nullptr &&
           hooks->frequency != nullptr &&
           hooks->sleep_milliseconds != nullptr &&
           hooks->yield != nullptr &&
           hooks->virtual_alloc != nullptr &&
           hooks->virtual_free != nullptr &&
           hooks->virtual_protect != nullptr;
}

bool validFlsHooks(const guidexos_nativeaot_fls_hooks* hooks) {
    return hooks != nullptr &&
           hooks->size >= sizeof(guidexos_nativeaot_fls_hooks) &&
           hooks->abi_version == GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION &&
           hooks->alloc != nullptr &&
           hooks->free_index != nullptr &&
           hooks->get != nullptr &&
           hooks->set != nullptr;
}

bool palInstalled() {
    return g_pal_hooks.size >= sizeof(g_pal_hooks) &&
           g_pal_hooks.abi_version == GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION;
}

bool flsInstalled() {
    return g_fls_hooks.size >= sizeof(g_fls_hooks) &&
           g_fls_hooks.abi_version == GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION;
}

} // namespace

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_install_hooks(const guidexos_nativeaot_pal_hooks* hooks) {
    if (hooks == nullptr) {
        g_pal_hooks = {};
        return 0;
    }
    if (!validPalHooks(hooks)) return -1;
    g_pal_hooks = *hooks;
    return 0;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_install_fls_hooks(const guidexos_nativeaot_fls_hooks* hooks) {
    if (hooks == nullptr) {
        g_fls_hooks = {};
        return 0;
    }
    if (!validFlsHooks(hooks)) return -1;
    g_fls_hooks = *hooks;
    return 0;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_current_thread_id(uint64_t* result) {
    if (result == nullptr || !palInstalled()) return -1;
    *result = g_pal_hooks.current_thread_id(g_pal_hooks.context);
    return *result == 0 ? -1 : 0;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_stack_bounds(uintptr_t* low, uintptr_t* high, uintptr_t* current) {
    if (low == nullptr || high == nullptr || current == nullptr || !palInstalled()) return -1;
    return g_pal_hooks.stack_bounds(g_pal_hooks.context, low, high, current);
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_counter(uint64_t* result) {
    if (result == nullptr || !palInstalled()) return -1;
    *result = g_pal_hooks.counter(g_pal_hooks.context);
    return 0;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_frequency(uint64_t* result) {
    if (result == nullptr || !palInstalled()) return -1;
    *result = g_pal_hooks.frequency(g_pal_hooks.context);
    return *result == 0 ? -1 : 0;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_sleep(uint32_t milliseconds) {
    return !palInstalled() ? -1 :
        g_pal_hooks.sleep_milliseconds(g_pal_hooks.context, milliseconds);
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_yield(void) {
    return !palInstalled() ? -1 : g_pal_hooks.yield(g_pal_hooks.context);
}

extern "C" uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fls_alloc(guidexos_nativeaot_fls_detach_callback callback) {
    return !flsInstalled() ? UINT32_MAX : g_fls_hooks.alloc(g_fls_hooks.context, callback);
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fls_free(uint32_t index) {
    return !flsInstalled() ? -1 : g_fls_hooks.free_index(g_fls_hooks.context, index);
}

extern "C" void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fls_get(uint32_t index) {
    return !flsInstalled() ? nullptr : g_fls_hooks.get(g_fls_hooks.context, index);
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fls_set(uint32_t index, void* value) {
    return !flsInstalled() ? -1 : g_fls_hooks.set(g_fls_hooks.context, index, value);
}

