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

extern "C" void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_virtual_alloc(void* preferred,
                                     uintptr_t size,
                                     uint32_t allocation_type,
                                     uint32_t protection) {
    return !palInstalled() ? nullptr : g_pal_hooks.virtual_alloc(
        g_pal_hooks.context, preferred, size, allocation_type, protection);
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_virtual_free(void* address,
                                    uintptr_t size,
                                    uint32_t free_type) {
    return !palInstalled() ? -1 : g_pal_hooks.virtual_free(
        g_pal_hooks.context, address, size, free_type);
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_virtual_protect(void* address,
                                       uintptr_t size,
                                       uint32_t protection,
                                       uint32_t* old_protection) {
    return !palInstalled() ? -1 : g_pal_hooks.virtual_protect(
        g_pal_hooks.context, address, size, protection, old_protection);
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_get_last_error(void) {
    return !palInstalled() || g_pal_hooks.get_last_error == nullptr
        ? -1 : g_pal_hooks.get_last_error(g_pal_hooks.context);
}

extern "C" void GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_set_last_error(int32_t value) {
    if (palInstalled() && g_pal_hooks.set_last_error != nullptr) {
        g_pal_hooks.set_last_error(g_pal_hooks.context, value);
    }
}

extern "C" void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_current_process(void) {
    return palInstalled() && g_pal_hooks.current_process != nullptr
        ? g_pal_hooks.current_process(g_pal_hooks.context) : nullptr;
}

extern "C" void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_current_thread(void) {
    return palInstalled() && g_pal_hooks.current_thread != nullptr
        ? g_pal_hooks.current_thread(g_pal_hooks.context) : nullptr;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_duplicate_handle(void* source_process,
                                        void* source_handle,
                                        void* target_process,
                                        void** target_handle,
                                        uint32_t access,
                                        int32_t inherit,
                                        uint32_t options) {
    return !palInstalled() || g_pal_hooks.duplicate_handle == nullptr
        ? -1 : g_pal_hooks.duplicate_handle(g_pal_hooks.context,
                                             source_process,
                                             source_handle,
                                             target_process,
                                             target_handle,
                                             access,
                                             inherit,
                                             options);
}

extern "C" void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_static_module_from_pointer(const void* pointer) {
    return palInstalled() && g_pal_hooks.static_module_from_pointer != nullptr
        ? g_pal_hooks.static_module_from_pointer(g_pal_hooks.context, pointer)
        : nullptr;
}

extern "C" void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_static_resolve(void* module, const char* name) {
    return palInstalled() && g_pal_hooks.static_resolve != nullptr
        ? g_pal_hooks.static_resolve(g_pal_hooks.context, module, name)
        : nullptr;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_create_thread(guidexos_nativeaot_thread_entry entry,
                                     void* entry_context,
                                     uint32_t stack_size,
                                     int32_t high_priority,
                                     guidexos_nativeaot_pal_opaque_handle* result) {
    return !palInstalled() || g_pal_hooks.create_thread == nullptr
        ? -1 : g_pal_hooks.create_thread(g_pal_hooks.context, entry,
                                          entry_context, stack_size,
                                          high_priority, result);
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_join_thread(guidexos_nativeaot_pal_opaque_handle handle,
                                   uint32_t timeout_milliseconds,
                                   uintptr_t* result) {
    return !palInstalled() || g_pal_hooks.join_thread == nullptr
        ? -1 : g_pal_hooks.join_thread(g_pal_hooks.context, handle,
                                        timeout_milliseconds, result);
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_close_thread(guidexos_nativeaot_pal_opaque_handle handle) {
    return !palInstalled() || g_pal_hooks.close_thread == nullptr
        ? -1 : g_pal_hooks.close_thread(g_pal_hooks.context, handle);
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_close_handle(void* handle) {
    return guidexos_nativeaot_pal_close_thread(
        reinterpret_cast<guidexos_nativeaot_pal_opaque_handle>(handle));
}

extern "C" uint8_t* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_current_teb(void) {
    return reinterpret_cast<uint8_t*>(guidexos_nativeaot_pal_current_thread());
}

extern "C" void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_create_event(int32_t manual_reset, int32_t initial_state) {
    return palInstalled() && g_pal_hooks.create_event != nullptr
        ? g_pal_hooks.create_event(g_pal_hooks.context, manual_reset, initial_state)
        : nullptr;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_wait_any(uint32_t timeout_milliseconds,
                                uint32_t count,
                                void* const* handles,
                                int32_t alertable) {
    return !palInstalled() || g_pal_hooks.wait_any == nullptr
        ? -1 : g_pal_hooks.wait_any(g_pal_hooks.context, timeout_milliseconds,
                                     count, handles, alertable);
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_close_event(void* handle) {
    return !palInstalled() || g_pal_hooks.close_event == nullptr
        ? -1 : g_pal_hooks.close_event(g_pal_hooks.context, handle);
}

extern "C" [[noreturn]] void GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fail_fast(uint32_t reason) {
    if (palInstalled() && g_pal_hooks.fail_fast != nullptr) {
        g_pal_hooks.fail_fast(g_pal_hooks.context, reason);
    }
    for (;;) {
    }
}

extern "C" [[noreturn]] void GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fail_fast_default(void) {
    guidexos_nativeaot_pal_fail_fast(0xE0000001u);
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
