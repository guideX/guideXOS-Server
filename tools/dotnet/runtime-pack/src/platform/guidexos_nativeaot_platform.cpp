#include <intrin.h>

#pragma intrinsic(__readgsqword)

namespace {

using gx_uintptr = unsigned __int64;
using gx_uint32 = unsigned long;

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
        *reinterpret_cast<void**>(cell) = cell;
        *reinterpret_cast<gx_uint32*>(cell + kRuntimeCellInitializedOffset) = 1u;

        void** value = flsCell(block, g_flsIndex);
        if (value != nullptr && *value == nullptr) {
            *value = cell;
        }
    }
}

} // namespace

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
