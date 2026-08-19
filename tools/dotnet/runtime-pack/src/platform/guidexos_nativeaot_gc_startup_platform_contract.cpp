#include "guidexos_nativeaot_gc_startup_platform_contract.h"

#include <stddef.h>

namespace {

guidexos_nativeaot_gc_startup_platform_table_v1 g_table = {};
bool g_installed = false;
uint64_t g_generation = 0;

bool valid(const guidexos_nativeaot_gc_startup_platform_table_v1* table) {
    if (table == nullptr || table->magic != GUIDEXOS_NATIVEAOT_GC_PLATFORM_MAGIC ||
        table->abi_version != GUIDEXOS_NATIVEAOT_GC_PLATFORM_ABI_VERSION ||
        table->structure_size < sizeof(guidexos_nativeaot_gc_startup_platform_table_v1) ||
        table->installation_generation == 0 ||
        (table->capability_bits & GUIDEXOS_NATIVEAOT_GC_CAP_REQUIRED) !=
            GUIDEXOS_NATIVEAOT_GC_CAP_REQUIRED) {
        return false;
    }
    return table->create_event != nullptr && table->set_event != nullptr &&
           table->reset_event != nullptr && table->wait_event != nullptr &&
           table->close_event != nullptr && table->reserve != nullptr &&
           table->commit != nullptr && table->decommit != nullptr &&
           table->release != nullptr && table->reset != nullptr &&
           table->page_size != nullptr && table->allocation_granularity != nullptr &&
           table->virtual_memory_limit != nullptr &&
           table->physical_memory_limit != nullptr && table->memory_status != nullptr;
}

bool installed() {
    return g_installed && valid(&g_table);
}

} // namespace

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_install_startup_platform_hooks(
    const guidexos_nativeaot_gc_startup_platform_table_v1* table) {
    if (g_installed || !valid(table)) return -1;
    g_table = *table;
    g_installed = true;
    g_generation = table->installation_generation;
    return 0;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_uninstall_startup_platform_hooks(void) {
    if (!installed()) return -1;
    g_table = {};
    g_installed = false;
    ++g_generation;
    return 0;
}

extern "C" uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_startup_platform_generation(void) {
    return g_generation;
}

extern "C" uintptr_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_get_native_continuation_hook(void) {
    return installed() ? static_cast<uintptr_t>(g_table.reserved[0]) : 0u;
}

extern "C" uintptr_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_get_native_unwind_lookup_hook(void) {
    return installed()
        ? static_cast<uintptr_t>(g_table.reserved[
              GUIDEXOS_NATIVEAOT_NATIVE_UNWIND_PLATFORM_RESERVED_INDEX])
        : 0u;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_native_unwind_lookup(
    uintptr_t control_pc,
    guidexos_nativeaot_native_unwind_lookup_result* result) {
    const uintptr_t hookAddress =
        guidexos_nativeaot_gc_get_native_unwind_lookup_hook();
    if (hookAddress == 0u || result == nullptr) return -1;
    const auto hook = reinterpret_cast<guidexos_nativeaot_native_unwind_lookup_hook>(
        hookAddress);
    return hook(control_pc, result);
}

extern "C" void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_create_event(uint32_t manual_reset, uint32_t initial_state) {
    return installed() ? g_table.create_event(manual_reset, initial_state) : nullptr;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_set_event(void* handle) {
    return installed() ? g_table.set_event(handle) : -1;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_reset_event(void* handle) {
    return installed() ? g_table.reset_event(handle) : -1;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_wait_event(void* handle, uint32_t timeout_milliseconds) {
    return installed() ? g_table.wait_event(handle, timeout_milliseconds) : -1;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_close_event(void* handle) {
    return installed() ? g_table.close_event(handle) : -1;
}

extern "C" void* GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_reserve(uintptr_t size, uintptr_t alignment,
                              uint32_t flags, uint16_t node) {
    return installed() ? g_table.reserve(size, alignment, flags, node) : nullptr;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_commit(void* address, uintptr_t size, uint16_t node) {
    return installed() ? g_table.commit(address, size, node) : -1;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_decommit(void* address, uintptr_t size) {
    return installed() ? g_table.decommit(address, size, 0xFFFFu) : -1;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_release(void* address, uintptr_t size) {
    return installed() ? g_table.release(address, size, 0xFFFFu) : -1;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_reset(void* address, uintptr_t size, uint32_t unlock) {
    return installed() ? g_table.reset(address, size, unlock) : -1;
}

extern "C" uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_page_size(void) {
    return installed() ? g_table.page_size() : 0;
}

extern "C" uint32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_allocation_granularity(void) {
    return installed() ? g_table.allocation_granularity() : 0;
}

extern "C" uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_virtual_memory_limit(void) {
    return installed() ? g_table.virtual_memory_limit() : 0;
}

extern "C" uint64_t GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_physical_memory_limit(uint32_t* restricted) {
    return installed() ? g_table.physical_memory_limit(restricted) : 0;
}

extern "C" void GUIDEXOS_NATIVEAOT_PAL_CALL
guidexos_nativeaot_gc_memory_status(uint64_t restricted_limit,
                                    uint32_t* memory_load,
                                    uint64_t* available_physical,
                                    uint64_t* available_page_file) {
    if (installed()) {
        g_table.memory_status(restricted_limit, memory_load, available_physical,
                              available_page_file);
    }
}
