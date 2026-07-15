#include <intrin.h>

#pragma intrinsic(__readgsqword)

namespace {

using gx_uintptr = unsigned __int64;
using gx_uint32 = unsigned long;
using gx_uint16 = unsigned short;
using gx_size = unsigned __int64;

// Keep this object independent of Windows headers. The numeric reason is the
// FAST_FAIL_FATAL_APP_EXIT value used by the stock helper; only the fail-fast
// instruction is part of this hosted pack's contract.
constexpr gx_uint32 kFastFailFatalAppExit = 7u;

constexpr gx_uint32 kFlsOutOfIndexes = 0xFFFFFFFFu;
constexpr gx_uint32 kGuideXosFlsIndex = 0u;
constexpr gx_uint32 kTlsVectorOffset = 0x58u;
constexpr gx_uint32 kRuntimeCellOffset = 0x30u;
constexpr gx_uint32 kRuntimeCellInitializedOffset = 0x38u;
constexpr gx_uint32 kRuntimeCellTransitionFrameOffset = 0x40u;
constexpr gx_uint32 kFlsCellOffset = 0x80u;
constexpr gx_uint32 kFlsCellCount = 8u;
constexpr gx_uint32 kMinimumTlsBlockSize = 0x110u;

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
constexpr gx_size kManagedHeapBytes = 0x10000u;
constexpr gx_uint32 kManagedArrayDataOffset = 0x10u;

struct GuideXosAllocationDiagnostics {
    gx_uint32 heapInitialized;
    gx_uint32 allocationCount;
    gx_uint32 requestedArrayLength;
    gx_uint32 requestedObjectSize;
    gx_uint32 outOfMemory;
    gx_uint32 collectionRequested;
    gx_uint32 gcSuspensionEntered;
    gx_uint32 reserved;
    gx_uintptr heapBase;
    gx_uintptr heapSize;
    gx_uintptr initialAllocationPointer;
    gx_uintptr allocationPointerAfter;
    gx_uintptr returnedObject;
    gx_uintptr arrayData;
};

// The allocation experiment uses a bounded, image-backed region. The mapped
// image loader provides zero-filled writable BSS for this storage; no Windows
// virtual-memory call is on the managed allocation path.
__declspec(align(16)) unsigned char g_guideXosManagedHeap[kManagedHeapBytes];
extern "C" volatile GuideXosAllocationDiagnostics g_guideXosAllocationDiagnostics = {};
extern "C" void* __cdecl guideXosStockRhpNewArray(void* eeType, gx_size length);
#endif

// These symbols are provided by the NativeAOT image. They are data symbols,
// not Server object layouts. The loader writes _tls_index before entry.
extern "C" gx_uint32 _tls_index;
extern "C" gx_uint32 g_flsIndex = kGuideXosFlsIndex;

volatile gx_uint32 g_guideXosRuntimeStartupState = 0;

[[noreturn]] void guideXosFailFast(gx_uint32 reason) {
    (void)reason;
    __fastfail(kFastFailFatalAppExit);
    for (;;) {
    }
}

unsigned char* currentTlsBlock() {
    void** vector = reinterpret_cast<void**>(__readgsqword(kTlsVectorOffset));
    if (vector == nullptr || _tls_index == kFlsOutOfIndexes) {
        return nullptr;
    }
    return reinterpret_cast<unsigned char*>(vector[_tls_index]);
}

void** flsCell(unsigned char* block, gx_uint32 index) {
    if (block == nullptr || index >= kFlsCellCount) {
        return nullptr;
    }
    return reinterpret_cast<void**>(block + kFlsCellOffset + (index * sizeof(void*)));
}

unsigned char* runtimeCell(unsigned char* block) {
    if (block == nullptr) {
        return nullptr;
    }
    return block + kRuntimeCellOffset;
}

void initializeRuntimeState(unsigned char* block) {
    if (block == nullptr) {
        guideXosFailFast(1u);
    }

    if (g_guideXosRuntimeStartupState == 0) {
        // This is the guideXOS runtime-pack's deterministic FLS namespace.
        // It is not a Windows FLS slot and is never shared across threads.
        g_flsIndex = kGuideXosFlsIndex;
        g_guideXosRuntimeStartupState = 1;
    }

    // The current image's TLS block is allocated by the host from the
    // generated _tls_start/_tls_end envelope. The minimum is checked by the
    // staging/build scripts; these offsets are within the 0x110-byte proof
    // block. The cell points into that same per-thread block, so no global
    // Thread object or host C++ object layout crosses the boundary.
    unsigned char* cell = runtimeCell(block);
    if (*reinterpret_cast<gx_uint32*>(cell + kRuntimeCellInitializedOffset) == 0u) {
#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
        // RhpNewArray/RhpNewFast consume the current thread's allocation
        // context at TLS block + 0x30: pointer at +0 and limit at +8.
        // This is a bounded no-collection allocation context, not a GC heap.
        for (gx_size i = 0; i < kManagedHeapBytes; ++i) {
            g_guideXosManagedHeap[i] = 0;
        }
        const gx_uintptr heapBase = reinterpret_cast<gx_uintptr>(g_guideXosManagedHeap);
        const gx_uintptr heapLimit = heapBase + kManagedHeapBytes;
        *reinterpret_cast<void**>(cell) = reinterpret_cast<void*>(heapBase);
        *reinterpret_cast<void**>(cell + sizeof(void*)) = reinterpret_cast<void*>(heapLimit);
        g_guideXosAllocationDiagnostics.heapInitialized = 1u;
        g_guideXosAllocationDiagnostics.heapBase = heapBase;
        g_guideXosAllocationDiagnostics.heapSize = kManagedHeapBytes;
        g_guideXosAllocationDiagnostics.initialAllocationPointer = heapBase;
        g_guideXosAllocationDiagnostics.allocationPointerAfter = heapBase;
#else
        *reinterpret_cast<void**>(cell) = cell;
#endif
        *reinterpret_cast<gx_uint32*>(cell + kRuntimeCellInitializedOffset) = 1u;

        void** value = flsCell(block, g_flsIndex);
        if (value != nullptr && *value == nullptr) {
            *value = cell;
        }
    }
}

} // namespace

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
extern "C" __declspec(noinline) int __cdecl guideXosManagedArrayHostLog(void* context, void* arrayObject);
// HostLogProof's generated NativeAOT P/Invoke slot is intentionally bound by
// the application-scoped runtime pack. The ELF loader does not run the Windows
// module resolver that would normally populate this slot.
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedArrayHostLog__Ansi;
#endif

#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
extern "C" __declspec(noinline) void* __cdecl RhpNewArray(void* eeType, gx_size length) {
    if (eeType == nullptr || length > 0x7FFFFFFFu) {
        g_guideXosAllocationDiagnostics.outOfMemory = 1u;
        guideXosFailFast(6u);
    }

    const auto* typeBytes = reinterpret_cast<const unsigned char*>(eeType);
    const gx_size componentSize = *reinterpret_cast<const gx_uint16*>(typeBytes);
    const gx_size baseSize = *reinterpret_cast<const gx_uint32*>(typeBytes + sizeof(gx_uint32));
    if (componentSize != 0u && length > ((~static_cast<gx_size>(0)) - baseSize - 7u) / componentSize) {
        g_guideXosAllocationDiagnostics.outOfMemory = 1u;
        guideXosFailFast(6u);
    }

    const gx_size unalignedObjectSize = baseSize + (componentSize * length);
    const gx_size objectSize = (unalignedObjectSize + 7u) & ~static_cast<gx_size>(7u);
    g_guideXosAllocationDiagnostics.requestedArrayLength = static_cast<gx_uint32>(length);
    g_guideXosAllocationDiagnostics.requestedObjectSize = static_cast<gx_uint32>(objectSize);

    unsigned char* block = currentTlsBlock();
    unsigned char* cell = runtimeCell(block);
    if (cell == nullptr) {
        g_guideXosAllocationDiagnostics.outOfMemory = 1u;
        guideXosFailFast(6u);
    }
    const gx_uintptr allocationPointer = reinterpret_cast<gx_uintptr>(*reinterpret_cast<void**>(cell));
    const gx_uintptr allocationLimit = reinterpret_cast<gx_uintptr>(*reinterpret_cast<void**>(cell + sizeof(void*)));
    if (allocationPointer == 0u || allocationLimit < allocationPointer || objectSize > allocationLimit - allocationPointer) {
        g_guideXosAllocationDiagnostics.outOfMemory = 1u;
        guideXosFailFast(6u);
    }

    void* result = guideXosStockRhpNewArray(eeType, length);
    g_guideXosAllocationDiagnostics.allocationCount += 1u;
    g_guideXosAllocationDiagnostics.returnedObject = reinterpret_cast<gx_uintptr>(result);
    g_guideXosAllocationDiagnostics.arrayData = reinterpret_cast<gx_uintptr>(result) + kManagedArrayDataOffset;
    g_guideXosAllocationDiagnostics.allocationPointerAfter = reinterpret_cast<gx_uintptr>(*reinterpret_cast<void**>(cell));
    if (result == nullptr) {
        g_guideXosAllocationDiagnostics.outOfMemory = 1u;
        guideXosFailFast(6u);
    }
    return result;
}

struct GuideXosNativeHostCallTable {
    gx_uint32 size;
    gx_uint32 version;
    int (__cdecl* log)(void* context, unsigned char* message);
};

struct GuideXosNativeAppContext {
    gx_uint32 size;
    gx_uint32 apiVersion;
    GuideXosNativeHostCallTable* host;
    void* userData;
};

// This is an application-scoped runtime helper. The managed object reference
// never crosses the guideXOS host ABI; only the computed byte-data pointer does.
extern "C" __declspec(noinline) int __cdecl guideXosManagedArrayHostLog(void* context, void* arrayObject) {
    auto* appContext = reinterpret_cast<GuideXosNativeAppContext*>(context);
    if (appContext == nullptr || appContext->host == nullptr || appContext->host->log == nullptr || arrayObject == nullptr) {
        guideXosFailFast(7u);
    }
    auto* arrayData = reinterpret_cast<unsigned char*>(arrayObject) + kManagedArrayDataOffset;
    return appContext->host->log(context, arrayData);
}
#endif

extern "C" __declspec(noinline) void* __cdecl FlsGetValue(gx_uint32 index) {
    if (g_guideXosRuntimeStartupState == 0 || index == kFlsOutOfIndexes) {
        return nullptr;
    }
    return flsCell(currentTlsBlock(), index) == nullptr
        ? nullptr
        : *flsCell(currentTlsBlock(), index);
}

extern "C" __declspec(noinline) int __cdecl FlsSetValue(gx_uint32 index, void* value) {
    if (g_guideXosRuntimeStartupState == 0) {
        return 0;
    }
    void** cell = flsCell(currentTlsBlock(), index);
    if (cell == nullptr) {
        return 0;
    }
    *cell = value;
    return 1;
}

// The NativeAOT image references the import-pointer spelling. Defining the
// pointer in the runtime-pack object binds the call to the guideXOS functions
// above and prevents a live KERNEL32 import thunk from being emitted for this
// transition.
extern "C" __declspec(selectany) void* __imp_FlsGetValue = reinterpret_cast<void*>(&FlsGetValue);
extern "C" __declspec(selectany) void* __imp_FlsSetValue = reinterpret_cast<void*>(&FlsSetValue);

extern "C" __declspec(noinline) void __cdecl RhpReversePInvoke(void* frame) {
    unsigned char* block = currentTlsBlock();
    if (frame == nullptr || block == nullptr || _tls_index == kFlsOutOfIndexes) {
        guideXosFailFast(2u);
    }

    initializeRuntimeState(block);
#if defined(GUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION)
    // Bind the one experimental __Internal P/Invoke slot after the reverse
    // transition has established the current thread state. This is not a
    // general P/Invoke resolver; it is the app-scoped allocation proof hook.
    using GuideXosManagedArrayHostLogFn = int (__cdecl*)(void*, void*);
    __pinvoke_HostLogProof__Module____Internal__guideXosManagedArrayHostLog__Ansi = reinterpret_cast<void*>(static_cast<GuideXosManagedArrayHostLogFn>(guideXosManagedArrayHostLog));
#endif
    unsigned char* cell = runtimeCell(block);
    void** transitionFrame = reinterpret_cast<void**>(frame);
    transitionFrame[0] = *reinterpret_cast<void**>(cell + kRuntimeCellTransitionFrameOffset);
    transitionFrame[1] = reinterpret_cast<void*>(cell);
    *reinterpret_cast<void**>(cell + kRuntimeCellTransitionFrameOffset) = frame;
}

extern "C" __declspec(noinline) void __cdecl RhpReversePInvokeReturn(void* frame) {
    if (frame == nullptr) {
        guideXosFailFast(3u);
    }
    void** transitionFrame = reinterpret_cast<void**>(frame);
    unsigned char* cell = reinterpret_cast<unsigned char*>(transitionFrame[1]);
    if (cell == nullptr) {
        guideXosFailFast(4u);
    }
    *reinterpret_cast<void**>(cell + kRuntimeCellTransitionFrameOffset) = transitionFrame[0];
}

extern "C" __declspec(noinline) void __cdecl RhpReversePInvoke2(void* frame) {
    RhpReversePInvoke(frame);
}

extern "C" __declspec(noinline) void __cdecl RhpReversePInvokeReturn2(void* frame) {
    RhpReversePInvokeReturn(frame);
}

extern "C" __declspec(noinline) void __cdecl RhpFallbackFailFast() {
    guideXosFailFast(5u);
}
