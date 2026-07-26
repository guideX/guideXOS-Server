#include "guidexos_nativeaot_pal_contract.h"

#include <stddef.h>

namespace {

guidexos_nativeaot_pal_hooks g_pal_hooks = {};
guidexos_nativeaot_fls_hooks g_fls_hooks = {};
#if !defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
guidexos_nativeaot_pal_hook_table_v1 g_hook_table = {};
bool g_hook_table_installed = false;
uint64_t g_hook_table_generation = 0;

struct WorkerToken {
    bool active;
    uint64_t installation_generation;
    guidexos_nativeaot_pal_worker_handle handle;
};

constexpr uint32_t kWorkerTokenCapacity = 8u;
WorkerToken g_worker_tokens[kWorkerTokenCapacity] = {};
#endif

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

#if !defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
bool hasCapability(uint64_t capabilities, uint64_t capability) {
    return (capabilities & capability) == capability;
}

bool validHookTable(const guidexos_nativeaot_pal_hook_table_v1* table) {
    if (table == nullptr || table->magic != GUIDEXOS_NATIVEAOT_PAL_HOOK_MAGIC ||
        table->abi_version != GUIDEXOS_NATIVEAOT_PAL_HOOK_ABI_VERSION ||
        table->structure_size < sizeof(guidexos_nativeaot_pal_hook_table_v1) ||
        table->installation_generation == 0 ||
        table->installation_generation > UINT32_MAX ||
        table->artifact_base == 0 || table->artifact_size == 0 ||
        table->artifact_size > UINT64_MAX - table->artifact_base ||
        !hasCapability(table->capability_bits,
                       GUIDEXOS_NATIVEAOT_PAL_CAP_REQUIRED)) {
        return false;
    }
    return table->current_thread_id != nullptr &&
           table->query_current_stack_bounds != nullptr &&
           table->fls_allocate != nullptr && table->fls_release != nullptr &&
           table->fls_get != nullptr && table->fls_set != nullptr &&
           table->create_worker != nullptr && table->join_worker != nullptr &&
           table->destroy_worker_handle != nullptr &&
           table->query_counter != nullptr &&
           table->query_counter_frequency != nullptr &&
           table->monotonic_milliseconds != nullptr &&
           table->sleep_milliseconds != nullptr && table->yield_thread != nullptr &&
           table->fail_fast != nullptr;
}

bool tableInstalled() {
    return g_hook_table_installed &&
           g_hook_table.magic == GUIDEXOS_NATIVEAOT_PAL_HOOK_MAGIC &&
           g_hook_table.abi_version == GUIDEXOS_NATIVEAOT_PAL_HOOK_ABI_VERSION &&
           g_hook_table.structure_size >= sizeof(guidexos_nativeaot_pal_hook_table_v1);
}

int32_t allocateWorkerToken(const guidexos_nativeaot_pal_worker_handle& handle,
                            guidexos_nativeaot_pal_opaque_handle* result) {
    if (result == nullptr || g_hook_table.installation_generation > UINT32_MAX) return -1;
    *result = 0;
    for (uint32_t index = 0; index < kWorkerTokenCapacity; ++index) {
        WorkerToken& token = g_worker_tokens[index];
        if (!token.active) {
            token.active = true;
            token.installation_generation = g_hook_table.installation_generation;
            token.handle = handle;
            *result = (static_cast<uintptr_t>(g_hook_table.installation_generation) << 32) |
                      static_cast<uintptr_t>(index + 1u);
            return 0;
        }
    }
    return -1;
}

WorkerToken* lookupWorkerToken(guidexos_nativeaot_pal_opaque_handle value) {
    const uint32_t encodedIndex = static_cast<uint32_t>(value & UINT64_C(0xFFFFFFFF));
    const uint32_t encodedGeneration = static_cast<uint32_t>(value >> 32);
    if (encodedIndex == 0 || encodedIndex > kWorkerTokenCapacity ||
        encodedGeneration == 0) return nullptr;
    WorkerToken& token = g_worker_tokens[encodedIndex - 1u];
    return token.active &&
           token.installation_generation == encodedGeneration &&
           token.installation_generation == g_hook_table.installation_generation
        ? &token : nullptr;
}
#endif

bool palInstalled() {
#if defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    return g_pal_hooks.size >= sizeof(g_pal_hooks) &&
           g_pal_hooks.abi_version == GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION;
#else
    return tableInstalled() || (g_pal_hooks.size >= sizeof(g_pal_hooks) &&
           g_pal_hooks.abi_version == GUIDEXOS_NATIVEAOT_PAL_ABI_VERSION);
#endif
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

#if !defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_install_hook_table(
    const guidexos_nativeaot_pal_hook_table_v1* table) {
    if (tableInstalled()) return -7;
    if (table == nullptr) return -1;
    if (table->magic != GUIDEXOS_NATIVEAOT_PAL_HOOK_MAGIC) return -2;
    if (table->abi_version != GUIDEXOS_NATIVEAOT_PAL_HOOK_ABI_VERSION) return -3;
    if (table->structure_size < sizeof(guidexos_nativeaot_pal_hook_table_v1)) return -4;
    if (!hasCapability(table->capability_bits,
                       GUIDEXOS_NATIVEAOT_PAL_CAP_REQUIRED)) return -5;
    if (!validHookTable(table)) return -6;
    g_hook_table = *table;
    g_hook_table_installed = true;
    g_hook_table_generation = table->installation_generation;
    return 0;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_uninstall_hook_table(void) {
    if (!tableInstalled()) return -1;
    for (uint32_t index = 0; index < kWorkerTokenCapacity; ++index) {
        if (g_worker_tokens[index].active) return -2;
    }
    g_hook_table = {};
    g_hook_table_installed = false;
    ++g_hook_table_generation;
    return 0;
}

extern "C" uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_hook_table_generation(void) {
    return g_hook_table_generation;
}
#endif

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_current_thread_id(uint64_t* result) {
    if (result == nullptr || !palInstalled()) return -1;
#if !defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    if (tableInstalled()) {
        *result = g_hook_table.current_thread_id();
        return *result == 0 ? -1 : 0;
    }
#endif
    *result = g_pal_hooks.current_thread_id(g_pal_hooks.context);
    return *result == 0 ? -1 : 0;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_stack_bounds(uintptr_t* low, uintptr_t* high, uintptr_t* current) {
    if (low == nullptr || high == nullptr || current == nullptr || !palInstalled()) return -1;
#if !defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    if (tableInstalled()) {
        guidexos_nativeaot_pal_stack_bounds_value bounds = {};
        const int32_t result = g_hook_table.query_current_stack_bounds(&bounds);
        *low = static_cast<uintptr_t>(bounds.low);
        *high = static_cast<uintptr_t>(bounds.high);
        *current = static_cast<uintptr_t>(bounds.current);
        return result;
    }
#endif
    return g_pal_hooks.stack_bounds(g_pal_hooks.context, low, high, current);
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_counter(uint64_t* result) {
    if (result == nullptr || !palInstalled()) return -1;
#if !defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    if (tableInstalled()) {
        *result = g_hook_table.query_counter();
        return 0;
    }
#endif
    *result = g_pal_hooks.counter(g_pal_hooks.context);
    return 0;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_frequency(uint64_t* result) {
    if (result == nullptr || !palInstalled()) return -1;
#if !defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    if (tableInstalled()) {
        *result = g_hook_table.query_counter_frequency();
        return *result == 0 ? -1 : 0;
    }
#endif
    *result = g_pal_hooks.frequency(g_pal_hooks.context);
    return *result == 0 ? -1 : 0;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_sleep(uint32_t milliseconds) {
    if (!palInstalled()) return -1;
#if !defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    if (tableInstalled()) {
        g_hook_table.sleep_milliseconds(milliseconds);
        return 0;
    }
#endif
    return
        g_pal_hooks.sleep_milliseconds(g_pal_hooks.context, milliseconds);
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_yield(void) {
    if (!palInstalled()) return -1;
#if !defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    if (tableInstalled()) {
        g_hook_table.yield_thread();
        return 0;
    }
#endif
    return g_pal_hooks.yield(g_pal_hooks.context);
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
#if defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    return !palInstalled() || g_pal_hooks.create_thread == nullptr
        ? -1 : g_pal_hooks.create_thread(g_pal_hooks.context, entry,
                                          entry_context, stack_size,
                                          high_priority, result);
#else
    if (!palInstalled() || result == nullptr || entry == nullptr) return -1;
    if (tableInstalled()) {
        guidexos_nativeaot_pal_worker_handle worker = {};
        const int32_t created = g_hook_table.create_worker(
            static_cast<guidexos_nativeaot_pal_win64_worker_entry>(entry),
            entry_context, stack_size, &worker);
        return created == 0 ? allocateWorkerToken(worker, result) : created;
    }
    return g_pal_hooks.create_thread(g_pal_hooks.context, entry, entry_context,
                                     stack_size, high_priority, result);
#endif
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_join_thread(guidexos_nativeaot_pal_opaque_handle handle,
                                    uint32_t timeout_milliseconds,
                                    uintptr_t* result) {
#if defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    return !palInstalled() || g_pal_hooks.join_thread == nullptr
        ? -1 : g_pal_hooks.join_thread(g_pal_hooks.context, handle,
                                        timeout_milliseconds, result);
#else
    if (!palInstalled()) return -1;
    if (tableInstalled()) {
        WorkerToken* token = lookupWorkerToken(handle);
        return token == nullptr ? -1 : g_hook_table.join_worker(
            token->handle, timeout_milliseconds, result);
    }
    return g_pal_hooks.join_thread(g_pal_hooks.context, handle,
                                   timeout_milliseconds, result);
#endif
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_close_thread(guidexos_nativeaot_pal_opaque_handle handle) {
#if defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    return !palInstalled() || g_pal_hooks.close_thread == nullptr
        ? -1 : g_pal_hooks.close_thread(g_pal_hooks.context, handle);
#else
    if (!palInstalled()) return -1;
    if (tableInstalled()) {
        WorkerToken* token = lookupWorkerToken(handle);
        if (token == nullptr) return -1;
        const int32_t result = g_hook_table.destroy_worker_handle(token->handle);
        if (result == 0) token->active = false;
        return result;
    }
    return g_pal_hooks.close_thread(g_pal_hooks.context, handle);
#endif
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
#if !defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    if (tableInstalled()) {
        g_hook_table.fail_fast(reason, 0);
    }
#endif
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
#if !defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    if (tableInstalled()) {
        return g_hook_table.fls_allocate(
            static_cast<guidexos_nativeaot_pal_win64_detach_callback>(callback));
    }
#endif
    return !flsInstalled() ? UINT32_MAX : g_fls_hooks.alloc(g_fls_hooks.context, callback);
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fls_free(uint32_t index) {
#if !defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    return tableInstalled() ? g_hook_table.fls_release(index) :
#else
    return
#endif
        (!flsInstalled() ? -1 : g_fls_hooks.free_index(g_fls_hooks.context, index));
}

extern "C" void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fls_get(uint32_t index) {
#if !defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    return tableInstalled() ? g_hook_table.fls_get(index) :
#else
    return
#endif
        (!flsInstalled() ? nullptr : g_fls_hooks.get(g_fls_hooks.context, index));
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_pal_fls_set(uint32_t index, void* value) {
#if !defined(GUIDEXOS_NATIVEAOT_PAL_ACTIVE_ARCHIVE)
    return tableInstalled() ? g_hook_table.fls_set(index, value) :
#else
    return
#endif
        (!flsInstalled() ? -1 : g_fls_hooks.set(g_fls_hooks.context, index, value));
}
