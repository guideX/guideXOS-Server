#include "guidexos_nativeaot_pal_contract.h"

#include <stdint.h>
#include <windows.h>

#define GUIDEXOS_NATIVEAOT_PAL_EXPORT extern "C"
#define GUIDEXOS_NATIVEAOT_PAL_API __stdcall

// This object deliberately keeps the NativeAOT PAL export ABI at its public
// Windows-shaped edge.  All guideXOS calls below cross only the fixed-width C
// contract; no guideXOS C++ object or runtime layout is exposed here.

uint32_t g_RhNumberOfProcessors = 1u;

GUIDEXOS_NATIVEAOT_PAL_EXPORT void GUIDEXOS_NATIVEAOT_PAL_API
PalGetModuleBounds(HANDLE module,
                   uint8_t** lower,
                   uint8_t** upper) {
    if (lower == nullptr || upper == nullptr || module == nullptr) {
        if (lower != nullptr) *lower = nullptr;
        if (upper != nullptr) *upper = nullptr;
        return;
    }

    const uint8_t* base = reinterpret_cast<const uint8_t*>(module);
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) {
        *lower = nullptr;
        *upper = nullptr;
        return;
    }

    const IMAGE_NT_HEADERS64* headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + static_cast<uintptr_t>(dos->e_lfanew));
    if (headers->Signature != IMAGE_NT_SIGNATURE ||
        headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        headers->OptionalHeader.SizeOfImage == 0) {
        *lower = nullptr;
        *upper = nullptr;
        return;
    }
    *lower = const_cast<uint8_t*>(base);
    *upper = const_cast<uint8_t*>(base + headers->OptionalHeader.SizeOfImage - 1u);
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT int32_t GUIDEXOS_NATIVEAOT_PAL_API
PalGetProcessCpuCount() {
    return g_RhNumberOfProcessors == 0 ? 1 : static_cast<int32_t>(g_RhNumberOfProcessors);
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT bool GUIDEXOS_NATIVEAOT_PAL_API
PalGetMaximumStackBounds(void** stack_low, void** stack_high) {
    uintptr_t low = 0;
    uintptr_t high = 0;
    uintptr_t current = 0;
    if (stack_low == nullptr || stack_high == nullptr ||
        guidexos_nativeaot_pal_stack_bounds(&low, &high, &current) != 0) {
        return false;
    }
    *stack_low = reinterpret_cast<void*>(low);
    *stack_high = reinterpret_cast<void*>(high);
    return low < high && current >= low && current < high;
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT int32_t
PalGetModuleFileName(const wchar_t** module_name, HANDLE module) {
    // Only the current-image token is supported.  Dynamic module loading and
    // arbitrary foreign image inspection are deliberately rejected by the
    // replacement contract.
    static const wchar_t current_image_name[] = L"guideXOS-nativeaot-image";
    if (module_name == nullptr || module != nullptr) {
        if (module_name != nullptr) *module_name = nullptr;
        return 0;
    }
    *module_name = current_image_name;
    return static_cast<int32_t>((sizeof(current_image_name) / sizeof(wchar_t)) - 1u);
}

GUIDEXOS_NATIVEAOT_PAL_EXPORT uint64_t GUIDEXOS_NATIVEAOT_PAL_API
PalGetTickCount64() {
    uint64_t counter = 0;
    uint64_t frequency = 0;
    if (guidexos_nativeaot_pal_counter(&counter) != 0 ||
        guidexos_nativeaot_pal_frequency(&frequency) != 0 || frequency == 0) {
        return 0;
    }
    return counter / frequency > UINT64_MAX / 1000u
        ? UINT64_MAX
        : (counter / frequency) * 1000u +
            ((counter % frequency) * 1000u) / frequency;
}
