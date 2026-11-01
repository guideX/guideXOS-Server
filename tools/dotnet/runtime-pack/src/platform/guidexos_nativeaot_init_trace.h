#pragma once

#include <stdint.h>

#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS) || \
    defined(GXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS)
enum guidexos_nativeaot_init_stage : uint32_t {
    GX_INIT_STAGE_ADAPTER = 0x0001u,
    GX_INIT_STAGE_RH_INITIALIZE = 0x0002u,
    GX_INIT_STAGE_PAL_INIT = 0x0010u,
    GX_INIT_STAGE_PAL_FLS = 0x0011u,
    GX_INIT_STAGE_GC_CONFIG = 0x0012u,
    GX_INIT_STAGE_GC_TO_OS = 0x0013u,
    GX_INIT_STAGE_INIT_DLL = 0x0020u,
    GX_INIT_STAGE_INTERFACE_DISPATCH = 0x0021u,
    GX_INIT_STAGE_GC_EVENT_LOCK = 0x0022u,
    GX_INIT_STAGE_RESTRICTED_CALLOUTS = 0x0023u,
    GX_INIT_STAGE_RUNTIME_INSTANCE = 0x0024u,
    GX_INIT_STAGE_MODULE_IDENTITY = 0x0025u,
    GX_INIT_STAGE_STARTUP_IMPORT_LOOKUP = 0x0026u,
    GX_INIT_STAGE_DEBUG_HEADERS = 0x0027u,
    GX_INIT_STAGE_RUNTIME_INSTANCE_ALLOC = 0x0028u,
    GX_INIT_STAGE_THREAD_STORE_CREATE = 0x0029u,
    GX_INIT_STAGE_RUNTIME_INSTANCE_PUBLISH = 0x002Au,
    GX_INIT_STAGE_GC_INITIALIZE = 0x0030u,
    GX_INIT_STAGE_GC_HEAP_PUBLICATION = 0x0031u,
    GX_INIT_STAGE_GC_HEAP_INITIALIZE = 0x0032u,
    GX_INIT_STAGE_GC_FINALIZATION = 0x0033u,
    GX_INIT_STAGE_GC_HANDLE_MANAGER = 0x0034u,
    GX_INIT_STAGE_CPU_FEATURES = 0x0040u,
    GX_INIT_STAGE_GC_EVENT_CREATE = 0x0050u,
    GX_INIT_STAGE_GC_VM_RESERVE = 0x0051u,
    GX_INIT_STAGE_GC_VM_COMMIT = 0x0052u,
    GX_INIT_STAGE_GC_VM_DECOMMIT = 0x0053u,
    GX_INIT_STAGE_GC_VM_RELEASE = 0x0054u,
    GX_INIT_STAGE_GC_VM_ADAPTER_INIT = 0x0055u,
    GX_INIT_STAGE_MANAGED_REVERSE_PINVOKE = 0x0060u,
    GX_INIT_STAGE_MODULE_REGISTRATION = 0x0061u,
    GX_INIT_STAGE_CODE_MANAGER_REGISTRATION = 0x0062u,
    GX_INIT_STAGE_MODULE_INITIALIZE = 0x0063u,
    GX_INIT_STAGE_GC_INTERNAL_RECORD = 0x0070u,
    GX_INIT_STAGE_GC_SEMISHARED_RECORD = 0x0071u,
};

enum guidexos_nativeaot_init_event : uint32_t {
    GX_INIT_EVENT_BEGIN = 1u,
    GX_INIT_EVENT_PASS = 2u,
    GX_INIT_EVENT_FAIL = 3u,
    GX_INIT_EVENT_OBSERVE = 4u,
};

typedef void (__cdecl *guidexos_nativeaot_init_trace_sink)(
    uint32_t invocation, uint32_t stage, uint32_t event,
    uint32_t result, uint32_t singleton_bits);

extern "C" void __cdecl guideXosNativeAotInitTraceRecord(
    uint32_t stage, uint32_t event, uint32_t result,
    uint32_t singleton_bits);
extern "C" void __cdecl guideXosNativeAotInitTraceSetInvocation(uint32_t invocation);
extern "C" __declspec(dllexport) void __cdecl guideXosNativeAotInitTraceSetSink(
    guidexos_nativeaot_init_trace_sink sink);

inline void guideXosNativeAotInitTraceBegin(uint32_t stage) {
    guideXosNativeAotInitTraceRecord(stage, GX_INIT_EVENT_BEGIN, 0xFFFFFFFFu,
                                     0xFFFFFFFFu);
}
inline void guideXosNativeAotInitTraceReturn(uint32_t stage, bool success,
                                             uint32_t result = 0u,
                                             uint32_t singleton_bits = 0xFFFFFFFFu) {
    guideXosNativeAotInitTraceRecord(stage,
        success ? GX_INIT_EVENT_PASS : GX_INIT_EVENT_FAIL,
        success ? result : (result == 0u ? 1u : result), singleton_bits);
}
#endif
