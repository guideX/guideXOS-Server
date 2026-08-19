#include "kernel/native_unwind_provider.h"

#include <stddef.h>

extern "C" const unsigned char __guidexos_native_image_base[];
extern "C" const unsigned char __guidexos_native_executable_start[];
extern "C" const unsigned char __guidexos_native_executable_end[];
extern "C" const unsigned char __guidexos_native_pdata_start[];
extern "C" const unsigned char __guidexos_native_pdata_end[];
extern "C" const unsigned char __guidexos_native_xdata_start[];
extern "C" const unsigned char __guidexos_native_xdata_end[];

namespace {

using RuntimeFunction = guidexos_nativeaot_runtime_function;

constexpr uint32_t kRegistryCapacity = 2u;
constexpr uint32_t kMaximumTableEntries = 8192u;

guidexos_nativeaot_native_unwind_module g_modules[kRegistryCapacity] = {};
uint32_t g_module_count = 0u;

bool addOverflow(uintptr_t left, uintptr_t right, uintptr_t* result) {
    if (result == nullptr || left > (~static_cast<uintptr_t>(0) - right)) {
        return true;
    }
    *result = left + right;
    return false;
}

bool validRange(uintptr_t start, uintptr_t end) {
    return start != 0u && start < end;
}

bool contains(uintptr_t start, uintptr_t end, uintptr_t value) {
    return validRange(start, end) && value >= start && value < end;
}

bool containsRange(uintptr_t start, uintptr_t end,
                   uintptr_t value, uintptr_t size) {
    uintptr_t valueEnd = 0u;
    return validRange(start, end) && size != 0u &&
           !addOverflow(value, size, &valueEnd) &&
           value >= start && valueEnd <= end;
}

bool resolveBaseRva(uintptr_t moduleBase, uint32_t rva, uintptr_t* address) {
    return address != nullptr && !addOverflow(
        moduleBase, static_cast<uintptr_t>(rva), address);
}

bool validateUnwindInfo(const guidexos_nativeaot_native_unwind_module& module,
                        uint32_t unwindData, uintptr_t* unwindInfo,
                        uint32_t* version, uint32_t* flags,
                        uint32_t* prologueSize, uint32_t* codeCount,
                        uint32_t* frameRegister, uint32_t* frameOffset) {
    uintptr_t address = 0u;
    if (!resolveBaseRva(static_cast<uintptr_t>(module.module_base), unwindData,
                        &address) ||
        !containsRange(static_cast<uintptr_t>(module.xdata_start),
                       static_cast<uintptr_t>(module.xdata_end), address, 4u)) {
        return false;
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(address);
    const uint32_t localVersion = bytes[0] & 0x07u;
    const uint32_t localFlags = bytes[0] >> 3;
    const uint32_t localCodeCount = bytes[2];
    const uint32_t slots = ((localCodeCount + 1u) & ~1u);
    if (localVersion != 1u || (localFlags & 0x04u) != 0u ||
        slots > (0xFFFFFFFFu - 4u) / 2u ||
        !containsRange(static_cast<uintptr_t>(module.xdata_start),
                       static_cast<uintptr_t>(module.xdata_end), address,
                       static_cast<uintptr_t>(4u + slots * 2u))) {
        return false;
    }
    if (unwindInfo != nullptr) *unwindInfo = address;
    if (version != nullptr) *version = localVersion;
    if (flags != nullptr) *flags = localFlags;
    if (prologueSize != nullptr) *prologueSize = bytes[1];
    if (codeCount != nullptr) *codeCount = localCodeCount;
    if (frameRegister != nullptr) *frameRegister = bytes[3] & 0x0Fu;
    if (frameOffset != nullptr) *frameOffset = bytes[3] >> 4;
    return true;
}

bool validateTable(guidexos_nativeaot_native_unwind_module* module) {
    if (module == nullptr || module->module_base == 0u ||
        !validRange(static_cast<uintptr_t>(module->executable_start),
                    static_cast<uintptr_t>(module->executable_end)) ||
        !validRange(static_cast<uintptr_t>(module->pdata_start),
                    static_cast<uintptr_t>(module->pdata_end)) ||
        !validRange(static_cast<uintptr_t>(module->xdata_start),
                    static_cast<uintptr_t>(module->xdata_end)) ||
        module->encoding != GUIDEXOS_NATIVEAOT_NATIVE_UNWIND_ENCODING_BASE_RVA) {
        return false;
    }
    const uintptr_t pdataBytes = static_cast<uintptr_t>(module->pdata_end) -
        static_cast<uintptr_t>(module->pdata_start);
    if (pdataBytes == 0u || pdataBytes % sizeof(RuntimeFunction) != 0u ||
        pdataBytes / sizeof(RuntimeFunction) > kMaximumTableEntries) {
        return false;
    }
    module->runtime_function_count = static_cast<uint32_t>(
        pdataBytes / sizeof(RuntimeFunction));
    module->runtime_function_table = module->pdata_start;

    const RuntimeFunction* table = reinterpret_cast<const RuntimeFunction*>(
        static_cast<uintptr_t>(module->runtime_function_table));
    bool sorted = true;
    uint32_t previousBegin = 0u;
    uint32_t previousEnd = 0u;
    for (uint32_t index = 0u; index < module->runtime_function_count; ++index) {
        const RuntimeFunction& entry = table[index];
        uintptr_t begin = 0u;
        uintptr_t end = 0u;
        if (entry.begin_address >= entry.end_address ||
            !resolveBaseRva(static_cast<uintptr_t>(module->module_base),
                            entry.begin_address, &begin) ||
            !resolveBaseRva(static_cast<uintptr_t>(module->module_base),
                            entry.end_address, &end) ||
            !containsRange(static_cast<uintptr_t>(module->executable_start),
                           static_cast<uintptr_t>(module->executable_end),
                           begin, end - begin) ||
            !validateUnwindInfo(*module, entry.unwind_data, nullptr, nullptr,
                                nullptr, nullptr, nullptr, nullptr, nullptr)) {
            return false;
        }
        if (index != 0u && entry.begin_address < previousBegin) sorted = false;
        if (index != 0u && sorted && entry.begin_address < previousEnd) {
            return false;
        }
        previousBegin = entry.begin_address;
        previousEnd = entry.end_address;
    }

    // The provider uses linear lookup regardless of this bit.  When the
    // linker does not preserve monotonic input order, validate overlaps with
    // a bounded startup-only pair scan; no scan or construction occurs after
    // EE suspension.
    if (!sorted) {
        if (module->runtime_function_count > kMaximumTableEntries) return false;
        for (uint32_t left = 0u; left < module->runtime_function_count; ++left) {
            const RuntimeFunction& a = table[left];
            for (uint32_t right = left + 1u;
                 right < module->runtime_function_count; ++right) {
                const RuntimeFunction& b = table[right];
                if (a.begin_address < b.end_address &&
                    b.begin_address < a.end_address) {
                    return false;
                }
            }
        }
    }
    module->table_sorted_by_begin = sorted ? 1u : 0u;
    module->validation_state = 1u;
    return true;
}

guidexos_nativeaot_native_unwind_module makeKernelModule() {
    guidexos_nativeaot_native_unwind_module module = {};
    module.module_base = reinterpret_cast<uintptr_t>(&__guidexos_native_image_base);
    module.executable_start = reinterpret_cast<uintptr_t>(
        &__guidexos_native_executable_start);
    module.executable_end = reinterpret_cast<uintptr_t>(
        &__guidexos_native_executable_end);
    module.pdata_start = reinterpret_cast<uintptr_t>(&__guidexos_native_pdata_start);
    module.pdata_end = reinterpret_cast<uintptr_t>(&__guidexos_native_pdata_end);
    module.xdata_start = reinterpret_cast<uintptr_t>(&__guidexos_native_xdata_start);
    module.xdata_end = reinterpret_cast<uintptr_t>(&__guidexos_native_xdata_end);
    module.encoding = GUIDEXOS_NATIVEAOT_NATIVE_UNWIND_ENCODING_BASE_RVA;
    return module;
}

} // namespace

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guideXosNativeUnwindRegisterKernelModule(void) {
    if (g_module_count != 0u) return g_modules[0].validation_state == 1u ? 0 : -1;
    if (g_module_count >= kRegistryCapacity) return -1;
    guidexos_nativeaot_native_unwind_module module = makeKernelModule();
    if (!validateTable(&module)) return -1;
    g_modules[g_module_count++] = module;
    return 0;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guideXosNativeUnwindLookup(
    uintptr_t control_pc,
    guidexos_nativeaot_native_unwind_lookup_result* result) {
    if (result == nullptr) return -1;
    *result = {};
    for (uint32_t moduleIndex = 0u; moduleIndex < g_module_count; ++moduleIndex) {
        const guidexos_nativeaot_native_unwind_module& module =
            g_modules[moduleIndex];
        if (module.validation_state != 1u ||
            !contains(static_cast<uintptr_t>(module.executable_start),
                      static_cast<uintptr_t>(module.executable_end), control_pc)) {
            continue;
        }
        const RuntimeFunction* table = reinterpret_cast<const RuntimeFunction*>(
            static_cast<uintptr_t>(module.runtime_function_table));
        for (uint32_t index = 0u; index < module.runtime_function_count; ++index) {
            const RuntimeFunction& entry = table[index];
            uintptr_t begin = 0u;
            uintptr_t end = 0u;
            if (resolveBaseRva(static_cast<uintptr_t>(module.module_base),
                               entry.begin_address, &begin) &&
                resolveBaseRva(static_cast<uintptr_t>(module.module_base),
                               entry.end_address, &end) &&
                control_pc >= begin && control_pc < end) {
                uintptr_t unwindInfo = 0u;
                uint32_t version = 0u;
                uint32_t flags = 0u;
                uint32_t prologueSize = 0u;
                uint32_t codeCount = 0u;
                uint32_t frameRegister = 0u;
                uint32_t frameOffset = 0u;
                if (!validateUnwindInfo(module, entry.unwind_data, &unwindInfo,
                                        &version, &flags, &prologueSize,
                                        &codeCount, &frameRegister,
                                        &frameOffset)) {
                    return -1;
                }
                result->module_base = module.module_base;
                result->executable_start = module.executable_start;
                result->executable_end = module.executable_end;
                result->pdata_start = module.pdata_start;
                result->pdata_end = module.pdata_end;
                result->xdata_start = module.xdata_start;
                result->xdata_end = module.xdata_end;
                result->runtime_function = reinterpret_cast<uintptr_t>(&table[index]);
                result->unwind_info = unwindInfo;
                result->begin_address = entry.begin_address;
                result->end_address = entry.end_address;
                result->unwind_data = entry.unwind_data;
                result->table_index = index;
                result->unwind_version = version;
                result->unwind_flags = flags;
                result->prologue_size = prologueSize;
                result->unwind_code_count = codeCount;
                result->frame_register = frameRegister;
                result->frame_offset = frameOffset;
                return 0;
            }
        }
        return -1;
    }
    return -1;
}

extern "C" const guidexos_nativeaot_native_unwind_module*
guideXosNativeUnwindGetKernelModule(void) {
    return g_module_count == 0u ? nullptr : &g_modules[0];
}

extern "C" uint32_t guideXosNativeUnwindRegistryCapacity(void) {
    return kRegistryCapacity;
}

extern "C" uint32_t guideXosNativeUnwindRegistryCount(void) {
    return g_module_count;
}

extern "C" int32_t GUIDEXOS_NATIVEAOT_PAL_CALL
guideXosNativeUnwindValidateFocusedCoverage(uint32_t* second_function_index) {
    if (second_function_index == nullptr || g_module_count == 0u ||
        g_modules[0].validation_state != 1u) {
        return -1;
    }
    const guidexos_nativeaot_native_unwind_module& module = g_modules[0];
    const RuntimeFunction* table = reinterpret_cast<const RuntimeFunction*>(
        static_cast<uintptr_t>(module.runtime_function_table));
    const uintptr_t base = static_cast<uintptr_t>(module.module_base);
    const auto expectLookup = [](uintptr_t pc, bool expectedHit) {
        guidexos_nativeaot_native_unwind_lookup_result result = {};
        const bool hit = guideXosNativeUnwindLookup(pc, &result) == 0;
        return hit == expectedHit;
    };
    const uint32_t count = module.runtime_function_count;
    if (count == 0u || !expectLookup(base + table[0].begin_address, true)) {
        return -1;
    }
    const uint32_t middle = count / 2u;
    const uint32_t last = count - 1u;
    if (!expectLookup(base + table[middle].begin_address, true) ||
        !expectLookup(base + table[last].begin_address, true) ||
        !expectLookup(base + table[last].end_address, false) ||
        !expectLookup(static_cast<uintptr_t>(module.executable_start) - 1u,
                      false) ||
        !expectLookup(static_cast<uintptr_t>(module.executable_end), false)) {
        return -1;
    }
    if (table[0].begin_address + 1u < table[0].end_address &&
        !expectLookup(base + table[0].begin_address + 1u, true)) {
        return -1;
    }
    if (table[middle].begin_address + 1u < table[middle].end_address &&
        !expectLookup(base + table[middle].begin_address + 1u, true)) {
        return -1;
    }
    for (uint32_t index = 1u; index < count; ++index) {
        if (table[index - 1u].end_address < table[index].begin_address) {
            const uint32_t gap = table[index - 1u].end_address +
                (table[index].begin_address - table[index - 1u].end_address) / 2u;
            if (!expectLookup(base + gap, false)) return -1;
            break;
        }
    }

    // Exercise the same validator against malformed copies without changing
    // the published descriptor or the linked table.
    guidexos_nativeaot_native_unwind_module malformed = module;
    malformed.encoding = 0u;
    if (validateTable(&malformed)) return -1;
    malformed = module;
    malformed.pdata_end = malformed.pdata_start + 1u;
    if (validateTable(&malformed)) return -1;

    *second_function_index = UINT32_MAX;
    for (uint32_t index = 1u; index < count; ++index) {
        uint32_t codeCount = 0u;
        if (validateUnwindInfo(module, table[index].unwind_data, nullptr, nullptr,
                               nullptr, nullptr, &codeCount, nullptr, nullptr) &&
            codeCount >= 2u) {
            *second_function_index = index;
            break;
        }
    }
    return *second_function_index == UINT32_MAX ? -1 : 0;
}
